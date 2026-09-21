/*
 * RoboPaint hit test (Phase 11.10) — see RoboPaintHitTest.h.
 *
 * This file is the **inverse walk**. The paint traversal turns a laid-out tree into a sequence
 * of drawings in paint order; this one takes a point on the drawing plane and walks the same
 * tree the other way round to find the node that would be drawn there.
 *
 * Everything it concludes comes from a door some other layer already owns, and that is the
 * property worth stating first because it is what makes the two walks agree rather than merely
 * resemble each other:
 *
 *   the rectangles      `robo_paint_boxes_of`, the same door the painter's markers and
 *                       decorations come from, plus `robo_paint_boxes_translate` for the page
 *                       accumulation -- so "where is this box" has one answer in this module;
 *   the boundary        `robo_layout_object_overflow` for the modes and
 *                       `robo_paint_clip_intersect` for the accumulation, exactly as
 *                       `paint_child_clip` reads them;
 *   the strength        `robo_paint_element_opacity` and `robo_paint_opacity_multiply`, which
 *                       are shared with the painter through `RoboPaintInternal.h` rather than
 *                       copied, so a change to what an element's `opacity` means cannot reach
 *                       one walk and not the other;
 *   the region          `robo_layout_overflow_edges_region`, the same door a parent folds a
 *                       child's reach in through, so "how far can this subtree reach" is
 *                       RoboLayout's conclusion and not a second one computed here.
 *
 * The one thing this file decides for itself is the **order**, and the order is the reverse of
 * the painter's:
 *
 *   a box's children are examined before the box itself, because the painter draws the box's
 *   own decorations first and its subtree over them;
 *   the children are examined last-to-first, because the painter draws them first-to-last;
 *   and the box's contents are examined under the boundary its own `overflow` puts in force
 *   while the box itself is examined under the boundary it was entered with, because that is
 *   where 11.8's clip scope stands in the sequence.
 *
 * Get any of those backwards and the answer is the node *underneath* the one the user is
 * pointing at, which is the classic hit-test bug -- which is why the suite states each of them
 * as its own case rather than relying on one overlapping fixture to catch all three.
 *
 * **No arithmetic of its own beyond the subtraction of an origin**, and no second model of
 * anything: this file is a walk, a record and a trace.
 */

#include "RoboPaint/RoboPaintHitTest.h"

#include "RoboHTML/RoboHTMLNode.h"

#include "RoboLayout/RoboLayoutOverflow.h"

#include "RoboPaint/RoboPaintOperation.h"

#include "RoboPaintInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The outcome vocabulary                                              */
/* ------------------------------------------------------------------ */

static const char *const hit_outcome_names[ROBO_PAINT_HIT_TEST_OUTCOME_COUNT] = {
    "none",
    "node",
    "miss",
    "stale"
};

int robo_paint_hit_test_outcome_is_known(robo_paint_hit_test_outcome_t outcome)
{
    return (int)outcome >= 0 && (size_t)outcome < (size_t)ROBO_PAINT_HIT_TEST_OUTCOME_COUNT;
}

const char *robo_paint_hit_test_outcome_name(robo_paint_hit_test_outcome_t outcome)
{
    if (!robo_paint_hit_test_outcome_is_known(outcome)) {
        return "unknown";
    }
    return hit_outcome_names[(size_t)outcome];
}

/* ------------------------------------------------------------------ */
/* The answer                                                          */
/* ------------------------------------------------------------------ */

void robo_paint_hit_test_result_init(robo_paint_hit_test_result_t *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->outcome = ROBO_PAINT_HIT_NONE;
    result->node = ROBO_LAYOUT_NODE_ID_NONE;
    robo_html_node_handle_clear(&result->dom_node);
    result->kind = ROBO_LAYOUT_OBJECT_NONE;
    /* The two states a document starts in rather than zeroed cells: a cleared result must be
     * *readable* -- "no boundary is in force" and "full strength" are the answers that draw
     * everything -- and a zeroed clip record would claim every coordinate is outside a
     * boundary of `[0, 0)`, which is the opposite of what a cleared record means. */
    robo_paint_boxes_init(&result->boxes);
    robo_paint_clip_init(&result->clip);
    robo_paint_opacity_init(&result->opacity);
}

int robo_paint_hit_test_result_is_valid(const robo_paint_hit_test_result_t *result)
{
    if (!result) {
        return 0;
    }
    if (!robo_paint_hit_test_outcome_is_known(result->outcome)) {
        return 0;
    }
    if ((result->flags & ~(uint32_t)ROBO_PAINT_HIT_FLAG_ALL) != 0u) {
        return 0;
    }
    if (!robo_paint_boxes_is_valid(&result->boxes) ||
        !robo_paint_clip_is_valid(&result->clip) ||
        !robo_paint_opacity_is_valid(&result->opacity)) {
        return 0;
    }
    if (result->outcome == ROBO_PAINT_HIT_ON_NODE) {
        /*
         * A hit has to be able to answer "which node": an id every other door in this module
         * accepts, a kind, and a DOM handle. All three, because a caller dispatching on the id
         * and a caller dispatching on the handle must not be able to disagree about whether
         * anything was hit, and a kind of `NONE` would say the answer is an object of no sort.
         */
        if (result->node == ROBO_LAYOUT_NODE_ID_NONE) {
            return 0;
        }
        if (result->kind != ROBO_LAYOUT_OBJECT_BOX &&
            result->kind != ROBO_LAYOUT_OBJECT_TEXT) {
            return 0;
        }
        if (!robo_html_node_handle_is_wellformed(&result->dom_node)) {
            return 0;
        }
        return 1;
    }
    /*
     * Everything else is the *absence* of an answer, and the record says so rather than
     * carrying a leftover: no node, no kind, no handle, and no flag describing a hit that did
     * not happen. `flags == 0` is the strict half of that, and it is deliberate -- a `MISS`
     * whose `CONTENT` bit was set would be a record a caller could read as "you are inside the
     * content box of nothing".
     */
    if (result->node != ROBO_LAYOUT_NODE_ID_NONE ||
        result->kind != ROBO_LAYOUT_OBJECT_NONE ||
        robo_html_node_handle_is_wellformed(&result->dom_node) ||
        result->flags != 0u) {
        return 0;
    }
    return 1;
}

int robo_paint_hit_test_result_is_hit(const robo_paint_hit_test_result_t *result)
{
    return result && result->outcome == ROBO_PAINT_HIT_ON_NODE;
}

int robo_paint_hit_test_result_has_flag(const robo_paint_hit_test_result_t *result, uint32_t flag)
{
    if (!result) {
        return 0;
    }
    return (result->flags & flag) != 0u;
}

robo_paint_hit_test_outcome_t robo_paint_hit_test_result_outcome(
    const robo_paint_hit_test_result_t *result)
{
    return result ? result->outcome : ROBO_PAINT_HIT_NONE;
}

robo_layout_node_id_t robo_paint_hit_test_result_node(const robo_paint_hit_test_result_t *result)
{
    return result ? result->node : ROBO_LAYOUT_NODE_ID_NONE;
}

robo_layout_object_kind_t robo_paint_hit_test_result_kind(
    const robo_paint_hit_test_result_t *result)
{
    return result ? result->kind : ROBO_LAYOUT_OBJECT_NONE;
}

size_t robo_paint_hit_test_result_depth(const robo_paint_hit_test_result_t *result)
{
    return result ? result->depth : 0u;
}

void robo_paint_hit_test_result_point(const robo_paint_hit_test_result_t *result,
                                     robo_layout_point_t *out)
{
    if (out) {
        *out = result ? result->point : robo_gfx_point(0, 0);
    }
}

void robo_paint_hit_test_result_local(const robo_paint_hit_test_result_t *result,
                                     robo_layout_point_t *out)
{
    if (out) {
        *out = result ? result->local : robo_gfx_point(0, 0);
    }
}

void robo_paint_hit_test_result_boxes(const robo_paint_hit_test_result_t *result,
                                     robo_paint_boxes_t *out)
{
    if (!out) {
        return;
    }
    if (!result) {
        robo_paint_boxes_init(out);
        return;
    }
    *out = result->boxes;
}

void robo_paint_hit_test_result_clip(const robo_paint_hit_test_result_t *result,
                                    robo_paint_clip_t *out)
{
    if (!out) {
        return;
    }
    if (!result) {
        robo_paint_clip_init(out);
        return;
    }
    *out = result->clip;
}

void robo_paint_hit_test_result_opacity(const robo_paint_hit_test_result_t *result,
                                       robo_paint_opacity_t *out)
{
    if (!out) {
        return;
    }
    if (!result) {
        robo_paint_opacity_init(out);
        return;
    }
    *out = result->opacity;
}

robo_html_node_handle_t robo_paint_hit_test_result_dom_node(
    const robo_paint_hit_test_result_t *result)
{
    robo_html_node_handle_t handle;

    robo_html_node_handle_clear(&handle);
    if (result) {
        handle = result->dom_node;
    }
    return handle;
}

int robo_paint_hit_test_result_equal(const robo_paint_hit_test_result_t *a,
                                    const robo_paint_hit_test_result_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->outcome != b->outcome || a->node != b->node || a->kind != b->kind ||
        a->depth != b->depth || a->flags != b->flags) {
        return 0;
    }
    if (a->point.x != b->point.x || a->point.y != b->point.y ||
        a->local.x != b->local.x || a->local.y != b->local.y ||
        a->origin.x != b->origin.x || a->origin.y != b->origin.y) {
        return 0;
    }
    if (!robo_paint_boxes_equal(&a->boxes, &b->boxes) ||
        !robo_paint_clip_equal(&a->clip, &b->clip) ||
        !robo_paint_opacity_equal(&a->opacity, &b->opacity)) {
        return 0;
    }
    /* The DOM handle is compared by its three tokens rather than by `memcmp`, because it is a
     * value with a well-formedness rule and two cleared handles are the same handle. */
    if (robo_html_node_handle_is_wellformed(&a->dom_node) !=
        robo_html_node_handle_is_wellformed(&b->dom_node)) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* What the query saw                                                  */
/* ------------------------------------------------------------------ */

void robo_paint_hit_test_stats_init(robo_paint_hit_test_stats_t *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

/* ------------------------------------------------------------------ */
/* The geometry of a subtree                                           */
/* ------------------------------------------------------------------ */

/**
 * The smallest rectangle containing both, tolerating an empty operand.
 *
 * An empty rectangle is not a point at the origin, it is *nothing* -- `RoboGraphicsGeometry.h`
 * states that a non-positive extent is empty rather than invalid -- so it must not drag the
 * union towards `(0, 0)`. A zero-height box is the ordinary case this protects: its border box
 * contributes its width and no height, and a naive min/max over the corners would make the bound
 * span from the document's origin to the box instead.
 *
 * Both extents are taken in 64 bits and saturated, the rule `RoboPaintInternal.h` states for
 * every accumulation in this module: an operand is bounded and a difference of two is not.
 */
static robo_layout_rect_t hit_rect_union(robo_layout_rect_t a, robo_layout_rect_t b)
{
    robo_layout_rect_t out;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (robo_gfx_rect_is_empty(a)) {
        return b;
    }
    if (robo_gfx_rect_is_empty(b)) {
        return a;
    }
    left = (int64_t)a.x < (int64_t)b.x ? (int64_t)a.x : (int64_t)b.x;
    top = (int64_t)a.y < (int64_t)b.y ? (int64_t)a.y : (int64_t)b.y;
    right = (int64_t)a.x + a.width > (int64_t)b.x + b.width ? (int64_t)a.x + a.width
                                                           : (int64_t)b.x + b.width;
    bottom = (int64_t)a.y + a.height > (int64_t)b.y + b.height ? (int64_t)a.y + a.height
                                                               : (int64_t)b.y + b.height;
    out.x = robo_paint_clamp_i64(left);
    out.y = robo_paint_clamp_i64(top);
    out.width = robo_paint_extent(right - left);
    out.height = robo_paint_extent(bottom - top);
    return out;
}

/*
 * The bound, and it is the difference between a query whose cost follows the point and one
 * whose cost follows the document.
 *
 * **What the bound is**: the box's border box, grown to include the region its contents reached
 * (10.13). That is exactly the rectangle every node beneath the box lives inside, which is why
 * it can be tested before anything is descended into.
 *
 * **Why the border box is where it starts, rather than the padding box the region is measured
 * from**: the region a parent folds in is measured from a child's padding box, but a box's own
 * *border* is outside that and is part of the box, so a bound of the region alone would exclude
 * the box's own border band -- and a hit test that could not find a border is a hit test that
 * fails on the one rectangle the painter definitely draws.
 *
 * **Why a refusal is not a gap**: `robo_layout_object_overflow` declines a text run and a
 * transparent inline box with contents, and for both the placement rectangle *is* the union of
 * what they enclose -- a run is a leaf, and a transparent inline box's rectangle is the union of
 * its contents by construction (`RoboLayoutObject.h`). So for those the border box is already
 * the whole bound, and declining is the correct answer rather than a hole in it. The two cases
 * are the same two `insets_measured` names on `robo_paint_boxes_t`, which is what makes the
 * bound and the painter's record agree about which placements have layers.
 */
static robo_result_t hit_subtree_bound(const robo_layout_tree_t *tree,
                                       robo_layout_node_id_t id,
                                       const robo_paint_boxes_t *boxes,
                                       robo_layout_rect_t *out,
                                       robo_paint_diag_t *diag)
{
    robo_layout_overflow_edges_t overflow;
    robo_layout_rect_t region;

    *out = boxes->border;
    if (robo_layout_object_overflow(tree, id, &overflow, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    if (!robo_layout_overflow_edges_has_overflow(&overflow)) {
        /*
         * Nothing reached past the padding box, and the region of a box with no extents *is* the
         * padding box -- which the border box already contains. So the early answer is the right
         * one and not merely the cheap one, and this is the case every box of an ordinary
         * document takes.
         */
        return ROBO_OK;
    }
    if (robo_layout_overflow_edges_region(&overflow, boxes->padding, &region, NULL) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_HIT_TEST, ROBO_ERROR,
                               "a box's overflow region could not be derived from its record");
    }
    *out = hit_rect_union(*out, region);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The two states a box passes down                                    */
/* ------------------------------------------------------------------ */

/** The strength @p box's own declaration puts in force for **itself and everything under it**.
 *
 * The painter's arithmetic, read from the same two doors: a box's own `opacity` multiplied into
 * the strength already in force. A box with **no computed style** declares nothing and is opaque,
 * which `robo_paint_element_opacity` answers rather than this call having to. */
static robo_paint_opacity_t hit_child_opacity(const robo_layout_tree_t *tree,
                                              robo_layout_node_id_t id,
                                              const robo_paint_opacity_t *opacity)
{
    robo_paint_opacity_t declared =
        robo_paint_element_opacity(robo_layout_object_style(tree, id));

    return robo_paint_opacity_multiply(opacity, &declared);
}

/** The boundary @p box's `overflow` puts in force for its contents, or @p clip unchanged.
 *
 * `paint_child_clip`'s rule stated once more rather than shared, because the two differ in one
 * thing that matters: the painter counts the box as `clipped` while it is there, and a hit test
 * has no document-level counters of that sort. Everything else -- the overflow door, the
 * "does either axis hide" question and the padding box as the clip edge -- is the same, and the
 * clip edge being the **padding box** is why the box's own border band stays hittable under a
 * boundary its contents are subject to. */
static robo_result_t hit_child_clip(const robo_layout_tree_t *tree,
                                    robo_layout_node_id_t id,
                                    const robo_paint_boxes_t *boxes,
                                    const robo_paint_clip_t *clip,
                                    robo_paint_clip_t *out,
                                    robo_paint_diag_t *diag)
{
    robo_layout_overflow_edges_t overflow;

    *out = *clip;
    if (robo_layout_object_overflow(tree, id, &overflow, NULL) != ROBO_OK) {
        return ROBO_OK;
    }
    if (!robo_layout_overflow_edges_clips_any(&overflow)) {
        return ROBO_OK;
    }
    return robo_paint_clip_intersect(clip, &boxes->padding, overflow.horizontal,
                                    overflow.vertical, out, diag);
}

/* ------------------------------------------------------------------ */
/* The trace                                                           */
/* ------------------------------------------------------------------ */

/*
 * Reasons a line names, in the order the walk reaches them, and each one is a decision the walk
 * makes rather than a status it later reports:
 *
 *   `unplaced`  nothing placed the node, so there is no rectangle and no coordinate to test in.
 *               Its subtree is not walked, because there is no origin for anything inside it
 *               (the same reason the painter emits nothing below such a box).
 *   `region`    the node is placed, and the point is outside the rectangle every node beneath it
 *               lives inside. The subtree is skipped.
 *   `clip`      the point is outside the boundary the node is drawn under, so neither it nor
 *               anything beneath it can be seen there.
 *   `enter`     the node was examined and the walk went on: a box descends into its children, a
 *               text run is simply tested. `kind=` on the same line says which.
 *   `hit`       the node is the answer. Written as its own line after the node's examination, so
 *               that the trace stays in traversal order and the answer is still readable.
 */
#define ROBO_PAINT_HIT_REASON_UNPLACED "unplaced"
#define ROBO_PAINT_HIT_REASON_REGION "region"
#define ROBO_PAINT_HIT_REASON_CLIP "clip"
#define ROBO_PAINT_HIT_REASON_ENTER "enter"
#define ROBO_PAINT_HIT_REASON_HIT "hit"

/** The kind column's width: `text` is the longer of the two. */
#define ROBO_PAINT_HIT_KIND_COLUMN 4u
/** The reason column's width: `unplaced` is the longest. */
#define ROBO_PAINT_HIT_REASON_COLUMN 8u

/** Append @p text padded with spaces to @p width. */
static void hit_write_column(robo_paint_writer_t *trace, const char *text, size_t width)
{
    size_t written = 0u;

    while (text[written] != '\0') {
        robo_paint_writer_char(trace, text[written]);
        written++;
    }
    while (written < width) {
        robo_paint_writer_char(trace, ' ');
        written++;
    }
}

/** The `box`/`text` name of @p kind; never NULL. */
static const char *hit_kind_name(robo_layout_object_kind_t kind)
{
    switch (kind) {
    case ROBO_LAYOUT_OBJECT_BOX:
        return "box";
    case ROBO_LAYOUT_OBJECT_TEXT:
        return "text";
    default:
        break;
    }
    return "none";
}

/** One examined object's line: `node=.. depth=.. kind.. outcome=.. border=.. bound=..`. */
static void hit_trace_node(robo_paint_writer_t *trace,
                          robo_layout_node_id_t id,
                          size_t depth,
                          robo_layout_object_kind_t kind,
                          const char *reason,
                          robo_layout_rect_t border,
                          robo_layout_rect_t bound)
{
    robo_paint_writer_string(trace, "node=");
    robo_paint_writer_u64(trace, (uint64_t)id);
    robo_paint_writer_string(trace, " depth=");
    robo_paint_writer_u64(trace, (uint64_t)depth);
    robo_paint_writer_char(trace, ' ');
    hit_write_column(trace, hit_kind_name(kind), ROBO_PAINT_HIT_KIND_COLUMN);
    robo_paint_writer_string(trace, " outcome=");
    hit_write_column(trace, reason, ROBO_PAINT_HIT_REASON_COLUMN);
    robo_paint_writer_string(trace, " border=");
    robo_paint_writer_rect(trace, border);
    robo_paint_writer_string(trace, " bound=");
    robo_paint_writer_rect(trace, bound);
    robo_paint_writer_char(trace, '\n');
}

/** The answer's own line, written after the node that produced it was examined. */
static void hit_trace_hit(robo_paint_writer_t *trace,
                         robo_layout_node_id_t id,
                         size_t depth,
                         robo_layout_point_t point)
{
    robo_paint_writer_string(trace, ROBO_PAINT_HIT_REASON_HIT);
    robo_paint_writer_string(trace, " node=");
    robo_paint_writer_u64(trace, (uint64_t)id);
    robo_paint_writer_string(trace, " depth=");
    robo_paint_writer_u64(trace, (uint64_t)depth);
    robo_paint_writer_string(trace, " point=");
    robo_paint_writer_i64(trace, (int64_t)point.x);
    robo_paint_writer_char(trace, ',');
    robo_paint_writer_i64(trace, (int64_t)point.y);
    robo_paint_writer_char(trace, '\n');
}

/* ------------------------------------------------------------------ */
/* Recording an answer                                                 */
/* ------------------------------------------------------------------ */

/**
 * Record @p id as the answer about @p point.
 *
 * The four facts that have to be copied in -- and each of them comes from the call that already
 * had to work it out rather than being re-derived here:
 *
 *   **the identity**: the layout object's id, and the DOM node it was built from. Both travel
 *   because the layers above consume different ones, and asking for the handle at the point of
 *   use would mean a walk into the tree at every use (RoboPaintHitTest.h);
 *
 *   **the two spaces**: the point as the query gave it, and the same point in the node's own
 *   containing-block space. The subtraction is saturating, so a node at the coordinate range's
 *   edge produces a *readable* local point rather than a wrapped one;
 *
 *   **the geometry**: the same three rectangles the painter read, in paint coordinates;
 *
 *   **the two states in force**: the boundary and the strength. @p opacity here is the strength
 *   **on this node's own drawing** -- the enclosing strength for a text run, whose owning element
 *   already opened its scope, and the box's own declaration multiplied in for a box, whose
 *   decorations are painted at its own strength. That is the painter's rule read at the same two
 *   places it applies it;
 *
 * plus the three flags, two of which are questions that are *derivable* from what is above and
 * one of which is not: `TEXT` is the kind, `CONTENT` is `boxes.content` containing the point, and
 * `TRANSPARENT` is the strength in force having come to zero -- which no rectangle can answer and
 * which is why it is a flag rather than something a caller computes. The derivable pair is
 * asserted against its derivation in the suite rather than trusted here.
 */
static void hit_record(robo_paint_hit_test_result_t *result,
                       const robo_layout_tree_t *tree,
                       robo_layout_node_id_t id,
                       size_t depth,
                       robo_layout_object_kind_t kind,
                       robo_layout_point_t point,
                       robo_layout_point_t origin,
                       const robo_paint_boxes_t *boxes,
                       const robo_paint_clip_t *clip,
                       const robo_paint_opacity_t *opacity)
{
    robo_html_node_handle_t handle;

    robo_html_node_handle_clear(&handle);
    (void)robo_layout_object_dom_node(tree, id, &handle);

    result->outcome = ROBO_PAINT_HIT_ON_NODE;
    result->node = id;
    result->dom_node = handle;
    result->kind = kind;
    result->depth = depth;
    result->point = point;
    result->local = robo_gfx_point(robo_paint_sub_saturated(point.x, origin.x),
                                   robo_paint_sub_saturated(point.y, origin.y));
    result->origin = origin;
    result->boxes = *boxes;
    result->clip = *clip;
    result->opacity = *opacity;
    result->flags = 0u;
    if (kind == ROBO_LAYOUT_OBJECT_TEXT) {
        result->flags |= ROBO_PAINT_HIT_FLAG_TEXT;
    }
    if (robo_gfx_rect_contains_point(boxes->content, point)) {
        result->flags |= ROBO_PAINT_HIT_FLAG_CONTENT;
    }
    if (robo_paint_opacity_is_transparent(opacity)) {
        result->flags |= ROBO_PAINT_HIT_FLAG_TRANSPARENT;
    }
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

/*
 * The walk, and its shape is the chapter this file is about. Read it as four questions asked in
 * order, and the order is the answer to "which node does the user see":
 *
 *   1. **is the point anywhere near this node?** -- the subtree bound. Cheapest and most
 *      decisive: a point outside the rectangle a node's whole subtree lives in cannot be on
 *      anything in it, so the subtree costs one comparison. This is also what keeps a query
 *      proportional to the boxes the point is near rather than to the document, and what makes
 *      the region 10.13 measured worth having a consumer at last.
 *
 *   2. **is the point inside the boundary the node is drawn under?** -- the clip in force. A
 *      `hidden` axis is a boundary the drawing does not cross, so a point beyond one is not on
 *      this node *or* anything beneath it.
 *
 *   3. **what is on top of this node?** -- its children, last first, under the boundary and the
 *      strength this box's own declarations put in force for its contents. Descendants are drawn
 *      over their ancestor's own decorations, so they are asked first; and the *last* child is
 *      drawn over the earlier ones, so it is asked first as well. The point in `child_origin` is
 *      the one coordinate the painter computes here too: the child's geometry is relative to
 *      this box's content origin.
 *
 *   4. **is the node itself there?** -- only after its subtree has declined to answer, and under
 *      the boundary the node was *entered* with rather than the one its contents are under. That
 *      asymmetry is what keeps a clipped ancestor's own border hittable.
 *
 * The recursion is a value walk, not a stack: the origin, the boundary and the strength are the
 * call's parameters, because "what is in force here" is what this call concluded on the way down
 * and a shared mutable stack would be a second mechanism for one fact -- the same argument
 * `RoboPaintClip.h` makes for the painter.
 *
 * @param placed_ancestor  non-zero when @p origin is a real containing-block content origin --
 *                         that is, when every ancestor was a placement. A node reached under one
 *                         that was not has no coordinates to be tested in, and the painter
 *                         treats it the same way.
 * @param trace            optional; when given, every examined object appends one line. The
 *                         public query passes NULL, so this costs nothing outside the dump.
 */
static robo_result_t hit_walk(const robo_layout_tree_t *tree,
                              robo_layout_node_id_t id,
                              size_t depth,
                              int placed_ancestor,
                              robo_layout_point_t origin,
                              const robo_paint_clip_t *clip,
                              const robo_paint_opacity_t *opacity,
                              robo_layout_point_t point,
                              robo_paint_hit_test_result_t *result,
                              robo_paint_hit_test_stats_t *stats,
                              robo_paint_writer_t *trace,
                              robo_paint_diag_t *diag)
{
    robo_layout_object_kind_t kind = robo_layout_object_kind(tree, id);
    robo_layout_node_id_t child;
    robo_paint_boxes_t local;
    robo_paint_boxes_t boxes;
    robo_layout_rect_t bound;
    robo_layout_point_t child_origin;
    robo_paint_clip_t child_clip;
    robo_paint_opacity_t child_opacity;

    if (!robo_layout_object_kind_is_known(kind) || kind == ROBO_LAYOUT_OBJECT_NONE) {
        /* A child link that names nothing: reported rather than skipped, because a query that
         * answered "no node" for a document it could not walk is a query nobody can act on. */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_HIT_TEST, ROBO_ERROR,
                               "a child link names no object in this tree");
    }

    stats->objects++;
    if (depth > stats->depth_max) {
        stats->depth_max = (uint32_t)depth;
    }

    /* The placement, through the door the painter's markers and runs come from -- so a node this
     * walk tests is a node the painter draws, and a node it declines is one the painter declines.
     * A **text run** and a **transparent inline box with contents** are placements whose inset
     * records decline and whose three rectangles are therefore one rectangle: `insets_measured`
     * on the record says which, and nothing here has to know. */
    if (!placed_ancestor || robo_paint_boxes_of(tree, id, &local, NULL) != ROBO_OK) {
        /*
         * Nothing placed this object. Counted and **not descended into**: every node below it
         * would be reached with no origin to be tested in, so the walk would examine rectangles
         * that have no position on the page -- which is the same reason the painter emits nothing
         * below such a box. `unplaced` is therefore one object rather than a subtree, and
         * `robo_paint_hit_test_stats_t` says so where a reader will look for the difference
         * between it and the tree's object count.
         */
        stats->unplaced++;
        if (trace) {
            /* No placement means no rectangle, and the line says so with two empty ones rather
             * than with a rectangle derived from a result that does not exist. */
            robo_layout_rect_t nothing = robo_gfx_rect(0, 0, 0, 0);

            hit_trace_node(trace, id, depth, kind, ROBO_PAINT_HIT_REASON_UNPLACED, nothing,
                           nothing);
        }
        return ROBO_OK;
    }

    if (kind == ROBO_LAYOUT_OBJECT_TEXT) {
        stats->texts++;
    } else {
        stats->boxes++;
    }

    /* Into one page space: the accumulated origin added to what layout wrote. */
    (void)robo_paint_boxes_translate(&local, origin.x, origin.y, &boxes, NULL);

    if (kind == ROBO_LAYOUT_OBJECT_TEXT) {
        /*
         * A run is a leaf: no children, no box layers, no boundary of its own -- so the walk
         * tests exactly one rectangle and returns. Its bound is its own placement, which is also
         * its answer to "how far can this subtree reach".
         */
        if (trace) {
            hit_trace_node(trace, id, depth, kind, ROBO_PAINT_HIT_REASON_ENTER, boxes.border,
                           boxes.border);
        }
        if (!robo_paint_clip_contains(clip, point)) {
            stats->rejected_clip++;
            return ROBO_OK;
        }
        if (!robo_gfx_rect_contains_point(boxes.border, point)) {
            return ROBO_OK;
        }
        hit_record(result, tree, id, depth, kind, point, origin, &boxes, clip, opacity);
        stats->hits++;
        if (trace) {
            hit_trace_hit(trace, id, depth, point);
        }
        return ROBO_OK;
    }

    /*
     * The subtree bound, and the one early-out that makes a query follow the point rather than
     * the document. It is asked **before** the boundary, because it is a question about geometry
     * -- which node could possibly be here -- where the boundary is a question about drawing, and
     * a reader of the trace wants the geometric reason first.
     */
    if (hit_subtree_bound(tree, id, &boxes, &bound, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_ERROR;
    }
    if (!robo_gfx_rect_contains_point(bound, point)) {
        stats->rejected_region++;
        if (trace) {
            hit_trace_node(trace, id, depth, kind, ROBO_PAINT_HIT_REASON_REGION, boxes.border,
                           bound);
        }
        return ROBO_OK;
    }

    /* The box's own painting is bounded by the boundary it was **entered** with, not by the one
     * its contents get -- 11.8's arrangement, and the reason a `overflow: hidden` box's own
     * border band is still hittable. */
    if (!robo_paint_clip_contains(clip, point)) {
        stats->rejected_clip++;
        if (trace) {
            hit_trace_node(trace, id, depth, kind, ROBO_PAINT_HIT_REASON_CLIP, boxes.border, bound);
        }
        return ROBO_OK;
    }

    if (trace) {
        hit_trace_node(trace, id, depth, kind, ROBO_PAINT_HIT_REASON_ENTER, boxes.border, bound);
    }

    child_opacity = hit_child_opacity(tree, id, opacity);
    if (hit_child_clip(tree, id, &boxes, clip, &child_clip, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_ERROR;
    }

    /*
     * The children, **last first**, because the painter draws them first-to-last and the node on
     * top is the one that answers. Each is entered with this box's content origin and with the
     * states this box's own declarations put in force for its contents -- which is where the clip
     * and the box's own region deliberately part company.
     *
     * A child outside the contents' boundary is not skipped here: it is entered and answers
     * `clip` itself, so the reason reaches the trace and the counter with the node it belongs to
     * rather than being folded into the parent's line.
     */
    child_origin = robo_gfx_point(boxes.content.x, boxes.content.y);
    child = ROBO_LAYOUT_NODE_ID_NONE;
    if (robo_layout_object_last_child(tree, id, &child) == ROBO_OK) {
        while (child != ROBO_LAYOUT_NODE_ID_NONE) {
            robo_result_t status = hit_walk(tree, child, depth + 1u, 1, child_origin, &child_clip,
                                            &child_opacity, point, result, stats, trace, diag);

            if (status != ROBO_OK) {
                return status;
            }
            if (result->outcome == ROBO_PAINT_HIT_ON_NODE) {
                return ROBO_OK;
            }
            if (robo_layout_object_previous_sibling(tree, child, &child) != ROBO_OK) {
                break;
            }
        }
    }

    /*
     * And only now the box itself: its own region is its border box, under the boundary it was
     * entered with, and at the strength its own `opacity` declaration produced -- which is what
     * the painter paints its decorations at.
     *
     * A box that draws nothing is still an answer, and that is CSS's rule rather than a
     * convenience: a transparent `div` receives a point over its whole border box, because
     * painting nothing is not the same as being absent. The alternative -- testing only boxes
     * with a visible background or border -- would need `pointer-events` machinery this build
     * does not have and would be wrong about a document that has none.
     */
    if (!robo_gfx_rect_contains_point(boxes.border, point)) {
        return ROBO_OK;
    }
    hit_record(result, tree, id, depth, kind, point, origin, &boxes, clip, &child_opacity);
    stats->hits++;
    if (trace) {
        hit_trace_hit(trace, id, depth, point);
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The query                                                           */
/* ------------------------------------------------------------------ */

/**
 * The walk's prologue: the states a document starts in, the empty-tree case and the refusal.
 *
 * Split from the trace door below because the two share all of it and differ only in the sink --
 * and a second copy of the refusal is the one place a stale tree could be answered two ways.
 */
static robo_result_t hit_run(const robo_layout_tree_t *tree,
                             robo_layout_point_t point,
                             robo_paint_hit_test_result_t *out,
                             robo_paint_hit_test_stats_t *out_stats,
                             robo_paint_writer_t *trace,
                             robo_paint_diag_t *diag)
{
    robo_paint_hit_test_stats_t stats;
    robo_layout_node_id_t root;
    robo_paint_clip_t clip;
    robo_paint_opacity_t opacity;
    robo_result_t status;

    robo_paint_hit_test_stats_init(&stats);
    robo_paint_hit_test_result_init(out);
    out->point = point;

    /*
     * A tree whose geometry is not final is **refused** rather than answered, and the refusal is
     * an outcome rather than an error for the reason the paint pass gives: the cure is to run a
     * layout pass, and a caller that could not tell "your tree is stale" from "your tree is
     * broken" would go looking for a bug in the document.
     *
     * It is a refusal rather than a best-effort walk because the two mistakes are not symmetric.
     * A hit test against half-computed geometry is not a less accurate answer, it is a wrong one:
     * it names whichever node happens to hold a stale rectangle, and a caller about to dispatch a
     * click on that answer would be delivering it to a node the user is not pointing at.
     */
    if (robo_layout_tree_needs_layout(tree)) {
        out->outcome = ROBO_PAINT_HIT_STALE;
        if (trace) {
            /* One line naming the refusal: nothing was examined, so there is no node to name and
             * the reason column is what a reader needs. */
            robo_layout_rect_t nothing = robo_gfx_rect(0, 0, 0, 0);

            hit_trace_node(trace, ROBO_LAYOUT_NODE_ID_NONE, 0u, ROBO_LAYOUT_OBJECT_NONE, "stale",
                           nothing, nothing);
        }
        if (out_stats) {
            *out_stats = stats;
        }
        return ROBO_OK;
    }

    root = robo_layout_tree_root(tree);
    if (root == ROBO_LAYOUT_NODE_ID_NONE) {
        /* No root box: a document that builds no boxes, or one whose root generates none. A miss
         * with nothing examined, which is what the counters already say rather than a special
         * case a caller has to test for. */
        out->outcome = ROBO_PAINT_HIT_MISS;
        if (out_stats) {
            *out_stats = stats;
        }
        return ROBO_OK;
    }

    /*
     * The root is entered under the two states a document starts in: a boundary that hides
     * neither axis, and full strength. Neither is a scope -- both are what an empty stack
     * answers -- so they are stated here as the initial values rather than pushed, exactly as the
     * paint pass states them, and the strength comes from the opacity module's own door rather
     * than from a written `1.0` for the same reason it does there.
     */
    clip = robo_paint_clip_none();
    opacity = robo_paint_opacity_opaque();
    status = hit_walk(tree, root, 0u, 1, robo_gfx_point(0, 0), &clip, &opacity, point, out, &stats,
                      trace, diag);
    if (status != ROBO_OK) {
        /* A broken tree: the answer is not "no node", it is "the question could not be asked", so
         * the result is returned to its cleared state rather than left half-filled. */
        robo_paint_hit_test_result_init(out);
        out->point = point;
        if (out_stats) {
            *out_stats = stats;
        }
        return status;
    }
    if (out->outcome == ROBO_PAINT_HIT_NONE) {
        out->outcome = ROBO_PAINT_HIT_MISS;
    }
    if (out_stats) {
        *out_stats = stats;
    }
    return ROBO_OK;
}

robo_result_t robo_paint_hit_test(const robo_layout_tree_t *tree,
                                 robo_layout_point_t point,
                                 robo_paint_hit_test_result_t *out,
                                 robo_paint_hit_test_stats_t *out_stats,
                                 robo_paint_diag_t *diag)
{
    if (!tree || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_HIT_TEST, ROBO_INVALID_ARGUMENT,
                               "a tree and a result are both required");
    }
    return hit_run(tree, point, out, out_stats, NULL, diag);
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

robo_result_t robo_paint_hit_test_write(const robo_layout_tree_t *tree,
                                       robo_layout_point_t point,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *out_length,
                                       robo_paint_diag_t *diag)
{
    robo_paint_hit_test_result_t answer;
    robo_paint_hit_test_result_t traced;
    robo_paint_writer_t head;
    robo_paint_writer_t trace;
    robo_result_t status;
    size_t head_length;

    if (!tree || !buffer || !out_length) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_HIT_TEST, ROBO_INVALID_ARGUMENT,
                               "a tree, a buffer and a length are all required");
    }

    /*
     * The answer's own line goes **first**, because it is what a reader came for, and the trace
     * follows in the order the walk examined the objects -- so a wrong answer is read by finding
     * the line that should have been the `hit` one and seeing which reason passed it over.
     *
     * That ordering is why the walk runs **twice**, and it is not a shortcut avoided: the header
     * names the outcome, so it cannot be written until the answer is known, and the trace has to
     * follow it. Running the walk twice is sound because the walk is a **pure function of the
     * tree and the point** -- the same property `robo_paint_hit_test` itself relies on when a
     * caller runs it once per input event -- so the second pass produces exactly the result the
     * first one did, which is the fact `paint.hit-test.dump` pins by dumping one tree twice.
     */
    robo_paint_hit_test_result_init(&answer);
    status = hit_run(tree, point, &answer, NULL, NULL, diag);
    if (status != ROBO_OK) {
        /* A broken tree: the refusal is reported rather than turned into a trace of a walk that
         * did not happen. */
        *out_length = 0u;
        buffer[0] = '\0';
        return status;
    }

    robo_paint_writer_init(&head, buffer, capacity);
    robo_paint_writer_string(&head, "hit-test point=");
    robo_paint_writer_i64(&head, (int64_t)point.x);
    robo_paint_writer_char(&head, ',');
    robo_paint_writer_i64(&head, (int64_t)point.y);
    robo_paint_writer_string(&head, " outcome=");
    robo_paint_writer_string(&head, robo_paint_hit_test_outcome_name(answer.outcome));
    if (answer.outcome == ROBO_PAINT_HIT_ON_NODE) {
        robo_paint_writer_string(&head, " node=");
        robo_paint_writer_u64(&head, (uint64_t)answer.node);
        robo_paint_writer_string(&head, " depth=");
        robo_paint_writer_u64(&head, (uint64_t)answer.depth);
    }
    robo_paint_writer_char(&head, '\n');
    head_length = head.length;

    /*
     * The trace, into whatever room is left. When the header alone did not fit there is no room
     * at all, so the second walk writes into a counting writer -- NULL buffer, zero capacity --
     * purely so that `out_length` can report what the caller needs, which is the contract every
     * `_write` door in this tree uses: a caller sizes a buffer from the refusal rather than from
     * an estimate.
     */
    robo_paint_hit_test_result_init(&traced);
    if (head_length < capacity) {
        robo_paint_writer_init(&trace, buffer + head_length, capacity - head_length);
    } else {
        robo_paint_writer_init(&trace, NULL, 0u);
    }
    status = hit_run(tree, point, &traced, NULL, &trace, diag);
    if (status != ROBO_OK) {
        *out_length = 0u;
        buffer[0] = '\0';
        return status;
    }

    *out_length = head_length + trace.length;
    if (!robo_paint_writer_fits(&head) || !robo_paint_writer_fits(&trace)) {
        return ROBO_NOT_SUPPORTED;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Version note                                                        */
/* ------------------------------------------------------------------ */

/*
 * Both the query and the dump are compilable against a **stale tree**: the refusal is a value,
 * and `hit_run` is the single place it is decided -- so the query's two entry points cannot
 * disagree about whether a tree was walkable, which is the failure a second copy of the check
 * would eventually produce. That is why the dump does not run a walk of its own and why the
 * trace is a sink rather than a second traversal: there is one order, stated once.
 */
