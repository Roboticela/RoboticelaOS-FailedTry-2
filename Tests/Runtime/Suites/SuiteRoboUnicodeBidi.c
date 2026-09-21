/*
 * RoboUnicode Bidirectional Algorithm suite (Goal 3.11 / UAX #9).
 *
 * Conformance data: Tests/Data/robo_bidi_character_conformance.bin
 * (path provided via RoboBidiConfPath from Build.ps1).
 *
 * Bidi ordering is not text shaping.
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeBidi.h"
#include "RoboTest.h"

#include "../Data/robo_bidi_character_conformance.h"

#ifdef RoboBidiConfPathFromBuild
#include "robo_bidi_conf_path.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RoboBidiConfPath
#define RoboBidiConfPath ""
#endif

static uint32_t RoboRdU32(const unsigned char **p, const unsigned char *end)
{
    uint32_t v;
    if (*p + 4 > end) {
        return 0;
    }
    v = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) | ((uint32_t)(*p)[2] << 16) |
        ((uint32_t)(*p)[3] << 24);
    *p += 4;
    return v;
}

static uint16_t RoboRdU16(const unsigned char **p, const unsigned char *end)
{
    uint16_t v;
    if (*p + 2 > end) {
        return 0;
    }
    v = (uint16_t)((uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8));
    *p += 2;
    return v;
}

static uint8_t RdU8(const unsigned char **p, const unsigned char *end)
{
    uint8_t v;
    if (*p >= end) {
        return 0;
    }
    v = **p;
    (*p)++;
    return v;
}

static unsigned char *BidiLoadConf(size_t *outLen)
{
    const char *path = RoboBidiConfPath;
    FILE *f;
    long sz;
    unsigned char *buf;

    if (!path || !path[0]) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *outLen = (size_t)sz;
    return buf;
}

static void TestBidiVersion(void)
{
    RoboCheckStrEq(UnicodeBidiUcdVersionString(), "16.0.0");
}

static void TestBidiAsciiLtr(void)
{
    Uchar32 cps[] = {'H', 'e', 'l', 'l', 'o'};
    UnicodeBidiResult r;
    size_t i;

    RoboCheckEqInt(UnicodeBidiAnalyze(cps, 5, RoboBidiAuto, &r), RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 0);
    RoboCheckEqInt(r.VisibleCount, 5u);
    for (i = 0; i < 5; i++) {
        RoboCheckEqInt(r.levels[i], 0);
        RoboCheckEqInt(r.visualToLogical[i], i);
    }
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiHebrew(void)
{
    /* שלום U+05E9 U+05DC U+05D5 U+05DD */
    Uchar32 cps[] = {0x05E9u, 0x05DCu, 0x05D5u, 0x05DDu};
    UnicodeBidiResult r;
    size_t i;

    RoboCheckEqInt(MakeUnicodeBidiClass(0x05E9u), RoboUBcR);
    RoboCheckEqInt(UnicodeBidiAnalyze(cps, 4, RoboBidiAuto, &r), RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 1);
    RoboCheckEqInt(r.VisibleCount, 4u);
    for (i = 0; i < 4; i++) {
        RoboCheckEqInt(r.levels[i], 1);
    }
    /* Visual order reverses at odd level */
    RoboCheckEqInt(r.visualToLogical[0], 3u);
    RoboCheckEqInt(r.visualToLogical[3], 0u);
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiArabic(void)
{
    /* العربية — sample letters */
    Uchar32 cps[] = {0x0627u, 0x0644u, 0x0639u, 0x0631u, 0x0628u, 0x064Au,
                            0x0629u};
    UnicodeBidiResult r;

    RoboCheckEqInt(MakeUnicodeBidiClass(0x0627u), RoboUBcAl);
    RoboCheckEqInt(UnicodeBidiAnalyze(cps, 7, RoboBidiAuto, &r), RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 1);
    RoboCheck(r.VisibleCount == 7u);
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiPersianUrdu(void)
{
    /* فارسی (Persian) + اردو (Urdu) */
    const char *fa = "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C";
    const char *ur = "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88";
    UnicodeBidiResult r;

    RoboCheckEqInt(
        UnicodeBidiAnalyzeUtf8(fa, strlen(fa), RoboUnicodeStrict, RoboBidiAuto, &r),
        RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 1);
    UnicodeBidiResultDestroy(&r);

    RoboCheckEqInt(
        UnicodeBidiAnalyzeUtf8(ur, strlen(ur), RoboUnicodeStrict, RoboBidiAuto, &r),
        RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 1);
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiMixed(void)
{
    /* English العربية English */
    const char *s = "English \xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9 English";
    UnicodeBidiResult r;
    size_t i;
    int sawRtl = 0;
    int sawLtr = 0;

    RoboCheckEqInt(
        UnicodeBidiAnalyzeUtf8(s, strlen(s), RoboUnicodeStrict, RoboBidiLtr, &r),
        RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 0);
    for (i = 0; i < r.cpCount; i++) {
        if (r.levels[i] == RoboBidiLevelRemoved) {
            continue;
        }
        if ((r.levels[i] % 2u) == 1u) {
            sawRtl = 1;
        } else {
            sawLtr = 1;
        }
    }
    RoboCheck(sawRtl);
    RoboCheck(sawLtr);
    RoboCheck(r.byteEnds != NULL);
    RoboCheckEqInt(r.byteEnds[r.cpCount - 1u], strlen(s));
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiNumbers(void)
{
    /* Hebrew + European digits + Latin */
    Uchar32 heb[] = {0x05D0u, 0x05D1u, ' ', '1', '2', '3', ' ', 'A', 'B'};
    /* Arabic + Arabic-Indic digits */
    Uchar32 ar[] = {0x0627u, 0x0628u, ' ', 0x0661u, 0x0662u, 0x0663u};
    /* Extended Arabic-Indic (Persian) digits */
    Uchar32 fa[] = {0x0627u, ' ', 0x06F1u, 0x06F2u};
    UnicodeBidiResult r;

    RoboCheckEqInt(MakeUnicodeBidiClass('1'), RoboUBcEn);
    RoboCheckEqInt(MakeUnicodeBidiClass(0x0661u), RoboUBcAn);
    RoboCheckEqInt(MakeUnicodeBidiClass(0x06F1u), RoboUBcEn);

    RoboCheckEqInt(UnicodeBidiAnalyze(heb, 9, RoboBidiRtl, &r), RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 1);
    UnicodeBidiResultDestroy(&r);

    RoboCheckEqInt(UnicodeBidiAnalyze(ar, 6, RoboBidiRtl, &r), RoboOk);
    RoboCheckEqInt(r.paragraphLevel, 1);
    UnicodeBidiResultDestroy(&r);

    RoboCheckEqInt(UnicodeBidiAnalyze(fa, 4, RoboBidiRtl, &r), RoboOk);
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiPunctuationBrackets(void)
{
    /* English (العربية) text */
    const char *s =
        "English (\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9) text";
    UnicodeBidiResult r;

    RoboCheckEqInt(
        UnicodeBidiAnalyzeUtf8(s, strlen(s), RoboUnicodeStrict, RoboBidiLtr, &r),
        RoboOk);
    RoboCheck(r.VisibleCount >= 10u);
    UnicodeBidiResultDestroy(&r);

    {
        Uchar32 cps[] = {'(', 0x05D0u, ')', 'A'};
        RoboCheckEqInt(UnicodeBidiAnalyze(cps, 4, RoboBidiLtr, &r), RoboOk);
        RoboCheckEqInt(r.cpCount, 4u);
        UnicodeBidiResultDestroy(&r);
    }
}

static void TestBidiNsmCombining(void)
{
    /* a + combining acute */
    Uchar32 cps[] = {'a', 0x0301u};
    UnicodeBidiResult r;

    RoboCheckEqInt(MakeUnicodeBidiClass(0x0301u), RoboUBcNsm);
    RoboCheckEqInt(UnicodeBidiAnalyze(cps, 2, RoboBidiLtr, &r), RoboOk);
    RoboCheckEqInt(r.levels[0], 0);
    RoboCheckEqInt(r.levels[1], 0);
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiEmbeddingsOverrides(void)
{
    /* RLO a PDF → forced RTL letter */
    Uchar32 cps[] = {0x202Eu, 'a', 0x202Cu};
    UnicodeBidiResult r;

    RoboCheckEqInt(UnicodeBidiAnalyze(cps, 3, RoboBidiLtr, &r), RoboOk);
    RoboCheckEqInt(r.levels[0], RoboBidiLevelRemoved);
    RoboCheckEqInt(r.levels[2], RoboBidiLevelRemoved);
    RoboCheck((r.levels[1] % 2u) == 1u);
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiIsolates(void)
{
    /* LRI ... PDI and nested RLI */
    Uchar32 cps[] = {0x2066u, 'A', 0x2067u, 0x05D0u, 0x2069u, 'B', 0x2069u};
    UnicodeBidiResult r;

    RoboCheckEqInt(UnicodeBidiAnalyze(cps, 7, RoboBidiLtr, &r), RoboOk);
    RoboCheck(r.VisibleCount == 7u); /* isolates are not X9-removed */
    UnicodeBidiResultDestroy(&r);

    {
        /* FSI + Arabic + PDI inside LTR */
        Uchar32 fsi[] = {'A', 0x2068u, 0x0627u, 0x2069u, 'B'};
        RoboCheckEqInt(UnicodeBidiAnalyze(fsi, 5, RoboBidiLtr, &r), RoboOk);
        RoboCheckEqInt(r.paragraphLevel, 0);
        UnicodeBidiResultDestroy(&r);
    }
}

static void TestBidiParagraphs(void)
{
    /* Explicit LTR vs AUTO vs RTL */
    Uchar32 mixed[] = {'A', 0x05D0u};
    uint8_t levels[2];
    uint8_t para = 99;

    RoboCheckEqInt(UnicodeBidiParagraphDirection(mixed, 2), 0);
    {
        Uchar32 rtlFirst[] = {0x05D0u, 'A'};
        RoboCheckEqInt(UnicodeBidiParagraphDirection(rtlFirst, 2), 1);
    }

    RoboCheckEqInt(
        UnicodeBidiResolveLevels(mixed, 2, RoboBidiRtl, levels, &para), RoboOk);
    RoboCheckEqInt(para, 1);

    RoboCheckEqInt(
        UnicodeBidiResolveLevels(mixed, 2, RoboBidiLtr, levels, &para), RoboOk);
    RoboCheckEqInt(para, 0);
}

static void TestBidiEmptyLongInvalid(void)
{
    UnicodeBidiResult r;
    char longbuf[512];
    size_t i;
    const char bad[] = {(char)0x80, 'A'};

    RoboCheckEqInt(UnicodeBidiAnalyze(NULL, 0, RoboBidiAuto, &r), RoboOk);
    RoboCheckEqInt(r.cpCount, 0u);
    RoboCheckEqInt(r.VisibleCount, 0u);
    UnicodeBidiResultDestroy(&r);

    for (i = 0; i < sizeof(longbuf) - 1u; i++) {
        longbuf[i] = (char)('a' + (i % 26u));
    }
    longbuf[sizeof(longbuf) - 1u] = '\0';
    RoboCheckEqInt(UnicodeBidiAnalyzeUtf8(
                          longbuf, strlen(longbuf), RoboUnicodeStrict, RoboBidiLtr, &r),
                      RoboOk);
    RoboCheckEqInt(r.cpCount, strlen(longbuf));
    UnicodeBidiResultDestroy(&r);

    RoboCheckEqInt(UnicodeBidiAnalyzeUtf8(
                          bad, 2, RoboUnicodeStrict, RoboBidiAuto, &r),
                      RoboParseError);
    RoboCheckEqInt(UnicodeBidiAnalyzeUtf8(
                          bad, 2, RoboUnicodeReplace, RoboBidiAuto, &r),
                      RoboOk);
    RoboCheck(r.cpCount >= 1u);
    UnicodeBidiResultDestroy(&r);
}

static void TestBidiReorderApi(void)
{
    uint8_t levels[] = {0, 1, 1, 0};
    size_t visual[4];
    size_t vis = 0;

    RoboCheckEqInt(UnicodeBidiReorder(levels, 4, visual, &vis), RoboOk);
    RoboCheckEqInt(vis, 4u);
    RoboCheckEqInt(visual[1], 2u); /* odd run reversed */
    RoboCheckEqInt(visual[2], 1u);
}

static void TestBidiConformance(void)
{
    size_t rawLen = 0;
    unsigned char *raw = BidiLoadConf(&rawLen);
    const unsigned char *p;
    const unsigned char *end;
    uint32_t magic;
    uint16_t maj;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
    uint32_t caseCount;
    uint32_t ci;
    size_t fail = 0;
    const size_t maxReport = 12u;
    Uchar32 cps[256];
    uint8_t wantLevels[256];
    uint16_t wantReorder[256];
    uint8_t gotLevels[256];
    size_t gotVisual[256];
    size_t gotVis;
    uint8_t paraGot;

    RoboCheck(raw != NULL);
    if (!raw) {
        return;
    }
    p = raw;
    end = raw + rawLen;
    magic = RoboRdU32(&p, end);
    RoboCheckEqInt(magic, RoboBidiCharConfMagic);
    maj = RoboRdU16(&p, end);
    minor = RoboRdU16(&p, end);
    patch = RoboRdU16(&p, end);
    reserved = RoboRdU16(&p, end);
    (void)reserved;
    RoboCheckEqInt(maj, 16);
    RoboCheckEqInt(minor, 0);
    RoboCheckEqInt(patch, 0);
    caseCount = RoboRdU32(&p, end);
    RoboCheckEqInt(caseCount, RoboBidiCharConfCaseCount);

    for (ci = 0; ci < caseCount; ci++) {
        uint16_t n;
        uint16_t rn;
        uint8_t paraDir;
        uint8_t paraWant;
        uint16_t pad;
        uint16_t i;
        UnicodeBidiParagraphDir dir;
        Result rc;
        int caseFail = 0;

        n = RoboRdU16(&p, end);
        rn = RoboRdU16(&p, end);
        paraDir = RdU8(&p, end);
        paraWant = RdU8(&p, end);
        pad = RoboRdU16(&p, end);
        (void)pad;
        RoboCheck(n <= 256u);
        RoboCheck(rn <= 256u);
        if (n > 256u || rn > 256u) {
            free(raw);
            return;
        }
        for (i = 0; i < n; i++) {
            cps[i] = (Uchar32)RoboRdU32(&p, end);
        }
        for (i = 0; i < n; i++) {
            wantLevels[i] = RdU8(&p, end);
        }
        if ((n % 2u) == 1u) {
            (void)RdU8(&p, end);
        }
        for (i = 0; i < rn; i++) {
            wantReorder[i] = RoboRdU16(&p, end);
        }

        if (paraDir == 0u) {
            dir = RoboBidiLtr;
        } else if (paraDir == 1u) {
            dir = RoboBidiRtl;
        } else {
            dir = RoboBidiAuto;
        }

        rc = UnicodeBidiResolveLevels(cps, n, dir, gotLevels, &paraGot);
        if (rc != RoboOk || paraGot != paraWant) {
            caseFail = 1;
        } else {
            for (i = 0; i < n; i++) {
                if (gotLevels[i] != wantLevels[i]) {
                    caseFail = 1;
                    break;
                }
            }
        }
        if (!caseFail) {
            rc = UnicodeBidiReorder(gotLevels, n, gotVisual, &gotVis);
            if (rc != RoboOk || gotVis != rn) {
                caseFail = 1;
            } else {
                for (i = 0; i < rn; i++) {
                    if (gotVisual[i] != (size_t)wantReorder[i]) {
                        caseFail = 1;
                        break;
                    }
                }
            }
        }
        if (caseFail) {
            fail++;
            if (fail <= maxReport) {
                TestFail(__FILE__,
                               __LINE__,
                               "BidiCharacterTest case %u n=%u para_dir=%u",
                               (unsigned)ci,
                               (unsigned)n,
                               (unsigned)paraDir);
            }
        }
    }

    RoboCheck(p == end);
    free(raw);
    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeBidi(void)
{
    TestRegister("bidi.version", TestBidiVersion);
    TestRegister("bidi.ascii_ltr", TestBidiAsciiLtr);
    TestRegister("bidi.hebrew", TestBidiHebrew);
    TestRegister("bidi.arabic", TestBidiArabic);
    TestRegister("bidi.persian_urdu", TestBidiPersianUrdu);
    TestRegister("bidi.mixed", TestBidiMixed);
    TestRegister("bidi.numbers", TestBidiNumbers);
    TestRegister("bidi.punctuation_brackets", TestBidiPunctuationBrackets);
    TestRegister("bidi.nsm_combining", TestBidiNsmCombining);
    TestRegister("bidi.embeddings_overrides", TestBidiEmbeddingsOverrides);
    TestRegister("bidi.isolates", TestBidiIsolates);
    TestRegister("bidi.paragraphs", TestBidiParagraphs);
    TestRegister("bidi.empty_long_invalid", TestBidiEmptyLongInvalid);
    TestRegister("bidi.reorder_api", TestBidiReorderApi);
    TestRegister("bidi.conformance", TestBidiConformance);
}
