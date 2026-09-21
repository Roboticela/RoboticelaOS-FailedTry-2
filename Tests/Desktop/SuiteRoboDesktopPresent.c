/*
 * RoboDesktop presentation suite (Phase 11.13) — `desktop.present.*`.
 *
 * Where a frame goes, and the end-to-end proof that it gets there with the right pixels in it. The chain
 * under test is the phase's whole point:
 *
 *     HTML → CSS → layout → paint → display list → rasteriser → a window's contents
 *                                                                    ↓
 *                                            the shell's screen target → a destination surface
 *
 * The destination here is a RoboGraphics surface rather than a native window, and deliberately: a case that
 * created a window would need a display session to check a copy, while `RenderPresent` is the *same*
 * door the window path uses (the desktop's `present` is a thin call of it into RoboWindow's own render
 * surface). What a native window adds is a backend, and the backend is verified where it lives — the QEMU
 * display module's own markers — not by a suite that would have to open one.
 *
 * The clipping cases are the presenter's rule read as assertions: a frame that does not fit its destination
 * is **clipped, not refused**, because a window that shrank between rasterising and presenting must show
 * the part of the picture that still fits. `presentedPixels + clippedPixels == frame pixels` is asserted
 * as the arithmetic it is.
 */

#include "RoboDesktopTestSupport.h"

static const char *PRESENT_HTML =
    "<html><body><div class='card'>RoboticelaOS</div></body></html>";
static const char *PRESENT_CSS =
    "html, body, div { display: block }"
    "html, body { margin: 0; padding: 0 }"
    ".card { width: 120px; height: 24px; background-color: #0e7aff }";

/* ------------------------------------------------------------------ */
/* 1. The destination                                                  */
/* ------------------------------------------------------------------ */

static void TestPresentToSurface(void)
{
    Desktop *desktop = DesktopOpen(200, 150);
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    RenderPresentRecord record;
    GfxColorRgba8 page = { 0, 0, 0, 255 };
    GfxColorRgba8 color;

    DesktopFixture();
    RoboCheckEqInt(DesktopRender(desktop, NULL, NULL), RoboOk);

    GfxSurfaceDescInit(&desc);
    desc.width = 200u;
    desc.height = 150u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheckEqInt(GfxSurfaceFillRect(surface, MakeGfxRect(0, 0, 200, 150), page), RoboOk);

    /* The whole frame into a destination of exactly its size: complete, nothing clipped. */
    RenderPresentRecordInit(&record);
    RoboCheckEqInt(DesktopPresentToSurface(desktop, surface, 0, 0, &record, NULL), RoboOk);
    RoboCheckEqInt(record.complete, 1);
    RoboCheckEqInt((int)record.presentedPixels, 200 * 150);
    RoboCheckEqInt((int)record.clippedPixels, 0);
    RoboCheckEqInt((int)record.frameChecksum, (int)DesktopFrameChecksum(desktop));
    RoboCheckEqInt(RenderPresentRecordIsValid(&record), 1);

    /* The pixels landed: the panel's colour is on the destination where the panel is. */
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, 5u, 5u, &color), RoboOk);
    DesktopPixelIs(desktop, 5, 5, color, "presented panel pixel matches the frame");

    /* A destination smaller than the frame clips rather than refusing: the part that fits is written. */
    {
        GfxSurfaceDesc small;
        GfxSurface *smaller = NULL;

        GfxSurfaceDescInit(&small);
        small.width = 100u;
        small.height = 40u;
        small.format = RoboGfxFormatRgba8;
        small.access = RoboGfxSurfaceAccessReadWrite;
        RoboCheckEqInt(GfxSurfaceCreate(&small, &smaller), RoboOk);
        RoboCheckEqInt(GfxSurfaceFillRect(smaller, MakeGfxRect(0, 0, 100, 40), page),
                          RoboOk);
        RoboCheckEqInt(DesktopPresentToSurface(desktop, smaller, 0, 0, &record, NULL),
                          RoboOk);
        RoboCheckEqInt(record.complete, 0);
        RoboCheckEqInt((int)record.presentedPixels, 100 * 40);
        /* Exhaustive: the two counts are the frame's pixels. */
        RoboCheckEqInt((int)(record.presentedPixels + record.clippedPixels), 200 * 150);
        RoboCheckEqInt(RenderPresentRecordIsValid(&record), 1);
        GfxSurfaceRelease(smaller);
    }

    /* An offset that pushes the frame off the destination entirely: a successful presentation of nothing,
     * described rather than refused. */
    RoboCheckEqInt(DesktopPresentToSurface(desktop, surface, -500, 0, &record, NULL), RoboOk);
    RoboCheckEqInt((int)record.presentedPixels, 0);
    RoboCheckEqInt((int)record.clippedPixels, 200 * 150);
    RoboCheckEqInt(record.complete, 0);

    /* Presenting twice is presenting twice: the frame is not consumed, cleared or modified. */
    {
        uint64_t before = DesktopFrameChecksum(desktop);

        RoboCheckEqInt(DesktopPresentToSurface(desktop, surface, 0, 0, NULL, NULL), RoboOk);
        RoboCheckEqInt((int)DesktopFrameChecksum(desktop), (int)before);
    }

    RoboCheckEqInt(DesktopPresentToSurface(NULL, surface, 0, 0, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopPresentToSurface(desktop, NULL, 0, 0, NULL, NULL),
                      RoboInvalidArgument);

    GfxSurfaceRelease(surface);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 2. The shell's own window                                           */
/* ------------------------------------------------------------------ */

static void TestPresentNeedsAWindow(void)
{
    Desktop *desktop = DesktopOpen(120, 90);
    int presented = 1;

    /* No native window: the door that presents into one refuses, while the frame itself is real. */
    RoboCheckEqInt(DesktopPresent(desktop, NULL, &presented, NULL), RoboNotSupported);
    RoboCheckEqInt(presented, 0);
    RoboCheckEqInt(DesktopRender(desktop, NULL, NULL), RoboOk);
    RoboCheck(DesktopFrameSurface(desktop) != NULL);

    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 3. End to end                                                       */
/* ------------------------------------------------------------------ */

static void TestPresentEndToEnd(void)
{
    RenderFace face;
    Desktop *desktop;
    DesktopDocument *document;
    DesktopWindowId id;
    GfxSurfaceDesc desc;
    GfxSurface *dest = NULL;
    RenderPresentRecord record;
    DesktopFrameRecord frame;
    GfxRect client;
    DesktopTheme theme = GetRoboDesktopTheme();

    DesktopFixture();
    RenderFaceCreate(&face);
    desktop = DesktopOpenWithFace(320, 240, face.face);
    id = DesktopAddWindow(desktop, "Roboticela Demo", 30, 40, 240, 160);

    /* The whole pipeline, in the order the launcher and the guest run it. */
    document = DesktopSetWindowContent(desktop, id, PRESENT_HTML, PRESENT_CSS, face.face);
    RoboCheckEqInt(DesktopWindowClientBounds(desktop, id, &client), RoboOk);
    RoboCheckEqInt(DesktopRender(desktop, &frame, NULL), RoboOk);
    RoboCheckEqInt((int)frame.windows, 1);
    RoboCheckEqInt((int)frame.contentRuns, 1);
    RoboCheckEqInt((int)frame.labels >= 3, 1); /* identity, applications, the window's title */
    RoboCheckEqInt((int)frame.labelsUnshaped, 0);

    /* The window's contents are in the client area, and the shell's chrome is around them. */
    DesktopRegionHasInkIs(desktop, client.x, client.y, client.width, client.height,
                              theme.windowSurface, "document ink in the client area");
    DesktopPixelIs(desktop, 30 + 60, 42, theme.windowTitleSurface, "the window's title bar");

    /* Present the finished frame into a destination surface of the screen's size. */
    GfxSurfaceDescInit(&desc);
    desc.width = 320u;
    desc.height = 240u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &dest), RoboOk);
    RoboCheckEqInt(DesktopPresentToSurface(desktop, dest, 0, 0, &record, NULL), RoboOk);
    RoboCheckEqInt(record.complete, 1);
    RoboCheckEqInt((int)record.presentedPixels, 320 * 240);
    RoboCheckEqInt((int)record.presented.width, 320);
    RoboCheckEqInt((int)record.presented.height, 240);

    /* Every pixel of the destination is the frame's, because the frame is opaque and covers the screen. */
    {
        GfxColorRgba8 presented;
        GfxColorRgba8 inFrame;

        RoboCheckEqInt(GfxSurfaceGetPixel(dest, 10u, 100u, &presented), RoboOk);
        inFrame = DesktopPixel(desktop, 10, 100);
        RoboCheck(DesktopColorIs(presented, inFrame));
        RoboCheckEqInt(GfxSurfaceGetPixel(dest, (uint32_t)client.x + 4u,
                                                     (uint32_t)client.y + 4u, &presented),
                          RoboOk);
        inFrame = DesktopPixel(desktop, (uint32_t)client.x + 4u, (uint32_t)client.y + 4u);
        RoboCheck(DesktopColorIs(presented, inFrame));
    }

    /* The checkpoint a person looks at. This is the one case that *asserts* it was written, and for the
     * reason the render module's own checkpoint case gives: the phase's visual-verification requirement is
     * a claim about a file somebody looks at, and a checkpoint that silently failed to be written is a case
     * that believes it verified something. The pixels above are asserted separately, so a filesystem that
     * refused this file would still leave the picture verified -- it would just fail this case loudly. */
    RoboCheckEqInt(DesktopWritePng(desktop, DesktopCheckpointPath()), 1);

    GfxSurfaceRelease(dest);
    DesktopDocumentDestroy(document);
    DesktopClose(desktop);
    RenderFaceDestroy(&face);
}

void TestRegisterDesktopPresent(void)
{
    TestRegister("desktop.present.to_surface", TestPresentToSurface);
    TestRegister("desktop.present.needs_a_window", TestPresentNeedsAWindow);
    TestRegister("desktop.present.end_to_end", TestPresentEndToEnd);
}
