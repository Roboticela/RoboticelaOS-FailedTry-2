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
 *                                                   executed) and on the pixels;
 *
 *   **a frame lands where it is told to**            11.13's half, and it is asserted the only way a
 *                                                   presentation can be: the *destination* is read
 *                                                   back. A frame presented at an offset puts a named
 *                                                   pixel of the picture at the same offset in the
 *                                                   destination, the destination's own pixels outside
 *                                                   the frame are untouched, and the record's two pixel
 *                                                   counts sum to the frame's own. A frame that does
 *                                                   not fit is clipped rather than refused, and the
 *                                                   destination proves which pixels survived.
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
/* Presentation (11.13)                                                */
/* ------------------------------------------------------------------ */

/** A page colour for the two destinations, deliberately not one the scenes use. */
#define PRESENT_DESK robo_gfx_rgba8(0xf5, 0xf5, 0xf7, 255)

/** An owned destination surface, cleared to @p fill. */
static robo_gfx_surface_t *present_surface(uint32_t width,
                                          uint32_t height,
                                          robo_gfx_pixel_format_t format,
                                          robo_gfx_color_rgba8_t fill)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = format;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(surface, fill), ROBO_OK);
    return surface;
}

/** A destination pixel, through the graphics layer's own read (format-independent). */
static robo_gfx_color_rgba8_t present_pixel(const robo_gfx_surface_t *surface,
                                           uint32_t x,
                                           uint32_t y)
{
    robo_gfx_color_rgba8_t color;

    memset(&color, 0, sizeof(color));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, x, y, &color), ROBO_OK);
    return color;
}

static void present_pixel_is(const robo_gfx_surface_t *surface,
                            uint32_t x,
                            uint32_t y,
                            robo_gfx_color_rgba8_t expected,
                            const char *what)
{
    robo_gfx_color_rgba8_t actual = present_pixel(surface, x, y);

    if (!render_same_color(actual, expected)) {
        robo_test_fail(__FILE__, __LINE__,
                       "%s: destination pixel %u,%u is %u,%u,%u,%u, expected %u,%u,%u,%u", what,
                       (unsigned)x, (unsigned)y, (unsigned)actual.r, (unsigned)actual.g,
                       (unsigned)actual.b, (unsigned)actual.a, (unsigned)expected.r,
                       (unsigned)expected.g, (unsigned)expected.b, (unsigned)expected.a);
    }
}

/*
 * The region door on its own: the arithmetic a caller asks *before* it decides to present. Every case
 * here is one rectangle, and the point of all of them is that the answer is the frame's own rectangle
 * moved by the origin and cut to the destination -- including the two shapes that are easy to get
 * wrong and impossible to notice: a frame hanging off an edge, and a frame with a negative origin.
 */
static void test_render_present_region(void)
{
    robo_render_target_t *frame;
    robo_gfx_surface_t *dest;
    robo_layout_rect_t region;
    robo_render_diag_t diag;

    render_fixture();
    robo_render_diag_reset(&diag);
    frame = render_page(16u, 8u, RASTER_BLUE);
    dest = present_surface(64u, 32u, ROBO_GFX_FORMAT_RGBA8, PRESENT_DESK);

    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, dest, 0, 0, &region, &diag), ROBO_OK);
    paint_rect_is(0, 0, 16, 8, region, "a frame at the destination's origin");

    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, dest, 48, 24, &region, &diag), ROBO_OK);
    paint_rect_is(48, 24, 16, 8, region, "a frame inside the destination");

    /* Off the right edge and the bottom edge: the meeting, not the request. */
    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, dest, 60, 30, &region, &diag), ROBO_OK);
    paint_rect_is(60, 30, 4, 2, region, "a frame hanging off the destination");

    /* A negative origin cuts the *left* edge, so the rectangle starts at zero and is narrower. */
    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, dest, -4, -4, &region, &diag), ROBO_OK);
    paint_rect_is(0, 0, 12, 4, region, "a frame with a negative origin");

    /* Entirely off: an empty rectangle and a success, which is the answer "nothing to show here". */
    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, dest, 64, 0, &region, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_rect_is_empty(region), 1);
    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, dest, 0, 32, &region, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_rect_is_empty(region), 1);

    /* The region door is a question, so it answers for a read-only destination too -- it writes
     * nothing, and refusing to answer would make a caller allocate a surface it does not need. */
    {
        robo_gfx_surface_desc_t desc;
        robo_gfx_surface_t *read_only = NULL;

        robo_gfx_surface_desc_init(&desc);
        desc.width = 8u;
        desc.height = 8u;
        desc.format = ROBO_GFX_FORMAT_RGBA8;
        desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
        ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &read_only), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_render_present_region(frame, read_only, 0, 0, &region, &diag),
                          ROBO_OK);
        paint_rect_is(0, 0, 8, 8, region, "a frame into a read-only destination");
        robo_gfx_surface_release(read_only);
    }

    ROBO_CHECK_EQ_INT(robo_render_present_region(NULL, dest, 0, 0, &region, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_FRAME);
    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, NULL, 0, 0, &region, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_present_region(frame, dest, 0, 0, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(dest);
    render_target_end(frame);
}

/*
 * The presentation itself, read back from the destination. The frame is a real document's frame -- a
 * white page with a blue box at (4, 4, 40, 24) -- so the pixel assertions are about a *picture* rather
 * than about a fill this suite wrote itself.
 */
static void test_render_present_pixels(void)
{
    render_scene_t scene;
    robo_render_target_t *frame;
    robo_gfx_surface_t *dest;
    robo_gfx_surface_t *other;
    robo_render_present_record_t record;
    robo_render_present_record_t again;
    uint64_t target_checksum;

    render_fixture();
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    frame = render_page(64u, 40u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, frame, NULL, NULL, NULL), ROBO_OK);
    target_checksum = robo_render_target_checksum(frame);

    dest = present_surface(128u, 96u, ROBO_GFX_FORMAT_RGBA8, PRESENT_DESK);

    /* The record first: four facts a caller reads, and all four are the frame's own. */
    ROBO_CHECK_EQ_INT(robo_render_present(frame, dest, 32, 24, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&record), 1);
    ROBO_CHECK_EQ_INT((int)record.frame_width, 64);
    ROBO_CHECK_EQ_INT((int)record.frame_height, 40);
    ROBO_CHECK_EQ_INT((int)record.dest_width, 128);
    ROBO_CHECK_EQ_INT((int)record.dest_height, 96);
    ROBO_CHECK_EQ_INT((int)record.dest_format, (int)ROBO_GFX_FORMAT_RGBA8);
    ROBO_CHECK_EQ_INT(record.offset_x, 32);
    ROBO_CHECK_EQ_INT(record.offset_y, 24);
    paint_rect_is(32, 24, 64, 40, record.presented, "a frame that fits");
    ROBO_CHECK_EQ_INT((int)record.presented_pixels, 64 * 40);
    ROBO_CHECK_EQ_INT((int)record.clipped_pixels, 0);
    ROBO_CHECK_EQ_INT(record.complete, 1);
    /* The checksum is the *picture*, taken from the frame rather than re-read from the destination. */
    ROBO_CHECK((int)record.frame_checksum == (int)target_checksum);

    /* Then the pixels, at the offset: the box's corner, the page around it, and the destination's own
     * colour everywhere the frame did not reach. */
    present_pixel_is(dest, 32u + 10u, 24u + 10u, RASTER_BLUE, "the box, presented at an offset");
    present_pixel_is(dest, 32u + 60u, 24u + 36u, RASTER_WHITE, "the page, presented at an offset");
    present_pixel_is(dest, 0u, 0u, PRESENT_DESK, "the destination above the frame");
    present_pixel_is(dest, 31u, 24u, PRESENT_DESK, "the destination one pixel left of the frame");
    present_pixel_is(dest, 96u, 24u, PRESENT_DESK, "the destination one pixel right of the frame");
    /* `y = 24 + 40` is the first row *past* the frame: the rectangle is half-open, so the frame's last
     * row is 63 and 64 is the destination's own colour again. */
    present_pixel_is(dest, 32u, 64u, PRESENT_DESK, "the destination one pixel below the frame");
    present_pixel_is(dest, 32u, 63u, RASTER_WHITE, "the frame's own last row");
    present_pixel_is(dest, 127u, 95u, PRESENT_DESK, "the destination's far corner");

    /*
     * Every frame pixel equals the destination pixel it was copied to -- the strong form of "the frame
     * landed", and cheap enough at this size to do in full rather than by sampling.
     */
    {
        uint32_t x;
        uint32_t y;
        int mismatches = 0;

        for (y = 0u; y < 40u; y++) {
            for (x = 0u; x < 64u; x++) {
                if (!render_same_color(render_pixel(frame, x, y),
                                       present_pixel(dest, x + 32u, y + 24u))) {
                    mismatches++;
                }
            }
        }
        ROBO_CHECK_EQ_INT(mismatches, 0);
    }

    /* Presenting the same frame again is the same presentation: no state, no "already shown" flag,
     * and an opaque frame over its own pixels is a fixed point. */
    ROBO_CHECK_EQ_INT(robo_render_present(frame, dest, 32, 24, &again, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_present_record_equal(&record, &again), 1);
    present_pixel_is(dest, 32u + 10u, 24u + 10u, RASTER_BLUE, "a frame presented twice");

    /* A second destination at a *different* offset holds the same picture, moved: the door has no
     * memory of the first one. */
    other = present_surface(128u, 96u, ROBO_GFX_FORMAT_RGBA8, PRESENT_DESK);
    ROBO_CHECK_EQ_INT(robo_render_present(frame, other, 0, 0, &again, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(again.offset_x, 0);
    present_pixel_is(other, 10u, 10u, RASTER_BLUE, "the same picture at the origin");
    present_pixel_is(other, 32u + 10u, 24u + 10u, RASTER_WHITE,
                     "the page where the first destination had the box");

    /* And a BGRA8 destination holds the same picture, logically: the conversion is the graphics
     * module's, and this assertion is what proves the presenter did not lay the channels out itself. */
    {
        robo_gfx_surface_t *bgra = present_surface(64u, 40u, ROBO_GFX_FORMAT_BGRA8, PRESENT_DESK);

        ROBO_CHECK_EQ_INT(robo_render_present(frame, bgra, 0, 0, &again, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(again.complete, 1);
        present_pixel_is(bgra, 10u, 10u, RASTER_BLUE, "a box in a BGRA8 destination");
        present_pixel_is(bgra, 60u, 36u, RASTER_WHITE, "a page in a BGRA8 destination");
        robo_gfx_surface_release(bgra);
    }

    robo_gfx_surface_release(other);
    robo_gfx_surface_release(dest);
    render_target_end(frame);
    render_scene_end(&scene);
}

/*
 * A frame that does not fit: clipped rather than refused, and the counts have to add up. This is the
 * resize case -- a window that shrank between the frame being rasterised and the frame being shown --
 * and the case that would silently lose a picture if the record's arithmetic were wrong.
 */
static void test_render_present_clipping(void)
{
    render_scene_t scene;
    robo_render_target_t *frame;
    robo_gfx_surface_t *dest;
    robo_render_present_record_t record;

    render_fixture();
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    frame = render_page(64u, 40u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, frame, NULL, NULL, NULL), ROBO_OK);
    dest = present_surface(32u, 16u, ROBO_GFX_FORMAT_RGBA8, PRESENT_DESK);

    ROBO_CHECK_EQ_INT(robo_render_present(frame, dest, 0, 0, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&record), 1);
    paint_rect_is(0, 0, 32, 16, record.presented, "the part of the frame that fits");
    ROBO_CHECK_EQ_INT((int)record.presented_pixels, 32 * 16);
    ROBO_CHECK_EQ_INT((int)record.clipped_pixels, 64 * 40 - 32 * 16);
    /* The two counts are the frame, split: a caller can check the arithmetic rather than trust it. */
    ROBO_CHECK_EQ_INT((int)(record.presented_pixels + record.clipped_pixels),
                      (int)robo_render_target_pixel_count(frame));
    ROBO_CHECK_EQ_INT(record.complete, 0);

    /* The surviving corner: the frame's own (31, 15), which the box covers. */
    present_pixel_is(dest, 31u, 15u, RASTER_BLUE, "the last pixel of a clipped frame");
    present_pixel_is(dest, 0u, 0u, RASTER_WHITE, "the frame's own origin in a small destination");

    /*
     * And a placement that puts the frame *entirely* off the destination: success, nothing written, and
     * every pixel counted as clipped. Not a failure -- a caller that has just resized should be told
     * "nothing of this frame is visible" rather than handed an error it cannot act on.
     */
    {
        robo_gfx_surface_t *far = present_surface(32u, 16u, ROBO_GFX_FORMAT_RGBA8, PRESENT_DESK);

        ROBO_CHECK_EQ_INT(robo_render_present(frame, far, 40, 20, &record, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_gfx_rect_is_empty(record.presented), 1);
        ROBO_CHECK_EQ_INT((int)record.presented_pixels, 0);
        ROBO_CHECK_EQ_INT((int)record.clipped_pixels, 64 * 40);
        ROBO_CHECK_EQ_INT(record.complete, 0);
        /* A record that describes a presentation of nothing is still *valid*: the counts agree. */
        ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&record), 1);
        present_pixel_is(far, 0u, 0u, PRESENT_DESK, "a destination a frame fell outside of");
        present_pixel_is(far, 31u, 15u, PRESENT_DESK, "a destination a frame fell outside of");
        robo_gfx_surface_release(far);
    }

    robo_gfx_surface_release(dest);
    render_target_end(frame);
    render_scene_end(&scene);
}

/*
 * The framebuffer door: the display-side shape, where the pixels are a mapping somebody else owns.
 */
static void test_render_present_framebuffer(void)
{
    render_scene_t scene;
    robo_render_target_t *frame;
    robo_gfx_framebuffer_desc_t desc;
    robo_gfx_framebuffer_t *fb = NULL;
    robo_render_present_record_t record;
    robo_gfx_color_rgba8_t color;
    const uint8_t *pixel = NULL;

    render_fixture();
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    frame = render_page(64u, 40u, RASTER_WHITE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, frame, NULL, NULL, NULL), ROBO_OK);

    robo_gfx_framebuffer_desc_init(&desc);
    desc.width = 64u;
    desc.height = 40u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&desc, &fb), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_render_present_framebuffer(frame, fb, 0, 0, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(record.complete, 1);
    paint_rect_is(0, 0, 64, 40, record.presented, "a frame filling a framebuffer");

    /* Read one pixel the framebuffer's own way, so the assertion is about the mapping and not about a
     * surface this file wrapped around it. */
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel_const(fb, 10u, 10u, &pixel), ROBO_OK);
    ROBO_CHECK(pixel != NULL);
    memset(&color, 0, sizeof(color));
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_RGBA8, pixel, 4u, &color), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)color.r, (int)RASTER_BLUE.r);
    ROBO_CHECK_EQ_INT((int)color.g, (int)RASTER_BLUE.g);
    ROBO_CHECK_EQ_INT((int)color.b, (int)RASTER_BLUE.b);
    /* The box is a *margin* box, so its top-left pixel is at (4, 4) and the pixel one before it is
     * the page: the same half-open rule the raster applied, seen through the display-side mapping. */
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_pixel_const(fb, 3u, 4u, &pixel), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_unpack(ROBO_GFX_FORMAT_RGBA8, pixel, 4u, &color), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)color.r, 255);
    ROBO_CHECK_EQ_INT((int)color.b, 255);

    ROBO_CHECK_EQ_INT(robo_render_present_framebuffer(frame, NULL, 0, 0, &record, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_present_framebuffer(NULL, fb, 0, 0, &record, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_framebuffer_release(fb);
    render_target_end(frame);
    render_scene_end(&scene);
}

/*
 * The record's own rules and the developer's dump. A record is what a caller logs, so a record whose
 * fields disagree has to be refusable -- and the dump is the text a person reads back.
 */
static void test_render_present_record_and_dump(void)
{
    robo_render_target_t *frame;
    robo_gfx_surface_t *dest;
    robo_render_present_record_t record;
    robo_render_present_record_t bad;
    char text[512];
    size_t needed = 0u;
    size_t count = 0u;

    render_fixture();
    frame = render_page(16u, 8u, RASTER_BLUE);
    dest = present_surface(64u, 32u, ROBO_GFX_FORMAT_RGBA8, PRESENT_DESK);
    ROBO_CHECK_EQ_INT(robo_render_present(frame, dest, 8, 8, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&record), 1);

    /* Field-for-field equality, and the two rules every comparison door in this tree keeps. */
    bad = record;
    ROBO_CHECK_EQ_INT(robo_render_present_record_equal(&record, &bad), 1);
    bad.presented_pixels = 1u;
    ROBO_CHECK_EQ_INT(robo_render_present_record_equal(&record, &bad), 0);
    ROBO_CHECK_EQ_INT(robo_render_present_record_equal(&record, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_render_present_record_equal(NULL, NULL), 1);

    /* One clause at a time: each of these is a record two fields of which disagree about what it is. */
    bad = record;
    bad.presented_pixels = record.presented_pixels - 1u;
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&bad), 0);
    bad = record;
    bad.clipped_pixels = 1u;
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&bad), 0);
    bad = record;
    bad.complete = 0;
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&bad), 0);
    bad = record;
    bad.frame_width = 0u;
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&bad), 0);
    bad = record;
    bad.dest_format = ROBO_GFX_FORMAT_BGRA8;
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&bad), 0);
    bad = record;
    bad.presented.width = -1;
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&bad), 0);

    /* The dump: four lines, one per group of facts. */
    ROBO_CHECK_EQ_INT(robo_render_present_write(&record, text, sizeof(text), &needed, NULL), ROBO_OK);
    ROBO_CHECK(needed > 0u);
    ROBO_CHECK_EQ_INT((int)needed, (int)strlen(text));
    {
        const char *at = text;

        while (*at != '\0') {
            if (*at == '\n') {
                count++;
            }
            at++;
        }
    }
    ROBO_CHECK_EQ_INT((int)count, 4);
    /* The format names come from the pixel-format layer, which spells them in the case the frame dump
     * beside this suite already uses -- read rather than restated. */
    ROBO_CHECK(strstr(text, "present 16x8 RGBA8 -> 64x32 RGBA8 at 8,8") != NULL);
    ROBO_CHECK(strstr(text, "presented=8,8,16,8") != NULL);
    ROBO_CHECK(strstr(text, "pixels=128 clipped=0 complete=1") != NULL);
    ROBO_CHECK(strstr(text, "record=valid") != NULL);
    /* No address, ever -- `=0x` and not a bare `0x`, so the assertion is about a printed pointer and
     * not about the substring `16x8` happens to contain. */
    ROBO_CHECK(strstr(text, "=0x") == NULL);

    /* A buffer that is too small reports the length it needed rather than truncating. */
    {
        char small[8];
        robo_render_diag_t diag;

        robo_render_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(robo_render_present_write(&record, small, sizeof(small), &count, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT((int)count, (int)needed);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_DUMP);
        ROBO_CHECK(small[0] != '\0');
    }
    ROBO_CHECK_EQ_INT(robo_render_present_write(NULL, text, sizeof(text), &needed, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_present_write(&record, NULL, sizeof(text), &needed, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_present_write(&record, text, sizeof(text), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_present_write(&record, text, 0u, &needed, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(dest);
    render_target_end(frame);
}

/*
 * What the door refuses, and the reason a read-only destination is one of them: presenting *writes*, so
 * a destination that cannot be written is a caller mistake rather than an empty presentation.
 */
static void test_render_present_refusals(void)
{
    robo_render_target_t *frame;
    robo_gfx_surface_t *dest;
    robo_gfx_surface_t *read_only = NULL;
    robo_gfx_surface_desc_t desc;
    robo_render_present_record_t record;
    robo_render_diag_t diag;

    render_fixture();
    robo_render_diag_reset(&diag);
    frame = render_page(8u, 8u, RASTER_BLUE);
    dest = present_surface(16u, 16u, ROBO_GFX_FORMAT_RGBA8, PRESENT_DESK);

    ROBO_CHECK_EQ_INT(robo_render_present(NULL, dest, 0, 0, &record, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_FRAME);
    ROBO_CHECK(diag.message != NULL);
    ROBO_CHECK_EQ_INT(robo_render_present(frame, NULL, 0, 0, &record, &diag),
                      ROBO_INVALID_ARGUMENT);
    /* The record is optional: a caller that only wants pixels is not made to hold one. */
    ROBO_CHECK_EQ_INT(robo_render_present(frame, dest, 0, 0, NULL, &diag), ROBO_OK);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 16u;
    desc.height = 16u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &read_only), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_present(frame, read_only, 0, 0, &record, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_FRAME);
    robo_gfx_surface_release(read_only);

    robo_gfx_surface_release(dest);
    render_target_end(frame);
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
    robo_test_register("render.present_region", test_render_present_region);
    robo_test_register("render.present_pixels", test_render_present_pixels);
    robo_test_register("render.present_clipping", test_render_present_clipping);
    robo_test_register("render.present_framebuffer", test_render_present_framebuffer);
    robo_test_register("render.present_record_and_dump", test_render_present_record_and_dump);
    robo_test_register("render.present_refusals", test_render_present_refusals);
}
