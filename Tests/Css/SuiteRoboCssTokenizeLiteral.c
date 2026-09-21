/*
 * RoboCSS tokenizer literal suite (Phase 9.2): strings and the numeric family.
 *
 * Two things get asserted here that a token-class test cannot show. First, that
 * the numeric conversion is *right* — the table of values is compared against
 * the host compiler's own conversion of the same literal, which is a genuinely
 * independent oracle for a decimal-to-double routine. Second, that the ends of
 * the numeric range are defined rather than accidental: overflow saturates to
 * infinity and underflow to zero, because CSS's conversion step leaves them
 * there and a tokenizer that refused them would be inventing a policy.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

static void TestTokenizeStringForms(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];
    uint8_t value[32];
    size_t length = 0u;

    CssLexText(&source, "\"hello\"", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "string|eof");
    RoboCheckEqInt(log.tokens[0].closed, 1);
    /* The token's text includes its quotes; its value excludes them. */
    RoboCheck(CssTokenEqualsAscii(&log.tokens[0], "\"hello\""));
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "hello"));
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 5u);
    RoboCheckEqInt(log.tokens[0].range.end, 7u);

    CssLexText(&source, "'hello'", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "hello"));

    /* The two quote characters do not close each other. */
    CssLexText(&source, "\"it's\"", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "it's"));
    CssLexText(&source, "'say \"hi\"'", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "say \"hi\""));

    /* An empty string is closed and empty, which is not the same as a string
     * that was never closed. */
    CssLexText(&source, "\"\"", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheckEqInt(log.tokens[0].closed, 1);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 0u);
    RoboCheckEqInt(log.hasError, 0);

    /* Whitespace inside a string is content. */
    CssLexText(&source, "\"  a  b  \"", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "  a  b  "));
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 8u);
    RoboCheckEqInt(memcmp(value, "  a  b  ", 8u), 0);
}

static void TestTokenizeStringEscapes(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    uint8_t value[32];
    size_t length = 0u;

    /* An escaped quote does not close the string. */
    CssLexText(&source, "\"a\\\"b\"", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a\"b"));

    CssLexText(&source, "'a\\'b'", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a'b"));

    /* A backslash is itself when escaped, and an ordinary character otherwise
     * — but \"otherwise\" excludes the hex digits, because a hex escape wins over
     * a literal one. `\q` is `q`; `\b` is U+000B, which is a trap worth pinning
     * precisely because `b` looks like a letter and is not one to CSS. */
    CssLexText(&source, "\"a\\\\b\"", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "a\\b"));

    CssLexText(&source, "\"a\\qb\"", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "aqb"));

    CssLexText(&source, "\"a\\b\"", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 2u);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 2u);
    RoboCheckEqInt(value[0], (uint8_t)'a');
    RoboCheckEqInt(value[1], 0x0Bu);

    /* Hex escapes: one to six digits, then one swallowed whitespace. */
    CssLexText(&source, "\"\\41\"", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "A"));
    CssLexText(&source, "\"\\000041\"", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "A"));
    CssLexText(&source, "\"\\41 b\"", &log);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "Ab"));

    /* A hex escape can spell a newline without ending the string: escapes are
     * never re-scanned as syntax. */
    CssLexText(&source, "\"a\\A b\"", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 3u);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 3u);
    RoboCheckEqInt(value[1], (uint8_t)'\n');

    /* Out-of-range escapes are U+FFFD, three bytes. */
    CssLexText(&source, "\"\\110000\"", &log);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 3u);

    /* A backslash before a newline swallows the newline and the string goes on. */
    CssLexText(&source, "\"a\\\nb\"", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheckEqInt(log.tokens[0].closed, 1);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "ab"));
    RoboCheckEqInt(log.hasError, 0);
    /* Six bytes: the two quotes, `a`, the backslash, the break and `b`. The
     * break is inside the token because the token's range is the source. */
    RoboCheckEqInt(log.tokens[0].range.end, 6u);

    /* A backslash at the end of input is dropped and the string is unclosed. */
    CssLexText(&source, "\"ab\\", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "ab"));
}

static void TestTokenizeStringUnicode(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    uint8_t value[128];
    size_t length = 0u;
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        char text[256];
        size_t sampleLength = strlen(cssSamples[i].text);

        snprintf(text, sizeof(text), "\"%s\"", cssSamples[i].text);
        CssLexText(&source, text, &log);
        RoboCheckEqInt(log.hasError, 0);
        RoboCheckEqInt(log.count, 2u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
        RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), sampleLength);
        RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                                   &length, NULL), RoboOk);
        RoboCheckEqInt(length, sampleLength);
        RoboCheckEqInt(memcmp(value, cssSamples[i].text, sampleLength), 0);
    }
}

static void TestTokenizeStringBadByNewline(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* A newline ends a string in front of itself. The newline is *not* consumed,
     * so it becomes the next token — which is the one place the specification
     * re-consumes an input code point, and the reason the whitespace token is
     * still there afterwards. */
    CssLexText(&source, "\"a\nb\"", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "bad-string|ws|ident|string|eof");
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheckEqInt(log.tokens[0].range.begin, 0u);
    RoboCheckEqInt(log.tokens[0].range.end, 2u);
    RoboCheckEqInt(log.tokens[1].range.begin, 2u);
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindRecoverableSyntax);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));

    /* A carriage return and a form feed are newlines for this purpose too. */
    CssLexText(&source, "\"a\rb", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadString);
    CssLexText(&source, "\"a\fb", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadString);

    /* A CRLF pair is one newline: one bad string, one whitespace token covering
     * both bytes. */
    CssLexText(&source, "\"a\r\nb\"", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "bad-string|ws|ident|string|eof");
    RoboCheckEqInt(log.tokens[1].range.begin, 2u);
    RoboCheckEqInt(log.tokens[1].range.end, 4u);

    /* A bad string's value is what it had before the newline. */
    CssLexText(&source, "\"abc\n", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenBadString);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "abc"));
}

static void TestTokenizeStringUnterminated(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* Running off the end of input leaves a string that was never closed — a
     * string, not a bad one, which is what `closed` is for. */
    CssLexText(&source, "\"abc", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "string|eof");
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheck(CssTokenValueEqualsAscii(&log.tokens[0], "abc"));
    RoboCheckEqInt(log.hasError, 1);
    RoboCheckEqInt(log.error.kind, RoboCssErrorKindUnexpectedEof);
    RoboCheckEqInt(log.error.position.offset, 0u);

    /* A lone quote is an empty, unclosed string. */
    CssLexText(&source, "\"", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheckEqInt(log.tokens[0].closed, 0);
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 0u);

    CssLexText(&source, "'", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheckEqInt(log.tokens[0].closed, 0);

    /* An unclosed string is not a fatal problem: the run of ordinary text after
     * it is still tokenized, and the stream still ends at EOF. */
    CssLexText(&source, "\"abc def ghi", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenString);
    RoboCheck(CssLogTiles(&log, CssSourceLength(&source)));
}

static void TestTokenizeNumberForms(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;

    CssLexText(&source, "0", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenNumber);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
    RoboCheckEqInt(log.tokens[0].isInteger, 1);
    RoboCheckEqInt(log.tokens[0].hasSign, 0);
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].repr, "0"));

    CssLexText(&source, "10", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    RoboCheckEqInt(log.tokens[0].isInteger, 1);

    CssLexText(&source, "-10", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, -10.0);
    RoboCheckEqInt(log.tokens[0].hasSign, 1);
    RoboCheckEqInt(log.tokens[0].isInteger, 1);

    CssLexText(&source, "+10", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    RoboCheckEqInt(log.tokens[0].hasSign, 1);

    CssLexText(&source, "3.14", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 3.14);
    RoboCheckEqInt(log.tokens[0].isInteger, 0);
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].repr, "3.14"));

    CssLexText(&source, ".5", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.5);
    RoboCheckEqInt(log.tokens[0].isInteger, 0);

    CssLexText(&source, "-0.25", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, -0.25);

    CssLexText(&source, "1e3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1000.0);
    /* An exponent makes it a number, not an integer, even when the value is
     * whole — which is what Blink's value type says too. */
    RoboCheckEqInt(log.tokens[0].isInteger, 0);

    CssLexText(&source, "1E3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1000.0);

    CssLexText(&source, "1e+3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1000.0);
    RoboCheckEqInt(log.tokens[0].hasSign, 0);

    CssLexText(&source, "1e-3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.001);

    CssLexText(&source, "12.5e2", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1250.0);

    CssLexText(&source, "007", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 7.0);
    RoboCheckEqInt(log.tokens[0].isInteger, 1);

    CssLexText(&source, "0.0", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
}

static void TestTokenizeNumberMatchesCompiler(void)
{
    /*
     * The independent oracle: the host compiler converts each of these literals
     * to a double correctly rounded, and this table makes the tokenizer's own
     * decimal conversion answer to it. A table rather than one value because a
     * conversion routine can be right about `10` and wrong about `0.1`.
     */
    static const struct {
        const char *text;
        double expected;
    } kCases[] = {
        {"0", 0.0},
        {"1", 1.0},
        {"10", 10.0},
        {"100", 100.0},
        {"0.1", 0.1},
        {"0.5", 0.5},
        {"1.5", 1.5},
        {"2.5", 2.5},
        {"3.14", 3.14},
        {"0.001", 0.001},
        {"1e3", 1000.0},
        {"1e-3", 0.001},
        {"1e22", 1e22},
        {"1e23", 1e23},
        {"1e-22", 1e-22},
        {"1.25e-7", 1.25e-7},
        {"6.02214076e23", 6.02214076e23},
        {"0.123456789012345", 0.123456789012345},
        {"123456789012345678", 123456789012345678.0},
        {"-0.0009765625", -0.0009765625}
        /* Two cases are deliberately not here: a decimal exponent beyond \u00b122
         * has no exact power of ten left to apply in one step, so those are
         * tested by the bound the conversion promises instead. See
         * TestTokenizeNumberBeyondExactScale. */
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        CssSource source = RoboCssCleared;
        CssTokenLog log;

        CssLexText(&source, kCases[i].text, &log);
        RoboCheckEqInt(log.count, 2u);
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenNumber);
        RoboCheckEqInt(log.tokens[0].range.end, strlen(kCases[i].text));
        CSS_CHECK_DOUBLE(log.tokens[0].number, kCases[i].expected);
    }
}

static void TestTokenizeNumberBeyondExactScale(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;

    /* Beyond a decimal exponent of \u00b122 the conversion has no exact power of ten
     * left to apply in a single step, so the scaling compounds one rounding per
     * step. The header states that as a bound rather than an equality, and these
     * are the two ends of the double range, where "a handful of digits" is not
     * what was written. A conversion that dropped digits, clamped the exponent
     * or saturated early would miss by very much more than a part in 10^15.
     *
     * 1e-23 is the smallest magnitude the exact table cannot reach and
     * DBL_MAX is the largest double there is, so between them they cover the
     * whole of the range the guarantee does not extend to. */
    CssLexText(&source, "1e-23", &log);
    RoboCheckEqInt(log.count, 2u);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenNumber);
    RoboCheck(log.tokens[0].number > 0.0);
    CSS_CHECK_DOUBLE_NEAR(log.tokens[0].number, 1e-23, 1e-23 * 1e-15);

    CssLexText(&source, "1.7976931348623157e308", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenNumber);
    RoboCheckEqInt(log.hasError, 0);
    CSS_CHECK_DOUBLE_NEAR(log.tokens[0].number, 1.7976931348623157e308,
                          1.7976931348623157e308 * 1e-15);
}

static void TestTokenizeNumberRangeEnds(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;

    /* Overflow saturates to infinity and underflow to zero: CSS's conversion
     * step leaves them there, and a tokenizer that refused them would be
     * inventing a policy of its own. */
    CssLexText(&source, "1e400", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenNumber);
    RoboCheck(log.tokens[0].number > 1.0e308);
    RoboCheckEqInt(log.hasError, 0);

    CssLexText(&source, "1e-400", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
    RoboCheckEqInt(log.tokens[0].hasSign, 0);
    RoboCheckEqInt(log.hasError, 0);

    CssLexText(&source, "-1e-400", &log);
    RoboCheck(log.tokens[0].number == 0.0);
    RoboCheckEqInt(log.tokens[0].hasSign, 1);

    /* An exponent of a billion is clamped rather than wrapped. */
    CssLexText(&source, "1e999999999999", &log);
    RoboCheck(log.tokens[0].number > 1.0e308);
    CssLexText(&source, "1e-999999999999", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);

    /* `-0` keeps its sign and is still zero. */
    CssLexText(&source, "-0", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
    RoboCheckEqInt(log.tokens[0].hasSign, 1);
    RoboCheckEqInt(log.tokens[0].isInteger, 1);

    /* Long digit strings keep the leading nineteen digits and drop the rest, so
     * the value is a truncation and stays inside the true value's decade. The
     * policy is documented in the tokenizer; this pins it. */
    CssLexText(&source, "12345678901234567890123", &log);
    RoboCheck(log.tokens[0].number > 1.234e22);
    RoboCheck(log.tokens[0].number < 1.235e22);
    CssLexText(&source, "0.0000000000000000000000001", &log);
    RoboCheck(log.tokens[0].number > 0.0);
    RoboCheck(log.tokens[0].number < 1.0e-24);
}

static void TestTokenizeNumberBoundaries(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    /* `10.` is a number followed by a delimiter: a `.` only continues a number
     * when a digit follows it. */
    CssLexText(&source, "10.", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|delim|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    RoboCheckEqInt(log.tokens[0].range.end, 2u);
    RoboCheckEqInt(log.tokens[1].range.begin, 2u);

    /* `1e` is a *dimension*, not a number followed by a name. The `e` cannot
     * continue the number — no digits follow it — so the numeric part stops at
     * `1`, and the unit rule then runs: `e` would start an identifier, so it is
     * the unit. Which is exactly what the grammar says, and worth pinning,
     * because the intuitive reading (an identifier follows the number) produces
     * two tokens and the correct one produces one. */
    CssLexText(&source, "1e", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "dimension|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1.0);
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].unit, "e"));
    RoboCheckEqInt(log.tokens[0].isInteger, 1);

    CssLexText(&source, "1e+", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "dimension|delim|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1.0);
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].unit, "e"));

    /* A sign with no digits is a delimiter. */
    CssLexText(&source, "+", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|eof");
    CssLexText(&source, ".", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|eof");
    CssLexText(&source, "+-", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|delim|eof");

    /* Two dots: the first cannot start a number, the second can. */
    CssLexText(&source, "1..2", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|delim|number|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1.0);
    CSS_CHECK_DOUBLE(log.tokens[2].number, 0.2);

    /* `+` before a dot-and-digit is a number; `.` alone is not. */
    CssLexText(&source, "+.5", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.5);
    RoboCheckEqInt(log.tokens[0].hasSign, 1);

    CssLexText(&source, "-.5", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, -0.5);

    /* Digits run as long as they run: no truncation of the token. */
    CssLexText(&source, "1000000000000000000000000000", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|eof");
    RoboCheckEqInt(log.tokens[0].range.end, 28u);
    /* The decade, not "greater than 1e27": the digits are exactly 10^27, so a
     * strict comparison against that literal would be testing which way the
     * conversion rounds rather than whether it kept the magnitude. */
    RoboCheck(log.tokens[0].number > 9.9e26);
    RoboCheck(log.tokens[0].number < 1.01e27);
}

static void TestTokenizePercentageTokens(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];

    CssLexText(&source, "10%", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "percentage|eof");
    RoboCheckEqInt(log.tokens[0].range.end, 3u);
    /* The value is the number as written, not divided by a hundred: CSS's
     * conversion leaves the division to whoever consumes it. */
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    RoboCheckEqInt(log.tokens[0].isInteger, 1);
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].repr, "10"));
    RoboCheck(CssSliceIsEmpty(log.tokens[0].unit));

    CssLexText(&source, "50.5%", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenPercentage);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 50.5);
    RoboCheckEqInt(log.tokens[0].isInteger, 0);

    CssLexText(&source, "+50%", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenPercentage);
    RoboCheckEqInt(log.tokens[0].hasSign, 1);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 50.0);

    CssLexText(&source, "-100%", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, -100.0);

    CssLexText(&source, "0%", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);

    /* A `%` is only a percentage right after a number. */
    CssLexText(&source, "%", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "delim|eof");
    CssLexText(&source, "10%px", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "percentage|ident|eof");
    CssLexText(&source, "10 %%", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|ws|delim|delim|eof");
}

static void TestTokenizeDimensionTokens(void)
{
    static const struct {
        const char *text;
        double number;
        const char *unit;
    } kCases[] = {
        {"10px", 10.0, "px"},     {"2em", 2.0, "em"},
        {"1rem", 1.0, "rem"},     {"100vw", 100.0, "vw"},
        {"100vh", 100.0, "vh"},   {"45deg", 45.0, "deg"},
        {"500ms", 500.0, "ms"},   {"1fr", 1.0, "fr"},
        {"0s", 0.0, "s"},         {"-1.5em", -1.5, "em"},
        {"+2.5px", 2.5, "px"},    {".5em", 0.5, "em"},
        {"1e3px", 1000.0, "px"},  {"10pt", 10.0, "pt"},
        {"3ch", 3.0, "ch"},       {"1vmin", 1.0, "vmin"},
        {"100%", 100.0, ""} /* percentage, not a dimension */
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        CssSource source = RoboCssCleared;
        CssTokenLog log;

        CssLexText(&source, kCases[i].text, &log);
        if (kCases[i].unit[0] == '\0') {
            RoboCheckEqInt(log.tokens[0].type, RoboCssTokenPercentage);
            continue;
        }
        RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDimension);
        RoboCheckEqInt(log.tokens[0].range.end, strlen(kCases[i].text));
        CSS_CHECK_DOUBLE(log.tokens[0].number, kCases[i].number);
        RoboCheck(CssSliceEqualsAscii(log.tokens[0].unit, kCases[i].unit));
        /* The numeric part is recoverable on its own: `repr` is the number as
         * written, the unit is a separate slice, and the token's value is the
         * number — so nothing has to re-scan bytes to tell them apart. */
        RoboCheckEqInt(log.tokens[0].repr.length,
                          log.tokens[0].range.end - strlen(kCases[i].unit));
        RoboCheckEqInt(memcmp(log.tokens[0].repr.bytes, kCases[i].text,
                                 log.tokens[0].repr.length), 0);
        RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]),
                          log.tokens[0].range.end - strlen(kCases[i].unit));
    }
}

static void TestTokenizeDimensionBoundaries(void)
{
    CssSource source = RoboCssCleared;
    CssTokenLog log;
    char kinds[128];
    uint8_t value[16];
    size_t length = 0u;

    /* A unit is consumed first, then `%`: `10px%` is a dimension and a
     * delimiter, never a percentage. */
    CssLexText(&source, "10px%", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "dimension|delim|eof");
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].unit, "px"));

    /* `-` may begin a unit when an identifier follows it. */
    CssLexText(&source, "10-px", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "dimension|eof");
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].unit, "-px"));

    /* A bare `-` after the number is not a unit. */
    CssLexText(&source, "10-", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|delim|eof");

    /* The unit is the raw spelling: an escape in it is preserved, and resolving
     * it is 9.10's job, not the tokenizer's. */
    CssLexText(&source, "10p\\78", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDimension);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].unit, "p\\78"));
    RoboCheckEqInt(CssTokenValueLength(&log.tokens[0]), 2u);
    RoboCheckEqInt(CssTokenValueCopy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), RoboOk);
    RoboCheckEqInt(length, 2u);
    RoboCheckEqInt(memcmp(value, "10", 2u), 0);

    /* A unit that is not a known CSS unit is still a unit: the tokenizer does
     * not keep a unit table. */
    CssLexText(&source, "10zzz", &log);
    RoboCheckEqInt(log.tokens[0].type, RoboCssTokenDimension);
    RoboCheck(CssSliceEqualsAscii(log.tokens[0].unit, "zzz"));

    /* A digit cannot begin a unit. */
    CssLexText(&source, "10 20", &log);
    CssLogKinds(&log, kinds, sizeof(kinds));
    RoboCheckStrEq(kinds, "number|ws|number|eof");
}

void TestRegisterCssTokenizeLiteral(void);

void TestRegisterCssTokenizeLiteral(void)
{
    TestRegister("css.tokenize.string_forms", TestTokenizeStringForms);
    TestRegister("css.tokenize.string_escapes", TestTokenizeStringEscapes);
    TestRegister("css.tokenize.string_unicode", TestTokenizeStringUnicode);
    TestRegister("css.tokenize.string_bad_by_newline",
                       TestTokenizeStringBadByNewline);
    TestRegister("css.tokenize.string_unterminated",
                       TestTokenizeStringUnterminated);
    TestRegister("css.tokenize.number_forms", TestTokenizeNumberForms);
    TestRegister("css.tokenize.number_matches_compiler",
                       TestTokenizeNumberMatchesCompiler);
    TestRegister("css.tokenize.number_beyond_exact_scale",
                       TestTokenizeNumberBeyondExactScale);
    TestRegister("css.tokenize.number_range_ends",
                       TestTokenizeNumberRangeEnds);
    TestRegister("css.tokenize.number_boundaries",
                       TestTokenizeNumberBoundaries);
    TestRegister("css.tokenize.percentage_tokens",
                       TestTokenizePercentageTokens);
    TestRegister("css.tokenize.dimension_tokens",
                       TestTokenizeDimensionTokens);
    TestRegister("css.tokenize.dimension_boundaries",
                       TestTokenizeDimensionBoundaries);
}
