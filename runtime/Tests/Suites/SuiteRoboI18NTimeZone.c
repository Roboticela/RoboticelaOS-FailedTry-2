/*
 * RoboI18N timezone / tzdb tests (Goal 4.14).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NTimeZone.h"
#include "RoboI18N/RoboTzData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void test_tz_version(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.14.0");
    ROBO_CHECK_STR_EQ(robo_i18n_tzdb_version_string(), "2026c");
    ROBO_CHECK_STR_EQ(robo_tzdb_version_string(), "2026c");
}

static void test_tz_lookup(void)
{
    robo_i18n_timezone_t tz;
    char canon[64];
    size_t n = 0;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_i18n_timezone_kind(&tz), (int)ROBO_I18N_TZ_IANA);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_canonical_id(&tz, canon, sizeof(canon), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(canon, "Asia/Karachi");

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Not/AZone", 9, &tz), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("UTC", 3, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Etc/UTC", 7, &tz), ROBO_OK);
}

static void test_tz_alias(void)
{
    robo_i18n_timezone_t tz;
    char canon[64];
    size_t n = 0;
    /* US/Eastern is a Link alias in backward → America/New_York. */
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("US/Eastern", 10, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_canonical_id(&tz, canon, sizeof(canon), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(canon, "America/New_York");
    ROBO_CHECK_STR_EQ(robo_i18n_timezone_id(&tz), "US/Eastern");
}

static void test_tz_historical(void)
{
    /* America/New_York 2006 spring (pre–Energy Policy Act permanent Mar/Nov rules). */
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_t utc;
    robo_i18n_instant_t inst;
    int32_t off;
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 2006, 4, 2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 6, 30, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &inst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&tz, &inst, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(off, -5 * 3600); /* still EST just before 2006-04-02 07:00 UTC */
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 2006, 4, 2), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 7, 30, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &inst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&tz, &inst, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(off, -4 * 3600);
}

static void test_tz_karachi_offset(void)
{
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_t utc;
    robo_i18n_instant_t inst;
    robo_i18n_datetime_t local;
    robo_i18n_tz_resolve_t res;
    int32_t off;
    int dst = 1;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 2024, 6, 15), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 0, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &inst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&tz, &inst, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(off, 5 * 3600);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_is_dst_at(&tz, &inst, &dst), ROBO_OK);
    ROBO_CHECK_EQ_INT(dst, 0);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_local(&tz, &inst, &local, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)local.time.hour, 5);
}

static void test_tz_roundtrip_unique(void)
{
    static const char *zones[] = {
        "UTC", "Asia/Tokyo", "Asia/Kolkata", "Europe/Paris", "America/Sao_Paulo",
        "Australia/Sydney", "Africa/Cairo", "Asia/Shanghai", "Pacific/Auckland"
    };
    size_t zi;
    for (zi = 0; zi < sizeof(zones) / sizeof(zones[0]); zi++) {
        robo_i18n_timezone_t tz;
        robo_i18n_datetime_t utc;
        robo_i18n_instant_t inst, back;
        robo_i18n_datetime_t local;
        robo_i18n_tz_resolve_t res;
        ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id(zones[zi], strlen(zones[zi]), &tz), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 2023, 3, 10), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 12, 0, 0, 0), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &inst), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_local(&tz, &inst, &local, &res), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_utc(&tz, &local, &back, &res), ROBO_OK);
        /* Ambiguous folds may shift; for UNIQUE expect exact. */
        if (res == ROBO_I18N_TZ_UNIQUE) {
            ROBO_CHECK_EQ_INT(robo_i18n_instant_compare(&inst, &back), 0);
        }
    }
}

static void test_tz_ny_spring_forward(void)
{
    /* America/New_York 2024-03-10: 02:00 → 03:00 EDT (gap). */
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_t local;
    robo_i18n_instant_t inst;
    robo_i18n_tz_resolve_t res;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&local.date, 2024, 3, 10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&local.time, 2, 30, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_utc_ex(&tz, &local, ROBO_I18N_TZ_DISAMBIG_REJECT, &inst,
                                                    &res),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)res, (int)ROBO_I18N_TZ_NONEXISTENT);
}

static void test_tz_ny_fall_back(void)
{
    /* America/New_York 2024-11-03: 01:30 occurs twice. */
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_t local;
    robo_i18n_instant_t earlier, later;
    robo_i18n_tz_resolve_t res;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&local.date, 2024, 11, 3), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&local.time, 1, 30, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_utc_ex(&tz, &local, ROBO_I18N_TZ_DISAMBIG_EARLIER,
                                                    &earlier, &res),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res, (int)ROBO_I18N_TZ_AMBIGUOUS);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_to_utc_ex(&tz, &local, ROBO_I18N_TZ_DISAMBIG_LATER, &later,
                                                    &res),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res, (int)ROBO_I18N_TZ_AMBIGUOUS);
    ROBO_CHECK(later.unix_ms > earlier.unix_ms);
    ROBO_CHECK_EQ_INT((int)((later.unix_ms - earlier.unix_ms) / 1000), 3600);
}

static void test_tz_transitions(void)
{
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_t utc;
    robo_i18n_instant_t inst;
    robo_i18n_tz_transition_t tr;
    int32_t off_before;
    int32_t off_after;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    /* Just before 2024 spring forward (~2024-03-10 07:00 UTC). */
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, 2024, 3, 10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, 6, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, &inst), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_next_transition(&tz, &inst, &tr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&tz, &inst, &off_before), ROBO_OK);
    {
        robo_i18n_instant_t after = tr.at;
        after.unix_ms += 1000;
        ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&tz, &after, &off_after), ROBO_OK);
    }
    ROBO_CHECK(tr.offset_after != tr.offset_before || tr.is_dst_after != tr.is_dst_before);
    ROBO_CHECK_EQ_INT(off_before, tr.offset_before);
}

static void test_tz_fixed_and_utc(void)
{
    robo_i18n_timezone_t tz;
    robo_i18n_instant_t inst = {0};
    int32_t off;
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_fixed_seconds(5 * 3600 + 30 * 60, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&tz, &inst, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(off, 5 * 3600 + 30 * 60);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_utc(&tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_offset_at(&tz, &inst, &off), ROBO_OK);
    ROBO_CHECK_EQ_INT(off, 0);
}

static void test_tz_fuzz(void)
{
    uint32_t seed = 0x414u;
    int i;
    for (i = 0; i < 80; i++) {
        char id[32];
        size_t len = (seed % 20u) + 1u;
        size_t j;
        robo_i18n_timezone_t tz;
        robo_i18n_instant_t inst;
        int32_t off;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/_-+";
            id[j] = alphabet[seed % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        (void)robo_i18n_timezone_from_id(id, len, &tz);
        inst.unix_ms = (int64_t)(int32_t)seed * 1000;
        if (tz.kind == ROBO_I18N_TZ_IANA || tz.kind == ROBO_I18N_TZ_UTC ||
            tz.kind == ROBO_I18N_TZ_FIXED) {
            (void)robo_i18n_timezone_offset_at(&tz, &inst, &off);
        }
    }
    ROBO_CHECK(1);
}

void robo_test_register_i18n_timezone(void)
{
    robo_test_register("i18n.tz.version", test_tz_version);
    robo_test_register("i18n.tz.lookup", test_tz_lookup);
    robo_test_register("i18n.tz.alias", test_tz_alias);
    robo_test_register("i18n.tz.historical", test_tz_historical);
    robo_test_register("i18n.tz.karachi", test_tz_karachi_offset);
    robo_test_register("i18n.tz.roundtrip", test_tz_roundtrip_unique);
    robo_test_register("i18n.tz.spring_forward", test_tz_ny_spring_forward);
    robo_test_register("i18n.tz.fall_back", test_tz_ny_fall_back);
    robo_test_register("i18n.tz.transitions", test_tz_transitions);
    robo_test_register("i18n.tz.fixed", test_tz_fixed_and_utc);
    robo_test_register("i18n.tz.fuzz", test_tz_fuzz);
}
