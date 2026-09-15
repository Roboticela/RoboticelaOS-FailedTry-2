/*
 * Token storage (Phase 8.2).
 *
 * Per-token scratch owned by the tokenizer:
 *   - one text buffer (growable) reused for tag names, comment payloads,
 *     doctype fields, and character runs;
 *   - one attribute pool (name buffer + value buffer per attribute).
 *
 * RoboHTMLToken.c provides these + helpers that keep the borrow lifetime:
 * data survives until the next token begins (flush) or reset/destroy.
 */

#include "RoboHTMLInternal.h"
#include "RoboHTML/RoboHTMLToken.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void robo_html_token_init(robo_html_token_t *tok)
{
    if (!tok) {
        return;
    }
    memset(tok, 0, sizeof(*tok));
    tok->kind = ROBO_HTML_TOKEN_NONE;
}

int robo_html_token_kind_is_known(robo_html_token_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_TOKEN_NONE:
    case ROBO_HTML_TOKEN_CHARACTER:
    case ROBO_HTML_TOKEN_START_TAG:
    case ROBO_HTML_TOKEN_END_TAG:
    case ROBO_HTML_TOKEN_COMMENT:
    case ROBO_HTML_TOKEN_DOCTYPE:
    case ROBO_HTML_TOKEN_EOF:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_token_kind_name(robo_html_token_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_TOKEN_NONE:
        return "none";
    case ROBO_HTML_TOKEN_CHARACTER:
        return "character";
    case ROBO_HTML_TOKEN_START_TAG:
        return "start-tag";
    case ROBO_HTML_TOKEN_END_TAG:
        return "end-tag";
    case ROBO_HTML_TOKEN_COMMENT:
        return "comment";
    case ROBO_HTML_TOKEN_DOCTYPE:
        return "doctype";
    case ROBO_HTML_TOKEN_EOF:
        return "eof";
    default:
        return "unknown";
    }
}

void robo_html_token_buffer_init(robo_html_token_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    buf->data = NULL;
    buf->length = 0u;
    buf->capacity = 0u;
}

void robo_html_token_buffer_free(robo_html_token_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    robo_free(buf->data);
    buf->data = NULL;
    buf->length = 0u;
    buf->capacity = 0u;
}

robo_result_t robo_html_token_buffer_reserve(robo_html_token_buffer_t *buf,
                                             size_t extra)
{
    size_t need;

    if (!buf) {
        return ROBO_INVALID_ARGUMENT;
    }
    need = buf->length + extra + 1u; /* + NUL */
    if (need <= buf->capacity) {
        return ROBO_OK;
    }
    {
        size_t cap = buf->capacity ? buf->capacity : 64u;
        char *grown;
        while (cap < need) {
            if (cap > (SIZE_MAX / 2u)) {
                cap = need;
                break;
            }
            cap *= 2u;
        }
        grown = (char *)robo_realloc(buf->data, cap);
        if (!grown) {
            return ROBO_OUT_OF_MEMORY;
        }
        buf->data = grown;
        buf->capacity = cap;
    }
    return ROBO_OK;
}

void robo_html_token_buffer_clear(robo_html_token_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    buf->length = 0u;
    if (buf->data && buf->capacity > 0u) {
        buf->data[0] = '\0';
    }
}

robo_result_t robo_html_token_buffer_append_byte(robo_html_token_buffer_t *buf,
                                                 char byte)
{
    robo_result_t r = robo_html_token_buffer_reserve(buf, 1u);

    if (r != ROBO_OK) {
        return r;
    }
    buf->data[buf->length++] = byte;
    buf->data[buf->length] = '\0';
    return ROBO_OK;
}

robo_result_t robo_html_token_buffer_append_utf8(
    robo_html_token_buffer_t *buf, uint32_t cp)
{
    char tmp[8];
    size_t written = 0u;
    size_t need = robo_unicode_utf8_encode_len(cp);

    if (need == 0u || need > sizeof(tmp)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_unicode_utf8_encode(cp, tmp, sizeof(tmp), &written) !=
        ROBO_UNICODE_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    {
        robo_result_t r = robo_html_token_buffer_reserve(buf, written);
        size_t i;
        if (r != ROBO_OK) {
            return r;
        }
        for (i = 0u; i < written; i++) {
            buf->data[buf->length++] = tmp[i];
        }
        buf->data[buf->length] = '\0';
    }
    return ROBO_OK;
}

void robo_html_token_pool_init(robo_html_token_pool_t *pool)
{
    if (!pool) {
        return;
    }
    pool->count = 0u;
    pool->capacity = 0u;
    pool->entries = NULL;
}

void robo_html_token_pool_free(robo_html_token_pool_t *pool)
{
    size_t i;

    if (!pool) {
        return;
    }
    for (i = 0u; i < pool->capacity; i++) {
        robo_html_token_buffer_free(&pool->entries[i].name);
        robo_html_token_buffer_free(&pool->entries[i].value);
    }
    robo_free(pool->entries);
    pool->entries = NULL;
    pool->count = 0u;
    pool->capacity = 0u;
}

void robo_html_token_pool_clear(robo_html_token_pool_t *pool)
{
    if (!pool) {
        return;
    }
    pool->count = 0u;
}

robo_result_t robo_html_token_pool_next(robo_html_token_pool_t *pool,
                                        robo_html_token_attr_entry_t **out)
{
    if (!pool || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (pool->count >= pool->capacity) {
        size_t cap = pool->capacity ? pool->capacity * 2u : 8u;
        robo_html_token_attr_entry_t *grown =
            (robo_html_token_attr_entry_t *)robo_realloc(
                pool->entries, cap * sizeof(*grown));
        size_t i;
        if (!grown) {
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = pool->capacity; i < cap; i++) {
            robo_html_token_buffer_init(&grown[i].name);
            robo_html_token_buffer_init(&grown[i].value);
        }
        pool->entries = grown;
        pool->capacity = cap;
    }
    *out = &pool->entries[pool->count++];
    return ROBO_OK;
}

void robo_html_diagnostics_init(robo_html_diag_list_t *list)
{
    if (!list) {
        return;
    }
    list->count = 0u;
}

void robo_html_diagnostics_push(robo_html_diag_list_t *list,
                                robo_result_t code,
                                robo_html_diagnostic_domain_t domain,
                                const char *message,
                                uint32_t line,
                                uint32_t column)
{
    robo_html_diag_t *d;

    if (!list || list->count >= ROBO_HTML_TOKENIZER_MAX_DIAGNOSTICS) {
        return; /* bounded: drop extras deterministically */
    }
    d = &list->items[list->count++];
    d->code = code;
    d->domain = domain;
    d->message = message;
    d->file = NULL;
    d->line = (int)line;
    (void)column;
}

void robo_html_diagnostics_clear(robo_html_diag_list_t *list)
{
    if (!list) {
        return;
    }
    list->count = 0u;
}
