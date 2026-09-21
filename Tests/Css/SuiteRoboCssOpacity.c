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
static void TestOpacityValue(void)
{
    CssFixture();

    RoboCheck(CssOpacityInitial() == 1.0);
    RoboCheckEqInt(CssOpacityIsOpaque(CssOpacityInitial()), 1);
    RoboCheckEqInt(CssOpacityIsTransparent(CssOpacityInitial()), 0);
    RoboCheckEqInt(CssOpacityIsValid(CssOpacityInitial()), 1);

    /* Both ends are values, and they are the two that matter: fully opaque is the state
     * in which the property changes nothing, fully transparent the state in which nothing
     * draws. */
    RoboCheckEqInt(CssOpacityIsValid(0.0), 1);
    RoboCheckEqInt(CssOpacityIsOpaque(0.0), 0);
    RoboCheckEqInt(CssOpacityIsTransparent(0.0), 1);
    RoboCheckEqInt(CssOpacityIsValid(1.0), 1);
    RoboCheckEqInt(CssOpacityIsOpaque(1.0), 1);
    RoboCheckEqInt(CssOpacityIsTransparent(1.0), 0);

    RoboCheckEqInt(CssOpacityIsValid(0.5), 1);
    RoboCheckEqInt(CssOpacityIsValid(0.000001), 1);
    RoboCheckEqInt(CssOpacityIsValid(0.999999), 1);
    RoboCheckEqInt(CssOpacityIsOpaque(0.5), 0);
    RoboCheckEqInt(CssOpacityIsTransparent(0.5), 0);

    /* Out of range, on both sides, and the smallest amount out of it. */
    RoboCheckEqInt(CssOpacityIsValid(-0.000001), 0);
    RoboCheckEqInt(CssOpacityIsValid(1.000001), 0);
    RoboCheckEqInt(CssOpacityIsValid(-1.0), 0);
    RoboCheckEqInt(CssOpacityIsValid(2.0), 0);

    /* Negative zero is zero, and it is a value rather than an out-of-range number: the
     * comparison is numerical, so the sign bit is not a fact about the opacity. */
    RoboCheckEqInt(CssOpacityIsValid(-0.0), 1);
    RoboCheckEqInt(CssOpacityIsTransparent(-0.0), 1);

    /* And the values that are not numbers at all. Every comparison against a NaN is false,
     * which is why the validity rule separates it rather than relying on the range test. */
    {
        volatile double zero = 0.0;
        double nanValue = zero / zero;
        double infinite = 1.0 / zero;

        RoboCheckEqInt(CssOpacityIsValid(nanValue), 0);
        RoboCheckEqInt(CssOpacityIsValid(infinite), 0);
        RoboCheckEqInt(CssOpacityIsValid(-infinite), 0);
        RoboCheckEqInt(CssOpacityIsTransparent(nanValue), 0);
        RoboCheckEqInt(CssOpacityIsOpaque(nanValue), 0);
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
static void TestOpacityClamp(void)
{
    volatile double zero = 0.0;
    double nanValue = zero / zero;

    CssFixture();

    /* In range: unchanged, so the door is safe to apply to a value a caller has not
     * questioned and safe to apply twice. */
    RoboCheck(CssOpacityClamp(0.0) == 0.0);
    RoboCheck(CssOpacityClamp(0.25) == 0.25);
    RoboCheck(CssOpacityClamp(0.5) == 0.5);
    RoboCheck(CssOpacityClamp(1.0) == 1.0);
    RoboCheck(CssOpacityClamp(CssOpacityClamp(0.5)) == 0.5);

    /* Above one, below zero, and the infinities -- which need no test of their own because
     * each is outside the range on its own side. */
    RoboCheck(CssOpacityClamp(1.5) == 1.0);
    RoboCheck(CssOpacityClamp(2.0) == 1.0);
    RoboCheck(CssOpacityClamp(100.0) == 1.0);
    RoboCheck(CssOpacityClamp(1.0 / zero) == 1.0);
    RoboCheck(CssOpacityClamp(-0.5) == 0.0);
    RoboCheck(CssOpacityClamp(-100.0) == 0.0);
    RoboCheck(CssOpacityClamp(-1.0 / zero) == 0.0);

    /* Not a number: the initial value, and the direction is the whole point of the case. */
    RoboCheck(CssOpacityClamp(nanValue) == CssOpacityInitial());

    /* Every clamped result is a value, which is what makes it safe to hand one to the
     * make-door without asking again. */
    RoboCheckEqInt(CssOpacityIsValid(CssOpacityClamp(2.0)), 1);
    RoboCheckEqInt(CssOpacityIsValid(CssOpacityClamp(-2.0)), 1);
    RoboCheckEqInt(CssOpacityIsValid(CssOpacityClamp(nanValue)), 1);
}

/** The percentage spelling: the same value, divided by a hundred, and not clamped here. */
static void TestOpacityPercentage(void)
{
    CssFixture();

    RoboCheck(CssOpacityFromPercentage(0.0) == 0.0);
    RoboCheck(CssOpacityFromPercentage(50.0) == 0.5);
    RoboCheck(CssOpacityFromPercentage(100.0) == 1.0);
    RoboCheck(CssOpacityFromPercentage(25.0) == 0.25);
    RoboCheck(CssOpacityFromPercentage(1.0) == 0.01);

    /* Out of range is *not* this door's rule: it converts, and `CssOpacityClamp`
     * is what the two spellings then meet at the same point. A conversion that clamped
     * would be a second place for the range to appear. */
    RoboCheck(CssOpacityFromPercentage(200.0) == 2.0);
    RoboCheck(CssOpacityClamp(CssOpacityFromPercentage(200.0)) == 1.0);
    RoboCheck(CssOpacityClamp(CssOpacityFromPercentage(-50.0)) == 0.0);
}

/* ------------------------------------------------------------------ */
/* The text                                                            */
/* ------------------------------------------------------------------ */

/** One value's canonical text, or a diagnostic. */
static const char *OpacityText(double opacity, char *out, size_t capacity)
{
    size_t length = 0u;

    out[0] = '\0';
    if (CssOpacityWrite(opacity, out, capacity, &length, NULL) != RoboOk) {
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
static void TestOpacitySerialization(void)
{
    char text[RoboCssOpacityTextMax];
    char small[4];
    size_t length = 0u;

    CssFixture();

    RoboCheckStrEq(OpacityText(1.0, text, sizeof(text)), "1");
    RoboCheckStrEq(OpacityText(0.0, text, sizeof(text)), "0");
    RoboCheckStrEq(OpacityText(0.5, text, sizeof(text)), "0.5");
    RoboCheckStrEq(OpacityText(0.25, text, sizeof(text)), "0.25");
    RoboCheckStrEq(OpacityText(0.3, text, sizeof(text)), "0.3");
    RoboCheckStrEq(OpacityText(0.75, text, sizeof(text)), "0.75");
    /* Negative zero is zero and has no sign, which is the same normalization the numeric
     * writer performs: a reader should not have to know that `-0` means nothing. */
    RoboCheckStrEq(OpacityText(-0.0, text, sizeof(text)), "0");

    /* A value that is not a value is refused rather than printed: a serializer that wrote
     * `2` would make an out-of-range number look like a record. */
    text[0] = '\0';
    RoboCheckEqInt(CssOpacityWrite(2.0, text, sizeof(text), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckStrEq(text, "");
    {
        volatile double zero = 0.0;

        RoboCheckEqInt(
            CssOpacityWrite(zero / zero, text, sizeof(text), &length, NULL),
            RoboInvalidArgument);
    }

    /* A buffer that is too small reports the length it would have needed, so a caller can
     * size one from the refusal rather than from a constant it has to keep in step. */
    length = 0u;
    RoboCheckEqInt(CssOpacityWrite(0.5, small, sizeof(small), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(small, "0.5");
    length = 0u;
    RoboCheckEqInt(CssOpacityWrite(0.5, small, 2u, &length, NULL),
                      RoboNotSupported);
    RoboCheckEqInt((int)length, 3);

    /* A NULL buffer and a zero capacity are argument mistakes rather than short buffers. */
    RoboCheckEqInt(CssOpacityWrite(0.5, NULL, sizeof(text), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssOpacityWrite(0.5, text, 0u, &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssOpacityWrite(0.5, text, sizeof(text), NULL, NULL),
                      RoboInvalidArgument);
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
 *   the two could disagree if a row's `initialText` were wrong, and only one of those is
 *   visible from a declaration;
 *
 *   the shape is its own, not the generic NUMBER one. The two would both answer a number
 *   for `o: 0.5` and only one of them clamps, which is exactly the difference a shape
 *   exists to make;
 *
 *   and a `calc()` is *carried* rather than refused, for the reason every value in this
 *   build carries one: it is legal CSS whose value is decided where its inputs are.
 */
static void TestOpacityComputedShape(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 0.5", "0.5");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: .5", "0.5");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 0.50", "0.5");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 1", "1");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 1.0", "1");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 0", "0");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o:  0.25  ", "0.25");

    /* The percentage spelling, which is the same value. */
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 50%", "0.5");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 100%", "1");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 0%", "0");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 25%", "0.25");

    /* Out of range clamps rather than refusing -- both ends, both spellings. */
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 2", "1");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 1.5", "1");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 200%", "1");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: -1", "0");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: -50%", "0");

    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationOpacity, "o: 0.5"),
                      "opacity");
    /*
     * Not the generic PRESERVE shape, which carries the very same `0.5` as tokens: both
     * "work" for this text and only one of them is a value a consumer can draw with. The
     * contrast is the point of giving a property a shape at all.
     */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve, "o: 0.5"),
                      "preserved");
    /* And not the length shape, which owns a different vocabulary: `10px` is a length
     * there and not an opacity here. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLength, "o: 10px"),
                      "length");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationOpacity, "o: 10px"),
                      "invalid");

    /* The shape's own name is what a diagnostic prints for it. */
    RoboCheckStrEq(CssPropertyComputationName(RoboCssPropertyComputationOpacity),
                      "opacity");
    RoboCheckEqInt(
        CssPropertyComputationIsKnown(RoboCssPropertyComputationOpacity), 1);

    /* A reference is carried, with the flag that keeps a consumer from reading the tokens
     * as an opacity. */
    {
        uint32_t flags = 0u;
        char shape[RoboCssComputedValueTextMax];

        CssComputeText(RoboCssPropertyComputationOpacity, "o: var(--a)", shape,
                         sizeof(shape), &flags);
        RoboCheckEqInt((int)flags, 1);
        RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationOpacity,
                                           "o: var(--a)"),
                          "preserved");
        CssComputeText(RoboCssPropertyComputationOpacity, "o: calc(1 / 2)", shape,
                         sizeof(shape), &flags);
        RoboCheckEqInt((int)flags, 1);
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
static void TestOpacityComputedInvalid(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: banana",
                       "not a number or a percentage");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 10px",
                       "not a number or a percentage");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 2em",
                       "not a number or a percentage");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 5deg",
                       "not a number or a percentage");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 0.5 0.5",
                       "not a number or a percentage");
    CssComputeEquals(RoboCssPropertyComputationOpacity, "o: 1e999",
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
static void TestOpacityRecord(void)
{
    CssComputedValue value = RoboCssCleared;

    CssFixture();

    /* A cleared record holds the initial value, not zero. */
    RoboCheck(CssComputedValueOpacity(&value) == 1.0);
    RoboCheck(CssComputedValueOpacity(NULL) == 1.0);

    RoboCheckEqInt(CssComputedValueMakeOpacity(&value, 0.5, NULL), RoboOk);
    RoboCheckEqInt((int)value.kind, (int)RoboCssComputedValueOpacity);
    RoboCheck(CssComputedValueOpacity(&value) == 0.5);
    RoboCheckStrEq(CssComputedValueKindName(value.kind), "opacity");

    /* Equality is the number, exactly: two opacities that differ are two values. */
    {
        CssComputedValue other = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueMakeOpacity(&other, 0.5, NULL), RoboOk);
        RoboCheckEqInt(CssComputedValueEquals(&value, &other), 1);
        RoboCheckEqInt(CssComputedValueMakeOpacity(&other, 0.25, NULL), RoboOk);
        RoboCheckEqInt(CssComputedValueEquals(&value, &other), 0);
        CssComputedValueDispose(&other);
    }

    /* The make-door refuses an out-of-range value rather than clamping it: the clamp is
     * the compute layer's rule for a declaration, and a caller that reached this door with
     * `2` skipped it. */
    RoboCheckEqInt(CssComputedValueMakeOpacity(&value, 2.0, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedValueMakeOpacity(&value, -0.5, NULL),
                      RoboInvalidArgument);
    /* And refuses no output at all. */
    RoboCheckEqInt(CssComputedValueMakeOpacity(NULL, 0.5, NULL),
                      RoboInvalidArgument);

    /* Both ends are values. */
    RoboCheckEqInt(CssComputedValueMakeOpacity(&value, 0.0, NULL), RoboOk);
    RoboCheck(CssComputedValueOpacity(&value) == 0.0);
    RoboCheckEqInt(CssComputedValueMakeOpacity(&value, 1.0, NULL), RoboOk);
    RoboCheck(CssComputedValueOpacity(&value) == 1.0);

    /* The accessor's default, asserted on a record of another kind: a keyword is not an
     * opacity, and the answer is the initial value rather than `none`-ish nothing. */
    {
        CssComputedValue keyword = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueMakeKeyword(
                              &keyword, CssSliceFromText("red"), NULL),
                          RoboOk);
        RoboCheck(CssComputedValueOpacity(&keyword) == 1.0);
        CssComputedValueDispose(&keyword);
    }

    /* A copy carries the number, and disposes cleanly -- the value owns no memory, which
     * is why the copy is a struct copy and nothing else. It is asserted through the text
     * door rather than the accessor alone, so a value that survived the copy but could no
     * longer be written would be a failure rather than a pass. */
    {
        CssComputedValue copy = RoboCssCleared;
        char text[RoboCssOpacityTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssComputedValueMakeOpacity(&copy, 0.125, NULL),
                          RoboOk);
        RoboCheck(CssComputedValueOpacity(&copy) == 0.125);
        text[0] = '\0';
        RoboCheckEqInt(CssComputedValueWrite(&copy, text, sizeof(text),
                                                        &length, NULL),
                          RoboOk);
        RoboCheckStrEq(text, "0.125");
        CssComputedValueDispose(&copy);
    }
    CssComputedValueDispose(&value);
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
static void TestOpacityRegistry(void)
{
    CssPropertyRegistry *registry = NULL;
    const CssPropertyDefinition *definition;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);

    definition = CssPropertyRegistryFind(registry, "opacity");
    RoboCheck(definition != NULL);
    if (definition) {
        RoboCheckEqInt(definition->computation,
                          (int)RoboCssPropertyComputationOpacity);
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckStrEq(definition->initialText, "1");
        RoboCheckEqInt(CssPropertyFlagsShorthand(definition->flags), 0);
        /*
         * And the row's initial text is asserted against what the *vocabulary* writes for
         * the initial value, rather than against a second literal here. That is the
         * property that matters: a row whose initial were `0` would make every element of
         * every document that mentions nothing invisible, and this comparison is what
         * makes the table and the value model unable to disagree about it.
         */
        {
            char initial[RoboCssOpacityTextMax];
            size_t length = 0u;

            RoboCheckEqInt(CssOpacityWrite(CssOpacityInitial(), initial,
                                                     sizeof(initial), &length, NULL),
                              RoboOk);
            RoboCheckStrEq(definition->initialText, initial);
        }
    }

    /* One row and not three: `opacity` is a single longhand with nothing to expand into,
     * which is the one structural difference between it and the overflow family beside it. */
    RoboCheck(CssPropertyRegistryFind(registry, "opacity-x") == NULL);
    RoboCheck(CssPropertyRegistryFind(registry, "opacity-nonsense") == NULL);
    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterCssOpacity(void);

void TestRegisterCssOpacity(void)
{
    TestRegister("css.opacity.value", TestOpacityValue);
    TestRegister("css.opacity.clamp", TestOpacityClamp);
    TestRegister("css.opacity.percentage", TestOpacityPercentage);
    TestRegister("css.opacity.serialization", TestOpacitySerialization);
    TestRegister("css.opacity.computed.shape", TestOpacityComputedShape);
    TestRegister("css.opacity.computed.invalid", TestOpacityComputedInvalid);
    TestRegister("css.opacity.record", TestOpacityRecord);
    TestRegister("css.opacity.registry", TestOpacityRegistry);
}
