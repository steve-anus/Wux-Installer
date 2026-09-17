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
#ifndef _MESSAGE_BOX_H_
#define _MESSAGE_BOX_H_

#include "gui/Gui.h"
#include "system/CMutex.h"
	
class MessageBox : public GuiFrame, public sigslot::has_slots<>
{
public:	
	
	MessageBox(int typeButtons = BT_NOBUTTON, int typeIcons = IT_NOICON, bool progressbar = false);
	
	virtual ~MessageBox();
	
	void reload(std::string title, std::string message1, std::string message2, int typeButtons = BT_NOBUTTON, int typeIcons = IT_NOICON, bool progressBar = false, std::string pbInfo = " ");
	// Worker-thread-safe companion of reload(): stages the payload; the
	// reload itself (fade effects, signal connections) is applied on the
	// render thread inside updateEffects().
	void stageReload(std::string title, std::string message1, std::string message2, int typeButtons = BT_NOBUTTON, int typeIcons = IT_NOICON, bool progressbar = false, std::string pbInfo = " ");
	void setTitle(const std::string & title);
	void setMessage1(const std::string & message);
	void setMessage2(const std::string & message);
	void setProgress(f32 percent);
	void setProgressBarInfo(const std::string & info);

	//!Applies queued cross-thread content (see the staging members below);
	//!runs on the render thread via the element update chain.
	virtual void updateEffects();
	
    sigslot::signal2<GuiElement *, int> messageCancelClicked;
	sigslot::signal2<GuiElement *, int> messageOkClicked;
	sigslot::signal2<GuiElement *, int> messageYesClicked;
	sigslot::signal2<GuiElement *, int> messageNoClicked;
	// Fired at the end of MessageBox::updateEffects(), on the render thread.
	// Owners use it to apply GUI-side work queued by their worker (button
	// signal wiring) in the same frame the staged reload landed.
	sigslot::signal0<> effectsTick;
	
	enum ButtonType
    {
		BT_NOBUTTON = -1,
		BT_OK,
		BT_CANCEL,
		BT_YESNO,
		BT_DEST
    };
	
	enum IconType
    {
		IT_NOICON,
		IT_ICONTRUE,
		IT_ICONERROR,
		IT_ICONINFORMATION,
		IT_ICONQUESTION,
		IT_ICONEXCLAMATION,
		IT_ICONWARNING
    };
	
	enum MessageResult
	{
		MR_YES,
		MR_NO,
		MR_OK,
		MR_CANCEL
	};
	
private:
    void OnOkButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
    {
			if(answered)
				return;
			answered = true;
			messageOkClicked(this, MR_OK);
    }
	void OnCancelButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
    {
			if(answered)
				return;
			answered = true;
			messageCancelClicked(this, MR_CANCEL);
    }
	void OnYesButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
    {
			if(answered)
				return;
			answered = true;
			messageYesClicked(this, MR_YES);
    }
	void OnNoButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger)
    {
			if(answered)
				return;
			answered = true;
			messageNoClicked(this, MR_NO);
    }
	
	void setIcon(int typeIcons);
	void setButtons(int typeButtons);
	
	void OnReloadFadeOutFinished(GuiElement * element);
	void OnReloadFadeInFinished(GuiElement * element);
	
	void OnDPADClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	void UpdateButtons(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
	
	GuiImage bgBlur;
	
	GuiText titleText;
	GuiText messageText1;
	GuiText messageText2;
	GuiText infoText;
	
	GuiImageData *iconImageData;
    GuiImage * iconImage;
	
	GuiSound *buttonClickSound;
    GuiImageData *boxImageData;
    GuiImage boxImage;   
	
    GuiTrigger touchTrigger;
    GuiTrigger buttonATrigger;
    GuiTrigger buttonLeftTrigger;
    GuiTrigger buttonRightTrigger;
	
	GuiImageData *buttonImageData;
    GuiImageData *buttonHighlightedImageData;
	
	GuiImageData *bgImageData;
    GuiImage bgImage;
    GuiImage progressImageBlack;
    GuiImage progressImageColored;

    GuiButton DPADButtons;
	
	int selectedButtonDPAD;
	// Single-shot answer latch: one physical press can reach a button twice
	// in a frame (A-proxy + touch). The first handler answers; the echo must
	// not emit a second decision. Cleared when a box has fully faded in.
	bool answered;
	
	typedef struct
    {
        GuiImage *messageButtonImg;
        GuiImage *messageButtonHighlightedImg;
        GuiButton *messageButton;
        GuiText *messageButtonText;
    } MessageButton;

    std::vector<MessageButton> messageButtons;
	
    int buttonCount;
	bool progressBar;
	
	int newButtonsType;
	int newIconType;
	bool newProgressBar;
	std::string newTitle;
	std::string newMessage1;
	std::string newMessage2;
	std::string newInfo;

	//! Cross-thread staging: the install/extract workers only queue content
	//! through the setters; updateEffects() applies it on the render thread.
	//! GuiText mutates glyph buffers and the shared font cache, so it must
	//! never be touched outside the render thread.
	CMutex crossThreadMutex;
	bool pendingTitle;
	bool pendingMessage1;
	bool pendingMessage2;
	bool pendingInfo;
	bool pendingProgress;
	bool pendingReload;
	std::string pendingTitleText;
	std::string pendingMessage1Text;
	std::string pendingMessage2Text;
	std::string pendingInfoText;
	f32 pendingProgressVal;
	
	GuiFrame progressFrame;
};

#endif //_MESSAGE_BOX_H_
