/*
 * RoboI18N locale negotiation & Accept-Language (Goal 4.17).
 */

#include "RoboI18N/RoboI18NLocaleNegotiate.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_i18n_preference_list {
    robo_i18n_language_range_t *items;
    size_t count;
    size_t capacity;
};

void robo_i18n_language_range_clear(robo_i18n_language_range_t *r)
{
    if (r) {
        memset(r, 0, sizeof(*r));
        r->q_milli = ROBO_I18N_Q_MILLI_DEFAULT;
    }
}

void robo_i18n_negotiate_result_clear(robo_i18n_negotiate_result_t *r)
{
    if (r) {
        memset(r, 0, sizeof(*r));
    }
}

robo_i18n_match_t robo_i18n_negotiate_kind_to_match(robo_i18n_negotiate_kind_t k)
{
    switch (k) {
    case ROBO_I18N_NEG_EXACT:
        return ROBO_I18N_MATCH_EXACT;
    case ROBO_I18N_NEG_REGION:
        return ROBO_I18N_MATCH_REGION;
    case ROBO_I18N_NEG_SCRIPT:
        return ROBO_I18N_MATCH_SCRIPT;
    case ROBO_I18N_NEG_LANGUAGE:
    case ROBO_I18N_NEG_WILDCARD:
        return ROBO_I18N_MATCH_LANGUAGE;
    default:
        return ROBO_I18N_MATCH_NONE;
    }
}

static int is_ascii_ws(unsigned char c)
{
    return c == ' ' || c == '\t';
}

static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static int utf8_ok_asciiish(const char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0) {
            return 0;
        }
        /* Allow printable ASCII + HTAB; reject other controls and high bytes
         * (language tags are ASCII). */
        if (c == '\t' || (c >= 0x20 && c <= 0x7E)) {
            continue;
        }
        return 0;
    }
    return 1;
}

/**
 * Parse q= value into millipercent. Accepts 0 / 1 / 0.xxx (≤3 digits).
 * Returns 0 on success; -1 on malformed / out of range.
 */
static int parse_q_milli(const char *s, size_t len, robo_i18n_q_milli_t *out)
{
    size_t i = 0;
    while (i < len && is_ascii_ws((unsigned char)s[i])) {
        i++;
    }
    while (len > i && is_ascii_ws((unsigned char)s[len - 1])) {
        len--;
    }
    if (i >= len) {
        return -1;
    }
    if (s[i] == '1') {
        i++;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && s[i] == '0') {
                i++;
            }
        }
        while (i < len && is_ascii_ws((unsigned char)s[i])) {
            i++;
        }
        if (i != len) {
            return -1;
        }
        *out = ROBO_I18N_Q_MILLI_MAX;
        return 0;
    }
    if (s[i] == '0') {
        unsigned v = 0;
        unsigned digits = 0;
        i++;
        if (i < len && s[i] == '.') {
            i++;
            while (i < len && is_digit((unsigned char)s[i])) {
                if (digits >= 3u) {
                    return -1; /* >3 fractional digits */
                }
                v = v * 10u + (unsigned)(s[i] - '0');
                digits++;
                i++;
            }
            while (digits < 3u) {
                v *= 10u;
                digits++;
            }
        }
        while (i < len && is_ascii_ws((unsigned char)s[i])) {
            i++;
        }
        if (i != len) {
            return -1;
        }
        if (v > ROBO_I18N_Q_MILLI_MAX) {
            return -1;
        }
        *out = (robo_i18n_q_milli_t)v;
        return 0;
    }
    return -1;
}

robo_result_t robo_i18n_language_range_parse(const char *text,
                                             size_t len,
                                             robo_i18n_language_range_t *out)
{
    size_t tag_end;
    size_t p;
    robo_i18n_q_milli_t q = ROBO_I18N_Q_MILLI_DEFAULT;
    int q_explicit = 0;
    size_t i;

    if (!text || !out || len == 0 || len > ROBO_I18N_RANGE_MAX_BYTES) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!utf8_ok_asciiish(text, len)) {
        return ROBO_PARSE_ERROR;
    }

    /* trim */
    i = 0;
    while (i < len && is_ascii_ws((unsigned char)text[i])) {
        i++;
    }
    while (len > i && is_ascii_ws((unsigned char)text[len - 1])) {
        len--;
    }
    if (i >= len) {
        return ROBO_PARSE_ERROR;
    }
    text += i;
    len -= i;

    tag_end = 0;
    while (tag_end < len && text[tag_end] != ';') {
        tag_end++;
    }
    {
        size_t te = tag_end;
        while (te > 0 && is_ascii_ws((unsigned char)text[te - 1])) {
            te--;
        }
        tag_end = te;
    }
    if (tag_end == 0) {
        return ROBO_PARSE_ERROR;
    }

    if (tag_end < len && text[tag_end] == ';') {
        p = tag_end + 1;
        while (p < len && is_ascii_ws((unsigned char)text[p])) {
            p++;
        }
        if (p + 2 > len || (text[p] != 'q' && text[p] != 'Q') || text[p + 1] != '=') {
            return ROBO_PARSE_ERROR;
        }
        if (parse_q_milli(text + p + 2, len - (p + 2), &q) != 0) {
            return ROBO_PARSE_ERROR;
        }
        q_explicit = 1;
    } else if (tag_end != len) {
        return ROBO_PARSE_ERROR;
    }

    robo_i18n_language_range_clear(out);
    out->q_milli = q;
    out->q_explicit = (uint8_t)q_explicit;

    if (tag_end == 1 && text[0] == '*') {
        out->is_wildcard = 1;
        return ROBO_OK;
    }

    /* Reject extended range forms like *-CH or en-* */
    {
        size_t j;
        for (j = 0; j < tag_end; j++) {
            if (text[j] == '*') {
                return ROBO_PARSE_ERROR;
            }
        }
    }

    if (robo_i18n_locale_parse_canonical(text, tag_end, &out->locale) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (out->locale.language[0] == '\0') {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

static int scripts_conflict(const robo_i18n_locale_t *a, const robo_i18n_locale_t *b)
{
    robo_i18n_locale_t ma;
    robo_i18n_locale_t mb;
    const char *sa;
    const char *sb;

    if (robo_i18n_locale_maximize(a, &ma) != ROBO_OK) {
        ma = *a;
    }
    if (robo_i18n_locale_maximize(b, &mb) != ROBO_OK) {
        mb = *b;
    }
    sa = ma.script[0] ? ma.script : a->script;
    sb = mb.script[0] ? mb.script : b->script;
    /* If either side still has no script, do not treat as conflict. */
    if (!sa[0] || !sb[0]) {
        return 0;
    }
    return strcmp(sa, sb) != 0;
}

static int regions_compatible(const robo_i18n_locale_t *want, const robo_i18n_locale_t *have)
{
    if (!want->region[0] || !have->region[0]) {
        return 0;
    }
    return strcmp(want->region, have->region) == 0;
}

static int scripts_compatible_explicit(const robo_i18n_locale_t *want,
                                       const robo_i18n_locale_t *have)
{
    if (!want->script[0] || !have->script[0]) {
        return 0;
    }
    return strcmp(want->script, have->script) == 0;
}

robo_i18n_negotiate_kind_t robo_i18n_locale_match_quality(
    const robo_i18n_language_range_t *range,
    const robo_i18n_locale_t *supported)
{
    robo_i18n_locale_t want;
    robo_i18n_locale_t have;

    if (!range || !supported) {
        return ROBO_I18N_NEG_NONE;
    }
    if (range->is_wildcard) {
        return ROBO_I18N_NEG_WILDCARD;
    }
    if (!range->locale.language[0] || !supported->language[0]) {
        return ROBO_I18N_NEG_NONE;
    }
    if (strcmp(range->locale.language, supported->language) != 0) {
        return ROBO_I18N_NEG_NONE;
    }

    want = range->locale;
    have = *supported;
    (void)robo_i18n_locale_canonicalize(&want);
    (void)robo_i18n_locale_canonicalize(&have);

    if (scripts_conflict(&want, &have)) {
        return ROBO_I18N_NEG_NONE;
    }

    if (robo_i18n_locale_equal(&want, &have)) {
        return ROBO_I18N_NEG_EXACT;
    }

    /* Strip extensions/private for equality-ish exact of core fields */
    {
        robo_i18n_locale_t w = want;
        robo_i18n_locale_t h = have;
        w.extensions[0] = '\0';
        w.privateuse[0] = '\0';
        h.extensions[0] = '\0';
        h.privateuse[0] = '\0';
        if (robo_i18n_locale_equal(&w, &h)) {
            return ROBO_I18N_NEG_EXACT;
        }
    }

    if (regions_compatible(&want, &have)) {
        return ROBO_I18N_NEG_REGION;
    }
    if (scripts_compatible_explicit(&want, &have)) {
        return ROBO_I18N_NEG_SCRIPT;
    }
    /* Maximized script agreement with explicit request script */
    if (want.script[0]) {
        robo_i18n_locale_t mh;
        if (robo_i18n_locale_maximize(&have, &mh) == ROBO_OK && mh.script[0] &&
            strcmp(want.script, mh.script) == 0) {
            return ROBO_I18N_NEG_SCRIPT;
        }
    }
    return ROBO_I18N_NEG_LANGUAGE;
}

void robo_i18n_preference_list_destroy(robo_i18n_preference_list_t *list)
{
    if (!list) {
        return;
    }
    robo_free(list->items);
    robo_free(list);
}

size_t robo_i18n_preference_list_size(const robo_i18n_preference_list_t *list)
{
    return list ? list->count : 0u;
}

robo_result_t robo_i18n_preference_list_get(const robo_i18n_preference_list_t *list,
                                            size_t index,
                                            robo_i18n_language_range_t *out)
{
    if (!list || !out || index >= list->count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = list->items[index];
    return ROBO_OK;
}

static robo_result_t pref_push(robo_i18n_preference_list_t *list,
                               const robo_i18n_language_range_t *r)
{
    if (list->count >= ROBO_I18N_PREF_MAX_RANGES) {
        return ROBO_ERROR;
    }
    if (list->count == list->capacity) {
        size_t nc = list->capacity ? list->capacity * 2u : 4u;
        void *ni;
        if (nc > ROBO_I18N_PREF_MAX_RANGES) {
            nc = ROBO_I18N_PREF_MAX_RANGES;
        }
        ni = robo_realloc(list->items, nc * sizeof(robo_i18n_language_range_t));
        if (!ni) {
            return ROBO_OUT_OF_MEMORY;
        }
        list->items = (robo_i18n_language_range_t *)ni;
        list->capacity = nc;
    }
    list->items[list->count++] = *r;
    return ROBO_OK;
}

static int range_same_identity(const robo_i18n_language_range_t *a,
                               const robo_i18n_language_range_t *b)
{
    if (a->is_wildcard != b->is_wildcard) {
        return 0;
    }
    if (a->is_wildcard) {
        return 1;
    }
    return robo_i18n_locale_equal(&a->locale, &b->locale);
}

robo_result_t robo_i18n_preference_list_normalize(robo_i18n_preference_list_t *list)
{
    size_t i;
    size_t w;
    size_t *order = NULL;
    robo_i18n_language_range_t *tmp = NULL;

    if (!list) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (list->count <= 1u) {
        return ROBO_OK;
    }

    /* Highest-q wins for duplicates; equal q → first-wins (scan forward). */
    w = 0;
    for (i = 0; i < list->count; i++) {
        size_t j;
        int keep = 1;
        for (j = 0; j < w; j++) {
            if (range_same_identity(&list->items[i], &list->items[j])) {
                if (list->items[i].q_milli > list->items[j].q_milli) {
                    list->items[j] = list->items[i];
                }
                keep = 0;
                break;
            }
        }
        if (keep) {
            list->items[w++] = list->items[i];
        }
    }
    list->count = w;
    if (list->count <= 1u) {
        return ROBO_OK;
    }

    order = (size_t *)robo_malloc(list->count * sizeof(size_t));
    tmp = (robo_i18n_language_range_t *)robo_malloc(list->count *
                                                    sizeof(robo_i18n_language_range_t));
    if (!order || !tmp) {
        robo_free(order);
        robo_free(tmp);
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0; i < list->count; i++) {
        order[i] = i;
    }
    /* Stable insertion sort by q descending. */
    for (i = 1; i < list->count; i++) {
        size_t key = order[i];
        size_t b = i;
        while (b > 0 && list->items[order[b - 1]].q_milli < list->items[key].q_milli) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = key;
    }
    for (i = 0; i < list->count; i++) {
        tmp[i] = list->items[order[i]];
    }
    memcpy(list->items, tmp, list->count * sizeof(robo_i18n_language_range_t));
    robo_free(order);
    robo_free(tmp);
    return ROBO_OK;
}

robo_result_t robo_i18n_accept_language_parse(const char *text,
                                              size_t len,
                                              robo_i18n_preference_list_t **out)
{
    robo_i18n_preference_list_t *list;
    size_t i = 0;

    if (!text || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (len > ROBO_I18N_ACCEPT_LANG_MAX_BYTES) {
        return ROBO_ERROR;
    }
    if (len > 0 && !utf8_ok_asciiish(text, len)) {
        return ROBO_PARSE_ERROR;
    }

    list = (robo_i18n_preference_list_t *)robo_calloc(1, sizeof(*list));
    if (!list) {
        return ROBO_OUT_OF_MEMORY;
    }

    while (i < len) {
        size_t start;
        size_t end;
        robo_i18n_language_range_t range;
        robo_result_t r;

        while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == ',')) {
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
        while (end > start && is_ascii_ws((unsigned char)text[end - 1])) {
            end--;
        }
        if (end <= start) {
            continue;
        }
        r = robo_i18n_language_range_parse(text + start, end - start, &range);
        if (r != ROBO_OK) {
            continue; /* skip invalid entries */
        }
        r = pref_push(list, &range);
        if (r != ROBO_OK) {
            robo_i18n_preference_list_destroy(list);
            return r;
        }
    }

    *out = list;
    return ROBO_OK;
}

static size_t append_str(char *buf, size_t cap, size_t pos, const char *s)
{
    size_t n;
    if (!s) {
        return pos;
    }
    n = strlen(s);
    if (pos + n >= cap) {
        return (size_t)-1;
    }
    memcpy(buf + pos, s, n);
    return pos + n;
}

static size_t append_q(char *buf, size_t cap, size_t pos, robo_i18n_q_milli_t q)
{
    char tmp[8];
    size_t n = 0;
    unsigned whole = q / 1000u;
    unsigned frac = q % 1000u;
    if (whole > 1u) {
        whole = 1u;
        frac = 0u;
    }
    tmp[n++] = (char)('0' + (char)whole);
    if (frac != 0u) {
        tmp[n++] = '.';
        tmp[n++] = (char)('0' + (frac / 100u) % 10u);
        if (frac % 100u != 0u) {
            tmp[n++] = (char)('0' + (frac / 10u) % 10u);
            if (frac % 10u != 0u) {
                tmp[n++] = (char)('0' + frac % 10u);
            }
        }
    }
    tmp[n] = '\0';
    return append_str(buf, cap, pos, tmp);
}

robo_result_t robo_i18n_accept_language_serialize(const robo_i18n_preference_list_t *list,
                                                  char *buf,
                                                  size_t buf_cap,
                                                  size_t *out_len)
{
    size_t i;
    size_t pos = 0;

    if (!list || !buf || buf_cap == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    buf[0] = '\0';
    for (i = 0; i < list->count; i++) {
        const robo_i18n_language_range_t *r = &list->items[i];
        char tag[ROBO_I18N_TAG_MAX];
        size_t tlen = 0;

        if (i > 0) {
            pos = append_str(buf, buf_cap, pos, ",");
            if (pos == (size_t)-1) {
                return ROBO_ERROR;
            }
        }
        if (r->is_wildcard) {
            pos = append_str(buf, buf_cap, pos, "*");
        } else {
            if (robo_i18n_locale_format(&r->locale, tag, sizeof(tag), &tlen) != ROBO_OK) {
                return ROBO_ERROR;
            }
            pos = append_str(buf, buf_cap, pos, tag);
        }
        if (pos == (size_t)-1) {
            return ROBO_ERROR;
        }
        if (r->q_milli != ROBO_I18N_Q_MILLI_DEFAULT || r->q_explicit) {
            if (r->q_milli != ROBO_I18N_Q_MILLI_DEFAULT) {
                pos = append_str(buf, buf_cap, pos, ";q=");
                if (pos == (size_t)-1) {
                    return ROBO_ERROR;
                }
                pos = append_q(buf, buf_cap, pos, r->q_milli);
                if (pos == (size_t)-1) {
                    return ROBO_ERROR;
                }
            }
        }
        buf[pos] = '\0';
    }
    if (out_len) {
        *out_len = pos;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_preference_list_from_locales(const robo_i18n_locale_t *locales,
                                                     size_t count,
                                                     robo_i18n_preference_list_t **out)
{
    robo_i18n_preference_list_t *list;
    size_t i;

    if (!out || (count > 0 && !locales)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (count > ROBO_I18N_PREF_MAX_RANGES) {
        return ROBO_ERROR;
    }
    *out = NULL;
    list = (robo_i18n_preference_list_t *)robo_calloc(1, sizeof(*list));
    if (!list) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0; i < count; i++) {
        robo_i18n_language_range_t r;
        robo_i18n_language_range_clear(&r);
        r.locale = locales[i];
        (void)robo_i18n_locale_canonicalize(&r.locale);
        if (pref_push(list, &r) != ROBO_OK) {
            robo_i18n_preference_list_destroy(list);
            return ROBO_OUT_OF_MEMORY;
        }
    }
    *out = list;
    return ROBO_OK;
}

robo_result_t robo_i18n_preference_list_from_locale_list(
    const robo_i18n_locale_list_t *src,
    robo_i18n_preference_list_t **out)
{
    robo_i18n_preference_list_t *list;
    size_t i;
    size_t n;

    if (!src || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    n = robo_i18n_locale_list_size(src);
    if (n > ROBO_I18N_PREF_MAX_RANGES) {
        return ROBO_ERROR;
    }
    list = (robo_i18n_preference_list_t *)robo_calloc(1, sizeof(*list));
    if (!list) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0; i < n; i++) {
        robo_i18n_language_range_t r;
        uint16_t q = ROBO_I18N_Q_MILLI_DEFAULT;
        robo_i18n_language_range_clear(&r);
        if (robo_i18n_locale_list_get(src, i, &r.locale) != ROBO_OK) {
            robo_i18n_preference_list_destroy(list);
            return ROBO_INVALID_ARGUMENT;
        }
        (void)robo_i18n_locale_canonicalize(&r.locale);
        (void)robo_i18n_locale_list_get_q_milli(src, i, &q);
        r.q_milli = q;
        r.q_explicit = (q != ROBO_I18N_Q_MILLI_DEFAULT) ? 1u : 0u;
        if (pref_push(list, &r) != ROBO_OK) {
            robo_i18n_preference_list_destroy(list);
            return ROBO_OUT_OF_MEMORY;
        }
    }
    *out = list;
    return ROBO_OK;
}

static int is_excluded(const robo_i18n_preference_list_t *prefs,
                       const robo_i18n_locale_t *supported)
{
    size_t i;
    for (i = 0; i < prefs->count; i++) {
        const robo_i18n_language_range_t *r = &prefs->items[i];
        if (r->q_milli != 0) {
            continue;
        }
        if (r->is_wildcard) {
            return 1;
        }
        if (robo_i18n_locale_match_quality(r, supported) != ROBO_I18N_NEG_NONE) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_i18n_locale_negotiate_ex(const robo_i18n_preference_list_t *prefs,
                                            const robo_i18n_locale_t *supported,
                                            size_t supported_count,
                                            robo_i18n_negotiate_result_t *out)
{
    size_t pi;
    size_t si;
    robo_i18n_locale_t canon[ROBO_I18N_SUPPORTED_MAX];

    if (!prefs || !out || (supported_count > 0 && !supported)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (supported_count == 0 || supported_count > ROBO_I18N_SUPPORTED_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_negotiate_result_clear(out);

    for (si = 0; si < supported_count; si++) {
        canon[si] = supported[si];
        (void)robo_i18n_locale_canonicalize(&canon[si]);
    }

    for (pi = 0; pi < prefs->count; pi++) {
        const robo_i18n_language_range_t *pref = &prefs->items[pi];
        robo_i18n_negotiate_kind_t best_kind = ROBO_I18N_NEG_NONE;
        size_t best_si = 0;
        int found = 0;

        if (pref->q_milli == 0) {
            continue; /* exclusion only */
        }

        if (pref->is_wildcard) {
            for (si = 0; si < supported_count; si++) {
                if (!is_excluded(prefs, &canon[si])) {
                    out->selected = canon[si];
                    robo_i18n_locale_clear(&out->requested);
                    out->kind = ROBO_I18N_NEG_WILDCARD;
                    out->preference_index = pi;
                    out->q_milli = pref->q_milli;
                    return ROBO_OK;
                }
            }
            continue;
        }

        for (si = 0; si < supported_count; si++) {
            robo_i18n_negotiate_kind_t k;
            if (is_excluded(prefs, &canon[si])) {
                continue;
            }
            k = robo_i18n_locale_match_quality(pref, &canon[si]);
            if (k == ROBO_I18N_NEG_NONE) {
                continue;
            }
            /* Lower enum value among EXACT..LANGUAGE is better (EXACT=1 best). */
            if (!found || (int)k < (int)best_kind) {
                best_kind = k;
                best_si = si;
                found = 1;
                if (k == ROBO_I18N_NEG_EXACT) {
                    break;
                }
            }
        }

        if (found) {
            out->selected = canon[best_si];
            out->requested = pref->locale;
            out->kind = best_kind;
            out->preference_index = pi;
            out->q_milli = pref->q_milli;
            return ROBO_OK;
        }
    }

    return ROBO_NOT_FOUND;
}
