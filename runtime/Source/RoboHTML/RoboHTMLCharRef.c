/*
 * Character-reference engine (Phase 8.2).
 *
 * Named references: longest-prefix match over the static sorted table
 * (one linear pass finds both the exact and the longest prefix match).
 * Semicolonless named forms are accepted when they are table entries
 * (the standard's legacy list); inside attribute values a semicolonless
 * match is rejected when the next character is '=' or alphanumeric — the
 * HTML legacy rule that keeps &ampx=1 literal.
 *
 * Numeric references: &#DDD; and &#xHHH; (case-insensitive x), with the
 * HTML value mapping: out-of-range → U+FFFD, surrogates → U+FFFD,
 * C1 controls remap per the standard table, NUL → U+FFFD, noncharacters
 * preserved. Missing ';' is tolerated with a diagnostic.
 */

#include "RoboHTMLCharRef.h"
#include "RoboHTMLCharRefData.h"

#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>

/* C1 remap table per the HTML numeric reference rules (0x80..0x9F). */
static const uint16_t g_c1_remap[32] = {
    0x20ACu, 0x81u, 0x201Au, 0x0192u, 0x201Eu, 0x2026u, 0x2020u, 0x2021u,
    0x02C6u, 0x2030u, 0x0160u, 0x2039u, 0x0152u, 0x8Du, 0x017Du, 0x8Fu,
    0x90u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2013u, 0x2014u,
    0x02DCu, 0x2122u, 0x0161u, 0x203Au, 0x0153u, 0x9Du, 0x017Eu, 0x0178u
};

uint32_t robo_html_charref_map_value(uint32_t value,
                                     robo_html_charref_error_t *err)
{
    if (value == 0u || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu)) {
        if (err && *err == ROBO_HTML_CHARREF_OK) {
            *err = ROBO_HTML_CHARREF_OUT_OF_RANGE;
        }
        return 0xFFFDu;
    }
    if (value >= 0x80u && value <= 0x9Fu) {
        if (err && *err == ROBO_HTML_CHARREF_OK) {
            *err = ROBO_HTML_CHARREF_OUT_OF_RANGE; /* legacy remap, still odd */
        }
        return g_c1_remap[value - 0x80u];
    }
    return value;
}

static size_t append_cp(char *out, size_t cap, size_t used, uint32_t cp)
{
    size_t need = robo_unicode_utf8_encode_len(cp);
    size_t written = 0u;

    if (need == 0u || used + need > cap) {
        return used;
    }
    if (robo_unicode_utf8_encode(cp, out + used, cap - used, &written) ==
        ROBO_UNICODE_OK) {
        return used + written;
    }
    return used;
}

static int name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static int hex_digit(char c, uint32_t *out)
{
    if (c >= '0' && c <= '9') {
        *out = (uint32_t)(c - '0');
        return 1;
    }
    if (c >= 'a' && c <= 'f') {
        *out = (uint32_t)(c - 'a' + 10);
        return 1;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (uint32_t)(c - 'A' + 10);
        return 1;
    }
    return 0;
}

static int table_name_matches(const robo_html_charref_entry_t *e,
                              const char *bytes,
                              size_t length,
                              size_t offset)
{
    size_t i;

    for (i = 0u; e->name[i] != '\0'; i++) {
        if (offset + i >= length || bytes[offset + i] != e->name[i]) {
            return 0;
        }
    }
    return 1;
}

int robo_html_charref_expand(const char *bytes,
                             size_t length,
                             size_t *offset,
                             int in_attribute,
                             char *out_utf8,
                             size_t *out_len,
                             robo_html_charref_error_t *out_error)
{
    size_t pos;
    robo_html_charref_error_t err = ROBO_HTML_CHARREF_OK;
    size_t used = 0u;

    if (!bytes || !offset || !out_utf8 || !out_len || *offset >= length ||
        bytes[*offset] != '&') {
        return 0;
    }
    pos = *offset + 1u;
    if (pos >= length) {
        return 0; /* bare '&' at EOF: literal */
    }

    if (bytes[pos] == '#') {
        /* --- numeric ----------------------------------------------------- */
        uint32_t value = 0u;
        int digits = 0;
        int overflow = 0;

        pos++;
        if (pos < length && (bytes[pos] == 'x' || bytes[pos] == 'X')) {
            pos++;
            while (pos < length) {
                uint32_t d;
                if (!hex_digit(bytes[pos], &d)) {
                    break;
                }
                if (value > 0x10FFFFu) {
                    overflow = 1;
                } else {
                    uint32_t next = value * 16u + d;
                    if (next < value) {
                        overflow = 1;
                    }
                    value = next;
                }
                digits++;
                pos++;
            }
        } else {
            while (pos < length && bytes[pos] >= '0' && bytes[pos] <= '9') {
                uint32_t d = (uint32_t)(bytes[pos] - '0');
                if (value > 0x10FFFFu) {
                    overflow = 1;
                } else {
                    uint32_t next = value * 10u + d;
                    if (next < value) {
                        overflow = 1;
                    }
                    value = next;
                }
                digits++;
                pos++;
            }
        }
        if (digits == 0 || overflow) {
            /* No digits: '&' '#' is literal text; rewind to '&'. */
            if (digits == 0) {
                return 0;
            }
            err = ROBO_HTML_CHARREF_OUT_OF_RANGE;
            value = 0xFFFDu;
        }
        if (pos < length && bytes[pos] == ';') {
            pos++;
        } else {
            if (err == ROBO_HTML_CHARREF_OK) {
                err = ROBO_HTML_CHARREF_MISSING_SEMICOLON;
            }
        }
        if (err != ROBO_HTML_CHARREF_OUT_OF_RANGE || !overflow) {
            value = robo_html_charref_map_value(value, &err);
        }
        used = append_cp(out_utf8, ROBO_HTML_CHARREF_MAX_BYTES, used, value);
        if (used == 0u) {
            return 0;
        }
        *offset = pos;
        *out_len = used;
        if (out_error) {
            *out_error = err;
        }
        return 1;
    }

    if (name_char(bytes[pos])) {
        /* --- named -------------------------------------------------------- */
        const robo_html_charref_entry_t *best = NULL;
        size_t i;

        for (i = 0u; i < robo_html_charref_table_size; i++) {
            const robo_html_charref_entry_t *e = &robo_html_charref_table[i];
            if (e->name[0] > bytes[pos]) {
                break; /* sorted table: nothing later can prefix-match */
            }
            if (table_name_matches(e, bytes, length, pos)) {
                best = e; /* keep the longest (later entries extend prefixes) */
            }
        }
        if (!best) {
            return 0; /* literal '&'; caller rescans from offset+1 */
        }
        {
            size_t nlen = 0u;
            while (best->name[nlen] != '\0') {
                nlen++;
            }
            pos += nlen;
            /* Legacy rule inside attributes: a semicolonless match followed
             * by '=' or an alphanumeric is treated as literal text. */
            if (in_attribute && best->name[nlen - 1u] != ';') {
                char next = (pos < length) ? bytes[pos] : '\0';
                if (next == '=' || name_char(next)) {
                    return 0;
                }
            }
            if (best->name[nlen - 1u] != ';') {
                if (err == ROBO_HTML_CHARREF_OK) {
                    err = ROBO_HTML_CHARREF_MISSING_SEMICOLON;
                }
            }
            used = append_cp(out_utf8, ROBO_HTML_CHARREF_MAX_BYTES, used,
                             best->cp1);
            if (best->cp2 != 0u) {
                used = append_cp(out_utf8, ROBO_HTML_CHARREF_MAX_BYTES, used,
                                 best->cp2);
            }
            if (used == 0u) {
                return 0;
            }
            *offset = pos;
            *out_len = used;
            if (out_error) {
                *out_error = err;
            }
            return 1;
        }
    }

    return 0; /* &<something-else>: literal */
}
