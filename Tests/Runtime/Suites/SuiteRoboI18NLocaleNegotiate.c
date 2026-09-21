/*
 * RoboI18N locale negotiation / Accept-Language (Goal 4.17).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NLocaleNegotiate.h"
#include "RoboI18N/RoboI18NNumber.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestNegVersion(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
}

static void TestNegRangeParse(void)
{
    I18nLanguageRange r;
    RoboCheckEqInt(I18nLanguageRangeParse("*", 1, &r), RoboOk);
    RoboCheck(r.isWildcard);
    RoboCheckEqInt(r.qMilli, 1000u);

    {
        const char *s = "en-US;q=0.8";
        RoboCheckEqInt(I18nLanguageRangeParse(s, strlen(s), &r), RoboOk);
    }
    RoboCheck(!r.isWildcard);
    RoboCheckStrEq(r.locale.language, "en");
    RoboCheckStrEq(r.locale.region, "US");
    RoboCheckEqInt(r.qMilli, 800u);

    {
        const char *s = "en;q=0";
        RoboCheckEqInt(I18nLanguageRangeParse(s, strlen(s), &r), RoboOk);
    }
    RoboCheckEqInt(r.qMilli, 0u);

    RoboCheck(I18nLanguageRangeParse("*-CH", 4, &r) != RoboOk);
    RoboCheck(I18nLanguageRangeParse("en;q=1.5", 8, &r) != RoboOk);
    RoboCheck(I18nLanguageRangeParse("en;q=-0.1", 9, &r) != RoboOk);
    RoboCheck(I18nLanguageRangeParse("", 0, &r) != RoboOk);
}

static void TestNegAcceptLanguage(void)
{
    I18nPreferenceList *list = NULL;
    I18nLanguageRange r;
    char buf[256];
    size_t n = 0;
    const char *hdr = "en-US,en;q=0.9,ur-PK;q=0.8,ar;q=0.7";

    RoboCheckEqInt(I18nAcceptLanguageParse(hdr, strlen(hdr), &list), RoboOk);
    RoboCheckEqInt(I18nPreferenceListNormalize(list), RoboOk);
    RoboCheckEqInt(I18nPreferenceListSize(list), 4u);
    RoboCheckEqInt(I18nPreferenceListGet(list, 0, &r), RoboOk);
    RoboCheckStrEq(r.locale.language, "en");
    RoboCheckStrEq(r.locale.region, "US");
    RoboCheckEqInt(r.qMilli, 1000u);

    RoboCheckEqInt(I18nAcceptLanguageSerialize(list, buf, sizeof(buf), &n), RoboOk);
    RoboCheck(n > 0);
    {
        I18nPreferenceList *again = NULL;
        RoboCheckEqInt(I18nAcceptLanguageParse(buf, n, &again), RoboOk);
        RoboCheckEqInt(I18nPreferenceListNormalize(again), RoboOk);
        RoboCheckEqInt(I18nPreferenceListSize(again), 4u);
        I18nPreferenceListDestroy(again);
    }
    I18nPreferenceListDestroy(list);
}

static void TestNegUrExample(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLocale supported[3];
    I18nNegotiateResult res;
    const char *hdr = "ur-PK,en-US;q=0.8";

    ParseTag("en-US", &supported[0]);
    ParseTag("ur", &supported[1]);
    ParseTag("ar", &supported[2]);

    RoboCheckEqInt(I18nAcceptLanguageParse(hdr, strlen(hdr), &prefs), RoboOk);
    RoboCheckEqInt(I18nPreferenceListNormalize(prefs), RoboOk);
    RoboCheckEqInt(I18nLocaleNegotiateEx(prefs, supported, 3, &res), RoboOk);
    RoboCheckStrEq(res.selected.language, "ur");
    RoboCheckEqInt(res.kind, RoboI18nNegLanguage);
    I18nPreferenceListDestroy(prefs);
}

static void TestNegZhScriptRegion(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLocale supported[3];
    I18nNegotiateResult res;
    I18nLanguageRange range;

    ParseTag("zh-Hans", &supported[0]);
    ParseTag("zh-TW", &supported[1]);
    ParseTag("en", &supported[2]);

    RoboCheckEqInt(I18nAcceptLanguageParse("zh-Hant-TW", 10, &prefs), RoboOk);
    RoboCheckEqInt(I18nLocaleNegotiateEx(prefs, supported, 3, &res), RoboOk);
    RoboCheckStrEq(res.selected.language, "zh");
    RoboCheckStrEq(res.selected.region, "TW");
    RoboCheck(res.kind == RoboI18nNegRegion || res.kind == RoboI18nNegExact);

    RoboCheckEqInt(I18nLanguageRangeParse("zh-Hant-TW", 10, &range), RoboOk);
    RoboCheckEqInt(I18nLocaleMatchQuality(&range, &supported[0]), RoboI18nNegNone);
    I18nPreferenceListDestroy(prefs);
}

static void TestNegQ0Exclusion(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLocale supported[2];
    I18nNegotiateResult res;
    const char *hdr = "en;q=0,fr";

    ParseTag("en", &supported[0]);
    ParseTag("fr", &supported[1]);
    RoboCheckEqInt(I18nAcceptLanguageParse(hdr, strlen(hdr), &prefs), RoboOk);
    RoboCheckEqInt(I18nPreferenceListNormalize(prefs), RoboOk);
    RoboCheckEqInt(I18nLocaleNegotiateEx(prefs, supported, 2, &res), RoboOk);
    RoboCheckStrEq(res.selected.language, "fr");
    I18nPreferenceListDestroy(prefs);
}

static void TestNegWildcard(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLocale supported[2];
    I18nNegotiateResult res;

    ParseTag("ja", &supported[0]);
    ParseTag("ko", &supported[1]);
    RoboCheckEqInt(I18nAcceptLanguageParse("*;q=0.1", 7, &prefs), RoboOk);
    RoboCheckEqInt(I18nLocaleNegotiateEx(prefs, supported, 2, &res), RoboOk);
    RoboCheckStrEq(res.selected.language, "ja"); /* first supported */
    RoboCheckEqInt(res.kind, RoboI18nNegWildcard);
    I18nPreferenceListDestroy(prefs);
}

static void TestNegDuplicateNormalize(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLanguageRange r;
    const char *hdr = "en;q=0.5,en;q=0.9,en-US";

    RoboCheckEqInt(I18nAcceptLanguageParse(hdr, strlen(hdr), &prefs), RoboOk);
    RoboCheckEqInt(I18nPreferenceListNormalize(prefs), RoboOk);
    /* en and en-US are distinct; two en entries merge to highest q */
    RoboCheck(I18nPreferenceListSize(prefs) >= 2u);
    RoboCheckEqInt(I18nPreferenceListGet(prefs, 0, &r), RoboOk);
    /* highest q first: en-US (1000) or en (900) */
    RoboCheck(r.qMilli >= 900u);
    I18nPreferenceListDestroy(prefs);
}

static void TestNegIntlMatrix(void)
{
    static const char *prefsHdr[] = {
        "ur-PK", "ur-IN", "hi-IN", "bn-BD", "ar-EG", "ar-SA", "fa-IR", "he-IL",
        "zh-Hans", "zh-Hant", "zh-CN", "zh-TW", "ja-JP", "ko-KR",
        "en-US", "en-GB", "fr-FR", "de-DE", "es-ES", "pt-BR", "pt-PT", "ru-RU", "tr-TR"
    };
    I18nLocale supported[8];
    size_t i;

    ParseTag("en", &supported[0]);
    ParseTag("ur", &supported[1]);
    ParseTag("ar", &supported[2]);
    ParseTag("zh-Hans", &supported[3]);
    ParseTag("zh-Hant", &supported[4]);
    ParseTag("ja", &supported[5]);
    ParseTag("pt-BR", &supported[6]);
    ParseTag("de", &supported[7]);

    for (i = 0; i < sizeof(prefsHdr) / sizeof(prefsHdr[0]); i++) {
        I18nPreferenceList *prefs = NULL;
        I18nNegotiateResult res;
        RoboCheckEqInt(
            I18nAcceptLanguageParse(prefsHdr[i], strlen(prefsHdr[i]), &prefs),
            RoboOk);
        /* May or may not match — must not crash; when match, locale valid */
        if (I18nLocaleNegotiateEx(prefs, supported, 8, &res) == RoboOk) {
            RoboCheck(res.selected.language[0] != '\0');
            RoboCheck(res.kind != RoboI18nNegNone);
        }
        I18nPreferenceListDestroy(prefs);
    }
}

static void TestNegPtRegion(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLocale supported[2];
    I18nNegotiateResult res;

    ParseTag("pt-BR", &supported[0]);
    ParseTag("pt-PT", &supported[1]);
    RoboCheckEqInt(I18nAcceptLanguageParse("pt-BR", 5, &prefs), RoboOk);
    RoboCheckEqInt(I18nLocaleNegotiateEx(prefs, supported, 2, &res), RoboOk);
    RoboCheckStrEq(res.selected.region, "BR");
    I18nPreferenceListDestroy(prefs);
}

static void TestNegLimitsNegative(void)
{
    char big[RoboI18nAcceptLangMaxBytes + 8u];
    I18nPreferenceList *list = NULL;
    memset(big, 'a', sizeof(big));
    big[5] = '-';
    RoboCheck(I18nAcceptLanguageParse(big, sizeof(big), &list) != RoboOk);

    {
        unsigned char bad[] = {0xFF, 0xFE, 'e', 'n'};
        RoboCheckEqInt(
            I18nAcceptLanguageParse((const char *)bad, sizeof(bad), &list),
            RoboParseError);
    }
}

static void TestNegCrossSubsystem(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLocale supported[2];
    I18nNegotiateResult res;
    I18nNumberFormatter nfmt;
    I18nPluralCategory cat;

    ParseTag("ur", &supported[0]);
    ParseTag("en-US", &supported[1]);
    RoboCheckEqInt(
        I18nAcceptLanguageParse("ur-PK,en-US;q=0.8", 17, &prefs), RoboOk);
    RoboCheckEqInt(I18nPreferenceListNormalize(prefs), RoboOk);
    RoboCheckEqInt(I18nLocaleNegotiateEx(prefs, supported, 2, &res), RoboOk);
    RoboCheckStrEq(res.selected.language, "ur");
    RoboCheckEqInt(I18nNumberFormatterInit(&nfmt, &res.selected, NULL), RoboOk);
    RoboCheckEqInt(I18nPluralCardinalI64(&res.selected, 1, &cat), RoboOk);
    I18nNumberFormatterDestroy(&nfmt);
    I18nPreferenceListDestroy(prefs);
}

static void TestNegFuzz(void)
{
    static const char *samples[] = {
        "", ",", ",,", "en,", ",en", "en;;q=1", "en;q=", "en;q=0.1234",
        "**********", "en-US-u-ca-gregory;q=0.5", "zh-Hans-CN;q=0.001",
        "*;q=0", "en;q=0,en;q=1", "a", "und", "root"
    };
    size_t i;
    uint32_t seed = 0xC0FFEEu;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        I18nPreferenceList *list = NULL;
        (void)I18nAcceptLanguageParse(samples[i], strlen(samples[i]), &list);
        if (list) {
            (void)I18nPreferenceListNormalize(list);
            I18nPreferenceListDestroy(list);
        }
    }
    for (i = 0; i < 64; i++) {
        char buf[64];
        size_t len = (seed % 60u) + 1u;
        size_t j;
        I18nPreferenceList *list = NULL;
        I18nLocale supported[3];
        I18nNegotiateResult res;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < len; j++) {
            static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-;,.q=* ";
            buf[j] = alphabet[seed % (sizeof(alphabet) - 1u)];
            seed = seed * 1664525u + 1013904223u;
        }
        (void)I18nAcceptLanguageParse(buf, len, &list);
        if (list) {
            ParseTag("en", &supported[0]);
            ParseTag("ur", &supported[1]);
            ParseTag("zh-Hans", &supported[2]);
            (void)I18nPreferenceListNormalize(list);
            (void)I18nLocaleNegotiateEx(list, supported, 3, &res);
            I18nPreferenceListDestroy(list);
        }
    }
    RoboCheck(1);
}

static void TestNegMicrobench(void)
{
    I18nPreferenceList *prefs = NULL;
    I18nLocale supported[16];
    size_t i;
    volatile int sink = 0;
    const char *hdr = "ur-PK,en-US;q=0.9,ar;q=0.7,zh-Hans;q=0.5,*;q=0.1";

    for (i = 0; i < 16; i++) {
        static const char *tags[] = {"en", "ur", "ar", "fa", "hi", "bn", "zh-Hans",
                                     "zh-Hant", "ja", "ko", "de", "fr", "es", "pt-BR",
                                     "ru", "tr"};
        ParseTag(tags[i], &supported[i]);
    }
    RoboCheckEqInt(I18nAcceptLanguageParse(hdr, strlen(hdr), &prefs), RoboOk);
    RoboCheckEqInt(I18nPreferenceListNormalize(prefs), RoboOk);
    for (i = 0; i < 5000; i++) {
        I18nNegotiateResult res;
        if (I18nLocaleNegotiateEx(prefs, supported, 16, &res) == RoboOk) {
            sink += (int)res.selected.language[0];
        }
    }
    RoboCheck(sink != 0);
    I18nPreferenceListDestroy(prefs);
}

void TestRegisterI18nLocaleNegotiate(void)
{
    TestRegister("i18n.neg.version", TestNegVersion);
    TestRegister("i18n.neg.range_parse", TestNegRangeParse);
    TestRegister("i18n.neg.accept_language", TestNegAcceptLanguage);
    TestRegister("i18n.neg.ur_example", TestNegUrExample);
    TestRegister("i18n.neg.zh_script_region", TestNegZhScriptRegion);
    TestRegister("i18n.neg.q0_exclusion", TestNegQ0Exclusion);
    TestRegister("i18n.neg.wildcard", TestNegWildcard);
    TestRegister("i18n.neg.duplicate_normalize", TestNegDuplicateNormalize);
    TestRegister("i18n.neg.intl_matrix", TestNegIntlMatrix);
    TestRegister("i18n.neg.pt_region", TestNegPtRegion);
    TestRegister("i18n.neg.limits_negative", TestNegLimitsNegative);
    TestRegister("i18n.neg.cross_subsystem", TestNegCrossSubsystem);
    TestRegister("i18n.neg.fuzz", TestNegFuzz);
    TestRegister("i18n.neg.microbench", TestNegMicrobench);
}
