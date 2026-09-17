/*
 * RoboCSS tokenizer stream suite (Phase 9.2): the machine around the tokens.
 *
 * The other four tokenizer suites assert what a token *is*. This one asserts
 * how the stream behaves — peek, skip, reset, EOF, the record of the first
 * problem, and the two properties a caller relies on and cannot see: that
 * lexing allocates nothing at all, and that a token stays valid after the
 * tokenizer that produced it is gone.
 */

#include "RoboCssTestSupport.h"

static void test_tokenize_stream_eof_for_empty_input(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_position_t position;

    css_source_of_string(&source, "");
    ROBO_CHECK_EQ_INT(robo_css_source_length(&source), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(&tokenizer), 1);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_remaining(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 0u);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(token.range.begin, 0u);
    ROBO_CHECK_EQ_INT(token.range.end, 0u);
    ROBO_CHECK(robo_css_slice_is_empty(token.text));
    ROBO_CHECK(robo_css_token_is_terminator(&token));
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 1u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(&tokenizer), 0u);

    /* EOF is a value, not a state: asking again answers the same thing at the
     * same place, forever, and never moves the cursor. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_position(&tokenizer, &position, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.offset, 0u);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 1u);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_stream_peek_is_stable(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t first = ROBO_CSS_CLEARED;
    robo_css_token_t again = ROBO_CSS_CLEARED;
    robo_css_token_t consumed = ROBO_CSS_CLEARED;
    robo_css_source_t empty = ROBO_CSS_CLEARED;

    css_source_of_string(&source, "ab");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(&tokenizer), 0);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(first.type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(tokenizer.offset, 2u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(&tokenizer), 1);
    /* Peeking is idempotent and does not advance the cursor past the token. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &again, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(again.type, first.type);
    ROBO_CHECK_EQ_INT(again.range.begin, first.range.begin);
    ROBO_CHECK_EQ_INT(again.range.end, first.range.end);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 0u);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &consumed, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(consumed.type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(consumed.range.end, 2u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(&tokenizer), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 1u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &consumed, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(consumed.type, ROBO_CSS_TOKEN_EOF);

    /* A disposed tokenizer peeks nothing and claims to be at the end. */
    robo_css_tokenizer_dispose(&tokenizer);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(&tokenizer), 1);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(&tokenizer), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &consumed, NULL),
                      ROBO_INVALID_ARGUMENT);

    css_source_of_string(&empty, "x");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &first, NULL), ROBO_OK);
    /* Re-initializing onto another source drops the lookahead rather than
     * handing back a token belonging to the previous source. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &empty, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(&tokenizer), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &again, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(again.type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(again.range.end, 1u);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_stream_skip_consumes(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;

    css_source_of_string(&source, "a b");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);

    /* skip with nothing peeked produces and discards. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 1u);
    ROBO_CHECK_EQ_INT(tokenizer.offset, 1u);

    /* skip with a peeked token drops the lookahead without re-reading. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_WHITESPACE);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(&tokenizer), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 2u);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_IDENT);
    /* Skipping the EOF token is legal and leaves the stream at rest. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(&tokenizer), 1);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(&tokenizer), 3u);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_stream_skip_trivia(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_source_t trivia_only = ROBO_CSS_CLEARED;

    css_source_of_string(&source, "  /* c */\n  a /* tail */");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip_trivia(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(token.range.begin, 12u);
    /* The trivia it stepped over is still counted as consumed. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 3u);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip_trivia(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    /* Repeated calls stay at EOF rather than spinning. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip_trivia(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    robo_css_tokenizer_dispose(&tokenizer);

    css_source_of_string(&trivia_only, "/**/ \t\n");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &trivia_only, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip_trivia(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(&tokenizer), 1);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_stream_reset_relexes(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    css_token_log_t first;
    css_token_log_t second;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    char kinds_first[256];
    char kinds_second[256];

    css_source_of_string(&source, "p { color: red; } /* c */");
    css_lex(&source, &first);
    ROBO_CHECK_EQ_INT(first.has_error, 0);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    while (!robo_css_tokenizer_at_end(&tokenizer)) {
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    }
    ROBO_CHECK(robo_css_tokenizer_tokens_produced(&tokenizer) > 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_reset(&tokenizer, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(&tokenizer), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(&tokenizer), 0);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, first.tokens[0].type);
    ROBO_CHECK_EQ_INT(token.range.end, first.tokens[0].range.end);

    /* Reset clears the recorded error too, so the same bytes report the same
     * first problem a second time. */
    {
        robo_css_source_t malformed = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        css_source_of_string(&malformed, "/* unterminated");
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &malformed, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 1);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_take_error(&tokenizer, &error), 1);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 0);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_reset(&tokenizer, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 0);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 1);
        robo_css_tokenizer_dispose(&tokenizer);
    }

    css_lex(&source, &second);
    css_log_kinds(&first, kinds_first, sizeof(kinds_first));
    css_log_kinds(&second, kinds_second, sizeof(kinds_second));
    ROBO_CHECK_STR_EQ(kinds_first, kinds_second);
    ROBO_CHECK_EQ_INT(first.count, second.count);
}

static void test_tokenize_stream_arguments_rejected(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_source_t unconfigured = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_position_t position;

    css_source_of_string(&source, "a");
    robo_css_source_init(&unconfigured);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(NULL, &source, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_reset(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(NULL, &token, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_peek(NULL, &token, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_skip_trivia(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_position(NULL, &position, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An EMPTY source is legal (a style sheet may be empty); a source nobody
     * configured is not — there are no bytes to be a cursor over. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &unconfigured, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    robo_css_tokenizer_dispose(&tokenizer);

    /* A disposed tokenizer answers INVALID_ARGUMENT rather than reading a NULL
     * source, and dispose is idempotent. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    robo_css_tokenizer_dispose(&tokenizer);
    robo_css_tokenizer_dispose(&tokenizer);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_source(&tokenizer), NULL);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_reset(&tokenizer, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_remaining(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 0);
    ROBO_CHECK(robo_css_tokenizer_error(&tokenizer) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_take_error(&tokenizer, NULL), 0);

    /* NULL accessors answer rather than crash. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_remaining(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(NULL), 1);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_peek(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(NULL), 0);
    ROBO_CHECK(robo_css_tokenizer_error(NULL) == NULL);
    ROBO_CHECK(robo_css_tokenizer_source(NULL) == NULL);
    robo_css_tokenizer_dispose(NULL);
}

static void test_tokenize_stream_first_error_wins(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_error_t first = ROBO_CSS_CLEARED;
    robo_css_error_t second = ROBO_CSS_CLEARED;
    robo_css_error_t taken = ROBO_CSS_CLEARED;
    robo_css_position_t position;

    css_source_of_string(&source, "a");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_fail(&tokenizer, ROBO_CSS_ERROR_KIND_NONE,
                                              ROBO_OK, "not a problem"), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 0);

    position = robo_css_position_at(0u);
    ROBO_CHECK_EQ_INT(robo_css_error_set(&first, ROBO_CSS_ERROR_KIND_INVALID_TOKEN,
                                         ROBO_INVALID_ARGUMENT, &position,
                                         "first"),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_error_set(&second, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF,
                                         ROBO_INVALID_ARGUMENT, &position,
                                         "second"),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_record_error(&tokenizer, &first), 1);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_record_error(&tokenizer, &second), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_fail(&tokenizer, ROBO_CSS_ERROR_KIND_MALFORMED_RULE,
                                              ROBO_INVALID_ARGUMENT, "third"), 0);
    ROBO_CHECK(robo_css_tokenizer_error(&tokenizer) != NULL);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_error(&tokenizer)->kind,
                      ROBO_CSS_ERROR_KIND_INVALID_TOKEN);
    ROBO_CHECK_STR_EQ(robo_css_tokenizer_error(&tokenizer)->message, "first");

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_take_error(&tokenizer, &taken), 1);
    ROBO_CHECK_STR_EQ(taken.message, "first");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_has_error(&tokenizer), 0);
    /* Taking the record hands it out; the next problem is then a first problem. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_record_error(&tokenizer, &second), 1);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_take_error(&tokenizer, &taken), 1);
    ROBO_CHECK_EQ_INT(taken.kind, ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF);

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_record_error(NULL, &first), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_record_error(&tokenizer, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_fail(NULL, ROBO_CSS_ERROR_KIND_INVALID_TOKEN,
                                              ROBO_INVALID_ARGUMENT, "x"), 0);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_stream_fail_positions_at_cursor(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    const robo_css_error_t *error;

    css_source_of_string(&source, "aa\nbb");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_WHITESPACE);
    /* The cursor is past the newline, on the second line. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(&tokenizer), 3u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_fail(&tokenizer, ROBO_CSS_ERROR_KIND_MALFORMED_RULE,
                                              ROBO_INVALID_ARGUMENT, "here"), 1);
    error = robo_css_tokenizer_error(&tokenizer);
    ROBO_CHECK(error != NULL);
    ROBO_CHECK_EQ_INT(error->position.offset, 3u);
    ROBO_CHECK_EQ_INT(error->position.line, 2u);
    ROBO_CHECK_EQ_INT(error->position.column, 1u);
    /* The recorded error never stops the stream. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_IDENT);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_stream_counts_and_remaining(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    size_t previous_remaining;
    size_t steps = 0u;

    css_source_of_string(&source, "a{b:c}");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &source, NULL), ROBO_OK);
    previous_remaining = robo_css_tokenizer_remaining(&tokenizer);
    ROBO_CHECK_EQ_INT(previous_remaining, 6u);

    while (robo_css_tokenizer_remaining(&tokenizer) > 0u) {
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
        ROBO_CHECK(robo_css_tokenizer_remaining(&tokenizer) < previous_remaining);
        previous_remaining = robo_css_tokenizer_remaining(&tokenizer);
        steps++;
        if (steps > 32u) {
            break;
        }
    }
    /* Six one-byte tokens — every byte here is one token, which is what makes
     * `remaining` strictly fall on each step — and then EOF, which is produced
     * on the next call rather than by the loop: EOF is not a byte. */
    ROBO_CHECK_EQ_INT(steps, 6u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(&tokenizer), 1);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_remaining(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 6u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_tokens_produced(&tokenizer), 7u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_remaining(&tokenizer), 0u);
    robo_css_tokenizer_dispose(&tokenizer);
}

static void test_tokenize_stream_allocates_nothing(void)
{
    static const char *kSheet =
        "/* a sheet */\n"
        "@media screen and (min-width: 40em) {\n"
        "  .card, article > .title { color: #123456; }\n"
        "}\n"
        "body {\n"
        "  margin: 0 auto; padding: 1.5rem 10px 0 2em;\n"
        "  content: \"a\\\"b\"; background: url(img/logo.png) no-repeat;\n"
        "}\n"
        "p::first-line { font: italic 12pt/1.4 'Caf\\xE9'; }\n";

    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    int we_enabled;
    size_t baseline;
    size_t after;

    css_source_of_string(&source, kSheet);
    we_enabled = css_allocator_ensure_enabled();
    baseline = robo_mem_debug_live_count();
    css_lex(&source, &log);
    after = robo_mem_debug_live_count();
    css_allocator_restore(we_enabled);

    ROBO_CHECK(log.count > 40u);
    ROBO_CHECK_EQ_INT(log.truncated, 0);
    ROBO_CHECK_EQ_INT(log.eof_seen, 1);
    ROBO_CHECK_EQ_INT(log.has_error, 0);
    /* The tokenizer is a value on the stack and owns nothing: a whole sheet
     * lexes with the allocator's live count untouched. */
    ROBO_CHECK_EQ_INT(baseline, after);
}

static void test_tokenize_stream_tokens_outlive_tokenizer(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    css_token_log_t log;
    const robo_css_token_t *token;
    uint8_t value[8];
    size_t length = 0u;

    css_source_of_string(&source, "color");
    /* css_lex disposes its tokenizer before returning. */
    css_lex(&source, &log);
    token = css_token_at(&log, 0u);
    ROBO_CHECK(token != NULL);
    ROBO_CHECK_EQ_INT(token->type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK(robo_css_token_value_equals_ascii(token, "color"));
    ROBO_CHECK_EQ_INT(robo_css_token_value_copy(token, value, sizeof(value), &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 5u);
    ROBO_CHECK_EQ_INT(memcmp(value, "color", 5u), 0);

    /* A token is a plain value: copying it copies the whole thing, and the copy
     * is independent of the original. */
    {
        robo_css_token_t copy = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_token_copy(token, &copy, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(copy.type, ROBO_CSS_TOKEN_IDENT);
        ROBO_CHECK(robo_css_token_value_equals_ascii(&copy, "color"));
        ROBO_CHECK_EQ_INT(copy.range.begin, token->range.begin);
        ROBO_CHECK_EQ_INT(copy.range.end, token->range.end);
    }
}

static void test_tokenize_stream_reuse_across_sources(void)
{
    robo_css_source_t one = ROBO_CSS_CLEARED;
    robo_css_source_t two = ROBO_CSS_CLEARED;
    robo_css_source_t copy = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    char kinds_a[128];
    char kinds_b[128];
    char kinds_c[128];
    css_token_log_t log_a;
    css_token_log_t log_b;
    css_token_log_t log_c;

    css_source_of_string(&one, "a:1;");
    css_source_of_string(&two, "b:2;");

    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &one, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(token.type), "ident");
    /* Re-initializing onto a second source is a supported reuse, not a leak. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, &two, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_offset(&tokenizer), 0u);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.text.bytes[0], (uint8_t)'b');
    robo_css_tokenizer_dispose(&tokenizer);

    /* The same bytes through an owned copy produce the same token stream. */
    ROBO_CHECK_EQ_INT(robo_css_source_copy(&copy, robo_css_source_bytes(&one),
                                           robo_css_source_length(&one), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_source_is_owned(&copy), 1);

    css_lex(&one, &log_a);
    css_lex(&copy, &log_b);
    css_lex(&two, &log_c);
    css_log_kinds(&log_a, kinds_a, sizeof(kinds_a));
    css_log_kinds(&log_b, kinds_b, sizeof(kinds_b));
    css_log_kinds(&log_c, kinds_c, sizeof(kinds_c));
    ROBO_CHECK_STR_EQ(kinds_a, kinds_b);
    ROBO_CHECK_EQ_INT(log_a.count, log_b.count);
    ROBO_CHECK_STR_EQ(kinds_a, "ident|colon|number|semicolon|eof");
    ROBO_CHECK_STR_EQ(kinds_c, "ident|colon|number|semicolon|eof");
    CSS_CHECK_DOUBLE(log_a.tokens[2].number, 1.0);
    CSS_CHECK_DOUBLE(log_c.tokens[2].number, 2.0);
    robo_css_source_dispose(&copy);
}

/**
 * The Phase 9.1 record-lifetime rule, restated for the records Phase 9.2 adds.
 *
 * A by-value record has no constructor to forget to call, which is what makes
 * `ROBO_CSS_CLEARED` the whole of the rule — and it is a rule with teeth: every
 * configuration entry point releases what it holds *before* it writes, so a
 * record that holds nothing is safe and a record that holds garbage is not.
 * What the suite can check is the safe half, and this is it: the cleared state
 * is the all-zero pattern, a used record returns to it, and the disposes are
 * idempotent. This is the property the tokenizing suites rely on when they
 * declare a tokenizer with the macro instead of initialising it.
 */
static void test_tokenize_records_cleared(void)
{
    robo_css_tokenizer_t declared = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t used = ROBO_CSS_CLEARED;
    robo_css_tokenizer_t zeroed;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_source_t copy = ROBO_CSS_CLEARED;

    memset(&zeroed, 0, sizeof(zeroed));

    /* The macro and the pattern are the same state, which is the claim the
     * macro's documentation makes and the one every "clear this record first"
     * call site depends on. */
    ROBO_CHECK_EQ_INT(memcmp(&declared, &zeroed, sizeof(zeroed)), 0);

    /* A record that has been used comes back to that same state, so a used
     * record and a fresh one are interchangeable as a starting point. */
    css_source_of_string(&source, "a{}");
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&used, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&used, &token, NULL), ROBO_OK);
    robo_css_tokenizer_dispose(&used);
    ROBO_CHECK_EQ_INT(memcmp(&used, &zeroed, sizeof(zeroed)), 0);
    /* Which is what makes a second dispose, and a dispose of something never
     * configured, ordinary no-ops rather than hazards. */
    robo_css_tokenizer_dispose(&used);
    robo_css_tokenizer_dispose(&declared);
    ROBO_CHECK_EQ_INT(memcmp(&declared, &zeroed, sizeof(zeroed)), 0);

    /* A disposed tokenizer answers for itself rather than reading a source it
     * no longer has. */
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&declared, &token, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_at_end(&declared), 1);

    /* The configuration paths release before they write, so each of them is
     * legal on a record holding nothing — including the one that allocates and
     * therefore has something to release on every call after the first. */
    ROBO_CHECK_EQ_INT(robo_css_source_copy(&copy, (const uint8_t *)"a{}", 3u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_source_is_owned(&copy), 1);
    ROBO_CHECK_EQ_INT(robo_css_source_copy(&copy, (const uint8_t *)"b{}", 3u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_source_length(&copy), 3u);
    ROBO_CHECK_EQ_INT(robo_css_source_set_string(&copy, "c{}", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_source_is_owned(&copy), 0);
    robo_css_source_dispose(&copy);
    robo_css_source_dispose(&copy);
    ROBO_CHECK_EQ_INT(copy.kind, ROBO_CSS_SOURCE_KIND_NONE);
}

void robo_test_register_css_tokenizer(void);

void robo_test_register_css_tokenizer(void)
{
    robo_test_register("css.tokenize.stream_empty_input",
                       test_tokenize_stream_eof_for_empty_input);
    robo_test_register("css.tokenize.stream_peek_is_stable",
                       test_tokenize_stream_peek_is_stable);
    robo_test_register("css.tokenize.stream_skip_consumes",
                       test_tokenize_stream_skip_consumes);
    robo_test_register("css.tokenize.stream_skip_trivia",
                       test_tokenize_stream_skip_trivia);
    robo_test_register("css.tokenize.stream_reset_relexes",
                       test_tokenize_stream_reset_relexes);
    robo_test_register("css.tokenize.stream_arguments_rejected",
                       test_tokenize_stream_arguments_rejected);
    robo_test_register("css.tokenize.stream_first_error_wins",
                       test_tokenize_stream_first_error_wins);
    robo_test_register("css.tokenize.stream_fail_positions_at_cursor",
                       test_tokenize_stream_fail_positions_at_cursor);
    robo_test_register("css.tokenize.stream_counts_and_remaining",
                       test_tokenize_stream_counts_and_remaining);
    robo_test_register("css.tokenize.stream_allocates_nothing",
                       test_tokenize_stream_allocates_nothing);
    robo_test_register("css.tokenize.stream_tokens_outlive_tokenizer",
                       test_tokenize_stream_tokens_outlive_tokenizer);
    robo_test_register("css.tokenize.stream_reuse_across_sources",
                       test_tokenize_stream_reuse_across_sources);
    robo_test_register("css.tokenize.records_cleared", test_tokenize_records_cleared);
}
