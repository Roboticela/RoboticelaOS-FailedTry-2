/*
 * RoboDesktop's checkpoint-scene suite (Phase 11.13) — `desktop.scene.*`.
 *
 * The scene is what the host launcher and the guest program both drive, so the cases below are about the
 * two things that makes it worth having: it is *one* definition (the words and the arrangement, asserted
 * against the same doors the launchers use), and it is *deterministic* (a step is a function of the
 * scene's state, and two steps of one state produce one picture).
 *
 * `desktop.scene.system_face` is the other half. The OS ships a face — the system typeface pack's
 * `System/Fonts/UI/Inter.ttf`, installed at `/System/Fonts/UI/Inter.ttf` — and the checkpoint's whole visual claim depends on it
 * parsing and covering the label's glyphs. So that case loads the shipped bytes, asserts what the face is,
 * and then asserts the two counts that are the text verdict in a frame: every label shaped with the face,
 * and every label *counted* without one.
 */

#include "RoboDesktopTestSupport.h"

/* The scene itself, which the support header does not pull in: the support is about the shell and its
 * document, while this suite is about the fixture that puts the two together. */
#include "RoboDesktop/RoboDesktopScene.h"

#if defined(RoboFontAssetPathFromBuild)
#include "robo_font_asset_path.h"
#endif
#ifndef RoboFontAssetPath
#define RoboFontAssetPath "System/Fonts/UI/Inter.ttf"
#endif

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* 1. Opening the scene                                                */
/* ------------------------------------------------------------------ */

static void TestSceneOpen(void)
{
    DesktopSceneConfig config;
    DesktopScene *scene = NULL;
    DesktopSceneStep step;
    DesktopWindowId window;
    GfxRect client;
    GfxSize screen;
    GfxSize viewport;
    const DesktopDocument *document;

    DesktopFixture();
    DesktopSceneConfigInit(NULL); /* a no-op, not a crash */
    DesktopSceneConfigInit(&config);

    /* The defaults first: a scene with no size is the panel's design size rather than a zero-sized screen,
     * it has no face, and it asks for its own native window. */
    RoboCheckEqInt(config.width, RoboDesktopSceneDefaultWidth);
    RoboCheckEqInt(config.height, RoboDesktopSceneDefaultHeight);
    RoboCheck(config.face == NULL);
    RoboCheckEqInt(config.RoboCreateWindow, 1);
    config.width = 640;
    config.height = 480;
    config.RoboCreateWindow = 0;

    RoboCheckEqInt(DesktopSceneOpen(&config, &scene, NULL), RoboOk);
    RoboCheck(scene != NULL);
    RoboCheckEqInt(DesktopSceneIsLive(scene), 1);

    screen = DesktopScreenSize(DesktopSceneDesktop(scene));
    RoboCheckEqInt(screen.width, 640);
    RoboCheckEqInt(screen.height, 480);

    /* One window, titled by the header, and it is the focused one. */
    RoboCheckEqInt((int)DesktopWindowCount(DesktopSceneDesktop(scene)), 1);
    window = DesktopSceneWindow(scene);
    RoboCheck(window != RoboDesktopWindowIdNone);
    RoboCheckStrEq(DesktopWindowTitle(DesktopSceneDesktop(scene), window),
                      RoboDesktopSceneWindowTitle);
    RoboCheckEqInt((int)DesktopWindowFocused(DesktopSceneDesktop(scene)), (int)window);

    /* The window sits inside the work area, with the margin the header names on every side. */
    {
        GfxRect work = DesktopWorkArea(DesktopSceneDesktop(scene));
        GfxRect bounds;

        RoboCheckEqInt(DesktopWindowBounds(DesktopSceneDesktop(scene), window, &bounds),
                          RoboOk);
        RoboCheckEqInt(bounds.width, work.width - RoboDesktopSceneMargin * 2);
        RoboCheckEqInt(bounds.height, work.height - RoboDesktopSceneMargin * 2);
        RoboCheck(bounds.x >= work.x && bounds.y >= work.y);
    }

    /* The window's document was built for the client size the *shell* computed. */
    document = DesktopSceneDocument(scene);
    RoboCheck(document != NULL);
    RoboCheckEqInt(DesktopWindowClientBounds(DesktopSceneDesktop(scene), window,
                                                        &client),
                      RoboOk);
    viewport = DesktopDocumentViewport(document);
    RoboCheckEqInt(viewport.width, client.width);
    RoboCheckEqInt(viewport.height, client.height);

    /* And it paints: one window, its contents rasterised, nothing clipped. */
    RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, &step, NULL), RoboOk);
    RoboCheckEqInt(step.painted, 1);
    RoboCheckEqInt((int)step.frame.windows, 1);
    RoboCheckEqInt((int)step.frame.windowsTotal, 1);
    RoboCheckEqInt(step.frame.contentRuns >= 1u, 1);
    RoboCheckEqInt(step.frame.contentPixels >= 1u, 1);
    RoboCheckEqInt((int)step.frame.clipped, 0);
    RoboCheckEqInt(DesktopFrameRecordIsValid(&step.frame), 1);
    /* A scene with no native window presents nowhere, and says so rather than claiming a presentation. */
    RoboCheckEqInt(step.presented, 0);

    /* NULL is answered, not assumed. */
    RoboCheckEqInt(DesktopSceneOpen(NULL, &scene, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopSceneOpen(&config, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopSceneIsLive(NULL), 0);
    RoboCheckEqInt(DesktopSceneWindow(NULL), RoboDesktopWindowIdNone);
    RoboCheck(DesktopSceneDocument(NULL) == NULL);
    RoboCheck(DesktopSceneDesktop(NULL) == NULL);
    RoboCheckEqInt(GetDesktopSceneStep(NULL, 0u, &step, NULL), RoboInvalidArgument);
    DesktopSceneInvalidate(NULL); /* a no-op, not a crash */
    DesktopSceneClose(NULL);

    /*
     * And the scene is gone. `close` frees it, so the pointer is *dead* rather than "closed" -- the door
     * that answers "is there a scene" is armed against NULL and must never be called on a released one, so
     * what is asserted here is the only thing a caller can still ask: that closing nothing is safe.
     */
    DesktopSceneClose(scene);
    DesktopSceneClose(NULL);
}

/* ------------------------------------------------------------------ */
/* 2. The scene's own words                                            */
/* ------------------------------------------------------------------ */

/** The class name following `class='` at @p at, or NULL. */
static const char *SceneClassAt(const char *at, size_t *outLength)
{
    const char *end;

    if (!at || strncmp(at, "class='", 7u) != 0) {
        return NULL;
    }
    at += 7u;
    end = strchr(at, '\'');
    if (!end) {
        return NULL;
    }
    *outLength = (size_t)(end - at);
    return at;
}

static void TestSceneWords(void)
{
    const char *markup = DesktopSceneMarkup();
    const char *style = DesktopSceneStyle();
    const char *cursor;
    size_t classes = 0u;

    DesktopFixture();

    RoboCheck(markup != NULL && markup[0] != '\0');
    RoboCheck(style != NULL && style[0] != '\0');
    RoboCheckEqInt((int)strlen(markup), (int)DesktopSceneMarkupLength());
    /* The document the scene builds is the markup it reports: a launcher that printed the length would
     * otherwise be printing a second copy's length. */
    RoboCheck(DesktopSceneMarkupLength() > 64u);

    /*
     * Every class the markup names is styled by the sheet beside it. This is the drift check that keeps the
     * two halves of the checkpoint one document: a class renamed in the markup alone would lay out as an
     * unstyled block and still *look* like a page, which is exactly the failure a picture cannot show.
     */
    for (cursor = markup; (cursor = strchr(cursor, 'c')) != NULL; cursor++) {
        size_t length = 0u;
        const char *name;

        if (strncmp(cursor, "class='", 7u) != 0) {
            continue;
        }
        name = SceneClassAt(cursor, &length);
        if (!name || length == 0u) {
            continue;
        }
        {
            char selector[64];

            RoboCheck(length + 1u < sizeof(selector));
            selector[0] = '.';
            memcpy(selector + 1, name, length);
            selector[length + 1u] = '\0';
            if (!strstr(style, selector)) {
                TestFail(__FILE__, __LINE__, "the sheet does not style '%s'", selector);
            }
        }
        classes++;
    }
    RoboCheck(classes >= 3u);

    /* The document that came out of those words has the elements they name. */
    {
        DesktopSceneConfig config;
        DesktopScene *scene = NULL;
        DesktopDocumentReport report;

        DesktopSceneConfigInit(&config);
        config.width = 800;
        config.height = 600;
        config.RoboCreateWindow = 0;
        RoboCheckEqInt(DesktopSceneOpen(&config, &scene, NULL), RoboOk);
        memset(&report, 0, sizeof(report));
        RoboCheckEqInt(GetDesktopDocumentReport(DesktopSceneDocument(scene), &report),
                          RoboOk);
        /*
         * And the document those words produced is the **whole desktop** rather than a fragment of it:
         * the scene's own frame draws the chrome, and everything inside the window comes from this
         * picture, so a page that reached the image but laid out to nothing would still present a
         * window — which is exactly the failure a screenshot cannot show and these numbers can.
         */
        RoboCheck(report.elements >= 60u);
        RoboCheck(report.texts >= 10u);
        RoboCheck(report.commands >= 100u);
        /* The document is laid out for the **client area** the shell computed, which is why the
         * viewport is a positive size rather than the scene's own. */
        RoboCheck(DesktopDocumentViewport(DesktopSceneDocument(scene)).width > 0);
        RoboCheck(report.elements >= 5u);
        RoboCheck(report.commands >= 1u);
        DesktopSceneClose(scene);
    }

    /* A step record clears to a step that did nothing. */
    {
        DesktopSceneStep step;

        DesktopSceneStepInit(&step);
        RoboCheckEqInt(step.painted, 0);
        RoboCheckEqInt(step.presented, 0);
        RoboCheckEqInt((int)step.events, 0);
        RoboCheckEqInt((int)step.frame.width, 0);
        DesktopSceneStepInit(NULL); /* a no-op, not a crash */
    }
}

/* ------------------------------------------------------------------ */
/* 3. Stepping, and determinism                                        */
/* ------------------------------------------------------------------ */

static void TestSceneStep(void)
{
    DesktopSceneConfig config;
    DesktopScene *scene = NULL;
    DesktopSceneStep first;
    DesktopSceneStep idle;
    DesktopSceneStep second;
    uint64_t checksum;

    DesktopFixture();
    DesktopSceneConfigInit(&config);
    config.width = 640;
    config.height = 480;
    config.RoboCreateWindow = 0;
    RoboCheckEqInt(DesktopSceneOpen(&config, &scene, NULL), RoboOk);

    RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, &first, NULL), RoboOk);
    RoboCheckEqInt(first.painted, 1);
    checksum = first.frame.checksum;

    /*
     * Nothing happened between the two calls, so the second paints nothing: a scene that repainted an
     * unchanged screen would be a scene that could never be compared frame to frame, which is the property
     * both launchers depend on.
     */
    RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, &idle, NULL), RoboOk);
    RoboCheckEqInt(idle.painted, 0);
    RoboCheckEqInt((int)idle.frame.width, 0);

    /* The same state painted again is the same picture, byte for byte. */
    DesktopSceneInvalidate(scene);
    RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, &second, NULL), RoboOk);
    RoboCheckEqInt(second.painted, 1);
    RoboCheckEqInt((int)second.frame.checksum, (int)checksum);
    /*
     * `contentRuns` is deliberately outside this claim. It counts *work*, not picture: the first paint
     * found the window's content dirty and rasterized it, and this one found it already cached, which is
     * the saving the field exists to report. Comparing it here would only ever ask that a scene never
     * benefit from its own cache -- so the work counter is normalized to the first frame's and the
     * difference is pinned on its own line instead, as the *expected* direction of that saving.
     */
    {
        DesktopFrameRecord comparable = second.frame;

        RoboCheckEqInt((int)second.frame.contentRuns, 0);
        comparable.contentRuns = first.frame.contentRuns;
        RoboCheckEqInt(DesktopFrameRecordEqual(&first.frame, &comparable), 1);
    }
    RoboCheckEqInt((int)DesktopFrameChecksum(DesktopSceneDesktop(scene)),
                      (int)checksum);

    /* A step's record may be omitted: the work still happens. */
    DesktopSceneInvalidate(scene);
    RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, NULL, NULL), RoboOk);
    RoboCheckEqInt((int)DesktopFrameChecksum(DesktopSceneDesktop(scene)),
                      (int)checksum);

    DesktopSceneClose(scene);
}

/* ------------------------------------------------------------------ */
/* 4. The system face                                                  */
/* ------------------------------------------------------------------ */

/** Read @p path whole; returns NULL when it cannot be read. Caller frees. */
static unsigned char *SceneReadFile(const char *path, size_t *outSize)
{
    FILE *file = fopen(path, "rb");
    long size;
    unsigned char *bytes;

    *outSize = 0u;
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size <= 0l || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *outSize = (size_t)size;
    return bytes;
}

static void TestSceneSystemFace(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    DesktopSceneConfig config;
    DesktopScene *scene = NULL;
    DesktopScene *textless = NULL;
    DesktopSceneStep step;
    FontCmapLookupResult lookup;
    FontMetrics metrics;
    FontOutline *outline = NULL;
    unsigned char *bytes;
    size_t size = 0u;
    uint32_t glyphs = 0u;

    DesktopFixture();
    RoboCheckEqInt(FontInit(), RoboOk);

    /*
     * The shipped face. A missing file is a **failure** here rather than a skip: the image build packs
     * this file and the guest loads it from `/System/Fonts/UI/Inter.ttf`, so a suite that shrugged at its
     * absence would let a checkpoint with no text leave the building.
     */
    bytes = SceneReadFile(RoboFontAssetPath, &size);
    RoboCheckMsg(bytes != NULL, "System/Fonts/UI/Inter.ttf is unreadable");
    if (!bytes) {
        return;
    }
    RoboCheck(size > 100000u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(bytes, size, &source), RoboOk);
    free(bytes);
    RoboCheckEqInt(FontFaceCreate(source, 0, &face), RoboOk);
    RoboCheck(face != NULL);

    /*
     * A face is created with nothing parsed, and each door below answers `RoboNotSupported` until the
     * part of the font it needs has been built (`RoboFontFace.h`). So the steps come first and in the order
     * the layer above builds them: character map, metrics, outlines -- the same three the document's own
     * text pass asks for (`RoboPaintDecoration.c` parses the cmap for exactly this reason, so that a face
     * handed to the shell is usable by the time the first label is looked up).
     */
    RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);

    /* A real face, with real coverage: Inter carries thousands of glyphs. */
    RoboCheckEqInt(FontFaceGlyphCount(face, &glyphs), RoboOk);
    RoboCheck(glyphs > 500u);

    /* Every character the shell's own labels are made of is mapped, and maps to a real glyph. */
    {
        static const char *letters = "RoboticelaOS Applications Welcome 0123456789x";

        for (; *letters; letters++) {
            memset(&lookup, 0, sizeof(lookup));
            RoboCheckEqInt(FontFaceLookupGlyph(face, (uint32_t)(unsigned char)*letters,
                                                          &lookup),
                              RoboOk);
            if (*letters == ' ') {
                continue; /* a space maps to a blank glyph, which is a legitimate mapping */
            }
            if (lookup.status != RoboFontCmapMapped) {
                TestFail(__FILE__, __LINE__, "'%c' is not mapped by the system face", *letters);
            }
        }
    }

    /* Metrics that a layout can measure with, and an outline with ink in it. */
    memset(&metrics, 0, sizeof(metrics));
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    RoboCheckEqInt(FontFaceLayoutMetrics(face, &metrics), RoboOk);
    RoboCheckEqInt(metrics.valid, 1);
    RoboCheckEqInt(metrics.unitsPerEm > 0u, 1);
    RoboCheck(metrics.ascent > 0);
    RoboCheck(metrics.descent >= 0);

    memset(&lookup, 0, sizeof(lookup));
    RoboCheckEqInt(FontFaceLookupGlyph(face, (uint32_t)'A', &lookup), RoboOk);
    RoboCheckEqInt((int)lookup.status, (int)RoboFontCmapMapped);
    RoboCheckEqInt(FontFaceParseOutlines(face), RoboOk);
    RoboCheckEqInt(FontFaceGlyphOutline(face, lookup.glyphId, &outline), RoboOk);
    RoboCheck(outline != NULL);
    RoboCheck(FontOutlineContourCount(outline) >= 1u);
    RoboCheck(FontOutlineSegmentCount(outline) >= 1u);
    RoboCheckEqInt(FontOutlineHasBounds(outline), 1);
    {
        FontBbox bounds;

        memset(&bounds, 0, sizeof(bounds));
        RoboCheckEqInt(FontOutlineBounds(outline, &bounds), RoboOk);
        RoboCheck(bounds.xMax > bounds.xMin);
        RoboCheck(bounds.yMax > bounds.yMin);
    }
    FontOutlineRelease(outline);
    outline = NULL;

    /* With the face: every label the shell drew was shaped by it. */
    DesktopSceneConfigInit(&config);
    config.width = 800;
    config.height = 600;
    config.face = face;
    config.RoboCreateWindow = 0;
    config.statusText = "800x600";
    RoboCheckEqInt(DesktopSceneOpen(&config, &scene, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, &step, NULL), RoboOk);
    /* identity, applications, status, and the window's title. */
    RoboCheckEqInt((int)step.frame.labels, 4);
    RoboCheckEqInt((int)step.frame.labelsUnshaped, 0);

    /*
     * And without one: nothing is shaped, every label is *counted*, and the frame still paints. The pair of
     * numbers is the whole reason the record carries both.
     */
    config.face = NULL;
    RoboCheckEqInt(DesktopSceneOpen(&config, &textless, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopSceneStep(textless, 0u, &step, NULL), RoboOk);
    RoboCheckEqInt((int)step.frame.labels, 0);
    RoboCheckEqInt(step.frame.labelsUnshaped >= 3u, 1);
    /* The picture is still a desktop: the geometry is there, and only the glyphs are not. */
    RoboCheckEqInt(step.frame.fills >= 5u, 1);

    DesktopSceneClose(textless);
    DesktopSceneClose(scene);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/* ------------------------------------------------------------------ */
/* 5. Lifetime                                                         */
/* ------------------------------------------------------------------ */

/**
 * Registered last, and for the reason the case states: it takes the document layers down and brings them
 * back, so nothing may run after it.
 */
static void TestSceneLifetime(void)
{
    DesktopSceneConfig config;
    DesktopScene *scene = NULL;
    DesktopSceneStep step;
    size_t baseline;
    int round;

    DesktopFixture();
    baseline = DesktopLiveBlocks();

    /* Repeated open/close returns every block it took. */
    for (round = 0; round < 3; round++) {
        DesktopSceneConfigInit(&config);
        config.width = 480;
        config.height = 360;
        config.RoboCreateWindow = 0;
        RoboCheckEqInt(DesktopSceneOpen(&config, &scene, NULL), RoboOk);
        RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, &step, NULL), RoboOk);
        DesktopSceneClose(scene);
    }
    RoboCheckEqInt((int)DesktopLiveBlocks(), (int)baseline);

    /*
     * A scene needs the layers under it, and says so rather than failing somewhere less useful: with the
     * paint stack down, opening one is refused with a reason.
     */
    PaintShutdown();
    LayoutShutdown();
    CssShutdown();
    HtmlShutdown();
    RoboCheckEqInt(HtmlIsInitialized(), 0);
    DesktopSceneConfigInit(&config);
    config.width = 320;
    config.height = 240;
    config.RoboCreateWindow = 0;
    RoboCheck(DesktopSceneOpen(&config, &scene, NULL) != RoboOk);
    RoboCheck(scene == NULL);

    /*
     * Restore, and open the same scene again, so a following case would find a working tree. Exactly the
     * four layers this case took down come back -- `BaseInit` is *not* called, because base was never
     * taken down and a second init is refused (`ROBO_ALREADY_INITIALIZED`), which is the module saying that
     * init is once per program rather than a reference count.
     */
    RoboCheckEqInt(HtmlInit(), RoboOk);
    RoboCheckEqInt(CssInit(), RoboOk);
    RoboCheckEqInt(LayoutInit(), RoboOk);
    RoboCheckEqInt(PaintInit(), RoboOk);
    RoboCheckEqInt(DesktopSceneOpen(&config, &scene, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopSceneStep(scene, 0u, &step, NULL), RoboOk);
    RoboCheckEqInt(step.painted, 1);
    DesktopSceneClose(scene);
    /*
     * No second leak reading here, and deliberately. `DesktopLiveBlocks` counts *every* live block in the
     * process, and this case is the only one that restarts modules: the number after a restart is a
     * statement about those modules (in a full run it comes back lower, because what they rebuild is not
     * byte-for-byte what they had), not about the scene. The leak reading that is this file's business is
     * the one above, where three open/close round-trips return exactly what they took.
     */
}

void TestRegisterDesktopScene(void)
{
    TestRegister("desktop.scene.open", TestSceneOpen);
    TestRegister("desktop.scene.words", TestSceneWords);
    TestRegister("desktop.scene.step", TestSceneStep);
    TestRegister("desktop.scene.system_face", TestSceneSystemFace);
    TestRegister("desktop.scene.lifetime", TestSceneLifetime);
}
