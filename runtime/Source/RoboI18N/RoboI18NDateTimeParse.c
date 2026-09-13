/*
 * RoboI18N date/time parsing (Goal 4.23).
 *
 * Pattern-driven reverse of Goal 4.6 formatting. Reuses compiled formatter
 * tokens and CLDR name/digit tables — no runtime CLDR XML/JSON parsing.
 */

#include "RoboI18N/RoboI18NDateTimeParse.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stdint.h>
#include <string.h>

typedef struct parse_state {
    const robo_i18n_datetime_parser_t *parser;
    const char *text;
    size_t len;
    size_t at;

    int have_year;
    int have_month;
    int have_day;
    int have_hour;
    int have_minute;
    int have_second;
    int have_nano;
    int have_weekday;
    int have_era;
    int have_ampm;
    int ampm_is_pm;
    int hour_style; /* 'h','H','K','k' */
    int32_t hour_raw;
    int32_t year;
    int32_t month;
    int32_t day;
    int32_t era_index;
    int32_t weekday; /* ISO 1..7 */
    int32_t minute;
    int32_t second;
    uint32_t nano;
    int have_offset;
    int have_tz_id;
    int32_t offset_seconds;
    char tz_id[ROBO_I18N_TZ_ID_MAX + 1u];
} parse_state_t;

void robo_i18n_dtparse_options_init(robo_i18n_dtparse_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->mode = ROBO_I18N_PARSE_STRICT;
    opts->date_style = ROBO_I18N_CAL_STYLE_MEDIUM;
    opts->time_style = ROBO_I18N_CAL_STYLE_MEDIUM;
    opts->allow_leading_ws = 1;
    opts->allow_trailing_ws = 1;
    opts->require_full = 1;
    opts->resolve_instant = 0;
    opts->disambiguation = ROBO_I18N_DTPARSE_DISAMBIG_REJECT;
}

void robo_i18n_dtparse_result_clear(robo_i18n_dtparse_result_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->error = ROBO_I18N_DTPARSE_ERR_NONE;
    out->tz_resolve = ROBO_I18N_TZ_UNIQUE;
}

const char *robo_i18n_dtparse_err_string(robo_i18n_dtparse_err_t err)
{
    switch (err) {
    case ROBO_I18N_DTPARSE_ERR_NONE:
        return "ok";
    case ROBO_I18N_DTPARSE_ERR_EMPTY:
        return "empty";
    case ROBO_I18N_DTPARSE_ERR_INVALID_UTF8:
        return "invalid_utf8";
    case ROBO_I18N_DTPARSE_ERR_UNEXPECTED_CHAR:
        return "unexpected_char";
    case ROBO_I18N_DTPARSE_ERR_TRAILING:
        return "trailing";
    case ROBO_I18N_DTPARSE_ERR_INVALID_DATE:
        return "invalid_date";
    case ROBO_I18N_DTPARSE_ERR_INVALID_TIME:
        return "invalid_time";
    case ROBO_I18N_DTPARSE_ERR_INVALID_FIELD:
        return "invalid_field";
    case ROBO_I18N_DTPARSE_ERR_INVALID_MONTH_NAME:
        return "invalid_month_name";
    case ROBO_I18N_DTPARSE_ERR_INVALID_WEEKDAY_NAME:
        return "invalid_weekday_name";
    case ROBO_I18N_DTPARSE_ERR_INVALID_ERA:
        return "invalid_era";
    case ROBO_I18N_DTPARSE_ERR_INVALID_DAY_PERIOD:
        return "invalid_day_period";
    case ROBO_I18N_DTPARSE_ERR_INVALID_TIMEZONE:
        return "invalid_timezone";
    case ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_TIMEZONE:
        return "ambiguous_timezone";
    case ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_LOCAL:
        return "ambiguous_local";
    case ROBO_I18N_DTPARSE_ERR_NONEXISTENT_LOCAL:
        return "nonexistent_local";
    case ROBO_I18N_DTPARSE_ERR_MISSING_TIMEZONE:
        return "missing_timezone";
    case ROBO_I18N_DTPARSE_ERR_OVERFLOW:
        return "overflow";
    case ROBO_I18N_DTPARSE_ERR_UNSUPPORTED_CALENDAR:
        return "unsupported_calendar";
    case ROBO_I18N_DTPARSE_ERR_UNSUPPORTED_LOCALE:
        return "unsupported_locale";
    case ROBO_I18N_DTPARSE_ERR_INPUT_TOO_LONG:
        return "input_too_long";
    case ROBO_I18N_DTPARSE_ERR_PATTERN:
        return "pattern";
    default:
        return "unknown";
    }
}

static int build_digit_table(robo_i18n_datetime_parser_t *parser)
{
    const char *g;
    size_t glen;
    size_t off = 0u;
    int d;

    for (d = 0; d < 10; d++) {
        parser->digit_utf8[d] = NULL;
        parser->digit_len[d] = 0u;
    }
    g = parser->fmt.number.digit_glyphs;
    if (!g || !g[0]) {
        static const char ascii[] = "0123456789";
        for (d = 0; d < 10; d++) {
            parser->digit_utf8[d] = ascii + d;
            parser->digit_len[d] = 1u;
        }
        return 1;
    }
    glen = strlen(g);
    for (d = 0; d < 10; d++) {
        robo_uchar32_t cp = 0;
        size_t next = off;
        if (off >= glen) {
            return 0;
        }
        if (robo_unicode_utf8_next(g, glen, off, ROBO_UNICODE_STRICT, &cp, &next) !=
            ROBO_UNICODE_OK) {
            return 0;
        }
        parser->digit_utf8[d] = g + off;
        parser->digit_len[d] = (uint8_t)(next - off);
        off = next;
    }
    return 1;
}

static void infer_hour_cycle_from_pattern(const char *pattern, char out[8])
{
    size_t i;
    int in_quote = 0;
    char seen = 0;

    out[0] = '\0';
    if (!pattern) {
        return;
    }
    for (i = 0; pattern[i]; i++) {
        char c = pattern[i];
        if (c == '\'') {
            if (pattern[i + 1] == '\'') {
                i++;
                continue;
            }
            in_quote = !in_quote;
            continue;
        }
        if (in_quote) {
            continue;
        }
        if (c == 'h' || c == 'H' || c == 'K' || c == 'k') {
            seen = c;
            break;
        }
    }
    if (seen == 'h') {
        memcpy(out, "h12", 4);
    } else if (seen == 'K') {
        memcpy(out, "h11", 4);
    } else if (seen == 'k') {
        memcpy(out, "h24", 4);
    } else if (seen == 'H') {
        memcpy(out, "h23", 4);
    }
}

static void copy_opts_to_dtfmt(const robo_i18n_dtparse_options_t *opts,
                               robo_i18n_dtfmt_options_t *fopts)
{
    robo_i18n_dtfmt_options_init(fopts);
    if (!opts) {
        return;
    }
    fopts->date_style = opts->date_style;
    fopts->time_style = opts->time_style;
    if (opts->numbering_system[0]) {
        memcpy(fopts->numbering_system, opts->numbering_system,
               sizeof(fopts->numbering_system));
    }
    if (opts->hour_cycle[0]) {
        memcpy(fopts->hour_cycle, opts->hour_cycle, sizeof(fopts->hour_cycle));
    }
}

static robo_result_t finish_parser_init(robo_i18n_datetime_parser_t *parser,
                                        const robo_i18n_dtparse_options_t *opts,
                                        robo_i18n_dtfmt_kind_t kind,
                                        int has_resolve_tz)
{
    if (opts) {
        parser->options = *opts;
    } else {
        robo_i18n_dtparse_options_init(&parser->options);
    }
    parser->kind = kind;
    parser->has_resolve_tz = has_resolve_tz ? 1u : 0u;
    if (!build_digit_table(parser)) {
        robo_i18n_datetime_formatter_destroy(&parser->fmt);
        return ROBO_ERROR;
    }
    parser->ready = 1u;
    return ROBO_OK;
}

robo_result_t robo_i18n_datetime_parser_init(robo_i18n_datetime_parser_t *parser,
                                             const robo_i18n_locale_t *locale,
                                             const robo_i18n_calendar_t *calendar,
                                             const robo_i18n_timezone_t *resolve_tz,
                                             robo_i18n_dtfmt_kind_t kind,
                                             const robo_i18n_dtparse_options_t *opts)
{
    robo_i18n_dtfmt_options_t fopts;
    robo_result_t r;

    if (!parser || !locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    copy_opts_to_dtfmt(opts, &fopts);
    r = robo_i18n_datetime_formatter_init(&parser->fmt, locale, calendar, resolve_tz, kind,
                                          &fopts);
    if (r != ROBO_OK) {
        return r;
    }
    return finish_parser_init(parser, opts, kind, resolve_tz != NULL);
}

robo_result_t robo_i18n_datetime_parser_init_pattern(
    robo_i18n_datetime_parser_t *parser,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *resolve_tz,
    const char *pattern,
    const robo_i18n_dtparse_options_t *opts)
{
    robo_i18n_dtfmt_options_t fopts;
    robo_result_t r;

    if (!parser || !locale || !pattern) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    copy_opts_to_dtfmt(opts, &fopts);
    /* Preserve explicit pattern hour letters (avoid locale hc remapping H→h). */
    if (!fopts.hour_cycle[0]) {
        infer_hour_cycle_from_pattern(pattern, fopts.hour_cycle);
    }
    r = robo_i18n_datetime_formatter_init_pattern(&parser->fmt, locale, calendar, resolve_tz,
                                                  pattern, &fopts);
    if (r != ROBO_OK) {
        return r;
    }
    return finish_parser_init(parser, opts, ROBO_I18N_DTFMT_DATETIME, resolve_tz != NULL);
}

robo_result_t robo_i18n_datetime_parser_init_skeleton(
    robo_i18n_datetime_parser_t *parser,
    const robo_i18n_locale_t *locale,
    const robo_i18n_calendar_t *calendar,
    const robo_i18n_timezone_t *resolve_tz,
    const char *skeleton,
    const robo_i18n_dtparse_options_t *opts)
{
    robo_i18n_dtfmt_options_t fopts;
    robo_result_t r;

    if (!parser || !locale || !skeleton) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(parser, 0, sizeof(*parser));
    copy_opts_to_dtfmt(opts, &fopts);
    if (!fopts.hour_cycle[0]) {
        const char *pat = NULL;
        if (robo_i18n_datetime_resolve_skeleton(locale, calendar, skeleton, &pat) == ROBO_OK &&
            pat) {
            infer_hour_cycle_from_pattern(pat, fopts.hour_cycle);
        }
    }
    r = robo_i18n_datetime_formatter_init_skeleton(&parser->fmt, locale, calendar, resolve_tz,
                                                   skeleton, &fopts);
    if (r != ROBO_OK) {
        return r;
    }
    return finish_parser_init(parser, opts, ROBO_I18N_DTFMT_DATETIME, resolve_tz != NULL);
}

void robo_i18n_datetime_parser_destroy(robo_i18n_datetime_parser_t *parser)
{
    if (!parser) {
        return;
    }
    robo_i18n_datetime_formatter_destroy(&parser->fmt);
    memset(parser, 0, sizeof(*parser));
}

/* ---- UTF-8 / whitespace helpers ---- */

static size_t skip_ws(const char *text, size_t len, size_t at)
{
    while (at < len) {
        robo_uchar32_t cp = 0;
        size_t next = at;
        if (robo_unicode_utf8_next(text, len, at, ROBO_UNICODE_STRICT, &cp, &next) !=
            ROBO_UNICODE_OK) {
            break;
        }
        if (!robo_unicode_is_whitespace(cp)) {
            break;
        }
        at = next;
    }
    return at;
}

static int match_digit(const robo_i18n_datetime_parser_t *p,
                       const char *text,
                       size_t len,
                       size_t at,
                       int *out_d,
                       size_t *out_next)
{
    int d;
    if (at >= len) {
        return 0;
    }
    for (d = 0; d < 10; d++) {
        if (p->digit_len[d] == 0u) {
            continue;
        }
        if (at + p->digit_len[d] <= len &&
            memcmp(text + at, p->digit_utf8[d], p->digit_len[d]) == 0) {
            *out_d = d;
            *out_next = at + p->digit_len[d];
            return 1;
        }
    }
    if (text[at] >= '0' && text[at] <= '9') {
        *out_d = (int)(text[at] - '0');
        *out_next = at + 1u;
        return 1;
    }
    return 0;
}

static int utf8_valid_span(const char *text, size_t len, size_t *bad_off)
{
    size_t at = 0;
    while (at < len) {
        robo_uchar32_t cp = 0;
        size_t next = at;
        if (robo_unicode_utf8_next(text, len, at, ROBO_UNICODE_STRICT, &cp, &next) !=
            ROBO_UNICODE_OK) {
            if (bad_off) {
                *bad_off = at;
            }
            return 0;
        }
        at = next;
    }
    return 1;
}

static int prefix_eq(const char *text, size_t len, size_t at, const char *lit, size_t lit_len)
{
    if (!lit || lit_len == 0) {
        return 1;
    }
    if (at + lit_len > len) {
        return 0;
    }
    return memcmp(text + at, lit, lit_len) == 0;
}

static int ascii_ieq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static int match_ascii_ci(const char *text, size_t len, size_t at, const char *lit, size_t *next)
{
    size_t i;
    size_t n = strlen(lit);
    if (at + n > len) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!ascii_ieq(text[at + i], lit[i])) {
            return 0;
        }
    }
    *next = at + n;
    return 1;
}

/* Read unsigned decimal with localized digits. max_digits caps length.
 * Returns digit count via *out_digits when non-NULL. */
static int read_uint(const robo_i18n_datetime_parser_t *p,
                     const char *text,
                     size_t len,
                     size_t at,
                     int min_digits,
                     int max_digits,
                     int64_t *out_v,
                     size_t *out_next,
                     int *overflow,
                     int *out_digits)
{
    int64_t v = 0;
    int count = 0;
    size_t cur = at;
    *overflow = 0;
    if (out_digits) {
        *out_digits = 0;
    }
    if (max_digits < 1) {
        max_digits = 1;
    }
    if (min_digits < 0) {
        min_digits = 0;
    }
    while (count < max_digits) {
        int d = 0;
        size_t next = cur;
        if (!match_digit(p, text, len, cur, &d, &next)) {
            break;
        }
        if (v > (INT64_MAX - d) / 10) {
            *overflow = 1;
            return 0;
        }
        v = v * 10 + d;
        cur = next;
        count++;
    }
    if (count < min_digits) {
        return 0;
    }
    *out_v = v;
    *out_next = cur;
    if (out_digits) {
        *out_digits = count;
    }
    return count > 0;
}

static robo_i18n_cal_name_width_t width_from_count(uint8_t count)
{
    if (count >= 5) {
        return ROBO_I18N_CAL_NAME_NARROW;
    }
    if (count >= 4) {
        return ROBO_I18N_CAL_NAME_WIDE;
    }
    return ROBO_I18N_CAL_NAME_ABBREVIATED;
}

static int match_name_longest(const char *text,
                              size_t len,
                              size_t at,
                              const char *const *names,
                              size_t name_count,
                              size_t *out_idx,
                              size_t *out_next)
{
    size_t best_len = 0;
    size_t best_i = 0;
    size_t i;
    int found = 0;
    for (i = 0; i < name_count; i++) {
        size_t n;
        if (!names[i] || !names[i][0]) {
            continue;
        }
        n = strlen(names[i]);
        if (n == 0 || at + n > len) {
            continue;
        }
        if (memcmp(text + at, names[i], n) == 0 && n > best_len) {
            best_len = n;
            best_i = i;
            found = 1;
        }
    }
    if (!found) {
        return 0;
    }
    *out_idx = best_i;
    *out_next = at + best_len;
    return 1;
}

static int match_month_name(const parse_state_t *st,
                            uint8_t count,
                            int permissive,
                            int32_t *out_month,
                            size_t *out_next)
{
    const robo_i18n_datetime_formatter_t *fmt = &st->parser->fmt;
    robo_i18n_cal_name_width_t widths[3];
    int nw = 0;
    int wi;
    widths[nw++] = width_from_count(count);
    if (permissive) {
        widths[nw++] = ROBO_I18N_CAL_NAME_WIDE;
        widths[nw++] = ROBO_I18N_CAL_NAME_ABBREVIATED;
    }
    for (wi = 0; wi < nw; wi++) {
        const char *names[12];
        uint8_t m;
        size_t idx = 0;
        size_t next = st->at;
        for (m = 1; m <= 12; m++) {
            const char *nm = NULL;
            if (robo_i18n_calendar_month_name(&fmt->locale, &fmt->calendar, m, widths[wi], &nm) !=
                ROBO_OK) {
                names[m - 1] = "";
            } else {
                names[m - 1] = nm;
            }
        }
        if (match_name_longest(st->text, st->len, st->at, names, 12, &idx, &next)) {
            *out_month = (int32_t)(idx + 1u);
            *out_next = next;
            return 1;
        }
    }
    return 0;
}

static int match_weekday_name(const parse_state_t *st,
                              uint8_t count,
                              int permissive,
                              int32_t *out_iso,
                              size_t *out_next)
{
    const robo_i18n_datetime_formatter_t *fmt = &st->parser->fmt;
    robo_i18n_cal_name_width_t widths[3];
    int nw = 0;
    int wi;
    /* ISO order Mon..Sun for match index → weekday */
    static const robo_i18n_weekday_t k_iso[7] = {
        ROBO_I18N_WEEKDAY_MON, ROBO_I18N_WEEKDAY_TUE, ROBO_I18N_WEEKDAY_WED,
        ROBO_I18N_WEEKDAY_THU, ROBO_I18N_WEEKDAY_FRI, ROBO_I18N_WEEKDAY_SAT,
        ROBO_I18N_WEEKDAY_SUN};
    widths[nw++] = width_from_count(count);
    if (permissive) {
        widths[nw++] = ROBO_I18N_CAL_NAME_WIDE;
        widths[nw++] = ROBO_I18N_CAL_NAME_ABBREVIATED;
    }
    for (wi = 0; wi < nw; wi++) {
        const char *names[7];
        int i;
        size_t idx = 0;
        size_t next = st->at;
        for (i = 0; i < 7; i++) {
            const char *nm = NULL;
            if (robo_i18n_calendar_weekday_name(&fmt->locale, &fmt->calendar, k_iso[i], widths[wi],
                                                &nm) != ROBO_OK) {
                names[i] = "";
            } else {
                names[i] = nm;
            }
        }
        if (match_name_longest(st->text, st->len, st->at, names, 7, &idx, &next)) {
            *out_iso = (int32_t)k_iso[idx];
            *out_next = next;
            return 1;
        }
    }
    return 0;
}

static int match_era_name(const parse_state_t *st, int32_t *out_era, size_t *out_next)
{
    const robo_i18n_datetime_formatter_t *fmt = &st->parser->fmt;
    size_t best_len = 0;
    int32_t best_era = 0;
    int found = 0;
    size_t i;

    for (i = 0; i < robo_cal_era_names_count; i++) {
        const robo_cal_era_name_t *row = &robo_cal_era_names[i];
        const char *cal = robo_cal_str(row->calendar);
        const char *name;
        size_t n;
        if (!cal || strcmp(cal, fmt->calendar.id) != 0) {
            continue;
        }
        name = robo_cal_str(row->name);
        if (!name || !name[0]) {
            continue;
        }
        n = strlen(name);
        if (st->at + n <= st->len && memcmp(st->text + st->at, name, n) == 0 && n > best_len) {
            best_len = n;
            best_era = row->era_index;
            found = 1;
        }
    }
    /* Also try locale API for gregorian AD/BC indices 0/1. */
    if (!found) {
        int32_t ei;
        for (ei = 0; ei < 32; ei++) {
            const char *nm = NULL;
            size_t n;
            if (robo_i18n_calendar_era_name(&fmt->locale, &fmt->calendar, ei,
                                            ROBO_I18N_CAL_NAME_ABBREVIATED, &nm) != ROBO_OK) {
                if (ei > 4) {
                    break;
                }
                continue;
            }
            n = strlen(nm);
            if (st->at + n <= st->len && memcmp(st->text + st->at, nm, n) == 0 && n > best_len) {
                best_len = n;
                best_era = ei;
                found = 1;
            }
        }
    }
    if (!found) {
        return 0;
    }
    *out_era = best_era;
    *out_next = st->at + best_len;
    return 1;
}

static int match_day_period(const parse_state_t *st, uint8_t count, int *is_pm, size_t *out_next)
{
    const robo_i18n_datetime_formatter_t *fmt = &st->parser->fmt;
    robo_i18n_cal_name_width_t width = width_from_count(count);
    const char *am = NULL;
    const char *pm = NULL;
    size_t am_len = 0;
    size_t pm_len = 0;
    (void)robo_i18n_calendar_day_period_name(&fmt->locale, &fmt->calendar, "am", width, &am);
    (void)robo_i18n_calendar_day_period_name(&fmt->locale, &fmt->calendar, "pm", width, &pm);
    if (am) {
        am_len = strlen(am);
    }
    if (pm) {
        pm_len = strlen(pm);
    }
    /* Longest match wins when both could prefix. */
    if (pm_len > 0 && st->at + pm_len <= st->len &&
        memcmp(st->text + st->at, pm, pm_len) == 0 &&
        (am_len == 0 || pm_len >= am_len ||
         !(st->at + am_len <= st->len && memcmp(st->text + st->at, am, am_len) == 0))) {
        if (!(am_len > pm_len && st->at + am_len <= st->len &&
              memcmp(st->text + st->at, am, am_len) == 0)) {
            *is_pm = 1;
            *out_next = st->at + pm_len;
            return 1;
        }
    }
    if (am_len > 0 && st->at + am_len <= st->len && memcmp(st->text + st->at, am, am_len) == 0) {
        if (pm_len > am_len && st->at + pm_len <= st->len &&
            memcmp(st->text + st->at, pm, pm_len) == 0) {
            *is_pm = 1;
            *out_next = st->at + pm_len;
            return 1;
        }
        *is_pm = 0;
        *out_next = st->at + am_len;
        return 1;
    }
    if (pm_len > 0 && st->at + pm_len <= st->len && memcmp(st->text + st->at, pm, pm_len) == 0) {
        *is_pm = 1;
        *out_next = st->at + pm_len;
        return 1;
    }
    /* ASCII fallback */
    if (match_ascii_ci(st->text, st->len, st->at, "PM", out_next) ||
        match_ascii_ci(st->text, st->len, st->at, "P.M.", out_next)) {
        *is_pm = 1;
        return 1;
    }
    if (match_ascii_ci(st->text, st->len, st->at, "AM", out_next) ||
        match_ascii_ci(st->text, st->len, st->at, "A.M.", out_next)) {
        *is_pm = 0;
        return 1;
    }
    return 0;
}

static int ascii_digit(char c)
{
    return c >= '0' && c <= '9' ? (c - '0') : -1;
}

static int parse_offset_ascii(const char *text,
                              size_t len,
                              size_t at,
                              int allow_z,
                              int32_t *out_off,
                              size_t *out_next)
{
    size_t cur = at;
    int sign = 1;
    int hours = 0;
    int mins = 0;
    int secs = 0;
    int d;

    if (allow_z && cur < len && (text[cur] == 'Z' || text[cur] == 'z')) {
        *out_off = 0;
        *out_next = cur + 1u;
        return 1;
    }
    if (cur >= len) {
        return 0;
    }
    if (text[cur] == '+') {
        sign = 1;
        cur++;
    } else if (text[cur] == '-') {
        sign = -1;
        cur++;
    } else {
        return 0;
    }
    if (cur + 2u > len) {
        return 0;
    }
    d = ascii_digit(text[cur]);
    if (d < 0) {
        return 0;
    }
    hours = d * 10;
    d = ascii_digit(text[cur + 1u]);
    if (d < 0) {
        return 0;
    }
    hours += d;
    cur += 2u;
    if (cur < len && text[cur] == ':') {
        cur++;
        if (cur + 2u > len) {
            return 0;
        }
        d = ascii_digit(text[cur]);
        if (d < 0) {
            return 0;
        }
        mins = d * 10;
        d = ascii_digit(text[cur + 1u]);
        if (d < 0) {
            return 0;
        }
        mins += d;
        cur += 2u;
        if (cur < len && text[cur] == ':') {
            cur++;
            if (cur + 2u > len) {
                return 0;
            }
            d = ascii_digit(text[cur]);
            if (d < 0) {
                return 0;
            }
            secs = d * 10;
            d = ascii_digit(text[cur + 1u]);
            if (d < 0) {
                return 0;
            }
            secs += d;
            cur += 2u;
        }
    } else if (cur + 2u <= len && ascii_digit(text[cur]) >= 0) {
        d = ascii_digit(text[cur]);
        mins = d * 10;
        d = ascii_digit(text[cur + 1u]);
        if (d < 0) {
            return 0;
        }
        mins += d;
        cur += 2u;
        if (cur + 2u <= len && ascii_digit(text[cur]) >= 0) {
            d = ascii_digit(text[cur]);
            secs = d * 10;
            d = ascii_digit(text[cur + 1u]);
            if (d < 0) {
                return 0;
            }
            secs += d;
            cur += 2u;
        }
    }
    if (hours > 18 || mins > 59 || secs > 59) {
        return 0;
    }
    *out_off = sign * (hours * 3600 + mins * 60 + secs);
    *out_next = cur;
    return 1;
}

static int parse_gmt_or_offset(const parse_state_t *st, int32_t *out_off, size_t *out_next)
{
    size_t cur = st->at;
    size_t next = cur;

    if (match_ascii_ci(st->text, st->len, cur, "GMT", &next) ||
        match_ascii_ci(st->text, st->len, cur, "UTC", &next)) {
        cur = next;
        if (cur >= st->len || (st->text[cur] != '+' && st->text[cur] != '-')) {
            *out_off = 0;
            *out_next = cur;
            return 1;
        }
        return parse_offset_ascii(st->text, st->len, cur, 0, out_off, out_next);
    }
    return parse_offset_ascii(st->text, st->len, cur, 1, out_off, out_next);
}

static int parse_tz_id(const parse_state_t *st, char *out_id, size_t *out_next)
{
    size_t cur = st->at;
    size_t start = cur;
    size_t n;
    robo_i18n_timezone_t tz;

    /* IANA-ish: Area/Location with ASCII letters, digits, _, -, / */
    while (cur < st->len) {
        unsigned char c = (unsigned char)st->text[cur];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '/' || c == '+') {
            cur++;
            continue;
        }
        break;
    }
    n = cur - start;
    if (n == 0 || n > ROBO_I18N_TZ_ID_MAX) {
        return 0;
    }
    if (robo_i18n_timezone_from_id(st->text + start, n, &tz) != ROBO_OK) {
        return 0;
    }
    memcpy(out_id, tz.id, strlen(tz.id) + 1u);
    *out_next = cur;
    return 1;
}

static robo_result_t fail(robo_i18n_dtparse_result_t *out,
                          robo_i18n_dtparse_err_t err,
                          size_t off,
                          size_t consumed)
{
    out->error = err;
    out->error_offset = off;
    out->consumed = consumed;
    return ROBO_PARSE_ERROR;
}

static int match_literal(parse_state_t *st, const robo_i18n_dtfmt_token_t *tok, int permissive)
{
    const char *lit = st->parser->fmt.pattern + tok->lit_off;
    size_t lit_len = tok->lit_len;
    size_t i;

    if (lit_len == 0) {
        return 1;
    }
    if (permissive) {
        /* Allow Unicode whitespace where the literal is only whitespace. */
        int lit_ws = 1;
        size_t li = 0;
        while (li < lit_len) {
            robo_uchar32_t cp = 0;
            size_t n = li;
            if (robo_unicode_utf8_next(lit, lit_len, li, ROBO_UNICODE_STRICT, &cp, &n) !=
                ROBO_UNICODE_OK) {
                lit_ws = 0;
                break;
            }
            if (!robo_unicode_is_whitespace(cp)) {
                lit_ws = 0;
                break;
            }
            li = n;
        }
        if (lit_ws) {
            size_t next = skip_ws(st->text, st->len, st->at);
            if (next == st->at && lit_len > 0) {
                /* Require at least some ws if literal had ws — permissive
                 * allows zero-width when optional separators appear. */
            }
            st->at = next;
            return 1;
        }
    }
    if (!prefix_eq(st->text, st->len, st->at, lit, lit_len)) {
        return 0;
    }
    /* In permissive mode, also accept ASCII space for NBSP-like separators
     * of length 1 non-letter — handled above for pure-ws literals only. */
    (void)i;
    st->at += lit_len;
    return 1;
}

static robo_result_t parse_field(parse_state_t *st,
                                 char field,
                                 uint8_t count,
                                 robo_i18n_dtparse_result_t *out)
{
    int permissive = st->parser->options.mode == ROBO_I18N_PARSE_PERMISSIVE;
    int overflow = 0;
    int64_t v = 0;
    size_t next = st->at;
    int min_d;
    int max_d;

    switch (field) {
    case 'G':
        if (!match_era_name(st, &st->era_index, &next)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_ERA, st->at, st->at);
        }
        st->have_era = 1;
        st->at = next;
        return ROBO_OK;

    case 'y':
    case 'Y':
    case 'u':
        if (count == 2) {
            min_d = 2;
            max_d = permissive ? 4 : 2;
        } else {
            min_d = permissive ? 1 : (count >= 4 ? 4 : count);
            if (min_d < 1) {
                min_d = 1;
            }
            max_d = count >= 4 ? 10 : (count > 4 ? count : 10);
            if (max_d < min_d) {
                max_d = min_d;
            }
        }
        {
            int digits = 0;
            if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next,
                           &overflow, &digits)) {
                return fail(out,
                            overflow ? ROBO_I18N_DTPARSE_ERR_OVERFLOW
                                     : ROBO_I18N_DTPARSE_ERR_INVALID_FIELD,
                            st->at, st->at);
            }
            if (count == 2 && digits == 2) {
                /* Century windows are calendar-aware and deterministic
                 * (no wall-clock pivot). */
                int yy = (int)v;
                if (st->parser->fmt.calendar.kind == ROBO_I18N_CAL_BUDDHIST) {
                    /* BE years 2500–2599 for yy 00–99 (covers contemporary Thai). */
                    st->year = 2500 + yy;
                } else {
                    /* Gregorian-style: 1970–2069. */
                    st->year = (yy >= 70) ? (1900 + yy) : (2000 + yy);
                }
            } else {
                if (v < ROBO_I18N_DATE_YEAR_MIN || v > ROBO_I18N_DATE_YEAR_MAX) {
                    return fail(out, ROBO_I18N_DTPARSE_ERR_OVERFLOW, st->at, st->at);
                }
                st->year = (int32_t)v;
            }
        }
        st->have_year = 1;
        st->at = next;
        return ROBO_OK;

    case 'M':
    case 'L':
        if (count >= 3) {
            int32_t month = 0;
            if (!match_month_name(st, count, permissive, &month, &next)) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_MONTH_NAME, st->at, st->at);
            }
            st->month = month;
            st->have_month = 1;
            st->at = next;
            return ROBO_OK;
        }
        min_d = permissive ? 1 : count;
        max_d = 2;
        if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next, &overflow,
                       NULL) ||
            v < 1 || v > 12) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_FIELD, st->at, st->at);
        }
        st->month = (int32_t)v;
        st->have_month = 1;
        st->at = next;
        return ROBO_OK;

    case 'd':
        min_d = permissive ? 1 : (count >= 2 ? 2 : 1);
        max_d = 2;
        if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next, &overflow,
                       NULL) ||
            v < 1 || v > 31) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_FIELD, st->at, st->at);
        }
        st->day = (int32_t)v;
        st->have_day = 1;
        st->at = next;
        return ROBO_OK;

    case 'E':
    case 'e':
    case 'c':
        if ((field == 'e' || field == 'c') && count <= 2) {
            min_d = permissive ? 1 : count;
            max_d = 1;
            if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next,
                           &overflow, NULL) ||
                v < 1 || v > 7) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_FIELD, st->at, st->at);
            }
            st->weekday = (int32_t)v;
            st->have_weekday = 1;
            st->at = next;
            return ROBO_OK;
        }
        {
            int32_t wd = 0;
            if (!match_weekday_name(st, count, permissive, &wd, &next)) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_WEEKDAY_NAME, st->at, st->at);
            }
            st->weekday = wd;
            st->have_weekday = 1;
            st->at = next;
            return ROBO_OK;
        }

    case 'a':
    case 'b':
    case 'B': {
        int is_pm = 0;
        if (!match_day_period(st, count, &is_pm, &next)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_DAY_PERIOD, st->at, st->at);
        }
        st->have_ampm = 1;
        st->ampm_is_pm = is_pm;
        st->at = next;
        return ROBO_OK;
    }

    case 'h':
    case 'H':
    case 'K':
    case 'k':
        min_d = permissive ? 1 : (count >= 2 ? 2 : 1);
        max_d = 2;
        if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next, &overflow,
                       NULL)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_FIELD, st->at, st->at);
        }
        if (field == 'h' && (v < 1 || v > 12)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        if (field == 'K' && (v < 0 || v > 11)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        if (field == 'H' && (v < 0 || v > 23)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        if (field == 'k' && (v < 1 || v > 24)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        st->hour_style = (int)field;
        st->hour_raw = (int32_t)v;
        st->have_hour = 1;
        st->at = next;
        return ROBO_OK;

    case 'm':
        min_d = permissive ? 1 : (count >= 2 ? 2 : 1);
        max_d = 2;
        if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next, &overflow,
                       NULL) ||
            v < 0 || v > 59) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        st->minute = (int32_t)v;
        st->have_minute = 1;
        st->at = next;
        return ROBO_OK;

    case 's':
        min_d = permissive ? 1 : (count >= 2 ? 2 : 1);
        max_d = 2;
        if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next, &overflow,
                       NULL) ||
            v < 0 || v > 59) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        st->second = (int32_t)v;
        st->have_second = 1;
        st->at = next;
        return ROBO_OK;

    case 'S': {
        int digits = 0;
        uint8_t want = count > 9 ? 9 : count;
        min_d = permissive ? 1 : (int)want;
        max_d = (int)want;
        if (!read_uint(st->parser, st->text, st->len, st->at, min_d, max_d, &v, &next, &overflow,
                       &digits)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        {
            uint32_t nano = 0;
            int i;
            char tmp[16];
            int dn = digits;
            int64_t t = v;
            if (dn > 9) {
                dn = 9;
            }
            for (i = dn - 1; i >= 0; i--) {
                tmp[i] = (char)('0' + (t % 10));
                t /= 10;
            }
            for (i = 0; i < 9; i++) {
                int dig = (i < dn) ? (tmp[i] - '0') : 0;
                nano = nano * 10u + (uint32_t)dig;
            }
            st->nano = nano;
            st->have_nano = 1;
            st->at = next;
            return ROBO_OK;
        }
    }

    case 'z':
    case 'v':
    case 'O':
        if (parse_gmt_or_offset(st, &st->offset_seconds, &next)) {
            st->have_offset = 1;
            st->at = next;
            return ROBO_OK;
        }
        if (parse_tz_id(st, st->tz_id, &next)) {
            st->have_tz_id = 1;
            st->at = next;
            return ROBO_OK;
        }
        /* Localized zone display names are not reverse-mapped unless they
         * coincide with an IANA id / GMT / offset — avoid silent guessing. */
        return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIMEZONE, st->at, st->at);

    case 'V':
        if (parse_tz_id(st, st->tz_id, &next)) {
            st->have_tz_id = 1;
            st->at = next;
            return ROBO_OK;
        }
        if (parse_gmt_or_offset(st, &st->offset_seconds, &next)) {
            st->have_offset = 1;
            st->at = next;
            return ROBO_OK;
        }
        return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIMEZONE, st->at, st->at);

    case 'Z':
    case 'X':
    case 'x':
        if (!parse_offset_ascii(st->text, st->len, st->at, field == 'X' || field == 'x',
                                &st->offset_seconds, &next)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIMEZONE, st->at, st->at);
        }
        st->have_offset = 1;
        st->at = next;
        return ROBO_OK;

    default:
        /* Unsupported letter: match literal letter×count like formatter emits. */
        {
            char tmp[16];
            uint8_t i;
            uint8_t n = count > 15 ? 15 : count;
            for (i = 0; i < n; i++) {
                tmp[i] = field;
            }
            if (!prefix_eq(st->text, st->len, st->at, tmp, n)) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_UNEXPECTED_CHAR, st->at, st->at);
            }
            st->at += n;
            return ROBO_OK;
        }
    }
}

static uint8_t hour_to_24(parse_state_t *st)
{
    int32_t h = st->hour_raw;
    if (!st->have_hour) {
        return 0;
    }
    if (st->hour_style == 'H') {
        return (uint8_t)h;
    }
    if (st->hour_style == 'k') {
        return (uint8_t)(h == 24 ? 0 : h);
    }
    if (st->hour_style == 'K') {
        if (st->have_ampm && st->ampm_is_pm) {
            return (uint8_t)(h + 12);
        }
        return (uint8_t)h;
    }
    /* 'h' 1..12 */
    if (st->have_ampm) {
        if (st->ampm_is_pm) {
            return (uint8_t)(h == 12 ? 12 : h + 12);
        }
        return (uint8_t)(h == 12 ? 0 : h);
    }
    /* No day period: treat as 1..12 morning (0..11) for h without am/pm —
     * ambiguous; leave as hour%12 with 12→0 only if permissive. */
    if (st->parser->options.mode == ROBO_I18N_PARSE_PERMISSIVE) {
        return (uint8_t)(h == 12 ? 0 : h);
    }
    return (uint8_t)(h == 12 ? 0 : h);
}

static robo_i18n_tz_disambiguation_t map_disambig(robo_i18n_dtparse_disambig_t d)
{
    switch (d) {
    case ROBO_I18N_DTPARSE_DISAMBIG_EARLIER:
        return ROBO_I18N_TZ_DISAMBIG_EARLIER;
    case ROBO_I18N_DTPARSE_DISAMBIG_LATER:
        return ROBO_I18N_TZ_DISAMBIG_LATER;
    case ROBO_I18N_DTPARSE_DISAMBIG_COMPATIBLE:
        return ROBO_I18N_TZ_DISAMBIG_EARLIER;
    case ROBO_I18N_DTPARSE_DISAMBIG_REJECT:
    default:
        return ROBO_I18N_TZ_DISAMBIG_REJECT;
    }
}

static robo_result_t finalize_parse(parse_state_t *st,
                                    robo_i18n_dtparse_result_t *out,
                                    int require_zone)
{
    const robo_i18n_datetime_parser_t *parser = st->parser;
    int permissive = parser->options.mode == ROBO_I18N_PARSE_PERMISSIVE;
    robo_result_t r;
    uint8_t hour24 = 0;

    out->have_era = st->have_era ? 1u : 0u;
    out->have_day_period = st->have_ampm ? 1u : 0u;
    out->day_period_is_pm = st->ampm_is_pm ? 1u : 0u;
    out->fields.year = st->have_year ? st->year : 0;
    out->fields.month = st->have_month ? (uint8_t)st->month : 0;
    out->fields.day = st->have_day ? (uint8_t)st->day : 0;
    out->fields.era_index = st->have_era ? st->era_index : 0;

    if (st->have_year && st->have_month && st->have_day) {
        if (!robo_i18n_calendar_has_conversion(&parser->fmt.calendar)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_UNSUPPORTED_CALENDAR, st->at, st->at);
        }
        r = robo_i18n_calendar_to_gregorian(&parser->fmt.calendar, &out->fields,
                                            &out->gregorian_date);
        if (r != ROBO_OK) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_DATE, st->at, st->at);
        }
        if (!robo_i18n_date_is_valid(&out->gregorian_date)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_DATE, st->at, st->at);
        }
        out->have_date = 1;
        out->local.date = out->gregorian_date;

        if (st->have_weekday && !permissive) {
            robo_i18n_weekday_t wd;
            if (robo_i18n_date_day_of_week(&out->gregorian_date, &wd) == ROBO_OK &&
                (int32_t)wd != st->weekday) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_DATE, st->at, st->at);
            }
        }
    }
    /* Partial date fields remain in out->fields; have_date stays 0. */

    if (st->have_hour || st->have_minute || st->have_second || st->have_nano || st->have_ampm) {
        if (st->have_hour && st->hour_style == 'h' && !st->have_ampm &&
            parser->options.mode == ROBO_I18N_PARSE_STRICT) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_DAY_PERIOD, st->at, st->at);
        }
        hour24 = hour_to_24(st);
        r = robo_i18n_time_init(&out->time, hour24, (uint8_t)(st->have_minute ? st->minute : 0),
                                (uint8_t)(st->have_second ? st->second : 0),
                                st->have_nano ? st->nano : 0u);
        if (r != ROBO_OK || !robo_i18n_time_is_valid(&out->time)) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIME, st->at, st->at);
        }
        out->have_time = 1;
        out->local.time = out->time;
    }

    if (out->have_date && out->have_time) {
        (void)robo_i18n_datetime_init(&out->local, &out->gregorian_date, &out->time);
    }

    if (st->have_offset) {
        out->have_offset = 1;
        out->offset_seconds = st->offset_seconds;
        out->zone_explicit = 1;
        {
            robo_i18n_timezone_t fixed;
            if (robo_i18n_timezone_fixed_seconds(st->offset_seconds, &fixed) == ROBO_OK) {
                size_t n = strlen(fixed.id);
                if (n > ROBO_I18N_TZ_ID_MAX) {
                    n = ROBO_I18N_TZ_ID_MAX;
                }
                memcpy(out->timezone_id, fixed.id, n);
                out->timezone_id[n] = '\0';
                out->have_timezone = 1;
            }
        }
    }
    if (st->have_tz_id) {
        out->have_timezone = 1;
        out->zone_explicit = 1;
        memcpy(out->timezone_id, st->tz_id, strlen(st->tz_id) + 1u);
    }

    if (require_zone && !out->zone_explicit &&
        !(parser->options.resolve_instant && parser->has_resolve_tz)) {
        return fail(out, ROBO_I18N_DTPARSE_ERR_MISSING_TIMEZONE, st->at, st->at);
    }

    if (parser->options.resolve_instant && out->have_date && out->have_time) {
        robo_i18n_timezone_t tz;
        robo_i18n_tz_resolve_t resolve = ROBO_I18N_TZ_UNIQUE;
        robo_i18n_tz_disambiguation_t pol = map_disambig(parser->options.disambiguation);

        if (out->zone_explicit && out->have_timezone) {
            r = robo_i18n_timezone_from_id(out->timezone_id, strlen(out->timezone_id), &tz);
            if (r != ROBO_OK) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIMEZONE, st->at, st->at);
            }
        } else if (parser->has_resolve_tz) {
            tz = parser->fmt.timezone;
        } else {
            return fail(out, ROBO_I18N_DTPARSE_ERR_MISSING_TIMEZONE, st->at, st->at);
        }

        r = robo_i18n_timezone_to_utc_ex(&tz, &out->local, pol, &out->instant, &resolve);
        out->tz_resolve = resolve;
        if (resolve == ROBO_I18N_TZ_NONEXISTENT) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_NONEXISTENT_LOCAL, st->at, st->at);
        }
        if (resolve == ROBO_I18N_TZ_AMBIGUOUS &&
            parser->options.disambiguation == ROBO_I18N_DTPARSE_DISAMBIG_REJECT) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_LOCAL, st->at, st->at);
        }
        if (r != ROBO_OK) {
            if (resolve == ROBO_I18N_TZ_AMBIGUOUS) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_LOCAL, st->at, st->at);
            }
            return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_TIMEZONE, st->at, st->at);
        }
        out->have_instant = 1;
    }

    out->error = ROBO_I18N_DTPARSE_ERR_NONE;
    out->error_offset = st->at;
    out->consumed = st->at;
    return ROBO_OK;
}

static robo_result_t parse_core(const robo_i18n_datetime_parser_t *parser,
                                const char *text,
                                size_t len,
                                int require_zone,
                                robo_i18n_dtparse_result_t *out)
{
    parse_state_t st;
    size_t i;
    size_t bad = 0;
    robo_result_t r;
    int permissive;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_i18n_dtparse_result_clear(out);
    if (!parser || !parser->ready || !text) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (len > ROBO_I18N_DTPARSE_INPUT_MAX) {
        out->error = ROBO_I18N_DTPARSE_ERR_INPUT_TOO_LONG;
        return ROBO_INVALID_ARGUMENT;
    }
    if (len == 0) {
        return fail(out, ROBO_I18N_DTPARSE_ERR_EMPTY, 0, 0);
    }
    if (!utf8_valid_span(text, len, &bad)) {
        return fail(out, ROBO_I18N_DTPARSE_ERR_INVALID_UTF8, bad, 0);
    }

    memset(&st, 0, sizeof(st));
    st.parser = parser;
    st.text = text;
    st.len = len;
    st.at = 0;
    permissive = parser->options.mode == ROBO_I18N_PARSE_PERMISSIVE;

    if (parser->options.allow_leading_ws) {
        st.at = skip_ws(text, len, st.at);
        if (st.at >= len) {
            return fail(out, ROBO_I18N_DTPARSE_ERR_EMPTY, st.at, st.at);
        }
    }

    for (i = 0; i < parser->fmt.token_count; i++) {
        const robo_i18n_dtfmt_token_t *tok = &parser->fmt.tokens[i];
        if (tok->is_literal) {
            if (!match_literal(&st, tok, permissive)) {
                return fail(out, ROBO_I18N_DTPARSE_ERR_UNEXPECTED_CHAR, st.at, st.at);
            }
        } else {
            r = parse_field(&st, (char)tok->field, tok->count, out);
            if (r != ROBO_OK) {
                return r;
            }
        }
    }

    if (parser->options.allow_trailing_ws) {
        st.at = skip_ws(text, len, st.at);
    }
    if (parser->options.require_full && st.at < len) {
        return fail(out, ROBO_I18N_DTPARSE_ERR_TRAILING, st.at, st.at);
    }

    return finalize_parse(&st, out, require_zone);
}

robo_result_t robo_i18n_parse_date(const robo_i18n_datetime_parser_t *parser,
                                   const char *text,
                                   size_t len,
                                   robo_i18n_dtparse_result_t *out)
{
    return parse_core(parser, text, len, 0, out);
}

robo_result_t robo_i18n_parse_time(const robo_i18n_datetime_parser_t *parser,
                                   const char *text,
                                   size_t len,
                                   robo_i18n_dtparse_result_t *out)
{
    return parse_core(parser, text, len, 0, out);
}

robo_result_t robo_i18n_parse_local_datetime(const robo_i18n_datetime_parser_t *parser,
                                             const char *text,
                                             size_t len,
                                             robo_i18n_dtparse_result_t *out)
{
    return parse_core(parser, text, len, 0, out);
}

robo_result_t robo_i18n_parse_zoned_datetime(const robo_i18n_datetime_parser_t *parser,
                                             const char *text,
                                             size_t len,
                                             robo_i18n_dtparse_result_t *out)
{
    return parse_core(parser, text, len, 1, out);
}
