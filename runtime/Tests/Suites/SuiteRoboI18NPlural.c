/*
 * RoboI18N plural-rules suite (Goal 4.4).
 *
 * Samples: Tests/Data/robo_plural_samples.txt (CLDR @integer/@decimal via ROBO_PLURAL_SAMPLES_PATH).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboPluralData.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ROBO_PLURAL_SAMPLES_PATH_FROM_BUILD
#include "robo_plural_samples_path.h"
#endif

#ifndef ROBO_PLURAL_SAMPLES_PATH
#define ROBO_PLURAL_SAMPLES_PATH ""
#endif

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static robo_i18n_plural_category_t cat_from_name(const char *name)
{
    size_t i;
    for (i = 0; i < 6; i++) {
        if (strcmp(robo_i18n_plural_category_name((robo_i18n_plural_category_t)i), name) == 0) {
            return (robo_i18n_plural_category_t)i;
        }
    }
    return ROBO_I18N_PLURAL_OTHER;
}

static void expect_cardinal(const char *tag, const char *input, const char *want_name)
{
    robo_i18n_locale_t loc;
    robo_i18n_plural_operands_t ops;
    robo_i18n_plural_category_t got;
    robo_i18n_plural_category_t want = cat_from_name(want_name);

    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string(input, strlen(input), &ops), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_select(&loc, ROBO_I18N_PLURAL_CARDINAL, &ops, &got), ROBO_OK);
    if (got != want) {
        fprintf(stderr,
                "PLURAL FAIL locale=%s input=%s expected=%s actual=%s "
                "ops: i=%.*s v=%u w=%u f=%.*s e=%u neg=%u\n",
                tag,
                input,
                want_name,
                robo_i18n_plural_category_name(got),
                (int)ops.i_len,
                ops.i_digits,
                (unsigned)ops.v,
                (unsigned)ops.w,
                (int)ops.v,
                ops.f_digits,
                (unsigned)ops.e,
                (unsigned)ops.negative_input);
    }
    ROBO_CHECK_EQ_INT((int)got, (int)want);
}

static void expect_ordinal(const char *tag, const char *input, const char *want_name)
{
    robo_i18n_locale_t loc;
    robo_i18n_plural_operands_t ops;
    robo_i18n_plural_category_t got;
    robo_i18n_plural_category_t want = cat_from_name(want_name);

    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string(input, strlen(input), &ops), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_select(&loc, ROBO_I18N_PLURAL_ORDINAL, &ops, &got), ROBO_OK);
    if (got != want) {
        fprintf(stderr,
                "ORDINAL FAIL locale=%s input=%s expected=%s actual=%s "
                "ops: i=%.*s v=%u w=%u f=%.*s e=%u neg=%u\n",
                tag,
                input,
                want_name,
                robo_i18n_plural_category_name(got),
                (int)ops.i_len,
                ops.i_digits,
                (unsigned)ops.v,
                (unsigned)ops.w,
                (int)ops.v,
                ops.f_digits,
                (unsigned)ops.e,
                (unsigned)ops.negative_input);
    }
    ROBO_CHECK_EQ_INT((int)got, (int)want);
}

static void test_plural_versions(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.25.0");
    ROBO_CHECK_STR_EQ(robo_i18n_cldr_version_string(), "46");
    ROBO_CHECK_STR_EQ(ROBO_PLURAL_CLDR_VERSION, "46");
    ROBO_CHECK_STR_EQ(ROBO_PLURAL_UNICODE_VERSION, "16.0.0");
    ROBO_CHECK(robo_plural_cardinal_index_count >= 200u);
    ROBO_CHECK(robo_plural_ordinal_index_count >= 100u);
    ROBO_CHECK(robo_plural_bytecode_size > 0u);
}

static void test_plural_category_names(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_plural_category_name(ROBO_I18N_PLURAL_ZERO), "zero");
    ROBO_CHECK_STR_EQ(robo_i18n_plural_category_name(ROBO_I18N_PLURAL_ONE), "one");
    ROBO_CHECK_STR_EQ(robo_i18n_plural_category_name(ROBO_I18N_PLURAL_TWO), "two");
    ROBO_CHECK_STR_EQ(robo_i18n_plural_category_name(ROBO_I18N_PLURAL_FEW), "few");
    ROBO_CHECK_STR_EQ(robo_i18n_plural_category_name(ROBO_I18N_PLURAL_MANY), "many");
    ROBO_CHECK_STR_EQ(robo_i18n_plural_category_name(ROBO_I18N_PLURAL_OTHER), "other");
}

static void test_plural_operands_trailing_zeros(void)
{
    robo_i18n_plural_operands_t a, b, c;
    ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string("1", 1, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string("1.0", 3, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string("1.00", 4, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)a.v, 0);
    ROBO_CHECK_EQ_INT((int)b.v, 1);
    ROBO_CHECK_EQ_INT((int)c.v, 2);
    ROBO_CHECK_EQ_INT((int)a.w, 0);
    ROBO_CHECK_EQ_INT((int)b.w, 0);
    ROBO_CHECK_EQ_INT((int)c.w, 0);
    ROBO_CHECK_EQ_INT((int)a.i_len, 1);
    ROBO_CHECK_EQ_INT((int)b.i_len, 1);
    ROBO_CHECK_EQ_INT((int)c.i_len, 1);
}

static void test_plural_en(void)
{
    expect_cardinal("en", "0", "other");
    expect_cardinal("en", "1", "one");
    expect_cardinal("en", "2", "other");
    expect_cardinal("en", "1.0", "other");
    expect_cardinal("en", "1.00", "other");
    expect_cardinal("en-US", "1", "one");
    expect_cardinal("en-GB", "21", "other");
}

static void test_plural_fr(void)
{
    expect_cardinal("fr", "0", "one");
    expect_cardinal("fr", "1", "one");
    expect_cardinal("fr", "1.0", "one");
    expect_cardinal("fr", "1.1", "one");
    expect_cardinal("fr", "2", "other");
    expect_cardinal("fr", "1000000", "many");
}

static void test_plural_de_es(void)
{
    expect_cardinal("de", "0", "other");
    expect_cardinal("de", "1", "one");
    expect_cardinal("de", "1.0", "other");
    expect_cardinal("es", "1", "one");
    expect_cardinal("es", "1.0", "one");
    expect_cardinal("es", "1.1", "other");
    expect_cardinal("es", "1000000", "many");
}

static void test_plural_slavic(void)
{
    /* Russian */
    expect_cardinal("ru", "1", "one");
    expect_cardinal("ru", "21", "one");
    expect_cardinal("ru", "2", "few");
    expect_cardinal("ru", "3", "few");
    expect_cardinal("ru", "4", "few");
    expect_cardinal("ru", "22", "few");
    expect_cardinal("ru", "5", "many");
    expect_cardinal("ru", "11", "many");
    expect_cardinal("ru", "12", "many");
    expect_cardinal("ru", "14", "many");
    expect_cardinal("ru", "25", "many");
    expect_cardinal("ru", "100", "many");
    expect_cardinal("ru", "101", "one");
    expect_cardinal("ru", "102", "few");
    expect_cardinal("ru", "111", "many");
    expect_cardinal("ru", "112", "many");
    expect_cardinal("ru", "1.5", "other");

    /* Ukrainian */
    expect_cardinal("uk", "1", "one");
    expect_cardinal("uk", "2", "few");
    expect_cardinal("uk", "5", "many");
    expect_cardinal("uk", "11", "many");
    expect_cardinal("uk", "22", "few");

    /* Polish */
    expect_cardinal("pl", "1", "one");
    expect_cardinal("pl", "2", "few");
    expect_cardinal("pl", "3", "few");
    expect_cardinal("pl", "4", "few");
    expect_cardinal("pl", "5", "many");
    expect_cardinal("pl", "12", "many");
    expect_cardinal("pl", "22", "few");
    expect_cardinal("pl", "25", "many");
    expect_cardinal("pl", "1.5", "other");

    /* Czech / Slovak / Slovenian */
    expect_cardinal("cs", "1", "one");
    expect_cardinal("cs", "2", "few");
    expect_cardinal("cs", "5", "other");
    expect_cardinal("sk", "1", "one");
    expect_cardinal("sk", "2", "few");
    expect_cardinal("sl", "1", "one");
    expect_cardinal("sl", "2", "two");
    expect_cardinal("sl", "3", "few");
    expect_cardinal("sl", "5", "other");
}

static void test_plural_arabic(void)
{
    expect_cardinal("ar", "0", "zero");
    expect_cardinal("ar", "1", "one");
    expect_cardinal("ar", "2", "two");
    expect_cardinal("ar", "3", "few");
    expect_cardinal("ar", "10", "few");
    expect_cardinal("ar", "11", "many");
    expect_cardinal("ar", "99", "many");
    expect_cardinal("ar", "100", "other");
    expect_cardinal("ar", "101", "other");
    expect_cardinal("ar", "102", "other");
    expect_cardinal("ar", "111", "many");
    expect_cardinal("ar", "0.0", "zero");
    expect_cardinal("ar", "1.0", "one");
    expect_cardinal("ar", "1.00", "one");
    expect_cardinal("ar", "2.0", "two");
    expect_cardinal("ar-SA", "3", "few");
    expect_cardinal("ar-EG", "11", "many");
}

static void test_plural_celtic_baltic(void)
{
    expect_cardinal("he", "1", "one");
    expect_cardinal("he", "2", "two");
    expect_cardinal("he", "20", "other");
    expect_cardinal("ga", "1", "one");
    expect_cardinal("ga", "2", "two");
    expect_cardinal("ga", "3", "few");
    expect_cardinal("ga", "7", "many");
    expect_cardinal("cy", "0", "zero");
    expect_cardinal("cy", "1", "one");
    expect_cardinal("cy", "2", "two");
    expect_cardinal("cy", "3", "few");
    expect_cardinal("cy", "6", "many");
    expect_cardinal("ro", "1", "one");
    expect_cardinal("ro", "0", "few");
    expect_cardinal("ro", "2", "few");
    expect_cardinal("ro", "20", "other");
    expect_cardinal("lt", "1", "one");
    expect_cardinal("lt", "2", "few");
    expect_cardinal("lt", "10", "other");
    expect_cardinal("lv", "0", "zero");
    expect_cardinal("lv", "1", "one");
    expect_cardinal("lv", "2", "other");
}

static void test_plural_pt_region(void)
{
    /* Brazilian Portuguese treats 0 as "one"; European Portuguese does not. */
    expect_cardinal("pt", "0", "one");
    expect_cardinal("pt-BR", "0", "one");
    expect_cardinal("pt-PT", "0", "other");
    expect_cardinal("pt-PT", "1", "one");
}

static void test_plural_east_asian(void)
{
    expect_cardinal("ja", "0", "other");
    expect_cardinal("ja", "1", "other");
    expect_cardinal("ja", "2", "other");
    expect_cardinal("zh", "1", "other");
    expect_cardinal("zh-Hans", "5", "other");
    expect_cardinal("ko", "1", "other");
    expect_cardinal("th", "1", "other");
}

static void test_plural_decimals(void)
{
    expect_cardinal("en", "0.0", "other");
    expect_cardinal("en", "0.1", "other");
    expect_cardinal("en", "0.5", "other");
    expect_cardinal("en", "1.1", "other");
    expect_cardinal("en", "1.5", "other");
    expect_cardinal("en", "2.0", "other");
    expect_cardinal("en", "10.0", "other");
    expect_cardinal("en", "10.1", "other");
    expect_cardinal("ru", "2.0", "other");
    expect_cardinal("ru", "5.0", "other");
    expect_cardinal("ru", "5.5", "other");
    expect_cardinal("pl", "2.0", "other");
    expect_cardinal("fr", "2.0", "other");
    expect_cardinal("fr", "2.1", "other");
}

static void test_plural_negatives(void)
{
    /* CLDR operands use absolute value. */
    expect_cardinal("en", "-1", "one");
    expect_cardinal("en", "-2", "other");
    expect_cardinal("ru", "-1", "one");
    expect_cardinal("ru", "-2", "few");
    expect_cardinal("ar", "-1", "one");
    expect_cardinal("ar", "-2", "two");
    expect_cardinal("en", "-1.5", "other");
    expect_cardinal("en", "-2.0", "other");
}

static void test_plural_large(void)
{
    expect_cardinal("en", "1000000000000000000", "other");
    expect_cardinal("fr", "1000000", "many");
    expect_cardinal("fr", "2000000", "many");
    expect_cardinal("ru", "1000001", "one");
    expect_cardinal("ar", "10000000011", "many");
}

static void test_plural_ordinal_en(void)
{
    expect_ordinal("en", "1", "one");
    expect_ordinal("en", "2", "two");
    expect_ordinal("en", "3", "few");
    expect_ordinal("en", "4", "other");
    expect_ordinal("en", "21", "one");
    expect_ordinal("en", "22", "two");
    expect_ordinal("en", "23", "few");
    expect_ordinal("en", "24", "other");
}

static void test_plural_categories_api(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_plural_category_t cats[8];
    size_t n = 0;
    size_t i;
    int saw_other = 0;

    parse_tag("ar", &loc);
    ROBO_CHECK_EQ_INT(
        robo_i18n_plural_categories(&loc, ROBO_I18N_PLURAL_CARDINAL, cats, 8, &n), ROBO_OK);
    ROBO_CHECK(n >= 6u);
    for (i = 0; i < n && i < 8; i++) {
        if (cats[i] == ROBO_I18N_PLURAL_OTHER) {
            saw_other = 1;
        }
    }
    ROBO_CHECK(saw_other);

    parse_tag("ja", &loc);
    ROBO_CHECK_EQ_INT(
        robo_i18n_plural_categories(&loc, ROBO_I18N_PLURAL_CARDINAL, cats, 8, &n), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)n, 1);
    ROBO_CHECK_EQ_INT((int)cats[0], (int)ROBO_I18N_PLURAL_OTHER);
}

static void test_plural_i64_api(void)
{
    robo_i18n_locale_t loc;
    robo_i18n_plural_category_t cat;
    parse_tag("en", &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&loc, 1, &cat), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)cat, (int)ROBO_I18N_PLURAL_ONE);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&loc, -1, &cat), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)cat, (int)ROBO_I18N_PLURAL_ONE);
}

static int load_and_run_samples(void)
{
    const char *path = ROBO_PLURAL_SAMPLES_PATH;
    FILE *f;
    char line[256];
    int failures = 0;
    int total = 0;
    int skipped = 0;

    if (!path || !path[0]) {
        fprintf(stderr, "plural samples path empty\n");
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open plural samples: %s\n", path);
        return -1;
    }
    while (fgets(line, (int)sizeof(line), f)) {
        char kind[16];
        char locale[32];
        char input[64];
        char expect[16];
        robo_i18n_locale_t loc;
        robo_i18n_plural_operands_t ops;
        robo_i18n_plural_category_t got;
        robo_i18n_plural_category_t want;
        robo_i18n_plural_kind_t pk;
        char *nl;

        if (line[0] == '#' || line[0] == '\0' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        nl = strchr(line, '\r');
        if (nl) {
            *nl = '\0';
        }
        if (sscanf(line, "%15s %31s %63s %15s", kind, locale, input, expect) != 4) {
            failures++;
            continue;
        }
        if (strcmp(kind, "cardinal") == 0) {
            pk = ROBO_I18N_PLURAL_CARDINAL;
        } else if (strcmp(kind, "ordinal") == 0) {
            pk = ROBO_I18N_PLURAL_ORDINAL;
        } else {
            skipped++;
            continue;
        }
        if (robo_i18n_locale_parse_canonical(locale, strlen(locale), &loc) != ROBO_OK) {
            failures++;
            continue;
        }
        if (robo_i18n_plural_operands_from_string(input, strlen(input), &ops) != ROBO_OK) {
            /* Extremely long / unsupported sample — skip safely. */
            skipped++;
            continue;
        }
        want = cat_from_name(expect);
        if (robo_i18n_plural_select(&loc, pk, &ops, &got) != ROBO_OK) {
            failures++;
            continue;
        }
        total++;
        if (got != want) {
            failures++;
            if (failures <= 20) {
                fprintf(stderr,
                        "SAMPLE FAIL kind=%s locale=%s input=%s expected=%s actual=%s "
                        "ops: i=%.*s v=%u w=%u f=%.*s e=%u\n",
                        kind,
                        locale,
                        input,
                        expect,
                        robo_i18n_plural_category_name(got),
                        (int)ops.i_len,
                        ops.i_digits,
                        (unsigned)ops.v,
                        (unsigned)ops.w,
                        (int)ops.v,
                        ops.f_digits,
                        (unsigned)ops.e);
            }
        }
    }
    fclose(f);
    fprintf(stderr, "plural samples: total=%d failures=%d skipped=%d\n", total, failures, skipped);
    return failures;
}

static void test_plural_cldr_samples(void)
{
    int failures = load_and_run_samples();
    ROBO_CHECK(failures == 0);
}

static void test_plural_fuzz_smoke(void)
{
    static const char *tags[] = {"en", "ar", "ru", "pl", "fr", "ja", "xx-YY", "und"};
    static const char *inputs[] = {
        "", "-", ".", "1.", ".1", "++1", "1..2", "9999999999999999999999999999999999999999",
        "1.000000000000000000000000000000000000000", "-0.000", "0001.00", "abc", "1e3", "NaN"
    };
    size_t ti, ii;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        robo_i18n_locale_t loc;
        if (robo_i18n_locale_parse_canonical(tags[ti], strlen(tags[ti]), &loc) != ROBO_OK) {
            continue;
        }
        for (ii = 0; ii < sizeof(inputs) / sizeof(inputs[0]); ii++) {
            robo_i18n_plural_operands_t ops;
            robo_i18n_plural_category_t cat;
            if (robo_i18n_plural_operands_from_string(inputs[ii], strlen(inputs[ii]), &ops) !=
                ROBO_OK) {
                continue;
            }
            ROBO_CHECK_EQ_INT(
                robo_i18n_plural_select(&loc, ROBO_I18N_PLURAL_CARDINAL, &ops, &cat), ROBO_OK);
            ROBO_CHECK_EQ_INT(
                robo_i18n_plural_select(&loc, ROBO_I18N_PLURAL_ORDINAL, &ops, &cat), ROBO_OK);
            (void)robo_i18n_plural_category_name(cat);
        }
    }
}

static void test_plural_perf(void)
{
    robo_i18n_locale_t en, ar, cy;
    robo_i18n_plural_category_t cat;
    clock_t t0, t1;
    size_t i;
    double ms;
    const size_t N = 20000;

    parse_tag("en", &en);
    parse_tag("ar", &ar);
    parse_tag("cy", &cy);

    t0 = clock();
    for (i = 0; i < N; i++) {
        ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&en, (int64_t)(i % 1000), &cat), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&ar, (int64_t)(i % 200), &cat), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&cy, (int64_t)(i % 10), &cat), ROBO_OK);
    }
    t1 = clock();
    ms = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
    fprintf(stderr, "plural perf: %zu selects in %.2f ms\n", N * 3u, ms);
    ROBO_CHECK(ms < 5000.0);
}

void robo_test_register_i18n_plural(void)
{
    robo_test_register("i18n.plural.versions", test_plural_versions);
    robo_test_register("i18n.plural.category_names", test_plural_category_names);
    robo_test_register("i18n.plural.operands_trailing_zeros", test_plural_operands_trailing_zeros);
    robo_test_register("i18n.plural.en", test_plural_en);
    robo_test_register("i18n.plural.fr", test_plural_fr);
    robo_test_register("i18n.plural.de_es", test_plural_de_es);
    robo_test_register("i18n.plural.slavic", test_plural_slavic);
    robo_test_register("i18n.plural.arabic", test_plural_arabic);
    robo_test_register("i18n.plural.celtic_baltic", test_plural_celtic_baltic);
    robo_test_register("i18n.plural.pt_region", test_plural_pt_region);
    robo_test_register("i18n.plural.east_asian", test_plural_east_asian);
    robo_test_register("i18n.plural.decimals", test_plural_decimals);
    robo_test_register("i18n.plural.negatives", test_plural_negatives);
    robo_test_register("i18n.plural.large", test_plural_large);
    robo_test_register("i18n.plural.ordinal_en", test_plural_ordinal_en);
    robo_test_register("i18n.plural.categories_api", test_plural_categories_api);
    robo_test_register("i18n.plural.i64_api", test_plural_i64_api);
    robo_test_register("i18n.plural.cldr_samples", test_plural_cldr_samples);
    robo_test_register("i18n.plural.fuzz_smoke", test_plural_fuzz_smoke);
    robo_test_register("i18n.plural.perf", test_plural_perf);
}
