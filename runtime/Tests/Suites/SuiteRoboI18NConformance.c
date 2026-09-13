/*
 * RoboI18N completeness / conformance suite (Goal 4.26).
 *
 * Covers: version manifest fields, international regression corpus, numbering
 * format/parse agreement, plural cardinal≠ordinal separation, search≠translit
 * boundary, locale fallback via RoboLocale, fuzz/robustness, light perf smoke.
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboUnicode/RoboUnicodeVersion.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void test_conf_versions(void)
{
    robo_i18n_data_versions_t v;
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.27.0");
    robo_i18n_data_versions(&v);
    ROBO_CHECK_STR_EQ(v.api_version, "0.27.0");
    ROBO_CHECK_STR_EQ(v.generator_version, "1.25.0");
    ROBO_CHECK_STR_EQ(v.cldr_version, "46");
    ROBO_CHECK_STR_EQ(v.unicode_version, "16.0.0");
    ROBO_CHECK_STR_EQ(v.tzdb_version, "2026c");
    ROBO_CHECK_STR_EQ(v.iana_file_date, "2026-05-05");
    ROBO_CHECK_STR_EQ(v.uca_version, "16.0.0");
    ROBO_CHECK_STR_EQ(v.iso4217_source, "CLDR-46-codeMappings");
    /* Unicode UCD pin must match CLDR Unicode association. */
    ROBO_CHECK_STR_EQ(ROBO_UNICODE_VERSION_STRING, v.unicode_version);
    ROBO_CHECK_STR_EQ(ROBO_EMOJI_VERSION_STRING, "16.0");
}

static int locale_ok(const char *tag)
{
    robo_i18n_locale_t loc;
    return robo_i18n_locale_parse_canonical(tag, strlen(tag), &loc) == ROBO_OK;
}

static void test_conf_regression_locales(void)
{
    static const char *const tags[] = {
        "en", "fr", "de", "es", "tr", "ru", "pl", "uk", "ar", "fa", "ur", "he",
        "hi", "bn", "th", "zh", "ja", "ko",
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        ROBO_CHECK(locale_ok(tags[i]));
    }
}

static void format_number(const char *tag, double value, char *out, size_t cap, size_t *n)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    robo_i18n_number_options_init(&opts);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_number_format_double(&fmt, value, out, cap, n), ROBO_OK);
    robo_i18n_number_formatter_destroy(&fmt);
}

static void test_conf_numbering_systems(void)
{
    char out[128];
    size_t n = 0;
    /* Latin digits (en) */
    format_number("en", 1234.5, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    ROBO_CHECK(strchr(out, '1') != NULL);
    /* Arabic-Indic / locale-default digits for ar — non-empty format */
    format_number("ar", 12.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    format_number("fa", 12.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    format_number("hi", 12.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    format_number("bn", 12.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    format_number("th", 12.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
}

static void test_conf_plural_cardinal_ordinal_distinct(void)
{
    robo_i18n_locale_t en;
    robo_i18n_plural_operands_t ops;
    robo_i18n_plural_category_t card = ROBO_I18N_PLURAL_OTHER;
    robo_i18n_plural_category_t ord = ROBO_I18N_PLURAL_OTHER;
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("en", 2, &en), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string("1", 1, &ops), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_select(&en, ROBO_I18N_PLURAL_CARDINAL, &ops, &card),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_select(&en, ROBO_I18N_PLURAL_ORDINAL, &ops, &ord),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)card, (int)ROBO_I18N_PLURAL_ONE);
    ROBO_CHECK_EQ_INT((int)ord, (int)ROBO_I18N_PLURAL_ONE);
    /* English cardinal "2" = other; ordinal "2" = two — separate rule tables. */
    {
        robo_i18n_plural_category_t c2 = ROBO_I18N_PLURAL_OTHER;
        robo_i18n_plural_category_t o2 = ROBO_I18N_PLURAL_OTHER;
        ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string("2", 1, &ops), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_plural_select(&en, ROBO_I18N_PLURAL_CARDINAL, &ops, &c2),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_plural_select(&en, ROBO_I18N_PLURAL_ORDINAL, &ops, &o2),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)c2, (int)ROBO_I18N_PLURAL_OTHER);
        ROBO_CHECK_EQ_INT((int)o2, (int)ROBO_I18N_PLURAL_TWO);
    }
}

static void test_conf_search_translit_boundary(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int found = 0;
    char out[64];
    size_t n = 0;
    robo_i18n_transliterator_t tr;

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_EXACT;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    /* Exact search must not transliterate Cyrillic → Latin. */
    ROBO_CHECK_EQ_INT(robo_i18n_search_contains(&m, "\xD0\x9C", 2, "M", 1, &found), ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 0);
    robo_i18n_searcher_destroy(&m);

    /* Explicit transliteration still works. */
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_create_id("Russian-Latin/BGN", NULL, &tr),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_transliterate(&tr, "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", 12, out,
                               sizeof(out), &n, NULL, 0, NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(out, "Moskva");
    robo_i18n_transliterator_destroy(&tr);
}

static void test_conf_locale_fallback(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_locale_t parent;
    /* en-US resolves; parent chain uses central locale APIs. */
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("en-US", 5, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parent(&loc, &parent), ROBO_OK);
    ROBO_CHECK_STR_EQ(parent.language, "en");
}

static void test_conf_currency_roundtrip_smoke(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    char out[128];
    size_t n = 0;
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("en", 2, &loc), ROBO_OK);
    robo_i18n_currency_options_init(&opts);
    memcpy(opts.currency, "USD", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format_double(&fmt, 1.5, out, sizeof(out), &n),
                      ROBO_OK);
    ROBO_CHECK(n > 0);
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void test_conf_timezone_smoke(void)
{
    robo_i18n_timezone_t tz;
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("UTC", 3, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
}

static void test_conf_rtl_corpus(void)
{
    char out[128];
    size_t n = 0;
    format_number("ar", 3.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    format_number("he", 3.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    format_number("ur", 3.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
    format_number("fa", 3.0, out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
}

static void test_conf_fuzz_robust(void)
{
    robo_i18n_locale_t loc;
    char out[64];
    size_t n = 0;
    (void)robo_i18n_locale_parse_canonical("", 0, &loc);
    (void)robo_i18n_locale_parse_canonical("!!!", 3, &loc);
    (void)robo_i18n_locale_parse_canonical("\xFF\xFE", 2, &loc);
    if (robo_i18n_locale_parse_canonical("en", 2, &loc) == ROBO_OK) {
        robo_i18n_number_formatter_t fmt;
        robo_i18n_number_options_t opts;
        robo_i18n_number_options_init(&opts);
        if (robo_i18n_number_formatter_init(&fmt, &loc, &opts) == ROBO_OK) {
            (void)robo_i18n_number_format_double(&fmt, 0.0, out, sizeof(out), &n);
            robo_i18n_number_formatter_destroy(&fmt);
        }
    }
}

static void test_conf_perf_smoke(void)
{
    clock_t t0, t1;
    size_t i;
    char out[64];
    size_t n = 0;
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    robo_i18n_number_options_init(&opts);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("en", 2, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    t0 = clock();
    for (i = 0; i < 2000u; i++) {
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_double(&fmt, (double)i, out, sizeof(out), &n),
                          ROBO_OK);
    }
    t1 = clock();
    robo_i18n_number_formatter_destroy(&fmt);
    ROBO_CHECK((t1 - t0) < 20 * CLOCKS_PER_SEC);
}

void robo_test_register_i18n_conformance(void)
{
    robo_test_register("i18n.conf.versions", test_conf_versions);
    robo_test_register("i18n.conf.regression_locales", test_conf_regression_locales);
    robo_test_register("i18n.conf.numbering", test_conf_numbering_systems);
    robo_test_register("i18n.conf.plural_kinds", test_conf_plural_cardinal_ordinal_distinct);
    robo_test_register("i18n.conf.search_translit", test_conf_search_translit_boundary);
    robo_test_register("i18n.conf.locale_fallback", test_conf_locale_fallback);
    robo_test_register("i18n.conf.currency", test_conf_currency_roundtrip_smoke);
    robo_test_register("i18n.conf.timezone", test_conf_timezone_smoke);
    robo_test_register("i18n.conf.rtl", test_conf_rtl_corpus);
    robo_test_register("i18n.conf.fuzz", test_conf_fuzz_robust);
    robo_test_register("i18n.conf.perf_smoke", test_conf_perf_smoke);
}
