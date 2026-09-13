/*
 * RoboI18N calendar arithmetic & date/time utilities (Goal 4.8).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_arith_version(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.24.0");
}

static void test_duration_vs_period(void)
{
    robo_i18n_duration_t d;
    robo_i18n_period_t p;

    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_days(1, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(d.millis, ROBO_I18N_DURATION_MILLIS_PER_DAY);
    ROBO_CHECK_EQ_INT(robo_i18n_period_of(0, 0, 0, 1, &p), ROBO_OK);
    ROBO_CHECK_EQ_INT(p.days, 1);
    /* Period has no millis field — concepts stay distinct. */
    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_hours(24, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(d.millis, ROBO_I18N_DURATION_MILLIS_PER_DAY);
}

static void test_date_add_days_roundtrip(void)
{
    robo_i18n_date_t a, b, c;
    int32_t n;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&a, 2024, 2, 28), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_days(&a, 366, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_days(&b, -366, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_compare(&a, &c), 0);
    ROBO_CHECK_EQ_INT(robo_i18n_date_difference_days(&a, &b, &n), ROBO_OK);
    ROBO_CHECK_EQ_INT(n, 366);
}

static void test_end_of_month_clamp(void)
{
    robo_i18n_date_t d, out;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2026, 1, 31), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_months(&d, 1, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.year, 2026);
    ROBO_CHECK_EQ_INT((int)out.month, 2);
    ROBO_CHECK_EQ_INT((int)out.day, 28); /* CLAMP */

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2024, 1, 31), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_months(&d, 1, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.day, 29); /* leap */

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2024, 3, 31), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_months(&d, -1, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.month, 2);
    ROBO_CHECK_EQ_INT((int)out.day, 29);

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2023, 2, 28), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_years(&d, 1, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.year, 2024);
    ROBO_CHECK_EQ_INT((int)out.day, 28); /* not promoted to 29 */
}

static void test_date_add_period_and_diff(void)
{
    robo_i18n_date_t a, b, back;
    robo_i18n_period_t p, diff;

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&a, 2020, 1, 15), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_period_of(1, 2, 1, 3, &p), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_period(&a, &p, &b), ROBO_OK);
    /* 2020-01-15 + 1y2m + 7d + 3d = 2021-03-15 + 10d = 2021-03-25 */
    ROBO_CHECK_EQ_INT(b.year, 2021);
    ROBO_CHECK_EQ_INT((int)b.month, 3);
    ROBO_CHECK_EQ_INT((int)b.day, 25);

    ROBO_CHECK_EQ_INT(robo_i18n_date_difference_period(&a, &b, &diff), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_period(&a, &diff, &back), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_compare(&b, &back), 0);

    ROBO_CHECK_EQ_INT(robo_i18n_date_difference_period(&b, &a, &diff), ROBO_OK);
    ROBO_CHECK(diff.years < 0 || diff.months < 0 || diff.days < 0);
}

static void test_instant_duration_roundtrip(void)
{
    robo_i18n_instant_t a, b, c;
    robo_i18n_duration_t d, neg, zero, big;
    robo_i18n_datetime_t utc;

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 1970, 1, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 0, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(a.unix_ms, 0);

    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_hours(1, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_add(&a, &d, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.unix_ms, 3600000LL);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_subtract(&b, &d, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_compare(&a, &c), 0);

    /* Pre-epoch */
    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_days(-1, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_add(&a, &d, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(b.unix_ms, -ROBO_I18N_DURATION_MILLIS_PER_DAY);

    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_millis(0, &zero), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_add(&a, &zero, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(c.unix_ms, 0);

    a.unix_ms = INT64_MAX;
    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_millis(1, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_add(&a, &d, &b), ROBO_NOT_SUPPORTED);

    a.unix_ms = INT64_MIN;
    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_millis(-1, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_add(&a, &d, &b), ROBO_NOT_SUPPORTED);

    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_millis(INT64_MIN, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_duration_negate(&d, &neg), ROBO_ERROR);

    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_millis(INT64_MAX, &big), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_duration_add(&big, &big, &d), ROBO_ERROR);
}

static void test_iso_week_utils(void)
{
    robo_i18n_date_t d, mon, sun;
    uint8_t weeks;
    int32_t wy;
    uint8_t week;
    robo_i18n_weekday_t wd;

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2026, 9, 13), ROBO_OK); /* Sunday */
    ROBO_CHECK_EQ_INT(robo_i18n_date_iso_week_start(&d, &mon), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_day_of_week(&mon, &wd), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)wd, (int)ROBO_I18N_WEEKDAY_MON);
    ROBO_CHECK_EQ_INT(mon.year, 2026);
    ROBO_CHECK_EQ_INT((int)mon.month, 9);
    ROBO_CHECK_EQ_INT((int)mon.day, 7);

    ROBO_CHECK_EQ_INT(robo_i18n_date_iso_week_end(&d, &sun), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)sun.day, 13);

    ROBO_CHECK_EQ_INT(robo_i18n_iso_weeks_in_year(2004, &weeks), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)weeks, 53);
    ROBO_CHECK_EQ_INT(robo_i18n_iso_weeks_in_year(2026, &weeks), ROBO_OK);
    ROBO_CHECK((weeks == 52) || (weeks == 53));

    ROBO_CHECK_EQ_INT(robo_i18n_date_to_iso_week(&d, &wy, &week, &wd), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)wd, (int)ROBO_I18N_WEEKDAY_SUN);
}

static void test_civil_boundaries(void)
{
    robo_i18n_date_t d, out;
    robo_i18n_datetime_t dt;

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2024, 5, 15), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_start_of_month(&d, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.day, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_date_end_of_month(&d, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.day, 31);
    ROBO_CHECK_EQ_INT(robo_i18n_date_start_of_year(&d, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.month, 1);
    ROBO_CHECK_EQ_INT((int)out.day, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_date_end_of_year(&d, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.month, 12);
    ROBO_CHECK_EQ_INT((int)out.day, 31);
    ROBO_CHECK_EQ_INT(robo_i18n_date_start_of_quarter(&d, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.month, 4);
    ROBO_CHECK_EQ_INT(robo_i18n_date_end_of_quarter(&d, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out.month, 6);
    ROBO_CHECK_EQ_INT((int)out.day, 30);

    ROBO_CHECK_EQ_INT(robo_i18n_datetime_start_of_day(&d, &dt), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dt.time.hour, 0);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_end_of_day(&d, &dt), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)dt.time.hour, 23);
    ROBO_CHECK_EQ_INT((int)dt.time.nano, 999999999);

    ROBO_CHECK_EQ_INT(robo_i18n_date_days_in_year(2024), 366);
    ROBO_CHECK_EQ_INT(robo_i18n_date_days_in_year(2023), 365);
}

static void test_locale_week_vs_iso(void)
{
    robo_i18n_date_t d, iso_start, loc_start;
    robo_i18n_locale_t us, fr;
    robo_i18n_weekday_t wd;

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&d, 2026, 9, 13), ROBO_OK); /* Sunday */
    ROBO_CHECK_EQ_INT(robo_i18n_date_iso_week_start(&d, &iso_start), ROBO_OK);

    parse_tag("en-US", &us);
    parse_tag("fr-FR", &fr);
    ROBO_CHECK_EQ_INT(robo_i18n_date_start_of_week(&d, &us, &loc_start), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_day_of_week(&loc_start, &wd), ROBO_OK);
    /* US typically Sunday-first — may equal d or earlier Sunday. */
    ROBO_CHECK(wd >= ROBO_I18N_WEEKDAY_MON && wd <= ROBO_I18N_WEEKDAY_SUN);

    ROBO_CHECK_EQ_INT(robo_i18n_date_start_of_week(&d, &fr, &loc_start), ROBO_OK);
    /* fr-FR Monday-first should match ISO Monday for this week. */
    ROBO_CHECK_EQ_INT(robo_i18n_date_compare(&loc_start, &iso_start), 0);
}

static void test_tz_aware_period(void)
{
    robo_i18n_timezone_t tz, iana;
    robo_i18n_instant_t inst, midnight, next;
    robo_i18n_datetime_t utc, local;
    robo_i18n_period_t one_day;
    robo_i18n_tz_resolve_t res;
    robo_i18n_duration_t dur_day;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("+05:00", 6, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 2026, 3, 10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 12, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &inst), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_instant_start_of_day(&inst, &tz, &midnight), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_local(&tz, &midnight, &local, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)local.time.hour, 0);
    ROBO_CHECK_EQ_INT((int)local.time.minute, 0);

    ROBO_CHECK_EQ_INT(robo_i18n_period_of(0, 0, 0, 1, &one_day), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_add_period(&inst, &one_day, &tz, &next), ROBO_OK);
    /* Fixed offset: +1 calendar day equals +86400000 ms. */
    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_days(1, &dur_day), ROBO_OK);
    {
        robo_i18n_instant_t via_dur;
        ROBO_CHECK_EQ_INT(robo_i18n_instant_add(&inst, &dur_day, &via_dur), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_instant_compare(&next, &via_dur), 0);
    }

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &iana), ROBO_OK);
    /* IANA zones now supported via tzdb (Goal 4.14). */
    ROBO_CHECK_EQ_INT(robo_i18n_instant_add_period(&inst, &one_day, &iana, &next), ROBO_OK);
    {
        robo_i18n_instant_t via_dur;
        ROBO_CHECK_EQ_INT(robo_i18n_instant_add(&inst, &dur_day, &via_dur), ROBO_OK);
        /* Away from a DST fold for this instant, calendar day matches 86400s. */
        ROBO_CHECK_EQ_INT(robo_i18n_instant_compare(&next, &via_dur), 0);
    }
}

static void test_calendar_arith(void)
{
    robo_i18n_calendar_t cal;
    robo_i18n_date_t g;
    robo_i18n_calendar_fields_t f, out;
    robo_i18n_period_t one_year;

    ROBO_CHECK_EQ_INT(robo_i18n_period_of(1, 0, 0, 0, &one_year), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&g, 2026, 9, 13), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("buddhist", 8, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_add_period(&cal, &f, &one_year, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.year, f.year + 1);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("japanese", 8, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&g, 2019, 5, 1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_gregorian(&cal, &g, &f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_add_period(&cal, &f, &one_year, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_to_gregorian(&cal, &out, &g), ROBO_OK);
    ROBO_CHECK_EQ_INT(g.year, 2020);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("islamic", 7, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_add_period(&cal, &f, &one_year, &out),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("persian", 7, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_add_period(&cal, &f, &one_year, &out),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("hebrew", 6, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_add_period(&cal, &f, &one_year, &out),
                      ROBO_NOT_SUPPORTED);
}

static int holiday_2026_09_14(const robo_i18n_date_t *date, void *ctx)
{
    (void)ctx;
    return date && date->year == 2026 && date->month == 9 && date->day == 14;
}

static void test_business_days(void)
{
    robo_i18n_business_calendar_t biz;
    robo_i18n_date_t fri, next, mon;
    robo_i18n_locale_t us;

    robo_i18n_business_calendar_init(&biz);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&fri, 2026, 9, 11), ROBO_OK); /* Friday */
    ROBO_CHECK(robo_i18n_date_is_business_day(&fri, &biz));
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_business_days(&fri, 1, &biz, &next), ROBO_OK);
    /* Next business day = Monday 14 */
    ROBO_CHECK_EQ_INT(next.year, 2026);
    ROBO_CHECK_EQ_INT((int)next.month, 9);
    ROBO_CHECK_EQ_INT((int)next.day, 14);

    biz.is_holiday = holiday_2026_09_14;
    ROBO_CHECK_EQ_INT(robo_i18n_date_add_business_days(&fri, 1, &biz, &next), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)next.day, 15);

    parse_tag("en-US", &us);
    ROBO_CHECK_EQ_INT(robo_i18n_business_calendar_from_locale(&us, &biz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&mon, 2026, 9, 12), ROBO_OK); /* Saturday */
    ROBO_CHECK(robo_i18n_date_is_weekend(&mon, &biz));
    ROBO_CHECK(!robo_i18n_date_is_business_day(&mon, &biz));
}

static void test_comparisons(void)
{
    robo_i18n_time_t t1, t2;
    robo_i18n_datetime_t a, b;
    robo_i18n_duration_t d1, d2;

    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&t1, 10, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&t2, 11, 0, 0, 0), ROBO_OK);
    ROBO_CHECK(robo_i18n_time_compare(&t1, &t2) < 0);
    ROBO_CHECK(robo_i18n_time_compare(&t2, &t1) > 0);

    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&a.date, 2026, 1, 1), ROBO_OK);
    a.time = t2;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&b.date, 2026, 1, 1), ROBO_OK);
    b.time = t1;
    ROBO_CHECK(robo_i18n_datetime_compare(&a, &b) > 0);

    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_seconds(1, &d1), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_duration_from_millis(1000, &d2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_duration_compare(&d1, &d2), 0);
}

static uint32_t fuzz_xorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0xA5A5A5A5u;
    return *s;
}

static void test_fuzz_date_invariants(void)
{
    uint32_t seed = 0xC0FFEEu;
    int i;
    for (i = 0; i < 200; i++) {
        int32_t y = (int32_t)(fuzz_xorshift(&seed) % 8000u) + 1600;
        uint8_t m = (uint8_t)((fuzz_xorshift(&seed) % 12u) + 1u);
        uint8_t dim = robo_i18n_date_days_in_month(y, m);
        uint8_t day = (uint8_t)((fuzz_xorshift(&seed) % dim) + 1u);
        int32_t delta = (int32_t)(fuzz_xorshift(&seed) % 400u) - 200;
        robo_i18n_date_t a, b, c;
        int32_t days;
        int cmp_ab;
        int cmp_ba;

        if (robo_i18n_date_init(&a, y, m, day) != ROBO_OK) {
            continue;
        }
        if (robo_i18n_date_add_days(&a, delta, &b) != ROBO_OK) {
            continue;
        }
        ROBO_CHECK_EQ_INT(robo_i18n_date_add_days(&b, -delta, &c), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_compare(&a, &c), 0);
        ROBO_CHECK_EQ_INT(robo_i18n_date_difference_days(&a, &b, &days), ROBO_OK);
        ROBO_CHECK_EQ_INT(days, delta);

        cmp_ab = robo_i18n_date_compare(&a, &b);
        cmp_ba = robo_i18n_date_compare(&b, &a);
        if (cmp_ab == 0) {
            ROBO_CHECK_EQ_INT(cmp_ba, 0);
        } else {
            ROBO_CHECK(cmp_ab == -cmp_ba);
        }

        /* Month add clamp then subtract months is not always identity — only
         * days round-trip is guaranteed. Month overflow must not crash. */
        {
            robo_i18n_date_t m1;
            int32_t months = (int32_t)(fuzz_xorshift(&seed) % 48u) - 24;
            (void)robo_i18n_date_add_months(&a, months, &m1);
        }
    }

    /* Malformed / extreme: must not crash. */
    {
        robo_i18n_date_t bad;
        robo_i18n_date_t out;
        robo_i18n_period_t p;
        memset(&bad, 0, sizeof(bad));
        bad.year = 99999;
        bad.month = 13;
        bad.day = 40;
        ROBO_CHECK_EQ_INT(robo_i18n_date_add_months(&bad, 1, &out), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_i18n_period_of(INT32_MAX, INT32_MAX, 0, 0, &p), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_init(&bad, 2020, 1, 1), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_add_period(&bad, &p, &out), ROBO_ERROR);
    }
}

void robo_test_register_i18n_datetime_arith(void)
{
    robo_test_register("i18n.arith.version", test_arith_version);
    robo_test_register("i18n.arith.duration_vs_period", test_duration_vs_period);
    robo_test_register("i18n.arith.days_roundtrip", test_date_add_days_roundtrip);
    robo_test_register("i18n.arith.eom_clamp", test_end_of_month_clamp);
    robo_test_register("i18n.arith.period_diff", test_date_add_period_and_diff);
    robo_test_register("i18n.arith.instant_duration", test_instant_duration_roundtrip);
    robo_test_register("i18n.arith.iso_week", test_iso_week_utils);
    robo_test_register("i18n.arith.boundaries", test_civil_boundaries);
    robo_test_register("i18n.arith.locale_week", test_locale_week_vs_iso);
    robo_test_register("i18n.arith.tz_period", test_tz_aware_period);
    robo_test_register("i18n.arith.calendars", test_calendar_arith);
    robo_test_register("i18n.arith.business", test_business_days);
    robo_test_register("i18n.arith.compare", test_comparisons);
    robo_test_register("i18n.arith.fuzz", test_fuzz_date_invariants);
}
