/*
 * RoboCSS border family (Phase 9.15) — see RoboCSSBorder.h.
 *
 * Two tables and one classifier, and everything below reads them:
 *
 *   the style rows        one per value, carrying the one fact a later phase asks
 *   the kind rows         width / style / colour, each naming the box-table row
 *                         whose longhands it owns
 *
 * The classifier is what makes the composite shorthands a third machine beside
 * the box model's positional mapping and `background`'s per-longhand one: a box
 * shorthand's components map to sides *by position*, `background`'s components map
 * to six longhands *by kind*, and a border's components map to *three* kinds whose
 * longhand is then chosen by side as well. Nothing here weighs a level or compares
 * a specificity — which of a shorthand and a longhand wins is the cascade's answer,
 * asked during resolution, not this module's.
 *
 * The twelve longhand names are not spelled here. They are read out of the box
 * table's positional rows, so `border-top-width` appears once in the build; this
 * module spells the three *shorthand* names and pairs each with the kind it
 * carries, and a walk that looks every one of them up is what makes a drift
 * between the two tables visible rather than silent.
 */

#include "RoboCSS/RoboCSSBorder.h"

#include "RoboCSS/RoboCSSColor.h"
#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The style vocabulary                                                */
/* ------------------------------------------------------------------ */

/*
 * `generates` is the value, not a description of it: `none` and `hidden` *are*
 * "this border draws nothing", which is why the question is asked of the row
 * rather than written as `style != NONE && style != HIDDEN` at each call site —
 * a comparison like that has to be revisited by every phase that adds a style,
 * and it is the kind of statement that reads as an optimisation until the day a
 * new value is neither of the two named.
 */
typedef struct border_style_row {
    robo_css_border_style_t value;
    const char *keyword;
    int generates;
} border_style_row_t;

static const border_style_row_t border_style_vocabulary[] = {
    { ROBO_CSS_BORDER_STYLE_NONE,   "none",   0 },
    { ROBO_CSS_BORDER_STYLE_HIDDEN, "hidden", 0 },
    { ROBO_CSS_BORDER_STYLE_DOTTED, "dotted", 1 },
    { ROBO_CSS_BORDER_STYLE_DASHED, "dashed", 1 },
    { ROBO_CSS_BORDER_STYLE_SOLID,  "solid",  1 },
    { ROBO_CSS_BORDER_STYLE_DOUBLE, "double", 1 },
    { ROBO_CSS_BORDER_STYLE_GROOVE, "groove", 1 },
    { ROBO_CSS_BORDER_STYLE_RIDGE,  "ridge",  1 },
    { ROBO_CSS_BORDER_STYLE_INSET,  "inset",  1 },
    { ROBO_CSS_BORDER_STYLE_OUTSET, "outset", 1 }
};

#define BORDER_STYLE_VOCABULARY_COUNT \
    (sizeof(border_style_vocabulary) / sizeof(border_style_vocabulary[0]))

/** The row for @p style, or NULL when it is not one of them. */
static const border_style_row_t *border_style_row_of(robo_css_border_style_t style)
{
    size_t index;

    for (index = 0u; index < BORDER_STYLE_VOCABULARY_COUNT; index++) {
        if (border_style_vocabulary[index].value == style) {
            return &border_style_vocabulary[index];
        }
    }
    return NULL;
}

int robo_css_border_style_is_known(robo_css_border_style_t style)
{
    return border_style_row_of(style) != NULL;
}

const char *robo_css_border_style_name(robo_css_border_style_t style)
{
    const border_style_row_t *row = border_style_row_of(style);

    return row ? row->keyword : "unknown";
}

size_t robo_css_border_style_count(void)
{
    return BORDER_STYLE_VOCABULARY_COUNT;
}

robo_css_border_style_t robo_css_border_style_from_slice(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword)) {
        return ROBO_CSS_BORDER_STYLE_NONE;
    }
    for (index = 0u; index < BORDER_STYLE_VOCABULARY_COUNT; index++) {
        const char *candidate = border_style_vocabulary[index].keyword;

        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length, candidate)) {
            return border_style_vocabulary[index].value;
        }
    }
    /* `none` is both a value and this function's answer for "not one of them",
     * which is why every *check* asks robo_css_border_style_slice_is_known. */
    return ROBO_CSS_BORDER_STYLE_NONE;
}

robo_css_border_style_t robo_css_border_style_from_text(const char *keyword)
{
    if (!keyword) {
        return ROBO_CSS_BORDER_STYLE_NONE;
    }
    return robo_css_border_style_from_slice(robo_css_slice_from_text(keyword));
}

int robo_css_border_style_slice_is_known(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword)) {
        return 0;
    }
    for (index = 0u; index < BORDER_STYLE_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                          border_style_vocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_css_border_style_write(robo_css_border_style_t style,
                                          char *out,
                                          size_t capacity,
                                          size_t *out_length,
                                          robo_css_diag_t *diag)
{
    const border_style_row_t *row = border_style_row_of(style);
    size_t length;

    if (!out || !out_length || !row) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_INVALID_ARGUMENT,
                             "output is NULL or not a border style");
    }
    length = strlen(row->keyword);
    if (capacity < length + 1u) {
        *out_length = length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_NOT_SUPPORTED,
                             "the border style keyword does not fit");
    }
    memcpy(out, row->keyword, length + 1u);
    *out_length = length;
    return ROBO_OK;
}

int robo_css_border_style_generates_border(robo_css_border_style_t style)
{
    const border_style_row_t *row = border_style_row_of(style);

    return row ? row->generates : 0;
}

/* ------------------------------------------------------------------ */
/* The kinds                                                           */
/* ------------------------------------------------------------------ */

/*
 * The rows, and the whole of the module's spelling of a shorthand name.
 *
 * `index == kind` by construction, which is the point: `border-width` *is* the
 * width row, and a second table relating the two would be a place for them to
 * disagree.
 */
typedef struct border_kind_row {
    robo_css_border_kind_t kind;
    const char *shorthand;
} border_kind_row_t;

static const border_kind_row_t border_kind_vocabulary[ROBO_CSS_BORDER_KIND_COUNT] = {
    { ROBO_CSS_BORDER_KIND_WIDTH, "border-width" },
    { ROBO_CSS_BORDER_KIND_STYLE, "border-style" },
    { ROBO_CSS_BORDER_KIND_COLOR, "border-color" }
};

static const char *const border_kind_names[ROBO_CSS_BORDER_KIND_COUNT] = {
    "width", "style", "color"
};

static const border_kind_row_t *border_kind_row_of(robo_css_border_kind_t kind)
{
    size_t index;

    for (index = 0u; index < ROBO_CSS_BORDER_KIND_COUNT; index++) {
        if (border_kind_vocabulary[index].kind == kind) {
            return &border_kind_vocabulary[index];
        }
    }
    return NULL;
}

int robo_css_border_kind_is_known(robo_css_border_kind_t kind)
{
    return border_kind_row_of(kind) != NULL;
}

const char *robo_css_border_kind_name(robo_css_border_kind_t kind)
{
    const border_kind_row_t *row = border_kind_row_of(kind);

    return row ? border_kind_names[(size_t)row->kind] : "unknown";
}

size_t robo_css_border_kind_count(void)
{
    return ROBO_CSS_BORDER_KIND_COUNT;
}

size_t robo_css_border_longhand_count(void)
{
    return ROBO_CSS_BORDER_LONGHAND_COUNT;
}

const robo_css_box_shorthand_t *robo_css_border_positional_row_of_kind(
    robo_css_border_kind_t kind)
{
    const border_kind_row_t *row = border_kind_row_of(kind);

    if (!row) {
        return NULL;
    }
    /* Asked rather than restated: the box table is where the four longhands of a
     * positional shorthand are written down, and it is asked for them by name so
     * this module cannot hold a stale copy. */
    return robo_css_box_shorthand_find(robo_css_slice_from_text(row->shorthand));
}

const char *robo_css_border_longhand_name(robo_css_box_side_t side,
                                          robo_css_border_kind_t kind)
{
    if (!robo_css_box_side_is_known(side)) {
        return NULL;
    }
    return robo_css_box_shorthand_longhand(robo_css_border_positional_row_of_kind(kind),
                                           side);
}

int robo_css_border_is_longhand(robo_css_slice_t name)
{
    const robo_css_box_shorthand_t *owner;

    owner = robo_css_box_shorthand_of_longhand(name, NULL);
    if (!owner) {
        return 0;
    }
    /* A longhand is a *border* longhand when the row that owns it is one of this
     * module's three — which is the same question `distributes_shorthand` asks,
     * asked from the other side, so the two cannot disagree about the family's
     * scope. */
    return robo_css_border_distributes_shorthand(
        robo_css_slice_from_text(owner->name));
}

/* ------------------------------------------------------------------ */
/* The positional shorthands                                           */
/* ------------------------------------------------------------------ */

size_t robo_css_border_positional_count(void)
{
    return ROBO_CSS_BORDER_POSITIONAL_COUNT;
}

const char *robo_css_border_positional_name(size_t index)
{
    return index < ROBO_CSS_BORDER_KIND_COUNT
               ? border_kind_vocabulary[index].shorthand
               : NULL;
}

robo_css_border_kind_t robo_css_border_positional_kind(size_t index)
{
    return index < ROBO_CSS_BORDER_KIND_COUNT
               ? border_kind_vocabulary[index].kind
               : ROBO_CSS_BORDER_KIND_WIDTH;
}

const robo_css_box_shorthand_t *robo_css_border_positional_row(size_t index)
{
    if (index >= ROBO_CSS_BORDER_KIND_COUNT) {
        return NULL;
    }
    return robo_css_border_positional_row_of_kind(border_kind_vocabulary[index].kind);
}

int robo_css_border_distributes_shorthand(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name)) {
        return 0;
    }
    for (index = 0u; index < ROBO_CSS_BORDER_KIND_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(
                name.bytes, name.length, border_kind_vocabulary[index].shorthand)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The composite shorthands                                            */
/* ------------------------------------------------------------------ */

/*
 * Index 0 is `border` and 1..4 are the sides in vocabulary order, so the side a
 * row covers is `index - 1` — derived rather than kept in a second parallel array
 * a walk could fall out of step with.
 */
static const char *const border_composite_vocabulary[ROBO_CSS_BORDER_COMPOSITE_COUNT] = {
    "border", "border-top", "border-right", "border-bottom", "border-left"
};

size_t robo_css_border_composite_count(void)
{
    return ROBO_CSS_BORDER_COMPOSITE_COUNT;
}

const char *robo_css_border_composite_name(size_t index)
{
    return index < ROBO_CSS_BORDER_COMPOSITE_COUNT
               ? border_composite_vocabulary[index]
               : NULL;
}

robo_css_box_side_t robo_css_border_composite_side(size_t index)
{
    if (index == 0u || index >= ROBO_CSS_BORDER_COMPOSITE_COUNT) {
        return ROBO_CSS_BOX_SIDE_TOP;
    }
    return (robo_css_box_side_t)(index - 1u);
}

int robo_css_border_composite_is_whole(size_t index)
{
    return index == 0u;
}

int robo_css_border_is_composite(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name)) {
        return 0;
    }
    for (index = 0u; index < ROBO_CSS_BORDER_COMPOSITE_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(
                name.bytes, name.length, border_composite_vocabulary[index])) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/* The two readers the classifier needs before the section that owns them. */
static const robo_css_value_t *border_component_at(const robo_css_value_t *value,
                                                   size_t wanted);
static size_t border_component_count(const robo_css_value_t *value);

/* Five rows, and the third and fourth are what keep this honest: DEFERRED is
 * "legal CSS whose answer is not written yet" and INVALID is "this means nothing
 * here", and an author who wrote `var(--w)` needs a different sentence from one
 * who wrote `10%`. */
static const char *const border_class_names[] = {
    "invalid", "width", "style", "color", "deferred"
};

/** Number of classification outcomes. */
#define BORDER_CLASS_COUNT \
    (sizeof(border_class_names) / sizeof(border_class_names[0]))

int robo_css_border_class_is_known(robo_css_border_class_t outcome)
{
    return outcome >= ROBO_CSS_BORDER_CLASS_INVALID &&
           (size_t)outcome < BORDER_CLASS_COUNT;
}

const char *robo_css_border_class_name(robo_css_border_class_t outcome)
{
    return robo_css_border_class_is_known(outcome)
               ? border_class_names[(size_t)outcome]
               : "unknown";
}

size_t robo_css_border_class_count(void)
{
    return BORDER_CLASS_COUNT;
}

/** Non-zero when @p keyword is `thin`, `medium` or `thick`. */
static int border_keyword_is_line_width(const char *keyword, size_t length)
{
    return robo_css_bytes_equal_ascii_ci((const uint8_t *)keyword, length, "thin") ||
           robo_css_bytes_equal_ascii_ci((const uint8_t *)keyword, length, "medium") ||
           robo_css_bytes_equal_ascii_ci((const uint8_t *)keyword, length, "thick");
}

/** Non-zero when @p value is a `var()` or `env()`, which substitution decides. */
static int border_component_is_substitution(const robo_css_value_t *value)
{
    return robo_css_value_function_named(value, "var") ||
           robo_css_value_function_named(value, "env");
}

/**
 * Which kind @p component belongs to, with @p out_reason naming the refusal.
 *
 * The *order* of the questions is the whole of the logic, and the vocabulary
 * forces it. A style keyword first, because `none` and `hidden` are styles and are
 * also the two words a reader is most likely to place somewhere else. Then a width
 * keyword, because after the styles those three are the only identifiers left that
 * are certainly not colours. Then a colour — because at that point an identifier
 * the 9.9 vocabulary recognises can only be one, and one it does not recognise is a
 * component this grammar has no place for at all. The colour question is asked of
 * 9.9's *value* door rather than of its name table, since `transparent` and
 * `currentcolor` are colours and deliberately not table entries.
 */
static robo_css_border_class_t border_classify_component(const robo_css_value_t *component,
                                                         const char **out_reason)
{
    robo_css_token_t token;

    if (out_reason) {
        *out_reason = "not a border value";
    }
    if (!component || component->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return ROBO_CSS_BORDER_CLASS_INVALID;
    }
    if (component->kind == ROBO_CSS_VALUE_KIND_LIST) {
        /* A declaration's value is a list, and for `border-top: solid` that list
         * holds one component. A *component* is what this function describes, so
         * one whose list has exactly one significant child is unwrapped and a
         * longer list is not: `10px 20px` is two components and classifying it as
         * whichever one happened to be first would be answering a question nobody
         * asked. The expansion below passes components rather than lists, so this
         * is for the direct caller. */
        const robo_css_value_t *only = border_component_at(component, 0u);

        if (!only || border_component_count(component) != 1u) {
            return ROBO_CSS_BORDER_CLASS_INVALID;
        }
        component = only;
    }
    if (component->kind == ROBO_CSS_VALUE_KIND_FUNCTION) {
        robo_css_color_t color = robo_css_color_none();

        if (border_component_is_substitution(component)) {
            /* A reference whose substitution can change *which kind* a component
             * is: `--rule: 1px solid red` makes one component three, of two
             * different kinds, so classifying it now would be right for the
             * count and wrong for every part. */
            if (out_reason) {
                *out_reason =
                    "a reference whose substitution can change which kind a component is";
            }
            return ROBO_CSS_BORDER_CLASS_DEFERRED;
        }
        /* A colour function *is* the colour — `rgb(10, 20, 30)`, `hsl(0 100% 50%)`
         * — and every other function is not a border value: a border has no
         * image and no position for a `url()` or a `translate()` to go to. */
        if (robo_css_color_from_function(component, &color) ==
            ROBO_CSS_COLOR_PARSE_RESOLVED) {
            return ROBO_CSS_BORDER_CLASS_COLOR;
        }
        if (out_reason) {
            *out_reason = "not a border colour function";
        }
        return ROBO_CSS_BORDER_CLASS_INVALID;
    }
    if (component->kind != ROBO_CSS_VALUE_KIND_TOKEN) {
        return ROBO_CSS_BORDER_CLASS_INVALID;
    }
    token = robo_css_value_token(component);
    switch (token.type) {
    case ROBO_CSS_TOKEN_IDENT: {
        char keyword[64];
        size_t keyword_length = 0u;
        robo_css_slice_t text;
        robo_css_color_t named = robo_css_color_none();

        if (!robo_css_token_keyword_text(&token, keyword, sizeof(keyword),
                                         &keyword_length)) {
            return ROBO_CSS_BORDER_CLASS_INVALID;
        }
        text = robo_css_slice_from_bytes((const uint8_t *)keyword, keyword_length);
        if (robo_css_border_style_slice_is_known(text)) {
            return ROBO_CSS_BORDER_CLASS_STYLE;
        }
        if (border_keyword_is_line_width(keyword, keyword_length)) {
            return ROBO_CSS_BORDER_CLASS_WIDTH;
        }
        /* Asked of the colour *value* rather than of the name table, for the three
         * spellings that are colours and not names: `transparent`, `currentcolor`
         * and `#hex` on the branch below. A name-table lookup would have refused
         * `border: 1px solid transparent`, which is ordinary authoring. */
        switch (robo_css_color_from_value(component, &named)) {
        case ROBO_CSS_COLOR_PARSE_RESOLVED:
        case ROBO_CSS_COLOR_PARSE_CURRENT:
            return ROBO_CSS_BORDER_CLASS_COLOR;
        default:
            break;
        }
        if (out_reason) {
            *out_reason = "not a border value";
        }
        return ROBO_CSS_BORDER_CLASS_INVALID;
    }
    case ROBO_CSS_TOKEN_HASH: {
        /* The colour spellings that are not a name, and they take the same road
         * as the name does: `#fff` is a colour the 9.9 module already parses, so
         * `border: 1px solid #fff` belongs to `border-color` for the same reason
         * `border: 1px solid white` does. */
        robo_css_color_t hex = robo_css_color_none();

        if (robo_css_color_from_value(component, &hex) ==
            ROBO_CSS_COLOR_PARSE_RESOLVED) {
            return ROBO_CSS_BORDER_CLASS_COLOR;
        }
        if (out_reason) {
            *out_reason = "not a border colour";
        }
        return ROBO_CSS_BORDER_CLASS_INVALID;
    }
    case ROBO_CSS_TOKEN_DIMENSION:
    case ROBO_CSS_TOKEN_PERCENTAGE:
    case ROBO_CSS_TOKEN_NUMBER:
        /* A width, all three of them, and the last two carry a boundary this build
         * states rather than enforces: `0` is the one bare number CSS accepts as a
         * length, and a percentage is not legal for `border-width` at all — but
         * the registry admits it (see the header) and the classifier follows the
         * registry rather than overruling it. */
        return ROBO_CSS_BORDER_CLASS_WIDTH;
    default:
        return ROBO_CSS_BORDER_CLASS_INVALID;
    }
}

robo_css_border_class_t robo_css_border_classify(const robo_css_value_t *component)
{
    return border_classify_component(component, NULL);
}

/* ------------------------------------------------------------------ */
/* Reading components out of a value                                   */
/* ------------------------------------------------------------------ */

/** The @p wanted-th significant component of @p value, or NULL. */
static const robo_css_value_t *border_component_at(const robo_css_value_t *value,
                                                   size_t wanted)
{
    size_t seen = 0u;
    size_t index;

    if (!value || value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return NULL;
    }
    if (value->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return wanted == 0u && !robo_css_value_is_trivia(value) ? value : NULL;
    }
    for (index = 0u; index < value->children.count; index++) {
        const robo_css_value_t *child = &value->children.items[index];

        if (robo_css_value_is_trivia(child)) {
            continue;
        }
        if (seen == wanted) {
            return child;
        }
        seen++;
    }
    return NULL;
}

/** Number of significant components in @p value. */
static size_t border_component_count(const robo_css_value_t *value)
{
    size_t count = 0u;
    size_t index;

    if (!value || value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return 0u;
    }
    if (value->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return robo_css_value_is_trivia(value) ? 0u : 1u;
    }
    for (index = 0u; index < value->children.count; index++) {
        if (!robo_css_value_is_trivia(&value->children.items[index])) {
            count++;
        }
    }
    return count;
}

/** Build each kind's one-component list, or leave KIND_NONE. */
static robo_result_t border_build_parts(
    const robo_css_value_t *assigned[ROBO_CSS_BORDER_KIND_COUNT],
    robo_css_value_t out_parts[ROBO_CSS_BORDER_KIND_COUNT],
    robo_css_diag_t *diag)
{
    size_t kind_index;

    for (kind_index = 0u; kind_index < ROBO_CSS_BORDER_KIND_COUNT; kind_index++) {
        robo_css_value_t list = ROBO_CSS_CLEARED;
        robo_css_value_t copy = ROBO_CSS_CLEARED;
        robo_result_t result;

        if (!assigned[kind_index]) {
            /* KIND_NONE: the shorthand did not name this kind, so that longhand's
             * initial value applies. Deliberately not supplied here — the initial
             * values are the registry's (see the header). */
            continue;
        }
        robo_css_value_init(&list);
        robo_css_value_init(&copy);
        result = robo_css_value_make_list(&list, diag);
        if (result == ROBO_OK) {
            result = robo_css_value_copy(&copy, assigned[kind_index], diag);
        }
        if (result == ROBO_OK) {
            /* Built as a list rather than as the bare component, so the result has
             * the shape a *declared* longhand's value has — which is what lets it
             * go through the same normalisers with no special case. `append` moves,
             * so `copy` owns nothing afterwards. */
            result = robo_css_value_append(&list, &copy, diag);
        }
        if (result != ROBO_OK) {
            robo_css_value_dispose(&copy);
            robo_css_value_dispose(&list);
            return result;
        }
        out_parts[kind_index] = list;
    }
    return ROBO_OK;
}

robo_result_t robo_css_border_expand(const robo_css_value_t *value,
                                     robo_css_value_t out_parts[ROBO_CSS_BORDER_KIND_COUNT],
                                     robo_css_diag_t *diag)
{
    const robo_css_value_t *assigned[ROBO_CSS_BORDER_KIND_COUNT];
    size_t count;
    size_t index;
    size_t kind_index;
    robo_result_t result;

    if (!out_parts) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_INVALID_ARGUMENT,
                             "the output array is NULL");
    }
    for (kind_index = 0u; kind_index < ROBO_CSS_BORDER_KIND_COUNT; kind_index++) {
        robo_css_value_init(&out_parts[kind_index]);
        assigned[kind_index] = NULL;
    }
    if (!value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_INVALID_ARGUMENT,
                             "the shorthand value is NULL");
    }
    count = border_component_count(value);
    if (count == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_NOT_SUPPORTED,
                             "a border shorthand has no components");
    }

    for (index = 0u; index < count; index++) {
        const robo_css_value_t *component = border_component_at(value, index);
        const char *reason = NULL;
        robo_css_border_class_t outcome = border_classify_component(component, &reason);
        size_t slot;

        switch (outcome) {
        case ROBO_CSS_BORDER_CLASS_WIDTH:
            slot = (size_t)ROBO_CSS_BORDER_KIND_WIDTH;
            break;
        case ROBO_CSS_BORDER_CLASS_STYLE:
            slot = (size_t)ROBO_CSS_BORDER_KIND_STYLE;
            break;
        case ROBO_CSS_BORDER_CLASS_COLOR:
            slot = (size_t)ROBO_CSS_BORDER_KIND_COLOR;
            break;
        case ROBO_CSS_BORDER_CLASS_DEFERRED:
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_NOT_SUPPORTED,
                                 reason);
        default:
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_NOT_SUPPORTED,
                                 reason ? reason : "not a border value");
        }
        if (assigned[slot]) {
            /* `border: 1px 2px solid` is not a two-value width — a border width is
             * one value, and there is no second width for the first to be the top
             * of. The positional rows are where a pair of lengths means four sides;
             * here it means the author has written something this grammar has no
             * room for. */
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BORDER, ROBO_NOT_SUPPORTED,
                                 "a border shorthand takes one value of each kind");
        }
        assigned[slot] = component;
    }

    result = border_build_parts(assigned, out_parts, diag);
    if (result != ROBO_OK) {
        /* All or nothing: a caller that ignored the result code must not be handed
         * two of a shorthand's three kinds. */
        for (kind_index = 0u; kind_index < ROBO_CSS_BORDER_KIND_COUNT; kind_index++) {
            robo_css_value_dispose(&out_parts[kind_index]);
            robo_css_value_init(&out_parts[kind_index]);
        }
        return result;
    }
    return ROBO_OK;
}
