#pragma once
class CVideo;
class GuiController;
class MainWindow
{
public:
    MainWindow(int w, int h);
    virtual ~MainWindow();
    bool isFlowIdle() const;
    bool abortFlowForExit();
    void logFlowGate(const char *why) const;
    void drawDrc(CVideo *video);
    void drawTv(CVideo *video);
    void update(GuiController *controller);
    void updateFlow();
    void updateEffects();
    void lockGUI();
    void unlockGUI();
};
