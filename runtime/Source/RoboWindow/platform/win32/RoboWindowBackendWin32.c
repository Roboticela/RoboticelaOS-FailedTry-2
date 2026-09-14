/*
 * Win32 RoboWindow backend (Goal 7.5) — host builds only.
 *
 * Isolated under platform/win32 so OS headers never enter the public API.
 * Guest freestanding ELF does not compile this file.
 *
 * APIs: RegisterClassExW, CreateWindowExW, DestroyWindow, ShowWindow,
 * SetWindowTextW, SetWindowPos, SetFocus, GetWindowRect/GetClientRect,
 * PeekMessageW/DispatchMessageW, MonitorFromWindow (display association hint).
 *
 * present_surface → ROBO_NOT_SUPPORTED (Goal 7.4 mock path; real present later).
 */

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowTypes.h"

#include "RoboBase/RoboLog.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define ROBO_WIN32_MAX_WINDOWS 32
#define ROBO_WIN32_CLASS_NAME L"Roboticela.RoboWindow"
#define ROBO_WIN32_TITLE_WCHARS 512

typedef struct robo_win32_slot {
    int in_use;
    robo_window_id_t window_id;
    HWND hwnd;
    uint32_t generation;
    DWORD create_tid;
    int32_t min_w;
    int32_t min_h;
    int32_t max_w;
    int32_t max_h;
    int resizable;
} robo_win32_slot_t;

static int g_live;
static int g_class_registered;
static HINSTANCE g_instance;
static DWORD g_init_tid;
static uint32_t g_next_generation = 1u;
static robo_win32_slot_t g_slots[ROBO_WIN32_MAX_WINDOWS];
static robo_window_backend_caps_t g_caps;

static robo_win32_slot_t *slot_from_handle(const robo_window_native_handle_t *native)
{
    uint64_t token;
    size_t idx;
    if (!robo_window_native_handle_is_valid(native)) {
        return NULL;
    }
    token = native->token;
    if (token == 0u || token > (uint64_t)ROBO_WIN32_MAX_WINDOWS) {
        return NULL;
    }
    idx = (size_t)(token - 1u);
    if (!g_slots[idx].in_use || g_slots[idx].generation != native->generation ||
        !g_slots[idx].hwnd) {
        return NULL;
    }
    return &g_slots[idx];
}

static robo_result_t require_create_thread(const robo_win32_slot_t *slot)
{
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (GetCurrentThreadId() != slot->create_tid) {
        robo_log_warn("robo_window.win32: wrong thread (create=%lu current=%lu)",
                      (unsigned long)slot->create_tid,
                      (unsigned long)GetCurrentThreadId());
        return ROBO_PERMISSION_DENIED;
    }
    return ROBO_OK;
}

static robo_result_t utf8_to_wide(const char *utf8, WCHAR *out, size_t out_cap)
{
    size_t src_len;
    size_t need = 0u;
    robo_unicode_status_t st;

    if (!out || out_cap == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    out[0] = L'\0';
    if (!utf8 || !utf8[0]) {
        return ROBO_OK;
    }
    src_len = strlen(utf8);
    if (!robo_unicode_utf8_validate(utf8, src_len)) {
        return ROBO_PARSE_ERROR;
    }
    st = robo_unicode_utf8_to_utf16(utf8, src_len, NULL, 0u, &need,
                                    ROBO_UNICODE_STRICT);
    if (st != ROBO_UNICODE_OK && st != ROBO_UNICODE_NO_SPACE) {
        return ROBO_PARSE_ERROR;
    }
    if (need + 1u > out_cap) {
        return ROBO_OUT_OF_MEMORY;
    }
    st = robo_unicode_utf8_to_utf16(utf8, src_len, (robo_uchar16_t *)out, out_cap - 1u,
                                    &need, ROBO_UNICODE_STRICT);
    if (st != ROBO_UNICODE_OK) {
        return ROBO_PARSE_ERROR;
    }
    out[need] = L'\0';
    return ROBO_OK;
}

static void client_to_outer(DWORD style, DWORD ex, int32_t cw, int32_t ch,
                            int32_t *ow, int32_t *oh)
{
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = (LONG)cw;
    r.bottom = (LONG)ch;
    AdjustWindowRectEx(&r, style, FALSE, ex);
    *ow = (int32_t)(r.right - r.left);
    *oh = (int32_t)(r.bottom - r.top);
}

static DWORD window_style(const robo_window_config_t *cfg)
{
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (cfg && !cfg->resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    return style;
}

static LRESULT CALLBACK robo_win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam,
                                           LPARAM lparam)
{
    robo_win32_slot_t *slot =
        (robo_win32_slot_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_GETMINMAXINFO:
        if (slot) {
            MINMAXINFO *mmi = (MINMAXINFO *)lparam;
            int32_t ow, oh;
            DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
            DWORD ex = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (slot->min_w > 0 && slot->min_h > 0) {
                client_to_outer(style, ex, slot->min_w, slot->min_h, &ow, &oh);
                mmi->ptMinTrackSize.x = (LONG)ow;
                mmi->ptMinTrackSize.y = (LONG)oh;
            }
            if (slot->max_w > 0 && slot->max_h > 0) {
                client_to_outer(style, ex, slot->max_w, slot->max_h, &ow, &oh);
                mmi->ptMaxTrackSize.x = (LONG)ow;
                mmi->ptMaxTrackSize.y = (LONG)oh;
            }
        }
        break;
    case WM_CLOSE:
        /* Phase 7.9 will route events fully; keep HWND until destroy API. */
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            robo_window_event_t ev;
            robo_window_event_init(&ev);
            ev.type = ROBO_WINDOW_EVENT_CLOSE_REQUESTED;
            ev.window_id = slot->window_id;
            (void)robo_window_event_push(&ev);
        }
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static robo_result_t ensure_class(void)
{
    WNDCLASSEXW wc;
    if (g_class_registered) {
        return ROBO_OK;
    }
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = robo_win32_wndproc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); /* IDC_ARROW */
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = ROBO_WIN32_CLASS_NAME;
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            robo_log_error("robo_window.win32: RegisterClassExW failed err=%lu",
                           (unsigned long)err);
            return ROBO_IO_ERROR;
        }
    }
    g_class_registered = 1;
    return ROBO_OK;
}

static void destroy_hwnd(robo_win32_slot_t *slot)
{
    if (!slot || !slot->hwnd) {
        return;
    }
    DestroyWindow(slot->hwnd);
    slot->hwnd = NULL;
}

static void clear_slot(robo_win32_slot_t *slot)
{
    if (!slot) {
        return;
    }
    memset(slot, 0, sizeof(*slot));
}

static robo_result_t win32_initialize(void *userdata)
{
    (void)userdata;
    if (g_live) {
        return ROBO_BUSY;
    }
    g_instance = GetModuleHandleW(NULL);
    if (!g_instance) {
        robo_log_error("robo_window.win32: GetModuleHandleW failed");
        return ROBO_IO_ERROR;
    }
    if (ensure_class() != ROBO_OK) {
        return ROBO_IO_ERROR;
    }
    memset(g_slots, 0, sizeof(g_slots));
    g_init_tid = GetCurrentThreadId();
    g_live = 1;
    robo_log_info("robo_window.win32: backend initialize tid=%lu",
                  (unsigned long)g_init_tid);
    return ROBO_OK;
}

static void win32_shutdown(void *userdata)
{
    size_t i;
    (void)userdata;
    if (!g_live) {
        return;
    }
    for (i = 0u; i < ROBO_WIN32_MAX_WINDOWS; i++) {
        if (g_slots[i].in_use) {
            destroy_hwnd(&g_slots[i]);
            clear_slot(&g_slots[i]);
        }
    }
    g_live = 0;
    robo_log_info("robo_window.win32: backend shutdown");
}

static robo_result_t win32_create_window(void *userdata,
                                         robo_window_id_t id,
                                         const robo_window_config_t *cfg,
                                         robo_window_native_handle_t *out_native)
{
    size_t i;
    robo_win32_slot_t *slot = NULL;
    DWORD style;
    DWORD ex = 0;
    int32_t ow, oh;
    int x, y;
    WCHAR title_w[ROBO_WIN32_TITLE_WCHARS];
    HWND hwnd;
    robo_result_t r;
    uint32_t gen;

    (void)userdata;
    if (!g_live || !cfg || !out_native) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (GetCurrentThreadId() != g_init_tid) {
        robo_log_warn("robo_window.win32: create on non-init thread");
        return ROBO_PERMISSION_DENIED;
    }
    if (cfg->fullscreen) {
        robo_log_warn("robo_window.win32: fullscreen deferred (not applied)");
    }
    if (cfg->scale_preference > 0.0f && cfg->scale_preference != 1.0f) {
        robo_log_debug("robo_window.win32: scale_preference ignored in 7.5");
    }

    for (i = 0u; i < ROBO_WIN32_MAX_WINDOWS; i++) {
        if (!g_slots[i].in_use) {
            slot = &g_slots[i];
            break;
        }
    }
    if (!slot) {
        return ROBO_BUSY;
    }

    r = utf8_to_wide(cfg->title, title_w, ROBO_WIN32_TITLE_WCHARS);
    if (r != ROBO_OK) {
        return r;
    }

    style = window_style(cfg);
    client_to_outer(style, ex, cfg->width, cfg->height, &ow, &oh);
    if (cfg->x == ROBO_WINDOW_COORD_UNKNOWN || cfg->y == ROBO_WINDOW_COORD_UNKNOWN) {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
    } else {
        x = (int)cfg->x;
        y = (int)cfg->y;
    }

    hwnd = CreateWindowExW(ex, ROBO_WIN32_CLASS_NAME, title_w, style, x, y, (int)ow,
                           (int)oh, NULL, NULL, g_instance, NULL);
    if (!hwnd) {
        robo_log_error("robo_window.win32: CreateWindowExW failed err=%lu",
                       (unsigned long)GetLastError());
        return ROBO_IO_ERROR;
    }

    gen = g_next_generation++;
    if (gen == 0u) {
        gen = g_next_generation++;
    }

    slot->in_use = 1;
    slot->window_id = id;
    slot->hwnd = hwnd;
    slot->generation = gen;
    slot->create_tid = GetCurrentThreadId();
    slot->min_w = cfg->min_width;
    slot->min_h = cfg->min_height;
    slot->max_w = cfg->max_width;
    slot->max_h = cfg->max_height;
    slot->resizable = cfg->resizable ? 1 : 0;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)slot);

    r = robo_window_native_handle_assign(out_native, (uint64_t)(i + 1u), gen);
    if (r != ROBO_OK) {
        destroy_hwnd(slot);
        clear_slot(slot);
        return r;
    }

    robo_log_debug("robo_window.win32: created hwnd=%p id=%llu token=%llu gen=%u",
                   (void *)hwnd, (unsigned long long)id,
                   (unsigned long long)(i + 1u), (unsigned)gen);
    return ROBO_OK;
}

static robo_result_t win32_destroy_window(void *userdata,
                                          robo_window_id_t id,
                                          robo_window_native_handle_t *native)
{
    robo_win32_slot_t *slot;
    robo_result_t r;
    (void)userdata;
    (void)id;
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    slot = slot_from_handle(native);
    if (!slot) {
        if (native) {
            robo_window_native_handle_invalidate(native);
        }
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }
    destroy_hwnd(slot);
    clear_slot(slot);
    if (native) {
        robo_window_native_handle_invalidate(native);
    }
    return ROBO_OK;
}

static robo_result_t win32_show(void *userdata,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    robo_win32_slot_t *slot;
    robo_result_t r;
    (void)userdata;
    (void)id;
    slot = slot_from_handle(native);
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (!ShowWindow(slot->hwnd, SW_SHOWNOACTIVATE)) {
        /* Already visible is OK; ShowWindow returns previous visibility. */
    }
    UpdateWindow(slot->hwnd);
    return ROBO_OK;
}

static robo_result_t win32_hide(void *userdata,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    robo_win32_slot_t *slot;
    robo_result_t r;
    (void)userdata;
    (void)id;
    slot = slot_from_handle(native);
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }
    ShowWindow(slot->hwnd, SW_HIDE);
    return ROBO_OK;
}

static robo_result_t win32_set_title(void *userdata,
                                     robo_window_id_t id,
                                     robo_window_native_handle_t *native,
                                     const char *utf8_title)
{
    robo_win32_slot_t *slot;
    WCHAR title_w[ROBO_WIN32_TITLE_WCHARS];
    robo_result_t r;
    (void)userdata;
    (void)id;
    slot = slot_from_handle(native);
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }
    r = utf8_to_wide(utf8_title, title_w, ROBO_WIN32_TITLE_WCHARS);
    if (r != ROBO_OK) {
        return r;
    }
    if (!SetWindowTextW(slot->hwnd, title_w)) {
        robo_log_error("robo_window.win32: SetWindowTextW failed err=%lu",
                       (unsigned long)GetLastError());
        return ROBO_IO_ERROR;
    }
    return ROBO_OK;
}

static robo_result_t win32_set_size(void *userdata,
                                    robo_window_id_t id,
                                    robo_window_native_handle_t *native,
                                    int32_t width,
                                    int32_t height)
{
    robo_win32_slot_t *slot;
    DWORD style, ex;
    int32_t ow, oh;
    robo_result_t r;
    (void)userdata;
    (void)id;
    if (width <= 0 || height <= 0 || width > ROBO_WINDOW_MAX_DIM ||
        height > ROBO_WINDOW_MAX_DIM) {
        return ROBO_INVALID_ARGUMENT;
    }
    slot = slot_from_handle(native);
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (slot->min_w > 0 && width < slot->min_w) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (slot->min_h > 0 && height < slot->min_h) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (slot->max_w > 0 && width > slot->max_w) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (slot->max_h > 0 && height > slot->max_h) {
        return ROBO_INVALID_ARGUMENT;
    }
    style = (DWORD)GetWindowLongPtrW(slot->hwnd, GWL_STYLE);
    ex = (DWORD)GetWindowLongPtrW(slot->hwnd, GWL_EXSTYLE);
    client_to_outer(style, ex, width, height, &ow, &oh);
    if (!SetWindowPos(slot->hwnd, NULL, 0, 0, (int)ow, (int)oh,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        robo_log_error("robo_window.win32: SetWindowPos(size) failed err=%lu",
                       (unsigned long)GetLastError());
        return ROBO_IO_ERROR;
    }
    return ROBO_OK;
}

static robo_result_t win32_set_position(void *userdata,
                                        robo_window_id_t id,
                                        robo_window_native_handle_t *native,
                                        int32_t x,
                                        int32_t y)
{
    robo_win32_slot_t *slot;
    robo_result_t r;
    (void)userdata;
    (void)id;
    slot = slot_from_handle(native);
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (!SetWindowPos(slot->hwnd, NULL, (int)x, (int)y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        robo_log_error("robo_window.win32: SetWindowPos(move) failed err=%lu",
                       (unsigned long)GetLastError());
        return ROBO_IO_ERROR;
    }
    return ROBO_OK;
}

static robo_result_t win32_focus(void *userdata,
                                 robo_window_id_t id,
                                 robo_window_native_handle_t *native)
{
    robo_win32_slot_t *slot;
    robo_result_t r;
    (void)userdata;
    (void)id;
    slot = slot_from_handle(native);
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (!IsWindowVisible(slot->hwnd)) {
        return ROBO_NOT_SUPPORTED;
    }
    if (!SetForegroundWindow(slot->hwnd)) {
        /* Common when OS blocks foreground steals — do not fake success. */
        robo_log_debug("robo_window.win32: SetForegroundWindow declined err=%lu",
                       (unsigned long)GetLastError());
        return ROBO_NOT_SUPPORTED;
    }
    SetFocus(slot->hwnd);
    return ROBO_OK;
}

static robo_result_t win32_query_state(void *userdata,
                                       robo_window_id_t id,
                                       const robo_window_native_handle_t *native,
                                       robo_window_state_t *inout_state)
{
    robo_win32_slot_t *slot;
    RECT wr, cr;
    (void)userdata;
    (void)id;
    if (!inout_state) {
        return ROBO_INVALID_ARGUMENT;
    }
    slot = slot_from_handle(native);
    if (!slot) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!GetWindowRect(slot->hwnd, &wr) || !GetClientRect(slot->hwnd, &cr)) {
        return ROBO_IO_ERROR;
    }
    inout_state->x = (int32_t)wr.left;
    inout_state->y = (int32_t)wr.top;
    inout_state->width = (int32_t)(cr.right - cr.left);
    inout_state->height = (int32_t)(cr.bottom - cr.top);
    inout_state->visible = IsWindowVisible(slot->hwnd) ? 1 : 0;
    inout_state->focused = (GetForegroundWindow() == slot->hwnd) ? 1 : 0;
    inout_state->minimized = IsIconic(slot->hwnd) ? 1 : 0;
    inout_state->maximized = IsZoomed(slot->hwnd) ? 1 : 0;
    /* Display association: keep existing id; host enum is later. */
    if (inout_state->display_id == ROBO_DISPLAY_ID_NONE) {
        /* MonitorFromWindow available for future mapping. */
        (void)MonitorFromWindow(slot->hwnd, MONITOR_DEFAULTTONEAREST);
    }
    return ROBO_OK;
}

static robo_result_t win32_present_surface(void *userdata,
                                           robo_window_id_t id,
                                           robo_window_native_handle_t *native,
                                           robo_gfx_surface_t *surface)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)surface;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t win32_process_events(void *userdata)
{
    MSG msg;
    int n = 0;
    (void)userdata;
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (++n > 256) {
            break;
        }
    }
    return ROBO_OK;
}

static const robo_window_backend_vtbl_t g_win32_vtbl = {
    "win32",
    &g_caps,
    win32_initialize,
    win32_shutdown,
    win32_create_window,
    win32_destroy_window,
    win32_show,
    win32_hide,
    win32_set_title,
    win32_set_size,
    win32_set_position,
    win32_focus,
    win32_query_state,
    win32_present_surface,
    win32_process_events
};

static void caps_init(void)
{
    memset(&g_caps, 0, sizeof(g_caps));
    g_caps.create_window = 1;
    g_caps.destroy_window = 1;
    g_caps.show_hide = 1;
    g_caps.resize = 1;
    g_caps.move = 1;
    g_caps.set_title = 1;
    g_caps.focus = 1;
    g_caps.query_state = 1;
    g_caps.present = 0;
    g_caps.process_events = 1;
    g_caps.reports_actual_state = 1;
    g_caps.present_format_mask = 0u;
    g_caps.present_max_width = 0u;
    g_caps.present_max_height = 0u;
}

int robo_window_backend_host_available(void)
{
    if (getenv("ROBO_WINDOW_SKIP_HOST") != NULL) {
        return 0;
    }
    /* Session without interactive desktop → treat as unavailable. */
    if (GetSystemMetrics(SM_CXSCREEN) <= 0 || GetSystemMetrics(SM_CYSCREEN) <= 0) {
        return 0;
    }
    return 1;
}

const robo_window_backend_vtbl_t *robo_window_backend_host_vtbl(void)
{
    caps_init();
    return &g_win32_vtbl;
}

robo_result_t robo_window_backend_use_host(void)
{
    if (!robo_window_backend_host_available()) {
        return ROBO_NOT_SUPPORTED;
    }
    caps_init();
    return robo_window_backend_register(&g_win32_vtbl, NULL);
}

#endif /* ROBO_WINDOW_HAS_WIN32 */
