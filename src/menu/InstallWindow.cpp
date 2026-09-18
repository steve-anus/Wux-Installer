/****************************************************************************
 * Copyright (C) 2011 Dimok
 * Copyright (C) 2012 Cyan
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
#include "Application.h"
#include "InstallWindow.h"
#include "utils/StringTools.h"
#include "utils/logger.h"
#include "common/common.h"
#include "common/fs_defs.h"
#include "system/power.h"
#include "fs/fs_utils.h"
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#include <coreinit/ios.h>
#include <coreinit/filesystem.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#define MCP_COMMAND_INSTALL_ASYNC   0x81
#define MAX_INSTALL_PATH_LENGTH     0x27F

static std::atomic<int> installCompleted(0);
static std::atomic<u32> installError(0);
// Identity token of the install the wait loop currently follows.
// IOS_IoctlvAsync echoes the context pointer back to the callback, so a
// late callback from an abandoned install (stall/cancel break-out) cannot
// publish into the flags and fake the next title's result.
static std::atomic<u32> installGen(0);

extern "C" MCPError MCP_GetLastRawError(void);

static void IosInstallCallback(IOSError errorCode, void * priv_data)
{
	if((u32)(uintptr_t)priv_data != installGen.load())
		return; // stale callback from an abandoned install
	installError = errorCode;
	installCompleted = 1;
}

// True if s[pos..] contains at least one character that is neither '/' nor
// '.': the tail after a boundary slash is a real name, not "", "/", "." or
// "..", which path normalization could fold back onto the volume root.
// A name that merely starts with dots (".hidden") passes.
static bool hasRealName(const std::string &s, size_t pos)
{
	for (; pos < s.size(); pos++)
		if (s[pos] != '/' && s[pos] != '.')
			return true;
	return false;
}

// The POSIX layer on the Wii U (wut's newlib glue) removes files through the
// legacy coreinit FSARemove API, which can fail on files that were not
// created through that layer (e.g. the .wux image and game.key, which the
// user put on the SD card). This removes the file through the modern coreinit
// FS client instead. Only the client is added/removed here - no FSInit/
// FSShutdown: the FS module stays up for the whole session, and ErrorViewer
// keeps its own client registered for the app's entire lifetime.
static bool deleteViaFsClient(const std::string &fsPath)
{
	// newlib path "fs:/vol/external01/..." -> coreinit FS path "sd:/...".
	// The FS client mounts the volume root at "sd:/", so the whole
	// "fs:/vol/external01" volume prefix must go, not just the "fs:" scheme
	// (leaving it produced "sd:/vol/external01/...", which never resolves).
	char path[MAX_INSTALL_PATH_LENGTH];
	static const char volPrefix[] = "fs:/vol/external01";
	const size_t volPrefixLen = sizeof(volPrefix) - 1;
	// Only ever delete a path whose tail below the volume root is a real
	// name: a bare "fs:/vol/external01", "fs:/vol/external01/", "fs:/" or a
	// dot-tail (".../.." normalization) would otherwise reach FSRemove at
	// the card root. A prefix match needs a '/' boundary AND a real name
	// after it (hasRealName); volume-shaped paths are honored only through
	// the first branch. Every other shape is refused with a log, never
	// passed raw.
	if (fsPath.compare(0, volPrefixLen, volPrefix) == 0 &&
	    fsPath.size() > volPrefixLen + 1 && fsPath[volPrefixLen] == '/' &&
	    hasRealName(fsPath, volPrefixLen + 1))
		snprintf(path, sizeof(path), "sd:%s", fsPath.c_str() + volPrefixLen);
	else if (fsPath.compare(0, 3, "fs:") == 0 && fsPath.size() > 4 &&
	         fsPath[3] == '/' && hasRealName(fsPath, 4) &&
	         fsPath.compare(0, volPrefixLen, volPrefix) != 0)
		snprintf(path, sizeof(path), "sd:%s", fsPath.c_str() + 3);
	else
	{
		log_printf("InstallWindow: deleteViaFsClient refused malformed path: %s",
		           fsPath.c_str());
		return false;
	}
	
	FSClient client;
	if (FSAddClient(&client, FS_ERROR_FLAG_NONE) != FS_STATUS_OK)
		return false;
	
	FSCmdBlock block;
	FSInitCmdBlock(&block);
	FSStatus st = FSRemove(&client, &block, path, FS_ERROR_FLAG_NONE);
	
	if (FSDelClient(&client, FS_ERROR_FLAG_NONE) != FS_STATUS_OK)
		log_printf("InstallWindow: FSDelClient failed after FSRemove");
	
	// FS_STATUS_NOT_FOUND = already gone: the desired end state for a
	// cleanup delete, but logged, so a card that was pulled and remounted
	// empty cannot silently "succeed" here.
	if (st == FS_STATUS_NOT_FOUND)
	{
		log_printf("InstallWindow: already gone via FS client: %s", fsPath.c_str());
		return true;
	}
	return st == FS_STATUS_OK;
}

// The delete-stop boundary for an install folder path (shared by the
// success tail and the WUX skip).
static const char *stopAtFor(const std::string &path)
{
	// The prefix must end at a directory boundary: "install" must not match
	// "installation", and a match must have a folder name after the slash.
	auto below = [&path](const char *prefix) {
		size_t len = strlen(prefix);
		return path.size() > len + 1 && path.compare(0, len, prefix) == 0 &&
		       path[len] == '/';
	};
	if (below(SD_INSTALL_PATH))
		return SD_INSTALL_PATH;
	if (below(SD_WUDUMP_PATH))
		return SD_WUDUMP_PATH;
	return NULL;
}

InstallWindow::InstallWindow(CFolderList * list, const InstallOptions & options)
	: GuiFrame(0, 0)
	, CThread(CThread::eAttributeAffCore0 | CThread::eAttributePinnedAff)
	, folderList(list)
	, deleteAfterInstall(options.deleteAfterInstall)
	, askDelete(options.askDelete)
	, deleteWuxFiles(false)
	, wuxFlow(options.wuxFlow)
	, installedCount(0)
	, skippedCount(0)
	, lastWasSkip(false)
	, wuxDeleteFailed(false)
	, cleanupFiles(options.cleanupFiles)
	, finalNote(options.finalNote)
	, boxClosing(false)
	, deleteFoldersFailed(false)
{
	mainWindow = Application::instance()->getMainWindow();
	
	folderCount = folderList->GetSelectedCount();
	
	if(options.skipConfirm && folderCount > 0)
	{
		// WUX flow: the folders were selected by code, so skip the
		// "are you sure" prompt and go straight to the destination question.
		messageBox = new MessageBox(MessageBox::BT_DEST, MessageBox::IT_ICONQUESTION, false);
		messageBox->setTitle("Where do you want to install?");
		messageBox->setMessage1(strfmt("%d application(s)", folderCount));
		messageBox->messageYesClicked.connect(this, &InstallWindow::OnDestinationChoice);
		messageBox->messageNoClicked.connect(this, &InstallWindow::OnDestinationChoice);
	}
	else if(folderCount > 0)
	{
		std::string message = strfmt("%d application(s)", folderCount);
		messageBox = new MessageBox(MessageBox::BT_YESNO, MessageBox::IT_ICONQUESTION, false);
		messageBox->setTitle("Are you sure you want to install:");
		messageBox->setMessage1(message);
		messageBox->messageYesClicked.connect(this, &InstallWindow::OnValidInstallClick);
		messageBox->messageNoClicked.connect(this, &InstallWindow::OnCloseWindow);
	}
	else
	{
		messageBox = new MessageBox(MessageBox::BT_OK, MessageBox::IT_ICONEXCLAMATION, false);
		messageBox->setTitle("No content selected.");
		messageBox->setMessage1("Nothing was selected to install.");
		messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
	}
	
	drcFrame = new GuiFrame(0, 0);
	drcFrame->setEffect(EFFECT_FADE, 10, 255);
	drcFrame->setState(GuiElement::STATE_DISABLED);
	drcFrame->effectFinished.connect(this, &InstallWindow::OnOpenEffectFinish);
	
	// Opaque black background behind the box (appended first, so it draws
	// under the MessageBox): the MessageBox's own dim layer is only 50%
	// alpha, which let the main screen show through during the install.
	blackBg = new GuiImage(1280, 720, (GX2Color){0, 0, 0, 255});
	drcFrame->append(blackBg);
	drcFrame->append(messageBox);

	messageBox->effectsTick.connect(this, &InstallWindow::OnBoxTick);
	mainWindow->append(drcFrame);
}

InstallWindow::~InstallWindow()
{
	drcFrame->remove(blackBg);
	drcFrame->remove(messageBox);
	mainWindow->remove(drcFrame);
	delete drcFrame;
	delete blackBg;
	delete messageBox;
}

void InstallWindow::OnValidInstallClick(GuiElement * element, int val)
{
	messageBox->messageYesClicked.disconnect(this);
	messageBox->messageNoClicked.disconnect(this);
	messageBox->reload("Where do you want to install?", "", "", MessageBox::BT_DEST, MessageBox::IT_ICONQUESTION);
	messageBox->messageYesClicked.connect(this, &InstallWindow::OnDestinationChoice);
	messageBox->messageNoClicked.connect(this, &InstallWindow::OnDestinationChoice);
}

void InstallWindow::OnDestinationChoice(GuiElement * element, int choice)
{
	if(choice == MessageBox::MR_YES)
		target = NAND;
	else
		target = USB;
	
	messageBox->messageYesClicked.disconnect(this);
	messageBox->messageNoClicked.disconnect(this);
	
	if(target == USB)
	{
		// Preflight: an unreachable USB drive otherwise surfaces mid-chain,
		// after earlier titles may already have installed. Asking MCP to accept
		// the USB target now turns that into one clear upfront error.
		u32 probe = MCP_Open();
		int resDev = -1, resUsb = -1;
		if(probe != 0)
		{
			resDev = MCP_InstallSetTargetDevice(probe, (MCPInstallTarget)USB);
			resUsb = MCP_InstallSetTargetUsb(probe, (MCPInstallTarget)USB);
			MCP_Close(probe);
		}
		if(probe == 0 || resDev != 0 || resUsb != 0)
		{
			log_printf("InstallWindow: USB pre-check failed (handle 0x%08X dev %d usb %d)",
			           probe, resDev, resUsb);
			messageBox->reload("Install failed", "",
			                   "No USB storage detected. Connect the drive and start the install again.",
			                   MessageBox::BT_OK, MessageBox::IT_ICONERROR);
			messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
			return;
		}
	}
	
	if(askDelete)
	{
		// .wux flow: image + game.key + extracted folders (removed after
		// the last title). Manual flow: its /install folders, on success.
		std::string question;
		if(wuxFlow)
		{
			std::string fileName = cleanupFiles.empty() ? std::string()
				: cleanupFiles[0].substr(cleanupFiles[0].find_last_of('/') + 1);
			question = strfmt("Delete %s, game.key and extracted files from the SD card after a successful install?",
			                  fileName.c_str());
		}
		else
		{
			question = "Delete the installed WUP folders from the SD card after a successful install?";
		}
		messageBox->reload("Delete files after install?",
			question,
			"", MessageBox::BT_YESNO, MessageBox::IT_ICONQUESTION);
		messageBox->messageYesClicked.connect(this, &InstallWindow::OnDeleteChoice);
		messageBox->messageNoClicked.connect(this, &InstallWindow::OnDeleteChoice);
		return;
	}
	
	startInstalling();
}

void InstallWindow::OnDeleteChoice(GuiElement * element, int choice)
{
	messageBox->messageYesClicked.disconnect(this);
	messageBox->messageNoClicked.disconnect(this);
	
	// Yes: deleteAfterInstall for both flows; deleteWuxFiles only for the
	// .wux flow (a manual run has no cleanupFiles, never deletes /wudump).
	// No: keep everything on the SD card.
	if(choice == MessageBox::MR_YES)
	{
		deleteAfterInstall = true;
		if(wuxFlow)
			deleteWuxFiles = true;
	}
	
	startInstalling();
}

void InstallWindow::executeThread()
{
	// The ctor creates this thread suspended; only a confirmed install resumes it
	// via startInstalling(). Bail out unless that happened, so the entry point can
	// never run for a window dismissed before confirmation. (In today's build a
	// thread still suspended at ~CThread is woken by the destructor's resume-before-
	// join but dispatches to the base CThread::executeThread(); this flag keeps the
	// invariant explicit and holds if that resume is ever hoisted out.)
	if(!startRequested)
		return;

	log_printf("InstallWindow: install thread entered (confirmed install) selected=%d\n",
	           folderList->GetSelectedCount());

	canceled = false;
	
	bool APD_enabled = isEnabledAutoPowerDown();
	if(APD_enabled)
		disableAutoPowerDown();
	
	int total = folderList->GetSelectedCount();
	int pos = 1;
	
	while(pos <= total && !canceled)
	{
		InstallProcess(pos, total);
		
		if(pos < total && !lastWasSkip)
		{
			int time = 6;
			u64 startTime = OSGetTime();
			u32 passedMs = 0;
			
			while(time && !canceled)
			{
				passedMs = OSTicksToMilliseconds(OSGetTime() - startTime);
				
				if(passedMs >= 1000)
				{
					time--;
					startTime = OSGetTime();
					messageBox->setMessage2(strfmt("Starting next installation in %d second(s)", time));
				}

				usleep(100 * 1000);
			}
			
			queueBoxOp(OP_DISCONNECT_CANCEL);
		}
		else if(pos < total && lastWasSkip)
		{
			// A skip shows no box: give the previous box fade time to settle
			// before the next iteration's reload (the MessageBox is mutated
			// from this thread while the GUI thread runs its fade handlers).
			usleep(600 * 1000);
		}
		
		pos++;
	}

	// A last title that was skipped left its "Installing..." box mid-fade;
	// let it settle before the finalize reload (same guard as in the loop).
	if(!canceled && lastWasSkip)
		usleep(600 * 1000);

	// Final box + cleanup once the whole chain is done. A hard failure or a
	// cancel already showed its own box and set canceled, so nothing here
	// runs in those cases (files stay on the SD card for a retry).
	if(!canceled)
	{
		// Join the skipped folder names for the box (full per-folder detail
		// is already in the SD log at each skip).
		auto joinSkipped = [this]() {
			std::string s;
			for(size_t i = 0; i < skippedNames.size(); ++i)
			{
				if(i)
					s += ", ";
				s += skippedNames[i];
			}
			return s;
		};

		if(installedCount > 0)
		{
			// The .wux image and game.key (in /wudump) are removed after the
			// last processed title, but only if something really installed
			// (retry-friendly otherwise). common.key is never in
			// cleanupFiles. A failed POSIX unlink (the same layer that removes
			// the extracted /install folders) falls back to the coreinit FS
			// client; both outcomes are logged for the hardware debug.
			if(deleteWuxFiles)
			{
				for(size_t i = 0; i < cleanupFiles.size(); ++i)
				{
					if (unlink(cleanupFiles[i].c_str()) != 0 &&
					    !deleteViaFsClient(cleanupFiles[i]))
					{
						wuxDeleteFailed = true;
						log_printf("InstallWindow: could not delete %s",
						           cleanupFiles[i].c_str());
					}
					else
						log_printf("InstallWindow: deleted %s",
						           cleanupFiles[i].c_str());
				}
			}

			std::string note;
			if(wuxDeleteFailed)
				note = "Could not delete the .wux / game.key from the SD card, remove them manually.";
			if(deleteFoldersFailed)
			{
				if(!note.empty())
					note += " ";
				note += "Some extracted folders could not be removed from the card, delete them manually.";
			}
			if(skippedCount > 0)
			{
				if(!note.empty())
					note += " ";
				// Manual runs name what was skipped (the folders are the
				// user's own placements); extraction output is ours, so a
				// count suffices.
				std::string names = wuxFlow ? std::string() : joinSkipped();
				if(names.empty())
					note += strfmt("Skipped %d non-installable title(s).",
					               skippedCount);
				else
					note += strfmt("Skipped %d non-installable title(s): %s.",
					               skippedCount, names.c_str());
			}
			if(!finalNote.empty())
			{
				if(!note.empty())
					note += " ";
				note += finalNote;
			}

			if(!note.empty())
				log_printf("InstallWindow: finalize note: %s", note.c_str());

			// Keep the box readable: the message2 line gets at most ~120
			// characters; each skip also has its own full log line above.
			std::string boxNote = note;
			if(boxNote.size() > 120)
			{
				boxNote.resize(117);
				boxNote += "...";
			}

			messageBox->stageReload("Successfully installed", lastGoodName, boxNote,
			                   MessageBox::BT_OK, MessageBox::IT_ICONTRUE);
			queueBoxOp(OP_CONNECT_OK);
		}
		else
		{
			// Every selected folder was a non-installable title.
			std::string msg = "No installable titles found (games, updates, DLC, demos and version titles install).";
			if(!wuxFlow && !skippedNames.empty())
			{
				msg += " Not installable: " + joinSkipped();
				if(msg.size() > 120)
				{
					msg.resize(117);
					msg += "...";
				}
			}
			messageBox->stageReload("Install failed", "", msg,
			                   MessageBox::BT_OK, MessageBox::IT_ICONERROR);
			queueBoxOp(OP_CONNECT_OK);
		}
	}
	
	if(APD_enabled)
		enableAutoPowerDown();
}

void InstallWindow::InstallProcess(int pos, int total)
{
	int index = folderList->GetFirstSelected();
	
	std::string title = strfmt("Installing... (%d/%d)", pos, total);
	std::string gameName = folderList->GetName(index);
	
	lastWasSkip = false;
	
	messageBox->stageReload(title, gameName, "", MessageBox::BT_NOBUTTON, MessageBox::IT_ICONINFORMATION, true, "0.0 %");
	
	/////////////////////////////
	// install process
	/////////////////////////////
	
	int result = 0;
	// Set when the wait loop breaks while the async install request is
	// still in flight; the tail must then keep everything IOS may use.
	bool abandonedInstall = false;
	// Fresh identity for this install before anything is submitted: a
	// callback still pending from an earlier abandoned install carries the
	// old identity and is dropped by IosInstallCallback instead of faking
	// a completion here.
	u32 myGen = installGen.fetch_add(1) + 1;
	installCompleted = 0;
	installError = 0;
	
	//!---------------------------------------------------
	//! This part of code originates from Crediars MCP patcher assembly code
	//! it is just translated to C
	//!---------------------------------------------------
	unsigned int mcpHandle = MCP_Open();
	if(mcpHandle == 0)
	{
		messageBox->stageReload("Install failed", gameName, "Failed to open MCP.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
		
		result = -1;
	}
	else
	{
        char installPath[256];
		unsigned int * mcpInstallInfo = (unsigned int *)OSAllocFromSystem(0x24, 0x40);
		char * mcpInstallPath = (char *)OSAllocFromSystem(MAX_INSTALL_PATH_LENGTH, 0x40);
		IOSVec * mcpPathInfoVector = (IOSVec *)OSAllocFromSystem(0x0C, 0x40);
		
		do
		{
			if(!mcpInstallInfo || !mcpInstallPath || !mcpPathInfoVector)
			{
				messageBox->stageReload("Install failed", gameName, "Could not allocate memory.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -2;
				break;
			}
			
			std::string installFolder = folderList->GetPath(index);
			// MCP reads through the app's own FS mount: the
			// "fs:/vol/external01/" prefix becomes "/vol/app_sd/". Compare
			// the prefix instead of blindly erasing 19 bytes; a path that
			// does not match cannot be installed by this flow.
			static const char fsPrefix[] = "fs:/vol/external01/";
			const size_t fsPrefixLen = sizeof(fsPrefix) - 1;
			if (installFolder.size() <= fsPrefixLen ||
			    installFolder.compare(0, fsPrefixLen, fsPrefix) != 0)
			{
				messageBox->stageReload("Install failed", gameName, "Folder path is not under the SD card mount.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -12;
				break;
			}
			installFolder = std::string("/vol/app_sd/") +
			                installFolder.substr(fsPrefixLen);
            
            snprintf(installPath, sizeof(installPath), "%s", installFolder.c_str());
			
			int res = MCP_InstallGetInfo(mcpHandle, installPath, (MCPInstallInfo*)mcpInstallInfo);
			if(res != 0)
			{
				//__os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallGetInfo 0x%08X", MCP_GetLastRawError());
				messageBox->stageReload(installFolder, gameName, "Confirm complete WUP files are in the folder.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -3;
				break;
			}
			
			u32 titleIdHigh = mcpInstallInfo[0];
			u32 titleIdLow = mcpInstallInfo[1];
			bool spoofFiles = false;
			// The 0x prefix was missing originally: "00050010" is octal
			// (= 0x5008), so this spoof branch never matched; the intended
			// category is 0x00050010.
			if ((titleIdHigh == 0x00050010)
				&&(	   (titleIdLow == 0x10041000)     // JAP Version.bin
					|| (titleIdLow == 0x10041100)     // USA Version.bin
					|| (titleIdLow == 0x10041200)))   // EUR Version.bin
			{
				spoofFiles = true;
			}
			// The Version.bin spoof forces NAND for this title only; writing
			// the member would silently override the user's chosen
			// destination for every later title in the run.
			int effTarget = spoofFiles ? NAND : target;
			
			if (spoofFiles
			   || (titleIdHigh == 0x0005000E)     // game update
			   || (titleIdHigh == 0x00050000)     // game
			   || (titleIdHigh == 0x0005000C)     // DLC
			   || (titleIdHigh == 0x00050002))    // Demo
			{
				res = MCP_InstallSetTargetDevice(mcpHandle, (MCPInstallTarget)(effTarget));
				if(res != 0)
				{
					messageBox->stageReload("Install failed", gameName, strfmt("MCP_InstallSetTargetDevice 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					//if (installToUsb)
					//	__os_snprintf(errorText2, sizeof(errorText2), "Possible USB HDD disconnected or failure");
					result = -5;
					break;
				}
				res = MCP_InstallSetTargetUsb(mcpHandle, (MCPInstallTarget)(effTarget));
				if(res != 0)
				{
					messageBox->stageReload("Install failed", gameName, strfmt("MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					//if (installToUsb)
					//	__os_snprintf(errorText2, sizeof(errorText2), "Possible USB HDD disconnected or failure");
					result = -6;
					break;
				}
				
				mcpInstallInfo[2] = (unsigned int)MCP_COMMAND_INSTALL_ASYNC;
				mcpInstallInfo[3] = (unsigned int)mcpPathInfoVector;
				mcpInstallInfo[4] = (unsigned int)1;
				mcpInstallInfo[5] = (unsigned int)0;
				
				memset(mcpInstallPath, 0, MAX_INSTALL_PATH_LENGTH);
				snprintf(mcpInstallPath, MAX_INSTALL_PATH_LENGTH, "%s", installFolder.c_str());
				memset(mcpPathInfoVector, 0, 0x0C);
				
				mcpPathInfoVector->vaddr = mcpInstallPath;
				mcpPathInfoVector->len = (unsigned int)MAX_INSTALL_PATH_LENGTH;
				
				res = IOS_IoctlvAsync(mcpHandle, MCP_COMMAND_INSTALL_ASYNC, 1, 0, mcpPathInfoVector, IosInstallCallback, (void *)(uintptr_t)myGen);
				if(res != 0)
				{
					messageBox->stageReload("Install failed", gameName, strfmt("MCP_InstallTitleAsync 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					result = -7;
					break;
				}
				
				// Wait for the IOS callback. There is deliberately no overall
				// time cap (multi-GB installs legitimately run for minutes).
				// The 5-minute stall detector only arms once MCP has
				// published a progress record: long prepare/commit phases
				// can hold the byte counter at zero legitimately. A run
				// that never gets any record escapes at 30 minutes. The
				// canceled check is defence in depth (this box has no
				// button while installing).
				bool sawRecord = false;
				u64 lastSeenBytes = 0;
				u64 stallStart = OSGetTime();
				const u64 loopStart = stallStart;
				while(!installCompleted)
				{
					if(canceled)
					{
						messageBox->stageReload("Install failed", gameName, "The install was stopped.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						result = -11;
						abandonedInstall = true;
						installGen.fetch_add(1); // drop the still-pending callback
						break;
					}

					memset(mcpInstallInfo, 0, 0x24);
					
					MCP_InstallGetProgress(mcpHandle, (MCPInstallProgress*)mcpInstallInfo);
					
					if(mcpInstallInfo[0] == 1)
					{
						u64 totalSize = ((u64)mcpInstallInfo[3] << 32ULL) | mcpInstallInfo[4];
						u64 installedSize = ((u64)mcpInstallInfo[5] << 32ULL) | mcpInstallInfo[6];
						int percent = (totalSize != 0) ? ((installedSize * 100.0f) / totalSize) : 0;
						
						std::string message = strfmt("%0.1f / %0.1f MB (%i", installedSize / (1024.0f * 1024.0f), totalSize / (1024.0f * 1024.0f), percent);
						message += "%)";
						
						messageBox->setProgress(percent);
						messageBox->setProgressBarInfo(message);

						// Arm the stall timer on the first record, re-arm
						// when bytes actually move. 0 bytes seen is not
						// treated as progress.
						if(!sawRecord || installedSize != lastSeenBytes)
						{
							sawRecord = true;
							lastSeenBytes = installedSize;
							stallStart = OSGetTime();
						}
					}

					bool stalled = sawRecord &&
					                 (OSTicksToMilliseconds(OSGetTime() - stallStart) > 300000);
					bool noRecord = !sawRecord &&
					                (OSTicksToMilliseconds(OSGetTime() - loopStart) > 1800000);
					if(stalled || noRecord)
					{
						messageBox->stageReload("Install failed", gameName,
						                   stalled ? "The install made no progress for 5 minutes."
						                           : "The install never reported progress.",
						                   MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						result = -10;
						abandonedInstall = true;
						installGen.fetch_add(1); // drop the still-pending callback
						break;
					}
					
					usleep(50000);
				}
				
				if(!abandonedInstall && installError != 0)
				{
					if ((installError == 0xFFFCFFE9) && (effTarget == USB))
					{
						messageBox->stageReload("Install failed", gameName, strfmt("0x%08X access failed (no USB storage attached?)", (unsigned)installError.load()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						result = -8;
					}
					else
					{
						//__os_snprintf(errorText1, sizeof(errorText1), "Error: install error code 0x%08X", installError);
						if (installError == 0xFFFBF446 || installError == 0xFFFBF43F)
							messageBox->stageReload("Install failed", gameName, "Possible missing or bad title.tik file", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFBF441)
							messageBox->stageReload("Install failed", gameName, "Possible incorrect console for DLC title.tik file", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFCFFE4)
							messageBox->stageReload("Install failed", gameName, "Possible not enough memory on target device", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFFF825)
							messageBox->stageReload("Install failed", gameName, "Possible bad SD card.  Reformat (32k blocks) or replace", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if ((installError & 0xFFFF0000) == 0xFFFB0000)
							messageBox->stageReload("Install failed", gameName, "Verify WUP files are correct & complete. DLC/E-shop require Sig Patch", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else
							messageBox->stageReload("Install failed", gameName, strfmt("Install failed with error code 0x%08X", (unsigned)installError.load()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						
						result = -9;
					}
				}
			}
			else
			{
				// Non-whitelist category (e.g. the disc's rear.rpx dummy):
				// benign in both flows - log, count, continue the chain. A
				// skipped manual folder was never installed, so it is never
				// removed; cleanup of skipped folders applies only to
				// deleting extraction runs.
				log_printf("InstallWindow: skipping non-installable title %08X-%08X (%s)",
				           titleIdHigh, titleIdLow, gameName.c_str());
				if(wuxFlow && deleteAfterInstall)
				{
					std::string path = folderList->GetPath(index);
					if(RemoveDirectoryAndEmptyParents(path.c_str(), stopAtFor(path)) != 0)
					{
						deleteFoldersFailed = true;
						log_printf("InstallWindow: could not remove skipped folder %s",
						           path.c_str());
					}
				}
				folderList->UnSelect(index);
				++skippedCount;
				skippedNames.push_back(gameName);
				lastWasSkip = true;
				result = kResultSkip;
			}
		}
		while(0);
		
		if(!abandonedInstall)
		{
			MCP_Close(mcpHandle);
			if(mcpPathInfoVector)
				OSFreeToSystem(mcpPathInfoVector);
			if(mcpInstallPath)
				OSFreeToSystem(mcpInstallPath);
			if(mcpInstallInfo)
				OSFreeToSystem(mcpInstallInfo);
		}
		else
		{
			// IOS maps the ioctlv buffers and the info block for the whole
			// lifetime of the async request and releases them when its
			// callback fires; freeing them (or closing the handle) now
			// would hand IOS freed memory. The chain stops right here (the
			// failure tail sets canceled), so leaking these few hundred
			// bytes is bounded - and the generation bump above ensures the
			// late callback cannot publish into the next install's flags.
			log_printf("InstallWindow: abandoned install still in flight; buffers and MCP handle deliberately kept");
		}
	}
	/////////////////////////////
	
	if(result >= 0)
	{
		++installedCount;
		lastGoodName = gameName;

		if(deleteAfterInstall)
		{
			std::string path = folderList->GetPath(index);
			const char *stop = stopAtFor(path);
			if(!wuxFlow && (stop == NULL || strcmp(stop, SD_INSTALL_PATH) != 0))
			{
				// A manual run may only ever remove its own /install
				// folders; anything else the list might hold is refused.
				deleteFoldersFailed = true;
				log_printf("InstallWindow: refused to remove non-/install folder %s",
				           path.c_str());
			}
			else if(RemoveDirectoryAndEmptyParents(path.c_str(), stop) != 0)
			{
				deleteFoldersFailed = true;
				log_printf("InstallWindow: could not remove installed folder %s",
				           path.c_str());
			}
		}

		if(pos < total)
		{
			messageBox->stageReload("Successfully installed", gameName, "Starting next installation in 6 second(s)", MessageBox::BT_CANCEL, MessageBox::IT_ICONTRUE);
			queueBoxOp(OP_CONNECT_CANCEL);
		}
		// The final box (and the .wux/game.key cleanup) is shown by
		// executeThread once the whole loop is done, so it also covers a
		// last folder that was skipped rather than installed.
		
		folderList->UnSelect(index);
	}
	else if(result == kResultSkip)
	{
		// Benign skip (both flows): handled where the title type was
		// checked (selection advanced, counters updated; deleting
		// extraction runs also cleaned the folder). No box.
	}
	else
	{
		queueBoxOp(OP_CONNECT_OK);
		
		canceled = true;
		folderList->UnSelectAll();
	}
}

void InstallWindow::queueBoxOp(int op)
{
	// Callable from the install worker: the box's sigslot signals are
	// single-threaded and GUI-owned, so wiring is queued and applied by
	// OnBoxTick on the render thread.
	opMutex.lock();
	pendingBoxOps.push_back(op);
	opMutex.unlock();
}

void InstallWindow::OnBoxTick()
{
	std::vector<int> ops;
	opMutex.lock();
	ops.swap(pendingBoxOps);
	opMutex.unlock();

	if(boxClosing)
		return;

	for(size_t i = 0; i < ops.size(); ++i)
	{
		switch(ops[i])
		{
			case OP_CONNECT_CANCEL:
				messageBox->messageCancelClicked.connect(this, &InstallWindow::OnInstallProcessCancel);
				break;
			case OP_DISCONNECT_CANCEL:
				messageBox->messageCancelClicked.disconnect(this);
				break;
			case OP_CONNECT_OK:
				messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
				break;
		}
	}
}

void InstallWindow::startInstalling()
{
	if(isCreated())
	{
		// Only a confirmed install sets this flag and starts the thread; resumeThread
		// must stay the thread's only other starter, and executeThread() checks this
		// flag first, so the install entry never runs unless the user confirmed.
		startRequested = true;
		resumeThread();
	}
	else if(!startEscape)
	{
		// Thread creation failed (~CThread logged it): this window can never
		// install, so run the normal close tail instead of leaving the owning
		// flow waiting on a dead window.
		startEscape = true;
		log_printf("InstallWindow: startInstalling on an uncreated window; closing it");
		OnCloseWindow(NULL, 0);
	}
}

void InstallWindow::OnInstallProcessCancel(GuiElement *element, int val)
{
	canceled = true;
	folderList->UnSelectAll();
	OnCloseWindow(this, 0);
}

void InstallWindow::OnCloseWindow(GuiElement * element, int val)
{
	messageBox->setEffect(EFFECT_FADE, -10, 255);
	messageBox->setState(GuiElement::STATE_DISABLED);
	messageBox->effectFinished.connect(this, &InstallWindow::OnWindowClosed);
}

void InstallWindow::OnWindowClosed(GuiElement *element)
{
	// The window is closing: stop applying worker-queued box wiring.
	boxClosing = true;
	messageBox->effectsTick.disconnect(this);
	messageBox->effectFinished.disconnect(this);
	installWindowClosed(this);
	
	AsyncDeleter::pushForDelete(this);
}

void InstallWindow::OnOpenEffectFinish(GuiElement *element)
{
	element->effectFinished.disconnect(this);
	element->clearState(GuiElement::STATE_DISABLED);
}
