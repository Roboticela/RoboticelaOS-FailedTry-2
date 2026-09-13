/*
 * RoboI18N locale display names (Goal 4.10).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboDispData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_disp_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.22.0");
    ROBO_CHECK_STR_EQ(robo_i18n_display_cldr_version_string(), "46");
    ROBO_CHECK(robo_disp_language_count > 1000u);
    ROBO_CHECK(robo_disp_script_count > 100u);
    ROBO_CHECK(robo_disp_territory_count > 100u);
    ROBO_CHECK(robo_disp_currency_count > 100u);
}

static void expect_name(robo_result_t (*fn)(const char *, size_t, const robo_i18n_locale_t *,
                                            const robo_i18n_display_options_t *, char *, size_t,
                                            size_t *),
                        const char *id,
                        const char *disp_tag,
                        const char *expect_sub)
{
    robo_i18n_locale_t disp;
    char buf[256];
    size_t n = 0;
    parse_tag(disp_tag, &disp);
    ROBO_CHECK_EQ_INT(fn(id, strlen(id), &disp, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(strstr(buf, expect_sub) != NULL);
}

static void test_disp_languages(void)
{
    robo_i18n_locale_t fr;
    char buf[128];
    size_t n = 0;
    parse_tag("fr", &fr);
    ROBO_CHECK_EQ_INT(
        robo_i18n_language_display_name("en", 2, &fr, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "anglais");

    expect_name(robo_i18n_language_display_name, "ur", "en", "Urdu");
    expect_name(robo_i18n_language_display_name, "ar", "ur", "عرب");
    expect_name(robo_i18n_language_display_name, "ja", "ja", "日本");
}

static void test_disp_scripts_regions(void)
{
    expect_name(robo_i18n_script_display_name, "Latn", "en", "Latin");
    expect_name(robo_i18n_script_display_name, "Arab", "en", "Arabic");
    expect_name(robo_i18n_script_display_name, "Deva", "hi", "देव");
    expect_name(robo_i18n_script_display_name, "Hebr", "he", "עבר");
    expect_name(robo_i18n_script_display_name, "Hans", "zh", "简");
    expect_name(robo_i18n_region_display_name, "US", "en", "United States");
    expect_name(robo_i18n_region_display_name, "PK", "ur", "پاکستان");
    expect_name(robo_i18n_region_display_name, "JP", "ja", "日本");
    expect_name(robo_i18n_region_display_name, "SA", "ar", "سعود");
}

static void test_disp_currency(void)
{
    robo_i18n_locale_t en;
    char buf[128];
    size_t n = 0;
    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_display_name("USD", 3, ROBO_I18N_CURRENCY_NAME, &en,
                                                      NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Dollar") != NULL);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_display_name("USD", 3, ROBO_I18N_CURRENCY_SYMBOL, &en,
                                                      NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "$");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_display_name("PKR", 3, ROBO_I18N_CURRENCY_NAME, &en,
                                                      NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Rupee") != NULL);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_display_name("EUR", 3, ROBO_I18N_CURRENCY_NARROW_SYMBOL,
                                                      &en, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(buf[0] != '\0');
}

static void test_disp_calendar_numbering(void)
{
    robo_i18n_locale_t en;
    robo_i18n_calendar_t cal;
    char buf[128];
    size_t n = 0;
    parse_tag("en", &en);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("gregorian", 9, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_calendar_display_name(&cal, &en, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(strstr(buf, "Gregorian") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("buddhist", 8, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_calendar_display_name(&cal, &en, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(strstr(buf, "Buddhist") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_id("islamic", 7, &cal), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_calendar_display_name(&cal, &en, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(strstr(buf, "Islamic") != NULL || strstr(buf, "Hijri") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_numbering_system_display_name("latn", 4, &en, NULL, buf,
                                                              sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Latin") != NULL || strstr(buf, "Western") != NULL);
    ROBO_CHECK_EQ_INT(robo_i18n_numbering_system_display_name("arab", 4, &en, NULL, buf,
                                                              sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Arabic") != NULL);
    ROBO_CHECK_EQ_INT(robo_i18n_numbering_system_display_name("deva", 4, &en, NULL, buf,
                                                              sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "Devanagari") != NULL);
}

static void test_disp_timezone(void)
{
    robo_i18n_locale_t en;
    robo_i18n_timezone_t tz;
    char buf[128];
    size_t n = 0;
    parse_tag("en", &en);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(
                          &tz, ROBO_I18N_TZ_DISP_LONG_STANDARD, &en, NULL, buf, sizeof(buf), &n),
                      ROBO_OK);
    /* Metazone Pakistan → "Pakistan Standard Time" or similar */
    ROBO_CHECK(strstr(buf, "Pakistan") != NULL || strstr(buf, "Karachi") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_utc(&tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(&tz, ROBO_I18N_TZ_DISP_GMT, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "GMT") != NULL);

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("+05:00", 6, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(&tz, ROBO_I18N_TZ_DISP_GMT, &en, NULL, buf,
                                                      sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "05") != NULL);
}

static void test_disp_locale_compose(void)
{
    robo_i18n_locale_t described;
    robo_i18n_locale_t en;
    char buf[256];
    size_t n = 0;
    parse_tag("en", &en);
    parse_tag("en-US", &described);
    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_display_name(&described, &en, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(strstr(buf, "English") != NULL);
    ROBO_CHECK(strstr(buf, "United States") != NULL);

    parse_tag("ur-PK", &described);
    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_display_name(&described, &en, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(strstr(buf, "Urdu") != NULL);
    ROBO_CHECK(strstr(buf, "Pakistan") != NULL);

    parse_tag("zh-Hans", &described);
    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_display_name(&described, &en, NULL, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(strstr(buf, "Chinese") != NULL);
}

static void test_disp_fallback(void)
{
    robo_i18n_locale_t en;
    robo_i18n_display_options_t opt;
    char buf[64];
    size_t n = 0;
    parse_tag("en", &en);
    robo_i18n_display_options_init(&opt);
    opt.fallback = ROBO_I18N_DISP_FALLBACK_CODE;
    ROBO_CHECK_EQ_INT(robo_i18n_language_display_name("zzxnotalang", 11, &en, &opt, buf,
                                                      sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "zzxnotalang");

    opt.fallback = ROBO_I18N_DISP_FALLBACK_NOT_FOUND;
    ROBO_CHECK_EQ_INT(robo_i18n_language_display_name("zzxnotalang", 11, &en, &opt, buf,
                                                      sizeof(buf), &n),
                      ROBO_NOT_FOUND);
}

static void test_disp_locales_smoke(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "ja", "ko", "de", "fr", "es", "tr"
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t disp;
        char buf[128];
        size_t n = 0;
        parse_tag(tags[i], &disp);
        ROBO_CHECK_EQ_INT(
            robo_i18n_language_display_name("en", 2, &disp, NULL, buf, sizeof(buf), &n),
            ROBO_OK);
        ROBO_CHECK(buf[0] != '\0');
        ROBO_CHECK_EQ_INT(
            robo_i18n_region_display_name("US", 2, &disp, NULL, buf, sizeof(buf), &n),
            ROBO_OK);
        ROBO_CHECK(buf[0] != '\0');
    }
}

static void test_disp_fuzz(void)
{
    robo_i18n_locale_t en;
    char buf[64];
    size_t n = 0;
    uint32_t seed = 0xD150u;
    int i;
    parse_tag("en", &en);
    for (i = 0; i < 40; i++) {
        char id[8];
        size_t len = (seed % 5u) + 1u;
        size_t j;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            id[j] = (char)('a' + ((seed >> j) % 26u));
            seed = seed * 1664525u + 1013904223u;
        }
        (void)robo_i18n_language_display_name(id, len, &en, NULL, buf, sizeof(buf), &n);
        (void)robo_i18n_script_display_name("Xyza", 4, &en, NULL, buf, sizeof(buf), &n);
        (void)robo_i18n_region_display_name("ZZ", 2, &en, NULL, buf, sizeof(buf), &n);
        (void)robo_i18n_currency_display_name("XXX", 3, ROBO_I18N_CURRENCY_NAME, &en, NULL, buf,
                                              sizeof(buf), &n);
    }
    ROBO_CHECK_EQ_INT(
        robo_i18n_language_display_name(NULL, 0, &en, NULL, buf, sizeof(buf), &n),
        ROBO_INVALID_ARGUMENT);
}

void robo_test_register_i18n_display_names(void)
{
    robo_test_register("i18n.disp.versions", test_disp_versions);
    robo_test_register("i18n.disp.languages", test_disp_languages);
    robo_test_register("i18n.disp.scripts_regions", test_disp_scripts_regions);
    robo_test_register("i18n.disp.currency", test_disp_currency);
    robo_test_register("i18n.disp.calendar_numbering", test_disp_calendar_numbering);
    robo_test_register("i18n.disp.timezone", test_disp_timezone);
    robo_test_register("i18n.disp.locale_compose", test_disp_locale_compose);
    robo_test_register("i18n.disp.fallback", test_disp_fallback);
    robo_test_register("i18n.disp.locales_smoke", test_disp_locales_smoke);
    robo_test_register("i18n.disp.fuzz", test_disp_fuzz);
}
