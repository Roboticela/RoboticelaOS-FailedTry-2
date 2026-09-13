/*
 * RoboI18N relative-time formatter (Goal 4.7).
 */

#include "RoboI18N/RoboI18NRelativeTime.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboRelData.h"

#include <string.h>

void robo_i18n_relative_options_init(robo_i18n_relative_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->width = ROBO_I18N_REL_WIDTH_LONG;
    opts->numeric = ROBO_I18N_REL_NUMERIC_AUTO;
}

robo_result_t robo_i18n_relative_time_init(robo_i18n_relative_time_t *out,
                                           int64_t value,
                                           robo_i18n_relative_unit_t unit)
{
    if (!out || (unsigned)unit > (unsigned)ROBO_I18N_REL_SECOND) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->value = value;
    out->unit = unit;
    return ROBO_OK;
}

robo_result_t robo_i18n_relative_formatter_init(robo_i18n_relative_formatter_t *fmt,
                                                const robo_i18n_locale_t *locale,
                                                const robo_i18n_relative_options_t *opts)
{
    robo_i18n_number_options_t nopts;
    robo_result_t r;

    if (!fmt || !locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(fmt, 0, sizeof(*fmt));
    fmt->locale = *locale;
    if (opts) {
        fmt->options = *opts;
    } else {
        robo_i18n_relative_options_init(&fmt->options);
    }
    if ((unsigned)fmt->options.width > (unsigned)ROBO_I18N_REL_WIDTH_NARROW) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_i18n_number_options_init(&nopts);
    nopts.use_grouping = 0;
    nopts.min_integer_digits = 1;
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
    fmt->ready = 1;
    return ROBO_OK;
}

void robo_i18n_relative_formatter_destroy(robo_i18n_relative_formatter_t *fmt)
{
    if (!fmt) {
        return;
    }
    robo_i18n_number_formatter_destroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
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

static const robo_rel_unit_t *rel_resolve(const robo_i18n_locale_t *loc,
                                          uint8_t unit,
                                          uint8_t width)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[64];
    const robo_rel_unit_t *row;

    if (!loc) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_rel_unit_find(tag, unit, width);
        if (row) {
            return row;
        }
        row = robo_rel_unit_find(cur.language, unit, width);
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
    return robo_rel_unit_find("en", unit, width);
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

static int64_t i64_abs(int64_t v)
{
    return v < 0 ? -v : v;
}

static const char *pattern_for_category(const int32_t pats[6],
                                        robo_i18n_plural_category_t cat)
{
    int32_t idx;
    if ((unsigned)cat > 5u) {
        cat = ROBO_I18N_PLURAL_OTHER;
    }
    idx = pats[(unsigned)cat];
    if (idx < 0) {
        idx = pats[ROBO_I18N_PLURAL_OTHER];
    }
    if (idx < 0) {
        return NULL;
    }
    return robo_rel_str(idx);
}

static robo_result_t expand_relative_pattern(const robo_i18n_relative_formatter_t *fmt,
                                             const char *pattern,
                                             int64_t abs_value,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && buf_cap > 0);
    robo_result_t r;

    if (!pattern || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 1u] == '0' &&
            pattern[i + 2u] == '}') {
            char num[48];
            size_t nlen = 0;
            r = robo_i18n_number_format_i64(&fmt->number, abs_value, num, sizeof(num), &nlen);
            if (r != ROBO_OK) {
                return r;
            }
            r = append_bytes(buf, buf_cap, &pos, num, nlen, writing);
            if (r != ROBO_OK) {
                *out_len = pos;
                return r;
            }
            i += 3u;
            continue;
        }
        r = append_bytes(buf, buf_cap, &pos, pattern + i, 1u, writing);
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

robo_result_t robo_i18n_relative_time_format(const robo_i18n_relative_formatter_t *fmt,
                                             const robo_i18n_relative_time_t *rel,
                                             char *buf,
                                             size_t buf_cap,
                                             size_t *out_len)
{
    const robo_rel_unit_t *row;
    int64_t abs_v;
    int past;
    const char *pattern = NULL;
    robo_i18n_plural_category_t cat = ROBO_I18N_PLURAL_OTHER;
    robo_result_t r;

    if (!fmt || !fmt->ready || !rel || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((unsigned)rel->unit > (unsigned)ROBO_I18N_REL_SECOND) {
        return ROBO_NOT_SUPPORTED;
    }

    row = rel_resolve(&fmt->locale, (uint8_t)rel->unit, (uint8_t)fmt->options.width);
    if (!row) {
        return ROBO_NOT_FOUND;
    }

    abs_v = i64_abs(rel->value);
    past = rel->value < 0;

    if (fmt->options.numeric == ROBO_I18N_REL_NUMERIC_AUTO && abs_v <= 2) {
        int32_t idx = -1;
        if (rel->value == -2) {
            idx = row->relative_m2;
        } else if (rel->value == -1) {
            idx = row->relative_m1;
        } else if (rel->value == 0) {
            idx = row->relative_0;
        } else if (rel->value == 1) {
            idx = row->relative_1;
        } else if (rel->value == 2) {
            idx = row->relative_2;
        }
        if (idx >= 0) {
            pattern = robo_rel_str(idx);
            return expand_relative_pattern(fmt, pattern, abs_v, buf, buf_cap, out_len);
        }
    }

    r = robo_i18n_plural_cardinal_i64(&fmt->locale, abs_v, &cat);
    if (r != ROBO_OK) {
        return r;
    }
    pattern = pattern_for_category(past ? row->past : row->future, cat);
    if (!pattern) {
        return ROBO_NOT_FOUND;
    }
    return expand_relative_pattern(fmt, pattern, abs_v, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_relative_time_format_unit(const robo_i18n_relative_formatter_t *fmt,
                                                  int64_t value,
                                                  robo_i18n_relative_unit_t unit,
                                                  char *buf,
                                                  size_t buf_cap,
                                                  size_t *out_len)
{
    robo_i18n_relative_time_t rel;
    robo_result_t r = robo_i18n_relative_time_init(&rel, value, unit);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_relative_time_format(fmt, &rel, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_relative_time_format_to_string(
    const robo_i18n_relative_formatter_t *fmt,
    const robo_i18n_relative_time_t *rel,
    robo_string_t *out)
{
    char stack[256];
    size_t len = 0;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_relative_time_format(fmt, rel, stack, sizeof(stack), &len);
    if (r == ROBO_OK) {
        return robo_string_append(out, stack, len);
    }
    return r;
}
