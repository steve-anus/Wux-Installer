#ifndef INSTALL_WINDOW_H_
#define INSTALL_WINDOW_H_

#include <string>
#include <vector>

#include "fs/CFolderList.hpp"
#include "gui/MessageBox.h"
#include "ProgressWindow.h"

class MainWindow;

class InstallWindow : public GuiFrame, public CThread, public sigslot::has_slots<>
{
public:
	// deleteAfterInstall: remove the install folder after each successful
	// title (existing browser checkbox behavior).
	// skipConfirm: skip the "are you sure" prompt and go straight to the
	// destination question (WUX flow: the folders were selected by code).
	// askDelete: after the destination choice, ask whether the files in
	// cleanupFiles (the .wux image and game.key) are deleted together with
	// the install folders once the last title has installed (WUX flow).
	InstallWindow(CFolderList * list, bool deleteAfterInstall = false,
	              bool skipConfirm = false, bool askDelete = false,
	              const std::vector<std::string> &cleanupFiles =
	                  std::vector<std::string>());
	~InstallWindow();
	
	void startInstalling()
	{
		resumeThread();
	}
	
	sigslot::signal1<GuiElement *> installWindowClosed;
	
private:
	void OnValidInstallClick(GuiElement * element, int val);
	void OnDestinationChoice(GuiElement * element, int choice);
	void OnDeleteChoice(GuiElement * element, int choice);
	void OnCloseWindow(GuiElement * element, int val);
	void OnWindowClosed(GuiElement *element);
	void OnInstallProcessCancel(GuiElement *element, int val);
	
	void OnOpenEffectFinish(GuiElement *element);
	void OnCloseEffectFinish(GuiElement *element);
	
	void executeThread();
	void InstallProcess(int pos, int total);
	
	GuiFrame * drcFrame;
	GuiImage * blackBg;   // opaque background, hides the main screen behind
	
	CFolderList * folderList;
	
	MessageBox * messageBox;
	
	MainWindow * mainWindow;
	
	int folderCount;
	bool canceled;
	bool deleteAfterInstall;
	bool askDelete;        // WUX flow: show the delete-files prompt
	bool deleteWuxFiles;   // set to true by the delete prompt answer (Yes)
	std::vector<std::string> cleanupFiles;   // .wux + game.key paths
	int target;
	
	enum
	{
		NAND,
		USB
	};
	
};

#endif
