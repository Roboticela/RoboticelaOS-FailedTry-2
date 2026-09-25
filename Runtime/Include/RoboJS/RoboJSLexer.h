/*
 * RoboJS lexer (Phase 16.2) — source text into tokens.
 *
 * The lexer is a struct with an offset and a line/column cursor, and `JsLexerNext` advances it by
 * one token. A pull lexer rather than a token array, because the parser is recursive descent and
 * needs one token of lookahead: `JsLexerPeek` fills a one-slot buffer, which is the whole of the
 * parser's lookahead machinery.
 *
 * ## Text lifetime
 *
 * An identifier's `text` points **into the source**, with `length` beside it: an identifier has no
 * escapes, so copying it would be work for nothing. A string literal *is* decoded (escapes,
 * `\n`, `\uXXXX`) and a number is not text at all — both are written into a scratch buffer the
 * lexer owns, reallocated as needed. So the rule a caller follows is: **a token's text is valid
 * until the next `JsLexerNext` call**, and the parser copies what it needs to keep. That is
 * documented here rather than discovered later.
 *
 * ## A lexer error is a structured error, not a diagnostic
 *
 * `JsLexerInit` can fail on a bad argument and `JsLexerNext` answers `RoboParseError` with
 * `JsLexerError` filled — kind `RoboJsErrorSyntax`, the message and the span. The lexer never
 * prints and never throws a JavaScript value: nothing has been executed yet.
 *
 * ## What is not in this build
 *
 * No regular-expression literal (the `/` is always division or the start of a comment), no template
 * literal, no tagged template, no numeric separators, no BigInt suffix. An unsupported character is
 * a `SyntaxError` naming the character and its position, which is a better answer than a guess.
 */

#ifndef ROBO_JS_LEXER_H
#define ROBO_JS_LEXER_H

#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One token. See the header comment for the lifetime of `text`. */
typedef struct JsToken {
    uint32_t kind; /* JsTokenKind */
    uint32_t flags;
    JsSpan span;
    const char *text;
    int32_t length;
    double number;
} JsToken;

/**
 * The lexer. Its layout is public because `JsToken` is handed to a caller by pointer and a test may
 * put one on the stack; nothing outside `RoboJSLexer.c` writes a field except through the doors
 * below.
 */
typedef struct JsLexer {
    JsHeap *heap;
    const char *source;
    int32_t length;
    int32_t offset;
    int32_t line;
    int32_t column;
    /** One-slot lookahead: `peeked` is 1 when `peek` holds a token already lexed. */
    int peeked;
    JsToken peek;
    /** Decoded string/number scratch; owned by the heap. */
    char *scratch;
    int32_t scratchCapacity;
    /** Set once the lexer has reported an error; every later call answers `RoboParseError`. */
    int failed;
    JsError error;
} JsLexer;

/**
 * Prepare @p lexer to read @p length bytes of @p source (length < 0 → strlen).
 * @return RoboOk; RoboInvalidArgument when @p lexer or @p heap is NULL or @p source is NULL with a
 *         positive length; RoboOutOfMemory when the first scratch allocation fails.
 */
Result JsLexerInit(JsLexer *lexer, JsHeap *heap, const char *source, int32_t length);

/** Advance to the next token and write it to @p out. Answers `RoboParseError` on a bad character,
 *  an unterminated string or an unterminated comment. */
Result JsLexerNext(JsLexer *lexer, JsToken *out);

/** The next token without consuming it (fills the one-slot buffer on first call). */
Result JsLexerPeek(JsLexer *lexer, JsToken *out);

/** The lexer's structured error; never NULL. `kind` is `RoboJsErrorNone` before a failure. */
const JsError *JsLexerError(const JsLexer *lexer);

/**
 * Copy @p token's text into @p buffer (NUL-terminated) — the door the parser uses for a name it
 * needs to keep. Answers the bytes a complete copy needs; truncates rather than overflowing.
 */
int32_t JsTokenCopyText(const JsToken *token, char *buffer, size_t capacity);

/** 1 when @p kind is a keyword token. */
int JsTokenKindIsKeyword(JsTokenKind kind);
/** 1 when @p kind can begin an expression (used by the parser's arrow lookahead and error text). */
int JsTokenKindStartsExpression(JsTokenKind kind);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_JS_LEXER_H */
