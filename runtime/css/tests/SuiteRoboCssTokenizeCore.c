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

static void test_tokenize_whitespace_runs(void)
{
    static const char *kKinds = "ws|ident|ws|ident|ws|ident|eof";
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* Space, tab and form feed all run; a mixed run is one token. */
    css_lex_text(&source, "   a\t\f b \t c", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, kKinds);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_WHITESPACE);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.begin, 0u);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 3u);
    ROBO_CHECK_EQ_INT(log.tokens[1].type, ROBO_CSS_TOKEN_IDENT);
    /* A run is *greedy*: it swallows every whitespace code point up to the next
     * non-whitespace one, whatever the mixture, so the second run is tab, form
     * feed and space together (4..7) and the third is space, tab and space
     * (8..11). Splitting a run at a type boundary would break the tiling. */
    ROBO_CHECK_EQ_INT(log.tokens[2].range.begin, 4u);
    ROBO_CHECK_EQ_INT(log.tokens[2].range.end, 7u);
    ROBO_CHECK_EQ_INT(log.tokens[4].range.begin, 8u);
    ROBO_CHECK_EQ_INT(log.tokens[4].range.end, 11u);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* Whitespace is a token type, not a gap: it is how a caller that needs to
     * reproduce the source gets its spacing back. */
    ROBO_CHECK(robo_css_token_type_is_ignorable(ROBO_CSS_TOKEN_WHITESPACE));
    ROBO_CHECK(robo_css_token_type_is_ignorable(ROBO_CSS_TOKEN_COMMENT));
    ROBO_CHECK(!robo_css_token_type_is_ignorable(ROBO_CSS_TOKEN_IDENT));
}

static void test_tokenize_whitespace_newlines(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_position_t position;

    /* LF, CR, FF and CRLF are each one break, and a *CRLF run* is one token
     * covering both bytes — LFCR is two breaks, not a pair. */
    css_lex_text(&source, "a\nb\rc\r\nd\fe", &log);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_WHITESPACE), 4u);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_IDENT), 5u);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK_EQ_INT(log.tokens[2].range.begin, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[2].range.end, 3u);
    ROBO_CHECK_EQ_INT(log.tokens[4].range.begin, 4u);
    ROBO_CHECK_EQ_INT(log.tokens[4].range.end, 5u);
    /* The pair is one run covering both bytes, which is the whole content of
     * the CRLF rule. */
    ROBO_CHECK_EQ_INT(log.tokens[5].type, ROBO_CSS_TOKEN_WHITESPACE);
    ROBO_CHECK_EQ_INT(log.tokens[5].range.begin, 5u);
    ROBO_CHECK_EQ_INT(log.tokens[5].range.end, 7u);

    /* Line and column: CRLF is one break and the form feed is another, so the
     * last `e` is on line 5 — four breaks in ten bytes. The loop asks the token
     * rather than the cursor, because `at_end` answers "is there source left"
     * and is already true once the final byte has been consumed. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    do {
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    } while (token.type != ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_position(&tokenizer, &position, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.offset, 10u);
    ROBO_CHECK_EQ_INT(position.line, 5u);
    ROBO_CHECK_EQ_INT(position.column, 2u);
    robo_css_tokenizer_dispose(&tokenizer);

    /* LFCR is not a CRLF pair. The run is still *one* whitespace token, because
     * a run is greedy regardless of which breaks it holds — what makes the pair
     * different is that it is two breaks where CRLF is one, so the line after it
     * is a line further down. */
    {
        robo_css_source_t lfcr = ROBO_CSS_CLEARED;
        css_token_log_t lfcr_log;

        css_lex_text(&lfcr, "a\n\rb", &lfcr_log);
        ROBO_CHECK_EQ_INT(css_count_type(&lfcr_log, ROBO_CSS_TOKEN_WHITESPACE), 1u);
        ROBO_CHECK_EQ_INT(css_count_type(&lfcr_log, ROBO_CSS_TOKEN_IDENT), 2u);
        ROBO_CHECK_EQ_INT(lfcr_log.tokens[1].range.begin, 1u);
        ROBO_CHECK_EQ_INT(lfcr_log.tokens[1].range.end, 3u);
        ROBO_CHECK_EQ_INT(robo_css_source_position(&lfcr, 3u, &position), ROBO_OK);
        ROBO_CHECK_EQ_INT(position.line, 3u);
        ROBO_CHECK_EQ_INT(position.column, 1u);
    }
}

static void test_tokenize_comment_forms(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    css_lex_text(&source, "/* one */", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.begin, 0u);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 9u);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    /* A comment's value is what it says: markers and all, never a name. */
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "/* one */"));

    css_lex_text(&source, "/**/", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 4u);

    css_lex_text(&source, "/* line\nline\nline */", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    /* One token across two newlines, and the newline is inside it. */
    ROBO_CHECK_EQ_INT(log.tokens[0].range.begin, 0u);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 20u);
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    css_lex_text(&source, "a/* x */b", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|comment|ident|eof");
    ROBO_CHECK_EQ_INT(log.tokens[1].range.begin, 1u);
    ROBO_CHECK_EQ_INT(log.tokens[1].range.end, 8u);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* An asterisk inside a comment does not end it, and a comment may be a
     * single pair of markers with nothing between them. */
    css_lex_text(&source, "/* a * b ** c */ ident", &log);
    /* Comment, the space that followed it, the name, and EOF. */
    ROBO_CHECK_EQ_INT(log.count, 4u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 16u);
    ROBO_CHECK_EQ_INT(log.tokens[1].type, ROBO_CSS_TOKEN_WHITESPACE);
    ROBO_CHECK_EQ_INT(log.tokens[2].type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
}

static void test_tokenize_comment_unicode(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    uint8_t value[64];
    size_t length = 0u;
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        char text[256];

        /* Marker, sample, marker: the sample's bytes must come back untouched. */
        snprintf(text, sizeof(text), "/* %s */", css_samples[i].text);
        css_lex_text(&source, text, &log);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
        ROBO_CHECK_EQ_INT(log.count, 2u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
        ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
        ROBO_CHECK(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                             &length, NULL) == ROBO_OK);
        ROBO_CHECK_EQ_INT(length, strlen(text));
        ROBO_CHECK_EQ_INT(memcmp(value, text, length), 0);
    }
}

static void test_tokenize_comment_unterminated(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    const robo_css_error_t *error;

    /* An unterminated comment runs to the end of input and is still a comment:
     * the specification never makes it an error token. The stream records the
     * reason anyway, and `closed` is how the token itself says. */
    css_lex_text(&source, "a /* never closed", &log);
    /* Name, space, the comment that runs to the end, and EOF: an unterminated
     * comment is still one token, not a token plus a remainder. */
    ROBO_CHECK_EQ_INT(log.count, 4u);
    ROBO_CHECK_EQ_INT(log.tokens[1].type, ROBO_CSS_TOKEN_WHITESPACE);
    ROBO_CHECK_EQ_INT(log.tokens[2].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[2].closed, 0);
    ROBO_CHECK_EQ_INT(log.tokens[2].range.end, 17u);
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);
    ROBO_CHECK_EQ_INT(log.error.position.offset, 2u);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    css_lex_text(&source, "/*", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 2u);

    /* An opening marker followed by an asterisk is not closed by that
     * asterisk: two bytes are needed, and there is only one. */
    css_lex_text(&source, "/**", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 3u);

    /* Marker, asterisk, slash: still open, because the asterisk was consumed
     * looking for a terminator before the slash arrived. */
    css_lex_text(&source, "/*/", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 3u);

    /* The recorded error does not stop the stream, and it is recoverable. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_COMMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 1);
    error = robo_css_tokenizer_error(&tokenizer);
    ROBO_CHECK(error != NULL);
    ROBO_CHECK_EQ_INT(robo_css_error_kind_is_recoverable(error->kind), 1);
    ROBO_CHECK_EQ_INT(robo_css_error_kind_is_fatal(error->kind), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_slash_is_not_a_comment(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    css_lex_text(&source, "a/b", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|delim|ident|eof");
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    css_lex_text(&source, "/", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].range.begin, 0u);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 1u);

    /* `/ *` with a space is two delimiters, not a comment. */
    css_lex_text(&source, "/ *", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|ws|delim|eof");
}

static void test_tokenize_cdo_cdc(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    css_lex_text(&source, "<!--", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "cdo|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 4u);

    css_lex_text(&source, "-->", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "cdc|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 3u);

    css_lex_text(&source, "<!-- a -->", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "cdo|ws|ident|ws|cdc|eof");
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* The lookalikes: `--` is an identifier (a custom-property prefix), `->` is
     * two delimiters, and `<!-` is a delimiter followed by an identifier. */
    css_lex_text(&source, "--", &log);
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(log.tokens[0].type), "ident");

    css_lex_text(&source, "->", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|delim|eof");

    css_lex_text(&source, "<!--", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_CDO);
    ROBO_CHECK_EQ_INT(log.tokens[0].text.length, 4u);
}

static void test_tokenize_punctuation_tokens(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    css_lex_text(&source, ":;,()[]{}", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds,
                      "colon|semicolon|comma|left-paren|right-paren|left-square|"
                      "right-square|left-curly|right-curly|eof");
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK_EQ_INT(log.tokens[3].range.begin, 3u);
    ROBO_CHECK_EQ_INT(log.tokens[8].range.end, 9u);

    /* Every bracket type knows its partner, and only its partner. */
    {
        robo_css_token_type_t close = ROBO_CSS_TOKEN_NONE;

        ROBO_CHECK_EQ_INT(robo_css_token_type_matching_close(ROBO_CSS_TOKEN_LEFT_PAREN,
                                                             &close), 1);
        ROBO_CHECK_EQ_INT(close, ROBO_CSS_TOKEN_RIGHT_PAREN);
        ROBO_CHECK_EQ_INT(robo_css_token_type_matching_close(ROBO_CSS_TOKEN_LEFT_SQUARE,
                                                             &close), 1);
        ROBO_CHECK_EQ_INT(close, ROBO_CSS_TOKEN_RIGHT_SQUARE);
        ROBO_CHECK_EQ_INT(robo_css_token_type_matching_close(ROBO_CSS_TOKEN_LEFT_CURLY,
                                                             &close), 1);
        ROBO_CHECK_EQ_INT(close, ROBO_CSS_TOKEN_RIGHT_CURLY);
        ROBO_CHECK_EQ_INT(robo_css_token_type_matching_close(ROBO_CSS_TOKEN_COLON,
                                                             &close), 0);
    }
}

static void test_tokenize_delimiter_set(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[256];
    size_t i;

    /* Each of these is one delimiter token on its own. */
    static const char kDelims[] = "!%&*=^?$|~`<>";
    for (i = 0u; kDelims[i] != '\0'; i++) {
        char text[2];

        text[0] = kDelims[i];
        text[1] = '\0';
        css_lex_text(&source, text, &log);
        ROBO_CHECK_EQ_INT(log.count, 2u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DELIM);
        ROBO_CHECK_EQ_INT(log.tokens[0].text.length, 1u);
        ROBO_CHECK_EQ_INT(log.tokens[0].text.bytes[0], (uint8_t)kDelims[i]);
    }

    /* The two-character match operators are *pairs* of delimiters in this
     * vocabulary: the 9.1 token record has no substring/prefix/suffix/dash/
     * include/column token, and inventing one here would be a second model.
     * 9.4 composes them, which is why this is pinned rather than left implicit. */
    css_lex_text(&source, "*= ^= $= ~= |= || <<<", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds,
                      "delim|delim|ws|delim|delim|ws|delim|delim|ws|delim|delim|"
                      "ws|delim|delim|ws|delim|delim|ws|delim|delim|delim|eof");
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* No unicode-range token exists either, so `U+0025` lexes as an identifier,
     * a `+`-signed number and nothing else. Pinned so a later phase cannot
     * assume a token the model does not have. */
    css_lex_text(&source, "U+0025", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|number|eof");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "U"));
    ROBO_CHECK_EQ_INT(log.tokens[1].has_sign, 1);
    CSS_CHECK_DOUBLE(log.tokens[1].number, 25.0);
}

static void test_tokenize_control_bytes_are_delimiters(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    size_t i;

    for (i = 1u; i < 0x20u; i++) {
        uint8_t text[3];

        if (i == 0x09u || i == 0x0Au || i == 0x0Cu || i == 0x0Du) {
            continue; /* whitespace, tested elsewhere */
        }
        text[0] = (uint8_t)i;
        text[1] = 'x';
        text[2] = '\0';
        css_lex_text(&source, (const char *)text, &log);
        ROBO_CHECK_EQ_INT(log.count, 3u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DELIM);
        ROBO_CHECK_EQ_INT(log.tokens[1].type, ROBO_CSS_TOKEN_IDENT);
    }

    {
        uint8_t del = 0x7Fu;
        char text[2];

        text[0] = (char)del;
        text[1] = '\0';
        css_lex_text(&source, text, &log);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DELIM);
    }
}

static void test_tokenize_nul_is_an_identifier_code_point(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    uint8_t value[8];
    size_t length = 0u;
    static const uint8_t kWithNul[] = {'a', 0x00u, 'b'};

    /* CSS's input preprocessing replaces U+0000 with U+FFFD, which starts an
     * identifier. We classify it the same way and keep the raw byte, so
     * `a<NUL>b` is one identifier of three bytes — not a name broken in two, and
     * not a value claiming a replacement character the caller never wrote. */
    css_source_of_string(&source, "");
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, kWithNul, sizeof(kWithNul), NULL),
                      ROBO_OK);
    css_lex(&source, &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 3u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 3u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 3u);
    ROBO_CHECK_EQ_INT(memcmp(value, kWithNul, 3u), 0);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK(css_log_tiles(&log, sizeof(kWithNul)));

    /* A leading NUL begins an identifier rather than being a delimiter. */
    {
        static const uint8_t kLeading[] = {0x00u, 'x'};
        robo_css_source_t leading = ROBO_CSS_CLEARED;
        css_token_log_t leading_log;

        css_source_of_string(&leading, "");
        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&leading, kLeading, sizeof(kLeading),
                                                     NULL), ROBO_OK);
        css_lex(&leading, &leading_log);
        ROBO_CHECK_EQ_INT(leading_log.count, 2u);
        ROBO_CHECK_EQ_INT(leading_log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
        ROBO_CHECK_EQ_INT(leading_log.tokens[0].range.end, 2u);
    }

    /* Inside a url, where U+FFFD is not allowed to be, a NUL is a non-printable
     * and makes the url bad rather than being silently carried as content. */
    {
        static const uint8_t kUrlNul[] = {'u', 'r', 'l', '(', 'a', 0x00u, 'b', ')'};
        robo_css_source_t url_source = ROBO_CSS_CLEARED;
        css_token_log_t url_log;

        css_source_of_string(&url_source, "");
        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&url_source, kUrlNul, sizeof(kUrlNul),
                                                    NULL), ROBO_OK);
        css_lex(&url_source, &url_log);
        ROBO_CHECK_EQ_INT(url_log.tokens[0].type, ROBO_CSS_TOKEN_BAD_URL);
        ROBO_CHECK_EQ_INT(url_log.has_error, 1);
        ROBO_CHECK(css_log_tiles(&url_log, sizeof(kUrlNul)));
    }
}

static void test_tokenize_invalid_utf8(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* A lone continuation byte, a stray 0xFF, and a truncated three-byte
     * sequence are each one ill-formed position: one INVALID token, one byte,
     * one recorded error — never folded into a neighbouring name. */
    {
        static const uint8_t kLoneContinuation[] = {'a', 0x80u, 'b'};

        css_source_of_string(&source, "");
        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, kLoneContinuation,
                                                     sizeof(kLoneContinuation), NULL),
                          ROBO_OK);
        css_lex(&source, &log);
        css_log_kinds(&log, kinds, sizeof(kinds));
        ROBO_CHECK_STR_EQ(kinds, "ident|invalid|ident|eof");
        ROBO_CHECK_EQ_INT(log.tokens[1].range.begin, 1u);
        ROBO_CHECK_EQ_INT(log.tokens[1].range.end, 2u);
        ROBO_CHECK_EQ_INT(log.has_error, 1);
        ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_INVALID_TOKEN);
        ROBO_CHECK_EQ_INT(log.error.position.offset, 1u);
        ROBO_CHECK(css_log_tiles(&log, sizeof(kLoneContinuation)));
    }
    {
        static const uint8_t kStrayFF[] = {0xFFu, 0xFEu};

        css_source_of_string(&source, "");
        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, kStrayFF, sizeof(kStrayFF),
                                                     NULL), ROBO_OK);
        css_lex(&source, &log);
        ROBO_CHECK_EQ_INT(log.count, 3u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_INVALID);
        ROBO_CHECK_EQ_INT(log.tokens[1].type, ROBO_CSS_TOKEN_INVALID);
        ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 1u);
        ROBO_CHECK_EQ_INT(log.tokens[1].range.begin, 1u);
        ROBO_CHECK_EQ_INT(log.tokens[1].range.end, 2u);
    }
    {
        static const uint8_t kTruncated[] = {0xE4u, 0xB8u};

        css_source_of_string(&source, "");
        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, kTruncated,
                                                     sizeof(kTruncated), NULL), ROBO_OK);
        css_lex(&source, &log);
        ROBO_CHECK_EQ_INT(log.count, 3u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_INVALID);
        ROBO_CHECK_EQ_INT(log.tokens[1].type, ROBO_CSS_TOKEN_INVALID);
    }

    /* A *surrogate encoded in UTF-8* is ill-formed UTF-8 and is reported as
     * such. Blink never sees this case because it tokenizes decoded UTF-16,
     * where the surrogate is substituted with U+FFFD instead. */
    {
        static const uint8_t kSurrogate[] = {0xEDu, 0xA0u, 0x80u};

        css_source_of_string(&source, "");
        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, kSurrogate,
                                                     sizeof(kSurrogate), NULL), ROBO_OK);
        css_lex(&source, &log);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_INVALID);
        ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 1u);
        ROBO_CHECK_EQ_INT(log.has_error, 1);
    }

    /* A well-formed multi-byte scalar is a name, not an error. */
    {
        static const uint8_t kValid[] = {0xE4u, 0xB8u, 0xADu};

        css_source_of_string(&source, "");
        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, kValid, sizeof(kValid),
                                                     NULL), ROBO_OK);
        css_lex(&source, &log);
        ROBO_CHECK_EQ_INT(log.count, 2u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
        ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 3u);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
    }
}

static void test_tokenize_source_kinds_agree(void)
{
    static const char *kSheet = "a{color:red}";

    robo_css_source_t borrowed = ROBO_CSS_CLEARED;
    robo_css_source_t owned = ROBO_CSS_CLEARED;
    robo_css_source_t empty = ROBO_CSS_CLEARED;
    css_token_log_t borrowed_log;
    css_token_log_t owned_log;
    css_token_log_t empty_log;
    char borrowed_kinds[128];
    char owned_kinds[128];

    css_source_of_string(&borrowed, kSheet);
    ROBO_CHECK_EQ_INT(robo_css_source_copy(&owned, robo_css_source_bytes(&borrowed),
                                           robo_css_source_length(&borrowed), NULL),
                      ROBO_OK);
    robo_css_source_init(&empty);
    ROBO_CHECK_EQ_INT(robo_css_source_set_empty(&empty, NULL), ROBO_OK);

    css_lex(&borrowed, &borrowed_log);
    css_lex(&owned, &owned_log);
    css_lex(&empty, &empty_log);
    css_log_kinds(&borrowed_log, borrowed_kinds, sizeof(borrowed_kinds));
    css_log_kinds(&owned_log, owned_kinds, sizeof(owned_kinds));
    ROBO_CHECK_STR_EQ(borrowed_kinds, owned_kinds);
    ROBO_CHECK_EQ_INT(borrowed_log.count, owned_log.count);
    ROBO_CHECK(css_log_tiles(&borrowed_log, strlen(kSheet)));
    ROBO_CHECK(css_log_tiles(&owned_log, strlen(kSheet)));

    /* EMPTY is a legal style sheet: one EOF, no error, nothing to read. */
    ROBO_CHECK_EQ_INT(empty_log.count, 1u);
    ROBO_CHECK_EQ_INT(empty_log.tokens[0].type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(empty_log.has_error, 0);

    robo_css_source_dispose(&owned);
}

void robo_test_register_css_tokenize_core(void);

void robo_test_register_css_tokenize_core(void)
{
    robo_test_register("css.tokenize.whitespace_runs", test_tokenize_whitespace_runs);
    robo_test_register("css.tokenize.whitespace_newlines",
                       test_tokenize_whitespace_newlines);
    robo_test_register("css.tokenize.comment_forms", test_tokenize_comment_forms);
    robo_test_register("css.tokenize.comment_unicode", test_tokenize_comment_unicode);
    robo_test_register("css.tokenize.comment_unterminated",
                       test_tokenize_comment_unterminated);
    robo_test_register("css.tokenize.slash_is_not_a_comment",
                       test_tokenize_slash_is_not_a_comment);
    robo_test_register("css.tokenize.cdo_cdc", test_tokenize_cdo_cdc);
    robo_test_register("css.tokenize.punctuation_tokens",
                       test_tokenize_punctuation_tokens);
    robo_test_register("css.tokenize.delimiter_set", test_tokenize_delimiter_set);
    robo_test_register("css.tokenize.control_bytes_are_delimiters",
                       test_tokenize_control_bytes_are_delimiters);
    robo_test_register("css.tokenize.nul_is_identifier_code_point",
                       test_tokenize_nul_is_an_identifier_code_point);
    robo_test_register("css.tokenize.invalid_utf8", test_tokenize_invalid_utf8);
    robo_test_register("css.tokenize.source_kinds_agree",
                       test_tokenize_source_kinds_agree);
}
