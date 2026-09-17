/*
 * Shared RoboCSS test fixtures (Phase 9.1).
 *
 * Every RoboCSS suite includes this. It is not a framework — the runtime has
 * exactly one of those (Tests/Framework/RoboTest.h) and this phase adds none. It
 * contains the three things a suite would otherwise copy:
 *
 *   fixture      bring RoboBase and RoboCSS up, and a source built from a
 *                literal without the caller tracking the length by hand
 *   samples      the Unicode script families every "does it preserve text"
 *                test walks, in one place so two tests cannot disagree about
 *                what "Arabic" means
 *   helpers      build a rule/declaration/value without six lines of setup,
 *                and measure the allocator's live block count around a scope
 *
 * The CSS fixtures are source data only. Phase 9.1 has no parser, so every test
 * that uses them asserts what the *model* does with bytes — ranges, slices,
 * ownership — and never that something was parsed.
 */

#ifndef ROBO_CSS_TEST_SUPPORT_H
#define ROBO_CSS_TEST_SUPPORT_H

#include "RoboBase/RoboBase.h"
#include "RoboCSS/RoboCSS.h"
#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSParser.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSRule.h"
#include "RoboCSS/RoboCSSSelector.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSStyleSheet.h"
#include "RoboCSS/RoboCSSToken.h"
#include "RoboCSS/RoboCSSValue.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** Bring RoboBase up (once) and leave RoboCSS initialized. */
static inline void css_fixture(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_css_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_OK);
    }
}

/**
 * Borrow @p text as a source. The literal outlives every test that uses it, so
 * the borrowed kind is the right one here — and it also means a suite that
 * forgets to dispose cannot corrupt a later test's allocator baseline.
 */
static inline void css_source_of_string(robo_css_source_t *source, const char *text)
{
    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_source_set_string(source, text, NULL), ROBO_OK);
}

/* The Phase 9.1 fixture corpus, verbatim from the phase specification. */
#define CSS_FIXTURE_PARAGRAPH "p {\n    color: red;\n}\n"
#define CSS_FIXTURE_CLASS ".button {\n    padding: 10px;\n}\n"
#define CSS_FIXTURE_ID "#title {\n    font-size: 24px;\n}\n"
#define CSS_FIXTURE_ATTRIBUTE "input[type=\"text\"] {\n    width: 200px;\n}\n"
#define CSS_FIXTURE_BODY "body {\n    background: white;\n}\n"

/** One named sample of text whose bytes must survive a round trip. */
typedef struct css_text_sample {
    const char *label;
    const char *text;
} css_text_sample_t;

/**
 * The thirteen families Phase 9.1.21 names. UTF-8 literals: the suite checks
 * that bytes are preserved, not that they are shaped or normalised.
 */
static const css_text_sample_t css_samples[] = {
    {"english", "The quick brown fox"},
    {"french", "Caf\xC3\xA9 na\xC3\xAFve"},
    /* "Uber " + o/a/u-umlaut + sharp s. The umlaut is split across two
     * literals because C hex escapes are greedy: "\xC3\x9Cber" would be read
     * as the single escape \x9Cb, which is out of range for char. */
    {"german", "\xC3\x9C" "ber \xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F"},
    {"arabic", "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9"},
    {"persian", "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C"},
    {"urdu", "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88"},
    {"hebrew", "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA"},
    {"devanagari", "\xE0\xA4\xA6\xE0\xA5\x87\xE0\xA4\xB5\xE0\xA4\xA8\xE0\xA4\xBE\xE0\xA4\x97\xE0\xA4\xB0\xE0\xA5\x80"},
    {"bengali", "\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0\xA6\xBE"},
    {"chinese", "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\xE4\xBD\x93"},
    {"japanese", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x83\x95\xE3\x82\xA9\xE3\x83\xB3\xE3\x83\x88"},
    {"korean", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"},
    {"emoji", "\xF0\x9F\x98\x80 \xF0\x9F\x8E\xA8\xEF\xB8\x8F"}
};

#define CSS_SAMPLE_COUNT (sizeof(css_samples) / sizeof(css_samples[0]))

/* ------------------------------------------------------------------ */
/* Builders                                                            */
/* ------------------------------------------------------------------ */

/**
 * Make a TOKEN value from @p text, borrowing @p source for its range.
 * Writes the token's byte range to @p out_range when non-NULL.
 */
static inline void css_value_token_at(robo_css_value_t *value,
                               const robo_css_source_t *source,
                               const char *text,
                               robo_css_range_t *out_range)
{
    robo_css_range_t range = robo_css_range_span(0u, strlen(text));
    robo_css_token_t token;

    ROBO_CHECK_EQ_INT(robo_css_token_make(&token, ROBO_CSS_TOKEN_IDENT, source, range,
                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_value_make_token(value, &token, NULL), ROBO_OK);
    if (out_range) {
        *out_range = range;
    }
}

/** Build `name(value)` as a FUNCTION value with one token child. */
static inline void css_value_function_with_token(robo_css_value_t *value,
                                          const robo_css_source_t *source,
                                          const char *name,
                                          const char *argument)
{
    robo_css_value_t child;

    ROBO_CHECK_EQ_INT(robo_css_value_make_function(value,
                                                   robo_css_slice_from_text(name),
                                                   robo_css_range_at(0u), NULL),
                      ROBO_OK);
    css_value_token_at(&child, source, argument, NULL);
    ROBO_CHECK_EQ_INT(robo_css_value_append(value, &child, NULL), ROBO_OK);
}

/**
 * Build a declaration named @p name (state PARSED) carrying @p value, and append
 * it to @p list. Convenience for the suites that care about lists, not about
 * declaration construction, which has its own tests.
 */
static inline void css_declaration_append(robo_css_declaration_list_t *list,
                                   const robo_css_source_t *source,
                                   const char *name,
                                   robo_css_value_t *value)
{
    robo_css_declaration_t declaration;

    (void)source;
    ROBO_CHECK_EQ_INT(robo_css_declaration_make(&declaration,
                                                robo_css_slice_from_text(name),
                                                ROBO_CSS_DECLARATION_STATE_PARSED, NULL),
                      ROBO_OK);
    if (value) {
        ROBO_CHECK_EQ_INT(robo_css_declaration_set_value(&declaration, value, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_declaration_list_append(list, &declaration, NULL),
                      ROBO_OK);
}

/** Build a complex selector `tag.class` (one compound, two simple selectors). */
static inline void css_complex_selector_of(robo_css_complex_selector_t *selector,
                                    const robo_css_source_t *source,
                                    const char *tag,
                                    const char *class_name)
{
    robo_css_compound_selector_t compound;
    robo_css_simple_selector_t simple;
    robo_css_range_t range = robo_css_range_span(0u, strlen(tag));

    (void)source;
    robo_css_compound_selector_init(&compound);
    ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                    ROBO_CSS_SIMPLE_SELECTOR_TYPE,
                                                    robo_css_slice_from_text(tag), NULL),
                      ROBO_OK);
    simple.range = range;
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                      ROBO_OK);
    if (class_name) {
        ROBO_CHECK_EQ_INT(robo_css_simple_selector_make(&simple,
                                                        ROBO_CSS_SIMPLE_SELECTOR_CLASS,
                                                        robo_css_slice_from_text(class_name),
                                                        NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_compound_selector_append(&compound, &simple, NULL),
                          ROBO_OK);
    }
    robo_css_compound_selector_seal_range(&compound);
    robo_css_complex_selector_init(selector);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_append_compound(selector, &compound,
                                                               NULL),
                      ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Allocator measurements                                              */
/* ------------------------------------------------------------------ */

/**
 * Enable the debug allocator if it is not already on, and report whether this
 * call turned it on (the caller must turn it off again if so, or every later
 * suite pays for bookkeeping it did not ask for).
 */
static inline int css_allocator_ensure_enabled(void)
{
    if (robo_mem_debug_is_enabled()) {
        return 0;
    }
    robo_mem_debug_enable();
    return 1;
}

/** Disable the debug allocator when @p we_enabled_it says we enabled it. */
static inline void css_allocator_restore(int we_enabled_it)
{
    if (we_enabled_it) {
        robo_mem_debug_disable();
    }
}

#endif /* ROBO_CSS_TEST_SUPPORT_H */
