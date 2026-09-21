/*
 * RoboI18N ordinal plural rules (Goal 4.18).
 * Cardinal coverage remains in SuiteRoboI18NPlural.c (incl. CLDR samples).
 */

#include "RoboI18N/RoboI18N.h"
#include "RoboI18N/RoboI18NMessageFormat.h"
#include "RoboI18N/RoboI18NPlural.h"
#include "RoboI18N/RoboPluralData.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

static void ParseTag(const char *tag, I18nLocale *out)
{
    RoboCheckEqInt(I18nLocaleParseCanonical(tag, strlen(tag), out), RoboOk);
}

static void ExpectOrd(const char *tag, const char *input, const char *wantName)
{
    I18nLocale loc;
    I18nPluralOperands ops;
    I18nPluralCategory got;
    I18nPluralCategory want;

    ParseTag(tag, &loc);
    RoboCheckEqInt(I18nPluralCategoryFromName(wantName, &want), RoboOk);
    RoboCheckEqInt(I18nPluralOperandsFromString(input, strlen(input), &ops), RoboOk);
    RoboCheckEqInt(I18nPluralSelect(&loc, RoboI18nPluralOrdinal, &ops, &got),
                      RoboOk);
    if (got != want) {
        fprintf(stderr, "ORDINAL FAIL locale=%s input=%s expected=%s actual=%s\n", tag, input,
                wantName, I18nPluralCategoryName(got));
    }
    RoboCheckEqInt((int)got, (int)want);
}

static void TestOrdVersion(void)
{
    RoboCheckStrEq(I18nVersionString(), "0.27.0");
    RoboCheckStrEq(RoboPluralCldrVersion, "46");
    RoboCheck(PluralOrdinalIndexCount >= 100u);
}

static void TestOrdEn(void)
{
    ExpectOrd("en", "1", "one");
    ExpectOrd("en", "2", "two");
    ExpectOrd("en", "3", "few");
    ExpectOrd("en", "4", "other");
    ExpectOrd("en", "21", "one");
    ExpectOrd("en", "22", "two");
    ExpectOrd("en", "23", "few");
    ExpectOrd("en", "24", "other");
    ExpectOrd("en", "111", "other");
    ExpectOrd("en", "1.0", "one"); /* CLDR en ordinals key on i, not v */
}

static void TestOrdEuropean(void)
{
    /* French: 1 → one; else other */
    ExpectOrd("fr", "1", "one");
    ExpectOrd("fr", "2", "other");
    ExpectOrd("fr", "11", "other");
    ExpectOrd("de", "1", "other");
    ExpectOrd("de", "2", "other");
    ExpectOrd("es", "1", "other");
    ExpectOrd("es", "2", "other");
    ExpectOrd("it", "1", "other");
    ExpectOrd("it", "8", "many");
}

static void TestOrdSlavic(void)
{
    /* Ukrainian / Russian / Polish ordinals differ from cardinals */
    ExpectOrd("uk", "1", "other");
    ExpectOrd("uk", "3", "few");
    ExpectOrd("ru", "1", "other");
    ExpectOrd("ru", "3", "other");
    ExpectOrd("pl", "1", "other");
    ExpectOrd("pl", "3", "other");
}

static void TestOrdRtlIndicCjk(void)
{
    ExpectOrd("ar", "1", "other");
    ExpectOrd("ar", "2", "other");
    ExpectOrd("he", "1", "other");
    ExpectOrd("hi", "1", "one");
    ExpectOrd("hi", "2", "two");
    ExpectOrd("hi", "4", "few");
    ExpectOrd("hi", "6", "many");
    ExpectOrd("bn", "1", "one");
    ExpectOrd("bn", "2", "two");
    ExpectOrd("bn", "4", "few");
    ExpectOrd("bn", "6", "many");
    ExpectOrd("ja", "1", "other");
    ExpectOrd("zh", "1", "other");
    ExpectOrd("zh-Hans", "7", "other");
}

static void TestOrdVsCardinal(void)
{
    I18nLocale en;
    I18nPluralCategory card;
    I18nPluralCategory ord;

    ParseTag("en", &en);
    RoboCheckEqInt(I18nPluralCardinalI64(&en, 2, &card), RoboOk);
    RoboCheckEqInt(I18nPluralOrdinalI64(&en, 2, &ord), RoboOk);
    RoboCheckEqInt((int)card, (int)RoboI18nPluralOther);
    RoboCheckEqInt((int)ord, (int)RoboI18nPluralTwo);

    RoboCheckEqInt(I18nPluralCardinalI64(&en, 1, &card), RoboOk);
    RoboCheckEqInt(I18nPluralOrdinalI64(&en, 1, &ord), RoboOk);
    RoboCheckEqInt((int)card, (int)RoboI18nPluralOne);
    RoboCheckEqInt((int)ord, (int)RoboI18nPluralOne);
}

static void TestOrdApiAvailability(void)
{
    I18nLocale en;
    I18nLocale und;
    I18nPluralCategory cats[8];
    size_t n = 0;
    I18nPluralCategory cat;

    ParseTag("en", &en);
    RoboCheck(I18nPluralHasRules(&en, RoboI18nPluralOrdinal));
    RoboCheck(I18nPluralHasRules(&en, RoboI18nPluralCardinal));
    RoboCheckEqInt(
        I18nPluralCategories(&en, RoboI18nPluralOrdinal, cats, 8, &n), RoboOk);
    RoboCheck(n >= 2u);

    ParseTag("und", &und);
    /* und may resolve via parents; selection still safe */
    RoboCheckEqInt(I18nPluralOrdinalI64(&und, 1, &cat), RoboOk);
    RoboCheckEqInt(I18nPluralCategoryFromName("few", &cat), RoboOk);
    RoboCheckEqInt((int)cat, (int)RoboI18nPluralFew);
    RoboCheckEqInt(I18nPluralCategoryFromName("nope", &cat), RoboNotFound);
}

static void TestOrdFallbackRegion(void)
{
    /* en-GB should inherit English ordinals */
    ExpectOrd("en-GB", "1", "one");
    ExpectOrd("en-GB", "2", "two");
    ExpectOrd("en-US", "23", "few");
}

static void TestOrdNegativesDecimals(void)
{
    ExpectOrd("en", "-1", "one");
    ExpectOrd("en", "-2", "two");
    ExpectOrd("en", "1.00", "one");
    ExpectOrd("fr", "1.0", "one");
    ExpectOrd("fr", "2.0", "other");
}

static void TestOrdMfSelectordinal(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";

    ParseTag("en", &en);
    args.items = &arg;
    args.count = 1;

    I18nMfArgSetI64(&arg, "n", 1);
    RoboCheckEqInt(
        I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        RoboOk);
    RoboCheckStrEq(buf, "1st");

    I18nMfArgSetI64(&arg, "n", 2);
    RoboCheckEqInt(
        I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        RoboOk);
    RoboCheckStrEq(buf, "2nd");

    I18nMfArgSetI64(&arg, "n", 3);
    RoboCheckEqInt(
        I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        RoboOk);
    RoboCheckStrEq(buf, "3rd");

    I18nMfArgSetI64(&arg, "n", 11);
    RoboCheckEqInt(
        I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        RoboOk);
    RoboCheckStrEq(buf, "11th");
}

static void TestOrdMfExactBeforeCategory(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char buf[64];
    size_t n = 0;
    /* Exact =2 must win over ordinal "two" branch naming */
    const char *tmpl =
        "{n, selectordinal, =2 {exact-two} two {ord-two} other {ord-other}}";

    ParseTag("en", &en);
    I18nMfArgSetI64(&arg, "n", 2);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(
        I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        RoboOk);
    RoboCheckStrEq(buf, "exact-two");

    I18nMfArgSetI64(&arg, "n", 22);
    RoboCheckEqInt(
        I18nMfFormatTemplate(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        RoboOk);
    RoboCheckStrEq(buf, "ord-two");
}

static void TestOrdMfNotCardinal(void)
{
    I18nLocale en;
    I18nMfArg arg;
    I18nMfArgs args;
    char bufOrd[64];
    char bufCard[64];
    size_t n = 0;
    const char *Ord = "{n, selectordinal, one {O} other {X}}";
    const char *Card = "{n, plural, one {O} other {X}}";

    ParseTag("en", &en);
    I18nMfArgSetI64(&arg, "n", 2);
    args.items = &arg;
    args.count = 1;
    RoboCheckEqInt(
        I18nMfFormatTemplate(Ord, strlen(Ord), &en, &args, bufOrd, sizeof(bufOrd),
                                     &n),
        RoboOk);
    RoboCheckEqInt(
        I18nMfFormatTemplate(Card, strlen(Card), &en, &args, bufCard,
                                     sizeof(bufCard), &n),
        RoboOk);
    /* ordinal 2 → two category → other branch; cardinal 2 → other */
    RoboCheckStrEq(bufOrd, "X");
    RoboCheckStrEq(bufCard, "X");

    I18nMfArgSetI64(&arg, "n", 1);
    RoboCheckEqInt(
        I18nMfFormatTemplate(Ord, strlen(Ord), &en, &args, bufOrd, sizeof(bufOrd),
                                     &n),
        RoboOk);
    RoboCheckStrEq(bufOrd, "O");
}

static void TestOrdFuzz(void)
{
    static const char *tags[] = {"en", "fr", "ru", "ar", "hi", "bn", "ja", "zh", "und"};
    size_t ti;
    int64_t v;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        I18nLocale loc;
        if (I18nLocaleParseCanonical(tags[ti], strlen(tags[ti]), &loc) != RoboOk) {
            continue;
        }
        for (v = -50; v <= 250; v++) {
            I18nPluralCategory cat;
            RoboCheckEqInt(I18nPluralOrdinalI64(&loc, v, &cat), RoboOk);
            RoboCheck((unsigned)cat <= (unsigned)RoboI18nPluralOther);
        }
    }
}

void TestRegisterI18nOrdinal(void)
{
    TestRegister("i18n.ord.version", TestOrdVersion);
    TestRegister("i18n.ord.en", TestOrdEn);
    TestRegister("i18n.ord.european", TestOrdEuropean);
    TestRegister("i18n.ord.slavic", TestOrdSlavic);
    TestRegister("i18n.ord.rtl_indic_cjk", TestOrdRtlIndicCjk);
    TestRegister("i18n.ord.vs_cardinal", TestOrdVsCardinal);
    TestRegister("i18n.ord.api", TestOrdApiAvailability);
    TestRegister("i18n.ord.fallback", TestOrdFallbackRegion);
    TestRegister("i18n.ord.neg_dec", TestOrdNegativesDecimals);
    TestRegister("i18n.ord.mf_selectordinal", TestOrdMfSelectordinal);
    TestRegister("i18n.ord.mf_exact", TestOrdMfExactBeforeCategory);
    TestRegister("i18n.ord.mf_not_cardinal", TestOrdMfNotCardinal);
    TestRegister("i18n.ord.fuzz", TestOrdFuzz);
}
