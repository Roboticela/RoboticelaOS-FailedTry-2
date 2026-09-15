/*
 * RoboHTML token model (Phase 8.2).
 *
 * Six token kinds, mirroring the HTML tokenizer output stages the future
 * tree builder (8.3) consumes. The model is an OUTPUT record: the tokenizer
 * fills it via robo_html_tokenizer_next() and the caller reads it, but
 * never owns or frees its buffers (see lifetime contract below).
 *
 * Lifetime contract:
 *   - A returned token is a borrowing view owned by the tokenizer.
 *   - Every string field (name, text, data, attribute names/values) is a
 *     borrowed pointer valid ONLY until the next robo_html_tokenizer_next()
 *     or robo_html_tokenizer_reset() call on the same tokenizer, or until
 *     the tokenizer is destroyed.
 *   - Attribute records live in the tokenizer's per-token attribute pool;
 *     the array pointer is likewise borrowed until the next token.
 *   - Callers that must keep data copy it out (robo_html_token_copy_*).
 *   - Tokens are plain structs: safe to copy, but copies keep only the
 *     borrowed pointers, so the validity rule still applies.
 *
 * All text in the model is UTF-8 (the runtime's native encoding per the
 * Phase 8.1 encoding boundary). Character references are decoded into
 * UTF-8 as tokens are emitted.
 */

#ifndef ROBO_HTML_TOKEN_H
#define ROBO_HTML_TOKEN_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_html_token_kind {
    ROBO_HTML_TOKEN_NONE = 0,
    /** One or more decoded characters (merging is a tree-builder concern). */
    ROBO_HTML_TOKEN_CHARACTER = 1,
    ROBO_HTML_TOKEN_START_TAG = 2,
    ROBO_HTML_TOKEN_END_TAG = 3,
    ROBO_HTML_TOKEN_COMMENT = 4,
    ROBO_HTML_TOKEN_DOCTYPE = 5,
    ROBO_HTML_TOKEN_EOF = 6
} robo_html_token_kind_t;

/** Non-zero if @p kind is a defined token kind enumerator. */
int robo_html_token_kind_is_known(robo_html_token_kind_t kind);

/** Human-readable token kind name; never NULL. */
const char *robo_html_token_kind_name(robo_html_token_kind_t kind);

/**
 * Source position of a token's first byte. Positions are recorded against
 * the attached source: @p offset is the byte offset from source start;
 * @p line starts at 1 and advances on LF (CRLF counts as one line break);
 * @p column counts Unicode code points since the last line break (1-based).
 */
typedef struct robo_html_token_pos {
    size_t offset;
    uint32_t line;
    uint32_t column;
} robo_html_token_pos_t;

/** One start/end-tag attribute (borrowed views; see lifetime contract). */
typedef struct robo_html_token_attr {
    /** ASCII-lowercased attribute name (NUL-terminated borrowed bytes). */
    const char *name;
    /** Attribute value, character references decoded (NUL-terminated). */
    const char *value;
} robo_html_token_attr_t;

typedef struct robo_html_token {
    robo_html_token_kind_t kind;

    /** Position of the token's first source byte. */
    robo_html_token_pos_t pos;

    union {
        struct {
            /** ASCII-lowercased tag name (borrowed). */
            const char *name;
            /** Attribute array (borrowed; valid until the next token). */
            const robo_html_token_attr_t *attrs;
            size_t attr_count;
            /** Non-zero when the source had "/>" for this tag. */
            int self_closing;
        } tag;
        struct {
            /** Decoded UTF-8 characters (borrowed; NUL-terminated). */
            const char *data;
            size_t length; /* bytes, excludes the NUL */
        } characters;
        struct {
            /** Raw comment payload (borrowed; no markup interpretation). */
            const char *data;
            size_t length;
        } comment;
        struct {
            /** Lowercased name after "DOCTYPE" (borrowed; usually "html"). */
            const char *name;
            /** Public identifier (borrowed; NULL when absent). */
            const char *public_id;
            /** System identifier (borrowed; NULL when absent). */
            const char *system_id;
            /** Force-quirks flag per the HTML doctype rules (diagnostic). */
            int force_quirks;
        } doctype;
    } u;
} robo_html_token_t;

/** Reset @p tok to kind NONE / empty. Safe on NULL. */
void robo_html_token_init(robo_html_token_t *tok);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TOKEN_H */
