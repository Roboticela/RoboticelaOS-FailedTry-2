/*
 * RoboLayout margins (Phase 10.9) — the CSS seam, and the two policies.
 *
 * Two things live here and they are deliberately unequal in size: a *classification*
 * (what a style declared, per side) and a *policy* (what a block box and an
 * inline-level participant do with it). The classification is four calls into 10.8's
 * seam with signedness opened; the policy is the arithmetic this phase is about, and
 * it is written once per policy rather than once per caller.
 *
 * Nothing here reads a rectangle and nothing here writes one: a margin is a number
 * about space, and the passes are what turn it into coordinates. That is why this
 * file has no `robo_layout_walk_t` in it.
 */

#include "RoboLayout/RoboLayoutMargin.h"

#include <string.h>

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

int robo_layout_margin_kind_is_known(robo_layout_margin_kind_t kind)
{
    return kind == ROBO_LAYOUT_MARGIN_ABSENT || kind == ROBO_LAYOUT_MARGIN_AUTO ||
           kind == ROBO_LAYOUT_MARGIN_RESOLVED || kind == ROBO_LAYOUT_MARGIN_UNRESOLVED;
}

const char *robo_layout_margin_kind_name(robo_layout_margin_kind_t kind)
{
    switch (kind) {
    case ROBO_LAYOUT_MARGIN_ABSENT:
        return "absent";
    case ROBO_LAYOUT_MARGIN_AUTO:
        return "auto";
    case ROBO_LAYOUT_MARGIN_RESOLVED:
        return "resolved";
    case ROBO_LAYOUT_MARGIN_UNRESOLVED:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

robo_layout_margin_t robo_layout_margin_zero(void)
{
    return robo_layout_margin_resolved((robo_gfx_pixel_x_t)0);
}

robo_layout_margin_t robo_layout_margin_absent(void)
{
    robo_layout_margin_t margin;

    margin.kind = ROBO_LAYOUT_MARGIN_ABSENT;
    margin.length = 0;
    return margin;
}

robo_layout_margin_t robo_layout_margin_resolved(robo_gfx_pixel_x_t length)
{
    robo_layout_margin_t margin;

    margin.kind = ROBO_LAYOUT_MARGIN_RESOLVED;
    margin.length = length;
    return margin;
}

robo_layout_margin_t robo_layout_margin_auto(void)
{
    robo_layout_margin_t margin;

    /*
     * A cleared length on the two kinds that carry none, so that two `auto`s built
     * by two callers compare equal -- `robo_layout_margin_equal` compares every field,
     * and a record whose meaningless field held a stack byte would make copying a
     * margin a way to change it.
     */
    margin.kind = ROBO_LAYOUT_MARGIN_AUTO;
    margin.length = 0;
    return margin;
}

robo_layout_margin_t robo_layout_margin_unresolved(void)
{
    robo_layout_margin_t margin;

    margin.kind = ROBO_LAYOUT_MARGIN_UNRESOLVED;
    margin.length = 0;
    return margin;
}

int robo_layout_margin_is_auto(const robo_layout_margin_t *margin)
{
    return margin && margin->kind == ROBO_LAYOUT_MARGIN_AUTO;
}

int robo_layout_margin_is_resolved(const robo_layout_margin_t *margin)
{
    return margin && margin->kind == ROBO_LAYOUT_MARGIN_RESOLVED;
}

int robo_layout_margin_is_absent(const robo_layout_margin_t *margin)
{
    return margin && margin->kind == ROBO_LAYOUT_MARGIN_ABSENT;
}

int robo_layout_margin_is_unresolved(const robo_layout_margin_t *margin)
{
    return margin && margin->kind == ROBO_LAYOUT_MARGIN_UNRESOLVED;
}

int robo_layout_margin_is_valid(const robo_layout_margin_t *margin)
{
    if (!margin) {
        return 0;
    }
    if (!robo_layout_margin_kind_is_known(margin->kind)) {
        return 0;
    }
    /*
     * The length is checked only where it means something, which is the same
     * asymmetry 10.8's dimension predicate has and for the same reason: an `auto`
     * carries a field that nothing reads, and refusing a record because of a byte
     * nobody will look at would make the two kinds unbuildable by a caller that
     * cleared its struct first.
     */
    if (margin->kind == ROBO_LAYOUT_MARGIN_RESOLVED) {
        double magnitude = (double)margin->length;

        if (magnitude < 0.0) {
            magnitude = -magnitude;
        }
        return magnitude <= LAYOUT_STYLE_LENGTH_MAX;
    }
    return 1;
}

robo_result_t robo_layout_margin_length(const robo_layout_margin_t *margin,
                                       robo_gfx_pixel_x_t *out)
{
    if (!margin || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (margin->kind != ROBO_LAYOUT_MARGIN_RESOLVED) {
        /* A miss rather than the zero the record happens to hold, for the reason
         * 10.8's dimension door gives: `margin: 0` is a gap an author asked not to
         * have, and it is a different answer from "there is no number here". */
        return ROBO_NOT_FOUND;
    }
    *out = margin->length;
    return ROBO_OK;
}

int robo_layout_margin_equal(const robo_layout_margin_t *a, const robo_layout_margin_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind != ROBO_LAYOUT_MARGIN_RESOLVED) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/** The longhand each side is read from, indexed by `robo_css_box_side_t`. */
static const char *const layout_margin_property[ROBO_CSS_BOX_SIDE_COUNT] = {
    "margin-top",
    "margin-right",
    "margin-bottom",
    "margin-left"
};

void robo_layout_margins_init(robo_layout_margins_t *margins)
{
    if (!margins) {
        return;
    }
    margins->top = robo_layout_margin_absent();
    margins->right = robo_layout_margin_absent();
    margins->bottom = robo_layout_margin_absent();
    margins->left = robo_layout_margin_absent();
}

/**
 * Map one classification answer onto a margin kind.
 *
 * The seam's second *shape*: 10.8's classification with signedness opened, and its four
 * answers mapped one-for-one onto the margin vocabulary -- which since 10.9 is a
 * *bijection* rather than a fold. It was not always: the first cut of this reader mapped
 * the classification's `ABSENT` onto a resolved zero, because CSS's initial value for a
 * margin is a number rather than the keyword. The counters forced the split, and the
 * reason is worth keeping beside the code: a reader that cannot tell "nobody declared
 * this" from "this was declared `0`" cannot answer *how many margins the document had*,
 * and `stats.block.margins_applied` is exactly that question asked once per placed box.
 */
static robo_layout_margin_t layout_margin_for_kind(layout_style_value_kind_t kind,
                                                   double number)
{
    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded through 10.8's door, which 10.9 made genuinely half-away-from-zero
         * so that `margin: -10.5px` is -11 rather than -10: a margin is the first
         * signed length this module rounds, and the door's own doc has promised
         * "half away from zero" since 10.8. */
        return robo_layout_margin_resolved(robo_layout_px_round(number));
    case LAYOUT_STYLE_VALUE_AUTO:
        return robo_layout_margin_auto();
    case LAYOUT_STYLE_VALUE_UNUSABLE:
        return robo_layout_margin_unresolved();
    case LAYOUT_STYLE_VALUE_ABSENT:
    default:
        return robo_layout_margin_absent();
    }
}

/** Read one side of @p style: the classification, then the mapping above. */
static robo_layout_margin_t layout_margin_of_property(const robo_css_computed_style_t *style,
                                                     const char *property)
{
    layout_style_value_kind_t kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    /* A NULL style cannot happen here (the caller has one by construction) but the
     * classification refuses it, and a refusal is an absence rather than a length. */
    if (!robo_layout_style_classify(style, property, 1, &kind, &number)) {
        return robo_layout_margin_absent();
    }
    return layout_margin_for_kind(kind, number);
}

robo_layout_margins_t robo_layout_margins_of(const robo_css_computed_style_t *style)
{
    robo_layout_margins_t margins;

    margins.top = layout_margin_of_property(style, layout_margin_property[0]);
    margins.right = layout_margin_of_property(style, layout_margin_property[1]);
    margins.bottom = layout_margin_of_property(style, layout_margin_property[2]);
    margins.left = layout_margin_of_property(style, layout_margin_property[3]);
    return margins;
}

robo_result_t robo_layout_margins_side(const robo_layout_margins_t *margins,
                                      robo_css_box_side_t side,
                                      robo_layout_margin_t *out)
{
    if (!margins || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_css_box_side_is_known(side)) {
        return ROBO_NOT_FOUND;
    }
    switch (side) {
    case ROBO_CSS_BOX_SIDE_TOP:
        *out = margins->top;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_RIGHT:
        *out = margins->right;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_BOTTOM:
        *out = margins->bottom;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_LEFT:
    default:
        *out = margins->left;
        return ROBO_OK;
    }
}

int robo_layout_margins_equal(const robo_layout_margins_t *a,
                              const robo_layout_margins_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return robo_layout_margin_equal(&a->top, &b->top) &&
           robo_layout_margin_equal(&a->right, &b->right) &&
           robo_layout_margin_equal(&a->bottom, &b->bottom) &&
           robo_layout_margin_equal(&a->left, &b->left);
}

int robo_layout_margins_is_valid(const robo_layout_margins_t *margins)
{
    if (!margins) {
        return 0;
    }
    return robo_layout_margin_is_valid(&margins->top) &&
           robo_layout_margin_is_valid(&margins->right) &&
           robo_layout_margin_is_valid(&margins->bottom) &&
           robo_layout_margin_is_valid(&margins->left);
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

void robo_layout_margin_edges_init(robo_layout_margin_edges_t *edges,
                                   int32_t containing_width)
{
    if (!edges) {
        return;
    }
    memset(edges, 0, sizeof(*edges));
    edges->available_content_width =
        containing_width > 0 ? containing_width : (int32_t)0;
}

int robo_layout_margin_edges_is_valid(const robo_layout_margin_edges_t *edges)
{
    if (!edges) {
        return 0;
    }
    if (edges->available_content_width < 0) {
        return 0;
    }
    /*
     * The counters have to be a partition of at most four sides, and that is a real
     * check rather than a formality: `resolved` counts sides whose *declaration*
     * reached geometry, so a record claiming five of them is a record some later
     * arithmetic built wrong, and the moment to catch it is before a caller reads a
     * coordinate out of it.
     */
    if (edges->resolved > ROBO_CSS_BOX_SIDE_COUNT ||
        edges->autos_used > ROBO_CSS_BOX_SIDE_COUNT ||
        edges->unresolved > ROBO_CSS_BOX_SIDE_COUNT) {
        return 0;
    }
    return edges->resolved + edges->autos_used + edges->unresolved <=
           ROBO_CSS_BOX_SIDE_COUNT;
}

int robo_layout_margin_edges_equal(const robo_layout_margin_edges_t *a,
                                  const robo_layout_margin_edges_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->top == b->top && a->right == b->right && a->bottom == b->bottom &&
           a->left == b->left &&
           a->available_content_width == b->available_content_width &&
           a->resolved == b->resolved && a->autos_used == b->autos_used &&
           a->unresolved == b->unresolved;
}

robo_result_t robo_layout_margin_edges_outer_width(const robo_layout_margin_edges_t *edges,
                                                  int32_t content_width,
                                                  int32_t *out)
{
    if (!edges || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = edges->left + content_width + edges->right;
    return ROBO_OK;
}

/**
 * Use every side that is not a horizontal `auto`: the shared half of both policies.
 *
 * Fills the four used values and the three counters, leaving `left` and `right` at
 * **zero** where the declaration was `auto` -- so a policy that means to distribute
 * finds the non-auto arithmetic already done, and a policy that does not finds the
 * used value it wants sitting there. That is the whole of the sharing: the two
 * policies differ only in what happens to a horizontal `auto` next.
 *
 * `auto` on a vertical side is used as zero *here*, in the shared half, because it is
 * zero under both policies and for two different reasons -- normal flow does not
 * distribute vertically, and an inline participant has no containing block to
 * distribute against.
 */
static void layout_margins_use_non_auto(const robo_layout_margins_t *margins,
                                        robo_layout_margin_edges_t *out)
{
    const robo_layout_margin_t *sides[ROBO_CSS_BOX_SIDE_COUNT];
    robo_gfx_pixel_x_t *used[ROBO_CSS_BOX_SIDE_COUNT];
    size_t index;

    sides[ROBO_CSS_BOX_SIDE_TOP] = &margins->top;
    sides[ROBO_CSS_BOX_SIDE_RIGHT] = &margins->right;
    sides[ROBO_CSS_BOX_SIDE_BOTTOM] = &margins->bottom;
    sides[ROBO_CSS_BOX_SIDE_LEFT] = &margins->left;
    used[ROBO_CSS_BOX_SIDE_TOP] = &out->top;
    used[ROBO_CSS_BOX_SIDE_RIGHT] = &out->right;
    used[ROBO_CSS_BOX_SIDE_BOTTOM] = &out->bottom;
    used[ROBO_CSS_BOX_SIDE_LEFT] = &out->left;

    for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
        *used[index] = 0;
        switch (sides[index]->kind) {
        case ROBO_LAYOUT_MARGIN_RESOLVED:
            *used[index] = sides[index]->length;
            out->resolved++;
            break;
        case ROBO_LAYOUT_MARGIN_ABSENT:
            /* Zero, and **counted nowhere**: the initial value is what the geometry
             * uses, and the counters are about declarations. The distinction survives
             * the switch -- which is the only reason the kind exists. */
            break;
        case ROBO_LAYOUT_MARGIN_UNRESOLVED:
            /* Used as zero and *counted*, which is 10.8's rule for a declaration it
             * cannot resolve: a document must not fail because of one, and an
             * author's `5%` must not be silently a confident zero nobody can see. */
            out->unresolved++;
            break;
        case ROBO_LAYOUT_MARGIN_AUTO:
        default:
            /* Left at zero for the policy that follows to decide about. */
            break;
        }
    }
}

/** Take the two horizontal margins out of the containing block, once, at the end. */
static void layout_margins_finish(robo_layout_margin_edges_t *edges, int32_t containing_width)
{
    int64_t available = (int64_t)containing_width - (int64_t)edges->left -
                        (int64_t)edges->right;

    /*
     * Floored, and floored *here* rather than at each term, because a negative margin
     * is a legal declaration that must come out negative in the edges themselves --
     * the box then overhangs its containing block, which is what CSS does -- while the
     * width left for its contents must never go below zero. One clamp, in one place,
     * on the one number a size arithmetic can act on.
     */
    if (available < 0) {
        available = 0;
    }
    if (available > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        available = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    edges->available_content_width = (int32_t)available;
}

robo_result_t robo_layout_margins_resolve_block(const robo_layout_margins_t *margins,
                                               int32_t containing_width,
                                               const robo_layout_dimension_t *width,
                                               robo_layout_margin_edges_t *out,
                                               robo_layout_diag_t *diag)
{
    robo_layout_margin_edges_t edges;
    int left_auto;
    int right_auto;

    if (!margins || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_MARGINS, ROBO_INVALID_ARGUMENT,
                                "resolving margins needs a record and an out");
    }

    robo_layout_margin_edges_init(&edges, containing_width);
    layout_margins_use_non_auto(margins, &edges);

    left_auto = robo_layout_margin_is_auto(&margins->left);
    right_auto = robo_layout_margin_is_auto(&margins->right);

    /*
     * The distribution, and the one condition that decides whether there is anything
     * to distribute: the box's width has to be **settled** before the space beside it
     * can be. An `auto` width takes all the space there is, so `auto` margins beside
     * it have nothing left to take -- CSS says their used value is 0 -- and a width
     * this build cannot resolve is treated the same way, because the *fill* rule for
     * it is the same rule.
     */
    if ((left_auto || right_auto) && width &&
        robo_layout_dimension_is_definite(width)) {
        int64_t leftover = (int64_t)containing_width - (int64_t)width->length -
                           (int64_t)edges.left - (int64_t)edges.right;

        if (leftover > 0) {
            if (left_auto && right_auto) {
                /*
                 * Split, with an odd pixel going **right**, so a centred box in an odd
                 * leftover sits one pixel left of centre. CSS 2.1 says the two used
                 * values are equal, which integers cannot always do; a build has to
                 * pick, the choice is invisible in every case that does not look for
                 * it, and `layout.margins.block_auto_odd` is the case that looks.
                 */
                int64_t half = leftover / 2;

                edges.left = (robo_gfx_pixel_x_t)half;
                edges.right = (robo_gfx_pixel_x_t)(leftover - half);
                edges.autos_used += 2u;
            } else if (left_auto) {
                edges.left = (robo_gfx_pixel_x_t)leftover;
                edges.autos_used++;
            } else {
                edges.right = (robo_gfx_pixel_x_t)leftover;
                edges.autos_used++;
            }
        }
        /*
         * And when there is no leftover -- an over-constrained box, where the margins
         * and the width already exceed the containing block -- both autos stay at the
         * zero the shared half left them at and the box overflows to the right. CSS
         * resolves this by adjusting `margin-right` and calling the layout
         * over-constrained; this build does not adjust a margin its author did not
         * leave `auto`, because the number it would write is not a number the style
         * asked for and the overflow is 10.13's to report.
         */
    }

    layout_margins_finish(&edges, containing_width);
    if (!robo_layout_margin_edges_is_valid(&edges)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_MARGINS, ROBO_ERROR,
                                "a margin resolution produced a record no result can "
                                "hold");
    }
    *out = edges;
    return ROBO_OK;
}

robo_result_t robo_layout_margins_resolve_inline(const robo_layout_margins_t *margins,
                                                int32_t containing_width,
                                                robo_layout_margin_edges_t *out,
                                                robo_layout_diag_t *diag)
{
    robo_layout_margin_edges_t edges;

    if (!margins || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_MARGINS, ROBO_INVALID_ARGUMENT,
                                "resolving margins needs a record and an out");
    }

    robo_layout_margin_edges_init(&edges, containing_width);
    layout_margins_use_non_auto(margins, &edges);

    /*
     * Every `auto` is already zero, and that is the policy rather than an omission:
     * `auto` margins distribute against a containing block in normal flow, and an
     * inline-level participant has no width constraint to be under- or over-. This
     * door exists so a caller cannot accidentally get the block rule by reaching for
     * the only function there is -- and `autos_used` stays 0 here, so the difference
     * between the two policies is visible in the record rather than only in the
     * numbers.
     */
    layout_margins_finish(&edges, containing_width);
    *out = edges;
    return ROBO_OK;
}
