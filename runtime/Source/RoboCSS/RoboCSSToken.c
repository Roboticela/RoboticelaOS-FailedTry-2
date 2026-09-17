/*
 * RoboCSS token model (Phase 9.1) — see RoboCSSToken.h.
 *
 * Note what is *not* here: no classification of bytes and no numeric
 * conversion. robo_css_token_make binds a type to a range and copies the
 * range's bytes into the token's text view, which is all a token record needs;
 * deciding that those bytes *are* an ident, or what number they spell, is the
 * tokenizer's job in 9.2.
 *
 * What is here, added by 9.2, is the other half of a token: its *value*. The
 * value accessors at the bottom of this file resolve escapes and strip the
 * delimiters the token's own type carries, working from `text` alone — a token
 * has no source pointer and needs none, which is what keeps a token a
 * self-contained value that can be copied, stored and compared at any time.
 * They allocate nothing: the comparisons decode escapes as they walk and the
 * only writer is the explicit copy into caller storage.
 */

#include "RoboCSS/RoboCSSToken.h"

#include "RoboCSSInternal.h"

#include <string.h>

int robo_css_token_type_is_known(robo_css_token_type_t type)
{
    return type >= ROBO_CSS_TOKEN_NONE &&
           type < (robo_css_token_type_t)ROBO_CSS_TOKEN_TYPE_COUNT;
}

const char *robo_css_token_type_name(robo_css_token_type_t type)
{
    switch (type) {
    case ROBO_CSS_TOKEN_NONE:
        return "none";
    case ROBO_CSS_TOKEN_IDENT:
        return "ident";
    case ROBO_CSS_TOKEN_FUNCTION:
        return "function";
    case ROBO_CSS_TOKEN_AT_KEYWORD:
        return "at-keyword";
    case ROBO_CSS_TOKEN_HASH:
        return "hash";
    case ROBO_CSS_TOKEN_STRING:
        return "string";
    case ROBO_CSS_TOKEN_BAD_STRING:
        return "bad-string";
    case ROBO_CSS_TOKEN_URL:
        return "url";
    case ROBO_CSS_TOKEN_BAD_URL:
        return "bad-url";
    case ROBO_CSS_TOKEN_DELIM:
        return "delim";
    case ROBO_CSS_TOKEN_NUMBER:
        return "number";
    case ROBO_CSS_TOKEN_PERCENTAGE:
        return "percentage";
    case ROBO_CSS_TOKEN_DIMENSION:
        return "dimension";
    case ROBO_CSS_TOKEN_WHITESPACE:
        return "whitespace";
    case ROBO_CSS_TOKEN_COMMENT:
        return "comment";
    case ROBO_CSS_TOKEN_CDO:
        return "cdo";
    case ROBO_CSS_TOKEN_CDC:
        return "cdc";
    case ROBO_CSS_TOKEN_COLON:
        return "colon";
    case ROBO_CSS_TOKEN_SEMICOLON:
        return "semicolon";
    case ROBO_CSS_TOKEN_COMMA:
        return "comma";
    case ROBO_CSS_TOKEN_LEFT_PAREN:
        return "left-paren";
    case ROBO_CSS_TOKEN_RIGHT_PAREN:
        return "right-paren";
    case ROBO_CSS_TOKEN_LEFT_SQUARE:
        return "left-square";
    case ROBO_CSS_TOKEN_RIGHT_SQUARE:
        return "right-square";
    case ROBO_CSS_TOKEN_LEFT_CURLY:
        return "left-curly";
    case ROBO_CSS_TOKEN_RIGHT_CURLY:
        return "right-curly";
    case ROBO_CSS_TOKEN_EOF:
        return "eof";
    case ROBO_CSS_TOKEN_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

int robo_css_token_type_is_numeric(robo_css_token_type_t type)
{
    return type == ROBO_CSS_TOKEN_NUMBER || type == ROBO_CSS_TOKEN_PERCENTAGE ||
           type == ROBO_CSS_TOKEN_DIMENSION;
}

int robo_css_token_type_is_ignorable(robo_css_token_type_t type)
{
    return type == ROBO_CSS_TOKEN_WHITESPACE || type == ROBO_CSS_TOKEN_COMMENT;
}

int robo_css_token_type_is_block_start(robo_css_token_type_t type)
{
    return type == ROBO_CSS_TOKEN_LEFT_PAREN || type == ROBO_CSS_TOKEN_LEFT_SQUARE ||
           type == ROBO_CSS_TOKEN_LEFT_CURLY;
}

int robo_css_token_type_is_block_end(robo_css_token_type_t type)
{
    return type == ROBO_CSS_TOKEN_RIGHT_PAREN || type == ROBO_CSS_TOKEN_RIGHT_SQUARE ||
           type == ROBO_CSS_TOKEN_RIGHT_CURLY;
}

int robo_css_token_type_matching_close(robo_css_token_type_t open_type,
                                      robo_css_token_type_t *out_close)
{
    robo_css_token_type_t close = ROBO_CSS_TOKEN_NONE;

    switch (open_type) {
    case ROBO_CSS_TOKEN_LEFT_PAREN:
        close = ROBO_CSS_TOKEN_RIGHT_PAREN;
        break;
    case ROBO_CSS_TOKEN_LEFT_SQUARE:
        close = ROBO_CSS_TOKEN_RIGHT_SQUARE;
        break;
    case ROBO_CSS_TOKEN_LEFT_CURLY:
        close = ROBO_CSS_TOKEN_RIGHT_CURLY;
        break;
    default:
        break;
    }
    if (out_close) {
        *out_close = close;
    }
    return close != ROBO_CSS_TOKEN_NONE;
}

int robo_css_token_type_matching_open(robo_css_token_type_t close_type,
                                     robo_css_token_type_t *out_open)
{
    robo_css_token_type_t open = ROBO_CSS_TOKEN_NONE;

    switch (close_type) {
    case ROBO_CSS_TOKEN_RIGHT_PAREN:
        open = ROBO_CSS_TOKEN_LEFT_PAREN;
        break;
    case ROBO_CSS_TOKEN_RIGHT_SQUARE:
        open = ROBO_CSS_TOKEN_LEFT_SQUARE;
        break;
    case ROBO_CSS_TOKEN_RIGHT_CURLY:
        open = ROBO_CSS_TOKEN_LEFT_CURLY;
        break;
    default:
        break;
    }
    if (out_open) {
        *out_open = open;
    }
    return open != ROBO_CSS_TOKEN_NONE;
}

int robo_css_hash_type_is_known(robo_css_hash_type_t type)
{
    return type >= ROBO_CSS_HASH_TYPE_NONE &&
           type < (robo_css_hash_type_t)ROBO_CSS_HASH_TYPE_COUNT;
}

const char *robo_css_hash_type_name(robo_css_hash_type_t type)
{
    switch (type) {
    case ROBO_CSS_HASH_TYPE_NONE:
        return "none";
    case ROBO_CSS_HASH_TYPE_ID:
        return "id";
    case ROBO_CSS_HASH_TYPE_UNRESTRICTED:
        return "unrestricted";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Tokens                                                             */
/* ------------------------------------------------------------------ */

void robo_css_token_init(robo_css_token_t *token)
{
    if (!token) {
        return;
    }
    token->type = ROBO_CSS_TOKEN_NONE;
    token->range = robo_css_range_at(0u);
    token->text = robo_css_slice_empty();
    token->number = 0.0;
    token->is_integer = 0;
    token->has_sign = 0;
    token->repr = robo_css_slice_empty();
    token->unit = robo_css_slice_empty();
    token->hash = ROBO_CSS_HASH_TYPE_NONE;
    token->closed = 0;
}

void robo_css_token_dispose(robo_css_token_t *token)
{
    robo_css_token_init(token);
}

robo_result_t robo_css_token_make(robo_css_token_t *token,
                                  robo_css_token_type_t type,
                                  const robo_css_source_t *source,
                                  robo_css_range_t range,
                                  robo_css_diag_t *diag)
{
    robo_css_slice_t text;

    if (!token || !source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "token or source is NULL");
    }
    if (!robo_css_token_type_is_known(type)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "unknown token type");
    }
    if (!robo_css_source_range_is_valid(source, range)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "range is not valid in the source");
    }
    if (robo_css_source_range_slice(source, range, &text) != ROBO_OK) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "range is not valid in the source");
    }
    robo_css_token_init(token);
    token->type = type;
    token->range = range;
    /* A slice with no bytes stays absent rather than becoming a NULL-plus-zero
     * view, so a caller can tell "this token has no text" from "this token's
     * text was empty", which for EOF is the difference that matters. */
    token->text = text;
    return ROBO_OK;
}

robo_result_t robo_css_token_make_eof(robo_css_token_t *token,
                                      const robo_css_source_t *source,
                                      size_t offset,
                                      robo_css_diag_t *diag)
{
    robo_result_t result;

    if (!token || !source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "token or source is NULL");
    }
    if (!robo_css_source_offset_is_valid(source, offset)) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "offset is past the end of the source");
    }
    robo_css_token_init(token);
    result = robo_css_token_make(token, ROBO_CSS_TOKEN_EOF, source,
                                 robo_css_range_at(offset), diag);
    if (result != ROBO_OK) {
        return result;
    }
    /* EOF carries no bytes by definition, whatever the empty range resolved to. */
    token->text = robo_css_slice_empty();
    return ROBO_OK;
}

robo_result_t robo_css_token_copy(const robo_css_token_t *token,
                                  robo_css_token_t *out_token,
                                  robo_css_diag_t *diag)
{
    if (!token || !out_token) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "token or output is NULL");
    }
    if (token == out_token) {
        return ROBO_OK;
    }
    *out_token = *token;
    return ROBO_OK;
}

int robo_css_token_is_none(const robo_css_token_t *token)
{
    return !token || token->type == ROBO_CSS_TOKEN_NONE;
}

int robo_css_token_is_terminator(const robo_css_token_t *token)
{
    if (!token) {
        return 1;
    }
    return token->type == ROBO_CSS_TOKEN_NONE || token->type == ROBO_CSS_TOKEN_EOF;
}

robo_css_slice_t robo_css_token_text(const robo_css_token_t *token)
{
    if (!token) {
        return robo_css_slice_empty();
    }
    return token->text;
}

int robo_css_token_equals_ascii(const robo_css_token_t *token, const char *ascii)
{
    if (!token) {
        return 0;
    }
    return robo_css_bytes_equal_ascii(token->text.bytes, token->text.length, ascii);
}

int robo_css_token_equals_ascii_ci(const robo_css_token_t *token, const char *ascii)
{
    if (!token) {
        return 0;
    }
    return robo_css_bytes_equal_ascii_ci(token->text.bytes, token->text.length, ascii);
}

/* ------------------------------------------------------------------ */
/* The token's value                                                   */
/* ------------------------------------------------------------------ */

/**
 * Non-zero for the token types CSS resolves escapes in.
 *
 * A COMMENT is deliberately not one: a backslash in a comment is a character,
 * not an escape, and the specification says so. Neither is a DIMENSION, whose
 * value is its numeric part — the unit is a name and is a separate slice.
 */
static int token_resolves_escapes(robo_css_token_type_t type)
{
    return type == ROBO_CSS_TOKEN_IDENT || type == ROBO_CSS_TOKEN_FUNCTION ||
           type == ROBO_CSS_TOKEN_AT_KEYWORD || type == ROBO_CSS_TOKEN_HASH ||
           type == ROBO_CSS_TOKEN_STRING || type == ROBO_CSS_TOKEN_BAD_STRING ||
           type == ROBO_CSS_TOKEN_URL || type == ROBO_CSS_TOKEN_BAD_URL;
}

static int bytes_are_quote(uint8_t b)
{
    return b == (uint8_t)'"' || b == (uint8_t)'\'';
}

/**
 * The span of `text` whose escapes resolve to the token's value: the delimiters
 * the token's own type contributes are removed, and nothing else is.
 *
 * Every removal is guarded, so a token built by hand (rather than produced by
 * the tokenizer) yields its whole text rather than a window outside it.
 */
static void token_value_window(const robo_css_token_t *token,
                               const uint8_t **out_bytes,
                               size_t *out_length)
{
    const uint8_t *bytes = token->text.bytes;
    size_t length = token->text.length;
    size_t begin = 0u;
    size_t end = length;

    switch (token->type) {
    case ROBO_CSS_TOKEN_FUNCTION:
        /* `name(` → `name`: the opening paren is part of the token, not of the
         * name, which is what a consumer comparing against "rgb" wants. */
        if (length > 0u && bytes[length - 1u] == (uint8_t)'(') {
            end = length - 1u;
        }
        break;
    case ROBO_CSS_TOKEN_AT_KEYWORD:
        if (length > 0u && bytes[0] == (uint8_t)'@') {
            begin = 1u;
        }
        break;
    case ROBO_CSS_TOKEN_HASH:
        if (length > 0u && bytes[0] == (uint8_t)'#') {
            begin = 1u;
        }
        break;
    case ROBO_CSS_TOKEN_DIMENSION:
        /* `10px` → `10`. The unit is raw and stays raw: reading it is 9.10. */
        if (token->unit.length > 0u && token->unit.length < length) {
            end = length - token->unit.length;
        }
        break;
    case ROBO_CSS_TOKEN_STRING:
    case ROBO_CSS_TOKEN_BAD_STRING:
        if (length > 0u && bytes_are_quote(bytes[0])) {
            begin = 1u;
        }
        if (token->type == ROBO_CSS_TOKEN_STRING && token->closed && end > begin &&
            bytes_are_quote(bytes[end - 1u])) {
            end -= 1u;
        }
        /* A string that never closed, and whose input ended on a backslash,
         * drops that backslash: it is the one byte the specification's
         * consume-a-string discards rather than keeping as content. The walk
         * steps over whole escapes, so a backslash that escaped the next byte
         * (`"a\\` , which the string did carry) is never the one dropped. */
        if (token->type == ROBO_CSS_TOKEN_STRING && !token->closed) {
            size_t pos = begin;
            size_t last = begin;

            while (pos < end) {
                if (bytes[pos] == (uint8_t)'\\') {
                    robo_css_escape_t escape =
                        robo_css_escape_scan(bytes + pos, end - pos);

                    if (escape.valid) {
                        pos += escape.length;
                        last = pos;
                        continue;
                    }
                    last = (pos + 1u < end) ? (pos + 1u) : pos;
                    pos += 1u;
                    continue;
                }
                pos++;
                last = pos;
            }
            end = last;
        }
        break;
    case ROBO_CSS_TOKEN_URL:
    case ROBO_CSS_TOKEN_BAD_URL:
        if (length >= 4u && robo_css_bytes_equal_ascii_ci(bytes, 4u, "url(")) {
            begin = 4u;
        }
        /* Only a *closed* url loses its `)`. A bad-url keeps its remnants —
         * there is no value to give it, and inventing one would be worse than
         * handing back what is actually there. */
        if (token->type == ROBO_CSS_TOKEN_URL && token->closed && end > begin &&
            bytes[end - 1u] == (uint8_t)')') {
            end -= 1u;
        }
        /* `url(   foo.png   )` has a value of `foo.png`: the padding is inside
         * the token's range, because the range has to tile the source, and
         * outside its value, because it is not part of the url. The walk is
         * escape-aware so `url(a\ )` keeps the space it spelled — an escaped
         * space is content, not padding. */
        if (token->type == ROBO_CSS_TOKEN_URL) {
            size_t pos = begin;
            size_t last = begin;

            while (pos < end) {
                if (bytes[pos] == (uint8_t)'\\') {
                    robo_css_escape_t escape =
                        robo_css_escape_scan(bytes + pos, end - pos);

                    if (escape.valid) {
                        pos += escape.length;
                        last = pos;
                        continue;
                    }
                }
                if (!robo_css_ascii_is_space(bytes[pos])) {
                    last = pos + 1u;
                }
                pos++;
            }
            while (begin < last && robo_css_ascii_is_space(bytes[begin])) {
                begin++;
            }
            end = last;
        }
        break;
    default:
        break;
    }
    if (end < begin) {
        end = begin;
    }
    *out_bytes = bytes ? bytes + begin : NULL;
    *out_length = end - begin;
}

/**
 * A cursor over a value window that emits the value one piece at a time, each
 * piece either a run of source bytes or the encoding of a hexadecimal escape.
 */
typedef struct token_value_cursor {
    const uint8_t *bytes;
    size_t length;
    size_t offset;
    int resolve;
    /**
     * 1 when a backslash before a newline is a *line continuation* rather than
     * two characters. It is the string rule: a name or a url never reaches this
     * state, because their scanners refuse such a backslash and end the token
     * before it, so the byte is never in the window being walked.
     */
    int line_continuation;
} token_value_cursor_t;

static void token_value_cursor_init(token_value_cursor_t *cursor,
                                    const robo_css_token_t *token)
{
    token_value_window(token, &cursor->bytes, &cursor->length);
    cursor->offset = 0u;
    cursor->resolve = token_resolves_escapes(token->type);
    cursor->line_continuation = token->type == ROBO_CSS_TOKEN_STRING;
}

/**
 * Emit the next piece. @p scratch holds the encoding of a hexadecimal escape,
 * which is the one piece whose bytes are not already in the source.
 * @return 1 when a piece was written, 0 at the end of the window.
 */
static int token_value_next(token_value_cursor_t *cursor,
                            uint8_t scratch[4],
                            robo_css_slice_t *out)
{
    for (;;) {
        size_t start;

        if (cursor->offset >= cursor->length) {
            return 0;
        }
        start = cursor->offset;

        if (cursor->resolve && cursor->bytes[start] == (uint8_t)'\\') {
            robo_css_escape_t escape =
                robo_css_escape_scan(cursor->bytes + start, cursor->length - start);

            if (escape.valid) {
                cursor->offset = start + escape.length;
                if (escape.is_hex) {
                    size_t written = 0u;

                    if (robo_css_utf8_append(scratch, 4u, &written, escape.code_point)) {
                        *out = robo_css_slice_from_bytes(scratch, written);
                        return 1;
                    }
                    /* Cannot happen for a scalar (four bytes is the maximum), but
                     * a refusal must still emit something rather than lose the
                     * byte. */
                    scratch[0] = 0xEFu;
                    scratch[1] = 0xBFu;
                    scratch[2] = 0xBDu;
                    *out = robo_css_slice_from_bytes(scratch, 3u);
                    return 1;
                }
                /* A literal escape stands for a character the source already
                 * spells, so its bytes are handed straight through — which is
                 * why an escaped multi-byte character is never re-encoded. */
                *out = robo_css_slice_from_bytes(cursor->bytes + start + 1u,
                                                 escape.length - 1u);
                return 1;
            }
            /* A backslash before a newline inside a string is the line
             * continuation the specification defines: both bytes are consumed
             * and *nothing* is appended in their place, which is what lets a
             * long string be wrapped. Skipping rather than emitting is the whole
             * of the rule, so the walk continues to the next real piece. */
            if (cursor->line_continuation && start + 1u < cursor->length &&
                robo_css_is_newline_byte(cursor->bytes[start + 1u])) {
                cursor->offset = start + 1u +
                                 robo_css_newline_length(cursor->bytes + start + 1u,
                                                         cursor->length - start - 1u);
                continue;
            }
            /* Otherwise an invalid escape is a backslash like any other byte:
             * CSS only treats it as one when a valid escape starts there, and
             * the tokenizer is what decides whether that can happen in this
             * token type. */
        }

        cursor->offset = start + 1u;
        while (cursor->offset < cursor->length) {
            if (cursor->resolve && cursor->bytes[cursor->offset] == (uint8_t)'\\') {
                break;
            }
            cursor->offset++;
        }
        *out = robo_css_slice_from_bytes(cursor->bytes + start, cursor->offset - start);
        return 1;
    }
}

size_t robo_css_token_value_length(const robo_css_token_t *token)
{
    token_value_cursor_t cursor;
    uint8_t scratch[4];
    robo_css_slice_t piece;
    size_t total = 0u;

    if (!token) {
        return 0u;
    }
    token_value_cursor_init(&cursor, token);
    while (token_value_next(&cursor, scratch, &piece)) {
        total += piece.length;
    }
    return total;
}

/**
 * The shared comparison behind both equality accessors: walk the value and the
 * ASCII needle together, a byte at a time.
 *
 * A piece may be a run of several bytes, so it is compared byte by byte rather
 * than assumed to be one character: an escape decodes to a piece that is one
 * character, but an escape-free stretch of the value comes back as a run of
 * many, and a run reading `url` matches the needle `url`. A piece holding a
 * multi-byte scalar cannot match an ASCII needle, and the byte comparison
 * settles that on its own — every continuation byte is above 0x7F.
 */
static int token_value_equals_ascii_impl(const robo_css_token_t *token,
                                         const char *ascii,
                                         int ignore_case)
{
    token_value_cursor_t cursor;
    uint8_t scratch[4];
    robo_css_slice_t piece;
    size_t index = 0u;

    if (!token || !ascii) {
        return 0;
    }
    token_value_cursor_init(&cursor, token);
    while (token_value_next(&cursor, scratch, &piece)) {
        size_t i;

        for (i = 0u; i < piece.length; i++) {
            uint8_t actual;
            uint8_t expected;

            if (ascii[index] == '\0') {
                return 0; /* the value is longer than the needle */
            }
            actual = piece.bytes[i];
            expected = (uint8_t)ascii[index];
            if (ignore_case) {
                actual = robo_css_ascii_lower(actual);
                expected = robo_css_ascii_lower(expected);
            }
            if (actual != expected) {
                return 0;
            }
            index++;
        }
    }
    return ascii[index] == '\0';
}

int robo_css_token_value_equals_ascii(const robo_css_token_t *token,
                                      const char *ascii)
{
    return token_value_equals_ascii_impl(token, ascii, 0);
}

int robo_css_token_value_equals_ascii_ci(const robo_css_token_t *token,
                                         const char *ascii)
{
    return token_value_equals_ascii_impl(token, ascii, 1);
}

robo_result_t robo_css_token_value_copy(const robo_css_token_t *token,
                                        uint8_t *out,
                                        size_t out_capacity,
                                        size_t *out_length,
                                        robo_css_diag_t *diag)
{
    token_value_cursor_t cursor;
    uint8_t scratch[4];
    robo_css_slice_t piece;
    size_t written = 0u;

    if (!token || !out || !out_length) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                             "token, output or length is NULL");
    }
    token_value_cursor_init(&cursor, token);
    while (token_value_next(&cursor, scratch, &piece)) {
        if (written + piece.length > out_capacity) {
            /* Report what is needed as well as what went wrong: a caller sizing
             * a second attempt has no other way to learn the size. */
            *out_length = robo_css_token_value_length(token);
            return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_INVALID_ARGUMENT,
                                 "output capacity is smaller than the token value");
        }
        memcpy(out + written, piece.bytes, piece.length);
        written += piece.length;
    }
    *out_length = written;
    return ROBO_OK;
}

int robo_css_token_has_escapes(const robo_css_token_t *token)
{
    const uint8_t *bytes = NULL;
    size_t length = 0u;
    size_t i;

    if (!token || !token_resolves_escapes(token->type)) {
        return 0;
    }
    token_value_window(token, &bytes, &length);
    for (i = 0u; i < length; i++) {
        if (bytes[i] == (uint8_t)'\\') {
            return 1;
        }
    }
    return 0;
}
