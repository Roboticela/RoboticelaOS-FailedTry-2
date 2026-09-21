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

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

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

#define RoboWin32MaxWindows 32
#define RoboWin32ClassName L"Roboticela.RoboWindow"
#define RoboWin32TitleWchars 512
#define RoboWin32PresentMax 8192u

typedef struct Win32Slot {
    int inUse;
    WindowId windowId;
    HWND hwnd;
    uint32_t generation;
    DWORD createTid;
    int32_t minW;
    int32_t minH;
    int32_t maxW;
    int32_t maxH;
    int resizable;
    /* Goal 7.13 staging framebuffer (owned by backend; not the caller surface). */
    HDC presentDc;
    HBITMAP presentDib;
    HGDIOBJ presentOld;
    uint8_t *presentBits;
    int32_t presentW;
    int32_t presentH;
} Win32Slot;

static int gLive;
static int gClassRegistered;
static HINSTANCE gInstance;
static DWORD gInitTid;
static uint32_t gNextGeneration = 1u;
static Win32Slot gSlots[RoboWin32MaxWindows];
static WindowBackendCaps gCaps;

static Win32Slot *SlotFromHandle(const WindowNativeHandle *native)
{
    uint64_t token;
    size_t idx;
    if (!WindowNativeHandleIsValid(native)) {
        return NULL;
    }
    token = native->token;
    if (token == 0u || token > (uint64_t)RoboWin32MaxWindows) {
        return NULL;
    }
    idx = (size_t)(token - 1u);
    if (!gSlots[idx].inUse || gSlots[idx].generation != native->generation ||
        !gSlots[idx].hwnd) {
        return NULL;
    }
    return &gSlots[idx];
}

static Result RequireCreateThread(const Win32Slot *slot)
{
    if (!slot) {
        return RoboInvalidArgument;
    }
    if (GetCurrentThreadId() != slot->createTid) {
        LogWarn("robo_window.win32: wrong thread (create=%lu current=%lu)",
                      (unsigned long)slot->createTid,
                      (unsigned long)GetCurrentThreadId());
        return RoboPermissionDenied;
    }
    return RoboOk;
}

static Result Utf8ToWide(const char *utf8, WCHAR *out, size_t outCap)
{
    size_t srcLen;
    size_t need = 0u;
    UnicodeStatus st;

    if (!out || outCap == 0u) {
        return RoboInvalidArgument;
    }
    out[0] = L'\0';
    if (!utf8 || !utf8[0]) {
        return RoboOk;
    }
    srcLen = strlen(utf8);
    if (!UnicodeUtf8Validate(utf8, srcLen)) {
        return RoboParseError;
    }
    st = UnicodeUtf8ToUtf16(utf8, srcLen, NULL, 0u, &need,
                                    RoboUnicodeStrict);
    if (st != RoboUnicodeOk && st != RoboUnicodeNoSpace) {
        return RoboParseError;
    }
    if (need + 1u > outCap) {
        return RoboOutOfMemory;
    }
    st = UnicodeUtf8ToUtf16(utf8, srcLen, (Uchar16 *)out, outCap - 1u,
                                    &need, RoboUnicodeStrict);
    if (st != RoboUnicodeOk) {
        return RoboParseError;
    }
    out[need] = L'\0';
    return RoboOk;
}

static void ClientToOuter(DWORD style, DWORD ex, int32_t cw, int32_t ch,
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

static void Win32PresentRelease(Win32Slot *slot)
{
    if (!slot) {
        return;
    }
    if (slot->presentDc) {
        if (slot->presentOld) {
            (void)SelectObject(slot->presentDc, slot->presentOld);
            slot->presentOld = NULL;
        }
        DeleteDC(slot->presentDc);
        slot->presentDc = NULL;
    }
    if (slot->presentDib) {
        DeleteObject(slot->presentDib);
        slot->presentDib = NULL;
    }
    slot->presentBits = NULL;
    slot->presentW = 0;
    slot->presentH = 0;
}

static Result Win32PresentEnsure(Win32Slot *slot, int32_t w,
                                          int32_t h)
{
    BITMAPINFO bmi;
    HDC screen;
    void *bits = NULL;
    HBITMAP dib;
    HDC mem;

    if (!slot || w <= 0 || h <= 0) {
        return RoboInvalidArgument;
    }
    if ((uint32_t)w > RoboWin32PresentMax ||
        (uint32_t)h > RoboWin32PresentMax) {
        return RoboInvalidArgument;
    }
    if (slot->presentDib && slot->presentDc && slot->presentBits &&
        slot->presentW == w && slot->presentH == h) {
        return RoboOk;
    }

    Win32PresentRelease(slot);

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)w;
    bmi.bmiHeader.biHeight = -(LONG)h; /* top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    if (!screen) {
        return RoboIoError;
    }
    dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!dib || !bits) {
        if (dib) {
            DeleteObject(dib);
        }
        LogError("robo_window.win32: CreateDIBSection %dx%d failed",
                       (int)w, (int)h);
        return RoboOutOfMemory;
    }

    mem = CreateCompatibleDC(NULL);
    if (!mem) {
        DeleteObject(dib);
        return RoboIoError;
    }
    slot->presentOld = SelectObject(mem, dib);
    slot->presentDc = mem;
    slot->presentDib = dib;
    slot->presentBits = (uint8_t *)bits;
    slot->presentW = w;
    slot->presentH = h;
    memset(slot->presentBits, 0, (size_t)w * (size_t)h * 4u);
    return RoboOk;
}

static uint32_t PackBgrxLe(uint8_t r, uint8_t g, uint8_t b)
{
    /* Win32 BI_RGB 32bpp memory order B,G,R,X → LE 0xXXRRGGBB */
    return ((uint32_t)b) | ((uint32_t)g << 8) | ((uint32_t)r << 16) |
           (0xFFu << 24);
}

static uint32_t ConvertToBgrx(GfxPixelFormat fmt, const uint8_t *p)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (fmt == RoboGfxFormatBgra8) {
        b = p[0];
        g = p[1];
        r = p[2];
    } else if (fmt == RoboGfxFormatRgba8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else if (fmt == RoboGfxFormatRgb8) {
        r = p[0];
        g = p[1];
        b = p[2];
    } else {
        return 0u;
    }
    return PackBgrxLe(r, g, b);
}

static Result Win32BlitSurface(Win32Slot *slot,
                                        const GfxSurface *surface)
{
    const uint8_t *src;
    uint32_t sw;
    uint32_t sh;
    size_t srcStride;
    GfxPixelFormat sfmt;
    uint8_t sbpp = 0u;
    uint32_t copyW;
    uint32_t copyH;
    uint32_t y;
    uint32_t *dst;
    size_t dstStrideBytes;

    if (!slot || !slot->presentBits || !surface) {
        return RoboInvalidArgument;
    }
    sw = GfxSurfaceWidth(surface);
    sh = GfxSurfaceHeight(surface);
    sfmt = GfxSurfaceFormat(surface);
    srcStride = GfxSurfaceStride(surface);
    src = GfxSurfacePixelsConst(surface);
    if (!src || sw == 0u || sh == 0u) {
        return RoboInvalidArgument;
    }
    if (sfmt != RoboGfxFormatBgra8 && sfmt != RoboGfxFormatRgba8 &&
        sfmt != RoboGfxFormatRgb8) {
        return RoboNotSupported;
    }
    if (GfxPixelFormatBytesPerPixel(sfmt, &sbpp) != RoboOk ||
        sbpp == 0u) {
        return RoboNotSupported;
    }
    if (srcStride < (size_t)sw * (size_t)sbpp) {
        return RoboInvalidArgument;
    }

    copyW = sw;
    copyH = sh;
    if (copyW > (uint32_t)slot->presentW) {
        copyW = (uint32_t)slot->presentW;
    }
    if (copyH > (uint32_t)slot->presentH) {
        copyH = (uint32_t)slot->presentH;
    }
    dstStrideBytes = (size_t)slot->presentW * 4u;
    /* Clear staging so uncovered regions are black after shrink. */
    memset(slot->presentBits, 0,
           (size_t)slot->presentW * (size_t)slot->presentH * 4u);

    for (y = 0u; y < copyH; y++) {
        const uint8_t *row = src + (size_t)y * srcStride;
        uint32_t x;
        dst = (uint32_t *)(slot->presentBits + (size_t)y * dstStrideBytes);
        for (x = 0u; x < copyW; x++) {
            dst[x] = ConvertToBgrx(sfmt, row + (size_t)x * sbpp);
        }
    }
    return RoboOk;
}

static void Win32PaintSlot(Win32Slot *slot, HWND hwnd)
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
    if (slot && slot->presentDc && slot->presentDib && slot->presentW > 0 &&
        slot->presentH > 0) {
        /* Stretch if client size differs from last present buffer. */
        if (cw == slot->presentW && ch == slot->presentH) {
            (void)BitBlt(hdc, 0, 0, cw, ch, slot->presentDc, 0, 0, SRCCOPY);
        } else {
            (void)SetStretchBltMode(hdc, COLORONCOLOR);
            (void)StretchBlt(hdc, 0, 0, cw, ch, slot->presentDc, 0, 0,
                             slot->presentW, slot->presentH, SRCCOPY);
        }
    } else {
        HBRUSH br = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(hdc, &cr, br);
    }
    EndPaint(hwnd, &ps);
}

static DWORD WindowStyle(const WindowConfig *cfg)
{
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (cfg && !cfg->resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    return style;
}

static LRESULT CALLBACK Win32Wndproc(HWND hwnd, UINT msg, WPARAM wparam,
                                           LPARAM lparam)
{
    Win32Slot *slot =
        (Win32Slot *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_GETMINMAXINFO:
        if (slot) {
            MINMAXINFO *mmi = (MINMAXINFO *)lparam;
            int32_t ow, oh;
            DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
            DWORD ex = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (slot->minW > 0 && slot->minH > 0) {
                ClientToOuter(style, ex, slot->minW, slot->minH, &ow, &oh);
                mmi->ptMinTrackSize.x = (LONG)ow;
                mmi->ptMinTrackSize.y = (LONG)oh;
            }
            if (slot->maxW > 0 && slot->maxH > 0) {
                ClientToOuter(style, ex, slot->maxW, slot->maxH, &ow, &oh);
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
        if (slot && slot->windowId != RoboWindowIdNone) {
            (void)WindowObserveCloseRequest(slot->windowId);
        }
        return 0;
    case WM_ERASEBKGND:
        /* Avoid flicker; WM_PAINT paints the full client. */
        return 1;
    case WM_PAINT:
        Win32PaintSlot(slot, hwnd);
        return 0;
    case WM_DPICHANGED:
        /* Scale is read in QueryState; invalidate so content redraws. */
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_SIZE:
        if (slot && slot->windowId != RoboWindowIdNone) {
            if (wparam != SIZE_MINIMIZED) {
                int32_t cw = (int32_t)LOWORD(lparam);
                int32_t ch = (int32_t)HIWORD(lparam);
                if (cw > 0 && ch > 0) {
                    (void)WindowObserveSize(slot->windowId, cw, ch);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        break;
    case WM_MOVE:
        if (slot && slot->windowId != RoboWindowIdNone) {
            int32_t x = (int32_t)(short)LOWORD(lparam);
            int32_t y = (int32_t)(short)HIWORD(lparam);
            (void)WindowObservePosition(slot->windowId, x, y);
        }
        break;
    case WM_SHOWWINDOW:
        if (slot && slot->windowId != RoboWindowIdNone) {
            (void)WindowObserveVisibility(slot->windowId,
                                                 wparam ? 1 : 0);
        }
        break;
    case WM_SETFOCUS:
        if (slot && slot->windowId != RoboWindowIdNone) {
            (void)WindowObserveFocus(slot->windowId, 1);
        }
        break;
    case WM_KILLFOCUS:
        if (slot && slot->windowId != RoboWindowIdNone) {
            (void)WindowObserveFocus(slot->windowId, 0);
        }
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (slot && slot->windowId != RoboWindowIdNone) {
            (void)KeyboardWin32HandleMessage(
                slot->windowId, msg, (uintptr_t)wparam, (intptr_t)lparam);
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
        if (slot && slot->windowId != RoboWindowIdNone) {
            (void)MouseWin32HandleMessage(
                slot->windowId, (void *)hwnd, msg, (uintptr_t)wparam,
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

static Result EnsureClass(void)
{
    WNDCLASSEXW wc;
    if (gClassRegistered) {
        return RoboOk;
    }
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Win32Wndproc;
    wc.hInstance = gInstance;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512)); /* IDC_ARROW */
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = RoboWin32ClassName;
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            LogError("robo_window.win32: RegisterClassExW failed err=%lu",
                           (unsigned long)err);
            return RoboIoError;
        }
    }
    gClassRegistered = 1;
    return RoboOk;
}

static void DestroyHwnd(Win32Slot *slot)
{
    if (!slot || !slot->hwnd) {
        return;
    }
    Win32PresentRelease(slot);
    DestroyWindow(slot->hwnd);
    slot->hwnd = NULL;
}

static void ClearSlot(Win32Slot *slot)
{
    if (!slot) {
        return;
    }
    Win32PresentRelease(slot);
    memset(slot, 0, sizeof(*slot));
}

static Result Win32Initialize(void *userdata)
{
    (void)userdata;
    if (gLive) {
        return RoboBusy;
    }
    gInstance = GetModuleHandleW(NULL);
    if (!gInstance) {
        LogError("robo_window.win32: GetModuleHandleW failed");
        return RoboIoError;
    }
    if (EnsureClass() != RoboOk) {
        return RoboIoError;
    }
    memset(gSlots, 0, sizeof(gSlots));
    gInitTid = GetCurrentThreadId();
    gLive = 1;
    LogInfo("robo_window.win32: backend initialize tid=%lu",
                  (unsigned long)gInitTid);
    return RoboOk;
}

static void Win32Shutdown(void *userdata)
{
    size_t i;
    (void)userdata;
    if (!gLive) {
        return;
    }
    for (i = 0u; i < RoboWin32MaxWindows; i++) {
        if (gSlots[i].inUse) {
            DestroyHwnd(&gSlots[i]);
            ClearSlot(&gSlots[i]);
        }
    }
    gLive = 0;
    LogInfo("robo_window.win32: backend shutdown");
}

static Result Win32CreateWindow(void *userdata,
                                         WindowId id,
                                         const WindowConfig *cfg,
                                         WindowNativeHandle *outNative)
{
    size_t i;
    Win32Slot *slot = NULL;
    DWORD style;
    DWORD ex = 0;
    int32_t ow, oh;
    int x, y;
    WCHAR titleW[RoboWin32TitleWchars];
    HWND hwnd;
    Result r;
    uint32_t gen;

    (void)userdata;
    if (!gLive || !cfg || !outNative) {
        return RoboInvalidArgument;
    }
    if (GetCurrentThreadId() != gInitTid) {
        LogWarn("robo_window.win32: create on non-init thread");
        return RoboPermissionDenied;
    }
    if (cfg->fullscreen) {
        LogWarn("robo_window.win32: fullscreen deferred (not applied)");
    }
    if (cfg->scalePreference > 0.0f &&
        DisplayScaleValidate(cfg->scalePreference) == RoboOk &&
        cfg->scalePreference != 1.0f) {
        LogDebug(
            "robo_window.win32: scale_preference=%g noted (HWND size is logical)",
            (double)cfg->scalePreference);
    }

    for (i = 0u; i < RoboWin32MaxWindows; i++) {
        if (!gSlots[i].inUse) {
            slot = &gSlots[i];
            break;
        }
    }
    if (!slot) {
        return RoboBusy;
    }

    r = Utf8ToWide(cfg->title, titleW, RoboWin32TitleWchars);
    if (r != RoboOk) {
        return r;
    }

    style = WindowStyle(cfg);
    ClientToOuter(style, ex, cfg->width, cfg->height, &ow, &oh);
    if (cfg->x == RoboWindowCoordUnknown || cfg->y == RoboWindowCoordUnknown) {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
    } else {
        x = (int)cfg->x;
        y = (int)cfg->y;
    }

    hwnd = CreateWindowExW(ex, RoboWin32ClassName, titleW, style, x, y, (int)ow,
                           (int)oh, NULL, NULL, gInstance, NULL);
    if (!hwnd) {
        LogError("robo_window.win32: CreateWindowExW failed err=%lu",
                       (unsigned long)GetLastError());
        return RoboIoError;
    }

    gen = gNextGeneration++;
    if (gen == 0u) {
        gen = gNextGeneration++;
    }

    slot->inUse = 1;
    slot->windowId = id;
    slot->hwnd = hwnd;
    slot->generation = gen;
    slot->createTid = GetCurrentThreadId();
    slot->minW = cfg->minWidth;
    slot->minH = cfg->minHeight;
    slot->maxW = cfg->maxWidth;
    slot->maxH = cfg->maxHeight;
    slot->resizable = cfg->resizable ? 1 : 0;

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)slot);

    r = WindowNativeHandleAssign(outNative, (uint64_t)(i + 1u), gen);
    if (r != RoboOk) {
        DestroyHwnd(slot);
        ClearSlot(slot);
        return r;
    }

    LogDebug("robo_window.win32: created hwnd=%p id=%llu token=%llu gen=%u",
                   (void *)hwnd, (unsigned long long)id,
                   (unsigned long long)(i + 1u), (unsigned)gen);
    return RoboOk;
}

static Result Win32DestroyWindow(void *userdata,
                                          WindowId id,
                                          WindowNativeHandle *native)
{
    Win32Slot *slot;
    Result r;
    (void)userdata;
    (void)id;
    if (!gLive) {
        return RoboInitError;
    }
    slot = SlotFromHandle(native);
    if (!slot) {
        if (native) {
            WindowNativeHandleInvalidate(native);
        }
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    DestroyHwnd(slot);
    ClearSlot(slot);
    if (native) {
        WindowNativeHandleInvalidate(native);
    }
    return RoboOk;
}

static Result Win32Show(void *userdata,
                                WindowId id,
                                WindowNativeHandle *native)
{
    Win32Slot *slot;
    Result r;
    (void)userdata;
    (void)id;
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    if (!ShowWindow(slot->hwnd, SW_SHOWNOACTIVATE)) {
        /* Already visible is OK; ShowWindow returns previous visibility. */
    }
    UpdateWindow(slot->hwnd);
    return RoboOk;
}

static Result Win32Hide(void *userdata,
                                WindowId id,
                                WindowNativeHandle *native)
{
    Win32Slot *slot;
    Result r;
    (void)userdata;
    (void)id;
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    ShowWindow(slot->hwnd, SW_HIDE);
    return RoboOk;
}

static Result Win32SetTitle(void *userdata,
                                     WindowId id,
                                     WindowNativeHandle *native,
                                     const char *utf8Title)
{
    Win32Slot *slot;
    WCHAR titleW[RoboWin32TitleWchars];
    Result r;
    (void)userdata;
    (void)id;
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    r = Utf8ToWide(utf8Title, titleW, RoboWin32TitleWchars);
    if (r != RoboOk) {
        return r;
    }
    if (!SetWindowTextW(slot->hwnd, titleW)) {
        LogError("robo_window.win32: SetWindowTextW failed err=%lu",
                       (unsigned long)GetLastError());
        return RoboIoError;
    }
    return RoboOk;
}

static Result Win32SetSize(void *userdata,
                                    WindowId id,
                                    WindowNativeHandle *native,
                                    int32_t width,
                                    int32_t height)
{
    Win32Slot *slot;
    DWORD style, ex;
    int32_t ow, oh;
    Result r;
    (void)userdata;
    (void)id;
    if (width <= 0 || height <= 0 || width > RoboWindowMaxDim ||
        height > RoboWindowMaxDim) {
        return RoboInvalidArgument;
    }
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    if (slot->minW > 0 && width < slot->minW) {
        return RoboInvalidArgument;
    }
    if (slot->minH > 0 && height < slot->minH) {
        return RoboInvalidArgument;
    }
    if (slot->maxW > 0 && width > slot->maxW) {
        return RoboInvalidArgument;
    }
    if (slot->maxH > 0 && height > slot->maxH) {
        return RoboInvalidArgument;
    }
    style = (DWORD)GetWindowLongPtrW(slot->hwnd, GWL_STYLE);
    ex = (DWORD)GetWindowLongPtrW(slot->hwnd, GWL_EXSTYLE);
    ClientToOuter(style, ex, width, height, &ow, &oh);
    if (!SetWindowPos(slot->hwnd, NULL, 0, 0, (int)ow, (int)oh,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        LogError("robo_window.win32: SetWindowPos(size) failed err=%lu",
                       (unsigned long)GetLastError());
        return RoboIoError;
    }
    return RoboOk;
}

static Result Win32SetSizeConstraints(void *userdata,
                                                WindowId id,
                                                WindowNativeHandle *native,
                                                int32_t minWidth,
                                                int32_t minHeight,
                                                int32_t maxWidth,
                                                int32_t maxHeight)
{
    Win32Slot *slot;
    Result r;
    (void)userdata;
    (void)id;
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    if (minWidth < 0 || minHeight < 0 || maxWidth < 0 || maxHeight < 0) {
        return RoboInvalidArgument;
    }
    if (minWidth > 0 && maxWidth > 0 && minWidth > maxWidth) {
        return RoboInvalidArgument;
    }
    if (minHeight > 0 && maxHeight > 0 && minHeight > maxHeight) {
        return RoboInvalidArgument;
    }
    slot->minW = minWidth;
    slot->minH = minHeight;
    slot->maxW = maxWidth;
    slot->maxH = maxHeight;
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
    LogDebug("robo_window.win32: constraints min=%dx%d max=%dx%d",
                   (int)minWidth, (int)minHeight, (int)maxWidth,
                   (int)maxHeight);
    return RoboOk;
}

static Result Win32SetPosition(void *userdata,
                                        WindowId id,
                                        WindowNativeHandle *native,
                                        int32_t x,
                                        int32_t y)
{
    Win32Slot *slot;
    Result r;
    (void)userdata;
    (void)id;
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    if (!SetWindowPos(slot->hwnd, NULL, (int)x, (int)y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        LogError("robo_window.win32: SetWindowPos(move) failed err=%lu",
                       (unsigned long)GetLastError());
        return RoboIoError;
    }
    return RoboOk;
}

static Result Win32Focus(void *userdata,
                                 WindowId id,
                                 WindowNativeHandle *native)
{
    Win32Slot *slot;
    Result r;
    (void)userdata;
    (void)id;
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }
    if (!IsWindowVisible(slot->hwnd)) {
        return RoboNotSupported;
    }
    if (!SetForegroundWindow(slot->hwnd)) {
        /* Common when OS blocks foreground steals — do not fake success. */
        LogDebug("robo_window.win32: SetForegroundWindow declined err=%lu",
                       (unsigned long)GetLastError());
        return RoboNotSupported;
    }
    SetFocus(slot->hwnd);
    return RoboOk;
}

static Result Win32QueryState(void *userdata,
                                       WindowId id,
                                       const WindowNativeHandle *native,
                                       WindowState *inoutState)
{
    Win32Slot *slot;
    RECT wr, cr;
    (void)userdata;
    (void)id;
    if (!inoutState) {
        return RoboInvalidArgument;
    }
    slot = SlotFromHandle(native);
    if (!slot) {
        return RoboInvalidArgument;
    }
    if (!GetWindowRect(slot->hwnd, &wr) || !GetClientRect(slot->hwnd, &cr)) {
        return RoboIoError;
    }
    inoutState->x = (int32_t)wr.left;
    inoutState->y = (int32_t)wr.top;
    inoutState->width = (int32_t)(cr.right - cr.left);
    inoutState->height = (int32_t)(cr.bottom - cr.top);
    inoutState->visible = IsWindowVisible(slot->hwnd) ? 1 : 0;
    inoutState->focused = (GetForegroundWindow() == slot->hwnd) ? 1 : 0;
    inoutState->minimized = IsIconic(slot->hwnd) ? 1 : 0;
    inoutState->maximized = IsZoomed(slot->hwnd) ? 1 : 0;
    /*
     * Goal 7.8: expose effective DPI/scale when the OS provides it.
     * GetDpiForWindow (Win10 1607+) — unavailable → leave scale unchanged.
     * Do not fabricate DPI. Full display enumeration remains later.
     */
    {
        typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
        static GetDpiForWindowFn SGetDpi;
        static int sDpiResolved;
        if (!sDpiResolved) {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) {
                SGetDpi = (GetDpiForWindowFn)GetProcAddress(
                    user32, "GetDpiForWindow");
            }
            sDpiResolved = 1;
        }
        if (SGetDpi) {
            UINT dpi = SGetDpi(slot->hwnd);
            if (dpi > 0u) {
                float scale = (float)dpi / RoboDisplayDpiReference;
                if (DisplayScaleValidate(scale) == RoboOk) {
                    inoutState->scale = scale;
                }
            }
        }
    }
    /* Display association via nearest monitor when host display backend is live. */
    {
        HMONITOR mon = MonitorFromWindow(slot->hwnd, MONITOR_DEFAULTTONEAREST);
        DisplayId did = DisplayWin32IdFromHmonitor(mon);
        if (did != RoboDisplayIdNone) {
            inoutState->displayId = did;
        }
    }
    return RoboOk;
}

static Result Win32PresentSurface(void *userdata,
                                           WindowId id,
                                           WindowNativeHandle *native,
                                           GfxSurface *surface)
{
    Win32Slot *slot;
    Result r;
    uint32_t sw;
    uint32_t sh;
    RECT cr;
    int32_t clientW;
    int32_t clientH;
    int32_t bufW;
    int32_t bufH;

    (void)userdata;
    (void)id;
    if (!gLive) {
        return RoboInitError;
    }
    if (!surface) {
        return RoboInvalidArgument;
    }
    slot = SlotFromHandle(native);
    if (!slot || !slot->hwnd) {
        return RoboNotFound;
    }
    r = RequireCreateThread(slot);
    if (r != RoboOk) {
        return r;
    }

    sw = GfxSurfaceWidth(surface);
    sh = GfxSurfaceHeight(surface);
    if (sw == 0u || sh == 0u) {
        return RoboInvalidArgument;
    }

    clientW = (int32_t)sw;
    clientH = (int32_t)sh;
    if (GetClientRect(slot->hwnd, &cr)) {
        int32_t cw = (int32_t)(cr.right - cr.left);
        int32_t ch = (int32_t)(cr.bottom - cr.top);
        if (cw > 0 && ch > 0) {
            clientW = cw;
            clientH = ch;
        }
    }
    /* Prefer client size for staging so WM_PAINT 1:1 BitBlt is common. */
    bufW = clientW;
    bufH = clientH;
    if ((uint32_t)bufW < sw) {
        bufW = (int32_t)sw;
    }
    if ((uint32_t)bufH < sh) {
        bufH = (int32_t)sh;
    }

    r = Win32PresentEnsure(slot, bufW, bufH);
    if (r != RoboOk) {
        return r;
    }
    r = Win32BlitSurface(slot, surface);
    if (r != RoboOk) {
        return r;
    }

    InvalidateRect(slot->hwnd, NULL, FALSE);
    /* Synchronous paint so tests can sample pixels immediately. */
    if (!UpdateWindow(slot->hwnd)) {
        LogWarn("robo_window.win32: UpdateWindow failed err=%lu",
                      (unsigned long)GetLastError());
    }
    LogDebug("robo_window.win32: present id=%llu surface=%ux%u dib=%dx%d",
                   (unsigned long long)slot->windowId, (unsigned)sw,
                   (unsigned)sh, (int)slot->presentW, (int)slot->presentH);
    return RoboOk;
}

static Result Win32ProcessEvents(void *userdata)
{
    MSG msg;
    int n = 0;
    (void)userdata;
    if (!gLive) {
        return RoboInitError;
    }
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (++n > 256) {
            break;
        }
    }
    return RoboOk;
}

static Result Win32WaitEvents(void *userdata, uint32_t timeoutMs)
{
    DWORD waitMs;
    (void)userdata;
    if (!gLive) {
        return RoboInitError;
    }
    if (timeoutMs == RoboWindowEventWaitForever) {
        waitMs = INFINITE;
    } else {
        waitMs = (DWORD)timeoutMs;
    }
    /*
     * Block until input arrives, PostThreadMessage wakeup, or timeout.
     * Does not drain the queue — caller runs ProcessEvents next.
     */
    (void)MsgWaitForMultipleObjects(0, NULL, FALSE, waitMs, QS_ALLINPUT);
    return RoboOk;
}

static Result Win32Wakeup(void *userdata)
{
    (void)userdata;
    if (!gLive) {
        return RoboInitError;
    }
    if (gInitTid == 0u) {
        return RoboInitError;
    }
    if (!PostThreadMessageW(gInitTid, WM_NULL, 0, 0)) {
        DWORD err = GetLastError();
        /* No message queue yet is benign for early wake. */
        if (err != ERROR_INVALID_THREAD_ID) {
            LogDebug("robo_window.win32: wakeup PostThreadMessage err=%lu",
                           (unsigned long)err);
        }
    }
    return RoboOk;
}

static Result Win32CaptureMouse(void *userdata, WindowId id,
                                         WindowNativeHandle *native)
{
    Win32Slot *slot;
    (void)userdata;
    (void)id;
    if (!gLive) {
        return RoboInitError;
    }
    slot = SlotFromHandle(native);
    if (!slot || !slot->hwnd) {
        return RoboNotFound;
    }
    if (SetCapture(slot->hwnd) == NULL && GetLastError() != 0) {
        /* SetCapture returns previous capture HWND; NULL is often OK. */
    }
    if (GetCapture() != slot->hwnd) {
        LogWarn("robo_window.win32: SetCapture failed for id=%llu",
                      (unsigned long long)id);
        return RoboIoError;
    }
    return RoboOk;
}

static Result Win32ReleaseMouseCapture(void *userdata)
{
    (void)userdata;
    if (GetCapture() != NULL) {
        (void)ReleaseCapture();
    }
    return RoboOk;
}

static const WindowBackendVtbl gWin32Vtbl = {
    "win32",
    &gCaps,
    Win32Initialize,
    Win32Shutdown,
    Win32CreateWindow,
    Win32DestroyWindow,
    Win32Show,
    Win32Hide,
    Win32SetTitle,
    Win32SetSize,
    Win32SetSizeConstraints,
    Win32SetPosition,
    Win32Focus,
    Win32QueryState,
    Win32PresentSurface,
    Win32ProcessEvents,
    Win32WaitEvents,
    Win32Wakeup,
    Win32CaptureMouse,
    Win32ReleaseMouseCapture
};

static void CapsInit(void)
{
    memset(&gCaps, 0, sizeof(gCaps));
    gCaps.RoboCreateWindow = 1;
    gCaps.RoboDestroyWindow = 1;
    gCaps.showHide = 1;
    gCaps.resize = 1;
    gCaps.move = 1;
    gCaps.SetTitle = 1;
    gCaps.focus = 1;
    gCaps.QueryState = 1;
    gCaps.present = 1;
    gCaps.ProcessEvents = 1;
    gCaps.reportsActualState = 1;
    gCaps.sizeConstraints = 1;
    gCaps.presentFormatMask = RoboWindowPresentFmtCpuDefault;
    gCaps.presentMaxWidth = RoboWin32PresentMax;
    gCaps.presentMaxHeight = RoboWin32PresentMax;
    gCaps.WaitEvents = 1;
    gCaps.wakeup = 1;
    gCaps.mouseCapture = 1;
}

int WindowBackendHostAvailable(void)
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

const WindowBackendVtbl *WindowBackendHostVtbl(void)
{
    CapsInit();
    return &gWin32Vtbl;
}

Result WindowBackendUseHost(void)
{
    Result r;
    if (!WindowBackendHostAvailable()) {
        return RoboNotSupported;
    }
    CapsInit();
    r = WindowBackendRegister(&gWin32Vtbl, NULL);
    if (r != RoboOk) {
        return r;
    }
    /* Prefer real monitor enum when display manager is already live. */
    if (DisplayIsInitialized() && DisplayBackendHostAvailable()) {
        if (DisplayBackendUseHost() == RoboOk) {
            (void)DisplayRefresh();
        }
    }
    return RoboOk;
}

#endif /* RoboWindowHasWin32 */
