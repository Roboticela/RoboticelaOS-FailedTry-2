/*
 * RoboI18N date/time interval formatter (Goal 4.7).
 *
 * Pattern selection: skeleton + greatest differing field letter → CLDR
 * intervalFormats row. Split pattern at the first repeated field letter
 * (ICU-style) so repeated fields collapse when CLDR encodes that.
 */

#include "RoboI18N/RoboI18NDateTimeInterval.h"

#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NLocaleData.h"

#include <string.h>

void robo_i18n_interval_options_init(robo_i18n_interval_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->normalize_reversed = 0;
}

static int datetime_cmp(const robo_i18n_datetime_t *a, const robo_i18n_datetime_t *b)
{
    int c = robo_i18n_date_compare(&a->date, &b->date);
    int64_t na, nb;
    if (c != 0) {
        return c;
    }
    if (robo_i18n_time_to_nanos_of_day(&a->time, &na) != ROBO_OK ||
        robo_i18n_time_to_nanos_of_day(&b->time, &nb) != ROBO_OK) {
        return 0;
    }
    if (na < nb) {
        return -1;
    }
    if (na > nb) {
        return 1;
    }
    return 0;
}

robo_result_t robo_i18n_datetime_interval_init(robo_i18n_datetime_interval_t *out,
                                               const robo_i18n_datetime_t *start,
                                               const robo_i18n_datetime_t *end,
                                               int normalize_reversed)
{
    if (!out || !start || !end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_i18n_date_is_valid(&start->date) || !robo_i18n_date_is_valid(&end->date) ||
        !robo_i18n_time_is_valid(&start->time) || !robo_i18n_time_is_valid(&end->time)) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->start = *start;
    out->end = *end;
    if (datetime_cmp(&out->start, &out->end) > 0) {
        if (!normalize_reversed) {
            return ROBO_INVALID_ARGUMENT;
        }
        {
            robo_i18n_datetime_t tmp = out->start;
            out->start = out->end;
            out->end = tmp;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_instant_interval_init(robo_i18n_instant_interval_t *out,
                                              const robo_i18n_instant_t *start,
                                              const robo_i18n_instant_t *end,
                                              int normalize_reversed)
{
    if (!out || !start || !end) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->start = *start;
    out->end = *end;
    if (robo_i18n_instant_compare(&out->start, &out->end) > 0) {
        if (!normalize_reversed) {
            return ROBO_INVALID_ARGUMENT;
        }
        {
            robo_i18n_instant_t tmp = out->start;
            out->start = out->end;
            out->end = tmp;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_interval_formatter_init(robo_i18n_interval_formatter_t *fmt,
                                                const robo_i18n_locale_t *locale,
                                                const robo_i18n_calendar_t *calendar,
                                                const robo_i18n_timezone_t *tz,
                                                const robo_i18n_interval_options_t *opts)
{
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
        robo_i18n_interval_options_init(&fmt->options);
    }
    fmt->ready = 1;
    return ROBO_OK;
}

void robo_i18n_interval_formatter_destroy(robo_i18n_interval_formatter_t *fmt)
{
    if (!fmt) {
        return;
    }
    memset(fmt, 0, sizeof(*fmt));
}

robo_result_t robo_i18n_interval_greatest_difference(const robo_i18n_datetime_t *start,
                                                     const robo_i18n_datetime_t *end,
                                                     char *out_letter,
                                                     size_t letter_cap)
{
    if (!start || !end || !out_letter || letter_cap < 2u) {
        return ROBO_INVALID_ARGUMENT;
    }
    out_letter[0] = '\0';
    if (start->date.year != end->date.year) {
        out_letter[0] = 'y';
        out_letter[1] = '\0';
        return ROBO_OK;
    }
    if (start->date.month != end->date.month) {
        out_letter[0] = 'M';
        out_letter[1] = '\0';
        return ROBO_OK;
    }
    if (start->date.day != end->date.day) {
        out_letter[0] = 'd';
        out_letter[1] = '\0';
        return ROBO_OK;
    }
    /* Same calendar day — time fields. Prefer 'a' when am/pm differs for 12h. */
    if ((start->time.hour < 12) != (end->time.hour < 12)) {
        out_letter[0] = 'a';
        out_letter[1] = '\0';
        return ROBO_OK;
    }
    if (start->time.hour != end->time.hour) {
        out_letter[0] = 'H';
        out_letter[1] = '\0';
        return ROBO_OK;
    }
    if (start->time.minute != end->time.minute) {
        out_letter[0] = 'm';
        out_letter[1] = '\0';
        return ROBO_OK;
    }
    if (start->time.second != end->time.second || start->time.nano != end->time.nano) {
        out_letter[0] = 's';
        out_letter[1] = '\0';
        return ROBO_OK;
    }
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

static const robo_cal_interval_format_t *interval_find_inherited(
    const robo_i18n_locale_t *loc,
    const char *calendar,
    const char *skeleton,
    const char *diff)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[64];
    const robo_cal_interval_format_t *row;

    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_cal_interval_format_find(tag, calendar, skeleton, diff);
        if (row) {
            return row;
        }
        row = robo_cal_interval_format_find(cur.language, calendar, skeleton, diff);
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
    return robo_cal_interval_format_find("en", calendar, skeleton, diff);
}

static const robo_cal_interval_fallback_t *fallback_find_inherited(
    const robo_i18n_locale_t *loc,
    const char *calendar)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[64];
    const robo_cal_interval_fallback_t *row;

    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_cal_interval_fallback_find(tag, calendar);
        if (row) {
            return row;
        }
        row = robo_cal_interval_fallback_find(cur.language, calendar);
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
    return robo_cal_interval_fallback_find("en", calendar);
}

static int is_pattern_letter(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * Split interval pattern at the first repeated pattern letter (second run).
 * Returns 1 on split, 0 if no repeat (caller should use fallback).
 */
static int split_interval_pattern(const char *pattern,
                                  char *left,
                                  size_t left_cap,
                                  char *right,
                                  size_t right_cap)
{
    size_t i = 0;
    size_t plen;
    uint8_t seen[128];
    size_t split = (size_t)-1;

    if (!pattern || !left || !right || left_cap == 0 || right_cap == 0) {
        return 0;
    }
    memset(seen, 0, sizeof(seen));
    plen = strlen(pattern);
    while (i < plen) {
        unsigned char c = (unsigned char)pattern[i];
        if (c == '\'') {
            i++;
            while (i < plen) {
                if (pattern[i] == '\'') {
                    if (i + 1u < plen && pattern[i + 1u] == '\'') {
                        i += 2u;
                        continue;
                    }
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (is_pattern_letter(c)) {
            size_t j = i + 1u;
            while (j < plen && (unsigned char)pattern[j] == c) {
                j++;
            }
            if (seen[c]) {
                split = i;
                break;
            }
            seen[c] = 1;
            i = j;
            continue;
        }
        i++;
    }
    if (split == (size_t)-1 || split >= plen || split >= left_cap) {
        return 0;
    }
    if (plen - split >= right_cap) {
        return 0;
    }
    memcpy(left, pattern, split);
    left[split] = '\0';
    memcpy(right, pattern + split, plen - split + 1u);
    return 1;
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

static robo_result_t format_one_pattern(const robo_i18n_interval_formatter_t *fmt,
                                        const char *pattern,
                                        const robo_i18n_datetime_t *dt,
                                        char *out,
                                        size_t out_cap,
                                        size_t *out_len)
{
    robo_i18n_datetime_formatter_t dfmt;
    robo_i18n_dtfmt_options_t dopts;
    robo_result_t r;

    robo_i18n_dtfmt_options_init(&dopts);
    if (fmt->options.numbering_system[0]) {
        size_t n = strlen(fmt->options.numbering_system);
        if (n < sizeof(dopts.numbering_system)) {
            memcpy(dopts.numbering_system, fmt->options.numbering_system, n + 1u);
        }
    }
    r = robo_i18n_datetime_formatter_init_pattern(&dfmt, &fmt->locale, &fmt->calendar,
                                                  &fmt->timezone, pattern, &dopts);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_datetime_format_datetime(&dfmt, dt, out, out_cap, out_len);
    robo_i18n_datetime_formatter_destroy(&dfmt);
    return r;
}

static const char *default_skeleton(const robo_i18n_datetime_interval_t *iv, char diff)
{
    (void)iv;
    if (diff == 'H' || diff == 'h' || diff == 'm' || diff == 's' || diff == 'a') {
        return "Hm";
    }
    if (diff == 'd' || diff == 'M' || diff == 'y' || diff == '\0') {
        return "yMMMd";
    }
    return "yMMMd";
}

static robo_result_t format_fallback(const robo_i18n_interval_formatter_t *fmt,
                                     const robo_i18n_datetime_interval_t *iv,
                                     const char *fallback_pat,
                                     const char *single_skel,
                                     char *buf,
                                     size_t buf_cap,
                                     size_t *out_len)
{
    char left[192];
    char right[192];
    size_t llen = 0;
    size_t rlen = 0;
    size_t pos = 0;
    size_t i;
    size_t fplen;
    int writing = (buf != NULL && buf_cap > 0);
    robo_result_t r;
    const char *skel = single_skel && single_skel[0] ? single_skel : "yMMMd";
    const char *avail = NULL;

    /* Resolve availableFormat for a full single-side format. */
    {
        robo_i18n_locale_t cur = fmt->locale;
        int guard = 0;
        char tag[64];
        const robo_cal_available_format_t *row = NULL;
        while (guard++ < 16) {
            format_locale_tag(&cur, tag, sizeof(tag));
            row = robo_cal_available_format_find(tag, fmt->calendar.id, skel);
            if (!row) {
                row = robo_cal_available_format_find(cur.language, fmt->calendar.id, skel);
            }
            if (row) {
                break;
            }
            {
                robo_i18n_locale_t parent;
                if (robo_i18n_locale_data_parent(&cur, &parent) != ROBO_OK) {
                    break;
                }
                cur = parent;
            }
        }
        if (!row) {
            row = robo_cal_available_format_find("en", fmt->calendar.id, skel);
        }
        if (row) {
            avail = robo_cal_str(row->pattern);
        } else {
            avail = "y-MM-dd HH:mm";
        }
    }

    r = format_one_pattern(fmt, avail, &iv->start, left, sizeof(left), &llen);
    if (r != ROBO_OK) {
        return r;
    }
    r = format_one_pattern(fmt, avail, &iv->end, right, sizeof(right), &rlen);
    if (r != ROBO_OK) {
        return r;
    }

    fplen = strlen(fallback_pat);
    for (i = 0; i < fplen;) {
        if (fallback_pat[i] == '{' && i + 2u < fplen && fallback_pat[i + 2u] == '}') {
            if (fallback_pat[i + 1u] == '0') {
                r = append_bytes(buf, buf_cap, &pos, left, llen, writing);
                if (r != ROBO_OK) {
                    *out_len = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
            if (fallback_pat[i + 1u] == '1') {
                r = append_bytes(buf, buf_cap, &pos, right, rlen, writing);
                if (r != ROBO_OK) {
                    *out_len = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
        }
        r = append_bytes(buf, buf_cap, &pos, fallback_pat + i, 1u, writing);
        if (r != ROBO_OK) {
            *out_len = pos;
            return r;
        }
        i++;
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

robo_result_t robo_i18n_datetime_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                                 const robo_i18n_datetime_interval_t *interval,
                                                 char *buf,
                                                 size_t buf_cap,
                                                 size_t *out_len)
{
    robo_i18n_datetime_interval_t iv;
    char diff[2];
    const char *skel;
    const robo_cal_interval_format_t *row;
    const char *ipat;
    char left[160];
    char right[160];
    char left_out[192];
    char right_out[192];
    size_t llen = 0;
    size_t rlen = 0;
    size_t pos = 0;
    int writing;
    robo_result_t r;
    int swap = 0;

    if (!fmt || !fmt->ready || !interval || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    writing = (buf != NULL && buf_cap > 0);

    r = robo_i18n_datetime_interval_init(&iv, &interval->start, &interval->end,
                                         fmt->options.normalize_reversed);
    if (r != ROBO_OK) {
        return r;
    }

    r = robo_i18n_interval_greatest_difference(&iv.start, &iv.end, diff, sizeof(diff));
    if (r != ROBO_OK) {
        return r;
    }

    /* Identical endpoints → single datetime format. */
    if (diff[0] == '\0') {
        robo_i18n_datetime_formatter_t dfmt;
        robo_i18n_dtfmt_options_t dopts;
        const char *use_skel =
            fmt->options.skeleton[0] ? fmt->options.skeleton : default_skeleton(&iv, '\0');
        robo_i18n_dtfmt_options_init(&dopts);
        if (fmt->options.numbering_system[0]) {
            size_t n = strlen(fmt->options.numbering_system);
            if (n < sizeof(dopts.numbering_system)) {
                memcpy(dopts.numbering_system, fmt->options.numbering_system, n + 1u);
            }
        }
        r = robo_i18n_datetime_formatter_init_skeleton(&dfmt, &fmt->locale, &fmt->calendar,
                                                       &fmt->timezone, use_skel, &dopts);
        if (r != ROBO_OK) {
            return r;
        }
        r = robo_i18n_datetime_format_datetime(&dfmt, &iv.start, buf, buf_cap, out_len);
        robo_i18n_datetime_formatter_destroy(&dfmt);
        return r;
    }

    skel = fmt->options.skeleton[0] ? fmt->options.skeleton : default_skeleton(&iv, diff[0]);
    /* Adjust letter for 12h skeletons preferring h/a. */
    if ((diff[0] == 'H' || diff[0] == 'h') && strchr(skel, 'h') && !strchr(skel, 'H')) {
        diff[0] = 'h';
    }

    row = interval_find_inherited(&fmt->locale, fmt->calendar.id, skel, diff);
    if (!row) {
        /* Try alternate hour letter. */
        if (diff[0] == 'H') {
            diff[0] = 'h';
            row = interval_find_inherited(&fmt->locale, fmt->calendar.id, skel, diff);
        } else if (diff[0] == 'h') {
            diff[0] = 'H';
            row = interval_find_inherited(&fmt->locale, fmt->calendar.id, skel, diff);
        }
    }

    if (!row) {
        const robo_cal_interval_fallback_t *fb =
            fallback_find_inherited(&fmt->locale, fmt->calendar.id);
        const char *fp = fb ? robo_cal_str(fb->pattern) : "{0} – {1}";
        return format_fallback(fmt, &iv, fp, skel, buf, buf_cap, out_len);
    }

    ipat = robo_cal_str(row->pattern);
    /* Strip earliestFirst:/latestFirst: prefixes. */
    if (strncmp(ipat, "earliestFirst:", 14) == 0) {
        ipat += 14;
    } else if (strncmp(ipat, "latestFirst:", 12) == 0) {
        ipat += 12;
        swap = 1;
    }

    if (!split_interval_pattern(ipat, left, sizeof(left), right, sizeof(right))) {
        const robo_cal_interval_fallback_t *fb =
            fallback_find_inherited(&fmt->locale, fmt->calendar.id);
        const char *fp = fb ? robo_cal_str(fb->pattern) : "{0} – {1}";
        return format_fallback(fmt, &iv, fp, skel, buf, buf_cap, out_len);
    }

    {
        const robo_i18n_datetime_t *a = swap ? &iv.end : &iv.start;
        const robo_i18n_datetime_t *b = swap ? &iv.start : &iv.end;
        r = format_one_pattern(fmt, left, a, left_out, sizeof(left_out), &llen);
        if (r != ROBO_OK) {
            return r;
        }
        r = format_one_pattern(fmt, right, b, right_out, sizeof(right_out), &rlen);
        if (r != ROBO_OK) {
            return r;
        }
    }

    r = append_bytes(buf, buf_cap, &pos, left_out, llen, writing);
    if (r != ROBO_OK) {
        *out_len = pos;
        return r;
    }
    r = append_bytes(buf, buf_cap, &pos, right_out, rlen, writing);
    if (r != ROBO_OK) {
        *out_len = pos;
        return r;
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

robo_result_t robo_i18n_date_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                             const robo_i18n_date_t *start,
                                             const robo_i18n_date_t *end,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len)
{
    robo_i18n_datetime_interval_t iv;
    memset(&iv, 0, sizeof(iv));
    if (!start || !end) {
        return ROBO_INVALID_ARGUMENT;
    }
    iv.start.date = *start;
    iv.end.date = *end;
    return robo_i18n_datetime_interval_format(fmt, &iv, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_time_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                             const robo_i18n_time_t *start,
                                             const robo_i18n_time_t *end,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len)
{
    robo_i18n_datetime_interval_t iv;
    robo_i18n_interval_formatter_t local;
    robo_i18n_interval_options_t opts;
    robo_result_t r;

    if (!fmt || !start || !end) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(&iv, 0, sizeof(iv));
    /* Anchor on a fixed date so only time fields differ. */
    (void)robo_i18n_date_init(&iv.start.date, 2000, 1, 1);
    (void)robo_i18n_date_init(&iv.end.date, 2000, 1, 1);
    iv.start.time = *start;
    iv.end.time = *end;

    opts = fmt->options;
    if (!opts.skeleton[0]) {
        memcpy(opts.skeleton, "Hm", 3);
    }
    local = *fmt;
    local.options = opts;
    r = robo_i18n_datetime_interval_format(&local, &iv, buf, buf_cap, out_len);
    return r;
}

robo_result_t robo_i18n_instant_interval_format(const robo_i18n_interval_formatter_t *fmt,
                                                const robo_i18n_instant_interval_t *interval,
                                                char *buf,
                                                size_t buf_cap,
                                                size_t *out_len)
{
    robo_i18n_datetime_interval_t iv;
    robo_i18n_tz_resolve_t rs = ROBO_I18N_TZ_UNIQUE;
    robo_i18n_tz_resolve_t re = ROBO_I18N_TZ_UNIQUE;
    robo_i18n_instant_interval_t ni;
    robo_result_t r;

    if (!fmt || !interval) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_instant_interval_init(&ni, &interval->start, &interval->end,
                                        fmt->options.normalize_reversed);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_timezone_to_local(&fmt->timezone, &ni.start, &iv.start, &rs);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_timezone_to_local(&fmt->timezone, &ni.end, &iv.end, &re);
    if (r != ROBO_OK) {
        return r;
    }
    if (rs != ROBO_I18N_TZ_UNIQUE || re != ROBO_I18N_TZ_UNIQUE) {
        return ROBO_NOT_SUPPORTED;
    }
    return robo_i18n_datetime_interval_format(fmt, &iv, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_datetime_interval_format_to_string(
    const robo_i18n_interval_formatter_t *fmt,
    const robo_i18n_datetime_interval_t *interval,
    robo_string_t *out)
{
    char stack[384];
    size_t len = 0;
    robo_result_t r;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_datetime_interval_format(fmt, interval, stack, sizeof(stack), &len);
    if (r == ROBO_OK) {
        return robo_string_append(out, stack, len);
    }
    return r;
}
