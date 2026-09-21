/*
 * Character-reference engine (Phase 8.2).
 *
 * Named references: longest-match scan over the static catalogue
 * (RoboHTMLCharRefData.c; the table is a curated, category-grouped set, so
 * the match is chosen by name length rather than table position).
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
static const uint16_t gC1Remap[32] = {
    0x20ACu, 0x81u, 0x201Au, 0x0192u, 0x201Eu, 0x2026u, 0x2020u, 0x2021u,
    0x02C6u, 0x2030u, 0x0160u, 0x2039u, 0x0152u, 0x8Du, 0x017Du, 0x8Fu,
    0x90u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2013u, 0x2014u,
    0x02DCu, 0x2122u, 0x0161u, 0x203Au, 0x0153u, 0x9Du, 0x017Eu, 0x0178u
};

uint32_t HtmlCharrefMapValue(uint32_t value,
                                     HtmlCharrefError *err)
{
    if (value == 0u || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu)) {
        if (err && *err == RoboHtmlCharrefOk) {
            *err = RoboHtmlCharrefOutOfRange;
        }
        return 0xFFFDu;
    }
    if (value >= 0x80u && value <= 0x9Fu) {
        if (err && *err == RoboHtmlCharrefOk) {
            *err = RoboHtmlCharrefOutOfRange; /* legacy remap, still odd */
        }
        return gC1Remap[value - 0x80u];
    }
    /* Control-character reference: a parse error, but the standard keeps
     * the referenced code point (ASCII whitespace and 0x0D included). */
    if ((value < 0x20u && value != 0x09u && value != 0x0Au && value != 0x0Cu &&
         value != 0x0Du) ||
        value == 0x7Fu) {
        if (err && *err == RoboHtmlCharrefOk) {
            *err = RoboHtmlCharrefOutOfRange;
        }
        return value;
    }
    return value;
}

static size_t AppendCp(char *out, size_t cap, size_t used, uint32_t cp)
{
    size_t need = UnicodeUtf8EncodeLen(cp);
    size_t written = 0u;

    if (need == 0u || used + need > cap) {
        return used;
    }
    if (UnicodeUtf8Encode(cp, out + used, cap - used, &written) ==
        RoboUnicodeOk) {
        return used + written;
    }
    return used;
}

static int NameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static int HexDigit(char c, uint32_t *out)
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

static int TableNameMatches(const HtmlCharrefEntry *e,
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

int HtmlCharrefExpand(const char *bytes,
                             size_t length,
                             size_t *offset,
                             int inAttribute,
                             char *outUtf8,
                             size_t *outLen,
                             HtmlCharrefError *outError)
{
    size_t pos;
    HtmlCharrefError err = RoboHtmlCharrefOk;
    size_t used = 0u;

    if (!bytes || !offset || !outUtf8 || !outLen || *offset >= length ||
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
                if (!HexDigit(bytes[pos], &d)) {
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
            err = RoboHtmlCharrefOutOfRange;
            value = 0xFFFDu;
        }
        if (pos < length && bytes[pos] == ';') {
            pos++;
        } else {
            if (err == RoboHtmlCharrefOk) {
                err = RoboHtmlCharrefMissingSemicolon;
            }
        }
        if (err != RoboHtmlCharrefOutOfRange || !overflow) {
            value = HtmlCharrefMapValue(value, &err);
        }
        used = AppendCp(outUtf8, RoboHtmlCharrefMaxBytes, used, value);
        if (used == 0u) {
            return 0;
        }
        *offset = pos;
        *outLen = used;
        if (outError) {
            *outError = err;
        }
        return 1;
    }

    if (NameChar(bytes[pos])) {
        /* --- named -------------------------------------------------------- */
        const HtmlCharrefEntry *best = NULL;
        size_t bestLen = 0u;
        size_t i;

        /*
         * Longest-match rule over the whole catalogue. The table is a
         * curated set grouped by category, so its order carries no meaning
         * and the winner is selected by name length: "&amp;" must beat
         * "&amp" wherever either sits. The first-byte comparison is a
         * cheap necessary condition for a prefix match.
         */
        for (i = 0u; i < HtmlCharrefTableSize; i++) {
            const HtmlCharrefEntry *e = &HtmlCharrefTable[i];
            size_t nlen;

            if (e->name[0] != bytes[pos]) {
                continue;
            }
            if (!TableNameMatches(e, bytes, length, pos)) {
                continue;
            }
            nlen = 0u;
            while (e->name[nlen] != '\0') {
                nlen++;
            }
            if (nlen > bestLen) {
                best = e;
                bestLen = nlen;
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
            if (inAttribute && best->name[nlen - 1u] != ';') {
                char next = (pos < length) ? bytes[pos] : '\0';
                if (next == '=' || NameChar(next)) {
                    return 0;
                }
            }
            if (best->name[nlen - 1u] != ';') {
                if (err == RoboHtmlCharrefOk) {
                    err = RoboHtmlCharrefMissingSemicolon;
                }
            }
            used = AppendCp(outUtf8, RoboHtmlCharrefMaxBytes, used,
                             best->cp1);
            if (best->cp2 != 0u) {
                used = AppendCp(outUtf8, RoboHtmlCharrefMaxBytes, used,
                                 best->cp2);
            }
            if (used == 0u) {
                return 0;
            }
            *offset = pos;
            *outLen = used;
            if (outError) {
                *outError = err;
            }
            return 1;
        }
    }

    return 0; /* &<something-else>: literal */
}
