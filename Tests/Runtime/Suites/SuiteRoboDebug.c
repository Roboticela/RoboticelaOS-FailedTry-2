/*
 * RoboDebug suite (Goal 1.5).
 *
 * Uses a custom panic handler so the suite never aborts the runner.
 * Default abort is validated via --child-panic / --child-assert from Build.ps1.
 */

#include "RoboBase/RoboDebug.h"
#include "RoboTest.h"

#include <string.h>

static int gPanicCount;
static char gPanicMsg[512];
static char gLogs[8][256];
static int gLogCount;

static void PanicHandler(const char *file, int line, const char *message,
                          void *ud)
{
    size_t i;
    (void)file;
    (void)line;
    (void)ud;
    gPanicCount++;
    gPanicMsg[0] = '\0';
    if (!message) {
        return;
    }
    for (i = 0; i + 1 < sizeof(gPanicMsg) && message[i]; i++) {
        gPanicMsg[i] = message[i];
    }
    gPanicMsg[i] = '\0';
}

static void LogWriter(LogLevel level, const char *line, void *ud)
{
    size_t i;
    (void)level;
    (void)ud;
    if (gLogCount >= 8 || !line) {
        return;
    }
    for (i = 0; i + 1 < sizeof(gLogs[0]) && line[i]; i++) {
        gLogs[gLogCount][i] = line[i];
    }
    gLogs[gLogCount][i] = '\0';
    gLogCount++;
}

static int LogHas(const char *needle)
{
    int i;
    for (i = 0; i < gLogCount; i++) {
        if (strstr(gLogs[i], needle)) {
            return 1;
        }
    }
    return 0;
}

static void InstallHooks(void)
{
    gPanicCount = 0;
    gLogCount = 0;
    gPanicMsg[0] = '\0';
    LogSetWriter(LogWriter, NULL);
    SetPanicHandler(PanicHandler, NULL);
}

static void ClearHooks(void)
{
    SetPanicHandler(NULL, NULL);
    LogSetWriter(NULL, NULL);
}

static void TestAssertPassFail(void)
{
    InstallHooks();
    RoboAssert(1 == 1);
    RoboCheckEqInt(gPanicCount, 0);

    RoboAssert(1 == 0);
    RoboCheckEqInt(gPanicCount, 1);
    RoboCheck(strstr(gPanicMsg, "assertion failed") != NULL);
    RoboCheck(LogHas("ASSERT failed") || LogHas("PANIC"));
    ClearHooks();
}

static void TestDebugAssertConfig(void)
{
    volatile int side = 0;
    InstallHooks();

#if RoboDebug
    RoboCheckEqInt(RoboDebugEnabled(), 1);
    RoboDebugAssert((side = 1, 1));
    RoboCheckEqInt(side, 1);
    RoboCheckEqInt(gPanicCount, 0);
    RoboDebugAssert(0);
    RoboCheckEqInt(gPanicCount, 1);
#else
    RoboCheckEqInt(RoboDebugEnabled(), 0);
    RoboDebugAssert((side = 1, 0));
    RoboCheckEqInt(side, 0);
    RoboCheckEqInt(gPanicCount, 0);
    RoboAssert(0);
    RoboCheckEqInt(gPanicCount, 1);
#endif
    ClearHooks();
}

static void TestPanicAndUnreachable(void)
{
    InstallHooks();
    Panic("hello %d", 7);
    RoboCheckEqInt(gPanicCount, 1);
    RoboCheck(strstr(gPanicMsg, "hello 7") != NULL);

    gPanicCount = 0;
    RoboUnreachable();
    RoboCheckEqInt(gPanicCount, 1);
    ClearHooks();
}

void TestRegisterDebug(void)
{
    TestRegister("debug.assert", TestAssertPassFail);
    TestRegister("debug.config", TestDebugAssertConfig);
    TestRegister("debug.panic_unreachable", TestPanicAndUnreachable);
}
