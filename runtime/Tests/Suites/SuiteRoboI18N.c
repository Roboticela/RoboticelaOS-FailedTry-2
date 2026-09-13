/*
 * RoboI18N locale suite (Goal 4.1 / BCP 47).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void expect_parse_ok(const char *tag, const char *want_lang, const char *want_script,
                            const char *want_region)
{
    robo_i18n_locale_t loc;
    char buf[128];
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), &loc), ROBO_OK);
    if (want_lang) {
        ROBO_CHECK_STR_EQ(loc.language, want_lang);
    }
    if (want_script) {
        ROBO_CHECK_STR_EQ(loc.script, want_script);
    } else {
        ROBO_CHECK(loc.script[0] == '\0');
    }
    if (want_region) {
        ROBO_CHECK_STR_EQ(loc.region, want_region);
    } else {
        ROBO_CHECK(loc.region[0] == '\0');
    }
    ROBO_CHECK_EQ_INT(robo_i18n_locale_format(&loc, buf, sizeof(buf), NULL), ROBO_OK);
}

static void test_i18n_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.22.0");
    ROBO_CHECK_STR_EQ(robo_i18n_iana_file_date(), "2026-05-05");
    ROBO_CHECK_STR_EQ(robo_i18n_cldr_version_string(), "46");
}

static void test_i18n_basic(void)
{
    expect_parse_ok("en", "en", NULL, NULL);
    expect_parse_ok("ur", "ur", NULL, NULL);
    expect_parse_ok("ar", "ar", NULL, NULL);
    expect_parse_ok("fa", "fa", NULL, NULL);
    expect_parse_ok("he", "he", NULL, NULL);
    expect_parse_ok("hi", "hi", NULL, NULL);
    expect_parse_ok("bn", "bn", NULL, NULL);
    expect_parse_ok("th", "th", NULL, NULL);
    expect_parse_ok("zh", "zh", NULL, NULL);
    expect_parse_ok("ja", "ja", NULL, NULL);
    expect_parse_ok("ko", "ko", NULL, NULL);
    expect_parse_ok("EN", "en", NULL, NULL); /* casing */
}

static void test_i18n_scripts(void)
{
    expect_parse_ok("zh-Hans", "zh", "Hans", NULL);
    expect_parse_ok("zh-Hant", "zh", "Hant", NULL);
    expect_parse_ok("zh-hant", "zh", "Hant", NULL);
    expect_parse_ok("sr-Cyrl", "sr", "Cyrl", NULL);
    expect_parse_ok("sr-Latn", "sr", "Latn", NULL);
}

static void test_i18n_regions(void)
{
    expect_parse_ok("en-US", "en", NULL, "US");
    expect_parse_ok("en-GB", "en", NULL, "GB");
    expect_parse_ok("ur-PK", "ur", NULL, "PK");
    expect_parse_ok("fa-AF", "fa", NULL, "AF");
    expect_parse_ok("ar-SA", "ar", NULL, "SA");
    expect_parse_ok("ar-EG", "ar", NULL, "EG");
    expect_parse_ok("zh-Hant-TW", "zh", "Hant", "TW");
    expect_parse_ok("sr-Latn-RS", "sr", "Latn", "RS");
}

static void test_i18n_complex(void)
{
    robo_i18n_locale_t loc;
    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_parse("en-US-u-ca-gregory", strlen("en-US-u-ca-gregory"), &loc),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(loc.language, "en");
    ROBO_CHECK_STR_EQ(loc.region, "US");
    ROBO_CHECK_STR_EQ(loc.extensions, "u-ca-gregory");

    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_parse("en-x-private", strlen("en-x-private"), &loc), ROBO_OK);
    ROBO_CHECK_STR_EQ(loc.privateuse, "x-private");
}

static void test_i18n_invalid(void)
{
    robo_i18n_locale_t loc;
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("", 0, &loc), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("e", 1, &loc), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("en-", 3, &loc), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("en--US", 6, &loc), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("-en", 3, &loc), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("toolonglang", 11, &loc), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse(NULL, 0, &loc), ROBO_INVALID_ARGUMENT);
}

static void test_i18n_canonicalize(void)
{
    robo_i18n_locale_t loc;
    /* iw → he (IANA Preferred-Value) */
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("iw", 2, &loc), ROBO_OK);
    ROBO_CHECK_STR_EQ(loc.language, "he");
    /* in → id */
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("in", 2, &loc), ROBO_OK);
    ROBO_CHECK_STR_EQ(loc.language, "id");
}

static void test_i18n_equality_match(void)
{
    robo_i18n_locale_t a, b, range;
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("zh-Hans", 7, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("zh-Hant", 7, &b), ROBO_OK);
    ROBO_CHECK(!robo_i18n_locale_equal(&a, &b));

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("ZH-hans", 7, &b), ROBO_OK);
    ROBO_CHECK(robo_i18n_locale_equal(&a, &b));

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("zh", 2, &range), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_matches(&range, &a), ROBO_I18N_MATCH_LANGUAGE);

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("sr-Cyrl", 7, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("sr-Latn", 7, &b), ROBO_OK);
    ROBO_CHECK(!robo_i18n_locale_equal(&a, &b));
}

static void test_i18n_fallback(void)
{
    robo_i18n_locale_t loc, parent;
    char buf[64];

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("zh-Hant-TW", 10, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parent(&loc, &parent), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_format(&parent, buf, sizeof(buf), NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "zh-Hant");
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parent(&parent, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_format(&loc, buf, sizeof(buf), NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "zh");
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parent(&loc, &parent), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("sr-Latn-RS", 10, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parent(&loc, &parent), ROBO_OK);
    ROBO_CHECK_STR_EQ(parent.script, "Latn");
    ROBO_CHECK(parent.region[0] == '\0');

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("en-US", 5, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parent(&loc, &parent), ROBO_OK);
    ROBO_CHECK_STR_EQ(parent.language, "en");
    ROBO_CHECK(parent.region[0] == '\0');

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("ur-PK", 5, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parent(&loc, &parent), ROBO_OK);
    ROBO_CHECK_STR_EQ(parent.language, "ur");
}

static void test_i18n_default_and_list(void)
{
    robo_i18n_locale_t loc, got;
    robo_i18n_locale_list_t *list = NULL;

    ROBO_CHECK_EQ_INT(robo_i18n_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_get_default_locale(&got), ROBO_OK);
    ROBO_CHECK_STR_EQ(got.language, "und");

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse("ur-PK", 5, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_set_default_locale(&loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_get_default_locale(&got), ROBO_OK);
    ROBO_CHECK(robo_i18n_locale_equal(&loc, &got));

    ROBO_CHECK_EQ_INT(
        robo_i18n_locale_list_parse("ur-PK, en;q=0.5, ar-SA;q=0.9", 28, &list), ROBO_OK);
    ROBO_CHECK(list != NULL);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_list_size(list), 3u);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_list_get(list, 0, &got), ROBO_OK);
    ROBO_CHECK_STR_EQ(got.language, "ur"); /* q=1.0 first */
    ROBO_CHECK_EQ_INT(robo_i18n_locale_list_get(list, 1, &got), ROBO_OK);
    ROBO_CHECK_STR_EQ(got.language, "ar"); /* q=0.9 */
    ROBO_CHECK_EQ_INT(robo_i18n_locale_list_get(list, 2, &got), ROBO_OK);
    ROBO_CHECK_STR_EQ(got.language, "en"); /* q=0.5 */
    robo_i18n_locale_list_destroy(list);
}

static void test_i18n_fuzz(void)
{
    /* Untrusted random-ish inputs must not crash; may PARSE_ERROR or OK. */
    static const unsigned char samples[][16] = {
        {0},
        {0xff, 0xfe, 0xfd},
        {'e', 'n', '-', 0x00, 'U', 'S'},
        {'-', '-', '-', '-'},
        {'x', '-', 'y'},
        {'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a'},
        {'z', 'h', '-', 'H', 'a', 'n', 's', '-', 'T', 'W', '-', 'u', '-', 'x'},
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        robo_i18n_locale_t loc;
        (void)robo_i18n_locale_parse((const char *)samples[i], sizeof(samples[i]), &loc);
    }
    ROBO_CHECK(1);
}

static void test_i18n_microbench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 50000u;
    double ms;
    robo_i18n_locale_t loc;

    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (robo_i18n_locale_parse_canonical("zh-Hant-TW", 10, &loc) == ROBO_OK);
        sink += robo_i18n_locale_equal(&loc, &loc);
    }
    t1 = clock();
    ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("i18n.microbench parse_equal=%zu time_ms=%.3f sink=%d\n", N, ms, sink);
    ROBO_CHECK(sink != 0);
}

void robo_test_register_i18n(void)
{
    robo_test_register("i18n.versions", test_i18n_versions);
    robo_test_register("i18n.basic", test_i18n_basic);
    robo_test_register("i18n.scripts", test_i18n_scripts);
    robo_test_register("i18n.regions", test_i18n_regions);
    robo_test_register("i18n.complex", test_i18n_complex);
    robo_test_register("i18n.invalid", test_i18n_invalid);
    robo_test_register("i18n.canonicalize", test_i18n_canonicalize);
    robo_test_register("i18n.equality_match", test_i18n_equality_match);
    robo_test_register("i18n.fallback", test_i18n_fallback);
    robo_test_register("i18n.default_list", test_i18n_default_and_list);
    robo_test_register("i18n.fuzz", test_i18n_fuzz);
    robo_test_register("i18n.microbench", test_i18n_microbench);
}
