/*
 * RoboCSS selector parsing suite (Phase 9.4) — css.select.parse.*
 *
 * Every test here asserts the *structure* the parser built: how many selectors,
 * how many compounds, which combinator between them, which kind of simple
 * selector, and the resolved name. Asserting "it parsed" would pass for a parser
 * that produced one universal selector for every input, so nothing here does.
 *
 * The names are the other half. A parsed name is the tokenizer's *value*, not
 * the source bytes, so `.foo\:bar` must read back as `foo:bar` and `#item\31 23`
 * as `item123`. If escapes were left in, every one of those tests would fail —
 * which is the point: escape handling is observable here rather than assumed.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Lists                                                               */
/* ------------------------------------------------------------------ */

static void TestCssSelectSingleType(void)
{
    CssSelectorList list;
    const CssSimpleSelector *simple;
    char text[32];

    CssSelectParse("p", &list);
    RoboCheckEqInt(CssSelectorListCount(&list), 1u);
    RoboCheckEqInt(CssSelectorListIsWellFormed(&list), 1);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(
                          CssSelectorListAt(&list, 0u)),
                      1u);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 1u);

    simple = CssSelectSimple(&list, 0u, 0u, 0u);
    RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorType);
    CssSelectText(simple->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "p"), 0);
    RoboCheckEqInt(simple->range.begin, 0u);
    RoboCheckEqInt(simple->range.end, 1u);
    CssSelectorListDispose(&list);
}

static void TestCssSelectListOrder(void)
{
    CssSelectorList list;
    size_t i;
    static const char *names[] = {"h1", "h2", "h3"};

    CssSelectParse("h1, h2, h3", &list);
    RoboCheckEqInt(CssSelectorListCount(&list), 3u);
    for (i = 0u; i < 3u; i++) {
        char text[16];

        CssSelectText(CssSelectSimple(&list, i, 0u, 0u)->name, text, sizeof(text));
        RoboCheckEqInt(strcmp(text, names[i]), 0);
    }
    /* The list's range spans the whole thing, commas included: `h1, h2, h3` is
     * ten bytes, and the derived range reaches the end of `h3`, not of the
     * comma before it. */
    RoboCheckEqInt(list.range.begin, 0u);
    RoboCheckEqInt(list.range.end, 10u);
    CssSelectorListDispose(&list);
}

static void TestCssSelectListWhitespaceAroundCommas(void)
{
    CssSelectorList list;

    CssSelectParse("  h1 ,\n\t h2  ", &list);
    RoboCheckEqInt(CssSelectorListCount(&list), 2u);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 1u);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Type selectors                                                      */
/* ------------------------------------------------------------------ */

static void TestCssSelectTypeKeepsSpelling(void)
{
    CssSelectorList list;
    const CssSimpleSelector *simple;
    char text[16];

    /* The author's spelling is preserved; folding is the matcher's business, so
     * a diagnostic can quote what was actually written. */
    CssSelectParse("DIV", &list);
    simple = CssSelectSimple(&list, 0u, 0u, 0u);
    RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorType);
    CssSelectText(simple->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "DIV"), 0);
    CssSelectorListDispose(&list);
}

static void TestCssSelectTypeUnicode(void)
{
    CssSelectorList list;
    const CssSimpleSelector *simple;
    char text[32];

    /* "fixture" in Arabic script: a non-ASCII identifier is one ident token and
     * must survive byte for byte. */
    CssSelectParse("\xD9\x82\xD8\xB3\xD9\x85", &list);
    simple = CssSelectSimple(&list, 0u, 0u, 0u);
    RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorType);
    RoboCheckEqInt(simple->name.length, 6u);
    CssSelectText(simple->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "\xD9\x82\xD8\xB3\xD9\x85"), 0);
    CssSelectorListDispose(&list);
}

static void TestCssSelectTypeEscaped(void)
{
    CssSelectorList list;
    char text[16];

    /* `\64 iv` spells `div`: the hex escape and the whitespace that terminates
     * it are both gone from the name. */
    CssSelectParse("\\64 iv", &list);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "div"), 0);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Universal                                                           */
/* ------------------------------------------------------------------ */

static void TestCssSelectUniversalAlone(void)
{
    CssSelectorList list;

    CssSelectParse("*", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorUniversal);
    RoboCheckEqInt(CssSliceIsEmpty(
                          CssSelectSimple(&list, 0u, 0u, 0u)->name),
                      1);
    CssSelectorListDispose(&list);
}

static void TestCssSelectUniversalInCompound(void)
{
    CssSelectorList list;

    CssSelectParse("*.card", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 2u);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorUniversal);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 1u)->kind,
                      RoboCssSimpleSelectorClass);
    CssSelectorListDispose(&list);
}

static void TestCssSelectUniversalDescendant(void)
{
    CssSelectorList list;
    const CssComplexSelector *complex;

    CssSelectParse("div *", &list);
    complex = CssSelectorListAt(&list, 0u);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(complex), 2u);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 0u),
                      RoboCssCombinatorDescendant);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 1u, 0u)->kind,
                      RoboCssSimpleSelectorUniversal);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Id and class                                                        */
/* ------------------------------------------------------------------ */

static void TestCssSelectIdAndClass(void)
{
    CssSelectorList list;
    char text[32];

    CssSelectParse("div#main.card", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 3u);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorType);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 1u)->kind,
                      RoboCssSimpleSelectorId);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 1u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "main"), 0);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 2u)->kind,
                      RoboCssSimpleSelectorClass);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 2u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "card"), 0);
    CssSelectorListDispose(&list);
}

static void TestCssSelectClassMultipleInOneCompound(void)
{
    CssSelectorList list;
    char text[32];

    CssSelectParse(".button.primary.active", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 3u);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 2u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "active"), 0);
    CssSelectorListDispose(&list);
}

static void TestCssSelectClassHyphenAndUnderscore(void)
{
    CssSelectorList list;
    char text[32];

    CssSelectParse(".foo-bar_baz", &list);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "foo-bar_baz"), 0);
    CssSelectorListDispose(&list);
}

static void TestCssSelectEscapedIdentifiers(void)
{
    CssSelectorList list;
    char text[32];

    /* A colon in a class name is spelled with an escape, and the escape is not
     * part of the name. */
    CssSelectParse(".foo\\:bar", &list);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "foo:bar"), 0);
    CssSelectorListDispose(&list);
}

static void TestCssSelectIdEscapedDigits(void)
{
    CssSelectorList list;
    char text[32];

    /* `#item\31 23` is `item123`: a leading digit needs the escape, and the
     * whitespace that ends the hex escape is consumed. */
    CssSelectParse("#item\\31 23", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorId);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "item123"), 0);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Attribute selectors                                                 */
/* ------------------------------------------------------------------ */

/** Parse @p text and return its first simple selector, disposed by the caller. */
static const CssSimpleSelector *SelectAttributeOf(
    CssSelectorList *list,
    const char *text)
{
    CssSelectParse(text, list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(list), 1u);
    return CssSelectSimple(list, 0u, 0u, 0u);
}

static void TestCssSelectAttributePresence(void)
{
    CssSelectorList list;
    const CssSimpleSelector *simple = SelectAttributeOf(&list, "[disabled]");
    char text[32];

    RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorAttribute);
    CssSelectText(simple->attribute, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "disabled"), 0);
    RoboCheckEqInt(simple->match, RoboCssAttributeMatchNone);
    /* Presence has no value: `[a]` and `[a=""]` must stay different selectors. */
    RoboCheckEqInt(CssSliceIsAbsent(simple->attributeValue), 1);
    CssSelectorListDispose(&list);
}

static void TestCssSelectAttributeAllOperators(void)
{
    static const struct {
        const char *text;
        CssAttributeMatch expected;
        const char *value;
    } cases[] = {
        {"[type=\"button\"]", RoboCssAttributeMatchEquals, "button"},
        {"[type='button']", RoboCssAttributeMatchEquals, "button"},
        {"[data-id=abc]", RoboCssAttributeMatchEquals, "abc"},
        {"[class~=active]", RoboCssAttributeMatchIncludes, "active"},
        {"[lang|=en]", RoboCssAttributeMatchDash, "en"},
        {"[href^=\"/api\"]", RoboCssAttributeMatchPrefix, "/api"},
        {"[href$=\".json\"]", RoboCssAttributeMatchSuffix, ".json"},
        {"[data-name*=\"test\"]", RoboCssAttributeMatchSubstring, "test"}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, cases[i].text);
        char text[32];

        RoboCheckEqInt(simple->match, cases[i].expected);
        CssSelectText(simple->attributeValue, text, sizeof(text));
        RoboCheckEqInt(strcmp(text, cases[i].value), 0);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectAttributeValueForms(void)
{
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[type=\"text\"]");
        RoboCheckEqInt(simple->valueWasString, 1);
        CssSelectorListDispose(&list);
    }
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[type=text]");
        /* The same comparison, spelled without quotes: the flag records which,
         * the meaning does not change. */
        RoboCheckEqInt(simple->valueWasString, 0);
        RoboCheckEqInt(simple->match, RoboCssAttributeMatchEquals);
        CssSelectorListDispose(&list);
    }
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[a=\"\"]");
        /* An empty value is a value: present, empty, and comparing equal to "". */
        RoboCheckEqInt(simple->match, RoboCssAttributeMatchEquals);
        RoboCheckEqInt(CssSliceIsEmpty(simple->attributeValue), 1);
        RoboCheckEqInt(simple->valueWasString, 1);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectAttributeCaseFlags(void)
{
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[type=\"text\" i]");
        RoboCheckEqInt(simple->caseFlag, RoboCssAttributeCaseInsensitive);
        CssSelectorListDispose(&list);
    }
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[type=\"text\" s]");
        RoboCheckEqInt(simple->caseFlag, RoboCssAttributeCaseSensitive);
        CssSelectorListDispose(&list);
    }
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[type=\"text\"]");
        RoboCheckEqInt(simple->caseFlag, RoboCssAttributeCaseDefault);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectAttributeWhitespaceAndEscapes(void)
{
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[ type = \"text\" ]");
        char text[32];

        RoboCheckEqInt(simple->match, RoboCssAttributeMatchEquals);
        CssSelectText(simple->attribute, text, sizeof(text));
        RoboCheckEqInt(strcmp(text, "type"), 0);
        CssSelectText(simple->attributeValue, text, sizeof(text));
        RoboCheckEqInt(strcmp(text, "text"), 0);
        CssSelectorListDispose(&list);
    }
    {
        CssSelectorList list;
        const CssSimpleSelector *simple =
            SelectAttributeOf(&list, "[data-name=\"a\\ b\"]");
        char text[32];

        /* An escaped space inside a string is content, so the value is `a b`. */
        CssSelectText(simple->attributeValue, text, sizeof(text));
        RoboCheckEqInt(strcmp(text, "a b"), 0);
        RoboCheckEqInt(simple->attributeValue.length, 3u);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectAttributeInCompound(void)
{
    CssSelectorList list;

    CssSelectParse("input[type=\"text\"].field", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 3u);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorType);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 1u)->kind,
                      RoboCssSimpleSelectorAttribute);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 2u)->kind,
                      RoboCssSimpleSelectorClass);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Combinators                                                         */
/* ------------------------------------------------------------------ */

static void TestCssSelectCombinatorsAll(void)
{
    static const struct {
        const char *text;
        CssCombinator expected;
    } cases[] = {
        {"div p", RoboCssCombinatorDescendant},
        {"div > p", RoboCssCombinatorChild},
        {"div>p", RoboCssCombinatorChild},
        {"h1 + p", RoboCssCombinatorNextSibling},
        {"h1+p", RoboCssCombinatorNextSibling},
        {"h1 ~ p", RoboCssCombinatorSubsequentSibling},
        {"h1~p", RoboCssCombinatorSubsequentSibling},
        {"col || td", RoboCssCombinatorColumn},
        {"div\t\np", RoboCssCombinatorDescendant}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSelectorList list;
        const CssComplexSelector *complex;

        CssSelectParse(cases[i].text, &list);
        complex = CssSelectorListAt(&list, 0u);
        RoboCheckEqInt(CssComplexSelectorCompoundCount(complex), 2u);
        RoboCheckEqInt(CssComplexSelectorCombinatorCount(complex), 1u);
        RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 0u),
                          cases[i].expected);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectCombinatorWhitespaceForms(void)
{
    static const char *cases[] = {
        "div  >  p", "div >p", "div> p", "div\n>\np"
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSelectorList list;
        const CssComplexSelector *complex;

        CssSelectParse(cases[i], &list);
        complex = CssSelectorListAt(&list, 0u);
        RoboCheckEqInt(CssComplexSelectorCompoundCount(complex), 2u);
        RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 0u),
                          RoboCssCombinatorChild);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectCommentIsNotACombinator(void)
{
    /* A comment before `>` is not whitespace and does not add a descendant
     * combinator: a comment between `div` and `>p` leaves exactly two compounds
     * joined by a child combinator, not three joined by descendant-then-child. */
    CssSelectorList list;
    const CssComplexSelector *complex;

    CssSelectParse("div/* note */>p", &list);
    complex = CssSelectorListAt(&list, 0u);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(complex), 2u);
    RoboCheckEqInt(CssComplexSelectorCombinatorCount(complex), 1u);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 0u),
                      RoboCssCombinatorChild);
    CssSelectorListDispose(&list);
}

static void TestCssSelectComplexSelector(void)
{
    CssSelectorList list;
    const CssComplexSelector *complex;

    CssSelectParse("#app > .panel .title", &list);
    complex = CssSelectorListAt(&list, 0u);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(complex), 3u);
    RoboCheckEqInt(CssComplexSelectorCombinatorCount(complex), 2u);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 0u),
                      RoboCssCombinatorChild);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 1u),
                      RoboCssCombinatorDescendant);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 1u);
    RoboCheckEqInt(CssCompoundSelectorCount(
                          CssComplexSelectorCompoundAt(complex, 1u)),
                      1u);
    RoboCheckEqInt(CssCompoundSelectorCount(
                          CssComplexSelectorCompoundAt(complex, 2u)),
                      1u);
    CssSelectorListDispose(&list);
}

static void TestCssSelectComplexFourCompounds(void)
{
    CssSelectorList list;
    const CssComplexSelector *complex;

    CssSelectParse("article.card > header.title + p em", &list);
    complex = CssSelectorListAt(&list, 0u);
    RoboCheckEqInt(CssComplexSelectorCompoundCount(complex), 4u);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 0u),
                      RoboCssCombinatorChild);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 1u),
                      RoboCssCombinatorNextSibling);
    RoboCheckEqInt(CssComplexSelectorCombinatorAt(complex, 2u),
                      RoboCssCombinatorDescendant);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Pseudo-classes                                                      */
/* ------------------------------------------------------------------ */

static void TestCssSelectStructuralPseudoNames(void)
{
    static const char *cases[] = {":root", ":first-child", ":last-child", ":only-child"};
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSelectorList list;
        const CssSimpleSelector *simple;

        CssSelectParse(cases[i], &list);
        simple = CssSelectSimple(&list, 0u, 0u, 0u);
        RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorPseudoClass);
        RoboCheckEqInt(simple->name.length, strlen(cases[i]) - 1u);
        RoboCheckEqInt(CssSliceIsAbsent(simple->argument), 1);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectNthChildForms(void)
{
    static const struct {
        const char *text;
        int a;
        int b;
    } cases[] = {
        {":nth-child(2n+1)", 2, 1},
        {":nth-child(odd)", 2, 1},
        {":nth-child(even)", 2, 0},
        {":nth-child(3)", 0, 3},
        {":nth-child(n)", 1, 0},
        {":nth-child(-n+3)", -1, 3},
        {":nth-child(2n)", 2, 0},
        {":nth-child( 2n + 1 )", 2, 1},
        {":nth-child(-3n-2)", -3, -2},
        {":nth-child(+2n)", 2, 0},
        {":nth-child(n-1)", 1, -1}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSelectorList list;
        const CssSimpleSelector *simple;

        CssSelectParse(cases[i].text, &list);
        simple = CssSelectSimple(&list, 0u, 0u, 0u);
        RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorPseudoClass);
        RoboCheckEqInt(CssSimpleSelectorHasNth(simple), 1);
        RoboCheckEqInt(CssSimpleSelectorNthA(simple), cases[i].a);
        RoboCheckEqInt(CssSimpleSelectorNthB(simple), cases[i].b);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectNthLastChild(void)
{
    CssSelectorList list;
    const CssSimpleSelector *simple;

    CssSelectParse("li:nth-last-child(2)", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 2u);
    simple = CssSelectSimple(&list, 0u, 0u, 1u);
    RoboCheckEqInt(CssSimpleSelectorIsPseudoNamed(simple, "nth-last-child"),
                      1);
    RoboCheckEqInt(CssSimpleSelectorNthA(simple), 0);
    RoboCheckEqInt(CssSimpleSelectorNthB(simple), 2);
    CssSelectorListDispose(&list);
}

static void TestCssSelectPseudoNameCaseInsensitive(void)
{
    CssSelectorList list;
    const CssSimpleSelector *simple;

    CssSelectParse(":FIRST-CHILD", &list);
    simple = CssSelectSimple(&list, 0u, 0u, 0u);
    RoboCheckEqInt(CssSimpleSelectorIsPseudoNamed(simple, "first-child"),
                      1);
    CssSelectorListDispose(&list);
}

static void TestCssSelectNotParsesItsArgument(void)
{
    CssSelectorList list;
    const CssCompoundSelector *compound;
    const CssSimpleSelector *simple;
    const CssSelectorList *argument;
    char text[32];

    CssSelectParse("p:not(.muted)", &list);
    compound = CssComplexSelectorCompoundAt(
        CssSelectorListAt(&list, 0u), 0u);
    RoboCheckEqInt(CssCompoundSelectorCount(compound), 2u);
    simple = CssCompoundSelectorAt(compound, 1u);
    RoboCheckEqInt(CssSimpleSelectorIsPseudoNamed(simple, "not"), 1);

    /* The raw argument is kept as written… */
    CssSelectText(simple->argument, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, ".muted"), 0);

    /* …and the parsed form is a real selector list owned by the compound. */
    RoboCheckEqInt(CssCompoundSelectorNestedCount(compound), 1u);
    argument = CssCompoundSelectorArgumentOf(compound, 1u);
    RoboCheckEqInt(argument != NULL, 1);
    RoboCheckEqInt(CssSelectorListCount(argument), 1u);
    RoboCheckEqInt(CssSimpleSelectorNestedIndex(simple), 0u);
    CssSelectorListDispose(&list);
}

static void TestCssSelectNotAcceptsAListAndAComplex(void)
{
    {
        CssSelectorList list;
        const CssCompoundSelector *compound;

        CssSelectParse(":not(.a, .b)", &list);
        compound = CssComplexSelectorCompoundAt(
            CssSelectorListAt(&list, 0u), 0u);
        RoboCheckEqInt(CssSelectorListCount(
                              CssCompoundSelectorArgumentOf(compound, 0u)),
                          2u);
        CssSelectorListDispose(&list);
    }
    {
        CssSelectorList list;
        const CssCompoundSelector *compound;
        const CssSelectorList *argument;

        CssSelectParse(":not(div > p)", &list);
        compound = CssComplexSelectorCompoundAt(
            CssSelectorListAt(&list, 0u), 0u);
        argument = CssCompoundSelectorArgumentOf(compound, 0u);
        RoboCheckEqInt(CssComplexSelectorCompoundCount(
                              CssSelectorListAt(argument, 0u)),
                          2u);
        RoboCheckEqInt(CssComplexSelectorCombinatorAt(
                              CssSelectorListAt(argument, 0u), 0u),
                          RoboCssCombinatorChild);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectNotNests(void)
{
    CssSelectorList list;
    const CssCompoundSelector *outer;
    const CssSelectorList *levelOne;
    const CssCompoundSelector *inner;
    const CssSelectorList *levelTwo;

    CssSelectParse(":not(:not(.a))", &list);
    outer = CssComplexSelectorCompoundAt(
        CssSelectorListAt(&list, 0u), 0u);
    levelOne = CssCompoundSelectorArgumentOf(outer, 0u);
    RoboCheckEqInt(levelOne != NULL, 1);
    inner = CssComplexSelectorCompoundAt(
        CssSelectorListAt(levelOne, 0u), 0u);
    levelTwo = CssCompoundSelectorArgumentOf(inner, 0u);
    RoboCheckEqInt(levelTwo != NULL, 1);
    RoboCheckEqInt(CssSelectorListCount(levelTwo), 1u);
    RoboCheckEqInt(CssCompoundSelectorCount(
                          CssComplexSelectorCompoundAt(
                              CssSelectorListAt(levelTwo, 0u), 0u)),
                      1u);
    CssSelectorListDispose(&list);
}

static void TestCssSelectPseudoElements(void)
{
    static const char *cases[] = {"::before", "::after", "::first-line", "::first-letter"};
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssSelectorList list;
        const CssSimpleSelector *simple;
        char text[32];

        CssSelectParse(cases[i], &list);
        simple = CssSelectSimple(&list, 0u, 0u, 0u);
        RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorPseudoElement);
        CssSelectText(simple->name, text, sizeof(text));
        RoboCheckEqInt(strcmp(text, cases[i] + 2), 0);
        CssSelectorListDispose(&list);
    }
}

static void TestCssSelectPseudoElementDiffersFromPseudoClass(void)
{
    CssSelectorList list;

    /* The colon count is the rule, with one deliberate exception: the four
     * pseudo-elements CSS2 spelled with a single colon are still pseudo-elements
     * in that spelling — CSS Selectors 3 says so, every engine accepts it (Blink
     * has a dedicated check for exactly `before`, `after`, `first-letter` and
     * `first-line`), and reading `:before` as a pseudo-*class* would make
     * generated content into a condition nothing can satisfy. */
    CssSelectParse("::before", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorPseudoElement);
    CssSelectorListDispose(&list);

    CssSelectParse(":before", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorPseudoElement);
    CssSelectorListDispose(&list);

    /* The exception is a closed list, and it is case-insensitive like every
     * other pseudo name. */
    CssSelectParse(":FIRST-LETTER", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorPseudoElement);
    CssSelectorListDispose(&list);

    CssSelectParse(":hover", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorPseudoClass);
    CssSelectorListDispose(&list);

    CssSelectParse(":first-child", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorPseudoClass);
    CssSelectorListDispose(&list);

    /* `:before` takes no argument, so a functional spelling of the same name
     * stays a pseudo-class rather than becoming generated content. */
    CssSelectParse(":before(x)", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->kind,
                      RoboCssSimpleSelectorPseudoClass);
    CssSelectorListDispose(&list);
}

static void TestCssSelectPseudoElementInCompound(void)
{
    CssSelectorList list;

    CssSelectParse("p.intro::first-line", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 3u);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 2u)->kind,
                      RoboCssSimpleSelectorPseudoElement);
    CssSelectorListDispose(&list);
}

static void TestCssSelectPseudoClassCase(void)
{
    CssSelectorList list;
    const CssCompoundSelector *compound;

    /* `:NOT(...)` is the same pseudo-class, and its argument is still parsed. */
    CssSelectParse(":NOT(.a)", &list);
    compound = CssComplexSelectorCompoundAt(
        CssSelectorListAt(&list, 0u), 0u);
    RoboCheckEqInt(CssCompoundSelectorNestedCount(compound), 1u);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* An+B                                                                */
/* ------------------------------------------------------------------ */

static void TestCssAnPlusBMembership(void)
{
    static const struct {
        int a;
        int b;
        int index;
        int expected;
    } cases[] = {
        {2, 1, 1, 1}, {2, 1, 3, 1}, {2, 1, 5, 1},
        {2, 1, 2, 0}, {2, 1, 0, 0}, {2, 1, -1, 0},
        {0, 3, 3, 1}, {0, 3, 4, 0},
        {1, 0, 1, 1}, {1, 0, 7, 1},
        {-1, 3, 3, 1}, {-1, 3, 1, 1}, {-1, 3, 0, 0}, {-1, 3, 4, 0},
        {-2, 4, 4, 1}, {-2, 4, 2, 1}, {-2, 4, 3, 0}, {-2, 4, 6, 0},
        {2, 0, 2, 1}, {2, 0, 4, 1}, {2, 0, 0, 0}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CssAnPlusB form;

        form.a = cases[i].a;
        form.b = cases[i].b;
        RoboCheckEqInt(CssAnPlusBMatches(&form, cases[i].index),
                          cases[i].expected);
    }
    RoboCheckEqInt(CssAnPlusBMatches(NULL, 1), 0);
}

static void TestCssAnPlusBParseTable(void)
{
    static const struct {
        const char *text;
        int a;
        int b;
    } good[] = {
        {"odd", 2, 1}, {"even", 2, 0}, {"ODD", 2, 1},
        {"3", 0, 3}, {"-1", 0, -1}, {"+2", 0, 2},
        {"n", 1, 0}, {"N", 1, 0}, {"-n", -1, 0}, {"+n", 1, 0},
        {"2n", 2, 0}, {"-3n", -3, 0},
        {"2n+1", 2, 1}, {"2n + 1", 2, 1}, {"2n +1", 2, 1},
        {"2n-1", 2, -1}, {"2n - 1", 2, -1},
        {"n-1", 1, -1}, {"-n+3", -1, 3}, {" 2n+1 ", 2, 1}, {"\n2n+1\t", 2, 1},
        /* The maximum, and the value that is exactly one past it. Eleven digits
         * is where a naive accumulator overflows a 32-bit `long` — which is a
         * signed-overflow bug rather than the "too many digits" answer — so
         * both ends of the bound are pinned here. */
        {"1000000000", 0, 1000000000}, {"2n+1000000000", 2, 1000000000},
        {"-1000000000", 0, -1000000000}
    };
    static const char *bad[] = {
        "", " ", "2n+", "2 n", "n*n", "oddn", "+", "-", "2n+1+1", "abc", "1.5",
        "2n +", "evenn",
        /* One past the bound, a run long enough to overflow any accumulator,
         * and a bound overflow hiding in the offset. */
        "1000000001", "99999999999", "2n+1000000001", "10000000000"
    };
    size_t i;

    for (i = 0u; i < sizeof(good) / sizeof(good[0]); i++) {
        CssAnPlusB form;

        form.a = 99;
        form.b = 99;
        RoboCheckEqInt(CssAnPlusBParse(good[i].text,
                                                  strlen(good[i].text), &form, NULL),
                          RoboOk);
        RoboCheckEqInt(form.a, good[i].a);
        RoboCheckEqInt(form.b, good[i].b);
    }
    for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CssAnPlusB form;

        RoboCheckEqInt(CssAnPlusBParse(bad[i], strlen(bad[i]), &form,
                                                  NULL),
                          RoboParseError);
    }
    RoboCheckEqInt(CssAnPlusBParse("2", 1u, NULL, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Names and the arena                                                 */
/* ------------------------------------------------------------------ */

static void TestCssSelectSimpleRangesCoverTheirSigils(void)
{
    /* `.a#b[x]:root` — a diagnostic that underlined only `a` of `.a` would
     * point at the wrong column, so every simple selector's range must cover the
     * text that introduced it: the dot, the hash, the brackets, the colons. The
     * spans here are the whole point of the test; the byte offsets are counted
     * from the literal, not from what the parser happens to report. */
    CssSelectorList list;
    static const struct {
        size_t index;
        size_t begin;
        size_t end;
    } spans[] = {
        {0u, 0u, 2u},   /* .a     */
        {1u, 2u, 4u},   /* #b     */
        {2u, 4u, 7u},   /* [x]    */
        {3u, 7u, 12u}   /* :root  */
    };
    size_t i;

    CssSelectParse(".a#b[x]:root", &list);
    RoboCheckEqInt(CssSelectFirstCompoundCount(&list), 4u);
    for (i = 0u; i < sizeof(spans) / sizeof(spans[0]); i++) {
        const CssSimpleSelector *simple =
            CssSelectSimple(&list, 0u, 0u, spans[i].index);

        RoboCheckEqInt(simple->range.begin, spans[i].begin);
        RoboCheckEqInt(simple->range.end, spans[i].end);
    }
    /* And the compound inherits its extent from them, so a compound selector
     * begins where its first simple selector does. */
    RoboCheckEqInt(CssComplexSelectorCompoundAt(
                          CssSelectorListAt(&list, 0u), 0u)
                          ->range.begin,
                      0u);
    RoboCheckEqInt(CssComplexSelectorCompoundAt(
                          CssSelectorListAt(&list, 0u), 0u)
                          ->range.end,
                      12u);
    CssSelectorListDispose(&list);
}

static void TestCssSelectPseudoElementRange(void)
{
    /* A pseudo-element's two colons are part of it. */
    CssSelectorList list;
    const CssSimpleSelector *simple;

    CssSelectParse("p::before", &list);
    simple = CssSelectSimple(&list, 0u, 0u, 1u);
    RoboCheckEqInt(simple->kind, RoboCssSimpleSelectorPseudoElement);
    RoboCheckEqInt(simple->range.begin, 1u);
    RoboCheckEqInt(simple->range.end, 9u);
    /* The compound spans both simples: from `p` to the end of `::before`. */
    RoboCheckEqInt(CssComplexSelectorCompoundAt(
                          CssSelectorListAt(&list, 0u), 0u)
                          ->range.begin,
                      0u);
    RoboCheckEqInt(CssComplexSelectorCompoundAt(
                          CssSelectorListAt(&list, 0u), 0u)
                          ->range.end,
                      9u);
    CssSelectorListDispose(&list);
}

static void TestCssSelectNameArena(void)
{
    CssSelectorList list;
    char text[64];

    /* Every name in the list lives in the list, so the bytes are readable for
     * as long as the list is — and they are the *resolved* bytes. */
    CssSelectParse(".alpha#beta, .gamma\\:delta", &list);
    RoboCheckEqInt(CssSelectorListNames(&list) != NULL, 1);
    RoboCheckEqInt(CssSelectorListNamesLength(&list) > 0u, 1);

    CssSelectText(CssSelectSimple(&list, 1u, 0u, 0u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "gamma:delta"), 0);
    CssSelectorListDispose(&list);
    RoboCheckEqInt(CssSelectorListNames(&list) == NULL, 1);
}

static void TestCssSelectNestedListOwnsItsNames(void)
{
    CssSelectorList list;
    const CssSelectorList *argument;
    const CssCompoundSelector *compound;
    char text[32];

    /* The nested list is a complete list with its own arena, so its names are
     * readable after the parent's arena has been filled further. */
    CssSelectParse(":not(.muted.strong)", &list);
    compound = CssComplexSelectorCompoundAt(
        CssSelectorListAt(&list, 0u), 0u);
    argument = CssCompoundSelectorArgumentOf(compound, 0u);
    RoboCheckEqInt(argument != NULL, 1);
    RoboCheckEqInt(CssSelectorListNames(argument) != NULL, 1);
    CssSelectText(CssSelectSimple(argument, 0u, 0u, 1u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "strong"), 0);
    CssSelectorListDispose(&list);
}

static void TestCssSelectNameCapacityCoversEscapes(void)
{
    CssSelectorList list;
    char text[32];

    /* A `\0` escape expands: two source bytes become the three bytes of U+FFFD.
     * The pre-reserved arena has to cover that, or interning would have to grow
     * and every name already recorded would move. */
    CssSelectParse(".a\\0\\0\\0", &list);
    RoboCheckEqInt(CssSelectSimple(&list, 0u, 0u, 0u)->name.length, 10u);
    CssSelectText(CssSelectSimple(&list, 0u, 0u, 0u)->name, text, sizeof(text));
    RoboCheckEqInt(strcmp(text, "a\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"), 0);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Rule integration                                                    */
/* ------------------------------------------------------------------ */

static void TestCssSelectRulePreludeBecomesSelectors(void)
{
    CssError error;
    CssStylesheetId sheet;
    const CssStyleRule *rule;
    const CssSource *source;

    sheet = CssParseSheet("p, .a > b { color: red; }", &error);
    rule = CssSheetStyleRule(sheet, 0u);
    RoboCheckEqInt(rule != NULL, 1);

    /* 9.3 left it empty on purpose; this is what fills it. */
    RoboCheckEqInt(CssStyleRuleSelectorCount(rule), 0u);
    source = CssStylesheetSource(sheet);
    RoboCheckEqInt(CssStyleRuleParseSelectors((CssStyleRule *)rule,
                                                         source, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssErrorIsError(&error), 0);
    RoboCheckEqInt(CssStyleRuleSelectorCount(rule), 2u);

    {
        const CssSelectorList *selectors = CssStyleRuleSelectors(rule);

        char text[16];

        RoboCheckEqInt(CssSelectorListCount(selectors), 2u);
        RoboCheckEqInt(CssSelectSimple(selectors, 0u, 0u, 0u)->kind,
                          RoboCssSimpleSelectorType);
        CssSelectText(CssSelectSimple(selectors, 0u, 0u, 0u)->name, text,
                        sizeof(text));
        RoboCheckEqInt(strcmp(text, "p"), 0);
        /* The prelude's own range is the source's coordinate system, so the
         * selector's range starts where the prelude does. */
        RoboCheckEqInt(CssSelectorListAt(selectors, 0u)->range.begin,
                          rule->preludeRange.begin);
        RoboCheckEqInt(CssSelectorListAt(selectors, 1u)->range.begin,
                          3u);
    }
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssSelectRuleReparseIsIdempotent(void)
{
    CssError error;
    CssStylesheetId sheet;
    const CssStyleRule *rule;
    const CssSource *source;

    sheet = CssParseSheet("a.b, c d { color: red; }", &error);
    rule = CssSheetStyleRule(sheet, 0u);
    source = CssStylesheetSource(sheet);

    RoboCheckEqInt(CssStyleRuleParseSelectors((CssStyleRule *)rule,
                                                         source, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleRuleParseSelectors((CssStyleRule *)rule,
                                                         source, &error, NULL),
                      RoboOk);
    /* Parsing twice replaces rather than accumulates — and does not leak the
     * list it replaced. */
    RoboCheckEqInt(CssStyleRuleSelectorCount(rule), 2u);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssSelectRuleWithBadPreludeKeepsTheRule(void)
{
    CssError error;
    CssStylesheetId sheet;
    const CssStyleRule *rule;
    const CssSource *source;

    sheet = CssParseSheet("p > { color: red; }", &error);
    rule = CssSheetStyleRule(sheet, 0u);
    RoboCheckEqInt(rule != NULL, 1);
    source = CssStylesheetSource(sheet);

    CssErrorInit(&error);
    RoboCheckEqInt(CssStyleRuleParseSelectors((CssStyleRule *)rule,
                                                         source, &error, NULL),
                      RoboParseError);
    RoboCheckEqInt(CssErrorIsError(&error), 1);
    /* The rule survives with an empty selector list and its declarations: a
     * prelude nobody can read does not delete the rule. */
    RoboCheckEqInt(CssStyleRuleSelectorCount(rule), 0u);
    RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 1u);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssSelectRuleScale(void)
{
    CssError error;
    CssStylesheetId sheet;
    const CssSource *source;
    size_t index;

    /* Every rule of a many-rule sheet, including at-rules and malformed ones,
     * which is where a range that is off by a byte shows up. */
    sheet = CssParseSheet(
        "h1 { color: red; }\n"
        ".button { padding: 10px; }\n"
        "#title { font-size: 24px; }\n"
        "input[type=\"text\"] { width: 200px; }\n"
        "body { background: white; }\n",
        &error);
    source = CssStylesheetSource(sheet);
    for (index = 0u; index < CssStylesheetRuleCount(sheet); index++) {
        const CssStyleRule *rule = CssSheetStyleRule(sheet, index);

        if (!rule) {
            continue;
        }
        CssErrorInit(&error);
        RoboCheckEqInt(CssStyleRuleParseSelectors(
                              (CssStyleRule *)rule, source, &error, NULL),
                          RoboOk);
        RoboCheckEqInt(CssStyleRuleSelectorCount(rule), 1u);
    }
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssSelectParseRangeBounds(void)
{
    CssSource source;
    CssSelectorList list = RoboCssCleared;
    CssError error;

    CssSourceOfString(&source, "div p");
    CssErrorInit(&error);
    /* A range past the end is refused, and the output stays a valid empty list
     * rather than an uninitialised one. */
    RoboCheckEqInt(CssSelectorListParseRange(
                          &list, &source, CssRangeSpan(0u, 64u), &error, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssErrorIsError(&error), 1);
    RoboCheckEqInt(CssSelectorListCount(&list), 0u);
    CssSelectorListDispose(&list);

    CssErrorInit(&error);
    RoboCheckEqInt(CssSelectorListParseRange(
                          &list, &source, CssRangeSpan(4u, 1u), &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssSelectorListCount(&list), 1u);
    /* Positions are source positions, not text positions. */
    RoboCheckEqInt(CssSelectorListAt(&list, 0u)->range.begin, 4u);
    CssSelectorListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void TestCssSelectLifetimeRepeat(void)
{
    int weEnabled = CssAllocatorEnsureEnabled();
    size_t baseline = MemDebugLiveCount();
    size_t i;

    for (i = 0u; i < 200u; i++) {
        CssSelectorList list;

        CssSelectParse("div.a#b > ul li:nth-child(2n+1):not(.x, .y)", &list);
        RoboCheckEqInt(CssSelectorListCount(&list), 1u);
        CssSelectorListDispose(&list);
    }
    RoboCheckEqInt(MemDebugLiveCount(), baseline);
    CssAllocatorRestore(weEnabled);
}

static void TestCssSelectLifetimeFailedParseReleasesEverything(void)
{
    int weEnabled = CssAllocatorEnsureEnabled();
    size_t baseline = MemDebugLiveCount();
    size_t i;

    /* A rejected selector must leave nothing behind: failures happen in the
     * middle of a partially built tree, which is where leaks live. */
    for (i = 0u; i < 200u; i++) {
        CssSelectorList list = RoboCssCleared;
        CssError error;

        CssErrorInit(&error);
        RoboCheckEqInt(CssSelectorListParseText(
                              &list, "div > ul li:nth-child(", &error, NULL),
                          RoboParseError);
        CssSelectorListDispose(&list);
    }
    RoboCheckEqInt(MemDebugLiveCount(), baseline);
    CssAllocatorRestore(weEnabled);
}

static void TestCssSelectApiArguments(void)
{
    CssSelectorList list;

    CssFixture();
    RoboCheckEqInt(CssSelectorListParseText(NULL, "p", NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSelectorListParseText(&list, NULL, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSelectorListParseSlice(&list,
                                                        CssSliceEmpty(), NULL,
                                                        NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssSelectorListParseRange(&list, NULL,
                                                        CssRangeAt(0u), NULL,
                                                        NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleRuleParseSelectors(NULL, NULL, NULL, NULL),
                      RoboInvalidArgument);
}

void TestRegisterCssSelectParse(void);

void TestRegisterCssSelectParse(void)
{
    TestRegister("css.select.parse.single_type", TestCssSelectSingleType);
    TestRegister("css.select.parse.list_order", TestCssSelectListOrder);
    TestRegister("css.select.parse.list_whitespace_around_commas",
                       TestCssSelectListWhitespaceAroundCommas);
    TestRegister("css.select.parse.type_keeps_spelling",
                       TestCssSelectTypeKeepsSpelling);
    TestRegister("css.select.parse.type_unicode", TestCssSelectTypeUnicode);
    TestRegister("css.select.parse.type_escaped", TestCssSelectTypeEscaped);
    TestRegister("css.select.parse.universal_alone",
                       TestCssSelectUniversalAlone);
    TestRegister("css.select.parse.universal_in_compound",
                       TestCssSelectUniversalInCompound);
    TestRegister("css.select.parse.universal_descendant",
                       TestCssSelectUniversalDescendant);
    TestRegister("css.select.parse.id_and_class", TestCssSelectIdAndClass);
    TestRegister("css.select.parse.class_multiple_in_one_compound",
                       TestCssSelectClassMultipleInOneCompound);
    TestRegister("css.select.parse.class_hyphen_and_underscore",
                       TestCssSelectClassHyphenAndUnderscore);
    TestRegister("css.select.parse.escaped_identifiers",
                       TestCssSelectEscapedIdentifiers);
    TestRegister("css.select.parse.id_escaped_digits",
                       TestCssSelectIdEscapedDigits);
    TestRegister("css.select.parse.attribute_presence",
                       TestCssSelectAttributePresence);
    TestRegister("css.select.parse.attribute_all_operators",
                       TestCssSelectAttributeAllOperators);
    TestRegister("css.select.parse.attribute_value_forms",
                       TestCssSelectAttributeValueForms);
    TestRegister("css.select.parse.attribute_case_flags",
                       TestCssSelectAttributeCaseFlags);
    TestRegister("css.select.parse.attribute_whitespace_and_escapes",
                       TestCssSelectAttributeWhitespaceAndEscapes);
    TestRegister("css.select.parse.attribute_in_compound",
                       TestCssSelectAttributeInCompound);
    TestRegister("css.select.parse.combinators_all",
                       TestCssSelectCombinatorsAll);
    TestRegister("css.select.parse.combinator_whitespace_forms",
                       TestCssSelectCombinatorWhitespaceForms);
    TestRegister("css.select.parse.comment_is_not_a_combinator",
                       TestCssSelectCommentIsNotACombinator);
    TestRegister("css.select.parse.complex_selector",
                       TestCssSelectComplexSelector);
    TestRegister("css.select.parse.complex_four_compounds",
                       TestCssSelectComplexFourCompounds);
    TestRegister("css.select.parse.structural_pseudo_names",
                       TestCssSelectStructuralPseudoNames);
    TestRegister("css.select.parse.nth_child_forms",
                       TestCssSelectNthChildForms);
    TestRegister("css.select.parse.nth_last_child",
                       TestCssSelectNthLastChild);
    TestRegister("css.select.parse.pseudo_name_case_insensitive",
                       TestCssSelectPseudoNameCaseInsensitive);
    TestRegister("css.select.parse.not_parses_its_argument",
                       TestCssSelectNotParsesItsArgument);
    TestRegister("css.select.parse.not_accepts_a_list_and_a_complex",
                       TestCssSelectNotAcceptsAListAndAComplex);
    TestRegister("css.select.parse.not_nests", TestCssSelectNotNests);
    TestRegister("css.select.parse.pseudo_elements",
                       TestCssSelectPseudoElements);
    TestRegister("css.select.parse.pseudo_element_differs_from_pseudo_class",
                       TestCssSelectPseudoElementDiffersFromPseudoClass);
    TestRegister("css.select.parse.pseudo_element_in_compound",
                       TestCssSelectPseudoElementInCompound);
    TestRegister("css.select.parse.pseudo_class_case",
                       TestCssSelectPseudoClassCase);
    TestRegister("css.select.parse.an_plus_b_membership",
                       TestCssAnPlusBMembership);
    TestRegister("css.select.parse.an_plus_b_parse_table",
                       TestCssAnPlusBParseTable);
    TestRegister("css.select.parse.simple_ranges_cover_their_sigils",
                       TestCssSelectSimpleRangesCoverTheirSigils);
    TestRegister("css.select.parse.pseudo_element_range",
                       TestCssSelectPseudoElementRange);
    TestRegister("css.select.parse.name_arena", TestCssSelectNameArena);
    TestRegister("css.select.parse.nested_list_owns_its_names",
                       TestCssSelectNestedListOwnsItsNames);
    TestRegister("css.select.parse.name_capacity_covers_escapes",
                       TestCssSelectNameCapacityCoversEscapes);
    TestRegister("css.select.parse.rule_prelude_becomes_selectors",
                       TestCssSelectRulePreludeBecomesSelectors);
    TestRegister("css.select.parse.rule_reparse_is_idempotent",
                       TestCssSelectRuleReparseIsIdempotent);
    TestRegister("css.select.parse.rule_with_bad_prelude_keeps_the_rule",
                       TestCssSelectRuleWithBadPreludeKeepsTheRule);
    TestRegister("css.select.parse.rule_scale", TestCssSelectRuleScale);
    TestRegister("css.select.parse.parse_range_bounds",
                       TestCssSelectParseRangeBounds);
    TestRegister("css.select.parse.lifetime_repeat",
                       TestCssSelectLifetimeRepeat);
    TestRegister("css.select.parse.lifetime_failed_parse_releases_everything",
                       TestCssSelectLifetimeFailedParseReleasesEverything);
    TestRegister("css.select.parse.api_arguments", TestCssSelectApiArguments);
}
