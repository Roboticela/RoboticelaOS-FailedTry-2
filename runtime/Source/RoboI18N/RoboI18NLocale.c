/*
 * RoboI18N locale implementation — BCP 47 / RFC 5646 (Goal 4.1).
 * Independent of Chromium/ICU. Preferred-Value tables from IANA registry.
 */

#include "RoboI18N/RoboI18NLocale.h"

#include "RoboI18N/RoboIanaData.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

static robo_i18n_locale_t g_default_locale;
static int g_default_ready;

static int is_alpha(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int is_alnum(unsigned char c)
{
    return is_alpha(c) || is_digit(c);
}

static char to_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static char to_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static void copy_lower(char *dst, size_t dst_cap, const char *src, size_t n)
{
    size_t i;
    if (n >= dst_cap) {
        n = dst_cap - 1u;
    }
    for (i = 0; i < n; i++) {
        dst[i] = to_lower(src[i]);
    }
    dst[n] = '\0';
}

static void copy_upper(char *dst, size_t dst_cap, const char *src, size_t n)
{
    size_t i;
    if (n >= dst_cap) {
        n = dst_cap - 1u;
    }
    for (i = 0; i < n; i++) {
        dst[i] = to_upper(src[i]);
    }
    dst[n] = '\0';
}

static void copy_title(char *dst, size_t dst_cap, const char *src, size_t n)
{
    size_t i;
    if (n >= dst_cap) {
        n = dst_cap - 1u;
    }
    for (i = 0; i < n; i++) {
        dst[i] = (i == 0) ? to_upper(src[i]) : to_lower(src[i]);
    }
    dst[n] = '\0';
}

static int streq_ci(const char *a, const char *b)
{
    size_t i;
    if (!a || !b) {
        return a == b;
    }
    for (i = 0; a[i] && b[i]; i++) {
        if (to_lower(a[i]) != to_lower(b[i])) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

void robo_i18n_locale_clear(robo_i18n_locale_t *loc)
{
    if (!loc) {
        return;
    }
    memset(loc, 0, sizeof(*loc));
}

int robo_i18n_locale_has_script(const robo_i18n_locale_t *loc)
{
    return loc && loc->script[0] != '\0';
}

int robo_i18n_locale_has_region(const robo_i18n_locale_t *loc)
{
    return loc && loc->region[0] != '\0';
}

const char *robo_i18n_locale_language(const robo_i18n_locale_t *loc)
{
    return loc ? loc->language : "";
}

const char *robo_i18n_locale_script(const robo_i18n_locale_t *loc)
{
    return loc ? loc->script : "";
}

const char *robo_i18n_locale_region(const robo_i18n_locale_t *loc)
{
    return loc ? loc->region : "";
}

static int all_alpha(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (!is_alpha((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int all_digit(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (!is_digit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int all_alnum(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (!is_alnum((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static robo_result_t parse_core(const char *tag, size_t len, robo_i18n_locale_t *out);

static robo_result_t try_replace_full_tag(const char *tag,
                                          size_t len,
                                          robo_i18n_locale_t *out)
{
    char key[ROBO_I18N_TAG_MAX + 1u];
    const char *pref;
    size_t i;

    if (len == 0 || len > ROBO_I18N_TAG_MAX) {
        return ROBO_PARSE_ERROR;
    }
    for (i = 0; i < len; i++) {
        key[i] = to_lower(tag[i]);
    }
    key[len] = '\0';

    pref = robo_iana_lookup(robo_iana_grandfathered, robo_iana_grandfathered_count, key);
    if (!pref) {
        pref = robo_iana_lookup(robo_iana_redundant, robo_iana_redundant_count, key);
    }
    if (!pref) {
        return ROBO_NOT_FOUND;
    }
    return parse_core(pref, strlen(pref), out);
}

static robo_result_t parse_core(const char *tag, size_t len, robo_i18n_locale_t *out)
{
    size_t pos = 0;
    size_t start;
    size_t n;

    robo_i18n_locale_clear(out);
    if (!tag || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len == 0) {
        return ROBO_PARSE_ERROR;
    }

    /* Primary language: 2-8 ALPHA */
    start = pos;
    while (pos < len && tag[pos] != '-') {
        pos++;
    }
    n = pos - start;
    if (n < 2u || n > 8u || !all_alpha(tag + start, n)) {
        return ROBO_PARSE_ERROR;
    }
    if (n > ROBO_I18N_LANG_MAX) {
        return ROBO_PARSE_ERROR;
    }
    copy_lower(out->language, sizeof(out->language), tag + start, n);

    while (pos < len) {
        if (tag[pos] != '-') {
            return ROBO_PARSE_ERROR;
        }
        pos++;
        if (pos >= len) {
            return ROBO_PARSE_ERROR; /* empty subtag */
        }
        start = pos;
        while (pos < len && tag[pos] != '-') {
            pos++;
        }
        n = pos - start;
        if (n == 0) {
            return ROBO_PARSE_ERROR;
        }

        /* Private use: x / X singleton */
        if (n == 1 && (tag[start] == 'x' || tag[start] == 'X')) {
            size_t rem = len - start;
            if (rem > ROBO_I18N_EXT_MAX) {
                return ROBO_PARSE_ERROR;
            }
            copy_lower(out->privateuse, sizeof(out->privateuse), tag + start, rem);
            return ROBO_OK;
        }

        /* Extension singleton (other than x): one ALPHA */
        if (n == 1 && is_alpha((unsigned char)tag[start])) {
            size_t rem;
            rem = len - start;
            if (rem > ROBO_I18N_EXT_MAX) {
                return ROBO_PARSE_ERROR;
            }
            {
                size_t p = start + 1;
                if (p >= len || tag[p] != '-') {
                    return ROBO_PARSE_ERROR;
                }
                p++;
                while (p < len) {
                    size_t s = p;
                    size_t sn;
                    while (p < len && tag[p] != '-') {
                        p++;
                    }
                    sn = p - s;
                    if (sn < 2u || sn > 8u || !all_alnum(tag + s, sn)) {
                        return ROBO_PARSE_ERROR;
                    }
                    if (p < len) {
                        p++;
                    }
                }
            }
            copy_lower(out->extensions, sizeof(out->extensions), tag + start, rem);
            return ROBO_OK;
        }

        /* Extlang: 3 ALPHA after language, before script/region (up to 3) — fold into language if empty script/region */
        if (out->script[0] == '\0' && out->region[0] == '\0' && out->variant_count == 0 &&
            n == 3u && all_alpha(tag + start, n) && strlen(out->language) <= 3u) {
            /* Keep primary language; extlang replaced via Preferred-Value if registered as language later.
             * Structurally accept and store first extlang by appending is non-standard.
             * RFC: extlang is a separate subtag. For locale model we keep primary only and
             * treat extlang as requiring Preferred-Value at canonicalize for known cases.
             * Store as additional language detail: if language is 2-3 letters, replace with
             * preferred of extlang when available at canonicalize; for now accept by ignoring
             * extlang content beyond validation — actually better: set language to extlang when
             * IANA says so. Simplest structural: allow up to one extlang stored in a side path.
             * Goal 4.1: skip storing extlang; reject if we already have language and next is
             * 3-letter AND following isn't clearly script/region — wait, 3-letter region is digits.
             * 3 ALPHA after language = extlang. We'll apply language Preferred-Value for that
             * subtag alone: if preferred exists, replace language. */
            {
                char ext[4];
                const char *pref;
                copy_lower(ext, sizeof(ext), tag + start, 3);
                pref = robo_iana_lookup(robo_iana_lang_pref, robo_iana_lang_pref_count, ext);
                if (pref && strlen(pref) <= ROBO_I18N_LANG_MAX && strchr(pref, '-') == NULL) {
                    copy_lower(out->language, sizeof(out->language), pref, strlen(pref));
                }
                /* If no preferred, keep primary language (extlang unknown) — still valid. */
            }
            continue;
        }

        /* Script: 4 ALPHA */
        if (out->script[0] == '\0' && out->region[0] == '\0' && out->variant_count == 0 &&
            n == 4u && all_alpha(tag + start, n)) {
            copy_title(out->script, sizeof(out->script), tag + start, n);
            continue;
        }

        /* Region: 2 ALPHA or 3 DIGIT */
        if (out->region[0] == '\0' && out->variant_count == 0 &&
            ((n == 2u && all_alpha(tag + start, n)) || (n == 3u && all_digit(tag + start, n)))) {
            if (n == 2u) {
                copy_upper(out->region, sizeof(out->region), tag + start, n);
            } else {
                memcpy(out->region, tag + start, n);
                out->region[n] = '\0';
            }
            continue;
        }

        /* Variant: 5-8 alphanum, or DIGIT + 3 alphanum */
        if ((n >= 5u && n <= 8u && all_alnum(tag + start, n)) ||
            (n == 4u && is_digit((unsigned char)tag[start]) && all_alnum(tag + start, n))) {
            if (out->variant_count >= ROBO_I18N_VARIANT_COUNT) {
                return ROBO_PARSE_ERROR;
            }
            copy_lower(out->variants[out->variant_count],
                       sizeof(out->variants[0]),
                       tag + start,
                       n);
            out->variant_count++;
            continue;
        }

        return ROBO_PARSE_ERROR;
    }

    return ROBO_OK;
}

robo_result_t robo_i18n_locale_parse(const char *tag, size_t len, robo_i18n_locale_t *out)
{
    robo_result_t r;

    if (!tag || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = try_replace_full_tag(tag, len, out);
    if (r == ROBO_OK) {
        return ROBO_OK;
    }
    if (r != ROBO_NOT_FOUND) {
        return r;
    }
    return parse_core(tag, len, out);
}

robo_result_t robo_i18n_locale_canonicalize(robo_i18n_locale_t *loc)
{
    const char *pref;
    uint8_t i;

    if (!loc || loc->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }

    pref = robo_iana_lookup(robo_iana_lang_pref, robo_iana_lang_pref_count, loc->language);
    if (pref) {
        if (strchr(pref, '-')) {
            /* Preferred language range — re-parse */
            return robo_i18n_locale_parse(pref, strlen(pref), loc);
        }
        if (strlen(pref) <= ROBO_I18N_LANG_MAX) {
            copy_lower(loc->language, sizeof(loc->language), pref, strlen(pref));
        }
    }

    if (loc->script[0]) {
        char key[5];
        copy_lower(key, sizeof(key), loc->script, strlen(loc->script));
        pref = robo_iana_lookup(robo_iana_script_pref, robo_iana_script_pref_count, key);
        if (pref && strlen(pref) == 4u) {
            copy_title(loc->script, sizeof(loc->script), pref, 4u);
        }
    }

    if (loc->region[0]) {
        char key[4];
        if (is_alpha((unsigned char)loc->region[0])) {
            copy_upper(key, sizeof(key), loc->region, strlen(loc->region));
        } else {
            memcpy(key, loc->region, sizeof(key));
        }
        pref = robo_iana_lookup(robo_iana_region_pref, robo_iana_region_pref_count, key);
        if (pref) {
            if (is_alpha((unsigned char)pref[0])) {
                copy_upper(loc->region, sizeof(loc->region), pref, strlen(pref));
            } else {
                size_t n = strlen(pref);
                if (n <= ROBO_I18N_REGION_MAX) {
                    memcpy(loc->region, pref, n + 1u);
                }
            }
        }
    }

    for (i = 0; i < loc->variant_count; i++) {
        pref = robo_iana_lookup(
            robo_iana_variant_pref, robo_iana_variant_pref_count, loc->variants[i]);
        if (pref && strlen(pref) <= ROBO_I18N_VARIANT_MAX) {
            copy_lower(loc->variants[i], sizeof(loc->variants[i]), pref, strlen(pref));
        }
    }

    return ROBO_OK;
}

robo_result_t robo_i18n_locale_parse_canonical(const char *tag,
                                               size_t len,
                                               robo_i18n_locale_t *out)
{
    robo_result_t r = robo_i18n_locale_parse(tag, len, out);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_locale_canonicalize(out);
}

robo_result_t robo_i18n_locale_format(const robo_i18n_locale_t *loc,
                                      char *buf,
                                      size_t buf_cap,
                                      size_t *out_len)
{
    char tmp[ROBO_I18N_TAG_MAX + 1u];
    size_t n = 0;
    uint8_t i;

    if (!loc || !buf || buf_cap == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (loc->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }

    n = strlen(loc->language);
    if (n >= sizeof(tmp)) {
        return ROBO_PARSE_ERROR;
    }
    memcpy(tmp, loc->language, n);

    if (loc->script[0]) {
        tmp[n++] = '-';
        memcpy(tmp + n, loc->script, 4u);
        n += 4u;
    }
    if (loc->region[0]) {
        size_t rn = strlen(loc->region);
        tmp[n++] = '-';
        memcpy(tmp + n, loc->region, rn);
        n += rn;
    }
    for (i = 0; i < loc->variant_count; i++) {
        size_t vn = strlen(loc->variants[i]);
        if (n + 1u + vn >= sizeof(tmp)) {
            return ROBO_PARSE_ERROR;
        }
        tmp[n++] = '-';
        memcpy(tmp + n, loc->variants[i], vn);
        n += vn;
    }
    if (loc->extensions[0]) {
        size_t en = strlen(loc->extensions);
        if (n + 1u + en >= sizeof(tmp)) {
            return ROBO_PARSE_ERROR;
        }
        tmp[n++] = '-';
        memcpy(tmp + n, loc->extensions, en);
        n += en;
    }
    if (loc->privateuse[0]) {
        size_t pn = strlen(loc->privateuse);
        if (n + 1u + pn >= sizeof(tmp)) {
            return ROBO_PARSE_ERROR;
        }
        tmp[n++] = '-';
        memcpy(tmp + n, loc->privateuse, pn);
        n += pn;
    }
    tmp[n] = '\0';

    if (n + 1u > buf_cap) {
        return ROBO_INVALID_ARGUMENT;
    }
    memcpy(buf, tmp, n + 1u);
    if (out_len) {
        *out_len = n;
    }
    return ROBO_OK;
}

int robo_i18n_locale_equal(const robo_i18n_locale_t *a, const robo_i18n_locale_t *b)
{
    uint8_t i;
    if (!a || !b) {
        return 0;
    }
    if (!streq_ci(a->language, b->language)) {
        return 0;
    }
    if (!streq_ci(a->script, b->script)) {
        return 0;
    }
    if (!streq_ci(a->region, b->region)) {
        return 0;
    }
    if (a->variant_count != b->variant_count) {
        return 0;
    }
    for (i = 0; i < a->variant_count; i++) {
        if (!streq_ci(a->variants[i], b->variants[i])) {
            return 0;
        }
    }
    if (!streq_ci(a->extensions, b->extensions)) {
        return 0;
    }
    if (!streq_ci(a->privateuse, b->privateuse)) {
        return 0;
    }
    return 1;
}

robo_i18n_match_t robo_i18n_locale_matches(const robo_i18n_locale_t *range,
                                           const robo_i18n_locale_t *candidate)
{
    if (!range || !candidate) {
        return ROBO_I18N_MATCH_NONE;
    }
    if (!streq_ci(range->language, candidate->language)) {
        return ROBO_I18N_MATCH_NONE;
    }
    if (range->script[0] && !streq_ci(range->script, candidate->script)) {
        return ROBO_I18N_MATCH_NONE;
    }
    if (range->region[0] && !streq_ci(range->region, candidate->region)) {
        return ROBO_I18N_MATCH_NONE;
    }
    if (robo_i18n_locale_equal(range, candidate)) {
        return ROBO_I18N_MATCH_EXACT;
    }
    if (range->region[0]) {
        return ROBO_I18N_MATCH_REGION;
    }
    if (range->script[0]) {
        return ROBO_I18N_MATCH_SCRIPT;
    }
    return ROBO_I18N_MATCH_LANGUAGE;
}

robo_result_t robo_i18n_locale_parent(const robo_i18n_locale_t *loc, robo_i18n_locale_t *out)
{
    if (!loc || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = *loc;

    if (out->privateuse[0]) {
        out->privateuse[0] = '\0';
        return ROBO_OK;
    }
    if (out->extensions[0]) {
        out->extensions[0] = '\0';
        return ROBO_OK;
    }
    if (out->variant_count > 0) {
        out->variant_count--;
        out->variants[out->variant_count][0] = '\0';
        return ROBO_OK;
    }
    if (out->region[0]) {
        out->region[0] = '\0';
        return ROBO_OK;
    }
    if (out->script[0]) {
        out->script[0] = '\0';
        return ROBO_OK;
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_i18n_set_default_locale(const robo_i18n_locale_t *loc)
{
    if (!loc || loc->language[0] == '\0') {
        return ROBO_INVALID_ARGUMENT;
    }
    g_default_locale = *loc;
    g_default_ready = 1;
    return ROBO_OK;
}

robo_result_t robo_i18n_get_default_locale(robo_i18n_locale_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_default_ready) {
        /* Deterministic default: undefined language (not en-US). */
        robo_i18n_locale_clear(out);
        memcpy(out->language, "und", 4);
        return ROBO_OK;
    }
    *out = g_default_locale;
    return ROBO_OK;
}

/* --- locale lists --- */

struct robo_i18n_locale_list {
    robo_i18n_locale_t *items;
    float *weights;
    size_t count;
    size_t capacity;
};

void robo_i18n_locale_list_destroy(robo_i18n_locale_list_t *list)
{
    if (!list) {
        return;
    }
    robo_free(list->items);
    robo_free(list->weights);
    robo_free(list);
}

size_t robo_i18n_locale_list_size(const robo_i18n_locale_list_t *list)
{
    return list ? list->count : 0u;
}

robo_result_t robo_i18n_locale_list_get(const robo_i18n_locale_list_t *list,
                                        size_t index,
                                        robo_i18n_locale_t *out)
{
    if (!list || !out || index >= list->count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = list->items[index];
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_list_get_q_milli(const robo_i18n_locale_list_t *list,
                                                size_t index,
                                                uint16_t *out_q_milli)
{
    float w;
    unsigned m;
    if (!list || !out_q_milli || index >= list->count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!list->weights) {
        *out_q_milli = 1000u;
        return ROBO_OK;
    }
    w = list->weights[index];
    if (w <= 0.0f) {
        *out_q_milli = 0u;
        return ROBO_OK;
    }
    if (w >= 1.0f) {
        *out_q_milli = 1000u;
        return ROBO_OK;
    }
    m = (unsigned)(w * 1000.0f + 0.5f);
    if (m > 1000u) {
        m = 1000u;
    }
    *out_q_milli = (uint16_t)m;
    return ROBO_OK;
}

static float parse_q(const char *s, size_t len)
{
    /* Parse q-value in [0,1] without libc float conversion. */
    size_t i = 0;
    float v = 0.0f;
    float scale = 0.1f;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) {
        i++;
    }
    if (i >= len) {
        return 1.0f;
    }
    if (s[i] == '1') {
        return 1.0f;
    }
    if (s[i] == '0') {
        i++;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && is_digit((unsigned char)s[i])) {
                v += (float)(s[i] - '0') * scale;
                scale *= 0.1f;
                i++;
            }
        }
        if (v > 1.0f) {
            v = 1.0f;
        }
        return v;
    }
    return 1.0f;
}
robo_result_t robo_i18n_locale_list_parse(const char *text,
                                          size_t len,
                                          robo_i18n_locale_list_t **out)
{
    robo_i18n_locale_list_t *list;
    size_t i = 0;
    size_t *order = NULL;
    robo_i18n_locale_t *sorted = NULL;
    float *sorted_w = NULL;

    if (!text || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    list = (robo_i18n_locale_list_t *)robo_calloc(1, sizeof(*list));
    if (!list) {
        return ROBO_OUT_OF_MEMORY;
    }

    while (i < len) {
        size_t start;
        size_t end;
        size_t tag_end;
        float q = 1.0f;
        robo_i18n_locale_t loc;
        robo_result_t r;

        while (i < len && (text[i] == ' ' || text[i] == '\t')) {
            i++;
        }
        if (i >= len) {
            break;
        }
        start = i;
        while (i < len && text[i] != ',') {
            i++;
        }
        end = i;
        if (i < len && text[i] == ',') {
            i++;
        }
        while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
            end--;
        }
        if (end <= start) {
            continue;
        }

        tag_end = start;
        while (tag_end < end && text[tag_end] != ';') {
            tag_end++;
        }
        {
            size_t te = tag_end;
            while (te > start && (text[te - 1] == ' ' || text[te - 1] == '\t')) {
                te--;
            }
            tag_end = te;
        }
        if (tag_end > start && tag_end < end && text[tag_end] != ';') {
            /* trimmed */
        }
        if (tag_end < end && text[tag_end] == ';') {
            /* parse q= */
            size_t p = tag_end + 1;
            while (p < end && (text[p] == ' ' || text[p] == '\t')) {
                p++;
            }
            if (p + 2 < end && (text[p] == 'q' || text[p] == 'Q') && text[p + 1] == '=') {
                q = parse_q(text + p + 2, end - (p + 2));
            }
        } else {
            /* no semicolon — tag_end may equal end */
        }

        /* Fix tag length when semicolon present */
        {
            size_t tlen;
            size_t te = start;
            while (te < end && text[te] != ';') {
                te++;
            }
            while (te > start && (text[te - 1] == ' ' || text[te - 1] == '\t')) {
                te--;
            }
            tlen = te - start;
            if (tlen == 0) {
                continue;
            }
            r = robo_i18n_locale_parse_canonical(text + start, tlen, &loc);
            if (r != ROBO_OK) {
                robo_i18n_locale_list_destroy(list);
                return r;
            }
        }

        if (list->count == list->capacity) {
            size_t nc = list->capacity ? list->capacity * 2u : 4u;
            void *ni = robo_realloc(list->items, nc * sizeof(robo_i18n_locale_t));
            void *nw = robo_realloc(list->weights, nc * sizeof(float));
            if (!ni || !nw) {
                robo_free(ni);
                robo_free(nw);
                robo_i18n_locale_list_destroy(list);
                return ROBO_OUT_OF_MEMORY;
            }
            list->items = (robo_i18n_locale_t *)ni;
            list->weights = (float *)nw;
            list->capacity = nc;
        }
        list->items[list->count] = loc;
        list->weights[list->count] = q;
        list->count++;
    }

    if (list->count > 1u) {
        order = (size_t *)robo_malloc(list->count * sizeof(size_t));
        sorted = (robo_i18n_locale_t *)robo_malloc(list->count * sizeof(robo_i18n_locale_t));
        sorted_w = (float *)robo_malloc(list->count * sizeof(float));
        if (!order || !sorted || !sorted_w) {
            robo_free(order);
            robo_free(sorted);
            robo_free(sorted_w);
            robo_i18n_locale_list_destroy(list);
            return ROBO_OUT_OF_MEMORY;
        }
        for (i = 0; i < list->count; i++) {
            order[i] = i;
        }
        {
            size_t a, b;
            for (a = 1; a < list->count; a++) {
                size_t key = order[a];
                b = a;
                while (b > 0 && list->weights[order[b - 1]] < list->weights[key]) {
                    order[b] = order[b - 1];
                    b--;
                }
                order[b] = key;
            }
        }
        for (i = 0; i < list->count; i++) {
            sorted[i] = list->items[order[i]];
            sorted_w[i] = list->weights[order[i]];
        }
        robo_free(list->items);
        robo_free(list->weights);
        robo_free(order);
        list->items = sorted;
        list->weights = sorted_w;
    }

    *out = list;
    return ROBO_OK;
}
