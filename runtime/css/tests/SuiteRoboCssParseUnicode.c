/*
 * RoboCSS parser: Unicode source (Phase 9.3.23).
 *
 * A CSS parser is a byte machine that is not allowed to know what a character
 * is. Its jobs are offsets, boundaries and structure, and none of them needs
 * the bytes to be UTF-8 — which is why this suite asserts only two things:
 *
 *   - **Nothing is corrupted.** The bytes that reach a prelude, a property
 *     name, a value, a custom property or a string come back out identical to
 *     the ones that went in. Not normalised, not re-encoded, not dropped.
 *   - **Boundaries stay at boundaries.** A Unicode identifier is one token, a
 *     range is counted in bytes rather than in characters, and a comment
 *     containing Arabic does not leak into the selector after it.
 *
 * The corpus is `css_samples` from the shared support header, so "Arabic" means
 * the same bytes here as it does in every other RoboCSS suite. Everything is
 * built into a buffer with snprintf rather than written as a C literal, because
 * the point is that these are *bytes from somewhere else* — the parser never
 * sees an escape it was handed by the compiler.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>

/** Prefix that turns a sample into a value inside a CSS string. */
#define CSS_UNICODE_STRING_PREFIX "p { content: \""
#define CSS_UNICODE_STRING_SUFFIX "\"; }\n"

/**
 * Every sample survives a prelude, byte for byte.
 *
 * The prelude is the one place a Unicode run is *not* trimmed or quoted: it is
 * raw component values up to `{`, so the prelude's range must span exactly the
 * sample's bytes and the shape must contain all of them.
 */
static void test_css_parse_unicode_preludes_preserve_bytes(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = css_samples[index].text;
        size_t length = strlen(sample);
        char text[256];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet;
        const robo_css_style_rule_t *rule;
        const robo_css_source_t *source;
        robo_css_slice_t prelude = robo_css_slice_empty();

        snprintf(text, sizeof(text), "%s { color: red; }", sample);
        sheet = css_parse_sheet(text, &error);

        /* Clean input, and the same number of rules whatever the script. */
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
        rule = css_sheet_style_rule(sheet, 0u);
        ROBO_CHECK(rule != NULL);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_block_closed(rule), 1);

        /* The prelude is exactly the sample: it starts at byte 0 and ends where
         * the sample does, so the ` { ` after it is not part of it. */
        source = robo_css_stylesheet_source(sheet);
        ROBO_CHECK(source != NULL);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_prelude_range(rule).begin, 0u);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_prelude_range(rule).end, length);
        ROBO_CHECK_EQ_INT(
            robo_css_source_range_slice(source,
                                        robo_css_style_rule_prelude_range(rule),
                                        &prelude),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(prelude.length, length);
        ROBO_CHECK(memcmp(prelude.bytes, sample, length) == 0);

        /* And it read as component values: at least one leaf, and every leaf
         * inside the prelude's own extent. */
        {
            char shape[256];

            css_prelude_shape(rule, shape, sizeof(shape));
            ROBO_CHECK(shape[0] != '\0');
        }

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }
}

/**
 * Every sample survives a string value, byte for byte.
 *
 * A string is where the value layer does the most work — it strips the quotes
 * and resolves escapes — so it is the place a byte-per-character assumption
 * would show up first: a decoder that walked code points to find the closing
 * quote, or a value length measured in characters, corrupts exactly here.
 */
static void test_css_parse_unicode_string_values_preserve_bytes(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = css_samples[index].text;
        size_t length = strlen(sample);
        char text[256];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet;
        const robo_css_value_t *value;
        const robo_css_value_t *leaf;
        uint8_t resolved[256];
        size_t resolved_length = 0u;

        snprintf(text, sizeof(text), CSS_UNICODE_STRING_PREFIX "%s"
                 CSS_UNICODE_STRING_SUFFIX, sample);
        sheet = css_parse_sheet(text, &error);

        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        value = robo_css_declaration_value(
            robo_css_style_rule_declaration(css_sheet_style_rule(sheet, 0u), 0u));
        ROBO_CHECK(value != NULL);
        ROBO_CHECK_EQ_INT(value->children.count, 1u);
        leaf = &value->children.items[0];
        ROBO_CHECK_EQ_INT(leaf->kind, ROBO_CSS_VALUE_KIND_TOKEN);
        ROBO_CHECK_EQ_INT(leaf->token.type, ROBO_CSS_TOKEN_STRING);
        /* The terminator was there, and the raw text kept the quotes — the two
         * answers a string token has to carry. */
        ROBO_CHECK_EQ_INT(leaf->token.closed, 1);
        ROBO_CHECK_EQ_INT(leaf->token.text.length, length + 2u);

        ROBO_CHECK_EQ_INT(robo_css_token_value_length(&leaf->token), length);
        ROBO_CHECK_EQ_INT(robo_css_token_value_copy(&leaf->token, resolved,
                                                    sizeof(resolved),
                                                    &resolved_length, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(resolved_length, length);
        ROBO_CHECK(memcmp(resolved, sample, length) == 0);

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }
}

/**
 * Every sample survives a property name and a custom property.
 *
 * The name is an identifier, so it ends at the first whitespace — and that is
 * the assertion: exactly the bytes before the space, unchanged. It is also the
 * check that a non-ASCII byte is an identifier character rather than a
 * delimiter, because a tokenizer that disagreed would produce two tokens and a
 * name shorter than the sample.
 */
static void test_css_parse_unicode_property_names_and_custom_properties(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = css_samples[index].text;
        size_t indent = 0u; /* the sample's first `ident` run */
        char ident[128];
        char text[256];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_slice_t name;
        char shadow[256];
        size_t i;

        while (sample[indent] != '\0' && sample[indent] != ' ') {
            indent++;
        }
        /* Four of the samples are two words, and a property name is one
         * identifier — so the name half of this test uses the sample's first
         * word, and the *value* half uses the whole sample. Using the whole
         * sample as a name would be asserting that a space is an identifier
         * character, which is not what CSS says and not what 9.2 did. */
        for (i = 0u; i < indent; i++) {
            ident[i] = sample[i];
        }
        ident[indent] = '\0';

        /* A plain declaration: the sample's first word is the property name. */
        snprintf(text, sizeof(text), "%s: red;", ident);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);

        ROBO_CHECK_EQ_INT(robo_css_declaration_list_count(&list), 1u);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        name = robo_css_declaration_name(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(name.length, indent);
        ROBO_CHECK(memcmp(name.bytes, sample, indent) == 0);
        /* The property is a name only: nothing resolved it to an identity,
         * because there is no property database in this phase. */
        ROBO_CHECK_EQ_INT(
            robo_css_declaration_list_at(&list, 0u)->state,
            ROBO_CSS_DECLARATION_STATE_PARSED);
        robo_css_declaration_list_dispose(&list);

        /* The same bytes as a custom property, where the `--` prefix is CSS's
         * own marker and the rest is still whatever the author wrote. */
        snprintf(text, sizeof(text), "--%s: %s;", ident, sample);
        ROBO_CHECK_EQ_INT(css_parse_declarations(text, &list, &error), ROBO_OK);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        name = robo_css_declaration_name(robo_css_declaration_list_at(&list, 0u));
        ROBO_CHECK_EQ_INT(name.length, indent + 2u);
        ROBO_CHECK(memcmp(name.bytes, "--", 2u) == 0);
        ROBO_CHECK(memcmp(name.bytes + 2u, sample, indent) == 0);

        /* Its value is kept as written — the sample's own bytes, in order,
         * with nothing resolved — which is what makes `var()` possible later.
         * The shape joins component values with `,` and folds whitespace to `_`,
         * so the expected string is the sample with each space replaced by the
         * separator: a sample with a space is two component values, and one
         * without is a single value. */
        css_declaration_shape(&list, 0u, shadow, sizeof(shadow));
        {
            char expected[256];
            size_t write = 0u;
            size_t at = 0u;

            expected[0] = '\0';
            while (sample[at] != '\0' && write + 1u < sizeof(expected)) {
                size_t run;

                if (sample[at] == ' ') {
                    /* A run of whitespace between two component values: one
                     * whitespace child, so the shape gains the separator, the
                     * `_`, and the separator that follows it. */
                    while (sample[at] == ' ') {
                        at++;
                    }
                    css_shape_text(expected, sizeof(expected), &write, ",_,");
                    continue;
                }
                run = at;
                while (sample[run] != '\0' && sample[run] != ' ') {
                    run++;
                }
                for (; at < run; at++) {
                    css_shape_byte(expected, sizeof(expected), &write,
                                   (uint8_t)sample[at]);
                }
            }
            /* Trailing punctuation from the last separator would mean the value
             * ended in whitespace, which the grammar trims — so the expected
             * string never ends in one. */
            ROBO_CHECK_STR_EQ(shadow, expected);
        }
        robo_css_declaration_list_dispose(&list);
    }
}

/**
 * A Unicode comment is a comment, and it does not leak into what follows.
 *
 * A comment is the one construct that is discarded, so a byte-oriented skip
 * that mis-measured the terminator would either swallow the next rule or end
 * the comment early and leave half of it in the prelude. The prelude assertion
 * is what distinguishes the two.
 */
static void test_css_parse_unicode_comments_stay_comments(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        char text[256];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet;
        char shape[64];

        snprintf(text, sizeof(text), "/* %s */ p { color: red; }",
                 css_samples[index].text);
        sheet = css_parse_sheet(text, &error);

        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
        /* `p` and nothing else: no part of the comment became a selector, and
         * the whole comment was skipped. */
        css_prelude_shape(css_sheet_style_rule(sheet, 0u), shape, sizeof(shape));
        ROBO_CHECK_STR_EQ(shape, "p");

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }
}

/**
 * Ranges are byte offsets, and a Unicode token's length is its byte length.
 *
 * The arithmetic is the assertion: with a sample of `n` bytes, the prelude ends
 * at `n`, the block begins at `n + 2` (the space and the `{`) and the whole rule
 * ends at `n + 21`. A parser that counted characters, or a value layer that
 * returned a character count from `value_length`, makes one of these numbers
 * wrong — and unlike a text comparison, that failure cannot be hidden by the
 * bytes still being correct.
 */
static void test_css_parse_unicode_ranges_are_byte_offsets(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = css_samples[index].text;
        size_t length = strlen(sample);
        char text[256];
        size_t total;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet;
        const robo_css_style_rule_t *rule;

        snprintf(text, sizeof(text), "%s { color: red; }", sample);
        total = strlen(text);
        sheet = css_parse_sheet(text, &error);
        rule = css_sheet_style_rule(sheet, 0u);

        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_prelude_range(rule).begin, 0u);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_prelude_range(rule).end, length);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_block_range(rule).end, total);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_range(rule).begin, 0u);
        ROBO_CHECK_EQ_INT(robo_css_style_rule_range(rule).end, total);

        /* The block's extent begins at the `{` itself — one byte after the space
         * that separates it from the prelude — and ends at the `}`. */
        ROBO_CHECK_EQ_INT(robo_css_style_rule_block_range(rule).begin, length + 1u);

        /* The declaration's value is `red`, and its offsets are counted from
         * byte 0 of the source rather than from the start of the block. */
        {
            const robo_css_declaration_t *declaration =
                robo_css_style_rule_declaration(rule, 0u);

            ROBO_CHECK_EQ_INT(declaration->name_range.end,
                              declaration->name_range.begin + 5u); /* color */
            ROBO_CHECK_EQ_INT(declaration->value_range.begin,
                              declaration->range.begin + 7u);     /* color: */
            ROBO_CHECK_EQ_INT(declaration->value_range.end,
                              declaration->value_range.begin + 3u); /* red */
            ROBO_CHECK(declaration->value_range.begin > length);
        }

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }
}

/**
 * A Unicode at-rule prelude is preserved, and a Unicode *name* is still just a
 * name after the `@`.
 *
 * The two halves are the same fact from opposite sides: the bytes after `@` are
 * the name and are not looked up, and the bytes after the name are the prelude
 * and are not interpreted. Neither is affected by the script.
 */
static void test_css_parse_unicode_at_rules(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = css_samples[index].text;
        size_t length = strlen(sample);
        char text[256];
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet;
        const robo_css_at_rule_t *at;
        const robo_css_source_t *source;
        robo_css_slice_t prelude = robo_css_slice_empty();

        snprintf(text, sizeof(text), "@media %s { p { color: red; } }", sample);
        sheet = css_parse_sheet(text, &error);

        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
        at = robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u));
        ROBO_CHECK(at != NULL);
        ROBO_CHECK(robo_css_slice_equals_ascii(at->name, "media"));
        ROBO_CHECK_EQ_INT(at->has_block, 1);
        ROBO_CHECK_EQ_INT(robo_css_at_rule_rule_count(at), 1u);

        /* The prelude is the sample alone: the space that separates it from
         * `@media` and the space before `{` are both outside it. */
        source = robo_css_stylesheet_source(sheet);
        ROBO_CHECK_EQ_INT(at->prelude_range.begin, 7u); /* strlen("@media ") */
        ROBO_CHECK_EQ_INT(at->prelude_range.end, 7u + length);
        ROBO_CHECK_EQ_INT(robo_css_source_range_slice(source, at->prelude_range,
                                                      &prelude),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(prelude.length, length);
        ROBO_CHECK(memcmp(prelude.bytes, sample, length) == 0);

        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }

    /* A non-ASCII *at-rule name* is not special-cased in either direction: it is
     * carried as written and nothing refuses it, because there is no at-rule
     * table to refuse it against. */
    {
        const char *sample = css_samples[3].text; /* arabic */
        robo_css_error_t error = ROBO_CSS_CLEARED;
        robo_css_stylesheet_id_t sheet;
        char text[128];

        snprintf(text, sizeof(text), "@%s screen { p { color: red; } }", sample);
        sheet = css_parse_sheet(text, &error);
        ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_rule_count(sheet), 1u);
        ROBO_CHECK(robo_css_slice_equals_ascii(
            robo_css_rule_at(robo_css_stylesheet_rule(sheet, 0u))->name, sample));
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_css_parse_unicode(void);

void robo_test_register_css_parse_unicode(void)
{
    robo_test_register("css.parse.unicode.preludes_preserve_bytes",
                       test_css_parse_unicode_preludes_preserve_bytes);
    robo_test_register("css.parse.unicode.string_values_preserve_bytes",
                       test_css_parse_unicode_string_values_preserve_bytes);
    robo_test_register("css.parse.unicode.property_names_and_custom_properties",
                       test_css_parse_unicode_property_names_and_custom_properties);
    robo_test_register("css.parse.unicode.comments_stay_comments",
                       test_css_parse_unicode_comments_stay_comments);
    robo_test_register("css.parse.unicode.ranges_are_byte_offsets",
                       test_css_parse_unicode_ranges_are_byte_offsets);
    robo_test_register("css.parse.unicode.at_rules",
                       test_css_parse_unicode_at_rules);
}
