/*
 * RoboDesktop's host launcher (Phase 11.13) — see RoboDesktopHost.h.
 *
 * The whole of the checkpoint on a host, in two phases:
 *
 *   offscreen   the scene renders into its own screen target with no window at all, and the frame is
 *               *read*: exact pixels at named places, the shell's own frame record, and — the check that
 *               matters most — the two counts that say whether text reached the picture
 *               (`labels` against `labelsUnshaped`). A run with no desktop session still verifies every
 *               pixel, which is what makes this usable from a script.
 *
 *   windowed    the same scene in a real native window: `DesktopCreate` creates it, the shell's
 *               `present` door drives its client area, and pointer input is delivered through
 *               `WindowObserveMouse` so a press travels observe → queue → poll → hit test → action.
 *               The last thing this phase does is press the window's close button, because that is the only
 *               shell action that *destroys* something and the only one a picture cannot prove.
 *
 * What is asserted, and why these things: the panel is the shell's own surface, the work area's corner is
 * the desktop's, the window's title band is drawn, the client area's first pixels are the document's *page*
 * background (a value only the CSS the scene ships can produce), the one accent appears both in the panel
 * (the identity mark) and in the document (the button), and every label was shaped by a real face. Each of
 * those fails if one stage of the pipeline stops doing its job, which is the property a checkpoint is for.
 *
 * Exit codes: 0 verified, 1 a stage or probe failed, 2 a usage error.
 */

#include "RoboDesktop/RoboDesktopHost.h"

#include "RoboBase/RoboBase.h"
#include "RoboCSS/RoboCSS.h"
#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopDocument.h"
#include "RoboDesktop/RoboDesktopScene.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendHost.h"
#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontLoader.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontSystemPack.h"
#include "RoboTypography/RoboTypography.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboLayout/RoboLayout.h"
#include "RoboMem/RoboMem.h"
#include "RoboPaint/RoboPaint.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindowMouse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The face the checkpoint draws with, as the *build* knows where it is. The fallback is the path inside the
 * runtime tree, so a source tree compiled by hand still finds it. */
#if defined(RoboFontAssetPathFromBuild)
#include "robo_font_asset_path.h"
#endif
#ifndef RoboFontAssetPath
#define RoboFontAssetPath "System/Fonts/UI/Inter.ttf"
#endif

/* The system typeface pack's root, as the *build* knows where it is (Phase 12.3). The launcher opens the
 * pack from here, which is the same directory the image installs to `/System/Fonts/` -- so the roles the
 * checkpoint draws with are resolved by the code path the guest runs, not by a host-only shortcut. */
#if defined(RoboSystemFontRootFromBuild)
#include "robo_system_font_root.h"
#endif
#ifndef RoboSystemFontRoot
#define RoboSystemFontRoot "System/Fonts"
#endif

#define HOST_KEY "RoboDesktop"

#define HOST_FRAME_WAIT_MS 16u
#define HOST_HOLD_DEFAULT_MS 4000u
#define HOST_LATE_WAIT_MS 64u
#define HOST_ACTION_ATTEMPTS 40u

/* The checkpoint's status label. A fixed string rather than a clock, because a launcher that stamped the
 * time into a frame would produce a different picture every run — and this file's whole job is to make one
 * picture checkable. */
#define HOST_STATUS_TEXT "11.13"

typedef struct hostReport {
    int checks;
    int failures;
} HostReport;

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

static GfxColorRgba8 HostColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    GfxColorRgba8 color;

    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

/* RGB-only compare: alpha packing differs between RGBA8 and BGRA8 targets, and every probe here is about
 * *which* colour landed rather than about its storage order. */
static int HostColorIs(GfxColorRgba8 a, GfxColorRgba8 b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void HostProbeTrue(int condition, const char *what, HostReport *report)
{
    report->checks++;
    if (condition) {
        printf("probe OK   %s\n", what);
        return;
    }
    report->failures++;
    printf("probe FAIL %s\n", what);
}

static void HostProbePixel(const GfxSurface *surface,
                             int32_t x,
                             int32_t y,
                             GfxColorRgba8 expected,
                             const char *what,
                             HostReport *report)
{
    GfxColorRgba8 got;

    report->checks++;
    memset(&got, 0, sizeof(got));
    if (!surface || x < 0 || y < 0 ||
        GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &got) != RoboOk) {
        report->failures++;
        printf("probe FAIL %s  no pixel at %d,%d\n", what, (int)x, (int)y);
        return;
    }
    if (!HostColorIs(got, expected)) {
        report->failures++;
        printf("probe FAIL %s  pixel %d,%d is %u,%u,%u expected %u,%u,%u\n", what, (int)x, (int)y,
               (unsigned)got.r, (unsigned)got.g, (unsigned)got.b, (unsigned)expected.r,
               (unsigned)expected.g, (unsigned)expected.b);
        return;
    }
    printf("probe OK   %s\n", what);
}

/** Non-zero when @p color appears anywhere in @p rect. */
static int HostRectHasColor(const GfxSurface *surface,
                               GfxRect rect,
                               GfxColorRgba8 color)
{
    int32_t y;

    if (!surface) {
        return 0;
    }
    for (y = rect.y; y < rect.y + rect.height; y++) {
        int32_t x;

        for (x = rect.x; x < rect.x + rect.width; x++) {
            GfxColorRgba8 got;

            if (x < 0 || y < 0) {
                continue;
            }
            if (GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &got) != RoboOk) {
                continue;
            }
            if (HostColorIs(got, color)) {
                return 1;
            }
        }
    }
    return 0;
}

static void HostProbeRegionHasColor(const GfxSurface *surface,
                                        GfxRect rect,
                                        GfxColorRgba8 color,
                                        const char *what,
                                        HostReport *report)
{
    report->checks++;
    if (HostRectHasColor(surface, rect, color)) {
        printf("probe OK   %s\n", what);
        return;
    }
    report->failures++;
    printf("probe FAIL %s  no %u,%u,%u inside %d,%d,%d,%d\n", what, (unsigned)color.r,
           (unsigned)color.g, (unsigned)color.b, (int)rect.x, (int)rect.y, (int)rect.width,
           (int)rect.height);
}

/* ------------------------------------------------------------------ */
/* The face                                                            */
/* ------------------------------------------------------------------ */

typedef struct hostFace {
    FontSource *source;
    FontFace *face;
} HostFace;

/**
 * Load the checkpoint's face from @p path.
 *
 * A missing or unreadable file is **not** fatal: a desktop with no face draws its geometry and its
 * document's fills and counts the labels it could not shape, which `RoboDesktop.h` treats as a state rather
 * than an error. What is fatal is a file that is there and does not parse — that is a broken asset, and a
 * checkpoint that silently degraded to a textless desktop would report the wrong thing about the pipeline.
 *
 * @return 1 loaded, 0 absent, -1 present but unusable.
 */
static int HostFaceLoad(const char *path, HostFace *out)
{
    FILE *file;
    long size;
    unsigned char *bytes;
    size_t readBytes;

    out->source = NULL;
    out->face = NULL;
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    size = ftell(file);
    if (size <= 0l || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return -1;
    }
    readBytes = fread(bytes, 1u, (size_t)size, file);
    fclose(file);
    if (readBytes != (size_t)size) {
        free(bytes);
        return -1;
    }

    /* `create_memory_copy` copies, so the file's bytes stop being this file's business here. */
    if (FontSourceCreateMemoryCopy(bytes, readBytes, &out->source) != RoboOk) {
        free(bytes);
        return -1;
    }
    free(bytes);
    if (FontFaceCreate(out->source, 0, &out->face) != RoboOk) {
        FontSourceRelease(out->source);
        out->source = NULL;
        return -1;
    }
    return 1;
}

static void HostFaceRelease(HostFace *face)
{
    if (face->face) {
        FontFaceRelease(face->face);
        face->face = NULL;
    }
    if (face->source) {
        FontSourceRelease(face->source);
        face->source = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* The system typeface pack, and the typography resolver over it       */
/* ------------------------------------------------------------------ */

typedef struct hostTypography {
    FontRegistry *registry;
    FontLoader *loader;
    FontSystemPack *pack;
    FontFallbackResolver *fallback;
    TypographyResolver *resolver;
} HostTypography;

/**
 * Read @p path whole for the pack, into a `RoboMem` buffer it releases with `RoboFree`.
 *
 * The pack's only IO is "read this path whole", and the three answers are `FontDiscoveryIo`'s three: a
 * missing file is `RoboNotFound`, a file that will not read whole is `RoboParseError`, and a buffer that
 * would not allocate is `RoboOutOfMemory`. A file larger than @p maxSize is refused rather than truncated,
 * because a truncated *manifest* would parse as a different manifest.
 */
static Result HostPackRead(void *userdata,
                           const char *path,
                           size_t maxSize,
                           uint8_t **outBytes,
                           size_t *outSize)
{
    FILE *file;
    long size;
    uint8_t *bytes;

    (void)userdata;
    if (!path || !outBytes || !outSize) {
        return RoboInvalidArgument;
    }
    *outBytes = NULL;
    *outSize = 0u;
    file = fopen(path, "rb");
    if (!file) {
        return RoboNotFound;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return RoboParseError;
    }
    size = ftell(file);
    if (size <= 0L || (size_t)size > maxSize || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return RoboParseError;
    }
    bytes = (uint8_t *)Malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return RoboOutOfMemory;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        RoboFree(bytes);
        fclose(file);
        return RoboParseError;
    }
    fclose(file);
    *outBytes = bytes;
    *outSize = (size_t)size;
    return RoboOk;
}

static void HostTypographyClose(HostTypography *typography);

/**
 * Open the system typeface pack under @p root and build the resolver the shell draws from.
 *
 * A failure here is **not** fatal, and that is the same posture `HostFaceLoad` takes: a pack that will not
 * open means the shell draws with the caller's single face and the frame record counts the labels it could
 * not shape. What is *reported* is which step failed, because "the checkpoint ran with no typography" and
 * "the checkpoint ran with the pack" are different checkpoints and a reader should not have to guess.
 *
 * @return 1 opened, 0 unavailable.
 */
static int HostTypographyOpen(HostTypography *out)
{
    FontLoaderConfig loaderConfig;
    FontSystemPackConfig packConfig;
    RoboTypographyResolverConfig resolverConfig;
    FontDiscoveryIo io;

    memset(out, 0, sizeof(*out));
    if (FontRegistryCreate(&out->registry) != RoboOk) {
        printf("typography: the font registry could not be created\n");
        return 0;
    }
    FontLoaderConfigInit(&loaderConfig);
    if (FontLoaderCreate(out->registry, &loaderConfig, &out->loader) != RoboOk) {
        printf("typography: the font loader could not be created\n");
        HostTypographyClose(out);
        return 0;
    }
    memset(&io, 0, sizeof(io));
    io.RoboReadFile = HostPackRead;
    FontSystemPackConfigInit(&packConfig);
    packConfig.root = RoboSystemFontRoot;
    packConfig.io = &io;
    if (FontSystemPackOpen(out->registry, out->loader, &packConfig, &out->pack) != RoboOk) {
        printf("typography: %s did not open as a system typeface pack\n", RoboSystemFontRoot);
        HostTypographyClose(out);
        return 0;
    }
    if (FontFallbackResolverCreate(out->registry, NULL, &out->fallback) != RoboOk) {
        printf("typography: the fallback resolver could not be created\n");
        HostTypographyClose(out);
        return 0;
    }
    RoboTypographyResolverConfigInit(&resolverConfig);
    resolverConfig.pack = out->pack;
    resolverConfig.fallback = out->fallback;
    if (RoboTypographyResolverCreate(&resolverConfig, &out->resolver, NULL) != RoboOk) {
        printf("typography: the resolver could not be created\n");
        HostTypographyClose(out);
        return 0;
    }
    printf("typography: %s (%u family, %u face) -> %u role(s)\n", RoboSystemFontRoot,
           (unsigned)FontSystemPackFamilyCount(out->pack),
           (unsigned)FontSystemPackRegisteredFaces(out->pack),
           (unsigned)RoboTypographyRoleCount);
    return 1;
}

static void HostTypographyClose(HostTypography *typography)
{
    if (!typography) {
        return;
    }
    RoboTypographyResolverDestroy(typography->resolver);
    typography->resolver = NULL;
    FontFallbackResolverDestroy(typography->fallback);
    typography->fallback = NULL;
    FontSystemPackClose(typography->pack);
    typography->pack = NULL;
    FontLoaderDestroy(typography->loader);
    typography->loader = NULL;
    FontRegistryDestroy(typography->registry);
    typography->registry = NULL;
}

/* ------------------------------------------------------------------ */
/* The checkpoint's picture                                            */
/* ------------------------------------------------------------------ */

/** Encode @p surface and write it to @p path. Non-zero on success. */
static int HostWritePng(const GfxSurface *surface, const char *path)
{
    GfxEncodeOptions options;
    GfxEncodedBuffer buffer;
    FILE *file;

    if (!surface || !path) {
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

/**
 * Read the frame back and assert what only the whole pipeline could have put there.
 *
 * Every expectation below is a *documented* value rather than a snapshot: the panel's white, the desktop
 * surface's `#EDEDEF`, the window's title band, the page background `#F5F5F7` that only the scene's CSS
 * produces, and the one accent `#0E7AFF` twice — the shell's identity mark and the document's button.
 */
static void HostProbeScene(DesktopScene *scene,
                             const DesktopFrameRecord *frame,
                             HostReport *report)
{
    Desktop *desktop = DesktopSceneDesktop(scene);
    DesktopWindowId window = DesktopSceneWindow(scene);
    const GfxSurface *surface = DesktopFrameSurface(desktop);
    DesktopGeometry geometry = DesktopGeometryOf(desktop);
    GfxSize size = DesktopScreenSize(desktop);
    DesktopTheme theme;
    DesktopWindowMetrics windowMetrics;
    GfxRect bounds;
    GfxRect client;
    DesktopDocumentReport documentReport;

    DesktopThemeInit(&theme);
    DesktopWindowMetricsInit(&windowMetrics);
    memset(&bounds, 0, sizeof(bounds));
    memset(&client, 0, sizeof(client));

    HostProbePixel(surface, size.width - 2, 1, theme.panelSurface, "panel surface", report);
    HostProbePixel(surface, size.width - 2, size.height - 2, theme.desktopSurface,
                     "desktop surface at the work area's corner", report);
    HostProbeRegionHasColor(surface, geometry.panel, theme.identityAccent,
                                "the shell's one accent in the panel", report);

    if (window != RoboDesktopWindowIdNone &&
        DesktopWindowBounds(desktop, window, &bounds) == RoboOk) {
        HostProbeRegionHasColor(
            surface,
            MakeGfxRect(bounds.x, bounds.y, bounds.width,
                          windowMetrics.border + windowMetrics.titleHeight),
            theme.windowTitleSurface, "the window's title bar", report);
    } else {
        HostProbeTrue(0, "the shell holds a window to probe", report);
    }

    if (window != RoboDesktopWindowIdNone &&
        DesktopWindowClientBounds(desktop, window, &client) == RoboOk && client.width > 2 &&
        client.height > 2) {
        /* The page's own background, which is the scene's stylesheet and nothing else. */
        HostProbePixel(surface, client.x + 2, client.y + 2, HostColor(245u, 245u, 247u, 255u),
                         "the document's page background", report);
        HostProbeRegionHasColor(surface, client, theme.identityAccent,
                                    "the document's own accent in the client area", report);
    } else {
        HostProbeTrue(0, "the window has a client area to probe", report);
    }

    /* What the frame record says, including the two counts that are the text verdict. */
    HostProbeTrue(frame->windows >= 1u, "one shell window painted", report);
    HostProbeTrue(frame->contentRuns >= 1u, "the window's document reached the rasteriser", report);
    HostProbeTrue(frame->labels >= 3u, "the panel's three labels drew", report);
    HostProbeTrue(frame->labelsUnshaped == 0u, "every label was shaped by a real face", report);

    /* And what the document itself reported about the pass it ran. */
    memset(&documentReport, 0, sizeof(documentReport));
    if (GetDesktopDocumentReport(DesktopSceneDocument(scene), &documentReport) == RoboOk) {
        HostProbeTrue(documentReport.elements >= 4u,
                        "the document's elements reached the layout bridge", report);
        HostProbeTrue(documentReport.texts >= 4u, "the document laid out its text runs", report);
        HostProbeTrue(documentReport.commands >= 1u, "the document's display list is not empty",
                        report);
    } else {
        HostProbeTrue(0, "the document reported the pass it ran", report);
    }
}

static void HostPrintSummary(Desktop *desktop, const DesktopFrameRecord *frame)
{
    DesktopStats stats;
    char dump[RoboDesktopDumpLineMax * 8u];

    memset(&stats, 0, sizeof(stats));
    (void)GetDesktopStats(desktop, &stats);
    printf("frame: %ux%u fills=%lu labels=%lu unshaped=%lu windows=%lu content_runs=%lu "
           "content_pixels=%lu\n",
           (unsigned)frame->width, (unsigned)frame->height, (unsigned long)frame->fills,
           (unsigned long)frame->labels, (unsigned long)frame->labelsUnshaped,
           (unsigned long)frame->windows, (unsigned long)frame->contentRuns,
           (unsigned long)frame->contentPixels);
    printf("shell: frames=%lu presentations=%lu resizes=%lu windows_added=%lu closed=%lu "
           "pointer_actions=%lu\n",
           (unsigned long)stats.frames, (unsigned long)stats.presentations, (unsigned long)stats.resizes,
           (unsigned long)stats.windowsAdded, (unsigned long)stats.windowsClosed,
           (unsigned long)stats.pointerActions);
    memset(dump, 0, sizeof(dump));
    if (DesktopWrite(desktop, dump, sizeof(dump), NULL, NULL) == RoboOk) {
        printf("%s", dump);
    }
}

/* ------------------------------------------------------------------ */
/* Phase 1 — offscreen                                                 */
/* ------------------------------------------------------------------ */

static int HostRunOffscreen(FontFace *face, TypographyResolver *typography, const char *shot,
                            HostReport *report)
{
    DesktopSceneConfig config;
    DesktopScene *scene = NULL;
    DesktopSceneStep step;
    DesktopDiag diag;
    Result result;

    memset(&diag, 0, sizeof(diag));
    DesktopSceneConfigInit(&config);
    config.face = face;
    config.typography = typography;
    config.RoboCreateWindow = 0;
    config.statusText = HOST_STATUS_TEXT;

    result = DesktopSceneOpen(&config, &scene, &diag);
    if (result != RoboOk) {
        printf("%s: scene_open failed code=%d message=%s\n", HOST_KEY, (int)result,
               diag.message ? diag.message : "(none)");
        return 0;
    }

    result = GetDesktopSceneStep(scene, 0u, &step, &diag);
    if (result != RoboOk || !step.painted) {
        printf("%s: the offscreen frame did not paint (code=%d)\n", HOST_KEY, (int)result);
        DesktopSceneClose(scene);
        return 0;
    }

    /* A screen-only scene presents nowhere, and says so rather than pretending. */
    HostProbeTrue(step.presented == 0, "a scene with no window presents nowhere", report);
    HostPrintSummary(DesktopSceneDesktop(scene), &step.frame);
    HostProbeScene(scene, &step.frame, report);

    if (shot) {
        report->checks++;
        if (HostWritePng(DesktopFrameSurface(DesktopSceneDesktop(scene)), shot)) {
            printf("probe OK   wrote %s\n", shot);
        } else {
            report->failures++;
            printf("probe FAIL could not write %s\n", shot);
        }
    }

    DesktopSceneClose(scene);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Phase 2 — a real window                                             */
/* ------------------------------------------------------------------ */

/** Push one pointer state through the production queue: observe → queue → poll → hit test → action. */
static void HostInjectPointer(WindowId window, int32_t x, int32_t y, int pressed)
{
    MouseEvent event;

    MouseEventInit(&event);
    event.windowId = window;
    event.type = RoboMouseEventMove;
    event.clientX = x;
    event.clientY = y;
    (void)WindowObserveMouse(window, &event);
    event.type = pressed ? RoboMouseEventButtonDown : RoboMouseEventButtonUp;
    event.button = RoboMouseButtonLeft;
    (void)WindowObserveMouse(window, &event);
}

/**
 * A point inside the app window's close button, found through the shell's own hit test.
 *
 * Searched for rather than computed: the close button's position is the *shell's* business
 * (`RoboDesktopWindow.h`'s metrics and `RoboDesktopChrome.c`'s layout), and a launcher that repeated that
 * arithmetic would be a second implementation of it — the one that would go stale silently.
 */
static int HostFindClosePoint(Desktop *desktop, DesktopWindowId window,
                                 int32_t *outX, int32_t *outY)
{
    GfxRect bounds;
    DesktopWindowMetrics metrics;
    int32_t y;

    if (DesktopWindowBounds(desktop, window, &bounds) != RoboOk) {
        return 0;
    }
    /* The title bar's vertical middle: the button is centred in it, and the band is
     * `border .. border + titleHeight` (`RoboDesktopTypes.h`). */
    DesktopWindowMetricsInit(&metrics);
    y = bounds.y + metrics.border + metrics.titleHeight / 2;
    {
        int32_t x;

        for (x = bounds.x + bounds.width - 1; x >= bounds.x; x--) {
            DesktopHit hit;

            if (DesktopHitTest(desktop, MakeGfxPoint(x, y), &hit) != RoboOk) {
                return 0;
            }
            if (hit.area == RoboDesktopAreaWindowClose) {
                *outX = x;
                *outY = y;
                return 1;
            }
        }
    }
    return 0;
}

static int HostRunWindowed(FontFace *face, TypographyResolver *typography, unsigned holdMs,
                           int inject, HostReport *report)
{
    DesktopSceneConfig config;
    DesktopScene *scene = NULL;
    DesktopSceneStep step;
    DesktopDiag diag;
    WindowId native = RoboWindowIdNone;
    unsigned elapsed = 0u;
    int checked = 0;
    int injected = 0;
    int closed = 0;

    memset(&diag, 0, sizeof(diag));
    if (!WindowBackendHostAvailable() || WindowBackendUseHost() != RoboOk) {
        printf("window: the host backend is unavailable — the offscreen result stands\n");
        return 1; /* not a failure: the offscreen phase already verified the picture */
    }

    DesktopSceneConfigInit(&config);
    config.face = face;
    config.typography = typography;
    config.RoboCreateWindow = 1;
    config.showCursor = 1;
    config.statusText = HOST_STATUS_TEXT;
    if (DesktopSceneOpen(&config, &scene, &diag) != RoboOk) {
        printf("%s: the windowed scene could not open (message=%s)\n", HOST_KEY,
               diag.message ? diag.message : "(none)");
        return 1; /* no interactive desktop session: the offscreen result stands */
    }
    native = DesktopAttachedWindow(DesktopSceneDesktop(scene));
    printf("window: %s native=%u\n", WindowBackendName(), (unsigned)native);

    while (elapsed < holdMs) {
        if (GetDesktopSceneStep(scene, HOST_FRAME_WAIT_MS, &step, &diag) != RoboOk) {
            printf("%s: a scene step failed (message=%s)\n", HOST_KEY,
                   diag.message ? diag.message : "(none)");
            report->failures++;
            DesktopSceneClose(scene);
            return 0;
        }
        elapsed += HOST_FRAME_WAIT_MS;

        /* The windowed picture is probed once, on the first frame it presents: every check below is
         * deterministic, so a second frame would assert the same things twice. */
        if (!checked && step.painted) {
            checked = 1;
            HostPrintSummary(DesktopSceneDesktop(scene), &step.frame);
            HostProbeScene(scene, &step.frame, report);
        }

        /* A press on the title bar, once the window exists: the shell's job is to raise and focus it. */
        if (inject && !injected && checked && native != RoboWindowIdNone) {
            GfxRect bounds;

            injected = 1;
            if (DesktopWindowBounds(DesktopSceneDesktop(scene),
                                           DesktopSceneWindow(scene),
                                           &bounds) != RoboOk) {
                report->checks++;
                report->failures++;
                printf("probe FAIL the app window has no bounds to press\n");
            } else {
                HostInjectPointer(native, bounds.x + bounds.width / 2, bounds.y + 6, 1);
                HostInjectPointer(native, bounds.x + bounds.width / 2, bounds.y + 6, 0);
            }
        }

        if (step.closeRequested) {
            closed = 1;
            break;
        }
    }

    /*
     * Injection is asynchronous — the queue is drained by a later iteration — so the *action* is asserted
     * after the events have been through the loop rather than at the moment they were posted. This is the
     * seam a checkpoint exists to cover: observe → queue → poll → hit test → raise.
     */
    if (inject && injected && !closed) {
        unsigned attempt = 0u;

        while (attempt < HOST_ACTION_ATTEMPTS) {
            attempt++;
            if (GetDesktopSceneStep(scene, HOST_FRAME_WAIT_MS, &step, &diag) != RoboOk) {
                break;
            }
            if (step.pointerActions > 0u || step.closeRequested) {
                break;
            }
        }
        {
            DesktopStats stats;

            memset(&stats, 0, sizeof(stats));
            (void)GetDesktopStats(DesktopSceneDesktop(scene), &stats);
            HostProbeTrue(stats.pointerActions >= 1u,
                            "a press on the title bar reached the shell through the event queue", report);
            HostProbeTrue(DesktopWindowFocused(DesktopSceneDesktop(scene)) ==
                                DesktopSceneWindow(scene),
                            "the pressed window is the focused one", report);
        }
    }

    /* The one action a picture cannot prove: the close button. Pressed last, because it retires the window
     * and the checkpoint is then over. */
    if (!closed && DesktopSceneWindow(scene) != RoboDesktopWindowIdNone) {
        int32_t closeX = 0;
        int32_t closeY = 0;

        if (native != RoboWindowIdNone &&
            HostFindClosePoint(DesktopSceneDesktop(scene), DesktopSceneWindow(scene),
                                  &closeX, &closeY)) {
            unsigned attempt = 0u;

            HostInjectPointer(native, closeX, closeY, 1);
            HostInjectPointer(native, closeX, closeY, 0);
            while (attempt < HOST_ACTION_ATTEMPTS) {
                attempt++;
                if (GetDesktopSceneStep(scene, HOST_LATE_WAIT_MS, &step, &diag) != RoboOk) {
                    break;
                }
                if (step.closeRequested) {
                    closed = 1;
                    break;
                }
            }
            {
                DesktopStats stats;

                memset(&stats, 0, sizeof(stats));
                (void)GetDesktopStats(DesktopSceneDesktop(scene), &stats);
                HostProbeTrue(stats.windowsClosed >= 1u,
                                "the close button closed the window it belongs to", report);
            }
        } else {
            printf("window: no close button found to press\n");
        }
    }

    DesktopSceneClose(scene);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

static unsigned HostParseMs(const char *text, unsigned fallback)
{
    long value;

    if (!text || text[0] == '\0') {
        return fallback;
    }
    value = strtol(text, NULL, 10);
    if (value < 0l) {
        return fallback;
    }
    return (unsigned)value;
}

int DesktopHostMain(int argc, char **argv)
{
    HostReport report;
    HostFace face;
    HostTypography typography;
    unsigned holdMs;
    const char *shot = NULL;
    int noWindow = 0;
    int inject = 1;
    int faceState;
    int i;

    report.checks = 0;
    report.failures = 0;
    holdMs = HostParseMs(getenv("ROBO_DESKTOP_HOLD_MS"), HOST_HOLD_DEFAULT_MS);
    shot = getenv("ROBO_DESKTOP_SHOT");
    if (getenv("ROBO_DESKTOP_NO_WINDOW") != NULL) {
        noWindow = 1;
    }

    for (i = 1; i < argc; i++) {
        const char *argument = argv[i];

        if (strcmp(argument, "--no-window") == 0) {
            noWindow = 1;
        } else if (strcmp(argument, "--no-inject-input") == 0) {
            inject = 0;
        } else if (strcmp(argument, "--hold") == 0 && i + 1 < argc) {
            holdMs = HostParseMs(argv[++i], HOST_HOLD_DEFAULT_MS);
        } else if (strcmp(argument, "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else {
            printf("desktop: unknown option %s\n", argument);
            printf("usage: %s [--no-window] [--no-inject-input] [--hold ms] [--shot path]\n", argv[0]);
            return 2;
        }
    }

    printf("RoboticelaOS — the desktop shell checkpoint\n");
    printf("document: %lu bytes of markup from RoboDesktopScene\n",
           (unsigned long)DesktopSceneMarkupLength());

    if (!BaseIsInitialized() && BaseInit() != RoboOk) {
        printf("desktop: robo_base_init failed\n");
        return 1;
    }
    if (GraphicsInit() != RoboOk || FontInit() != RoboOk || HtmlInit() != RoboOk ||
        CssInit() != RoboOk || LayoutInit() != RoboOk || PaintInit() != RoboOk) {
        printf("desktop: the runtime's layers did not come up\n");
        return 1;
    }
    if (DisplayInit() == RoboOk) {
        (void)DisplayBackendUseHost();
    }
    if (WindowInit() != RoboOk) {
        printf("desktop: robo_window_init failed\n");
        return 1;
    }

    faceState = HostFaceLoad(RoboFontAssetPath, &face);
    if (faceState > 0) {
        printf("face: %s\n", RoboFontAssetPath);
    } else if (faceState == 0) {
        printf("face: %s is not there — the checkpoint will draw no glyphs\n", RoboFontAssetPath);
    } else {
        printf("desktop: %s did not parse as a font\n", RoboFontAssetPath);
        return 1;
    }

    /*
     * The system typeface pack, and the resolver the shell's roles come from (Phase 12.3). Opened after
     * `FontInit` because it registers faces through the same discovery/registry/loader the guest uses, and
     * *not* fatal when it is absent: the checkpoint then falls back to the single face, which is a worse
     * checkpoint but still a checkpoint.
     */
    (void)HostTypographyOpen(&typography);

    if (!HostRunOffscreen(face.face, typography.resolver, shot, &report)) {
        HostTypographyClose(&typography);
        HostFaceRelease(&face);
        return 1;
    }
    if (!noWindow) {
        (void)HostRunWindowed(face.face, typography.resolver, holdMs, inject, &report);
    }

    HostTypographyClose(&typography);
    HostFaceRelease(&face);
    WindowShutdown();
    DisplayShutdown();
    PaintShutdown();
    LayoutShutdown();
    CssShutdown();
    HtmlShutdown();
    FontShutdown();
    GraphicsShutdown();

    printf("checks: %d passed=%d failed=%d\n", report.checks, report.checks - report.failures,
           report.failures);
    printf("RESULT: %s\n", report.failures == 0 ? "PASS" : "FAIL");
    return report.failures == 0 ? 0 : 1;
}
