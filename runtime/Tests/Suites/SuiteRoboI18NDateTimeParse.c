/*
 * RoboI18N date/time parsing suite (Goal 4.23).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_dtparse_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.24.0");
    ROBO_CHECK_STR_EQ(robo_i18n_dtparse_err_string(ROBO_I18N_DTPARSE_ERR_NONE), "ok");
    ROBO_CHECK_STR_EQ(robo_i18n_dtparse_err_string(ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_LOCAL),
                      "ambiguous_local");
}

static void roundtrip_date(const char *tag, robo_i18n_cal_format_style_t style, int y, int m, int d)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtfmt_options_t fopts;
    robo_i18n_dtparse_options_t popts;
    robo_i18n_date_t date;
    robo_i18n_dtparse_result_t res;
    char buf[192];
    size_t len = 0;

    parse_tag(tag, &loc);
    robo_i18n_dtfmt_options_init(&fopts);
    fopts.date_style = style;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_formatter_init(&fmt, &loc, NULL, NULL, ROBO_I18N_DTFMT_DATE,
                                                        &fopts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&date, y, (uint8_t)m, (uint8_t)d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &date, buf, sizeof(buf), &len), ROBO_OK);
    robo_i18n_datetime_formatter_destroy(&fmt);

    robo_i18n_dtparse_options_init(&popts);
    popts.date_style = style;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init(&parser, &loc, NULL, NULL, ROBO_I18N_DTFMT_DATE,
                                                     &popts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, buf, len, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.have_date, 1);
    ROBO_CHECK_EQ_INT(res.gregorian_date.year, y);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.month, m);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.day, d);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void roundtrip_time(const char *tag,
                           robo_i18n_cal_format_style_t style,
                           int h,
                           int mi,
                           int s)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtfmt_options_t fopts;
    robo_i18n_dtparse_options_t popts;
    robo_i18n_time_t time;
    robo_i18n_dtparse_result_t res;
    char buf[192];
    size_t len = 0;
    robo_result_t pr;

    parse_tag(tag, &loc);
    robo_i18n_dtfmt_options_init(&fopts);
    fopts.time_style = style;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_formatter_init(&fmt, &loc, NULL, NULL, ROBO_I18N_DTFMT_TIME,
                                                        &fopts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_time_init(&time, (uint8_t)h, (uint8_t)mi, (uint8_t)s, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_time(&fmt, &time, buf, sizeof(buf), &len), ROBO_OK);
    robo_i18n_datetime_formatter_destroy(&fmt);

    robo_i18n_dtparse_options_init(&popts);
    popts.time_style = style;
    /* Style patterns may use day-period fields; permissive avoids brittle
     * literal whitespace differences across CLDR spacing. */
    popts.mode = ROBO_I18N_PARSE_PERMISSIVE;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init(&parser, &loc, NULL, NULL, ROBO_I18N_DTFMT_TIME,
                                                     &popts),
                      ROBO_OK);
    pr = robo_i18n_parse_time(&parser, buf, len, &res);
    if (pr != ROBO_OK || !res.have_time || res.time.hour != (uint8_t)h ||
        res.time.minute != (uint8_t)mi || res.time.second != (uint8_t)s) {
        fprintf(stderr, "dtparse time roundtrip fail tag=%s fmt=[%s] err=%s\n", tag, buf,
                robo_i18n_dtparse_err_string(res.error));
    }
    ROBO_CHECK_EQ_INT(pr, ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.have_time, 1);
    ROBO_CHECK_EQ_INT((int)res.time.hour, h);
    ROBO_CHECK_EQ_INT((int)res.time.minute, mi);
    ROBO_CHECK_EQ_INT((int)res.time.second, s);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_roundtrip_en(void)
{
    roundtrip_date("en-US", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13);
    roundtrip_date("en-US", ROBO_I18N_CAL_STYLE_MEDIUM, 2026, 9, 13);
    roundtrip_date("en-US", ROBO_I18N_CAL_STYLE_LONG, 2026, 9, 13);
    roundtrip_date("en-US", ROBO_I18N_CAL_STYLE_FULL, 2026, 9, 13);
    roundtrip_date("en-GB", ROBO_I18N_CAL_STYLE_SHORT, 2026, 9, 13);
    roundtrip_time("en-US", ROBO_I18N_CAL_STYLE_SHORT, 15, 45, 0);
    roundtrip_time("en-US", ROBO_I18N_CAL_STYLE_MEDIUM, 15, 45, 30);
    roundtrip_time("en-GB", ROBO_I18N_CAL_STYLE_SHORT, 15, 45, 0);
}

static void test_dtparse_intl_matrix(void)
{
    static const char *tags[] = {"en-US", "en-GB", "de-DE", "fr-FR", "es-ES", "ru-RU",
                                 "pl-PL", "ar-SA", "fa-IR", "ur-PK", "hi-IN", "bn-BD",
                                 "th-TH", "zh-CN", "zh-TW", "ja-JP", "ko-KR", "he-IL"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        roundtrip_date(tags[i], ROBO_I18N_CAL_STYLE_MEDIUM, 2026, 9, 13);
        roundtrip_date(tags[i], ROBO_I18N_CAL_STYLE_SHORT, 2024, 1, 5);
        roundtrip_time(tags[i], ROBO_I18N_CAL_STYLE_MEDIUM, 9, 5, 7);
    }
}

static void test_dtparse_pattern_offset(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    const char *text = "2024-11-03 01:30:00+00:00";

    parse_tag("en-US", &loc);
    robo_i18n_dtparse_options_init(&opts);
    opts.resolve_instant = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ssxxx", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_zoned_datetime(&parser, text, strlen(text), &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.have_date, 1);
    ROBO_CHECK_EQ_INT((int)res.have_time, 1);
    ROBO_CHECK_EQ_INT((int)res.have_offset, 1);
    ROBO_CHECK_EQ_INT((int)res.zone_explicit, 1);
    ROBO_CHECK_EQ_INT((int)res.have_instant, 1);
    ROBO_CHECK_EQ_INT(res.offset_seconds, 0);
    ROBO_CHECK_EQ_INT(res.gregorian_date.year, 2024);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.month, 11);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.day, 3);
    ROBO_CHECK_EQ_INT((int)res.time.hour, 1);
    ROBO_CHECK_EQ_INT((int)res.time.minute, 30);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_z_utc(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    const char *text = "2020-01-01T00:00:00Z";

    parse_tag("en", &loc);
    robo_i18n_dtparse_options_init(&opts);
    opts.resolve_instant = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd'T'HH:mm:ssX", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_zoned_datetime(&parser, text, strlen(text), &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.have_instant, 1);
    ROBO_CHECK_EQ_INT(res.offset_seconds, 0);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_no_invent_utc(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    const char *text = "2026-09-13 12:00:00";

    parse_tag("en", &loc);
    robo_i18n_dtparse_options_init(&opts);
    opts.resolve_instant = 1; /* but no resolve_tz and no zone in input */
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_local_datetime(&parser, text, strlen(text), &res),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_DTPARSE_ERR_MISSING_TIMEZONE);
    ROBO_CHECK_EQ_INT((int)res.have_instant, 0);
    /* Without resolve_instant, local parse succeeds */
    opts.resolve_instant = 0;
    robo_i18n_datetime_parser_destroy(&parser);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_local_datetime(&parser, text, strlen(text), &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.have_date, 1);
    ROBO_CHECK_EQ_INT((int)res.have_time, 1);
    ROBO_CHECK_EQ_INT((int)res.have_instant, 0);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_dst_ambiguous(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    const char *text = "2024-11-03 01:30:00";

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);

    robo_i18n_dtparse_options_init(&opts);
    opts.resolve_instant = 1;
    opts.disambiguation = ROBO_I18N_DTPARSE_DISAMBIG_REJECT;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, &tz,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_local_datetime(&parser, text, strlen(text), &res),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_DTPARSE_ERR_AMBIGUOUS_LOCAL);
    robo_i18n_datetime_parser_destroy(&parser);

    opts.disambiguation = ROBO_I18N_DTPARSE_DISAMBIG_EARLIER;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, &tz,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_local_datetime(&parser, text, strlen(text), &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.have_instant, 1);
    ROBO_CHECK_EQ_INT((int)res.tz_resolve, (int)ROBO_I18N_TZ_AMBIGUOUS);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_dst_gap(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_timezone_t tz;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    const char *text = "2024-03-10 02:30:00";

    parse_tag("en-US", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_timezone_from_id("America/New_York", 16, &tz), ROBO_OK);
    robo_i18n_dtparse_options_init(&opts);
    opts.resolve_instant = 1;
    opts.disambiguation = ROBO_I18N_DTPARSE_DISAMBIG_COMPATIBLE;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, &tz,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_local_datetime(&parser, text, strlen(text), &res),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_DTPARSE_ERR_NONEXISTENT_LOCAL);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_buddhist(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_calendar_t cal;
    robo_i18n_datetime_formatter_t fmt;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtfmt_options_t fopts;
    robo_i18n_dtparse_options_t popts;
    robo_i18n_date_t g;
    robo_i18n_dtparse_result_t res;
    char buf[192];
    size_t len = 0;

    parse_tag("th", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_calendar_from_kind(ROBO_I18N_CAL_BUDDHIST, &cal), ROBO_OK);
    robo_i18n_dtfmt_options_init(&fopts);
    fopts.date_style = ROBO_I18N_CAL_STYLE_SHORT;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_formatter_init(&fmt, &loc, &cal, NULL, ROBO_I18N_DTFMT_DATE,
                                                        &fopts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_date_init(&g, 2026, 9, 13), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_format_date(&fmt, &g, buf, sizeof(buf), &len), ROBO_OK);
    robo_i18n_datetime_formatter_destroy(&fmt);

    robo_i18n_dtparse_options_init(&popts);
    popts.date_style = ROBO_I18N_CAL_STYLE_SHORT;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init(&parser, &loc, &cal, NULL, ROBO_I18N_DTFMT_DATE,
                                                     &popts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, buf, len, &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.gregorian_date.year, 2026);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.month, 9);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.day, 13);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_localized_digits(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    /* arab digits ٢٠٢٦-٠٩-١٣ */
    const char *arab = "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA6-\xD9\xA0\xD9\xA9-\xD9\xA1\xD9\xA3";

    parse_tag("ar", &loc);
    robo_i18n_dtparse_options_init(&opts);
    memcpy(opts.numbering_system, "arab", 5);
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL, "yyyy-MM-dd",
                                                             &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, arab, strlen(arab), &res), ROBO_OK);
    ROBO_CHECK_EQ_INT(res.gregorian_date.year, 2026);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.month, 9);
    ROBO_CHECK_EQ_INT((int)res.gregorian_date.day, 13);
    robo_i18n_datetime_parser_destroy(&parser);

    /* thai ๒๐๒๖ */
    {
        const char *thai = "\xE0\xB9\x92\xE0\xB9\x90\xE0\xB9\x92\xE0\xB9\x96";
        parse_tag("th", &loc);
        robo_i18n_dtparse_options_init(&opts);
        memcpy(opts.numbering_system, "thai", 5);
        ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL, "yyyy",
                                                                 &opts),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, thai, strlen(thai), &res), ROBO_OK);
        ROBO_CHECK_EQ_INT(res.fields.year, 2026);
        robo_i18n_datetime_parser_destroy(&parser);
    }
}

static void test_dtparse_errors(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;

    parse_tag("en-US", &loc);
    robo_i18n_dtparse_options_init(&opts);
    opts.date_style = ROBO_I18N_CAL_STYLE_SHORT;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init(&parser, &loc, NULL, NULL, ROBO_I18N_DTFMT_DATE,
                                                     &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, "", 0, &res), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_DTPARSE_ERR_EMPTY);

    {
        const char bad[] = {(char)0x80, (char)0x00};
        ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, bad, 1, &res), ROBO_PARSE_ERROR);
        ROBO_CHECK_EQ_INT((int)res.error, (int)ROBO_I18N_DTPARSE_ERR_INVALID_UTF8);
    }
    ROBO_CHECK_EQ_INT(robo_i18n_parse_date(&parser, "13/09/2026", 10, &res), ROBO_PARSE_ERROR);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_fuzz_robust(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    char longbuf[5000];
    size_t i;

    parse_tag("en", &loc);
    robo_i18n_dtparse_options_init(&opts);
    opts.mode = ROBO_I18N_PARSE_PERMISSIVE;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      ROBO_OK);

    for (i = 0; i < sizeof(longbuf); i++) {
        longbuf[i] = (char)(i & 0x7f);
    }
    ROBO_CHECK_EQ_INT(robo_i18n_parse_local_datetime(&parser, longbuf, sizeof(longbuf), &res),
                      ROBO_INVALID_ARGUMENT);

    (void)robo_i18n_parse_local_datetime(&parser, "\xFF\xFE\xFD", 3, &res);
    (void)robo_i18n_parse_local_datetime(&parser, "9999-99-99 99:99:99", 19, &res);
    (void)robo_i18n_parse_local_datetime(&parser, "----::", 6, &res);
    (void)robo_i18n_parse_local_datetime(&parser, "GMT+99:99", 9, &res);
    robo_i18n_datetime_parser_destroy(&parser);
}

static void test_dtparse_iana_id(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_datetime_parser_t parser;
    robo_i18n_dtparse_options_t opts;
    robo_i18n_dtparse_result_t res;
    const char *text = "2023-06-01 12:00:00 America/New_York";

    parse_tag("en", &loc);
    robo_i18n_dtparse_options_init(&opts);
    opts.resolve_instant = 1;
    opts.disambiguation = ROBO_I18N_DTPARSE_DISAMBIG_COMPATIBLE;
    ROBO_CHECK_EQ_INT(robo_i18n_datetime_parser_init_pattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss V", &opts),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_parse_zoned_datetime(&parser, text, strlen(text), &res), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)res.have_timezone, 1);
    ROBO_CHECK_EQ_INT((int)res.have_instant, 1);
    ROBO_CHECK(strstr(res.timezone_id, "New_York") != NULL ||
               strcmp(res.timezone_id, "America/New_York") == 0);
    robo_i18n_datetime_parser_destroy(&parser);
}

void robo_test_register_i18n_datetime_parse(void)
{
    robo_test_register("i18n.dtparse.versions", test_dtparse_versions);
    robo_test_register("i18n.dtparse.roundtrip_en", test_dtparse_roundtrip_en);
    robo_test_register("i18n.dtparse.intl_matrix", test_dtparse_intl_matrix);
    robo_test_register("i18n.dtparse.pattern_offset", test_dtparse_pattern_offset);
    robo_test_register("i18n.dtparse.z_utc", test_dtparse_z_utc);
    robo_test_register("i18n.dtparse.no_invent_utc", test_dtparse_no_invent_utc);
    robo_test_register("i18n.dtparse.dst_ambiguous", test_dtparse_dst_ambiguous);
    robo_test_register("i18n.dtparse.dst_gap", test_dtparse_dst_gap);
    robo_test_register("i18n.dtparse.buddhist", test_dtparse_buddhist);
    robo_test_register("i18n.dtparse.digits", test_dtparse_localized_digits);
    robo_test_register("i18n.dtparse.errors", test_dtparse_errors);
    robo_test_register("i18n.dtparse.fuzz", test_dtparse_fuzz_robust);
    robo_test_register("i18n.dtparse.iana", test_dtparse_iana_id);
}
