/*
 * RoboI18N runtime locale service (Goal 4.16).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NCollation.h"
#include "RoboI18N/RoboI18NDateTimeFormat.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboI18NMessage.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboHcData.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestLsVersion(void)
{
    I18nDataVersions v;
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    GetI18nDataVersions(&v);
    RoboCheckStrEq(v.apiVersion, "0.27.0");
    RoboCheckStrEq(v.generatorVersion, "1.25.0");
    RoboCheckStrEq(v.cldrVersion, "46");
    RoboCheckStrEq(v.unicodeVersion, "16.0.0");
    RoboCheckStrEq(v.tzdbVersion, "2026c");
    RoboCheckStrEq(v.ianaFileDate, "2026-05-05");
    RoboCheckStrEq(v.ucaVersion, "16.0.0");
    RoboCheckStrEq(v.iso4217Source, "CLDR-46-codeMappings");
    RoboCheck(HcRegionCount > 100u);
}

static void TestLsUnicodeKeywords(void)
{
    I18nLocale loc;
    I18nUnicodeKeywords kw;
    char buf[32];

    ParseTag("ur-PK-u-ca-islamic-nu-arabext-hc-h12-co-standard", &loc);
    RoboCheckEqInt(I18nLocaleUnicodeKeywords(&loc, &kw), RoboOk);
    RoboCheckStrEq(kw.calendar, "islamic");
    RoboCheckStrEq(kw.numberingSystem, "arabext");
    RoboCheckStrEq(kw.hourCycle, "h12");
    RoboCheckStrEq(kw.collation, "standard");

    RoboCheckEqInt(I18nLocaleGetKeyword(&loc, "nu", buf, sizeof(buf)), RoboOk);
    RoboCheckStrEq(buf, "arabext");
    RoboCheckEqInt(I18nLocaleGetKeyword(&loc, "xx", buf, sizeof(buf)),
                      RoboNotFound);

    ParseTag("en", &loc);
    RoboCheckEqInt(I18nLocaleSetKeyword(&loc, "nu", "arab"), RoboOk);
    RoboCheckEqInt(I18nLocaleUnicodeKeywords(&loc, &kw), RoboOk);
    RoboCheckStrEq(kw.numberingSystem, "arab");
}

static void TestLsResolvePrecedence(void)
{
    I18nLocale loc;
    I18nLocaleOverrides ov;
    I18nLocaleSettings st;
    char ns[16];
    char cal[32];
    char hc[8];

    ParseTag("ar-EG-u-nu-latn-ca-gregory-hc-h23", &loc);
    I18nLocaleOverridesInit(&ov);
    RoboCheckEqInt(I18nLocaleResolve(&loc, &ov, &st), RoboOk);
    RoboCheckStrEq(st.numberingSystem, "latn");
    RoboCheckEqInt(st.numberingSrc, RoboI18nSrcExtension);
    RoboCheckStrEq(st.calendar, "gregory");
    RoboCheckEqInt(st.calendarSrc, RoboI18nSrcExtension);
    RoboCheckStrEq(st.hourCycle, "h23");
    RoboCheckEqInt(st.hourCycleSrc, RoboI18nSrcExtension);

    ov.numberingSystem = "arab";
    ov.calendar = "islamic";
    ov.hourCycle = "h12";
    RoboCheckEqInt(I18nLocaleResolve(&loc, &ov, &st), RoboOk);
    RoboCheckStrEq(st.numberingSystem, "arab");
    RoboCheckEqInt(st.numberingSrc, RoboI18nSrcOverride);
    RoboCheckStrEq(st.calendar, "islamic");
    RoboCheckStrEq(st.hourCycle, "h12");

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nLocaleResolveNumbering(&loc, NULL, ns, sizeof(ns)), RoboOk);
    RoboCheckStrEq(ns, "latn");
    RoboCheckEqInt(I18nLocaleResolveCalendar(&loc, NULL, cal, sizeof(cal)), RoboOk);
    RoboCheckStrEq(cal, "gregorian");
    RoboCheckEqInt(I18nLocaleResolveHourCycle(&loc, NULL, hc, sizeof(hc)), RoboOk);
    RoboCheckStrEq(hc, "h12"); /* US prefers 12-hour */

    ParseTag("de-DE", &loc);
    RoboCheckEqInt(I18nLocaleResolveHourCycle(&loc, NULL, hc, sizeof(hc)), RoboOk);
    RoboCheckStrEq(hc, "h23");
}

static void TestLsTimezoneNotLocale(void)
{
    I18nLocale loc;
    I18nLocaleSettings st;
    char tz[80];

    ParseTag("en-US", &loc);
    RoboCheckEqInt(I18nLocaleResolve(&loc, NULL, &st), RoboOk);
    RoboCheckEqInt(st.timezoneSrc, RoboI18nSrcUnset);
    RoboCheck(st.timezoneId[0] == '\0');
    RoboCheckEqInt(I18nLocaleResolveTimezone(&loc, NULL, tz, sizeof(tz)), RoboOk);
    RoboCheckStrEq(tz, "UTC"); /* runtime fallback only */

    RoboCheckEqInt(
        I18nLocaleResolveTimezone(&loc, "Asia/Karachi", tz, sizeof(tz)), RoboOk);
    RoboCheckStrEq(tz, "Asia/Karachi");
}

static void TestLsFallbackChain(void)
{
    I18nLocale loc;
    I18nLocale chain[RoboI18nFallbackMax];
    size_t n = 0;
    char tag[64];
    size_t tlen = 0;

    ParseTag("zh-Hans-CN", &loc);
    RoboCheckEqInt(I18nLocaleFallbackChain(&loc, chain, RoboI18nFallbackMax, &n),
                      RoboOk);
    RoboCheck(n >= 2u);
    RoboCheckEqInt(I18nLocaleFormat(&chain[0], tag, sizeof(tag), &tlen), RoboOk);
    RoboCheck(strstr(tag, "zh") != NULL);
    /* Chain is finite */
    RoboCheck(n <= RoboI18nFallbackMax);
}

static void TestLsNegotiate(void)
{
    I18nLocaleList *req = NULL;
    I18nLocale available[4];
    I18nLocale best;
    I18nMatch match = RoboI18nMatchNone;

    ParseTag("en", &available[0]);
    ParseTag("ur", &available[1]);
    ParseTag("ar", &available[2]);
    ParseTag("de", &available[3]);

    RoboCheckEqInt(I18nLocaleListParse("ur-PK, en;q=0.8", 15, &req), RoboOk);
    RoboCheckEqInt(
        I18nLocaleNegotiate(req, available, 4, &best, &match), RoboOk);
    RoboCheckStrEq(best.language, "ur");
    RoboCheck(match >= RoboI18nMatchLanguage);
    I18nLocaleListDestroy(req);
}

static void TestLsApplicationLocale(void)
{
    I18nLocale ur;
    I18nLocale got;
    ParseTag("ur-PK", &ur);
    I18nClearApplicationLocale();
    RoboCheckEqInt(I18nGetApplicationLocale(&got), RoboNotFound);
    RoboCheckEqInt(I18nSetApplicationLocale(&ur), RoboOk);
    RoboCheckEqInt(I18nGetApplicationLocale(&got), RoboOk);
    RoboCheck(I18nLocaleEqual(&ur, &got));
    RoboCheckEqInt(I18nGetEffectiveLocale(&got), RoboOk);
    RoboCheck(I18nLocaleEqual(&ur, &got));
    I18nClearApplicationLocale();
}

static void TestLsCapabilities(void)
{
    I18nLocale en;
    ParseTag("en", &en);
    RoboCheck(I18nLocaleSupportsCalendar(&en, "gregorian"));
    RoboCheck(I18nLocaleSupportsCalendar(&en, "gregory"));
    RoboCheck(I18nLocaleSupportsNumbering(&en, "latn"));
    RoboCheck(I18nLocaleSupportsCollationType(&en, "standard"));
    RoboCheck(I18nTimezoneIdIsKnown("Asia/Karachi", 12));
    RoboCheck(!I18nTimezoneIdIsKnown("Not/AZone", 9));
}

static void TestLsMaximizeMatrix(void)
{
    static const char *tags[] = {
        "en", "ur", "ar", "fa", "he", "hi", "bn", "ta", "th", "zh", "ja", "ko", "ru",
        "fr", "de", "es", "tr", "en-Latn", "zh-Hans", "sr-Cyrl", "ar-Arab-SA",
        "hi-Deva-IN", "th-Thai-TH"
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nLocale maxed;
        ParseTag(tags[i], &loc);
        RoboCheckEqInt(I18nLocaleMaximize(&loc, &maxed), RoboOk);
        RoboCheck(maxed.language[0] != '\0');
    }
}

static void TestLsCrossSubsystemNumbering(void)
{
    I18nLocale loc;
    I18nNumberFormatter nfmt;
    I18nDatetimeFormatter dfmt;
    I18nLocaleSettings st;
    char ns[16];

    ParseTag("ar-EG-u-nu-arab", &loc);
    RoboCheckEqInt(I18nLocaleResolve(&loc, NULL, &st), RoboOk);
    RoboCheckStrEq(st.numberingSystem, "arab");

    RoboCheckEqInt(I18nNumberFormatterInit(&nfmt, &loc, NULL), RoboOk);
    RoboCheckStrEq(nfmt.numberingSystem, "arab");

    RoboCheckEqInt(
        I18nDatetimeFormatterInitPattern(&dfmt, &loc, NULL, NULL, "yMd", NULL),
        RoboOk);
    RoboCheckStrEq(dfmt.number.numberingSystem, "arab");

    RoboCheckEqInt(I18nLocaleResolveNumbering(&loc, NULL, ns, sizeof(ns)), RoboOk);
    RoboCheckStrEq(ns, nfmt.numberingSystem);
    RoboCheckStrEq(ns, dfmt.number.numberingSystem);

    I18nNumberFormatterDestroy(&nfmt);
    I18nDatetimeFormatterDestroy(&dfmt);
}

static void TestLsCollationExtension(void)
{
    I18nLocale loc;
    I18nCollator *c = NULL;
    char type[16];

    /* BCP47 u-type max length is 8; CLDR short form is phonebk. */
    ParseTag("de-u-co-phonebk", &loc);
    RoboCheckEqInt(I18nLocaleResolveCollation(&loc, NULL, type, sizeof(type)),
                      RoboOk);
    RoboCheckStrEq(type, "phonebk");
    RoboCheckEqInt(I18nCollatorCreate(&loc, NULL, NULL, &c), RoboOk);
    RoboCheck(c != NULL);
    I18nCollatorDestroy(c);
}

static void TestLsRobustness(void)
{
    I18nLocale loc;
    I18nLocaleSettings st;
    I18nUnicodeKeywords kw;
    char big[512];
    size_t i;

    RoboCheckEqInt(I18nLocaleResolve(NULL, NULL, &st), RoboInvalidArgument);
    RoboCheckEqInt(I18nLocaleUnicodeKeywords(NULL, &kw), RoboInvalidArgument);

    memset(big, 'a', sizeof(big) - 1u);
    big[sizeof(big) - 1u] = '\0';
    RoboCheck(I18nLocaleParseCanonical(big, strlen(big), &loc) != RoboOk);

    ParseTag("en", &loc);
    for (i = 0; i < 64; i++) {
        I18nLocale chain[RoboI18nFallbackMax];
        size_t n = 0;
        RoboCheckEqInt(
            I18nLocaleFallbackChain(&loc, chain, RoboI18nFallbackMax, &n),
            RoboOk);
        RoboCheck(n <= RoboI18nFallbackMax);
    }
}

void TestRegisterI18nLocaleService(void)
{
    TestRegister("i18n.ls.version", TestLsVersion);
    TestRegister("i18n.ls.unicode_keywords", TestLsUnicodeKeywords);
    TestRegister("i18n.ls.resolve_precedence", TestLsResolvePrecedence);
    TestRegister("i18n.ls.timezone_not_locale", TestLsTimezoneNotLocale);
    TestRegister("i18n.ls.fallback_chain", TestLsFallbackChain);
    TestRegister("i18n.ls.negotiate", TestLsNegotiate);
    TestRegister("i18n.ls.application_locale", TestLsApplicationLocale);
    TestRegister("i18n.ls.capabilities", TestLsCapabilities);
    TestRegister("i18n.ls.maximize_matrix", TestLsMaximizeMatrix);
    TestRegister("i18n.ls.cross_subsystem_numbering", TestLsCrossSubsystemNumbering);
    TestRegister("i18n.ls.collation_extension", TestLsCollationExtension);
    TestRegister("i18n.ls.robustness", TestLsRobustness);
}
