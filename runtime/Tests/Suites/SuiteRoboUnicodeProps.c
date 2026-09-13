/*
 * RoboUnicode property suite (Goal 3.4) — UCD lookups.
 */

#include "RoboUnicode/RoboUnicodeProps.h"
#include "RoboTest.h"

#include <string.h>

static void test_ucd_version(void)
{
    ROBO_CHECK_STR_EQ(robo_unicode_ucd_version_string(), "16.0.0");
    ROBO_CHECK_STR_EQ(robo_unicode_version_string(), "16.0.0");
}

static void test_ucd_ascii_latin(void)
{
    ROBO_CHECK_EQ_INT(robo_unicode_category('A'), ROBO_U_GC_LU);
    ROBO_CHECK_EQ_INT(robo_unicode_category('z'), ROBO_U_GC_LL);
    ROBO_CHECK_EQ_INT(robo_unicode_category('0'), ROBO_U_GC_ND);
    ROBO_CHECK_EQ_INT(robo_unicode_category(' '), ROBO_U_GC_ZS);
    ROBO_CHECK(robo_unicode_is_alphabetic('A'));
    ROBO_CHECK(robo_unicode_is_uppercase('A'));
    ROBO_CHECK(robo_unicode_is_lowercase('a'));
    ROBO_CHECK(!robo_unicode_is_uppercase('a'));
    ROBO_CHECK(robo_unicode_is_whitespace(' '));
    ROBO_CHECK(robo_unicode_is_whitespace('\t'));
    ROBO_CHECK(robo_unicode_is_whitespace('\n'));
    ROBO_CHECK(!robo_unicode_is_whitespace('A'));
    ROBO_CHECK_EQ_INT(robo_unicode_script('A'), ROBO_U_SCRIPT_LATIN);
    ROBO_CHECK_EQ_INT(robo_unicode_combining_class('A'), 0);
    ROBO_CHECK_STR_EQ(robo_unicode_category_name(ROBO_U_GC_LU), "Lu");
}

static void test_ucd_scripts(void)
{
    /* Arabic letter ا U+0627 */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0627), ROBO_U_SCRIPT_ARABIC);
    ROBO_CHECK(robo_unicode_is_alphabetic(0x0627));
    /* Hebrew א */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x05D0), ROBO_U_SCRIPT_HEBREW);
    /* Devanagari अ */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0905), ROBO_U_SCRIPT_DEVANAGARI);
    /* Bengali অ */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0985), ROBO_U_SCRIPT_BENGALI);
    /* Thai ก */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0E01), ROBO_U_SCRIPT_THAI);
    /* Han 中 */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x4E2D), ROBO_U_SCRIPT_HAN);
    /* Hiragana あ */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x3042), ROBO_U_SCRIPT_HIRAGANA);
    /* Hangul 한 */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0xD55C), ROBO_U_SCRIPT_HANGUL);
    /* Common digit */
    ROBO_CHECK_EQ_INT(robo_unicode_script('5'), ROBO_U_SCRIPT_COMMON);
}

static void test_ucd_urdu_persian(void)
{
    /* Urdu/Persian Arabic-script letters: پ U+067E, گ U+06AF, ھ U+06BE */
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x067E), ROBO_U_SCRIPT_ARABIC);
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x06AF), ROBO_U_SCRIPT_ARABIC);
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x06BE), ROBO_U_SCRIPT_ARABIC);
    ROBO_CHECK(robo_unicode_is_alphabetic(0x067E));
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x067E), ROBO_U_GC_LO);
}

static void test_ucd_combining_and_marks(void)
{
    /* Combining acute U+0301, CCC=230 */
    ROBO_CHECK_EQ_INT(robo_unicode_combining_class(0x0301), 230);
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x0301), ROBO_U_GC_MN);
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0301), ROBO_U_SCRIPT_INHERITED);
    /* Devanagari virama U+094D */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x094D), ROBO_U_GC_MN);
    ROBO_CHECK(robo_unicode_combining_class(0x094D) != 0);
}

static void test_ucd_cjk_eaw(void)
{
    ROBO_CHECK_EQ_INT(robo_unicode_east_asian_width(0x4E2D), ROBO_U_EAW_WIDE);
    ROBO_CHECK_EQ_INT(robo_unicode_east_asian_width('A'), ROBO_U_EAW_NARROW);
    /* Fullwidth A */
    ROBO_CHECK_EQ_INT(robo_unicode_east_asian_width(0xFF21), ROBO_U_EAW_FULLWIDTH);
}

static void test_ucd_symbols_currency_math(void)
{
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x0024), ROBO_U_GC_SC); /* $ */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x20AC), ROBO_U_GC_SC); /* € */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x2200), ROBO_U_GC_SM); /* ∀ */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x221E), ROBO_U_GC_SM); /* ∞ */
}

static void test_ucd_emoji_and_supplementary(void)
{
    /* 😀 U+1F600 */
    ROBO_CHECK(robo_unicode_is_emoji(0x1F600));
    ROBO_CHECK(robo_unicode_is_emoji_presentation(0x1F600));
    ROBO_CHECK(robo_unicode_is_extended_pictographic(0x1F600));
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x1F600), ROBO_U_GC_SO);
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x1F600), ROBO_U_SCRIPT_COMMON);
    /* Musical symbol supplementary */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x1D11E), ROBO_U_GC_SO); /* 𝄞 */
}

static void test_ucd_default_ignorable_and_unassigned(void)
{
    /* Soft hyphen U+00AD is Default_Ignorable */
    ROBO_CHECK(robo_unicode_is_default_ignorable(0x00AD));
    /* Unassigned high plane — Cn / Unknown */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x0378), ROBO_U_GC_CN);
    ROBO_CHECK_EQ_INT(robo_unicode_script(0x0378), ROBO_U_SCRIPT_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_unicode_combining_class(0x0378), 0);
    /* Out of range */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x110000), ROBO_U_GC_CN);
}

static void test_ucd_boundaries(void)
{
    ROBO_CHECK_EQ_INT(robo_unicode_category(0), ROBO_U_GC_CC);
    ROBO_CHECK_EQ_INT(robo_unicode_category(0x10FFFF), ROBO_U_GC_CN);
    /* Surrogate code points are Cs in UCD */
    ROBO_CHECK_EQ_INT(robo_unicode_category(0xD800), ROBO_U_GC_CS);
    ROBO_CHECK_STR_EQ(robo_unicode_script_name(ROBO_U_SCRIPT_ARABIC), "Arabic");
}

void robo_test_register_unicode_props(void)
{
    robo_test_register("ucd.version", test_ucd_version);
    robo_test_register("ucd.ascii_latin", test_ucd_ascii_latin);
    robo_test_register("ucd.scripts", test_ucd_scripts);
    robo_test_register("ucd.urdu_persian", test_ucd_urdu_persian);
    robo_test_register("ucd.combining", test_ucd_combining_and_marks);
    robo_test_register("ucd.cjk_eaw", test_ucd_cjk_eaw);
    robo_test_register("ucd.symbols", test_ucd_symbols_currency_math);
    robo_test_register("ucd.emoji_supp", test_ucd_emoji_and_supplementary);
    robo_test_register("ucd.ignorable_unassigned", test_ucd_default_ignorable_and_unassigned);
    robo_test_register("ucd.boundaries", test_ucd_boundaries);
}
