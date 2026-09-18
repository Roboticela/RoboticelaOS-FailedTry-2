/*
 * RoboCSS box model suite (Phase 9.11) — the vocabulary, the component mapping,
 * the three value shapes, and what the pipeline makes of a shorthand.
 *
 * Four groups, in the order the module is built:
 *
 *   the vocabulary  the four sides and the three shorthands, every row walked
 *                   rather than a representative one typed out
 *   expansion       component counting, the 1/2/3/4 mapping, and the two
 *                   refusals — five components, and a `var()` that would change
 *                   the count once substituted
 *   the shapes      `auto`, a length, a percentage, and the width keywords, each
 *                   asserted through *declaration text* so the tokenizer's route
 *                   is the one under test
 *   integration     a shorthand through a real element: expansion, the cascade's
 *                   answer when a longhand competes, the CSS-wide keywords, and
 *                   non-inheritance
 *
 * Nothing here asserts a pixel. A box model at this phase is a set of *values*
 * with a shape, and every expectation below is about the value's type, its
 * canonical text, or where it came from — which is the whole difference between a
 * computed value and a used one.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Reading a value out of declaration text                             */
/* ------------------------------------------------------------------ */

/** The specified value of the single declaration in @p text. */
static const robo_css_value_t *css_box_value_of(const char *text,
                                               robo_css_declaration_list_t *list)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    css_fixture();
    ROBO_CHECK_EQ_INT(css_parse_declarations(text, list, &error), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_css_declaration_list_count(list), 1);
    return robo_css_declaration_value(robo_css_declaration_list_at(list, 0u));
}

/** The shape of @p side's component of the shorthand written in @p text. */
static void css_box_side_shape(const char *text,
                               size_t component_count,
                               robo_css_box_side_t side,
                               char *out,
                               size_t capacity)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_box_value_of(text, &list);
    robo_css_value_t component = ROBO_CSS_CLEARED;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    ROBO_CHECK_EQ_INT(
        robo_css_box_expand_side(value, component_count, side, &component, NULL),
        ROBO_OK);
    css_shape(&component, out, capacity);
    robo_css_value_dispose(&component);
    robo_css_declaration_list_dispose(&list);
}

/** One side of a shorthand, as shape and as count, from one assertion. */
static void css_box_side_is(const char *text,
                            size_t component_count,
                            robo_css_box_side_t side,
                            const char *expected)
{
    char shape[256];

    css_box_side_shape(text, component_count, side, shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, expected);
}

/** The component count of @p text, and whether it can be distributed. */
static void css_box_count_is(const char *text, size_t expected_count, int expandable)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_box_value_of(text, &list);
    size_t reported = 0u;
    int ok;

    ROBO_CHECK_EQ_INT((int)robo_css_box_component_count(value), (int)expected_count);
    ok = robo_css_box_is_expandable(value, &reported);
    ROBO_CHECK_EQ_INT(ok, expandable);
    /* The count is only written when the answer is yes, so a refused value cannot
     * hand a caller a mapping to apply. */
    ROBO_CHECK_EQ_INT((int)reported, expandable ? (int)expected_count : 0);
    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * The four sides, and the order they are in.
 *
 * The order is the assertion, not the spelling: a table that listed the sides
 * differently would transpose every two- and three-component shorthand, and the
 * only way to see that from the vocabulary is to walk it in the defined order and
 * read the names back.
 */
static void test_box_side_vocabulary(void)
{
    static const char *const names[] = { "top", "right", "bottom", "left" };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BOX_SIDE_COUNT, 4);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        robo_css_box_side_t side = (robo_css_box_side_t)index;

        ROBO_CHECK_EQ_INT(robo_css_box_side_is_known(side), 1);
        ROBO_CHECK_STR_EQ(robo_css_box_side_name(side), names[index]);
    }
    ROBO_CHECK_EQ_INT(robo_css_box_side_is_known((robo_css_box_side_t)4), 0);
    ROBO_CHECK_EQ_INT(robo_css_box_side_is_known((robo_css_box_side_t)-1), 0);
    ROBO_CHECK_STR_EQ(robo_css_box_side_name((robo_css_box_side_t)9), "unknown");
}

/**
 * Five shorthands, each with four longhands, walked rather than sampled.
 *
 * `border-width` is the row worth naming: it is a shorthand this build expands
 * while implementing no border *painting* at all, because a `border-width: 1px 2px`
 * that no longhand ever saw would be a declaration no later phase could consume.
 * `border-style` and `border-color` joined it in 9.15 — the same 1/2/3/4 mapping
 * over the same four sides, which is exactly why they reuse this table rather than
 * getting a copy of it.
 */
static void test_box_shorthand_table(void)
{
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)robo_css_box_shorthand_count(), 5);

    {
        static const char *const expected[][4] = {
            { "margin-top", "margin-right", "margin-bottom", "margin-left" },
            { "padding-top", "padding-right", "padding-bottom", "padding-left" },
            { "border-top-width", "border-right-width", "border-bottom-width",
              "border-left-width" },
            { "border-top-style", "border-right-style", "border-bottom-style",
              "border-left-style" },
            { "border-top-color", "border-right-color", "border-bottom-color",
              "border-left-color" }
        };
        static const char *const shorthand_names[] = {
            "margin", "padding", "border-width", "border-style", "border-color"
        };

        for (index = 0u; index < 5u; index++) {
            const robo_css_box_shorthand_t *row = robo_css_box_shorthand_at(index);
            size_t side_index;

            ROBO_CHECK(row != NULL);
            if (!row) {
                continue;
            }
            ROBO_CHECK_STR_EQ(row->name, shorthand_names[index]);
            ROBO_CHECK_EQ_INT((int)row->name_length, (int)strlen(row->name));
            for (side_index = 0u; side_index < 4u; side_index++) {
                const char *longhand = robo_css_box_shorthand_longhand(
                    row, (robo_css_box_side_t)side_index);
                robo_css_box_side_t back_side = ROBO_CSS_BOX_SIDE_TOP;
                const robo_css_box_shorthand_t *back;

                ROBO_CHECK_STR_EQ(longhand, expected[index][side_index]);
                /* The lookup runs both ways, so a caller holding a *declaration*
                 * can find the shorthand that owns it. */
                back = robo_css_box_shorthand_of_longhand(
                    robo_css_slice_from_text(longhand), &back_side);
                ROBO_CHECK(back == row);
                ROBO_CHECK_EQ_INT((int)back_side, (int)side_index);
            }
        }
    }

    /* Out of range, and past the end. */
    ROBO_CHECK(robo_css_box_shorthand_at(5u) == NULL);
    ROBO_CHECK(robo_css_box_shorthand_at((size_t)-1) == NULL);

    /* Found by name, case-insensitively — CSS compares property names that way. */
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text("margin")) != NULL);
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text("MARGIN")) != NULL);
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text("border-width")) !=
               NULL);
    /* A longhand is not a shorthand, and neither is anything else. */
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text("margin-top")) ==
               NULL);
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text("border")) == NULL);
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text("")) == NULL);
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_empty()) == NULL);

    /* `border` is deliberately *not* a row here: it covers all four sides of all
     * three kinds, and a longhand it sets can also be set by `border-top` and by
     * `border-width` at once — which is a decision no positional row can take, so
     * the border family owns that distribution (9.15). Its longhands are still
     * reachable both ways. */
    ROBO_CHECK(robo_css_box_shorthand_of_longhand(
                   robo_css_slice_from_text("border-top-width"), NULL) != NULL);
    ROBO_CHECK(robo_css_box_shorthand_of_longhand(
                   robo_css_slice_from_text("border-top-style"), NULL) != NULL);
    ROBO_CHECK(robo_css_box_shorthand_of_longhand(
                   robo_css_slice_from_text("margin-inline-start"), NULL) == NULL);

    /* A NULL shorthand has no longhands, and neither has an unknown side. */
    ROBO_CHECK(robo_css_box_shorthand_longhand(NULL, ROBO_CSS_BOX_SIDE_TOP) == NULL);
    ROBO_CHECK(robo_css_box_shorthand_longhand(robo_css_box_shorthand_at(0u),
                                               (robo_css_box_side_t)7) == NULL);
}

/* ------------------------------------------------------------------ */
/* Expansion                                                           */
/* ------------------------------------------------------------------ */

/**
 * The mapping, all four arities, read out of a table rather than restated.
 *
 * Every case below is the shortest spelling that writes a distinct value per
 * side, so the four rows are the whole rule. The values are `1px`..`4px` so that a
 * side answered with the wrong component is visible in the shape and not merely
 * off by a number.
 */
static void test_box_component_mapping(void)
{
    css_fixture();

    css_box_count_is("m: 1px", 1u, 1);
    css_box_count_is("m: 1px 2px", 2u, 1);
    css_box_count_is("m: 1px 2px 3px", 3u, 1);
    css_box_count_is("m: 1px 2px 3px 4px", 4u, 1);

    /* One component: all four. */
    css_box_side_is("m: 1px", 1u, ROBO_CSS_BOX_SIDE_TOP, "1px");
    css_box_side_is("m: 1px", 1u, ROBO_CSS_BOX_SIDE_RIGHT, "1px");
    css_box_side_is("m: 1px", 1u, ROBO_CSS_BOX_SIDE_BOTTOM, "1px");
    css_box_side_is("m: 1px", 1u, ROBO_CSS_BOX_SIDE_LEFT, "1px");

    /* Two: vertical, then horizontal. */
    css_box_side_is("m: 1px 2px", 2u, ROBO_CSS_BOX_SIDE_TOP, "1px");
    css_box_side_is("m: 1px 2px", 2u, ROBO_CSS_BOX_SIDE_RIGHT, "2px");
    css_box_side_is("m: 1px 2px", 2u, ROBO_CSS_BOX_SIDE_BOTTOM, "1px");
    css_box_side_is("m: 1px 2px", 2u, ROBO_CSS_BOX_SIDE_LEFT, "2px");

    /* Three: top differs from bottom, and left borrows right. This is the row a
     * hand-written branch gets wrong. */
    css_box_side_is("m: 1px 2px 3px", 3u, ROBO_CSS_BOX_SIDE_TOP, "1px");
    css_box_side_is("m: 1px 2px 3px", 3u, ROBO_CSS_BOX_SIDE_RIGHT, "2px");
    css_box_side_is("m: 1px 2px 3px", 3u, ROBO_CSS_BOX_SIDE_BOTTOM, "3px");
    css_box_side_is("m: 1px 2px 3px", 3u, ROBO_CSS_BOX_SIDE_LEFT, "2px");

    /* Four: clockwise from the top. */
    css_box_side_is("m: 1px 2px 3px 4px", 4u, ROBO_CSS_BOX_SIDE_TOP, "1px");
    css_box_side_is("m: 1px 2px 3px 4px", 4u, ROBO_CSS_BOX_SIDE_RIGHT, "2px");
    css_box_side_is("m: 1px 2px 3px 4px", 4u, ROBO_CSS_BOX_SIDE_BOTTOM, "3px");
    css_box_side_is("m: 1px 2px 3px 4px", 4u, ROBO_CSS_BOX_SIDE_LEFT, "4px");

    /* Whitespace inside a value is not a component: `1px   2px` is two, and the
     * interior trivia the grammar keeps is skipped rather than counted. */
    css_box_count_is("m: 1px   2px", 2u, 1);
    css_box_count_is("m: 1px\n\t2px", 2u, 1);
    /* A percentage and a relative unit are components like any other here; what
     * they *mean* is the value layer's question, not the vocabulary's. */
    css_box_count_is("m: 50% 2em auto", 3u, 1);
    css_box_side_is("m: 50% 2em auto", 3u, ROBO_CSS_BOX_SIDE_LEFT, "2em");
    /* An absolute unit keeps its author's spelling in the *component* — the
     * component is the specified value, and normalisation is a later stage. */
    css_box_side_is("m: 1in 2in", 2u, ROBO_CSS_BOX_SIDE_LEFT, "2in");

    /* The all-or-nothing door agrees with the one-at-a-time door. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value = css_box_value_of("m: 1px 2px 3px 4px", &list);
        robo_css_value_t sides[ROBO_CSS_BOX_SIDE_COUNT];
        static const char *const expected[] = { "1px", "2px", "3px", "4px" };
        size_t index;

        ROBO_CHECK_EQ_INT(robo_css_box_expand(value, 4u, sides, NULL), ROBO_OK);
        for (index = 0u; index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; index++) {
            char shape[64];

            css_shape(&sides[index], shape, sizeof(shape));
            ROBO_CHECK_STR_EQ(shape, expected[index]);
            robo_css_value_dispose(&sides[index]);
        }
        robo_css_declaration_list_dispose(&list);
    }
}

/**
 * What cannot be distributed, and why each one cannot.
 *
 * Two refusals, and they are different in kind. Five components is an *invalid
 * declaration* that the grammar accepted because validity is not syntax. A
 * `var()` is a perfectly good declaration whose component count is not known yet,
 * because substitution happens before expansion in CSS — so splitting it now would
 * assign the whole reference to every side and be wrong on the first real case.
 */
static void test_box_expansion_refusals(void)
{
    css_fixture();

    /* Five is not a longer shorthand. */
    css_box_count_is("m: 1px 2px 3px 4px 5px", 5u, 0);
    /* Six, so the refusal is a bound rather than a special case. */
    css_box_count_is("m: 1px 2px 3px 4px 5px 6px", 6u, 0);
    /* Nothing at all. */
    css_box_count_is("m: var(--x)", 1u, 0);
    css_box_count_is("m: var(--x) 2px", 2u, 0);
    css_box_count_is("m: 1px env(--y)", 2u, 0);
    /* ...and the reason is the reference, not the arity: the same three values
     * without a reference distribute. */
    css_box_count_is("m: calc(1px + 1px) 2px 3px", 3u, 1);

    /* Expanding an arity there is no row for is refused rather than clamped. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        const robo_css_value_t *value = css_box_value_of("m: 1px 2px", &list);
        robo_css_value_t component = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_box_expand_side(value, 0u, ROBO_CSS_BOX_SIDE_TOP,
                                                   &component, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK(robo_css_value_is_empty(&component));
        ROBO_CHECK_EQ_INT(robo_css_box_expand_side(value, 5u, ROBO_CSS_BOX_SIDE_TOP,
                                                   &component, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK(robo_css_value_is_empty(&component));
        /* An unknown side is refused before anything is built. */
        ROBO_CHECK_EQ_INT(robo_css_box_expand_side(value, 2u, (robo_css_box_side_t)11,
                                                   &component, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_box_expand_side(NULL, 2u, ROBO_CSS_BOX_SIDE_TOP,
                                                   &component, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_box_expand(value, 2u, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        robo_css_declaration_list_dispose(&list);
    }

    /* A NULL value and a cleared one have no components; both are safe. */
    {
        robo_css_value_t empty = ROBO_CSS_CLEARED;
        size_t count = 99u;

        robo_css_value_init(&empty);
        ROBO_CHECK_EQ_INT((int)robo_css_box_component_count(NULL), 0);
        ROBO_CHECK_EQ_INT((int)robo_css_box_component_count(&empty), 0);
        ROBO_CHECK_EQ_INT(robo_css_box_is_expandable(NULL, &count), 0);
        ROBO_CHECK_EQ_INT((int)count, 0);
        ROBO_CHECK_EQ_INT(robo_css_box_is_expandable(&empty, NULL), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The three value shapes                                              */
/* ------------------------------------------------------------------ */

/**
 * What each box-model family accepts, through declaration text.
 *
 * The three families differ by *vocabulary*, and that is the whole reason there
 * are three shapes instead of one: `auto` is a margin and is not a padding,
 * `medium` is a border width and is not a margin, and `min-content` is a width and
 * is neither. A shape that accepted every length-family keyword everywhere would
 * make `padding: auto` computable, and CSS says it is a mistake.
 */
static void test_box_shapes(void)
{
    css_fixture();

    /* --- margin: a length, a percentage, or `auto` --- */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: 10px", "10px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: 1in", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: 50%", "50%");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: auto", "auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: 0", "0px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: -10px", "-10px");
    /* Not the family's vocabulary. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: normal", "not a length, a percentage or auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: min-content", "not a length, a percentage or auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: red", "not a length, a percentage or auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: 1foo", "unknown length unit");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "m: 10px 20px", "not a single length component");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET,
                                       "m: auto"),
                      "keyword");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET,
                                       "m: 50%"),
                      "percentage");

    /* --- padding: the same, minus `auto` --- */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: 10px", "10px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: 2.54cm", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: 12.5%", "12.5%");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: 0", "0px");
    /* The assertion the family exists for: a padding has nothing to distribute,
     * so `auto` is not a value it has. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: auto", "not a padding length");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: normal", "not a padding length");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: red", "not a padding length");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, "p: 1foo", "unknown length unit");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET,
                                       "p: auto"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET,
                                       "p: 10px"),
                      "length");

    /* --- border width: a length, or one of the three keywords --- */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: 2px", "2px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: 1in", "96px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: thin", "thin");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: medium", "medium");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: thick", "thick");
    /* The keywords stay keywords: what `medium` is in pixels depends on the
     * border's style and the device, and that is a used value, not this one. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH,
                                       "b: medium"),
                      "keyword");
    /* A negative width is not legal CSS... but this layer knows a value's *shape*,
     * not a property's range, and 9.8's contract says a length is a length. The
     * assertion records that boundary rather than pretending it is enforced here. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: -1px", "-1px");
    /* Not a width. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: auto", "not a border width keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: 50%", "50%");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: thin thick", "not a single length component");

    /* The name of each shape is what a diagnostic prints for it. */
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET),
                      "edge offset");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET),
                      "edge inset");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH),
                      "line width");
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          (robo_css_property_computation_t)ROBO_CSS_PROPERTY_COMPUTATION_COUNT),
                      0);
}

/* ------------------------------------------------------------------ */
/* Integration: a shorthand through a real element                     */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void css_box_compute_of(css_inherit_fixture_t *fixture,
                               robo_css_element_ref_t ref,
                               robo_css_computed_style_t *out)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(css_inherit_context(fixture),
                                                          ref, out, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
}

/**
 * A shorthand arrives at four computed longhands, and the shorthand itself is
 * still there.
 *
 * The four are the point of the phase: everything downstream is written against
 * the longhands, so `padding: 8px 12px` has to *become* four values rather than
 * stay one declaration nothing can consume. The shorthand's own entry staying put
 * is the other half of the same decision — the author's text is not destroyed on
 * the way, and a caller reading `padding` sees what was written.
 */
static void test_box_computed_longhands(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *box = NULL;
    robo_css_computed_style_t *single = NULL;
    robo_css_element_ref_t div;
    robo_css_element_ref_t uniform;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "box", NULL);
    uniform = css_host_add(css_inherit_host(&fixture), "div", "even", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#box { padding: 8px 12px 4px; border-width: 1px 2px;"
                      " margin: 10px; box-sizing: border-box }"
                      "#even { margin: 0 auto }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&box, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&single, NULL), ROBO_OK);
    css_box_compute_of(&fixture, div, box);
    css_box_compute_of(&fixture, uniform, single);

    /* Three components: top 8, right/left 12, bottom 4. */
    css_computed_is(box, "padding-top", "8px");
    css_computed_is(box, "padding-right", "12px");
    css_computed_is(box, "padding-bottom", "4px");
    css_computed_is(box, "padding-left", "12px");
    css_computed_flags_are(box, "padding-left", "declared");
    /* Two: vertical, then horizontal. */
    css_computed_is(box, "border-top-width", "1px");
    css_computed_is(box, "border-right-width", "2px");
    css_computed_is(box, "border-bottom-width", "1px");
    css_computed_is(box, "border-left-width", "2px");
    /* One: all four. */
    css_computed_is(box, "margin-top", "10px");
    css_computed_is(box, "margin-left", "10px");
    /* The shorthand's own entry survives, unexpanded and uninterpreted — the
     * author's text is not destroyed on the way to the four longhands. */
    css_computed_is(box, "padding", "8px 12px 4px");
    css_computed_is(box, "border-width", "1px 2px");
    /* `box-sizing` is a value model with no layout behind it: the keyword is what
     * a declaration means here, and which box a width applies to is a later
     * phase's question. */
    css_computed_is(box, "box-sizing", "border-box");
    /* `margin: 0 auto` is the two-component form, and `auto` survives as the
     * keyword it is rather than becoming a number. */
    css_computed_is(single, "margin-top", "0px");
    css_computed_is(single, "margin-right", "auto");
    css_computed_is(single, "margin-left", "auto");

    robo_css_computed_style_destroy(single);
    robo_css_computed_style_destroy(box);
    css_inherit_end(&fixture);
}

/**
 * One edge, spoken for twice — and the cascade decides which time counts.
 *
 * This is the assertion the phase's shorthand work exists for. The two orderings
 * below are the same two declarations with their order swapped, and CSS's answer
 * is different for each: document order is the last of the three keys, so within
 * one rule it is simply "later wins". Nothing in the box model weighs a level or
 * compares a specificity to work that out; it asks the cascade the same question
 * it asks for a single property.
 */
static void test_box_cascade_shorthand_versus_longhand(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *longhand_later = NULL;
    robo_css_computed_style_t *shorthand_later = NULL;
    robo_css_computed_style_t *zero_then_longhand = NULL;
    robo_css_element_ref_t a;
    robo_css_element_ref_t b;
    robo_css_element_ref_t c;

    css_fixture();
    css_inherit_begin(&fixture);
    a = css_host_add(css_inherit_host(&fixture), "div", "a", NULL);
    b = css_host_add(css_inherit_host(&fixture), "div", "b", NULL);
    c = css_host_add(css_inherit_host(&fixture), "div", "c", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      /* The longhand is later, so it wins that one edge only. */
                      "#a { margin: 10px; margin-left: 20px }"
                      /* The shorthand is later, so it overwrites the longhand. */
                      "#b { margin-left: 20px; margin: 10px }"
                      /* And a shorthand reset only erases what came before it. */
                      "#c { margin: 10px; margin: 0; margin-top: 5px }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&longhand_later, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&shorthand_later, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&zero_then_longhand, NULL), ROBO_OK);
    css_box_compute_of(&fixture, a, longhand_later);
    css_box_compute_of(&fixture, b, shorthand_later);
    css_box_compute_of(&fixture, c, zero_then_longhand);

    css_computed_is(longhand_later, "margin-top", "10px");
    css_computed_is(longhand_later, "margin-right", "10px");
    css_computed_is(longhand_later, "margin-bottom", "10px");
    css_computed_is(longhand_later, "margin-left", "20px");

    css_computed_is(shorthand_later, "margin-top", "10px");
    css_computed_is(shorthand_later, "margin-left", "10px");

    css_computed_is(zero_then_longhand, "margin-top", "5px");
    css_computed_is(zero_then_longhand, "margin-right", "0px");
    css_computed_is(zero_then_longhand, "margin-bottom", "0px");
    css_computed_is(zero_then_longhand, "margin-left", "0px");

    robo_css_computed_style_destroy(zero_then_longhand);
    robo_css_computed_style_destroy(shorthand_later);
    robo_css_computed_style_destroy(longhand_later);
    css_inherit_end(&fixture);
}

/**
 * Specificity, origin and `!important`, across a shorthand and a longhand.
 *
 * The cascade's three keys are not restated by the box model, so what is asserted
 * here is that they still decide the edge when the two competing declarations
 * carry *different property names*. Every one of these would be got wrong by an
 * implementation that resolved a longhand from its own declaration and then let a
 * shorthand overwrite it unconditionally.
 */
static void test_box_cascade_specificity_and_importance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *box = NULL;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "panel", "box");
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      /* A type selector loses to an id selector, whichever of the
                       * two is the shorthand and whichever came first. */
                      "div { margin-left: 20px }"
                      "#panel { margin: 10px }"
                      /* An author declaration cannot outrank an important one, and
                       * the important longhand here comes *before* the shorthand. */
                      "#panel { padding: 1px !important }"
                      "#panel { padding-top: 9px }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&box, NULL), ROBO_OK);
    css_box_compute_of(&fixture, div, box);

    css_computed_is(box, "margin-left", "10px");
    css_computed_is(box, "margin-top", "10px");
    css_computed_is(box, "padding-top", "1px");
    css_computed_is(box, "padding-left", "1px");

    robo_css_computed_style_destroy(box);
    css_inherit_end(&fixture);
}

/**
 * The CSS-wide keywords, on a shorthand and on a longhand.
 *
 * `margin: inherit` and `margin-left: inherit` mean the same thing to all four
 * edges, and that is by construction rather than by two implementations agreeing:
 * a distributed longhand goes through the same keyword handling a declared one
 * does. `initial` and `unset` land on the longhand's own initial — zero — and not
 * on the parent's spacing, which is the only correct answer for a property that
 * does not inherit.
 */
static void test_box_keywords(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent = NULL;
    robo_css_computed_style_t *kid = NULL;
    robo_css_element_ref_t outer;
    robo_css_element_ref_t child;

    css_fixture();
    css_inherit_begin(&fixture);
    outer = css_host_add(css_inherit_host(&fixture), "div", "outer", NULL);
    child = css_host_add(css_inherit_host(&fixture), "div", "inner", NULL);
    css_host_set_parent(css_inherit_host(&fixture), child, outer);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#outer { margin: 25px }"
                      "#inner { margin: inherit; padding: initial;"
                      " border-width: unset }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&kid, NULL), ROBO_OK);
    css_box_compute_of(&fixture, outer, parent);
    css_box_compute_of(&fixture, child, kid);

    css_computed_is(parent, "margin-left", "25px");
    /* `inherit` on the shorthand is the parent's *computed* value for every edge,
     * which is `25px` and not the text `25px` — the two would differ the moment the
     * parent wrote `1in`. */
    css_computed_is(kid, "margin-top", "25px");
    css_computed_is(kid, "margin-left", "25px");
    css_computed_flags_are(kid, "margin-left", "inherited");
    /* `initial` and `unset` on a non-inherited property both land on the edge's own
     * initial value: zero for a padding, `medium` for a border width. */
    css_computed_is(kid, "padding-top", "0px");
    css_computed_flags_are(kid, "padding-top", "default");
    css_computed_is(kid, "border-top-width", "medium");
    css_computed_flags_are(kid, "border-top-width", "default");

    robo_css_computed_style_destroy(kid);
    robo_css_computed_style_destroy(parent);
    css_inherit_end(&fixture);
}

/**
 * A box has edges nobody mentioned, and they are absent rather than invented.
 *
 * `margin-top: auto` on every element because nobody declared it would put a value
 * in a resolution that no input produced — which is the same rule the inherited
 * properties have had since 9.7, applied to the family that must not propagate.
 * The *initial value* is still reachable, through the registry, which is where a
 * property's initial value belongs.
 */
static void test_box_non_inheritance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t parent = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t child = ROBO_CSS_CLEARED;
    robo_css_computed_style_t *kid = NULL;
    robo_css_cascade_t parent_cascade = ROBO_CSS_CLEARED;
    robo_css_cascade_t child_cascade = ROBO_CSS_CLEARED;
    robo_css_element_ref_t outer;
    robo_css_element_ref_t inner;

    css_fixture();
    css_inherit_begin(&fixture);
    outer = css_host_add(css_inherit_host(&fixture), "div", "outer", NULL);
    inner = css_host_add(css_inherit_host(&fixture), "div", "inner", NULL);
    css_host_set_parent(css_inherit_host(&fixture), inner, outer);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#outer { margin: 10px; padding: 10px; width: 100px }");
    css_cascade_collect(&fixture.cascade, outer, &parent_cascade, NULL);
    css_cascade_collect(&fixture.cascade, inner, &child_cascade, NULL);
    css_inherit_resolve(&fixture, &parent_cascade, NULL, &parent);
    css_inherit_resolve(&fixture, &child_cascade, &parent, &child);

    /* The parent has all four edges; the child has none of them, by name. */
    css_inherit_is(&parent, "margin-left", "10px", "declared");
    css_inherit_is(&child, "margin-left", "<absent>", "<absent>");
    css_inherit_is(&child, "padding-top", "<absent>", "<absent>");
    css_inherit_is(&child, "width", "<absent>", "<absent>");
    css_inherit_is(&child, "box-sizing", "<absent>", "<absent>");
    /* An inherited property would have come across — from the parent's own
     * default, which is why its provenance is the parent and not the registry —
     * and that is what makes the absences above a *classification* rather than
     * an empty result. */
    css_inherit_is(&child, "color", "black", "parent");

    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&kid, NULL), ROBO_OK);
    css_box_compute_of(&fixture, inner, kid);
    /* The initial value is still reachable — from the registry, not from the
     * parent's box. */
    {
        robo_css_computed_value_t from_initial = ROBO_CSS_CLEARED;
        int initial = 0;
        size_t written = 0u;
        char shape[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

        ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                              css_inherit_context(&fixture), kid,
                              robo_css_slice_from_text("margin-left"), &from_initial,
                              &initial, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(initial, 1);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&from_initial, shape,
                                                        sizeof(shape), &written, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(shape, "0px");
        robo_css_computed_value_dispose(&from_initial);
    }

    robo_css_computed_style_destroy(kid);
    robo_css_inherited_style_dispose(&child);
    robo_css_inherited_style_dispose(&parent);
    robo_css_cascade_dispose(&child_cascade);
    robo_css_cascade_dispose(&parent_cascade);
    css_inherit_end(&fixture);
}

/**
 * A shorthand that cannot be distributed is *reported*, not silently skipped.
 *
 * The failing case is the quiet one: the shorthand's own entry is there with the
 * author's text intact, so a consumer reading the style would otherwise see a
 * declaration, no longhands, and no way to tell that anything was left undone.
 */
static void test_box_unexpanded_shorthand(void)
{
    css_inherit_fixture_t fixture;
    robo_css_inherited_style_t resolved = ROBO_CSS_CLEARED;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_element_ref_t div;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "box", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#box { margin: 1px 2px 3px 4px 5px; padding: var(--edges) }");
    css_cascade_collect(&fixture.cascade, div, &cascade, NULL);
    css_inherit_resolve(&fixture, &cascade, NULL, &resolved);

    /* The shorthand's own entry is intact — the author's declaration is never
     * thrown away — and no longhand was invented from it. */
    css_inherit_is(&resolved, "margin", "1px,_,2px,_,3px,_,4px,_,5px", "declared");
    css_inherit_is(&resolved, "margin-top", "<absent>", "<absent>");
    css_inherit_is(&resolved, "padding", "var(--edges)", "declared");
    css_inherit_is(&resolved, "padding-left", "<absent>", "<absent>");
    /* Two shorthands, two counts, and the result is marked incomplete because
     * eight edges are unanswerable. */
    ROBO_CHECK_EQ_INT((int)robo_css_inherited_style_shorthands_unexpanded(&resolved),
                      2);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&resolved), 0);

    robo_css_inherited_style_dispose(&resolved);
    robo_css_cascade_dispose(&cascade);

    css_inherit_end(&fixture);

    /* A shorthand that *does* distribute leaves the counter at zero. Its own
     * fixture, because a context's sheets are shared by every element in it and
     * the two failing declarations above are still in the first one. */
    {
        css_inherit_fixture_t clean;
        robo_css_cascade_t ok_cascade = ROBO_CSS_CLEARED;
        robo_css_inherited_style_t ok = ROBO_CSS_CLEARED;
        robo_css_element_ref_t clean_div;

        css_inherit_begin(&clean);
        clean_div = css_host_add(css_inherit_host(&clean), "div", "box", NULL);
        css_host_seal(css_inherit_host(&clean));
        css_inherit_sheet(&clean, ROBO_CSS_ORIGIN_AUTHOR,
                          "#box { margin: 1px 2px 3px 4px; padding: 10px 20px;"
                          " border-width: thin thick thin thick }");
        css_cascade_collect(&clean.cascade, clean_div, &ok_cascade, NULL);
        css_inherit_resolve(&clean, &ok_cascade, NULL, &ok);

        ROBO_CHECK_EQ_INT(
            (int)robo_css_inherited_style_shorthands_unexpanded(&ok), 0);
        ROBO_CHECK_EQ_INT(robo_css_inherited_style_is_complete(&ok), 1);
        /* All three shorthands distributed, each arity arriving at the right
         * edges — which is what makes the zero above a statement about the
         * distribution and not about the counter never being written. */
        css_inherit_is(&ok, "margin-left", "4px", "declared");
        css_inherit_is(&ok, "padding-top", "10px", "declared");
        css_inherit_is(&ok, "padding-left", "20px", "declared");
        css_inherit_is(&ok, "border-top-width", "thin", "declared");
        css_inherit_is(&ok, "border-left-width", "thick", "declared");

        robo_css_inherited_style_dispose(&ok);
        robo_css_cascade_dispose(&ok_cascade);
        css_inherit_end(&clean);
    }
}

/* ------------------------------------------------------------------ */
/* Robustness and determinism                                          */
/* ------------------------------------------------------------------ */

/**
 * Malformed and hostile input, none of which may crash or invent a value.
 *
 * The shorthand vocabulary is reached through declaration text, so the values
 * below are the tokenizer's output rather than hand-built trees — which is the
 * input a real sheet can actually produce.
 */
static void test_box_robust(void)
{
    static const char *const texts[] = {
        "m:",
        "m: ;",
        "m: 1px 2px 3px 4px 5px 6px 7px 8px",
        "m: var()",
        "m: var(--x, 1px)",
        "m: calc()",
        "m: (1px)",
        "m: [1px 2px]",
        "m: \"1px 2px\"",
        "m: 1px/2px",
        "m: 999999999999999999999999999999px",
        "m: -0",
        "m: 1e309px",
        "m: #fff #000",
        "m: 1px !important",
    };
    size_t index;

    css_fixture();
    for (index = 0u; index < sizeof(texts) / sizeof(texts[0]); index++) {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        size_t declaration_index;

        /* Parsing may recover and may report; neither is a crash and neither is
         * asserted away, because the point is that the walk below is safe. */
        ROBO_CHECK_EQ_INT(css_parse_declarations(texts[index], &list, &error), ROBO_OK);
        for (declaration_index = 0u;
             declaration_index < robo_css_declaration_list_count(&list);
             declaration_index++) {
            const robo_css_value_t *value = robo_css_declaration_value(
                robo_css_declaration_list_at(&list, declaration_index));
            size_t count = robo_css_box_component_count(value);
            size_t reported = 12345u;
            int expandable = robo_css_box_is_expandable(value, &reported);
            robo_css_computed_value_t computed = ROBO_CSS_CLEARED;

            /* The count is never past the bound that has a mapping, and the
             * reported count is only written for the answer that applies it. */
            ROBO_CHECK(count <= 8u);
            ROBO_CHECK_EQ_INT((int)reported, expandable ? (int)count : 0);
            /* Every shape either produces a value or refuses; nothing crashes. */
            ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                  ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, value,
                                  &computed, NULL),
                              ROBO_OK);
            robo_css_computed_value_dispose(&computed);
            ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                  ROBO_CSS_PROPERTY_COMPUTATION_EDGE_INSET, value,
                                  &computed, NULL),
                              ROBO_OK);
            robo_css_computed_value_dispose(&computed);
            ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                  ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, value,
                                  &computed, NULL),
                              ROBO_OK);
            robo_css_computed_value_dispose(&computed);
        }
        robo_css_declaration_list_dispose(&list);
    }
}

/**
 * The same inputs produce the same box, every time.
 *
 * Determinism over a document rather than over a value: two independent
 * computations of one element must agree on all four edges, and the shorthand
 * table must answer identically when walked forwards and backwards — an
 * order-dependent vocabulary would show up as a transposed edge somewhere.
 */
static void test_box_determinism(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *first = NULL;
    robo_css_computed_style_t *second = NULL;
    robo_css_element_ref_t div;
    static const char *const names[] = {
        "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width"
    };
    size_t index;
    int reverse;

    css_fixture();
    css_inherit_begin(&fixture);
    div = css_host_add(css_inherit_host(&fixture), "div", "app", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#app { margin: 1px 2px 3px 4px; padding: 10%;"
                      " border-width: thin thick }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&second, NULL), ROBO_OK);
    css_box_compute_of(&fixture, div, first);
    css_box_compute_of(&fixture, div, second);

    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        char one[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        char two[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

        css_computed_note(first, names[index], one, sizeof(one));
        css_computed_note(second, names[index], two, sizeof(two));
        ROBO_CHECK_STR_EQ(one, two);
    }

    robo_css_computed_style_destroy(second);
    robo_css_computed_style_destroy(first);
    css_inherit_end(&fixture);

    /* Walked backwards, the vocabulary answers the same questions. */
    for (reverse = (int)robo_css_box_shorthand_count() - 1; reverse >= 0; reverse--) {
        const robo_css_box_shorthand_t *row =
            robo_css_box_shorthand_at((size_t)reverse);
        size_t side_index;

        ROBO_CHECK(row != NULL);
        if (!row) {
            continue;
        }
        for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT;
             side_index++) {
            const char *name = robo_css_box_shorthand_longhand(
                row, (robo_css_box_side_t)side_index);
            robo_css_box_side_t side = ROBO_CSS_BOX_SIDE_TOP;

            ROBO_CHECK(robo_css_box_shorthand_of_longhand(
                           robo_css_slice_from_text(name), &side) == row);
            ROBO_CHECK_EQ_INT((int)side, (int)side_index);
        }
    }
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_box(void);

void robo_test_register_css_box(void)
{
    robo_test_register("css.box.side_vocabulary", test_box_side_vocabulary);
    robo_test_register("css.box.shorthand_table", test_box_shorthand_table);
    robo_test_register("css.box.component_mapping", test_box_component_mapping);
    robo_test_register("css.box.expansion_refusals", test_box_expansion_refusals);
    robo_test_register("css.box.shapes", test_box_shapes);
    robo_test_register("css.box.computed.longhands", test_box_computed_longhands);
    robo_test_register("css.box.cascade.shorthand_versus_longhand",
                       test_box_cascade_shorthand_versus_longhand);
    robo_test_register("css.box.cascade.specificity_and_importance",
                       test_box_cascade_specificity_and_importance);
    robo_test_register("css.box.keywords", test_box_keywords);
    robo_test_register("css.box.non_inheritance", test_box_non_inheritance);
    robo_test_register("css.box.unexpanded_shorthand", test_box_unexpanded_shorthand);
    robo_test_register("css.box.robust", test_box_robust);
    robo_test_register("css.box.determinism", test_box_determinism);
}
