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
#include <unistd.h>

#include "MainWindow.h"
#include "Application.h"
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
	wuxButton = NULL;
	wuxLabel = NULL;
	wuxButtonImage = NULL;
	wuxButtonImageHi = NULL;
	wupButton = NULL;
	wupLabel = NULL;
	wupButtonImage = NULL;
	wupButtonImageHi = NULL;
	mainUpButton = NULL;
	mainDownButton = NULL;
	mainAButton = NULL;
	mainFocusWux = true;
	
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
	delete wuxButtonImageHi;
	delete wupButton;
	delete wupLabel;
	delete wupButtonImage;
	delete wupButtonImageHi;
	delete mainUpButton;
	delete mainDownButton;
	delete mainAButton;

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
	
	// NOTE: the extraction-complete poll moved to updateFlow(); update()
	// runs only for controllers that reported input this frame, which must
	// not gate the flow (see updateFlow's comment).

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

bool MainWindow::abortFlowForExit()
{
	//! The app quits from the foreground-release path while a flow holds live
	//! windows. The extraction worker writes through stdio and must not be
	//! abandoned mid-write: ask it to stop (it checks the flag per written
	//! chunk and unlinks the partial file like a write error would) and wait
	//! a bounded grace for it to end. The install worker only drives MCP
	//! handoffs the server completes or drops on its own; abandoning it at
	//! process exit is the situation the exit-to-menu path always presented.
	WuxExtractThread *t = wux.thread();
	//! isCreated() also covers the never-ran case: an unstarted or
	//! already-released thread reports not-terminated forever, so polling it
	//! would only burn the grace and log a false "still running".
	if(!t || !t->isCreated())
		return true;

	t->requestCancel();
	for(int i = 0; i < 300 && !t->isThreadTerminated(); i++)
		usleep(5000);

	if(!t->isThreadTerminated())
	{
		log_printf("abortFlowForExit: extraction worker still running after 1.5s\n");
		return false;
	}
	return true;
}

void MainWindow::logFlowGate(const char *why) const
{
	log_printf("flow gate (%s): state=%s progress box=%s fade-out=%s\n",
		why, WuxFlow::stateName(wux.state()),
		wux.progressBox() ? "live" : "none",
		wux.progressFadingOut() ? "yes" : "no");
}

void MainWindow::updateFlow()
{
	// Runs every frame from the main loop, independent of controller input:
	// a finished extraction must release the progress box even with every
	// controller asleep. OnWuxExtractFinished clears the worker pointer, so
	// the transition still happens exactly once.
	if (wux.state() == WuxFlow::State::WuxExtract && wux.extractDone())
		OnWuxExtractFinished();
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
	
	SetDrcHeader();

	// Two stacked buttons, neither of which answers A on its own. Three
	// whole-screen imageless proxy buttons (built below) own the A, up and
	// down triggers, so a single press activates only the focused button and
	// the two flows can never race. Each visible button keeps a
	// position-based touch trigger, so tapping it directly still works.

	// "install wux": extract a .wux from /wudump into /install/<TITLEID>/ and
	// hand the result to the installer.
	wuxButtonImage = new GuiImage(600, 120, (GX2Color){ 42, 159, 217, 255 });
	wuxButtonImageHi = new GuiImage(600, 120, (GX2Color){ 120, 200, 255, 255 });
	wuxLabel = new GuiText("install wux", 48, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	wuxLabel->setAlignment(ALIGN_CENTERED);
	wuxButton = new GuiButton(600, 120);
	wuxButton->setImage(wuxButtonImage);
	wuxButton->setImageOver(wuxButtonImageHi);
	wuxButton->setLabel(wuxLabel);
	wuxButton->setAlignment(ALIGN_CENTERED);
	wuxButton->setPosition(0, 90);
	// Non-selectable so pointer hover never sets STATE_OVER: STATE_SELECTED
	// (moved by the dpad) is then the only highlight, and hover can never leave
	// a stale button lit. Tap-to-activate still works (the click gate tests
	// isInside directly).
	wuxButton->setSelectable(false);
	wuxTouchTrigger.setTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH);
	wuxButton->setTrigger(&wuxTouchTrigger);
	wuxButton->clicked.connect(this, &MainWindow::OnWuxInstallClicked);
	currentDrcFrame->append(wuxButton);

	// "install wup": install every WUP folder already present under /install.
	wupButtonImage = new GuiImage(600, 120, (GX2Color){ 42, 159, 217, 255 });
	wupButtonImageHi = new GuiImage(600, 120, (GX2Color){ 120, 200, 255, 255 });
	wupLabel = new GuiText("install wup", 48, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	wupLabel->setAlignment(ALIGN_CENTERED);
	wupButton = new GuiButton(600, 120);
	wupButton->setImage(wupButtonImage);
	wupButton->setImageOver(wupButtonImageHi);
	wupButton->setLabel(wupLabel);
	wupButton->setAlignment(ALIGN_CENTERED);
	wupButton->setPosition(0, -90);
	wupButton->setSelectable(false);
	wupTouchTrigger.setTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH);
	wupButton->setTrigger(&wupTouchTrigger);
	wupButton->clicked.connect(this, &MainWindow::OnWupInstallClicked);
	currentDrcFrame->append(wupButton);

	// Input proxies. A whole-screen, imageless button answers A and a second one
	// answers the dpad; they are SEPARATE GuiButtons on purpose: a button stores
	// only one clickedTrigger, so A and dpad on a single button would let a held
	// dpad direction swallow an A press. Neither button has
	// an image, so both draw nothing while still receiving input.
	mainAtrigger.setTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_A);
	mainAtrigger.setClickEverywhere(true);
	mainUpTrigger.setTrigger(GuiTrigger::CHANNEL_ALL,
	                         GuiTrigger::BUTTON_UP | GuiTrigger::STICK_L_UP);
	mainUpTrigger.setClickEverywhere(true);
	mainDownTrigger.setTrigger(GuiTrigger::CHANNEL_ALL,
	                           GuiTrigger::BUTTON_DOWN | GuiTrigger::STICK_L_DOWN);
	mainDownTrigger.setClickEverywhere(true);

	mainUpButton = new GuiButton(width, height);
	mainUpButton->setAlignment(ALIGN_CENTERED);
	mainUpButton->setPosition(0, 0);
	mainUpButton->setSelectable(false);
	mainUpButton->setTrigger(&mainUpTrigger);
	mainUpButton->clicked.connect(this, &MainWindow::OnMainNavClick);
	currentDrcFrame->append(mainUpButton);

	mainDownButton = new GuiButton(width, height);
	mainDownButton->setAlignment(ALIGN_CENTERED);
	mainDownButton->setPosition(0, 0);
	mainDownButton->setSelectable(false);
	mainDownButton->setTrigger(&mainDownTrigger);
	mainDownButton->clicked.connect(this, &MainWindow::OnMainNavClick);
	currentDrcFrame->append(mainDownButton);

	mainAButton = new GuiButton(width, height);
	mainAButton->setAlignment(ALIGN_CENTERED);
	mainAButton->setPosition(0, 0);
	mainAButton->setSelectable(false);
	mainAButton->setTrigger(&mainAtrigger);
	mainAButton->clicked.connect(this, &MainWindow::OnMainNavClick);
	currentDrcFrame->append(mainAButton);

	// Default highlight: the wux button.
	SetMainFocus(true);

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

void MainWindow::OnWuxInstallWindowClosed(GuiElement *element)
{
	// The wux flow's install window closed: return to the plain main screen
	// (header + the two install buttons).
	FinishInstallFlow();
}

void MainWindow::OnWupInstallWindowClosed(GuiElement *element)
{
	// The manual /install WUP flow's install window closed: same return to the
	// plain main screen. Nothing is rebuilt - the folder browser is gone.
	FinishInstallFlow();
}

void MainWindow::FinishInstallFlow()
{
	// Any install window closing releases the flow, so a new one may start.
	// Re-enabling home is harmless - the install thread already did it.
	wux.flowFinished();
	OSEnableHomeButtonMenu(TRUE);
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
	// Direct touch on the wux button (the A key is handled by OnMainNavClick).
	// The tap takes the A-highlight too, so the flow started by A afterwards
	// is the one the user last used, not the old default.
	SetMainFocus(true);
	TriggerWuxInstall();
}

void MainWindow::OnWupInstallClicked(GuiButton *button, const GuiController *controller,
                                     GuiTrigger *trigger)
{
	// Direct touch on the wup button; same highlight rule as the wux tap.
	SetMainFocus(false);
	TriggerWupInstall();
}

void MainWindow::OnMainNavClick(GuiButton *button, const GuiController *controller,
                                GuiTrigger *trigger)
{
	// The whole-screen proxy is the only element with an A trigger, so A always
	// means "activate the focused button" - never both at once.
	if (trigger == &mainAtrigger)
	{
		// Same explicit re-entrancy contract as the dpad branch below: a
		// press a box is answering must never also start a flow. The
		// Idle guard is the gate; child-append order is not.
		if (wux.state() != WuxFlow::State::Idle)
			return;

		if (mainFocusWux)
			TriggerWuxInstall();
		else
			TriggerWupInstall();
		return;
	}

	// D-pad moves the highlight only while the flow is idle; a box or worker
	// owns the screen otherwise.
	if (wux.state() != WuxFlow::State::Idle)
		return;

	bool wantWux = (trigger == &mainUpTrigger);
	if (wantWux == mainFocusWux)
		return;

	SetMainFocus(wantWux);
}

void MainWindow::SetMainFocus(bool focusWux)
{
	mainFocusWux = focusWux;
	if (mainFocusWux)
	{
		wuxButton->setState(GuiElement::STATE_SELECTED);
		wupButton->clearState(GuiElement::STATE_SELECTED);
	}
	else
	{
		wupButton->setState(GuiElement::STATE_SELECTED);
		wuxButton->clearState(GuiElement::STATE_SELECTED);
	}
}

void MainWindow::TriggerWuxInstall()
{
	// Idle -> WuxExtract is the re-entrancy guard: it refuses while any flow is
	// active, so a press during extraction or an install is ignored.
	if (!wux.beginExtraction())
		return;

	RunWuxInstall();
}

void MainWindow::TriggerWupInstall()
{
	// Idle -> WupInstall, the same guard on the manual /install path.
	if (!wux.beginWupInstall())
		return;

	RunWupInstall();
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

	// The list is name-sorted and the first file is used (a chooser is an
	// open feature item), so say which image this run uses - visibly, not
	// only after the extraction in the delete prompt.
	const char *wuxName = dl.GetFilename(0);
	log_printf("WUX: %d .wux file(s) in /wudump, installing %s%s",
	           dl.GetFilecount(), wuxName,
	           dl.GetFilecount() > 1 ? " (alphabetically first; one image per run)" : "");

	std::string wuxPath = std::string(SD_WUDUMP_PATH) + "/" + wuxName;
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
	// Name the image on screen while it runs, so a silent positional pick of
	// one file among several is visible during the extraction, not after.
	wux.progressBox()->setMessage1(wuxPath.substr(wuxPath.find_last_of('/') + 1) +
	                                " - reading disc structure...");
	wux.progressBox()->setProgress(0.0f);
	wux.progressBox()->setProgressBarInfo("0.0 / 0.0 MB (0%)");
	wux.progressBox()->effectFinished.connect(this, &MainWindow::OnWuxProgressBoxEffectFinished);
	currentDrcFrame->append(wux.progressBox());

	// Keep the home button out while files are being written; the install
	// worker applies the same guard during its run.
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
		// Refresh the folder list and select every extracted folder so the
		// installer runs without the user picking folders by hand. A disc can
		// yield several titles (e.g. the game plus the rear.rpx dummy).
		if (folderList == NULL)
			folderList = new CFolderList();
		folderList->Get();
		// Drop any selection left over from the scan; otherwise a
		// folder selected by a previous flow could be installed and deleted by
		// this one.
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
				// The wux flow ends back on the plain main screen, so it gets
				// its own close handler (distinct from the manual WUP flow).
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

void MainWindow::RunWupInstall()
{
	// Manual WUP install. Re-read the shared folder list, then select every
	// folder directly under /install (the scanner already admits only those
	// that directly contain title.tik. Any
	// /wudump entries and the bare-"install" fallback (loose .tik files) are
	// left unselected, so this button installs only what the user placed in
	// /install.
	if (folderList == NULL)
		folderList = new CFolderList();
	folderList->Get();
	folderList->UnSelectAll();

	const std::string installPrefix = std::string(SD_INSTALL_PATH) + "/";
	for (int i = 0; i < folderList->GetCount(); ++i)
	{
		std::string path = folderList->GetPath(i);
		if (path.size() > installPrefix.size() &&
		    path.compare(0, installPrefix.size(), installPrefix) == 0)
			folderList->Select(i);
	}

	if (folderList->GetSelectedCount() == 0)
	{
		// Nothing recognizable: report it and leave the flow via the box's OK
		// click rather than starting a 0-selection install.
		ShowWupResult("No installable WUP folder found in /install.", false);
		return;
	}

	// The "are you sure" and NAND/USB prompts stay; askDelete adds the wux
	// flow's delete question. Yes removes only folders whose own install
	// succeeded; with no cleanupFiles this flow cannot reach /wudump.
	InstallWindow::InstallOptions options;
	options.askDelete = true;
	InstallWindow *window = new InstallWindow(folderList, options);
	if (!window->isCreated())
	{
		delete window;
		ShowWupResult("Could not start the installer (thread creation failed).", false);
		return;
	}
	window->installWindowClosed.connect(this, &MainWindow::OnWupInstallWindowClosed);
}

void MainWindow::ShowWupResult(const std::string &msg, bool ok)
{
	MessageBox *box = new MessageBox(MessageBox::BT_OK,
	                                 ok ? MessageBox::IT_ICONTRUE : MessageBox::IT_ICONERROR,
	                                 false);
	box->setState(GuiElement::STATE_DISABLED);
	box->setEffect(EFFECT_FADE, 10, 255);
	box->setTitle(ok ? "WUP install:" : "WUP install failed:");
	box->setMessage1(msg);
	box->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);
	box->messageOkClicked.connect(this, &MainWindow::OnWuxMessageBoxClick);
	currentDrcFrame->append(box);

	// The box owns the screen until the OK click; OnWuxMessageBoxClick is
	// flow-agnostic and returns both entry points to Idle.
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
}
