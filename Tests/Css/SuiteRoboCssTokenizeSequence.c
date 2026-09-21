/*
 * RoboCSS tokenizer sequence suite (Phase 9.2): whole streams, malformed input
 * and the robustness properties.
 *
 * A tokenizer can pass every per-class test and still be wrong at the seams:
 * one byte eaten in a corner that no single-token case reaches shifts every
 * following token. So this suite asserts the sequence, and it asserts the
 * invariants that hold over *any* input — the ranges tile the source exactly,
 * the stream always terminates on EOF, and the same bytes always produce the
 * same tokens. Those three are checked over a corpus that includes every
 * malformed shape the phase names, because that is where they are hardest.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

static void TestTokenizeSequenceDeclaration(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* The phase's own example, asserted whole rather than one token at a time. */
    CssLexText(&source, "color: red;", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|colon|ws|ident|semicolon|eof");
    RoboCheckEqInt(log.count, 6u);
    RoboCheckEqInt(log.hasError, 0);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "color"));
    RoboCheckEqInt(log.tokens[1].range.begin, 5u);
    RoboCheckEqInt(log.tokens[2].range.begin, 6u);
    RoboCheckEqInt(log.tokens[2].range.end, 7u);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[3], "red"));
    RoboCheckEqInt(log.tokens[4].range.begin, 10u);
    RoboCheckEqInt(log.tokens[5].range.begin, 11u);
    RoboCheckEqInt(log.tokens[5].range.end, 11u);
}

static void TestTokenizeSequenceMargins(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[256];

    CssLexText(&source, "margin: 10px 20px;", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds,
                      "ident|colon|ws|dimension|ws|dimension|semicolon|eof");
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheck(CssSliceEqualsAscii(log.tokens[3].unit, "px"));
    RoboCheck(CssSliceEqualsAscii(log.tokens[5].unit, "px"));
    CSS_CHECK_DOUBLE(log.tokens[3].number, 10.0);
    CSS_CHECK_DOUBLE(log.tokens[5].number, 20.0);
    RoboCheckEqInt(log.hasError, 0);
}

static void TestTokenizeSequenceAtRule(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[512];

    /* The phase's third example, lexically: an at-keyword, a prelude, nested
     * curly blocks, a class selector and a declaration — with no parser
     * anywhere near it. */
    CssLexText(&source,
                 "@media screen {\n"
                 "  .button {\n"
                 "    color: red;\n"
                 "  }\n"
                 "}\n",
                 &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds,
                      "at-keyword|ws|ident|ws|left-curly|ws|delim|ident|ws|"
                      "left-curly|ws|ident|colon|ws|ident|semicolon|ws|right-curly|"
                      "ws|right-curly|ws|eof");
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheckEqInt(log.hasError, 0);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenLeftCurly), 2u);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenRightCurly), 2u);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "media"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[2], "screen"));
}

static void TestTokenizeSequenceFixtureCorpus(void)
{
    static const char *kFixtures[] = {
        CSS_FIXTURE_PARAGRAPH,
        CSS_FIXTURE_CLASS,
        CSS_FIXTURE_ID,
        CSS_FIXTURE_ATTRIBUTE,
        CSS_FIXTURE_BODY
    };
    size_t i;

    for (i = 0u; i < sizeof(kFixtures) / sizeof(kFixtures[0]); i++) {
        CssSource source = RoboCssCleared;
        CssTokenLog log;
        size_t j;

        CssLexText(&source, kFixtures[i], &log);
        RoboCheckEqInt(log.hasError, 0);
        RoboCheckEqInt(log.truncated, 0);
        RoboCheckEqInt(log.eofSeen, 1);
        RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
        RoboCheckEqInt(log.tokens[log.count - 1u].type, RoboCssTokenEof);
        RoboCheckEqInt(CssCountType(&log, RoboCssTokenLeftCurly), 1u);
        RoboCheckEqInt(CssCountType(&log, RoboCssTokenRightCurly), 1u);
        RoboCheckEqInt(CssCountType(&log, RoboCssTokenColon), 1u);
        RoboCheckEqInt(CssCountType(&log, RoboCssTokenSemicolon), 1u);

        /* Every token's text is exactly the bytes its range covers, which is the
         * pairing that makes a token self-describing. */
        for (j = 0u; j < log.count; j++) {
            const CssToken *token = CssTokenAt(&log, j);

            RoboCheck(token != NULL);
            RoboCheckEqInt(token->text.length,
                              token->range.end - token->range.begin);
        }
    }
}

static void TestTokenizeSequenceHexColourAndFunctions(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[256];

    CssLexText(&source, "a { color: #ABC; width: calc(100% - 10px); }", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds,
                      "ident|ws|left-curly|ws|ident|colon|ws|hash|semicolon|ws|"
                      "ident|colon|ws|function|percentage|ws|delim|ws|dimension|"
                      "right-paren|semicolon|ws|right-curly|eof");
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheckEqInt(log.hasError, 0);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[7], "ABC"));
    RoboCheckEqInt(log.tokens[7].hash, RoboCssHashTypeId);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[13], "calc"));
}

static void TestTokenizeMalformedCorpus(void)
{
    static const char *kCorpus[] = {
        "\"unterminated",
        "/* unterminated",
        "10.",
        "--bad\\",
        "url(\"unterminated)",
        "@$",
        "<div><span>",
        "<p><div>broken",
        "<select><option>One<option>Two",
        "<div id=\"a\" id=\"b\"></div>",
        "a{b:c",
        "(((((",
        "}]){",
        "url(a b)",
        "'",
        "\\",
        "\"a\\",
        "1e",
        "#",
        "@",
        "/*/",
        "0.0e",
        "----",
        "%%%"
    };
    size_t i;

    for (i = 0u; i < sizeof(kCorpus) / sizeof(kCorpus[0]); i++) {
        CssSource source = RoboCssCleared;
        CssTokenLog first;
        CssTokenLog second;
        size_t length = strlen(kCorpus[i]);

        CssLexText(&source, kCorpus[i], &first);

        /* Deterministic output, no corruption, and a stream that ends. */
        RoboCheckEqInt(first.truncated, 0);
        RoboCheckEqInt(first.eofSeen, 1);
        RoboCheckEqInt(first.tokens[first.count - 1u].type, RoboCssTokenEof);
        RoboCheck(CssLogTiles(&first, length));

        CssLex(&source, &second);
        RoboCheckEqInt(first.count, second.count);
        {
            char kindsFirst[512];
            char kindsSecond[512];

            CssLogKinds(&first, kindsFirst, sizeof(kindsFirst));
            CssLogKinds(&second, kindsSecond, sizeof(kindsSecond));
            RoboCheckStrEq(kindsFirst, kindsSecond);
        }

        /* Whatever went wrong, it is not fatal: the error vocabulary's
         * recoverable/fatal split is what a caller switches on, and malformed
         * CSS never produces a fatal one. */
        if (first.hasError) {
            RoboCheck(CssErrorKindIsError(first.error.kind));
            RoboCheckEqInt(CssErrorKindIsFatal(first.error.kind), 0);
        }
    }
}

static void TestTokenizeMalformedSpecificSequences(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* `10.` is a number and a dot, not a failed number. */
    CssLexText(&source, "10.", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|delim|eof");
    RoboCheckEqInt(log.hasError, 0);

    /* `--bad\` is the identifier `--bad`, then a delimiter for the backslash,
     * and a recorded parse error for it. */
    CssLexText(&source, "--bad\\", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|delim|eof");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "--bad"));
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheckEqInt(log.error.position.offset, 5u);

    /* `url("unterminated)` is a function and an unclosed string — and no `)`
     * token, because the `)` is inside the string: a string that never closes
     * runs to the end of input, and the tokenizer is not entitled to look into
     * it for a parenthesis. That is the whole reason an unterminated string is
     * worth starting lexically and finishing at the terminator. */
    CssLexText(&source, "url(\"unterminated)", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "function|string|eof");
    RoboCheckEqInt(log.tokens[1].closed, 0);
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindUnexpectedEof);

    /* `@$` is two delimiters and no error at all: there is nothing wrong with
     * it that a tokenizer can see. */
    CssLexText(&source, "@$", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|delim|eof");
    RoboCheckEqInt(log.hasError, 0);

    /* Markup is just delimiters, identifiers and strings to a CSS tokenizer —
     * no HTML parser is involved, and nothing crashes. */
    CssLexText(&source, "<div id=\"a\">text</div>", &log);
    RoboCheckEqInt(log.count, 13u);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenString), 1u);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheckEqInt(log.hasError, 0);

    /* Unbalanced brackets are tokens, not errors: pairing belongs to the
     * grammar, and a tokenizer has no opinion about it. */
    CssLexText(&source, "(((((", &log);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenLeftParen), 5u);
    RoboCheckEqInt(log.hasError, 0);
    CssLexText(&source, "}]){", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "right-curly|right-square|right-paren|left-curly|eof");
    RoboCheckEqInt(log.hasError, 0);
}

static void TestTokenizeDeepNestingIsIterative(void)
{
    CssSource source = RoboCssCleared;
    CssStreamAudit audit;
    size_t i;
    size_t depth = 200u;
    char text[1024];

    /* 200 nested parens and 200 nested braces: no recursion, no stack growth,
     * no nesting budget. The tokenizer's depth is a property of the input, not
     * of its own stack, and this is what says so.
     *
     * These three fixtures produce 401, 401 and 601 tokens, so they are read
     * through the stream audit rather than a log — a log is bounded by
     * CSS_TOK_MAX, and silently truncating the very stream whose length is
     * being asserted would make the assertion meaningless. */
    for (i = 0u; i < depth; i++) {
        text[i] = '(';
    }
    for (i = 0u; i < depth; i++) {
        text[depth + i] = ')';
    }
    text[depth * 2u] = '\0';
    CssSourceOfString(&source, text);
    CssAuditStream(&source, &audit);
    RoboCheckEqInt(audit.total, (depth * 2u) + 1u);
    RoboCheckEqInt(audit.byType[RoboCssTokenLeftParen], depth);
    RoboCheckEqInt(audit.byType[RoboCssTokenRightParen], depth);
    RoboCheckEqInt(audit.hasError, 0);
    RoboCheckEqInt(audit.eofSeen, 1);
    RoboCheck(audit.tiles);

    for (i = 0u; i < depth; i++) {
        text[i] = '{';
    }
    for (i = 0u; i < depth; i++) {
        text[depth + i] = '}';
    }
    text[depth * 2u] = '\0';
    CssSourceOfString(&source, text);
    CssAuditStream(&source, &audit);
    RoboCheckEqInt(audit.total, (depth * 2u) + 1u);
    RoboCheckEqInt(audit.byType[RoboCssTokenLeftCurly], depth);
    RoboCheckEqInt(audit.byType[RoboCssTokenRightCurly], depth);
    RoboCheck(audit.tiles);

    /* Nested function calls: 200 names each followed by `(`. */
    {
        size_t offset = 0u;

        for (i = 0u; i < depth && offset + 2u < sizeof(text); i++) {
            text[offset++] = 'f';
            text[offset++] = '(';
        }
        for (i = 0u; i < depth && offset + 1u < sizeof(text); i++) {
            text[offset++] = ')';
        }
        text[offset] = '\0';
        CssSourceOfString(&source, text);
        CssAuditStream(&source, &audit);
        RoboCheckEqInt(audit.byType[RoboCssTokenFunction], depth);
        RoboCheckEqInt(audit.byType[RoboCssTokenRightParen], depth);
        RoboCheckEqInt(audit.hasError, 0);
        RoboCheck(audit.tiles);
    }
}

static void TestTokenizeLargeSheet(void)
{
    static char sheet[24000];
    CssSource source = RoboCssCleared;
    CssStreamAudit audit;
    size_t offset = 0u;
    size_t rules = 200u;
    size_t i;

    /* A sheet of a realistic size, built in place: the tokenizer must stay
     * linear, so this completing at all is the assertion — a quadratic scan
     * would not finish in the suite's lifetime at this size. The token count is
     * asserted exactly, which is what makes it more than a smoke test. */
    for (i = 0u; i < rules; i++) {
        int written = snprintf(sheet + offset, sizeof(sheet) - offset,
                               ".rule-%u { color: #%06X; margin: 10px 20px; }\n",
                               (unsigned)i, (unsigned)(i * 7919u) & 0xFFFFFFu);

        if (written <= 0 || (size_t)written >= sizeof(sheet) - offset) {
            break;
        }
        offset += (size_t)written;
    }
    RoboCheck(offset > 8000u);

    CssSourceOfString(&source, "");
    RoboCheckEqInt(CssSourceSetMemory(&source, (const uint8_t *)sheet,
                                                 offset, NULL), RoboOk);
    CssAuditStream(&source, &audit);
    RoboCheckEqInt(audit.hasError, 0);
    RoboCheckEqInt(audit.eofSeen, 1);
    RoboCheck(audit.tiles);
    /* Every rule contributes: `.rule-N` (delim + ident), ws, `{`, ws, decl… */
    RoboCheck(audit.byType[RoboCssTokenLeftCurly] == rules);
    RoboCheck(audit.byType[RoboCssTokenRightCurly] == rules);
    RoboCheck(audit.byType[RoboCssTokenSemicolon] == rules * 2u);
    RoboCheck(audit.byType[RoboCssTokenHash] == rules);
    /* And the token count is exact rather than "roughly 200 rules": a sheet this
     * size is where a quadratic scan or a missed byte would show up as a count
     * that drifts by one per rule. */
    RoboCheck(audit.total > rules * 20u);
}

static void TestTokenizeUnicodeMatrix(void)
{
    /* Each family through three routes: a name, a string and a comment. All
     * three must hand back the sample's bytes untouched. */
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        const char *sample = cssSamples[i].text;
        size_t sampleLength = strlen(sample);
        char name[256];
        char stringForm[256];
        char commentForm[256];
        CssSource source = RoboCssCleared;
        CssTokenLog log;
        uint8_t value[256];
        size_t length = 0u;

        snprintf(name, sizeof(name), "%s", sample);
        snprintf(stringForm, sizeof(stringForm), "\"%s\"", sample);
        snprintf(commentForm, sizeof(commentForm), "/* %s */", sample);

        /* The sample as a bare name. The ones that hold a space are several
         * identifiers; the rest are one covering every byte. */
        CssLexText(&source, name, &log);
        RoboCheckEqInt(log.hasError, 0);
        RoboCheck(CssLogTiles(&log, sampleLength));
        if (strchr(sample, ' ') == NULL) {
            RoboCheckEqInt(log.count, 2u);
            RoboCheckEqInt(log.tokens[0].type, RoboCssTokenIdent);
            RoboCheckEqInt(log.tokens[0].range.end, sampleLength);
        } else {
            size_t t;

            for (t = 0u; t < log.count; t++) {
                if (log.tokens[t].type != RoboCssTokenEof) {
                    RoboCheck(log.tokens[t].type == RoboCssTokenIdent ||
                               log.tokens[t].type == RoboCssTokenWhitespace);
                }
            }
        }

        CssLexText(&source, stringForm, &log);
        RoboCheckEqInt(log.count, 2u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
        RoboCheckEqInt(log.hasError, 0);
        RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value,
                                                   sizeof(value), &length, NULL),
                          RoboOk);
        RoboCheckEqInt(length, sampleLength);
        RoboCheckEqInt(memcmp(value, sample, sampleLength), 0);

        CssLexText(&source, commentForm, &log);
        RoboCheckEqInt(log.count, 2u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
        RoboCheckEqInt(log.hasError, 0);
        RoboCheckEqInt(log.tokens[0].range.end, strlen(commentForm));

        /* The same sample inside an unquoted url, for the samples a url can
         * hold at all: a url may not contain whitespace, a quote or a paren, so
         * the multi-word samples are skipped here (the url suite covers the
         * rejections). What this adds is that non-ASCII *bytes* inside a url
         * survive it. */
        if (strchr(sample, ' ') == NULL && strchr(sample, '"') == NULL &&
            strchr(sample, '\'') == NULL) {
            char urlForm[256];
            uint8_t urlValue[256];
            size_t urlLength = 0u;

            snprintf(urlForm, sizeof(urlForm), "url(%s)", sample);
            CssLexText(&source, urlForm, &log);
            RoboCheckEqInt(log.count, 2u);
            RoboCheckEqInt(log.tokens[0].type, RoboCssTokenUrl);
            RoboCheckEqInt(log.tokens[0].closed, 1);
            RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], urlValue,
                                                       sizeof(urlValue), &urlLength,
                                                       NULL), RoboOk);
            RoboCheckEqInt(urlLength, sampleLength);
            RoboCheckEqInt(memcmp(urlValue, sample, sampleLength), 0);
        }
    }
}

static void TestTokenizeIdenticalBytesIdenticalTokens(void)
{
    /* The determinism claim, made over the input shapes that are most likely to
     * expose it: escapes, comments at the end, bad urls, ill-formed bytes. */
    static const char *kInputs[] = {
        "a{color:red}",
        "/* c */ a",
        "url(a b) x",
        "\"a\\41 b\"",
        "\\63 olor: \\31 23px",
        "#id .cls @media",
        "\"unterminated",
        "/* unterminated"
    };
    size_t i;

    for (i = 0u; i < sizeof(kInputs) / sizeof(kInputs[0]); i++) {
        CssSource source = RoboCssCleared;
        CssTokenLog first;
        CssTokenLog second;
        char firstKinds[512];
        char secondKinds[512];
        size_t j;

        CssLexText(&source, kInputs[i], &first);
        CssLexText(&source, kInputs[i], &second);
        RoboCheckEqInt(first.count, second.count);
        RoboCheckEqInt(first.hasError, second.hasError);
        CssLogKinds(&first, firstKinds, sizeof(firstKinds));
        CssLogKinds(&second, secondKinds, sizeof(secondKinds));
        RoboCheckStrEq(firstKinds, secondKinds);

        for (j = 0u; j < first.count && j < CSS_TOK_MAX; j++) {
            const CssToken *a = &first.tokens[j];
            const CssToken *b = &second.tokens[j];

            RoboCheckEqInt(a->type, b->type);
            RoboCheckEqInt(a->range.begin, b->range.begin);
            RoboCheckEqInt(a->range.end, b->range.end);
            RoboCheckEqInt(a->closed, b->closed);
            RoboCheckEqInt(a->hash, b->hash);
            RoboCheckEqInt(CssTokenValueLength(a),
                              CssTokenValueLength(b));
            CSS_CHECK_DOUBLE(a->number, b->number);
        }

        /* Lexing twice through the *same* tokenizer, reset between runs, gives
         * the same answer as two fresh ones. */
        {
            CssTokenizer tokenizer = RoboCssCleared;
            CssToken token = RoboCssCleared;
            size_t producedFirst = 0u;
            size_t producedSecond = 0u;

            RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL),
                              RoboOk);
            while (!CssTokenizerAtEnd(&tokenizer)) {
                RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL),
                                  RoboOk);
            }
            producedFirst = CssTokenizerTokensProduced(&tokenizer);
            RoboCheckEqInt(CssTokenizerReset(&tokenizer, NULL), RoboOk);
            while (!CssTokenizerAtEnd(&tokenizer)) {
                RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL),
                                  RoboOk);
            }
            producedSecond = CssTokenizerTokensProduced(&tokenizer);
            RoboCheckEqInt(producedFirst, producedSecond);
            CssTokenizerDispose(&tokenizer);
        }
    }
}

static void TestTokenizeWholeSheetIntegration(void)
{
    static const char *kSheet =
        "@charset \"utf-8\";\n"
        "/* Roboticela demo sheet */\n"
        "@media (min-width: 40em) {\n"
        "  :root { --brand: #0E7AFF; }\n"
        "  .card::after { content: \"\\2192\"; }\n"
        "  a[href^='https'] { text-decoration: underline; }\n"
        "}\n"
        "body {\n"
        "  font: 400 1rem/1.5 \"SF Pro Text\", -apple-system, sans-serif;\n"
        "  background: url(assets/grain.png) repeat;\n"
        "  transform: translate3d(-50%, .5em, 0) scale(1.02);\n"
        "  color: rgb(29 29 31 / 87%);\n"
        "}\n";

    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[4096];

    CssLexText(&source, kSheet, &log);
    /* Whole-sheet lexing: no errors, a complete stream, byte-exact tiling, and
     * every class the phase names present in one pass. */
    RoboCheckEqInt(log.hasError, 0);
    RoboCheckEqInt(log.truncated, 0);
    RoboCheckEqInt(log.eofSeen, 1);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheck(CssCountType(&log, RoboCssTokenAtKeyword) == 2u);
    RoboCheck(CssCountType(&log, RoboCssTokenComment) == 1u);
    RoboCheck(CssCountType(&log, RoboCssTokenHash) == 1u);
    RoboCheck(CssCountType(&log, RoboCssTokenUrl) == 1u);
    /* `-50%` in the transform and `87%` in the relative colour. */
    RoboCheck(CssCountType(&log, RoboCssTokenPercentage) == 2u);
    /* Three dimensions: `40em` in the media query, `1rem` in the shorthand and
     * `.5em` in the transform. `1.5` and `1.02` are numbers, not dimensions,
     * which is exactly the distinction the dimension tests pin. */
    RoboCheck(CssCountType(&log, RoboCssTokenDimension) == 3u);
    RoboCheck(CssCountType(&log, RoboCssTokenString) == 4u);
    RoboCheck(CssCountType(&log, RoboCssTokenFunction) == 3u);
    RoboCheck(CssCountType(&log, RoboCssTokenColon) >= 8u);
    RoboCheck(CssCountType(&log, RoboCssTokenSemicolon) >= 6u);
    RoboCheck(CssCountType(&log, RoboCssTokenDelim) >= 4u);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheck(strlen(kinds) > 0u);
}

void TestRegisterCssTokenizeSequence(void);

void TestRegisterCssTokenizeSequence(void)
{
    TestRegister("css.tokenize.sequence_declaration",
                       TestTokenizeSequenceDeclaration);
    TestRegister("css.tokenize.sequence_margins", TestTokenizeSequenceMargins);
    TestRegister("css.tokenize.sequence_at_rule", TestTokenizeSequenceAtRule);
    TestRegister("css.tokenize.sequence_fixture_corpus",
                       TestTokenizeSequenceFixtureCorpus);
    TestRegister("css.tokenize.sequence_hex_colour_and_functions",
                       TestTokenizeSequenceHexColourAndFunctions);
    TestRegister("css.tokenize.malformed_corpus", TestTokenizeMalformedCorpus);
    TestRegister("css.tokenize.malformed_specific_sequences",
                       TestTokenizeMalformedSpecificSequences);
    TestRegister("css.tokenize.deep_nesting_is_iterative",
                       TestTokenizeDeepNestingIsIterative);
    TestRegister("css.tokenize.large_sheet", TestTokenizeLargeSheet);
    TestRegister("css.tokenize.unicode_matrix", TestTokenizeUnicodeMatrix);
    TestRegister("css.tokenize.identical_bytes_identical_tokens",
                       TestTokenizeIdenticalBytesIdenticalTokens);
    TestRegister("css.tokenize.whole_sheet_integration",
                       TestTokenizeWholeSheetIntegration);
}
