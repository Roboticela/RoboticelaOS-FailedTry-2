/*
 * RoboCSS box model vocabulary (Phase 9.11) — see RoboCSSBox.h.
 *
 * Three things live here and nothing else: the side order, the shorthand table,
 * and the component mapping. No property is classified, no value is interpreted
 * and no precedence is decided — those belong to the registry, the value layer
 * and the cascade respectively, and each is a place this module deliberately
 * does not reach into.
 */

#include "RoboCSS/RoboCSSBox.h"

#include "RoboCSSInternal.h"

/* ------------------------------------------------------------------ */
/* The sides                                                           */
/* ------------------------------------------------------------------ */

int robo_css_box_side_is_known(robo_css_box_side_t side)
{
    return side == ROBO_CSS_BOX_SIDE_TOP || side == ROBO_CSS_BOX_SIDE_RIGHT ||
           side == ROBO_CSS_BOX_SIDE_BOTTOM || side == ROBO_CSS_BOX_SIDE_LEFT;
}

const char *robo_css_box_side_name(robo_css_box_side_t side)
{
    switch (side) {
    case ROBO_CSS_BOX_SIDE_TOP:
        return "top";
    case ROBO_CSS_BOX_SIDE_RIGHT:
        return "right";
    case ROBO_CSS_BOX_SIDE_BOTTOM:
        return "bottom";
    case ROBO_CSS_BOX_SIDE_LEFT:
        return "left";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* The shorthands                                                      */
/* ------------------------------------------------------------------ */

/*
 * The shorthands this build expands.
 *
 * Three rows, and the reason there are exactly these three is the phase
 * boundary rather than CSS's full shorthand list. `border` is a shorthand for
 * four widths, four styles and four colours; this build has no border styles and
 * paints no border, so expanding `border` would invent three quarters of a
 * declaration. `font`, `background` and the rest are shorthands for properties
 * this build does not represent at all.
 *
 * `border-width` is here, and it is the interesting row: it is a shorthand whose
 * longhands are lengths, which is exactly the box model's subject, and it is
 * *not* here because anything draws a border.
 *
 * The longhand spellings are written out rather than spliced from the side name
 * at runtime. Building `"margin-" + name` would allocate per call, and a table
 * whose contents a reader can see is a table a test can assert against.
 */
static const robo_css_box_shorthand_t box_shorthands[] = {
    { "margin", sizeof("margin") - 1u,
      { "margin-top", "margin-right", "margin-bottom", "margin-left" } },
    { "padding", sizeof("padding") - 1u,
      { "padding-top", "padding-right", "padding-bottom", "padding-left" } },
    { "border-width", sizeof("border-width") - 1u,
      { "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width" } }
};

size_t robo_css_box_shorthand_count(void)
{
    return sizeof(box_shorthands) / sizeof(box_shorthands[0]);
}

const robo_css_box_shorthand_t *robo_css_box_shorthand_at(size_t index)
{
    if (index >= robo_css_box_shorthand_count()) {
        return NULL;
    }
    return &box_shorthands[index];
}

const robo_css_box_shorthand_t *robo_css_box_shorthand_find(robo_css_slice_t name)
{
    size_t index;

    if (robo_css_slice_is_absent(name)) {
        return NULL;
    }
    for (index = 0u; index < robo_css_box_shorthand_count(); index++) {
        const robo_css_box_shorthand_t *row = &box_shorthands[index];

        if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length, row->name)) {
            return row;
        }
    }
    return NULL;
}

const char *robo_css_box_shorthand_longhand(const robo_css_box_shorthand_t *shorthand,
                                            robo_css_box_side_t side)
{
    if (!shorthand || !robo_css_box_side_is_known(side)) {
        return NULL;
    }
    return shorthand->longhands[(size_t)side];
}

const robo_css_box_shorthand_t *robo_css_box_shorthand_of_longhand(
    robo_css_slice_t name,
    robo_css_box_side_t *out_side)
{
    size_t row_index;

    if (out_side) {
        *out_side = ROBO_CSS_BOX_SIDE_TOP;
    }
    if (robo_css_slice_is_absent(name)) {
        return NULL;
    }
    for (row_index = 0u; row_index < robo_css_box_shorthand_count(); row_index++) {
        const robo_css_box_shorthand_t *row = &box_shorthands[row_index];
        size_t side_index;

        for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side_index++) {
            if (robo_css_bytes_equal_ascii_ci(name.bytes, name.length,
                                              row->longhands[side_index])) {
                if (out_side) {
                    *out_side = (robo_css_box_side_t)side_index;
                }
                return row;
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Expansion                                                           */
/* ------------------------------------------------------------------ */

/**
 * Which component supplies @p side when there are @p component_count of them.
 *
 * Rows are component counts 1..4, columns are sides in side order. The one-value
 * row is the whole spelling for a uniform box, the two-value row is the
 * horizontal/vertical shorthand, and the three-value row is where top differs
 * from bottom and left borrows right — the row a table gets right and a
 * hand-written branch gets wrong.
 */
static const size_t box_component_map[ROBO_CSS_BOX_COMPONENT_MAX][ROBO_CSS_BOX_SIDE_COUNT] = {
    /* 1: a a a a */ { 0u, 0u, 0u, 0u },
    /* 2: a b a b */ { 0u, 1u, 0u, 1u },
    /* 3: a b c b */ { 0u, 1u, 2u, 1u },
    /* 4: a b c d */ { 0u, 1u, 2u, 3u }
};

/**
 * The @p wanted-th significant component of @p value, or NULL.
 *
 * `significant` is what the shorthand means: trivia is skipped, so `10px 20px`
 * is two components rather than three. A value that is a single component rather
 * than a list — a hand-built token, or a function — answers itself for index 0.
 */
static const robo_css_value_t *box_component_at(const robo_css_value_t *value,
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

size_t robo_css_box_component_count(const robo_css_value_t *shorthand_value)
{
    size_t count = 0u;
    size_t index;

    if (!shorthand_value || shorthand_value->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return 0u;
    }
    if (shorthand_value->kind != ROBO_CSS_VALUE_KIND_LIST) {
        return robo_css_value_is_trivia(shorthand_value) ? 0u : 1u;
    }
    for (index = 0u; index < shorthand_value->children.count; index++) {
        if (!robo_css_value_is_trivia(&shorthand_value->children.items[index])) {
            count++;
        }
    }
    return count;
}

/**
 * Non-zero when @p value is a reference whose substitution can change how many
 * components a shorthand has: `var()` or `env()`.
 *
 * The two names are listed rather than derived because there are no others — a
 * `calc()` is one component whatever it contains, and a nested `var()` inside a
 * `calc()` is the calc's business, not the shorthand's. Nothing here evaluates
 * or substitutes; it decides only whether splitting now would be premature,
 * which is the one question whose answer can be wrong in a way that looks right.
 */
static int box_component_is_substitution(const robo_css_value_t *value)
{
    return robo_css_value_function_named(value, "var") ||
           robo_css_value_function_named(value, "env");
}

int robo_css_box_is_expandable(const robo_css_value_t *shorthand_value,
                               size_t *out_component_count)
{
    size_t count;
    size_t index;

    if (out_component_count) {
        *out_component_count = 0u;
    }
    count = robo_css_box_component_count(shorthand_value);
    if (count == 0u || count > (size_t)ROBO_CSS_BOX_COMPONENT_MAX) {
        return 0;
    }
    for (index = 0u; index < count; index++) {
        if (box_component_is_substitution(box_component_at(shorthand_value, index))) {
            return 0;
        }
    }
    if (out_component_count) {
        *out_component_count = count;
    }
    return 1;
}

robo_result_t robo_css_box_expand_side(const robo_css_value_t *shorthand_value,
                                       size_t component_count,
                                       robo_css_box_side_t side,
                                       robo_css_value_t *out,
                                       robo_css_diag_t *diag)
{
    const robo_css_value_t *component;
    robo_css_value_t list = ROBO_CSS_CLEARED;
    robo_css_value_t copy = ROBO_CSS_CLEARED;
    size_t component_index;
    robo_result_t result;

    if (!out) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BOX, ROBO_INVALID_ARGUMENT,
                             "output is NULL");
    }
    robo_css_value_dispose(out);
    if (!shorthand_value) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BOX, ROBO_INVALID_ARGUMENT,
                             "the shorthand value is NULL");
    }
    if (!robo_css_box_side_is_known(side)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BOX, ROBO_INVALID_ARGUMENT,
                             "not one of a box's four sides");
    }
    if (component_count == 0u || component_count > (size_t)ROBO_CSS_BOX_COMPONENT_MAX) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BOX, ROBO_NOT_SUPPORTED,
                             "a shorthand expands from one to four components");
    }
    component_index = box_component_map[component_count - 1u][(size_t)side];
    component = box_component_at(shorthand_value, component_index);
    if (!component) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BOX, ROBO_NOT_SUPPORTED,
                             "the shorthand has no component for that side");
    }

    /* Built as a list of one rather than as the bare component, so that the
     * result has the shape a *declared* longhand's value has — which is what
     * lets it go through the same normalisers without a special case. */
    robo_css_value_init(&list);
    robo_css_value_init(&copy);
    result = robo_css_value_make_list(&list, diag);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&list);
        return result;
    }
    result = robo_css_value_copy(&copy, component, diag);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&copy);
        robo_css_value_dispose(&list);
        return result;
    }
    result = robo_css_value_append(&list, &copy, diag);
    if (result != ROBO_OK) {
        robo_css_value_dispose(&copy);
        robo_css_value_dispose(&list);
        return result;
    }
    *out = list;
    return ROBO_OK;
}

robo_result_t robo_css_box_expand(const robo_css_value_t *shorthand_value,
                                  size_t component_count,
                                  robo_css_value_t out_sides[ROBO_CSS_BOX_SIDE_COUNT],
                                  robo_css_diag_t *diag)
{
    size_t index;
    robo_result_t result;

    if (!out_sides) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_BOX, ROBO_INVALID_ARGUMENT,
                             "the output array is NULL");
    }
    for (index = 0u; index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; index++) {
        robo_css_value_init(&out_sides[index]);
    }
    for (index = 0u; index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; index++) {
        result = robo_css_box_expand_side(shorthand_value, component_count,
                                          (robo_css_box_side_t)index,
                                          &out_sides[index], diag);
        if (result != ROBO_OK) {
            /* All or nothing: two sides of a four-sided box is not a box, and a
             * caller that ignored the result code must not be handed one. */
            size_t undo;

            for (undo = 0u; undo < (size_t)ROBO_CSS_BOX_SIDE_COUNT; undo++) {
                robo_css_value_dispose(&out_sides[undo]);
                robo_css_value_init(&out_sides[undo]);
            }
            return result;
        }
    }
    return ROBO_OK;
}
