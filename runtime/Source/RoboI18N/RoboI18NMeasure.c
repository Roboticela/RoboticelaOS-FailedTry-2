/*
 * RoboI18N measurement preferences (Goal 4.20).
 */

#include "RoboI18N/RoboI18NMeasure.h"

#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboMeasurePrefData.h"

#include <string.h>

static void copy_id(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int parse_ms_keyword(const char *type, robo_i18n_measurement_t *out)
{
    if (!type || !out) {
        return 0;
    }
    if (strcmp(type, "metric") == 0) {
        *out = ROBO_I18N_MEASURE_METRIC;
        return 1;
    }
    if (strcmp(type, "ussystem") == 0 || strcmp(type, "US") == 0) {
        *out = ROBO_I18N_MEASURE_US;
        return 1;
    }
    if (strcmp(type, "uksystem") == 0 || strcmp(type, "UK") == 0) {
        *out = ROBO_I18N_MEASURE_UK;
        return 1;
    }
    return 0;
}

static void resolve_region(const robo_i18n_locale_t *loc, char *region, size_t cap)
{
    if (loc->region[0]) {
        copy_id(region, cap, loc->region);
        return;
    }
    {
        robo_i18n_locale_t maxed;
        if (robo_i18n_locale_maximize(loc, &maxed) == ROBO_OK && maxed.region[0]) {
            copy_id(region, cap, maxed.region);
            return;
        }
    }
    copy_id(region, cap, "001");
}

static robo_i18n_measurement_t lookup_system_table(const char *region, int temperature)
{
    const robo_mpref_system_t *row;
    if (temperature) {
        row = robo_mpref_temp_system_find(region);
        if (row) {
            return (robo_i18n_measurement_t)row->system;
        }
        return ROBO_I18N_MEASURE_METRIC; /* no entry → use world default metric */
    }
    row = robo_mpref_system_find(region);
    if (!row) {
        row = robo_mpref_system_find("001");
    }
    if (!row) {
        return ROBO_I18N_MEASURE_METRIC;
    }
    return (robo_i18n_measurement_t)row->system;
}

static int read_ms_from_locale(const robo_i18n_locale_t *loc, robo_i18n_measurement_t *out)
{
    robo_i18n_unicode_keywords_t kw;
    const char *p;
    char ms[32];

    if (!loc || !out) {
        return 0;
    }
    ms[0] = '\0';
    if (robo_i18n_locale_get_keyword(loc, "ms", ms, sizeof(ms)) == ROBO_OK &&
        parse_ms_keyword(ms, out)) {
        return 1;
    }
    if (robo_i18n_locale_unicode_keywords(loc, &kw) != ROBO_OK) {
        return 0;
    }
    /* Unknown keys (incl. ms) live in kw.other as key-type-key-type… */
    p = kw.other;
    while (p && *p) {
        const char *hit = strstr(p, "ms-");
        if (!hit) {
            break;
        }
        if (hit == kw.other || hit[-1] == '-') {
            char type[16];
            size_t i = 0;
            hit += 3;
            while (hit[i] && hit[i] != '-' && i + 1u < sizeof(type)) {
                type[i] = hit[i];
                i++;
            }
            type[i] = '\0';
            if (parse_ms_keyword(type, out)) {
                return 1;
            }
        }
        p = hit + 1;
    }
    return 0;
}

static void refresh_systems(robo_i18n_measure_prefs_t *prefs)
{
    robo_i18n_measurement_t from_ms;

    if (prefs->system_overridden) {
        prefs->temp_system = prefs->system;
        return;
    }

    if (read_ms_from_locale(&prefs->locale, &from_ms)) {
        prefs->system = from_ms;
        prefs->temp_system = from_ms;
        return;
    }

    prefs->system = lookup_system_table(prefs->region, 0);
    if (robo_mpref_temp_system_find(prefs->region)) {
        prefs->temp_system = lookup_system_table(prefs->region, 1);
    } else {
        prefs->temp_system =
            (prefs->system == ROBO_I18N_MEASURE_US) ? ROBO_I18N_MEASURE_US : ROBO_I18N_MEASURE_METRIC;
    }
}

void robo_i18n_measure_prefs_clear(robo_i18n_measure_prefs_t *prefs)
{
    if (!prefs) {
        return;
    }
    memset(prefs, 0, sizeof(*prefs));
}

robo_result_t robo_i18n_measure_prefs_init(robo_i18n_measure_prefs_t *prefs,
                                           const robo_i18n_locale_t *locale)
{
    if (!prefs || !locale) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(prefs, 0, sizeof(*prefs));
    prefs->locale = *locale;
    resolve_region(locale, prefs->region, sizeof(prefs->region));
    refresh_systems(prefs);
    prefs->ready = 1;
    return ROBO_OK;
}

robo_result_t robo_i18n_measure_prefs_set_system(robo_i18n_measure_prefs_t *prefs,
                                                 robo_i18n_measurement_t system)
{
    if (!prefs || !prefs->ready ||
        (unsigned)system > (unsigned)ROBO_I18N_MEASURE_UK) {
        return ROBO_INVALID_ARGUMENT;
    }
    prefs->system = system;
    prefs->system_overridden = 1;
    prefs->temp_system = system;
    if (system != ROBO_I18N_MEASURE_US && robo_mpref_temp_system_find(prefs->region)) {
        /* Keep temperature category exception only when not forcing US/metric/UK wholesale —
         * forced system applies to temperature too for predictability. */
    }
    return ROBO_OK;
}

robo_result_t robo_i18n_measure_prefs_clear_system(robo_i18n_measure_prefs_t *prefs)
{
    if (!prefs || !prefs->ready) {
        return ROBO_INVALID_ARGUMENT;
    }
    prefs->system_overridden = 0;
    refresh_systems(prefs);
    return ROBO_OK;
}

static int unit_matches_category(robo_i18n_unit_id_t unit, robo_i18n_measure_category_t cat)
{
    robo_i18n_unit_quantity_t q = robo_i18n_unit_quantity(unit);
    switch (cat) {
    case ROBO_I18N_MEASURE_CAT_TEMPERATURE:
        return q == ROBO_I18N_UNIT_Q_TEMPERATURE;
    case ROBO_I18N_MEASURE_CAT_LENGTH:
        return q == ROBO_I18N_UNIT_Q_LENGTH;
    case ROBO_I18N_MEASURE_CAT_AREA:
        return q == ROBO_I18N_UNIT_Q_AREA;
    case ROBO_I18N_MEASURE_CAT_MASS:
        return q == ROBO_I18N_UNIT_Q_MASS;
    case ROBO_I18N_MEASURE_CAT_VOLUME:
        return q == ROBO_I18N_UNIT_Q_VOLUME;
    case ROBO_I18N_MEASURE_CAT_SPEED:
        return q == ROBO_I18N_UNIT_Q_SPEED;
    case ROBO_I18N_MEASURE_CAT_PRESSURE:
        return q == ROBO_I18N_UNIT_Q_PRESSURE;
    case ROBO_I18N_MEASURE_CAT_ENERGY:
        return q == ROBO_I18N_UNIT_Q_ENERGY;
    case ROBO_I18N_MEASURE_CAT_POWER:
        return q == ROBO_I18N_UNIT_Q_POWER;
    default:
        return 0;
    }
}

robo_result_t robo_i18n_measure_prefs_set_unit(robo_i18n_measure_prefs_t *prefs,
                                               robo_i18n_measure_category_t category,
                                               robo_i18n_unit_id_t unit)
{
    if (!prefs || !prefs->ready ||
        (unsigned)category >= (unsigned)ROBO_I18N_MEASURE_CAT_COUNT) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((unsigned)unit >= (unsigned)ROBO_I18N_UNIT_COUNT ||
        !unit_matches_category(unit, category)) {
        return ROBO_INVALID_ARGUMENT;
    }
    prefs->has_cat_override[(unsigned)category] = 1;
    prefs->cat_override[(unsigned)category] = unit;
    return ROBO_OK;
}

robo_result_t robo_i18n_measure_prefs_clear_unit(robo_i18n_measure_prefs_t *prefs,
                                                 robo_i18n_measure_category_t category)
{
    if (!prefs || !prefs->ready ||
        (unsigned)category >= (unsigned)ROBO_I18N_MEASURE_CAT_COUNT) {
        return ROBO_INVALID_ARGUMENT;
    }
    prefs->has_cat_override[(unsigned)category] = 0;
    prefs->cat_override[(unsigned)category] = (robo_i18n_unit_id_t)0;
    return ROBO_OK;
}

robo_i18n_measurement_t robo_i18n_measure_prefs_system(const robo_i18n_measure_prefs_t *prefs)
{
    if (!prefs || !prefs->ready) {
        return ROBO_I18N_MEASURE_METRIC;
    }
    return prefs->system;
}

robo_i18n_measurement_t robo_i18n_measure_prefs_temperature_system(
    const robo_i18n_measure_prefs_t *prefs)
{
    if (!prefs || !prefs->ready) {
        return ROBO_I18N_MEASURE_METRIC;
    }
    return prefs->temp_system;
}

static robo_i18n_unit_id_t system_default_unit(robo_i18n_measure_category_t cat,
                                               robo_i18n_measurement_t system,
                                               int for_temperature)
{
    robo_i18n_measurement_t s = for_temperature ? system : system;
    (void)for_temperature;
    switch (cat) {
    case ROBO_I18N_MEASURE_CAT_TEMPERATURE:
        return (s == ROBO_I18N_MEASURE_US) ? ROBO_I18N_UNIT_TEMPERATURE_FAHRENHEIT
                                           : ROBO_I18N_UNIT_TEMPERATURE_CELSIUS;
    case ROBO_I18N_MEASURE_CAT_LENGTH:
        return (s == ROBO_I18N_MEASURE_METRIC) ? ROBO_I18N_UNIT_LENGTH_KILOMETER
                                               : ROBO_I18N_UNIT_LENGTH_MILE;
    case ROBO_I18N_MEASURE_CAT_AREA:
        return (s == ROBO_I18N_MEASURE_METRIC) ? ROBO_I18N_UNIT_AREA_SQUARE_KILOMETER
                                               : ROBO_I18N_UNIT_AREA_ACRE;
    case ROBO_I18N_MEASURE_CAT_MASS:
        return (s == ROBO_I18N_MEASURE_METRIC) ? ROBO_I18N_UNIT_MASS_KILOGRAM
                                               : ROBO_I18N_UNIT_MASS_POUND;
    case ROBO_I18N_MEASURE_CAT_VOLUME:
        return (s == ROBO_I18N_MEASURE_METRIC) ? ROBO_I18N_UNIT_VOLUME_LITER
                                               : ROBO_I18N_UNIT_VOLUME_GALLON;
    case ROBO_I18N_MEASURE_CAT_SPEED:
        return (s == ROBO_I18N_MEASURE_METRIC) ? ROBO_I18N_UNIT_SPEED_KILOMETER_PER_HOUR
                                               : ROBO_I18N_UNIT_SPEED_MILE_PER_HOUR;
    case ROBO_I18N_MEASURE_CAT_PRESSURE:
        return (s == ROBO_I18N_MEASURE_METRIC) ? ROBO_I18N_UNIT_PRESSURE_HECTOPASCAL
                                               : ROBO_I18N_UNIT_PRESSURE_POUND_FORCE_PER_SQUARE_INCH;
    case ROBO_I18N_MEASURE_CAT_ENERGY:
        return ROBO_I18N_UNIT_ENERGY_KILOWATT_HOUR;
    case ROBO_I18N_MEASURE_CAT_POWER:
        return (s == ROBO_I18N_MEASURE_METRIC) ? ROBO_I18N_UNIT_POWER_KILOWATT
                                               : ROBO_I18N_UNIT_POWER_HORSEPOWER;
    default:
        return ROBO_I18N_UNIT_LENGTH_METER;
    }
}

static uint8_t effective_usage(robo_i18n_measure_category_t cat,
                               robo_i18n_measure_usage_t usage)
{
    if ((unsigned)usage >= (unsigned)ROBO_I18N_MEASURE_USAGE_COUNT) {
        return (uint8_t)ROBO_I18N_MEASURE_USAGE_DEFAULT;
    }
    /* Volume: prefer fluid list when DEFAULT (CLDR default volume is cubic-*). */
    if (cat == ROBO_I18N_MEASURE_CAT_VOLUME && usage == ROBO_I18N_MEASURE_USAGE_DEFAULT) {
        return (uint8_t)ROBO_I18N_MEASURE_USAGE_FLUID;
    }
    return (uint8_t)usage;
}

static const robo_mpref_units_t *find_pref_units(uint8_t cat,
                                                 uint8_t usage,
                                                 const char *region)
{
    const robo_mpref_units_t *row = robo_mpref_units_find(cat, usage, region);
    if (row) {
        return row;
    }
    return robo_mpref_units_find(cat, usage, "001");
}

robo_result_t robo_i18n_measure_resolve_unit(const robo_i18n_measure_prefs_t *prefs,
                                             robo_i18n_measure_category_t category,
                                             robo_i18n_measure_usage_t usage,
                                             const robo_i18n_unit_id_t *explicit_unit,
                                             robo_i18n_unit_id_t *out_unit,
                                             robo_i18n_measure_source_t *out_src)
{
    uint8_t u;
    const robo_mpref_units_t *row;
    robo_i18n_measurement_t sys;

    if (!prefs || !prefs->ready || !out_unit ||
        (unsigned)category >= (unsigned)ROBO_I18N_MEASURE_CAT_COUNT) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (explicit_unit) {
        if ((unsigned)*explicit_unit >= (unsigned)ROBO_I18N_UNIT_COUNT ||
            !unit_matches_category(*explicit_unit, category)) {
            return ROBO_INVALID_ARGUMENT;
        }
        *out_unit = *explicit_unit;
        if (out_src) {
            *out_src = ROBO_I18N_MEASURE_SRC_EXPLICIT;
        }
        return ROBO_OK;
    }

    if (prefs->has_cat_override[(unsigned)category]) {
        *out_unit = prefs->cat_override[(unsigned)category];
        if (out_src) {
            *out_src = ROBO_I18N_MEASURE_SRC_CATEGORY_OVERRIDE;
        }
        return ROBO_OK;
    }

    u = effective_usage(category, usage);
    row = find_pref_units((uint8_t)category, u, prefs->region);
    if (!row && u != (uint8_t)ROBO_I18N_MEASURE_USAGE_DEFAULT) {
        row = find_pref_units((uint8_t)category, (uint8_t)ROBO_I18N_MEASURE_USAGE_DEFAULT,
                              prefs->region);
    }
    if (row && row->count > 0) {
        *out_unit = (robo_i18n_unit_id_t)row->units[0];
        if (out_src) {
            *out_src = ROBO_I18N_MEASURE_SRC_CLDR_PREFERENCE;
        }
        return ROBO_OK;
    }

    sys = (category == ROBO_I18N_MEASURE_CAT_TEMPERATURE) ? prefs->temp_system : prefs->system;
    if (prefs->system_overridden && out_src) {
        *out_src = ROBO_I18N_MEASURE_SRC_SYSTEM_OVERRIDE;
    } else if (out_src) {
        *out_src = ROBO_I18N_MEASURE_SRC_SYSTEM_DEFAULT;
    }
    *out_unit = system_default_unit(category, sys, category == ROBO_I18N_MEASURE_CAT_TEMPERATURE);
    return ROBO_OK;
}

robo_result_t robo_i18n_measure_preferred_unit(const robo_i18n_measure_prefs_t *prefs,
                                               robo_i18n_measure_category_t category,
                                               robo_i18n_unit_id_t *out_unit)
{
    return robo_i18n_measure_resolve_unit(prefs, category, ROBO_I18N_MEASURE_USAGE_DEFAULT, NULL,
                                          out_unit, NULL);
}

robo_result_t robo_i18n_measure_preferred_unit_usage(
    const robo_i18n_measure_prefs_t *prefs,
    robo_i18n_measure_category_t category,
    robo_i18n_measure_usage_t usage,
    robo_i18n_unit_id_t *out_unit)
{
    return robo_i18n_measure_resolve_unit(prefs, category, usage, NULL, out_unit, NULL);
}

robo_result_t robo_i18n_measure_format_converted(
    const robo_i18n_measure_prefs_t *prefs,
    const robo_i18n_unit_formatter_t *fmt,
    robo_i18n_measure_category_t category,
    robo_i18n_measure_usage_t usage,
    const robo_i18n_decimal_t *value,
    robo_i18n_unit_id_t from_unit,
    const robo_i18n_unit_id_t *explicit_unit,
    char *buf,
    size_t buf_cap,
    size_t *out_len)
{
    robo_i18n_unit_id_t to_unit;
    robo_i18n_decimal_t converted;
    int approx = 0;
    robo_result_t r;

    if (!prefs || !fmt || !value) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_measure_resolve_unit(prefs, category, usage, explicit_unit, &to_unit, NULL);
    if (r != ROBO_OK) {
        return r;
    }
    if (from_unit == to_unit) {
        return robo_i18n_unit_format(fmt, value, to_unit, buf, buf_cap, out_len);
    }
    r = robo_i18n_unit_convert(from_unit, to_unit, value, &converted, &approx);
    if (r != ROBO_OK) {
        return r;
    }
    (void)approx;
    /* Prefer exact integer decimals when conversion lands on a whole number. */
    if (converted.kind == ROBO_I18N_DECIMAL_NUMBER && converted.scale > 0) {
        int frac_nonzero = 0;
        size_t i;
        size_t int_digits;
        if (converted.digit_count > converted.scale) {
            int_digits = (size_t)(converted.digit_count - converted.scale);
        } else {
            int_digits = 0;
        }
        for (i = int_digits; i < converted.digit_count; i++) {
            if (converted.digits[i] != '0') {
                frac_nonzero = 1;
                break;
            }
        }
        if (!frac_nonzero) {
            int64_t iv = 0;
            int neg = converted.negative;
            if (int_digits == 0) {
                iv = 0;
            } else {
                for (i = 0; i < int_digits; i++) {
                    int digit = converted.digits[i] - '0';
                    if (iv > (9223372036854775807LL - digit) / 10) {
                        iv = 0;
                        frac_nonzero = 1;
                        break;
                    }
                    iv = iv * 10 + digit;
                }
            }
            if (!frac_nonzero) {
                if (neg) {
                    iv = -iv;
                }
                r = robo_i18n_decimal_from_i64(iv, &converted);
                if (r != ROBO_OK) {
                    return r;
                }
            }
        }
    }
    return robo_i18n_unit_format(fmt, &converted, to_unit, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_measure_format_converted_i64(
    const robo_i18n_measure_prefs_t *prefs,
    const robo_i18n_unit_formatter_t *fmt,
    robo_i18n_measure_category_t category,
    robo_i18n_measure_usage_t usage,
    int64_t value,
    robo_i18n_unit_id_t from_unit,
    const robo_i18n_unit_id_t *explicit_unit,
    char *buf,
    size_t buf_cap,
    size_t *out_len)
{
    robo_i18n_decimal_t dec;
    robo_result_t r = robo_i18n_decimal_from_i64(value, &dec);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_i18n_measure_format_converted(prefs, fmt, category, usage, &dec, from_unit,
                                              explicit_unit, buf, buf_cap, out_len);
}

robo_result_t robo_i18n_measure_system_for_locale(const robo_i18n_locale_t *locale,
                                                  robo_i18n_measurement_t *out)
{
    robo_i18n_measure_prefs_t prefs;
    robo_result_t r;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_measure_prefs_init(&prefs, locale);
    if (r != ROBO_OK) {
        return r;
    }
    *out = prefs.system;
    return ROBO_OK;
}

robo_result_t robo_i18n_measure_temperature_system_for_locale(
    const robo_i18n_locale_t *locale,
    robo_i18n_measurement_t *out)
{
    robo_i18n_measure_prefs_t prefs;
    robo_result_t r;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_i18n_measure_prefs_init(&prefs, locale);
    if (r != ROBO_OK) {
        return r;
    }
    *out = prefs.temp_system;
    return ROBO_OK;
}

const char *robo_i18n_measure_category_name(robo_i18n_measure_category_t category)
{
    if ((unsigned)category >= ROBO_MPREF_CATEGORY_COUNT) {
        return "";
    }
    return robo_mpref_category_names[(unsigned)category];
}

const char *robo_i18n_measure_usage_name(robo_i18n_measure_usage_t usage)
{
    if ((unsigned)usage >= ROBO_MPREF_USAGE_COUNT) {
        return "";
    }
    return robo_mpref_usage_names[(unsigned)usage];
}

const char *robo_i18n_measure_cldr_version_string(void)
{
    return robo_mpref_version_string();
}
