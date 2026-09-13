/*
 * RoboI18N CLDR locale-data suite (Goal 4.2).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_fmt(const robo_i18n_locale_t *loc, const char *want)
{
    char buf[128];
    ROBO_CHECK_EQ_INT(robo_i18n_locale_format(loc, buf, sizeof(buf), NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
}

static void expect_max(const char *in, const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_t out;
    robo_i18n_locale_t original;
    parse_tag(in, &loc);
    original = loc;
    ROBO_CHECK_EQ_INT(robo_i18n_locale_maximize(&loc, &out), ROBO_OK);
    ROBO_CHECK(robo_i18n_locale_equal(&loc, &original));
    expect_fmt(&out, want);
}

static void expect_min(const char *in, const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_t out;
    parse_tag(in, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_minimize(&loc, &out), ROBO_OK);
    expect_fmt(&out, want);
}

static void expect_ns(const char *tag, const char *want_default, const char *want_native)
{
    robo_i18n_locale_t loc;
    char def[32];
    char nat[32];
    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_default_numbering_system(&loc, def, sizeof(def)), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_native_numbering_system(&loc, nat, sizeof(nat)), ROBO_OK);
    ROBO_CHECK_STR_EQ(def, want_default);
    ROBO_CHECK_STR_EQ(nat, want_native);
}

static void expect_cal(const char *tag, const char *want_first)
{
    robo_i18n_locale_t loc;
    char id[32];
    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_default_calendar(&loc, id, sizeof(id)), ROBO_OK);
    ROBO_CHECK_STR_EQ(id, want_first);
}

static void expect_meas(const char *tag, robo_i18n_measurement_t want)
{
    robo_i18n_locale_t loc;
    robo_i18n_measurement_t got;
    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_measurement_system(&loc, &got), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)got, (int)want);
}

static void test_cldr_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(robo_i18n_cldr_unicode_version_string(), "16.0.0");
}

static void test_cldr_maximize(void)
{
    expect_max("zh", "zh-Hans-CN");
    expect_max("zh-TW", "zh-Hant-TW");
    expect_max("zh-Hant", "zh-Hant-TW");
    expect_max("sr", "sr-Cyrl-RS");
    expect_max("sr-Latn", "sr-Latn-RS");
    expect_max("ur", "ur-Arab-PK");
    expect_max("ar", "ar-Arab-EG");
    expect_max("fa", "fa-Arab-IR");
    expect_max("hi", "hi-Deva-IN");
    expect_max("bn", "bn-Beng-BD");
    expect_max("th", "th-Thai-TH");
    expect_max("ja", "ja-Jpan-JP");
    expect_max("ko", "ko-Kore-KR");
    expect_max("en", "en-Latn-US");
    expect_max("he", "he-Hebr-IL");
}

static void test_cldr_minimize(void)
{
    expect_min("zh-Hans-CN", "zh");
    expect_min("zh-Hant-TW", "zh-Hant");
    expect_min("sr-Cyrl-RS", "sr");
    expect_min("sr-Latn-RS", "sr-Latn");
    expect_min("en-Latn-US", "en");
}

static void test_cldr_numbering_locales(void)
{
    expect_ns("en-US", "latn", "latn");
    expect_ns("en-GB", "latn", "latn");
    expect_ns("ar-SA", "arab", "arab");
    expect_ns("ar-EG", "arab", "arab");
    expect_ns("fa-IR", "arabext", "arabext");
    expect_ns("fa-AF", "arabext", "arabext");
    expect_ns("hi-IN", "latn", "deva");
    expect_ns("bn-BD", "beng", "beng");
    expect_ns("th-TH", "latn", "thai");
    expect_ns("ur-PK", "latn", "arabext");
    expect_ns("zh-CN", "latn", "hanidec");
    expect_ns("zh-TW", "latn", "hanidec");
    expect_ns("ja-JP", "latn", "latn");
    expect_ns("ko-KR", "latn", "latn");
    expect_ns("de-DE", "latn", "latn");
    expect_ns("fr-FR", "latn", "latn");
    expect_ns("es-ES", "latn", "latn");
    expect_ns("he-IL", "latn", "latn");
}

static void test_cldr_numbering_digits(void)
{
    static const struct {
        const char *id;
        const char *type;
        /* UTF-8 concatenation of digit 0..9 from CLDR 46 numberingSystems.json */
        const unsigned char digits[40];
        size_t digits_len;
    } cases[] = {
        {"latn", "numeric", {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'}, 10},
        {"arab",
         "numeric",
         {0xd9, 0xa0, 0xd9, 0xa1, 0xd9, 0xa2, 0xd9, 0xa3, 0xd9, 0xa4, 0xd9, 0xa5, 0xd9, 0xa6,
          0xd9, 0xa7, 0xd9, 0xa8, 0xd9, 0xa9},
         20},
        {"arabext",
         "numeric",
         {0xdb, 0xb0, 0xdb, 0xb1, 0xdb, 0xb2, 0xdb, 0xb3, 0xdb, 0xb4, 0xdb, 0xb5, 0xdb, 0xb6,
          0xdb, 0xb7, 0xdb, 0xb8, 0xdb, 0xb9},
         20},
        {"deva",
         "numeric",
         {0xe0, 0xa5, 0xa6, 0xe0, 0xa5, 0xa7, 0xe0, 0xa5, 0xa8, 0xe0, 0xa5, 0xa9, 0xe0, 0xa5,
          0xaa, 0xe0, 0xa5, 0xab, 0xe0, 0xa5, 0xac, 0xe0, 0xa5, 0xad, 0xe0, 0xa5, 0xae, 0xe0,
          0xa5, 0xaf},
         30},
        {"beng",
         "numeric",
         {0xe0, 0xa7, 0xa6, 0xe0, 0xa7, 0xa7, 0xe0, 0xa7, 0xa8, 0xe0, 0xa7, 0xa9, 0xe0, 0xa7,
          0xaa, 0xe0, 0xa7, 0xab, 0xe0, 0xa7, 0xac, 0xe0, 0xa7, 0xad, 0xe0, 0xa7, 0xae, 0xe0,
          0xa7, 0xaf},
         30},
        {"thai",
         "numeric",
         {0xe0, 0xb9, 0x90, 0xe0, 0xb9, 0x91, 0xe0, 0xb9, 0x92, 0xe0, 0xb9, 0x93, 0xe0, 0xb9,
          0x94, 0xe0, 0xb9, 0x95, 0xe0, 0xb9, 0x96, 0xe0, 0xb9, 0x97, 0xe0, 0xb9, 0x98, 0xe0,
          0xb9, 0x99},
         30},
        {"fullwide",
         "numeric",
         {0xef, 0xbc, 0x90, 0xef, 0xbc, 0x91, 0xef, 0xbc, 0x92, 0xef, 0xbc, 0x93, 0xef, 0xbc,
          0x94, 0xef, 0xbc, 0x95, 0xef, 0xbc, 0x96, 0xef, 0xbc, 0x97, 0xef, 0xbc, 0x98, 0xef,
          0xbc, 0x99},
         30},
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        robo_i18n_numbering_system_t ns;
        ROBO_CHECK_EQ_INT(robo_i18n_numbering_system_get(cases[i].id, &ns), ROBO_OK);
        ROBO_CHECK_STR_EQ(ns.id, cases[i].id);
        ROBO_CHECK_STR_EQ(ns.type, cases[i].type);
        ROBO_CHECK_EQ_INT((int)strlen(ns.digits), (int)cases[i].digits_len);
        ROBO_CHECK(memcmp(ns.digits, cases[i].digits, cases[i].digits_len) == 0);
    }
}

static void test_cldr_calendars(void)
{
    expect_cal("en-US", "gregorian");
    expect_cal("en-GB", "gregorian");
    expect_cal("fa-AF", "persian");
    expect_cal("fa-IR", "persian");
    expect_cal("th-TH", "buddhist");
    expect_cal("ar-SA", "gregorian");
    expect_cal("he-IL", "gregorian");
    expect_cal("ja-JP", "gregorian");
    expect_cal("zh-CN", "gregorian");
    expect_cal("zh-TW", "gregorian");
}

static void test_cldr_measurement(void)
{
    expect_meas("en-US", ROBO_I18N_MEASURE_US);
    expect_meas("en-GB", ROBO_I18N_MEASURE_UK);
    expect_meas("de-DE", ROBO_I18N_MEASURE_METRIC);
    expect_meas("ur-PK", ROBO_I18N_MEASURE_METRIC);
    expect_meas("ar-EG", ROBO_I18N_MEASURE_METRIC);
    expect_meas("ja-JP", ROBO_I18N_MEASURE_METRIC);
}

static void test_cldr_data_parent(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_t parent;
    char buf[64];

    parse_tag("zh-Hant-TW", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_data_parent(&loc, &parent), ROBO_OK);
    expect_fmt(&parent, "zh-Hant");

    parse_tag("zh-Hant", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_data_parent(&loc, &parent), ROBO_OK);
    /* CLDR parentLocales maps zh-Hant → und */
    expect_fmt(&parent, "und");

    parse_tag("en-GB", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_data_parent(&loc, &parent), ROBO_OK);
    expect_fmt(&parent, "en-001");

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_data_parent(&loc, &parent), ROBO_OK);
    expect_fmt(&parent, "en");

    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_data_parent(&loc, &parent), ROBO_OK);
    ROBO_CHECK_STR_EQ(parent.language, "root");

    parse_tag("root", &loc);
    /* syntactically "root" as language */
    ROBO_CHECK_EQ_INT(robo_i18n_locale_data_parent(&loc, &parent), ROBO_NOT_FOUND);

    (void)buf;
}

static void test_cldr_script_locale_distinction(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_t maxed;
    /* ur-PK does not invent a script from the language name alone without CLDR. */
    parse_tag("ur-PK", &loc);
    ROBO_CHECK(loc.script[0] == '\0');
    ROBO_CHECK_EQ_INT(robo_i18n_locale_maximize(&loc, &maxed), ROBO_OK);
    ROBO_CHECK_STR_EQ(maxed.script, "Arab");
    ROBO_CHECK_STR_EQ(maxed.region, "PK");
}

static void test_cldr_intl_coverage(void)
{
    static const char *tags[] = {
        "en-US", "en-GB", "ur-PK", "ar-SA", "ar-EG", "fa-IR", "fa-AF", "he-IL",
        "hi-IN", "bn-BD", "th-TH", "zh-CN", "zh-TW", "ja-JP", "ko-KR", "de-DE",
        "fr-FR", "es-ES",
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_locale_t maxed;
        char ns[32];
        char cal[32];
        robo_i18n_measurement_t m;
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_locale_maximize(&loc, &maxed), ROBO_OK);
        ROBO_CHECK(maxed.script[0] != '\0' || maxed.region[0] != '\0');
        ROBO_CHECK_EQ_INT(robo_i18n_locale_default_numbering_system(&loc, ns, sizeof(ns)),
                          ROBO_OK);
        ROBO_CHECK(ns[0] != '\0');
        ROBO_CHECK_EQ_INT(robo_i18n_locale_default_calendar(&loc, cal, sizeof(cal)), ROBO_OK);
        ROBO_CHECK(cal[0] != '\0');
        ROBO_CHECK_EQ_INT(robo_i18n_locale_measurement_system(&loc, &m), ROBO_OK);
    }
}

static void test_cldr_malformed(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_t out;
    robo_i18n_numbering_system_t ns;
    char buf[8];
    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_maximize(NULL, &out), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_minimize(&loc, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_numbering_system_get("not-a-real-ns", &ns), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_default_numbering_system(NULL, buf, sizeof(buf)),
                      ROBO_INVALID_ARGUMENT);
}

static void test_cldr_microbench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 20000u;
    double ms;
    robo_i18n_locale_t loc;
    robo_i18n_locale_t out;
    char ns[32];
    char cal[32];
    robo_i18n_measurement_t m;

    parse_tag("zh-Hant-TW", &loc);
    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (robo_i18n_locale_maximize(&loc, &out) == ROBO_OK);
        sink += (robo_i18n_locale_minimize(&out, &out) == ROBO_OK);
        sink += (robo_i18n_locale_default_numbering_system(&loc, ns, sizeof(ns)) == ROBO_OK);
        sink += (robo_i18n_locale_default_calendar(&loc, cal, sizeof(cal)) == ROBO_OK);
        sink += (robo_i18n_locale_measurement_system(&loc, &m) == ROBO_OK);
    }
    t1 = clock();
    ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("i18n.cldr.microbench N=%zu time_ms=%.3f sink=%d\n", N, ms, sink);
    ROBO_CHECK(sink != 0);
}

void robo_test_register_i18n_cldr(void)
{
    robo_test_register("i18n.cldr.versions", test_cldr_versions);
    robo_test_register("i18n.cldr.maximize", test_cldr_maximize);
    robo_test_register("i18n.cldr.minimize", test_cldr_minimize);
    robo_test_register("i18n.cldr.numbering_locales", test_cldr_numbering_locales);
    robo_test_register("i18n.cldr.numbering_digits", test_cldr_numbering_digits);
    robo_test_register("i18n.cldr.calendars", test_cldr_calendars);
    robo_test_register("i18n.cldr.measurement", test_cldr_measurement);
    robo_test_register("i18n.cldr.data_parent", test_cldr_data_parent);
    robo_test_register("i18n.cldr.script_locale", test_cldr_script_locale_distinction);
    robo_test_register("i18n.cldr.intl_coverage", test_cldr_intl_coverage);
    robo_test_register("i18n.cldr.malformed", test_cldr_malformed);
    robo_test_register("i18n.cldr.microbench", test_cldr_microbench);
}
