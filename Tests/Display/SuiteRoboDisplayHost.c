/*
 * Host RoboDisplay backend suite (Goal 7.13).
 *
 * Real EnumDisplayMonitors path when Win32 host is available.
 * Skips cleanly under ROBO_WINDOW_SKIP_HOST or no interactive desktop.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendHost.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int HostDisplaySkip(char *buf, size_t cap)
{
    if (getenv("ROBO_WINDOW_SKIP_HOST") != NULL) {
        if (buf && cap) {
            snprintf(buf, cap, "ROBO_WINDOW_SKIP_HOST");
        }
        return 1;
    }
    if (!DisplayBackendHostAvailable()) {
        if (buf && cap) {
            snprintf(buf, cap, "host display unavailable");
        }
        return 1;
    }
    return 0;
}

static void TestHostDisplayAvailability(void)
{
    const DisplayBackendVtbl *vtbl = DisplayBackendHostVtbl();
#if defined(RoboWindowHasWin32) && RoboWindowHasWin32
    if (HostDisplaySkip(NULL, 0)) {
        RoboCheck(vtbl != NULL); /* vtbl exists even when skip env set */
        RoboCheckStrEq(vtbl->name, "win32");
        return;
    }
    RoboCheck(vtbl != NULL);
    RoboCheckStrEq(vtbl->name, "win32");
    RoboCheckEqInt(DisplayBackendHostAvailable(), 1);
#else
    RoboCheck(vtbl == NULL);
    RoboCheckEqInt(DisplayBackendHostAvailable(), 0);
    RoboCheckEqInt(DisplayBackendUseHost(), RoboNotSupported);
#endif
}

static int BeginHostDisplay(void)
{
    char reason[64];
    Result r;

    if (HostDisplaySkip(reason, sizeof(reason))) {
        printf("SKIP display.host.* (%s)\n", reason);
        return 0;
    }
    RoboCheckEqInt(DisplayInit(), RoboOk);
    r = DisplayBackendUseHost();
    if (r != RoboOk) {
        printf("SKIP display.host.* (use_host=%d)\n", (int)r);
        DisplayShutdown();
        return 0;
    }
    RoboCheckEqInt(DisplayRefresh(), RoboOk);
    return 1;
}

static void TestHostDisplayEnumerate(void)
{
    DisplayInfo infos[16];
    size_t count = 0u;
    size_t i;
    int sawPrimary = 0;
    DisplayBackendCaps caps;
    DisplayInfo primary;

    if (!BeginHostDisplay()) {
        return;
    }

    RoboCheckEqInt(DisplayBackendCapabilities(&caps), RoboOk);
    RoboCheck(caps.enumerate);
    RoboCheck(caps.primary);
    RoboCheck(caps.GetInfo);
    RoboCheck(caps.workArea);
    RoboCheck(caps.scaleDpi);
    RoboCheck(!caps.refreshRate); /* Win32 path does not fabricate Hz */

    count = DisplayCount();
    RoboCheck(count >= 1u);
    RoboCheck(count <= 16u);
    printf("host displays=%u\n", (unsigned)count);

    for (i = 0u; i < count; i++) {
        float scale = 0.0f;
        RoboCheckEqInt(DisplayAt(i, &infos[i]), RoboOk);
        RoboCheck(infos[i].id != RoboDisplayIdNone);
        RoboCheck(infos[i].resolutionWidthPx > 0);
        RoboCheck(infos[i].resolutionHeightPx > 0);
        RoboCheck(infos[i].bounds.width > 0);
        RoboCheck(infos[i].bounds.height > 0);
        RoboCheck(infos[i].workArea.width > 0);
        RoboCheck(infos[i].workArea.height > 0);
        RoboCheckEqInt(DisplayInfoEffectiveScale(&infos[i], &scale),
                          RoboOk);
        RoboCheck(DisplayScaleIsKnown(scale));
        RoboCheck(infos[i].dpiX > 0.0f);
        RoboCheck(infos[i].dpiY > 0.0f);
        RoboCheck(infos[i].refreshHz == 0.0f); /* unknown, not faked */
        if (infos[i].isPrimary) {
            sawPrimary = 1;
        }
        printf("  display id=%llu name=%s %dx%d scale=%g dpi=%g\n",
               (unsigned long long)infos[i].id,
               infos[i].name[0] ? infos[i].name : "(unnamed)",
               (int)infos[i].resolutionWidthPx,
               (int)infos[i].resolutionHeightPx, (double)scale,
               (double)infos[i].dpiX);
    }
    RoboCheck(sawPrimary);

    RoboCheckEqInt(DisplayPrimary(&primary), RoboOk);
    RoboCheck(primary.isPrimary);
    RoboCheckEqInt(DisplayGet(primary.id, &infos[0]), RoboOk);
    RoboCheckEqInt((int)infos[0].id, (int)primary.id);

    {
        DisplayMode modes[4];
        size_t modeCount = 0u;
        RoboCheckEqInt(
            DisplayGetModes(primary.id, modes, 4, &modeCount), RoboOk);
        RoboCheckEqInt((int)modeCount, 1);
        RoboCheckEqInt(modes[0].widthPx, primary.resolutionWidthPx);
        RoboCheckEqInt(modes[0].heightPx, primary.resolutionHeightPx);
    }

    DisplayShutdown();
}

static void TestHostDisplayReinit(void)
{
    if (!BeginHostDisplay()) {
        return;
    }
    DisplayShutdown();
    RoboCheckEqInt(DisplayIsInitialized(), 0);
    if (!BeginHostDisplay()) {
        return;
    }
    {
        size_t count = 0u;
        count = DisplayCount();
        RoboCheck(count >= 1u);
    }
    DisplayShutdown();
}

static void TestHostDisplayUseWhenSkipped(void)
{
    char reason[64];
    RoboCheckEqInt(DisplayInit(), RoboOk);
    if (HostDisplaySkip(reason, sizeof(reason))) {
        RoboCheckEqInt(DisplayBackendUseHost(), RoboNotSupported);
    }
    DisplayShutdown();
}

void TestRegisterDisplayHost(void)
{
    TestRegister("display.host.availability",
                       TestHostDisplayAvailability);
    TestRegister("display.host.enumerate", TestHostDisplayEnumerate);
    TestRegister("display.host.reinit", TestHostDisplayReinit);
    TestRegister("display.host.use_skipped",
                       TestHostDisplayUseWhenSkipped);
}
