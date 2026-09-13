/*
 * RoboI18N timezone display names / GMT / formatter integration (Goal 4.15).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NDisplayNames.h"
#include "RoboI18N/RoboDispData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void make_utc_instant(int y, int mo, int d, int h, int mi, robo_i18n_instant_t *out)
{
    robo_i18n_datetime_t utc;
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&utc.date, y, mo, d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&utc.time, h, mi, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_instant_from_utc(&utc, out), ROBO_OK);
}

static void test_tzdisp_version(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.15.0");
    ROBO_CHECK_STR_EQ(robo_i18n_display_cldr_version_string(), "46");
    ROBO_CHECK(robo_disp_tz_metazone_count > 100u);
    ROBO_CHECK(robo_disp_iana_mz_range_count > 100u);
    ROBO_CHECK(robo_disp_tz_exemplar_count > 100u);
}

static void test_tzdisp_long_std_dst(void)
{
    robo_i18n_locale_t en;
    robo_i18n_timezone_t tz;
    robo_i18n_instant_t winter, summer;
    char buf[128];
    size_t n = 0;

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    make_utc_instant(2024, 1, 15, 17, 0, &winter);
    make_utc_instant(2024, 7, 15, 16, 0, &summer);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &winter, ROBO_I18N_TZ_DISP_SPECIFIC_LONG, &en, NULL, buf,
                          sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Eastern Standard Time") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &summer, ROBO_I18N_TZ_DISP_SPECIFIC_LONG, &en, NULL, buf,
                          sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Eastern Daylight Time") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &summer, ROBO_I18N_TZ_DISP_LONG_GENERIC, &en, NULL, buf,
                          sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Eastern Time") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &winter, ROBO_I18N_TZ_DISP_SPECIFIC_SHORT, &en, NULL, buf,
                          sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "EST") != NULL);
}

static void test_tzdisp_exemplar_alias(void)
{
    robo_i18n_locale_t en;
    robo_i18n_timezone_t tz;
    char buf[128];
    size_t n = 0;
    parse_tag("en", &en);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(
                          &tz, ROBO_I18N_TZ_DISP_EXEMPLAR_CITY, &en, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    /* en CLDR often omits common English exemplars; structural fallback → Karachi. */
    ROBO_CHECK(strcmp(buf, "Karachi") == 0 || strstr(buf, "Karachi") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("US/Eastern", 10, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(
                          &tz, ROBO_I18N_TZ_DISP_EXEMPLAR_CITY, &en, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "New York");
    /* Localized exemplar where CLDR provides one (ja). */
    {
        robo_i18n_locale_t ja;
        parse_tag("ja", &ja);
        ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(
                              &tz, ROBO_I18N_TZ_DISP_EXEMPLAR_CITY, &ja, NULL, buf, sizeof(buf),
                              &n),
                          ROBO_OK);
        ROBO_CHECK(n > 0);
        ROBO_CHECK(strcmp(buf, "New_York") != 0);
    }
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("US/Eastern", 10, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(
                          &tz, ROBO_I18N_TZ_DISP_IDENTIFIER, &en, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "US/Eastern");
}

static void test_tzdisp_gmt_iana(void)
{
    robo_i18n_locale_t en, ar;
    robo_i18n_timezone_t tz;
    robo_i18n_instant_t inst;
    char buf[128];
    size_t n = 0;

    parse_tag("en", &en);
    parse_tag("ar", &ar);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
    make_utc_instant(2024, 6, 1, 0, 0, &inst);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &inst, ROBO_I18N_TZ_DISP_GMT, &en, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "GMT") != NULL);
    ROBO_CHECK(strstr(buf, "05") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(&tz, ROBO_I18N_TZ_DISP_GMT, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_utc(&tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(&tz, ROBO_I18N_TZ_DISP_GMT, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "GMT");

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &inst, ROBO_I18N_TZ_DISP_GMT, &ar, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    /* Arabic gmtZeroFormat — contains Arabic letters, not necessarily ASCII GMT. */
    ROBO_CHECK(n > 0);
}

static void test_tzdisp_intl_locales(void)
{
    static const char *tags[] = {
        "en", "ar", "ur", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko", "ru", "de", "fr"
    };
    static const char *zones[] = {
        "UTC", "Asia/Karachi", "Asia/Kolkata", "Asia/Tokyo", "Asia/Shanghai", "Asia/Seoul",
        "Europe/London", "Europe/Paris", "America/New_York", "America/Los_Angeles",
        "Australia/Sydney", "Africa/Cairo"
    };
    size_t ti, zi;
    robo_i18n_instant_t inst;
    make_utc_instant(2024, 6, 15, 12, 0, &inst);

    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        robo_i18n_locale_t loc;
        parse_tag(tags[ti], &loc);
        for (zi = 0; zi < sizeof(zones) / sizeof(zones[0]); zi++) {
            robo_i18n_timezone_t tz;
            char buf[192];
            size_t n = 0;
            ROBO_CHECK_EQ_INT(
                robo_i18n_timezone_from_id(zones[zi], strlen(zones[zi]), &tz), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                                  &tz, &inst, ROBO_I18N_TZ_DISP_LONG_GENERIC, &loc, NULL, buf,
                                  sizeof(buf), &n),
                              ROBO_OK);
            ROBO_CHECK(n > 0);
            ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                                  &tz, &inst, ROBO_I18N_TZ_DISP_GMT, &loc, NULL, buf, sizeof(buf),
                                  &n),
                              ROBO_OK);
            ROBO_CHECK(n > 0);
        }
    }
}

static void test_tzdisp_formatter(void)
{
    robo_i18n_locale_t en;
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_instant_t winter;
    char buf[192];
    size_t n = 0;

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    make_utc_instant(2024, 1, 15, 17, 0, &winter);
    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &en, NULL, &tz, "zzzz", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_instant(&fmt, &winter, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Eastern Standard Time") != NULL);
    robo_i18n_datetime_formatter_destroy(&fmt);

    ROBO_CHECK_EQ_INT(
        robo_i18n_datetime_formatter_init_pattern(&fmt, &en, NULL, &tz, "VVVV", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_instant(&fmt, &winter, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "New York") != NULL);
    robo_i18n_datetime_formatter_destroy(&fmt);
}

static void test_tzdisp_metazone_history(void)
{
    /* Europe/London used British metazone before 1971-10-31; then GMT. */
    robo_i18n_locale_t en;
    robo_i18n_timezone_t tz;
    robo_i18n_instant_t early, late;
    char buf_a[128], buf_b[128];
    size_t na = 0, nb = 0;

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Europe/London", 13, &tz), ROBO_OK);
    make_utc_instant(1960, 6, 1, 12, 0, &early);
    make_utc_instant(2020, 6, 1, 12, 0, &late);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &early, ROBO_I18N_TZ_DISP_LONG_GENERIC, &en, NULL, buf_a,
                          sizeof(buf_a), &na),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name_at(
                          &tz, &late, ROBO_I18N_TZ_DISP_LONG_GENERIC, &en, NULL, buf_b,
                          sizeof(buf_b), &nb),
                      ROBO_OK);
    ROBO_CHECK(na > 0 && nb > 0);
    /* Names may differ across metazone eras; both must be non-empty UTF-8. */
}

static void test_tzdisp_fuzz(void)
{
    uint32_t seed = 0x415u;
    int i;
    for (i = 0; i < 64; i++) {
        char id[40];
        size_t len = (seed % 24u) + 1u;
        size_t j;
        robo_i18n_timezone_t tz;
        robo_i18n_locale_t en;
        robo_i18n_instant_t inst;
        char buf[128];
        size_t n = 0;
        parse_tag("en", &en);
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/_-";
            id[j] = alphabet[seed % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        inst.unix_ms = (int64_t)(int32_t)seed * 1000;
        if (robo_i18n_timezone_from_id(id, len, &tz) == ROBO_OK) {
            (void)robo_i18n_timezone_display_name_at(&tz, &inst, ROBO_I18N_TZ_DISP_LONG_GENERIC,
                                                     &en, NULL, buf, sizeof(buf), &n);
            (void)robo_i18n_timezone_display_name_at(&tz, &inst, ROBO_I18N_TZ_DISP_GMT, &en, NULL,
                                                     buf, sizeof(buf), &n);
        }
    }
    ROBO_CHECK(1);
}

void robo_test_register_i18n_timezone_display(void)
{
    robo_test_register("i18n.tzdisp.version", test_tzdisp_version);
    robo_test_register("i18n.tzdisp.std_dst", test_tzdisp_long_std_dst);
    robo_test_register("i18n.tzdisp.exemplar_alias", test_tzdisp_exemplar_alias);
    robo_test_register("i18n.tzdisp.gmt", test_tzdisp_gmt_iana);
    robo_test_register("i18n.tzdisp.intl", test_tzdisp_intl_locales);
    robo_test_register("i18n.tzdisp.formatter", test_tzdisp_formatter);
    robo_test_register("i18n.tzdisp.metazone_hist", test_tzdisp_metazone_history);
    robo_test_register("i18n.tzdisp.fuzz", test_tzdisp_fuzz);
}
