/*
 * RoboI18N measurement preferences suite (Goal 4.20).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboMeasurePrefData.h"
#include "RoboTest.h"

#include <math.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_system(const char *tag, robo_i18n_measurement_t want)
{
    robo_i18n_locale_t loc;
    robo_i18n_measure_prefs_t prefs;
    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_measure_prefs_system(&prefs), (int)want);
}

static void expect_unit(const char *tag,
                        robo_i18n_measure_category_t cat,
                        robo_i18n_measure_usage_t usage,
                        robo_i18n_unit_id_t want)
{
    robo_i18n_locale_t loc;
    robo_i18n_measure_prefs_t prefs;
    robo_i18n_unit_id_t got = ROBO_I18N_UNIT_COUNT;
    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_preferred_unit_usage(&prefs, cat, usage, &got), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)got, (int)want);
}

static void test_meas_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.27.0");
    ROBO_CHECK_STR_EQ(robo_i18n_measure_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(ROBO_MPREF_CLDR_VERSION, "46");
    ROBO_CHECK(robo_mpref_units_count > 20u);
}

static void test_meas_systems_matrix(void)
{
    expect_system("en-US", ROBO_I18N_MEASURE_US);
    expect_system("en-GB", ROBO_I18N_MEASURE_UK);
    expect_system("en-CA", ROBO_I18N_MEASURE_METRIC);
    expect_system("en-AU", ROBO_I18N_MEASURE_METRIC);
    expect_system("fr-FR", ROBO_I18N_MEASURE_METRIC);
    expect_system("de-DE", ROBO_I18N_MEASURE_METRIC);
    expect_system("es-ES", ROBO_I18N_MEASURE_METRIC);
    expect_system("ru-RU", ROBO_I18N_MEASURE_METRIC);
    expect_system("ar-SA", ROBO_I18N_MEASURE_METRIC);
    expect_system("fa-IR", ROBO_I18N_MEASURE_METRIC);
    expect_system("ur-PK", ROBO_I18N_MEASURE_METRIC);
    expect_system("hi-IN", ROBO_I18N_MEASURE_METRIC);
    expect_system("bn-BD", ROBO_I18N_MEASURE_METRIC);
    expect_system("zh-CN", ROBO_I18N_MEASURE_METRIC);
    expect_system("zh-TW", ROBO_I18N_MEASURE_METRIC);
    expect_system("ja-JP", ROBO_I18N_MEASURE_METRIC);
    expect_system("ko-KR", ROBO_I18N_MEASURE_METRIC);
}

static void test_meas_language_vs_region(void)
{
    /* Same language, different regions. */
    expect_unit("en-US", ROBO_I18N_MEASURE_CAT_TEMPERATURE, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_TEMPERATURE_FAHRENHEIT);
    expect_unit("en-GB", ROBO_I18N_MEASURE_CAT_TEMPERATURE, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_TEMPERATURE_CELSIUS);
    expect_unit("en-CA", ROBO_I18N_MEASURE_CAT_TEMPERATURE, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_TEMPERATURE_CELSIUS);
    expect_unit("en-AU", ROBO_I18N_MEASURE_CAT_TEMPERATURE, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_TEMPERATURE_CELSIUS);

    expect_unit("en-US", ROBO_I18N_MEASURE_CAT_LENGTH, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_LENGTH_MILE);
    expect_unit("en-GB", ROBO_I18N_MEASURE_CAT_LENGTH, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_LENGTH_MILE);
    expect_unit("en-CA", ROBO_I18N_MEASURE_CAT_LENGTH, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_LENGTH_KILOMETER);
    expect_unit("fr-FR", ROBO_I18N_MEASURE_CAT_LENGTH, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_LENGTH_KILOMETER);

    expect_unit("en-US", ROBO_I18N_MEASURE_CAT_VOLUME, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_VOLUME_GALLON);
    expect_unit("fr-FR", ROBO_I18N_MEASURE_CAT_VOLUME, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_VOLUME_LITER);

    expect_unit("en-US", ROBO_I18N_MEASURE_CAT_SPEED, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_SPEED_MILE_PER_HOUR);
    expect_unit("de-DE", ROBO_I18N_MEASURE_CAT_SPEED, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_SPEED_KILOMETER_PER_HOUR);

    expect_unit("en-US", ROBO_I18N_MEASURE_CAT_MASS, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_MASS_POUND);
    expect_unit("ja-JP", ROBO_I18N_MEASURE_CAT_MASS, ROBO_I18N_MEASURE_USAGE_DEFAULT,
                ROBO_I18N_UNIT_MASS_KILOGRAM);
}

static void test_meas_overrides_and_explicit(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_measure_prefs_t prefs;
    robo_i18n_unit_id_t got = ROBO_I18N_UNIT_COUNT;
    robo_i18n_unit_id_t explicit_u = ROBO_I18N_UNIT_TEMPERATURE_KELVIN;
    robo_i18n_measure_source_t src = ROBO_I18N_MEASURE_SRC_SYSTEM_DEFAULT;

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_set_unit(&prefs, ROBO_I18N_MEASURE_CAT_TEMPERATURE,
                                                       ROBO_I18N_UNIT_TEMPERATURE_CELSIUS),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_resolve_unit(&prefs, ROBO_I18N_MEASURE_CAT_TEMPERATURE,
                                                     ROBO_I18N_MEASURE_USAGE_DEFAULT, NULL, &got,
                                                     &src),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)got, (int)ROBO_I18N_UNIT_TEMPERATURE_CELSIUS);
    ROBO_CHECK_EQ_INT((int)src, (int)ROBO_I18N_MEASURE_SRC_CATEGORY_OVERRIDE);

    ROBO_CHECK_EQ_INT(robo_i18n_measure_resolve_unit(&prefs, ROBO_I18N_MEASURE_CAT_TEMPERATURE,
                                                     ROBO_I18N_MEASURE_USAGE_DEFAULT, &explicit_u,
                                                     &got, &src),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)got, (int)ROBO_I18N_UNIT_TEMPERATURE_KELVIN);
    ROBO_CHECK_EQ_INT((int)src, (int)ROBO_I18N_MEASURE_SRC_EXPLICIT);

    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_clear_unit(&prefs, ROBO_I18N_MEASURE_CAT_TEMPERATURE),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_set_system(&prefs, ROBO_I18N_MEASURE_METRIC),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_measure_prefs_system(&prefs), (int)ROBO_I18N_MEASURE_METRIC);

    /* Wrong quantity override rejected. */
    ROBO_CHECK(robo_i18n_measure_prefs_set_unit(&prefs, ROBO_I18N_MEASURE_CAT_TEMPERATURE,
                                                ROBO_I18N_UNIT_LENGTH_METER) != ROBO_OK);
}

static void test_meas_unicode_ms(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_measure_prefs_t prefs;
    robo_i18n_unit_id_t got = ROBO_I18N_UNIT_COUNT;

    parse_tag("en-US-u-ms-metric", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_measure_prefs_system(&prefs), (int)ROBO_I18N_MEASURE_METRIC);
    /* Preference lists are still region-US; length stays mile from CLDR prefs. */
    ROBO_CHECK_EQ_INT(robo_i18n_measure_preferred_unit(&prefs, ROBO_I18N_MEASURE_CAT_LENGTH, &got),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)got, (int)ROBO_I18N_UNIT_LENGTH_MILE);

    parse_tag("fr-FR-u-ms-ussystem", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_measure_prefs_system(&prefs), (int)ROBO_I18N_MEASURE_US);
}

static void test_meas_convert_format(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_measure_prefs_t prefs;
    robo_i18n_unit_formatter_t fmt;
    robo_i18n_unit_options_t opts;
    char buf[128];
    size_t len = 0;
    robo_i18n_decimal_t din;
    robo_i18n_decimal_t dout;
    int approx = 0;
    double f;

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    robo_i18n_unit_options_init(&opts);
    opts.width = ROBO_I18N_UNIT_WIDTH_SHORT;
    ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, &opts), ROBO_OK);

    /* 0 °C → Fahrenheit preference */
    ROBO_CHECK_EQ_INT(robo_i18n_measure_format_converted_i64(
                          &prefs, &fmt, ROBO_I18N_MEASURE_CAT_TEMPERATURE,
                          ROBO_I18N_MEASURE_USAGE_DEFAULT, 0, ROBO_I18N_UNIT_TEMPERATURE_CELSIUS,
                          NULL, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "32\xC2\xB0""F");

    /* Explicit Kelvin must not be overridden */
    {
        robo_i18n_unit_id_t kelvin = ROBO_I18N_UNIT_TEMPERATURE_KELVIN;
        ROBO_CHECK_EQ_INT(robo_i18n_measure_format_converted_i64(
                              &prefs, &fmt, ROBO_I18N_MEASURE_CAT_TEMPERATURE,
                              ROBO_I18N_MEASURE_USAGE_DEFAULT, 0, ROBO_I18N_UNIT_TEMPERATURE_CELSIUS,
                              &kelvin, buf, sizeof(buf), &len),
                          ROBO_OK);
        ROBO_CHECK(strstr(buf, "K") != NULL);
    }

    robo_i18n_unit_formatter_destroy(&fmt);

    /* Conversion math checks */
    ROBO_CHECK_EQ_INT(robo_i18n_decimal_from_i64(100, &din), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_convert(ROBO_I18N_UNIT_TEMPERATURE_CELSIUS,
                                             ROBO_I18N_UNIT_TEMPERATURE_FAHRENHEIT, &din, &dout,
                                             &approx),
                      ROBO_OK);
    f = 0.0;
    {
        size_t i;
        for (i = 0; i < dout.digit_count; i++) {
            f = f * 10.0 + (double)(dout.digits[i] - '0');
        }
        for (i = 0; i < dout.scale; i++) {
            f /= 10.0;
        }
        if (dout.negative) {
            f = -f;
        }
    }
    ROBO_CHECK(fabs(f - 212.0) < 0.05);

    {
        int64_t out = 0;
        ROBO_CHECK_EQ_INT(robo_i18n_unit_convert_i64(ROBO_I18N_UNIT_LENGTH_KILOMETER,
                                                     ROBO_I18N_UNIT_LENGTH_METER, 5, &out, &approx),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)out, 5000);
        ROBO_CHECK_EQ_INT(approx, 0);
    }

    parse_tag("en-GB", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_format_converted_i64(
                          &prefs, &fmt, ROBO_I18N_MEASURE_CAT_LENGTH,
                          ROBO_I18N_MEASURE_USAGE_DEFAULT, 1609, ROBO_I18N_UNIT_LENGTH_METER, NULL,
                          buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK(len > 0);
    robo_i18n_unit_formatter_destroy(&fmt);
}

static void test_meas_intl_smoke(void)
{
    static const char *tags[] = {"en-US", "en-GB", "en-CA", "en-AU", "fr-FR", "de-DE",
                                 "es-ES", "ru-RU", "ar-SA", "fa-IR", "ur-PK",  "hi-IN",
                                 "bn-BD", "zh-CN", "zh-TW", "ja-JP", "ko-KR"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_measure_prefs_t prefs;
        robo_i18n_unit_id_t u = ROBO_I18N_UNIT_COUNT;
        int c;
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
        for (c = 0; c < (int)ROBO_I18N_MEASURE_CAT_COUNT; c++) {
            ROBO_CHECK_EQ_INT(robo_i18n_measure_preferred_unit(
                                  &prefs, (robo_i18n_measure_category_t)c, &u),
                              ROBO_OK);
            ROBO_CHECK((unsigned)u < (unsigned)ROBO_I18N_UNIT_COUNT);
        }
    }
}

static void test_meas_fuzz(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_measure_prefs_t prefs;
    robo_i18n_unit_id_t u = ROBO_I18N_UNIT_COUNT;
    char buf[64];
    size_t len = 0;

    ROBO_CHECK(robo_i18n_measure_prefs_init(NULL, NULL) != ROBO_OK);
    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    ROBO_CHECK(robo_i18n_measure_preferred_unit(&prefs, (robo_i18n_measure_category_t)99, &u) !=
               ROBO_OK);
    ROBO_CHECK(robo_i18n_measure_prefs_set_system(&prefs, (robo_i18n_measurement_t)9) != ROBO_OK);
    ROBO_CHECK(robo_i18n_locale_parse_canonical("!!!", 3, &loc) != ROBO_OK);

    parse_tag("xx-ZZ", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_prefs_init(&prefs, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_measure_preferred_unit(&prefs, ROBO_I18N_MEASURE_CAT_LENGTH, &u),
                      ROBO_OK);

    {
        robo_i18n_unit_formatter_t fmt;
        ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &prefs.locale, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_measure_format_converted_i64(
                              &prefs, &fmt, ROBO_I18N_MEASURE_CAT_MASS,
                              ROBO_I18N_MEASURE_USAGE_DEFAULT, 0, ROBO_I18N_UNIT_MASS_KILOGRAM,
                              NULL, buf, sizeof(buf), &len),
                          ROBO_OK);
        robo_i18n_unit_formatter_destroy(&fmt);
    }

    ROBO_CHECK_STR_EQ(robo_i18n_measure_category_name(ROBO_I18N_MEASURE_CAT_SPEED), "speed");
    ROBO_CHECK_STR_EQ(robo_i18n_measure_usage_name(ROBO_I18N_MEASURE_USAGE_FLUID), "fluid");
}

void robo_test_register_i18n_measure(void)
{
    robo_test_register("i18n.measure.versions", test_meas_versions);
    robo_test_register("i18n.measure.systems_matrix", test_meas_systems_matrix);
    robo_test_register("i18n.measure.lang_vs_region", test_meas_language_vs_region);
    robo_test_register("i18n.measure.overrides", test_meas_overrides_and_explicit);
    robo_test_register("i18n.measure.unicode_ms", test_meas_unicode_ms);
    robo_test_register("i18n.measure.convert_format", test_meas_convert_format);
    robo_test_register("i18n.measure.intl_smoke", test_meas_intl_smoke);
    robo_test_register("i18n.measure.fuzz", test_meas_fuzz);
}
