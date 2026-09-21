/*
 * RoboPaint invalidation suite (Phase 11.11) — paint.invalidation.*
 *
 * The release that answers the question the ten before it raised: given a picture this module
 * already produced, which of its pixels are no longer the pixels the document describes? Every
 * case here is built from **real artifacts** — two documents through the real chain (HTML → CSS
 * → layout → records → display list), or two states of one, or the records of one and a mark
 * naming a box in it — because the thing under test is a *claim about artifacts*, and a
 * hand-built list would prove the walk reads a struct rather than the walk reads a document.
 *
 * The claims the release makes, and where each is pinned:
 *
 *   **a region is sufficient, never exact**            `stale_pixels` paints two frames into two
 *                                                     real surfaces and asserts that **every
 *                                                     pixel that differs** lies inside some
 *                                                     region of the set — the property a partial
 *                                                     repaint is allowed to depend on, checked
 *                                                     against pixels rather than against the
 *                                                     records that produced them;
 *
 *   **and sufficient is enough to repaint**            `partial_repaint_matches_full` is the
 *                                                     capstone: clear the regions of the first
 *                                                     frame, paint the second frame into it, and
 *                                                     compare the whole surface against a fresh
 *                                                     render of the second frame. Identical — or
 *                                                     the invalidation missed something;
 *
 *   **geometry is two rectangles, not one**            `geometry_move` and `geometry_resize` pin
 *                                                     that the old rectangle is in the set as
 *                                                     well as the new, which is the difference
 *                                                     between a repaint and a smear;
 *
 *   **the boundary is read, never invented**           `narrowed` intersects against a boundary
 *                                                     that names both axes; `conservative` keeps
 *                                                     the whole rectangle for an axis with no
 *                                                     extent; `zero_area_and_invalid` pins that
 *                                                     a boundary which excludes everything
 *                                                     produces **no** region rather than an
 *                                                     empty one;
 *
 *   **a no-op is not a repaint**                       `no_op` compares a document with itself
 *                                                     and `unconsumed_properties` a property no
 *                                                     layer consumes: nothing owed, scope NONE;
 *
 *   **the set is a snapshot**                          `mutation_isolation_and_determinism`
 *                                                     releases both artifacts and rebuilds the
 *                                                     transition independently; the set outlives
 *                                                     them and two runs are equal;
 *
 *   **11.1's hook is wired**                           `from_stats` turns `style_only` into a
 *                                                     conservative whole-extent region -- and
 *                                                     into *nothing* when the count is zero.
 *
 * Everything is deterministic, and the dump cases assert it by text: the module writes no
 * address, and `determinism` compares two independently built sets **and their dumps**.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboCSS/RoboCSSOverflow.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"
#include "RoboPaint/RoboPaintInvalidation.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/*
 * One box, one sheet, two states. The geometry is declared rather than intrinsic (this build has
 * no intrinsic sizing and no UA sheet), so a case can move, resize or recolour the box by
 * swapping the sheet and nothing else.
 */
#define INVAL_DOC \
    "<html><body><div class=\"box\"></div></body></html>"

#define INVAL_BASE \
    "html, body, div, p { display: block } " \
    "html, body { background-color: #ffffff } "

#define INVAL_BOX_BLUE \
    ".box { width: 60px; height: 40px; margin: 20px; background-color: #3366cc }"

/** The page's own colour: what a partial repaint clears an invalidated region to. */
#define INVAL_PAGE robo_gfx_rgba8(0xff, 0xff, 0xff, 255)

/** The surface every pixel case renders into. */
#define INVAL_SURFACE_W 200u
#define INVAL_SURFACE_H 160u

static robo_layout_rect_t inval_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    robo_layout_rect_t rect;

    rect.x = x;
    rect.y = y;
    rect.width = w;
    rect.height = h;
    return rect;
}

static robo_paint_invalidation_set_t *inval_set(void)
{
    robo_paint_invalidation_set_t *set = NULL;

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_create(&set, NULL), ROBO_OK);
    ROBO_CHECK(set != NULL);
    return set;
}

static void inval_destroy(robo_paint_invalidation_set_t *set)
{
    robo_paint_invalidation_set_destroy(set);
}

/** A boundary that names both axes: the one case this module may narrow against. */
static robo_paint_clip_t inval_clip_both(int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    robo_paint_clip_t clip;

    robo_paint_clip_init(&clip);
    clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    clip.horizontal.start = x0;
    clip.horizontal.end = x1;
    clip.vertical.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    clip.vertical.start = y0;
    clip.vertical.end = y1;
    return clip;
}

/** A boundary that hides one axis and leaves the other free. */
static robo_paint_clip_t inval_clip_x_only(int32_t x0, int32_t x1)
{
    robo_paint_clip_t clip;

    robo_paint_clip_init(&clip);
    clip.horizontal.mode = ROBO_CSS_OVERFLOW_HIDDEN;
    clip.horizontal.start = x0;
    clip.horizontal.end = x1;
    return clip;
}

/** Assert one region's four fields and both flags. */
static void inval_region_is(const robo_paint_invalidation_region_t *region,
                            robo_layout_rect_t rect,
                            robo_paint_invalidation_reason_t reason,
                            int narrowed,
                            int full,
                            const char *what)
{
    paint_rect_eq(region->rect, rect, what);
    if (region->reason != reason) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected reason %s, got %s", what,
                       robo_paint_invalidation_reason_name(reason),
                       robo_paint_invalidation_reason_name(region->reason));
    }
    if (region->narrowed != narrowed || region->full != full) {
        robo_test_fail(__FILE__, __LINE__, "%s: narrowed=%d full=%d, expected %d/%d", what,
                       region->narrowed, region->full, narrowed, full);
    }
}

static robo_paint_invalidation_region_t inval_at(const robo_paint_invalidation_set_t *set,
                                                 size_t index)
{
    robo_paint_invalidation_region_t region;

    robo_paint_invalidation_region_init(&region);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_at(set, index, &region, NULL), ROBO_OK);
    return region;
}

/** Non-zero when @p rect contains the point. */
static int inval_rect_contains(robo_layout_rect_t rect, int32_t x, int32_t y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/** Non-zero when **some** region of @p set contains the point. */
static int inval_set_contains(const robo_paint_invalidation_set_t *set, int32_t x, int32_t y)
{
    size_t index;

    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        if (inval_rect_contains(inval_at(set, index).rect, x, y)) {
            return 1;
        }
    }
    return 0;
}

/**
 * The **paint-space** border box of @p node, read from its background record.
 *
 * The record carries both coordinate spaces (11.1) and the two are not interchangeable:
 * `robo_paint_boxes_of` answers in layout's, everything in the paint list is in page space, and a
 * case that compared one against the other would be asserting about two different rectangles.
 * So every geometry expectation in this suite is read out of the artifact, in the space the
 * artifact is in.
 *
 * @return 1 when @p node has a background record and @p out was filled; 0 otherwise.
 */
static int inval_box_rect(const paint_result_t *result,
                          robo_layout_node_id_t node,
                          robo_layout_rect_t *out)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return 0;
        }
        if (operation.kind == ROBO_PAINT_OP_BACKGROUND && operation.node == node) {
            *out = operation.boxes.border;
            return 1;
        }
    }
    return 0;
}

/** The `BEGIN_BOX` record of @p node in @p result, when the list has one. */
static int inval_box_start(const paint_result_t *result,
                           robo_layout_node_id_t node,
                           robo_paint_operation_t *out)
{
    size_t index;

    for (index = 0u; index < robo_paint_list_count(result->list); index++) {
        robo_paint_operation_t operation;

        if (robo_paint_list_at(result->list, index, &operation) != ROBO_OK) {
            return 0;
        }
        if (operation.kind == ROBO_PAINT_OP_BEGIN_BOX && operation.node == node) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
    }
    return 0;
}

/** Non-zero when some region of @p set has exactly @p rect and @p node. */
static int inval_set_has(const robo_paint_invalidation_set_t *set,
                         robo_layout_rect_t rect,
                         robo_layout_node_id_t node)
{
    size_t index;

    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        robo_paint_invalidation_region_t region = inval_at(set, index);

        if (region.node == node && region.rect.x == rect.x && region.rect.y == rect.y &&
            region.rect.width == rect.width && region.rect.height == rect.height) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Two states of one document                                          */
/* ------------------------------------------------------------------ */

typedef struct inval_pair {
    paint_fixture_doc_t before;
    paint_fixture_doc_t after;
    paint_result_t before_result;
    paint_result_t after_result;
} inval_pair_t;

/**
 * Build @p html twice: once with @p css_before and once with @p css_after, laid out and painted
 * through the real chain.
 *
 * Two independent trees rather than a mutation of one, which is what makes a comparison honest:
 * both sides are documents, so a region the walk attributes to a node is a node both trees
 * really have.
 */
static void inval_pair_begin(inval_pair_t *pair,
                             const char *html,
                             const char *css_before,
                             const char *css_after)
{
    paint_begin_laid_out(&pair->before, html, css_before);
    paint_once(&pair->before, &pair->before_result);
    paint_begin_laid_out(&pair->after, html, css_after);
    paint_once(&pair->after, &pair->after_result);
}

static void inval_pair_end(inval_pair_t *pair)
{
    paint_result_dispose(&pair->before_result);
    paint_result_dispose(&pair->after_result);
    paint_end(&pair->after);
    paint_end(&pair->before);
}

/* ------------------------------------------------------------------ */
/* Surfaces                                                            */
/* ------------------------------------------------------------------ */

static robo_gfx_surface_t *inval_surface(robo_gfx_color_rgba8_t page)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = INVAL_SURFACE_W;
    desc.height = INVAL_SURFACE_H;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK(surface != NULL);
    /* Every frame starts as the page, which is also what a partial repaint clears to. */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(surface, page), ROBO_OK);
    return surface;
}

/** Build the display list for @p records and paint it into @p target. */
static void inval_render(const robo_paint_list_t *records, robo_gfx_surface_t *target)
{
    robo_paint_display_list_t *display = NULL;
    robo_paint_display_list_builder_t *builder = NULL;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&display, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_paint_display_list_builder_build(builder, records, display, NULL, NULL), ROBO_OK);
    /* No face: the documents these cases render have no text, so nothing needs one. */
    ROBO_CHECK_EQ_INT(robo_paint_decoration_paint(display, target, NULL, NULL, NULL), ROBO_OK);
    robo_paint_display_list_builder_destroy(builder);
    robo_paint_display_list_destroy(display);
}

/** Clear every region of @p set to @p page -- the partial repaint's first half. */
static void inval_clear_regions(const robo_paint_invalidation_set_t *set,
                                robo_gfx_surface_t *target,
                                robo_gfx_color_rgba8_t page)
{
    robo_gfx_raster_t *raster = NULL;
    size_t index;

    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(target, &raster), ROBO_OK);
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        robo_paint_invalidation_region_t region = inval_at(set, index);

        ROBO_CHECK_EQ_INT(robo_gfx_raster_fill_rect(raster, region.rect, page), ROBO_OK);
    }
    robo_gfx_raster_destroy(raster);
}

static robo_gfx_color_rgba8_t inval_pixel(robo_gfx_surface_t *surface, int32_t x, int32_t y)
{
    robo_gfx_color_rgba8_t color;

    memset(&color, 0, sizeof(color));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      ROBO_OK);
    return color;
}

static int inval_same_pixel(robo_gfx_color_rgba8_t a, robo_gfx_color_rgba8_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/**
 * The count of points where @p a and @p b differ, and the number of those points **outside**
 * every region of @p set.
 *
 * The two numbers are the phase's whole claim: the first is what a full repaint would have to
 * fix, the second is what a partial repaint would leave wrong.
 */
static void inval_compare_frames(robo_gfx_surface_t *a,
                                 robo_gfx_surface_t *b,
                                 const robo_paint_invalidation_set_t *set,
                                 size_t *out_differing,
                                 size_t *out_uncovered)
{
    size_t differing = 0u;
    size_t uncovered = 0u;
    int32_t x;
    int32_t y;

    for (y = 0; y < (int32_t)INVAL_SURFACE_H; y++) {
        for (x = 0; x < (int32_t)INVAL_SURFACE_W; x++) {
            if (inval_same_pixel(inval_pixel(a, x, y), inval_pixel(b, x, y))) {
                continue;
            }
            differing++;
            if (!inval_set_contains(set, x, y)) {
                uncovered++;
            }
        }
    }
    *out_differing = differing;
    *out_uncovered = uncovered;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_invalidation_reason_vocabulary(void)
{
    size_t index;
    size_t duplicates = 0u;

    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_INVALIDATION_REASON_COUNT, 11);
    for (index = 0u; index < (size_t)ROBO_PAINT_INVALIDATION_REASON_COUNT; index++) {
        robo_paint_invalidation_reason_t reason = (robo_paint_invalidation_reason_t)index;
        size_t other;

        ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_is_known(reason), 1);
        ROBO_CHECK(strcmp(robo_paint_invalidation_reason_name(reason), "unknown") != 0);
        for (other = 0u; other < index; other++) {
            if (strcmp(robo_paint_invalidation_reason_name(reason),
                       robo_paint_invalidation_reason_name(
                           (robo_paint_invalidation_reason_t)other)) == 0) {
                duplicates++;
            }
        }
    }
    ROBO_CHECK_EQ_INT((int)duplicates, 0);
    /* `NONE` is the cleared record's value rather than a reason a producer may hand in. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_is_known(
                          (robo_paint_invalidation_reason_t)ROBO_PAINT_INVALIDATION_REASON_COUNT),
                      0);
    ROBO_CHECK(strcmp(robo_paint_invalidation_reason_name(
                          (robo_paint_invalidation_reason_t)99),
                      "unknown") == 0);
    /* Every reason the doors below produce is named here, which is what makes the vocabulary a
     * statement about the code rather than a list of hopes. */
    ROBO_CHECK(strcmp(robo_paint_invalidation_reason_name(
                          ROBO_PAINT_INVALIDATION_REASON_STYLE),
                      "style") == 0);
    ROBO_CHECK(strcmp(robo_paint_invalidation_reason_name(
                          ROBO_PAINT_INVALIDATION_REASON_VIEWPORT),
                      "viewport") == 0);
}

static void test_invalidation_class_and_scope_vocabulary(void)
{
    size_t index;

    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_INVALIDATION_CLASS_COUNT, 3);
    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_INVALIDATION_SCOPE_COUNT, 3);
    for (index = 0u; index < (size_t)ROBO_PAINT_INVALIDATION_CLASS_COUNT; index++) {
        ROBO_CHECK_EQ_INT(
            robo_paint_invalidation_class_is_known((robo_paint_invalidation_class_t)index), 1);
        ROBO_CHECK(strcmp(robo_paint_invalidation_class_name(
                              (robo_paint_invalidation_class_t)index),
                          "unknown") != 0);
    }
    for (index = 0u; index < (size_t)ROBO_PAINT_INVALIDATION_SCOPE_COUNT; index++) {
        ROBO_CHECK_EQ_INT(
            robo_paint_invalidation_scope_is_known((robo_paint_invalidation_scope_t)index), 1);
        ROBO_CHECK(strcmp(robo_paint_invalidation_scope_name(
                              (robo_paint_invalidation_scope_t)index),
                          "unknown") != 0);
    }
    ROBO_CHECK(strcmp(robo_paint_invalidation_class_name(
                          ROBO_PAINT_INVALIDATION_CLASS_LAYOUT),
                      "layout") == 0);
    ROBO_CHECK(strcmp(robo_paint_invalidation_scope_name(
                          ROBO_PAINT_INVALIDATION_SCOPE_PARTIAL),
                      "partial") == 0);
}

static void test_invalidation_classify_property(void)
{
    /*
     * What layout measures is a geometry change whatever it paints: a repaint of a box that moved
     * is only sound once a layout pass has run, so the class says so and the caller owes both.
     */
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("width"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_LAYOUT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("margin-left"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_LAYOUT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("font-size"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_LAYOUT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("display"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_LAYOUT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("overflow"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_LAYOUT);

    /* What this module draws from: a repaint of a region, geometry untouched. */
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("background-color"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_PAINT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("background"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_PAINT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("border-top-color"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_PAINT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("color"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_PAINT);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("opacity"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_PAINT);

    /* A spelling from a document, not from a table. */
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("BACKGROUND-COLOR"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_PAINT);

    /* Nothing this build draws: no pixel changes, so nothing is owed. */
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("cursor"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_NONE);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("not-a-property"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_NONE);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property(NULL),
        (int)ROBO_PAINT_INVALIDATION_CLASS_NONE);
    /* `border-style` is layout's, because a style of `none` collapses the used width. */
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("border-style"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_LAYOUT);
}

static void test_invalidation_reason_for_property(void)
{
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_reason_for_property("background-color"),
        (int)ROBO_PAINT_INVALIDATION_REASON_BACKGROUND);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_for_property("border-color"),
                      (int)ROBO_PAINT_INVALIDATION_REASON_BORDER);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_for_property("color"),
                      (int)ROBO_PAINT_INVALIDATION_REASON_TEXT);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_for_property("opacity"),
                      (int)ROBO_PAINT_INVALIDATION_REASON_OPACITY);
    /* A measured property repaints because a coordinate moved, so the reason names the geometry
     * and the caller reads its own cascade for the declaration. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_for_property("width"),
                      (int)ROBO_PAINT_INVALIDATION_REASON_GEOMETRY);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_for_property("visibility"),
                      (int)ROBO_PAINT_INVALIDATION_REASON_NONE);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_reason_for_property("unknown-thing"),
                      (int)ROBO_PAINT_INVALIDATION_REASON_NONE);
}

/**
 * The three properties layout calls PAINT and **nobody draws**.
 *
 * This is the phase's one veto, and it is a veto of a *repaint* rather than of a row in a table:
 * `visibility` is registered in RoboCSS and consumed by no layer in this runtime, no background
 * image is ever painted, and `text-decoration` is drawn by nobody -- so a change to any of them
 * produces an **identical picture**, and a system that asked for a repaint would be asking for
 * work that cannot change a pixel. The document-level half of the case is the proof: two sheets
 * that differ only in `visibility` produce **equal** record lists, so the comparison between them
 * is a comparison between two current pictures.
 */
static void test_invalidation_unconsumed_properties(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE INVAL_BOX_BLUE " .box { visibility: hidden }");

    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("visibility"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_NONE);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("text-decoration"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_NONE);
    ROBO_CHECK_EQ_INT(
        robo_paint_invalidation_classify_property("background-image"),
        (int)ROBO_PAINT_INVALIDATION_CLASS_NONE);

    /* The picture really is the same one, which is what makes `NONE` the sound answer rather than
     * a decision to look away. */
    ROBO_CHECK(robo_paint_list_equal(pair.before_result.list, pair.after_result.list));

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 0);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_NONE);

    inval_destroy(set);
    inval_pair_end(&pair);
}

/* ------------------------------------------------------------------ */
/* The set                                                             */
/* ------------------------------------------------------------------ */

static void test_invalidation_empty_set(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_stats_t stats;
    robo_layout_rect_t bounds = inval_rect(0, 0, 0, 0);

    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 0);
    ROBO_CHECK(robo_paint_invalidation_set_capacity(set) >=
               ROBO_PAINT_INVALIDATION_INITIAL_CAPACITY);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_is_empty(set), 1);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_NONE);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_bounds(set, &bounds), ROBO_NOT_FOUND);
    /* Nothing owed, and no regions produced: the record of an empty set is all zeros. */
    memset(&stats, 0xff, sizeof(stats));
    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK_EQ_INT((int)stats.added, 0);
    ROBO_CHECK_EQ_INT((int)stats.excluded, 0);
    ROBO_CHECK_EQ_INT((int)stats.compared, 0);

    /* The NULL-tolerant half of every read, which is what a caller with no set can rely on. */
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_is_empty(NULL), 1);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(NULL),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_NONE);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_bounds(NULL, &bounds), ROBO_INVALID_ARGUMENT);

    robo_paint_invalidation_set_clear(set);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 0);
    inval_destroy(set);
    inval_destroy(NULL);
}

static void test_invalidation_add_rect(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_stats_t stats;
    robo_paint_invalidation_region_t region;
    robo_layout_rect_t bounds;
    robo_layout_rect_t rect = inval_rect(20, 20, 60, 40);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, rect, (robo_layout_node_id_t)7,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_is_empty(set), 0);

    region = inval_at(set, 0u);
    inval_region_is(&region, rect, ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, 0, 0, "the region");
    ROBO_CHECK_EQ_INT((int)region.node, 7);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_region_is_valid(&region), 1);
    /* No boundary was handed in, so there is none in force and nothing was narrowed. */
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&region.clip), 0);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_bounds(set, &bounds), ROBO_OK);
    paint_rect_eq(bounds, rect, "the bounds of one region are the region");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_PARTIAL);

    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK_EQ_INT((int)stats.added, 1);
    ROBO_CHECK_EQ_INT((int)stats.conservative, 1);
    ROBO_CHECK_EQ_INT((int)stats.narrowed, 0);
    ROBO_CHECK_EQ_INT((int)stats.full, 0);

    /* A hand-built region with no reason is not a record this build may store. */
    robo_paint_invalidation_region_init(&region);
    region.rect = rect;
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_region_is_valid(&region), 0);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_region(set, &region, NULL),
                      ROBO_INVALID_ARGUMENT);

    inval_destroy(set);
}

static void test_invalidation_bounds_union(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_layout_rect_t bounds;

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(10, 10, 20, 20), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(100, 5, 10, 10), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_TEXT, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(0, 50, 5, 5), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_IMAGE, NULL, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 3);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_bounds(set, &bounds), ROBO_OK);
    /* The rectangle containing all three, which is always sufficient and may be too large. */
    paint_rect_is(0, 5, 110, 50, bounds, "the union bounds");

    /* Deterministic iteration: the order added, by value, reason and rectangle. */
    ROBO_CHECK_EQ_INT((int)inval_at(set, 0u).reason,
                      (int)ROBO_PAINT_INVALIDATION_REASON_BACKGROUND);
    ROBO_CHECK_EQ_INT((int)inval_at(set, 1u).reason, (int)ROBO_PAINT_INVALIDATION_REASON_TEXT);
    ROBO_CHECK_EQ_INT((int)inval_at(set, 2u).reason, (int)ROBO_PAINT_INVALIDATION_REASON_IMAGE);
    paint_rect_is(10, 10, 20, 20, inval_at(set, 0u).rect, "the first region");
    paint_rect_is(0, 50, 5, 5, inval_at(set, 2u).rect, "the third region");

    inval_destroy(set);
}

static void test_invalidation_narrowed(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_clip_t boundary = inval_clip_both(0, 50, 0, 30);
    robo_paint_invalidation_stats_t stats;
    robo_paint_invalidation_region_t region;

    /* A rectangle that hangs out of a boundary that names both axes: the region is the overlap,
     * which is the one narrowing this module is allowed to perform. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(20, 10, 60, 40), (robo_layout_node_id_t)3,
                          ROBO_PAINT_INVALIDATION_REASON_GEOMETRY, &boundary, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    region = inval_at(set, 0u);
    paint_rect_is(20, 10, 30, 20, region.rect, "the intersection with the boundary");
    ROBO_CHECK_EQ_INT(region.narrowed, 1);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&region.clip), 1);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_region_is_valid(&region), 1);

    /* A rectangle the boundary excludes entirely produces **no** region: no pixel of it was ever
     * drawn, so nothing there can be stale. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(200, 200, 10, 10), (robo_layout_node_id_t)3,
                          ROBO_PAINT_INVALIDATION_REASON_GEOMETRY, &boundary, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);

    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK_EQ_INT((int)stats.added, 1);
    ROBO_CHECK_EQ_INT((int)stats.narrowed, 1);
    ROBO_CHECK_EQ_INT((int)stats.conservative, 0);
    ROBO_CHECK_EQ_INT((int)stats.excluded, 1);

    inval_destroy(set);
}

static void test_invalidation_conservative_boundary(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_clip_t boundary = inval_clip_x_only(0, 50);
    robo_paint_invalidation_stats_t stats;
    robo_paint_invalidation_region_t region;

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(20, 10, 60, 40), (robo_layout_node_id_t)3,
                          ROBO_PAINT_INVALIDATION_REASON_GEOMETRY, &boundary, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    region = inval_at(set, 0u);
    /* The vertical axis has no extent to intersect with, so the rectangle is kept whole: a
     * region may be too large and may never be too small. */
    paint_rect_is(20, 10, 60, 40, region.rect, "a rectangle kept whole");
    ROBO_CHECK_EQ_INT(region.narrowed, 0);
    ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&region.clip), 1);
    /* The boundary travels with the region, so a consumer that *can* clip still can. */
    ROBO_CHECK_EQ_INT(
        robo_paint_clip_equal(&region.clip, &boundary), 1);

    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK_EQ_INT((int)stats.conservative, 1);
    ROBO_CHECK_EQ_INT((int)stats.narrowed, 0);

    inval_destroy(set);
}

static void test_invalidation_zero_area_and_invalid(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_stats_t stats;
    robo_paint_invalidation_region_t region;
    robo_paint_clip_t boundary = inval_clip_both(0, 50, 0, 30);

    robo_paint_invalidation_region_init(&region);

    /* A rectangle with no area is a conclusion -- a box with no size changed no pixel -- and it is
     * success with nothing stored. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(10, 10, 0, 40), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_is_empty(set), 1);
    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK_EQ_INT((int)stats.excluded, 1);
    ROBO_CHECK_EQ_INT((int)stats.added, 0);

    /* A negative size is not a rectangle, a missing reason is not a conclusion, and an id past
     * the end is not a region. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(10, 10, -5, 40), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(10, 10, 5, 40), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_NONE, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(10, 10, 5, 40), ROBO_LAYOUT_NODE_ID_NONE,
                          (robo_paint_invalidation_reason_t)99, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          NULL, inval_rect(10, 10, 5, 40), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_at(set, 0u, &region, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_at(set, 0u, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_rect(
                          set, inval_rect(10, 60, 5, 40), ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, &boundary, NULL),
                      ROBO_OK);
    /* The rectangle is below the boundary's vertical extent, so nothing is stored -- the same rule
     * as `narrowed`'s disjoint case, reached from the other direction. */
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 0);

    inval_destroy(set);
}

/* ------------------------------------------------------------------ */
/* Two states: what a change costs                                     */
/* ------------------------------------------------------------------ */

static void test_invalidation_no_op(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_stats_t stats;

    /* The same sheet twice: two documents, two trees, one picture. */
    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE, INVAL_BASE INVAL_BOX_BLUE);
    ROBO_CHECK(robo_paint_list_equal(pair.before_result.list, pair.after_result.list));

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 0);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_NONE);

    /* And the walk really did visit the document: "nothing owed" is a conclusion about the
     * records, not a comparison that never happened. */
    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK(stats.compared > 0u);
    ROBO_CHECK_EQ_INT((int)stats.matched, (int)stats.compared);
    ROBO_CHECK_EQ_INT((int)stats.differed, 0);

    inval_destroy(set);
    inval_pair_end(&pair);
}

static void test_invalidation_background_change(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_region_t region;
    robo_paint_boxes_t boxes;
    size_t index;

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; background-color: #cc3344 }");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_PARTIAL);

    region = inval_at(set, 0u);
    ROBO_CHECK_EQ_INT((int)region.reason, (int)ROBO_PAINT_INVALIDATION_REASON_BACKGROUND);
    ROBO_CHECK_EQ_INT((int)region.narrowed, 0);
    ROBO_CHECK_EQ_INT((int)region.full, 0);

    /* The region is the box layout concluded -- read from the record rather than written down. */
    ROBO_CHECK(region.node != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(region.node, (int)paint_box(&pair.before, "box"));
    /* The background acts on the **border box**, which is where the record says it is -- the
     * body's background is a record too, so the lookup names the node rather than taking the
     * first one the list happens to hold. */
    memset(&boxes, 0, sizeof(boxes));
    ROBO_CHECK(inval_box_rect(&pair.before_result, region.node, &boxes.border));
    paint_rect_eq(region.rect, boxes.border, "the region is the box's background record box");
    index = 0u;
    (void)index;

    inval_destroy(set);
    inval_pair_end(&pair);
}

static void test_invalidation_border_change(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    size_t index;

    inval_pair_begin(
        &pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE " .box { border: 4px solid #111111 }",
        INVAL_BASE INVAL_BOX_BLUE " .box { border: 4px solid #eeeeee }");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_paint_invalidation_set_count(set) >= 1u);
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        ROBO_CHECK_EQ_INT((int)inval_at(set, index).reason,
                          (int)ROBO_PAINT_INVALIDATION_REASON_BORDER);
    }

    inval_destroy(set);
    inval_pair_end(&pair);
}

static void test_invalidation_text_change(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    size_t index;
    int texts = 0;

    inval_pair_begin(&pair,
                     "<html><body><div class=\"box\"><p>Hello</p></div></body></html>",
                     INVAL_BASE INVAL_BOX_BLUE " .box p { color: #ff0000 }",
                     INVAL_BASE INVAL_BOX_BLUE " .box p { color: #00ff00 }");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_paint_invalidation_set_count(set) >= 1u);
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        if (inval_at(set, index).reason == ROBO_PAINT_INVALIDATION_REASON_TEXT) {
            texts++;
        }
    }
    /* The run's own record is the one that changed, and its reason is the text. */
    ROBO_CHECK_EQ_INT(texts, 1);

    inval_destroy(set);
    inval_pair_end(&pair);
}

static void test_invalidation_opacity_change(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_set_t *subtree = inval_set();
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_rect_t inner_rect;
    robo_layout_rect_t outer_rect;
    size_t index;
    int opacity_regions = 0;

    inval_pair_begin(&pair,
                     "<html><body><div class=\"box\"><p class=\"child\">Child</p></div></body></html>",
                     INVAL_BASE INVAL_BOX_BLUE " .box { opacity: 1 } "
                     ".box .child { width: 30px; height: 12px; background-color: #22aa55 }",
                     INVAL_BASE INVAL_BOX_BLUE " .box { opacity: 0.4 } "
                     ".box .child { width: 30px; height: 12px; background-color: #22aa55 }");

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        if (inval_at(set, index).reason == ROBO_PAINT_INVALIDATION_REASON_OPACITY) {
            opacity_regions++;
        }
    }
    ROBO_CHECK(opacity_regions >= 1);

    /*
     * And the door an element's strength needs, read on its own: a subtree mark covers the
     * **descendant's** rectangle, which is the whole difference between invalidating the box and
     * invalidating what the box composites to.
     */
    outer = paint_box(&pair.before, "box");
    inner = paint_box(&pair.before, "child");
    /* The descendant's rectangle **in the artifact**, not in layout's space: the region is a paint
     * rectangle, and the two spaces are not interchangeable (11.1). */
    memset(&inner_rect, 0, sizeof(inner_rect));
    ROBO_CHECK_EQ_INT(inval_box_rect(&pair.before_result, inner, &inner_rect), 1);
    ROBO_CHECK_EQ_INT(inval_box_rect(&pair.before_result, outer, &outer_rect), 1);
    ROBO_CHECK(inner_rect.x >= outer_rect.x && inner_rect.y >= outer_rect.y);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_subtree(
                          subtree, pair.before_result.list, outer,
                          ROBO_PAINT_INVALIDATION_REASON_OPACITY, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(subtree), 1);
    {
        robo_paint_invalidation_region_t region = inval_at(subtree, 0u);
        robo_paint_invalidation_stats_t stats;

        ROBO_CHECK(inval_rect_contains(region.rect, inner_rect.x, inner_rect.y));
        ROBO_CHECK(inval_rect_contains(region.rect, inner_rect.x + 1, inner_rect.y + 1));
        /* And the whole of it, not just its top-left corner. */
        ROBO_CHECK(inval_rect_contains(region.rect, inner_rect.x + inner_rect.width - 1,
                                       inner_rect.y + inner_rect.height - 1));
        ROBO_CHECK_EQ_INT((int)region.reason, (int)ROBO_PAINT_INVALIDATION_REASON_OPACITY);
        robo_paint_invalidation_set_stats(subtree, &stats);
        ROBO_CHECK_EQ_INT((int)stats.spans, 1);
    }

    inval_destroy(subtree);
    inval_destroy(set);
    inval_pair_end(&pair);
}

/**
 * The old-and-new rectangle, as one region.
 *
 * The door a caller uses when it knows **both** rectangles itself: a resource that finished
 * decoding somewhere else, a box a caller moved without a layout pass, a region a theme swap
 * restores. The two rectangles are unioned in 64 bits and saturated, and the boundary narrows
 * the **union** rather than either side -- a union narrowed by one of the two would be an answer
 * describing half of what changed.
 */
static void test_invalidation_add_change_union(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_stats_t stats;
    robo_paint_invalidation_region_t region;
    robo_paint_clip_t boundary = inval_clip_both(0, 200, 0, 200);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_change(set, inval_rect(20, 20, 100, 50),
                                                             inval_rect(40, 30, 100, 50),
                                                             (robo_layout_node_id_t)5,
                                                             ROBO_PAINT_INVALIDATION_REASON_GEOMETRY,
                                                             &boundary, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    region = inval_at(set, 0u);
    /* The union of the two, which is exactly the brief's `A ∪ B`. */
    paint_rect_is(20, 20, 120, 60, region.rect, "the union of the old and new rectangles");
    ROBO_CHECK_EQ_INT(region.narrowed, 1);
    ROBO_CHECK_EQ_INT((int)region.reason, (int)ROBO_PAINT_INVALIDATION_REASON_GEOMETRY);
    ROBO_CHECK_EQ_INT(robo_paint_clip_equal(&region.clip, &boundary), 1);
    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK_EQ_INT((int)stats.unions, 1);
    ROBO_CHECK_EQ_INT((int)stats.added, 1);

    /* The two rectangles are snapshots: the values handed in are copied, so the region cannot
     * follow anything a caller does to its own records afterwards. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_region_is_valid(&region), 1);

    /* A union outside its own boundary is dropped by the same rule as any other rectangle. */
    robo_paint_invalidation_set_clear(set);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_add_change(
                          set, inval_rect(400, 400, 10, 10), inval_rect(420, 420, 10, 10),
                          ROBO_LAYOUT_NODE_ID_NONE, ROBO_PAINT_INVALIDATION_REASON_IMAGE,
                          &boundary, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_is_empty(set), 1);

    inval_destroy(set);
}

static void test_invalidation_geometry_move(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_boxes_t was;
    robo_paint_boxes_t now;
    robo_paint_invalidation_region_t first;
    robo_paint_invalidation_region_t second;
    robo_layout_rect_t bounds;
    robo_layout_rect_t old_rect;
    robo_layout_rect_t new_rect;
    int found_old = 0;
    int found_new = 0;

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE INVAL_BOX_BLUE " .box { position: relative; left: 40px }");

    memset(&was, 0, sizeof(was));
    memset(&now, 0, sizeof(now));
    ROBO_CHECK_EQ_INT(robo_paint_boxes_of(pair.before.tree, paint_box(&pair.before, "box"),
                                          &was, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_paint_boxes_of(pair.after.tree, paint_box(&pair.after, "box"), &now, NULL), ROBO_OK);
    ROBO_CHECK(was.border.x != now.border.x);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 2);
    first = inval_at(set, 0u);
    second = inval_at(set, 1u);
    /* Both rectangles: the one the box left, and the one it is in. */
    if (first.node == paint_box(&pair.before, "box")) {
        found_old = 1;
    }
    if (second.node == paint_box(&pair.after, "box")) {
        found_new = 1;
    }
    ROBO_CHECK_EQ_INT(found_old, 1);
    ROBO_CHECK_EQ_INT(found_new, 1);
    ROBO_CHECK_EQ_INT((int)first.reason, (int)ROBO_PAINT_INVALIDATION_REASON_GEOMETRY);
    ROBO_CHECK_EQ_INT((int)second.reason, (int)ROBO_PAINT_INVALIDATION_REASON_GEOMETRY);
    ROBO_CHECK(first.rect.x < second.rect.x);

    /* And each region carries that box's own painted rectangle, not merely its number: the brief's
     * "old bounds ∪ new bounds" is read out of the two artifacts here rather than restated by
     * hand, so a walk that widened, dropped or misplaced either rectangle would fail this. */
    ROBO_CHECK(inval_box_rect(&pair.before_result, paint_box(&pair.before, "box"), &old_rect));
    ROBO_CHECK(inval_box_rect(&pair.after_result, paint_box(&pair.after, "box"), &new_rect));
    ROBO_CHECK(inval_set_has(set, old_rect, paint_box(&pair.before, "box")));
    ROBO_CHECK(inval_set_has(set, new_rect, paint_box(&pair.after, "box")));
    ROBO_CHECK(old_rect.x != new_rect.x);
    /* A displacement changes no coordinate the box's own records carry except the position, so the
     * rectangles are the same size and 40px apart -- the shape the old-and-new rule exists for. */
    ROBO_CHECK_EQ_INT((int)first.rect.width, (int)second.rect.width);
    ROBO_CHECK_EQ_INT((int)first.rect.height, (int)second.rect.height);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_bounds(set, &bounds), ROBO_OK);
    ROBO_CHECK(inval_rect_contains(bounds, first.rect.x, first.rect.y));
    ROBO_CHECK(inval_rect_contains(bounds, second.rect.x, second.rect.y));

    inval_destroy(set);
    inval_pair_end(&pair);
}

static void test_invalidation_geometry_resize(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 120px; height: 40px; margin: 20px; background-color: #3366cc }");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_paint_invalidation_set_count(set) >= 2u);
    /* The wide rectangle is covered: a background that grew leaves stale pixels in the part the
     * smaller one did not reach. */
    {
        robo_layout_rect_t bounds;

        ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_bounds(set, &bounds), ROBO_OK);
        ROBO_CHECK(bounds.width >= 120);
    }

    inval_destroy(set);
    inval_pair_end(&pair);
}

/**
 * A box that appears and a box that goes away.
 *
 * The transition is `display: none` on an element that is **last** in the document: the records
 * the change adds are at the tail of the list, so the positions both artifacts share still pair
 * up and the difference this case is about is not buried under the shift a change in the middle
 * would cause. The same pair answers both directions -- the second call hands the artifacts over
 * swapped -- because an insertion and a removal are one difference read from either end.
 *
 * The regions are `CONTENT` and not `STYLE`: what changed for a reader is that the sequence of
 * drawings changed, which is the reason the vocabulary keeps for exactly this.
 */
static void test_invalidation_insertion_removal(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_stats_t stats;
    size_t index;
    int content_regions = 0;

    inval_pair_begin(
        &pair, "<html><body><div class=\"box\"></div><div class=\"extra\"></div></body></html>",
        INVAL_BASE INVAL_BOX_BLUE
            ".extra { display: none; width: 24px; height: 18px; margin: 20px; "
            "background-color: #22aa55 }",
        INVAL_BASE INVAL_BOX_BLUE
            ".extra { width: 24px; height: 18px; margin: 20px; background-color: #22aa55 }");

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK(stats.compared > 0u);
    /* The positions only the after list has, and -- the part that is a claim rather than a count
     * -- a region for the one of them that draws. */
    ROBO_CHECK(stats.inserted > 0u);
    ROBO_CHECK_EQ_INT((int)stats.removed, 0);
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        if (inval_at(set, index).reason == ROBO_PAINT_INVALIDATION_REASON_CONTENT) {
            content_regions++;
        }
    }
    ROBO_CHECK(content_regions >= 1);

    /* The same difference from the other end: the element is now the one that went away. */
    robo_paint_invalidation_set_clear(set);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(
                          set, pair.after_result.list, pair.before_result.list, NULL),
                      ROBO_OK);
    robo_paint_invalidation_set_stats(set, &stats);
    ROBO_CHECK(stats.removed > 0u);
    ROBO_CHECK_EQ_INT((int)stats.inserted, 0);
    content_regions = 0;
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        if (inval_at(set, index).reason == ROBO_PAINT_INVALIDATION_REASON_CONTENT) {
            content_regions++;
        }
    }
    ROBO_CHECK(content_regions >= 1);

    inval_destroy(set);
    inval_pair_end(&pair);
}

/**
 * The boundary moving is its own reason.
 *
 * A clipping ancestor displaced by 40px moves the interval it puts in force, so the records inside
 * it differ in their **boundary** as well as their rectangle. The reason is reported as `clip`
 * rather than `geometry` on purpose: what changed for the reader is *what is drawn there*, and a
 * reason that said "it moved" would send them looking for a layout change in a document that has
 * one.
 */
static void test_invalidation_clip_change(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_stats_t stats;
    size_t index;
    int clip_regions = 0;

    inval_pair_begin(
        &pair, "<html><body><div class=\"clip\"><div class=\"wide\"></div></div></body></html>",
        INVAL_BASE
        ".clip { width: 40px; height: 30px; margin: 10px; overflow: hidden } "
        ".wide { width: 200px; height: 20px; background-color: #3366cc }",
        INVAL_BASE
        ".clip { width: 40px; height: 30px; margin: 10px; overflow: hidden; "
        "position: relative; left: 40px } "
        ".wide { width: 200px; height: 200px; background-color: #3366cc }");

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_paint_invalidation_set_count(set) >= 1u);
    robo_paint_invalidation_set_stats(set, &stats);
    for (index = 0u; index < robo_paint_invalidation_set_count(set); index++) {
        robo_paint_invalidation_region_t region = inval_at(set, index);

        if (region.reason == ROBO_PAINT_INVALIDATION_REASON_CLIP) {
            clip_regions++;
            /* The boundary travels with the region, and a clipped region says so. */
            ROBO_CHECK_EQ_INT(robo_paint_clip_is_active(&region.clip), 1);
        }
    }
    ROBO_CHECK(clip_regions >= 1);
    ROBO_CHECK((int)stats.narrowed >= 1);

    inval_destroy(set);
    inval_pair_end(&pair);
}

/* ------------------------------------------------------------------ */
/* Two states: pixels                                                  */
/* ------------------------------------------------------------------ */

/**
 * Every pixel that changed lies inside a region -- the claim a partial repaint may rely on.
 *
 * The transition is a colour change **and** a displacement at once, so the difference between the
 * frames is neither a single rectangle nor a rectangle pair, and the regions are the records' own
 * boxes rather than the ink: the case is deliberately harder than the shape of the answer.
 */
static void test_invalidation_stale_pixels(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_gfx_surface_t *frame_a = inval_surface(INVAL_PAGE);
    robo_gfx_surface_t *frame_b = inval_surface(INVAL_PAGE);
    size_t differing = 0u;
    size_t uncovered = 0u;

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 30px }");
    inval_render(pair.before_result.list, frame_a);
    inval_render(pair.after_result.list, frame_b);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    inval_compare_frames(frame_a, frame_b, set, &differing, &uncovered);

    /* The two frames really are different -- otherwise "nothing outside a region changed" would be
     * a fact about an empty picture. */
    ROBO_CHECK(differing > 0u);
    ROBO_CHECK_EQ_INT((int)uncovered, 0);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_PARTIAL);

    robo_gfx_surface_release(frame_b);
    robo_gfx_surface_release(frame_a);
    inval_destroy(set);
    inval_pair_end(&pair);
}

/**
 * The capstone: clearing the regions and painting the new frame reproduces a full repaint.
 *
 * `frame_a` becomes the partial repaint (its regions cleared to the page, then the after list
 * painted into it) and `frame_c` is a fresh render of the after list. Byte-for-byte equal, or the
 * set was missing a pixel. It is the strongest statement this build can make about invalidation
 * with a rasteriser that has no notion of damage, and it is why the module is allowed to claim
 * *sufficiency* rather than exactness.
 */
static void test_invalidation_partial_repaint_matches_full(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_gfx_surface_t *partial = inval_surface(INVAL_PAGE);
    robo_gfx_surface_t *full = inval_surface(INVAL_PAGE);
    size_t differing = 0u;
    size_t uncovered = 0u;

    inval_pair_begin(
        &pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
        INVAL_BASE
        ".box { width: 90px; height: 55px; margin: 25px; "
        "background-color: #228844; position: relative; top: 35px; left: 40px }");

    /* The first frame, painted in full -- that is what a real caller would already have on screen. */
    inval_render(pair.before_result.list, partial);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);

    /* The partial repaint: clear what the set says is stale, then paint the new frame. */
    inval_clear_regions(set, partial, INVAL_PAGE);
    inval_render(pair.after_result.list, partial);

    /* The reference: a full render of the same frame from the same base colour. */
    inval_render(pair.after_result.list, full);

    inval_compare_frames(partial, full, set, &differing, &uncovered);
    ROBO_CHECK_EQ_INT((int)differing, 0);

    robo_gfx_surface_release(full);
    robo_gfx_surface_release(partial);
    inval_destroy(set);
    inval_pair_end(&pair);
}

/* ------------------------------------------------------------------ */
/* Marks                                                               */
/* ------------------------------------------------------------------ */

static void test_invalidation_mark_operation_and_node(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_layout_node_id_t box;
    robo_paint_operation_t marker;
    robo_paint_invalidation_region_t from_operation;
    robo_paint_invalidation_region_t from_list;

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE, INVAL_BASE INVAL_BOX_BLUE);
    box = paint_box(&pair.before, "box");
    /* The box's **own** begin record rather than the list's first one of that kind: what the case
     * is about is this node's record, and a marker picked by position would go on passing if the
     * walk started emitting some other box first. */
    ROBO_CHECK(inval_box_start(&pair.before_result, box, &marker));

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_operation(
                          set, &marker, ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    from_operation = inval_at(set, 0u);
    ROBO_CHECK_EQ_INT((int)from_operation.node, (int)box);
    paint_rect_eq(from_operation.rect, marker.boxes.border, "the record's border box");

    /* The list door reads the same geometry out of the same artifact. */
    robo_paint_invalidation_set_clear(set);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_node(
                          set, pair.before_result.list, box,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    from_list = inval_at(set, 0u);
    ROBO_CHECK(robo_paint_invalidation_region_equal(&from_operation, &from_list));

    /* A box this artifact does not have is a caller's mistake rather than a region. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_node(
                          set, pair.before_result.list, (robo_layout_node_id_t)999999u,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_node(
                          set, pair.before_result.list, ROBO_LAYOUT_NODE_ID_NONE,
                          ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_operation(
                          set, NULL, ROBO_PAINT_INVALIDATION_REASON_BACKGROUND, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Both artifacts at once, which is the moment in the middle of a mutation. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_node_change(
                          set, pair.before_result.list, pair.after_result.list, box,
                          ROBO_PAINT_INVALIDATION_REASON_GEOMETRY, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_node_change(
                          set, pair.before_result.list, pair.after_result.list,
                          (robo_layout_node_id_t)999999u,
                          ROBO_PAINT_INVALIDATION_REASON_GEOMETRY, NULL),
                      ROBO_NOT_FOUND);

    inval_destroy(set);
    inval_pair_end(&pair);
}

static void test_invalidation_full_and_from_stats(void)
{
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_stats_t stats;
    robo_paint_invalidation_region_t region;
    robo_layout_rect_t extent = inval_rect(0, 0, 800, 600);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_mark_full(
                          set, extent, ROBO_PAINT_INVALIDATION_REASON_VIEWPORT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    region = inval_at(set, 0u);
    inval_region_is(&region, extent, ROBO_PAINT_INVALIDATION_REASON_VIEWPORT, 0, 1,
                    "a whole-extent region");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_FULL);

    /* 11.1's hook, read the way the phase said it would be: a count with no rectangle. */
    robo_paint_invalidation_set_clear(set);
    memset(&stats, 0, sizeof(stats));
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_stats(set, &stats, extent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_is_empty(set), 1);

    stats.style_only = 4u;
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_stats(set, &stats, extent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), 1);
    region = inval_at(set, 0u);
    inval_region_is(&region, extent, ROBO_PAINT_INVALIDATION_REASON_STYLE, 0, 1,
                    "the conservative answer");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_scope(set),
                      (int)ROBO_PAINT_INVALIDATION_SCOPE_FULL);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_stats(NULL, &stats, extent, NULL),
                      ROBO_INVALID_ARGUMENT);

    inval_destroy(set);
}

/* ------------------------------------------------------------------ */
/* Display lists                                                       */
/* ------------------------------------------------------------------ */

static void test_invalidation_display_lists(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *from_records = inval_set();
    robo_paint_invalidation_set_t *from_commands = inval_set();
    robo_paint_display_list_t *before_display = NULL;
    robo_paint_display_list_t *after_display = NULL;
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_invalidation_region_t region;
    size_t index;
    int backgrounds = 0;

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; background-color: #cc3344 }");

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&before_display, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&after_display, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, pair.before_result.list,
                                                            before_display, NULL, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, pair.after_result.list,
                                                            after_display, NULL, NULL),
                      ROBO_OK);
    robo_paint_display_list_builder_destroy(builder);

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_display_lists(
                          from_commands, before_display, after_display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(from_commands), 1);
    region = inval_at(from_commands, 0u);
    ROBO_CHECK_EQ_INT((int)region.reason, (int)ROBO_PAINT_INVALIDATION_REASON_BACKGROUND);
    for (index = 0u; index < robo_paint_display_list_count(before_display); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(before_display, index, &command),
                          ROBO_OK);
        /* The **changed box's** background command, found by node rather than by position: the
         * page's own backgrounds are `draw-background` commands too, and the first of them is
         * neither the box nor the one the region is about. */
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND &&
            robo_paint_display_command_node(&command) == region.node) {
            /* A command's rectangle is the resolved one, which is what a rasteriser reads. */
            paint_rect_eq(region.rect, command.rect, "the draw-background rect");
            backgrounds++;
            break;
        }
    }
    ROBO_CHECK_EQ_INT(backgrounds, 1);

    /* The two artifact kinds agree about a background change: the same rectangle, the same reason. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(
                          from_records, pair.before_result.list, pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(from_records), 1);
    {
        robo_paint_invalidation_region_t from_records_region = inval_at(from_records, 0u);

        ROBO_CHECK(robo_paint_invalidation_region_equal(&region, &from_records_region));
    }

    robo_paint_display_list_destroy(after_display);
    robo_paint_display_list_destroy(before_display);
    inval_destroy(from_commands);
    inval_destroy(from_records);
    inval_pair_end(&pair);
}

/* ------------------------------------------------------------------ */
/* Determinism, isolation and the dump                                 */
/* ------------------------------------------------------------------ */

/**
 * The set is a snapshot of a **decision**, not a view of an artifact.
 *
 * The set is built from one pair of trees, then the transition is rebuilt from scratch in two
 * **other** trees and compared: equal regions, in the same order, with the same reasons -- which is
 * a statement no address, no allocation order and no clock can satisfy by accident. Then the
 * original pair's record lists are destroyed and the set is read again, unchanged: a set that
 * borrowed an artifact would be reading freed memory by then.
 */
static void test_invalidation_determinism_and_isolation(void)
{
    inval_pair_t first;
    inval_pair_t second;
    robo_paint_invalidation_set_t *set = inval_set();
    robo_paint_invalidation_set_t *again = inval_set();
    robo_paint_invalidation_region_t saved[8];
    size_t saved_count;
    size_t index;
    char first_dump[4096];
    char second_dump[4096];
    size_t first_length = 0u;
    size_t second_length = 0u;

    inval_pair_begin(&first, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");
    inval_pair_begin(&second, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(
                          set, first.before_result.list, first.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(
                          again, second.before_result.list, second.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_paint_invalidation_set_count(set) > 0u);
    ROBO_CHECK(robo_paint_invalidation_set_equal(set, again));
    ROBO_CHECK(!robo_paint_invalidation_set_equal(set, NULL));
    ROBO_CHECK(robo_paint_invalidation_set_equal(NULL, NULL));

    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_write(set, first_dump, sizeof(first_dump),
                                                        &first_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_write(again, second_dump, sizeof(second_dump),
                                                        &second_length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)first_length, (int)second_length);
    ROBO_CHECK(strcmp(first_dump, second_dump) == 0);
    /* No address, no allocation and no clock reaches the dump. */
    ROBO_CHECK(strstr(first_dump, "0x") == NULL);

    saved_count = robo_paint_invalidation_set_count(set);
    ROBO_CHECK(saved_count <= sizeof(saved) / sizeof(saved[0]));
    for (index = 0u; index < saved_count; index++) {
        saved[index] = inval_at(set, index);
    }

    /* The artifacts go away. */
    paint_result_dispose(&first.before_result);
    paint_result_dispose(&first.after_result);
    paint_end(&first.after);
    paint_end(&first.before);

    /* And the decision is still there, field for field. */
    ROBO_CHECK_EQ_INT((int)robo_paint_invalidation_set_count(set), (int)saved_count);
    for (index = 0u; index < saved_count; index++) {
        robo_paint_invalidation_region_t region = inval_at(set, index);

        ROBO_CHECK(robo_paint_invalidation_region_equal(&region, &saved[index]));
        ROBO_CHECK_EQ_INT(robo_paint_invalidation_region_is_valid(&region), 1);
    }

    inval_destroy(again);
    inval_destroy(set);
    inval_pair_end(&second);
}

static void test_invalidation_dump(void)
{
    inval_pair_t pair;
    robo_paint_invalidation_set_t *set = inval_set();
    char buffer[4096];
    char small[16];
    size_t length = 0u;
    size_t needed = 0u;

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(set, pair.before_result.list,
                                                             pair.after_result.list, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_write(set, buffer, sizeof(buffer), &length,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK(length > 0u);
    ROBO_CHECK(strstr(buffer, "reason=geometry") != NULL);
    ROBO_CHECK(strstr(buffer, "rect=") != NULL);
    ROBO_CHECK(strstr(buffer, "narrowed=") != NULL);
    ROBO_CHECK(strstr(buffer, "full=") != NULL);
    ROBO_CHECK(strstr(buffer, "scope=partial") != NULL);
    ROBO_CHECK(strstr(buffer, "regions=2") != NULL);
    ROBO_CHECK(strstr(buffer, "excluded=0") != NULL);
    ROBO_CHECK(strstr(buffer, "0x") == NULL);

    /* The `_write` contract: a refusal hands back the length it would have needed, so a caller can
     * size its buffer from the refusal rather than from an estimate. */
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_write(set, small, sizeof(small), &needed, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)needed, (int)length);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_write(set, NULL, 0u, &needed, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An empty set dumps as a summary line and nothing else. */
    robo_paint_invalidation_set_clear(set);
    ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_write(set, buffer, sizeof(buffer), &length,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK(strstr(buffer, "scope=none") != NULL);
    ROBO_CHECK(strstr(buffer, "regions=0") != NULL);
    ROBO_CHECK(strstr(buffer, "reason=") == NULL);

    inval_destroy(set);
    inval_pair_end(&pair);
}

/**
 * The set owns its regions and gives them back.
 *
 * Run **twice** and compare the two ends, which is the rule 11.7's finding recorded: a baseline
 * measured before the first rig and after the second teardown says as much about what the harness
 * had already allocated as about this module, while comparing the two cycles measures the module's
 * own ownership.
 */
static void test_invalidation_lifetime(void)
{
    inval_pair_t pair;
    size_t first;
    size_t second;
    int cycle;

    inval_pair_begin(&pair, INVAL_DOC, INVAL_BASE INVAL_BOX_BLUE,
                     INVAL_BASE
                     ".box { width: 60px; height: 40px; margin: 20px; "
                     "background-color: #cc3344; position: relative; left: 25px }");

    first = paint_live_blocks();
    for (cycle = 0; cycle < 2; cycle++) {
        robo_paint_invalidation_set_t *set = inval_set();
        robo_paint_invalidation_region_t region;
        robo_paint_invalidation_stats_t stats;

        ROBO_CHECK_EQ_INT(robo_paint_invalidation_set_from_lists(
                              set, pair.before_result.list, pair.after_result.list, NULL),
                          ROBO_OK);
        /* Growth: more regions than the first block, so the array really is re-taken. */
        region = inval_at(set, 0u);
        ROBO_CHECK_EQ_INT(robo_paint_invalidation_region_is_valid(&region), 1);
        robo_paint_invalidation_set_stats(set, &stats);
        ROBO_CHECK(stats.added > 0u);

        /* Clearing keeps the allocation and resets the record. */
        robo_paint_invalidation_set_clear(set);
        robo_paint_invalidation_set_stats(set, &stats);
        ROBO_CHECK_EQ_INT((int)stats.added, 0);
        ROBO_CHECK(robo_paint_invalidation_set_capacity(set) >=
                   ROBO_PAINT_INVALIDATION_INITIAL_CAPACITY);

        inval_destroy(set);
    }
    second = paint_live_blocks();
    ROBO_CHECK_EQ_INT((int)second, (int)first);

    inval_pair_end(&pair);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_paint_invalidation(void)
{
    robo_test_register("paint.invalidation.reason_vocabulary",
                       test_invalidation_reason_vocabulary);
    robo_test_register("paint.invalidation.class_and_scope_vocabulary",
                       test_invalidation_class_and_scope_vocabulary);
    robo_test_register("paint.invalidation.classify_property",
                       test_invalidation_classify_property);
    robo_test_register("paint.invalidation.reason_for_property",
                       test_invalidation_reason_for_property);
    robo_test_register("paint.invalidation.unconsumed_properties",
                       test_invalidation_unconsumed_properties);
    robo_test_register("paint.invalidation.empty_set", test_invalidation_empty_set);
    robo_test_register("paint.invalidation.add_rect", test_invalidation_add_rect);
    robo_test_register("paint.invalidation.bounds_union", test_invalidation_bounds_union);
    robo_test_register("paint.invalidation.narrowed", test_invalidation_narrowed);
    robo_test_register("paint.invalidation.conservative",
                       test_invalidation_conservative_boundary);
    robo_test_register("paint.invalidation.zero_area_and_invalid",
                       test_invalidation_zero_area_and_invalid);
    robo_test_register("paint.invalidation.no_op", test_invalidation_no_op);
    robo_test_register("paint.invalidation.add_change_union",
                       test_invalidation_add_change_union);
    robo_test_register("paint.invalidation.background_change",
                       test_invalidation_background_change);
    robo_test_register("paint.invalidation.border_change", test_invalidation_border_change);
    robo_test_register("paint.invalidation.text_change", test_invalidation_text_change);
    robo_test_register("paint.invalidation.opacity_change", test_invalidation_opacity_change);
    robo_test_register("paint.invalidation.geometry_move", test_invalidation_geometry_move);
    robo_test_register("paint.invalidation.geometry_resize", test_invalidation_geometry_resize);
    robo_test_register("paint.invalidation.insertion", test_invalidation_insertion_removal);
    robo_test_register("paint.invalidation.clip_change", test_invalidation_clip_change);
    robo_test_register("paint.invalidation.stale_pixels", test_invalidation_stale_pixels);
    robo_test_register("paint.invalidation.partial_repaint_matches_full",
                       test_invalidation_partial_repaint_matches_full);
    robo_test_register("paint.invalidation.mark_operation_and_node",
                       test_invalidation_mark_operation_and_node);
    robo_test_register("paint.invalidation.full_and_from_stats",
                       test_invalidation_full_and_from_stats);
    robo_test_register("paint.invalidation.display_lists", test_invalidation_display_lists);
    robo_test_register("paint.invalidation.determinism_and_isolation",
                       test_invalidation_determinism_and_isolation);
    robo_test_register("paint.invalidation.dump", test_invalidation_dump);
    robo_test_register("paint.invalidation.lifetime", test_invalidation_lifetime);
}
