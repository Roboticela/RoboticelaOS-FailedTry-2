/*
 * RoboCSS parser: declaration values (Phase 9.3).
 *
 * A declaration's value is the one place in RoboCSS where the model is a *tree*
 * rather than a record, and that is the whole reason 9.2 kept a token's spelling
 * separate from its value: `calc(100% - 20px)` has to survive as a function with
 * four children, because 9.10's arithmetic and 9.11's box model both need the
 * structure and neither can get it back from a string.
 *
 * What these tests are looking for, in one sentence each:
 *
 *   - every byte of a value is accounted for as a component value, and the
 *     value's range is exactly the bytes it covers;
 *   - `(`, `[` and `{` nest, and the value records which closing token matched;
 *   - nesting is bounded, and past the bound the construct goes flat rather than
 *     taking the stack down with it.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Component values                                                    */
/* ------------------------------------------------------------------ */

static void TestCssParseValueComponentVocabulary(void)
{
    static const struct {
        const char *value;
        const char *shape;
    } cases[] = {
        { "red", "red" },
        { "1", "1" },
        { "1.5", "1.5" },
        { "-10", "-10" },
        { "+10", "+10" },
        { "50%", "50%" },
        { "10px", "10px" },
        { "1e3", "1e3" },
        { "#fff", "#fff" },
        { "\"text\"", "\"text\"" },
        { "/", "/" },
        { "a ! b", "a,_,!,_,b" },
        { "auto 0 auto", "auto,_,0,_,auto" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[64];
        char shape[64];
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;

        /* Build `x: <case>` in a buffer the shape helper can read. */
        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
        RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
        CssDeclarationShape(&list, 0u, shape, sizeof(shape));
        RoboCheckStrEq(shape, cases[index].shape);
        CssDeclarationListDispose(&list);
    }
}

static void TestCssParseValueTokensKeepTheirRanges(void)
{
    static const char *text = "p { color: #06c; }";
    CssError error = RoboCssCleared;
    CssStylesheetId sheet = CssParseSheet(text, &error);
    const CssValue *value = CssDeclarationValue(
        CssStyleRuleDeclaration(CssSheetStyleRule(sheet, 0u), 0u));
    const CssValue *token;

    RoboCheck(value != NULL);
    RoboCheckEqInt(value->kind, RoboCssValueKindList);
    RoboCheckEqInt(value->children.count, 1u);
    token = &value->children.items[0];
    RoboCheckEqInt(token->kind, RoboCssValueKindToken);
    RoboCheckEqInt(token->token.type, RoboCssTokenHash);
    /* `#06c` is a hash the tokenizer classified *unrestricted*, because an
     * ident-headed name is what makes a hash an id. 9.3 preserved the
     * classification and did not second-guess it: `#06c` is a valid colour and
     * not a valid selector, and which reading applies is 9.4's or 9.9's. */
    RoboCheckEqInt(token->token.hash, RoboCssHashTypeUnrestricted);

    /* The hash's own range is 11..15 — `#06c` — and the token's text is those
     * same bytes, so a caller can hand either to a diagnostic and get the same
     * answer. */
    RoboCheckEqInt(token->token.range.begin, 11u);
    RoboCheckEqInt(token->token.range.end, 15u);
    RoboCheckEqInt(CssValueRange(value).begin, 11u);
    RoboCheckEqInt(CssValueRange(value).end, 15u);
    RoboCheckEqInt(token->token.text.length, 4u);
    RoboCheck(memcmp(token->token.text.bytes, "#06c", 4u) == 0);

    RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Functions                                                           */
/* ------------------------------------------------------------------ */

static void TestCssParseValueFunctions(void)
{
    static const struct {
        const char *value;
        const char *name;
        size_t children;
    } cases[] = {
        { "rgb(1, 2, 3)", "rgb", 7u },
        { "calc(1px)", "calc", 1u },
        { "var(--x)", "var", 1u },
        { "max()", "max", 0u },
        /* The function's *name* keeps the author's spelling; ASCII-case
         * insensitivity is a property of the function lookup, and there is no
         * function table in this build. */
        { "RGB(1)", "RGB", 1u },
        { "translate(-50%)", "translate", 1u },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value;
        const CssValue *function;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
        value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(value->children.count, 1u);
        function = &value->children.items[0];
        RoboCheckEqInt(function->kind, RoboCssValueKindFunction);
        RoboCheck(CssSliceEqualsAscii(CssValueName(function),
                                              cases[index].name));
        RoboCheckEqInt(CssValueChildCount(function), cases[index].children);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        CssDeclarationListDispose(&list);
    }
}

static void TestCssParseValueNestedFunctions(void)
{
    static const char *text = "width: calc(100% - var(--gutter, 20px));";
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;
    const CssValue *calc;
    const CssValue *var;
    char shape[128];

    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    calc = &CssDeclarationValue(CssDeclarationListAt(&list, 0u))
                ->children.items[0];
    RoboCheckEqInt(calc->kind, RoboCssValueKindFunction);
    RoboCheck(CssSliceEqualsAscii(CssValueName(calc), "calc"));

    /* The `var()` is a *child of the calc*, one level down and inside the
     * arithmetic — which is the one thing a string value could not express.
     * `100% - var(...)` is five children: the two operands, the operator and the
     * whitespace that separates them. */
    RoboCheckEqInt(calc->children.count, 5u);
    var = &calc->children.items[4];
    RoboCheckEqInt(var->kind, RoboCssValueKindFunction);
    RoboCheck(CssSliceEqualsAscii(CssValueName(var), "var"));
    /* `--gutter`, the `,`, the space and `20px` are four children: the comma and
     * the whitespace survive as component values here because nothing has said
     * they are separators yet. Trimming them is the grammar that later *reads*
     * `var()`'s arguments, and that grammar is not this phase's. */
    RoboCheckEqInt(CssValueChildCount(var), 4u);

    CssShape(CssDeclarationValue(CssDeclarationListAt(&list, 0u)),
              shape, sizeof(shape));
    RoboCheckStrEq(shape, "calc(100%,_,-,_,var(--gutter,,,_,20px))");

    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Blocks                                                              */
/* ------------------------------------------------------------------ */

static void TestCssParseValueBlocks(void)
{
    static const struct {
        const char *value;
        const char *shape;
    } cases[] = {
        /* `a[b]` is two component values: an IDENT and a block. They are not
         * one thing until a selector parser says so. */
        { "a[b]", "a,[b]" },
        { "url(x)", "url(x)" },
        { "{}", "{}" },
        { "(a)(b)", "(a),(b)" },
        { "[a(b)]", "[a(b)]" },
        { "{ a: b }", "{_,a,:,_,b,_}" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        char shape[96];
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
        CssDeclarationShape(&list, 0u, shape, sizeof(shape));
        RoboCheckStrEq(shape, cases[index].shape);
        CssDeclarationListDispose(&list);
    }
}

static void TestCssParseValueBlocksRecordTheirMatchingClose(void)
{
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;
    const CssValue *value;
    const CssValue *square;
    const CssValue *paren;
    const CssValue *unmatched;

    RoboCheckEqInt(CssParseDeclarations("x: a[1] (2) [3", &list, &error),
                      RoboOk);
    value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
    /* `a`, `[1]`, space, `(2)`, space, `[3` — six children. `a` and `[1]` are
     * adjacent in the source, so there is no whitespace token between them; the
     * whitespace that *is* there is a child like any other, which is why the
     * blocks sit at 1, 3 and 5 rather than 0, 1 and 2. */
    RoboCheckEqInt(value->children.count, 6u);

    square = &value->children.items[1];
    RoboCheckEqInt(square->kind, RoboCssValueKindBlock);
    RoboCheckEqInt(square->open, RoboCssTokenLeftSquare);
    RoboCheckEqInt(square->close, RoboCssTokenRightSquare);

    paren = &value->children.items[3];
    RoboCheckEqInt(paren->kind, RoboCssValueKindBlock);
    RoboCheckEqInt(paren->open, RoboCssTokenLeftParen);
    RoboCheckEqInt(paren->close, RoboCssTokenRightParen);

    /* The last block never closed. It is still a block, still holds its
     * contents, and its `close` is NONE — "this was left open" is a fact a
     * later phase can act on, and inventing a `]` would hide it. */
    unmatched = &value->children.items[5];
    RoboCheckEqInt(unmatched->kind, RoboCssValueKindBlock);
    RoboCheckEqInt(unmatched->open, RoboCssTokenLeftSquare);
    RoboCheckEqInt(unmatched->close, RoboCssTokenNone);
    RoboCheckEqInt(error.kind, RoboCssErrorKindUnexpectedEof);

    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* The nesting bound                                                   */
/* ------------------------------------------------------------------ */

static void TestCssParseValueNestingBound(void)
{
    char text[1024];
    size_t offset = 0u;
    size_t depth;
    CssError error = RoboCssCleared;
    CssDeclarationList list = RoboCssCleared;

    /* RoboCssMaxNesting + 8 functions, one inside the other. The bound is
     * what keeps adversarial input from being a stack overflow, and what these
     * assertions are for is that hitting it is *reported* and the parse still
     * ends at a defined place with a dispose-able tree. */
    offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "x: ");
    for (depth = 0u; depth < RoboCssMaxNesting + 8u; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "f(");
    }
    offset += (size_t)snprintf(text + offset, sizeof(text) - offset, "1");
    for (depth = 0u; depth < RoboCssMaxNesting + 8u; depth++) {
        offset += (size_t)snprintf(text + offset, sizeof(text) - offset, ")");
    }

    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheck(error.position.offset > 0u);

    /* The tree is at most as deep as the bound allowed, and it is a tree the
     * disposer can walk: this call is the assertion. */
    {
        const CssValue *value =
            CssDeclarationValue(CssDeclarationListAt(&list, 0u));

        RoboCheck(value != NULL);
    }
    CssDeclarationListDispose(&list);

    /* And a depth the bound *does* allow builds the whole thing. */
    {
        char shallow[512];
        size_t at = 0u;
        size_t level;

        at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, "x: ");
        for (level = 0u; level < 16u; level++) {
            at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, "f(");
        }
        at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, "1");
        for (level = 0u; level < 16u; level++) {
            at += (size_t)snprintf(shallow + at, sizeof(shallow) - at, ")");
        }

        RoboCheckEqInt(CssParseDeclarations(shallow, &list, &error), RoboOk);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        {
            const CssValue *value =
                CssDeclarationValue(CssDeclarationListAt(&list, 0u));
            const CssValue *function = value;
            size_t seen = 0u;

            while (function->kind == RoboCssValueKindList) {
                function = &function->children.items[0];
            }
            while (function->kind == RoboCssValueKindFunction) {
                seen++;
                function = &function->children.items[0];
            }
            RoboCheckEqInt(seen, 16u);
            RoboCheckEqInt(function->kind, RoboCssValueKindToken);
        }
        CssDeclarationListDispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* Strings and urls                                                    */
/* ------------------------------------------------------------------ */

static void TestCssParseValueStrings(void)
{
    static const struct {
        const char *value;
        const char *shape;
    } cases[] = {
        { "\"text\"", "\"text\"" },
        { "'text'", "'text'" },
        { "\"\"", "\"\"" },
        /* The shape folds whitespace to `_`, and a string's own text is not an
         * exception: `"a b"` shapes as `"a_b"`. The *token* still holds the
         * space — that is what the next assertion is for. */
        { "\"a b\"", "\"a_b\"" },
        { "\"a\\\"b\"", "\"a\\\"b\"" },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[64];
        char shape[64];
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        CssDeclarationShape(&list, 0u, shape, sizeof(shape));
        RoboCheckStrEq(shape, cases[index].shape);
        CssDeclarationListDispose(&list);
    }

    /* The two ways a string can end badly are two different tokens, and the
     * difference is the specification's: a *newline* inside a string makes it a
     * BAD_STRING (never valid), while *end of input* returns the string token
     * with the error recorded, because a truncated stylesheet is not the same as
     * a malformed one. The parser keeps whichever it was given. */
    {
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value;

        RoboCheckEqInt(CssParseDeclarations("x: \"unterminated", &list, &error),
                          RoboOk);
        RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
        value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(value->children.items[0].token.type, RoboCssTokenString);
        RoboCheckEqInt(value->children.items[0].token.closed, 0);
        CssDeclarationListDispose(&list);

        RoboCheckEqInt(CssParseDeclarations("x: \"broken\nmore", &list, &error),
                          RoboOk);
        value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(value->children.items[0].token.type,
                          RoboCssTokenBadString);
        CssDeclarationListDispose(&list);
    }

    /* And a string's interior whitespace is content, not a separator: the shape
     * helper folds it for readability, so this is the assertion that the token
     * itself kept it. */
    {
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value;
        uint8_t raw[16];
        size_t length = 0u;

        RoboCheckEqInt(CssParseDeclarations("x: \"a b\"", &list, &error),
                          RoboOk);
        value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(CssTokenValueCopy(&value->children.items[0].token, raw,
                                                   sizeof(raw), &length, NULL),
                          RoboOk);
        RoboCheckEqInt(length, 3u);
        RoboCheck(memcmp(raw, "a b", 3u) == 0);
        (void)0;
        CssDeclarationListDispose(&list);
    }
}

static void TestCssParseValueUrlTokens(void)
{
    static const struct {
        const char *value;
        int closed;
    } cases[] = {
        /* An unquoted url is one *token*, and the tokenizer's own test says so;
         * 9.3 preserved it rather than re-reading the bytes. Nothing was
         * fetched, nothing was resolved, no file was opened. */
        { "url(image.png)", 1 },
        { "url()", 1 },
        /* End of input does not close the url, and the specification is explicit
         * that it is still a url — `closed` is where that fact lives. */
        { "url(broken", 0 },
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        char shape[96];
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value;

        snprintf(text, sizeof(text), "x: %s", cases[index].value);
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
        RoboCheckEqInt(CssDeclarationListCount(&list), 1u);

        value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(value->children.count, 1u);
        RoboCheckEqInt(value->children.items[0].kind, RoboCssValueKindToken);
        RoboCheckEqInt(value->children.items[0].token.type, RoboCssTokenUrl);
        RoboCheckEqInt(value->children.items[0].token.closed, cases[index].closed);
        CssDeclarationShape(&list, 0u, shape, sizeof(shape));
        RoboCheckStrEq(shape, cases[index].value);
        CssDeclarationListDispose(&list);
    }
}

static void TestCssParseValueUrlQuotedIsAFunction(void)
{
    static const char *cases[] = {
        "url(\"image.png\")",
        "url('image.png')",
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char text[96];
        char shape[96];
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        const CssValue *value;
        const CssValue *function;

        /* A *quoted* url is a function, not a url token: the specification forks
         * on the quote, and the tokenizer (9.2) is where that fork belongs. The
         * parser's job is to keep whichever it was given, so this asserts the
         * function's name and its string child. */
        snprintf(text, sizeof(text), "x: %s", cases[index]);
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
        value = CssDeclarationValue(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(value->children.count, 1u);
        function = &value->children.items[0];
        RoboCheckEqInt(function->kind, RoboCssValueKindFunction);
        RoboCheck(CssSliceEqualsAscii(CssValueName(function), "url"));
        RoboCheckEqInt(CssValueChildCount(function), 1u);
        RoboCheckEqInt(function->children.items[0].kind, RoboCssValueKindToken);
        RoboCheckEqInt(function->children.items[0].token.type, RoboCssTokenString);
        CssDeclarationShape(&list, 0u, shape, sizeof(shape));
        RoboCheckStrEq(shape, cases[index]);
        CssDeclarationListDispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void TestCssParseValueLifetime(void)
{
    static const char *text =
        "p { background: url(\"a.png\") no-repeat center / cover;"
        "    width: calc(100% - var(--x, 2px));"
        "    font-family: \"A\", \"B\", sans-serif; }";
    int weEnabled = CssAllocatorEnsureEnabled();
    size_t baseline = MemDebugLiveCount();
    int round;

    /* Every round builds and destroys a value tree with blocks, functions,
     * strings and urls in it. A leaked child array or a double-freed node shows
     * up here rather than in a production parse. */
    for (round = 0; round < 30; round++) {
        CssError error = RoboCssCleared;
        CssStylesheetId sheet = CssParseSheet(text, &error);
        const CssStyleRule *rule = CssSheetStyleRule(sheet, 0u);

        RoboCheckEqInt(CssStyleRuleDeclarationCount(rule), 3u);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    }

    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
}

/* ------------------------------------------------------------------ */

void TestRegisterCssParseValue(void);

void TestRegisterCssParseValue(void)
{
    TestRegister("css.parse.value.component_vocabulary",
                       TestCssParseValueComponentVocabulary);
    TestRegister("css.parse.value.tokens_keep_their_ranges",
                       TestCssParseValueTokensKeepTheirRanges);
    TestRegister("css.parse.value.functions", TestCssParseValueFunctions);
    TestRegister("css.parse.value.nested_functions",
                       TestCssParseValueNestedFunctions);
    TestRegister("css.parse.value.blocks", TestCssParseValueBlocks);
    TestRegister("css.parse.value.blocks_match_their_close",
                       TestCssParseValueBlocksRecordTheirMatchingClose);
    TestRegister("css.parse.value.nesting_bound",
                       TestCssParseValueNestingBound);
    TestRegister("css.parse.value.strings", TestCssParseValueStrings);
    TestRegister("css.parse.value.url_tokens", TestCssParseValueUrlTokens);
    TestRegister("css.parse.value.url_quoted_is_a_function",
                       TestCssParseValueUrlQuotedIsAFunction);
    TestRegister("css.parse.value.lifetime", TestCssParseValueLifetime);
}
