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
 *              pixel read goes through `robo_render_target_surface` and RoboGraphics' own
 *              `robo_gfx_surface_get_pixel` deliberately: this module has no pixel door of its own,
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
static inline void render_fixture(void)
{
    paint_fixture();
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t render_live_blocks(void)
{
    return robo_mem_debug_live_count();
}

/* ------------------------------------------------------------------ */
/* A scene: one document through the whole chain                       */
/* ------------------------------------------------------------------ */

/** Everything one frame is run against: the document, its records, and its display list. */
typedef struct render_scene {
    paint_fixture_doc_t doc;
    paint_result_t records;
    robo_paint_display_list_t *display;
} render_scene_t;

/** Build @p html with @p css and lay it out. The paint and the display list come next. */
static inline void render_scene_open(render_scene_t *scene, const char *html, const char *css)
{
    memset(scene, 0, sizeof(*scene));
    paint_begin_laid_out(&scene->doc, html, css);
}

/**
 * Paint the laid-out document and build a display list from the records.
 *
 * Two artifacts rather than one because they answer different questions and this suite needs both:
 * the display list is what a frame executes, and the paint list is what an invalidation set is
 * compared against (`robo_paint_invalidation_set_from_lists`). Keeping both is also what lets a case
 * mutate or release one and assert the other is unaffected -- the snapshot property each artifact
 * claims on its own.
 */
static inline void render_scene_paint(render_scene_t *scene)
{
    robo_paint_display_list_builder_t *builder = NULL;

    paint_once(&scene->doc, &scene->records);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&scene->display, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_paint_display_list_builder_build(builder, scene->records.list, scene->display, NULL, NULL),
        ROBO_OK);
    robo_paint_display_list_builder_destroy(builder);
    ROBO_CHECK(robo_paint_display_list_count(scene->display) > 0u);
}

/** The three calls a pixel case wants: a document, laid out, painted, listed. */
static inline void render_scene_begin(render_scene_t *scene, const char *html, const char *css)
{
    render_scene_open(scene, html, css);
    render_scene_paint(scene);
}

static inline void render_scene_end(render_scene_t *scene)
{
    robo_paint_display_list_destroy(scene->display);
    scene->display = NULL;
    paint_result_dispose(&scene->records);
    paint_end(&scene->doc);
}

/**
 * Release the **record list** and the display list but keep the tree, which is what a case about a
 * snapshot wants: the artifacts a frame reads must not be reachable from anything the document still
 * owns.
 */
static inline void render_scene_release_artifacts(render_scene_t *scene)
{
    robo_paint_display_list_destroy(scene->display);
    scene->display = NULL;
    paint_result_dispose(&scene->records);
}

/* ------------------------------------------------------------------ */
/* Targets                                                             */
/* ------------------------------------------------------------------ */

/** Create a target, asserted to succeed. */
static inline robo_render_target_t *render_target(uint32_t width, uint32_t height)
{
    robo_render_target_t *target = NULL;

    ROBO_CHECK_EQ_INT(robo_render_target_create(&target, width, height, NULL), ROBO_OK);
    ROBO_CHECK(target != NULL);
    return target;
}

/** Create a target and clear it to @p page -- the explicit frame baseline. */
static inline robo_render_target_t *render_page(uint32_t width, uint32_t height,
                                               robo_gfx_color_rgba8_t page)
{
    robo_render_target_t *target = render_target(width, height);

    ROBO_CHECK_EQ_INT(robo_render_target_clear(target, page, NULL), ROBO_OK);
    return target;
}

static inline void render_target_end(robo_render_target_t *target)
{
    robo_render_target_destroy(target);
}

/** The colour of @p target at @p x,@p y, through the graphics layer's own read. */
static inline robo_gfx_color_rgba8_t render_pixel(const robo_render_target_t *target,
                                                 uint32_t x,
                                                 uint32_t y)
{
    robo_gfx_color_rgba8_t color;

    memset(&color, 0, sizeof(color));
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_get_pixel(robo_render_target_surface_const(target), x, y, &color), ROBO_OK);
    return color;
}

static inline int render_same_color(robo_gfx_color_rgba8_t a, robo_gfx_color_rgba8_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/** Assert @p target's pixel at @p x,@p y is exactly @p expected. */
static inline void render_pixel_is(const robo_render_target_t *target,
                                  uint32_t x,
                                  uint32_t y,
                                  robo_gfx_color_rgba8_t expected,
                                  const char *what)
{
    robo_gfx_color_rgba8_t actual = render_pixel(target, x, y);

    if (!render_same_color(actual, expected)) {
        robo_test_fail(__FILE__, __LINE__, "%s: pixel %u,%u is %u,%u,%u,%u, expected %u,%u,%u,%u",
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
static inline int render_region_has_ink(const robo_render_target_t *target,
                                       int32_t x,
                                       int32_t y,
                                       int32_t w,
                                       int32_t h,
                                       robo_gfx_color_rgba8_t background)
{
    int32_t width = (int32_t)robo_render_target_width(target);
    int32_t height = (int32_t)robo_render_target_height(target);
    int32_t x0 = (x < 0) ? 0 : x;
    int32_t y0 = (y < 0) ? 0 : y;
    int32_t x1 = ((x + w) > width) ? width : (x + w);
    int32_t y1 = ((y + h) > height) ? height : (y + h);
    int32_t px;
    int32_t py;

    for (py = y0; py < y1; py++) {
        for (px = x0; px < x1; px++) {
            if (!render_same_color(render_pixel(target, (uint32_t)px, (uint32_t)py), background)) {
                return 1;
            }
        }
    }
    return 0;
}

/** Assert that @p what drew at least one pixel inside the region. */
static inline void render_region_has_ink_is(const robo_render_target_t *target,
                                           int32_t x,
                                           int32_t y,
                                           int32_t w,
                                           int32_t h,
                                           robo_gfx_color_rgba8_t background,
                                           const char *what)
{
    if (!render_region_has_ink(target, x, y, w, h, background)) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: no pixel in %d,%d,%d,%d differs from the background", what, (int)x,
                       (int)y, (int)w, (int)h);
    }
}

/** Assert that @p what drew nothing inside the region (the other half of the claim). */
static inline void render_region_is_clear(const robo_render_target_t *target,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         robo_gfx_color_rgba8_t background,
                                         const char *what)
{
    if (render_region_has_ink(target, x, y, w, h, background)) {
        robo_test_fail(__FILE__, __LINE__, "%s: a pixel in %d,%d,%d,%d is not the background", what,
                       (int)x, (int)y, (int)w, (int)h);
    }
}

/** Assert @p target's pixel at @p x,@p y is **not** @p other (used to prove something was drawn). */
static inline void render_pixel_is_not(const robo_render_target_t *target,
                                      uint32_t x,
                                      uint32_t y,
                                      robo_gfx_color_rgba8_t other,
                                      const char *what)
{
    robo_gfx_color_rgba8_t actual = render_pixel(target, x, y);

    if (render_same_color(actual, other)) {
        robo_test_fail(__FILE__, __LINE__, "%s: pixel %u,%u is still %u,%u,%u,%u", what,
                       (unsigned)x, (unsigned)y, (unsigned)actual.r, (unsigned)actual.g,
                       (unsigned)actual.b, (unsigned)actual.a);
    }
}

/** Assert two targets hold the same bytes, field by field (the determinism claim). */
static inline void render_targets_equal(const robo_render_target_t *a,
                                       const robo_render_target_t *b,
                                       const char *what)
{
    size_t size_a = 0u;
    size_t size_b = 0u;
    const uint8_t *bytes_a = robo_render_target_bytes(a, &size_a);
    const uint8_t *bytes_b = robo_render_target_bytes(b, &size_b);

    ROBO_CHECK(bytes_a != NULL && bytes_b != NULL);
    ROBO_CHECK_EQ_INT((int)size_a, (int)size_b);
    if (memcmp(bytes_a, bytes_b, size_a) != 0) {
        size_t index;

        for (index = 0u; index < size_a; index++) {
            if (bytes_a[index] != bytes_b[index]) {
                robo_test_fail(__FILE__, __LINE__,
                               "%s: byte %u differs (%u vs %u)", what, (unsigned)index,
                               (unsigned)bytes_a[index], (unsigned)bytes_b[index]);
                return;
            }
        }
        robo_test_fail(__FILE__, __LINE__, "%s: the bytes differ but no byte differs", what);
    }
    ROBO_CHECK_EQ_INT((int)robo_render_target_checksum(a),
                      (int)robo_render_target_checksum(b));
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
static inline int render_write_png(const robo_render_target_t *target, const char *path)
{
    robo_gfx_encode_options_t options;
    robo_gfx_encoded_buffer_t buffer;
    FILE *file;

    robo_gfx_encode_options_init(&options);
    options.container = ROBO_GFX_ENCODE_PNG;
    options.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&buffer, 0, sizeof(buffer));
    if (robo_gfx_surface_encode(robo_render_target_surface_const(target), &options, &buffer) !=
        ROBO_OK) {
        return 0;
    }
    file = fopen(path, "wb");
    if (!file) {
        robo_gfx_encoded_buffer_release(&buffer);
        return 0;
    }
    if (fwrite(buffer.data, 1u, buffer.size, file) != buffer.size) {
        fclose(file);
        robo_gfx_encoded_buffer_release(&buffer);
        return 0;
    }
    fclose(file);
    robo_gfx_encoded_buffer_release(&buffer);
    return 1;
}

/**
 * Where the checkpoint artifact goes: `Out/RenderScene.png` unless the environment says otherwise.
 *
 * The default is the repository's out-of-tree build directory, which is where every generated file in
 * this project lives and which no source-control rule has to know about. The override exists so a
 * person running the suite by hand can put the picture somewhere they will find it.
 */
static inline const char *render_checkpoint_path(void)
{
    const char *override = getenv("ROBO_RENDER_SCENE_PNG");

    return (override && override[0] != '\0') ? override : "Out/RenderScene.png";
}

/* ------------------------------------------------------------------ */
/* An image resource, encoded and decoded for real                     */
/* ------------------------------------------------------------------ */

/** A document's picture: the surface encoded, the bytes, and the asset table they live in. */
typedef struct render_assets {
    robo_gfx_surface_t *surface;
    robo_gfx_encoded_buffer_t encoded;
    robo_html_image_assets_t *assets;
} render_assets_t;

/**
 * A 4x4 picture with a fixed, non-uniform pattern: one red quadrant, one green, one blue and one
 * transparent.
 *
 * Four quadrants rather than a flat colour because a scene's image assertion has to be able to say
 * *which part* of the picture landed where -- a flat fill would pass with the whole image sampled
 * from one corner.
 */
static inline robo_gfx_surface_t *render_logo_surface(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;
    uint32_t x;
    uint32_t y;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 4u;
    desc.height = 4u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            robo_gfx_color_rgba8_t color;

            if (y < 2u) {
                color = (x < 2u) ? robo_gfx_rgba8(0xff, 0x00, 0x00, 255)
                                 : robo_gfx_rgba8(0x00, 0xff, 0x00, 255);
            } else {
                color = (x < 2u) ? robo_gfx_rgba8(0x00, 0x00, 0xff, 255)
                                 : robo_gfx_rgba8(0x00, 0x00, 0x00, 0);
            }
            ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(surface, x, y, color), ROBO_OK);
        }
    }
    return surface;
}

/** Encode a logo and register it as `Assets/Logo.png`; takes ownership of @p surface. */
static inline void render_assets_begin(render_assets_t *assets)
{
    robo_gfx_encode_options_t options;

    memset(assets, 0, sizeof(*assets));
    assets->surface = render_logo_surface();
    robo_gfx_encode_options_init(&options);
    options.container = ROBO_GFX_ENCODE_PNG;
    options.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&assets->encoded, 0, sizeof(assets->encoded));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(assets->surface, &options, &assets->encoded),
                      ROBO_OK);
    ROBO_CHECK(assets->encoded.size > 0u);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_create(NULL, 0u, &assets->assets), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_image_assets_add(assets->assets, "Assets/Logo.png",
                                                 assets->encoded.data, assets->encoded.size, NULL),
                      ROBO_OK);
}

static inline void render_assets_end(render_assets_t *assets)
{
    robo_html_image_assets_destroy(assets->assets);
    assets->assets = NULL;
    robo_gfx_encoded_buffer_release(&assets->encoded);
    robo_gfx_surface_release(assets->surface);
    assets->surface = NULL;
}

/**
 * Resolve the image element with class @p name through @p assets, **before** layout runs.
 *
 * Before, because that is the order a real document loads in and because it keeps the fixture honest:
 * the resource is attached to the element before anything reads a box, so nothing here can depend on
 * a paint-time mutation being visible.
 */
static inline void render_resolve_image(paint_fixture_doc_t *fixture,
                                        const char *name,
                                        const render_assets_t *assets)
{
    robo_html_node_handle_t element = paint_find(fixture, name);
    robo_html_image_resolver_t resolver = robo_html_image_assets_resolver(assets->assets);

    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_is_wellformed(&element), 1);
    ROBO_CHECK_EQ_INT(robo_html_element_image_resolve(&element, &resolver, NULL), ROBO_OK);
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

typedef struct render_synth_table {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} render_synth_table_t;

static inline void render_put_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static inline void render_put_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static inline void render_put_i16(uint8_t **p, int16_t v) { render_put_u16(p, (uint16_t)v); }
static inline void render_put_u32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}
static inline void render_put_i64(uint8_t **p, int64_t v)
{
    int i;

    for (i = 7; i >= 0; i--) {
        render_put_u8(p, (uint8_t)((uint64_t)v >> (i * 8)));
    }
}

static inline size_t render_build_sfnt(uint8_t *out,
                                       size_t out_cap,
                                       const render_synth_table_t *tables,
                                       uint32_t n)
{
    size_t dir_end = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dir_end;
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint32_t i;
    uint8_t *p;

    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > out_cap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    render_put_u32(&p, ROBO_FONT_SFNT_VERSION_TRUE);
    while ((uint16_t)(pow2 * 2u) <= n) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = out + 4;
    render_put_u16(&p, (uint16_t)n);
    render_put_u16(&p, (uint16_t)(pow2 * 16u));
    render_put_u16(&p, entry);
    render_put_u16(&p, (uint16_t)(n * 16u - pow2 * 16u));

    cursor = dir_end;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *entry_p = out + 12u + (size_t)i * 16u;

        render_put_u32(&entry_p, tables[i].tag);
        render_put_u32(&entry_p, 0u);
        render_put_u32(&entry_p, (uint32_t)cursor);
        render_put_u32(&entry_p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return cursor;
}

static inline size_t render_build_head(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;

    render_put_u16(&p, 1);
    render_put_u16(&p, 0);
    render_put_u32(&p, 0x00010000u);
    render_put_u32(&p, 0u);
    render_put_u32(&p, ROBO_FONT_HEAD_MAGIC);
    render_put_u16(&p, 0u);
    render_put_u16(&p, upem);
    render_put_i64(&p, 0);
    render_put_i64(&p, 0);
    render_put_i16(&p, -50);
    render_put_i16(&p, -200);
    render_put_i16(&p, 1000);
    render_put_i16(&p, 800);
    render_put_u16(&p, 0u);
    render_put_u16(&p, 8u);
    render_put_i16(&p, 2);
    render_put_i16(&p, 1); /* long loca */
    render_put_i16(&p, 0);
    return (size_t)(p - b);
}

static inline size_t render_build_hhea(uint8_t *b, uint16_t n_h_metrics)
{
    uint8_t *p = b;
    int i;

    render_put_u16(&p, 1);
    render_put_u16(&p, 0);
    render_put_i16(&p, 800);
    render_put_i16(&p, -200);
    render_put_i16(&p, 90);
    render_put_u16(&p, 900);
    render_put_i16(&p, -10);
    render_put_i16(&p, -20);
    render_put_i16(&p, 880);
    render_put_i16(&p, 1);
    render_put_i16(&p, 0);
    render_put_i16(&p, 0);
    for (i = 0; i < 4; i++) {
        render_put_i16(&p, 0);
    }
    render_put_i16(&p, 0);
    render_put_u16(&p, n_h_metrics);
    return (size_t)(p - b);
}

static inline size_t render_build_maxp(uint8_t *b, uint16_t num_glyphs)
{
    uint8_t *p = b;
    int i;

    render_put_u32(&p, 0x00010000u);
    render_put_u16(&p, num_glyphs);
    render_put_u16(&p, 8);
    render_put_u16(&p, 2);
    for (i = 0; i < 12; i++) {
        render_put_u16(&p, 0);
    }
    return (size_t)(p - b);
}

static inline size_t render_build_hmtx(uint8_t *b, const uint16_t *advances, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        render_put_u16(&p, advances[i]);
        render_put_i16(&p, (int16_t)(i + 1));
    }
    return (size_t)(p - b);
}

/** One glyph: a square from (0, 0) to (100, 100) in FUnits. */
static inline size_t render_build_square_glyf(uint8_t *b)
{
    uint8_t *p = b;

    render_put_i16(&p, 1);                        /* numberOfContours */
    render_put_i16(&p, 0);                        /* xMin */
    render_put_i16(&p, 0);                        /* yMin */
    render_put_i16(&p, RENDER_FACE_SQUARE_UNITS); /* xMax */
    render_put_i16(&p, RENDER_FACE_SQUARE_UNITS); /* yMax */
    render_put_u16(&p, 3);                        /* endPtsOfContours[0] */
    render_put_u16(&p, 0);                        /* instructionLength */
    render_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE |
                                ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                                ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    render_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                                ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                                ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    render_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_Y_SHORT |
                                ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE |
                                ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    render_put_u8(&p, (uint8_t)(ROBO_FONT_GLYF_FLAG_ON_CURVE | ROBO_FONT_GLYF_FLAG_X_SHORT |
                                ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE));
    render_put_u8(&p, RENDER_FACE_SQUARE_UNITS);
    render_put_u8(&p, RENDER_FACE_SQUARE_UNITS);
    render_put_u8(&p, RENDER_FACE_SQUARE_UNITS);
    return (size_t)(p - b);
}

/** A one-subtable cmap holding a format-4 segment table mapping `'A'` to glyph 1. */
static inline size_t render_build_cmap(uint8_t *b)
{
    uint8_t *p = b;
    uint8_t *length_ptr;
    uint8_t *subtable = b + 12;
    uint16_t first = 0x41u;

    render_put_u16(&p, 0);   /* version */
    render_put_u16(&p, 1);   /* numTables */
    render_put_u16(&p, 3);   /* Windows */
    render_put_u16(&p, 1);   /* Unicode BMP */
    render_put_u32(&p, 12u); /* subtable offset */

    render_put_u16(&p, 4); /* format */
    length_ptr = p;
    render_put_u16(&p, 0); /* length, patched below */
    render_put_u16(&p, 0); /* language */
    render_put_u16(&p, 4); /* segCountX2: two segments */
    render_put_u16(&p, 4); /* searchRange */
    render_put_u16(&p, 1); /* entrySelector */
    render_put_u16(&p, 0); /* rangeShift */
    render_put_u16(&p, first);
    render_put_u16(&p, 0xFFFFu);
    render_put_u16(&p, 0); /* reservedPad */
    render_put_u16(&p, first);
    render_put_u16(&p, 0xFFFFu);
    render_put_i16(&p, (int16_t)(1 - (int32_t)first)); /* idDelta[0]: 'A' -> glyph 1 */
    render_put_i16(&p, 1);                             /* idDelta[1] */
    render_put_u16(&p, 0);                             /* idRangeOffset[0] */
    render_put_u16(&p, 0);                             /* idRangeOffset[1] */

    {
        uint16_t length = (uint16_t)(p - subtable);

        length_ptr[0] = (uint8_t)(length >> 8);
        length_ptr[1] = (uint8_t)length;
    }
    return (size_t)(p - b);
}

/** A face plus the source it came from, so a case releases both the way it built them. */
typedef struct render_face {
    robo_font_source_t *source;
    robo_font_face_t *face;
} render_face_t;

/** Build the synthetic face: `'A'` maps to the one square glyph, everything else to `.notdef`. */
static inline void render_face_create(render_face_t *out)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[64], loca[64], glyf[128], cmap[64];
    uint8_t font[1024];
    static const uint16_t advances[RENDER_FACE_NUM_GLYPHS] = {
        (uint16_t)RENDER_FACE_ADVANCE_NOTDEF,
        (uint16_t)RENDER_FACE_ADVANCE_GLYPH
    };
    render_synth_table_t tables[7];
    uint32_t nt = 0u;
    size_t glyph_size;
    size_t n;
    uint8_t *p;

    out->source = NULL;
    out->face = NULL;

    glyph_size = render_build_square_glyf(glyf);
    /*
     * `loca` has `numGlyphs + 1` entries, and the two leading zeroes are the *statement*: glyph 0
     * (`.notdef`) is empty, so its outline spans nothing and its end offset equals its start. One
     * mapped glyph and one empty one is exactly what makes "a mapped code point draws and an unmapped
     * one does not" true of this face.
     */
    {
        const uint32_t offsets[RENDER_FACE_NUM_GLYPHS + 1u] = { 0u, 0u, (uint32_t)glyph_size };

        p = loca;
        for (nt = 0u; nt <= (uint32_t)RENDER_FACE_NUM_GLYPHS; nt++) {
            render_put_u32(&p, offsets[nt]);
        }
    }
    nt = 0u;

    tables[nt].tag = ROBO_FONT_TAG_HEAD;
    tables[nt].data = head;
    tables[nt].length = (uint32_t)render_build_head(head, (uint16_t)RENDER_FACE_UPEM);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HHEA;
    tables[nt].data = hhea;
    tables[nt].length = (uint32_t)render_build_hhea(hhea, (uint16_t)RENDER_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_MAXP;
    tables[nt].data = maxp;
    tables[nt].length = (uint32_t)render_build_maxp(maxp, (uint16_t)RENDER_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_HMTX;
    tables[nt].data = hmtx;
    tables[nt].length =
        (uint32_t)render_build_hmtx(hmtx, advances, (uint16_t)RENDER_FACE_NUM_GLYPHS);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_CMAP;
    tables[nt].data = cmap;
    tables[nt].length = (uint32_t)render_build_cmap(cmap);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_LOCA;
    tables[nt].data = loca;
    tables[nt].length = (uint32_t)((RENDER_FACE_NUM_GLYPHS + 1u) * 4u);
    nt++;
    tables[nt].tag = ROBO_FONT_TAG_GLYF;
    tables[nt].data = glyf;
    tables[nt].length = (uint32_t)glyph_size;
    nt++;

    n = render_build_sfnt(font, sizeof(font), tables, nt);
    ROBO_CHECK(n > 0u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(font, n, &out->source), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(out->source, 0, &out->face), ROBO_OK);
}

static inline void render_face_destroy(render_face_t *face)
{
    if (face->face) {
        robo_font_face_release(face->face);
        face->face = NULL;
    }
    if (face->source) {
        robo_font_source_release(face->source);
        face->source = NULL;
    }
}

#endif /* ROBO_RENDER_TEST_SUPPORT_H */
