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

void HtmlTokenInit(HtmlToken *tok)
{
    if (!tok) {
        return;
    }
    memset(tok, 0, sizeof(*tok));
    tok->kind = RoboHtmlTokenNone;
}

int HtmlTokenKindIsKnown(HtmlTokenKind kind)
{
    switch (kind) {
    case RoboHtmlTokenNone:
    case RoboHtmlTokenCharacter:
    case RoboHtmlTokenStartTag:
    case RoboHtmlTokenEndTag:
    case RoboHtmlTokenComment:
    case RoboHtmlTokenDoctype:
    case RoboHtmlTokenEof:
        return 1;
    default:
        return 0;
    }
}

const char *HtmlTokenKindName(HtmlTokenKind kind)
{
    switch (kind) {
    case RoboHtmlTokenNone:
        return "none";
    case RoboHtmlTokenCharacter:
        return "character";
    case RoboHtmlTokenStartTag:
        return "start-tag";
    case RoboHtmlTokenEndTag:
        return "end-tag";
    case RoboHtmlTokenComment:
        return "comment";
    case RoboHtmlTokenDoctype:
        return "doctype";
    case RoboHtmlTokenEof:
        return "eof";
    default:
        return "unknown";
    }
}

void HtmlTokenBufferInit(HtmlTokenBuffer *buf)
{
    if (!buf) {
        return;
    }
    buf->data = NULL;
    buf->length = 0u;
    buf->capacity = 0u;
}

void HtmlTokenBufferFree(HtmlTokenBuffer *buf)
{
    if (!buf) {
        return;
    }
    RoboFree(buf->data);
    buf->data = NULL;
    buf->length = 0u;
    buf->capacity = 0u;
}

Result HtmlTokenBufferReserve(HtmlTokenBuffer *buf,
                                             size_t extra)
{
    size_t need;

    if (!buf) {
        return RoboInvalidArgument;
    }
    need = buf->length + extra + 1u; /* + NUL */
    if (need <= buf->capacity) {
        return RoboOk;
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
        grown = (char *)Realloc(buf->data, cap);
        if (!grown) {
            return RoboOutOfMemory;
        }
        buf->data = grown;
        buf->capacity = cap;
    }
    return RoboOk;
}

void HtmlTokenBufferClear(HtmlTokenBuffer *buf)
{
    if (!buf) {
        return;
    }
    buf->length = 0u;
    if (buf->data && buf->capacity > 0u) {
        buf->data[0] = '\0';
    }
}

Result HtmlTokenBufferAppendByte(HtmlTokenBuffer *buf,
                                                 char byte)
{
    Result r = HtmlTokenBufferReserve(buf, 1u);

    if (r != RoboOk) {
        return r;
    }
    buf->data[buf->length++] = byte;
    buf->data[buf->length] = '\0';
    return RoboOk;
}

Result HtmlTokenBufferAppendUtf8(
    HtmlTokenBuffer *buf, uint32_t cp)
{
    char tmp[8];
    size_t written = 0u;
    size_t need = UnicodeUtf8EncodeLen(cp);

    if (need == 0u || need > sizeof(tmp)) {
        return RoboInvalidArgument;
    }
    if (UnicodeUtf8Encode(cp, tmp, sizeof(tmp), &written) !=
        RoboUnicodeOk) {
        return RoboInvalidArgument;
    }
    {
        Result r = HtmlTokenBufferReserve(buf, written);
        size_t i;
        if (r != RoboOk) {
            return r;
        }
        for (i = 0u; i < written; i++) {
            buf->data[buf->length++] = tmp[i];
        }
        buf->data[buf->length] = '\0';
    }
    return RoboOk;
}

void HtmlTokenPoolInit(HtmlTokenPool *pool)
{
    if (!pool) {
        return;
    }
    pool->count = 0u;
    pool->capacity = 0u;
    pool->entries = NULL;
}

void HtmlTokenPoolFree(HtmlTokenPool *pool)
{
    size_t i;

    if (!pool) {
        return;
    }
    for (i = 0u; i < pool->capacity; i++) {
        HtmlTokenBufferFree(&pool->entries[i].name);
        HtmlTokenBufferFree(&pool->entries[i].value);
    }
    RoboFree(pool->entries);
    pool->entries = NULL;
    pool->count = 0u;
    pool->capacity = 0u;
}

void HtmlTokenPoolClear(HtmlTokenPool *pool)
{
    if (!pool) {
        return;
    }
    pool->count = 0u;
}

Result HtmlTokenPoolNext(HtmlTokenPool *pool,
                                        HtmlTokenAttrEntry **out)
{
    if (!pool || !out) {
        return RoboInvalidArgument;
    }
    if (pool->count >= pool->capacity) {
        size_t cap = pool->capacity ? pool->capacity * 2u : 8u;
        HtmlTokenAttrEntry *grown =
            (HtmlTokenAttrEntry *)Realloc(
                pool->entries, cap * sizeof(*grown));
        size_t i;
        if (!grown) {
            return RoboOutOfMemory;
        }
        for (i = pool->capacity; i < cap; i++) {
            HtmlTokenBufferInit(&grown[i].name);
            HtmlTokenBufferInit(&grown[i].value);
        }
        pool->entries = grown;
        pool->capacity = cap;
    }
    {
        HtmlTokenAttrEntry *entry = &pool->entries[pool->count++];

        /*
         * An entry is only ever handed out to start a *new* attribute, so it
         * must arrive empty. The pool is reused across tokens and across
         * attributes within one token: rewinding the cursor
         * (HtmlTokenPoolClear) is the cheap half of that reuse, and
         * this is the half that makes it correct. Without it the tokenizer
         * *appends* to whatever the previous token left in the slot —
         * `<div class=... id=...>` followed by `<input disabled value=...>`
         * produced "idvalue" as an attribute name — silently corrupting
         * every attribute that reuses a pool slot.
         */
        HtmlTokenBufferClear(&entry->name);
        HtmlTokenBufferClear(&entry->value);
        *out = entry;
    }
    return RoboOk;
}

void HtmlDiagnosticsInit(HtmlDiagList *list)
{
    if (!list) {
        return;
    }
    list->count = 0u;
}

void HtmlDiagnosticsPush(HtmlDiagList *list,
                                Result code,
                                HtmlDiagnosticDomain domain,
                                const char *message,
                                uint32_t line,
                                uint32_t column)
{
    HtmlDiag *d;

    if (!list || list->count >= RoboHtmlTokenizerMaxDiagnostics) {
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

void HtmlDiagnosticsClear(HtmlDiagList *list)
{
    if (!list) {
        return;
    }
    list->count = 0u;
}
