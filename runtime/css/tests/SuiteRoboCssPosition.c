/*
 * RoboCSS positioning suite (Phase 9.13) — the vocabulary, its two columns, the
 * four inset properties, `z-index`, and what the pipeline makes of a declaration.
 *
 * Four groups, in the order the phase is built:
 *
 *   the vocabulary  the five values walked rather than a representative one typed
 *                   out, with both columns read from the same rows
 *   classification  what a later phase asks — is there a containing block, does
 *                   an offset apply, does the box still take up room — asserted
 *                   for every value and cross-checked so two values cannot answer
 *                   the same way by accident
 *   the family      the four insets as *one* walk over the side vocabulary rather
 *                   than four hand-written cases, and `z-index` with its three
 *                   different refusals
 *   integration     a real element: the initial values, `inherit`, `initial`,
 *                   `unset`, non-inheritance proved from both sides, the
 *                   cascade's three keys, and the query door a consumer reads
 *
 * Nothing here computes a box. A positioning value at this phase is a *value*
 * with two facts in it, an inset is a length, a percentage or `auto`, and
 * `z-index` is a keyword or a whole number — and every expectation below is about
 * the value, what it classifies as, or where it came from. That is the whole
 * difference between a computed offset and an applied one.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * The five values, and what each is spelled as.
 *
 * The spelling is the assertion, not the enumeration: a table whose rows and
 * enumerators had drifted would still answer `is_known` for all five and would
 * only show up as a value that printed as another one's keyword. Walking the set
 * in enumerator order and reading every name back is what makes that visible.
 */
static void test_position_vocabulary(void)
{
    static const char *const keywords[] = { "static", "relative", "absolute", "fixed",
                                            "sticky" };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_POSITIONING_COUNT, 5);
    ROBO_CHECK_EQ_INT((int)robo_css_positioning_count(), 5);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        robo_css_positioning_t position = (robo_css_positioning_t)index;

        ROBO_CHECK_EQ_INT(robo_css_positioning_is_known(position), 1);
        ROBO_CHECK_STR_EQ(robo_css_positioning_name(position), keywords[index]);
        ROBO_CHECK_EQ_INT(robo_css_positioning_from_text(keywords[index]), (int)position);
    }
    /* The enumerator past the end and a value nothing defined answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_known(
                          (robo_css_positioning_t)ROBO_CSS_POSITIONING_COUNT),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_known((robo_css_positioning_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_css_positioning_name((robo_css_positioning_t)99), "");
}

/**
 * The flow column, for every value.
 *
 * `relative` and `sticky` are the rows that earn the column rather than the
 * spelling: both are in flow *and* positioned, which is the pair that reads as a
 * contradiction until you notice they are two questions. `static` answers yes to
 * flow and no to positioned — the one row where the two disagree — and `absolute`
 * and `fixed` answer no to the first.
 */
static void test_position_flow(void)
{
    static const struct {
        robo_css_positioning_t position;
        int in_flow;
        int out_of_flow;
    } rows[] = {
        { ROBO_CSS_POSITIONING_STATIC,   1, 0 },
        { ROBO_CSS_POSITIONING_RELATIVE, 1, 0 },
        { ROBO_CSS_POSITIONING_ABSOLUTE, 0, 1 },
        { ROBO_CSS_POSITIONING_FIXED,    0, 1 },
        { ROBO_CSS_POSITIONING_STICKY,   1, 0 }
    };
    size_t index;

    css_fixture();
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_css_positioning_is_in_flow(rows[index].position),
                          rows[index].in_flow);
        ROBO_CHECK_EQ_INT(robo_css_positioning_is_out_of_flow(rows[index].position),
                          rows[index].out_of_flow);
    }
    /* An unknown enumerator is *not* out of flow: "this box is somewhere else" is
     * a claim a value nothing defined cannot support, and the two predicates are
     * therefore not each other's negation. */
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_in_flow((robo_css_positioning_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_out_of_flow((robo_css_positioning_t)99), 0);
}

/**
 * The positioned column, and that `static` alone is false.
 *
 * This is the fact that does two jobs, and both are worth naming here because
 * they are the reason it is a column: an offset applies only to a positioned box
 * (`top` on a `static` element is ignored rather than honoured), and a positioned
 * box is what an absolutely positioned descendant resolves its offsets against.
 */
static void test_position_positioned(void)
{
    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(ROBO_CSS_POSITIONING_STATIC), 0);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(ROBO_CSS_POSITIONING_RELATIVE), 1);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(ROBO_CSS_POSITIONING_ABSOLUTE), 1);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(ROBO_CSS_POSITIONING_FIXED), 1);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(ROBO_CSS_POSITIONING_STICKY), 1);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned((robo_css_positioning_t)99), 0);
}

/**
 * The three questions partition the five values, and the one collision is named.
 *
 * Without a cross-check this suite would pass for a table whose rows had been
 * filled in carelessly, because every expectation above is written per value and
 * a reader can copy a wrong one. So every pair is compared on *the whole triple*,
 * and the count of pairs that agree must be one.
 *
 * There are **two**, and both are named rather than merely counted:
 *
 *   relative / sticky     separated only by what happens when the document
 *                         scrolls — a sticky box sticks — and scrolling does not
 *                         exist in this build
 *   absolute / fixed      separated only by what an offset resolves *against*: the
 *                         nearest positioned ancestor's box, or the viewport
 *
 * So a table whose two rows agree on every column this phase has is the *correct*
 * answer, and it is the honest shape of a phase that stops here: four distinct
 * answers over five values, with each collision being exactly one unimplemented
 * question. Naming them means a *third* collision fails the test, and the day one
 * of these two gains a column the pair drops out of the list and this says so.
 */
static void test_position_classification(void)
{
    size_t collisions = 0u;
    int saw_relative_sticky = 0;
    int saw_absolute_fixed = 0;
    int saw_unexpected = 0;
    size_t a;
    size_t b;

    css_fixture();
    for (a = 0u; a < (size_t)ROBO_CSS_POSITIONING_COUNT; a++) {
        for (b = a + 1u; b < (size_t)ROBO_CSS_POSITIONING_COUNT; b++) {
            robo_css_positioning_t left = (robo_css_positioning_t)a;
            robo_css_positioning_t right = (robo_css_positioning_t)b;

            if (robo_css_positioning_is_in_flow(left) !=
                    robo_css_positioning_is_in_flow(right) ||
                robo_css_positioning_is_out_of_flow(left) !=
                    robo_css_positioning_is_out_of_flow(right) ||
                robo_css_positioning_is_positioned(left) !=
                    robo_css_positioning_is_positioned(right)) {
                continue;
            }
            collisions++;
            if (left == ROBO_CSS_POSITIONING_RELATIVE &&
                right == ROBO_CSS_POSITIONING_STICKY) {
                saw_relative_sticky = 1;
            } else if (left == ROBO_CSS_POSITIONING_ABSOLUTE &&
                       right == ROBO_CSS_POSITIONING_FIXED) {
                saw_absolute_fixed = 1;
            } else {
                saw_unexpected = 1;
            }
        }
    }
    ROBO_CHECK_EQ_INT((int)collisions, 2);
    ROBO_CHECK_EQ_INT(saw_relative_sticky, 1);
    ROBO_CHECK_EQ_INT(saw_absolute_fixed, 1);
    ROBO_CHECK_EQ_INT(saw_unexpected, 0);
}

/* ------------------------------------------------------------------ */
/* Parsing and serialization                                           */
/* ------------------------------------------------------------------ */

/**
 * The identifier door, case-insensitively, and the ambiguity behind it.
 *
 * `static` is both a value and `from_slice`'s answer for "not one" — which is
 * worse than the display vocabulary's version of the same trap, because `static`
 * is the *initial* value and therefore the most likely thing a caller is asking
 * about. The predicate on the identifier is what keeps the two apart, so it is
 * asserted separately for a value and for every kind of non-value.
 */
static void test_position_parsing(void)
{
    static const char *const known[] = { "static",   "relative",  "absolute",
                                         "fixed",    "sticky",    "STATIC",
                                         "Relative", "ABSOLUTE",  "sTiCkY" };
    static const char *const unknown[] = { "",         "banana",    "statics",
                                           "stat",     "static-",   "static_",
                                           "run-in",   "running()", "centre",
                                           "position", "0",         "initial",
                                           "inherit",  "unset",     "revert",
                                           "sticky ",  " static" };
    size_t index;

    css_fixture();
    for (index = 0u; index < sizeof(known) / sizeof(known[0]); index++) {
        robo_css_slice_t slice = robo_css_slice_from_text(known[index]);
        char lower[32];
        size_t at;

        for (at = 0u; at < strlen(known[index]) && at + 1u < sizeof(lower); at++) {
            char c = known[index][at];

            lower[at] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        }
        lower[at] = '\0';
        ROBO_CHECK_EQ_INT(robo_css_positioning_slice_is_known(slice), 1);
        ROBO_CHECK_EQ_INT(
            robo_css_positioning_is_known(robo_css_positioning_from_slice(slice)), 1);
        /* Case-insensitivity is not an accident of the comparison: the *name*
         * comes back canonical however the identifier was written. */
        ROBO_CHECK_STR_EQ(robo_css_positioning_name(robo_css_positioning_from_slice(slice)),
                          lower);
    }

    for (index = 0u; index < sizeof(unknown) / sizeof(unknown[0]); index++) {
        robo_css_slice_t slice = robo_css_slice_from_text(unknown[index]);

        ROBO_CHECK_EQ_INT(robo_css_positioning_slice_is_known(slice), 0);
        /* And the value door answers STATIC, which is why a caller has to ask the
         * predicate — the result alone cannot distinguish `static` from `banana`. */
        ROBO_CHECK_EQ_INT(robo_css_positioning_from_slice(slice),
                          (int)ROBO_CSS_POSITIONING_STATIC);
    }
    /* A prefix is not a value and neither is the property name itself. */
    ROBO_CHECK_EQ_INT(robo_css_positioning_slice_is_known(robo_css_slice_from_text("fi")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_positioning_from_text(NULL),
                      (int)ROBO_CSS_POSITIONING_STATIC);
    ROBO_CHECK_EQ_INT(robo_css_positioning_slice_is_known(robo_css_slice_from_text("")), 0);
}

/** Serialization: one canonical keyword per value, and refusals that say why. */
static void test_position_serialization(void)
{
    char buffer[ROBO_CSS_POSITIONING_TEXT_MAX];
    size_t length = 0u;
    size_t index;

    css_fixture();
    for (index = 0u; index < (size_t)ROBO_CSS_POSITIONING_COUNT; index++) {
        robo_css_positioning_t position = (robo_css_positioning_t)index;

        ROBO_CHECK_EQ_INT(robo_css_positioning_write(position, buffer, sizeof(buffer),
                                                 &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, robo_css_positioning_name(position));
        ROBO_CHECK_EQ_INT((int)length, (int)strlen(robo_css_positioning_name(position)));
        /* The keyword round-trips, which is what makes a computed position
         * debuggable from its text. */
        ROBO_CHECK_EQ_INT(robo_css_positioning_from_text(buffer), (int)position);
    }
    /* A short buffer is a refusal, and it says how much room it needed rather
     * than truncating the keyword into a different one. */
    {
        char tiny[4];
        size_t needed = 0u;

        ROBO_CHECK_EQ_INT(robo_css_positioning_write(ROBO_CSS_POSITIONING_ABSOLUTE, tiny,
                                                  sizeof(tiny), &needed, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_STR_EQ(tiny, "");
        ROBO_CHECK_EQ_INT((int)needed, 8);
    }
    /* A value nothing defined is an argument error, not an empty write. */
    {
        char text[ROBO_CSS_POSITIONING_TEXT_MAX];
        size_t written = 5u;

        ROBO_CHECK_EQ_INT(robo_css_positioning_write((robo_css_positioning_t)99, text,
                                                  sizeof(text), &written, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_STR_EQ(text, "");
        ROBO_CHECK_EQ_INT((int)written, 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_positioning_write(ROBO_CSS_POSITIONING_STATIC, NULL,
                                              sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_positioning_write(ROBO_CSS_POSITIONING_STATIC, buffer,
                                              sizeof(buffer), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_positioning_write(ROBO_CSS_POSITIONING_STATIC, buffer, 0u,
                                              &length, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The shape                                                           */
/* ------------------------------------------------------------------ */

/**
 * A declaration through the real grammar, into a typed position value.
 *
 * The shape is asserted by *kind* as well as by text, because the point of the
 * phase is that a position is an enumerator and not the keyword's letters: a
 * value that printed `absolute` but was a KEYWORD would pass a text assertion and
 * hand a layout pass a string to compare.
 */
static void test_position_computed_shape(void)
{
    static const char *const keywords[] = { "static", "relative", "absolute", "fixed",
                                            "sticky" };
    size_t index;

    css_fixture();
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        char declaration[32];
        char expected[32];

        (void)snprintf(declaration, sizeof(declaration), "p: %s", keywords[index]);
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, declaration,
                           keywords[index]);
        /* Case-insensitively, and the *canonical* spelling is what comes out. */
        (void)snprintf(expected, sizeof(expected), "%s", keywords[index]);
        {
            char upper[32];
            size_t at;

            for (at = 0u; at < strlen(keywords[index]); at++) {
                char c = keywords[index][at];

                upper[at] = (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
            }
            upper[strlen(keywords[index])] = '\0';
            (void)snprintf(declaration, sizeof(declaration), "p: %s", upper);
            css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, declaration,
                               expected);
        }
    }
    /* Whitespace around the value and around the colon is the tokenizer's, and all
     * three spellings are the same declaration. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p:absolute", "absolute");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p:   sticky   ", "sticky");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "P: Relative\n", "relative");

    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_POSITION,
                                       "p: absolute"),
                      "position");
    /* Not the generic keyword shape, even though the text is one word: the two are
     * different representations of different things. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                                       "p: absolute"),
                      "keyword");

    /* The shape's own name is what a diagnostic prints for it. */
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_POSITION),
                      "position");
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          ROBO_CSS_PROPERTY_COMPUTATION_POSITION),
                      1);

    /*
     * The registry and the vocabulary agree about the property.
     *
     * Asserted rather than assumed, because the two are written in different files
     * and the failure would be quiet: a `position` row left on the generic KEYWORD
     * shape would accept `position: banana` again, and a row whose initial text
     * were not one of the vocabulary would make `position: initial` resolve
     * through a shape that refuses it.
     */
    {
        robo_css_property_registry_t *registry = NULL;
        const robo_css_property_definition_t *definition;

        ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                          ROBO_OK);
        definition = robo_css_property_registry_find(registry, "position");
        ROBO_CHECK(definition != NULL);
        if (definition) {
            ROBO_CHECK_EQ_INT(definition->computation,
                              (int)ROBO_CSS_PROPERTY_COMPUTATION_POSITION);
            ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
            ROBO_CHECK_STR_EQ(definition->initial_text, "static");
            ROBO_CHECK_EQ_INT(robo_css_positioning_from_text(definition->initial_text),
                              (int)ROBO_CSS_POSITIONING_STATIC);
            ROBO_CHECK_EQ_INT(robo_css_positioning_slice_is_known(
                                  robo_css_slice_from_text(definition->initial_text)),
                              1);
        }
        robo_css_property_registry_destroy(registry);
    }
}

/**
 * The three refusals, told apart.
 *
 * A closed vocabulary is only worth having if a value outside it is *refused*, and
 * the refusals are worth having in three shapes because they are three different
 * things to tell an author: an identifier this build has not added
 * (`position: running`), a value that is not an identifier at all (`20px`, `#fff`,
 * `10%`), and a declaration written with two keywords (`static relative`).
 * Collapsing them into one "invalid" would still be correct and would tell a
 * developer nothing.
 */
static void test_position_computed_invalid(void)
{
    css_fixture();

    /* An identifier, and not one of the five. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: banana",
                       "not a position value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: run-in",
                       "not a position value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: centre",
                       "not a position value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: sticky-",
                       "not a position value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: statics",
                       "not a position value");
    /* The spelling with an underscore, which is how a keyword reads in code and
     * not how CSS spells it. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: relative_",
                       "not a position value");

    /* Not an identifier at all. Four spellings, and each is a token the grammar
     * produced that this property has no use for. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: 20px",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: 10",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: #fff",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: 10%",
                       "not an identifier");
    /* A string is one token and is still not an identifier: `"static"` is seven
     * characters of which four are quotes. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: \"static\"",
                       "not an identifier");
    /* A function is a component *container* rather than a token, so it is refused
     * as not one component — which is a different sentence from "not an
     * identifier" and the right one for `rgb(0, 0, 0)`. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: rgb(0, 0, 0)",
                       "not a single position keyword");

    /* Two keywords is not a keyword, and the multi-keyword display syntax this
     * phase's sibling refuses is refused here for the same reason. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: static relative",
                       "not a single position keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: absolute fixed",
                       "not a single position keyword");
    /* Nothing at all: the declaration was not one component. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p:",
                       "not a single position keyword");


    /* And the shape is *asked*, not guessed at: a value that is not a position is
     * never a POSITION computed value, whatever kind it ends up as. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_POSITION,
                                       "p: banana"),
                      "invalid");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_POSITION,
                                       "p: static"),
                      "position");
}

/**
 * A reference is carried, not refused, and it is *flagged*.
 *
 * `position: var(--p)` is legal CSS whose value is decided where `--p` is, which
 * is a later stage's question and not a mistake here. The unresolved flag is what
 * makes it safe to carry: a consumer that read the tokens without it would be
 * reading a reference as if it were a position.
 */
static void test_position_computed_reference(void)
{
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    uint32_t unresolved = 0u;

    css_fixture();
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: var(--p)", text,
                     sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "var(--p)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_POSITION,
                                       "p: var(--p)"),
                      "preserved");

    /* A reference is as far as it goes: `env()` is the same shape of answer, and
     * an unknown function is a mistake rather than a reference. */
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: env(--p)", text,
                     sizeof(text), &unresolved);
    ROBO_CHECK_STR_EQ(text, "env(--p)");
    ROBO_CHECK_EQ_INT((int)unresolved, 1);
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_POSITION, "p: nonsense(--p)",
                       "not a single position keyword");
}

/* ------------------------------------------------------------------ */
/* The four insets, as one walk                                        */
/* ------------------------------------------------------------------ */

/**
 * `top`, `right`, `bottom` and `left` — all four, walked rather than typed out.
 *
 * The four properties are the four *sides*, and the side vocabulary is where their
 * names come from (RoboCSSBox.h): a suite that hand-listed `top`, `right`,
 * `bottom` and `left` would have no way to notice a registry row written against
 * the wrong side, which is exactly the kind of transposition the box model's own
 * suite exists to catch. So the walk is over `robo_css_box_side_count()` and the
 * names are read from `robo_css_box_side_name`.
 *
 * The shape is the box model's EDGE_OFFSET, and that is a decision worth asserting
 * rather than assuming: it is what makes `top: 25%` stay a percentage and
 * `top: auto` stay a keyword, and what makes `top: min-content` a mistake.
 */
static void test_position_inset_shape(void)
{
    size_t side;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BOX_SIDE_COUNT, 4);
    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        const char *name = robo_css_box_side_name((robo_css_box_side_t)side);
        char declaration[64];

        /* `auto` — the initial value, and not zero. */
        (void)snprintf(declaration, sizeof(declaration), "p: auto");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, declaration,
                           "auto");
        /* A bare zero is a length, and it is *not* `auto`: the difference is the
         * whole reason `top: 0` pins a box and `top: auto` does not. */
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: 0", "0px");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: 10px", "10px");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: -12px",
                           "-12px");
        /* A percentage stays a percentage: only layout knows what it is a
         * percentage of, and the containing block for an inset is not the one a
         * width would use. */
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: 25%", "25%");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: 50%", "50%");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: 12.5%",
                           "12.5%");
        /* A contextual length keeps its unit, and an absolute one converts. */
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: 2em", "2em");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: 1in", "96px");

        /* The insets are offsets, so `auto` is in the vocabulary and nothing else
         * a length-shaped property accepts is. */
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: min-content",
                           "not a length, a percentage or auto");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: normal",
                           "not a length, a percentage or auto");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: red",
                           "not a length, a percentage or auto");
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: thin",
                           "not a length, a percentage or auto");
        /* The name is the side's, and it is spelled the way CSS spells it. */
        ROBO_CHECK_STR_EQ(name, side == 0u   ? "top"
                                : side == 1u ? "right"
                                : side == 2u ? "bottom"
                                             : "left");
    }
}

/**
 * The four insets' registry rows: the shape, the initial value, and no inheritance.
 *
 * `auto` is an *initial value*, and it is not a placeholder for zero — turning it
 * into zero would place every absolutely positioned box against the top-left
 * corner of its containing block, which is the single most visible way to be wrong
 * about this family. The assertion that matters is therefore that the initial text
 * is `auto` *and* that it computes through the property's own shape to the keyword
 * rather than to a length.
 */
static void test_position_inset_initial(void)
{
    robo_css_property_registry_t *registry = NULL;
    size_t side;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                      ROBO_OK);
    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        const char *name = robo_css_box_side_name((robo_css_box_side_t)side);
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_find(registry, name);

        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        ROBO_CHECK_EQ_INT(definition->computation,
                          (int)ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET);
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
        ROBO_CHECK_STR_EQ(definition->initial_text, "auto");
        /* Through the property's own shape: `auto` is a *keyword* here, and a
         * shape that turned it into `0px` would be the defect this test exists
         * for. */
        css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, "p: auto",
                           "auto");
        {
            char declaration[32];

            (void)snprintf(declaration, sizeof(declaration), "p: %s",
                           definition->initial_text);
            css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET, declaration,
                               "auto");
        }
    }
    /* A fifth side does not exist, and the vocabulary says so rather than
     * answering with the first row. */
    ROBO_CHECK_EQ_INT(robo_css_box_side_is_known((robo_css_box_side_t)4), 0);
    /* The box vocabulary answers `unknown` where the display and positioning ones
     * answer the empty string. Asserted as it is rather than as one might wish:
     * this phase consumes the side vocabulary and does not get to restate it. */
    ROBO_CHECK_STR_EQ(robo_css_box_side_name((robo_css_box_side_t)4), "unknown");
    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* z-index                                                             */
/* ------------------------------------------------------------------ */

/**
 * `z-index`: `auto` or a whole number, and three refusals that stay three.
 *
 * `auto` and `0` are different answers and the shape has to hold both: `auto` says
 * the element takes no part in its parent's stacking decisions, `0` says it does,
 * at level zero. The refusals are the interesting half — `1.5` is not a whole
 * number, `1.0` is a whole number written in a spelling `<integer>` does not
 * accept, and `10px` is not a number at all — because a shape that accepted any
 * number would make all three computable and hand a later phase a value it can
 * only fail on.
 */
static void test_position_z_index_shape(void)
{
    css_fixture();

    /* `auto`, as a keyword rather than as zero. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: auto", "auto");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX,
                                       "z: auto"),
                      "keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: AUTO", "auto");

    /* Whole numbers, positive, zero and negative. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 0", "0");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 1", "1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 10", "10");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: -1", "-1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: -100", "-100");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: +7", "7");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z:   42  ", "42");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: -1"),
                      "number");

    /* Negative zero is zero, and it prints as zero: the module's rule, not a
     * coincidence of the shape. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: -0", "0");

    /* Three refusals, three sentences. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 1.5",
                       "not a whole number");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 0.5",
                       "not a whole number");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 1.0",
                       "a whole number must be written as an integer");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 1e2",
                       "a whole number must be written as an integer");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 10px",
                       "not a whole number or auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 10%",
                       "not a whole number or auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 1foo",
                       "not a whole number or auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: hello",
                       "not a whole number or auto");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: none",
                       "not a whole number or auto");
    /* A magnitude no runtime can hold as an integer is refused rather than
     * wrapped, and it is the unit module's bound that refuses it. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 1e999",
                       "not a finite number");
    /* Two components is not a number. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: 1 2",
                       "not a single z-index component");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z:",
                       "not a single z-index component");
    /* A reference is carried, exactly as it is for the two closed vocabularies. */
    {
        char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
        uint32_t unresolved = 0u;

        css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX, "z: var(--z)", text,
                         sizeof(text), &unresolved);
        ROBO_CHECK_STR_EQ(text, "var(--z)");
        ROBO_CHECK_EQ_INT((int)unresolved, 1);
    }

    /* The shape's own name, and the registry row that uses it. */
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX),
                      "z-index");
    {
        robo_css_property_registry_t *registry = NULL;
        const robo_css_property_definition_t *definition;

        ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                          ROBO_OK);
        definition = robo_css_property_registry_find(registry, "z-index");
        ROBO_CHECK(definition != NULL);
        if (definition) {
            ROBO_CHECK_EQ_INT(definition->computation,
                              (int)ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX);
            ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
            ROBO_CHECK_STR_EQ(definition->initial_text, "auto");
        }
        robo_css_property_registry_destroy(registry);
    }
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void css_position_compute_of(css_inherit_fixture_t *fixture,
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
 * The typed `position` of an element, and where it came from.
 *
 * The door a later phase reads, and it is the *context's* rather than the style's
 * for the reason 9.12 recorded: `position` does not inherit, so an element that
 * declared nothing has **no entry** — 9.7 materialises initial values for the
 * inherited-by-default set only — and "what is this element's position" is a
 * question only something holding the registry can answer.
 */
static robo_css_positioning_t css_position_of(css_inherit_fixture_t *fixture,
                                           const robo_css_computed_style_t *style,
                                           int *out_from_initial)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_positioning_t position;

    if (out_from_initial) {
        *out_from_initial = -1;
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          css_inherit_context(fixture), style,
                          robo_css_slice_from_text("position"), &value,
                          out_from_initial, NULL),
                      ROBO_OK);
    /* The answer is a position *value* and not the keyword's letters, which is
     * what the shape change buys a consumer. */
    ROBO_CHECK_EQ_INT(value.kind, (int)ROBO_CSS_COMPUTED_VALUE_POSITION);
    position = robo_css_computed_value_position(&value);
    robo_css_computed_value_dispose(&value);
    return position;
}

/**
 * The typed value through the two doors a consumer has, for all six properties.
 *
 * A declared element and an undeclared one are the two halves of the question,
 * and the undeclared one is the half that matters: an element that says nothing
 * has no entry for any of these six, and it still has an answer — the property's
 * own initial value, which the query door reports and marks as such. All six are
 * asked because a phase that resolved `position` and forgot the offsets would
 * show up here as `top` answering something other than `auto`.
 */
static void test_position_computed_style_query(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t placed;
    robo_css_element_ref_t plain;
    robo_css_positioning_t position;
    int from_initial = -1;
    size_t side;

    css_fixture();
    css_inherit_begin(&fixture);
    placed = css_host_add(css_inherit_host(&fixture), "div", "placed", NULL);
    plain = css_host_add(css_inherit_host(&fixture), "span", "plain", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#placed { position: absolute; top: 25%; right: auto;"
                      " bottom: 10px; left: 2em; z-index: -1 }"
                      "#plain { color: red }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* Declared: every one of the six arrives typed, and the two that carry an
     * enumerator and a whole number are read through their own accessors. */
    css_position_compute_of(&fixture, placed, style);
    position = css_position_of(&fixture, style, &from_initial);
    ROBO_CHECK_EQ_INT((int)position, (int)ROBO_CSS_POSITIONING_ABSOLUTE);
    ROBO_CHECK_EQ_INT(from_initial, 0);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(position), 1);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_out_of_flow(position), 1);
    css_computed_is(style, "position", "absolute");
    css_computed_flags_are(style, "position", "declared");
    css_computed_is(style, "top", "25%");
    css_computed_is(style, "right", "auto");
    css_computed_is(style, "bottom", "10px");
    css_computed_is(style, "left", "2em");
    css_computed_is(style, "z-index", "-1");
    /* The same fact through the record's own accessors, so the two doors cannot
     * disagree about what a typed value is. */
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(
                style, robo_css_slice_from_text("position"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_POSITION);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_position(value),
                          (int)ROBO_CSS_POSITIONING_ABSOLUTE);
    }
    /* A percentage inset is a PERCENTAGE and an `auto` one is a KEYWORD: the two
     * are different kinds, which is what stops a later phase reading `auto` as a
     * zero-length offset. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET,
                                       "p: 25%"),
                      "percentage");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET,
                                       "p: auto"),
                      "keyword");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX,
                                       "z: -1"),
                      "number");

    /* Nothing declared: no entry for any of the six, and every one of them still
     * answers — with the property's own initial value, marked as such. */
    css_position_compute_of(&fixture, plain, style);
    css_computed_is(style, "position", "<absent>");
    position = css_position_of(&fixture, style, &from_initial);
    ROBO_CHECK_EQ_INT((int)position, (int)ROBO_CSS_POSITIONING_STATIC);
    ROBO_CHECK_EQ_INT(from_initial, 1);
    /* Static is a value and not a failure: the box simply is not positioned, and
     * an offset on it does not apply. */
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_positioned(position), 0);
    ROBO_CHECK_EQ_INT(robo_css_positioning_is_in_flow(position), 1);
    ROBO_CHECK_EQ_INT(robo_css_positioning_slice_is_known(
                          robo_css_slice_from_text("static")),
                      1);
    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        const char *name = robo_css_box_side_name((robo_css_box_side_t)side);
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;
        int came_from_initial = -1;

        css_computed_is(style, name, "<absent>");
        ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                              css_inherit_context(&fixture), style,
                              robo_css_slice_from_text(name), &value,
                              &came_from_initial, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(came_from_initial, 1);
        /* A *keyword* and not a length: an `auto` inset that arrived as `0px`
         * would pin every box its author left to the containing block's corner. */
        ROBO_CHECK_EQ_INT(value.kind, (int)ROBO_CSS_COMPUTED_VALUE_KEYWORD);
        {
            char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
            size_t length = 0u;

            ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, text,
                                                           sizeof(text), &length, NULL),
                              ROBO_OK);
            ROBO_CHECK_STR_EQ(text, "auto");
        }
        robo_css_computed_value_dispose(&value);
    }
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;
        int came_from_initial = -1;

        ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                              css_inherit_context(&fixture), style,
                              robo_css_slice_from_text("z-index"), &value,
                              &came_from_initial, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(came_from_initial, 1);
        ROBO_CHECK_EQ_INT(value.kind, (int)ROBO_CSS_COMPUTED_VALUE_KEYWORD);
        robo_css_computed_value_dispose(&value);
    }
    /* While `color`, which does inherit, is present in the same walk — so the
     * test cannot pass by the element having no style at all. */
    css_computed_is(style, "color", "#ff0000");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * Non-inheritance, proved from both sides, for all six.
 *
 * An `absolute` parent with offsets and a stacking level, and a child whose author
 * never mentioned any of them: if any of the six propagated, the child would be
 * lifted out of flow, pinned to its parent's corners and given its parent's
 * stacking level — the most visible way to be wrong about this family, and the
 * reason none of the six is inherited. An inherited property is declared beside
 * them so the test cannot pass by the child having no style at all.
 */
static void test_position_non_inheritance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent_style = NULL;
    robo_css_computed_style_t *child_style = NULL;
    robo_css_element_ref_t parent;
    robo_css_element_ref_t child;
    size_t side;

    css_fixture();
    css_inherit_begin(&fixture);
    parent = css_host_add(css_inherit_host(&fixture), "div", "outer", NULL);
    child = css_host_add(css_inherit_host(&fixture), "p", "inner", NULL);
    css_host_set_parent(css_inherit_host(&fixture), child, parent);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#outer { position: absolute; top: 10%; right: 1px;"
                      " bottom: 2em; left: 3%; z-index: 5; color: #336699 }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child_style, NULL), ROBO_OK);
    css_position_compute_of(&fixture, parent, parent_style);
    css_position_compute_of(&fixture, child, child_style);

    css_computed_is(parent_style, "position", "absolute");
    css_computed_is(parent_style, "top", "10%");
    css_computed_is(parent_style, "z-index", "5");
    /* The child has no entry for any of the six — a non-inherited property the
     * element does not mention is not materialised (9.7) — and the query door then
     * answers each with the property's own initial value rather than the parent's. */
    css_computed_is(child_style, "position", "<absent>");
    ROBO_CHECK_EQ_INT(css_position_of(&fixture, child_style, NULL),
                      (int)ROBO_CSS_POSITIONING_STATIC);
    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        const char *name = robo_css_box_side_name((robo_css_box_side_t)side);

        css_computed_is(child_style, name, "<absent>");
    }
    css_computed_is(child_style, "z-index", "<absent>");
    /* While `color`, which does inherit, arrives from the parent in the same
     * walk. */
    css_computed_is(child_style, "color", "#336699");
    css_computed_flags_are(child_style, "color", "inherited");

    robo_css_computed_style_destroy(child_style);
    robo_css_computed_style_destroy(parent_style);
    css_inherit_end(&fixture);
}

/**
 * `inherit`, `initial` and `unset`, on properties that do not inherit.
 *
 * Three expectations, and each is CSS's own rule for a non-inherited property:
 * `inherit` takes the parent's *computed* value (which is how a child can be given
 * its parent's position at all), `initial` is the property's own initial value
 * rather than the parent's, and `unset` behaves as `initial` *because* the
 * property is not inherited — which is the assertion that would fail if any of the
 * six were misclassified. All six are asked on one element, so `top: inherit`
 * cannot be right by accident while `position: inherit` is wrong.
 */
static void test_position_wide_keywords(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent_style = NULL;
    robo_css_computed_style_t *inherit_style = NULL;
    robo_css_computed_style_t *initial_style = NULL;
    robo_css_computed_style_t *unset_style = NULL;
    robo_css_element_ref_t parent;
    robo_css_element_ref_t takes_inherit;
    robo_css_element_ref_t takes_initial;
    robo_css_element_ref_t takes_unset;
    size_t side;

    css_fixture();
    css_inherit_begin(&fixture);
    parent = css_host_add(css_inherit_host(&fixture), "div", "outer", NULL);
    takes_inherit = css_host_add(css_inherit_host(&fixture), "p", "inherit", NULL);
    takes_initial = css_host_add(css_inherit_host(&fixture), "p", "initial", NULL);
    takes_unset = css_host_add(css_inherit_host(&fixture), "p", "unset", NULL);
    css_host_set_parent(css_inherit_host(&fixture), takes_inherit, parent);
    css_host_set_parent(css_inherit_host(&fixture), takes_initial, parent);
    css_host_set_parent(css_inherit_host(&fixture), takes_unset, parent);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#outer { position: fixed; top: 12px; right: 1%;"
                      " bottom: 2em; left: 3px; z-index: 9 }"
                      "#inherit { position: inherit; top: inherit; right: inherit;"
                      " bottom: inherit; left: inherit; z-index: inherit }"
                      "#initial { position: initial; top: initial; right: initial;"
                      " bottom: initial; left: initial; z-index: initial }"
                      "#unset { position: unset; top: unset; right: unset;"
                      " bottom: unset; left: unset; z-index: unset }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&inherit_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&initial_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&unset_style, NULL), ROBO_OK);
    css_position_compute_of(&fixture, parent, parent_style);
    css_position_compute_of(&fixture, takes_inherit, inherit_style);
    css_position_compute_of(&fixture, takes_initial, initial_style);
    css_position_compute_of(&fixture, takes_unset, unset_style);

    css_computed_is(parent_style, "position", "fixed");
    /* `inherit`: the parent's computed value, on all six. */
    css_computed_is(inherit_style, "position", "fixed");
    css_computed_flags_are(inherit_style, "position", "inherited");
    css_computed_is(inherit_style, "top", "12px");
    css_computed_is(inherit_style, "right", "1%");
    css_computed_is(inherit_style, "bottom", "2em");
    css_computed_is(inherit_style, "left", "3px");
    css_computed_is(inherit_style, "z-index", "9");
    /* `initial`: the property's own initial value, which for an inset is `auto`
     * and not the parent's `12px`. */
    css_computed_is(initial_style, "position", "static");
    css_computed_flags_are(initial_style, "position", "default");
    for (side = 0u; side < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side++) {
        css_computed_is(initial_style,
                        robo_css_box_side_name((robo_css_box_side_t)side), "auto");
        css_computed_is(unset_style,
                        robo_css_box_side_name((robo_css_box_side_t)side), "auto");
    }
    css_computed_is(initial_style, "z-index", "auto");
    /* `unset` on a non-inherited property *is* `initial`, which is the assertion
     * that would fail if the property were classified as inherited. */
    css_computed_is(unset_style, "position", "static");
    css_computed_flags_are(unset_style, "position", "default");
    css_computed_is(unset_style, "z-index", "auto");
    /* And the typed door agrees with the text on all three, on the values that
     * came from the parent and from the registry rather than from a declaration. */
    ROBO_CHECK_EQ_INT(css_position_of(&fixture, inherit_style, NULL),
                      (int)ROBO_CSS_POSITIONING_FIXED);
    ROBO_CHECK_EQ_INT(css_position_of(&fixture, initial_style, NULL),
                      (int)ROBO_CSS_POSITIONING_STATIC);
    ROBO_CHECK_EQ_INT(css_position_of(&fixture, unset_style, NULL),
                      (int)ROBO_CSS_POSITIONING_STATIC);

    robo_css_computed_style_destroy(unset_style);
    robo_css_computed_style_destroy(initial_style);
    robo_css_computed_style_destroy(inherit_style);
    robo_css_computed_style_destroy(parent_style);
    css_inherit_end(&fixture);
}

/**
 * The cascade's three keys, and the inline attribute, on `position` and `top`.
 *
 * Not a re-test of the cascade: it is the statement that these properties
 * participate in it like any other, and that a losing declaration is *dropped*
 * rather than carried. An implementation that gave the positioning shapes their
 * own precedence would show up here as the wrong winner — and it is asserted
 * across the two shapes at once (`position` is an enumerator and `top` is an
 * offset), so a shape that reordered one and not the other would show too.
 */
static void test_position_cascade(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t panel;
    robo_css_element_ref_t important;
    robo_css_element_ref_t source_order;
    robo_css_element_ref_t inline_style;

    css_fixture();
    css_inherit_begin(&fixture);
    panel = css_host_add(css_inherit_host(&fixture), "div", "panel", "box");
    important = css_host_add(css_inherit_host(&fixture), "div", "important", NULL);
    source_order = css_host_add(css_inherit_host(&fixture), "div", "order", NULL);
    inline_style = css_host_add(css_inherit_host(&fixture), "div", "inline", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      /* An id selector outranks a type selector, whichever came
                       * first and whatever order the two are written in. */
                      "div { position: static; top: 1px }"
                      "#panel { position: relative; top: 10px }"
                      /* `!important` outranks a later normal declaration. */
                      "#important { position: fixed !important; top: 2px !important }"
                      "#important { position: absolute; top: 20px }"
                      /* Level ties are broken by document order. */
                      "#order { position: sticky; top: 3px }"
                      "#order { position: relative; top: 30px }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    css_position_compute_of(&fixture, panel, style);
    css_computed_is(style, "position", "relative");
    css_computed_is(style, "top", "10px");
    css_computed_flags_are(style, "position", "declared");

    css_position_compute_of(&fixture, important, style);
    css_computed_is(style, "position", "fixed");
    css_computed_is(style, "top", "2px");

    css_position_compute_of(&fixture, source_order, style);
    css_computed_is(style, "position", "relative");
    css_computed_is(style, "top", "30px");

    /* An inline `style` attribute is the strongest normal level, and it reaches
     * both shapes through the same grammar as a sheet's declaration. */
    {
        static css_inline_stub_t stub;

        css_inherit_inline(&fixture, &stub,
                           "position: absolute; top: auto; z-index: 3");
    }
    css_position_compute_of(&fixture, inline_style, style);
    css_computed_is(style, "position", "absolute");
    css_computed_is(style, "top", "auto");
    css_computed_is(style, "z-index", "3");
    css_computed_flags_are(style, "position", "declared");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A refused declaration is *reported*, not silently replaced.
 *
 * `position: banana` and `z-index: 1.5` reach the cascade, win it, and are then
 * found to mean nothing — and the entries stay, saying so. That is 9.8's contract
 * and this phase does not change it: an invalid value is an *answer* about the
 * value, `has_invalid` is true, and a consumer that wants to know whether the
 * declaration was used reads the flag rather than reading a `static` that might
 * have come from anywhere.
 *
 * The alternative — answering the initial value here — would be a guess in the one
 * place the pipeline deliberately does not guess, and it would be *wrong* in a way
 * nothing could see: with `#odd { position: banana }` and
 * `#odd { position: absolute }` in that order a browser discards the first and uses
 * the second, so the answer is `absolute`. Reporting the refusal lets a later phase
 * do that; inventing `static` would hide it.
 */
static void test_position_invalid_declaration(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t element;
    robo_css_computed_value_t queried = ROBO_CSS_CLEARED;
    int from_initial = -1;

    css_fixture();
    css_inherit_begin(&fixture);
    element = css_host_add(css_inherit_host(&fixture), "div", "odd", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#odd { position: banana; top: nope; z-index: 1.5 }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    css_position_compute_of(&fixture, element, style);

    /* Every entry is there, it is the declaration that won, and it says the value
     * means nothing for this property. */
    css_computed_is(style, "position", "not a position value");
    css_computed_flags_are(style, "position", "declared+invalid");
    css_computed_is(style, "top", "not a length, a percentage or auto");
    css_computed_flags_are(style, "top", "declared+invalid");
    css_computed_is(style, "z-index", "not a whole number");
    css_computed_flags_are(style, "z-index", "declared+invalid");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(style), 1);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_incomplete_count(style) >= 1u, 1);

    /* The query door hands the refusal on rather than laundering it into a
     * well-formed default, which is the whole point of reporting it: a caller can
     * see that the declaration was there and was wrong. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          css_inherit_context(&fixture), style,
                          robo_css_slice_from_text("position"), &queried,
                          &from_initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(from_initial, 0);
    ROBO_CHECK_EQ_INT(queried.kind, (int)ROBO_CSS_COMPUTED_VALUE_INVALID);
    robo_css_computed_value_dispose(&queried);

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * The three shapes against malformed input, with invariants on every case.
 *
 * Every string is either a token sequence the grammar can produce or one it
 * refuses; none may crash, leak, or produce a value outside the shape's own
 * contract. The strings are generated from a table of hostile shapes rather than
 * typed out one expectation at a time, and all three of this phase's shapes are
 * driven at once so a fault in the shared vocabulary body cannot hide behind the
 * other shape's table.
 */
static void test_position_robust(void)
{
    static const char *const values[] = {
        "p:", "p: ", "p: ;", "p: ...", "p: -", "p: --", "p: ---", "p: #",
        "p: #fff", "p: ##fff", "p: 1e999", "p: 1e999px", "p: -0", "p: +relative",
        "p: relative!", "p: relative absolute", "p: static static static",
        "p: var(--", "p: var()", "p: calc(", "p: calc(1px + 1px)", "p: rgb(",
        "p: url(", "p: \"unterminated", "p: {}", "p: []", "p: ()",
        "p:  \"absolute\"", "p: STICKY", "p: FiXeD", "p: \\73 ticky",
        "p: relative\\", "p: 9999999999999999999999", "p: -9999999999999999999999",
        "p: 0.0000000000001", "p: 1e-999", "p: NaN", "p: infinity",
        "p: 2147483648", "p: -2147483649", "p: 9007199254740993"
    };
    static const robo_css_property_computation_t shapes[] = {
        ROBO_CSS_PROPERTY_COMPUTATION_POSITION,
        ROBO_CSS_PROPERTY_COMPUTATION_EDGE_OFFSET,
        ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX
    };
    size_t index;
    size_t shape;

    css_fixture();
    for (shape = 0u; shape < sizeof(shapes) / sizeof(shapes[0]); shape++) {
        for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
            robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
            robo_css_error_t error = ROBO_CSS_CLEARED;
            size_t declaration_index;

            /* The grammar returns ROBO_OK whenever it produced a result,
             * recovering from what it could not use — so the assertions are about
             * that result rather than about the parse. */
            ROBO_CHECK_EQ_INT(css_parse_declarations(values[index], &list, &error),
                              ROBO_OK);
            for (declaration_index = 0u;
                 declaration_index < robo_css_declaration_list_count(&list);
                 declaration_index++) {
                const robo_css_declaration_t *declaration =
                    robo_css_declaration_list_at(&list, declaration_index);
                robo_css_computed_value_t computed = ROBO_CSS_CLEARED;
                char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
                size_t length = 0u;

                ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                      shapes[shape],
                                      robo_css_declaration_value(declaration),
                                      &computed, NULL),
                                  ROBO_OK);
                /* Whatever it decided, it is a decision: a known kind, and a
                 * rendering that fits. */
                ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(computed.kind),
                                  1);
                ROBO_CHECK_EQ_INT(robo_css_computed_value_write(
                                      &computed, text, sizeof(text), &length, NULL),
                                  ROBO_OK);
                /* A POSITION value is always one the vocabulary knows — a shape
                 * that could produce an enumerator nothing can classify would be
                 * a value a layout pass could only fail on. */
                if (computed.kind == ROBO_CSS_COMPUTED_VALUE_POSITION) {
                    ROBO_CHECK_EQ_INT(robo_css_positioning_is_known(computed.position), 1);
                    ROBO_CHECK_EQ_INT(robo_css_computed_value_position(&computed),
                                      (int)computed.position);
                }
                /* And a NUMBER produced by the z-index shape is always a whole
                 * number: `is_integer` is set for exactly that reason. */
                if (shapes[shape] == ROBO_CSS_PROPERTY_COMPUTATION_Z_INDEX &&
                    computed.kind == ROBO_CSS_COMPUTED_VALUE_NUMBER) {
                    ROBO_CHECK_EQ_INT(computed.is_integer, 1);
                }
                robo_css_computed_value_dispose(&computed);
            }
            robo_css_declaration_list_dispose(&list);
        }
    }

    /* The value door's own contract, and the accessor's behaviour on a record
     * that holds something else. */
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              (robo_css_property_computation_t)200, NULL, &value, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_position(
                              &value, (robo_css_positioning_t)99, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_position(NULL,
                                                               ROBO_CSS_POSITIONING_FIXED,
                                                               NULL),
                          ROBO_INVALID_ARGUMENT);
        /* The accessor on a record that holds something else answers STATIC rather
         * than reading an uninitialised field — and STATIC is safe *because* the
         * kind check is the caller's, which is why it is documented as such. */
        ROBO_CHECK_EQ_INT(robo_css_computed_value_position(&value),
                          (int)ROBO_CSS_POSITIONING_STATIC);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_position(NULL),
                          (int)ROBO_CSS_POSITIONING_STATIC);
        robo_css_computed_value_dispose(&value);
    }
}

/**
 * The same document computes the same values, in any order, twice.
 *
 * Determinism is a property of the whole pipeline and not of these shapes, but
 * `position` is what a later phase keys on and the offsets are what it reads
 * beside it, so it is checked here rather than assumed: the five values are walked
 * in both directions across two passes with all four insets and a `z-index` on
 * every element, so an iteration-order dependency anywhere in the resolution would
 * show as a disagreement between the two.
 */
static void test_position_determinism(void)
{
    static const char *const ids[] = { "a", "b", "c", "d", "e" };
    static const char *const keywords[] = { "static", "relative", "absolute", "fixed",
                                            "sticky" };
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t refs[5];
    size_t index;
    size_t pass;

    css_fixture();
    css_inherit_begin(&fixture);
    for (index = 0u; index < 5u; index++) {
        refs[index] = css_host_add(css_inherit_host(&fixture), "div", ids[index], NULL);
    }
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#a { position: static;   top: auto; left: auto; z-index: auto }"
                      "#b { position: relative; top: 1px;  left: 2px;  z-index: 1 }"
                      "#c { position: absolute; top: 25%;  left: 50%;  z-index: 0 }"
                      "#d { position: fixed;    top: -1px; left: 2em;  z-index: -1 }"
                      "#e { position: sticky;   top: 3%;   left: 4px;  z-index: 10 }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    for (pass = 0u; pass < 2u; pass++) {
        for (index = 0u; index < 5u; index++) {
            /* The second pass walks the same five backwards. */
            size_t at = (pass == 0u) ? index : (4u - index);

            css_position_compute_of(&fixture, refs[at], style);
            css_computed_is(style, "position", keywords[at]);
            ROBO_CHECK_EQ_INT(css_position_of(&fixture, style, NULL),
                              (int)at);
            /* The offsets and the level are read on every element in both passes,
             * which is what makes this a statement about the family and not only
             * about the enumerator. */
            css_computed_is(style, "top", at == 0u   ? "auto"
                                          : at == 1u ? "1px"
                                          : at == 2u ? "25%"
                                          : at == 3u ? "-1px"
                                                     : "3%");
            css_computed_is(style, "z-index", at == 0u   ? "auto"
                                               : at == 1u ? "1"
                                               : at == 2u ? "0"
                                               : at == 3u ? "-1"
                                                          : "10");
        }
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_position(void);

void robo_test_register_css_position(void)
{
    robo_test_register("css.position.vocabulary", test_position_vocabulary);
    robo_test_register("css.position.flow", test_position_flow);
    robo_test_register("css.position.positioned", test_position_positioned);
    robo_test_register("css.position.classification", test_position_classification);
    robo_test_register("css.position.parsing", test_position_parsing);
    robo_test_register("css.position.serialization", test_position_serialization);
    robo_test_register("css.position.computed.shape", test_position_computed_shape);
    robo_test_register("css.position.computed.invalid", test_position_computed_invalid);
    robo_test_register("css.position.computed.reference",
                       test_position_computed_reference);
    robo_test_register("css.position.inset.shape", test_position_inset_shape);
    robo_test_register("css.position.inset.initial", test_position_inset_initial);
    robo_test_register("css.position.z_index.shape", test_position_z_index_shape);
    robo_test_register("css.position.computed.style_query",
                       test_position_computed_style_query);
    robo_test_register("css.position.non_inheritance", test_position_non_inheritance);
    robo_test_register("css.position.keywords", test_position_wide_keywords);
    robo_test_register("css.position.cascade", test_position_cascade);
    robo_test_register("css.position.invalid_declaration",
                       test_position_invalid_declaration);
    robo_test_register("css.position.robust", test_position_robust);
    robo_test_register("css.position.determinism", test_position_determinism);
}
