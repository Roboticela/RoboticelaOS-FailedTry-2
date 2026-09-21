/*
 * RoboI18N plural-rules suite (Goal 4.4).
 *
 * Samples: Tests/Data/robo_plural_samples.txt (CLDR @integer/@decimal via RoboPluralSamplesPath).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboPluralData.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef RoboPluralSamplesPathFromBuild
#include "robo_plural_samples_path.h"
#endif

#ifndef RoboPluralSamplesPath
#define RoboPluralSamplesPath ""
#endif

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static I18nPluralCategory CatFromName(const char *name)
{
    size_t i;
    for (i = 0; i < 6; i++) {
        if (strcmp(I18nPluralCategoryName((I18nPluralCategory)i), name) == 0) {
            return (I18nPluralCategory)i;
        }
    }
    return RoboI18nPluralOther;
}

static void ExpectCardinal(const char *tag, const char *input, const char *wantName)
{
    I18nLocale loc;
    I18nPluralOperands ops;
    I18nPluralCategory got;
    I18nPluralCategory want = CatFromName(wantName);

    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nPluralOperandsFromString(input, strlen(input), &ops), RoboOk);
    RoboCheckEqInt(I18nPluralSelect(&loc, RoboI18nPluralCardinal, &ops, &got), RoboOk);
    if (got != want) {
        fprintf(stderr,
                "PLURAL FAIL locale=%s input=%s expected=%s actual=%s "
                "ops: i=%.*s v=%u w=%u f=%.*s e=%u neg=%u\n",
                tag,
                input,
                wantName,
                I18nPluralCategoryName(got),
                (int)ops.iLen,
                ops.iDigits,
                (unsigned)ops.v,
                (unsigned)ops.w,
                (int)ops.v,
                ops.fDigits,
                (unsigned)ops.e,
                (unsigned)ops.negativeInput);
    }
    RoboCheckEqInt((int)got, (int)want);
}

static void ExpectOrdinal(const char *tag, const char *input, const char *wantName)
{
    I18nLocale loc;
    I18nPluralOperands ops;
    I18nPluralCategory got;
    I18nPluralCategory want = CatFromName(wantName);

    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nPluralOperandsFromString(input, strlen(input), &ops), RoboOk);
    RoboCheckEqInt(I18nPluralSelect(&loc, RoboI18nPluralOrdinal, &ops, &got), RoboOk);
    if (got != want) {
        fprintf(stderr,
                "ORDINAL FAIL locale=%s input=%s expected=%s actual=%s "
                "ops: i=%.*s v=%u w=%u f=%.*s e=%u neg=%u\n",
                tag,
                input,
                wantName,
                I18nPluralCategoryName(got),
                (int)ops.iLen,
                ops.iDigits,
                (unsigned)ops.v,
                (unsigned)ops.w,
                (int)ops.v,
                ops.fDigits,
                (unsigned)ops.e,
                (unsigned)ops.negativeInput);
    }
    RoboCheckEqInt((int)got, (int)want);
}

static void TestPluralVersions(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(I18nCldrVersionString(), "46");
    RoboCheckStrEq(RoboPluralCldrVersion, "46");
    RoboCheckStrEq(RoboPluralUnicodeVersion, "16.0.0");
    RoboCheck(PluralCardinalIndexCount >= 200u);
    RoboCheck(PluralOrdinalIndexCount >= 100u);
    RoboCheck(PluralBytecodeSize > 0u);
}

static void TestPluralCategoryNames(void)
{
    RoboCheckStrEq(I18nPluralCategoryName(RoboI18nPluralZero), "zero");
    RoboCheckStrEq(I18nPluralCategoryName(RoboI18nPluralOne), "one");
    RoboCheckStrEq(I18nPluralCategoryName(RoboI18nPluralTwo), "two");
    RoboCheckStrEq(I18nPluralCategoryName(RoboI18nPluralFew), "few");
    RoboCheckStrEq(I18nPluralCategoryName(RoboI18nPluralMany), "many");
    RoboCheckStrEq(I18nPluralCategoryName(RoboI18nPluralOther), "other");
}

static void TestPluralOperandsTrailingZeros(void)
{
    I18nPluralOperands a, b, c;
    RoboCheckEqInt(I18nPluralOperandsFromString("1", 1, &a), RoboOk);
    RoboCheckEqInt(I18nPluralOperandsFromString("1.0", 3, &b), RoboOk);
    RoboCheckEqInt(I18nPluralOperandsFromString("1.00", 4, &c), RoboOk);
    RoboCheckEqInt((int)a.v, 0);
    RoboCheckEqInt((int)b.v, 1);
    RoboCheckEqInt((int)c.v, 2);
    RoboCheckEqInt((int)a.w, 0);
    RoboCheckEqInt((int)b.w, 0);
    RoboCheckEqInt((int)c.w, 0);
    RoboCheckEqInt((int)a.iLen, 1);
    RoboCheckEqInt((int)b.iLen, 1);
    RoboCheckEqInt((int)c.iLen, 1);
}

static void TestPluralEn(void)
{
    ExpectCardinal("en", "0", "other");
    ExpectCardinal("en", "1", "one");
    ExpectCardinal("en", "2", "other");
    ExpectCardinal("en", "1.0", "other");
    ExpectCardinal("en", "1.00", "other");
    ExpectCardinal("en-US", "1", "one");
    ExpectCardinal("en-GB", "21", "other");
}

static void TestPluralFr(void)
{
    ExpectCardinal("fr", "0", "one");
    ExpectCardinal("fr", "1", "one");
    ExpectCardinal("fr", "1.0", "one");
    ExpectCardinal("fr", "1.1", "one");
    ExpectCardinal("fr", "2", "other");
    ExpectCardinal("fr", "1000000", "many");
}

static void TestPluralDeEs(void)
{
    ExpectCardinal("de", "0", "other");
    ExpectCardinal("de", "1", "one");
    ExpectCardinal("de", "1.0", "other");
    ExpectCardinal("es", "1", "one");
    ExpectCardinal("es", "1.0", "one");
    ExpectCardinal("es", "1.1", "other");
    ExpectCardinal("es", "1000000", "many");
}

static void TestPluralSlavic(void)
{
    /* Russian */
    ExpectCardinal("ru", "1", "one");
    ExpectCardinal("ru", "21", "one");
    ExpectCardinal("ru", "2", "few");
    ExpectCardinal("ru", "3", "few");
    ExpectCardinal("ru", "4", "few");
    ExpectCardinal("ru", "22", "few");
    ExpectCardinal("ru", "5", "many");
    ExpectCardinal("ru", "11", "many");
    ExpectCardinal("ru", "12", "many");
    ExpectCardinal("ru", "14", "many");
    ExpectCardinal("ru", "25", "many");
    ExpectCardinal("ru", "100", "many");
    ExpectCardinal("ru", "101", "one");
    ExpectCardinal("ru", "102", "few");
    ExpectCardinal("ru", "111", "many");
    ExpectCardinal("ru", "112", "many");
    ExpectCardinal("ru", "1.5", "other");

    /* Ukrainian */
    ExpectCardinal("uk", "1", "one");
    ExpectCardinal("uk", "2", "few");
    ExpectCardinal("uk", "5", "many");
    ExpectCardinal("uk", "11", "many");
    ExpectCardinal("uk", "22", "few");

    /* Polish */
    ExpectCardinal("pl", "1", "one");
    ExpectCardinal("pl", "2", "few");
    ExpectCardinal("pl", "3", "few");
    ExpectCardinal("pl", "4", "few");
    ExpectCardinal("pl", "5", "many");
    ExpectCardinal("pl", "12", "many");
    ExpectCardinal("pl", "22", "few");
    ExpectCardinal("pl", "25", "many");
    ExpectCardinal("pl", "1.5", "other");

    /* Czech / Slovak / Slovenian */
    ExpectCardinal("cs", "1", "one");
    ExpectCardinal("cs", "2", "few");
    ExpectCardinal("cs", "5", "other");
    ExpectCardinal("sk", "1", "one");
    ExpectCardinal("sk", "2", "few");
    ExpectCardinal("sl", "1", "one");
    ExpectCardinal("sl", "2", "two");
    ExpectCardinal("sl", "3", "few");
    ExpectCardinal("sl", "5", "other");
}

static void TestPluralArabic(void)
{
    ExpectCardinal("ar", "0", "zero");
    ExpectCardinal("ar", "1", "one");
    ExpectCardinal("ar", "2", "two");
    ExpectCardinal("ar", "3", "few");
    ExpectCardinal("ar", "10", "few");
    ExpectCardinal("ar", "11", "many");
    ExpectCardinal("ar", "99", "many");
    ExpectCardinal("ar", "100", "other");
    ExpectCardinal("ar", "101", "other");
    ExpectCardinal("ar", "102", "other");
    ExpectCardinal("ar", "111", "many");
    ExpectCardinal("ar", "0.0", "zero");
    ExpectCardinal("ar", "1.0", "one");
    ExpectCardinal("ar", "1.00", "one");
    ExpectCardinal("ar", "2.0", "two");
    ExpectCardinal("ar-SA", "3", "few");
    ExpectCardinal("ar-EG", "11", "many");
}

static void TestPluralCelticBaltic(void)
{
    ExpectCardinal("he", "1", "one");
    ExpectCardinal("he", "2", "two");
    ExpectCardinal("he", "20", "other");
    ExpectCardinal("ga", "1", "one");
    ExpectCardinal("ga", "2", "two");
    ExpectCardinal("ga", "3", "few");
    ExpectCardinal("ga", "7", "many");
    ExpectCardinal("cy", "0", "zero");
    ExpectCardinal("cy", "1", "one");
    ExpectCardinal("cy", "2", "two");
    ExpectCardinal("cy", "3", "few");
    ExpectCardinal("cy", "6", "many");
    ExpectCardinal("ro", "1", "one");
    ExpectCardinal("ro", "0", "few");
    ExpectCardinal("ro", "2", "few");
    ExpectCardinal("ro", "20", "other");
    ExpectCardinal("lt", "1", "one");
    ExpectCardinal("lt", "2", "few");
    ExpectCardinal("lt", "10", "other");
    ExpectCardinal("lv", "0", "zero");
    ExpectCardinal("lv", "1", "one");
    ExpectCardinal("lv", "2", "other");
}

static void TestPluralPtRegion(void)
{
    /* Brazilian Portuguese treats 0 as "one"; European Portuguese does not. */
    ExpectCardinal("pt", "0", "one");
    ExpectCardinal("pt-BR", "0", "one");
    ExpectCardinal("pt-PT", "0", "other");
    ExpectCardinal("pt-PT", "1", "one");
}

static void TestPluralEastAsian(void)
{
    ExpectCardinal("ja", "0", "other");
    ExpectCardinal("ja", "1", "other");
    ExpectCardinal("ja", "2", "other");
    ExpectCardinal("zh", "1", "other");
    ExpectCardinal("zh-Hans", "5", "other");
    ExpectCardinal("ko", "1", "other");
    ExpectCardinal("th", "1", "other");
}

static void TestPluralDecimals(void)
{
    ExpectCardinal("en", "0.0", "other");
    ExpectCardinal("en", "0.1", "other");
    ExpectCardinal("en", "0.5", "other");
    ExpectCardinal("en", "1.1", "other");
    ExpectCardinal("en", "1.5", "other");
    ExpectCardinal("en", "2.0", "other");
    ExpectCardinal("en", "10.0", "other");
    ExpectCardinal("en", "10.1", "other");
    ExpectCardinal("ru", "2.0", "other");
    ExpectCardinal("ru", "5.0", "other");
    ExpectCardinal("ru", "5.5", "other");
    ExpectCardinal("pl", "2.0", "other");
    ExpectCardinal("fr", "2.0", "other");
    ExpectCardinal("fr", "2.1", "other");
}

static void TestPluralNegatives(void)
{
    /* CLDR operands use absolute value. */
    ExpectCardinal("en", "-1", "one");
    ExpectCardinal("en", "-2", "other");
    ExpectCardinal("ru", "-1", "one");
    ExpectCardinal("ru", "-2", "few");
    ExpectCardinal("ar", "-1", "one");
    ExpectCardinal("ar", "-2", "two");
    ExpectCardinal("en", "-1.5", "other");
    ExpectCardinal("en", "-2.0", "other");
}

static void TestPluralLarge(void)
{
    ExpectCardinal("en", "1000000000000000000", "other");
    ExpectCardinal("fr", "1000000", "many");
    ExpectCardinal("fr", "2000000", "many");
    ExpectCardinal("ru", "1000001", "one");
    ExpectCardinal("ar", "10000000011", "many");
}

static void TestPluralOrdinalEn(void)
{
    ExpectOrdinal("en", "1", "one");
    ExpectOrdinal("en", "2", "two");
    ExpectOrdinal("en", "3", "few");
    ExpectOrdinal("en", "4", "other");
    ExpectOrdinal("en", "21", "one");
    ExpectOrdinal("en", "22", "two");
    ExpectOrdinal("en", "23", "few");
    ExpectOrdinal("en", "24", "other");
}

static void TestPluralCategoriesApi(void)
{
    I18nLocale loc;
    I18nPluralCategory cats[8];
    size_t n = 0;
    size_t i;
    int sawOther = 0;

    ParseTag("ar", &loc);
    RoboCheckEqInt(
        I18nPluralCategories(&loc, RoboI18nPluralCardinal, cats, 8, &n), RoboOk);
    RoboCheck(n >= 6u);
    for (i = 0; i < n && i < 8; i++) {
        if (cats[i] == RoboI18nPluralOther) {
            sawOther = 1;
        }
    }
    RoboCheck(sawOther);

    ParseTag("ja", &loc);
    RoboCheckEqInt(
        I18nPluralCategories(&loc, RoboI18nPluralCardinal, cats, 8, &n), RoboOk);
    RoboCheckEqInt((int)n, 1);
    RoboCheckEqInt((int)cats[0], (int)RoboI18nPluralOther);
}

static void TestPluralI64Api(void)
{
    I18nLocale loc;
    I18nPluralCategory cat;
    ParseTag("en", &loc);
    RoboCheckEqInt(I18nPluralCardinalI64(&loc, 1, &cat), RoboOk);
    RoboCheckEqInt((int)cat, (int)RoboI18nPluralOne);
    RoboCheckEqInt(I18nPluralCardinalI64(&loc, -1, &cat), RoboOk);
    RoboCheckEqInt((int)cat, (int)RoboI18nPluralOne);
}

static int LoadAndRunSamples(void)
{
    const char *path = RoboPluralSamplesPath;
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
        I18nLocale loc;
        I18nPluralOperands ops;
        I18nPluralCategory got;
        I18nPluralCategory want;
        I18nPluralKind pk;
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
            pk = RoboI18nPluralCardinal;
        } else if (strcmp(kind, "ordinal") == 0) {
            pk = RoboI18nPluralOrdinal;
        } else {
            skipped++;
            continue;
        }
        if (I18nLocaleParseCanonical(locale, strlen(locale), &loc) != RoboOk) {
            failures++;
            continue;
        }
        if (I18nPluralOperandsFromString(input, strlen(input), &ops) != RoboOk) {
            /* Extremely long / unsupported sample — skip safely. */
            skipped++;
            continue;
        }
        want = CatFromName(expect);
        if (I18nPluralSelect(&loc, pk, &ops, &got) != RoboOk) {
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
                        I18nPluralCategoryName(got),
                        (int)ops.iLen,
                        ops.iDigits,
                        (unsigned)ops.v,
                        (unsigned)ops.w,
                        (int)ops.v,
                        ops.fDigits,
                        (unsigned)ops.e);
            }
        }
    }
    fclose(f);
    fprintf(stderr, "plural samples: total=%d failures=%d skipped=%d\n", total, failures, skipped);
    return failures;
}

static void TestPluralCldrSamples(void)
{
    int failures = LoadAndRunSamples();
    RoboCheck(failures == 0);
}

static void TestPluralFuzzSmoke(void)
{
    static const char *tags[] = {"en", "ar", "ru", "pl", "fr", "ja", "xx-YY", "und"};
    static const char *inputs[] = {
        "", "-", ".", "1.", ".1", "++1", "1..2", "9999999999999999999999999999999999999999",
        "1.000000000000000000000000000000000000000", "-0.000", "0001.00", "abc", "1e3", "NaN"
    };
    size_t ti, ii;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        I18nLocale loc;
        if (I18nLocaleParseCanonical(tags[ti], strlen(tags[ti]), &loc) != RoboOk) {
            continue;
        }
        for (ii = 0; ii < sizeof(inputs) / sizeof(inputs[0]); ii++) {
            I18nPluralOperands ops;
            I18nPluralCategory cat;
            if (I18nPluralOperandsFromString(inputs[ii], strlen(inputs[ii]), &ops) !=
                RoboOk) {
                continue;
            }
            RoboCheckEqInt(
                I18nPluralSelect(&loc, RoboI18nPluralCardinal, &ops, &cat), RoboOk);
            RoboCheckEqInt(
                I18nPluralSelect(&loc, RoboI18nPluralOrdinal, &ops, &cat), RoboOk);
            (void)I18nPluralCategoryName(cat);
        }
    }
}

static void TestPluralPerf(void)
{
    I18nLocale en, ar, cy;
    I18nPluralCategory cat;
    clock_t t0, t1;
    size_t i;
    double ms;
    const size_t N = 20000;

    ParseTag("en", &en);
    ParseTag("ar", &ar);
    ParseTag("cy", &cy);

    t0 = clock();
    for (i = 0; i < N; i++) {
        RoboCheckEqInt(I18nPluralCardinalI64(&en, (int64_t)(i % 1000), &cat), RoboOk);
        RoboCheckEqInt(I18nPluralCardinalI64(&ar, (int64_t)(i % 200), &cat), RoboOk);
        RoboCheckEqInt(I18nPluralCardinalI64(&cy, (int64_t)(i % 10), &cat), RoboOk);
    }
    t1 = clock();
    ms = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
    fprintf(stderr, "plural perf: %zu selects in %.2f ms\n", N * 3u, ms);
    RoboCheck(ms < 5000.0);
}

void TestRegisterI18nPlural(void)
{
    TestRegister("i18n.plural.versions", TestPluralVersions);
    TestRegister("i18n.plural.category_names", TestPluralCategoryNames);
    TestRegister("i18n.plural.operands_trailing_zeros", TestPluralOperandsTrailingZeros);
    TestRegister("i18n.plural.en", TestPluralEn);
    TestRegister("i18n.plural.fr", TestPluralFr);
    TestRegister("i18n.plural.de_es", TestPluralDeEs);
    TestRegister("i18n.plural.slavic", TestPluralSlavic);
    TestRegister("i18n.plural.arabic", TestPluralArabic);
    TestRegister("i18n.plural.celtic_baltic", TestPluralCelticBaltic);
    TestRegister("i18n.plural.pt_region", TestPluralPtRegion);
    TestRegister("i18n.plural.east_asian", TestPluralEastAsian);
    TestRegister("i18n.plural.decimals", TestPluralDecimals);
    TestRegister("i18n.plural.negatives", TestPluralNegatives);
    TestRegister("i18n.plural.large", TestPluralLarge);
    TestRegister("i18n.plural.ordinal_en", TestPluralOrdinalEn);
    TestRegister("i18n.plural.categories_api", TestPluralCategoriesApi);
    TestRegister("i18n.plural.i64_api", TestPluralI64Api);
    TestRegister("i18n.plural.cldr_samples", TestPluralCldrSamples);
    TestRegister("i18n.plural.fuzz_smoke", TestPluralFuzzSmoke);
    TestRegister("i18n.plural.perf", TestPluralPerf);
}
