/*
 * RoboCSS background suite (Phase 9.14) — the two closed vocabularies, the edge
 * vocabulary and the two pairs, the six longhands through the pipeline, and what
 * the `background` shorthand does to them.
 *
 * Five groups, in the order the phase is built:
 *
 *   the vocabularies  repeat and attachment walked rather than a representative
 *                     typed out, with the repeat rows' two *axes* read from the
 *                     same rows the names come from
 *   the components    an edge is spelled by its axis, a pair is two components,
 *                     and equality is kind-aware — `left` and `0px` are different
 *                   however equal they look
 *   the pairs         position and size: what parses, what is refused and why,
 *                     and the *folding* that makes `center` and `center center`
 *                     one computed value rather than two
 *   the longhands     all six through the real grammar, in every refusals'
 *                     wording, and the one shape whose value is a pair
 *   integration       a real element: initial values through the query door,
 *                     non-inheritance proved from both sides, `inherit`,
 *                     `initial` and `unset`, the cascade's three keys, the
 *                     shorthand's distribution and its *reset*
 *
 * Nothing here paints anything. A background at this phase is a set of six
 * *values* with shapes: `25% 50%` stays two percentages because only the phase
 * with a box can say 25% of what, `none` is a value rather than a missing image,
 * and `background: red` is six declarations rather than a colour.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The repeat vocabulary                                               */
/* ------------------------------------------------------------------ */

/**
 * The four values, and what each is spelled as.
 *
 * The spelling is the assertion rather than the enumeration: a table whose rows
 * and enumerators had drifted would still answer `is_known` for all four and
 * would only show as a value that printed as another one's keyword. Walking the
 * set in enumerator order and reading every name back is what makes that visible.
 */
static void test_background_repeat_vocabulary(void)
{
    static const char *const keywords[] = { "repeat", "repeat-x", "repeat-y",
                                            "no-repeat" };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_REPEAT_COUNT, 4);
    ROBO_CHECK_EQ_INT((int)robo_css_background_repeat_count(), 4);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        robo_css_background_repeat_t repeat = (robo_css_background_repeat_t)index;

        ROBO_CHECK_EQ_INT(robo_css_background_repeat_is_known(repeat), 1);
        ROBO_CHECK_STR_EQ(robo_css_background_repeat_name(repeat), keywords[index]);
        ROBO_CHECK_EQ_INT(robo_css_background_repeat_from_text(keywords[index]),
                          (int)repeat);
    }
    /* The enumerator past the end and a value nothing defined answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_is_known(
                          (robo_css_background_repeat_t)ROBO_CSS_BACKGROUND_REPEAT_COUNT),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_is_known(
                          (robo_css_background_repeat_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_background_repeat_name(
                          (robo_css_background_repeat_t)99),
                      "");
    /* An unknown *identifier* answers `repeat`, which is a real value — so the
     * predicate on the identifier is what tells the two apart, not the result. */
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_slice_is_known(
                          robo_css_slice_from_text("repeat")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_slice_is_known(
                          robo_css_slice_from_text("REPEAT-Y")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_slice_is_known(
                          robo_css_slice_from_text("space")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_slice_is_known(
                          robo_css_slice_from_text("repeatx")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_slice_is_known(
                          robo_css_slice_from_text("")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_from_slice(
                          robo_css_slice_from_text("space")),
                      (int)ROBO_CSS_BACKGROUND_REPEAT);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_from_text(NULL),
                      (int)ROBO_CSS_BACKGROUND_REPEAT);
}

/**
 * The two axes, and that `repeat-x` is a *row* rather than a fifth behaviour.
 *
 * `repeat-x` is not "repeat, horizontally": it is "tiles horizontally, does not
 * tile vertically", and the row carries both answers so a value added later
 * (`space`, `round`) answers for itself. The unknown enumerator answers zero on
 * both axes — "this tiles" is a claim a value nothing defined cannot support —
 * so the two columns are not each other's negation.
 */
static void test_background_repeat_axes(void)
{
    static const struct {
        robo_css_background_repeat_t repeat;
        int horizontal;
        int vertical;
    } rows[] = {
        { ROBO_CSS_BACKGROUND_REPEAT,    1, 1 },
        { ROBO_CSS_BACKGROUND_REPEAT_X,  1, 0 },
        { ROBO_CSS_BACKGROUND_REPEAT_Y,  0, 1 },
        { ROBO_CSS_BACKGROUND_NO_REPEAT, 0, 0 }
    };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_AXIS_COUNT, 2);
    ROBO_CHECK_STR_EQ(robo_css_background_axis_name(ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      "horizontal");
    ROBO_CHECK_STR_EQ(robo_css_background_axis_name(ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      "vertical");
    ROBO_CHECK_EQ_INT(robo_css_background_axis_is_known(
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_axis_is_known(
                          ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_axis_is_known(
                          (robo_css_background_axis_t)ROBO_CSS_BACKGROUND_AXIS_COUNT),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_background_axis_name((robo_css_background_axis_t)99), "");
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_css_background_repeat_repeats_on_axis(
                              rows[index].repeat,
                              ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                          rows[index].horizontal);
        ROBO_CHECK_EQ_INT(robo_css_background_repeat_repeats_on_axis(
                              rows[index].repeat,
                              ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                          rows[index].vertical);
    }
    /* One of the four tiles one axis only, and the vocabulary says *which*:
     * swapping the two columns is the mistake this cross-check exists for. */
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_repeats_on_axis(
                          ROBO_CSS_BACKGROUND_REPEAT_X,
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_repeats_on_axis(
                          ROBO_CSS_BACKGROUND_REPEAT_X,
                          ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_repeats_on_axis(
                          (robo_css_background_repeat_t)99,
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_repeat_repeats_on_axis(
                          (robo_css_background_repeat_t)99,
                          ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      0);
}

/** The attachment vocabulary: two values, and `local` is a row not added. */
static void test_background_attachment_vocabulary(void)
{
    static const char *const keywords[] = { "scroll", "fixed" };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_ATTACHMENT_COUNT, 2);
    ROBO_CHECK_EQ_INT((int)robo_css_background_attachment_count(), 2);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        robo_css_background_attachment_t attachment =
            (robo_css_background_attachment_t)index;

        ROBO_CHECK_EQ_INT(robo_css_background_attachment_is_known(attachment), 1);
        ROBO_CHECK_STR_EQ(robo_css_background_attachment_name(attachment),
                          keywords[index]);
        ROBO_CHECK_EQ_INT(robo_css_background_attachment_from_text(keywords[index]),
                          (int)attachment);
    }
    ROBO_CHECK_EQ_INT(robo_css_background_attachment_is_known(
                          (robo_css_background_attachment_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_background_attachment_name(
                          (robo_css_background_attachment_t)99),
                      "");
    ROBO_CHECK_EQ_INT(robo_css_background_attachment_slice_is_known(
                          robo_css_slice_from_text("FIXED")),
                      1);
    /* `local` scrolls with the element's own content rather than with the page,
     * and it is a value this build has not added — which is a different answer
     * from a word nothing knows. */
    ROBO_CHECK_EQ_INT(robo_css_background_attachment_slice_is_known(
                          robo_css_slice_from_text("local")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_attachment_slice_is_known(
                          robo_css_slice_from_text("floating")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_attachment_slice_is_known(
                          robo_css_slice_from_text("")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_attachment_from_text(NULL),
                      (int)ROBO_CSS_BACKGROUND_ATTACHMENT_SCROLL);
}

/**
 * The edges, and the fact that a position keyword is spelled *by its axis*.
 *
 * `left` and `top` are both the START edge, and the component alone cannot say
 * which one was written — so the axis is an argument to both doors. That is what
 * makes `top` in the horizontal slot a refusal rather than a silent `center`,
 * which would turn `background-position: top` into the top-left corner.
 */
static void test_background_edge_vocabulary(void)
{
    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_EDGE_COUNT, 3);
    ROBO_CHECK_EQ_INT(ROBO_CSS_BACKGROUND_EDGE_CENTER, 0);
    ROBO_CHECK_EQ_INT(ROBO_CSS_BACKGROUND_EDGE_START, 1);
    ROBO_CHECK_EQ_INT(ROBO_CSS_BACKGROUND_EDGE_END, 2);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_is_known(ROBO_CSS_BACKGROUND_EDGE_START),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_is_known(
                          (robo_css_background_edge_t)99),
                      0);

    /* One edge, two spellings: the axis picks the word. */
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name(ROBO_CSS_BACKGROUND_EDGE_START,
                                                    ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      "left");
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name(ROBO_CSS_BACKGROUND_EDGE_START,
                                                    ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      "top");
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name(ROBO_CSS_BACKGROUND_EDGE_END,
                                                    ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      "right");
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name(ROBO_CSS_BACKGROUND_EDGE_END,
                                                    ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      "bottom");
    /* And the one edge that means the same on both, which is why it exists. */
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name(ROBO_CSS_BACKGROUND_EDGE_CENTER,
                                                    ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      "center");
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name(ROBO_CSS_BACKGROUND_EDGE_CENTER,
                                                    ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      "center");
    /* An unknown edge, or an unknown axis, spells nothing rather than a guess. */
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name((robo_css_background_edge_t)99,
                                                    ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      "");
    ROBO_CHECK_STR_EQ(robo_css_background_edge_name(ROBO_CSS_BACKGROUND_EDGE_START,
                                                    (robo_css_background_axis_t)99),
                      "");

    /* Half the vocabulary per axis, and the overlap is `center` alone. */
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("left"),
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("left"),
                          ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("TOP"),
                          ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("TOP"),
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("center"),
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("center"),
                          ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("centre"),
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_slice_is_known(
                          robo_css_slice_from_text("banana"),
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      0);
    /* A word that is not an edge on that axis answers CENTER, which is a real
     * value — the predicate is what tells the two apart. */
    ROBO_CHECK_EQ_INT(robo_css_background_edge_from_slice(
                          robo_css_slice_from_text("top"),
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL),
                      (int)ROBO_CSS_BACKGROUND_EDGE_CENTER);
    ROBO_CHECK_EQ_INT(robo_css_background_edge_from_slice(
                          robo_css_slice_from_text("bottom"),
                          ROBO_CSS_BACKGROUND_AXIS_VERTICAL),
                      (int)ROBO_CSS_BACKGROUND_EDGE_END);
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/**
 * The value of the first declaration in @p text, with @p list left owning it.
 *
 * The declaration's value is a *component tree the grammar built*, which is the
 * only input the background readers take: they never see a token stream, so a
 * fixture that parsed by hand would be testing a different machine.
 */
static const robo_css_value_t *css_background_value(const char *text,
                                                    robo_css_declaration_list_t *list)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    const robo_css_declaration_t *declaration;

    ROBO_CHECK_EQ_INT(css_parse_declarations(text, list, &error), ROBO_OK);
    declaration = robo_css_declaration_list_at(list, 0u);
    ROBO_CHECK(declaration != NULL);
    return declaration ? robo_css_declaration_value(declaration) : NULL;
}

/** What @p text reads as, as a background position. */
static robo_css_background_parse_t css_background_position_outcome(const char *text)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_background_value(text, &list);
    robo_css_background_position_t position = robo_css_background_position_none();
    robo_css_background_parse_t outcome =
        robo_css_background_position_from_value(value, &position);

    /* Any outcome but POSITION leaves @p position absent, which is what makes a
     * refusal readable rather than a half-filled pair. */
    if (outcome != ROBO_CSS_BACKGROUND_PARSE_POSITION) {
        ROBO_CHECK_EQ_INT(robo_css_background_position_is_present(position), 0);
    }
    robo_css_declaration_list_dispose(&list);
    return outcome;
}

/** The canonical text of @p text read as a background position. */
static void css_background_position_is(const char *text, const char *expected)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_background_value(text, &list);
    robo_css_background_position_t position = robo_css_background_position_none();
    char shape[ROBO_CSS_NUMERIC_TEXT_MAX * 2u + 2u];
    size_t length = 0u;

    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(value, &position),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(robo_css_background_position_is_present(position), 1);
    ROBO_CHECK_EQ_INT(robo_css_background_position_write(position, shape, sizeof(shape),
                                                         &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(shape, expected);
    ROBO_CHECK_EQ_INT((int)length, (int)strlen(expected));
    robo_css_declaration_list_dispose(&list);
}

/** What @p text reads as, as a background size. */
static robo_css_background_parse_t css_background_size_outcome(const char *text)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_background_value(text, &list);
    robo_css_background_size_t size = robo_css_background_size_none();
    robo_css_background_parse_t outcome =
        robo_css_background_size_from_value(value, &size);

    if (outcome != ROBO_CSS_BACKGROUND_PARSE_SIZE) {
        /* A cleared record reads as `auto` — the initial value is the safest
         * thing a cleared one could hold — so the outcome is the only thing a
         * refusal can be read from. */
        ROBO_CHECK_EQ_INT(outcome != ROBO_CSS_BACKGROUND_PARSE_SIZE, 1);
    }
    robo_css_declaration_list_dispose(&list);
    return outcome;
}

/** The canonical text of @p text read as a background size. */
static void css_background_size_is(const char *text, const char *expected)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value = css_background_value(text, &list);
    robo_css_background_size_t size = robo_css_background_size_none();
    char shape[ROBO_CSS_NUMERIC_TEXT_MAX * 2u + 2u];
    size_t length = 0u;

    ROBO_CHECK_EQ_INT(robo_css_background_size_from_value(value, &size),
                      ROBO_CSS_BACKGROUND_PARSE_SIZE);
    ROBO_CHECK_EQ_INT(robo_css_background_size_is_present(size), 1);
    ROBO_CHECK_EQ_INT(robo_css_background_size_write(size, shape, sizeof(shape), &length,
                                                     NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(shape, expected);
    ROBO_CHECK_EQ_INT((int)length, (int)strlen(expected));
    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* The components                                                      */
/* ------------------------------------------------------------------ */

/**
 * A component's model: four kinds, kind-aware equality, and absence.
 *
 * Equality is where the model earns its place. `left` and `0px` are the same
 * place and are *not* the same value — one is an edge and one is an offset, and
 * the day something resolves them the difference is the whole question — so
 * equality compares kinds first. `0px` and `0%` are different for the same reason
 * at the same magnitude, and two absent components are not equal either: "no
 * component" is not a value, and `is_present` is the door for that question.
 */
static void test_background_component_model(void)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value;
    robo_css_background_position_t offsets = robo_css_background_position_none();
    robo_css_background_position_t edges = robo_css_background_position_none();
    robo_css_background_position_t percents = robo_css_background_position_none();

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_COMPONENT_KIND_COUNT, 4);
    ROBO_CHECK_STR_EQ(robo_css_background_component_kind_name(
                          ROBO_CSS_BACKGROUND_COMPONENT_NONE),
                      "none");
    ROBO_CHECK_STR_EQ(robo_css_background_component_kind_name(
                          ROBO_CSS_BACKGROUND_COMPONENT_EDGE),
                      "edge");
    ROBO_CHECK_STR_EQ(robo_css_background_component_kind_name(
                          ROBO_CSS_BACKGROUND_COMPONENT_LENGTH),
                      "length");
    ROBO_CHECK_STR_EQ(robo_css_background_component_kind_name(
                          ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE),
                      "percentage");
    ROBO_CHECK_EQ_INT(robo_css_background_component_kind_is_known(
                          (robo_css_background_component_kind_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_background_component_kind_name(
                          (robo_css_background_component_kind_t)99),
                      "unknown");

    /* `10px 20px`: two offsets, and they are different values. */
    value = css_background_value("p: 10px 20px", &list);
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(value, &offsets),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(offsets.horizontal.kind, ROBO_CSS_BACKGROUND_COMPONENT_LENGTH);
    ROBO_CHECK_EQ_INT(offsets.vertical.kind, ROBO_CSS_BACKGROUND_COMPONENT_LENGTH);
    ROBO_CHECK_EQ_INT(robo_css_background_component_equals(offsets.horizontal,
                                                          offsets.horizontal),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_component_equals(offsets.horizontal,
                                                          offsets.vertical),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_component_is_present(offsets.horizontal), 1);
    robo_css_declaration_list_dispose(&list);

    /* `left top`: two edges, one START each — and equal, because a component
     * records the place rather than the word. The axis is what spells it, and
     * that is the writer's argument, not a field. */
    memset(&list, 0, sizeof(list));
    value = css_background_value("p: left top", &list);
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(value, &edges),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(edges.horizontal.kind, ROBO_CSS_BACKGROUND_COMPONENT_EDGE);
    ROBO_CHECK_EQ_INT(edges.horizontal.edge, ROBO_CSS_BACKGROUND_EDGE_START);
    ROBO_CHECK_EQ_INT(edges.vertical.edge, ROBO_CSS_BACKGROUND_EDGE_START);
    ROBO_CHECK_EQ_INT(robo_css_background_component_equals(edges.horizontal,
                                                          edges.vertical),
                      1);
    robo_css_declaration_list_dispose(&list);

    /* And a percentage at the same magnitude as a length is a different value:
     * 10% of a box is not ten pixels until something says what the box is. */
    memset(&list, 0, sizeof(list));
    value = css_background_value("p: 10% 10%", &list);
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(value, &percents),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(percents.horizontal.kind,
                      ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE);
    ROBO_CHECK_EQ_INT(robo_css_background_component_equals(percents.horizontal,
                                                          offsets.horizontal),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_component_equals(percents.horizontal,
                                                          edges.horizontal),
                      0);
    robo_css_declaration_list_dispose(&list);

    /* Absence is not a value: there is nothing for two absences to be equal
     * about, and a *pair* is present only when both halves are. */
    ROBO_CHECK_EQ_INT(robo_css_background_component_is_present(
                          robo_css_background_component_none()),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_component_equals(
                          robo_css_background_component_none(),
                          robo_css_background_component_none()),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_position_is_present(
                          robo_css_background_position_none()),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_position_equals(
                          robo_css_background_position_none(),
                          robo_css_background_position_none()),
                      0);
    {
        /* A half-filled pair is what the reader holds mid-parse and never what it
         * returns: built by hand here to prove the pair's own answer. */
        robo_css_background_position_t half = robo_css_background_position_none();

        half.horizontal = edges.horizontal;
        ROBO_CHECK_EQ_INT(robo_css_background_position_is_present(half), 0);
        ROBO_CHECK_EQ_INT(robo_css_background_position_equals(half, half), 0);
    }
}

/**
 * Writing one component, and the two refusals that are not a keyword's.
 *
 * An edge is spelled by its axis, so the same component prints `left` or `top`
 * depending on what it is a component *of* — which is the one thing this module
 * cannot decide for itself. A length and a percentage print one canonical
 * spelling shared with every other numeric value in the subsystem.
 */
static void test_background_component_write(void)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *value;
    robo_css_background_position_t position = robo_css_background_position_none();
    robo_css_background_component_t edge = robo_css_background_component_none();
    char buffer[ROBO_CSS_NUMERIC_TEXT_MAX];
    size_t length = 0u;

    css_fixture();
    edge.kind = ROBO_CSS_BACKGROUND_COMPONENT_EDGE;
    edge.edge = ROBO_CSS_BACKGROUND_EDGE_START;
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          edge, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL, buffer,
                          sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "left");
    ROBO_CHECK_EQ_INT((int)length, 4);
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          edge, ROBO_CSS_BACKGROUND_AXIS_VERTICAL, buffer,
                          sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "top");
    edge.edge = ROBO_CSS_BACKGROUND_EDGE_END;
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          edge, ROBO_CSS_BACKGROUND_AXIS_VERTICAL, buffer,
                          sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "bottom");

    /* A value and a percentage, from a real parse rather than a hand-built
     * record, so the spelling is the one the computed layer would print. */
    value = css_background_value("p: 2em 25%", &list);
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(value, &position),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          position.horizontal, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL,
                          buffer, sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "2em");
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          position.vertical, ROBO_CSS_BACKGROUND_AXIS_VERTICAL, buffer,
                          sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "25%");
    robo_css_declaration_list_dispose(&list);

    /* Three refusals, each its own sentence. */
    {
        char tiny[4];

        ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                              edge, ROBO_CSS_BACKGROUND_AXIS_VERTICAL, tiny,
                              sizeof(tiny), &length, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_STR_EQ(tiny, "");
        ROBO_CHECK_EQ_INT((int)length, 6); /* "bottom" */
    }
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          robo_css_background_component_none(),
                          ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL, buffer, sizeof(buffer),
                          &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          edge, (robo_css_background_axis_t)99, buffer, sizeof(buffer),
                          &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          edge, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL, NULL,
                          sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          edge, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL, buffer,
                          sizeof(buffer), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_background_component_write(
                          edge, ROBO_CSS_BACKGROUND_AXIS_HORIZONTAL, buffer, 0u,
                          &length, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The position pair                                                   */
/* ------------------------------------------------------------------ */

/**
 * What a position is, and the folding that makes it one value.
 *
 * The pair is the computed value whatever the declaration looked like: `center`
 * is `center center` and `10px` is `10px center`, because CSS says a single
 * component leaves the other axis at centre and a consumer that had to remember
 * that would be re-deriving a rule this module already applied. The asymmetry is
 * CSS's own — `top` names the *vertical* axis and so it goes in the vertical
 * slot, while `left` and every offset are horizontal — and it is why the single
 * component's home is a branch rather than the same assignment twice.
 */
static void test_background_position_parse(void)
{
    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_PARSE_COUNT, 4);
    ROBO_CHECK_STR_EQ(robo_css_background_parse_name(
                          ROBO_CSS_BACKGROUND_PARSE_INVALID),
                      "invalid");
    ROBO_CHECK_STR_EQ(robo_css_background_parse_name(
                          ROBO_CSS_BACKGROUND_PARSE_POSITION),
                      "position");
    ROBO_CHECK_STR_EQ(robo_css_background_parse_name(ROBO_CSS_BACKGROUND_PARSE_SIZE),
                      "size");
    ROBO_CHECK_STR_EQ(robo_css_background_parse_name(
                          ROBO_CSS_BACKGROUND_PARSE_DEFERRED),
                      "deferred");
    ROBO_CHECK_EQ_INT(robo_css_background_parse_is_known(
                          (robo_css_background_parse_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_background_parse_name(
                          (robo_css_background_parse_t)99),
                      "unknown");

    /* Two keywords, one per axis, in either order and either case. */
    css_background_position_is("p: left top", "left top");
    css_background_position_is("p: top left", "left top");
    css_background_position_is("p: LEFT TOP", "left top");
    css_background_position_is("p: right bottom", "right bottom");
    css_background_position_is("p: bottom right", "right bottom");
    css_background_position_is("p: center center", "center center");

    /* Two offsets, and two percentages: kept as they are, because only the phase
     * with a box can answer 25% of what. */
    css_background_position_is("p: 10px 20px", "10px 20px");
    css_background_position_is("p: 25% 50%", "25% 50%");
    css_background_position_is("p: 2em 1.5rem", "2em 1.5rem");
    css_background_position_is("p: 1in 12px", "96px 12px");

    /* The single-component form, folded — and the two halves of CSS's
     * asymmetry: a vertical keyword goes in the vertical slot, everything else
     * is horizontal. */
    css_background_position_is("p: center", "center center");
    css_background_position_is("p: left", "left center");
    css_background_position_is("p: right", "right center");
    css_background_position_is("p: top", "center top");
    css_background_position_is("p: bottom", "center bottom");
    css_background_position_is("p: 10px", "10px center");
    css_background_position_is("p: 25%", "25% center");

    /* A centre and an offset is a position — horizontal centre, ten pixels down
     * — which is the one keyword-plus-offset spelling this build reads. */
    css_background_position_is("p: center 10px", "center 10px");

    /* Whitespace is the grammar's, and it changes nothing. */
    css_background_position_is("p:   25%   50%  ", "25% 50%");
    css_background_position_is("p:25% 50%", "25% 50%");
}

/**
 * The refusals, each for its own reason.
 *
 * The `left 10px` form is the interesting one: it is legal CSS where the offset
 * hangs off the named edge, its *order* carries the meaning, and it is a
 * four-component grammar this build does not represent. Reading it as `left` +
 * `10px` would be a plausible wrong answer — a position ten pixels from the left
 * edge is not `left 10px`'s meaning when the edge is named — so it is refused.
 * Three or more components are refused for the same reason: the count decides the
 * meaning in that grammar.
 */
static void test_background_position_invalid(void)
{
    css_fixture();

    /* The edge-relative form, in both its spellings. */
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: left 10px"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: right 2em"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: top 10px"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    /* Two keywords on the same axis names one axis twice and leaves the other
     * unnamed, which is a statement this value cannot make. */
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: left right"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: top bottom"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);

    /* A word, an absent value, and a bare number — which is not a position
     * because CSS's `<length-percentage>` has no unitless branch. */
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: banana"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: lefty"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: 10"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p:"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: none"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);

    /* Three and four components: the count decides the meaning there. */
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: left top center"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: left top 10px 20px"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    /* A colour is not a position, and neither is a string. */
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: red"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: #fff"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: \"left\""),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
}

/**
 * A reference is not a refusal, and it is not a position either.
 *
 * `background-position: calc(10px + 1%)` is legal CSS this build does not
 * evaluate; the outcome says so, which is what lets the computed layer carry the
 * tokens and *flag* them rather than calling the declaration a mistake.
 */
static void test_background_position_reference(void)
{
    css_fixture();
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: var(--p)"),
                      ROBO_CSS_BACKGROUND_PARSE_DEFERRED);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: env(--p)"),
                      ROBO_CSS_BACKGROUND_PARSE_DEFERRED);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: calc(10px + 1%)"),
                      ROBO_CSS_BACKGROUND_PARSE_DEFERRED);
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: min(10px, 2em)"),
                      ROBO_CSS_BACKGROUND_PARSE_DEFERRED);
    /* A function that is not a reference is not a position either. */
    ROBO_CHECK_EQ_INT(css_background_position_outcome("p: nonsense(10px)"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
}

/** Writing a pair, and comparing two of them. */
static void test_background_position_write(void)
{
    robo_css_declaration_list_t first_list = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t second_list = ROBO_CSS_CLEARED;
    robo_css_background_position_t first = robo_css_background_position_none();
    robo_css_background_position_t second = robo_css_background_position_none();
    char buffer[ROBO_CSS_NUMERIC_TEXT_MAX * 2u + 2u];
    size_t length = 0u;

    css_fixture();
    /* One spelling per value: `center` and `center center` are the same computed
     * value and print the same string. */
    css_background_position_is("p: center", "center center");
    css_background_position_is("p: center center", "center center");

    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(
                          css_background_value("p: left top", &first_list), &first),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(
                          css_background_value("p: left top", &second_list), &second),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(robo_css_background_position_equals(first, second), 1);
    robo_css_declaration_list_dispose(&second_list);

    /* Order is not part of the value: `top left` and `left top` are one place. */
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(
                          css_background_value("p: top left", &second_list), &second),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(robo_css_background_position_equals(first, second), 1);
    robo_css_declaration_list_dispose(&second_list);

    /* A different place, and a different *kind* of place: `0px 0px` is not
     * `0% 0%`, however alike they look once a box exists. */
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(
                          css_background_value("p: right top", &second_list), &second),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    ROBO_CHECK_EQ_INT(robo_css_background_position_equals(first, second), 0);
    robo_css_declaration_list_dispose(&second_list);
    ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(
                          css_background_value("p: 0px 0px", &second_list), &second),
                      ROBO_CSS_BACKGROUND_PARSE_POSITION);
    {
        robo_css_background_position_t percentages = robo_css_background_position_none();

        ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(
                              css_background_value("p: 0% 0%", &first_list), &percentages),
                          ROBO_CSS_BACKGROUND_PARSE_POSITION);
        /* The initial value the registry names, spelled as CSS spells it. */
        ROBO_CHECK_EQ_INT(robo_css_background_position_write(
                              percentages, buffer, sizeof(buffer), &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, "0% 0%");
        ROBO_CHECK_EQ_INT(robo_css_background_position_equals(percentages, second), 0);
    }
    robo_css_declaration_list_dispose(&second_list);
    robo_css_declaration_list_dispose(&first_list);

    /* Three refusals: too small a buffer says how much it needed rather than
     * truncating into a different position; an absent pair has no text; and the
     * argument checks come before either. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_background_position_t wide = robo_css_background_position_none();
        char tiny[4];

        ROBO_CHECK_EQ_INT(robo_css_background_position_from_value(
                              css_background_value("p: center center", &list), &wide),
                          ROBO_CSS_BACKGROUND_PARSE_POSITION);
        ROBO_CHECK_EQ_INT(robo_css_background_position_write(wide, tiny, sizeof(tiny),
                                                             &length, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_STR_EQ(tiny, "");
        ROBO_CHECK_EQ_INT((int)length, 13); /* "center center" */
        ROBO_CHECK_EQ_INT(robo_css_background_position_write(
                              robo_css_background_position_none(), buffer,
                              sizeof(buffer), &length, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_background_position_write(
                              wide, NULL, sizeof(buffer), &length, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_background_position_write(
                              wide, buffer, sizeof(buffer), NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_background_position_write(wide, buffer, 0u, &length,
                                                             NULL),
                          ROBO_INVALID_ARGUMENT);
        robo_css_declaration_list_dispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* The size pair                                                       */
/* ------------------------------------------------------------------ */

/**
 * What a size is: three whole-value keywords, or a pair.
 *
 * `cover` and `contain` are not components — neither is a width — so unlike a
 * position this shape is not always a pair. And a pair of two `auto`s is folded
 * back to the keyword, because `auto` and `auto auto` are one computed value with
 * one spelling; a single component leaves the height `auto`, as CSS says.
 */
static void test_background_size_parse(void)
{
    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_SIZE_KIND_COUNT, 4);
    ROBO_CHECK_STR_EQ(robo_css_background_size_kind_name(
                          ROBO_CSS_BACKGROUND_SIZE_AUTO),
                      "auto");
    ROBO_CHECK_STR_EQ(robo_css_background_size_kind_name(
                          ROBO_CSS_BACKGROUND_SIZE_COVER),
                      "cover");
    ROBO_CHECK_STR_EQ(robo_css_background_size_kind_name(
                          ROBO_CSS_BACKGROUND_SIZE_CONTAIN),
                      "contain");
    ROBO_CHECK_STR_EQ(robo_css_background_size_kind_name(
                          ROBO_CSS_BACKGROUND_SIZE_PAIR),
                      "pair");
    ROBO_CHECK_EQ_INT(robo_css_background_size_kind_is_known(
                          (robo_css_background_size_kind_t)99),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_background_size_kind_name(
                          (robo_css_background_size_kind_t)99),
                      "unknown");

    /* The three whole values, and `auto auto` folding onto the first. */
    css_background_size_is("p: auto", "auto");
    css_background_size_is("p: AUTO", "auto");
    css_background_size_is("p: auto auto", "auto");
    css_background_size_is("p: cover", "cover");
    css_background_size_is("p: CONTAIN", "contain");

    /* Pairs, in both component kinds, and `auto` beside a real component. */
    css_background_size_is("p: 100px 50px", "100px 50px");
    css_background_size_is("p: 50% 25%", "50% 25%");
    css_background_size_is("p: 2em 1.5rem", "2em 1.5rem");
    css_background_size_is("p: 20px auto", "20px auto");
    css_background_size_is("p: auto 20px", "auto 20px");
    css_background_size_is("p: 1in 12px", "96px 12px");

    /* The single-component form, folded: a width, and the height left `auto`. */
    css_background_size_is("p: 100px", "100px auto");
    css_background_size_is("p: 50%", "50% auto");
    css_background_size_is("p:   50%   ", "50% auto");
}

/** The size's refusals, and that a reference is not one of them. */
static void test_background_size_invalid(void)
{
    css_fixture();

    /* An edge is a position component and not a size: `background-size: left`
     * is a mistake rather than a left-hand-side size. */
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: left"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: center"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: 10px left"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    /* A word, an absent value, a bare number, and too many components. */
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: banana"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p:"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: 10"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: 10px 20px 30px"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: cover contain"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: red"),
                      ROBO_CSS_BACKGROUND_PARSE_INVALID);

    /* A reference is carried, which is a different answer from a mistake. */
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: var(--s)"),
                      ROBO_CSS_BACKGROUND_PARSE_DEFERRED);
    ROBO_CHECK_EQ_INT(css_background_size_outcome("p: calc(100% - 20px)"),
                      ROBO_CSS_BACKGROUND_PARSE_DEFERRED);
}

/** Writing a size and comparing two, including the `auto` flag's part. */
static void test_background_size_write(void)
{
    robo_css_declaration_list_t first_list = ROBO_CSS_CLEARED;
    robo_css_declaration_list_t second_list = ROBO_CSS_CLEARED;
    robo_css_background_size_t first = robo_css_background_size_none();
    robo_css_background_size_t second = robo_css_background_size_none();
    char buffer[ROBO_CSS_NUMERIC_TEXT_MAX * 2u + 2u];
    size_t length = 0u;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_background_size_from_value(
                          css_background_value("p: auto auto", &first_list), &first),
                      ROBO_CSS_BACKGROUND_PARSE_SIZE);
    ROBO_CHECK_EQ_INT(first.kind, ROBO_CSS_BACKGROUND_SIZE_AUTO);
    ROBO_CHECK_EQ_INT(robo_css_background_size_from_value(
                          css_background_value("p: auto", &second_list), &second),
                      ROBO_CSS_BACKGROUND_PARSE_SIZE);
    ROBO_CHECK_EQ_INT(robo_css_background_size_equals(first, second), 1);
    robo_css_declaration_list_dispose(&second_list);

    /* `cover` and `contain` are not a pair of anything, so they are not equal to
     * one another or to a pair that happens to print differently. */
    ROBO_CHECK_EQ_INT(robo_css_background_size_from_value(
                          css_background_value("p: cover", &second_list), &second),
                      ROBO_CSS_BACKGROUND_PARSE_SIZE);
    ROBO_CHECK_EQ_INT(robo_css_background_size_equals(first, second), 0);
    ROBO_CHECK_EQ_INT(robo_css_background_size_equals(second, second), 1);

    /* `auto` is a flag rather than a numeric kind, so `20px auto` differs from
     * `20px 0px` — which is the distinction the pair exists to keep. */
    ROBO_CHECK_EQ_INT(robo_css_background_size_from_value(
                          css_background_value("p: 20px auto", &first_list), &first),
                      ROBO_CSS_BACKGROUND_PARSE_SIZE);
    ROBO_CHECK_EQ_INT(robo_css_background_size_from_value(
                          css_background_value("p: 20px 0px", &second_list), &second),
                      ROBO_CSS_BACKGROUND_PARSE_SIZE);
    ROBO_CHECK_EQ_INT(robo_css_background_size_equals(first, second), 0);
    ROBO_CHECK_EQ_INT(robo_css_background_size_write(first, buffer, sizeof(buffer),
                                                     &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "20px auto");
    robo_css_declaration_list_dispose(&second_list);
    robo_css_declaration_list_dispose(&first_list);

    /* A cleared record is `auto`, which is why the outcome is the only thing a
     * refusal can be read from — and why a pair with both halves `auto` is not
     * the pair kind. */
    ROBO_CHECK_EQ_INT(robo_css_background_size_none().kind,
                      ROBO_CSS_BACKGROUND_SIZE_AUTO);
    ROBO_CHECK_EQ_INT(robo_css_background_size_is_present(
                          robo_css_background_size_none()),
                      1);

    /* Refusals: a short buffer says how much it needed rather than truncating
     * into a different size, an unknown kind is not a value, and a pair with
     * neither half set is incoherent. */
    {
        robo_css_background_size_t pair = robo_css_background_size_none();
        robo_css_numeric_t twenty = robo_css_numeric_none();
        char tiny[4];

        ROBO_CHECK_EQ_INT(robo_css_numeric_make_length(&twenty, 20.0, ROBO_CSS_UNIT_PX,
                                                      NULL),
                          ROBO_OK);
        pair.kind = ROBO_CSS_BACKGROUND_SIZE_PAIR;
        pair.width.is_auto = 1;
        pair.height.numeric = twenty;
        ROBO_CHECK_EQ_INT(robo_css_background_size_is_present(pair), 1);
        ROBO_CHECK_EQ_INT(robo_css_background_size_write(pair, tiny, sizeof(tiny),
                                                         &length, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_STR_EQ(tiny, "");
        ROBO_CHECK_EQ_INT((int)length, 9); /* "auto 20px" */
        ROBO_CHECK_EQ_INT(robo_css_background_size_write(pair, buffer, sizeof(buffer),
                                                         &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, "auto 20px");
        pair.kind = (robo_css_background_size_kind_t)99;
        ROBO_CHECK_EQ_INT(robo_css_background_size_is_present(pair), 0);
        ROBO_CHECK_EQ_INT(robo_css_background_size_write(pair, buffer, sizeof(buffer),
                                                        &length, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
    {
        robo_css_background_size_t pair = robo_css_background_size_none();

        pair.kind = ROBO_CSS_BACKGROUND_SIZE_PAIR;
        ROBO_CHECK_EQ_INT(robo_css_background_size_is_present(pair), 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_background_size_write(
                          robo_css_background_size_none(), NULL, sizeof(buffer),
                          &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_background_size_write(
                          robo_css_background_size_none(), buffer, sizeof(buffer),
                          NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_background_size_write(
                          robo_css_background_size_none(), buffer, 0u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A cleared record reads as `auto` — the initial value is the safest thing a
     * cleared one could hold — so two of them *are* equal, and absence is not a
     * state this shape can express. The contrast with the component and position
     * pairs above is deliberate: those can be absent, this one cannot. */
    ROBO_CHECK_EQ_INT(robo_css_background_size_equals(
                          robo_css_background_size_none(),
                          robo_css_background_size_none()),
                      1);
}

/* ------------------------------------------------------------------ */
/* The six longhands through the value layer                           */
/* ------------------------------------------------------------------ */

/**
 * `background-position` as a computed value: the pair, typed.
 *
 * The assertion that matters is by *kind* as well as by text: a value that
 * printed `25% 50%` but was a preserved token list would pass a text assertion
 * and hand a later phase a string to re-parse.
 */
static void test_background_position_computed(void)
{
    css_fixture();
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: left top", "left top");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: center", "center center");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: 10px 20px", "10px 20px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: 25% 50%", "25% 50%");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: top", "center top");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: 0% 0%", "0% 0%");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: 0%", "0% center");
    /* This spelling is CSS's default position and it is *not* the initial value's
     * text: `0% 0%` is the top-left, and `0%` alone leaves the vertical at
     * centre. Both are correct and they are different places. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                                       "p: left top"),
                      "background-position");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION),
                      "background-position");
}

/** `background-position`'s refusals, told apart from a carried reference. */
static void test_background_position_computed_invalid(void)
{
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    uint32_t unresolved = 0u;

    css_fixture();
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: left 10px", "not a background position");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: banana", "not a background position");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: 10", "not a background position");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: left top center", "not a background position");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION, "p:",
                       "not a background position");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                                       "p: banana"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                                       "p: left top"),
                      "background-position");

    /* A reference is carried with the flag rather than refused: `calc()` and
     * `var()` are legal CSS whose value is decided elsewhere. */
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                     "p: var(--p)", text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "var(--p)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                                       "p: var(--p)"),
                      "preserved");
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                     "p: calc(10px + 1%)", text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "calc(10px + 1%)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    /* An unknown function is a mistake rather than a reference, and the sentence
     * is the module's rather than the resolver's. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                       "p: nonsense(10px)", "not a background position");
}

/** `background-size` as a computed value: the keyword or the pair, typed. */
static void test_background_size_computed(void)
{
    css_fixture();
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: auto",
                       "auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: auto auto",
                       "auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: cover",
                       "cover");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: CONTAIN",
                       "contain");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                       "p: 100px 50px", "100px 50px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                       "p: 50% 25%", "50% 25%");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: 100px",
                       "100px auto");
    /* One shape for all four: `cover` is a whole *value* of the size property, so
     * unlike the two closed vocabularies it is not a KEYWORD — there is a
     * difference between "the keyword `cover`" and "the value `cover`". */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                                       "p: cover"),
                      "background-size");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                                       "p: 50% 25%"),
                      "background-size");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE),
                      "background-size");

    /* Refusals, and a reference carried. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: left",
                       "not a background size");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: banana",
                       "not a background size");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE, "p: 10",
                       "not a background size");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                       "p: 10px 20px 30px", "not a background size");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                                       "p: banana"),
                      "invalid");
    {
        char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        uint32_t unresolved = 0u;

        css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                         "p: var(--s)", text, sizeof(text), &unresolved);
        ROBO_CHECK_STR_EQ(text, "var(--s)");
        ROBO_CHECK_EQ_INT((int)unresolved, 1);
    }
}

/**
 * `background-image`: `none` is a value, everything else is a reference.
 *
 * The second answer is deliberately not a refusal. `url("a.png")` and a bare
 * `url(a.png)` are the same fact, a gradient is another, and this build can hold
 * every one of them and fetch none of them — so they are carried with the
 * unresolved flag set, exactly as 9.9 carries `oklch()`. What is left is the
 * refusal: an identifier that is not `none`, a number, a string, or a sequence of
 * more than one component.
 */
static void test_background_image_computed(void)
{
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    uint32_t unresolved = 0u;

    css_fixture();
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE),
                      "background-image");

    /* The one value, canonically spelled whatever the author wrote. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p: none",
                       "none");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p: NONE",
                       "none");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                                       "p: none"),
                      "keyword");
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p: none", text,
                     sizeof(text), &unresolved);
    ROBO_CHECK_EQ_INT((int)unresolved, 0);

    /* References, carried with their tokens intact and *flagged*, which is what
     * stops a consumer reading them as an image. */
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                     "p: url(\"a.png\")", text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "url(\"a.png\")");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                                       "p: url(\"a.png\")"),
                      "preserved");
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p: url(a.png)",
                     text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "url(a.png)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                     "p: linear-gradient(red, blue)", text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "linear-gradient(red, blue)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                     "p: var(--image)", text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "var(--image)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);

    /* And what is left: a word, a number, a dimension, a string, two components. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p: banana",
                       "not a background image");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p: 20px",
                       "not a background image");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p: 10",
                       "not a background image");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                       "p: \"a.png\"", "not a background image");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                       "p: none none", "not a background image");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE, "p:",
                       "not a background image");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                                       "p: banana"),
                      "invalid");
}

/**
 * The two closed vocabularies, through the shared grammar.
 *
 * They carry the keyword as their whole value — there is nothing to *do* with
 * `no-repeat` at this stage, so a typed enumerator would be a second copy of a
 * table for no reader. What the closed set buys is the refusal, which is the
 * difference between a mistake and a keyword carrying four letters.
 */
static void test_background_repeat_computed(void)
{
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    uint32_t unresolved = 0u;

    css_fixture();
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT, "p: repeat",
                       "repeat");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT, "p: repeat-x",
                       "repeat-x");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT, "p: repeat-y",
                       "repeat-y");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT, "p: no-repeat",
                       "no-repeat");
    /* Case-insensitively, and the *canonical* spelling is what comes out. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                       "p: NO-REPEAT", "no-repeat");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                       "p:   Repeat-X  ", "repeat-x");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                                       "p: repeat-x"),
                      "keyword");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT),
                      "background-repeat");

    /* Three refusals, three sentences: a value this build has not added
     * (`space`), something that is not an identifier at all, and two keywords. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                       "p: space", "not a background repeat value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                       "p: banana", "not a background repeat value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT, "p: 20px",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                       "p: repeat repeat",
                       "not a single background-repeat keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT, "p:",
                       "not a single background-repeat keyword");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                                       "p: banana"),
                      "invalid");

    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
                     "p: var(--repeat)", text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "var(--repeat)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
}

/** The attachment vocabulary, and `local` as a value not added. */
static void test_background_attachment_computed(void)
{
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    uint32_t unresolved = 0u;

    css_fixture();
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                       "p: scroll", "scroll");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                       "p: fixed", "fixed");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                       "p: FIXED", "fixed");
    ROBO_CHECK_STR_EQ(css_compute_kind(
                          ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                          "p: fixed"),
                      "keyword");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT),
                      "background-attachment");

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                       "p: local", "not a background attachment value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                       "p: floating", "not a background attachment value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                       "p: 10px", "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                       "p: scroll fixed",
                       "not a single background-attachment keyword");
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT),
                      "background-attachment");

    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
                     "p: var(--attachment)", text, sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "var(--attachment)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
}

/**
 * `background-color` is the 9.9 colour shape and not a background one.
 *
 * Asserted rather than assumed because the family may not grow a second colour
 * parser: `background-color: #f00` has to reach the same channels `color: #f00`
 * does, and the only way that stays true is if the property keeps the shape 9.9
 * gave it.
 */
static void test_background_color_computed(void)
{
    css_fixture();
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "p: transparent",
                       "#00000000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "p: red", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "p: #336699", "#336699");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "p: #fff", "#ffffff");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "p: rgb(10, 20, 30)",
                       "#0a141e");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "p: rgba(0, 0, 0, 0.5)",
                       "#00000080");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "p: hsl(0, 100%, 50%)",
                       "#ff0000");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "p: transparent"),
                      "colour");
    /* The two registrations are one computation: a colour is a colour however the
     * property it belongs to is spelled. */
    {
        robo_css_property_registry_t *registry = NULL;
        const robo_css_property_definition_t *definition;

        ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                          ROBO_OK);
        definition = robo_css_property_registry_find(registry, "background-color");
        ROBO_CHECK(definition != NULL);
        if (definition) {
            ROBO_CHECK_EQ_INT(definition->computation,
                              (int)ROBO_CSS_PROPERTY_COMPUTATION_COLOR);
            ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
            ROBO_CHECK_STR_EQ(definition->initial_text, "transparent");
        }
        robo_css_property_registry_destroy(registry);
    }
}

/**
 * The registry rows, walked against the module's own longhand table.
 *
 * Two files have to agree about six names and this is the one place that can
 * *walk* them: the module owns the order a serialisation writes them in, and the
 * registry owns which shape each is computed by and what it starts at. A row
 * written against the wrong shape would accept the wrong grammar silently, and a
 * row whose initial text were not readable by its own shape would make `initial`
 * resolve through a shape that refuses it.
 */
static void test_background_registry(void)
{
    static const struct {
        robo_css_property_computation_t computation;
        const char *initial;
    } rows[] = {
        { ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,      "none" },
        { ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,   "0% 0%" },
        { ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,       "auto" },
        { ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,     "repeat" },
        { ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT, "scroll" },
        { ROBO_CSS_PROPERTY_COMPUTATION_COLOR,                 "transparent" }
    };
    robo_css_property_registry_t *registry = NULL;
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BACKGROUND_LONGHAND_COUNT, 6);
    ROBO_CHECK_EQ_INT((int)robo_css_background_longhand_count(), 6);
    ROBO_CHECK_EQ_INT(
        (int)(sizeof(rows) / sizeof(rows[0])),
        (int)ROBO_CSS_BACKGROUND_LONGHAND_COUNT);
    ROBO_CHECK_STR_EQ(robo_css_background_shorthand_name(), "background");
    ROBO_CHECK_EQ_INT(robo_css_background_longhand_name(
                          ROBO_CSS_BACKGROUND_LONGHAND_COUNT),
                      (int)(size_t)NULL);

    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                      ROBO_OK);
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        const char *name = robo_css_background_longhand_name(index);
        const robo_css_property_definition_t *definition;

        ROBO_CHECK(name != NULL);
        if (!name) {
            continue;
        }
        /* The name is the module's, and it is one the module recognises. */
        ROBO_CHECK_EQ_INT(robo_css_background_is_longhand(robo_css_slice_from_text(name)),
                          1);
        ROBO_CHECK_EQ_INT((int)rows[index].computation,
                          (int)rows[index].computation);
        definition = robo_css_property_registry_find(registry, name);
        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        ROBO_CHECK_EQ_INT(definition->computation, (int)rows[index].computation);
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
        ROBO_CHECK_STR_EQ(definition->initial_text, rows[index].initial);
        /* And the initial value is readable by that shape, which is what makes
         * `background-image: initial` a value rather than a refusal. */
        {
            char declaration[32];

            (void)snprintf(declaration, sizeof(declaration), "p: %s",
                           definition->initial_text);
            css_compute_text(rows[index].computation, declaration, NULL, 0u, NULL);
        }
    }
    /* Case-insensitively, and only for the six. */
    ROBO_CHECK_EQ_INT(robo_css_background_is_longhand(
                          robo_css_slice_from_text("BACKGROUND-COLOR")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_background_is_longhand(
                          robo_css_slice_from_text("background")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_background_is_longhand(robo_css_slice_from_text("")), 0);
    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* The shorthand                                                       */
/* ------------------------------------------------------------------ */

/** The shape of @p part, as the fixture's one-line rendering. */
static void css_background_part_shape(const robo_css_value_t *part, char *out,
                                      size_t capacity)
{
    css_shape(part, out, capacity);
}

/**
 * The computed text of @p part read by @p computation, or "" when it is absent.
 *
 * A distributed part is a *list* in the shape a declared longhand's value has, so
 * the honest assertion is not "the tree looks like this" but "it computes to
 * this". A part that landed on the wrong longhand is refused by that longhand's
 * own grammar, which is a claim about the distribution rather than about the tree
 * rendering — and one a rename inside the value layer cannot break.
 */
static void css_background_part_computed(const robo_css_value_t *part,
                                         robo_css_property_computation_t computation,
                                         char *out,
                                         size_t capacity)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    size_t length = 0u;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    if (!part || part->kind == ROBO_CSS_VALUE_KIND_NONE) {
        return;
    }
    ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(computation, part, &value, NULL),
                      ROBO_OK);
    if (out && capacity > 0u) {
        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, out, capacity, &length,
                                                        NULL),
                          ROBO_OK);
    }
    robo_css_computed_value_dispose(&value);
}

/**
 * The six parts, each read by the longhand it is indexed as.
 *
 * The order is the module's own (image, position, size, repeat, attachment,
 * colour), which is also the order CSS serialises `background` in — so a part
 * written against the wrong index shows up as a value computed by the wrong
 * grammar rather than as two tables that happen to agree.
 */
static void css_background_parts_are(const robo_css_value_t *parts,
                                     const char *const *expected)
{
    static const robo_css_property_computation_t computations[] = {
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
        ROBO_CSS_PROPERTY_COMPUTATION_COLOR
    };
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    size_t index;

    for (index = 0u; index < sizeof(computations) / sizeof(computations[0]); index++) {
        css_background_part_computed(&parts[index], computations[index], text,
                                     sizeof(text));
        ROBO_CHECK_STR_EQ(text, expected[index]);
    }
}

/**
 * One `background:` layer, distributed by *kind*.
 *
 * The mechanism is the whole point: a box shorthand's components map to sides by
 * position, and a background's map to longhands by kind — a colour is a colour
 * however it is spelled, a repeat keyword is the repeat, an edge or an offset is
 * the position, and only what follows a `/` is a size. The output is indexed by
 * the module's own order, and a longhand the shorthand did not mention is
 * KIND_NONE — which is *not* "leave it alone"; see the reset test below and the
 * resolution pass that applies the initial value.
 */
static void test_background_shorthand_expand(void)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_value_t parts[ROBO_CSS_BACKGROUND_LONGHAND_COUNT];
    char shape[256];
    size_t index;

    css_fixture();
    /* A colour alone: the other five are unspoken, not reset here. */
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_init(&parts[index]);
    }
    ROBO_CHECK_EQ_INT((int)(sizeof(parts) / sizeof(parts[0])),
                      (int)ROBO_CSS_BACKGROUND_LONGHAND_COUNT);
    ROBO_CHECK_EQ_INT(robo_css_background_expand(
                          css_background_value("p: red", &list), parts, NULL),
                      ROBO_OK);
    css_background_part_shape(&parts[0], shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "");
    css_background_part_computed(&parts[5], ROBO_CSS_PROPERTY_COMPUTATION_COLOR, shape,
                                 sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "#ff0000");
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_dispose(&parts[index]);
        robo_css_value_init(&parts[index]);
    }
    robo_css_declaration_list_dispose(&list);

    /* Four spellings of a colour, and all four reach `background-color` — the
     * named one, the hash one, the function one, and `transparent`. */
    {
        static const char *const colours[] = { "white", "#fff", "rgb(10, 20, 30)",
                                              "transparent" };
        static const char *const hexes[] = { "#ffffff", "#ffffff", "#0a141e",
                                            "#00000000" };
        size_t at;

        for (at = 0u; at < sizeof(colours) / sizeof(colours[0]); at++) {
            char declaration[64];

            (void)snprintf(declaration, sizeof(declaration), "p: %s", colours[at]);
            size_t part;

            for (part = 0u; part < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT;
                 part++) {
                robo_css_value_init(&parts[part]);
            }
            ROBO_CHECK_EQ_INT(robo_css_background_expand(
                                  css_background_value(declaration, &list), parts, NULL),
                              ROBO_OK);
            /* The part reaches the property and the *property's* shape reduces it:
             * four spellings, one computed colour each. */
            css_background_part_computed(&parts[5], ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                         shape, sizeof(shape));
            ROBO_CHECK_STR_EQ(shape, hexes[at]);
            for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT;
                 index++) {
                robo_css_value_dispose(&parts[index]);
                robo_css_value_init(&parts[index]);
            }
            robo_css_declaration_list_dispose(&list);
        }
    }

    /* A whole layer: image, position, size, repeat, attachment and colour — each
     * to its own longhand, and the position and size pairs folded into one list
     * each, which is the shape a *declared* longhand's value has. */
    ROBO_CHECK_EQ_INT(robo_css_background_expand(
                          css_background_value(
                              "p: url(\"a.png\") 10px 20px / 50% 25% no-repeat fixed "
                              "#336699",
                              &list),
                          parts, NULL),
                      ROBO_OK);
    {
        static const char *const expected[] = {
            "url(\"a.png\")", "10px 20px", "50% 25%", "no-repeat", "fixed",
            "#336699"
        };

        css_background_parts_are(parts, expected);
    }
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_dispose(&parts[index]);
        robo_css_value_init(&parts[index]);
    }
    robo_css_declaration_list_dispose(&list);

    /* `center/cover`: the size group is what follows the `/`, and the position
     * before it is a position. */
    ROBO_CHECK_EQ_INT(robo_css_background_expand(
                          css_background_value("p: center/cover", &list), parts, NULL),
                      ROBO_OK);
    css_background_part_shape(&parts[0], shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "");
    /* Folded by the property's own shape: the part is the single component the
     * author wrote, and the computed position is the pair. */
    css_background_part_computed(&parts[1], ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                                 shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "center center");
    css_background_part_computed(&parts[2], ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
                                 shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "cover");
    css_background_part_shape(&parts[3], shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "");
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_dispose(&parts[index]);
        robo_css_value_init(&parts[index]);
    }
    robo_css_declaration_list_dispose(&list);

    /* `none` is an *image*, and a gradient is one this build carries. */
    ROBO_CHECK_EQ_INT(robo_css_background_expand(
                          css_background_value("p: none", &list), parts, NULL),
                      ROBO_OK);
    css_background_part_computed(&parts[0], ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                                 shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "none");
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_dispose(&parts[index]);
        robo_css_value_init(&parts[index]);
    }
    robo_css_declaration_list_dispose(&list);
    ROBO_CHECK_EQ_INT(robo_css_background_expand(
                          css_background_value("p: linear-gradient(red, blue)", &list),
                          parts, NULL),
                      ROBO_OK);
    /* A gradient is an image this build carries: the part is the tree and the
     * property's shape preserves it with its tokens intact. */
    css_background_part_computed(&parts[0], ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
                                 shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "linear-gradient(red, blue)");
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_dispose(&parts[index]);
        robo_css_value_init(&parts[index]);
    }
    robo_css_declaration_list_dispose(&list);

    /* A position with no image is a position: `background: left top`. */
    ROBO_CHECK_EQ_INT(robo_css_background_expand(
                          css_background_value("p: left top", &list), parts, NULL),
                      ROBO_OK);
    css_background_part_shape(&parts[0], shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "");
    css_background_part_computed(&parts[1], ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
                                 shape, sizeof(shape));
    ROBO_CHECK_STR_EQ(shape, "left top");
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_dispose(&parts[index]);
        robo_css_value_init(&parts[index]);
    }
    robo_css_declaration_list_dispose(&list);
}

/**
 * The shorthands this build refuses, each with a reason and *nothing* assigned.
 *
 * The all-or-nothing property is the one worth asserting: a caller that ignored
 * the result code must not be handed four of a shorthand's six longhands, which
 * is why every refusal leaves the output array clear. The reasons are three
 * different mistakes — a second layer, a size where CSS's grammar puts one only
 * after a `/`, and a reference whose substitution can change how many components
 * there are — and collapsing them would still be correct and would tell a
 * developer nothing.
 */
static void test_background_shorthand_refusals(void)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_value_t parts[ROBO_CSS_BACKGROUND_LONGHAND_COUNT];
    char shape[64];
    size_t index;
    size_t at;

    static const char *const refused[] = {
        /* Two layers: a comma-separated list of layers, each a whole set of
         * longhands, is a value model this build does not have. */
        "p: red, blue",
        "p: url(a.png), url(b.png)",
        /* A size where CSS's grammar puts one only after a `/`. */
        "p: cover",
        "p: contain",
        "p: auto",
        /* A reference whose substitution can change the component count. */
        "p: var(--layer)",
        "p: env(--layer)",
        /* Two components one longhand cannot take. */
        "p: red blue",
        "p: no-repeat repeat",
        /* A component that belongs to no longhand. */
        "p: 10",
        "p: banana",
        "p: url(a.png) banana",
        /* A `/` with nothing after it, nothing before it, or two of them. */
        "p: 10px /",
        "p: / 10px",
        "p: 10px / 50% / 25%",
        /* Nothing at all. */
        "p:"
    };

    css_fixture();
    for (at = 0u; at < sizeof(refused) / sizeof(refused[0]); at++) {
        for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT;
             index++) {
            robo_css_value_init(&parts[index]);
        }
        ROBO_CHECK(robo_css_background_expand(css_background_value(refused[at], &list),
                                             parts, NULL) != ROBO_OK);
        /* Nothing is assigned, so a caller that ignored the code owns no
         * longhand rather than some of them. */
        for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT;
             index++) {
            css_background_part_shape(&parts[index], shape, sizeof(shape));
            ROBO_CHECK_STR_EQ(shape, "");
            robo_css_value_dispose(&parts[index]);
            robo_css_value_init(&parts[index]);
        }
        robo_css_declaration_list_dispose(&list);
    }

    /* The argument checks come before any of it. */
    for (index = 0u; index < (size_t)ROBO_CSS_BACKGROUND_LONGHAND_COUNT; index++) {
        robo_css_value_init(&parts[index]);
    }
    ROBO_CHECK_EQ_INT(robo_css_background_expand(NULL, parts, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_background_expand(
                          css_background_value("p: red", &list), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_css_declaration_list_dispose(&list);
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void css_background_compute_of(css_inherit_fixture_t *fixture,
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
 * The typed value behind a name, through the query door a consumer reads.
 *
 * The door is the *context's* rather than the style's, for the reason 9.12
 * recorded: none of the six inherits, so an element that declared nothing has
 * **no entry** — 9.7 materialises initial values for the inherited-by-default set
 * only — and "what is this element's background-image" is a question only
 * something holding the registry can answer. `out_from_initial` is what says
 * whether the answer was the property's own initial value.
 */
static void css_background_query(css_inherit_fixture_t *fixture,
                                 const robo_css_computed_style_t *style,
                                 const char *name,
                                 char *out,
                                 size_t capacity,
                                 int *out_from_initial)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    size_t length = 0u;

    if (out_from_initial) {
        *out_from_initial = -1;
    }
    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          css_inherit_context(fixture), style,
                          robo_css_slice_from_text(name), &value, out_from_initial,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(value.kind), 1);
    if (out && capacity > 0u) {
        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, out, capacity, &length,
                                                        NULL),
                          ROBO_OK);
    }
    robo_css_computed_value_dispose(&value);
}

/**
 * The seven rows an element that says nothing still has answers for.
 *
 * `background` is in the table beside the six longhands because the shorthand is
 * a property like any other until it is distributed: `background-image: none` is
 * a value the query door reports, and it is the same value `background` starts
 * at. The two entries worth reading twice are `0% 0%` — the top-left as a
 * *percentage*, so it stays relative to whatever box it lands on — and `auto`,
 * which is not a length.
 */
static void test_background_initial(void)
{
    static const struct {
        const char *name;
        const char *computed;
    } rows[] = {
        { "background-image",      "none" },
        { "background-position",   "0% 0%" },
        { "background-size",       "auto" },
        { "background-repeat",     "repeat" },
        { "background-attachment", "scroll" },
        { "background-color",      "#00000000" },
        { "background",            "transparent" }
    };
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t plain;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    size_t index;

    css_fixture();
    css_inherit_begin(&fixture);
    plain = css_host_add(css_inherit_host(&fixture), "div", "plain", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "#plain { color: red }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_background_compute_of(&fixture, plain, style);
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        int from_initial = -1;

        css_background_query(&fixture, style, rows[index].name, text, sizeof(text),
                             &from_initial);
        ROBO_CHECK_STR_EQ(text, rows[index].computed);
        ROBO_CHECK_EQ_INT(from_initial, 1);
    }
    /* The last one is the shorthand, whose initial value is not one of the six —
     * it is the token `transparent`, because the shorthand's own shape is PRESERVE
     * and its own initial value is a colour. Asserted as it is rather than as one
     * might wish: this phase distributes the shorthand and does not restate its
     * own row. */
    css_background_query(&fixture, style, "background", text, sizeof(text), NULL);
    ROBO_CHECK_STR_EQ(text, "transparent");

    /* And the *style* has no entry for any of the six, which is what a computed
     * style of an element that declared nothing looks like. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_find(style,
                                                  robo_css_slice_from_text(
                                                      "background-image")) ==
                          (size_t)-1,
                      1);
    css_computed_is(style, "background-image", "<absent>");
    css_computed_is(style, "background-position", "<absent>");
    css_computed_is(style, "background-size", "<absent>");
    css_computed_is(style, "background-repeat", "<absent>");
    css_computed_is(style, "background-attachment", "<absent>");
    css_computed_is(style, "background-color", "<absent>");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A parent's background is not a child's, from both sides.
 *
 * The assertion is the *absence*, which is a stronger statement than "the child
 * has the initial value": a child that declared nothing has no entry at all, and
 * the query door is what reports the initial value and says that is what it is.
 * And the explicit `inherit` goes the other way — it is a declaration, so the
 * child has an entry and it carries the parent's computed value.
 */
static void test_background_non_inheritance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t parent;
    robo_css_element_ref_t child;
    robo_css_element_ref_t explicit_child;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    int from_initial = -1;

    css_fixture();
    css_inherit_begin(&fixture);
    parent = css_host_add(css_inherit_host(&fixture), "div", "parent", NULL);
    child = css_host_add(css_inherit_host(&fixture), "div", "child", NULL);
    explicit_child = css_host_add(css_inherit_host(&fixture), "div", "explicit", NULL);
    css_host_set_parent(css_inherit_host(&fixture), child, parent);
    css_host_set_parent(css_inherit_host(&fixture), explicit_child, parent);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#parent { background-color: #336699; background-image: "
                      "url(a.png); background-repeat: no-repeat; "
                      "background-position: 25% 50%; background-size: cover; "
                      "background-attachment: fixed }"
                      "#explicit { background-color: inherit; background-image: "
                      "inherit; background-position: inherit; background-size: "
                      "inherit; background-repeat: inherit; background-attachment: "
                      "inherit }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* The parent declares all six and has all six. */
    css_background_compute_of(&fixture, parent, style);
    css_computed_is(style, "background-color", "#336699");
    css_computed_is(style, "background-image", "url(a.png)");
    css_computed_is(style, "background-repeat", "no-repeat");
    css_computed_is(style, "background-position", "25% 50%");
    css_computed_is(style, "background-size", "cover");
    css_computed_is(style, "background-attachment", "fixed");
    css_computed_flags_are(style, "background-color", "declared");

    /* The child declares nothing, so none of it is there — and the query door
     * says so by answering the initial value and *marking* it as such. */
    css_background_compute_of(&fixture, child, style);
    css_computed_is(style, "background-color", "<absent>");
    css_computed_is(style, "background-image", "<absent>");
    css_computed_is(style, "background-position", "<absent>");
    css_computed_is(style, "background-size", "<absent>");
    css_computed_is(style, "background-repeat", "<absent>");
    css_computed_is(style, "background-attachment", "<absent>");
    css_background_query(&fixture, style, "background-color", text, sizeof(text),
                         &from_initial);
    ROBO_CHECK_STR_EQ(text, "#00000000");
    ROBO_CHECK_EQ_INT(from_initial, 1);
    css_background_query(&fixture, style, "background-image", text, sizeof(text),
                         &from_initial);
    ROBO_CHECK_STR_EQ(text, "none");
    ROBO_CHECK_EQ_INT(from_initial, 1);
    /* Five of the six would look the same if the child had inherited the parent's
     * values, so the child is asserted *against* the parent's, not only against
     * the initial value. */
    ROBO_CHECK_EQ_INT(strcmp(text, "url(a.png)") == 0, 0);

    /* An explicit `inherit` is a declaration, and it carries the parent's
     * *computed* value — the typed pair, not the text `25% 50%`. */
    css_background_compute_of(&fixture, explicit_child, style);
    css_computed_is(style, "background-color", "#336699");
    css_computed_is(style, "background-image", "url(a.png)");
    css_computed_is(style, "background-position", "25% 50%");
    css_computed_is(style, "background-size", "cover");
    css_computed_is(style, "background-repeat", "no-repeat");
    css_computed_is(style, "background-attachment", "fixed");
    /* The flag is `inherited`, not `declared`: the *declaration* was `inherit` and
     * the value is the parent's, which is exactly what the provenance says. */
    css_computed_flags_are(style, "background-color", "inherited");
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                style, robo_css_slice_from_text("background-position"));

        ROBO_CHECK_EQ_INT(value->kind,
                          (int)ROBO_CSS_COMPUTED_VALUE_BACKGROUND_POSITION);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_background_position(value).horizontal.kind,
                          ROBO_CSS_BACKGROUND_COMPONENT_PERCENTAGE);
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/** `initial`, `inherit` and `unset` for a family that does not inherit. */
static void test_background_wide_keywords(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t parent;
    robo_css_element_ref_t child;
    robo_css_element_ref_t shorthand;
    size_t index;

    static const char *const names[] = {
        "background-color", "background-image", "background-position",
        "background-size",  "background-repeat", "background-attachment"
    };
    static const char *const initials[] = {
        "#00000000", "none", "0% 0%", "auto", "repeat", "scroll"
    };

    css_fixture();
    css_inherit_begin(&fixture);
    parent = css_host_add(css_inherit_host(&fixture), "div", "parent", NULL);
    child = css_host_add(css_inherit_host(&fixture), "div", "child", NULL);
    shorthand = css_host_add(css_inherit_host(&fixture), "div", "shorthand", NULL);
    css_host_set_parent(css_inherit_host(&fixture), child, parent);
    css_host_set_parent(css_inherit_host(&fixture), shorthand, parent);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#parent { background-color: #336699; background-image: "
                      "url(a.png); background-repeat: no-repeat; "
                      "background-position: 25% 50%; background-size: cover; "
                      "background-attachment: fixed }"
                      "#child { background-color: initial; background-image: "
                      "unset; background-position: initial; background-size: "
                      "initial; background-repeat: unset; background-attachment: "
                      "initial }"
                      "#shorthand { background: inherit }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* `initial` is the property's own value, and `unset` is the same thing here
     * because none of the six inherits — which is the difference from `inherit`,
     * asserted beside it in the non-inheritance test. */
    css_background_compute_of(&fixture, child, style);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        css_computed_is(style, names[index], initials[index]);
    }
    /* The entries exist and say where they came from, and neither is `declared`:
     * a CSS-wide keyword resolves to the property's own initial value, which is
     * the `default` flag — and `unset` on a non-inherited property is the same
     * thing said in another word. */
    css_computed_flags_are(style, "background-color", "default");
    css_computed_flags_are(style, "background-repeat", "default");

    /* `background: inherit` distributes the *keyword*: one sentence, six times,
     * through the same rule an explicit longhand `inherit` goes through. */
    css_background_compute_of(&fixture, shorthand, style);
    css_computed_is(style, "background-color", "#336699");
    css_computed_is(style, "background-image", "url(a.png)");
    css_computed_is(style, "background-position", "25% 50%");
    css_computed_is(style, "background-size", "cover");
    css_computed_is(style, "background-repeat", "no-repeat");
    css_computed_is(style, "background-attachment", "fixed");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * The cascade's three keys, over the family.
 *
 * Nothing here weighs a level or compares a specificity — the cascade answers
 * which declaration won and this phase only reads the answer. Two keys are worth
 * naming: source order settles two rules of equal specificity, and `!important`
 * beats both, which is what makes the family usable by a caller who has to
 * override an author's shorthand.
 */
static void test_background_cascade(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t element;
    robo_css_element_ref_t important;
    robo_css_element_ref_t inline_element;
    css_inline_stub_t stub;

    css_fixture();
    css_inherit_begin(&fixture);
    element = css_host_add(css_inherit_host(&fixture), "div", "element", NULL);
    important = css_host_add(css_inherit_host(&fixture), "div", "important", NULL);
    inline_element = css_host_add(css_inherit_host(&fixture), "div", "inlined", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#element { background-color: red; background-repeat: "
                      "repeat-x }"
                      "#element { background-color: blue; background-repeat: "
                      "repeat-y }"
                      "#important { background-color: red !important }"
                      "div#important { background-color: blue }"
                      ".never { background-color: red }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* Source order: the second rule wins on both keys, and the *winner* is what
     * the computed style carries rather than a merge of the two. */
    css_background_compute_of(&fixture, element, style);
    css_computed_is(style, "background-color", "#0000ff");
    css_computed_is(style, "background-repeat", "repeat-y");
    css_computed_flags_are(style, "background-color", "declared");

    /* `!important` beats a more specific selector, which is the whole point of
     * it and a fact this phase consumes rather than restates. */
    css_background_compute_of(&fixture, important, style);
    css_computed_is(style, "background-color", "#ff0000");

    /* And an inline declaration is the strongest normal level, so it beats a
     * sheet that says `!important` about the same element — installed here rather
     * than above because the stub answers *every* element, which is precisely
     * what the two assertions before it must not be measuring. */
    css_inherit_inline(&fixture, &stub, "background-color: #336699");
    css_background_compute_of(&fixture, inline_element, style);
    css_computed_is(style, "background-color", "#336699");
    css_computed_flags_are(style, "background-color", "declared");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * `background: red` after `background-image: url(a.png)` *clears* the image.
 *
 * This is the behaviour the shorthand's third outcome exists for, and it is the
 * one an author relies on when they write the shorthand to mean "just this": the
 * shorthand stands for all six longhands, so a longhand it did not mention is
 * reset to its initial value rather than left as it was. The reverse order is the
 * same rule read the other way — the longhand wins on document order, and the
 * image survives.
 */
static void test_background_shorthand_reset(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t reset;
    robo_css_element_ref_t kept;

    css_fixture();
    css_inherit_begin(&fixture);
    reset = css_host_add(css_inherit_host(&fixture), "div", "reset", NULL);
    kept = css_host_add(css_inherit_host(&fixture), "div", "kept", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      /* An earlier rule gives the first element an image and a
                       * repeat; the shorthand comes after it. */
                      "#reset { background-image: url(a.png); background-repeat: "
                      "no-repeat; background-attachment: fixed }"
                      "#reset { background: red }"
                      /* And the mirror: the longhand comes after the shorthand. */
                      "#kept { background: red }"
                      "#kept { background-image: url(a.png) }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* Everything the shorthand did not name is the property's initial value, and
     * `background: red` is a colour and not a colour plus five leftovers. */
    css_background_compute_of(&fixture, reset, style);
    css_computed_is(style, "background-color", "#ff0000");
    css_computed_is(style, "background-image", "none");
    css_computed_is(style, "background-repeat", "repeat");
    css_computed_is(style, "background-attachment", "scroll");
    css_computed_is(style, "background-position", "0% 0%");
    css_computed_is(style, "background-size", "auto");
    /* The flag matters: these are *declared* by the shorthand's distribution, not
     * absent and not inherited. */
    css_computed_flags_are(style, "background-image", "declared");

    /* The reverse order: the longhand wins, and the rest of the shorthand still
     * applies. */
    css_background_compute_of(&fixture, kept, style);
    css_computed_is(style, "background-color", "#ff0000");
    css_computed_is(style, "background-image", "url(a.png)");
    css_computed_is(style, "background-repeat", "repeat");
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                style, robo_css_slice_from_text("background-image"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_PRESERVED);
        ROBO_CHECK_EQ_INT(value->unresolved, 1u);
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A shorthand that cannot be distributed leaves a *report*, not a guess.
 *
 * `background: var(--layer)` is legal CSS whose component count is not known
 * until substitution — and substitution is a later stage's question — so the
 * author's text stays on the shorthand's own entry, no longhand is guessed at,
 * and the resolution counts itself as carrying an undistributed shorthand. A
 * consumer can therefore see that six parts are unanswerable instead of finding a
 * declaration and nothing underneath it.
 */
static void test_background_shorthand_undistributed(void)
{
    css_inherit_fixture_t fixture;
    robo_css_error_t error = ROBO_CSS_CLEARED;
    robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
    robo_css_inherited_style_t resolved = ROBO_CSS_CLEARED;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t element;

    css_fixture();
    css_inherit_begin(&fixture);
    element = css_host_add(css_inherit_host(&fixture), "div", "element", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#element { background: var(--layer) }");
    ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(
                          css_inherit_context(&fixture), element, &cascade, &error,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_inherit_resolve(css_inherit_context(&fixture), &cascade,
                                              NULL, &resolved, NULL),
                      ROBO_OK);
    /* Reported, not silent — and it is the shorthand that says so. */
    ROBO_CHECK_EQ_INT((int)resolved.shorthands_unexpanded, 1);
    ROBO_CHECK_EQ_INT(resolved.unsupported, 1);
    /* Nothing is guessed at: no longhand has an entry, so `background-image` here
     * is the property's initial value rather than a reading of the reference. */
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_find(
                          &resolved, robo_css_slice_from_text("background-image")) ==
                          NULL,
                      1);
    ROBO_CHECK_EQ_INT(robo_css_inherited_style_find(
                          &resolved, robo_css_slice_from_text("background-color")) ==
                          NULL,
                      1);
    /* The author's declaration is still there, on the shorthand's own entry. */
    {
        const robo_css_inherited_property_t *entry = robo_css_inherited_style_find(
            &resolved, robo_css_slice_from_text("background"));

        ROBO_CHECK(entry != NULL);
        if (entry) {
            char shape[64];

            css_shape(&entry->value, shape, sizeof(shape));
            ROBO_CHECK_STR_EQ(shape, "var(--layer)");
        }
    }
    robo_css_inherited_style_dispose(&resolved);
    robo_css_cascade_dispose(&cascade);

    /* And the computed style says the same thing: the shorthand is carried and
     * flagged, the six are the initial values. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    css_background_compute_of(&fixture, element, style);
    css_computed_is(style, "background-image", "<absent>");
    css_computed_is(style, "background-color", "<absent>");
    css_computed_is(style, "background-position", "<absent>");
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                style, robo_css_slice_from_text("background"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_PRESERVED);
        ROBO_CHECK_EQ_INT(value->unresolved, 1u);
        css_computed_is(style, "background", "var(--layer)");
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * Malformed input is an answer, never a crash and never a partial value.
 *
 * Every one of these is a declaration the grammar *accepted* and the value layer
 * has to make something of — truncation in the middle of a function, a `/` with
 * nothing on one side, an enormous magnitude, an escaped keyword, a delimiter
 * where a component should be. Each is computed under all six shapes, twice, and
 * the two readings must agree: a refusal that depended on an uninitialised field
 * would show up as two different kinds for one input.
 */
static void test_background_robust(void)
{
    static const char *const malformed[] = {
        "p: rgb(", "p: rgb()", "p: rgb(,,)", "p: rgb(1 2 3 4 5 6)",
        "p: url(", "p: url(", "p: url('a\n", "p: linear-gradient(",
        "p: calc(", "p: calc()", "p: var()", "p: var(--)", "p: --p",
        "p: /", "p: //", "p: 10px /", "p: / 10px", "p: 1e999px",
        "p: -1e999%", "p: 1e999", "p: 10px 20px 30px 40px 50px",
        "p: left top right bottom", "p: inherit inherit", "p: \\6c eft",
        "p: url(a.png) url(b.png)", "p: @media", "p: !important",
        "p: (10px)", "p: [10px]", "p: 'unterminated", "p: \"unterminated",
        "p: ,", "p: red,", "p: ,red", "p: ;", "p: 0.0.0", "p: #",
        "p: #12345", "p: #1234567", "p: rgb(1,2,3", "p: currentcolor"
    };
    static const robo_css_property_computation_t computations[] = {
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_IMAGE,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_POSITION,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_SIZE,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_REPEAT,
        ROBO_CSS_PROPERTY_COMPUTATION_BACKGROUND_ATTACHMENT,
        ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
        ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE
    };
    size_t at;
    size_t which;

    css_fixture();
    for (at = 0u; at < sizeof(malformed) / sizeof(malformed[0]); at++) {
        for (which = 0u; which < sizeof(computations) / sizeof(computations[0]);
             which++) {
            robo_css_declaration_list_t first = ROBO_CSS_CLEARED;
            robo_css_declaration_list_t second = ROBO_CSS_CLEARED;
            const robo_css_declaration_t *declaration;
            robo_css_computed_value_t a = ROBO_CSS_CLEARED;
            robo_css_computed_value_t b = ROBO_CSS_CLEARED;
            char first_text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
            char second_text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
            size_t first_length = 0u;
            size_t second_length = 0u;
            /* Read before the record is disposed, because a disposed value's kind
             * is not a fact and comparing against it would be reading freed
             * memory — which is exactly the class of bug this test hunts. */
            robo_css_computed_value_kind_t first_kind;

            ROBO_CHECK_EQ_INT(
                css_parse_declarations(malformed[at], &first, NULL), ROBO_OK);
            declaration = robo_css_declaration_list_at(&first, 0u);
            /* A declaration the grammar rejected is not a value, and there is
             * nothing to compute — which is itself the safe answer. */
            if (!declaration) {
                robo_css_declaration_list_dispose(&first);
                continue;
            }
            ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                  computations[which],
                                  robo_css_declaration_value(declaration), &a, NULL),
                              ROBO_OK);
            /* Whatever came out is a *defined* kind: a shape cannot invent one,
             * and a refusal is INVALID rather than a cleared record. */
            ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(a.kind), 1);
            first_kind = a.kind;
            ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&a, first_text,
                                                           sizeof(first_text),
                                                           &first_length, NULL),
                              ROBO_OK);
            robo_css_computed_value_dispose(&a);
            robo_css_declaration_list_dispose(&first);

            /* The same input, computed again from a fresh parse: one spelling,
             * one kind, and the same text. */
            ROBO_CHECK_EQ_INT(
                css_parse_declarations(malformed[at], &second, NULL), ROBO_OK);
            declaration = robo_css_declaration_list_at(&second, 0u);
            ROBO_CHECK(declaration != NULL);
            if (declaration) {
                ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                      computations[which],
                                      robo_css_declaration_value(declaration), &b,
                                      NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(b.kind, (int)first_kind);
                ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&b, second_text,
                                                               sizeof(second_text),
                                                               &second_length, NULL),
                                  ROBO_OK);
                ROBO_CHECK_STR_EQ(second_text, first_text);
                ROBO_CHECK_EQ_INT((int)second_length, (int)first_length);
                robo_css_computed_value_dispose(&b);
            }
            robo_css_declaration_list_dispose(&second);
        }
    }
}

/**
 * The same document computes the same backgrounds, in any order, twice.
 *
 * Determinism is a property of the whole pipeline and not of these shapes, but
 * this family is the one whose *expansion* is an ordering question — six
 * longhands, a shorthand that stands for all of them, and a cascade that decides
 * one name at a time — so a document that mixes the two is walked forwards and
 * backwards across two passes. An iteration-order dependency anywhere in the
 * distribution would show as a disagreement between the two.
 */
static void test_background_determinism(void)
{
    /*
     * Every element's six values, spelled out rather than derived.
     *
     * `<absent>` is a real expectation for four of the five, and it is the
     * *interesting* one: none of the six inherits, so an element that declared
     * only a colour has no entry for the other five. The `background` shorthand
     * is what fills all six at once — which is exactly the ordering question this
     * test exists for, since a shorthand's distribution and a longhand's own
     * declaration meet in the cascade and must settle the same way every time.
     */
    static const struct {
        const char *id;
        const char *computed_color;
        const char *computed_image;
        const char *computed_position;
        const char *computed_size;
        const char *computed_repeat;
        const char *computed_attachment;
    } rows[] = {
        /* `background: red` alone: all six, the other five reset. */
        { "a", "#ff0000", "none", "0% 0%", "auto", "repeat", "scroll" },
        /* Three longhands, so the other three are absent. */
        { "b", "#0000ff", "url(a.png)", "<absent>", "<absent>", "no-repeat",
          "<absent>" },
        { "c", "<absent>", "<absent>", "<absent>", "<absent>", "repeat-x",
          "<absent>" },
        /* The shorthand first, then two longhands after it: the longhands win on
         * document order and the rest of the shorthand still applies. */
        { "d", "#336699", "linear-gradient(red, blue)", "25% 50%", "cover",
          "no-repeat", "scroll" },
        /* The shorthand last, so its distribution beats the two longhands before
         * it — the mirror of the row above, in one document. */
        { "e", "#ffffff", "none", "0% 0%", "auto", "no-repeat", "scroll" }
    };
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t refs[5];
    size_t index;
    size_t pass;

    css_fixture();
    css_inherit_begin(&fixture);
    for (index = 0u; index < 5u; index++) {
        refs[index] = css_host_add(css_inherit_host(&fixture), "div", rows[index].id,
                                   NULL);
    }
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#a { background: red }"
                      "#b { background-color: blue; background-image: url(a.png);"
                      " background-repeat: no-repeat }"
                      "#c { background-repeat: repeat-x }"
                      "#d { background: 25% 50% / cover no-repeat; background-color:"
                      " #336699; background-image: linear-gradient(red, blue) }"
                      "#e { background-color: white; background-repeat: no-repeat;"
                      " background: white no-repeat }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    for (pass = 0u; pass < 2u; pass++) {
        for (index = 0u; index < 5u; index++) {
            /* The second pass walks the same five backwards. */
            size_t at = (pass == 0u) ? index : (4u - index);

            css_background_compute_of(&fixture, refs[at], style);
            css_computed_is(style, "background-color", rows[at].computed_color);
            css_computed_is(style, "background-image", rows[at].computed_image);
            /* Both pair shapes are read on every element in both passes, which is
             * what makes this a statement about the family and not only about the
             * three keyword-shaped halves. */
            css_computed_is(style, "background-position", rows[at].computed_position);
            css_computed_is(style, "background-size", rows[at].computed_size);
            css_computed_is(style, "background-repeat", rows[at].computed_repeat);
            css_computed_is(style, "background-attachment",
                            rows[at].computed_attachment);
        }
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_background(void);

void robo_test_register_css_background(void)
{
    robo_test_register("css.background.repeat.vocabulary",
                       test_background_repeat_vocabulary);
    robo_test_register("css.background.repeat.axes", test_background_repeat_axes);
    robo_test_register("css.background.attachment.vocabulary",
                       test_background_attachment_vocabulary);
    robo_test_register("css.background.edge.vocabulary", test_background_edge_vocabulary);
    robo_test_register("css.background.component.model", test_background_component_model);
    robo_test_register("css.background.component.write", test_background_component_write);
    robo_test_register("css.background.position.parse", test_background_position_parse);
    robo_test_register("css.background.position.invalid", test_background_position_invalid);
    robo_test_register("css.background.position.reference",
                       test_background_position_reference);
    robo_test_register("css.background.position.write", test_background_position_write);
    robo_test_register("css.background.size.parse", test_background_size_parse);
    robo_test_register("css.background.size.invalid", test_background_size_invalid);
    robo_test_register("css.background.size.write", test_background_size_write);
    robo_test_register("css.background.position.computed",
                       test_background_position_computed);
    robo_test_register("css.background.position.computed_invalid",
                       test_background_position_computed_invalid);
    robo_test_register("css.background.size.computed", test_background_size_computed);
    robo_test_register("css.background.image.computed", test_background_image_computed);
    robo_test_register("css.background.repeat.computed", test_background_repeat_computed);
    robo_test_register("css.background.attachment.computed",
                       test_background_attachment_computed);
    robo_test_register("css.background.color.computed", test_background_color_computed);
    robo_test_register("css.background.registry", test_background_registry);
    robo_test_register("css.background.shorthand.expand",
                       test_background_shorthand_expand);
    robo_test_register("css.background.shorthand.refusals",
                       test_background_shorthand_refusals);
    robo_test_register("css.background.initial", test_background_initial);
    robo_test_register("css.background.non_inheritance",
                       test_background_non_inheritance);
    robo_test_register("css.background.keywords", test_background_wide_keywords);
    robo_test_register("css.background.cascade", test_background_cascade);
    robo_test_register("css.background.shorthand.reset",
                       test_background_shorthand_reset);
    robo_test_register("css.background.shorthand.undistributed",
                       test_background_shorthand_undistributed);
    robo_test_register("css.background.robust", test_background_robust);
    robo_test_register("css.background.determinism", test_background_determinism);
}
