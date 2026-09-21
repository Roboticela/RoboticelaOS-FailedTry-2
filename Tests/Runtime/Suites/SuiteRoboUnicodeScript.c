/*
 * RoboUnicode Script / Script_Extensions suite (Goal 3.12 / UAX #24).
 */

#include "RoboUnicode/RoboUnicodeProps.h"
#include "RoboTest.h"

#include "../Data/robo_script_conformance.h"

#ifdef RoboScriptConfPathFromBuild
#include "robo_script_conf_path.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RoboScriptConfPath
#define RoboScriptConfPath ""
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

static unsigned char *LoadConf(size_t *outLen)
{
    FILE *f;
    long sz;
    unsigned char *buf;
    const char *path = RoboScriptConfPath;

    if (!path || !path[0]) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
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

static void TestScriptNames(void)
{
    RoboCheckStrEq(UnicodeScriptName(RoboUScriptLatin), "Latin");
    RoboCheckStrEq(UnicodeScriptShortName(RoboUScriptLatin), "Latn");
    RoboCheckStrEq(UnicodeScriptShortName(RoboUScriptArabic), "Arab");
    RoboCheckStrEq(UnicodeScriptShortName(RoboUScriptCommon), "Zyyy");
    RoboCheckStrEq(UnicodeScriptShortName(RoboUScriptInherited), "Zinh");
    RoboCheckStrEq(UnicodeScriptShortName(RoboUScriptUnknown), "Zzzz");
    RoboCheckEqInt(UnicodeScriptFromName("Arabic"), RoboUScriptArabic);
    RoboCheckEqInt(UnicodeScriptFromName("Arab"), RoboUScriptArabic);
    RoboCheckEqInt(UnicodeScriptFromName("Qaai"), RoboUScriptInherited);
    RoboCheckEqInt(UnicodeScriptFromName("nope"), RoboUScriptUnknown);
}

static void TestScriptCatalogue(void)
{
    struct {
        Uchar32 cp;
        UnicodeScript sc;
    } samples[] = {
        {'A', RoboUScriptLatin},
        {0x03B1u, RoboUScriptGreek},
        {0x0430u, RoboUScriptCyrillic},
        {0x0561u, RoboUScriptArmenian},
        {0x10D0u, RoboUScriptGeorgian},
        {0x0627u, RoboUScriptArabic},
        {0x067Eu, RoboUScriptArabic}, /* Persian/Urdu peh */
        {0x06AFu, RoboUScriptArabic}, /* gaf */
        {0x05D0u, RoboUScriptHebrew},
        {0x0710u, RoboUScriptSyriac},
        {0x0780u, RoboUScriptThaana},
        {0x0905u, RoboUScriptDevanagari},
        {0x0985u, RoboUScriptBengali},
        {0x0A05u, RoboUScriptGurmukhi},
        {0x0A85u, RoboUScriptGujarati},
        {0x0B05u, RoboUScriptOriya},
        {0x0B85u, RoboUScriptTamil},
        {0x0C05u, RoboUScriptTelugu},
        {0x0C85u, RoboUScriptKannada},
        {0x0D05u, RoboUScriptMalayalam},
        {0x0D85u, RoboUScriptSinhala},
        {0x0E01u, RoboUScriptThai},
        {0x0E81u, RoboUScriptLao},
        {0x1780u, RoboUScriptKhmer},
        {0x1000u, RoboUScriptMyanmar},
        {0x0F40u, RoboUScriptTibetan},
        {0x1200u, RoboUScriptEthiopic},
        {0x4E2Du, RoboUScriptHan},
        {0x3042u, RoboUScriptHiragana},
        {0x30A2u, RoboUScriptKatakana},
        {0xAC00u, RoboUScriptHangul},
        /* Historical / less-common */
        {0x10300u, RoboUScriptOldItalic},
        {0x10400u, RoboUScriptDeseret},
        {0x10A00u, RoboUScriptKharoshthi},
        {0x13000u, RoboUScriptEgyptianHieroglyphs},
        /* Common / Inherited / Unknown */
        {'0', RoboUScriptCommon},
        {0x0301u, RoboUScriptInherited},
        {0x0378u, RoboUScriptUnknown},
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        RoboCheckEqInt(MakeUnicodeScript(samples[i].cp), samples[i].sc);
    }
}

static void TestScriptExtensions(void)
{
    UnicodeScript buf[32];
    size_t n;

    /* Latin letter: SCX == Script */
    n = UnicodeScriptExtensions('A', buf, 32);
    RoboCheckEqInt(n, 1u);
    RoboCheckEqInt(buf[0], RoboUScriptLatin);
    RoboCheck(UnicodeHasScript('A', RoboUScriptLatin));
    RoboCheck(!UnicodeHasScript('A', RoboUScriptArabic));

    /* U+0301 COMBINING ACUTE: Script=Inherited, SCX includes Latn Grek … */
    RoboCheckEqInt(MakeUnicodeScript(0x0301u), RoboUScriptInherited);
    n = UnicodeScriptExtensions(0x0301u, buf, 32);
    RoboCheck(n > 1u);
    RoboCheck(UnicodeHasScript(0x0301u, RoboUScriptLatin));
    RoboCheck(UnicodeHasScript(0x0301u, RoboUScriptGreek));
    RoboCheck(!UnicodeHasScript(0x0301u, RoboUScriptArabic));

    /* U+00B7 MIDDLE DOT — many scripts in SCX */
    RoboCheckEqInt(MakeUnicodeScript(0x00B7u), RoboUScriptCommon);
    n = UnicodeScriptExtensions(0x00B7u, buf, 32);
    RoboCheck(n > 5u);
    RoboCheck(UnicodeHasScript(0x00B7u, RoboUScriptLatin));
    RoboCheck(UnicodeHasScript(0x00B7u, RoboUScriptHan));
}

static void TestScriptRuns(void)
{
    /* Hello + Arabic alef + combining acute on e */
    Uchar32 cps[] = {'H', 'i', 0x0627u, 'e', 0x0301u};
    UnicodeScriptRun runs[8];
    size_t n = 0;

    RoboCheckEqInt(
        UnicodeScriptRuns(cps, 5, runs, 8, &n), RoboOk);
    RoboCheck(n >= 2u);
    RoboCheckEqInt(runs[0].script, RoboUScriptLatin);
    /* Inherited acute should continue Latin after 'e' under run heuristic */
}

static void TestScriptIndependentOfBidi(void)
{
    /* Script Arabic ≠ automatic AL for all; digits are Common + EN bidi */
    RoboCheckEqInt(MakeUnicodeScript(0x0660u), RoboUScriptArabic);
    RoboCheckEqInt(MakeUnicodeScript('1'), RoboUScriptCommon);
}

static void TestScriptConformance(void)
{
    size_t rawLen = 0;
    unsigned char *raw = LoadConf(&rawLen);
    const unsigned char *p;
    const unsigned char *end;
    uint32_t scxCount;
    uint32_t sampleCount;
    uint32_t i;
    size_t fail = 0;
    const size_t maxReport = 12u;

    RoboCheck(raw != NULL);
    if (!raw) {
        return;
    }
    p = raw;
    end = raw + rawLen;
    RoboCheck(p + 4 <= end && p[0] == 'S' && p[1] == 'C' && p[2] == 'R' &&
               p[3] == 'P');
    p += 4;
    RoboCheckEqInt(RoboRdU16(&p, end), RoboScriptConfVersionMajor);
    RoboCheckEqInt(RoboRdU16(&p, end), RoboScriptConfVersionMinor);
    RoboCheckEqInt(RoboRdU16(&p, end), RoboScriptConfVersionPatch);
    scxCount = RoboRdU32(&p, end);
    sampleCount = RoboRdU32(&p, end);
    RoboCheckEqInt(scxCount, RoboScriptConfScxRanges);
    RoboCheckEqInt(sampleCount, RoboScriptConfScriptSamples);

    for (i = 0; i < scxCount; i++) {
        uint32_t start = RoboRdU32(&p, end);
        uint32_t stop = RoboRdU32(&p, end);
        uint16_t cnt = RoboRdU16(&p, end);
        UnicodeScript want[64];
        uint16_t k;
        uint32_t cp;

        RoboCheck(cnt <= 64u);
        for (k = 0; k < cnt; k++) {
            want[k] = (UnicodeScript)RoboRdU16(&p, end);
        }
        for (cp = start; cp <= stop; cp++) {
            UnicodeScript got[64];
            size_t n = UnicodeScriptExtensions(cp, got, 64);
            int bad = 0;
            if (n != (size_t)cnt) {
                bad = 1;
            } else {
                for (k = 0; k < cnt; k++) {
                    if (got[k] != want[k]) {
                        bad = 1;
                        break;
                    }
                }
            }
            if (bad) {
                fail++;
                if (fail <= maxReport) {
                    TestFail(__FILE__,
                                   __LINE__,
                                   "SCX U+%04X n=%zu want=%u",
                                   (unsigned)cp,
                                   n,
                                   (unsigned)cnt);
                }
            }
        }
    }

    for (i = 0; i < sampleCount; i++) {
        uint32_t cp = RoboRdU32(&p, end);
        uint16_t want = RoboRdU16(&p, end);
        if ((uint16_t)MakeUnicodeScript(cp) != want) {
            fail++;
            if (fail <= maxReport) {
                TestFail(__FILE__,
                               __LINE__,
                               "Script U+%04X got=%u want=%u",
                               (unsigned)cp,
                               (unsigned)MakeUnicodeScript(cp),
                               (unsigned)want);
            }
        }
    }

    RoboCheck(p == end);
    free(raw);
    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeScript(void)
{
    TestRegister("script.names", TestScriptNames);
    TestRegister("script.catalogue", TestScriptCatalogue);
    TestRegister("script.extensions", TestScriptExtensions);
    TestRegister("script.runs", TestScriptRuns);
    TestRegister("script.vs_bidi", TestScriptIndependentOfBidi);
    TestRegister("script.conformance", TestScriptConformance);
}
