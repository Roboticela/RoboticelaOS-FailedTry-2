/*
 * RoboCSS tokenizer core suite (Phase 9.2): whitespace, comments, CDO/CDC,
 * punctuation, delimiters, EOF — and the two ends of the byte spectrum, NUL and
 * ill-formed UTF-8, where this tokenizer's no-preprocessing decision is visible.
 *
 * Every test here also asserts the token's *range*, because for these classes
 * the range is the interesting part: a whitespace run is one token covering many
 * bytes, a comment includes its delimiters, an ill-formed byte is exactly one
 * byte, and the EOF token is an empty range at the end.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

static void TestTokenizeWhitespaceRuns(void)
{
    static const char *kKinds = "ws|ident|ws|ident|ws|ident|eof";
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* Space, tab and form feed all run; a mixed run is one token. */
    CssLexText(&source, "   a\t\f b \t c", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, kKinds);
    RoboCheckEqInt(log.hasError, 0);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenWhitespace);
    RoboCheckEqInt(log.tokens[0].range.begin, 0u);
    RoboCheckEqInt(log.tokens[0].range.end, 3u);
    RoboCheckEqInt(log.tokens[1].type, RoboCssTokenIdent);
    /* A run is *greedy*: it swallows every whitespace code point up to the next
     * non-whitespace one, whatever the mixture, so the second run is tab, form
     * feed and space together (4..7) and the third is space, tab and space
     * (8..11). Splitting a run at a type boundary would break the tiling. */
    RoboCheckEqInt(log.tokens[2].range.begin, 4u);
    RoboCheckEqInt(log.tokens[2].range.end, 7u);
    RoboCheckEqInt(log.tokens[4].range.begin, 8u);
    RoboCheckEqInt(log.tokens[4].range.end, 11u);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* Whitespace is a token type, not a gap: it is how a caller that needs to
     * reproduce the source gets its spacing back. */
    RoboCheck(CssTokenTypeIsIgnorable(RoboCssTokenWhitespace));
    RoboCheck(CssTokenTypeIsIgnorable(RoboCssTokenComment));
    RoboCheck(!CssTokenTypeIsIgnorable(RoboCssTokenIdent));
}

static void TestTokenizeWhitespaceNewlines(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    CssPosition position;

    /* LF, CR, FF and CRLF are each one break, and a *CRLF run* is one token
     * covering both bytes — LFCR is two breaks, not a pair. */
    CssLexText(&source, "a\nb\rc\r\nd\fe", &log);
    RoboCheckEqInt(log.hasError, 0);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenWhitespace), 4u);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenIdent), 5u);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheckEqInt(log.tokens[2].range.begin, 2u);
    RoboCheckEqInt(log.tokens[2].range.end, 3u);
    RoboCheckEqInt(log.tokens[4].range.begin, 4u);
    RoboCheckEqInt(log.tokens[4].range.end, 5u);
    /* The pair is one run covering both bytes, which is the whole content of
     * the CRLF rule. */
    RoboCheckEqInt(log.tokens[5].type, RoboCssTokenWhitespace);
    RoboCheckEqInt(log.tokens[5].range.begin, 5u);
    RoboCheckEqInt(log.tokens[5].range.end, 7u);

    /* Line and column: CRLF is one break and the form feed is another, so the
     * last `e` is on line 5 — four breaks in ten bytes. The loop asks the token
     * rather than the cursor, because `at_end` answers "is there source left"
     * and is already true once the final byte has been consumed. */
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    do {
        RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    } while (token.type != RoboCssTokenEof);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    RoboCheckEqInt(CssTokenizerPosition(&tokenizer, &position, NULL), RoboOk);
    RoboCheckEqInt(position.offset, 10u);
    RoboCheckEqInt(position.line, 5u);
    RoboCheckEqInt(position.column, 2u);
    CssTokenizerDispose(&tokenizer);

    /* LFCR is not a CRLF pair. The run is still *one* whitespace token, because
     * a run is greedy regardless of which breaks it holds — what makes the pair
     * different is that it is two breaks where CRLF is one, so the line after it
     * is a line further down. */
    {
        CssSource lfcr = RoboCssCleared;
        CssTokenLog lfcrLog;

        CssLexText(&lfcr, "a\n\rb", &lfcrLog);
        RoboCheckEqInt(CssCountType(&lfcrLog, RoboCssTokenWhitespace), 1u);
        RoboCheckEqInt(CssCountType(&lfcrLog, RoboCssTokenIdent), 2u);
        RoboCheckEqInt(lfcrLog.tokens[1].range.begin, 1u);
        RoboCheckEqInt(lfcrLog.tokens[1].range.end, 3u);
        RoboCheckEqInt(CssSourcePosition(&lfcr, 3u, &position), RoboOk);
        RoboCheckEqInt(position.line, 3u);
        RoboCheckEqInt(position.column, 1u);
    }
}

static void TestTokenizeCommentForms(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    CssLexText(&source, "/* one */", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[0].closed, 1);
    RoboCheckEqInt(log.tokens[0].range.begin, 0u);
    RoboCheckEqInt(log.tokens[0].range.end, 9u);
    RoboCheckEqInt(log.hasError, 0);
    /* A comment's value is what it says: markers and all, never a name. */
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "/* one */"));

    CssLexText(&source, "/**/", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[0].closed, 1);
    RoboCheckEqInt(log.tokens[0].range.end, 4u);

    CssLexText(&source, "/* line\nline\nline */", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[0].closed, 1);
    /* One token across two newlines, and the newline is inside it. */
    RoboCheckEqInt(log.tokens[0].range.begin, 0u);
    RoboCheckEqInt(log.tokens[0].range.end, 20u);
    RoboCheckEqInt(log.hasError, 0);

    CssLexText(&source, "a/* x */b", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|comment|ident|eof");
    RoboCheckEqInt(log.tokens[1].range.begin, 1u);
    RoboCheckEqInt(log.tokens[1].range.end, 8u);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* An asterisk inside a comment does not end it, and a comment may be a
     * single pair of markers with nothing between them. */
    CssLexText(&source, "/* a * b ** c */ ident", &log);
    /* Comment, the space that followed it, the name, and EOF. */
    RoboCheckEqInt(log.count, 4u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[0].range.end, 16u);
    RoboCheckEqInt(log.tokens[1].type, RoboCssTokenWhitespace);
    RoboCheckEqInt(log.tokens[2].type, RoboCssTokenIdent);
    RoboCheckEqInt(log.hasError, 0);
}

static void TestTokenizeCommentUnicode(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    uint8_t value[64];
    size_t length = 0u;
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        char text[256];

        /* Marker, sample, marker: the sample's bytes must come back untouched. */
        snprintf(text, sizeof(text), "/* %s */", cssSamples[i].text);
        CssLexText(&source, text, &log);
        RoboCheckEqInt(log.hasError, 0);
        RoboCheckEqInt(log.count, 2u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
        RoboCheckEqInt(log.tokens[0].closed, 1);
        RoboCheck(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                             &length, NULL) == RoboOk);
        RoboCheckEqInt(length, strlen(text));
        RoboCheckEqInt(memcmp(value, text, length), 0);
    }
}

static void TestTokenizeCommentUnterminated(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    const CssError *error;

    /* An unterminated comment runs to the end of input and is still a comment:
     * the specification never makes it an error token. The stream records the
     * reason anyway, and `closed` is how the token itself says. */
    CssLexText(&source, "a /* never closed", &log);
    /* Name, space, the comment that runs to the end, and EOF: an unterminated
     * comment is still one token, not a token plus a remainder. */
    RoboCheckEqInt(log.count, 4u);
    RoboCheckEqInt(log.tokens[1].type, RoboCssTokenWhitespace);
    RoboCheckEqInt(log.tokens[2].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[2].closed, 0);
    RoboCheckEqInt(log.tokens[2].range.end, 17u);
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindUnexpectedEof);
    RoboCheckEqInt(log.error.position.offset, 2u);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    CssLexText(&source, "/*", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheckEqInt(log.tokens[0].range.end, 2u);

    /* An opening marker followed by an asterisk is not closed by that
     * asterisk: two bytes are needed, and there is only one. */
    CssLexText(&source, "/**", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheckEqInt(log.tokens[0].range.end, 3u);

    /* Marker, asterisk, slash: still open, because the asterisk was consumed
     * looking for a terminator before the slash arrived. */
    CssLexText(&source, "/*/", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenComment);
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheckEqInt(log.tokens[0].range.end, 3u);

    /* The recorded error does not stop the stream, and it is recoverable. */
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, &source, NULL), RoboOk);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenComment);
    RoboCheckEqInt(CssTokenizerHasError(&tokenizer), 1);
    error = CssTokenizerError(&tokenizer);
    RoboCheck(error != NULL);
    RoboCheckEqInt(CssErrorKindIsRecoverable(error->kind), 1);
    RoboCheckEqInt(CssErrorKindIsFatal(error->kind), 0);
    RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
    RoboCheckEqInt(token.type, RoboCssTokenEof);
    CssTokenizerDispose(&tokenizer);
}

static void TestTokenizeSlashIsNotAComment(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    CssLexText(&source, "a/b", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|delim|ident|eof");
    RoboCheckEqInt(log.hasError, 0);

    CssLexText(&source, "/", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|eof");
    RoboCheckEqInt(log.tokens[0].range.begin, 0u);
    RoboCheckEqInt(log.tokens[0].range.end, 1u);

    /* `/ *` with a space is two delimiters, not a comment. */
    CssLexText(&source, "/ *", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|ws|delim|eof");
}

static void TestTokenizeCdoCdc(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    CssLexText(&source, "<!--", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "cdo|eof");
    RoboCheckEqInt(log.tokens[0].range.end, 4u);

    CssLexText(&source, "-->", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "cdc|eof");
    RoboCheckEqInt(log.tokens[0].range.end, 3u);

    CssLexText(&source, "<!-- a -->", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "cdo|ws|ident|ws|cdc|eof");
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* The lookalikes: `--` is an identifier (a custom-property prefix), `->` is
     * two delimiters, and `<!-` is a delimiter followed by an identifier. */
    CssLexText(&source, "--", &log);
    RoboCheckStrEq(CssTokenTypeName(log.tokens[0].type), "ident");

    CssLexText(&source, "->", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|delim|eof");

    CssLexText(&source, "<!--", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenCdo);
    RoboCheckEqInt(log.tokens[0].text.length, 4u);
}

static void TestTokenizePunctuationTokens(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    CssLexText(&source, ":;,()[]{}", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds,
                      "colon|semicolon|comma|left-paren|right-paren|left-square|"
                      "right-square|left-curly|right-curly|eof");
    RoboCheckEqInt(log.hasError, 0);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheckEqInt(log.tokens[3].range.begin, 3u);
    RoboCheckEqInt(log.tokens[8].range.end, 9u);

    /* Every bracket type knows its partner, and only its partner. */
    {
        CssTokenType close = RoboCssTokenNone;

        RoboCheckEqInt(CssTokenTypeMatchingClose(RoboCssTokenLeftParen,
                                                             &close), 1);
        RoboCheckEqInt(close, RoboCssTokenRightParen);
        RoboCheckEqInt(CssTokenTypeMatchingClose(RoboCssTokenLeftSquare,
                                                             &close), 1);
        RoboCheckEqInt(close, RoboCssTokenRightSquare);
        RoboCheckEqInt(CssTokenTypeMatchingClose(RoboCssTokenLeftCurly,
                                                             &close), 1);
        RoboCheckEqInt(close, RoboCssTokenRightCurly);
        RoboCheckEqInt(CssTokenTypeMatchingClose(RoboCssTokenColon,
                                                             &close), 0);
    }
}

static void TestTokenizeDelimiterSet(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[256];
    size_t i;

    /* Each of these is one delimiter token on its own. */
    static const char kDelims[] = "!%&*=^?$|~`<>";
    for (i = 0u; kDelims[i] != '\0'; i++) {
        char text[2];

        text[0] = kDelims[i];
        text[1] = '\0';
        CssLexText(&source, text, &log);
        RoboCheckEqInt(log.count, 2u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDelim);
        RoboCheckEqInt(log.tokens[0].text.length, 1u);
        RoboCheckEqInt(log.tokens[0].text.bytes[0], (uint8_t)kDelims[i]);
    }

    /* The two-character match operators are *pairs* of delimiters in this
     * vocabulary: the 9.1 token record has no substring/prefix/suffix/dash/
     * include/column token, and inventing one here would be a second model.
     * 9.4 composes them, which is why this is pinned rather than left implicit. */
    CssLexText(&source, "*= ^= $= ~= |= || <<<", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds,
                      "delim|delim|ws|delim|delim|ws|delim|delim|ws|delim|delim|"
                      "ws|delim|delim|ws|delim|delim|ws|delim|delim|delim|eof");
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* No unicode-range token exists either, so `U+0025` lexes as an identifier,
     * a `+`-signed number and nothing else. Pinned so a later phase cannot
     * assume a token the model does not have. */
    CssLexText(&source, "U+0025", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|number|eof");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "U"));
    RoboCheckEqInt(log.tokens[1].hasSign, 1);
    CSS_CHECK_DOUBLE(log.tokens[1].number, 25.0);
}

static void TestTokenizeControlBytesAreDelimiters(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    size_t i;

    for (i = 1u; i < 0x20u; i++) {
        uint8_t text[3];

        if (i == 0x09u || i == 0x0Au || i == 0x0Cu || i == 0x0Du) {
            continue; /* whitespace, tested elsewhere */
        }
        text[0] = (uint8_t)i;
        text[1] = 'x';
        text[2] = '\0';
        CssLexText(&source, (const char *)text, &log);
        RoboCheckEqInt(log.count, 3u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDelim);
        RoboCheckEqInt(log.tokens[1].type, RoboCssTokenIdent);
    }

    {
        uint8_t del = 0x7Fu;
        char text[2];

        text[0] = (char)del;
        text[1] = '\0';
        CssLexText(&source, text, &log);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDelim);
    }
}

static void TestTokenizeNulIsAnIdentifierCodePoint(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    uint8_t value[8];
    size_t length = 0u;
    static const uint8_t kWithNul[] = {'a', 0x00u, 'b'};

    /* CSS's input preprocessing replaces U+0000 with U+FFFD, which starts an
     * identifier. We classify it the same way and keep the raw byte, so
     * `a<NUL>b` is one identifier of three bytes — not a name broken in two, and
     * not a value claiming a replacement character the caller never wrote. */
    CssSourceOfString(&source, "");
    RoboCheckEqInt(CssSourceSetMemory(&source, kWithNul, sizeof(kWithNul), NULL),
                      RoboOk);
    CssLex(&source, &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenIdent);
    RoboCheckEqInt(log.tokens[0].range.end, 3u);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 3u);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 3u);
    RoboCheckEqInt(memcmp(value, kWithNul, 3u), 0);
    RoboCheckEqInt(log.hasError, 0);
    RoboCheck(CssLogTiles(&log, sizeof(kWithNul)));

    /* A leading NUL begins an identifier rather than being a delimiter. */
    {
        static const uint8_t kLeading[] = {0x00u, 'x'};
        CssSource leading = RoboCssCleared;
        CssTokenLog leadingLog;

        CssSourceOfString(&leading, "");
        RoboCheckEqInt(CssSourceSetMemory(&leading, kLeading, sizeof(kLeading),
                                                     NULL), RoboOk);
        CssLex(&leading, &leadingLog);
        RoboCheckEqInt(leadingLog.count, 2u);
        RoboCheckEqInt(leadingLog.tokens[0].type, RoboCssTokenIdent);
        RoboCheckEqInt(leadingLog.tokens[0].range.end, 2u);
    }

    /* Inside a url, where U+FFFD is not allowed to be, a NUL is a non-printable
     * and makes the url bad rather than being silently carried as content. */
    {
        static const uint8_t kUrlNul[] = {'u', 'r', 'l', '(', 'a', 0x00u, 'b', ')'};
        CssSource urlSource = RoboCssCleared;
        CssTokenLog urlLog;

        CssSourceOfString(&urlSource, "");
        RoboCheckEqInt(CssSourceSetMemory(&urlSource, kUrlNul, sizeof(kUrlNul),
                                                    NULL), RoboOk);
        CssLex(&urlSource, &urlLog);
        RoboCheckEqInt(urlLog.tokens[0].type, RoboCssTokenBadUrl);
        RoboCheckEqInt(urlLog.hasError, 1);
        RoboCheck(CssLogTiles(&urlLog, sizeof(kUrlNul)));
    }
}

static void TestTokenizeInvalidUtf8(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* A lone continuation byte, a stray 0xFF, and a truncated three-byte
     * sequence are each one ill-formed position: one INVALID token, one byte,
     * one recorded error — never folded into a neighbouring name. */
    {
        static const uint8_t kLoneContinuation[] = {'a', 0x80u, 'b'};

        CssSourceOfString(&source, "");
        RoboCheckEqInt(CssSourceSetMemory(&source, kLoneContinuation,
                                                     sizeof(kLoneContinuation), NULL),
                          RoboOk);
        CssLex(&source, &log);
        CssLogKinds(&log, kinds, sizeof(kinds));
        RoboCheckStrEq(kinds, "ident|invalid|ident|eof");
        RoboCheckEqInt(log.tokens[1].range.begin, 1u);
        RoboCheckEqInt(log.tokens[1].range.end, 2u);
        RoboCheckEqInt(log.hasError, 1);
        RoboCheckEqInt(log.error.kind, RoboCssErrorKindInvalidToken);
        RoboCheckEqInt(log.error.position.offset, 1u);
        RoboCheck(CssLogTiles(&log, sizeof(kLoneContinuation)));
    }
    {
        static const uint8_t kStrayFF[] = {0xFFu, 0xFEu};

        CssSourceOfString(&source, "");
        RoboCheckEqInt(CssSourceSetMemory(&source, kStrayFF, sizeof(kStrayFF),
                                                     NULL), RoboOk);
        CssLex(&source, &log);
        RoboCheckEqInt(log.count, 3u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenInvalid);
        RoboCheckEqInt(log.tokens[1].type, RoboCssTokenInvalid);
        RoboCheckEqInt(log.tokens[0].range.end, 1u);
        RoboCheckEqInt(log.tokens[1].range.begin, 1u);
        RoboCheckEqInt(log.tokens[1].range.end, 2u);
    }
    {
        static const uint8_t kTruncated[] = {0xE4u, 0xB8u};

        CssSourceOfString(&source, "");
        RoboCheckEqInt(CssSourceSetMemory(&source, kTruncated,
                                                     sizeof(kTruncated), NULL), RoboOk);
        CssLex(&source, &log);
        RoboCheckEqInt(log.count, 3u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenInvalid);
        RoboCheckEqInt(log.tokens[1].type, RoboCssTokenInvalid);
    }

    /* A *surrogate encoded in UTF-8* is ill-formed UTF-8 and is reported as
     * such. Blink never sees this case because it tokenizes decoded UTF-16,
     * where the surrogate is substituted with U+FFFD instead. */
    {
        static const uint8_t kSurrogate[] = {0xEDu, 0xA0u, 0x80u};

        CssSourceOfString(&source, "");
        RoboCheckEqInt(CssSourceSetMemory(&source, kSurrogate,
                                                     sizeof(kSurrogate), NULL), RoboOk);
        CssLex(&source, &log);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenInvalid);
        RoboCheckEqInt(log.tokens[0].range.end, 1u);
        RoboCheckEqInt(log.hasError, 1);
    }

    /* A well-formed multi-byte scalar is a name, not an error. */
    {
        static const uint8_t kValid[] = {0xE4u, 0xB8u, 0xADu};

        CssSourceOfString(&source, "");
        RoboCheckEqInt(CssSourceSetMemory(&source, kValid, sizeof(kValid),
                                                     NULL), RoboOk);
        CssLex(&source, &log);
        RoboCheckEqInt(log.count, 2u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenIdent);
        RoboCheckEqInt(log.tokens[0].range.end, 3u);
        RoboCheckEqInt(log.hasError, 0);
    }
}

static void TestTokenizeSourceKindsAgree(void)
{
    static const char *kSheet = "a{color:red}";

    CssSource borrowed = RoboCssCleared;
    CssSource owned = RoboCssCleared;
    CssSource empty = RoboCssCleared;
    CssTokenLog borrowedLog;
    CssTokenLog ownedLog;
    CssTokenLog emptyLog;
    char borrowedKinds[128];
    char ownedKinds[128];

    CssSourceOfString(&borrowed, kSheet);
    RoboCheckEqInt(CssSourceCopy(&owned, CssSourceBytes(&borrowed),
                                           CssSourceLength(&borrowed), NULL),
                      RoboOk);
    CssSourceInit(&empty);
    RoboCheckEqInt(CssSourceSetEmpty(&empty, NULL), RoboOk);

    CssLex(&borrowed, &borrowedLog);
    CssLex(&owned, &ownedLog);
    CssLex(&empty, &emptyLog);
    CssLogKinds(&borrowedLog, borrowedKinds, sizeof(borrowedKinds));
    CssLogKinds(&ownedLog, ownedKinds, sizeof(ownedKinds));
    RoboCheckStrEq(borrowedKinds, ownedKinds);
    RoboCheckEqInt(borrowedLog.count, ownedLog.count);
    RoboCheck(CssLogTiles(&borrowedLog, strlen(kSheet)));
    RoboCheck(CssLogTiles(&ownedLog, strlen(kSheet)));

    /* EMPTY is a legal style sheet: one EOF, no error, nothing to read. */
    RoboCheckEqInt(emptyLog.count, 1u);
    RoboCheckEqInt(emptyLog.tokens[0].type, RoboCssTokenEof);
    RoboCheckEqInt(emptyLog.hasError, 0);

    CssSourceDispose(&owned);
}

void TestRegisterCssTokenizeCore(void);

void TestRegisterCssTokenizeCore(void)
{
    TestRegister("css.tokenize.whitespace_runs", TestTokenizeWhitespaceRuns);
    TestRegister("css.tokenize.whitespace_newlines",
                       TestTokenizeWhitespaceNewlines);
    TestRegister("css.tokenize.comment_forms", TestTokenizeCommentForms);
    TestRegister("css.tokenize.comment_unicode", TestTokenizeCommentUnicode);
    TestRegister("css.tokenize.comment_unterminated",
                       TestTokenizeCommentUnterminated);
    TestRegister("css.tokenize.slash_is_not_a_comment",
                       TestTokenizeSlashIsNotAComment);
    TestRegister("css.tokenize.cdo_cdc", TestTokenizeCdoCdc);
    TestRegister("css.tokenize.punctuation_tokens",
                       TestTokenizePunctuationTokens);
    TestRegister("css.tokenize.delimiter_set", TestTokenizeDelimiterSet);
    TestRegister("css.tokenize.control_bytes_are_delimiters",
                       TestTokenizeControlBytesAreDelimiters);
    TestRegister("css.tokenize.nul_is_identifier_code_point",
                       TestTokenizeNulIsAnIdentifierCodePoint);
    TestRegister("css.tokenize.invalid_utf8", TestTokenizeInvalidUtf8);
    TestRegister("css.tokenize.source_kinds_agree",
                       TestTokenizeSourceKindsAgree);
}
