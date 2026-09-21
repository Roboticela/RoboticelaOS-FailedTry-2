/*
 * Shared RoboDesktop test fixtures (Phase 11.13).
 *
 * Every suite in `desktop/tests` includes this. Like the paint and render support headers it is **not** a
 * framework -- the runtime has exactly one of those (`Tests/Framework/RoboTest.h`) and this release adds
 * none -- and it holds the four things a desktop suite would otherwise copy:
 *
 *   bring-up    every layer the shell draws on, through `RoboRenderTestSupport.h` rather than beside it.
 *               A desktop fixture *is* a render fixture plus a shell, and a second copy of "build a
 *               document, style it, lay it out, paint it, list it" would be a second thing to keep in step
 *               with the pipeline this module merely composites;
 *
 *   a face      the render fixture's synthetic SFNT, so a window's text is measured and drawn by a real
 *               face and a desktop case can assert that a title *drew* rather than that it was asked to.
 *               The shell's own chrome face is the same face: one face for chrome and content keeps a
 *               pixel assertion comparable across the two;
 *
 *   a screen    an owned desktop at a named size with the default theme and metrics, plus the two reads a
 *               pixel assertion needs. The pixel read goes through `DesktopFrameSurface` and
 *               RoboGraphics' own `GfxSurfaceGetPixel` deliberately: this module has no pixel door
 *               of its own, because reading a pixel is what the graphics layer is for;
 *
 *   a document  `desktop_window_content` builds a real application document (markup, an author sheet,
 *               layout, paint, display list) for a window's client area, at the client size the *shell*
 *               computed -- so a case can say "this window shows this markup" in one call without
 *               re-deriving the client rectangle, which is the door the shell itself would use.
 *
 * Every helper asserts rather than returning a status, which is the convention the layout, paint and
 * render suites set: a suite that had to check each step of its own scaffolding would drown its subject in
 * harness.
 */

#ifndef ROBO_DESKTOP_TEST_SUPPORT_H
#define ROBO_DESKTOP_TEST_SUPPORT_H

#include "RoboRenderTestSupport.h"

#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopDocument.h"
/* The blend itself, for a fixture that has to state an expected pixel for a *translucent* drawing: the
 * panel's falloff rows are a shadow over the surface, and a test that hand-computed the blend would be a
 * second implementation of it. */
#include "RoboGraphics/RoboGraphicsComposite.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

/** Bring every layer a desktop test needs up (once) and leave them up. */
static inline void DesktopFixture(void)
{
    RenderFixture();
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t DesktopLiveBlocks(void)
{
    return MemDebugLiveCount();
}

/** This build's default theme, by value. */
static inline DesktopTheme GetRoboDesktopTheme(void)
{
    DesktopTheme theme;

    DesktopThemeInit(&theme);
    return theme;
}

/** This build's default panel metrics, by value. */
static inline DesktopPanelMetrics GetRoboDesktopPanelMetrics(void)
{
    DesktopPanelMetrics metrics;

    DesktopPanelMetricsInit(&metrics);
    return metrics;
}

/** This build's default window decoration metrics, by value. */
static inline DesktopWindowMetrics GetRoboDesktopWindowMetrics(void)
{
    DesktopWindowMetrics metrics;

    DesktopWindowMetricsInit(&metrics);
    return metrics;
}

/* ------------------------------------------------------------------ */
/* A screen                                                            */
/* ------------------------------------------------------------------ */

/**
 * A desktop at @p width by @p height with no native window.
 *
 * No window on purpose: this build's desktop is presentable without one (`DesktopPresent` reports
 * `RoboNotSupported` and the frames are still real), and a suite that created one would need a display
 * session to check a rectangle. The cases that *are* about presentation take a window explicitly.
 */
static inline Desktop *DesktopOpen(int32_t width, int32_t height)
{
    DesktopConfig config;
    Desktop *desktop = NULL;

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = width;
    config.height = height;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);
    RoboCheck(desktop != NULL);
    return desktop;
}

/** The same, with a chrome face: a desktop that draws its labels. */
static inline Desktop *DesktopOpenWithFace(int32_t width, int32_t height,
                                                    FontFace *face)
{
    DesktopConfig config;
    Desktop *desktop = NULL;

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = width;
    config.height = height;
    config.face = face;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);
    RoboCheck(desktop != NULL);
    return desktop;
}

static inline void DesktopClose(Desktop *desktop)
{
    DesktopDestroy(desktop);
}

/** One window, asserted to have been added. */
static inline DesktopWindowId DesktopAddWindow(Desktop *desktop,
                                                          const char *title,
                                                          int32_t x,
                                                          int32_t y,
                                                          int32_t width,
                                                          int32_t height)
{
    DesktopWindowDesc desc;
    DesktopWindowId id = RoboDesktopWindowIdNone;

    DesktopWindowDescInit(&desc);
    desc.title = title;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;
    RoboCheckEqInt(DesktopWindowAdd(desktop, &desc, &id, NULL), RoboOk);
    RoboCheck(id != RoboDesktopWindowIdNone);
    return id;
}

/* ------------------------------------------------------------------ */
/* Pixels                                                              */
/* ------------------------------------------------------------------ */

/** The frame's colour at @p x,@p y, through the graphics layer's own read. */
static inline GfxColorRgba8 DesktopPixel(const Desktop *desktop, uint32_t x, uint32_t y)
{
    GfxColorRgba8 color;
    const GfxSurface *surface = DesktopFrameSurface(desktop);

    memset(&color, 0, sizeof(color));
    RoboCheck(surface != NULL);
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
    return color;
}

static inline int DesktopColorIs(GfxColorRgba8 a, GfxColorRgba8 b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/** Assert the frame's pixel at @p x,@p y is exactly @p expected. */
static inline void DesktopPixelIs(const Desktop *desktop,
                                    int32_t x,
                                    int32_t y,
                                    GfxColorRgba8 expected,
                                    const char *what)
{
    GfxColorRgba8 actual = DesktopPixel(desktop, (uint32_t)x, (uint32_t)y);

    if (!DesktopColorIs(actual, expected)) {
        TestFail(__FILE__, __LINE__,
                       "%s: pixel %d,%d is %u,%u,%u,%u, expected %u,%u,%u,%u", what, (int)x, (int)y,
                       (unsigned)actual.r, (unsigned)actual.g, (unsigned)actual.b,
                       (unsigned)actual.a, (unsigned)expected.r, (unsigned)expected.g,
                       (unsigned)expected.b, (unsigned)expected.a);
    }
}

/** Assert the frame's pixel at @p x,@p y is **not** @p other. */
static inline void DesktopPixelIsNot(const Desktop *desktop,
                                        int32_t x,
                                        int32_t y,
                                        GfxColorRgba8 other,
                                        const char *what)
{
    GfxColorRgba8 actual = DesktopPixel(desktop, (uint32_t)x, (uint32_t)y);

    if (DesktopColorIs(actual, other)) {
        TestFail(__FILE__, __LINE__, "%s: pixel %d,%d is still %u,%u,%u,%u", what, (int)x,
                       (int)y, (unsigned)actual.r, (unsigned)actual.g, (unsigned)actual.b,
                       (unsigned)actual.a);
    }
}

/** Non-zero when any pixel in the half-open rectangle differs from @p background. */
static inline int DesktopRegionHasInk(const Desktop *desktop,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         GfxColorRgba8 background)
{
    GfxSize size = DesktopScreenSize(desktop);
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = ((x + w) > size.width) ? size.width : (x + w);
    int32_t y1 = ((y + h) > size.height) ? size.height : (y + h);
    int32_t px;
    int32_t py;

    for (py = y0; py < y1; py++) {
        for (px = x0; px < x1; px++) {
            if (!DesktopColorIs(DesktopPixel(desktop, (uint32_t)px, (uint32_t)py), background)) {
                return 1;
            }
        }
    }
    return 0;
}

/** Assert that something drew inside the region. */
static inline void DesktopRegionHasInkIs(const Desktop *desktop,
                                            int32_t x,
                                            int32_t y,
                                            int32_t w,
                                            int32_t h,
                                            GfxColorRgba8 background,
                                            const char *what)
{
    if (!DesktopRegionHasInk(desktop, x, y, w, h, background)) {
        TestFail(__FILE__, __LINE__,
                       "%s: no pixel in %d,%d,%d,%d differs from the background", what, (int)x,
                       (int)y, (int)w, (int)h);
    }
}

/** Assert that nothing drew inside the region. */
static inline void DesktopRegionIsClear(const Desktop *desktop,
                                          int32_t x,
                                          int32_t y,
                                          int32_t w,
                                          int32_t h,
                                          GfxColorRgba8 background,
                                          const char *what)
{
    if (DesktopRegionHasInk(desktop, x, y, w, h, background)) {
        TestFail(__FILE__, __LINE__, "%s: a pixel in %d,%d,%d,%d is not the background", what,
                       (int)x, (int)y, (int)w, (int)h);
    }
}

/* ------------------------------------------------------------------ */
/* A window's contents                                                 */
/* ------------------------------------------------------------------ */

/**
 * Build a real application document for @p id's client area and attach it.
 *
 * The client rectangle comes from the *shell* (`DesktopWindowClientBounds`), so a document is
 * exactly the size the window will composite it at -- which is the same order the host launcher and the
 * guest build in, one call each. The document object is returned so the caller can release it after the
 * window is destroyed, which is the lifetime `DesktopWindowSetContent` states.
 */
static inline DesktopDocument *DesktopSetWindowContent(Desktop *desktop,
                                                                 DesktopWindowId id,
                                                                 const char *html,
                                                                 const char *css,
                                                                 FontFace *face)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    GfxRect client;

    RoboCheckEqInt(DesktopWindowClientBounds(desktop, id, &client), RoboOk);
    DesktopDocumentConfigInit(&config);
    config.html = html;
    config.css = css;
    config.width = client.width;
    config.height = client.height;
    config.face = face;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheck(document != NULL);
    RoboCheckEqInt(DesktopWindowSetContent(desktop, id,
                                                     DesktopDocumentDisplayList(document),
                                                     face, NULL),
                      RoboOk);
    return document;
}

/* ------------------------------------------------------------------ */
/* The visual checkpoint                                               */
/* ------------------------------------------------------------------ */

/**
 * Encode the current frame and write it to @p path. Returns non-zero on success.
 *
 * Deliberately **not** an assertion, for the reason the render fixture's own checkpoint gives: a
 * checkpoint is evidence for a person to look at, and a suite whose verdict depended on the filesystem
 * would fail on a machine with a read-only tree while the pixels it verified were perfectly good.
 */
static inline int DesktopWritePng(const Desktop *desktop, const char *path)
{
    GfxEncodeOptions options;
    GfxEncodedBuffer buffer;
    const GfxSurface *surface = DesktopFrameSurface(desktop);
    FILE *file;

    if (!surface) {
        return 0;
    }
    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&buffer, 0, sizeof(buffer));
    if (GfxSurfaceEncode(surface, &options, &buffer) != RoboOk) {
        return 0;
    }
    file = fopen(path, "wb");
    if (!file) {
        GfxEncodedBufferRelease(&buffer);
        return 0;
    }
    if (fwrite(buffer.data, 1u, buffer.size, file) != buffer.size) {
        fclose(file);
        GfxEncodedBufferRelease(&buffer);
        return 0;
    }
    fclose(file);
    GfxEncodedBufferRelease(&buffer);
    return 1;
}

/** `Out/Desktop.png` unless the environment names somewhere else. */
static inline const char *DesktopCheckpointPath(void)
{
    const char *override = getenv("ROBO_DESKTOP_PNG");

    return (override && override[0] != '\0') ? override : "Out/Desktop.png";
}

#endif /* ROBO_DESKTOP_TEST_SUPPORT_H */
