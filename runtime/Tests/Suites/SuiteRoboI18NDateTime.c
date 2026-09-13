/*
 * RoboI18N date/time/calendar/timezone suite (Goal 4.5).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboCalData.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_dt_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.14.0");
    ROBO_CHECK_STR_EQ(robo_i18n_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(ROBO_CAL_CLDR_VERSION, "46");
    ROBO_CHECK(robo_cal_week_count > 100u);
    ROBO_CHECK(robo_cal_japanese_era_start_count > 200u);
    ROBO_CHECK(robo_cal_symbols_count >= 20u);
}

static void test_gregorian_leap(void)
{
    ROBO_CHECK(robo_i18n_date_is_leap_year(2000));
    ROBO_CHECK(!robo_i18n_date_is_leap_year(1900));
    ROBO_CHECK(robo_i18n_date_is_leap_year(2004));
    ROBO_CHECK(!robo_i18n_date_is_leap_year(2100));
    ROBO_CHECK_EQ_INT((int)robo_i18n_date_days_in_month(2024, 2), 29);
    ROBO_CHECK_EQ_INT((int)robo_i18n_date_days_in_month(2023, 2), 28);
    ROBO_CHECK_EQ_INT((int)robo_i18n_date_days_in_month(2023, 4), 30);
    ROBO_CHECK_EQ_INT((int)robo_i18n_date_days_in_month(2023, 1), 31);
}

static void test_gregorian_valid(void)
{
    robo_i18n_date_t d;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK(robo_i18n_date_is_valid(&d));
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2023, 2, 29), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init_checked(&d, 2023, 2, 29, 0), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init_checked(&d, 2023, 2, 29, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(d.year, 2023);
    ROBO_CHECK_EQ_INT((int)d.month, 3);
    ROBO_CHECK_EQ_INT((int)d.day, 1);
}

static void test_gregorian_boundaries(void)
{
    robo_i18n_date_t d, n;
    robo_i18n_weekday_t wd;
    int32_t doy;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2023, 12, 31), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_days(&d, 1, &n), ROBO_OK);
    ROBO_CHECK_EQ_INT(n.year, 2024);
    ROBO_CHECK_EQ_INT((int)n.month, 1);
    ROBO_CHECK_EQ_INT((int)n.day, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2024, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_day_of_year(&d, &doy), ROBO_OK);
    ROBO_CHECK_EQ_INT(doy, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2024, 12, 31), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_day_of_year(&d, &doy), ROBO_OK);
    ROBO_CHECK_EQ_INT(doy, 366);
    /* 2026-09-13 is Sunday */
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_day_of_week(&d, &wd), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)wd, (int)ROBO_I18N_WEEKDAY_SUN);
}

static void test_iso_week(void)
{
    robo_i18n_date_t d, back;
    int32_t wy;
    uint8_t week;
    robo_i18n_weekday_t wd;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2026, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_to_iso_week(&d, &wy, &week, &wd), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_from_iso_week(wy, week, wd, &back), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_compare(&d, &back), 0);
    /* Known: 2005-01-01 is ISO week 2004-W53 */
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2005, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_to_iso_week(&d, &wy, &week, &wd), ROBO_OK);
    ROBO_CHECK_EQ_INT(wy, 2004);
    ROBO_CHECK_EQ_INT((int)week, 53);
}

static void test_time_primitives(void)
{
    robo_i18n_time_t t;
    int64_t nanos;
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&t, 0, 0, 0, 0), ROBO_OK);
    ROBO_CHECK(robo_i18n_time_is_valid(&t));
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&t, 23, 59, 59, 999999999u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&t, 24, 0, 0, 0), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&t, 0, 0, 0, 1000000000u), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&t, 12, 30, 45, 123456789u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_to_nanos_of_day(&t, &nanos), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_from_nanos_of_day(nanos, &t), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)t.hour, 12);
    ROBO_CHECK_EQ_INT((int)t.minute, 30);
    ROBO_CHECK_EQ_INT((int)t.second, 45);
    ROBO_CHECK_EQ_INT((int)t.nano, 123456789);
}

static void test_instant_roundtrip(void)
{
    robo_i18n_datetime_t utc, back;
    robo_i18n_instant_t inst, epoch, before, after;
    int64_t ms;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 1970, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 0, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &epoch), ROBO_OK);
    ROBO_CHECK_EQ_INT(epoch.unix_ms, 0);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_to_utc(&epoch, &back), ROBO_OK);
    ROBO_CHECK_EQ_INT(back.date.year, 1970);

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 1969, 12, 31), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 23, 59, 59, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &before), ROBO_OK);
    ROBO_CHECK(before.unix_ms < 0);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_to_utc(&before, &back), ROBO_OK);
    ROBO_CHECK_EQ_INT(back.date.year, 1969);
    ROBO_CHECK_EQ_INT((int)back.date.month, 12);
    ROBO_CHECK_EQ_INT((int)back.date.day, 31);

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 6, 19, 0, 500000000u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &inst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_to_utc(&inst, &back), ROBO_OK);
    ROBO_CHECK_EQ_INT(back.date.year, 2026);
    ROBO_CHECK_EQ_INT((int)back.date.month, 9);
    ROBO_CHECK_EQ_INT((int)back.date.day, 13);
    ROBO_CHECK_EQ_INT((int)back.time.hour, 6);
    ROBO_CHECK_EQ_INT((int)back.time.minute, 19);
    /* nano truncated to ms in instant representation */
    ROBO_CHECK_EQ_INT((int)back.time.nano, 500000000);

    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_unix_s(1, &after), ROBO_OK);
    ROBO_CHECK_EQ_INT(after.unix_ms, 1000);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_to_unix_ms(&after, &ms), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ms, 1000);
}

static void test_timezone_fixed(void)
{
    robo_i18n_timezone_t utc, karachi, neg, iana, sys;
    robo_i18n_instant_t inst;
    robo_i18n_datetime_t local, utc_dt;
    robo_i18n_tz_resolve_t res;
    int32_t off;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_utc(&utc), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_timezone_kind(&utc), (int)ROBO_I18N_TZ_UTC);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("+05:00", 6, &karachi), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_timezone_kind(&karachi), (int)ROBO_I18N_TZ_FIXED);
    ROBO_CHECK_EQ_INT(karachi.offset_seconds, 5 * 3600);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("-08:00", 6, &neg), ROBO_OK);
    ROBO_CHECK_EQ_INT(neg.offset_seconds, -8 * 3600);

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc_dt.date, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc_dt.time, 6, 19, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc_dt, &inst), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&karachi, &inst, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(off, 5 * 3600);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_local(&karachi, &inst, &local, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res, (int)ROBO_I18N_TZ_UNIQUE);
    ROBO_CHECK_EQ_INT((int)local.time.hour, 11);
    ROBO_CHECK_EQ_INT((int)local.time.minute, 19);

    {
        robo_i18n_instant_t back;
        ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_utc(&karachi, &local, &back, &res), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_instant_compare(&inst, &back), 0);
    }

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &iana), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_timezone_kind(&iana), (int)ROBO_I18N_TZ_IANA);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&iana, &inst, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(off, 5 * 3600);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("UTC+05:30", 9, &karachi), ROBO_OK);
    ROBO_CHECK_EQ_INT(karachi.offset_seconds, 5 * 3600 + 30 * 60);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("local", 5, &sys), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_timezone_kind(&sys), (int)ROBO_I18N_TZ_SYSTEM);
}

static void test_calendar_conversions(void)
{
    robo_i18n_calendar_t cal;
    robo_i18n_date_t g;
    robo_i18n_calendar_fields_t f, back;

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("gregorian", 9, &cal), ROBO_OK);
    ROBO_CHECK(robo_i18n_calendar_has_conversion(&cal));
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&g, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &f), ROBO_OK);
    ROBO_CHECK_EQ_INT(f.year, 2026);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("buddhist", 8, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &f), ROBO_OK);
    ROBO_CHECK_EQ_INT(f.year, 2026 + 543);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_to_gregorian(&cal, &f, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(g.year, 2026);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("japanese", 8, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&g, 2019, 5, 1), ROBO_OK); /* Reiwa starts */
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &f), ROBO_OK);
    ROBO_CHECK_EQ_INT(f.year, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_to_gregorian(&cal, &f, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(g.year, 2019);
    ROBO_CHECK_EQ_INT((int)g.month, 5);
    ROBO_CHECK_EQ_INT((int)g.day, 1);

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&g, 2019, 4, 30), ROBO_OK); /* Heisei last day */
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_to_gregorian(&cal, &f, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(g.year, 2019);
    ROBO_CHECK_EQ_INT((int)g.month, 4);
    ROBO_CHECK_EQ_INT((int)g.day, 30);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("islamic", 7, &cal), ROBO_OK);
    ROBO_CHECK(!robo_i18n_calendar_has_conversion(&cal));
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &back), ROBO_NOT_SUPPORTED);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("persian", 7, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &back), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("hebrew", 6, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &back), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("chinese", 7, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &back), ROBO_NOT_SUPPORTED);
}

static void test_calendar_metadata_locales(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko"
    };
    size_t i;
    robo_i18n_calendar_t greg;
    const char *name;
    const char *pat;

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_kind(ROBO_I18N_CAL_GREGORIAN, &greg), ROBO_OK);
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_weekday_t first, ws, we;
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_locale_first_day_of_week(&loc, &first), ROBO_OK);
        ROBO_CHECK(first >= 1 && first <= 7);
        ROBO_CHECK_EQ_INT(robo_i18n_locale_weekend(&loc, &ws, &we), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_month_name(&loc, &greg, 1, ROBO_I18N_CAL_NAME_WIDE, &name),
            ROBO_OK);
        ROBO_CHECK(name && name[0]);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_weekday_name(
                &loc, &greg, ROBO_I18N_WEEKDAY_MON, ROBO_I18N_CAL_NAME_ABBREVIATED, &name),
            ROBO_OK);
        ROBO_CHECK(name && name[0]);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_date_pattern(&loc, &greg, ROBO_I18N_CAL_STYLE_MEDIUM, &pat),
            ROBO_OK);
        ROBO_CHECK(pat && pat[0]);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_time_pattern(&loc, &greg, ROBO_I18N_CAL_STYLE_SHORT, &pat),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_day_period_name(
                &loc, &greg, "am", ROBO_I18N_CAL_NAME_ABBREVIATED, &name),
            ROBO_OK);
    }

    {
        robo_i18n_locale_t ja, th, en_us, sa;
        robo_i18n_calendar_t jap, bud;
        robo_i18n_weekday_t first;
        const char *era;
        parse_tag("ja", &ja);
        parse_tag("th", &th);
        parse_tag("en-US", &en_us);
        parse_tag("sa", &sa); /* Saudi — weekend Fri-Sat typically */
        ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_kind(ROBO_I18N_CAL_JAPANESE, &jap), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_kind(ROBO_I18N_CAL_BUDDHIST, &bud), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_era_name(&ja, &jap, 236, ROBO_I18N_CAL_NAME_ABBREVIATED, &era),
            ROBO_OK); /* Reiwa */
        ROBO_CHECK(era && era[0]);
        ROBO_CHECK_EQ_INT(
            robo_i18n_calendar_month_name(&th, &bud, 1, ROBO_I18N_CAL_NAME_WIDE, &era), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_locale_first_day_of_week(&en_us, &first), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)first, (int)ROBO_I18N_WEEKDAY_SUN);
    }
}

static void test_datetime_fuzz_smoke(void)
{
    int32_t y;
    uint8_t m, d;
    robo_i18n_date_t date, round;
    int64_t rd;
    for (y = 1900; y <= 2100; y += 17) {
        for (m = 1; m <= 12; m++) {
            d = robo_i18n_date_days_in_month(y, m);
            if (robo_i18n_date_init(&date, y, m, d) != ROBO_OK) {
                continue;
            }
            ROBO_CHECK_EQ_INT(robo_i18n_date_to_rata_die(&date, &rd), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_i18n_date_from_rata_die(rd, &round), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_i18n_date_compare(&date, &round), 0);
        }
    }
    /* Invalid timezone ids must not crash */
    {
        robo_i18n_timezone_t tz;
        ROBO_CHECK(robo_i18n_timezone_from_id("", 0, &tz) != ROBO_OK);
        ROBO_CHECK(robo_i18n_timezone_from_id("@@@", 3, &tz) != ROBO_OK);
        ROBO_CHECK(robo_i18n_timezone_from_id("+99:99", 6, &tz) != ROBO_OK);
    }
}

void robo_test_register_i18n_datetime(void)
{
    robo_test_register("i18n.datetime.versions", test_dt_versions);
    robo_test_register("i18n.datetime.gregorian_leap", test_gregorian_leap);
    robo_test_register("i18n.datetime.gregorian_valid", test_gregorian_valid);
    robo_test_register("i18n.datetime.gregorian_boundaries", test_gregorian_boundaries);
    robo_test_register("i18n.datetime.iso_week", test_iso_week);
    robo_test_register("i18n.datetime.time", test_time_primitives);
    robo_test_register("i18n.datetime.instant", test_instant_roundtrip);
    robo_test_register("i18n.datetime.timezone", test_timezone_fixed);
    robo_test_register("i18n.datetime.calendars", test_calendar_conversions);
    robo_test_register("i18n.datetime.metadata_locales", test_calendar_metadata_locales);
    robo_test_register("i18n.datetime.fuzz_smoke", test_datetime_fuzz_smoke);
}
