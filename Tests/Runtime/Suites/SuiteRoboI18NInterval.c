/*
 * RoboI18N interval + relative-time suites (Goal 4.7).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboCalData.h"
#include "RoboI18N/RoboRelData.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestIntervalVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(RoboCalCldrVersion, "46");
    RoboCheck(CalIntervalFormatsCount > 1000u);
    RoboCheck(CalIntervalFallbacksCount > 10u);
}

static void TestIntervalModel(void)
{
    I18nDatetime a, b;
    I18nDatetimeInterval iv;

    RoboCheckEqInt(I18nDateInit(&a.date, 2026, 1, 12), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&a.time, 0, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nDateInit(&b.date, 2026, 1, 10), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&b.time, 0, 0, 0, 0), RoboOk);

    RoboCheckEqInt(I18nDatetimeIntervalInit(&iv, &a, &b, 0), RoboInvalidArgument);
    RoboCheckEqInt(I18nDatetimeIntervalInit(&iv, &a, &b, 1), RoboOk);
    RoboCheckEqInt(iv.start.date.day, 10);
    RoboCheckEqInt(iv.end.date.day, 12);

    RoboCheckEqInt(I18nDatetimeIntervalInit(&iv, &b, &b, 0), RoboOk);
}

static void TestIntervalEnCollapse(void)
{
    I18nLocale loc;
    I18nIntervalFormatter fmt;
    I18nIntervalOptions opts;
    I18nDate s, e;
    char buf[128];
    size_t len = 0;
    char diff[2];
    I18nDatetime ds, de;

    ParseTag("en", &loc);
    I18nIntervalOptionsInit(&opts);
    memcpy(opts.skeleton, "yMMMd", 6);
    RoboCheckEqInt(I18nIntervalFormatterInit(&fmt, &loc, NULL, NULL, &opts), RoboOk);

    RoboCheckEqInt(I18nDateInit(&s, 2026, 1, 10), RoboOk);
    RoboCheckEqInt(I18nDateInit(&e, 2026, 1, 12), RoboOk);
    memset(&ds, 0, sizeof(ds));
    memset(&de, 0, sizeof(de));
    ds.date = s;
    de.date = e;
    RoboCheckEqInt(I18nIntervalGreatestDifference(&ds, &de, diff, sizeof(diff)),
                      RoboOk);
    RoboCheckEqInt((int)diff[0], (int)'d');

    RoboCheckEqInt(I18nDateIntervalFormat(&fmt, &s, &e, buf, sizeof(buf), &len),
                      RoboOk);
    /* U+2009 thin space around en dash */
    RoboCheckStrEq(buf, "Jan 10\xE2\x80\x89\xE2\x80\x93\xE2\x80\x89""12, 2026");

    RoboCheckEqInt(I18nDateInit(&e, 2026, 2, 12), RoboOk);
    RoboCheckEqInt(I18nDateIntervalFormat(&fmt, &s, &e, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "Jan 10\xE2\x80\x89\xE2\x80\x93\xE2\x80\x89""Feb 12, 2026");

    RoboCheckEqInt(I18nDateInit(&e, 2027, 1, 12), RoboOk);
    RoboCheckEqInt(I18nDateIntervalFormat(&fmt, &s, &e, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheck(strstr(buf, "2026") != NULL);
    RoboCheck(strstr(buf, "2027") != NULL);

    /* Same date → single format */
    RoboCheckEqInt(I18nDateIntervalFormat(&fmt, &s, &s, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheckStrEq(buf, "Jan 10, 2026");

    I18nIntervalFormatterDestroy(&fmt);
}

static void TestIntervalTimeAndLocales(void)
{
    I18nLocale loc;
    I18nIntervalFormatter fmt;
    I18nIntervalOptions opts;
    I18nTime s, e;
    char buf[128];
    size_t len = 0;

    ParseTag("en", &loc);
    I18nIntervalOptionsInit(&opts);
    memcpy(opts.skeleton, "Hm", 3);
    RoboCheckEqInt(I18nIntervalFormatterInit(&fmt, &loc, NULL, NULL, &opts), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&s, 9, 0, 0, 0), RoboOk);
    RoboCheckEqInt(I18nTimeInit(&e, 11, 30, 0, 0), RoboOk);
    RoboCheckEqInt(I18nTimeIntervalFormat(&fmt, &s, &e, buf, sizeof(buf), &len),
                      RoboOk);
    RoboCheck(strstr(buf, "09:00") != NULL || strstr(buf, "9:00") != NULL);
    RoboCheck(strstr(buf, "11:30") != NULL);
    I18nIntervalFormatterDestroy(&fmt);

    ParseTag("ja", &loc);
    I18nIntervalOptionsInit(&opts);
    memcpy(opts.skeleton, "yMMMd", 6);
    RoboCheckEqInt(I18nIntervalFormatterInit(&fmt, &loc, NULL, NULL, &opts), RoboOk);
    {
        I18nDate d0, d1;
        RoboCheckEqInt(I18nDateInit(&d0, 2026, 1, 10), RoboOk);
        RoboCheckEqInt(I18nDateInit(&d1, 2026, 1, 12), RoboOk);
        RoboCheckEqInt(I18nDateIntervalFormat(&fmt, &d0, &d1, buf, sizeof(buf), &len),
                          RoboOk);
        RoboCheck(buf[0] != '\0');
    }
    I18nIntervalFormatterDestroy(&fmt);

    /* Unsupported calendar */
    {
        I18nCalendar cal;
        ParseTag("en", &loc);
        RoboCheckEqInt(I18nCalendarFromId("islamic", 7, &cal), RoboOk);
        RoboCheckEqInt(I18nIntervalFormatterInit(&fmt, &loc, &cal, NULL, NULL),
                          RoboNotSupported);
    }
}

static void TestIntervalBuddhist(void)
{
    I18nLocale loc;
    I18nCalendar cal;
    I18nIntervalFormatter fmt;
    I18nIntervalOptions opts;
    I18nDate s, e;
    char buf[128];
    size_t len = 0;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nCalendarFromId("buddhist", 8, &cal), RoboOk);
    I18nIntervalOptionsInit(&opts);
    memcpy(opts.skeleton, "y", 2);
    RoboCheckEqInt(I18nIntervalFormatterInit(&fmt, &loc, &cal, NULL, &opts), RoboOk);
    RoboCheckEqInt(I18nDateInit(&s, 2026, 1, 1), RoboOk);
    RoboCheckEqInt(I18nDateInit(&e, 2027, 1, 1), RoboOk);
    RoboCheckEqInt(I18nDateIntervalFormat(&fmt, &s, &e, buf, sizeof(buf), &len),
                      RoboOk);
    /* Buddhist years 2569–2570 */
    RoboCheck(strstr(buf, "2569") != NULL);
    RoboCheck(strstr(buf, "2570") != NULL);
    I18nIntervalFormatterDestroy(&fmt);
}

static void TestIntervalFuzz(void)
{
    I18nLocale loc;
    I18nIntervalFormatter fmt;
    I18nDate s, e;
    char buf[64];
    size_t len = 0;
    int y;

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nIntervalFormatterInit(&fmt, &loc, NULL, NULL, NULL), RoboOk);
    for (y = 2000; y <= 2030; y += 3) {
        RoboCheckEqInt(I18nDateInit(&s, y, 3, 1), RoboOk);
        RoboCheckEqInt(I18nDateInit(&e, y, 3, 15), RoboOk);
        RoboCheckEqInt(I18nDateIntervalFormat(&fmt, &s, &e, buf, sizeof(buf), &len),
                          RoboOk);
    }
    I18nIntervalFormatterDestroy(&fmt);
}

void TestRegisterI18nInterval(void)
{
    TestRegister("i18n.interval.versions", TestIntervalVersions);
    TestRegister("i18n.interval.model", TestIntervalModel);
    TestRegister("i18n.interval.en_collapse", TestIntervalEnCollapse);
    TestRegister("i18n.interval.time_locales", TestIntervalTimeAndLocales);
    TestRegister("i18n.interval.buddhist", TestIntervalBuddhist);
    TestRegister("i18n.interval.fuzz", TestIntervalFuzz);
}
