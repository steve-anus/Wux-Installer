#include "model.h"
#include "gui/FreeTypeGX.h"
#include "gui/GuiImage.h"
#include "gui/GuiSound.h"
#include "gui/GuiText.h"
#include "gui/MessageBox.h"
#include "gui/VPadController.h"
#include "gui/WPadController.h"
#include "menu/ErrorViewer.h"
#include "menu/MainWindow.h"
#include "menu/WuxExtractThread.h"
#include "resources/Resources.h"
#include "sounds/SoundHandler.hpp"
#include "system/AsyncDeleter.h"
#include "system/CThread.h"
#include "system/exception_handler.h"
#include "utils/logger.h"
#include "video/CVideo.h"
#include <coreinit/foreground.h>
#include <coreinit/systeminfo.h>
#include <nn/erreula.h>
#include <cstdarg>
#include <cstdio>
#include <set>
#include <vector>

namespace {
std::set<MainWindow *> windows;
std::set<CVideo *> videos;
std::set<FreeTypeGX *> fonts;
std::set<const void *> boxes;
}

namespace host {
void cleanupFakes()
{
    // Application leaves these alive for process exit; reclaim after all checks.
    while(!windows.empty()) delete *windows.begin();
    while(!fonts.empty()) delete *fonts.begin();
    while(!videos.empty()) delete *videos.begin();
}

bool boxAlive(const void *box)
{
    return boxes.count(box) != 0;
}
}

MainWindow::MainWindow(int w, int h)
{
    windows.insert(this);
    host::record("new MainWindow");
}

MainWindow::~MainWindow()
{
    host::record("delete MainWindow");
    windows.erase(this);
}

bool MainWindow::isFlowIdle() const
{
    host::record("isFlowIdle");
    return host::model.flowIdle;
}

bool MainWindow::abortFlowForExit()
{
    host::record("abortFlowForExit");
    return true;
}

void MainWindow::logFlowGate(const char *why) const
{
    host::record(std::string("logFlowGate(") + why + ")");
}

void MainWindow::drawDrc(CVideo *video) { host::foregroundUse("MainWindow::drawDrc"); }
void MainWindow::drawTv(CVideo *video) { host::foregroundUse("MainWindow::drawTv"); }
void MainWindow::update(GuiController *controller) { host::foregroundUse("MainWindow::update"); }
void MainWindow::updateFlow() { host::foregroundUse("MainWindow::updateFlow"); }
void MainWindow::updateEffects() { host::foregroundUse("MainWindow::updateEffects"); }
void MainWindow::lockGUI() { host::foregroundUse("MainWindow::lockGUI"); }
void MainWindow::unlockGUI() { host::foregroundUse("MainWindow::unlockGUI"); }

CVideo::CVideo(s32 tvMode, s32 drcMode)
{
    videos.insert(this);
    host::foregroundUse("new CVideo");
}

CVideo::~CVideo()
{
    host::record("delete CVideo");
    videos.erase(this);
}

void CVideo::tvEnable(bool enable) { host::foregroundUse("CVideo::tvEnable"); }
void CVideo::drcEnable(bool enable) { host::foregroundUse("CVideo::drcEnable"); }
void CVideo::prepareTvRendering() { host::foregroundUse("CVideo::prepareTvRendering"); }
void CVideo::prepareDrcRendering() { host::foregroundUse("CVideo::prepareDrcRendering"); }
void CVideo::tvDrawDone() { host::foregroundUse("CVideo::tvDrawDone"); }
void CVideo::drcDrawDone() { host::foregroundUse("CVideo::drcDrawDone"); }

void CVideo::waitForVSync()
{
    host::foregroundUse("CVideo::waitForVSync");
    ++frameCount;
}

FreeTypeGX::FreeTypeGX(const std::uint8_t *buffer, long size, bool lastFace)
{
    fonts.insert(this);
    host::record("new font");
}

FreeTypeGX::~FreeTypeGX()
{
    host::record("delete font");
    fonts.erase(this);
}

void GuiText::setPresetFont(FreeTypeGX *font) { host::record("GuiText::setPresetFont"); }
GuiImage::GuiImage(int w, int h, const GX2Color &color, int type) {}
void GuiImage::setAlpha(float alpha) {}
void GuiImage::draw(CVideo *video) { host::foregroundUse("GuiImage::draw"); }
GuiSound::GuiSound(const u8 *sound, int length) {}
void GuiSound::SetLoop(bool loop) {}
void GuiSound::Play() {}
void GuiSound::SetVolume(int volume) {}
bool VPadController::update(int width, int height) { return true; }
bool WPadController::update(int width, int height) { return true; }

MessageBox::MessageBox()
{
    boxes.insert(this);
    host::record("new box");
}

MessageBox::~MessageBox()
{
    host::record("delete box");
    boxes.erase(this);
}

WuxExtractThread::WuxExtractThread() { host::record("new thread"); }

WuxExtractThread::~WuxExtractThread()
{
    // The real CThread destructor joins before releasing its storage.
    shutdownThread();
    host::record("delete thread");
}

bool WuxExtractThread::isThreadTerminated() const { return host::model.threadTerminated; }
void WuxExtractThread::shutdownThread() { host::record("join"); }

CThread::CThread(int attributes, int priority, int stackSize,
                 Callback callback, void *callbackArg)
    : callback(callback), callbackArg(callbackArg)
{
}

CThread::~CThread() { shutdownThread(); }
void CThread::resumeThread() { worker = std::thread([this] { executeThread(); }); }
void CThread::shutdownThread() { if(worker.joinable()) worker.join(); }
void CThread::executeThread() { if(callback) callback(this, callbackArg); }

void AsyncDeleter::triggerDeleteProcess() { host::record("triggerDeleteProcess"); }
bool AsyncDeleter::deleteQueueEmpty()
{
    host::record("deleteQueueEmpty");
    return host::model.deleteQueueEmpty;
}
void AsyncDeleter::destroyInstance() { host::record("AsyncDeleter::destroyInstance"); }
bool ErrorViewer::isInitialized() { return host::model.errorViewerInitialized; }
bool Resources::LoadFiles(const char *path) { return true; }
const u8 *Resources::GetFile(const char *name) { return nullptr; }
u32 Resources::GetFileSize(const char *name) { return 0; }
void Resources::Clear() {}
void SoundHandler::DestroyInstance() {}
void setup_os_exceptions() {}
void OSSavesDone_ReadyToRelease() {}

BOOL OSEnableHomeButtonMenu(BOOL enable)
{
    host::record(enable ? "menu on" : "menu off");
    return TRUE;
}

void nn::erreula::AppearHomeNixSign(const HomeNixSignArg &arg)
{
    host::record("AppearHomeNixSign");
    ++host::model.homeNixSigns;
}

void GX2SetDepthOnlyControl(GX2Boolean test, GX2Boolean write, GX2CompareFunction compare)
{
    host::foregroundUse("GX2SetDepthOnlyControl");
}

void log_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if(length >= 0)
    {
        std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
        std::vsnprintf(buffer.data(), buffer.size(), format, args);
        std::string line(buffer.data());
        while(!line.empty() && line.back() == '\n') line.pop_back();
        host::record("log: " + line);
    }
    va_end(args);
}
