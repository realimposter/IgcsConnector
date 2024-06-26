///////////////////////////////////////////////////////////////////////
//
// Part of IGCS Connector, an add on for Reshade 5+ which allows you
// to connect IGCS built camera tools with reshade to exchange data and control
// from Reshade.
// 
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/IgcsConnector
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////

#pragma once

#include "ConstantsEnums.h"
#include "stdafx.h"
#include "ShlObj_core.h"

#pragma comment(lib, "shell32.lib")

struct ScreenshotSettings
{
	int typeOfScreenshot = (int)ScreenshotType::MultiView;  // Default to MultiView
	int screenshotFileType = (int)ScreenshotFiletype::Jpeg;
	int numberOfFramesToWaitBetweenSteps = 1;
	float lightField_distanceBetweenShots = 1.0f;
	int lightField_numberOfShotsToTake = 45;
	float pano_totalAngleDegrees = 110.0f;
	float pano_overlapPercentagePerShot = 80.0f;
	int multiView_numberOfShots = 2;  // New setting for MultiView shot
	char screenshotFolder[_MAX_PATH + 1] = { 0 };

	ScreenshotSettings()
	{
		SHGetFolderPathA(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, screenshotFolder);
	}
};
