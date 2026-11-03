#pragma once
#include <proc_ui/procui.h>
#include <cstddef>
#include <string>
#include <vector>

namespace host {
struct Step
{
    ProcUIStatus status;
    bool flowIdle = true;
};
struct Model
{
    std::vector<Step> script;
    std::size_t next = 0;
    ProcUIStatus last = PROCUI_STATUS_IN_FOREGROUND;
    bool releaseWindow = false;
    bool memoryLive = false;
    bool flowIdle = true;
    bool deleteQueueEmpty = true;
    bool errorViewerInitialized = false;
    bool threadTerminated = false;
    unsigned homeNixSigns = 0;
    unsigned shutdowns = 0;
    ProcUICallback homeDenied = nullptr;
    void *homeContext = nullptr;
    std::vector<std::string> events;
    std::vector<std::string> violations;
};
extern Model model;
void reset();
void record(const std::string &event);
void violation(int number, const std::string &message);
void foregroundUse(const char *event);
void finish();
void cleanupFakes();
bool boxAlive(const void *box);
}
