/*
 * RoboCSS Phase 9.1 token suite:
 * the token vocabulary, the bracket pairings, and binding a token to a range.
 *
 * Every test here asserts what the *record* is. None of them claims a byte was
 * classified into a token: that is the tokenizer's job in 9.2, and this build
 * has no tokenizer.
 */

#include "RoboCssTestSupport.h"

static void test_css_token_vocabulary(void)
{
    robo_css_token_type_t type;

    for (type = ROBO_CSS_TOKEN_NONE;
         type < (robo_css_token_type_t)ROBO_CSS_TOKEN_TYPE_COUNT;
         type = (robo_css_token_type_t)(type + 1)) {
        const char *name = robo_css_token_type_name(type);

        ROBO_CHECK(robo_css_token_type_is_known(type));
        ROBO_CHECK(name != NULL);
        ROBO_CHECK(strcmp(name, "unknown") != 0);
    }
    ROBO_CHECK(!robo_css_token_type_is_known((robo_css_token_type_t)99));
    ROBO_CHECK_STR_EQ(robo_css_token_type_name((robo_css_token_type_t)99), "unknown");

    /* The specific spellings other phases will compare against. */
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(ROBO_CSS_TOKEN_AT_KEYWORD), "at-keyword");
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(ROBO_CSS_TOKEN_DIMENSION),
                      "dimension");
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(ROBO_CSS_TOKEN_LEFT_CURLY),
                      "left-curly");
    ROBO_CHECK_STR_EQ(robo_css_token_type_name(ROBO_CSS_TOKEN_INVALID), "invalid");
}

static void test_css_token_classification(void)
{
    ROBO_CHECK(robo_css_token_type_is_numeric(ROBO_CSS_TOKEN_NUMBER));
    ROBO_CHECK(robo_css_token_type_is_numeric(ROBO_CSS_TOKEN_PERCENTAGE));
    ROBO_CHECK(robo_css_token_type_is_numeric(ROBO_CSS_TOKEN_DIMENSION));
    ROBO_CHECK(!robo_css_token_type_is_numeric(ROBO_CSS_TOKEN_IDENT));
    ROBO_CHECK(!robo_css_token_type_is_numeric(ROBO_CSS_TOKEN_STRING));

    /* Trivia is the two kinds a grammar steps over, and only those. */
    ROBO_CHECK(robo_css_token_type_is_ignorable(ROBO_CSS_TOKEN_WHITESPACE));
    ROBO_CHECK(robo_css_token_type_is_ignorable(ROBO_CSS_TOKEN_COMMENT));
    ROBO_CHECK(!robo_css_token_type_is_ignorable(ROBO_CSS_TOKEN_COLON));
    ROBO_CHECK(!robo_css_token_type_is_ignorable(ROBO_CSS_TOKEN_DELIM));

    ROBO_CHECK(robo_css_token_type_is_block_start(ROBO_CSS_TOKEN_LEFT_PAREN));
    ROBO_CHECK(robo_css_token_type_is_block_start(ROBO_CSS_TOKEN_LEFT_SQUARE));
    ROBO_CHECK(robo_css_token_type_is_block_start(ROBO_CSS_TOKEN_LEFT_CURLY));
    ROBO_CHECK(!robo_css_token_type_is_block_start(ROBO_CSS_TOKEN_COMMA));
    ROBO_CHECK(robo_css_token_type_is_block_end(ROBO_CSS_TOKEN_RIGHT_CURLY));
    ROBO_CHECK(!robo_css_token_type_is_block_end(ROBO_CSS_TOKEN_LEFT_CURLY));

    /* A token cannot be both, and the six brackets split three and three. */
    {
        int starts = 0;
        int ends = 0;
        robo_css_token_type_t type;

        for (type = ROBO_CSS_TOKEN_NONE;
             type < (robo_css_token_type_t)ROBO_CSS_TOKEN_TYPE_COUNT;
             type = (robo_css_token_type_t)(type + 1)) {
            int is_start = robo_css_token_type_is_block_start(type);
            int is_end = robo_css_token_type_is_block_end(type);

            ROBO_CHECK(!(is_start && is_end));
            starts += is_start;
            ends += is_end;
        }
        ROBO_CHECK_EQ_INT(starts, 3);
        ROBO_CHECK_EQ_INT(ends, 3);
    }
}

static void test_css_token_bracket_pairs(void)
{
    robo_css_token_type_t close = ROBO_CSS_TOKEN_NONE;
    robo_css_token_type_t open = ROBO_CSS_TOKEN_NONE;

    ROBO_CHECK(robo_css_token_type_matching_close(ROBO_CSS_TOKEN_LEFT_PAREN, &close));
    ROBO_CHECK_EQ_INT(close, ROBO_CSS_TOKEN_RIGHT_PAREN);
    ROBO_CHECK(robo_css_token_type_matching_close(ROBO_CSS_TOKEN_LEFT_SQUARE, &close));
    ROBO_CHECK_EQ_INT(close, ROBO_CSS_TOKEN_RIGHT_SQUARE);
    ROBO_CHECK(robo_css_token_type_matching_close(ROBO_CSS_TOKEN_LEFT_CURLY, &close));
    ROBO_CHECK_EQ_INT(close, ROBO_CSS_TOKEN_RIGHT_CURLY);

    /* A non-bracket sets the output back to NONE and answers no. */
    close = ROBO_CSS_TOKEN_RIGHT_PAREN;
    ROBO_CHECK(!robo_css_token_type_matching_close(ROBO_CSS_TOKEN_IDENT, &close));
    ROBO_CHECK_EQ_INT(close, ROBO_CSS_TOKEN_NONE);

    ROBO_CHECK(robo_css_token_type_matching_open(ROBO_CSS_TOKEN_RIGHT_PAREN, &open));
    ROBO_CHECK_EQ_INT(open, ROBO_CSS_TOKEN_LEFT_PAREN);
    ROBO_CHECK(robo_css_token_type_matching_open(ROBO_CSS_TOKEN_RIGHT_SQUARE, &open));
    ROBO_CHECK_EQ_INT(open, ROBO_CSS_TOKEN_LEFT_SQUARE);
    ROBO_CHECK(robo_css_token_type_matching_open(ROBO_CSS_TOKEN_RIGHT_CURLY, &open));
    ROBO_CHECK_EQ_INT(open, ROBO_CSS_TOKEN_LEFT_CURLY);
    ROBO_CHECK(!robo_css_token_type_matching_open(ROBO_CSS_TOKEN_COLON, &open));

    /* The pairing is symmetric for all six brackets, and the output is
     * optional (the answer is then only the return value). */
    {
        robo_css_token_type_t type;

        for (type = ROBO_CSS_TOKEN_NONE;
             type < (robo_css_token_type_t)ROBO_CSS_TOKEN_TYPE_COUNT;
             type = (robo_css_token_type_t)(type + 1)) {
            robo_css_token_type_t other;

            if (!robo_css_token_type_matching_close(type, &other)) {
                continue;
            }
            ROBO_CHECK(robo_css_token_type_matching_open(other, NULL));
            ROBO_CHECK(robo_css_token_type_matching_close(type, NULL));
        }
    }
}

static void test_css_token_hash_types(void)
{
    ROBO_CHECK(robo_css_hash_type_is_known(ROBO_CSS_HASH_TYPE_ID));
    ROBO_CHECK(robo_css_hash_type_is_known(ROBO_CSS_HASH_TYPE_NONE));
    ROBO_CHECK(!robo_css_hash_type_is_known((robo_css_hash_type_t)9));
    ROBO_CHECK_STR_EQ(robo_css_hash_type_name(ROBO_CSS_HASH_TYPE_ID), "id");
    ROBO_CHECK_STR_EQ(robo_css_hash_type_name(ROBO_CSS_HASH_TYPE_UNRESTRICTED),
                      "unrestricted");
    ROBO_CHECK_STR_EQ(robo_css_hash_type_name((robo_css_hash_type_t)9), "unknown");
}

static void test_css_token_make_binds_range(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "padding: 10px;");

    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, &source,
                                          robo_css_range_span(0u, 7u), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_IDENT);
    ROBO_CHECK_EQ_INT(token.range.begin, 0u);
    ROBO_CHECK_EQ_INT(token.range.end, 7u);
    ROBO_CHECK_EQ_INT(robo_css_token_text(&token).length, 7u);
    ROBO_CHECK(robo_css_token_equals_ascii(&token, "padding"));

    /* A token constructed here is bound, not classified: the numeric fields are
     * what a tokenizer will fill, and a tokenizer does not exist yet. */
    ROBO_CHECK_EQ_INT(token.number, 0);
    ROBO_CHECK_EQ_INT(token.is_integer, 0);
    ROBO_CHECK_EQ_INT(token.has_sign, 0);
    ROBO_CHECK(robo_css_token_text(&token).bytes ==
               robo_css_source_bytes(&source));

    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_DIMENSION, &source,
                                          robo_css_range_span(9u, 4u), NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_token_equals_ascii(&token, "10px"));
    ROBO_CHECK_EQ_INT(token.number, 0);
    ROBO_CHECK(robo_css_slice_is_empty(token.unit));
    ROBO_CHECK(robo_css_slice_is_empty(token.repr));

    /* Case-insensitive comparison is the one CSS keyword matching uses. */
    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, &source,
                                          robo_css_range_span(0u, 7u), NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_css_token_equals_ascii_ci(&token, "PADDING"));
    ROBO_CHECK(!robo_css_token_equals_ascii(&token, "PADDING"));
    ROBO_CHECK(!robo_css_token_equals_ascii(&token, "paddin"));
    ROBO_CHECK(!robo_css_token_equals_ascii(&token, "padding!"));
}

static void test_css_token_make_rejects(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_diag_t diag;

    css_fixture();
    css_source_of_string(&source, "abc");
    robo_css_token_init(&token);

    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, (robo_css_token_type_t)99, &source,
                                          robo_css_range_span(0u, 1u), &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_TOKEN);
    /* The token is untouched: a refused construction leaves no half-token. */
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_NONE);

    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, &source,
                                          robo_css_range_span(0u, 99u), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, &source,
                                          (robo_css_range_t){2u, 1u}, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, NULL,
                                          robo_css_range_span(0u, 1u), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_make(NULL, ROBO_CSS_TOKEN_IDENT, &source,
                                          robo_css_range_span(0u, 1u), NULL),
                      ROBO_INVALID_ARGUMENT);
}

static void test_css_token_eof(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "a{b}");

    ROBO_CHECK_EQ_INT(robo_css_token_make_eof(&token, &source, 4u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK(robo_css_token_is_terminator(&token));
    ROBO_CHECK(!robo_css_token_is_none(&token));
    /* EOF carries no bytes, whatever an empty range would have resolved to. */
    ROBO_CHECK(robo_css_slice_is_empty(robo_css_token_text(&token)));

    /* Past the end is refused; the end itself is the valid EOF offset. */
    ROBO_CHECK_EQ_INT(robo_css_token_make_eof(&token, &source, 5u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_make_eof(&token, NULL, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An empty source has exactly one valid EOF offset: 0. */
    robo_css_source_dispose(&source);
    css_source_of_string(&source, "");
    ROBO_CHECK_EQ_INT(robo_css_token_make_eof(&token, &source, 0u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(robo_css_token_make_eof(&token, &source, 1u, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_source_dispose(&source);
}

static void test_css_token_lifecycle(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    robo_css_token_t copy = ROBO_CSS_CLEARED;
    int we_enabled;
    size_t baseline;

    css_fixture();
    we_enabled = css_allocator_ensure_enabled();
    css_source_of_string(&source, "color:red");
    baseline = robo_mem_debug_live_count();

    robo_css_token_init(&token);
    ROBO_CHECK(robo_css_token_is_none(&token));
    ROBO_CHECK(robo_css_token_is_terminator(&token));
    ROBO_CHECK_EQ_INT(token.type, ROBO_CSS_TOKEN_NONE);
    ROBO_CHECK(robo_css_slice_is_absent(token.text));

    {
        int i;

        for (i = 0; i < 32; i++) {
            /* A token owns nothing, so make/copy/dispose must leave the
             * allocator untouched. That is the claim, and this is the check. */
            ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT,
                                                 &source, robo_css_range_span(0u, 5u),
                                                 NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_token_copy(&token, &copy, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(copy.type, ROBO_CSS_TOKEN_IDENT);
            ROBO_CHECK(robo_css_token_equals_ascii(&copy, "color"));
            robo_css_token_dispose(&copy);
            ROBO_CHECK(robo_css_token_is_none(&copy));
            ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        }
    }

    ROBO_CHECK_EQ_INT(robo_css_token_copy(&token, &copy, NULL), ROBO_OK);
    /* Copying onto itself is a no-op rather than a self-overwrite. */
    ROBO_CHECK_EQ_INT(robo_css_token_copy(&copy, &copy, NULL), ROBO_OK);
    ROBO_CHECK(robo_css_token_equals_ascii(&copy, "color"));

    robo_css_token_dispose(&token);
    robo_css_token_dispose(NULL);
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    ROBO_CHECK_EQ_INT(robo_css_token_copy(NULL, &copy, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_token_copy(&copy, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_css_token_is_none(NULL));
    ROBO_CHECK(robo_css_token_is_terminator(NULL));
    ROBO_CHECK(robo_css_slice_is_absent(robo_css_token_text(NULL)));
    ROBO_CHECK(!robo_css_token_equals_ascii(NULL, "color"));
    ROBO_CHECK(!robo_css_token_equals_ascii_ci(NULL, "COLOR"));

    css_allocator_restore(we_enabled);
    robo_css_source_dispose(&source);
}

void robo_test_register_css_token(void);

void robo_test_register_css_token(void)
{
    robo_test_register("css.token.vocabulary", test_css_token_vocabulary);
    robo_test_register("css.token.classification", test_css_token_classification);
    robo_test_register("css.token.bracket_pairs", test_css_token_bracket_pairs);
    robo_test_register("css.token.hash_types", test_css_token_hash_types);
    robo_test_register("css.token.make_binds_range", test_css_token_make_binds_range);
    robo_test_register("css.token.make_rejects", test_css_token_make_rejects);
    robo_test_register("css.token.eof", test_css_token_eof);
    robo_test_register("css.token.lifecycle", test_css_token_lifecycle);
}
