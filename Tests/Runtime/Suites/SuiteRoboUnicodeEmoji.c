/*
 * RoboUnicode Emoji properties & RGI sequences (Goal 3.13 / UTS #51).
 */

#include "RoboUnicode/RoboUnicodeEmoji.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"
#include "RoboTest.h"

#include "../Data/robo_emoji_conformance.h"

#ifdef RoboEmojiConfPathFromBuild
#include "robo_emoji_conf_path.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RoboEmojiConfPath
#define RoboEmojiConfPath ""
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
    const char *path = RoboEmojiConfPath;

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
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *outLen = (size_t)sz;
    return buf;
}

static void TestEmojiVersion(void)
{
    RoboCheckStrEq(UnicodeEmojiUcdVersionString(), "16.0.0");
    RoboCheckStrEq(UnicodeEmojiVersionString(), "16.0");
}

static void TestEmojiProperties(void)
{
    RoboCheck(UnicodeIsEmoji(0x1F600u));
    RoboCheck(UnicodeIsEmojiPresentation(0x1F600u));
    RoboCheck(UnicodeIsExtendedPictographic(0x1F600u));
    RoboCheck(!UnicodeIsEmoji('A'));

    RoboCheck(UnicodeIsEmojiModifier(0x1F3FBu));
    RoboCheck(UnicodeIsEmojiModifierBase(0x1F44Du)); /* thumbs up */
    RoboCheck(UnicodeIsEmojiComponent(0x1F3FBu));
    RoboCheck(UnicodeIsRegionalIndicator(0x1F1E6u));
    RoboCheck(UnicodeIsVariationSelector(0xFE0Fu));
    RoboCheck(UnicodeIsVariationSelector(0xFE0Eu));
    RoboCheck(!UnicodeIsRegionalIndicator(0x1F600u));
}

static void TestEmojiKeycapFlagModifier(void)
{
    /* Keycap: 1 FE0F 20E3 */
    Uchar32 keycap[] = {0x0031u, 0xFE0Fu, 0x20E3u};
    /* Flag US: 1F1FA 1F1F8 */
    Uchar32 flag[] = {0x1F1FAu, 0x1F1F8u};
    /* Thumbs up + medium skin tone */
    Uchar32 mod[] = {0x1F44Du, 0x1F3FDu};

    RoboCheckEqInt(UnicodeEmojiSequenceClassify(keycap, 3),
                      RoboEmojiSeqKeycap);
    RoboCheckEqInt(UnicodeEmojiSequenceClassify(flag, 2),
                      RoboEmojiSeqFlag);
    RoboCheckEqInt(UnicodeEmojiSequenceClassify(mod, 2),
                      RoboEmojiSeqModifier);

    /* Incomplete flag */
    RoboCheckEqInt(UnicodeEmojiSequenceClassify(flag, 1),
                      RoboEmojiSeqNone);
}

static void TestEmojiZwjFamily(void)
{
    /* Man + ZWJ + boy */
    Uchar32 fam[] = {0x1F468u, 0x200Du, 0x1F466u};
    RoboCheckEqInt(UnicodeEmojiSequenceClassify(fam, 3),
                      RoboEmojiSeqZwj);
    /* Trailing ZWJ alone incomplete */
    {
        Uchar32 bad[] = {0x1F468u, 0x200Du};
        RoboCheckEqInt(UnicodeEmojiSequenceClassify(bad, 2),
                          RoboEmojiSeqNone);
    }
}

static void TestEmojiUtf8MatchNext(void)
{
    /* Grinning face U+1F600 as UTF-8 */
    const char *grin = "\xF0\x9F\x98\x80";
    size_t end = 0;
    size_t start = 0;
    UnicodeEmojiSeqKind kind = RoboEmojiSeqNone;

    RoboCheckEqInt(UnicodeEmojiSequenceMatch(
                          grin, 4, 0, RoboUnicodeStrict, &end, &kind),
                      RoboOk);
    RoboCheckEqInt(kind, RoboEmojiSeqBasic);
    RoboCheckEqInt(end, 4u);

    {
        const char *s = "Hi\xF0\x9F\x98\x80!";
        RoboCheckEqInt(UnicodeEmojiSequenceNext(
                              s, strlen(s), 0, RoboUnicodeStrict, &start, &end, &kind),
                          RoboOk);
        RoboCheckEqInt(start, 2u);
        RoboCheckEqInt(end, 6u);
        RoboCheckEqInt(kind, RoboEmojiSeqBasic);
    }

    {
        const char bad[] = {(char)0x80, 'A'};
        RoboCheckEqInt(UnicodeEmojiSequenceMatch(
                              bad, 2, 0, RoboUnicodeStrict, &end, &kind),
                          RoboParseError);
    }
}

static void TestEmojiVsGrapheme(void)
{
    /* Flag is often one grapheme cluster (RI×RI) and one FLAG sequence */
    const char *us = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"; /* 🇺🇸 */
    size_t gcount = 0;
    size_t end = 0;
    UnicodeEmojiSeqKind kind = RoboEmojiSeqNone;

    RoboCheckEqInt(UnicodeGraphemeCountUtf8(
                          us, strlen(us), RoboUnicodeStrict, &gcount),
                      RoboOk);
    RoboCheckEqInt(gcount, 1u);
    RoboCheckEqInt(UnicodeEmojiSequenceMatch(
                          us, strlen(us), 0, RoboUnicodeStrict, &end, &kind),
                      RoboOk);
    RoboCheckEqInt(kind, RoboEmojiSeqFlag);

    /* Text vs emoji presentation (VS15 / VS16) — property/sequence only */
    {
        Uchar32 textPres[] = {0x2764u, 0xFE0Eu}; /* ❤︎ */
        Uchar32 emojiPres[] = {0x2764u, 0xFE0Fu}; /* ❤️ */
        RoboCheckEqInt(UnicodeEmojiSequenceClassify(emojiPres, 2),
                          RoboEmojiSeqBasic);
        /* Text-style heart may or may not be RGI Basic; must not crash */
        (void)UnicodeEmojiSequenceClassify(textPres, 2);
        RoboCheck(UnicodeIsVariationSelector(0xFE0Eu));
        RoboCheck(UnicodeIsVariationSelector(0xFE0Fu));
    }
}

static void TestEmojiTagSequence(void)
{
    /* Scotland flag: 🏴 + tag [g b s c t] + cancel */
    Uchar32 sct[] = {
        0x1F3F4u, 0xE0067u, 0xE0062u, 0xE0073u, 0xE0063u, 0xE0074u, 0xE007Fu};
    RoboCheckEqInt(UnicodeEmojiSequenceClassify(sct, 7),
                      RoboEmojiSeqTag);
    RoboCheckEqInt(UnicodeEmojiSequenceClassify(sct, 3),
                      RoboEmojiSeqNone);
}

static void TestEmojiInternationalMix(void)
{
    size_t start = 0;
    size_t end = 0;
    UnicodeEmojiSeqKind kind = RoboEmojiSeqNone;
    const char *grin = "\xF0\x9F\x98\x80"; /* U+1F600 */

    /* Arabic + emoji + Latin */
    {
        const char *s =
            "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 " /* مرحبا */
            "\xF0\x9F\x98\x80"
            " hi";
        RoboCheckEqInt(UnicodeEmojiSequenceNext(
                              s, strlen(s), 0, RoboUnicodeStrict, &start, &end, &kind),
                          RoboOk);
        RoboCheck(kind == RoboEmojiSeqBasic);
        RoboCheck(start > 0u);
    }

    /* Urdu / Persian / Hebrew / Devanagari / Bengali / Thai / CJK — scan finds grin */
    {
        const char *mix[] = {
            "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88",           /* اردو */
            "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C", /* فارسی */
            "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D",           /* שלום */
            "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80",
            "\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0\xA6\xBE",
            "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2",
            "\xE4\xB8\xAD\xE6\x96\x87\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xED\x95\x9C",
        };
        size_t i;
        for (i = 0; i < sizeof(mix) / sizeof(mix[0]); i++) {
            char buf[128];
            size_t n = strlen(mix[i]);
            RoboCheck(n + 4u < sizeof(buf));
            memcpy(buf, mix[i], n);
            memcpy(buf + n, grin, 4);
            buf[n + 4] = '\0';
            RoboCheckEqInt(
                UnicodeEmojiSequenceNext(
                    buf, n + 4u, 0, RoboUnicodeStrict, &start, &end, &kind),
                RoboOk);
            RoboCheckEqInt(kind, RoboEmojiSeqBasic);
            RoboCheckEqInt(start, n);
            RoboCheckEqInt(end, n + 4u);
        }
    }
}

static void TestEmojiNegative(void)
{
    Uchar32 text[] = {'H', 'i'};
    size_t end = 0;
    UnicodeEmojiSeqKind kind = RoboEmojiSeqNone;

    RoboCheckEqInt(UnicodeEmojiSequenceClassify(text, 2),
                      RoboEmojiSeqNone);
    RoboCheckEqInt(UnicodeEmojiSequenceClassify(NULL, 0),
                      RoboEmojiSeqNone);

    /* Truncated / overlong / surrogate-style UTF-8 — STRICT → PARSE_ERROR */
    {
        const char trunc[] = {(char)0xF0, (char)0x9F}; /* truncated 4-byte */
        const char overlong[] = {(char)0xC0, (char)0xAF};
        RoboCheckEqInt(UnicodeEmojiSequenceMatch(
                              trunc, 2, 0, RoboUnicodeStrict, &end, &kind),
                          RoboParseError);
        RoboCheckEqInt(UnicodeEmojiSequenceMatch(
                              overlong, 2, 0, RoboUnicodeStrict, &end, &kind),
                          RoboParseError);
    }
}

static void TestEmojiConformance(void)
{
    size_t rawLen = 0;
    unsigned char *raw = LoadConf(&rawLen);
    const unsigned char *p;
    const unsigned char *end;
    uint32_t count;
    uint32_t i;
    size_t fail = 0;
    const size_t maxReport = 12u;
    Uchar32 cps[32];

    RoboCheck(raw != NULL);
    if (!raw) {
        return;
    }
    p = raw;
    end = raw + rawLen;
    RoboCheck(p + 4 <= end && memcmp(p, "EMOJ", 4) == 0);
    p += 4;
    (void)RoboRdU16(&p, end);
    (void)RoboRdU16(&p, end);
    (void)RoboRdU16(&p, end);
    (void)RoboRdU16(&p, end);
    (void)RoboRdU16(&p, end);
    (void)RoboRdU16(&p, end);
    count = RoboRdU32(&p, end);
    RoboCheckEqInt(count, RoboEmojiConfCaseCount);

    for (i = 0; i < count; i++) {
        uint16_t n = RoboRdU16(&p, end);
        uint8_t want = (uint8_t)(*p);
        uint8_t pad;
        uint16_t k;
        UnicodeEmojiSeqKind got;

        p++;
        pad = *p;
        p++;
        (void)pad;
        RoboCheck(n <= 32u);
        for (k = 0; k < n; k++) {
            cps[k] = (Uchar32)RoboRdU32(&p, end);
        }
        got = UnicodeEmojiSequenceClassify(cps, n);
        if ((uint8_t)got != want) {
            fail++;
            if (fail <= maxReport) {
                TestFail(__FILE__,
                               __LINE__,
                               "emoji-test case %u n=%u got=%u want=%u",
                               (unsigned)i,
                               (unsigned)n,
                               (unsigned)got,
                               (unsigned)want);
            }
        }
    }
    RoboCheck(p == end);
    free(raw);
    RoboCheckEqInt(fail, 0);
}

void TestRegisterUnicodeEmoji(void)
{
    TestRegister("emoji.version", TestEmojiVersion);
    TestRegister("emoji.properties", TestEmojiProperties);
    TestRegister("emoji.keycap_flag_modifier", TestEmojiKeycapFlagModifier);
    TestRegister("emoji.zwj_family", TestEmojiZwjFamily);
    TestRegister("emoji.utf8_match_next", TestEmojiUtf8MatchNext);
    TestRegister("emoji.vs_grapheme", TestEmojiVsGrapheme);
    TestRegister("emoji.tag_sequence", TestEmojiTagSequence);
    TestRegister("emoji.international_mix", TestEmojiInternationalMix);
    TestRegister("emoji.negative", TestEmojiNegative);
    TestRegister("emoji.conformance", TestEmojiConformance);
}
