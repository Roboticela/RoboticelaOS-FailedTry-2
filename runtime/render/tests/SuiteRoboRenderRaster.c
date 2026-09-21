/*
 * RoboRender raster suite (Phase 11.12) — `render.raster.*`.
 *
 * `render.scene.*` beside it renders a *document*; this suite is about the **primitive** claims a
 * rasteriser makes, and every one of them is checked at a pixel or a byte rather than argued:
 *
 *   **a frame that draws nothing changes nothing**   a document with no background, no border, no text
 *                                                   and no image executes its whole list
 *                                                   (`commands > 0`) and leaves every byte of the
 *                                                   target as it was (`draws == 0`), which is the
 *                                                   statement that structural commands are not
 *                                                   drawings;
 *
 *   **a fill is a rectangle, half-open**            the border box's own edges are painted and the
 *                                                   pixel one past each edge is not -- pinned at
 *                                                   all four edges, because a rasteriser that is
 *                                                   off by one looks right in the middle;
 *
 *   **alpha and opacity are two numbers**           a half-transparent fill and a half-opaque element
 *                                                   each land *between* the page and the source, and
 *                                                   each equals a reference built by driving the
 *                                                   graphics raster directly with the same colour,
 *                                                   opacity and rectangle. That equality is the
 *                                                   plumbing claim: the frame passed the right
 *                                                   colour, strength and geometry down;
 *
 *   **the boundary clips, and the target bounds**   a child that overhangs an `overflow: hidden`
 *                                                   ancestor is cut at the ancestor's padding edge,
 *                                                   and a box that overhangs the *target* is cut at
 *                                                   the target -- with the suite run under a
 *                                                   sanitizer, which is what makes "no write outside
 *                                                   the buffer" checkable rather than asserted;
 *
 *   **identical input, identical bytes**            the same list into two targets, built in
 *                                                   different orders and at different times, is
 *                                                   compared byte for byte, with the record and the
 *                                                   dump compared beside it;
 *
 *   **a list this build cannot execute refuses**    a hand-built list with an unbalanced scope pair
 *                                                   is refused *before a pixel is written* -- the
 *                                                   target's checksum is unchanged -- because half a
 *                                                   frame is not a frame;
 *
 *   **a region is clamped, and a region that misses does nothing**  both are the arithmetic a
 *                                                   partial repaint depends on, and both are
 *                                                   asserted on the *record* (which rectangle was
 *                                                   executed) and on the pixels.
 *
 * Every case builds a real document through the real chain and runs a real frame. Hand-built lists
 * appear only where the question *is* the list's shape. Chromium is reference only.
 */

#include "RoboRenderTestSupport.h"

#include "RoboGraphics/RoboGraphicsRaster.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/* The page every pixel case starts from, and the two colours a case names. */
#define RASTER_WHITE robo_gfx_rgba8(0xff, 0xff, 0xff, 255)
#define RASTER_RED robo_gfx_rgba8(0xcc, 0x00, 0x00, 255)
#define RASTER_BLUE robo_gfx_rgba8(0x33, 0x66, 0xcc, 255)
#define RASTER_GREEN robo_gfx_rgba8(0x00, 0xd0, 0x40, 255)

#define RASTER_BASE_CSS "html, body, div, p, span { display: block }"

/* ------------------------------------------------------------------ */
/* A reference frame, driven through the graphics raster by hand       */
/* ------------------------------------------------------------------ */

/**
 * Fill @p rect into @p target with @p color at @p opacity, through RoboGraphics' own raster.
 *
 * The reference the alpha and opacity cases compare against, and it is not a second implementation of
 * the blend: it is the *same* primitive (`robo_gfx_raster_fill_rect`) reached by a different road. What
 * the comparison actually proves is that the frame handed that primitive the right rectangle, the
 * right colour and the right strength -- which is precisely what this module is responsible for and
 * what a hard-coded expected byte would not have distinguished.
 */
static void raster_reference(robo_render_target_t *target,
                             robo_layout_rect_t rect,
                             robo_gfx_color_rgba8_t color,
                             uint8_t opacity)
{
    robo_gfx_raster_t *raster = NULL;

    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(robo_render_target_surface(target), &raster), ROBO_OK);
    robo_gfx_raster_set_opacity(raster, opacity);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_fill_rect(raster, rect, color), ROBO_OK);
    robo_gfx_raster_destroy(raster);
}

/* ------------------------------------------------------------------ */
/* An empty frame                                                      */
/* ------------------------------------------------------------------ */

static void test_render_raster_empty_frame(void)
{
    render_scene_t scene;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    uint64_t before;

    render_fixture();
    /* A document with structure and nothing to draw: the display list carries the three node markers
     * per box and not one drawing command. */
    render_scene_begin(&scene,
                       "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS);
    target = render_page(48u, 32u, RASTER_WHITE);
    before = robo_render_target_checksum(target);

    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, &frame, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&frame), 1);
    ROBO_CHECK(frame.pass.commands > 0u);
    ROBO_CHECK_EQ_INT((int)frame.pass.draws, 0);
    ROBO_CHECK_EQ_INT((int)frame.pass.filled, 0);
    ROBO_CHECK_EQ_INT((int)frame.pass.skipped, (int)frame.pass.commands);
    ROBO_CHECK_EQ_INT(frame.limited, 0);
    ROBO_CHECK_EQ_INT((int)frame.regions, 0);
    paint_rect_is(0, 0, 48, 32, frame.region, "a full frame's region");
    ROBO_CHECK_EQ_INT((int)frame.region_pixels, 48 * 32);
    ROBO_CHECK_EQ_INT((int)frame.target_pixels, 48 * 32);
    /* Not one byte moved, and the record says why: nothing in that list is a drawing. */
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));
    render_pixel_is(target, 0u, 0u, RASTER_WHITE, "an empty frame");
    render_pixel_is(target, 47u, 31u, RASTER_WHITE, "an empty frame");

    render_target_end(target);
    render_scene_end(&scene);
}

static void test_render_raster_argument_rules(void)
{
    render_scene_t scene;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    robo_render_diag_t diag;

    render_fixture();
    render_scene_begin(&scene, "<html><body></body></html>", RASTER_BASE_CSS);
    target = render_page(16u, 16u, RASTER_WHITE);
    robo_render_diag_reset(&diag);

    ROBO_CHECK_EQ_INT(robo_render_rasterize(NULL, target, NULL, &frame, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_FRAME);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, NULL, NULL, &frame, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_rasterize_regions(NULL, target, NULL, NULL, &frame, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_rasterize_regions(scene.display, NULL, NULL, NULL, &frame, &diag),
                      ROBO_INVALID_ARGUMENT);

    /* A NULL record is legal: a caller that only wants pixels is not made to hold a record. */
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, NULL, NULL), ROBO_OK);

    render_target_end(target);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* Rectangles                                                          */
/* ------------------------------------------------------------------ */

static void test_render_raster_background_pixels(void)
{
    render_scene_t scene;
    robo_render_target_t *target;
    robo_render_frame_t frame;

    render_fixture();
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px;"
                       " background-color: #3366cc }");
    target = render_page(64u, 40u, RASTER_WHITE);

    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, &frame, NULL), ROBO_OK);
    ROBO_CHECK(frame.pass.backgrounds >= 1u);
    ROBO_CHECK(frame.pass.filled >= 1u);

    /* The box's border box is `(4, 4, 40, 24)`: half-open, so `x` is inside and `x + width` is
     * not. All four edges, because a rasteriser that is off by one is still right in the middle. */
    render_pixel_is(target, 4u, 4u, RASTER_BLUE, "the box's own top-left corner");
    render_pixel_is(target, 43u, 27u, RASTER_BLUE, "the box's own bottom-right corner");
    render_pixel_is(target, 3u, 4u, RASTER_WHITE, "one pixel left of the box");
    render_pixel_is(target, 44u, 4u, RASTER_WHITE, "one pixel right of the box");
    render_pixel_is(target, 4u, 3u, RASTER_WHITE, "one pixel above the box");
    render_pixel_is(target, 4u, 28u, RASTER_WHITE, "one pixel below the box");
    /* And the page colour really is the *body's*, not a leftover: the body fills the viewport. */
    render_pixel_is(target, 60u, 36u, RASTER_WHITE, "the page beyond the box");

    render_target_end(target);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* Alpha and opacity                                                   */
/* ------------------------------------------------------------------ */

static void test_render_raster_alpha_and_opacity(void)
{
    render_scene_t scene;
    robo_render_target_t *alpha_frame;
    robo_render_target_t *alpha_reference;
    robo_render_target_t *opacity_frame;
    robo_render_target_t *opacity_reference;
    robo_render_frame_t frame;
    robo_gfx_color_rgba8_t blended;

    render_fixture();

    /*
     * A half-transparent background: the colour's own alpha, with no `opacity` in the document at
     * all. The rectangle is `(4, 4, 40, 24)` and the reference drives the same fill with the same
     * channels -- including alpha -- at full strength.
     */
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       " .box { width: 40px; height: 24px; margin: 4px;"
                       " background-color: rgba(0, 0, 0, 0.5) }");
    alpha_frame = render_page(64u, 40u, RASTER_WHITE);
    alpha_reference = render_page(64u, 40u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, alpha_frame, NULL, &frame, NULL),
                      ROBO_OK);
    raster_reference(alpha_reference, robo_gfx_rect(4, 4, 40, 24), robo_gfx_rgba8(0, 0, 0, 128),
                     255u);
    render_targets_equal(alpha_frame, alpha_reference, "a half-transparent fill");
    blended = render_pixel(alpha_frame, 10u, 10u);
    /* And the pixel really is *between* the two colours -- otherwise the equality above would be a
     * statement about a fill that did nothing. */
    ROBO_CHECK(blended.r != 0u && blended.r != 0xff);
    ROBO_CHECK(blended.r == blended.g && blended.g == blended.b);
    render_pixel_is(alpha_frame, 2u, 2u, RASTER_WHITE, "outside a half-transparent fill");
    render_target_end(alpha_reference);
    render_target_end(alpha_frame);
    render_scene_end(&scene);

    /*
     * The same picture through the other door: an **opaque** colour inside an element at
     * `opacity: 0.5`. Two numbers, one result -- which is the claim `RoboPaintOpacity.h` makes and
     * the reason the reference passes the strength to the raster instead of the test blending by
     * hand.
     */
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       " .box { width: 40px; height: 24px; margin: 4px; opacity: 0.5;"
                       " background-color: #000000 }");
    opacity_frame = render_page(64u, 40u, RASTER_WHITE);
    opacity_reference = render_page(64u, 40u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, opacity_frame, NULL, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK(frame.pass.faded >= 1u);
    raster_reference(opacity_reference, robo_gfx_rect(4, 4, 40, 24), robo_gfx_rgba8(0, 0, 0, 255),
                     128u);
    render_targets_equal(opacity_frame, opacity_reference, "a half-opaque element");
    blended = render_pixel(opacity_frame, 10u, 10u);
    ROBO_CHECK(blended.r != 0u && blended.r != 0xff);
    render_pixel_is(opacity_frame, 2u, 2u, RASTER_WHITE, "outside a half-opaque element");
    render_target_end(opacity_reference);
    render_target_end(opacity_frame);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* Boundaries                                                          */
/* ------------------------------------------------------------------ */

static void test_render_raster_clip_and_bounds(void)
{
    render_scene_t scene;
    robo_render_target_t *target;
    robo_render_frame_t frame;

    render_fixture();
    /*
     * An `overflow: hidden` parent whose child is far wider than it: CSS clips the *contents* at the
     * padding edge, and the parent's own background is drawn under the boundary the parent was entered
     * with -- which is the arrangement `paint/docs/Clipping.md` argues and the one a rasteriser can
     * get wrong invisibly.
     */
    render_scene_begin(&scene,
                       "<html><body><div class=\"clip\"><div class=\"wide\"></div></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .clip { width: 40px; height: 20px; overflow: hidden;"
                       " background-color: #3300cc }"
                       " .wide { width: 200px; height: 20px; background-color: #cc0000 }");
    target = render_page(80u, 40u, RASTER_WHITE);

    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, &frame, NULL), ROBO_OK);
    ROBO_CHECK(frame.pass.clip_scopes >= 1u);
    ROBO_CHECK(frame.pass.clipped >= 1u);

    /* Inside the boundary the child is drawn over its parent's background. */
    render_pixel_is(target, 0u, 0u, RASTER_RED, "the child inside the clip");
    render_pixel_is(target, 39u, 19u, RASTER_RED, "the last clipped pixel");
    /* And outside it -- one pixel past the parent's padding edge -- the child is gone, while the page
     * beyond it is untouched. */
    render_pixel_is(target, 40u, 0u, RASTER_WHITE, "one pixel past the clip");
    render_pixel_is(target, 60u, 10u, RASTER_WHITE, "the page past the clip");

    render_target_end(target);
    render_scene_end(&scene);
}

static void test_render_raster_offscreen_and_edges(void)
{
    render_scene_t scene;
    robo_render_target_t *target;
    robo_render_frame_t frame;

    render_fixture();
    /*
     * Three boxes and a target smaller than any of them: one hanging off the top-left (negative
     * offsets), one entirely off the right, and one inside. Under the sanitizer gate this is the case
     * that proves "never writes outside the pixel buffer", and without it the visible part of the
     * first box must still be exactly where the geometry says.
     */
    render_scene_begin(&scene,
                       "<html><body><div class=\"off\"></div><div class=\"away\"></div>"
                       "<div class=\"on\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .off { width: 40px; height: 20px; margin-left: -12px; margin-top: -6px;"
                       " background-color: #00d040 }"
                       " .away { width: 30px; height: 10px; margin-left: 200px;"
                       " background-color: #cc0000 }"
                       " .on { width: 8px; height: 8px; margin-left: 4px;"
                       " background-color: #3366cc }");
    target = render_page(32u, 24u, RASTER_WHITE);

    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, &frame, NULL), ROBO_OK);
    /* The box that hangs off the corner is drawn from the target's own origin: the part that fits is
     * at the geometry's coordinates, and nothing wrapped around. */
    render_pixel_is(target, 0u, 0u, RASTER_GREEN, "a box hanging off the origin");
    render_pixel_is(target, 27u, 13u, RASTER_GREEN, "the last pixel of that box");
    render_pixel_is(target, 28u, 14u, RASTER_WHITE, "one pixel past it");
    /* The box entirely outside drew nothing at all, anywhere. */
    render_pixel_is(target, 31u, 23u, RASTER_WHITE, "a box entirely off the target");

    render_target_end(target);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

static void test_render_raster_determinism(void)
{
    render_scene_t scene;
    robo_render_target_t *first;
    robo_render_target_t *second;
    robo_render_target_t *noise;
    robo_render_frame_t frame_a;
    robo_render_frame_t frame_b;
    char dump_a[2048];
    char dump_b[2048];
    size_t length_a = 0u;
    size_t length_b = 0u;

    render_fixture();
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 30px; height: 18px; margin: 5px; opacity: 0.75;"
                       " background-color: rgba(0, 0, 0, 0.5) }");

    first = render_page(48u, 32u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, first, NULL, &frame_a, NULL), ROBO_OK);

    /* Allocation order between the two runs is deliberately different: a target made, used and
     * released, so the second run's buffer is a different buffer at a different address. Nothing in
     * the output may depend on that. */
    noise = render_page(20u, 20u, RASTER_RED);
    render_target_end(noise);

    second = render_page(48u, 32u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, second, NULL, &frame_b, NULL), ROBO_OK);

    render_targets_equal(first, second, "two runs of one list");
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(&frame_a, &frame_b), 1);
    ROBO_CHECK(robo_render_target_checksum(first) != 0u);
    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame_a, dump_a, sizeof(dump_a), &length_a, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame_b, dump_b, sizeof(dump_b), &length_b, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)length_a, (int)length_b);
    ROBO_CHECK_STR_EQ(dump_a, dump_b);

    /* Re-running the frame into the *same* target twice is also stable, which is the stronger
     * statement: a second execution over its own output must not drift, and a `clear` in between must
     * make it identical to the first run. */
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, second, NULL, NULL, NULL), ROBO_OK);
    render_targets_equal(first, second, "a re-run over its own output");
    ROBO_CHECK_EQ_INT(robo_render_target_clear(second, RASTER_WHITE, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, second, NULL, NULL, NULL), ROBO_OK);
    render_targets_equal(first, second, "a clear and re-run");

    render_target_end(second);
    render_target_end(first);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* The preflight                                                       */
/* ------------------------------------------------------------------ */

static void test_render_raster_preflight_refuses(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_command_t command;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    robo_render_diag_t diag;
    uint64_t before;

    render_fixture();
    target = render_page(16u, 16u, RASTER_WHITE);
    before = robo_render_target_checksum(target);

    /* A list a builder would never produce: a `pop-clip` that closes a scope nothing opened. The
     * *record* is valid (a known kind carrying a boundary in force), so the list's own append accepts
     * it -- which is what makes the balance rule the rasteriser's to check. A clip command's validity
     * rule is the one 11.8 states: it must carry an **active** boundary, because a scope that bounds
     * neither axis is a command whose fields disagree about what it is. */
    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_POP_CLIP;
    command.node = (robo_layout_node_id_t)1;
    command.rect = robo_gfx_rect(0, 0, 16, 16);
    command.clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    command.clip.horizontal.start = 0;
    command.clip.horizontal.end = 16;
    command.clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    command.clip.vertical.start = 0;
    command.clip.vertical.end = 16;
    ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, NULL), ROBO_OK);

    robo_render_frame_init(&frame);
    frame.checksum = 0xfeedu;
    ROBO_CHECK_EQ_INT(robo_render_rasterize(list, target, NULL, &frame, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_LIST);
    ROBO_CHECK(diag.message != NULL);
    /* **All-or-nothing**: the refusal happened before a pixel was written, and the record the caller
     * passed in was not overwritten with a half-made one. */
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));
    ROBO_CHECK_EQ_INT((int)frame.checksum, (int)0xfeed);
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&frame), 0);
    robo_paint_display_list_clear(list);

    /* The other half of the pair: a `push-clip` nothing closes, which would leave every following
     * drawing bounded by a scope the list never ends. */
    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP;
    command.node = (robo_layout_node_id_t)1;
    command.rect = robo_gfx_rect(0, 0, 16, 16);
    command.clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    command.clip.horizontal.start = 0;
    command.clip.horizontal.end = 16;
    command.clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    command.clip.vertical.start = 0;
    command.clip.vertical.end = 16;
    ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(list, target, NULL, &frame, &diag), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));
    robo_paint_display_list_clear(list);

    /* And the *other* half of the preflight -- an unknown command kind -- is unreachable through the
     * public API, which is asserted rather than assumed: the list's own append refuses such a command,
     * so the check exists for a list this build did not produce. */
    robo_paint_display_command_init(&command);
    command.kind = (robo_paint_display_command_kind_t)99;
    command.node = (robo_layout_node_id_t)1;
    command.rect = robo_gfx_rect(0, 0, 16, 16);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);
    robo_paint_display_list_destroy(list);

    render_target_end(target);
}

/* ------------------------------------------------------------------ */
/* Text without a face                                                 */
/* ------------------------------------------------------------------ */

static void test_render_raster_no_font_is_a_number(void)
{
    render_scene_t scene;
    render_face_t face;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    uint64_t before;

    render_fixture();
    render_scene_begin(&scene, "<html><body><p class=\"line\">AA</p></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .line { font-size: 100px; color: #cc0000 }");
    target = render_page(64u, 32u, RASTER_WHITE);
    before = robo_render_target_checksum(target);

    /* No face: nothing is drawn, and the count says so rather than the frame being silently blank. */
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, &frame, NULL), ROBO_OK);
    ROBO_CHECK(frame.pass.texts >= 1u);
    ROBO_CHECK_EQ_INT((int)frame.pass.no_font, (int)frame.pass.texts);
    ROBO_CHECK_EQ_INT((int)frame.pass.glyphs, 0);
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));

    /* With the face, the run's code points resolve through its cmap and its glyphs stroke the square
     * outline the fixture states -- so a pixel changes and the count of glyph draws equals the count of
     * characters. */
    render_face_create(&face);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, face.face, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)frame.pass.no_font, 0);
    ROBO_CHECK_EQ_INT((int)frame.pass.glyphs, 2);
    ROBO_CHECK_EQ_INT((int)frame.pass.glyphs_missing, 0);
    ROBO_CHECK(robo_render_target_checksum(target) != before);
    /* The run's own box, scanned for ink rather than pinned to a pixel: at `font-size: 100px` the one
     * mapped glyph is a square **outline** ten pixels on a side, so its middle is background and its
     * edges are the run. Naming a pixel here would be asserting about the glyph's interior. */
    render_region_has_ink_is(target, 0, 0, 16, 14, RASTER_WHITE, "a run's glyphs");
    render_face_destroy(&face);

    render_target_end(target);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* Regions                                                             */
/* ------------------------------------------------------------------ */

static void test_render_raster_region_clamping(void)
{
    render_scene_t scene;
    robo_paint_invalidation_set_t *set = NULL;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    uint64_t before;

    render_fixture();
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    target = render_page(64u, 40u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, NULL, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_create(&set, NULL), ROBO_OK);

    /* A region that runs off the target: executed at the intersection, and the record reports the
     * rectangle it *executed* rather than the one it was handed -- so "did my region reach the
     * surface?" is answerable from the frame alone. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(set, robo_gfx_rect(48, 20, 100, 100),
                                                           ROBO_LAYOUT_NODE_ID_NONE,
                                                           ROBO_PAINT_INVALIDATION_REASON_STYLE, NULL,
                                                           NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_rasterize_regions(scene.display, target, NULL, set, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(frame.limited, 1);
    ROBO_CHECK_EQ_INT((int)frame.regions, 1);
    paint_rect_is(48, 20, 16, 20, frame.region, "a region clamped to the target");
    ROBO_CHECK_EQ_INT((int)frame.region_pixels, 16 * 20);
    ROBO_CHECK(frame.region_pixels < frame.target_pixels);
    /* The box is at `(4, 4, 40, 24)` and the region starts at x = 48, so the run executed the list and
     * drew nothing of it: `bounded_out` is the counter that says the work was outside the window. */
    ROBO_CHECK(frame.pass.bounded_out >= 1u);
    render_pixel_is(target, 10u, 10u, RASTER_BLUE, "the box outside the region");

    /* A region that misses the target entirely: success, nothing executed, not one byte moved. */
    before = robo_render_target_checksum(target);
    robo_paint_invalidation_set_clear(set);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(set, robo_gfx_rect(500, 500, 10, 10),
                                                           ROBO_LAYOUT_NODE_ID_NONE,
                                                           ROBO_PAINT_INVALIDATION_REASON_STYLE, NULL,
                                                           NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_rasterize_regions(scene.display, target, NULL, set, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)frame.region_pixels, 0);
    ROBO_CHECK_EQ_INT((int)frame.pass.commands, 0);
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));
    render_pixel_is(target, 10u, 10u, RASTER_BLUE, "a region that missed the target");

    /* And a region *inside* the target repaints exactly the box: the same list, a smaller window. */
    robo_paint_invalidation_set_clear(set);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(set, robo_gfx_rect(0, 0, 48, 32),
                                                           ROBO_LAYOUT_NODE_ID_NONE,
                                                           ROBO_PAINT_INVALIDATION_REASON_STYLE, NULL,
                                                           NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_target_clear(target, RASTER_WHITE, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_rasterize_regions(scene.display, target, NULL, set, &frame, NULL),
                      ROBO_OK);
    paint_rect_is(0, 0, 48, 32, frame.region, "a region inside the target");
    render_pixel_is(target, 10u, 10u, RASTER_BLUE, "the box inside the region");

    robo_paint_invalidation_set_destroy(set);
    render_target_end(target);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_render_raster(void)
{
    robo_test_register("render.raster.empty_frame", test_render_raster_empty_frame);
    robo_test_register("render.raster.argument_rules", test_render_raster_argument_rules);
    robo_test_register("render.raster.background_pixels", test_render_raster_background_pixels);
    robo_test_register("render.raster.alpha_and_opacity", test_render_raster_alpha_and_opacity);
    robo_test_register("render.raster.clip_and_bounds", test_render_raster_clip_and_bounds);
    robo_test_register("render.raster.offscreen_and_edges", test_render_raster_offscreen_and_edges);
    robo_test_register("render.raster.determinism", test_render_raster_determinism);
    robo_test_register("render.raster.preflight_refuses", test_render_raster_preflight_refuses);
    robo_test_register("render.raster.no_font_is_a_number",
                       test_render_raster_no_font_is_a_number);
    robo_test_register("render.raster.region_clamping", test_render_raster_region_clamping);
}
