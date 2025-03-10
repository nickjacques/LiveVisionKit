//     *************************** LiveVisionKit ****************************
//     Copyright (C) 2022  Sebastian Di Marco (crowsinc.dev@gmail.com)
//
//     This program is free software: you can redistribute it and/or modify
//     it under the terms of the GNU General Public License as published by
//     the Free Software Foundation, either version 3 of the License, or
//     (at your option) any later version.
//
//     This program is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU General Public License for more details.
//
//     You should have received a copy of the GNU General Public License
//     along with this program.  If not, see <https://www.gnu.org/licenses/>.
//     **********************************************************************

#include "FrameTracker.hpp"

#include "Directives.hpp"
#include "Math/Homography.hpp"
#include "Functions/Math.hpp"
#include "Functions/Container.hpp"
#include "Functions/Extensions.hpp"

namespace lvk
{

//---------------------------------------------------------------------------------------------------------------------

	constexpr double GOOD_DISTRIBUTION_QUALITY = 0.6;

//---------------------------------------------------------------------------------------------------------------------

	FrameTracker::FrameTracker(const FrameTrackerSettings& settings)
	{
        configure(settings);

		// Light sharpening kernel
		m_FilterKernel = cv::Mat({3, 3}, {
			0.0f, -0.5f,  0.0f,
		   -0.5f,  3.0f, -0.5f,
			0.0f, -0.5f,  0.0f
		});

		restart();
	}

//---------------------------------------------------------------------------------------------------------------------

    void FrameTracker::configure(const FrameTrackerSettings& settings)
    {
        LVK_ASSERT(settings.sample_size_threshold >= 4);
        LVK_ASSERT_01(settings.uniformity_threshold);
        LVK_ASSERT_01(settings.stability_threshold);

        m_FeatureDetector.configure(settings);
        m_TrackedPoints.reserve(m_FeatureDetector.feature_capacity());
        m_MatchedPoints.reserve(m_FeatureDetector.feature_capacity());
        m_InlierStatus.reserve(m_FeatureDetector.feature_capacity());
        m_MatchStatus.reserve(m_FeatureDetector.feature_capacity());

        // If we are tracking motion with a resolution of 2x2 (Homography)
        // then tighten up the homography estimation parameters for global
        // motion. Otherwise, loosen them up to allow local motion through.
        if(settings.motion_resolution == WarpField::MinimumSize)
        {
            // For accurate Homography estimation
            m_USACParams.sampler = cv::SAMPLING_UNIFORM;
            m_USACParams.score = cv::SCORE_METHOD_MAGSAC;
            m_USACParams.loMethod = cv::LOCAL_OPTIM_SIGMA;
            m_USACParams.maxIterations = 100;
            m_USACParams.confidence = 0.99;
            m_USACParams.loIterations = 10;
            m_USACParams.loSampleSize = 20;
            m_USACParams.threshold = 4;
        }
        else
        {
            // For major outlier rejection
            m_USACParams.sampler = cv::SAMPLING_UNIFORM;
            m_USACParams.score = cv::SCORE_METHOD_MSAC;
            m_USACParams.loMethod = cv::LOCAL_OPTIM_INNER_LO;
            m_USACParams.maxIterations = 100;
            m_USACParams.confidence = 0.99;
            m_USACParams.loIterations = 10;
            m_USACParams.loSampleSize = 20;
            m_USACParams.threshold = 20;
        }

        m_Settings = settings;
    }

//---------------------------------------------------------------------------------------------------------------------

	void FrameTracker::restart()
	{
        m_Uniformity = 0.0f;
        m_Stability = 0.0f;

		m_FirstFrame = true;
        m_FeatureDetector.reset();
	}

//---------------------------------------------------------------------------------------------------------------------

    std::optional<WarpField> FrameTracker::track(const cv::UMat& next_frame)
	{
		LVK_ASSERT(!next_frame.empty());
		LVK_ASSERT(next_frame.type() == CV_8UC1);

        // Reset the state to track the next frame
        m_TrackedPoints.clear();
        m_MatchedPoints.clear();

        // Move the last tracked frame to the previous frame.
        std::swap(m_PrevFrame, m_NextFrame);

        // Import the next frame for tracking by scaling it to the tracking resolution.
        // We also enhance its sharpness to counteract the loss in quality from scaling.
        cv::resize(next_frame, m_NextFrame, tracking_resolution(), 0, 0, cv::INTER_AREA);
        cv::filter2D(m_NextFrame, m_NextFrame, m_NextFrame.type(), m_FilterKernel);

        // We need at least two frames for tracking, so exit early on the first frame.
        if(m_FirstFrame)
        {
            m_FirstFrame = false;
            return std::nullopt;
        }


        // Detect tracking points in the previous frames. Note that this also
        // returns all the points that were propagated from the previous frame.
        m_FeatureDetector.detect(m_PrevFrame, m_TrackedPoints);
        m_Uniformity = m_FeatureDetector.distribution_quality();

        if(m_TrackedPoints.size() < m_Settings.sample_size_threshold)
            return abort_tracking();

        if(m_Uniformity < m_Settings.uniformity_threshold)
            return abort_tracking();


		// Match tracking points
		cv::calcOpticalFlowPyrLK(
			m_PrevFrame,
			m_NextFrame,
			m_TrackedPoints,
			m_MatchedPoints,
			m_MatchStatus,
			cv::noArray(),
			cv::Size(7, 7)
		);

		fast_filter(m_TrackedPoints, m_MatchedPoints, m_MatchStatus);
        if(m_MatchedPoints.size() < m_Settings.sample_size_threshold)
            return abort_tracking();


        // NOTE: We force estimation of an affine homography if we have a low
        // tracking point distribution. This is to avoid perspectivity-based
        // distortions due to dominant local motions being applied globally.
        std::optional<Homography> motion;
        motion = Homography::Estimate(
            m_TrackedPoints,
            m_MatchedPoints,
            m_InlierStatus,
            m_USACParams,
            m_Uniformity < GOOD_DISTRIBUTION_QUALITY
        );

        // Filter outliers and propagate the inliers back to the detector.
        fast_filter(m_TrackedPoints, m_MatchedPoints, m_InlierStatus);
        m_FeatureDetector.propagate(m_MatchedPoints);

        // NOTE: Scene stability is measured by the inlier ratio.
        const auto inlier_motions = static_cast<float>(m_MatchedPoints.size());
        const auto motion_samples = static_cast<float>(m_InlierStatus.size());

        m_Stability = inlier_motions / motion_samples;
        if(m_Stability < m_Settings.stability_threshold)
            return abort_tracking();


        // Convert the global Homography into a motion field.
        WarpField motion_field(m_Settings.motion_resolution);
        if(m_Settings.motion_resolution != WarpField::MinimumSize)
        {
            const cv::Rect2f region({0,0}, tracking_resolution());
            motion_field.fit_points(region, m_TrackedPoints, m_MatchedPoints, motion);
        }
        else motion_field.set_to(*motion, tracking_resolution());


        // We must scale the motion to match the original frame size.
        const cv::Size2f frame_scale = next_frame.size();
        const cv::Size2f tracking_scale = tracking_resolution();
        motion_field *= frame_scale / tracking_scale;

        return std::move(motion_field);
	}

//---------------------------------------------------------------------------------------------------------------------

    std::nullopt_t FrameTracker::abort_tracking()
    {
        restart();
        return std::nullopt;
    }

//---------------------------------------------------------------------------------------------------------------------

    float FrameTracker::scene_stability() const
	{
		return m_Stability;
	}

//---------------------------------------------------------------------------------------------------------------------

    float FrameTracker::scene_uniformity() const
    {
        return m_Uniformity;
    }

//---------------------------------------------------------------------------------------------------------------------

    const cv::Size& FrameTracker::motion_resolution() const
    {
        return m_Settings.motion_resolution;
    }

//---------------------------------------------------------------------------------------------------------------------

    const cv::Size& FrameTracker::tracking_resolution() const
    {
        return m_Settings.detect_resolution;
    }

//---------------------------------------------------------------------------------------------------------------------

	const std::vector<cv::Point2f>& FrameTracker::tracking_points() const
	{
		return m_MatchedPoints;
	}

//---------------------------------------------------------------------------------------------------------------------

}
