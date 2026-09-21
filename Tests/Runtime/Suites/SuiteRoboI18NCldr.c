/*
 * RoboI18N CLDR locale-data suite (Goal 4.2).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectFmt(const I18nLocale *loc, const char *want)
{
    char buf[128];
    RoboCheckEqInt(I18nLocaleFormat(loc, buf, sizeof(buf), NULL), RoboOk);
    RoboCheckStrEq(buf, want);
}

static void ExpectMax(const char *in, const char *want)
{
    I18nLocale loc;
    I18nLocale out;
    I18nLocale original;
    ParseTag(in, &loc);
    original = loc;
    RoboCheckEqInt(I18nLocaleMaximize(&loc, &out), RoboOk);
    RoboCheck(I18nLocaleEqual(&loc, &original));
    ExpectFmt(&out, want);
}

static void ExpectMin(const char *in, const char *want)
{
    I18nLocale loc;
    I18nLocale out;
    ParseTag(in, &loc);
    RoboCheckEqInt(I18nLocaleMinimize(&loc, &out), RoboOk);
    ExpectFmt(&out, want);
}

static void ExpectNs(const char *tag, const char *wantDefault, const char *wantNative)
{
    I18nLocale loc;
    char def[32];
    char nat[32];
    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nLocaleDefaultNumberingSystem(&loc, def, sizeof(def)), RoboOk);
    RoboCheckEqInt(I18nLocaleNativeNumberingSystem(&loc, nat, sizeof(nat)), RoboOk);
    RoboCheckStrEq(def, wantDefault);
    RoboCheckStrEq(nat, wantNative);
}

static void ExpectCal(const char *tag, const char *wantFirst)
{
    I18nLocale loc;
    char id[32];
    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nLocaleDefaultCalendar(&loc, id, sizeof(id)), RoboOk);
    RoboCheckStrEq(id, wantFirst);
}

static void ExpectMeas(const char *tag, I18nMeasurement want)
{
    I18nLocale loc;
    I18nMeasurement got;
    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nLocaleMeasurementSystem(&loc, &got), RoboOk);
    RoboCheckEqInt((int)got, (int)want);
}

static void TestCldrVersions(void)
{
    RoboCheckStrEq(I18nCldrVersionString(), "46");
    RoboCheckStrEq(I18nCldrUnicodeVersionString(), "16.0.0");
}

static void TestCldrMaximize(void)
{
    ExpectMax("zh", "zh-Hans-CN");
    ExpectMax("zh-TW", "zh-Hant-TW");
    ExpectMax("zh-Hant", "zh-Hant-TW");
    ExpectMax("sr", "sr-Cyrl-RS");
    ExpectMax("sr-Latn", "sr-Latn-RS");
    ExpectMax("ur", "ur-Arab-PK");
    ExpectMax("ar", "ar-Arab-EG");
    ExpectMax("fa", "fa-Arab-IR");
    ExpectMax("hi", "hi-Deva-IN");
    ExpectMax("bn", "bn-Beng-BD");
    ExpectMax("th", "th-Thai-TH");
    ExpectMax("ja", "ja-Jpan-JP");
    ExpectMax("ko", "ko-Kore-KR");
    ExpectMax("en", "en-Latn-US");
    ExpectMax("he", "he-Hebr-IL");
}

static void TestCldrMinimize(void)
{
    ExpectMin("zh-Hans-CN", "zh");
    ExpectMin("zh-Hant-TW", "zh-Hant");
    ExpectMin("sr-Cyrl-RS", "sr");
    ExpectMin("sr-Latn-RS", "sr-Latn");
    ExpectMin("en-Latn-US", "en");
}

static void TestCldrNumberingLocales(void)
{
    ExpectNs("en-US", "latn", "latn");
    ExpectNs("en-GB", "latn", "latn");
    ExpectNs("ar-SA", "arab", "arab");
    ExpectNs("ar-EG", "arab", "arab");
    ExpectNs("fa-IR", "arabext", "arabext");
    ExpectNs("fa-AF", "arabext", "arabext");
    ExpectNs("hi-IN", "latn", "deva");
    ExpectNs("bn-BD", "beng", "beng");
    ExpectNs("th-TH", "latn", "thai");
    ExpectNs("ur-PK", "latn", "arabext");
    ExpectNs("zh-CN", "latn", "hanidec");
    ExpectNs("zh-TW", "latn", "hanidec");
    ExpectNs("ja-JP", "latn", "latn");
    ExpectNs("ko-KR", "latn", "latn");
    ExpectNs("de-DE", "latn", "latn");
    ExpectNs("fr-FR", "latn", "latn");
    ExpectNs("es-ES", "latn", "latn");
    ExpectNs("he-IL", "latn", "latn");
}

static void TestCldrNumberingDigits(void)
{
    static const struct {
        const char *id;
        const char *type;
        /* UTF-8 concatenation of digit 0..9 from CLDR 46 numberingSystems.json */
        const unsigned char digits[40];
        size_t digitsLen;
    } cases[] = {
        {"latn", "numeric", {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'}, 10},
        {"arab",
         "numeric",
         {0xd9, 0xa0, 0xd9, 0xa1, 0xd9, 0xa2, 0xd9, 0xa3, 0xd9, 0xa4, 0xd9, 0xa5, 0xd9, 0xa6,
          0xd9, 0xa7, 0xd9, 0xa8, 0xd9, 0xa9},
         20},
        {"arabext",
         "numeric",
         {0xdb, 0xb0, 0xdb, 0xb1, 0xdb, 0xb2, 0xdb, 0xb3, 0xdb, 0xb4, 0xdb, 0xb5, 0xdb, 0xb6,
          0xdb, 0xb7, 0xdb, 0xb8, 0xdb, 0xb9},
         20},
        {"deva",
         "numeric",
         {0xe0, 0xa5, 0xa6, 0xe0, 0xa5, 0xa7, 0xe0, 0xa5, 0xa8, 0xe0, 0xa5, 0xa9, 0xe0, 0xa5,
          0xaa, 0xe0, 0xa5, 0xab, 0xe0, 0xa5, 0xac, 0xe0, 0xa5, 0xad, 0xe0, 0xa5, 0xae, 0xe0,
          0xa5, 0xaf},
         30},
        {"beng",
         "numeric",
         {0xe0, 0xa7, 0xa6, 0xe0, 0xa7, 0xa7, 0xe0, 0xa7, 0xa8, 0xe0, 0xa7, 0xa9, 0xe0, 0xa7,
          0xaa, 0xe0, 0xa7, 0xab, 0xe0, 0xa7, 0xac, 0xe0, 0xa7, 0xad, 0xe0, 0xa7, 0xae, 0xe0,
          0xa7, 0xaf},
         30},
        {"thai",
         "numeric",
         {0xe0, 0xb9, 0x90, 0xe0, 0xb9, 0x91, 0xe0, 0xb9, 0x92, 0xe0, 0xb9, 0x93, 0xe0, 0xb9,
          0x94, 0xe0, 0xb9, 0x95, 0xe0, 0xb9, 0x96, 0xe0, 0xb9, 0x97, 0xe0, 0xb9, 0x98, 0xe0,
          0xb9, 0x99},
         30},
        {"fullwide",
         "numeric",
         {0xef, 0xbc, 0x90, 0xef, 0xbc, 0x91, 0xef, 0xbc, 0x92, 0xef, 0xbc, 0x93, 0xef, 0xbc,
          0x94, 0xef, 0xbc, 0x95, 0xef, 0xbc, 0x96, 0xef, 0xbc, 0x97, 0xef, 0xbc, 0x98, 0xef,
          0xbc, 0x99},
         30},
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        I18nNumberingSystem ns;
        RoboCheckEqInt(I18nNumberingSystemGet(cases[i].id, &ns), RoboOk);
        RoboCheckStrEq(ns.id, cases[i].id);
        RoboCheckStrEq(ns.type, cases[i].type);
        RoboCheckEqInt((int)strlen(ns.digits), (int)cases[i].digitsLen);
        RoboCheck(memcmp(ns.digits, cases[i].digits, cases[i].digitsLen) == 0);
    }
}

static void TestCldrCalendars(void)
{
    ExpectCal("en-US", "gregorian");
    ExpectCal("en-GB", "gregorian");
    ExpectCal("fa-AF", "persian");
    ExpectCal("fa-IR", "persian");
    ExpectCal("th-TH", "buddhist");
    ExpectCal("ar-SA", "gregorian");
    ExpectCal("he-IL", "gregorian");
    ExpectCal("ja-JP", "gregorian");
    ExpectCal("zh-CN", "gregorian");
    ExpectCal("zh-TW", "gregorian");
}

static void TestCldrMeasurement(void)
{
    ExpectMeas("en-US", RoboI18nMeasureUs);
    ExpectMeas("en-GB", RoboI18nMeasureUk);
    ExpectMeas("de-DE", RoboI18nMeasureMetric);
    ExpectMeas("ur-PK", RoboI18nMeasureMetric);
    ExpectMeas("ar-EG", RoboI18nMeasureMetric);
    ExpectMeas("ja-JP", RoboI18nMeasureMetric);
}

static void TestCldrDataParent(void)
{
    I18nLocale loc;
    I18nLocale parent;
    char buf[64];

    ParseTag("zh-Hant-TW", &loc);
    RoboCheckEqInt(I18nLocaleDataParent(&loc, &parent), RoboOk);
    ExpectFmt(&parent, "zh-Hant");

    ParseTag("zh-Hant", &loc);
    RoboCheckEqInt(I18nLocaleDataParent(&loc, &parent), RoboOk);
    /* CLDR parentLocales maps zh-Hant → und */
    ExpectFmt(&parent, "und");

    ParseTag("en-GB", &loc);
    RoboCheckEqInt(I18nLocaleDataParent(&loc, &parent), RoboOk);
    ExpectFmt(&parent, "en-001");

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nLocaleDataParent(&loc, &parent), RoboOk);
    ExpectFmt(&parent, "en");

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nLocaleDataParent(&loc, &parent), RoboOk);
    RoboCheckStrEq(parent.language, "root");

    ParseTag("root", &loc);
    /* syntactically "root" as language */
    RoboCheckEqInt(I18nLocaleDataParent(&loc, &parent), RoboNotFound);

    (void)buf;
}

static void TestCldrScriptLocaleDistinction(void)
{
    I18nLocale loc;
    I18nLocale maxed;
    /* ur-PK does not invent a script from the language name alone without CLDR. */
    ParseTag("ur-PK", &loc);
    RoboCheck(loc.script[0] == '\0');
    RoboCheckEqInt(I18nLocaleMaximize(&loc, &maxed), RoboOk);
    RoboCheckStrEq(maxed.script, "Arab");
    RoboCheckStrEq(maxed.region, "PK");
}

static void TestCldrIntlCoverage(void)
{
    static const char *tags[] = {
        "en-US", "en-GB", "ur-PK", "ar-SA", "ar-EG", "fa-IR", "fa-AF", "he-IL",
        "hi-IN", "bn-BD", "th-TH", "zh-CN", "zh-TW", "ja-JP", "ko-KR", "de-DE",
        "fr-FR", "es-ES",
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nLocale maxed;
        char ns[32];
        char cal[32];
        I18nMeasurement m;
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nLocaleMaximize(&loc, &maxed), RoboOk);
        RoboCheck(maxed.script[0] != '\0' || maxed.region[0] != '\0');
        RoboCheckEqInt(I18nLocaleDefaultNumberingSystem(&loc, ns, sizeof(ns)),
                          RoboOk);
        RoboCheck(ns[0] != '\0');
        RoboCheckEqInt(I18nLocaleDefaultCalendar(&loc, cal, sizeof(cal)), RoboOk);
        RoboCheck(cal[0] != '\0');
        RoboCheckEqInt(I18nLocaleMeasurementSystem(&loc, &m), RoboOk);
    }
}

static void TestCldrMalformed(void)
{
    I18nLocale loc;
    I18nLocale out;
    I18nNumberingSystem ns;
    char buf[8];
    ParseTag("en", &loc);
    RoboCheckEqInt(I18nLocaleMaximize(NULL, &out), RoboInvalidArgument);
    RoboCheckEqInt(I18nLocaleMinimize(&loc, NULL), RoboInvalidArgument);
    RoboCheckEqInt(I18nNumberingSystemGet("not-a-real-ns", &ns), RoboNotFound);
    RoboCheckEqInt(I18nLocaleDefaultNumberingSystem(NULL, buf, sizeof(buf)),
                      RoboInvalidArgument);
}

static void TestCldrMicrobench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 20000u;
    double ms;
    I18nLocale loc;
    I18nLocale out;
    char ns[32];
    char cal[32];
    I18nMeasurement m;

    ParseTag("zh-Hant-TW", &loc);
    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (I18nLocaleMaximize(&loc, &out) == RoboOk);
        sink += (I18nLocaleMinimize(&out, &out) == RoboOk);
        sink += (I18nLocaleDefaultNumberingSystem(&loc, ns, sizeof(ns)) == RoboOk);
        sink += (I18nLocaleDefaultCalendar(&loc, cal, sizeof(cal)) == RoboOk);
        sink += (I18nLocaleMeasurementSystem(&loc, &m) == RoboOk);
    }
    t1 = clock();
    ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("i18n.cldr.microbench N=%zu time_ms=%.3f sink=%d\n", N, ms, sink);
    RoboCheck(sink != 0);
}

void TestRegisterI18nCldr(void)
{
    TestRegister("i18n.cldr.versions", TestCldrVersions);
    TestRegister("i18n.cldr.maximize", TestCldrMaximize);
    TestRegister("i18n.cldr.minimize", TestCldrMinimize);
    TestRegister("i18n.cldr.numbering_locales", TestCldrNumberingLocales);
    TestRegister("i18n.cldr.numbering_digits", TestCldrNumberingDigits);
    TestRegister("i18n.cldr.calendars", TestCldrCalendars);
    TestRegister("i18n.cldr.measurement", TestCldrMeasurement);
    TestRegister("i18n.cldr.data_parent", TestCldrDataParent);
    TestRegister("i18n.cldr.script_locale", TestCldrScriptLocaleDistinction);
    TestRegister("i18n.cldr.intl_coverage", TestCldrIntlCoverage);
    TestRegister("i18n.cldr.malformed", TestCldrMalformed);
    TestRegister("i18n.cldr.microbench", TestCldrMicrobench);
}
