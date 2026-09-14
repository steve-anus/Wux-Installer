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
#ifndef _MAIN_WINDOW_H_
#define _MAIN_WINDOW_H_

#include <vector>

#include "gui/GuiFrame.h"
#include "gui/GuiImage.h"
#include "gui/GuiText.h"
#include "gui/GuiTrigger.h"
#include "system/CMutex.h"
#include "WuxFlow.h"

// Only the types used by name below are included; everything reachable
// through a pointer or a reference is forward declared so that adding a
// window class here does not pull its whole dependency chain into every
// file that includes this header.
class CVideo;
class CFolderList;
class BrowserWindow;
class ErrorViewer;
class GuiButton;
class GuiController;
class GuiImageData;

class MainWindow : public sigslot::has_slots<>
{
public:
    MainWindow(int w, int h);
    virtual ~MainWindow();

    void appendTv(GuiElement *e)
    {
        if(!e)
            return;

        removeTv(e);
        tvElements.push_back(e);
    }
    void appendDrc(GuiElement *e)
    {
        if(!e)
            return;

        removeDrc(e);
        drcElements.push_back(e);
    }

    void append(GuiElement *e)
    {
        appendTv(e);
        appendDrc(e);
    }

    void removeTv(GuiElement *e)
    {
        for(u32 i = 0; i < tvElements.size(); ++i)
        {
            if(e == tvElements[i])
            {
                tvElements.erase(tvElements.begin() + i);
                break;
            }
        }
    }
    void removeDrc(GuiElement *e)
    {
        for(u32 i = 0; i < drcElements.size(); ++i)
        {
            if(e == drcElements[i])
            {
                drcElements.erase(drcElements.begin() + i);
                break;
            }
        }
    }

    void remove(GuiElement *e)
    {
        removeTv(e);
        removeDrc(e);
    }

    void drawDrc(CVideo *video);
    void drawTv(CVideo *video);
    void update(GuiController *controller);
    void updateEffects();

    void lockGUI()
    {
        guiMutex.lock();
    }
    void unlockGUI()
    {
        guiMutex.unlock();
    }
	
private:
    void SetupMainView(void);
	void SetDrcHeader(void);
	void SetBrowserWindow(void);

	//! Takes the folder browser out of the draw tree and queues it for
	//! deletion. Single owner of that transition: the paths that used to
	//! null the member directly left the object alive in the tree, and the
	//! ones that deleted it by hand duplicated the removal steps.
	void CloseBrowser(void);
	
	void OnInstallButtonClicked(GuiElement *element);
	void OnBrowserCloseEffectFinish(GuiElement *element);
	void OnInstallWindowClosed(GuiElement *element);
	void OnWuxInstallWindowClosed(GuiElement *element);
	//! Shared tail of the two install-window close handlers: releases the
	//! flow and restores the home button. The browser flow rebuilds the
	//! folder browser, the wux flow returns to the plain start screen.
	void FinishInstallFlow(bool rebuildBrowser);
	void OnOpenEffectFinish(GuiElement *element);
	void OnWuxInstallClicked(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnWuxMessageBoxClick(GuiElement *element, int ok);
	void RunWuxInstall();
	void ShowWuxResult(const std::string &msg, bool ok);
	void StartWuxExtraction(const std::string &wuxPath, const std::string &keyPath,
	                          const std::string &commonKeyPath);
	void OnWuxExtractFinished();
	void OnWuxProgressBoxEffectFinished(GuiElement *element);
	
	int width, height;
    std::vector<GuiElement *> drcElements;
    std::vector<GuiElement *> tvElements;

	GuiImageData *titleImgData;
    GuiImage titleImg;
	GuiText titleText;
	GuiText versionText;
    GuiFrame headerFrame;
    
    GuiFrame * currentDrcFrame;

    GuiImageData *pointerImgData[4];
    GuiImage *pointerImg[4];
    bool pointerValid[4];
	
	CFolderList * folderList;
    BrowserWindow * browserWindow;
    ErrorViewer * errorViewer;

    CMutex guiMutex;

    GuiButton *wuxButton;
    GuiText *wuxLabel;
    GuiTrigger wuxTrigger;
    GuiTrigger wuxTouchTrigger;
    GuiImage *wuxButtonImage;

    //! Whole install flow: one State value plus the flow's resources, where
    //! the re-entrancy guards used to be three independent booleans. Owned
    //! by the GUI thread; see the state table in WuxFlow.h.
    WuxFlow wux;
};

#endif //_MAIN_WINDOW_H_
