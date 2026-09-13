/*
 * RoboI18N relative-time suite (Goal 4.7).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboRelData.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_rel(const char *tag,
                       int64_t value,
                       robo_i18n_relative_unit_t unit,
                       robo_i18n_relative_numeric_t numeric,
                       const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_relative_formatter_t fmt;
    robo_i18n_relative_options_t opts;
    char buf[128];
    size_t len = 0;

    parse_tag(tag, &loc);
    robo_i18n_relative_options_init(&opts);
    opts.numeric = numeric;
    ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_relative_time_format_unit(&fmt, value, unit, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    robo_i18n_relative_formatter_destroy(&fmt);
}

static void test_rel_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.25.0");
    ROBO_CHECK_STR_EQ(ROBO_REL_CLDR_VERSION, "46");
    ROBO_CHECK(robo_rel_units_count > 100u);
}

static void test_rel_en_auto(void)
{
    expect_rel("en", -1, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_AUTO, "yesterday");
    expect_rel("en", 0, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_AUTO, "today");
    expect_rel("en", 1, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_AUTO, "tomorrow");
    expect_rel("en", -1, ROBO_I18N_REL_YEAR, ROBO_I18N_REL_NUMERIC_AUTO, "last year");
    expect_rel("en", 1, ROBO_I18N_REL_YEAR, ROBO_I18N_REL_NUMERIC_AUTO, "next year");
}

static void test_rel_en_numeric(void)
{
    expect_rel("en", -1, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS, "1 day ago");
    expect_rel("en", 1, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS, "in 1 day");
    expect_rel("en", -3, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS, "3 days ago");
    expect_rel("en", 3, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS, "in 3 days");
    expect_rel("en", 0, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS, "in 0 days");
    expect_rel("en-GB", -2, ROBO_I18N_REL_WEEK, ROBO_I18N_REL_NUMERIC_ALWAYS, "2 weeks ago");
}

static void test_rel_plurals_slavic(void)
{
    /* Russian: 1 день, 2 дня, 5 дней */
    expect_rel("ru", -1, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS,
               "1 \xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8C \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");
    expect_rel("ru", -2, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS,
               "2 \xD0\xB4\xD0\xBD\xD1\x8F \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");
    expect_rel("ru", -5, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS,
               "5 \xD0\xB4\xD0\xBD\xD0\xB5\xD0\xB9 \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");

    /* Polish: 1 dzień temu, 2 dni temu */
    expect_rel("pl", -1, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS,
               "1 dzie\xC5\x84 temu");
    expect_rel("pl", -2, ROBO_I18N_REL_DAY, ROBO_I18N_REL_NUMERIC_ALWAYS, "2 dni temu");
}

static void test_rel_rtl_and_asian(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_relative_formatter_t fmt;
    char buf[128];
    size_t len = 0;
    static const char *tags[] = {"ar", "fa", "he", "ur", "hi", "bn", "th", "zh", "ja", "ko"};
    size_t i;

    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_i18n_relative_time_format_unit(&fmt, -3, ROBO_I18N_REL_DAY, buf, sizeof(buf),
                                                &len),
            ROBO_OK);
        ROBO_CHECK(len > 0);
        ROBO_CHECK_EQ_INT(
            robo_i18n_relative_time_format_unit(&fmt, 1, ROBO_I18N_REL_DAY, buf, sizeof(buf),
                                                &len),
            ROBO_OK);
        ROBO_CHECK(len > 0);
        robo_i18n_relative_formatter_destroy(&fmt);
    }
}

static void test_rel_units_and_widths(void)
{
    expect_rel("en", -2, ROBO_I18N_REL_HOUR, ROBO_I18N_REL_NUMERIC_ALWAYS, "2 hours ago");
    expect_rel("en", 4, ROBO_I18N_REL_MINUTE, ROBO_I18N_REL_NUMERIC_ALWAYS, "in 4 minutes");
    expect_rel("en", -1, ROBO_I18N_REL_SECOND, ROBO_I18N_REL_NUMERIC_AUTO, "1 second ago");

    {
        robo_i18n_locale_t loc;
        robo_i18n_relative_formatter_t fmt;
        robo_i18n_relative_options_t opts;
        char buf[64];
        size_t len = 0;
        parse_tag("en", &loc);
        robo_i18n_relative_options_init(&opts);
        opts.width = ROBO_I18N_REL_WIDTH_SHORT;
        opts.numeric = ROBO_I18N_REL_NUMERIC_ALWAYS;
        ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_i18n_relative_time_format_unit(&fmt, -3, ROBO_I18N_REL_DAY, buf, sizeof(buf),
                                                &len),
            ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "3 days ago");
        robo_i18n_relative_formatter_destroy(&fmt);
    }
}

static void test_rel_fuzz(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_relative_formatter_t fmt;
    char buf[96];
    size_t len = 0;
    int64_t v;
    int u;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_relative_formatter_init(&fmt, &loc, NULL), ROBO_OK);
    for (v = -20; v <= 20; v++) {
        for (u = 0; u <= (int)ROBO_I18N_REL_SECOND; u++) {
            ROBO_CHECK_EQ_INT(
                robo_i18n_relative_time_format_unit(&fmt, v, (robo_i18n_relative_unit_t)u, buf,
                                                    sizeof(buf), &len),
                ROBO_OK);
        }
    }
    ROBO_CHECK(robo_i18n_relative_time_format_unit(&fmt, 0, (robo_i18n_relative_unit_t)99, buf,
                                                   sizeof(buf), &len) != ROBO_OK);
    robo_i18n_relative_formatter_destroy(&fmt);
}

void robo_test_register_i18n_relative(void)
{
    robo_test_register("i18n.relative.versions", test_rel_versions);
    robo_test_register("i18n.relative.en_auto", test_rel_en_auto);
    robo_test_register("i18n.relative.en_numeric", test_rel_en_numeric);
    robo_test_register("i18n.relative.slavic", test_rel_plurals_slavic);
    robo_test_register("i18n.relative.rtl_asian", test_rel_rtl_and_asian);
    robo_test_register("i18n.relative.units_widths", test_rel_units_and_widths);
    robo_test_register("i18n.relative.fuzz", test_rel_fuzz);
}
