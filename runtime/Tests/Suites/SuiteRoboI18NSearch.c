/*
 * RoboI18N search & matching suite (Goal 4.24).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboTest.h"

#include <string.h>

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void test_search_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.25.0");
    ROBO_CHECK_STR_EQ(robo_i18n_search_mode_string(ROBO_I18N_SEARCH_CASEFOLD), "casefold");
    ROBO_CHECK_STR_EQ(robo_i18n_search_mode_string(ROBO_I18N_SEARCH_COLLATION), "collation");
}

static void test_search_exact(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int eq = 0;
    int found = 0;
    robo_i18n_search_hit_t hit;

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_EXACT;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, "abc", 3, "abc", 3, &eq), ROBO_OK);
    ROBO_CHECK_EQ_INT(eq, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_search_find(&m, "xxabcyy", 7, "abc", 3, &found, &hit), ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    ROBO_CHECK_EQ_INT((int)hit.start, 2);
    ROBO_CHECK_EQ_INT((int)hit.end, 5);
    ROBO_CHECK_EQ_INT(robo_i18n_search_starts_with(&m, "abcdef", 6, "abc", 3, &found), ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_search_ends_with(&m, "xyzabc", 6, "abc", 3, &found), ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    robo_i18n_searcher_destroy(&m);
}

static void test_search_canonical(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int eq = 0;
    /* U+00E9 (é) vs e + combining acute */
    const char *precomposed = "\xC3\xA9";
    const char *decomposed = "e\xCC\x81";

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_CANONICAL;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, precomposed, strlen(precomposed), decomposed,
                                            strlen(decomposed), &eq),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(eq, 1);
    /* Exact must differ */
    {
        robo_i18n_search_options_t ox;
        robo_i18n_searcher_t mx;
        robo_i18n_search_options_init(&ox);
        ox.mode = ROBO_I18N_SEARCH_EXACT;
        ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&mx, NULL, &ox), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&mx, precomposed, strlen(precomposed), decomposed,
                                                strlen(decomposed), &eq),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(eq, 0);
        robo_i18n_searcher_destroy(&mx);
    }
    robo_i18n_searcher_destroy(&m);
}

static void test_search_casefold(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int eq = 0;
    int found = 0;
    robo_i18n_search_hit_t hit;

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_CASEFOLD;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, "Stra\xC3\x9F" "e", 7, "STRASSE", 7, &eq),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(eq, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, "\xCE\xA3", 2, "\xCF\x83", 2, &eq), ROBO_OK); /* Σ/σ */
    ROBO_CHECK_EQ_INT(eq, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_search_find(&m, "Hello WORLD", 11, "world", 5, &found, &hit),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    ROBO_CHECK_EQ_INT((int)hit.start, 6);
    ROBO_CHECK_EQ_INT((int)hit.end, 11);
    robo_i18n_searcher_destroy(&m);
}

static void test_search_diacritic(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int eq = 0;
    int found = 0;

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_CANONICAL;
    o.ignore_diacritics = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, "caf\xC3\xA9", 5, "cafe", 4, &eq), ROBO_OK);
    ROBO_CHECK_EQ_INT(eq, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_search_contains(&m, "na\xC3\xAFve", 6, "naive", 5, &found),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    robo_i18n_searcher_destroy(&m);
}

static void test_search_collation(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int eq = 0;

    parse_tag("en", &loc);
    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_COLLATION;
    o.collation_strength = ROBO_I18N_COLLATION_PRIMARY;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, &loc, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, "resume", 6, "r\xC3\xA9sum\xC3\xA9", 8, &eq),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(eq, 1);
    ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, "a", 1, "A", 1, &eq), ROBO_OK);
    ROBO_CHECK_EQ_INT(eq, 1);
    robo_i18n_searcher_destroy(&m);
}

static void test_search_intl_matrix(void)
{
    static const char *tags[] = {"en", "de", "fr", "tr", "el", "ru", "ar", "fa",
                                 "ur", "he", "hi", "bn", "th", "zh", "ja", "ko"};
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        robo_i18n_locale_t loc;
        robo_i18n_searcher_t m;
        robo_i18n_search_options_t o;
        int found = 0;
        parse_tag(tags[i], &loc);
        robo_i18n_search_options_init(&o);
        o.mode = ROBO_I18N_SEARCH_CASEFOLD;
        ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, &loc, &o), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_search_contains(&m, "AbcXyz", 6, "xyz", 3, &found), ROBO_OK);
        ROBO_CHECK_EQ_INT(found, 1);
        robo_i18n_searcher_destroy(&m);

        robo_i18n_search_options_init(&o);
        o.mode = ROBO_I18N_SEARCH_COLLATION;
        o.collation_strength = ROBO_I18N_COLLATION_PRIMARY;
        ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, &loc, &o), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_search_equal(&m, "test", 4, "TEST", 4, &found), ROBO_OK);
        ROBO_CHECK_EQ_INT(found, 1);
        robo_i18n_searcher_destroy(&m);
    }
}

static void test_search_rtl(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int found = 0;
    robo_i18n_search_hit_t hit;
    /* سلام — Arabic "salam" */
    const char *hay = "xx\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85yy";
    const char *needle = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_EXACT;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_find(&m, hay, strlen(hay), needle, strlen(needle), &found,
                                           &hit),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    ROBO_CHECK_EQ_INT((int)hit.start, 2);
    robo_i18n_searcher_destroy(&m);
}

static void test_search_grapheme(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int found = 0;
    robo_i18n_search_hit_t hit;
    /* family emoji ZWJ sequence */
    const char *emoji = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    char hay[64];
    size_t n;

    memcpy(hay, "a", 1);
    memcpy(hay + 1, emoji, strlen(emoji));
    memcpy(hay + 1 + strlen(emoji), "b", 1);
    n = 1 + strlen(emoji) + 1;

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_EXACT;
    o.grapheme_safe = 1;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_find(&m, hay, n, emoji, strlen(emoji), &found, &hit),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(found, 1);
    ROBO_CHECK_EQ_INT((int)hit.start, 1);
    ROBO_CHECK(hit.grapheme_end > hit.grapheme_start);
    robo_i18n_searcher_destroy(&m);
}

static void test_search_find_all(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    robo_i18n_search_hit_t hits[8];
    size_t count = 0;

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_CASEFOLD;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_search_find_all(&m, "aBaBa", 5, "a", 1, hits, 8, &count), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)count, 3);
    ROBO_CHECK_EQ_INT((int)hits[0].start, 0);
    ROBO_CHECK_EQ_INT((int)hits[1].start, 2);
    ROBO_CHECK_EQ_INT((int)hits[2].start, 4);
    robo_i18n_searcher_destroy(&m);
}

static void test_search_fuzz(void)
{
    robo_i18n_searcher_t m;
    robo_i18n_search_options_t o;
    int found = 0;
    char longbuf[70000];
    size_t i;

    robo_i18n_search_options_init(&o);
    o.mode = ROBO_I18N_SEARCH_CASEFOLD;
    ROBO_CHECK_EQ_INT(robo_i18n_searcher_init(&m, NULL, &o), ROBO_OK);
    (void)robo_i18n_search_contains(&m, "\xFF\xFE", 2, "a", 1, &found);
    (void)robo_i18n_search_contains(&m, "e\xCC\x81\xCC\x81\xCC\x81", 7, "é", 2, &found);
    for (i = 0; i < sizeof(longbuf); i++) {
        longbuf[i] = 'a';
    }
    ROBO_CHECK_EQ_INT(robo_i18n_search_contains(&m, longbuf, sizeof(longbuf), "a", 1, &found),
                      ROBO_INVALID_ARGUMENT);
    robo_i18n_searcher_destroy(&m);
}

void robo_test_register_i18n_search(void)
{
    robo_test_register("i18n.search.versions", test_search_versions);
    robo_test_register("i18n.search.exact", test_search_exact);
    robo_test_register("i18n.search.canonical", test_search_canonical);
    robo_test_register("i18n.search.casefold", test_search_casefold);
    robo_test_register("i18n.search.diacritic", test_search_diacritic);
    robo_test_register("i18n.search.collation", test_search_collation);
    robo_test_register("i18n.search.intl", test_search_intl_matrix);
    robo_test_register("i18n.search.rtl", test_search_rtl);
    robo_test_register("i18n.search.grapheme", test_search_grapheme);
    robo_test_register("i18n.search.find_all", test_search_find_all);
    robo_test_register("i18n.search.fuzz", test_search_fuzz);
}
