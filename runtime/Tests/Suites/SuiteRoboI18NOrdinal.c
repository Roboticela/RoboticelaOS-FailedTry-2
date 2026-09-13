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

static void parse_tag(const char *tag, robo_i18n_locale_t *out)
{
    ROBO_CHECK_EQ_INT(robo_i18n_locale_parse_canonical(tag, strlen(tag), out), ROBO_OK);
}

static void expect_ord(const char *tag, const char *input, const char *want_name)
{
    robo_i18n_locale_t loc;
    robo_i18n_plural_operands_t ops;
    robo_i18n_plural_category_t got;
    robo_i18n_plural_category_t want;

    parse_tag(tag, &loc);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_category_from_name(want_name, &want), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_operands_from_string(input, strlen(input), &ops), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_select(&loc, ROBO_I18N_PLURAL_ORDINAL, &ops, &got),
                      ROBO_OK);
    if (got != want) {
        fprintf(stderr, "ORDINAL FAIL locale=%s input=%s expected=%s actual=%s\n", tag, input,
                want_name, robo_i18n_plural_category_name(got));
    }
    ROBO_CHECK_EQ_INT((int)got, (int)want);
}

static void test_ord_version(void)
{
    ROBO_CHECK_STR_EQ(robo_i18n_version_string(), "0.20.0");
    ROBO_CHECK_STR_EQ(ROBO_PLURAL_CLDR_VERSION, "46");
    ROBO_CHECK(robo_plural_ordinal_index_count >= 100u);
}

static void test_ord_en(void)
{
    expect_ord("en", "1", "one");
    expect_ord("en", "2", "two");
    expect_ord("en", "3", "few");
    expect_ord("en", "4", "other");
    expect_ord("en", "21", "one");
    expect_ord("en", "22", "two");
    expect_ord("en", "23", "few");
    expect_ord("en", "24", "other");
    expect_ord("en", "111", "other");
    expect_ord("en", "1.0", "one"); /* CLDR en ordinals key on i, not v */
}

static void test_ord_european(void)
{
    /* French: 1 → one; else other */
    expect_ord("fr", "1", "one");
    expect_ord("fr", "2", "other");
    expect_ord("fr", "11", "other");
    expect_ord("de", "1", "other");
    expect_ord("de", "2", "other");
    expect_ord("es", "1", "other");
    expect_ord("es", "2", "other");
    expect_ord("it", "1", "other");
    expect_ord("it", "8", "many");
}

static void test_ord_slavic(void)
{
    /* Ukrainian / Russian / Polish ordinals differ from cardinals */
    expect_ord("uk", "1", "other");
    expect_ord("uk", "3", "few");
    expect_ord("ru", "1", "other");
    expect_ord("ru", "3", "other");
    expect_ord("pl", "1", "other");
    expect_ord("pl", "3", "other");
}

static void test_ord_rtl_indic_cjk(void)
{
    expect_ord("ar", "1", "other");
    expect_ord("ar", "2", "other");
    expect_ord("he", "1", "other");
    expect_ord("hi", "1", "one");
    expect_ord("hi", "2", "two");
    expect_ord("hi", "4", "few");
    expect_ord("hi", "6", "many");
    expect_ord("bn", "1", "one");
    expect_ord("bn", "2", "two");
    expect_ord("bn", "4", "few");
    expect_ord("bn", "6", "many");
    expect_ord("ja", "1", "other");
    expect_ord("zh", "1", "other");
    expect_ord("zh-Hans", "7", "other");
}

static void test_ord_vs_cardinal(void)
{
    robo_i18n_locale_t en;
    robo_i18n_plural_category_t card;
    robo_i18n_plural_category_t ord;

    parse_tag("en", &en);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&en, 2, &card), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_ordinal_i64(&en, 2, &ord), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)card, (int)ROBO_I18N_PLURAL_OTHER);
    ROBO_CHECK_EQ_INT((int)ord, (int)ROBO_I18N_PLURAL_TWO);

    ROBO_CHECK_EQ_INT(robo_i18n_plural_cardinal_i64(&en, 1, &card), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_ordinal_i64(&en, 1, &ord), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)card, (int)ROBO_I18N_PLURAL_ONE);
    ROBO_CHECK_EQ_INT((int)ord, (int)ROBO_I18N_PLURAL_ONE);
}

static void test_ord_api_availability(void)
{
    robo_i18n_locale_t en;
    robo_i18n_locale_t und;
    robo_i18n_plural_category_t cats[8];
    size_t n = 0;
    robo_i18n_plural_category_t cat;

    parse_tag("en", &en);
    ROBO_CHECK(robo_i18n_plural_has_rules(&en, ROBO_I18N_PLURAL_ORDINAL));
    ROBO_CHECK(robo_i18n_plural_has_rules(&en, ROBO_I18N_PLURAL_CARDINAL));
    ROBO_CHECK_EQ_INT(
        robo_i18n_plural_categories(&en, ROBO_I18N_PLURAL_ORDINAL, cats, 8, &n), ROBO_OK);
    ROBO_CHECK(n >= 2u);

    parse_tag("und", &und);
    /* und may resolve via parents; selection still safe */
    ROBO_CHECK_EQ_INT(robo_i18n_plural_ordinal_i64(&und, 1, &cat), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_category_from_name("few", &cat), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)cat, (int)ROBO_I18N_PLURAL_FEW);
    ROBO_CHECK_EQ_INT(robo_i18n_plural_category_from_name("nope", &cat), ROBO_NOT_FOUND);
}

static void test_ord_fallback_region(void)
{
    /* en-GB should inherit English ordinals */
    expect_ord("en-GB", "1", "one");
    expect_ord("en-GB", "2", "two");
    expect_ord("en-US", "23", "few");
}

static void test_ord_negatives_decimals(void)
{
    expect_ord("en", "-1", "one");
    expect_ord("en", "-2", "two");
    expect_ord("en", "1.00", "one");
    expect_ord("fr", "1.0", "one");
    expect_ord("fr", "2.0", "other");
}

static void test_ord_mf_selectordinal(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[64];
    size_t n = 0;
    const char *tmpl = "{n, selectordinal, one {#st} two {#nd} few {#rd} other {#th}}";

    parse_tag("en", &en);
    args.items = &arg;
    args.count = 1;

    robo_i18n_mf_arg_set_i64(&arg, "n", 1);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "1st");

    robo_i18n_mf_arg_set_i64(&arg, "n", 2);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "2nd");

    robo_i18n_mf_arg_set_i64(&arg, "n", 3);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "3rd");

    robo_i18n_mf_arg_set_i64(&arg, "n", 11);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "11th");
}

static void test_ord_mf_exact_before_category(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf[64];
    size_t n = 0;
    /* Exact =2 must win over ordinal "two" branch naming */
    const char *tmpl =
        "{n, selectordinal, =2 {exact-two} two {ord-two} other {ord-other}}";

    parse_tag("en", &en);
    robo_i18n_mf_arg_set_i64(&arg, "n", 2);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "exact-two");

    robo_i18n_mf_arg_set_i64(&arg, "n", 22);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(tmpl, strlen(tmpl), &en, &args, buf, sizeof(buf), &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf, "ord-two");
}

static void test_ord_mf_not_cardinal(void)
{
    robo_i18n_locale_t en;
    robo_i18n_mf_arg_t arg;
    robo_i18n_mf_args_t args;
    char buf_ord[64];
    char buf_card[64];
    size_t n = 0;
    const char *ord_t = "{n, selectordinal, one {O} other {X}}";
    const char *card_t = "{n, plural, one {O} other {X}}";

    parse_tag("en", &en);
    robo_i18n_mf_arg_set_i64(&arg, "n", 2);
    args.items = &arg;
    args.count = 1;
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(ord_t, strlen(ord_t), &en, &args, buf_ord, sizeof(buf_ord),
                                     &n),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(card_t, strlen(card_t), &en, &args, buf_card,
                                     sizeof(buf_card), &n),
        ROBO_OK);
    /* ordinal 2 → two category → other branch; cardinal 2 → other */
    ROBO_CHECK_STR_EQ(buf_ord, "X");
    ROBO_CHECK_STR_EQ(buf_card, "X");

    robo_i18n_mf_arg_set_i64(&arg, "n", 1);
    ROBO_CHECK_EQ_INT(
        robo_i18n_mf_format_template(ord_t, strlen(ord_t), &en, &args, buf_ord, sizeof(buf_ord),
                                     &n),
        ROBO_OK);
    ROBO_CHECK_STR_EQ(buf_ord, "O");
}

static void test_ord_fuzz(void)
{
    static const char *tags[] = {"en", "fr", "ru", "ar", "hi", "bn", "ja", "zh", "und"};
    size_t ti;
    int64_t v;
    for (ti = 0; ti < sizeof(tags) / sizeof(tags[0]); ti++) {
        robo_i18n_locale_t loc;
        if (robo_i18n_locale_parse_canonical(tags[ti], strlen(tags[ti]), &loc) != ROBO_OK) {
            continue;
        }
        for (v = -50; v <= 250; v++) {
            robo_i18n_plural_category_t cat;
            ROBO_CHECK_EQ_INT(robo_i18n_plural_ordinal_i64(&loc, v, &cat), ROBO_OK);
            ROBO_CHECK((unsigned)cat <= (unsigned)ROBO_I18N_PLURAL_OTHER);
        }
    }
}

void robo_test_register_i18n_ordinal(void)
{
    robo_test_register("i18n.ord.version", test_ord_version);
    robo_test_register("i18n.ord.en", test_ord_en);
    robo_test_register("i18n.ord.european", test_ord_european);
    robo_test_register("i18n.ord.slavic", test_ord_slavic);
    robo_test_register("i18n.ord.rtl_indic_cjk", test_ord_rtl_indic_cjk);
    robo_test_register("i18n.ord.vs_cardinal", test_ord_vs_cardinal);
    robo_test_register("i18n.ord.api", test_ord_api_availability);
    robo_test_register("i18n.ord.fallback", test_ord_fallback_region);
    robo_test_register("i18n.ord.neg_dec", test_ord_negatives_decimals);
    robo_test_register("i18n.ord.mf_selectordinal", test_ord_mf_selectordinal);
    robo_test_register("i18n.ord.mf_exact", test_ord_mf_exact_before_category);
    robo_test_register("i18n.ord.mf_not_cardinal", test_ord_mf_not_cardinal);
    robo_test_register("i18n.ord.fuzz", test_ord_fuzz);
}
