#pragma once
#include <gx2/state.h>
class CVideo;
class GuiImage
{
public:
    GuiImage(int w, int h, const GX2Color &color, int type = 0);
    void setAlpha(float alpha);
    void draw(CVideo *video);
};
