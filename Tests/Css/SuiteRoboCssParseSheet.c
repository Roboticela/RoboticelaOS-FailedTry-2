/*
 * RoboCSS parser: stylesheets, rules and at-rules (Phase 9.3).
 *
 * What this suite pins is the *structure* level of the grammar: how many rules
 * a sheet has, in what order, what each one's prelude is, where each one's
 * bytes are, and what an at-rule is when the parser has never heard of it.
 *
 * Two properties run through nearly every test here and are worth stating once:
 *
 *   - **Order is source order, and nothing is reordered.** A stylesheet's rules
 *     are the cascade's input (9.5), so a parser that "helpfully" sorted or
 *     deduplicated would be implementing the cascade by accident.
 *   - **An at-rule is syntax and nothing else.** `@media` and `@nonsense` go
 *     through exactly the same code, and the assertions say so: the name is
 *     compared without a table, and the block is a rule list because that is the
 *     only structure a block can have before something knows what it means.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Stylesheets                                                         */
/* ------------------------------------------------------------------ */

static void TestCssParseEmptySheets(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet;

    /* Empty and whitespace-only are both a legal stylesheet with no rules — and
     * both are *clean*: nothing was wrong, there was simply nothing. */
    sheet = CssParseSheet("", &error);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 0u);
    RoboCheckEqInt(MakeCssStylesheetState(sheet),
                      RoboCssStylesheetStateParsed);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    sheet = CssParseSheet("  \n\t\r\n  ", &error);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 0u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    /* A stylesheet that is only a comment is also empty of rules. Comments are
     * not component values, so the comment does not become one. */
    sheet = CssParseSheet("/* nothing here */\n", &error);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 0u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    /* A stylesheet with no attached source parses to no rules rather than
     * failing: a parse reads bytes, and there are none. */
    {
        CssStylesheetId bare = RoboCssStylesheetIdNone;

        CssFixture();
        RoboCheckEqInt(CssStylesheetCreate(&bare, RoboCssOriginAuthor, NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetParse(bare, &error, NULL), RoboOk);
        RoboCheckEqInt(CssStylesheetRuleCount(bare), 0u);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        RoboCheckEqInt(CssStylesheetDestroy(bare, NULL), RoboOk);
    }
}

static void TestCssParseOneRule(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet("p { color: red; }\n", &error);
    const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);
    char shape[256];

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheckEqInt(CssStylesheetTreeRuleCount(sheet), 1u);
    RoboCheckEqInt(CssStylesheetStyleRuleCount(sheet), 1u);
    RoboCheck(rule != NULL);

    CssPreludeShape(rule, shape, sizeof(shape));
    RoboCheckStrEq(shape, "p");
    RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 1u);
    RoboCheckEqInt(rule->blockClosed, 1);

    /* The rule's extent is its prelude through its closing brace, and the block
     * extent starts at the `{` — `p { color: red; }` is 17 bytes, 0..16, with
     * the trailing LF at 17 outside it. Neither end is a byte the rule does not
     * contain: `end` is one past the `}`. */
    RoboCheckEqInt(CssStyleRuleRange(rule).begin, 0u);
    RoboCheckEqInt(CssStyleRulePreludeRange(rule).begin, 0u);
    RoboCheckEqInt(CssStyleRulePreludeRange(rule).end, 1u);
    RoboCheckEqInt(CssStyleRuleBlockRange(rule).begin, 2u);
    RoboCheckEqInt(CssStyleRuleBlockRange(rule).end, 17u);
    RoboCheckEqInt(CssStyleRuleRange(rule).end, 17u);

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseManyRulesInOrder(void)
{
    static const char *text =
        "h1 { color: red; }\n"
        "p { margin: 0; }\n"
        "a { text-decoration: none; }\n"
        ".button { padding: 10px; }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    /* `.button` is two component values — a DELIM and an IDENT — and the shape
     * convention joins children with `,`, so the class selector reads as
     * `.,button`. A shape that said `.button` would be a shape that had folded
     * the delimiter into the ident, which is selector *parsing*. */
    static const char *expected[] = { "h1", "p", "a", ".,button" };
    size_t index;

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 4u);
    RoboCheckEqInt(CssStylesheetStyleRuleCount(sheet), 4u);

    /* Source order, every time. A sheet that reordered its rules would hand the
     * cascade a different input, and a selector list's order is author-visible. */
    for (index = 0u; index < 4u; index++) {
        const CssStyleRule *rule = CssSheetStyleRule(sheet, index);
        char shape[256];

        RoboCheck(rule != NULL);
        CssPreludeShape(rule, shape, sizeof(shape));
        RoboCheckStrEq(shape, expected[index]);
        RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 1u);
    }
    RoboCheck(CssSheetStyleRule(sheet, 4u) == NULL);

    /* Ranges ascend and do not overlap: rule N's start is past rule N-1's end. */
    for (index = 1u; index < 4u; index++) {
        const CssStyleRule *previous = CssSheetStyleRule(sheet, index - 1u);
        const CssStyleRule *rule = CssSheetStyleRule(sheet, index);

        RoboCheck(CssStyleRuleRange(rule).begin >
                   CssStyleRuleRange(previous).end);
    }

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseCommentsAndCdoCdc(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet;
    char shape[256];

    /* A comment between rules is trivia, like whitespace: it does not become a
     * rule, and it does not become part of the rule that follows it. */
    sheet = CssParseSheet("/* one */ p { color: red; } /* two */\n", &error);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    CssPreludeShape(CssSheetStyleRule(sheet, 0u), shape, sizeof(shape));
    RoboCheckStrEq(shape, "p");
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    /* CDO and CDC at the top of a stylesheet are the HTML comment tokens CSS
     * tolerates, and they are ignored rather than becoming a rule's prelude. */
    sheet = CssParseSheet("<!--\np { color: red; }\n-->\n", &error);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    CssPreludeShape(CssSheetStyleRule(sheet, 0u), shape, sizeof(shape));
    RoboCheckStrEq(shape, "p");
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Style rule preludes                                                 */
/* ------------------------------------------------------------------ */

static void TestCssParsePreludeIsComponentValues(void)
{
    static const char *text =
        ".button:hover > span[data-role=\"x\"] , #id + a::before { color: red; }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);
    char shape[512];

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheck(rule != NULL);

    /* Every byte of the prelude is accounted for as component values: no
     * selector has been interpreted, no pseudo-class recognised, and no
     * combinator folded away. This *is* what 9.4 will read. */
    CssPreludeShape(rule, shape, sizeof(shape));
    RoboCheckStrEq(shape,
                      ".,button,:,hover,_,>,_,span,[data-role,=,\"x\"],_,,,_,#id,_,+,_,a,:,:,before");

    /* And nothing was promoted into selectors: that is 9.4's job. */
    RoboCheckEqInt(CssStyleRuleSelectorCount(rule), 0u);

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseDescendantCombinatorNeedsWhitespace(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet;
    char descendant[256];
    char compound[256];

    /* The reason whitespace is a component value. `a b` and `ab` differ by one
     * node here, and by a completely different meaning to 9.4 — a descendant
     * combinator versus a single type selector. A prelude that dropped
     * whitespace could not tell an author's `a b` from their `ab`. */
    sheet = CssParseSheet("a b { color: red; }\n", &error);
    CssPreludeShape(CssSheetStyleRule(sheet, 0u), descendant, sizeof(descendant));
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    sheet = CssParseSheet("a,b { color: red; }\n", &error);
    CssPreludeShape(CssSheetStyleRule(sheet, 0u), compound, sizeof(compound));
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    RoboCheckStrEq(descendant, "a,_,b");
    RoboCheckStrEq(compound, "a,,,b");
    RoboCheck(strcmp(descendant, compound) != 0);
}

static void TestCssParseEmptyBlockIsARule(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(".empty {\n}\n", &error);
    const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);
    char shape[256];

    /* A rule with a prelude and no declarations is legal CSS and stays a rule:
     * "has selectors and no declarations" is a "worth keeping" test, not a
     * validity test. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheck(rule != NULL);
    CssPreludeShape(rule, shape, sizeof(shape));
    RoboCheckStrEq(shape, ".,empty");
    RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 0u);
    RoboCheckEqInt(rule->blockClosed, 1);
    RoboCheckEqInt(CssStyleRuleIsPopulated(rule), 0);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* At-rules                                                            */
/* ------------------------------------------------------------------ */

static void TestCssParseStatementAtRule(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet("@import \"theme.css\";\n", &error);
    const CssRule *rule = CssStylesheetRule(sheet, 0u);
    const CssAtRule *at = CssRuleAt(rule);

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheckEqInt(rule->kind, RoboCssRuleKindAt);
    RoboCheck(at != NULL);

    /* The name is what followed the `@`, without it. */
    RoboCheck(CssSliceEqualsAscii(at->name, "import"));
    /* No block, so nothing was loaded and no children exist. The prelude is the
     * string token, and it stayed a token. */
    RoboCheckEqInt(at->hasBlock, 0);
    RoboCheckEqInt(CssAtRuleRuleCount(at), 0u);
    RoboCheckEqInt(CssAtRuleBlockClosed(at), 0);
    RoboCheck(CssSliceEqualsAscii(at->prelude, "\"theme.css\""));
    {
        char shape[256];

        CssShape(CssAtRulePreludeValues(at), shape, sizeof(shape));
        RoboCheckStrEq(shape, "\"theme.css\"");
    }
    /* Whole-rule extent includes the `;`. */
    RoboCheckEqInt(CssAtRuleRange(at).begin, 0u);
    RoboCheckEqInt(CssAtRuleRange(at).end, strlen("@import \"theme.css\";"));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseBlockAtRuleHoldsRules(void)
{
    static const char *text =
        "@media screen {\n"
        "    p { color: red; }\n"
        "    .x { margin: 0; }\n"
        "}\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssAtRule *at = CssRuleAt(CssStylesheetRule(sheet, 0u));
    char shape[256];

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheck(at != NULL);
    RoboCheck(CssSliceEqualsAscii(at->name, "media"));
    RoboCheckEqInt(at->hasBlock, 1);
    RoboCheckEqInt(CssAtRuleBlockClosed(at), 1);

    /* The prelude is `screen`, trimmed — the newline after `@media` is not part
     * of it — and the block's contents are rules, not component values. */
    CssShape(CssAtRulePreludeValues(at), shape, sizeof(shape));
    RoboCheckStrEq(shape, "screen");

    RoboCheckEqInt(CssAtRuleRuleCount(at), 2u);
    CssPreludeShape(CssRuleStyle(CssAtRuleRuleAt(at, 0u)), shape,
                      sizeof(shape));
    RoboCheckStrEq(shape, "p");
    CssPreludeShape(CssRuleStyle(CssAtRuleRuleAt(at, 1u)), shape,
                      sizeof(shape));
    RoboCheckStrEq(shape, ".,x");

    /* The sheet's counts see through the block; its top-level count does not. */
    RoboCheckEqInt(CssStylesheetTreeRuleCount(sheet), 3u);
    RoboCheckEqInt(CssStylesheetStyleRuleCount(sheet), 2u);

    /* The whole-rule extent covers the block, so it ends at the final `}`. */
    RoboCheckEqInt(CssAtRuleRange(at).end, strlen(text) - 1u);

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseAtRulesAreNotInterpreted(void)
{
    static const char *text =
        "@supports (display: grid) { p { color: red; } }\n"
        "@font-face { font-family: X; src: url(x.woff); }\n"
        "@keyframes spin { from { opacity: 0; } }\n"
        "@totally-made-up foo { p { color: red; } }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    static const char *names[] = { "supports", "font-face", "keyframes",
                                   "totally-made-up" };
    size_t index;

    /* There is no at-rule table and there is no at-rule semantics: the four
     * names go through the same code, and the one this build has never heard of
     * is not treated differently from the three it has. `@font-face` loaded
     * nothing; `@keyframes` evaluated nothing; the made-up name was not refused.
     *
     * What the code *does* do without a table is notice what a block contains,
     * because that is a property of the bytes and not of a name: three of these
     * blocks hold rules, and `@font-face`'s holds declarations. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 4u);
    for (index = 0u; index < 4u; index++) {
        const CssAtRule *at =
            CssRuleAt(CssStylesheetRule(sheet, index));
        const int isDescriptors = index == 1u;

        RoboCheck(at != NULL);
        RoboCheck(CssSliceEqualsAscii(at->name, names[index]));
        RoboCheckEqInt(at->hasBlock, 1);
        RoboCheckEqInt(CssAtRuleBlockClosed(at), 1);
        RoboCheckEqInt(CssAtRuleRuleCount(at), isDescriptors ? 0u : 1u);
        RoboCheckEqInt(CssAtRuleDeclarationCount(at),
                          isDescriptors ? 2u : 0u);
    }

    /* `@font-face`'s two descriptors are its block's declarations, in source
     * order, with their values preserved as text and as component values —
     * nothing was resolved, and `X` was not looked up as a font. */
    {
        const CssAtRule *fontFace =
            CssRuleAt(CssStylesheetRule(sheet, 1u));
        char shape[256];

        RoboCheck(CssSliceEqualsAscii(
            CssDeclarationName(CssAtRuleDeclaration(fontFace, 0u)),
            "font-family"));
        RoboCheck(CssSliceEqualsAscii(
            CssDeclarationName(CssAtRuleDeclaration(fontFace, 1u)),
            "src"));
        CssShape(CssDeclarationValue(CssAtRuleDeclaration(fontFace, 0u)),
                  shape, sizeof(shape));
        RoboCheckStrEq(shape, "X");
        CssShape(CssDeclarationValue(CssAtRuleDeclaration(fontFace, 1u)),
                  shape, sizeof(shape));
        RoboCheckStrEq(shape, "url(x.woff)");
        RoboCheck(CssAtRuleDeclaration(fontFace, 2u) == NULL);
    }

    /* Three group blocks, each with one rule, plus the four at-rules. */
    RoboCheckEqInt(CssStylesheetTreeRuleCount(sheet), 7u);
    RoboCheckEqInt(CssStylesheetStyleRuleCount(sheet), 3u);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseNestedAtRuleBlocks(void)
{
    static const char *text =
        "@media screen {\n"
        "    @supports (display: grid) {\n"
        "        p { color: red; }\n"
        "    }\n"
        "}\n"
        "a { color: blue; }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssAtRule *outer = CssRuleAt(CssStylesheetRule(sheet, 0u));
    const CssAtRule *inner;

    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheck(outer != NULL);
    RoboCheckEqInt(CssAtRuleRuleCount(outer), 1u);

    inner = CssRuleAt(CssAtRuleRuleAt(outer, 0u));
    RoboCheck(inner != NULL);
    RoboCheck(CssSliceEqualsAscii(inner->name, "supports"));
    RoboCheckEqInt(CssAtRuleRuleCount(inner), 1u);
    RoboCheck(CssSliceEqualsAscii(
        CssDeclarationName(CssStyleRuleDeclaration(
            CssRuleStyle(CssAtRuleRuleAt(inner, 0u)), 0u)),
        "color"));

    /* Four rules in the tree: the two at-rules, the `p` inside them and the `a`
     * that followed the whole thing. */
    RoboCheckEqInt(CssStylesheetTreeRuleCount(sheet), 4u);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 2u);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseAtRuleWithoutFinalSemicolon(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet;
    const CssAtRule *at;
    char shape[256];

    /* No `;` and no block, at the end of the input: a statement at-rule whose
     * terminator was omitted. Every browser accepts this, so it is not an
     * error — the prelude is simply where the input stopped. */
    sheet = CssParseSheet("@import \"theme.css\"", &error);
    at = CssRuleAt(CssStylesheetRule(sheet, 0u));
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheck(CssSliceEqualsAscii(at->name, "import"));
    RoboCheckEqInt(at->hasBlock, 0);
    CssShape(CssAtRulePreludeValues(at), shape, sizeof(shape));
    RoboCheckStrEq(shape, "\"theme.css\"");
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

    /* An at-rule with no prelude at all: `@foo;` is a name and a `;`. */
    sheet = CssParseSheet("@foo;\n", &error);
    at = CssRuleAt(CssStylesheetRule(sheet, 0u));
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheck(CssSliceEqualsAscii(at->name, "foo"));
    RoboCheckEqInt(at->hasBlock, 0);
    RoboCheckEqInt(CssValueChildCount(CssAtRulePreludeValues(at)), 0u);
    RoboCheck(CssSliceIsEmpty(at->prelude));
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

static void TestCssParseBlockAtRuleUnclosed(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet("@media screen {\n p { color: red; }\n",
                                                    &error);
    const CssAtRule *at = CssRuleAt(CssStylesheetRule(sheet, 0u));

    /* A block that ran to the end of the input: the at-rule and its rules are
     * real, and the fact that the `}` was never written is recorded rather than
     * guessed at. A closed block and an unclosed one are different rules. */
    RoboCheckEqInt(error.kind, RoboCssErrorKindUnexpectedEof);
    RoboCheck(CssErrorKindIsRecoverable(error.kind));
    RoboCheck(at != NULL);
    RoboCheckEqInt(at->hasBlock, 1);
    RoboCheckEqInt(CssAtRuleBlockClosed(at), 0);
    RoboCheckEqInt(CssAtRuleRuleCount(at), 1u);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Ownership                                                           */
/* ------------------------------------------------------------------ */

static void TestCssParseSheetLifetime(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet;
    size_t baseline;
    int weEnabled = CssAllocatorEnsureEnabled();

    baseline = MemDebugLiveCount();
    /* Parsing a sheet allocates a whole tree and must give every byte of it
     * back: the sheet owns its rules, an at-rule owns its children, a rule owns
     * its prelude values and its declarations, and a declaration owns its value.
     * Four levels of ownership, one measurement. */
    {
        static const char *text =
            "@media screen { p { color: red; } }\n"
            "a.b > c { margin: calc(1px + 2px) 3px !important; }\n";
        size_t round;

        for (round = 0u; round < 4u; round++) {
            sheet = CssParseSheet(text, &error);
            RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
            RoboCheckEqInt(CssStylesheetRuleCount(sheet), 2u);
            RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
    }

    /* A parse that replaces a sheet's rules releases the ones it replaced —
     * parsing twice is idempotent, not additive, and not a leak. */
    sheet = CssParseSheet("p { color: red; }\n", &error);
    RoboCheckEqInt(CssStylesheetParse(sheet, &error, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetParse(sheet, &error, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);

    CssAllocatorRestore(weEnabled);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssParseSheet(void);

void TestRegisterCssParseSheet(void)
{
    TestRegister("css.parse.sheet.empty", TestCssParseEmptySheets);
    TestRegister("css.parse.sheet.one_rule", TestCssParseOneRule);
    TestRegister("css.parse.sheet.many_rules_in_order",
                       TestCssParseManyRulesInOrder);
    TestRegister("css.parse.sheet.comments_and_cdo_cdc",
                       TestCssParseCommentsAndCdoCdc);
    TestRegister("css.parse.rule.prelude_is_component_values",
                       TestCssParsePreludeIsComponentValues);
    TestRegister("css.parse.rule.descendant_combinator_needs_whitespace",
                       TestCssParseDescendantCombinatorNeedsWhitespace);
    TestRegister("css.parse.rule.empty_block_is_a_rule",
                       TestCssParseEmptyBlockIsARule);
    TestRegister("css.parse.at_rule.statement", TestCssParseStatementAtRule);
    TestRegister("css.parse.at_rule.block_holds_rules",
                       TestCssParseBlockAtRuleHoldsRules);
    TestRegister("css.parse.at_rule.not_interpreted",
                       TestCssParseAtRulesAreNotInterpreted);
    TestRegister("css.parse.at_rule.nested_blocks",
                       TestCssParseNestedAtRuleBlocks);
    TestRegister("css.parse.at_rule.optional_semicolon",
                       TestCssParseAtRuleWithoutFinalSemicolon);
    TestRegister("css.parse.at_rule.unclosed_block",
                       TestCssParseBlockAtRuleUnclosed);
    TestRegister("css.parse.sheet.lifetime", TestCssParseSheetLifetime);
}
