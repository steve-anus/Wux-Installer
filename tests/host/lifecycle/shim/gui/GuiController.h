#pragma once
class GuiController
{
public:
    explicit GuiController(int channel) {}
    virtual ~GuiController() = default;
    virtual bool update(int width, int height) = 0;
};
