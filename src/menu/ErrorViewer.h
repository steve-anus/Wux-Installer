#pragma once

#include <nn/erreula.h>
#include <coreinit/filesystem.h>

class ErrorViewer
{
    public:
        ErrorViewer();

        ~ErrorViewer();

        void calc();

        static void drawTV();

        static void drawDRC();

        //! Mirrors the erreula singleton state for callers that must not
        //! touch it after teardown (the home-button-denied callback).
        static bool isInitialized() { return initialized; }

    private:
        FSClient *client;
        nn::erreula::CreateArg createArg;
        // True when THIS instance fully constructed itself. Teardown is owned
        // by the creating instance only, so a second instance whose Create()
        // failed can never destroy the first one's erreula state.
        bool ready;
        // nn::erreula is a process-wide singleton, so readiness is mirrored
        // here for the static draw entry points (single instance in practice).
        static bool initialized;
};
