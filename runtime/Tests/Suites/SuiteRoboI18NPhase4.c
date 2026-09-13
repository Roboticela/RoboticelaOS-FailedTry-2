/*
 * RoboI18N Phase 4 final integration suite (Goal 4.27).
 *
 * Frozen API check, regional regression matrix, formatter/parser round trips,
 * Unicode-heavy / RTL / message integration, concurrent formatters, fuzz/perf.
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboI18NVersion.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboUnicode/RoboUnicodeVersion.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const char *const k_phase4_tags[] = {
    "en-US", "en-GB", "fr-FR", "de-DE", "es-ES", "tr-TR", "ru-RU", "uk-UA",
    "pl-PL", "ar-SA", "fa-IR", "ur-PK", "he-IL", "hi-IN", "bn-BD", "th-TH",
    "zh-CN", "zh-TW", "ja-JP", "ko-KR",
};

static void test_p4_freeze_versions(void)
{
    robo_i18n_data_versions_t v;
    ROBO_CHECK_EQ_INT(ROBO_I18N_API_FROZEN, 1);
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.27.0");
    robo_i18n_data_versions(&v);
    ROBO_CHECK_STR_EQ(v.api_version, "0.27.0");
    ROBO_CHECK_STR_EQ(v.generator_version, "1.25.0");
    ROBO_CHECK_STR_EQ(v.cldr_version, "46");
    ROBO_CHECK_STR_EQ(v.unicode_version, "16.0.0");
    ROBO_CHECK_STR_EQ(v.tzdb_version, "2026c");
    ROBO_CHECK_STR_EQ(v.uca_version, "16.0.0");
    ROBO_CHECK_STR_EQ(v.iso4217_source, "CLDR-46-codeMappings");
    ROBO_CHECK_STR_EQ(ROBO_UNICODE_VERSION_STRING, v.unicode_version);
}

static void test_p4_regional_matrix(void)
{
    size_t i;
    for (i = 0; i < sizeof(k_phase4_tags) / sizeof(k_phase4_tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_locale_settings_t st;
        robo_i18n_locale_overrides_t ov;
        robo_i18n_number_formatter_t nfmt;
        robo_i18n_number_options_t nopts;
        robo_i18n_plural_operands_t ops;
        robo_i18n_plural_category_t cat = ROBO_I18N_PLURAL_OTHER;
        char buf[96];
        size_t n = 0;

        ROBO_CHECK_EQ_INT(
            robo_i18n_locale_parse_canonical(k_phase4_tags[i], strlen(k_phase4_tags[i]), &loc),
            ROBO_OK);
        robo_i18n_locale_overrides_init(&ov);
        ROBO_CHECK_EQ_INT(robo_i18n_locale_resolve(&loc, &ov, &st), ROBO_OK);
        ROBO_CHECK(st.numbering_system[0] != 0);
        ROBO_CHECK(st.calendar[0] != 0);

        robo_i18n_number_options_init(&nopts);
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&nfmt, &loc, &nopts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_double(&nfmt, 1234.5, buf, sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(n > 0);
        robo_i18n_number_formatter_destroy(&nfmt);

        ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string("1", 1, &ops), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_i18n_plural_select(&loc, ROBO_I18N_PLURAL_CARDINAL, &ops, &cat), ROBO_OK);
    }
}

static void test_p4_number_roundtrip(void)
{
    static const char *const tags[] = {"en-US", "de-DE", "fr-FR", "ar-SA", "hi-IN", "th-TH"};
    size_t ti;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        robo_i18n_number_options_t opts;
        robo_i18n_number_parser_t parser;
        robo_i18n_parse_options_t popts;
        robo_i18n_parse_result_t res;
        robo_i18n_decimal_t dec;
        char buf[128];
        size_t n = 0;

        ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tags[ti], strlen(tags[ti]), &loc),
                          ROBO_OK);
        robo_i18n_number_options_init(&opts);
        opts.max_fraction_digits = 2;
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        memset(&dec, 0, sizeof(dec));
        dec.kind = ROBO_I18N_DECIMAL_NUMBER;
        dec.digit_count = 4;
        memcpy(dec.digits, "1234", 4);
        dec.scale = 2; /* 12.34 */
        ROBO_CHECK_EQ_INT(robo_i18n_number_format(&fmt, &dec, buf, sizeof(buf), &n), ROBO_OK);
        ROBO_CHECK(n > 0);

        robo_i18n_parse_options_init(&popts);
        popts.mode = ROBO_I18N_PARSE_PERMISSIVE;
        ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&parser, &loc, &popts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&parser, buf, n, &res), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_PARSE_ERR_NONE);
        ROBO_CHECK_EQ_INT((int)res.value.kind, (int)ROBO_I18N_DECIMAL_NUMBER);
        ROBO_CHECK_EQ_INT((int)res.value.digit_count, 4);
        ROBO_CHECK(memcmp(res.value.digits, "1234", 4) == 0);
        ROBO_CHECK_EQ_INT((int)res.value.scale, 2);
        robo_i18n_number_parser_destroy(&parser);
        robo_i18n_number_formatter_destroy(&fmt);
    }
}

static void test_p4_currency_roundtrip(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    robo_i18n_currency_parser_t parser;
    robo_i18n_currency_parse_options_t popts;
    robo_i18n_parse_result_t res;
    char buf[128];
    size_t n = 0;

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("en-US", 5, &loc), ROBO_OK);
    robo_i18n_currency_options_init(&opts);
    memcpy(opts.currency, "USD", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format_minor(&fmt, 1234, buf, sizeof(buf), &n),
                      ROBO_OK);
    ROBO_CHECK(n > 0);

    robo_i18n_currency_parse_options_init(&popts);
    memcpy(popts.require_currency, "USD", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&parser, &loc, &popts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&parser, buf, n, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_PARSE_ERR_NONE);
    ROBO_CHECK_STR_EQ(res.currency, "USD");
    robo_i18n_currency_parser_destroy(&parser);
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void test_p4_datetime_roundtrip(void)
{
    static const char *const tags[] = {"en-US", "fr-FR", "ja-JP", "ar-SA", "zh-CN"};
    size_t ti;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        robo_i18n_locale_t loc;
        robo_i18n_datetime_formatter_t fmt;
        robo_i18n_datetime_parser_t parser;
        robo_i18n_dtfmt_options_t fopts;
        robo_i18n_dtparse_options_t popts;
        robo_i18n_dtparse_result_t res;
        robo_i18n_date_t date;
        char buf[128];
        size_t n = 0;

        ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tags[ti], strlen(tags[ti]), &loc),
                          ROBO_OK);
        robo_i18n_dtfmt_options_init(&fopts);
        fopts.date_style = ROBO_I18N_CAL_STYLE_SHORT;
        ROBO_CHECK_EQ_INT(robo_i18n_datetime_formatter_init(&fmt, &loc, NULL, NULL,
                                                            ROBO_I18N_DTFMT_DATE, &fopts),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, 2026, 9, 13), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(n > 0);

        robo_i18n_dtparse_options_init(&popts);
        popts.date_style = ROBO_I18N_CAL_STYLE_SHORT;
        ROBO_CHECK_EQ_INT(
            robo_i18n_datetime_parser_init(&parser, &loc, NULL, NULL, ROBO_I18N_DTFMT_DATE,
                                           &popts),
            ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, buf, n, &res), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_DTPARSE_ERR_NONE);
        ROBO_CHECK_EQ_INT((int)res.have_date, 1);
        ROBO_CHECK_EQ_INT(res.gregorian_date.year, 2026);
        ROBO_CHECK_EQ_INT((int)res.gregorian_date.month, 9);
        ROBO_CHECK_EQ_INT((int)res.gregorian_date.day, 13);
        robo_i18n_datetime_parser_destroy(&parser);
        robo_i18n_datetime_formatter_destroy(&fmt);
    }
}

static void test_p4_message_plural_kinds(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_message_t *msg = NULL;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char out[128];
    size_t n = 0;
    const char *card = "{n, plural, one {one card} other {other card}}";
    const char *ord = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("en-US", 5, &en), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_mf_parse(card, strlen(card), &msg), ROBO_OK);
    robo_i18n_mf_arg_set_i64(&arg, "n", 1);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format(msg, &en, &args, out, sizeof(out), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(out, "one card");
    robo_i18n_mf_arg_set_i64(&arg, "n", 2);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format(msg, &en, &args, out, sizeof(out), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(out, "other card");
    robo_i18n_mf_destroy(msg);

    ROBO_CHECK_EQ_INT(robo_i18n_mf_parse(ord, strlen(ord), &msg), ROBO_OK);
    robo_i18n_mf_arg_set_i64(&arg, "n", 2);
    ROBO_CHECK_EQ_INT(robo_i18n_mf_format(msg, &en, &args, out, sizeof(out), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(out, "2nd");
    robo_i18n_mf_destroy(msg);
}

static void test_p4_unicode_heavy(void)
{
    /* Combining mark: e + combining acute */
    static const char nfc_e[] = "\xC3\xA9";  /* é NFC */
    static const char nfd_e[] = "e\xCC\x81"; /* e + U+0301 */
    /* ZWJ family emoji (man+ZWJ+woman+ZWJ+girl) — validate UTF-8 only */
    static const char zwj[] =
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    /* Arabic + Latin mix */
    static const char mix[] = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    char *norm = NULL;
    size_t nlen = 0;
    int found = 0;
    robo_i18n_searcher_t searcher;
    robo_i18n_search_options_t sopts;
    robo_i18n_search_hit_t hit;
    robo_i18n_locale_t ar;

    ROBO_CHECK_EQ_INT(robo_unicode_normalize_nfc_utf8(nfd_e, strlen(nfd_e), ROBO_UNICODE_STRICT,
                                                      &norm, &nlen),
                      ROBO_OK);
    ROBO_CHECK(nlen == strlen(nfc_e));
    ROBO_CHECK(memcmp(norm, nfc_e, nlen) == 0);
    robo_free(norm);

    ROBO_CHECK(robo_unicode_utf8_validate(zwj, strlen(zwj)));
    ROBO_CHECK(robo_unicode_utf8_validate(mix, strlen(mix)));

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("ar-SA", 5, &ar), ROBO_OK);
    robo_i18n_search_options_init(&sopts);
    sopts.mode = ROBO_I18N_SEARCH_CANONICAL;
    sopts.grapheme_safe = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&searcher, &ar, &sopts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_find(&searcher, nfd_e, strlen(nfd_e), nfc_e,
                                            strlen(nfc_e), &found, &hit),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    ROBO_CHECK_EQ_INT((int)hit.start, 0);
    ROBO_CHECK((int)hit.end > 0);
    robo_i18n_searcher_destroy(&searcher);
}

static void test_p4_search_byte_ranges(void)
{
    robo_i18n_searcher_t s;
    robo_i18n_search_options_t o;
    robo_i18n_search_hit_t hit;
    int found = 0;
    /* "café" with NFC é — needle "fé" */
    const char *hay = "caf\xC3\xA9";
    const char *needle = "f\xC3\xA9";

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_EXACT;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&s, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_search_find(&s, hay, strlen(hay), needle, strlen(needle), &found, &hit),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    ROBO_CHECK_EQ_INT((int)hit.start, 2);
    ROBO_CHECK_EQ_INT((int)hit.end, (int)strlen(hay));
    /* Source byte range must stay within haystack. */
    ROBO_CHECK(hit.end <= strlen(hay));
    robo_i18n_searcher_destroy(&s);
}

static void test_p4_timezone_rtl(void)
{
    robo_i18n_timezone_t tz;
    robo_i18n_locale_t loc;
    char name[96];
    size_t n = 0;

    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("Asia/Karachi", 12, &tz), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("ur-PK", 5, &loc), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_display_name(&tz, ROBO_I18N_TZ_DISP_EXEMPLAR_CITY,
                                                      &loc, NULL, name, sizeof(name), &n),
                      ROBO_OK);
    ROBO_CHECK(n > 0);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
}

#ifdef _WIN32
typedef struct p4_thread_arg {
    int ok;
    int iters;
} p4_thread_arg_t;

static DWORD WINAPI p4_worker(LPVOID param)
{
    p4_thread_arg_t *a = (p4_thread_arg_t *)param;
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    char buf[64];
    size_t n = 0;
    int i;

    if (robo_i18n_locale_parse_canonical("en-US", 5, &loc) != ROBO_OK) {
        a->ok = 0;
        return 1;
    }
    robo_i18n_number_options_init(&opts);
    if (robo_i18n_number_formatter_init(&fmt, &loc, &opts) != ROBO_OK) {
        a->ok = 0;
        return 1;
    }
    for (i = 0; i < a->iters; i++) {
        if (robo_i18n_number_format_double(&fmt, (double)i, buf, sizeof(buf), &n) !=
            ROBO_OK) {
            robo_i18n_number_formatter_destroy(&fmt);
            a->ok = 0;
            return 1;
        }
    }
    robo_i18n_number_formatter_destroy(&fmt);
    a->ok = 1;
    return 0;
}

static void test_p4_threads(void)
{
    enum { N = 4 };
    HANDLE hs[N];
    p4_thread_arg_t args[N];
    int i;
    for (i = 0; i < N; i++) {
        args[i].ok = 0;
        args[i].iters = 500;
        hs[i] = CreateThread(NULL, 0, p4_worker, &args[i], 0, NULL);
        ROBO_CHECK(hs[i] != NULL);
    }
    WaitForMultipleObjects(N, hs, TRUE, INFINITE);
    for (i = 0; i < N; i++) {
        CloseHandle(hs[i]);
        ROBO_CHECK_EQ_INT(args[i].ok, 1);
    }
}
#else
static void test_p4_threads(void)
{
    /* Host tests currently target Windows; treat as skip-pass elsewhere. */
    ROBO_CHECK(1);
}
#endif

static void test_p4_fuzz_limits(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_mf_message_t *msg = NULL;
    (void)robo_i18n_locale_parse_canonical("\x80\x81", 2, &loc);
    (void)robo_i18n_locale_parse_canonical(
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        76, &loc);
    (void)robo_i18n_mf_parse("{", 1, &msg);
    if (msg) {
        robo_i18n_mf_destroy(msg);
    }
    (void)robo_i18n_mf_validate("{n, plural, one {x}", 18);
}

static void test_p4_perf_smoke(void)
{
    clock_t t0, t1;
    size_t i;
    char out[64];
    size_t n = 0;
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    robo_i18n_plural_category_t cat;

    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical("en-US", 5, &loc), ROBO_OK);
    robo_i18n_number_options_init(&opts);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    t0 = clock();
    for (i = 0; i < 3000u; i++) {
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_double(&fmt, (double)i, out, sizeof(out), &n),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&loc, (int64_t)i, &cat), ROBO_OK);
    }
    t1 = clock();
    robo_i18n_number_formatter_destroy(&fmt);
    ROBO_CHECK((t1 - t0) < 30 * CLOCKS_PER_SEC);
}

void robo_test_register_i18n_phase4(void)
{
    robo_test_register("i18n.p4.freeze", test_p4_freeze_versions);
    robo_test_register("i18n.p4.regional_matrix", test_p4_regional_matrix);
    robo_test_register("i18n.p4.number_roundtrip", test_p4_number_roundtrip);
    robo_test_register("i18n.p4.currency_roundtrip", test_p4_currency_roundtrip);
    robo_test_register("i18n.p4.datetime_roundtrip", test_p4_datetime_roundtrip);
    robo_test_register("i18n.p4.message_kinds", test_p4_message_plural_kinds);
    robo_test_register("i18n.p4.unicode_heavy", test_p4_unicode_heavy);
    robo_test_register("i18n.p4.search_ranges", test_p4_search_byte_ranges);
    robo_test_register("i18n.p4.timezone_rtl", test_p4_timezone_rtl);
    robo_test_register("i18n.p4.threads", test_p4_threads);
    robo_test_register("i18n.p4.fuzz", test_p4_fuzz_limits);
    robo_test_register("i18n.p4.perf_smoke", test_p4_perf_smoke);
}
