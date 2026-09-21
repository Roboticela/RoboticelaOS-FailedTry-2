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
 * enumerators had drifted would still answer `IsKnown` for all four and would
 * only show up as a value that printed as another one's keyword. Walking the set
 * in enumerator order and reading every name back is what makes that visible.
 */
static void TestDisplayVocabulary(void)
{
    static const char *const keywords[] = { "none", "block", "inline", "inline-block" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssDisplayCount, 4);
    RoboCheckEqInt((int)CssDisplayCount(), 4);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssDisplay display = (CssDisplay)index;

        RoboCheckEqInt(CssDisplayIsKnown(display), 1);
        RoboCheckStrEq(CssDisplayName(display), keywords[index]);
        RoboCheckEqInt(CssDisplayFromText(keywords[index]), (int)display);
    }
    /* The enumerator past the end and a value nothing defined answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    RoboCheckEqInt(CssDisplayIsKnown(
                          (CssDisplay)RoboCssDisplayCount),
                      0);
    RoboCheckEqInt(CssDisplayIsKnown((CssDisplay)99), 0);
    RoboCheckStrEq(CssDisplayName((CssDisplay)99), "");
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
static void TestDisplayOuterInner(void)
{
    static const struct {
        CssDisplay display;
        CssDisplayOuter outer;
        CssDisplayInner inner;
        const char *outerName;
        const char *innerName;
    } expected[] = {
        {RoboCssDisplayNone, RoboCssDisplayOuterNone, RoboCssDisplayInnerNone,
         "none", "none"},
        {RoboCssDisplayBlock, RoboCssDisplayOuterBlock, RoboCssDisplayInnerFlow,
         "block", "flow"},
        {RoboCssDisplayInline, RoboCssDisplayOuterInline,
         RoboCssDisplayInnerFlow, "inline", "flow"},
        {RoboCssDisplayInlineBlock, RoboCssDisplayOuterInline,
         RoboCssDisplayInnerFlowRoot, "inline", "flow-root"}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssDisplayOuterCount, 3);
    RoboCheckEqInt((int)RoboCssDisplayInnerCount, 3);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        RoboCheckEqInt(MakeCssDisplayOuter(expected[index].display),
                          (int)expected[index].outer);
        RoboCheckEqInt(MakeCssDisplayInner(expected[index].display),
                          (int)expected[index].inner);
        RoboCheckStrEq(CssDisplayOuterName(expected[index].outer),
                          expected[index].outerName);
        RoboCheckStrEq(CssDisplayInnerName(expected[index].inner),
                          expected[index].innerName);
    }
    /* An unknown enumerator is not a display value, so it has no halves — which
     * is the answer `OUTER_NONE` gives and the reason it is not a failure. */
    RoboCheckEqInt(MakeCssDisplayOuter((CssDisplay)99),
                      (int)RoboCssDisplayOuterNone);
    RoboCheckEqInt(MakeCssDisplayInner((CssDisplay)99),
                      (int)RoboCssDisplayInnerNone);
    RoboCheckStrEq(CssDisplayOuterName((CssDisplayOuter)99),
                      "unknown");
    RoboCheckStrEq(CssDisplayInnerName((CssDisplayInner)99),
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
static void TestDisplayClassification(void)
{
    static const struct {
        CssDisplay display;
        int generatesBox;
        int isNone;
        int blockLevel;
        int inlineLevel;
        int inlineBlock;
    } expected[] = {
        {RoboCssDisplayNone, 0, 1, 0, 0, 0},
        {RoboCssDisplayBlock, 1, 0, 1, 0, 0},
        {RoboCssDisplayInline, 1, 0, 0, 1, 0},
        {RoboCssDisplayInlineBlock, 1, 0, 0, 1, 1}
    };
    size_t index;
    int boxless = 0;
    int atomic = 0;

    CssFixture();
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        CssDisplay display = expected[index].display;

        RoboCheckEqInt(CssDisplayGeneratesBox(display),
                          expected[index].generatesBox);
        RoboCheckEqInt(CssDisplayIsNone(display), expected[index].isNone);
        RoboCheckEqInt(CssDisplayIsBlockLevel(display),
                          expected[index].blockLevel);
        RoboCheckEqInt(CssDisplayIsInlineLevel(display),
                          expected[index].inlineLevel);
        RoboCheckEqInt(CssDisplayIsInlineBlock(display),
                          expected[index].inlineBlock);

        /* The invariants, asserted per value rather than once over the table. */
        RoboCheckEqInt(CssDisplayIsBlockLevel(display) +
                              CssDisplayIsInlineLevel(display),
                          expected[index].generatesBox);
        if (CssDisplayIsInlineBlock(display)) {
            RoboCheckEqInt(CssDisplayIsInlineLevel(display), 1);
        }
        if (!CssDisplayGeneratesBox(display)) {
            boxless++;
            /* No box means no contents, so there is nothing to lay out. */
            RoboCheckEqInt(MakeCssDisplayInner(display),
                              (int)RoboCssDisplayInnerNone);
        }
        if (CssDisplayIsInlineBlock(display)) {
            atomic++;
        }
    }
    /* Exactly one value generates no box, and exactly one is atomic. A row added
     * without one of those facts being reconsidered moves these counts and fails
     * here rather than in a layout pass. */
    RoboCheckEqInt(boxless, 1);
    RoboCheckEqInt(atomic, 1);
    /* An unknown enumerator answers "no" to every question rather than the first
     * row's answers. */
    RoboCheckEqInt(CssDisplayGeneratesBox((CssDisplay)99), 0);
    RoboCheckEqInt(CssDisplayIsNone((CssDisplay)99), 0);
    RoboCheckEqInt(CssDisplayIsBlockLevel((CssDisplay)99), 0);
    RoboCheckEqInt(CssDisplayIsInlineLevel((CssDisplay)99), 0);
    RoboCheckEqInt(CssDisplayIsInlineBlock((CssDisplay)99), 0);
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
static void TestDisplayParsing(void)
{
    static const struct {
        const char *text;
        CssDisplay display;
    } known[] = {
        {"none", RoboCssDisplayNone},
        {"block", RoboCssDisplayBlock},
        {"inline", RoboCssDisplayInline},
        {"inline-block", RoboCssDisplayInlineBlock},
        /* CSS spells keywords case-insensitively. */
        {"BLOCK", RoboCssDisplayBlock},
        {"Inline", RoboCssDisplayInline},
        {"INLINE-BLOCK", RoboCssDisplayInlineBlock},
        {"None", RoboCssDisplayNone}
    };
    static const char *const unknown[] = {
        "bogus", "flex", "grid", "table", "contents", "inline-flex", "inline_block",
        "block;", "inlineblock", "blockflow", ""
    };
    size_t index;
    char text[64];

    CssFixture();
    for (index = 0u; index < sizeof(known) / sizeof(known[0]); index++) {
        CssSlice slice = CssSliceFromText(known[index].text);

        RoboCheckEqInt(CssDisplaySliceIsKnown(slice), 1);
        RoboCheckEqInt(CssDisplayFromSlice(slice), (int)known[index].display);
        RoboCheckEqInt(CssDisplayFromText(known[index].text),
                          (int)known[index].display);
    }
    for (index = 0u; index < sizeof(unknown) / sizeof(unknown[0]); index++) {
        CssSlice slice = CssSliceFromText(unknown[index]);

        /* Not one of them — and the two answers are asked together, because the
         * whole point is that the first cannot say this on its own. */
        RoboCheckEqInt(CssDisplaySliceIsKnown(slice), 0);
        RoboCheckEqInt(CssDisplayFromSlice(slice),
                          (int)RoboCssDisplayNone);
    }
    /* A NULL text and an absent slice are not identifiers at all. */
    RoboCheckEqInt(CssDisplayFromText(NULL), (int)RoboCssDisplayNone);
    RoboCheckEqInt(CssDisplaySliceIsKnown(CssSliceEmpty()), 0);
    RoboCheckEqInt(CssDisplaySliceIsKnown((CssSlice){NULL, 0u}), 0);
    RoboCheckEqInt(CssDisplayFromSlice(CssSliceEmpty()),
                      (int)RoboCssDisplayNone);

    /* Round trip: every value prints as the keyword that reads back as it. */
    for (index = 0u; index < (size_t)RoboCssDisplayCount; index++) {
        size_t length = 0u;

        RoboCheckEqInt(CssDisplayWrite((CssDisplay)index, text,
                                                 sizeof(text), &length, NULL),
                          RoboOk);
        RoboCheckEqInt((int)length, (int)strlen(text));
        RoboCheckEqInt(CssDisplayFromText(text), (int)index);
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
static void TestDisplaySerialization(void)
{
    char buffer[RoboCssDisplayTextMax];
    size_t length = 0u;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssDisplayWrite(RoboCssDisplayInlineBlock, buffer,
                                             sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "inline-block");
    RoboCheckEqInt((int)length, 12);
    RoboCheckEqInt(CssDisplayWrite(RoboCssDisplayNone, buffer,
                                             sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "none");

    /* Too small, and the buffer is left empty rather than half-filled. */
    buffer[0] = 'x';
    buffer[1] = '\0';
    RoboCheckEqInt(CssDisplayWrite(RoboCssDisplayInlineBlock, buffer, 4u,
                                             &length, NULL),
                      RoboNotSupported);
    RoboCheckStrEq(buffer, "");
    RoboCheckEqInt((int)length, 12);
    /* Exactly big enough is enough: twelve bytes plus the NUL. */
    RoboCheckEqInt(CssDisplayWrite(RoboCssDisplayInlineBlock, buffer,
                                             13u, &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "inline-block");

    /* An enumerator nothing defined has no spelling, and NULL arguments are
     * refused rather than written through. */
    RoboCheckEqInt(CssDisplayWrite((CssDisplay)99, buffer,
                                             sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDisplayWrite(RoboCssDisplayBlock, NULL,
                                             sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDisplayWrite(RoboCssDisplayBlock, buffer,
                                             sizeof(buffer), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssDisplayWrite(RoboCssDisplayBlock, buffer, 0u,
                                             &length, NULL),
                      RoboInvalidArgument);

    /* And every row survives the round trip through a buffer sized by the
     * constant the header hands out, which is what that constant is for. */
    for (index = 0u; index < (size_t)RoboCssDisplayCount; index++) {
        RoboCheckEqInt(CssDisplayWrite((CssDisplay)index, buffer,
                                                 sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheck(length < RoboCssDisplayTextMax);
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
static void TestDisplayComputedShape(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: none", "none");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: block", "block");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: inline", "inline");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: inline-block",
                       "inline-block");
    /* Whitespace around the value and around the colon is the tokenizer's, and
     * all three spellings are the same declaration. */
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d:block", "block");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d:   block   ", "block");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: INLINE-BLOCK\n",
                       "inline-block");

    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationDisplay,
                                       "d: block"),
                      "display");
    /* Not the generic keyword shape, even though the text is one word: the two
     * are different representations of different things. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationDisplay,
                                       "d: inline"),
                      "display");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationKeyword,
                                       "d: inline"),
                      "keyword");

    /* The shape's own name is what a diagnostic prints for it. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationDisplay),
                      "display");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationDisplay),
                      1);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)
                              RoboCssPropertyComputationCount),
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
        CssPropertyRegistry *registry = NULL;
        const CssPropertyDefinition *definition;

        RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                          RoboOk);
        definition = CssPropertyRegistryFind(registry, "display");
        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              (int)RoboCssPropertyComputationDisplay);
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
            RoboCheckStrEq(definition->initialText, "inline");
            RoboCheckEqInt(CssDisplayFromText(definition->initialText),
                              (int)RoboCssDisplayInline);
            RoboCheckEqInt(CssDisplaySliceIsKnown(
                                  CssSliceFromText(definition->initialText)),
                              1);
        }
        CssPropertyRegistryDestroy(registry);
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
static void TestDisplayComputedInvalid(void)
{
    CssFixture();

    /* An identifier, and not one of the four. */
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: bogus",
                       "not a display value");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: flex",
                       "not a display value");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: grid",
                       "not a display value");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: table",
                       "not a display value");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: contents",
                       "not a display value");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: inline-flex",
                       "not a display value");
    /* The spelling with an underscore, which is how a keyword reads in code and
     * not how CSS spells it. */
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: inline_block",
                       "not a display value");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: red",
                       "not a display value");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationDisplay,
                                       "d: bogus"),
                      "invalid");

    /* Not an identifier: a dimension, a number, a percentage, a hash, a string,
     * a URL. The tokenizer classifies each of them, and none is a display. */
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: 20px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: 10",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: 10%",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: #fff",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: \"block\"",
                       "not an identifier");
    /* A URL token is one token and is still not an identifier. */
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: url(block)",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: rgb(0, 0, 0)",
                       "not a single display keyword");

    /* Two keywords: legal CSS 3 that this build does not parse, and it says so
     * rather than picking one. `inline block` is `inline-block` to a browser and
     * is refused here, which is the limitation the phase documents. */
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: inline block",
                       "not a single display keyword");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: inline flow-root",
                       "not a single display keyword");
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d: block flow",
                       "not a single display keyword");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationDisplay,
                                       "d: inline block"),
                      "invalid");

    /* A declaration with no value at all recovers as a value with no components,
     * which is not an identifier either. It is refused rather than read as the
     * property's initial value: `display: ;` is not a declaration an author
     * meant, and the cascade drops it, so the element is left with no declaration
     * and gets its initial value by *that* route rather than by this one. */
    CssComputeEquals(RoboCssPropertyComputationDisplay, "d:",
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
static void TestDisplayComputedReference(void)
{
    uint32_t flags = 0u;
    char text[RoboCssComputedValueTextMax];

    CssFixture();

    CssComputeText(RoboCssPropertyComputationDisplay, "d: var(--d)", text,
                     sizeof(text), &flags);
    RoboCheckStrEq(text, "var(--d)");
    RoboCheckEqInt((int)flags, 1);
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationDisplay,
                                       "d: var(--d)"),
                      "preserved");

    flags = 0u;
    CssComputeText(RoboCssPropertyComputationDisplay, "d: var(--d, block)", text,
                     sizeof(text), &flags);
    RoboCheckStrEq(text, "var(--d, block)");
    RoboCheckEqInt((int)flags, 1);

    flags = 0u;
    CssComputeText(RoboCssPropertyComputationDisplay, "d: env(display)", text,
                     sizeof(text), &flags);
    RoboCheckStrEq(text, "env(display)");
    RoboCheckEqInt((int)flags, 1);

    /* A resolved keyword is *not* flagged unresolved, so the flag is a fact about
     * this value rather than a constant of the shape. */
    flags = 0u;
    CssComputeText(RoboCssPropertyComputationDisplay, "d: block", text,
                     sizeof(text), &flags);
    RoboCheckStrEq(text, "block");
    RoboCheckEqInt((int)flags, 0);
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void CssDisplayComputeOf(CssInheritFixture *fixture,
                                   CssElementRef ref,
                                   CssComputedStyle *out)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssStyleContextComputeStyle(CssInheritContext(fixture),
                                                          ref, out, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
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
static CssDisplay CssDisplayOf(CssInheritFixture *fixture,
                                         const CssComputedStyle *style,
                                         int *outFromInitial)
{
    CssComputedValue value = RoboCssCleared;
    CssDisplay display;

    if (outFromInitial) {
        *outFromInitial = -1;
    }
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          CssInheritContext(fixture), style,
                          CssSliceFromText("display"), &value,
                          outFromInitial, NULL),
                      RoboOk);
    /* The answer is a display *value* and not the keyword's letters, which is
     * what the phase's shape change buys a consumer. */
    RoboCheckEqInt(value.kind, (int)RoboCssComputedValueDisplay);
    display = CssComputedValueDisplay(&value);
    CssComputedValueDispose(&value);
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
static void TestDisplayComputedStyleQuery(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef shown;
    CssElementRef plain;
    CssDisplay display;
    int fromInitial = -1;

    CssFixture();
    CssInheritBegin(&fixture);
    shown = CssHostAdd(CssInheritHost(&fixture), "div", "shown", NULL);
    plain = CssHostAdd(CssInheritHost(&fixture), "span", "plain", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#shown { display: inline-block } #plain { color: red }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssDisplayComputeOf(&fixture, shown, style);
    display = CssDisplayOf(&fixture, style, &fromInitial);
    RoboCheckEqInt((int)display, (int)RoboCssDisplayInlineBlock);
    RoboCheckEqInt(fromInitial, 0);
    RoboCheckEqInt(CssDisplayIsInlineBlock(display), 1);
    RoboCheckEqInt(CssDisplayGeneratesBox(display), 1);
    /* The same fact by name, as text, and through the record's own accessor. */
    CssComputedIs(style, "display", "inline-block");
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(style,
                                                    CssSliceFromText("display"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValueDisplay);
        RoboCheckEqInt(CssComputedValueDisplay(value),
                          (int)RoboCssDisplayInlineBlock);
    }

    /* An element that declared nothing has no *entry* — a non-inherited property
     * the element does not mention is not materialised (9.7) — and its display is
     * still the property's initial value `inline`, which the query door answers
     * and marks as coming from the initial value. */
    CssDisplayComputeOf(&fixture, plain, style);
    CssComputedIs(style, "display", "<absent>");
    display = CssDisplayOf(&fixture, style, &fromInitial);
    RoboCheckEqInt((int)display, (int)RoboCssDisplayInline);
    RoboCheckEqInt(fromInitial, 1);
    RoboCheckEqInt(CssDisplayIsInlineLevel(display), 1);

    /* A NULL style is "nothing was computed for this element", and the answer is
     * still the initial value rather than a failure. */
    display = CssDisplayOf(&fixture, NULL, &fromInitial);
    RoboCheckEqInt((int)display, (int)RoboCssDisplayInline);
    RoboCheckEqInt(fromInitial, 1);

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestDisplayNoneIsDecided(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef hidden;
    CssDisplay display;
    const CssComputedValue *value;

    CssFixture();
    CssInheritBegin(&fixture);
    hidden = CssHostAdd(CssInheritHost(&fixture), "div", "hidden", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#hidden { display: none; width: 100px; height: 100px }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    CssDisplayComputeOf(&fixture, hidden, style);

    display = CssDisplayOf(&fixture, style, NULL);
    RoboCheckEqInt((int)display, (int)RoboCssDisplayNone);
    RoboCheckEqInt(CssDisplayIsNone(display), 1);
    RoboCheckEqInt(CssDisplayGeneratesBox(display), 0);
    CssComputedIs(style, "display", "none");
    CssComputedFlagsAre(style, "display", "declared");
    /* Not an invalid value: the kind is the display kind and no flag says the
     * entry is bad. A `none` that were recorded as a mistake would be dropped by
     * a consumer that skips invalid entries, which is the same as not hiding. */
    value = CssComputedStyleComputedByName(style,
                                                    CssSliceFromText("display"));
    RoboCheckEqInt(value->kind, (int)RoboCssComputedValueDisplay);
    RoboCheckEqInt(CssComputedStyleHasInvalid(style), 0);
    RoboCheckEqInt(CssComputedStyleHasFlag(
                          style, CssSliceFromText("display"),
                          RoboCssComputedFlagInvalid),
                      0);
    /* The other properties of the same element are untouched by it: hiding is not
     * a geometry, and `width` is still the length an author wrote. */
    CssComputedIs(style, "width", "100px");
    CssComputedIs(style, "height", "100px");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * Non-inheritance, proved from both sides.
 *
 * The parent's box is removed and the child's is not, so a build that inherited
 * `display` would hide a child whose author never mentioned it — the single most
 * visible way to be wrong about this property. An inherited property is declared
 * beside it so the test cannot pass by the child having *no* style at all.
 */
static void TestDisplayNonInheritance(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parentStyle = NULL;
    CssComputedStyle *childStyle = NULL;
    CssElementRef parent;
    CssElementRef child;

    CssFixture();
    CssInheritBegin(&fixture);
    parent = CssHostAdd(CssInheritHost(&fixture), "div", "outer", NULL);
    child = CssHostAdd(CssInheritHost(&fixture), "p", "inner", NULL);
    CssHostSetParent(CssInheritHost(&fixture), child, parent);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#outer { display: block; color: #336699 }");
    RoboCheckEqInt(CssComputedStyleCreate(&parentStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&childStyle, NULL), RoboOk);
    CssDisplayComputeOf(&fixture, parent, parentStyle);
    CssDisplayComputeOf(&fixture, child, childStyle);

    CssComputedIs(parentStyle, "display", "block");
    /* The child declared nothing, and there is no entry for a non-inherited
     * property — the parent's box was not handed down, and the query door then
     * answers with the property's initial value rather than the parent's. */
    CssComputedIs(childStyle, "display", "<absent>");
    RoboCheckEqInt(CssDisplayOf(&fixture, childStyle, NULL),
                      (int)RoboCssDisplayInline);
    /* While `color`, which does inherit, arrives from the parent in the same
     * walk — so the test cannot pass by the child having no style at all. */
    CssComputedIs(childStyle, "color", "#336699");
    CssComputedFlagsAre(childStyle, "color", "inherited");

    CssComputedStyleDestroy(childStyle);
    CssComputedStyleDestroy(parentStyle);
    CssInheritEnd(&fixture);
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
static void TestDisplayWideKeywords(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parentStyle = NULL;
    CssComputedStyle *inheritStyle = NULL;
    CssComputedStyle *initialStyle = NULL;
    CssComputedStyle *unsetStyle = NULL;
    CssElementRef parent;
    CssElementRef takesInherit;
    CssElementRef takesInitial;
    CssElementRef takesUnset;

    CssFixture();
    CssInheritBegin(&fixture);
    parent = CssHostAdd(CssInheritHost(&fixture), "div", "outer", NULL);
    takesInherit = CssHostAdd(CssInheritHost(&fixture), "p", "inherit", NULL);
    takesInitial = CssHostAdd(CssInheritHost(&fixture), "p", "initial", NULL);
    takesUnset = CssHostAdd(CssInheritHost(&fixture), "p", "unset", NULL);
    CssHostSetParent(CssInheritHost(&fixture), takesInherit, parent);
    CssHostSetParent(CssInheritHost(&fixture), takesInitial, parent);
    CssHostSetParent(CssInheritHost(&fixture), takesUnset, parent);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#outer { display: block }"
                      "#inherit { display: inherit }"
                      "#initial { display: initial }"
                      "#unset { display: unset }");
    RoboCheckEqInt(CssComputedStyleCreate(&parentStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&inheritStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&initialStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&unsetStyle, NULL), RoboOk);
    CssDisplayComputeOf(&fixture, parent, parentStyle);
    CssDisplayComputeOf(&fixture, takesInherit, inheritStyle);
    CssDisplayComputeOf(&fixture, takesInitial, initialStyle);
    CssDisplayComputeOf(&fixture, takesUnset, unsetStyle);

    CssComputedIs(parentStyle, "display", "block");
    CssComputedIs(inheritStyle, "display", "block");
    CssComputedFlagsAre(inheritStyle, "display", "inherited");
    CssComputedIs(initialStyle, "display", "inline");
    CssComputedFlagsAre(initialStyle, "display", "default");
    CssComputedIs(unsetStyle, "display", "inline");
    CssComputedFlagsAre(unsetStyle, "display", "default");
    /* And the typed door agrees with the text, on the value that came from the
     * parent rather than from a declaration. */
    RoboCheckEqInt(CssDisplayOf(&fixture, inheritStyle, NULL),
                      (int)RoboCssDisplayBlock);
    RoboCheckEqInt(CssDisplayOf(&fixture, initialStyle, NULL),
                      (int)RoboCssDisplayInline);
    RoboCheckEqInt(CssDisplayOf(&fixture, unsetStyle, NULL),
                      (int)RoboCssDisplayInline);

    CssComputedStyleDestroy(unsetStyle);
    CssComputedStyleDestroy(initialStyle);
    CssComputedStyleDestroy(inheritStyle);
    CssComputedStyleDestroy(parentStyle);
    CssInheritEnd(&fixture);
}

/**
 * The cascade's three keys, and the inline attribute, on `display`.
 *
 * Not a re-test of the cascade: it is the statement that `display` participates
 * in it like any other property, and that a losing declaration is *dropped*
 * rather than being carried as an unresolved entry. An implementation that gave
 * the display shape its own precedence would show up here as the wrong winner.
 */
static void TestDisplayCascade(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef panel;
    CssElementRef important;
    CssElementRef inlineStyle;

    CssFixture();
    CssInheritBegin(&fixture);
    panel = CssHostAdd(CssInheritHost(&fixture), "div", "panel", "box");
    important = CssHostAdd(CssInheritHost(&fixture), "div", "important", NULL);
    inlineStyle = CssHostAdd(CssInheritHost(&fixture), "div", "inline", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      /* An id selector outranks a type selector, whichever came
                       * first and whatever order the two are written in. */
                      "div { display: block }"
                      "#panel { display: inline-block }"
                      /* `!important` outranks a later normal declaration. */
                      "#important { display: none !important }"
                      "#important { display: block }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    CssDisplayComputeOf(&fixture, panel, style);
    CssComputedIs(style, "display", "inline-block");
    CssComputedFlagsAre(style, "display", "declared");

    CssDisplayComputeOf(&fixture, important, style);
    CssComputedIs(style, "display", "none");

    /* An inline `style` attribute is the strongest normal level, and it reaches
     * the display shape through the same grammar as a sheet's declaration. */
    {
        static CssInlineStub stub;

        CssInheritInline(&fixture, &stub, "display: inline-block");
    }
    CssDisplayComputeOf(&fixture, inlineStyle, style);
    CssComputedIs(style, "display", "inline-block");
    CssComputedFlagsAre(style, "display", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestDisplayInvalidDeclaration(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef element;
    CssComputedValue queried = RoboCssCleared;
    int fromInitial = -1;

    CssFixture();
    CssInheritBegin(&fixture);
    element = CssHostAdd(CssInheritHost(&fixture), "div", "odd", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "#odd { display: bogus }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    CssDisplayComputeOf(&fixture, element, style);

    /* The entry is there, it is the declaration that won, and it says the value
     * means nothing for this property. */
    CssComputedIs(style, "display", "not a display value");
    CssComputedFlagsAre(style, "display", "declared+invalid");
    RoboCheckEqInt(CssComputedStyleHasInvalid(style), 1);
    RoboCheckEqInt(CssComputedStyleIncompleteCount(style) >= 1u, 1);

    /* The query door hands that refusal on rather than laundering it into a
     * display: the value it returns is INVALID, and `fromInitial` is 0 because
     * the answer came from the style and not from the property's default. */
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          CssInheritContext(&fixture), style,
                          CssSliceFromText("display"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(queried.kind, (int)RoboCssComputedValueInvalid);
    RoboCheckEqInt(fromInitial, 0);
    RoboCheckEqInt(CssComputedValueDisplay(&queried),
                      (int)RoboCssDisplayNone);
    CssComputedValueDispose(&queried);

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestDisplayRobust(void)
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

    CssFixture();
    for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;
        size_t declarationIndex;

        /* The grammar returns RoboOk whenever it produced a result, recovering
         * from what it could not use — so the assertions are about that result
         * rather than about the parse. */
        RoboCheckEqInt(CssParseDeclarations(values[index], &list, &error), RoboOk);
        for (declarationIndex = 0u;
             declarationIndex < CssDeclarationListCount(&list);
             declarationIndex++) {
            const CssDeclaration *declaration =
                CssDeclarationListAt(&list, declarationIndex);
            CssComputedValue computed = RoboCssCleared;
            char text[RoboCssComputedValueTextMax];
            size_t length = 0u;

            RoboCheckEqInt(CssComputedValueCompute(
                                  RoboCssPropertyComputationDisplay,
                                  CssDeclarationValue(declaration), &computed,
                                  NULL),
                              RoboOk);
            /* Whatever it decided, it is a decision: a known kind, and a
             * rendering that fits. */
            RoboCheckEqInt(CssComputedValueKindIsKnown(computed.kind), 1);
            RoboCheckEqInt(CssComputedValueWrite(&computed, text,
                                                           sizeof(text), &length, NULL),
                              RoboOk);
            /* A DISPLAY value is always one of the four, and the module agrees
             * it is one — a shape that could produce an enumerator the
             * vocabulary does not know would be a value nothing can classify. */
            if (computed.kind == RoboCssComputedValueDisplay) {
                RoboCheckEqInt(CssDisplayIsKnown(computed.display), 1);
                RoboCheckEqInt(CssComputedValueDisplay(&computed),
                                  (int)computed.display);
            }
            CssComputedValueDispose(&computed);
        }
        CssDeclarationListDispose(&list);
    }
    /* A value that is none of the kinds, asked for as a display, is refused with
     * the answer rather than a failure. */
    {
        CssComputedValue value = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueCompute(
                              (CssPropertyComputation)99, NULL, &value, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssComputedValueMakeDisplay(
                              &value, (CssDisplay)99, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssComputedValueMakeDisplay(NULL,
                                                              RoboCssDisplayBlock,
                                                              NULL),
                          RoboInvalidArgument);
        /* The accessor on a record that holds something else answers NONE rather
         * than reading an uninitialised field. */
        RoboCheckEqInt(CssComputedValueDisplay(&value),
                          (int)RoboCssDisplayNone);
        RoboCheckEqInt(CssComputedValueDisplay(NULL),
                          (int)RoboCssDisplayNone);
        CssComputedValueDispose(&value);
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
static void TestDisplayDeterminism(void)
{
    static const char *const ids[] = { "a", "b", "c", "d" };
    static const char *const keywords[] = { "none", "block", "inline", "inline-block" };
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef refs[4];
    size_t index;
    size_t pass;

    CssFixture();
    CssInheritBegin(&fixture);
    for (index = 0u; index < 4u; index++) {
        refs[index] = CssHostAdd(CssInheritHost(&fixture), "div", ids[index], NULL);
    }
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#a { display: none }"
                      "#b { display: block }"
                      "#c { display: inline }"
                      "#d { display: inline-block }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    for (pass = 0u; pass < 2u; pass++) {
        for (index = 0u; index < 4u; index++) {
            /* The second pass walks the same four backwards. */
            size_t at = (pass == 0u) ? index : (3u - index);

            CssDisplayComputeOf(&fixture, refs[at], style);
            CssComputedIs(style, "display", keywords[at]);
        }
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssDisplay(void);

void TestRegisterCssDisplay(void)
{
    TestRegister("css.display.vocabulary", TestDisplayVocabulary);
    TestRegister("css.display.outer_inner", TestDisplayOuterInner);
    TestRegister("css.display.classification", TestDisplayClassification);
    TestRegister("css.display.parsing", TestDisplayParsing);
    TestRegister("css.display.serialization", TestDisplaySerialization);
    TestRegister("css.display.computed.shape", TestDisplayComputedShape);
    TestRegister("css.display.computed.invalid", TestDisplayComputedInvalid);
    TestRegister("css.display.computed.reference", TestDisplayComputedReference);
    TestRegister("css.display.computed.style_query",
                       TestDisplayComputedStyleQuery);
    TestRegister("css.display.none", TestDisplayNoneIsDecided);
    TestRegister("css.display.non_inheritance", TestDisplayNonInheritance);
    TestRegister("css.display.keywords", TestDisplayWideKeywords);
    TestRegister("css.display.cascade", TestDisplayCascade);
    TestRegister("css.display.invalid_declaration", TestDisplayInvalidDeclaration);
    TestRegister("css.display.robust", TestDisplayRobust);
    TestRegister("css.display.determinism", TestDisplayDeterminism);
}
