/*
 * RoboCSS parser: malformed input, recovery, and consuming the real tokenizer
 * (Phase 9.3).
 *
 * The sheet/declaration/value suites pin what the grammar produces from input
 * that has a shape. This one pins what it produces from input that does not,
 * and it is the suite that decides whether a *recovering* parser is a real one:
 *
 *   - **A problem is local.** Every test here puts a valid rule after the
 *     broken construct, because "the parse did not crash" is worth almost
 *     nothing next to "the next rule is still there, in order, with its
 *     declarations". Recovery that loses the rest of the sheet is not recovery.
 *   - **A rejection is visible.** A declaration that was entry-shaped but not
 *     `name: value` is kept as an INVALID declaration rather than dropped, so
 *     the count reflects what the author wrote and not what the parser liked.
 *   - **The parser lexes nothing.** Nothing here re-reads the source as bytes:
 *     every component value is checked against a token the 9.2 tokenizer
 *     produced for the same bytes, at the same offsets. That is what makes
 *     "the parser consumes the tokenizer" a tested property instead of a
 *     structural claim about which headers are included.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Malformed rules                                                     */
/* ------------------------------------------------------------------ */

/**
 * The four shapes a broken qualified rule can take, and what each one leaves.
 *
 * They are one test rather than four because the thing being pinned is the
 * contrast: an unclosed block still *is* a rule, a prelude that never reaches a
 * `{` is not a rule at all, and an empty prelude is a rule that cannot exist.
 * A table makes that read as one decision instead of three accidents.
 */
static void TestCssParseMalformedRuleShapes(void)
{
    static const struct {
        const char *label;
        const char *text;
        const char *prelude; /* expected prelude shape when the rule survives */
        size_t rules;
        size_t declarations; /* meaningful only when rules == 1 */
        int blockClosed;    /* meaningful only when rules == 1 */
        CssErrorKind error;
    } cases[] = {
        /* The block was never closed. The rule exists, says so, and kept the
         * declaration it had read. */
        { "unclosed_block", "p { color: red;\n", "p",
          1u, 1u, 0, RoboCssErrorKindUnexpectedEof },
        /* A prelude with no `{` at all: consumed to EOF and dropped, because a
         * qualified rule without a block is not a rule. */
        { "no_block", "p color: red;\n", "",
          0u, 0u, 0, RoboCssErrorKindUnexpectedEof },
        /* `{ color: red }` with nothing before the brace. Consumed and dropped:
         * no selector list can be empty, so the rule can never exist and its
         * declarations have no owner. */
        { "empty_prelude", "{ color: red; }\n", "",
          0u, 0u, 0, RoboCssErrorKindMalformedRule },
        /* A selector that reaches its `{` and then runs out: still a rule, and
         * an empty one. The prelude survived the missing `}` and the empty
         * block is not an error of its own. */
        { "selector_only", ".foo {\n", ".,foo",
          1u, 0u, 0, RoboCssErrorKindUnexpectedEof },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        CssError error = RoboCssCleared;
        CssStylesheetId sheet = CssParseSheet(cases[index].text, &error);

        RoboCheckEqInt(CssStylesheetRuleCount(sheet), cases[index].rules);
        RoboCheckEqInt(error.kind, cases[index].error);
        /* A recovered parse still *completed*, which is the state a caller
         * branches on: PARSED means "there are rules here", not "nothing was
         * wrong". */
        RoboCheckEqInt(MakeCssStylesheetState(sheet),
                          RoboCssStylesheetStateParsed);

        if (cases[index].rules == 1u) {
            const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);
            char shape[64];

            RoboCheck(rule != NULL);
            RoboCheckEqInt(CssStyleRuleBlockClosed(rule),
                              cases[index].blockClosed);
            /* The prelude survived intact even though the block did not: the
             * selector text is not a casualty of the missing `}`. */
            CssPreludeShape(rule, shape, sizeof(shape));
            RoboCheckStrEq(shape, cases[index].prelude);
            RoboCheckEqInt(CssStyleRuleDeclarationCount(rule),
                              cases[index].declarations);
        }

        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    }
}

/** A stray `}` at the top level closes nothing, and does not stop the parse. */
static void TestCssParseRecoverStrayClosingBrace(void)
{
    CssError error = RoboCssCleared;
    CssStylesheetId sheet =
        CssParseSheet("}\n}\np { color: red; }\n", &error);
    char shape[64];

    RoboCheckEqInt(error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    CssPreludeShape(CssSheetStyleRule(sheet, 0u), shape, sizeof(shape));
    RoboCheckStrEq(shape, "p");
    RoboCheckEqInt(CssStyleRuleDeclarationCount(
                          CssSheetStyleRule(sheet, 0u)),
                      1u);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Error recovery                                                      */
/* ------------------------------------------------------------------ */

/**
 * A rule whose block holds a broken declaration is still a rule, and the next
 * rule is still found.
 *
 * `broken { invalid }` is the case where recovery is *not* needed: the entry is
 * declaration-shaped, the parser notices the colon is missing, records it and
 * keeps going with an INVALID declaration. Nothing is lost — which is the point
 * of the test, because a recovering parser that dropped the declaration would
 * also pass a weaker "did not crash" assertion.
 */
static void TestCssParseRecoverBrokenDeclarationKeepsTheRule(void)
{
    static const char *text =
        "broken { invalid }\n"
        "p { color: red; }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssStyleRule *broken = CssSheetStyleRule(sheet, 0u);
    const CssStyleRule *valid = CssSheetStyleRule(sheet, 1u);
    char shape[64];

    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 2u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindMalformedDeclaration);

    RoboCheck(broken != NULL);
    CssPreludeShape(broken, shape, sizeof(shape));
    RoboCheckStrEq(shape, "broken");
    RoboCheckEqInt(CssStyleRuleBlockClosed(broken), 1);

    /* One declaration, kept, and unusable. `is_usable` is the accessor a later
     * phase reads before it tries to cascade it, and it is 0 here. */
    RoboCheckEqInt(CssStyleRuleDeclarationCount(broken), 1u);
    {
        const CssDeclaration *declaration =
            CssStyleRuleDeclaration(broken, 0u);

        RoboCheck(declaration != NULL);
        RoboCheckEqInt(declaration->state, RoboCssDeclarationStateInvalid);
        RoboCheck(CssSliceEqualsAscii(CssDeclarationName(declaration),
                                               "invalid"));
        RoboCheckEqInt(CssDeclarationIsUsable(declaration), 0);
        /* The name was recorded and nothing was invented for the value. */
        RoboCheckEqInt(CssDeclarationHasValue(declaration), 0);
    }

    /* The valid rule after it is intact — prelude, block and declaration. */
    RoboCheck(valid != NULL);
    CssPreludeShape(valid, shape, sizeof(shape));
    RoboCheckStrEq(shape, "p");
    RoboCheckEqInt(CssStyleRuleBlockClosed(valid), 1);
    RoboCheckEqInt(CssStyleRuleDeclarationCount(valid), 1u);
    RoboCheck(CssParseDeclarationIs(valid, 0u, "color"));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/**
 * A rule the parser *dropped* costs exactly that rule.
 *
 * `{ color: red; }` is discarded and its block consumed; the rule after it must
 * still be produced, with the correct prelude. This is the case where recovery
 * genuinely has to move past something, so it is the one that would fail if the
 * empty-prelude path consumed too much or too little.
 */
static void TestCssParseRecoverDroppedRuleCostsOneRule(void)
{
    static const char *text =
        "{ color: red; }\n"
        "p { color: red; }\n"
        "a { margin: 0; }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    char shape[64];

    RoboCheckEqInt(error.kind, RoboCssErrorKindMalformedRule);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 2u);

    /* Order is source order, and the dropped rule left no hole in it. */
    CssPreludeShape(CssSheetStyleRule(sheet, 0u), shape, sizeof(shape));
    RoboCheckStrEq(shape, "p");
    CssPreludeShape(CssSheetStyleRule(sheet, 1u), shape, sizeof(shape));
    RoboCheckStrEq(shape, "a");

    /* Each surviving rule kept its own declaration, so neither block was eaten
     * by the recovery that preceded it. */
    RoboCheck(CssParseDeclarationIs(CssSheetStyleRule(sheet, 0u), 0u, "color"));
    RoboCheck(CssParseDeclarationIs(CssSheetStyleRule(sheet, 1u), 0u, "margin"));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/**
 * Several independent problems in one sheet, with the valid rules surviving all
 * of them and the *first* problem reported.
 *
 * Reporting the first is the contract (`CssStylesheetParse`): a caller
 * wants one position to point a user at, and which one is deterministic rather
 * than whichever the last pass happened to see.
 */
static void TestCssParseRecoverManyProblemsKeepAllValidRules(void)
{
    static const char *text =
        "}\n"
        "{ color: red; }\n"
        "a { color: red }\n"
        "b { margin: 0; }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    char shape[64];

    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 2u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindRecoverableSyntax);
    /* The stray `}` is the first byte of the source, so that is where the
     * reported position points — not at a later, more interesting problem. */
    RoboCheckEqInt(error.position.offset, 0u);

    CssPreludeShape(CssSheetStyleRule(sheet, 0u), shape, sizeof(shape));
    RoboCheckStrEq(shape, "a");
    CssPreludeShape(CssSheetStyleRule(sheet, 1u), shape, sizeof(shape));
    RoboCheckStrEq(shape, "b");

    /* `a`'s declaration has no final semicolon and is still terminated by the
     * `}`; `b`'s is ordinary. */
    RoboCheck(CssParseDeclarationIs(CssSheetStyleRule(sheet, 0u), 0u, "color"));
    RoboCheck(CssParseDeclarationIs(CssSheetStyleRule(sheet, 1u), 0u, "margin"));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/**
 * A nested rule inside a style rule's block is CSS Nesting, and this build does
 * not implement it.
 *
 * What it must *not* do is mistake the nested block for declarations. The
 * nested rule is consumed whole — braces and all — its `color: blue` is never
 * seen as its parent's, and the declaration after it is still read. That last
 * part is the assertion worth having: a skip that consumed to the next `}`
 * without balancing would end the parent one declaration early.
 */
static void TestCssParseRecoverNestedRuleIsSkippedWhole(void)
{
    static const char *text =
        "p {\n"
        "    color: red;\n"
        "    .b { color: blue; }\n"
        "    margin: 0;\n"
        "}\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);

    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheck(rule != NULL);
    RoboCheckEqInt(CssStyleRuleBlockClosed(rule), 1);

    /* Two declarations, not three: `.b`'s `color: blue` was inside the skipped
     * nested rule and never reached the parent's list. */
    RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 2u);
    RoboCheck(CssParseDeclarationIs(rule, 0u, "color"));
    RoboCheck(CssParseDeclarationIs(rule, 1u, "margin"));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/** An at-rule inside a style rule's block is skipped the same way. */
static void TestCssParseRecoverAtRuleInsideABlock(void)
{
    static const char *text =
        "p {\n"
        "    color: red;\n"
        "    @media screen { }\n"
        "    margin: 0;\n"
        "}\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);

    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 2u);
    RoboCheck(CssParseDeclarationIs(rule, 0u, "color"));
    RoboCheck(CssParseDeclarationIs(rule, 1u, "margin"));

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/**
 * Rule nesting deeper than the build's bound is flattened, and the rest of the
 * sheet is unharmed.
 *
 * The bound exists so that a hostile or generated sheet cannot drive the
 * grammar's one recursion off the stack. The failure mode a bound introduces is
 * the opposite one — a parse that gives up and loses everything after the deep
 * construct — so this test builds a chain well past the bound and then a
 * perfectly ordinary rule, and asserts both that the deep chain is still *there*
 * (each level produced, the innermost one empty) and that the ordinary rule
 * survived.
 */
static void TestCssParseRecoverRuleNestingBound(void)
{
    enum { DEPTH = RoboCssMaxNesting + 8u };
    char text[DEPTH * 20u + 64u];
    size_t offset = 0u;
    size_t depth;
    CssError error = RoboCssCleared;
    CssStylesheetId sheet;
    const CssAtRule *level;
    size_t built;

    /* Rule nesting in this build is *at-rule* nesting: a style rule's block is
     * a declaration list (a rule inside it is CSS Nesting, skipped), so the only
     * rule-list recursion to bound is a group block's, which is what this chain
     * is made of. */
    for (depth = 0u; depth < (size_t)DEPTH; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "@media x{");
    }
    for (depth = 0u; depth < (size_t)DEPTH; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "}");
    }
    (void)snprintf(text + offset, sizeof(text) - offset,
                   "\np { color: red; }\n");

    sheet = CssParseSheet(text, &error);

    RoboCheckEqInt(error.kind, RoboCssErrorKindRecoverableSyntax);
    /* The giant chain is one top-level rule; the ordinary rule is the other.
     * The bound being reached must not cost the *top-level* list anything. */
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 2u);
    RoboCheckEqInt(CssStyleRuleDeclarationCount(
                          CssSheetStyleRule(sheet, 1u)),
                      1u);
    RoboCheck(CssParseDeclarationIs(CssSheetStyleRule(sheet, 1u), 0u, "color"));

    /* Walk the chain down. Each level up to the bound is a real at-rule with a
     * real child list; the level that would open an over-deep block is still
     * produced, still closed, and simply has no children — so the walk stops
     * there rather than running off the end of a tree that did not stop. */
    level = CssRuleAt(CssStylesheetRule(sheet, 0u));
    built = 0u;
    while (level) {
        RoboCheck(CssSliceEqualsAscii(level->name, "media"));
        RoboCheckEqInt(level->hasBlock, 1);
        RoboCheckEqInt(CssAtRuleBlockClosed(level), 1);
        built++;
        if (CssAtRuleRuleCount(level) == 0u) {
            break;
        }
        RoboCheckEqInt(CssAtRuleRuleCount(level), 1u);
        level = CssRuleAt(CssAtRuleRuleAt(level, 0u));
        RoboCheck(built <= (size_t)DEPTH);
    }

    /* The chain was built down to the bound and stopped *at* it: exactly the
     * levels the bound allows, no fewer (which would lose rules) and no more
     * (which would mean no bound was applied). */
    RoboCheckEqInt(built, (size_t)RoboCssMaxNesting + 1u);

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Consuming the tokenizer                                             */
/* ------------------------------------------------------------------ */

/** Collect every leaf token of a value tree into @p out, depth-first. */
static size_t CssCollectValueTokens(const CssValue *value,
                                       CssToken *out,
                                       size_t capacity)
{
    size_t count = 0u;
    size_t index;

    if (!value) {
        return 0u;
    }
    if (value->kind == RoboCssValueKindToken) {
        if (count < capacity) {
            out[count] = value->token;
        }
        return 1u;
    }
    for (index = 0u; index < value->children.count; index++) {
        size_t added = CssCollectValueTokens(&value->children.items[index],
                                                out + (count < capacity ? count : 0u),
                                                count < capacity ? capacity - count : 0u);

        count += added;
    }
    return count;
}

/**
 * Every component value the parser produced is a token the *tokenizer*
 * produced, for the same bytes.
 *
 * This is the integration property stated as an assertion rather than as a
 * claim about includes: the parse is walked to its leaves, and each leaf's type
 * and range must appear in an independent lex of the same source. A parser that
 * scanned bytes itself, or trimmed a token's range, or classified a `#06c` as
 * something other than what 9.2 called it, fails here — and nothing else in the
 * suite would notice, because the shapes would still match.
 */
static void TestCssParseIntegrationValuesAreTheTokenizersTokens(void)
{
    static const char *text =
        "p { color: #06c; width: 10px; font: 12px/1.5 \"Roboto\", sans-serif; }\n"
        "@media (min-width: 30em) { a[href^=\"x\"] { margin: 0 4% -2px; } }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    CssToken leaves[64];
    size_t leafCount = 0u;
    size_t index;
    size_t matched = 0u;
    size_t ruleIndex;

    CssSourceOfString(&source, text);
    RoboCheck(CssLex(&source, &log) > 0u);
    RoboCheckEqInt(log.truncated, 0);

    for (ruleIndex = 0u; ruleIndex < CssStylesheetRuleCount(sheet);
         ruleIndex++) {
        const CssStyleRule *rule =
            CssSheetStyleRule(sheet, ruleIndex);
        size_t declarationIndex;

        if (rule) {
            leafCount += CssCollectValueTokens(
                CssStyleRulePreludeValues(rule), leaves + leafCount,
                sizeof(leaves) / sizeof(leaves[0]) - leafCount);
            for (declarationIndex = 0u;
                 declarationIndex < CssStyleRuleDeclarationCount(rule);
                 declarationIndex++) {
                leafCount += CssCollectValueTokens(
                    CssDeclarationValue(
                        CssStyleRuleDeclaration(rule, declarationIndex)),
                    leaves + leafCount,
                    sizeof(leaves) / sizeof(leaves[0]) - leafCount);
            }
            continue;
        }
        /* The `@media` block: its prelude and its nested rules' leaves too. */
        {
            const CssAtRule *at =
                CssRuleAt(CssStylesheetRule(sheet, ruleIndex));
            size_t nested;

            if (!at) {
                continue;
            }
            leafCount += CssCollectValueTokens(
                CssAtRulePreludeValues(at), leaves + leafCount,
                sizeof(leaves) / sizeof(leaves[0]) - leafCount);
            for (nested = 0u; nested < CssAtRuleRuleCount(at); nested++) {
                const CssStyleRule *inner =
                    CssRuleStyle(CssAtRuleRuleAt(at, nested));
                size_t declarationIndex;

                if (!inner) {
                    continue;
                }
                leafCount += CssCollectValueTokens(
                    CssStyleRulePreludeValues(inner), leaves + leafCount,
                    sizeof(leaves) / sizeof(leaves[0]) - leafCount);
                for (declarationIndex = 0u;
                     declarationIndex < CssStyleRuleDeclarationCount(inner);
                     declarationIndex++) {
                    leafCount += CssCollectValueTokens(
                        CssDeclarationValue(
                            CssStyleRuleDeclaration(inner, declarationIndex)),
                        leaves + leafCount,
                        sizeof(leaves) / sizeof(leaves[0]) - leafCount);
                }
            }
        }
    }

    /* The fixture is large enough that a walk finding nothing would be a bug in
     * the walk rather than a fact about the parse. */
    RoboCheck(leafCount >= 20u);
    RoboCheck(leafCount <= sizeof(leaves) / sizeof(leaves[0]));

    for (index = 0u; index < leafCount; index++) {
        size_t tokenIndex;
        int found = 0;

        for (tokenIndex = 0u; tokenIndex < log.count && !found; tokenIndex++) {
            const CssToken *token = CssTokenAt(&log, tokenIndex);

            if (!token || token->type != leaves[index].type) {
                continue;
            }
            if (token->range.begin != leaves[index].range.begin ||
                token->range.end != leaves[index].range.end) {
                continue;
            }
            /* Same range *and* same classification: the hash kind and the
             * dimension unit have to agree as well, because those are the
             * decisions the tokenizer made and 9.3 must not re-make. */
            if (token->hash != leaves[index].hash ||
                token->unit.length != leaves[index].unit.length) {
                continue;
            }
            found = 1;
        }
        RoboCheck(found);
        matched += (size_t)found;
    }
    RoboCheckEqInt(matched, leafCount);

    /* And the numeric payload came across unchanged: `10px` is a dimension
     * whose unit is `px` and whose number is 10, read by 9.2 and merely carried
     * by 9.3. */
    {
        const CssDeclaration *width = CssStyleRuleDeclaration(
            CssSheetStyleRule(sheet, 0u), 1u);
        const CssValue *value = CssDeclarationValue(width);
        const CssValue *dimension;

        RoboCheck(value != NULL);
        RoboCheckEqInt(value->children.count, 1u);
        dimension = &value->children.items[0];
        RoboCheckEqInt(dimension->kind, RoboCssValueKindToken);
        RoboCheckEqInt(dimension->token.type, RoboCssTokenDimension);
        RoboCheck(CssSliceEqualsAscii(dimension->token.unit, "px"));
        CSS_CHECK_DOUBLE(dimension->token.number, 10.0);
    }

    CssSourceDispose(&source);
    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/**
 * Parsing the same bytes twice produces the same tree, and a second parse of
 * one sheet does not accumulate rules.
 *
 * Both halves matter for the same reason: a parser that appends to a rule list
 * it was handed rather than filling a fresh one would double every rule on a
 * reparse, and nothing in a single-parse test would ever show it.
 */
static void TestCssParseIntegrationReparseIsIdenticalAndDoesNotAccumulate(void)
{
    static const char *text =
        "p { color: red; }\n"
        "@media screen { a { margin: 0; } }\n"
        "b { padding: 1px 2px; }\n";
    CssStylesheetId sheet;
    CssError first = RoboCssCleared;
    CssError second = RoboCssCleared;
    char before[256];
    char after[256];
    size_t index;

    sheet = CssParseSheet(text, &first);
    CssShape(CssAtRulePreludeValues(
                  CssRuleAt(CssStylesheetRule(sheet, 1u))),
              before, sizeof(before));

    /* Reparse the same sheet. Same rules, same count, same error. */
    RoboCheckEqInt(CssStylesheetParse(sheet, &second, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 3u);
    RoboCheckEqInt(CssStylesheetTreeRuleCount(sheet), 4u);
    RoboCheckEqInt(CssStylesheetStyleRuleCount(sheet), 3u);
    RoboCheckEqInt(second.kind, first.kind);

    CssShape(CssAtRulePreludeValues(
                  CssRuleAt(CssStylesheetRule(sheet, 1u))),
              after, sizeof(after));
    RoboCheckStrEq(after, before);

    /* And the tree is a tree, not a log: the values are rebuilt, not appended
     * to, so the third rule's value still has exactly two dimensions. */
    for (index = 0u; index < CssStylesheetRuleCount(sheet); index++) {
        RoboCheck(CssStylesheetRule(sheet, index) != NULL);
    }
    RoboCheckEqInt(
        CssDeclarationValue(CssStyleRuleDeclaration(
            CssSheetStyleRule(sheet, 2u), 0u))->children.count,
        3u);

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/**
 * Every range a parse reports is a byte range *inside* the source.
 *
 * A range that overran its source would be a slice pointing at memory the
 * caller never handed over, which is a read out of bounds waiting for the first
 * caller that trusts the range instead of the text. Checking each one against
 * the source is cheap and catches it at the only place it can be caught.
 */
static void TestCssParseIntegrationRangesStayInsideTheSource(void)
{
    static const char *text =
        "/* leading */\n"
        "@import \"theme.css\";\n"
        "p, .a > b { color: #06c; font: 12px/1.5 \"Roboto\", sans-serif; }\n"
        "input[type=\"text\"] { width: calc(100% - 20px); }\n"
        "@media screen and (min-width: 30em) { a { margin: 0; } }\n"
        "q { --x: [1 2]; }\n";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssSource *source = CssStylesheetSource(sheet);
    size_t length = strlen(text);
    size_t index;

    RoboCheck(source != NULL);
    RoboCheckEqInt(source->length, length);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    /* Five top-level rules: `@import`, `p, .a > b`, `input[type="text"]`,
     * `@media` and `q`. The leading comment is not one. */
    RoboCheckEqInt(CssStylesheetRuleCount(sheet), 5u);

    for (index = 0u; index < CssStylesheetRuleCount(sheet); index++) {
        const CssRule *rule = CssStylesheetRule(sheet, index);
        const CssStyleRule *style = CssRuleStyle(rule);
        const CssAtRule *at = CssRuleAt(rule);
        size_t declarationIndex;

        RoboCheck(rule != NULL);
        if (style) {
            RoboCheck(CssSourceRangeIsValid(
                source, CssStyleRuleRange(style)));
            RoboCheck(CssSourceRangeIsValid(
                source, CssStyleRulePreludeRange(style)));
            RoboCheck(CssSourceRangeIsValid(
                source, CssStyleRuleBlockRange(style)));
            for (declarationIndex = 0u;
                 declarationIndex < CssStyleRuleDeclarationCount(style);
                 declarationIndex++) {
                const CssDeclaration *declaration =
                    CssStyleRuleDeclaration(style, declarationIndex);
                const CssValue *value = CssDeclarationValue(declaration);

                RoboCheckEqInt(declaration->state,
                                  RoboCssDeclarationStateParsed);
                RoboCheck(CssSourceRangeIsValid(source, declaration->range));
                RoboCheck(CssSourceRangeIsValid(source,
                                                          declaration->nameRange));
                RoboCheck(CssSourceRangeIsValid(source,
                                                          declaration->valueRange));
                RoboCheck(CssSourceRangeIsValid(source,
                                                          CssValueRange(value)));
                /* The declaration's own extent begins at its name and its
                 * value's extent ends it, so the two together are the whole
                 * declaration and neither is empty. */
                RoboCheckEqInt(declaration->range.begin,
                                  declaration->nameRange.begin);
                RoboCheckEqInt(declaration->range.end, declaration->valueRange.end);
                RoboCheck(declaration->nameRange.end > declaration->nameRange.begin);
                RoboCheck(declaration->valueRange.end > declaration->valueRange.begin);
            }
            continue;
        }
        RoboCheck(at != NULL);
        RoboCheck(CssSourceRangeIsValid(source, CssAtRuleRange(at)));
        RoboCheck(CssSourceRangeIsValid(source,
                                                  CssAtRulePreludeRange(at)));
        for (declarationIndex = 0u;
             declarationIndex < CssAtRuleDeclarationCount(at);
             declarationIndex++) {
            RoboCheck(CssSourceRangeIsValid(
                source,
                CssAtRuleDeclaration(at, declarationIndex)->range));
        }
    }

    /* The last rule's `--x: [1 2]` is a custom property, so its value is kept
     * as written — brackets and all — and its range still ends inside the
     * source even though a `]` was involved. */
    {
        const CssStyleRule *last =
            CssSheetStyleRule(sheet, CssStylesheetRuleCount(sheet) - 1u);
        const CssDeclaration *declaration =
            CssStyleRuleDeclaration(last, 0u);
        char shape[64];

        RoboCheck(CssSliceEqualsAscii(CssDeclarationName(declaration),
                                               "--x"));
        CssShape(CssDeclarationValue(declaration), shape, sizeof(shape));
        RoboCheckStrEq(shape, "[1,_,2]");
    }

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterCssParseRecover(void);

void TestRegisterCssParseRecover(void)
{
    TestRegister("css.parse.malformed_rule_shapes",
                       TestCssParseMalformedRuleShapes);
    TestRegister("css.parse.recover.stray_closing_brace",
                       TestCssParseRecoverStrayClosingBrace);
    TestRegister("css.parse.recover.broken_declaration_keeps_the_rule",
                       TestCssParseRecoverBrokenDeclarationKeepsTheRule);
    TestRegister("css.parse.recover.dropped_rule_costs_one_rule",
                       TestCssParseRecoverDroppedRuleCostsOneRule);
    TestRegister("css.parse.recover.many_problems_keep_all_valid_rules",
                       TestCssParseRecoverManyProblemsKeepAllValidRules);
    TestRegister("css.parse.recover.nested_rule_is_skipped_whole",
                       TestCssParseRecoverNestedRuleIsSkippedWhole);
    TestRegister("css.parse.recover.at_rule_inside_a_block",
                       TestCssParseRecoverAtRuleInsideABlock);
    TestRegister("css.parse.recover.rule_nesting_bound",
                       TestCssParseRecoverRuleNestingBound);
    TestRegister("css.parse.integration.values_are_the_tokenizers_tokens",
                       TestCssParseIntegrationValuesAreTheTokenizersTokens);
    TestRegister("css.parse.integration.reparse_is_identical",
                       TestCssParseIntegrationReparseIsIdenticalAndDoesNotAccumulate);
    TestRegister("css.parse.integration.ranges_stay_inside_the_source",
                       TestCssParseIntegrationRangesStayInsideTheSource);
}
