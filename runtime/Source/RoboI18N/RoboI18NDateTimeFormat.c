/*
 * RoboI18N date/time formatter (Goal 4.6).
 *
 * Pattern compile once → token list; format walks tokens.
 * Skeleton matching documented in i18n/README.md.
 */

#include "RoboI18N/RoboI18NDateTimeFormat.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboI18NDisplayNames.h"
#include "RoboI18N/RoboI18NLocaleData.h"

#include <string.h>

/* Canonical skeleton field order (CLDR UTS #35 subset). */
static const char k_skel_order[] = "GyQMwWEdDeEcahHkKmsSAVzZvVxX";

static int is_pattern_letter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

void robo_i18n_dtfmt_options_init(robo_i18n_dtfmt_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->date_style = ROBO_I18N_CAL_STYLE_MEDIUM;
    opts->time_style = ROBO_I18N_CAL_STYLE_MEDIUM;
}

static void format_locale_tag(const robo_i18n_locale_t *loc, char *buf, size_t cap)
{
    size_t n = 0;
    size_t ln;
    if (!loc || !buf || cap == 0) {
        return;
    }
    ln = strlen(loc->language);
    if (ln >= cap) {
        ln = cap - 1u;
    }
    memcpy(buf, loc->language, ln);
    n = ln;
    buf[n] = '\0';
    if (loc->script[0] && n + 5u < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4);
        n += 4;
        buf[n] = '\0';
    }
    if (loc->region[0] && n + 1u + strlen(loc->region) < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->region, strlen(loc->region));
        n += strlen(loc->region);
        buf[n] = '\0';
    }
}

static robo_result_t append_bytes(char *buf,
                                  size_t buf_cap,
                                  size_t *pos,
                                  const char *bytes,
                                  size_t len,
                                  int writing)
{
    if (!pos) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (writing) {
        if (!buf || buf_cap == 0) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (*pos + len + 1u > buf_cap) {
            return ROBO_ERROR;
        }
        memcpy(buf + *pos, bytes, len);
    }
    *pos += len;
    return ROBO_OK;
}

static robo_result_t compile_pattern(robo_i18n_datetime_formatter_t *fmt, const char *pattern)
{
    size_t i = 0;
    size_t plen;
    uint16_t tc = 0;

    if (!fmt || !pattern) {
        return ROBO_INVALID_ARGUMENT;
    }
    plen = strlen(pattern);
    if (plen == 0 || plen >= ROBO_I18N_DTFMT_PATTERN_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    memcpy(fmt->pattern, pattern, plen + 1u);
    fmt->token_count = 0;
    {
        size_t pack = plen + 1u; /* quoted literals packed after pattern NUL */

    while (i < plen) {
        unsigned char c = (unsigned char)pattern[i];
        robo_i18n_dtfmt_token_t *tok;

        if (tc >= ROBO_I18N_DTFMT_TOKEN_MAX) {
            return ROBO_ERROR;
        }
        tok = &fmt->tokens[tc];
        memset(tok, 0, sizeof(*tok));

        if (c == '\'') {
            /* Quoted literal. '' → single quote. */
            size_t j = i + 1u;
            size_t lit_len = 0;
            char unpacked[ROBO_I18N_DTFMT_PATTERN_MAX];

            while (j < plen) {
                if (pattern[j] == '\'') {
                    if (j + 1u < plen && pattern[j + 1u] == '\'') {
                        if (lit_len + 1u >= sizeof(unpacked)) {
                            return ROBO_ERROR;
                        }
                        unpacked[lit_len++] = '\'';
                        j += 2u;
                        continue;
                    }
                    break;
                }
                if (lit_len + 1u >= sizeof(unpacked)) {
                    return ROBO_ERROR;
                }
                unpacked[lit_len++] = pattern[j++];
            }
            if (pack + lit_len >= ROBO_I18N_DTFMT_PATTERN_MAX) {
                return ROBO_ERROR;
            }
            memcpy(fmt->pattern + pack, unpacked, lit_len);
            tok->is_literal = 1;
            tok->lit_off = (uint16_t)pack;
            tok->lit_len = (uint16_t)lit_len;
            pack += lit_len;
            i = (j < plen && pattern[j] == '\'') ? j + 1u : plen;
            tc++;
            continue;
        }

        if (is_pattern_letter(c)) {
            size_t j = i + 1u;
            while (j < plen && (unsigned char)pattern[j] == c) {
                j++;
            }
            tok->is_literal = 0;
            tok->field = (uint8_t)c;
            tok->count = (uint8_t)(j - i);
            if (tok->count == 0) {
                return ROBO_INVALID_ARGUMENT;
            }
            i = j;
            tc++;
            continue;
        }

        /* Non-letter literal run. */
        {
            size_t j = i + 1u;
            while (j < plen) {
                unsigned char d = (unsigned char)pattern[j];
                if (d == '\'' || is_pattern_letter(d)) {
                    break;
                }
                j++;
            }
            tok->is_literal = 1;
            tok->lit_off = (uint16_t)i;
            tok->lit_len = (uint16_t)(j - i);
            i = j;
            tc++;
        }
    }
    } /* pack scope */

    fmt->token_count = tc;
    return ROBO_OK;
}

static size_t format_offset_ascii(char *tmp,
                                  size_t cap,
                                  int32_t off,
                                  int with_colon,
                                  int with_gmt,
                                  int hour_width)
{
    int sign = off < 0 ? -1 : 1;
    int32_t abs_off = off < 0 ? -off : off;
    int hours = (int)(abs_off / 3600);
    int mins = (int)((abs_off % 3600) / 60);
    size_t n = 0;

    if (!tmp || cap < 12) {
        return 0;
    }
    if (with_gmt) {
        tmp[n++] = 'G';
        tmp[n++] = 'M';
        tmp[n++] = 'T';
    }
    tmp[n++] = (char)(sign < 0 ? '-' : '+');
    if (hour_width >= 2) {
        tmp[n++] = (char)('0' + (hours / 10));
        tmp[n++] = (char)('0' + (hours % 10));
    } else {
        if (hours >= 10) {
            tmp[n++] = (char)('0' + (hours / 10));
        }
        tmp[n++] = (char)('0' + (hours % 10));
    }
    if (with_colon) {
        tmp[n++] = ':';
        tmp[n++] = (char)('0' + (mins / 10));
        tmp[n++] = (char)('0' + (mins % 10));
    } else if (hour_width >= 2) {
        /* Always include minutes for basic ±HHMM forms. */
        tmp[n++] = (char)('0' + (mins / 10));
        tmp[n++] = (char)('0' + (mins % 10));
    }
    tmp[n] = '\0';
    return n;
}

static robo_result_t expand_datetime_combo(char *out,
                                           size_t out_cap,
                                           const char *combo,
                                           const char *date_pat,
                                           const char *time_pat)
{
    size_t oi = 0;
    size_t i = 0;
    size_t clen;
    if (!out || !combo || !date_pat || !time_pat || out_cap == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    clen = strlen(combo);
    while (i < clen) {
        if (combo[i] == '{' && i + 2u < clen && combo[i + 2u] == '}') {
            const char *ins = NULL;
            if (combo[i + 1u] == '0') {
                ins = time_pat;
            } else if (combo[i + 1u] == '1') {
                ins = date_pat;
            }
            if (ins) {
                size_t n = strlen(ins);
                if (oi + n + 1u > out_cap) {
                    return ROBO_ERROR;
                }
                memcpy(out + oi, ins, n);
                oi += n;
                i += 3u;
                continue;
            }
        }
        if (oi + 2u > out_cap) {
            return ROBO_ERROR;
        }
        out[oi++] = combo[i++];
    }
    out[oi] = '\0';
    return ROBO_OK;
}

/* ---- Skeleton normalize / match ---- */

typedef struct skel_field {
    char letter;
    uint8_t count;
} skel_field_t;

static int skel_order_index(char letter)
{
    const char *p = strchr(k_skel_order, letter);
    if (!p) {
        return -1;
    }
    return (int)(p - k_skel_order);
}

static size_t skel_parse(const char *skel, skel_field_t *out, size_t out_cap)
{
    size_t n = 0;
    size_t i = 0;
    size_t len;
    if (!skel || !out) {
        return 0;
    }
    len = strlen(skel);
    while (i < len && n < out_cap) {
        char c = skel[i];
        size_t j = i + 1u;
        if (!is_pattern_letter((unsigned char)c)) {
            i++;
            continue;
        }
        while (j < len && skel[j] == c) {
            j++;
        }
        out[n].letter = c;
        out[n].count = (uint8_t)(j - i);
        n++;
        i = j;
    }
    return n;
}

static void skel_normalize(skel_field_t *fields, size_t *count)
{
    skel_field_t tmp[32];
    size_t n;
    size_t i;
    size_t w = 0;
    if (!fields || !count) {
        return;
    }
    n = *count;
    /* Merge duplicates by keeping max count; then sort by k_skel_order. */
    for (i = 0; i < n; i++) {
        size_t j;
        int found = 0;
        for (j = 0; j < w; j++) {
            if (tmp[j].letter == fields[i].letter) {
                if (fields[i].count > tmp[j].count) {
                    tmp[j].count = fields[i].count;
                }
                found = 1;
                break;
            }
        }
        if (!found && w < 32) {
            tmp[w++] = fields[i];
        }
    }
    /* Insertion sort by order index; unknown letters last. */
    for (i = 1; i < w; i++) {
        skel_field_t key = tmp[i];
        int ki = skel_order_index(key.letter);
        size_t j = i;
        while (j > 0) {
            int pj = skel_order_index(tmp[j - 1u].letter);
            if (pj <= ki) {
                break;
            }
            tmp[j] = tmp[j - 1u];
            j--;
        }
        tmp[j] = key;
    }
    memcpy(fields, tmp, w * sizeof(skel_field_t));
    *count = w;
}

static void skel_to_string(const skel_field_t *fields, size_t count, char *out, size_t cap)
{
    size_t i;
    size_t o = 0;
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    for (i = 0; i < count; i++) {
        uint8_t c;
        for (c = 0; c < fields[i].count; c++) {
            if (o + 2u > cap) {
                out[cap - 1u] = '\0';
                return;
            }
            out[o++] = fields[i].letter;
        }
    }
    out[o] = '\0';
}

static int skel_score(const skel_field_t *want, size_t wn, const skel_field_t *have, size_t hn)
{
    int score = 0;
    size_t i;
    size_t j;
    /* Missing requested field: heavy penalty. Extra field: light. Length delta: medium. */
    for (i = 0; i < wn; i++) {
        int found = 0;
        for (j = 0; j < hn; j++) {
            if (have[j].letter == want[i].letter) {
                int d = (int)have[j].count - (int)want[i].count;
                if (d < 0) {
                    d = -d;
                }
                score += d * 4;
                found = 1;
                break;
            }
        }
        if (!found) {
            score += 100;
        }
    }
    for (j = 0; j < hn; j++) {
        int found = 0;
        for (i = 0; i < wn; i++) {
            if (want[i].letter == have[j].letter) {
                found = 1;
                break;
            }
        }
        if (!found) {
            score += 10;
        }
    }
    return score;
}

static const robo_cal_available_format_t *avail_find_inherited(const robo_i18n_locale_t *loc,
                                                                const char *calendar,
                                                                const char *skeleton)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[64];
    const robo_cal_available_format_t *row;

    if (!loc || !calendar || !skeleton) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_cal_available_format_find(tag, calendar, skeleton);
        if (row) {
            return row;
        }
        row = robo_cal_available_format_find(cur.language, calendar, skeleton);
        if (row) {
            return row;
        }
        {
            robo_i18n_locale_t parent;
            if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
                break;
            }
            cur = parent;
        }
    }
    return robo_cal_available_format_find("en", calendar, skeleton);
}

static robo_result_t resolve_skeleton_pattern(const robo_i18n_locale_t *locale,
                                              const robo_i18n_calendar_t *calendar,
                                              const char *skeleton,
                                              const char **out_pattern)
{
    skel_field_t want[32];
    size_t wn;
    char norm[ROBO_I18N_DTFMT_SKELETON_MAX];
    const robo_cal_available_format_t *row;
    const char *cid;
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[64];
    int best_score = 0x7fffffff;
    const char *best_pat = NULL;

    if (!locale || !calendar || !skeleton || !out_pattern) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_pattern = NULL;
    cid = calendar->id;
    wn = skel_parse(skeleton, want, 32);
    if (wn == 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    skel_normalize(want, &wn);
    skel_to_string(want, wn, norm, sizeof(norm));

    row = avail_find_inherited(locale, cid, norm);
    if (!row) {
        row = avail_find_inherited(locale, cid, skeleton);
    }
    if (row) {
        *out_pattern = robo_cal_str(row->pattern);
        return ROBO_OK;
    }

    /* Best-match over availableFormats for locale inheritance chain. */
    cur = *locale;
    while (guard++ < 16) {
        size_t count = 0;
        const robo_cal_available_format_t *begin;
        size_t i;
        format_locale_tag(&cur, tag, sizeof(tag));
        begin = robo_cal_available_formats_begin(tag, cid, &count);
        if (!begin || count == 0) {
            begin = robo_cal_available_formats_begin(cur.language, cid, &count);
        }
        if (begin) {
            for (i = 0; i < count; i++) {
                skel_field_t have[32];
                size_t hn;
                int sc;
                const char *sk = robo_cal_str(begin[i].skeleton);
                hn = skel_parse(sk, have, 32);
                skel_normalize(have, &hn);
                sc = skel_score(want, wn, have, hn);
                if (sc < best_score) {
                    best_score = sc;
                    best_pat = robo_cal_str(begin[i].pattern);
                }
            }
        }
        {
            robo_i18n_locale_t parent;
            if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
                break;
            }
            cur = parent;
        }
    }
    if (!best_pat) {
        size_t count = 0;
        const robo_cal_available_format_t *begin =
            robo_cal_available_formats_begin("en", cid, &count);
        size_t i;
        if (begin) {
            for (i = 0; i < count; i++) {
                skel_field_t have[32];
                size_t hn;
                int sc;
                const char *sk = robo_cal_str(begin[i].skeleton);
                hn = skel_parse(sk, have, 32);
                skel_normalize(have, &hn);
                sc = skel_score(want, wn, have, hn);
                if (sc < best_score) {
                    best_score = sc;
                    best_pat = robo_cal_str(begin[i].pattern);
                }
            }
        }
    }
    if (!best_pat || best_score >= 100) {
        return ROBO_NOT_FOUND;
    }
    *out_pattern = best_pat;
    return ROBO_OK;
}

robo_result_t robo_i18n_datetime_resolve_skeleton(const robo_i18n_locale_t *locale,
                                                  const robo_i18n_calendar_t *calendar,
                                                  const char *skeleton,
                                                  const char **out_pattern)
{
    robo_i18n_calendar_t cal;
    if (!calendar) {
        if (robo_i18n_calendar_from_kind(ROBO_I18N_CAL_GREGORIAN, &cal) != ROBO_OK) {
            return ROBO_INVALID_ARGUMENT;
        }
        calendar = &cal;
    }
    return resolve_skeleton_pattern(locale, calendar, skeleton, out_pattern);
}

static robo_result_t init_common(robo_i18n_datetime_formatter_t *fmt,
                                 const robo_i18n_locale_t *locale,
                                 const robo_i18n_calendar_t *calendar,
                                 const robo_i18n_timezone_t *tz,
                                 const robo_i18n_dtfmt_options_t *opts)
{
    robo_i18n_number_options_t nopts;
    robo_result_t r;

    if (!fmt || !locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(fmt, 0, sizeof(*fmt));
    fmt->locale = *locale;
    if (calendar) {
        fmt->calendar = *calendar;
    } else {
        r = robo_i18n_calendar_from_kind(ROBO_I18N_CAL_GREGORIAN, &fmt->calendar);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (!robo_i18n_calendar_has_conversion(&fmt->calendar)) {
        return ROBO_NOT_SUPPORTED;
    }
    if (tz) {
        fmt->timezone = *tz;
    } else {
        r = robo_i18n_timezone_utc(&fmt->timezone);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (opts) {
        fmt->options = *opts;
    } else {
        robo_i18n_dtfmt_options_init(&fmt->options);
    }

    robo_i18n_number_options_init(&nopts);
    nopts.use_grouping = 0;
    nopts.min_integer_digits = 1;
    nopts.min_fraction_digits = 0;
    nopts.max_fraction_digits = 0;
    if (fmt->options.numbering_system[0]) {
        size_t n = strlen(fmt->options.numbering_system);
        if (n >= sizeof(nopts.numbering_system)) {
            return ROBO_INVALID_ARGUMENT;
        }
        memcpy(nopts.numbering_system, fmt->options.numbering_system, n + 1u);
    }
    r = robo_i18n_number_formatter_init(&fmt->number, &fmt->locale, &nopts);
    if (r != ROBO_OK) {
        return r;
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_datetime_formatter_init_pattern(
    robo_i18n_datetime_formatter_t *fmt,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *tz,
    const char *pattern,
    const robo_i18n_dtfmt_options_t *opts)
{
    robo_result_t r = init_common(fmt, locale, calendar, tz, opts);
    if (r != ROBO_OK) {
        return r;
    }
    r = compile_pattern(fmt, pattern);
    if (r != ROBO_OK) {
        robo_i18n_datetime_formatter_destroy(fmt);
        return r;
    }
    fmt->ready = 1;
    return ROBO_OK;
}

robo_result_t robo_i18n_datetime_formatter_init(robo_i18n_datetime_formatter_t *fmt,
                                                const robo_i18n_locale_t *locale,
                                                const robo_i18n_calendar_t *calendar,
                                                const robo_i18n_timezone_t *tz,
                                                robo_i18n_dtfmt_kind_t kind,
                                                const robo_i18n_dtfmt_options_t *opts)
{
    robo_i18n_dtfmt_options_t local_opts;
    const char *date_pat = NULL;
    const char *time_pat = NULL;
    const char *combo = NULL;
    char combined[ROBO_I18N_DTFMT_PATTERN_MAX];
    robo_result_t r;
    robo_i18n_calendar_t tmp_cal;
    const robo_i18n_calendar_t *cal;

    if (opts) {
        local_opts = *opts;
    } else {
        robo_i18n_dtfmt_options_init(&local_opts);
    }

    cal = calendar;
    if (!cal) {
        r = robo_i18n_calendar_from_kind(ROBO_I18N_CAL_GREGORIAN, &tmp_cal);
        if (r != ROBO_OK) {
            return r;
        }
        cal = &tmp_cal;
    }

    if (kind == ROBO_I18N_DTFMT_DATE || kind == ROBO_I18N_DTFMT_DATETIME) {
        r = robo_i18n_calendar_date_pattern(locale, cal, local_opts.date_style, &date_pat);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (kind == ROBO_I18N_DTFMT_TIME || kind == ROBO_I18N_DTFMT_DATETIME) {
        r = robo_i18n_calendar_time_pattern(locale, cal, local_opts.time_style, &time_pat);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (kind == ROBO_I18N_DTFMT_DATE) {
        return robo_i18n_datetime_formatter_init_pattern(fmt, locale, calendar, tz, date_pat,
                                                         &local_opts);
    }
    if (kind == ROBO_I18N_DTFMT_TIME) {
        return robo_i18n_datetime_formatter_init_pattern(fmt, locale, calendar, tz, time_pat,
                                                         &local_opts);
    }
    if (kind != ROBO_I18N_DTFMT_DATETIME) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_calendar_datetime_pattern(locale, cal, local_opts.date_style, &combo);
    if (r != ROBO_OK) {
        return r;
    }
    r = expand_datetime_combo(combined, sizeof(combined), combo, date_pat, time_pat);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_datetime_formatter_init_pattern(fmt, locale, calendar, tz, combined,
                                                     &local_opts);
}

robo_result_t robo_i18n_datetime_formatter_init_skeleton(
    robo_i18n_datetime_formatter_t *fmt,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *tz,
    const char *skeleton,
    const robo_i18n_dtfmt_options_t *opts)
{
    const char *pattern = NULL;
    robo_i18n_calendar_t tmp;
    const robo_i18n_calendar_t *cal = calendar;
    robo_result_t r;

    if (!cal) {
        r = robo_i18n_calendar_from_kind(ROBO_I18N_CAL_GREGORIAN, &tmp);
        if (r != ROBO_OK) {
            return r;
        }
        cal = &tmp;
    }
    r = resolve_skeleton_pattern(locale, cal, skeleton, &pattern);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_datetime_formatter_init_pattern(fmt, locale, calendar, tz, pattern, opts);
}

void robo_i18n_datetime_formatter_destroy(robo_i18n_datetime_formatter_t *fmt)
{
    if (!fmt) {
        return;
    }
    robo_i18n_number_formatter_destroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
}

/* ---- Field emission ---- */

typedef struct fmt_ctx {
    const robo_i18n_datetime_formatter_t *fmt;
    robo_i18n_calendar_fields_t fields;
    robo_i18n_date_t gregorian;
    robo_i18n_time_t time;
    robo_i18n_weekday_t weekday;
    int32_t offset_seconds; /* known zone offset for display; 0 if UTC */
    const robo_i18n_instant_t *instant; /* non-NULL when formatting an instant */
    int have_time;
    int have_date;
} fmt_ctx_t;

static robo_result_t emit_number(const robo_i18n_datetime_formatter_t *fmt,
                                 int64_t value,
                                 int min_digits,
                                 char *buf,
                                 size_t buf_cap,
                                 size_t *pos,
                                 int writing)
{
    robo_i18n_number_formatter_t nf = fmt->number;
    char tmp[48];
    size_t len = 0;
    robo_result_t r;

    nf.options.min_integer_digits = min_digits < 1 ? 1 : min_digits;
    if (nf.options.min_integer_digits > 16) {
        nf.options.min_integer_digits = 16;
    }
    nf.options.use_grouping = 0;
    nf.options.max_fraction_digits = 0;
    nf.options.min_fraction_digits = 0;
    r = robo_i18n_number_format_i64(&nf, value, tmp, sizeof(tmp), &len);
    if (r != ROBO_OK) {
        return r;
    }
    return append_bytes(buf, buf_cap, pos, tmp, len, writing);
}

static robo_result_t emit_text(char *buf,
                               size_t buf_cap,
                               size_t *pos,
                               const char *s,
                               int writing)
{
    if (!s) {
        return ROBO_NOT_FOUND;
    }
    return append_bytes(buf, buf_cap, pos, s, strlen(s), writing);
}

static robo_result_t emit_zone(const fmt_ctx_t *ctx,
                               char field,
                               uint8_t count,
                               char *buf,
                               size_t buf_cap,
                               size_t *pos,
                               int writing)
{
    int32_t off = ctx->offset_seconds;
    char tmp[128];
    size_t n = 0;
    robo_result_t r;
    robo_i18n_timezone_name_style_t style;
    const robo_i18n_instant_t *inst = ctx->instant;

    /* Localized names via Goal 4.15 display layer. */
    if (field == 'z') {
        style = count >= 4 ? ROBO_I18N_TZ_DISP_SPECIFIC_LONG : ROBO_I18N_TZ_DISP_SPECIFIC_SHORT;
        r = robo_i18n_timezone_display_name_at(&ctx->fmt->timezone, inst, style, &ctx->fmt->locale,
                                               NULL, tmp, sizeof(tmp), &n);
        if (r == ROBO_OK) {
            return append_bytes(buf, buf_cap, pos, tmp, n, writing);
        }
    } else if (field == 'v') {
        style = count >= 4 ? ROBO_I18N_TZ_DISP_LONG_GENERIC : ROBO_I18N_TZ_DISP_SHORT_GENERIC;
        r = robo_i18n_timezone_display_name_at(&ctx->fmt->timezone, inst, style, &ctx->fmt->locale,
                                               NULL, tmp, sizeof(tmp), &n);
        if (r == ROBO_OK) {
            return append_bytes(buf, buf_cap, pos, tmp, n, writing);
        }
    } else if (field == 'V') {
        if (count >= 4) {
            style = ROBO_I18N_TZ_DISP_EXEMPLAR_CITY;
        } else if (count == 1) {
            style = ROBO_I18N_TZ_DISP_IDENTIFIER;
        } else {
            style = ROBO_I18N_TZ_DISP_EXEMPLAR_CITY;
        }
        r = robo_i18n_timezone_display_name_at(&ctx->fmt->timezone, inst, style, &ctx->fmt->locale,
                                               NULL, tmp, sizeof(tmp), &n);
        if (r == ROBO_OK) {
            return append_bytes(buf, buf_cap, pos, tmp, n, writing);
        }
    } else if (field == 'O') {
        r = robo_i18n_timezone_display_name_at(&ctx->fmt->timezone, inst, ROBO_I18N_TZ_DISP_GMT,
                                               &ctx->fmt->locale, NULL, tmp, sizeof(tmp), &n);
        if (r == ROBO_OK) {
            return append_bytes(buf, buf_cap, pos, tmp, n, writing);
        }
    }

    if (field == 'X' && off == 0) {
        return append_bytes(buf, buf_cap, pos, "Z", 1u, writing);
    }
    if (field == 'Z' && count < 5) {
        n = format_offset_ascii(tmp, sizeof(tmp), off, 0, 0, 2);
    } else if (field == 'X' || field == 'x') {
        if (count == 1) {
            int sign = off < 0 ? -1 : 1;
            int hours = (int)((off < 0 ? -off : off) / 3600);
            tmp[0] = (char)(sign < 0 ? '-' : '+');
            tmp[1] = (char)('0' + (hours / 10));
            tmp[2] = (char)('0' + (hours % 10));
            tmp[3] = '\0';
            n = 3;
        } else if (count == 2 || count == 4) {
            n = format_offset_ascii(tmp, sizeof(tmp), off, 0, 0, 2);
        } else {
            n = format_offset_ascii(tmp, sizeof(tmp), off, 1, 0, 2);
        }
    } else if (field == 'Z' && count >= 5) {
        n = format_offset_ascii(tmp, sizeof(tmp), off, 1, 0, 2);
    } else {
        /* Fallback GMT±HH:MM ASCII if localized path failed. */
        n = format_offset_ascii(tmp, sizeof(tmp), off, 1, 1, 2);
    }
    if (n == 0) {
        return ROBO_ERROR;
    }
    return append_bytes(buf, buf_cap, pos, tmp, n, writing);
}

static int hour12(uint8_t hour24)
{
    int h = (int)(hour24 % 12u);
    return h == 0 ? 12 : h;
}

static robo_result_t emit_field(const fmt_ctx_t *ctx,
                                char field,
                                uint8_t count,
                                char *buf,
                                size_t buf_cap,
                                size_t *pos,
                                int writing)
{
    const robo_i18n_datetime_formatter_t *fmt = ctx->fmt;
    const char *name = NULL;
    robo_result_t r;
    robo_i18n_cal_name_width_t width;

    switch (field) {
    case 'G':
        width = count >= 5   ? ROBO_I18N_CAL_NAME_NARROW
                : count >= 4 ? ROBO_I18N_CAL_NAME_WIDE
                             : ROBO_I18N_CAL_NAME_ABBREVIATED;
        r = robo_i18n_calendar_era_name(&fmt->locale, &fmt->calendar, ctx->fields.era_index, width,
                                        &name);
        if (r != ROBO_OK) {
            return r;
        }
        return emit_text(buf, buf_cap, pos, name, writing);

    case 'y':
    case 'Y':
    case 'u':
        if (!ctx->have_date) {
            return ROBO_INVALID_ARGUMENT;
        }
        {
            int64_t year = ctx->fields.year;
            int min_d = count == 2 ? 2 : (count >= 4 ? 4 : count);
            if (count == 2) {
                year = year % 100;
                if (year < 0) {
                    year = -year;
                }
            }
            return emit_number(fmt, year, min_d, buf, buf_cap, pos, writing);
        }

    case 'M':
    case 'L':
        if (!ctx->have_date) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (count == 1 || count == 2) {
            return emit_number(fmt, (int64_t)ctx->fields.month, count, buf, buf_cap, pos, writing);
        }
        width = count >= 5   ? ROBO_I18N_CAL_NAME_NARROW
                : count == 4 ? ROBO_I18N_CAL_NAME_WIDE
                             : ROBO_I18N_CAL_NAME_ABBREVIATED;
        r = robo_i18n_calendar_month_name(&fmt->locale, &fmt->calendar, ctx->fields.month, width,
                                          &name);
        if (r != ROBO_OK) {
            return r;
        }
        return emit_text(buf, buf_cap, pos, name, writing);

    case 'd':
        if (!ctx->have_date) {
            return ROBO_INVALID_ARGUMENT;
        }
        return emit_number(fmt, (int64_t)ctx->fields.day, count >= 2 ? 2 : 1, buf, buf_cap, pos,
                           writing);

    case 'E':
    case 'e':
    case 'c':
        if (!ctx->have_date) {
            return ROBO_INVALID_ARGUMENT;
        }
        if ((field == 'e' || field == 'c') && count <= 2) {
            /* Local day-of-week number — use ISO weekday for now. */
            return emit_number(fmt, (int64_t)ctx->weekday, count, buf, buf_cap, pos, writing);
        }
        width = count >= 5   ? ROBO_I18N_CAL_NAME_NARROW
                : count >= 4 ? ROBO_I18N_CAL_NAME_WIDE
                             : ROBO_I18N_CAL_NAME_ABBREVIATED;
        r = robo_i18n_calendar_weekday_name(&fmt->locale, &fmt->calendar, ctx->weekday, width,
                                            &name);
        if (r != ROBO_OK) {
            return r;
        }
        return emit_text(buf, buf_cap, pos, name, writing);

    case 'a':
    case 'b':
    case 'B':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        {
            const char *pid = ctx->time.hour < 12 ? "am" : "pm";
            width = count >= 5   ? ROBO_I18N_CAL_NAME_NARROW
                    : count >= 4 ? ROBO_I18N_CAL_NAME_WIDE
                                 : ROBO_I18N_CAL_NAME_ABBREVIATED;
            r = robo_i18n_calendar_day_period_name(&fmt->locale, &fmt->calendar, pid, width, &name);
            if (r != ROBO_OK) {
                return r;
            }
            return emit_text(buf, buf_cap, pos, name, writing);
        }

    case 'h':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        return emit_number(fmt, (int64_t)hour12(ctx->time.hour), count >= 2 ? 2 : 1, buf, buf_cap,
                           pos, writing);

    case 'H':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        return emit_number(fmt, (int64_t)ctx->time.hour, count >= 2 ? 2 : 1, buf, buf_cap, pos,
                           writing);

    case 'K':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        return emit_number(fmt, (int64_t)(ctx->time.hour % 12u), count >= 2 ? 2 : 1, buf, buf_cap,
                           pos, writing);

    case 'k':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        {
            int k = ctx->time.hour == 0 ? 24 : (int)ctx->time.hour;
            return emit_number(fmt, (int64_t)k, count >= 2 ? 2 : 1, buf, buf_cap, pos, writing);
        }

    case 'm':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        return emit_number(fmt, (int64_t)ctx->time.minute, count >= 2 ? 2 : 1, buf, buf_cap, pos,
                           writing);

    case 's':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        return emit_number(fmt, (int64_t)ctx->time.second, count >= 2 ? 2 : 1, buf, buf_cap, pos,
                           writing);

    case 'S':
        if (!ctx->have_time) {
            return ROBO_INVALID_ARGUMENT;
        }
        {
            uint8_t want = count > 9 ? 9 : count;
            uint32_t div = 1u;
            uint8_t i;
            uint32_t v;
            for (i = 0; i < (uint8_t)(9u - want); i++) {
                div *= 10u;
            }
            v = ctx->time.nano / div;
            return emit_number(fmt, (int64_t)v, (int)want, buf, buf_cap, pos, writing);
        }

    case 'z':
    case 'Z':
    case 'O':
    case 'v':
    case 'V':
    case 'x':
    case 'X':
        return emit_zone(ctx, field, count, buf, buf_cap, pos, writing);

    default:
        /* Unsupported pattern letter: emit as-is (letter × count) for resilience. */
        {
            char tmp[16];
            uint8_t i;
            uint8_t n = count > 15 ? 15 : count;
            for (i = 0; i < n; i++) {
                tmp[i] = field;
            }
            return append_bytes(buf, buf_cap, pos, tmp, n, writing);
        }
    }
}

static robo_result_t format_ctx(const fmt_ctx_t *ctx, char *buf, size_t buf_cap, size_t *out_len)
{
    size_t pos = 0;
    size_t i;
    int writing = (buf != NULL && buf_cap > 0);
    robo_result_t r;

    if (!ctx || !ctx->fmt || !ctx->fmt->ready || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }

    for (i = 0; i < ctx->fmt->token_count; i++) {
        const robo_i18n_dtfmt_token_t *tok = &ctx->fmt->tokens[i];
        if (tok->is_literal) {
            r = append_bytes(buf, buf_cap, &pos, ctx->fmt->pattern + tok->lit_off, tok->lit_len,
                             writing);
            if (r != ROBO_OK) {
                *out_len = pos;
                return r;
            }
        } else {
            r = emit_field(ctx, (char)tok->field, tok->count, buf, buf_cap, &pos, writing);
            if (r != ROBO_OK) {
                *out_len = pos;
                return r;
            }
        }
    }

    *out_len = pos;
    if (writing) {
        if (pos + 1u > buf_cap) {
            return ROBO_ERROR;
        }
        buf[pos] = '\0';
    }
    return ROBO_OK;
}

static robo_result_t prepare_from_datetime(const robo_i18n_datetime_formatter_t *fmt,
                                           const robo_i18n_datetime_t *dt,
                                           int need_date,
                                           int need_time,
                                           fmt_ctx_t *ctx)
{
    robo_result_t r;
    if (!fmt || !fmt->ready || !dt || !ctx) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->fmt = fmt;
    ctx->have_date = need_date;
    ctx->have_time = need_time;
    ctx->time = dt->time;
    ctx->gregorian = dt->date;
    ctx->offset_seconds = fmt->timezone.offset_seconds;
    if (fmt->timezone.kind == ROBO_I18N_TZ_UTC) {
        ctx->offset_seconds = 0;
    }

    if (need_date) {
        if (!robo_i18n_date_is_valid(&dt->date)) {
            return ROBO_INVALID_ARGUMENT;
        }
        r = robo_i18n_calendar_from_gregorian(&fmt->calendar, &dt->date, &ctx->fields);
        if (r != ROBO_OK) {
            return r;
        }
        r = robo_i18n_date_day_of_week(&dt->date, &ctx->weekday);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (need_time) {
        if (!robo_i18n_time_is_valid(&dt->time)) {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_datetime_format_date(const robo_i18n_datetime_formatter_t *fmt,
                                             const robo_i18n_date_t *date,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len)
{
    robo_i18n_datetime_t dt;
    fmt_ctx_t ctx;
    robo_result_t r;
    if (!date) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(&dt, 0, sizeof(dt));
    dt.date = *date;
    r = prepare_from_datetime(fmt, &dt, 1, 0, &ctx);
    if (r != ROBO_OK) {
        return r;
    }
    return format_ctx(&ctx, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_datetime_format_time(const robo_i18n_datetime_formatter_t *fmt,
                                             const robo_i18n_time_t *time,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len)
{
    robo_i18n_datetime_t dt;
    fmt_ctx_t ctx;
    robo_result_t r;
    if (!time) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(&dt, 0, sizeof(dt));
    dt.time = *time;
    r = prepare_from_datetime(fmt, &dt, 0, 1, &ctx);
    if (r != ROBO_OK) {
        return r;
    }
    return format_ctx(&ctx, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_datetime_format_datetime(const robo_i18n_datetime_formatter_t *fmt,
                                                 const robo_i18n_datetime_t *dt,
                                                 char *buf,
                                                 size_t buf_cap,
                                                 size_t *out_len)
{
    fmt_ctx_t ctx;
    robo_result_t r = prepare_from_datetime(fmt, dt, 1, 1, &ctx);
    if (r != ROBO_OK) {
        return r;
    }
    return format_ctx(&ctx, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_datetime_format_instant(const robo_i18n_datetime_formatter_t *fmt,
                                                const robo_i18n_instant_t *instant,
                                                char *buf,
                                                size_t buf_cap,
                                                size_t *out_len)
{
    robo_i18n_datetime_t local;
    robo_i18n_tz_resolve_t resolve = ROBO_I18N_TZ_UNIQUE;
    fmt_ctx_t ctx;
    robo_result_t r;
    int32_t off = 0;

    if (!fmt || !fmt->ready || !instant) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_timezone_to_local(&fmt->timezone, instant, &local, &resolve);
    if (r != ROBO_OK) {
        return r;
    }
    if (resolve != ROBO_I18N_TZ_UNIQUE) {
        /* Should not happen for UTC/FIXED; refuse ambiguous without policy. */
        return ROBO_NOT_SUPPORTED;
    }
    r = robo_i18n_timezone_offset_at(&fmt->timezone, instant, &off);
    if (r != ROBO_OK) {
        return r;
    }
    r = prepare_from_datetime(fmt, &local, 1, 1, &ctx);
    if (r != ROBO_OK) {
        return r;
    }
    ctx.offset_seconds = off;
    ctx.instant = instant;
    return format_ctx(&ctx, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_datetime_format_to_string(const robo_i18n_datetime_formatter_t *fmt,
                                                  const robo_i18n_datetime_t *dt,
                                                  robo_string_t *out)
{
    char stack[384];
    size_t len = 0;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_datetime_format_datetime(fmt, dt, stack, sizeof(stack), &len);
    if (r == ROBO_OK) {
        return robo_string_append(out, stack, len);
    }
    return r;
}
