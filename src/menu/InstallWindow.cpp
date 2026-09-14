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

#define MCP_COMMAND_INSTALL_ASYNC   0x81
#define MAX_INSTALL_PATH_LENGTH     0x27F

static int installCompleted = 0;
static u32 installError = 0;

extern "C" MCPError MCP_GetLastRawError(void);

static void* IosInstallCallback(IOSError errorCode, void * priv_data)
{
	installError = errorCode;
	installCompleted = 1;
	return 0;
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
	char path[MAX_INSTALL_PATH_LENGTH];
	if (fsPath.compare(0, 3, "fs:") == 0)
		snprintf(path, sizeof(path), "sd:%s", fsPath.c_str() + 3);
	else
		snprintf(path, sizeof(path), "%s", fsPath.c_str());
	
	FSClient client;
	if (FSAddClient(&client, FS_ERROR_FLAG_NONE) != FS_STATUS_OK)
		return false;
	
	FSCmdBlock block;
	FSInitCmdBlock(&block);
	FSStatus st = FSRemove(&client, &block, path, FS_ERROR_FLAG_NONE);
	
	if (FSDelClient(&client, FS_ERROR_FLAG_NONE) != FS_STATUS_OK)
		log_printf("InstallWindow: FSDelClient failed after FSRemove");
	
	// FS_STATUS_NOT_FOUND = already gone, which is the desired end state.
	return st == FS_STATUS_OK || st == FS_STATUS_NOT_FOUND;
}

// The delete-stop boundary for an install folder path (shared by the
// success tail and the WUX skip).
static const char *stopAtFor(const std::string &path)
{
	if (path.find(SD_INSTALL_PATH) == 0)
		return SD_INSTALL_PATH;
	if (path.find(SD_WUDUMP_PATH) == 0)
		return SD_WUDUMP_PATH;
	return NULL;
}

InstallWindow::InstallWindow(CFolderList * list, bool deleteAfterInstall,
                             bool skipConfirm, bool askDelete,
                             const std::vector<std::string> &cleanupFiles,
                             bool wuxFlow)
	: GuiFrame(0, 0)
	, CThread(CThread::eAttributeAffCore0 | CThread::eAttributePinnedAff)
	, folderList(list)
	, deleteAfterInstall(deleteAfterInstall)
	, askDelete(askDelete)
	, deleteWuxFiles(false)
	, wuxFlow(wuxFlow)
	, installedCount(0)
	, skippedCount(0)
	, lastWasSkip(false)
	, wuxDeleteFailed(false)
	, cleanupFiles(cleanupFiles)
{
	mainWindow = Application::instance()->getMainWindow();
	
	folderCount = folderList->GetSelectedCount();
	
	if(skipConfirm && folderCount > 0)
	{
		// WUX flow: the folders were selected by code, so skip the
		// "are you sure" prompt and go straight to the destination question.
		messageBox = new MessageBox(MessageBox::BT_DEST, MessageBox::IT_ICONQUESTION, false);
		messageBox->setTitle("Where do you want to install?");
		messageBox->setMessage1(fmt("%d application(s)", folderCount));
		messageBox->messageYesClicked.connect(this, &InstallWindow::OnDestinationChoice);
		messageBox->messageNoClicked.connect(this, &InstallWindow::OnDestinationChoice);
	}
	else if(folderCount > 0)
	{
		std::string message = fmt("%d application(s)", folderCount);
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
		messageBox->setMessage1("Return to folder browser.");
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
	
	if(askDelete)
	{
		// WUX flow: ask whether the .wux image, game.key and the extracted
		// .app folders are deleted from the SD card after a successful
		// install. The deletion itself runs after the last title has
		// installed (see InstallProcess).
		std::string fileName = cleanupFiles.empty() ? std::string()
			: cleanupFiles[0].substr(cleanupFiles[0].find_last_of('/') + 1);
		messageBox->reload("Delete files after install?",
			fmt("Delete %s, game.key and extracted files from the SD card after a successful install?",
			    fileName.c_str()),
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
	
	// Yes: delete the install folders after each title (deleteAfterInstall)
	// and the .wux image + game.key after the last title (deleteWuxFiles).
	// No: keep everything on the SD card.
	if(choice == MessageBox::MR_YES)
	{
		deleteAfterInstall = true;
		deleteWuxFiles = true;
	}
	
	startInstalling();
}

void InstallWindow::executeThread()
{
	Application::instance()->exitDisable();
	OSEnableHomeButtonMenu(FALSE);
	
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
					messageBox->setMessage2(fmt("Starting next installation in %d second(s)", time));
				}
			}
			
			messageBox->messageCancelClicked.disconnect(this);
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
		if(installedCount > 0)
		{
			// The .wux image and game.key (in /wudump) are removed after the
			// last processed title, but only if something really installed
			// (retry-friendly otherwise). common.key is never in
			// cleanupFiles. A failed POSIX unlink (the same layer the fork's
			// RemoveDirectory uses for /install) falls back to the coreinit
			// FS client; both outcomes are logged for the hardware debug.
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
			if(skippedCount > 0)
			{
				if(!note.empty())
					note += " ";
				note += strfmt("Skipped %d non-installable title(s) (already on the console).",
				               skippedCount);
			}

			messageBox->reload("Successfully installed", lastGoodName, note,
			                   MessageBox::BT_OK, MessageBox::IT_ICONTRUE);
			messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
		}
		else
		{
			// Every selected folder was a non-installable system title.
			messageBox->reload("Install failed", "",
			                   "No installable titles (all were system titles already on the console).",
			                   MessageBox::BT_OK, MessageBox::IT_ICONERROR);
			messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
		}
	}
	
	if(APD_enabled)
		enableAutoPowerDown();
	
	OSEnableHomeButtonMenu(TRUE);
	Application::instance()->exitEnable();
}

void InstallWindow::InstallProcess(int pos, int total)
{
	int index = folderList->GetFirstSelected();
	
	std::string title = fmt("Installing... (%d/%d)", pos, total);
	std::string gameName = folderList->GetName(index);
	
	lastWasSkip = false;
	
	messageBox->reload(title, gameName, "", MessageBox::BT_NOBUTTON, MessageBox::IT_ICONINFORMATION, true, "0.0 %");
	
	/////////////////////////////
	// install process
	/////////////////////////////
	
	int result = 0;
	installCompleted = 0;
	installError = 0;
	
	//!---------------------------------------------------
	//! This part of code originates from Crediars MCP patcher assembly code
	//! it is just translated to C
	//!---------------------------------------------------
	unsigned int mcpHandle = MCP_Open();
	if(mcpHandle == 0)
	{
		messageBox->reload("Install failed", gameName, "Failed to open MCP.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
		
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
				messageBox->reload("Install failed", gameName, "Could not allocate memory.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -2;
				break;
			}
			
			std::string installFolder = folderList->GetPath(index);
			installFolder.erase(0, 19);
			installFolder.insert(0, "/vol/app_sd/");
            
            snprintf(installPath, sizeof(installPath), "%s", installFolder.c_str());
			
			int res = MCP_InstallGetInfo(mcpHandle, installPath, (MCPInstallInfo*)mcpInstallInfo);
			if(res != 0)
			{
				//__os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallGetInfo 0x%08X", MCP_GetLastRawError());
				messageBox->reload(installFolder, gameName, "Confirm complete WUP files are in the folder.", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -3;
				break;
			}
			
			u32 titleIdHigh = mcpInstallInfo[0];
			u32 titleIdLow = mcpInstallInfo[1];
			bool spoofFiles = false;
			// The 0x was missing here: 00050010 is OCTAL (= 0x5008), so the
			// Version.bin spoof never matched. Restores the fork's intent.
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
					messageBox->reload("Install failed", gameName, fmt("MCP_InstallSetTargetDevice 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					//if (installToUsb)
					//	__os_snprintf(errorText2, sizeof(errorText2), "Possible USB HDD disconnected or failure");
					result = -5;
					break;
				}
				res = MCP_InstallSetTargetUsb(mcpHandle, (MCPInstallTarget)(effTarget));
				if(res != 0)
				{
					messageBox->reload("Install failed", gameName, fmt("MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
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
				
				res = IOS_IoctlvAsync(mcpHandle, MCP_COMMAND_INSTALL_ASYNC, 1, 0, mcpPathInfoVector, (IOSAsyncCallbackFn)IosInstallCallback, mcpInstallInfo);
				if(res != 0)
				{
					messageBox->reload("Install failed", gameName, fmt("MCP_InstallTitleAsync 0x%08X", MCP_GetLastRawError()), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
					result = -7;
					break;
				}
				
				while(!installCompleted)
				{
					memset(mcpInstallInfo, 0, 0x24);
					
					MCP_InstallGetProgress(mcpHandle, (MCPInstallProgress*)mcpInstallInfo);
					
					if(mcpInstallInfo[0] == 1)
					{
						u64 totalSize = ((u64)mcpInstallInfo[3] << 32ULL) | mcpInstallInfo[4];
						u64 installedSize = ((u64)mcpInstallInfo[5] << 32ULL) | mcpInstallInfo[6];
						int percent = (totalSize != 0) ? ((installedSize * 100.0f) / totalSize) : 0;
						
						std::string message = fmt("%0.1f / %0.1f MB (%i", installedSize / (1024.0f * 1024.0f), totalSize / (1024.0f * 1024.0f), percent);
						message += "%)";
						
						messageBox->setProgress(percent);
						messageBox->setProgressBarInfo(message);
					}
					
					usleep(50000);
				}
				
				if(installError != 0)
				{
					if ((installError == 0xFFFCFFE9) && (effTarget == USB))
					{
						messageBox->reload("Install failed", gameName, fmt("0x%08X access failed (no USB storage attached?)", installError), MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						result = -8;
					}
					else
					{
						//__os_snprintf(errorText1, sizeof(errorText1), "Error: install error code 0x%08X", installError);
						if (installError == 0xFFFBF446 || installError == 0xFFFBF43F)
							messageBox->reload("Install failed", gameName, "Possible missing or bad title.tik file", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFBF441)
							messageBox->reload("Install failed", gameName, "Possible incorrect console for DLC title.tik file", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFCFFE4)
							messageBox->reload("Install failed", gameName, "Possible not enough memory on target device", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if (installError == 0xFFFFF825)
							messageBox->reload("Install failed", gameName, "Possible bad SD card.  Reformat (32k blocks) or replace", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						else if ((installError & 0xFFFF0000) == 0xFFFB0000)
							messageBox->reload("Install failed", gameName, "Verify WUP files are correct & complete. DLC/E-shop require Sig Patch", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
						
						result = -9;
					}
				}
			}
			else if(wuxFlow)
			{
				// Non-installable system title (e.g. the disc's rear.rpx
				// dummy 00050010-10060000 - the console already ships it on
				// NAND). In the WUX flow this is benign: it is our own
				// extraction output, so clean it up like an installed folder
				// and let the chain continue.
				log_printf("InstallWindow: skipping non-installable title %08X-%08X (%s)",
				           titleIdHigh, titleIdLow, gameName.c_str());
				if(deleteAfterInstall)
				{
					std::string path = folderList->GetPath(index);
					RemoveDirectoryAndEmptyParents(path.c_str(), stopAtFor(path));
					struct stat st;
					if (stat(path.c_str(), &st) == 0)
						log_printf("InstallWindow: skipped folder still present: %s",
						           path.c_str());
				}
				folderList->UnSelect(index);
				++skippedCount;
				lastWasSkip = true;
				result = kResultSkip;
			}
			else
			{
				messageBox->reload("Install failed", gameName, "Not a game, game update, DLC, demo or version title", MessageBox::BT_OK, MessageBox::IT_ICONERROR);
				result = -4;
			}
		}
		while(0);
		
		MCP_Close(mcpHandle);
		if(mcpPathInfoVector)
			OSFreeToSystem(mcpPathInfoVector);
		if(mcpInstallPath)
			OSFreeToSystem(mcpInstallPath);
		if(mcpInstallInfo)
			OSFreeToSystem(mcpInstallInfo);
	}
	/////////////////////////////
	
	if(result >= 0)
	{
		++installedCount;
		lastGoodName = gameName;

		if(deleteAfterInstall)
		{
			std::string path = folderList->GetPath(index);
			RemoveDirectoryAndEmptyParents(path.c_str(), stopAtFor(path));
		}

		if(pos < total)
		{
			messageBox->reload("Successfully installed", gameName, "Starting next installation in 6 second(s)", MessageBox::BT_CANCEL, MessageBox::IT_ICONTRUE);
			messageBox->messageCancelClicked.connect(this, &InstallWindow::OnInstallProcessCancel);
		}
		// The final box (and the .wux/game.key cleanup) is shown by
		// executeThread once the whole loop is done, so it also covers a
		// last folder that was skipped rather than installed.
		
		folderList->UnSelect(index);
	}
	else if(result == kResultSkip)
	{
		// Benign WUX-flow skip: handled where the title type was checked
		// (folder cleaned, selection advanced, counters updated). No box.
	}
	else
	{
		messageBox->messageOkClicked.connect(this, &InstallWindow::OnCloseWindow);
		
		canceled = true;
		folderList->UnSelectAll();
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
	messageBox->effectFinished.disconnect(this);
	installWindowClosed(this);
	
	AsyncDeleter::pushForDelete(this);
}

void InstallWindow::OnOpenEffectFinish(GuiElement *element)
{
	element->effectFinished.disconnect(this);
	element->clearState(GuiElement::STATE_DISABLED);
}

void InstallWindow::OnCloseEffectFinish(GuiElement *element)
{
	remove(element);
	AsyncDeleter::pushForDelete(element);
}
