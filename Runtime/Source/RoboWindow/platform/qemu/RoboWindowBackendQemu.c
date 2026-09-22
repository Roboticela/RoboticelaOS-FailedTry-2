/*
 * RoboWindow QEMU backend (Goal 7.12).
 *
 * Guest: SYS_DISPLAY claim/info/present/input (UEFI GOP via kernel).
 * Host sim: blit into robo_qemu_sim framebuffer.
 */

#include "RoboWindow/RoboWindowBackendQemu.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendQemu.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindowPresent.h"
#include "RoboWindow/RoboWindowState.h"

#include <string.h>

#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
#include <robosys/display.h>
#include <robosys/input.h>
#include <robosys/syscall.h>
#include <robosys/syscallNumbers.h>
#endif

#define RoboQemuWinToken 1u
#define RoboQemuWinGen 1u

typedef struct qemuWin {
    int live;
    int claimed;
    WindowId windowId;
    int32_t width;
    int32_t height;
} QemuWin;

static QemuWin gWin;
static WindowBackendCaps gCaps;
static int gGuestMode;

#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
static int SysIsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static Result GuestClaim(void)
{
    Long r;
    RoboUserDisplay info;
    QemuFbInfo fb;

    r = Display((int)RoboDispClaim, 0, 0, 0,
                     (int)(RoboDispRoleOutput | RoboDispRoleInput));
    if (SysIsErr(r)) {
        LogError("robo_window.qemu: SYS_DISPLAY CLAIM failed");
        return RoboIoError;
    }
    memset(&info, 0, sizeof(info));
    r = Display((int)RoboDispInfo, 0, &info, sizeof(info), 0);
    if (SysIsErr(r)) {
        (void)Display((int)RoboDispRelease, 0, 0, 0, 0);
        return RoboIoError;
    }
    QemuFbInfoInit(&fb);
    if (!info.ready || !info.width || !info.height) {
        fb.headless = 1;
        fb.ready = 0;
        (void)QemuFbSetCurrent(&fb);
        gWin.claimed = 1;
        LogWarn("robo_window.qemu: headless display claim");
        return RoboOk;
    }
    fb.width = info.width;
    fb.height = info.height;
    fb.pitchBytes = info.pitch ? info.pitch : info.width * 4u;
    fb.bpp = info.bpp ? info.bpp : 32u;
    fb.pixfmt = info.pixfmt;
    fb.pixels = NULL; /* present via syscall */
    fb.byteSize = 0u;
    fb.ready = 1;
    fb.headless = 0;
    if (QemuFbSetCurrent(&fb) != RoboOk) {
        (void)Display((int)RoboDispRelease, 0, 0, 0, 0);
        return RoboInvalidArgument;
    }
    gWin.claimed = 1;
    LogInfo("robo_window.qemu: claimed %ux%u pitch=%u pixfmt=%u",
                  (unsigned)fb.width, (unsigned)fb.height,
                  (unsigned)fb.pitchBytes, (unsigned)fb.pixfmt);
    return RoboOk;
}

static void GuestRelease(void)
{
    if (gWin.claimed) {
        (void)Display((int)RoboDispRelease, 0, 0, 0, 0);
        gWin.claimed = 0;
    }
}

static Result GuestPresent(const GfxSurface *surface)
{
    RoboDisplayPresent req;
    const uint8_t *px;
    Long r;
    const QemuFbInfo *fb = QemuFbCurrent();

    if (!fb || fb->headless || !fb->ready) {
        return RoboOk; /* headless accept */
    }
    if (!surface) {
        return RoboInvalidArgument;
    }
    px = GfxSurfacePixelsConst(surface);
    if (!px) {
        return RoboInvalidArgument;
    }
    memset(&req, 0, sizeof(req));
    req.pixels = (uint64_t)(uintptr_t)px;
    req.width = GfxSurfaceWidth(surface);
    req.height = GfxSurfaceHeight(surface);
    {
        /* Kernel SYS_DISPLAY PRESENT: stride is in pixels (32 bpp rows). */
        size_t strideBytes = GfxSurfaceStride(surface);
        uint8_t bpp = 4u;
        (void)GfxPixelFormatBytesPerPixel(
            GfxSurfaceFormat(surface), &bpp);
        if (bpp == 0u) {
            bpp = 4u;
        }
        req.stride = (uint32_t)(strideBytes / (size_t)bpp);
        if (req.stride < req.width) {
            req.stride = req.width;
        }
    }
    if (QemuFbPixfmtFromGfx(GfxSurfaceFormat(surface),
                                     &req.pixfmt) != RoboOk) {
        req.pixfmt = fb->pixfmt;
    }
    req.destX = 0;
    req.destY = 0;
    r = Display((int)RoboDispPresent, 0, &req, sizeof(req), 0);
    if (SysIsErr(r)) {
        LogWarn("robo_window.qemu: PRESENT failed");
        return RoboIoError;
    }
    return RoboOk;
}
#endif /* RoboWindowHasQemu */

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
    gCaps.presentMaxWidth = 8192u;
    gCaps.presentMaxHeight = 8192u;
#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
    gCaps.WaitEvents = 1;
#else
    gCaps.WaitEvents = 0;
#endif
    gCaps.wakeup = 0;
    gCaps.mouseCapture = 0;
}

static Result QemuInitialize(void *ud)
{
    (void)ud;
    memset(&gWin, 0, sizeof(gWin));
    CapsInit();
#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
    gGuestMode = 1;
    return GuestClaim();
#else
    gGuestMode = 0;
    if (!QemuFbCurrent() || !QemuFbCurrent()->ready) {
        LogError("robo_window.qemu: no framebuffer (call sim_init first)");
        return RoboInitError;
    }
    return RoboOk;
#endif
}

static void QemuShutdown(void *ud)
{
    (void)ud;
#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
    GuestRelease();
#endif
    memset(&gWin, 0, sizeof(gWin));
    if (!QemuSimIsActive()) {
        QemuFbClearCurrent();
    }
}

static Result QemuCreateWindow(void *ud, WindowId id,
                                        const WindowConfig *cfg,
                                        WindowNativeHandle *out)
{
    const QemuFbInfo *fb = QemuFbCurrent();
    (void)ud;
    (void)cfg;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (gWin.live) {
        return RoboBusy;
    }
    if (!fb || (!fb->ready && !fb->headless)) {
        return RoboNotFound;
    }
    gWin.live = 1;
    gWin.windowId = id;
    if (fb->ready) {
        gWin.width = (int32_t)fb->width;
        gWin.height = (int32_t)fb->height;
    } else {
        gWin.width = cfg && cfg->width > 0 ? cfg->width : 640;
        gWin.height = cfg && cfg->height > 0 ? cfg->height : 480;
    }
    return WindowNativeHandleAssign(out, RoboQemuWinToken,
                                            RoboQemuWinGen);
}

static Result QemuDestroyWindow(void *ud, WindowId id,
                                         WindowNativeHandle *native)
{
    (void)ud;
    if (gWin.live && gWin.windowId == id) {
        gWin.live = 0;
        gWin.windowId = RoboWindowIdNone;
    }
    if (native) {
        WindowNativeHandleInvalidate(native);
    }
    return RoboOk;
}

static Result QemuOk(void *ud, WindowId id,
                             WindowNativeHandle *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return RoboOk;
}

static Result QemuTitle(void *ud, WindowId id,
                                WindowNativeHandle *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return RoboOk;
}

static Result QemuSize(void *ud, WindowId id,
                               WindowNativeHandle *n, int32_t w,
                               int32_t h)
{
    (void)ud;
    (void)n;
    if (!gWin.live || gWin.windowId != id) {
        return RoboNotFound;
    }
    /* Fixed FB: keep actual size; note request for observe path. */
    (void)w;
    (void)h;
    return RoboOk;
}

static Result QemuCons(void *ud, WindowId id,
                               WindowNativeHandle *n, int32_t a,
                               int32_t b, int32_t c, int32_t d)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return RoboOk;
}

static Result QemuPos(void *ud, WindowId id,
                              WindowNativeHandle *n, int32_t x,
                              int32_t y)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result QemuQuery(void *ud, WindowId id,
                                const WindowNativeHandle *n,
                                WindowState *st)
{
    (void)ud;
    (void)n;
    if (!st || !gWin.live || gWin.windowId != id) {
        return RoboNotFound;
    }
    st->width = gWin.width;
    st->height = gWin.height;
    st->x = 0;
    st->y = 0;
    st->visible = 1;
    st->focused = 1;
    st->scale = 1.0f;
    return RoboOk;
}

static Result QemuPresent(void *ud, WindowId id,
                                  WindowNativeHandle *n,
                                  GfxSurface *surface)
{
    const QemuFbInfo *fb;
    (void)ud;
    (void)n;
    if (!gWin.live || gWin.windowId != id) {
        return RoboNotFound;
    }
    if (!surface) {
        return RoboInvalidArgument;
    }
#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
    if (gGuestMode) {
        return GuestPresent(surface);
    }
#endif
    fb = QemuFbCurrent();
    if (!fb) {
        return RoboNotFound;
    }
    return QemuFbPresentSurface(fb, surface, 0, 0);
}

/* --- Input translation (Robosys HID → RoboKeyboard / RoboMouse) --- */

static KeyModifierFlags MapMods(uint32_t m)
{
    KeyModifierFlags out = RoboKeyModNone;
#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
    if (m & RoboModShift) {
        out |= RoboKeyModShift;
    }
    if (m & RoboModCtrl) {
        out |= RoboKeyModCtrl;
    }
    if (m & RoboModAlt) {
        out |= RoboKeyModAlt;
    }
    if (m & RoboModMeta) {
        out |= RoboKeyModMeta;
    }
#else
    /* Host inject uses the same bit layout as robosys/input.h. */
    if (m & 1u) {
        out |= RoboKeyModShift;
    }
    if (m & 2u) {
        out |= RoboKeyModCtrl;
    }
    if (m & 4u) {
        out |= RoboKeyModAlt;
    }
    if (m & 8u) {
        out |= RoboKeyModMeta;
    }
#endif
    return out;
}

static void MapHidKey(uint32_t code, PhysicalKey *phys,
                        LogicalKey *log)
{
    *phys = RoboPhysicalKeyUnknown;
    *log = RoboLogicalKeyUnknown;
    /* HID usage → Robo keys (subset; unknown codes stay UNKNOWN). */
    if (code >= 0x04u && code <= 0x1Du) {
        /* A..Z */
        unsigned i = (unsigned)(code - 0x04u);
        *phys = (PhysicalKey)(RoboPhysicalKeyA + (int)i);
        *log = (LogicalKey)(RoboLogicalKeyA + (int)i);
        return;
    }
    if (code >= 0x1Eu && code <= 0x26u) {
        unsigned i = (unsigned)(code - 0x1Eu);
        *phys = (PhysicalKey)(RoboPhysicalKeyDigit1 + (int)i);
        *log = (LogicalKey)(RoboLogicalKeyDigit1 + (int)i);
        return;
    }
    if (code == 0x27u) {
        *phys = RoboPhysicalKeyDigit0;
        *log = RoboLogicalKeyDigit0;
        return;
    }
    switch (code) {
    case 0x28u:
        *phys = RoboPhysicalKeyEnter;
        *log = RoboLogicalKeyEnter;
        break;
    case 0x29u:
        *phys = RoboPhysicalKeyEscape;
        *log = RoboLogicalKeyEscape;
        break;
    case 0x2Au:
        *phys = RoboPhysicalKeyBackspace;
        *log = RoboLogicalKeyBackspace;
        break;
    case 0x2Bu:
        *phys = RoboPhysicalKeyTab;
        *log = RoboLogicalKeyTab;
        break;
    case 0x2Cu:
        *phys = RoboPhysicalKeySpace;
        *log = RoboLogicalKeySpace;
        break;
    case 0x4Fu:
        *phys = RoboPhysicalKeyArrowRight;
        *log = RoboLogicalKeyArrowRight;
        break;
    case 0x50u:
        *phys = RoboPhysicalKeyArrowLeft;
        *log = RoboLogicalKeyArrowLeft;
        break;
    case 0x51u:
        *phys = RoboPhysicalKeyArrowDown;
        *log = RoboLogicalKeyArrowDown;
        break;
    case 0x52u:
        *phys = RoboPhysicalKeyArrowUp;
        *log = RoboLogicalKeyArrowUp;
        break;
    default:
        break;
    }
}

static int32_t gPtrX;
static int32_t gPtrY;
static int gPtrReady;

static void PointerBounds(int32_t *maxX, int32_t *maxY)
{
    const QemuFbInfo *fb = QemuFbCurrent();

    *maxX = 1279;
    *maxY = 799;
    if (fb && fb->ready && fb->width > 0u && fb->height > 0u) {
        *maxX = (int32_t)fb->width - 1;
        *maxY = (int32_t)fb->height - 1;
    }
    if (*maxX < 0) {
        *maxX = 0;
    }
    if (*maxY < 0) {
        *maxY = 0;
    }
}

static void ClampPointer(void)
{
    int32_t maxX;
    int32_t maxY;

    PointerBounds(&maxX, &maxY);
    if (gPtrX < 0) {
        gPtrX = 0;
    }
    if (gPtrY < 0) {
        gPtrY = 0;
    }
    if (gPtrX > maxX) {
        gPtrX = maxX;
    }
    if (gPtrY > maxY) {
        gPtrY = maxY;
    }
}

/**
 * PS/2 reports relative motion with x=y=0. Injected/tablet events carry an absolute point.
 * The software cursor and hit-test both need a position on the screen, so this is where those
 * two shapes become one.
 */
static void ResolvePointer(int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    int32_t maxX;
    int32_t maxY;
    int first = !gPtrReady;

    PointerBounds(&maxX, &maxY);
    if (x != 0 || y != 0) {
        gPtrX = x;
        gPtrY = y;
        gPtrReady = 1;
    } else {
        if (!gPtrReady) {
            gPtrX = maxX / 2;
            gPtrY = maxY / 2;
            gPtrReady = 1;
        }
        gPtrX += dx;
        gPtrY += dy;
    }
    ClampPointer();
    if (first) {
        LogDebug("robo_window.qemu: pointer %d,%d", (int)gPtrX, (int)gPtrY);
    }
}

static Result TranslateInput(uint32_t type, uint32_t code,
                                     uint32_t value, int32_t x, int32_t y,
                                     int32_t dx, int32_t dy, uint32_t modifiers,
                                     uint32_t buttons)
{
    WindowId wid =
        gWin.live ? gWin.windowId : RoboWindowIdNone;
    KeyModifierFlags mods = MapMods(modifiers);

    (void)buttons;
    (void)value;

    /* Keyboard */
    if (type == 1u /* RoboKeyDown */ || type == 2u /* RoboKeyUp */) {
        KeyboardEvent ke;
        PhysicalKey phys;
        LogicalKey log;
        MapHidKey(code, &phys, &log);
        KeyboardEventInit(&ke);
        ke.action =
            (type == 1u) ? RoboKeyActionDown : RoboKeyActionUp;
        ke.physicalKey = phys;
        ke.logicalKey = log;
        ke.modifiers = mods;
        return WindowObserveKey(wid, &ke);
    }

    if (type == 3u || type == 4u) {
        ResolvePointer(x, y, dx, dy);
    } else if (type == 5u) {
        if (x != 0 || y != 0) {
            ResolvePointer(x, y, 0, 0);
        } else if (!gPtrReady) {
            ResolvePointer(0, 0, 0, 0);
        }
    }

    /* Mouse move */
    if (type == 3u) {
        MouseEvent me;
        MouseEventInit(&me);
        me.type = RoboMouseEventMove;
        me.clientX = gPtrX;
        me.clientY = gPtrY;
        me.deltaX = dx;
        me.deltaY = dy;
        me.modifiers = mods;
        return WindowObserveMouse(wid, &me);
    }

    /* Mouse button: code = button mask bit, value = 1 down / 0 up */
    if (type == 4u) {
        MouseEvent me;
        MouseButton btn = RoboMouseButtonUnknown;
        if (code & 1u) {
            btn = RoboMouseButtonLeft;
        } else if (code & 2u) {
            btn = RoboMouseButtonRight;
        } else if (code & 4u) {
            btn = RoboMouseButtonMiddle;
        }
        MouseEventInit(&me);
        me.type = value ? RoboMouseEventButtonDown
                        : RoboMouseEventButtonUp;
        me.button = btn;
        me.clientX = gPtrX;
        me.clientY = gPtrY;
        me.modifiers = mods;
        me.clickCount = value ? 1 : 0;
        return WindowObserveMouse(wid, &me);
    }

    /* Scroll: dy/dx in notches * 120 convention when value unused */
    if (type == 5u) {
        MouseEvent me;
        MouseEventInit(&me);
        me.type = RoboMouseEventWheel;
        me.clientX = gPtrX;
        me.clientY = gPtrY;
        me.scrollX = dx;
        me.scrollY = dy != 0 ? dy : (int32_t)value;
        me.scrollUnit = RoboMouseScrollUnitLine;
        me.modifiers = mods;
        return WindowObserveMouse(wid, &me);
    }

    return RoboOk;
}

Result QemuInputInjectRaw(uint32_t type, uint32_t code,
                                         uint32_t value, int32_t x, int32_t y,
                                         int32_t dx, int32_t dy,
                                         uint32_t modifiers, uint32_t buttons)
{
    return TranslateInput(type, code, value, x, y, dx, dy, modifiers, buttons);
}

Result QemuInputPoll(void)
{
#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
    int n = 0;
    if (!gWin.claimed) {
        return RoboOk;
    }
    for (;;) {
        RoboInputEvent ev;
        Long r;
        memset(&ev, 0, sizeof(ev));
        r = Display((int)RoboDispInput, 0, &ev, sizeof(ev),
                         (int)RoboDispNowait);
        if (SysIsErr(r)) {
            break;
        }
        (void)TranslateInput(ev.type, ev.code, ev.value, ev.x, ev.y, ev.dx,
                              ev.dy, ev.modifiers, ev.buttons);
        n++;
        if (n > 64) {
            break;
        }
    }
#else
    /* Host: nothing to poll unless tests inject. */
#endif
    return RoboOk;
}

static Result QemuWaitEvents(void *ud, uint32_t timeoutMs)
{
    (void)ud;
#if defined(RoboWindowHasQemu) && RoboWindowHasQemu
    if (timeoutMs == 0u) {
        return RoboOk;
    }
    if (timeoutMs == RoboWindowEventWaitForever) {
        timeoutMs = 16u;
    }
    (void)RoboSleep((Ulong)timeoutMs);
#else
    (void)timeoutMs;
#endif
    return RoboOk;
}

static Result QemuProcessEvents(void *ud)
{
    (void)ud;
    return QemuInputPoll();
}

static const WindowBackendVtbl gQemuVtbl = {
    "qemu",
    &gCaps,
    QemuInitialize,
    QemuShutdown,
    QemuCreateWindow,
    QemuDestroyWindow,
    QemuOk, /* show */
    QemuOk, /* hide */
    QemuTitle,
    QemuSize,
    QemuCons,
    QemuPos,
    QemuOk, /* focus */
    QemuQuery,
    QemuPresent,
    QemuProcessEvents,
    QemuWaitEvents,
    NULL,
    NULL,
    NULL
};

const WindowBackendVtbl *WindowBackendQemuVtbl(void)
{
    return &gQemuVtbl;
}

Result WindowBackendUseQemu(void)
{
    CapsInit();
    return WindowBackendRegister(&gQemuVtbl, NULL);
}

Result WindowBackendUseQemuSim(uint32_t width, uint32_t height,
                                               uint32_t pixfmt)
{
    Result r;
    r = QemuSimInit(width, height, pixfmt);
    if (r != RoboOk) {
        return r;
    }
    if (!DisplayIsInitialized()) {
        r = DisplayInit();
        if (r != RoboOk) {
            QemuSimShutdown();
            return r;
        }
    }
    r = DisplayBackendUseQemu();
    if (r != RoboOk) {
        QemuSimShutdown();
        return r;
    }
    (void)DisplayRefresh();
    CapsInit();
    gGuestMode = 0;
    return WindowBackendRegister(&gQemuVtbl, NULL);
}
