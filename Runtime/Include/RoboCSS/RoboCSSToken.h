/*
 * RoboCSS token model (Phase 9.1).
 *
 * This header defines the *record* a token is, not the machine that produces
 * tokens. Phase 9.2 owns the tokenizer: the CSS Syntax state machine, the
 * preprocessing step, escapes, and the numeric conversions. What 9.1 fixes is
 * the shape every one of those writes into, so the tokenizer can be written
 * against a frozen vocabulary instead of inventing one.
 *
 * The vocabulary is the CSS Syntax token taxonomy — ident, function,
 * at-keyword, hash, string and bad-string, url and bad-url, delim, number,
 * percentage, dimension, whitespace, comment, CDO, CDC, the four punctuation
 * tokens, the six bracket tokens, EOF and an explicit invalid token.
 *
 * Two rules keep this honest:
 *
 *   - A token borrows, always. `text`, `repr` and `unit` point into the source
 *     the token was taken from; no token copies bytes and no token owns memory,
 *     so a token is a plain value that can be copied freely and disposed as a
 *     no-op (CssTokenDispose exists so the lifetime story is uniform
 *     with the owning models, not because anything is freed). Nothing about a
 *     token ever dangles, and nothing about a token is ever invalidated by
 *     asking for the next one at the same time.
 *   - A constructor does not tokenize. CssTokenMake binds a type to a
 *     range of an existing source and nothing else: it does not classify the
 *     bytes, does not parse a number, and does not validate that the bytes
 *     could ever be that type. Classification is 9.2's job; claiming it here
 *     would be the one thing this phase must not do.
 *
 * Raw bytes versus value (9.2). `text` is always the raw bytes the token covers,
 * so `text` and `range` can never disagree. That is deliberately *not* the same
 * thing as the token's value, because CSS resolves escapes and strips the
 * delimiters that are part of the token: `col\6fr` is the identifier `color`,
 * and a string token's value excludes its quotes. Both are reachable, without
 * either one lying: `text` for the bytes, and the robo_css_token_value_* family
 * at the bottom of this header for the value. Those accessors work from `text`
 * alone — no source pointer is needed — so a token stays a self-contained value.
 */

#ifndef ROBO_CSS_TOKEN_H
#define ROBO_CSS_TOKEN_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSSource.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The CSS token vocabulary. */
typedef enum CssTokenType {
    /** No token: a cleared record. Never produced by a tokenizer. */
    RoboCssTokenNone = 0,
    /** `ident` — a name, escapes already resolved by 9.2. */
    RoboCssTokenIdent = 1,
    /** `ident(` — a function name; the opening paren is part of the token. */
    RoboCssTokenFunction = 2,
    /** `@ident` — including the leading `@` in `text`. */
    RoboCssTokenAtKeyword = 3,
    /** `#name` — an id selector or a colour; see `hash`. */
    RoboCssTokenHash = 4,
    /** A string literal, closed. */
    RoboCssTokenString = 5,
    /** A string literal that ended in a newline: never valid. */
    RoboCssTokenBadString = 6,
    /** `url(...)` — an unquoted URL, closed. */
    RoboCssTokenUrl = 7,
    /** A `url(` that failed: never valid. */
    RoboCssTokenBadUrl = 8,
    /** A single delimiter character. */
    RoboCssTokenDelim = 9,
    /** A number. */
    RoboCssTokenNumber = 10,
    /** A number immediately followed by `%`. */
    RoboCssTokenPercentage = 11,
    /** A number immediately followed by an ident: `10px`, `2em`, `1fr`. */
    RoboCssTokenDimension = 12,
    /** A run of whitespace, preserved rather than dropped. */
    RoboCssTokenWhitespace = 13,
    /** A CSS comment, brackets and all — preserved as a token, as the CSS
     *  Syntax specification requires rather than discarded by the tokenizer. */
    RoboCssTokenComment = 14,
    /** `<!--` */
    RoboCssTokenCdo = 15,
    /** `-->` */
    RoboCssTokenCdc = 16,
    /** `:` */
    RoboCssTokenColon = 17,
    /** `;` */
    RoboCssTokenSemicolon = 18,
    /** `,` */
    RoboCssTokenComma = 19,
    /** `(` */
    RoboCssTokenLeftParen = 20,
    /** `)` */
    RoboCssTokenRightParen = 21,
    /** `[` */
    RoboCssTokenLeftSquare = 22,
    /** `]` */
    RoboCssTokenRightSquare = 23,
    /** `{` */
    RoboCssTokenLeftCurly = 24,
    /** `}` */
    RoboCssTokenRightCurly = 25,
    /** End of input: a real token, so every loop terminates on a value. */
    RoboCssTokenEof = 26,
    /** Bytes that cannot start a token at all. */
    RoboCssTokenInvalid = 27
} CssTokenType;

/** Number of defined token types. */
#define RoboCssTokenTypeCount 28u

/** Non-zero if @p type is a defined token type enumerator. */
int CssTokenTypeIsKnown(CssTokenType type);

/** Human-readable token type name; never NULL; "unknown" when undefined. */
const char *CssTokenTypeName(CssTokenType type);

/** Non-zero for NUMBER, PERCENTAGE and DIMENSION. */
int CssTokenTypeIsNumeric(CssTokenType type);

/** Non-zero for WHITESPACE and COMMENT — trivia the grammar steps over. */
int CssTokenTypeIsIgnorable(CssTokenType type);

/** Non-zero for `(`, `[` and `{`. */
int CssTokenTypeIsBlockStart(CssTokenType type);

/** Non-zero for `)`, `]` and `}`. */
int CssTokenTypeIsBlockEnd(CssTokenType type);

/**
 * The bracket that closes @p openType.
 * @return 1 and writes @p outClose for `(`, `[`, `{`; 0 and writes
 *         RoboCssTokenNone otherwise. A NULL output is tolerated (the
 *         answer is then only the return value).
 */
int CssTokenTypeMatchingClose(CssTokenType openType,
                                      CssTokenType *outClose);

/**
 * The bracket that opens @p closeType. The inverse of the above, for a
 * parser recovering from an unexpected `)`.
 * @return 1 and writes @p outOpen for `)`, `]`, `}`; 0 otherwise.
 */
int CssTokenTypeMatchingOpen(CssTokenType closeType,
                                     CssTokenType *outOpen);

/** A hash token's type: `#id` is an identifier-like hash, `#123` is not. */
typedef enum CssHashType {
    /** Not a hash token, or not yet classified. */
    RoboCssHashTypeNone = 0,
    /** The hash is a valid identifier: usable as an id selector. */
    RoboCssHashTypeId = 1,
    /** The hash is not identifier-like: a colour, never an id selector. */
    RoboCssHashTypeUnrestricted = 2
} CssHashType;

/** Number of defined hash types. */
#define RoboCssHashTypeCount 3u

/** Non-zero if @p type is a defined hash type enumerator. */
int CssHashTypeIsKnown(CssHashType type);

/** Human-readable hash type name; never NULL. */
const char *CssHashTypeName(CssHashType type);

/**
 * One token.
 *
 * Every byte-bearing field borrows from the source the token was taken from
 * and must not outlive it. The numeric fields are filled by 9.2: a token built
 * by CssTokenMake has `number == 0`, `isInteger == 0`, `hasSign == 0`
 * and absent `repr`/`unit`, because reading a number out of bytes is
 * tokenizing, and 9.1 does not tokenize.
 */
typedef struct CssToken {
    CssTokenType type;
    /** Source range this token covers (`[begin, end)`). */
    CssRange range;
    /**
     * Raw bytes of the token, borrowed from the source: `range`'s bytes exactly,
     * delimiters and escapes included, never resolved. Use the value accessors
     * below for what the token *means*.
     */
    CssSlice text;
    /** NUMBER/PERCENTAGE/DIMENSION value (9.2); 0 otherwise. */
    double number;
    /** Non-zero when the numeric value was written as an integer. */
    int isInteger;
    /** Non-zero when the numeric representation carried an explicit sign. */
    int hasSign;
    /** The number as written (e.g. "+1.5e3"), borrowed; absent otherwise. */
    CssSlice repr;
    /** DIMENSION unit as written (e.g. "px"), borrowed; absent otherwise. */
    CssSlice unit;
    /** HASH classification (9.2); NONE otherwise. */
    CssHashType hash;
    /** STRING/URL: 1 when the terminator was present, 0 when it was not. */
    int closed;
} CssToken;

/** Clear @p token to the NONE token. Safe on NULL (no-op). */
void CssTokenInit(CssToken *token);

/**
 * Dispose @p token. Tokens own nothing, so this is defined as a reset to NONE;
 * it exists so every model in the subsystem has the same lifetime shape and
 * so a caller can be explicit about a token it is finished with. Idempotent
 * and safe on NULL.
 */
void CssTokenDispose(CssToken *token);

/**
 * Bind @p type to @p range in @p source.
 *
 * @p range must be a valid range of @p source (see
 * CssSourceRangeIsValid) and @p type must be a known type. Nothing is
 * classified: `text` becomes the bytes the range covers, and every numeric
 * field stays unset.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL source/token/output, an
 *         unknown type, or a range that is not valid in @p source.
 */
Result CssTokenMake(CssToken *token,
                                  CssTokenType type,
                                  const CssSource *source,
                                  CssRange range,
                                  CssDiag *diag);

/**
 * Build the EOF token at @p offset, with an empty range there. This is the one
 * token that legitimately carries no bytes.
 * @return RoboOk; RoboInvalidArgument for a NULL output or an offset past
 *         the end of @p source.
 */
Result CssTokenMakeEof(CssToken *token,
                                      const CssSource *source,
                                      size_t offset,
                                      CssDiag *diag);

/** Copy @p token's plain fields into @p outToken (no allocation). NULL → INVALID. */
Result CssTokenCopy(const CssToken *token,
                                  CssToken *outToken,
                                  CssDiag *diag);

/** Non-zero when @p token is the NONE token. NULL → 1. */
int CssTokenIsNone(const CssToken *token);

/** Non-zero when @p token is NONE or EOF — "there is no more input". */
int CssTokenIsTerminator(const CssToken *token);

/** Borrowed raw bytes of @p token (absent when it has none). NULL → absent. */
CssSlice CssTokenText(const CssToken *token);

/** Non-zero when @p token's raw bytes equal the ASCII @p ascii exactly. */
int CssTokenEqualsAscii(const CssToken *token, const char *ascii);

/**
 * Non-zero when @p token's raw bytes equal the ASCII @p ascii ignoring ASCII
 * case. This is the comparison CSS itself uses for property names, tag names,
 * keywords and hex colours, so it is the one a caller almost always wants —
 * but it compares *raw* bytes, so it answers "no" for an escaped spelling of
 * the same name. Use CssTokenValueEqualsAsciiCi for that.
 */
int CssTokenEqualsAsciiCi(const CssToken *token,
                                   const char *ascii);

/* ------------------------------------------------------------------ */
/* The token's value (Phase 9.2)                                       */
/* ------------------------------------------------------------------ */

/**
 * The token's value is the text CSS says the token stands for, which is not
 * always the text it is written with:
 *
 * | Token | Value |
 * |---|---|
 * | IDENT | the name, escapes resolved (`col\6fr` → `color`) |
 * | FUNCTION | the name without the `(` |
 * | AT_KEYWORD | the name without the leading `@` |
 * | HASH | the name without the leading `#` |
 * | DIMENSION | the numeric part as written; the unit is separate, in `unit` |
 * | STRING / BAD_STRING | the content, quotes excluded, escapes resolved |
 * | URL | the content, `url(` and the closing `)` excluded, surrounding
 *   whitespace trimmed, escapes resolved |
 * | BAD_URL | everything after `url(` — a bad url has no value to speak of,
 *   so what comes back is what is actually there |
 * | COMMENT | the comment as written, its opening and closing markers included |
 * | anything else | the same bytes as `text` |
 *
 * Four accessors rather than one, because the four things a consumer needs are
 * genuinely different: how long the value is, whether it *is* some ASCII word, a
 * case-insensitive spelling of it, and the bytes themselves. The first three
 * decode escapes as they go and allocate nothing, so a keyword check on a token
 * never needs a buffer and never needs the source. Only
 * CssTokenValueCopy writes bytes, and it writes them into storage the
 * caller owns.
 */

/**
 * Number of bytes in @p token's value once escapes are resolved.
 * NULL token → 0.
 */
size_t CssTokenValueLength(const CssToken *token);

/**
 * Non-zero when @p token's value equals the ASCII @p ascii byte for byte.
 * A value containing a non-ASCII character never matches a pure-ASCII needle.
 */
int CssTokenValueEqualsAscii(const CssToken *token,
                                      const char *ascii);

/**
 * Non-zero when @p token's value equals @p ascii ignoring ASCII case. This is
 * the comparison CSS itself uses for property names, tag names, keywords and
 * hex colours, and unlike CssTokenEqualsAsciiCi it sees through
 * escapes: `R\47 B` matches "rgb".
 */
int CssTokenValueEqualsAsciiCi(const CssToken *token,
                                         const char *ascii);

/**
 * Write @p token's resolved value into @p out as @p outCapacity bytes, and the
 * number written to @p outLength. The bytes are not NUL-terminated; the length
 * is the answer.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p token, a NULL @p out or
 *         @p outLength, or a capacity smaller than
 *         CssTokenValueLength (@p outLength still receives the length
 *         that was needed, so a caller can size a second attempt).
 */
Result CssTokenValueCopy(const CssToken *token,
                                        uint8_t *out,
                                        size_t outCapacity,
                                        size_t *outLength,
                                        CssDiag *diag);

/**
 * Non-zero when @p token's bytes contain an escape, i.e. when its value differs
 * from its text. The tokenizer sets this as it scans, so it costs nothing to
 * ask; a caller that needs a value as a slice rather than as comparisons can use
 * it to skip the copy in the overwhelmingly common no-escape case.
 */
int CssTokenHasEscapes(const CssToken *token);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TOKEN_H */
