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
 * enumerators had drifted would still answer `IsKnown` for both and would only show
 * up as a value that printed as the other one's keyword. Walking the set in
 * enumerator order and reading every name back is what makes that visible.
 */
static void TestOverflowVocabulary(void)
{
    static const char *const keywords[] = { "visible", "hidden" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssOverflowCount, 2);
    RoboCheckEqInt((int)CssOverflowCount(), 2);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssOverflow overflow = (CssOverflow)index;

        RoboCheckEqInt(CssOverflowIsKnown(overflow), 1);
        RoboCheckStrEq(CssOverflowName(overflow), keywords[index]);
        RoboCheckEqInt(CssOverflowFromText(keywords[index]), (int)overflow);
    }
    /* The enumerator past the end, and the value nothing defined, answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    RoboCheckEqInt(CssOverflowIsKnown(
                          (CssOverflow)RoboCssOverflowCount),
                      0);
    RoboCheckEqInt(CssOverflowIsKnown((CssOverflow)99), 0);
    RoboCheckStrEq(CssOverflowName((CssOverflow)99), "");
    RoboCheckEqInt(CssOverflowClips((CssOverflow)99), 0);
}

/**
 * `visible` is both a value and the answer for "not one", so the *identifier* is what
 * a caller has to ask.
 *
 * This is the one ambiguity the vocabulary has, and it is the reason there are two
 * doors rather than one: `CssOverflowFromSlice` answers VISIBLE for text it
 * does not recognise — which is correct, because VISIBLE is the initial value — and
 * `CssOverflowSliceIsKnown` is what tells an author that `overflow: scroll`
 * was refused rather than obeyed. `scroll` is the interesting row here: it is legal
 * CSS this build has not added, and it has to be a *reported* absence rather than a
 * silent `visible`.
 */
static void TestOverflowParsing(void)
{
    CssFixture();

    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("visible")),
                      1);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("hidden")),
                      1);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("HIDDEN")),
                      1);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("Hidden")),
                      1);
    /* Prefixes, near-misses and CSS's other overflow values. */
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("hidd")),
                      0);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("hiddenly")),
                      0);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("vis")),
                      0);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("scroll")),
                      0);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("auto")),
                      0);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("clip")),
                      0);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceFromText("")), 0);
    RoboCheckEqInt(CssOverflowSliceIsKnown(CssSliceEmpty()), 0);

    /* And the ambiguity, asserted from both sides. */
    RoboCheckEqInt(CssOverflowFromSlice(CssSliceFromText("HIDDEN")),
                      RoboCssOverflowHidden);
    RoboCheckEqInt(CssOverflowFromSlice(CssSliceFromText("scroll")),
                      RoboCssOverflowVisible);
    RoboCheckEqInt(CssOverflowFromSlice(CssSliceFromText("banana")),
                      RoboCssOverflowVisible);
    RoboCheckEqInt(CssOverflowFromSlice(CssSliceFromText("")),
                      RoboCssOverflowVisible);
    RoboCheckEqInt(CssOverflowFromText(NULL), RoboCssOverflowVisible);
}

/** One stable spelling per value, and a buffer too small is a refusal. */
static void TestOverflowSerialization(void)
{
    char buffer[RoboCssOverflowTextMax];
    char tiny[4];
    size_t length = 0u;

    CssFixture();

    RoboCheckEqInt(CssOverflowWrite(RoboCssOverflowVisible, buffer,
                                              sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "visible");
    RoboCheckEqInt((int)length, 7);
    RoboCheckEqInt(CssOverflowWrite(RoboCssOverflowHidden, buffer,
                                              sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "hidden");
    RoboCheckEqInt((int)length, 6);

    /* A refused write leaves the buffer empty and reports the length it needed, so a
     * caller can size a buffer from the refusal instead of from a constant. */
    length = 0u;
    RoboCheckEqInt(CssOverflowWrite(RoboCssOverflowVisible, tiny,
                                              sizeof(tiny), &length, NULL),
                      RoboNotSupported);
    RoboCheckStrEq(tiny, "");
    RoboCheckEqInt((int)length, 7);
    RoboCheckEqInt(CssOverflowWrite((CssOverflow)99, buffer,
                                              sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssOverflowWrite(RoboCssOverflowVisible, NULL,
                                              sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
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
static void TestOverflowClassification(void)
{
    CssFixture();

    RoboCheckEqInt(CssOverflowClips(RoboCssOverflowVisible), 0);
    RoboCheckEqInt(CssOverflowClips(RoboCssOverflowHidden), 1);

    RoboCheckEqInt(CssOverflowIsVisible(RoboCssOverflowVisible), 1);
    RoboCheckEqInt(CssOverflowIsVisible(RoboCssOverflowHidden), 0);

    /* The two classifications partition the set: no value both clips and is the
     * initial state, and none is neither. */
    RoboCheckEqInt(CssOverflowClips(RoboCssOverflowVisible) +
                          CssOverflowIsVisible(RoboCssOverflowVisible),
                      1);
    RoboCheckEqInt(CssOverflowClips(RoboCssOverflowHidden) +
                          CssOverflowIsVisible(RoboCssOverflowHidden),
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
static void TestOverflowComputedShape(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: visible", "visible");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: hidden", "hidden");
    /* Whitespace and case are the tokenizer's and the vocabulary's, so all four
     * spellings are one value. */
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o:hidden", "hidden");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o:   hidden   ", "hidden");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: HIDDEN\n", "hidden");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: Visible", "visible");

    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationOverflow, "o: hidden"),
                      "overflow");
    /* Not the generic keyword shape, even though the text is one word: the two are
     * different representations of different things, and only one of them refuses an
     * identifier it does not know. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationKeyword, "o: hidden"),
                      "keyword");

    /* The shape's own name is what a diagnostic prints for it. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationOverflow),
                      "overflow");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationOverflow),
                      1);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)
                              RoboCssPropertyComputationCount),
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
static void TestOverflowComputedInvalid(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: banana",
                       "not an overflow value");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: scroll",
                       "not an overflow value");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: auto",
                       "not an overflow value");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: 20px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationOverflow, "o: hidden visible",
                       "not a single overflow keyword");
    /* A reference is *carried* rather than refused, for the reason every closed
     * vocabulary in this build carries one: `overflow: var(--o)` is a declaration
     * whose value is decided where `--o` is, which is a later stage's question. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationOverflow,
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
static void TestOverflowRegistry(void)
{
    static const char *const names[] = { "overflow", "overflow-x", "overflow-y" };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, names[index]);

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(definition->computation,
                          (int)RoboCssPropertyComputationOverflow);
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckStrEq(definition->initialText, "visible");
        /* The initial text is checked as text the *vocabulary* knows rather than as a
         * string, which is the property that matters: a row whose initial were
         * `hidden` would make every box that declares nothing clip. */
        RoboCheckEqInt(CssOverflowSliceIsKnown(
                              CssSliceFromText(definition->initialText)),
                          1);
        RoboCheckEqInt(CssOverflowFromText(definition->initialText),
                          RoboCssOverflowVisible);
    }
    /* The shorthand is flagged as what it is, and the two longhands are not. */
    {
        const CssPropertyDefinition *shorthand =
            CssPropertyRegistryFind(registry, "overflow");

        RoboCheck(shorthand != NULL);
        if (shorthand) {
            RoboCheckEqInt(CssPropertyFlagsShorthand(shorthand->flags), 1);
        }
    }
    RoboCheck(CssPropertyRegistryFind(registry, "overflow-nonsense") == NULL);
    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterCssOverflow(void);

void TestRegisterCssOverflow(void)
{
    TestRegister("css.overflow.vocabulary", TestOverflowVocabulary);
    TestRegister("css.overflow.parsing", TestOverflowParsing);
    TestRegister("css.overflow.serialization", TestOverflowSerialization);
    TestRegister("css.overflow.classification", TestOverflowClassification);
    TestRegister("css.overflow.computed.shape", TestOverflowComputedShape);
    TestRegister("css.overflow.computed.invalid", TestOverflowComputedInvalid);
    TestRegister("css.overflow.registry", TestOverflowRegistry);
}
