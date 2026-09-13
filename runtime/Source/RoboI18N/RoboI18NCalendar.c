/*
 * Calendar abstraction + CLDR metadata lookup (Goal 4.5).
 */

#include "RoboI18N/RoboI18NCalendar.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboI18NLocaleData.h"

#include <string.h>

typedef struct cal_alias {
    const char *id;
    robo_i18n_calendar_kind_t kind;
} cal_alias_t;

static const cal_alias_t CAL_ALIASES[] = {
    {"gregorian", ROBO_I18N_CAL_GREGORIAN},
    {"gregory", ROBO_I18N_CAL_GREGORIAN},
    {"iso8601", ROBO_I18N_CAL_ISO8601},
    {"buddhist", ROBO_I18N_CAL_BUDDHIST},
    {"japanese", ROBO_I18N_CAL_JAPANESE},
    {"islamic", ROBO_I18N_CAL_ISLAMIC},
    {"islamic-civil", ROBO_I18N_CAL_ISLAMIC},
    {"islamic-umalqura", ROBO_I18N_CAL_ISLAMIC},
    {"islamic-tbla", ROBO_I18N_CAL_ISLAMIC},
    {"islamic-rgsa", ROBO_I18N_CAL_ISLAMIC},
    {"hebrew", ROBO_I18N_CAL_HEBREW},
    {"persian", ROBO_I18N_CAL_PERSIAN},
    {"chinese", ROBO_I18N_CAL_CHINESE},
};

static const char *kind_canonical_id(robo_i18n_calendar_kind_t kind)
{
    switch (kind) {
    case ROBO_I18N_CAL_GREGORIAN:
        return "gregorian";
    case ROBO_I18N_CAL_ISO8601:
        return "iso8601";
    case ROBO_I18N_CAL_BUDDHIST:
        return "buddhist";
    case ROBO_I18N_CAL_JAPANESE:
        return "japanese";
    case ROBO_I18N_CAL_ISLAMIC:
        return "islamic";
    case ROBO_I18N_CAL_HEBREW:
        return "hebrew";
    case ROBO_I18N_CAL_PERSIAN:
        return "persian";
    case ROBO_I18N_CAL_CHINESE:
        return "chinese";
    default:
        return "";
    }
}

static int eq_ci(const char *a, size_t alen, const char *b)
{
    size_t i;
    size_t blen = strlen(b);
    if (alen != blen) {
        return 0;
    }
    for (i = 0; i < alen; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

robo_result_t robo_i18n_calendar_from_id(const char *id,
                                         size_t len,
                                         robo_i18n_calendar_t *out)
{
    size_t i;
    if (!id || !out || len == 0 || len >= ROBO_I18N_CALENDAR_ID_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0; i < sizeof(CAL_ALIASES) / sizeof(CAL_ALIASES[0]); i++) {
        if (eq_ci(id, len, CAL_ALIASES[i].id)) {
            return robo_i18n_calendar_from_kind(CAL_ALIASES[i].kind, out);
        }
    }
    memset(out, 0, sizeof(*out));
    out->kind = ROBO_I18N_CAL_UNKNOWN;
    memcpy(out->id, id, len);
    out->id[len] = '\0';
    return ROBO_OK;
}

robo_result_t robo_i18n_calendar_from_kind(robo_i18n_calendar_kind_t kind,
                                           robo_i18n_calendar_t *out)
{
    const char *cid;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    cid = kind_canonical_id(kind);
    if (!cid[0]) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    memcpy(out->id, cid, strlen(cid) + 1u);
    return ROBO_OK;
}

const char *robo_i18n_calendar_id(const robo_i18n_calendar_t *cal)
{
    return cal ? cal->id : "";
}

robo_i18n_calendar_kind_t robo_i18n_calendar_kind(const robo_i18n_calendar_t *cal)
{
    return cal ? cal->kind : ROBO_I18N_CAL_UNKNOWN;
}

int robo_i18n_calendar_has_conversion(const robo_i18n_calendar_t *cal)
{
    if (!cal) {
        return 0;
    }
    switch (cal->kind) {
    case ROBO_I18N_CAL_GREGORIAN:
    case ROBO_I18N_CAL_ISO8601:
    case ROBO_I18N_CAL_BUDDHIST:
    case ROBO_I18N_CAL_JAPANESE:
        return 1;
    default:
        return 0;
    }
}

static int date_cmp_ymd(int32_t y1, uint8_t m1, uint8_t d1, int32_t y2, uint8_t m2, uint8_t d2)
{
    if (y1 != y2) {
        return y1 < y2 ? -1 : 1;
    }
    if (m1 != m2) {
        return m1 < m2 ? -1 : 1;
    }
    if (d1 != d2) {
        return d1 < d2 ? -1 : 1;
    }
    return 0;
}

static const robo_cal_era_start_t *japanese_era_for_gregorian(const robo_i18n_date_t *g)
{
    size_t i;
    const robo_cal_era_start_t *best = NULL;
    for (i = 0; i < robo_cal_japanese_era_start_count; i++) {
        const robo_cal_era_start_t *e = &robo_cal_japanese_era_start[i];
        if (date_cmp_ymd(g->year, g->month, g->day, e->start_year, e->start_month, e->start_day) >= 0) {
            best = e;
        } else {
            break; /* chronological table */
        }
    }
    return best;
}

robo_result_t robo_i18n_calendar_to_gregorian(const robo_i18n_calendar_t *cal,
                                              const robo_i18n_calendar_fields_t *fields,
                                              robo_i18n_date_t *out_gregorian)
{
    if (!cal || !fields || !out_gregorian) {
        return ROBO_INVALID_ARGUMENT;
    }
    switch (cal->kind) {
    case ROBO_I18N_CAL_GREGORIAN:
        return robo_i18n_date_init(out_gregorian, fields->year, fields->month, fields->day);
    case ROBO_I18N_CAL_ISO8601:
        /* Fields year/month/day are Gregorian; ISO week uses dedicated APIs. */
        return robo_i18n_date_init(out_gregorian, fields->year, fields->month, fields->day);
    case ROBO_I18N_CAL_BUDDHIST:
        /* BE year = Gregorian year + 543. */
        return robo_i18n_date_init(out_gregorian, fields->year - 543, fields->month, fields->day);
    case ROBO_I18N_CAL_JAPANESE: {
        const robo_cal_era_start_t *era = robo_cal_era_start_find(fields->era_index);
        int32_t gy;
        if (!era) {
            return ROBO_NOT_FOUND;
        }
        if (fields->year < 1) {
            return ROBO_INVALID_ARGUMENT;
        }
        gy = era->start_year + fields->year - 1;
        return robo_i18n_date_init(out_gregorian, gy, fields->month, fields->day);
    }
    case ROBO_I18N_CAL_ISLAMIC:
    case ROBO_I18N_CAL_HEBREW:
    case ROBO_I18N_CAL_PERSIAN:
    case ROBO_I18N_CAL_CHINESE:
        return ROBO_NOT_SUPPORTED;
    default:
        return ROBO_NOT_SUPPORTED;
    }
}

robo_result_t robo_i18n_calendar_from_gregorian(const robo_i18n_calendar_t *cal,
                                                const robo_i18n_date_t *gregorian,
                                                robo_i18n_calendar_fields_t *out_fields)
{
    if (!cal || !gregorian || !out_fields || !robo_i18n_date_is_valid(gregorian)) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out_fields, 0, sizeof(*out_fields));
    switch (cal->kind) {
    case ROBO_I18N_CAL_GREGORIAN:
    case ROBO_I18N_CAL_ISO8601:
        out_fields->year = gregorian->year;
        out_fields->month = gregorian->month;
        out_fields->day = gregorian->day;
        out_fields->era_index = 0;
        return ROBO_OK;
    case ROBO_I18N_CAL_BUDDHIST:
        out_fields->year = gregorian->year + 543;
        out_fields->month = gregorian->month;
        out_fields->day = gregorian->day;
        out_fields->era_index = 0;
        return ROBO_OK;
    case ROBO_I18N_CAL_JAPANESE: {
        const robo_cal_era_start_t *era = japanese_era_for_gregorian(gregorian);
        if (!era) {
            return ROBO_NOT_FOUND;
        }
        out_fields->era_index = era->era_index;
        out_fields->year = gregorian->year - era->start_year + 1;
        out_fields->month = gregorian->month;
        out_fields->day = gregorian->day;
        return ROBO_OK;
    }
    case ROBO_I18N_CAL_ISLAMIC:
    case ROBO_I18N_CAL_HEBREW:
    case ROBO_I18N_CAL_PERSIAN:
    case ROBO_I18N_CAL_CHINESE:
        return ROBO_NOT_SUPPORTED;
    default:
        return ROBO_NOT_SUPPORTED;
    }
}

static void region_for_locale(const robo_i18n_locale_t *loc, char *out, size_t cap)
{
    robo_i18n_locale_t maxed;
    if (!loc || !out || cap < 4) {
        return;
    }
    out[0] = '\0';
    if (loc->region[0]) {
        size_t n = strlen(loc->region);
        if (n >= cap) {
            n = cap - 1u;
        }
        memcpy(out, loc->region, n);
        out[n] = '\0';
        return;
    }
    if (robo_i18n_locale_maximize(loc, &maxed) == ROBO_OK && maxed.region[0]) {
        size_t n = strlen(maxed.region);
        if (n >= cap) {
            n = cap - 1u;
        }
        memcpy(out, maxed.region, n);
        out[n] = '\0';
    }
}

robo_result_t robo_i18n_locale_first_day_of_week(const robo_i18n_locale_t *loc,
                                                 robo_i18n_weekday_t *out)
{
    char region[8];
    uint8_t fd;
    if (!loc || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    region_for_locale(loc, region, sizeof(region));
    fd = robo_cal_week_first_day(region[0] ? region : "001");
    if (fd < 1 || fd > 7) {
        return ROBO_ERROR;
    }
    *out = (robo_i18n_weekday_t)fd;
    return ROBO_OK;
}

robo_result_t robo_i18n_locale_weekend(const robo_i18n_locale_t *loc,
                                       robo_i18n_weekday_t *out_start,
                                       robo_i18n_weekday_t *out_end)
{
    char region[8];
    uint8_t a, b;
    if (!loc || !out_start || !out_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    region_for_locale(loc, region, sizeof(region));
    a = robo_cal_week_weekend_start(region[0] ? region : "001");
    b = robo_cal_week_weekend_end(region[0] ? region : "001");
    if (a < 1 || a > 7 || b < 1 || b > 7) {
        return ROBO_ERROR;
    }
    *out_start = (robo_i18n_weekday_t)a;
    *out_end = (robo_i18n_weekday_t)b;
    return ROBO_OK;
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
    if (loc->script[0] && n + 5 < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->script, 4);
        n += 4;
        buf[n] = '\0';
    }
    if (loc->region[0] && n + 1 + strlen(loc->region) < cap) {
        buf[n++] = '-';
        memcpy(buf + n, loc->region, strlen(loc->region));
        n += strlen(loc->region);
        buf[n] = '\0';
    }
}

static const robo_cal_symbols_t *symbols_resolve(const robo_i18n_locale_t *loc,
                                                 const robo_i18n_calendar_t *cal)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[ROBO_I18N_TAG_MAX];
    const char *cid;
    const robo_cal_symbols_t *row;

    if (!loc || !cal) {
        return NULL;
    }
    cid = cal->id;
    if (cal->kind == ROBO_I18N_CAL_ISO8601) {
        cid = "gregorian"; /* ISO week calendar reuses Gregorian names/patterns */
    }
    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_cal_symbols_find(tag, cid);
        if (row) {
            return row;
        }
        if (cur.language[0] && strcmp(cur.language, "root") != 0) {
            char lang[ROBO_I18N_LANG_MAX + 1u];
            memcpy(lang, cur.language, sizeof(lang));
            row = robo_cal_symbols_find(lang, cid);
            if (row) {
                return row;
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
    return robo_cal_symbols_find("en", cid);
}

static size_t iso_to_day_storage(robo_i18n_weekday_t wd)
{
    /* Mon=1..Sat=6,Sun=7 → storage sun..sat index: iso % 7 */
    return (size_t)((unsigned)wd % 7u);
}

robo_result_t robo_i18n_calendar_month_name(const robo_i18n_locale_t *loc,
                                            const robo_i18n_calendar_t *cal,
                                            uint8_t month,
                                            robo_i18n_cal_name_width_t width,
                                            const char **out_utf8)
{
    const robo_cal_symbols_t *row;
    int32_t idx;
    if (!out_utf8 || month < 1 || month > 12) {
        return ROBO_INVALID_ARGUMENT;
    }
    row = symbols_resolve(loc, cal);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    if (width == ROBO_I18N_CAL_NAME_WIDE) {
        idx = row->months_wide[month - 1u];
    } else if (width == ROBO_I18N_CAL_NAME_ABBREVIATED) {
        idx = row->months_abbr[month - 1u];
    } else if (width == ROBO_I18N_CAL_NAME_NARROW) {
        idx = row->months_narrow[month - 1u];
    } else {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_utf8 = robo_cal_str(idx);
    return ROBO_OK;
}

robo_result_t robo_i18n_calendar_weekday_name(const robo_i18n_locale_t *loc,
                                              const robo_i18n_calendar_t *cal,
                                              robo_i18n_weekday_t weekday,
                                              robo_i18n_cal_name_width_t width,
                                              const char **out_utf8)
{
    const robo_cal_symbols_t *row;
    size_t si;
    int32_t idx;
    if (!out_utf8 || weekday < 1 || weekday > 7) {
        return ROBO_INVALID_ARGUMENT;
    }
    row = symbols_resolve(loc, cal);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    si = iso_to_day_storage(weekday);
    if (width == ROBO_I18N_CAL_NAME_WIDE) {
        idx = row->days_wide[si];
    } else if (width == ROBO_I18N_CAL_NAME_NARROW) {
        idx = row->days_narrow[si];
    } else {
        idx = row->days_abbr[si];
    }
    *out_utf8 = robo_cal_str(idx);
    return ROBO_OK;
}

robo_result_t robo_i18n_calendar_era_name(const robo_i18n_locale_t *loc,
                                          const robo_i18n_calendar_t *cal,
                                          int32_t era_index,
                                          robo_i18n_cal_name_width_t width,
                                          const char **out_utf8)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[ROBO_I18N_TAG_MAX];
    const robo_cal_era_name_t *row;
    (void)width; /* only abbr table generated in Goal 4.5 */

    if (!loc || !cal || !out_utf8) {
        return ROBO_INVALID_ARGUMENT;
    }
    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_cal_era_name_find(tag, cal->id, era_index);
        if (!row) {
            row = robo_cal_era_name_find(cur.language, cal->id, era_index);
        }
        if (row) {
            *out_utf8 = robo_cal_str(row->name);
            return ROBO_OK;
        }
        {
            robo_i18n_locale_t parent;
            if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
                break;
            }
            cur = parent;
        }
    }
    row = robo_cal_era_name_find("en", cal->id, era_index);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    *out_utf8 = robo_cal_str(row->name);
    return ROBO_OK;
}

robo_result_t robo_i18n_calendar_day_period_name(const robo_i18n_locale_t *loc,
                                                 const robo_i18n_calendar_t *cal,
                                                 const char *period_id,
                                                 robo_i18n_cal_name_width_t width,
                                                 const char **out_utf8)
{
    const robo_cal_symbols_t *row;
    (void)width;
    if (!period_id || !out_utf8) {
        return ROBO_INVALID_ARGUMENT;
    }
    row = symbols_resolve(loc, cal);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    if (strcmp(period_id, "am") == 0) {
        *out_utf8 = robo_cal_str(row->am);
        return ROBO_OK;
    }
    if (strcmp(period_id, "pm") == 0) {
        *out_utf8 = robo_cal_str(row->pm);
        return ROBO_OK;
    }
    return ROBO_NOT_FOUND;
}

static robo_result_t pattern_from_style(const robo_cal_symbols_t *row,
                                        robo_i18n_cal_format_style_t style,
                                        int which,
                                        const char **out_utf8)
{
    int32_t idx;
    if (!row || !out_utf8) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (which == 0) { /* date */
        switch (style) {
        case ROBO_I18N_CAL_STYLE_FULL:
            idx = row->date_full;
            break;
        case ROBO_I18N_CAL_STYLE_LONG:
            idx = row->date_long;
            break;
        case ROBO_I18N_CAL_STYLE_MEDIUM:
            idx = row->date_medium;
            break;
        case ROBO_I18N_CAL_STYLE_SHORT:
            idx = row->date_short;
            break;
        default:
            return ROBO_INVALID_ARGUMENT;
        }
    } else if (which == 1) { /* time */
        switch (style) {
        case ROBO_I18N_CAL_STYLE_FULL:
            idx = row->time_full;
            break;
        case ROBO_I18N_CAL_STYLE_LONG:
            idx = row->time_long;
            break;
        case ROBO_I18N_CAL_STYLE_MEDIUM:
            idx = row->time_medium;
            break;
        case ROBO_I18N_CAL_STYLE_SHORT:
            idx = row->time_short;
            break;
        default:
            return ROBO_INVALID_ARGUMENT;
        }
    } else {
        switch (style) {
        case ROBO_I18N_CAL_STYLE_FULL:
            idx = row->datetime_full;
            break;
        case ROBO_I18N_CAL_STYLE_LONG:
            idx = row->datetime_long;
            break;
        case ROBO_I18N_CAL_STYLE_MEDIUM:
            idx = row->datetime_medium;
            break;
        case ROBO_I18N_CAL_STYLE_SHORT:
            idx = row->datetime_short;
            break;
        default:
            return ROBO_INVALID_ARGUMENT;
        }
    }
    *out_utf8 = robo_cal_str(idx);
    return ROBO_OK;
}

robo_result_t robo_i18n_calendar_date_pattern(const robo_i18n_locale_t *loc,
                                              const robo_i18n_calendar_t *cal,
                                              robo_i18n_cal_format_style_t style,
                                              const char **out_utf8)
{
    const robo_cal_symbols_t *row = symbols_resolve(loc, cal);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    return pattern_from_style(row, style, 0, out_utf8);
}

robo_result_t robo_i18n_calendar_time_pattern(const robo_i18n_locale_t *loc,
                                              const robo_i18n_calendar_t *cal,
                                              robo_i18n_cal_format_style_t style,
                                              const char **out_utf8)
{
    const robo_cal_symbols_t *row = symbols_resolve(loc, cal);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    return pattern_from_style(row, style, 1, out_utf8);
}

robo_result_t robo_i18n_calendar_datetime_pattern(const robo_i18n_locale_t *loc,
                                                  const robo_i18n_calendar_t *cal,
                                                  robo_i18n_cal_format_style_t style,
                                                  const char **out_utf8)
{
    const robo_cal_symbols_t *row = symbols_resolve(loc, cal);
    if (!row) {
        return ROBO_NOT_FOUND;
    }
    return pattern_from_style(row, style, 2, out_utf8);
}
