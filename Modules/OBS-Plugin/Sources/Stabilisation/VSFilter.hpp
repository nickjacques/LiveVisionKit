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

#pragma once

#include <LiveVisionKit.hpp>

#include "Interop/VisionFilter.hpp"

namespace lvk
{

	class VSFilter : public VisionFilter
	{
	public:

		static obs_properties_t* Properties();

		static void LoadDefaults(obs_data_t* settings);

		explicit VSFilter(obs_source_t* context);

		void configure(obs_data_t* settings);

		bool validate() const;

	private:

        void filter(FrameBuffer& buffer) override;

		void draw_debug_hud(cv::UMat& frame);

	private:
		obs_source_t* m_Context = nullptr;

		StabilizationFilter m_Filter;
		bool m_TestMode = false;
	};

}
