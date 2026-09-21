/*
 * RoboCSS box model suite (Phase 9.11) — the vocabulary, the component mapping,
 * the three value shapes, and what the pipeline makes of a shorthand.
 *
 * Four groups, in the order the module is built:
 *
 *   the vocabulary  the four sides and the three shorthands, every row walked
 *                   rather than a representative one typed out
 *   expansion       component counting, the 1/2/3/4 mapping, and the two
 *                   refusals — five components, and a `var()` that would change
 *                   the count once substituted
 *   the shapes      `auto`, a length, a percentage, and the width keywords, each
 *                   asserted through *declaration text* so the tokenizer's route
 *                   is the one under test
 *   integration     a shorthand through a real element: expansion, the cascade's
 *                   answer when a longhand competes, the CSS-wide keywords, and
 *                   non-inheritance
 *
 * Nothing here asserts a pixel. A box model at this phase is a set of *values*
 * with a shape, and every expectation below is about the value's type, its
 * canonical text, or where it came from — which is the whole difference between a
 * computed value and a used one.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Reading a value out of declaration text                             */
/* ------------------------------------------------------------------ */

/** The specified value of the single declaration in @p text. */
static const CssValue *CssBoxValueOf(const char *text,
                                               CssDeclarationList *list)
{
    CssError error = RoboCssCleared;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations(text, list, &error), RoboOk);
    RoboCheckEqInt((int)CssDeclarationListCount(list), 1);
    return CssDeclarationValue(CssDeclarationListAt(list, 0u));
}

/** The shape of @p side's component of the shorthand written in @p text. */
static void CssBoxSideShape(const char *text,
                               size_t componentCount,
                               CssBoxSide side,
                               char *out,
                               size_t capacity)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssBoxValueOf(text, &list);
    CssValue component = RoboCssCleared;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    RoboCheckEqInt(
        CssBoxExpandSide(value, componentCount, side, &component, NULL),
        RoboOk);
    CssShape(&component, out, capacity);
    CssValueDispose(&component);
    CssDeclarationListDispose(&list);
}

/** One side of a shorthand, as shape and as count, from one assertion. */
static void CssBoxSideIs(const char *text,
                            size_t componentCount,
                            CssBoxSide side,
                            const char *expected)
{
    char shape[256];

    CssBoxSideShape(text, componentCount, side, shape, sizeof(shape));
    RoboCheckStrEq(shape, expected);
}

/** The component count of @p text, and whether it can be distributed. */
static void CssBoxCountIs(const char *text, size_t expectedCount, int expandable)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value = CssBoxValueOf(text, &list);
    size_t reported = 0u;
    int ok;

    RoboCheckEqInt((int)CssBoxComponentCount(value), (int)expectedCount);
    ok = CssBoxIsExpandable(value, &reported);
    RoboCheckEqInt(ok, expandable);
    /* The count is only written when the answer is yes, so a refused value cannot
     * hand a caller a mapping to apply. */
    RoboCheckEqInt((int)reported, expandable ? (int)expectedCount : 0);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * The four sides, and the order they are in.
 *
 * The order is the assertion, not the spelling: a table that listed the sides
 * differently would transpose every two- and three-component shorthand, and the
 * only way to see that from the vocabulary is to walk it in the defined order and
 * read the names back.
 */
static void TestBoxSideVocabulary(void)
{
    static const char *const names[] = { "top", "right", "bottom", "left" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBoxSideCount, 4);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        CssBoxSide side = (CssBoxSide)index;

        RoboCheckEqInt(CssBoxSideIsKnown(side), 1);
        RoboCheckStrEq(CssBoxSideName(side), names[index]);
    }
    RoboCheckEqInt(CssBoxSideIsKnown((CssBoxSide)4), 0);
    RoboCheckEqInt(CssBoxSideIsKnown((CssBoxSide)-1), 0);
    RoboCheckStrEq(CssBoxSideName((CssBoxSide)9), "unknown");
}

/**
 * Five shorthands, each with four longhands, walked rather than sampled.
 *
 * `border-width` is the row worth naming: it is a shorthand this build expands
 * while implementing no border *painting* at all, because a `border-width: 1px 2px`
 * that no longhand ever saw would be a declaration no later phase could consume.
 * `border-style` and `border-color` joined it in 9.15 — the same 1/2/3/4 mapping
 * over the same four sides, which is exactly why they reuse this table rather than
 * getting a copy of it.
 */
static void TestBoxShorthandTable(void)
{
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)CssBoxShorthandCount(), 5);

    {
        static const char *const expected[][4] = {
            { "margin-top", "margin-right", "margin-bottom", "margin-left" },
            { "padding-top", "padding-right", "padding-bottom", "padding-left" },
            { "border-top-width", "border-right-width", "border-bottom-width",
              "border-left-width" },
            { "border-top-style", "border-right-style", "border-bottom-style",
              "border-left-style" },
            { "border-top-color", "border-right-color", "border-bottom-color",
              "border-left-color" }
        };
        static const char *const shorthandNames[] = {
            "margin", "padding", "border-width", "border-style", "border-color"
        };

        for (index = 0u; index < 5u; index++) {
            const CssBoxShorthand *row = CssBoxShorthandAt(index);
            size_t sideIndex;

            RoboCheck(row != NULL);
            if (!row) {
                continue;
            }
            RoboCheckStrEq(row->name, shorthandNames[index]);
            RoboCheckEqInt((int)row->nameLength, (int)strlen(row->name));
            for (sideIndex = 0u; sideIndex < 4u; sideIndex++) {
                const char *longhand = CssBoxShorthandLonghand(
                    row, (CssBoxSide)sideIndex);
                CssBoxSide backSide = RoboCssBoxSideTop;
                const CssBoxShorthand *back;

                RoboCheckStrEq(longhand, expected[index][sideIndex]);
                /* The lookup runs both ways, so a caller holding a *declaration*
                 * can find the shorthand that owns it. */
                back = CssBoxShorthandOfLonghand(
                    CssSliceFromText(longhand), &backSide);
                RoboCheck(back == row);
                RoboCheckEqInt((int)backSide, (int)sideIndex);
            }
        }
    }

    /* Out of range, and past the end. */
    RoboCheck(CssBoxShorthandAt(5u) == NULL);
    RoboCheck(CssBoxShorthandAt((size_t)-1) == NULL);

    /* Found by name, case-insensitively — CSS compares property names that way. */
    RoboCheck(CssBoxShorthandFind(CssSliceFromText("margin")) != NULL);
    RoboCheck(CssBoxShorthandFind(CssSliceFromText("MARGIN")) != NULL);
    RoboCheck(CssBoxShorthandFind(CssSliceFromText("border-width")) !=
               NULL);
    /* A longhand is not a shorthand, and neither is anything else. */
    RoboCheck(CssBoxShorthandFind(CssSliceFromText("margin-top")) ==
               NULL);
    RoboCheck(CssBoxShorthandFind(CssSliceFromText("border")) == NULL);
    RoboCheck(CssBoxShorthandFind(CssSliceFromText("")) == NULL);
    RoboCheck(CssBoxShorthandFind(CssSliceEmpty()) == NULL);

    /* `border` is deliberately *not* a row here: it covers all four sides of all
     * three kinds, and a longhand it sets can also be set by `border-top` and by
     * `border-width` at once — which is a decision no positional row can take, so
     * the border family owns that distribution (9.15). Its longhands are still
     * reachable both ways. */
    RoboCheck(CssBoxShorthandOfLonghand(
                   CssSliceFromText("border-top-width"), NULL) != NULL);
    RoboCheck(CssBoxShorthandOfLonghand(
                   CssSliceFromText("border-top-style"), NULL) != NULL);
    RoboCheck(CssBoxShorthandOfLonghand(
                   CssSliceFromText("margin-inline-start"), NULL) == NULL);

    /* A NULL shorthand has no longhands, and neither has an unknown side. */
    RoboCheck(CssBoxShorthandLonghand(NULL, RoboCssBoxSideTop) == NULL);
    RoboCheck(CssBoxShorthandLonghand(CssBoxShorthandAt(0u),
                                               (CssBoxSide)7) == NULL);
}

/* ------------------------------------------------------------------ */
/* Expansion                                                           */
/* ------------------------------------------------------------------ */

/**
 * The mapping, all four arities, read out of a table rather than restated.
 *
 * Every case below is the shortest spelling that writes a distinct value per
 * side, so the four rows are the whole rule. The values are `1px`..`4px` so that a
 * side answered with the wrong component is visible in the shape and not merely
 * off by a number.
 */
static void TestBoxComponentMapping(void)
{
    CssFixture();

    CssBoxCountIs("m: 1px", 1u, 1);
    CssBoxCountIs("m: 1px 2px", 2u, 1);
    CssBoxCountIs("m: 1px 2px 3px", 3u, 1);
    CssBoxCountIs("m: 1px 2px 3px 4px", 4u, 1);

    /* One component: all four. */
    CssBoxSideIs("m: 1px", 1u, RoboCssBoxSideTop, "1px");
    CssBoxSideIs("m: 1px", 1u, RoboCssBoxSideRight, "1px");
    CssBoxSideIs("m: 1px", 1u, RoboCssBoxSideBottom, "1px");
    CssBoxSideIs("m: 1px", 1u, RoboCssBoxSideLeft, "1px");

    /* Two: vertical, then horizontal. */
    CssBoxSideIs("m: 1px 2px", 2u, RoboCssBoxSideTop, "1px");
    CssBoxSideIs("m: 1px 2px", 2u, RoboCssBoxSideRight, "2px");
    CssBoxSideIs("m: 1px 2px", 2u, RoboCssBoxSideBottom, "1px");
    CssBoxSideIs("m: 1px 2px", 2u, RoboCssBoxSideLeft, "2px");

    /* Three: top differs from bottom, and left borrows right. This is the row a
     * hand-written branch gets wrong. */
    CssBoxSideIs("m: 1px 2px 3px", 3u, RoboCssBoxSideTop, "1px");
    CssBoxSideIs("m: 1px 2px 3px", 3u, RoboCssBoxSideRight, "2px");
    CssBoxSideIs("m: 1px 2px 3px", 3u, RoboCssBoxSideBottom, "3px");
    CssBoxSideIs("m: 1px 2px 3px", 3u, RoboCssBoxSideLeft, "2px");

    /* Four: clockwise from the top. */
    CssBoxSideIs("m: 1px 2px 3px 4px", 4u, RoboCssBoxSideTop, "1px");
    CssBoxSideIs("m: 1px 2px 3px 4px", 4u, RoboCssBoxSideRight, "2px");
    CssBoxSideIs("m: 1px 2px 3px 4px", 4u, RoboCssBoxSideBottom, "3px");
    CssBoxSideIs("m: 1px 2px 3px 4px", 4u, RoboCssBoxSideLeft, "4px");

    /* Whitespace inside a value is not a component: `1px   2px` is two, and the
     * interior trivia the grammar keeps is skipped rather than counted. */
    CssBoxCountIs("m: 1px   2px", 2u, 1);
    CssBoxCountIs("m: 1px\n\t2px", 2u, 1);
    /* A percentage and a relative unit are components like any other here; what
     * they *mean* is the value layer's question, not the vocabulary's. */
    CssBoxCountIs("m: 50% 2em auto", 3u, 1);
    CssBoxSideIs("m: 50% 2em auto", 3u, RoboCssBoxSideLeft, "2em");
    /* An absolute unit keeps its author's spelling in the *component* — the
     * component is the specified value, and normalisation is a later stage. */
    CssBoxSideIs("m: 1in 2in", 2u, RoboCssBoxSideLeft, "2in");

    /* The all-or-nothing door agrees with the one-at-a-time door. */
    {
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value = CssBoxValueOf("m: 1px 2px 3px 4px", &list);
        CssValue sides[RoboCssBoxSideCount];
        static const char *const expected[] = { "1px", "2px", "3px", "4px" };
        size_t index;

        RoboCheckEqInt(CssBoxExpand(value, 4u, sides, NULL), RoboOk);
        for (index = 0u; index < (size_t)RoboCssBoxSideCount; index++) {
            char shape[64];

            CssShape(&sides[index], shape, sizeof(shape));
            RoboCheckStrEq(shape, expected[index]);
            CssValueDispose(&sides[index]);
        }
        CssDeclarationListDispose(&list);
    }
}

/**
 * What cannot be distributed, and why each one cannot.
 *
 * Two refusals, and they are different in kind. Five components is an *invalid
 * declaration* that the grammar accepted because validity is not syntax. A
 * `var()` is a perfectly good declaration whose component count is not known yet,
 * because substitution happens before expansion in CSS — so splitting it now would
 * assign the whole reference to every side and be wrong on the first real case.
 */
static void TestBoxExpansionRefusals(void)
{
    CssFixture();

    /* Five is not a longer shorthand. */
    CssBoxCountIs("m: 1px 2px 3px 4px 5px", 5u, 0);
    /* Six, so the refusal is a bound rather than a special case. */
    CssBoxCountIs("m: 1px 2px 3px 4px 5px 6px", 6u, 0);
    /* Nothing at all. */
    CssBoxCountIs("m: var(--x)", 1u, 0);
    CssBoxCountIs("m: var(--x) 2px", 2u, 0);
    CssBoxCountIs("m: 1px env(--y)", 2u, 0);
    /* ...and the reason is the reference, not the arity: the same three values
     * without a reference distribute. */
    CssBoxCountIs("m: calc(1px + 1px) 2px 3px", 3u, 1);

    /* Expanding an arity there is no row for is refused rather than clamped. */
    {
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value = CssBoxValueOf("m: 1px 2px", &list);
        CssValue component = RoboCssCleared;

        RoboCheckEqInt(CssBoxExpandSide(value, 0u, RoboCssBoxSideTop,
                                                   &component, NULL),
                          RoboNotSupported);
        RoboCheck(CssValueIsEmpty(&component));
        RoboCheckEqInt(CssBoxExpandSide(value, 5u, RoboCssBoxSideTop,
                                                   &component, NULL),
                          RoboNotSupported);
        RoboCheck(CssValueIsEmpty(&component));
        /* An unknown side is refused before anything is built. */
        RoboCheckEqInt(CssBoxExpandSide(value, 2u, (CssBoxSide)11,
                                                   &component, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssBoxExpandSide(NULL, 2u, RoboCssBoxSideTop,
                                                   &component, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssBoxExpand(value, 2u, NULL, NULL),
                          RoboInvalidArgument);
        CssDeclarationListDispose(&list);
    }

    /* A NULL value and a cleared one have no components; both are safe. */
    {
        CssValue empty = RoboCssCleared;
        size_t count = 99u;

        CssValueInit(&empty);
        RoboCheckEqInt((int)CssBoxComponentCount(NULL), 0);
        RoboCheckEqInt((int)CssBoxComponentCount(&empty), 0);
        RoboCheckEqInt(CssBoxIsExpandable(NULL, &count), 0);
        RoboCheckEqInt((int)count, 0);
        RoboCheckEqInt(CssBoxIsExpandable(&empty, NULL), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The three value shapes                                              */
/* ------------------------------------------------------------------ */

/**
 * What each box-model family accepts, through declaration text.
 *
 * The three families differ by *vocabulary*, and that is the whole reason there
 * are three shapes instead of one: `auto` is a margin and is not a padding,
 * `medium` is a border width and is not a margin, and `min-content` is a width and
 * is neither. A shape that accepted every length-family keyword everywhere would
 * make `padding: auto` computable, and CSS says it is a mistake.
 */
static void TestBoxShapes(void)
{
    CssFixture();

    /* --- margin: a length, a percentage, or `auto` --- */
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: 10px", "10px");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: 1in", "96px");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: 50%", "50%");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: auto", "auto");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: 0", "0px");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: -10px", "-10px");
    /* Not the family's vocabulary. */
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: normal", "not a length, a percentage or auto");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: min-content", "not a length, a percentage or auto");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: red", "not a length, a percentage or auto");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: 1foo", "unknown length unit");
    CssComputeEquals(RoboCssPropertyComputationEdgeOffset, "m: 10px 20px", "not a single length component");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationEdgeOffset,
                                       "m: auto"),
                      "keyword");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationEdgeOffset,
                                       "m: 50%"),
                      "percentage");

    /* --- padding: the same, minus `auto` --- */
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: 10px", "10px");
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: 2.54cm", "96px");
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: 12.5%", "12.5%");
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: 0", "0px");
    /* The assertion the family exists for: a padding has nothing to distribute,
     * so `auto` is not a value it has. */
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: auto", "not a padding length");
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: normal", "not a padding length");
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: red", "not a padding length");
    CssComputeEquals(RoboCssPropertyComputationEdgeInset, "p: 1foo", "unknown length unit");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationEdgeInset,
                                       "p: auto"),
                      "invalid");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationEdgeInset,
                                       "p: 10px"),
                      "length");

    /* --- border width: a length, or one of the three keywords --- */
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: 2px", "2px");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: 1in", "96px");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: thin", "thin");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: medium", "medium");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: thick", "thick");
    /* The keywords stay keywords: what `medium` is in pixels depends on the
     * border's style and the device, and that is a used value, not this one. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLineWidth,
                                       "b: medium"),
                      "keyword");
    /* A negative width is not legal CSS... but this layer knows a value's *shape*,
     * not a property's range, and 9.8's contract says a length is a length. The
     * assertion records that boundary rather than pretending it is enforced here. */
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: -1px", "-1px");
    /* Not a width. */
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: auto", "not a border width keyword");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: 50%", "50%");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: thin thick", "not a single length component");

    /* The name of each shape is what a diagnostic prints for it. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationEdgeOffset),
                      "edge offset");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationEdgeInset),
                      "edge inset");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationLineWidth),
                      "line width");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)RoboCssPropertyComputationCount),
                      0);
}

/* ------------------------------------------------------------------ */
/* Integration: a shorthand through a real element                     */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void CssBoxComputeOf(CssInheritFixture *fixture,
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
 * A shorthand arrives at four computed longhands, and the shorthand itself is
 * still there.
 *
 * The four are the point of the phase: everything downstream is written against
 * the longhands, so `padding: 8px 12px` has to *become* four values rather than
 * stay one declaration nothing can consume. The shorthand's own entry staying put
 * is the other half of the same decision — the author's text is not destroyed on
 * the way, and a caller reading `padding` sees what was written.
 */
static void TestBoxComputedLonghands(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *box = NULL;
    CssComputedStyle *single = NULL;
    CssElementRef div;
    CssElementRef uniform;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "box", NULL);
    uniform = CssHostAdd(CssInheritHost(&fixture), "div", "even", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#box { padding: 8px 12px 4px; border-width: 1px 2px;"
                      " margin: 10px; box-sizing: border-box }"
                      "#even { margin: 0 auto }");
    RoboCheckEqInt(CssComputedStyleCreate(&box, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&single, NULL), RoboOk);
    CssBoxComputeOf(&fixture, div, box);
    CssBoxComputeOf(&fixture, uniform, single);

    /* Three components: top 8, right/left 12, bottom 4. */
    CssComputedIs(box, "padding-top", "8px");
    CssComputedIs(box, "padding-right", "12px");
    CssComputedIs(box, "padding-bottom", "4px");
    CssComputedIs(box, "padding-left", "12px");
    CssComputedFlagsAre(box, "padding-left", "declared");
    /* Two: vertical, then horizontal. */
    CssComputedIs(box, "border-top-width", "1px");
    CssComputedIs(box, "border-right-width", "2px");
    CssComputedIs(box, "border-bottom-width", "1px");
    CssComputedIs(box, "border-left-width", "2px");
    /* One: all four. */
    CssComputedIs(box, "margin-top", "10px");
    CssComputedIs(box, "margin-left", "10px");
    /* The shorthand's own entry survives, unexpanded and uninterpreted — the
     * author's text is not destroyed on the way to the four longhands. */
    CssComputedIs(box, "padding", "8px 12px 4px");
    CssComputedIs(box, "border-width", "1px 2px");
    /* `box-sizing` is a value model with no layout behind it: the keyword is what
     * a declaration means here, and which box a width applies to is a later
     * phase's question. */
    CssComputedIs(box, "box-sizing", "border-box");
    /* `margin: 0 auto` is the two-component form, and `auto` survives as the
     * keyword it is rather than becoming a number. */
    CssComputedIs(single, "margin-top", "0px");
    CssComputedIs(single, "margin-right", "auto");
    CssComputedIs(single, "margin-left", "auto");

    CssComputedStyleDestroy(single);
    CssComputedStyleDestroy(box);
    CssInheritEnd(&fixture);
}

/**
 * One edge, spoken for twice — and the cascade decides which time counts.
 *
 * This is the assertion the phase's shorthand work exists for. The two orderings
 * below are the same two declarations with their order swapped, and CSS's answer
 * is different for each: document order is the last of the three keys, so within
 * one rule it is simply "later wins". Nothing in the box model weighs a level or
 * compares a specificity to work that out; it asks the cascade the same question
 * it asks for a single property.
 */
static void TestBoxCascadeShorthandVersusLonghand(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *longhandLater = NULL;
    CssComputedStyle *shorthandLater = NULL;
    CssComputedStyle *zeroThenLonghand = NULL;
    CssElementRef a;
    CssElementRef b;
    CssElementRef c;

    CssFixture();
    CssInheritBegin(&fixture);
    a = CssHostAdd(CssInheritHost(&fixture), "div", "a", NULL);
    b = CssHostAdd(CssInheritHost(&fixture), "div", "b", NULL);
    c = CssHostAdd(CssInheritHost(&fixture), "div", "c", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      /* The longhand is later, so it wins that one edge only. */
                      "#a { margin: 10px; margin-left: 20px }"
                      /* The shorthand is later, so it overwrites the longhand. */
                      "#b { margin-left: 20px; margin: 10px }"
                      /* And a shorthand reset only erases what came before it. */
                      "#c { margin: 10px; margin: 0; margin-top: 5px }");
    RoboCheckEqInt(CssComputedStyleCreate(&longhandLater, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&shorthandLater, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&zeroThenLonghand, NULL), RoboOk);
    CssBoxComputeOf(&fixture, a, longhandLater);
    CssBoxComputeOf(&fixture, b, shorthandLater);
    CssBoxComputeOf(&fixture, c, zeroThenLonghand);

    CssComputedIs(longhandLater, "margin-top", "10px");
    CssComputedIs(longhandLater, "margin-right", "10px");
    CssComputedIs(longhandLater, "margin-bottom", "10px");
    CssComputedIs(longhandLater, "margin-left", "20px");

    CssComputedIs(shorthandLater, "margin-top", "10px");
    CssComputedIs(shorthandLater, "margin-left", "10px");

    CssComputedIs(zeroThenLonghand, "margin-top", "5px");
    CssComputedIs(zeroThenLonghand, "margin-right", "0px");
    CssComputedIs(zeroThenLonghand, "margin-bottom", "0px");
    CssComputedIs(zeroThenLonghand, "margin-left", "0px");

    CssComputedStyleDestroy(zeroThenLonghand);
    CssComputedStyleDestroy(shorthandLater);
    CssComputedStyleDestroy(longhandLater);
    CssInheritEnd(&fixture);
}

/**
 * Specificity, origin and `!important`, across a shorthand and a longhand.
 *
 * The cascade's three keys are not restated by the box model, so what is asserted
 * here is that they still decide the edge when the two competing declarations
 * carry *different property names*. Every one of these would be got wrong by an
 * implementation that resolved a longhand from its own declaration and then let a
 * shorthand overwrite it unconditionally.
 */
static void TestBoxCascadeSpecificityAndImportance(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *box = NULL;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "panel", "box");
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      /* A type selector loses to an id selector, whichever of the
                       * two is the shorthand and whichever came first. */
                      "div { margin-left: 20px }"
                      "#panel { margin: 10px }"
                      /* An author declaration cannot outrank an important one, and
                       * the important longhand here comes *before* the shorthand. */
                      "#panel { padding: 1px !important }"
                      "#panel { padding-top: 9px }");
    RoboCheckEqInt(CssComputedStyleCreate(&box, NULL), RoboOk);
    CssBoxComputeOf(&fixture, div, box);

    CssComputedIs(box, "margin-left", "10px");
    CssComputedIs(box, "margin-top", "10px");
    CssComputedIs(box, "padding-top", "1px");
    CssComputedIs(box, "padding-left", "1px");

    CssComputedStyleDestroy(box);
    CssInheritEnd(&fixture);
}

/**
 * The CSS-wide keywords, on a shorthand and on a longhand.
 *
 * `margin: inherit` and `margin-left: inherit` mean the same thing to all four
 * edges, and that is by construction rather than by two implementations agreeing:
 * a distributed longhand goes through the same keyword handling a declared one
 * does. `initial` and `unset` land on the longhand's own initial — zero — and not
 * on the parent's spacing, which is the only correct answer for a property that
 * does not inherit.
 */
static void TestBoxKeywords(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parent = NULL;
    CssComputedStyle *kid = NULL;
    CssElementRef outer;
    CssElementRef child;

    CssFixture();
    CssInheritBegin(&fixture);
    outer = CssHostAdd(CssInheritHost(&fixture), "div", "outer", NULL);
    child = CssHostAdd(CssInheritHost(&fixture), "div", "inner", NULL);
    CssHostSetParent(CssInheritHost(&fixture), child, outer);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#outer { margin: 25px }"
                      "#inner { margin: inherit; padding: initial;"
                      " border-width: unset }");
    RoboCheckEqInt(CssComputedStyleCreate(&parent, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&kid, NULL), RoboOk);
    CssBoxComputeOf(&fixture, outer, parent);
    CssBoxComputeOf(&fixture, child, kid);

    CssComputedIs(parent, "margin-left", "25px");
    /* `inherit` on the shorthand is the parent's *computed* value for every edge,
     * which is `25px` and not the text `25px` — the two would differ the moment the
     * parent wrote `1in`. */
    CssComputedIs(kid, "margin-top", "25px");
    CssComputedIs(kid, "margin-left", "25px");
    CssComputedFlagsAre(kid, "margin-left", "inherited");
    /* `initial` and `unset` on a non-inherited property both land on the edge's own
     * initial value: zero for a padding, `medium` for a border width. */
    CssComputedIs(kid, "padding-top", "0px");
    CssComputedFlagsAre(kid, "padding-top", "default");
    CssComputedIs(kid, "border-top-width", "medium");
    CssComputedFlagsAre(kid, "border-top-width", "default");

    CssComputedStyleDestroy(kid);
    CssComputedStyleDestroy(parent);
    CssInheritEnd(&fixture);
}

/**
 * A box has edges nobody mentioned, and they are absent rather than invented.
 *
 * `margin-top: auto` on every element because nobody declared it would put a value
 * in a resolution that no input produced — which is the same rule the inherited
 * properties have had since 9.7, applied to the family that must not propagate.
 * The *initial value* is still reachable, through the registry, which is where a
 * property's initial value belongs.
 */
static void TestBoxNonInheritance(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle parent = RoboCssCleared;
    CssInheritedStyle child = RoboCssCleared;
    CssComputedStyle *kid = NULL;
    CssCascade parentCascade = RoboCssCleared;
    CssCascade childCascade = RoboCssCleared;
    CssElementRef outer;
    CssElementRef inner;

    CssFixture();
    CssInheritBegin(&fixture);
    outer = CssHostAdd(CssInheritHost(&fixture), "div", "outer", NULL);
    inner = CssHostAdd(CssInheritHost(&fixture), "div", "inner", NULL);
    CssHostSetParent(CssInheritHost(&fixture), inner, outer);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#outer { margin: 10px; padding: 10px; width: 100px }");
    RoboCssCascadeCollect(&fixture.cascade, outer, &parentCascade, NULL);
    RoboCssCascadeCollect(&fixture.cascade, inner, &childCascade, NULL);
    CssInheritFixtureResolve(&fixture, &parentCascade, NULL, &parent);
    CssInheritFixtureResolve(&fixture, &childCascade, &parent, &child);

    /* The parent has all four edges; the child has none of them, by name. */
    CssInheritIs(&parent, "margin-left", "10px", "declared");
    CssInheritIs(&child, "margin-left", "<absent>", "<absent>");
    CssInheritIs(&child, "padding-top", "<absent>", "<absent>");
    CssInheritIs(&child, "width", "<absent>", "<absent>");
    CssInheritIs(&child, "box-sizing", "<absent>", "<absent>");
    /* An inherited property would have come across — from the parent's own
     * default, which is why its provenance is the parent and not the registry —
     * and that is what makes the absences above a *classification* rather than
     * an empty result. */
    CssInheritIs(&child, "color", "black", "parent");

    RoboCheckEqInt(CssComputedStyleCreate(&kid, NULL), RoboOk);
    CssBoxComputeOf(&fixture, inner, kid);
    /* The initial value is still reachable — from the registry, not from the
     * parent's box. */
    {
        CssComputedValue fromInitial = RoboCssCleared;
        int initial = 0;
        size_t written = 0u;
        char shape[RoboCssComputedValueTextMax];

        RoboCheckEqInt(CssStyleContextQueryComputedValue(
                              CssInheritContext(&fixture), kid,
                              CssSliceFromText("margin-left"), &fromInitial,
                              &initial, NULL),
                          RoboOk);
        RoboCheckEqInt(initial, 1);
        RoboCheckEqInt(CssComputedValueWrite(&fromInitial, shape,
                                                        sizeof(shape), &written, NULL),
                          RoboOk);
        RoboCheckStrEq(shape, "0px");
        CssComputedValueDispose(&fromInitial);
    }

    CssComputedStyleDestroy(kid);
    CssInheritedStyleDispose(&child);
    CssInheritedStyleDispose(&parent);
    CssCascadeDispose(&childCascade);
    CssCascadeDispose(&parentCascade);
    CssInheritEnd(&fixture);
}

/**
 * A shorthand that cannot be distributed is *reported*, not silently skipped.
 *
 * The failing case is the quiet one: the shorthand's own entry is there with the
 * author's text intact, so a consumer reading the style would otherwise see a
 * declaration, no longhands, and no way to tell that anything was left undone.
 */
static void TestBoxUnexpandedShorthand(void)
{
    CssInheritFixture fixture;
    CssInheritedStyle resolved = RoboCssCleared;
    CssCascade cascade = RoboCssCleared;
    CssElementRef div;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "box", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#box { margin: 1px 2px 3px 4px 5px; padding: var(--edges) }");
    RoboCssCascadeCollect(&fixture.cascade, div, &cascade, NULL);
    CssInheritFixtureResolve(&fixture, &cascade, NULL, &resolved);

    /* The shorthand's own entry is intact — the author's declaration is never
     * thrown away — and no longhand was invented from it. */
    CssInheritIs(&resolved, "margin", "1px,_,2px,_,3px,_,4px,_,5px", "declared");
    CssInheritIs(&resolved, "margin-top", "<absent>", "<absent>");
    CssInheritIs(&resolved, "padding", "var(--edges)", "declared");
    CssInheritIs(&resolved, "padding-left", "<absent>", "<absent>");
    /* Two shorthands, two counts, and the result is marked incomplete because
     * eight edges are unanswerable. */
    RoboCheckEqInt((int)CssInheritedStyleShorthandsUnexpanded(&resolved),
                      2);
    RoboCheckEqInt(CssInheritedStyleIsComplete(&resolved), 0);

    CssInheritedStyleDispose(&resolved);
    CssCascadeDispose(&cascade);

    CssInheritEnd(&fixture);

    /* A shorthand that *does* distribute leaves the counter at zero. Its own
     * fixture, because a context's sheets are shared by every element in it and
     * the two failing declarations above are still in the first one. */
    {
        CssInheritFixture clean;
        CssCascade okCascade = RoboCssCleared;
        CssInheritedStyle ok = RoboCssCleared;
        CssElementRef cleanDiv;

        CssInheritBegin(&clean);
        cleanDiv = CssHostAdd(CssInheritHost(&clean), "div", "box", NULL);
        CssHostSeal(CssInheritHost(&clean));
        CssInheritSheet(&clean, RoboCssOriginAuthor,
                          "#box { margin: 1px 2px 3px 4px; padding: 10px 20px;"
                          " border-width: thin thick thin thick }");
        RoboCssCascadeCollect(&clean.cascade, cleanDiv, &okCascade, NULL);
        CssInheritFixtureResolve(&clean, &okCascade, NULL, &ok);

        RoboCheckEqInt(
            (int)CssInheritedStyleShorthandsUnexpanded(&ok), 0);
        RoboCheckEqInt(CssInheritedStyleIsComplete(&ok), 1);
        /* All three shorthands distributed, each arity arriving at the right
         * edges — which is what makes the zero above a statement about the
         * distribution and not about the counter never being written. */
        CssInheritIs(&ok, "margin-left", "4px", "declared");
        CssInheritIs(&ok, "padding-top", "10px", "declared");
        CssInheritIs(&ok, "padding-left", "20px", "declared");
        CssInheritIs(&ok, "border-top-width", "thin", "declared");
        CssInheritIs(&ok, "border-left-width", "thick", "declared");

        CssInheritedStyleDispose(&ok);
        CssCascadeDispose(&okCascade);
        CssInheritEnd(&clean);
    }
}

/* ------------------------------------------------------------------ */
/* Robustness and determinism                                          */
/* ------------------------------------------------------------------ */

/**
 * Malformed and hostile input, none of which may crash or invent a value.
 *
 * The shorthand vocabulary is reached through declaration text, so the values
 * below are the tokenizer's output rather than hand-built trees — which is the
 * input a real sheet can actually produce.
 */
static void TestBoxRobust(void)
{
    static const char *const texts[] = {
        "m:",
        "m: ;",
        "m: 1px 2px 3px 4px 5px 6px 7px 8px",
        "m: var()",
        "m: var(--x, 1px)",
        "m: calc()",
        "m: (1px)",
        "m: [1px 2px]",
        "m: \"1px 2px\"",
        "m: 1px/2px",
        "m: 999999999999999999999999999999px",
        "m: -0",
        "m: 1e309px",
        "m: #fff #000",
        "m: 1px !important",
    };
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(texts) / sizeof(texts[0]); index++) {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;
        size_t declarationIndex;

        /* Parsing may recover and may report; neither is a crash and neither is
         * asserted away, because the point is that the walk below is safe. */
        RoboCheckEqInt(CssParseDeclarations(texts[index], &list, &error), RoboOk);
        for (declarationIndex = 0u;
             declarationIndex < CssDeclarationListCount(&list);
             declarationIndex++) {
            const CssValue *value = CssDeclarationValue(
                CssDeclarationListAt(&list, declarationIndex));
            size_t count = CssBoxComponentCount(value);
            size_t reported = 12345u;
            int expandable = CssBoxIsExpandable(value, &reported);
            CssComputedValue computed = RoboCssCleared;

            /* The count is never past the bound that has a mapping, and the
             * reported count is only written for the answer that applies it. */
            RoboCheck(count <= 8u);
            RoboCheckEqInt((int)reported, expandable ? (int)count : 0);
            /* Every shape either produces a value or refuses; nothing crashes. */
            RoboCheckEqInt(CssComputedValueCompute(
                                  RoboCssPropertyComputationEdgeOffset, value,
                                  &computed, NULL),
                              RoboOk);
            CssComputedValueDispose(&computed);
            RoboCheckEqInt(CssComputedValueCompute(
                                  RoboCssPropertyComputationEdgeInset, value,
                                  &computed, NULL),
                              RoboOk);
            CssComputedValueDispose(&computed);
            RoboCheckEqInt(CssComputedValueCompute(
                                  RoboCssPropertyComputationLineWidth, value,
                                  &computed, NULL),
                              RoboOk);
            CssComputedValueDispose(&computed);
        }
        CssDeclarationListDispose(&list);
    }
}

/**
 * The same inputs produce the same box, every time.
 *
 * Determinism over a document rather than over a value: two independent
 * computations of one element must agree on all four edges, and the shorthand
 * table must answer identically when walked forwards and backwards — an
 * order-dependent vocabulary would show up as a transposed edge somewhere.
 */
static void TestBoxDeterminism(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *first = NULL;
    CssComputedStyle *second = NULL;
    CssElementRef div;
    static const char *const names[] = {
        "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width"
    };
    size_t index;
    int reverse;

    CssFixture();
    CssInheritBegin(&fixture);
    div = CssHostAdd(CssInheritHost(&fixture), "div", "app", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#app { margin: 1px 2px 3px 4px; padding: 10%;"
                      " border-width: thin thick }");
    RoboCheckEqInt(CssComputedStyleCreate(&first, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&second, NULL), RoboOk);
    CssBoxComputeOf(&fixture, div, first);
    CssBoxComputeOf(&fixture, div, second);

    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        char one[RoboCssComputedValueTextMax];
        char two[RoboCssComputedValueTextMax];

        CssComputedNote(first, names[index], one, sizeof(one));
        CssComputedNote(second, names[index], two, sizeof(two));
        RoboCheckStrEq(one, two);
    }

    CssComputedStyleDestroy(second);
    CssComputedStyleDestroy(first);
    CssInheritEnd(&fixture);

    /* Walked backwards, the vocabulary answers the same questions. */
    for (reverse = (int)CssBoxShorthandCount() - 1; reverse >= 0; reverse--) {
        const CssBoxShorthand *row =
            CssBoxShorthandAt((size_t)reverse);
        size_t sideIndex;

        RoboCheck(row != NULL);
        if (!row) {
            continue;
        }
        for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
             sideIndex++) {
            const char *name = CssBoxShorthandLonghand(
                row, (CssBoxSide)sideIndex);
            CssBoxSide side = RoboCssBoxSideTop;

            RoboCheck(CssBoxShorthandOfLonghand(
                           CssSliceFromText(name), &side) == row);
            RoboCheckEqInt((int)side, (int)sideIndex);
        }
    }
}

/* ------------------------------------------------------------------ */

void TestRegisterCssBox(void);

void TestRegisterCssBox(void)
{
    TestRegister("css.box.side_vocabulary", TestBoxSideVocabulary);
    TestRegister("css.box.shorthand_table", TestBoxShorthandTable);
    TestRegister("css.box.component_mapping", TestBoxComponentMapping);
    TestRegister("css.box.expansion_refusals", TestBoxExpansionRefusals);
    TestRegister("css.box.shapes", TestBoxShapes);
    TestRegister("css.box.computed.longhands", TestBoxComputedLonghands);
    TestRegister("css.box.cascade.shorthand_versus_longhand",
                       TestBoxCascadeShorthandVersusLonghand);
    TestRegister("css.box.cascade.specificity_and_importance",
                       TestBoxCascadeSpecificityAndImportance);
    TestRegister("css.box.keywords", TestBoxKeywords);
    TestRegister("css.box.non_inheritance", TestBoxNonInheritance);
    TestRegister("css.box.unexpanded_shorthand", TestBoxUnexpandedShorthand);
    TestRegister("css.box.robust", TestBoxRobust);
    TestRegister("css.box.determinism", TestBoxDeterminism);
}
