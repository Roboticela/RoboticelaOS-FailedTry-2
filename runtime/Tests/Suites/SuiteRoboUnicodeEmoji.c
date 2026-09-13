/*
 * RoboUnicode Emoji properties & RGI sequences (Goal 3.13 / UTS #51).
 */

#include "RoboUnicode/RoboUnicodeEmoji.h"
#include "RoboUnicode/RoboUnicodeGrapheme.h"
#include "RoboTest.h"

#include "../Data/robo_emoji_conformance.h"

#ifdef ROBO_EMOJI_CONF_PATH_FROM_BUILD
#include "robo_emoji_conf_path.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ROBO_EMOJI_CONF_PATH
#define ROBO_EMOJI_CONF_PATH ""
#endif

static uint32_t rd_u32(const unsigned char **p, const unsigned char *end)
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

static uint16_t rd_u16(const unsigned char **p, const unsigned char *end)
{
    uint16_t v;
    if (*p + 2 > end) {
        return 0;
    }
    v = (uint16_t)((uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8));
    *p += 2;
    return v;
}

static unsigned char *load_conf(size_t *out_len)
{
    FILE *f;
    long sz;
    unsigned char *buf;
    const char *path = ROBO_EMOJI_CONF_PATH;

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
    *out_len = (size_t)sz;
    return buf;
}

static void test_emoji_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_emoji_ucd_version_string(), "16.0.0");
    ROBO_CHECK_STR_EQ(robo_unicode_emoji_version_string(), "16.0");
}

static void test_emoji_properties(void)
{
    ROBO_CHECK(robo_unicode_is_emoji(0x1F600u));
    ROBO_CHECK(robo_unicode_is_emoji_presentation(0x1F600u));
    ROBO_CHECK(robo_unicode_is_extended_pictographic(0x1F600u));
    ROBO_CHECK(!robo_unicode_is_emoji('A'));

    ROBO_CHECK(robo_unicode_is_emoji_modifier(0x1F3FBu));
    ROBO_CHECK(robo_unicode_is_emoji_modifier_base(0x1F44Du)); /* thumbs up */
    ROBO_CHECK(robo_unicode_is_emoji_component(0x1F3FBu));
    ROBO_CHECK(robo_unicode_is_regional_indicator(0x1F1E6u));
    ROBO_CHECK(robo_unicode_is_variation_selector(0xFE0Fu));
    ROBO_CHECK(robo_unicode_is_variation_selector(0xFE0Eu));
    ROBO_CHECK(!robo_unicode_is_regional_indicator(0x1F600u));
}

static void test_emoji_keycap_flag_modifier(void)
{
    /* Keycap: 1 FE0F 20E3 */
    robo_uchar32_t keycap[] = {0x0031u, 0xFE0Fu, 0x20E3u};
    /* Flag US: 1F1FA 1F1F8 */
    robo_uchar32_t flag[] = {0x1F1FAu, 0x1F1F8u};
    /* Thumbs up + medium skin tone */
    robo_uchar32_t mod[] = {0x1F44Du, 0x1F3FDu};

    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(keycap, 3),
                      ROBO_EMOJI_SEQ_KEYCAP);
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(flag, 2),
                      ROBO_EMOJI_SEQ_FLAG);
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(mod, 2),
                      ROBO_EMOJI_SEQ_MODIFIER);

    /* Incomplete flag */
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(flag, 1),
                      ROBO_EMOJI_SEQ_NONE);
}

static void test_emoji_zwj_family(void)
{
    /* Man + ZWJ + boy */
    robo_uchar32_t fam[] = {0x1F468u, 0x200Du, 0x1F466u};
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(fam, 3),
                      ROBO_EMOJI_SEQ_ZWJ);
    /* Trailing ZWJ alone incomplete */
    {
        robo_uchar32_t bad[] = {0x1F468u, 0x200Du};
        ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(bad, 2),
                          ROBO_EMOJI_SEQ_NONE);
    }
}

static void test_emoji_utf8_match_next(void)
{
    /* Grinning face U+1F600 as UTF-8 */
    const char *grin = "\xF0\x9F\x98\x80";
    size_t end = 0;
    size_t start = 0;
    robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;

    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_match(
                          grin, 4, 0, ROBO_UNICODE_STRICT, &end, &kind),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_EMOJI_SEQ_BASIC);
    ROBO_CHECK_EQ_INT(end, 4u);

    {
        const char *s = "Hi\xF0\x9F\x98\x80!";
        ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_next(
                              s, strlen(s), 0, ROBO_UNICODE_STRICT, &start, &end, &kind),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(start, 2u);
        ROBO_CHECK_EQ_INT(end, 6u);
        ROBO_CHECK_EQ_INT(kind, ROBO_EMOJI_SEQ_BASIC);
    }

    {
        const char bad[] = {(char)0x80, 'A'};
        ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_match(
                              bad, 2, 0, ROBO_UNICODE_STRICT, &end, &kind),
                          ROBO_PARSE_ERROR);
    }
}

static void test_emoji_vs_grapheme(void)
{
    /* Flag is often one grapheme cluster (RI×RI) and one FLAG sequence */
    const char *us = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"; /* 🇺🇸 */
    size_t gcount = 0;
    size_t end = 0;
    robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;

    ROBO_CHECK_EQ_INT(robo_unicode_grapheme_count_utf8(
                          us, strlen(us), ROBO_UNICODE_STRICT, &gcount),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(gcount, 1u);
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_match(
                          us, strlen(us), 0, ROBO_UNICODE_STRICT, &end, &kind),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(kind, ROBO_EMOJI_SEQ_FLAG);

    /* Text vs emoji presentation (VS15 / VS16) — property/sequence only */
    {
        robo_uchar32_t text_pres[] = {0x2764u, 0xFE0Eu}; /* ❤︎ */
        robo_uchar32_t emoji_pres[] = {0x2764u, 0xFE0Fu}; /* ❤️ */
        ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(emoji_pres, 2),
                          ROBO_EMOJI_SEQ_BASIC);
        /* Text-style heart may or may not be RGI Basic; must not crash */
        (void)robo_unicode_emoji_sequence_classify(text_pres, 2);
        ROBO_CHECK(robo_unicode_is_variation_selector(0xFE0Eu));
        ROBO_CHECK(robo_unicode_is_variation_selector(0xFE0Fu));
    }
}

static void test_emoji_tag_sequence(void)
{
    /* Scotland flag: 🏴 + tag [g b s c t] + cancel */
    robo_uchar32_t sct[] = {
        0x1F3F4u, 0xE0067u, 0xE0062u, 0xE0073u, 0xE0063u, 0xE0074u, 0xE007Fu};
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(sct, 7),
                      ROBO_EMOJI_SEQ_TAG);
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(sct, 3),
                      ROBO_EMOJI_SEQ_NONE);
}

static void test_emoji_international_mix(void)
{
    size_t start = 0;
    size_t end = 0;
    robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;
    const char *grin = "\xF0\x9F\x98\x80"; /* U+1F600 */

    /* Arabic + emoji + Latin */
    {
        const char *s =
            "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 " /* مرحبا */
            "\xF0\x9F\x98\x80"
            " hi";
        ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_next(
                              s, strlen(s), 0, ROBO_UNICODE_STRICT, &start, &end, &kind),
                          ROBO_OK);
        ROBO_CHECK(kind == ROBO_EMOJI_SEQ_BASIC);
        ROBO_CHECK(start > 0u);
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
            ROBO_CHECK(n + 4u < sizeof(buf));
            memcpy(buf, mix[i], n);
            memcpy(buf + n, grin, 4);
            buf[n + 4] = '\0';
            ROBO_CHECK_EQ_INT(
                robo_unicode_emoji_sequence_next(
                    buf, n + 4u, 0, ROBO_UNICODE_STRICT, &start, &end, &kind),
                ROBO_OK);
            ROBO_CHECK_EQ_INT(kind, ROBO_EMOJI_SEQ_BASIC);
            ROBO_CHECK_EQ_INT(start, n);
            ROBO_CHECK_EQ_INT(end, n + 4u);
        }
    }
}

static void test_emoji_negative(void)
{
    robo_uchar32_t text[] = {'H', 'i'};
    size_t end = 0;
    robo_unicode_emoji_seq_kind_t kind = ROBO_EMOJI_SEQ_NONE;

    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(text, 2),
                      ROBO_EMOJI_SEQ_NONE);
    ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_classify(NULL, 0),
                      ROBO_EMOJI_SEQ_NONE);

    /* Truncated / overlong / surrogate-style UTF-8 — STRICT → PARSE_ERROR */
    {
        const char trunc[] = {(char)0xF0, (char)0x9F}; /* truncated 4-byte */
        const char overlong[] = {(char)0xC0, (char)0xAF};
        ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_match(
                              trunc, 2, 0, ROBO_UNICODE_STRICT, &end, &kind),
                          ROBO_PARSE_ERROR);
        ROBO_CHECK_EQ_INT(robo_unicode_emoji_sequence_match(
                              overlong, 2, 0, ROBO_UNICODE_STRICT, &end, &kind),
                          ROBO_PARSE_ERROR);
    }
}

static void test_emoji_conformance(void)
{
    size_t raw_len = 0;
    unsigned char *raw = load_conf(&raw_len);
    const unsigned char *p;
    const unsigned char *end;
    uint32_t count;
    uint32_t i;
    size_t fail = 0;
    const size_t max_report = 12u;
    robo_uchar32_t cps[32];

    ROBO_CHECK(raw != NULL);
    if (!raw) {
        return;
    }
    p = raw;
    end = raw + raw_len;
    ROBO_CHECK(p + 4 <= end && memcmp(p, "EMOJ", 4) == 0);
    p += 4;
    (void)rd_u16(&p, end);
    (void)rd_u16(&p, end);
    (void)rd_u16(&p, end);
    (void)rd_u16(&p, end);
    (void)rd_u16(&p, end);
    (void)rd_u16(&p, end);
    count = rd_u32(&p, end);
    ROBO_CHECK_EQ_INT(count, ROBO_EMOJI_CONF_CASE_COUNT);

    for (i = 0; i < count; i++) {
        uint16_t n = rd_u16(&p, end);
        uint8_t want = (uint8_t)(*p);
        uint8_t pad;
        uint16_t k;
        robo_unicode_emoji_seq_kind_t got;

        p++;
        pad = *p;
        p++;
        (void)pad;
        ROBO_CHECK(n <= 32u);
        for (k = 0; k < n; k++) {
            cps[k] = (robo_uchar32_t)rd_u32(&p, end);
        }
        got = robo_unicode_emoji_sequence_classify(cps, n);
        if ((uint8_t)got != want) {
            fail++;
            if (fail <= max_report) {
                robo_test_fail(__FILE__,
                               __LINE__,
                               "emoji-test case %u n=%u got=%u want=%u",
                               (unsigned)i,
                               (unsigned)n,
                               (unsigned)got,
                               (unsigned)want);
            }
        }
    }
    ROBO_CHECK(p == end);
    free(raw);
    ROBO_CHECK_EQ_INT(fail, 0);
}

void robo_test_register_unicode_emoji(void)
{
    robo_test_register("emoji.version", test_emoji_version);
    robo_test_register("emoji.properties", test_emoji_properties);
    robo_test_register("emoji.keycap_flag_modifier", test_emoji_keycap_flag_modifier);
    robo_test_register("emoji.zwj_family", test_emoji_zwj_family);
    robo_test_register("emoji.utf8_match_next", test_emoji_utf8_match_next);
    robo_test_register("emoji.vs_grapheme", test_emoji_vs_grapheme);
    robo_test_register("emoji.tag_sequence", test_emoji_tag_sequence);
    robo_test_register("emoji.international_mix", test_emoji_international_mix);
    robo_test_register("emoji.negative", test_emoji_negative);
    robo_test_register("emoji.conformance", test_emoji_conformance);
}
