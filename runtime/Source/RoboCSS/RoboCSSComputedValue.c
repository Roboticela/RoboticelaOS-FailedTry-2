/*
 * RoboCSS computed values (Phase 9.8) — see RoboCSSComputedValue.h.
 *
 * Three things live here and nothing else:
 *
 *   units      the closed set, which of them have a fixed px factor, and the
 *              identifier -> unit mapping
 *   colours    the standard named set, the four hex lengths, and rgb()/rgba()
 *              in both syntaxes — everything the COLOUR kind needs, with the
 *              modern spaces and `currentColor` left to 9.9
 *   compute    the one dispatch from "what shape is this property" to a typed
 *              value, which is the whole of this phase's normalisation
 *
 * Two habits are worth naming because they are what keeps the file honest.
 *
 * First, every path that reads a token reads its *value* rather than its `text`:
 * `R\47 B` is `rgb`, `\72 ed` is `red`, and a hex digit cannot be an escape
 * because escapes were already resolved by the time this file looks. Using the
 * raw bytes would make an escape spelling a different colour, which is exactly
 * the bug CSS's escape rule exists to prevent.
 *
 * Second, nothing here resolves a relative length or a percentage. `2em` is
 * carried as 2 em and `50%` as 50 percent because the information needed to go
 * further — a parent font size, a containing block — does not exist at this
 * stage. A converter that guessed would produce a number that looked computed
 * and was wrong.
 */

#include "RoboCSS/RoboCSSComputedValue.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Units                                                              */
/* ------------------------------------------------------------------ */

/** One unit: its spelling, and its factor to px (0 when it has none). */
typedef struct computed_unit_entry {
    const char *name;
    robo_css_unit_t unit;
    double to_px;
} computed_unit_entry_t;

/*
 * The closed set.
 *
 * The px factors are CSS's own, and they are exact by definition rather than by
 * measurement: 1in is 96px, 1cm is 96/2.54px, and the rest follow. Nothing here
 * consults a display's DPI, which is the point — an absolute length means the
 * same thing everywhere, and only a *used* value (a later phase's) may care
 * about the device.
 */
static const computed_unit_entry_t computed_units[] = {
    { "px",   ROBO_CSS_UNIT_PX,   1.0 },
    { "cm",   ROBO_CSS_UNIT_CM,   96.0 / 2.54 },
    { "mm",   ROBO_CSS_UNIT_MM,   96.0 / 25.4 },
    { "q",    ROBO_CSS_UNIT_Q,    96.0 / 101.6 },
    { "in",   ROBO_CSS_UNIT_IN,   96.0 },
    { "pt",   ROBO_CSS_UNIT_PT,   96.0 / 72.0 },
    { "pc",   ROBO_CSS_UNIT_PC,   16.0 },
    { "em",   ROBO_CSS_UNIT_EM,   0.0 },
    { "rem",  ROBO_CSS_UNIT_REM,  0.0 },
    { "ex",   ROBO_CSS_UNIT_EX,   0.0 },
    { "ch",   ROBO_CSS_UNIT_CH,   0.0 },
    { "vw",   ROBO_CSS_UNIT_VW,   0.0 },
    { "vh",   ROBO_CSS_UNIT_VH,   0.0 },
    { "vmin", ROBO_CSS_UNIT_VMIN, 0.0 },
    { "vmax", ROBO_CSS_UNIT_VMAX, 0.0 }
};

int robo_css_unit_is_known(robo_css_unit_t unit)
{
    return unit >= ROBO_CSS_UNIT_NONE && unit < (robo_css_unit_t)ROBO_CSS_UNIT_COUNT;
}

int robo_css_unit_is_absolute(robo_css_unit_t unit)
{
    size_t index;

    /* Asked of the table, not of `to_px`: that function answers with the value
     * it was given when the unit has no fixed factor, so a relative unit and an
     * undeclared one both come back looking convertible. The factor being
     * present is the fact this predicate is about. */
    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (computed_units[index].unit == unit) {
            return computed_units[index].to_px != 0.0;
        }
    }
    return 0;
}

const char *robo_css_unit_name(robo_css_unit_t unit)
{
    size_t index;

    if (unit == ROBO_CSS_UNIT_NONE) {
        return "";
    }
    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (computed_units[index].unit == unit) {
            return computed_units[index].name;
        }
    }
    return "";
}

robo_css_unit_t robo_css_unit_from_slice(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name) || name.length == 0u) {
        return ROBO_CSS_UNIT_NONE;
    }
    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length,
                                         computed_units[index].name)) {
            return computed_units[index].unit;
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
    size_t index;

    for (index = 0u; index < sizeof(computed_units) / sizeof(computed_units[0]); index++) {
        if (computed_units[index].unit == unit && computed_units[index].to_px != 0.0) {
            return value * computed_units[index].to_px;
        }
    }
    return value;
}

/* ------------------------------------------------------------------ */
/* Reading a component value                                          */
/* ------------------------------------------------------------------ */

/*
 * Four readers used to be defined here — trivia, "the one significant
 * component", "is this function named X", and "a token's decoded keyword text".
 * Phase 9.9's colour subsystem needs all four (an `rgb()` call's arguments are
 * component values like any other), so they moved to RoboCSSInternal.h rather
 * than being written twice. What is left here is the one wrapper the length and
 * keyword normalisers want.
 */

/** The token of a single non-trivia component, or NULL. */
static const robo_css_token_t *computed_single_token(const robo_css_value_t *value)
{
    const robo_css_value_t *single = robo_css_value_single(value);

    if (!single || single->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return NULL;
    }
    if (single->token.type == ROBO_CSS_TOKEN_WHITESPACE ||
        single->token.type == ROBO_CSS_TOKEN_COMMENT) {
        return NULL;
    }
    return &single->token;
}

/**
 * Non-zero when @p value is a function this build recognises but cannot compute
 * — `calc()` and the maths family, `var()` and the environment family.
 *
 * They are *recognised* rather than invalid, and that is the point: `calc(100%
 * - 20px)` is a perfectly good computed value that only a layout pass can
 * reduce, and calling it invalid would delete a declaration the author wrote.
 * Carried PRESERVED with the unresolved flag set, it survives to the phase that
 * has the context, which is the correct behaviour for every one of these.
 */
static int computed_function_is_unresolved(const robo_css_value_t *value)
{
    static const char *const names[] = { "calc", "min", "max", "clamp", "var", "env" };
    size_t index;

    if (!value || value->kind != ROBO_CSS_VALUE_KIND_FUNCTION) {
        return 0;
    }
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        if (robo_css_bytes_equal_ascii_ci(value->name.bytes, value->name.length,
                                          names[index])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

void robo_css_computed_value_init(robo_css_computed_value_t *value)
{
    if (!value) {
        return;
    }
    memset(value, 0, sizeof(*value));
    value->kind = ROBO_CSS_COMPUTED_VALUE_NONE;
    value->unit = ROBO_CSS_UNIT_NONE;
    robo_css_value_init(&value->preserved);
}

void robo_css_computed_value_dispose(robo_css_computed_value_t *value)
{
    if (!value) {
        return;
    }
    robo_free(value->text);
    robo_css_value_dispose(&value->preserved);
    robo_css_computed_value_init(value);
}

robo_result_t robo_css_computed_value_move(robo_css_computed_value_t *value,
                                          robo_css_computed_value_t *out_value,
                                          robo_css_diag_t *diag)
{
    if (!value || !out_value || value == out_value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a computed value cannot move into itself");
    }
    robo_css_computed_value_dispose(out_value);
    *out_value = *value;
    robo_css_computed_value_init(value);
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_copy(robo_css_computed_value_t *out_value,
                                          const robo_css_computed_value_t *value,
                                          robo_css_diag_t *diag)
{
    robo_result_t result;

    if (!out_value || !value || out_value == value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a computed value cannot copy into itself");
    }
    robo_css_computed_value_dispose(out_value);
    *out_value = *value;
    out_value->text = NULL;
    out_value->text_length = 0u;
    robo_css_value_init(&out_value->preserved);

    if (value->text) {
        out_value->text = (char *)robo_malloc(value->text_length + 1u);
        if (!out_value->text) {
            robo_css_computed_value_init(out_value);
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "could not copy the computed value's text");
        }
        memcpy(out_value->text, value->text, value->text_length);
        out_value->text[value->text_length] = '\0';
        /* The length goes back only once the bytes it describes exist: a value
         * whose text pointer is set but whose length is still zero reads as
         * empty, which is how a copied keyword (`red`) would arrive at a child
         * as no keyword at all. */
        out_value->text_length = value->text_length;
    }
    if (value->preserved.kind != ROBO_CSS_VALUE_KIND_NONE) {
        result = robo_css_value_copy(&out_value->preserved, &value->preserved, diag);
        if (result != ROBO_OK) {
            robo_free(out_value->text);
            robo_css_computed_value_init(out_value);
            return result;
        }
    }
    return ROBO_OK;
}

int robo_css_computed_value_is_empty(const robo_css_computed_value_t *value)
{
    return !value || value->kind == ROBO_CSS_COMPUTED_VALUE_NONE;
}

/* ------------------------------------------------------------------ */
/* Kinds                                                              */
/* ------------------------------------------------------------------ */

int robo_css_computed_value_kind_is_known(robo_css_computed_value_kind_t kind)
{
    return kind >= ROBO_CSS_COMPUTED_VALUE_NONE &&
           kind < (robo_css_computed_value_kind_t)ROBO_CSS_COMPUTED_VALUE_KIND_COUNT;
}

const char *robo_css_computed_value_kind_name(robo_css_computed_value_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_COMPUTED_VALUE_NONE:
        return "none";
    case ROBO_CSS_COMPUTED_VALUE_KEYWORD:
        return "keyword";
    case ROBO_CSS_COMPUTED_VALUE_NUMBER:
        return "number";
    case ROBO_CSS_COMPUTED_VALUE_PERCENTAGE:
        return "percentage";
    case ROBO_CSS_COMPUTED_VALUE_LENGTH:
        return "length";
    case ROBO_CSS_COMPUTED_VALUE_COLOR:
        return "colour";
    case ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST:
        return "family-list";
    case ROBO_CSS_COMPUTED_VALUE_PRESERVED:
        return "preserved";
    case ROBO_CSS_COMPUTED_VALUE_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Construction                                                       */
/* ------------------------------------------------------------------ */

/** Own a copy of @p bytes in @p value->text, or fail without changing it. */
static int computed_adopt_text(robo_css_computed_value_t *value,
                               const uint8_t *bytes,
                               size_t length)
{
    char *copy = (char *)robo_malloc(length + 1u);

    if (!copy) {
        return 0;
    }
    if (length > 0u) {
        memcpy(copy, bytes, length);
    }
    copy[length] = '\0';
    robo_free(value->text);
    value->text = copy;
    value->text_length = length;
    return 1;
}

robo_result_t robo_css_computed_value_make_keyword(robo_css_computed_value_t *value,
                                                  robo_css_slice_t keyword,
                                                  robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (!computed_adopt_text(value, keyword.bytes, keyword.length)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not copy the keyword");
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_KEYWORD;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_number(robo_css_computed_value_t *value,
                                                 double number,
                                                 int is_integer,
                                                 robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_NUMBER;
    value->number = number;
    value->is_integer = is_integer ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_percentage(robo_css_computed_value_t *value,
                                                     double percentage,
                                                     int is_integer,
                                                     robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_PERCENTAGE;
    value->number = percentage;
    value->is_integer = is_integer ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_length(robo_css_computed_value_t *value,
                                                 double number,
                                                 robo_css_unit_t unit,
                                                 int is_integer,
                                                 robo_css_diag_t *diag)
{
    if (!value || !robo_css_unit_is_known(unit) || unit == ROBO_CSS_UNIT_NONE) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a length needs a known, non-empty unit");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_LENGTH;
    value->number = number;
    value->unit = unit;
    value->is_integer = is_integer ? 1 : 0;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_color(robo_css_computed_value_t *value,
                                                robo_css_color_t color,
                                                robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    if (!robo_css_color_is_valid(color)) {
        /* A COLOR value that is not a colour (NONE or INVALID) is a bug in the
         * caller, not a value: the honest way to say "there is no colour here"
         * is a computed value that is not a COLOR at all. `currentColor` is
         * accepted, because it *is* a colour and its whole point is that it is
         * not resolved yet. */
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "a colour value needs a colour, or currentColor");
    }
    robo_css_computed_value_dispose(value);
    value->kind = ROBO_CSS_COMPUTED_VALUE_COLOR;
    value->color = color;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_family_list(robo_css_computed_value_t *value,
                                                       robo_css_slice_t families,
                                                       robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (!computed_adopt_text(value, families.bytes, families.length)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not copy the family list");
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST;
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_preserved(robo_css_computed_value_t *value,
                                                     const robo_css_value_t *specified,
                                                     robo_css_diag_t *diag)
{
    if (!value || !specified) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value or specified value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (specified->kind == ROBO_CSS_VALUE_KIND_NONE) {
        /* Nothing to preserve. NONE is the honest answer: "there was no value"
         * is not "here is a value that means nothing". */
        return ROBO_OK;
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_PRESERVED;
    {
        robo_result_t result = robo_css_value_copy(&value->preserved, specified, diag);

        if (result != ROBO_OK) {
            robo_css_computed_value_init(value);
            return result;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_css_computed_value_make_invalid(robo_css_computed_value_t *value,
                                                   const char *reason,
                                                   robo_css_diag_t *diag)
{
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value is NULL");
    }
    robo_css_computed_value_dispose(value);
    if (!computed_adopt_text(value, (const uint8_t *)(reason ? reason : ""),
                             reason ? strlen(reason) : 0u)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not copy the reason");
    }
    value->kind = ROBO_CSS_COMPUTED_VALUE_INVALID;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Reading                                                            */
/* ------------------------------------------------------------------ */

robo_css_slice_t robo_css_computed_value_text(const robo_css_computed_value_t *value)
{
    if (!value || !value->text) {
        return robo_css_slice_empty();
    }
    return robo_css_slice_from_bytes((const uint8_t *)value->text, value->text_length);
}

const robo_css_value_t *robo_css_computed_value_preserved(
    const robo_css_computed_value_t *value)
{
    if (!value || value->kind != ROBO_CSS_COMPUTED_VALUE_PRESERVED) {
        return NULL;
    }
    return &value->preserved;
}

robo_css_color_t robo_css_computed_value_color(const robo_css_computed_value_t *value)
{
    if (!value || value->kind != ROBO_CSS_COMPUTED_VALUE_COLOR) {
        return robo_css_color_make(0u, 0u, 0u, 0u);
    }
    return value->color;
}

/* ------------------------------------------------------------------ */
/* Equality                                                           */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when two tokens' *values* are the same bytes.
 *
 * The raw text is the fast path and the correct one whenever neither token
 * contains an escape, which is the overwhelming majority: an escape always
 * changes the value's length, so equal text length and value length proves
 * there is none. When there is one, both values are decoded and compared,
 * because `\72 ed` and `red` are the same token and a comparison that said
 * otherwise would make two identical styles unequal.
 */
static int computed_token_value_equals(const robo_css_token_t *a,
                                       const robo_css_token_t *b)
{
    uint8_t scratch_a[128];
    uint8_t scratch_b[128];
    uint8_t *bytes_a = scratch_a;
    uint8_t *bytes_b = scratch_b;
    size_t length_a = robo_css_token_value_length(a);
    size_t length_b = robo_css_token_value_length(b);
    int equal;

    if (length_a != length_b) {
        return 0;
    }
    if (a->text.length == length_a && b->text.length == length_b) {
        return robo_css_slice_equals(a->text, b->text);
    }
    if (length_a > sizeof(scratch_a)) {
        bytes_a = (uint8_t *)robo_malloc(length_a);
        bytes_b = (uint8_t *)robo_malloc(length_b);
        if (!bytes_a || !bytes_b) {
            robo_free(bytes_a == scratch_a ? NULL : bytes_a);
            robo_free(bytes_b == scratch_b ? NULL : bytes_b);
            return 0;
        }
    }
    equal = robo_css_token_value_copy(a, bytes_a, length_a, &length_a, NULL) == ROBO_OK &&
            robo_css_token_value_copy(b, bytes_b, length_b, &length_b, NULL) == ROBO_OK &&
            length_a == length_b &&
            (length_a == 0u || memcmp(bytes_a, bytes_b, length_a) == 0);
    if (bytes_a != scratch_a) {
        robo_free(bytes_a);
        robo_free(bytes_b);
    }
    return equal;
}

/** Structural equality of two component-value trees. */
static int computed_trees_equal(const robo_css_value_t *a, const robo_css_value_t *b)
{
    size_t index;

    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
    case ROBO_CSS_VALUE_KIND_NONE:
        return 1;
    case ROBO_CSS_VALUE_KIND_TOKEN:
        /* The resolved value, not the raw text: `\72 ed` and `red` are the same
         * token, and two values that mean the same thing must compare equal or
         * a test cannot say what it expects. */
        return a->token.type == b->token.type &&
               computed_token_value_equals(&a->token, &b->token);
    case ROBO_CSS_VALUE_KIND_FUNCTION:
        if (a->children.count != b->children.count ||
            !robo_css_slice_equals_ci(a->name, b->name)) {
            return 0;
        }
        break;
    case ROBO_CSS_VALUE_KIND_BLOCK:
        if (a->children.count != b->children.count || a->open != b->open ||
            a->close != b->close) {
            return 0;
        }
        break;
    default:
        if (a->children.count != b->children.count) {
            return 0;
        }
        break;
    }
    for (index = 0u; index < a->children.count; index++) {
        if (!computed_trees_equal(&a->children.items[index], &b->children.items[index])) {
            return 0;
        }
    }
    return 1;
}

int robo_css_computed_value_equals(const robo_css_computed_value_t *a,
                                   const robo_css_computed_value_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind || a->unresolved != b->unresolved) {
        return 0;
    }
    switch (a->kind) {
    case ROBO_CSS_COMPUTED_VALUE_NONE:
        return 1;
    case ROBO_CSS_COMPUTED_VALUE_KEYWORD:
        return robo_css_slice_equals(robo_css_computed_value_text(a),
                                     robo_css_computed_value_text(b));
    case ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST:
    case ROBO_CSS_COMPUTED_VALUE_INVALID:
        return robo_css_slice_equals(robo_css_computed_value_text(a),
                                     robo_css_computed_value_text(b));
    case ROBO_CSS_COMPUTED_VALUE_NUMBER:
        return a->is_integer == b->is_integer && a->number == b->number;
    case ROBO_CSS_COMPUTED_VALUE_PERCENTAGE:
        return a->is_integer == b->is_integer && a->number == b->number;
    case ROBO_CSS_COMPUTED_VALUE_LENGTH:
        return a->unit == b->unit && a->is_integer == b->is_integer &&
               a->number == b->number;
    case ROBO_CSS_COMPUTED_VALUE_COLOR:
        return robo_css_color_equals(a->color, b->color);
    default:
        return computed_trees_equal(&a->preserved, &b->preserved);
    }
}

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

/** The character a block's opening token type stands for. */
static char computed_bracket_char(robo_css_token_type_t type)
{
    switch (type) {
    case ROBO_CSS_TOKEN_LEFT_SQUARE:
        return '[';
    case ROBO_CSS_TOKEN_LEFT_CURLY:
        return '{';
    default:
        return '(';
    }
}

static char computed_bracket_close(robo_css_token_type_t type)
{
    switch (type) {
    case ROBO_CSS_TOKEN_LEFT_SQUARE:
        return ']';
    case ROBO_CSS_TOKEN_LEFT_CURLY:
        return '}';
    default:
        return ')';
    }
}

/**
 * Write @p value's *number* into the buffer, trimming meaningless zeroes.
 *
 * Integers render without a decimal point because a value written as `10` is a
 * different value from `10.0` in CSS only where the property says so, and the
 * `is_integer` flag already records that. A non-integer renders with at most six
 * decimals — enough that every length and colour channel this build produces
 * round-trips, and few enough that `0.1 + 0.2` does not print seventeen digits.
 */
static int computed_write_number(double value, int is_integer,
                                 char *buffer, size_t capacity, size_t *used)
{
    char digits[64];
    int written;
    size_t length;

    if (capacity <= *used + 1u) {
        return 0;
    }
    written = snprintf(digits, sizeof(digits),
                       is_integer ? "%.0f" : "%.6f", value);
    if (written < 0 || (size_t)written >= sizeof(digits)) {
        return 0;
    }
    length = (size_t)written;
    if (!is_integer) {
        while (length > 0u && digits[length - 1u] == '0') {
            length--;
        }
        if (length > 0u && digits[length - 1u] == '.') {
            length--;
        }
        if (length == 0u) {
            /* `-0.000000` is zero, and zero renders as `0`. */
            digits[0] = '0';
            length = 1u;
        }
    }
    if (capacity <= *used + length) {
        return 0;
    }
    memcpy(buffer + *used, digits, length);
    *used += length;
    buffer[*used] = '\0';
    return 1;
}

static int computed_write_into(const robo_css_value_t *value, char *buffer,
                               size_t capacity, size_t *used);

static int computed_write_children(const robo_css_value_t *value, char *buffer,
                                   size_t capacity, size_t *used)
{
    size_t index;

    for (index = 0u; index < value->children.count; index++) {
        if (!computed_write_into(&value->children.items[index], buffer, capacity,
                                 used)) {
            return 0;
        }
    }
    return 1;
}

static int computed_append_char(char *buffer, size_t capacity, size_t *used, char byte)
{
    if (capacity <= *used + 1u) {
        return 0;
    }
    buffer[*used] = byte;
    (*used)++;
    buffer[*used] = '\0';
    return 1;
}

static int computed_write_token_text(const robo_css_value_t *value, char *buffer,
                                     size_t capacity, size_t *used);

static int computed_write_into(const robo_css_value_t *value, char *buffer,
                               size_t capacity, size_t *used)
{
    switch (value->kind) {
    case ROBO_CSS_VALUE_KIND_TOKEN:
        /* The raw text: this rendering is a diagnostic, and for a preserved
         * tree the source spelling is what a developer wants to see. */
        return computed_write_token_text(value, buffer, capacity, used);
    case ROBO_CSS_VALUE_KIND_FUNCTION:
        if (value->name.length > capacity - *used - 1u) {
            return 0;
        }
        if (value->name.length > 0u) {
            memcpy(buffer + *used, value->name.bytes, value->name.length);
            *used += value->name.length;
            buffer[*used] = '\0';
        }
        return computed_append_char(buffer, capacity, used, '(') &&
               computed_write_children(value, buffer, capacity, used) &&
               computed_append_char(buffer, capacity, used, ')');
    case ROBO_CSS_VALUE_KIND_BLOCK:
        return computed_append_char(buffer, capacity, used,
                                    computed_bracket_char(value->open)) &&
               computed_write_children(value, buffer, capacity, used) &&
               computed_append_char(buffer, capacity, used,
                                    computed_bracket_close(value->open));
    case ROBO_CSS_VALUE_KIND_LIST:
        return computed_write_children(value, buffer, capacity, used);
    default:
        return 1;
    }
}

/**
 * One token's raw text, appended to the buffer.
 *
 * The raw text rather than the decoded value, because this rendering exists so
 * that a developer can see the tokens a PRESERVED value carries and the source
 * spelling is exactly what they wrote.
 */
static int computed_write_token_text(const robo_css_value_t *value, char *buffer,
                                     size_t capacity, size_t *used)
{
    if (value->token.text.length > capacity - *used - 1u) {
        return 0;
    }
    if (value->token.text.length > 0u) {
        memcpy(buffer + *used, value->token.text.bytes, value->token.text.length);
        *used += value->token.text.length;
        buffer[*used] = '\0';
    }
    return 1;
}

robo_result_t robo_css_computed_value_write(const robo_css_computed_value_t *value,
                                            char *buffer,
                                            size_t capacity,
                                            size_t *out_length,
                                            robo_css_diag_t *diag)
{
    size_t used = 0u;
    int ok = 1;

    if (!value || !buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "value, buffer and out_length are required");
    }
    buffer[0] = '\0';

    switch (value->kind) {
    case ROBO_CSS_COMPUTED_VALUE_NONE:
        break;
    case ROBO_CSS_COMPUTED_VALUE_KEYWORD:
    case ROBO_CSS_COMPUTED_VALUE_FAMILY_LIST:
    case ROBO_CSS_COMPUTED_VALUE_INVALID:
        if (value->text_length >= capacity) {
            *out_length = value->text_length;
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                                 "the computed value's text does not fit");
        }
        if (value->text_length > 0u) {
            memcpy(buffer, value->text, value->text_length);
        }
        buffer[value->text_length] = '\0';
        used = value->text_length;
        break;
    case ROBO_CSS_COMPUTED_VALUE_NUMBER:
        ok = computed_write_number(value->number, value->is_integer, buffer, capacity,
                                   &used);
        break;
    case ROBO_CSS_COMPUTED_VALUE_PERCENTAGE:
        ok = computed_write_number(value->number, value->is_integer, buffer, capacity,
                                   &used) &&
             computed_append_char(buffer, capacity, &used, '%');
        break;
    case ROBO_CSS_COMPUTED_VALUE_LENGTH:
        ok = computed_write_number(value->number, value->is_integer, buffer, capacity,
                                   &used);
        if (ok) {
            const char *unit = robo_css_unit_name(value->unit);
            size_t length = strlen(unit);

            if (length >= capacity - used) {
                ok = 0;
            } else {
                memcpy(buffer + used, unit, length);
                used += length;
                buffer[used] = '\0';
            }
        }
        break;
    case ROBO_CSS_COMPUTED_VALUE_COLOR:
        /* One spelling per colour, and it is the colour subsystem's: this layer
         * formats lengths, numbers and token trees, and a colour is none of
         * those. `currentColor` therefore prints as `currentColor` because that
         * is what the colour module says it prints as. */
        ok = robo_css_color_write(value->color, buffer, capacity, &used, NULL) == ROBO_OK;
        break;
    case ROBO_CSS_COMPUTED_VALUE_PRESERVED:
        if (value->preserved.kind == ROBO_CSS_VALUE_KIND_TOKEN) {
            ok = computed_write_token_text(&value->preserved, buffer, capacity, &used);
        } else {
            ok = computed_write_into(&value->preserved, buffer, capacity, &used);
        }
        break;
    default:
        ok = 0;
        break;
    }

    if (!ok) {
        buffer[0] = '\0';
        *out_length = used;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED,
                             "the computed value's text does not fit");
    }
    *out_length = used;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Computing                                                          */
/* ------------------------------------------------------------------ */

/** Make @p out INVALID and record why, always succeeding at the report. */
static robo_result_t computed_reject(robo_css_computed_value_t *out,
                                     robo_css_property_computation_t computation,
                                     const char *reason,
                                     robo_css_diag_t *diag)
{
    char message[128];

    (void)snprintf(message, sizeof(message), "%s: %s",
                   robo_css_property_computation_name(computation), reason);
    ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_NOT_SUPPORTED, message);
    return robo_css_computed_value_make_invalid(out, reason, NULL);
}

/** Preserve @p specified, marking it unresolved for @p out. */
static robo_result_t computed_keep(robo_css_computed_value_t *out,
                                   const robo_css_value_t *specified,
                                   int unresolved,
                                   robo_css_diag_t *diag)
{
    robo_result_t result = robo_css_computed_value_make_preserved(out, specified, diag);

    if (result == ROBO_OK) {
        out->unresolved = unresolved ? 1 : 0;
    }
    return result;
}

/**
 * The reason an INVALID colour carries, naming the function it was written as.
 *
 * `rgb()` and `hsl()` fail for the same set of reasons — a channel out of range,
 * the wrong separator, the wrong number of arguments — and an author who wrote
 * `hsl(0, 100%)` is better served by a message that says which call was wrong.
 * The name is quoted from the value itself rather than reconstructed from the
 * property, so a message cannot disagree with the declaration it describes.
 */
static void computed_color_reason(const robo_css_value_t *specified, char *buffer,
                                  size_t capacity)
{
    const robo_css_value_t *single = robo_css_value_single(specified);
    size_t length;

    if (!single || single->kind != ROBO_CSS_VALUE_KIND_FUNCTION ||
        robo_css_slice_is_absent(single->name) || single->name.length == 0u) {
        (void)snprintf(buffer, capacity, "a colour function written wrong");
        return;
    }
    length = single->name.length;
    if (length > 32u) {
        length = 32u;
    }
    (void)snprintf(buffer, capacity, "%.*s() arguments are out of range or malformed",
                   (int)length, (const char *)single->name.bytes);
}

/**
 * Compute a colour, by handing it to the colour subsystem.
 *
 * The whole of this layer's colour knowledge, and it is deliberately almost
 * nothing: `robo_css_color_from_value` (RoboCSSColor.h) owns every spelling — the
 * named set, `transparent`, all four hex lengths, `rgb()`/`rgba()` and
 * `hsl()`/`hsla()` — and this function's only job is to turn its answer into the
 * right computed value. There is one colour parser in this subsystem and it is
 * that one; a second copy here is how two spellings of `rgb()` come to disagree.
 *
 * The five outcomes, and why each maps where it does:
 *
 *   RESOLVED    the channels, as a COLOR value
 *   CURRENT     a COLOR value whose colour is the CURRENT kind, flagged
 *               unresolved. It is *typed* rather than carried as tokens, and it
 *               stays unresolved because the only context that can resolve it —
 *               the element's own `color`, and its parent's — is one the computed
 *               style has and this function does not (see RoboCSSComputed.c).
 *   DEFERRED    a colour function this build does not represent, carried with
 *               its tokens and flagged unresolved. The author wrote legal CSS;
 *               refusing it would be a lie, and approximating it would be worse.
 *   MALFORMED   a colour function written wrong, reported INVALID with *why*.
 *   INVALID     not a colour at all.
 *
 * One case is settled before any of that, because the colour parser is the wrong
 * authority for it: a **reference** — `var()`, `env()`, `calc()` — is not a
 * colour *yet*, so `color: var(--brand)` is carried with its tokens and flagged
 * unresolved rather than reported invalid. The colour module would call it
 * non-colour syntax, and it would be right about the syntax and wrong about the
 * author, who wrote a legal reference to a colour. Custom properties are still
 * never *interpreted* here (see RoboCSSValue.h): nothing substitutes `--brand`,
 * and nothing reads it as a colour until a later phase resolves the reference.
 */
static robo_result_t computed_compute_color(robo_css_computed_value_t *out,
                                            const robo_css_value_t *specified,
                                            robo_css_diag_t *diag)
{
    robo_css_color_t color = robo_css_color_none();
    robo_css_color_parse_t outcome = robo_css_color_from_value(specified, &color);

    if (outcome == ROBO_CSS_COLOR_PARSE_INVALID) {
        const robo_css_value_t *single = robo_css_value_single(specified);

        if (single && single->kind == ROBO_CSS_VALUE_KIND_FUNCTION &&
            computed_function_is_unresolved(single)) {
            return computed_keep(out, specified, 1, diag);
        }
    }

    switch (outcome) {
    case ROBO_CSS_COLOR_PARSE_RESOLVED:
    case ROBO_CSS_COLOR_PARSE_CURRENT:
        if (outcome == ROBO_CSS_COLOR_PARSE_CURRENT) {
            /* The one thing this layer can say about `currentColor`: the value
             * is a colour, and it is not resolved. */
            robo_result_t rc = robo_css_computed_value_make_color(out, color, diag);

            if (rc == ROBO_OK) {
                out->unresolved = 1;
            }
            return rc;
        }
        return robo_css_computed_value_make_color(out, color, diag);
    case ROBO_CSS_COLOR_PARSE_DEFERRED:
        /* Recognised and deliberately not computed: the colour subsystem owns
         * these spaces, and a wrong colour is worse than a deferred one. */
        return computed_keep(out, specified, 1, diag);
    case ROBO_CSS_COLOR_PARSE_MALFORMED: {
        char reason[128];

        computed_color_reason(specified, reason, sizeof(reason));
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR, reason, diag);
    }
    case ROBO_CSS_COLOR_PARSE_UNKNOWN_NAME:
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                               "unknown colour name", diag);
    default:
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                               "not a colour", diag);
    }
}

/**
 * The identifiers a length-shaped property may be written with, as one set.
 *
 * The shape is shared by several properties whose keyword lists differ, so this
 * is their union: `font-size` has `medium` and the size keywords, `letter-spacing`
 * has `normal`, `width` has `auto` and the intrinsic sizes. Which of them belongs
 * to *which* property is the property registry's business; what this layer knows
 * is a value's shape.
 *
 * What it will not do is call every identifier a length keyword. `width: red`
 * is a mistake an author wants told about, and carrying it as a computed keyword
 * would hand a later phase a value it can only fail on.
 */
static const char *const computed_length_keywords[] = {
    "auto",     "none",       "normal",   "medium",   "min-content",
    "max-content", "fit-content", "smaller", "larger",   "xx-small",
    "x-small",  "small",      "large",    "x-large",  "xx-large",
    "xxx-large"
};

/** Non-zero when @p keyword (already lowercased) names a length keyword. */
static int computed_length_keyword_is_known(const char *keyword, size_t length)
{
    size_t index;

    for (index = 0u;
         index < sizeof(computed_length_keywords) / sizeof(computed_length_keywords[0]);
         index++) {
        if (robo_css_bytes_equal_ascii_ci((const uint8_t *)keyword, length,
                                          computed_length_keywords[index])) {
            return 1;
        }
    }
    return 0;
}

/** Compute a length-family value: dimension, percentage, zero, or keyword. */
static robo_result_t computed_compute_length(robo_css_computed_value_t *out,
                                             const robo_css_value_t *specified,
                                             robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);

    if (!token) {
        const robo_css_value_t *single = robo_css_value_single(specified);

        if (single && (computed_function_is_unresolved(single) ||
                       robo_css_value_function_named(single, "calc") ||
                       robo_css_value_function_named(single, "min") ||
                       robo_css_value_function_named(single, "max") ||
                       robo_css_value_function_named(single, "clamp"))) {
            return computed_keep(out, specified, 1, diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "not a single length component", diag);
    }
    if (!robo_css_number_is_finite(token->number)) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "not a finite number", diag);
    }
    switch (token->type) {
    case ROBO_CSS_TOKEN_DIMENSION: {
        robo_css_unit_t unit = robo_css_unit_from_slice(token->unit);

        if (unit == ROBO_CSS_UNIT_NONE) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                                   "unknown length unit", diag);
        }
        if (robo_css_unit_is_absolute(unit)) {
            /* `1in` and `96px` are the same length, so a computed value has one
             * of them. Converting here is exact, not an approximation. */
            return robo_css_computed_value_make_length(
                out, robo_css_unit_to_px(token->number, unit), ROBO_CSS_UNIT_PX,
                token->is_integer, diag);
        }
        return robo_css_computed_value_make_length(out, token->number, unit,
                                                   token->is_integer, diag);
    }
    case ROBO_CSS_TOKEN_PERCENTAGE:
        /* Stays a percentage: only layout knows what it is a percentage *of*. */
        return robo_css_computed_value_make_percentage(out, token->number,
                                                       token->is_integer, diag);
    case ROBO_CSS_TOKEN_NUMBER:
        if (token->number == 0.0) {
            /* CSS's one exception: a bare `0` is a length, because there is no
             * unit that changes what zero means. */
            return robo_css_computed_value_make_length(out, 0.0, ROBO_CSS_UNIT_PX, 1,
                                                       diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "a bare number is not a length", diag);
    case ROBO_CSS_TOKEN_IDENT: {
        char keyword[64];
        size_t length = 0u;

        if (!robo_css_token_keyword_text(token, keyword, sizeof(keyword), &length) ||
            !computed_length_keyword_is_known(keyword, length)) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                                   "not a length keyword", diag);
        }
        return robo_css_computed_value_make_keyword(
            out, robo_css_slice_from_bytes((const uint8_t *)keyword, length), diag);
    }
    default:
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LENGTH,
                               "not a length", diag);
    }
}

/** Compute a single-identifier keyword. */
static robo_result_t computed_compute_keyword(robo_css_computed_value_t *out,
                                              const robo_css_value_t *specified,
                                              robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);
    const robo_css_value_t *single = robo_css_value_single(specified);
    char keyword[64];
    size_t length = 0u;

    if (!token) {
        if (single && computed_function_is_unresolved(single)) {
            return computed_keep(out, specified, 1, diag);
        }
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                               "not a single identifier", diag);
    }
    if (token->type != ROBO_CSS_TOKEN_IDENT) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                               "not an identifier", diag);
    }
    if (!robo_css_token_keyword_text(token, keyword, sizeof(keyword), &length)) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                               "not an identifier", diag);
    }
    return robo_css_computed_value_make_keyword(
        out, robo_css_slice_from_bytes((const uint8_t *)keyword, length), diag);
}

/**
 * Compute a font family list: identifiers and strings, comma separated.
 *
 * Two passes over the children, and the reason is the shape check. A family
 * list is a strictly alternating sequence — part, comma, part, comma, part — and
 * a validator that consumed the components while checking would have already
 * built half a string when it found the malformed part. Validating and summing
 * first, then building into exactly the right buffer, means a malformed list
 * leaves nothing behind and there is no growth logic to get wrong.
 *
 * `font-family: ""` is refused: an empty family name is not a family, and
 * accepting it would put an empty entry in a list that later code reads as a
 * name.
 */
static robo_result_t computed_compute_family(robo_css_computed_value_t *out,
                                             const robo_css_value_t *specified,
                                             robo_css_diag_t *diag)
{
    size_t families = 0u;
    size_t total = 0u;
    size_t index;
    int expecting_part = 1;
    char *joined;
    size_t position = 0u;
    robo_result_t result;

    if (!specified || specified->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                               "not a family list", diag);
    }
    for (index = 0u; index < specified->children.count; index++) {
        const robo_css_value_t *child = &specified->children.items[index];
        size_t length;

        if (robo_css_value_is_trivia(child)) {
            continue;
        }
        if (child->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "not a family name", diag);
        }
        if (child->token.type == ROBO_CSS_TOKEN_COMMA) {
            if (expecting_part) {
                return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                       "a family list cannot begin with a comma", diag);
            }
            expecting_part = 1;
            continue;
        }
        if (child->token.type != ROBO_CSS_TOKEN_IDENT &&
            child->token.type != ROBO_CSS_TOKEN_STRING) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "not a family name", diag);
        }
        if (!expecting_part) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "two family names with no comma between them", diag);
        }
        length = robo_css_token_value_length(&child->token);
        if (length == 0u) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                                   "an empty family name is not a family", diag);
        }
        if (total > (size_t)-1 - length) {
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                 "the family list is absurdly long");
        }
        total += length;
        families++;
        expecting_part = 0;
    }
    if (families == 0u || expecting_part) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY,
                               "not a family list", diag);
    }

    /* `, ` between families, so the value is a list rather than a coincidence of
     * token order. */
    joined = (char *)robo_malloc(total + (families - 1u) * 2u + 1u);
    if (!joined) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                             "could not build the family list");
    }
    for (index = 0u; index < specified->children.count; index++) {
        const robo_css_value_t *child = &specified->children.items[index];
        size_t length = 0u;
        uint8_t scratch[256];
        uint8_t *bytes = scratch;

        if (robo_css_value_is_trivia(child) || child->token.type == ROBO_CSS_TOKEN_COMMA) {
            if (child->kind == ROBO_CSS_VALUE_KIND_TOKEN &&
                child->token.type == ROBO_CSS_TOKEN_COMMA) {
                joined[position] = ',';
                joined[position + 1u] = ' ';
                position += 2u;
            }
            continue;
        }
        length = robo_css_token_value_length(&child->token);
        if (length > sizeof(scratch)) {
            /* The first pass proved a family this long exists; a list long
             * enough to need the heap is rare, so it is handled rather than
             * rejected, but the stack path is the one that runs. */
            bytes = (uint8_t *)robo_malloc(length);
            if (!bytes) {
                robo_free(joined);
                return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_OUT_OF_MEMORY,
                                     "could not build the family list");
            }
        }
        /* The *decoded* value, so `\31 a` and `1a` are the same family. */
        (void)robo_css_token_value_copy(&child->token, bytes, length, &length, NULL);
        memcpy(joined + position, bytes, length);
        position += length;
        if (bytes != scratch) {
            robo_free(bytes);
        }
    }
    joined[position] = '\0';
    result = robo_css_computed_value_make_family_list(out, robo_css_slice_from_text(joined),
                                                      diag);
    robo_free(joined);
    return result;
}

/** Compute `normal`/`bold`/…/100–900. */
static robo_result_t computed_compute_font_weight(robo_css_computed_value_t *out,
                                                  const robo_css_value_t *specified,
                                                  robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);

    if (!token) {
        return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                               "not a font weight", diag);
    }
    if (token->type == ROBO_CSS_TOKEN_NUMBER) {
        if (!robo_css_number_is_finite(token->number) || token->number < 1.0 ||
            token->number > 1000.0) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                                   "a font weight number must be 1 to 1000", diag);
        }
        return robo_css_computed_value_make_number(out, token->number,
                                                   token->is_integer, diag);
    }
    if (token->type == ROBO_CSS_TOKEN_IDENT) {
        static const char *const weights[] = { "normal", "bold", "bolder", "lighter" };
        size_t index;

        for (index = 0u; index < sizeof(weights) / sizeof(weights[0]); index++) {
            if (robo_css_token_value_equals_ascii_ci(token, weights[index])) {
                return robo_css_computed_value_make_keyword(
                    out, robo_css_slice_from_text(weights[index]), diag);
            }
        }
    }
    return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT,
                           "not a font weight", diag);
}

/** Compute `normal`, a number, a length or a percentage. */
static robo_result_t computed_compute_line_height(robo_css_computed_value_t *out,
                                                  const robo_css_value_t *specified,
                                                  robo_css_diag_t *diag)
{
    const robo_css_token_t *token = computed_single_token(specified);

    if (token && token->type == ROBO_CSS_TOKEN_IDENT &&
        robo_css_token_value_equals_ascii_ci(token, "normal")) {
        return robo_css_computed_value_make_keyword(out, robo_css_slice_from_text("normal"),
                                                    diag);
    }
    if (token && token->type == ROBO_CSS_TOKEN_NUMBER) {
        if (!robo_css_number_is_finite(token->number)) {
            return computed_reject(out, ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT,
                                   "not a finite number", diag);
        }
        /* A bare number is a *multiplier* here, not a length. */
        return robo_css_computed_value_make_number(out, token->number,
                                                   token->is_integer, diag);
    }
    return computed_compute_length(out, specified, diag);
}

robo_result_t robo_css_computed_value_compute(
    robo_css_property_computation_t computation,
    const robo_css_value_t *specified,
    robo_css_computed_value_t *out,
    robo_css_diag_t *diag)
{
    if (!out || !specified) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "output or specified value is NULL");
    }
    robo_css_computed_value_init(out);
    if (!robo_css_property_computation_is_known(computation)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_COMPUTED, ROBO_INVALID_ARGUMENT,
                             "not a value-computation classification");
    }
    if (specified->kind == ROBO_CSS_VALUE_KIND_NONE) {
        /* Nothing was specified: NONE, and not an invalid value. The entry this
         * belongs to carries the "unresolved default" fact, not the value. */
        return ROBO_OK;
    }

    switch (computation) {
    case ROBO_CSS_PROPERTY_COMPUTATION_COLOR:
        return computed_compute_color(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_LENGTH:
        return computed_compute_length(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD:
        return computed_compute_keyword(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_FONT_FAMILY:
        return computed_compute_family(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_FONT_WEIGHT:
        return computed_compute_font_weight(out, specified, diag);
    case ROBO_CSS_PROPERTY_COMPUTATION_LINE_HEIGHT:
        return computed_compute_line_height(out, specified, diag);
    default:
        return computed_keep(out, specified, computed_function_is_unresolved(
                                             robo_css_value_single(specified)),
                             diag);
    }
}
