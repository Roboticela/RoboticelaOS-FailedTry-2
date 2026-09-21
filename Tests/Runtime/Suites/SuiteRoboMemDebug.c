/*
 * RoboMem debug allocator suite (Goal 2.4).
 */

#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct faultCapture {
    int count;
    char kind[64];
    char message[256];
} FaultCapture;

static void FaultCaptureHandler(const char *kind, const char *message, void *userdata)
{
    FaultCapture *c = (FaultCapture *)userdata;
    size_t n;
    c->count++;
    if (kind) {
        n = strlen(kind);
        if (n >= sizeof(c->kind)) {
            n = sizeof(c->kind) - 1;
        }
        memcpy(c->kind, kind, n);
        c->kind[n] = '\0';
    }
    if (message) {
        n = strlen(message);
        if (n >= sizeof(c->message)) {
            n = sizeof(c->message) - 1;
        }
        memcpy(c->message, message, n);
        c->message[n] = '\0';
    }
}

static void DebugTestBegin(FaultCapture *cap)
{
    memset(cap, 0, sizeof(*cap));
    MemDebugEnable();
    MemDebugResetStats();
    MemDebugSetFaultHandler(FaultCaptureHandler, cap);
}

static void DebugTestEnd(void)
{
    MemDebugSetFaultHandler(NULL, NULL);
    MemDebugEnable();
}

static void TestMemdbgNormalAllocFree(void)
{
    FaultCapture cap;
    void *p;

    DebugTestBegin(&cap);
#if RoboMemDebug
    RoboCheck(MemDebugIsEnabled());
    p = Malloc(48);
    RoboCheck(p != NULL);
    RoboCheckEqInt(MemDebugLiveCount(), 1);
    RoboCheckEqInt(MemDebugCheck(p), 0);
    RoboFree(p);
    RoboCheckEqInt(MemDebugLiveCount(), 0);
    RoboCheckEqInt(cap.count, 0);
#else
    RoboCheck(!MemDebugIsEnabled());
    p = Malloc(48);
    RoboCheck(p != NULL);
    RoboFree(p);
#endif
    DebugTestEnd();
}

static void TestMemdbgLeakReport(void)
{
    FaultCapture cap;
    void *a;
    void *b;
    void *c;

    DebugTestBegin(&cap);
    a = Malloc(16);
    b = Malloc(32);
    c = Malloc(64);
    RoboCheck(a && b && c);
#if RoboMemDebug
    RoboCheckEqInt(MemDebugLiveCount(), 3);
#endif
    /* Intentionally leave a and b live for report; free c. */
    RoboFree(c);
    MemDebugReport();
#if RoboMemDebug
    RoboCheckEqInt(MemDebugLiveCount(), 2);
#endif
    RoboFree(a);
    RoboFree(b);
    DebugTestEnd();
}

static void TestMemdbgDoubleFree(void)
{
    FaultCapture cap;
    void *p;

    DebugTestBegin(&cap);
    p = Malloc(24);
    RoboCheck(p != NULL);
    RoboFree(p);
#if RoboMemDebug
    RoboFree(p);
    RoboCheckEqInt(cap.count, 1);
    RoboCheckStrEq(cap.kind, "double-free");
#else
    (void)p;
    RoboCheck(1);
#endif
    DebugTestEnd();
}

static void TestMemdbgInvalidFree(void)
{
    FaultCapture cap;
    int stackLocal = 0;
    void *foreign;

    DebugTestBegin(&cap);
#if RoboMemDebug
    RoboFree(&stackLocal);
    RoboCheck(cap.count >= 1);
    RoboCheckStrEq(cap.kind, "invalid-free");

    memset(&cap, 0, sizeof(cap));
    foreign = malloc(8);
    RoboCheck(foreign != NULL);
    RoboFree(foreign); /* not a debug allocation */
    RoboCheckEqInt(cap.count, 1);
    RoboCheckStrEq(cap.kind, "invalid-free");
    free(foreign);
#else
    (void)stackLocal;
    (void)foreign;
    RoboCheck(1);
#endif
    DebugTestEnd();
}

static void TestMemdbgUnderflow(void)
{
    FaultCapture cap;
    void *p;

    DebugTestBegin(&cap);
    p = Malloc(32);
    RoboCheck(p != NULL);
#if RoboMemDebug
    MemDebugCorruptGuardForTest(p, 0);
    RoboCheckEqInt(MemDebugCheck(p), 1);
    RoboFree(p);
    RoboCheckEqInt(cap.count, 1);
    RoboCheckStrEq(cap.kind, "guard-corruption");
#else
    RoboFree(p);
#endif
    DebugTestEnd();
}

static void TestMemdbgOverflow(void)
{
    FaultCapture cap;
    void *p;

    DebugTestBegin(&cap);
    p = Malloc(32);
    RoboCheck(p != NULL);
#if RoboMemDebug
    MemDebugCorruptGuardForTest(p, 1);
    RoboCheckEqInt(MemDebugCheck(p), 1);
    RoboFree(p);
    RoboCheckEqInt(cap.count, 1);
    RoboCheckStrEq(cap.kind, "guard-corruption");
#else
    RoboFree(p);
#endif
    DebugTestEnd();
}

static void TestMemdbgStats(void)
{
    FaultCapture cap;
    MemStats s;
    void *a;
    void *b;

    DebugTestBegin(&cap);
    a = Malloc(10);
    b = Malloc(20);
    RoboCheck(a && b);
    MemDebugGetStats(&s);
#if RoboMemDebug
    RoboCheckEqInt(s.liveCount, 2);
    RoboCheckEqInt(s.liveBytes, 30);
    RoboCheckEqInt(s.totalAllocs, 2);
    RoboCheckEqInt(s.totalFrees, 0);
    RoboFree(a);
    MemDebugGetStats(&s);
    RoboCheckEqInt(s.liveCount, 1);
    RoboCheckEqInt(s.liveBytes, 20);
    RoboCheckEqInt(s.totalFrees, 1);
    RoboFree(b);
#else
    RoboFree(a);
    RoboFree(b);
    RoboCheckEqInt(s.liveCount, 0);
#endif
    DebugTestEnd();
}

static void TestMemdbgPeakStats(void)
{
    FaultCapture cap;
    MemStats s;
    void *a;
    void *b;
    void *c;

    DebugTestBegin(&cap);
    a = Malloc(100);
    b = Malloc(200);
    c = Malloc(50);
    RoboCheck(a && b && c);
    RoboFree(c);
    MemDebugGetStats(&s);
#if RoboMemDebug
    RoboCheckEqInt(s.peakLiveCount, 3);
    RoboCheckEqInt(s.peakLiveBytes, 350);
    RoboCheckEqInt(s.liveCount, 2);
    RoboCheckEqInt(s.liveBytes, 300);
#endif
    RoboFree(a);
    RoboFree(b);
    DebugTestEnd();
}

static void TestMemdbgEnableDisable(void)
{
    FaultCapture cap;
    void *tracked;
    void *rawish;
    MemStats s;

    DebugTestBegin(&cap);
#if RoboMemDebug
    tracked = Malloc(40);
    RoboCheck(tracked != NULL);
    RoboCheckEqInt(MemDebugLiveCount(), 1);

    MemDebugDisable();
    RoboCheck(!MemDebugIsEnabled());
    rawish = Malloc(40);
    RoboCheck(rawish != NULL);
    MemDebugGetStats(&s);
    /* Disabled path does not bump live tracking for new allocs. */
    RoboCheckEqInt(s.liveCount, 1);

    RoboFree(rawish); /* untracked while disabled → system free */
    MemDebugEnable();
    RoboFree(tracked);
    RoboCheckEqInt(MemDebugLiveCount(), 0);
    RoboCheckEqInt(cap.count, 0);
#else
    (void)tracked;
    (void)rawish;
    (void)s;
    MemDebugDisable();
    RoboCheck(!MemDebugIsEnabled());
    MemDebugEnable();
#endif
    DebugTestEnd();
}

void TestRegisterMemDebug(void)
{
    TestRegister("memdbg.normal_alloc_free", TestMemdbgNormalAllocFree);
    TestRegister("memdbg.leak_report", TestMemdbgLeakReport);
    TestRegister("memdbg.double_free", TestMemdbgDoubleFree);
    TestRegister("memdbg.invalid_free", TestMemdbgInvalidFree);
    TestRegister("memdbg.underflow", TestMemdbgUnderflow);
    TestRegister("memdbg.overflow", TestMemdbgOverflow);
    TestRegister("memdbg.stats", TestMemdbgStats);
    TestRegister("memdbg.peak_stats", TestMemdbgPeakStats);
    TestRegister("memdbg.enable_disable", TestMemdbgEnableDisable);
}
