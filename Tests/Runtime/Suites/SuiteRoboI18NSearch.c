/*
 * RoboI18N search & matching suite (Goal 4.24).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestSearchVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nSearchModeString(RoboI18nSearchCasefold), "casefold");
    RoboCheckStrEq(I18nSearchModeString(RoboI18nSearchCollation), "collation");
}

static void TestSearchExact(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int eq = 0;
    int found = 0;
    I18nSearchHit hit;

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchExact;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchEqual(&m, "abc", 3, "abc", 3, &eq), RoboOk);
    RoboCheckEqInt(eq, 1);
    RoboCheckEqInt(I18nSearchFind(&m, "xxabcyy", 7, "abc", 3, &found, &hit), RoboOk);
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt((int)hit.start, 2);
    RoboCheckEqInt((int)hit.end, 5);
    RoboCheckEqInt(I18nSearchStartsWith(&m, "abcdef", 6, "abc", 3, &found), RoboOk);
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt(I18nSearchEndsWith(&m, "xyzabc", 6, "abc", 3, &found), RoboOk);
    RoboCheckEqInt(found, 1);
    I18nSearcherDestroy(&m);
}

static void TestSearchCanonical(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int eq = 0;
    /* U+00E9 (é) vs e + combining acute */
    const char *precomposed = "\xC3\xA9";
    const char *decomposed = "e\xCC\x81";

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchCanonical;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchEqual(&m, precomposed, strlen(precomposed), decomposed,
                                            strlen(decomposed), &eq),
                      RoboOk);
    RoboCheckEqInt(eq, 1);
    /* Exact must differ */
    {
        I18nSearchOptions ox;
        I18nSearcher mx;
        I18nSearchOptionsInit(&ox);
        ox.mode = RoboI18nSearchExact;
        RoboCheckEqInt(I18nSearcherInit(&mx, NULL, &ox), RoboOk);
        RoboCheckEqInt(I18nSearchEqual(&mx, precomposed, strlen(precomposed), decomposed,
                                                strlen(decomposed), &eq),
                          RoboOk);
        RoboCheckEqInt(eq, 0);
        I18nSearcherDestroy(&mx);
    }
    I18nSearcherDestroy(&m);
}

static void TestSearchCasefold(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int eq = 0;
    int found = 0;
    I18nSearchHit hit;

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchCasefold;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchEqual(&m, "Stra\xC3\x9F" "e", 7, "STRASSE", 7, &eq),
                      RoboOk);
    RoboCheckEqInt(eq, 1);
    RoboCheckEqInt(I18nSearchEqual(&m, "\xCE\xA3", 2, "\xCF\x83", 2, &eq), RoboOk); /* Σ/σ */
    RoboCheckEqInt(eq, 1);
    RoboCheckEqInt(I18nSearchFind(&m, "Hello WORLD", 11, "world", 5, &found, &hit),
                      RoboOk);
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt((int)hit.start, 6);
    RoboCheckEqInt((int)hit.end, 11);
    I18nSearcherDestroy(&m);
}

static void TestSearchDiacritic(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int eq = 0;
    int found = 0;

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchCanonical;
    o.ignoreDiacritics = 1;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchEqual(&m, "caf\xC3\xA9", 5, "cafe", 4, &eq), RoboOk);
    RoboCheckEqInt(eq, 1);
    RoboCheckEqInt(I18nSearchContains(&m, "na\xC3\xAFve", 6, "naive", 5, &found),
                      RoboOk);
    RoboCheckEqInt(found, 1);
    I18nSearcherDestroy(&m);
}

static void TestSearchCollation(void)
{
    I18nLocale loc;
    I18nSearcher m;
    I18nSearchOptions o;
    int eq = 0;

    ParseTag("en", &loc);
    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchCollation;
    o.collationStrength = RoboI18nCollationPrimary;
    RoboCheckEqInt(I18nSearcherInit(&m, &loc, &o), RoboOk);
    RoboCheckEqInt(I18nSearchEqual(&m, "resume", 6, "r\xC3\xA9sum\xC3\xA9", 8, &eq),
                      RoboOk);
    RoboCheckEqInt(eq, 1);
    RoboCheckEqInt(I18nSearchEqual(&m, "a", 1, "A", 1, &eq), RoboOk);
    RoboCheckEqInt(eq, 1);
    I18nSearcherDestroy(&m);
}

static void TestSearchIntlMatrix(void)
{
    static const char *tags[] = {"en", "de", "fr", "tr", "el", "ru", "ar", "fa",
                                 "ur", "he", "hi", "bn", "th", "zh", "ja", "ko"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nLocale loc;
        I18nSearcher m;
        I18nSearchOptions o;
        int found = 0;
        ParseTag(tags[i], &loc);
        I18nSearchOptionsInit(&o);
        o.mode = RoboI18nSearchCasefold;
        RoboCheckEqInt(I18nSearcherInit(&m, &loc, &o), RoboOk);
        RoboCheckEqInt(I18nSearchContains(&m, "AbcXyz", 6, "xyz", 3, &found), RoboOk);
        RoboCheckEqInt(found, 1);
        I18nSearcherDestroy(&m);

        I18nSearchOptionsInit(&o);
        o.mode = RoboI18nSearchCollation;
        o.collationStrength = RoboI18nCollationPrimary;
        RoboCheckEqInt(I18nSearcherInit(&m, &loc, &o), RoboOk);
        RoboCheckEqInt(I18nSearchEqual(&m, "test", 4, "TEST", 4, &found), RoboOk);
        RoboCheckEqInt(found, 1);
        I18nSearcherDestroy(&m);
    }
}

static void TestSearchRtl(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int found = 0;
    I18nSearchHit hit;
    /* سلام — Arabic "salam" */
    const char *hay = "xx\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85yy";
    const char *needle = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchExact;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchFind(&m, hay, strlen(hay), needle, strlen(needle), &found,
                                           &hit),
                      RoboOk);
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt((int)hit.start, 2);
    I18nSearcherDestroy(&m);
}

static void TestSearchGrapheme(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int found = 0;
    I18nSearchHit hit;
    /* family emoji ZWJ sequence */
    const char *emoji = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    char hay[64];
    size_t n;

    memcpy(hay, "a", 1);
    memcpy(hay + 1, emoji, strlen(emoji));
    memcpy(hay + 1 + strlen(emoji), "b", 1);
    n = 1 + strlen(emoji) + 1;

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchExact;
    o.graphemeSafe = 1;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchFind(&m, hay, n, emoji, strlen(emoji), &found, &hit),
                      RoboOk);
    RoboCheckEqInt(found, 1);
    RoboCheckEqInt((int)hit.start, 1);
    RoboCheck(hit.graphemeEnd > hit.graphemeStart);
    I18nSearcherDestroy(&m);
}

static void TestSearchFindAll(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    I18nSearchHit hits[8];
    size_t count = 0;

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchCasefold;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    RoboCheckEqInt(I18nSearchFindAll(&m, "aBaBa", 5, "a", 1, hits, 8, &count), RoboOk);
    RoboCheckEqInt((int)count, 3);
    RoboCheckEqInt((int)hits[0].start, 0);
    RoboCheckEqInt((int)hits[1].start, 2);
    RoboCheckEqInt((int)hits[2].start, 4);
    I18nSearcherDestroy(&m);
}

static void TestSearchFuzz(void)
{
    I18nSearcher m;
    I18nSearchOptions o;
    int found = 0;
    char longbuf[70000];
    size_t i;

    I18nSearchOptionsInit(&o);
    o.mode = RoboI18nSearchCasefold;
    RoboCheckEqInt(I18nSearcherInit(&m, NULL, &o), RoboOk);
    (void)I18nSearchContains(&m, "\xFF\xFE", 2, "a", 1, &found);
    (void)I18nSearchContains(&m, "e\xCC\x81\xCC\x81\xCC\x81", 7, "é", 2, &found);
    for (i = 0; i < sizeof(longbuf); i++) {
        longbuf[i] = 'a';
    }
    RoboCheckEqInt(I18nSearchContains(&m, longbuf, sizeof(longbuf), "a", 1, &found),
                      RoboInvalidArgument);
    I18nSearcherDestroy(&m);
}

void TestRegisterI18nSearch(void)
{
    TestRegister("i18n.search.versions", TestSearchVersions);
    TestRegister("i18n.search.exact", TestSearchExact);
    TestRegister("i18n.search.canonical", TestSearchCanonical);
    TestRegister("i18n.search.casefold", TestSearchCasefold);
    TestRegister("i18n.search.diacritic", TestSearchDiacritic);
    TestRegister("i18n.search.collation", TestSearchCollation);
    TestRegister("i18n.search.intl", TestSearchIntlMatrix);
    TestRegister("i18n.search.rtl", TestSearchRtl);
    TestRegister("i18n.search.grapheme", TestSearchGrapheme);
    TestRegister("i18n.search.find_all", TestSearchFindAll);
    TestRegister("i18n.search.fuzz", TestSearchFuzz);
}
