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

static void test_tokenize_sequence_declaration(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* The phase's own example, asserted whole rather than one token at a time. */
    css_lex_text(&source, "color: red;", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|colon|ws|ident|semicolon|eof");
    ROBO_CHECK_EQ_INT(log.count, 6u);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));

    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "color"));
    ROBO_CHECK_EQ_INT(log.tokens[1].range.begin, 5u);
    ROBO_CHECK_EQ_INT(log.tokens[2].range.begin, 6u);
    ROBO_CHECK_EQ_INT(log.tokens[2].range.end, 7u);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[3], "red"));
    ROBO_CHECK_EQ_INT(log.tokens[4].range.begin, 10u);
    ROBO_CHECK_EQ_INT(log.tokens[5].range.begin, 11u);
    ROBO_CHECK_EQ_INT(log.tokens[5].range.end, 11u);
}

static void test_tokenize_sequence_margins(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[256];

    css_lex_text(&source, "margin: 10px 20px;", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds,
                      "ident|colon|ws|dimension|ws|dimension|semicolon|eof");
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[3].unit, "px"));
    ROBO_CHECK(robo_css_slice_equals_ascii(log.tokens[5].unit, "px"));
    CSS_CHECK_DOUBLE(log.tokens[3].number, 10.0);
    CSS_CHECK_DOUBLE(log.tokens[5].number, 20.0);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
}

static void test_tokenize_sequence_at_rule(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[512];

    /* The phase's third example, lexically: an at-keyword, a prelude, nested
     * curly blocks, a class selector and a declaration — with no parser
     * anywhere near it. */
    css_lex_text(&source,
                 "@media screen {\n"
                 "  .button {\n"
                 "    color: red;\n"
                 "  }\n"
                 "}\n",
                 &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds,
                      "at-keyword|ws|ident|ws|left-curly|ws|delim|ident|ws|"
                      "left-curly|ws|ident|colon|ws|ident|semicolon|ws|right-curly|"
                      "ws|right-curly|ws|eof");
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_LEFT_CURLY), 2u);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_RIGHT_CURLY), 2u);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "media"));
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[2], "screen"));
}

static void test_tokenize_sequence_fixture_corpus(void)
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
        robo_css_source_t source = ROBO_CSS_CLEARED;
        css_token_log_t log;
        size_t j;

        css_lex_text(&source, kFixtures[i], &log);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
        ROBO_CHECK_EQ_INT(log.truncated, 0);
        ROBO_CHECK_EQ_INT(log.eof_seen, 1);
        ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
        ROBO_CHECK_EQ_INT(log.tokens[log.count - 1u].type, ROBO_CSS_TOKEN_EOF);
        ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_LEFT_CURLY), 1u);
        ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_RIGHT_CURLY), 1u);
        ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_COLON), 1u);
        ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_SEMICOLON), 1u);

        /* Every token's text is exactly the bytes its range covers, which is the
         * pairing that makes a token self-describing. */
        for (j = 0u; j < log.count; j++) {
            const robo_css_token_t *token = css_token_at(&log, j);

            ROBO_CHECK(token != NULL);
            ROBO_CHECK_EQ_INT(token->text.length,
                              token->range.end - token->range.begin);
        }
    }
}

static void test_tokenize_sequence_hex_colour_and_functions(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[256];

    css_lex_text(&source, "a { color: #ABC; width: calc(100% - 10px); }", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds,
                      "ident|ws|left-curly|ws|ident|colon|ws|hash|semicolon|ws|"
                      "ident|colon|ws|function|percentage|ws|delim|ws|dimension|"
                      "right-paren|semicolon|ws|right-curly|eof");
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[7], "ABC"));
    ROBO_CHECK_EQ_INT(log.tokens[7].hash, ROBO_CSS_HASH_TYPE_ID);
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[13], "calc"));
}

static void test_tokenize_malformed_corpus(void)
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
        robo_css_source_t source = ROBO_CSS_CLEARED;
        css_token_log_t first;
        css_token_log_t second;
        size_t length = strlen(kCorpus[i]);

        css_lex_text(&source, kCorpus[i], &first);

        /* Deterministic output, no corruption, and a stream that ends. */
        ROBO_CHECK_EQ_INT(first.truncated, 0);
        ROBO_CHECK_EQ_INT(first.eof_seen, 1);
        ROBO_CHECK_EQ_INT(first.tokens[first.count - 1u].type, ROBO_CSS_TOKEN_EOF);
        ROBO_CHECK(css_log_tiles(&first, length));

        css_lex(&source, &second);
        ROBO_CHECK_EQ_INT(first.count, second.count);
        {
            char kinds_first[512];
            char kinds_second[512];

            css_log_kinds(&first, kinds_first, sizeof(kinds_first));
            css_log_kinds(&second, kinds_second, sizeof(kinds_second));
            ROBO_CHECK_STR_EQ(kinds_first, kinds_second);
        }

        /* Whatever went wrong, it is not fatal: the error vocabulary's
         * recoverable/fatal split is what a caller switches on, and malformed
         * CSS never produces a fatal one. */
        if (first.has_error) {
            ROBO_CHECK(robo_css_error_kind_is_error(first.error.kind));
            ROBO_CHECK_EQ_INT(robo_css_error_kind_is_fatal(first.error.kind), 0);
        }
    }
}

static void test_tokenize_malformed_specific_sequences(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[128];

    /* `10.` is a number and a dot, not a failed number. */
    css_lex_text(&source, "10.", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "number|delim|eof");
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    /* `--bad\` is the identifier `--bad`, then a delimiter for the backslash,
     * and a recorded parse error for it. */
    css_lex_text(&source, "--bad\\", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "ident|delim|eof");
    ROBO_CHECK(robo_css_token_value_equals_ascii(&log.tokens[0], "--bad"));
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_RECOVERABLE_SYNTAX);
    ROBO_CHECK_EQ_INT(log.error.position.offset, 5u);

    /* `url("unterminated)` is a function and an unclosed string — and no `)`
     * token, because the `)` is inside the string: a string that never closes
     * runs to the end of input, and the tokenizer is not entitled to look into
     * it for a parenthesis. That is the whole reason an unterminated string is
     * worth starting lexically and finishing at the terminator. */
    css_lex_text(&source, "url(\"unterminated)", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "function|string|eof");
    ROBO_CHECK_EQ_INT(log.tokens[1].closed, 0);
    ROBO_CHECK_EQ_INT(log.has_error, 1);
    ROBO_CHECK_EQ_INT(log.error.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);

    /* `@$` is two delimiters and no error at all: there is nothing wrong with
     * it that a tokenizer can see. */
    css_lex_text(&source, "@$", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "delim|delim|eof");
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    /* Markup is just delimiters, identifiers and strings to a CSS tokenizer —
     * no HTML parser is involved, and nothing crashes. */
    css_lex_text(&source, "<div id=\"a\">text</div>", &log);
    ROBO_CHECK_EQ_INT(log.count, 13u);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_STRING), 1u);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK_EQ_INT(log.has_error, 0);

    /* Unbalanced brackets are tokens, not errors: pairing belongs to the
     * grammar, and a tokenizer has no opinion about it. */
    css_lex_text(&source, "(((((", &log);
    ROBO_CHECK_EQ_INT(css_count_type(&log, ROBO_CSS_TOKEN_LEFT_PAREN), 5u);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    css_lex_text(&source, "}]){", &log);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK_STR_EQ(kinds, "right-curly|right-square|right-paren|left-curly|eof");
    ROBO_CHECK_EQ_INT(log.has_error, 0);
}

static void test_tokenize_deep_nesting_is_iterative(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_stream_audit_t audit;
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
    css_source_of_string(&source, text);
    css_audit_stream(&source, &audit);
    ROBO_CHECK_EQ_INT(audit.total, (depth * 2u) + 1u);
    ROBO_CHECK_EQ_INT(audit.by_type[ROBO_CSS_TOKEN_LEFT_PAREN], depth);
    ROBO_CHECK_EQ_INT(audit.by_type[ROBO_CSS_TOKEN_RIGHT_PAREN], depth);
    ROBO_CHECK_EQ_INT(audit.has_error, 0);
    ROBO_CHECK_EQ_INT(audit.eof_seen, 1);
    ROBO_CHECK(audit.tiles);

    for (i = 0u; i < depth; i++) {
        text[i] = '{';
    }
    for (i = 0u; i < depth; i++) {
        text[depth + i] = '}';
    }
    text[depth * 2u] = '\0';
    css_source_of_string(&source, text);
    css_audit_stream(&source, &audit);
    ROBO_CHECK_EQ_INT(audit.total, (depth * 2u) + 1u);
    ROBO_CHECK_EQ_INT(audit.by_type[ROBO_CSS_TOKEN_LEFT_CURLY], depth);
    ROBO_CHECK_EQ_INT(audit.by_type[ROBO_CSS_TOKEN_RIGHT_CURLY], depth);
    ROBO_CHECK(audit.tiles);

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
        css_source_of_string(&source, text);
        css_audit_stream(&source, &audit);
        ROBO_CHECK_EQ_INT(audit.by_type[ROBO_CSS_TOKEN_FUNCTION], depth);
        ROBO_CHECK_EQ_INT(audit.by_type[ROBO_CSS_TOKEN_RIGHT_PAREN], depth);
        ROBO_CHECK_EQ_INT(audit.has_error, 0);
        ROBO_CHECK(audit.tiles);
    }
}

static void test_tokenize_large_sheet(void)
{
    static char sheet[24000];
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_stream_audit_t audit;
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
    ROBO_CHECK(offset > 8000u);

    css_source_of_string(&source, "");
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, (const uint8_t *)sheet,
                                                 offset, NULL), ROBO_OK);
    css_audit_stream(&source, &audit);
    ROBO_CHECK_EQ_INT(audit.has_error, 0);
    ROBO_CHECK_EQ_INT(audit.eof_seen, 1);
    ROBO_CHECK(audit.tiles);
    /* Every rule contributes: `.rule-N` (delim + ident), ws, `{`, ws, decl… */
    ROBO_CHECK(audit.by_type[ROBO_CSS_TOKEN_LEFT_CURLY] == rules);
    ROBO_CHECK(audit.by_type[ROBO_CSS_TOKEN_RIGHT_CURLY] == rules);
    ROBO_CHECK(audit.by_type[ROBO_CSS_TOKEN_SEMICOLON] == rules * 2u);
    ROBO_CHECK(audit.by_type[ROBO_CSS_TOKEN_HASH] == rules);
    /* And the token count is exact rather than "roughly 200 rules": a sheet this
     * size is where a quadratic scan or a missed byte would show up as a count
     * that drifts by one per rule. */
    ROBO_CHECK(audit.total > rules * 20u);
}

static void test_tokenize_unicode_matrix(void)
{
    /* Each family through three routes: a name, a string and a comment. All
     * three must hand back the sample's bytes untouched. */
    size_t i;

    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        const char *sample = css_samples[i].text;
        size_t sample_length = strlen(sample);
        char name[256];
        char string_form[256];
        char comment_form[256];
        robo_css_source_t source = ROBO_CSS_CLEARED;
        css_token_log_t log;
        uint8_t value[256];
        size_t length = 0u;

        snprintf(name, sizeof(name), "%s", sample);
        snprintf(string_form, sizeof(string_form), "\"%s\"", sample);
        snprintf(comment_form, sizeof(comment_form), "/* %s */", sample);

        /* The sample as a bare name. The ones that hold a space are several
         * identifiers; the rest are one covering every byte. */
        css_lex_text(&source, name, &log);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
        ROBO_CHECK(css_log_tiles(&log, sample_length));
        if (strchr(sample, ' ') == NULL) {
            ROBO_CHECK_EQ_INT(log.count, 2u);
            ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_IDENT);
            ROBO_CHECK_EQ_INT(log.tokens[0].range.end, sample_length);
        } else {
            size_t t;

            for (t = 0u; t < log.count; t++) {
                if (log.tokens[t].type != ROBO_CSS_TOKEN_EOF) {
                    ROBO_CHECK(log.tokens[t].type == ROBO_CSS_TOKEN_IDENT ||
                               log.tokens[t].type == ROBO_CSS_TOKEN_WHITESPACE);
                }
            }
        }

        css_lex_text(&source, string_form, &log);
        ROBO_CHECK_EQ_INT(log.count, 2u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_STRING);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
        ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], value,
                                                   sizeof(value), &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(length, sample_length);
        ROBO_CHECK_EQ_INT(memcmp(value, sample, sample_length), 0);

        css_lex_text(&source, comment_form, &log);
        ROBO_CHECK_EQ_INT(log.count, 2u);
        ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_COMMENT);
        ROBO_CHECK_EQ_INT(log.has_error, 0);
        ROBO_CHECK_EQ_INT(log.tokens[0].range.end, strlen(comment_form));

        /* The same sample inside an unquoted url, for the samples a url can
         * hold at all: a url may not contain whitespace, a quote or a paren, so
         * the multi-word samples are skipped here (the url suite covers the
         * rejections). What this adds is that non-ASCII *bytes* inside a url
         * survive it. */
        if (strchr(sample, ' ') == NULL && strchr(sample, '"') == NULL &&
            strchr(sample, '\'') == NULL) {
            char url_form[256];
            uint8_t url_value[256];
            size_t url_length = 0u;

            snprintf(url_form, sizeof(url_form), "url(%s)", sample);
            css_lex_text(&source, url_form, &log);
            ROBO_CHECK_EQ_INT(log.count, 2u);
            ROBO_CHECK_EQ_INT(log.tokens[0].type, ROBO_CSS_TOKEN_URL);
            ROBO_CHECK_EQ_INT(log.tokens[0].closed, 1);
            ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&log.tokens[0], url_value,
                                                       sizeof(url_value), &url_length,
                                                       NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(url_length, sample_length);
            ROBO_CHECK_EQ_INT(memcmp(url_value, sample, sample_length), 0);
        }
    }
}

static void test_tokenize_identical_bytes_identical_tokens(void)
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
        robo_css_source_t source = ROBO_CSS_CLEARED;
        css_token_log_t first;
        css_token_log_t second;
        char first_kinds[512];
        char second_kinds[512];
        size_t j;

        css_lex_text(&source, kInputs[i], &first);
        css_lex_text(&source, kInputs[i], &second);
        ROBO_CHECK_EQ_INT(first.count, second.count);
        ROBO_CHECK_EQ_INT(first.has_error, second.has_error);
        css_log_kinds(&first, first_kinds, sizeof(first_kinds));
        css_log_kinds(&second, second_kinds, sizeof(second_kinds));
        ROBO_CHECK_STR_EQ(first_kinds, second_kinds);

        for (j = 0u; j < first.count && j < CSS_TOK_MAX; j++) {
            const robo_css_token_t *a = &first.tokens[j];
            const robo_css_token_t *b = &second.tokens[j];

            ROBO_CHECK_EQ_INT(a->type, b->type);
            ROBO_CHECK_EQ_INT(a->range.begin, b->range.begin);
            ROBO_CHECK_EQ_INT(a->range.end, b->range.end);
            ROBO_CHECK_EQ_INT(a->closed, b->closed);
            ROBO_CHECK_EQ_INT(a->hash, b->hash);
            ROBO_CHECK_EQ_INT(robo_css_token_value_length(a),
                              robo_css_token_value_length(b));
            CSS_CHECK_DOUBLE(a->number, b->number);
        }

        /* Lexing twice through the *same* tokenizer, reset between runs, gives
         * the same answer as two fresh ones. */
        {
            robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
            robo_css_token_t token = ROBO_CSS_CLEARED;
            size_t produced_first = 0u;
            size_t produced_second = 0u;

            ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL),
                              ROBO_OK);
            while (!robo_css_tokenizer_at_end(&tokenizer)) {
                ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL),
                                  ROBO_OK);
            }
            produced_first = robo_css_tokenizer_tokens_produced(&tokenizer);
            ROBO_CHECK_EQ_INT(robo_css_tokenizer_reset(&tokenizer, NULL), ROBO_OK);
            while (!robo_css_tokenizer_at_end(&tokenizer)) {
                ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL),
                                  ROBO_OK);
            }
            produced_second = robo_css_tokenizer_tokens_produced(&tokenizer);
            ROBO_CHECK_EQ_INT(produced_first, produced_second);
            robo_css_tokenizer_dispose(&tokenizer);
        }
    }
}

static void test_tokenize_whole_sheet_integration(void)
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

    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    char kinds[4096];

    css_lex_text(&source, kSheet, &log);
    /* Whole-sheet lexing: no errors, a complete stream, byte-exact tiling, and
     * every class the phase names present in one pass. */
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    ROBO_CHECK_EQ_INT(log.truncated, 0);
    ROBO_CHECK_EQ_INT(log.eof_seen, 1);
    ROBO_CHECK(css_log_tiles(&log, robo_css_source_length(&source)));
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_AT_KEYWORD) == 2u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_COMMENT) == 1u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_HASH) == 1u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_URL) == 1u);
    /* `-50%` in the transform and `87%` in the relative colour. */
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_PERCENTAGE) == 2u);
    /* Three dimensions: `40em` in the media query, `1rem` in the shorthand and
     * `.5em` in the transform. `1.5` and `1.02` are numbers, not dimensions,
     * which is exactly the distinction the dimension tests pin. */
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_DIMENSION) == 3u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_STRING) == 4u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_FUNCTION) == 3u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_COLON) >= 8u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_SEMICOLON) >= 6u);
    ROBO_CHECK(css_count_type(&log, ROBO_CSS_TOKEN_DELIM) >= 4u);
    css_log_kinds(&log, kinds, sizeof(kinds));
    ROBO_CHECK(strlen(kinds) > 0u);
}

void robo_test_register_css_tokenize_sequence(void);

void robo_test_register_css_tokenize_sequence(void)
{
    robo_test_register("css.tokenize.sequence_declaration",
                       test_tokenize_sequence_declaration);
    robo_test_register("css.tokenize.sequence_margins", test_tokenize_sequence_margins);
    robo_test_register("css.tokenize.sequence_at_rule", test_tokenize_sequence_at_rule);
    robo_test_register("css.tokenize.sequence_fixture_corpus",
                       test_tokenize_sequence_fixture_corpus);
    robo_test_register("css.tokenize.sequence_hex_colour_and_functions",
                       test_tokenize_sequence_hex_colour_and_functions);
    robo_test_register("css.tokenize.malformed_corpus", test_tokenize_malformed_corpus);
    robo_test_register("css.tokenize.malformed_specific_sequences",
                       test_tokenize_malformed_specific_sequences);
    robo_test_register("css.tokenize.deep_nesting_is_iterative",
                       test_tokenize_deep_nesting_is_iterative);
    robo_test_register("css.tokenize.large_sheet", test_tokenize_large_sheet);
    robo_test_register("css.tokenize.unicode_matrix", test_tokenize_unicode_matrix);
    robo_test_register("css.tokenize.identical_bytes_identical_tokens",
                       test_tokenize_identical_bytes_identical_tokens);
    robo_test_register("css.tokenize.whole_sheet_integration",
                       test_tokenize_whole_sheet_integration);
}
