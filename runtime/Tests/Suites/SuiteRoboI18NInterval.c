/*
 * RoboI18N interval + relative-time suites (Goal 4.7).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboRelData.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_interval_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.22.0");
    ROBO_CHECK_STR_EQ(ROBO_CAL_CLDR_VERSION, "46");
    ROBO_CHECK(robo_cal_interval_formats_count > 1000u);
    ROBO_CHECK(robo_cal_interval_fallbacks_count > 10u);
}

static void test_interval_model(void)
{
    robo_i18n_datetime_t a, b;
    robo_i18n_datetime_interval_t iv;

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&a.date, 2026, 1, 12), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&a.time, 0, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&b.date, 2026, 1, 10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&b.time, 0, 0, 0, 0), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_datetime_interval_init(&iv, &a, &b, 0), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_interval_init(&iv, &a, &b, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(iv.start.date.day, 10);
    ROBO_CHECK_EQ_INT(iv.end.date.day, 12);

    ROBO_CHECK_EQ_INT(robo_i18n_datetime_interval_init(&iv, &b, &b, 0), ROBO_OK);
}

static void test_interval_en_collapse(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_interval_formatter_t fmt;
    robo_i18n_interval_options_t opts;
    robo_i18n_date_t s, e;
    char buf[128];
    size_t len = 0;
    char diff[2];
    robo_i18n_datetime_t ds, de;

    parse_tag("en", &loc);
    robo_i18n_interval_options_init(&opts);
    memcpy(opts.skeleton, "yMMMd", 6);
    ROBO_CHECK_EQ_INT(robo_i18n_interval_formatter_init(&fmt, &loc, NULL, NULL, &opts), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&s, 2026, 1, 10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&e, 2026, 1, 12), ROBO_OK);
    memset(&ds, 0, sizeof(ds));
    memset(&de, 0, sizeof(de));
    ds.date = s;
    de.date = e;
    ROBO_CHECK_EQ_INT(robo_i18n_interval_greatest_difference(&ds, &de, diff, sizeof(diff)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diff[0], (int)'d');

    ROBO_CHECK_EQ_INT(robo_i18n_date_interval_format(&fmt, &s, &e, buf, sizeof(buf), &len),
                      ROBO_OK);
    /* U+2009 thin space around en dash */
    ROBO_CHECK_STR_EQ(buf, "Jan 10\xE2\x80\x89\xE2\x80\x93\xE2\x80\x89""12, 2026");

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&e, 2026, 2, 12), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_interval_format(&fmt, &s, &e, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Jan 10\xE2\x80\x89\xE2\x80\x93\xE2\x80\x89""Feb 12, 2026");

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&e, 2027, 1, 12), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_interval_format(&fmt, &s, &e, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "2026") != NULL);
    ROBO_CHECK(strstr(buf, "2027") != NULL);

    /* Same date → single format */
    ROBO_CHECK_EQ_INT(robo_i18n_date_interval_format(&fmt, &s, &s, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Jan 10, 2026");

    robo_i18n_interval_formatter_destroy(&fmt);
}

static void test_interval_time_and_locales(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_interval_formatter_t fmt;
    robo_i18n_interval_options_t opts;
    robo_i18n_time_t s, e;
    char buf[128];
    size_t len = 0;

    parse_tag("en", &loc);
    robo_i18n_interval_options_init(&opts);
    memcpy(opts.skeleton, "Hm", 3);
    ROBO_CHECK_EQ_INT(robo_i18n_interval_formatter_init(&fmt, &loc, NULL, NULL, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&s, 9, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&e, 11, 30, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_interval_format(&fmt, &s, &e, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "09:00") != NULL || strstr(buf, "9:00") != NULL);
    ROBO_CHECK(strstr(buf, "11:30") != NULL);
    robo_i18n_interval_formatter_destroy(&fmt);

    parse_tag("ja", &loc);
    robo_i18n_interval_options_init(&opts);
    memcpy(opts.skeleton, "yMMMd", 6);
    ROBO_CHECK_EQ_INT(robo_i18n_interval_formatter_init(&fmt, &loc, NULL, NULL, &opts), ROBO_OK);
    {
        robo_i18n_date_t d0, d1;
        ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d0, 2026, 1, 10), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d1, 2026, 1, 12), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_interval_format(&fmt, &d0, &d1, buf, sizeof(buf), &len),
                          ROBO_OK);
        ROBO_CHECK(buf[0] != '\0');
    }
    robo_i18n_interval_formatter_destroy(&fmt);

    /* Unsupported calendar */
    {
        robo_i18n_calendar_t cal;
        parse_tag("en", &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("islamic", 7, &cal), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_interval_formatter_init(&fmt, &loc, &cal, NULL, NULL),
                          ROBO_NOT_SUPPORTED);
    }
}

static void test_interval_buddhist(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_calendar_t cal;
    robo_i18n_interval_formatter_t fmt;
    robo_i18n_interval_options_t opts;
    robo_i18n_date_t s, e;
    char buf[128];
    size_t len = 0;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("buddhist", 8, &cal), ROBO_OK);
    robo_i18n_interval_options_init(&opts);
    memcpy(opts.skeleton, "y", 2);
    ROBO_CHECK_EQ_INT(robo_i18n_interval_formatter_init(&fmt, &loc, &cal, NULL, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&s, 2026, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&e, 2027, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_interval_format(&fmt, &s, &e, buf, sizeof(buf), &len),
                      ROBO_OK);
    /* Buddhist years 2569–2570 */
    ROBO_CHECK(strstr(buf, "2569") != NULL);
    ROBO_CHECK(strstr(buf, "2570") != NULL);
    robo_i18n_interval_formatter_destroy(&fmt);
}

static void test_interval_fuzz(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_interval_formatter_t fmt;
    robo_i18n_date_t s, e;
    char buf[64];
    size_t len = 0;
    int y;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_interval_formatter_init(&fmt, &loc, NULL, NULL, NULL), ROBO_OK);
    for (y = 2000; y <= 2030; y += 3) {
        ROBO_CHECK_EQ_INT(robo_i18n_date_init(&s, y, 3, 1), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_init(&e, y, 3, 15), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_interval_format(&fmt, &s, &e, buf, sizeof(buf), &len),
                          ROBO_OK);
    }
    robo_i18n_interval_formatter_destroy(&fmt);
}

void robo_test_register_i18n_interval(void)
{
    robo_test_register("i18n.interval.versions", test_interval_versions);
    robo_test_register("i18n.interval.model", test_interval_model);
    robo_test_register("i18n.interval.en_collapse", test_interval_en_collapse);
    robo_test_register("i18n.interval.time_locales", test_interval_time_and_locales);
    robo_test_register("i18n.interval.buddhist", test_interval_buddhist);
    robo_test_register("i18n.interval.fuzz", test_interval_fuzz);
}
