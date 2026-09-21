/*
 * QEMU display backend suite (Goal 7.12).
 * Host simulator — does not require a live QEMU process.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendQemu.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendQemu.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <string.h>

static void teardown(void)
{
    if (WindowIsInitialized()) {
        WindowShutdown();
    }
    if (DisplayIsInitialized()) {
        DisplayShutdown();
    }
    QemuSimShutdown();
    GraphicsShutdown();
}

static Result BootSim(uint32_t w, uint32_t h, uint32_t pixfmt)
{
    Result r;
    teardown();
    r = GraphicsInit();
    if (r != RoboOk) {
        return r;
    }
    r = DisplayInit();
    if (r != RoboOk) {
        return r;
    }
    r = WindowInit();
    if (r != RoboOk) {
        return r;
    }
    r = WindowBackendUseQemuSim(w, h, pixfmt);
    return r;
}

static void TestQemuFbValidate(void)
{
    QemuFbInfo info;
    uint8_t buf[64];

    QemuFbInfoInit(&info);
    RoboCheckEqInt(QemuFbInfoValidate(&info), RoboNotFound);

    info.ready = 1;
    info.width = 4;
    info.height = 4;
    info.pitchBytes = 16;
    info.bpp = 32;
    info.pixfmt = RoboQemuPixBgrx;
    info.pixels = buf;
    info.byteSize = sizeof(buf);
    RoboCheckEqInt(QemuFbInfoValidate(&info), RoboOk);

    info.pitchBytes = 8; /* too small */
    RoboCheckEqInt(QemuFbInfoValidate(&info), RoboInvalidArgument);

    info.pitchBytes = 16;
    info.bpp = 16;
    RoboCheckEqInt(QemuFbInfoValidate(&info), RoboNotSupported);

    info.bpp = 32;
    info.pixfmt = 99u;
    RoboCheckEqInt(QemuFbInfoValidate(&info), RoboNotSupported);

    info.headless = 1;
    info.ready = 0;
    RoboCheckEqInt(QemuFbInfoValidate(&info), RoboOk);
}

static void TestQemuSimPresentColors(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSurface *surf = NULL;
    GfxSurfaceDesc desc;
    const QemuFbInfo *fb;
    uint32_t px = 0u;
    DisplayInfo di;

    RoboCheckEqInt(BootSim(64, 48, RoboQemuPixBgrx), RoboOk);
    RoboCheck(QemuSimIsActive());
    fb = QemuFbCurrent();
    RoboCheck(fb && fb->ready && fb->pixels);
    RoboCheckEqInt((int)fb->width, 64);
    RoboCheckEqInt((int)fb->height, 48);

    RoboCheckEqInt(DisplayPrimary(&di), RoboOk);
    RoboCheckEqInt(di.resolutionWidthPx, 64);
    RoboCheckEqInt(di.resolutionHeightPx, 48);
    RoboCheck(di.scale == 1.0f);
    RoboCheck(di.dpiX == 0.0f);

    WindowConfigInit(&cfg);
    cfg.width = 64;
    cfg.height = 48;
    cfg.title = "qemu-sim";
    cfg.visible = 1;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    GfxSurfaceDescInit(&desc);
    desc.width = 64;
    desc.height = 48;
    desc.format = RoboGfxFormatBgra8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surf), RoboOk);

    (void)GfxSurfaceClear(surf, (GfxColorRgba8){0, 0, 0, 255});
    (void)GfxSurfaceSetPixel(surf, 0, 0,
                                     (GfxColorRgba8){255, 255, 255, 255});
    (void)GfxSurfaceSetPixel(surf, 63, 0,
                                     (GfxColorRgba8){255, 0, 0, 255});
    (void)GfxSurfaceSetPixel(surf, 0, 47,
                                     (GfxColorRgba8){0, 255, 0, 255});
    (void)GfxSurfaceSetPixel(surf, 63, 47,
                                     (GfxColorRgba8){0, 0, 255, 255});
    (void)GfxSurfaceFillRect(
        surf, MakeGfxRect(20, 20, 8, 8),
        (GfxColorRgba8){14, 122, 255, 255});

    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);

    /* PackBgrx → LE uint32 0xXXRRGGBB */
    RoboCheckEqInt(QemuFbGetPixel(fb, 0, 0, &px), RoboOk);
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0xFFFFFFu);
    RoboCheckEqInt(QemuFbGetPixel(fb, 63, 0, &px), RoboOk);
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0xFF0000); /* red */
    RoboCheckEqInt(QemuFbGetPixel(fb, 0, 47, &px), RoboOk);
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0x00FF00);
    RoboCheckEqInt(QemuFbGetPixel(fb, 63, 47, &px), RoboOk);
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0x0000FF); /* blue */
    RoboCheckEqInt(QemuFbGetPixel(fb, 20, 20, &px), RoboOk);
    /* brand #0E7AFF → 0x000E7AFF */
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0x0E7AFF);

    RoboCheckEqInt(QemuFbGetPixel(fb, 64, 0, &px),
                      RoboInvalidArgument);

    GfxSurfaceRelease(surf);
    (void)WindowDestroy(id);
    teardown();
}

static void TestQemuSimRgbxAndInput(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSurface *surf = NULL;
    GfxSurfaceDesc desc;
    const QemuFbInfo *fb;
    uint32_t px = 0u;
    WindowEvent ev;
    int sawKey = 0;
    int sawMouse = 0;

    RoboCheckEqInt(BootSim(32, 32, RoboQemuPixRgbx), RoboOk);
    fb = QemuFbCurrent();
    RoboCheck(fb && fb->pixfmt == RoboQemuPixRgbx);

    WindowConfigInit(&cfg);
    cfg.width = 32;
    cfg.height = 32;
    cfg.title = "qemu-rgbx";
    cfg.visible = 1;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);
    (void)WindowObserveFocus(id, 1);

    GfxSurfaceDescInit(&desc);
    desc.width = 32;
    desc.height = 32;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surf), RoboOk);
    (void)GfxSurfaceClear(surf, (GfxColorRgba8){255, 0, 0, 255});
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);
    RoboCheckEqInt(QemuFbGetPixel(fb, 0, 0, &px), RoboOk);
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0x0000FF); /* RGBX red */

    RoboCheckEqInt(
        QemuInputInjectRaw(1u /* KEY_DOWN */, 0x04u /* A */, 0, 0, 0, 0,
                                   0, 1u /* SHIFT */, 0),
        RoboOk);
    RoboCheckEqInt(
        QemuInputInjectRaw(2u /* KEY_UP */, 0x04u, 0, 0, 0, 0, 0, 0, 0),
        RoboOk);
    RoboCheckEqInt(
        QemuInputInjectRaw(3u /* MOVE */, 0, 0, 5, 7, 1, 2, 0, 0),
        RoboOk);
    RoboCheckEqInt(
        QemuInputInjectRaw(4u /* BUTTON */, 1u, 1u, 5, 7, 0, 0, 0, 0),
        RoboOk);
    RoboCheckEqInt(
        QemuInputInjectRaw(4u, 1u, 0u, 5, 7, 0, 0, 0, 0), RoboOk);
    RoboCheckEqInt(
        QemuInputInjectRaw(5u /* SCROLL */, 0, 120, 5, 7, 0, 120, 0, 0),
        RoboOk);

    while (WindowEventPoll(&ev) == RoboOk) {
        if (ev.type == RoboWindowEventKeyDown ||
            ev.type == RoboWindowEventKeyUp) {
            sawKey = 1;
        }
        if (ev.type == RoboWindowEventMouseMove ||
            ev.type == RoboWindowEventMouseButtonDown ||
            ev.type == RoboWindowEventMouseButtonUp ||
            ev.type == RoboWindowEventMouseWheel) {
            sawMouse = 1;
        }
    }
    RoboCheck(sawKey);
    RoboCheck(sawMouse);

    GfxSurfaceRelease(surf);
    (void)WindowDestroy(id);
    teardown();
}

static void TestQemuBoundsAndCleanup(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSurface *surf = NULL;
    GfxSurfaceDesc desc;

    RoboCheckEqInt(BootSim(16, 16, RoboQemuPixBgrx), RoboOk);
    WindowConfigInit(&cfg);
    cfg.width = 16;
    cfg.height = 16;
    cfg.title = "qemu-bounds";
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    GfxSurfaceDescInit(&desc);
    desc.width = 32;
    desc.height = 32;
    desc.format = RoboGfxFormatBgra8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surf), RoboOk);
    (void)GfxSurfaceClear(surf, (GfxColorRgba8){255, 255, 255, 255});
    /* Larger surface still presents clipped — must not crash. */
    RoboCheckEqInt(WindowPresent(id, surf), RoboOk);

    GfxSurfaceRelease(surf);
    (void)WindowDestroy(id);
    teardown();
    RoboCheck(!QemuSimIsActive());
    RoboCheck(QemuFbCurrent() == NULL);
}

static void TestQemuSimFrameLifecycle(void)
{
    WindowId id = RoboWindowIdNone;
    WindowConfig cfg;
    GfxSurface *surf = NULL;
    const QemuFbInfo *fb;
    uint32_t px = 0u;

    RoboCheckEqInt(BootSim(80, 60, RoboQemuPixBgrx), RoboOk);
    fb = QemuFbCurrent();
    RoboCheck(fb && fb->ready);

    WindowConfigInit(&cfg);
    cfg.width = 80;
    cfg.height = 60;
    cfg.title = "qemu-frame";
    cfg.visible = 1;
    RoboCheckEqInt(WindowCreate(&cfg, &id), RoboOk);

    RoboCheckEqInt(WindowBeginFrame(id, &surf), RoboOk);
    RoboCheck(surf != NULL);
    RoboCheckEqInt((int)GfxSurfaceWidth(surf), 80);
    RoboCheckEqInt((int)GfxSurfaceHeight(surf), 60);
    (void)GfxSurfaceClear(surf, (GfxColorRgba8){0, 0, 0, 255});
    (void)GfxSurfaceSetPixel(surf, 0, 0,
                                     (GfxColorRgba8){255, 255, 255, 255});
    (void)GfxSurfaceFillRect(
        surf, MakeGfxRect(20, 20, 10, 10),
        (GfxColorRgba8){14, 122, 255, 255});
    RoboCheckEqInt(WindowEndFrame(id), RoboOk);
    RoboCheck(WindowIsDirty(id));
    RoboCheckEqInt(WindowPresentFrame(id), RoboOk);
    RoboCheck(!WindowIsDirty(id));

    RoboCheckEqInt(QemuFbGetPixel(fb, 0, 0, &px), RoboOk);
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0xFFFFFFu);
    RoboCheckEqInt(QemuFbGetPixel(fb, 20, 20, &px), RoboOk);
    RoboCheckEqInt((int)(px & 0xFFFFFFu), 0x0E7AFF);

    (void)WindowDestroy(id);
    teardown();
}

void TestRegisterWindowQemuDisplay(void)
{
    TestRegister("window.qemu.fb_validate", TestQemuFbValidate);
    TestRegister("window.qemu.sim_present_colors",
                       TestQemuSimPresentColors);
    TestRegister("window.qemu.sim_rgbx_input",
                       TestQemuSimRgbxAndInput);
    TestRegister("window.qemu.bounds_cleanup",
                       TestQemuBoundsAndCleanup);
    TestRegister("window.qemu.sim_frame_lifecycle",
                       TestQemuSimFrameLifecycle);
}
