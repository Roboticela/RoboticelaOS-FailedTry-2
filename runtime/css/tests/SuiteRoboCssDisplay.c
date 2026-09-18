/*
 * RoboCSS display suite (Phase 9.12) — the vocabulary, the outer/inner table,
 * the classifications, and what the pipeline makes of a declaration.
 *
 * Four groups, in the order the module is built:
 *
 *   the vocabulary  the four values walked rather than a representative one
 *                   typed out, with the outer and inner halves read from the
 *                   same rows
 *   classification  what a layout pass will ask — is there a box, is it
 *                   block-level, is it atomic — asserted for every value and
 *                   cross-checked so two values cannot both answer the same way
 *   the shape       declaration text through the real tokenizer, so `none`,
 *                   `block`, `inline` and `inline-block` are *computed values*
 *                   and `bogus`, `20px` and `inline block` are three different
 *                   mistakes rather than one
 *   integration     a real element: the initial value, `inherit`, `initial`,
 *                   `unset`, non-inheritance proven from both sides, the
 *                   cascade's three keys, and the accessor a layout pass reads
 *
 * Nothing here asserts a box. A display value at this phase is a *value* with
 * three facts in it, and every expectation below is about the value, what it
 * classifies as, or where it came from — which is the whole difference between a
 * computed display and a laid-out one.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * The four values, and what each is spelled as.
 *
 * The spelling is the assertion, not the enumeration: a table whose rows and
 * enumerators had drifted would still answer `is_known` for all four and would
 * only show up as a value that printed as another one's keyword. Walking the set
 * in enumerator order and reading every name back is what makes that visible.
 */
static void test_display_vocabulary(void)
{
    static const char *const keywords[] = { "none", "block", "inline", "inline-block" };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_DISPLAY_COUNT, 4);
    ROBO_CHECK_EQ_INT((int)robo_css_display_count(), 4);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        robo_css_display_t display = (robo_css_display_t)index;

        ROBO_CHECK_EQ_INT(robo_css_display_is_known(display), 1);
        ROBO_CHECK_STR_EQ(robo_css_display_name(display), keywords[index]);
        ROBO_CHECK_EQ_INT(robo_css_display_from_text(keywords[index]), (int)display);
    }
    /* The enumerator past the end and a value nothing defined answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    ROBO_CHECK_EQ_INT(robo_css_display_is_known(
                          (robo_css_display_t)ROBO_CSS_DISPLAY_COUNT),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_known((robo_css_display_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_css_display_name((robo_css_display_t)99), "");
}

/**
 * The outer and inner halves, read from the same rows as the keywords.
 *
 * `inline-block` is the row that earns the two columns: it is inline-level *and*
 * atomic, which is two facts, and a model that carried only "is it inline" would
 * have to keep a flag beside it that could drift. `none`'s inner is NONE because
 * a value that generates no box has no contents to lay out — not because it is
 * unwritten.
 */
static void test_display_outer_inner(void)
{
    static const struct {
        robo_css_display_t display;
        robo_css_display_outer_t outer;
        robo_css_display_inner_t inner;
        const char *outer_name;
        const char *inner_name;
    } expected[] = {
        {ROBO_CSS_DISPLAY_NONE, ROBO_CSS_DISPLAY_OUTER_NONE, ROBO_CSS_DISPLAY_INNER_NONE,
         "none", "none"},
        {ROBO_CSS_DISPLAY_BLOCK, ROBO_CSS_DISPLAY_OUTER_BLOCK, ROBO_CSS_DISPLAY_INNER_FLOW,
         "block", "flow"},
        {ROBO_CSS_DISPLAY_INLINE, ROBO_CSS_DISPLAY_OUTER_INLINE,
         ROBO_CSS_DISPLAY_INNER_FLOW, "inline", "flow"},
        {ROBO_CSS_DISPLAY_INLINE_BLOCK, ROBO_CSS_DISPLAY_OUTER_INLINE,
         ROBO_CSS_DISPLAY_INNER_FLOW_ROOT, "inline", "flow-root"}
    };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_DISPLAY_OUTER_COUNT, 3);
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_DISPLAY_INNER_COUNT, 3);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_css_display_outer(expected[index].display),
                          (int)expected[index].outer);
        ROBO_CHECK_EQ_INT(robo_css_display_inner(expected[index].display),
                          (int)expected[index].inner);
        ROBO_CHECK_STR_EQ(robo_css_display_outer_name(expected[index].outer),
                          expected[index].outer_name);
        ROBO_CHECK_STR_EQ(robo_css_display_inner_name(expected[index].inner),
                          expected[index].inner_name);
    }
    /* An unknown enumerator is not a display value, so it has no halves — which
     * is the answer `OUTER_NONE` gives and the reason it is not a failure. */
    ROBO_CHECK_EQ_INT(robo_css_display_outer((robo_css_display_t)99),
                      (int)ROBO_CSS_DISPLAY_OUTER_NONE);
    ROBO_CHECK_EQ_INT(robo_css_display_inner((robo_css_display_t)99),
                      (int)ROBO_CSS_DISPLAY_INNER_NONE);
    ROBO_CHECK_STR_EQ(robo_css_display_outer_name((robo_css_display_outer_t)99),
                      "unknown");
    ROBO_CHECK_STR_EQ(robo_css_display_inner_name((robo_css_display_inner_t)99),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * What a layout pass asks, for every value, cross-checked.
 *
 * The cross-checks are the half that matters. "Block-level and inline-level are
 * mutually exclusive", "exactly one value generates no box", "exactly one value
 * is atomic" and "an atomic value is inline-level" are the invariants that keep
 * the table honest as rows are added — a fifth row that broke one of them would
 * be a row whose classification contradicts the two it shares with its siblings.
 */
static void test_display_classification(void)
{
    static const struct {
        robo_css_display_t display;
        int generates_box;
        int is_none;
        int block_level;
        int inline_level;
        int inline_block;
    } expected[] = {
        {ROBO_CSS_DISPLAY_NONE, 0, 1, 0, 0, 0},
        {ROBO_CSS_DISPLAY_BLOCK, 1, 0, 1, 0, 0},
        {ROBO_CSS_DISPLAY_INLINE, 1, 0, 0, 1, 0},
        {ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 0, 0, 1, 1}
    };
    size_t index;
    int boxless = 0;
    int atomic = 0;

    css_fixture();
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        robo_css_display_t display = expected[index].display;

        ROBO_CHECK_EQ_INT(robo_css_display_generates_box(display),
                          expected[index].generates_box);
        ROBO_CHECK_EQ_INT(robo_css_display_is_none(display), expected[index].is_none);
        ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(display),
                          expected[index].block_level);
        ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(display),
                          expected[index].inline_level);
        ROBO_CHECK_EQ_INT(robo_css_display_is_inline_block(display),
                          expected[index].inline_block);

        /* The invariants, asserted per value rather than once over the table. */
        ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(display) +
                              robo_css_display_is_inline_level(display),
                          expected[index].generates_box);
        if (robo_css_display_is_inline_block(display)) {
            ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(display), 1);
        }
        if (!robo_css_display_generates_box(display)) {
            boxless++;
            /* No box means no contents, so there is nothing to lay out. */
            ROBO_CHECK_EQ_INT(robo_css_display_inner(display),
                              (int)ROBO_CSS_DISPLAY_INNER_NONE);
        }
        if (robo_css_display_is_inline_block(display)) {
            atomic++;
        }
    }
    /* Exactly one value generates no box, and exactly one is atomic. A row added
     * without one of those facts being reconsidered moves these counts and fails
     * here rather than in a layout pass. */
    ROBO_CHECK_EQ_INT(boxless, 1);
    ROBO_CHECK_EQ_INT(atomic, 1);
    /* An unknown enumerator answers "no" to every question rather than the first
     * row's answers. */
    ROBO_CHECK_EQ_INT(robo_css_display_generates_box((robo_css_display_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_none((robo_css_display_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_block_level((robo_css_display_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level((robo_css_display_t)99), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_block((robo_css_display_t)99), 0);
}

/* ------------------------------------------------------------------ */
/* Parsing and serialization                                           */
/* ------------------------------------------------------------------ */

/**
 * Reading an identifier, and the one ambiguity the interface has.
 *
 * `none` is a display value *and* what an unrecognised identifier gets back, so
 * a caller cannot tell the two apart from the result alone. `slice_is_known` is
 * the door that does tell them apart, and this is where that is pinned: if it
 * ever answered 1 for `bogus`, `display: bogus` would compute to `display: none`
 * and every element an author mistyped would vanish.
 */
static void test_display_parsing(void)
{
    static const struct {
        const char *text;
        robo_css_display_t display;
    } known[] = {
        {"none", ROBO_CSS_DISPLAY_NONE},
        {"block", ROBO_CSS_DISPLAY_BLOCK},
        {"inline", ROBO_CSS_DISPLAY_INLINE},
        {"inline-block", ROBO_CSS_DISPLAY_INLINE_BLOCK},
        /* CSS spells keywords case-insensitively. */
        {"BLOCK", ROBO_CSS_DISPLAY_BLOCK},
        {"Inline", ROBO_CSS_DISPLAY_INLINE},
        {"INLINE-BLOCK", ROBO_CSS_DISPLAY_INLINE_BLOCK},
        {"None", ROBO_CSS_DISPLAY_NONE}
    };
    static const char *const unknown[] = {
        "bogus", "flex", "grid", "table", "contents", "inline-flex", "inline_block",
        "block;", "inlineblock", "blockflow", ""
    };
    size_t index;
    char text[64];

    css_fixture();
    for (index = 0u; index < sizeof(known) / sizeof(known[0]); index++) {
        robo_css_slice_t slice = robo_css_slice_from_text(known[index].text);

        ROBO_CHECK_EQ_INT(robo_css_display_slice_is_known(slice), 1);
        ROBO_CHECK_EQ_INT(robo_css_display_from_slice(slice), (int)known[index].display);
        ROBO_CHECK_EQ_INT(robo_css_display_from_text(known[index].text),
                          (int)known[index].display);
    }
    for (index = 0u; index < sizeof(unknown) / sizeof(unknown[0]); index++) {
        robo_css_slice_t slice = robo_css_slice_from_text(unknown[index]);

        /* Not one of them — and the two answers are asked together, because the
         * whole point is that the first cannot say this on its own. */
        ROBO_CHECK_EQ_INT(robo_css_display_slice_is_known(slice), 0);
        ROBO_CHECK_EQ_INT(robo_css_display_from_slice(slice),
                          (int)ROBO_CSS_DISPLAY_NONE);
    }
    /* A NULL text and an absent slice are not identifiers at all. */
    ROBO_CHECK_EQ_INT(robo_css_display_from_text(NULL), (int)ROBO_CSS_DISPLAY_NONE);
    ROBO_CHECK_EQ_INT(robo_css_display_slice_is_known(robo_css_slice_empty()), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_slice_is_known((robo_css_slice_t){NULL, 0u}), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_from_slice(robo_css_slice_empty()),
                      (int)ROBO_CSS_DISPLAY_NONE);

    /* Round trip: every value prints as the keyword that reads back as it. */
    for (index = 0u; index < (size_t)ROBO_CSS_DISPLAY_COUNT; index++) {
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(robo_css_display_write((robo_css_display_t)index, text,
                                                 sizeof(text), &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)length, (int)strlen(text));
        ROBO_CHECK_EQ_INT(robo_css_display_from_text(text), (int)index);
    }
}

/**
 * One canonical spelling per value, and a refusal rather than a truncation.
 *
 * `inline-block` is the longest keyword and the one that decides whether a small
 * buffer is a bug: nine bytes plus a NUL means a ten-byte buffer works and a
 * nine-byte one is refused *with the length it needed*, so a caller can size
 * from the refusal instead of from a constant it has to keep in step.
 */
static void test_display_serialization(void)
{
    char buffer[ROBO_CSS_DISPLAY_TEXT_MAX];
    size_t length = 0u;
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_display_write(ROBO_CSS_DISPLAY_INLINE_BLOCK, buffer,
                                             sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "inline-block");
    ROBO_CHECK_EQ_INT((int)length, 12);
    ROBO_CHECK_EQ_INT(robo_css_display_write(ROBO_CSS_DISPLAY_NONE, buffer,
                                             sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "none");

    /* Too small, and the buffer is left empty rather than half-filled. */
    buffer[0] = 'x';
    buffer[1] = '\0';
    ROBO_CHECK_EQ_INT(robo_css_display_write(ROBO_CSS_DISPLAY_INLINE_BLOCK, buffer, 4u,
                                             &length, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_STR_EQ(buffer, "");
    ROBO_CHECK_EQ_INT((int)length, 12);
    /* Exactly big enough is enough: twelve bytes plus the NUL. */
    ROBO_CHECK_EQ_INT(robo_css_display_write(ROBO_CSS_DISPLAY_INLINE_BLOCK, buffer,
                                             13u, &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "inline-block");

    /* An enumerator nothing defined has no spelling, and NULL arguments are
     * refused rather than written through. */
    ROBO_CHECK_EQ_INT(robo_css_display_write((robo_css_display_t)99, buffer,
                                             sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_display_write(ROBO_CSS_DISPLAY_BLOCK, NULL,
                                             sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_display_write(ROBO_CSS_DISPLAY_BLOCK, buffer,
                                             sizeof(buffer), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_display_write(ROBO_CSS_DISPLAY_BLOCK, buffer, 0u,
                                             &length, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* And every row survives the round trip through a buffer sized by the
     * constant the header hands out, which is what that constant is for. */
    for (index = 0u; index < (size_t)ROBO_CSS_DISPLAY_COUNT; index++) {
        ROBO_CHECK_EQ_INT(robo_css_display_write((robo_css_display_t)index, buffer,
                                                 sizeof(buffer), &length, NULL),
                          ROBO_OK);
        ROBO_CHECK(length < ROBO_CSS_DISPLAY_TEXT_MAX);
    }
}

/* ------------------------------------------------------------------ */
/* The value shape                                                     */
/* ------------------------------------------------------------------ */

/**
 * A declaration through the real grammar, into a typed display value.
 *
 * The shape is asserted by *kind* as well as by text, because the point of the
 * phase is that a display is an enumerator and not the keyword's letters: a value
 * that printed `block` but was a KEYWORD would pass a text assertion and hand a
 * layout pass a string to compare.
 */
static void test_display_computed_shape(void)
{
    css_fixture();

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: none", "none");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: block", "block");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: inline", "inline");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: inline-block",
                       "inline-block");
    /* Whitespace around the value and around the colon is the tokenizer's, and
     * all three spellings are the same declaration. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d:block", "block");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d:   block   ", "block");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: INLINE-BLOCK\n",
                       "inline-block");

    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,
                                       "d: block"),
                      "display");
    /* Not the generic keyword shape, even though the text is one word: the two
     * are different representations of different things. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,
                                       "d: inline"),
                      "display");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD,
                                       "d: inline"),
                      "keyword");

    /* The shape's own name is what a diagnostic prints for it. */
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY),
                      "display");
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          (robo_css_property_computation_t)
                              ROBO_CSS_PROPERTY_COMPUTATION_COUNT),
                      0);

    /*
     * The registry and the vocabulary agree about the property.
     *
     * Asserted rather than assumed, because the two are written in different
     * files and the failure would be quiet: a `display` row left on the generic
     * KEYWORD shape would accept `display: bogus` again, and a row whose initial
     * text were not one of the vocabulary would make `display: initial` resolve
     * through a shape that refuses it. The initial value is checked as *text the
     * vocabulary knows* rather than as a string, which is the property that
     * matters.
     */
    {
        robo_css_property_registry_t *registry = NULL;
        const robo_css_property_definition_t *definition;

        ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                          ROBO_OK);
        definition = robo_css_property_registry_find(registry, "display");
        ROBO_CHECK(definition != NULL);
        if (definition) {
            ROBO_CHECK_EQ_INT(definition->computation,
                              (int)ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY);
            ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
            ROBO_CHECK_STR_EQ(definition->initial_text, "inline");
            ROBO_CHECK_EQ_INT(robo_css_display_from_text(definition->initial_text),
                              (int)ROBO_CSS_DISPLAY_INLINE);
            ROBO_CHECK_EQ_INT(robo_css_display_slice_is_known(
                                  robo_css_slice_from_text(definition->initial_text)),
                              1);
        }
        robo_css_property_registry_destroy(registry);
    }
}

/**
 * The four mistakes, told apart.
 *
 * A closed vocabulary is only worth having if a value outside it is *refused*,
 * and the refusals are worth having in four shapes because they are four
 * different things to tell an author: an identifier this build has not added
 * (`flex`), a value that is not an identifier at all (`20px`, `#fff`, `10%`), a
 * declaration written with two keywords (`inline block`), and a value whose
 * meaning is a reference (`rgb(0, 0, 0)` is a colour function, not a display).
 * Collapsing them into one "invalid" would still be correct and would tell a
 * developer nothing.
 */
static void test_display_computed_invalid(void)
{
    css_fixture();

    /* An identifier, and not one of the four. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: bogus",
                       "not a display value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: flex",
                       "not a display value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: grid",
                       "not a display value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: table",
                       "not a display value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: contents",
                       "not a display value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: inline-flex",
                       "not a display value");
    /* The spelling with an underscore, which is how a keyword reads in code and
     * not how CSS spells it. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: inline_block",
                       "not a display value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: red",
                       "not a display value");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,
                                       "d: bogus"),
                      "invalid");

    /* Not an identifier: a dimension, a number, a percentage, a hash, a string,
     * a URL. The tokenizer classifies each of them, and none is a display. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: 20px",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: 10",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: 10%",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: #fff",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: \"block\"",
                       "not an identifier");
    /* A URL token is one token and is still not an identifier. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: url(block)",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: rgb(0, 0, 0)",
                       "not a single display keyword");

    /* Two keywords: legal CSS 3 that this build does not parse, and it says so
     * rather than picking one. `inline block` is `inline-block` to a browser and
     * is refused here, which is the limitation the phase documents. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: inline block",
                       "not a single display keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: inline flow-root",
                       "not a single display keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: block flow",
                       "not a single display keyword");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,
                                       "d: inline block"),
                      "invalid");

    /* A declaration with no value at all recovers as a value with no components,
     * which is not an identifier either. It is refused rather than read as the
     * property's initial value: `display: ;` is not a declaration an author
     * meant, and the cascade drops it, so the element is left with no declaration
     * and gets its initial value by *that* route rather than by this one. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d:",
                       "not a single display keyword");
}

/**
 * A reference is carried, not refused.
 *
 * `display: var(--d)` is legal CSS whose value is decided where the variable is,
 * which is a later stage's question. Carrying it with the unresolved flag set is
 * the same answer the colour shape gives a colour space this build cannot
 * represent, and the flag is the half that matters: a consumer that read the
 * tokens as a display value would be reading a reference as a value.
 */
static void test_display_computed_reference(void)
{
    uint32_t flags = 0u;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

    css_fixture();

    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: var(--d)", text,
                     sizeof(text), &flags);
    ROBO_CHECK_STR_EQ(text, "var(--d)");
    ROBO_CHECK_EQ_INT((int)flags, 1);
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,
                                       "d: var(--d)"),
                      "preserved");

    flags = 0u;
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: var(--d, block)", text,
                     sizeof(text), &flags);
    ROBO_CHECK_STR_EQ(text, "var(--d, block)");
    ROBO_CHECK_EQ_INT((int)flags, 1);

    flags = 0u;
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: env(display)", text,
                     sizeof(text), &flags);
    ROBO_CHECK_STR_EQ(text, "env(display)");
    ROBO_CHECK_EQ_INT((int)flags, 1);

    /* A resolved keyword is *not* flagged unresolved, so the flag is a fact about
     * this value rather than a constant of the shape. */
    flags = 0u;
    css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY, "d: block", text,
                     sizeof(text), &flags);
    ROBO_CHECK_STR_EQ(text, "block");
    ROBO_CHECK_EQ_INT((int)flags, 0);
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void css_display_compute_of(css_inherit_fixture_t *fixture,
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
 * What the pipeline says this element's `display` is, and where that came from.
 *
 * The door the phase hands a layout pass, and it is the *context's* rather than
 * the style's for a reason the container's shape makes unavoidable: `display` is
 * non-inherited, so an element that declared nothing has **no entry** in a
 * computed style — 9.7 materialises initial values for the inherited-by-default
 * set only. "What is this element's display" still has an answer, and it is the
 * property's own initial value, which is a fact only something holding the
 * registry can state. This door holds it.
 */
static robo_css_display_t css_display_of(css_inherit_fixture_t *fixture,
                                         const robo_css_computed_style_t *style,
                                         int *out_from_initial)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_display_t display;

    if (out_from_initial) {
        *out_from_initial = -1;
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          css_inherit_context(fixture), style,
                          robo_css_slice_from_text("display"), &value,
                          out_from_initial, NULL),
                      ROBO_OK);
    /* The answer is a display *value* and not the keyword's letters, which is
     * what the phase's shape change buys a consumer. */
    ROBO_CHECK_EQ_INT(value.kind, (int)ROBO_CSS_COMPUTED_VALUE_DISPLAY);
    display = robo_css_computed_value_display(&value);
    robo_css_computed_value_dispose(&value);
    return display;
}

/**
 * The typed value, through the two doors a consumer has.
 *
 * A phase that already holds a computed value reads it through the value
 * record's accessor; a phase asking about an element reads the query door, which
 * also answers when the element declared nothing. Both are asked here for the
 * same elements, so they cannot disagree about what a display is.
 *
 * The declared element and the undeclared one are the two halves of the question,
 * and the undeclared one is the half that matters: "the entry exists" and "the
 * entry is absent, so the initial value applies" are different facts, and the
 * second is the common case on any real document.
 */
static void test_display_computed_style_query(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t shown;
    robo_css_element_ref_t plain;
    robo_css_display_t display;
    int from_initial = -1;

    css_fixture();
    css_inherit_begin(&fixture);
    shown = css_host_add(css_inherit_host(&fixture), "div", "shown", NULL);
    plain = css_host_add(css_inherit_host(&fixture), "span", "plain", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#shown { display: inline-block } #plain { color: red }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_display_compute_of(&fixture, shown, style);
    display = css_display_of(&fixture, style, &from_initial);
    ROBO_CHECK_EQ_INT((int)display, (int)ROBO_CSS_DISPLAY_INLINE_BLOCK);
    ROBO_CHECK_EQ_INT(from_initial, 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_block(display), 1);
    ROBO_CHECK_EQ_INT(robo_css_display_generates_box(display), 1);
    /* The same fact by name, as text, and through the record's own accessor. */
    css_computed_is(style, "display", "inline-block");
    {
        const robo_css_computed_value_t *value =
            robo_css_computed_style_computed_by_name(style,
                                                    robo_css_slice_from_text("display"));

        ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_DISPLAY);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_display(value),
                          (int)ROBO_CSS_DISPLAY_INLINE_BLOCK);
    }

    /* An element that declared nothing has no *entry* — a non-inherited property
     * the element does not mention is not materialised (9.7) — and its display is
     * still the property's initial value `inline`, which the query door answers
     * and marks as coming from the initial value. */
    css_display_compute_of(&fixture, plain, style);
    css_computed_is(style, "display", "<absent>");
    display = css_display_of(&fixture, style, &from_initial);
    ROBO_CHECK_EQ_INT((int)display, (int)ROBO_CSS_DISPLAY_INLINE);
    ROBO_CHECK_EQ_INT(from_initial, 1);
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(display), 1);

    /* A NULL style is "nothing was computed for this element", and the answer is
     * still the initial value rather than a failure. */
    display = css_display_of(&fixture, NULL, &from_initial);
    ROBO_CHECK_EQ_INT((int)display, (int)ROBO_CSS_DISPLAY_INLINE);
    ROBO_CHECK_EQ_INT(from_initial, 1);

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * `display: none` is a decided value and not a missing one.
 *
 * The assertion that matters is that the accessor answers **1** with NONE. A door
 * that reported `none` as "no value" would make "this element generates no box"
 * indistinguishable from "nobody computed this", and a layout pass that conflated
 * them would either drop every element it had not styled or lay out one it should
 * have removed. It is also not zero geometry, not a hidden visibility and not an
 * invalid value — each of which is asserted by the fact that this is a valid
 * computed value with its own keyword.
 */
static void test_display_none_is_decided(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t hidden;
    robo_css_display_t display;
    const robo_css_computed_value_t *value;

    css_fixture();
    css_inherit_begin(&fixture);
    hidden = css_host_add(css_inherit_host(&fixture), "div", "hidden", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#hidden { display: none; width: 100px; height: 100px }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    css_display_compute_of(&fixture, hidden, style);

    display = css_display_of(&fixture, style, NULL);
    ROBO_CHECK_EQ_INT((int)display, (int)ROBO_CSS_DISPLAY_NONE);
    ROBO_CHECK_EQ_INT(robo_css_display_is_none(display), 1);
    ROBO_CHECK_EQ_INT(robo_css_display_generates_box(display), 0);
    css_computed_is(style, "display", "none");
    css_computed_flags_are(style, "display", "declared");
    /* Not an invalid value: the kind is the display kind and no flag says the
     * entry is bad. A `none` that were recorded as a mistake would be dropped by
     * a consumer that skips invalid entries, which is the same as not hiding. */
    value = robo_css_computed_style_computed_by_name(style,
                                                    robo_css_slice_from_text("display"));
    ROBO_CHECK_EQ_INT(value->kind, (int)ROBO_CSS_COMPUTED_VALUE_DISPLAY);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(style), 0);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_flag(
                          style, robo_css_slice_from_text("display"),
                          ROBO_CSS_COMPUTED_FLAG_INVALID),
                      0);
    /* The other properties of the same element are untouched by it: hiding is not
     * a geometry, and `width` is still the length an author wrote. */
    css_computed_is(style, "width", "100px");
    css_computed_is(style, "height", "100px");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * Non-inheritance, proved from both sides.
 *
 * The parent's box is removed and the child's is not, so a build that inherited
 * `display` would hide a child whose author never mentioned it — the single most
 * visible way to be wrong about this property. An inherited property is declared
 * beside it so the test cannot pass by the child having *no* style at all.
 */
static void test_display_non_inheritance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent_style = NULL;
    robo_css_computed_style_t *child_style = NULL;
    robo_css_element_ref_t parent;
    robo_css_element_ref_t child;

    css_fixture();
    css_inherit_begin(&fixture);
    parent = css_host_add(css_inherit_host(&fixture), "div", "outer", NULL);
    child = css_host_add(css_inherit_host(&fixture), "p", "inner", NULL);
    css_host_set_parent(css_inherit_host(&fixture), child, parent);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#outer { display: block; color: #336699 }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child_style, NULL), ROBO_OK);
    css_display_compute_of(&fixture, parent, parent_style);
    css_display_compute_of(&fixture, child, child_style);

    css_computed_is(parent_style, "display", "block");
    /* The child declared nothing, and there is no entry for a non-inherited
     * property — the parent's box was not handed down, and the query door then
     * answers with the property's initial value rather than the parent's. */
    css_computed_is(child_style, "display", "<absent>");
    ROBO_CHECK_EQ_INT(css_display_of(&fixture, child_style, NULL),
                      (int)ROBO_CSS_DISPLAY_INLINE);
    /* While `color`, which does inherit, arrives from the parent in the same
     * walk — so the test cannot pass by the child having no style at all. */
    css_computed_is(child_style, "color", "#336699");
    css_computed_flags_are(child_style, "color", "inherited");

    robo_css_computed_style_destroy(child_style);
    robo_css_computed_style_destroy(parent_style);
    css_inherit_end(&fixture);
}

/**
 * `inherit`, `initial` and `unset`, on a property that does not inherit.
 *
 * Three expectations and each is the one CSS's own rule gives for a
 * non-inherited property: `inherit` takes the parent's *computed* value (which is
 * how a child can be given its parent's display at all), `initial` is the
 * property's own initial value rather than the parent's, and `unset` behaves as
 * `initial` *because* the property is not inherited — which is the assertion that
 * would fail if `display` were misclassified as inherited.
 */
static void test_display_wide_keywords(void)
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
                      "#outer { display: block }"
                      "#inherit { display: inherit }"
                      "#initial { display: initial }"
                      "#unset { display: unset }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&inherit_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&initial_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&unset_style, NULL), ROBO_OK);
    css_display_compute_of(&fixture, parent, parent_style);
    css_display_compute_of(&fixture, takes_inherit, inherit_style);
    css_display_compute_of(&fixture, takes_initial, initial_style);
    css_display_compute_of(&fixture, takes_unset, unset_style);

    css_computed_is(parent_style, "display", "block");
    css_computed_is(inherit_style, "display", "block");
    css_computed_flags_are(inherit_style, "display", "inherited");
    css_computed_is(initial_style, "display", "inline");
    css_computed_flags_are(initial_style, "display", "default");
    css_computed_is(unset_style, "display", "inline");
    css_computed_flags_are(unset_style, "display", "default");
    /* And the typed door agrees with the text, on the value that came from the
     * parent rather than from a declaration. */
    ROBO_CHECK_EQ_INT(css_display_of(&fixture, inherit_style, NULL),
                      (int)ROBO_CSS_DISPLAY_BLOCK);
    ROBO_CHECK_EQ_INT(css_display_of(&fixture, initial_style, NULL),
                      (int)ROBO_CSS_DISPLAY_INLINE);
    ROBO_CHECK_EQ_INT(css_display_of(&fixture, unset_style, NULL),
                      (int)ROBO_CSS_DISPLAY_INLINE);

    robo_css_computed_style_destroy(unset_style);
    robo_css_computed_style_destroy(initial_style);
    robo_css_computed_style_destroy(inherit_style);
    robo_css_computed_style_destroy(parent_style);
    css_inherit_end(&fixture);
}

/**
 * The cascade's three keys, and the inline attribute, on `display`.
 *
 * Not a re-test of the cascade: it is the statement that `display` participates
 * in it like any other property, and that a losing declaration is *dropped*
 * rather than being carried as an unresolved entry. An implementation that gave
 * the display shape its own precedence would show up here as the wrong winner.
 */
static void test_display_cascade(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t panel;
    robo_css_element_ref_t important;
    robo_css_element_ref_t inline_style;

    css_fixture();
    css_inherit_begin(&fixture);
    panel = css_host_add(css_inherit_host(&fixture), "div", "panel", "box");
    important = css_host_add(css_inherit_host(&fixture), "div", "important", NULL);
    inline_style = css_host_add(css_inherit_host(&fixture), "div", "inline", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      /* An id selector outranks a type selector, whichever came
                       * first and whatever order the two are written in. */
                      "div { display: block }"
                      "#panel { display: inline-block }"
                      /* `!important` outranks a later normal declaration. */
                      "#important { display: none !important }"
                      "#important { display: block }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    css_display_compute_of(&fixture, panel, style);
    css_computed_is(style, "display", "inline-block");
    css_computed_flags_are(style, "display", "declared");

    css_display_compute_of(&fixture, important, style);
    css_computed_is(style, "display", "none");

    /* An inline `style` attribute is the strongest normal level, and it reaches
     * the display shape through the same grammar as a sheet's declaration. */
    {
        static css_inline_stub_t stub;

        css_inherit_inline(&fixture, &stub, "display: inline-block");
    }
    css_display_compute_of(&fixture, inline_style, style);
    css_computed_is(style, "display", "inline-block");
    css_computed_flags_are(style, "display", "declared");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A refused declaration is *reported*, not silently replaced.
 *
 * `display: bogus` reaches the cascade, wins it, and is then found to mean
 * nothing for the property — and the entry stays, saying so. That is 9.8's
 * contract and this phase does not change it: an invalid value is an *answer*
 * about the value, the style's `has_invalid` is true, and a consumer that wants
 * to know whether the declaration was used reads the flag rather than reading an
 * `inline` that might have come from anywhere.
 *
 * The alternative — answering `inline` here — would be a guess in the one place
 * the pipeline deliberately does not guess, and it would also be *wrong* in a way
 * nothing could see: with `#odd { display: bogus }` and `#odd { display: block }`
 * in that order, a browser discards the first and uses the second, so the answer
 * is `block`. Reporting the refusal lets a later phase do that; inventing `inline`
 * would hide it.
 */
static void test_display_invalid_declaration(void)
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
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "#odd { display: bogus }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    css_display_compute_of(&fixture, element, style);

    /* The entry is there, it is the declaration that won, and it says the value
     * means nothing for this property. */
    css_computed_is(style, "display", "not a display value");
    css_computed_flags_are(style, "display", "declared+invalid");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_has_invalid(style), 1);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_incomplete_count(style) >= 1u, 1);

    /* The query door hands that refusal on rather than laundering it into a
     * display: the value it returns is INVALID, and `from_initial` is 0 because
     * the answer came from the style and not from the property's default. */
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          css_inherit_context(&fixture), style,
                          robo_css_slice_from_text("display"), &queried,
                          &from_initial, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(queried.kind, (int)ROBO_CSS_COMPUTED_VALUE_INVALID);
    ROBO_CHECK_EQ_INT(from_initial, 0);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_display(&queried),
                      (int)ROBO_CSS_DISPLAY_NONE);
    robo_css_computed_value_dispose(&queried);

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * Malformed input, at volume.
 *
 * Every string is either a token sequence the grammar can produce or one it
 * refuses; none may crash, leak, or produce a kind outside the closed set. The
 * strings are generated from a table of hostile shapes rather than typed out, and
 * the display shape is added to the list the 9.8 sweep already drives so that this
 * phase's shape is exercised by the same fuzz as every other.
 */
static void test_display_robust(void)
{
    static const char *const values[] = {
        "d:", "d: ", "d: ;", "d: ...", "d: -", "d: --", "d: ---", "d: #", "d: #fff",
        "d: ##fff", "d: 1e999", "d: 1e999px", "d: -0", "d: +block", "d: block!",
        "d: block block", "d: none none", "d: inline block inline", "d: var(--",
        "d: var()", "d: calc(", "d: calc(1px + 1px)", "d: rgb(", "d: url(",
        "d: \"unterminated", "d: {}", "d: []", "d: ()", "d:  \"block\"", "d: NONE",
        "d: iNlInE", "d: \\62 lock", "d: block\\", "d: 9999999999999999999999px"
    };
    size_t index;

    css_fixture();
    for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        size_t declaration_index;

        /* The grammar returns ROBO_OK whenever it produced a result, recovering
         * from what it could not use — so the assertions are about that result
         * rather than about the parse. */
        ROBO_CHECK_EQ_INT(css_parse_declarations(values[index], &list, &error), ROBO_OK);
        for (declaration_index = 0u;
             declaration_index < robo_css_declaration_list_count(&list);
             declaration_index++) {
            const robo_css_declaration_t *declaration =
                robo_css_declaration_list_at(&list, declaration_index);
            robo_css_computed_value_t computed = ROBO_CSS_CLEARED;
            char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
            size_t length = 0u;

            ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                                  ROBO_CSS_PROPERTY_COMPUTATION_DISPLAY,
                                  robo_css_declaration_value(declaration), &computed,
                                  NULL),
                              ROBO_OK);
            /* Whatever it decided, it is a decision: a known kind, and a
             * rendering that fits. */
            ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(computed.kind), 1);
            ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&computed, text,
                                                           sizeof(text), &length, NULL),
                              ROBO_OK);
            /* A DISPLAY value is always one of the four, and the module agrees
             * it is one — a shape that could produce an enumerator the
             * vocabulary does not know would be a value nothing can classify. */
            if (computed.kind == ROBO_CSS_COMPUTED_VALUE_DISPLAY) {
                ROBO_CHECK_EQ_INT(robo_css_display_is_known(computed.display), 1);
                ROBO_CHECK_EQ_INT(robo_css_computed_value_display(&computed),
                                  (int)computed.display);
            }
            robo_css_computed_value_dispose(&computed);
        }
        robo_css_declaration_list_dispose(&list);
    }
    /* A value that is none of the kinds, asked for as a display, is refused with
     * the answer rather than a failure. */
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              (robo_css_property_computation_t)99, NULL, &value, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(
                              &value, (robo_css_display_t)99, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(NULL,
                                                              ROBO_CSS_DISPLAY_BLOCK,
                                                              NULL),
                          ROBO_INVALID_ARGUMENT);
        /* The accessor on a record that holds something else answers NONE rather
         * than reading an uninitialised field. */
        ROBO_CHECK_EQ_INT(robo_css_computed_value_display(&value),
                          (int)ROBO_CSS_DISPLAY_NONE);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_display(NULL),
                          (int)ROBO_CSS_DISPLAY_NONE);
        robo_css_computed_value_dispose(&value);
    }
}

/**
 * The same document computes the same displays, in any order, twice.
 *
 * Determinism is a property of the whole pipeline and not of this shape, but the
 * display is what a layout pass keys on, so it is checked here rather than
 * assumed: the four values are walked in both directions across two passes, so an
 * iteration-order dependency anywhere in the resolution would show as a
 * disagreement between the two.
 */
static void test_display_determinism(void)
{
    static const char *const ids[] = { "a", "b", "c", "d" };
    static const char *const keywords[] = { "none", "block", "inline", "inline-block" };
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t refs[4];
    size_t index;
    size_t pass;

    css_fixture();
    css_inherit_begin(&fixture);
    for (index = 0u; index < 4u; index++) {
        refs[index] = css_host_add(css_inherit_host(&fixture), "div", ids[index], NULL);
    }
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#a { display: none }"
                      "#b { display: block }"
                      "#c { display: inline }"
                      "#d { display: inline-block }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    for (pass = 0u; pass < 2u; pass++) {
        for (index = 0u; index < 4u; index++) {
            /* The second pass walks the same four backwards. */
            size_t at = (pass == 0u) ? index : (3u - index);

            css_display_compute_of(&fixture, refs[at], style);
            css_computed_is(style, "display", keywords[at]);
        }
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_display(void);

void robo_test_register_css_display(void)
{
    robo_test_register("css.display.vocabulary", test_display_vocabulary);
    robo_test_register("css.display.outer_inner", test_display_outer_inner);
    robo_test_register("css.display.classification", test_display_classification);
    robo_test_register("css.display.parsing", test_display_parsing);
    robo_test_register("css.display.serialization", test_display_serialization);
    robo_test_register("css.display.computed.shape", test_display_computed_shape);
    robo_test_register("css.display.computed.invalid", test_display_computed_invalid);
    robo_test_register("css.display.computed.reference", test_display_computed_reference);
    robo_test_register("css.display.computed.style_query",
                       test_display_computed_style_query);
    robo_test_register("css.display.none", test_display_none_is_decided);
    robo_test_register("css.display.non_inheritance", test_display_non_inheritance);
    robo_test_register("css.display.keywords", test_display_wide_keywords);
    robo_test_register("css.display.cascade", test_display_cascade);
    robo_test_register("css.display.invalid_declaration", test_display_invalid_declaration);
    robo_test_register("css.display.robust", test_display_robust);
    robo_test_register("css.display.determinism", test_display_determinism);
}
