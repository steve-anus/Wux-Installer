#pragma once
#include "GuiController.h"
class VPadController : public GuiController
{
public:
    explicit VPadController(int channel) : GuiController(channel) {}
    bool update(int width, int height) override;
};
