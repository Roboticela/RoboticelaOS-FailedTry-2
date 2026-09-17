/*
 * RoboCSS token model (Phase 9.1) — see RoboCSSToken.h.
 *
 * Note what is *not* here: no classification of bytes, no escape handling, no
 * numeric conversion. robo_css_token_make binds a type to a range and copies
 * the range's bytes into the token's text view, which is all a token record
 * needs; deciding that those bytes *are* an ident, or what number they spell,
 * is the tokenizer's job in 9.2.
 */

#include "RoboCSS/RoboCSSToken.h"

#include "RoboCSSInternal.h"

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
