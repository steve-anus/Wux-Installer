#pragma once
#include "GuiController.h"
class WPadController : public GuiController
{
public:
    explicit WPadController(int channel) : GuiController(channel) {}
    bool update(int width, int height) override;
};
