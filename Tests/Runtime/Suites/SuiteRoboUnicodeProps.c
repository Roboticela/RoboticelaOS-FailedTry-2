/*
 * RoboUnicode property suite (Goal 3.4) — UCD lookups.
 */

#include "RoboUnicode/RoboUnicodeProps.h"
#include "RoboTest.h"

#include <string.h>

static void TestUcdVersion(void)
{
    RoboCheckStrEq(UnicodeUcdVersionString(), "16.0.0");
    RoboCheckStrEq(UnicodeVersionString(), "16.0.0");
}

static void TestUcdAsciiLatin(void)
{
    RoboCheckEqInt(UnicodeCategory('A'), RoboUGcLu);
    RoboCheckEqInt(UnicodeCategory('z'), RoboUGcLl);
    RoboCheckEqInt(UnicodeCategory('0'), RoboUGcNd);
    RoboCheckEqInt(UnicodeCategory(' '), RoboUGcZs);
    RoboCheck(UnicodeIsAlphabetic('A'));
    RoboCheck(UnicodeIsUppercase('A'));
    RoboCheck(UnicodeIsLowercase('a'));
    RoboCheck(!UnicodeIsUppercase('a'));
    RoboCheck(UnicodeIsWhitespace(' '));
    RoboCheck(UnicodeIsWhitespace('\t'));
    RoboCheck(UnicodeIsWhitespace('\n'));
    RoboCheck(!UnicodeIsWhitespace('A'));
    RoboCheckEqInt(MakeUnicodeScript('A'), RoboUScriptLatin);
    RoboCheckEqInt(UnicodeCombiningClass('A'), 0);
    RoboCheckStrEq(UnicodeCategoryName(RoboUGcLu), "Lu");
}

static void TestUcdScripts(void)
{
    /* Arabic letter ا U+0627 */
    RoboCheckEqInt(MakeUnicodeScript(0x0627), RoboUScriptArabic);
    RoboCheck(UnicodeIsAlphabetic(0x0627));
    /* Hebrew א */
    RoboCheckEqInt(MakeUnicodeScript(0x05D0), RoboUScriptHebrew);
    /* Devanagari अ */
    RoboCheckEqInt(MakeUnicodeScript(0x0905), RoboUScriptDevanagari);
    /* Bengali অ */
    RoboCheckEqInt(MakeUnicodeScript(0x0985), RoboUScriptBengali);
    /* Thai ก */
    RoboCheckEqInt(MakeUnicodeScript(0x0E01), RoboUScriptThai);
    /* Han 中 */
    RoboCheckEqInt(MakeUnicodeScript(0x4E2D), RoboUScriptHan);
    /* Hiragana あ */
    RoboCheckEqInt(MakeUnicodeScript(0x3042), RoboUScriptHiragana);
    /* Hangul 한 */
    RoboCheckEqInt(MakeUnicodeScript(0xD55C), RoboUScriptHangul);
    /* Common digit */
    RoboCheckEqInt(MakeUnicodeScript('5'), RoboUScriptCommon);
}

static void TestUcdUrduPersian(void)
{
    /* Urdu/Persian Arabic-script letters: پ U+067E, گ U+06AF, ھ U+06BE */
    RoboCheckEqInt(MakeUnicodeScript(0x067E), RoboUScriptArabic);
    RoboCheckEqInt(MakeUnicodeScript(0x06AF), RoboUScriptArabic);
    RoboCheckEqInt(MakeUnicodeScript(0x06BE), RoboUScriptArabic);
    RoboCheck(UnicodeIsAlphabetic(0x067E));
    RoboCheckEqInt(UnicodeCategory(0x067E), RoboUGcLo);
}

static void TestUcdCombiningAndMarks(void)
{
    /* Combining acute U+0301, CCC=230 */
    RoboCheckEqInt(UnicodeCombiningClass(0x0301), 230);
    RoboCheckEqInt(UnicodeCategory(0x0301), RoboUGcMn);
    RoboCheckEqInt(MakeUnicodeScript(0x0301), RoboUScriptInherited);
    /* Devanagari virama U+094D */
    RoboCheckEqInt(UnicodeCategory(0x094D), RoboUGcMn);
    RoboCheck(UnicodeCombiningClass(0x094D) != 0);
}

static void TestUcdCjkEaw(void)
{
    RoboCheckEqInt(MakeUnicodeEastAsianWidth(0x4E2D), RoboUEawWide);
    RoboCheckEqInt(MakeUnicodeEastAsianWidth('A'), RoboUEawNarrow);
    /* Fullwidth A */
    RoboCheckEqInt(MakeUnicodeEastAsianWidth(0xFF21), RoboUEawFullwidth);
}

static void TestUcdSymbolsCurrencyMath(void)
{
    RoboCheckEqInt(UnicodeCategory(0x0024), RoboUGcSc); /* $ */
    RoboCheckEqInt(UnicodeCategory(0x20AC), RoboUGcSc); /* € */
    RoboCheckEqInt(UnicodeCategory(0x2200), RoboUGcSm); /* ∀ */
    RoboCheckEqInt(UnicodeCategory(0x221E), RoboUGcSm); /* ∞ */
}

static void TestUcdEmojiAndSupplementary(void)
{
    /* 😀 U+1F600 */
    RoboCheck(UnicodeIsEmoji(0x1F600));
    RoboCheck(UnicodeIsEmojiPresentation(0x1F600));
    RoboCheck(UnicodeIsExtendedPictographic(0x1F600));
    RoboCheckEqInt(UnicodeCategory(0x1F600), RoboUGcSo);
    RoboCheckEqInt(MakeUnicodeScript(0x1F600), RoboUScriptCommon);
    /* Musical symbol supplementary */
    RoboCheckEqInt(UnicodeCategory(0x1D11E), RoboUGcSo); /* 𝄞 */
}

static void TestUcdDefaultIgnorableAndUnassigned(void)
{
    /* Soft hyphen U+00AD is Default_Ignorable */
    RoboCheck(UnicodeIsDefaultIgnorable(0x00AD));
    /* Unassigned high plane — Cn / Unknown */
    RoboCheckEqInt(UnicodeCategory(0x0378), RoboUGcCn);
    RoboCheckEqInt(MakeUnicodeScript(0x0378), RoboUScriptUnknown);
    RoboCheckEqInt(UnicodeCombiningClass(0x0378), 0);
    /* Out of range */
    RoboCheckEqInt(UnicodeCategory(0x110000), RoboUGcCn);
}

static void TestUcdBoundaries(void)
{
    RoboCheckEqInt(UnicodeCategory(0), RoboUGcCc);
    RoboCheckEqInt(UnicodeCategory(0x10FFFF), RoboUGcCn);
    /* Surrogate code points are Cs in UCD */
    RoboCheckEqInt(UnicodeCategory(0xD800), RoboUGcCs);
    RoboCheckStrEq(UnicodeScriptName(RoboUScriptArabic), "Arabic");
}

void TestRegisterUnicodeProps(void)
{
    TestRegister("ucd.version", TestUcdVersion);
    TestRegister("ucd.ascii_latin", TestUcdAsciiLatin);
    TestRegister("ucd.scripts", TestUcdScripts);
    TestRegister("ucd.urdu_persian", TestUcdUrduPersian);
    TestRegister("ucd.combining", TestUcdCombiningAndMarks);
    TestRegister("ucd.cjk_eaw", TestUcdCjkEaw);
    TestRegister("ucd.symbols", TestUcdSymbolsCurrencyMath);
    TestRegister("ucd.emoji_supp", TestUcdEmojiAndSupplementary);
    TestRegister("ucd.ignorable_unassigned", TestUcdDefaultIgnorableAndUnassigned);
    TestRegister("ucd.boundaries", TestUcdBoundaries);
}
