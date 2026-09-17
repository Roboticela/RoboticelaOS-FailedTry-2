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
#include "RoboCSS/RoboCSSTokenizer.h"
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
/* Lexing (Phase 9.2)                                                  */
/* ------------------------------------------------------------------ */

/** Upper bound on the tokens a fixture in these suites may produce. */
#define CSS_TOK_MAX 256u

/**
 * A whole token stream, kept so a test can assert on the sequence rather than
 * on one token at a time.
 *
 * The tokens borrow the *source*, not the tokenizer that produced them, so a
 * log stays valid after its tokenizer is gone — which is a property worth
 * relying on (and worth testing: see css.tokenize.tokens_outlive_tokenizer).
 */
typedef struct css_token_log {
    size_t count;
    int truncated;      /* 1 when the fixture produced more than CSS_TOK_MAX */
    int eof_seen;       /* 1 when the stream ended on an EOF token */
    robo_css_error_t error;
    int has_error;
    robo_css_token_t tokens[CSS_TOK_MAX];
} css_token_log_t;

/**
 * Lex @p source completely into @p log.
 *
 * The tokenizer is local and is disposed before returning, which is the point:
 * the tokens in the log borrow @p source and nothing else. Returns the number
 * of tokens the stream consumed (EOF included).
 */
static inline size_t css_lex(const robo_css_source_t *source, css_token_log_t *log)
{
    /* Cleared, not merely declared. `init` releases what the record already
     * holds before it configures anything — that is what makes re-initializing
     * one safe — so an uninitialized record here would be read as if it were a
     * tokenizer: harmless in effect (the release only memsets) but an
     * indeterminate read, and it logs whatever was on the stack as though it
     * were a token count and an error kind. */
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    size_t consumed;

    css_fixture();
    memset(log, 0, sizeof(*log));
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, source, NULL), ROBO_OK);
    for (;;) {
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
        if (log->count < CSS_TOK_MAX) {
            log->tokens[log->count] = token;
        } else {
            log->truncated = 1;
        }
        log->count++;
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            log->eof_seen = 1;
            break;
        }
    }
    consumed = robo_css_tokenizer_tokens_produced(&tokenizer);
    if (robo_css_tokenizer_has_error(&tokenizer)) {
        log->has_error = 1;
        log->error = *robo_css_tokenizer_error(&tokenizer);
    }
    robo_css_tokenizer_dispose(&tokenizer);
    return consumed;
}

/** Lex @p text (borrowing the literal) into @p log. */
static inline size_t css_lex_text(robo_css_source_t *source, const char *text, css_token_log_t *log)
{
    css_source_of_string(source, text);
    return css_lex(source, log);
}

/**
 * The name @p type is written as inside an expected-sequence string.
 *
 * The canonical name comes from robo_css_token_type_name, and that is what the
 * vocabulary tests assert against. Inside a sequence string, though, the ten
 * types with the longest names are also the ones a fixture repeats most, and a
 * hint that compares forty tokens is unreadable when each one costs twenty
 * characters. Whitespace is shortened because it is the separator between
 * every other token; nothing else is, so a mismatch still names the class that
 * was wrong. The mapping stays one-to-one, which is the property that matters:
 * a misclassified token still shows up as a diff.
 */
static inline const char *css_kind_label(robo_css_token_type_t type)
{
    if (type == ROBO_CSS_TOKEN_WHITESPACE) {
        return "ws";
    }
    return robo_css_token_type_name(type);
}

/**
 * Write the log as `ident|colon|ws|...` into @p buffer, which must be
 * comfortably larger than CSS_TOK_MAX * 12. A whole sequence in one string is
 * what makes a mismatch readable: the assertion prints both sides.
 */
static inline void css_log_kinds(const css_token_log_t *log, char *buffer, size_t capacity)
{
    size_t i;
    size_t written = 0u;

    if (!buffer || capacity == 0u) {
        return;
    }
    buffer[0] = '\0';
    for (i = 0u; i < log->count && i < CSS_TOK_MAX; i++) {
        const char *name = css_kind_label(log->tokens[i].type);
        size_t need = strlen(name) + 2u;

        if (written + need >= capacity) {
            break;
        }
        if (i > 0u) {
            buffer[written++] = '|';
        }
        memcpy(buffer + written, name, strlen(name));
        written += strlen(name);
        buffer[written] = '\0';
    }
}

/**
 * Assert a double equals @p expected exactly, reporting both values.
 *
 * The framework's only numeric macro casts to `long long`, which would quietly
 * turn 0.5 into 0 — so a double comparison needs its own. This is a macro in the
 * shared test header, not new vocabulary in the framework, which is left
 * exactly as it is.
 */
#define CSS_CHECK_DOUBLE(actual, expected)                                     \
    do {                                                                       \
        double _css_da = (actual);                                             \
        double _css_de = (expected);                                           \
        if (!(_css_da == _css_de)) {                                           \
            robo_test_fail(__FILE__, __LINE__,                                 \
                           "%s == %s  (%.17g != %.17g)", #actual, #expected,   \
                           _css_da, _css_de);                                  \
        }                                                                      \
    } while (0)

/** Assert two doubles are within @p tolerance of each other. */
#define CSS_CHECK_DOUBLE_NEAR(actual, expected, tolerance)                     \
    do {                                                                       \
        double _css_na = (actual);                                             \
        double _css_ne = (expected);                                           \
        double _css_nt = (tolerance);                                          \
        double _css_nd = _css_na - _css_ne;                                    \
        if (_css_nd < 0.0) {                                                   \
            _css_nd = -_css_nd;                                                \
        }                                                                      \
        if (!(_css_nd <= _css_nt)) {                                           \
            robo_test_fail(__FILE__, __LINE__,                                 \
                           "%s ~= %s  (%.17g vs %.17g)", #actual, #expected,   \
                           _css_na, _css_ne);                                  \
        }                                                                      \
    } while (0)

/**
 * Non-zero when the log's token ranges tile `[0, length)` exactly: the first
 * token starts at 0, every token starts where the previous one ended, and the
 * last ends at @p length.
 *
 * This is the invariant that makes a source-preserving consumer possible, and
 * it is the one thing a tokenizer can get wrong invisibly: a token that eats a
 * byte it should not, or forgets one it should, still produces a plausible
 * stream. The EOF token is the empty range at the end and contributes nothing.
 */
static inline int css_log_tiles(const css_token_log_t *log, size_t length)
{
    size_t cursor = 0u;
    size_t i;

    if (!log || log->count == 0u || log->truncated) {
        return 0;
    }
    for (i = 0u; i < log->count && i < CSS_TOK_MAX; i++) {
        const robo_css_token_t *token = &log->tokens[i];

        if (token->type == ROBO_CSS_TOKEN_EOF) {
            return cursor == length;
        }
        if (token->range.begin != cursor || token->range.end < token->range.begin) {
            return 0;
        }
        cursor = token->range.end;
    }
    return 0;
}

/** The @p index'th token, or NULL when the log is shorter than that. */
static inline const robo_css_token_t *css_token_at(const css_token_log_t *log, size_t index)
{
    if (!log || index >= log->count || index >= CSS_TOK_MAX) {
        return NULL;
    }
    return &log->tokens[index];
}

/**
 * How a whole stream looked when it was read without being kept.
 */
typedef struct css_stream_audit {
    size_t total;                              /* tokens, EOF included */
    size_t by_type[ROBO_CSS_TOKEN_TYPE_COUNT]; /* one count per token type */
    int tiles;      /* ranges covered [0, length) exactly and ended at EOF */
    int eof_seen;
    int has_error;
} css_stream_audit_t;

/**
 * Lex @p source from end to end, counting types and checking the tiling, but
 * storing nothing.
 *
 * A log is what lets a test point at one token, and it is bounded by
 * CSS_TOK_MAX — which a two-hundred-rule sheet and a two-hundred-deep nest both
 * exceed. Those two properties are exactly the ones worth testing (the
 * tokenizer is a cursor, and its cost must not follow the input's depth), so
 * they are read through the stream instead: the same single pass a log would
 * have made, minus the storage. Counting and tiling in one walk also means the
 * audit cannot disagree with itself about what it saw.
 */
static inline void css_audit_stream(const robo_css_source_t *source,
                                   css_stream_audit_t *audit)
{
    robo_css_tokenizer_t tokenizer = ROBO_CSS_CLEARED;
    robo_css_token_t token = ROBO_CSS_CLEARED;
    size_t cursor = 0u;

    if (!audit) {
        return;
    }
    memset(audit, 0, sizeof(*audit));
    audit->tiles = 1;
    ROBO_CHECK_EQ_INT(robo_css_tokenizer_init(&tokenizer, source, NULL), ROBO_OK);
    for (;;) {
        ROBO_CHECK_EQ_INT(robo_css_tokenizer_next(&tokenizer, &token, NULL), ROBO_OK);
        if ((size_t)token.type < (size_t)ROBO_CSS_TOKEN_TYPE_COUNT) {
            audit->by_type[token.type]++;
        }
        audit->total++;
        if (token.type == ROBO_CSS_TOKEN_EOF) {
            audit->eof_seen = 1;
            if (token.range.begin != cursor) {
                audit->tiles = 0;
            }
            break;
        }
        if (token.range.begin != cursor || token.range.end <= token.range.begin) {
            audit->tiles = 0;
            /* A token that covered nothing would spin this loop forever, so a
             * regression stops the audit instead of hanging the suite. */
            if (token.range.end <= token.range.begin) {
                break;
            }
        }
        cursor = token.range.end;
    }
    if (cursor != robo_css_source_length(source)) {
        audit->tiles = 0;
    }
    audit->has_error = robo_css_tokenizer_has_error(&tokenizer);
    robo_css_tokenizer_dispose(&tokenizer);
}

/**
 * Number of tokens in @p log with type @p type.
 */
static inline size_t css_count_type(const css_token_log_t *log, robo_css_token_type_t type)
{
    size_t i;
    size_t total = 0u;

    for (i = 0u; i < log->count && i < CSS_TOK_MAX; i++) {
        if (log->tokens[i].type == type) {
            total++;
        }
    }
    return total;
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

/* ------------------------------------------------------------------ */
/* The parser (9.3)                                                    */
/* ------------------------------------------------------------------ */

/**
 * Parse @p text as a stylesheet and return the sheet's id.
 *
 * The source is *copied* into the sheet, so @p text may be a temporary — which
 * is what lets a suite call this with a string literal or a local buffer
 * without worrying about what the rules borrow.
 */
static inline robo_css_stylesheet_id_t css_parse_sheet(const char *text,
                                                       robo_css_error_t *out_error)
{
    robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_copy_source(id, (const uint8_t *)text,
                                                      strlen(text), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_parse(id, out_error, NULL), ROBO_OK);
    return id;
}

/**
 * Parse @p text as a declaration list into @p out.
 *
 * @p text must outlive @p out: the declarations borrow its bytes (see
 * robo_css_declaration_list_parse_text). Every suite here passes a literal or a
 * buffer that outlives the list.
 */
static inline robo_result_t css_parse_declarations(const char *text,
                                                   robo_css_declaration_list_t *out,
                                                   robo_css_error_t *out_error)
{
    css_fixture();
    return robo_css_declaration_list_parse_text(text, strlen(text),
                                                ROBO_CSS_ORIGIN_INLINE, out, out_error);
}

/** The style rule of @p sheet at @p index, or NULL when it is not a style rule. */
static inline const robo_css_style_rule_t *css_sheet_style_rule(
    robo_css_stylesheet_id_t sheet,
    size_t index)
{
    return robo_css_rule_style(robo_css_stylesheet_rule(sheet, index));
}

/** Non-zero when @p component is a token of @p type. */
static inline int css_component_is(const robo_css_value_t *component,
                                   robo_css_token_type_t type)
{
    return component && component->kind == ROBO_CSS_VALUE_KIND_TOKEN &&
           component->token.type == type;
}

/** Append @p text to @p out at @p *offset, NUL-terminating, never overrunning. */
static inline void css_shape_text(char *out, size_t capacity, size_t *offset,
                                  const char *text)
{
    while (text && *text && *offset + 1u < capacity) {
        out[(*offset)++] = *text++;
    }
    if (out && *offset < capacity) {
        out[*offset] = '\0';
    }
}

/** One byte of a shape: whitespace becomes `_` so a run is visible and exact. */
static inline void css_shape_byte(char *out, size_t capacity, size_t *offset, uint8_t b)
{
    if (*offset + 1u >= capacity) {
        return;
    }
    out[(*offset)++] = (b == (uint8_t)' ' || b == (uint8_t)'\t' || b == (uint8_t)'\n' ||
                        b == (uint8_t)'\r' || b == (uint8_t)'\f')
                           ? '_'
                           : (char)b;
    out[*offset] = '\0';
}

/**
 * Write a component-value tree as a one-line shape.
 *
 *   LIST       its children, comma-separated
 *   TOKEN      its raw text, whitespace folded to `_`
 *   FUNCTION   `name(children)`
 *   BLOCK      `(`/`[`/`{` then the children then the matching close
 *
 * One string pins a whole tree, which is what makes a value assertion readable
 * *and* failure-sensitive: a missing whitespace token, a mis-nested function or
 * a trimmed-away leaf all change the string. The tokenizer suite's `css_log_kinds`
 * exists for the same reason one level down.
 */
static inline void css_shape_write(const robo_css_value_t *value, char *out,
                                   size_t capacity, size_t *offset)
{
    size_t index;
    char open = '(';
    char close = ')';

    if (!value || !out) {
        return;
    }
    switch (value->kind) {
    case ROBO_CSS_VALUE_KIND_LIST:
        for (index = 0u; index < value->children.count; index++) {
            if (index > 0u) {
                css_shape_text(out, capacity, offset, ",");
            }
            css_shape_write(&value->children.items[index], out, capacity, offset);
        }
        return;
    case ROBO_CSS_VALUE_KIND_TOKEN:
        for (index = 0u; index < value->token.text.length; index++) {
            css_shape_byte(out, capacity, offset, value->token.text.bytes[index]);
        }
        return;
    case ROBO_CSS_VALUE_KIND_FUNCTION:
        for (index = 0u; index < value->name.length; index++) {
            css_shape_byte(out, capacity, offset, value->name.bytes[index]);
        }
        break;
    case ROBO_CSS_VALUE_KIND_BLOCK:
        if (value->open == ROBO_CSS_TOKEN_LEFT_SQUARE) {
            open = '[';
            close = ']';
        } else if (value->open == ROBO_CSS_TOKEN_LEFT_CURLY) {
            open = '{';
            close = '}';
        }
        break;
    default:
        return;
    }
    css_shape_byte(out, capacity, offset, (uint8_t)open);
    for (index = 0u; index < value->children.count; index++) {
        if (index > 0u) {
            css_shape_text(out, capacity, offset, ",");
        }
        css_shape_write(&value->children.items[index], out, capacity, offset);
    }
    css_shape_byte(out, capacity, offset, (uint8_t)close);
}

/**
 * The shape of @p value written into @p out, which must hold @p capacity bytes.
 * A shape longer than the buffer is truncated, and the assertion that read it
 * then shows the truncation, which is why 256 bytes is worth asking for.
 */
static inline void css_shape(const robo_css_value_t *value, char *out, size_t capacity)
{
    size_t offset = 0u;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    css_shape_write(value, out, capacity, &offset);
}

/** The shape of the @p index'th declaration's value in @p list ("" when absent). */
static inline void css_declaration_shape(const robo_css_declaration_list_t *list,
                                         size_t index, char *out, size_t capacity)
{
    const robo_css_declaration_t *declaration = robo_css_declaration_list_at(list, index);

    css_shape(declaration ? robo_css_declaration_value(declaration) : NULL, out, capacity);
}

/** The shape of a style rule's prelude. */
static inline void css_prelude_shape(const robo_css_style_rule_t *rule, char *out,
                                     size_t capacity)
{
    css_shape(rule ? robo_css_style_rule_prelude_values(rule) : NULL, out, capacity);
}

/**
 * Non-zero when the @p index'th declaration of @p rule is named @p name.
 *
 * A one-line wrapper for the assertion recovery tests make over and over —
 * "the surviving rule kept *its* declaration" — because spelling it out at each
 * site buries the fact being pinned under three levels of accessor.
 */
static inline int css_parse_declaration_is(const robo_css_style_rule_t *rule,
                                           size_t index, const char *name)
{
    return robo_css_slice_equals_ascii(
        robo_css_declaration_name(robo_css_style_rule_declaration(rule, index)), name);
}

/** The property name of a declaration in @p list as a NUL-terminated string. */
static inline void css_declaration_name_of(const robo_css_declaration_list_t *list,
                                           size_t index, char *out, size_t capacity)
{
    const robo_css_declaration_t *declaration = robo_css_declaration_list_at(list, index);
    size_t offset = 0u;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (declaration) {
        robo_css_slice_t name = robo_css_declaration_name(declaration);
        size_t i;

        for (i = 0u; i < name.length; i++) {
            css_shape_byte(out, capacity, &offset, name.bytes[i]);
        }
    }
}

#endif /* ROBO_CSS_TEST_SUPPORT_H */
