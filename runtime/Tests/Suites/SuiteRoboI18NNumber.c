/*
 * RoboI18N number formatting suite (Goal 4.3).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void fmt_i64(const char *tag,
                    const robo_i18n_number_options_t *opts,
                    int64_t value,
                    const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    char buf[128];
    size_t n = 0;
    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, value, buf, sizeof(buf), &n), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    ROBO_CHECK_EQ_INT((int)n, (int)strlen(want));
    robo_i18n_number_formatter_destroy(&fmt);
}

static void fmt_dec(const char *tag,
                    const robo_i18n_number_options_t *opts,
                    const robo_i18n_decimal_t *dec,
                    const char *want)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    char buf[192];
    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_number_format(&fmt, dec, buf, sizeof(buf), NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, want);
    robo_i18n_number_formatter_destroy(&fmt);
}

static void make_dec(const char *ascii, uint8_t scale, uint8_t neg, robo_i18n_decimal_t *out)
{
    size_t n = strlen(ascii);
    memset(out, 0, sizeof(*out));
    out->kind = ROBO_I18N_DECIMAL_NUMBER;
    out->negative = neg;
    out->scale = scale;
    out->digit_count = (uint8_t)n;
    memcpy(out->digits, ascii, n);
}

static void test_number_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.22.0");
    ROBO_CHECK_STR_EQ(robo_i18n_cldr_version_string(), "46");
}

static void test_number_en_us_integers(void)
{
    robo_i18n_number_options_t opts;
    robo_i18n_number_options_init(&opts);
    opts.max_fraction_digits = 0;
    fmt_i64("en-US", &opts, 0, "0");
    fmt_i64("en-US", &opts, 1, "1");
    fmt_i64("en-US", &opts, 10, "10");
    fmt_i64("en-US", &opts, 999, "999");
    fmt_i64("en-US", &opts, 1000, "1,000");
    fmt_i64("en-US", &opts, 1234567, "1,234,567");
    fmt_i64("en-US", &opts, -1234567, "-1,234,567");
    opts.use_grouping = 0;
    fmt_i64("en-US", &opts, 1234567, "1234567");
}

static void test_number_locale_symbols(void)
{
    robo_i18n_number_options_t opts;
    robo_i18n_decimal_t dec;
    robo_i18n_number_options_init(&opts);
    opts.max_fraction_digits = 2;
    opts.min_fraction_digits = 2;

    make_dec("123456", 2, 0, &dec); /* 1234.56 */
    fmt_dec("en-US", &opts, &dec, "1,234.56");
    fmt_dec("de-DE", &opts, &dec, "1.234,56");
    fmt_dec("fr-FR", &opts, &dec, "1\xE2\x80\xAF" "234,56"); /* U+202F */

    make_dec("1234567", 0, 0, &dec);
    opts.max_fraction_digits = 0;
    opts.min_fraction_digits = 0;
    /* Force Latin digits so the assertion checks grouping, not digit glyphs. */
    memcpy(opts.numbering_system, "latn", 5);
    fmt_dec("hi-IN", &opts, &dec, "12,34,567");
    fmt_dec("bn-BD", &opts, &dec, "12,34,567");
    opts.numbering_system[0] = '\0';
}

static void test_number_digits_localized(void)
{
    robo_i18n_number_options_t opts;
    robo_i18n_number_options_init(&opts);
    opts.max_fraction_digits = 0;

    /* ar-SA default numbering arab; 1234 → 1٬234 */
    {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        char buf[64];
        parse_tag("ar-SA", &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, 1234, buf, sizeof(buf), NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "\xD9\xA1\xD9\xAC\xD9\xA2\xD9\xA3\xD9\xA4");
        robo_i18n_number_formatter_destroy(&fmt);
    }

    /* fa-IR arabext */
    {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        char buf[64];
        parse_tag("fa-IR", &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, 12, buf, sizeof(buf), NULL), ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "\xDB\xB1\xDB\xB2");
        robo_i18n_number_formatter_destroy(&fmt);
    }

    /* Explicit thai digits on th-TH */
    {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        char buf[64];
        parse_tag("th-TH", &loc);
        opts.numbering_system[0] = 't';
        opts.numbering_system[1] = 'h';
        opts.numbering_system[2] = 'a';
        opts.numbering_system[3] = 'i';
        opts.numbering_system[4] = '\0';
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, 5, buf, sizeof(buf), NULL), ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "\xE0\xB9\x95");
        robo_i18n_number_formatter_destroy(&fmt);
        opts.numbering_system[0] = '\0';
    }

    /* hi-IN with native override would still use latn by default; force deva */
    {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        char buf[64];
        parse_tag("hi-IN", &loc);
        memcpy(opts.numbering_system, "deva", 5);
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, 7, buf, sizeof(buf), NULL), ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "\xE0\xA5\xAD");
        robo_i18n_number_formatter_destroy(&fmt);
        opts.numbering_system[0] = '\0';
    }

    /* bn native default is beng */
    {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        char buf[64];
        parse_tag("bn-BD", &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, 9, buf, sizeof(buf), NULL), ROBO_OK);
        ROBO_CHECK_STR_EQ(buf, "\xE0\xA7\xAF");
        robo_i18n_number_formatter_destroy(&fmt);
    }
}

static void test_number_rounding(void)
{
    robo_i18n_number_options_t opts;
    robo_i18n_decimal_t dec;
    robo_i18n_number_options_init(&opts);
    opts.max_fraction_digits = 0;
    opts.min_fraction_digits = 0;
    opts.use_grouping = 0;
    opts.rounding = ROBO_I18N_ROUND_HALF_EVEN;

    make_dec("15", 1, 0, &dec); /* 1.5 → 2 (even) */
    fmt_dec("en-US", &opts, &dec, "2");
    make_dec("25", 1, 0, &dec); /* 2.5 → 2 */
    fmt_dec("en-US", &opts, &dec, "2");
    make_dec("14", 1, 0, &dec); /* 1.4 → 1 */
    fmt_dec("en-US", &opts, &dec, "1");
    make_dec("16", 1, 0, &dec); /* 1.6 → 2 */
    fmt_dec("en-US", &opts, &dec, "2");

    opts.rounding = ROBO_I18N_ROUND_HALF_UP;
    make_dec("25", 1, 0, &dec);
    fmt_dec("en-US", &opts, &dec, "3");

    make_dec("15", 1, 1, &dec); /* -1.5 half-up → -2 */
    fmt_dec("en-US", &opts, &dec, "-2");
}

static void test_number_fraction_options(void)
{
    robo_i18n_number_options_t opts;
    robo_i18n_decimal_t dec;
    robo_i18n_number_options_init(&opts);
    opts.use_grouping = 0;

    make_dec("1234", 0, 0, &dec);
    opts.min_fraction_digits = 2;
    opts.max_fraction_digits = 2;
    fmt_dec("en-US", &opts, &dec, "1234.00");

    make_dec("12345678", 4, 0, &dec); /* 1234.5678 */
    opts.min_fraction_digits = 0;
    opts.max_fraction_digits = 2;
    opts.rounding = ROBO_I18N_ROUND_HALF_UP;
    fmt_dec("en-US", &opts, &dec, "1234.57");

    opts.min_integer_digits = 5;
    opts.max_fraction_digits = 0;
    make_dec("12", 0, 0, &dec);
    fmt_dec("en-US", &opts, &dec, "00012");
}

static double bits_to_double(uint64_t bits)
{
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

static void test_number_double_specials(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    char buf[64];

    robo_i18n_number_options_init(&opts);
    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);

    ROBO_CHECK_EQ_INT(
        robo_i18n_number_format_double(&fmt, bits_to_double(0x7ff8000000000001ULL), buf,
                                       sizeof(buf), NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "NaN");

    ROBO_CHECK_EQ_INT(
        robo_i18n_number_format_double(&fmt, bits_to_double(0x7ff0000000000000ULL), buf,
                                       sizeof(buf), NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "\xE2\x88\x9E");

    ROBO_CHECK_EQ_INT(
        robo_i18n_number_format_double(&fmt, bits_to_double(0xfff0000000000000ULL), buf,
                                       sizeof(buf), NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "-\xE2\x88\x9E");

    opts.max_fraction_digits = 0;
    robo_i18n_number_formatter_destroy(&fmt);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_number_format_double(&fmt, bits_to_double(0x8000000000000000ULL), buf,
                                       sizeof(buf), NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "-0");

    robo_i18n_number_formatter_destroy(&fmt);
}

static void test_number_u64_exact(void)
{
    robo_i18n_number_options_t opts;
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    char buf[64];
    robo_i18n_number_options_init(&opts);
    opts.use_grouping = 0;
    opts.max_fraction_digits = 0;
    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_number_format_u64(&fmt, 18446744073709551615ULL, buf, sizeof(buf), NULL),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "18446744073709551615");
    robo_i18n_number_formatter_destroy(&fmt);
}

static void test_number_intl_smoke(void)
{
    static const char *tags[] = {
        "en-US", "en-GB", "de-DE", "fr-FR", "es-ES", "ur-PK", "ar-SA", "ar-EG",
        "fa-IR", "fa-AF", "hi-IN", "bn-BD", "th-TH", "zh-CN", "zh-TW", "ja-JP", "ko-KR",
    };
    size_t i;
    robo_i18n_number_options_t opts;
    robo_i18n_number_options_init(&opts);
    opts.max_fraction_digits = 2;
    opts.min_fraction_digits = 2;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_number_formatter_t fmt;
        char buf[128];
        size_t n = 0;
        parse_tag(tags[i], &loc);
        ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, -1234, buf, sizeof(buf), &n),
                          ROBO_OK);
        ROBO_CHECK(n > 0);
        ROBO_CHECK(buf[0] != '\0');
        robo_i18n_number_formatter_destroy(&fmt);
    }
}

static void test_number_buffer_and_string(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    robo_string_t str;
    size_t need = 0;
    char tiny[2];

    robo_i18n_number_options_init(&opts);
    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, 1234567, NULL, 0, &need), ROBO_OK);
    ROBO_CHECK(need == strlen("1,234,567"));
    ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(&fmt, 1234567, tiny, sizeof(tiny), &need),
                      ROBO_ERROR);
    ROBO_CHECK(need == strlen("1,234,567"));

    ROBO_CHECK_EQ_INT(robo_string_init(&str), ROBO_OK);
    {
        robo_i18n_decimal_t dec;
        ROBO_CHECK_EQ_INT(robo_i18n_decimal_from_i64(42, &dec), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_number_format_to_string(&fmt, &dec, &str), ROBO_OK);
        ROBO_CHECK_STR_EQ(robo_string_data(&str), "42");
    }
    robo_string_destroy(&str);
    robo_i18n_number_formatter_destroy(&fmt);
}

static void test_number_invalid(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    char buf[16];

    robo_i18n_number_options_init(&opts);
    memcpy(opts.numbering_system, "nope", 5);
    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_i18n_number_format_i64(NULL, 1, buf, sizeof(buf), NULL),
                      ROBO_INVALID_ARGUMENT);
}

static void test_number_microbench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 50000u;
    double ms;
    robo_i18n_locale_t loc;
    robo_i18n_number_formatter_t fmt;
    robo_i18n_number_options_t opts;
    char buf[64];

    robo_i18n_number_options_init(&opts);
    parse_tag("de-DE", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_number_formatter_init(&fmt, &loc, &opts), ROBO_OK);
    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (robo_i18n_number_format_i64(&fmt, (int64_t)i, buf, sizeof(buf), NULL) ==
                 ROBO_OK);
    }
    t1 = clock();
    ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("i18n.number.microbench N=%zu time_ms=%.3f sink=%d\n", N, ms, sink);
    ROBO_CHECK(sink != 0);
    robo_i18n_number_formatter_destroy(&fmt);
}

void robo_test_register_i18n_number(void)
{
    robo_test_register("i18n.number.versions", test_number_versions);
    robo_test_register("i18n.number.en_us_integers", test_number_en_us_integers);
    robo_test_register("i18n.number.locale_symbols", test_number_locale_symbols);
    robo_test_register("i18n.number.digits_localized", test_number_digits_localized);
    robo_test_register("i18n.number.rounding", test_number_rounding);
    robo_test_register("i18n.number.fraction_options", test_number_fraction_options);
    robo_test_register("i18n.number.double_specials", test_number_double_specials);
    robo_test_register("i18n.number.u64_exact", test_number_u64_exact);
    robo_test_register("i18n.number.intl_smoke", test_number_intl_smoke);
    robo_test_register("i18n.number.buffer_string", test_number_buffer_and_string);
    robo_test_register("i18n.number.invalid", test_number_invalid);
    robo_test_register("i18n.number.microbench", test_number_microbench);
}
