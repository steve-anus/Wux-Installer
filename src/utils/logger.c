#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "common/common.h"
#include "logger.h"

#ifdef DEBUG_LOGGER

// SD-card log file: hardware rounds are debuggable without a
// UDP listener. Opened once on the main thread at log_init (before any
// worker exists); stdio's internal locking covers rare concurrent writes.
// Appended to, never truncated: after a crash the normal flow is
// relaunch-and-read-this-file, so the crashed run's trail must survive the
// relaunch. Cross-run growth is bounded by the rotate in log_init.
static FILE *logFile = NULL;
// Gates every handler entry. Cleared first on deinit; the FILE itself is
// then deliberately kept open (see log_deinit) so a writer already past the
// check can never meet a freed FILE. A volatile flag cannot order a close -
// not closing is what makes the window safe.
static volatile bool logFileActive = false;
// Written-byte tally; the log is capped so a runaway loop cannot fill the card.
#define LOG_MAX_BYTES 524288
// Cross-run growth bound: several capped runs may accumulate before the
// file rotates to .log.old (one generation kept, next run starts fresh).
#define LOG_ROTATE_BYTES (4 * LOG_MAX_BYTES)
static long logBytes = 0;
static bool logCapNoteWritten = false;

static void LogFileHandler(const char *msg)
{
    if (!logFileActive || logFile == NULL)
        return;

    if (logBytes > LOG_MAX_BYTES)
    {
        if (!logCapNoteWritten)
        {
            logCapNoteWritten = true;
            int n = fprintf(logFile, "log capped at 512 KiB\n");
            if (n > 0)
                logBytes += n;
            fflush(logFile);
        }
        return;
    }

    int n = fprintf(logFile, "%s\n", msg);
    if (n > 0)
        logBytes += n;
    // Keep the trail even if the app dies later.
    fflush(logFile);
}

void log_init()
{
    static const char *logPath = "fs:/vol/external01/wux_installer.log";
    WHBLogUdpInit();
    logFile = fopen(logPath, "a");
    if (logFile != NULL)
    {
        // If many runs piled the file up past the rotate bound, move one
        // generation to .log.old and start appending fresh.
        if (fseek(logFile, 0, SEEK_END) == 0 && ftell(logFile) > (long)LOG_ROTATE_BYTES)
        {
            fclose(logFile);
            rename(logPath, "fs:/vol/external01/wux_installer.log.old");
            logFile = fopen(logPath, "a");
        }
        if (logFile != NULL)
        {
            logBytes = 0;
            logCapNoteWritten = false;
            logFileActive = true;
            WHBAddLogHandler(LogFileHandler);
        }
    }
}

void log_deinit(void)
{
    WHBLogUdpDeinit();
    logFileActive = false;
    // No fclose: a worker thread can still be inside LogFileHandler here
    // (nothing joins it first), and fclose frees the FILE it writes to.
    // Flushing is safe under newlib's stdio locking; the descriptor is
    // reclaimed at process exit, and the run's log already ends complete.
    if (logFile != NULL)
        fflush(logFile);
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
