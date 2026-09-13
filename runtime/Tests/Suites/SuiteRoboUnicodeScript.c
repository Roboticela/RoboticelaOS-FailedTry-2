/*
 * RoboUnicode Script / Script_Extensions suite (Goal 3.12 / UAX #24).
 */

#include "RoboUnicode/RoboUnicodeProps.h"
#include "RoboTest.h"

#include "../Data/robo_script_conformance.h"

#ifdef ROBO_SCRIPT_CONF_PATH_FROM_BUILD
#include "robo_script_conf_path.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ROBO_SCRIPT_CONF_PATH
#define ROBO_SCRIPT_CONF_PATH ""
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
    const char *path = ROBO_SCRIPT_CONF_PATH;

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
    *out_len = (size_t)sz;
    return buf;
}

static void test_script_names(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_script_name(ROBO_U_SCRIPT_LATIN), "Latin");
    ROBO_CHECK_STR_EQ(robo_unicode_script_short_name(ROBO_U_SCRIPT_LATIN), "Latn");
    ROBO_CHECK_STR_EQ(robo_unicode_script_short_name(ROBO_U_SCRIPT_ARABIC), "Arab");
    ROBO_CHECK_STR_EQ(robo_unicode_script_short_name(ROBO_U_SCRIPT_COMMON), "Zyyy");
    ROBO_CHECK_STR_EQ(robo_unicode_script_short_name(ROBO_U_SCRIPT_INHERITED), "Zinh");
    ROBO_CHECK_STR_EQ(robo_unicode_script_short_name(ROBO_U_SCRIPT_UNKNOWN), "Zzzz");
    ROBO_CHECK_EQ_INT(robo_unicode_script_from_name("Arabic"), ROBO_U_SCRIPT_ARABIC);
    ROBO_CHECK_EQ_INT(robo_unicode_script_from_name("Arab"), ROBO_U_SCRIPT_ARABIC);
    ROBO_CHECK_EQ_INT(robo_unicode_script_from_name("Qaai"), ROBO_U_SCRIPT_INHERITED);
    ROBO_CHECK_EQ_INT(robo_unicode_script_from_name("nope"), ROBO_U_SCRIPT_UNKNOWN);
}

static void test_script_catalogue(void)
{
    struct {
        robo_uchar32_t cp;
        robo_unicode_script_t sc;
    } samples[] = {
        {'A', ROBO_U_SCRIPT_LATIN},
        {0x03B1u, ROBO_U_SCRIPT_GREEK},
        {0x0430u, ROBO_U_SCRIPT_CYRILLIC},
        {0x0561u, ROBO_U_SCRIPT_ARMENIAN},
        {0x10D0u, ROBO_U_SCRIPT_GEORGIAN},
        {0x0627u, ROBO_U_SCRIPT_ARABIC},
        {0x067Eu, ROBO_U_SCRIPT_ARABIC}, /* Persian/Urdu peh */
        {0x06AFu, ROBO_U_SCRIPT_ARABIC}, /* gaf */
        {0x05D0u, ROBO_U_SCRIPT_HEBREW},
        {0x0710u, ROBO_U_SCRIPT_SYRIAC},
        {0x0780u, ROBO_U_SCRIPT_THAANA},
        {0x0905u, ROBO_U_SCRIPT_DEVANAGARI},
        {0x0985u, ROBO_U_SCRIPT_BENGALI},
        {0x0A05u, ROBO_U_SCRIPT_GURMUKHI},
        {0x0A85u, ROBO_U_SCRIPT_GUJARATI},
        {0x0B05u, ROBO_U_SCRIPT_ORIYA},
        {0x0B85u, ROBO_U_SCRIPT_TAMIL},
        {0x0C05u, ROBO_U_SCRIPT_TELUGU},
        {0x0C85u, ROBO_U_SCRIPT_KANNADA},
        {0x0D05u, ROBO_U_SCRIPT_MALAYALAM},
        {0x0D85u, ROBO_U_SCRIPT_SINHALA},
        {0x0E01u, ROBO_U_SCRIPT_THAI},
        {0x0E81u, ROBO_U_SCRIPT_LAO},
        {0x1780u, ROBO_U_SCRIPT_KHMER},
        {0x1000u, ROBO_U_SCRIPT_MYANMAR},
        {0x0F40u, ROBO_U_SCRIPT_TIBETAN},
        {0x1200u, ROBO_U_SCRIPT_ETHIOPIC},
        {0x4E2Du, ROBO_U_SCRIPT_HAN},
        {0x3042u, ROBO_U_SCRIPT_HIRAGANA},
        {0x30A2u, ROBO_U_SCRIPT_KATAKANA},
        {0xAC00u, ROBO_U_SCRIPT_HANGUL},
        /* Historical / less-common */
        {0x10300u, ROBO_U_SCRIPT_OLD_ITALIC},
        {0x10400u, ROBO_U_SCRIPT_DESERET},
        {0x10A00u, ROBO_U_SCRIPT_KHAROSHTHI},
        {0x13000u, ROBO_U_SCRIPT_EGYPTIAN_HIEROGLYPHS},
        /* Common / Inherited / Unknown */
        {'0', ROBO_U_SCRIPT_COMMON},
        {0x0301u, ROBO_U_SCRIPT_INHERITED},
        {0x0378u, ROBO_U_SCRIPT_UNKNOWN},
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        ROBO_CHECK_EQ_INT(robo_unicode_script(samples[i].cp), samples[i].sc);
    }
}

static void test_script_extensions(void)
{
    robo_unicode_script_t buf[32];
    size_t n;

    /* Latin letter: SCX == Script */
    n = robo_unicode_script_extensions('A', buf, 32);
    ROBO_CHECK_EQ_INT(n, 1u);
    ROBO_CHECK_EQ_INT(buf[0], ROBO_U_SCRIPT_LATIN);
    ROBO_CHECK(robo_unicode_has_script('A', ROBO_U_SCRIPT_LATIN));
    ROBO_CHECK(!robo_unicode_has_script('A', ROBO_U_SCRIPT_ARABIC));

    /* U+0301 COMBINING ACUTE: Script=Inherited, SCX includes Latn Grek … */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0301u), ROBO_U_SCRIPT_INHERITED);
    n = robo_unicode_script_extensions(0x0301u, buf, 32);
    ROBO_CHECK(n > 1u);
    ROBO_CHECK(robo_unicode_has_script(0x0301u, ROBO_U_SCRIPT_LATIN));
    ROBO_CHECK(robo_unicode_has_script(0x0301u, ROBO_U_SCRIPT_GREEK));
    ROBO_CHECK(!robo_unicode_has_script(0x0301u, ROBO_U_SCRIPT_ARABIC));

    /* U+00B7 MIDDLE DOT — many scripts in SCX */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x00B7u), ROBO_U_SCRIPT_COMMON);
    n = robo_unicode_script_extensions(0x00B7u, buf, 32);
    ROBO_CHECK(n > 5u);
    ROBO_CHECK(robo_unicode_has_script(0x00B7u, ROBO_U_SCRIPT_LATIN));
    ROBO_CHECK(robo_unicode_has_script(0x00B7u, ROBO_U_SCRIPT_HAN));
}

static void test_script_runs(void)
{
    /* Hello + Arabic alef + combining acute on e */
    robo_uchar32_t cps[] = {'H', 'i', 0x0627u, 'e', 0x0301u};
    robo_unicode_script_run_t runs[8];
    size_t n = 0;

    ROBO_CHECK_EQ_INT(
        robo_unicode_script_runs(cps, 5, runs, 8, &n), ROBO_OK);
    ROBO_CHECK(n >= 2u);
    ROBO_CHECK_EQ_INT(runs[0].script, ROBO_U_SCRIPT_LATIN);
    /* Inherited acute should continue Latin after 'e' under run heuristic */
}

static void test_script_independent_of_bidi(void)
{
    /* Script Arabic ≠ automatic AL for all; digits are Common + EN bidi */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0660u), ROBO_U_SCRIPT_ARABIC);
    ROBO_CHECK_EQ_INT(robo_unicode_script('1'), ROBO_U_SCRIPT_COMMON);
}

static void test_script_conformance(void)
{
    size_t raw_len = 0;
    unsigned char *raw = load_conf(&raw_len);
    const unsigned char *p;
    const unsigned char *end;
    uint32_t scx_count;
    uint32_t sample_count;
    uint32_t i;
    size_t fail = 0;
    const size_t max_report = 12u;

    ROBO_CHECK(raw != NULL);
    if (!raw) {
        return;
    }
    p = raw;
    end = raw + raw_len;
    ROBO_CHECK(p + 4 <= end && p[0] == 'S' && p[1] == 'C' && p[2] == 'R' &&
               p[3] == 'P');
    p += 4;
    ROBO_CHECK_EQ_INT(rd_u16(&p, end), ROBO_SCRIPT_CONF_VERSION_MAJOR);
    ROBO_CHECK_EQ_INT(rd_u16(&p, end), ROBO_SCRIPT_CONF_VERSION_MINOR);
    ROBO_CHECK_EQ_INT(rd_u16(&p, end), ROBO_SCRIPT_CONF_VERSION_PATCH);
    scx_count = rd_u32(&p, end);
    sample_count = rd_u32(&p, end);
    ROBO_CHECK_EQ_INT(scx_count, ROBO_SCRIPT_CONF_SCX_RANGES);
    ROBO_CHECK_EQ_INT(sample_count, ROBO_SCRIPT_CONF_SCRIPT_SAMPLES);

    for (i = 0; i < scx_count; i++) {
        uint32_t start = rd_u32(&p, end);
        uint32_t stop = rd_u32(&p, end);
        uint16_t cnt = rd_u16(&p, end);
        robo_unicode_script_t want[64];
        uint16_t k;
        uint32_t cp;

        ROBO_CHECK(cnt <= 64u);
        for (k = 0; k < cnt; k++) {
            want[k] = (robo_unicode_script_t)rd_u16(&p, end);
        }
        for (cp = start; cp <= stop; cp++) {
            robo_unicode_script_t got[64];
            size_t n = robo_unicode_script_extensions(cp, got, 64);
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
                if (fail <= max_report) {
                    robo_test_fail(__FILE__,
                                   __LINE__,
                                   "SCX U+%04X n=%zu want=%u",
                                   (unsigned)cp,
                                   n,
                                   (unsigned)cnt);
                }
            }
        }
    }

    for (i = 0; i < sample_count; i++) {
        uint32_t cp = rd_u32(&p, end);
        uint16_t want = rd_u16(&p, end);
        if ((uint16_t)robo_unicode_script(cp) != want) {
            fail++;
            if (fail <= max_report) {
                robo_test_fail(__FILE__,
                               __LINE__,
                               "Script U+%04X got=%u want=%u",
                               (unsigned)cp,
                               (unsigned)robo_unicode_script(cp),
                               (unsigned)want);
            }
        }
    }

    ROBO_CHECK(p == end);
    free(raw);
    ROBO_CHECK_EQ_INT(fail, 0);
}

void robo_test_register_unicode_script(void)
{
    robo_test_register("script.names", test_script_names);
    robo_test_register("script.catalogue", test_script_catalogue);
    robo_test_register("script.extensions", test_script_extensions);
    robo_test_register("script.runs", test_script_runs);
    robo_test_register("script.vs_bidi", test_script_independent_of_bidi);
    robo_test_register("script.conformance", test_script_conformance);
}
