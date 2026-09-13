/*
 * RoboI18N transliteration suite (Goal 4.25).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void translit(const char *id, const char *in, char *out, size_t out_cap, size_t *out_len)
{
    robo_i18n_transliterator_t tr;
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_create_id(id, NULL, &tr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_transliterate(&tr, in, strlen(in), out, out_cap, out_len, NULL, 0,
                                              NULL),
                      ROBO_OK);
    robo_i18n_transliterator_destroy(&tr);
}

static void test_translit_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.25.0");
    ROBO_CHECK_STR_EQ(robo_i18n_translit_cldr_version_string(), "46");
    ROBO_CHECK(robo_i18n_translit_id_count() >= 8u);
}

static void test_translit_lookup(void)
{
    uint32_t idx = 0;
    robo_i18n_translit_id_info_t info;
    ROBO_CHECK_EQ_INT(robo_i18n_translit_find_id("Cyrillic-Latin", &idx), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_translit_id_info(idx, &info), ROBO_OK);
    ROBO_CHECK_STR_EQ(info.id, "Cyrillic-Latin");
    ROBO_CHECK_EQ_INT(robo_i18n_translit_find_id("NoSuch-Transform", &idx), ROBO_NOT_FOUND);
}

static void test_translit_russian_bgn(void)
{
    char out[256];
    size_t n = 0;
    /* Москва → Moskva (BGN) */
    translit("Russian-Latin/BGN", "\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0", out,
             sizeof(out), &n);
    ROBO_CHECK_STR_EQ(out, "Moskva");
}

static void test_translit_cyrillic(void)
{
    char out[256];
    size_t n = 0;
    translit("Cyrillic-Latin", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", out, sizeof(out),
             &n);
    ROBO_CHECK(n > 0);
    ROBO_CHECK(out[0] == 'P' || out[0] == 'p');
}

static void test_translit_ukrainian(void)
{
    char out[256];
    size_t n = 0;
    translit("Ukrainian-Latin/BGN", "\xD0\x9A\xD0\xB8\xD1\x97\xD0\xB2", out, sizeof(out), &n);
    ROBO_CHECK(n > 0);
}

static void test_translit_latin_ascii(void)
{
    char out[256];
    size_t n = 0;
    uint32_t idx = 0;
    ROBO_CHECK_EQ_INT(robo_i18n_translit_find_id("Latin-ASCII", &idx), ROBO_OK);
    ROBO_CHECK(idx != 0u); /* must not collapse to Cyrillic-Latin */
    /* Æ → AE */
    translit("Latin-ASCII", "\xC3\x86", out, sizeof(out), &n);
    ROBO_CHECK_STR_EQ(out, "AE");
    /* ß → ss */
    translit("Latin-ASCII", "\xC3\x9F", out, sizeof(out), &n);
    ROBO_CHECK_STR_EQ(out, "ss");
}

static void test_translit_script_matrix(void)
{
    char out[256];
    size_t n = 0;
    /* Greek λ → l */
    translit("Greek-Latin", "\xCE\xBB", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK(out[0] == 'l' || out[0] == 'L');
    /* Arabic ب → b (BGN-style Latin) */
    translit("Arabic-Latin", "\xD8\xA8", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Hebrew ב */
    translit("Hebrew-Latin", "\xD7\x91", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Devanagari क */
    translit("Devanagari-Latin", "\xE0\xA4\x95", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Bengali ক */
    translit("Bengali-Latin", "\xE0\xA6\x95", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Hangul 한 */
    translit("Hangul-Latin", "\xED\x95\x9C", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Katakana カ */
    translit("Katakana-Latin", "\xE3\x82\xAB", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
    /* Hiragana か */
    translit("Hiragana-Latin", "\xE3\x81\x8B", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
    ROBO_CHECK((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z'));
}

static void test_translit_rtl_logical(void)
{
    char out[256];
    size_t n = 0;
    translit("Arabic-Latin", "\xD8\xA8\xD8\xA7", out, sizeof(out), &n);
    ROBO_CHECK(n >= 1);
}

static void test_translit_stream(void)
{
    robo_i18n_transliterator_t tr;
    char a[64], b[64], c[64];
    size_t na = 0, nb = 0, nc = 0;
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_create_id("Latin-ASCII", NULL, &tr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_stream_reset(&tr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_stream_feed(&tr, "caf", 3, a, sizeof(a), &na),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_transliterator_stream_feed(&tr, "\xC3\xA9", 2, b, sizeof(b), &nb), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_stream_finish(&tr, c, sizeof(c), &nc), ROBO_OK);
    robo_i18n_transliterator_destroy(&tr);
}

static void test_translit_limits_fuzz(void)
{
    robo_i18n_transliterator_t tr;
    char out[64];
    size_t n = 0;
    char longbuf[70000];
    size_t i;
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_create_id("Cyrillic-Latin", NULL, &tr), ROBO_OK);
    (void)robo_i18n_transliterate(&tr, "\xFF\xFE", 2, out, sizeof(out), &n, NULL, 0, NULL);
    for (i = 0; i < sizeof(longbuf); i++) {
        longbuf[i] = 'a';
    }
    ROBO_CHECK_EQ_INT(
        robo_i18n_transliterate(&tr, longbuf, sizeof(longbuf), out, sizeof(out), &n, NULL, 0, NULL),
        ROBO_INVALID_ARGUMENT);
    robo_i18n_transliterator_destroy(&tr);
}

static void test_translit_search_boundary(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int found = 0;
    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_EXACT;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_contains(&m, "Moskva", 6, "Moskva", 6, &found), ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    found = 0;
    /* Exact search does not transliterate Cyrillic to Latin. */
    ROBO_CHECK_EQ_INT(robo_i18n_search_contains(&m, "\xD0\x9C", 2, "M", 1, &found), ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 0);
    robo_i18n_searcher_destroy(&m);
}

static void test_translit_map(void)
{
    robo_i18n_transliterator_t tr;
    char out[64];
    size_t n = 0, mc = 0;
    robo_i18n_translit_map_entry_t map[4];
    ROBO_CHECK_EQ_INT(robo_i18n_transliterator_create_id("Russian-Latin/BGN", NULL, &tr), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_transliterate(&tr, "\xD0\x9C", 2, out, sizeof(out), &n, map, 4,
                                              &mc),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mc, 1);
    ROBO_CHECK_EQ_INT((int)map[0].src_start, 0);
    ROBO_CHECK_EQ_INT((int)map[0].src_end, 2);
    robo_i18n_transliterator_destroy(&tr);
}

void robo_test_register_i18n_translit(void)
{
    robo_test_register("i18n.translit.versions", test_translit_versions);
    robo_test_register("i18n.translit.lookup", test_translit_lookup);
    robo_test_register("i18n.translit.russian_bgn", test_translit_russian_bgn);
    robo_test_register("i18n.translit.cyrillic", test_translit_cyrillic);
    robo_test_register("i18n.translit.ukrainian", test_translit_ukrainian);
    robo_test_register("i18n.translit.latin_ascii", test_translit_latin_ascii);
    robo_test_register("i18n.translit.script_matrix", test_translit_script_matrix);
    robo_test_register("i18n.translit.rtl", test_translit_rtl_logical);
    robo_test_register("i18n.translit.stream", test_translit_stream);
    robo_test_register("i18n.translit.fuzz", test_translit_limits_fuzz);
    robo_test_register("i18n.translit.search_boundary", test_translit_search_boundary);
    robo_test_register("i18n.translit.map", test_translit_map);
}
