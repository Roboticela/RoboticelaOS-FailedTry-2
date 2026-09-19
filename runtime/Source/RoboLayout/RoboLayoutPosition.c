/*
 * RoboLayout positioning (Phase 10.12) — see RoboLayoutPosition.h.
 *
 * One reader over a computed style, one policy over the declarations it read, and one
 * arithmetic over the numbers the policy produced. The file is short because the phase
 * is: the vocabulary it needs already exists (9.11's offset shape, 9.13's mode), so
 * what is left is the four lines that turn two opposing pairs into two numbers.
 *
 * The reader is the one place the shared classification is asked with
 * `allow_negative = 1`, which is the margin reader's parameter rather than the
 * dimension, padding or border reader's -- and it is not a coincidence: an inset and a
 * margin are the same `EDGE_OFFSET` grammar (9.13 says so), and `left: -10px` is legal
 * for exactly the reason `margin-left: -10px` is.
 *
 * The policy is where the phase's one rule lives:
 *
 *   left wins over right, top wins over bottom, and an `auto` side says nothing
 *
 * which is deliberately *less* than CSS's offset resolution (writing mode, direction
 * and containing-block anchoring all decide which of an opposing pair is the one that
 * means something) and deliberately stated at the rule rather than spread across the
 * callers, so that the phase that implements the full rule changes this function and
 * nothing else.
 *
 * No allocations, no state between calls, no cache: every function below is a function
 * of the record it was handed, which is what makes re-layout a matter of calling the
 * pass again.
 */

#include "RoboLayout/RoboLayoutPosition.h"

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* The inset's kinds                                                   */
/* ------------------------------------------------------------------ */

int robo_layout_inset_kind_is_known(robo_layout_inset_kind_t kind)
{
    return kind == ROBO_LAYOUT_INSET_ABSENT || kind == ROBO_LAYOUT_INSET_RESOLVED ||
           kind == ROBO_LAYOUT_INSET_AUTO || kind == ROBO_LAYOUT_INSET_UNRESOLVED;
}

const char *robo_layout_inset_kind_name(robo_layout_inset_kind_t kind)
{
    switch (kind) {
    case ROBO_LAYOUT_INSET_ABSENT:
        return "absent";
    case ROBO_LAYOUT_INSET_RESOLVED:
        return "resolved";
    case ROBO_LAYOUT_INSET_AUTO:
        return "auto";
    case ROBO_LAYOUT_INSET_UNRESOLVED:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

/** The one shape every constructor below has: a kind and a length that means
 *  something for exactly one of the four. */
static robo_layout_inset_t layout_inset_make(robo_layout_inset_kind_t kind,
                                             robo_gfx_pixel_x_t length)
{
    robo_layout_inset_t inset;

    inset.kind = kind;
    inset.length = length;
    return inset;
}

robo_layout_inset_t robo_layout_inset_zero(void)
{
    return layout_inset_make(ROBO_LAYOUT_INSET_RESOLVED, 0);
}

robo_layout_inset_t robo_layout_inset_absent(void)
{
    return layout_inset_make(ROBO_LAYOUT_INSET_ABSENT, 0);
}

robo_layout_inset_t robo_layout_inset_resolved(robo_gfx_pixel_x_t length)
{
    return layout_inset_make(ROBO_LAYOUT_INSET_RESOLVED, length);
}

robo_layout_inset_t robo_layout_inset_auto(void)
{
    return layout_inset_make(ROBO_LAYOUT_INSET_AUTO, 0);
}

robo_layout_inset_t robo_layout_inset_unresolved(void)
{
    return layout_inset_make(ROBO_LAYOUT_INSET_UNRESOLVED, 0);
}

int robo_layout_inset_is_auto(const robo_layout_inset_t *inset)
{
    return inset && inset->kind == ROBO_LAYOUT_INSET_AUTO;
}

int robo_layout_inset_is_resolved(const robo_layout_inset_t *inset)
{
    return inset && inset->kind == ROBO_LAYOUT_INSET_RESOLVED;
}

int robo_layout_inset_is_absent(const robo_layout_inset_t *inset)
{
    return inset && inset->kind == ROBO_LAYOUT_INSET_ABSENT;
}

int robo_layout_inset_is_unresolved(const robo_layout_inset_t *inset)
{
    return inset && inset->kind == ROBO_LAYOUT_INSET_UNRESOLVED;
}

int robo_layout_inset_is_valid(const robo_layout_inset_t *inset)
{
    if (!inset || !robo_layout_inset_kind_is_known(inset->kind)) {
        return 0;
    }
    /*
     * The range check the shared classification already made, restated as a predicate
     * for the reason 10.8 gives for its dimension: a caller may hand this module a
     * record it built itself, and "is this something I may read?" should have an answer
     * that does not depend on how it was built. Only a `RESOLVED` inset has a length, so
     * only one kind is bounded.
     */
    if (inset->kind == ROBO_LAYOUT_INSET_RESOLVED &&
        (inset->length > (robo_gfx_pixel_x_t)LAYOUT_STYLE_LENGTH_MAX ||
         inset->length < (robo_gfx_pixel_x_t)(-LAYOUT_STYLE_LENGTH_MAX))) {
        return 0;
    }
    return 1;
}

robo_result_t robo_layout_inset_length(const robo_layout_inset_t *inset,
                                       robo_gfx_pixel_x_t *out)
{
    if (!inset || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (inset->kind != ROBO_LAYOUT_INSET_RESOLVED) {
        return ROBO_NOT_FOUND;
    }
    *out = inset->length;
    return ROBO_OK;
}

int robo_layout_inset_equal(const robo_layout_inset_t *a, const robo_layout_inset_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    /* The length only where it means something: two `ABSENT` sides are equal whatever
     * their stale `length` fields hold, which is the same rule the kind exists for. */
    if (a->kind != ROBO_LAYOUT_INSET_RESOLVED) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/** The longhand each side is read from, indexed by `robo_css_box_side_t`. */
static const char *const layout_inset_property[ROBO_CSS_BOX_SIDE_COUNT] = {
    "top",
    "right",
    "bottom",
    "left"
};

void robo_layout_insets_init(robo_layout_insets_t *insets)
{
    if (!insets) {
        return;
    }
    insets->top = robo_layout_inset_absent();
    insets->right = robo_layout_inset_absent();
    insets->bottom = robo_layout_inset_absent();
    insets->left = robo_layout_inset_absent();
}

/**
 * The declared inset @p style holds for @p property.
 *
 * The shared classification, with the parameter a **margin** passes and a dimension,
 * a padding and a border do not: an inset is signed. That is the only respect in which
 * this reader differs from the border and padding readers beside it, and it is a
 * property of the CSS grammar rather than a choice -- `top: -10px` is legal for the
 * same reason `margin-top: -10px` is, and neither is a position the author did not ask
 * for.
 *
 * The classification answers `ABSENT`, `PX`, `AUTO` and every unusable length (a
 * percentage, a relative unit, an out-of-range magnitude) in one call, which is why
 * there is no second vocabulary consulted here -- unlike the border reader, whose
 * property has keywords that name lengths.
 */
static robo_layout_inset_t layout_inset_of_property(const robo_css_computed_style_t *style,
                                                    const char *property)
{
    layout_style_value_kind_t kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    if (!robo_layout_style_classify(style, property, 1, &kind, &number)) {
        /* A NULL style or property cannot happen here -- the caller has one by
         * construction -- but the classification refuses it, and a refusal is an
         * absence rather than a length. */
        return robo_layout_inset_absent();
    }

    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded through 10.8's door, half away from zero -- and the direction matters
         * here where it did not for a padding or a border: a negative inset is legal, so
         * `-1.5px` is -2 rather than 2. */
        return robo_layout_inset_resolved(robo_layout_px_round(number));
    case LAYOUT_STYLE_VALUE_AUTO:
        return robo_layout_inset_auto();
    case LAYOUT_STYLE_VALUE_ABSENT:
        return robo_layout_inset_absent();
    case LAYOUT_STYLE_VALUE_UNUSABLE:
    default:
        break;
    }
    return robo_layout_inset_unresolved();
}

/** Read one side. */
static robo_layout_inset_t layout_inset_of(const robo_css_computed_style_t *style,
                                           size_t index)
{
    return layout_inset_of_property(style, layout_inset_property[index]);
}

robo_layout_insets_t robo_layout_insets_of(const robo_css_computed_style_t *style)
{
    robo_layout_insets_t insets;

    insets.top = layout_inset_of(style, ROBO_CSS_BOX_SIDE_TOP);
    insets.right = layout_inset_of(style, ROBO_CSS_BOX_SIDE_RIGHT);
    insets.bottom = layout_inset_of(style, ROBO_CSS_BOX_SIDE_BOTTOM);
    insets.left = layout_inset_of(style, ROBO_CSS_BOX_SIDE_LEFT);
    return insets;
}

robo_result_t robo_layout_insets_side(const robo_layout_insets_t *insets,
                                      robo_css_box_side_t side,
                                      robo_layout_inset_t *out)
{
    if (!insets || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_css_box_side_is_known(side)) {
        return ROBO_NOT_FOUND;
    }
    switch (side) {
    case ROBO_CSS_BOX_SIDE_TOP:
        *out = insets->top;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_RIGHT:
        *out = insets->right;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_BOTTOM:
        *out = insets->bottom;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_LEFT:
    default:
        *out = insets->left;
        return ROBO_OK;
    }
}

int robo_layout_insets_equal(const robo_layout_insets_t *a, const robo_layout_insets_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return robo_layout_inset_equal(&a->top, &b->top) &&
           robo_layout_inset_equal(&a->right, &b->right) &&
           robo_layout_inset_equal(&a->bottom, &b->bottom) &&
           robo_layout_inset_equal(&a->left, &b->left);
}

int robo_layout_insets_is_valid(const robo_layout_insets_t *insets)
{
    if (!insets) {
        return 0;
    }
    return robo_layout_inset_is_valid(&insets->top) &&
           robo_layout_inset_is_valid(&insets->right) &&
           robo_layout_inset_is_valid(&insets->bottom) &&
           robo_layout_inset_is_valid(&insets->left);
}

/* ------------------------------------------------------------------ */
/* The declared position                                               */
/* ------------------------------------------------------------------ */

void robo_layout_position_init(robo_layout_position_t *position)
{
    if (!position) {
        return;
    }
    position->mode = ROBO_CSS_POSITIONING_STATIC;
    robo_layout_insets_init(&position->insets);
}

robo_layout_position_t robo_layout_position_of(const robo_css_computed_style_t *style)
{
    robo_layout_position_t position;

    robo_layout_position_init(&position);
    if (!style) {
        return position;
    }

    /*
     * The mode first, and the kind check is not optional: 9.13's accessor answers
     * `static` for any other kind, and `static` is both a real value and the property's
     * initial value, so reading it without the check would report "this box is not
     * positioned" where the truth might be "this is not a position property at all".
     * An absent longhand answers `static` too, and there it *is* the initial value
     * rather than a confusion.
     */
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text("position"));

        if (value && value->kind == ROBO_CSS_COMPUTED_VALUE_POSITION) {
            robo_css_positioning_t declared = robo_css_computed_value_position(value);

            if (robo_css_positioning_is_known(declared)) {
                position.mode = declared;
            }
        }
    }
    position.insets = robo_layout_insets_of(style);
    return position;
}

int robo_layout_position_is_relative(const robo_layout_position_t *position)
{
    return position && position->mode == ROBO_CSS_POSITIONING_RELATIVE;
}

int robo_layout_position_is_valid(const robo_layout_position_t *position)
{
    if (!position) {
        return 0;
    }
    if (!robo_css_positioning_is_known(position->mode)) {
        return 0;
    }
    return robo_layout_insets_is_valid(&position->insets);
}

int robo_layout_position_equal(const robo_layout_position_t *a,
                               const robo_layout_position_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->mode == b->mode && robo_layout_insets_equal(&a->insets, &b->insets);
}

/* ------------------------------------------------------------------ */
/* The used offset                                                     */
/* ------------------------------------------------------------------ */

void robo_layout_position_offset_init(robo_layout_position_offset_t *offset)
{
    if (!offset) {
        return;
    }
    offset->dx = 0;
    offset->dy = 0;
    offset->insets_resolved = 0u;
    offset->insets_auto = 0u;
    offset->insets_unresolved = 0u;
    offset->insets_declined = 0u;
}

int robo_layout_position_offset_is_valid(const robo_layout_position_offset_t *offset)
{
    if (!offset) {
        return 0;
    }
    /*
     * The four counters cannot outrun four sides, and the sum is the check rather than
     * each field: the record's whole claim is that it accounts for the declarations it
     * was given, and a hand-built record that counted five sides across four kinds would
     * be one no resolve produced.
     */
    if (offset->insets_resolved + offset->insets_auto + offset->insets_unresolved +
            offset->insets_declined >
        (size_t)ROBO_CSS_BOX_SIDE_COUNT) {
        return 0;
    }
    return 1;
}

int robo_layout_position_offset_equal(const robo_layout_position_offset_t *a,
                                      const robo_layout_position_offset_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    /* The displacement first, then the counters -- and the counters are compared
     * because two resolutions that moved a box the same way while disagreeing about how
     * many declarations they declined are two answers to one document. */
    return a->dx == b->dx && a->dy == b->dy &&
           a->insets_resolved == b->insets_resolved &&
           a->insets_auto == b->insets_auto &&
           a->insets_unresolved == b->insets_unresolved &&
           a->insets_declined == b->insets_declined;
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/**
 * Count one declared side into @p offset, and report whether it was a usable length.
 *
 * Split out so the four sides are counted by one switch rather than four, and so that
 * the reason a side did or did not contribute is written once. `*out_length` is filled
 * only for a `RESOLVED` side, which is what lets the caller tell "the author wrote
 * `0px`" from "the author wrote nothing" without a second look at the kind.
 *
 * @return 1 when the side is a usable length the caller may use for its axis.
 */
static int layout_position_count_inset(const robo_layout_inset_t *inset,
                                       robo_layout_position_offset_t *offset,
                                       robo_gfx_pixel_x_t *out_length)
{
    switch (inset->kind) {
    case ROBO_LAYOUT_INSET_RESOLVED:
        offset->insets_resolved++;
        *out_length = inset->length;
        return 1;
    case ROBO_LAYOUT_INSET_AUTO:
        offset->insets_auto++;
        return 0;
    case ROBO_LAYOUT_INSET_UNRESOLVED:
        offset->insets_unresolved++;
        return 0;
    case ROBO_LAYOUT_INSET_ABSENT:
    default:
        /* Nobody declared this side: CSS's initial value applies, and it is counted
         * nowhere -- the distinction 10.9 added a kind for. */
        return 0;
    }
}

robo_result_t robo_layout_position_resolve(const robo_layout_position_t *position,
                                           robo_layout_position_offset_t *out,
                                           robo_layout_diag_t *diag)
{
    robo_layout_position_offset_t offset;
    size_t index;
    const robo_layout_inset_t *declared[ROBO_CSS_BOX_SIDE_COUNT];

    if (!position || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_POSITION, ROBO_INVALID_ARGUMENT,
                                "resolving a position needs a record and an out");
    }

    robo_layout_position_offset_init(&offset);
    declared[ROBO_CSS_BOX_SIDE_TOP] = &position->insets.top;
    declared[ROBO_CSS_BOX_SIDE_RIGHT] = &position->insets.right;
    declared[ROBO_CSS_BOX_SIDE_BOTTOM] = &position->insets.bottom;
    declared[ROBO_CSS_BOX_SIDE_LEFT] = &position->insets.left;

    /*
     * Rule 1: a mode that is not `relative` produces no displacement and *declines*
     * every declared side. `static` reaches this branch because CSS does not apply an
     * offset to one, and `absolute`, `fixed` and `sticky` reach it because placing a box
     * against a containing block or a scrollport is 18.5's. The counter is the same on
     * both paths and the mode is what tells a reader which of the two documents this is.
     */
    if (!robo_layout_position_is_relative(position)) {
        for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
            if (declared[index]->kind != ROBO_LAYOUT_INSET_ABSENT) {
                offset.insets_declined++;
            }
        }
        *out = offset;
        return ROBO_OK;
    }

    /*
     * Rule 2 and 3: the two axes, each by the precedence rule -- the "start" side first,
     * and the "end" side only when the start one said nothing. `left` winning over
     * `right` and `top` over `bottom` is one sentence of behaviour rather than CSS's
     * direction-aware rule, and it is stated at `RoboLayoutPosition.h` so the phase that
     * replaces it changes this function and nothing else.
     *
     * The counters are accumulated for *every* side before either axis is decided, so a
     * `left` and a `right` that both resolve are both counted even though only `left`
     * contributes -- the record's counters are about declarations that reached this
     * layer, and precedence is a separate fact about which of two the displacement uses.
     */
    {
        robo_gfx_pixel_x_t left = 0;
        robo_gfx_pixel_x_t right = 0;
        robo_gfx_pixel_x_t top = 0;
        robo_gfx_pixel_x_t bottom = 0;
        int have_left = layout_position_count_inset(declared[ROBO_CSS_BOX_SIDE_LEFT],
                                                    &offset, &left);
        int have_right = layout_position_count_inset(declared[ROBO_CSS_BOX_SIDE_RIGHT],
                                                     &offset, &right);
        int have_top = layout_position_count_inset(declared[ROBO_CSS_BOX_SIDE_TOP],
                                                   &offset, &top);
        int have_bottom = layout_position_count_inset(declared[ROBO_CSS_BOX_SIDE_BOTTOM],
                                                      &offset, &bottom);

        if (have_left) {
            offset.dx = left;
        } else if (have_right) {
            /* `right` is a distance from the *right* edge, so it displaces leftward --
             * the sign flip the header names as the one place a reader can get this
             * wrong. */
            offset.dx = (robo_gfx_pixel_x_t)(-(int64_t)right);
        }
        if (have_top) {
            offset.dy = top;
        } else if (have_bottom) {
            offset.dy = (robo_gfx_pixel_y_t)(-(int64_t)bottom);
        }
    }

    *out = offset;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The two coordinate spaces                                           */
/* ------------------------------------------------------------------ */

/** @p value saturated into the geometry's range, in 64 bits. */
static int64_t layout_position_clamp(int64_t value)
{
    if (value > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (value < -(int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return -(int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    return value;
}

robo_result_t robo_layout_position_apply(robo_layout_point_t flow,
                                         const robo_layout_position_offset_t *offset,
                                         robo_layout_point_t *out,
                                         robo_layout_diag_t *diag)
{
    if (!offset || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_POSITION, ROBO_INVALID_ARGUMENT,
                                "applying a position needs an offset and an out");
    }
    if (!robo_layout_position_offset_is_valid(offset)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_POSITION, ROBO_ERROR,
                                "an offset record no position can be derived from");
    }

    /*
     * In 64 bits and clamped, for the reason the two inset layers sum a size that way:
     * the flow position and the displacement are each bounded by the seam's maximum and
     * their sum is not, so a box already at the extreme displaced by an extreme inset
     * must saturate rather than wrap into a coordinate on the other side of the screen.
     * Clamped rather than refused, because there is no document an author can write that
     * some coordinate cannot describe.
     */
    out->x = (robo_gfx_pixel_x_t)layout_position_clamp((int64_t)flow.x + (int64_t)offset->dx);
    out->y = (robo_gfx_pixel_y_t)layout_position_clamp((int64_t)flow.y + (int64_t)offset->dy);
    return ROBO_OK;
}
