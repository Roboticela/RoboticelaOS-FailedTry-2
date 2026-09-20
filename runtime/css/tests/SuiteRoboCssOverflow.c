/*
 * RoboCSS overflow suite (Phase 10.13) — the vocabulary, its classifications, what
 * a declaration computes to, and the registry rows the family is registered by.
 *
 * Four groups, in the order the module is built:
 *
 *   the vocabulary  the two values walked rather than a representative one typed
 *                   out, with the keyword each is spelled by read back from the
 *                   same row
 *   classification  what a layout pass will ask — does this axis clip, is this the
 *                   initial value — asserted for every value and cross-checked so
 *                   the two cannot disagree
 *   the shape       declaration text through the real tokenizer, so `visible` and
 *                   `hidden` are *computed values* and `banana`, `scroll`, `20px`
 *                   and `hidden visible` are four different answers rather than one
 *   the registry    the three rows — the shorthand and its two longhands — with the
 *                   shape, the initial value and the non-inheritance the family is
 *                   registered by, read from the standard registry rather than
 *                   restated from the table that writes them
 *
 * Nothing here asserts a box. An overflow value at this phase is a *value* with one
 * fact in it — whether its axis has a boundary — and every expectation below is about
 * the value, what it classifies as, or where it came from. Where the boundary *is*
 * belongs to the phase with a box, which is the whole difference between a computed
 * overflow and a clipped one.
 */

#include "RoboCssTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * The two values, and what each is spelled as.
 *
 * The spelling is the assertion, not the enumeration: a table whose rows and
 * enumerators had drifted would still answer `is_known` for both and would only show
 * up as a value that printed as the other one's keyword. Walking the set in
 * enumerator order and reading every name back is what makes that visible.
 */
static void test_overflow_vocabulary(void)
{
    static const char *const keywords[] = { "visible", "hidden" };
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_OVERFLOW_COUNT, 2);
    ROBO_CHECK_EQ_INT((int)robo_css_overflow_count(), 2);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        robo_css_overflow_t overflow = (robo_css_overflow_t)index;

        ROBO_CHECK_EQ_INT(robo_css_overflow_is_known(overflow), 1);
        ROBO_CHECK_STR_EQ(robo_css_overflow_name(overflow), keywords[index]);
        ROBO_CHECK_EQ_INT(robo_css_overflow_from_text(keywords[index]), (int)overflow);
    }
    /* The enumerator past the end, and the value nothing defined, answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    ROBO_CHECK_EQ_INT(robo_css_overflow_is_known(
                          (robo_css_overflow_t)ROBO_CSS_OVERFLOW_COUNT),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_is_known((robo_css_overflow_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_css_overflow_name((robo_css_overflow_t)99), "");
    ROBO_CHECK_EQ_INT(robo_css_overflow_clips((robo_css_overflow_t)99), 0);
}

/**
 * `visible` is both a value and the answer for "not one", so the *identifier* is what
 * a caller has to ask.
 *
 * This is the one ambiguity the vocabulary has, and it is the reason there are two
 * doors rather than one: `robo_css_overflow_from_slice` answers VISIBLE for text it
 * does not recognise — which is correct, because VISIBLE is the initial value — and
 * `robo_css_overflow_slice_is_known` is what tells an author that `overflow: scroll`
 * was refused rather than obeyed. `scroll` is the interesting row here: it is legal
 * CSS this build has not added, and it has to be a *reported* absence rather than a
 * silent `visible`.
 */
static void test_overflow_parsing(void)
{
    css_fixture();

    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("visible")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("hidden")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("HIDDEN")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("Hidden")),
                      1);
    /* Prefixes, near-misses and CSS's other overflow values. */
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("hidd")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("hiddenly")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("vis")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("scroll")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("auto")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("clip")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_from_text("")), 0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(robo_css_slice_empty()), 0);

    /* And the ambiguity, asserted from both sides. */
    ROBO_CHECK_EQ_INT(robo_css_overflow_from_slice(robo_css_slice_from_text("HIDDEN")),
                      ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT(robo_css_overflow_from_slice(robo_css_slice_from_text("scroll")),
                      ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_css_overflow_from_slice(robo_css_slice_from_text("banana")),
                      ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_css_overflow_from_slice(robo_css_slice_from_text("")),
                      ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_css_overflow_from_text(NULL), ROBO_CSS_OVERFLOW_VISIBLE);
}

/** One stable spelling per value, and a buffer too small is a refusal. */
static void test_overflow_serialization(void)
{
    char buffer[ROBO_CSS_OVERFLOW_TEXT_MAX];
    char tiny[4];
    size_t length = 0u;

    css_fixture();

    ROBO_CHECK_EQ_INT(robo_css_overflow_write(ROBO_CSS_OVERFLOW_VISIBLE, buffer,
                                              sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "visible");
    ROBO_CHECK_EQ_INT((int)length, 7);
    ROBO_CHECK_EQ_INT(robo_css_overflow_write(ROBO_CSS_OVERFLOW_HIDDEN, buffer,
                                              sizeof(buffer), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "hidden");
    ROBO_CHECK_EQ_INT((int)length, 6);

    /* A refused write leaves the buffer empty and reports the length it needed, so a
     * caller can size a buffer from the refusal instead of from a constant. */
    length = 0u;
    ROBO_CHECK_EQ_INT(robo_css_overflow_write(ROBO_CSS_OVERFLOW_VISIBLE, tiny,
                                              sizeof(tiny), &length, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_STR_EQ(tiny, "");
    ROBO_CHECK_EQ_INT((int)length, 7);
    ROBO_CHECK_EQ_INT(robo_css_overflow_write((robo_css_overflow_t)99, buffer,
                                              sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_overflow_write(ROBO_CSS_OVERFLOW_VISIBLE, NULL,
                                              sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * The one fact a consumer dispatches on, and the two values are its two answers.
 *
 * Asserted for every value rather than for the interesting one, because the
 * classification is a *table* and a caller must be able to read it without knowing
 * which enumerator means what: `clips` is what a layout pass asks, and a future
 * `clip` row will answer it here without any caller changing.
 */
static void test_overflow_classification(void)
{
    css_fixture();

    ROBO_CHECK_EQ_INT(robo_css_overflow_clips(ROBO_CSS_OVERFLOW_VISIBLE), 0);
    ROBO_CHECK_EQ_INT(robo_css_overflow_clips(ROBO_CSS_OVERFLOW_HIDDEN), 1);

    ROBO_CHECK_EQ_INT(robo_css_overflow_is_visible(ROBO_CSS_OVERFLOW_VISIBLE), 1);
    ROBO_CHECK_EQ_INT(robo_css_overflow_is_visible(ROBO_CSS_OVERFLOW_HIDDEN), 0);

    /* The two classifications partition the set: no value both clips and is the
     * initial state, and none is neither. */
    ROBO_CHECK_EQ_INT(robo_css_overflow_clips(ROBO_CSS_OVERFLOW_VISIBLE) +
                          robo_css_overflow_is_visible(ROBO_CSS_OVERFLOW_VISIBLE),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_overflow_clips(ROBO_CSS_OVERFLOW_HIDDEN) +
                          robo_css_overflow_is_visible(ROBO_CSS_OVERFLOW_HIDDEN),
                      1);
}

/* ------------------------------------------------------------------ */
/* The shape                                                           */
/* ------------------------------------------------------------------ */

/**
 * The shape is asserted by *kind* as well as by text, because the point of the phase
 * is that an overflow is an enumerator and not the keyword's letters: a value that
 * printed `hidden` but was a KEYWORD would pass a text assertion and hand a layout
 * pass a string to compare.
 */
static void test_overflow_computed_shape(void)
{
    css_fixture();

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: visible", "visible");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: hidden", "hidden");
    /* Whitespace and case are the tokenizer's and the vocabulary's, so all four
     * spellings are one value. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o:hidden", "hidden");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o:   hidden   ", "hidden");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: HIDDEN\n", "hidden");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: Visible", "visible");

    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: hidden"),
                      "overflow");
    /* Not the generic keyword shape, even though the text is one word: the two are
     * different representations of different things, and only one of them refuses an
     * identifier it does not know. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_KEYWORD, "o: hidden"),
                      "keyword");

    /* The shape's own name is what a diagnostic prints for it. */
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW),
                      "overflow");
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          (robo_css_property_computation_t)
                              ROBO_CSS_PROPERTY_COMPUTATION_COUNT),
                      0);
}

/**
 * The four refusals, and they are four different mistakes.
 *
 * An identifier outside the vocabulary is the author's word; `scroll` is legal CSS
 * this build has not added and reads the same way deliberately, because the honest
 * report for both is "not an overflow value" and the alternative — guessing at
 * scrolling — is a behaviour this build does not have. A length is not an identifier
 * at all, and two identifiers are the shorthand's two-value form, which this
 * vocabulary does not read.
 */
static void test_overflow_computed_invalid(void)
{
    css_fixture();

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: banana",
                       "not an overflow value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: scroll",
                       "not an overflow value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: auto",
                       "not an overflow value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: 20px",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW, "o: hidden visible",
                       "not a single overflow keyword");
    /* A reference is *carried* rather than refused, for the reason every closed
     * vocabulary in this build carries one: `overflow: var(--o)` is a declaration
     * whose value is decided where `--o` is, which is a later stage's question. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW,
                                       "o: var(--o)"),
                      "preserved");
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

/**
 * The three rows, read from the standard registry rather than restated from the table
 * that writes them.
 *
 * Three rows and not one, which is the family's shape: `overflow` is the shorthand and
 * `overflow-x`/`overflow-y` are the longhands every consumer reads, and all three take
 * the same vocabulary so that a document written either way reaches the same value.
 * The shorthand carrying the *shape* is the one unusual fact here — nothing in this
 * build expands a shorthand — and it is asserted rather than left implicit, because
 * the day expansion arrives the row is what will change.
 */
static void test_overflow_registry(void)
{
    static const char *const names[] = { "overflow", "overflow-x", "overflow-y" };
    robo_css_property_registry_t *registry = NULL;
    size_t index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        const robo_css_property_definition_t *definition =
            robo_css_property_registry_find(registry, names[index]);

        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        ROBO_CHECK_EQ_INT(definition->computation,
                          (int)ROBO_CSS_PROPERTY_COMPUTATION_OVERFLOW);
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
        ROBO_CHECK_STR_EQ(definition->initial_text, "visible");
        /* The initial text is checked as text the *vocabulary* knows rather than as a
         * string, which is the property that matters: a row whose initial were
         * `hidden` would make every box that declares nothing clip. */
        ROBO_CHECK_EQ_INT(robo_css_overflow_slice_is_known(
                              robo_css_slice_from_text(definition->initial_text)),
                          1);
        ROBO_CHECK_EQ_INT(robo_css_overflow_from_text(definition->initial_text),
                          ROBO_CSS_OVERFLOW_VISIBLE);
    }
    /* The shorthand is flagged as what it is, and the two longhands are not. */
    {
        const robo_css_property_definition_t *shorthand =
            robo_css_property_registry_find(registry, "overflow");

        ROBO_CHECK(shorthand != NULL);
        if (shorthand) {
            ROBO_CHECK_EQ_INT(robo_css_property_flags_shorthand(shorthand->flags), 1);
        }
    }
    ROBO_CHECK(robo_css_property_registry_find(registry, "overflow-nonsense") == NULL);
    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_css_overflow(void);

void robo_test_register_css_overflow(void)
{
    robo_test_register("css.overflow.vocabulary", test_overflow_vocabulary);
    robo_test_register("css.overflow.parsing", test_overflow_parsing);
    robo_test_register("css.overflow.serialization", test_overflow_serialization);
    robo_test_register("css.overflow.classification", test_overflow_classification);
    robo_test_register("css.overflow.computed.shape", test_overflow_computed_shape);
    robo_test_register("css.overflow.computed.invalid", test_overflow_computed_invalid);
    robo_test_register("css.overflow.registry", test_overflow_registry);
}
