/*
 * RoboI18N date/time parsing suite (Goal 4.23).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestDtparseVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nDtparseErrString(RoboI18nDtparseErrNone), "ok");
    RoboCheckStrEq(I18nDtparseErrString(RoboI18nDtparseErrAmbiguousLocal),
                      "ambiguous_local");
}

static void RoundtripDate(const char *tag, I18nCalFormatStyle style, int y, int m, int d)
{
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nDatetimeParser parser;
    I18nDtfmtOptions fopts;
    I18nDtparseOptions popts;
    I18nDate date;
    I18nDtparseResult res;
    char buf[192];
    size_t len = 0;

    ParseTag(tag, &loc);
    I18nDtfmtOptionsInit(&fopts);
    fopts.dateStyle = style;
    RoboCheckEqInt(I18nDatetimeFormatterInit(&fmt, &loc, NULL, NULL, RoboI18nDtfmtDate,
                                                        &fopts),
                      RoboOk);
    RoboCheckEqInt(I18nDateInit(&date, y, (uint8_t)m, (uint8_t)d), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &len), RoboOk);
    I18nDatetimeFormatterDestroy(&fmt);

    I18nDtparseOptionsInit(&popts);
    popts.dateStyle = style;
    RoboCheckEqInt(I18nDatetimeParserInit(&parser, &loc, NULL, NULL, RoboI18nDtfmtDate,
                                                     &popts),
                      RoboOk);
    RoboCheckEqInt(I18nParseDate(&parser, buf, len, &res), RoboOk);
    RoboCheckEqInt((int)res.haveDate, 1);
    RoboCheckEqInt(res.gregorianDate.year, y);
    RoboCheckEqInt((int)res.gregorianDate.month, m);
    RoboCheckEqInt((int)res.gregorianDate.day, d);
    I18nDatetimeParserDestroy(&parser);
}

static void RoundtripTime(const char *tag,
                           I18nCalFormatStyle style,
                           int h,
                           int mi,
                           int s)
{
    I18nLocale loc;
    I18nDatetimeFormatter fmt;
    I18nDatetimeParser parser;
    I18nDtfmtOptions fopts;
    I18nDtparseOptions popts;
    I18nTime time;
    I18nDtparseResult res;
    char buf[192];
    size_t len = 0;
    Result pr;

    ParseTag(tag, &loc);
    I18nDtfmtOptionsInit(&fopts);
    fopts.timeStyle = style;
    RoboCheckEqInt(I18nDatetimeFormatterInit(&fmt, &loc, NULL, NULL, RoboI18nDtfmtTime,
                                                        &fopts),
                      RoboOk);
    RoboCheckEqInt(I18nTimeInit(&time, (uint8_t)h, (uint8_t)mi, (uint8_t)s, 0), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatTime(&fmt, &time, buf, sizeof(buf), &len), RoboOk);
    I18nDatetimeFormatterDestroy(&fmt);

    I18nDtparseOptionsInit(&popts);
    popts.timeStyle = style;
    /* Style patterns may use day-period fields; permissive avoids brittle
     * literal whitespace differences across CLDR spacing. */
    popts.mode = RoboI18nParsePermissive;
    RoboCheckEqInt(I18nDatetimeParserInit(&parser, &loc, NULL, NULL, RoboI18nDtfmtTime,
                                                     &popts),
                      RoboOk);
    pr = I18nParseTime(&parser, buf, len, &res);
    if (pr != RoboOk || !res.haveTime || res.time.hour != (uint8_t)h ||
        res.time.minute != (uint8_t)mi || res.time.second != (uint8_t)s) {
        fprintf(stderr, "dtparse time roundtrip fail tag=%s fmt=[%s] err=%s\n", tag, buf,
                I18nDtparseErrString(res.error));
    }
    RoboCheckEqInt(pr, RoboOk);
    RoboCheckEqInt((int)res.haveTime, 1);
    RoboCheckEqInt((int)res.time.hour, h);
    RoboCheckEqInt((int)res.time.minute, mi);
    RoboCheckEqInt((int)res.time.second, s);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseRoundtripEn(void)
{
    RoundtripDate("en-US", RoboI18nCalStyleShort, 2026, 9, 13);
    RoundtripDate("en-US", RoboI18nCalStyleMedium, 2026, 9, 13);
    RoundtripDate("en-US", RoboI18nCalStyleLong, 2026, 9, 13);
    RoundtripDate("en-US", RoboI18nCalStyleFull, 2026, 9, 13);
    RoundtripDate("en-GB", RoboI18nCalStyleShort, 2026, 9, 13);
    RoundtripTime("en-US", RoboI18nCalStyleShort, 15, 45, 0);
    RoundtripTime("en-US", RoboI18nCalStyleMedium, 15, 45, 30);
    RoundtripTime("en-GB", RoboI18nCalStyleShort, 15, 45, 0);
}

static void TestDtparseIntlMatrix(void)
{
    static const char *tags[] = {"en-US", "en-GB", "de-DE", "fr-FR", "es-ES", "ru-RU",
                                 "pl-PL", "ar-SA", "fa-IR", "ur-PK", "hi-IN", "bn-BD",
                                 "th-TH", "zh-CN", "zh-TW", "ja-JP", "ko-KR", "he-IL"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        RoundtripDate(tags[i], RoboI18nCalStyleMedium, 2026, 9, 13);
        RoundtripDate(tags[i], RoboI18nCalStyleShort, 2024, 1, 5);
        RoundtripTime(tags[i], RoboI18nCalStyleMedium, 9, 5, 7);
    }
}

static void TestDtparsePatternOffset(void)
{
    I18nLocale loc;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    const char *text = "2024-11-03 01:30:00+00:00";

    ParseTag("en-US", &loc);
    I18nDtparseOptionsInit(&opts);
    opts.resolveInstant = 1;
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ssxxx", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseZonedDatetime(&parser, text, strlen(text), &res), RoboOk);
    RoboCheckEqInt((int)res.haveDate, 1);
    RoboCheckEqInt((int)res.haveTime, 1);
    RoboCheckEqInt((int)res.haveOffset, 1);
    RoboCheckEqInt((int)res.zoneExplicit, 1);
    RoboCheckEqInt((int)res.haveInstant, 1);
    RoboCheckEqInt(res.offsetSeconds, 0);
    RoboCheckEqInt(res.gregorianDate.year, 2024);
    RoboCheckEqInt((int)res.gregorianDate.month, 11);
    RoboCheckEqInt((int)res.gregorianDate.day, 3);
    RoboCheckEqInt((int)res.time.hour, 1);
    RoboCheckEqInt((int)res.time.minute, 30);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseZUtc(void)
{
    I18nLocale loc;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    const char *text = "2020-01-01T00:00:00Z";

    ParseTag("en", &loc);
    I18nDtparseOptionsInit(&opts);
    opts.resolveInstant = 1;
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd'T'HH:mm:ssX", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseZonedDatetime(&parser, text, strlen(text), &res), RoboOk);
    RoboCheckEqInt((int)res.haveInstant, 1);
    RoboCheckEqInt(res.offsetSeconds, 0);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseNoInventUtc(void)
{
    I18nLocale loc;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    const char *text = "2026-09-13 12:00:00";

    ParseTag("en", &loc);
    I18nDtparseOptionsInit(&opts);
    opts.resolveInstant = 1; /* but no resolveTz and no zone in input */
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseLocalDatetime(&parser, text, strlen(text), &res),
                      RoboParseError);
    RoboCheckEqInt((int)res.error, (int)RoboI18nDtparseErrMissingTimezone);
    RoboCheckEqInt((int)res.haveInstant, 0);
    /* Without resolveInstant, local parse succeeds */
    opts.resolveInstant = 0;
    I18nDatetimeParserDestroy(&parser);
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseLocalDatetime(&parser, text, strlen(text), &res), RoboOk);
    RoboCheckEqInt((int)res.haveDate, 1);
    RoboCheckEqInt((int)res.haveTime, 1);
    RoboCheckEqInt((int)res.haveInstant, 0);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseDstAmbiguous(void)
{
    I18nLocale loc;
    I18nTimezone tz;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    const char *text = "2024-11-03 01:30:00";

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);

    I18nDtparseOptionsInit(&opts);
    opts.resolveInstant = 1;
    opts.disambiguation = RoboI18nDtparseDisambigReject;
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, &tz,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseLocalDatetime(&parser, text, strlen(text), &res),
                      RoboParseError);
    RoboCheckEqInt((int)res.error, (int)RoboI18nDtparseErrAmbiguousLocal);
    I18nDatetimeParserDestroy(&parser);

    opts.disambiguation = RoboI18nDtparseDisambigEarlier;
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, &tz,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseLocalDatetime(&parser, text, strlen(text), &res), RoboOk);
    RoboCheckEqInt((int)res.haveInstant, 1);
    RoboCheckEqInt((int)res.tzResolve, (int)RoboI18nTzAmbiguous);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseDstGap(void)
{
    I18nLocale loc;
    I18nTimezone tz;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    const char *text = "2024-03-10 02:30:00";

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
    I18nDtparseOptionsInit(&opts);
    opts.resolveInstant = 1;
    opts.disambiguation = RoboI18nDtparseDisambigCompatible;
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, &tz,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseLocalDatetime(&parser, text, strlen(text), &res),
                      RoboParseError);
    RoboCheckEqInt((int)res.error, (int)RoboI18nDtparseErrNonexistentLocal);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseBuddhist(void)
{
    I18nLocale loc;
    I18nCalendar cal;
    I18nDatetimeFormatter fmt;
    I18nDatetimeParser parser;
    I18nDtfmtOptions fopts;
    I18nDtparseOptions popts;
    I18nDate g;
    I18nDtparseResult res;
    char buf[192];
    size_t len = 0;

    ParseTag("th", &loc);
    RoboCheckEqInt(I18nCalendarFromKind(RoboI18nCalBuddhist, &cal), RoboOk);
    I18nDtfmtOptionsInit(&fopts);
    fopts.dateStyle = RoboI18nCalStyleShort;
    RoboCheckEqInt(I18nDatetimeFormatterInit(&fmt, &loc, &cal, NULL, RoboI18nDtfmtDate,
                                                        &fopts),
                      RoboOk);
    RoboCheckEqInt(I18nDateInit(&g, 2026, 9, 13), RoboOk);
    RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &g, buf, sizeof(buf), &len), RoboOk);
    I18nDatetimeFormatterDestroy(&fmt);

    I18nDtparseOptionsInit(&popts);
    popts.dateStyle = RoboI18nCalStyleShort;
    RoboCheckEqInt(I18nDatetimeParserInit(&parser, &loc, &cal, NULL, RoboI18nDtfmtDate,
                                                     &popts),
                      RoboOk);
    RoboCheckEqInt(I18nParseDate(&parser, buf, len, &res), RoboOk);
    RoboCheckEqInt(res.gregorianDate.year, 2026);
    RoboCheckEqInt((int)res.gregorianDate.month, 9);
    RoboCheckEqInt((int)res.gregorianDate.day, 13);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseLocalizedDigits(void)
{
    I18nLocale loc;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    /* arab digits ٢٠٢٦-٠٩-١٣ */
    const char *arab = "\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA6-\xD9\xA0\xD9\xA9-\xD9\xA1\xD9\xA3";

    ParseTag("ar", &loc);
    I18nDtparseOptionsInit(&opts);
    memcpy(opts.numberingSystem, "arab", 5);
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL, "yyyy-MM-dd",
                                                             &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseDate(&parser, arab, strlen(arab), &res), RoboOk);
    RoboCheckEqInt(res.gregorianDate.year, 2026);
    RoboCheckEqInt((int)res.gregorianDate.month, 9);
    RoboCheckEqInt((int)res.gregorianDate.day, 13);
    I18nDatetimeParserDestroy(&parser);

    /* thai ๒๐๒๖ */
    {
        const char *thai = "\xE0\xB9\x92\xE0\xB9\x90\xE0\xB9\x92\xE0\xB9\x96";
        ParseTag("th", &loc);
        I18nDtparseOptionsInit(&opts);
        memcpy(opts.numberingSystem, "thai", 5);
        RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL, "yyyy",
                                                                 &opts),
                          RoboOk);
        RoboCheckEqInt(I18nParseDate(&parser, thai, strlen(thai), &res), RoboOk);
        RoboCheckEqInt(res.fields.year, 2026);
        I18nDatetimeParserDestroy(&parser);
    }
}

static void TestDtparseErrors(void)
{
    I18nLocale loc;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;

    ParseTag("en-US", &loc);
    I18nDtparseOptionsInit(&opts);
    opts.dateStyle = RoboI18nCalStyleShort;
    RoboCheckEqInt(I18nDatetimeParserInit(&parser, &loc, NULL, NULL, RoboI18nDtfmtDate,
                                                     &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseDate(&parser, "", 0, &res), RoboParseError);
    RoboCheckEqInt((int)res.error, (int)RoboI18nDtparseErrEmpty);

    {
        const char bad[] = {(char)0x80, (char)0x00};
        RoboCheckEqInt(I18nParseDate(&parser, bad, 1, &res), RoboParseError);
        RoboCheckEqInt((int)res.error, (int)RoboI18nDtparseErrInvalidUtf8);
    }
    RoboCheckEqInt(I18nParseDate(&parser, "13/09/2026", 10, &res), RoboParseError);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseFuzzRobust(void)
{
    I18nLocale loc;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    char longbuf[5000];
    size_t i;

    ParseTag("en", &loc);
    I18nDtparseOptionsInit(&opts);
    opts.mode = RoboI18nParsePermissive;
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss", &opts),
                      RoboOk);

    for (i = 0; i < sizeof(longbuf); i++) {
        longbuf[i] = (char)(i & 0x7f);
    }
    RoboCheckEqInt(I18nParseLocalDatetime(&parser, longbuf, sizeof(longbuf), &res),
                      RoboInvalidArgument);

    (void)I18nParseLocalDatetime(&parser, "\xFF\xFE\xFD", 3, &res);
    (void)I18nParseLocalDatetime(&parser, "9999-99-99 99:99:99", 19, &res);
    (void)I18nParseLocalDatetime(&parser, "----::", 6, &res);
    (void)I18nParseLocalDatetime(&parser, "GMT+99:99", 9, &res);
    I18nDatetimeParserDestroy(&parser);
}

static void TestDtparseIanaId(void)
{
    I18nLocale loc;
    I18nDatetimeParser parser;
    I18nDtparseOptions opts;
    I18nDtparseResult res;
    const char *text = "2023-06-01 12:00:00 America/New_York";

    ParseTag("en", &loc);
    I18nDtparseOptionsInit(&opts);
    opts.resolveInstant = 1;
    opts.disambiguation = RoboI18nDtparseDisambigCompatible;
    RoboCheckEqInt(I18nDatetimeParserInitPattern(&parser, &loc, NULL, NULL,
                                                             "yyyy-MM-dd HH:mm:ss V", &opts),
                      RoboOk);
    RoboCheckEqInt(I18nParseZonedDatetime(&parser, text, strlen(text), &res), RoboOk);
    RoboCheckEqInt((int)res.haveTimezone, 1);
    RoboCheckEqInt((int)res.haveInstant, 1);
    RoboCheck(strstr(res.timezoneId, "New_York") != NULL ||
               strcmp(res.timezoneId, "America/New_York") == 0);
    I18nDatetimeParserDestroy(&parser);
}

void TestRegisterI18nDatetimeParse(void)
{
    TestRegister("i18n.dtparse.versions", TestDtparseVersions);
    TestRegister("i18n.dtparse.roundtrip_en", TestDtparseRoundtripEn);
    TestRegister("i18n.dtparse.intl_matrix", TestDtparseIntlMatrix);
    TestRegister("i18n.dtparse.pattern_offset", TestDtparsePatternOffset);
    TestRegister("i18n.dtparse.z_utc", TestDtparseZUtc);
    TestRegister("i18n.dtparse.no_invent_utc", TestDtparseNoInventUtc);
    TestRegister("i18n.dtparse.dst_ambiguous", TestDtparseDstAmbiguous);
    TestRegister("i18n.dtparse.dst_gap", TestDtparseDstGap);
    TestRegister("i18n.dtparse.buddhist", TestDtparseBuddhist);
    TestRegister("i18n.dtparse.digits", TestDtparseLocalizedDigits);
    TestRegister("i18n.dtparse.errors", TestDtparseErrors);
    TestRegister("i18n.dtparse.fuzz", TestDtparseFuzzRobust);
    TestRegister("i18n.dtparse.iana", TestDtparseIanaId);
}
