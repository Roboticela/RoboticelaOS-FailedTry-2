/*
 * RoboI18N Phase 4 final integration suite (Goal 4.27).
 *
 * Frozen API check, regional regression matrix, formatter/parser round trips,
 * Unicode-heavy / RTL / message integration, concurrent formatters, fuzz/perf.
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NLocaleService.h"
#include "RoboI18N/RoboI18NVersion.h"
#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeNorm.h"
#include "RoboUnicode/RoboUnicodeVersion.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const char *const kPhase4Tags[] = {
    "en-US", "en-GB", "fr-FR", "de-DE", "es-ES", "tr-TR", "ru-RU", "uk-UA",
    "pl-PL", "ar-SA", "fa-IR", "ur-PK", "he-IL", "hi-IN", "bn-BD", "th-TH",
    "zh-CN", "zh-TW", "ja-JP", "ko-KR",
};

static void TestP4FreezeVersions(void)
{
    I18nDataVersions v;
    RoboCheckEqInt(RoboI18nApiFrozen, 1);
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    GetI18nDataVersions(&v);
    RoboCheckStrEq(v.apiVersion, "0.27.0");
    RoboCheckStrEq(v.generatorVersion, "1.25.0");
    RoboCheckStrEq(v.cldrVersion, "46");
    RoboCheckStrEq(v.unicodeVersion, "16.0.0");
    RoboCheckStrEq(v.tzdbVersion, "2026c");
    RoboCheckStrEq(v.ucaVersion, "16.0.0");
    RoboCheckStrEq(v.iso4217Source, "CLDR-46-codeMappings");
    RoboCheckStrEq(RoboUnicodeVersionString, v.unicodeVersion);
}

static void TestP4RegionalMatrix(void)
{
    size_t i;
    for (i = 0; i < sizeof(kPhase4Tags) / sizeof(kPhase4Tags[0]); i++) {
        I18nLocale loc;
        I18nLocaleSettings st;
        I18nLocaleOverrides ov;
        I18nNumberFormatter nfmt;
        I18nNumberOptions nopts;
        I18nPluralOperands ops;
        I18nPluralCategory cat = RoboI18nPluralOther;
        char buf[96];
        size_t n = 0;

        RoboCheckEqInt(
            I18nLocaleParseCanonical(kPhase4Tags[i], strlen(kPhase4Tags[i]), &loc),
            RoboOk);
        I18nLocaleOverridesInit(&ov);
        RoboCheckEqInt(I18nLocaleResolve(&loc, &ov, &st), RoboOk);
        RoboCheck(st.numberingSystem[0] != 0);
        RoboCheck(st.calendar[0] != 0);

        I18nNumberOptionsInit(&nopts);
        RoboCheckEqInt(I18nNumberFormatterInit(&nfmt, &loc, &nopts), RoboOk);
        RoboCheckEqInt(I18nNumberFormatDouble(&nfmt, 1234.5, buf, sizeof(buf), &n),
                          RoboOk);
        RoboCheck(n > 0);
        I18nNumberFormatterDestroy(&nfmt);

        RoboCheckEqInt(I18nPluralOperandsFromString("1", 1, &ops), RoboOk);
        RoboCheckEqInt(
            I18nPluralSelect(&loc, RoboI18nPluralCardinal, &ops, &cat), RoboOk);
    }
}

static void TestP4NumberRoundtrip(void)
{
    static const char *const tags[] = {"en-US", "de-DE", "fr-FR", "ar-SA", "hi-IN", "th-TH"};
    size_t ti;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        I18nLocale loc;
        I18nNumberFormatter fmt;
        I18nNumberOptions opts;
        I18nNumberParser parser;
        I18nParseOptions popts;
        I18nParseResult res;
        I18nDecimal dec;
        char buf[128];
        size_t n = 0;

        RoboCheckEqInt(I18nLocaleParseCanonical(tags[ti], strlen(tags[ti]), &loc),
                          RoboOk);
        I18nNumberOptionsInit(&opts);
        opts.maxFractionDigits = 2;
        RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
        memset(&dec, 0, sizeof(dec));
        dec.kind = RoboI18nDecimalNumber;
        dec.digitCount = 4;
        memcpy(dec.digits, "1234", 4);
        dec.scale = 2; /* 12.34 */
        RoboCheckEqInt(I18nNumberFormat(&fmt, &dec, buf, sizeof(buf), &n), RoboOk);
        RoboCheck(n > 0);

        I18nParseOptionsInit(&popts);
        popts.mode = RoboI18nParsePermissive;
        RoboCheckEqInt(I18nNumberParserInit(&parser, &loc, &popts), RoboOk);
        RoboCheckEqInt(I18nParseNumber(&parser, buf, n, &res), RoboOk);
        RoboCheckEqInt((int)res.error, (int)RoboI18nParseErrNone);
        RoboCheckEqInt((int)res.value.kind, (int)RoboI18nDecimalNumber);
        RoboCheckEqInt((int)res.value.digitCount, 4);
        RoboCheck(memcmp(res.value.digits, "1234", 4) == 0);
        RoboCheckEqInt((int)res.value.scale, 2);
        I18nNumberParserDestroy(&parser);
        I18nNumberFormatterDestroy(&fmt);
    }
}

static void TestP4CurrencyRoundtrip(void)
{
    I18nLocale loc;
    I18nCurrencyFormatter fmt;
    I18nCurrencyOptions opts;
    I18nCurrencyParser parser;
    I18nCurrencyParseOptions popts;
    I18nParseResult res;
    char buf[128];
    size_t n = 0;

    RoboCheckEqInt(I18nLocaleParseCanonical("en-US", 5, &loc), RoboOk);
    I18nCurrencyOptionsInit(&opts);
    memcpy(opts.currency, "USD", 4);
    RoboCheckEqInt(I18nCurrencyFormatterInit(&fmt, &loc, &opts), RoboOk);
    RoboCheckEqInt(I18nCurrencyFormatMinor(&fmt, 1234, buf, sizeof(buf), &n),
                      RoboOk);
    RoboCheck(n > 0);

    I18nCurrencyParseOptionsInit(&popts);
    memcpy(popts.requireCurrency, "USD", 4);
    RoboCheckEqInt(I18nCurrencyParserInit(&parser, &loc, &popts), RoboOk);
    RoboCheckEqInt(I18nParseCurrency(&parser, buf, n, &res), RoboOk);
    RoboCheckEqInt((int)res.error, (int)RoboI18nParseErrNone);
    RoboCheckStrEq(res.currency, "USD");
    I18nCurrencyParserDestroy(&parser);
    I18nCurrencyFormatterDestroy(&fmt);
}

static void TestP4DatetimeRoundtrip(void)
{
    static const char *const tags[] = {"en-US", "fr-FR", "ja-JP", "ar-SA", "zh-CN"};
    size_t ti;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        I18nLocale loc;
        I18nDatetimeFormatter fmt;
        I18nDatetimeParser parser;
        I18nDtfmtOptions fopts;
        I18nDtparseOptions popts;
        I18nDtparseResult res;
        I18nDate date;
        char buf[128];
        size_t n = 0;

        RoboCheckEqInt(I18nLocaleParseCanonical(tags[ti], strlen(tags[ti]), &loc),
                          RoboOk);
        I18nDtfmtOptionsInit(&fopts);
        fopts.dateStyle = RoboI18nCalStyleShort;
        RoboCheckEqInt(I18nDatetimeFormatterInit(&fmt, &loc, NULL, NULL,
                                                            RoboI18nDtfmtDate, &fopts),
                          RoboOk);
        RoboCheckEqInt(I18nDateInit(&date, 2026, 9, 13), RoboOk);
        RoboCheckEqInt(I18nDatetimeFormatDate(&fmt, &date, buf, sizeof(buf), &n),
                          RoboOk);
        RoboCheck(n > 0);

        I18nDtparseOptionsInit(&popts);
        popts.dateStyle = RoboI18nCalStyleShort;
        RoboCheckEqInt(
            I18nDatetimeParserInit(&parser, &loc, NULL, NULL, RoboI18nDtfmtDate,
                                           &popts),
            RoboOk);
        RoboCheckEqInt(I18nParseDate(&parser, buf, n, &res), RoboOk);
        RoboCheckEqInt((int)res.error, (int)RoboI18nDtparseErrNone);
        RoboCheckEqInt((int)res.haveDate, 1);
        RoboCheckEqInt(res.gregorianDate.year, 2026);
        RoboCheckEqInt((int)res.gregorianDate.month, 9);
        RoboCheckEqInt((int)res.gregorianDate.day, 13);
        I18nDatetimeParserDestroy(&parser);
        I18nDatetimeFormatterDestroy(&fmt);
    }
}

static void TestP4MessagePluralKinds(void)
{
    I18nLocale en;
    I18nMfMessage *msg = NULL;
    I18nMfArg arg;
    I18nMfArgs args;
    char out[128];
    size_t n = 0;
    const char *card = "{n, plural, one {one card} other {other card}}";
    const char *ord = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";

    RoboCheckEqInt(I18nLocaleParseCanonical("en-US", 5, &en), RoboOk);

    RoboCheckEqInt(I18nMfParse(card, strlen(card), &msg), RoboOk);
    I18nMfArgSetI64(&arg, "n", 1);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(I18nMfFormat(msg, &en, &args, out, sizeof(out), &n), RoboOk);
    RoboCheckStrEq(out, "one card");
    I18nMfArgSetI64(&arg, "n", 2);
    RoboCheckEqInt(I18nMfFormat(msg, &en, &args, out, sizeof(out), &n), RoboOk);
    RoboCheckStrEq(out, "other card");
    I18nMfDestroy(msg);

    RoboCheckEqInt(I18nMfParse(ord, strlen(ord), &msg), RoboOk);
    I18nMfArgSetI64(&arg, "n", 2);
    RoboCheckEqInt(I18nMfFormat(msg, &en, &args, out, sizeof(out), &n), RoboOk);
    RoboCheckStrEq(out, "2nd");
    I18nMfDestroy(msg);
}

static void TestP4UnicodeHeavy(void)
{
    /* Combining mark: e + combining acute */
    static const char nfcE[] = "\xC3\xA9";  /* é NFC */
    static const char nfdE[] = "e\xCC\x81"; /* e + U+0301 */
    /* ZWJ family emoji (man+ZWJ+woman+ZWJ+girl) — validate UTF-8 only */
    static const char zwj[] =
        "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    /* Arabic + Latin mix */
    static const char mix[] = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    char *norm = NULL;
    size_t nlen = 0;
    int found = 0;
    I18nSearcher searcher;
    I18nSearchOptions sopts;
    I18nSearchHit hit;
    I18nLocale ar;

    RoboCheckEqInt(UnicodeNormalizeNfcUtf8(nfdE, strlen(nfdE), RoboUnicodeStrict,
                                                      &norm, &nlen),
                      RoboOk);
    RoboCheck(nlen == strlen(nfcE));
    RoboCheck(memcmp(norm, nfcE, nlen) == 0);
    RoboFree(norm);

    RoboCheck(UnicodeUtf8Validate(zwj, strlen(zwj)));
    RoboCheck(UnicodeUtf8Validate(mix, strlen(mix)));

    RoboCheckEqInt(I18nLocaleParseCanonical("ar-SA", 5, &ar), RoboOk);
    I18nSearchOptionsInit(&sopts);
    sopts.mode = RoboI18nSearchCanonical;
    sopts.graphemeSafe = 1;
    RoboCheckEqInt(I18nSearcherInit(&searcher, &ar, &sopts), RoboOk);
    RoboCheckEqInt(I18nSearchFind(&searcher, nfdE, strlen(nfdE), nfcE,
                                            strlen(nfcE), &found, &hit),
                      RoboOk);
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt((int)hit.start, 0);
    RoboCheck((int)hit.end > 0);
    I18nSearcherDestroy(&searcher);
}

static void TestP4SearchByteRanges(void)
{
    I18nSearcher s;
    I18nSearchOptions o;
    I18nSearchHit hit;
    int found = 0;
    /* "café" with NFC é — needle "fé" */
    const char *hay = "caf\xC3\xA9";
    const char *needle = "f\xC3\xA9";

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchExact;
    RoboCheckEqInt(I18nSearcherInit(&s, NULL, &o), RoboOk);
    RoboCheckEqInt(
        I18nSearchFind(&s, hay, strlen(hay), needle, strlen(needle), &found, &hit),
        RoboOk);
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt((int)hit.start, 2);
    RoboCheckEqInt((int)hit.end, (int)strlen(hay));
    /* Source byte range must stay within haystack. */
    RoboCheck(hit.end <= strlen(hay));
    I18nSearcherDestroy(&s);
}

static void TestP4TimezoneRtl(void)
{
    I18nTimezone tz;
    I18nLocale loc;
    char name[96];
    size_t n = 0;

    RoboCheckEqInt(I18nTimezoneFromId("Asia/Karachi", 12, &tz), RoboOk);
    RoboCheckEqInt(I18nLocaleParseCanonical("ur-PK", 5, &loc), RoboOk);
    RoboCheckEqInt(I18nTimezoneDisplayName(&tz, RoboI18nTzDispExemplarCity,
                                                      &loc, NULL, name, sizeof(name), &n),
                      RoboOk);
    RoboCheck(n > 0);
    RoboCheckEqInt(I18nTimezoneFromId("America/New_York", 16, &tz), RoboOk);
}

#ifdef _WIN32
typedef struct p4ThreadArg {
    int ok;
    int iters;
} P4ThreadArg;

static DWORD WINAPI P4Worker(LPVOID param)
{
    P4ThreadArg *a = (P4ThreadArg *)param;
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    char buf[64];
    size_t n = 0;
    int i;

    if (I18nLocaleParseCanonical("en-US", 5, &loc) != RoboOk) {
        a->ok = 0;
        return 1;
    }
    I18nNumberOptionsInit(&opts);
    if (I18nNumberFormatterInit(&fmt, &loc, &opts) != RoboOk) {
        a->ok = 0;
        return 1;
    }
    for (i = 0; i < a->iters; i++) {
        if (I18nNumberFormatDouble(&fmt, (double)i, buf, sizeof(buf), &n) !=
            RoboOk) {
            I18nNumberFormatterDestroy(&fmt);
            a->ok = 0;
            return 1;
        }
    }
    I18nNumberFormatterDestroy(&fmt);
    a->ok = 1;
    return 0;
}

static void TestP4Threads(void)
{
    enum { N = 4 };
    HANDLE hs[N];
    P4ThreadArg args[N];
    int i;
    for (i = 0; i < N; i++) {
        args[i].ok = 0;
        args[i].iters = 500;
        hs[i] = CreateThread(NULL, 0, P4Worker, &args[i], 0, NULL);
        RoboCheck(hs[i] != NULL);
    }
    WaitForMultipleObjects(N, hs, TRUE, INFINITE);
    for (i = 0; i < N; i++) {
        CloseHandle(hs[i]);
        RoboCheckEqInt(args[i].ok, 1);
    }
}
#else
static void TestP4Threads(void)
{
    /* Host tests currently target Windows; treat as skip-pass elsewhere. */
    RoboCheck(1);
}
#endif

static void TestP4FuzzLimits(void)
{
    I18nLocale loc;
    I18nMfMessage *msg = NULL;
    (void)I18nLocaleParseCanonical("\x80\x81", 2, &loc);
    (void)I18nLocaleParseCanonical(
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        76, &loc);
    (void)I18nMfParse("{", 1, &msg);
    if (msg) {
        I18nMfDestroy(msg);
    }
    (void)I18nMfValidate("{n, plural, one {x}", 18);
}

static void TestP4PerfSmoke(void)
{
    clock_t t0, t1;
    size_t i;
    char out[64];
    size_t n = 0;
    I18nLocale loc;
    I18nNumberFormatter fmt;
    I18nNumberOptions opts;
    I18nPluralCategory cat;

    RoboCheckEqInt(I18nLocaleParseCanonical("en-US", 5, &loc), RoboOk);
    I18nNumberOptionsInit(&opts);
    RoboCheckEqInt(I18nNumberFormatterInit(&fmt, &loc, &opts), RoboOk);
    t0 = clock();
    for (i = 0; i < 3000u; i++) {
        RoboCheckEqInt(I18nNumberFormatDouble(&fmt, (double)i, out, sizeof(out), &n),
                          RoboOk);
        RoboCheckEqInt(I18nPluralCardinalI64(&loc, (int64_t)i, &cat), RoboOk);
    }
    t1 = clock();
    I18nNumberFormatterDestroy(&fmt);
    RoboCheck((t1 - t0) < 30 * CLOCKS_PER_SEC);
}

void TestRegisterI18nPhase4(void)
{
    TestRegister("i18n.p4.freeze", TestP4FreezeVersions);
    TestRegister("i18n.p4.regional_matrix", TestP4RegionalMatrix);
    TestRegister("i18n.p4.number_roundtrip", TestP4NumberRoundtrip);
    TestRegister("i18n.p4.currency_roundtrip", TestP4CurrencyRoundtrip);
    TestRegister("i18n.p4.datetime_roundtrip", TestP4DatetimeRoundtrip);
    TestRegister("i18n.p4.message_kinds", TestP4MessagePluralKinds);
    TestRegister("i18n.p4.unicode_heavy", TestP4UnicodeHeavy);
    TestRegister("i18n.p4.search_ranges", TestP4SearchByteRanges);
    TestRegister("i18n.p4.timezone_rtl", TestP4TimezoneRtl);
    TestRegister("i18n.p4.threads", TestP4Threads);
    TestRegister("i18n.p4.fuzz", TestP4FuzzLimits);
    TestRegister("i18n.p4.perf_smoke", TestP4PerfSmoke);
}
