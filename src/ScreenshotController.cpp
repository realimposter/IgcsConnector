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
#include "stdafx.h"
#include "ScreenshotController.h"
#include "CameraToolsConnector.h"
#include <direct.h>
#include "OverlayControl.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "std_image_write.h"
#include "Utils.h"
#include <thread>
#include <random>

#include "fpng.h"

ScreenshotController::ScreenshotController(CameraToolsConnector& connector) : _cameraToolsConnector(connector)
{
}


void ScreenshotController::configure(std::string rootFolder, int numberOfFramesToWaitBetweenSteps, ScreenshotFiletype filetype)
{
	if (_state != ScreenshotControllerState::Off)
	{
		// Configure can't be called when a screenhot is in progress. ignore
		return;
	}
	reset();

	_rootFolder = rootFolder;
	_numberOfFramesToWaitBetweenSteps = numberOfFramesToWaitBetweenSteps;
	_filetype = filetype;
}


bool ScreenshotController::shouldTakeShot()
{
	if (_convolutionFrameCounter > 0)
	{
		// always false as we're still waiting
		return false;
	}
	return _state == ScreenshotControllerState::InSession;
}


void ScreenshotController::presentCalled()
{
	if (_convolutionFrameCounter > 0)
	{
		_convolutionFrameCounter--;
	}
}


void ScreenshotController::reshadeEffectsRendered(reshade::api::effect_runtime* runtime)
{
	if(_state!=ScreenshotControllerState::InSession)
	{
		return;
	}
	if(shouldTakeShot())
	{
		// take a screenshot
		runtime->get_screenshot_width_and_height(&_framebufferWidth, &_framebufferHeight);
		std::vector<uint8_t> shotData(_framebufferWidth * _framebufferHeight * 4);
		runtime->capture_screenshot(shotData.data());

		// as alpha is 0 anyway, we pack the RGBA data as RGB data. This is faster than setting all alpha channels to FF.
		// From Reshade
		for(int i = 0; i < _framebufferWidth * _framebufferHeight; ++i)
		{
			*reinterpret_cast<uint32_t*>(shotData.data() + 3 * i) = *reinterpret_cast<const uint32_t*>(shotData.data() + 4 * i);
		}
		storeGrabbedShot(shotData);
	}
}


void ScreenshotController::cancelSession()
{
	switch(_state)
	{
	case ScreenshotControllerState::Off: 
		return;
	case ScreenshotControllerState::InSession:
		_cameraToolsConnector.endScreenshotSession();
		_state = ScreenshotControllerState::Canceling;
		// kill the wait thread
		_waitCompletionHandle.notify_all();
		break;
	case ScreenshotControllerState::SavingShots:
		_state = ScreenshotControllerState::Canceling;
		break;
	}
}


void ScreenshotController::completeShotSession()
{
	const std::string shotTypeDescription = typeOfShotAsString();
	// we'll wait now till all the shots are taken. 
	waitForShots();
	if(_state != ScreenshotControllerState::Canceling)
	{
		if(_isTestRun)
		{
			OverlayControl::addNotification("Test run completed.");
		}
		else
		{
			OverlayControl::addNotification("All " + shotTypeDescription + " shots have been taken. Writing shots to disk...");
			saveGrabbedShots();
			OverlayControl::addNotification(shotTypeDescription + " done.");
		}
	}
	// done
	reset();
}


void ScreenshotController::displayScreenshotSessionStartError(const ScreenshotSessionStartReturnCode sessionStartResult)
{
	std::string reason = "Unknown error.";
	switch(sessionStartResult)
	{
	case ScreenshotSessionStartReturnCode::Error_CameraNotEnabled:
		reason = "you haven't enabled the camera.";
		break;
	case ScreenshotSessionStartReturnCode::Error_CameraPathPlaying:
		reason = "there's a camera path playing.";
		break;
	case ScreenshotSessionStartReturnCode::Error_AlreadySessionActive:
		reason = "there's already a session active.";
		break;
	case ScreenshotSessionStartReturnCode::Error_CameraFeatureNotAvailable:
		reason = "the camera feature isn't available in the tools.";
		break;
	}
	OverlayControl::addNotification("Screenshot session couldn't be started: " + reason);
}


bool ScreenshotController::startSession()
{
	uint8_t typeOfShotToUse = (uint8_t)_typeOfShot;
#ifdef _DEBUG
	if(_typeOfShot==ScreenshotType::DebugGrid)
	{
		typeOfShotToUse = (uint8_t)ScreenshotType::MultiShot;
	}
#endif

	const auto sessionStartResult = _cameraToolsConnector.startScreenshotSession(typeOfShotToUse);
	if(sessionStartResult != ScreenshotSessionStartReturnCode::AllOk)
	{
		displayScreenshotSessionStartError(sessionStartResult);
		return false;
	}
	return true;
}


void ScreenshotController::startHorizontalPanoramaShot(float totalFoVInDegrees, float overlapPercentagePerPanoShot, float currentFoVInDegrees, bool isTestRun)
{
	if(!_cameraToolsConnector.cameraToolsConnected())
	{
		return;
	}

	reset();

	// the fov passed in is in degrees as well as the total fov, we change that to radians as the tools camera works with radians.
	const float currentFoVInRadians = IGCS::Utils::degreesToRadians(currentFoVInDegrees);
	_pano_totalFoVRadians = IGCS::Utils::degreesToRadians(totalFoVInDegrees);
	_overlapPercentagePerPanoShot = overlapPercentagePerPanoShot;
	_pano_currentFoVRadians = currentFoVInRadians;
	_typeOfShot = ScreenshotType::HorizontalPanorama;
	_isTestRun = isTestRun;
	// panos are rotated from the far left to the far right of the total fov, where at the start, the center of the screen is rotated to the far left of the total fov, 
	// till the center of the screen hits the far right of the total fov. This is done because panorama stitching can often lead to corners not being used, so an overlap
	// on either side is preferable.

	// calculate the angle to step
	_pano_anglePerStep = currentFoVInRadians * ((100.0f-overlapPercentagePerPanoShot) / 100.0f);
	// calculate the # of shots to take
	_numberOfShotsToTake = ((_pano_totalFoVRadians / _pano_anglePerStep) + 1);

	// tell the camera tools we're starting a session.
	if(!startSession())
	{
		return;
	}
	
	// move to start
	moveCameraForPanorama(-1, true);

	// set convolution counter to its initial value
	_convolutionFrameCounter = _numberOfFramesToWaitBetweenSteps;
	_state = ScreenshotControllerState::InSession;

	// Create a thread which will handle the end of the shot session as the shot taking is done by event handlers
	std::thread t(&ScreenshotController::completeShotSession, this);
	t.detach();
}


void ScreenshotController::startLightfieldShot(float distancePerStep, int numberOfShots, bool isTestRun)
{
	if(!_cameraToolsConnector.cameraToolsConnected())
	{
		return;
	}

	reset();
	_isTestRun = isTestRun;
	_lightField_distancePerStep = distancePerStep;
	_numberOfShotsToTake = numberOfShots;
	_typeOfShot = ScreenshotType::MultiShot;

	// tell the camera tools we're starting a session.
	if(!startSession())
	{
		return;
	}

	// move to start
	moveCameraForLightfield(-1, true);
	// set convolution counter to its initial value
	_convolutionFrameCounter = _numberOfFramesToWaitBetweenSteps;
	_state = ScreenshotControllerState::InSession;

	// Create a thread which will handle the end of the shot session as the shot taking is done by event handlers
	std::thread t(&ScreenshotController::completeShotSession, this);
	t.detach();
}


void ScreenshotController::startDebugGridShot()
{
	if(!_cameraToolsConnector.cameraToolsConnected())
	{
		return;
	}

	// debug grid is basically a 5 horizontal, 3 row grid where the camera is moved 10 positions to the right and then down, then to the left then down and to the right again.

	reset();
	_isTestRun = true;
	_lightField_distancePerStep = 10;
	_numberOfShotsToTake = 15;
	_typeOfShot = ScreenshotType::DebugGrid;

	// tell the camera tools we're starting a session.
	if(!startSession())
	{
		return;
	}

	// move to start
	moveCameraForDebugGrid(-1, true);
	// set convolution counter to its initial value
	_convolutionFrameCounter = _numberOfFramesToWaitBetweenSteps;
	_state = ScreenshotControllerState::InSession;

	// Create a thread which will handle the end of the shot session as the shot taking is done by event handlers
	std::thread t(&ScreenshotController::completeShotSession, this);
	t.detach();
}


void ScreenshotController::startMultiViewShot(int numberOfShots, bool isTestRun)
{
	if(!_cameraToolsConnector.cameraToolsConnected())
	{
		return;
	}

	reset();
	_isTestRun = isTestRun;
	_numberOfShotsToTake = numberOfShots;
	_typeOfShot = ScreenshotType::MultiView;

	// tell the camera tools we're starting a session.
	if(!startSession())
	{
		return;
	}

	// set convolution counter to its initial value
	_convolutionFrameCounter = _numberOfFramesToWaitBetweenSteps;
	_state = ScreenshotControllerState::InSession;

	// Create a thread which will handle the end of the shot session as the shot taking is done by event handlers
	std::thread t(&ScreenshotController::completeShotSession, this);
	t.detach();
}


std::string ScreenshotController::createScreenshotFolder()
{
	time_t t = time(nullptr);
	tm tm;
	localtime_s(&tm, &t);
	const std::string optionalBackslash = (_rootFolder.ends_with('\\')) ? "" : "\\";
	std::string folderName = IGCS::Utils::formatString("%s%s%s-%.4d-%.2d-%.2d-%.2d-%.2d-%.2d", _rootFolder.c_str(), optionalBackslash.c_str(), typeOfShotAsString().c_str(), 
													   (tm.tm_year + 1900), (tm.tm_mon + 1), tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	_mkdir(folderName.c_str());
	return folderName;
}


void ScreenshotController::modifyCamera()
{
	// based on the type of the shot, we'll either rotate or move.
	switch (_typeOfShot)
	{
	case ScreenshotType::HorizontalPanorama:
		moveCameraForPanorama(1, false);
		break;
	case ScreenshotType::MultiShot:
		moveCameraForLightfield(1, false);
		break;
	case ScreenshotType::MultiView:
		moveCameraForMultiView();
		break;
#ifdef _DEBUG
	case ScreenshotType::DebugGrid:
		moveCameraForDebugGrid(_shotCounter, false);
		break;
#endif

	}
}


std::string ScreenshotController::typeOfShotAsString()
{
	// based on the type of the shot, we'll either rotate or move.
	switch(_typeOfShot)
	{
	case ScreenshotType::HorizontalPanorama:
		return "HorizontalPanorama";
	case ScreenshotType::MultiShot:
		return "Lightfield";
	case ScreenshotType::MultiView:
		return "MultiView";
#ifdef _DEBUG
	case ScreenshotType::DebugGrid:
		return "DebugGrid";
#endif
		
	}
	return "";
}


void ScreenshotController::moveCameraForLightfield(int direction, bool end)
{
	float distance = direction * _lightField_distancePerStep;
	if (end)
	{
		distance *= 0.5f * _numberOfShotsToTake;
	}
	// we don't know the movement speed, so we pass the distance to the camera, and the camere has to divide by movement speed so it's independent of movement speed.
	// we don't move up/down so we pass in 0. We don't change the fov and the step is relative to the current camera location.
	_cameraToolsConnector.moveCameraMultishot(distance, 0.0f, 0.0f, false);
}


void ScreenshotController::moveCameraForPanorama(int direction, bool end)
{
	float distance = direction * _pano_anglePerStep;
	if (end)
	{
		distance *= 0.5f * _numberOfShotsToTake;
	}
	// we don't know the movement speed, so we pass the distance to the camera, and the camere has to divide by movement speed so it's independent of movement speed.
	_cameraToolsConnector.moveCameraPanorama(distance);
}


void ScreenshotController::moveCameraForDebugGrid(int shotCounter, bool end)
{
	float horizontalStep = 0.0f;
	float verticalStep = 0.0f;
	if(end)
	{
		horizontalStep = -20.0f;
		verticalStep = -20.0f;
	}
	else
	{
		verticalStep = (shotCounter / 5);
		verticalStep = (-20 + 10 * verticalStep);
		horizontalStep = (shotCounter % 5);
		horizontalStep = -20 + 10 * horizontalStep;
	}
	// we don't know the movement speed, so we pass the distance to the camera, and the camere has to divide by movement speed so it's independent of movement speed.
	// we don't move up/down so we pass in 0. We don't change the fov and the step is relative to the current camera location.
	_cameraToolsConnector.moveCameraMultishot(horizontalStep, verticalStep, (shotCounter % 5) * 10.0f, true);
}

void ScreenshotController::moveCameraForMultiView()
{
	// Generate random positions and angles relative to the current camera position
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<> dis(-10.0, 10.0);

	float randomX = dis(gen);
	float randomY = dis(gen);
	float randomZ = dis(gen);
	float randomPitch = dis(gen);
	float randomYaw = dis(gen);

	// Move the camera to the new random position and angle
	_cameraToolsConnector.moveCameraMultishot(randomX, randomY, randomZ, false);
	_cameraToolsConnector.rotateCamera(randomPitch, randomYaw, 0.0f);
}


void ScreenshotController::storeGrabbedShot(std::vector<uint8_t> grabbedShot)
{
	if(grabbedShot.size() <= 0)
	{
		// failed
		return;
	}

	_grabbedFrames.push_back(grabbedShot);
	_shotCounter++;
	if(_shotCounter >= _numberOfShotsToTake)
	{
		// we're done. Move to the next state, which is saving shots. 
		_state = ScreenshotControllerState::SavingShots;
		// tell the waiting thread to wake up so the system can proceed as normal.
		_waitCompletionHandle.notify_all();
	}
	else
	{
		modifyCamera();
		_convolutionFrameCounter = _numberOfFramesToWaitBetweenSteps;
	}
}


void ScreenshotController::saveGrabbedShots()
{
	if(_grabbedFrames.size() <= 0)
	{
		return;
	}
	if(!_isTestRun)
	{
		_state = ScreenshotControllerState::SavingShots;
		const std::string destinationFolder = createScreenshotFolder();
		int frameNumber = 0;
		for(const std::vector<uint8_t>& frame : _grabbedFrames)
		{
			saveShotToFile(destinationFolder, frame, frameNumber);
			frameNumber++;
		}
	}
}


void ScreenshotController::saveShotToFile(std::string destinationFolder, const std::vector<uint8_t>& data, int frameNumber)
{
	std::string filename = "";

	// The shot data is RGB as we packed the RGBA data as RGB as Alpha is 0 in the source. So we pass 3 as the comp
	switch(_filetype)
	{
	case ScreenshotFiletype::Bmp:
		filename = IGCS::Utils::formatString("%s\\%d.bmp", destinationFolder.c_str(), frameNumber);
		stbi_write_bmp(filename.c_str(), _framebufferWidth, _framebufferHeight, 3, data.data()) != 0;
		break;
	case ScreenshotFiletype::Jpeg:
		filename = IGCS::Utils::formatString("%s\\%d.jpg", destinationFolder.c_str(), frameNumber);
		stbi_write_jpg(filename.c_str(), _framebufferWidth, _framebufferHeight, 3, data.data(), 98) != 0;
		break;
	case ScreenshotFiletype::Png:
		filename = IGCS::Utils::formatString("%s\\%d.png", destinationFolder.c_str(), frameNumber);
		// 3 bytes per pixel!
		//stbi_write_png(filename.c_str(), _framebufferWidth, _framebufferHeight, 3, data.data(), 3 * _framebufferWidth) != 0;
		std::vector<uint8_t> encoded_data;
		fpng::fpng_encode_image_to_memory(data.data(), _framebufferWidth, _framebufferHeight, 3, encoded_data);
		FILE* pngFile;
		if(fopen_s(&pngFile, filename.c_str(), "wb")==0)
		{
			fwrite(encoded_data.data(), encoded_data.size(), 1, pngFile);
		}
		if(nullptr != pngFile)
		{
			fclose(pngFile);
		}
		break;
	}
}


void ScreenshotController::waitForShots()
{
	std::unique_lock lock(_waitCompletionMutex);
	_waitCompletionHandle.wait(lock, [this] {return _state != ScreenshotControllerState::InSession; });
	// state isn't in-session, we're notified so we're all goed to save the shots.
	// signal the tools the session ended.
	_cameraToolsConnector.endScreenshotSession();
}


void ScreenshotController::reset()
{
	// don't reset framebuffer width/height, numberOfFramesToWaitBetweenSteps, movementSpeed, 
	// rotationSpeed, rootFolder as those are set through configure!
	_typeOfShot = ScreenshotType::HorizontalPanorama;
	_state = ScreenshotControllerState::Off;
	_pano_totalFoVRadians = 0.0f;
	_pano_currentFoVRadians = 0.0f;
	_lightField_distancePerStep = 0.0f;
	_pano_anglePerStep = 0.0f;
	_numberOfShotsToTake = 0;
	_convolutionFrameCounter = 0;
	_shotCounter = 0;
	_overlapPercentagePerPanoShot = 30.0f;
	_isTestRun = false;
	_grabbedFrames.clear();
}
