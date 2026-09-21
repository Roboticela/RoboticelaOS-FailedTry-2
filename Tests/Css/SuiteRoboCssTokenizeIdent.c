/*
 * RoboCSS tokenizer name suite (Phase 9.2): identifiers, functions,
 * at-keywords, hashes and urls — the tokens whose *value* differs from their
 * bytes, and therefore the ones the escape machinery exists for.
 *
 * The distinction this suite is built around: `text` is always the raw source
 * slice (so it can never disagree with `range`), and the value accessors resolve
 * escapes and strip the delimiters the token's own type carries. Both are
 * asserted on the same token, side by side, because that pairing is the design.
 */

#include "RoboCssTestSupport.h"

static void TestTokenizeIdentSpellings(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    CssLexText(&source, "color", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|eof");
    RoboCheckEqInt(log.tokens[0].range.end, 5u);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "color"));
    RoboCheck(CssTokenValueEqualsAsciiCi(&log.tokens[0], "COLOR"));
    RoboCheck(!CssTokenValueEqualsAscii(&log.tokens[0], "COLOR"));
    /* `text` and the value agree when there is nothing to resolve. */
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "color"));
    RoboCheckEqInt(CssTokenHasEscapes(&log.tokens[0]), 0);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 5u);

    CssLexText(&source, "foo-bar _underscore --custom-property -webkit-box x1", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|ws|ident|ws|ident|ws|ident|ws|ident|eof");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "foo-bar"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[2], "_underscore"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[4], "--custom-property"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[6], "-webkit-box"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[8], "x1"));
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* A name is a name: the tokenizer does not know a property from a tag, and
     * `.class` is a delimiter followed by an identifier, not a token. */
    CssLexText(&source, "background button #title .class", &log);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenIdent), 3u);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenHash), 1u);
    RoboCheckEqInt(CssCountType(&log, RoboCssTokenDelim), 1u);
}

static void TestTokenizeIdentUnicode(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    uint8_t value[128];
    size_t length = 0u;
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        const char *sample = cssSamples[i].text;
        size_t sampleLength = strlen(sample);

        CssLexText(&source, sample, &log);
        RoboCheckEqInt(log.hasError, 0);
        RoboCheck(CssLogTiles(&log, sampleLength));

        if (strchr(sample, ' ') == NULL) {
            /* A word of non-ASCII characters is one identifier covering all of
             * it, and its value is the sample's bytes exactly: no shaping, no
             * normalisation, no replacement. */
            RoboCheckEqInt(log.count, 2u);
            RoboCheckEqInt(log.tokens[0].type, RoboCssTokenIdent);
            RoboCheckEqInt(log.tokens[0].range.end, sampleLength);
            RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), sampleLength);
            RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value,
                                                       sizeof(value), &length, NULL),
                              RoboOk);
            RoboCheckEqInt(length, sampleLength);
            RoboCheckEqInt(memcmp(value, sample, sampleLength), 0);
        } else {
            /* The multi-word samples are several identifiers around whitespace,
             * and putting the values back with the spaces the source had must
             * reproduce the sample byte for byte. That is a stronger claim than
             * the one-word case: it says no token in the run lost a byte and
             * none gained one. */
            size_t joined = 0u;
            size_t t;

            for (t = 0u; t < log.count; t++) {
                /* EOF is in the log too, and is neither a word nor a space. */
                if (log.tokens[t].type == RoboCssTokenWhitespace ||
                    log.tokens[t].type == RoboCssTokenEof) {
                    continue;
                }
                RoboCheckEqInt(log.tokens[t].type, RoboCssTokenIdent);
                if (joined > 0u) {
                    value[joined++] = (uint8_t)' ';
                }
                RoboCheckEqInt(CssTokenValueCopy(&log.tokens[t], value + joined,
                                                           sizeof(value) - joined,
                                                           &length, NULL), RoboOk);
                joined += length;
            }
            RoboCheckEqInt(joined, sampleLength);
            RoboCheckEqInt(memcmp(value, sample, sampleLength), 0);
        }
    }

    /* A name may mix scripts: non-ASCII code points are name characters, so an
     * Arabic word followed immediately by an accented Latin one is a single
     * identifier covering every byte of both. */
    CssLexText(&source,
                 "\xD8\xA7\xD9\x84\xD9\x84\xD9\x88\xD9\x86" "caf\xC3\xA9", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenIdent);
    RoboCheckEqInt(log.tokens[0].range.end, CssSourceLength(&source));
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]),
                      CssSourceLength(&source));
    RoboCheckEqInt(log.hasError, 0);
}

static void TestTokenizeIdentEscapes(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];
    uint8_t value[32];
    size_t length = 0u;

    /* `\63 olor` is the identifier `color`: six hex digits at most, then one
     * whitespace that belongs to the escape. */
    CssLexText(&source, "\\63 olor", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|eof");
    RoboCheckEqInt(log.tokens[0].range.end, 8u);
    RoboCheckEqInt(log.tokens[0].text.length, 8u);
    RoboCheckEqInt(CssTokenHasEscapes(&log.tokens[0]), 1);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 5u);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "color"));
    RoboCheck(CssTokenValueEqualsAsciiCi(&log.tokens[0], "COLOR"));
    /* Raw bytes still say what was written. */
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "\\63 olor"));
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 5u);
    RoboCheckEqInt(memcmp(value, "color", 5u), 0);

    /* The escape is legal anywhere in a name, including at the end and at the
     * start, and `\3` without a following hex digit is the single character. */
    CssLexText(&source, "col\\6fr", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "color"));

    CssLexText(&source, "\\41", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "A"));

    /* An escaped quote or space is an escaped character like any other. */
    CssLexText(&source, "a\\.b", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a.b"));

    CssLexText(&source, "a\\ b", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a b"));

    /* Out-of-range hex escapes are U+FFFD, which is three bytes and not ASCII. */
    CssLexText(&source, "\\0", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenIdent);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 3u);
    RoboCheck(!CssTokenValueEqualsAscii(&log.tokens[0], "x"));

    CssLexText(&source, "\\D800", &log);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 3u);

    CssLexText(&source, "\\110000", &log);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 3u);

    /* A multi-byte character escaped literally is passed through, not decoded
     * and re-encoded. */
    CssLexText(&source, "caf\\\xC3\xA9", &log);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 5u);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "caf\xC3\xA9"));
}

static void TestTokenizeEscapedNewlineIsNotAnEscape(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* A backslash before a newline is not an escape: the name ends before the
     * backslash, the backslash is a delimiter, and the newline is its own
     * whitespace token. The specification calls it a parse error, so the stream
     * records one — and the stream keeps going. */
    CssLexText(&source, "a\\\nb", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|delim|ws|ident|eof");
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheckEqInt(log.error.position.offset, 1u);

    /* A backslash at the end of input: a delimiter, and the same note. */
    CssLexText(&source, "a\\", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|delim|eof");
    RoboCheckEqInt(log.hasError, 1);

    /* An escaped CR is a newline too, and swallows nothing. */
    CssLexText(&source, "a\\\r\nb", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|delim|ws|ident|eof");
}

static void TestTokenizeFunctionTokens(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];
    uint8_t value[16];
    size_t length = 0u;

    CssLexText(&source, "rgb(", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "function|eof");
    /* The opening paren is part of the token and not of its value. */
    RoboCheckEqInt(log.tokens[0].range.end, 4u);
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "rgb("));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "rgb"));
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 3u);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 3u);
    RoboCheckEqInt(memcmp(value, "rgb", 3u), 0);

    CssLexText(&source, "calc( var( translate( scale3d(", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "function|ws|function|ws|function|ws|function|eof");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "calc"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[2], "var"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[6], "scale3d"));
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* Whitespace between the name and the paren makes them two tokens: only an
     * immediately following `(` makes a function. */
    CssLexText(&source, "foo (x)", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|ws|left-paren|ident|right-paren|eof");

    /* An escaped function name is a function too, and compares resolved. */
    CssLexText(&source, "r\\67 b(", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenFunction);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "rgb"));

    /* A function token is not a block start in this vocabulary: the record has
     * no pairing for it, which is 9.3's business, not the tokenizer's. */
    RoboCheckEqInt(CssTokenTypeIsBlockStart(RoboCssTokenFunction), 0);
    RoboCheckEqInt(CssTokenTypeIsBlockStart(RoboCssTokenLeftParen), 1);
}

static void TestTokenizeAtKeywords(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    CssLexText(&source, "@media @font-face @supports @import", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "at-keyword|ws|at-keyword|ws|at-keyword|ws|at-keyword|eof");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "media"));
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "@media"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[2], "font-face"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[4], "supports"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[6], "import"));
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 5u);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* `@media` and `media` are different tokens, which is the whole point. */
    CssLexText(&source, "media", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenIdent);
    CssLexText(&source, "@media", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenAtKeyword);

    /* A `@` that starts no name is a delimiter. */
    CssLexText(&source, "@", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDelim);
    CssLexText(&source, "@ 1", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDelim);
    CssLexText(&source, "@$", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|delim|eof");
    RoboCheckEqInt(log.hasError, 0);

    /* `@-x` and `@--x` are at-keywords: `-` starts an identifier here too. */
    CssLexText(&source, "@-x", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenAtKeyword);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "-x"));
    CssLexText(&source, "@--x", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenAtKeyword);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "--x"));

    /* An escaped at-keyword name resolves. */
    CssLexText(&source, "@\\6d edia", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenAtKeyword);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "media"));
}

static void TestTokenizeHashTokens(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* An identifier-like hash: usable as an id selector. */
    CssLexText(&source, "#header", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "hash|eof");
    RoboCheckEqInt(log.tokens[0].hash, RoboCssHashTypeId);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "header"));
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "#header"));
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 6u);

    /* A digit-led hash is unrestricted: a colour, never an id selector. */
    CssLexText(&source, "#123", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenHash);
    RoboCheckEqInt(log.tokens[0].hash, RoboCssHashTypeUnrestricted);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "123"));

    /* `-` counts as identifier-like, which is what makes `#-x` an id. */
    CssLexText(&source, "#-x", &log);
    RoboCheckEqInt(log.tokens[0].hash, RoboCssHashTypeId);
    CssLexText(&source, "#--x", &log);
    RoboCheckEqInt(log.tokens[0].hash, RoboCssHashTypeId);
    CssLexText(&source, "#-", &log);
    RoboCheckEqInt(log.tokens[0].hash, RoboCssHashTypeUnrestricted);

    /* An escaped name is identifier-like, so it is an id hash. */
    CssLexText(&source, "#\\31 23x", &log);
    RoboCheckEqInt(log.tokens[0].hash, RoboCssHashTypeId);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "123x"));

    /* A `#` that starts nothing is a delimiter. */
    CssLexText(&source, "#", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|eof");
    CssLexText(&source, "# x", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|ws|ident|eof");
    CssLexText(&source, "a#b", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "ident|hash|eof");
}

static void TestTokenizeUrlUnquoted(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];
    uint8_t value[32];
    size_t length = 0u;

    CssLexText(&source, "url(image.png)", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "url|eof");
    RoboCheckEqInt(log.tokens[0].closed, 1);
    /* The range covers `url(...)` whole, closing paren included, which is what
     * makes the token's text identical to the bytes the caller wrote. */
    RoboCheckEqInt(log.tokens[0].range.end, 14u);
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "url(image.png)"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "image.png"));
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 9u);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 9u);
    RoboCheckEqInt(memcmp(value, "image.png", 9u), 0);
    RoboCheckEqInt(log.hasError, 0);

    /* The case-insensitivity is CSS's, and it applies to `url` too. */
    CssLexText(&source, "URL(a)", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenUrl);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a"));

    CssLexText(&source, "\\75 rl(a)", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenUrl);

    /* An empty url is a url with an empty value. */
    CssLexText(&source, "url()", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenUrl);
    RoboCheckEqInt(log.tokens[0].closed, 1);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 0u);
    RoboCheckEqInt(log.hasError, 0);

    /* Leading and trailing whitespace is inside the token's range and outside
     * its value, which is what the ranges-tile-the-source invariant needs. */
    CssLexText(&source, "url(   foo.png   )", &log);
    RoboCheckStrEq(CssTokenTypeName(log.tokens[0].type), "url");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "foo.png"));
    RoboCheckEqInt(log.tokens[0].range.end, 18u);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* An escaped space is content, not a separator. */
    CssLexText(&source, "url(a\\ b.png)", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenUrl);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a b.png"));

    /* End of input closes nothing but is still a url: the specification says so
     * explicitly, and `closed` is where that fact lives. */
    CssLexText(&source, "url(foo", &log);
    RoboCheckStrEq(CssTokenTypeName(log.tokens[0].type), "url");
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "foo"));
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindUnexpectedEof);

    CssLexText(&source, "url(foo ", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenUrl);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "foo"));
}

static void TestTokenizeUrlQuotedIsAFunction(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* `url("x.png")` is a function token, a string and a `)` — the grammar's
     * two-argument form, which is how the specification distinguishes it. There
     * is no url token for the quoted spelling at all. */
    CssLexText(&source, "url(\"x.png\")", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "function|string|right-paren|eof");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "url"));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[1], "x.png"));
    RoboCheckEqInt(log.hasError, 0);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    CssLexText(&source, "url('x.png')", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "function|string|right-paren|eof");

    /* The documented divergence from the specification's literal text: the
     * whitespace before a quoted argument stays in the stream as its own token,
     * so that every byte is covered by exactly one token. Blink collapses it;
     * the specification keeps it; we keep it and say so here. */
    CssLexText(&source, "url( \"x.png\")", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "function|ws|string|right-paren|eof");
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* A *long* run of whitespace is still a function: only the first
     * non-whitespace character decides. */
    CssLexText(&source, "url(\t  'x')", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "function|ws|string|right-paren|eof");
}

static void TestTokenizeUrlBad(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* An illegal character in an unquoted url makes a bad-url, and its remnants
     * are consumed so the rest of the sheet does not become garbage. */
    CssLexText(&source, "url(a b) c", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "bad-url|ws|ident|eof");
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* The delimiter characters that end an unquoted url early. */
    CssLexText(&source, "url(a\"b)", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadUrl);
    CssLexText(&source, "url(a'b)", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadUrl);
    CssLexText(&source, "url(a(b)", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadUrl);
    CssLexText(&source, "url(a\\", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadUrl);

    /* A bad url still ends at its `)` or at the end of input, so the token
     * after it is the token after it. */
    CssLexText(&source, "url(a b)url(c)", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "bad-url|url|eof");
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[1], "c"));

    /* Whitespace is only legal before the closing paren. `url(a b` runs off the
     * end: the specification's remnant loop stops at end of input, so this is a
     * bad url and not a url. */
    CssLexText(&source, "url(a b", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadUrl);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
}

static void TestTokenizeValueCopyCapacity(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    uint8_t small[2];
    uint8_t exact[5];
    size_t length = 0u;

    CssLexText(&source, "color", &log);
    /* Too small: told so, and told what was needed, without writing anything. */
    small[0] = 0xAAu;
    small[1] = 0xAAu;
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], small, sizeof(small),
                                               &length, NULL), RoboInvalidArgument);
    RoboCheckEqInt(length, 5u);
    RoboCheckEqInt(small[0], 0xAAu);
    RoboCheckEqInt(small[1], 0xAAu);

    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], exact, sizeof(exact),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 5u);
    RoboCheckEqInt(memcmp(exact, "color", 5u), 0);

    /* Exactly the value's length fits: the copy is not NUL-terminated and does
     * not need a byte it was not given. */
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 5u);

    /* A NULL token, output or length is a caller mistake, and the length
     * output is written before any capacity check. */
    RoboCheckEqInt(CssTokenValueCopy(NULL, exact, sizeof(exact), &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], NULL, sizeof(exact),
                                               &length, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], exact, sizeof(exact),
                                               NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssTokenValueLength(NULL), 0u);
    RoboCheckEqInt(CssTokenValueEqualsAscii(NULL, "x"), 0);
    RoboCheckEqInt(CssTokenValueEqualsAsciiCi(NULL, "x"), 0);
    RoboCheckEqInt(CssTokenValueEqualsAscii(&log.tokens[0], NULL), 0);
    RoboCheckEqInt(CssTokenHasEscapes(NULL), 0);
}

static void TestTokenizeValueForEveryType(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;

    /* The value rule for each token type, in one place: the delimiters a type
     * carries are stripped and what is left is the value. This is the table in
     * RoboCSSToken.h asserted rather than described. */
    CssLexText(&source, "a", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a"));

    CssLexText(&source, "@a", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a"));

    CssLexText(&source, "#a", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a"));

    CssLexText(&source, "f(", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "f"));

    CssLexText(&source, "\"a\"", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a"));

    CssLexText(&source, "'a'", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a"));

    CssLexText(&source, "url(a)", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a"));

    /* A dimension's value is its numeric part; the unit is a separate slice. */
    CssLexText(&source, "10px", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "10"));
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "10px"));

    /* A number's value is its own text. */
    CssLexText(&source, "-1.5e2", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "-1.5e2"));

    /* A delimiter's value is the delimiter; a comment's is the whole comment —
     * a comment is the one token whose value is deliberately not stripped. */
    CssLexText(&source, "+", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "+"));

    CssLexText(&source, "/* c */", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "/* c */"));

    /* EOF has no text at all, so its value is absent rather than the empty
     * string: `text` being absent is the token's own distinction. */
    CssLexText(&source, "", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenEof);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 0u);
    RoboCheck(CssSliceIsAbsent(CssTokenText(&log.tokens[0])));
}

void TestRegisterCssTokenizeIdent(void);

void TestRegisterCssTokenizeIdent(void)
{
    TestRegister("css.tokenize.ident_spellings", TestTokenizeIdentSpellings);
    TestRegister("css.tokenize.ident_unicode", TestTokenizeIdentUnicode);
    TestRegister("css.tokenize.ident_escapes", TestTokenizeIdentEscapes);
    TestRegister("css.tokenize.escaped_newline_is_not_an_escape",
                       TestTokenizeEscapedNewlineIsNotAnEscape);
    TestRegister("css.tokenize.function_tokens", TestTokenizeFunctionTokens);
    TestRegister("css.tokenize.at_keywords", TestTokenizeAtKeywords);
    TestRegister("css.tokenize.hash_tokens", TestTokenizeHashTokens);
    TestRegister("css.tokenize.url_unquoted", TestTokenizeUrlUnquoted);
    TestRegister("css.tokenize.url_quoted_is_a_function",
                       TestTokenizeUrlQuotedIsAFunction);
    TestRegister("css.tokenize.url_bad", TestTokenizeUrlBad);
    TestRegister("css.tokenize.value_copy_capacity",
                       TestTokenizeValueCopyCapacity);
    TestRegister("css.tokenize.value_for_every_type",
                       TestTokenizeValueForEveryType);
}
