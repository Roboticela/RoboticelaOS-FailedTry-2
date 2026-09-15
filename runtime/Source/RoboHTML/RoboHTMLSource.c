/*
 * RoboHTML source/input abstraction (Phase 8.1).
 *
 * Borrowing-only view over application-owned bytes. RoboHTML never frees
 * source memory and never extends its lifetime.
 */

#include "RoboHTML/RoboHTMLSource.h"

#include <stddef.h>
#include <string.h>

static size_t bounded_strlen(const char *s)
{
    size_t n = 0u;
    if (!s) {
        return 0u;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int label_ascii_ok(const char *s, size_t cap, size_t *out_len)
{
    size_t n = bounded_strlen(s);
    size_t i;

    if (n == 0u || n >= cap) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20u || c >= 0x7Fu) {
            return 0;
        }
    }
    *out_len = n;
    return 1;
}

void robo_html_source_init(robo_html_source_t *src)
{
    if (!src) {
        return;
    }
    memset(src, 0, sizeof(*src));
    src->kind = ROBO_HTML_SOURCE_KIND_NONE;
}

robo_result_t robo_html_source_empty(robo_html_source_t *src,
                                     robo_html_diag_t *diag)
{
    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    robo_html_source_init(src);
    src->kind = ROBO_HTML_SOURCE_KIND_EMPTY;
    return ROBO_OK;
}

robo_result_t robo_html_source_memory(robo_html_source_t *src,
                                      const uint8_t *bytes,
                                      size_t length,
                                      robo_html_diag_t *diag)
{
    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    if (!bytes && length != 0u) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT,
                              "memory source: bytes NULL with non-zero length");
    }
    robo_html_source_init(src);
    src->kind = ROBO_HTML_SOURCE_KIND_MEMORY;
    src->bytes = bytes;
    src->length = bytes ? length : 0u;
    return ROBO_OK;
}

robo_result_t robo_html_source_string(robo_html_source_t *src,
                                      const char *utf8,
                                      robo_html_diag_t *diag)
{
    size_t n;

    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    robo_html_source_init(src);
    if (!utf8) {
        src->kind = ROBO_HTML_SOURCE_KIND_STRING;
        return ROBO_OK;
    }
    n = bounded_strlen(utf8);
    src->kind = ROBO_HTML_SOURCE_KIND_STRING;
    src->bytes = (const uint8_t *)utf8;
    src->length = n;
    return ROBO_OK;
}

robo_result_t robo_html_source_set_declared_encoding(robo_html_source_t *src,
                                                     const char *label,
                                                     robo_html_diag_t *diag)
{
    size_t n;

    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    if (!label) {
        /* Clearing the label is allowed. */
        src->encoding[0] = '\0';
        return ROBO_OK;
    }
    if (!label_ascii_ok(label, ROBO_HTML_ENCODING_LABEL_MAX, &n)) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_ENCODING,
                              ROBO_PARSE_ERROR,
                              "encoding label empty, too long, or non-ASCII");
    }
    memcpy(src->encoding, label, n + 1u);
    return ROBO_OK;
}

const char *robo_html_source_declared_encoding(const robo_html_source_t *src)
{
    if (!src) {
        return NULL;
    }
    return src->encoding;
}

robo_result_t robo_html_source_set_name(robo_html_source_t *src,
                                        const char *name,
                                        robo_html_diag_t *diag)
{
    size_t n;

    if (!src) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_INVALID_ARGUMENT, "source is NULL");
    }
    if (!name) {
        src->name[0] = '\0';
        return ROBO_OK;
    }
    n = bounded_strlen(name);
    if (n >= ROBO_HTML_URL_MAX) {
        return ROBO_HTML_DIAG(diag, ROBO_HTML_DOMAIN_SOURCE,
                              ROBO_NOT_SUPPORTED, "source name too long");
    }
    memcpy(src->name, name, n + 1u);
    return ROBO_OK;
}

const char *robo_html_source_name(const robo_html_source_t *src)
{
    if (!src) {
        return NULL;
    }
    return src->name;
}

size_t robo_html_source_length(const robo_html_source_t *src)
{
    if (!src) {
        return 0u;
    }
    if (src->kind == ROBO_HTML_SOURCE_KIND_STRING && src->bytes) {
        return bounded_strlen((const char *)src->bytes);
    }
    if (src->kind == ROBO_HTML_SOURCE_KIND_EMPTY ||
        src->kind == ROBO_HTML_SOURCE_KIND_NONE) {
        return 0u;
    }
    return src->length;
}

int robo_html_source_is_empty(const robo_html_source_t *src)
{
    if (!src) {
        return 1;
    }
    if (src->kind == ROBO_HTML_SOURCE_KIND_NONE ||
        src->kind == ROBO_HTML_SOURCE_KIND_EMPTY) {
        return 1;
    }
    return robo_html_source_length(src) == 0u;
}

robo_result_t robo_html_source_copy_bytes(const robo_html_source_t *src,
                                          uint8_t *out,
                                          size_t out_capacity)
{
    size_t n;

    if (!src || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = robo_html_source_length(src);
    if (n > out_capacity) {
        return ROBO_NOT_SUPPORTED;
    }
    if (n > 0u) {
        memcpy(out, src->bytes, n);
    }
    return ROBO_OK;
}

int robo_html_source_kind_is_known(robo_html_source_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_SOURCE_KIND_NONE:
    case ROBO_HTML_SOURCE_KIND_EMPTY:
    case ROBO_HTML_SOURCE_KIND_MEMORY:
    case ROBO_HTML_SOURCE_KIND_STRING:
    case ROBO_HTML_SOURCE_KIND_STREAM:
        return 1;
    default:
        return 0;
    }
}

const char *robo_html_source_kind_name(robo_html_source_kind_t kind)
{
    switch (kind) {
    case ROBO_HTML_SOURCE_KIND_NONE:
        return "none";
    case ROBO_HTML_SOURCE_KIND_EMPTY:
        return "empty";
    case ROBO_HTML_SOURCE_KIND_MEMORY:
        return "memory";
    case ROBO_HTML_SOURCE_KIND_STRING:
        return "string";
    case ROBO_HTML_SOURCE_KIND_STREAM:
        return "stream";
    default:
        return "unknown";
    }
}
