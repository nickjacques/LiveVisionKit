//    *************************** LiveVisionKit ****************************
//    Copyright (C) 2022  Sebastian Di Marco (crowsinc.dev@gmail.com)
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.
// 	  **********************************************************************

#include "DeblockingFilter.hpp"

#include "Functions/Drawing.hpp"

namespace lvk
{

//---------------------------------------------------------------------------------------------------------------------

	DeblockingFilter::DeblockingFilter(DeblockingFilterSettings settings)
		: VideoFilter("Deblocking Filter")
	{
        configure(settings);
	}

//---------------------------------------------------------------------------------------------------------------------

    void lvk::DeblockingFilter::configure(const DeblockingFilterSettings& settings)
    {
        LVK_ASSERT(settings.block_size > 0);
        LVK_ASSERT(settings.filter_size >= 3);
        LVK_ASSERT(settings.filter_size % 2 == 1);
        LVK_ASSERT(settings.detection_levels > 0);
        LVK_ASSERT(settings.filter_scaling > 1.0f);

        m_Settings = settings;
    }

//---------------------------------------------------------------------------------------------------------------------

    void DeblockingFilter::filter(
        Frame&& input,
        Frame& output,
        Stopwatch& timer,
        const bool debug
    )
	{
        LVK_ASSERT(!input.is_empty());

		// NOTE: De-blocking is achieved by adaptively blending a median smoothed
		// frame with the original. Filtering occurs on a downscaled frame to boost
		// its performance and effective area. Blend maps are made by comparing the
		// original frame with a reference maximal blocking artifact frame, created
		// by simplifying each block to its average value. Not all frame resolutions
		// fit an integer number of macroblocks, so the frame must be padded or cropped.
		// Both these techniques lead to approximately the same result, so cropping
		// is preferred for performance. Blocks are assumed to be safe to smooth if
		// they are similar, by threshold,to the reference blocks. To make the choice
		// of threshold less strict for the user; multiple thresholds are used, each
		// with their own weighting that increases as details become stronger.

		const int macroblock_size = static_cast<int>(m_Settings.block_size);
		const cv::Size macroblock_extent = input.size() / macroblock_size;
		const cv::Rect macroblock_region({0,0}, macroblock_extent * macroblock_size);

		// Resolutions such as 1920x1080 may not be evenly divisible by macroblocks.
		// We ignore areas containing partial blocks by applying the filter on only
		// the region of the frame which consists of only full macroblocks.
		cv::UMat filter_region = input.data(macroblock_region);

		// Generate smooth frame
		const float area_scaling = 1.0f / m_Settings.filter_scaling;
		cv::resize(filter_region, m_DeblockBuffer, cv::Size(), area_scaling, area_scaling, cv::INTER_AREA);
		cv::medianBlur(m_DeblockBuffer, m_DeblockBuffer, static_cast<int>(m_Settings.filter_size));
		cv::resize(m_DeblockBuffer, m_SmoothFrame, macroblock_region.size(), 0, 0, cv::INTER_LINEAR);

		// Generate reference frame
		cv::extractChannel(filter_region, m_DetectionFrame, 0);
		cv::resize(m_DetectionFrame, m_BlockGrid, macroblock_extent, 0, 0, cv::INTER_AREA);
		cv::resize(m_BlockGrid, m_ReferenceFrame, m_DetectionFrame.size(), 0, 0, cv::INTER_NEAREST);
		cv::absdiff(m_DetectionFrame, m_ReferenceFrame, m_DetectionFrame);
		cv::resize(m_DetectionFrame, m_BlockGrid, macroblock_extent, 0, 0, cv::INTER_AREA);

		// Produce blend maps
		m_FloatBuffer.create(macroblock_extent, CV_32FC1);
		m_FloatBuffer.setTo(cv::Scalar(0.0));

		const double level_step = 1.0 / m_Settings.detection_levels;
		for(int l = 0; l < m_Settings.detection_levels; l++)
		{
			cv::threshold(m_BlockGrid, m_BlockMask, l, 255, cv::THRESH_BINARY);
			m_FloatBuffer.setTo(cv::Scalar((l + 1.0) * level_step), m_BlockMask);
		}

		cv::resize(m_FloatBuffer, m_KeepBlendMap, filter_region.size(), 0, 0, cv::INTER_LINEAR);
		cv::absdiff(m_KeepBlendMap, cv::Scalar(1.0), m_DeblockBlendMap);

		// Set smoothing frame to magenta so that we can see all the detection levels.
		if(debug)
			m_SmoothFrame.setTo(yuv::MAGENTA);

		// Adaptively blend original and smooth frames
		cv::blendLinear(
            filter_region,
            m_SmoothFrame,
            m_KeepBlendMap,
            m_DeblockBlendMap,
            filter_region
		);

        output = std::move(input);
	}

//---------------------------------------------------------------------------------------------------------------------

}
