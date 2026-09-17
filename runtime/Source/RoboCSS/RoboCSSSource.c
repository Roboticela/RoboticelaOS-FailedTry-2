/*
 * RoboCSS source model (Phase 9.1) — see RoboCSSSource.h.
 *
 * Two invariants make the rest of this file simple:
 *
 *   1. Exactly one source kind owns bytes, and it is COPY. Every other kind
 *      borrows or has none, so `dispose` is "free if COPY, then forget".
 *   2. Every configuration path disposes whatever the source held first. A
 *      source is therefore never half-configured — either the new
 *      configuration succeeded, or the source is back to KIND_NONE.
 */

#include "RoboCSS/RoboCSSSource.h"

#include "RoboCSSInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

int robo_css_source_kind_is_known(robo_css_source_kind_t kind)
{
    return kind >= ROBO_CSS_SOURCE_KIND_NONE &&
           kind < (robo_css_source_kind_t)ROBO_CSS_SOURCE_KIND_COUNT;
}

const char *robo_css_source_kind_name(robo_css_source_kind_t kind)
{
    switch (kind) {
    case ROBO_CSS_SOURCE_KIND_NONE:
        return "none";
    case ROBO_CSS_SOURCE_KIND_EMPTY:
        return "empty";
    case ROBO_CSS_SOURCE_KIND_MEMORY:
        return "memory";
    case ROBO_CSS_SOURCE_KIND_STRING:
        return "string";
    case ROBO_CSS_SOURCE_KIND_COPY:
        return "copy";
    default:
        return "unknown";
    }
}

int robo_css_source_kind_is_owning(robo_css_source_kind_t kind)
{
    return kind == ROBO_CSS_SOURCE_KIND_COPY;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

void robo_css_source_init(robo_css_source_t *source)
{
    if (!source) {
        return;
    }
    source->kind = ROBO_CSS_SOURCE_KIND_NONE;
    source->bytes = NULL;
    source->length = 0u;
    source->name[0] = '\0';
}

void robo_css_source_dispose(robo_css_source_t *source)
{
    if (!source) {
        return;
    }
    if (source->kind == ROBO_CSS_SOURCE_KIND_COPY && source->bytes) {
        robo_free((void *)source->bytes);
    }
    robo_css_source_init(source);
}

/** Free whatever the source holds but keep its label. */
static void source_release_content(robo_css_source_t *source)
{
    if (source->kind == ROBO_CSS_SOURCE_KIND_COPY && source->bytes) {
        robo_free((void *)source->bytes);
    }
    source->kind = ROBO_CSS_SOURCE_KIND_NONE;
    source->bytes = NULL;
    source->length = 0u;
}

robo_result_t robo_css_source_set_empty(robo_css_source_t *source,
                                       robo_css_diag_t *diag)
{
    if (!source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
                             "source is NULL");
    }
    source_release_content(source);
    source->kind = ROBO_CSS_SOURCE_KIND_EMPTY;
    return ROBO_OK;
}

robo_result_t robo_css_source_set_memory(robo_css_source_t *source,
                                        const uint8_t *bytes,
                                        size_t length,
                                        robo_css_diag_t *diag)
{
    if (!source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
                             "source is NULL");
    }
    if (!bytes && length > 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
                             "NULL bytes with a non-zero length");
    }
    source_release_content(source);
    if (length == 0u) {
        /* A zero-length memory source is an empty source, not a borrow of
         * nothing: the caller's pointer would be meaningless to keep. */
        source->kind = ROBO_CSS_SOURCE_KIND_EMPTY;
        return ROBO_OK;
    }
    source->kind = ROBO_CSS_SOURCE_KIND_MEMORY;
    source->bytes = bytes;
    source->length = length;
    return ROBO_OK;
}

robo_result_t robo_css_source_set_string(robo_css_source_t *source,
                                        const char *utf8,
                                        robo_css_diag_t *diag)
{
    if (!source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
                             "source is NULL");
    }
    if (!utf8) {
        return robo_css_source_set_empty(source, diag);
    }
    /* Borrow as KIND_STRING rather than delegating to set_memory. The two kinds
     * describe the same borrowed bytes, but they are not interchangeable: the
     * caller asked for the NUL-terminated one, and the kind is what the model
     * reports back, what the source's own description says, and what a later
     * phase reads to know whether it may assume a terminator. A kind no setter
     * can produce is a kind that does not exist. */
    source_release_content(source);
    if (utf8[0] == '\0') {
        /* Same rule as set_memory: nothing to borrow is an empty source, even
         * though a valid pointer was passed. */
        source->kind = ROBO_CSS_SOURCE_KIND_EMPTY;
        return ROBO_OK;
    }
    source->kind = ROBO_CSS_SOURCE_KIND_STRING;
    source->bytes = (const uint8_t *)utf8;
    source->length = strlen(utf8);
    return ROBO_OK;
}

robo_result_t robo_css_source_copy(robo_css_source_t *source,
                                  const uint8_t *bytes,
                                  size_t length,
                                  robo_css_diag_t *diag)
{
    uint8_t *copy;

    if (!source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
                             "source is NULL");
    }
    if (!bytes && length > 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
                             "NULL bytes with a non-zero length");
    }
    source_release_content(source);
    if (length == 0u) {
        /* A copy of nothing allocates nothing: an empty source is the honest
         * representation of an empty copy, and it has no ownership to manage. */
        source->kind = ROBO_CSS_SOURCE_KIND_EMPTY;
        return ROBO_OK;
    }
    copy = (uint8_t *)robo_malloc(length);
    if (!copy) {
        /* Nothing was configured: the source is back to KIND_NONE, never
         * half-configured with a length it cannot back with bytes. */
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_OUT_OF_MEMORY,
                             "could not allocate the source copy");
    }
    memcpy(copy, bytes, length);
    source->kind = ROBO_CSS_SOURCE_KIND_COPY;
    source->bytes = copy;
    source->length = length;
    return ROBO_OK;
}

robo_result_t robo_css_source_set_name(robo_css_source_t *source,
                                      const char *name,
                                      robo_css_diag_t *diag)
{
    if (!source) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_SOURCE, ROBO_INVALID_ARGUMENT,
                             "source is NULL");
    }
    robo_css_copy_label(source->name, sizeof(source->name), name);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

const char *robo_css_source_name(const robo_css_source_t *source)
{
    if (!source) {
        return NULL;
    }
    return source->name;
}

const uint8_t *robo_css_source_bytes(const robo_css_source_t *source)
{
    return source ? source->bytes : NULL;
}

size_t robo_css_source_length(const robo_css_source_t *source)
{
    return source ? source->length : 0u;
}

int robo_css_source_is_empty(const robo_css_source_t *source)
{
    return !source || source->length == 0u;
}

int robo_css_source_is_owned(const robo_css_source_t *source)
{
    return source && source->kind == ROBO_CSS_SOURCE_KIND_COPY;
}

int robo_css_source_is_configured(const robo_css_source_t *source)
{
    return source && source->kind != ROBO_CSS_SOURCE_KIND_NONE;
}

robo_css_slice_t robo_css_source_slice(const robo_css_source_t *source)
{
    if (!source || !source->bytes || source->length == 0u) {
        return robo_css_slice_empty();
    }
    return robo_css_slice_from_bytes(source->bytes, source->length);
}

/* ------------------------------------------------------------------ */
/* Bounds, positions, ranges                                           */
/* ------------------------------------------------------------------ */

int robo_css_source_offset_is_valid(const robo_css_source_t *source, size_t offset)
{
    if (!source) {
        return 0;
    }
    return offset <= source->length;
}

robo_result_t robo_css_source_position(const robo_css_source_t *source,
                                      size_t offset,
                                      robo_css_position_t *out_position)
{
    size_t i = 0u;
    uint32_t line = 1u;
    uint32_t column = 1u;

    if (!source || !out_position) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (offset > source->length) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Nothing valid can be counted in a source with no bytes; offset 0 there
     * is line 1, column 1 rather than "no position", because the caller did
     * name a real place: the start. */
    while (i < offset) {
        uint8_t byte = source->bytes[i];

        if (byte == (uint8_t)'\r') {
            if (i + 1u < offset && source->bytes[i + 1u] == (uint8_t)'\n') {
                i += 2u;
            } else {
                i += 1u;
            }
            line++;
            column = 1u;
            continue;
        }
        /* A form feed ends a line exactly as LF and CR do: CSS defines its
         * newline as the three of them, and robo_css_is_newline_byte says the
         * same thing — counting only two of the three would make a position
         * disagree with the tokenizer that produced it. */
        if (byte == (uint8_t)'\n' || byte == (uint8_t)'\f') {
            i += 1u;
            line++;
            column = 1u;
            continue;
        }
        i += robo_css_utf8_scalar_length(source->bytes + i, source->length - i);
        column++;
    }
    out_position->offset = offset;
    out_position->line = line;
    out_position->column = column;
    return ROBO_OK;
}

size_t robo_css_source_line_count(const robo_css_source_t *source)
{
    size_t i = 0u;
    size_t breaks = 0u;

    if (!source || source->length == 0u) {
        return 0u;
    }
    while (i < source->length) {
        uint8_t byte = source->bytes[i];

        if (byte == (uint8_t)'\r') {
            breaks++;
            i++;
            if (i < source->length && source->bytes[i] == (uint8_t)'\n') {
                i++;
            }
            continue;
        }
        if (byte == (uint8_t)'\n') {
            breaks++;
            i++;
            continue;
        }
        i++;
    }
    /* One line per break, plus the line the last break opened — which is why a
     * source ending in a break counts the empty line after it, and why the
     * highest position robo_css_source_position can report agrees with this. */
    return breaks + 1u;
}

int robo_css_source_range_is_valid(const robo_css_source_t *source,
                                   robo_css_range_t range)
{
    if (!source) {
        return 0;
    }
    if (range.begin > range.end) {
        return 0;
    }
    return range.end <= source->length;
}

robo_result_t robo_css_source_range_slice(const robo_css_source_t *source,
                                         robo_css_range_t range,
                                         robo_css_slice_t *out_slice)
{
    if (!out_slice) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_slice = robo_css_slice_empty();
    if (!source) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_css_source_range_is_valid(source, range)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (range.begin == range.end) {
        return ROBO_OK; /* a valid empty range yields an empty slice */
    }
    *out_slice = robo_css_slice_from_bytes(source->bytes + range.begin,
                                           range.end - range.begin);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Describing the bytes (never rewriting)                              */
/* ------------------------------------------------------------------ */

int robo_css_source_is_valid_utf8(const robo_css_source_t *source)
{
    size_t i = 0u;

    if (!source || source->length == 0u) {
        return 1;
    }
    while (i < source->length) {
        size_t length = robo_css_utf8_scalar_length(source->bytes + i,
                                                   source->length - i);

        if (length == 1u && source->bytes[i] >= 0x80u) {
            return 0; /* an ill-formed scalar: we never repair, only report */
        }
        i += length;
    }
    return 1;
}

size_t robo_css_source_scalar_count(const robo_css_source_t *source)
{
    size_t i = 0u;
    size_t count = 0u;

    if (!source || source->length == 0u) {
        return 0u;
    }
    while (i < source->length) {
        size_t length = robo_css_utf8_scalar_length(source->bytes + i,
                                                   source->length - i);

        /* An ill-formed leading byte is one position, not zero: a column count
         * that skipped it would report a column that does not exist. */
        i += (length == 0u) ? 1u : length;
        count++;
    }
    return count;
}
