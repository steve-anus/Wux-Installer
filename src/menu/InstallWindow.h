#ifndef INSTALL_WINDOW_H_
#define INSTALL_WINDOW_H_

#include <atomic>
#include <string>
#include <vector>

#include "fs/CFolderList.hpp"
#include "gui/MessageBox.h"

class MainWindow;

class InstallWindow : public GuiFrame, public CThread, public sigslot::has_slots<>
{
public:
	// deleteAfterInstall: remove the install folder after each successful
	// title (set by the delete prompt; No keeps every folder).
	// skipConfirm: skip the "are you sure" prompt and go straight to the
	// destination question (WUX flow: the folders were selected by code).
	// askDelete: after the destination choice, ask the delete-after-install
	// question. For the .wux flow Yes also removes cleanupFiles (the .wux
	// image and game.key); the manual flow passes none, so only its own
	// /install folders can ever be deleted.
	// wuxFlow: titles outside the installable categories (e.g. the disc's
	// rear.rpx dummy, 00050010-10060000) are skipped as non-fatal instead
	// of failing the chain - they are the extraction's own output and the
	// console already ships them on NAND.
	// finalNote: non-fatal extraction accounting (e.g. "extracted 2 of 3
	// titles") appended to the final success message.
	struct InstallOptions
	{
		bool deleteAfterInstall = false;
		bool skipConfirm = false;
		bool askDelete = false;
		std::vector<std::string> cleanupFiles;
		bool wuxFlow = false;
		std::string finalNote;
	};

	InstallWindow(CFolderList * list, const InstallOptions & options);
	~InstallWindow();
	
	void startInstalling();
	
	sigslot::signal1<GuiElement *> installWindowClosed;
	
private:
	void OnValidInstallClick(GuiElement * element, int val);
	void OnDestinationChoice(GuiElement * element, int choice);
	void OnDeleteChoice(GuiElement * element, int choice);
	void OnCloseWindow(GuiElement * element, int val);
	void OnWindowClosed(GuiElement *element);
	void OnInstallProcessCancel(GuiElement *element, int val);
	
	void OnOpenEffectFinish(GuiElement *element);
	
	void executeThread();
	void InstallProcess(int pos, int total);
	
	// Benign outcome for a skipped (non-installable) title (both flows).
	static const int kResultSkip = -100;

	enum
	{
		NAND,
		USB
	};

	GuiFrame * drcFrame;
	GuiImage * blackBg;   // opaque background, hides the main screen behind
	
	CFolderList * folderList;
	
	MessageBox * messageBox;
	
	MainWindow * mainWindow;
	
	int folderCount;
	// True only once the user has confirmed the install (see startInstalling).
	// executeThread() returns immediately if it is unset, so a window dismissed
	// before confirmation can never run a declined install.
	std::atomic<bool> startRequested = { false };
	std::atomic<bool> canceled = { false };
	bool deleteAfterInstall;
	bool askDelete;        // show the delete-after-install prompt (both flows)
	bool deleteWuxFiles;   // set by Yes; .wux flow only (removes cleanupFiles)
	bool wuxFlow;          // extraction run: skipped folders are ours, so
	                       // cleanup and count-only box wording apply to it
	int installedCount;    // titles actually installed this run
	int skippedCount;      // titles skipped as non-installable
	std::vector<std::string> skippedNames; // folders skipped (named on screen for manual runs)
	bool lastWasSkip;      // suppress the 6 s countdown after a skip
	std::string lastGoodName; // name of the last installed title
	bool wuxDeleteFailed;  // .wux/game.key cleanup failed (success note)
	std::vector<std::string> cleanupFiles;   // .wux + game.key paths
	std::string finalNote;     // extraction accounting for the final box
	int target = NAND;         // set by the destination prompt before each title
	bool startEscape = { false }; // close tail already running after a failed thread start

	// Worker-queued MessageBox wiring. The box's sigslot signals may only be
	// touched from the render thread, so the install worker queues these ops
	// and OnBoxTick (connected to MessageBox::effectsTick) applies them.
	enum BoxOp { OP_CONNECT_CANCEL = 1, OP_DISCONNECT_CANCEL, OP_CONNECT_OK };
	void queueBoxOp(int op);
	void OnBoxTick();
	bool boxClosing;
	bool deleteFoldersFailed;    // an extracted folder could not be removed
	std::vector<int> pendingBoxOps;
	CMutex opMutex;
	
};

#endif
