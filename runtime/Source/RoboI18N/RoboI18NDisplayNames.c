/*
 * RoboI18N locale display names (Goal 4.10).
 * CLDR 46 tables in RoboDispData. Fallback: display locale → parents → en → code.
 */

#include "RoboI18N/RoboI18NDisplayNames.h"

#include "RoboI18N/RoboDispData.h"
#include "RoboI18N/RoboI18NLocaleData.h"

#include <stdio.h>
#include <string.h>

void robo_i18n_display_options_init(robo_i18n_display_options_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->fallback = ROBO_I18N_DISP_FALLBACK_CODE;
    out->dialect = 0;
}

const char *robo_i18n_display_cldr_version_string(void)
{
    return robo_disp_cldr_version_string();
}

static const robo_i18n_display_options_t *opts_or_default(
    const robo_i18n_display_options_t *options,
    robo_i18n_display_options_t *scratch)
{
    if (options) {
        return options;
    }
    robo_i18n_display_options_init(scratch);
    return scratch;
}

static int str_eq_idx(int32_t idx, const char *s, size_t len)
{
    const char *p = robo_disp_string(idx);
    size_t n;
    if (!p) {
        return 0;
    }
    n = strlen(p);
    return n == len && memcmp(p, s, len) == 0;
}

static int find_locale_idx(const char *tag, size_t len, int32_t *out_idx)
{
    uint32_t i;
    for (i = 0; i < robo_disp_string_count; i++) {
        if (str_eq_idx((int32_t)i, tag, len)) {
            *out_idx = (int32_t)i;
            return 1;
        }
    }
    return 0;
}

static int lookup_name_row(const robo_disp_name_row_t *rows,
                           uint32_t count,
                           int32_t loc_idx,
                           const char *code,
                           size_t code_len,
                           int32_t *out_name)
{
    uint32_t lo = 0;
    uint32_t hi = count;
    /* Find lower bound for loc_idx */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (rows[mid].display_locale < loc_idx) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    while (lo < count && rows[lo].display_locale == loc_idx) {
        if (str_eq_idx(rows[lo].code, code, code_len)) {
            *out_name = rows[lo].name;
            return 1;
        }
        lo++;
    }
    return 0;
}

static robo_result_t write_out(const char *s,
                               char *out,
                               size_t out_cap,
                               size_t *out_len)
{
    size_t n;
    if (!s || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = strlen(s);
    *out_len = n;
    if (!out) {
        return ROBO_OK;
    }
    if (out_cap < n + 1u) {
        *out_len = n + 1u;
        return ROBO_ERROR;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return ROBO_OK;
}

static void format_tag(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    if (!loc || cap == 0) {
        return;
    }
    (void)robo_i18n_locale_format(loc, buf, cap, NULL);
}

static int next_display_locale(robo_i18n_locale_t *cur)
{
    robo_i18n_locale_t parent;
    if (robo_i18n_locale_data_parent(cur, &parent) == ROBO_OK) {
        *cur = parent;
        return 1;
    }
    if (robo_i18n_locale_parent(cur, &parent) == ROBO_OK) {
        *cur = parent;
        return 1;
    }
    return 0;
}

/**
 * Walk display locale chain then "en". Return name string or NULL.
 */
static const char *resolve_name(const robo_disp_name_row_t *rows,
                                uint32_t count,
                                const robo_i18n_locale_t *display_locale,
                                const char *code,
                                size_t code_len)
{
    robo_i18n_locale_t cur;
    char tag[128];
    int32_t loc_idx;
    int32_t name_idx;
    int guard = 0;

    if (display_locale) {
        cur = *display_locale;
    } else {
        (void)robo_i18n_locale_parse_canonical("en", 2, &cur);
    }

    for (;;) {
        size_t tlen = 0;
        format_tag(&cur, tag, sizeof(tag));
        tlen = strlen(tag);
        if (tlen && find_locale_idx(tag, tlen, &loc_idx) &&
            lookup_name_row(rows, count, loc_idx, code, code_len, &name_idx)) {
            return robo_disp_string(name_idx);
        }
        if (!next_display_locale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }

    /* Ultimate table fallback: en (CLDR root often empty for display names). */
    if (find_locale_idx("en", 2, &loc_idx) &&
        lookup_name_row(rows, count, loc_idx, code, code_len, &name_idx)) {
        return robo_disp_string(name_idx);
    }
    return NULL;
}

static robo_result_t finish_name(const char *name,
                                 const char *code_fallback,
                                 size_t code_len,
                                 const robo_i18n_display_options_t *opt,
                                 char *out,
                                 size_t out_cap,
                                 size_t *out_len)
{
    char tmp[128];
    if (name) {
        return write_out(name, out, out_cap, out_len);
    }
    if (opt->fallback == ROBO_I18N_DISP_FALLBACK_CODE) {
        if (code_len >= sizeof(tmp)) {
            return ROBO_INVALID_ARGUMENT;
        }
        memcpy(tmp, code_fallback, code_len);
        tmp[code_len] = '\0';
        return write_out(tmp, out, out_cap, out_len);
    }
    if (out_len) {
        *out_len = 0;
    }
    if (out && out_cap > 0) {
        out[0] = '\0';
    }
    return ROBO_NOT_FOUND;
}

static void normalize_lang_key(const char *id, size_t len, char *out, size_t cap, size_t *out_len)
{
    /* Accept bare language or BCP 47; use primary language subtag lowercased. */
    robo_i18n_locale_t loc;
    size_t i;
    if (robo_i18n_locale_parse_canonical(id, len, &loc) == ROBO_OK && loc.language[0]) {
        size_t n = strlen(loc.language);
        if (n >= cap) {
            n = cap - 1;
        }
        memcpy(out, loc.language, n);
        out[n] = '\0';
        *out_len = n;
        return;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    for (i = 0; i < len; i++) {
        char c = id[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[i] = c;
    }
    out[len] = '\0';
    *out_len = len;
}

robo_result_t robo_i18n_language_display_name(const char *language_id,
                                              size_t language_len,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    char key[64];
    size_t key_len = 0;
    const char *name;

    if (!language_id || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    normalize_lang_key(language_id, language_len, key, sizeof(key), &key_len);
    name = resolve_name(robo_disp_languages, robo_disp_language_count, display_locale, key,
                        key_len);
    return finish_name(name, key, key_len, opt, out, out_cap, out_len);
}

robo_result_t robo_i18n_script_display_name(const char *script_id,
                                            size_t script_len,
                                            const robo_i18n_locale_t *display_locale,
                                            const robo_i18n_display_options_t *options,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    char key[16];
    size_t i;
    const char *name;

    if (!script_id || script_len == 0 || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (script_len >= sizeof(key)) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    /* ISO 15924: 4 letters, Title case in CLDR keys (Latn). */
    for (i = 0; i < script_len; i++) {
        char c = script_id[i];
        if (i == 0) {
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 'A');
            }
        } else {
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 'a');
            }
        }
        key[i] = c;
    }
    key[script_len] = '\0';
    name = resolve_name(robo_disp_scripts, robo_disp_script_count, display_locale, key,
                        script_len);
    return finish_name(name, key, script_len, opt, out, out_cap, out_len);
}

robo_result_t robo_i18n_region_display_name(const char *region_id,
                                            size_t region_len,
                                            const robo_i18n_locale_t *display_locale,
                                            const robo_i18n_display_options_t *options,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    char key[16];
    size_t i;
    const char *name;

    if (!region_id || region_len == 0 || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (region_len >= sizeof(key)) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    for (i = 0; i < region_len; i++) {
        char c = region_id[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        key[i] = c;
    }
    key[region_len] = '\0';
    name = resolve_name(robo_disp_territories, robo_disp_territory_count, display_locale, key,
                        region_len);
    return finish_name(name, key, region_len, opt, out, out_cap, out_len);
}

static int lookup_currency(int32_t loc_idx,
                           const char *code,
                           size_t code_len,
                           const robo_disp_currency_row_t **out_row)
{
    uint32_t lo = 0;
    uint32_t hi = robo_disp_currency_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (robo_disp_currencies[mid].display_locale < loc_idx) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    while (lo < robo_disp_currency_count &&
           robo_disp_currencies[lo].display_locale == loc_idx) {
        if (str_eq_idx(robo_disp_currencies[lo].code, code, code_len)) {
            *out_row = &robo_disp_currencies[lo];
            return 1;
        }
        lo++;
    }
    return 0;
}

static const robo_disp_currency_row_t *resolve_currency(
    const robo_i18n_locale_t *display_locale,
    const char *code,
    size_t code_len)
{
    robo_i18n_locale_t cur;
    char tag[128];
    int32_t loc_idx;
    const robo_disp_currency_row_t *row = NULL;
    int guard = 0;

    if (display_locale) {
        cur = *display_locale;
    } else {
        (void)robo_i18n_locale_parse_canonical("en", 2, &cur);
    }
    for (;;) {
        format_tag(&cur, tag, sizeof(tag));
        if (tag[0] && find_locale_idx(tag, strlen(tag), &loc_idx) &&
            lookup_currency(loc_idx, code, code_len, &row)) {
            return row;
        }
        if (!next_display_locale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
    if (find_locale_idx("en", 2, &loc_idx) &&
        lookup_currency(loc_idx, code, code_len, &row)) {
        return row;
    }
    return NULL;
}

robo_result_t robo_i18n_currency_display_name(const char *currency_code,
                                              size_t currency_len,
                                              robo_i18n_currency_name_style_t style,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    char key[8];
    size_t i;
    const robo_disp_currency_row_t *row;
    const char *name = NULL;

    if (!currency_code || currency_len == 0 || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (currency_len >= sizeof(key)) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    for (i = 0; i < currency_len; i++) {
        char c = currency_code[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        key[i] = c;
    }
    key[currency_len] = '\0';
    row = resolve_currency(display_locale, key, currency_len);
    if (row) {
        if (style == ROBO_I18N_CURRENCY_SYMBOL) {
            name = robo_disp_string(row->symbol);
        } else if (style == ROBO_I18N_CURRENCY_NARROW_SYMBOL) {
            name = robo_disp_string(row->narrow_symbol);
        } else {
            name = robo_disp_string(row->name);
        }
    }
    return finish_name(name, key, currency_len, opt, out, out_cap, out_len);
}

robo_result_t robo_i18n_calendar_display_name(const robo_i18n_calendar_t *calendar,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    const char *id;
    const char *name;

    if (!calendar || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    id = robo_i18n_calendar_id(calendar);
    if (!id) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* iso8601 displays as gregorian calendar name when no dedicated entry */
    name = resolve_name(robo_disp_calendars, robo_disp_calendar_count, display_locale, id,
                        strlen(id));
    if (!name && strcmp(id, "iso8601") == 0) {
        name = resolve_name(robo_disp_calendars, robo_disp_calendar_count, display_locale,
                            "gregorian", 9);
    }
    return finish_name(name, id, strlen(id), opt, out, out_cap, out_len);
}

robo_result_t robo_i18n_numbering_system_display_name(
    const char *numbering_id,
    size_t numbering_len,
    const robo_i18n_locale_t *display_locale,
    const robo_i18n_display_options_t *options,
    char *out,
    size_t out_cap,
    size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    char key[32];
    size_t i;
    const char *name;

    if (!numbering_id || numbering_len == 0 || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (numbering_len >= sizeof(key)) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    for (i = 0; i < numbering_len; i++) {
        char c = numbering_id[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        key[i] = c;
    }
    key[numbering_len] = '\0';
    name = resolve_name(robo_disp_numbering, robo_disp_numbering_count, display_locale, key,
                        numbering_len);
    return finish_name(name, key, numbering_len, opt, out, out_cap, out_len);
}

static int lookup_mz_for_iana(const char *iana, size_t len, char *mz_out, size_t mz_cap)
{
    uint32_t lo = 0;
    uint32_t hi = robo_disp_iana_metazone_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const char *id = robo_disp_string(robo_disp_iana_metazones[mid].iana_id);
        int cmp;
        if (!id) {
            lo = mid + 1u;
            continue;
        }
        cmp = strncmp(id, iana, len);
        if (cmp == 0 && id[len] == '\0') {
            const char *mz = robo_disp_string(robo_disp_iana_metazones[mid].metazone);
            if (!mz) {
                return 0;
            }
            strncpy(mz_out, mz, mz_cap - 1);
            mz_out[mz_cap - 1] = '\0';
            return 1;
        }
        if (cmp < 0 || (cmp == 0 && strlen(id) < len)) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    /* Linear fallback if sort by string index isn't lexical by content —
     * indices aren't lexical. Scan. */
    for (lo = 0; lo < robo_disp_iana_metazone_count; lo++) {
        const char *id = robo_disp_string(robo_disp_iana_metazones[lo].iana_id);
        if (id && strlen(id) == len && memcmp(id, iana, len) == 0) {
            const char *mz = robo_disp_string(robo_disp_iana_metazones[lo].metazone);
            if (!mz) {
                return 0;
            }
            strncpy(mz_out, mz, mz_cap - 1);
            mz_out[mz_cap - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

static const robo_disp_tz_metazone_row_t *resolve_metazone_row(
    const robo_i18n_locale_t *display_locale,
    const char *mz,
    size_t mz_len)
{
    robo_i18n_locale_t cur;
    char tag[128];
    int32_t loc_idx;
    int guard = 0;

    if (display_locale) {
        cur = *display_locale;
    } else {
        (void)robo_i18n_locale_parse_canonical("en", 2, &cur);
    }
    for (;;) {
        uint32_t i;
        format_tag(&cur, tag, sizeof(tag));
        if (tag[0] && find_locale_idx(tag, strlen(tag), &loc_idx)) {
            for (i = 0; i < robo_disp_tz_metazone_count; i++) {
                if (robo_disp_tz_metazones[i].display_locale == loc_idx &&
                    str_eq_idx(robo_disp_tz_metazones[i].metazone, mz, mz_len)) {
                    return &robo_disp_tz_metazones[i];
                }
            }
        }
        if (!next_display_locale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
    if (find_locale_idx("en", 2, &loc_idx)) {
        uint32_t i;
        for (i = 0; i < robo_disp_tz_metazone_count; i++) {
            if (robo_disp_tz_metazones[i].display_locale == loc_idx &&
                str_eq_idx(robo_disp_tz_metazones[i].metazone, mz, mz_len)) {
                return &robo_disp_tz_metazones[i];
            }
        }
    }
    return NULL;
}

static void format_gmt(int32_t offset_seconds, const char *gmt_fmt, const char *hour_fmt,
                       char *out, size_t cap)
{
    char hm[16];
    int neg = offset_seconds < 0;
    int32_t abs_s = neg ? -offset_seconds : offset_seconds;
    int hours = (int)(abs_s / 3600);
    int mins = (int)((abs_s % 3600) / 60);
    const char *pos_pat = hour_fmt;
    const char *neg_pat = hour_fmt;
    const char *semi = strchr(hour_fmt, ';');
    char pattern_buf[32];
    const char *pat;
    char *dst;
    const char *src;
    size_t oi = 0;

    if (semi) {
        size_t n = (size_t)(semi - hour_fmt);
        if (n >= sizeof(pattern_buf)) {
            n = sizeof(pattern_buf) - 1;
        }
        memcpy(pattern_buf, hour_fmt, n);
        pattern_buf[n] = '\0';
        pos_pat = pattern_buf;
        neg_pat = semi + 1;
    }
    pat = neg ? neg_pat : pos_pat;
    /* Expand HH:mm style */
    dst = hm;
    for (src = pat; *src && (size_t)(dst - hm) < sizeof(hm) - 1; src++) {
        if (src[0] == 'H' && src[1] == 'H') {
            dst += sprintf(dst, "%02d", hours);
            src++;
        } else if (src[0] == 'm' && src[1] == 'm') {
            dst += sprintf(dst, "%02d", mins);
            src++;
        } else {
            *dst++ = *src;
        }
    }
    *dst = '\0';

    /* Substitute {0} in gmtFormat */
    for (src = gmt_fmt; *src && oi + 1 < cap; src++) {
        if (src[0] == '{' && src[1] == '0' && src[2] == '}') {
            size_t n = strlen(hm);
            if (oi + n >= cap) {
                break;
            }
            memcpy(out + oi, hm, n);
            oi += n;
            src += 2;
        } else {
            out[oi++] = *src;
        }
    }
    out[oi] = '\0';
}

robo_result_t robo_i18n_timezone_display_name(const robo_i18n_timezone_t *tz,
                                              robo_i18n_timezone_name_style_t style,
                                              const robo_i18n_locale_t *display_locale,
                                              const robo_i18n_display_options_t *options,
                                              char *out,
                                              size_t out_cap,
                                              size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    char mz[64];
    const char *name = NULL;
    const char *iana;

    if (!tz || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);
    iana = tz->id[0] ? tz->id : "UTC";

    if (style == ROBO_I18N_TZ_DISP_GMT) {
        int32_t off = 0;
        char tag[128];
        int32_t loc_idx;
        const char *gmt_fmt = "GMT{0}";
        const char *hour_fmt = "+HH:mm;-HH:mm";
        char buf[64];
        robo_i18n_locale_t cur;
        int guard = 0;

        if (tz->kind == ROBO_I18N_TZ_IANA || tz->kind == ROBO_I18N_TZ_SYSTEM) {
            return ROBO_NOT_SUPPORTED;
        }
        off = tz->offset_seconds;
        if (display_locale) {
            cur = *display_locale;
        } else {
            (void)robo_i18n_locale_parse_canonical("en", 2, &cur);
        }
        for (;;) {
            uint32_t i;
            format_tag(&cur, tag, sizeof(tag));
            if (tag[0] && find_locale_idx(tag, strlen(tag), &loc_idx)) {
                for (i = 0; i < robo_disp_tz_pattern_count; i++) {
                    if (robo_disp_tz_patterns[i].display_locale == loc_idx) {
                        gmt_fmt = robo_disp_string(robo_disp_tz_patterns[i].gmt_format);
                        hour_fmt = robo_disp_string(robo_disp_tz_patterns[i].hour_format);
                        goto got_pat;
                    }
                }
            }
            if (!next_display_locale(&cur)) {
                break;
            }
            if (++guard > 16) {
                break;
            }
        }
    got_pat:
        if (!gmt_fmt) {
            gmt_fmt = "GMT{0}";
        }
        if (!hour_fmt) {
            hour_fmt = "+HH:mm;-HH:mm";
        }
        format_gmt(off, gmt_fmt, hour_fmt, buf, sizeof(buf));
        return write_out(buf, out, out_cap, out_len);
    }

    if (style == ROBO_I18N_TZ_DISP_EXEMPLAR_CITY) {
        name = resolve_name(robo_disp_tz_exemplars, robo_disp_tz_exemplar_count,
                            display_locale, iana, strlen(iana));
        if (!name) {
            /* Fall back to last path segment */
            const char *slash = strrchr(iana, '/');
            const char *city = slash ? slash + 1 : iana;
            return finish_name(NULL, city, strlen(city), opt, out, out_cap, out_len);
        }
        return finish_name(name, iana, strlen(iana), opt, out, out_cap, out_len);
    }

    if (!lookup_mz_for_iana(iana, strlen(iana), mz, sizeof(mz))) {
        return finish_name(NULL, iana, strlen(iana), opt, out, out_cap, out_len);
    }
    {
        const robo_disp_tz_metazone_row_t *row =
            resolve_metazone_row(display_locale, mz, strlen(mz));
        if (row) {
            int32_t idx = -1;
            if (style == ROBO_I18N_TZ_DISP_LONG_GENERIC) {
                idx = row->long_generic >= 0 ? row->long_generic : row->long_standard;
            } else if (style == ROBO_I18N_TZ_DISP_LONG_STANDARD) {
                idx = row->long_standard;
            } else if (style == ROBO_I18N_TZ_DISP_LONG_DAYLIGHT) {
                idx = row->long_daylight >= 0 ? row->long_daylight : row->long_standard;
            }
            if (idx >= 0) {
                name = robo_disp_string(idx);
            }
        }
    }
    return finish_name(name, iana, strlen(iana), opt, out, out_cap, out_len);
}

static void apply_pattern2(const char *pat, const char *a, const char *b, char *out, size_t cap)
{
    size_t oi = 0;
    const char *src;
    for (src = pat; *src && oi + 1 < cap; src++) {
        if (src[0] == '{' && src[2] == '}') {
            const char *rep = (src[1] == '0') ? a : (src[1] == '1') ? b : NULL;
            if (rep) {
                size_t n = strlen(rep);
                if (oi + n >= cap) {
                    break;
                }
                memcpy(out + oi, rep, n);
                oi += n;
                src += 2;
                continue;
            }
        }
        out[oi++] = *src;
    }
    out[oi] = '\0';
}

static void get_patterns(const robo_i18n_locale_t *display_locale,
                         const char **locale_pat,
                         const char **sep_pat)
{
    robo_i18n_locale_t cur;
    char tag[128];
    int32_t loc_idx;
    int guard = 0;

    *locale_pat = "{0} ({1})";
    *sep_pat = "{0}, {1}";
    if (display_locale) {
        cur = *display_locale;
    } else {
        (void)robo_i18n_locale_parse_canonical("en", 2, &cur);
    }
    for (;;) {
        uint32_t i;
        format_tag(&cur, tag, sizeof(tag));
        if (tag[0] && find_locale_idx(tag, strlen(tag), &loc_idx)) {
            for (i = 0; i < robo_disp_pattern_count; i++) {
                if (robo_disp_patterns[i].display_locale == loc_idx) {
                    *locale_pat = robo_disp_string(robo_disp_patterns[i].locale_pattern);
                    *sep_pat = robo_disp_string(robo_disp_patterns[i].locale_separator);
                    return;
                }
            }
        }
        if (!next_display_locale(&cur)) {
            break;
        }
        if (++guard > 16) {
            break;
        }
    }
}

robo_result_t robo_i18n_locale_display_name(const robo_i18n_locale_t *described,
                                            const robo_i18n_locale_t *display_locale,
                                            const robo_i18n_display_options_t *options,
                                            char *out,
                                            size_t out_cap,
                                            size_t *out_len)
{
    robo_i18n_display_options_t scratch;
    const robo_i18n_display_options_t *opt;
    char lang[128];
    char script[128];
    char region[128];
    char rest[256];
    char composed[384];
    size_t n = 0;
    const char *locale_pat;
    const char *sep_pat;
    int have_rest = 0;

    if (!described || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    opt = opts_or_default(options, &scratch);

    if (robo_i18n_language_display_name(described->language, strlen(described->language),
                                        display_locale, opt, lang, sizeof(lang), &n) !=
        ROBO_OK) {
        return ROBO_ERROR;
    }

    rest[0] = '\0';
    if (described->script[0]) {
        if (robo_i18n_script_display_name(described->script, strlen(described->script),
                                          display_locale, opt, script, sizeof(script),
                                          &n) != ROBO_OK) {
            return ROBO_ERROR;
        }
        strncpy(rest, script, sizeof(rest) - 1);
        have_rest = 1;
    }
    if (described->region[0]) {
        if (robo_i18n_region_display_name(described->region, strlen(described->region),
                                          display_locale, opt, region, sizeof(region),
                                          &n) != ROBO_OK) {
            return ROBO_ERROR;
        }
        if (have_rest) {
            char tmp[256];
            get_patterns(display_locale, &locale_pat, &sep_pat);
            apply_pattern2(sep_pat, rest, region, tmp, sizeof(tmp));
            strncpy(rest, tmp, sizeof(rest) - 1);
        } else {
            strncpy(rest, region, sizeof(rest) - 1);
            have_rest = 1;
        }
    }

    if (!have_rest) {
        return write_out(lang, out, out_cap, out_len);
    }
    get_patterns(display_locale, &locale_pat, &sep_pat);
    apply_pattern2(locale_pat, lang, rest, composed, sizeof(composed));
    return write_out(composed, out, out_cap, out_len);
}
