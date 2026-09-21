/*
 * RoboCSS Phase 9.1 robustness suite:
 * empty, whitespace-only, large, incomplete and malformed input; deep and wide
 * structures; repeated creation and destruction; and the Unicode matrix.
 *
 * Phase 9.1 has no parser, so "malformed CSS" cannot fail here the way it will
 * in 9.3. What it can do — and what these tests check — is be *representable*:
 * every one of these inputs is carried by the source model, described by its
 * positions, and released without leaving an allocation behind.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSStyle.h"

/** A large but reasonable stylesheet, assembled once. */
static char cssBigSource[32768];
static size_t cssBigLength;

static void CssBuildBigSource(void)
{
    static const char rule[] = "p { color: red; margin: 10px; }\n";
    size_t ruleLength = sizeof(rule) - 1u;

    if (cssBigLength > 0u) {
        return;
    }
    while (cssBigLength + ruleLength + 1u < sizeof(cssBigSource)) {
        memcpy(cssBigSource + cssBigLength, rule, ruleLength);
        cssBigLength += ruleLength;
    }
    cssBigSource[cssBigLength] = '\0';
}

static void TestCssRobustEmptyAndWhitespace(void)
{
    static const char *inputs[] = {"", " ", "\n", "\r\n", "\t\f\r ", "/* nothing */"};
    size_t i;

    CssFixture();
    for (i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        CssSource source = RoboCssCleared;
        CssParser parser = RoboCssCleared;
        CssToken token = RoboCssCleared;

        CssSourceOfString(&source, inputs[i]);
        RoboCheckEqInt(CssSourceLength(&source), strlen(inputs[i]));
        RoboCheck(CssSourceIsValidUtf8(&source));
        /* Every one of these is a legal place to put a cursor, whether or not
         * there is anything to read there. */
        RoboCheckEqInt(CssParserInit(&parser, &source, RoboCssOriginAuthor,
                                               NULL),
                          RoboOk);
        RoboCheckEqInt(CssParserOffset(&parser), 0u);
        RoboCheckEqInt(CssParserRemaining(&parser), strlen(inputs[i]));
        RoboCheckEqInt(CssTokenMakeEof(&token, &source, strlen(inputs[i]),
                                                  NULL),
                          RoboOk);
        RoboCheckEqInt(token.type, RoboCssTokenEof);
        CssParserDispose(&parser);
        CssSourceDispose(&source);
    }

    /* An empty source is not the same as no source at all, and the model keeps
     * the difference visible. */
    {
        CssSource empty = RoboCssCleared;
        CssSource unset = RoboCssCleared;

        CssSourceInit(&unset);
        CssSourceInit(&empty);
        RoboCheckEqInt(CssSourceSetEmpty(&empty, NULL), RoboOk);
        RoboCheckEqInt(CssSourceIsConfigured(&empty), 1);
        RoboCheckEqInt(CssSourceIsConfigured(&unset), 0);
        RoboCheck(CssSourceIsEmpty(&empty));
        RoboCheck(CssSourceIsEmpty(&unset));
    }
}

static void TestCssRobustLargeSource(void)
{
    CssSource source = RoboCssCleared;
    CssPosition position;
    size_t expectedBreaks = 0u;
    size_t i;

    CssFixture();
    CssBuildBigSource();
    RoboCheck(cssBigLength > 16000u);
    RoboCheckEqInt(CssSourceSetMemory(&source, (const uint8_t *)cssBigSource,
                                                 cssBigLength, NULL),
                      RoboOk);

    for (i = 0u; i < cssBigLength; i++) {
        if (cssBigSource[i] == '\n') {
            expectedBreaks++;
        }
    }
    RoboCheckEqInt(CssSourceLineCount(&source), expectedBreaks + 1u);
    RoboCheckEqInt(CssSourceScalarCount(&source), cssBigLength);
    RoboCheck(CssSourceIsValidUtf8(&source));

    /* A position at the very end is still a position, and resolving several
     * positions over a 30 KB source stays linear and total. */
    RoboCheckEqInt(CssSourcePosition(&source, cssBigLength, &position),
                      RoboOk);
    RoboCheckEqInt(position.line, (uint32_t)(expectedBreaks + 1u));
    RoboCheckEqInt(position.column, 1u);
    RoboCheckEqInt(CssSourcePosition(&source, 0u, &position), RoboOk);
    RoboCheckEqInt(position.line, 1u);

    /* Copying the whole thing owns an equal number of bytes and releases them. */
    {
        int weEnabled = CssAllocatorEnsureEnabled();
        size_t baseline = MemDebugLiveCount();
        CssSource copy = RoboCssCleared;

        CssSourceInit(&copy);
        RoboCheckEqInt(CssSourceCopy(&copy, (const uint8_t *)cssBigSource,
                                               cssBigLength, NULL),
                          RoboOk);
        RoboCheck(CssSourceIsOwned(&copy));
        RoboCheckEqInt(CssSourceLength(&copy), cssBigLength);
        RoboCheckEqInt(CssSourceBytes(&copy)[cssBigLength - 1u],
                          (uint8_t)'\n');
        CssSourceDispose(&copy);
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        CssAllocatorRestore(weEnabled);
    }

    CssSourceDispose(&source);
}

static void TestCssRobustIncompleteAndMalformed(void)
{
    static const char *inputs[] = {
        "p {",
        "p { color:",
        "p { color: red",
        "p { color: red;",
        "p { color: red; } @media",
        "@media print {",
        "div > > p {",
        "a { ] }",
        "[unclosed",
        "/* unterminated comment",
        "\"unterminated string",
        "} } }",
        ";;;",
        "::",
        "p { color: red !important",
        "\xEF\xBB\xBFp { color: red }",
        "p { content: \"\x01\x02\"; }"
    };
    size_t i;

    CssFixture();
    for (i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        CssSource source = RoboCssCleared;
        CssSlice whole;
        size_t j;
        int same = 1;

        /* Malformed input is representable: the bytes are kept exactly, the
         * length is exact, and every offset is a position. Nothing here claims
         * to have understood the CSS, which is the point. */
        CssSourceOfString(&source, inputs[i]);
        RoboCheckEqInt(CssSourceLength(&source), strlen(inputs[i]));
        whole = CssSourceSlice(&source);
        RoboCheckEqInt(whole.length, strlen(inputs[i]));
        for (j = 0u; j < strlen(inputs[i]); j++) {
            if (whole.bytes[j] != (uint8_t)inputs[i][j]) {
                same = 0;
            }
        }
        RoboCheck(same);
        for (j = 0u; j <= strlen(inputs[i]); j++) {
            CssPosition position;

            RoboCheckEqInt(CssSourcePosition(&source, j, &position), RoboOk);
            RoboCheckEqInt(position.offset, j);
            RoboCheck(position.line >= 1u);
            RoboCheck(position.column >= 1u);
        }
        CssSourceDispose(&source);
    }

    /* Malformed markup through the declaration parser: no crash, no leak, and a
     * defined answer. A `}` ends a declaration list, so this text is a
     * declaration list that ends immediately — the same thing a browser does
     * with a stray brace inside a `style` attribute, because there is one
     * declaration-list algorithm and this is it. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;

        RoboCheckEqInt(CssDeclarationListParseText("}}}::;;", 7u,
                                                               RoboCssOriginInline,
                                                               &list, &error),
                          RoboOk);
        RoboCheckEqInt(CssDeclarationListCount(&list), 0u);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        CssDeclarationListDispose(&list);
    }

    /* The recovery boundaries, exercised through the parser rather than
     * described: a run of declarations with three different problems in it
     * still yields every declaration that was fine, in source order, and the
     * *first* problem is the one reported. */
    {
        CssDeclarationList list = RoboCssCleared;
        CssError error = RoboCssCleared;
        const char *text =
            "a: 1; b c; : 2; d: 4; e: calc(1px + 2px); f: 6";

        RoboCheckEqInt(CssDeclarationListParseText(
                              text, strlen(text), RoboCssOriginInline, &list, &error),
                          RoboOk);
        RoboCheckEqInt(CssDeclarationListCount(&list), 6u);
        RoboCheckEqInt(error.kind, RoboCssErrorKindMalformedDeclaration);
        RoboCheckEqInt(CssDeclarationListAt(&list, 1u)->state,
                          RoboCssDeclarationStateInvalid);
        RoboCheckEqInt(CssDeclarationListAt(&list, 2u)->state,
                          RoboCssDeclarationStateInvalid);
        RoboCheckEqInt(CssDeclarationListAt(&list, 5u)->state,
                          RoboCssDeclarationStateParsed);
        CssDeclarationListDispose(&list);
    }
}

static void TestCssRobustUnicodeThroughModels(void)
{
    size_t i;

    CssFixture();
    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        static const char prefix[] = "/* ";
        static const char suffix[] = " */\np { color: red; }\n";
        char rule[512];
        size_t ruleLength = 0u;
        CssSource source = RoboCssCleared;
        CssStylesheetId sheet = RoboCssStylesheetIdNone;
        CssToken token = RoboCssCleared;
        CssDeclaration declaration = RoboCssCleared;
        CssSelectorList selectors = RoboCssCleared;
        CssValue value = RoboCssCleared;

        memcpy(rule, prefix, sizeof(prefix) - 1u);
        ruleLength += sizeof(prefix) - 1u;
        memcpy(rule + ruleLength, cssSamples[i].text, strlen(cssSamples[i].text));
        ruleLength += strlen(cssSamples[i].text);
        memcpy(rule + ruleLength, suffix, sizeof(suffix) - 1u);
        ruleLength += sizeof(suffix) - 1u;
        rule[ruleLength] = '\0';

        CssSourceOfString(&source, rule);

        /* A token over the non-ASCII span keeps those exact bytes. */
        RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, &source,
                                              CssRangeSpan(sizeof(prefix) - 1u,
                                                                  strlen(cssSamples[i].text)),
                                              NULL),
                          RoboOk);
        RoboCheckEqInt(CssTokenText(&token).length,
                          strlen(cssSamples[i].text));
        RoboCheck(memcmp(CssTokenText(&token).bytes, cssSamples[i].text,
                          strlen(cssSamples[i].text)) == 0);

        /* A declaration named in that script, and a selector class too. */
        CssDeclarationMake(&declaration, CssSliceFromText(cssSamples[i].text),
                                  RoboCssDeclarationStateParsed, NULL);
        RoboCheck(CssSliceEqualsAscii(CssDeclarationName(&declaration),
                                               cssSamples[i].text));
        RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
        RoboCheckEqInt(CssDeclarationSetValue(&declaration, &value, NULL),
                          RoboOk);
        CssSelectorListInit(&selectors);
        {
            CssComplexSelector complex = RoboCssCleared;

            CssComplexSelectorOf(&complex, &source, "p", cssSamples[i].text);
            RoboCheckEqInt(CssSelectorListAppend(&selectors, &complex, NULL),
                              RoboOk);
        }
        RoboCheckEqInt(
            CssCompoundSelectorCount(
                CssComplexSelectorCompoundAt(
                    CssSelectorListAt(&selectors, 0u), 0u)),
            2u);
        RoboCheck(CssSliceEqualsAscii(
                       CssCompoundSelectorAt(
                           CssComplexSelectorCompoundAt(
                               CssSelectorListAt(&selectors, 0u), 0u),
                           1u)
                           ->name,
                       cssSamples[i].text));
        CssSelectorListDispose(&selectors);

        /* And a stylesheet that owns the whole thing, so the bytes outlive the
         * buffer the caller stops tracking. */
        RoboCheckEqInt(CssStylesheetCreate(&sheet, RoboCssOriginAuthor,
                                                     NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetCopySource(sheet,
                                                          (const uint8_t *)rule,
                                                          ruleLength, NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetSourceLength(sheet), ruleLength);
        {
            CssRule styleRule = RoboCssCleared;

            RoboCheckEqInt(CssRuleMakeStyle(&styleRule, NULL), RoboOk);
            RoboCheckEqInt(CssStyleRuleAppendDeclaration(&styleRule.style,
                                                                     &declaration, NULL),
                              RoboOk);
            RoboCheckEqInt(CssStylesheetAppendRule(sheet, &styleRule, NULL),
                              RoboOk);
        }
        RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);

        CssSourceDispose(&source);
    }
}

static void TestCssRobustRepeatedChurn(void)
{
    int weEnabled;
    size_t baseline;
    int i;

    CssFixture();
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();

    /* Repeated stylesheet lifetimes must return to the same live count, which
     * is what "reusable slots" has to mean in practice. */
    for (i = 0; i < (int)RoboCssMaxStylesheets * 3; i++) {
        CssStylesheetId id = RoboCssStylesheetIdNone;

        RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                          RoboOk);
        CssStylesheetSetName(id, "churn", NULL);
        RoboCheckEqInt(CssStylesheetCopySource(
                              id, (const uint8_t *)CSS_FIXTURE_ATTRIBUTE,
                              strlen(CSS_FIXTURE_ATTRIBUTE), NULL),
                          RoboOk);
        RoboCheckEqInt(CssStylesheetDestroy(id, NULL), RoboOk);
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    }
    RoboCheckEqInt(CssLiveStylesheetCount(), 0u);
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);

    /* The same for style contexts and property tables, which own their own
     * storage rather than living in module slots. */
    for (i = 0; i < 64; i++) {
        CssStyleContext *context = NULL;
        CssPropertyTable *table = NULL;
        CssPropertyId id = RoboCssPropertyIdNone;

        RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);
        RoboCheckEqInt(CssPropertyTableRegister(table, "color", 0u, &id,
                                                           NULL),
                          RoboOk);
        RoboCheckEqInt(CssStyleContextCreate(&context, NULL), RoboOk);
        RoboCheckEqInt(CssStyleContextSetPropertyTable(context, table,
                                                                     NULL),
                          RoboOk);
        CssStyleContextDestroy(context);
        CssPropertyTableDestroy(table);
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    }

    CssAllocatorRestore(weEnabled);
}

static void TestCssRobustDeepRuleNesting(void)
{
    CssRule root = RoboCssCleared;
    CssRule wrapper = RoboCssCleared;
    const CssRule *flattened[128];
    int depth = 64;
    int i;

    CssFixture();
    /* Built inside-out, because a rule is moved into its parent: appending the
     * next level into a rule that has already been moved away would append
     * into a rule that no longer exists. Two locals and a value assignment keep
     * exactly one owner at every step. */
    RoboCheckEqInt(CssRuleMakeAt(&root, CssSliceFromText("media"),
                                            NULL),
                      RoboOk);
    RoboCheckEqInt(CssAtRuleSetPrelude(&root.at, CssSliceEmpty(), 1,
                                                   NULL),
                      RoboOk);
    for (i = 1; i < depth; i++) {
        RoboCheckEqInt(CssRuleMakeAt(&wrapper,
                                                CssSliceFromText("supports"),
                                                NULL),
                          RoboOk);
        RoboCheckEqInt(CssAtRuleSetPrelude(&wrapper.at,
                                                       CssSliceEmpty(), 1, NULL),
                          RoboOk);
        RoboCheckEqInt(CssAtRuleAppendRule(&wrapper.at, &root, NULL),
                          RoboOk);
        root = wrapper;
        CssRuleInit(&wrapper);
    }
    {
        CssRule leaf = RoboCssCleared;

        RoboCheckEqInt(CssRuleMakeStyle(&leaf, NULL), RoboOk);
        RoboCheckEqInt(CssAtRuleAppendRule(&root.at, &leaf, NULL), RoboOk);
    }

    /* 64 at-rules plus the style rule at the bottom, counted through the tree
     * and again through a depth-first flatten: two measurements of the same
     * tree that must agree. */
    RoboCheckEqInt(CssRuleTreeCount(&root), (size_t)depth + 1u);
    RoboCheckEqInt(CssRuleListFlatten(root.at.children, flattened, 128u),
                      (size_t)depth);
    RoboCheckEqInt(flattened[0]->kind, RoboCssRuleKindAt);
    RoboCheckEqInt(flattened[depth - 1]->kind, RoboCssRuleKindStyle);

    CssRuleDispose(&root);
    RoboCheckEqInt(CssRuleTreeCount(&root), 0u);
}

static void TestCssRobustWideStructures(void)
{
    CssSource source = RoboCssCleared;
    int weEnabled;
    size_t baseline;

    CssFixture();
    CssSourceOfString(&source, "4px");
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();

    /* 512 top-level rules with 4 declarations each: wide, shallow, and released
     * by one dispose. */
    {
        CssRuleList rules = RoboCssCleared;
        int i;

        CssRuleListInit(&rules);
        for (i = 0; i < 512; i++) {
            CssRule rule = RoboCssCleared;
            int j;

            RoboCheckEqInt(CssRuleMakeStyle(&rule, NULL), RoboOk);
            for (j = 0; j < 4; j++) {
                CssDeclaration declaration = RoboCssCleared;
                CssValue value = RoboCssCleared;

                CssDeclarationMake(&declaration,
                                          CssSliceFromText("color"),
                                          RoboCssDeclarationStateParsed, NULL);
                CssValueTokenAt(&value, &source, "4px", NULL);
                RoboCheckEqInt(CssDeclarationSetValue(&declaration, &value,
                                                                 NULL),
                                  RoboOk);
                RoboCheckEqInt(CssStyleRuleAppendDeclaration(&rule.style,
                                                                         &declaration,
                                                                         NULL),
                                  RoboOk);
            }
            RoboCheckEqInt(CssRuleListAppend(&rules, &rule, NULL), RoboOk);
        }
        RoboCheckEqInt(CssRuleListCount(&rules), 512u);
        RoboCheckEqInt(CssRuleListStyleRuleCount(&rules), 512u);
        RoboCheckEqInt(CssRuleListTreeCount(&rules), 512u);
        RoboCheckEqInt(CssRuleListFlatten(&rules, NULL, 0u), 512u);
        CssRuleListDispose(&rules);
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    }

    /* 256 alternatives in one selector list: the width a real stylesheet has. */
    {
        CssSelectorList selectors = RoboCssCleared;
        int i;

        CssSelectorListInit(&selectors);
        for (i = 0; i < 256; i++) {
            CssComplexSelector complex = RoboCssCleared;

            CssComplexSelectorOf(&complex, &source, "p", "x");
            RoboCheckEqInt(CssSelectorListAppend(&selectors, &complex, NULL),
                              RoboOk);
        }
        RoboCheckEqInt(CssSelectorListCount(&selectors), 256u);
        RoboCheck(CssSelectorListIsWellFormed(&selectors));
        CssSelectorListDispose(&selectors);
        RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    }

    CssAllocatorRestore(weEnabled);
}

static void TestCssRobustDeepValueTrees(void)
{
    CssValue root = RoboCssCleared;
    CssValue wrapper = RoboCssCleared;
    int depth = 64;
    int i;

    CssFixture();
    /* Inside-out again, for the same reason rules are: an appended value is
     * moved, so the container that is appended into must still be the owner. */
    RoboCheckEqInt(CssValueMakeList(&root, NULL), RoboOk);
    for (i = 1; i < depth; i++) {
        RoboCheckEqInt(CssValueMakeFunction(&wrapper,
                                                       CssSliceFromText("f"),
                                                       CssRangeAt(0u), NULL),
                          RoboOk);
        RoboCheckEqInt(CssValueAppend(&wrapper, &root, NULL), RoboOk);
        root = wrapper;
        CssValueInit(&wrapper);
    }
    RoboCheckEqInt(CssValueDepth(&root), (size_t)depth);
    RoboCheckEqInt(CssValueTreeCount(&root), (size_t)depth);
    RoboCheckEqInt(CssValueChildCount(&root), 1u);

    /* Disposal recurses as deep as the tree is; 64 frames is far above what a
     * parser will allow and far below a stack budget. */
    CssValueDispose(&root);
    RoboCheckEqInt(CssValueDepth(&root), 0u);
    RoboCheckEqInt(CssValueTreeCount(&root), 0u);
}

void TestRegisterCssRobust(void);

void TestRegisterCssRobust(void)
{
    TestRegister("css.robust.empty_and_whitespace",
                       TestCssRobustEmptyAndWhitespace);
    TestRegister("css.robust.large_source", TestCssRobustLargeSource);
    TestRegister("css.robust.incomplete_and_malformed",
                       TestCssRobustIncompleteAndMalformed);
    TestRegister("css.robust.unicode_through_models",
                       TestCssRobustUnicodeThroughModels);
    TestRegister("css.robust.repeated_churn", TestCssRobustRepeatedChurn);
    TestRegister("css.robust.deep_rule_nesting",
                       TestCssRobustDeepRuleNesting);
    TestRegister("css.robust.wide_structures", TestCssRobustWideStructures);
    TestRegister("css.robust.deep_value_trees", TestCssRobustDeepValueTrees);
}
