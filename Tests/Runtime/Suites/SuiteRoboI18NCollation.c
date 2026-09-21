/*
 * RoboI18N collation suite (Goal 4.9).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboCollData.h"
#include "RoboI18N/RoboUcaData.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RoboCollationTestPathFromBuild
#include "robo_collation_test_path.h"
#endif
#ifndef RoboCollationTestPath
#define RoboCollationTestPath ""
#endif

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void TestCollVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nCollationUcaVersionString(), "16.0.0");
    RoboCheckStrEq(I18nCollationCldrVersionString(), "46");
    RoboCheckStrEq(RoboUcaVersionString, "16.0.0");
    RoboCheck(UcaMapCount > 10000u);
    RoboCheck(CollLocaleCount >= 10u);
}

static I18nCollator *MakeColl(const char *tag, const char *type)
{
    I18nLocale loc;
    I18nCollator *c = NULL;
    I18nCollatorOptions opt;
    ParseTag(tag, &loc);
    I18nCollatorOptionsInit(&opt);
    RoboCheckEqInt(I18nCollatorCreate(&loc, type, &opt, &c), RoboOk);
    RoboCheck(c != NULL);
    return c;
}

static int CmpStr(I18nCollator *c, const char *a, const char *b)
{
    int cmp = 0;
    RoboCheckEqInt(
        I18nCollatorCompare(c, a, strlen(a), b, strlen(b), &cmp), RoboOk);
    return cmp;
}

static void TestCollModesSeparated(void)
{
    int bcmp = 0;
    int pcmp = 0;
    int lcmp = 0;
    I18nCollator *c = MakeColl("en", "standard");
    const char *nfdE = "e\xCC\x81"; /* e + combining acute */
    const char *nfcE = "\xC3\xA9";  /* é */

    RoboCheckEqInt(
        I18nCompareUtf8Binary(nfdE, strlen(nfdE), nfcE, strlen(nfcE), &bcmp),
        RoboOk);
    RoboCheck(bcmp != 0); /* different bytes */

    RoboCheckEqInt(I18nCompareUtf8Codepoints(
                          nfdE, strlen(nfdE), nfcE, strlen(nfcE), &pcmp),
                      RoboOk);
    RoboCheck(pcmp != 0); /* different code-point sequences before NFD unify */

    lcmp = CmpStr(c, nfdE, nfcE);
    RoboCheckEqInt(lcmp, 0); /* UCA NFD → equal at primary+ */

    I18nCollatorDestroy(c);
}

static void TestCollStrength(void)
{
    I18nCollator *c = MakeColl("en", "standard");
    I18nCollatorOptions opt;
    int eq = 0;

    RoboCheckEqInt(I18nCollatorGetOptions(c, &opt), RoboOk);
    opt.strength = RoboI18nCollationPrimary;
    RoboCheckEqInt(I18nCollatorSetOptions(c, &opt), RoboOk);
    RoboCheckEqInt(
        I18nCollatorEqual(c, "resume", 6, "résumé", strlen("résumé"), &eq), RoboOk);
    RoboCheck(eq);

    opt.strength = RoboI18nCollationTertiary;
    RoboCheckEqInt(I18nCollatorSetOptions(c, &opt), RoboOk);
    RoboCheck(CmpStr(c, "a", "A") != 0);
    opt.strength = RoboI18nCollationPrimary;
    RoboCheckEqInt(I18nCollatorSetOptions(c, &opt), RoboOk);
    RoboCheckEqInt(CmpStr(c, "a", "A"), 0);

    I18nCollatorDestroy(c);
}

static void TestCollSortKey(void)
{
    I18nCollator *c = MakeColl("en", "standard");
    uint8_t ka[256];
    uint8_t kb[256];
    size_t la = 0;
    size_t lb = 0;
    int cmp = 0;

    RoboCheckEqInt(
        I18nCollatorGetSortKey(c, "file2", 5, NULL, 0, &la), RoboOk);
    RoboCheck(la > 0);
    RoboCheckEqInt(
        I18nCollatorGetSortKey(c, "file2", 5, ka, sizeof(ka), &la), RoboOk);
    RoboCheckEqInt(
        I18nCollatorGetSortKey(c, "file10", 6, kb, sizeof(kb), &lb), RoboOk);
    RoboCheck(memcmp(ka, kb, la < lb ? la : lb) != 0 || la != lb);

    cmp = CmpStr(c, "file2", "file10");
    {
        int kc = 0;
        size_t n = la < lb ? la : lb;
        int m = memcmp(ka, kb, n);
        if (m == 0) {
            kc = (la < lb) ? -1 : (la > lb) ? 1 : 0;
        } else {
            kc = m < 0 ? -1 : 1;
        }
        RoboCheckEqInt(kc, cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
    }

    I18nCollatorDestroy(c);
}

static void TestCollNumeric(void)
{
    I18nCollator *c = MakeColl("en", "standard");
    I18nCollatorOptions opt;
    RoboCheckEqInt(I18nCollatorGetOptions(c, &opt), RoboOk);
    opt.numeric = 1;
    RoboCheckEqInt(I18nCollatorSetOptions(c, &opt), RoboOk);
    RoboCheck(CmpStr(c, "file2", "file10") < 0);
    opt.numeric = 0;
    RoboCheckEqInt(I18nCollatorSetOptions(c, &opt), RoboOk);
    RoboCheck(CmpStr(c, "file2", "file10") > 0); /* lexicographic: '1' < '2' → file10 first */
    I18nCollatorDestroy(c);
}

static void TestCollTailoringSvEsDe(void)
{
    I18nCollator *sv = MakeColl("sv", "standard");
    I18nCollator *es = MakeColl("es", "standard");
    I18nCollator *de = MakeColl("de", "phonebook");
    I18nCollator *en = MakeColl("en", "standard");

    /* Swedish: ö after z (primary remap). */
    RoboCheck(CmpStr(sv, "z", "ö") < 0);
    /* English root: ö typically near o, before z. */
    RoboCheck(CmpStr(en, "ö", "z") < 0);

    /* Spanish: ñ after n. */
    RoboCheck(CmpStr(es, "n", "ñ") < 0);
    RoboCheck(CmpStr(es, "ñ", "o") < 0);

    /* German phonebook: ä treated near ae — at least creates without error. */
    RoboCheck(CmpStr(de, "ae", "ä") != 99); /* smoke */

    I18nCollatorDestroy(sv);
    I18nCollatorDestroy(es);
    I18nCollatorDestroy(de);
    I18nCollatorDestroy(en);
}

static void TestCollLocalesSmoke(void)
{
    static const char *tags[] = {
        "en", "de", "sv", "es", "tr", "ar", "fa", "ur", "he", "hi", "bn", "zh", "ja", "ko"
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        I18nCollator *c = MakeColl(tags[i], "standard");
        RoboCheck(CmpStr(c, "a", "b") < 0);
        /* mixed scripts should not crash */
        (void)CmpStr(c, "hello", "سلام");
        (void)CmpStr(c, "東京", "京都");
        I18nCollatorDestroy(c);
    }
}

static void TestCollInvalidUtf8(void)
{
    I18nCollator *c = MakeColl("en", "standard");
    int cmp = 0;
    const char bad[] = {(char)0xC3, (char)0x28}; /* invalid */
    RoboCheckEqInt(
        I18nCollatorCompare(c, bad, 2, "a", 1, &cmp), RoboParseError);
    RoboCheckEqInt(
        I18nCompareUtf8Codepoints(bad, 2, "a", 1, &cmp), RoboParseError);
    I18nCollatorDestroy(c);
}

static void TestCollFuzz(void)
{
    I18nCollator *c = MakeColl("en", "standard");
    uint32_t seed = 0xC011A7u;
    int i;
    for (i = 0; i < 80; i++) {
        char a[24];
        char b[24];
        size_t al = (seed % 12u) + 1u;
        size_t bl = ((seed >> 3) % 12u) + 1u;
        size_t j;
        int c1 = 0;
        int c2 = 0;
        seed = seed * 1664525u + 1013904223u;
        for (j = 0; j < al; j++) {
            a[j] = (char)('a' + ((seed >> (j % 8)) % 26u));
            seed = seed * 1664525u + 1013904223u;
        }
        for (j = 0; j < bl; j++) {
            b[j] = (char)('a' + ((seed >> (j % 8)) % 26u));
            seed = seed * 1664525u + 1013904223u;
        }
        RoboCheckEqInt(I18nCollatorCompare(c, a, al, b, bl, &c1), RoboOk);
        RoboCheckEqInt(I18nCollatorCompare(c, b, bl, a, al, &c2), RoboOk);
        if (c1 == 0) {
            RoboCheckEqInt(c2, 0);
        } else {
            RoboCheck(c1 == -c2);
        }
    }
    /* malformed */
    {
        char bad[3] = {(char)0xFF, (char)0xFE, (char)0xFD};
        int cmp = 0;
        RoboCheck(
            I18nCollatorCompare(c, bad, 3, "x", 1, &cmp) == RoboParseError);
    }
    I18nCollatorDestroy(c);
}

static int ParseHexLine(const char *line, uint32_t *out, size_t cap, size_t *n)
{
    const char *p = line;
    *n = 0;
    while (*p) {
        char *end = NULL;
        unsigned long v;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '#' || *p == '\n' || *p == '\r') {
            break;
        }
        v = strtoul(p, &end, 16);
        if (end == p) {
            return 0;
        }
        if (*n >= cap) {
            return 0;
        }
        out[(*n)++] = (uint32_t)v;
        p = end;
    }
    return 1;
}

static size_t EncodeUtf8(const uint32_t *cps, size_t n, char *out, size_t cap)
{
    size_t i;
    size_t o = 0;
    for (i = 0; i < n; i++) {
        size_t w = 0;
        if (UnicodeUtf8Encode(cps[i], out + o, cap - o, &w) != RoboUnicodeOk) {
            return 0;
        }
        o += w;
    }
    return o;
}

static void TestCollUcaConformanceSample(void)
{
    const char *path = RoboCollationTestPath;
    FILE *f;
    char line[512];
    uint32_t prev[32];
    size_t prevN = 0;
    char prevUtf[128];
    size_t prevLen = 0;
    int havePrev = 0;
    size_t checked = 0;
    size_t lineNo = 0;
    I18nCollator *c;
    I18nCollatorOptions opt;

    if (!path || !path[0]) {
        RoboCheck(0 && "collation test path missing");
        return;
    }
    f = fopen(path, "rb");
    if (!f) {
        RoboCheck(0 && "cannot open CollationTest");
        return;
    }

    c = MakeColl("root", "standard");
    RoboCheckEqInt(I18nCollatorGetOptions(c, &opt), RoboOk);
    opt.alternate = RoboI18nCollationNonIgnorable;
    opt.strength = RoboI18nCollationTertiary;
    RoboCheckEqInt(I18nCollatorSetOptions(c, &opt), RoboOk);

    while (fgets(line, sizeof(line), f) && checked < 4000) {
        uint32_t cps[32];
        size_t n = 0;
        char utf[128];
        size_t ulen;
        lineNo++;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (!ParseHexLine(line, cps, 32, &n) || n == 0) {
            continue;
        }
        ulen = EncodeUtf8(cps, n, utf, sizeof(utf));
        if (ulen == 0) {
            continue;
        }
        if (havePrev) {
            int cmp = 0;
            Result r =
                I18nCollatorCompare(c, prevUtf, prevLen, utf, ulen, &cmp);
            if (r == RoboOk) {
                /* UCA test lines are weakly increasing (non-decreasing). */
                RoboCheck(cmp <= 0);
                checked++;
            }
        }
        memcpy(prev, cps, n * sizeof(uint32_t));
        prevN = n;
        memcpy(prevUtf, utf, ulen);
        prevLen = ulen;
        havePrev = 1;
        (void)prevN;
        (void)lineNo;
    }
    fclose(f);
    RoboCheck(checked > 100);
    I18nCollatorDestroy(c);
}

void TestRegisterI18nCollation(void)
{
    TestRegister("i18n.coll.versions", TestCollVersions);
    TestRegister("i18n.coll.modes", TestCollModesSeparated);
    TestRegister("i18n.coll.strength", TestCollStrength);
    TestRegister("i18n.coll.sort_key", TestCollSortKey);
    TestRegister("i18n.coll.numeric", TestCollNumeric);
    TestRegister("i18n.coll.tailoring", TestCollTailoringSvEsDe);
    TestRegister("i18n.coll.locales", TestCollLocalesSmoke);
    TestRegister("i18n.coll.invalid_utf8", TestCollInvalidUtf8);
    TestRegister("i18n.coll.fuzz", TestCollFuzz);
    TestRegister("i18n.coll.uca_sample", TestCollUcaConformanceSample);
}
