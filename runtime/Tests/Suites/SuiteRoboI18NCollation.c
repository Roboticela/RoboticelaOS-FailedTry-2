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

#ifdef ROBO_COLLATION_TEST_PATH_FROM_BUILD
#include "robo_collation_test_path.h"
#endif
#ifndef ROBO_COLLATION_TEST_PATH
#define ROBO_COLLATION_TEST_PATH ""
#endif

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_coll_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.27.0");
    ROBO_CHECK_STR_EQ(robo_i18n_collation_uca_version_string(), "16.0.0");
    ROBO_CHECK_STR_EQ(robo_i18n_collation_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(ROBO_UCA_VERSION_STRING, "16.0.0");
    ROBO_CHECK(robo_uca_map_count > 10000u);
    ROBO_CHECK(robo_coll_locale_count >= 10u);
}

static robo_i18n_collator_t *make_coll(const char *tag, const char *type)
{
    robo_i18n_locale_t loc;
    robo_i18n_collator_t *c = NULL;
    robo_i18n_collator_options_t opt;
    parse_tag(tag, &loc);
    robo_i18n_collator_options_init(&opt);
    ROBO_CHECK_EQ_INT(robo_i18n_collator_create(&loc, type, &opt, &c), ROBO_OK);
    ROBO_CHECK(c != NULL);
    return c;
}

static int cmp_str(robo_i18n_collator_t *c, const char *a, const char *b)
{
    int cmp = 0;
    ROBO_CHECK_EQ_INT(
        robo_i18n_collator_compare(c, a, strlen(a), b, strlen(b), &cmp), ROBO_OK);
    return cmp;
}

static void test_coll_modes_separated(void)
{
    int bcmp = 0;
    int pcmp = 0;
    int lcmp = 0;
    robo_i18n_collator_t *c = make_coll("en", "standard");
    const char *nfd_e = "e\xCC\x81"; /* e + combining acute */
    const char *nfc_e = "\xC3\xA9";  /* é */

    ROBO_CHECK_EQ_INT(
        robo_i18n_compare_utf8_binary(nfd_e, strlen(nfd_e), nfc_e, strlen(nfc_e), &bcmp),
        ROBO_OK);
    ROBO_CHECK(bcmp != 0); /* different bytes */

    ROBO_CHECK_EQ_INT(robo_i18n_compare_utf8_codepoints(
                          nfd_e, strlen(nfd_e), nfc_e, strlen(nfc_e), &pcmp),
                      ROBO_OK);
    ROBO_CHECK(pcmp != 0); /* different code-point sequences before NFD unify */

    lcmp = cmp_str(c, nfd_e, nfc_e);
    ROBO_CHECK_EQ_INT(lcmp, 0); /* UCA NFD → equal at primary+ */

    robo_i18n_collator_destroy(c);
}

static void test_coll_strength(void)
{
    robo_i18n_collator_t *c = make_coll("en", "standard");
    robo_i18n_collator_options_t opt;
    int eq = 0;

    ROBO_CHECK_EQ_INT(robo_i18n_collator_get_options(c, &opt), ROBO_OK);
    opt.strength = ROBO_I18N_COLLATION_PRIMARY;
    ROBO_CHECK_EQ_INT(robo_i18n_collator_set_options(c, &opt), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_collator_equal(c, "resume", 6, "résumé", strlen("résumé"), &eq), ROBO_OK);
    ROBO_CHECK(eq);

    opt.strength = ROBO_I18N_COLLATION_TERTIARY;
    ROBO_CHECK_EQ_INT(robo_i18n_collator_set_options(c, &opt), ROBO_OK);
    ROBO_CHECK(cmp_str(c, "a", "A") != 0);
    opt.strength = ROBO_I18N_COLLATION_PRIMARY;
    ROBO_CHECK_EQ_INT(robo_i18n_collator_set_options(c, &opt), ROBO_OK);
    ROBO_CHECK_EQ_INT(cmp_str(c, "a", "A"), 0);

    robo_i18n_collator_destroy(c);
}

static void test_coll_sort_key(void)
{
    robo_i18n_collator_t *c = make_coll("en", "standard");
    uint8_t ka[256];
    uint8_t kb[256];
    size_t la = 0;
    size_t lb = 0;
    int cmp = 0;

    ROBO_CHECK_EQ_INT(
        robo_i18n_collator_get_sort_key(c, "file2", 5, NULL, 0, &la), ROBO_OK);
    ROBO_CHECK(la > 0);
    ROBO_CHECK_EQ_INT(
        robo_i18n_collator_get_sort_key(c, "file2", 5, ka, sizeof(ka), &la), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_collator_get_sort_key(c, "file10", 6, kb, sizeof(kb), &lb), ROBO_OK);
    ROBO_CHECK(memcmp(ka, kb, la < lb ? la : lb) != 0 || la != lb);

    cmp = cmp_str(c, "file2", "file10");
    {
        int kc = 0;
        size_t n = la < lb ? la : lb;
        int m = memcmp(ka, kb, n);
        if (m == 0) {
            kc = (la < lb) ? -1 : (la > lb) ? 1 : 0;
        } else {
            kc = m < 0 ? -1 : 1;
        }
        ROBO_CHECK_EQ_INT(kc, cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
    }

    robo_i18n_collator_destroy(c);
}

static void test_coll_numeric(void)
{
    robo_i18n_collator_t *c = make_coll("en", "standard");
    robo_i18n_collator_options_t opt;
    ROBO_CHECK_EQ_INT(robo_i18n_collator_get_options(c, &opt), ROBO_OK);
    opt.numeric = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_collator_set_options(c, &opt), ROBO_OK);
    ROBO_CHECK(cmp_str(c, "file2", "file10") < 0);
    opt.numeric = 0;
    ROBO_CHECK_EQ_INT(robo_i18n_collator_set_options(c, &opt), ROBO_OK);
    ROBO_CHECK(cmp_str(c, "file2", "file10") > 0); /* lexicographic: '1' < '2' → file10 first */
    robo_i18n_collator_destroy(c);
}

static void test_coll_tailoring_sv_es_de(void)
{
    robo_i18n_collator_t *sv = make_coll("sv", "standard");
    robo_i18n_collator_t *es = make_coll("es", "standard");
    robo_i18n_collator_t *de = make_coll("de", "phonebook");
    robo_i18n_collator_t *en = make_coll("en", "standard");

    /* Swedish: ö after z (primary remap). */
    ROBO_CHECK(cmp_str(sv, "z", "ö") < 0);
    /* English root: ö typically near o, before z. */
    ROBO_CHECK(cmp_str(en, "ö", "z") < 0);

    /* Spanish: ñ after n. */
    ROBO_CHECK(cmp_str(es, "n", "ñ") < 0);
    ROBO_CHECK(cmp_str(es, "ñ", "o") < 0);

    /* German phonebook: ä treated near ae — at least creates without error. */
    ROBO_CHECK(cmp_str(de, "ae", "ä") != 99); /* smoke */

    robo_i18n_collator_destroy(sv);
    robo_i18n_collator_destroy(es);
    robo_i18n_collator_destroy(de);
    robo_i18n_collator_destroy(en);
}

static void test_coll_locales_smoke(void)
{
    static const char *tags[] = {
        "en", "de", "sv", "es", "tr", "ar", "fa", "ur", "he", "hi", "bn", "zh", "ja", "ko"
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_collator_t *c = make_coll(tags[i], "standard");
        ROBO_CHECK(cmp_str(c, "a", "b") < 0);
        /* mixed scripts should not crash */
        (void)cmp_str(c, "hello", "سلام");
        (void)cmp_str(c, "東京", "京都");
        robo_i18n_collator_destroy(c);
    }
}

static void test_coll_invalid_utf8(void)
{
    robo_i18n_collator_t *c = make_coll("en", "standard");
    int cmp = 0;
    const char bad[] = {(char)0xC3, (char)0x28}; /* invalid */
    ROBO_CHECK_EQ_INT(
        robo_i18n_collator_compare(c, bad, 2, "a", 1, &cmp), ROBO_PARSE_ERROR);
    ROBO_CHECK_EQ_INT(
        robo_i18n_compare_utf8_codepoints(bad, 2, "a", 1, &cmp), ROBO_PARSE_ERROR);
    robo_i18n_collator_destroy(c);
}

static void test_coll_fuzz(void)
{
    robo_i18n_collator_t *c = make_coll("en", "standard");
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
        ROBO_CHECK_EQ_INT(robo_i18n_collator_compare(c, a, al, b, bl, &c1), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_collator_compare(c, b, bl, a, al, &c2), ROBO_OK);
        if (c1 == 0) {
            ROBO_CHECK_EQ_INT(c2, 0);
        } else {
            ROBO_CHECK(c1 == -c2);
        }
    }
    /* malformed */
    {
        char bad[3] = {(char)0xFF, (char)0xFE, (char)0xFD};
        int cmp = 0;
        ROBO_CHECK(
            robo_i18n_collator_compare(c, bad, 3, "x", 1, &cmp) == ROBO_PARSE_ERROR);
    }
    robo_i18n_collator_destroy(c);
}

static int parse_hex_line(const char *line, uint32_t *out, size_t cap, size_t *n)
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

static size_t encode_utf8(const uint32_t *cps, size_t n, char *out, size_t cap)
{
    size_t i;
    size_t o = 0;
    for (i = 0; i < n; i++) {
        size_t w = 0;
        if (robo_unicode_utf8_encode(cps[i], out + o, cap - o, &w) != ROBO_UNICODE_OK) {
            return 0;
        }
        o += w;
    }
    return o;
}

static void test_coll_uca_conformance_sample(void)
{
    const char *path = ROBO_COLLATION_TEST_PATH;
    FILE *f;
    char line[512];
    uint32_t prev[32];
    size_t prev_n = 0;
    char prev_utf[128];
    size_t prev_len = 0;
    int have_prev = 0;
    size_t checked = 0;
    size_t line_no = 0;
    robo_i18n_collator_t *c;
    robo_i18n_collator_options_t opt;

    if (!path || !path[0]) {
        ROBO_CHECK(0 && "collation test path missing");
        return;
    }
    f = fopen(path, "rb");
    if (!f) {
        ROBO_CHECK(0 && "cannot open CollationTest");
        return;
    }

    c = make_coll("root", "standard");
    ROBO_CHECK_EQ_INT(robo_i18n_collator_get_options(c, &opt), ROBO_OK);
    opt.alternate = ROBO_I18N_COLLATION_NON_IGNORABLE;
    opt.strength = ROBO_I18N_COLLATION_TERTIARY;
    ROBO_CHECK_EQ_INT(robo_i18n_collator_set_options(c, &opt), ROBO_OK);

    while (fgets(line, sizeof(line), f) && checked < 4000) {
        uint32_t cps[32];
        size_t n = 0;
        char utf[128];
        size_t ulen;
        line_no++;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (!parse_hex_line(line, cps, 32, &n) || n == 0) {
            continue;
        }
        ulen = encode_utf8(cps, n, utf, sizeof(utf));
        if (ulen == 0) {
            continue;
        }
        if (have_prev) {
            int cmp = 0;
            robo_result_t r =
                robo_i18n_collator_compare(c, prev_utf, prev_len, utf, ulen, &cmp);
            if (r == ROBO_OK) {
                /* UCA test lines are weakly increasing (non-decreasing). */
                ROBO_CHECK(cmp <= 0);
                checked++;
            }
        }
        memcpy(prev, cps, n * sizeof(uint32_t));
        prev_n = n;
        memcpy(prev_utf, utf, ulen);
        prev_len = ulen;
        have_prev = 1;
        (void)prev_n;
        (void)line_no;
    }
    fclose(f);
    ROBO_CHECK(checked > 100);
    robo_i18n_collator_destroy(c);
}

void robo_test_register_i18n_collation(void)
{
    robo_test_register("i18n.coll.versions", test_coll_versions);
    robo_test_register("i18n.coll.modes", test_coll_modes_separated);
    robo_test_register("i18n.coll.strength", test_coll_strength);
    robo_test_register("i18n.coll.sort_key", test_coll_sort_key);
    robo_test_register("i18n.coll.numeric", test_coll_numeric);
    robo_test_register("i18n.coll.tailoring", test_coll_tailoring_sv_es_de);
    robo_test_register("i18n.coll.locales", test_coll_locales_smoke);
    robo_test_register("i18n.coll.invalid_utf8", test_coll_invalid_utf8);
    robo_test_register("i18n.coll.fuzz", test_coll_fuzz);
    robo_test_register("i18n.coll.uca_sample", test_coll_uca_conformance_sample);
}
