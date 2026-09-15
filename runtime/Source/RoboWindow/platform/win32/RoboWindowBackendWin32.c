/*
 * Win32 RoboWindow backend (Goals 7.5–7.13) — host builds only.
 *
 * Isolated under platform/win32 so OS headers never enter the public API.
 * Guest freestanding ELF does not compile this file.
 *
 * Presentation (Goal 7.13): RoboGraphics surface → staging CreateDIBSection
 * (top-down BGRA/BGRX) → InvalidateRect → WM_PAINT BitBlt into HWND.
 * Inspired by Chromium SoftwareOutputDeviceWinDirect GDI BitBlt path as
 * technical reference only — no Chromium dependency.
 */

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendHost.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboWindow/RoboWindowPresent.h"
#include "RoboWindow/RoboWindowTypes.h"
#include "RoboWindowKeyboardWin32.h"
#include "RoboWindowMouseWin32.h"
#include "RoboDisplayWin32.h"

#include "RoboBase/RoboLog.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define ROBO_WIN32_MAX_WINDOWS 32
#define ROBO_WIN32_CLASS_NAME L"Roboticela.RoboWindow"
#define ROBO_WIN32_TITLE_WCHARS 512
#define ROBO_WIN32_PRESENT_MAX 8192u

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
    /* Goal 7.13 staging framebuffer (owned by backend; not the caller surface). */
    HDC present_dc;
    HBITMAP present_dib;
    HGDIOBJ present_old;
    uint8_t *present_bits;
    int32_t present_w;
    int32_t present_h;
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

static void win32_present_release(robo_win32_slot_t *slot)
{
    if (!slot) {
        return;
    }
    if (slot->present_dc) {
        if (slot->present_old) {
            (void)SelectObject(slot->present_dc, slot->present_old);
            slot->present_old = NULL;
        }
        DeleteDC(slot->present_dc);
        slot->present_dc = NULL;
    }
    if (slot->present_dib) {
        DeleteObject(slot->present_dib);
        slot->present_dib = NULL;
    }
    slot->present_bits = NULL;
    slot->present_w = 0;
    slot->present_h = 0;
}

static robo_result_t win32_present_ensure(robo_win32_slot_t *slot, int32_t w,
                                          int32_t h)
{
    BITMAPINFO bmi;
    HDC screen;
    void *bits = NULL;
    HBITMAP dib;
    HDC mem;

    if (!slot || w <= 0 || h <= 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((uint32_t)w > ROBO_WIN32_PRESENT_MAX ||
        (uint32_t)h > ROBO_WIN32_PRESENT_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (slot->present_dib && slot->present_dc && slot->present_bits &&
        slot->present_w == w && slot->present_h == h) {
        return ROBO_OK;
    }

    win32_present_release(slot);

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)w;
    bmi.bmiHeader.biHeight = -(LONG)h; /* top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    if (!screen) {
        return ROBO_IO_ERROR;
    }
    dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!dib || !bits) {
        if (dib) {
            DeleteObject(dib);
        }
        robo_log_error("robo_window.win32: CreateDIBSection %dx%d failed",
                       (int)w, (int)h);
        return ROBO_OUT_OF_MEMORY;
    }

    mem = CreateCompatibleDC(NULL);
    if (!mem) {
        DeleteObject(dib);
        return ROBO_IO_ERROR;
    }
    slot->present_old = SelectObject(mem, dib);
    slot->present_dc = mem;
    slot->present_dib = dib;
    slot->present_bits = (uint8_t *)bits;
    slot->present_w = w;
    slot->present_h = h;
    memset(slot->present_bits, 0, (size_t)w * (size_t)h * 4u);
    return ROBO_OK;
}

static uint32_t pack_bgrx_le(uint8_t r, uint8_t g, uint8_t b)
{
    /* Win32 BI_RGB 32bpp memory order B,G,R,X → LE 0xXXRRGGBB */
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16) |
           (0xFFu << 24);
}

static uint32_t convert_to_bgrx(robo_gfx_pixel_format_t fmt, const uint8_t *p)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (fmt == ROBO_GFX_FORMAT_BGRA8) {
        b = p[0];
        g = p[1];
        r = p[2];
    } else if (fmt == ROBO_GFX_FORMAT_RGBA8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else if (fmt == ROBO_GFX_FORMAT_RGB8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else {
        return 0u;
    }
    return pack_bgrx_le(r, g, b);
}

static robo_result_t win32_blit_surface(robo_win32_slot_t *slot,
                                        const robo_gfx_surface_t *surface)
{
    const uint8_t *src;
    uint32_t sw;
    uint32_t sh;
    size_t src_stride;
    robo_gfx_pixel_format_t sfmt;
    uint8_t sbpp = 0u;
    uint32_t copy_w;
    uint32_t copy_h;
    uint32_t y;
    uint32_t *dst;
    size_t dst_stride_bytes;

    if (!slot || !slot->present_bits || !surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    sw = robo_gfx_surface_width(surface);
    sh = robo_gfx_surface_height(surface);
    sfmt = robo_gfx_surface_format(surface);
    src_stride = robo_gfx_surface_stride(surface);
    src = robo_gfx_surface_pixels_const(surface);
    if (!src || sw == 0u || sh == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (sfmt != ROBO_GFX_FORMAT_BGRA8 && sfmt != ROBO_GFX_FORMAT_RGBA8 &&
        sfmt != ROBO_GFX_FORMAT_RGB8) {
        return ROBO_NOT_SUPPORTED;
    }
    if (robo_gfx_pixel_format_bytes_per_pixel(sfmt, &sbpp) != ROBO_OK ||
        sbpp == 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    if (src_stride < (size_t)sw * (size_t)sbpp) {
        return ROBO_INVALID_ARGUMENT;
    }

    copy_w = sw;
    copy_h = sh;
    if (copy_w > (uint32_t)slot->present_w) {
        copy_w = (uint32_t)slot->present_w;
    }
    if (copy_h > (uint32_t)slot->present_h) {
        copy_h = (uint32_t)slot->present_h;
    }
    dst_stride_bytes = (size_t)slot->present_w * 4u;
    /* Clear staging so uncovered regions are black after shrink. */
    memset(slot->present_bits, 0,
           (size_t)slot->present_w * (size_t)slot->present_h * 4u);

    for (y = 0u; y < copy_h; y++) {
        const uint8_t *row = src + (size_t)y * src_stride;
        uint32_t x;
        dst = (uint32_t *)(slot->present_bits + (size_t)y * dst_stride_bytes);
        for (x = 0u; x < copy_w; x++) {
            dst[x] = convert_to_bgrx(sfmt, row + (size_t)x * sbpp);
        }
    }
    return ROBO_OK;
}

static void win32_paint_slot(robo_win32_slot_t *slot, HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc;
    RECT cr;
    int cw;
    int ch;

    hdc = BeginPaint(hwnd, &ps);
    if (!hdc) {
        return;
    }
    if (!GetClientRect(hwnd, &cr)) {
        EndPaint(hwnd, &ps);
        return;
    }
    cw = (int)(cr.right - cr.left);
    ch = (int)(cr.bottom - cr.top);
    if (cw <= 0 || ch <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }
    if (slot && slot->present_dc && slot->present_dib && slot->present_w > 0 &&
        slot->present_h > 0) {
        /* Stretch if client size differs from last present buffer. */
        if (cw == slot->present_w && ch == slot->present_h) {
            (void)BitBlt(hdc, 0, 0, cw, ch, slot->present_dc, 0, 0, SRCCOPY);
        } else {
            (void)SetStretchBltMode(hdc, COLORONCOLOR);
            (void)StretchBlt(hdc, 0, 0, cw, ch, slot->present_dc, 0, 0,
                             slot->present_w, slot->present_h, SRCCOPY);
        }
    } else {
        HBRUSH br = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(hdc, &cr, br);
    }
    EndPaint(hwnd, &ps);
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
        /*
         * Goal 7.7/7.9: native close → observe_close_request (CREATED→CLOSING).
         * Do not DestroyWindow here — destroy API owns native teardown.
         */
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            (void)robo_window_observe_close_request(slot->window_id);
        }
        return 0;
    case WM_ERASEBKGND:
        /* Avoid flicker; WM_PAINT paints the full client. */
        return 1;
    case WM_PAINT:
        win32_paint_slot(slot, hwnd);
        return 0;
    case WM_DPICHANGED:
        /* Scale is read in query_state; invalidate so content redraws. */
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_SIZE:
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            if (wparam != SIZE_MINIMIZED) {
                int32_t cw = (int32_t)LOWORD(lparam);
                int32_t ch = (int32_t)HIWORD(lparam);
                if (cw > 0 && ch > 0) {
                    (void)robo_window_observe_size(slot->window_id, cw, ch);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        break;
    case WM_MOVE:
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            int32_t x = (int32_t)(short)LOWORD(lparam);
            int32_t y = (int32_t)(short)HIWORD(lparam);
            (void)robo_window_observe_position(slot->window_id, x, y);
        }
        break;
    case WM_SHOWWINDOW:
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            (void)robo_window_observe_visibility(slot->window_id,
                                                 wparam ? 1 : 0);
        }
        break;
    case WM_SETFOCUS:
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            (void)robo_window_observe_focus(slot->window_id, 1);
        }
        break;
    case WM_KILLFOCUS:
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            (void)robo_window_observe_focus(slot->window_id, 0);
        }
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            (void)robo_keyboard_win32_handle_message(
                slot->window_id, msg, (uintptr_t)wparam, (intptr_t)lparam);
        }
        break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (slot && slot->window_id != ROBO_WINDOW_ID_NONE) {
            (void)robo_mouse_win32_handle_message(
                slot->window_id, (void *)hwnd, msg, (uintptr_t)wparam,
                (intptr_t)lparam);
            if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP ||
                msg == WM_XBUTTONDBLCLK) {
                return TRUE;
            }
        }
        break;
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
    win32_present_release(slot);
    DestroyWindow(slot->hwnd);
    slot->hwnd = NULL;
}

static void clear_slot(robo_win32_slot_t *slot)
{
    if (!slot) {
        return;
    }
    win32_present_release(slot);
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
    if (cfg->scale_preference > 0.0f &&
        robo_display_scale_validate(cfg->scale_preference) == ROBO_OK &&
        cfg->scale_preference != 1.0f) {
        robo_log_debug(
            "robo_window.win32: scale_preference=%g noted (HWND size is logical)",
            (double)cfg->scale_preference);
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

static robo_result_t win32_set_size_constraints(void *userdata,
                                                robo_window_id_t id,
                                                robo_window_native_handle_t *native,
                                                int32_t min_width,
                                                int32_t min_height,
                                                int32_t max_width,
                                                int32_t max_height)
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
    if (min_width < 0 || min_height < 0 || max_width < 0 || max_height < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (min_width > 0 && max_width > 0 && min_width > max_width) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (min_height > 0 && max_height > 0 && min_height > max_height) {
        return ROBO_INVALID_ARGUMENT;
    }
    slot->min_w = min_width;
    slot->min_h = min_height;
    slot->max_w = max_width;
    slot->max_h = max_height;
    /* WM_GETMINMAXINFO picks these up; nudge so Windows refreshes track sizes. */
    {
        RECT wr;
        if (GetWindowRect(slot->hwnd, &wr)) {
            SetWindowPos(slot->hwnd, NULL, 0, 0, wr.right - wr.left,
                         wr.bottom - wr.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_FRAMECHANGED);
        }
    }
    robo_log_debug("robo_window.win32: constraints min=%dx%d max=%dx%d",
                   (int)min_width, (int)min_height, (int)max_width,
                   (int)max_height);
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
    /*
     * Goal 7.8: expose effective DPI/scale when the OS provides it.
     * GetDpiForWindow (Win10 1607+) — unavailable → leave scale unchanged.
     * Do not fabricate DPI. Full display enumeration remains later.
     */
    {
        typedef UINT(WINAPI *robo_get_dpi_for_window_fn)(HWND);
        static robo_get_dpi_for_window_fn s_get_dpi;
        static int s_dpi_resolved;
        if (!s_dpi_resolved) {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) {
                s_get_dpi = (robo_get_dpi_for_window_fn)GetProcAddress(
                    user32, "GetDpiForWindow");
            }
            s_dpi_resolved = 1;
        }
        if (s_get_dpi) {
            UINT dpi = s_get_dpi(slot->hwnd);
            if (dpi > 0u) {
                float scale = (float)dpi / ROBO_DISPLAY_DPI_REFERENCE;
                if (robo_display_scale_validate(scale) == ROBO_OK) {
                    inout_state->scale = scale;
                }
            }
        }
    }
    /* Display association via nearest monitor when host display backend is live. */
    {
        HMONITOR mon = MonitorFromWindow(slot->hwnd, MONITOR_DEFAULTTONEAREST);
        robo_display_id_t did = robo_display_win32_id_from_hmonitor(mon);
        if (did != ROBO_DISPLAY_ID_NONE) {
            inout_state->display_id = did;
        }
    }
    return ROBO_OK;
}

static robo_result_t win32_present_surface(void *userdata,
                                           robo_window_id_t id,
                                           robo_window_native_handle_t *native,
                                           robo_gfx_surface_t *surface)
{
    robo_win32_slot_t *slot;
    robo_result_t r;
    uint32_t sw;
    uint32_t sh;
    RECT cr;
    int32_t client_w;
    int32_t client_h;
    int32_t buf_w;
    int32_t buf_h;

    (void)userdata;
    (void)id;
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    slot = slot_from_handle(native);
    if (!slot || !slot->hwnd) {
        return ROBO_NOT_FOUND;
    }
    r = require_create_thread(slot);
    if (r != ROBO_OK) {
        return r;
    }

    sw = robo_gfx_surface_width(surface);
    sh = robo_gfx_surface_height(surface);
    if (sw == 0u || sh == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    client_w = (int32_t)sw;
    client_h = (int32_t)sh;
    if (GetClientRect(slot->hwnd, &cr)) {
        int32_t cw = (int32_t)(cr.right - cr.left);
        int32_t ch = (int32_t)(cr.bottom - cr.top);
        if (cw > 0 && ch > 0) {
            client_w = cw;
            client_h = ch;
        }
    }
    /* Prefer client size for staging so WM_PAINT 1:1 BitBlt is common. */
    buf_w = client_w;
    buf_h = client_h;
    if ((uint32_t)buf_w < sw) {
        buf_w = (int32_t)sw;
    }
    if ((uint32_t)buf_h < sh) {
        buf_h = (int32_t)sh;
    }

    r = win32_present_ensure(slot, buf_w, buf_h);
    if (r != ROBO_OK) {
        return r;
    }
    r = win32_blit_surface(slot, surface);
    if (r != ROBO_OK) {
        return r;
    }

    InvalidateRect(slot->hwnd, NULL, FALSE);
    /* Synchronous paint so tests can sample pixels immediately. */
    if (!UpdateWindow(slot->hwnd)) {
        robo_log_warn("robo_window.win32: UpdateWindow failed err=%lu",
                      (unsigned long)GetLastError());
    }
    robo_log_debug("robo_window.win32: present id=%llu surface=%ux%u dib=%dx%d",
                   (unsigned long long)slot->window_id, (unsigned)sw,
                   (unsigned)sh, (int)slot->present_w, (int)slot->present_h);
    return ROBO_OK;
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

static robo_result_t win32_wait_events(void *userdata, uint32_t timeout_ms)
{
    DWORD wait_ms;
    (void)userdata;
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    if (timeout_ms == ROBO_WINDOW_EVENT_WAIT_FOREVER) {
        wait_ms = INFINITE;
    } else {
        wait_ms = (DWORD)timeout_ms;
    }
    /*
     * Block until input arrives, PostThreadMessage wakeup, or timeout.
     * Does not drain the queue — caller runs process_events next.
     */
    (void)MsgWaitForMultipleObjects(0, NULL, FALSE, wait_ms, QS_ALLINPUT);
    return ROBO_OK;
}

static robo_result_t win32_wakeup(void *userdata)
{
    (void)userdata;
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    if (g_init_tid == 0u) {
        return ROBO_INIT_ERROR;
    }
    if (!PostThreadMessageW(g_init_tid, WM_NULL, 0, 0)) {
        DWORD err = GetLastError();
        /* No message queue yet is benign for early wake. */
        if (err != ERROR_INVALID_THREAD_ID) {
            robo_log_debug("robo_window.win32: wakeup PostThreadMessage err=%lu",
                           (unsigned long)err);
        }
    }
    return ROBO_OK;
}

static robo_result_t win32_capture_mouse(void *userdata, robo_window_id_t id,
                                         robo_window_native_handle_t *native)
{
    robo_win32_slot_t *slot;
    (void)userdata;
    (void)id;
    if (!g_live) {
        return ROBO_INIT_ERROR;
    }
    slot = slot_from_handle(native);
    if (!slot || !slot->hwnd) {
        return ROBO_NOT_FOUND;
    }
    if (SetCapture(slot->hwnd) == NULL && GetLastError() != 0) {
        /* SetCapture returns previous capture HWND; NULL is often OK. */
    }
    if (GetCapture() != slot->hwnd) {
        robo_log_warn("robo_window.win32: SetCapture failed for id=%llu",
                      (unsigned long long)id);
        return ROBO_IO_ERROR;
    }
    return ROBO_OK;
}

static robo_result_t win32_release_mouse_capture(void *userdata)
{
    (void)userdata;
    if (GetCapture() != NULL) {
        (void)ReleaseCapture();
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
    win32_set_size_constraints,
    win32_set_position,
    win32_focus,
    win32_query_state,
    win32_present_surface,
    win32_process_events,
    win32_wait_events,
    win32_wakeup,
    win32_capture_mouse,
    win32_release_mouse_capture
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
    g_caps.present = 1;
    g_caps.process_events = 1;
    g_caps.reports_actual_state = 1;
    g_caps.size_constraints = 1;
    g_caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT;
    g_caps.present_max_width = ROBO_WIN32_PRESENT_MAX;
    g_caps.present_max_height = ROBO_WIN32_PRESENT_MAX;
    g_caps.wait_events = 1;
    g_caps.wakeup = 1;
    g_caps.mouse_capture = 1;
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
    robo_result_t r;
    if (!robo_window_backend_host_available()) {
        return ROBO_NOT_SUPPORTED;
    }
    caps_init();
    r = robo_window_backend_register(&g_win32_vtbl, NULL);
    if (r != ROBO_OK) {
        return r;
    }
    /* Prefer real monitor enum when display manager is already live. */
    if (robo_display_is_initialized() && robo_display_backend_host_available()) {
        if (robo_display_backend_use_host() == ROBO_OK) {
            (void)robo_display_refresh();
        }
    }
    return ROBO_OK;
}

#endif /* ROBO_WINDOW_HAS_WIN32 */
