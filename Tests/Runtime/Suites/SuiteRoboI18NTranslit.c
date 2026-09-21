/*
 * RoboI18N transliteration suite (Goal 4.25).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void translit(const char *id, const char *in, char *out, size_t outCap, size_t *outLen)
{
    I18nTransliterator tr;
    RoboCheckEqInt(I18nTransliteratorCreateId(id, NULL, &tr), RoboOk);
    RoboCheckEqInt(I18nTransliterate(&tr, in, strlen(in), out, outCap, outLen, NULL, 0,
                                              NULL),
                      RoboOk);
    I18nTransliteratorDestroy(&tr);
}

static void TestTranslitVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nTranslitCldrVersionString(), "46");
    RoboCheck(I18nTranslitIdCount() >= 8u);
}

static void TestTranslitLookup(void)
{
    uint32_t idx = 0;
    I18nTranslitIdInfo info;
    RoboCheckEqInt(I18nTranslitFindId("Cyrillic-Latin", &idx), RoboOk);
    RoboCheckEqInt(GetI18nTranslitIdInfo(idx, &info), RoboOk);
    RoboCheckStrEq(info.id, "Cyrillic-Latin");
    RoboCheckEqInt(I18nTranslitFindId("NoSuch-Transform", &idx), RoboNotFound);
}

static void TestTranslitRussianBgn(void)
{
    char out[256];
    size_t n = 0;
    /* Москва → Moskva (BGN) */
    translit("Russian-Latin/BGN", "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", out,
             sizeof(out), &n);
    RoboCheckStrEq(out, "Moskva");
}

static void TestTranslitCyrillic(void)
{
    char out[256];
    size_t n = 0;
    translit("Cyrillic-Latin", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", out, sizeof(out),
             &n);
    RoboCheck(n > 0);
    RoboCheck(out[0] == 'P' || out[0] == 'p');
}

static void TestTranslitUkrainian(void)
{
    char out[256];
    size_t n = 0;
    translit("Ukrainian-Latin/BGN", "\xD0\x9A\xD0\xB8\xD1\x97\xD0\xB2", out, sizeof(out), &n);
    RoboCheck(n > 0);
}

static void TestTranslitLatinAscii(void)
{
    char out[256];
    size_t n = 0;
    uint32_t idx = 0;
    RoboCheckEqInt(I18nTranslitFindId("Latin-ASCII", &idx), RoboOk);
    RoboCheck(idx != 0u); /* must not collapse to Cyrillic-Latin */
    /* Æ → AE */
    translit("Latin-ASCII", "\xC3\x86", out, sizeof(out), &n);
    RoboCheckStrEq(out, "AE");
    /* ß → ss */
    translit("Latin-ASCII", "\xC3\x9F", out, sizeof(out), &n);
    RoboCheckStrEq(out, "ss");
}

static void TestTranslitScriptMatrix(void)
{
    char out[256];
    size_t n = 0;
    /* Greek λ → l */
    translit("Greek-Latin", "\xCE\xBB", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck(out[0] == 'l' || out[0] == 'L');
    /* Arabic ب → b (BGN-style Latin) */
    translit("Arabic-Latin", "\xD8\xA8", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Hebrew ב */
    translit("Hebrew-Latin", "\xD7\x91", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Devanagari क */
    translit("Devanagari-Latin", "\xE0\xA4\x95", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Bengali ক */
    translit("Bengali-Latin", "\xE0\xA6\x95", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Hangul 한 */
    translit("Hangul-Latin", "\xED\x95\x9C", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Katakana カ */
    translit("Katakana-Latin", "\xE3\x82\xAB", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Hiragana か */
    translit("Hiragana-Latin", "\xE3\x81\x8B", out, sizeof(out), &n);
    RoboCheck(n >= 1);
    RoboCheck((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
}

static void TestTranslitRtlLogical(void)
{
    char out[256];
    size_t n = 0;
    translit("Arabic-Latin", "\xD8\xA8\xD8\xA7", out, sizeof(out), &n);
    RoboCheck(n >= 1);
}

static void TestTranslitStream(void)
{
    I18nTransliterator tr;
    char a[64], b[64], c[64];
    size_t na = 0, nb = 0, nc = 0;
    RoboCheckEqInt(I18nTransliteratorCreateId("Latin-ASCII", NULL, &tr), RoboOk);
    RoboCheckEqInt(I18nTransliteratorStreamReset(&tr), RoboOk);
    RoboCheckEqInt(I18nTransliteratorStreamFeed(&tr, "caf", 3, a, sizeof(a), &na),
                      RoboOk);
    RoboCheckEqInt(
        I18nTransliteratorStreamFeed(&tr, "\xC3\xA9", 2, b, sizeof(b), &nb), RoboOk);
    RoboCheckEqInt(I18nTransliteratorStreamFinish(&tr, c, sizeof(c), &nc), RoboOk);
    I18nTransliteratorDestroy(&tr);
}

static void TestTranslitLimitsFuzz(void)
{
    I18nTransliterator tr;
    char out[64];
    size_t n = 0;
    char longbuf[70000];
    size_t i;
    RoboCheckEqInt(I18nTransliteratorCreateId("Cyrillic-Latin", NULL, &tr), RoboOk);
    (void)I18nTransliterate(&tr, "\xFF\xFE", 2, out, sizeof(out), &n, NULL, 0, NULL);
    for (i = 0; i < sizeof(longbuf); i++) {
        longbuf[i] = 'a';
    }
    RoboCheckEqInt(
        I18nTransliterate(&tr, longbuf, sizeof(longbuf), out, sizeof(out), &n, NULL, 0, NULL),
        RoboInvalidArgument);
    I18nTransliteratorDestroy(&tr);
}

static void TestTranslitSearchBoundary(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int found = 0;
    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchExact;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchContains(&m, "Moskva", 6, "Moskva", 6, &found), RoboOk);
    RoboCheckEqInt(found, 1);
    found = 0;
    /* Exact search does not transliterate Cyrillic to Latin. */
    RoboCheckEqInt(I18nSearchContains(&m, "\xD0\x9C", 2, "M", 1, &found), RoboOk);
    RoboCheckEqInt(found, 0);
    I18nSearcherDestroy(&m);
}

static void TestTranslitMap(void)
{
    I18nTransliterator tr;
    char out[64];
    size_t n = 0, mc = 0;
    I18nTranslitMapEntry map[4];
    RoboCheckEqInt(I18nTransliteratorCreateId("Russian-Latin/BGN", NULL, &tr), RoboOk);
    RoboCheckEqInt(I18nTransliterate(&tr, "\xD0\x9C", 2, out, sizeof(out), &n, map, 4,
                                              &mc),
                      RoboOk);
    RoboCheckEqInt((int)mc, 1);
    RoboCheckEqInt((int)map[0].srcStart, 0);
    RoboCheckEqInt((int)map[0].srcEnd, 2);
    I18nTransliteratorDestroy(&tr);
}

void TestRegisterI18nTranslit(void)
{
    TestRegister("i18n.translit.versions", TestTranslitVersions);
    TestRegister("i18n.translit.lookup", TestTranslitLookup);
    TestRegister("i18n.translit.russian_bgn", TestTranslitRussianBgn);
    TestRegister("i18n.translit.cyrillic", TestTranslitCyrillic);
    TestRegister("i18n.translit.ukrainian", TestTranslitUkrainian);
    TestRegister("i18n.translit.latin_ascii", TestTranslitLatinAscii);
    TestRegister("i18n.translit.script_matrix", TestTranslitScriptMatrix);
    TestRegister("i18n.translit.rtl", TestTranslitRtlLogical);
    TestRegister("i18n.translit.stream", TestTranslitStream);
    TestRegister("i18n.translit.fuzz", TestTranslitLimitsFuzz);
    TestRegister("i18n.translit.search_boundary", TestTranslitSearchBoundary);
    TestRegister("i18n.translit.map", TestTranslitMap);
}
