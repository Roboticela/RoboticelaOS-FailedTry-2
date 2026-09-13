/*
 * RoboI18N locale negotiation / Accept-Language (Goal 4.17).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NLocaleNegotiate.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_neg_version(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.27.0");
}

static void test_neg_range_parse(void)
{
    robo_i18n_language_range_t r;
    ROBO_CHECK_EQ_INT(robo_i18n_language_range_parse("*", 1, &r), ROBO_OK);
    ROBO_CHECK(r.is_wildcard);
    ROBO_CHECK_EQ_INT(r.q_milli, 1000u);

    {
        const char *s = "en-US;q=0.8";
        ROBO_CHECK_EQ_INT(robo_i18n_language_range_parse(s, strlen(s), &r), ROBO_OK);
    }
    ROBO_CHECK(!r.is_wildcard);
    ROBO_CHECK_STR_EQ(r.locale.language, "en");
    ROBO_CHECK_STR_EQ(r.locale.region, "US");
    ROBO_CHECK_EQ_INT(r.q_milli, 800u);

    {
        const char *s = "en;q=0";
        ROBO_CHECK_EQ_INT(robo_i18n_language_range_parse(s, strlen(s), &r), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(r.q_milli, 0u);

    ROBO_CHECK(robo_i18n_language_range_parse("*-CH", 4, &r) != ROBO_OK);
    ROBO_CHECK(robo_i18n_language_range_parse("en;q=1.5", 8, &r) != ROBO_OK);
    ROBO_CHECK(robo_i18n_language_range_parse("en;q=-0.1", 9, &r) != ROBO_OK);
    ROBO_CHECK(robo_i18n_language_range_parse("", 0, &r) != ROBO_OK);
}

static void test_neg_accept_language(void)
{
    robo_i18n_preference_list_t *list = NULL;
    robo_i18n_language_range_t r;
    char buf[256];
    size_t n = 0;
    const char *hdr = "en-US,en;q=0.9,ur-PK;q=0.8,ar;q=0.7";

    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse(hdr, strlen(hdr), &list), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_normalize(list), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_size(list), 4u);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_get(list, 0, &r), ROBO_OK);
    ROBO_CHECK_STR_EQ(r.locale.language, "en");
    ROBO_CHECK_STR_EQ(r.locale.region, "US");
    ROBO_CHECK_EQ_INT(r.q_milli, 1000u);

    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_serialize(list, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK(n > 0);
    {
        robo_i18n_preference_list_t *again = NULL;
        ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse(buf, n, &again), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_preference_list_normalize(again), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_preference_list_size(again), 4u);
        robo_i18n_preference_list_destroy(again);
    }
    robo_i18n_preference_list_destroy(list);
}

static void test_neg_ur_example(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_locale_t supported[3];
    robo_i18n_negotiate_result_t res;
    const char *hdr = "ur-PK,en-US;q=0.8";

    parse_tag("en-US", &supported[0]);
    parse_tag("ur", &supported[1]);
    parse_tag("ar", &supported[2]);

    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse(hdr, strlen(hdr), &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_normalize(prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_negotiate_ex(prefs, supported, 3, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.selected.language, "ur");
    ROBO_CHECK_EQ_INT(res.kind, ROBO_I18N_NEG_LANGUAGE);
    robo_i18n_preference_list_destroy(prefs);
}

static void test_neg_zh_script_region(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_locale_t supported[3];
    robo_i18n_negotiate_result_t res;
    robo_i18n_language_range_t range;

    parse_tag("zh-Hans", &supported[0]);
    parse_tag("zh-TW", &supported[1]);
    parse_tag("en", &supported[2]);

    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse("zh-Hant-TW", 10, &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_negotiate_ex(prefs, supported, 3, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.selected.language, "zh");
    ROBO_CHECK_STR_EQ(res.selected.region, "TW");
    ROBO_CHECK(res.kind == ROBO_I18N_NEG_REGION || res.kind == ROBO_I18N_NEG_EXACT);

    ROBO_CHECK_EQ_INT(robo_i18n_language_range_parse("zh-Hant-TW", 10, &range), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_match_quality(&range, &supported[0]), ROBO_I18N_NEG_NONE);
    robo_i18n_preference_list_destroy(prefs);
}

static void test_neg_q0_exclusion(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_locale_t supported[2];
    robo_i18n_negotiate_result_t res;
    const char *hdr = "en;q=0,fr";

    parse_tag("en", &supported[0]);
    parse_tag("fr", &supported[1]);
    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse(hdr, strlen(hdr), &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_normalize(prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_negotiate_ex(prefs, supported, 2, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.selected.language, "fr");
    robo_i18n_preference_list_destroy(prefs);
}

static void test_neg_wildcard(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_locale_t supported[2];
    robo_i18n_negotiate_result_t res;

    parse_tag("ja", &supported[0]);
    parse_tag("ko", &supported[1]);
    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse("*;q=0.1", 7, &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_negotiate_ex(prefs, supported, 2, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.selected.language, "ja"); /* first supported */
    ROBO_CHECK_EQ_INT(res.kind, ROBO_I18N_NEG_WILDCARD);
    robo_i18n_preference_list_destroy(prefs);
}

static void test_neg_duplicate_normalize(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_language_range_t r;
    const char *hdr = "en;q=0.5,en;q=0.9,en-US";

    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse(hdr, strlen(hdr), &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_normalize(prefs), ROBO_OK);
    /* en and en-US are distinct; two en entries merge to highest q */
    ROBO_CHECK(robo_i18n_preference_list_size(prefs) >= 2u);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_get(prefs, 0, &r), ROBO_OK);
    /* highest q first: en-US (1000) or en (900) */
    ROBO_CHECK(r.q_milli >= 900u);
    robo_i18n_preference_list_destroy(prefs);
}

static void test_neg_intl_matrix(void)
{
    static const char *prefs_hdr[] = {
        "ur-PK", "ur-IN", "hi-IN", "bn-BD", "ar-EG", "ar-SA", "fa-IR", "he-IL",
        "zh-Hans", "zh-Hant", "zh-CN", "zh-TW", "ja-JP", "ko-KR",
        "en-US", "en-GB", "fr-FR", "de-DE", "es-ES", "pt-BR", "pt-PT", "ru-RU", "tr-TR"
    };
    robo_i18n_locale_t supported[8];
    size_t i;

    parse_tag("en", &supported[0]);
    parse_tag("ur", &supported[1]);
    parse_tag("ar", &supported[2]);
    parse_tag("zh-Hans", &supported[3]);
    parse_tag("zh-Hant", &supported[4]);
    parse_tag("ja", &supported[5]);
    parse_tag("pt-BR", &supported[6]);
    parse_tag("de", &supported[7]);

    for (i = 0; i < sizeof(prefs_hdr) / sizeof(prefs_hdr[0]); i++) {
        robo_i18n_preference_list_t *prefs = NULL;
        robo_i18n_negotiate_result_t res;
        ROBO_CHECK_EQ_INT(
            robo_i18n_accept_language_parse(prefs_hdr[i], strlen(prefs_hdr[i]), &prefs),
            ROBO_OK);
        /* May or may not match — must not crash; when match, locale valid */
        if (robo_i18n_locale_negotiate_ex(prefs, supported, 8, &res) == ROBO_OK) {
            ROBO_CHECK(res.selected.language[0] != '\0');
            ROBO_CHECK(res.kind != ROBO_I18N_NEG_NONE);
        }
        robo_i18n_preference_list_destroy(prefs);
    }
}

static void test_neg_pt_region(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_locale_t supported[2];
    robo_i18n_negotiate_result_t res;

    parse_tag("pt-BR", &supported[0]);
    parse_tag("pt-PT", &supported[1]);
    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse("pt-BR", 5, &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_negotiate_ex(prefs, supported, 2, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.selected.region, "BR");
    robo_i18n_preference_list_destroy(prefs);
}

static void test_neg_limits_negative(void)
{
    char big[ROBO_I18N_ACCEPT_LANG_MAX_BYTES + 8u];
    robo_i18n_preference_list_t *list = NULL;
    memset(big, 'a', sizeof(big));
    big[5] = '-';
    ROBO_CHECK(robo_i18n_accept_language_parse(big, sizeof(big), &list) != ROBO_OK);

    {
        unsigned char bad[] = {0xFF, 0xFE, 'e', 'n'};
        ROBO_CHECK_EQ_INT(
            robo_i18n_accept_language_parse((const char *)bad, sizeof(bad), &list),
            ROBO_PARSE_ERROR);
    }
}

static void test_neg_cross_subsystem(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_locale_t supported[2];
    robo_i18n_negotiate_result_t res;
    robo_i18n_number_formatter_t nfmt;
    robo_i18n_plural_category_t cat;

    parse_tag("ur", &supported[0]);
    parse_tag("en-US", &supported[1]);
    ROBO_CHECK_EQ_INT(
        robo_i18n_accept_language_parse("ur-PK,en-US;q=0.8", 17, &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_normalize(prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_negotiate_ex(prefs, supported, 2, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.selected.language, "ur");
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&nfmt, &res.selected, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&res.selected, 1, &cat), ROBO_OK);
    robo_i18n_number_formatter_destroy(&nfmt);
    robo_i18n_preference_list_destroy(prefs);
}

static void test_neg_fuzz(void)
{
    static const char *samples[] = {
        "", ",", ",,", "en,", ",en", "en;;q=1", "en;q=", "en;q=0.1234",
        "**********", "en-US-u-ca-gregory;q=0.5", "zh-Hans-CN;q=0.001",
        "*;q=0", "en;q=0,en;q=1", "a", "und", "root"
    };
    size_t i;
    uint32_t seed = 0xC0FFEEu;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        robo_i18n_preference_list_t *list = NULL;
        (void)robo_i18n_accept_language_parse(samples[i], strlen(samples[i]), &list);
        if (list) {
            (void)robo_i18n_preference_list_normalize(list);
            robo_i18n_preference_list_destroy(list);
        }
    }
    for (i = 0; i < 64; i++) {
        char buf[64];
        size_t len = (seed % 60u) + 1u;
        size_t j;
        robo_i18n_preference_list_t *list = NULL;
        robo_i18n_locale_t supported[3];
        robo_i18n_negotiate_result_t res;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-;,.q=* ";
            buf[j] = alphabet[seed % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        (void)robo_i18n_accept_language_parse(buf, len, &list);
        if (list) {
            parse_tag("en", &supported[0]);
            parse_tag("ur", &supported[1]);
            parse_tag("zh-Hans", &supported[2]);
            (void)robo_i18n_preference_list_normalize(list);
            (void)robo_i18n_locale_negotiate_ex(list, supported, 3, &res);
            robo_i18n_preference_list_destroy(list);
        }
    }
    ROBO_CHECK(1);
}

static void test_neg_microbench(void)
{
    robo_i18n_preference_list_t *prefs = NULL;
    robo_i18n_locale_t supported[16];
    size_t i;
    volatile int sink = 0;
    const char *hdr = "ur-PK,en-US;q=0.9,ar;q=0.7,zh-Hans;q=0.5,*;q=0.1";

    for (i = 0; i < 16; i++) {
        static const char *tags[] = {"en", "ur", "ar", "fa", "hi", "bn", "zh-Hans",
                                     "zh-Hant", "ja", "ko", "de", "fr", "es", "pt-BR",
                                     "ru", "tr"};
        parse_tag(tags[i], &supported[i]);
    }
    ROBO_CHECK_EQ_INT(robo_i18n_accept_language_parse(hdr, strlen(hdr), &prefs), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_preference_list_normalize(prefs), ROBO_OK);
    for (i = 0; i < 5000; i++) {
        robo_i18n_negotiate_result_t res;
        if (robo_i18n_locale_negotiate_ex(prefs, supported, 16, &res) == ROBO_OK) {
            sink += (int)res.selected.language[0];
        }
    }
    ROBO_CHECK(sink != 0);
    robo_i18n_preference_list_destroy(prefs);
}

void robo_test_register_i18n_locale_negotiate(void)
{
    robo_test_register("i18n.neg.version", test_neg_version);
    robo_test_register("i18n.neg.range_parse", test_neg_range_parse);
    robo_test_register("i18n.neg.accept_language", test_neg_accept_language);
    robo_test_register("i18n.neg.ur_example", test_neg_ur_example);
    robo_test_register("i18n.neg.zh_script_region", test_neg_zh_script_region);
    robo_test_register("i18n.neg.q0_exclusion", test_neg_q0_exclusion);
    robo_test_register("i18n.neg.wildcard", test_neg_wildcard);
    robo_test_register("i18n.neg.duplicate_normalize", test_neg_duplicate_normalize);
    robo_test_register("i18n.neg.intl_matrix", test_neg_intl_matrix);
    robo_test_register("i18n.neg.pt_region", test_neg_pt_region);
    robo_test_register("i18n.neg.limits_negative", test_neg_limits_negative);
    robo_test_register("i18n.neg.cross_subsystem", test_neg_cross_subsystem);
    robo_test_register("i18n.neg.fuzz", test_neg_fuzz);
    robo_test_register("i18n.neg.microbench", test_neg_microbench);
}
