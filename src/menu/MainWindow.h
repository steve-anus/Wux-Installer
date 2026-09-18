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

    //! Polls the extraction worker once per frame regardless of controller
    //! input. The main loop calls it every frame; without it, a finished
    //! extraction only transitions out when some controller reports input.
    void updateFlow();

    //! True when no flow holds a window, worker, or progress box (a box
    //! mid-fade-out is orthogonal to State, so both tests are needed): the
    //! only state in which the foreground cycle may tear the window down and
    //! rebuild it (the Application::procUI release branch consults this).
    bool isFlowIdle() const
    {
        return wux.state() == WuxFlow::State::Idle &&
               !wux.progressFadingOut() && !wux.progressBox();
    }

    //! Foreground release while a flow runs: asks the extraction worker to
    //! stop and waits a bounded time for it, before the app quits to the
    //! menu. NULL-safe when no worker exists.
    //! True = the writer stopped (or never ran); false = abandoned after the
    //! grace, with the reason already in the log.
    bool abortFlowForExit();

    //! Prints the flow conditions that block a rebuild (state, live progress
    //! box, box mid-fade-out), so the log says which one held the gate.
    void logFlowGate(const char *why) const;

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

	void OnWuxInstallWindowClosed(GuiElement *element);
	void OnWupInstallWindowClosed(GuiElement *element);
	//! Shared tail of the install-window close handlers: releases the flow and
	//! restores the home button. Returns to the plain start screen; the folder
	//! browser is gone, so nothing is rebuilt.
	void FinishInstallFlow(void);
	void OnOpenEffectFinish(GuiElement *element);
	void OnWuxInstallClicked(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void OnWupInstallClicked(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	//! Whole-screen navigation proxy handler: dpad moves the highlight between
	//! the two buttons, A starts the highlighted flow, so one press is one action.
	void OnMainNavClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	//! Highlights exactly one of the two entry buttons and records it as the
	//! A-target. Used by the dpad handlers and by direct taps, so the A
	//! highlight always matches what the user used last.
	void SetMainFocus(bool focusWux);
	//! The single re-entrancy gate for each flow entry point (touch or A).
	void TriggerWuxInstall();
	void TriggerWupInstall();
	void OnWuxMessageBoxClick(GuiElement *element, int ok);
	void RunWuxInstall();
	void RunWupInstall();
	void ShowWuxResult(const std::string &msg, bool ok);
	void ShowWupResult(const std::string &msg, bool ok);
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
    ErrorViewer * errorViewer;

    CMutex guiMutex;

    GuiButton *wuxButton;
    GuiText *wuxLabel;
    GuiTrigger wuxTouchTrigger;
    GuiImage *wuxButtonImage;
    GuiImage *wuxButtonImageHi;   // focused / pointer-over look

    GuiButton *wupButton;
    GuiText *wupLabel;
    GuiTrigger wupTouchTrigger;
    GuiImage *wupButtonImage;
    GuiImage *wupButtonImageHi;

    //! Whole-screen, imageless proxies (see OnMainNavClick). A, up and down
    //! each get their OWN button because a GuiButton stores a single
    //! clickedTrigger: triggers sharing a button let a held direction swallow
    //! the opposite press. None of the three has an image, so none draws; the
    //! two visible buttons keep only touch triggers.
    GuiButton *mainUpButton;    // up trigger: highlights the wux button
    GuiButton *mainDownButton;  // down trigger: highlights the wup button
    GuiButton *mainAButton;     // A: activates the focused button
    GuiTrigger mainAtrigger;
    GuiTrigger mainUpTrigger;
    GuiTrigger mainDownTrigger;
    bool mainFocusWux;   // true = wux highlighted, false = wup highlighted

    //! Whole install flow: one State value plus the flow's resources, where
    //! the re-entrancy guards used to be three independent booleans. Owned
    //! by the GUI thread; see the state table in WuxFlow.h.
    WuxFlow wux;
};

#endif //_MAIN_WINDOW_H_
