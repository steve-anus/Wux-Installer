#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "common/common.h"
#include "logger.h"

#ifdef DEBUG_LOGGER

// SD-card log file (PLAN 5.2): hardware rounds are debuggable without a
// UDP listener. Opened once on the main thread at log_init (before any
// worker exists); stdio's internal locking covers rare concurrent writes.
static FILE *logFile = NULL;

static void LogFileHandler(const char *msg)
{
    if (logFile == NULL)
        return;
    fprintf(logFile, "%s\n", msg);
    // Keep the trail even if the app dies later.
    fflush(logFile);
}

void log_init()
{
    WHBLogUdpInit();
    logFile = fopen("fs:/vol/external01/wux_installer.log", "a");
    if (logFile != NULL)
        WHBAddLogHandler(LogFileHandler);
}

void log_deinit(void)
{
    WHBLogUdpDeinit();
    if (logFile != NULL)
    {
        fclose(logFile);
        logFile = NULL;
    }
}

void log_print(const char *str)
{
	WHBLogPrint(str);
}

void log_printf(const char *format, ...)
{
    // Format first, then print: WHBLogPrintf would lose the variadic args.
    char buf[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    WHBLogPrint(buf);
}
#endif
