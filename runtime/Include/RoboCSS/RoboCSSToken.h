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
 *   - A token borrows. `text`, `repr` and `unit` all point into the source the
 *     token was taken from; no token copies bytes and no token owns memory, so
 *     a token is a plain value that can be copied freely and disposed as a
 *     no-op (robo_css_token_dispose exists so the lifetime story is uniform
 *     with the owning models, not because anything is freed).
 *   - A constructor does not tokenize. robo_css_token_make binds a type to a
 *     range of an existing source and nothing else: it does not classify the
 *     bytes, does not parse a number, and does not validate that the bytes
 *     could ever be that type. Classification is 9.2's job; claiming it here
 *     would be the one thing this phase must not do.
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
typedef enum robo_css_token_type {
    /** No token: a cleared record. Never produced by a tokenizer. */
    ROBO_CSS_TOKEN_NONE = 0,
    /** `ident` — a name, escapes already resolved by 9.2. */
    ROBO_CSS_TOKEN_IDENT = 1,
    /** `ident(` — a function name; the opening paren is part of the token. */
    ROBO_CSS_TOKEN_FUNCTION = 2,
    /** `@ident` — including the leading `@` in `text`. */
    ROBO_CSS_TOKEN_AT_KEYWORD = 3,
    /** `#name` — an id selector or a colour; see `hash`. */
    ROBO_CSS_TOKEN_HASH = 4,
    /** A string literal, closed. */
    ROBO_CSS_TOKEN_STRING = 5,
    /** A string literal that ended in a newline: never valid. */
    ROBO_CSS_TOKEN_BAD_STRING = 6,
    /** `url(...)` — an unquoted URL, closed. */
    ROBO_CSS_TOKEN_URL = 7,
    /** A `url(` that failed: never valid. */
    ROBO_CSS_TOKEN_BAD_URL = 8,
    /** A single delimiter character. */
    ROBO_CSS_TOKEN_DELIM = 9,
    /** A number. */
    ROBO_CSS_TOKEN_NUMBER = 10,
    /** A number immediately followed by `%`. */
    ROBO_CSS_TOKEN_PERCENTAGE = 11,
    /** A number immediately followed by an ident: `10px`, `2em`, `1fr`. */
    ROBO_CSS_TOKEN_DIMENSION = 12,
    /** A run of whitespace, preserved rather than dropped. */
    ROBO_CSS_TOKEN_WHITESPACE = 13,
    /** A CSS comment, brackets and all — preserved as a token, as the CSS
     *  Syntax specification requires rather than discarded by the tokenizer. */
    ROBO_CSS_TOKEN_COMMENT = 14,
    /** `<!--` */
    ROBO_CSS_TOKEN_CDO = 15,
    /** `-->` */
    ROBO_CSS_TOKEN_CDC = 16,
    /** `:` */
    ROBO_CSS_TOKEN_COLON = 17,
    /** `;` */
    ROBO_CSS_TOKEN_SEMICOLON = 18,
    /** `,` */
    ROBO_CSS_TOKEN_COMMA = 19,
    /** `(` */
    ROBO_CSS_TOKEN_LEFT_PAREN = 20,
    /** `)` */
    ROBO_CSS_TOKEN_RIGHT_PAREN = 21,
    /** `[` */
    ROBO_CSS_TOKEN_LEFT_SQUARE = 22,
    /** `]` */
    ROBO_CSS_TOKEN_RIGHT_SQUARE = 23,
    /** `{` */
    ROBO_CSS_TOKEN_LEFT_CURLY = 24,
    /** `}` */
    ROBO_CSS_TOKEN_RIGHT_CURLY = 25,
    /** End of input: a real token, so every loop terminates on a value. */
    ROBO_CSS_TOKEN_EOF = 26,
    /** Bytes that cannot start a token at all. */
    ROBO_CSS_TOKEN_INVALID = 27
} robo_css_token_type_t;

/** Number of defined token types. */
#define ROBO_CSS_TOKEN_TYPE_COUNT 28u

/** Non-zero if @p type is a defined token type enumerator. */
int robo_css_token_type_is_known(robo_css_token_type_t type);

/** Human-readable token type name; never NULL; "unknown" when undefined. */
const char *robo_css_token_type_name(robo_css_token_type_t type);

/** Non-zero for NUMBER, PERCENTAGE and DIMENSION. */
int robo_css_token_type_is_numeric(robo_css_token_type_t type);

/** Non-zero for WHITESPACE and COMMENT — trivia the grammar steps over. */
int robo_css_token_type_is_ignorable(robo_css_token_type_t type);

/** Non-zero for `(`, `[` and `{`. */
int robo_css_token_type_is_block_start(robo_css_token_type_t type);

/** Non-zero for `)`, `]` and `}`. */
int robo_css_token_type_is_block_end(robo_css_token_type_t type);

/**
 * The bracket that closes @p open_type.
 * @return 1 and writes @p out_close for `(`, `[`, `{`; 0 and writes
 *         ROBO_CSS_TOKEN_NONE otherwise. A NULL output is tolerated (the
 *         answer is then only the return value).
 */
int robo_css_token_type_matching_close(robo_css_token_type_t open_type,
                                      robo_css_token_type_t *out_close);

/**
 * The bracket that opens @p close_type. The inverse of the above, for a
 * parser recovering from an unexpected `)`.
 * @return 1 and writes @p out_open for `)`, `]`, `}`; 0 otherwise.
 */
int robo_css_token_type_matching_open(robo_css_token_type_t close_type,
                                     robo_css_token_type_t *out_open);

/** A hash token's type: `#id` is an identifier-like hash, `#123` is not. */
typedef enum robo_css_hash_type {
    /** Not a hash token, or not yet classified. */
    ROBO_CSS_HASH_TYPE_NONE = 0,
    /** The hash is a valid identifier: usable as an id selector. */
    ROBO_CSS_HASH_TYPE_ID = 1,
    /** The hash is not identifier-like: a colour, never an id selector. */
    ROBO_CSS_HASH_TYPE_UNRESTRICTED = 2
} robo_css_hash_type_t;

/** Number of defined hash types. */
#define ROBO_CSS_HASH_TYPE_COUNT 3u

/** Non-zero if @p type is a defined hash type enumerator. */
int robo_css_hash_type_is_known(robo_css_hash_type_t type);

/** Human-readable hash type name; never NULL. */
const char *robo_css_hash_type_name(robo_css_hash_type_t type);

/**
 * One token.
 *
 * Every byte-bearing field borrows from the source the token was taken from
 * and must not outlive it. The numeric fields are filled by 9.2: a token built
 * by robo_css_token_make has `number == 0`, `is_integer == 0`, `has_sign == 0`
 * and absent `repr`/`unit`, because reading a number out of bytes is
 * tokenizing, and 9.1 does not tokenize.
 */
typedef struct robo_css_token {
    robo_css_token_type_t type;
    /** Source range this token covers (`[begin, end)`). */
    robo_css_range_t range;
    /** Raw bytes of the token, borrowed from the source. */
    robo_css_slice_t text;
    /** NUMBER/PERCENTAGE/DIMENSION value (9.2); 0 otherwise. */
    double number;
    /** Non-zero when the numeric value was written as an integer. */
    int is_integer;
    /** Non-zero when the numeric representation carried an explicit sign. */
    int has_sign;
    /** The number as written (e.g. "+1.5e3"), borrowed; absent otherwise. */
    robo_css_slice_t repr;
    /** DIMENSION unit as written (e.g. "px"), borrowed; absent otherwise. */
    robo_css_slice_t unit;
    /** HASH classification (9.2); NONE otherwise. */
    robo_css_hash_type_t hash;
    /** STRING/URL: 1 when the terminator was present, 0 when it was not. */
    int closed;
} robo_css_token_t;

/** Clear @p token to the NONE token. Safe on NULL (no-op). */
void robo_css_token_init(robo_css_token_t *token);

/**
 * Dispose @p token. Tokens own nothing, so this is defined as a reset to NONE;
 * it exists so every model in the subsystem has the same lifetime shape and
 * so a caller can be explicit about a token it is finished with. Idempotent
 * and safe on NULL.
 */
void robo_css_token_dispose(robo_css_token_t *token);

/**
 * Bind @p type to @p range in @p source.
 *
 * @p range must be a valid range of @p source (see
 * robo_css_source_range_is_valid) and @p type must be a known type. Nothing is
 * classified: `text` becomes the bytes the range covers, and every numeric
 * field stays unset.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL source/token/output, an
 *         unknown type, or a range that is not valid in @p source.
 */
robo_result_t robo_css_token_make(robo_css_token_t *token,
                                  robo_css_token_type_t type,
                                  const robo_css_source_t *source,
                                  robo_css_range_t range,
                                  robo_css_diag_t *diag);

/**
 * Build the EOF token at @p offset, with an empty range there. This is the one
 * token that legitimately carries no bytes.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL output or an offset past
 *         the end of @p source.
 */
robo_result_t robo_css_token_make_eof(robo_css_token_t *token,
                                      const robo_css_source_t *source,
                                      size_t offset,
                                      robo_css_diag_t *diag);

/** Copy @p token's plain fields into @p out_token (no allocation). NULL → INVALID. */
robo_result_t robo_css_token_copy(const robo_css_token_t *token,
                                  robo_css_token_t *out_token,
                                  robo_css_diag_t *diag);

/** Non-zero when @p token is the NONE token. NULL → 1. */
int robo_css_token_is_none(const robo_css_token_t *token);

/** Non-zero when @p token is NONE or EOF — "there is no more input". */
int robo_css_token_is_terminator(const robo_css_token_t *token);

/** Borrowed raw bytes of @p token (absent when it has none). NULL → absent. */
robo_css_slice_t robo_css_token_text(const robo_css_token_t *token);

/** Non-zero when @p token's raw bytes equal the ASCII @p ascii exactly. */
int robo_css_token_equals_ascii(const robo_css_token_t *token, const char *ascii);

/**
 * Non-zero when @p token's raw bytes equal the ASCII @p ascii ignoring ASCII
 * case. This is the comparison CSS itself uses for property names, tag names,
 * keywords and hex colours, so it is the one a caller almost always wants.
 */
int robo_css_token_equals_ascii_ci(const robo_css_token_t *token,
                                   const char *ascii);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TOKEN_H */
