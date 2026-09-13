/*
 * RoboI18N number & currency parsing suite (Goal 4.22).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_number(const char *tag,
                          robo_i18n_parse_mode_t mode,
                          const char *text,
                          const char *want_digits,
                          uint8_t want_scale,
                          uint8_t want_neg)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_parser_t p;
    robo_i18n_parse_options_t opts;
    robo_i18n_parse_result_t res;

    parse_tag(tag, &loc);
    robo_i18n_parse_options_init(&opts);
    opts.mode = mode;
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, text, strlen(text), &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_PARSE_ERR_NONE);
    ROBO_CHECK_EQ_INT((int)res.value.kind, (int)ROBO_I18N_DECIMAL_NUMBER);
    ROBO_CHECK_EQ_INT((int)res.value.scale, (int)want_scale);
    ROBO_CHECK_EQ_INT((int)res.value.negative, (int)want_neg);
    ROBO_CHECK_EQ_INT((int)res.value.digit_count, (int)strlen(want_digits));
    ROBO_CHECK(memcmp(res.value.digits, want_digits, res.value.digit_count) == 0);
    robo_i18n_number_parser_destroy(&p);
}

static void expect_number_fail(const char *tag,
                               robo_i18n_parse_mode_t mode,
                               const char *text,
                               robo_i18n_parse_err_t err)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_parser_t p;
    robo_i18n_parse_options_t opts;
    robo_i18n_parse_result_t res;

    parse_tag(tag, &loc);
    robo_i18n_parse_options_init(&opts);
    opts.mode = mode;
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, text, strlen(text), &res),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)res.error, (int)err);
    robo_i18n_number_parser_destroy(&p);
}

static void test_parse_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.25.0");
    ROBO_CHECK_STR_EQ(robo_i18n_parse_err_string(ROBO_I18N_PARSE_ERR_NONE), "ok");
}

static void test_parse_en_basic(void)
{
    expect_number("en-US", ROBO_I18N_PARSE_STRICT, "1234", "1234", 0, 0);
    expect_number("en-US", ROBO_I18N_PARSE_STRICT, "1,234.56", "123456", 2, 0);
    expect_number("en-US", ROBO_I18N_PARSE_STRICT, "-1,234.50", "123450", 2, 1);
    expect_number("en-US", ROBO_I18N_PARSE_STRICT, "0", "0", 0, 0);
    expect_number("en-US", ROBO_I18N_PARSE_STRICT, "1.0", "10", 1, 0);
    expect_number("en-US", ROBO_I18N_PARSE_STRICT, "1.00", "100", 2, 0);
    expect_number_fail("en-US", ROBO_I18N_PARSE_STRICT, "1,23.45",
                       ROBO_I18N_PARSE_ERR_INVALID_GROUPING);
    expect_number_fail("en-US", ROBO_I18N_PARSE_STRICT, "12.34.56",
                       ROBO_I18N_PARSE_ERR_TRAILING);
}

static void test_parse_de_fr(void)
{
    expect_number("de-DE", ROBO_I18N_PARSE_STRICT, "1.234,56", "123456", 2, 0);
    expect_number("fr-FR", ROBO_I18N_PARSE_STRICT, "1\xE2\x80\xAF" "234,56", "123456", 2,
                  0); /* U+202F narrow NBSP */
    expect_number("es-ES", ROBO_I18N_PARSE_STRICT, "1.234,5", "12345", 1, 0);
}

static void test_parse_hi_grouping(void)
{
    /* Indian secondary grouping of 2 */
    expect_number("hi-IN", ROBO_I18N_PARSE_STRICT, "1,23,456.78", "12345678", 2, 0);
}

static void test_parse_localized_digits(void)
{
    /* Arabic-Indic ١٢٣٤٫٥٦ with arab numbering — use ar with nu=arab if needed */
    robo_i18n_locale_t loc;
    robo_i18n_number_parser_t p;
    robo_i18n_parse_options_t opts;
    robo_i18n_parse_result_t res;

    parse_tag("ar", &loc);
    robo_i18n_parse_options_init(&opts);
    memcpy(opts.numbering_system, "arab", 5);
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);
    /* ١٢٣٤٫٥٦ = 1234.56 in arab digits + arab decimal */
    {
        const char *t = "\xD9\xA1\xD9\xA2\xD9\xA3\xD9\xA4\xD9\xAB\xD9\xA5\xD9\xA6";
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, t, strlen(t), &res), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)res.value.scale, 2);
        ROBO_CHECK(memcmp(res.value.digits, "123456", 6) == 0);
    }
    robo_i18n_number_parser_destroy(&p);

    parse_tag("fa", &loc);
    robo_i18n_parse_options_init(&opts);
    memcpy(opts.numbering_system, "arabext", 8);
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);
    {
        /* ۰۱۲ arabext */
        const char *t = "\xDB\xB1\xDB\xB2\xDB\xB3";
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, t, strlen(t), &res), ROBO_OK);
        ROBO_CHECK(memcmp(res.value.digits, "123", 3) == 0);
    }
    robo_i18n_number_parser_destroy(&p);

    parse_tag("hi", &loc);
    robo_i18n_parse_options_init(&opts);
    memcpy(opts.numbering_system, "deva", 5);
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);
    {
        const char *t = "\xE0\xA5\xA7\xE0\xA5\xA8\xE0\xA5\xA9"; /* १२३ */
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, t, strlen(t), &res), ROBO_OK);
        ROBO_CHECK(memcmp(res.value.digits, "123", 3) == 0);
    }
    robo_i18n_number_parser_destroy(&p);

    parse_tag("bn", &loc);
    robo_i18n_parse_options_init(&opts);
    memcpy(opts.numbering_system, "beng", 5);
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);
    {
        const char *t = "\xE0\xA7\xA7\xE0\xA7\xA8"; /* ১২ */
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, t, strlen(t), &res), ROBO_OK);
        ROBO_CHECK(memcmp(res.value.digits, "12", 2) == 0);
    }
    robo_i18n_number_parser_destroy(&p);

    parse_tag("th", &loc);
    robo_i18n_parse_options_init(&opts);
    memcpy(opts.numbering_system, "thai", 5);
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);
    {
        const char *t = "\xE0\xB9\x91\xE0\xB9\x92\xE0\xB9\x93"; /* ๑๒๓ */
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, t, strlen(t), &res), ROBO_OK);
        ROBO_CHECK(memcmp(res.value.digits, "123", 3) == 0);
    }
    robo_i18n_number_parser_destroy(&p);
}

static void test_parse_permissive(void)
{
    expect_number("en-US", ROBO_I18N_PARSE_PERMISSIVE, "1234.56", "123456", 2, 0);
    expect_number("en-US", ROBO_I18N_PARSE_PERMISSIVE, "1,23,456", "123456", 0, 0);
}

static void test_parse_roundtrip(void)
{
    const char *tags[] = {"en-US", "de-DE", "fr-FR", "ja-JP", "zh-CN", "ko-KR", "pl-PL",
                          "ru-RU"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        robo_i18n_number_parser_t parser;
        robo_i18n_parse_options_t popts;
        robo_i18n_number_options_t fopts;
        robo_i18n_decimal_t dec;
        robo_i18n_parse_result_t res;
        char buf[128];
        size_t len = 0;

        parse_tag(tags[i], &loc);
        robo_i18n_number_options_init(&fopts);
        fopts.min_fraction_digits = 2;
        fopts.max_fraction_digits = 2;
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &fopts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_decimal_from_i64(123456, &dec), ROBO_OK);
        /* 1234.56 */
        dec.digit_count = 6;
        memcpy(dec.digits, "123456", 6);
        dec.scale = 2;
        ROBO_CHECK_EQ_INT(robo_i18n_number_format(&fmt, &dec, buf, sizeof(buf), &len),
                          ROBO_OK);

        robo_i18n_parse_options_init(&popts);
        popts.mode = ROBO_I18N_PARSE_STRICT;
        ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&parser, &loc, &popts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&parser, buf, len, &res), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)res.value.scale, 2);
        ROBO_CHECK(memcmp(res.value.digits, "123456", 6) == 0);
        robo_i18n_number_parser_destroy(&parser);
        robo_i18n_number_formatter_destroy(&fmt);
    }
}

static void test_parse_currency_basic(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_parser_t p;
    robo_i18n_currency_parse_options_t opts;
    robo_i18n_parse_result_t res;

    parse_tag("en-US", &loc);
    robo_i18n_currency_parse_options_init(&opts);
    opts.number.mode = ROBO_I18N_PARSE_STRICT;
    memcpy(opts.require_currency, "USD", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&p, &loc, &opts), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&p, "$1,234.56", 9, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.currency, "USD");
    ROBO_CHECK(memcmp(res.value.digits, "123456", 6) == 0);
    ROBO_CHECK_EQ_INT((int)res.value.scale, 2);

    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&p, "USD 10.00", 9, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.currency, "USD");

    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&p, "($12.00)", 8, &res), ROBO_OK);
    ROBO_CHECK(res.value.negative);
    ROBO_CHECK(memcmp(res.value.digits, "1200", 4) == 0);

    robo_i18n_currency_parser_destroy(&p);
}

static void test_parse_currency_locales(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_parser_t p;
    robo_i18n_currency_parse_options_t opts;
    robo_i18n_parse_result_t res;

    parse_tag("de-DE", &loc);
    robo_i18n_currency_parse_options_init(&opts);
    memcpy(opts.require_currency, "EUR", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&p, &loc, &opts), ROBO_OK);
    {
        const char *text = "1.234,56\xC2\xA0\xE2\x82\xAC";
        ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&p, text, strlen(text), &res), ROBO_OK);
    }
    ROBO_CHECK_STR_EQ(res.currency, "EUR");
    robo_i18n_currency_parser_destroy(&p);

    parse_tag("en-GB", &loc);
    robo_i18n_currency_parse_options_init(&opts);
    memcpy(opts.require_currency, "GBP", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&p, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&p, "\xC2\xA3" "10.00", 7, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.currency, "GBP");
    robo_i18n_currency_parser_destroy(&p);

    parse_tag("ja-JP", &loc);
    robo_i18n_currency_parse_options_init(&opts);
    memcpy(opts.require_currency, "JPY", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&p, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&p, "JPY 1000", 8, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.currency, "JPY");
    robo_i18n_currency_parser_destroy(&p);
}

static void test_parse_currency_ambiguous(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_parser_t p;
    robo_i18n_currency_parse_options_t opts;
    robo_i18n_parse_result_t res;

    parse_tag("en-US", &loc);
    robo_i18n_currency_parse_options_init(&opts);
    opts.number.mode = ROBO_I18N_PARSE_STRICT;
    /* No require_currency — bare $ may be ambiguous across USD/CAD/... */
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&p, &loc, &opts), ROBO_OK);
    {
        robo_result_t r = robo_i18n_parse_currency(&p, "$5.00", 5, &res);
        /* Either unique USD in display data, or ambiguous — both OK if deterministic */
        if (r == ROBO_PARSE_ERROR) {
            ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_PARSE_ERR_AMBIGUOUS_CURRENCY);
            ROBO_CHECK(res.ambiguous_count >= 1u);
        } else {
            ROBO_CHECK_EQ_INT(r, ROBO_OK);
            ROBO_CHECK(res.currency[0] != '\0');
        }
    }
    robo_i18n_currency_parser_destroy(&p);

    /* Permissive resolves to locale default when among matches */
    robo_i18n_currency_parse_options_init(&opts);
    opts.number.mode = ROBO_I18N_PARSE_PERMISSIVE;
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&p, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&p, "$5.00", 5, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.currency, "USD");
    robo_i18n_currency_parser_destroy(&p);
}

static void test_parse_currency_roundtrip(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t fopts;
    robo_i18n_currency_parser_t parser;
    robo_i18n_currency_parse_options_t popts;
    robo_i18n_parse_result_t res;
    char buf[128];
    size_t len = 0;

    parse_tag("en-US", &loc);
    robo_i18n_currency_options_init(&fopts);
    memcpy(fopts.currency, "USD", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &fopts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format_minor(&fmt, 12345, buf, sizeof(buf), &len),
                      ROBO_OK);

    robo_i18n_currency_parse_options_init(&popts);
    memcpy(popts.require_currency, "USD", 4);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_parser_init(&parser, &loc, &popts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_currency(&parser, buf, len, &res), ROBO_OK);
    ROBO_CHECK_STR_EQ(res.currency, "USD");
    ROBO_CHECK(memcmp(res.value.digits, "12345", 5) == 0);
    ROBO_CHECK_EQ_INT((int)res.value.scale, 2);
    robo_i18n_currency_parser_destroy(&parser);
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void test_parse_robust(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_parser_t p;
    robo_i18n_parse_options_t opts;
    robo_i18n_parse_result_t res;
    char longbuf[5000];
    size_t i;

    parse_tag("en", &loc);
    robo_i18n_parse_options_init(&opts);
    ROBO_CHECK_EQ_INT(robo_i18n_number_parser_init(&p, &loc, &opts), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, "", 0, &res), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_PARSE_ERR_EMPTY);

    /* malformed UTF-8 */
    {
        char bad[2] = {(char)0xC3, (char)0x28};
        ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, bad, 2, &res), ROBO_PARSE_ERROR);
        ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_PARSE_ERR_INVALID_UTF8);
    }

    memset(longbuf, '1', sizeof(longbuf));
    ROBO_CHECK_EQ_INT(robo_i18n_parse_number(&p, longbuf, sizeof(longbuf), &res),
                      ROBO_INVALID_ARGUMENT);

    for (i = 0; i < 64u; i++) {
        char junk[8];
        junk[0] = (char)(i + 1);
        junk[1] = ',';
        junk[2] = '.';
        junk[3] = '$';
        junk[4] = 0;
        (void)robo_i18n_parse_number(&p, junk, 4, &res);
    }
    robo_i18n_number_parser_destroy(&p);
}

void robo_test_register_i18n_parse(void)
{
    robo_test_register("i18n.parse.versions", test_parse_versions);
    robo_test_register("i18n.parse.en", test_parse_en_basic);
    robo_test_register("i18n.parse.de_fr", test_parse_de_fr);
    robo_test_register("i18n.parse.hi", test_parse_hi_grouping);
    robo_test_register("i18n.parse.digits", test_parse_localized_digits);
    robo_test_register("i18n.parse.permissive", test_parse_permissive);
    robo_test_register("i18n.parse.roundtrip", test_parse_roundtrip);
    robo_test_register("i18n.parse.currency", test_parse_currency_basic);
    robo_test_register("i18n.parse.currency_locales", test_parse_currency_locales);
    robo_test_register("i18n.parse.currency_ambig", test_parse_currency_ambiguous);
    robo_test_register("i18n.parse.currency_roundtrip", test_parse_currency_roundtrip);
    robo_test_register("i18n.parse.robust", test_parse_robust);
}
