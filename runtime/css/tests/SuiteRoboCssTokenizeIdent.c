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

static void test_tokenize_ident_spellings(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    css_lex_text(&source, "color", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 5u);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "color"));
    ROBO_CHECK(robo_css_token_value_equals_ascii_ci(&log.tokens[0], "COLOR"));
    ROBO_CHECK(!robo_css_token_value_equals_ascii(&log.tokens[0], "COLOR"));
    /* `text` and the value agree when there is nothing to resolve. */
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "color"));
    ROBO_CHECK_EQ_INT(robo_css_token_has_escapes(&log.tokens[0]), 0);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 5u);

    css_lex_text(&source, "foo-bar _underscore --custom-property -webkit-box x1", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|ws|ident|ws|ident|ws|ident|ws|ident|eof");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "foo-bar"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[2], "_underscore"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[4], "--custom-property"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[6], "-webkit-box"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[8], "x1"));
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* A name is a name: the tokenizer does not know a property from a tag, and
     * `.class` is a delimiter followed by an identifier, not a token. */
    css_lex_text(&source, "background button #title .class", &log);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_IDENT), 3u);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_HASH), 1u);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_DELIM), 1u);
}

static void test_tokenize_ident_unicode(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    uint8_t value[128];
    size_t length = 0u;
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        const char *sample = css_samples[i].text;
        size_t sample_length = strlen(sample);

        css_lex_text(&source, sample, &log);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
        ROBO_CHECK(css_log_tiles(&log, sample_length));

        if (strchr(sample, ' ') == NULL) {
            /* A word of non-ASCII characters is one identifier covering all of
             * it, and its value is the sample's bytes exactly: no shaping, no
             * normalisation, no replacement. */
            ROBO_CHECK_EQ_INT(log.count, 2u);
            ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
            ROBO_CHECK_EQ_INT(log.tokens[0].range.end, sample_length);
            ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), sample_length);
            ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value,
                                                       sizeof(value), &length, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(length, sample_length);
            ROBO_CHECK_EQ_INT(memcmp(value, sample, sample_length), 0);
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
                if (log.tokens[t].type == ROBO_CSS_TOKEN_WHITESPACE ||
                    log.tokens[t].type == ROBO_CSS_TOKEN_EOF) {
                    continue;
                }
                ROBO_CHECK_EQ_INT(log.tokens[t].type, ROBO_CSS_TOKEN_IDENT);
                if (joined > 0u) {
                    value[joined++] = (uint8_t)' ';
                }
                ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[t], value + joined,
                                                           sizeof(value) - joined,
                                                           &length, NULL), ROBO_OK);
                joined += length;
            }
            ROBO_CHECK_EQ_INT(joined, sample_length);
            ROBO_CHECK_EQ_INT(memcmp(value, sample, sample_length), 0);
        }
    }

    /* A name may mix scripts: non-ASCII code points are name characters, so an
     * Arabic word followed immediately by an accented Latin one is a single
     * identifier covering every byte of both. */
    css_lex_text(&source,
                 "\xD8\xA7\xD9\x84\xD9\x84\xD9\x88\xD9\x86" "caf\xC3\xA9", &log);
    ROBO_CHECK_EQ_INT(log.count, 2u);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, robo_css_source_length(&source));
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]),
                      robo_css_source_length(&source));
    ROBO_CHECK_EQ_INT(log.has_error, 0);
}

static void test_tokenize_ident_escapes(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];
    uint8_t value[32];
    size_t length = 0u;

    /* `\63 olor` is the identifier `color`: six hex digits at most, then one
     * whitespace that belongs to the escape. */
    css_lex_text(&source, "\\63 olor", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 8u);
    ROBO_CHECK_EQ_INT(log.tokens[0].text.length, 8u);
    ROBO_CHECK_EQ_INT(robo_css_token_has_escapes(&log.tokens[0]), 1);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 5u);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "color"));
    ROBO_CHECK(robo_css_token_value_equals_ascii_ci(&log.tokens[0], "COLOR"));
    /* Raw bytes still say what was written. */
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "\\63 olor"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 5u);
    ROBO_CHECK_EQ_INT(memcmp(value, "color", 5u), 0);

    /* The escape is legal anywhere in a name, including at the end and at the
     * start, and `\3` without a following hex digit is the single character. */
    css_lex_text(&source, "col\\6fr", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "color"));

    css_lex_text(&source, "\\41", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "A"));

    /* An escaped quote or space is an escaped character like any other. */
    css_lex_text(&source, "a\\.b", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a.b"));

    css_lex_text(&source, "a\\ b", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a b"));

    /* Out-of-range hex escapes are U+FFFD, which is three bytes and not ASCII. */
    css_lex_text(&source, "\\0", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 3u);
    ROBO_CHECK(!robo_css_token_value_equals_ascii(&log.tokens[0], "x"));

    css_lex_text(&source, "\\D800", &log);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 3u);

    css_lex_text(&source, "\\110000", &log);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 3u);

    /* A multi-byte character escaped literally is passed through, not decoded
     * and re-encoded. */
    css_lex_text(&source, "caf\\\xC3\xA9", &log);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 5u);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "caf\xC3\xA9"));
}

static void test_tokenize_escaped_newline_is_not_an_escape(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* A backslash before a newline is not an escape: the name ends before the
     * backslash, the backslash is a delimiter, and the newline is its own
     * whitespace token. The specification calls it a parse error, so the stream
     * records one — and the stream keeps going. */
    css_lex_text(&source, "a\\\nb", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|delim|ws|ident|eof");
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK_EQ_INT(log.error.position.offset, 1u);

    /* A backslash at the end of input: a delimiter, and the same note. */
    css_lex_text(&source, "a\\", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|delim|eof");
    ROBO_CHECK_EQ_INT(log.has_error, 1);

    /* An escaped CR is a newline too, and swallows nothing. */
    css_lex_text(&source, "a\\\r\nb", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|delim|ws|ident|eof");
}

static void test_tokenize_function_tokens(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];
    uint8_t value[16];
    size_t length = 0u;

    css_lex_text(&source, "rgb(", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "function|eof");
    /* The opening paren is part of the token and not of its value. */
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 4u);
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "rgb("));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "rgb"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 3u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 3u);
    ROBO_CHECK_EQ_INT(memcmp(value, "rgb", 3u), 0);

    css_lex_text(&source, "calc( var( translate( scale3d(", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "function|ws|function|ws|function|ws|function|eof");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "calc"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[2], "var"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[6], "scale3d"));
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* Whitespace between the name and the paren makes them two tokens: only an
     * immediately following `(` makes a function. */
    css_lex_text(&source, "foo (x)", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|ws|left-paren|ident|right-paren|eof");

    /* An escaped function name is a function too, and compares resolved. */
    css_lex_text(&source, "r\\67 b(", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_FUNCTION);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "rgb"));

    /* A function token is not a block start in this vocabulary: the record has
     * no pairing for it, which is 9.3's business, not the tokenizer's. */
    ROBO_CHECK_EQ_INT(robo_css_token_type_is_block_start(ROBO_CSS_TOKEN_FUNCTION), 0);
    ROBO_CHECK_EQ_INT(robo_css_token_type_is_block_start(ROBO_CSS_TOKEN_LEFT_PAREN), 1);
}

static void test_tokenize_at_keywords(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    css_lex_text(&source, "@media @font-face @supports @import", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "at-keyword|ws|at-keyword|ws|at-keyword|ws|at-keyword|eof");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "media"));
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "@media"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[2], "font-face"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[4], "supports"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[6], "import"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 5u);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* `@media` and `media` are different tokens, which is the whole point. */
    css_lex_text(&source, "media", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
    css_lex_text(&source, "@media", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_AT_KEYWORD);

    /* A `@` that starts no name is a delimiter. */
    css_lex_text(&source, "@", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DELIM);
    css_lex_text(&source, "@ 1", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_DELIM);
    css_lex_text(&source, "@$", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|delim|eof");
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    /* `@-x` and `@--x` are at-keywords: `-` starts an identifier here too. */
    css_lex_text(&source, "@-x", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_AT_KEYWORD);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "-x"));
    css_lex_text(&source, "@--x", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_AT_KEYWORD);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "--x"));

    /* An escaped at-keyword name resolves. */
    css_lex_text(&source, "@\\6d edia", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_AT_KEYWORD);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "media"));
}

static void test_tokenize_hash_tokens(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* An identifier-like hash: usable as an id selector. */
    css_lex_text(&source, "#header", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "hash|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].hash, ROBO_CSS_HASH_TYPE_ID);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "header"));
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "#header"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 6u);

    /* A digit-led hash is unrestricted: a colour, never an id selector. */
    css_lex_text(&source, "#123", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_HASH);
    ROBO_CHECK_EQ_INT(log.tokens[0].hash, ROBO_CSS_HASH_TYPE_UNRESTRICTED);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "123"));

    /* `-` counts as identifier-like, which is what makes `#-x` an id. */
    css_lex_text(&source, "#-x", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].hash, ROBO_CSS_HASH_TYPE_ID);
    css_lex_text(&source, "#--x", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].hash, ROBO_CSS_HASH_TYPE_ID);
    css_lex_text(&source, "#-", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].hash, ROBO_CSS_HASH_TYPE_UNRESTRICTED);

    /* An escaped name is identifier-like, so it is an id hash. */
    css_lex_text(&source, "#\\31 23x", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].hash, ROBO_CSS_HASH_TYPE_ID);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "123x"));

    /* A `#` that starts nothing is a delimiter. */
    css_lex_text(&source, "#", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|eof");
    css_lex_text(&source, "# x", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|ws|ident|eof");
    css_lex_text(&source, "a#b", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|hash|eof");
}

static void test_tokenize_url_unquoted(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];
    uint8_t value[32];
    size_t length = 0u;

    css_lex_text(&source, "url(image.png)", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "url|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    /* The range covers `url(...)` whole, closing paren included, which is what
     * makes the token's text identical to the bytes the caller wrote. */
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 14u);
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "url(image.png)"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "image.png"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 9u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value, sizeof(value),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 9u);
    ROBO_CHECK_EQ_INT(memcmp(value, "image.png", 9u), 0);
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    /* The case-insensitivity is CSS's, and it applies to `url` too. */
    css_lex_text(&source, "URL(a)", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_URL);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a"));

    css_lex_text(&source, "\\75 rl(a)", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_URL);

    /* An empty url is a url with an empty value. */
    css_lex_text(&source, "url()", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_URL);
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 0u);
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    /* Leading and trailing whitespace is inside the token's range and outside
     * its value, which is what the ranges-tile-the-source invariant needs. */
    css_lex_text(&source, "url(   foo.png   )", &log);
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(log.tokens[0].type), "url");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "foo.png"));
    ROBO_CHECK_EQ_INT(log.tokens[0].range.end, 18u);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* An escaped space is content, not a separator. */
    css_lex_text(&source, "url(a\\ b.png)", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_URL);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a b.png"));

    /* End of input closes nothing but is still a url: the specification says so
     * explicitly, and `closed` is where that fact lives. */
    css_lex_text(&source, "url(foo", &log);
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(log.tokens[0].type), "url");
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "foo"));
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);

    css_lex_text(&source, "url(foo ", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_URL);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "foo"));
}

static void test_tokenize_url_quoted_is_a_function(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* `url("x.png")` is a function token, a string and a `)` — the grammar's
     * two-argument form, which is how the specification distinguishes it. There
     * is no url token for the quoted spelling at all. */
    css_lex_text(&source, "url(\"x.png\")", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "function|string|right-paren|eof");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "url"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[1], "x.png"));
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    css_lex_text(&source, "url('x.png')", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "function|string|right-paren|eof");

    /* The documented divergence from the specification's literal text: the
     * whitespace before a quoted argument stays in the stream as its own token,
     * so that every byte is covered by exactly one token. Blink collapses it;
     * the specification keeps it; we keep it and say so here. */
    css_lex_text(&source, "url( \"x.png\")", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "function|ws|string|right-paren|eof");
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* A *long* run of whitespace is still a function: only the first
     * non-whitespace character decides. */
    css_lex_text(&source, "url(\t  'x')", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "function|ws|string|right-paren|eof");
}

static void test_tokenize_url_bad(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* An illegal character in an unquoted url makes a bad-url, and its remnants
     * are consumed so the rest of the sheet does not become garbage. */
    css_lex_text(&source, "url(a b) c", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "bad-url|ws|ident|eof");
    ROBO_CHECK_EQ_INT(log.tokens[0].closed, 0);
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    /* The delimiter characters that end an unquoted url early. */
    css_lex_text(&source, "url(a\"b)", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_URL);
    css_lex_text(&source, "url(a'b)", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_URL);
    css_lex_text(&source, "url(a(b)", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_URL);
    css_lex_text(&source, "url(a\\", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_URL);

    /* A bad url still ends at its `)` or at the end of input, so the token
     * after it is the token after it. */
    css_lex_text(&source, "url(a b)url(c)", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "bad-url|url|eof");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[1], "c"));

    /* Whitespace is only legal before the closing paren. `url(a b` runs off the
     * end: the specification's remnant loop stops at end of input, so this is a
     * bad url and not a url. */
    css_lex_text(&source, "url(a b", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_BAD_URL);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
}

static void test_tokenize_value_copy_capacity(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    uint8_t small[2];
    uint8_t exact[5];
    size_t length = 0u;

    css_lex_text(&source, "color", &log);
    /* Too small: told so, and told what was needed, without writing anything. */
    small[0] = 0xAAu;
    small[1] = 0xAAu;
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], small, sizeof(small),
                                               &length, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(length, 5u);
    ROBO_CHECK_EQ_INT(small[0], 0xAAu);
    ROBO_CHECK_EQ_INT(small[1], 0xAAu);

    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], exact, sizeof(exact),
                                               &length, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 5u);
    ROBO_CHECK_EQ_INT(memcmp(exact, "color", 5u), 0);

    /* Exactly the value's length fits: the copy is not NUL-terminated and does
     * not need a byte it was not given. */
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 5u);

    /* A NULL token, output or length is a caller mistake, and the length
     * output is written before any capacity check. */
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(NULL, exact, sizeof(exact), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], NULL, sizeof(exact),
                                               &length, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], exact, sizeof(exact),
                                               NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_token_value_equals_ascii(NULL, "x"), 0);
    ROBO_CHECK_EQ_INT(robo_css_token_value_equals_ascii_ci(NULL, "x"), 0);
    ROBO_CHECK_EQ_INT(robo_css_token_value_equals_ascii(&log.tokens[0], NULL), 0);
    ROBO_CHECK_EQ_INT(robo_css_token_has_escapes(NULL), 0);
}

static void test_tokenize_value_for_every_type(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;

    /* The value rule for each token type, in one place: the delimiters a type
     * carries are stripped and what is left is the value. This is the table in
     * RoboCSSToken.h asserted rather than described. */
    css_lex_text(&source, "a", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a"));

    css_lex_text(&source, "@a", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a"));

    css_lex_text(&source, "#a", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a"));

    css_lex_text(&source, "f(", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "f"));

    css_lex_text(&source, "\"a\"", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a"));

    css_lex_text(&source, "'a'", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a"));

    css_lex_text(&source, "url(a)", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "a"));

    /* A dimension's value is its numeric part; the unit is a separate slice. */
    css_lex_text(&source, "10px", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "10"));
    ROBO_CHECK(robo_css_token_equals_ascii(&log.tokens[0], "10px"));

    /* A number's value is its own text. */
    css_lex_text(&source, "-1.5e2", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "-1.5e2"));

    /* A delimiter's value is the delimiter; a comment's is the whole comment —
     * a comment is the one token whose value is deliberately not stripped. */
    css_lex_text(&source, "+", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "+"));

    css_lex_text(&source, "/* c */", &log);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "/* c */"));

    /* EOF has no text at all, so its value is absent rather than the empty
     * string: `text` being absent is the token's own distinction. */
    css_lex_text(&source, "", &log);
    ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(robo_css_token_value_length(&log.tokens[0]), 0u);
    ROBO_CHECK(robo_css_slice_is_absent(robo_css_token_text(&log.tokens[0])));
}

void robo_test_register_css_tokenize_ident(void);

void robo_test_register_css_tokenize_ident(void)
{
    robo_test_register("css.tokenize.ident_spellings", test_tokenize_ident_spellings);
    robo_test_register("css.tokenize.ident_unicode", test_tokenize_ident_unicode);
    robo_test_register("css.tokenize.ident_escapes", test_tokenize_ident_escapes);
    robo_test_register("css.tokenize.escaped_newline_is_not_an_escape",
                       test_tokenize_escaped_newline_is_not_an_escape);
    robo_test_register("css.tokenize.function_tokens", test_tokenize_function_tokens);
    robo_test_register("css.tokenize.at_keywords", test_tokenize_at_keywords);
    robo_test_register("css.tokenize.hash_tokens", test_tokenize_hash_tokens);
    robo_test_register("css.tokenize.url_unquoted", test_tokenize_url_unquoted);
    robo_test_register("css.tokenize.url_quoted_is_a_function",
                       test_tokenize_url_quoted_is_a_function);
    robo_test_register("css.tokenize.url_bad", test_tokenize_url_bad);
    robo_test_register("css.tokenize.value_copy_capacity",
                       test_tokenize_value_copy_capacity);
    robo_test_register("css.tokenize.value_for_every_type",
                       test_tokenize_value_for_every_type);
}
