/*
 * RoboLayout borders (Phase 10.11) — see RoboLayoutBorder.h.
 *
 * Three readers over the same computed style, and they are three because the three
 * properties have three genuinely different computed shapes:
 *
 *   a **width** is a length *or* one of three keywords (`thin`, `medium`, `thick`),
 *   and the shared classification folds every keyword but `auto` onto UNUSABLE --
 *   correctly, because no other property's keyword names a length. So this reader
 *   asks the classification first (which owns the length rule: px, finite,
 *   non-negative, inside the geometry's range) and consults the keyword vocabulary
 *   only when the classification says "declared, and not a length".
 *
 *   a **style** is a typed enumerator (9.15 kept the vocabulary rather than
 *   resolving it), so this reader is a kind check and a read -- and it is the reader
 *   that decides whether any of the twelve widths is *used*.
 *
 *   a **colour** is a typed record (9.9), contextual when the author wrote
 *   `currentColor`, and carried here exactly as 9.9 left it: only a layer with an
 *   element can resolve one, and this one has a style rather than an element.
 *
 * The one rule this file adds to the module is the fourth clause of the resolve
 * policy:
 *
 *   a border's used width is ZERO unless its style generates a border
 *
 * which is CSS's own rule and the reason `robo_css_border_style_generates_border`
 * exists. Everything else is 10.10's arithmetic composed one layer out -- which is
 * the phase's whole shape, and the reason the two files read as a pair.
 *
 * No allocations, no state between calls, no cache: every function below is a
 * function of the record it was handed, which is what makes re-layout a matter of
 * calling the pass again.
 */

#include "RoboLayout/RoboLayoutBorder.h"

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* The width's kinds                                                   */
/* ------------------------------------------------------------------ */

int robo_layout_border_width_kind_is_known(robo_layout_border_width_kind_t kind)
{
    return kind == ROBO_LAYOUT_BORDER_WIDTH_ABSENT ||
           kind == ROBO_LAYOUT_BORDER_WIDTH_RESOLVED ||
           kind == ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED;
}

const char *robo_layout_border_width_kind_name(robo_layout_border_width_kind_t kind)
{
    switch (kind) {
    case ROBO_LAYOUT_BORDER_WIDTH_ABSENT:
        return "absent";
    case ROBO_LAYOUT_BORDER_WIDTH_RESOLVED:
        return "resolved";
    case ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

robo_layout_border_width_t robo_layout_border_width_zero(void)
{
    return robo_layout_border_width_resolved(0);
}

robo_layout_border_width_t robo_layout_border_width_absent(void)
{
    robo_layout_border_width_t width;

    width.kind = ROBO_LAYOUT_BORDER_WIDTH_ABSENT;
    width.length = 0;
    return width;
}

robo_layout_border_width_t robo_layout_border_width_resolved(robo_gfx_pixel_x_t length)
{
    robo_layout_border_width_t width;

    width.kind = ROBO_LAYOUT_BORDER_WIDTH_RESOLVED;
    width.length = length;
    return width;
}

robo_layout_border_width_t robo_layout_border_width_unresolved(void)
{
    robo_layout_border_width_t width;

    width.kind = ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED;
    width.length = 0;
    return width;
}

int robo_layout_border_width_is_resolved(const robo_layout_border_width_t *width)
{
    return width && width->kind == ROBO_LAYOUT_BORDER_WIDTH_RESOLVED;
}

int robo_layout_border_width_is_absent(const robo_layout_border_width_t *width)
{
    return width && width->kind == ROBO_LAYOUT_BORDER_WIDTH_ABSENT;
}

int robo_layout_border_width_is_unresolved(const robo_layout_border_width_t *width)
{
    return width && width->kind == ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED;
}

int robo_layout_border_width_is_valid(const robo_layout_border_width_t *width)
{
    if (!width || !robo_layout_border_width_kind_is_known(width->kind)) {
        return 0;
    }
    /*
     * The check this vocabulary has and the margin one does not: a border width is
     * never negative. `border-width: -10px` is invalid CSS, so a record holding one is
     * a record this module's own reader never produced -- which makes it worth stating
     * as a predicate rather than trusting, the same argument 10.10 made for padding.
     */
    if (width->kind == ROBO_LAYOUT_BORDER_WIDTH_RESOLVED && width->length < 0) {
        return 0;
    }
    return 1;
}

robo_result_t robo_layout_border_width_length(const robo_layout_border_width_t *width,
                                              robo_gfx_pixel_x_t *out)
{
    if (!width || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (width->kind != ROBO_LAYOUT_BORDER_WIDTH_RESOLVED) {
        return ROBO_NOT_FOUND;
    }
    *out = width->length;
    return ROBO_OK;
}

int robo_layout_border_width_equal(const robo_layout_border_width_t *a,
                                   const robo_layout_border_width_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    /* The length only where it means something: two `ABSENT` sides are equal whatever
     * their stale `length` fields hold, which is the same rule the kind exists for. */
    if (a->kind != ROBO_LAYOUT_BORDER_WIDTH_RESOLVED) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

robo_layout_border_side_t robo_layout_border_side_none(void)
{
    robo_layout_border_side_t side;

    side.width = robo_layout_border_width_absent();
    side.style = ROBO_CSS_BORDER_STYLE_NONE;
    side.color = robo_css_color_none();
    return side;
}

int robo_layout_border_side_is_valid(const robo_layout_border_side_t *side)
{
    if (!side) {
        return 0;
    }
    if (!robo_layout_border_width_is_valid(&side->width)) {
        return 0;
    }
    if (!robo_css_border_style_is_known(side->style)) {
        return 0;
    }
    return 1;
}

int robo_layout_border_side_generates(const robo_layout_border_side_t *side)
{
    if (!side) {
        return 0;
    }
    if (!robo_css_border_style_generates_border(side->style)) {
        return 0;
    }
    /* A width nobody declared or this build could not use is zero, and a zero-width
     * border draws nothing -- so "generates" is the conjunction of the style and a
     * positive declared width, and the used question is answered once, below. */
    return side->width.kind == ROBO_LAYOUT_BORDER_WIDTH_RESOLVED && side->width.length > 0;
}

int robo_layout_border_side_equal(const robo_layout_border_side_t *a,
                                  const robo_layout_border_side_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->style != b->style) {
        return 0;
    }
    if (!robo_layout_border_width_equal(&a->width, &b->width)) {
        return 0;
    }
    /*
     * The *whole* colour record, so a `currentColor` and a resolved colour that
     * happened to share channels are different values. 9.9 made the kind part of the
     * record for that reason and this comparison inherits it rather than re-deciding.
     */
    return robo_css_color_equals(a->color, b->color);
}

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/** The width longhand each side is read from, indexed by `robo_css_box_side_t`. */
static const char *const layout_border_width_property[ROBO_CSS_BOX_SIDE_COUNT] = {
    "border-top-width",
    "border-right-width",
    "border-bottom-width",
    "border-left-width"
};

/** The style longhand each side is read from. */
static const char *const layout_border_style_property[ROBO_CSS_BOX_SIDE_COUNT] = {
    "border-top-style",
    "border-right-style",
    "border-bottom-style",
    "border-left-style"
};

/** The colour longhand each side is read from. */
static const char *const layout_border_color_property[ROBO_CSS_BOX_SIDE_COUNT] = {
    "border-top-color",
    "border-right-color",
    "border-bottom-color",
    "border-left-color"
};

void robo_layout_borders_init(robo_layout_borders_t *borders)
{
    if (!borders) {
        return;
    }
    borders->top = robo_layout_border_side_none();
    borders->right = robo_layout_border_side_none();
    borders->bottom = robo_layout_border_side_none();
    borders->left = robo_layout_border_side_none();
}

/**
 * The declared width @p style holds for @p property.
 *
 * The classification first, then the keyword vocabulary, and the order is the point:
 * the classification *owns* the length rule -- px, finite, non-negative, inside the
 * geometry's range -- so this reader does not restate it, and it answers `ABSENT`,
 * `PX` and every unusable length (a percentage, a relative unit, a negative, an
 * out-of-range magnitude) before anything below runs.
 *
 * What it cannot answer is `thin`, `medium` and `thick`. The shared classification
 * folds every keyword but `auto` onto UNUSABLE, and that is right for the property it
 * was written for: no *box dimension* takes a keyword that names a length. A border
 * width does, so the vocabulary is consulted here, in the one reader that has a use
 * for it -- and only on the branch the classification already called unusable, which
 * is why a `KEYWORD` read costs nothing on the common path.
 *
 * `auto` reaches the keyword branch and falls through to `UNRESOLVED`, deliberately:
 * CSS's grammar has no `border-width: auto`, so a style carrying one has broken its
 * own grammar and the honest report is "declared, and not a value this build can use".
 */
static robo_layout_border_width_t layout_border_width_of_property(
    const robo_css_computed_style_t *style,
    const char *property)
{
    layout_style_value_kind_t kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;
    const robo_css_computed_value_t *value;
    robo_css_slice_t text;

    if (!robo_layout_style_classify(style, property, 0, &kind, &number)) {
        /* A NULL style or property cannot happen here -- the caller has one by
         * construction -- but the classification refuses it, and a refusal is an
         * absence rather than a length. */
        return robo_layout_border_width_absent();
    }

    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded through 10.8's door, half away from zero. A border width is never
         * negative by the time it gets here, so the direction is the only thing that
         * matters: `border-width: 1.5px` is 2, and `2.5px` is 3. */
        return robo_layout_border_width_resolved(robo_layout_px_round(number));
    case LAYOUT_STYLE_VALUE_ABSENT:
        return robo_layout_border_width_absent();
    case LAYOUT_STYLE_VALUE_AUTO:
    case LAYOUT_STYLE_VALUE_UNUSABLE:
    default:
        break;
    }

    value = robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text(property));
    if (value && value->kind == ROBO_CSS_COMPUTED_VALUE_KEYWORD) {
        text = robo_css_computed_value_text(value);
        if (robo_css_slice_equals_ascii_ci(text, "thin")) {
            return robo_layout_border_width_resolved(ROBO_LAYOUT_BORDER_THIN_PX);
        }
        if (robo_css_slice_equals_ascii_ci(text, "medium")) {
            return robo_layout_border_width_resolved(ROBO_LAYOUT_BORDER_MEDIUM_PX);
        }
        if (robo_css_slice_equals_ascii_ci(text, "thick")) {
            return robo_layout_border_width_resolved(ROBO_LAYOUT_BORDER_THICK_PX);
        }
    }
    return robo_layout_border_width_unresolved();
}

/**
 * The declared style @p style holds for @p property.
 *
 * A kind check and a read, and the kind check is not optional: 9.15's accessor answers
 * `none` for any other kind, and `none` is both a real style and the property's
 * initial value, so reading it without the check would report "this border does not
 * draw" where the truth is "this is not a border style at all". An absent longhand
 * answers `none` too, and there it is the initial value rather than a confusion.
 */
static robo_css_border_style_t layout_border_style_of_property(
    const robo_css_computed_style_t *style,
    const char *property)
{
    const robo_css_computed_value_t *value;

    if (!style || !property) {
        return ROBO_CSS_BORDER_STYLE_NONE;
    }
    value = robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text(property));
    if (value && value->kind == ROBO_CSS_COMPUTED_VALUE_BORDER_STYLE) {
        robo_css_border_style_t declared = robo_css_computed_value_border_style(value);

        if (robo_css_border_style_is_known(declared)) {
            return declared;
        }
    }
    return ROBO_CSS_BORDER_STYLE_NONE;
}

/**
 * The declared colour @p style holds for @p property.
 *
 * Carried exactly as 9.9 and 9.8 left it, `currentColor` included: this layer has a
 * style rather than an element, so a contextual colour stays contextual and the layer
 * that *does* have an element resolves it. Copying a channel set here would be
 * inventing a second colour system, which is the one thing the phase's brief forbids
 * outright.
 *
 * An absent longhand answers a `NONE` colour, which is "nobody declared one" rather
 * than `transparent`: CSS's initial border colour is `currentColor`, and reporting
 * `transparent` would tell a paint pass the author had asked for an invisible border.
 */
static robo_css_color_t layout_border_color_of_property(
    const robo_css_computed_style_t *style,
    const char *property)
{
    const robo_css_computed_value_t *value;

    if (!style || !property) {
        return robo_css_color_none();
    }
    value = robo_css_computed_style_computed_by_name(style, robo_css_slice_from_text(property));
    if (value && value->kind == ROBO_CSS_COMPUTED_VALUE_COLOR) {
        return robo_css_computed_value_color(value);
    }
    return robo_css_color_none();
}

/** Read one side's three declarations. */
static robo_layout_border_side_t layout_border_side_of(const robo_css_computed_style_t *style,
                                                      size_t index)
{
    robo_layout_border_side_t side;

    side.width = layout_border_width_of_property(style, layout_border_width_property[index]);
    side.style = layout_border_style_of_property(style, layout_border_style_property[index]);
    side.color = layout_border_color_of_property(style, layout_border_color_property[index]);
    return side;
}

robo_layout_borders_t robo_layout_borders_of(const robo_css_computed_style_t *style)
{
    robo_layout_borders_t borders;

    borders.top = layout_border_side_of(style, ROBO_CSS_BOX_SIDE_TOP);
    borders.right = layout_border_side_of(style, ROBO_CSS_BOX_SIDE_RIGHT);
    borders.bottom = layout_border_side_of(style, ROBO_CSS_BOX_SIDE_BOTTOM);
    borders.left = layout_border_side_of(style, ROBO_CSS_BOX_SIDE_LEFT);
    return borders;
}

robo_result_t robo_layout_borders_side(const robo_layout_borders_t *borders,
                                       robo_css_box_side_t side,
                                       robo_layout_border_side_t *out)
{
    if (!borders || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_css_box_side_is_known(side)) {
        return ROBO_NOT_FOUND;
    }
    switch (side) {
    case ROBO_CSS_BOX_SIDE_TOP:
        *out = borders->top;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_RIGHT:
        *out = borders->right;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_BOTTOM:
        *out = borders->bottom;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_LEFT:
    default:
        *out = borders->left;
        return ROBO_OK;
    }
}

int robo_layout_borders_equal(const robo_layout_borders_t *a,
                              const robo_layout_borders_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return robo_layout_border_side_equal(&a->top, &b->top) &&
           robo_layout_border_side_equal(&a->right, &b->right) &&
           robo_layout_border_side_equal(&a->bottom, &b->bottom) &&
           robo_layout_border_side_equal(&a->left, &b->left);
}

int robo_layout_borders_is_valid(const robo_layout_borders_t *borders)
{
    if (!borders) {
        return 0;
    }
    return robo_layout_border_side_is_valid(&borders->top) &&
           robo_layout_border_side_is_valid(&borders->right) &&
           robo_layout_border_side_is_valid(&borders->bottom) &&
           robo_layout_border_side_is_valid(&borders->left);
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

void robo_layout_border_edges_init(robo_layout_border_edges_t *edges)
{
    if (!edges) {
        return;
    }
    edges->top.width = 0;
    edges->top.style = ROBO_CSS_BORDER_STYLE_NONE;
    edges->top.color = robo_css_color_none();
    edges->right = edges->top;
    edges->bottom = edges->top;
    edges->left = edges->top;
    edges->widths_resolved = 0u;
    edges->widths_unresolved = 0u;
    edges->borders_drawn = 0u;
}

int robo_layout_border_edges_is_valid(const robo_layout_border_edges_t *edges)
{
    const robo_layout_border_used_side_t *sides[ROBO_CSS_BOX_SIDE_COUNT];
    size_t index;

    if (!edges) {
        return 0;
    }
    sides[ROBO_CSS_BOX_SIDE_TOP] = &edges->top;
    sides[ROBO_CSS_BOX_SIDE_RIGHT] = &edges->right;
    sides[ROBO_CSS_BOX_SIDE_BOTTOM] = &edges->bottom;
    sides[ROBO_CSS_BOX_SIDE_LEFT] = &edges->left;

    for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
        if (sides[index]->width < 0) {
            return 0;
        }
        if (!robo_css_border_style_is_known(sides[index]->style)) {
            return 0;
        }
        /*
         * The invariant the fourth resolve rule *is*: a used width above zero implies
         * the style draws. A record that violated it would be a border paint would
         * draw as nothing while geometry charged it room -- which is the exact failure
         * the counter below exists to make visible, and checking it here means a
         * hand-built record cannot produce one.
         */
        if (sides[index]->width > 0 &&
            !robo_css_border_style_generates_border(sides[index]->style)) {
            return 0;
        }
    }

    if (edges->widths_resolved + edges->widths_unresolved > ROBO_CSS_BOX_SIDE_COUNT) {
        return 0;
    }
    if (edges->borders_drawn > edges->widths_resolved) {
        return 0;
    }
    return 1;
}

int robo_layout_border_edges_equal(const robo_layout_border_edges_t *a,
                                   const robo_layout_border_edges_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->top.width != b->top.width || a->right.width != b->right.width ||
        a->bottom.width != b->bottom.width || a->left.width != b->left.width) {
        return 0;
    }
    if (a->top.style != b->top.style || a->right.style != b->right.style ||
        a->bottom.style != b->bottom.style || a->left.style != b->left.style) {
        return 0;
    }
    if (!robo_css_color_equals(a->top.color, b->top.color) ||
        !robo_css_color_equals(a->right.color, b->right.color) ||
        !robo_css_color_equals(a->bottom.color, b->bottom.color) ||
        !robo_css_color_equals(a->left.color, b->left.color)) {
        return 0;
    }
    return a->widths_resolved == b->widths_resolved &&
           a->widths_unresolved == b->widths_unresolved &&
           a->borders_drawn == b->borders_drawn;
}

robo_result_t robo_layout_border_edges_width(const robo_layout_border_edges_t *edges,
                                             robo_css_box_side_t side,
                                             robo_gfx_pixel_x_t *out)
{
    if (!edges || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    switch (side) {
    case ROBO_CSS_BOX_SIDE_TOP:
        *out = edges->top.width;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_RIGHT:
        *out = edges->right.width;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_BOTTOM:
        *out = edges->bottom.width;
        return ROBO_OK;
    case ROBO_CSS_BOX_SIDE_LEFT:
        *out = edges->left.width;
        return ROBO_OK;
    default:
        break;
    }
    return ROBO_INVALID_ARGUMENT;
}

/* ------------------------------------------------------------------ */
/* The two derived numbers                                             */
/* ------------------------------------------------------------------ */

/**
 * The one place a border record is refused, so both doors refuse it identically.
 *
 * A record with a negative side or counters that disagree with four sides is not one
 * this module's resolver can produce, so one arriving here is a caller bug -- and the
 * moment to see it is before a size is derived from it, which is why the refusal is a
 * diagnostic rather than a floor.
 */
static robo_result_t layout_border_edges_require(const robo_layout_border_edges_t *edges,
                                                robo_layout_diag_t *diag)
{
    if (!robo_layout_border_edges_is_valid(edges)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BORDER, ROBO_ERROR,
                                "a border record no size can be derived from");
    }
    return ROBO_OK;
}

robo_result_t robo_layout_border_edges_padding_box_width(const robo_layout_border_edges_t *edges,
                                                        int32_t containing_width,
                                                        int32_t *out,
                                                        robo_layout_diag_t *diag)
{
    int64_t available;
    robo_result_t status;

    if (!edges || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BORDER, ROBO_INVALID_ARGUMENT,
                                "reading a padding box width needs a record and an out");
    }
    status = layout_border_edges_require(edges, diag);
    if (status != ROBO_OK) {
        return status;
    }

    available = (int64_t)containing_width - (int64_t)edges->left.width -
                (int64_t)edges->right.width;
    /*
     * Floored, and floored *here* rather than at each term, which is the shape both
     * earlier layers have and for the same reason: one clamp, in one place, on the one
     * number a size arithmetic can act on. A containing block narrower than the box's
     * own borders leaves no room for a padding box, and "no room" is zero rather than a
     * negative width -- a negative available width would make every `auto` dimension in
     * the subtree depend on which of two impossible numbers a subtraction produced.
     */
    if (available < 0) {
        available = 0;
    }
    if (available > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        available = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    *out = (int32_t)available;
    return ROBO_OK;
}

robo_result_t robo_layout_border_edges_border_box(const robo_layout_border_edges_t *edges,
                                                  robo_layout_size_t padding_box,
                                                  robo_layout_size_t *out,
                                                  robo_layout_diag_t *diag)
{
    int64_t width;
    int64_t height;
    robo_result_t status;

    if (!edges || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BORDER, ROBO_INVALID_ARGUMENT,
                                "deriving a border box needs a record and an out");
    }
    if (padding_box.width < 0 || padding_box.height < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BORDER, ROBO_INVALID_ARGUMENT,
                                "a padding box is never negative");
    }
    status = layout_border_edges_require(edges, diag);
    if (status != ROBO_OK) {
        return status;
    }

    /*
     * In 64 bits and clamped, exactly as 10.10's door is and for the same reason one
     * layer in: the terms are each bounded by the seam's maximum and their sum is not,
     * so a `width: 2147483647px` box with a `border: 5px solid` must saturate rather
     * than wrap into a negative rectangle. The clamps compose because both saturate at
     * the same bound: a padding box that already saturated keeps the border it cannot
     * express rather than losing it.
     */
    width = (int64_t)padding_box.width + (int64_t)edges->left.width + (int64_t)edges->right.width;
    height = (int64_t)padding_box.height + (int64_t)edges->top.width + (int64_t)edges->bottom.width;

    if (width > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        width = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (height > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        height = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }

    out->width = (robo_gfx_pixel_x_t)width;
    out->height = (robo_gfx_pixel_y_t)height;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

robo_result_t robo_layout_border_resolve(const robo_layout_borders_t *borders,
                                         robo_layout_border_edges_t *out,
                                         robo_layout_diag_t *diag)
{
    const robo_layout_border_side_t *declared[ROBO_CSS_BOX_SIDE_COUNT];
    robo_layout_border_used_side_t *used[ROBO_CSS_BOX_SIDE_COUNT];
    robo_layout_border_edges_t edges;
    size_t index;

    if (!borders || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_BORDER, ROBO_INVALID_ARGUMENT,
                                "resolving borders needs a record and an out");
    }

    robo_layout_border_edges_init(&edges);
    declared[ROBO_CSS_BOX_SIDE_TOP] = &borders->top;
    declared[ROBO_CSS_BOX_SIDE_RIGHT] = &borders->right;
    declared[ROBO_CSS_BOX_SIDE_BOTTOM] = &borders->bottom;
    declared[ROBO_CSS_BOX_SIDE_LEFT] = &borders->left;
    used[ROBO_CSS_BOX_SIDE_TOP] = &edges.top;
    used[ROBO_CSS_BOX_SIDE_RIGHT] = &edges.right;
    used[ROBO_CSS_BOX_SIDE_BOTTOM] = &edges.bottom;
    used[ROBO_CSS_BOX_SIDE_LEFT] = &edges.left;

    for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
        const robo_layout_border_side_t *side = declared[index];

        /*
         * The style and the colour travel with the side whatever its width comes to, so
         * a paint pass reads one record and never re-reads a declaration to find out
         * whether it was one this build could use.
         */
        used[index]->style = side->style;
        used[index]->color = side->color;

        switch (side->width.kind) {
        case ROBO_LAYOUT_BORDER_WIDTH_ABSENT:
            /* Nobody declared this side: CSS's initial value is `medium`, this build
             * uses zero, and it is counted nowhere -- the distinction 10.9 added a
             * kind for. */
            used[index]->width = 0;
            break;
        case ROBO_LAYOUT_BORDER_WIDTH_RESOLVED:
            edges.widths_resolved++;
            /*
             * And the phase's one new rule: a width is *used* only when the style
             * draws. `border: 5px none` is a declared five and a used zero, and a
             * build that used the declaration would charge a box ten pixels of width
             * for a border nothing paints -- which is the most plausible way to get
             * this phase wrong, and the reason the declaration and the used value are
             * different records.
             */
            if (robo_css_border_style_generates_border(side->style)) {
                used[index]->width = side->width.length;
            } else {
                used[index]->width = 0;
            }
            if (used[index]->width > 0) {
                edges.borders_drawn++;
            }
            break;
        case ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED:
        default:
            /* Declared, and not a value this build can use. Zero in geometry, counted
             * so that a document whose borders depend on a `calc()` is visible as one. */
            edges.widths_unresolved++;
            used[index]->width = 0;
            break;
        }
    }

    *out = edges;
    return ROBO_OK;
}
