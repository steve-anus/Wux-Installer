#include <stdio.h>
#include <malloc.h>
#include <vpad/input.h>
#include "utils/logger.h"
#include "ErrorViewer.h"

bool ErrorViewer::initialized = false;

ErrorViewer::ErrorViewer() 
    : client(NULL)
    , ready(false)
{
    client = (FSClient *)malloc(sizeof(FSClient));
    if (!client) {
        log_printf("ErrorViewer: FSClient allocation failed");
        return;
    }
    if (FSAddClient(client, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
    {
        log_printf("ErrorViewer: FSAddClient failed");
        free(client);
        client = NULL;
        return;
    }
    
    createArg.workMemory = malloc(nn::erreula::GetWorkMemorySize());
    if (!createArg.workMemory) {
        log_printf("ErrorViewer: work memory allocation failed");
        FSDelClient(client, FS_ERROR_FLAG_ALL);
        free(client);
        client = NULL;
        return;
    }

    createArg.fsClient = client;
    if (!nn::erreula::Create(createArg)) {
        log_printf("Failed to create error viewer");
        free(createArg.workMemory);
        createArg.workMemory = NULL;
        FSDelClient(client, FS_ERROR_FLAG_ALL);
        free(client);
        client = NULL;
        return;
    }

    ready = true;
    initialized = true;
}

ErrorViewer::~ErrorViewer() 
{
    if (!ready)
        return;

    nn::erreula::Destroy();
    free(createArg.workMemory);
    createArg.workMemory = NULL;
    FSDelClient(client, FS_ERROR_FLAG_ALL);
    free(client);
    client = NULL;
    initialized = false;
}

void ErrorViewer::calc()
{
    if (!initialized)
        return;

    // Zero-init: VPAD_READ_NO_SAMPLES leaves the struct untouched and the
    // erreula Calc below still consumes it.
    VPADStatus vpadStatus = {};
    VPADReadError vpadError = VPAD_READ_NO_SAMPLES;
    VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadError);
    if (vpadError != VPAD_READ_SUCCESS && vpadError != VPAD_READ_NO_SAMPLES)
        return;
    VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpadStatus.tpNormal, &vpadStatus.tpNormal);

    nn::erreula::ControllerInfo controllerInfo;
    controllerInfo.vpad = &vpadStatus;
    controllerInfo.kpad[0] = nullptr;
    controllerInfo.kpad[1] = nullptr;
    controllerInfo.kpad[2] = nullptr;
    controllerInfo.kpad[3] = nullptr;
    
    nn::erreula::Calc(controllerInfo);
}

void ErrorViewer::drawTV()
{
    if (!initialized)
        return;

    nn::erreula::DrawTV();
}

void ErrorViewer::drawDRC()
{
    if (!initialized)
        return;

    nn::erreula::DrawDRC();
}
