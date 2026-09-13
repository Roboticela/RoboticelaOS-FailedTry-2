/*
 * RoboI18N currency metadata + formatting suite (Goal 4.21).
 */

#include "RoboI18N/RoboCurrencyData.h"
#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_currency(const char *tag,
                            const char *code,
                            robo_i18n_currency_style_t style,
                            int64_t major,
                            const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    char buf[256];
    size_t len = 0;

    parse_tag(tag, &loc);
    robo_i18n_currency_options_init(&opts);
    opts.style = style;
    if (code && code[0]) {
        memcpy(opts.currency, code, 4u);
    }
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format_i64(&fmt, major, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void expect_currency_dec(const char *tag,
                                const char *code,
                                robo_i18n_currency_style_t style,
                                const char *digits,
                                uint8_t scale,
                                uint8_t negative,
                                const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    robo_i18n_decimal_t dec;
    char buf[256];
    size_t len = 0;
    size_t n;

    parse_tag(tag, &loc);
    robo_i18n_currency_options_init(&opts);
    opts.style = style;
    if (code && code[0]) {
        memcpy(opts.currency, code, 4u);
    }
    memset(&dec, 0, sizeof(dec));
    dec.kind = ROBO_I18N_DECIMAL_NUMBER;
    dec.negative = negative;
    dec.scale = scale;
    n = strlen(digits);
    ROBO_CHECK(n > 0u && n <= ROBO_I18N_DECIMAL_DIGITS_MAX);
    dec.digit_count = (uint8_t)n;
    memcpy(dec.digits, digits, n);

    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format(&fmt, &dec, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void test_currency_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.25.0");
    ROBO_CHECK_STR_EQ(robo_i18n_currency_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(ROBO_CURRENCY_CLDR_VERSION, "46");
    ROBO_CHECK(robo_curr_meta_count > 100u);
    ROBO_CHECK(robo_curr_region_count > 100u);
    ROBO_CHECK(robo_curr_fmt_count > 10u);
}

static void test_currency_meta(void)
{
    robo_i18n_currency_info_t info;
    char code[4];

    ROBO_CHECK_EQ_INT(robo_i18n_currency_normalize("usd", 3, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "USD");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_normalize("US", 2, code), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_normalize("12A", 3, code), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup("USD", 3, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)info.digits, 2);
    ROBO_CHECK_EQ_INT((int)info.numeric, 840);
    ROBO_CHECK(info.known);

    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup("JPY", 3, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)info.digits, 0);

    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup("KWD", 3, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)info.digits, 3);

    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup("CHF", 3, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)info.digits, 2);
    ROBO_CHECK_EQ_INT((int)info.cash_rounding, 5);

    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup("PKR", 3, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)info.cash_digits, 0);

    /* Well-formed unknown → defaults, NOT_FOUND */
    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup("ZZZ", 3, &info), ROBO_NOT_FOUND);
    ROBO_CHECK_STR_EQ(info.code, "ZZZ");
    ROBO_CHECK_EQ_INT((int)info.digits, 2);
    ROBO_CHECK(!info.known);
}

static void test_currency_region_locale(void)
{
    char code[4];
    robo_i18n_locale_t loc;

    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_region("US", 2, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "USD");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_region("GB", 2, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "GBP");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_region("DE", 2, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "EUR");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_region("JP", 2, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "JPY");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_region("PK", 2, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "PKR");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_region("BD", 2, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "BDT");

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_locale(&loc, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "USD");

    parse_tag("en-US-u-cu-eur", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_locale(&loc, code), ROBO_OK);
    ROBO_CHECK_STR_EQ(code, "EUR");
}

static void test_currency_en_us(void)
{
    expect_currency("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 1234, "$1,234.00");
    expect_currency("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 0, "$0.00");
    expect_currency_dec("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_SYMBOL, "12345", 2, 1,
                        "-$123.45");
    expect_currency("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_CODE, 12, "USD\xC2\xA0" "12.00");
    expect_currency("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_NAME, 1, "1.00 US dollar");
    expect_currency("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_NAME, 2, "2.00 US dollars");
}

static void test_currency_accounting(void)
{
    expect_currency_dec("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_ACCOUNTING, "12345", 2, 1,
                        "($123.45)");
    expect_currency("en-US", "USD", ROBO_I18N_CURRENCY_STYLE_ACCOUNTING, 10, "$10.00");
    /* fr accounting uses parentheses without hard-coded US rules */
    expect_currency_dec("fr-FR", "EUR", ROBO_I18N_CURRENCY_STYLE_ACCOUNTING, "12345", 2, 1,
                        "(123,45\xC2\xA0\xE2\x82\xAC)");
}

static void test_currency_intl_matrix(void)
{
    expect_currency("en-GB", "GBP", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 10, "\xC2\xA3" "10.00");
    expect_currency("de-DE", "EUR", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 10,
                    "10,00\xC2\xA0\xE2\x82\xAC");
    expect_currency("fr-FR", "EUR", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 10,
                    "10,00\xC2\xA0\xE2\x82\xAC");
    expect_currency("ja-JP", "JPY", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 1234,
                    "\xEF\xBF\xA5" "1,234"); /* U+FFE5 fullwidth yen */
    expect_currency("zh-CN", "CNY", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 10, "\xC2\xA5" "10.00");
    expect_currency("hi-IN", "INR", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 123456,
                    "\xE2\x82\xB9" "1,23,456.00");
    expect_currency("ko-KR", "KRW", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 1000, "\xE2\x82\xA9" "1,000");

    /* ur-PK / ar-SA / fa-IR / bn-BD — symbol or code path must succeed */
    {
        robo_i18n_locale_t loc;
        robo_i18n_currency_formatter_t fmt;
        robo_i18n_currency_options_t opts;
        char buf[256];
        size_t len = 0;
        const char *cases[][2] = {
            {"ur-PK", "PKR"}, {"ar-SA", "SAR"}, {"fa-IR", "IRR"}, {"bn-BD", "BDT"}};
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            parse_tag(cases[i][0], &loc);
            robo_i18n_currency_options_init(&opts);
            memcpy(opts.currency, cases[i][1], 4u);
            ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(
                robo_i18n_currency_format_i64(&fmt, 100, buf, sizeof(buf), &len), ROBO_OK);
            ROBO_CHECK(len > 0u);
            robo_i18n_currency_formatter_destroy(&fmt);
        }
    }
}

static void test_currency_zero_and_three_decimals(void)
{
    expect_currency("en-US", "JPY", ROBO_I18N_CURRENCY_STYLE_SYMBOL, 1000, "\xC2\xA5" "1,000");
    expect_currency_dec("en-US", "KWD", ROBO_I18N_CURRENCY_STYLE_SYMBOL, "1234567", 3, 0,
                        "KWD\xC2\xA0" "1,234.567");
}

static void test_currency_cash_rounding(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    robo_i18n_decimal_t dec;
    char buf[128];
    size_t len = 0;

    parse_tag("de-CH", &loc);
    robo_i18n_currency_options_init(&opts);
    memcpy(opts.currency, "CHF", 4u);
    opts.usage = ROBO_I18N_CURRENCY_USAGE_CASH;
    opts.style = ROBO_I18N_CURRENCY_STYLE_CODE;

    memset(&dec, 0, sizeof(dec));
    dec.kind = ROBO_I18N_DECIMAL_NUMBER;
    dec.digit_count = 4;
    dec.scale = 2;
    memcpy(dec.digits, "1203", 4); /* 12.03 → cash 12.05 */

    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format(&fmt, &dec, buf, sizeof(buf), &len),
                      ROBO_OK);
    /* de pattern places currency after number with NBSP when alpha */
    ROBO_CHECK(strstr(buf, "12.05") != NULL || strstr(buf, "12,05") != NULL);
    robo_i18n_currency_formatter_destroy(&fmt);

    /* STANDARD must NOT apply cash rounding */
    opts.usage = ROBO_I18N_CURRENCY_USAGE_STANDARD;
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format(&fmt, &dec, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK(strstr(buf, "12.03") != NULL || strstr(buf, "12,03") != NULL);
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void test_currency_minor_units(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    char buf[128];
    size_t len = 0;

    parse_tag("en-US", &loc);
    robo_i18n_currency_options_init(&opts);
    memcpy(opts.currency, "USD", 4u);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format_minor(&fmt, 12345, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "$123.45");
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format_minor(&fmt, -50, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "-$0.50");
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void test_currency_cf_account(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    robo_i18n_decimal_t dec;
    char buf[128];
    size_t len = 0;

    parse_tag("en-US-u-cf-account", &loc);
    robo_i18n_currency_options_init(&opts);
    memcpy(opts.currency, "USD", 4u);
    memset(&dec, 0, sizeof(dec));
    dec.kind = ROBO_I18N_DECIMAL_NUMBER;
    dec.negative = 1;
    dec.digit_count = 3;
    dec.scale = 2;
    memcpy(dec.digits, "100", 3);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK(fmt.accounting);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format(&fmt, &dec, buf, sizeof(buf), &len),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "($1.00)");
    robo_i18n_currency_formatter_destroy(&fmt);
}

static void test_currency_robustness(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_currency_formatter_t fmt;
    robo_i18n_currency_options_t opts;
    robo_i18n_currency_info_t info;
    char buf[64];
    size_t len = 0;
    size_t i;

    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup(NULL, 3, &info), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_lookup("", 0, &info), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_for_region("", 0, buf), ROBO_INVALID_ARGUMENT);

    parse_tag("en", &loc);
    robo_i18n_currency_options_init(&opts);
    memcpy(opts.currency, "!!!", 3);
    opts.currency[3] = '\0';
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts),
                      ROBO_INVALID_ARGUMENT);

    memcpy(opts.currency, "USD", 4u);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_currency_format(&fmt, NULL, buf, sizeof(buf), &len),
                      ROBO_INVALID_ARGUMENT);

    /* Fuzz well-formed codes */
    for (i = 0; i < 26u; i++) {
        char c[4] = {'A', 'A', (char)('A' + (i % 26)), '\0'};
        (void)robo_i18n_currency_lookup(c, 3, &info);
        memcpy(opts.currency, c, 4u);
        if (robo_i18n_currency_formatter_init(&fmt, &loc, &opts) == ROBO_OK) {
            (void)robo_i18n_currency_format_i64(&fmt, (int64_t)i * 1000, buf, sizeof(buf),
                                                &len);
            robo_i18n_currency_formatter_destroy(&fmt);
        }
    }

    /* Malformed locale must not crash */
    if (robo_i18n_locale_parse_canonical("@@@", 3, &loc) != ROBO_OK) {
        ROBO_CHECK(1);
    }
}

void robo_test_register_i18n_currency(void)
{
    robo_test_register("i18n.currency.versions", test_currency_versions);
    robo_test_register("i18n.currency.meta", test_currency_meta);
    robo_test_register("i18n.currency.region", test_currency_region_locale);
    robo_test_register("i18n.currency.en_us", test_currency_en_us);
    robo_test_register("i18n.currency.accounting", test_currency_accounting);
    robo_test_register("i18n.currency.intl", test_currency_intl_matrix);
    robo_test_register("i18n.currency.precision", test_currency_zero_and_three_decimals);
    robo_test_register("i18n.currency.cash", test_currency_cash_rounding);
    robo_test_register("i18n.currency.minor", test_currency_minor_units);
    robo_test_register("i18n.currency.cf", test_currency_cf_account);
    robo_test_register("i18n.currency.robust", test_currency_robustness);
}
