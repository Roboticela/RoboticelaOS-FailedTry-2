/*
 * RoboCSS opacity suite (Phase 11.9) — the α-value range, the clamp that makes an
 * out-of-range declaration meaningful, the text, what a declaration computes to, and the
 * registry row the family is registered by.
 *
 * Four groups, in the order the module is built:
 *
 *   the value        the range rule, the clamp, the percentage conversion and the two
 *                    classifiers, asserted on values rather than on declarations
 *   serialization    the pinned fixed-point spelling, and the two refusals -- a value
 *                    that is not one, and a buffer that is too small
 *   the shape        declaration text through the real tokenizer, so `0.5`, `50%`, `2`
 *                    and `-1` are four *computed values* rather than four spellings
 *   the registry     the one row, read from the standard registry rather than restated
 *                    from the table that writes it
 *
 * Nothing here asserts a box or a pixel. What an element's opacity *does* -- multiplying
 * along a path, reaching a surface, being multiplied by a child's -- belongs to the paint
 * layer that accumulates it, and the whole reason this module exists separately is that
 * those are different questions from "what is a legal opacity and what does an
 * out-of-range one become".
 *
 * Two expectations below are the ones worth reading twice, because they are the two ways
 * this property can fail silently: `opacity: 2` computes to **`1`** rather than to an
 * invalid value (CSS clamps, and refusing would make a document that every browser renders
 * correctly render here as if the declaration were absent), and a value that is not an
 * opacity at all reads back as **`1`** rather than as `0` (the answer that renders the way
 * "nothing was declared" does, and a build that got it backwards would draw no document at
 * all without reporting anything).
 */

#include "RoboCssTestSupport.h"

/* The property's own module, which nothing above this line reaches: a computed value
 * carries an opacity as a `double`, so unlike the closed vocabularies this family's header
 * is not pulled in by RoboCSSComputedValue.h. */
#include "RoboCSS/RoboCSSOpacity.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The value                                                           */
/* ------------------------------------------------------------------ */

/**
 * The range, its two ends, and the value nothing declared gets.
 *
 * The initial value is asserted first and as a *door* rather than as a literal: it is the
 * one answer in this module that a build can get exactly backwards without anything else
 * looking wrong, so it is checked as the door every other reader goes through.
 */
static void test_opacity_value(void)
{
    css_fixture();

    ROBO_CHECK(robo_css_opacity_initial() == 1.0);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_opaque(robo_css_opacity_initial()), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_transparent(robo_css_opacity_initial()), 0);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(robo_css_opacity_initial()), 1);

    /* Both ends are values, and they are the two that matter: fully opaque is the state
     * in which the property changes nothing, fully transparent the state in which nothing
     * draws. */
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(0.0), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_opaque(0.0), 0);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_transparent(0.0), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(1.0), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_opaque(1.0), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_transparent(1.0), 0);

    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(0.5), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(0.000001), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(0.999999), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_opaque(0.5), 0);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_transparent(0.5), 0);

    /* Out of range, on both sides, and the smallest amount out of it. */
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(-0.000001), 0);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(1.000001), 0);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(-1.0), 0);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(2.0), 0);

    /* Negative zero is zero, and it is a value rather than an out-of-range number: the
     * comparison is numerical, so the sign bit is not a fact about the opacity. */
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(-0.0), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_transparent(-0.0), 1);

    /* And the values that are not numbers at all. Every comparison against a NaN is false,
     * which is why the validity rule separates it rather than relying on the range test. */
    {
        volatile double zero = 0.0;
        double nan_value = zero / zero;
        double infinite = 1.0 / zero;

        ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(nan_value), 0);
        ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(infinite), 0);
        ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(-infinite), 0);
        ROBO_CHECK_EQ_INT(robo_css_opacity_is_transparent(nan_value), 0);
        ROBO_CHECK_EQ_INT(robo_css_opacity_is_opaque(nan_value), 0);
    }
}

/**
 * The clamp: the one rule that makes `opacity: 2` a declaration rather than a mistake.
 *
 * Both directions are asserted, and the *non-finite* case is the one that is a decision
 * rather than a specification: CSS says nothing about a NaN because an author cannot write
 * one, so the answer here is the module's own and it is deliberately the **initial** value
 * -- a record nothing can read must render the way an element nothing declared does, and
 * that way is visible. A NaN answered with zero would turn a caller's arithmetic bug into a
 * blank page with nothing to report.
 */
static void test_opacity_clamp(void)
{
    volatile double zero = 0.0;
    double nan_value = zero / zero;

    css_fixture();

    /* In range: unchanged, so the door is safe to apply to a value a caller has not
     * questioned and safe to apply twice. */
    ROBO_CHECK(robo_css_opacity_clamp(0.0) == 0.0);
    ROBO_CHECK(robo_css_opacity_clamp(0.25) == 0.25);
    ROBO_CHECK(robo_css_opacity_clamp(0.5) == 0.5);
    ROBO_CHECK(robo_css_opacity_clamp(1.0) == 1.0);
    ROBO_CHECK(robo_css_opacity_clamp(robo_css_opacity_clamp(0.5)) == 0.5);

    /* Above one, below zero, and the infinities -- which need no test of their own because
     * each is outside the range on its own side. */
    ROBO_CHECK(robo_css_opacity_clamp(1.5) == 1.0);
    ROBO_CHECK(robo_css_opacity_clamp(2.0) == 1.0);
    ROBO_CHECK(robo_css_opacity_clamp(100.0) == 1.0);
    ROBO_CHECK(robo_css_opacity_clamp(1.0 / zero) == 1.0);
    ROBO_CHECK(robo_css_opacity_clamp(-0.5) == 0.0);
    ROBO_CHECK(robo_css_opacity_clamp(-100.0) == 0.0);
    ROBO_CHECK(robo_css_opacity_clamp(-1.0 / zero) == 0.0);

    /* Not a number: the initial value, and the direction is the whole point of the case. */
    ROBO_CHECK(robo_css_opacity_clamp(nan_value) == robo_css_opacity_initial());

    /* Every clamped result is a value, which is what makes it safe to hand one to the
     * make-door without asking again. */
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(robo_css_opacity_clamp(2.0)), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(robo_css_opacity_clamp(-2.0)), 1);
    ROBO_CHECK_EQ_INT(robo_css_opacity_is_valid(robo_css_opacity_clamp(nan_value)), 1);
}

/** The percentage spelling: the same value, divided by a hundred, and not clamped here. */
static void test_opacity_percentage(void)
{
    css_fixture();

    ROBO_CHECK(robo_css_opacity_from_percentage(0.0) == 0.0);
    ROBO_CHECK(robo_css_opacity_from_percentage(50.0) == 0.5);
    ROBO_CHECK(robo_css_opacity_from_percentage(100.0) == 1.0);
    ROBO_CHECK(robo_css_opacity_from_percentage(25.0) == 0.25);
    ROBO_CHECK(robo_css_opacity_from_percentage(1.0) == 0.01);

    /* Out of range is *not* this door's rule: it converts, and `robo_css_opacity_clamp`
     * is what the two spellings then meet at the same point. A conversion that clamped
     * would be a second place for the range to appear. */
    ROBO_CHECK(robo_css_opacity_from_percentage(200.0) == 2.0);
    ROBO_CHECK(robo_css_opacity_clamp(robo_css_opacity_from_percentage(200.0)) == 1.0);
    ROBO_CHECK(robo_css_opacity_clamp(robo_css_opacity_from_percentage(-50.0)) == 0.0);
}

/* ------------------------------------------------------------------ */
/* The text                                                            */
/* ------------------------------------------------------------------ */

/** One value's canonical text, or a diagnostic. */
static const char *opacity_text(double opacity, char *out, size_t capacity)
{
    size_t length = 0u;

    out[0] = '\0';
    if (robo_css_opacity_write(opacity, out, capacity, &length, NULL) != ROBO_OK) {
        /* A sentinel rather than a silent empty string, so a failed write shows up as a
         * mismatch rather than as an expectation that happened to be empty. */
        (void)snprintf(out, capacity, "<refused>");
        return out;
    }
    return out;
}

/**
 * The pinned fixed-point form, and the two refusals.
 *
 * The spellings are the same shape every number this subsystem has emitted since 9.8 uses,
 * and the two ends of the range are the rows worth reading: `1` and `0` print as integers
 * rather than as `1.000000`, because an author reads a dump and a value that prints in a
 * form nobody writes is a value that is hard to recognise in one.
 */
static void test_opacity_serialization(void)
{
    char text[ROBO_CSS_OPACITY_TEXT_MAX];
    char small[4];
    size_t length = 0u;

    css_fixture();

    ROBO_CHECK_STR_EQ(opacity_text(1.0, text, sizeof(text)), "1");
    ROBO_CHECK_STR_EQ(opacity_text(0.0, text, sizeof(text)), "0");
    ROBO_CHECK_STR_EQ(opacity_text(0.5, text, sizeof(text)), "0.5");
    ROBO_CHECK_STR_EQ(opacity_text(0.25, text, sizeof(text)), "0.25");
    ROBO_CHECK_STR_EQ(opacity_text(0.3, text, sizeof(text)), "0.3");
    ROBO_CHECK_STR_EQ(opacity_text(0.75, text, sizeof(text)), "0.75");
    /* Negative zero is zero and has no sign, which is the same normalization the numeric
     * writer performs: a reader should not have to know that `-0` means nothing. */
    ROBO_CHECK_STR_EQ(opacity_text(-0.0, text, sizeof(text)), "0");

    /* A value that is not a value is refused rather than printed: a serializer that wrote
     * `2` would make an out-of-range number look like a record. */
    text[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_css_opacity_write(2.0, text, sizeof(text), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_STR_EQ(text, "");
    {
        volatile double zero = 0.0;

        ROBO_CHECK_EQ_INT(
            robo_css_opacity_write(zero / zero, text, sizeof(text), &length, NULL),
            ROBO_INVALID_ARGUMENT);
    }

    /* A buffer that is too small reports the length it would have needed, so a caller can
     * size one from the refusal rather than from a constant it has to keep in step. */
    length = 0u;
    ROBO_CHECK_EQ_INT(robo_css_opacity_write(0.5, small, sizeof(small), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(small, "0.5");
    length = 0u;
    ROBO_CHECK_EQ_INT(robo_css_opacity_write(0.5, small, 2u, &length, NULL),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)length, 3);

    /* A NULL buffer and a zero capacity are argument mistakes rather than short buffers. */
    ROBO_CHECK_EQ_INT(robo_css_opacity_write(0.5, NULL, sizeof(text), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_opacity_write(0.5, text, 0u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_opacity_write(0.5, text, sizeof(text), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The shape                                                           */
/* ------------------------------------------------------------------ */

/**
 * `opacity` computed from declaration text, through the real tokenizer.
 *
 * Five things are asserted, and each is a different question:
 *
 *   both spellings of `<alpha-value>` are one value. `0.5` and `50%` compute to the same
 *   text, which is what makes the percentage conversion a fact about the property rather
 *   than about numbers;
 *
 *   `opacity: 2` and `opacity: -1` are **values**, not mistakes. This is the row that
 *   would be the loudest possible bug: CSS clamps them, and refusing would make a document
 *   every browser renders correctly render here as if the declaration had never been
 *   written;
 *
 *   the *initial* value is one, asserted as a computed value rather than only as a door --
 *   the two could disagree if a row's `initial_text` were wrong, and only one of those is
 *   visible from a declaration;
 *
 *   the shape is its own, not the generic NUMBER one. The two would both answer a number
 *   for `o: 0.5` and only one of them clamps, which is exactly the difference a shape
 *   exists to make;
 *
 *   and a `calc()` is *carried* rather than refused, for the reason every value in this
 *   build carries one: it is legal CSS whose value is decided where its inputs are.
 */
static void test_opacity_computed_shape(void)
{
    css_fixture();

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 0.5", "0.5");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: .5", "0.5");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 0.50", "0.5");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 1", "1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 1.0", "1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 0", "0");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o:  0.25  ", "0.25");

    /* The percentage spelling, which is the same value. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 50%", "0.5");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 100%", "1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 0%", "0");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 25%", "0.25");

    /* Out of range clamps rather than refusing -- both ends, both spellings. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 2", "1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 1.5", "1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 200%", "1");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: -1", "0");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: -50%", "0");

    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 0.5"),
                      "opacity");
    /*
     * Not the generic PRESERVE shape, which carries the very same `0.5` as tokens: both
     * "work" for this text and only one of them is a value a consumer can draw with. The
     * contrast is the point of giving a property a shape at all.
     */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE, "o: 0.5"),
                      "preserved");
    /* And not the length shape, which owns a different vocabulary: `10px` is a length
     * there and not an opacity here. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LENGTH, "o: 10px"),
                      "length");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 10px"),
                      "invalid");

    /* The shape's own name is what a diagnostic prints for it. */
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY),
                      "opacity");
    ROBO_CHECK_EQ_INT(
        robo_css_property_computation_is_known(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY), 1);

    /* A reference is carried, with the flag that keeps a consumer from reading the tokens
     * as an opacity. */
    {
        uint32_t flags = 0u;
        char shape[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

        css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: var(--a)", shape,
                         sizeof(shape), &flags);
        ROBO_CHECK_EQ_INT((int)flags, 1);
        ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY,
                                           "o: var(--a)"),
                          "preserved");
        css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: calc(1 / 2)", shape,
                         sizeof(shape), &flags);
        ROBO_CHECK_EQ_INT((int)flags, 1);
    }
}

/**
 * The three refusals, and they tell an author three different things.
 *
 * `banana` is not a number at all; `10px` is a number of the wrong *kind*, which is the
 * truthful answer for a property whose grammar has no length in it; and `1e999` is a number
 * the tokenizer could not make finite, which is arithmetic that went wrong rather than a
 * value anyone writes. The distinction is worth the three cases because a single "not an
 * opacity" message would send a reader looking in the wrong place for two of them.
 */
static void test_opacity_computed_invalid(void)
{
    css_fixture();

    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: banana",
                       "not a number or a percentage");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 10px",
                       "not a number or a percentage");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 2em",
                       "not a number or a percentage");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 5deg",
                       "not a number or a percentage");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 0.5 0.5",
                       "not a number or a percentage");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_OPACITY, "o: 1e999",
                       "not a finite number");
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * The computed record: the make-door, its refusal, and the accessor that closes the trap.
 *
 * The two expectations a reader should stop on are the accessor's answer for a value that
 * is *not* an opacity -- `1`, the initial value, because "this is not an opacity" must
 * render the way "nothing was declared" does -- and a cleared record's own field, which is
 * the same `1` written explicitly by the init because zeroing the struct would not produce
 * it. Those are the same fact asserted from two sides, and it is the one fact in this
 * module whose failure mode is a silently blank page.
 */
static void test_opacity_record(void)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;

    css_fixture();

    /* A cleared record holds the initial value, not zero. */
    ROBO_CHECK(robo_css_computed_value_opacity(&value) == 1.0);
    ROBO_CHECK(robo_css_computed_value_opacity(NULL) == 1.0);

    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&value, 0.5, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)value.kind, (int)ROBO_CSS_COMPUTED_VALUE_OPACITY);
    ROBO_CHECK(robo_css_computed_value_opacity(&value) == 0.5);
    ROBO_CHECK_STR_EQ(robo_css_computed_value_kind_name(value.kind), "opacity");

    /* Equality is the number, exactly: two opacities that differ are two values. */
    {
        robo_css_computed_value_t other = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&other, 0.5, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(&value, &other), 1);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&other, 0.25, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_equals(&value, &other), 0);
        robo_css_computed_value_dispose(&other);
    }

    /* The make-door refuses an out-of-range value rather than clamping it: the clamp is
     * the compute layer's rule for a declaration, and a caller that reached this door with
     * `2` skipped it. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&value, 2.0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&value, -0.5, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* And refuses no output at all. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(NULL, 0.5, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Both ends are values. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&value, 0.0, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_computed_value_opacity(&value) == 0.0);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&value, 1.0, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_computed_value_opacity(&value) == 1.0);

    /* The accessor's default, asserted on a record of another kind: a keyword is not an
     * opacity, and the answer is the initial value rather than `none`-ish nothing. */
    {
        robo_css_computed_value_t keyword = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                              &keyword, robo_css_slice_from_text("red"), NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_css_computed_value_opacity(&keyword) == 1.0);
        robo_css_computed_value_dispose(&keyword);
    }

    /* A copy carries the number, and disposes cleanly -- the value owns no memory, which
     * is why the copy is a struct copy and nothing else. It is asserted through the text
     * door rather than the accessor alone, so a value that survived the copy but could no
     * longer be written would be a failure rather than a pass. */
    {
        robo_css_computed_value_t copy = ROBO_CSS_CLEARED;
        char text[ROBO_CSS_OPACITY_TEXT_MAX];
        size_t length = 0u;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_opacity(&copy, 0.125, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_css_computed_value_opacity(&copy) == 0.125);
        text[0] = '\0';
        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&copy, text, sizeof(text),
                                                        &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(text, "0.125");
        robo_css_computed_value_dispose(&copy);
    }
    robo_css_computed_value_dispose(&value);
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

/**
 * The one row, read from the standard registry.
 *
 * Four facts, and the non-inheritance is the one worth stating: `opacity` does **not**
 * inherit, which is CSS's classification and is *not* the same fact as "a child is drawn at
 * its parent's strength". Those look alike and are opposites -- an inherited opacity would
 * make a child at `opacity: 1` inside a parent at `0.5` fully opaque again, where CSS renders
 * it at `0.5` because the parent's group opacity applies to everything it contains. Copying a
 * value down and multiplying along a path are different operations, and this row is where the
 * first one is declined so that the second can be the paint layer's.
 *
 * The initial *text* is checked as text the vocabulary knows rather than as a string, which
 * is the property that matters: a row whose initial were `0` would make every element of
 * every document that mentions nothing invisible.
 */
static void test_opacity_registry(void)
{
    robo_css_property_registry_t *registry = NULL;
    const robo_css_property_definition_t *definition;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL), ROBO_OK);

    definition = robo_css_property_registry_find(registry, "opacity");
    ROBO_CHECK(definition != NULL);
    if (definition) {
        ROBO_CHECK_EQ_INT(definition->computation,
                          (int)ROBO_CSS_PROPERTY_COMPUTATION_OPACITY);
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
        ROBO_CHECK_STR_EQ(definition->initial_text, "1");
        ROBO_CHECK_EQ_INT(robo_css_property_flags_shorthand(definition->flags), 0);
        /*
         * And the row's initial text is asserted against what the *vocabulary* writes for
         * the initial value, rather than against a second literal here. That is the
         * property that matters: a row whose initial were `0` would make every element of
         * every document that mentions nothing invisible, and this comparison is what
         * makes the table and the value model unable to disagree about it.
         */
        {
            char initial[ROBO_CSS_OPACITY_TEXT_MAX];
            size_t length = 0u;

            ROBO_CHECK_EQ_INT(robo_css_opacity_write(robo_css_opacity_initial(), initial,
                                                     sizeof(initial), &length, NULL),
                              ROBO_OK);
            ROBO_CHECK_STR_EQ(definition->initial_text, initial);
        }
    }

    /* One row and not three: `opacity` is a single longhand with nothing to expand into,
     * which is the one structural difference between it and the overflow family beside it. */
    ROBO_CHECK(robo_css_property_registry_find(registry, "opacity-x") == NULL);
    ROBO_CHECK(robo_css_property_registry_find(registry, "opacity-nonsense") == NULL);
    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_css_opacity(void);

void robo_test_register_css_opacity(void)
{
    robo_test_register("css.opacity.value", test_opacity_value);
    robo_test_register("css.opacity.clamp", test_opacity_clamp);
    robo_test_register("css.opacity.percentage", test_opacity_percentage);
    robo_test_register("css.opacity.serialization", test_opacity_serialization);
    robo_test_register("css.opacity.computed.shape", test_opacity_computed_shape);
    robo_test_register("css.opacity.computed.invalid", test_opacity_computed_invalid);
    robo_test_register("css.opacity.record", test_opacity_record);
    robo_test_register("css.opacity.registry", test_opacity_registry);
}
