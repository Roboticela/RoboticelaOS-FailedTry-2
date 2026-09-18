/*
 * RoboCSS fundamental types (Phase 9.1) — see RoboCSSTypes.h.
 */

#include "RoboCSS/RoboCSSTypes.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Slices                                                              */
/* ------------------------------------------------------------------ */

robo_css_slice_t robo_css_slice_empty(void)
{
    robo_css_slice_t slice;

    slice.bytes = NULL;
    slice.length = 0u;
    return slice;
}

robo_css_slice_t robo_css_slice_from_bytes(const uint8_t *bytes, size_t length)
{
    robo_css_slice_t slice;

    slice.bytes = bytes;
    slice.length = length;
    return slice;
}

robo_css_slice_t robo_css_slice_from_text(const char *utf8)
{
    robo_css_slice_t slice;

    if (!utf8) {
        return robo_css_slice_empty();
    }
    slice.bytes = (const uint8_t *)utf8;
    slice.length = strlen(utf8);
    return slice;
}

int robo_css_slice_is_empty(robo_css_slice_t slice)
{
    return !slice.bytes || slice.length == 0u;
}

int robo_css_slice_is_absent(robo_css_slice_t slice)
{
    return slice.bytes == NULL;
}

int robo_css_slice_equals(robo_css_slice_t a, robo_css_slice_t b)
{
    if (a.length != b.length) {
        return 0;
    }
    if (a.length == 0u) {
        return 1;
    }
    if (!a.bytes || !b.bytes) {
        return 0;
    }
    return memcmp(a.bytes, b.bytes, a.length) == 0;
}

int robo_css_slice_equals_ci(robo_css_slice_t a, robo_css_slice_t b)
{
    size_t i;

    if (a.length != b.length) {
        return 0;
    }
    if (a.length == 0u) {
        return 1;
    }
    if (!a.bytes || !b.bytes) {
        return 0;
    }
    for (i = 0u; i < a.length; i++) {
        if (robo_css_ascii_lower(a.bytes[i]) != robo_css_ascii_lower(b.bytes[i])) {
            return 0;
        }
    }
    return 1;
}

int robo_css_slice_equals_ascii(robo_css_slice_t slice, const char *ascii)
{
    return robo_css_bytes_equal_ascii(slice.bytes, slice.length, ascii);
}

int robo_css_slice_equals_ascii_ci(robo_css_slice_t slice, const char *ascii)
{
    return robo_css_bytes_equal_ascii_ci(slice.bytes, slice.length, ascii);
}

int robo_css_slice_starts_with_ascii(robo_css_slice_t slice, const char *prefix)
{
    size_t prefix_length;

    if (!prefix) {
        return 0;
    }
    prefix_length = strlen(prefix);
    if (prefix_length > slice.length) {
        return 0;
    }
    if (prefix_length == 0u) {
        return 1;
    }
    if (!slice.bytes) {
        return 0;
    }
    return memcmp(slice.bytes, prefix, prefix_length) == 0;
}

robo_result_t robo_css_slice_copy_bytes(robo_css_slice_t slice,
                                        uint8_t *out,
                                        size_t out_capacity)
{
    if (!out || out_capacity < slice.length) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (slice.length == 0u) {
        return ROBO_OK;
    }
    if (!slice.bytes) {
        return ROBO_INVALID_ARGUMENT;
    }
    memcpy(out, slice.bytes, slice.length);
    return ROBO_OK;
}

robo_result_t robo_css_slice_copy_string(robo_css_slice_t slice,
                                         char *out,
                                         size_t out_capacity)
{
    if (!out || out_capacity < slice.length + 1u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (slice.length > 0u) {
        if (!slice.bytes) {
            return ROBO_INVALID_ARGUMENT;
        }
        memcpy(out, slice.bytes, slice.length);
    }
    out[slice.length] = '\0';
    return ROBO_OK;
}

size_t robo_css_split_ascii_whitespace(robo_css_slice_t text,
                                       robo_css_slice_t *out_fields,
                                       size_t capacity,
                                       int *out_truncated)
{
    size_t written = 0u;
    size_t i = 0u;
    int truncated = 0;

    if (out_truncated) {
        *out_truncated = 0;
    }
    if (!text.bytes || text.length == 0u) {
        return 0u;
    }
    while (i < text.length) {
        size_t start;

        while (i < text.length && robo_css_ascii_is_space(text.bytes[i])) {
            i++;
        }
        if (i >= text.length) {
            break;
        }
        start = i;
        while (i < text.length && !robo_css_ascii_is_space(text.bytes[i])) {
            i++;
        }
        if (written < capacity && out_fields) {
            out_fields[written].bytes = text.bytes + start;
            out_fields[written].length = i - start;
            written++;
        } else {
            truncated = 1;
        }
    }
    if (out_truncated) {
        *out_truncated = truncated;
    }
    return written;
}

/* ------------------------------------------------------------------ */
/* Positions and ranges                                                */
/* ------------------------------------------------------------------ */

robo_css_position_t robo_css_position_none(void)
{
    robo_css_position_t position;

    position.offset = 0u;
    position.line = 0u;
    position.column = 0u;
    return position;
}

robo_css_position_t robo_css_position_at(size_t offset)
{
    robo_css_position_t position;

    position.offset = offset;
    position.line = 0u;
    position.column = 0u;
    return position;
}

int robo_css_position_has_line(const robo_css_position_t *position)
{
    return position && position->line > 0u;
}

robo_css_range_t robo_css_range_at(size_t offset)
{
    robo_css_range_t range;

    range.begin = offset;
    range.end = offset;
    return range;
}

robo_css_range_t robo_css_range_span(size_t begin, size_t length)
{
    robo_css_range_t range;

    range.begin = begin;
    if (length > (SIZE_MAX - begin)) {
        range.end = SIZE_MAX;
    } else {
        range.end = begin + length;
    }
    return range;
}

int robo_css_range_is_empty(robo_css_range_t range)
{
    return range.begin >= range.end;
}

size_t robo_css_range_length(robo_css_range_t range)
{
    if (range.end <= range.begin) {
        return 0u;
    }
    return range.end - range.begin;
}

int robo_css_range_contains(robo_css_range_t range, size_t offset)
{
    return offset >= range.begin && offset < range.end;
}

/* ------------------------------------------------------------------ */
/* Origin                                                              */
/* ------------------------------------------------------------------ */

int robo_css_origin_is_known(robo_css_origin_t origin)
{
    switch (origin) {
    case ROBO_CSS_ORIGIN_NONE:
    case ROBO_CSS_ORIGIN_RUNTIME:
    case ROBO_CSS_ORIGIN_USER:
    case ROBO_CSS_ORIGIN_AUTHOR:
    case ROBO_CSS_ORIGIN_INLINE:
        return 1;
    default:
        return 0;
    }
}

const char *robo_css_origin_name(robo_css_origin_t origin)
{
    switch (origin) {
    case ROBO_CSS_ORIGIN_NONE:
        return "none";
    case ROBO_CSS_ORIGIN_RUNTIME:
        return "runtime";
    case ROBO_CSS_ORIGIN_USER:
        return "user";
    case ROBO_CSS_ORIGIN_AUTHOR:
        return "author";
    case ROBO_CSS_ORIGIN_INLINE:
        return "inline";
    default:
        return "unknown";
    }
}

int robo_css_origin_is_stylesheet_origin(robo_css_origin_t origin)
{
    return origin == ROBO_CSS_ORIGIN_RUNTIME || origin == ROBO_CSS_ORIGIN_USER ||
           origin == ROBO_CSS_ORIGIN_AUTHOR;
}

int robo_css_origin_is_inline(robo_css_origin_t origin)
{
    return origin == ROBO_CSS_ORIGIN_INLINE;
}

/* ------------------------------------------------------------------ */
/* Stylesheet state                                                    */
/* ------------------------------------------------------------------ */

int robo_css_stylesheet_state_is_known(robo_css_stylesheet_state_t state)
{
    switch (state) {
    case ROBO_CSS_STYLESHEET_STATE_NONE:
    case ROBO_CSS_STYLESHEET_STATE_CREATED:
    case ROBO_CSS_STYLESHEET_STATE_PARSED:
    case ROBO_CSS_STYLESHEET_STATE_FAILED:
        return 1;
    default:
        return 0;
    }
}

const char *robo_css_stylesheet_state_name(robo_css_stylesheet_state_t state)
{
    switch (state) {
    case ROBO_CSS_STYLESHEET_STATE_NONE:
        return "none";
    case ROBO_CSS_STYLESHEET_STATE_CREATED:
        return "created";
    case ROBO_CSS_STYLESHEET_STATE_PARSED:
        return "parsed";
    case ROBO_CSS_STYLESHEET_STATE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

int robo_css_stylesheet_state_is_settled(robo_css_stylesheet_state_t state)
{
    return state == ROBO_CSS_STYLESHEET_STATE_PARSED ||
           state == ROBO_CSS_STYLESHEET_STATE_FAILED;
}

/* ------------------------------------------------------------------ */
/* Diagnostic domains                                                  */
/* ------------------------------------------------------------------ */

int robo_css_diagnostic_domain_is_known(robo_css_diagnostic_domain_t domain)
{
    return domain >= ROBO_CSS_DOMAIN_NONE && domain < (robo_css_diagnostic_domain_t)ROBO_CSS_DOMAIN_COUNT;
}

const char *robo_css_diagnostic_domain_name(robo_css_diagnostic_domain_t domain)
{
    switch (domain) {
    case ROBO_CSS_DOMAIN_NONE:
        return "none";
    case ROBO_CSS_DOMAIN_MODULE:
        return "module";
    case ROBO_CSS_DOMAIN_SOURCE:
        return "source";
    case ROBO_CSS_DOMAIN_TOKEN:
        return "token";
    case ROBO_CSS_DOMAIN_VALUE:
        return "value";
    case ROBO_CSS_DOMAIN_PROPERTY:
        return "property";
    case ROBO_CSS_DOMAIN_DECLARATION:
        return "declaration";
    case ROBO_CSS_DOMAIN_SELECTOR:
        return "selector";
    case ROBO_CSS_DOMAIN_RULE:
        return "rule";
    case ROBO_CSS_DOMAIN_STYLESHEET:
        return "stylesheet";
    case ROBO_CSS_DOMAIN_PARSER:
        return "parser";
    case ROBO_CSS_DOMAIN_STYLE:
        return "style";
    case ROBO_CSS_DOMAIN_COMPUTED:
        return "computed";
    case ROBO_CSS_DOMAIN_PROPERTY_REGISTRY:
        return "property registry";
    case ROBO_CSS_DOMAIN_INHERIT:
        return "inherit";
    case ROBO_CSS_DOMAIN_BOX:
        return "box";
    default:
        return "unknown";
    }
}
