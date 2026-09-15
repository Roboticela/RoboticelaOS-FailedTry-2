/*
 * Win32 RoboDisplay backend (Goal 7.13) — host builds only.
 *
 * EnumDisplayMonitors / GetMonitorInfoW / GetDpiForMonitor (optional).
 * Chromium display::ScreenWin is technical reference only — no dependency.
 */

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

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

#define ROBO_WIN32_DISPLAY_MAX 16

typedef struct robo_win32_display {
    HMONITOR monitor;
    robo_display_info_t info;
} robo_win32_display_t;

static robo_win32_display_t g_displays[ROBO_WIN32_DISPLAY_MAX];
static size_t g_display_count;
static int g_live;
static robo_display_backend_caps_t g_caps;

typedef HRESULT(WINAPI *robo_get_dpi_for_monitor_fn)(HMONITOR, int, UINT *,
                                                      UINT *);

static float dpi_to_scale(UINT dpi)
{
    float scale;
    if (dpi == 0u) {
        return 0.0f;
    }
    scale = (float)dpi / ROBO_DISPLAY_DPI_REFERENCE;
    if (robo_display_scale_validate(scale) != ROBO_OK) {
        return 0.0f;
    }
    return scale;
}

static void fill_from_monitor(HMONITOR mon, robo_display_info_t *out,
                              robo_display_id_t id)
{
    MONITORINFOEXW mi;
    UINT dpi_x = 0u;
    UINT dpi_y = 0u;
    float scale = 0.0f;
    int32_t bw;
    int32_t bh;
    int32_t ww;
    int32_t wh;

    robo_display_info_init(out);
    out->id = id;
    out->connection = ROBO_DISPLAY_CONNECTION_CONNECTED;

    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, (MONITORINFO *)&mi)) {
        out->connection = ROBO_DISPLAY_CONNECTION_UNAVAILABLE;
        return;
    }

    out->is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    bw = (int32_t)(mi.rcMonitor.right - mi.rcMonitor.left);
    bh = (int32_t)(mi.rcMonitor.bottom - mi.rcMonitor.top);
    ww = (int32_t)(mi.rcWork.right - mi.rcWork.left);
    wh = (int32_t)(mi.rcWork.bottom - mi.rcWork.top);
    out->bounds =
        robo_gfx_rect(mi.rcMonitor.left, mi.rcMonitor.top, bw, bh);
    out->work_area = robo_gfx_rect(mi.rcWork.left, mi.rcWork.top, ww, wh);
    out->resolution_width_px = bw;
    out->resolution_height_px = bh;
    out->refresh_hz = 0.0f; /* not reliably available without more APIs */
    out->orientation = (bw >= bh) ? ROBO_DISPLAY_ORIENTATION_LANDSCAPE
                                  : ROBO_DISPLAY_ORIENTATION_PORTRAIT;
    out->physical_width_mm = 0;
    out->physical_height_mm = 0;

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
        static robo_get_dpi_for_monitor_fn s_get_dpi;
        static int s_resolved;
        if (!s_resolved) {
            HMODULE shcore = LoadLibraryW(L"Shcore.dll");
            if (shcore) {
                s_get_dpi = (robo_get_dpi_for_monitor_fn)GetProcAddress(
                    shcore, "GetDpiForMonitor");
            }
            s_resolved = 1;
        }
        if (s_get_dpi &&
            s_get_dpi(mon, 0 /* MDT_EFFECTIVE_DPI */, &dpi_x, &dpi_y) == S_OK) {
            out->dpi_x = (float)dpi_x;
            out->dpi_y = (float)dpi_y;
            scale = dpi_to_scale(dpi_x);
        }
    }
    if (scale <= 0.0f) {
        HDC hdc = GetDC(NULL);
        if (hdc) {
            int logx = GetDeviceCaps(hdc, LOGPIXELSX);
            int logy = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(NULL, hdc);
            if (logx > 0) {
                out->dpi_x = (float)logx;
                out->dpi_y = (float)(logy > 0 ? logy : logx);
                scale = dpi_to_scale((UINT)logx);
            }
        }
    }
    out->scale = scale > 0.0f ? scale : 1.0f;
}

static BOOL CALLBACK enum_mon_proc(HMONITOR mon, HDC hdc, LPRECT rc,
                                   LPARAM lp)
{
    size_t *count = (size_t *)lp;
    (void)hdc;
    (void)rc;
    if (!count || *count >= ROBO_WIN32_DISPLAY_MAX) {
        return FALSE;
    }
    g_displays[*count].monitor = mon;
    fill_from_monitor(mon, &g_displays[*count].info,
                      (robo_display_id_t)(*count + 1u));
    (*count)++;
    return TRUE;
}

static robo_result_t refresh_monitors(void)
{
    size_t count = 0u;
    memset(g_displays, 0, sizeof(g_displays));
    g_display_count = 0u;
    if (!EnumDisplayMonitors(NULL, NULL, enum_mon_proc, (LPARAM)&count)) {
        robo_log_warn("robo_display.win32: EnumDisplayMonitors failed");
        return ROBO_IO_ERROR;
    }
    g_display_count = count;
    robo_log_info("robo_display.win32: enumerated %u monitor(s)",
                  (unsigned)g_display_count);
    return ROBO_OK;
}

robo_display_id_t robo_display_win32_id_from_hmonitor(HMONITOR mon)
{
    size_t i;
    if (!mon || !g_live) {
        return ROBO_DISPLAY_ID_NONE;
    }
    for (i = 0u; i < g_display_count; i++) {
        if (g_displays[i].monitor == mon) {
            return g_displays[i].info.id;
        }
    }
    return ROBO_DISPLAY_ID_NONE;
}

static robo_result_t host_init(void *ud)
{
    (void)ud;
    g_live = 1;
    return refresh_monitors();
}

static void host_shutdown(void *ud)
{
    (void)ud;
    g_live = 0;
    g_display_count = 0u;
    memset(g_displays, 0, sizeof(g_displays));
}

static robo_result_t host_enumerate(void *ud, robo_display_info_t *out,
                                    size_t capacity, size_t *out_count)
{
    size_t i;
    size_t n;
    (void)ud;
    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_live) {
        *out_count = 0u;
        return ROBO_INIT_ERROR;
    }
    (void)refresh_monitors();
    n = g_display_count;
    *out_count = n;
    if (capacity == 0u) {
        return ROBO_OK;
    }
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; i++) {
        out[i] = g_displays[i].info;
    }
    *out_count = n;
    return ROBO_OK;
}

static robo_result_t host_primary(void *ud, robo_display_info_t *out)
{
    size_t i;
    (void)ud;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    (void)refresh_monitors();
    for (i = 0u; i < g_display_count; i++) {
        if (g_displays[i].info.is_primary) {
            *out = g_displays[i].info;
            return ROBO_OK;
        }
    }
    if (g_display_count == 0u) {
        return ROBO_NOT_FOUND;
    }
    *out = g_displays[0].info;
    return ROBO_OK;
}

static robo_result_t host_get_info(void *ud, robo_display_id_t id,
                                   robo_display_info_t *out)
{
    size_t i;
    (void)ud;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    for (i = 0u; i < g_display_count; i++) {
        if (g_displays[i].info.id == id) {
            *out = g_displays[i].info;
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

static robo_result_t host_get_modes(void *ud, robo_display_id_t id,
                                    robo_display_mode_t *out, size_t capacity,
                                    size_t *out_count)
{
    size_t i;
    (void)ud;
    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    for (i = 0u; i < g_display_count; i++) {
        if (g_displays[i].info.id == id) {
            *out_count = 1u;
            if (capacity == 0u) {
                return ROBO_OK;
            }
            if (!out) {
                return ROBO_INVALID_ARGUMENT;
            }
            robo_display_mode_init(&out[0]);
            out[0].width_px = g_displays[i].info.resolution_width_px;
            out[0].height_px = g_displays[i].info.resolution_height_px;
            out[0].refresh_hz = g_displays[i].info.refresh_hz;
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

static const robo_display_backend_vtbl_t g_host_vtbl = {
    "win32",
    &g_caps,
    host_init,
    host_shutdown,
    host_enumerate,
    host_primary,
    host_get_info,
    host_get_modes
};

static void caps_init(void)
{
    memset(&g_caps, 0, sizeof(g_caps));
    g_caps.enumerate = 1;
    g_caps.primary = 1;
    g_caps.get_info = 1;
    g_caps.work_area = 1;
    g_caps.refresh_rate = 0; /* not reported */
    g_caps.scale_dpi = 1;
    g_caps.orientation = 1;
    g_caps.modes = 1;
}

int robo_display_backend_host_available(void)
{
    if (getenv("ROBO_WINDOW_SKIP_HOST") != NULL) {
        return 0;
    }
    if (GetSystemMetrics(SM_CXSCREEN) <= 0 || GetSystemMetrics(SM_CYSCREEN) <= 0) {
        return 0;
    }
    return 1;
}

const robo_display_backend_vtbl_t *robo_display_backend_host_vtbl(void)
{
    caps_init();
    return &g_host_vtbl;
}

robo_result_t robo_display_backend_use_host(void)
{
    if (!robo_display_backend_host_available()) {
        return ROBO_NOT_SUPPORTED;
    }
    caps_init();
    return robo_display_backend_register(&g_host_vtbl, NULL);
}

#endif /* ROBO_WINDOW_HAS_WIN32 */
