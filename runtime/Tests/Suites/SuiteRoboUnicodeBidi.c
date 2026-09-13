/*
 * RoboUnicode Bidirectional Algorithm suite (Goal 3.11 / UAX #9).
 *
 * Conformance data: Tests/Data/robo_bidi_character_conformance.bin
 * (path provided via ROBO_BIDI_CONF_PATH from Build.ps1).
 *
 * Bidi ordering is not text shaping.
 */

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeBidi.h"
#include "RoboTest.h"

#include "../Data/robo_bidi_character_conformance.h"

#ifdef ROBO_BIDI_CONF_PATH_FROM_BUILD
#include "robo_bidi_conf_path.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ROBO_BIDI_CONF_PATH
#define ROBO_BIDI_CONF_PATH ""
#endif

static uint32_t robo_rd_u32(const unsigned char **p, const unsigned char *end)
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

static uint16_t robo_rd_u16(const unsigned char **p, const unsigned char *end)
{
    uint16_t v;
    if (*p + 2 > end) {
        return 0;
    }
    v = (uint16_t)((uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8));
    *p += 2;
    return v;
}

static uint8_t robo_rd_u8(const unsigned char **p, const unsigned char *end)
{
    uint8_t v;
    if (*p >= end) {
        return 0;
    }
    v = **p;
    (*p)++;
    return v;
}

static unsigned char *robo_bidi_load_conf(size_t *out_len)
{
    const char *path = ROBO_BIDI_CONF_PATH;
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
    *out_len = (size_t)sz;
    return buf;
}

static void test_bidi_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_bidi_ucd_version_string(), "16.0.0");
}

static void test_bidi_ascii_ltr(void)
{
    robo_uchar32_t cps[] = {'H', 'e', 'l', 'l', 'o'};
    robo_unicode_bidi_result_t r;
    size_t i;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(cps, 5, ROBO_BIDI_AUTO, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 0);
    ROBO_CHECK_EQ_INT(r.visible_count, 5u);
    for (i = 0; i < 5; i++) {
        ROBO_CHECK_EQ_INT(r.levels[i], 0);
        ROBO_CHECK_EQ_INT(r.visual_to_logical[i], i);
    }
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_hebrew(void)
{
    /* שלום U+05E9 U+05DC U+05D5 U+05DD */
    robo_uchar32_t cps[] = {0x05E9u, 0x05DCu, 0x05D5u, 0x05DDu};
    robo_unicode_bidi_result_t r;
    size_t i;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_class(0x05E9u), ROBO_U_BC_R);
    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(cps, 4, ROBO_BIDI_AUTO, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 1);
    ROBO_CHECK_EQ_INT(r.visible_count, 4u);
    for (i = 0; i < 4; i++) {
        ROBO_CHECK_EQ_INT(r.levels[i], 1);
    }
    /* Visual order reverses at odd level */
    ROBO_CHECK_EQ_INT(r.visual_to_logical[0], 3u);
    ROBO_CHECK_EQ_INT(r.visual_to_logical[3], 0u);
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_arabic(void)
{
    /* العربية — sample letters */
    robo_uchar32_t cps[] = {0x0627u, 0x0644u, 0x0639u, 0x0631u, 0x0628u, 0x064Au,
                            0x0629u};
    robo_unicode_bidi_result_t r;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_class(0x0627u), ROBO_U_BC_AL);
    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(cps, 7, ROBO_BIDI_AUTO, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 1);
    ROBO_CHECK(r.visible_count == 7u);
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_persian_urdu(void)
{
    /* فارسی (Persian) + اردو (Urdu) */
    const char *fa = "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C";
    const char *ur = "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88";
    robo_unicode_bidi_result_t r;

    ROBO_CHECK_EQ_INT(
        robo_unicode_bidi_analyze_utf8(fa, strlen(fa), ROBO_UNICODE_STRICT, ROBO_BIDI_AUTO, &r),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 1);
    robo_unicode_bidi_result_destroy(&r);

    ROBO_CHECK_EQ_INT(
        robo_unicode_bidi_analyze_utf8(ur, strlen(ur), ROBO_UNICODE_STRICT, ROBO_BIDI_AUTO, &r),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 1);
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_mixed(void)
{
    /* English العربية English */
    const char *s = "English \xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9 English";
    robo_unicode_bidi_result_t r;
    size_t i;
    int saw_rtl = 0;
    int saw_ltr = 0;

    ROBO_CHECK_EQ_INT(
        robo_unicode_bidi_analyze_utf8(s, strlen(s), ROBO_UNICODE_STRICT, ROBO_BIDI_LTR, &r),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 0);
    for (i = 0; i < r.cp_count; i++) {
        if (r.levels[i] == ROBO_BIDI_LEVEL_REMOVED) {
            continue;
        }
        if ((r.levels[i] % 2u) == 1u) {
            saw_rtl = 1;
        } else {
            saw_ltr = 1;
        }
    }
    ROBO_CHECK(saw_rtl);
    ROBO_CHECK(saw_ltr);
    ROBO_CHECK(r.byte_ends != NULL);
    ROBO_CHECK_EQ_INT(r.byte_ends[r.cp_count - 1u], strlen(s));
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_numbers(void)
{
    /* Hebrew + European digits + Latin */
    robo_uchar32_t heb[] = {0x05D0u, 0x05D1u, ' ', '1', '2', '3', ' ', 'A', 'B'};
    /* Arabic + Arabic-Indic digits */
    robo_uchar32_t ar[] = {0x0627u, 0x0628u, ' ', 0x0661u, 0x0662u, 0x0663u};
    /* Extended Arabic-Indic (Persian) digits */
    robo_uchar32_t fa[] = {0x0627u, ' ', 0x06F1u, 0x06F2u};
    robo_unicode_bidi_result_t r;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_class('1'), ROBO_U_BC_EN);
    ROBO_CHECK_EQ_INT(robo_unicode_bidi_class(0x0661u), ROBO_U_BC_AN);
    ROBO_CHECK_EQ_INT(robo_unicode_bidi_class(0x06F1u), ROBO_U_BC_EN);

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(heb, 9, ROBO_BIDI_RTL, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 1);
    robo_unicode_bidi_result_destroy(&r);

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(ar, 6, ROBO_BIDI_RTL, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.paragraph_level, 1);
    robo_unicode_bidi_result_destroy(&r);

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(fa, 4, ROBO_BIDI_RTL, &r), ROBO_OK);
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_punctuation_brackets(void)
{
    /* English (العربية) text */
    const char *s =
        "English (\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9) text";
    robo_unicode_bidi_result_t r;

    ROBO_CHECK_EQ_INT(
        robo_unicode_bidi_analyze_utf8(s, strlen(s), ROBO_UNICODE_STRICT, ROBO_BIDI_LTR, &r),
        ROBO_OK);
    ROBO_CHECK(r.visible_count >= 10u);
    robo_unicode_bidi_result_destroy(&r);

    {
        robo_uchar32_t cps[] = {'(', 0x05D0u, ')', 'A'};
        ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(cps, 4, ROBO_BIDI_LTR, &r), ROBO_OK);
        ROBO_CHECK_EQ_INT(r.cp_count, 4u);
        robo_unicode_bidi_result_destroy(&r);
    }
}

static void test_bidi_nsm_combining(void)
{
    /* a + combining acute */
    robo_uchar32_t cps[] = {'a', 0x0301u};
    robo_unicode_bidi_result_t r;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_class(0x0301u), ROBO_U_BC_NSM);
    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(cps, 2, ROBO_BIDI_LTR, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.levels[0], 0);
    ROBO_CHECK_EQ_INT(r.levels[1], 0);
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_embeddings_overrides(void)
{
    /* RLO a PDF → forced RTL letter */
    robo_uchar32_t cps[] = {0x202Eu, 'a', 0x202Cu};
    robo_unicode_bidi_result_t r;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(cps, 3, ROBO_BIDI_LTR, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.levels[0], ROBO_BIDI_LEVEL_REMOVED);
    ROBO_CHECK_EQ_INT(r.levels[2], ROBO_BIDI_LEVEL_REMOVED);
    ROBO_CHECK((r.levels[1] % 2u) == 1u);
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_isolates(void)
{
    /* LRI ... PDI and nested RLI */
    robo_uchar32_t cps[] = {0x2066u, 'A', 0x2067u, 0x05D0u, 0x2069u, 'B', 0x2069u};
    robo_unicode_bidi_result_t r;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(cps, 7, ROBO_BIDI_LTR, &r), ROBO_OK);
    ROBO_CHECK(r.visible_count == 7u); /* isolates are not X9-removed */
    robo_unicode_bidi_result_destroy(&r);

    {
        /* FSI + Arabic + PDI inside LTR */
        robo_uchar32_t fsi[] = {'A', 0x2068u, 0x0627u, 0x2069u, 'B'};
        ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(fsi, 5, ROBO_BIDI_LTR, &r), ROBO_OK);
        ROBO_CHECK_EQ_INT(r.paragraph_level, 0);
        robo_unicode_bidi_result_destroy(&r);
    }
}

static void test_bidi_paragraphs(void)
{
    /* Explicit LTR vs AUTO vs RTL */
    robo_uchar32_t mixed[] = {'A', 0x05D0u};
    uint8_t levels[2];
    uint8_t para = 99;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_paragraph_direction(mixed, 2), 0);
    {
        robo_uchar32_t rtl_first[] = {0x05D0u, 'A'};
        ROBO_CHECK_EQ_INT(robo_unicode_bidi_paragraph_direction(rtl_first, 2), 1);
    }

    ROBO_CHECK_EQ_INT(
        robo_unicode_bidi_resolve_levels(mixed, 2, ROBO_BIDI_RTL, levels, &para), ROBO_OK);
    ROBO_CHECK_EQ_INT(para, 1);

    ROBO_CHECK_EQ_INT(
        robo_unicode_bidi_resolve_levels(mixed, 2, ROBO_BIDI_LTR, levels, &para), ROBO_OK);
    ROBO_CHECK_EQ_INT(para, 0);
}

static void test_bidi_empty_long_invalid(void)
{
    robo_unicode_bidi_result_t r;
    char longbuf[512];
    size_t i;
    const char bad[] = {(char)0x80, 'A'};

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze(NULL, 0, ROBO_BIDI_AUTO, &r), ROBO_OK);
    ROBO_CHECK_EQ_INT(r.cp_count, 0u);
    ROBO_CHECK_EQ_INT(r.visible_count, 0u);
    robo_unicode_bidi_result_destroy(&r);

    for (i = 0; i < sizeof(longbuf) - 1u; i++) {
        longbuf[i] = (char)('a' + (i % 26u));
    }
    longbuf[sizeof(longbuf) - 1u] = '\0';
    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze_utf8(
                          longbuf, strlen(longbuf), ROBO_UNICODE_STRICT, ROBO_BIDI_LTR, &r),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(r.cp_count, strlen(longbuf));
    robo_unicode_bidi_result_destroy(&r);

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze_utf8(
                          bad, 2, ROBO_UNICODE_STRICT, ROBO_BIDI_AUTO, &r),
                      ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(robo_unicode_bidi_analyze_utf8(
                          bad, 2, ROBO_UNICODE_REPLACE, ROBO_BIDI_AUTO, &r),
                      ROBO_OK);
    ROBO_CHECK(r.cp_count >= 1u);
    robo_unicode_bidi_result_destroy(&r);
}

static void test_bidi_reorder_api(void)
{
    uint8_t levels[] = {0, 1, 1, 0};
    size_t visual[4];
    size_t vis = 0;

    ROBO_CHECK_EQ_INT(robo_unicode_bidi_reorder(levels, 4, visual, &vis), ROBO_OK);
    ROBO_CHECK_EQ_INT(vis, 4u);
    ROBO_CHECK_EQ_INT(visual[1], 2u); /* odd run reversed */
    ROBO_CHECK_EQ_INT(visual[2], 1u);
}

static void test_bidi_conformance(void)
{
    size_t raw_len = 0;
    unsigned char *raw = robo_bidi_load_conf(&raw_len);
    const unsigned char *p;
    const unsigned char *end;
    uint32_t magic;
    uint16_t maj;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
    uint32_t case_count;
    uint32_t ci;
    size_t fail = 0;
    const size_t max_report = 12u;
    robo_uchar32_t cps[256];
    uint8_t want_levels[256];
    uint16_t want_reorder[256];
    uint8_t got_levels[256];
    size_t got_visual[256];
    size_t got_vis;
    uint8_t para_got;

    ROBO_CHECK(raw != NULL);
    if (!raw) {
        return;
    }
    p = raw;
    end = raw + raw_len;
    magic = robo_rd_u32(&p, end);
    ROBO_CHECK_EQ_INT(magic, ROBO_BIDI_CHAR_CONF_MAGIC);
    maj = robo_rd_u16(&p, end);
    minor = robo_rd_u16(&p, end);
    patch = robo_rd_u16(&p, end);
    reserved = robo_rd_u16(&p, end);
    (void)reserved;
    ROBO_CHECK_EQ_INT(maj, 16);
    ROBO_CHECK_EQ_INT(minor, 0);
    ROBO_CHECK_EQ_INT(patch, 0);
    case_count = robo_rd_u32(&p, end);
    ROBO_CHECK_EQ_INT(case_count, ROBO_BIDI_CHAR_CONF_CASE_COUNT);

    for (ci = 0; ci < case_count; ci++) {
        uint16_t n;
        uint16_t rn;
        uint8_t para_dir;
        uint8_t para_want;
        uint16_t pad;
        uint16_t i;
        robo_unicode_bidi_paragraph_dir_t dir;
        robo_result_t rc;
        int case_fail = 0;

        n = robo_rd_u16(&p, end);
        rn = robo_rd_u16(&p, end);
        para_dir = robo_rd_u8(&p, end);
        para_want = robo_rd_u8(&p, end);
        pad = robo_rd_u16(&p, end);
        (void)pad;
        ROBO_CHECK(n <= 256u);
        ROBO_CHECK(rn <= 256u);
        if (n > 256u || rn > 256u) {
            free(raw);
            return;
        }
        for (i = 0; i < n; i++) {
            cps[i] = (robo_uchar32_t)robo_rd_u32(&p, end);
        }
        for (i = 0; i < n; i++) {
            want_levels[i] = robo_rd_u8(&p, end);
        }
        if ((n % 2u) == 1u) {
            (void)robo_rd_u8(&p, end);
        }
        for (i = 0; i < rn; i++) {
            want_reorder[i] = robo_rd_u16(&p, end);
        }

        if (para_dir == 0u) {
            dir = ROBO_BIDI_LTR;
        } else if (para_dir == 1u) {
            dir = ROBO_BIDI_RTL;
        } else {
            dir = ROBO_BIDI_AUTO;
        }

        rc = robo_unicode_bidi_resolve_levels(cps, n, dir, got_levels, &para_got);
        if (rc != ROBO_OK || para_got != para_want) {
            case_fail = 1;
        } else {
            for (i = 0; i < n; i++) {
                if (got_levels[i] != want_levels[i]) {
                    case_fail = 1;
                    break;
                }
            }
        }
        if (!case_fail) {
            rc = robo_unicode_bidi_reorder(got_levels, n, got_visual, &got_vis);
            if (rc != ROBO_OK || got_vis != rn) {
                case_fail = 1;
            } else {
                for (i = 0; i < rn; i++) {
                    if (got_visual[i] != (size_t)want_reorder[i]) {
                        case_fail = 1;
                        break;
                    }
                }
            }
        }
        if (case_fail) {
            fail++;
            if (fail <= max_report) {
                robo_test_fail(__FILE__,
                               __LINE__,
                               "BidiCharacterTest case %u n=%u para_dir=%u",
                               (unsigned)ci,
                               (unsigned)n,
                               (unsigned)para_dir);
            }
        }
    }

    ROBO_CHECK(p == end);
    free(raw);
    ROBO_CHECK_EQ_INT(fail, 0);
}

void robo_test_register_unicode_bidi(void)
{
    robo_test_register("bidi.version", test_bidi_version);
    robo_test_register("bidi.ascii_ltr", test_bidi_ascii_ltr);
    robo_test_register("bidi.hebrew", test_bidi_hebrew);
    robo_test_register("bidi.arabic", test_bidi_arabic);
    robo_test_register("bidi.persian_urdu", test_bidi_persian_urdu);
    robo_test_register("bidi.mixed", test_bidi_mixed);
    robo_test_register("bidi.numbers", test_bidi_numbers);
    robo_test_register("bidi.punctuation_brackets", test_bidi_punctuation_brackets);
    robo_test_register("bidi.nsm_combining", test_bidi_nsm_combining);
    robo_test_register("bidi.embeddings_overrides", test_bidi_embeddings_overrides);
    robo_test_register("bidi.isolates", test_bidi_isolates);
    robo_test_register("bidi.paragraphs", test_bidi_paragraphs);
    robo_test_register("bidi.empty_long_invalid", test_bidi_empty_long_invalid);
    robo_test_register("bidi.reorder_api", test_bidi_reorder_api);
    robo_test_register("bidi.conformance", test_bidi_conformance);
}
