/*
 * RoboPaint geometry (Phase 11.1) — see RoboPaintGeometry.h.
 *
 * The three boxes and the boundary, and the whole of this module's arithmetic lives
 * here: two derivations of nested rectangles, one translation, and one per-axis
 * intersection. Everything else in the module either reads a record or records one.
 */

#include "RoboPaint/RoboPaintGeometry.h"

#include "RoboPaintInternal.h"

/* ------------------------------------------------------------------ */
/* The two inset records, as arrays                                        */
/* ------------------------------------------------------------------ */

/*
 * Both layers are read through RoboLayout's own width doors rather than by reaching for
 * `edges->top.width`, and the reason is the one `RoboLayoutBorder.h` gives for the door
 * existing: "which field of the used record is the width" is asked once here instead of
 * at every site that does arithmetic with it. Eight arithmetic sites would be eight
 * chances to read a style by mistake.
 */

/** The four sides, in the order the arrays below are indexed. */
static const robo_css_box_side_t paint_sides[ROBO_CSS_BOX_SIDE_COUNT] = {
    ROBO_CSS_BOX_SIDE_TOP,
    ROBO_CSS_BOX_SIDE_RIGHT,
    ROBO_CSS_BOX_SIDE_BOTTOM,
    ROBO_CSS_BOX_SIDE_LEFT
};

robo_result_t robo_paint_border_widths(const robo_layout_border_edges_t *borders,
                                       int32_t out_widths[ROBO_CSS_BOX_SIDE_COUNT],
                                       robo_paint_diag_t *diag)
{
    size_t index;

    if (!borders || !out_widths) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "borders and the width array are required");
    }
    if (!robo_layout_border_edges_is_valid(borders)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the border record is not one this build produced");
    }
    for (index = 0u; index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; index++) {
        robo_gfx_pixel_x_t width = 0;

        if (robo_layout_border_edges_width(borders, paint_sides[index], &width) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                                   "a border side's used width could not be read");
        }
        out_widths[index] = (int32_t)width;
    }
    return ROBO_OK;
}

robo_result_t robo_paint_padding_widths(const robo_layout_padding_edges_t *padding,
                                        int32_t out_widths[ROBO_CSS_BOX_SIDE_COUNT],
                                        robo_paint_diag_t *diag)
{
    if (!padding || !out_widths) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "padding and the width array are required");
    }
    if (!robo_layout_padding_edges_is_valid(padding)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the padding record is not one this build produced");
    }
    /*
     * The padding layer has no per-side reader: its four fields are the four sides in
     * the same `top, right, bottom, left` order that `robo_css_box_side_t` uses, so the
     * array is filled in that order directly -- and the order is asserted once, by the
     * suite, against a record it built from a real stylesheet.
     */
    out_widths[ROBO_CSS_BOX_SIDE_TOP] = (int32_t)padding->top;
    out_widths[ROBO_CSS_BOX_SIDE_RIGHT] = (int32_t)padding->right;
    out_widths[ROBO_CSS_BOX_SIDE_BOTTOM] = (int32_t)padding->bottom;
    out_widths[ROBO_CSS_BOX_SIDE_LEFT] = (int32_t)padding->left;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The three boxes                                                     */
/* ------------------------------------------------------------------ */

void robo_paint_boxes_init(robo_paint_boxes_t *boxes)
{
    if (!boxes) {
        return;
    }
    boxes->border = robo_gfx_rect_empty();
    boxes->padding = robo_gfx_rect_empty();
    boxes->content = robo_gfx_rect_empty();
    boxes->insets_measured = 0;
}

int robo_paint_boxes_is_valid(const robo_paint_boxes_t *boxes)
{
    if (!boxes) {
        return 0;
    }
    if (!robo_gfx_rect_is_valid(boxes->border) || !robo_gfx_rect_is_valid(boxes->padding) ||
        !robo_gfx_rect_is_valid(boxes->content)) {
        return 0;
    }
    if (!robo_gfx_rect_encloses(boxes->border, boxes->padding) ||
        !robo_gfx_rect_encloses(boxes->padding, boxes->content)) {
        return 0;
    }
    /*
     * The flag and the geometry have to agree: a box whose insets were not measured has
     * one rectangle wearing three names, and a record that claimed otherwise would be
     * telling a consumer it may draw a border band that is empty because nobody computed
     * one.
     */
    if (!boxes->insets_measured &&
        !(robo_gfx_rect_equal(boxes->border, boxes->padding) &&
          robo_gfx_rect_equal(boxes->padding, boxes->content))) {
        return 0;
    }
    return 1;
}

int robo_paint_boxes_equal(const robo_paint_boxes_t *a, const robo_paint_boxes_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->insets_measured == b->insets_measured &&
           robo_gfx_rect_equal(a->border, b->border) &&
           robo_gfx_rect_equal(a->padding, b->padding) &&
           robo_gfx_rect_equal(a->content, b->content);
}

/**
 * The layer inside @p outer, inset by @p left/@p top/@p right/@p bottom -- and
 * **clamped so that it is always inside @p outer**.
 *
 * The clamp is the difference between an arithmetic that is right for an ordinary box and
 * one that is right for every box, and the case that needs it is ordinary enough: a
 * `width: 10px` box with a `border: 20px solid` has insets that the box cannot hold, and
 * the unclamped answer puts the padding box's *origin* outside the border box. That is not
 * a box -- it is a rectangle whose layers cross -- and this module's own validity rule
 * (`robo_paint_boxes_is_valid`) would then refuse every operation derived from it, which
 * would fail a pass on a document that is merely extreme.
 *
 * So a layer the insets squeeze out is an **empty layer at the outer box's own edge**, and
 * the three rectangles nest by construction rather than by luck. That is also the readable
 * answer: a border wider than its box covers the box, and there is no room left inside it
 * for anything.
 *
 * Every sum is taken in 64 bits and saturated, for the reason `RoboPaintInternal.h`
 * states: an accumulation of two bounded coordinates is not bounded.
 */
static void paint_inset_rect(const robo_layout_rect_t *outer,
                             int32_t left,
                             int32_t top,
                             int32_t right,
                             int32_t bottom,
                             robo_layout_rect_t *out)
{
    int32_t outer_right = robo_paint_add_saturated(outer->x, outer->width);
    int32_t outer_bottom = robo_paint_add_saturated(outer->y, outer->height);
    int32_t x = robo_paint_add_saturated(outer->x, left);
    int32_t y = robo_paint_add_saturated(outer->y, top);
    int32_t far_x = robo_paint_sub_saturated(outer_right, right);
    int32_t far_y = robo_paint_sub_saturated(outer_bottom, bottom);

    /* The near edge is pulled back inside the outer box, then the far edge is pushed out
     * to at least the near one -- which is what keeps the extent non-negative without a
     * separate floor. */
    if (x < outer->x) {
        x = outer->x;
    }
    if (x > outer_right) {
        x = outer_right;
    }
    if (y < outer->y) {
        y = outer->y;
    }
    if (y > outer_bottom) {
        y = outer_bottom;
    }
    if (far_x > outer_right) {
        far_x = outer_right;
    }
    if (far_x < x) {
        far_x = x;
    }
    if (far_y > outer_bottom) {
        far_y = outer_bottom;
    }
    if (far_y < y) {
        far_y = y;
    }

    out->x = x;
    out->y = y;
    out->width = robo_paint_extent((int64_t)far_x - (int64_t)x);
    out->height = robo_paint_extent((int64_t)far_y - (int64_t)y);
}

robo_result_t robo_paint_boxes_from_layers(const robo_layout_rect_t *border_box,
                                           const robo_layout_border_edges_t *borders,
                                           const robo_layout_padding_edges_t *padding,
                                           robo_paint_boxes_t *out,
                                           robo_paint_diag_t *diag)
{
    int32_t border_widths[ROBO_CSS_BOX_SIDE_COUNT];
    int32_t paddings[ROBO_CSS_BOX_SIDE_COUNT];

    if (!border_box || !borders || !padding || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "the border box and both inset records are required");
    }
    if (!robo_gfx_rect_is_valid(*border_box)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "a border box with a negative size is not a box");
    }
    if (robo_paint_border_widths(borders, border_widths, diag) != ROBO_OK ||
        robo_paint_padding_widths(padding, paddings, diag) != ROBO_OK) {
        return diag ? diag->code : ROBO_ERROR;
    }

    out->border = *border_box;

    /*
     * Two applications of the same clamp, and the order matters: the padding box is the
     * border box inset by the border widths, and the content box is *that* inset by the
     * paddings -- which is what makes the three compose in the order CSS's box model does
     * rather than each being computed from the border box independently.
     */
    paint_inset_rect(border_box, border_widths[ROBO_CSS_BOX_SIDE_LEFT],
                     border_widths[ROBO_CSS_BOX_SIDE_TOP],
                     border_widths[ROBO_CSS_BOX_SIDE_RIGHT],
                     border_widths[ROBO_CSS_BOX_SIDE_BOTTOM], &out->padding);
    paint_inset_rect(&out->padding, paddings[ROBO_CSS_BOX_SIDE_LEFT],
                     paddings[ROBO_CSS_BOX_SIDE_TOP], paddings[ROBO_CSS_BOX_SIDE_RIGHT],
                     paddings[ROBO_CSS_BOX_SIDE_BOTTOM], &out->content);

    out->insets_measured = 1;
    return ROBO_OK;
}

robo_result_t robo_paint_boxes_of(const robo_layout_tree_t *tree,
                                  robo_layout_node_id_t id,
                                  robo_paint_boxes_t *out,
                                  robo_paint_diag_t *diag)
{
    robo_layout_result_t result;
    robo_layout_border_edges_t borders;
    robo_layout_padding_edges_t padding;
    robo_layout_rect_t border_box;
    robo_result_t border_status;
    robo_result_t padding_status;

    if (!tree || !out || id == ROBO_LAYOUT_NODE_ID_NONE) {
        /* A `NONE` id is an *argument* error and a missing box is a *miss*: the same
         * division every read door in this tree draws, and the reason the two are not
         * folded into one code. */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "a tree, an id and an output are required");
    }
    robo_paint_boxes_init(out);

    if (robo_layout_object_result(tree, id, &result) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_NOT_FOUND,
                               "the id names no box in this tree");
    }
    /*
     * An object nothing placed is **not paint**: there is no rectangle, and answering
     * three zeroed ones would put a box at the page origin with no size, which is a
     * wrong answer a consumer could not tell from a real empty box. RoboLayout keeps the
     * per-box authority in the result *state* for exactly this question, so this door
     * asks it rather than inferring from a zero.
     */
    if (!robo_layout_result_is_computed(&result)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_NOT_FOUND,
                               "nothing placed this object, so it has no geometry to paint");
    }
    if (robo_layout_result_rect(&result, &border_box, NULL) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the result's geometry is not a rectangle");
    }

    border_status = robo_layout_object_borders(tree, id, &borders, NULL);
    padding_status = robo_layout_object_padding(tree, id, &padding, NULL);
    if (border_status == ROBO_OK && padding_status == ROBO_OK) {
        return robo_paint_boxes_from_layers(&border_box, &borders, &padding, out, diag);
    }
    /*
     * Both doors decline the same two things -- a **text run** and a **transparent inline
     * box with contents** -- and declining them is a statement rather than a failure: a
     * run is not a box, and a transparent inline box's rectangle is the union of what it
     * enclosed, which this build does not inflate by a declared padding and border
     * (`RoboLayoutPadding.h` argues it). So the box has one rectangle wearing three
     * names, and the flag says so.
     *
     * A failure that is not a decline is a different answer: a record the layout pass
     * never wrote, or one that is not a record at all, means the tree is not the state
     * this door was promised, and it is reported rather than flattened into "no insets".
     */
    if ((border_status != ROBO_OK && border_status != ROBO_NOT_FOUND) ||
        (padding_status != ROBO_OK && padding_status != ROBO_NOT_FOUND)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the box's inset records could not be read");
    }
    out->border = border_box;
    out->padding = border_box;
    out->content = border_box;
    out->insets_measured = 0;
    return ROBO_OK;
}

robo_result_t robo_paint_boxes_translate(const robo_paint_boxes_t *boxes,
                                         int32_t dx,
                                         int32_t dy,
                                         robo_paint_boxes_t *out,
                                         robo_paint_diag_t *diag)
{
    if (!boxes || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "the boxes and an output are required");
    }
    *out = *boxes;
    out->border.x = robo_paint_add_saturated(boxes->border.x, dx);
    out->border.y = robo_paint_add_saturated(boxes->border.y, dy);
    out->padding.x = robo_paint_add_saturated(boxes->padding.x, dx);
    out->padding.y = robo_paint_add_saturated(boxes->padding.y, dy);
    out->content.x = robo_paint_add_saturated(boxes->content.x, dx);
    out->content.y = robo_paint_add_saturated(boxes->content.y, dy);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The clip in force                                                   */
/* ------------------------------------------------------------------ */

void robo_paint_clip_init(robo_paint_clip_t *clip)
{
    if (!clip) {
        return;
    }
    clip->horizontal.mode = ROBO_CSS_OVERFLOW_VISIBLE;
    clip->horizontal.start = 0;
    clip->horizontal.end = 0;
    clip->vertical.mode = ROBO_CSS_OVERFLOW_VISIBLE;
    clip->vertical.start = 0;
    clip->vertical.end = 0;
}

robo_paint_clip_t robo_paint_clip_none(void)
{
    robo_paint_clip_t clip;

    robo_paint_clip_init(&clip);
    return clip;
}

int robo_paint_clip_axis_is_active(const robo_paint_clip_axis_t *axis)
{
    if (!axis) {
        return 0;
    }
    /* "has a boundary" and "this mode clips" are the same question, and RoboCSS already
     * answers it -- so the identity is borrowed rather than re-decided here, and a mode
     * added to that vocabulary later that also clips counts as active without a change
     * in this file. */
    return robo_css_overflow_clips(axis->mode);
}

int robo_paint_clip_is_active(const robo_paint_clip_t *clip)
{
    if (!clip) {
        return 0;
    }
    return robo_paint_clip_axis_is_active(&clip->horizontal) ||
           robo_paint_clip_axis_is_active(&clip->vertical);
}

int robo_paint_clip_is_valid(const robo_paint_clip_t *clip)
{
    if (!clip) {
        return 0;
    }
    if (!robo_css_overflow_is_known(clip->horizontal.mode) ||
        !robo_css_overflow_is_known(clip->vertical.mode)) {
        return 0;
    }
    if (robo_paint_clip_axis_is_active(&clip->horizontal) &&
        clip->horizontal.end < clip->horizontal.start) {
        return 0;
    }
    if (robo_paint_clip_axis_is_active(&clip->vertical) &&
        clip->vertical.end < clip->vertical.start) {
        return 0;
    }
    return 1;
}

int robo_paint_clip_equal(const robo_paint_clip_t *a, const robo_paint_clip_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->horizontal.mode == b->horizontal.mode &&
           a->horizontal.start == b->horizontal.start &&
           a->horizontal.end == b->horizontal.end &&
           a->vertical.mode == b->vertical.mode &&
           a->vertical.start == b->vertical.start && a->vertical.end == b->vertical.end;
}

int robo_paint_clip_axis_is_empty(const robo_paint_clip_axis_t *axis)
{
    return robo_paint_clip_axis_is_active(axis) && axis->end <= axis->start;
}

int robo_paint_clip_is_empty(const robo_paint_clip_t *clip)
{
    if (!clip) {
        return 0;
    }
    /* Either axis is enough: an interval with no width bounds nothing, whatever the other
     * axis says -- which is why this is an `or` and not an `and`. */
    return robo_paint_clip_axis_is_empty(&clip->horizontal) ||
           robo_paint_clip_axis_is_empty(&clip->vertical);
}

/**
 * One axis of the intersection: @p existing bounded by @p start/@p end when @p mode
 * hides, carried through unchanged when it does not.
 *
 * A helper rather than two copies because the two axes differ only in which numbers they
 * are given, and a transposed pair of copies is the classic way this arithmetic goes
 * wrong.
 *
 * **A disjoint result is canonicalized to a zero-length interval**, and that is a decision
 * rather than a tidy-up. The smaller of two `max`es and the larger of two `min`es can come out
 * backwards -- a box an ancestor's boundary entirely excluded produces exactly that -- and a
 * record whose interval runs backwards is one `robo_paint_clip_is_valid` refuses. Left alone,
 * the cell of the record model that means "nothing can be drawn here" would be the one cell that
 * is not a record, and every consumer would have to choose between trusting the type and
 * trusting the arithmetic. Canonicalized, an excluded box is an **empty interval at the
 * boundary that excluded it** (`end == start`), which is valid, is what
 * `robo_paint_clip_axis_is_empty` answers, and survives the whole pipeline -- a push, a pop, a
 * stack, a display command and a projection to a rectangle of zero width -- without a caller
 * ever having to ask whether it is holding a legal record.
 *
 * The placement of the empty interval is the **upper start**: the larger of the two starts, the
 * later of the two edges either interval began at. That is a function of the two intervals and of
 * nothing else, which is what makes it deterministic for equal inputs -- no notion of which side
 * "did the excluding" enters the arithmetic, and a `[200, 210]` box under a `[0, 40]` boundary
 * lands on `[200, 200]` just as a `[0, 40]` box under a `[200, 210]` boundary would.
 */
static void paint_clip_intersect_axis(robo_paint_clip_axis_t *out,
                                      const robo_paint_clip_axis_t *existing,
                                      robo_css_overflow_t mode,
                                      int32_t start,
                                      int32_t end)
{
    if (!robo_css_overflow_clips(mode)) {
        /* A `visible` axis does not clip, and it does not *unclip* either: whatever an
         * ancestor put in force stays in force, which is why this is a copy and not a
         * clear. */
        *out = *existing;
        return;
    }
    if (robo_paint_clip_axis_is_active(existing)) {
        out->mode = mode;
        out->start = robo_paint_max(existing->start, start);
        out->end = robo_paint_min(existing->end, end);
    } else {
        out->mode = mode;
        out->start = start;
        out->end = end;
    }
    if (out->end < out->start) {
        out->end = out->start;
    }
}

robo_result_t robo_paint_clip_intersect(const robo_paint_clip_t *clip,
                                       const robo_layout_rect_t *boundary,
                                       robo_css_overflow_t horizontal,
                                       robo_css_overflow_t vertical,
                                       robo_paint_clip_t *out,
                                       robo_paint_diag_t *diag)
{
    int32_t right;
    int32_t bottom;

    if (!clip || !boundary || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_INVALID_ARGUMENT,
                               "the clip, the boundary and an output are required");
    }
    if (!robo_css_overflow_is_known(horizontal) || !robo_css_overflow_is_known(vertical)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_INVALID_ARGUMENT,
                               "an overflow mode this build does not have");
    }
    if (!robo_gfx_rect_is_valid(*boundary)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_INVALID_ARGUMENT,
                               "a boundary with a negative size is not a rectangle");
    }
    if (!robo_paint_clip_is_valid(clip)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_ERROR,
                               "the clip in force is not a record this build produced");
    }

    /* The far edges in 64 bits: `x + width` is the sum of two bounded numbers. */
    right = robo_paint_add_saturated(boundary->x, boundary->width);
    bottom = robo_paint_add_saturated(boundary->y, boundary->height);

    paint_clip_intersect_axis(&out->horizontal, &clip->horizontal, horizontal,
                              boundary->x, right);
    paint_clip_intersect_axis(&out->vertical, &clip->vertical, vertical,
                              boundary->y, bottom);
    return ROBO_OK;
}

robo_result_t robo_paint_clip_intersect_clip(const robo_paint_clip_t *outer,
                                            const robo_paint_clip_t *inner,
                                            robo_paint_clip_t *out,
                                            robo_paint_diag_t *diag)
{
    if (!outer || !inner || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_INVALID_ARGUMENT,
                               "both clips and an output are required");
    }
    if (!robo_paint_clip_is_valid(outer) || !robo_paint_clip_is_valid(inner)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_ERROR,
                               "a clip in the intersection is not a record this build "
                               "produced");
    }
    /*
     * The same per-axis helper the boundary intersection uses, in the same order, for the
     * reason it exists: the two axes differ only in which numbers they are given, and a
     * transposed pair of copies is the classic way this arithmetic goes wrong. An axis
     * `inner` does not bound takes the `visible` branch inside the helper and is copied from
     * `outer` -- so "a scope that bounds only one axis does not free the other" is the
     * helper's own rule rather than a second statement of it here.
     */
    paint_clip_intersect_axis(&out->horizontal, &outer->horizontal, inner->horizontal.mode,
                              inner->horizontal.start, inner->horizontal.end);
    paint_clip_intersect_axis(&out->vertical, &outer->vertical, inner->vertical.mode,
                              inner->vertical.start, inner->vertical.end);
    return ROBO_OK;
}

robo_result_t robo_paint_clip_rect(const robo_paint_clip_t *clip, robo_layout_rect_t *out)
{
    if (!clip || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_paint_clip_axis_is_active(&clip->horizontal) ||
        !robo_paint_clip_axis_is_active(&clip->vertical)) {
        return ROBO_NOT_FOUND;
    }
    out->x = clip->horizontal.start;
    out->y = clip->vertical.start;
    out->width = robo_paint_extent((int64_t)clip->horizontal.end - (int64_t)clip->horizontal.start);
    out->height = robo_paint_extent((int64_t)clip->vertical.end - (int64_t)clip->vertical.start);
    return ROBO_OK;
}
