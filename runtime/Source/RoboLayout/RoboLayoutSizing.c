/*
 * RoboLayout sizing (Phase 10.8) — see RoboLayoutSizing.h.
 *
 * Two readers over one classification, and the classification is the file's centre.
 * `layout_style_read_px` asks a computed style one question -- *what does this
 * declaration hold?* -- and answers with a kind and a raw number. Both readers then
 * shape that answer:
 *
 *   `robo_layout_style_px_length` wants only "is it a usable px length, and how
 *   many?" because that is all `font-size` needs (RoboLayoutText.c), and a font
 *   size is not a box dimension: `12.5px` has to stay `12.5`.
 *
 *   `robo_layout_sizing_read_dimension` wants the whole classification, because
 *   `auto` and `50%` resolve the *same way today* and differently tomorrow, and
 *   folding them here is what would make that change a rewrite.
 *
 * One classification and two shapes is the same arrangement 10.3 arrived at for its
 * property reader and 10.7 for its baseline rules: a fact computed once and read
 * twice, so the day the fact changes there is one place to change it.
 *
 * The resolver is deliberately *not* a cache. Everything below reads the record as
 * it stands and writes the answer into it, so resolving twice is legal and the
 * second answer is a function of the second input. That is the whole of the
 * re-layout story this phase promises, and it is a property of the shape rather
 * than a feature that had to be added.
 */

#include "RoboLayout/RoboLayoutSizing.h"

#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSUnit.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* A `double` that fits the geometry: `robo_gfx_pixel_x_t` is int32_t, and a
 * declared length of `3e9px` or a NaN is not a size a box can have. */
#define LAYOUT_SIZING_LENGTH_MAX 2147483647.0

/* ------------------------------------------------------------------ */
/* The shared classification                                           */
/* ------------------------------------------------------------------ */

/**
 * What a computed style's declaration holds, before anybody decides what to do
 * with it.
 *
 * Four answers rather than a boolean, because "nothing there", "the keyword
 * `auto`" and "a percentage" are three different facts and the two readers below
 * need different subsets of them. `ABSENT` and `AUTO` resolve identically
 * *everywhere* -- CSS's initial value for `width` and `height` is `auto`, so an
 * element nothing declared one for is `auto` -- but they are kept apart because
 * the day a UA stylesheet (a later goal) gives an element a declared `auto`, this
 * is the seam that can see the difference.
 */
typedef enum layout_style_value_kind {
    /** No entry, or an entry that determines nothing at all. */
    LAYOUT_STYLE_VALUE_ABSENT = 0,
    /** The keyword `auto`. */
    LAYOUT_STYLE_VALUE_AUTO = 1,
    /** A usable px length, in @c out_number. */
    LAYOUT_STYLE_VALUE_PX = 2,
    /** Declared, and not usable here: another unit, a percentage, or out of range. */
    LAYOUT_STYLE_VALUE_UNUSABLE = 3
} layout_style_value_kind_t;

/**
 * Classify what @p style declares for @p property.
 *
 * The four checks 10.3's version of this rule wrote, and they are four for the same
 * reasons:
 *
 *   the entry must exist. `robo_css_computed_style_computed_by_name` returns NULL
 *   for an absent property, and that is `ABSENT` rather than a failure.
 *
 *   a `KEYWORD` is `auto` when it spells `auto` and `UNUSABLE` otherwise: no other
 *   keyword names a length, so `width: inherit` reaching here as a keyword is a value
 *   this build cannot use rather than a value it may reinterpret.
 *
 *   a `LENGTH` in `px`, finite, non-negative and inside the geometry's range is
 *   usable. 9.8's computed rule normalises an *absolute* unit to px exactly, so
 *   `1in` arrives as `96`; a relative unit is not resolvable here.
 *
 *   anything else -- a percentage, a bare number, a colour -- is `UNUSABLE`.
 *
 * `unresolved` is honoured where the computed layer sets it: a `var()` or a `calc()`
 * is carried intact and is not a length whatever its kind says.
 *
 * @return 1 when the question could be asked; 0 for a NULL @p style or @p property,
 *         which leaves both outputs untouched. That asymmetry is the contract the
 *         public door documents: a box with no style has made no declarations, and
 *         its caller's initialised record already says so.
 */
static int layout_style_read_px(const robo_css_computed_style_t *style,
                                const char *property,
                                layout_style_value_kind_t *out_kind,
                                double *out_number)
{
    const robo_css_computed_value_t *value;

    if (!style || !property) {
        return 0;
    }
    value = robo_css_computed_style_computed_by_name(style,
                                                     robo_css_slice_from_text(property));
    if (!value || value->kind == ROBO_CSS_COMPUTED_VALUE_NONE) {
        *out_kind = LAYOUT_STYLE_VALUE_ABSENT;
        *out_number = 0.0;
        return 1;
    }
    if (value->kind == ROBO_CSS_COMPUTED_VALUE_KEYWORD) {
        robo_css_slice_t text = robo_css_computed_value_text(value);

        *out_kind = robo_css_slice_equals_ascii_ci(text, "auto") ? LAYOUT_STYLE_VALUE_AUTO
                                                                : LAYOUT_STYLE_VALUE_UNUSABLE;
        *out_number = 0.0;
        return 1;
    }
    if (value->kind == ROBO_CSS_COMPUTED_VALUE_LENGTH && value->unit == ROBO_CSS_UNIT_PX &&
        !value->unresolved) {
        /*
         * Deliberately written as a negated pair so that a NaN -- which compares
         * false against everything -- fails the first test rather than slipping
         * through a `number < 0` check.
         */
        if ((value->number >= 0.0) && (value->number <= LAYOUT_SIZING_LENGTH_MAX)) {
            *out_kind = LAYOUT_STYLE_VALUE_PX;
            *out_number = value->number;
            return 1;
        }
    }
    *out_kind = LAYOUT_STYLE_VALUE_UNUSABLE;
    *out_number = 0.0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The dimension                                                       */
/* ------------------------------------------------------------------ */

int robo_layout_dimension_kind_is_known(robo_layout_dimension_kind_t kind)
{
    return kind == ROBO_LAYOUT_DIMENSION_AUTO || kind == ROBO_LAYOUT_DIMENSION_DEFINITE ||
           kind == ROBO_LAYOUT_DIMENSION_UNRESOLVED;
}

const char *robo_layout_dimension_kind_name(robo_layout_dimension_kind_t kind)
{
    switch (kind) {
    case ROBO_LAYOUT_DIMENSION_AUTO:
        return "auto";
    case ROBO_LAYOUT_DIMENSION_DEFINITE:
        return "definite";
    case ROBO_LAYOUT_DIMENSION_UNRESOLVED:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

void robo_layout_dimension_init(robo_layout_dimension_t *dimension)
{
    if (!dimension) {
        return;
    }
    /* The cleared state is `auto` and not a zero length, which is the whole point
     * of the kind: a caller that read a cleared record's `length` would read 0, and
     * 0 is a *size* an author can ask for. */
    dimension->kind = ROBO_LAYOUT_DIMENSION_AUTO;
    dimension->length = 0;
}

robo_layout_dimension_t robo_layout_dimension_auto(void)
{
    robo_layout_dimension_t dimension;

    robo_layout_dimension_init(&dimension);
    return dimension;
}

robo_layout_dimension_t robo_layout_dimension_definite(robo_gfx_pixel_x_t length)
{
    robo_layout_dimension_t dimension;

    dimension.kind = ROBO_LAYOUT_DIMENSION_DEFINITE;
    dimension.length = length;
    return dimension;
}

robo_layout_dimension_t robo_layout_dimension_unresolved(void)
{
    robo_layout_dimension_t dimension;

    dimension.kind = ROBO_LAYOUT_DIMENSION_UNRESOLVED;
    dimension.length = 0;
    return dimension;
}

int robo_layout_dimension_is_auto(const robo_layout_dimension_t *dimension)
{
    return dimension && dimension->kind == ROBO_LAYOUT_DIMENSION_AUTO;
}

int robo_layout_dimension_is_definite(const robo_layout_dimension_t *dimension)
{
    return dimension && dimension->kind == ROBO_LAYOUT_DIMENSION_DEFINITE;
}

int robo_layout_dimension_is_unresolved(const robo_layout_dimension_t *dimension)
{
    return dimension && dimension->kind == ROBO_LAYOUT_DIMENSION_UNRESOLVED;
}

int robo_layout_dimension_is_valid(const robo_layout_dimension_t *dimension)
{
    if (!dimension) {
        return 0;
    }
    if (!robo_layout_dimension_kind_is_known(dimension->kind)) {
        return 0;
    }
    /* Only a length can be wrong: a non-negative px length is the whole of what
     * `DEFINITE` means, and the other two kinds carry no number at all. */
    return dimension->kind != ROBO_LAYOUT_DIMENSION_DEFINITE || dimension->length >= 0;
}

robo_result_t robo_layout_dimension_length(const robo_layout_dimension_t *dimension,
                                           robo_gfx_pixel_x_t *out)
{
    if (!dimension || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (dimension->kind != ROBO_LAYOUT_DIMENSION_DEFINITE) {
        /* A different answer from a length of zero, and the caller has to be able
         * to tell them apart: `width: 0` is a box an author asked to be empty. */
        return ROBO_NOT_FOUND;
    }
    *out = dimension->length;
    return ROBO_OK;
}

int robo_layout_dimension_equal(const robo_layout_dimension_t *a,
                                const robo_layout_dimension_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    /* The length is compared only where it means something, so two `auto`s built
     * by different paths -- one from `init`, one from `dimension_auto` -- are equal
     * rather than equal-by-luck. */
    if (a->kind != ROBO_LAYOUT_DIMENSION_DEFINITE) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* The source                                                          */
/* ------------------------------------------------------------------ */

int robo_layout_dimension_source_is_known(robo_layout_dimension_source_t source)
{
    return source >= ROBO_LAYOUT_DIMENSION_SOURCE_NONE &&
           source <= ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT;
}

const char *robo_layout_dimension_source_name(robo_layout_dimension_source_t source)
{
    switch (source) {
    case ROBO_LAYOUT_DIMENSION_SOURCE_NONE:
        return "none";
    case ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED:
        return "declared";
    case ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE:
        return "available";
    case ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT:
        return "content";
    default:
        break;
    }
    return "unknown";
}

int robo_layout_dimension_source_equal(robo_layout_dimension_source_t a,
                                       robo_layout_dimension_source_t b)
{
    return a == b;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void robo_layout_sizing_init(robo_layout_sizing_t *sizing)
{
    if (!sizing) {
        return;
    }
    memset(sizing, 0, sizeof(*sizing));
    /* A cleared record is a *legal* one: both dimensions `auto`, every availability
     * and bound unset, nothing resolved. That is the same choice 10.1 made for the
     * constraints record, and it is what makes `memset` the right implementation --
     * zero is `AUTO` and `SOURCE_NONE` by the enums' own order, so there is no
     * field a reader could find in a state the enum does not define. */
}

void robo_layout_sizing_clear(robo_layout_sizing_t *sizing)
{
    robo_layout_sizing_init(sizing);
}

robo_result_t robo_layout_sizing_set_available_width(robo_layout_sizing_t *sizing,
                                                     robo_gfx_pixel_x_t extent,
                                                     robo_layout_diag_t *diag)
{
    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (extent < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "available width is negative");
    }
    sizing->available.width = extent;
    sizing->has_available_width = 1;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_set_available_height(robo_layout_sizing_t *sizing,
                                                      robo_gfx_pixel_y_t extent,
                                                      robo_layout_diag_t *diag)
{
    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (extent < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "available height is negative");
    }
    sizing->available.height = extent;
    sizing->has_available_height = 1;
    return ROBO_OK;
}

int robo_layout_sizing_has_available_width(const robo_layout_sizing_t *sizing)
{
    return sizing && sizing->has_available_width;
}

int robo_layout_sizing_has_available_height(const robo_layout_sizing_t *sizing)
{
    return sizing && sizing->has_available_height;
}

robo_result_t robo_layout_sizing_available_width(const robo_layout_sizing_t *sizing,
                                                 robo_gfx_pixel_x_t *out)
{
    if (!sizing || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!sizing->has_available_width) {
        return ROBO_NOT_FOUND;
    }
    *out = sizing->available.width;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_available_height(const robo_layout_sizing_t *sizing,
                                                  robo_gfx_pixel_y_t *out)
{
    if (!sizing || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!sizing->has_available_height) {
        return ROBO_NOT_FOUND;
    }
    *out = sizing->available.height;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_set_width(robo_layout_sizing_t *sizing,
                                           robo_layout_dimension_t dimension,
                                           robo_layout_diag_t *diag)
{
    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (!robo_layout_dimension_is_valid(&dimension)) {
        /* Refused rather than clamped, for the reason 10.1 gives for a negative
         * available size: nothing in layout can mean anything by a negative width,
         * and this is the one moment the mistake is cheap to see. */
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "a declared width must be a known kind with a "
                                "non-negative length");
    }
    sizing->width = dimension;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_set_height(robo_layout_sizing_t *sizing,
                                            robo_layout_dimension_t dimension,
                                            robo_layout_diag_t *diag)
{
    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (!robo_layout_dimension_is_valid(&dimension)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "a declared height must be a known kind with a "
                                "non-negative length");
    }
    sizing->height = dimension;
    return ROBO_OK;
}

robo_layout_dimension_t robo_layout_sizing_width(const robo_layout_sizing_t *sizing)
{
    /* `auto` for NULL, which is the same answer the cleared record carries: "this
     * box has been said nothing about" and "there is no record" are one fact about a
     * dimension, and inventing a second answer for the second case would be a second
     * thing for a caller to handle. */
    return sizing ? sizing->width : robo_layout_dimension_auto();
}

robo_layout_dimension_t robo_layout_sizing_height(const robo_layout_sizing_t *sizing)
{
    return sizing ? sizing->height : robo_layout_dimension_auto();
}

robo_result_t robo_layout_sizing_set_minimum(robo_layout_sizing_t *sizing,
                                             robo_layout_size_t size,
                                             robo_layout_diag_t *diag)
{
    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "minimum size is negative");
    }
    sizing->minimum = size;
    sizing->has_minimum = 1;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_set_maximum(robo_layout_sizing_t *sizing,
                                             robo_layout_size_t size,
                                             robo_layout_diag_t *diag)
{
    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "maximum size is negative");
    }
    sizing->maximum = size;
    sizing->has_maximum = 1;
    return ROBO_OK;
}

int robo_layout_sizing_has_minimum(const robo_layout_sizing_t *sizing)
{
    return sizing && sizing->has_minimum;
}

int robo_layout_sizing_has_maximum(const robo_layout_sizing_t *sizing)
{
    return sizing && sizing->has_maximum;
}

robo_result_t robo_layout_sizing_minimum(const robo_layout_sizing_t *sizing,
                                         robo_layout_size_t *out)
{
    if (!sizing || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!sizing->has_minimum) {
        return ROBO_NOT_FOUND;
    }
    *out = sizing->minimum;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_maximum(const robo_layout_sizing_t *sizing,
                                         robo_layout_size_t *out)
{
    if (!sizing || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!sizing->has_maximum) {
        return ROBO_NOT_FOUND;
    }
    *out = sizing->maximum;
    return ROBO_OK;
}

int robo_layout_sizing_is_valid(const robo_layout_sizing_t *sizing)
{
    if (!sizing) {
        return 0;
    }
    if (!robo_layout_dimension_is_valid(&sizing->width) ||
        !robo_layout_dimension_is_valid(&sizing->height)) {
        return 0;
    }
    if (sizing->has_available_width && sizing->available.width < 0) {
        return 0;
    }
    if (sizing->has_available_height && sizing->available.height < 0) {
        return 0;
    }
    /* One bound is always consistent with itself, and "nothing is known" is a legal
     * state rather than an invalid one. Only a pair can contradict -- the same rule,
     * restated over this record's fields rather than reached through a conversion. */
    if (sizing->has_minimum && sizing->has_maximum) {
        if (sizing->minimum.width > sizing->maximum.width) {
            return 0;
        }
        if (sizing->minimum.height > sizing->maximum.height) {
            return 0;
        }
    }
    return 1;
}

robo_result_t robo_layout_sizing_set_content(robo_layout_sizing_t *sizing,
                                             robo_layout_size_t size,
                                             robo_layout_diag_t *diag)
{
    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "contents extent is negative");
    }
    sizing->content = size;
    sizing->has_content = 1;
    return ROBO_OK;
}

int robo_layout_sizing_has_content(const robo_layout_sizing_t *sizing)
{
    return sizing && sizing->has_content;
}

robo_result_t robo_layout_sizing_content(const robo_layout_sizing_t *sizing,
                                         robo_layout_size_t *out)
{
    if (!sizing || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!sizing->has_content) {
        return ROBO_NOT_FOUND;
    }
    *out = sizing->content;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Resolving                                                           */
/* ------------------------------------------------------------------ */

/**
 * The bounds, as 10.1's record holds them.
 *
 * A conversion rather than a second copy of the arithmetic, and that is the whole
 * point: `robo_layout_constraints_clamp` is where "min wins over max" is decided,
 * `RoboLayoutConstraints.h` promised this goal would reach it, and the two records
 * hold their bounds the same way precisely so that reaching it is a copy.
 */
static robo_result_t layout_sizing_bounds(const robo_layout_sizing_t *sizing,
                                          robo_layout_constraints_t *out,
                                          robo_layout_diag_t *diag)
{
    robo_layout_constraints_init(out);
    if (sizing->has_minimum) {
        robo_result_t status = robo_layout_constraints_set_minimum(out, sizing->minimum, diag);

        if (status != ROBO_OK) {
            return status;
        }
    }
    if (sizing->has_maximum) {
        return robo_layout_constraints_set_maximum(out, sizing->maximum, diag);
    }
    return ROBO_OK;
}

/**
 * Apply the bounds to one dimension's candidate and report whether they moved it.
 *
 * The *other* dimension is carried through the clamp as whatever the record holds,
 * and its result is discarded -- deliberately, because the alternative is a second
 * arithmetic that clamps one number and a header that would then have two places to
 * document. What the caller must not do is read the other field: each resolver only
 * ever writes and only ever flags its own.
 *
 * A pair of bounds that contradict each other is *not* resolved in either's favour:
 * `clamp` reports `ROBO_NOT_FOUND` and this function passes that through, which is
 * what the public header documents.
 */
static robo_result_t layout_sizing_clamp(const robo_layout_sizing_t *sizing,
                                        robo_layout_size_t candidate,
                                        robo_layout_size_t *out,
                                        robo_layout_diag_t *diag)
{
    robo_layout_constraints_t bounds;
    robo_result_t status;

    if (!sizing->has_minimum && !sizing->has_maximum) {
        *out = candidate;
        return ROBO_OK;
    }
    status = layout_sizing_bounds(sizing, &bounds, diag);
    if (status != ROBO_OK) {
        return status;
    }
    return robo_layout_constraints_clamp(&bounds, candidate, out, diag);
}

robo_result_t robo_layout_sizing_resolve_width(robo_layout_sizing_t *sizing,
                                               robo_layout_diag_t *diag)
{
    robo_layout_size_t candidate;
    robo_layout_size_t clamped;
    robo_layout_dimension_source_t source;
    robo_gfx_pixel_x_t base;
    robo_result_t status;

    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (!robo_layout_dimension_kind_is_known(sizing->width.kind)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "the declared width is not a dimension kind");
    }
    if (!robo_layout_dimension_is_valid(&sizing->width)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "the declared width is negative");
    }

    if (sizing->width.kind == ROBO_LAYOUT_DIMENSION_DEFINITE) {
        base = sizing->width.length;
        source = ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED;
    } else {
        /*
         * `AUTO` and `UNRESOLVED` take the same branch, and the header argues why
         * that is the only defensible answer *today*: a percentage needs a containing
         * dimension, and the containing block's content width is the best statement
         * this build can make about one. What keeps the decision honest is that the
         * *dimension* is still `UNRESOLVED` -- the merge happens here, per resolution,
         * rather than at the seam that read the declaration.
         *
         * The available width being unset is a failure and not a zero. A caller that
         * asked for a block's width without saying how wide the containing block is
         * has not given this layer an input it needs, and answering 0 would put a
         * zero-sized box in a document that has no reason to contain one.
         */
        if (!sizing->has_available_width) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_NOT_FOUND,
                                    "an auto width needs an available width, and none "
                                    "has been recorded");
        }
        base = sizing->available.width;
        source = ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE;
    }

    candidate.width = base;
    candidate.height = sizing->resolved.height;
    status = layout_sizing_clamp(sizing, candidate, &clamped, diag);
    if (status != ROBO_OK) {
        return status;
    }

    sizing->resolved.width = clamped.width;
    sizing->width_source = source;
    sizing->width_clamped = clamped.width != base;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_resolve_height(robo_layout_sizing_t *sizing,
                                                robo_layout_diag_t *diag)
{
    robo_layout_size_t candidate;
    robo_layout_size_t clamped;
    robo_layout_dimension_source_t source;
    robo_gfx_pixel_y_t base;
    robo_result_t status;

    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    if (!robo_layout_dimension_kind_is_known(sizing->height.kind)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "the declared height is not a dimension kind");
    }
    if (!robo_layout_dimension_is_valid(&sizing->height)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "the declared height is negative");
    }

    if (sizing->height.kind == ROBO_LAYOUT_DIMENSION_DEFINITE) {
        base = sizing->height.length;
        source = ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED;
    } else {
        /*
         * The mirror of the width's branch, and the asymmetry is the interesting
         * part: a block box's height is *never* its containing block's, so `auto`
         * takes the contents' extent rather than any available height. This build
         * indeed never sets an available height, which is why the flag exists -- a
         * later goal that gives percentage heights something to resolve against will
         * set it, and this branch is where that starts.
         *
         * A contents extent of zero is a *known* zero and resolves; no contents
         * extent at all is a failure, because a caller that has not laid the children
         * out yet has asked a question that has no answer yet.
         */
        if (!sizing->has_content) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_NOT_FOUND,
                                    "an auto height needs the contents' extent, and "
                                    "none has been recorded");
        }
        base = sizing->content.height;
        source = ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT;
    }

    candidate.width = sizing->resolved.width;
    candidate.height = base;
    status = layout_sizing_clamp(sizing, candidate, &clamped, diag);
    if (status != ROBO_OK) {
        return status;
    }

    sizing->resolved.height = clamped.height;
    sizing->height_source = source;
    sizing->height_clamped = clamped.height != base;
    return ROBO_OK;
}

robo_result_t robo_layout_sizing_resolve(robo_layout_sizing_t *sizing,
                                         robo_layout_diag_t *diag)
{
    robo_result_t status;

    if (!sizing) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_SIZING, ROBO_INVALID_ARGUMENT,
                                "sizing is NULL");
    }
    status = robo_layout_sizing_resolve_width(sizing, diag);
    if (status != ROBO_OK) {
        return status;
    }
    /*
     * The width first, deliberately, and not because the order matters to these two
     * numbers -- it does not, which is why the pair exists. It is so that a caller
     * reading the record after a *failed* height finds a width that really was
     * resolved, rather than a half-filled record whose first field is stale. The
     * half that succeeded is not rolled back: the record is a scratchpad the caller
     * is about to discard, and undoing a correct answer to make a failure look tidy
     * would hide which half went wrong.
     */
    return robo_layout_sizing_resolve_height(sizing, diag);
}

robo_result_t robo_layout_sizing_resolved(const robo_layout_sizing_t *sizing,
                                          robo_layout_size_t *out)
{
    if (!sizing || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    /*
     * "Resolved" is `width_source != NONE`, and that is what the source field is
     * *for*: adding a pair of `has_width` flags beside it would be two fields that
     * had to agree, and the day they disagreed the record would have two answers to
     * one question.
     */
    if (sizing->width_source == ROBO_LAYOUT_DIMENSION_SOURCE_NONE ||
        sizing->height_source == ROBO_LAYOUT_DIMENSION_SOURCE_NONE) {
        return ROBO_NOT_FOUND;
    }
    *out = sizing->resolved;
    return ROBO_OK;
}

robo_layout_dimension_source_t robo_layout_sizing_width_source(
    const robo_layout_sizing_t *sizing)
{
    return sizing ? sizing->width_source : ROBO_LAYOUT_DIMENSION_SOURCE_NONE;
}

robo_layout_dimension_source_t robo_layout_sizing_height_source(
    const robo_layout_sizing_t *sizing)
{
    return sizing ? sizing->height_source : ROBO_LAYOUT_DIMENSION_SOURCE_NONE;
}

int robo_layout_sizing_width_clamped(const robo_layout_sizing_t *sizing)
{
    return sizing && sizing->width_clamped;
}

int robo_layout_sizing_height_clamped(const robo_layout_sizing_t *sizing)
{
    return sizing && sizing->height_clamped;
}

int robo_layout_sizing_equal(const robo_layout_sizing_t *a,
                             const robo_layout_sizing_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->has_available_width != b->has_available_width ||
        a->has_available_height != b->has_available_height ||
        a->has_minimum != b->has_minimum || a->has_maximum != b->has_maximum ||
        a->has_content != b->has_content || a->width_clamped != b->width_clamped ||
        a->height_clamped != b->height_clamped) {
        return 0;
    }
    if (!robo_layout_dimension_equal(&a->width, &b->width) ||
        !robo_layout_dimension_equal(&a->height, &b->height)) {
        return 0;
    }
    if (a->width_source != b->width_source || a->height_source != b->height_source) {
        return 0;
    }
    /* Only the fields that are set are compared: an unset field's bytes are whatever
     * the caller's stack held, and comparing them would make two equal records
     * unequal -- the same rule 10.1's constraints equality follows. */
    if (a->has_available_width && !robo_gfx_size_equal(a->available, b->available)) {
        return 0;
    }
    if (a->has_minimum && !robo_gfx_size_equal(a->minimum, b->minimum)) {
        return 0;
    }
    if (a->has_maximum && !robo_gfx_size_equal(a->maximum, b->maximum)) {
        return 0;
    }
    if (a->has_content && !robo_gfx_size_equal(a->content, b->content)) {
        return 0;
    }
    /* The resolved pair is compared only when both dimensions have been resolved,
     * because until then `resolved` holds one field that means something and one that
     * does not. */
    if (a->width_source != ROBO_LAYOUT_DIMENSION_SOURCE_NONE &&
        a->height_source != ROBO_LAYOUT_DIMENSION_SOURCE_NONE) {
        return robo_gfx_size_equal(a->resolved, b->resolved);
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The CSS seam                                                        */
/* ------------------------------------------------------------------ */

int robo_layout_sizing_read_dimension(const robo_css_computed_style_t *style,
                                      const char *property,
                                      robo_layout_dimension_t *out)
{
    layout_style_value_kind_t kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    if (!out) {
        return 0;
    }
    if (!layout_style_read_px(style, property, &kind, &number)) {
        return 0;
    }

    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded here and nowhere else, and rounded at all rather than truncated:
         * `width: 10.5px` has to become one of two integers, and a build that
         * truncated would make every fractional declared width come out a pixel short
         * of what the author wrote. */
        *out = robo_layout_dimension_definite(robo_layout_px_round(number));
        return 1;
    case LAYOUT_STYLE_VALUE_AUTO:
    case LAYOUT_STYLE_VALUE_ABSENT:
        *out = robo_layout_dimension_auto();
        return 1;
    case LAYOUT_STYLE_VALUE_UNUSABLE:
    default:
        /* A percentage, a relative unit, or a keyword that names no length. Kept as
         * its own kind rather than folded into `auto`: it is a statement about the
         * document, and the resolution rule for it is the one that will change. */
        *out = robo_layout_dimension_unresolved();
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* The two readers 10.3 wrote, now sharing the classification above     */
/* ------------------------------------------------------------------ */

robo_gfx_pixel_x_t robo_layout_px_round(double value)
{
    return (robo_gfx_pixel_x_t)(value + 0.5);
}

int robo_layout_style_px_length(const robo_css_computed_style_t *style,
                                const char *property,
                                double *out_value)
{
    layout_style_value_kind_t kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    if (!out_value) {
        return 0;
    }
    if (!layout_style_read_px(style, property, &kind, &number)) {
        return 0;
    }
    if (kind != LAYOUT_STYLE_VALUE_PX) {
        /* `auto`, an absent entry, a percentage and a relative unit are all "no
         * usable px length" to this reader, which is exactly what 10.3's version
         * answered. The *classification* of them is above, for the reader that can
         * tell them apart. */
        return 0;
    }
    *out_value = number;
    return 1;
}
