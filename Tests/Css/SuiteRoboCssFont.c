/*
 * RoboCSS font suite (Phase 18.14) — css.font.*
 *
 * Twelve closed sets over a family whose values describe a *request* rather than a
 * result, the shared closed-vocabulary computation body they are read through, and
 * the registry rows all of it is registered by.
 *
 * Three things this suite is really about, and each is a fact no single accessor
 * call could state.
 *
 * First, the split by *grammar*. Six properties have a genuinely closed set and take
 * a typed shape — `font-style`, `font-kerning`, `font-optical-sizing`, the four
 * `font-synthesis-*` longhands, `font-variant-caps` and `font-variant-position` — so
 * `font-kerning: banana` is refused with a sentence. The rest have a grammar wider
 * than any closed set, so their declarations are CARRIED and the closed sets inside
 * them (`CssFontStretch`, `CssFontSizeKeyword`, `CssFontVariantNumeric`,
 * `CssFontVariantEastAsian`) are read here — `font-stretch: 87.5%` survives, and so
 * does `font-stretch: banana`, which is this build's trade for a property whose real
 * grammar is not closed.
 *
 * Second, `@font-face`'s descriptors are not properties. The same spelling means
 * something different in a block — `font-weight: 700` declares a face's weight where
 * the property selects one — and eight names exist only there. So the descriptor
 * vocabulary is asserted name by name, including which of them is a property too.
 *
 * Third, the family is entirely **inherited** apart from the `font` shorthand, which
 * is a different statement from the rest of Phase 18's families and the reason the
 * registry case reads the bit rather than trusting the list.
 *
 * Nothing here opens a font, shapes a run, measures a glyph or synthesises anything.
 * A value at this phase is a value with classifications in it, and every expectation
 * below is about the value, what it classifies as, or where it came from.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSFont.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The typed vocabularies                                              */
/* ------------------------------------------------------------------ */

static void TestFontStyleVocabulary(void)
{
    static const char *const keywords[] = { "normal", "italic", "oblique" };
    static const struct {
        int slanted;
        int oblique;
    } expected[] = {
        {0, 0}, {1, 0}, {1, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontStyleCount, 3);
    RoboCheckEqInt((int)CssFontStyleCount(), 3);
    for (index = 0u; index < 3u; index++) {
        CssFontStyle style = (CssFontStyle)index;

        RoboCheckEqInt(CssFontStyleIsKnown(style), 1);
        RoboCheckStrEq(CssFontStyleName(style), keywords[index]);
        RoboCheckEqInt(CssFontStyleFromText(keywords[index]), (int)style);
        RoboCheck(CssFontStyleSliceIsKnown(CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssFontStyleIsSlanted(style), expected[index].slanted);
        RoboCheckEqInt(CssFontStyleIsOblique(style), expected[index].oblique);
    }
    RoboCheckEqInt(CssFontStyleIsKnown((CssFontStyle)99), 0);
    RoboCheckStrEq(CssFontStyleName((CssFontStyle)99), "");
    RoboCheckEqInt(CssFontStyleFromText("ITALIC"), (int)RoboCssFontStyleItalic);
    RoboCheckEqInt(CssFontStyleFromText(NULL), (int)RoboCssFontStyleNormal);
    RoboCheckEqInt(CssFontStyleFromText("banana"), (int)RoboCssFontStyleNormal);
    RoboCheckEqInt(CssFontStyleSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssFontStyleSliceIsKnown(CssSliceEmpty()), 0);
    /* An unknown value neither leans nor is an oblique: a value this build cannot
     * read must not claim a face was asked for. */
    RoboCheckEqInt(CssFontStyleIsSlanted((CssFontStyle)99), 0);
    RoboCheckEqInt(CssFontStyleIsOblique((CssFontStyle)99), 0);
    /* The two slanted rows are two answers, not one: an italic is a separate design
     * and an oblique is the upright one turned. */
    RoboCheckEqInt(CssFontStyleIsSlanted(RoboCssFontStyleItalic), 1);
    RoboCheckEqInt(CssFontStyleIsOblique(RoboCssFontStyleItalic), 0);

    {
        char buffer[RoboCssFontStyleTextMax];
        size_t length = 0u;
        char tiny[4];

        RoboCheckEqInt(CssFontStyleWrite(RoboCssFontStyleOblique, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "oblique");
        RoboCheckEqInt((int)length, 7);
        tiny[0] = 'x';
        RoboCheckEqInt(CssFontStyleWrite(RoboCssFontStyleOblique, tiny,
                                              sizeof(tiny), &length, NULL),
                          RoboNotSupported);
        RoboCheckStrEq(tiny, "");
        RoboCheckEqInt((int)length, 7);
        RoboCheckEqInt(CssFontStyleWrite((CssFontStyle)99, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
        RoboCheckStrEq(buffer, "");
    }
}

static void TestFontKerningVocabulary(void)
{
    static const char *const keywords[] = { "auto", "normal", "none" };

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontKerningCount, 3);
    RoboCheckEqInt((int)CssFontKerningCount(), 3);
    RoboCheckStrEq(CssFontKerningName(RoboCssFontKerningAuto), keywords[0]);
    RoboCheckStrEq(CssFontKerningName(RoboCssFontKerningNormal), keywords[1]);
    RoboCheckStrEq(CssFontKerningName(RoboCssFontKerningNone), keywords[2]);
    RoboCheckEqInt(CssFontKerningFromText("AUTO"), (int)RoboCssFontKerningAuto);
    RoboCheckEqInt(CssFontKerningFromText("none"), (int)RoboCssFontKerningNone);
    RoboCheckEqInt(CssFontKerningFromText(NULL), (int)RoboCssFontKerningAuto);
    RoboCheckEqInt(CssFontKerningIsKnown((CssFontKerning)99), 0);
    RoboCheckEqInt(CssFontKerningSliceIsKnown(CssSliceFromText("banana")), 0);

    /* The pair that is easy to get wrong: `auto` is *not* "do not kern" — it means
     * the font's own behaviour decides — so only `none` refuses. */
    RoboCheckEqInt(CssFontKerningEnablesKerning(RoboCssFontKerningAuto), 1);
    RoboCheckEqInt(CssFontKerningEnablesKerning(RoboCssFontKerningNormal), 1);
    RoboCheckEqInt(CssFontKerningEnablesKerning(RoboCssFontKerningNone), 0);
    RoboCheckEqInt(CssFontKerningEnablesKerning((CssFontKerning)99), 0);

    {
        char buffer[RoboCssFontKerningTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontKerningWrite(RoboCssFontKerningNormal, buffer,
                                                 sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "normal");
        RoboCheckEqInt(CssFontKerningWrite((CssFontKerning)99, buffer,
                                                 sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestFontOpticalSizingVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssFontOpticalSizingCount, 2);
    RoboCheckEqInt((int)CssFontOpticalSizingCount(), 2);
    RoboCheckStrEq(CssFontOpticalSizingName(RoboCssFontOpticalSizingAuto),
                      "auto");
    RoboCheckStrEq(CssFontOpticalSizingName(RoboCssFontOpticalSizingNone),
                      "none");
    RoboCheckEqInt(CssFontOpticalSizingFromText("NONE"),
                      (int)RoboCssFontOpticalSizingNone);
    RoboCheckEqInt(CssFontOpticalSizingFromText(NULL),
                      (int)RoboCssFontOpticalSizingAuto);
    RoboCheckEqInt(CssFontOpticalSizingIsKnown((CssFontOpticalSizing)99), 0);
    RoboCheckEqInt(CssFontOpticalSizingSliceIsKnown(CssSliceFromText("size")), 0);
    RoboCheckEqInt(CssFontOpticalSizingIsAutomatic(RoboCssFontOpticalSizingAuto), 1);
    RoboCheckEqInt(CssFontOpticalSizingIsAutomatic(RoboCssFontOpticalSizingNone), 0);
    RoboCheckEqInt(CssFontOpticalSizingIsAutomatic((CssFontOpticalSizing)99), 0);

    {
        char buffer[RoboCssFontOpticalSizingTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontOpticalSizingWrite(RoboCssFontOpticalSizingNone,
                                                       buffer, sizeof(buffer),
                                                       &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "none");
    }
}

static void TestFontSynthesisVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssFontSynthesisCount, 2);
    RoboCheckEqInt((int)CssFontSynthesisCount(), 2);
    RoboCheckStrEq(CssFontSynthesisName(RoboCssFontSynthesisAuto), "auto");
    RoboCheckStrEq(CssFontSynthesisName(RoboCssFontSynthesisNone), "none");
    RoboCheckEqInt(CssFontSynthesisFromText("Auto"),
                      (int)RoboCssFontSynthesisAuto);
    RoboCheckEqInt(CssFontSynthesisFromText(NULL),
                      (int)RoboCssFontSynthesisAuto);
    RoboCheckEqInt(CssFontSynthesisSliceIsKnown(CssSliceFromText("auto")), 1);
    RoboCheckEqInt(CssFontSynthesisSliceIsKnown(CssSliceFromText("always")), 0);
    RoboCheckEqInt(CssFontSynthesisIsKnown((CssFontSynthesis)99), 0);

    /* `auto` is a *permission*, not an instruction — nothing in this build
     * synthesises a weight or a slope (the rule 12.3 records) — so the classification
     * is "may it" and the answer for `none` is no. */
    RoboCheckEqInt(CssFontSynthesisAllows(RoboCssFontSynthesisAuto), 1);
    RoboCheckEqInt(CssFontSynthesisAllows(RoboCssFontSynthesisNone), 0);
    RoboCheckEqInt(CssFontSynthesisAllows((CssFontSynthesis)99), 0);

    {
        char buffer[RoboCssFontSynthesisTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontSynthesisWrite(RoboCssFontSynthesisAuto, buffer,
                                                   sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "auto");
    }
}

static void TestFontVariantCapsVocabulary(void)
{
    static const char *const keywords[] = {
        "normal", "small-caps", "all-small-caps", "petite-caps",
        "all-petite-caps", "unicase", "titling-caps"
    };
    static const struct {
        int capsVariant;
        int affectsLowercase;
        int petite;
    } expected[] = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {1, 0, 1}, {1, 1, 1}, {1, 0, 0}, {1, 0, 0}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontVariantCapsCount, 7);
    RoboCheckEqInt((int)CssFontVariantCapsCount(), 7);
    for (index = 0u; index < 7u; index++) {
        CssFontVariantCaps caps = (CssFontVariantCaps)index;

        RoboCheckEqInt(CssFontVariantCapsIsKnown(caps), 1);
        RoboCheckStrEq(CssFontVariantCapsName(caps), keywords[index]);
        RoboCheckEqInt(CssFontVariantCapsFromText(keywords[index]), (int)caps);
        RoboCheck(CssFontVariantCapsSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssFontVariantCapsIsCapsVariant(caps),
                          expected[index].capsVariant);
        RoboCheckEqInt(CssFontVariantCapsAffectsLowercase(caps),
                          expected[index].affectsLowercase);
        RoboCheckEqInt(CssFontVariantCapsIsPetite(caps), expected[index].petite);
    }
    RoboCheckEqInt(CssFontVariantCapsIsKnown((CssFontVariantCaps)99), 0);
    RoboCheckEqInt(CssFontVariantCapsFromText(NULL),
                      (int)RoboCssFontVariantCapsNormal);
    RoboCheckEqInt(CssFontVariantCapsSliceIsKnown(CssSliceFromText("caps")), 0);
    RoboCheckEqInt(CssFontVariantCapsAffectsLowercase(
                          RoboCssFontVariantCapsSmallCaps),
                      0);
    RoboCheckEqInt(CssFontVariantCapsAffectsLowercase(
                          RoboCssFontVariantCapsAllSmallCaps),
                      1);
    RoboCheckEqInt(CssFontVariantCapsAffectsLowercase(
                          (CssFontVariantCaps)99),
                      0);

    {
        char buffer[RoboCssFontVariantCapsTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontVariantCapsWrite(
                              RoboCssFontVariantCapsAllPetiteCaps, buffer,
                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "all-petite-caps");
        RoboCheckEqInt((int)length, 15);
        RoboCheckEqInt(CssFontVariantCapsWrite((CssFontVariantCaps)99, buffer,
                                                      sizeof(buffer), &length,
                                                      NULL),
                          RoboInvalidArgument);
    }
}

static void TestFontVariantPositionVocabulary(void)
{
    static const char *const keywords[] = { "normal", "sub", "super" };
    static const struct {
        int shifted;
        int superscript;
    } expected[] = {
        {0, 0}, {1, 0}, {1, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontVariantPositionCount, 3);
    RoboCheckEqInt((int)CssFontVariantPositionCount(), 3);
    for (index = 0u; index < 3u; index++) {
        CssFontVariantPosition position = (CssFontVariantPosition)index;

        RoboCheckEqInt(CssFontVariantPositionIsKnown(position), 1);
        RoboCheckStrEq(CssFontVariantPositionName(position), keywords[index]);
        RoboCheckEqInt(CssFontVariantPositionIsShifted(position),
                          expected[index].shifted);
        RoboCheckEqInt(CssFontVariantPositionIsSuperscript(position),
                          expected[index].superscript);
    }
    RoboCheckEqInt(CssFontVariantPositionIsKnown((CssFontVariantPosition)99), 0);
    RoboCheckEqInt(CssFontVariantPositionFromText("SUPER"),
                      (int)RoboCssFontVariantPositionSuper);
    RoboCheckEqInt(CssFontVariantPositionFromText(NULL),
                      (int)RoboCssFontVariantPositionNormal);
    RoboCheckEqInt(CssFontVariantPositionSliceIsKnown(
                          CssSliceFromText("baseline")),
                      0);
    /* The shift has a *sign*, which is why the two questions are asked separately:
     * a subscript is shifted and is not a superscript. */
    RoboCheckEqInt(CssFontVariantPositionIsShifted(
                          RoboCssFontVariantPositionSub),
                      1);
    RoboCheckEqInt(CssFontVariantPositionIsSuperscript(
                          RoboCssFontVariantPositionSub),
                      0);
    RoboCheckEqInt(CssFontVariantPositionIsShifted(
                          (CssFontVariantPosition)99),
                      0);
    RoboCheckEqInt(CssFontVariantPositionIsSuperscript(
                          (CssFontVariantPosition)99),
                      0);

    {
        char buffer[RoboCssFontVariantPositionTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontVariantPositionWrite(
                              RoboCssFontVariantPositionSuper, buffer,
                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "super");
    }
}

/* ------------------------------------------------------------------ */
/* The classified vocabularies                                         */
/* ------------------------------------------------------------------ */

static void TestFontVariantNumericVocabulary(void)
{
    static const char *const keywords[] = {
        "normal", "ordinal", "slashed-zero", "lining-nums", "oldstyle-nums",
        "proportional-nums", "tabular-nums", "diagonal-fractions",
        "stacked-fractions"
    };
    static const CssFontVariantNumericKind kinds[] = {
        RoboCssFontVariantNumericKindNormal,
        RoboCssFontVariantNumericKindOrdinal,
        RoboCssFontVariantNumericKindSlashedZero,
        RoboCssFontVariantNumericKindFigureStyle,
        RoboCssFontVariantNumericKindFigureStyle,
        RoboCssFontVariantNumericKindFigureSpacing,
        RoboCssFontVariantNumericKindFigureSpacing,
        RoboCssFontVariantNumericKindFraction,
        RoboCssFontVariantNumericKindFraction
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontVariantNumericCount, 9);
    RoboCheckEqInt((int)CssFontVariantNumericCount(), 9);
    RoboCheckEqInt((int)RoboCssFontVariantNumericKindCount, 6);
    for (index = 0u; index < 9u; index++) {
        CssFontVariantNumeric numeric = (CssFontVariantNumeric)index;

        RoboCheckEqInt(CssFontVariantNumericIsKnown(numeric), 1);
        RoboCheckStrEq(CssFontVariantNumericName(numeric), keywords[index]);
        RoboCheckEqInt(CssFontVariantNumericFromText(keywords[index]),
                          (int)numeric);
        RoboCheck(CssFontVariantNumericSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssFontVariantNumericKindOf(numeric), kinds[index]);
    }
    RoboCheckEqInt(CssFontVariantNumericIsKnown(
                       (CssFontVariantNumeric)99),
                   0);
    RoboCheckStrEq(CssFontVariantNumericName((CssFontVariantNumeric)99), "");
    RoboCheckEqInt(CssFontVariantNumericFromText(NULL),
                      (int)RoboCssFontVariantNumericNormal);
    RoboCheckEqInt(CssFontVariantNumericSliceIsKnown(
                       CssSliceFromText("lining")),
                   0);
    /* The kind is the *grouping*, and it is what makes `lining-nums
     * oldstyle-nums` contradictory while `lining-nums tabular-nums` is not: the two
     * figure-style rows share one kind and the spacing row has another. */
    RoboCheckEqInt(CssFontVariantNumericKindOf(
                       RoboCssFontVariantNumericLiningNums),
                   CssFontVariantNumericKindOf(
                       RoboCssFontVariantNumericOldstyleNums));
    RoboCheck(CssFontVariantNumericKindOf(
                  RoboCssFontVariantNumericLiningNums) !=
                  CssFontVariantNumericKindOf(
                      RoboCssFontVariantNumericTabularNums));
    /* An unknown value answers the normal kind rather than a kind of its own. */
    RoboCheckEqInt(CssFontVariantNumericKindOf((CssFontVariantNumeric)99),
                      RoboCssFontVariantNumericKindNormal);

    for (index = 0u; index < 6u; index++) {
        CssFontVariantNumericKind kind = (CssFontVariantNumericKind)index;

        RoboCheckEqInt(CssFontVariantNumericKindIsKnown(kind), 1);
        RoboCheck(strlen(CssFontVariantNumericKindName(kind)) > 0u);
    }
    RoboCheckEqInt(CssFontVariantNumericKindIsKnown(
                       (CssFontVariantNumericKind)99),
                   0);
    RoboCheckStrEq(CssFontVariantNumericKindName(
                       (CssFontVariantNumericKind)99),
                      "unknown");

    {
        char buffer[RoboCssFontVariantNumericTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontVariantNumericWrite(
                              RoboCssFontVariantNumericDiagonalFractions, buffer,
                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "diagonal-fractions");
        RoboCheckEqInt((int)length, 18);
        RoboCheckEqInt(CssFontVariantNumericWrite((CssFontVariantNumeric)99,
                                                         buffer, sizeof(buffer),
                                                         &length, NULL),
                          RoboInvalidArgument);
    }
}

static void TestFontVariantEastAsianVocabulary(void)
{
    static const char *const keywords[] = {
        "normal", "jis78", "jis83", "jis90", "jis04", "simplified",
        "traditional", "full-width", "proportional-width", "ruby"
    };
    static const CssFontVariantEastAsianKind kinds[] = {
        RoboCssFontVariantEastAsianKindNormal,
        RoboCssFontVariantEastAsianKindVariant,
        RoboCssFontVariantEastAsianKindVariant,
        RoboCssFontVariantEastAsianKindVariant,
        RoboCssFontVariantEastAsianKindVariant,
        RoboCssFontVariantEastAsianKindVariant,
        RoboCssFontVariantEastAsianKindVariant,
        RoboCssFontVariantEastAsianKindWidth,
        RoboCssFontVariantEastAsianKindWidth,
        RoboCssFontVariantEastAsianKindRuby
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontVariantEastAsianCount, 10);
    RoboCheckEqInt((int)CssFontVariantEastAsianCount(), 10);
    RoboCheckEqInt((int)RoboCssFontVariantEastAsianKindCount, 4);
    for (index = 0u; index < 10u; index++) {
        CssFontVariantEastAsian eastAsian = (CssFontVariantEastAsian)index;

        RoboCheckEqInt(CssFontVariantEastAsianIsKnown(eastAsian), 1);
        RoboCheckStrEq(CssFontVariantEastAsianName(eastAsian), keywords[index]);
        RoboCheckEqInt(CssFontVariantEastAsianFromText(keywords[index]),
                          (int)eastAsian);
        RoboCheckEqInt(CssFontVariantEastAsianKindOf(eastAsian), kinds[index]);
    }
    RoboCheckEqInt(CssFontVariantEastAsianIsKnown(
                       (CssFontVariantEastAsian)99),
                   0);
    RoboCheckEqInt(CssFontVariantEastAsianFromText("JIS04"),
                      (int)RoboCssFontVariantEastAsianJis04);
    RoboCheckEqInt(CssFontVariantEastAsianFromText(NULL),
                      (int)RoboCssFontVariantEastAsianNormal);
    RoboCheckEqInt(CssFontVariantEastAsianSliceIsKnown(
                       CssSliceFromText("jis")),
                   0);
    RoboCheckEqInt(CssFontVariantEastAsianKindOf(
                       (CssFontVariantEastAsian)99),
                   RoboCssFontVariantEastAsianKindNormal);
    for (index = 0u; index < 4u; index++) {
        CssFontVariantEastAsianKind kind = (CssFontVariantEastAsianKind)index;

        RoboCheckEqInt(CssFontVariantEastAsianKindIsKnown(kind), 1);
        RoboCheck(strlen(CssFontVariantEastAsianKindName(kind)) > 0u);
    }
    RoboCheckEqInt(CssFontVariantEastAsianKindIsKnown(
                       (CssFontVariantEastAsianKind)99),
                   0);
    RoboCheckStrEq(CssFontVariantEastAsianKindName(
                       (CssFontVariantEastAsianKind)99),
                      "unknown");

    {
        char buffer[RoboCssFontVariantEastAsianTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontVariantEastAsianWrite(
                              RoboCssFontVariantEastAsianProportionalWidth,
                              buffer, sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "proportional-width");
        RoboCheckEqInt((int)length, 18);
    }
}

static void TestFontStretchVocabulary(void)
{
    static const char *const keywords[] = {
        "ultra-condensed", "extra-condensed", "condensed", "semi-condensed",
        "normal", "semi-expanded", "expanded", "extra-expanded",
        "ultra-expanded"
    };
    static const double percentages[] = {
        50.0, 62.5, 75.0, 87.5, 100.0, 112.5, 125.0, 150.0, 200.0
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontStretchCount, 9);
    RoboCheckEqInt((int)CssFontStretchCount(), 9);
    for (index = 0u; index < 9u; index++) {
        CssFontStretch stretch = (CssFontStretch)index;

        RoboCheckEqInt(CssFontStretchIsKnown(stretch), 1);
        RoboCheckStrEq(CssFontStretchName(stretch), keywords[index]);
        RoboCheckEqInt(CssFontStretchFromText(keywords[index]), (int)stretch);
        RoboCheck(CssFontStretchSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        CSS_CHECK_DOUBLE(CssFontStretchPercentage(stretch), percentages[index]);
    }
    RoboCheckEqInt(CssFontStretchIsKnown((CssFontStretch)99), 0);
    RoboCheckStrEq(CssFontStretchName((CssFontStretch)99), "");
    RoboCheckEqInt(CssFontStretchFromText(NULL), (int)RoboCssFontStretchNormal);
    RoboCheckEqInt(CssFontStretchSliceIsKnown(CssSliceFromText("wide")), 0);
    /* An unknown value is 100%, the answer that changes no width. */
    CSS_CHECK_DOUBLE(CssFontStretchPercentage((CssFontStretch)99), 100.0);

    /* The enumerator's order is the axis' order, and `normal` is the *middle* row
     * rather than the zero — which is why the shape carries the keyword. */
    RoboCheckEqInt((int)RoboCssFontStretchNormal, 4);
    RoboCheckEqInt(CssFontStretchIsCondensed(RoboCssFontStretchCondensed), 1);
    RoboCheckEqInt(CssFontStretchIsExpanded(RoboCssFontStretchCondensed), 0);
    RoboCheckEqInt(CssFontStretchIsExpanded(RoboCssFontStretchExpanded), 1);
    RoboCheckEqInt(CssFontStretchIsCondensed(RoboCssFontStretchExpanded), 0);
    RoboCheckEqInt(CssFontStretchIsNormal(RoboCssFontStretchNormal), 1);
    RoboCheckEqInt(CssFontStretchIsCondensed(RoboCssFontStretchNormal), 0);
    RoboCheckEqInt(CssFontStretchIsExpanded(RoboCssFontStretchNormal), 0);
    RoboCheckEqInt(CssFontStretchIsCondensed((CssFontStretch)99), 0);
    RoboCheckEqInt(CssFontStretchIsExpanded((CssFontStretch)99), 0);
    RoboCheckEqInt(CssFontStretchIsNormal((CssFontStretch)99), 0);

    /* The nearest-keyword rule, stated once here rather than in each consumer: an
     * exact percentage answers its own row, a between value takes the nearer, and a
     * tie takes the *narrower* one. */
    RoboCheckEqInt(CssFontStretchFromPercentage(100.0),
                      (int)RoboCssFontStretchNormal);
    RoboCheckEqInt(CssFontStretchFromPercentage(87.5),
                      (int)RoboCssFontStretchSemiCondensed);
    RoboCheckEqInt(CssFontStretchFromPercentage(90.0),
                      (int)RoboCssFontStretchSemiCondensed);
    RoboCheckEqInt(CssFontStretchFromPercentage(95.0),
                      (int)RoboCssFontStretchNormal);
    RoboCheckEqInt(CssFontStretchFromPercentage(0.0),
                      (int)RoboCssFontStretchUltraCondensed);
    RoboCheckEqInt(CssFontStretchFromPercentage(-10.0),
                      (int)RoboCssFontStretchUltraCondensed);
    RoboCheckEqInt(CssFontStretchFromPercentage(1000.0),
                      (int)RoboCssFontStretchUltraExpanded);

    {
        char buffer[RoboCssFontStretchTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontStretchWrite(RoboCssFontStretchUltraCondensed,
                                                 buffer, sizeof(buffer),
                                                 &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "ultra-condensed");
        RoboCheckEqInt((int)length, 15);
    }
}

static void TestFontSizeKeywordVocabulary(void)
{
    static const char *const keywords[] = {
        "xx-small", "x-small", "small", "medium", "large", "x-large",
        "xx-large", "xxx-large", "larger", "smaller"
    };
    static const double ratios[] = {
        0.6, 0.75, 0.8888888888888888, 1.0, 1.2, 1.5, 2.0, 3.0, 1.2,
        0.8333333333333334
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontSizeKeywordCount, 10);
    RoboCheckEqInt((int)CssFontSizeKeywordCount(), 10);
    for (index = 0u; index < 10u; index++) {
        CssFontSizeKeyword size = (CssFontSizeKeyword)index;

        RoboCheckEqInt(CssFontSizeKeywordIsKnown(size), 1);
        RoboCheckStrEq(CssFontSizeKeywordName(size), keywords[index]);
        RoboCheckEqInt(CssFontSizeKeywordFromText(keywords[index]), (int)size);
        RoboCheck(CssFontSizeKeywordSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        CSS_CHECK_DOUBLE_NEAR(CssFontSizeKeywordRatio(size), ratios[index],
                              1e-12);
    }
    RoboCheckEqInt(CssFontSizeKeywordIsKnown((CssFontSizeKeyword)99), 0);
    RoboCheckEqInt(CssFontSizeKeywordFromText("XX-SMALL"),
                      (int)RoboCssFontSizeKeywordXxSmall);
    RoboCheckEqInt(CssFontSizeKeywordFromText(NULL),
                      (int)RoboCssFontSizeKeywordMedium);
    RoboCheckEqInt(CssFontSizeKeywordFromText("banana"),
                      (int)RoboCssFontSizeKeywordMedium);
    RoboCheckEqInt(CssFontSizeKeywordSliceIsKnown(
                       CssSliceFromText("bigger")),
                   0);
    /* An unknown value is a ratio of 1, the answer that changes nothing. */
    CSS_CHECK_DOUBLE(CssFontSizeKeywordRatio((CssFontSizeKeyword)99), 1.0);

    /* The two *kinds*, and the property's most important fact: `medium` is not a
     * pixel count — the eight absolute keywords are against the reader's preferred
     * size — while `larger`/`smaller` are against the parent's. */
    RoboCheckEqInt(CssFontSizeKeywordIsAbsolute(
                       RoboCssFontSizeKeywordMedium),
                   1);
    RoboCheckEqInt(CssFontSizeKeywordIsAbsolute(
                       RoboCssFontSizeKeywordLarger),
                   0);
    RoboCheckEqInt(CssFontSizeKeywordIsRelative(
                       RoboCssFontSizeKeywordLarger),
                   1);
    RoboCheckEqInt(CssFontSizeKeywordIsRelative(
                       RoboCssFontSizeKeywordMedium),
                   0);
    RoboCheckEqInt(CssFontSizeKeywordIsAbsolute(
                       RoboCssFontSizeKeywordXxxLarge),
                   1);
    RoboCheckEqInt(CssFontSizeKeywordIsAbsolute(
                       (CssFontSizeKeyword)99),
                   0);
    RoboCheckEqInt(CssFontSizeKeywordIsRelative(
                       (CssFontSizeKeyword)99),
                   0);
    /* The absolute and relative kinds are mutually exclusive over the whole table,
     * which is the invariant a consumer relies on. */
    for (index = 0u; index < 10u; index++) {
        RoboCheckEqInt(CssFontSizeKeywordIsAbsolute((CssFontSizeKeyword)index) +
                           CssFontSizeKeywordIsRelative(
                               (CssFontSizeKeyword)index),
                       1);
    }

    {
        char buffer[RoboCssFontSizeKeywordTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontSizeKeywordWrite(
                              RoboCssFontSizeKeywordXxxLarge, buffer,
                              sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "xxx-large");
        RoboCheckEqInt((int)length, 9);
    }
}

/* ------------------------------------------------------------------ */
/* The @font-face descriptors                                          */
/* ------------------------------------------------------------------ */

static void TestFontFaceDescriptorVocabulary(void)
{
    static const char *const keywords[] = {
        "src", "font-family", "font-style", "font-weight", "font-stretch",
        "font-display", "unicode-range", "font-feature-settings",
        "font-variation-settings", "font-named-instance", "ascent-override",
        "descent-override", "line-gap-override", "size-adjust",
        "font-language-override", "font-kerning", "font-optical-sizing",
        "font-synthesis-weight", "font-synthesis-style",
        "font-synthesis-small-caps", "font-synthesis-position",
        "font-variant-caps", "font-variant-east-asian",
        "font-variant-ligatures", "font-variant-numeric",
        "font-variant-position", "font-palette"
    };
    static const int propertyShared[] = {
        0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1
    };
    size_t index;
    size_t sharedCount = 0u;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontFaceDescriptorCount, 27);
    RoboCheckEqInt((int)sizeof(keywords) / sizeof(keywords[0]), 27);
    RoboCheckEqInt((int)CssFontFaceDescriptorCount(), 27);
    for (index = 0u; index < 27u; index++) {
        CssFontFaceDescriptor descriptor = (CssFontFaceDescriptor)index;

        RoboCheckEqInt(CssFontFaceDescriptorIsKnown(descriptor), 1);
        RoboCheckStrEq(CssFontFaceDescriptorName(descriptor), keywords[index]);
        RoboCheckEqInt(CssFontFaceDescriptorFromText(keywords[index]),
                          (int)descriptor);
        RoboCheck(CssFontFaceDescriptorSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssFontFaceDescriptorIsPropertyShared(descriptor),
                          propertyShared[index]);
        if (propertyShared[index]) {
            sharedCount++;
        }
    }
    RoboCheckEqInt(CssFontFaceDescriptorIsKnown(
                       (CssFontFaceDescriptor)99),
                   0);
    RoboCheckStrEq(CssFontFaceDescriptorName((CssFontFaceDescriptor)99), "");
    RoboCheckEqInt(CssFontFaceDescriptorFromText(NULL),
                      (int)RoboCssFontFaceDescriptorSrc);
    RoboCheckEqInt(CssFontFaceDescriptorSliceIsKnown(
                       CssSliceFromText("font-size")),
                   0);
    RoboCheckEqInt(CssFontFaceDescriptorIsPropertyShared(
                       (CssFontFaceDescriptor)99),
                   0);

    /* The classification's whole point, stated as three named rows rather than a
     * count: `src` and `unicode-range` exist only inside a `@font-face` block, while
     * `font-weight` wears the same name on an element and means a different thing. */
    RoboCheckEqInt(CssFontFaceDescriptorIsPropertyShared(
                       RoboCssFontFaceDescriptorSrc),
                   0);
    RoboCheckEqInt(CssFontFaceDescriptorIsPropertyShared(
                       RoboCssFontFaceDescriptorUnicodeRange),
                   0);
    RoboCheckEqInt(CssFontFaceDescriptorIsPropertyShared(
                       RoboCssFontFaceDescriptorFontWeight),
                   1);
    RoboCheckEqInt(CssFontFaceDescriptorIsPropertyShared(
                       RoboCssFontFaceDescriptorFontDisplay),
                   0);
    RoboCheckEqInt(CssFontFaceDescriptorIsPropertyShared(
                       RoboCssFontFaceDescriptorAscentOverride),
                   0);
    /* Nineteen of the twenty-seven names are properties too — the number is
     * asserted so a row added later is a deliberate act. */
    RoboCheckEqInt((int)sharedCount, 19);

    {
        char buffer[RoboCssFontFaceDescriptorTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontFaceDescriptorWrite(
                              RoboCssFontFaceDescriptorFontVariationSettings,
                              buffer, sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "font-variation-settings");
        RoboCheckEqInt((int)length, 23);
        RoboCheckEqInt(CssFontFaceDescriptorWrite(
                              RoboCssFontFaceDescriptorFontSynthesisSmallCaps,
                              buffer, sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "font-synthesis-small-caps");
    }
}

static void TestFontDisplayVocabulary(void)
{
    static const char *const keywords[] = {
        "auto", "block", "swap", "fallback", "optional"
    };
    static const struct {
        int blocks;
        int blockPeriodMillis;
        int swaps;
    } expected[] = {
        {1, 0, 0}, {1, 3000, 0}, {0, 0, 1}, {1, 100, 1}, {1, 100, 0}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssFontDisplayCount, 5);
    RoboCheckEqInt((int)CssFontDisplayCount(), 5);
    for (index = 0u; index < 5u; index++) {
        CssFontDisplay display = (CssFontDisplay)index;

        RoboCheckEqInt(CssFontDisplayIsKnown(display), 1);
        RoboCheckStrEq(CssFontDisplayName(display), keywords[index]);
        RoboCheckEqInt(CssFontDisplayFromText(keywords[index]), (int)display);
        RoboCheck(CssFontDisplaySliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssFontDisplayBlocksRendering(display),
                          expected[index].blocks);
        RoboCheckEqInt(CssFontDisplayBlockPeriodMillis(display),
                          expected[index].blockPeriodMillis);
        RoboCheckEqInt(CssFontDisplaySwapsAfterBlockPeriod(display),
                          expected[index].swaps);
    }
    RoboCheckEqInt(CssFontDisplayIsKnown((CssFontDisplay)99), 0);
    RoboCheckStrEq(CssFontDisplayName((CssFontDisplay)99), "");
    RoboCheckEqInt(CssFontDisplayFromText("SWAP"), (int)RoboCssFontDisplaySwap);
    RoboCheckEqInt(CssFontDisplayFromText(NULL), (int)RoboCssFontDisplayAuto);
    RoboCheckEqInt(CssFontDisplaySliceIsKnown(CssSliceFromText("wait")), 0);

    /* `swap` is the row that blocks nothing and replaces the fallback; `block` is
     * its opposite — the longest block period and no swap. */
    RoboCheckEqInt(CssFontDisplayBlocksRendering(RoboCssFontDisplaySwap), 0);
    RoboCheckEqInt(CssFontDisplaySwapsAfterBlockPeriod(RoboCssFontDisplaySwap), 1);
    RoboCheckEqInt(CssFontDisplayBlockPeriodMillis(RoboCssFontDisplayBlock), 3000);
    RoboCheckEqInt(CssFontDisplaySwapsAfterBlockPeriod(RoboCssFontDisplayBlock), 0);
    /* `optional` blocks briefly and never swaps — the pair that is easy to read as
     * `fallback`, which blocks for the same 100 ms and *does* swap. */
    RoboCheckEqInt(CssFontDisplayBlockPeriodMillis(RoboCssFontDisplayOptional),
                      100);
    RoboCheckEqInt(CssFontDisplaySwapsAfterBlockPeriod(RoboCssFontDisplayOptional),
                      0);
    RoboCheckEqInt(CssFontDisplaySwapsAfterBlockPeriod(RoboCssFontDisplayFallback),
                      1);
    RoboCheckEqInt(CssFontDisplayIsAutomatic(RoboCssFontDisplayAuto), 1);
    RoboCheckEqInt(CssFontDisplayIsAutomatic(RoboCssFontDisplayBlock), 0);
    /* An unknown value blocks — the answer that risks invisible text must not be
     * handed out for free — and has no period of its own. */
    RoboCheckEqInt(CssFontDisplayBlocksRendering((CssFontDisplay)99), 1);
    RoboCheckEqInt(CssFontDisplayBlockPeriodMillis((CssFontDisplay)99), 0);
    RoboCheckEqInt(CssFontDisplaySwapsAfterBlockPeriod((CssFontDisplay)99), 0);
    RoboCheckEqInt(CssFontDisplayIsAutomatic((CssFontDisplay)99), 0);

    {
        char buffer[RoboCssFontDisplayTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssFontDisplayWrite(RoboCssFontDisplayFallback, buffer,
                                                  sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "fallback");
        RoboCheckEqInt((int)length, 8);
        RoboCheckEqInt(CssFontDisplayWrite((CssFontDisplay)99, buffer,
                                                  sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The computed values                                                 */
/* ------------------------------------------------------------------ */

static void TestFontComputedShapes(void)
{
    CssFixture();

    /* The six typed rows read through the shared closed-vocabulary body, so a value
     * this build can read is a KEYWORD carrying the canonical spelling. */
    CssComputeEquals(RoboCssPropertyComputationFontStyle, "o: italic",
                       "italic");
    CssComputeEquals(RoboCssPropertyComputationFontStyle, "o: OBLIQUE",
                       "oblique");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationFontStyle,
                                        "o: italic"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationFontKerning, "o: none", "none");
    CssComputeEquals(RoboCssPropertyComputationFontOpticalSizing, "o: auto",
                       "auto");
    CssComputeEquals(RoboCssPropertyComputationFontSynthesis, "o: none", "none");
    CssComputeEquals(RoboCssPropertyComputationFontVariantCaps,
                       "o: all-small-caps", "all-small-caps");
    CssComputeEquals(RoboCssPropertyComputationFontVariantPosition, "o: super",
                       "super");

    /* A reference is deferred -- legal CSS whose value is decided where the
     * reference is -- and it is carried rather than refused. */
    {
        uint32_t flags = 0u;

        CssComputeEquals(RoboCssPropertyComputationFontStyle, "o: var(--s)",
                           "var(--s)");
        CssComputeText(RoboCssPropertyComputationFontStyle, "o: var(--s)",
                         NULL, 0u, &flags);
        RoboCheckEqInt(flags, 1u);
    }

    /* The refusals, which is what the closed set buys: a word the vocabulary does
     * not know, something that is not an identifier, and *two* identifiers. */
    CssComputeEquals(RoboCssPropertyComputationFontStyle, "o: banana",
                       "not a font-style value");
    CssComputeEquals(RoboCssPropertyComputationFontStyle, "o: 3px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationFontStyle, "o: italic oblique",
                       "not a single font-style keyword");
    CssComputeEquals(RoboCssPropertyComputationFontKerning, "o: banana",
                       "not a font-kerning value");
    CssComputeEquals(RoboCssPropertyComputationFontVariantCaps, "o: smallcaps",
                       "not a font-variant-caps value");
    CssComputeEquals(RoboCssPropertyComputationFontVariantPosition, "o: banana",
                       "not a font-variant-position value");
    CssComputeEquals(RoboCssPropertyComputationFontSynthesis, "o: banana",
                       "not a font-synthesis value");

    /* The other half of the family is *carried*, and two of these rows are the
     * phase's stated trade: a `font-stretch` percentage and a
     * `font-variant-numeric` axis set are legal CSS whose grammar is not a closed
     * set, so the declaration survives intact — including the word nothing
     * recognises — rather than being refused. */
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 87.5%", "87.5%");
    CssComputeEquals(RoboCssPropertyComputationPreserve,
                       "o: tabular-nums diagonal-fractions",
                       "tabular-nums diagonal-fractions");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: banana", "banana");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve,
                                        "o: banana"),
                      "preserved");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: normal", "normal");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestFontRegistry(void)
{
    /*
     * The family is **entirely inherited** apart from the `font` shorthand, which is
     * the opposite of every other family Phase 18 added and the reason this case
     * reads the bit rather than trusting the list: a `font-style`, a `font-kerning`
     * and a `font-variant-*` all propagate, so a child on axes its parent was not on
     * cannot be described otherwise, while a shorthand's own computed value is the
     * tokens an author wrote and expansion — not the cascade — is what distributes it.
     */
    static const struct {
        const char *name;
        const char *shape;
        int inherited;
    } expected[] = {
        {"font", "normal,_,normal,_,normal,_,normal,_,medium,/,normal,_,serif", 0},
        {"font-feature-settings", "normal", 1},
        {"font-kerning", "auto", 1},
        {"font-language-override", "normal", 1},
        {"font-optical-sizing", "auto", 1},
        {"font-palette", "normal", 1},
        {"font-size", "medium", 1},
        {"font-stretch", "normal", 1},
        {"font-style", "normal", 1},
        {"font-synthesis", "auto,_,auto,_,auto,_,auto", 1},
        {"font-synthesis-position", "auto", 1},
        {"font-synthesis-small-caps", "auto", 1},
        {"font-synthesis-style", "auto", 1},
        {"font-synthesis-weight", "auto", 1},
        {"font-variant", "normal", 1},
        {"font-variant-alternates", "normal", 1},
        {"font-variant-caps", "normal", 1},
        {"font-variant-east-asian", "normal", 1},
        {"font-variant-ligatures", "normal", 1},
        {"font-variant-numeric", "normal", 1},
        {"font-variant-position", "normal", 1},
        {"font-variation-settings", "normal", 1},
        {"font-weight", "normal", 1}
    };
    static const struct {
        const char *name;
        CssPropertyComputation computation;
    } shapes[] = {
        {"font-style", RoboCssPropertyComputationFontStyle},
        {"font-kerning", RoboCssPropertyComputationFontKerning},
        {"font-optical-sizing", RoboCssPropertyComputationFontOpticalSizing},
        {"font-synthesis-weight", RoboCssPropertyComputationFontSynthesis},
        {"font-synthesis-style", RoboCssPropertyComputationFontSynthesis},
        {"font-synthesis-small-caps", RoboCssPropertyComputationFontSynthesis},
        {"font-synthesis-position", RoboCssPropertyComputationFontSynthesis},
        {"font-variant-caps", RoboCssPropertyComputationFontVariantCaps},
        {"font-variant-position",
         RoboCssPropertyComputationFontVariantPosition},
        {"font-stretch", RoboCssPropertyComputationPreserve},
        {"font-variant-numeric", RoboCssPropertyComputationPreserve},
        {"font-variant-east-asian", RoboCssPropertyComputationPreserve},
        {"font-variant-ligatures", RoboCssPropertyComputationPreserve},
        {"font-feature-settings", RoboCssPropertyComputationPreserve},
        {"font-variation-settings", RoboCssPropertyComputationPreserve},
        {"font", RoboCssPropertyComputationPreserve}
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, expected[index].name);
        char shape[64];

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition),
                          expected[index].inherited);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    /* The shape is read from the definition rather than inferred from the name, so a
     * row that had drifted onto another family's computation shows here. */
    for (index = 0u; index < sizeof(shapes) / sizeof(shapes[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, shapes[index].name);

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              shapes[index].computation);
        }
    }

    /* `font-size`'s shape is the shared LENGTH grammar and stays that way: the ten
     * size keywords are carried by that grammar already, and what this phase adds is
     * their *classification* (CssFontSizeKeyword), not a second value model. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "font-size");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              RoboCssPropertyComputationLength);
        }
    }
    /* The `font` shorthand is the one row of the family that is not inherited, and
     * the inherited count on the wide suite would not catch a row that was. */
    {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, "font");

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(CssPropertyDefinitionInherited(definition), 0);
            RoboCheck((definition->flags & RoboCssPropertyFlagShorthand) != 0u);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestFontCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.fonts, 1);
    /* What the flag does not claim: no default stylesheet and no loading. */
    RoboCheckEqInt(caps.defaultStyles, 0);
    RoboCheckEqInt(caps.stylesheetLoading, 0);

    RoboCheckEqInt(RoboCssVersionMinor, 32);
    RoboCheckStrEq(CssVersionString(), "0.32.0");

    /* The shape names a diagnostic prints, one per new computation, and the domain
     * the vocabulary reports under. */
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFontStyle),
                      "font-style");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFontKerning),
                      "font-kerning");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFontOpticalSizing),
                      "font-optical-sizing");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFontSynthesis),
                      "font-synthesis");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFontVariantCaps),
                      "font-variant-caps");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationFontVariantPosition),
                      "font-variant-position");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationFontVariantPosition),
                      1);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)RoboCssPropertyComputationCount),
                      0);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainFont), "font");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainFont), 1);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestFontDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: all-small-caps", &list, &error),
                      RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationFontVariantCaps,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationFontVariantCaps,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    {
        CssDeclarationList otherList = RoboCssCleared;
        CssError otherError = RoboCssCleared;
        CssComputedValue other = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("o: small-caps", &otherList,
                                                 &otherError), RoboOk);
        declaration = CssDeclarationListAt(&otherList, 0u);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationFontVariantCaps,
                              CssDeclarationValue(declaration), &other, NULL),
                          RoboOk);
        /* The two caps rows are different values, not two spellings of one — the
         * same distinction the `all-` classification exists to make visible. */
        RoboCheckEqInt(CssComputedValueEquals(&first, &other), 0);
        CssComputedValueDispose(&other);
        CssDeclarationListDispose(&otherList);
    }

    CssComputedValueDispose(&first);
    CssComputedValueDispose(&second);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssFont(void);

void TestRegisterCssFont(void)
{
    TestRegister("css.font.style_vocabulary", TestFontStyleVocabulary);
    TestRegister("css.font.kerning_vocabulary", TestFontKerningVocabulary);
    TestRegister("css.font.optical_sizing_vocabulary",
                 TestFontOpticalSizingVocabulary);
    TestRegister("css.font.synthesis_vocabulary", TestFontSynthesisVocabulary);
    TestRegister("css.font.variant_caps_vocabulary",
                 TestFontVariantCapsVocabulary);
    TestRegister("css.font.variant_position_vocabulary",
                 TestFontVariantPositionVocabulary);
    TestRegister("css.font.variant_numeric_vocabulary",
                 TestFontVariantNumericVocabulary);
    TestRegister("css.font.variant_east_asian_vocabulary",
                 TestFontVariantEastAsianVocabulary);
    TestRegister("css.font.stretch_vocabulary", TestFontStretchVocabulary);
    TestRegister("css.font.size_keywords", TestFontSizeKeywordVocabulary);
    TestRegister("css.font.face_descriptors", TestFontFaceDescriptorVocabulary);
    TestRegister("css.font.display_vocabulary", TestFontDisplayVocabulary);
    TestRegister("css.font.computed", TestFontComputedShapes);
    TestRegister("css.font.registry", TestFontRegistry);
    TestRegister("css.font.capabilities", TestFontCapabilities);
    TestRegister("css.font.determinism", TestFontDeterminism);
}
