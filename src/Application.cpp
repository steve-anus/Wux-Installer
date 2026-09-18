/****************************************************************************
 * Copyright (C) 2015 Dimok
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include <coreinit/foreground.h>
#include <proc_ui/procui.h>
#include <coreinit/memdefaultheap.h>
#include <nn/erreula.h>
#include <unistd.h>
#include "Application.h"
#include "gui/FreeTypeGX.h"
#include "gui/GuiImage.h"
#include "gui/GuiSound.h"
#include "gui/GuiText.h"
#include "gui/GuiTrigger.h"
#include "gui/VPadController.h"
#include "gui/WPadController.h"
#include "resources/Resources.h"
#include "sounds/SoundHandler.hpp"
#include "system/AsyncDeleter.h"
#include "system/exception_handler.h"
#include "system/memory.h"
#include "menu/ErrorViewer.h"
#include "utils/logger.h"

Application *Application::applicationInstance = NULL;
bool Application::exitApplication = false;
bool Application::quitRequest = false;

u32 Application::hbmDeniedCallback(void *context)
{
	//! The erreula singleton is created and destroyed with the main window;
	//! a dispatch arriving after its teardown must not touch it.
	if(!ErrorViewer::isInitialized())
		return 0;

	nn::erreula::HomeNixSignArg homeNixSignArg;
	nn::erreula::AppearHomeNixSign(homeNixSignArg);
	
	return 0;
}

Application::Application()
	: CThread(CThread::eAttributeAffCore0 | CThread::eAttributePinnedAff, 0, 0x20000)
	, bgMusic(NULL)
	, video(NULL)
	, mainWindow(NULL)
	, fontSystem(NULL)
{
	controller[0] = new VPadController(GuiTrigger::CHANNEL_1);
	controller[1] = new WPadController(GuiTrigger::CHANNEL_2);
	controller[2] = new WPadController(GuiTrigger::CHANNEL_3);
	controller[3] = new WPadController(GuiTrigger::CHANNEL_4);
	controller[4] = new WPadController(GuiTrigger::CHANNEL_5);
	
    //! load resources
    Resources::LoadFiles("fs:/vol/content");

	bgMusic = new GuiSound(Resources::GetFile("bgMusic.ogg"), Resources::GetFileSize("bgMusic.ogg"));
	bgMusic->SetLoop(true);
	bgMusic->Play();
	bgMusic->SetVolume(60);

	exitApplication = false;

    ProcUIInit(OSSavesDone_ReadyToRelease);
	ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED, hbmDeniedCallback, NULL, 1);
}

Application::~Application()
{
	delete bgMusic;
	
	for(int i = 0; i < 5; i++)
		delete controller[i];
	
	AsyncDeleter::destroyInstance();
	Resources::Clear();
	
	SoundHandler::DestroyInstance();
	
	ProcUIShutdown();
}

void Application::exec()
{
	//! start main GX2 thread
	resumeThread();
	//! now wait for thread to finish
	shutdownThread();
}

void Application::quit()
{
	exitApplication = true;
    quitRequest = true;
}

void Application::fadeOut()
{
    GuiImage fadeOut(video->getTvWidth(), video->getTvHeight(), (GX2Color){ 0, 0, 0, 255 });

	for(int i = 0; i < 255; i += 10)
    {
        if(i > 255)
            i = 255;

        fadeOut.setAlpha(i / 255.0f);

        //! start rendering DRC
	    video->prepareDrcRendering();
	    mainWindow->drawDrc(video);

        GX2SetDepthOnlyControl(GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS);
        fadeOut.draw(video);
        GX2SetDepthOnlyControl(GX2_ENABLE, GX2_ENABLE, GX2_COMPARE_FUNC_LEQUAL);

	    video->drcDrawDone();

        //! start rendering TV
	    video->prepareTvRendering();

	    mainWindow->drawTv(video);

        GX2SetDepthOnlyControl(GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS);
        fadeOut.draw(video);
        GX2SetDepthOnlyControl(GX2_ENABLE, GX2_ENABLE, GX2_COMPARE_FUNC_LEQUAL);

	    video->tvDrawDone();

	    //! as last point update the effects as it can drop elements
	    mainWindow->updateEffects();

	    video->waitForVSync();
    }

    //! one last cleared black screen
    video->prepareDrcRendering();
    video->drcDrawDone();
    video->prepareTvRendering();
    video->tvDrawDone();
    video->waitForVSync();
    video->tvEnable(false);
    video->drcEnable(false);
}

//! Hands the deferred-delete queues to the delete worker and waits (bounded)
//! for both queues and any in-flight delete to settle. True = fully drained.
static bool drainDeleteQueue(void)
{
	for(int i = 0; i < 200; i++)
	{
		AsyncDeleter::triggerDeleteProcess();
		if(AsyncDeleter::deleteQueueEmpty())
			return true;
		usleep(5000);
	}
	return false;
}

bool Application::procUI(void)
{
    bool executeProcess = false;
	
	switch(ProcUIProcessMessages(true))
    {
		case PROCUI_STATUS_EXITING:
		{
			log_printf("PROCUI_STATUS_EXITING\n");
			//! Stop an extraction writer that may still run: the bounded
			//! grace is affordable here (no drawDoneRelease deadline) and
			//! process teardown must not race a live write loop.
			if(mainWindow)
				mainWindow->abortFlowForExit();
			exitApplication = true;
			break;
		}
		case PROCUI_STATUS_RELEASE_FOREGROUND:
		{
			log_printf("PROCUI_STATUS_RELEASE_FOREGROUND\n");
			if(video != nullptr)
			{
				// we can turn of the screen but we don't need to and it will display the last image
				video->tvEnable(true);
				video->drcEnable(true);
				
				//! Rebuilding is only safe once every deferred deletion has
				//! actually run: a window queued by its own close handler is
				//! still referenced by MainWindow's element lists until the
				//! delete worker frees it, so hand the queues over and wait
				//! (bounded) before deciding what this cycle can do.
				bool rebuildUi = drainDeleteQueue();
				if(!rebuildUi)
				{
					//! Cannot prove the queued destructors have run; tearing
					//! the UI down now could free memory under a pending
					//! delete. Quit instead: process exit reclaims it all.
					//! The writer still gets the cancel + bounded grace - a
					//! stuck delete queue is no reason to abandon it mid-write.
					log_printf("memory: delete queue drain timed out\n");
					log_printf("foreground release with stuck delete queue: exiting\n");
					if(mainWindow)
					{
						mainWindow->logFlowGate("stuck delete queue");
						mainWindow->abortFlowForExit();
					}
					quit();
				}
				else if(mainWindow && !mainWindow->isFlowIdle())
				{
					//! An active flow owns windows its worker threads are
					//! still writing to; a rebuild would pull them out from
					//! under the workers. Stop the extraction worker, then
					//! quit to the menu without tearing anything down.
					mainWindow->logFlowGate("foreground release");
					log_printf("foreground release during active flow: exiting\n");
					mainWindow->abortFlowForExit();
					quit();
					rebuildUi = false;
				}

				if(rebuildUi)
				{
					//! The UI caches state that lives in the heaps torn down
					//! here: every GuiText holds the font object from
					//! construction and GuiImageData pixels are MEM1/bucket
					//! allocations. Draw the old window after memoryRelease()
					//! and the first frame reads freed memory (DSI on
					//! resume). So tear the window down with the rest and let
					//! the IN_FOREGROUND pass rebuild it - that is why its
					//! creation is guarded on nullptr.
					if(mainWindow)
					{
						log_printf("delete mainWindow\n");
						delete mainWindow;
						mainWindow = nullptr;
					}

					log_printf("delete fontSystem\n");
					delete fontSystem;
					fontSystem = nullptr;

					log_printf("delete video\n");
					delete video;
					video = nullptr;

					log_printf("deinitialize memory\n");
					//! The window's own destructor just queued its image
					//! data; those frees hit the exp heaps, so they must
					//! settle before the release.
					if(drainDeleteQueue())
						memoryRelease();
					else
					{
						log_printf("memory: delete queue drain timed out\n");
						//! A delete worker caught between pop and finish
						//! would free into a destroyed heap: keep the heaps
						//! (memoryInitialize reuses surviving ones anyway).
					}
				}
				else
				{
					//! Quit path: the loop tail's fadeOut() must not run once
					//! ProcUIDrawDoneRelease() has handed the screen over, and
					//! any live worker may still depend on the current
					//! allocations. Detach the video instead of destroying it;
					//! process exit reclaims everything.
					video = nullptr;
				}
				ProcUIDrawDoneRelease();
			}
			else
			{
				ProcUIDrawDoneRelease();
			}
			break;
		}
		case PROCUI_STATUS_IN_FOREGROUND:
		{
			if(!quitRequest)
			{
				if(video == nullptr)
				{
					log_printf("PROCUI_STATUS_IN_FOREGROUND\n");
					log_printf("initialize memory\n");
					memoryInitialize();
					
					log_printf("Initialize video\n");
					video = new CVideo(GX2_TV_SCAN_MODE_720P, GX2_DRC_RENDER_MODE_SINGLE);
					log_printf("Video size %i x %i\n", video->getTvWidth(), video->getTvHeight());
					
					//! setup default Font
					log_printf("Initialize main font system\n");
					if (fontSystem == nullptr)
					{
						fontSystem = new FreeTypeGX(Resources::GetFile("font.ttf"), Resources::GetFileSize("font.ttf"), true);
						GuiText::setPresetFont(fontSystem);
					}

					if (mainWindow == nullptr)
					{
						log_printf("Initialize main window\n");
						mainWindow = new MainWindow(video->getTvWidth(), video->getTvHeight());
					}
				}
				executeProcess = true;
			}
			break;
		}
		case PROCUI_STATUS_IN_BACKGROUND:
		default:
			break;
    }

    return executeProcess;
}

void Application::executeThread(void)
{
	//! setup exceptions on the main GX2 core
	setup_os_exceptions();
	
	log_printf("Entering main loop\n");
	
	//! main GX2 loop (60 Hz cycle with max priority on core 1)
	while(!exitApplication)
	{
	    if(procUI() == false)
			continue;
		
		mainWindow->lockGUI();
		//! Poll the install flow FIRST, every frame: a finished extraction
		//! thread must be able to release the UI even when no controller
		//! reported input this frame (MainWindow::updateFlow).
		mainWindow->updateFlow();

		//! Read out inputs
		for(int i = 0; i < 5; i++)
		{
			if(controller[i]->update(video->getTvWidth(), video->getTvHeight()) == false)
				continue;
			
			//! update controller states
			mainWindow->update(controller[i]);
		}
		
		//! start rendering DRC
		video->prepareDrcRendering();
		mainWindow->drawDrc(video);
		video->drcDrawDone();
		
		//! start rendering TV
		video->prepareTvRendering();
		mainWindow->drawTv(video);
		video->tvDrawDone();
		
		//! enable screen after first frame render
		if(video->getFrameCount() == 0) {
			video->tvEnable(true);
			video->drcEnable(true);
		}
		
		//! as last point update the effects as it can drop elements
		mainWindow->updateEffects();
		mainWindow->unlockGUI();
		
		video->waitForVSync();
		
		//! transfer elements to real delete list here after all processes are finished
		//! the elements are transfered to another list to delete the elements in a separate thread
		//! and avoid blocking the GUI thread
		AsyncDeleter::triggerDeleteProcess();
	}
	
	log_printf("Exiting main loop\n");
	
    if(video)
    {
        fadeOut();
    }
}
