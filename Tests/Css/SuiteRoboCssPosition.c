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
 * enumerators had drifted would still answer `IsKnown` for all five and would
 * only show up as a value that printed as another one's keyword. Walking the set
 * in enumerator order and reading every name back is what makes that visible.
 */
static void TestPositionVocabulary(void)
{
    static const char *const keywords[] = { "static", "relative", "absolute", "fixed",
                                            "sticky" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssPositioningCount, 5);
    RoboCheckEqInt((int)CssPositioningCount(), 5);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssPositioning position = (CssPositioning)index;

        RoboCheckEqInt(CssPositioningIsKnown(position), 1);
        RoboCheckStrEq(CssPositioningName(position), keywords[index]);
        RoboCheckEqInt(CssPositioningFromText(keywords[index]), (int)position);
    }
    /* The enumerator past the end and a value nothing defined answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    RoboCheckEqInt(CssPositioningIsKnown(
                          (CssPositioning)RoboCssPositioningCount),
                      0);
    RoboCheckEqInt(CssPositioningIsKnown((CssPositioning)99), 0);
    RoboCheckStrEq(CssPositioningName((CssPositioning)99), "");
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
static void TestPositionFlow(void)
{
    static const struct {
        CssPositioning position;
        int inFlow;
        int outOfFlow;
    } rows[] = {
        { RoboCssPositioningStatic,   1, 0 },
        { RoboCssPositioningRelative, 1, 0 },
        { RoboCssPositioningAbsolute, 0, 1 },
        { RoboCssPositioningFixed,    0, 1 },
        { RoboCssPositioningSticky,   1, 0 }
    };
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        RoboCheckEqInt(CssPositioningIsInFlow(rows[index].position),
                          rows[index].inFlow);
        RoboCheckEqInt(CssPositioningIsOutOfFlow(rows[index].position),
                          rows[index].outOfFlow);
    }
    /* An unknown enumerator is *not* out of flow: "this box is somewhere else" is
     * a claim a value nothing defined cannot support, and the two predicates are
     * therefore not each other's negation. */
    RoboCheckEqInt(CssPositioningIsInFlow((CssPositioning)99), 0);
    RoboCheckEqInt(CssPositioningIsOutOfFlow((CssPositioning)99), 0);
}

/**
 * The positioned column, and that `static` alone is false.
 *
 * This is the fact that does two jobs, and both are worth naming here because
 * they are the reason it is a column: an offset applies only to a positioned box
 * (`top` on a `static` element is ignored rather than honoured), and a positioned
 * box is what an absolutely positioned descendant resolves its offsets against.
 */
static void TestPositionPositioned(void)
{
    CssFixture();
    RoboCheckEqInt(CssPositioningIsPositioned(RoboCssPositioningStatic), 0);
    RoboCheckEqInt(CssPositioningIsPositioned(RoboCssPositioningRelative), 1);
    RoboCheckEqInt(CssPositioningIsPositioned(RoboCssPositioningAbsolute), 1);
    RoboCheckEqInt(CssPositioningIsPositioned(RoboCssPositioningFixed), 1);
    RoboCheckEqInt(CssPositioningIsPositioned(RoboCssPositioningSticky), 1);
    RoboCheckEqInt(CssPositioningIsPositioned((CssPositioning)99), 0);
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
static void TestPositionClassification(void)
{
    size_t collisions = 0u;
    int sawRelativeSticky = 0;
    int sawAbsoluteFixed = 0;
    int sawUnexpected = 0;
    size_t a;
    size_t b;

    CssFixture();
    for (a = 0u; a < (size_t)RoboCssPositioningCount; a++) {
        for (b = a + 1u; b < (size_t)RoboCssPositioningCount; b++) {
            CssPositioning left = (CssPositioning)a;
            CssPositioning right = (CssPositioning)b;

            if (CssPositioningIsInFlow(left) !=
                    CssPositioningIsInFlow(right) ||
                CssPositioningIsOutOfFlow(left) !=
                    CssPositioningIsOutOfFlow(right) ||
                CssPositioningIsPositioned(left) !=
                    CssPositioningIsPositioned(right)) {
                continue;
            }
            collisions++;
            if (left == RoboCssPositioningRelative &&
                right == RoboCssPositioningSticky) {
                sawRelativeSticky = 1;
            } else if (left == RoboCssPositioningAbsolute &&
                       right == RoboCssPositioningFixed) {
                sawAbsoluteFixed = 1;
            } else {
                sawUnexpected = 1;
            }
        }
    }
    RoboCheckEqInt((int)collisions, 2);
    RoboCheckEqInt(sawRelativeSticky, 1);
    RoboCheckEqInt(sawAbsoluteFixed, 1);
    RoboCheckEqInt(sawUnexpected, 0);
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
static void TestPositionParsing(void)
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

    CssFixture();
    for (index = 0u; index < sizeof(known) / sizeof(known[0]); index++) {
        CssSlice slice = CssSliceFromText(known[index]);
        char lower[32];
        size_t at;

        for (at = 0u; at < strlen(known[index]) && at + 1u < sizeof(lower); at++) {
            char c = known[index][at];

            lower[at] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        }
        lower[at] = '\0';
        RoboCheckEqInt(CssPositioningSliceIsKnown(slice), 1);
        RoboCheckEqInt(
            CssPositioningIsKnown(CssPositioningFromSlice(slice)), 1);
        /* Case-insensitivity is not an accident of the comparison: the *name*
         * comes back canonical however the identifier was written. */
        RoboCheckStrEq(CssPositioningName(CssPositioningFromSlice(slice)),
                          lower);
    }

    for (index = 0u; index < sizeof(unknown) / sizeof(unknown[0]); index++) {
        CssSlice slice = CssSliceFromText(unknown[index]);

        RoboCheckEqInt(CssPositioningSliceIsKnown(slice), 0);
        /* And the value door answers STATIC, which is why a caller has to ask the
         * predicate — the result alone cannot distinguish `static` from `banana`. */
        RoboCheckEqInt(CssPositioningFromSlice(slice),
                          (int)RoboCssPositioningStatic);
    }
    /* A prefix is not a value and neither is the property name itself. */
    RoboCheckEqInt(CssPositioningSliceIsKnown(CssSliceFromText("fi")),
                      0);
    RoboCheckEqInt(CssPositioningFromText(NULL),
                      (int)RoboCssPositioningStatic);
    RoboCheckEqInt(CssPositioningSliceIsKnown(CssSliceFromText("")), 0);
}

/** Serialization: one canonical keyword per value, and refusals that say why. */
static void TestPositionSerialization(void)
{
    char buffer[RoboCssPositioningTextMax];
    size_t length = 0u;
    size_t index;

    CssFixture();
    for (index = 0u; index < (size_t)RoboCssPositioningCount; index++) {
        CssPositioning position = (CssPositioning)index;

        RoboCheckEqInt(CssPositioningWrite(position, buffer, sizeof(buffer),
                                                 &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, CssPositioningName(position));
        RoboCheckEqInt((int)length, (int)strlen(CssPositioningName(position)));
        /* The keyword round-trips, which is what makes a computed position
         * debuggable from its text. */
        RoboCheckEqInt(CssPositioningFromText(buffer), (int)position);
    }
    /* A short buffer is a refusal, and it says how much room it needed rather
     * than truncating the keyword into a different one. */
    {
        char tiny[4];
        size_t needed = 0u;

        RoboCheckEqInt(CssPositioningWrite(RoboCssPositioningAbsolute, tiny,
                                                  sizeof(tiny), &needed, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(tiny, "");
        RoboCheckEqInt((int)needed, 8);
    }
    /* A value nothing defined is an argument error, not an empty write. */
    {
        char text[RoboCssPositioningTextMax];
        size_t written = 5u;

        RoboCheckEqInt(CssPositioningWrite((CssPositioning)99, text,
                                                  sizeof(text), &written, NULL),
                          RoboInvalidArgument);
        RoboCheckStrEq(text, "");
        RoboCheckEqInt((int)written, 0);
    }
    RoboCheckEqInt(CssPositioningWrite(RoboCssPositioningStatic, NULL,
                                              sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPositioningWrite(RoboCssPositioningStatic, buffer,
                                              sizeof(buffer), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssPositioningWrite(RoboCssPositioningStatic, buffer, 0u,
                                              &length, NULL),
                      RoboInvalidArgument);
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
static void TestPositionComputedShape(void)
{
    static const char *const keywords[] = { "static", "relative", "absolute", "fixed",
                                            "sticky" };
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        char declaration[32];
        char expected[32];

        (void)snprintf(declaration, sizeof(declaration), "p: %s", keywords[index]);
        CssComputeEquals(RoboCssPropertyComputationPosition, declaration,
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
            CssComputeEquals(RoboCssPropertyComputationPosition, declaration,
                               expected);
        }
    }
    /* Whitespace around the value and around the colon is the tokenizer's, and all
     * three spellings are the same declaration. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p:absolute", "absolute");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p:   sticky   ", "sticky");
    CssComputeEquals(RoboCssPropertyComputationPosition, "P: Relative\n", "relative");

    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPosition,
                                       "p: absolute"),
                      "position");
    /* Not the generic keyword shape, even though the text is one word: the two are
     * different representations of different things. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationKeyword,
                                       "p: absolute"),
                      "keyword");

    /* The shape's own name is what a diagnostic prints for it. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationPosition),
                      "position");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationPosition),
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
        CssPropertyRegistry *registry = NULL;
        const CssPropertyDefinition *definition;

        RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                          RoboOk);
        definition = CssPropertyRegistryFind(registry, "position");
        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              (int)RoboCssPropertyComputationPosition);
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
            RoboCheckStrEq(definition->initialText, "static");
            RoboCheckEqInt(CssPositioningFromText(definition->initialText),
                              (int)RoboCssPositioningStatic);
            RoboCheckEqInt(CssPositioningSliceIsKnown(
                                  CssSliceFromText(definition->initialText)),
                              1);
        }
        CssPropertyRegistryDestroy(registry);
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
static void TestPositionComputedInvalid(void)
{
    CssFixture();

    /* An identifier, and not one of the five. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: banana",
                       "not a position value");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: run-in",
                       "not a position value");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: centre",
                       "not a position value");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: sticky-",
                       "not a position value");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: statics",
                       "not a position value");
    /* The spelling with an underscore, which is how a keyword reads in code and
     * not how CSS spells it. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: relative_",
                       "not a position value");

    /* Not an identifier at all. Four spellings, and each is a token the grammar
     * produced that this property has no use for. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: 20px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: 10",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: #fff",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: 10%",
                       "not an identifier");
    /* A string is one token and is still not an identifier: `"static"` is seven
     * characters of which four are quotes. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: \"static\"",
                       "not an identifier");
    /* A function is a component *container* rather than a token, so it is refused
     * as not one component — which is a different sentence from "not an
     * identifier" and the right one for `rgb(0, 0, 0)`. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: rgb(0, 0, 0)",
                       "not a single position keyword");

    /* Two keywords is not a keyword, and the multi-keyword display syntax this
     * phase's sibling refuses is refused here for the same reason. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: static relative",
                       "not a single position keyword");
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: absolute fixed",
                       "not a single position keyword");
    /* Nothing at all: the declaration was not one component. */
    CssComputeEquals(RoboCssPropertyComputationPosition, "p:",
                       "not a single position keyword");


    /* And the shape is *asked*, not guessed at: a value that is not a position is
     * never a POSITION computed value, whatever kind it ends up as. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPosition,
                                       "p: banana"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPosition,
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
static void TestPositionComputedReference(void)
{
    char text[RoboCssComputedValueTextMax];
    uint32_t unresolved = 0u;

    CssFixture();
    CssComputeText(RoboCssPropertyComputationPosition, "p: var(--p)", text,
                     sizeof(text), &unresolved);
    RoboCheckStrEq(text, "var(--p)");
    RoboCheckEqInt((int)unresolved, 1);
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPosition,
                                       "p: var(--p)"),
                      "preserved");

    /* A reference is as far as it goes: `env()` is the same shape of answer, and
     * an unknown function is a mistake rather than a reference. */
    CssComputeText(RoboCssPropertyComputationPosition, "p: env(--p)", text,
                     sizeof(text), &unresolved);
    RoboCheckStrEq(text, "env(--p)");
    RoboCheckEqInt((int)unresolved, 1);
    CssComputeEquals(RoboCssPropertyComputationPosition, "p: nonsense(--p)",
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
 * names are read from `CssBoxSideName`.
 *
 * The shape is the box model's EDGE_OFFSET, and that is a decision worth asserting
 * rather than assuming: it is what makes `top: 25%` stay a percentage and
 * `top: auto` stay a keyword, and what makes `top: min-content` a mistake.
 */
static void TestPositionInsetShape(void)
{
    size_t side;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBoxSideCount, 4);
    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        const char *name = CssBoxSideName((CssBoxSide)side);
        char declaration[64];

        /* `auto` — the initial value, and not zero. */
        (void)snprintf(declaration, sizeof(declaration), "p: auto");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, declaration,
                           "auto");
        /* A bare zero is a length, and it is *not* `auto`: the difference is the
         * whole reason `top: 0` pins a box and `top: auto` does not. */
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: 0", "0px");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: 10px", "10px");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: -12px",
                           "-12px");
        /* A percentage stays a percentage: only layout knows what it is a
         * percentage of, and the containing block for an inset is not the one a
         * width would use. */
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: 25%", "25%");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: 50%", "50%");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: 12.5%",
                           "12.5%");
        /* A contextual length keeps its unit, and an absolute one converts. */
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: 2em", "2em");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: 1in", "96px");

        /* The insets are offsets, so `auto` is in the vocabulary and nothing else
         * a length-shaped property accepts is. */
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: min-content",
                           "not a length, a percentage or auto");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: normal",
                           "not a length, a percentage or auto");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: red",
                           "not a length, a percentage or auto");
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: thin",
                           "not a length, a percentage or auto");
        /* The name is the side's, and it is spelled the way CSS spells it. */
        RoboCheckStrEq(name, side == 0u   ? "top"
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
static void TestPositionInsetInitial(void)
{
    CssPropertyRegistry *registry = NULL;
    size_t side;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                      RoboOk);
    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        const char *name = CssBoxSideName((CssBoxSide)side);
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, name);

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(definition->computation,
                          (int)RoboCssPropertyComputationEdgeOffset);
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckStrEq(definition->initialText, "auto");
        /* Through the property's own shape: `auto` is a *keyword* here, and a
         * shape that turned it into `0px` would be the defect this test exists
         * for. */
        CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "p: auto",
                           "auto");
        {
            char declaration[32];

            (void)snprintf(declaration, sizeof(declaration), "p: %s",
                           definition->initialText);
            CssComputeEquals(RoboCssPropertyComputationEdgeOffset, declaration,
                               "auto");
        }
    }
    /* A fifth side does not exist, and the vocabulary says so rather than
     * answering with the first row. */
    RoboCheckEqInt(CssBoxSideIsKnown((CssBoxSide)4), 0);
    /* The box vocabulary answers `unknown` where the display and positioning ones
     * answer the empty string. Asserted as it is rather than as one might wish:
     * this phase consumes the side vocabulary and does not get to restate it. */
    RoboCheckStrEq(CssBoxSideName((CssBoxSide)4), "unknown");
    CssPropertyRegistryDestroy(registry);
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
static void TestPositionZIndexShape(void)
{
    CssFixture();

    /* `auto`, as a keyword rather than as zero. */
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: auto", "auto");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationZIndex,
                                       "z: auto"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: AUTO", "auto");

    /* Whole numbers, positive, zero and negative. */
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 0", "0");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 1", "1");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 10", "10");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: -1", "-1");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: -100", "-100");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: +7", "7");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z:   42  ", "42");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationZIndex, "z: -1"),
                      "number");

    /* Negative zero is zero, and it prints as zero: the module's rule, not a
     * coincidence of the shape. */
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: -0", "0");

    /* Three refusals, three sentences. */
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 1.5",
                       "not a whole number");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 0.5",
                       "not a whole number");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 1.0",
                       "a whole number must be written as an integer");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 1e2",
                       "a whole number must be written as an integer");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 10px",
                       "not a whole number or auto");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 10%",
                       "not a whole number or auto");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 1foo",
                       "not a whole number or auto");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: hello",
                       "not a whole number or auto");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: none",
                       "not a whole number or auto");
    /* A magnitude no runtime can hold as an integer is refused rather than
     * wrapped, and it is the unit module's bound that refuses it. */
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 1e999",
                       "not a finite number");
    /* Two components is not a number. */
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z: 1 2",
                       "not a single z-index component");
    CssComputeEquals(RoboCssPropertyComputationZIndex, "z:",
                       "not a single z-index component");
    /* A reference is carried, exactly as it is for the two closed vocabularies. */
    {
        char text[RoboCssComputedValueTextMax];
        uint32_t unresolved = 0u;

        CssComputeText(RoboCssPropertyComputationZIndex, "z: var(--z)", text,
                         sizeof(text), &unresolved);
        RoboCheckStrEq(text, "var(--z)");
        RoboCheckEqInt((int)unresolved, 1);
    }

    /* The shape's own name, and the registry row that uses it. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationZIndex),
                      "z-index");
    {
        CssPropertyRegistry *registry = NULL;
        const CssPropertyDefinition *definition;

        RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                          RoboOk);
        definition = CssPropertyRegistryFind(registry, "z-index");
        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              (int)RoboCssPropertyComputationZIndex);
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
            RoboCheckStrEq(definition->initialText, "auto");
        }
        CssPropertyRegistryDestroy(registry);
    }
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void CssPositionComputeOf(CssInheritFixture *fixture,
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
 * The typed `position` of an element, and where it came from.
 *
 * The door a later phase reads, and it is the *context's* rather than the style's
 * for the reason 9.12 recorded: `position` does not inherit, so an element that
 * declared nothing has **no entry** — 9.7 materialises initial values for the
 * inherited-by-default set only — and "what is this element's position" is a
 * question only something holding the registry can answer.
 */
static CssPositioning CssPositionOf(CssInheritFixture *fixture,
                                           const CssComputedStyle *style,
                                           int *outFromInitial)
{
    CssComputedValue value = RoboCssCleared;
    CssPositioning position;

    if (outFromInitial) {
        *outFromInitial = -1;
    }
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          CssInheritContext(fixture), style,
                          CssSliceFromText("position"), &value,
                          outFromInitial, NULL),
                      RoboOk);
    /* The answer is a position *value* and not the keyword's letters, which is
     * what the shape change buys a consumer. */
    RoboCheckEqInt(value.kind, (int)RoboCssComputedValuePosition);
    position = CssComputedValuePosition(&value);
    CssComputedValueDispose(&value);
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
static void TestPositionComputedStyleQuery(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef placed;
    CssElementRef plain;
    CssPositioning position;
    int fromInitial = -1;
    size_t side;

    CssFixture();
    CssInheritBegin(&fixture);
    placed = CssHostAdd(CssInheritHost(&fixture), "div", "placed", NULL);
    plain = CssHostAdd(CssInheritHost(&fixture), "span", "plain", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#placed { position: absolute; top: 25%; right: auto;"
                      " bottom: 10px; left: 2em; z-index: -1 }"
                      "#plain { color: red }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* Declared: every one of the six arrives typed, and the two that carry an
     * enumerator and a whole number are read through their own accessors. */
    CssPositionComputeOf(&fixture, placed, style);
    position = CssPositionOf(&fixture, style, &fromInitial);
    RoboCheckEqInt((int)position, (int)RoboCssPositioningAbsolute);
    RoboCheckEqInt(fromInitial, 0);
    RoboCheckEqInt(CssPositioningIsPositioned(position), 1);
    RoboCheckEqInt(CssPositioningIsOutOfFlow(position), 1);
    CssComputedIs(style, "position", "absolute");
    CssComputedFlagsAre(style, "position", "declared");
    CssComputedIs(style, "top", "25%");
    CssComputedIs(style, "right", "auto");
    CssComputedIs(style, "bottom", "10px");
    CssComputedIs(style, "left", "2em");
    CssComputedIs(style, "z-index", "-1");
    /* The same fact through the record's own accessors, so the two doors cannot
     * disagree about what a typed value is. */
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                style, CssSliceFromText("position"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValuePosition);
        RoboCheckEqInt(CssComputedValuePosition(value),
                          (int)RoboCssPositioningAbsolute);
    }
    /* A percentage inset is a PERCENTAGE and an `auto` one is a KEYWORD: the two
     * are different kinds, which is what stops a later phase reading `auto` as a
     * zero-length offset. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationEdgeOffset,
                                       "p: 25%"),
                      "percentage");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationEdgeOffset,
                                       "p: auto"),
                      "keyword");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationZIndex,
                                       "z: -1"),
                      "number");

    /* Nothing declared: no entry for any of the six, and every one of them still
     * answers — with the property's own initial value, marked as such. */
    CssPositionComputeOf(&fixture, plain, style);
    CssComputedIs(style, "position", "<absent>");
    position = CssPositionOf(&fixture, style, &fromInitial);
    RoboCheckEqInt((int)position, (int)RoboCssPositioningStatic);
    RoboCheckEqInt(fromInitial, 1);
    /* Static is a value and not a failure: the box simply is not positioned, and
     * an offset on it does not apply. */
    RoboCheckEqInt(CssPositioningIsPositioned(position), 0);
    RoboCheckEqInt(CssPositioningIsInFlow(position), 1);
    RoboCheckEqInt(CssPositioningSliceIsKnown(
                          CssSliceFromText("static")),
                      1);
    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        const char *name = CssBoxSideName((CssBoxSide)side);
        CssComputedValue value = RoboCssCleared;
        int cameFromInitial = -1;

        CssComputedIs(style, name, "<absent>");
        RoboCheckEqInt(CssStyleContextQueryComputedValue(
                              CssInheritContext(&fixture), style,
                              CssSliceFromText(name), &value,
                              &cameFromInitial, NULL),
                          RoboOk);
        RoboCheckEqInt(cameFromInitial, 1);
        /* A *keyword* and not a length: an `auto` inset that arrived as `0px`
         * would pin every box its author left to the containing block's corner. */
        RoboCheckEqInt(value.kind, (int)RoboCssComputedValueKeyword);
        {
            char text[RoboCssComputedValueTextMax];
            size_t length = 0u;

            RoboCheckEqInt(CssComputedValueWrite(&value, text,
                                                           sizeof(text), &length, NULL),
                              RoboOk);
            RoboCheckStrEq(text, "auto");
        }
        CssComputedValueDispose(&value);
    }
    {
        CssComputedValue value = RoboCssCleared;
        int cameFromInitial = -1;

        RoboCheckEqInt(CssStyleContextQueryComputedValue(
                              CssInheritContext(&fixture), style,
                              CssSliceFromText("z-index"), &value,
                              &cameFromInitial, NULL),
                          RoboOk);
        RoboCheckEqInt(cameFromInitial, 1);
        RoboCheckEqInt(value.kind, (int)RoboCssComputedValueKeyword);
        CssComputedValueDispose(&value);
    }
    /* While `color`, which does inherit, is present in the same walk — so the
     * test cannot pass by the element having no style at all. */
    CssComputedIs(style, "color", "#ff0000");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestPositionNonInheritance(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parentStyle = NULL;
    CssComputedStyle *childStyle = NULL;
    CssElementRef parent;
    CssElementRef child;
    size_t side;

    CssFixture();
    CssInheritBegin(&fixture);
    parent = CssHostAdd(CssInheritHost(&fixture), "div", "outer", NULL);
    child = CssHostAdd(CssInheritHost(&fixture), "p", "inner", NULL);
    CssHostSetParent(CssInheritHost(&fixture), child, parent);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#outer { position: absolute; top: 10%; right: 1px;"
                      " bottom: 2em; left: 3%; z-index: 5; color: #336699 }");
    RoboCheckEqInt(CssComputedStyleCreate(&parentStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&childStyle, NULL), RoboOk);
    CssPositionComputeOf(&fixture, parent, parentStyle);
    CssPositionComputeOf(&fixture, child, childStyle);

    CssComputedIs(parentStyle, "position", "absolute");
    CssComputedIs(parentStyle, "top", "10%");
    CssComputedIs(parentStyle, "z-index", "5");
    /* The child has no entry for any of the six — a non-inherited property the
     * element does not mention is not materialised (9.7) — and the query door then
     * answers each with the property's own initial value rather than the parent's. */
    CssComputedIs(childStyle, "position", "<absent>");
    RoboCheckEqInt(CssPositionOf(&fixture, childStyle, NULL),
                      (int)RoboCssPositioningStatic);
    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        const char *name = CssBoxSideName((CssBoxSide)side);

        CssComputedIs(childStyle, name, "<absent>");
    }
    CssComputedIs(childStyle, "z-index", "<absent>");
    /* While `color`, which does inherit, arrives from the parent in the same
     * walk. */
    CssComputedIs(childStyle, "color", "#336699");
    CssComputedFlagsAre(childStyle, "color", "inherited");

    CssComputedStyleDestroy(childStyle);
    CssComputedStyleDestroy(parentStyle);
    CssInheritEnd(&fixture);
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
static void TestPositionWideKeywords(void)
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
    size_t side;

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
                      "#outer { position: fixed; top: 12px; right: 1%;"
                      " bottom: 2em; left: 3px; z-index: 9 }"
                      "#inherit { position: inherit; top: inherit; right: inherit;"
                      " bottom: inherit; left: inherit; z-index: inherit }"
                      "#initial { position: initial; top: initial; right: initial;"
                      " bottom: initial; left: initial; z-index: initial }"
                      "#unset { position: unset; top: unset; right: unset;"
                      " bottom: unset; left: unset; z-index: unset }");
    RoboCheckEqInt(CssComputedStyleCreate(&parentStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&inheritStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&initialStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&unsetStyle, NULL), RoboOk);
    CssPositionComputeOf(&fixture, parent, parentStyle);
    CssPositionComputeOf(&fixture, takesInherit, inheritStyle);
    CssPositionComputeOf(&fixture, takesInitial, initialStyle);
    CssPositionComputeOf(&fixture, takesUnset, unsetStyle);

    CssComputedIs(parentStyle, "position", "fixed");
    /* `inherit`: the parent's computed value, on all six. */
    CssComputedIs(inheritStyle, "position", "fixed");
    CssComputedFlagsAre(inheritStyle, "position", "inherited");
    CssComputedIs(inheritStyle, "top", "12px");
    CssComputedIs(inheritStyle, "right", "1%");
    CssComputedIs(inheritStyle, "bottom", "2em");
    CssComputedIs(inheritStyle, "left", "3px");
    CssComputedIs(inheritStyle, "z-index", "9");
    /* `initial`: the property's own initial value, which for an inset is `auto`
     * and not the parent's `12px`. */
    CssComputedIs(initialStyle, "position", "static");
    CssComputedFlagsAre(initialStyle, "position", "default");
    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        CssComputedIs(initialStyle,
                        CssBoxSideName((CssBoxSide)side), "auto");
        CssComputedIs(unsetStyle,
                        CssBoxSideName((CssBoxSide)side), "auto");
    }
    CssComputedIs(initialStyle, "z-index", "auto");
    /* `unset` on a non-inherited property *is* `initial`, which is the assertion
     * that would fail if the property were classified as inherited. */
    CssComputedIs(unsetStyle, "position", "static");
    CssComputedFlagsAre(unsetStyle, "position", "default");
    CssComputedIs(unsetStyle, "z-index", "auto");
    /* And the typed door agrees with the text on all three, on the values that
     * came from the parent and from the registry rather than from a declaration. */
    RoboCheckEqInt(CssPositionOf(&fixture, inheritStyle, NULL),
                      (int)RoboCssPositioningFixed);
    RoboCheckEqInt(CssPositionOf(&fixture, initialStyle, NULL),
                      (int)RoboCssPositioningStatic);
    RoboCheckEqInt(CssPositionOf(&fixture, unsetStyle, NULL),
                      (int)RoboCssPositioningStatic);

    CssComputedStyleDestroy(unsetStyle);
    CssComputedStyleDestroy(initialStyle);
    CssComputedStyleDestroy(inheritStyle);
    CssComputedStyleDestroy(parentStyle);
    CssInheritEnd(&fixture);
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
static void TestPositionCascade(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef panel;
    CssElementRef important;
    CssElementRef sourceOrder;
    CssElementRef inlineStyle;

    CssFixture();
    CssInheritBegin(&fixture);
    panel = CssHostAdd(CssInheritHost(&fixture), "div", "panel", "box");
    important = CssHostAdd(CssInheritHost(&fixture), "div", "important", NULL);
    sourceOrder = CssHostAdd(CssInheritHost(&fixture), "div", "order", NULL);
    inlineStyle = CssHostAdd(CssInheritHost(&fixture), "div", "inline", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
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
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    CssPositionComputeOf(&fixture, panel, style);
    CssComputedIs(style, "position", "relative");
    CssComputedIs(style, "top", "10px");
    CssComputedFlagsAre(style, "position", "declared");

    CssPositionComputeOf(&fixture, important, style);
    CssComputedIs(style, "position", "fixed");
    CssComputedIs(style, "top", "2px");

    CssPositionComputeOf(&fixture, sourceOrder, style);
    CssComputedIs(style, "position", "relative");
    CssComputedIs(style, "top", "30px");

    /* An inline `style` attribute is the strongest normal level, and it reaches
     * both shapes through the same grammar as a sheet's declaration. */
    {
        static CssInlineStub stub;

        CssInheritInline(&fixture, &stub,
                           "position: absolute; top: auto; z-index: 3");
    }
    CssPositionComputeOf(&fixture, inlineStyle, style);
    CssComputedIs(style, "position", "absolute");
    CssComputedIs(style, "top", "auto");
    CssComputedIs(style, "z-index", "3");
    CssComputedFlagsAre(style, "position", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestPositionInvalidDeclaration(void)
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
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#odd { position: banana; top: nope; z-index: 1.5 }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    CssPositionComputeOf(&fixture, element, style);

    /* Every entry is there, it is the declaration that won, and it says the value
     * means nothing for this property. */
    CssComputedIs(style, "position", "not a position value");
    CssComputedFlagsAre(style, "position", "declared+invalid");
    CssComputedIs(style, "top", "not a length, a percentage or auto");
    CssComputedFlagsAre(style, "top", "declared+invalid");
    CssComputedIs(style, "z-index", "not a whole number");
    CssComputedFlagsAre(style, "z-index", "declared+invalid");
    RoboCheckEqInt(CssComputedStyleHasInvalid(style), 1);
    RoboCheckEqInt(CssComputedStyleIncompleteCount(style) >= 1u, 1);

    /* The query door hands the refusal on rather than laundering it into a
     * well-formed default, which is the whole point of reporting it: a caller can
     * see that the declaration was there and was wrong. */
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          CssInheritContext(&fixture), style,
                          CssSliceFromText("position"), &queried,
                          &fromInitial, NULL),
                      RoboOk);
    RoboCheckEqInt(fromInitial, 0);
    RoboCheckEqInt(queried.kind, (int)RoboCssComputedValueInvalid);
    CssComputedValueDispose(&queried);

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestPositionRobust(void)
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
    static const CssPropertyComputation shapes[] = {
        RoboCssPropertyComputationPosition,
        RoboCssPropertyComputationEdgeOffset,
        RoboCssPropertyComputationZIndex
    };
    size_t index;
    size_t shape;

    CssFixture();
    for (shape = 0u; shape < sizeof(shapes) / sizeof(shapes[0]); shape++) {
        for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
            CssDeclarationList list = RoboCssCleared;
            CssError error = RoboCssCleared;
            size_t declarationIndex;

            /* The grammar returns RoboOk whenever it produced a result,
             * recovering from what it could not use — so the assertions are about
             * that result rather than about the parse. */
            RoboCheckEqInt(CssParseDeclarations(values[index], &list, &error),
                              RoboOk);
            for (declarationIndex = 0u;
                 declarationIndex < CssDeclarationListCount(&list);
                 declarationIndex++) {
                const CssDeclaration *declaration =
                    CssDeclarationListAt(&list, declarationIndex);
                CssComputedValue computed = RoboCssCleared;
                char text[RoboCssComputedValueTextMax];
                size_t length = 0u;

                RoboCheckEqInt(CssComputedValueCompute(
                                      shapes[shape],
                                      CssDeclarationValue(declaration),
                                      &computed, NULL),
                                  RoboOk);
                /* Whatever it decided, it is a decision: a known kind, and a
                 * rendering that fits. */
                RoboCheckEqInt(CssComputedValueKindIsKnown(computed.kind),
                                  1);
                RoboCheckEqInt(CssComputedValueWrite(
                                      &computed, text, sizeof(text), &length, NULL),
                                  RoboOk);
                /* A POSITION value is always one the vocabulary knows — a shape
                 * that could produce an enumerator nothing can classify would be
                 * a value a layout pass could only fail on. */
                if (computed.kind == RoboCssComputedValuePosition) {
                    RoboCheckEqInt(CssPositioningIsKnown(computed.position), 1);
                    RoboCheckEqInt(CssComputedValuePosition(&computed),
                                      (int)computed.position);
                }
                /* And a NUMBER produced by the z-index shape is always a whole
                 * number: `isInteger` is set for exactly that reason. */
                if (shapes[shape] == RoboCssPropertyComputationZIndex &&
                    computed.kind == RoboCssComputedValueNumber) {
                    RoboCheckEqInt(computed.isInteger, 1);
                }
                CssComputedValueDispose(&computed);
            }
            CssDeclarationListDispose(&list);
        }
    }

    /* The value door's own contract, and the accessor's behaviour on a record
     * that holds something else. */
    {
        CssComputedValue value = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueCompute(
                              (CssPropertyComputation)200, NULL, &value, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssComputedValueMakePosition(
                              &value, (CssPositioning)99, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssComputedValueMakePosition(NULL,
                                                               RoboCssPositioningFixed,
                                                               NULL),
                          RoboInvalidArgument);
        /* The accessor on a record that holds something else answers STATIC rather
         * than reading an uninitialised field — and STATIC is safe *because* the
         * kind check is the caller's, which is why it is documented as such. */
        RoboCheckEqInt(CssComputedValuePosition(&value),
                          (int)RoboCssPositioningStatic);
        RoboCheckEqInt(CssComputedValuePosition(NULL),
                          (int)RoboCssPositioningStatic);
        CssComputedValueDispose(&value);
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
static void TestPositionDeterminism(void)
{
    static const char *const ids[] = { "a", "b", "c", "d", "e" };
    static const char *const keywords[] = { "static", "relative", "absolute", "fixed",
                                            "sticky" };
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef refs[5];
    size_t index;
    size_t pass;

    CssFixture();
    CssInheritBegin(&fixture);
    for (index = 0u; index < 5u; index++) {
        refs[index] = CssHostAdd(CssInheritHost(&fixture), "div", ids[index], NULL);
    }
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#a { position: static;   top: auto; left: auto; z-index: auto }"
                      "#b { position: relative; top: 1px;  left: 2px;  z-index: 1 }"
                      "#c { position: absolute; top: 25%;  left: 50%;  z-index: 0 }"
                      "#d { position: fixed;    top: -1px; left: 2em;  z-index: -1 }"
                      "#e { position: sticky;   top: 3%;   left: 4px;  z-index: 10 }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    for (pass = 0u; pass < 2u; pass++) {
        for (index = 0u; index < 5u; index++) {
            /* The second pass walks the same five backwards. */
            size_t at = (pass == 0u) ? index : (4u - index);

            CssPositionComputeOf(&fixture, refs[at], style);
            CssComputedIs(style, "position", keywords[at]);
            RoboCheckEqInt(CssPositionOf(&fixture, style, NULL),
                              (int)at);
            /* The offsets and the level are read on every element in both passes,
             * which is what makes this a statement about the family and not only
             * about the enumerator. */
            CssComputedIs(style, "top", at == 0u   ? "auto"
                                          : at == 1u ? "1px"
                                          : at == 2u ? "25%"
                                          : at == 3u ? "-1px"
                                                     : "3%");
            CssComputedIs(style, "z-index", at == 0u   ? "auto"
                                               : at == 1u ? "1"
                                               : at == 2u ? "0"
                                               : at == 3u ? "-1"
                                                          : "10");
        }
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssPosition(void);

void TestRegisterCssPosition(void)
{
    TestRegister("css.position.vocabulary", TestPositionVocabulary);
    TestRegister("css.position.flow", TestPositionFlow);
    TestRegister("css.position.positioned", TestPositionPositioned);
    TestRegister("css.position.classification", TestPositionClassification);
    TestRegister("css.position.parsing", TestPositionParsing);
    TestRegister("css.position.serialization", TestPositionSerialization);
    TestRegister("css.position.computed.shape", TestPositionComputedShape);
    TestRegister("css.position.computed.invalid", TestPositionComputedInvalid);
    TestRegister("css.position.computed.reference",
                       TestPositionComputedReference);
    TestRegister("css.position.inset.shape", TestPositionInsetShape);
    TestRegister("css.position.inset.initial", TestPositionInsetInitial);
    TestRegister("css.position.z_index.shape", TestPositionZIndexShape);
    TestRegister("css.position.computed.style_query",
                       TestPositionComputedStyleQuery);
    TestRegister("css.position.non_inheritance", TestPositionNonInheritance);
    TestRegister("css.position.keywords", TestPositionWideKeywords);
    TestRegister("css.position.cascade", TestPositionCascade);
    TestRegister("css.position.invalid_declaration",
                       TestPositionInvalidDeclaration);
    TestRegister("css.position.robust", TestPositionRobust);
    TestRegister("css.position.determinism", TestPositionDeterminism);
}
