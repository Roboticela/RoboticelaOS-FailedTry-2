/*
 * Win32 RoboDisplay backend (Goal 7.13) — host builds only.
 *
 * EnumDisplayMonitors / GetMonitorInfoW / GetDpiForMonitor (optional).
 * Chromium display::ScreenWin is technical reference only — no dependency.
 */

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RoboDisplay/RoboDisplayBackendHost.h"
#include "RoboDisplayWin32.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayInfo.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define RoboWin32DisplayMax 16

typedef struct Win32Display {
    HMONITOR monitor;
    DisplayInfo info;
} Win32Display;

static Win32Display gDisplays[RoboWin32DisplayMax];
static size_t gDisplayCount;
static int gLive;
static DisplayBackendCaps gCaps;

typedef HRESULT(WINAPI *GetDpiForMonitorFn)(HMONITOR, int, UINT *,
                                                      UINT *);

static float DpiToScale(UINT dpi)
{
    float scale;
    if (dpi == 0u) {
        return 0.0f;
    }
    scale = (float)dpi / RoboDisplayDpiReference;
    if (DisplayScaleValidate(scale) != RoboOk) {
        return 0.0f;
    }
    return scale;
}

static void FillFromMonitor(HMONITOR mon, DisplayInfo *out,
                              DisplayId id)
{
    MONITORINFOEXW mi;
    UINT dpiX = 0u;
    UINT dpiY = 0u;
    float scale = 0.0f;
    int32_t bw;
    int32_t bh;
    int32_t ww;
    int32_t wh;

    DisplayInfoInit(out);
    out->id = id;
    out->connection = RoboDisplayConnectionConnected;

    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, (MONITORINFO *)&mi)) {
        out->connection = RoboDisplayConnectionUnavailable;
        return;
    }

    out->isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    bw = (int32_t)(mi.rcMonitor.right - mi.rcMonitor.left);
    bh = (int32_t)(mi.rcMonitor.bottom - mi.rcMonitor.top);
    ww = (int32_t)(mi.rcWork.right - mi.rcWork.left);
    wh = (int32_t)(mi.rcWork.bottom - mi.rcWork.top);
    out->bounds =
        MakeGfxRect(mi.rcMonitor.left, mi.rcMonitor.top, bw, bh);
    out->workArea = MakeGfxRect(mi.rcWork.left, mi.rcWork.top, ww, wh);
    out->resolutionWidthPx = bw;
    out->resolutionHeightPx = bh;
    out->refreshHz = 0.0f; /* not reliably available without more APIs */
    out->orientation = (bw >= bh) ? RoboDisplayOrientationLandscape
                                  : RoboDisplayOrientationPortrait;
    out->physicalWidthMm = 0;
    out->physicalHeightMm = 0;

    if (mi.szDevice[0]) {
        /* DEVICE name is typically ASCII \\.\DISPLAYN — copy as UTF-8 bytes. */
        size_t i;
        for (i = 0; i + 1u < sizeof(out->name) && mi.szDevice[i]; i++) {
            WCHAR ch = mi.szDevice[i];
            out->name[i] = (ch < 128) ? (char)ch : '?';
        }
        out->name[i] = '\0';
    }

    {
        static GetDpiForMonitorFn SGetDpi;
        static int sResolved;
        if (!sResolved) {
            HMODULE shcore = LoadLibraryW(L"Shcore.dll");
            if (shcore) {
                SGetDpi = (GetDpiForMonitorFn)GetProcAddress(
                    shcore, "GetDpiForMonitor");
            }
            sResolved = 1;
        }
        if (SGetDpi &&
            SGetDpi(mon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY) == S_OK) {
            out->dpiX = (float)dpiX;
            out->dpiY = (float)dpiY;
            scale = DpiToScale(dpiX);
        }
    }
    if (scale <= 0.0f) {
        HDC hdc = GetDC(NULL);
        if (hdc) {
            int logx = GetDeviceCaps(hdc, LOGPIXELSX);
            int logy = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(NULL, hdc);
            if (logx > 0) {
                out->dpiX = (float)logx;
                out->dpiY = (float)(logy > 0 ? logy : logx);
                scale = DpiToScale((UINT)logx);
            }
        }
    }
    out->scale = scale > 0.0f ? scale : 1.0f;
}

static BOOL CALLBACK EnumMonProc(HMONITOR mon, HDC hdc, LPRECT rc,
                                   LPARAM lp)
{
    size_t *count = (size_t *)lp;
    (void)hdc;
    (void)rc;
    if (!count || *count >= RoboWin32DisplayMax) {
        return FALSE;
    }
    gDisplays[*count].monitor = mon;
    FillFromMonitor(mon, &gDisplays[*count].info,
                      (DisplayId)(*count + 1u));
    (*count)++;
    return TRUE;
}

static Result RefreshMonitors(void)
{
    size_t count = 0u;
    memset(gDisplays, 0, sizeof(gDisplays));
    gDisplayCount = 0u;
    if (!EnumDisplayMonitors(NULL, NULL, EnumMonProc, (LPARAM)&count)) {
        LogWarn("robo_display.win32: EnumDisplayMonitors failed");
        return RoboIoError;
    }
    gDisplayCount = count;
    LogInfo("robo_display.win32: enumerated %u monitor(s)",
                  (unsigned)gDisplayCount);
    return RoboOk;
}

DisplayId DisplayWin32IdFromHmonitor(HMONITOR mon)
{
    size_t i;
    if (!mon || !gLive) {
        return RoboDisplayIdNone;
    }
    for (i = 0u; i < gDisplayCount; i++) {
        if (gDisplays[i].monitor == mon) {
            return gDisplays[i].info.id;
        }
    }
    return RoboDisplayIdNone;
}

static Result HostInit(void *ud)
{
    (void)ud;
    gLive = 1;
    return RefreshMonitors();
}

static void HostShutdown(void *ud)
{
    (void)ud;
    gLive = 0;
    gDisplayCount = 0u;
    memset(gDisplays, 0, sizeof(gDisplays));
}

static Result HostEnumerate(void *ud, DisplayInfo *out,
                                    size_t capacity, size_t *outCount)
{
    size_t i;
    size_t n;
    (void)ud;
    if (!outCount) {
        return RoboInvalidArgument;
    }
    if (!gLive) {
        *outCount = 0u;
        return RoboInitError;
    }
    (void)RefreshMonitors();
    n = gDisplayCount;
    *outCount = n;
    if (capacity == 0u) {
        return RoboOk;
    }
    if (!out) {
        return RoboInvalidArgument;
    }
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; i++) {
        out[i] = gDisplays[i].info;
    }
    *outCount = n;
    return RoboOk;
}

static Result HostPrimary(void *ud, DisplayInfo *out)
{
    size_t i;
    (void)ud;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gLive) {
        return RoboInitError;
    }
    (void)RefreshMonitors();
    for (i = 0u; i < gDisplayCount; i++) {
        if (gDisplays[i].info.isPrimary) {
            *out = gDisplays[i].info;
            return RoboOk;
        }
    }
    if (gDisplayCount == 0u) {
        return RoboNotFound;
    }
    *out = gDisplays[0].info;
    return RoboOk;
}

static Result HostGetInfo(void *ud, DisplayId id,
                                   DisplayInfo *out)
{
    size_t i;
    (void)ud;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gLive) {
        return RoboInitError;
    }
    for (i = 0u; i < gDisplayCount; i++) {
        if (gDisplays[i].info.id == id) {
            *out = gDisplays[i].info;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static Result HostGetModes(void *ud, DisplayId id,
                                    DisplayMode *out, size_t capacity,
                                    size_t *outCount)
{
    size_t i;
    (void)ud;
    if (!outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0u;
    for (i = 0u; i < gDisplayCount; i++) {
        if (gDisplays[i].info.id == id) {
            *outCount = 1u;
            if (capacity == 0u) {
                return RoboOk;
            }
            if (!out) {
                return RoboInvalidArgument;
            }
            DisplayModeInit(&out[0]);
            out[0].widthPx = gDisplays[i].info.resolutionWidthPx;
            out[0].heightPx = gDisplays[i].info.resolutionHeightPx;
            out[0].refreshHz = gDisplays[i].info.refreshHz;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static const DisplayBackendVtbl gHostVtbl = {
    "win32",
    &gCaps,
    HostInit,
    HostShutdown,
    HostEnumerate,
    HostPrimary,
    HostGetInfo,
    HostGetModes
};

static void CapsInit(void)
{
    memset(&gCaps, 0, sizeof(gCaps));
    gCaps.enumerate = 1;
    gCaps.primary = 1;
    gCaps.GetInfo = 1;
    gCaps.workArea = 1;
    gCaps.refreshRate = 0; /* not reported */
    gCaps.scaleDpi = 1;
    gCaps.orientation = 1;
    gCaps.modes = 1;
}

int DisplayBackendHostAvailable(void)
{
    if (getenv("ROBO_WINDOW_SKIP_HOST") != NULL) {
        return 0;
    }
    if (GetSystemMetrics(SM_CXSCREEN) <= 0 || GetSystemMetrics(SM_CYSCREEN) <= 0) {
        return 0;
    }
    return 1;
}

const DisplayBackendVtbl *DisplayBackendHostVtbl(void)
{
    CapsInit();
    return &gHostVtbl;
}

Result DisplayBackendUseHost(void)
{
    if (!DisplayBackendHostAvailable()) {
        return RoboNotSupported;
    }
    CapsInit();
    return DisplayBackendRegister(&gHostVtbl, NULL);
}

#endif /* RoboWindowHasWin32 */
