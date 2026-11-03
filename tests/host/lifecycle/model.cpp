// wut include/proc_ui/procui.h:102-115 documents the release handshake:
// the next ProcessMessages switches context; foreground use waits for foreground.
// Lines 18-28 describe background restrictions and EXITING shutdown.
#include "model.h"
#include "system/memory.h"

namespace host {
Model model;

void reset()
{
    model = Model{};
}

void record(const std::string &event)
{
    model.events.push_back(event);
}

void violation(int number, const std::string &message)
{
    model.violations.push_back("violation " + std::to_string(number) + ": " + message);
}

void foregroundUse(const char *event)
{
    record(event);
    if(model.releaseWindow || model.last == PROCUI_STATUS_IN_BACKGROUND)
        violation(3, std::string(event) + " used foreground resources while released/backgrounded");
}

void finish()
{
    if(model.shutdowns != 1)
        violation(5, "ProcUIShutdown count is " + std::to_string(model.shutdowns) + ", expected 1");
}
}

void ProcUIInit(ProcUISaveCallback callback)
{
    host::record("ProcUIInit");
}

void ProcUIRegisterCallback(ProcUICallbackType type, ProcUICallback callback,
                            void *param, std::uint32_t priority)
{
    host::record("ProcUIRegisterCallback");
    if(type == PROCUI_CALLBACK_HOME_BUTTON_DENIED)
    {
        host::model.homeDenied = callback;
        host::model.homeContext = param;
    }
}

ProcUIStatus ProcUIProcessMessages(BOOL block)
{
    auto &model = host::model;
    if(model.next < model.script.size())
    {
        const auto &step = model.script[model.next++];
        model.last = step.status;
        model.flowIdle = step.flowIdle;
    }
    else
        model.last = PROCUI_STATUS_EXITING;

    if(model.last == PROCUI_STATUS_IN_FOREGROUND || model.last == PROCUI_STATUS_EXITING)
        model.releaseWindow = false;

    switch(model.last)
    {
        case PROCUI_STATUS_IN_FOREGROUND: host::record("status FG"); break;
        case PROCUI_STATUS_IN_BACKGROUND: host::record("status BG"); break;
        case PROCUI_STATUS_RELEASE_FOREGROUND: host::record("status RELEASE"); break;
        case PROCUI_STATUS_EXITING: host::record("status EXITING"); break;
    }
    return model.last;
}

void ProcUIDrawDoneRelease()
{
    host::record("ProcUIDrawDoneRelease");
    if(host::model.last != PROCUI_STATUS_RELEASE_FOREGROUND)
        host::violation(2, "ProcUIDrawDoneRelease without RELEASE_FOREGROUND");
    host::model.releaseWindow = true;
}

void ProcUIShutdown()
{
    host::record("ProcUIShutdown");
    ++host::model.shutdowns;
    if(host::model.releaseWindow)
        host::violation(1, "ProcUIShutdown in the release/switch window without EXITING");
}

void memoryInitialize()
{
    host::record("memoryInitialize");
    if(host::model.memoryLive)
        host::violation(4, "memoryInitialize twice without memoryRelease");
    host::model.memoryLive = true;
}

void memoryRelease()
{
    host::record("memoryRelease");
    if(!host::model.memoryLive)
        host::violation(4, "memoryRelease without memoryInitialize");
    host::model.memoryLive = false;
}
