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
#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSCascade.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSDeclaration.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSParser.h"
#include "RoboCSS/RoboCSSMatcher.h"
#include "RoboCSS/RoboCSSProperty.h"
#include "RoboCSS/RoboCSSPropertyRegistry.h"
#include "RoboCSS/RoboCSSInherit.h"
#include "RoboCSS/RoboCSSRule.h"
#include "RoboCSS/RoboCSSSelector.h"
#include "RoboCSS/RoboCSSSelectorParser.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSSpecificity.h"
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
static inline void CssFixture(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!CssIsInitialized()) {
        RoboCheckEqInt(CssInit(), RoboOk);
    }
}

/**
 * Borrow @p text as a source. The literal outlives every test that uses it, so
 * the borrowed kind is the right one here — and it also means a suite that
 * forgets to dispose cannot corrupt a later test's allocator baseline.
 */
static inline void CssSourceOfString(CssSource *source, const char *text)
{
    CssFixture();
    RoboCheckEqInt(CssSourceSetString(source, text, NULL), RoboOk);
}

/* The Phase 9.1 fixture corpus, verbatim from the phase specification. */
#define CSS_FIXTURE_PARAGRAPH "p {\n    color: red;\n}\n"
#define CSS_FIXTURE_CLASS ".button {\n    padding: 10px;\n}\n"
#define CSS_FIXTURE_ID "#title {\n    font-size: 24px;\n}\n"
#define CSS_FIXTURE_ATTRIBUTE "input[type=\"text\"] {\n    width: 200px;\n}\n"
#define CSS_FIXTURE_BODY "body {\n    background: white;\n}\n"

/** One named sample of text whose bytes must survive a round trip. */
typedef struct cssTextSample {
    const char *label;
    const char *text;
} CssTextSample;

/**
 * The thirteen families Phase 9.1.21 names. UTF-8 literals: the suite checks
 * that bytes are preserved, not that they are shaped or normalised.
 */
static const CssTextSample cssSamples[] = {
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

#define CSS_SAMPLE_COUNT (sizeof(cssSamples) / sizeof(cssSamples[0]))

/* ------------------------------------------------------------------ */
/* Builders                                                            */
/* ------------------------------------------------------------------ */

/**
 * Point @p source at @p text without copying it.
 *
 * @p source must be zeroed first (`RoboCssCleared`). A memory source borrows,
 * so nothing owns the bytes and nothing has to outlive anything except the
 * literal itself — which is why it is the right way to give a hand-built token
 * something real to point at.
 */
static inline void CssSourceOfLiteral(CssSource *source, const char *text)
{
    RoboCheckEqInt(CssSourceSetMemory(source, (const uint8_t *)text,
                                                 strlen(text), NULL),
                      RoboOk);
}

/**
 * Make a token of @p type covering @p text, borrowing @p source.
 *
 * A token is bound to a source and a range inside it; one built without a source
 * is not a token and the constructor refuses it. Hand-built tokens therefore go
 * through a real source exactly as parsed ones do. @p source must outlive the
 * token's slice.
 */
static inline void CssTokenOfText(CssToken *token,
                                     CssTokenType type,
                                     const CssSource *source,
                                     const char *text)
{
    RoboCheckEqInt(CssTokenMake(token, type, source,
                                          CssRangeSpan(0u, strlen(text)), NULL),
                      RoboOk);
}

/**
 * Make a TOKEN value from @p text, borrowing @p source for its range.
 * Writes the token's byte range to @p outRange when non-NULL.
 */
static inline void CssValueTokenAt(CssValue *value,
                               const CssSource *source,
                               const char *text,
                               CssRange *outRange)
{
    CssRange range = CssRangeSpan(0u, strlen(text));
    CssToken token;

    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, source, range,
                                          NULL),
                      RoboOk);
    RoboCheckEqInt(CssValueMakeToken(value, &token, NULL), RoboOk);
    if (outRange) {
        *outRange = range;
    }
}

/** Build `name(value)` as a FUNCTION value with one token child. */
static inline void CssValueFunctionWithToken(CssValue *value,
                                          const CssSource *source,
                                          const char *name,
                                          const char *argument)
{
    CssValue child;

    RoboCheckEqInt(CssValueMakeFunction(value,
                                                   CssSliceFromText(name),
                                                   CssRangeAt(0u), NULL),
                      RoboOk);
    CssValueTokenAt(&child, source, argument, NULL);
    RoboCheckEqInt(CssValueAppend(value, &child, NULL), RoboOk);
}

/**
 * Build a declaration named @p name (state PARSED) carrying @p value, and append
 * it to @p list. Convenience for the suites that care about lists, not about
 * declaration construction, which has its own tests.
 */
static inline void CssDeclarationAppend(CssDeclarationList *list,
                                   const CssSource *source,
                                   const char *name,
                                   CssValue *value)
{
    CssDeclaration declaration;

    (void)source;
    RoboCheckEqInt(CssDeclarationMake(&declaration,
                                                CssSliceFromText(name),
                                                RoboCssDeclarationStateParsed, NULL),
                      RoboOk);
    if (value) {
        RoboCheckEqInt(CssDeclarationSetValue(&declaration, value, NULL),
                          RoboOk);
    }
    RoboCheckEqInt(CssDeclarationListAppend(list, &declaration, NULL),
                      RoboOk);
}

/** Build a complex selector `tag.class` (one compound, two simple selectors). */
static inline void CssComplexSelectorOf(CssComplexSelector *selector,
                                    const CssSource *source,
                                    const char *tag,
                                    const char *className)
{
    CssCompoundSelector compound;
    CssSimpleSelector simple;
    CssRange range = CssRangeSpan(0u, strlen(tag));

    (void)source;
    CssCompoundSelectorInit(&compound);
    RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                    RoboCssSimpleSelectorType,
                                                    CssSliceFromText(tag), NULL),
                      RoboOk);
    simple.range = range;
    RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                      RoboOk);
    if (className) {
        RoboCheckEqInt(CssSimpleSelectorMake(&simple,
                                                        RoboCssSimpleSelectorClass,
                                                        CssSliceFromText(className),
                                                        NULL),
                          RoboOk);
        RoboCheckEqInt(CssCompoundSelectorAppend(&compound, &simple, NULL),
                          RoboOk);
    }
    CssCompoundSelectorSealRange(&compound);
    CssComplexSelectorInit(selector);
    RoboCheckEqInt(CssComplexSelectorAppendCompound(selector, &compound,
                                                               NULL),
                      RoboOk);
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
typedef struct cssTokenLog {
    size_t count;
    int truncated;      /* 1 when the fixture produced more than CSS_TOK_MAX */
    int eofSeen;       /* 1 when the stream ended on an EOF token */
    CssError error;
    int hasError;
    CssToken tokens[CSS_TOK_MAX];
} CssTokenLog;

/**
 * Lex @p source completely into @p log.
 *
 * The tokenizer is local and is disposed before returning, which is the point:
 * the tokens in the log borrow @p source and nothing else. Returns the number
 * of tokens the stream consumed (EOF included).
 */
static inline size_t CssLex(const CssSource *source, CssTokenLog *log)
{
    /* Cleared, not merely declared. `init` releases what the record already
     * holds before it configures anything — that is what makes re-initializing
     * one safe — so an uninitialized record here would be read as if it were a
     * tokenizer: harmless in effect (the release only memsets) but an
     * indeterminate read, and it logs whatever was on the stack as though it
     * were a token count and an error kind. */
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    size_t consumed;

    CssFixture();
    memset(log, 0, sizeof(*log));
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, source, NULL), RoboOk);
    for (;;) {
        RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
        if (log->count < CSS_TOK_MAX) {
            log->tokens[log->count] = token;
        } else {
            log->truncated = 1;
        }
        log->count++;
        if (token.type == RoboCssTokenEof) {
            log->eofSeen = 1;
            break;
        }
    }
    consumed = CssTokenizerTokensProduced(&tokenizer);
    if (CssTokenizerHasError(&tokenizer)) {
        log->hasError = 1;
        log->error = *CssTokenizerError(&tokenizer);
    }
    CssTokenizerDispose(&tokenizer);
    return consumed;
}

/** Lex @p text (borrowing the literal) into @p log. */
static inline size_t CssLexText(CssSource *source, const char *text, CssTokenLog *log)
{
    CssSourceOfString(source, text);
    return CssLex(source, log);
}

/**
 * The name @p type is written as inside an expected-sequence string.
 *
 * The canonical name comes from CssTokenTypeName, and that is what the
 * vocabulary tests assert against. Inside a sequence string, though, the ten
 * types with the longest names are also the ones a fixture repeats most, and a
 * hint that compares forty tokens is unreadable when each one costs twenty
 * characters. Whitespace is shortened because it is the separator between
 * every other token; nothing else is, so a mismatch still names the class that
 * was wrong. The mapping stays one-to-one, which is the property that matters:
 * a misclassified token still shows up as a diff.
 */
static inline const char *CssKindLabel(CssTokenType type)
{
    if (type == RoboCssTokenWhitespace) {
        return "ws";
    }
    return CssTokenTypeName(type);
}

/**
 * Write the log as `ident|colon|ws|...` into @p buffer, which must be
 * comfortably larger than CSS_TOK_MAX * 12. A whole sequence in one string is
 * what makes a mismatch readable: the assertion prints both sides.
 */
static inline void CssLogKinds(const CssTokenLog *log, char *buffer, size_t capacity)
{
    size_t i;
    size_t written = 0u;

    if (!buffer || capacity == 0u) {
        return;
    }
    buffer[0] = '\0';
    for (i = 0u; i < log->count && i < CSS_TOK_MAX; i++) {
        const char *name = CssKindLabel(log->tokens[i].type);
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
        double cssDa = (actual);                                             \
        double cssDe = (expected);                                           \
        if (!(cssDa == cssDe)) {                                           \
            TestFail(__FILE__, __LINE__,                                 \
                           "%s == %s  (%.17g != %.17g)", #actual, #expected,   \
                           cssDa, cssDe);                                  \
        }                                                                      \
    } while (0)

/** Assert two doubles are within @p tolerance of each other. */
#define CSS_CHECK_DOUBLE_NEAR(actual, expected, tolerance)                     \
    do {                                                                       \
        double cssNa = (actual);                                             \
        double cssNe = (expected);                                           \
        double cssNt = (tolerance);                                          \
        double cssNd = cssNa - cssNe;                                    \
        if (cssNd < 0.0) {                                                   \
            cssNd = -cssNd;                                                \
        }                                                                      \
        if (!(cssNd <= cssNt)) {                                           \
            TestFail(__FILE__, __LINE__,                                 \
                           "%s ~= %s  (%.17g vs %.17g)", #actual, #expected,   \
                           cssNa, cssNe);                                  \
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
static inline int CssLogTiles(const CssTokenLog *log, size_t length)
{
    size_t cursor = 0u;
    size_t i;

    if (!log || log->count == 0u || log->truncated) {
        return 0;
    }
    for (i = 0u; i < log->count && i < CSS_TOK_MAX; i++) {
        const CssToken *token = &log->tokens[i];

        if (token->type == RoboCssTokenEof) {
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
static inline const CssToken *CssTokenAt(const CssTokenLog *log, size_t index)
{
    if (!log || index >= log->count || index >= CSS_TOK_MAX) {
        return NULL;
    }
    return &log->tokens[index];
}

/**
 * How a whole stream looked when it was read without being kept.
 */
typedef struct cssStreamAudit {
    size_t total;                              /* tokens, EOF included */
    size_t byType[RoboCssTokenTypeCount]; /* one count per token type */
    int tiles;      /* ranges covered [0, length) exactly and ended at EOF */
    int eofSeen;
    int hasError;
} CssStreamAudit;

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
static inline void CssAuditStream(const CssSource *source,
                                   CssStreamAudit *audit)
{
    CssTokenizer tokenizer = RoboCssCleared;
    CssToken token = RoboCssCleared;
    size_t cursor = 0u;

    if (!audit) {
        return;
    }
    memset(audit, 0, sizeof(*audit));
    audit->tiles = 1;
    RoboCheckEqInt(CssTokenizerInit(&tokenizer, source, NULL), RoboOk);
    for (;;) {
        RoboCheckEqInt(CssTokenizerNext(&tokenizer, &token, NULL), RoboOk);
        if ((size_t)token.type < (size_t)RoboCssTokenTypeCount) {
            audit->byType[token.type]++;
        }
        audit->total++;
        if (token.type == RoboCssTokenEof) {
            audit->eofSeen = 1;
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
    if (cursor != CssSourceLength(source)) {
        audit->tiles = 0;
    }
    audit->hasError = CssTokenizerHasError(&tokenizer);
    CssTokenizerDispose(&tokenizer);
}

/**
 * Number of tokens in @p log with type @p type.
 */
static inline size_t CssCountType(const CssTokenLog *log, CssTokenType type)
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
static inline int CssAllocatorEnsureEnabled(void)
{
    if (MemDebugIsEnabled()) {
        return 0;
    }
    MemDebugEnable();
    return 1;
}

/** Disable the debug allocator when @p weEnabledIt says we enabled it. */
static inline void CssAllocatorRestore(int weEnabledIt)
{
    if (weEnabledIt) {
        MemDebugDisable();
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
static inline CssStylesheetId CssParseSheet(const char *text,
                                                       CssError *outError)
{
    CssStylesheetId id = RoboCssStylesheetIdNone;

    CssFixture();
    RoboCheckEqInt(CssStylesheetCreate(&id, RoboCssOriginAuthor, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetCopySource(id, (const uint8_t *)text,
                                                      strlen(text), NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetParse(id, outError, NULL), RoboOk);
    return id;
}

/**
 * Parse @p text as a declaration list into @p out.
 *
 * @p text must outlive @p out: the declarations borrow its bytes (see
 * CssDeclarationListParseText). Every suite here passes a literal or a
 * buffer that outlives the list.
 */
static inline Result CssParseDeclarations(const char *text,
                                                   CssDeclarationList *out,
                                                   CssError *outError)
{
    CssFixture();
    return CssDeclarationListParseText(text, strlen(text),
                                                RoboCssOriginInline, out, outError);
}

/** The style rule of @p sheet at @p index, or NULL when it is not a style rule. */
static inline const CssStyleRule *CssSheetStyleRule(
    CssStylesheetId sheet,
    size_t index)
{
    return CssRuleStyle(CssStylesheetRule(sheet, index));
}

/** Non-zero when @p component is a token of @p type. */
static inline int CssComponentIs(const CssValue *component,
                                   CssTokenType type)
{
    return component && component->kind == RoboCssValueKindToken &&
           component->token.type == type;
}

/** Append @p text to @p out at @p *offset, NUL-terminating, never overrunning. */
static inline void CssShapeText(char *out, size_t capacity, size_t *offset,
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
static inline void CssShapeByte(char *out, size_t capacity, size_t *offset, uint8_t b)
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
 * a trimmed-away leaf all change the string. The tokenizer suite's `CssLogKinds`
 * exists for the same reason one level down.
 */
static inline void CssShapeWrite(const CssValue *value, char *out,
                                   size_t capacity, size_t *offset)
{
    size_t index;
    char open = '(';
    char close = ')';

    if (!value || !out) {
        return;
    }
    switch (value->kind) {
    case RoboCssValueKindList:
        for (index = 0u; index < value->children.count; index++) {
            if (index > 0u) {
                CssShapeText(out, capacity, offset, ",");
            }
            CssShapeWrite(&value->children.items[index], out, capacity, offset);
        }
        return;
    case RoboCssValueKindToken:
        for (index = 0u; index < value->token.text.length; index++) {
            CssShapeByte(out, capacity, offset, value->token.text.bytes[index]);
        }
        return;
    case RoboCssValueKindFunction:
        for (index = 0u; index < value->name.length; index++) {
            CssShapeByte(out, capacity, offset, value->name.bytes[index]);
        }
        break;
    case RoboCssValueKindBlock:
        if (value->open == RoboCssTokenLeftSquare) {
            open = '[';
            close = ']';
        } else if (value->open == RoboCssTokenLeftCurly) {
            open = '{';
            close = '}';
        }
        break;
    default:
        return;
    }
    CssShapeByte(out, capacity, offset, (uint8_t)open);
    for (index = 0u; index < value->children.count; index++) {
        if (index > 0u) {
            CssShapeText(out, capacity, offset, ",");
        }
        CssShapeWrite(&value->children.items[index], out, capacity, offset);
    }
    CssShapeByte(out, capacity, offset, (uint8_t)close);
}

/**
 * The shape of @p value written into @p out, which must hold @p capacity bytes.
 * A shape longer than the buffer is truncated, and the assertion that read it
 * then shows the truncation, which is why 256 bytes is worth asking for.
 */
static inline void CssShape(const CssValue *value, char *out, size_t capacity)
{
    size_t offset = 0u;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    CssShapeWrite(value, out, capacity, &offset);
}

/** The shape of the @p index'th declaration's value in @p list ("" when absent). */
static inline void CssDeclarationShape(const CssDeclarationList *list,
                                         size_t index, char *out, size_t capacity)
{
    const CssDeclaration *declaration = CssDeclarationListAt(list, index);

    CssShape(declaration ? CssDeclarationValue(declaration) : NULL, out, capacity);
}

/** The shape of a style rule's prelude. */
static inline void CssPreludeShape(const CssStyleRule *rule, char *out,
                                     size_t capacity)
{
    CssShape(rule ? CssStyleRulePreludeValues(rule) : NULL, out, capacity);
}

/**
 * Non-zero when the @p index'th declaration of @p rule is named @p name.
 *
 * A one-line wrapper for the assertion recovery tests make over and over —
 * "the surviving rule kept *its* declaration" — because spelling it out at each
 * site buries the fact being pinned under three levels of accessor.
 */
static inline int CssParseDeclarationIs(const CssStyleRule *rule,
                                           size_t index, const char *name)
{
    return CssSliceEqualsAscii(
        CssDeclarationName(CssStyleRuleDeclaration(rule, index)), name);
}

/** The property name of a declaration in @p list as a NUL-terminated string. */
static inline void CssDeclarationNameOf(const CssDeclarationList *list,
                                           size_t index, char *out, size_t capacity)
{
    const CssDeclaration *declaration = CssDeclarationListAt(list, index);
    size_t offset = 0u;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (declaration) {
        CssSlice name = CssDeclarationName(declaration);
        size_t i;

        for (i = 0u; i < name.length; i++) {
            CssShapeByte(out, capacity, &offset, name.bytes[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Selector parsing (Phase 9.4)                                        */
/* ------------------------------------------------------------------ */

/** Parse @p text, asserting it is a valid selector list. */
static inline void CssSelectParse(const char *text, CssSelectorList *out)
{
    CssError error;

    CssFixture();
    CssErrorInit(&error);
    RoboCheckEqInt(CssSelectorListParseText(out, text, &error, NULL),
                      RoboOk);
    RoboCheckEqInt(CssErrorIsError(&error), 0);
}

/**
 * Parse @p text, asserting it is *not* a valid selector list. Returns the
 * recorded error kind so a test can pin which problem was reported rather than
 * merely that one was.
 */
static inline CssErrorKind CssSelectParseRejected(const char *text)
{
    CssSelectorList list = RoboCssCleared;
    CssError error;

    CssFixture();
    CssErrorInit(&error);
    RoboCheckEqInt(CssSelectorListParseText(&list, text, &error, NULL),
                      RoboParseError);
    RoboCheckEqInt(CssErrorIsError(&error), 1);
    RoboCheckEqInt(CssSelectorListCount(&list), 0u);
    CssSelectorListDispose(&list);
    return error.kind;
}

/** The @p index'th simple selector of a list's @p listIndex'th complex
 *  selector's @p compoundIndex'th compound selector, or NULL. */
static inline const CssSimpleSelector *CssSelectSimple(
    const CssSelectorList *list,
    size_t listIndex,
    size_t compoundIndex,
    size_t simpleIndex)
{
    const CssComplexSelector *complex =
        CssSelectorListAt(list, listIndex);
    const CssCompoundSelector *compound =
        CssComplexSelectorCompoundAt(complex, compoundIndex);

    return CssCompoundSelectorAt(compound, simpleIndex);
}

/** Number of simple selectors in the first compound of the first selector. */
static inline size_t CssSelectFirstCompoundCount(const CssSelectorList *list)
{
    const CssComplexSelector *complex = CssSelectorListAt(list, 0u);
    const CssCompoundSelector *compound =
        CssComplexSelectorCompoundAt(complex, 0u);

    return CssCompoundSelectorCount(compound);
}

/**
 * Parse @p text and return the specificity of its @p index'th complex selector.
 * The one-line door the specificity suites read every case through.
 */
static inline CssSpecificity CssSpecificityOf(const char *text, size_t index)
{
    CssSelectorList list;
    CssSpecificity specificity;

    CssSelectParse(text, &list);
    specificity = CssSpecificityOfListItem(&list, index);
    CssSelectorListDispose(&list);
    return specificity;
}

/** Assert @p specificity is exactly (id, classLike, typeLike). */
static inline void CssSpecificityIs(CssSpecificity specificity,
                                      uint32_t id,
                                      uint32_t classLike,
                                      uint32_t typeLike)
{
    RoboCheckEqInt(specificity.id, id);
    RoboCheckEqInt(specificity.classLike, classLike);
    RoboCheckEqInt(specificity.typeLike, typeLike);
}

/** Write @p slice into @p out as a NUL-terminated string (truncating). */
static inline void CssSelectText(CssSlice slice, char *out, size_t capacity)
{
    size_t i;

    if (!out || capacity == 0u) {
        return;
    }
    for (i = 0u; i + 1u < capacity && i < slice.length; i++) {
        out[i] = (char)slice.bytes[i];
    }
    out[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* The element host test double (Phase 9.4)                            */
/* ------------------------------------------------------------------ */

/*
 * A host, not a DOM.
 *
 * RoboCSS matches through callbacks (RoboCSSStyle.h), so the matcher can be
 * tested exhaustively — every combinator, every structural pseudo-class, every
 * boundary — without a document, and the DOM integration suite separately proves
 * the same results against the real RoboHTML tree. What this holds is exactly
 * what a view holds: a tag, an id, a class list, an attribute list, and three
 * relations. It has no node kinds, no text, no payload arena and no tree
 * operations, which is what keeps it a test double for a callback interface
 * rather than a second DOM in a test file.
 *
 * Refs are `index + 1`, so 0 stays "no element" as the seam requires.
 */

#define CSS_HOST_MAX_ELEMENTS 24u
#define CSS_HOST_MAX_ATTRIBUTES 6u
#define CSS_HOST_MAX_CLASSES 4u

typedef struct cssHostElement {
    const char *tag;
    const char *id;
    CssSlice classes[CSS_HOST_MAX_CLASSES];
    size_t classCount;
    CssAttribute attributes[CSS_HOST_MAX_ATTRIBUTES];
    size_t attributeCount;
    CssElementRef parent;
    CssElementRef previous;
    CssElementRef next;
} CssHostElement;

typedef struct cssHost {
    CssHostElement elements[CSS_HOST_MAX_ELEMENTS];
    size_t count;
    /* The seam, ready to hand to the matcher or install in a style context. */
    CssElementHost host;
} CssHost;

static inline Result CssHostViewOf(void *userdata,
                                             CssElementRef ref,
                                             CssElementView *out)
{
    CssHost *host = (CssHost *)userdata;
    CssHostElement *element;

    if (!host || !out || ref == RoboCssElementRefNone ||
        (size_t)(ref - 1u) >= host->count) {
        return RoboNotFound;
    }
    element = &host->elements[ref - 1u];
    CssElementViewInit(out);
    RoboCheckEqInt(CssElementViewSetElement(
                          out, ref, CssSliceFromText(element->tag), NULL),
                      RoboOk);
    if (element->id) {
        RoboCheckEqInt(CssElementViewSetId(
                              out, CssSliceFromText(element->id), NULL),
                          RoboOk);
    }
    RoboCheckEqInt(CssElementViewSetClasses(out, element->classes,
                                                       element->classCount, NULL),
                      RoboOk);
    RoboCheckEqInt(CssElementViewSetAttributes(out, element->attributes,
                                                          element->attributeCount, NULL),
                      RoboOk);
    return RoboOk;
}

static inline CssElementRef CssHostParentOf(void *userdata,
                                                        CssElementRef ref)
{
    CssHost *host = (CssHost *)userdata;

    if (!host || ref == RoboCssElementRefNone || (size_t)(ref - 1u) >= host->count) {
        return RoboCssElementRefNone;
    }
    return host->elements[ref - 1u].parent;
}

static inline CssElementRef CssHostPreviousOf(void *userdata,
                                                          CssElementRef ref)
{
    CssHost *host = (CssHost *)userdata;

    if (!host || ref == RoboCssElementRefNone || (size_t)(ref - 1u) >= host->count) {
        return RoboCssElementRefNone;
    }
    return host->elements[ref - 1u].previous;
}

static inline CssElementRef CssHostNextOf(void *userdata,
                                                      CssElementRef ref)
{
    CssHost *host = (CssHost *)userdata;

    if (!host || ref == RoboCssElementRefNone || (size_t)(ref - 1u) >= host->count) {
        return RoboCssElementRefNone;
    }
    return host->elements[ref - 1u].next;
}

/** Reset @p host to empty and install the four callbacks. */
static inline void CssHostInit(CssHost *host)
{
    CssFixture();
    memset(host, 0, sizeof(*host));
    CssElementHostInit(&host->host);
    host->host.userdata = host;
    host->host.view = CssHostViewOf;
    host->host.parent = CssHostParentOf;
    host->host.PreviousSibling = CssHostPreviousOf;
    host->host.NextSibling = CssHostNextOf;
}

/** Add an element and return its ref. @p classes is whitespace-separated. */
static inline CssElementRef CssHostAdd(CssHost *host,
                                                  const char *tag,
                                                  const char *id,
                                                  const char *classes)
{
    CssHostElement *element;
    const char *at;

    RoboCheckEqInt(host->count < CSS_HOST_MAX_ELEMENTS, 1);
    element = &host->elements[host->count];
    memset(element, 0, sizeof(*element));
    element->tag = tag;
    element->id = id;

    at = classes;
    while (at && *at && element->classCount < CSS_HOST_MAX_CLASSES) {
        const char *begin;

        while (*at == ' ') {
            at++;
        }
        if (!*at) {
            break;
        }
        begin = at;
        while (*at && *at != ' ') {
            at++;
        }
        element->classes[element->classCount] =
            CssSliceFromBytes((const uint8_t *)begin, (size_t)(at - begin));
        element->classCount++;
    }
    host->count++;
    return (CssElementRef)host->count;
}

/** Attach an attribute to @p ref. Both strings must outlive the host's use. */
static inline void CssHostAttribute(CssHost *host,
                                      CssElementRef ref,
                                      const char *name,
                                      const char *value)
{
    CssHostElement *element;

    RoboCheckEqInt(ref != RoboCssElementRefNone &&
                          (size_t)(ref - 1u) < host->count,
                      1);
    element = &host->elements[ref - 1u];
    RoboCheckEqInt(element->attributeCount < CSS_HOST_MAX_ATTRIBUTES, 1);
    element->attributes[element->attributeCount].name = CssSliceFromText(name);
    element->attributes[element->attributeCount].value =
        CssSliceFromText(value ? value : "");
    element->attributeCount++;
}

/** Make @p child a child of @p parent. */
static inline void CssHostSetParent(CssHost *host,
                                       CssElementRef child,
                                       CssElementRef parent)
{
    RoboCheckEqInt(child != RoboCssElementRefNone &&
                          (size_t)(child - 1u) < host->count,
                      1);
    host->elements[child - 1u].parent = parent;
}

/**
 * Derive the sibling chain of every parent from insertion order, which is how
 * these fixtures are written: elements are added parent-first, then their
 * children in the order they appear. Call once after the last set_parent.
 *
 * An element with no parent has no siblings. That is not a detail of the
 * double: two root elements are not siblings in any document, and treating them
 * as such would make `:first-child` false for the second root and for the first
 * — which is exactly the bug this guard prevents here.
 */
static inline void CssHostSeal(CssHost *host)
{
    size_t i;

    for (i = 0u; i < host->count; i++) {
        size_t j;

        host->elements[i].previous = RoboCssElementRefNone;
        host->elements[i].next = RoboCssElementRefNone;
        if (host->elements[i].parent == RoboCssElementRefNone) {
            continue;
        }
        for (j = 0u; j < i; j++) {
            if (host->elements[j].parent == host->elements[i].parent) {
                host->elements[i].previous = (CssElementRef)(j + 1u);
            }
        }
        for (j = i + 1u; j < host->count; j++) {
            if (host->elements[j].parent == host->elements[i].parent) {
                host->elements[i].next = (CssElementRef)(j + 1u);
                break;
            }
        }
    }
}

/**
 * Match @p text against @p ref, filling @p out with everything the matcher
 * found. Used when the test is about `unsupported`, `condition`, `selectorIndex`
 * or `range` rather than only about the verdict.
 */
static inline int CssMatchFull(CssHost *host,
                                 const char *text,
                                 CssElementRef ref,
                                 CssSelectorMatch *out)
{
    CssSelectorList list = RoboCssCleared;
    int matched;

    CssSelectParse(text, &list);
    CssSelectorMatchInit(out);
    matched = CssSelectorListMatches(&list, &host->host, ref, out, NULL);
    CssSelectorListDispose(&list);
    return matched;
}

/**
 * Match @p text against @p ref and copy the reported `condition` into @p name.
 *
 * `condition` is the one field of the result that *borrows* — it points into the
 * selector list's name arena — so it has to be read before the list is released.
 * The other fields are values and survive the dispose. A test that read the
 * slice after this helper returned would be reading freed bytes.
 */
static inline int CssMatchCondition(CssHost *host,
                                      const char *text,
                                      CssElementRef ref,
                                      CssSelectorMatch *out,
                                      char *name,
                                      size_t capacity)
{
    CssSelectorList list = RoboCssCleared;
    int matched;

    CssSelectParse(text, &list);
    CssSelectorMatchInit(out);
    matched = CssSelectorListMatches(&list, &host->host, ref, out, NULL);
    if (name && capacity > 0u) {
        CssSelectText(out->condition, name, capacity);
    }
    CssSelectorListDispose(&list);
    return matched;
}

/**
 * Match @p text against @p ref, asserting it was decided one way or the other.
 *
 * The `unsupported == 0` assertion is the important half: a negative that means
 * "not decided" would pass a plain `matched == 0` test while quietly leaving a
 * rule unapplied.
 */
static inline int CssMatch(CssHost *host, const char *text, CssElementRef ref)
{
    CssSelectorMatch result;
    int matched = CssMatchFull(host, text, ref, &result);

    RoboCheckEqInt(result.unsupported, 0);
    return matched;
}

/** Which relation a host is to be left without. */
typedef enum cssHostRelation {
    CSS_HOST_RELATION_PARENT = 0,
    CSS_HOST_RELATION_PREVIOUS = 1,
    CSS_HOST_RELATION_NEXT = 2
} CssHostRelation;

/**
 * Remove one callback from @p host, modelling a seam that cannot answer that
 * relation at all — a host with only a view, for instance.
 */
static inline void CssHostDropRelation(CssHost *host, CssHostRelation which)
{
    switch (which) {
    case CSS_HOST_RELATION_PARENT:
        host->host.parent = NULL;
        break;
    case CSS_HOST_RELATION_PREVIOUS:
        host->host.PreviousSibling = NULL;
        break;
    case CSS_HOST_RELATION_NEXT:
        host->host.NextSibling = NULL;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The cascade (Phase 9.5)                                             */
/* ------------------------------------------------------------------ */

/**
 * A style context with a host installed, ready to collect a cascade from.
 *
 * The host double is the same one the matcher suites use, which is deliberate:
 * a cascade test that needs to know *what* matched and a matching test that
 * needs to know *whether* something matched are asking the same seam two
 * questions, and giving each suite its own fake host would let them drift.
 */
/**
 * How many sheets one fixture may own.
 *
 * The module keeps a fixed table of live stylesheets, so a suite that made one
 * per test and never released it would exhaust the table partway through a run
 * and start failing for a reason that has nothing to do with what it tests.
 * The fixture therefore owns what it makes and releases it on the way out; the
 * bound is the most any single fixture in these suites has needed.
 */
#define CSS_FIXTURE_MAX_SHEETS 32u

typedef struct cssCascadeFixture {
    CssHost host;
    CssStyleContext *context;
    /*
     * The sheets this fixture created, so it can release them. A sheet made
     * through CssCascadeSheet belongs to the fixture's test, and a test that
     * destroys one itself is harmless: the release below tolerates an id that is
     * already dead.
     */
    CssStylesheetId sheets[CSS_FIXTURE_MAX_SHEETS];
    size_t sheetCount;
} CssCascadeFixture;

/** Create @p context with @p host installed, and seal the host's sibling links. */
static inline void CssCascadeBegin(CssCascadeFixture *fixture)
{
    CssHostInit(&fixture->host);
    CssHostSeal(&fixture->host);
    fixture->context = NULL;
    fixture->sheetCount = 0u;
    RoboCheckEqInt(CssStyleContextCreate(&fixture->context, NULL), RoboOk);
    RoboCheckEqInt(CssStyleContextSetElementHost(
                          fixture->context, &fixture->host.host, NULL),
                      RoboOk);
}

/**
 * Destroy the context and every sheet the fixture made.
 *
 * The host belongs to the caller and is left alone. The sheets do not: they live
 * in the module, and leaving them to accumulate would fill the module's fixed
 * table long before the run ends.
 */
static inline void CssCascadeEnd(CssCascadeFixture *fixture)
{
    size_t index;

    CssStyleContextDestroy(fixture->context);
    fixture->context = NULL;
    for (index = 0u; index < fixture->sheetCount; index++) {
        /* An id the test already destroyed answers NOT_FOUND, which is exactly
         * the same state the fixture wanted: gone. */
        (void)CssStylesheetDestroy(fixture->sheets[index], NULL);
    }
    fixture->sheetCount = 0u;
}

/**
 * Parse @p text as a stylesheet, parse every style rule's selectors, and add the
 * sheet to @p fixture under @p origin.
 *
 * The selector parse is what a real caller does and what the cascade requires:
 * a rule whose prelude was never turned into selectors reaches the cascade as a
 * rule with no selectors, which it reports as incomplete. Doing both here keeps
 * the suites from testing that report by accident in every test.
 */
static inline CssStylesheetId CssCascadeSheet(CssCascadeFixture *fixture,
                                                         CssOrigin origin,
                                                         const char *text)
{
    CssStylesheetId id = CssParseSheet(text, NULL);
    size_t index;

    RoboCheckEqInt(CssStylesheetSetOrigin(id, origin, NULL), RoboOk);
    for (index = 0u; index < CssStylesheetRuleCount(id); index++) {
        CssStyleRule *rule = (CssStyleRule *)CssRuleStyle(
            CssStylesheetRule(id, index));

        if (rule) {
            CssError ruleError = RoboCssCleared;

            RoboCheckEqInt(CssStyleRuleParseSelectors(
                                  rule, CssStylesheetSource(id), &ruleError, NULL),
                              RoboOk);
        }
    }
    RoboCheckEqInt(CssStyleContextAddSheet(fixture->context, origin, id, NULL),
                      RoboOk);
    if (fixture->sheetCount < CSS_FIXTURE_MAX_SHEETS) {
        fixture->sheets[fixture->sheetCount++] = id;
    }
    return id;
}

/*
 * The `style` attribute side of the seam, standing in for a DOM.
 *
 * RoboCSS calls it, parses what it returns and keeps none of it (9.1.10), so the
 * stub has two facts to offer — the text, or "there is no attribute" — and a
 * counter for the call itself. The text it hands over is a literal, which is
 * what the ownership rule requires: the declarations parsed from it borrow those
 * bytes, so they must outlive the cascade.
 */
typedef struct cssInlineStub {
    const char *text;      /* NULL: the element has no style attribute */
    Result failure; /* non-OK: the host could not answer at all */
    int calls;
    CssElementRef lastElement;
} CssInlineStub;

static inline Result CssInlineStubLoad(void *userdata,
                                                CssElementRef element,
                                                CssSlice *outText)
{
    CssInlineStub *stub = (CssInlineStub *)userdata;

    stub->calls++;
    stub->lastElement = element;
    if (stub->failure != RoboOk) {
        return stub->failure;
    }
    if (!stub->text) {
        return RoboNotFound;
    }
    *outText = CssSliceFromText(stub->text);
    return RoboOk;
}

/** Install the stub with @p text (NULL means "no `style` attribute"). */
static inline void CssCascadeInline(CssCascadeFixture *fixture,
                                     CssInlineStub *stub,
                                     const char *text)
{
    memset(stub, 0, sizeof(*stub));
    stub->text = text;
    stub->failure = RoboOk;
    RoboCheckEqInt(CssStyleContextSetInlineStyle(
                          fixture->context, CssInlineStubLoad, stub, NULL),
                      RoboOk);
    /* The callback and its userdata are installed and read together: a caller
     * that could reach one without the other could not call it at all. */
    RoboCheckEqInt(CssStyleContextInlineStyle(fixture->context) ==
                          CssInlineStubLoad,
                      1);
    RoboCheckEqInt(CssStyleContextInlineUserdata(fixture->context) ==
                          (void *)stub,
                      1);
}

/** Collect the cascade for @p element, asserting the call succeeded. */
static inline void RoboCssCascadeCollect(CssCascadeFixture *fixture,
                                       CssElementRef element,
                                       CssCascade *out,
                                       CssError *outError)
{
    CssError error = RoboCssCleared;

    RoboCheckEqInt(CssStyleContextMatchDeclarations(
                          fixture->context, element, out, &error, NULL),
                      RoboOk);
    if (outError) {
        *outError = error;
    }
}

/** The name of the @p index'th candidate's declaration, as a NUL-terminated
 *  string, shaped the way CssDeclarationNameOf shapes a list's. */
static inline void CssCascadeCandidateName(const CssCascade *cascade,
                                              size_t index,
                                              char *out,
                                              size_t capacity)
{
    const CssCascadeCandidate *candidate = CssCascadeAt(cascade, index);
    size_t offset = 0u;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (candidate) {
        CssSlice name = CssDeclarationName(candidate->declaration);
        size_t i;

        for (i = 0u; i < name.length; i++) {
            CssShapeByte(out, capacity, &offset, name.bytes[i]);
        }
    }
}

/**
 * Resolve @p name in @p cascade and copy the winning declaration's value *text*
 * into @p out.
 *
 * The value is read as its first token's raw text, which is all a cascade suite
 * needs: what is being asserted is *which* declaration won, and the value is the
 * shortest way to say which one. Nothing here interprets the value — that is
 * 9.8's job — so the helper deliberately reads the token's bytes rather than a
 * parsed form.
 */
static inline CssCascadeVerdict CssCascadeWinnerText(
    const CssCascade *cascade,
    const char *name,
    char *out,
    size_t capacity)
{
    const CssCascadeCandidate *winner = NULL;
    CssCascadeVerdict verdict;
    const CssValue *value;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    verdict = CssCascadeResolveName(cascade,
                                           CssSliceFromText(name), &winner);
    if (verdict != RoboCssCascadeVerdictDecided || !winner || !out || capacity == 0u) {
        return verdict;
    }
    value = CssDeclarationValue(winner->declaration);
    if (value && value->kind == RoboCssValueKindList && value->children.count > 0u) {
        value = &value->children.items[0];
    }
    if (value && value->kind == RoboCssValueKindToken) {
        size_t offset = 0u;
        size_t i;

        for (i = 0u; i < value->token.text.length; i++) {
            CssShapeByte(out, capacity, &offset, value->token.text.bytes[i]);
        }
    }
    return verdict;
}

/* ------------------------------------------------------------------ */
/* Inheritance (Phase 9.7)                                             */
/* ------------------------------------------------------------------ */

/**
 * A cascade fixture plus the two things a resolution needs.
 *
 * The property table and the registry are the caller's policy ("which
 * properties exist, and which of them inherit"), and a resolution cannot answer
 * `color: inherit` without both. They are built here through the supported path
 * — create the standard registry, apply it to a table — so a suite exercises the
 * same wiring an application would, and so the table's INHERITED flags and the
 * registry's classifications cannot disagree in the fixture.
 */
typedef struct cssInheritFixture {
    CssCascadeFixture cascade;
    CssPropertyTable *table;
    CssPropertyRegistry *registry;
} CssInheritFixture;

static inline void CssInheritBegin(CssInheritFixture *fixture)
{
    CssCascadeBegin(&fixture->cascade);
    fixture->table = NULL;
    fixture->registry = NULL;
    RoboCheckEqInt(CssPropertyTableCreate(&fixture->table, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&fixture->registry, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyRegistryApplyToTable(fixture->registry,
                                                               fixture->table, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextSetPropertyTable(
                          fixture->cascade.context, fixture->table, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextSetPropertyRegistry(
                          fixture->cascade.context, fixture->registry, NULL),
                      RoboOk);
}

/**
 * The three nested things a resolution test needs, under names that read the
 * way the tests write them.
 *
 * The fixture embeds the cascade fixture rather than duplicating it — one host
 * double, one context, one place a callback is installed — so these are the
 * doors to the parts, not second copies of them.
 */
static inline CssHost *CssInheritHost(CssInheritFixture *fixture)
{
    return &fixture->cascade.host;
}

static inline CssStyleContext *CssInheritContext(CssInheritFixture *fixture)
{
    return fixture->cascade.context;
}

/** Parse @p text and add it to the fixture's context under @p origin. */
static inline CssStylesheetId CssInheritSheet(CssInheritFixture *fixture,
                                                         CssOrigin origin,
                                                         const char *text)
{
    return CssCascadeSheet(&fixture->cascade, origin, text);
}

/** Install @p text as the element's `style` attribute (NULL means none). */
static inline void CssInheritInline(CssInheritFixture *fixture,
                                      CssInlineStub *stub,
                                      const char *text)
{
    CssCascadeInline(&fixture->cascade, stub, text);
}

static inline void CssInheritEnd(CssInheritFixture *fixture)
{
    CssPropertyRegistryDestroy(fixture->registry);
    CssPropertyTableDestroy(fixture->table);
    fixture->registry = NULL;
    fixture->table = NULL;
    CssCascadeEnd(&fixture->cascade);
}

/** Resolve @p cascade with @p parent (may be NULL) into @p out. */
static inline void RoboCssInheritResolve(const CssInheritFixture *fixture,
                                       const CssCascade *cascade,
                                       const CssInheritedStyle *parent,
                                       CssInheritedStyle *out)
{
    RoboCheckEqInt(RoboCssInheritResolve(fixture->cascade.context, cascade, parent,
                                              out, NULL),
                      RoboOk);
}

/**
 * The resolved value of @p name, as a shape string.
 *
 * Three states are distinguishable, and they are the three a test needs to tell
 * apart: `""` shapes the value, `<absent>` means the property has no entry at
 * all, and `<unresolved>` means the entry exists but this build does not know the
 * property's default. Collapsing any two of those would let a test pass for the
 * wrong reason.
 */
static inline void CssInheritValue(const CssInheritedStyle *style,
                                     const char *name,
                                     char *out,
                                     size_t capacity)
{
    const CssInheritedProperty *entry;
    size_t offset = 0u;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    entry = CssInheritedStyleFind(style, CssSliceFromText(name));
    if (!entry) {
        CssShapeText(out, capacity, &offset, "<absent>");
        return;
    }
    if (entry->unresolved) {
        CssShapeText(out, capacity, &offset, "<unresolved>");
        return;
    }
    CssShapeWrite(&entry->value, out, capacity, &offset);
}

/** The provenance of @p name as a name string, or "<absent>". */
static inline const char *CssInheritSource(const CssInheritedStyle *style,
                                             const char *name)
{
    const CssInheritedProperty *entry =
        CssInheritedStyleFind(style, CssSliceFromText(name));

    if (!entry) {
        return "<absent>";
    }
    return CssInheritedSourceName(entry->source);
}

/** The CSS-wide keyword recorded for @p name, or "<absent>". */
static inline const char *CssInheritKeyword(const CssInheritedStyle *style,
                                              const char *name)
{
    const CssInheritedProperty *entry =
        CssInheritedStyleFind(style, CssSliceFromText(name));

    if (!entry) {
        return "<absent>";
    }
    return CssWideKeywordName(entry->keyword);
}

/** Assert @p style's entry for @p name has @p value as its shape and @p source. */
static inline void CssInheritIs(const CssInheritedStyle *style,
                                  const char *name,
                                  const char *value,
                                  const char *source)
{
    char shape[256];

    CssInheritValue(style, name, shape, sizeof(shape));
    RoboCheckStrEq(shape, value);
    RoboCheckStrEq(CssInheritSource(style, name), source);
}

/* ------------------------------------------------------------------ */
/* Computed values (Phase 9.8)                                         */
/* ------------------------------------------------------------------ */

/**
 * A computed value's canonical text, or one of two distinguishable absences.
 *
 * The three states are the three a test has to tell apart and they must not be
 * collapsed: `""` shapes a value, `<absent>` means there is no entry, and
 * `<none>` means the entry exists but carries no computed value. A style with a
 * property silently missing and a style with the property computed as nothing
 * are different answers.
 */
static inline void CssComputedNote(const CssComputedStyle *style,
                                     const char *name,
                                     char *out,
                                     size_t capacity)
{
    const CssComputedValue *value;
    size_t length = 0u;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (CssComputedStyleFind(style, CssSliceFromText(name)) ==
        (size_t)-1) {
        CssShapeText(out, capacity, &length, "<absent>");
        return;
    }
    value = CssComputedStyleComputedByName(style, CssSliceFromText(name));
    if (!value) {
        CssShapeText(out, capacity, &length, "<none>");
        return;
    }
    out[length] = '\0';
    if (CssComputedValueWrite(value, out, capacity, &length, NULL) != RoboOk) {
        out[0] = '\0';
    }
}

/** Assert @p style's computed value for @p name renders as @p expected. */
static inline void CssComputedIs(const CssComputedStyle *style,
                                   const char *name,
                                   const char *expected)
{
    char text[RoboCssComputedValueTextMax];

    CssComputedNote(style, name, text, sizeof(text));
    RoboCheckStrEq(text, expected);
}

/**
 * The provenance flags of @p name as a `+`-joined name list, or `<absent>`.
 *
 * Names rather than bits, because an assertion that a value is
 * `inherited+default` is a statement about the cascade and an assertion that a
 * mask equals 6 is a statement about the mask.
 */
static inline void CssComputedFlags(const CssComputedStyle *style,
                                      const char *name,
                                      char *out,
                                      size_t capacity)
{
    static const struct {
        uint32_t flag;
        const char *name;
    } names[] = {
        { RoboCssComputedFlagDeclared, "declared" },
        { RoboCssComputedFlagInherited, "inherited" },
        { RoboCssComputedFlagDefault, "default" },
        { RoboCssComputedFlagDeferred, "deferred" },
        { RoboCssComputedFlagCustom, "custom" },
        { RoboCssComputedFlagUnresolved, "unresolved" },
        { RoboCssComputedFlagInvalid, "invalid" }
    };
    uint32_t flags;
    size_t offset = 0u;
    size_t index;

    if (!out || capacity == 0u) {
        return;
    }
    out[0] = '\0';
    if (CssComputedStyleFind(style, CssSliceFromText(name)) ==
        (size_t)-1) {
        CssShapeText(out, capacity, &offset, "<absent>");
        return;
    }
    flags = CssComputedStyleFlags(style, CssSliceFromText(name));
    if (flags == RoboCssComputedFlagNone) {
        CssShapeText(out, capacity, &offset, "none");
        return;
    }
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        if ((flags & names[index].flag) == 0u) {
            continue;
        }
        if (offset > 0u) {
            CssShapeByte(out, capacity, &offset, (uint8_t)'+');
        }
        CssShapeText(out, capacity, &offset, names[index].name);
    }
}

/** Assert @p name's provenance flags render as @p expected. */
static inline void CssComputedFlagsAre(const CssComputedStyle *style,
                                          const char *name,
                                          const char *expected)
{
    char text[128];

    CssComputedFlags(style, name, text, sizeof(text));
    RoboCheckStrEq(text, expected);
}

/**
 * The computed text of one value built from @p text and @p computation.
 *
 * A one-line fixture for the normaliser's own suite: parse @p text as a value
 * through the *public* declaration path (so the value under test is one the real
 * grammar produced, not a hand-built tree), compute it, and shape the result.
 * The parse result is checked, because a fixture that silently failed to parse
 * would make every expectation below it an assertion about nothing.
 */
static inline void CssComputeText(CssPropertyComputation computation,
                                    const char *text,
                                    char *out,
                                    size_t capacity,
                                    uint32_t *outFlags)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;
    CssComputedValue value = RoboCssCleared;
    size_t length = 0u;

    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    if (outFlags) {
        *outFlags = 0u;
    }
    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          computation, CssDeclarationValue(declaration), &value,
                          NULL),
                      RoboOk);
    if (outFlags) {
        *outFlags = value.unresolved ? 1u : 0u;
    }
    if (out && capacity > 0u) {
        out[0] = '\0';
        if (value.kind == RoboCssComputedValueNone) {
            CssShapeText(out, capacity, &length, "<none>");
        } else if (CssComputedValueWrite(&value, out, capacity, &length, NULL) !=
                   RoboOk) {
            out[0] = '\0';
        }
    }
    CssComputedValueDispose(&value);
    CssDeclarationListDispose(&list);
}

/** Assert the normalised text of @p text under @p computation is @p expected. */
static inline void CssComputeEquals(CssPropertyComputation computation,
                                      const char *text,
                                      const char *expected)
{
    char shape[RoboCssComputedValueTextMax];

    CssComputeText(computation, text, shape, sizeof(shape), NULL);
    RoboCheckStrEq(shape, expected);
}

/** The kind name of @p text computed under @p computation, as a static string. */
static inline const char *CssComputeKind(CssPropertyComputation computation,
                                           const char *text)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    const CssDeclaration *declaration;
    CssComputedValue value = RoboCssCleared;
    const char *name;

    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          computation, CssDeclarationValue(declaration), &value,
                          NULL),
                      RoboOk);
    name = CssComputedValueKindName(value.kind);
    CssComputedValueDispose(&value);
    CssDeclarationListDispose(&list);
    return name;
}

#endif /* ROBO_CSS_TEST_SUPPORT_H */
