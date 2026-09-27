/*
 * RoboCSS flexbox suite (Phase 18.3) — css.flex.*
 *
 * The vocabulary, the classifications a layout pass dispatches on, the four value
 * shapes the alignment set is read through, the two numeric shapes, and the registry
 * rows the family is registered by.
 *
 * It is the first family whose values are three vocabularies, so the alignment half is
 * where the assertions earn their keep. One enumerator set serves four properties, and
 * what differs between them is a column of the rows rather than a branch at a call
 * site; this suite walks that column and then proves the shapes read it — the same
 * computation, asked for four different properties, refuses four different values.
 *
 * Nothing here asserts a layout. A flex value at this phase is a *value* with
 * classifications in it — which axis is main, whether it wraps, what an alignment
 * means — and every expectation below is about the value, what it classifies as, or
 * where it came from. Where the items land belongs to the phase with a box.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* flex-direction                                                      */
/* ------------------------------------------------------------------ */

/** The four values, their spellings, and the two classifications each carries. */
static void TestFlexDirectionVocabulary(void)
{
    static const char *const keywords[] = {
        "row", "row-reverse", "column", "column-reverse"
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFlexDirectionCount, 4);
    RoboCheckEqInt((int)CssFlexDirectionCount(), 4);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssFlexDirection direction = (CssFlexDirection)index;

        RoboCheckEqInt(CssFlexDirectionIsKnown(direction), 1);
        RoboCheckStrEq(CssFlexDirectionName(direction), keywords[index]);
        RoboCheckEqInt(CssFlexDirectionFromText(keywords[index]), (int)direction);
    }
    /* The main axis and the reversal, asserted per row: `row-reverse` shares `row`'s
     * axis and not its edges, and a comparison on either fact alone would make the two
     * spellings one value. */
    RoboCheckEqInt(CssFlexDirectionIsRow(RoboCssFlexDirectionRow), 1);
    RoboCheckEqInt(CssFlexDirectionIsColumn(RoboCssFlexDirectionRow), 0);
    RoboCheckEqInt(CssFlexDirectionIsReversed(RoboCssFlexDirectionRow), 0);
    RoboCheckEqInt(CssFlexDirectionIsRow(RoboCssFlexDirectionRowReverse), 1);
    RoboCheckEqInt(CssFlexDirectionIsReversed(RoboCssFlexDirectionRowReverse), 1);
    RoboCheckEqInt(CssFlexDirectionIsRow(RoboCssFlexDirectionColumn), 0);
    RoboCheckEqInt(CssFlexDirectionIsColumn(RoboCssFlexDirectionColumn), 1);
    RoboCheckEqInt(CssFlexDirectionIsReversed(RoboCssFlexDirectionColumn), 0);
    RoboCheckEqInt(CssFlexDirectionIsRow(RoboCssFlexDirectionColumnReverse), 0);
    RoboCheckEqInt(CssFlexDirectionIsColumn(RoboCssFlexDirectionColumnReverse), 1);
    RoboCheckEqInt(CssFlexDirectionIsReversed(RoboCssFlexDirectionColumnReverse), 1);

    /* The enumerator past the end and the value nothing defined answer nothing. */
    RoboCheckEqInt(CssFlexDirectionIsKnown(
                          (CssFlexDirection)RoboCssFlexDirectionCount),
                      0);
    RoboCheckEqInt(CssFlexDirectionIsKnown((CssFlexDirection)99), 0);
    RoboCheckStrEq(CssFlexDirectionName((CssFlexDirection)99), "");
    RoboCheckEqInt(CssFlexDirectionIsRow((CssFlexDirection)99), 0);
    RoboCheckEqInt(CssFlexDirectionIsColumn((CssFlexDirection)99), 0);
    RoboCheckEqInt(CssFlexDirectionIsReversed((CssFlexDirection)99), 0);
}

/** ROW is both a value and the answer for "not one", so the identifier is asked. */
static void TestFlexDirectionParsing(void)
{
    CssFixture();

    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceFromText("row")), 1);
    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceFromText("ROW")), 1);
    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceFromText("Column")), 1);
    /* Prefixes, near-misses and CSS's other values. */
    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceFromText("ro")), 0);
    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceFromText("roww")), 0);
    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceFromText("rows")), 0);
    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceFromText("")), 0);
    RoboCheckEqInt(CssFlexDirectionSliceIsKnown(CssSliceEmpty()), 0);

    RoboCheckEqInt(CssFlexDirectionFromSlice(CssSliceFromText("COLUMN")),
                      RoboCssFlexDirectionColumn);
    RoboCheckEqInt(CssFlexDirectionFromSlice(CssSliceFromText("banana")),
                      RoboCssFlexDirectionRow);
    RoboCheckEqInt(CssFlexDirectionFromText(NULL), RoboCssFlexDirectionRow);
}

/** One stable spelling per value, and a buffer too small is a refusal. */
static void TestFlexDirectionSerialization(void)
{
    char buffer[RoboCssFlexDirectionTextMax];
    char tiny[4];
    size_t length = 0u;

    CssFixture();

    RoboCheckEqInt(CssFlexDirectionWrite(RoboCssFlexDirectionRowReverse,
                                                   buffer, sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "row-reverse");
    RoboCheckEqInt((int)length, 11);
    RoboCheckEqInt(CssFlexDirectionWrite(RoboCssFlexDirectionColumnReverse,
                                                   buffer, sizeof(buffer), &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "column-reverse");
    /* The longest spelling fits a buffer of the advertised capacity, which is the
     * property the constant exists to promise. */
    RoboCheckEqInt((int)length, 14);

    length = 0u;
    RoboCheckEqInt(CssFlexDirectionWrite(RoboCssFlexDirectionColumnReverse,
                                                   tiny, sizeof(tiny), &length, NULL),
                      RoboNotSupported);
    RoboCheckStrEq(tiny, "");
    RoboCheckEqInt((int)length, 14);
    RoboCheckEqInt(CssFlexDirectionWrite((CssFlexDirection)99, buffer,
                                                   sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssFlexDirectionWrite(RoboCssFlexDirectionRow, NULL,
                                                   sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* flex-wrap                                                           */
/* ------------------------------------------------------------------ */

/** The three values, their spellings, and whether each wraps and reverses. */
static void TestFlexWrapVocabulary(void)
{
    static const char *const keywords[] = { "nowrap", "wrap", "wrap-reverse" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFlexWrapCount, 3);
    RoboCheckEqInt((int)CssFlexWrapCount(), 3);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssFlexWrap wrap = (CssFlexWrap)index;

        RoboCheckEqInt(CssFlexWrapIsKnown(wrap), 1);
        RoboCheckStrEq(CssFlexWrapName(wrap), keywords[index]);
        RoboCheckEqInt(CssFlexWrapFromText(keywords[index]), (int)wrap);
    }
    RoboCheckEqInt(CssFlexWrapIsWrapped(RoboCssFlexWrapNoWrap), 0);
    RoboCheckEqInt(CssFlexWrapIsReversed(RoboCssFlexWrapNoWrap), 0);
    RoboCheckEqInt(CssFlexWrapIsWrapped(RoboCssFlexWrapWrap), 1);
    RoboCheckEqInt(CssFlexWrapIsReversed(RoboCssFlexWrapWrap), 0);
    /* `wrap-reverse` wraps and reverses: the two facts are independent columns, which
     * is what makes the third row a row rather than a special case. */
    RoboCheckEqInt(CssFlexWrapIsWrapped(RoboCssFlexWrapWrapReverse), 1);
    RoboCheckEqInt(CssFlexWrapIsReversed(RoboCssFlexWrapWrapReverse), 1);

    RoboCheckEqInt(CssFlexWrapIsKnown((CssFlexWrap)99), 0);
    RoboCheckStrEq(CssFlexWrapName((CssFlexWrap)99), "");
    RoboCheckEqInt(CssFlexWrapSliceIsKnown(CssSliceFromText("wrap")), 1);
    RoboCheckEqInt(CssFlexWrapSliceIsKnown(CssSliceFromText("WRAP-REVERSE")), 1);
    RoboCheckEqInt(CssFlexWrapSliceIsKnown(CssSliceFromText("wrapreverse")), 0);
    RoboCheckEqInt(CssFlexWrapSliceIsKnown(CssSliceFromText("nowrap ")), 0);
    RoboCheckEqInt(CssFlexWrapFromSlice(CssSliceFromText("nope")),
                      RoboCssFlexWrapNoWrap);

    {
        char buffer[RoboCssFlexWrapTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFlexWrapWrite(RoboCssFlexWrapWrapReverse, buffer,
                                                    sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "wrap-reverse");
        RoboCheckEqInt((int)length, 12);
    }
}

/* ------------------------------------------------------------------ */
/* The alignment vocabulary                                            */
/* ------------------------------------------------------------------ */

/** All sixteen values, and the name each is spelled by. */
static void TestFlexAlignVocabulary(void)
{
    static const char *const keywords[] = {
        "normal",        "stretch",    "flex-start", "flex-end",
        "center",        "space-between", "space-around", "space-evenly",
        "baseline",      "auto",       "start",      "end",
        "self-start",    "self-end",   "left",       "right"
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFlexAlignCount, 16);
    RoboCheckEqInt((int)CssFlexAlignCount(), 16);
    RoboCheckEqInt(RoboCssFlexAlignCount, 16);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssFlexAlign align = (CssFlexAlign)index;

        RoboCheckEqInt(CssFlexAlignIsKnown(align), 1);
        RoboCheckStrEq(CssFlexAlignName(align), keywords[index]);
        RoboCheckEqInt(CssFlexAlignFromText(keywords[index]), (int)align);
    }
    RoboCheckEqInt(CssFlexAlignIsKnown((CssFlexAlign)99), 0);
    RoboCheckStrEq(CssFlexAlignName((CssFlexAlign)99), "");
    RoboCheckEqInt(CssFlexAlignSliceIsKnown(CssSliceFromText("SPACE-BETWEEN")), 1);
    RoboCheckEqInt(CssFlexAlignSliceIsKnown(CssSliceFromText("spacebetween")), 0);
    RoboCheckEqInt(CssFlexAlignFromSlice(CssSliceFromText("nonsense")),
                      RoboCssFlexAlignNormal);

    {
        char buffer[RoboCssFlexAlignTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFlexAlignWrite(RoboCssFlexAlignSpaceBetween, buffer,
                                                       sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "space-between");
        /* The longest spelling and its NUL fill the advertised capacity. */
        RoboCheckEqInt((int)length, 13);
        RoboCheckEqInt(CssFlexAlignWrite(RoboCssFlexAlignSpaceEvenly, buffer,
                                                       sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "space-evenly");
        /* A buffer too small is a refusal, with the length it needed. */
        {
            char tiny[4];

            RoboCheckEqInt(CssFlexAlignWrite(RoboCssFlexAlignSpaceBetween, tiny,
                                                           sizeof(tiny), &length, NULL),
                              RoboNotSupported);
            RoboCheckStrEq(tiny, "");
            RoboCheckEqInt((int)length, 13);
        }
    }
}

/**
 * The four property columns — the one question that makes one enumerator set safe for
 * four properties.
 *
 * Every row is asserted, because the columns *are* the vocabulary's meaning: the six
 * values legal for all four properties must answer 1 four times, and the six that are
 * not must answer exactly the properties CSS gives them. An unknown value answers 0
 * for every role, and an unknown role answers 0 for every value — which is the safe
 * direction, since accepting a value for a property it might not belong to is how a
 * legal-looking declaration reaches a layout pass as a rule nothing implements.
 */
static void TestFlexAlignRoles(void)
{
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFlexAlignRoleCount, 4);
    RoboCheckEqInt(CssFlexAlignRoleIsKnown(RoboCssFlexAlignRoleJustify), 1);
    RoboCheckEqInt(CssFlexAlignRoleIsKnown(RoboCssFlexAlignRoleContent), 1);
    RoboCheckEqInt(CssFlexAlignRoleIsKnown((CssFlexAlignRole)99), 0);
    RoboCheckStrEq(CssFlexAlignRoleName(RoboCssFlexAlignRoleJustify), "justify");
    RoboCheckStrEq(CssFlexAlignRoleName(RoboCssFlexAlignRoleItems), "items");
    RoboCheckStrEq(CssFlexAlignRoleName(RoboCssFlexAlignRoleSelf), "self");
    RoboCheckStrEq(CssFlexAlignRoleName(RoboCssFlexAlignRoleContent), "content");
    RoboCheckStrEq(CssFlexAlignRoleName((CssFlexAlignRole)99), "unknown");

    /* The six every property accepts. */
    for (index = 0u; index < 6u; index++) {
        static const CssFlexAlign all[] = {
            RoboCssFlexAlignNormal,    RoboCssFlexAlignStretch,
            RoboCssFlexAlignFlexStart, RoboCssFlexAlignFlexEnd,
            RoboCssFlexAlignCenter,    RoboCssFlexAlignStart
        };
        CssFlexAlignRole role;

        for (role = RoboCssFlexAlignRoleJustify;
             role < (CssFlexAlignRole)RoboCssFlexAlignRoleCount;
             role = (CssFlexAlignRole)(role + 1)) {
            RoboCheckEqInt(CssFlexAlignAllowedFor(all[index], role), 1);
        }
    }
    /* `end` is the seventh the four share. */
    {
        CssFlexAlignRole role;

        for (role = RoboCssFlexAlignRoleJustify;
             role < (CssFlexAlignRole)RoboCssFlexAlignRoleCount;
             role = (CssFlexAlignRole)(role + 1)) {
            RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignEnd, role), 1);
        }
    }

    /* The distribution values: the two container properties and neither item one. */
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSpaceBetween,
                                                    RoboCssFlexAlignRoleJustify), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSpaceBetween,
                                                    RoboCssFlexAlignRoleItems), 0);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSpaceBetween,
                                                    RoboCssFlexAlignRoleSelf), 0);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSpaceBetween,
                                                    RoboCssFlexAlignRoleContent), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSpaceEvenly,
                                                    RoboCssFlexAlignRoleContent), 1);

    /* `baseline` is the three alignment properties and not justify-content. */
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignBaseline,
                                                    RoboCssFlexAlignRoleJustify), 0);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignBaseline,
                                                    RoboCssFlexAlignRoleItems), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignBaseline,
                                                    RoboCssFlexAlignRoleSelf), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignBaseline,
                                                    RoboCssFlexAlignRoleContent), 1);

    /* `auto` is `align-self`'s alone. */
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignAuto,
                                                    RoboCssFlexAlignRoleSelf), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignAuto,
                                                    RoboCssFlexAlignRoleItems), 0);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignAuto,
                                                    RoboCssFlexAlignRoleJustify), 0);

    /* The self-position values pair with the item properties. */
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSelfStart,
                                                    RoboCssFlexAlignRoleItems), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSelfStart,
                                                    RoboCssFlexAlignRoleSelf), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSelfStart,
                                                    RoboCssFlexAlignRoleJustify), 0);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignSelfEnd,
                                                    RoboCssFlexAlignRoleContent), 0);

    /* The two physical edges are `justify-content`'s. */
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignLeft,
                                                    RoboCssFlexAlignRoleJustify), 1);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignLeft,
                                                    RoboCssFlexAlignRoleContent), 0);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignRight,
                                                    RoboCssFlexAlignRoleSelf), 0);

    /* An unknown value and an unknown role both answer 0. */
    RoboCheckEqInt(CssFlexAlignAllowedFor((CssFlexAlign)99,
                                                    RoboCssFlexAlignRoleJustify), 0);
    RoboCheckEqInt(CssFlexAlignAllowedFor(RoboCssFlexAlignCenter,
                                                    (CssFlexAlignRole)99), 0);
}

/** The alignment classifications a consumer reads besides the enumerator. */
static void TestFlexAlignClassification(void)
{
    CssFixture();

    RoboCheckEqInt(CssFlexAlignIsDistributed(RoboCssFlexAlignSpaceBetween), 1);
    RoboCheckEqInt(CssFlexAlignIsDistributed(RoboCssFlexAlignSpaceAround), 1);
    RoboCheckEqInt(CssFlexAlignIsDistributed(RoboCssFlexAlignSpaceEvenly), 1);
    RoboCheckEqInt(CssFlexAlignIsDistributed(RoboCssFlexAlignCenter), 0);
    RoboCheckEqInt(CssFlexAlignIsBaselinePosition(RoboCssFlexAlignBaseline), 1);
    RoboCheckEqInt(CssFlexAlignIsBaselinePosition(RoboCssFlexAlignCenter), 0);
    RoboCheckEqInt(CssFlexAlignIsAuto(RoboCssFlexAlignAuto), 1);
    RoboCheckEqInt(CssFlexAlignIsAuto(RoboCssFlexAlignNormal), 0);
    RoboCheckEqInt(CssFlexAlignIsNormal(RoboCssFlexAlignNormal), 1);
    /* The three classifications partition the interesting values: a distribution is
     * neither a baseline position nor the initial. */
    RoboCheckEqInt(CssFlexAlignIsDistributed(RoboCssFlexAlignBaseline) +
                          CssFlexAlignIsBaselinePosition(RoboCssFlexAlignBaseline),
                      1);
}

/* ------------------------------------------------------------------ */
/* flex-grow / flex-shrink                                             */
/* ------------------------------------------------------------------ */

/** The factor rule: finite, and not negative. */
static void TestFlexFactorRule(void)
{
    CssFixture();

    RoboCheckEqInt(CssFlexFactorIsValid(0.0), 1);
    RoboCheckEqInt(CssFlexFactorIsValid(1.0), 1);
    RoboCheckEqInt(CssFlexFactorIsValid(2.5), 1);
    /* There is no upper bound: a very large factor is legal CSS. */
    RoboCheckEqInt(CssFlexFactorIsValid(1.0e9), 1);
    RoboCheckEqInt(CssFlexFactorIsValid(-1.0), 0);
    RoboCheckEqInt(CssFlexFactorIsValid(-0.0), 1);
}

/* ------------------------------------------------------------------ */
/* The shapes                                                          */
/* ------------------------------------------------------------------ */

/** The direction and wrap shapes are typed enumerators, and their kind is theirs. */
static void TestFlexDirectionAndWrapShapes(void)
{
    CssFixture();

    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFlexDirection,
                                       "o: row"),
                      "flex-direction");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFlexWrap,
                                       "o: nowrap"),
                      "flex-wrap");
    CssComputeEquals(RoboCssPropertyComputationFlexDirection, "o: row", "row");
    CssComputeEquals(RoboCssPropertyComputationFlexDirection, "o: COLUMN",
                       "column");
    CssComputeEquals(RoboCssPropertyComputationFlexDirection,
                       "o:   column-reverse  ", "column-reverse");
    CssComputeEquals(RoboCssPropertyComputationFlexWrap, "o: wrap", "wrap");
    CssComputeEquals(RoboCssPropertyComputationFlexWrap, "o: WRAP-REVERSE",
                       "wrap-reverse");

    /* Three different mistakes, three different sentences. */
    CssComputeEquals(RoboCssPropertyComputationFlexDirection, "o: banana",
                       "not a flex-direction value");
    CssComputeEquals(RoboCssPropertyComputationFlexWrap, "o: banana",
                       "not a flex-wrap value");
    CssComputeEquals(RoboCssPropertyComputationFlexDirection, "o: 20px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationFlexWrap, "o: row wrap",
                       "not a single flex-wrap keyword");
    /* A reference is carried rather than refused, like every closed vocabulary. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFlexDirection,
                                       "o: var(--dir)"),
                      "preserved");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFlexDirection),
                      "flex-direction");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFlexWrap),
                      "flex-wrap");
}

/**
 * The alignment shape is one vocabulary read through four shapes, and the property is
 * what decides which values are legal.
 *
 * This is the assertion the phase exists for: `space-between` computes for
 * `justify-content` and is refused for `align-items`; `baseline` is the reverse;
 * `auto` is `align-self`'s alone; `left` is `justify-content`'s. Every one of them is
 * the same enumerator underneath, which is what lets a consumer read one typed value
 * whatever property produced it.
 */
static void TestFlexAlignShapes(void)
{
    CssFixture();

    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationJustifyContent,
                                       "o: center"),
                      "flex-align");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationAlignSelf,
                                       "o: auto"),
                      "flex-align");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationJustifyContent),
                      "justify-content");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationAlignItems),
                      "align-items");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationAlignSelf),
                      "align-self");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationAlignContent),
                      "align-content");

    /* The values all four share. */
    CssComputeEquals(RoboCssPropertyComputationJustifyContent, "o: center",
                       "center");
    CssComputeEquals(RoboCssPropertyComputationAlignItems, "o: center",
                       "center");
    CssComputeEquals(RoboCssPropertyComputationAlignSelf, "o: normal",
                       "normal");
    CssComputeEquals(RoboCssPropertyComputationAlignContent, "o: stretch",
                       "stretch");

    /* The distribution values reach the two container properties only. */
    CssComputeEquals(RoboCssPropertyComputationJustifyContent,
                       "o: space-between", "space-between");
    CssComputeEquals(RoboCssPropertyComputationAlignContent,
                       "o: space-between", "space-between");
    CssComputeEquals(RoboCssPropertyComputationAlignItems,
                       "o: space-between", "not an align-items value");
    CssComputeEquals(RoboCssPropertyComputationAlignSelf,
                       "o: space-evenly", "not an align-self value");

    /* `baseline` is the three alignment properties and not `justify-content`. */
    CssComputeEquals(RoboCssPropertyComputationAlignItems, "o: baseline",
                       "baseline");
    CssComputeEquals(RoboCssPropertyComputationAlignSelf, "o: baseline",
                       "baseline");
    CssComputeEquals(RoboCssPropertyComputationAlignContent, "o: baseline",
                       "baseline");
    CssComputeEquals(RoboCssPropertyComputationJustifyContent, "o: baseline",
                       "not a justify-content value");

    /* `auto` is `align-self`'s, and `left`/`right` are `justify-content`'s. */
    CssComputeEquals(RoboCssPropertyComputationAlignSelf, "o: auto", "auto");
    CssComputeEquals(RoboCssPropertyComputationAlignItems, "o: auto",
                       "not an align-items value");
    CssComputeEquals(RoboCssPropertyComputationJustifyContent, "o: left",
                       "left");
    CssComputeEquals(RoboCssPropertyComputationAlignContent, "o: left",
                       "not an align-content value");
    CssComputeEquals(RoboCssPropertyComputationAlignItems, "o: self-start",
                       "self-start");

    /* And the two refusals every vocabulary shares. */
    CssComputeEquals(RoboCssPropertyComputationAlignContent, "o: banana",
                       "not an align-content value");
    CssComputeEquals(RoboCssPropertyComputationJustifyContent, "o: 20px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationAlignItems, "o: center left",
                       "not a single align-items keyword");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationAlignContent,
                                       "o: var(--a)"),
                      "preserved");
}

/** The two numeric shapes: a factor that must not be negative, an order that may be. */
static void TestFlexNumericShapes(void)
{
    CssFixture();

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFlexFactor),
                      "flex-factor");
    RoboCheckStrEq(CssPropertyComputationName(RoboCssPropertyComputationOrder),
                      "order");

    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: 0", "0");
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: 1", "1");
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: 2.5", "2.5");
    /* `1.0` is a number here, not a near-miss integer: a factor has no integer grammar. */
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: 1.0", "1");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFlexFactor,
                                       "o: 2"),
                      "number");
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: -1",
                       "a flex factor cannot be negative");
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: 50%",
                       "not a non-negative number");
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: 10px",
                       "not a non-negative number");
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: banana",
                       "not a non-negative number");
    CssComputeEquals(RoboCssPropertyComputationFlexFactor, "o: 1 2",
                       "not a non-negative number");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFlexFactor,
                                       "o: calc(1 + 1)"),
                      "preserved");

    /* `order` is signed, and it is an `<integer>`. */
    CssComputeEquals(RoboCssPropertyComputationOrder, "o: 0", "0");
    CssComputeEquals(RoboCssPropertyComputationOrder, "o: -1", "-1");
    CssComputeEquals(RoboCssPropertyComputationOrder, "o: 3", "3");
    CssComputeEquals(RoboCssPropertyComputationOrder, "o: 1.5",
                       "not a whole number");
    /* `1.0` is the near miss: a whole value spelled as a number, which `<integer>`
     * still refuses, exactly as `z-index` does. */
    CssComputeEquals(RoboCssPropertyComputationOrder, "o: 1.0",
                       "a whole number must be written as an integer");
    CssComputeEquals(RoboCssPropertyComputationOrder, "o: auto",
                       "not a whole number");
    CssComputeEquals(RoboCssPropertyComputationOrder, "o: 10px",
                       "not a whole number");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationOrder, "o: 2"),
                      "number");
}

/** `flex-basis` is a length family: a length, a percentage or one of four keywords. */
static void TestFlexBasisShape(void)
{
    CssFixture();

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFlexBasis),
                      "flex-basis");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: auto", "auto");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: content", "content");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: min-content",
                       "min-content");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: max-content",
                       "max-content");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFlexBasis,
                                       "o: auto"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: 10px", "10px");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: 50%", "50%");
    /* An absolute unit arrives as the reference pixel, so `flex-basis: 1in` and
     * `width: 1in` cannot come to disagree about what a computed length is. */
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: 1in", "96px");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: 0", "0px");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: 2em", "2em");

    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: banana",
                       "not a length, a percentage, auto or content");
    /* `normal` is a length keyword for `width` and is not one here: the keyword list is
     * the property's, which is the whole reason the grammar carries one. */
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: normal",
                       "not a length, a percentage, auto or content");
    CssComputeEquals(RoboCssPropertyComputationFlexBasis, "o: 10px 20px",
                       "not a single length component");
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

/** The twelve rows, read from the standard registry rather than restated. */
static void TestFlexRegistry(void)
{
    static const struct {
        const char *name;
        CssPropertyComputation computation;
        const char *initial;
    } rows[] = {
        { "align-content",  RoboCssPropertyComputationAlignContent,   "normal" },
        { "align-items",    RoboCssPropertyComputationAlignItems,     "normal" },
        { "align-self",     RoboCssPropertyComputationAlignSelf,      "auto" },
        { "flex",           RoboCssPropertyComputationPreserve,       "0 1 auto" },
        { "flex-basis",     RoboCssPropertyComputationFlexBasis,      "auto" },
        { "flex-direction", RoboCssPropertyComputationFlexDirection,  "row" },
        { "flex-flow",      RoboCssPropertyComputationPreserve,       "row nowrap" },
        { "flex-grow",      RoboCssPropertyComputationFlexFactor,     "0" },
        { "flex-shrink",    RoboCssPropertyComputationFlexFactor,     "1" },
        { "flex-wrap",      RoboCssPropertyComputationFlexWrap,       "nowrap" },
        { "justify-content", RoboCssPropertyComputationJustifyContent, "normal" },
        { "order",          RoboCssPropertyComputationOrder,          "0" }
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, rows[index].name);

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(definition->computation, (int)rows[index].computation);
        /* None of the twelve inherits: a flex container's direction, a wrap rule, an
         * alignment and a factor are the box's own, and a child that inherited one
         * would lay itself out by its parent's rule. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckStrEq(definition->initialText, rows[index].initial);
    }

    /* The alignment initials are values *their own property* may hold — `align-self`'s
     * `auto` is legal for `self` and not for `items`, which is what the initial run
     * through the shape above already proved; here the role is named. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "align-self");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(CssFlexAlignAllowedFor(
                                  CssFlexAlignFromText(definition->initialText),
                                  RoboCssFlexAlignRoleSelf),
                          1);
        }
    }

    /* The two shorthands are flagged as what they are and carry their tokens; the ten
     * others are not shorthands. */
    {
        const CssPropertyDefinition *flex =
            CssPropertyRegistryFind(registry, "flex");
        const CssPropertyDefinition *flow =
            CssPropertyRegistryFind(registry, "flex-flow");
        const CssPropertyDefinition *grow =
            CssPropertyRegistryFind(registry, "flex-grow");

        RoboCheck(flex != NULL && flow != NULL && grow != NULL);
        if (flex) {
            RoboCheckEqInt(CssPropertyFlagsShorthand(flex->flags), 1);
        }
        if (flow) {
            RoboCheckEqInt(CssPropertyFlagsShorthand(flow->flags), 1);
        }
        if (grow) {
            RoboCheckEqInt(CssPropertyFlagsShorthand(grow->flags), 0);
        }
    }
    RoboCheck(CssPropertyRegistryFind(registry, "flex-nonsense") == NULL);
    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* Module and determinism                                              */
/* ------------------------------------------------------------------ */

/** The capability, the version and the kind names the family added. */
static void TestFlexCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.flexbox, 1);
    /* What the flag does not claim: no default stylesheet and no loading. */
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 35);
    RoboCheckStrEq(CssVersionString(), "0.35.0");

    RoboCheckEqInt((int)RoboCssComputedValueKindCount, 20);
    RoboCheckStrEq(CssComputedValueKindName(RoboCssComputedValueFlexDirection),
                      "flex-direction");
    RoboCheckStrEq(CssComputedValueKindName(RoboCssComputedValueFlexWrap),
                      "flex-wrap");
    RoboCheckStrEq(CssComputedValueKindName(RoboCssComputedValueFlexAlign),
                      "flex-align");
    RoboCheckEqInt(CssComputedValueKindIsKnown(RoboCssComputedValueFlexAlign), 1);

    /* The diagnostic domain the vocabulary reports under. */
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainFlex), "flex");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainFlex), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestFlexDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: space-evenly", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationAlignContent,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationAlignContent,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    /* A direction and a wrap value are their enumerator, so a copy compares equal to
     * its source and unequal to its neighbour. */
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationFlexDirection,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheck(CssComputedValueFlexDirection(&first) ==
              RoboCssFlexDirectionRow);
    RoboCheck(CssComputedValueFlexWrap(&first) == RoboCssFlexWrapNoWrap);
    RoboCheck(CssComputedValueFlexAlign(&first) == RoboCssFlexAlignNormal);

    CssComputedValueDispose(&first);
    CssComputedValueDispose(&second);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterCssFlex(void);

void TestRegisterCssFlex(void)
{
    TestRegister("css.flex.direction_vocabulary", TestFlexDirectionVocabulary);
    TestRegister("css.flex.direction_parsing", TestFlexDirectionParsing);
    TestRegister("css.flex.direction_serialization",
                       TestFlexDirectionSerialization);
    TestRegister("css.flex.wrap_vocabulary", TestFlexWrapVocabulary);
    TestRegister("css.flex.align_vocabulary", TestFlexAlignVocabulary);
    TestRegister("css.flex.align_roles", TestFlexAlignRoles);
    TestRegister("css.flex.align_classification", TestFlexAlignClassification);
    TestRegister("css.flex.factor_rule", TestFlexFactorRule);
    TestRegister("css.flex.computed.direction_and_wrap",
                       TestFlexDirectionAndWrapShapes);
    TestRegister("css.flex.computed.align", TestFlexAlignShapes);
    TestRegister("css.flex.computed.numeric", TestFlexNumericShapes);
    TestRegister("css.flex.computed.basis", TestFlexBasisShape);
    TestRegister("css.flex.registry", TestFlexRegistry);
    TestRegister("css.flex.capabilities", TestFlexCapabilities);
    TestRegister("css.flex.determinism", TestFlexDeterminism);
}
