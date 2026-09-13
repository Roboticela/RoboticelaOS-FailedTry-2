/*
 * RoboI18N unit formatting + relative ±2 suite (Goal 4.19).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboUnitData.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_unit(const char *tag,
                        robo_i18n_unit_width_t width,
                        int64_t value,
                        robo_i18n_unit_id_t unit,
                        const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_unit_formatter_t fmt;
    robo_i18n_unit_options_t opts;
    char buf[192];
    size_t len = 0;

    parse_tag(tag, &loc);
    robo_i18n_unit_options_init(&opts);
    opts.width = width;
    ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_format_i64(&fmt, value, unit, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    robo_i18n_unit_formatter_destroy(&fmt);
}

static void test_unit_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.22.0");
    ROBO_CHECK_STR_EQ(robo_i18n_unit_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(ROBO_UNIT_CLDR_VERSION, "46");
    ROBO_CHECK_EQ_INT((int)ROBO_I18N_UNIT_COUNT, (int)ROBO_UNIT_ID_COUNT);
    ROBO_CHECK(robo_unit_rows_count > 1000u);
}

static void test_unit_en_styles(void)
{
    expect_unit("en", ROBO_I18N_UNIT_WIDTH_LONG, 1, ROBO_I18N_UNIT_LENGTH_METER, "1 meter");
    expect_unit("en", ROBO_I18N_UNIT_WIDTH_LONG, 2, ROBO_I18N_UNIT_LENGTH_METER, "2 meters");
    expect_unit("en", ROBO_I18N_UNIT_WIDTH_SHORT, 1, ROBO_I18N_UNIT_LENGTH_METER, "1 m");
    expect_unit("en", ROBO_I18N_UNIT_WIDTH_NARROW, 5, ROBO_I18N_UNIT_LENGTH_METER, "5m");
    expect_unit("en", ROBO_I18N_UNIT_WIDTH_SHORT, 90, ROBO_I18N_UNIT_SPEED_KILOMETER_PER_HOUR,
                "90 km/h");
    expect_unit("en", ROBO_I18N_UNIT_WIDTH_LONG, 7,
                ROBO_I18N_UNIT_CONSUMPTION_LITER_PER_100_KILOMETER,
                "7 liters per 100 kilometers");
}

static void test_unit_locales(void)
{
    /* French long meter uses NBSP (U+00A0) before unit. */
    expect_unit("fr", ROBO_I18N_UNIT_WIDTH_LONG, 1, ROBO_I18N_UNIT_LENGTH_METER,
                "1\xC2\xA0m\xC3\xA8tre");
    expect_unit("de", ROBO_I18N_UNIT_WIDTH_LONG, 1, ROBO_I18N_UNIT_LENGTH_METER, "1 Meter");
    expect_unit("es", ROBO_I18N_UNIT_WIDTH_LONG, 2, ROBO_I18N_UNIT_LENGTH_METER, "2 metros");
    /* Arabic: cardinal one drops the number placeholder. */
    expect_unit("ar", ROBO_I18N_UNIT_WIDTH_LONG, 1, ROBO_I18N_UNIT_LENGTH_METER, "\xD9\x85\xD8\xAA\xD8\xB1");
    expect_unit("ru", ROBO_I18N_UNIT_WIDTH_LONG, 1, ROBO_I18N_UNIT_LENGTH_METER,
                "1 \xD0\xBC\xD0\xB5\xD1\x82\xD1\x80");
    expect_unit("pl", ROBO_I18N_UNIT_WIDTH_LONG, 2, ROBO_I18N_UNIT_LENGTH_METER, "2 metry");
    expect_unit("ja", ROBO_I18N_UNIT_WIDTH_LONG, 3, ROBO_I18N_UNIT_LENGTH_METER,
                "3 \xE3\x83\xA1\xE3\x83\xBC\xE3\x83\x88\xE3\x83\xAB");
    expect_unit("zh", ROBO_I18N_UNIT_WIDTH_LONG, 3, ROBO_I18N_UNIT_LENGTH_METER,
                "3\xE7\xB1\xB3");
    expect_unit("ko", ROBO_I18N_UNIT_WIDTH_SHORT, 3, ROBO_I18N_UNIT_LENGTH_METER, "3m");
    expect_unit("hi", ROBO_I18N_UNIT_WIDTH_LONG, 1, ROBO_I18N_UNIT_LENGTH_METER,
                "1 \xE0\xA4\xAE\xE0\xA5\x80\xE0\xA4\x9F\xE0\xA4\xB0");
}

static void test_unit_compound_per(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_unit_formatter_t fmt;
    robo_i18n_unit_options_t opts;
    char buf[192];
    size_t len = 0;

    parse_tag("en", &loc);
    robo_i18n_unit_options_init(&opts);
    opts.width = ROBO_I18N_UNIT_WIDTH_SHORT;
    ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_format_per_i64(&fmt, 5, ROBO_I18N_UNIT_DIGITAL_MEGABYTE,
                                                    ROBO_I18N_UNIT_DURATION_SECOND, buf,
                                                    sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "5 MB/s");
    robo_i18n_unit_formatter_destroy(&fmt);

    robo_i18n_unit_options_init(&opts);
    opts.width = ROBO_I18N_UNIT_WIDTH_LONG;
    ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_format_per_i64(&fmt, 5, ROBO_I18N_UNIT_DIGITAL_MEGABYTE,
                                                    ROBO_I18N_UNIT_DURATION_SECOND, buf,
                                                    sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "5 megabytes per second");
    robo_i18n_unit_formatter_destroy(&fmt);

    parse_tag("fr", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_format_per_i64(&fmt, 5, ROBO_I18N_UNIT_DIGITAL_MEGABYTE,
                                                    ROBO_I18N_UNIT_DURATION_SECOND, buf,
                                                    sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK(len > 0);
    ROBO_CHECK(strstr(buf, "par seconde") != NULL || strstr(buf, "/s") != NULL);
    robo_i18n_unit_formatter_destroy(&fmt);
}

static void test_unit_convert(void)
{
    int64_t out = 0;
    int approx = 0;
    robo_i18n_decimal_t din;
    robo_i18n_decimal_t dout;

    ROBO_CHECK_EQ_INT(robo_i18n_unit_convert_i64(ROBO_I18N_UNIT_LENGTH_KILOMETER,
                                                 ROBO_I18N_UNIT_LENGTH_METER, 2, &out, &approx),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, 2000);
    ROBO_CHECK_EQ_INT(approx, 0);

    ROBO_CHECK_EQ_INT(robo_i18n_unit_convert_i64(ROBO_I18N_UNIT_LENGTH_METER,
                                                 ROBO_I18N_UNIT_LENGTH_CENTIMETER, 3, &out,
                                                 &approx),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, 300);

    ROBO_CHECK_EQ_INT(robo_i18n_decimal_from_i64(100, &din), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_convert(ROBO_I18N_UNIT_TEMPERATURE_CELSIUS,
                                             ROBO_I18N_UNIT_TEMPERATURE_FAHRENHEIT, &din, &dout,
                                             &approx),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(approx, 1);

    ROBO_CHECK(robo_i18n_unit_convert_i64(ROBO_I18N_UNIT_LENGTH_METER, ROBO_I18N_UNIT_MASS_GRAM,
                                          1, &out, &approx) != ROBO_OK);
}

static void test_unit_ids_and_fallback(void)
{
    robo_i18n_unit_id_t id = ROBO_I18N_UNIT_COUNT;
    robo_i18n_locale_t loc;
    ROBO_CHECK_EQ_INT(robo_i18n_unit_id_from_cldr_key("length-meter", &id), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_I18N_UNIT_LENGTH_METER);
    ROBO_CHECK_STR_EQ(robo_i18n_unit_cldr_key(ROBO_I18N_UNIT_LENGTH_METER), "length-meter");
    ROBO_CHECK(robo_i18n_unit_id_from_cldr_key("not-a-unit", &id) != ROBO_OK);

    parse_tag("xx-YY", &loc);
    ROBO_CHECK(robo_i18n_unit_has_patterns(&loc, ROBO_I18N_UNIT_LENGTH_METER,
                                           ROBO_I18N_UNIT_WIDTH_LONG));
    expect_unit("xx-YY", ROBO_I18N_UNIT_WIDTH_LONG, 1, ROBO_I18N_UNIT_LENGTH_METER, "1 meter");
}

static void test_unit_intl_smoke(void)
{
    static const char *tags[] = {"en", "fr", "de", "es", "ru", "pl", "ar", "fa", "ur",
                                 "he", "hi", "bn", "th", "zh", "ja", "ko", "it"};
    robo_i18n_locale_t loc;
    robo_i18n_unit_formatter_t fmt;
    char buf[160];
    size_t len = 0;
    size_t i;

    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_unit_format_i64(&fmt, 0, ROBO_I18N_UNIT_LENGTH_METER, buf,
                                                    sizeof(buf), &len),
                          ROBO_OK);
        ROBO_CHECK(len > 0);
        ROBO_CHECK_EQ_INT(robo_i18n_unit_format_i64(&fmt, -3, ROBO_I18N_UNIT_MASS_KILOGRAM, buf,
                                                    sizeof(buf), &len),
                          ROBO_OK);
        ROBO_CHECK(len > 0);
        ROBO_CHECK_EQ_INT(
            robo_i18n_unit_format_i64(&fmt, 1000000, ROBO_I18N_UNIT_DIGITAL_MEGABYTE, buf,
                                      sizeof(buf), &len),
            ROBO_OK);
        robo_i18n_unit_formatter_destroy(&fmt);
    }
}

static void test_unit_fuzz(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_unit_formatter_t fmt;
    char buf[128];
    size_t len = 0;
    int u;
    int64_t v;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_unit_formatter_init(&fmt, &loc, NULL), ROBO_OK);
    for (u = 0; u < (int)ROBO_I18N_UNIT_COUNT; u++) {
        for (v = -5; v <= 5; v++) {
            ROBO_CHECK_EQ_INT(robo_i18n_unit_format_i64(&fmt, v, (robo_i18n_unit_id_t)u, buf,
                                                        sizeof(buf), &len),
                              ROBO_OK);
        }
    }
    ROBO_CHECK(robo_i18n_unit_format_i64(&fmt, 1, (robo_i18n_unit_id_t)999, buf, sizeof(buf),
                                         &len) != ROBO_OK);
    ROBO_CHECK(robo_i18n_unit_format_per_i64(&fmt, 1, ROBO_I18N_UNIT_LENGTH_METER,
                                             (robo_i18n_unit_id_t)999, buf, sizeof(buf),
                                             &len) != ROBO_OK);
    robo_i18n_unit_formatter_destroy(&fmt);
}

static void test_rel_special_pm2(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_relative_formatter_t fmt;
    robo_i18n_relative_options_t opts;
    char buf[96];
    size_t len = 0;

    /* English has no ±2 keywords → numeric past/future. */
    parse_tag("en", &loc);
    robo_i18n_relative_options_init(&opts);
    opts.numeric = ROBO_I18N_REL_NUMERIC_AUTO;
    ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, -2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "2 days ago");
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, 2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "in 2 days");
    robo_i18n_relative_formatter_destroy(&fmt);

    parse_tag("es", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, -2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "anteayer");
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, 2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "pasado ma\xC3\xB1""ana");
    robo_i18n_relative_formatter_destroy(&fmt);

    parse_tag("de", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, -2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "vorgestern");
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, 2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "\xC3\xBC""bermorgen");
    robo_i18n_relative_formatter_destroy(&fmt);

    parse_tag("fr", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, -2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "avant-hier");
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, 2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "apr\xC3\xA8""s-demain");
    robo_i18n_relative_formatter_destroy(&fmt);

    parse_tag("pl", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, -2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "przedwczoraj");
    ROBO_CHECK_EQ_INT(
        robo_i18n_relative_time_format_unit(&fmt, 2, ROBO_I18N_REL_DAY, buf, sizeof(buf), &len),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "pojutrze");
    robo_i18n_relative_formatter_destroy(&fmt);
}

void robo_test_register_i18n_unit(void)
{
    robo_test_register("i18n.unit.versions", test_unit_versions);
    robo_test_register("i18n.unit.en_styles", test_unit_en_styles);
    robo_test_register("i18n.unit.locales", test_unit_locales);
    robo_test_register("i18n.unit.compound_per", test_unit_compound_per);
    robo_test_register("i18n.unit.convert", test_unit_convert);
    robo_test_register("i18n.unit.ids_fallback", test_unit_ids_and_fallback);
    robo_test_register("i18n.unit.intl_smoke", test_unit_intl_smoke);
    robo_test_register("i18n.unit.fuzz", test_unit_fuzz);
    robo_test_register("i18n.unit.rel_special_pm2", test_rel_special_pm2);
}
