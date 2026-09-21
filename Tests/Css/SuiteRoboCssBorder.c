/*
 * RoboCSS border suite (Phase 9.15) — the style vocabulary, the three kinds over
 * four sides, the classifier, the five distributing shorthands, and what the
 * cascade makes of a longhand four declarations can set.
 *
 * Five groups, in the order the phase is built:
 *
 *   the vocabularies  the ten styles and the three kinds walked rather than a
 *                     representative typed out, with the style rows' one fact
 *                     (`generates_border`) asserted per row and the twelve
 *                     longhand names reached through the walk rather than listed
 *   the classifier    what one component of a composite shorthand is, and the
 *                     *order* the questions are asked in, which is the whole logic
 *   the expansion     one `border:` value split into three parts, the reset a
 *                     kind the author did not write takes, and every refusal
 *   the shorthands    the three positional rows through the box table's 1/2/3/4
 *                     mapping, and the five composites through the registry
 *   integration       a real element: initial values through the query door,
 *                     non-inheritance proved from both sides, `inherit`,
 *                     `initial` and `unset`, one longhand set by four different
 *                     declarers at four different keys, and the reset semantics
 *
 * Nothing here paints anything. A border at this phase is twelve *values* with
 * shapes: `2px` stays a length, `medium` stays a keyword, `none` is a value rather
 * than an absent border, and `currentcolor` stays a reference until the one layer
 * that has an element resolves it.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The style vocabulary                                                */
/* ------------------------------------------------------------------ */

/**
 * The ten values, and what each is spelled as.
 *
 * The spelling is the assertion rather than the enumeration: a table whose rows
 * and enumerators had drifted would still answer `IsKnown` for all ten and would
 * only show as a value that printed as another one's keyword. Walking the set in
 * enumerator order and reading every name back is what makes that visible.
 */
static void TestBorderStyleVocabulary(void)
{
    static const char *const keywords[] = {
        "none", "hidden", "dotted", "dashed", "solid",
        "double", "groove", "ridge", "inset", "outset"
    };
    size_t index;
    char buffer[32];
    size_t length = 0u;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBorderStyleCount, 10);
    RoboCheckEqInt((int)CssBorderStyleCount(), 10);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        CssBorderStyle style = (CssBorderStyle)index;

        RoboCheckEqInt(CssBorderStyleIsKnown(style), 1);
        RoboCheckStrEq(CssBorderStyleName(style), keywords[index]);
        /* Read *back*: the keyword must name the same enumerator it came from. */
        RoboCheckEqInt(CssBorderStyleFromText(keywords[index]), style);
        RoboCheckEqInt(CssBorderStyleFromSlice(
                              CssSliceFromText(keywords[index])),
                          style);
        RoboCheckEqInt(CssBorderStyleSliceIsKnown(
                              CssSliceFromText(keywords[index])),
                          1);
        /* CSS compares keywords ASCII case-insensitively, so `SOLID` is `solid` —
         * and the *read-back* is what says so: knowness alone is true of all ten
         * rows and would pass for a table whose names had drifted. */
        RoboCheckEqInt(CssBorderStyleSliceIsKnown(
                              CssSliceFromText("SOLID")),
                          1);
        RoboCheckEqInt(CssBorderStyleFromSlice(
                              CssSliceFromText("SOLID")),
                          RoboCssBorderStyleSolid);
        RoboCheckEqInt(CssBorderStyleWrite(style, buffer, sizeof(buffer),
                                                     &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, keywords[index]);
        RoboCheckEqInt((int)length, (int)strlen(keywords[index]));
    }

    /* An out-of-range enumerator answers *nothing* rather than the first row — the
     * difference between a table lookup and an index into it. */
    RoboCheckEqInt(CssBorderStyleIsKnown((CssBorderStyle)10), 0);
    RoboCheckStrEq(CssBorderStyleName((CssBorderStyle)10),
                      "unknown");
    RoboCheckStrEq(CssBorderStyleName((CssBorderStyle)-1),
                      "unknown");
    RoboCheckEqInt(CssBorderStyleSliceIsKnown(
                          CssSliceFromText("banana")),
                      0);
    RoboCheckEqInt(CssBorderStyleSliceIsKnown(
                          CssSliceFromText("")),
                      0);
}

/**
 * `none` is both a value and the answer for "not one of them".
 *
 * The trap the display and position vocabularies each recorded, in its third
 * place: a *check* on the result would accept `border-style: banana` as
 * `border-style: none`, which is the most damaging confusion this property has —
 * `none` is not a spelling mistake, it is "this border draws nothing".
 */
static void TestBorderStyleNoneTrap(void)
{
    CssFixture();
    RoboCheckEqInt(CssBorderStyleFromText("banana"),
                      RoboCssBorderStyleNone);
    /* ...and the door that tells them apart. */
    RoboCheckEqInt(CssBorderStyleSliceIsKnown(
                          CssSliceFromText("banana")),
                      0);
    RoboCheckEqInt(CssBorderStyleSliceIsKnown(
                          CssSliceFromText("none")),
                      1);
    RoboCheckEqInt(CssBorderStyleFromText(NULL),
                      RoboCssBorderStyleNone);
}

/**
 * Which styles draw anything.
 *
 * The one fact about a style a later phase must have, asserted per row: `none` and
 * `hidden` draw nothing — and a border that draws nothing has a *used* width of
 * zero whatever `border-width` says — while the other eight all draw. The two are
 * kept apart as values because `hidden` outranks every other style in border
 * collapsing, which is a table rule this build has no tables for; they answer this
 * one question the same way, and the assertion says so rather than leaving it to a
 * reader to notice.
 */
static void TestBorderStyleGenerates(void)
{
    static const CssBorderStyle silent[] = {
        RoboCssBorderStyleNone, RoboCssBorderStyleHidden
    };
    size_t index;
    size_t silentCount = 0u;

    CssFixture();
    for (index = 0u; index < CssBorderStyleCount(); index++) {
        CssBorderStyle style = (CssBorderStyle)index;

        if (!CssBorderStyleGeneratesBorder(style)) {
            silentCount++;
            RoboCheckEqInt(style, silent[silentCount - 1u]);
        }
    }
    /* Exactly the two, in order, so a style added later cannot arrive silent by
     * accident and cannot arrive drawing without this count moving. */
    RoboCheckEqInt((int)silentCount, 2);
    for (index = 0u; index < sizeof(silent) / sizeof(silent[0]); index++) {
        RoboCheckEqInt(CssBorderStyleGeneratesBorder(silent[index]), 0);
    }
    RoboCheckEqInt(CssBorderStyleGeneratesBorder(
                          RoboCssBorderStyleSolid),
                      1);
    RoboCheckEqInt(CssBorderStyleGeneratesBorder(
                          RoboCssBorderStyleDouble),
                      1);
    /* An unknown enumerator answers *nothing*, not "draws". */
    RoboCheckEqInt(CssBorderStyleGeneratesBorder(
                          (CssBorderStyle)10),
                      0);
}

/** A buffer one byte short reports the length it needed and writes nothing. */
static void TestBorderStyleWrite(void)
{
    char buffer[8];
    size_t length = 99u;
    CssDiag diag = RoboCssCleared;

    CssFixture();
    memset(buffer, '#', sizeof(buffer));
    RoboCheckEqInt(CssBorderStyleWrite(RoboCssBorderStyleDotted,
                                                 buffer, 5u, &length, &diag),
                      RoboNotSupported);
    RoboCheckEqInt((int)length, 6);
    RoboCheckEqInt(diag.domain, RoboCssDomainBorder);

    /* Exactly enough succeeds, NUL included. */
    RoboCheckEqInt(CssBorderStyleWrite(RoboCssBorderStyleDotted,
                                                 buffer, 7u, &length, NULL),
                      RoboOk);
    RoboCheckStrEq(buffer, "dotted");

    /* The argument checks come first. */
    RoboCheckEqInt(CssBorderStyleWrite(RoboCssBorderStyleSolid, NULL,
                                                 8u, &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBorderStyleWrite(RoboCssBorderStyleSolid, buffer,
                                                  sizeof(buffer), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssBorderStyleWrite((CssBorderStyle)10, buffer,
                                                 sizeof(buffer), &length, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The kinds and the twelve longhands                                  */
/* ------------------------------------------------------------------ */

/**
 * The three kinds, and the twelve names reached through the walk.
 *
 * The names are read out of the box table's positional rows rather than listed
 * here, which is what makes this a *family* assertion: for every side and every
 * kind there is a name, every name is one the box table owns, and the three rows
 * between them own exactly the twelve. A row written against the wrong side or a
 * kind whose row was never added cannot pass.
 */
static void TestBorderKinds(void)
{
    static const char *const kindNames[] = { "width", "style", "color" };
    size_t kindIndex;
    size_t sideIndex;
    size_t seen = 0u;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBorderKindCount, 3);
    RoboCheckEqInt((int)CssBorderKindCount(), 3);
    RoboCheckEqInt((int)CssBorderLonghandCount(), 12);
    for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
         kindIndex++) {
        RoboCheckEqInt(CssBorderKindIsKnown(
                              (CssBorderKind)kindIndex),
                          1);
        RoboCheckStrEq(CssBorderKindName(
                              (CssBorderKind)kindIndex),
                          kindNames[kindIndex]);
        /* The positional row exists, and its *kind* is the index it sits at — the
         * one thing this module states about the relation between the two. */
        RoboCheck(CssBorderPositionalRowOfKind(
                       (CssBorderKind)kindIndex) != NULL);
        RoboCheckEqInt(CssBorderPositionalKind(kindIndex), kindIndex);
    }
    RoboCheckEqInt(CssBorderKindIsKnown((CssBorderKind)3), 0);
    RoboCheckStrEq(CssBorderKindName((CssBorderKind)3),
                      "unknown");
    RoboCheck(CssBorderPositionalRowOfKind(
                   (CssBorderKind)3) == NULL);

    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
         sideIndex++) {
        CssBoxSide side = (CssBoxSide)sideIndex;

        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            CssBorderKind kind = (CssBorderKind)kindIndex;
            const char *name = CssBorderLonghandName(side, kind);
            CssBoxSide ownerSide = (CssBoxSide)99;

            RoboCheck(name != NULL);
            if (!name) {
                continue;
            }
            /* Composed of the side name and the kind name, which is what makes a
             * transposition visible: a row written against `right` would produce
             * `border-right-width` where `border-left-width` was asked for. */
            {
                char expected[64];

                (void)snprintf(expected, sizeof(expected), "border-%s-%s",
                               CssBoxSideName(side), kindNames[kindIndex]);
                RoboCheckStrEq(name, expected);
            }
            RoboCheckEqInt(CssBorderIsLonghand(CssSliceFromText(name)),
                              1);
            /* And the *box table* is where it lives, with the side it belongs to. */
            RoboCheck(CssBoxShorthandOfLonghand(CssSliceFromText(name),
                                                         &ownerSide) != NULL);
            RoboCheckEqInt(ownerSide, side);
            seen++;
        }
    }
    RoboCheckEqInt((int)seen, 12);
    RoboCheckEqInt(CssBorderLonghandName(
                          (CssBoxSide)9, RoboCssBorderKindWidth) == NULL,
                      1);
    RoboCheckEqInt(CssBorderLonghandName(
                          RoboCssBoxSideTop, (CssBorderKind)9) == NULL,
                      1);
    RoboCheckEqInt(CssBorderIsLonghand(CssSliceFromText(
                          "border-top")),
                      0);
    RoboCheckEqInt(CssBorderIsLonghand(CssSliceFromText(
                          "margin-top")),
                      0);
    RoboCheckEqInt(CssBorderIsLonghand(CssSliceFromText(
                          "border-top-radius")),
                      0);
}

/**
 * The three positional rows, and whose they are.
 *
 * The box table holds them — that is where the twelve longhand names are written
 * down — and the *border family* distributes them, because a longhand they cover
 * can also be set by a side shorthand and by `border`. This is the predicate the
 * box model's pass asks before it distributes a row, and the answer has to be
 * exactly the three.
 */
static void TestBorderPositionalRows(void)
{
    size_t index;
    size_t found = 0u;

    CssFixture();
    RoboCheckEqInt((int)CssBorderPositionalCount(), 3);
    for (index = 0u; index < CssBoxShorthandCount(); index++) {
        const CssBoxShorthand *row = CssBoxShorthandAt(index);
        int ours = CssBorderDistributesShorthand(
            CssSliceFromText(row->name));

        if (ours) {
            found++;
            RoboCheckEqInt((int)strncmp(row->name, "border-", 7u), 0);
        }
    }
    /* Exactly the three border rows are the family's, and the other two stay the
     * box model's — `margin` and `padding` are walked over the same table and must
     * not be caught by this predicate. */
    RoboCheckEqInt((int)found, 3);
    RoboCheckEqInt(CssBorderDistributesShorthand(
                          CssSliceFromText("margin")),
                      0);
    RoboCheckEqInt(CssBorderDistributesShorthand(
                          CssSliceFromText("padding")),
                      0);
    RoboCheckEqInt(CssBorderDistributesShorthand(
                          CssSliceFromText("border")),
                      0);
    RoboCheckEqInt(CssBorderDistributesShorthand(
                          CssSliceFromText("BORDER-STYLE")),
                      1);
    RoboCheckEqInt(CssBorderDistributesShorthand(CssSliceFromText("")),
                      0);

    for (index = 0u; index < CssBorderPositionalCount(); index++) {
        const CssBoxShorthand *row = CssBorderPositionalRow(index);
        CssBorderKind kind = CssBorderPositionalKind(index);

        RoboCheck(row != NULL);
        RoboCheckStrEq(row->name, CssBorderPositionalName(index));
        RoboCheckEqInt(CssBorderDistributesShorthand(
                              CssSliceFromText(row->name)),
                          1);
        /* The row's longhands are the ones this module hands out for that kind,
         * side for side — the two tables agreeing, which is the whole point of
         * reading one from the other. */
        {
            size_t sideIndex;

            for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
                 sideIndex++) {
                RoboCheckStrEq(
                    CssBorderLonghandName((CssBoxSide)sideIndex, kind),
                    CssBoxShorthandLonghand(row,
                                                    (CssBoxSide)sideIndex));
            }
        }
        RoboCheckEqInt(CssBorderPositionalName(
                              CssBorderPositionalCount()),
                          (const char *)NULL);
        RoboCheck(CssBorderPositionalRow(CssBorderPositionalCount()) ==
                   NULL);
    }
    /* The three names, in kind order — `border-width` first, which is also the
     * order a serialisation would write them in. */
    RoboCheckStrEq(CssBorderPositionalName(0u), "border-width");
    RoboCheckStrEq(CssBorderPositionalName(1u), "border-style");
    RoboCheckStrEq(CssBorderPositionalName(2u), "border-color");
}

/**
 * The five composites, and the side each covers.
 *
 * Index 0 is the whole shorthand and 1..4 are the sides in vocabulary order, so
 * the side is *derived* rather than kept in a second parallel array a walk could
 * fall out of step with. `border` is not a positional row and `border-top` is not
 * a longhand of one, which the assertions say because both are easy to get wrong
 * in the other direction.
 */
static void TestBorderComposites(void)
{
    static const char *const names[] = {
        "border", "border-top", "border-right", "border-bottom", "border-left"
    };
    size_t index;
    size_t sideIndex;

    CssFixture();
    RoboCheckEqInt((int)CssBorderCompositeCount(), 5);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        RoboCheckStrEq(CssBorderCompositeName(index), names[index]);
        RoboCheckEqInt(CssBorderIsComposite(
                              CssSliceFromText(names[index])),
                          1);
        RoboCheckEqInt(CssBorderCompositeIsWhole(index), index == 0u);
        if (index > 0u) {
            RoboCheckEqInt(CssBorderCompositeSide(index),
                              (CssBoxSide)(index - 1u));
            RoboCheckStrEq(CssBoxSideName(
                                  CssBorderCompositeSide(index)),
                              names[index] + 7u);
        }
    }
    RoboCheckEqInt(CssBorderIsComposite(
                          CssSliceFromText("BORDER-TOP")),
                      1);
    RoboCheckEqInt(CssBorderIsComposite(
                          CssSliceFromText("border-width")),
                      0);
    RoboCheckEqInt(CssBorderIsComposite(
                          CssSliceFromText("border-top-width")),
                      0);
    RoboCheckEqInt(CssBorderIsComposite(CssSliceFromText("")), 0);
    RoboCheckEqInt(CssBorderCompositeName(
                          CssBorderCompositeCount()) == NULL,
                      1);

    /* `border` is *not* a positional row: it is not four longhands, it is twelve,
     * and it is not a longhand of anything either. */
    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount; sideIndex++) {
        char name[32];

        (void)snprintf(name, sizeof(name), "border-%s",
                       CssBoxSideName((CssBoxSide)sideIndex));
        RoboCheck(CssBoxShorthandFind(CssSliceFromText(name)) == NULL);
        RoboCheck(CssBoxShorthandOfLonghand(
                       CssSliceFromText(name), NULL) == NULL);
    }
    RoboCheck(CssBoxShorthandFind(CssSliceFromText("border")) == NULL);
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/** The component shape `name: text` produces, or NULL (asserting it parses). */
static const CssValue *CssBorderComponent(const char *text,
                                                    CssDeclarationList *list)
{
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;

    RoboCheckEqInt(CssParseDeclarations(text, list, &error), RoboOk);
    declaration = CssDeclarationListAt(list, 0u);
    RoboCheck(declaration != NULL);
    return declaration ? CssDeclarationValue(declaration) : NULL;
}

/** What `b: text` classifies as, through the real grammar. */
static CssBorderClass CssBorderClassifyOf(const char *text)
{
    CssDeclarationList list = RoboCssCleared;
    const CssValue *component = CssBorderComponent(text, &list);
    CssBorderClass outcome = CssBorderClassify(component);

    CssDeclarationListDispose(&list);
    return outcome;
}

/** Assert `b: @p text` classifies as @p expected (a class *name*). */
static void CssBorderIsClass(const char *text, const char *expected)
{
    RoboCheckStrEq(CssBorderClassName(CssBorderClassifyOf(text)),
                      expected);
}

/**
 * The five outcomes, and the *order* the questions are asked in.
 *
 * The order is the whole logic and it is the vocabulary that forces it: a style
 * keyword before a width keyword before a colour *name*, because `none` and
 * `hidden` are styles, `thin`/`medium`/`thick` are the only identifiers left that
 * are certainly not colours, and after those two an identifier the 9.9 table
 * recognises can only be a colour. Each row below is a near miss that a different
 * order would place somewhere else — `red` is not a style, `medium` is not a
 * colour, `none` is not a width — and `solid` is asserted beside them because it is
 * a style *and* an English word.
 */
static void TestBorderClassify(void)
{
    size_t index;
    static const struct {
        const char *text;
        const char *expected;
    } cases[] = {
        /* --- widths --- */
        { "b: 1px", "width" },
        { "b: 0", "width" },
        { "b: 1.5em", "width" },
        { "b: thin", "width" },
        { "b: medium", "width" },
        { "b: thick", "width" },
        { "b: 50%", "width" },
        { "b: -1px", "width" },
        /* --- styles: all ten, so a style the classifier forgot is a failure --- */
        { "b: none", "style" },
        { "b: hidden", "style" },
        { "b: dotted", "style" },
        { "b: dashed", "style" },
        { "b: solid", "style" },
        { "b: double", "style" },
        { "b: groove", "style" },
        { "b: ridge", "style" },
        { "b: inset", "style" },
        { "b: outset", "style" },
        /* --- colours, in all four of 9.9's spellings --- */
        { "b: red", "color" },
        { "b: transparent", "color" },
        { "b: currentcolor", "color" },
        { "b: #fff", "color" },
        { "b: #aabbccdd", "color" },
        { "b: rgb(10, 20, 30)", "color" },
        { "b: hsl(0 100% 50%)", "color" },
        /* --- deferred: substitution decides, so nothing is guessed --- */
        { "b: var(--w)", "deferred" },
        { "b: env(--w)", "deferred" },
        /* --- nothing this grammar has a place for --- */
        { "b: banana", "invalid" },
        { "b: url(a.png)", "invalid" },
        { "b: translate(1px)", "invalid" },
        { "b: \"solid\"", "invalid" },
        { "b: [solid]", "invalid" },
        { "b: #gg", "invalid" }
    };

    CssFixture();
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        CssBorderIsClass(cases[index].text, cases[index].expected);
    }

    /* A list of more than one component is not *a* component — answering with the
     * first would be answering a question nobody asked. */
    CssBorderIsClass("b: solid dashed", "invalid");
    /* And a list of exactly one is unwrapped, because that is what a declaration
     * with one component is. */
    CssBorderIsClass("b: 1px", "width");
    /* A list whose one significant child is buried in trivia is one component. */
    CssBorderIsClass("b:   solid  ", "style");

    /* The vocabulary doors. */
    RoboCheckEqInt((int)CssBorderClassCount(), 5);
    for (index = 0u; index < CssBorderClassCount(); index++) {
        RoboCheckEqInt(CssBorderClassIsKnown(
                              (CssBorderClass)index),
                          1);
        RoboCheck(CssBorderClassName((CssBorderClass)index) != NULL);
    }
    RoboCheckEqInt(CssBorderClassIsKnown((CssBorderClass)5), 0);
    RoboCheckStrEq(CssBorderClassName((CssBorderClass)5),
                      "unknown");
    /* The names, in enumerator order — a diagnostic prints these. */
    RoboCheckStrEq(CssBorderClassName(RoboCssBorderClassInvalid),
                      "invalid");
    RoboCheckStrEq(CssBorderClassName(RoboCssBorderClassWidth),
                      "width");
    RoboCheckStrEq(CssBorderClassName(RoboCssBorderClassStyle),
                      "style");
    RoboCheckStrEq(CssBorderClassName(RoboCssBorderClassColor),
                      "color");
    RoboCheckStrEq(CssBorderClassName(RoboCssBorderClassDeferred),
                      "deferred");
    /* A NULL component is INVALID rather than a crash. */
    RoboCheckEqInt(CssBorderClassify(NULL), RoboCssBorderClassInvalid);
}

/* ------------------------------------------------------------------ */
/* The expansion                                                       */
/* ------------------------------------------------------------------ */

/** `b: text` split into three parts, written as `width|style|color`. */
static void CssBorderExpandIs(const char *text, const char *expected)
{
    CssDeclarationList list = RoboCssCleared;
    CssValue parts[RoboCssBorderKindCount];
    const CssValue *value = CssBorderComponent(text, &list);
    char shape[128];
    size_t offset = 0u;
    size_t kindIndex;

    for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
         kindIndex++) {
        CssValueInit(&parts[kindIndex]);
    }
    shape[0] = '\0';
    RoboCheckEqInt(CssBorderExpand(value, parts, NULL), RoboOk);
    for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
         kindIndex++) {
        if (kindIndex > 0u) {
            CssShapeByte(shape, sizeof(shape), &offset, (uint8_t)'|');
        }
        if (parts[kindIndex].kind == RoboCssValueKindNone) {
            /* An *absent* kind, which is not the empty string: the caller applies
             * the longhand's initial value, and the assertion has to be able to
             * tell that from "a component that happened to print as nothing". */
            CssShapeText(shape, sizeof(shape), &offset, "<absent>");
        } else {
            CssShapeWrite(&parts[kindIndex], shape, sizeof(shape), &offset);
        }
    }
    RoboCheckStrEq(shape, expected);

    for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
         kindIndex++) {
        CssValueDispose(&parts[kindIndex]);
    }
    CssDeclarationListDispose(&list);
}

/**
 * `b: @p text` expanded, written as each slot's *class* name (`-` when absent).
 *
 * The companion to the shape above for the components whose rendering is their
 * token stream rather than a value: a colour function keeps the tokens the grammar
 * gave it, so "which slot did this land in" is the assertion that says something.
 */
static void CssBorderExpandClassesIs(const char *text, const char *expected)
{
    CssDeclarationList list = RoboCssCleared;
    CssValue parts[RoboCssBorderKindCount];
    const CssValue *value = CssBorderComponent(text, &list);
    char shape[64];
    size_t offset = 0u;
    size_t kindIndex;

    for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
         kindIndex++) {
        CssValueInit(&parts[kindIndex]);
    }
    shape[0] = '\0';
    RoboCheckEqInt(CssBorderExpand(value, parts, NULL), RoboOk);
    for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
         kindIndex++) {
        if (kindIndex > 0u) {
            CssShapeByte(shape, sizeof(shape), &offset, (uint8_t)'|');
        }
        if (parts[kindIndex].kind == RoboCssValueKindNone) {
            CssShapeByte(shape, sizeof(shape), &offset, (uint8_t)'-');
        } else {
            CssShapeText(shape, sizeof(shape), &offset,
                           CssBorderClassName(
                               CssBorderClassify(&parts[kindIndex])));
        }
    }
    RoboCheckStrEq(shape, expected);

    for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
         kindIndex++) {
        CssValueDispose(&parts[kindIndex]);
    }
    CssDeclarationListDispose(&list);
}

/**
 * One `border:` value split into three, and the reset a missing kind takes.
 *
 * `<absent>` is the important column: `border: 1px solid` says nothing about the
 * colour, and what that means is not "leave it alone" — the shorthand stands for
 * all three kinds, so an unnamed kind takes the longhand's *initial* value. The
 * function deliberately does not supply it; the integration group below is where
 * the reset is asserted end to end.
 */
static void TestBorderExpand(void)
{
    size_t index;

    CssFixture();
    /* All three, in either order — the classifier's order, not the author's. */
    CssBorderExpandIs("b: 1px solid red", "1px|solid|red");
    CssBorderExpandIs("b: solid 1px red", "1px|solid|red");
    CssBorderExpandIs("b: red solid 1px", "1px|solid|red");
    /* Two of the three. */
    CssBorderExpandIs("b: 1px solid", "1px|solid|<absent>");
    CssBorderExpandIs("b: solid", "<absent>|solid|<absent>");
    CssBorderExpandIs("b: none", "<absent>|none|<absent>");
    CssBorderExpandIs("b: 2px", "2px|<absent>|<absent>");
    CssBorderExpandIs("b: red", "<absent>|<absent>|red");
    CssBorderExpandIs("b: 1px red", "1px|<absent>|red");
    /* One of each, and every spelling of a colour takes the colour slot. */
    CssBorderExpandIs("b: #fff", "<absent>|<absent>|#fff");
    CssBorderExpandIs("b: transparent", "<absent>|<absent>|transparent");
    CssBorderExpandIs("b: currentcolor", "<absent>|<absent>|currentcolor");
    /* A *functional* colour reaches the colour slot, and the slot holds the
     * component the grammar produced — its tokens — so what is asserted here is
     * which slot it landed in rather than a rendering of it. The colour it denotes
     * is asserted where it is computed, in the shapes group above. */
    CssBorderExpandClassesIs("b: rgb(10, 20, 30)", "-|-|color");
    CssBorderExpandIs("b: thick", "thick|<absent>|<absent>");
    CssBorderExpandIs("b: 0", "0|<absent>|<absent>");

    /* The refusals, each with its own reason and every output left absent so a
     * caller that ignored the code owns nothing rather than some of it. */
    {
        static const char *const refused[] = {
            /* A reference whose substitution can change *which kind* a component
             * is: `--rule: 1px solid red` makes one component three. */
            "p: var(--rule)",
            "p: env(--rule)",
            "p: 1px var(--s)",
            /* Two components for one kind. `1px 2px solid` is not a two-value
             * width — a border width is one value. */
            "p: 1px 2px solid",
            "p: red blue",
            "p: solid dashed",
            "p: 1px 2px",
            /* A component that belongs to no kind. */
            "p: banana",
            "p: 1px solid url(a.png)",
            /* Nothing at all. */
            "p:"
        };
        CssValue parts[RoboCssBorderKindCount];

        for (index = 0u; index < sizeof(refused) / sizeof(refused[0]); index++) {
            CssDeclarationList list = RoboCssCleared;
            const CssValue *value = CssBorderComponent(refused[index], &list);
            CssDiag diag = RoboCssCleared;
            size_t kindIndex;
            char shape[64];

            for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
                 kindIndex++) {
                CssValueInit(&parts[kindIndex]);
            }
            RoboCheck(CssBorderExpand(value, parts, &diag) != RoboOk);
            RoboCheckEqInt(diag.domain, RoboCssDomainBorder);
            RoboCheck(diag.message != NULL);
            for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
                 kindIndex++) {
                shape[0] = '\0';
                CssShape(&parts[kindIndex], shape, sizeof(shape));
                RoboCheckStrEq(shape, "");
                CssValueDispose(&parts[kindIndex]);
            }
            CssDeclarationListDispose(&list);
        }
    }

    /* The refusals that *name* the mistake, because collapsing them would still be
     * correct and would tell a developer nothing. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssDiag diag = RoboCssCleared;
        CssValue parts[RoboCssBorderKindCount];
        size_t kindIndex;

        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            CssValueInit(&parts[kindIndex]);
        }
        (void)CssBorderExpand(CssBorderComponent("p: var(--rule)", &list),
                                     parts, &diag);
        RoboCheck(strstr(diag.message, "substitution") != NULL);
        CssDeclarationListDispose(&list);

        (void)CssBorderExpand(CssBorderComponent("p: 1px 2px solid", &list),
                                     parts, &diag);
        RoboCheck(strstr(diag.message, "one value of each kind") != NULL);
        CssDeclarationListDispose(&list);

        (void)CssBorderExpand(CssBorderComponent("p: banana", &list), parts,
                                     &diag);
        RoboCheck(strstr(diag.message, "border value") != NULL);
        CssDeclarationListDispose(&list);

        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            CssValueDispose(&parts[kindIndex]);
            CssValueInit(&parts[kindIndex]);
        }
        RoboCheckEqInt(CssBorderExpand(NULL, parts, NULL),
                          RoboInvalidArgument);
        (void)CssBorderExpand(CssBorderComponent("p: red", &list), NULL, NULL);
        CssDeclarationListDispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

/**
 * The seventeen rows, and the three shapes the twelve longhands take.
 *
 * The three kinds deliberately do *not* share a shape: a width is 9.11's
 * `LINE_WIDTH`, a colour is 9.9's `COLOR` — so `border-top-color: currentColor`
 * and `color: currentColor` are one computation resolved by one rule — and only
 * the style needed a shape of its own. That is asserted rather than assumed,
 * because a phase that gave a border colour its own shape would be a second place
 * `currentColor` is resolved.
 */
static void TestBorderRegistry(void)
{
    CssPropertyRegistry *registry = NULL;
    const CssPropertyDefinition *definition;
    size_t sideIndex;
    size_t kindIndex;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL),
                      RoboOk);

    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
         sideIndex++) {
        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            CssBoxSide side = (CssBoxSide)sideIndex;
            CssBorderKind kind = (CssBorderKind)kindIndex;
            const char *name = CssBorderLonghandName(side, kind);

            definition = CssPropertyRegistryFindSlice(
                registry, CssSliceFromText(name));
            RoboCheck(definition != NULL);
            if (!definition) {
                continue;
            }
            /* Non-inherited, all twelve: a border is drawn per element, and a
             * child that inherited one would draw its parent's. */
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
            switch (kind) {
            case RoboCssBorderKindWidth:
                RoboCheckEqInt(definition->computation,
                                  RoboCssPropertyComputationLineWidth);
                RoboCheckStrEq(definition->initialText, "medium");
                break;
            case RoboCssBorderKindStyle:
                RoboCheckEqInt(definition->computation,
                                  RoboCssPropertyComputationBorderStyle);
                RoboCheckStrEq(definition->initialText, "none");
                break;
            case RoboCssBorderKindColor:
                RoboCheckEqInt(definition->computation,
                                  RoboCssPropertyComputationColor);
                /* The one initial value in the table that is a *reference*. It is
                 * not `transparent`, and it cannot be: a border whose colour had
                 * been resolved at registration time would be a border nobody
                 * could see. */
                RoboCheckStrEq(definition->initialText, "currentcolor");
                break;
            default:
                break;
            }
        }
    }

    /* The five shorthands are rows too, with the same concatenated initial a
     * shorthand carries and the shorthand flag set — a shorthand's own entry holds
     * what the author wrote, and the distribution is expansion's job. */
    for (kindIndex = 0u; kindIndex < CssBorderCompositeCount();
         kindIndex++) {
        definition = CssPropertyRegistryFindSlice(
            registry,
            CssSliceFromText(CssBorderCompositeName(kindIndex)));
        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckEqInt(definition->computation,
                          RoboCssPropertyComputationPreserve);
        RoboCheckEqInt((definition->flags & RoboCssPropertyFlagShorthand) != 0,
                          1);
        RoboCheckStrEq(definition->initialText, "medium none currentcolor");
    }
    for (kindIndex = 0u; kindIndex < CssBorderPositionalCount();
         kindIndex++) {
        definition = CssPropertyRegistryFindSlice(
            registry,
            CssSliceFromText(CssBorderPositionalName(kindIndex)));
        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
        RoboCheckEqInt((definition->flags & RoboCssPropertyFlagShorthand) != 0,
                          1);
    }
    RoboCheckStrEq(CssPropertyRegistryFindSlice(
                          registry, CssSliceFromText("border-style"))
                          ->initialText,
                      "none");
    RoboCheckStrEq(CssPropertyRegistryFindSlice(
                          registry, CssSliceFromText("border-color"))
                          ->initialText,
                      "currentcolor");
    RoboCheckStrEq(CssPropertyRegistryFindSlice(
                          registry, CssSliceFromText("border-width"))
                          ->initialText,
                      "medium");
    /* `border-radius` is not a value this build has. */
    RoboCheck(CssPropertyRegistryFindSlice(
                   registry, CssSliceFromText("border-radius")) == NULL);

    CssPropertyRegistryDestroy(registry);
}

/* ------------------------------------------------------------------ */
/* The computed shapes                                                 */
/* ------------------------------------------------------------------ */

/**
 * The four longhand shapes through declaration text.
 *
 * A width keeps `medium` as a keyword, a percentage is admitted because the 9.11
 * registry admits it and this phase follows the registry rather than overruling it
 * (the boundary the box suite records), and a colour reuses 9.9's family so
 * `currentColor` survives as a reference.
 */
static void TestBorderLonghandShapes(void)
{
    CssFixture();

    /* --- width --- */
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: 1px", "1px");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: 0.5em", "0.5em");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: thin", "thin");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: medium", "medium");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: thick", "thick");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLineWidth,
                                       "b: medium"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: red",
                       "not a border width keyword");
    CssComputeEquals(RoboCssPropertyComputationLineWidth, "b: banana",
                       "not a border width keyword");

    /* --- style: all ten, and the refusals kept apart --- */
    {
        size_t index;

        for (index = 0u; index < CssBorderStyleCount(); index++) {
            char text[64];

            (void)snprintf(text, sizeof(text), "b: %s",
                           CssBorderStyleName(
                               (CssBorderStyle)index));
            CssComputeEquals(RoboCssPropertyComputationBorderStyle, text,
                               CssBorderStyleName(
                                   (CssBorderStyle)index));
        }
    }
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBorderStyle,
                                       "b: solid"),
                      "border-style");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationBorderStyle,
                                       "b: none"),
                      "border-style");
    /* Three refusals, three sentences: an identifier this vocabulary does not have,
     * something that is not an identifier, and more than one component. */
    CssComputeEquals(RoboCssPropertyComputationBorderStyle, "b: banana",
                       "not a border style value");
    CssComputeEquals(RoboCssPropertyComputationBorderStyle, "b: 20px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationBorderStyle, "b: #fff",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationBorderStyle, "b: 10%",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationBorderStyle, "b: solid dashed",
                       "not a single border style keyword");
    /* And a reference is *carried*, not refused — `border-style: var(--s)` is a
     * declaration whose value is decided where `--s` is. */
    {
        uint32_t flags = 0u;
        char shape[RoboCssComputedValueTextMax];

        CssComputeText(RoboCssPropertyComputationBorderStyle,
                         "b: var(--s)", shape, sizeof(shape), &flags);
        RoboCheckEqInt(flags, 1u);
    }

    /* --- colour: 9.9's family, so a reference survives --- */
    CssComputeEquals(RoboCssPropertyComputationColor, "b: red", "#ff0000");
    CssComputeEquals(RoboCssPropertyComputationColor, "b: #00ff00", "#00ff00");
    CssComputeEquals(RoboCssPropertyComputationColor, "b: rgb(0, 0, 255)",
                       "#0000ff");
    CssComputeEquals(RoboCssPropertyComputationColor, "b: transparent",
                       "#00000000");
    /* The kind name is the computed-value layer's own: `colour`, which is what its
     * 9.9 suite established and what a diagnostic prints. A `currentcolor` is that
     * kind — a colour that is not resolved *yet* rather than a refusal. */
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationColor,
                                       "b: currentcolor"),
                      "colour");
    /* An identifier that is not a colour is reported as exactly that, apart from
     * every other refusal, because the author misspelled a colour rather than
     * putting the wrong kind of value in a colour property. */
    CssComputeEquals(RoboCssPropertyComputationColor, "b: banana",
                       "unknown colour name");
    /* The shape name a diagnostic prints for the new one. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBorderStyle),
                      "border-style");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)
                              RoboCssPropertyComputationCount),
                      0);
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void CssBorderComputeOf(CssInheritFixture *fixture,
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
 * recorded: nothing in the border family inherits, so an element that declared
 * nothing has no entry — 9.7 materialises initial values for the inherited set
 * only — and "what is this element's border-top-width" is a question only
 * something holding the registry can answer.
 */
static void CssBorderQuery(CssInheritFixture *fixture,
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
 * The twelve initial values, and the thirteen shorthand ones.
 *
 * `medium`, `none` and `currentcolor` — and the last is the one worth reading
 * twice, because it is a *reference* rather than a value: a border colour is
 * whatever the element's `color` is, and resolving it here would be inventing an
 * answer the element has not given.
 */
static void TestBorderInitial(void)
{
    /* The third one is the element's *own* colour, and that is the phase's most
     * easily-lost fact: the property's initial value is the reference `currentcolor`
     * — asserted as such, as text, in the registry group — and what an element sees
     * is that reference resolved against its own `color`. Left as the word, or
     * resolved to anything but the element's colour, `border: 1px solid` would draw
     * a border nobody could see. The sheet below declares the colour and no border,
     * so a resolution is visible in the answer rather than a default. */
    static const char *const initials[RoboCssBorderKindCount] = {
        "medium", "none", "#008000"
    };
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef element;
    char text[RoboCssComputedValueTextMax];
    size_t sideIndex;
    size_t kindIndex;

    CssFixture();
    CssInheritBegin(&fixture);
    element = CssHostAdd(CssInheritHost(&fixture), "div", "bare", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    /* A sheet that says nothing about borders, so every answer below is the
     * property's own initial value. */
    CssInheritSheet(&fixture, RoboCssOriginAuthor, "#bare { color: green }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssBorderComputeOf(&fixture, element, style);
    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
         sideIndex++) {
        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            const char *name = CssBorderLonghandName(
                (CssBoxSide)sideIndex, (CssBorderKind)kindIndex);
            int fromInitial = -1;

            CssBorderQuery(&fixture, style, name, text, sizeof(text),
                             &fromInitial);
            RoboCheckEqInt(fromInitial, 1);
            RoboCheckStrEq(text, initials[kindIndex]);
            /* Nothing was declared, so there is no entry — which is not the same
             * fact as "it has a value", and the door's flag is what says so. */
            RoboCheckEqInt(
                CssComputedStyleFind(
                    style, CssSliceFromText(name)) == (size_t)-1,
                1);
        }
    }
    /* `border-style: none` is the property's initial value *and* a decided value,
     * not an absent border — the same distinction `display: none` draws. */
    CssBorderQuery(&fixture, style, "border-top-style", text, sizeof(text), NULL);
    RoboCheckStrEq(text, "none");
    CssBorderQuery(&fixture, style, "border-top-width", text, sizeof(text), NULL);
    RoboCheckStrEq(text, "medium");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * Every declaration the family has, on one element, through the computed style.
 *
 * The acceptance test for the *shapes*: four sides, three kinds, and the author's
 * value arriving as a value rather than as letters.
 */
static void TestBorderLonghandsComputed(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef element;

    CssFixture();
    CssInheritBegin(&fixture);
    element = CssHostAdd(CssInheritHost(&fixture), "div", "box", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#box { border-top-width: 1px; border-right-width: 2px;"
                      " border-bottom-width: 3px; border-left-width: 4px;"
                      " border-top-style: solid; border-right-style: dashed;"
                      " border-bottom-style: dotted; border-left-style: double;"
                      " border-top-color: red; border-right-color: #00ff00;"
                      " border-bottom-color: rgb(0, 0, 255);"
                      " border-left-color: transparent }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    CssBorderComputeOf(&fixture, element, style);

    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-right-width", "2px");
    CssComputedIs(style, "border-bottom-width", "3px");
    CssComputedIs(style, "border-left-width", "4px");
    CssComputedIs(style, "border-top-style", "solid");
    CssComputedIs(style, "border-right-style", "dashed");
    CssComputedIs(style, "border-bottom-style", "dotted");
    CssComputedIs(style, "border-left-style", "double");
    CssComputedIs(style, "border-top-color", "#ff0000");
    CssComputedIs(style, "border-right-color", "#00ff00");
    CssComputedIs(style, "border-bottom-color", "#0000ff");
    CssComputedIs(style, "border-left-color", "#00000000");
    CssComputedFlagsAre(style, "border-top-width", "declared");
    CssComputedFlagsAre(style, "border-left-style", "declared");
    CssComputedFlagsAre(style, "border-top-color", "declared");

    /* The shorthands keep the author's text on their own entries, exactly as
     * `margin` and `background` do — but only the ones the sheet *declared*, which
     * here is none of them. */
    CssComputedIs(style, "border-width", "<absent>");
    CssComputedIs(style, "border", "<absent>");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * A border does not inherit, proved from both sides.
 *
 * "The child has no entry" is the half that catches a property misclassified as
 * inherited; "the child has the initial value when asked" is the half that catches
 * a query door reporting a hole as a value. And an explicit `inherit` still works
 * through the existing mechanism, which is why the property is classified rather
 * than special-cased.
 */
static void TestBorderNonInheritance(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parentStyle = NULL;
    CssComputedStyle *childStyle = NULL;
    CssElementRef parent;
    CssElementRef child;
    char text[RoboCssComputedValueTextMax];

    CssFixture();
    CssInheritBegin(&fixture);
    parent = CssHostAdd(CssInheritHost(&fixture), "div", "parent", NULL);
    child = CssHostAdd(CssInheritHost(&fixture), "div", "child", NULL);
    CssHostSetParent(CssInheritHost(&fixture), child, parent);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#parent { border: 3px dashed red; border-top-style: ridge;"
                      " color: green }"
                      "#child { border-top-style: inherit }");
    RoboCheckEqInt(CssComputedStyleCreate(&parentStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&childStyle, NULL), RoboOk);

    CssBorderComputeOf(&fixture, parent, parentStyle);
    CssComputedIs(parentStyle, "border-top-width", "3px");
    CssComputedIs(parentStyle, "border-top-style", "ridge");
    CssComputedIs(parentStyle, "border-top-color", "#ff0000");
    CssComputedIs(parentStyle, "border-left-color", "#ff0000");

    CssBorderComputeOf(&fixture, child, childStyle);
    /* The longhand that asked for the parent's value got it... */
    CssComputedIs(childStyle, "border-top-style", "ridge");
    CssComputedFlagsAre(childStyle, "border-top-style", "inherited");
    /* ...and a sibling kind on the *same* side did not, which is the assertion a
     * blanket "children inherit the parent's border" would fail. */
    CssComputedIs(childStyle, "border-top-width", "<absent>");
    CssComputedIs(childStyle, "border-top-color", "<absent>");
    CssComputedIs(childStyle, "border-bottom-style", "<absent>");
    CssBorderQuery(&fixture, childStyle, "border-top-width", text, sizeof(text),
                     NULL);
    RoboCheckStrEq(text, "medium");
    /* ...and what an undeclared border colour *is*: not the parent's red border,
     * which no child may inherit, but the child's own `color` — which it did inherit,
     * because `color` is an inherited property and a border colour is a reference to
     * it. One assertion, two rules. */
    CssBorderQuery(&fixture, childStyle, "border-left-color", text, sizeof(text),
                     NULL);
    RoboCheckStrEq(text, "#008000");

    CssComputedStyleDestroy(parentStyle);
    CssComputedStyleDestroy(childStyle);
    CssInheritEnd(&fixture);
}

/**
 * `initial`, `inherit` and `unset` on a non-inherited family.
 *
 * `unset` is the assertion that matters: it behaves as `initial` for a property
 * that does not inherit, so it would fail if any of the twelve were classified
 * wrongly. `border: initial` is the shorthand form of the same statement, and it is
 * checked too — a shorthand whose keyword did not travel would leave all twelve
 * longhands absent while the shorthand's own entry carried the word.
 */
static void TestBorderWideKeywords(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *parentStyle = NULL;
    CssComputedStyle *childStyle = NULL;
    CssElementRef parent;
    CssElementRef child;
    CssElementRef plain;
    char text[RoboCssComputedValueTextMax];

    CssFixture();
    CssInheritBegin(&fixture);
    parent = CssHostAdd(CssInheritHost(&fixture), "div", "parent", NULL);
    child = CssHostAdd(CssInheritHost(&fixture), "div", "child", NULL);
    plain = CssHostAdd(CssInheritHost(&fixture), "div", "plain", NULL);
    CssHostSetParent(CssInheritHost(&fixture), child, parent);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#parent { border-top-width: 7px; border-top-style: solid;"
                      " border-top-color: red; color: green }"
                      "#child { border-top-width: initial; border-top-style: inherit;"
                      " border-top-color: unset }"
                      "#plain { border: initial }");
    RoboCheckEqInt(CssComputedStyleCreate(&parentStyle, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&childStyle, NULL), RoboOk);

    CssBorderComputeOf(&fixture, parent, parentStyle);
    CssComputedIs(parentStyle, "border-top-width", "7px");

    CssBorderComputeOf(&fixture, child, childStyle);
    /* `initial` is the property's own initial value... */
    CssComputedIs(childStyle, "border-top-width", "medium");
    /* `default` rather than `declared+default`: the declaration is `initial`, so what
     * the longhand carries is the property's default, which is the same pair of
     * facts the box model's own wide-keyword test records. */
    CssComputedFlagsAre(childStyle, "border-top-width", "default");
    /* ...`inherit` is the parent's... */
    CssComputedIs(childStyle, "border-top-style", "solid");
    CssComputedFlagsAre(childStyle, "border-top-style", "inherited");
    /* ...and `unset` on a non-inherited property is `initial`, which is the
     * assertion that would fail if any of the twelve were misclassified. */
    CssComputedIs(childStyle, "border-top-color", "#008000");
    CssComputedFlagsAre(childStyle, "border-top-color", "default");

    /* The shorthand form, on an element that declared nothing else. */
    CssBorderComputeOf(&fixture, plain, childStyle);
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationLineWidth,
                                       "b: 1px"),
                      "length");
    CssBorderQuery(&fixture, childStyle, "border-top-width", text, sizeof(text),
                     NULL);
    RoboCheckStrEq(text, "medium");
    CssBorderQuery(&fixture, childStyle, "border-top-style", text, sizeof(text),
                     NULL);
    RoboCheckStrEq(text, "none");
    /* `plain` declared the whole shorthand as `initial` and nothing else, and it has
     * no parent, so its `color` is `color`'s own initial: black. The reference
     * resolves to that — a colour, not the word and not `transparent`. */
    CssBorderQuery(&fixture, childStyle, "border-left-color", text, sizeof(text),
                     NULL);
    RoboCheckStrEq(text, "#000000");

    CssComputedStyleDestroy(parentStyle);
    CssComputedStyleDestroy(childStyle);
    CssInheritEnd(&fixture);
}

/**
 * The positional rows: 1/2/3/4 components over four sides.
 *
 * The mapping is the box model's — literally the same function — and that is why
 * it is asserted for all three kinds rather than for one: `border-style: solid
 * dashed dotted double` and `border-width: 1px 2px 3px 4px` must distribute by the
 * same rule, and a second copy of the mapping is how they would come to disagree.
 */
static void TestBorderPositionalExpansion(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef one;
    CssElementRef two;
    CssElementRef three;
    CssElementRef four;

    CssFixture();
    CssInheritBegin(&fixture);
    one = CssHostAdd(CssInheritHost(&fixture), "div", "one", NULL);
    two = CssHostAdd(CssInheritHost(&fixture), "div", "two", NULL);
    three = CssHostAdd(CssInheritHost(&fixture), "div", "three", NULL);
    four = CssHostAdd(CssInheritHost(&fixture), "div", "four", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#one { border-width: 1px; border-style: solid;"
                      " border-color: red }"
                      "#two { border-width: 1px 2px; border-style: solid dashed;"
                      " border-color: red blue }"
                      "#three { border-width: 1px 2px 3px;"
                      " border-style: solid dashed dotted;"
                      " border-color: red blue green }"
                      "#four { border-width: 1px 2px 3px 4px;"
                      " border-style: solid dashed dotted double;"
                      " border-color: red blue green black }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssBorderComputeOf(&fixture, one, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-right-width", "1px");
    CssComputedIs(style, "border-bottom-width", "1px");
    CssComputedIs(style, "border-left-width", "1px");
    CssComputedIs(style, "border-top-style", "solid");
    CssComputedIs(style, "border-left-style", "solid");
    CssComputedIs(style, "border-top-color", "#ff0000");
    CssComputedIs(style, "border-bottom-color", "#ff0000");

    CssBorderComputeOf(&fixture, two, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-right-width", "2px");
    CssComputedIs(style, "border-bottom-width", "1px");
    CssComputedIs(style, "border-left-width", "2px");
    CssComputedIs(style, "border-top-style", "solid");
    CssComputedIs(style, "border-right-style", "dashed");
    CssComputedIs(style, "border-bottom-style", "solid");
    CssComputedIs(style, "border-left-style", "dashed");
    CssComputedIs(style, "border-top-color", "#ff0000");
    CssComputedIs(style, "border-left-color", "#0000ff");

    CssBorderComputeOf(&fixture, three, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-right-width", "2px");
    CssComputedIs(style, "border-bottom-width", "3px");
    CssComputedIs(style, "border-left-width", "2px");
    CssComputedIs(style, "border-bottom-style", "dotted");
    CssComputedIs(style, "border-right-style", "dashed");
    CssComputedIs(style, "border-left-style", "dashed");
    CssComputedIs(style, "border-bottom-color", "#008000");
    CssComputedIs(style, "border-left-color", "#0000ff");

    CssBorderComputeOf(&fixture, four, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-right-width", "2px");
    CssComputedIs(style, "border-bottom-width", "3px");
    CssComputedIs(style, "border-left-width", "4px");
    CssComputedIs(style, "border-top-style", "solid");
    CssComputedIs(style, "border-right-style", "dashed");
    CssComputedIs(style, "border-bottom-style", "dotted");
    CssComputedIs(style, "border-left-style", "double");
    CssComputedIs(style, "border-top-color", "#ff0000");
    CssComputedIs(style, "border-right-color", "#0000ff");
    CssComputedIs(style, "border-bottom-color", "#008000");
    CssComputedIs(style, "border-left-color", "#000000");

    /* Five components is not a longer shorthand, it is an invalid declaration —
     * and the author's text stays on the shorthand's own entry either way. */
    {
        CssElementRef five;

        five = CssHostAdd(CssInheritHost(&fixture), "div", "five", NULL);
        CssHostSeal(CssInheritHost(&fixture));
        CssInheritSheet(&fixture, RoboCssOriginAuthor,
                          "#five { border-width: 1px 2px 3px 4px 5px }");
        CssBorderComputeOf(&fixture, five, style);
        CssComputedIs(style, "border-top-width", "<absent>");
        CssComputedIs(style, "border-width", "1px 2px 3px 4px 5px");
    }

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * One longhand, four declarers, and the cascade deciding.
 *
 * The phase's central fact, in the only form that can be wrong: `border-top-width`
 * can be set by itself, by `border-width`, by `border-top`, and by `border`, and
 * which of them it *is* comes down to the same three keys as anything else — level,
 * specificity, document order. Every pair below is a different one of those keys,
 * and the last is the case a single-shorthand implementation cannot see: two
 * *different* shorthands competing for one longhand.
 */
static void TestBorderCascade(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef order;
    CssElementRef reversed;
    CssElementRef specific;
    CssElementRef important;
    CssElementRef cross;
    CssElementRef inlined;
    CssInlineStub stub;

    CssFixture();
    CssInheritBegin(&fixture);
    order = CssHostAdd(CssInheritHost(&fixture), "div", "order", NULL);
    reversed = CssHostAdd(CssInheritHost(&fixture), "div", "reversed", NULL);
    specific = CssHostAdd(CssInheritHost(&fixture), "div", "specific", NULL);
    important = CssHostAdd(CssInheritHost(&fixture), "div", "important", NULL);
    cross = CssHostAdd(CssInheritHost(&fixture), "div", "cross", NULL);
    inlined = CssHostAdd(CssInheritHost(&fixture), "div", "inlined", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      /* the shorthand then the longhand: the longhand is later,
                       * so it wins */
                      "#order { border: 1px solid red; border-top-width: 5px }"
                      /* the reverse: the shorthand is later, so *it* wins, and the
                       * width arrives out of it */
                      "#reversed { border-top-width: 5px; border: 1px solid red }"
                      /* specificity beating document order across two declarers */
                      "#specific { border-width: 9px }"
                      "div#specific { border-top-width: 1px }"
                      /* `!important` beats a more specific selector */
                      "#important { border-top-width: 1px !important }"
                      "div#important { border-top-width: 9px }"
                      /* two *different* shorthands, one longhand */
                      "#cross { border-width: 1px; border-top: 3px solid blue }"
                      ".never { border-top-width: 99px }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssBorderComputeOf(&fixture, order, style);
    CssComputedIs(style, "border-top-width", "5px");
    CssComputedIs(style, "border-top-style", "solid");
    CssComputedFlagsAre(style, "border-top-width", "declared");

    /* The other order: the shorthand wins and the longhand arrives *from* it, with
     * the other three sides taking the same value. */
    CssBorderComputeOf(&fixture, reversed, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-left-width", "1px");
    CssComputedIs(style, "border-top-color", "#ff0000");

    CssBorderComputeOf(&fixture, specific, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-left-width", "9px");

    CssBorderComputeOf(&fixture, important, style);
    CssComputedIs(style, "border-top-width", "1px");

    /* `border-width` and `border-top` are two shorthands for one longhand, and the
     * later one wins — the case a pass that owned them separately could not see. */
    CssBorderComputeOf(&fixture, cross, style);
    CssComputedIs(style, "border-top-width", "3px");
    CssComputedIs(style, "border-top-style", "solid");
    CssComputedIs(style, "border-left-width", "1px");
    /* A positional row covers four sides of *one kind*, so the left side has a width
     * and no style at all: absent is not `none`, and the difference is what keeps a
     * shorthand from inventing a border. */
    CssComputedIs(style, "border-left-style", "<absent>");

    /* And an inline declaration is the strongest normal level. Installed here
     * rather than above because the stub answers *every* element, which is exactly
     * what the assertions before it must not be measuring. */
    CssInheritInline(&fixture, &stub, "border-top-width: 7px");
    CssBorderComputeOf(&fixture, inlined, style);
    CssComputedIs(style, "border-top-width", "7px");
    CssComputedFlagsAre(style, "border-top-width", "declared");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * A kind the shorthand did not name is *reset*, and that is the composite rule.
 *
 * `border: 1px solid` after `border-color: red` really does clear the colour: the
 * shorthand stands for all three kinds on all four sides, so a kind it did not
 * mention takes the longhand's initial value. The reverse order is the same rule
 * read the other way — the longhand is later, so it wins and the colour survives.
 * And a *positional* row has no such case at all: one to four components always
 * cover all four sides, so `border-width: 1px` never resets a style.
 */
static void TestBorderShorthandReset(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef reset;
    CssElementRef kept;
    CssElementRef positional;
    char text[RoboCssComputedValueTextMax];

    CssFixture();
    CssInheritBegin(&fixture);
    reset = CssHostAdd(CssInheritHost(&fixture), "div", "reset", NULL);
    kept = CssHostAdd(CssInheritHost(&fixture), "div", "kept", NULL);
    positional = CssHostAdd(CssInheritHost(&fixture), "div", "positional", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#reset { border-color: red; border-top-style: dashed;"
                      " border: 1px solid; color: green }"
                      "#kept { border: 1px solid; border-left-color: red;"
                      " border-left-style: dashed; color: green }"
                      "#positional { border-style: dashed; border-width: 1px }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssBorderComputeOf(&fixture, reset, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-top-style", "solid");
    /* The colour was named by an earlier declaration and the shorthand did not name
     * it, so it is the property's *initial* value rather than the earlier red. */
    CssBorderQuery(&fixture, style, "border-top-color", text, sizeof(text), NULL);
    RoboCheckStrEq(text, "#008000");
    RoboCheckEqInt(
        CssComputedStyleFind(style,
                                     CssSliceFromText("border-top-color")) ==
            (size_t)-1,
        0);
    /* And every side, not only the one an earlier declaration happened to name. */
    CssBorderQuery(&fixture, style, "border-left-color", text, sizeof(text), NULL);
    RoboCheckStrEq(text, "#008000");

    /* The reverse order: the longhands are later, so they win on their own sides
     * and the reset happens only for the other three. */
    CssBorderComputeOf(&fixture, kept, style);
    CssComputedIs(style, "border-left-color", "#ff0000");
    CssComputedIs(style, "border-left-style", "dashed");
    CssComputedIs(style, "border-right-style", "solid");
    CssBorderQuery(&fixture, style, "border-right-color", text, sizeof(text), NULL);
    RoboCheckStrEq(text, "#008000");

    /* A positional row covers all four sides, so nothing is ever left unnamed: the
     * dashed style declared before `border-width: 1px` survives. */
    CssBorderComputeOf(&fixture, positional, style);
    CssComputedIs(style, "border-top-width", "1px");
    CssComputedIs(style, "border-top-style", "dashed");
    CssComputedIs(style, "border-left-style", "dashed");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * A shorthand that cannot be distributed is reported, not guessed at.
 *
 * `border-top: var(--rule)` is legal CSS whose value is decided where `--rule` is,
 * and substitution can change how many components there are and *which kind* each
 * is — `--rule: 1px solid red` makes one component three, of two kinds. So the
 * author's text stays on the shorthand's own entry, the three longhands it would
 * have covered stay absent, and the style is marked unsupported rather than
 * silently answering with a value nobody wrote. The same rule for a positional row
 * whose count substitution would change.
 */
static void TestBorderShorthandUndistributed(void)
{
    CssInheritFixture fixture;
    CssComputedStyle *style = NULL;
    CssElementRef composite;
    CssElementRef positional;
    char text[RoboCssComputedValueTextMax];

    CssFixture();
    CssInheritBegin(&fixture);
    composite = CssHostAdd(CssInheritHost(&fixture), "div", "composite", NULL);
    positional = CssHostAdd(CssInheritHost(&fixture), "div", "positional", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    CssInheritSheet(&fixture, RoboCssOriginAuthor,
                      "#composite { border-top: var(--rule) }"
                      "#positional { border-width: var(--edges) }");
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    /* The longhands are absent, and the shorthand's own entry carries the author's
     * text — which is what makes the failure *recoverable* by a later phase. The
     * count is one per *declaration*, not one per longhand it would have covered:
     * `border-top` covers three and is counted once, which is the difference
     * between reporting an author's mistake and reporting its consequences. */
    {
        CssCascade cascade = RoboCssCleared;
        CssInheritedStyle resolved = RoboCssCleared;
        CssError error = RoboCssCleared;

        RoboCheckEqInt(CssStyleContextMatchDeclarations(
                              CssInheritContext(&fixture), composite, &cascade,
                              &error, NULL),
                          RoboOk);
        RoboCheckEqInt(RoboCssInheritResolve(CssInheritContext(&fixture),
                                                  &cascade, NULL, &resolved, NULL),
                          RoboOk);
        RoboCheckEqInt(
            (int)CssInheritedStyleShorthandsUnexpanded(&resolved), 1);
        RoboCheckEqInt(resolved.unsupported, 1);
        RoboCheckEqInt(CssInheritedStyleFind(
                              &resolved, CssSliceFromText("border-top-width")) ==
                              NULL,
                          1);
        CssInheritIs(&resolved, "border-top-width", "<absent>", "<absent>");
        CssInheritIs(&resolved, "border-top", "var(--rule)", "declared");
        CssInheritedStyleDispose(&resolved);
        CssCascadeDispose(&cascade);
    }
    {
        CssCascade cascade = RoboCssCleared;
        CssInheritedStyle resolved = RoboCssCleared;
        CssError error = RoboCssCleared;

        RoboCheckEqInt(CssStyleContextMatchDeclarations(
                              CssInheritContext(&fixture), positional, &cascade,
                              &error, NULL),
                          RoboOk);
        RoboCheckEqInt(RoboCssInheritResolve(CssInheritContext(&fixture),
                                                  &cascade, NULL, &resolved, NULL),
                          RoboOk);
        RoboCheckEqInt(
            (int)CssInheritedStyleShorthandsUnexpanded(&resolved), 1);
        CssInheritIs(&resolved, "border-top-width", "<absent>", "<absent>");
        CssInheritIs(&resolved, "border-width", "var(--edges)", "declared");
        CssInheritedStyleDispose(&resolved);
        CssCascadeDispose(&cascade);
    }

    /* Through the computed style the longhands are absent too, and the query door
     * still answers the initial values — the two facts a consumer needs together. */
    CssBorderComputeOf(&fixture, composite, style);
    CssComputedIs(style, "border-top-width", "<absent>");
    CssBorderQuery(&fixture, style, "border-top-width", text, sizeof(text), NULL);
    RoboCheckStrEq(text, "medium");

    CssComputedStyleDestroy(style);
    CssInheritEnd(&fixture);
}

/**
 * Hostile input, and the invariant that matters.
 *
 * Every string is driven through the classifier, the expansion and the computed
 * style, and the invariants are stated rather than the outcomes: the classification
 * is always one of the five, an expansion that succeeded leaves each kind either
 * absent or a component, and nothing crashes. Malformed CSS must never take the
 * runtime down, and a colour cannot become a width because the input looked odd.
 */
static void TestBorderRobust(void)
{
    static const char *const hostile[] = {
        "b:",
        "b: ",
        "b: ;",
        "b: 1px solid red extra",
        "b: 1px 1px 1px 1px",
        "b: red solid 1px #fff",
        "b: var(--a) var(--b)",
        "b: rgb(1)",
        "b: rgb(1, 2)",
        "b: hsl(nonsense)",
        "b: #1234567890",
        "b: #",
        "b: -",
        "b: 1e999px",
        "b: 999999999999999999999999999px",
        "b: 0/0",
        "b: calc(1px + 1px) solid",
        "b: (solid)",
        "b: [1px]",
        "b: url('a.png')",
        "b: \"dashed\"",
        "b: currentColor",
        "b: CURRENTCOLOR",
        "b: SOLID",
        "b: None",
        "b: thin thin",
        "b: inherit inherit",
        "b: initial"
    };
    size_t index;

    CssFixture();
    for (index = 0u; index < sizeof(hostile) / sizeof(hostile[0]); index++) {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;
        const CssValue *value;
        CssBorderClass outcome;
        CssValue parts[RoboCssBorderKindCount];
        size_t kindIndex;

        /* The declaration may or may not survive the grammar; both are answers. */
        if (CssParseDeclarations(hostile[index], &list, &error) != RoboOk) {
            continue;
        }
        value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
        if (!value) {
            CssDeclarationListDispose(&list);
            continue;
        }
        outcome = CssBorderClassify(value);
        RoboCheckEqInt(CssBorderClassIsKnown(outcome), 1);

        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            CssValueInit(&parts[kindIndex]);
        }
        if (CssBorderExpand(value, parts, NULL) == RoboOk) {
            /* Success means every kind is absent or a *single-component* list,
             * which is the shape a declared longhand has. */
            for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
                 kindIndex++) {
                if (parts[kindIndex].kind == RoboCssValueKindNone) {
                    continue;
                }
                RoboCheckEqInt(parts[kindIndex].kind, RoboCssValueKindList);
                RoboCheckEqInt((int)parts[kindIndex].children.count, 1);
            }
        }
        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            CssValueDispose(&parts[kindIndex]);
        }
        CssDeclarationListDispose(&list);
    }

    /* The same strings through the other two shapes, where they must answer rather
     * than crash — a malformed declaration is an invalid *computed value*, not a
     * failure of the normaliser. */
    for (index = 0u; index < sizeof(hostile) / sizeof(hostile[0]); index++) {
        CssComputedValue value = RoboCssCleared;

        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationBorderStyle,
                              NULL, &value, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(CssComputedValueKindIsKnown(value.kind), 1);
        CssComputedValueDispose(&value);
        break;
    }
    /* A NULL component and a cleared value are both answers. */
    RoboCheckEqInt(CssBorderClassify(NULL), RoboCssBorderClassInvalid);
    {
        CssValue cleared = RoboCssCleared;
        CssValue parts[RoboCssBorderKindCount];
        size_t kindIndex;

        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            CssValueInit(&parts[kindIndex]);
        }
        RoboCheckEqInt(CssBorderExpand(&cleared, parts, NULL),
                          RoboNotSupported);
        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            RoboCheckEqInt(parts[kindIndex].kind, RoboCssValueKindNone);
        }
    }
}

/**
 * The same document twice, and the vocabulary walked backwards.
 *
 * Determinism is a property of the whole family: twelve longhands from five
 * declarers means twelve resolutions per element, and an order-dependent weight (a
 * "last one wins" that quietly meant "last one *read*") would show here or nowhere.
 */
static void TestBorderDeterminism(void)
{
    static const char *const sheetText =
        "#a { border: 1px solid red; border-top-width: 2px }"
        "#b { border-width: 1px 2px 3px 4px; border-color: red blue green black }"
        "#c { border-style: solid dashed dotted double; border-top: 5px }";
    CssInheritFixture fixture;
    CssComputedStyle *first = NULL;
    CssComputedStyle *second = NULL;
    char a[RoboCssComputedValueTextMax];
    char b[RoboCssComputedValueTextMax];
    size_t sideIndex;
    size_t kindIndex;
    size_t pass;

    CssFixture();
    CssInheritBegin(&fixture);
    (void)CssHostAdd(CssInheritHost(&fixture), "div", "a", NULL);
    (void)CssHostAdd(CssInheritHost(&fixture), "div", "b", NULL);
    (void)CssHostAdd(CssInheritHost(&fixture), "div", "c", NULL);
    CssHostSeal(CssInheritHost(&fixture));
    RoboCheckEqInt(CssComputedStyleCreate(&first, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&second, NULL), RoboOk);

    for (pass = 0u; pass < 2u; pass++) {
        CssStylesheetId sheet =
            CssInheritSheet(&fixture, RoboCssOriginAuthor, sheetText);

        for (sideIndex = 0u; sideIndex < 3u; sideIndex++) {
            CssComputedStyle *style = pass == 0u ? first : second;
            CssElementRef ref =
                (CssElementRef)(sideIndex + 1u);

            CssBorderComputeOf(&fixture, ref, style);
        }
        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    }

    for (sideIndex = 0u; sideIndex < (size_t)RoboCssBoxSideCount;
         sideIndex++) {
        for (kindIndex = 0u; kindIndex < (size_t)RoboCssBorderKindCount;
             kindIndex++) {
            const char *name = CssBorderLonghandName(
                (CssBoxSide)sideIndex, (CssBorderKind)kindIndex);

            CssBorderQuery(&fixture, first, name, a, sizeof(a), NULL);
            CssBorderQuery(&fixture, second, name, b, sizeof(b), NULL);
            RoboCheckStrEq(a, b);
        }
    }

    /* And the vocabulary walked *backwards* through the classifier, so a head-first
     * search that stopped at the wrong row would show as a disagreement with the
     * forward walk. */
    for (sideIndex = CssBorderStyleCount(); sideIndex > 0u; sideIndex--) {
        CssBorderStyle style =
            (CssBorderStyle)(sideIndex - 1u);

        RoboCheckEqInt(CssBorderStyleFromText(
                              CssBorderStyleName(style)),
                          style);
    }

    CssComputedStyleDestroy(first);
    CssComputedStyleDestroy(second);
    CssInheritEnd(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssBorder(void);

void TestRegisterCssBorder(void)
{
    TestRegister("css.border.style.vocabulary", TestBorderStyleVocabulary);
    TestRegister("css.border.style.none_trap", TestBorderStyleNoneTrap);
    TestRegister("css.border.style.generates", TestBorderStyleGenerates);
    TestRegister("css.border.style.write", TestBorderStyleWrite);
    TestRegister("css.border.kinds", TestBorderKinds);
    TestRegister("css.border.positional.rows", TestBorderPositionalRows);
    TestRegister("css.border.composites", TestBorderComposites);
    TestRegister("css.border.classify", TestBorderClassify);
    TestRegister("css.border.expand", TestBorderExpand);
    TestRegister("css.border.registry", TestBorderRegistry);
    TestRegister("css.border.longhand.shapes", TestBorderLonghandShapes);
    TestRegister("css.border.initial", TestBorderInitial);
    TestRegister("css.border.longhands.computed",
                       TestBorderLonghandsComputed);
    TestRegister("css.border.non_inheritance", TestBorderNonInheritance);
    TestRegister("css.border.keywords", TestBorderWideKeywords);
    TestRegister("css.border.positional.expansion",
                       TestBorderPositionalExpansion);
    TestRegister("css.border.cascade", TestBorderCascade);
    TestRegister("css.border.shorthand.reset", TestBorderShorthandReset);
    TestRegister("css.border.shorthand.undistributed",
                       TestBorderShorthandUndistributed);
    TestRegister("css.border.robust", TestBorderRobust);
    TestRegister("css.border.determinism", TestBorderDeterminism);
}
