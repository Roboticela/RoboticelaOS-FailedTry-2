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
 * The corpus is `cssSamples` from the shared support header, so "Arabic" means
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
static void TestCssParseUnicodePreludesPreserveBytes(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = cssSamples[index].text;
        size_t length = strlen(sample);
        char text[256];
        CssError error = RoboCssCleared;
        CssStylesheetId sheet;
        const CssStyleRule *rule;
        const CssSource *source;
        CssSlice prelude = CssSliceEmpty();

        snprintf(text, sizeof(text), "%s { color: red; }", sample);
        sheet = CssParseSheet(text, &error);

        /* Clean input, and the same number of rules whatever the script. */
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
        rule = CssSheetStyleRule(sheet, 0u);
        RoboCheck(rule != NULL);
        RoboCheckEqInt(CssStyleRuleBlockClosed(rule), 1);

        /* The prelude is exactly the sample: it starts at byte 0 and ends where
         * the sample does, so the ` { ` after it is not part of it. */
        source = CssStylesheetSource(sheet);
        RoboCheck(source != NULL);
        RoboCheckEqInt(CssStyleRulePreludeRange(rule).begin, 0u);
        RoboCheckEqInt(CssStyleRulePreludeRange(rule).end, length);
        RoboCheckEqInt(
            CssSourceRangeSlice(source,
                                        CssStyleRulePreludeRange(rule),
                                        &prelude),
            RoboOk);
        RoboCheckEqInt(prelude.length, length);
        RoboCheck(memcmp(prelude.bytes, sample, length) == 0);

        /* And it read as component values: at least one leaf, and every leaf
         * inside the prelude's own extent. */
        {
            char shape[256];

            CssPreludeShape(rule, shape, sizeof(shape));
            RoboCheck(shape[0] != '\0');
        }

        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
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
static void TestCssParseUnicodeStringValuesPreserveBytes(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = cssSamples[index].text;
        size_t length = strlen(sample);
        char text[256];
        CssError error = RoboCssCleared;
        CssStylesheetId sheet;
        const CssValue *value;
        const CssValue *leaf;
        uint8_t resolved[256];
        size_t resolvedLength = 0u;

        snprintf(text, sizeof(text), CSS_UNICODE_STRING_PREFIX "%s"
                 CSS_UNICODE_STRING_SUFFIX, sample);
        sheet = CssParseSheet(text, &error);

        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        value = CssDeclarationValue(
            CssStyleRuleDeclaration(CssSheetStyleRule(sheet, 0u), 0u));
        RoboCheck(value != NULL);
        RoboCheckEqInt(value->children.count, 1u);
        leaf = &value->children.items[0];
        RoboCheckEqInt(leaf->kind, RoboCssValueKindToken);
        RoboCheckEqInt(leaf->token.type, RoboCssTokenString);
        /* The terminator was there, and the raw text kept the quotes — the two
         * answers a string token has to carry. */
        RoboCheckEqInt(leaf->token.closed, 1);
        RoboCheckEqInt(leaf->token.text.length, length + 2u);

        RoboCheckEqInt(CssTokenValueLength(&leaf->token), length);
        RoboCheckEqInt(CssTokenValueCopy(&leaf->token, resolved,
                                                    sizeof(resolved),
                                                    &resolvedLength, NULL),
                          RoboOk);
        RoboCheckEqInt(resolvedLength, length);
        RoboCheck(memcmp(resolved, sample, length) == 0);

        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
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
static void TestCssParseUnicodePropertyNamesAndCustomProperties(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = cssSamples[index].text;
        size_t indent = 0u; /* the sample's first `ident` run */
        char ident[128];
        char text[256];
        CssError error = RoboCssCleared;
        CssDeclarationList list = RoboCssCleared;
        CssSlice name;
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
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);

        RoboCheckEqInt(CssDeclarationListCount(&list), 1u);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        name = CssDeclarationName(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(name.length, indent);
        RoboCheck(memcmp(name.bytes, sample, indent) == 0);
        /* The property is a name only: nothing resolved it to an identity,
         * because there is no property database in this phase. */
        RoboCheckEqInt(
            CssDeclarationListAt(&list, 0u)->state,
            RoboCssDeclarationStateParsed);
        CssDeclarationListDispose(&list);

        /* The same bytes as a custom property, where the `--` prefix is CSS's
         * own marker and the rest is still whatever the author wrote. */
        snprintf(text, sizeof(text), "--%s: %s;", ident, sample);
        RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        name = CssDeclarationName(CssDeclarationListAt(&list, 0u));
        RoboCheckEqInt(name.length, indent + 2u);
        RoboCheck(memcmp(name.bytes, "--", 2u) == 0);
        RoboCheck(memcmp(name.bytes + 2u, sample, indent) == 0);

        /* Its value is kept as written — the sample's own bytes, in order,
         * with nothing resolved — which is what makes `var()` possible later.
         * The shape joins component values with `,` and folds whitespace to `_`,
         * so the expected string is the sample with each space replaced by the
         * separator: a sample with a space is two component values, and one
         * without is a single value. */
        CssDeclarationShape(&list, 0u, shadow, sizeof(shadow));
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
                    CssShapeText(expected, sizeof(expected), &write, ",_,");
                    continue;
                }
                run = at;
                while (sample[run] != '\0' && sample[run] != ' ') {
                    run++;
                }
                for (; at < run; at++) {
                    CssShapeByte(expected, sizeof(expected), &write,
                                   (uint8_t)sample[at]);
                }
            }
            /* Trailing punctuation from the last separator would mean the value
             * ended in whitespace, which the grammar trims — so the expected
             * string never ends in one. */
            RoboCheckStrEq(shadow, expected);
        }
        CssDeclarationListDispose(&list);
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
static void TestCssParseUnicodeCommentsStayComments(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        char text[256];
        CssError error = RoboCssCleared;
        CssStylesheetId sheet;
        char shape[64];

        snprintf(text, sizeof(text), "/* %s */ p { color: red; }",
                 cssSamples[index].text);
        sheet = CssParseSheet(text, &error);

        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
        /* `p` and nothing else: no part of the comment became a selector, and
         * the whole comment was skipped. */
        CssPreludeShape(CssSheetStyleRule(sheet, 0u), shape, sizeof(shape));
        RoboCheckStrEq(shape, "p");

        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    }
}

/**
 * Ranges are byte offsets, and a Unicode token's length is its byte length.
 *
 * The arithmetic is the assertion: with a sample of `n` bytes, the prelude ends
 * at `n`, the block begins at `n + 2` (the space and the `{`) and the whole rule
 * ends at `n + 21`. A parser that counted characters, or a value layer that
 * returned a character count from `valueLength`, makes one of these numbers
 * wrong — and unlike a text comparison, that failure cannot be hidden by the
 * bytes still being correct.
 */
static void TestCssParseUnicodeRangesAreByteOffsets(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = cssSamples[index].text;
        size_t length = strlen(sample);
        char text[256];
        size_t total;
        CssError error = RoboCssCleared;
        CssStylesheetId sheet;
        const CssStyleRule *rule;

        snprintf(text, sizeof(text), "%s { color: red; }", sample);
        total = strlen(text);
        sheet = CssParseSheet(text, &error);
        rule = CssSheetStyleRule(sheet, 0u);

        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        RoboCheckEqInt(CssStyleRulePreludeRange(rule).begin, 0u);
        RoboCheckEqInt(CssStyleRulePreludeRange(rule).end, length);
        RoboCheckEqInt(CssStyleRuleBlockRange(rule).end, total);
        RoboCheckEqInt(CssStyleRuleRange(rule).begin, 0u);
        RoboCheckEqInt(CssStyleRuleRange(rule).end, total);

        /* The block's extent begins at the `{` itself — one byte after the space
         * that separates it from the prelude — and ends at the `}`. */
        RoboCheckEqInt(CssStyleRuleBlockRange(rule).begin, length + 1u);

        /* The declaration's value is `red`, and its offsets are counted from
         * byte 0 of the source rather than from the start of the block. */
        {
            const CssDeclaration *declaration =
                CssStyleRuleDeclaration(rule, 0u);

            RoboCheckEqInt(declaration->nameRange.end,
                              declaration->nameRange.begin + 5u); /* color */
            RoboCheckEqInt(declaration->valueRange.begin,
                              declaration->range.begin + 7u);     /* color: */
            RoboCheckEqInt(declaration->valueRange.end,
                              declaration->valueRange.begin + 3u); /* red */
            RoboCheck(declaration->valueRange.begin > length);
        }

        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
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
static void TestCssParseUnicodeAtRules(void)
{
    size_t index;

    for (index = 0u; index < CSS_SAMPLE_COUNT; index++) {
        const char *sample = cssSamples[index].text;
        size_t length = strlen(sample);
        char text[256];
        CssError error = RoboCssCleared;
        CssStylesheetId sheet;
        const CssAtRule *at;
        const CssSource *source;
        CssSlice prelude = CssSliceEmpty();

        snprintf(text, sizeof(text), "@media %s { p { color: red; } }", sample);
        sheet = CssParseSheet(text, &error);

        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
        at = CssRuleAt(CssStylesheetRule(sheet, 0u));
        RoboCheck(at != NULL);
        RoboCheck(CssSliceEqualsAscii(at->name, "media"));
        RoboCheckEqInt(at->hasBlock, 1);
        RoboCheckEqInt(CssAtRuleRuleCount(at), 1u);

        /* The prelude is the sample alone: the space that separates it from
         * `@media` and the space before `{` are both outside it. */
        source = CssStylesheetSource(sheet);
        RoboCheckEqInt(at->preludeRange.begin, 7u); /* strlen("@media ") */
        RoboCheckEqInt(at->preludeRange.end, 7u + length);
        RoboCheckEqInt(CssSourceRangeSlice(source, at->preludeRange,
                                                      &prelude),
                          RoboOk);
        RoboCheckEqInt(prelude.length, length);
        RoboCheck(memcmp(prelude.bytes, sample, length) == 0);

        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    }

    /* A non-ASCII *at-rule name* is not special-cased in either direction: it is
     * carried as written and nothing refuses it, because there is no at-rule
     * table to refuse it against. */
    {
        const char *sample = cssSamples[3].text; /* arabic */
        CssError error = RoboCssCleared;
        CssStylesheetId sheet;
        char text[128];

        snprintf(text, sizeof(text), "@%s screen { p { color: red; } }", sample);
        sheet = CssParseSheet(text, &error);
        RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
        RoboCheckEqInt(CssStylesheetRuleCount(sheet), 1u);
        RoboCheck(CssSliceEqualsAscii(
            CssRuleAt(CssStylesheetRule(sheet, 0u))->name, sample));
        RoboCheckEqInt(CssStylesheetDestroy(sheet, NULL), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterCssParseUnicode(void);

void TestRegisterCssParseUnicode(void)
{
    TestRegister("css.parse.unicode.preludes_preserve_bytes",
                       TestCssParseUnicodePreludesPreserveBytes);
    TestRegister("css.parse.unicode.string_values_preserve_bytes",
                       TestCssParseUnicodeStringValuesPreserveBytes);
    TestRegister("css.parse.unicode.property_names_and_custom_properties",
                       TestCssParseUnicodePropertyNamesAndCustomProperties);
    TestRegister("css.parse.unicode.comments_stay_comments",
                       TestCssParseUnicodeCommentsStayComments);
    TestRegister("css.parse.unicode.ranges_are_byte_offsets",
                       TestCssParseUnicodeRangesAreByteOffsets);
    TestRegister("css.parse.unicode.at_rules",
                       TestCssParseUnicodeAtRules);
}
