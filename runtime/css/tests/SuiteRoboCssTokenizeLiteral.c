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

static void test_tokenize_string_forms(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];
    uint8_t value[32];
    size_t length = 0u;

    css_lex_text(&source, "\"hello\"", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "string|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    /* The token's text includes its quotes; its value excludes them. */
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "\"hello\""));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "hello"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 5u);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 7u);

    css_lex_text(&source, "'hello'", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "hello"));

    /* The two quote characters do not close each other. */
    css_lex_text(&source, "\"it's\"", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "it's"));
    css_lex_text(&source, "'say \"hi\"'", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "say \"hi\""));

    /* An empty string is closed and empty, which is not the same as a string
     * that was never closed. */
    css_lex_text(&source, "\"\"", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 0u);
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    /* Whitespace inside a string is content. */
    css_lex_text(&source, "\"  a  b  \"", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "  a  b  "));
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 8u);
    ROBO_CHECK_EQ_INT(memcmp(value, "  a  b  ", 8u), 0);
}

static void test_tokenize_string_escapes(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    uint8_t value[32];
    size_t length = 0u;

    /* An escaped quote does not close the string. */
    css_lex_text(&source, "\"a\\\"b\"", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a\"b"));

    css_lex_text(&source, "'a\\'b'", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a'b"));

    /* A backslash is itself when escaped, and an ordinary character otherwise
     * — but \"otherwise\" excludes the hex digits, because a hex escape wins over
     * a literal one. `\q` is `q`; `\b` is U+000B, which is a trap worth pinning
     * precisely because `b` looks like a letter and is not one to CSS. */
    css_lex_text(&source, "\"a\\\\b\"", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a\\b"));

    css_lex_text(&source, "\"a\\qb\"", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "aqb"));

    css_lex_text(&source, "\"a\\b\"", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 2u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 2u);
    ROBO_CHECK_EQ_INT(value[0], (uint8_t)'a');
    ROBO_CHECK_EQ_INT(value[1], 0x0Bu);

    /* Hex escapes: one to six digits, then one swallowed whitespace. */
    css_lex_text(&source, "\"\\41\"", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "A"));
    css_lex_text(&source, "\"\\000041\"", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "A"));
    css_lex_text(&source, "\"\\41 b\"", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "Ab"));

    /* A hex escape can spell a newline without ending the string: escapes are
     * never re-scanned as syntax. */
    css_lex_text(&source, "\"a\\A b\"", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 3u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 3u);
    ROBO_CHECK_EQ_INT(value[1], (uint8_t)'\n');

    /* Out-of-range escapes are U+FFFD, three bytes. */
    css_lex_text(&source, "\"\\110000\"", &log);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 3u);

    /* A backslash before a newline swallows the newline and the string goes on. */
    css_lex_text(&source, "\"a\\\nb\"", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "ab"));
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    /* Six bytes: the two quotes, `a`, the backslash, the break and `b`. The
     * break is inside the token because the token's range is the source. */
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 6u);

    /* A backslash at the end of input is dropped and the string is unclosed. */
    css_lex_text(&source, "\"ab\\", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "ab"));
}

static void test_tokenize_string_unicode(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    uint8_t value[128];
    size_t length = 0u;
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        char text[256];
        size_t sample_length = strlen(css_samples[i].text);

        snprintf(text, sizeof(text), "\"%s\"", css_samples[i].text);
        css_lex_text(&source, text, &log);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
        ROBO_CHECK_EQ_INT(log.count, 2u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
        ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), sample_length);
        ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                                   &length, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(length, sample_length);
        ROBO_CHECK_EQ_INT(memcmp(value, css_samples[i].text, sample_length), 0);
    }
}

static void test_tokenize_string_bad_by_newline(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* A newline ends a string in front of itself. The newline is *not* consumed,
     * so it becomes the next token — which is the one place the specification
     * re-consumes an input code point, and the reason the whitespace token is
     * still there afterwards. */
    css_lex_text(&source, "\"a\nb\"", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "bad-string|ws|ident|string|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.begin, 0u);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[1].range.begin, 2u);
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* A carriage return and a form feed are newlines for this purpose too. */
    css_lex_text(&source, "\"a\rb", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_STRING);
    css_lex_text(&source, "\"a\fb", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_STRING);

    /* A CRLF pair is one newline: one bad string, one whitespace token covering
     * both bytes. */
    css_lex_text(&source, "\"a\r\nb\"", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "bad-string|ws|ident|string|eof");
    ROBO_CHECK_EQ_INT(log.tokens[1].range.begin, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[1].range.end, 4u);

    /* A bad string's value is what it had before the newline. */
    css_lex_text(&source, "\"abc\n", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_STRING);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "abc"));
}

static void test_tokenize_string_unterminated(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* Running off the end of input leaves a string that was never closed — a
     * string, not a bad one, which is what `closed` is for. */
    css_lex_text(&source, "\"abc", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "string|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "abc"));
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);
    ROBO_CHECK_EQ_INT(log.error.position.offset, 0u);

    /* A lone quote is an empty, unclosed string. */
    css_lex_text(&source, "\"", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 0u);

    css_lex_text(&source, "'", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);

    /* An unclosed string is not a fatal problem: the run of ordinary text after
     * it is still tokenized, and the stream still ends at EOF. */
    css_lex_text(&source, "\"abc def ghi", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
}

static void test_tokenize_number_forms(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;

    css_lex_text(&source, "0", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_NUMBER);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 1);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 0);
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].repr, "0"));

    css_lex_text(&source, "10", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 1);

    css_lex_text(&source, "-10", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, -10.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 1);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 1);

    css_lex_text(&source, "+10", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 1);

    css_lex_text(&source, "3.14", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 3.14);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 0);
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].repr, "3.14"));

    css_lex_text(&source, ".5", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.5);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 0);

    css_lex_text(&source, "-0.25", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, -0.25);

    css_lex_text(&source, "1e3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1000.0);
    /* An exponent makes it a number, not an integer, even when the value is
     * whole — which is what Blink's value type says too. */
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 0);

    css_lex_text(&source, "1E3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1000.0);

    css_lex_text(&source, "1e+3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1000.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 0);

    css_lex_text(&source, "1e-3", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.001);

    css_lex_text(&source, "12.5e2", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1250.0);

    css_lex_text(&source, "007", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 7.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 1);

    css_lex_text(&source, "0.0", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
}

static void test_tokenize_number_matches_compiler(void)
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
         * test_tokenize_number_beyond_exact_scale. */
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        robo_css_source_t source = ROBO_CSS_CLEARED;
        css_token_log_t log;

        css_lex_text(&source, kCases[i].text, &log);
        ROBO_CHECK_EQ_INT(log.count, 2u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_NUMBER);
        ROBO_CHECK_EQ_INT(log.tokens[0].range.end, strlen(kCases[i].text));
        CSS_CHECK_DOUBLE(log.tokens[0].number, kCases[i].expected);
    }
}

static void test_tokenize_number_beyond_exact_scale(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;

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
    css_lex_text(&source, "1e-23", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_NUMBER);
    ROBO_CHECK(log.tokens[0].number > 0.0);
    CSS_CHECK_DOUBLE_NEAR(log.tokens[0].number, 1e-23, 1e-23 * 1e-15);

    css_lex_text(&source, "1.7976931348623157e308", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_NUMBER);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    CSS_CHECK_DOUBLE_NEAR(log.tokens[0].number, 1.7976931348623157e308,
                          1.7976931348623157e308 * 1e-15);
}

static void test_tokenize_number_range_ends(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;

    /* Overflow saturates to infinity and underflow to zero: CSS's conversion
     * step leaves them there, and a tokenizer that refused them would be
     * inventing a policy of its own. */
    css_lex_text(&source, "1e400", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_NUMBER);
    ROBO_CHECK(log.tokens[0].number > 1.0e308);
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    css_lex_text(&source, "1e-400", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 0);
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    css_lex_text(&source, "-1e-400", &log);
    ROBO_CHECK(log.tokens[0].number == 0.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 1);

    /* An exponent of a billion is clamped rather than wrapped. */
    css_lex_text(&source, "1e999999999999", &log);
    ROBO_CHECK(log.tokens[0].number > 1.0e308);
    css_lex_text(&source, "1e-999999999999", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);

    /* `-0` keeps its sign and is still zero. */
    css_lex_text(&source, "-0", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 1);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 1);

    /* Long digit strings keep the leading nineteen digits and drop the rest, so
     * the value is a truncation and stays inside the true value's decade. The
     * policy is documented in the tokenizer; this pins it. */
    css_lex_text(&source, "12345678901234567890123", &log);
    ROBO_CHECK(log.tokens[0].number > 1.234e22);
    ROBO_CHECK(log.tokens[0].number < 1.235e22);
    css_lex_text(&source, "0.0000000000000000000000001", &log);
    ROBO_CHECK(log.tokens[0].number > 0.0);
    ROBO_CHECK(log.tokens[0].number < 1.0e-24);
}

static void test_tokenize_number_boundaries(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* `10.` is a number followed by a delimiter: a `.` only continues a number
     * when a digit follows it. */
    css_lex_text(&source, "10.", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|delim|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[1].range.begin, 2u);

    /* `1e` is a *dimension*, not a number followed by a name. The `e` cannot
     * continue the number — no digits follow it — so the numeric part stops at
     * `1`, and the unit rule then runs: `e` would start an identifier, so it is
     * the unit. Which is exactly what the grammar says, and worth pinning,
     * because the intuitive reading (an identifier follows the number) produces
     * two tokens and the correct one produces one. */
    css_lex_text(&source, "1e", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "dimension|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1.0);
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].unit, "e"));
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 1);

    css_lex_text(&source, "1e+", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "dimension|delim|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1.0);
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].unit, "e"));

    /* A sign with no digits is a delimiter. */
    css_lex_text(&source, "+", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|eof");
    css_lex_text(&source, ".", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|eof");
    css_lex_text(&source, "+-", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|delim|eof");

    /* Two dots: the first cannot start a number, the second can. */
    css_lex_text(&source, "1..2", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|delim|number|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 1.0);
    CSS_CHECK_DOUBLE(log.tokens[2].number, 0.2);

    /* `+` before a dot-and-digit is a number; `.` alone is not. */
    css_lex_text(&source, "+.5", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.5);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 1);

    css_lex_text(&source, "-.5", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|eof");
    CSS_CHECK_DOUBLE(log.tokens[0].number, -0.5);

    /* Digits run as long as they run: no truncation of the token. */
    css_lex_text(&source, "1000000000000000000000000000", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 28u);
    /* The decade, not "greater than 1e27": the digits are exactly 10^27, so a
     * strict comparison against that literal would be testing which way the
     * conversion rounds rather than whether it kept the magnitude. */
    ROBO_CHECK(log.tokens[0].number > 9.9e26);
    ROBO_CHECK(log.tokens[0].number < 1.01e27);
}

static void test_tokenize_percentage_tokens(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    css_lex_text(&source, "10%", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "percentage|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 3u);
    /* The value is the number as written, not divided by a hundred: CSS's
     * conversion leaves the division to whoever consumes it. */
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 1);
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].repr, "10"));
    ROBO_CHECK(robo_css_slice_is_empty(log.tokens[0].unit));

    css_lex_text(&source, "50.5%", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_PERCENTAGE);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 50.5);
    ROBO_CHECK_EQ_INT(log.tokens[0].is_integer, 0);

    css_lex_text(&source, "+50%", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_PERCENTAGE);
    ROBO_CHECK_EQ_INT(log.tokens[0].has_sign, 1);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 50.0);

    css_lex_text(&source, "-100%", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, -100.0);

    css_lex_text(&source, "0%", &log);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 0.0);

    /* A `%` is only a percentage right after a number. */
    css_lex_text(&source, "%", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|eof");
    css_lex_text(&source, "10%px", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "percentage|ident|eof");
    css_lex_text(&source, "10 %%", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|ws|delim|delim|eof");
}

static void test_tokenize_dimension_tokens(void)
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
        robo_css_source_t source = ROBO_CSS_CLEARED;
        css_token_log_t log;

        css_lex_text(&source, kCases[i].text, &log);
        if (kCases[i].unit[0] == '\0') {
            ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_PERCENTAGE);
            continue;
        }
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DIMENSION);
        ROBO_CHECK_EQ_INT(log.tokens[0].range.end, strlen(kCases[i].text));
        CSS_CHECK_DOUBLE(log.tokens[0].number, kCases[i].number);
        ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].unit, kCases[i].unit));
        /* The numeric part is recoverable on its own: `repr` is the number as
         * written, the unit is a separate slice, and the token's value is the
         * number — so nothing has to re-scan bytes to tell them apart. */
        ROBO_CHECK_EQ_INT(log.tokens[0].repr.length,
                          log.tokens[0].range.end - strlen(kCases[i].unit));
        ROBO_CHECK_EQ_INT(memcmp(log.tokens[0].repr.bytes, kCases[i].text,
                                 log.tokens[0].repr.length), 0);
        ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]),
                          log.tokens[0].range.end - strlen(kCases[i].unit));
    }
}

static void test_tokenize_dimension_boundaries(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];
    uint8_t value[16];
    size_t length = 0u;

    /* A unit is consumed first, then `%`: `10px%` is a dimension and a
     * delimiter, never a percentage. */
    css_lex_text(&source, "10px%", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "dimension|delim|eof");
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].unit, "px"));

    /* `-` may begin a unit when an identifier follows it. */
    css_lex_text(&source, "10-px", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "dimension|eof");
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].unit, "-px"));

    /* A bare `-` after the number is not a unit. */
    css_lex_text(&source, "10-", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|delim|eof");

    /* The unit is the raw spelling: an escape in it is preserved, and resolving
     * it is 9.10's job, not the tokenizer's. */
    css_lex_text(&source, "10p\\78", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DIMENSION);
    CSS_CHECK_DOUBLE(log.tokens[0].number, 10.0);
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].unit, "p\\78"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 2u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 2u);
    ROBO_CHECK_EQ_INT(memcmp(value, "10", 2u), 0);

    /* A unit that is not a known CSS unit is still a unit: the tokenizer does
     * not keep a unit table. */
    css_lex_text(&source, "10zzz", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DIMENSION);
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[0].unit, "zzz"));

    /* A digit cannot begin a unit. */
    css_lex_text(&source, "10 20", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|ws|number|eof");
}

void robo_test_register_css_tokenize_literal(void);

void robo_test_register_css_tokenize_literal(void)
{
    robo_test_register("css.tokenize.string_forms", test_tokenize_string_forms);
    robo_test_register("css.tokenize.string_escapes", test_tokenize_string_escapes);
    robo_test_register("css.tokenize.string_unicode", test_tokenize_string_unicode);
    robo_test_register("css.tokenize.string_bad_by_newline",
                       test_tokenize_string_bad_by_newline);
    robo_test_register("css.tokenize.string_unterminated",
                       test_tokenize_string_unterminated);
    robo_test_register("css.tokenize.number_forms", test_tokenize_number_forms);
    robo_test_register("css.tokenize.number_matches_compiler",
                       test_tokenize_number_matches_compiler);
    robo_test_register("css.tokenize.number_beyond_exact_scale",
                       test_tokenize_number_beyond_exact_scale);
    robo_test_register("css.tokenize.number_range_ends",
                       test_tokenize_number_range_ends);
    robo_test_register("css.tokenize.number_boundaries",
                       test_tokenize_number_boundaries);
    robo_test_register("css.tokenize.percentage_tokens",
                       test_tokenize_percentage_tokens);
    robo_test_register("css.tokenize.dimension_tokens",
                       test_tokenize_dimension_tokens);
    robo_test_register("css.tokenize.dimension_boundaries",
                       test_tokenize_dimension_boundaries);
}
