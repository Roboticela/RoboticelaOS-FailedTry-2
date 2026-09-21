/*
 * RoboI18N locale suite (Goal 4.1 / BCP 47).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void ExpectParseOk(const char *tag, const char *wantLang, const char *wantScript,
                            const char *wantRegion)
{
    I18nLocale loc;
    char buf[128];
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), &loc), RoboOk);
    if (wantLang) {
        RoboCheckStrEq(loc.language, wantLang);
    }
    if (wantScript) {
        RoboCheckStrEq(loc.script, wantScript);
    } else {
        RoboCheck(loc.script[0] == '\0');
    }
    if (wantRegion) {
        RoboCheckStrEq(loc.region, wantRegion);
    } else {
        RoboCheck(loc.region[0] == '\0');
    }
    RoboCheckEqInt(I18nLocaleFormat(&loc, buf, sizeof(buf), NULL), RoboOk);
}

static void TestI18nVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nIanaFileDate(), "2026-05-05");
    RoboCheckStrEq(I18nCldrVersionString(), "46");
}

static void TestI18nBasic(void)
{
    ExpectParseOk("en", "en", NULL, NULL);
    ExpectParseOk("ur", "ur", NULL, NULL);
    ExpectParseOk("ar", "ar", NULL, NULL);
    ExpectParseOk("fa", "fa", NULL, NULL);
    ExpectParseOk("he", "he", NULL, NULL);
    ExpectParseOk("hi", "hi", NULL, NULL);
    ExpectParseOk("bn", "bn", NULL, NULL);
    ExpectParseOk("th", "th", NULL, NULL);
    ExpectParseOk("zh", "zh", NULL, NULL);
    ExpectParseOk("ja", "ja", NULL, NULL);
    ExpectParseOk("ko", "ko", NULL, NULL);
    ExpectParseOk("EN", "en", NULL, NULL); /* casing */
}

static void TestI18nScripts(void)
{
    ExpectParseOk("zh-Hans", "zh", "Hans", NULL);
    ExpectParseOk("zh-Hant", "zh", "Hant", NULL);
    ExpectParseOk("zh-hant", "zh", "Hant", NULL);
    ExpectParseOk("sr-Cyrl", "sr", "Cyrl", NULL);
    ExpectParseOk("sr-Latn", "sr", "Latn", NULL);
}

static void TestI18nRegions(void)
{
    ExpectParseOk("en-US", "en", NULL, "US");
    ExpectParseOk("en-GB", "en", NULL, "GB");
    ExpectParseOk("ur-PK", "ur", NULL, "PK");
    ExpectParseOk("fa-AF", "fa", NULL, "AF");
    ExpectParseOk("ar-SA", "ar", NULL, "SA");
    ExpectParseOk("ar-EG", "ar", NULL, "EG");
    ExpectParseOk("zh-Hant-TW", "zh", "Hant", "TW");
    ExpectParseOk("sr-Latn-RS", "sr", "Latn", "RS");
}

static void TestI18nComplex(void)
{
    I18nLocale loc;
    RoboCheckEqInt(
        I18nLocaleParse("en-US-u-ca-gregory", strlen("en-US-u-ca-gregory"), &loc),
        RoboOk);
    RoboCheckStrEq(loc.language, "en");
    RoboCheckStrEq(loc.region, "US");
    RoboCheckStrEq(loc.extensions, "u-ca-gregory");

    RoboCheckEqInt(
        I18nLocaleParse("en-x-private", strlen("en-x-private"), &loc), RoboOk);
    RoboCheckStrEq(loc.privateuse, "x-private");
}

static void TestI18nInvalid(void)
{
    I18nLocale loc;
    RoboCheckEqInt(I18nLocaleParse("", 0, &loc), RoboParseError);
    RoboCheckEqInt(I18nLocaleParse("e", 1, &loc), RoboParseError);
    RoboCheckEqInt(I18nLocaleParse("en-", 3, &loc), RoboParseError);
    RoboCheckEqInt(I18nLocaleParse("en--US", 6, &loc), RoboParseError);
    RoboCheckEqInt(I18nLocaleParse("-en", 3, &loc), RoboParseError);
    RoboCheckEqInt(I18nLocaleParse("toolonglang", 11, &loc), RoboParseError);
    RoboCheckEqInt(I18nLocaleParse(NULL, 0, &loc), RoboInvalidArgument);
}

static void TestI18nCanonicalize(void)
{
    I18nLocale loc;
    /* iw → he (IANA Preferred-Value) */
    RoboCheckEqInt(I18nLocaleParseCanonical("iw", 2, &loc), RoboOk);
    RoboCheckStrEq(loc.language, "he");
    /* in → id */
    RoboCheckEqInt(I18nLocaleParseCanonical("in", 2, &loc), RoboOk);
    RoboCheckStrEq(loc.language, "id");
}

static void TestI18nEqualityMatch(void)
{
    I18nLocale a, b, range;
    RoboCheckEqInt(I18nLocaleParse("zh-Hans", 7, &a), RoboOk);
    RoboCheckEqInt(I18nLocaleParse("zh-Hant", 7, &b), RoboOk);
    RoboCheck(!I18nLocaleEqual(&a, &b));

    RoboCheckEqInt(I18nLocaleParse("ZH-hans", 7, &b), RoboOk);
    RoboCheck(I18nLocaleEqual(&a, &b));

    RoboCheckEqInt(I18nLocaleParse("zh", 2, &range), RoboOk);
    RoboCheckEqInt(I18nLocaleMatches(&range, &a), RoboI18nMatchLanguage);

    RoboCheckEqInt(I18nLocaleParse("sr-Cyrl", 7, &a), RoboOk);
    RoboCheckEqInt(I18nLocaleParse("sr-Latn", 7, &b), RoboOk);
    RoboCheck(!I18nLocaleEqual(&a, &b));
}

static void TestI18nFallback(void)
{
    I18nLocale loc, parent;
    char buf[64];

    RoboCheckEqInt(I18nLocaleParse("zh-Hant-TW", 10, &loc), RoboOk);
    RoboCheckEqInt(I18nLocaleParent(&loc, &parent), RoboOk);
    RoboCheckEqInt(I18nLocaleFormat(&parent, buf, sizeof(buf), NULL), RoboOk);
    RoboCheckStrEq(buf, "zh-Hant");
    RoboCheckEqInt(I18nLocaleParent(&parent, &loc), RoboOk);
    RoboCheckEqInt(I18nLocaleFormat(&loc, buf, sizeof(buf), NULL), RoboOk);
    RoboCheckStrEq(buf, "zh");
    RoboCheckEqInt(I18nLocaleParent(&loc, &parent), RoboNotFound);

    RoboCheckEqInt(I18nLocaleParse("sr-Latn-RS", 10, &loc), RoboOk);
    RoboCheckEqInt(I18nLocaleParent(&loc, &parent), RoboOk);
    RoboCheckStrEq(parent.script, "Latn");
    RoboCheck(parent.region[0] == '\0');

    RoboCheckEqInt(I18nLocaleParse("en-US", 5, &loc), RoboOk);
    RoboCheckEqInt(I18nLocaleParent(&loc, &parent), RoboOk);
    RoboCheckStrEq(parent.language, "en");
    RoboCheck(parent.region[0] == '\0');

    RoboCheckEqInt(I18nLocaleParse("ur-PK", 5, &loc), RoboOk);
    RoboCheckEqInt(I18nLocaleParent(&loc, &parent), RoboOk);
    RoboCheckStrEq(parent.language, "ur");
}

static void TestI18nDefaultAndList(void)
{
    I18nLocale loc, got;
    I18nLocaleList *list = NULL;

    RoboCheckEqInt(I18nInit(), RoboOk);
    RoboCheckEqInt(I18nGetDefaultLocale(&got), RoboOk);
    RoboCheckStrEq(got.language, "und");

    RoboCheckEqInt(I18nLocaleParse("ur-PK", 5, &loc), RoboOk);
    RoboCheckEqInt(I18nSetDefaultLocale(&loc), RoboOk);
    RoboCheckEqInt(I18nGetDefaultLocale(&got), RoboOk);
    RoboCheck(I18nLocaleEqual(&loc, &got));

    RoboCheckEqInt(
        I18nLocaleListParse("ur-PK, en;q=0.5, ar-SA;q=0.9", 28, &list), RoboOk);
    RoboCheck(list != NULL);
    RoboCheckEqInt(I18nLocaleListSize(list), 3u);
    RoboCheckEqInt(I18nLocaleListGet(list, 0, &got), RoboOk);
    RoboCheckStrEq(got.language, "ur"); /* q=1.0 first */
    RoboCheckEqInt(I18nLocaleListGet(list, 1, &got), RoboOk);
    RoboCheckStrEq(got.language, "ar"); /* q=0.9 */
    RoboCheckEqInt(I18nLocaleListGet(list, 2, &got), RoboOk);
    RoboCheckStrEq(got.language, "en"); /* q=0.5 */
    I18nLocaleListDestroy(list);
}

static void TestI18nFuzz(void)
{
    /* Untrusted random-ish inputs must not crash; may PARSE_ERROR or OK. */
    static const unsigned char samples[][16] = {
        {0},
        {0xff, 0xfe, 0xfd},
        {'e', 'n', '-', 0x00, 'U', 'S'},
        {'-', '-', '-', '-'},
        {'x', '-', 'y'},
        {'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a'},
        {'z', 'h', '-', 'H', 'a', 'n', 's', '-', 'T', 'W', '-', 'u', '-', 'x'},
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        I18nLocale loc;
        (void)I18nLocaleParse((const char *)samples[i], sizeof(samples[i]), &loc);
    }
    RoboCheck(1);
}

static void TestI18nMicrobench(void)
{
    volatile int sink = 0;
    clock_t t0, t1;
    size_t i;
    const size_t N = 50000u;
    double ms;
    I18nLocale loc;

    t0 = clock();
    for (i = 0; i < N; i++) {
        sink += (I18nLocaleParseCanonical("zh-Hant-TW", 10, &loc) == RoboOk);
        sink += I18nLocaleEqual(&loc, &loc);
    }
    t1 = clock();
    ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("i18n.microbench parse_equal=%zu time_ms=%.3f sink=%d\n", N, ms, sink);
    RoboCheck(sink != 0);
}

void TestRegisterI18n(void)
{
    TestRegister("i18n.versions", TestI18nVersions);
    TestRegister("i18n.basic", TestI18nBasic);
    TestRegister("i18n.scripts", TestI18nScripts);
    TestRegister("i18n.regions", TestI18nRegions);
    TestRegister("i18n.complex", TestI18nComplex);
    TestRegister("i18n.invalid", TestI18nInvalid);
    TestRegister("i18n.canonicalize", TestI18nCanonicalize);
    TestRegister("i18n.equality_match", TestI18nEqualityMatch);
    TestRegister("i18n.fallback", TestI18nFallback);
    TestRegister("i18n.default_list", TestI18nDefaultAndList);
    TestRegister("i18n.fuzz", TestI18nFuzz);
    TestRegister("i18n.microbench", TestI18nMicrobench);
}
