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
#include <sysapp/launch.h>
#include <coreinit/foreground.h>

#include "MainWindow.h"
#include "Application.h"
#include "utils/StringTools.h"
#include "common/common.h"
#include "common/fs_defs.h"
#include "fs/DirList.h"
#include "gui/MessageBox.h"
#include "wux/wux_installer.h"

MainWindow::MainWindow(int w, int h)
	: width(w)
	, height(h)
	, splashImgData(Resources::GetImageData("splash.png"))
	, splashImg(splashImgData)
	, titleImgData(Resources::GetImageData("titleHeader.png"))
	, titleImg(titleImgData)
	, titleText("Wux Installer")
	, versionText("V1.0")
{
	folderList = NULL;
	installWindow = NULL;
	wuxExtractThread = NULL;
	wuxProgressBox = NULL;
	wuxBusy = false;
	installWindowOpen = false;
	wuxBoxClosing = false;
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
	Resources::RemoveImageData(splashImgData);
	Resources::RemoveImageData(titleImgData);
	
	while(!tvElements.empty())
	{
		delete tvElements[0];
		remove(tvElements[0]);
	}
	while(!drcElements.empty())
	{
		delete drcElements[0];
		remove(drcElements[0]);
	}
	for(int i = 0; i < 4; i++)
	{
		delete pointerImg[i];
		Resources::RemoveImageData(pointerImgData[i]);
	}
	
	delete wuxButton;
	delete wuxLabel;
	delete wuxButtonImage;

	// The progress box is owned by us (it is a child of currentDrcFrame, but
	// GuiFrame does not delete its children). The worker is joined first so
	// it cannot touch the progress box after the box is deleted. This path
	// is currently unreachable (mainWindow is never destroyed), but it must
	// be safe if it ever becomes reachable.
	if (wuxExtractThread != NULL)
	{
		delete wuxExtractThread;   // ~CThread joins the worker thread
		wuxExtractThread = NULL;
	}
	if (wuxProgressBox != NULL)
	{
		delete wuxProgressBox;
		wuxProgressBox = NULL;
	}

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
	// controller per frame; OnWuxExtractFinished clears the worker pointer,
	// so the transition runs exactly once.
	if (wuxBusy && wuxExtractThread && wuxExtractThread->isThreadTerminated())
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

void MainWindow::SetBrowserWindow()
{
	browserWindow = NULL;
	
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
	// While a wux flow is running (extraction or install) the browser must
	// not start a second install window on top of it.
	if (wuxBusy)
		return;
	installWindowOpen = true;
	
	browserWindow->setEffect(EFFECT_FADE, -10, 255);
	browserWindow->setState(GuiElement::STATE_DISABLED);
	browserWindow->effectFinished.connect(this, &MainWindow::OnBrowserCloseEffectFinish);
	
	installWindow = new InstallWindow(folderList, browserWindow->DeleteAfterInstallEnabled());
	installWindow->installWindowClosed.connect(this, &MainWindow::OnInstallWindowClosed);
}

void MainWindow::OnBrowserCloseEffectFinish(GuiElement *element)
{
	//! remove element from draw list and push to delete queue
	currentDrcFrame->remove(element);
	AsyncDeleter::pushForDelete(element);
}
void MainWindow::OnInstallWindowClosed(GuiElement *element)
{
	// The BROWSER flow's install window closed (the wux flow uses its own
	// close handler, OnWuxInstallWindowClosed): a new flow may start again.
	// Re-enabling home is harmless - the install thread already did it.
	wuxBusy = false;
	installWindowOpen = false;
	installWindow = NULL;
	OSEnableHomeButtonMenu(TRUE);
	Application::instance()->exitEnable();
	
	if(folderList)
		folderList->Get();
	SetBrowserWindow();
	currentDrcFrame->bringToFront(&headerFrame);
}

void MainWindow::OnWuxInstallWindowClosed(GuiElement *element)
{
	// The wux flow's install window closed: return to the plain main screen
	// (header + "install wux" button) and do NOT recreate the WUP folder
	// browser, which the flow removed when the install started. The browser
	// flow keeps its own close handler (OnInstallWindowClosed), which does
	// recreate the browser.
	wuxBusy = false;
	installWindowOpen = false;
	installWindow = NULL;
	OSEnableHomeButtonMenu(TRUE);
	Application::instance()->exitEnable();
	
	if(folderList)
		folderList->Get();
	currentDrcFrame->bringToFront(&headerFrame);
}

void MainWindow::OnErrorMessageBoxClick(GuiElement *element, int ok)
{
	SYSLaunchMenu();
}

void MainWindow::OnOpenEffectFinish(GuiElement *element)
{
	//! once the menu is open reset its state and allow it to be "clicked/hold"
	element->effectFinished.disconnect(this);
	element->clearState(GuiElement::STATE_DISABLED);
}

void MainWindow::OnCloseEffectFinish(GuiElement *element)
{
	//! remove element from draw list and push to delete queue
	remove(element);
	AsyncDeleter::pushForDelete(element);
}

void MainWindow::OnWuxInstallClicked(GuiButton *button, const GuiController *controller,
                                     GuiTrigger *trigger)
{
	// Guard against re-entrancy: the A/touch triggers fire from anywhere
	// (setClickEverywhere), so a press while a wux flow is running must not
	// start a second extraction, and a press while an InstallWindow from
	// either flow is open must not start a parallel install.
	if (wuxBusy || installWindowOpen)
		return;
	wuxBusy = true;
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
	wuxCleanupFiles.clear();
	wuxCleanupFiles.push_back(wuxPath);
	wuxCleanupFiles.push_back(keyPath);

	// Progress window on top of the main screen, styled like the WUP install
	// progress box. The extraction itself runs on a worker thread, so the UI
	// keeps rendering while the files are written (see WuxExtractThread).
	wuxProgressBox = new MessageBox(MessageBox::BT_NOBUTTON,
	                                MessageBox::IT_ICONINFORMATION, true);
	wuxProgressBox->setState(GuiElement::STATE_DISABLED);
	wuxProgressBox->setEffect(EFFECT_FADE, 10, 255);
	wuxProgressBox->setTitle("Extracting .wux");
	wuxProgressBox->setMessage1("Reading disc structure...");
	wuxProgressBox->setProgress(0.0f);
	wuxProgressBox->setProgressBarInfo("0.0 / 0.0 MB (0%)");
	wuxProgressBox->effectFinished.connect(this, &MainWindow::OnWuxProgressBoxEffectFinished);
	currentDrcFrame->append(wuxProgressBox);

	// Keep the home button out while files are being written, mirroring what
	// the fork does during the install phase.
	Application::instance()->exitDisable();
	OSEnableHomeButtonMenu(FALSE);

	wuxExtractThread = new WuxExtractThread(wuxPath, keyPath, commonKeyPath,
	                                        SD_INSTALL_PATH, wuxProgressBox);
	if (wuxExtractThread->getThread() == NULL)
	{
		// Thread creation failed (OOM): clean up the worker, fade the
		// progress box out, and report the error. The error box re-enables
		// the flow when the user closes it (OnWuxMessageBoxClick).
		delete wuxExtractThread;
		wuxExtractThread = NULL;
		wuxBoxClosing = true;
		wuxProgressBox->setEffect(EFFECT_FADE, -10, 255);
		wuxProgressBox->setState(GuiElement::STATE_DISABLED);
		wuxProgressBox->effectFinished.connect(this, &MainWindow::OnWuxProgressBoxEffectFinished);
		ShowWuxResult("Could not start the extraction thread.", false);
		return;
	}
	wuxExtractThread->resumeThread();
}

void MainWindow::OnWuxExtractFinished()
{
	wux::Error err = wuxExtractThread->error;
	wux::ExtractResult result = wuxExtractThread->result;

	delete wuxExtractThread;
	wuxExtractThread = NULL;

	// Fade the progress window out; it is removed once the effect finishes.
	// The effect handler disconnected itself when the fade-in finished, so
	// reconnect it for the fade-out.
	wuxBoxClosing = true;
	if (wuxProgressBox)
	{
		wuxProgressBox->setEffect(EFFECT_FADE, -10, 255);
		wuxProgressBox->setState(GuiElement::STATE_DISABLED);
		wuxProgressBox->effectFinished.connect(this, &MainWindow::OnWuxProgressBoxEffectFinished);
	}

	if (err == wux::Error::Ok && result.ok)
	{
		// Refresh the folder list and select every extracted folder so the
		// installer runs without the user navigating the browser. A disc can
		// yield several titles (e.g. the game plus the rear.rpx dummy).
		if (folderList == NULL)
			folderList = new CFolderList();
		folderList->Get();
		for (size_t d = 0; d < result.outDirs.size(); ++d)
		{
			const std::string& outDir = result.outDirs[d];
			std::string name = outDir.substr(outDir.find_last_of('/') + 1);
			for (int i = 0; i < folderList->GetCount(); ++i)
				if (folderList->GetName(i) == name) { folderList->Select(i); break; }
		}

		if (folderList->GetSelectedCount() == 0)
		{
			// Extraction wrote folders but none is installable (enumeration
			// glitch, missing title.tik): report it instead of starting a
			// 0-selection install, which would dead-end in an uncloseable
			// dialog.
			ShowWuxResult("No installable title found in /install after extraction.", false);
			return;
		}

		// Hand the selected folders to the installer by code. The WUX path
		// skips the "are you sure" prompt, asks about deleting the .wux /
		// game.key / .app files, and gets the cleanup file list.
		if (browserWindow)
		{
			currentDrcFrame->remove(browserWindow);
			AsyncDeleter::pushForDelete(browserWindow);
			browserWindow = NULL;
		}
		installWindowOpen = true;
		installWindow = new InstallWindow(folderList, false, true, true, wuxCleanupFiles, true);
		// The wux flow ends back on the plain main screen (no WUP browser),
		// unlike the browser flow, so it gets its own close handler.
		installWindow->installWindowClosed.connect(this, &MainWindow::OnWuxInstallWindowClosed);
	}
	else
	{
		ShowWuxResult(result.error.empty() ? std::string(wux::errorName(err))
		                                  : result.error, false);
	}
}

void MainWindow::OnWuxProgressBoxEffectFinished(GuiElement *element)
{
	element->effectFinished.disconnect(this);
	
	// Idempotency guard: on a fast fail (the worker dies while the fade-in
	// is still in flight) the fade-in connection is still alive when the
	// fade-out reconnects this handler, so the signal can invoke it twice.
	// Once the box is closed wuxProgressBox is NULL and the second
	// invocation must skip both branches.
	if (wuxProgressBox != element)
		return;
	
	if (wuxBoxClosing)
	{
		// Fade-out finished: remove the box from the tree and delete it.
		currentDrcFrame->remove(element);
		AsyncDeleter::pushForDelete(element);
		wuxProgressBox = NULL;
		wuxBoxClosing = false;
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
}

void MainWindow::OnWuxMessageBoxClick(GuiElement *element, int ok)
{
	currentDrcFrame->remove(element);
	AsyncDeleter::pushForDelete(element);
	
	// The wux flow has ended (extraction failed or no image found): the
	// "install wux" button may be used again.
	wuxBusy = false;
	OSEnableHomeButtonMenu(TRUE);
	Application::instance()->exitEnable();
}
