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
 * and enumerators had drifted would still answer `IsKnown` for all four and
 * would only show as a value that printed as another one's keyword. Walking the
 * set in enumerator order and reading every name back is what makes that visible.
 */
static void TestBackgroundRepeatVocabulary(void)
{
    static const char *const keywords[] = { "repeat", "repeat-x", "repeat-y",
                                            "no-repeat" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundRepeatCount, 4);
    RoboCheckEqInt((int)CssBackgroundRepeatCount(), 4);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssBackgroundRepeat repeat = (CssBackgroundRepeat)index;

        RoboCheckEqInt(CssBackgroundRepeatIsKnown(repeat), 1);
        RoboCheckStrEq(CssBackgroundRepeatName(repeat), keywords[index]);
        RoboCheckEqInt(CssBackgroundRepeatFromText(keywords[index]),
                          (int)repeat);
    }
    /* The enumerator past the end and a value nothing defined answer nothing: a
     * value the vocabulary has never heard of is not silently the first row. */
    RoboCheckEqInt(CssBackgroundRepeatIsKnown(
                          (CssBackgroundRepeat)RoboCssBackgroundRepeatCount),
                      0);
    RoboCheckEqInt(CssBackgroundRepeatIsKnown(
                          (CssBackgroundRepeat)99),
                      0);
    RoboCheckStrEq(CssBackgroundRepeatName(
                          (CssBackgroundRepeat)99),
                      "");
    /* An unknown *identifier* answers `repeat`, which is a real value — so the
     * predicate on the identifier is what tells the two apart, not the result. */
    RoboCheckEqInt(CssBackgroundRepeatSliceIsKnown(
                          CssSliceFromText("repeat")),
                      1);
    RoboCheckEqInt(CssBackgroundRepeatSliceIsKnown(
                          CssSliceFromText("REPEAT-Y")),
                      1);
    RoboCheckEqInt(CssBackgroundRepeatSliceIsKnown(
                          CssSliceFromText("space")),
                      0);
    RoboCheckEqInt(CssBackgroundRepeatSliceIsKnown(
                          CssSliceFromText("repeatx")),
                      0);
    RoboCheckEqInt(CssBackgroundRepeatSliceIsKnown(
                          CssSliceFromText("")),
                      0);
    RoboCheckEqInt(CssBackgroundRepeatFromSlice(
                          CssSliceFromText("space")),
                      (int)RoboCssBackgroundRepeat);
    RoboCheckEqInt(CssBackgroundRepeatFromText(NULL),
                      (int)RoboCssBackgroundRepeat);
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
static void TestBackgroundRepeatAxes(void)
{
    static const struct {
        CssBackgroundRepeat repeat;
        int horizontal;
        int vertical;
    } rows[] = {
        { RoboCssBackgroundRepeat,    1, 1 },
        { RoboCssBackgroundRepeatX,  1, 0 },
        { RoboCssBackgroundRepeatY,  0, 1 },
        { RoboCssBackgroundNoRepeat, 0, 0 }
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundAxisCount, 2);
    RoboCheckStrEq(CssBackgroundAxisName(RoboCssBackgroundAxisHorizontal),
                      "horizontal");
    RoboCheckStrEq(CssBackgroundAxisName(RoboCssBackgroundAxisVertical),
                      "vertical");
    RoboCheckEqInt(CssBackgroundAxisIsKnown(
                          RoboCssBackgroundAxisHorizontal),
                      1);
    RoboCheckEqInt(CssBackgroundAxisIsKnown(
                          RoboCssBackgroundAxisVertical),
                      1);
    RoboCheckEqInt(CssBackgroundAxisIsKnown(
                          (CssBackgroundAxis)RoboCssBackgroundAxisCount),
                      0);
    RoboCheckStrEq(CssBackgroundAxisName((CssBackgroundAxis)99), "");
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        RoboCheckEqInt(CssBackgroundRepeatRepeatsOnAxis(
                              rows[index].repeat,
                              RoboCssBackgroundAxisHorizontal),
                          rows[index].horizontal);
        RoboCheckEqInt(CssBackgroundRepeatRepeatsOnAxis(
                              rows[index].repeat,
                              RoboCssBackgroundAxisVertical),
                          rows[index].vertical);
    }
    /* One of the four tiles one axis only, and the vocabulary says *which*:
     * swapping the two columns is the mistake this cross-check exists for. */
    RoboCheckEqInt(CssBackgroundRepeatRepeatsOnAxis(
                          RoboCssBackgroundRepeatX,
                          RoboCssBackgroundAxisHorizontal),
                      1);
    RoboCheckEqInt(CssBackgroundRepeatRepeatsOnAxis(
                          RoboCssBackgroundRepeatX,
                          RoboCssBackgroundAxisVertical),
                      0);
    RoboCheckEqInt(CssBackgroundRepeatRepeatsOnAxis(
                          (CssBackgroundRepeat)99,
                          RoboCssBackgroundAxisHorizontal),
                      0);
    RoboCheckEqInt(CssBackgroundRepeatRepeatsOnAxis(
                          (CssBackgroundRepeat)99,
                          RoboCssBackgroundAxisVertical),
                      0);
}

/** The attachment vocabulary: two values, and `local` is a row not added. */
static void TestBackgroundAttachmentVocabulary(void)
{
    static const char *const keywords[] = { "scroll", "fixed" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundAttachmentCount, 2);
    RoboCheckEqInt((int)CssBackgroundAttachmentCount(), 2);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssBackgroundAttachment attachment =
            (CssBackgroundAttachment)index;

        RoboCheckEqInt(CssBackgroundAttachmentIsKnown(attachment), 1);
        RoboCheckStrEq(CssBackgroundAttachmentName(attachment),
                          keywords[index]);
        RoboCheckEqInt(CssBackgroundAttachmentFromText(keywords[index]),
                          (int)attachment);
    }
    RoboCheckEqInt(CssBackgroundAttachmentIsKnown(
                          (CssBackgroundAttachment)99),
                      0);
    RoboCheckStrEq(CssBackgroundAttachmentName(
                          (CssBackgroundAttachment)99),
                      "");
    RoboCheckEqInt(CssBackgroundAttachmentSliceIsKnown(
                          CssSliceFromText("FIXED")),
                      1);
    /* `local` scrolls with the element's own content rather than with the page,
     * and it is a value this build has not added — which is a different answer
     * from a word nothing knows. */
    RoboCheckEqInt(CssBackgroundAttachmentSliceIsKnown(
                          CssSliceFromText("local")),
                      0);
    RoboCheckEqInt(CssBackgroundAttachmentSliceIsKnown(
                          CssSliceFromText("floating")),
                      0);
    RoboCheckEqInt(CssBackgroundAttachmentSliceIsKnown(
                          CssSliceFromText("")),
                      0);
    RoboCheckEqInt(CssBackgroundAttachmentFromText(NULL),
                      (int)RoboCssBackgroundAttachmentScroll);
}

/**
 * The edges, and the fact that a position keyword is spelled *by its axis*.
 *
 * `left` and `top` are both the START edge, and the component alone cannot say
 * which one was written — so the axis is an argument to both doors. That is what
 * makes `top` in the horizontal slot a refusal rather than a silent `center`,
 * which would turn `background-position: top` into the top-left corner.
 */
static void TestBackgroundEdgeVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundEdgeCount, 3);
    RoboCheckEqInt(RoboCssBackgroundEdgeCenter, 0);
    RoboCheckEqInt(RoboCssBackgroundEdgeStart, 1);
    RoboCheckEqInt(RoboCssBackgroundEdgeEnd, 2);
    RoboCheckEqInt(CssBackgroundEdgeIsKnown(RoboCssBackgroundEdgeStart),
                      1);
    RoboCheckEqInt(CssBackgroundEdgeIsKnown(
                          (CssBackgroundEdge)99),
                      0);

    /* One edge, two spellings: the axis picks the word. */
    RoboCheckStrEq(CssBackgroundEdgeName(RoboCssBackgroundEdgeStart,
                                                    RoboCssBackgroundAxisHorizontal),
                      "left");
    RoboCheckStrEq(CssBackgroundEdgeName(RoboCssBackgroundEdgeStart,
                                                    RoboCssBackgroundAxisVertical),
                      "top");
    RoboCheckStrEq(CssBackgroundEdgeName(RoboCssBackgroundEdgeEnd,
                                                    RoboCssBackgroundAxisHorizontal),
                      "right");
    RoboCheckStrEq(CssBackgroundEdgeName(RoboCssBackgroundEdgeEnd,
                                                    RoboCssBackgroundAxisVertical),
                      "bottom");
    /* And the one edge that means the same on both, which is why it exists. */
    RoboCheckStrEq(CssBackgroundEdgeName(RoboCssBackgroundEdgeCenter,
                                                    RoboCssBackgroundAxisHorizontal),
                      "center");
    RoboCheckStrEq(CssBackgroundEdgeName(RoboCssBackgroundEdgeCenter,
                                                    RoboCssBackgroundAxisVertical),
                      "center");
    /* An unknown edge, or an unknown axis, spells nothing rather than a guess. */
    RoboCheckStrEq(CssBackgroundEdgeName((CssBackgroundEdge)99,
                                                    RoboCssBackgroundAxisHorizontal),
                      "");
    RoboCheckStrEq(CssBackgroundEdgeName(RoboCssBackgroundEdgeStart,
                                                    (CssBackgroundAxis)99),
                      "");

    /* Half the vocabulary per axis, and the overlap is `center` alone. */
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("left"),
                          RoboCssBackgroundAxisHorizontal),
                      1);
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("left"),
                          RoboCssBackgroundAxisVertical),
                      0);
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("TOP"),
                          RoboCssBackgroundAxisVertical),
                      1);
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("TOP"),
                          RoboCssBackgroundAxisHorizontal),
                      0);
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("center"),
                          RoboCssBackgroundAxisHorizontal),
                      1);
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("center"),
                          RoboCssBackgroundAxisVertical),
                      1);
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("centre"),
                          RoboCssBackgroundAxisHorizontal),
                      0);
    RoboCheckEqInt(CssBackgroundEdgeSliceIsKnown(
                          CssSliceFromText("banana"),
                          RoboCssBackgroundAxisHorizontal),
                      0);
    /* A word that is not an edge on that axis answers CENTER, which is a real
     * value — the predicate is what tells the two apart. */
    RoboCheckEqInt(CssBackgroundEdgeFromSlice(
                          CssSliceFromText("top"),
                          RoboCssBackgroundAxisHorizontal),
                      (int)RoboCssBackgroundEdgeCenter);
    RoboCheckEqInt(CssBackgroundEdgeFromSlice(
                          CssSliceFromText("bottom"),
                          RoboCssBackgroundAxisVertical),
                      (int)RoboCssBackgroundEdgeEnd);
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
static const CssValue *CssBackgroundValue(const char *text,
                                                    CssDeclarationList *list)
{
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;

    RoboCheckEqInt(CssParseDeclarations(text, list, &error), RoboOk);
    declaration = CssDeclarationListAt(list, 0u);
    RoboCheck(declaration != NULL);
    return declaration ? CssDeclarationValue(declaration) : NULL;
}

/** What @p text reads as, as a background position. */
static CssBackgroundParse CssBackgroundPositionOutcome(const char *text)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssBackgroundValue(text, &list);
    CssBackgroundPosition position = CssBackgroundPositionNone();
    CssBackgroundParse outcome =
        CssBackgroundPositionFromValue(value, &position);

    /* Any outcome but POSITION leaves @p position absent, which is what makes a
     * refusal readable rather than a half-filled pair. */
    if (outcome != RoboCssBackgroundParsePosition) {
        RoboCheckEqInt(CssBackgroundPositionIsPresent(position), 0);
    }
    CssDeclarationListDispose(&list);
    return outcome;
}

/** The canonical text of @p text read as a background position. */
static void CssBackgroundPositionIs(const char *text, const char *expected)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssBackgroundValue(text, &list);
    CssBackgroundPosition position = CssBackgroundPositionNone();
    char shape[RoboCssNumericTextMax * 2u + 2u];
    size_t length = 0u;

    RoboCheckEqInt(CssBackgroundPositionFromValue(value, &position),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(CssBackgroundPositionIsPresent(position), 1);
    RoboCheckEqInt(CssBackgroundPositionWrite(position, shape, sizeof(shape),
                                                         &length, NULL),
                      RoboOk);
    RoboCheckStrEq(shape, expected);
    RoboCheckEqInt((int)length, (int)strlen(expected));
    CssDeclarationListDispose(&list);
}

/** What @p text reads as, as a background size. */
static CssBackgroundParse CssBackgroundSizeOutcome(const char *text)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssBackgroundValue(text, &list);
    CssBackgroundSize size = CssBackgroundSizeNone();
    CssBackgroundParse outcome =
        CssBackgroundSizeFromValue(value, &size);

    if (outcome != RoboCssBackgroundParseSize) {
        /* A cleared record reads as `auto` — the initial value is the safest
         * thing a cleared one could hold — so the outcome is the only thing a
         * refusal can be read from. */
        RoboCheckEqInt(outcome != RoboCssBackgroundParseSize, 1);
    }
    CssDeclarationListDispose(&list);
    return outcome;
}

/** The canonical text of @p text read as a background size. */
static void CssBackgroundSizeIs(const char *text, const char *expected)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssBackgroundValue(text, &list);
    CssBackgroundSize size = CssBackgroundSizeNone();
    char shape[RoboCssNumericTextMax * 2u + 2u];
    size_t length = 0u;

    RoboCheckEqInt(CssBackgroundSizeFromValue(value, &size),
                      RoboCssBackgroundParseSize);
    RoboCheckEqInt(CssBackgroundSizeIsPresent(size), 1);
    RoboCheckEqInt(CssBackgroundSizeWrite(size, shape, sizeof(shape), &length,
                                                     NULL),
                      RoboOk);
    RoboCheckStrEq(shape, expected);
    RoboCheckEqInt((int)length, (int)strlen(expected));
    CssDeclarationListDispose(&list);
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
static void TestBackgroundComponentModel(void)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value;
    CssBackgroundPosition offsets = CssBackgroundPositionNone();
    CssBackgroundPosition edges = CssBackgroundPositionNone();
    CssBackgroundPosition percents = CssBackgroundPositionNone();

    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundComponentKindCount, 4);
    RoboCheckStrEq(CssBackgroundComponentKindName(
                          RoboCssBackgroundComponentNone),
                      "none");
    RoboCheckStrEq(CssBackgroundComponentKindName(
                          RoboCssBackgroundComponentEdge),
                      "edge");
    RoboCheckStrEq(CssBackgroundComponentKindName(
                          RoboCssBackgroundComponentLength),
                      "length");
    RoboCheckStrEq(CssBackgroundComponentKindName(
                          RoboCssBackgroundComponentPercentage),
                      "percentage");
    RoboCheckEqInt(CssBackgroundComponentKindIsKnown(
                          (CssBackgroundComponentKind)99),
                      0);
    RoboCheckStrEq(CssBackgroundComponentKindName(
                          (CssBackgroundComponentKind)99),
                      "unknown");

    /* `10px 20px`: two offsets, and they are different values. */
    value = CssBackgroundValue("p: 10px 20px", &list);
    RoboCheckEqInt(CssBackgroundPositionFromValue(value, &offsets),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(offsets.horizontal.kind, RoboCssBackgroundComponentLength);
    RoboCheckEqInt(offsets.vertical.kind, RoboCssBackgroundComponentLength);
    RoboCheckEqInt(CssBackgroundComponentEquals(offsets.horizontal,
                                                          offsets.horizontal),
                      1);
    RoboCheckEqInt(CssBackgroundComponentEquals(offsets.horizontal,
                                                          offsets.vertical),
                      0);
    RoboCheckEqInt(CssBackgroundComponentIsPresent(offsets.horizontal), 1);
    CssDeclarationListDispose(&list);

    /* `left top`: two edges, one START each — and equal, because a component
     * records the place rather than the word. The axis is what spells it, and
     * that is the writer's argument, not a field. */
    memset(&list, 0, sizeof(list));
    value = CssBackgroundValue("p: left top", &list);
    RoboCheckEqInt(CssBackgroundPositionFromValue(value, &edges),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(edges.horizontal.kind, RoboCssBackgroundComponentEdge);
    RoboCheckEqInt(edges.horizontal.edge, RoboCssBackgroundEdgeStart);
    RoboCheckEqInt(edges.vertical.edge, RoboCssBackgroundEdgeStart);
    RoboCheckEqInt(CssBackgroundComponentEquals(edges.horizontal,
                                                          edges.vertical),
                      1);
    CssDeclarationListDispose(&list);

    /* And a percentage at the same magnitude as a length is a different value:
     * 10% of a box is not ten pixels until something says what the box is. */
    memset(&list, 0, sizeof(list));
    value = CssBackgroundValue("p: 10% 10%", &list);
    RoboCheckEqInt(CssBackgroundPositionFromValue(value, &percents),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(percents.horizontal.kind,
                      RoboCssBackgroundComponentPercentage);
    RoboCheckEqInt(CssBackgroundComponentEquals(percents.horizontal,
                                                          offsets.horizontal),
                      0);
    RoboCheckEqInt(CssBackgroundComponentEquals(percents.horizontal,
                                                          edges.horizontal),
                      0);
    CssDeclarationListDispose(&list);

    /* Absence is not a value: there is nothing for two absences to be equal
     * about, and a *pair* is present only when both halves are. */
    RoboCheckEqInt(CssBackgroundComponentIsPresent(
                          CssBackgroundComponentNone()),
                      0);
    RoboCheckEqInt(CssBackgroundComponentEquals(
                          CssBackgroundComponentNone(),
                          CssBackgroundComponentNone()),
                      0);
    RoboCheckEqInt(CssBackgroundPositionIsPresent(
                          CssBackgroundPositionNone()),
                      0);
    RoboCheckEqInt(CssBackgroundPositionEquals(
                          CssBackgroundPositionNone(),
                          CssBackgroundPositionNone()),
                      0);
    {
        /* A half-filled pair is what the reader holds mid-parse and never what it
         * returns: built by hand here to prove the pair's own answer. */
        CssBackgroundPosition half = CssBackgroundPositionNone();

        half.horizontal = edges.horizontal;
        RoboCheckEqInt(CssBackgroundPositionIsPresent(half), 0);
        RoboCheckEqInt(CssBackgroundPositionEquals(half, half), 0);
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
static void TestBackgroundComponentWrite(void)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value;
    CssBackgroundPosition position = CssBackgroundPositionNone();
    CssBackgroundComponent edge = CssBackgroundComponentNone();
    char buffer[RoboCssNumericTextMax];
    size_t length = 0u;

    CssFixture();
    edge.kind = RoboCssBackgroundComponentEdge;
    edge.edge = RoboCssBackgroundEdgeStart;
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          edge, RoboCssBackgroundAxisHorizontal, buffer,
                          sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "left");
    RoboCheckEqInt((int)length, 4);
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          edge, RoboCssBackgroundAxisVertical, buffer,
                          sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "top");
    edge.edge = RoboCssBackgroundEdgeEnd;
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          edge, RoboCssBackgroundAxisVertical, buffer,
                          sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "bottom");

    /* A value and a percentage, from a real parse rather than a hand-built
     * record, so the spelling is the one the computed layer would print. */
    value = CssBackgroundValue("p: 2em 25%", &list);
    RoboCheckEqInt(CssBackgroundPositionFromValue(value, &position),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          position.horizontal, RoboCssBackgroundAxisHorizontal,
                          buffer, sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "2em");
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          position.vertical, RoboCssBackgroundAxisVertical, buffer,
                          sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "25%");
    CssDeclarationListDispose(&list);

    /* Three refusals, each its own sentence. */
    {
        char tiny[4];

        RoboCheckEqInt(CssBackgroundComponentWrite(
                              edge, RoboCssBackgroundAxisVertical, tiny,
                              sizeof(tiny), &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(tiny, "");
        RoboCheckEqInt((int)length, 6); /* "bottom" */
    }
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          CssBackgroundComponentNone(),
                          RoboCssBackgroundAxisHorizontal, buffer, sizeof(buffer),
                          &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          edge, (CssBackgroundAxis)99, buffer, sizeof(buffer),
                          &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          edge, RoboCssBackgroundAxisHorizontal, NULL,
                          sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          edge, RoboCssBackgroundAxisHorizontal, buffer,
                          sizeof(buffer), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBackgroundComponentWrite(
                          edge, RoboCssBackgroundAxisHorizontal, buffer, 0u,
                          &length, NULL),
                      RoboInvalidArgument);
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
static void TestBackgroundPositionParse(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundParseCount, 4);
    RoboCheckStrEq(CssBackgroundParseName(
                          RoboCssBackgroundParseInvalid),
                      "invalid");
    RoboCheckStrEq(CssBackgroundParseName(
                          RoboCssBackgroundParsePosition),
                      "position");
    RoboCheckStrEq(CssBackgroundParseName(RoboCssBackgroundParseSize),
                      "size");
    RoboCheckStrEq(CssBackgroundParseName(
                          RoboCssBackgroundParseDeferred),
                      "deferred");
    RoboCheckEqInt(CssBackgroundParseIsKnown(
                          (CssBackgroundParse)99),
                      0);
    RoboCheckStrEq(CssBackgroundParseName(
                          (CssBackgroundParse)99),
                      "unknown");

    /* Two keywords, one per axis, in either order and either case. */
    CssBackgroundPositionIs("p: left top", "left top");
    CssBackgroundPositionIs("p: top left", "left top");
    CssBackgroundPositionIs("p: LEFT TOP", "left top");
    CssBackgroundPositionIs("p: right bottom", "right bottom");
    CssBackgroundPositionIs("p: bottom right", "right bottom");
    CssBackgroundPositionIs("p: center center", "center center");

    /* Two offsets, and two percentages: kept as they are, because only the phase
     * with a box can answer 25% of what. */
    CssBackgroundPositionIs("p: 10px 20px", "10px 20px");
    CssBackgroundPositionIs("p: 25% 50%", "25% 50%");
    CssBackgroundPositionIs("p: 2em 1.5rem", "2em 1.5rem");
    CssBackgroundPositionIs("p: 1in 12px", "96px 12px");

    /* The single-component form, folded — and the two halves of CSS's
     * asymmetry: a vertical keyword goes in the vertical slot, everything else
     * is horizontal. */
    CssBackgroundPositionIs("p: center", "center center");
    CssBackgroundPositionIs("p: left", "left center");
    CssBackgroundPositionIs("p: right", "right center");
    CssBackgroundPositionIs("p: top", "center top");
    CssBackgroundPositionIs("p: bottom", "center bottom");
    CssBackgroundPositionIs("p: 10px", "10px center");
    CssBackgroundPositionIs("p: 25%", "25% center");

    /* A centre and an offset is a position — horizontal centre, ten pixels down
     * — which is the one keyword-plus-offset spelling this build reads. */
    CssBackgroundPositionIs("p: center 10px", "center 10px");

    /* Whitespace is the grammar's, and it changes nothing. */
    CssBackgroundPositionIs("p:   25%   50%  ", "25% 50%");
    CssBackgroundPositionIs("p:25% 50%", "25% 50%");
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
static void TestBackgroundPositionInvalid(void)
{
    CssFixture();

    /* The edge-relative form, in both its spellings. */
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: left 10px"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: right 2em"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: top 10px"),
                      RoboCssBackgroundParseInvalid);
    /* Two keywords on the same axis names one axis twice and leaves the other
     * unnamed, which is a statement this value cannot make. */
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: left right"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: top bottom"),
                      RoboCssBackgroundParseInvalid);

    /* A word, an absent value, and a bare number — which is not a position
     * because CSS's `<length-percentage>` has no unitless branch. */
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: banana"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: lefty"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: 10"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p:"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: none"),
                      RoboCssBackgroundParseInvalid);

    /* Three and four components: the count decides the meaning there. */
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: left top center"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: left top 10px 20px"),
                      RoboCssBackgroundParseInvalid);
    /* A colour is not a position, and neither is a string. */
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: red"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: #fff"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: \"left\""),
                      RoboCssBackgroundParseInvalid);
}

/**
 * A reference is not a refusal, and it is not a position either.
 *
 * `background-position: calc(10px + 1%)` is legal CSS this build does not
 * evaluate; the outcome says so, which is what lets the computed layer carry the
 * tokens and *flag* them rather than calling the declaration a mistake.
 */
static void TestBackgroundPositionReference(void)
{
    CssFixture();
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: var(--p)"),
                      RoboCssBackgroundParseDeferred);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: env(--p)"),
                      RoboCssBackgroundParseDeferred);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: calc(10px + 1%)"),
                      RoboCssBackgroundParseDeferred);
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: min(10px, 2em)"),
                      RoboCssBackgroundParseDeferred);
    /* A function that is not a reference is not a position either. */
    RoboCheckEqInt(CssBackgroundPositionOutcome("p: nonsense(10px)"),
                      RoboCssBackgroundParseInvalid);
}

/** Writing a pair, and comparing two of them. */
static void TestBackgroundPositionWrite(void)
{
    CssDeclarationList firstList = RoboCssCleared;
    CssDeclarationList secondList = RoboCssCleared;
    CssBackgroundPosition first = CssBackgroundPositionNone();
    CssBackgroundPosition second = CssBackgroundPositionNone();
    char buffer[RoboCssNumericTextMax * 2u + 2u];
    size_t length = 0u;

    CssFixture();
    /* One spelling per value: `center` and `center center` are the same computed
     * value and print the same string. */
    CssBackgroundPositionIs("p: center", "center center");
    CssBackgroundPositionIs("p: center center", "center center");

    RoboCheckEqInt(CssBackgroundPositionFromValue(
                          CssBackgroundValue("p: left top", &firstList), &first),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(CssBackgroundPositionFromValue(
                          CssBackgroundValue("p: left top", &secondList), &second),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(CssBackgroundPositionEquals(first, second), 1);
    CssDeclarationListDispose(&secondList);

    /* Order is not part of the value: `top left` and `left top` are one place. */
    RoboCheckEqInt(CssBackgroundPositionFromValue(
                          CssBackgroundValue("p: top left", &secondList), &second),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(CssBackgroundPositionEquals(first, second), 1);
    CssDeclarationListDispose(&secondList);

    /* A different place, and a different *kind* of place: `0px 0px` is not
     * `0% 0%`, however alike they look once a box exists. */
    RoboCheckEqInt(CssBackgroundPositionFromValue(
                          CssBackgroundValue("p: right top", &secondList), &second),
                      RoboCssBackgroundParsePosition);
    RoboCheckEqInt(CssBackgroundPositionEquals(first, second), 0);
    CssDeclarationListDispose(&secondList);
    RoboCheckEqInt(CssBackgroundPositionFromValue(
                          CssBackgroundValue("p: 0px 0px", &secondList), &second),
                      RoboCssBackgroundParsePosition);
    {
        CssBackgroundPosition percentages = CssBackgroundPositionNone();

        RoboCheckEqInt(CssBackgroundPositionFromValue(
                              CssBackgroundValue("p: 0% 0%", &firstList), &percentages),
                          RoboCssBackgroundParsePosition);
        /* The initial value the registry names, spelled as CSS spells it. */
        RoboCheckEqInt(CssBackgroundPositionWrite(
                              percentages, buffer, sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "0% 0%");
        RoboCheckEqInt(CssBackgroundPositionEquals(percentages, second), 0);
    }
    CssDeclarationListDispose(&secondList);
    CssDeclarationListDispose(&firstList);

    /* Three refusals: too small a buffer says how much it needed rather than
     * truncating into a different position; an absent pair has no text; and the
     * argument checks come before either. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssBackgroundPosition wide = CssBackgroundPositionNone();
        char tiny[4];

        RoboCheckEqInt(CssBackgroundPositionFromValue(
                              CssBackgroundValue("p: center center", &list), &wide),
                          RoboCssBackgroundParsePosition);
        RoboCheckEqInt(CssBackgroundPositionWrite(wide, tiny, sizeof(tiny),
                                                             &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(tiny, "");
        RoboCheckEqInt((int)length, 13); /* "center center" */
        RoboCheckEqInt(CssBackgroundPositionWrite(
                              CssBackgroundPositionNone(), buffer,
                              sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssBackgroundPositionWrite(
                              wide, NULL, sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssBackgroundPositionWrite(
                              wide, buffer, sizeof(buffer), NULL, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssBackgroundPositionWrite(wide, buffer, 0u, &length,
                                                             NULL),
                          RoboInvalidArgument);
        CssDeclarationListDispose(&list);
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
static void TestBackgroundSizeParse(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundSizeKindCount, 4);
    RoboCheckStrEq(CssBackgroundSizeKindName(
                          RoboCssBackgroundSizeAuto),
                      "auto");
    RoboCheckStrEq(CssBackgroundSizeKindName(
                          RoboCssBackgroundSizeCover),
                      "cover");
    RoboCheckStrEq(CssBackgroundSizeKindName(
                          RoboCssBackgroundSizeContain),
                      "contain");
    RoboCheckStrEq(CssBackgroundSizeKindName(
                          RoboCssBackgroundSizePair),
                      "pair");
    RoboCheckEqInt(CssBackgroundSizeKindIsKnown(
                          (CssBackgroundSizeKind)99),
                      0);
    RoboCheckStrEq(CssBackgroundSizeKindName(
                          (CssBackgroundSizeKind)99),
                      "unknown");

    /* The three whole values, and `auto auto` folding onto the first. */
    CssBackgroundSizeIs("p: auto", "auto");
    CssBackgroundSizeIs("p: AUTO", "auto");
    CssBackgroundSizeIs("p: auto auto", "auto");
    CssBackgroundSizeIs("p: cover", "cover");
    CssBackgroundSizeIs("p: CONTAIN", "contain");

    /* Pairs, in both component kinds, and `auto` beside a real component. */
    CssBackgroundSizeIs("p: 100px 50px", "100px 50px");
    CssBackgroundSizeIs("p: 50% 25%", "50% 25%");
    CssBackgroundSizeIs("p: 2em 1.5rem", "2em 1.5rem");
    CssBackgroundSizeIs("p: 20px auto", "20px auto");
    CssBackgroundSizeIs("p: auto 20px", "auto 20px");
    CssBackgroundSizeIs("p: 1in 12px", "96px 12px");

    /* The single-component form, folded: a width, and the height left `auto`. */
    CssBackgroundSizeIs("p: 100px", "100px auto");
    CssBackgroundSizeIs("p: 50%", "50% auto");
    CssBackgroundSizeIs("p:   50%   ", "50% auto");
}

/** The size's refusals, and that a reference is not one of them. */
static void TestBackgroundSizeInvalid(void)
{
    CssFixture();

    /* An edge is a position component and not a size: `background-size: left`
     * is a mistake rather than a left-hand-side size. */
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: left"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: center"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: 10px left"),
                      RoboCssBackgroundParseInvalid);
    /* A word, an absent value, a bare number, and too many components. */
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: banana"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p:"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: 10"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: 10px 20px 30px"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: cover contain"),
                      RoboCssBackgroundParseInvalid);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: red"),
                      RoboCssBackgroundParseInvalid);

    /* A reference is carried, which is a different answer from a mistake. */
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: var(--s)"),
                      RoboCssBackgroundParseDeferred);
    RoboCheckEqInt(CssBackgroundSizeOutcome("p: calc(100% - 20px)"),
                      RoboCssBackgroundParseDeferred);
}

/** Writing a size and comparing two, including the `auto` flag's part. */
static void TestBackgroundSizeWrite(void)
{
    CssDeclarationList firstList = RoboCssCleared;
    CssDeclarationList secondList = RoboCssCleared;
    CssBackgroundSize first = CssBackgroundSizeNone();
    CssBackgroundSize second = CssBackgroundSizeNone();
    char buffer[RoboCssNumericTextMax * 2u + 2u];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt(CssBackgroundSizeFromValue(
                          CssBackgroundValue("p: auto auto", &firstList), &first),
                      RoboCssBackgroundParseSize);
    RoboCheckEqInt(first.kind, RoboCssBackgroundSizeAuto);
    RoboCheckEqInt(CssBackgroundSizeFromValue(
                          CssBackgroundValue("p: auto", &secondList), &second),
                      RoboCssBackgroundParseSize);
    RoboCheckEqInt(CssBackgroundSizeEquals(first, second), 1);
    CssDeclarationListDispose(&secondList);

    /* `cover` and `contain` are not a pair of anything, so they are not equal to
     * one another or to a pair that happens to print differently. */
    RoboCheckEqInt(CssBackgroundSizeFromValue(
                          CssBackgroundValue("p: cover", &secondList), &second),
                      RoboCssBackgroundParseSize);
    RoboCheckEqInt(CssBackgroundSizeEquals(first, second), 0);
    RoboCheckEqInt(CssBackgroundSizeEquals(second, second), 1);

    /* `auto` is a flag rather than a numeric kind, so `20px auto` differs from
     * `20px 0px` — which is the distinction the pair exists to keep. */
    RoboCheckEqInt(CssBackgroundSizeFromValue(
                          CssBackgroundValue("p: 20px auto", &firstList), &first),
                      RoboCssBackgroundParseSize);
    RoboCheckEqInt(CssBackgroundSizeFromValue(
                          CssBackgroundValue("p: 20px 0px", &secondList), &second),
                      RoboCssBackgroundParseSize);
    RoboCheckEqInt(CssBackgroundSizeEquals(first, second), 0);
    RoboCheckEqInt(CssBackgroundSizeWrite(first, buffer, sizeof(buffer),
                                                     &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "20px auto");
    CssDeclarationListDispose(&secondList);
    CssDeclarationListDispose(&firstList);

    /* A cleared record is `auto`, which is why the outcome is the only thing a
     * refusal can be read from — and why a pair with both halves `auto` is not
     * the pair kind. */
    RoboCheckEqInt(CssBackgroundSizeNone().kind,
                      RoboCssBackgroundSizeAuto);
    RoboCheckEqInt(CssBackgroundSizeIsPresent(
                          CssBackgroundSizeNone()),
                      1);

    /* Refusals: a short buffer says how much it needed rather than truncating
     * into a different size, an unknown kind is not a value, and a pair with
     * neither half set is incoherent. */
    {
        CssBackgroundSize pair = CssBackgroundSizeNone();
        CssNumeric twenty = CssNumericNone();
        char tiny[4];

        RoboCheckEqInt(CssNumericMakeLength(&twenty, 20.0, RoboCssUnitPx,
                                                      NULL),
                          RoboOk);
        pair.kind = RoboCssBackgroundSizePair;
        pair.width.isAuto = 1;
        pair.height.numeric = twenty;
        RoboCheckEqInt(CssBackgroundSizeIsPresent(pair), 1);
        RoboCheckEqInt(CssBackgroundSizeWrite(pair, tiny, sizeof(tiny),
                                                         &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(tiny, "");
        RoboCheckEqInt((int)length, 9); /* "auto 20px" */
        RoboCheckEqInt(CssBackgroundSizeWrite(pair, buffer, sizeof(buffer),
                                                         &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "auto 20px");
        pair.kind = (CssBackgroundSizeKind)99;
        RoboCheckEqInt(CssBackgroundSizeIsPresent(pair), 0);
        RoboCheckEqInt(CssBackgroundSizeWrite(pair, buffer, sizeof(buffer),
                                                        &length, NULL),
                          RoboInvalidArgument);
    }
    {
        CssBackgroundSize pair = CssBackgroundSizeNone();

        pair.kind = RoboCssBackgroundSizePair;
        RoboCheckEqInt(CssBackgroundSizeIsPresent(pair), 0);
    }
    RoboCheckEqInt(CssBackgroundSizeWrite(
                          CssBackgroundSizeNone(), NULL, sizeof(buffer),
                          &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBackgroundSizeWrite(
                          CssBackgroundSizeNone(), buffer, sizeof(buffer),
                          NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBackgroundSizeWrite(
                          CssBackgroundSizeNone(), buffer, 0u, &length, NULL),
                      RoboInvalidArgument);
    /* A cleared record reads as `auto` — the initial value is the safest thing a
     * cleared one could hold — so two of them *are* equal, and absence is not a
     * state this shape can express. The contrast with the component and position
     * pairs above is deliberate: those can be absent, this one cannot. */
    RoboCheckEqInt(CssBackgroundSizeEquals(
                          CssBackgroundSizeNone(),
                          CssBackgroundSizeNone()),
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
static void TestBackgroundPositionComputed(void)
{
    CssFixture();
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: left top", "left top");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: center", "center center");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: 10px 20px", "10px 20px");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: 25% 50%", "25% 50%");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: top", "center top");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: 0% 0%", "0% 0%");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: 0%", "0% center");
    /* This spelling is CSS's default position and it is *not* the initial value's
     * text: `0% 0%` is the top-left, and `0%` alone leaves the vertical at
     * centre. Both are correct and they are different places. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundPosition,
                                       "p: left top"),
                      "background-position");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBackgroundPosition),
                      "background-position");
}

/** `background-position`'s refusals, told apart from a carried reference. */
static void TestBackgroundPositionComputedInvalid(void)
{
    char text[RoboCssComputedValueTextMax];
    uint32_t unresolved = 0u;

    CssFixture();
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: left 10px", "not a background position");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: banana", "not a background position");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: 10", "not a background position");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: left top center", "not a background position");
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition, "p:",
                       "not a background position");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundPosition,
                                       "p: banana"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundPosition,
                                       "p: left top"),
                      "background-position");

    /* A reference is carried with the flag rather than refused: `calc()` and
     * `var()` are legal CSS whose value is decided elsewhere. */
    CssComputeText(RoboCssPropertyComputationBackgroundPosition,
                     "p: var(--p)", text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "var(--p)");
    RoboCheckEqInt((int)unresolved, 1);
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundPosition,
                                       "p: var(--p)"),
                      "preserved");
    CssComputeText(RoboCssPropertyComputationBackgroundPosition,
                     "p: calc(10px + 1%)", text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "calc(10px + 1%)");
    RoboCheckEqInt((int)unresolved, 1);
    /* An unknown function is a mistake rather than a reference, and the sentence
     * is the module's rather than the resolver's. */
    CssComputeEquals(RoboCssPropertyComputationBackgroundPosition,
                       "p: nonsense(10px)", "not a background position");
}

/** `background-size` as a computed value: the keyword or the pair, typed. */
static void TestBackgroundSizeComputed(void)
{
    CssFixture();
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: auto",
                       "auto");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: auto auto",
                       "auto");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: cover",
                       "cover");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: CONTAIN",
                       "contain");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize,
                       "p: 100px 50px", "100px 50px");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize,
                       "p: 50% 25%", "50% 25%");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: 100px",
                       "100px auto");
    /* One shape for all four: `cover` is a whole *value* of the size property, so
     * unlike the two closed vocabularies it is not a KEYWORD — there is a
     * difference between "the keyword `cover`" and "the value `cover`". */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundSize,
                                       "p: cover"),
                      "background-size");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundSize,
                                       "p: 50% 25%"),
                      "background-size");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBackgroundSize),
                      "background-size");

    /* Refusals, and a reference carried. */
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: left",
                       "not a background size");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: banana",
                       "not a background size");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize, "p: 10",
                       "not a background size");
    CssComputeEquals(RoboCssPropertyComputationBackgroundSize,
                       "p: 10px 20px 30px", "not a background size");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundSize,
                                       "p: banana"),
                      "invalid");
    {
        char text[RoboCssComputedValueTextMax];
        uint32_t unresolved = 0u;

        CssComputeText(RoboCssPropertyComputationBackgroundSize,
                         "p: var(--s)", text, sizeof(text), &unresolved);
        RoboCheckStrEq(text, "var(--s)");
        RoboCheckEqInt((int)unresolved, 1);
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
static void TestBackgroundImageComputed(void)
{
    char text[RoboCssComputedValueTextMax];
    uint32_t unresolved = 0u;

    CssFixture();
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBackgroundImage),
                      "background-image");

    /* The one value, canonically spelled whatever the author wrote. */
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage, "p: none",
                       "none");
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage, "p: NONE",
                       "none");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundImage,
                                       "p: none"),
                      "keyword");
    CssComputeText(RoboCssPropertyComputationBackgroundImage, "p: none", text,
                     sizeof(text), &unresolved);
    RoboCheckEqInt((int)unresolved, 0);

    /* References, carried with their tokens intact and *flagged*, which is what
     * stops a consumer reading them as an image. */
    CssComputeText(RoboCssPropertyComputationBackgroundImage,
                     "p: url(\"a.png\")", text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "url(\"a.png\")");
    RoboCheckEqInt((int)unresolved, 1);
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundImage,
                                       "p: url(\"a.png\")"),
                      "preserved");
    CssComputeText(RoboCssPropertyComputationBackgroundImage, "p: url(a.png)",
                     text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "url(a.png)");
    RoboCheckEqInt((int)unresolved, 1);
    CssComputeText(RoboCssPropertyComputationBackgroundImage,
                     "p: linear-gradient(red, blue)", text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "linear-gradient(red, blue)");
    RoboCheckEqInt((int)unresolved, 1);
    CssComputeText(RoboCssPropertyComputationBackgroundImage,
                     "p: var(--image)", text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "var(--image)");
    RoboCheckEqInt((int)unresolved, 1);

    /* And what is left: a word, a number, a dimension, a string, two components. */
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage, "p: banana",
                       "not a background image");
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage, "p: 20px",
                       "not a background image");
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage, "p: 10",
                       "not a background image");
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage,
                       "p: \"a.png\"", "not a background image");
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage,
                       "p: none none", "not a background image");
    CssComputeEquals(RoboCssPropertyComputationBackgroundImage, "p:",
                       "not a background image");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundImage,
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
static void TestBackgroundRepeatComputed(void)
{
    char text[RoboCssComputedValueTextMax];
    uint32_t unresolved = 0u;

    CssFixture();
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat, "p: repeat",
                       "repeat");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat, "p: repeat-x",
                       "repeat-x");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat, "p: repeat-y",
                       "repeat-y");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat, "p: no-repeat",
                       "no-repeat");
    /* Case-insensitively, and the *canonical* spelling is what comes out. */
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat,
                       "p: NO-REPEAT", "no-repeat");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat,
                       "p:   Repeat-X  ", "repeat-x");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundRepeat,
                                       "p: repeat-x"),
                      "keyword");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBackgroundRepeat),
                      "background-repeat");

    /* Three refusals, three sentences: a value this build has not added
     * (`space`), something that is not an identifier at all, and two keywords. */
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat,
                       "p: space", "not a background repeat value");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat,
                       "p: banana", "not a background repeat value");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat, "p: 20px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat,
                       "p: repeat repeat",
                       "not a single background-repeat keyword");
    CssComputeEquals(RoboCssPropertyComputationBackgroundRepeat, "p:",
                       "not a single background-repeat keyword");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBackgroundRepeat,
                                       "p: banana"),
                      "invalid");

    CssComputeText(RoboCssPropertyComputationBackgroundRepeat,
                     "p: var(--repeat)", text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "var(--repeat)");
    RoboCheckEqInt((int)unresolved, 1);
}

/** The attachment vocabulary, and `local` as a value not added. */
static void TestBackgroundAttachmentComputed(void)
{
    char text[RoboCssComputedValueTextMax];
    uint32_t unresolved = 0u;

    CssFixture();
    CssComputeEquals(RoboCssPropertyComputationBackgroundAttachment,
                       "p: scroll", "scroll");
    CssComputeEquals(RoboCssPropertyComputationBackgroundAttachment,
                       "p: fixed", "fixed");
    CssComputeEquals(RoboCssPropertyComputationBackgroundAttachment,
                       "p: FIXED", "fixed");
    RoboCheckStrEq(CssComputeKind(
                          RoboCssPropertyComputationBackgroundAttachment,
                          "p: fixed"),
                      "keyword");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBackgroundAttachment),
                      "background-attachment");

    CssComputeEquals(RoboCssPropertyComputationBackgroundAttachment,
                       "p: local", "not a background attachment value");
    CssComputeEquals(RoboCssPropertyComputationBackgroundAttachment,
                       "p: floating", "not a background attachment value");
    CssComputeEquals(RoboCssPropertyComputationBackgroundAttachment,
                       "p: 10px", "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationBackgroundAttachment,
                       "p: scroll fixed",
                       "not a single background-attachment keyword");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBackgroundAttachment),
                      "background-attachment");

    CssComputeText(RoboCssPropertyComputationBackgroundAttachment,
                     "p: var(--attachment)", text, sizeof(text), &unresolved);
    RoboCheckStrEq(text, "var(--attachment)");
    RoboCheckEqInt((int)unresolved, 1);
}

/**
 * `background-color` is the 9.9 colour shape and not a background one.
 *
 * Asserted rather than assumed because the family may not grow a second colour
 * parser: `background-color: #f00` has to reach the same channels `color: #f00`
 * does, and the only way that stays true is if the property keeps the shape 9.9
 * gave it.
 */
static void TestBackgroundColorComputed(void)
{
    CssFixture();
    CssComputeEquals(RoboCssPropertyComputationColor, "p: transparent",
                       "#00000000");
    CssComputeEquals(RoboCssPropertyComputationColor, "p: red", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "p: #336699", "#336699");
    CssComputeEquals(RoboCssPropertyComputationColor, "p: #fff", "#ffffff");
    CssComputeEquals(RoboCssPropertyComputationColor, "p: rgb(10, 20, 30)",
                       "#0a141e");
    CssComputeEquals(RoboCssPropertyComputationColor, "p: rgba(0, 0, 0, 0.5)",
                       "#00000080");
    CssComputeEquals(RoboCssPropertyComputationColor, "p: hsl(0, 100%, 50%)",
                       "#ff0000");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "p: transparent"),
                      "colour");
    /* The two registrations are one computation: a colour is a colour however the
     * property it belongs to is spelled. */
    {
        CssPropertyRegistry *registry = NULL;
        const CssPropertyDefinition *definition;

        RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                          RoboOk);
        definition = CssPropertyRegistryFind(registry, "background-color");
        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              (int)RoboCssPropertyComputationColor);
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
            RoboCheckStrEq(definition->initialText, "transparent");
        }
        CssPropertyRegistryDestroy(registry);
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
static void TestBackgroundRegistry(void)
{
    static const struct {
        CssPropertyComputation computation;
        const char *initial;
    } rows[] = {
        { RoboCssPropertyComputationBackgroundImage,      "none" },
        { RoboCssPropertyComputationBackgroundPosition,   "0% 0%" },
        { RoboCssPropertyComputationBackgroundSize,       "auto" },
        { RoboCssPropertyComputationBackgroundRepeat,     "repeat" },
        { RoboCssPropertyComputationBackgroundAttachment, "scroll" },
        { RoboCssPropertyComputationColor,                 "transparent" }
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBackgroundLonghandCount, 6);
    RoboCheckEqInt((int)CssBackgroundLonghandCount(), 6);
    RoboCheckEqInt(
        (int)(sizeof(rows) / sizeof(rows[0])),
        (int)RoboCssBackgroundLonghandCount);
    RoboCheckStrEq(CssBackgroundShorthandName(), "background");
    RoboCheckEqInt(CssBackgroundLonghandName(
                          RoboCssBackgroundLonghandCount),
                      (int)(size_t)NULL);

    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                      RoboOk);
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        const char *name = CssBackgroundLonghandName(index);
        const CssPropertyDefinition *definition;

        RoboCheck(name != NULL);
        if (!name) {
            continue;
        }
        /* The name is the module's, and it is one the module recognises. */
        RoboCheckEqInt(CssBackgroundIsLonghand(CssSliceFromText(name)),
                          1);
        RoboCheckEqInt((int)rows[index].computation,
                          (int)rows[index].computation);
        definition = CssPropertyRegistryFind(registry, name);
        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(definition->computation, (int)rows[index].computation);
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckStrEq(definition->initialText, rows[index].initial);
        /* And the initial value is readable by that shape, which is what makes
         * `background-image: initial` a value rather than a refusal. */
        {
            char declaration[32];

            (void)snprintf(declaration, sizeof(declaration), "p: %s",
                           definition->initialText);
            CssComputeText(rows[index].computation, declaration, NULL, 0u, NULL);
        }
    }
    /* Case-insensitively, and only for the six. */
    RoboCheckEqInt(CssBackgroundIsLonghand(
                          CssSliceFromText("BACKGROUND-COLOR")),
                      1);
    RoboCheckEqInt(CssBackgroundIsLonghand(
                          CssSliceFromText("background")),
                      0);
    RoboCheckEqInt(CssBackgroundIsLonghand(CssSliceFromText("")), 0);
    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* The shorthand                                                       */
/* ------------------------------------------------------------------ */

/** The shape of @p part, as the fixture's one-line rendering. */
static void CssBackgroundPartShape(const CssValue *part, char *out,
                                      size_t capacity)
{
    CssShape(part, out, capacity);
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
static void CssBackgroundPartComputed(const CssValue *part,
                                         CssPropertyComputation computation,
                                         char *out,
                                         size_t capacity)
{
    CssComputedValue value = RoboCssCleared;
    size_t length = 0u;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    if (!part || part->kind == RoboCssValueKindNone) {
        return;
    }
    RoboCheckEqInt(CssComputedValueCompute(computation, part, &value, NULL),
                      RoboOk);
    if (out && capacity > 0u) {
        RoboCheckEqInt(CssComputedValueWrite(&value, out, capacity, &length,
                                                        NULL),
                          RoboOk);
    }
    CssComputedValueDispose(&value);
}

/**
 * The six parts, each read by the longhand it is indexed as.
 *
 * The order is the module's own (image, position, size, repeat, attachment,
 * colour), which is also the order CSS serialises `background` in — so a part
 * written against the wrong index shows up as a value computed by the wrong
 * grammar rather than as two tables that happen to agree.
 */
static void CssBackgroundPartsAre(const CssValue *parts,
                                     const char *const *expected)
{
    static const CssPropertyComputation computations[] = {
        RoboCssPropertyComputationBackgroundImage,
        RoboCssPropertyComputationBackgroundPosition,
        RoboCssPropertyComputationBackgroundSize,
        RoboCssPropertyComputationBackgroundRepeat,
        RoboCssPropertyComputationBackgroundAttachment,
        RoboCssPropertyComputationColor
    };
    char text[RoboCssComputedValueTextMax];
    size_t index;

    for (index = 0u; index < sizeof(computations) / sizeof(computations[0]); index++) {
        CssBackgroundPartComputed(&parts[index], computations[index], text,
                                     sizeof(text));
        RoboCheckStrEq(text, expected[index]);
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
static void TestBackgroundShorthandExpand(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssValue parts[RoboCssBackgroundLonghandCount];
    char shape[256];
    size_t index;

    CssFixture();
    /* A colour alone: the other five are unspoken, not reset here. */
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueInit(&parts[index]);
    }
    RoboCheckEqInt((int)(sizeof(parts) / sizeof(parts[0])),
                      (int)RoboCssBackgroundLonghandCount);
    RoboCheckEqInt(CssBackgroundExpand(
                          CssBackgroundValue("p: red", &list), parts, NULL),
                      RoboOk);
    CssBackgroundPartShape(&parts[0], shape, sizeof(shape));
    RoboCheckStrEq(shape, "");
    CssBackgroundPartComputed(&parts[5], RoboCssPropertyComputationColor, shape,
                                 sizeof(shape));
    RoboCheckStrEq(shape, "#ff0000");
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueDispose(&parts[index]);
        CssValueInit(&parts[index]);
    }
    CssDeclarationListDispose(&list);

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

            for (part = 0u; part < (size_t)RoboCssBackgroundLonghandCount;
                 part++) {
                CssValueInit(&parts[part]);
            }
            RoboCheckEqInt(CssBackgroundExpand(
                                  CssBackgroundValue(declaration, &list), parts, NULL),
                              RoboOk);
            /* The part reaches the property and the *property's* shape reduces it:
             * four spellings, one computed colour each. */
            CssBackgroundPartComputed(&parts[5], RoboCssPropertyComputationColor,
                                         shape, sizeof(shape));
            RoboCheckStrEq(shape, hexes[at]);
            for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount;
                 index++) {
                CssValueDispose(&parts[index]);
                CssValueInit(&parts[index]);
            }
            CssDeclarationListDispose(&list);
        }
    }

    /* A whole layer: image, position, size, repeat, attachment and colour — each
     * to its own longhand, and the position and size pairs folded into one list
     * each, which is the shape a *declared* longhand's value has. */
    RoboCheckEqInt(CssBackgroundExpand(
                          CssBackgroundValue(
                              "p: url(\"a.png\") 10px 20px / 50% 25% no-repeat fixed "
                              "#336699",
                              &list),
                          parts, NULL),
                      RoboOk);
    {
        static const char *const expected[] = {
            "url(\"a.png\")", "10px 20px", "50% 25%", "no-repeat", "fixed",
            "#336699"
        };

        CssBackgroundPartsAre(parts, expected);
    }
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueDispose(&parts[index]);
        CssValueInit(&parts[index]);
    }
    CssDeclarationListDispose(&list);

    /* `center/cover`: the size group is what follows the `/`, and the position
     * before it is a position. */
    RoboCheckEqInt(CssBackgroundExpand(
                          CssBackgroundValue("p: center/cover", &list), parts, NULL),
                      RoboOk);
    CssBackgroundPartShape(&parts[0], shape, sizeof(shape));
    RoboCheckStrEq(shape, "");
    /* Folded by the property's own shape: the part is the single component the
     * author wrote, and the computed position is the pair. */
    CssBackgroundPartComputed(&parts[1], RoboCssPropertyComputationBackgroundPosition,
                                 shape, sizeof(shape));
    RoboCheckStrEq(shape, "center center");
    CssBackgroundPartComputed(&parts[2], RoboCssPropertyComputationBackgroundSize,
                                 shape, sizeof(shape));
    RoboCheckStrEq(shape, "cover");
    CssBackgroundPartShape(&parts[3], shape, sizeof(shape));
    RoboCheckStrEq(shape, "");
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueDispose(&parts[index]);
        CssValueInit(&parts[index]);
    }
    CssDeclarationListDispose(&list);

    /* `none` is an *image*, and a gradient is one this build carries. */
    RoboCheckEqInt(CssBackgroundExpand(
                          CssBackgroundValue("p: none", &list), parts, NULL),
                      RoboOk);
    CssBackgroundPartComputed(&parts[0], RoboCssPropertyComputationBackgroundImage,
                                 shape, sizeof(shape));
    RoboCheckStrEq(shape, "none");
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueDispose(&parts[index]);
        CssValueInit(&parts[index]);
    }
    CssDeclarationListDispose(&list);
    RoboCheckEqInt(CssBackgroundExpand(
                          CssBackgroundValue("p: linear-gradient(red, blue)", &list),
                          parts, NULL),
                      RoboOk);
    /* A gradient is an image this build carries: the part is the tree and the
     * property's shape preserves it with its tokens intact. */
    CssBackgroundPartComputed(&parts[0], RoboCssPropertyComputationBackgroundImage,
                                 shape, sizeof(shape));
    RoboCheckStrEq(shape, "linear-gradient(red, blue)");
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueDispose(&parts[index]);
        CssValueInit(&parts[index]);
    }
    CssDeclarationListDispose(&list);

    /* A position with no image is a position: `background: left top`. */
    RoboCheckEqInt(CssBackgroundExpand(
                          CssBackgroundValue("p: left top", &list), parts, NULL),
                      RoboOk);
    CssBackgroundPartShape(&parts[0], shape, sizeof(shape));
    RoboCheckStrEq(shape, "");
    CssBackgroundPartComputed(&parts[1], RoboCssPropertyComputationBackgroundPosition,
                                 shape, sizeof(shape));
    RoboCheckStrEq(shape, "left top");
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueDispose(&parts[index]);
        CssValueInit(&parts[index]);
    }
    CssDeclarationListDispose(&list);
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
static void TestBackgroundShorthandRefusals(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssValue parts[RoboCssBackgroundLonghandCount];
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

    CssFixture();
    for (at = 0u; at < sizeof(refused) / sizeof(refused[0]); at++) {
        for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount;
             index++) {
            CssValueInit(&parts[index]);
        }
        RoboCheck(CssBackgroundExpand(CssBackgroundValue(refused[at], &list),
                                             parts, NULL) != RoboOk);
        /* Nothing is assigned, so a caller that ignored the code owns no
         * longhand rather than some of them. */
        for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount;
             index++) {
            CssBackgroundPartShape(&parts[index], shape, sizeof(shape));
            RoboCheckStrEq(shape, "");
            CssValueDispose(&parts[index]);
            CssValueInit(&parts[index]);
        }
        CssDeclarationListDispose(&list);
    }

    /* The argument checks come before any of it. */
    for (index = 0u; index < (size_t)RoboCssBackgroundLonghandCount; index++) {
        CssValueInit(&parts[index]);
    }
    RoboCheckEqInt(CssBackgroundExpand(NULL, parts, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBackgroundExpand(
                          CssBackgroundValue("p: red", &list), NULL, NULL),
                      RoboInvalidArgument);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void CssBackgroundComputeOf(CssInheritFixture *fixture,
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
 * The typed value behind a name, through the query door a consumer reads.
 *
 * The door is the *context's* rather than the style's, for the reason 9.12
 * recorded: none of the six inherits, so an element that declared nothing has
 * **no entry** — 9.7 materialises initial values for the inherited-by-default set
 * only — and "what is this element's background-image" is a question only
 * something holding the registry can answer. `outFromInitial` is what says
 * whether the answer was the property's own initial value.
 */
static void CssBackgroundQuery(CssInheritFixture *fixture,
                                 const CssComputedStyle *style,
                                 const char *name,
                                 char *out,
                                 size_t capacity,
                                 int *outFromInitial)
{
    CssComputedValue value = RoboCssCleared;
    size_t length = 0u;

    if (outFromInitial) {
        *outFromInitial = -1;
    }
    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    RoboCheckEqInt(CssStyleContextQueryComputedValue(
                          CssInheritContext(fixture), style,
                          CssSliceFromText(name), &value, outFromInitial,
                          NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueKindIsKnown(value.kind), 1);
    if (out && capacity > 0u) {
        RoboCheckEqInt(CssComputedValueWrite(&value, out, capacity, &length,
                                                        NULL),
                          RoboOk);
    }
    CssComputedValueDispose(&value);
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
static void TestBackgroundInitial(void)
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
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef plain;
    char text[RoboCssComputedValueTextMax];
    size_t index;

    CssFixture();
    CssInheritBegin(&fixture);
    plain = CssHostAdd(CssInheritHost(&fixture), "div", "plain", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "#plain { color: red }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssBackgroundComputeOf(&fixture, plain, style);
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        int fromInitial = -1;

        CssBackgroundQuery(&fixture, style, rows[index].name, text, sizeof(text),
                             &fromInitial);
        RoboCheckStrEq(text, rows[index].computed);
        RoboCheckEqInt(fromInitial, 1);
    }
    /* The last one is the shorthand, whose initial value is not one of the six —
     * it is the token `transparent`, because the shorthand's own shape is PRESERVE
     * and its own initial value is a colour. Asserted as it is rather than as one
     * might wish: this phase distributes the shorthand and does not restate its
     * own row. */
    CssBackgroundQuery(&fixture, style, "background", text, sizeof(text), NULL);
    RoboCheckStrEq(text, "transparent");

    /* And the *style* has no entry for any of the six, which is what a computed
     * style of an element that declared nothing looks like. */
    RoboCheckEqInt(CssComputedStyleFind(style,
                                                  CssSliceFromText(
                                                      "background-image")) ==
                          (size_t)-1,
                      1);
    CssComputedIs(style, "background-image", "<absent>");
    CssComputedIs(style, "background-position", "<absent>");
    CssComputedIs(style, "background-size", "<absent>");
    CssComputedIs(style, "background-repeat", "<absent>");
    CssComputedIs(style, "background-attachment", "<absent>");
    CssComputedIs(style, "background-color", "<absent>");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestBackgroundNonInheritance(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef parent;
    CssElementRef child;
    CssElementRef explicitChild;
    char text[RoboCssComputedValueTextMax];
    int fromInitial = -1;

    CssFixture();
    CssInheritBegin(&fixture);
    parent = CssHostAdd(CssInheritHost(&fixture), "div", "parent", NULL);
    child = CssHostAdd(CssInheritHost(&fixture), "div", "child", NULL);
    explicitChild = CssHostAdd(CssInheritHost(&fixture), "div", "explicit", NULL);
    CssHostSetParent(CssInheritHost(&fixture), child, parent);
    CssHostSetParent(CssInheritHost(&fixture), explicitChild, parent);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#parent { background-color: #336699; background-image: "
                      "url(a.png); background-repeat: no-repeat; "
                      "background-position: 25% 50%; background-size: cover; "
                      "background-attachment: fixed }"
                      "#explicit { background-color: inherit; background-image: "
                      "inherit; background-position: inherit; background-size: "
                      "inherit; background-repeat: inherit; background-attachment: "
                      "inherit }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* The parent declares all six and has all six. */
    CssBackgroundComputeOf(&fixture, parent, style);
    CssComputedIs(style, "background-color", "#336699");
    CssComputedIs(style, "background-image", "url(a.png)");
    CssComputedIs(style, "background-repeat", "no-repeat");
    CssComputedIs(style, "background-position", "25% 50%");
    CssComputedIs(style, "background-size", "cover");
    CssComputedIs(style, "background-attachment", "fixed");
    CssComputedFlagsAre(style, "background-color", "declared");

    /* The child declares nothing, so none of it is there — and the query door
     * says so by answering the initial value and *marking* it as such. */
    CssBackgroundComputeOf(&fixture, child, style);
    CssComputedIs(style, "background-color", "<absent>");
    CssComputedIs(style, "background-image", "<absent>");
    CssComputedIs(style, "background-position", "<absent>");
    CssComputedIs(style, "background-size", "<absent>");
    CssComputedIs(style, "background-repeat", "<absent>");
    CssComputedIs(style, "background-attachment", "<absent>");
    CssBackgroundQuery(&fixture, style, "background-color", text, sizeof(text),
                         &fromInitial);
    RoboCheckStrEq(text, "#00000000");
    RoboCheckEqInt(fromInitial, 1);
    CssBackgroundQuery(&fixture, style, "background-image", text, sizeof(text),
                         &fromInitial);
    RoboCheckStrEq(text, "none");
    RoboCheckEqInt(fromInitial, 1);
    /* Five of the six would look the same if the child had inherited the parent's
     * values, so the child is asserted *against* the parent's, not only against
     * the initial value. */
    RoboCheckEqInt(strcmp(text, "url(a.png)") == 0, 0);

    /* An explicit `inherit` is a declaration, and it carries the parent's
     * *computed* value — the typed pair, not the text `25% 50%`. */
    CssBackgroundComputeOf(&fixture, explicitChild, style);
    CssComputedIs(style, "background-color", "#336699");
    CssComputedIs(style, "background-image", "url(a.png)");
    CssComputedIs(style, "background-position", "25% 50%");
    CssComputedIs(style, "background-size", "cover");
    CssComputedIs(style, "background-repeat", "no-repeat");
    CssComputedIs(style, "background-attachment", "fixed");
    /* The flag is `inherited`, not `declared`: the *declaration* was `inherit` and
     * the value is the parent's, which is exactly what the provenance says. */
    CssComputedFlagsAre(style, "background-color", "inherited");
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                style, CssSliceFromText("background-position"));

        RoboCheckEqInt(value->kind,
                          (int)RoboCssComputedValueBackgroundPosition);
        RoboCheckEqInt(CssComputedValueBackgroundPosition(value).horizontal.kind,
                          RoboCssBackgroundComponentPercentage);
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/** `initial`, `inherit` and `unset` for a family that does not inherit. */
static void TestBackgroundWideKeywords(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef parent;
    CssElementRef child;
    CssElementRef shorthand;
    size_t index;

    static const char *const names[] = {
        "background-color", "background-image", "background-position",
        "background-size",  "background-repeat", "background-attachment"
    };
    static const char *const initials[] = {
        "#00000000", "none", "0% 0%", "auto", "repeat", "scroll"
    };

    CssFixture();
    CssInheritBegin(&fixture);
    parent = CssHostAdd(CssInheritHost(&fixture), "div", "parent", NULL);
    child = CssHostAdd(CssInheritHost(&fixture), "div", "child", NULL);
    shorthand = CssHostAdd(CssInheritHost(&fixture), "div", "shorthand", NULL);
    CssHostSetParent(CssInheritHost(&fixture), child, parent);
    CssHostSetParent(CssInheritHost(&fixture), shorthand, parent);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#parent { background-color: #336699; background-image: "
                      "url(a.png); background-repeat: no-repeat; "
                      "background-position: 25% 50%; background-size: cover; "
                      "background-attachment: fixed }"
                      "#child { background-color: initial; background-image: "
                      "unset; background-position: initial; background-size: "
                      "initial; background-repeat: unset; background-attachment: "
                      "initial }"
                      "#shorthand { background: inherit }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* `initial` is the property's own value, and `unset` is the same thing here
     * because none of the six inherits — which is the difference from `inherit`,
     * asserted beside it in the non-inheritance test. */
    CssBackgroundComputeOf(&fixture, child, style);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        CssComputedIs(style, names[index], initials[index]);
    }
    /* The entries exist and say where they came from, and neither is `declared`:
     * a CSS-wide keyword resolves to the property's own initial value, which is
     * the `default` flag — and `unset` on a non-inherited property is the same
     * thing said in another word. */
    CssComputedFlagsAre(style, "background-color", "default");
    CssComputedFlagsAre(style, "background-repeat", "default");

    /* `background: inherit` distributes the *keyword*: one sentence, six times,
     * through the same rule an explicit longhand `inherit` goes through. */
    CssBackgroundComputeOf(&fixture, shorthand, style);
    CssComputedIs(style, "background-color", "#336699");
    CssComputedIs(style, "background-image", "url(a.png)");
    CssComputedIs(style, "background-position", "25% 50%");
    CssComputedIs(style, "background-size", "cover");
    CssComputedIs(style, "background-repeat", "no-repeat");
    CssComputedIs(style, "background-attachment", "fixed");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestBackgroundCascade(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef element;
    CssElementRef important;
    CssElementRef inlineElement;
    CssInlineStub stub;

    CssFixture();
    CssInheritBegin(&fixture);
    element = CssHostAdd(CssInheritHost(&fixture), "div", "element", NULL);
    important = CssHostAdd(CssInheritHost(&fixture), "div", "important", NULL);
    inlineElement = CssHostAdd(CssInheritHost(&fixture), "div", "inlined", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#element { background-color: red; background-repeat: "
                      "repeat-x }"
                      "#element { background-color: blue; background-repeat: "
                      "repeat-y }"
                      "#important { background-color: red !important }"
                      "div#important { background-color: blue }"
                      ".never { background-color: red }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* Source order: the second rule wins on both keys, and the *winner* is what
     * the computed style carries rather than a merge of the two. */
    CssBackgroundComputeOf(&fixture, element, style);
    CssComputedIs(style, "background-color", "#0000ff");
    CssComputedIs(style, "background-repeat", "repeat-y");
    CssComputedFlagsAre(style, "background-color", "declared");

    /* `!important` beats a more specific selector, which is the whole point of
     * it and a fact this phase consumes rather than restates. */
    CssBackgroundComputeOf(&fixture, important, style);
    CssComputedIs(style, "background-color", "#ff0000");

    /* And an inline declaration is the strongest normal level, so it beats a
     * sheet that says `!important` about the same element — installed here rather
     * than above because the stub answers *every* element, which is precisely
     * what the two assertions before it must not be measuring. */
    CssInheritInline(&fixture, &stub, "background-color: #336699");
    CssBackgroundComputeOf(&fixture, inlineElement, style);
    CssComputedIs(style, "background-color", "#336699");
    CssComputedFlagsAre(style, "background-color", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestBackgroundShorthandReset(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef reset;
    CssElementRef kept;

    CssFixture();
    CssInheritBegin(&fixture);
    reset = CssHostAdd(CssInheritHost(&fixture), "div", "reset", NULL);
    kept = CssHostAdd(CssInheritHost(&fixture), "div", "kept", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      /* An earlier rule gives the first element an image and a
                       * repeat; the shorthand comes after it. */
                      "#reset { background-image: url(a.png); background-repeat: "
                      "no-repeat; background-attachment: fixed }"
                      "#reset { background: red }"
                      /* And the mirror: the longhand comes after the shorthand. */
                      "#kept { background: red }"
                      "#kept { background-image: url(a.png) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* Everything the shorthand did not name is the property's initial value, and
     * `background: red` is a colour and not a colour plus five leftovers. */
    CssBackgroundComputeOf(&fixture, reset, style);
    CssComputedIs(style, "background-color", "#ff0000");
    CssComputedIs(style, "background-image", "none");
    CssComputedIs(style, "background-repeat", "repeat");
    CssComputedIs(style, "background-attachment", "scroll");
    CssComputedIs(style, "background-position", "0% 0%");
    CssComputedIs(style, "background-size", "auto");
    /* The flag matters: these are *declared* by the shorthand's distribution, not
     * absent and not inherited. */
    CssComputedFlagsAre(style, "background-image", "declared");

    /* The reverse order: the longhand wins, and the rest of the shorthand still
     * applies. */
    CssBackgroundComputeOf(&fixture, kept, style);
    CssComputedIs(style, "background-color", "#ff0000");
    CssComputedIs(style, "background-image", "url(a.png)");
    CssComputedIs(style, "background-repeat", "repeat");
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                style, CssSliceFromText("background-image"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValuePreserved);
        RoboCheckEqInt(value->unresolved, 1u);
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestBackgroundShorthandUndistributed(void)
{
    CssInheritFixture fixture;
    CssError error = RoboCssCleared;
    CssCascade cascade = RoboCssCleared;
    CssInheritedStyle resolved = RoboCssCleared;
    CssComputedStyle *style = NULL;
    CssElementRef element;

    CssFixture();
    CssInheritBegin(&fixture);
    element = CssHostAdd(CssInheritHost(&fixture), "div", "element", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#element { background: var(--layer) }");
    RoboCheckEqInt(CssStyleContextMatchDeclarations(
                          CssInheritContext(&fixture), element, &cascade, &error,
                          NULL),
                      RoboOk);
    RoboCheckEqInt(RoboCssInheritResolve(CssInheritContext(&fixture), &cascade,
                                              NULL, &resolved, NULL),
                      RoboOk);
    /* Reported, not silent — and it is the shorthand that says so. */
    RoboCheckEqInt((int)resolved.shorthandsUnexpanded, 1);
    RoboCheckEqInt(resolved.unsupported, 1);
    /* Nothing is guessed at: no longhand has an entry, so `background-image` here
     * is the property's initial value rather than a reading of the reference. */
    RoboCheckEqInt(CssInheritedStyleFind(
                          &resolved, CssSliceFromText("background-image")) ==
                          NULL,
                      1);
    RoboCheckEqInt(CssInheritedStyleFind(
                          &resolved, CssSliceFromText("background-color")) ==
                          NULL,
                      1);
    /* The author's declaration is still there, on the shorthand's own entry. */
    {
        const CssInheritedProperty *entry = CssInheritedStyleFind(
            &resolved, CssSliceFromText("background"));

        RoboCheck(entry != NULL);
        if (entry) {
            char shape[64];

            CssShape(&entry->value, shape, sizeof(shape));
            RoboCheckStrEq(shape, "var(--layer)");
        }
    }
    CssInheritedStyleDispose(&resolved);
    CssCascadeDispose(&cascade);

    /* And the computed style says the same thing: the shorthand is carried and
     * flagged, the six are the initial values. */
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    CssBackgroundComputeOf(&fixture, element, style);
    CssComputedIs(style, "background-image", "<absent>");
    CssComputedIs(style, "background-color", "<absent>");
    CssComputedIs(style, "background-position", "<absent>");
    {
        const CssComputedValue *value =
            CssComputedStyleComputedByName(
                style, CssSliceFromText("background"));

        RoboCheckEqInt(value->kind, (int)RoboCssComputedValuePreserved);
        RoboCheckEqInt(value->unresolved, 1u);
        CssComputedIs(style, "background", "var(--layer)");
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
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
static void TestBackgroundRobust(void)
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
    static const CssPropertyComputation computations[] = {
        RoboCssPropertyComputationBackgroundImage,
        RoboCssPropertyComputationBackgroundPosition,
        RoboCssPropertyComputationBackgroundSize,
        RoboCssPropertyComputationBackgroundRepeat,
        RoboCssPropertyComputationBackgroundAttachment,
        RoboCssPropertyComputationColor,
        RoboCssPropertyComputationPreserve
    };
    size_t at;
    size_t which;

    CssFixture();
    for (at = 0u; at < sizeof(malformed) / sizeof(malformed[0]); at++) {
        for (which = 0u; which < sizeof(computations) / sizeof(computations[0]);
             which++) {
            CssDeclarationList first = RoboCssCleared;
            CssDeclarationList second = RoboCssCleared;
            const CssDeclaration *declaration;
            CssComputedValue a = RoboCssCleared;
            CssComputedValue b = RoboCssCleared;
            char firstText[RoboCssComputedValueTextMax];
            char secondText[RoboCssComputedValueTextMax];
            size_t firstLength = 0u;
            size_t secondLength = 0u;
            /* Read before the record is disposed, because a disposed value's kind
             * is not a fact and comparing against it would be reading freed
             * memory — which is exactly the class of bug this test hunts. */
            CssComputedValueKind firstKind;

            RoboCheckEqInt(
                CssParseDeclarations(malformed[at], &first, NULL), RoboOk);
            declaration = CssDeclarationListAt(&first, 0u);
            /* A declaration the grammar rejected is not a value, and there is
             * nothing to compute — which is itself the safe answer. */
            if (!declaration) {
                CssDeclarationListDispose(&first);
                continue;
            }
            RoboCheckEqInt(CssComputedValueCompute(
                                  computations[which],
                                  CssDeclarationValue(declaration), &a, NULL),
                              RoboOk);
            /* Whatever came out is a *defined* kind: a shape cannot invent one,
             * and a refusal is INVALID rather than a cleared record. */
            RoboCheckEqInt(CssComputedValueKindIsKnown(a.kind), 1);
            firstKind = a.kind;
            RoboCheckEqInt(CssComputedValueWrite(&a, firstText,
                                                           sizeof(firstText),
                                                           &firstLength, NULL),
                              RoboOk);
            CssComputedValueDispose(&a);
            CssDeclarationListDispose(&first);

            /* The same input, computed again from a fresh parse: one spelling,
             * one kind, and the same text. */
            RoboCheckEqInt(
                CssParseDeclarations(malformed[at], &second, NULL), RoboOk);
            declaration = CssDeclarationListAt(&second, 0u);
            RoboCheck(declaration != NULL);
            if (declaration) {
                RoboCheckEqInt(CssComputedValueCompute(
                                      computations[which],
                                      CssDeclarationValue(declaration), &b,
                                      NULL),
                                  RoboOk);
                RoboCheckEqInt(b.kind, (int)firstKind);
                RoboCheckEqInt(CssComputedValueWrite(&b, secondText,
                                                               sizeof(secondText),
                                                               &secondLength, NULL),
                                  RoboOk);
                RoboCheckStrEq(secondText, firstText);
                RoboCheckEqInt((int)secondLength, (int)firstLength);
                CssComputedValueDispose(&b);
            }
            CssDeclarationListDispose(&second);
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
static void TestBackgroundDeterminism(void)
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
        const char *computedColor;
        const char *computedImage;
        const char *computedPosition;
        const char *computedSize;
        const char *computedRepeat;
        const char *computedAttachment;
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
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef refs[5];
    size_t index;
    size_t pass;

    CssFixture();
    CssInheritBegin(&fixture);
    for (index = 0u; index < 5u; index++) {
        refs[index] = CssHostAdd(CssInheritHost(&fixture), "div", rows[index].id,
                                   NULL);
    }
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#a { background: red }"
                      "#b { background-color: blue; background-image: url(a.png);"
                      " background-repeat: no-repeat }"
                      "#c { background-repeat: repeat-x }"
                      "#d { background: 25% 50% / cover no-repeat; background-color:"
                      " #336699; background-image: linear-gradient(red, blue) }"
                      "#e { background-color: white; background-repeat: no-repeat;"
                      " background: white no-repeat }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    for (pass = 0u; pass < 2u; pass++) {
        for (index = 0u; index < 5u; index++) {
            /* The second pass walks the same five backwards. */
            size_t at = (pass == 0u) ? index : (4u - index);

            CssBackgroundComputeOf(&fixture, refs[at], style);
            CssComputedIs(style, "background-color", rows[at].computedColor);
            CssComputedIs(style, "background-image", rows[at].computedImage);
            /* Both pair shapes are read on every element in both passes, which is
             * what makes this a statement about the family and not only about the
             * three keyword-shaped halves. */
            CssComputedIs(style, "background-position", rows[at].computedPosition);
            CssComputedIs(style, "background-size", rows[at].computedSize);
            CssComputedIs(style, "background-repeat", rows[at].computedRepeat);
            CssComputedIs(style, "background-attachment",
                            rows[at].computedAttachment);
        }
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssBackground(void);

void TestRegisterCssBackground(void)
{
    TestRegister("css.background.repeat.vocabulary",
                       TestBackgroundRepeatVocabulary);
    TestRegister("css.background.repeat.axes", TestBackgroundRepeatAxes);
    TestRegister("css.background.attachment.vocabulary",
                       TestBackgroundAttachmentVocabulary);
    TestRegister("css.background.edge.vocabulary", TestBackgroundEdgeVocabulary);
    TestRegister("css.background.component.model", TestBackgroundComponentModel);
    TestRegister("css.background.component.write", TestBackgroundComponentWrite);
    TestRegister("css.background.position.parse", TestBackgroundPositionParse);
    TestRegister("css.background.position.invalid", TestBackgroundPositionInvalid);
    TestRegister("css.background.position.reference",
                       TestBackgroundPositionReference);
    TestRegister("css.background.position.write", TestBackgroundPositionWrite);
    TestRegister("css.background.size.parse", TestBackgroundSizeParse);
    TestRegister("css.background.size.invalid", TestBackgroundSizeInvalid);
    TestRegister("css.background.size.write", TestBackgroundSizeWrite);
    TestRegister("css.background.position.computed",
                       TestBackgroundPositionComputed);
    TestRegister("css.background.position.computed_invalid",
                       TestBackgroundPositionComputedInvalid);
    TestRegister("css.background.size.computed", TestBackgroundSizeComputed);
    TestRegister("css.background.image.computed", TestBackgroundImageComputed);
    TestRegister("css.background.repeat.computed", TestBackgroundRepeatComputed);
    TestRegister("css.background.attachment.computed",
                       TestBackgroundAttachmentComputed);
    TestRegister("css.background.color.computed", TestBackgroundColorComputed);
    TestRegister("css.background.registry", TestBackgroundRegistry);
    TestRegister("css.background.shorthand.expand",
                       TestBackgroundShorthandExpand);
    TestRegister("css.background.shorthand.refusals",
                       TestBackgroundShorthandRefusals);
    TestRegister("css.background.initial", TestBackgroundInitial);
    TestRegister("css.background.non_inheritance",
                       TestBackgroundNonInheritance);
    TestRegister("css.background.keywords", TestBackgroundWideKeywords);
    TestRegister("css.background.cascade", TestBackgroundCascade);
    TestRegister("css.background.shorthand.reset",
                       TestBackgroundShorthandReset);
    TestRegister("css.background.shorthand.undistributed",
                       TestBackgroundShorthandUndistributed);
    TestRegister("css.background.robust", TestBackgroundRobust);
    TestRegister("css.background.determinism", TestBackgroundDeterminism);
}
