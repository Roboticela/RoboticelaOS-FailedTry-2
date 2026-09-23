/*
 * Shared RoboRender test fixtures (Phase 11.12).
 *
 * Every suite in `render/tests` includes this. Like the paint module's support header it is **not** a
 * framework -- the runtime has exactly one of those (`Tests/Framework/RoboTest.h`) and this release
 * adds none -- and it holds the five things a render suite would otherwise copy:
 *
 *   bring-up   every layer a frame needs, in `DEPENDENCIES.md` order. It builds on
 *              `RoboPaintTestSupport.h` rather than restating it, and that is the same argument that
 *              header makes about `RoboLayoutTestSupport.h` one layer down: a render fixture *is* a
 *              paint fixture plus a target, and a second copy of "build a document, style it,
 *              lay it out, paint it" would be a second thing to keep in step with the subsystem this
 *              module merely consumes;
 *
 *   a scene    the whole chain in three calls -- a document through HTML → CSS → layout → records →
 *              **display list** -- with the display list kept alive beside the artifacts it came
 *              from. Both halves are needed: the scene is what a frame is run against, and the
 *              *records* are what an invalidation set is built from (`set_from_lists`), which is how
 *              the region case gets its regions from something other than a hand-written rectangle;
 *
 *   targets    an owned target with a page colour, and the two reads a pixel assertion needs. The
 *              pixel read goes through `RenderTargetSurface` and RoboGraphics' own
 *              `GfxSurfaceGetPixel` deliberately: this module has no pixel door of its own,
 *              because reading a pixel is what the graphics layer is *for*, and a second door would
 *              be a second conversion of the same bytes;
 *
 *   a face     a synthetic SFNT with one glyph, built here out of numbers so that every expectation
 *              about a run is arithmetic. It is the paint text suite's fixture restated rather than
 *              shared, and the reason is the reason that suite's own measurer is local: a fixture two
 *              suites edit is a fixture that breaks both suites' expectations the day either one
 *              changes, and this one is stated as *numbers* (`units per em`, an advance, a square) so
 *              that a change to it is a change to every assertion built on it. What it is used for
 *              here is narrower than there: a scene must contain text to be a scene, and one mapped
 *              code point is enough to prove a run's glyphs reached the target;
 *
 *   a checkpoint  one PNG writer, so the visual release can leave a picture behind for a person to
 *              look at. It goes through RoboGraphics' real encoder and writes to a path the caller
 *              names, defaulting to `Out/RenderScene.png` under the repository's own out-of-tree
 *              build directory -- which is where every generated artifact in this project already
 *              lives.
 *
 * Every helper asserts rather than returning a status, which is the convention the layout and paint
 * suites set: a suite that had to check each step of its own scaffolding would drown its subject in
 * harness.
 */

#ifndef ROBO_RENDER_TEST_SUPPORT_H
#define ROBO_RENDER_TEST_SUPPORT_H

#include "RoboPaintTestSupport.h"

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontTt.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"

#include "RoboPaint/RoboPaintInvalidation.h"

#include "RoboRender/RoboRender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

/** Bring every layer a render test needs up (once) and leave them up. */
static inline void RenderFixture(void)
{
    PaintFixture();
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t RenderLiveBlocks(void)
{
    return MemDebugLiveCount();
}

/* ------------------------------------------------------------------ */
/* A scene: one document through the whole chain                       */
/* ------------------------------------------------------------------ */

/** Everything one frame is run against: the document, its records, and its display list. */
typedef struct renderScene {
    PaintFixtureDoc doc;
    PaintResult records;
    PaintDisplayList *display;
} RenderScene;

/** Build @p html with @p css and lay it out. The paint and the display list come next. */
static inline void RenderSceneOpen(RenderScene *scene, const char *html, const char *css)
{
    memset(scene, 0, sizeof(*scene));
    PaintBeginLaidOut(&scene->doc, html, css);
}

/**
 * Paint the laid-out document and build a display list from the records.
 *
 * Two artifacts rather than one because they answer different questions and this suite needs both:
 * the display list is what a frame executes, and the paint list is what an invalidation set is
 * compared against (`PaintInvalidationSetFromLists`). Keeping both is also what lets a case
 * mutate or release one and assert the other is unaffected -- the snapshot property each artifact
 * claims on its own.
 */
static inline void RenderScenePaint(RenderScene *scene)
{
    PaintDisplayListBuilder *builder = NULL;

    PaintOnce(&scene->doc, &scene->records);
    RoboCheckEqInt(PaintDisplayListCreate(&scene->display, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    RoboCheckEqInt(
        PaintDisplayListBuilderBuild(builder, scene->records.list, scene->display, NULL, NULL),
        RoboOk);
    PaintDisplayListBuilderDestroy(builder);
    RoboCheck(PaintDisplayListCount(scene->display) > 0u);
}

/**
 * The same, with @p face installed for **shaping** (Phase 12.16).
 *
 * A text frame is recorded with the face it will be drawn with, because the glyphs are produced at recording
 * time rather than per frame -- so a case that rasterizes a run for real paints the scene with the face. A
 * case that asserts the "recorded without a face" state uses the plain door above, which is the pre-12.16
 * shape and still a legal document.
 */
static inline void RenderScenePaintWith(RenderScene *scene, FontFace *face)
{
    PaintDisplayListBuilder *builder = NULL;

    PaintOnceWithFace(&scene->doc, face, &scene->records);
    RoboCheckEqInt(PaintDisplayListCreate(&scene->display, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    RoboCheckEqInt(
        PaintDisplayListBuilderBuild(builder, scene->records.list, scene->display, NULL, NULL),
        RoboOk);
    PaintDisplayListBuilderDestroy(builder);
    RoboCheck(PaintDisplayListCount(scene->display) > 0u);
}

/** The three calls a pixel case wants: a document, laid out, painted, listed. */
static inline void RenderSceneBegin(RenderScene *scene, const char *html, const char *css)
{
    RenderSceneOpen(scene, html, css);
    RenderScenePaint(scene);
}

static inline void RenderSceneEnd(RenderScene *scene)
{
    PaintDisplayListDestroy(scene->display);
    scene->display = NULL;
    PaintResultDispose(&scene->records);
    PaintEnd(&scene->doc);
}

/**
 * Release the **record list** and the display list but keep the tree, which is what a case about a
 * snapshot wants: the artifacts a frame reads must not be reachable from anything the document still
 * owns.
 */
static inline void RenderSceneReleaseArtifacts(RenderScene *scene)
{
    PaintDisplayListDestroy(scene->display);
    scene->display = NULL;
    PaintResultDispose(&scene->records);
}

/* ------------------------------------------------------------------ */
/* Targets                                                             */
/* ------------------------------------------------------------------ */

/** Create a target, asserted to succeed. */
static inline RenderTarget *GetRoboRenderTarget(uint32_t width, uint32_t height)
{
    RenderTarget *target = NULL;

    RoboCheckEqInt(RenderTargetCreate(&target, width, height, NULL), RoboOk);
    RoboCheck(target != NULL);
    return target;
}

/** Create a target and clear it to @p page -- the explicit frame baseline. */
static inline RenderTarget *RenderPage(uint32_t width, uint32_t height,
                                               GfxColorRgba8 page)
{
    RenderTarget *target = GetRoboRenderTarget(width, height);

    RoboCheckEqInt(RenderTargetClear(target, page, NULL), RoboOk);
    return target;
}

static inline void RenderTargetEnd(RenderTarget *target)
{
    RenderTargetDestroy(target);
}

/** The colour of @p target at @p x,@p y, through the graphics layer's own read. */
static inline GfxColorRgba8 RenderPixel(const RenderTarget *target,
                                                 uint32_t x,
                                                 uint32_t y)
{
    GfxColorRgba8 color;

    memset(&color, 0, sizeof(color));
    RoboCheckEqInt(
        GfxSurfaceGetPixel(RenderTargetSurfaceConst(target), x, y, &color), RoboOk);
    return color;
}

static inline int RenderSameColor(GfxColorRgba8 a, GfxColorRgba8 b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/** Assert @p target's pixel at @p x,@p y is exactly @p expected. */
static inline void RenderPixelIs(const RenderTarget *target,
                                  uint32_t x,
                                  uint32_t y,
                                  GfxColorRgba8 expected,
                                  const char *what)
{
    GfxColorRgba8 actual = RenderPixel(target, x, y);

    if (!RenderSameColor(actual, expected)) {
        TestFail(__FILE__, __LINE__, "%s: pixel %u,%u is %u,%u,%u,%u, expected %u,%u,%u,%u",
                       what, (unsigned)x, (unsigned)y, (unsigned)actual.r, (unsigned)actual.g,
                       (unsigned)actual.b, (unsigned)actual.a, (unsigned)expected.r,
                       (unsigned)expected.g, (unsigned)expected.b, (unsigned)expected.a);
    }
}

/**
 * Non-zero when any pixel of the half-open rectangle @p x,@p y,@p w,@p h differs from @p background.
 *
 * The door a case needs when what it wants to prove is *that ink landed somewhere in a region* rather
 * than what one named pixel holds -- text is the case that made this necessary, because a glyph is an
 * **outline**: the interior of a 10x10 square is background, its four one-pixel edges are the run, and
 * a case pinned to the middle of the square would be asserting about the glyph the way a font designer
 * draws it rather than about whether the frame contains it. The region is clamped to the target, so a
 * box that hangs off the edge is a smaller question rather than an invalid one, and an empty region
 * answers "no ink" -- which is the honest answer to nothing.
 */
static inline int RenderRegionHasInk(const RenderTarget *target,
                                       int32_t x,
                                       int32_t y,
                                       int32_t w,
                                       int32_t h,
                                       GfxColorRgba8 background)
{
    int32_t width = (int32_t)RenderTargetWidth(target);
    int32_t height = (int32_t)RenderTargetHeight(target);
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = ((x + w) > width) ? width : (x + w);
    int32_t y1 = ((y + h) > height) ? height : (y + h);
    int32_t px;
    int32_t py;

    for (py = y0; py < y1; py++) {
        for (px = x0; px < x1; px++) {
            if (!RenderSameColor(RenderPixel(target, (uint32_t)px, (uint32_t)py), background)) {
                return 1;
            }
        }
    }
    return 0;
}

/** Assert that @p what drew at least one pixel inside the region. */
static inline void RenderRegionHasInkIs(const RenderTarget *target,
                                           int32_t x,
                                           int32_t y,
                                           int32_t w,
                                           int32_t h,
                                           GfxColorRgba8 background,
                                           const char *what)
{
    if (!RenderRegionHasInk(target, x, y, w, h, background)) {
        TestFail(__FILE__, __LINE__,
                       "%s: no pixel in %d,%d,%d,%d differs from the background", what, (int)x,
                       (int)y, (int)w, (int)h);
    }
}

/** Assert that @p what drew nothing inside the region (the other half of the claim). */
static inline void RenderRegionIsClear(const RenderTarget *target,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         GfxColorRgba8 background,
                                         const char *what)
{
    if (RenderRegionHasInk(target, x, y, w, h, background)) {
        TestFail(__FILE__, __LINE__, "%s: a pixel in %d,%d,%d,%d is not the background", what,
                       (int)x, (int)y, (int)w, (int)h);
    }
}

/** Assert @p target's pixel at @p x,@p y is **not** @p other (used to prove something was drawn). */
static inline void RenderPixelIsNot(const RenderTarget *target,
                                      uint32_t x,
                                      uint32_t y,
                                      GfxColorRgba8 other,
                                      const char *what)
{
    GfxColorRgba8 actual = RenderPixel(target, x, y);

    if (RenderSameColor(actual, other)) {
        TestFail(__FILE__, __LINE__, "%s: pixel %u,%u is still %u,%u,%u,%u", what,
                       (unsigned)x, (unsigned)y, (unsigned)actual.r, (unsigned)actual.g,
                       (unsigned)actual.b, (unsigned)actual.a);
    }
}

/** Assert two targets hold the same bytes, field by field (the determinism claim). */
static inline void RenderTargetsEqual(const RenderTarget *a,
                                       const RenderTarget *b,
                                       const char *what)
{
    size_t sizeA = 0u;
    size_t sizeB = 0u;
    const uint8_t *bytesA = RoboRenderTargetBytes(a, &sizeA);
    const uint8_t *bytesB = RoboRenderTargetBytes(b, &sizeB);

    RoboCheck(bytesA != NULL && bytesB != NULL);
    RoboCheckEqInt((int)sizeA, (int)sizeB);
    if (memcmp(bytesA, bytesB, sizeA) != 0) {
        size_t index;

        for (index = 0u; index < sizeA; index++) {
            if (bytesA[index] != bytesB[index]) {
                TestFail(__FILE__, __LINE__,
                               "%s: byte %u differs (%u vs %u)", what, (unsigned)index,
                               (unsigned)bytesA[index], (unsigned)bytesB[index]);
                return;
            }
        }
        TestFail(__FILE__, __LINE__, "%s: the bytes differ but no byte differs", what);
    }
    RoboCheckEqInt((int)RenderTargetChecksum(a),
                      (int)RenderTargetChecksum(b));
}

/* ------------------------------------------------------------------ */
/* The visual checkpoint                                               */
/* ------------------------------------------------------------------ */

/**
 * Encode @p target to PNG and write it to @p path. Returns non-zero on success.
 *
 * Deliberately **not** an assertion: a checkpoint is evidence for a person to look at, and a suite
 * whose verdict depended on the filesystem would fail on a machine with a read-only tree while the
 * pixels it verified were perfectly good. The suites that call it assert the *pixels* and report the
 * file; `render.scene.checkpoint` is the one that asserts the file was written, because leaving an
 * artifact behind is part of what that case is for.
 */
static inline int RenderWritePng(const RenderTarget *target, const char *path)
{
    GfxEncodeOptions options;
    GfxEncodedBuffer buffer;
    FILE *file;

    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&buffer, 0, sizeof(buffer));
    if (GfxSurfaceEncode(RenderTargetSurfaceConst(target), &options, &buffer) !=
        RoboOk) {
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
 * Where the checkpoint artifact goes: `Out/RenderScene.png` unless the environment says otherwise.
 *
 * The default is the repository's out-of-tree build directory, which is where every generated file in
 * this project lives and which no source-control rule has to know about. The override exists so a
 * person running the suite by hand can put the picture somewhere they will find it.
 */
static inline const char *RenderCheckpointPath(void)
{
    const char *override = getenv("ROBO_RENDER_SCENE_PNG");

    return (override && override[0] != '\0') ? override : "Out/RenderScene.png";
}

/* ------------------------------------------------------------------ */
/* An image resource, encoded and decoded for real                     */
/* ------------------------------------------------------------------ */

/** A document's picture: the surface encoded, the bytes, and the asset table they live in. */
typedef struct renderAssets {
    GfxSurface *surface;
    GfxEncodedBuffer encoded;
    HtmlImageAssets *assets;
} RenderAssets;

/**
 * A 4x4 picture with a fixed, non-uniform pattern: one red quadrant, one green, one blue and one
 * transparent.
 *
 * Four quadrants rather than a flat colour because a scene's image assertion has to be able to say
 * *which part* of the picture landed where -- a flat fill would pass with the whole image sampled
 * from one corner.
 */
static inline GfxSurface *RenderLogoSurface(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    uint32_t x;
    uint32_t y;

    GfxSurfaceDescInit(&desc);
    desc.width = 4u;
    desc.height = 4u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            GfxColorRgba8 color;

            if (y < 2u) {
                color = (x < 2u) ? GfxRgba8(0xff, 0x00, 0x00, 255)
                                 : GfxRgba8(0x00, 0xff, 0x00, 255);
            } else {
                color = (x < 2u) ? GfxRgba8(0x00, 0x00, 0xff, 255)
                                 : GfxRgba8(0x00, 0x00, 0x00, 0);
            }
            RoboCheckEqInt(GfxSurfaceSetPixel(surface, x, y, color), RoboOk);
        }
    }
    return surface;
}

/** Encode a logo and register it as `Assets/Logo.png`; takes ownership of @p surface. */
static inline void RenderAssetsBegin(RenderAssets *assets)
{
    GfxEncodeOptions options;

    memset(assets, 0, sizeof(*assets));
    assets->surface = RenderLogoSurface();
    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&assets->encoded, 0, sizeof(assets->encoded));
    RoboCheckEqInt(GfxSurfaceEncode(assets->surface, &options, &assets->encoded),
                      RoboOk);
    RoboCheck(assets->encoded.size > 0u);
    RoboCheckEqInt(HtmlImageAssetsCreate(NULL, 0u, &assets->assets), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(assets->assets, "Assets/Logo.png",
                                                 assets->encoded.data, assets->encoded.size, NULL),
                      RoboOk);
}

static inline void RenderAssetsEnd(RenderAssets *assets)
{
    HtmlImageAssetsDestroy(assets->assets);
    assets->assets = NULL;
    GfxEncodedBufferRelease(&assets->encoded);
    GfxSurfaceRelease(assets->surface);
    assets->surface = NULL;
}

/**
 * Resolve the image element with class @p name through @p assets, **before** layout runs.
 *
 * Before, because that is the order a real document loads in and because it keeps the fixture honest:
 * the resource is attached to the element before anything reads a box, so nothing here can depend on
 * a paint-time mutation being visible.
 */
static inline void RenderResolveImage(PaintFixtureDoc *fixture,
                                        const char *name,
                                        const RenderAssets *assets)
{
    HtmlNodeHandle element = PaintFind(fixture, name);
    HtmlImageResolver resolver = HtmlImageAssetsResolver(assets->assets);

    RoboCheckEqInt((int)HtmlNodeHandleIsWellformed(&element), 1);
    RoboCheckEqInt(HtmlElementImageResolve(&element, &resolver, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* A synthetic face, built here                                        */
/* ------------------------------------------------------------------ */

/*
 * The face, stated once as numbers so every expectation about a run is arithmetic:
 *
 *   units per em   1000   so at `font-size: 100px` one unit is 0.1 device pixels
 *   glyph 0        empty  `.notdef`: an unmapped code point draws nothing
 *   glyph 1        square from (0, 0) to (100, 100) FUnits -- 10x10 device pixels at 100px, i.e.
 *                  `'A'`
 *   hmtx           advance 600 units for glyph 1, 500 for glyph 0, so at 100px `'A'` advances 60px
 *
 * One mapped code point and not two: what a render suite has to prove about text is that the
 * characters of a run reach the target as glyphs, and a second mapping would add assertions about a
 * font rather than about a frame. The paint module's text suite owns the character-level story.
 */
#define RENDER_FACE_UPEM 1000u
#define RENDER_FACE_ADVANCE_GLYPH 600u
#define RENDER_FACE_ADVANCE_NOTDEF 500u
#define RENDER_FACE_NUM_GLYPHS 2u
#define RENDER_FACE_SQUARE_UNITS 100
#define RENDER_FACE_FONT_SIZE 100.0

typedef struct renderSynthTable {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} RenderSynthTable;

static inline void RenderPutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static inline void RenderPutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static inline void RenderPutI16(uint8_t **p, int16_t v) { RenderPutU16(p, (uint16_t)v); }
static inline void RenderPutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static inline void RenderPutI64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        RenderPutU8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

static inline size_t RenderBuildSfnt(uint8_t *out,
                                       size_t outCap,
                                       const RenderSynthTable *tables,
                                       uint32_t n)
{
    size_t dirEnd = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dirEnd;
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint32_t i;
    uint8_t *p;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    RenderPutU32(&p, RoboFontSfntVersionTrue);
    while ((uint16_t)(pow2 * 2u) <= n) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = out + 4;
    RenderPutU16(&p, (uint16_t)n);
    RenderPutU16(&p, (uint16_t)(pow2 * 16u));
    RenderPutU16(&p, entry);
    RenderPutU16(&p, (uint16_t)(n * 16u - pow2 * 16u));

    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *entryP = out + 12u + (size_t)i * 16u;

        RenderPutU32(&entryP, tables[i].tag);
        RenderPutU32(&entryP, 0u);
        RenderPutU32(&entryP, (uint32_t)cursor);
        RenderPutU32(&entryP, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static inline size_t RenderBuildHead(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    RenderPutU16(&p, 1);
    RenderPutU16(&p, 0);
    RenderPutU32(&p, 0x00010000u);
    RenderPutU32(&p, 0u);
    RenderPutU32(&p, RoboFontHeadMagic);
    RenderPutU16(&p, 0u);
    RenderPutU16(&p, upem);
    RenderPutI64(&p, 0);
    RenderPutI64(&p, 0);
    RenderPutI16(&p, -50);
    RenderPutI16(&p, -200);
    RenderPutI16(&p, 1000);
    RenderPutI16(&p, 800);
    RenderPutU16(&p, 0u);
    RenderPutU16(&p, 8u);
    RenderPutI16(&p, 2);
    RenderPutI16(&p, 1); /* long loca */
    RenderPutI16(&p, 0);
    return (size_t)(p - b);
}

static inline size_t RenderBuildHhea(uint8_t *b, uint16_t nHMetrics)
{
    uint8_t *p = b;
    int i;

    RenderPutU16(&p, 1);
    RenderPutU16(&p, 0);
    RenderPutI16(&p, 800);
    RenderPutI16(&p, -200);
    RenderPutI16(&p, 90);
    RenderPutU16(&p, 900);
    RenderPutI16(&p, -10);
    RenderPutI16(&p, -20);
    RenderPutI16(&p, 880);
    RenderPutI16(&p, 1);
    RenderPutI16(&p, 0);
    RenderPutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        RenderPutI16(&p, 0);
    }
    RenderPutI16(&p, 0);
    RenderPutU16(&p, nHMetrics);
    return (size_t)(p - b);
}

static inline size_t RenderBuildMaxp(uint8_t *b, uint16_t numGlyphs)
{
    uint8_t *p = b;
    int i;

    RenderPutU32(&p, 0x00010000u);
    RenderPutU16(&p, numGlyphs);
    RenderPutU16(&p, 8);
    RenderPutU16(&p, 2);
    for (i = 0; i < 12; i++) {
        RenderPutU16(&p, 0);
    }
    return (size_t)(p - b);
}

static inline size_t RenderBuildHmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        RenderPutU16(&p, advances[i]);
        RenderPutI16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

/** One glyph: a square from (0, 0) to (100, 100) in FUnits. */
static inline size_t RenderBuildSquareGlyf(uint8_t *b)
{
    uint8_t *p = b;

    RenderPutI16(&p, 1);                        /* numberOfContours */
    RenderPutI16(&p, 0);                        /* xMin */
    RenderPutI16(&p, 0);                        /* yMin */
    RenderPutI16(&p, RENDER_FACE_SQUARE_UNITS); /* xMax */
    RenderPutI16(&p, RENDER_FACE_SQUARE_UNITS); /* yMax */
    RenderPutU16(&p, 3);                        /* endPtsOfContours[0] */
    RenderPutU16(&p, 0);                        /* instructionLength */
    RenderPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve |
                                RoboFontGlyfFlagXSameOrPositive |
                                RoboFontGlyfFlagYSameOrPositive));
    RenderPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                                RoboFontGlyfFlagXSameOrPositive |
                                RoboFontGlyfFlagYSameOrPositive));
    RenderPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagYShort |
                                RoboFontGlyfFlagXSameOrPositive |
                                RoboFontGlyfFlagYSameOrPositive));
    RenderPutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                                RoboFontGlyfFlagYSameOrPositive));
    RenderPutU8(&p, RENDER_FACE_SQUARE_UNITS);
    RenderPutU8(&p, RENDER_FACE_SQUARE_UNITS);
    RenderPutU8(&p, RENDER_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/** A one-subtable cmap holding a format-4 segment table mapping `'A'` to glyph 1. */
static inline size_t RenderBuildCmap(uint8_t *b)
{
    uint8_t *p = b;
    uint8_t *lengthPtr;
    uint8_t *subtable = b + 12;
    uint16_t first = 0x41u;

    RenderPutU16(&p, 0);   /* version */
    RenderPutU16(&p, 1);   /* numTables */
    RenderPutU16(&p, 3);   /* Windows */
    RenderPutU16(&p, 1);   /* Unicode BMP */
    RenderPutU32(&p, 12u); /* subtable offset */

    RenderPutU16(&p, 4); /* format */
    lengthPtr = p;
    RenderPutU16(&p, 0); /* length, patched below */
    RenderPutU16(&p, 0); /* language */
    RenderPutU16(&p, 4); /* segCountX2: two segments */
    RenderPutU16(&p, 4); /* searchRange */
    RenderPutU16(&p, 1); /* entrySelector */
    RenderPutU16(&p, 0); /* rangeShift */
    RenderPutU16(&p, first);
    RenderPutU16(&p, 0xFFFFu);
    RenderPutU16(&p, 0); /* reservedPad */
    RenderPutU16(&p, first);
    RenderPutU16(&p, 0xFFFFu);
    RenderPutI16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    RenderPutI16(&p, 1);                             /* idDelta[1] */
    RenderPutU16(&p, 0);                             /* idRangeOffset[0] */
    RenderPutU16(&p, 0);                             /* idRangeOffset[1] */

    {
        uint16_t length = (uint16_t)(p - subtable);

        lengthPtr[0] = (uint8_t)(length >> 8);
        lengthPtr[1] = (uint8_t)length;
    }
    return (size_t)(p - b);
}

/** A face plus the source it came from, so a case releases both the way it built them. */
typedef struct renderFace {
    FontSource *source;
    FontFace *face;
} RenderFace;

/** Build the synthetic face: `'A'` maps to the one square glyph, everything else to `.notdef`. */
static inline void RenderFaceCreate(RenderFace *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[RENDER_FACE_NUM_GLYPHS] = {
        (uint16_t)RENDER_FACE_ADVANCE_NOTDEF,
        (uint16_t)RENDER_FACE_ADVANCE_GLYPH
    };
    RenderSynthTable tables[7];
    uint32_t nt = 0u;
    size_t glyphSize;
    size_t n;
    uint8_t *p;

    out->source = NULL;
    out->face = NULL;

    glyphSize = RenderBuildSquareGlyf(glyf);
    /*
     * `loca` has `numGlyphs + 1` entries, and the two leading zeroes are the *statement*: glyph 0
     * (`.notdef`) is empty, so its outline spans nothing and its end offset equals its start. One
     * mapped glyph and one empty one is exactly what makes "a mapped code point draws and an unmapped
     * one does not" true of this face.
     */
    {
        const uint32_t offsets[RENDER_FACE_NUM_GLYPHS + 1u] = { 0u, 0u, (uint32_t)glyphSize };

        p = loca;
        for (nt = 0u; nt <= (uint32_t)RENDER_FACE_NUM_GLYPHS; nt++) {
            RenderPutU32(&p, offsets[nt]);
        }
    }
    nt = 0u;

    tables[nt].tag = RoboFontTagHead;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)RenderBuildHead(head, (uint16_t)RENDER_FACE_UPEM);
    nt++;
    tables[nt].tag = RoboFontTagHhea;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)RenderBuildHhea(hhea, (uint16_t)RENDER_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagMaxp;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)RenderBuildMaxp(maxp, (uint16_t)RENDER_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagHmtx;
    tables[nt].data = hmtx;
    tables[nt].length =
        (uint32_t)RenderBuildHmtx(hmtx, advances, (uint16_t)RENDER_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = RoboFontTagCmap;
    tables[nt].data = cmap;
    tables[nt].length = (uint32_t)RenderBuildCmap(cmap);
    nt++;
    tables[nt].tag = RoboFontTagLoca;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((RENDER_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = RoboFontTagGlyf;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)glyphSize;
    nt++;

    n = RenderBuildSfnt(font, sizeof(font), tables, nt);
    RoboCheck(n > 0u);
    RoboCheckEqInt(FontSourceCreateMemoryCopy(font, n, &out->source), RoboOk);
    RoboCheckEqInt(FontFaceCreate(out->source, 0, &out->face), RoboOk);
}

static inline void RenderFaceDestroy(RenderFace *face)
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

#endif /* ROBO_RENDER_TEST_SUPPORT_H */
