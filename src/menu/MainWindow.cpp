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

#include "MainWindow.h"
#include "Application.h"
#include "BrowserWindow.h"
#include "ErrorViewer.h"
#include "InstallWindow.h"
#include "WuxExtractThread.h"
#include "utils/StringTools.h"
#include "utils/logger.h"
#include "common/common.h"
#include "common/fs_defs.h"
#include "fs/CFolderList.hpp"
#include "fs/DirList.h"
#include "gui/GuiButton.h"
#include "gui/GuiController.h"
#include "gui/MessageBox.h"
#include "system/AsyncDeleter.h"
#include "wux/wux_installer.h"

MainWindow::MainWindow(int w, int h)
	: width(w)
	, height(h)
	, titleImgData(Resources::GetImageData("titleHeader.png"))
	, titleImg(titleImgData)
	, titleText("Wux Installer")
	, versionText("V1.0")
{
	folderList = NULL;
	// Initialized here, not only by SetBrowserWindow(): CloseBrowser() reads
	// the pointer, and SetupMainView() runs from this constructor.
	browserWindow = NULL;
	wuxButton = NULL;
	wuxLabel = NULL;
	wuxButtonImage = NULL;
	
	for(int i = 0; i < 4; i++)
	{
		std::string filename = strfmt("player%i_point.png", i+1);
		pointerImgData[i] = Resources::GetImageData(filename.c_str());
		pointerImg[i] = new GuiImage(pointerImgData[i]);
		pointerImg[i]->setScale(1.5f);
		pointerValid[i] = false;
	}

	errorViewer = new ErrorViewer();
	
	SetupMainView();
}

MainWindow::~MainWindow()
{
	Resources::RemoveImageData(titleImgData);
	
	// Flow-owned children first, then the frames that carried them. Today the
	// order is only hygiene: GuiFrame::append records the parent in
	// GuiElement::parentElement, not in GuiFrame::parent, and every frame here
	// uses the two-argument constructor, so ~GuiFrame's `if(parent)` removal
	// never runs. Were a parent-taking constructor ever adopted, deleting a
	// child after its frame would hand ~GuiFrame a freed parent to unregister
	// from - hence children first.
	wux.shutdown();

	// A browser still owned here means it never reached a delete path (both
	// the fade handler and CloseBrowser() clear the pointer).
	if (browserWindow != NULL)
	{
		delete browserWindow;
		browserWindow = NULL;
	}

	while(!tvElements.empty())
	{
		// Read the pointer, unlink, then free: deleting first leaves a
		// freed pointer being used as the lookup key below.
		GuiElement *element = tvElements[0];
		remove(element);
		delete element;
	}
	while(!drcElements.empty())
	{
		GuiElement *element = drcElements[0];
		remove(element);
		delete element;
	}
	for(int i = 0; i < 4; i++)
	{
		delete pointerImg[i];
		Resources::RemoveImageData(pointerImgData[i]);
	}
	
	delete wuxButton;
	delete wuxLabel;
	delete wuxButtonImage;

	if(folderList != NULL)
		delete folderList;

	delete errorViewer;
}

void MainWindow::updateEffects()
{
	//! dont read behind the initial elements in case one was added
	u32 tvSize = tvElements.size();
	u32 drcSize = drcElements.size();
	
	for(u32 i = 0; (i < drcSize) && (i < drcElements.size()); ++i)
	{
		drcElements[i]->updateEffects();
	}
	
	//! only update TV elements that are not updated yet because they are on DRC
	for(u32 i = 0; (i < tvSize) && (i < tvElements.size()); ++i)
	{
		u32 n;
		for(n = 0; (n < drcSize) && (n < drcElements.size()); n++)
		{
			if(tvElements[i] == drcElements[n])
				break;
		}
		if(n == drcElements.size())
		{
			tvElements[i]->updateEffects();
		}
	}
}

void MainWindow::update(GuiController *controller)
{
	//! dont read behind the initial elements in case one was added
	
	// The extraction worker runs on another thread. When it terminates, run
	// the flow transition on this (GUI) thread. update() is called once per
	// controller per frame; OnWuxExtractFinished releases the worker
	// pointer, so the transition runs exactly once.
	if (wux.state() == WuxFlow::State::WuxExtract && wux.extractDone())
		OnWuxExtractFinished();
	
	if(controller->chan & GuiTrigger::CHANNEL_1)
	{
		u32 drcSize = drcElements.size();
		
		for(u32 i = 0; (i < drcSize) && (i < drcElements.size()); ++i)
		{
			drcElements[i]->update(controller);
		}
	}
	else
	{
		u32 tvSize = tvElements.size();
		
		for(u32 i = 0; (i < tvSize) && (i < tvElements.size()); ++i)
		{
			tvElements[i]->update(controller);
		}
	}
	
	if(controller->wpadChanIdx >= 1 && controller->wpadChanIdx <= 4 && controller->data.validPointer)
	{
		int wpadIdx = controller->wpadChanIdx;
		f32 posX = controller->data.x;
		f32 posY = controller->data.y;
		pointerImg[wpadIdx]->setPosition(posX, posY);
		pointerImg[wpadIdx]->setAngle(controller->data.pointerAngle);
		pointerValid[wpadIdx] = true;
	}

	errorViewer->calc();
}

void MainWindow::drawDrc(CVideo *video)
{
	for(u32 i = 0; i < drcElements.size(); ++i)
	{
		drcElements[i]->draw(video);
	}
	
	for(int i = 0; i < 4; i++)
	{
		if(pointerValid[i])
		{
			pointerImg[i]->setAlpha(0.5f);
			pointerImg[i]->draw(video);
			pointerImg[i]->setAlpha(1.0f);
		}
	}
	
	errorViewer->drawDRC();
}

void MainWindow::drawTv(CVideo *video)
{
	for(u32 i = 0; i < tvElements.size(); ++i)
	{
		tvElements[i]->draw(video);
	}
	
	for(int i = 0; i < 4; i++)
	{
		if(pointerValid[i])
		{
			pointerImg[i]->draw(video);
			pointerValid[i] = false;
		}
	}

	errorViewer->drawTV();
}

void MainWindow::SetupMainView()
{
	currentDrcFrame = new GuiFrame(width, height);
	currentDrcFrame->setEffect(EFFECT_FADE, 10, 255);
	currentDrcFrame->setState(GuiElement::STATE_DISABLED);
	currentDrcFrame->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);
	
	SetBrowserWindow();
	SetDrcHeader();

	// "Install WUX": extract a .wux from /wudump into /install/<TITLEID>/ and
	// hand the result to the installer.
	wuxButtonImage = new GuiImage(600, 120, (GX2Color){ 42, 159, 217, 255 });
	wuxLabel = new GuiText("install wux", 48, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	wuxLabel->setAlignment(ALIGN_CENTERED);
	wuxButton = new GuiButton(600, 120);
	wuxButton->setImage(wuxButtonImage);
	wuxButton->setLabel(wuxLabel);
	wuxButton->setAlignment(ALIGN_CENTERED);
	wuxButton->setPosition(0, 0);
	wuxTrigger.setTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_A);
	wuxTrigger.setClickEverywhere(true);
	wuxTouchTrigger.setTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH);
	wuxButton->setTrigger(&wuxTrigger);
	wuxButton->setTrigger(&wuxTouchTrigger);
	wuxButton->clicked.connect(this, &MainWindow::OnWuxInstallClicked);
	currentDrcFrame->append(wuxButton);
	// An empty /install is a normal starting state. The user extracts a .wux via the "Install WUX" button, which rebuilds the browser
	// afterwards (see RunWuxInstall). Do not show an error or exit here.

	append(currentDrcFrame);
}

void MainWindow::SetDrcHeader()
{
	titleText.setColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	titleText.setFontSize(46);
	titleText.setPosition(0, 10);
	titleText.setBlurGlowColor(5.0f, glm::vec4(0.0, 0.0, 0.0f, 1.0f));
	
	versionText.setColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	versionText.setFontSize(30);
	versionText.setPosition(-15, -40);
	versionText.setBlurGlowColor(5.0f, glm::vec4(0.0, 0.0, 0.0f, 1.0f));
	versionText.setAlignment(ALIGN_RIGHT | ALIGN_TOP);
	versionText.setText("V1.0");
	
	headerFrame.setSize(titleImg.getWidth(), titleImg.getHeight());
	headerFrame.setPosition(0, 310);
	headerFrame.append(&titleImg);
	headerFrame.append(&titleText);
	headerFrame.append(&versionText);
	
	currentDrcFrame->append(&headerFrame);
}

void MainWindow::CloseBrowser()
{
	if (browserWindow == NULL)
		return;

	currentDrcFrame->remove(browserWindow);
	AsyncDeleter::pushForDelete(browserWindow);
	browserWindow = NULL;
}

void MainWindow::SetBrowserWindow()
{
	// Release the browser before the folder list is read again. Its buttons
	// were built from the previous scan, so a refreshed CFolderList would be
	// indexed out of range by the stale count it cached at construction.
	CloseBrowser();
	
	if(folderList == NULL)
	{
		folderList = new CFolderList();
		folderList->Get();
	}
	
	if(!folderList->GetCount())
	{
		delete folderList;
		folderList = NULL;
		return;
	}
	
	browserWindow = new BrowserWindow(920, height, folderList);
	browserWindow->setAlignment(ALIGN_LEFT | ALIGN_MIDDLE);
	browserWindow->setPosition(50, 0);
	browserWindow->installButtonClicked.connect(this, &MainWindow::OnInstallButtonClicked);
	browserWindow->setState(GuiElement::STATE_DISABLED);
	browserWindow->setEffect(EFFECT_FADE, 10, 255);
	browserWindow->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);
	currentDrcFrame->append(browserWindow);
}

void MainWindow::OnInstallButtonClicked(GuiElement *element)
{
	// Idle -> BrowserInstall is the only gate: it refuses every other state,
	// which covers a running wux flow (extraction, install or result box) as
	// well as a browser install already in flight. The wux flow selects and
	// deletes folders behind the user's back, so it must never be overlaid.
	// A refusal leaves the browser untouched, so the click can be retried.
	if (!wux.beginBrowserInstall())
		return;

	InstallWindow::InstallOptions options;
	options.deleteAfterInstall = browserWindow->DeleteAfterInstallEnabled();
	InstallWindow *window = new InstallWindow(folderList, options);
	if (!window->isCreated())
	{
		// Thread creation failed (OOM): drop the window and leave the browser
		// fully intact - it has not been faded or disabled at this point.
		delete window;
		wux.flowFinished();
		log_printf("MainWindow: install thread could not be created");
		return;
	}
	window->installWindowClosed.connect(this, &MainWindow::OnInstallWindowClosed);

	// Only tear the browser down once the install window is known good.
	browserWindow->setEffect(EFFECT_FADE, -10, 255);
	browserWindow->setState(GuiElement::STATE_DISABLED);
	browserWindow->effectFinished.connect(this, &MainWindow::OnBrowserCloseEffectFinish);
}

void MainWindow::OnBrowserCloseEffectFinish(GuiElement *element)
{
	//! remove element from draw list and push to delete queue
	currentDrcFrame->remove(element);

	// The delete queue owns the object from here. Clearing the member is what
	// keeps CloseBrowser() from queueing the same browser twice.
	if (element == browserWindow)
		browserWindow = NULL;

	AsyncDeleter::pushForDelete(element);
}
void MainWindow::OnInstallWindowClosed(GuiElement *element)
{
	// The BROWSER flow's install window closed. Rebuild the browser: the
	// install may have deleted folders, so the list is refreshed first and
	// the window rebuilt from it.
	FinishInstallFlow(true);
}

void MainWindow::OnWuxInstallWindowClosed(GuiElement *element)
{
	// The wux flow's install window closed: return to the plain main screen
	// (header + "install wux" button). The browser is deliberately not
	// rebuilt; the flow removed it when the install started.
	FinishInstallFlow(false);
}

void MainWindow::FinishInstallFlow(bool rebuildBrowser)
{
	// Any install window closing releases the flow, so a new one may start.
	// Re-enabling home is harmless - the install thread already did it.
	wux.flowFinished();
	OSEnableHomeButtonMenu(TRUE);
	Application::instance()->exitEnable();
	
	if(folderList)
		folderList->Get();
	if(rebuildBrowser)
		SetBrowserWindow();
	currentDrcFrame->bringToFront(&headerFrame);
}

void MainWindow::OnOpenEffectFinish(GuiElement *element)
{
	//! once the menu is open reset its state and allow it to be "clicked/hold"
	element->effectFinished.disconnect(this);
	element->clearState(GuiElement::STATE_DISABLED);
}

void MainWindow::OnWuxInstallClicked(GuiButton *button, const GuiController *controller,
                                     GuiTrigger *trigger)
{
	// Guard against re-entrancy: the A/touch triggers fire from anywhere
	// (setClickEverywhere), so a press is accepted only while the flow is
	// Idle - no extraction running and no install window from either flow.
	// The state transition is the guard; a refusal leaves the screen as it is.
	if (!wux.beginExtraction())
		return;

	RunWuxInstall();
}

void MainWindow::RunWuxInstall()
{
	// Locate a .wux image and its keys in the wudump folder.
	DirList dl(SD_WUDUMP_PATH, ".wux", DirList::Files);
	if (dl.GetFilecount() == 0)
	{
		ShowWuxResult("No .wux image found in /wudump.", false);
		return;
	}

	std::string wuxPath = std::string(SD_WUDUMP_PATH) + "/" + dl.GetFilename(0);
	std::string keyPath = std::string(SD_WUDUMP_PATH) + "/game.key";
	std::string commonKeyPath = std::string(SD_WUDUMP_PATH) + "/common.key";

	StartWuxExtraction(wuxPath, keyPath, commonKeyPath);
}

void MainWindow::StartWuxExtraction(const std::string &wuxPath,
                                    const std::string &keyPath,
                                    const std::string &commonKeyPath)
{
	// Files the delete-files prompt can remove once the install succeeds.
	wux.setCleanupFiles(wuxPath, keyPath);

	// Progress window on top of the main screen, styled like the WUP install
	// progress box. The extraction itself runs on a worker thread, so the UI
	// keeps rendering while the files are written (see WuxExtractThread).
	// setProgressBox() logs if a previous box never reached its fade-out.
	wux.setProgressBox(new MessageBox(MessageBox::BT_NOBUTTON,
	                                   MessageBox::IT_ICONINFORMATION, true));

	wux.progressBox()->setState(GuiElement::STATE_DISABLED);
	wux.progressBox()->setEffect(EFFECT_FADE, 10, 255);
	wux.progressBox()->setTitle("Extracting .wux");
	wux.progressBox()->setMessage1("Reading disc structure...");
	wux.progressBox()->setProgress(0.0f);
	wux.progressBox()->setProgressBarInfo("0.0 / 0.0 MB (0%)");
	wux.progressBox()->effectFinished.connect(this, &MainWindow::OnWuxProgressBoxEffectFinished);
	currentDrcFrame->append(wux.progressBox());

	// Keep the home button out while files are being written; the install
	// worker applies the same guard during its run.
	Application::instance()->exitDisable();
	OSEnableHomeButtonMenu(FALSE);

	wux.setThread(new WuxExtractThread(wuxPath, keyPath, commonKeyPath,
	                                   SD_INSTALL_PATH, wux.progressBox()));
	if (!wux.thread()->isCreated())
	{
		// Thread creation failed (OOM): clean up the worker, fade the
		// progress box out, and report the error. The error box re-enables
		// the flow when the user closes it (OnWuxMessageBoxClick).
		wux.releaseThread();
		wux.armProgressFadeOut();
		wux.progressBox()->setEffect(EFFECT_FADE, -10, 255);
		wux.progressBox()->setState(GuiElement::STATE_DISABLED);
		wux.progressBox()->effectFinished.connect(this, &MainWindow::OnWuxProgressBoxEffectFinished);
		ShowWuxResult("Could not start the extraction thread.", false);
		return;
	}
	wux.thread()->resumeThread();
}

void MainWindow::OnWuxExtractFinished()
{
	// Join first, then read. `result`/`error` are plain members the worker
	// writes without a lock; isThreadTerminated() alone is only a non-atomic
	// OS poll and gives no happens-before edge for them. The join does, and
	// shutdownThread() is idempotent (it nulls the thread handle), so the
	// later ~CThread pass is a no-op. The GUI thread already blocked here.
	wux.joinThread();

	wux::Error err = wux.thread()->error;
	wux::ExtractResult result = wux.thread()->result;
	wux.releaseThread();

	wux.setFinalNote(result.note);
	if (!wux.finalNote().empty())
		log_printf("WUX extract note: %s", wux.finalNote().c_str());

	// Fade the progress window out; it is removed once the effect finishes.
	// The effect handler disconnected itself when the fade-in finished, so
	// reconnect it for the fade-out. The flag and the effect move together
	// through armProgressFadeOut().
	wux.armProgressFadeOut();
	if (wux.progressBox())
	{
		wux.progressBox()->setEffect(EFFECT_FADE, -10, 255);
		wux.progressBox()->setState(GuiElement::STATE_DISABLED);
		wux.progressBox()->effectFinished.connect(this, &MainWindow::OnWuxProgressBoxEffectFinished);
	}

	bool installStarted = false;

	if (err == wux::Error::Ok && result.ok)
	{
		// Release the browser before the folder list is re-read: its buttons
		// and its cached count belong to the previous scan.
		CloseBrowser();

		// Refresh the folder list and select every extracted folder so the
		// installer runs without the user navigating the browser. A disc can
		// yield several titles (e.g. the game plus the rear.rpx dummy).
		if (folderList == NULL)
			folderList = new CFolderList();
		folderList->Get();
		// Drop any selection left over from the file browser; otherwise a
		// folder the user ticked earlier would be installed and deleted by
		// this flow.
		folderList->UnSelectAll();
		for (size_t d = 0; d < result.outDirs.size(); ++d)
		{
			const std::string& outDir = result.outDirs[d];
			// Match on the full path; display names are prefixed by the
			// recursive scans.
			for (int i = 0; i < folderList->GetCount(); ++i)
			{
				std::string path = folderList->GetPath(i);
				std::string want = outDir;
				if (!path.empty() && path[path.size() - 1] == '/')
					path.erase(path.size() - 1);
				if (!want.empty() && want[want.size() - 1] == '/')
					want.erase(want.size() - 1);
				if (path == want) { folderList->Select(i); break; }
			}
		}

		if (folderList->GetSelectedCount() == 0)
		{
			// Extraction wrote folders but none is installable (enumeration
			// glitch, missing title.tik): report it instead of starting a
			// 0-selection install, which would dead-end in an uncloseable
			// dialog.
			ShowWuxResult("No installable title found in /install after extraction.", false);
		}
		else
		{
			// Hand the selected folders to the installer by code. The WUX path
			// skips the "are you sure" prompt, asks about deleting the .wux /
			// game.key / .app files, and gets the cleanup file list.
			InstallWindow::InstallOptions options;
			options.skipConfirm = true;
			options.askDelete = true;
			options.cleanupFiles = wux.cleanupFiles();
			options.wuxFlow = true;
			options.finalNote = wux.finalNote();
			InstallWindow *window = new InstallWindow(folderList, options);
			if (!window->isCreated())
			{
				// Same failure shape as the extraction thread: the error box
				// re-enables the flow on close (OnWuxMessageBoxClick).
				delete window;
				ShowWuxResult("Could not start the installer (thread creation failed).", false);
			}
			else
			{
				// The wux flow ends back on the plain main screen (no WUP
				// browser), unlike the browser flow, so it gets its own close
				// handler.
				window->installWindowClosed.connect(this,
				                                    &MainWindow::OnWuxInstallWindowClosed);
				installStarted = true;
			}
		}
	}
	else
	{
		ShowWuxResult(result.error.empty() ? std::string(wux::errorName(err))
		                                  : result.error, false);
	}

	// One transition for the whole tail: the installer took over, or a box is
	// waiting for the OK click (ShowWuxResult already staged that state).
	wux.extractionFinished(installStarted);
}

void MainWindow::OnWuxProgressBoxEffectFinished(GuiElement *element)
{
	element->effectFinished.disconnect(this);
	
	// Idempotency guard: on a fast fail (the worker dies while the fade-in
	// is still in flight) the fade-in connection is still alive when the
	// fade-out reconnects this handler, so the signal can invoke it twice.
	// Once the box is closed the pointer is NULL and the second invocation
	// must skip both branches.
	if (wux.progressBox() != element)
		return;
	
	if (wux.progressFadingOut())
	{
		// Fade-out finished: remove the box from the tree and let the delete
		// queue take it (a direct delete here would race this render pass).
		// finishProgressFadeOut() drops the pointer and the flag in step, so
		// shutdown() cannot delete a box the queue already owns.
		currentDrcFrame->remove(element);
		AsyncDeleter::pushForDelete(element);
		wux.finishProgressFadeOut();
	}
	else
	{
		// Fade-in finished: allow input on the box.
		element->clearState(GuiElement::STATE_DISABLED);
	}
}

void MainWindow::ShowWuxResult(const std::string &msg, bool ok)
{
	MessageBox *box = new MessageBox(MessageBox::BT_OK,
	                                 ok ? MessageBox::IT_ICONTRUE : MessageBox::IT_ICONERROR,
	                                 false);
	box->setState(GuiElement::STATE_DISABLED);
	box->setEffect(EFFECT_FADE, 10, 255);
	box->setTitle(ok ? "WUX extract:" : "WUX extract failed:");
	box->setMessage1(msg);
	box->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);
	box->messageOkClicked.connect(this, &MainWindow::OnWuxMessageBoxClick);
	currentDrcFrame->append(box);

	// The flow now owns the screen through this box: both entry points stay
	// blocked until OnWuxMessageBoxClick releases it.
	wux.enterErrorBox();
}

void MainWindow::OnWuxMessageBoxClick(GuiElement *element, int ok)
{
	currentDrcFrame->remove(element);
	AsyncDeleter::pushForDelete(element);
	
	// The wux flow has ended (extraction failed or no image found): the
	// "install wux" button may be used again.
	wux.flowFinished();
	OSEnableHomeButtonMenu(TRUE);
	Application::instance()->exitEnable();
}
