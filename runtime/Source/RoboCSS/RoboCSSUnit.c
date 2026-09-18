/*
 * RoboCSS units and numbers (Phase 9.10) — see RoboCSSUnit.h.
 *
 * Four things live here and nothing else:
 *
 *   the set      the closed vocabulary, one table row per unit: its spelling,
 *                its family, its fixed factor to px when it has one, and — for
 *                a viewport unit — which viewport and which dimension it
 *                measures
 *   the context  a plain struct of numbers with a `has_` flag each, and the
 *                setters that are the only way to fill one
 *   resolve      the one place a relative length becomes pixels, as a *question*
 *                with four answers rather than a multiplication
 *   numeric      the four kinds, their constructors and equality, the parse that
 *                reads them out of a value tree, and one canonical spelling
 *
 * The unit table is the whole of the vocabulary, and every accessor is a lookup in
 * it. That is the property worth protecting: there is one row per unit, and adding
 * `lh` or `dvw` is adding a row rather than adding a branch to four functions.
 *
 * Nothing here uses math.h. The runtime is freestanding on the guest side, and
 * every function of it this file could want is one comparison: finiteness, minimum,
 * maximum and "is this whole" are all arithmetic the C language already has.
 */

#include "RoboCSS/RoboCSSUnit.h"

#include "RoboCSSInternal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The unit table                                                      */
/* ------------------------------------------------------------------ */

/** Which dimension of a viewport a unit measures. NONE for a non-viewport unit. */
typedef enum unit_viewport_measure {
    UNIT_VIEWPORT_NONE = 0,
    /** `vw`, and `vi` in a horizontal writing mode. */
    UNIT_VIEWPORT_WIDTH,
    /** `vh`, and `vb` in a horizontal writing mode. */
    UNIT_VIEWPORT_HEIGHT,
    UNIT_VIEWPORT_MIN,
    UNIT_VIEWPORT_MAX,
    /** `vi`: the inline axis, so width horizontally and height vertically. */
    UNIT_VIEWPORT_INLINE,
    /** `vb`: the block axis, so height horizontally and width vertically. */
    UNIT_VIEWPORT_BLOCK
} unit_viewport_measure_t;

/** One unit: everything any accessor needs to answer about it. */
typedef struct unit_entry {
    const char *name;
    robo_css_unit_t unit;
    robo_css_unit_class_t unit_class;
    /** Factor to px for an absolute unit; 0.0 when the unit has no fixed factor. */
    double to_px;
    /** Which viewport a viewport unit measures, and which of its dimensions. */
    robo_css_viewport_family_t family;
    unit_viewport_measure_t measure;
} unit_entry_t;

/*
 * The closed set.
 *
 * The px factors are CSS's own, and they are exact by definition rather than by
 * measurement: 1in is 96px, 1cm is 96/2.54px, 1mm is 1/10cm, 1Q is 1/40cm, 1pt is
 * 1/72in, 1pc is 12pt. Nothing here consults a display's DPI, which is the point —
 * an absolute length means the same thing everywhere, and only a *used* value (a
 * later phase's) may care about the device.
 *
 * The order is the enumerator order, so `robo_css_unit_name` and friends walk a
 * table whose index order matches the enum and a reader can check one against the
 * other by eye.
 */
static const unit_entry_t unit_entries[] = {
    /* absolute */
    { "px",    ROBO_CSS_UNIT_PX,    ROBO_CSS_UNIT_CLASS_ABSOLUTE, 1.0,            ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "cm",    ROBO_CSS_UNIT_CM,    ROBO_CSS_UNIT_CLASS_ABSOLUTE, 96.0 / 2.54,    ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "mm",    ROBO_CSS_UNIT_MM,    ROBO_CSS_UNIT_CLASS_ABSOLUTE, 96.0 / 25.4,    ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "q",     ROBO_CSS_UNIT_Q,     ROBO_CSS_UNIT_CLASS_ABSOLUTE, 96.0 / 101.6,   ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "in",    ROBO_CSS_UNIT_IN,    ROBO_CSS_UNIT_CLASS_ABSOLUTE, 96.0,           ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "pt",    ROBO_CSS_UNIT_PT,    ROBO_CSS_UNIT_CLASS_ABSOLUTE, 96.0 / 72.0,    ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "pc",    ROBO_CSS_UNIT_PC,    ROBO_CSS_UNIT_CLASS_ABSOLUTE, 16.0,           ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    /* font-relative */
    { "em",    ROBO_CSS_UNIT_EM,    ROBO_CSS_UNIT_CLASS_FONT_RELATIVE, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "rem",   ROBO_CSS_UNIT_REM,   ROBO_CSS_UNIT_CLASS_FONT_RELATIVE, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "ex",    ROBO_CSS_UNIT_EX,    ROBO_CSS_UNIT_CLASS_FONT_RELATIVE, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "ch",    ROBO_CSS_UNIT_CH,    ROBO_CSS_UNIT_CLASS_FONT_RELATIVE, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "lh",    ROBO_CSS_UNIT_LH,    ROBO_CSS_UNIT_CLASS_FONT_RELATIVE, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    { "rlh",   ROBO_CSS_UNIT_RLH,   ROBO_CSS_UNIT_CLASS_FONT_RELATIVE, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_NONE },
    /* viewport: the default family is the large viewport */
    { "vw",    ROBO_CSS_UNIT_VW,    ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_WIDTH },
    { "vh",    ROBO_CSS_UNIT_VH,    ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_HEIGHT },
    { "vmin",  ROBO_CSS_UNIT_VMIN,  ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_MIN },
    { "vmax",  ROBO_CSS_UNIT_VMAX,  ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_MAX },
    { "vi",    ROBO_CSS_UNIT_VI,    ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_INLINE },
    { "vb",    ROBO_CSS_UNIT_VB,    ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DEFAULT, UNIT_VIEWPORT_BLOCK },
    /* viewport: the small family */
    { "svw",   ROBO_CSS_UNIT_SVW,   ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_SMALL, UNIT_VIEWPORT_WIDTH },
    { "svh",   ROBO_CSS_UNIT_SVH,   ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_SMALL, UNIT_VIEWPORT_HEIGHT },
    { "svmin", ROBO_CSS_UNIT_SVMIN, ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_SMALL, UNIT_VIEWPORT_MIN },
    { "svmax", ROBO_CSS_UNIT_SVMAX, ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_SMALL, UNIT_VIEWPORT_MAX },
    /* viewport: the large family */
    { "lvw",   ROBO_CSS_UNIT_LVW,   ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_LARGE, UNIT_VIEWPORT_WIDTH },
    { "lvh",   ROBO_CSS_UNIT_LVH,   ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_LARGE, UNIT_VIEWPORT_HEIGHT },
    { "lvmin", ROBO_CSS_UNIT_LVMIN, ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_LARGE, UNIT_VIEWPORT_MIN },
    { "lvmax", ROBO_CSS_UNIT_LVMAX, ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_LARGE, UNIT_VIEWPORT_MAX },
    /* viewport: the dynamic family */
    { "dvw",   ROBO_CSS_UNIT_DVW,   ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DYNAMIC, UNIT_VIEWPORT_WIDTH },
    { "dvh",   ROBO_CSS_UNIT_DVH,   ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DYNAMIC, UNIT_VIEWPORT_HEIGHT },
    { "dvmin", ROBO_CSS_UNIT_DVMIN, ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DYNAMIC, UNIT_VIEWPORT_MIN },
    { "dvmax", ROBO_CSS_UNIT_DVMAX, ROBO_CSS_UNIT_CLASS_VIEWPORT, 0.0, ROBO_CSS_VIEWPORT_DYNAMIC, UNIT_VIEWPORT_MAX }
};

/** The row for @p unit, or NULL for NONE and for an unknown enumerator. */
static const unit_entry_t *unit_lookup(robo_css_unit_t unit)
{
    size_t index;

    if (unit == ROBO_CSS_UNIT_NONE) {
        return NULL;
    }
    for (index = 0u; index < sizeof(unit_entries) / sizeof(unit_entries[0]); index++) {
        if (unit_entries[index].unit == unit) {
            return &unit_entries[index];
        }
    }
    return NULL;
}

int robo_css_unit_is_known(robo_css_unit_t unit)
{
    return unit >= ROBO_CSS_UNIT_NONE && unit < (robo_css_unit_t)ROBO_CSS_UNIT_COUNT;
}

int robo_css_unit_is_absolute(robo_css_unit_t unit)
{
    /* Asked of the table, not of `to_px`: that function answers with the value it
     * was given when the unit has no fixed factor, so a relative unit and an
     * unknown one both come back looking convertible. The factor being present is
     * the fact this predicate is about. */
    const unit_entry_t *entry = unit_lookup(unit);

    return entry != NULL && entry->to_px != 0.0;
}

robo_css_unit_class_t robo_css_unit_class(robo_css_unit_t unit)
{
    const unit_entry_t *entry = unit_lookup(unit);

    return entry ? entry->unit_class : ROBO_CSS_UNIT_CLASS_NONE;
}

const char *robo_css_unit_class_name(robo_css_unit_class_t unit_class)
{
    switch (unit_class) {
    case ROBO_CSS_UNIT_CLASS_ABSOLUTE:
        return "absolute";
    case ROBO_CSS_UNIT_CLASS_FONT_RELATIVE:
        return "font-relative";
    case ROBO_CSS_UNIT_CLASS_VIEWPORT:
        return "viewport";
    default:
        return "none";
    }
}

int robo_css_unit_is_viewport(robo_css_unit_t unit)
{
    return robo_css_unit_class(unit) == ROBO_CSS_UNIT_CLASS_VIEWPORT;
}

robo_css_viewport_family_t robo_css_unit_viewport_family(robo_css_unit_t unit)
{
    const unit_entry_t *entry = unit_lookup(unit);

    return entry ? entry->family : ROBO_CSS_VIEWPORT_DEFAULT;
}

const char *robo_css_unit_name(robo_css_unit_t unit)
{
    const unit_entry_t *entry = unit_lookup(unit);

    return entry ? entry->name : "";
}

robo_css_unit_t robo_css_unit_from_slice(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name) || name.length == 0u) {
        return ROBO_CSS_UNIT_NONE;
    }
    for (index = 0u; index < sizeof(unit_entries) / sizeof(unit_entries[0]); index++) {
        if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length, unit_entries[index].name)) {
            return unit_entries[index].unit;
        }
    }
    return ROBO_CSS_UNIT_NONE;
}

robo_css_unit_t robo_css_unit_from_text(const char *name)
{
    if (!name) {
        return ROBO_CSS_UNIT_NONE;
    }
    return robo_css_unit_from_slice(robo_css_slice_from_text(name));
}

double robo_css_unit_to_px(double value, robo_css_unit_t unit)
{
    const unit_entry_t *entry = unit_lookup(unit);

    if (entry && entry->to_px != 0.0) {
        return value * entry->to_px;
    }
    return value;
}

/* ------------------------------------------------------------------ */
/* The resolution context                                              */
/* ------------------------------------------------------------------ */

/*
 * The setters exist rather than callers assigning fields, and the reason is the
 * pairing: every value has a `has_` flag that must be set with it, and a writer
 * that set one without the other would produce a context that resolves lengths
 * against numbers nobody supplied. A setter cannot forget.
 */

void robo_css_unit_context_init(robo_css_unit_context_t *context)
{
    if (context) {
        memset(context, 0, sizeof(*context));
    }
}

void robo_css_unit_context_set_font(robo_css_unit_context_t *context,
                                    double font_size,
                                    double x_height,
                                    double zero_width,
                                    double line_height)
{
    if (!context) {
        return;
    }
    context->has_font_metrics = 1;
    context->font_size = font_size;
    context->x_height = x_height;
    context->zero_width = zero_width;
    context->line_height = line_height;
}

void robo_css_unit_context_set_root_font(robo_css_unit_context_t *context,
                                         double root_font_size,
                                         double root_line_height)
{
    if (!context) {
        return;
    }
    context->has_root_font_metrics = 1;
    context->root_font_size = root_font_size;
    context->root_line_height = root_line_height;
}

void robo_css_unit_context_set_viewport(robo_css_unit_context_t *context,
                                        double width,
                                        double height)
{
    if (!context) {
        return;
    }
    context->has_viewport = 1;
    context->viewport_width = width;
    context->viewport_height = height;
}

void robo_css_unit_context_set_small_viewport(robo_css_unit_context_t *context,
                                              double width,
                                              double height)
{
    if (!context) {
        return;
    }
    context->has_small_viewport = 1;
    context->small_viewport_width = width;
    context->small_viewport_height = height;
}

void robo_css_unit_context_set_large_viewport(robo_css_unit_context_t *context,
                                              double width,
                                              double height)
{
    if (!context) {
        return;
    }
    context->has_large_viewport = 1;
    context->large_viewport_width = width;
    context->large_viewport_height = height;
}

void robo_css_unit_context_set_dynamic_viewport(robo_css_unit_context_t *context,
                                                double width,
                                                double height)
{
    if (!context) {
        return;
    }
    context->has_dynamic_viewport = 1;
    context->dynamic_viewport_width = width;
    context->dynamic_viewport_height = height;
}

/* ------------------------------------------------------------------ */
/* Contextual resolution                                               */
/* ------------------------------------------------------------------ */

int robo_css_length_resolution_is_known(robo_css_length_resolution_t resolution)
{
    return resolution >= ROBO_CSS_LENGTH_RESOLVED &&
           resolution < (robo_css_length_resolution_t)ROBO_CSS_LENGTH_RESOLUTION_COUNT;
}

const char *robo_css_length_resolution_name(robo_css_length_resolution_t resolution)
{
    switch (resolution) {
    case ROBO_CSS_LENGTH_RESOLVED:
        return "resolved";
    case ROBO_CSS_LENGTH_NEEDS_CONTEXT:
        return "needs-context";
    case ROBO_CSS_LENGTH_NOT_A_LENGTH:
        return "not-a-length";
    case ROBO_CSS_LENGTH_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

/** @p a and @p b, smaller first. Replaces fmin/fmax, which need math.h. */
static double unit_smaller(double a, double b)
{
    return a < b ? a : b;
}

static double unit_larger(double a, double b)
{
    return a > b ? a : b;
}

/**
 * The viewport size @p family measures, as two finite dimensions.
 *
 * The fallback chain is the design (see the header): a family with its own size
 * uses it, and a family without one uses the default, because a window whose
 * chrome never moves has one size and it is all four of them. Only the *default*
 * is required — a caller that set no viewport at all gets NEEDS_CONTEXT, and a
 * caller that set only the default gets a correct answer for all sixteen units.
 *
 * @return 1 and writes both outputs on success; 0 when the context has no viewport
 *         to offer, or when the size it offers is not finite.
 */
static int unit_viewport_size(const robo_css_unit_context_t *context,
                              robo_css_viewport_family_t family,
                              double *out_width,
                              double *out_height,
                              int *out_unusable)
{
    double width = 0.0;
    double height = 0.0;
    int have = 0;

    *out_unusable = 0;
    if (!context) {
        return 0;
    }
    /* The family's own size when it has one, and the default when it does not. An
     * if/else chain rather than a switch, because three of the four arms do the
     * same thing and a switch would say they do three different things. */
    if (family == ROBO_CSS_VIEWPORT_SMALL && context->has_small_viewport) {
        width = context->small_viewport_width;
        height = context->small_viewport_height;
        have = 1;
    } else if (family == ROBO_CSS_VIEWPORT_LARGE && context->has_large_viewport) {
        width = context->large_viewport_width;
        height = context->large_viewport_height;
        have = 1;
    } else if (family == ROBO_CSS_VIEWPORT_DYNAMIC && context->has_dynamic_viewport) {
        width = context->dynamic_viewport_width;
        height = context->dynamic_viewport_height;
        have = 1;
    }
    if (!have) {
        if (!context->has_viewport) {
            return 0;
        }
        width = context->viewport_width;
        height = context->viewport_height;
    }
    if (!robo_css_number_is_finite(width) || !robo_css_number_is_finite(height)) {
        /* An infinite viewport is not a size, and `50vw` of one is not a length.
         * Reported apart from "I do not know", because a caller can fix the
         * first and can only ask for the second. */
        *out_unusable = 1;
        return 0;
    }
    *out_width = width;
    *out_height = height;
    return 1;
}

/**
 * The one context number @p unit needs, or why there is not one.
 *
 * A switch over the font-relative units rather than a loop over the table,
 * because which *input* a unit needs is a fact about the unit that the table's
 * columns would have to grow a member to carry — and the member would be read in
 * exactly one place, which is here.
 *
 * @return 1 when @p out_value holds a finite number; 0 otherwise, with
 *         @p out_unusable saying whether the caller supplied a value that is not
 *         one (unusable) or supplied nothing (missing).
 */
static int unit_font_relative_input(const robo_css_unit_context_t *context,
                                    robo_css_unit_t unit,
                                    double *out_value,
                                    int *out_unusable)
{
    double value;

    *out_unusable = 0;
    if (!context) {
        return 0;
    }
    switch (unit) {
    case ROBO_CSS_UNIT_EM:
        if (!context->has_font_metrics) {
            return 0;
        }
        value = context->font_size;
        break;
    case ROBO_CSS_UNIT_EX:
        if (!context->has_font_metrics) {
            return 0;
        }
        value = context->x_height;
        break;
    case ROBO_CSS_UNIT_CH:
        if (!context->has_font_metrics) {
            return 0;
        }
        value = context->zero_width;
        break;
    case ROBO_CSS_UNIT_LH:
        if (!context->has_font_metrics) {
            return 0;
        }
        value = context->line_height;
        break;
    case ROBO_CSS_UNIT_REM:
        if (!context->has_root_font_metrics) {
            return 0;
        }
        value = context->root_font_size;
        break;
    case ROBO_CSS_UNIT_RLH:
        if (!context->has_root_font_metrics) {
            return 0;
        }
        value = context->root_line_height;
        break;
    default:
        return 0;
    }
    if (!robo_css_number_is_finite(value)) {
        *out_unusable = 1;
        return 0;
    }
    *out_value = value;
    return 1;
}

robo_css_length_resolution_t robo_css_unit_resolve_px(double value,
                                                      robo_css_unit_t unit,
                                                      const robo_css_unit_context_t *context,
                                                      double *out_px)
{
    const unit_entry_t *entry = unit_lookup(unit);
    double resolved;

    if (!entry) {
        return ROBO_CSS_LENGTH_NOT_A_LENGTH;
    }
    if (!robo_css_number_is_finite(value)) {
        return ROBO_CSS_LENGTH_INVALID;
    }

    switch (entry->unit_class) {
    case ROBO_CSS_UNIT_CLASS_ABSOLUTE:
        /* No context needed, and none consulted: an absolute length is a
         * definition rather than a measurement. */
        resolved = value * entry->to_px;
        break;
    case ROBO_CSS_UNIT_CLASS_FONT_RELATIVE: {
        double factor = 0.0;
        int unusable = 0;

        if (!unit_font_relative_input(context, unit, &factor, &unusable)) {
            return unusable ? ROBO_CSS_LENGTH_INVALID : ROBO_CSS_LENGTH_NEEDS_CONTEXT;
        }
        resolved = value * factor;
        break;
    }
    case ROBO_CSS_UNIT_CLASS_VIEWPORT: {
        double width = 0.0;
        double height = 0.0;
        double dimension;
        int unusable = 0;

        if (!unit_viewport_size(context, entry->family, &width, &height, &unusable)) {
            return unusable ? ROBO_CSS_LENGTH_INVALID : ROBO_CSS_LENGTH_NEEDS_CONTEXT;
        }
        switch (entry->measure) {
        case UNIT_VIEWPORT_WIDTH:
            dimension = width;
            break;
        case UNIT_VIEWPORT_HEIGHT:
            dimension = height;
            break;
        case UNIT_VIEWPORT_MIN:
            dimension = unit_smaller(width, height);
            break;
        case UNIT_VIEWPORT_MAX:
            dimension = unit_larger(width, height);
            break;
        case UNIT_VIEWPORT_INLINE:
            dimension = context->vertical_writing ? height : width;
            break;
        case UNIT_VIEWPORT_BLOCK:
            dimension = context->vertical_writing ? width : height;
            break;
        default:
            return ROBO_CSS_LENGTH_NOT_A_LENGTH;
        }
        resolved = value * (dimension / 100.0);
        break;
    }
    default:
        return ROBO_CSS_LENGTH_NOT_A_LENGTH;
    }

    if (!robo_css_number_is_finite(resolved)) {
        /* The multiplication overflowed: `1e308in` is a number and is not a
         * length, and an infinity that reached a layout would be worse than a
         * refusal. */
        return ROBO_CSS_LENGTH_INVALID;
    }
    if (out_px) {
        *out_px = resolved;
    }
    return ROBO_CSS_LENGTH_RESOLVED;
}

/* ------------------------------------------------------------------ */
/* The typed numeric value                                             */
/* ------------------------------------------------------------------ */

int robo_css_numeric_kind_is_known(robo_css_numeric_kind_t kind)
{
    return kind >= ROBO_CSS_NUMERIC_NONE &&
           kind < (robo_css_numeric_kind_t)ROBO_CSS_NUMERIC_KIND_COUNT;
}

const char *robo_css_numeric_kind_name(robo_css_numeric_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_NUMERIC_NUMBER:
        return "number";
    case ROBO_CSS_NUMERIC_INTEGER:
        return "integer";
    case ROBO_CSS_NUMERIC_PERCENTAGE:
        return "percentage";
    case ROBO_CSS_NUMERIC_LENGTH:
        return "length";
    default:
        return "none";
    }
}

robo_css_numeric_t robo_css_numeric_none(void)
{
    robo_css_numeric_t numeric;

    numeric.kind = ROBO_CSS_NUMERIC_NONE;
    numeric.number = 0.0;
    numeric.unit = ROBO_CSS_UNIT_NONE;
    return numeric;
}

robo_result_t robo_css_numeric_make_number(robo_css_numeric_t *out,
                                           double number,
                                           int is_integer,
                                           robo_css_diag_t *diag)
{
    if (!out) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "output is NULL");
    }
    if (!robo_css_number_is_finite(number)) {
        /* A programmer error rather than a parse outcome: the tokenizer cannot
         * produce one of these without the parser having rejected it, and a value
         * that cannot be written is not a value. */
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a numeric value must be finite");
    }
    *out = robo_css_numeric_none();
    out->kind = is_integer ? ROBO_CSS_NUMERIC_INTEGER : ROBO_CSS_NUMERIC_NUMBER;
    out->number = number;
    return ROBO_OK;
}

robo_result_t robo_css_numeric_make_percentage(robo_css_numeric_t *out,
                                               double percentage,
                                               robo_css_diag_t *diag)
{
    if (!out) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "output is NULL");
    }
    if (!robo_css_number_is_finite(percentage)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a numeric value must be finite");
    }
    *out = robo_css_numeric_none();
    out->kind = ROBO_CSS_NUMERIC_PERCENTAGE;
    out->number = percentage;
    return ROBO_OK;
}

robo_result_t robo_css_numeric_make_length(robo_css_numeric_t *out,
                                           double number,
                                           robo_css_unit_t unit,
                                           robo_css_diag_t *diag)
{
    if (!out) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "output is NULL");
    }
    if (!robo_css_number_is_finite(number)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a numeric value must be finite");
    }
    if (!robo_css_unit_is_known(unit) || unit == ROBO_CSS_UNIT_NONE) {
        /* A length with no unit is a number, and a caller that meant that should
         * say so rather than getting a length whose unit is nothing. */
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a length needs a known, non-empty unit");
    }
    *out = robo_css_numeric_none();
    out->kind = ROBO_CSS_NUMERIC_LENGTH;
    out->number = number;
    out->unit = unit;
    return ROBO_OK;
}

int robo_css_numeric_is_present(robo_css_numeric_t numeric)
{
    return numeric.kind != ROBO_CSS_NUMERIC_NONE;
}

int robo_css_numeric_is_integral(robo_css_numeric_t numeric)
{
    if (!robo_css_numeric_is_present(numeric)) {
        return 0;
    }
    /* The range check comes first, and that order is the whole of numeric safety
     * here: casting a double outside the integer range is undefined behaviour, so
     * a function that asked "is this whole" by casting and then checking would
     * have already invoked the bug it was trying to guard against. */
    if (!(numeric.number <= ROBO_CSS_NUMERIC_INTEGER_LIMIT &&
          numeric.number >= -ROBO_CSS_NUMERIC_INTEGER_LIMIT)) {
        /* Beyond 2^53 a double cannot represent every integer, so "is this whole"
         * has no reliable answer and the honest one is no. */
        return 0;
    }
    return (double)(int64_t)numeric.number == numeric.number;
}

int robo_css_numeric_integer_value(robo_css_numeric_t numeric, int64_t *out_integer)
{
    if (numeric.kind != ROBO_CSS_NUMERIC_INTEGER) {
        /* A NUMBER that happens to be whole is a different question's answer. */
        return 0;
    }
    if (numeric.number > ROBO_CSS_NUMERIC_INTEGER_LIMIT ||
        numeric.number < -ROBO_CSS_NUMERIC_INTEGER_LIMIT) {
        /* Refused rather than cast: the cast would be undefined behaviour, and
         * rounding would turn an out-of-range value into a plausible small one. */
        return 0;
    }
    if (out_integer) {
        *out_integer = (int64_t)numeric.number;
    }
    return 1;
}

int robo_css_numeric_equals(robo_css_numeric_t a, robo_css_numeric_t b)
{
    if (a.kind != b.kind) {
        /* `0` is not `0%` and `0px` is not `0`: the difference is what property
         * the value belongs to, and a comparison that ignored it is how a length
         * gets used as a proportion. */
        return 0;
    }
    if (a.kind == ROBO_CSS_NUMERIC_NONE) {
        return 1;
    }
    if (a.number != b.number) {
        return 0;
    }
    if (a.kind == ROBO_CSS_NUMERIC_LENGTH) {
        return a.unit == b.unit;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

int robo_css_numeric_parse_is_known(robo_css_numeric_parse_t outcome)
{
    return outcome >= ROBO_CSS_NUMERIC_PARSE_INVALID &&
           outcome < (robo_css_numeric_parse_t)ROBO_CSS_NUMERIC_PARSE_COUNT;
}

const char *robo_css_numeric_parse_name(robo_css_numeric_parse_t outcome)
{
    switch (outcome) {
    case ROBO_CSS_NUMERIC_PARSE_RESOLVED:
        return "resolved";
    case ROBO_CSS_NUMERIC_PARSE_DEFERRED:
        return "deferred";
    case ROBO_CSS_NUMERIC_PARSE_UNKNOWN_UNIT:
        return "unknown-unit";
    case ROBO_CSS_NUMERIC_PARSE_NON_FINITE:
        return "non-finite";
    default:
        return "invalid";
    }
}

/** Non-zero when @p value is the one function @p name, which is deferred. */
static int numeric_function_is_named(const robo_css_value_t *value, const char *name)
{
    return robo_css_value_function_named(value, name);
}

/**
 * Non-zero when @p value is a function whose evaluation this build defers.
 *
 * Five names, and they are one group because they are one thing: a *reference*.
 * `calc()` and its three relatives are arithmetic over lengths that needs a
 * containing block, and `var()` is a custom-property substitution; none of them is
 * a number yet, and each is legal CSS. Treating them as a mistake would be a lie
 * about the author, and evaluating them here would be a lie about the value.
 */
static int numeric_function_is_deferred(const robo_css_value_t *value)
{
    static const char *const deferred[] = { "calc", "min", "max", "clamp", "var", "env" };
    size_t index;

    for (index = 0u; index < sizeof(deferred) / sizeof(deferred[0]); index++) {
        if (numeric_function_is_named(value, deferred[index])) {
            return 1;
        }
    }
    return 0;
}

robo_css_numeric_parse_t robo_css_numeric_from_token(const robo_css_token_t *token,
                                                     robo_css_numeric_t *out)
{
    robo_css_numeric_t numeric = robo_css_numeric_none();

    if (!token || !out) {
        return ROBO_CSS_NUMERIC_PARSE_INVALID;
    }
    if (!robo_css_number_is_finite(token->number)) {
        /* Legal syntax, unrepresentable magnitude: `1e999` is a number as CSS
         * spells it and an infinity as a double holds it. Reported apart from
         * INVALID, because "your number is too big" is a fixable thing to know. */
        return ROBO_CSS_NUMERIC_PARSE_NON_FINITE;
    }
    switch (token->type) {
    case ROBO_CSS_TOKEN_NUMBER:
        if (robo_css_numeric_make_number(&numeric, token->number, token->is_integer,
                                         NULL) != ROBO_OK) {
            return ROBO_CSS_NUMERIC_PARSE_NON_FINITE;
        }
        break;
    case ROBO_CSS_TOKEN_PERCENTAGE:
        if (robo_css_numeric_make_percentage(&numeric, token->number, NULL) != ROBO_OK) {
            return ROBO_CSS_NUMERIC_PARSE_NON_FINITE;
        }
        break;
    case ROBO_CSS_TOKEN_DIMENSION: {
        robo_css_unit_t unit = robo_css_unit_from_slice(token->unit);

        if (unit == ROBO_CSS_UNIT_NONE) {
            /* `10foo`: the number and the shape are fine and the unit is one this
             * build does not know. */
            return ROBO_CSS_NUMERIC_PARSE_UNKNOWN_UNIT;
        }
        if (robo_css_numeric_make_length(&numeric, token->number, unit, NULL) != ROBO_OK) {
            return ROBO_CSS_NUMERIC_PARSE_NON_FINITE;
        }
        break;
    }
    default:
        /* An identifier, a string, a `%`, a bracket: none of them is a number. */
        return ROBO_CSS_NUMERIC_PARSE_INVALID;
    }
    *out = numeric;
    return ROBO_CSS_NUMERIC_PARSE_RESOLVED;
}

robo_css_numeric_parse_t robo_css_numeric_from_value(const robo_css_value_t *specified,
                                                     robo_css_numeric_t *out)
{
    const robo_css_value_t *single;

    if (!out) {
        return ROBO_CSS_NUMERIC_PARSE_INVALID;
    }
    single = robo_css_value_single(specified);
    if (!single) {
        /* Nothing, or more than one significant component: `10 20` is two numbers
         * and therefore not one, and `1.2.3` arrives as three components because
         * the tokenizer is lossless and the grammar is total. */
        return ROBO_CSS_NUMERIC_PARSE_INVALID;
    }
    if (single->kind == ROBO_CSS_VALUE_KIND_FUNCTION) {
        if (numeric_function_is_deferred(single)) {
            return ROBO_CSS_NUMERIC_PARSE_DEFERRED;
        }
        /* A function that is not a reference: `rgb(…)`, `url(…)`. Not a number. */
        return ROBO_CSS_NUMERIC_PARSE_INVALID;
    }
    if (single->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        /* A block or a nested list is not a numeric value. */
        return ROBO_CSS_NUMERIC_PARSE_INVALID;
    }
    return robo_css_numeric_from_token(&single->token, out);
}

/* ------------------------------------------------------------------ */
/* Serialization                                                       */
/* ------------------------------------------------------------------ */

/** The buffer the fixed-point rendering is built in before it is copied out. */
#define NUMERIC_DIGITS_MAX 64u

/**
 * @p value rendered canonically into @p out, NUL-terminated.
 *
 * The rules, and each is a decision:
 *
 *   `%.0f` when the value is whole, so `10` has no decimal point;
 *   `%.6f` otherwise, with trailing zeroes and then a bare `.` stripped, so `1.5`
 *   is `1.5` and `0.50` is `0.5`;
 *   `-0` becomes `0`, because it is zero and has no sign;
 *   and a **non-zero value that the fixed-point precision renders as zero** falls
 *   back to a compact exponent form. That last rule is the one that stops this
 *   function lying: `1e-07` rounded to six decimals is `0.000000`, and a value
 *   that is not zero must not print as though it were.
 *
 * Hex-float or `%g`-everything were both available and both rejected: the pinned
 * shape of every number this subsystem has emitted since 9.8 is fixed-point, and a
 * serialization that changes notation with the magnitude is harder to read and no
 * more correct.
 */
static int numeric_render(double value, char *out, size_t capacity)
{
    char digits[NUMERIC_DIGITS_MAX];
    int whole = 0;
    int written;
    size_t length;

    /* The in-range test guards the cast, in that order: a cast of a double outside
     * the integer range is undefined behaviour, and `1e300` is a value this
     * function is asked to render. */
    if (value <= ROBO_CSS_NUMERIC_INTEGER_LIMIT &&
        value >= -ROBO_CSS_NUMERIC_INTEGER_LIMIT &&
        (double)(int64_t)value == value) {
        whole = 1;
    }
    written = snprintf(digits, sizeof(digits), whole ? "%.0f" : "%.6f", value);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        return 0;
    }
    length = (size_t)written;
    if (!whole) {
        while (length > 0u && digits[length - 1u] == '0') {
            length--;
        }
        if (length > 0u && digits[length - 1u] == '.') {
            length--;
        }
    }
    /*
     * Negative zero is zero, and it is normalized *after* the two branches
     * rather than inside the fractional one. `-0` reaches here with no fraction
     * to strip — `%.0f` renders it whole — so a rule written only for the
     * fractional path would leave the sign on a value that is zero, which is a
     * spelling no reader should have to know means nothing.
     */
    if (length == 0u || (length == 2u && digits[0] == '-' && digits[1] == '0')) {
        digits[0] = '0';
        length = 1u;
    }
    if (!whole) {
        if (length == 1u && digits[0] == '0' && value != 0.0) {
            /* Below the fixed-point precision and not zero: a compact form says
             * what the fixed-point one would have denied. */
            written = snprintf(digits, sizeof(digits), "%.6g", value);
            if (written < 0 || (size_t)written >= sizeof(digits)) {
                return 0;
            }
            length = (size_t)written;
        }
    }
    if (capacity <= length) {
        return 0;
    }
    memcpy(out, digits, length);
    out[length] = '\0';
    return 1;
}

robo_result_t robo_css_numeric_write(robo_css_numeric_t numeric,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_css_diag_t *diag)
{
    char digits[NUMERIC_DIGITS_MAX];
    size_t used = 0u;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    if (!robo_css_numeric_is_present(numeric)) {
        /* Nothing to write, and that is a success: a cleared record has no text,
         * exactly as a cleared computed value does. */
        return ROBO_OK;
    }
    if (!numeric_render(numeric.number, digits, sizeof(digits))) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                             "the number does not fit its canonical form");
    }
    {
        size_t length = strlen(digits);

        if (length >= capacity) {
            *out_length = length;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                                 "the numeric value's text does not fit");
        }
        memcpy(buffer, digits, length);
        used = length;
        buffer[used] = '\0';
    }
    if (numeric.kind == ROBO_CSS_NUMERIC_PERCENTAGE) {
        if (used + 1u >= capacity) {
            buffer[0] = '\0';
            *out_length = used + 1u;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                                 "the numeric value's text does not fit");
        }
        buffer[used++] = '%';
        buffer[used] = '\0';
    } else if (numeric.kind == ROBO_CSS_NUMERIC_LENGTH) {
        /* The canonical spelling from the table, which is also what proves the
         * unit's case-insensitivity: `10PX` writes as `10px`. */
        const char *unit = robo_css_unit_name(numeric.unit);
        size_t length = strlen(unit);

        if (length == 0u || used + length >= capacity) {
            buffer[0] = '\0';
            *out_length = used + length;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                                 "the numeric value's text does not fit");
        }
        memcpy(buffer + used, unit, length);
        used += length;
        buffer[used] = '\0';
    }
    *out_length = used;
    return ROBO_OK;
}
