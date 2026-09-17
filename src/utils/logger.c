#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "common/common.h"
#include "logger.h"
#include <coreinit/mutex.h>

#ifdef DEBUG_LOGGER

// SD-card log file: hardware rounds are debuggable without a UDP listener.
// One file only - the card root stays tidy. Appended across relaunches so a
// crashed run's tail survives, but once the byte cap is passed the same file
// restarts from empty with a marker line: the newest entries (the useful
// ones) always survive and the file stays small. Several threads write here,
// so every FILE access runs under logMutex, including the restart's
// close-and-reopen. The Wii U scheduler is co-operative, so this lock must
// block (waiters enter a waiting state); a spin here could starve the holder.
static const char *logPath = "fs:/vol/external01/wux_installer.log";
static FILE *logFile = NULL;
static OSMutex logMutex;
// Gates every handler entry. Cleared first on deinit; the FILE itself is
// then deliberately kept open (see log_deinit) so a writer already past the
// check can never meet a freed FILE. A volatile flag cannot order a close -
// not closing is what makes the window safe.
static volatile bool logFileActive = false;
// Bytes currently on the card for this file, counted from the file itself at
// init and kept live by each write; on overflow the file restarts, so a
// runaway loop can neither fill the card nor bury its newest lines.
#define LOG_MAX_BYTES 524288
static long logBytes = 0;

static void logLock(void)
{
    OSLockMutex(&logMutex);
}

static void logUnlock(void)
{
    OSUnlockMutex(&logMutex);
}

static void LogFileHandler(const char *msg)
{
    if (!logFileActive || logFile == NULL)
        return;

    logLock();
    if (!logFileActive || logFile == NULL)
    {
        logUnlock();
        return;
    }

    if (logBytes > LOG_MAX_BYTES)
    {
        // Restart the same file: newest lines win, still exactly one log.
        fclose(logFile);
        logFile = fopen(logPath, "w");
        if (logFile == NULL)
        {
            logFileActive = false;
            logUnlock();
            WHBLogPrint("SD log disabled: reopen failed");
            return;
        }
        logBytes = 0;
        int m = fprintf(logFile, "--- earlier entries dropped (log restarted at cap) ---\n");
        if (m > 0)
            logBytes += m;
        fflush(logFile);
    }

    int n = fprintf(logFile, "%s\n", msg);
    if (n > 0)
        logBytes += n;
    // Keep the trail even if the app dies later.
    fflush(logFile);
    logUnlock();
}

void log_init(void)
{
    OSInitMutex(&logMutex);
    WHBLogUdpInit();
    logLock();
    logFile = fopen(logPath, "a");
    if (logFile != NULL)
    {
        // Seed the tally with the file's real on-card size: relaunches
        // cannot pile the log up, because the first write that passes the
        // cap restarts the file (bounded at cap plus one line).
        long sz = 0;
        if (fseek(logFile, 0, SEEK_END) == 0)
        {
            long t = ftell(logFile);
            if (t > 0)
                sz = t;
        }
        logBytes = sz;
        logFileActive = true;
        WHBAddLogHandler(LogFileHandler);
    }
    logUnlock();
}

void log_deinit(void)
{
    WHBLogUdpDeinit();
    logFileActive = false;
    // No fclose: a worker thread can still be inside LogFileHandler here
    // (nothing joins it first), and fclose frees the FILE it writes to.
    // The lock below keeps the flush out of a restart's close-and-reopen
    // window; flushing is safe under newlib's stdio locking, and the
    // descriptor is reclaimed at process exit, so the run's log ends
    // complete.
    logLock();
    if (logFile != NULL)
        fflush(logFile);
    logUnlock();
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
