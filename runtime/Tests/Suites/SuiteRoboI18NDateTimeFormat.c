/*
 * RoboI18N localized date/time formatting suite (Goal 4.6).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboCalData.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_date(const char *tag,
                        robo_i18n_cal_format_style_t style,
                        int y,
                        int m,
                        int d,
                        const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_dtfmt_options_t opts;
    robo_i18n_date_t date;
    char buf[192];
    size_t len = 0;

    parse_tag(tag, &loc);
    robo_i18n_dtfmt_options_init(&opts);
    opts.date_style = style;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_formatter_init(&fmt, &loc, NULL, NULL, ROBO_I18N_DTFMT_DATE,
                                                        &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, y, (uint8_t)m, (uint8_t)d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &len), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    robo_i18n_datetime_formatter_destroy(&fmt);
}

static void expect_time(const char *tag,
                        robo_i18n_cal_format_style_t style,
                        int h,
                        int mi,
                        int s,
                        const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_dtfmt_options_t opts;
    robo_i18n_time_t time;
    char buf[192];
    size_t len = 0;

    parse_tag(tag, &loc);
    robo_i18n_dtfmt_options_init(&opts);
    opts.time_style = style;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_formatter_init(&fmt, &loc, NULL, NULL, ROBO_I18N_DTFMT_TIME,
                                                        &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&time, (uint8_t)h, (uint8_t)mi, (uint8_t)s, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_time(&fmt, &time, buf, sizeof(buf), &len), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    robo_i18n_datetime_formatter_destroy(&fmt);
}

static void test_dtfmt_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.15.0");
    ROBO_CHECK_STR_EQ(robo_i18n_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(ROBO_CAL_CLDR_VERSION, "46");
    ROBO_CHECK(robo_cal_available_formats_count > 1000u);
}

static void test_en_styles(void)
{
    /* 2026-09-13 is Sunday */
    expect_date("en", ROBO_I18N_CAL_STYLE_FULL, 2026, 9, 13, "Sunday, September 13, 2026");
    expect_date("en", ROBO_I18N_CAL_STYLE_LONG, 2026, 9, 13, "September 13, 2026");
    expect_date("en", ROBO_I18N_CAL_STYLE_MEDIUM, 2026, 9, 13, "Sep 13, 2026");
    expect_date("en", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13, "9/13/26");
    expect_date("en-US", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13, "9/13/26");
    expect_date("en-GB", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13, "13/09/2026");
    expect_date("en-GB", ROBO_I18N_CAL_STYLE_MEDIUM, 2026, 9, 13, "13 Sept 2026");

    /* NNBSP (U+202F) before day period in en */
    expect_time("en", ROBO_I18N_CAL_STYLE_SHORT, 15, 45, 0, "3:45\xE2\x80\xAF""PM");
    expect_time("en", ROBO_I18N_CAL_STYLE_MEDIUM, 15, 45, 30, "3:45:30\xE2\x80\xAF""PM");
    expect_time("en-GB", ROBO_I18N_CAL_STYLE_SHORT, 15, 45, 0, "15:45");
    expect_time("en-GB", ROBO_I18N_CAL_STYLE_MEDIUM, 15, 45, 30, "15:45:30");
}

static void test_en_datetime_combo(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_dtfmt_options_t opts;
    robo_i18n_datetime_t dt;
    char buf[256];
    size_t len = 0;

    parse_tag("en", &loc);
    robo_i18n_dtfmt_options_init(&opts);
    opts.date_style = ROBO_I18N_CAL_STYLE_MEDIUM;
    opts.time_style = ROBO_I18N_CAL_STYLE_MEDIUM;
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init(&fmt, &loc, NULL, NULL, ROBO_I18N_DTFMT_DATETIME, &opts),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&dt.date, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&dt.time, 15, 45, 30, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_datetime(&fmt, &dt, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Sep 13, 2026, 3:45:30\xE2\x80\xAF""PM");
    robo_i18n_datetime_formatter_destroy(&fmt);
}

static void test_non_latin_dates(void)
{
    /* Arabic/Persian digits require region locales (CLDR default digits). */
    expect_date("ar-SA", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13,
                "\xD9\xA1\xD9\xA3\xE2\x80\x8F/\xD9\xA9\xE2\x80\x8F/\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA6");
    expect_date("ar-SA", ROBO_I18N_CAL_STYLE_LONG, 2026, 9, 13,
                "\xD9\xA1\xD9\xA3 \xD8\xB3\xD8\xA8\xD8\xAA\xD9\x85\xD8\xA8\xD8\xB1 "
                "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA6");

    /* Persian digits */
    expect_date("fa-IR", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13,
                "\xDB\xB2\xDB\xB0\xDB\xB2\xDB\xB6/\xDB\xB9/\xDB\xB1\xDB\xB3");

    expect_date("ja", ROBO_I18N_CAL_STYLE_LONG, 2026, 9, 13,
                "2026\xE5\xB9\xB4"
                "9"
                "\xE6\x9C\x88"
                "13"
                "\xE6\x97\xA5");
    expect_date("zh", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13, "2026/9/13");
    expect_date("ko", ROBO_I18N_CAL_STYLE_MEDIUM, 2026, 9, 13, "2026. 9. 13.");
    expect_date("hi", ROBO_I18N_CAL_STYLE_MEDIUM, 2026, 9, 13,
                "13 \xE0\xA4\xB8\xE0\xA4\xBF\xE0\xA4\xA4\xE0\xA5\xB0 2026");
}

static void test_ur_bn_he_th(void)
{
    expect_date("ur", ROBO_I18N_CAL_STYLE_MEDIUM, 2026, 9, 13,
                "13 \xD8\xB3\xD8\xAA\xD9\x85\xD8\xA8\xD8\xB1\xD8\x8C 2026");
    expect_date("bn", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13,
                "\xE0\xA7\xA7\xE0\xA7\xA9/\xE0\xA7\xAF/\xE0\xA7\xA8\xE0\xA7\xAC");
    expect_date("he", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13, "13.9.2026");
    expect_date("th", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13, "13/9/26");
}

static void test_day_periods_and_names(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_time_t time;
    char buf[64];
    size_t len = 0;
    const char *name;

    parse_tag("ja", &loc);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, NULL, NULL, "a", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&time, 9, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_time(&fmt, &time, buf, sizeof(buf), &len), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "\xE5\x8D\x88\xE5\x89\x8D"); /* 午前 */
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&time, 15, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_time(&fmt, &time, buf, sizeof(buf), &len), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "\xE5\x8D\x88\xE5\xBE\x8C"); /* 午後 */
    robo_i18n_datetime_formatter_destroy(&fmt);

    parse_tag("ar", &loc);
    {
        robo_i18n_calendar_t greg;
        ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_kind(ROBO_I18N_CAL_GREGORIAN, &greg), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_month_name(&loc, &greg, 9, ROBO_I18N_CAL_NAME_NARROW, &name),
            ROBO_OK);
        ROBO_CHECK(name && name[0]);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_weekday_name(&loc, &greg, ROBO_I18N_WEEKDAY_SUN,
                                            ROBO_I18N_CAL_NAME_NARROW, &name),
            ROBO_OK);
        ROBO_CHECK(name && name[0]);
    }
}

static void test_skeletons(void)
{
    robo_i18n_locale_t loc;
    const char *pat = NULL;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_date_t date;
    char buf[96];
    size_t len = 0;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_resolve_skeleton(&loc, NULL, "yMMMd", &pat), ROBO_OK);
    ROBO_CHECK_STR_EQ(pat, "MMM d, y");
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_resolve_skeleton(&loc, NULL, "Hm", &pat), ROBO_OK);
    ROBO_CHECK_STR_EQ(pat, "HH:mm");
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_resolve_skeleton(&loc, NULL, "yMd", &pat), ROBO_OK);
    ROBO_CHECK_STR_EQ(pat, "M/d/y");

    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_skeleton(&fmt, &loc, NULL, NULL, "yMMMd", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &len), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "Sep 13, 2026");
    robo_i18n_datetime_formatter_destroy(&fmt);

    /* Best-match foundation: unknown exact skeleton still resolves something usable. */
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_resolve_skeleton(&loc, NULL, "yMMMMd", &pat), ROBO_OK);
    ROBO_CHECK(pat && pat[0]);
}

static void test_calendars_format(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_calendar_t cal;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_date_t date;
    char buf[96];
    size_t len = 0;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, 2026, 9, 13), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("buddhist", 8, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, &cal, NULL, "y", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &len), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "2569");
    robo_i18n_datetime_formatter_destroy(&fmt);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("japanese", 8, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, 2019, 5, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, &cal, NULL, "Gy", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &len), ROBO_OK);
    /* Reiwa + era year 1 */
    ROBO_CHECK(strstr(buf, "1") != NULL);
    ROBO_CHECK(strstr(buf, "Reiwa") != NULL || strstr(buf, "R") != NULL);
    robo_i18n_datetime_formatter_destroy(&fmt);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("islamic", 7, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, &cal, NULL, "y", NULL),
        ROBO_NOT_SUPPORTED);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("hebrew", 6, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init(&fmt, &loc, &cal, NULL, ROBO_I18N_DTFMT_DATE, NULL),
        ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("persian", 7, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init(&fmt, &loc, &cal, NULL, ROBO_I18N_DTFMT_DATE, NULL),
        ROBO_NOT_SUPPORTED);
}

static void test_timezone_display(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_instant_t inst;
    char buf[96];
    size_t len = 0;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_fixed_seconds(5 * 3600, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, NULL, &tz, "XXX", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_unix_ms(0, &inst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_instant(&fmt, &inst, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "+05:00");
    robo_i18n_datetime_formatter_destroy(&fmt);

    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, NULL, &tz, "O", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_instant(&fmt, &inst, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "GMT") != NULL);
    ROBO_CHECK(strstr(buf, "05") != NULL);
    robo_i18n_datetime_formatter_destroy(&fmt);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, NULL, &tz, "y-MM-dd HH:mm", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_instant(&fmt, &inst, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "1970-01-01 05:00");
    robo_i18n_datetime_formatter_destroy(&fmt);
}

static void test_fa_datetime_order(void)
{
    /* Persian combo uses "ساعت" between date and time — not a plain space concat. */
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_dtfmt_options_t opts;
    robo_i18n_datetime_t dt;
    char buf[256];
    size_t len = 0;

    parse_tag("fa", &loc);
    robo_i18n_dtfmt_options_init(&opts);
    opts.date_style = ROBO_I18N_CAL_STYLE_FULL;
    opts.time_style = ROBO_I18N_CAL_STYLE_SHORT;
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init(&fmt, &loc, NULL, NULL, ROBO_I18N_DTFMT_DATETIME, &opts),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&dt.date, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&dt.time, 15, 45, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_datetime(&fmt, &dt, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "\xD8\xB3\xD8\xA7\xD8\xB9\xD8\xAA") != NULL); /* ساعت */
    robo_i18n_datetime_formatter_destroy(&fmt);
}

static void test_pattern_fuzz(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_date_t date;
    char buf[128];
    size_t len = 0;
    const char *pats[] = {
        "",
        "'",
        "''''",
        "yyyy'MMMM'dd",
        "QQQQ",
        "zzzzzzzz",
        "y"
    };
    size_t i;

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, 2026, 1, 1), ROBO_OK);

    ROBO_CHECK(robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, NULL, NULL, "", NULL) !=
               ROBO_OK);
    ROBO_CHECK(robo_i18n_datetime_formatter_init_skeleton(&fmt, &loc, NULL, NULL, "", NULL) !=
               ROBO_OK);
    ROBO_CHECK(robo_i18n_datetime_formatter_init_skeleton(&fmt, &loc, NULL, NULL, "@@@", NULL) !=
               ROBO_OK);

    for (i = 2; i < 6; i++) {
        robo_result_t r =
            robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, NULL, NULL, pats[i], NULL);
        if (r == ROBO_OK) {
            (void)robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &len);
            robo_i18n_datetime_formatter_destroy(&fmt);
        }
    }

    /* Extreme dates */
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, ROBO_I18N_DATE_YEAR_MIN, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &loc, NULL, NULL, "y-MM-dd", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &len), ROBO_OK);
    robo_i18n_datetime_formatter_destroy(&fmt);
}

void robo_test_register_i18n_datetime_format(void)
{
    robo_test_register("i18n.dtfmt.versions", test_dtfmt_versions);
    robo_test_register("i18n.dtfmt.en_styles", test_en_styles);
    robo_test_register("i18n.dtfmt.en_datetime", test_en_datetime_combo);
    robo_test_register("i18n.dtfmt.non_latin", test_non_latin_dates);
    robo_test_register("i18n.dtfmt.ur_bn_he_th", test_ur_bn_he_th);
    robo_test_register("i18n.dtfmt.day_periods", test_day_periods_and_names);
    robo_test_register("i18n.dtfmt.skeletons", test_skeletons);
    robo_test_register("i18n.dtfmt.calendars", test_calendars_format);
    robo_test_register("i18n.dtfmt.timezone", test_timezone_display);
    robo_test_register("i18n.dtfmt.fa_combo", test_fa_datetime_order);
    robo_test_register("i18n.dtfmt.fuzz", test_pattern_fuzz);
}
