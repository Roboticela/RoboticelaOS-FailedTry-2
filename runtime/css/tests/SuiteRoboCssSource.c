/*
 * RoboCSS Phase 9.1 source suite:
 * configuration, ownership, positions, ranges, byte description.
 */

#include "RoboCssTestSupport.h"

static void test_css_source_kinds(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    const char *text = CSS_FIXTURE_PARAGRAPH;

    css_fixture();
    robo_css_source_init(&source);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_NONE);
    ROBO_CHECK(!robo_css_source_is_configured(&source));
    ROBO_CHECK(robo_css_source_is_empty(&source));
    ROBO_CHECK(robo_css_source_bytes(&source) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_source_length(&source), 0u);
    ROBO_CHECK(!robo_css_source_is_owned(&source));

    ROBO_CHECK_EQ_INT(robo_css_source_set_empty(&source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_EMPTY);
    ROBO_CHECK(robo_css_source_is_configured(&source));
    ROBO_CHECK(robo_css_source_is_empty(&source));
    ROBO_CHECK(!robo_css_source_is_owned(&source));

    ROBO_CHECK_EQ_INT(robo_css_source_set_string(&source, text, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_STRING);
    /* A borrowed source points at the caller's bytes: no copy was made. */
    ROBO_CHECK(robo_css_source_bytes(&source) == (const uint8_t *)text);
    ROBO_CHECK_EQ_INT(robo_css_source_length(&source), strlen(text));
    ROBO_CHECK(!robo_css_source_is_owned(&source));

    /* A NULL string is an explicit empty source, not an error. */
    ROBO_CHECK_EQ_INT(robo_css_source_set_string(&source, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_EMPTY);

    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source,
                                                 (const uint8_t *)text,
                                                 strlen(text), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_MEMORY);

    /* A zero-length memory source is empty, not a borrow of nothing. */
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, NULL, 0u, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_EMPTY);
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, (const uint8_t *)text, 0u,
                                                 NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_EMPTY);

    robo_css_source_dispose(&source);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_NONE);
}

static void test_css_source_copy_owns_bytes(void)
{
    char buffer[64];
    robo_css_source_t source = ROBO_CSS_CLEARED;

    css_fixture();
    strcpy(buffer, CSS_FIXTURE_CLASS);
    robo_css_source_init(&source);

    ROBO_CHECK_EQ_INT(robo_css_source_copy(&source, (const uint8_t *)buffer,
                                           strlen(buffer), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_COPY);
    ROBO_CHECK(robo_css_source_is_owned(&source));
    ROBO_CHECK(robo_css_source_bytes(&source) != (const uint8_t *)buffer);

    /* Damage the caller's buffer: the copy must be unaffected, which is the
     * entire reason the owned kind exists. */
    buffer[0] = 'X';
    ROBO_CHECK_EQ_INT(robo_css_source_bytes(&source)[0], (uint8_t)'.');
    ROBO_CHECK_EQ_INT(robo_css_source_length(&source), strlen(CSS_FIXTURE_CLASS));

    /* Dispose is idempotent and repeated disposal is not a double free. */
    robo_css_source_dispose(&source);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_NONE);
    robo_css_source_dispose(&source);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_NONE);
    robo_css_source_dispose(NULL);
}

static void test_css_source_reconfigure_releases(void)
{
    char buffer[64];
    robo_css_source_t source = ROBO_CSS_CLEARED;
    int we_enabled;
    size_t baseline;

    css_fixture();
    we_enabled = css_allocator_ensure_enabled();
    strcpy(buffer, CSS_FIXTURE_BODY);
    robo_css_source_init(&source);

    baseline = robo_mem_debug_live_count();
    {
        int i;

        for (i = 0; i < 8; i++) {
            /* Copy, then overwrite with a borrow, then copy again: every path
             * that replaces content must release what the previous one owned. */
            ROBO_CHECK_EQ_INT(robo_css_source_copy(&source, (const uint8_t *)buffer,
                                                   strlen(buffer), NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_source_set_string(&source, buffer, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_source_copy(&source, (const uint8_t *)buffer,
                                                   strlen(buffer), NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_css_source_set_empty(&source, NULL), ROBO_OK);
        }
    }
    robo_css_source_dispose(&source);
    ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
    css_allocator_restore(we_enabled);
}

static void test_css_source_rejects_bad_arguments(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_diag_t diag;

    css_fixture();
    robo_css_source_init(&source);
    ROBO_CHECK_EQ_INT(robo_css_source_set_empty(&source, NULL), ROBO_OK);

    /* NULL bytes with a length is a caller mistake, and the source is left as
     * it was rather than half-configured. */
    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, NULL, 4u, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_SOURCE);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_EMPTY);
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, NULL, 4u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_copy(&source, NULL, 4u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(source.kind, ROBO_CSS_SOURCE_KIND_EMPTY);

    /* Every entry point tolerates a NULL source, and says so. */
    ROBO_CHECK_EQ_INT(robo_css_source_set_empty(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_set_memory(NULL, NULL, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_set_string(NULL, "x", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_copy(NULL, NULL, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_set_name(NULL, "x", NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_css_source_name(NULL) == NULL);
    ROBO_CHECK(robo_css_source_bytes(NULL) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_source_length(NULL), 0u);
    ROBO_CHECK(robo_css_source_is_empty(NULL));
    ROBO_CHECK(!robo_css_source_is_owned(NULL));
    ROBO_CHECK(!robo_css_source_is_configured(NULL));
    ROBO_CHECK(robo_css_source_slice(NULL).bytes == NULL);
}

static void test_css_source_labels(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    char long_label[ROBO_CSS_NAME_MAX + 32];
    size_t i;

    css_fixture();
    robo_css_source_init(&source);
    ROBO_CHECK_STR_EQ(robo_css_source_name(&source), "");

    ROBO_CHECK_EQ_INT(robo_css_source_set_name(&source, "Sheet 1", NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_css_source_name(&source), "Sheet 1");

    /* A label is diagnostics: too long is truncated, never refused. */
    for (i = 0u; i < sizeof(long_label) - 1u; i++) {
        long_label[i] = 'L';
    }
    long_label[sizeof(long_label) - 1u] = '\0';
    ROBO_CHECK_EQ_INT(robo_css_source_set_name(&source, long_label, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(strlen(robo_css_source_name(&source)), (size_t)ROBO_CSS_NAME_MAX - 1u);

    /* A cleared content path keeps the label: the source still names itself. */
    ROBO_CHECK_EQ_INT(robo_css_source_set_empty(&source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(strlen(robo_css_source_name(&source)), (size_t)ROBO_CSS_NAME_MAX - 1u);

    /* Disposing clears everything, including the label. */
    robo_css_source_dispose(&source);
    ROBO_CHECK_STR_EQ(robo_css_source_name(&source), "");
}

static void test_css_source_positions_lf(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_position_t position;

    css_fixture();
    css_source_of_string(&source, "p {\n  color: red;\n}\n");

    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 0u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 1u);

    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 2u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 3u);

    /* The offset of the LF itself is still on line 1... */
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 3u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 4u);
    /* ...and one past it is the start of line 2. */
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 4u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 2u);
    ROBO_CHECK_EQ_INT(position.column, 1u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 6u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 2u);
    ROBO_CHECK_EQ_INT(position.column, 3u);

    /* The end offset is a valid position: the cursor reaching the end. */
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, strlen("p {\n  color: red;\n}\n"),
                                               &position),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 4u);
    ROBO_CHECK_EQ_INT(position.column, 1u);

    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 99u, &position),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_position(NULL, 0u, &position),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 0u, NULL),
                      ROBO_INVALID_ARGUMENT);
}

static void test_css_source_positions_crlf_and_cr(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_position_t position;

    css_fixture();
    /* CRLF is one break, so the second line starts at offset 3. */
    css_source_of_string(&source, "a\r\nb");
    ROBO_CHECK_EQ_INT(robo_css_source_line_count(&source), 2u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 3u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 2u);
    ROBO_CHECK_EQ_INT(position.column, 1u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 4u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 2u);
    ROBO_CHECK_EQ_INT(position.column, 2u);

    /* A lone CR is also a break. */
    robo_css_source_dispose(&source);
    css_source_of_string(&source, "a\rb");
    ROBO_CHECK_EQ_INT(robo_css_source_line_count(&source), 2u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 2u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 2u);
    ROBO_CHECK_EQ_INT(position.column, 1u);

    /* A source with no break at all is still one line. */
    robo_css_source_dispose(&source);
    css_source_of_string(&source, "abc");
    ROBO_CHECK_EQ_INT(robo_css_source_line_count(&source), 1u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 3u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 4u);

    /* Empty: no lines to count, but offset 0 is still a position. */
    robo_css_source_dispose(&source);
    ROBO_CHECK_EQ_INT(robo_css_source_line_count(&source), 0u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 0u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 1u);

    robo_css_source_dispose(&source);
}

static void test_css_source_positions_count_scalars(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_position_t position;

    css_fixture();
    /* Three two-byte scalars then a newline: the column after them is 4, not 7,
     * because a column number has to mean the same thing in every language. */
    css_source_of_string(&source, "caf\xC3\xA9##\nnext");
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 7u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 7u);

    robo_css_source_dispose(&source);
    css_source_of_string(&source, "\xD8\xA7\xD9\x84\nx");
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 4u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 1u);
    ROBO_CHECK_EQ_INT(position.column, 3u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 5u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.line, 2u);
    ROBO_CHECK_EQ_INT(position.column, 1u);

    /* An emoji is one scalar even at four bytes, and a variation selector adds
     * one more — this phase counts scalars, it does not segment graphemes. */
    robo_css_source_dispose(&source);
    css_source_of_string(&source, "\xF0\x9F\x8E\xA8\xEF\xB8\x8F!");
    ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&source), 3u);
    ROBO_CHECK_EQ_INT(robo_css_source_position(&source, 7u, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.column, 3u);

    robo_css_source_dispose(&source);
}

static void test_css_source_ranges(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;
    robo_css_slice_t slice;
    const char *text = CSS_FIXTURE_ID;

    css_fixture();
    css_source_of_string(&source, text);

    ROBO_CHECK(robo_css_source_range_is_valid(&source, robo_css_range_span(0u, 1u)));
    ROBO_CHECK(robo_css_source_range_is_valid(&source, robo_css_range_at(0u)));
    /* The empty range at the very end is valid: that is the cursor at EOF. */
    ROBO_CHECK(robo_css_source_range_is_valid(&source, robo_css_range_at(strlen(text))));
    ROBO_CHECK(!robo_css_source_range_is_valid(&source,
                                               robo_css_range_span(0u, strlen(text) + 1u)));
    ROBO_CHECK(!robo_css_source_range_is_valid(&source,
                                               (robo_css_range_t){5u, 2u}));
    ROBO_CHECK(!robo_css_source_range_is_valid(NULL, robo_css_range_at(0u)));

    /* CSS_FIXTURE_ID is "#title { ... }": offsets 1..5 are the id selector's
     * own name, which is the span a selector would later keep. */
    ROBO_CHECK_EQ_INT(robo_css_source_range_slice(&source, robo_css_range_span(1u, 5u),
                                                  &slice),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(slice.length, 5u);
    ROBO_CHECK(robo_css_slice_equals_ascii(slice, "title"));
    /* The slice borrows the source: it points into the source's own bytes at
     * the range's begin, so reading it back from here still sees the source. */
    ROBO_CHECK(slice.bytes == robo_css_source_bytes(&source) + 1u);

    /* A valid empty range yields an empty slice, not an absent one. */
    ROBO_CHECK_EQ_INT(robo_css_source_range_slice(&source, robo_css_range_at(3u),
                                                  &slice),
                      ROBO_OK);
    ROBO_CHECK(robo_css_slice_is_empty(slice));

    ROBO_CHECK_EQ_INT(robo_css_source_range_slice(&source,
                                                  robo_css_range_span(0u, 999u), &slice),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_range_slice(&source, robo_css_range_at(0u), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_source_range_slice(NULL, robo_css_range_at(0u), &slice),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_css_slice_is_empty(slice));

    /* Offsets: 0 through length inclusive. */
    ROBO_CHECK(robo_css_source_offset_is_valid(&source, 0u));
    ROBO_CHECK(robo_css_source_offset_is_valid(&source, strlen(text)));
    ROBO_CHECK(!robo_css_source_offset_is_valid(&source, strlen(text) + 1u));
    ROBO_CHECK(!robo_css_source_offset_is_valid(NULL, 0u));

    robo_css_source_dispose(&source);
}

static void test_css_source_utf8_description(void)
{
    robo_css_source_t source = ROBO_CSS_CLEARED;

    css_fixture();
    css_source_of_string(&source, "body { color: red; }");
    ROBO_CHECK(robo_css_source_is_valid_utf8(&source));
    ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&source), strlen("body { color: red; }"));

    /* Ill-formed bytes are reported and preserved, never repaired. */
    robo_css_source_dispose(&source);
    {
        static const uint8_t bad[] = {'a', 0xC3u, 0x28u, 'b'};
        int we_enabled = css_allocator_ensure_enabled();
        size_t baseline = robo_mem_debug_live_count();

        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, bad, sizeof(bad), NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_css_source_is_valid_utf8(&source));
        /* An ill-formed byte is one position, so the count stays meaningful. */
        ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&source), 4u);
        ROBO_CHECK_EQ_INT(robo_css_source_bytes(&source)[1], 0xC3u);
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)baseline);
        css_allocator_restore(we_enabled);
    }

    /* Overlong, surrogate and out-of-range forms are all ill-formed. */
    robo_css_source_dispose(&source);
    {
        static const uint8_t overlong[] = {0xC0u, 0x80u};
        static const uint8_t surrogate[] = {0xEDu, 0xA0u, 0x80u};
        static const uint8_t too_big[] = {0xF5u, 0x80u, 0x80u, 0x80u};

        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, overlong,
                                                     sizeof(overlong), NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_css_source_is_valid_utf8(&source));
        ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&source), 2u);

        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, surrogate,
                                                     sizeof(surrogate), NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_css_source_is_valid_utf8(&source));

        ROBO_CHECK_EQ_INT(robo_css_source_set_memory(&source, too_big,
                                                     sizeof(too_big), NULL),
                          ROBO_OK);
        ROBO_CHECK(!robo_css_source_is_valid_utf8(&source));
    }

    /* A well-formed four-byte scalar is valid and counts as one. */
    robo_css_source_dispose(&source);
    css_source_of_string(&source, "\xF0\x9F\x98\x80");
    ROBO_CHECK(robo_css_source_is_valid_utf8(&source));
    ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&source), 1u);

    robo_css_source_dispose(&source);
    ROBO_CHECK(robo_css_source_is_valid_utf8(&source));
    ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&source), 0u);
}

static void test_css_source_preserves_unicode(void)
{
    size_t i;

    css_fixture();
    for (i = 0u; i < CSS_SAMPLE_COUNT; i++) {
        static const char prefix[] = "/* ";
        static const char suffix[] = " */\np { color: red; }\n";
        char rule[512];
        char copied[512];
        robo_css_source_t borrowed = ROBO_CSS_CLEARED;
        robo_css_source_t owned = ROBO_CSS_CLEARED;
        size_t rule_length;
        size_t j;
        int same = 1;

        /* A stylesheet written in each script family — the sample in a comment,
         * which is exactly how non-ASCII bytes most often appear in CSS. The
         * bytes must come back byte-for-byte, borrowed or copied. */
        rule_length = 0u;
        memcpy(rule, prefix, strlen(prefix));
        rule_length += strlen(prefix);
        memcpy(rule + rule_length, css_samples[i].text, strlen(css_samples[i].text));
        rule_length += strlen(css_samples[i].text);
        memcpy(rule + rule_length, suffix, strlen(suffix));
        rule_length += strlen(suffix);
        rule[rule_length] = '\0';

        css_source_of_string(&borrowed, rule);
        ROBO_CHECK(robo_css_source_is_valid_utf8(&borrowed));
        ROBO_CHECK_EQ_INT(robo_css_source_length(&borrowed), rule_length);
        for (j = 0u; j < rule_length; j++) {
            if (robo_css_source_bytes(&borrowed)[j] != (uint8_t)rule[j]) {
                same = 0;
            }
        }
        ROBO_CHECK_MSG(same, css_samples[i].label);

        robo_css_source_init(&owned);
        ROBO_CHECK_EQ_INT(robo_css_source_copy(&owned, (const uint8_t *)rule,
                                               rule_length, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_css_source_is_owned(&owned));
        ROBO_CHECK_EQ_INT(robo_css_source_length(&owned),
                          robo_css_source_length(&borrowed));
        for (j = 0u; j < rule_length; j++) {
            copied[j] = (char)robo_css_source_bytes(&owned)[j];
        }
        copied[rule_length] = '\0';
        ROBO_CHECK_STR_EQ(copied, rule);

        /* A column count is a scalar count, so a multi-byte sample must not
         * inflate it the way a byte count would. */
        ROBO_CHECK(robo_css_source_scalar_count(&owned) <=
                   robo_css_source_length(&owned));
        ROBO_CHECK_EQ_INT(robo_css_source_scalar_count(&owned),
                          robo_css_source_scalar_count(&borrowed));

        robo_css_source_dispose(&owned);
        robo_css_source_dispose(&borrowed);
    }
}

void robo_test_register_css_source(void);

void robo_test_register_css_source(void)
{
    robo_test_register("css.source.kinds", test_css_source_kinds);
    robo_test_register("css.source.copy_owns_bytes", test_css_source_copy_owns_bytes);
    robo_test_register("css.source.reconfigure_releases",
                       test_css_source_reconfigure_releases);
    robo_test_register("css.source.rejects_bad_arguments",
                       test_css_source_rejects_bad_arguments);
    robo_test_register("css.source.labels", test_css_source_labels);
    robo_test_register("css.source.positions_lf", test_css_source_positions_lf);
    robo_test_register("css.source.positions_crlf_and_cr",
                       test_css_source_positions_crlf_and_cr);
    robo_test_register("css.source.positions_scalars",
                       test_css_source_positions_count_scalars);
    robo_test_register("css.source.ranges", test_css_source_ranges);
    robo_test_register("css.source.utf8_description", test_css_source_utf8_description);
    robo_test_register("css.source.preserves_unicode",
                       test_css_source_preserves_unicode);
}
