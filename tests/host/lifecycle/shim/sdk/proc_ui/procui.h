#pragma once
#include <wut.h>
using ProcUISaveCallback = void (*)();
using ProcUICallback = std::uint32_t (*)(void *);
enum ProcUICallbackType
{
    PROCUI_CALLBACK_HOME_BUTTON_DENIED = 5
};
enum ProcUIStatus
{
    PROCUI_STATUS_IN_FOREGROUND,
    PROCUI_STATUS_IN_BACKGROUND,
    PROCUI_STATUS_RELEASE_FOREGROUND,
    PROCUI_STATUS_EXITING
};
void ProcUIInit(ProcUISaveCallback callback);
void ProcUIRegisterCallback(ProcUICallbackType type, ProcUICallback callback,
                            void *param, std::uint32_t priority);
ProcUIStatus ProcUIProcessMessages(BOOL block);
void ProcUIDrawDoneRelease();
void ProcUIShutdown();
