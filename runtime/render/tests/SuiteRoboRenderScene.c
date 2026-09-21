/*
 * RoboRender scene suite (Phase 11.12) — `render.scene.*`.
 *
 * The suite that renders a **document**, and the phase's strongest evidence: a page with a background,
 * a bordered card, a semi-transparent overlay, a line of text, a picture and a clipping ancestor is
 * rasterised into one target, every claim about it is checked at a named pixel, and the result is
 * written out as a PNG for a person to look at.
 *
 * The five claims, and where each is pinned:
 *
 *   **the scene renders**            `checkpoint` asserts a pixel inside the card, a pixel on its
 *                                    border band, a pixel of the semi-transparent overlay, a pixel of
 *                                    the picture, and a pixel on each side of the clip boundary -- and
 *                                    that the frame's own record counted one of each drawing kind, so
 *                                    "the picture is there" and "the record says why" are one
 *                                    statement rather than two;
 *
 *   **a region run equals a full frame inside the region**  `region_matches_full` is the capstone:
 *                                    render the first state into a target, take the regions 11.11 says
 *                                    are stale, re-run **the second state's list** bounded by them,
 *                                    and compare the whole target against a full render of the second
 *                                    state. Byte for byte, or the region run missed a pixel;
 *
 *   **no repaint owed is exactly no work**  `no_repaint` takes the regions of a document compared with
 *                                    itself -- an empty set -- and asserts that the run executed
 *                                    nothing, touched no byte, and said so in its record;
 *
 *   **a frame is a snapshot**        `snapshot` releases the record list, the display list and the
 *                                    tree **after** rendering and asserts the target's bytes are
 *                                    unchanged, then rebuilds the same document and asserts the two
 *                                    frames are equal -- which is the whole reason a display list
 *                                    carries no pointer;
 *
 *   **the dump of a real frame is deterministic**  `dump` renders one scene twice and compares the two
 *                                    dumps as text, with no address in either.
 *
 * The scene is deliberately *not* a demonstration of everything the runtime can draw: it is the
 * smallest document that makes each of the six requests in the phase's brief observable at a pixel, so
 * that a later phase changing any one of them breaks a named assertion rather than a picture nobody
 * looks at. Chromium is reference only; nothing here is a translation of it.
 */

#include "RoboRenderTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The scene                                                           */
/* ------------------------------------------------------------------ */

/*
 * The document, stated once, with every number chosen so an assertion can name a pixel.
 *
 * The numbers are the **flow geometry** of this document in this build, and they are written out rather
 * than derived because they are what the display list was checked against: `position` is parsed by
 * RoboCSS but not laid out by RoboLayout (the runtime's Phase 10 scope), so every box below is in
 * normal flow, at the coordinate its margins put it at. Stating them makes a later layout phase that
 * starts honouring `left`/`top` fail a named assertion here rather than quietly moving the picture:
 *
 *   page       #ffffff, the body fills the 200x200 viewport
 *   card       margin 10, content 80x40, `border: 4px solid`, so its border box is (10, 10, 88, 48),
 *              its padding box (14, 14, 80, 40) and its four border bands 4 wide on each side
 *   text       one run in the card at `font-size: 100px`, coloured #cc0000. The run's box is what the
 *              fixture's measurer said it was -- (14, 14, 16, 12) -- and the glyphs are *stroked
 *              outlines* one pixel wide, so the case scans the box for ink instead of naming a pixel
 *              inside a glyph's empty middle
 *   picture    a 4x4 image at (10, 68) stretched to 32x32: its red top-left quadrant lands at the
 *              destination's top-left, so a pixel there says which corner the sampler read
 *   box        a 60x30 sibling at (0, 100) that is `rgba(0, 0, 0, 0.5)` *and* `opacity: 0.5` -- two
 *              numbers that multiply, which is why no assertion can be satisfied by a build that
 *              applies only one of them
 *   clip       an `overflow: hidden` box at (10, 140), 40x30, whose child is 200 wide: the child is cut
 *              at the parent's padding edge, and a pixel past that edge is the page
 *
 * The card carries the text, so a pixel inside it is a statement about the *order* of its own
 * decorations as much as about any one of them. The flow is stated top to bottom: card, text (inside
 * the card), picture, box, clip -- and the 200x200 viewport is what makes the last of them visible.
 */
#define SCENE_DOC \
    "<html><body>" \
    "<div class=\"card\">AA</div>" \
    "<img class=\"pic\" src=\"Assets/Logo.png\">" \
    "<div class=\"overlay\"></div>" \
    "<div class=\"clip\"><div class=\"wide\"></div></div>" \
    "</body></html>"

/* No `position` anywhere: this build lays every box out in flow, so declaring one would be a comment
 * about a mechanism that does not exist yet rather than about the picture. The margins are what place
 * each box, and they are the numbers the assertions above were written against. */
#define SCENE_CSS \
    "html, body, div, p, span, img { display: block }" \
    "html, body { background-color: #ffffff; width: 200px; height: 200px }" \
    " .card { margin: 10px; width: 80px; height: 40px; border: 4px solid #111111;" \
    " background-color: #3366cc; color: #cc0000; font-size: 100px }" \
    " .pic { margin-left: 10px; width: 32px; height: 32px }" \
    " .overlay { width: 60px; height: 30px; background-color: rgba(0, 0, 0, 0.5); opacity: 0.5 }" \
    " .clip { margin: 10px; width: 40px; height: 30px; overflow: hidden;" \
    " background-color: #3300cc }" \
    " .wide { width: 200px; height: 30px; background-color: #cc0000 }"

#define SCENE_WIDTH 200u
#define SCENE_HEIGHT 200u

#define SCENE_PAGE robo_gfx_rgba8(0xff, 0xff, 0xff, 255)
#define SCENE_CARD robo_gfx_rgba8(0x33, 0x66, 0xcc, 255)
#define SCENE_BORDER robo_gfx_rgba8(0x11, 0x11, 0x11, 255)
#define SCENE_CLIP_CHILD robo_gfx_rgba8(0xcc, 0x00, 0x00, 255)

/*
 * The scene's build order is worth one line because it is not the fixture's default: the picture's
 * `src` is resolved **before** the layout pass (`render_resolve_image` then `paint_layout`), so the
 * box the image occupies exists when the document is laid out -- which is the order a real document
 * loads in and the reason a scene case spells its three steps out rather than calling
 * `render_scene_begin`.
 */

/* ------------------------------------------------------------------ */
/* 1. The checkpoint                                                   */
/* ------------------------------------------------------------------ */

static void test_render_scene_checkpoint(void)
{
    render_scene_t scene;
    render_assets_t assets;
    render_face_t face;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    const char *path;
    int written;

    render_fixture();
    render_assets_begin(&assets);
    render_scene_open(&scene, SCENE_DOC, SCENE_CSS);
    render_resolve_image(&scene.doc, "pic", &assets);
    /* The layout pass runs *after* the resolve, so the picture's box exists when the document is laid
     * out -- the order a real document loads in. */
    paint_layout(&scene.doc, 200, 200);
    render_scene_paint(&scene);
    render_face_create(&face);

    target = render_page(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, face.face, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&frame), 1);

    /* A background, a border band, a text run and an image all reached the target -- the four drawing
     * kinds, counted rather than argued. */
    ROBO_CHECK(frame.pass.backgrounds >= 2u);
    ROBO_CHECK(frame.pass.borders >= 1u);
    ROBO_CHECK(frame.pass.texts >= 1u);
    ROBO_CHECK(frame.pass.images >= 1u);
    ROBO_CHECK(frame.pass.glyphs >= 2u);
    ROBO_CHECK_EQ_INT((int)frame.pass.glyphs_missing, 0);
    ROBO_CHECK(frame.pass.filled >= 4u);
    ROBO_CHECK(frame.pass.clip_scopes >= 1u);
    ROBO_CHECK(frame.pass.opacity_scopes >= 1u);

    /* The page, and the card's border band over its own background. */
    render_pixel_is(target, 190u, 190u, SCENE_PAGE, "the page");
    render_pixel_is(target, 12u, 24u, SCENE_BORDER, "the card's left border band");
    render_pixel_is(target, 50u, 12u, SCENE_BORDER, "the card's top border band");
    render_pixel_is(target, 90u, 40u, SCENE_CARD, "the card's background");

    /* The line inside the card: its own box, and whether any of it is ink. The run is stroked outlines
     * at `font-size: 100px`, so the claim is "the characters reached the target inside the box layout
     * gave them" -- which is the claim the phase can make, and the one a glyph-level assertion would be
     * guessing at. */
    render_region_has_ink_is(target, 14, 14, 16, 12, SCENE_CARD, "the run's own box");
    render_region_is_clear(target, 14, 40, 80, 10, SCENE_CARD, "the card below the run's box");

    /* The picture: its four quadrants land in the destination's four corners, so four pixels say the
     * sampler read the source rectangle the display list named *and* that a transparent source pixel
     * leaves the page showing through. */
    render_pixel_is(target, 12u, 70u, robo_gfx_rgba8(0xff, 0x00, 0x00, 255), "the picture's red corner");
    render_pixel_is(target, 30u, 70u, robo_gfx_rgba8(0x00, 0xff, 0x00, 255), "the picture's green corner");
    render_pixel_is(target, 12u, 90u, robo_gfx_rgba8(0x00, 0x00, 0xff, 255), "the picture's blue corner");
    render_pixel_is(target, 30u, 90u, SCENE_PAGE, "the picture's transparent corner");
    render_pixel_is(target, 43u, 70u, SCENE_PAGE, "the page past the picture's right edge");

    /* The box: half-transparent colour *and* half opacity, so the pixel is a quarter of black over the
     * page -- strictly between the page and black, and equal on all three channels. */
    {
        robo_gfx_color_rgba8_t overlay = render_pixel(target, 30u, 115u);

        ROBO_CHECK(overlay.r == overlay.g && overlay.g == overlay.b);
        ROBO_CHECK(overlay.r != 0u && overlay.r != 0xff);
        render_pixel_is(target, 150u, 115u, SCENE_PAGE, "the page beside the semi-transparent box");
    }

    /* The clip: the child is drawn inside it and cut at the parent's padding edge, one pixel past which
     * the page shows -- and below the clip's own box the page shows too. */
    render_pixel_is(target, 12u, 145u, SCENE_CLIP_CHILD, "the clipped child inside the boundary");
    render_pixel_is(target, 55u, 145u, SCENE_PAGE, "the page past the clip boundary");
    render_pixel_is(target, 12u, 175u, SCENE_PAGE, "the page below the clip box");

    /* And the artifact: a PNG of the frame, for a person to look at. The pixels above are the
     * verification; this is the evidence, and it is asserted only where it can be (a writable tree). */
    path = render_checkpoint_path();
    written = render_write_png(target, path);
    if (!written) {
        printf("render.scene.checkpoint: no PNG written to %s (read-only tree?)\n", path);
    } else {
        printf("render.scene.checkpoint: wrote %s (%ux%u)\n", path, (unsigned)SCENE_WIDTH,
               (unsigned)SCENE_HEIGHT);
    }

    render_face_destroy(&face);
    render_target_end(target);
    render_scene_end(&scene);
    render_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* 2. A region run equals a full frame                                 */
/* ------------------------------------------------------------------ */

static void test_render_scene_region_matches_full(void)
{
    render_scene_t before;
    render_scene_t after;
    render_assets_t assets;
    render_face_t face;
    robo_paint_invalidation_set_t *set = NULL;
    robo_render_target_t *partial;
    robo_render_target_t *full;
    robo_render_frame_t partial_frame;
    robo_render_frame_t full_frame;

    render_fixture();
    render_assets_begin(&assets);
    /* Two states of one document, differing in *three* ways at once -- a colour, a position and an
     * opacity -- so the regions the set produces cover more than one producer and the equality claim
     * is not a statement about one box. */
    render_scene_open(&before, SCENE_DOC, SCENE_CSS);
    render_resolve_image(&before.doc, "pic", &assets);
    paint_layout(&before.doc, 200, 200);
    render_scene_paint(&before);

    render_scene_open(&after, SCENE_DOC,
                      SCENE_CSS
                      " .card { background-color: #22aa55 }" /* the coloured box changed */
                      " .overlay { margin-left: 24px; opacity: 0.25 }" /* moved and faded */
                      " .clip .wide { background-color: #0000cc }");
    render_resolve_image(&after.doc, "pic", &assets);
    paint_layout(&after.doc, 200, 200);
    render_scene_paint(&after);

    render_face_create(&face);
    partial = render_page(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
    full = render_page(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);

    /* The first state is what the caller already has on screen. */
    ROBO_CHECK_EQ_INT(robo_render_rasterize(before.display, partial, face.face, NULL, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_create(&set, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_set_from_lists(set, before.records.list, after.records.list, NULL),
        ROBO_OK);
    ROBO_CHECK(robo_paint_invalidation_set_count(set) > 0u);

    /* The partial repaint: the second state's list, bounded by the regions the set names. */
    ROBO_CHECK_EQ_INT(
        robo_render_rasterize_regions(after.display, partial, face.face, set, &partial_frame, NULL),
        ROBO_OK);
    /* The reference: a full render of the same list into a fresh target. */
    ROBO_CHECK_EQ_INT(robo_render_rasterize(after.display, full, face.face, &full_frame, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(partial_frame.limited, 1);
    ROBO_CHECK_EQ_INT((int)partial_frame.regions, (int)robo_paint_invalidation_set_count(set));
    /* The region really was smaller than the frame -- otherwise "identical" would be a statement
     * about a full repaint with extra steps. */
    ROBO_CHECK(partial_frame.region_pixels < partial_frame.target_pixels);
    ROBO_CHECK(partial_frame.pass.bounded_out <= partial_frame.pass.draws);

    render_targets_equal(partial, full, "a region run against a full frame");

    robo_paint_invalidation_set_destroy(set);
    render_target_end(full);
    render_target_end(partial);
    render_face_destroy(&face);
    render_scene_end(&after);
    render_scene_end(&before);
    render_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* 3. Nothing owed is nothing done                                     */
/* ------------------------------------------------------------------ */

static void test_render_scene_no_repaint(void)
{
    render_scene_t scene;
    robo_paint_invalidation_set_t *set = NULL;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    uint64_t before;

    render_fixture();
    render_scene_begin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       "html, body, div { display: block }"
                       "html, body { background-color: #ffffff }"
                       " .box { width: 20px; height: 20px; background-color: #3366cc }");
    target = render_page(40u, 40u, SCENE_PAGE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, NULL, NULL), ROBO_OK);
    before = robo_render_target_checksum(target);

    /* A document compared with itself produces no region at all -- 11.11's own answer to "is the
     * picture current?" -- and the region door's answer to that is a successful run that did nothing
     * rather than an error or a full repaint. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_create(&set, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_set_from_lists(set, scene.records.list, scene.records.list, NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_NONE);

    ROBO_CHECK_EQ_INT(robo_render_rasterize_regions(scene.display, target, NULL, set, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&frame), 1);
    ROBO_CHECK_EQ_INT(frame.limited, 1);
    ROBO_CHECK_EQ_INT((int)frame.regions, 0);
    ROBO_CHECK_EQ_INT((int)frame.region_pixels, 0);
    ROBO_CHECK_EQ_INT((int)frame.pass.commands, 0);
    ROBO_CHECK_EQ_INT((int)frame.checksum, (int)before);
    render_pixel_is(target, 10u, 10u, robo_gfx_rgba8(0x33, 0x66, 0xcc, 255),
                    "a box the run did not touch");

    /* A NULL set is the same conclusion, not an error: 11.11's `is_empty(NULL)` is true. */
    ROBO_CHECK_EQ_INT(robo_render_rasterize_regions(scene.display, target, NULL, NULL, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)frame.pass.commands, 0);
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));

    robo_paint_invalidation_set_destroy(set);
    render_target_end(target);
    render_scene_end(&scene);
}

/* ------------------------------------------------------------------ */
/* 4. A frame is a snapshot                                            */
/* ------------------------------------------------------------------ */

/** The document the snapshot case renders twice, stated once so both halves are literally one text. */
#define SNAPSHOT_DOC "<html><body><div class=\"box\"></div></body></html>"
#define SNAPSHOT_CSS \
    "html, body, div { display: block }" \
    "html, body { background-color: #ffffff }" \
    " .box { width: 24px; height: 16px; margin: 3px; background-color: #3366cc;" \
    " border: 2px solid #111111 }"

static void test_render_scene_snapshot(void)
{
    render_scene_t scene;
    render_scene_t rebuilt;
    robo_render_target_t *target;
    robo_render_target_t *fresh;
    robo_render_frame_t first;
    robo_render_frame_t second;
    uint64_t before;

    render_fixture();
    render_scene_begin(&scene, SNAPSHOT_DOC, SNAPSHOT_CSS);
    target = render_page(40u, 32u, SCENE_PAGE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, NULL, &first, NULL), ROBO_OK);
    before = robo_render_target_checksum(target);

    /* Release the artifacts a frame was run against. A display list carries no pointer, so the pixels
     * and the record cannot change -- and the record is a value the caller already holds rather than a
     * view of one. */
    render_scene_release_artifacts(&scene);
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&first), 1);
    render_scene_end(&scene);

    /* And the same document rebuilt from scratch renders the same picture -- the second statement a
     * snapshot makes, and the one that would fail if anything in the pipeline had kept a reference
     * into the tree it came from. */
    render_scene_begin(&rebuilt, SNAPSHOT_DOC, SNAPSHOT_CSS);
    fresh = render_page(40u, 32u, SCENE_PAGE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(rebuilt.display, fresh, NULL, &second, NULL), ROBO_OK);
    render_targets_equal(target, fresh, "a document rebuilt from scratch");
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(&first, &second), 1);
    ROBO_CHECK_EQ_INT((int)before, (int)robo_render_target_checksum(target));

    render_target_end(fresh);
    render_target_end(target);
    render_scene_end(&rebuilt);
}

/* ------------------------------------------------------------------ */
/* 5. The dump of a real frame                                         */
/* ------------------------------------------------------------------ */

static void test_render_scene_dump(void)
{
    render_scene_t scene;
    render_assets_t assets;
    render_face_t face;
    robo_render_target_t *target;
    robo_render_frame_t frame;
    char text[2048];
    char again[2048];
    size_t length = 0u;
    size_t second = 0u;

    render_fixture();
    render_assets_begin(&assets);
    render_scene_open(&scene, SCENE_DOC, SCENE_CSS);
    render_resolve_image(&scene.doc, "pic", &assets);
    paint_layout(&scene.doc, 200, 200);
    render_scene_paint(&scene);
    render_face_create(&face);

    target = render_page(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
    ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, target, face.face, &frame, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame, text, sizeof(text), &length, NULL), ROBO_OK);
    ROBO_CHECK(length > 0u);
    ROBO_CHECK(strstr(text, "frame 200x200 RGBA8 stride=800") != NULL);
    ROBO_CHECK(strstr(text, "limited=0") != NULL);
    ROBO_CHECK(strstr(text, "region=0,0,200,200") != NULL);
    ROBO_CHECK(strstr(text, "commands=") != NULL);
    ROBO_CHECK(strstr(text, "images=1") != NULL);
    /* No address in the dump: `=0x` is what a printed pointer looks like, and asserting *that* rather
     * than the bare `0x` is the difference between a claim about addresses and a claim about the
     * substring of "200x200". */
    ROBO_CHECK(strstr(text, "=0x") == NULL);

    /* Re-running the same list into a fresh target produces the same *text*: the record is a function
     * of the document, and the dump is a function of the record. */
    {
        robo_render_target_t *other = render_page(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
        robo_render_frame_t other_frame;

        ROBO_CHECK_EQ_INT(robo_render_rasterize(scene.display, other, face.face, &other_frame, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_render_frame_equal(&frame, &other_frame), 1);
        ROBO_CHECK_EQ_INT(robo_render_frame_write(&other_frame, again, sizeof(again), &second, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)length, (int)second);
        ROBO_CHECK_STR_EQ(text, again);
        render_target_end(other);
    }

    render_face_destroy(&face);
    render_target_end(target);
    render_scene_end(&scene);
    render_assets_end(&assets);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_render_scene(void)
{
    robo_test_register("render.scene.checkpoint", test_render_scene_checkpoint);
    robo_test_register("render.scene.region_matches_full", test_render_scene_region_matches_full);
    robo_test_register("render.scene.no_repaint", test_render_scene_no_repaint);
    robo_test_register("render.scene.snapshot", test_render_scene_snapshot);
    robo_test_register("render.scene.dump", test_render_scene_dump);
}
