/*
 * RoboDesktop frame suite (Phase 11.13) — `desktop.frame.*`.
 *
 * The frame lifecycle and the pixels it produces: the three-call sequence and the one-call shorthand that
 * abbreviates it, the refusals a caller gets for nesting one frame inside another, the order the shell
 * paints in, what each region of the screen holds afterwards, the counters a frame reports, and the one
 * property the whole module is written to keep — **two frames of one state are byte-identical**.
 *
 * The pixel cases read the frame through `DesktopFrameSurface` and RoboGraphics' own
 * `GfxSurfaceGetPixel`, because that is the door a compositor, a screenshot or a test would use
 * and this module deliberately has no pixel door of its own: reading a pixel is what the graphics layer is
 * for. The colours come from the *theme* (`RoboDesktopTheme()`), so a case states "the panel is the panel
 * colour" rather than restating a hex value a restyle would break.
 *
 * The determinism case is the strongest form available: two independently created desktops, each with a
 * window whose contents came from the same markup, rendered and compared **byte for byte** — with the
 * record and the checksum compared beside them, because two pictures that match while their records differ
 * would mean one of the two is describing something else.
 */

#include "RoboDesktopTestSupport.h"

/* ------------------------------------------------------------------ */
/* 1. The lifecycle                                                    */
/* ------------------------------------------------------------------ */

static void TestFrameSequence(void)
{
    Desktop *desktop = DesktopOpen(320, 200);
    DesktopFrameRecord record;
    DesktopFrameRecord shorthand;
    DesktopStats stats;

    DesktopFrameRecordInit(&record);
    RoboCheckEqInt(DesktopFrameIsOpen(desktop), 0);
    RoboCheckEqInt(DesktopPaint(desktop, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopEndFrame(desktop, &record, NULL), RoboInvalidArgument);

    RoboCheckEqInt(DesktopBeginFrame(desktop, NULL), RoboOk);
    RoboCheckEqInt(DesktopFrameIsOpen(desktop), 1);
    /* Nested rather than silently accepted: a frame painted twice would composite every translucent
     * thing twice, and the record would only say so if somebody watched the counters. */
    RoboCheckEqInt(DesktopBeginFrame(desktop, NULL), RoboBusy);
    RoboCheckEqInt(DesktopPaint(desktop, NULL), RoboOk);
    RoboCheckEqInt(DesktopPaint(desktop, NULL), RoboBusy);
    RoboCheckEqInt(DesktopEndFrame(desktop, &record, NULL), RoboOk);
    RoboCheckEqInt(DesktopFrameIsOpen(desktop), 0);
    RoboCheckEqInt(DesktopEndFrame(desktop, &record, NULL), RoboInvalidArgument);

    /* What a desktop with nothing on it painted: the surface, the panel, its separator, two falloff rows
     * and the identity mark -- and no window at all. */
    RoboCheckEqInt((int)record.width, 320);
    RoboCheckEqInt((int)record.height, 200);
    RoboCheckEqInt((int)record.bytes, 320 * 200 * 4);
    RoboCheckEqInt((int)record.windows, 0);
    RoboCheckEqInt((int)record.windowsTotal, 0);
    RoboCheckEqInt((int)record.contentRuns, 0);
    RoboCheckEqInt((int)record.contentPixels, 0);
    RoboCheckEqInt((int)record.fills, 6);
    RoboCheckEqInt((int)record.clipped, 0);
    RoboCheckEqInt(DesktopFrameRecordIsValid(&record), 1);
    RoboCheck(record.checksum != 0u);
    RoboCheckEqInt((int)record.checksum, (int)DesktopFrameChecksum(desktop));

    RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
    RoboCheckEqInt((int)stats.frames, 1);
    RoboCheckEqInt((int)stats.framesWithoutWindows, 1);
    RoboCheckEqInt((int)stats.fillDrawings, 6);
    RoboCheckEqInt((int)stats.labelsUnshaped, 2);

    /* The shorthand is the same frame, and it must not drift from the sequence it abbreviates. */
    RoboCheckEqInt(DesktopRender(desktop, &shorthand, NULL), RoboOk);
    RoboCheckEqInt(DesktopFrameRecordEqual(&record, &shorthand), 1);
    RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
    RoboCheckEqInt((int)stats.frames, 2);
    RoboCheckEqInt((int)stats.framesWithoutWindows, 2);

    DesktopClose(desktop);
    RoboCheckEqInt(DesktopBeginFrame(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopEndFrame(NULL, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopPaint(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopRender(NULL, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopFrameIsOpen(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* 2. The shell's pixels                                               */
/* ------------------------------------------------------------------ */

static void TestFrameShellPixels(void)
{
    Desktop *desktop = DesktopOpen(320, 200);
    DesktopTheme theme = GetRoboDesktopTheme();
    DesktopFrameRecord record;

    DesktopFixture();
    RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);

    /* 1. The desktop surface, over the whole screen; the panel over its top band. */
    DesktopPixelIs(desktop, 5, 180, theme.desktopSurface, "desktop surface");
    DesktopPixelIs(desktop, 5, 5, theme.panelSurface, "panel surface");
    /* The panel's last row is the hairline, and the two rows below it are the stepped falloff -- which is
     * a *translucent* pair of rows, so the expected pixel is the graphics module's own source-over blend of
     * the shadow colour over the surface beneath it. Asserting the raw colour would fail, and asserting a
     * hand-computed constant would be a second implementation of the blend. */
    DesktopPixelIs(desktop, 5, 29, theme.panelSeparator, "panel separator");
    DesktopPixelIs(desktop, 5, 30,
                     GfxCompositeSrcOver(theme.shadowNear, theme.desktopSurface),
                     "panel shadow, near row");
    DesktopPixelIs(desktop, 5, 31,
                     GfxCompositeSrcOver(theme.shadowFar, theme.desktopSurface),
                     "panel shadow, far row");
    /* The identity mark: the shell's one accent, three pixels wide and twelve tall, centred in the panel. */
    DesktopPixelIs(desktop, 15, 15, theme.identityAccent, "identity mark");

    /* The colours are the theme's, so the frame is a function of the palette and nothing else. */
    {
        DesktopTheme custom = GetRoboDesktopTheme();
        DesktopConfig config;
        Desktop *themed = NULL;

        custom.desktopSurface = GfxRgba8(1, 2, 3, 255);
        custom.panelSurface = GfxRgba8(4, 5, 6, 255);
        DesktopConfigInit(&config);
        config.width = 64;
        config.height = 48;
        config.theme = &custom;
        RoboCheckEqInt(DesktopCreate(&config, &themed, NULL), RoboOk);
        RoboCheckEqInt(DesktopRender(themed, NULL, NULL), RoboOk);
        DesktopPixelIs(themed, 5, 5, custom.panelSurface, "custom panel surface");
        DesktopPixelIs(themed, 5, 45, custom.desktopSurface, "custom desktop surface");
        DesktopClose(themed);
    }

    DesktopClose(desktop);
}

static void TestFrameWindowPixels(void)
{
    Desktop *desktop = DesktopOpen(320, 200);
    DesktopTheme theme = GetRoboDesktopTheme();
    DesktopWindowId id = DesktopAddWindow(desktop, "Window", 100, 60, 160, 100);
    DesktopFrameRecord record;
    GfxRect client;

    DesktopFixture();
    RoboCheckEqInt(DesktopWindowClientBounds(desktop, id, &client), RoboOk);
    RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
    RoboCheckEqInt((int)record.windows, 1);
    RoboCheckEqInt((int)record.windowsTotal, 1);

    /* The border is the whole outer rectangle, painted first and covered by the title bar and the body:
     * one pixel of ring without four side fills. */
    DesktopPixelIs(desktop, 100, 60, theme.windowBorderFocused, "focused window's top border");
    DesktopPixelIs(desktop, 100, 100, theme.windowBorderFocused, "focused window's left border");
    /* The title bar: one pixel in from the border, twenty-four tall. */
    DesktopPixelIs(desktop, 150, 70, theme.windowTitleSurface, "window title bar");
    /* The body, which is what a window whose contents could not be drawn still shows. */
    DesktopPixelIs(desktop, client.x, client.y + client.height - 1, theme.windowSurface,
                     "window body");
    /* The close button: ten pixels square, six in from the frame's right edge, centred in the title bar --
     * read at its top-left corner rather than its middle, because the middle carries the cross. */
    DesktopPixelIs(desktop, 100 + 160 - 1 - 6 - 10 + 1, 60 + 1 + 7 + 1, theme.windowClose,
                     "close button");

    /* An unfocused window is a different border, and that is the shell's whole focus story. */
    {
        (void)DesktopAddWindow(desktop, "Other", 30, 30, 120, 80);

        RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
        /* Read on `first`'s top border *outside* `second`, which is the front window: (30, 30, 120, 80)
         * covers everything left of x = 150 and above y = 110. */
        DesktopPixelIs(desktop, 200, 60, theme.windowBorder, "unfocused window's border");
        RoboCheckEqInt((int)record.windows, 2);
    }

    /* A hidden window is not painted at all: the desktop's own surface shows where it was. */
    RoboCheckEqInt(DesktopWindowSetVisible(desktop, id, 0, NULL), RoboOk);
    RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
    RoboCheckEqInt((int)record.windows, 1);
    RoboCheckEqInt((int)record.windowsTotal, 2);
    DesktopPixelIs(desktop, 150, 100, theme.desktopSurface, "no window where the hidden one was");

    DesktopClose(desktop);
}

static void TestFrameWindowContent(void)
{
    {
        Desktop *desktop = DesktopOpen(320, 240);
        DesktopWindowId id = DesktopAddWindow(desktop, "Content", 40, 40, 200, 140);
        DesktopDocument *document = NULL;
        DesktopFrameRecord record;
        DesktopTheme theme = GetRoboDesktopTheme();
        DesktopStats stats;
        GfxRect client;

        DesktopFixture();
        /* A window's contents are a document, and the window's client rectangle is what it is built for. */
        document = DesktopSetWindowContent(
            desktop, id, "<html><body><div class='card'>Hello</div></body></html>",
            /* The same shape the render scenes' sheets use: an explicit `display: block` for every kind of
             * box the document has, because this build's cascade does not default one, and then the box the
             * picture is about. A sheet without it lays out nothing, which is what the first version of this
             * fixture did and why the case had to be fixed rather than the module. */
            "html, body, div { display: block }"
            "html, body { margin: 0; padding: 0 }"
            ".card { width: 120px; height: 40px; background-color: #0e7aff }",
            NULL);
        RoboCheckEqInt(DesktopWindowClientBounds(desktop, id, &client), RoboOk);
        RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
        RoboCheckEqInt((int)record.contentRuns, 1);
        RoboCheckEqInt((int)record.contentPixels, client.width * client.height);
        /* Ink landed in the client area: the document's own background is not the window's body. */
        DesktopRegionHasInkIs(desktop, client.x, client.y, client.width, client.height,
                                  theme.windowSurface, "window contents");

        RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
        RoboCheckEqInt((int)stats.contentRuns, 1);
        RoboCheckEqInt((int)stats.contentPixels, client.width * client.height);

        /* A window with no contents keeps its body: `contentRuns` is 0 and the body is the window
         * colour, which is the state that makes the shell's own smoke test possible. */
        RoboCheckEqInt(DesktopWindowSetContent(desktop, id, NULL, NULL, NULL), RoboOk);
        RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
        RoboCheckEqInt((int)record.contentRuns, 0);
        DesktopPixelIs(desktop, client.x + 2, client.y + 2, theme.windowSurface,
                         "window body with no contents");

        DesktopDocumentDestroy(document);
        DesktopClose(desktop);
    }
}

/* ------------------------------------------------------------------ */
/* 3. Labels                                                           */
/* ------------------------------------------------------------------ */

static void TestFrameLabels(void)
{
    Desktop *fontless = DesktopOpen(400, 300);
    DesktopFrameRecord record;

    /* No face: the shell counts what it *would* have drawn, which is how a caller learns that its desktop
     * has labels it is not showing. Two labels are asked for -- the identity and the applications; the
     * status label is empty because a caller supplies it -- and both are counted as unshaped. */
    RoboCheckEqInt(DesktopRender(fontless, &record, NULL), RoboOk);
    RoboCheckEqInt((int)record.labels, 0);
    RoboCheckEqInt((int)record.labelsUnshaped, 2);
    DesktopClose(fontless);

    {
        RenderFace face;
        Desktop *desktop;

        RenderFaceCreate(&face);
        desktop = DesktopOpenWithFace(400, 300, face.face);
        /* A status label is drawn last within the panel and only when there is one. */
        RoboCheckEqInt(DesktopSetStatus(desktop, "9:41", NULL), RoboOk);
        RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
        RoboCheckEqInt((int)record.labels, 3);
        RoboCheckEqInt((int)record.labelsUnshaped, 0);
        DesktopClose(desktop);
        RenderFaceDestroy(&face);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Determinism                                                      */
/* ------------------------------------------------------------------ */

static const char *DETERMINISM_HTML = "<html><body><div class='card'>RoboticelaOS</div></body></html>";
static const char *DETERMINISM_CSS =
    "html, body, div { display: block }"
    "html, body { margin: 0; padding: 0 }"
    ".card { width: 120px; height: 24px; background-color: #0e7aff }";

static void RenderSceneDesktop(Desktop *desktop, DesktopWindowId *outWindow,
                                 DesktopDocument **outDocument)
{
    DesktopWindowId id = DesktopAddWindow(desktop, "Determinism", 20, 40, 200, 120);

    *outDocument = DesktopSetWindowContent(desktop, id, DETERMINISM_HTML, DETERMINISM_CSS, NULL);
    *outWindow = id;
    RoboCheckEqInt(DesktopRender(desktop, NULL, NULL), RoboOk);
}

static void TestFrameDeterminism(void)
{
    Desktop *a = DesktopOpen(240, 180);
    Desktop *b = DesktopOpen(240, 180);
    DesktopWindowId windowA = RoboDesktopWindowIdNone;
    DesktopWindowId windowB = RoboDesktopWindowIdNone;
    DesktopDocument *documentA = NULL;
    DesktopDocument *documentB = NULL;
    size_t sizeA = 0u;
    size_t sizeB = 0u;
    const uint8_t *bytesA;
    const uint8_t *bytesB;
    DesktopFrameRecord recordA;
    DesktopFrameRecord recordB;

    RenderSceneDesktop(a, &windowA, &documentA);
    RenderSceneDesktop(b, &windowB, &documentB);
    RoboCheckEqInt((int)windowA, (int)windowB);
    RoboCheckEqInt((int)DesktopFrameChecksum(a), (int)DesktopFrameChecksum(b));

    bytesA = DesktopFrameBytes(a, &sizeA);
    bytesB = DesktopFrameBytes(b, &sizeB);
    RoboCheck(bytesA != NULL && bytesB != NULL);
    RoboCheckEqInt((int)sizeA, (int)sizeB);
    RoboCheckEqInt((int)sizeA, 240 * 180 * 4);
    if (memcmp(bytesA, bytesB, sizeA) != 0) {
        size_t index;

        for (index = 0u; index < sizeA; index++) {
            if (bytesA[index] != bytesB[index]) {
                TestFail(__FILE__, __LINE__, "byte %u differs: %u vs %u", (unsigned)index,
                               (unsigned)bytesA[index], (unsigned)bytesB[index]);
                break;
            }
        }
    }

    /* The records agree too: two pictures that matched while their records differed would mean one of the
     * two was describing something else. */
    RoboCheckEqInt(DesktopRender(a, &recordA, NULL), RoboOk);
    RoboCheckEqInt(DesktopRender(b, &recordB, NULL), RoboOk);
    RoboCheckEqInt(DesktopFrameRecordEqual(&recordA, &recordB), 1);
    /* And a second frame of the same state is the same frame, which is the property a checksum exists to
     * let a caller check rather than believe. */
    RoboCheckEqInt((int)DesktopFrameChecksum(a), (int)recordA.checksum);

    DesktopDocumentDestroy(documentA);
    DesktopDocumentDestroy(documentB);
    DesktopClose(a);
    DesktopClose(b);
}

/* ------------------------------------------------------------------ */
/* 5. Resize and the pointer                                           */
/* ------------------------------------------------------------------ */

static void TestFrameResize(void)
{
    Desktop *desktop = DesktopOpen(400, 300);
    DesktopWindowId id = DesktopAddWindow(desktop, "Resized", 250, 150, 300, 200);
    DesktopStats stats;
    GfxRect bounds;
    uint64_t before;

    RoboCheckEqInt(DesktopRender(desktop, NULL, NULL), RoboOk);
    before = DesktopFrameChecksum(desktop);

    /* The sequence the phase asks for: new framebuffer, new geometry, windows revalidated, repaint. */
    RoboCheckEqInt(DesktopResize(desktop, 800, 600, NULL), RoboOk);
    /* A resized screen has no previous frame to show: the next frame begins from the target's zero state
     * and paints a whole picture, so the checksum has to have changed. */
    RoboCheck(DesktopFrameChecksum(desktop) != before);
    RoboCheckEqInt(DesktopRender(desktop, NULL, NULL), RoboOk);

    RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
    RoboCheckEqInt((int)stats.resizes, 1);

    /* The window is clamped into the *new* work area rather than left where it was. */
    RoboCheckEqInt(DesktopWindowBounds(desktop, id, &bounds), RoboOk);
    RoboCheck(bounds.x + bounds.width <= 800);
    RoboCheck(bounds.y + bounds.height <= 600);
    RoboCheck(bounds.y >= 30);

    /* A frame that was in flight when the screen changed is abandoned rather than presented as if its
     * record still described the screen. */
    RoboCheckEqInt(DesktopBeginFrame(desktop, NULL), RoboOk);
    RoboCheckEqInt(DesktopResize(desktop, 500, 400, NULL), RoboOk);
    RoboCheckEqInt(DesktopFrameIsOpen(desktop), 0);
    RoboCheckEqInt(DesktopEndFrame(desktop, NULL, NULL), RoboInvalidArgument);

    /* Following a native window with none attached is a refusal rather than a silent no-op. */
    RoboCheckEqInt(DesktopSyncSize(desktop, NULL, NULL), RoboNotSupported);
    RoboCheckEqInt(DesktopSyncSize(NULL, NULL, NULL), RoboInvalidArgument);

    DesktopClose(desktop);
}

static void TestFrameNoWindowState(void)
{
    Desktop *desktop = DesktopOpen(200, 150);

    /* A desktop with no window is a legitimate desktop: it presents nowhere and says so. */
    RoboCheckEqInt(DesktopPresent(desktop, NULL, NULL, NULL), RoboNotSupported);
    RoboCheckEqInt(DesktopPresent(NULL, NULL, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopPresentToSurface(desktop, NULL, 0, 0, NULL, NULL),
                      RoboInvalidArgument);

    DesktopClose(desktop);
    RoboCheck(DesktopFrameSurface(NULL) == NULL);
    RoboCheckEqInt((int)DesktopFrameChecksum(NULL), 0);
    RoboCheck(DesktopFrameBytes(NULL, NULL) == NULL);
}

void TestRegisterDesktopFrame(void)
{
    TestRegister("desktop.frame.sequence", TestFrameSequence);
    TestRegister("desktop.frame.shell_pixels", TestFrameShellPixels);
    TestRegister("desktop.frame.window_pixels", TestFrameWindowPixels);
    TestRegister("desktop.frame.window_content", TestFrameWindowContent);
    TestRegister("desktop.frame.labels", TestFrameLabels);
    TestRegister("desktop.frame.determinism", TestFrameDeterminism);
    TestRegister("desktop.frame.resize", TestFrameResize);
    TestRegister("desktop.frame.no_window_state", TestFrameNoWindowState);
}
