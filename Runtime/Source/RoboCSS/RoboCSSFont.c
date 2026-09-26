/*
 * RoboCSS font vocabulary (Phase 18.14) — see RoboCSSFont.h.
 *
 * Twelve tables, one per closed set, and every accessor below reads one of them.
 * The rows carry the keyword, the enumerator and the classifications a consumer
 * dispatches on; nothing in this file opens a font, measures a glyph, resolves a
 * family name or synthesises anything.
 *
 * The keywords are lowercase because that is how CSS writes them and how the
 * writers below must print them; matching is ASCII case-insensitive against these
 * spellings and against nothing else, so `small-caps` matches and `smallcaps`
 * does not.
 *
 * Two tables here carry *numbers* rather than booleans — `font-stretch`'s
 * percentage per keyword and `font-size`'s ratio per keyword. They are the value's
 * meaning and not a measurement: CSS leaves both to the user agent, so what the
 * table states is the spec's own suggestion and a consumer that multiplied a size
 * by one of these ratios would be making a decision this module does not.
 */

#include "RoboCSS/RoboCSSFont.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/** The one keyword-write body every vocabulary below shares. */
static Result FontWriteKeyword(const char *keyword,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainFont, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainFont, RoboInvalidArgument,
                           "not a font value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainFont, RoboNotSupported,
                           "the font keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

/** Non-zero when @p keyword equals @p spelling, ASCII case-insensitively. */
static int FontKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/**
 * The one "value is not defined" refusal every writer below shares.
 *
 * A writer for an unknown enumerator leaves the buffer empty and reports who the
 * caller was, which is the same discipline every other vocabulary in this module
 * follows: a partial spelling is worse than none, because it looks like an answer.
 */
static Result FontWriteUnknown(char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag,
                               const char *what)
{
    if (buffer && outLength && capacity > 0u) {
        buffer[0] = '\0';
    }
    return RoboCssDiag(diag, RoboCssDomainFont, RoboInvalidArgument, what);
}

/* ------------------------------------------------------------------ */
/* font-style                                                          */
/* ------------------------------------------------------------------ */

typedef struct fontSizeRow {
    CssFontStyle value;
    const char *keyword;
    int slanted;
    int oblique;
} FontStyleRow;

static const FontStyleRow fontStyleVocabulary[] = {
    { RoboCssFontStyleNormal,  "normal",  0, 0 },
    { RoboCssFontStyleItalic,  "italic",  1, 0 },
    { RoboCssFontStyleOblique, "oblique", 1, 1 }
};

#define FONT_STYLE_COUNT \
    (sizeof(fontStyleVocabulary) / sizeof(fontStyleVocabulary[0]))

static const FontStyleRow *FontStyleRowOf(CssFontStyle style)
{
    size_t index;

    for (index = 0u; index < FONT_STYLE_COUNT; index++) {
        if (fontStyleVocabulary[index].value == style) {
            return &fontStyleVocabulary[index];
        }
    }
    return NULL;
}

int CssFontStyleIsKnown(CssFontStyle style)
{
    return FontStyleRowOf(style) != NULL;
}

const char *CssFontStyleName(CssFontStyle style)
{
    const FontStyleRow *row = FontStyleRowOf(style);

    return row ? row->keyword : "";
}

size_t CssFontStyleCount(void)
{
    return FONT_STYLE_COUNT;
}

CssFontStyle CssFontStyleFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontStyleNormal;
    }
    for (index = 0u; index < FONT_STYLE_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontStyleVocabulary[index].keyword)) {
            return fontStyleVocabulary[index].value;
        }
    }
    return RoboCssFontStyleNormal;
}

CssFontStyle CssFontStyleFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontStyleNormal;
    }
    return CssFontStyleFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontStyleSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_STYLE_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontStyleVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontStyleIsSlanted(CssFontStyle style)
{
    const FontStyleRow *row = FontStyleRowOf(style);

    return row ? row->slanted : 0;
}

int CssFontStyleIsOblique(CssFontStyle style)
{
    const FontStyleRow *row = FontStyleRowOf(style);

    return row ? row->oblique : 0;
}

Result CssFontStyleWrite(CssFontStyle style,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag)
{
    const FontStyleRow *row = FontStyleRowOf(style);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-style value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-kerning                                                        */
/* ------------------------------------------------------------------ */

typedef struct fontKerningRow {
    CssFontKerning value;
    const char *keyword;
    int enables;
} FontKerningRow;

static const FontKerningRow fontKerningVocabulary[] = {
    { RoboCssFontKerningAuto,   "auto",   1 },
    { RoboCssFontKerningNormal, "normal", 1 },
    { RoboCssFontKerningNone,   "none",   0 }
};

#define FONT_KERNING_COUNT \
    (sizeof(fontKerningVocabulary) / sizeof(fontKerningVocabulary[0]))

static const FontKerningRow *FontKerningRowOf(CssFontKerning kerning)
{
    size_t index;

    for (index = 0u; index < FONT_KERNING_COUNT; index++) {
        if (fontKerningVocabulary[index].value == kerning) {
            return &fontKerningVocabulary[index];
        }
    }
    return NULL;
}

int CssFontKerningIsKnown(CssFontKerning kerning)
{
    return FontKerningRowOf(kerning) != NULL;
}

const char *CssFontKerningName(CssFontKerning kerning)
{
    const FontKerningRow *row = FontKerningRowOf(kerning);

    return row ? row->keyword : "";
}

size_t CssFontKerningCount(void)
{
    return FONT_KERNING_COUNT;
}

CssFontKerning CssFontKerningFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontKerningAuto;
    }
    for (index = 0u; index < FONT_KERNING_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontKerningVocabulary[index].keyword)) {
            return fontKerningVocabulary[index].value;
        }
    }
    return RoboCssFontKerningAuto;
}

CssFontKerning CssFontKerningFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontKerningAuto;
    }
    return CssFontKerningFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontKerningSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_KERNING_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontKerningVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontKerningEnablesKerning(CssFontKerning kerning)
{
    const FontKerningRow *row = FontKerningRowOf(kerning);

    return row ? row->enables : 0;
}

Result CssFontKerningWrite(CssFontKerning kerning,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const FontKerningRow *row = FontKerningRowOf(kerning);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-kerning value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-optical-sizing                                                 */
/* ------------------------------------------------------------------ */

typedef struct fontOpticalSizingRow {
    CssFontOpticalSizing value;
    const char *keyword;
    int automatic;
} FontOpticalSizingRow;

static const FontOpticalSizingRow fontOpticalSizingVocabulary[] = {
    { RoboCssFontOpticalSizingAuto, "auto", 1 },
    { RoboCssFontOpticalSizingNone, "none", 0 }
};

#define FONT_OPTICAL_SIZING_COUNT \
    (sizeof(fontOpticalSizingVocabulary) / \
     sizeof(fontOpticalSizingVocabulary[0]))

static const FontOpticalSizingRow *FontOpticalSizingRowOf(
    CssFontOpticalSizing sizing)
{
    size_t index;

    for (index = 0u; index < FONT_OPTICAL_SIZING_COUNT; index++) {
        if (fontOpticalSizingVocabulary[index].value == sizing) {
            return &fontOpticalSizingVocabulary[index];
        }
    }
    return NULL;
}

int CssFontOpticalSizingIsKnown(CssFontOpticalSizing sizing)
{
    return FontOpticalSizingRowOf(sizing) != NULL;
}

const char *CssFontOpticalSizingName(CssFontOpticalSizing sizing)
{
    const FontOpticalSizingRow *row = FontOpticalSizingRowOf(sizing);

    return row ? row->keyword : "";
}

size_t CssFontOpticalSizingCount(void)
{
    return FONT_OPTICAL_SIZING_COUNT;
}

CssFontOpticalSizing CssFontOpticalSizingFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontOpticalSizingAuto;
    }
    for (index = 0u; index < FONT_OPTICAL_SIZING_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontOpticalSizingVocabulary[index].keyword)) {
            return fontOpticalSizingVocabulary[index].value;
        }
    }
    return RoboCssFontOpticalSizingAuto;
}

CssFontOpticalSizing CssFontOpticalSizingFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontOpticalSizingAuto;
    }
    return CssFontOpticalSizingFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontOpticalSizingSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_OPTICAL_SIZING_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontOpticalSizingVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontOpticalSizingIsAutomatic(CssFontOpticalSizing sizing)
{
    const FontOpticalSizingRow *row = FontOpticalSizingRowOf(sizing);

    return row ? row->automatic : 0;
}

Result CssFontOpticalSizingWrite(CssFontOpticalSizing sizing,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag)
{
    const FontOpticalSizingRow *row = FontOpticalSizingRowOf(sizing);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-optical-sizing value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-synthesis                                                      */
/* ------------------------------------------------------------------ */

typedef struct fontSynthesisRow {
    CssFontSynthesis value;
    const char *keyword;
    int allows;
} FontSynthesisRow;

static const FontSynthesisRow fontSynthesisVocabulary[] = {
    { RoboCssFontSynthesisAuto, "auto", 1 },
    { RoboCssFontSynthesisNone, "none", 0 }
};

#define FONT_SYNTHESIS_COUNT \
    (sizeof(fontSynthesisVocabulary) / sizeof(fontSynthesisVocabulary[0]))

static const FontSynthesisRow *FontSynthesisRowOf(CssFontSynthesis synthesis)
{
    size_t index;

    for (index = 0u; index < FONT_SYNTHESIS_COUNT; index++) {
        if (fontSynthesisVocabulary[index].value == synthesis) {
            return &fontSynthesisVocabulary[index];
        }
    }
    return NULL;
}

int CssFontSynthesisIsKnown(CssFontSynthesis synthesis)
{
    return FontSynthesisRowOf(synthesis) != NULL;
}

const char *CssFontSynthesisName(CssFontSynthesis synthesis)
{
    const FontSynthesisRow *row = FontSynthesisRowOf(synthesis);

    return row ? row->keyword : "";
}

size_t CssFontSynthesisCount(void)
{
    return FONT_SYNTHESIS_COUNT;
}

CssFontSynthesis CssFontSynthesisFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontSynthesisAuto;
    }
    for (index = 0u; index < FONT_SYNTHESIS_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontSynthesisVocabulary[index].keyword)) {
            return fontSynthesisVocabulary[index].value;
        }
    }
    return RoboCssFontSynthesisAuto;
}

CssFontSynthesis CssFontSynthesisFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontSynthesisAuto;
    }
    return CssFontSynthesisFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontSynthesisSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_SYNTHESIS_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontSynthesisVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontSynthesisAllows(CssFontSynthesis synthesis)
{
    const FontSynthesisRow *row = FontSynthesisRowOf(synthesis);

    return row ? row->allows : 0;
}

Result CssFontSynthesisWrite(CssFontSynthesis synthesis,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag)
{
    const FontSynthesisRow *row = FontSynthesisRowOf(synthesis);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-synthesis value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-variant-caps                                                   */
/* ------------------------------------------------------------------ */

typedef struct fontVariantCapsRow {
    CssFontVariantCaps value;
    const char *keyword;
    int capsVariant;
    int affectsLowercase;
    int petite;
} FontVariantCapsRow;

static const FontVariantCapsRow fontVariantCapsVocabulary[] = {
    { RoboCssFontVariantCapsNormal,        "normal",          0, 0, 0 },
    { RoboCssFontVariantCapsSmallCaps,     "small-caps",      1, 0, 0 },
    { RoboCssFontVariantCapsAllSmallCaps,  "all-small-caps",  1, 1, 0 },
    { RoboCssFontVariantCapsPetiteCaps,    "petite-caps",     1, 0, 1 },
    { RoboCssFontVariantCapsAllPetiteCaps, "all-petite-caps", 1, 1, 1 },
    { RoboCssFontVariantCapsUnicase,       "unicase",         1, 0, 0 },
    { RoboCssFontVariantCapsTitlingCaps,   "titling-caps",    1, 0, 0 }
};

#define FONT_VARIANT_CAPS_COUNT \
    (sizeof(fontVariantCapsVocabulary) / sizeof(fontVariantCapsVocabulary[0]))

static const FontVariantCapsRow *FontVariantCapsRowOf(CssFontVariantCaps caps)
{
    size_t index;

    for (index = 0u; index < FONT_VARIANT_CAPS_COUNT; index++) {
        if (fontVariantCapsVocabulary[index].value == caps) {
            return &fontVariantCapsVocabulary[index];
        }
    }
    return NULL;
}

int CssFontVariantCapsIsKnown(CssFontVariantCaps caps)
{
    return FontVariantCapsRowOf(caps) != NULL;
}

const char *CssFontVariantCapsName(CssFontVariantCaps caps)
{
    const FontVariantCapsRow *row = FontVariantCapsRowOf(caps);

    return row ? row->keyword : "";
}

size_t CssFontVariantCapsCount(void)
{
    return FONT_VARIANT_CAPS_COUNT;
}

CssFontVariantCaps CssFontVariantCapsFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontVariantCapsNormal;
    }
    for (index = 0u; index < FONT_VARIANT_CAPS_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantCapsVocabulary[index].keyword)) {
            return fontVariantCapsVocabulary[index].value;
        }
    }
    return RoboCssFontVariantCapsNormal;
}

CssFontVariantCaps CssFontVariantCapsFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontVariantCapsNormal;
    }
    return CssFontVariantCapsFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontVariantCapsSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_VARIANT_CAPS_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantCapsVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontVariantCapsIsCapsVariant(CssFontVariantCaps caps)
{
    const FontVariantCapsRow *row = FontVariantCapsRowOf(caps);

    return row ? row->capsVariant : 0;
}

int CssFontVariantCapsAffectsLowercase(CssFontVariantCaps caps)
{
    const FontVariantCapsRow *row = FontVariantCapsRowOf(caps);

    return row ? row->affectsLowercase : 0;
}

int CssFontVariantCapsIsPetite(CssFontVariantCaps caps)
{
    const FontVariantCapsRow *row = FontVariantCapsRowOf(caps);

    return row ? row->petite : 0;
}

Result CssFontVariantCapsWrite(CssFontVariantCaps caps,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag)
{
    const FontVariantCapsRow *row = FontVariantCapsRowOf(caps);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-variant-caps value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-variant-numeric                                                */
/* ------------------------------------------------------------------ */

typedef struct fontVariantNumericRow {
    CssFontVariantNumeric value;
    const char *keyword;
    CssFontVariantNumericKind kind;
} FontVariantNumericRow;

static const FontVariantNumericRow fontVariantNumericVocabulary[] = {
    { RoboCssFontVariantNumericNormal, "normal",
      RoboCssFontVariantNumericKindNormal },
    { RoboCssFontVariantNumericOrdinal, "ordinal",
      RoboCssFontVariantNumericKindOrdinal },
    { RoboCssFontVariantNumericSlashedZero, "slashed-zero",
      RoboCssFontVariantNumericKindSlashedZero },
    { RoboCssFontVariantNumericLiningNums, "lining-nums",
      RoboCssFontVariantNumericKindFigureStyle },
    { RoboCssFontVariantNumericOldstyleNums, "oldstyle-nums",
      RoboCssFontVariantNumericKindFigureStyle },
    { RoboCssFontVariantNumericProportionalNums, "proportional-nums",
      RoboCssFontVariantNumericKindFigureSpacing },
    { RoboCssFontVariantNumericTabularNums, "tabular-nums",
      RoboCssFontVariantNumericKindFigureSpacing },
    { RoboCssFontVariantNumericDiagonalFractions, "diagonal-fractions",
      RoboCssFontVariantNumericKindFraction },
    { RoboCssFontVariantNumericStackedFractions, "stacked-fractions",
      RoboCssFontVariantNumericKindFraction }
};

#define FONT_VARIANT_NUMERIC_COUNT \
    (sizeof(fontVariantNumericVocabulary) / \
     sizeof(fontVariantNumericVocabulary[0]))

static const FontVariantNumericRow *FontVariantNumericRowOf(
    CssFontVariantNumeric numeric)
{
    size_t index;

    for (index = 0u; index < FONT_VARIANT_NUMERIC_COUNT; index++) {
        if (fontVariantNumericVocabulary[index].value == numeric) {
            return &fontVariantNumericVocabulary[index];
        }
    }
    return NULL;
}

int CssFontVariantNumericIsKnown(CssFontVariantNumeric numeric)
{
    return FontVariantNumericRowOf(numeric) != NULL;
}

const char *CssFontVariantNumericName(CssFontVariantNumeric numeric)
{
    const FontVariantNumericRow *row = FontVariantNumericRowOf(numeric);

    return row ? row->keyword : "";
}

size_t CssFontVariantNumericCount(void)
{
    return FONT_VARIANT_NUMERIC_COUNT;
}

CssFontVariantNumeric CssFontVariantNumericFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontVariantNumericNormal;
    }
    for (index = 0u; index < FONT_VARIANT_NUMERIC_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantNumericVocabulary[index].keyword)) {
            return fontVariantNumericVocabulary[index].value;
        }
    }
    return RoboCssFontVariantNumericNormal;
}

CssFontVariantNumeric CssFontVariantNumericFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontVariantNumericNormal;
    }
    return CssFontVariantNumericFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontVariantNumericSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_VARIANT_NUMERIC_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantNumericVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

CssFontVariantNumericKind CssFontVariantNumericKindOf(
    CssFontVariantNumeric numeric)
{
    const FontVariantNumericRow *row = FontVariantNumericRowOf(numeric);

    return row ? row->kind : RoboCssFontVariantNumericKindNormal;
}

int CssFontVariantNumericKindIsKnown(CssFontVariantNumericKind kind)
{
    return kind >= RoboCssFontVariantNumericKindNormal &&
           kind < (CssFontVariantNumericKind)RoboCssFontVariantNumericKindCount;
}

const char *CssFontVariantNumericKindName(CssFontVariantNumericKind kind)
{
    switch (kind) {
    case RoboCssFontVariantNumericKindNormal:
        return "normal";
    case RoboCssFontVariantNumericKindOrdinal:
        return "ordinal";
    case RoboCssFontVariantNumericKindSlashedZero:
        return "slashed zero";
    case RoboCssFontVariantNumericKindFigureStyle:
        return "figure style";
    case RoboCssFontVariantNumericKindFigureSpacing:
        return "figure spacing";
    case RoboCssFontVariantNumericKindFraction:
        return "fraction";
    default:
        return "unknown";
    }
}

Result CssFontVariantNumericWrite(CssFontVariantNumeric numeric,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    const FontVariantNumericRow *row = FontVariantNumericRowOf(numeric);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-variant-numeric value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-variant-east-asian                                             */
/* ------------------------------------------------------------------ */

typedef struct fontVariantEastAsianRow {
    CssFontVariantEastAsian value;
    const char *keyword;
    CssFontVariantEastAsianKind kind;
} FontVariantEastAsianRow;

static const FontVariantEastAsianRow fontVariantEastAsianVocabulary[] = {
    { RoboCssFontVariantEastAsianNormal, "normal",
      RoboCssFontVariantEastAsianKindNormal },
    { RoboCssFontVariantEastAsianJis78, "jis78",
      RoboCssFontVariantEastAsianKindVariant },
    { RoboCssFontVariantEastAsianJis83, "jis83",
      RoboCssFontVariantEastAsianKindVariant },
    { RoboCssFontVariantEastAsianJis90, "jis90",
      RoboCssFontVariantEastAsianKindVariant },
    { RoboCssFontVariantEastAsianJis04, "jis04",
      RoboCssFontVariantEastAsianKindVariant },
    { RoboCssFontVariantEastAsianSimplified, "simplified",
      RoboCssFontVariantEastAsianKindVariant },
    { RoboCssFontVariantEastAsianTraditional, "traditional",
      RoboCssFontVariantEastAsianKindVariant },
    { RoboCssFontVariantEastAsianFullWidth, "full-width",
      RoboCssFontVariantEastAsianKindWidth },
    { RoboCssFontVariantEastAsianProportionalWidth, "proportional-width",
      RoboCssFontVariantEastAsianKindWidth },
    { RoboCssFontVariantEastAsianRuby, "ruby",
      RoboCssFontVariantEastAsianKindRuby }
};

#define FONT_VARIANT_EAST_ASIAN_COUNT \
    (sizeof(fontVariantEastAsianVocabulary) / \
     sizeof(fontVariantEastAsianVocabulary[0]))

static const FontVariantEastAsianRow *FontVariantEastAsianRowOf(
    CssFontVariantEastAsian eastAsian)
{
    size_t index;

    for (index = 0u; index < FONT_VARIANT_EAST_ASIAN_COUNT; index++) {
        if (fontVariantEastAsianVocabulary[index].value == eastAsian) {
            return &fontVariantEastAsianVocabulary[index];
        }
    }
    return NULL;
}

int CssFontVariantEastAsianIsKnown(CssFontVariantEastAsian eastAsian)
{
    return FontVariantEastAsianRowOf(eastAsian) != NULL;
}

const char *CssFontVariantEastAsianName(CssFontVariantEastAsian eastAsian)
{
    const FontVariantEastAsianRow *row = FontVariantEastAsianRowOf(eastAsian);

    return row ? row->keyword : "";
}

size_t CssFontVariantEastAsianCount(void)
{
    return FONT_VARIANT_EAST_ASIAN_COUNT;
}

CssFontVariantEastAsian CssFontVariantEastAsianFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontVariantEastAsianNormal;
    }
    for (index = 0u; index < FONT_VARIANT_EAST_ASIAN_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantEastAsianVocabulary[index].keyword)) {
            return fontVariantEastAsianVocabulary[index].value;
        }
    }
    return RoboCssFontVariantEastAsianNormal;
}

CssFontVariantEastAsian CssFontVariantEastAsianFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontVariantEastAsianNormal;
    }
    return CssFontVariantEastAsianFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontVariantEastAsianSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_VARIANT_EAST_ASIAN_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantEastAsianVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

CssFontVariantEastAsianKind CssFontVariantEastAsianKindOf(
    CssFontVariantEastAsian eastAsian)
{
    const FontVariantEastAsianRow *row = FontVariantEastAsianRowOf(eastAsian);

    return row ? row->kind : RoboCssFontVariantEastAsianKindNormal;
}

int CssFontVariantEastAsianKindIsKnown(CssFontVariantEastAsianKind kind)
{
    return kind >= RoboCssFontVariantEastAsianKindNormal &&
           kind <
               (CssFontVariantEastAsianKind)RoboCssFontVariantEastAsianKindCount;
}

const char *CssFontVariantEastAsianKindName(CssFontVariantEastAsianKind kind)
{
    switch (kind) {
    case RoboCssFontVariantEastAsianKindNormal:
        return "normal";
    case RoboCssFontVariantEastAsianKindVariant:
        return "glyph form";
    case RoboCssFontVariantEastAsianKindWidth:
        return "width";
    case RoboCssFontVariantEastAsianKindRuby:
        return "ruby";
    default:
        return "unknown";
    }
}

Result CssFontVariantEastAsianWrite(CssFontVariantEastAsian eastAsian,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag)
{
    const FontVariantEastAsianRow *row =
        FontVariantEastAsianRowOf(eastAsian);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-variant-east-asian value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-variant-position                                               */
/* ------------------------------------------------------------------ */

typedef struct fontVariantPositionRow {
    CssFontVariantPosition value;
    const char *keyword;
    int shifted;
    int superscript;
} FontVariantPositionRow;

static const FontVariantPositionRow fontVariantPositionVocabulary[] = {
    { RoboCssFontVariantPositionNormal, "normal", 0, 0 },
    { RoboCssFontVariantPositionSub,    "sub",    1, 0 },
    { RoboCssFontVariantPositionSuper,  "super",  1, 1 }
};

#define FONT_VARIANT_POSITION_COUNT \
    (sizeof(fontVariantPositionVocabulary) / \
     sizeof(fontVariantPositionVocabulary[0]))

static const FontVariantPositionRow *FontVariantPositionRowOf(
    CssFontVariantPosition position)
{
    size_t index;

    for (index = 0u; index < FONT_VARIANT_POSITION_COUNT; index++) {
        if (fontVariantPositionVocabulary[index].value == position) {
            return &fontVariantPositionVocabulary[index];
        }
    }
    return NULL;
}

int CssFontVariantPositionIsKnown(CssFontVariantPosition position)
{
    return FontVariantPositionRowOf(position) != NULL;
}

const char *CssFontVariantPositionName(CssFontVariantPosition position)
{
    const FontVariantPositionRow *row = FontVariantPositionRowOf(position);

    return row ? row->keyword : "";
}

size_t CssFontVariantPositionCount(void)
{
    return FONT_VARIANT_POSITION_COUNT;
}

CssFontVariantPosition CssFontVariantPositionFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontVariantPositionNormal;
    }
    for (index = 0u; index < FONT_VARIANT_POSITION_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantPositionVocabulary[index].keyword)) {
            return fontVariantPositionVocabulary[index].value;
        }
    }
    return RoboCssFontVariantPositionNormal;
}

CssFontVariantPosition CssFontVariantPositionFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontVariantPositionNormal;
    }
    return CssFontVariantPositionFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontVariantPositionSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_VARIANT_POSITION_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontVariantPositionVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontVariantPositionIsShifted(CssFontVariantPosition position)
{
    const FontVariantPositionRow *row = FontVariantPositionRowOf(position);

    return row ? row->shifted : 0;
}

int CssFontVariantPositionIsSuperscript(CssFontVariantPosition position)
{
    const FontVariantPositionRow *row = FontVariantPositionRowOf(position);

    return row ? row->superscript : 0;
}

Result CssFontVariantPositionWrite(CssFontVariantPosition position,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag)
{
    const FontVariantPositionRow *row = FontVariantPositionRowOf(position);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-variant-position value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-stretch                                                        */
/* ------------------------------------------------------------------ */

typedef struct fontStretchRow {
    CssFontStretch value;
    const char *keyword;
    double percentage;
} FontStretchRow;

static const FontStretchRow fontStretchVocabulary[] = {
    { RoboCssFontStretchUltraCondensed, "ultra-condensed",  50.0 },
    { RoboCssFontStretchExtraCondensed, "extra-condensed",  62.5 },
    { RoboCssFontStretchCondensed,      "condensed",        75.0 },
    { RoboCssFontStretchSemiCondensed,  "semi-condensed",   87.5 },
    { RoboCssFontStretchNormal,         "normal",          100.0 },
    { RoboCssFontStretchSemiExpanded,   "semi-expanded",   112.5 },
    { RoboCssFontStretchExpanded,       "expanded",        125.0 },
    { RoboCssFontStretchExtraExpanded,  "extra-expanded",  150.0 },
    { RoboCssFontStretchUltraExpanded,  "ultra-expanded",  200.0 }
};

#define FONT_STRETCH_COUNT \
    (sizeof(fontStretchVocabulary) / sizeof(fontStretchVocabulary[0]))

static const FontStretchRow *FontStretchRowOf(CssFontStretch stretch)
{
    size_t index;

    for (index = 0u; index < FONT_STRETCH_COUNT; index++) {
        if (fontStretchVocabulary[index].value == stretch) {
            return &fontStretchVocabulary[index];
        }
    }
    return NULL;
}

int CssFontStretchIsKnown(CssFontStretch stretch)
{
    return FontStretchRowOf(stretch) != NULL;
}

const char *CssFontStretchName(CssFontStretch stretch)
{
    const FontStretchRow *row = FontStretchRowOf(stretch);

    return row ? row->keyword : "";
}

size_t CssFontStretchCount(void)
{
    return FONT_STRETCH_COUNT;
}

CssFontStretch CssFontStretchFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontStretchNormal;
    }
    for (index = 0u; index < FONT_STRETCH_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontStretchVocabulary[index].keyword)) {
            return fontStretchVocabulary[index].value;
        }
    }
    return RoboCssFontStretchNormal;
}

CssFontStretch CssFontStretchFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontStretchNormal;
    }
    return CssFontStretchFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontStretchSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_STRETCH_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontStretchVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

double CssFontStretchPercentage(CssFontStretch stretch)
{
    const FontStretchRow *row = FontStretchRowOf(stretch);

    return row ? row->percentage : 100.0;
}

CssFontStretch CssFontStretchFromPercentage(double percentage)
{
    /*
     * The table is ordered along the axis, so the answer is the nearer of the two
     * rows a percentage falls between. A tie takes the *narrower* one, which is the
     * rule stated once here rather than a coin left to each caller: `100%` is
     * `normal` exactly, and `106.25%` — halfway between `normal` and
     * `semi-expanded` — is `normal`.
     */
    size_t index;
    const FontStretchRow *best = &fontStretchVocabulary[0];
    double bestDistance = percentage - best->percentage;

    if (bestDistance < 0.0) {
        bestDistance = -bestDistance;
    }
    for (index = 1u; index < FONT_STRETCH_COUNT; index++) {
        double distance = percentage - fontStretchVocabulary[index].percentage;

        if (distance < 0.0) {
            distance = -distance;
        }
        /* `<` rather than `<=`: on a tie the earlier — narrower — row wins. */
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &fontStretchVocabulary[index];
        }
    }
    return best->value;
}

int CssFontStretchIsCondensed(CssFontStretch stretch)
{
    const FontStretchRow *row = FontStretchRowOf(stretch);

    return row && row->percentage < 100.0 ? 1 : 0;
}

int CssFontStretchIsExpanded(CssFontStretch stretch)
{
    const FontStretchRow *row = FontStretchRowOf(stretch);

    return row && row->percentage > 100.0 ? 1 : 0;
}

int CssFontStretchIsNormal(CssFontStretch stretch)
{
    return stretch == RoboCssFontStretchNormal;
}

Result CssFontStretchWrite(CssFontStretch stretch,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const FontStretchRow *row = FontStretchRowOf(stretch);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-stretch value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-size keywords                                                  */
/* ------------------------------------------------------------------ */

typedef struct fontSizeKeywordRow {
    CssFontSizeKeyword value;
    const char *keyword;
    int absolute;
    int relative;
    double ratio;
} FontSizeKeywordRow;

static const FontSizeKeywordRow fontSizeKeywordVocabulary[] = {
    { RoboCssFontSizeKeywordXxSmall,  "xx-small",  1, 0,   0.6 },
    { RoboCssFontSizeKeywordXSmall,   "x-small",   1, 0,   0.75 },
    { RoboCssFontSizeKeywordSmall,    "small",     1, 0,   0.8888888888888888 },
    { RoboCssFontSizeKeywordMedium,   "medium",    1, 0,   1.0 },
    { RoboCssFontSizeKeywordLarge,    "large",     1, 0,   1.2 },
    { RoboCssFontSizeKeywordXLarge,   "x-large",   1, 0,   1.5 },
    { RoboCssFontSizeKeywordXxLarge,  "xx-large",  1, 0,   2.0 },
    { RoboCssFontSizeKeywordXxxLarge, "xxx-large", 1, 0,   3.0 },
    { RoboCssFontSizeKeywordLarger,   "larger",    0, 1,   1.2 },
    { RoboCssFontSizeKeywordSmaller,  "smaller",   0, 1,   0.8333333333333334 }
};

#define FONT_SIZE_KEYWORD_COUNT \
    (sizeof(fontSizeKeywordVocabulary) / sizeof(fontSizeKeywordVocabulary[0]))

static const FontSizeKeywordRow *FontSizeKeywordRowOf(CssFontSizeKeyword size)
{
    size_t index;

    for (index = 0u; index < FONT_SIZE_KEYWORD_COUNT; index++) {
        if (fontSizeKeywordVocabulary[index].value == size) {
            return &fontSizeKeywordVocabulary[index];
        }
    }
    return NULL;
}

int CssFontSizeKeywordIsKnown(CssFontSizeKeyword size)
{
    return FontSizeKeywordRowOf(size) != NULL;
}

const char *CssFontSizeKeywordName(CssFontSizeKeyword size)
{
    const FontSizeKeywordRow *row = FontSizeKeywordRowOf(size);

    return row ? row->keyword : "";
}

size_t CssFontSizeKeywordCount(void)
{
    return FONT_SIZE_KEYWORD_COUNT;
}

CssFontSizeKeyword CssFontSizeKeywordFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontSizeKeywordMedium;
    }
    for (index = 0u; index < FONT_SIZE_KEYWORD_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontSizeKeywordVocabulary[index].keyword)) {
            return fontSizeKeywordVocabulary[index].value;
        }
    }
    return RoboCssFontSizeKeywordMedium;
}

CssFontSizeKeyword CssFontSizeKeywordFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontSizeKeywordMedium;
    }
    return CssFontSizeKeywordFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontSizeKeywordSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_SIZE_KEYWORD_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontSizeKeywordVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontSizeKeywordIsAbsolute(CssFontSizeKeyword size)
{
    const FontSizeKeywordRow *row = FontSizeKeywordRowOf(size);

    return row ? row->absolute : 0;
}

int CssFontSizeKeywordIsRelative(CssFontSizeKeyword size)
{
    const FontSizeKeywordRow *row = FontSizeKeywordRowOf(size);

    return row ? row->relative : 0;
}

double CssFontSizeKeywordRatio(CssFontSizeKeyword size)
{
    const FontSizeKeywordRow *row = FontSizeKeywordRowOf(size);

    return row ? row->ratio : 1.0;
}

Result CssFontSizeKeywordWrite(CssFontSizeKeyword size,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag)
{
    const FontSizeKeywordRow *row = FontSizeKeywordRowOf(size);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-size keyword");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* @font-face descriptors                                              */
/* ------------------------------------------------------------------ */

typedef struct fontFaceDescriptorRow {
    CssFontFaceDescriptor value;
    const char *keyword;
    int propertyShared;
} FontFaceDescriptorRow;

static const FontFaceDescriptorRow fontFaceDescriptorVocabulary[] = {
    { RoboCssFontFaceDescriptorSrc,                    "src",                     0 },
    { RoboCssFontFaceDescriptorFontFamily,             "font-family",             1 },
    { RoboCssFontFaceDescriptorFontStyle,              "font-style",              1 },
    { RoboCssFontFaceDescriptorFontWeight,             "font-weight",             1 },
    { RoboCssFontFaceDescriptorFontStretch,            "font-stretch",            1 },
    { RoboCssFontFaceDescriptorFontDisplay,            "font-display",            0 },
    { RoboCssFontFaceDescriptorUnicodeRange,           "unicode-range",           0 },
    { RoboCssFontFaceDescriptorFontFeatureSettings,    "font-feature-settings",   1 },
    { RoboCssFontFaceDescriptorFontVariationSettings,  "font-variation-settings", 1 },
    { RoboCssFontFaceDescriptorFontNamedInstance,      "font-named-instance",     0 },
    { RoboCssFontFaceDescriptorAscentOverride,         "ascent-override",         0 },
    { RoboCssFontFaceDescriptorDescentOverride,        "descent-override",        0 },
    { RoboCssFontFaceDescriptorLineGapOverride,        "line-gap-override",       0 },
    { RoboCssFontFaceDescriptorSizeAdjust,             "size-adjust",             0 },
    { RoboCssFontFaceDescriptorFontLanguageOverride,   "font-language-override",  1 },
    { RoboCssFontFaceDescriptorFontKerning,            "font-kerning",            1 },
    { RoboCssFontFaceDescriptorFontOpticalSizing,      "font-optical-sizing",     1 },
    { RoboCssFontFaceDescriptorFontSynthesisWeight,    "font-synthesis-weight",   1 },
    { RoboCssFontFaceDescriptorFontSynthesisStyle,     "font-synthesis-style",    1 },
    { RoboCssFontFaceDescriptorFontSynthesisSmallCaps, "font-synthesis-small-caps", 1 },
    { RoboCssFontFaceDescriptorFontSynthesisPosition,  "font-synthesis-position", 1 },
    { RoboCssFontFaceDescriptorFontVariantCaps,        "font-variant-caps",       1 },
    { RoboCssFontFaceDescriptorFontVariantEastAsian,   "font-variant-east-asian", 1 },
    { RoboCssFontFaceDescriptorFontVariantLigatures,   "font-variant-ligatures",  1 },
    { RoboCssFontFaceDescriptorFontVariantNumeric,     "font-variant-numeric",    1 },
    { RoboCssFontFaceDescriptorFontVariantPosition,    "font-variant-position",   1 },
    { RoboCssFontFaceDescriptorFontPalette,            "font-palette",            1 }
};

#define FONT_FACE_DESCRIPTOR_COUNT \
    (sizeof(fontFaceDescriptorVocabulary) / \
     sizeof(fontFaceDescriptorVocabulary[0]))

static const FontFaceDescriptorRow *FontFaceDescriptorRowOf(
    CssFontFaceDescriptor descriptor)
{
    size_t index;

    for (index = 0u; index < FONT_FACE_DESCRIPTOR_COUNT; index++) {
        if (fontFaceDescriptorVocabulary[index].value == descriptor) {
            return &fontFaceDescriptorVocabulary[index];
        }
    }
    return NULL;
}

int CssFontFaceDescriptorIsKnown(CssFontFaceDescriptor descriptor)
{
    return FontFaceDescriptorRowOf(descriptor) != NULL;
}

const char *CssFontFaceDescriptorName(CssFontFaceDescriptor descriptor)
{
    const FontFaceDescriptorRow *row = FontFaceDescriptorRowOf(descriptor);

    return row ? row->keyword : "";
}

size_t CssFontFaceDescriptorCount(void)
{
    return FONT_FACE_DESCRIPTOR_COUNT;
}

CssFontFaceDescriptor CssFontFaceDescriptorFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontFaceDescriptorSrc;
    }
    for (index = 0u; index < FONT_FACE_DESCRIPTOR_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontFaceDescriptorVocabulary[index].keyword)) {
            return fontFaceDescriptorVocabulary[index].value;
        }
    }
    /* An unknown name answers the *first* row, which is `src`: the only zero this
     * enumerator has, and the door that tells a genuine `src` from the default is
     * SliceIsKnown rather than the answer. */
    return RoboCssFontFaceDescriptorSrc;
}

CssFontFaceDescriptor CssFontFaceDescriptorFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontFaceDescriptorSrc;
    }
    return CssFontFaceDescriptorFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontFaceDescriptorSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_FACE_DESCRIPTOR_COUNT; index++) {
        if (FontKeywordEquals(keyword,
                              fontFaceDescriptorVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontFaceDescriptorIsPropertyShared(CssFontFaceDescriptor descriptor)
{
    const FontFaceDescriptorRow *row = FontFaceDescriptorRowOf(descriptor);

    return row ? row->propertyShared : 0;
}

Result CssFontFaceDescriptorWrite(CssFontFaceDescriptor descriptor,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag)
{
    const FontFaceDescriptorRow *row = FontFaceDescriptorRowOf(descriptor);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-face descriptor");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* font-display                                                        */
/* ------------------------------------------------------------------ */

typedef struct fontDisplayRow {
    CssFontDisplay value;
    const char *keyword;
    int blocks;
    int blockPeriodMillis;
    int swaps;
} FontDisplayRow;

static const FontDisplayRow fontDisplayVocabulary[] = {
    { RoboCssFontDisplayAuto,     "auto",     1,    0, 0 },
    { RoboCssFontDisplayBlock,    "block",    1, 3000, 0 },
    { RoboCssFontDisplaySwap,     "swap",     0,    0, 1 },
    { RoboCssFontDisplayFallback, "fallback", 1,  100, 1 },
    { RoboCssFontDisplayOptional, "optional", 1,  100, 0 }
};

#define FONT_DISPLAY_COUNT \
    (sizeof(fontDisplayVocabulary) / sizeof(fontDisplayVocabulary[0]))

static const FontDisplayRow *FontDisplayRowOf(CssFontDisplay display)
{
    size_t index;

    for (index = 0u; index < FONT_DISPLAY_COUNT; index++) {
        if (fontDisplayVocabulary[index].value == display) {
            return &fontDisplayVocabulary[index];
        }
    }
    return NULL;
}

int CssFontDisplayIsKnown(CssFontDisplay display)
{
    return FontDisplayRowOf(display) != NULL;
}

const char *CssFontDisplayName(CssFontDisplay display)
{
    const FontDisplayRow *row = FontDisplayRowOf(display);

    return row ? row->keyword : "";
}

size_t CssFontDisplayCount(void)
{
    return FONT_DISPLAY_COUNT;
}

CssFontDisplay CssFontDisplayFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssFontDisplayAuto;
    }
    for (index = 0u; index < FONT_DISPLAY_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontDisplayVocabulary[index].keyword)) {
            return fontDisplayVocabulary[index].value;
        }
    }
    return RoboCssFontDisplayAuto;
}

CssFontDisplay CssFontDisplayFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssFontDisplayAuto;
    }
    return CssFontDisplayFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssFontDisplaySliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < FONT_DISPLAY_COUNT; index++) {
        if (FontKeywordEquals(keyword, fontDisplayVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssFontDisplayBlocksRendering(CssFontDisplay display)
{
    const FontDisplayRow *row = FontDisplayRowOf(display);

    /* An unknown value answers 1: the answer that risks invisible text is the one a
     * consumer must not get for free. */
    return row ? row->blocks : 1;
}

int CssFontDisplayBlockPeriodMillis(CssFontDisplay display)
{
    const FontDisplayRow *row = FontDisplayRowOf(display);

    return row ? row->blockPeriodMillis : 0;
}

int CssFontDisplaySwapsAfterBlockPeriod(CssFontDisplay display)
{
    const FontDisplayRow *row = FontDisplayRowOf(display);

    return row ? row->swaps : 0;
}

int CssFontDisplayIsAutomatic(CssFontDisplay display)
{
    return display == RoboCssFontDisplayAuto;
}

Result CssFontDisplayWrite(CssFontDisplay display,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag)
{
    const FontDisplayRow *row = FontDisplayRowOf(display);

    if (!row) {
        return FontWriteUnknown(buffer, capacity, outLength, diag,
                                "not a font-display value");
    }
    return FontWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}
