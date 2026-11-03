#pragma once
#include <gx2/state.h>
class CVideo
{
public:
    CVideo(s32 tvMode = -1, s32 drcMode = -1);
    virtual ~CVideo();
    u32 getTvWidth() const { return 1280; }
    u32 getTvHeight() const { return 720; }
    u32 getFrameCount() const { return frameCount; }
    void tvEnable(bool enable);
    void drcEnable(bool enable);
    void prepareTvRendering();
    void prepareDrcRendering();
    void tvDrawDone();
    void drcDrawDone();
    void waitForVSync();
private:
    u32 frameCount = 0;
};
