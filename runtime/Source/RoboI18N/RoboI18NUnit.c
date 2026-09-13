/*
 * RoboI18N unit formatter + conversion (Goal 4.19).
 */

#include "RoboI18N/RoboI18NUnit.h"

#include "RoboI18N/RoboI18NLocaleData.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboUnitData.h"

#include <string.h>

void robo_i18n_unit_options_init(robo_i18n_unit_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->width = ROBO_I18N_UNIT_WIDTH_LONG;
}

robo_result_t robo_i18n_unit_formatter_init(robo_i18n_unit_formatter_t *fmt,
                                            const robo_i18n_locale_t *locale,
                                            const robo_i18n_unit_options_t *opts)
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
        robo_i18n_unit_options_init(&fmt->options);
    }
    if ((unsigned)fmt->options.width > (unsigned)ROBO_I18N_UNIT_WIDTH_NARROW) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_i18n_number_options_init(&nopts);
    nopts.use_grouping = 1;
    nopts.min_integer_digits = 1;
    nopts.max_fraction_digits = -1;
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

void robo_i18n_unit_formatter_destroy(robo_i18n_unit_formatter_t *fmt)
{
    if (!fmt) {
        return;
    }
    robo_i18n_number_formatter_destroy(&fmt->number);
    memset(fmt, 0, sizeof(*fmt));
}

const char *robo_i18n_unit_cldr_key(robo_i18n_unit_id_t unit)
{
    return robo_unit_cldr_key((uint16_t)unit);
}

robo_result_t robo_i18n_unit_id_from_cldr_key(const char *key, robo_i18n_unit_id_t *out)
{
    size_t i;
    if (!key || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0; i < (size_t)ROBO_I18N_UNIT_COUNT; i++) {
        const char *k = robo_unit_cldr_keys[i];
        if (k && strcmp(k, key) == 0) {
            *out = (robo_i18n_unit_id_t)i;
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

robo_i18n_unit_quantity_t robo_i18n_unit_quantity(robo_i18n_unit_id_t unit)
{
    if ((unsigned)unit >= (unsigned)ROBO_I18N_UNIT_COUNT) {
        return ROBO_I18N_UNIT_Q_UNKNOWN;
    }
    if (unit <= ROBO_I18N_UNIT_LENGTH_INCH) {
        return ROBO_I18N_UNIT_Q_LENGTH;
    }
    if (unit <= ROBO_I18N_UNIT_MASS_OUNCE) {
        return ROBO_I18N_UNIT_Q_MASS;
    }
    if (unit <= ROBO_I18N_UNIT_AREA_ACRE) {
        return ROBO_I18N_UNIT_Q_AREA;
    }
    if (unit <= ROBO_I18N_UNIT_VOLUME_CUBIC_METER) {
        return ROBO_I18N_UNIT_Q_VOLUME;
    }
    if (unit <= ROBO_I18N_UNIT_DURATION_MILLISECOND) {
        return ROBO_I18N_UNIT_Q_DURATION;
    }
    if (unit <= ROBO_I18N_UNIT_SPEED_MILE_PER_HOUR) {
        return ROBO_I18N_UNIT_Q_SPEED;
    }
    if (unit <= ROBO_I18N_UNIT_TEMPERATURE_KELVIN) {
        return ROBO_I18N_UNIT_Q_TEMPERATURE;
    }
    if (unit <= ROBO_I18N_UNIT_DIGITAL_TERABYTE) {
        return ROBO_I18N_UNIT_Q_DIGITAL;
    }
    if (unit <= ROBO_I18N_UNIT_ENERGY_FOODCALORIE) {
        return ROBO_I18N_UNIT_Q_ENERGY;
    }
    if (unit <= ROBO_I18N_UNIT_POWER_HORSEPOWER) {
        return ROBO_I18N_UNIT_Q_POWER;
    }
    if (unit <= ROBO_I18N_UNIT_PRESSURE_POUND_FORCE_PER_SQUARE_INCH) {
        return ROBO_I18N_UNIT_Q_PRESSURE;
    }
    if (unit <= ROBO_I18N_UNIT_FREQUENCY_GIGAHERTZ) {
        return ROBO_I18N_UNIT_Q_FREQUENCY;
    }
    if (unit <= ROBO_I18N_UNIT_ANGLE_ARC_SECOND) {
        return ROBO_I18N_UNIT_Q_ANGLE;
    }
    if (unit == ROBO_I18N_UNIT_CONSUMPTION_LITER_PER_100_KILOMETER) {
        return ROBO_I18N_UNIT_Q_CONSUMPTION;
    }
    return ROBO_I18N_UNIT_Q_UNKNOWN;
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

static const robo_unit_row_t *unit_resolve(const robo_i18n_locale_t *loc,
                                           uint16_t unit,
                                           uint8_t width)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[64];
    const robo_unit_row_t *row;

    if (!loc || (unsigned)unit >= (unsigned)ROBO_I18N_UNIT_COUNT) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_unit_row_find(tag, unit, width);
        if (row) {
            return row;
        }
        row = robo_unit_row_find(cur.language, unit, width);
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
    return robo_unit_row_find("en", unit, width);
}

static const robo_unit_compound_t *compound_resolve(const robo_i18n_locale_t *loc, uint8_t width)
{
    robo_i18n_locale_t cur;
    int guard = 0;
    char tag[64];
    const robo_unit_compound_t *row;

    if (!loc) {
        return NULL;
    }
    cur = *loc;
    while (guard++ < 16) {
        format_locale_tag(&cur, tag, sizeof(tag));
        row = robo_unit_compound_find(tag, width);
        if (row) {
            return row;
        }
        row = robo_unit_compound_find(cur.language, width);
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
    return robo_unit_compound_find("en", width);
}

int robo_i18n_unit_has_patterns(const robo_i18n_locale_t *locale,
                                robo_i18n_unit_id_t unit,
                                robo_i18n_unit_width_t width)
{
    if (!locale || (unsigned)unit >= (unsigned)ROBO_I18N_UNIT_COUNT ||
        (unsigned)width > (unsigned)ROBO_I18N_UNIT_WIDTH_NARROW) {
        return 0;
    }
    return unit_resolve(locale, (uint16_t)unit, (uint8_t)width) != NULL;
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
    return robo_unit_str(idx);
}

static robo_result_t expand_unit_pattern(const robo_i18n_unit_formatter_t *fmt,
                                         const char *pattern,
                                         const robo_i18n_decimal_t *value,
                                         char *buf,
                                         size_t buf_cap,
                                         size_t *out_len)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && buf_cap > 0);
    robo_result_t r;

    if (!pattern || !value || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 1u] == '0' &&
            pattern[i + 2u] == '}') {
            char num[96];
            size_t nlen = 0;
            r = robo_i18n_number_format(&fmt->number, value, num, sizeof(num), &nlen);
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

static robo_result_t expand_slot0(const char *pattern,
                                  const char *payload,
                                  size_t payload_len,
                                  char *buf,
                                  size_t buf_cap,
                                  size_t *out_len)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && buf_cap > 0);
    robo_result_t r;

    if (!pattern || !payload || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 1u] == '0' &&
            pattern[i + 2u] == '}') {
            r = append_bytes(buf, buf_cap, &pos, payload, payload_len, writing);
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

static robo_result_t expand_compound_per(const char *pattern,
                                         const char *a,
                                         size_t a_len,
                                         const char *b,
                                         size_t b_len,
                                         char *buf,
                                         size_t buf_cap,
                                         size_t *out_len)
{
    size_t pos = 0;
    size_t i = 0;
    size_t plen;
    int writing = (buf != NULL && buf_cap > 0);
    robo_result_t r;

    if (!pattern || !a || !b || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    plen = strlen(pattern);
    while (i < plen) {
        if (pattern[i] == '{' && i + 2u < plen && pattern[i + 2u] == '}') {
            if (pattern[i + 1u] == '0') {
                r = append_bytes(buf, buf_cap, &pos, a, a_len, writing);
                if (r != ROBO_OK) {
                    *out_len = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
            if (pattern[i + 1u] == '1') {
                r = append_bytes(buf, buf_cap, &pos, b, b_len, writing);
                if (r != ROBO_OK) {
                    *out_len = pos;
                    return r;
                }
                i += 3u;
                continue;
            }
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

robo_result_t robo_i18n_unit_format(const robo_i18n_unit_formatter_t *fmt,
                                    const robo_i18n_decimal_t *value,
                                    robo_i18n_unit_id_t unit,
                                    char *buf,
                                    size_t buf_cap,
                                    size_t *out_len)
{
    const robo_unit_row_t *row;
    robo_i18n_plural_category_t cat = ROBO_I18N_PLURAL_OTHER;
    const char *pattern;
    robo_result_t r;

    if (!fmt || !fmt->ready || !value || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((unsigned)unit >= (unsigned)ROBO_I18N_UNIT_COUNT) {
        return ROBO_NOT_SUPPORTED;
    }

    row = unit_resolve(&fmt->locale, (uint16_t)unit, (uint8_t)fmt->options.width);
    if (!row) {
        return ROBO_NOT_FOUND;
    }

    /* Plural category follows the locale that owns the patterns (after fallback). */
    {
        const char *pat_loc = robo_unit_str(row->locale);
        robo_i18n_locale_t ploc;
        if (pat_loc && pat_loc[0] &&
            robo_i18n_locale_parse_canonical(pat_loc, strlen(pat_loc), &ploc) == ROBO_OK) {
            r = robo_i18n_plural_cardinal(&ploc, value, &cat);
        } else {
            r = robo_i18n_plural_cardinal(&fmt->locale, value, &cat);
        }
    }
    if (r != ROBO_OK) {
        return r;
    }
    pattern = pattern_for_category(row->patterns, cat);
    if (!pattern) {
        return ROBO_NOT_FOUND;
    }
    return expand_unit_pattern(fmt, pattern, value, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_unit_format_i64(const robo_i18n_unit_formatter_t *fmt,
                                        int64_t value,
                                        robo_i18n_unit_id_t unit,
                                        char *buf,
                                        size_t buf_cap,
                                        size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_i64(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_unit_format(fmt, &dec, unit, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_unit_format_double(const robo_i18n_unit_formatter_t *fmt,
                                           double value,
                                           robo_i18n_unit_id_t unit,
                                           char *buf,
                                           size_t buf_cap,
                                           size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_double(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_unit_format(fmt, &dec, unit, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_unit_format_per(const robo_i18n_unit_formatter_t *fmt,
                                        const robo_i18n_decimal_t *value,
                                        robo_i18n_unit_id_t unit_num,
                                        robo_i18n_unit_id_t unit_den,
                                        char *buf,
                                        size_t buf_cap,
                                        size_t *out_len)
{
    char primary[192];
    size_t plen = 0;
    const robo_unit_row_t *den_row;
    const robo_unit_compound_t *comp;
    robo_result_t r;

    if (!fmt || !fmt->ready || !value || !out_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((unsigned)unit_num >= (unsigned)ROBO_I18N_UNIT_COUNT ||
        (unsigned)unit_den >= (unsigned)ROBO_I18N_UNIT_COUNT) {
        return ROBO_NOT_SUPPORTED;
    }

    r = robo_i18n_unit_format(fmt, value, unit_num, primary, sizeof(primary), &plen);
    if (r != ROBO_OK) {
        return r;
    }

    den_row = unit_resolve(&fmt->locale, (uint16_t)unit_den, (uint8_t)fmt->options.width);
    if (!den_row) {
        return ROBO_NOT_FOUND;
    }

    if (den_row->per_unit >= 0) {
        const char *per_pat = robo_unit_str(den_row->per_unit);
        return expand_slot0(per_pat, primary, plen, buf, buf_cap, out_len);
    }

    comp = compound_resolve(&fmt->locale, (uint8_t)fmt->options.width);
    if (!comp || comp->per_pattern < 0 || den_row->display_name < 0) {
        return ROBO_NOT_FOUND;
    }
    {
        const char *per = robo_unit_str(comp->per_pattern);
        const char *dn = robo_unit_str(den_row->display_name);
        return expand_compound_per(per, primary, plen, dn, strlen(dn), buf, buf_cap, out_len);
    }
}

robo_result_t robo_i18n_unit_format_per_i64(const robo_i18n_unit_formatter_t *fmt,
                                            int64_t value,
                                            robo_i18n_unit_id_t unit_num,
                                            robo_i18n_unit_id_t unit_den,
                                            char *buf,
                                            size_t buf_cap,
                                            size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_i64(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_unit_format_per(fmt, &dec, unit_num, unit_den, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_unit_format_to_string(const robo_i18n_unit_formatter_t *fmt,
                                              const robo_i18n_decimal_t *value,
                                              robo_i18n_unit_id_t unit,
                                              robo_string_t *out)
{
    char stack[256];
    size_t len = 0;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_unit_format(fmt, value, unit, stack, sizeof(stack), &len);
    if (r == ROBO_OK) {
        return robo_string_append(out, stack, len);
    }
    return r;
}

/* ---- conversion (SI factors; separate from localization) ---- */

typedef struct unit_factor {
    robo_i18n_unit_id_t unit;
    /* value_si = value * num / den  (SI base for quantity) */
    int64_t num;
    int64_t den;
    uint8_t exact; /* 1 = exact rational; 0 = approximate customary */
} unit_factor_t;

/*
 * Length SI = meter. Exact metric; mile/yard/foot/inch approximate (intl yard).
 * Mass SI = kilogram. Exact metric; pound/ounce approximate (avoirdupois).
 * Area SI = square meter. hectare exact; acre approximate.
 * Volume SI = cubic meter. liter/mL exact; US liquid gallon approximate.
 * Duration SI = second. year/month approximate (Gregorian mean); week/day exact.
 * Speed SI = m/s. km/h exact; mph approximate.
 * Digital SI = bit. CLDR digital-* uses decimal 1000 prefixes (exact).
 * Energy SI = joule. kWh exact; calorie/foodcalorie approximate.
 * Power SI = watt. kW exact; horsepower approximate (mechanical).
 * Pressure SI = pascal. hPa/mbar exact; psi approximate.
 * Frequency SI = hertz. metric prefixes exact.
 * Angle SI = radian. degree/arcmin/arcsec approximate (pi).
 */

static const unit_factor_t k_factors[] = {
    {ROBO_I18N_UNIT_LENGTH_MILLIMETER, 1, 1000, 1},
    {ROBO_I18N_UNIT_LENGTH_CENTIMETER, 1, 100, 1},
    {ROBO_I18N_UNIT_LENGTH_METER, 1, 1, 1},
    {ROBO_I18N_UNIT_LENGTH_KILOMETER, 1000, 1, 1},
    {ROBO_I18N_UNIT_LENGTH_INCH, 254, 10000, 0}, /* 0.0254 m */
    {ROBO_I18N_UNIT_LENGTH_FOOT, 3048, 10000, 0},
    {ROBO_I18N_UNIT_LENGTH_YARD, 9144, 10000, 0},
    {ROBO_I18N_UNIT_LENGTH_MILE, 1609344, 1000, 0}, /* 1609.344 m */

    {ROBO_I18N_UNIT_MASS_GRAM, 1, 1000, 1},
    {ROBO_I18N_UNIT_MASS_KILOGRAM, 1, 1, 1},
    {ROBO_I18N_UNIT_MASS_OUNCE, 28349523125LL, 1000000000000LL, 0}, /* ~0.0283495 */
    {ROBO_I18N_UNIT_MASS_POUND, 45359237, 100000000, 0}, /* 0.45359237 */

    {ROBO_I18N_UNIT_AREA_SQUARE_METER, 1, 1, 1},
    {ROBO_I18N_UNIT_AREA_SQUARE_KILOMETER, 1000000, 1, 1},
    {ROBO_I18N_UNIT_AREA_HECTARE, 10000, 1, 1},
    {ROBO_I18N_UNIT_AREA_ACRE, 40468564224LL, 10000000LL, 0}, /* 4046.8564224 */

    {ROBO_I18N_UNIT_VOLUME_MILLILITER, 1, 1000000, 1},
    {ROBO_I18N_UNIT_VOLUME_LITER, 1, 1000, 1},
    {ROBO_I18N_UNIT_VOLUME_CUBIC_METER, 1, 1, 1},
    {ROBO_I18N_UNIT_VOLUME_GALLON, 3785411784LL, 1000000000000LL, 0}, /* US liq */

    {ROBO_I18N_UNIT_DURATION_MILLISECOND, 1, 1000, 1},
    {ROBO_I18N_UNIT_DURATION_SECOND, 1, 1, 1},
    {ROBO_I18N_UNIT_DURATION_MINUTE, 60, 1, 1},
    {ROBO_I18N_UNIT_DURATION_HOUR, 3600, 1, 1},
    {ROBO_I18N_UNIT_DURATION_DAY, 86400, 1, 1},
    {ROBO_I18N_UNIT_DURATION_WEEK, 604800, 1, 1},
    {ROBO_I18N_UNIT_DURATION_MONTH, 2629746, 1, 0}, /* mean Gregorian month */
    {ROBO_I18N_UNIT_DURATION_YEAR, 31556952, 1, 0}, /* mean Gregorian year */

    {ROBO_I18N_UNIT_SPEED_METER_PER_SECOND, 1, 1, 1},
    {ROBO_I18N_UNIT_SPEED_KILOMETER_PER_HOUR, 5, 18, 1}, /* /3.6 */
    {ROBO_I18N_UNIT_SPEED_MILE_PER_HOUR, 1609344, 3600000, 0},

    {ROBO_I18N_UNIT_DIGITAL_BIT, 1, 1, 1},
    {ROBO_I18N_UNIT_DIGITAL_BYTE, 8, 1, 1},
    {ROBO_I18N_UNIT_DIGITAL_KILOBYTE, 8000, 1, 1},
    {ROBO_I18N_UNIT_DIGITAL_MEGABYTE, 8000000, 1, 1},
    {ROBO_I18N_UNIT_DIGITAL_GIGABYTE, 8000000000LL, 1, 1},
    {ROBO_I18N_UNIT_DIGITAL_TERABYTE, 8000000000000LL, 1, 1},

    {ROBO_I18N_UNIT_ENERGY_JOULE, 1, 1, 1},
    {ROBO_I18N_UNIT_ENERGY_KILOWATT_HOUR, 3600000, 1, 1},
    {ROBO_I18N_UNIT_ENERGY_CALORIE, 4184, 1000, 0}, /* thermochemical */
    {ROBO_I18N_UNIT_ENERGY_FOODCALORIE, 4184, 1, 0},

    {ROBO_I18N_UNIT_POWER_WATT, 1, 1, 1},
    {ROBO_I18N_UNIT_POWER_KILOWATT, 1000, 1, 1},
    {ROBO_I18N_UNIT_POWER_HORSEPOWER, 7457, 10, 0}, /* 745.7 W */

    {ROBO_I18N_UNIT_PRESSURE_PASCAL, 1, 1, 1},
    {ROBO_I18N_UNIT_PRESSURE_HECTOPASCAL, 100, 1, 1},
    {ROBO_I18N_UNIT_PRESSURE_MILLIBAR, 100, 1, 1},
    {ROBO_I18N_UNIT_PRESSURE_POUND_FORCE_PER_SQUARE_INCH, 6894757, 1000, 0},

    {ROBO_I18N_UNIT_FREQUENCY_HERTZ, 1, 1, 1},
    {ROBO_I18N_UNIT_FREQUENCY_KILOHERTZ, 1000, 1, 1},
    {ROBO_I18N_UNIT_FREQUENCY_MEGAHERTZ, 1000000, 1, 1},
    {ROBO_I18N_UNIT_FREQUENCY_GIGAHERTZ, 1000000000, 1, 1},

    {ROBO_I18N_UNIT_ANGLE_RADIAN, 1, 1, 1},
    {ROBO_I18N_UNIT_ANGLE_DEGREE, 174532925, 10000000000LL, 0}, /* pi/180 */
    {ROBO_I18N_UNIT_ANGLE_ARC_MINUTE, 290888208, 1000000000000LL, 0},
    {ROBO_I18N_UNIT_ANGLE_ARC_SECOND, 48481368, 10000000000000LL, 0},
};

static const unit_factor_t *find_factor(robo_i18n_unit_id_t unit)
{
    size_t i;
    for (i = 0; i < sizeof(k_factors) / sizeof(k_factors[0]); i++) {
        if (k_factors[i].unit == unit) {
            return &k_factors[i];
        }
    }
    return NULL;
}

static double decimal_to_double(const robo_i18n_decimal_t *d)
{
    double v = 0.0;
    size_t i;
    if (!d || d->kind != ROBO_I18N_DECIMAL_NUMBER || d->digit_count == 0) {
        return 0.0;
    }
    for (i = 0; i < d->digit_count; i++) {
        v = v * 10.0 + (double)(d->digits[i] - '0');
    }
    for (i = 0; i < d->scale; i++) {
        v /= 10.0;
    }
    if (d->negative) {
        v = -v;
    }
    return v;
}

static robo_result_t convert_temperature(robo_i18n_unit_id_t from,
                                         robo_i18n_unit_id_t to,
                                         const robo_i18n_decimal_t *in,
                                         robo_i18n_decimal_t *out,
                                         int *approximate_out)
{
    double v = decimal_to_double(in);
    double k;

    if (approximate_out) {
        *approximate_out = 1;
    }
    /* Normalize to Kelvin. */
    if (from == ROBO_I18N_UNIT_TEMPERATURE_KELVIN) {
        k = v;
    } else if (from == ROBO_I18N_UNIT_TEMPERATURE_CELSIUS) {
        k = v + 273.15;
    } else if (from == ROBO_I18N_UNIT_TEMPERATURE_FAHRENHEIT) {
        k = (v - 32.0) * 5.0 / 9.0 + 273.15;
    } else {
        return ROBO_NOT_SUPPORTED;
    }
    if (to == ROBO_I18N_UNIT_TEMPERATURE_KELVIN) {
        return robo_i18n_decimal_from_double(k, out);
    }
    if (to == ROBO_I18N_UNIT_TEMPERATURE_CELSIUS) {
        return robo_i18n_decimal_from_double(k - 273.15, out);
    }
    if (to == ROBO_I18N_UNIT_TEMPERATURE_FAHRENHEIT) {
        return robo_i18n_decimal_from_double((k - 273.15) * 9.0 / 5.0 + 32.0, out);
    }
    return ROBO_NOT_SUPPORTED;
}

robo_result_t robo_i18n_unit_convert(robo_i18n_unit_id_t from,
                                     robo_i18n_unit_id_t to,
                                     const robo_i18n_decimal_t *in,
                                     robo_i18n_decimal_t *out,
                                     int *approximate_out)
{
    robo_i18n_unit_quantity_t qf;
    robo_i18n_unit_quantity_t qt;
    const unit_factor_t *ff;
    const unit_factor_t *ft;
    double v;
    double si;
    double result;

    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (approximate_out) {
        *approximate_out = 0;
    }
    if (from == to) {
        *out = *in;
        return ROBO_OK;
    }

    qf = robo_i18n_unit_quantity(from);
    qt = robo_i18n_unit_quantity(to);
    if (qf == ROBO_I18N_UNIT_Q_UNKNOWN || qf != qt) {
        return ROBO_NOT_SUPPORTED;
    }
    if (qf == ROBO_I18N_UNIT_Q_TEMPERATURE) {
        return convert_temperature(from, to, in, out, approximate_out);
    }
    if (qf == ROBO_I18N_UNIT_Q_CONSUMPTION) {
        return ROBO_NOT_SUPPORTED;
    }

    ff = find_factor(from);
    ft = find_factor(to);
    if (!ff || !ft) {
        return ROBO_NOT_SUPPORTED;
    }
    if (!ff->exact || !ft->exact) {
        if (approximate_out) {
            *approximate_out = 1;
        }
    }

    v = decimal_to_double(in);
    si = v * (double)ff->num / (double)ff->den;
    result = si * (double)ft->den / (double)ft->num;
    return robo_i18n_decimal_from_double(result, out);
}

robo_result_t robo_i18n_unit_convert_i64(robo_i18n_unit_id_t from,
                                         robo_i18n_unit_id_t to,
                                         int64_t value,
                                         int64_t *out,
                                         int *approximate_out)
{
    robo_i18n_decimal_t in;
    robo_i18n_decimal_t dec;
    robo_result_t r;
    double d;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_decimal_from_i64(value, &in);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_i18n_unit_convert(from, to, &in, &dec, approximate_out);
    if (r != ROBO_OK) {
        return r;
    }
    d = decimal_to_double(&dec);
    if (d > 9223372036854775807.0 || d < -9223372036854775807.0) {
        return ROBO_ERROR;
    }
    *out = (int64_t)(d >= 0.0 ? d + 0.5 : d - 0.5);
    return ROBO_OK;
}

const char *robo_i18n_unit_cldr_version_string(void)
{
    return robo_unit_version_string();
}
