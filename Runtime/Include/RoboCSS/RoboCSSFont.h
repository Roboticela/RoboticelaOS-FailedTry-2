/*
 * RoboCSS font vocabulary (Phase 18.14).
 *
 * The font family is the fourth property *family* this build types (after the box
 * model, the background family, the border family and the flex/grid/table/column
 * sets of Phase 18), and it is the first whose values describe **a request rather
 * than a result**.
 *
 * That is the one fact worth reading twice. `font-weight: 700` does not name a
 * face; it *asks* for one, and which face answers is the font database's business —
 * in this build the resolved font request 12.17 built (RoboLayout's
 * `LayoutFontRequestSelect` over RoboDesktop's provider). Nothing in this module
 * loads a font, so nothing here can be wrong about which face exists: a value says
 * what the author asked for, with the classifications a consumer reads.
 *
 * The second fact is `@font-face`, and it is why `CssFontFaceDescriptor` exists
 * rather than a set of properties. Inside a `@font-face` block the same spellings
 * mean something different: `font-weight: 700` *declares the face's own weight*
 * rather than selecting one, `font-display` exists only there, and `src` and
 * `unicode-range` exist only there. A build that read a `@font-face` block as an
 * element's declarations would read the request as the answer.
 *
 * The third fact is that `font-variant-*` is a family of **independent axes** —
 * caps, numeric, east-asian, position, ligatures, alternates — which compose on one
 * element; the `font-variant` shorthand resets all of them. `font-variant-ligatures`
 * is the row deliberately *not* typed here: its five tags (`liga`, `clig`, `rlig`,
 * `dlig`, `hlig`) and its vocabulary are RoboText's (12.12,
 * `RoboTextLigature.h`), and a second copy in RoboCSS is exactly how the shape the
 * CSS author writes and the policy the shaper reads would come to disagree. The row
 * is registered as a carried declaration and this header names the boundary.
 *
 * Twelve closed sets live here, each a table of rows every accessor reads:
 *
 *   typed — the property's value *is* the closed set:
 *     font-style              normal | italic | oblique
 *     font-kerning            auto | normal | none
 *     font-optical-sizing     auto | none
 *     font-synthesis          auto | none      (four longhands, one set)
 *     font-variant-caps       normal | small-caps | all-small-caps | petite-caps |
 *                             all-petite-caps | unicase | titling-caps
 *     font-variant-position   normal | sub | super
 *
 *   classified — the value's grammar is *wider* than the set, so the declaration
 *   is carried and the set types what is inside it (18.7's `transform` rule):
 *     font-stretch            nine keywords, plus a percentage this build carries
 *     font-size               ten absolute/relative size keywords, over the
 *                             shared length grammar
 *     font-variant-numeric    nine values, on five independent axes
 *     font-variant-east-asian ten values, on three
 *
 *   descriptors — `@font-face`'s own vocabulary, which is not a property family:
 *     font-display            auto | block | swap | fallback | optional
 *     <descriptor-name>       the names a `@font-face` block may contain
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a font database: nothing parses a font file,
 * resolves a family name to a face, walks a coverage table or selects a fallback —
 * that is RoboFont and, for layout, 12.17's selection seam. Not a shaper: no glyph
 * id, no `GSUB`/`GPOS` lookup and no text run appears below, and the ligature
 * vocabulary is RoboText's. Not a synthesiser: `font-synthesis-*` says what an
 * author *permits*, and this build synthesises neither a weight nor a slope (the
 * rule 12.3 records). A value here says what an author asked for; the face that
 * answers belongs to the phase that has one.
 */

#ifndef ROBO_CSS_FONT_H
#define ROBO_CSS_FONT_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* font-style                                                          */
/* ------------------------------------------------------------------ */

/**
 * A computed `font-style`.
 *
 * NORMAL is 0 because it is the property's initial value, so a cleared record
 * holds the state an author who declared nothing finds their text in.
 *
 * `oblique` is a *row* and not a family of angles: CSS's `oblique <angle>{0,2}`
 * form is a second grammar this build does not read, and the shape that read it
 * before this phase was the bare KEYWORD one, which refused it too — so the form
 * was already unavailable and this vocabulary only stops `font-style: banana` from
 * computing to a keyword carrying the letters.
 */
typedef enum CssFontStyle {
    /** `normal`: upright text. */
    RoboCssFontStyleNormal = 0,
    /** `italic`: the face's own italic, which is a *design* rather than a slant. */
    RoboCssFontStyleItalic = 1,
    /** `oblique`: the face slanted, which is not the same as its italic. */
    RoboCssFontStyleOblique = 2
} CssFontStyle;

/** Number of defined `font-style` values. */
#define RoboCssFontStyleCount 3u

int CssFontStyleIsKnown(CssFontStyle style);
const char *CssFontStyleName(CssFontStyle style);
size_t CssFontStyleCount(void);
CssFontStyle CssFontStyleFromSlice(CssSlice keyword);
CssFontStyle CssFontStyleFromText(const char *keyword);
int CssFontStyleSliceIsKnown(CssSlice keyword);

/**
 * Whether @p style asks for text that leans.
 *
 * `italic` and `oblique` both do, and they are two rows rather than one value
 * spelled twice: an italic is a separate design in the file while an oblique is the
 * upright design turned, so which face answers a `700 italic` request is a question
 * about the file. The classification is what a consumer reads when it has neither.
 * An unknown value answers 0.
 */
int CssFontStyleIsSlanted(CssFontStyle style);

/**
 * Whether @p style is the *slanted-upright* row specifically (`oblique`).
 *
 * A different question from @ref CssFontStyleIsSlanted, and 12.3's rule reads it:
 * no emboldening and no slant is ever synthesised, so a face with no oblique that
 * answers `oblique` is a face asked for something it does not have rather than one
 * artificially turned. An unknown value answers 0.
 */
int CssFontStyleIsOblique(CssFontStyle style);

Result CssFontStyleWrite(CssFontStyle style,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssFontStyleWrite needs ("oblique" plus a NUL). */
#define RoboCssFontStyleTextMax 8u

/* ------------------------------------------------------------------ */
/* font-kerning                                                        */
/* ------------------------------------------------------------------ */

/**
 * A computed `font-kerning`.
 *
 * AUTO is 0 — the initial value — and it is the row worth reading: `auto` means
 * *whatever the font says*, which is neither "kern" nor "do not kern". Only
 * `normal` is a request that kerning happen even where the font disables it by
 * default, and only `none` is a refusal.
 */
typedef enum CssFontKerning {
    /** `auto`: the font's own kerning behaviour decides. */
    RoboCssFontKerningAuto = 0,
    /** `normal`: kerning is applied where the font defines it. */
    RoboCssFontKerningNormal = 1,
    /** `none`: no kerning. */
    RoboCssFontKerningNone = 2
} CssFontKerning;

/** Number of defined `font-kerning` values. */
#define RoboCssFontKerningCount 3u

int CssFontKerningIsKnown(CssFontKerning kerning);
const char *CssFontKerningName(CssFontKerning kerning);
size_t CssFontKerningCount(void);
CssFontKerning CssFontKerningFromSlice(CssSlice keyword);
CssFontKerning CssFontKerningFromText(const char *keyword);
int CssFontKerningSliceIsKnown(CssSlice keyword);

/**
 * Whether @p kerning lets kerning be applied at all.
 *
 * `auto` and `normal` both do — the difference between them is *which* kerning the
 * font's own default disables, which is a question about the file and not about the
 * value — while `none` does not. An unknown value answers 0: a value this build
 * cannot read must not claim the author asked for kerning.
 */
int CssFontKerningEnablesKerning(CssFontKerning kerning);

Result CssFontKerningWrite(CssFontKerning kerning,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssFontKerningWrite needs ("normal" plus a NUL). */
#define RoboCssFontKerningTextMax 8u

/* ------------------------------------------------------------------ */
/* font-optical-sizing                                                 */
/* ------------------------------------------------------------------ */

/**
 * A computed `font-optical-sizing`.
 *
 * Two rows, and closed for the reason the background-attachment set is: `auto` is
 * the initial and means *the design size the face was cut for decides*, while
 * `none` refuses it. A third spelling would be a value with no meaning.
 */
typedef enum CssFontOpticalSizing {
    /** `auto`: optical sizing is applied where the face has it. */
    RoboCssFontOpticalSizingAuto = 0,
    /** `none`: the face is used at one design. */
    RoboCssFontOpticalSizingNone = 1
} CssFontOpticalSizing;

/** Number of defined `font-optical-sizing` values. */
#define RoboCssFontOpticalSizingCount 2u

int CssFontOpticalSizingIsKnown(CssFontOpticalSizing sizing);
const char *CssFontOpticalSizingName(CssFontOpticalSizing sizing);
size_t CssFontOpticalSizingCount(void);
CssFontOpticalSizing CssFontOpticalSizingFromSlice(CssSlice keyword);
CssFontOpticalSizing CssFontOpticalSizingFromText(const char *keyword);
int CssFontOpticalSizingSliceIsKnown(CssSlice keyword);

/** Whether @p sizing applies optical sizing (`auto`). An unknown value answers 0. */
int CssFontOpticalSizingIsAutomatic(CssFontOpticalSizing sizing);

Result CssFontOpticalSizingWrite(CssFontOpticalSizing sizing,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 CssDiag *diag);

/** Capacity @ref CssFontOpticalSizingWrite needs ("auto" plus a NUL). */
#define RoboCssFontOpticalSizingTextMax 8u

/* ------------------------------------------------------------------ */
/* font-synthesis                                                      */
/* ------------------------------------------------------------------ */

/**
 * A computed `font-synthesis-weight`, `…-style`, `…-small-caps` or `…-position`.
 *
 * One vocabulary and four properties, which is 18.3's alignment decision made
 * again: CSS gives the four the *same* two values, so a second table would be the
 * same rows under a second name. What each property means by them differs — and
 * that is the property's business, not the table's.
 *
 * The value is a *permission* and never an instruction. `auto` says "you may
 * synthesise this if the face lacks it", and this build never synthesises a weight
 * or a slope at all (the rule 12.3 records and RoboText keeps), so `auto` is what
 * an author wrote rather than something that happens.
 */
typedef enum CssFontSynthesis {
    /** `auto`: synthesis of this aspect is permitted. */
    RoboCssFontSynthesisAuto = 0,
    /** `none`: it is forbidden. */
    RoboCssFontSynthesisNone = 1
} CssFontSynthesis;

/** Number of defined `font-synthesis-*` values. */
#define RoboCssFontSynthesisCount 2u

int CssFontSynthesisIsKnown(CssFontSynthesis synthesis);
const char *CssFontSynthesisName(CssFontSynthesis synthesis);
size_t CssFontSynthesisCount(void);
CssFontSynthesis CssFontSynthesisFromSlice(CssSlice keyword);
CssFontSynthesis CssFontSynthesisFromText(const char *keyword);
int CssFontSynthesisSliceIsKnown(CssSlice keyword);

/**
 * Whether @p synthesis permits the aspect to be synthesised.
 *
 * Only `auto` does. The question is deliberately "may it" rather than "is it":
 * nothing in this build synthesises, so the answer is what an author asked for and
 * a consumer that had to decide would read both this and the face. An unknown value
 * answers 0.
 */
int CssFontSynthesisAllows(CssFontSynthesis synthesis);

Result CssFontSynthesisWrite(CssFontSynthesis synthesis,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag);

/** Capacity @ref CssFontSynthesisWrite needs ("auto" plus a NUL). */
#define RoboCssFontSynthesisTextMax 8u

/* ------------------------------------------------------------------ */
/* font-variant-caps                                                   */
/* ------------------------------------------------------------------ */

/**
 * A computed `font-variant-caps`.
 *
 * Seven rows on one axis, and the axis has a shape worth reading: the `all-`
 * prefixed pair applies the variant to **lowercase letters too**, so
 * `small-caps` and `all-small-caps` are different requests about the same text and
 * not two spellings of one. `petite-caps` is the same idea at a smaller size, and
 * `unicase` and `titling-caps` are neither.
 */
typedef enum CssFontVariantCaps {
    /** `normal`: no caps variant. */
    RoboCssFontVariantCapsNormal = 0,
    /** `small-caps`: small capitals for lowercase letters. */
    RoboCssFontVariantCapsSmallCaps = 1,
    /** `all-small-caps`: small capitals for upper- and lowercase alike. */
    RoboCssFontVariantCapsAllSmallCaps = 2,
    /** `petite-caps`: the smaller cut, lowercase only. */
    RoboCssFontVariantCapsPetiteCaps = 3,
    /** `all-petite-caps`: the smaller cut, both cases. */
    RoboCssFontVariantCapsAllPetiteCaps = 4,
    /** `unicase`: capitals and lowercase drawn at one height. */
    RoboCssFontVariantCapsUnicase = 5,
    /** `titling-caps`: capitals designed for title case. */
    RoboCssFontVariantCapsTitlingCaps = 6
} CssFontVariantCaps;

/** Number of defined `font-variant-caps` values. */
#define RoboCssFontVariantCapsCount 7u

int CssFontVariantCapsIsKnown(CssFontVariantCaps caps);
const char *CssFontVariantCapsName(CssFontVariantCaps caps);
size_t CssFontVariantCapsCount(void);
CssFontVariantCaps CssFontVariantCapsFromSlice(CssSlice keyword);
CssFontVariantCaps CssFontVariantCapsFromText(const char *keyword);
int CssFontVariantCapsSliceIsKnown(CssSlice keyword);

/**
 * Whether @p caps asks for a caps variant at all.
 *
 * The raw predicate — everything but `normal` — and deliberately not the whole
 * answer a consumer wants. Reading only this would treat `small-caps` and
 * `all-small-caps` as one request, which is the mistake the two rows below exist to
 * make visible. An unknown value answers 0.
 */
int CssFontVariantCapsIsCapsVariant(CssFontVariantCaps caps);

/**
 * Whether @p caps applies to **lowercase letters too** (the two `all-` rows).
 *
 * `small-caps` leaves an `H` alone and shrinks an `h`; `all-small-caps` draws both
 * as small capitals. So this is the classification that tells the two apart, and it
 * is asked of the row rather than derived from an `all-` prefix — a prefix is a
 * spelling, and a build that matched on one would answer correctly in English and
 * wrongly in any language whose word for "all" collides. An unknown value answers 0.
 */
int CssFontVariantCapsAffectsLowercase(CssFontVariantCaps caps);

/** Whether @p caps is one of the two *petite* rows. An unknown value answers 0. */
int CssFontVariantCapsIsPetite(CssFontVariantCaps caps);

Result CssFontVariantCapsWrite(CssFontVariantCaps caps,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag);

/** Capacity @ref CssFontVariantCapsWrite needs ("all-petite-caps" plus a NUL). */
#define RoboCssFontVariantCapsTextMax 16u

/* ------------------------------------------------------------------ */
/* font-variant-numeric                                                */
/* ------------------------------------------------------------------ */

/**
 * Which of the five independent axes a `font-variant-numeric` value is on.
 *
 * The property's grammar is a **space-separated set** — `font-variant-numeric:
 * tabular-nums diagonal-fractions` is legal — and the members fall into groups that
 * are alternatives *within* a group and compose *across* groups: a figure style is
 * one of two, a figure spacing one of two, a fraction form one of two. The kind is
 * that grouping, and it is the reason this shape has one: a consumer has to know
 * that `lining-nums oldstyle-nums` is contradictory while `lining-nums
 * tabular-nums` is not, and only the row can say which is which.
 */
typedef enum CssFontVariantNumericKind {
    /** `normal`: the axis is untouched. */
    RoboCssFontVariantNumericKindNormal = 0,
    /** `ordinal`: the ordinal axis, on its own. */
    RoboCssFontVariantNumericKindOrdinal = 1,
    /** `slashed-zero`: the zero axis, on its own. */
    RoboCssFontVariantNumericKindSlashedZero = 2,
    /** `lining-nums` / `oldstyle-nums`: the *figure style* axis. */
    RoboCssFontVariantNumericKindFigureStyle = 3,
    /** `proportional-nums` / `tabular-nums`: the *figure spacing* axis. */
    RoboCssFontVariantNumericKindFigureSpacing = 4,
    /** `diagonal-fractions` / `stacked-fractions`: the *fraction* axis. */
    RoboCssFontVariantNumericKindFraction = 5
} CssFontVariantNumericKind;

/** Number of defined `font-variant-numeric` kinds. */
#define RoboCssFontVariantNumericKindCount 6u

/**
 * One value of `font-variant-numeric`.
 *
 * NORMAL is 0 because it is the property's initial value. The rest are listed in
 * the order CSS writes the grammar's groups, so the enumerator's order is the axes'
 * order and not an alphabet.
 */
typedef enum CssFontVariantNumeric {
    /** `normal`: no numeric variant. */
    RoboCssFontVariantNumericNormal = 0,
    /** `ordinal`: ordinals get their own forms. */
    RoboCssFontVariantNumericOrdinal = 1,
    /** `slashed-zero`: a zero with a slash. */
    RoboCssFontVariantNumericSlashedZero = 2,
    /** `lining-nums`: figures sit on the baseline at cap height. */
    RoboCssFontVariantNumericLiningNums = 3,
    /** `oldstyle-nums`: figures with ascenders and descenders. */
    RoboCssFontVariantNumericOldstyleNums = 4,
    /** `proportional-nums`: figures of their own widths. */
    RoboCssFontVariantNumericProportionalNums = 5,
    /** `tabular-nums`: figures of one width, for a column. */
    RoboCssFontVariantNumericTabularNums = 6,
    /** `diagonal-fractions`: a slash between the halves. */
    RoboCssFontVariantNumericDiagonalFractions = 7,
    /** `stacked-fractions`: one above the other. */
    RoboCssFontVariantNumericStackedFractions = 8
} CssFontVariantNumeric;

/** Number of defined `font-variant-numeric` values. */
#define RoboCssFontVariantNumericCount 9u

int CssFontVariantNumericIsKnown(CssFontVariantNumeric numeric);
const char *CssFontVariantNumericName(CssFontVariantNumeric numeric);
size_t CssFontVariantNumericCount(void);
CssFontVariantNumeric CssFontVariantNumericFromSlice(CssSlice keyword);
CssFontVariantNumeric CssFontVariantNumericFromText(const char *keyword);
int CssFontVariantNumericSliceIsKnown(CssSlice keyword);

/**
 * The axis @p numeric is on.
 *
 * An unknown value answers the *normal* kind rather than a kind of its own: a value
 * this build cannot read must not claim to be a member of an axis it might
 * contradict.
 */
CssFontVariantNumericKind CssFontVariantNumericKindOf(
    CssFontVariantNumeric numeric);

/** Whether @p kind is a defined kind. */
int CssFontVariantNumericKindIsKnown(CssFontVariantNumericKind kind);

/** Human-readable kind name; never NULL. */
const char *CssFontVariantNumericKindName(CssFontVariantNumericKind kind);

Result CssFontVariantNumericWrite(CssFontVariantNumeric numeric,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag);

/** Capacity @ref CssFontVariantNumericWrite needs ("diagonal-fractions" + NUL). */
#define RoboCssFontVariantNumericTextMax 24u

/* ------------------------------------------------------------------ */
/* font-variant-east-asian                                             */
/* ------------------------------------------------------------------ */

/** The three axes a `font-variant-east-asian` value is on. */
typedef enum CssFontVariantEastAsianKind {
    /** `normal`. */
    RoboCssFontVariantEastAsianKindNormal = 0,
    /** The six *glyph form* values (`jis78` … `traditional`). */
    RoboCssFontVariantEastAsianKindVariant = 1,
    /** `full-width` / `proportional-width`: the *width* axis. */
    RoboCssFontVariantEastAsianKindWidth = 2,
    /** `ruby`: the ruby axis, on its own. */
    RoboCssFontVariantEastAsianKindRuby = 3
} CssFontVariantEastAsianKind;

/** Number of defined `font-variant-east-asian` kinds. */
#define RoboCssFontVariantEastAsianKindCount 4u

/**
 * One value of `font-variant-east-asian`.
 *
 * The six glyph-form values are alternatives to each other — a font cannot be both
 * JIS78 and traditional — while the width and ruby values compose with them, which
 * is the same "alternatives within an axis" shape `font-variant-numeric` has.
 */
typedef enum CssFontVariantEastAsian {
    /** `normal`: no east-asian variant. */
    RoboCssFontVariantEastAsianNormal = 0,
    /** `jis78`: the 1978 JIS forms. */
    RoboCssFontVariantEastAsianJis78 = 1,
    /** `jis83`: the 1983 forms. */
    RoboCssFontVariantEastAsianJis83 = 2,
    /** `jis90`: the 1990 forms. */
    RoboCssFontVariantEastAsianJis90 = 3,
    /** `jis04`: the 2004 forms. */
    RoboCssFontVariantEastAsianJis04 = 4,
    /** `simplified`: simplified Chinese forms. */
    RoboCssFontVariantEastAsianSimplified = 5,
    /** `traditional`: traditional Chinese forms. */
    RoboCssFontVariantEastAsianTraditional = 6,
    /** `full-width`: full-width forms. */
    RoboCssFontVariantEastAsianFullWidth = 7,
    /** `proportional-width`: proportional forms. */
    RoboCssFontVariantEastAsianProportionalWidth = 8,
    /** `ruby`: ruby annotations take their own forms. */
    RoboCssFontVariantEastAsianRuby = 9
} CssFontVariantEastAsian;

/** Number of defined `font-variant-east-asian` values. */
#define RoboCssFontVariantEastAsianCount 10u

int CssFontVariantEastAsianIsKnown(CssFontVariantEastAsian eastAsian);
const char *CssFontVariantEastAsianName(CssFontVariantEastAsian eastAsian);
size_t CssFontVariantEastAsianCount(void);
CssFontVariantEastAsian CssFontVariantEastAsianFromSlice(CssSlice keyword);
CssFontVariantEastAsian CssFontVariantEastAsianFromText(const char *keyword);
int CssFontVariantEastAsianSliceIsKnown(CssSlice keyword);

/** The axis @p eastAsian is on. An unknown value answers the *normal* kind. */
CssFontVariantEastAsianKind CssFontVariantEastAsianKindOf(
    CssFontVariantEastAsian eastAsian);

/** Whether @p kind is a defined kind. */
int CssFontVariantEastAsianKindIsKnown(CssFontVariantEastAsianKind kind);

/** Human-readable kind name; never NULL. */
const char *CssFontVariantEastAsianKindName(CssFontVariantEastAsianKind kind);

Result CssFontVariantEastAsianWrite(CssFontVariantEastAsian eastAsian,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag);

/** Capacity @ref CssFontVariantEastAsianWrite needs ("proportional-width" + NUL). */
#define RoboCssFontVariantEastAsianTextMax 24u

/* ------------------------------------------------------------------ */
/* font-variant-position                                               */
/* ------------------------------------------------------------------ */

/**
 * A computed `font-variant-position`.
 *
 * Three rows, closed, and the classification that matters is the *direction* of the
 * shift: `sub` and `super` are the only two, and they move the text opposite ways.
 * The row is not a replacement for `vertical-align` — `font-variant-position` moves
 * only the glyphs a font supplies for it, and `vertical-align` moves the box.
 */
typedef enum CssFontVariantPosition {
    /** `normal`: the text sits on the baseline. */
    RoboCssFontVariantPositionNormal = 0,
    /** `sub`: subscript forms, below the baseline. */
    RoboCssFontVariantPositionSub = 1,
    /** `super`: superscript forms, above it. */
    RoboCssFontVariantPositionSuper = 2
} CssFontVariantPosition;

/** Number of defined `font-variant-position` values. */
#define RoboCssFontVariantPositionCount 3u

int CssFontVariantPositionIsKnown(CssFontVariantPosition position);
const char *CssFontVariantPositionName(CssFontVariantPosition position);
size_t CssFontVariantPositionCount(void);
CssFontVariantPosition CssFontVariantPositionFromSlice(CssSlice keyword);
CssFontVariantPosition CssFontVariantPositionFromText(const char *keyword);
int CssFontVariantPositionSliceIsKnown(CssSlice keyword);

/** Whether @p position shifts the text off the baseline at all. Unknown answers 0. */
int CssFontVariantPositionIsShifted(CssFontVariantPosition position);

/**
 * Whether @p position shifts the text **up** (`super`).
 *
 * Asked separately from @ref CssFontVariantPositionIsShifted because the shift has
 * a sign: a build that had only the first would place a subscript where a
 * superscript goes, and both would answer "shifted". An unknown value answers 0.
 */
int CssFontVariantPositionIsSuperscript(CssFontVariantPosition position);

Result CssFontVariantPositionWrite(CssFontVariantPosition position,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag);

/** Capacity @ref CssFontVariantPositionWrite needs ("super" plus a NUL). */
#define RoboCssFontVariantPositionTextMax 8u

/* ------------------------------------------------------------------ */
/* font-stretch                                                        */
/* ------------------------------------------------------------------ */

/**
 * A `font-stretch` keyword.
 *
 * The property's grammar is *wider* than this set — CSS Fonts 4 also admits a
 * `<percentage>`, and `font-stretch: 87.5%` is a value between two keywords — so
 * the declaration is registered as a **carried** one and this table types what is
 * inside it, exactly as 18.7 typed the names inside a `transform` list.
 *
 * The enumerator's order is the axis' order, from the most condensed to the most
 * expanded, and `normal` is therefore the **fourth** row rather than the zero —
 * which is why the shape carries the keyword rather than the enumerator, the same
 * answer 18.6 gave `column-fill: balance`.
 */
typedef enum CssFontStretch {
    /** `ultra-condensed`: 50%. */
    RoboCssFontStretchUltraCondensed = 0,
    /** `extra-condensed`: 62.5%. */
    RoboCssFontStretchExtraCondensed = 1,
    /** `condensed`: 75%. */
    RoboCssFontStretchCondensed = 2,
    /** `semi-condensed`: 87.5%. */
    RoboCssFontStretchSemiCondensed = 3,
    /** `normal`: 100%, and the property's initial value. */
    RoboCssFontStretchNormal = 4,
    /** `semi-expanded`: 112.5%. */
    RoboCssFontStretchSemiExpanded = 5,
    /** `expanded`: 125%. */
    RoboCssFontStretchExpanded = 6,
    /** `extra-expanded`: 150%. */
    RoboCssFontStretchExtraExpanded = 7,
    /** `ultra-expanded`: 200%. */
    RoboCssFontStretchUltraExpanded = 8
} CssFontStretch;

/** Number of defined `font-stretch` keywords. */
#define RoboCssFontStretchCount 9u

int CssFontStretchIsKnown(CssFontStretch stretch);
const char *CssFontStretchName(CssFontStretch stretch);
size_t CssFontStretchCount(void);
CssFontStretch CssFontStretchFromSlice(CssSlice keyword);
CssFontStretch CssFontStretchFromText(const char *keyword);
int CssFontStretchSliceIsKnown(CssSlice keyword);

/**
 * The percentage @p stretch stands for.
 *
 * The spec's own nine numbers (50, 62.5, 75, 87.5, 100, 112.5, 125, 150, 200), and
 * they are *recommendations* rather than truths: CSS leaves the ratio a face is
 * actually drawn at to the face. So this is the value's meaning and not a width —
 * nothing here scales anything. An unknown value answers 100, which is the initial
 * and the answer that changes no width.
 */
double CssFontStretchPercentage(CssFontStretch stretch);

/**
 * The keyword nearest to @p percentage, and never a refusal.
 *
 * `87.5%` is `semi-condensed`; a percentage between two keywords takes the nearer,
 * and a tie takes the narrower — the rule is stated once here rather than left to
 * each consumer. A negative percentage is clamped to the first row, and a
 * percentage beyond the last takes it. The property's *declaration* is carried, so
 * this is a classification of what an author wrote rather than a re-spelling of it.
 */
CssFontStretch CssFontStretchFromPercentage(double percentage);

/** Whether @p stretch is narrower than `normal` (50–87.5%). Unknown answers 0. */
int CssFontStretchIsCondensed(CssFontStretch stretch);

/** Whether @p stretch is wider than `normal` (112.5–200%). Unknown answers 0. */
int CssFontStretchIsExpanded(CssFontStretch stretch);

/** Whether @p stretch is the `normal` row itself. An unknown value answers 0. */
int CssFontStretchIsNormal(CssFontStretch stretch);

Result CssFontStretchWrite(CssFontStretch stretch,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssFontStretchWrite needs ("ultra-condensed" plus a NUL). */
#define RoboCssFontStretchTextMax 16u

/* ------------------------------------------------------------------ */
/* font-size keywords                                                  */
/* ------------------------------------------------------------------ */

/**
 * A `font-size` keyword.
 *
 * `font-size`'s computed value goes through the shared length grammar, so the
 * declaration and its value need nothing new here — the LENGTH shape already
 * carries these ten keywords, exactly as it carries `medium` for a border width.
 * What the property's grammar has and the length family does not is the *meaning*
 * of those keywords, and it is the property's most important one: a size keyword is
 * a **relative** statement.
 *
 * `medium` is not sixteen pixels. The absolute keywords are defined against a user
 * agent's preferred size, so `medium` is whatever the reader's setting says and
 * `x-large` is 1.5 of *that* — which is why this table carries a ratio per row and
 * why a build that resolved `medium` to a pixel count at parse time would be wrong
 * on every machine whose reader disagrees with the guess. `larger`/`smaller` are
 * relative to the *parent's* size instead of the reader's.
 */
typedef enum CssFontSizeKeyword {
    /** `xx-small`: 3/5 of the reader's preferred size. */
    RoboCssFontSizeKeywordXxSmall = 0,
    /** `x-small`: 3/4. */
    RoboCssFontSizeKeywordXSmall = 1,
    /** `small`: 8/9. */
    RoboCssFontSizeKeywordSmall = 2,
    /** `medium`: the reader's preferred size itself (ratio 1), and the initial value. */
    RoboCssFontSizeKeywordMedium = 3,
    /** `large`: 6/5. */
    RoboCssFontSizeKeywordLarge = 4,
    /** `x-large`: 3/2. */
    RoboCssFontSizeKeywordXLarge = 5,
    /** `xx-large`: 2. */
    RoboCssFontSizeKeywordXxLarge = 6,
    /** `xxx-large`: 3. */
    RoboCssFontSizeKeywordXxxLarge = 7,
    /** `larger`: 6/5 of the *parent's* size. */
    RoboCssFontSizeKeywordLarger = 8,
    /** `smaller`: 5/6 of the parent's size. */
    RoboCssFontSizeKeywordSmaller = 9
} CssFontSizeKeyword;

/** Number of defined `font-size` keywords. */
#define RoboCssFontSizeKeywordCount 10u

int CssFontSizeKeywordIsKnown(CssFontSizeKeyword size);
const char *CssFontSizeKeywordName(CssFontSizeKeyword size);
size_t CssFontSizeKeywordCount(void);
CssFontSizeKeyword CssFontSizeKeywordFromSlice(CssSlice keyword);
CssFontSizeKeyword CssFontSizeKeywordFromText(const char *keyword);
int CssFontSizeKeywordSliceIsKnown(CssSlice keyword);

/**
 * Whether @p size is one of the eight **absolute** keywords.
 *
 * Absolute here means "against the reader's preferred size" and *not* "a known
 * number of pixels" — the distinction the whole table exists for. The two relative
 * keywords answer 0.
 */
int CssFontSizeKeywordIsAbsolute(CssFontSizeKeyword size);

/**
 * Whether @p size is `larger` or `smaller`, the two keywords measured against the
 * **parent's** size rather than the reader's preference.
 */
int CssFontSizeKeywordIsRelative(CssFontSizeKeyword size);

/**
 * The ratio @p size stands for: what to multiply the base size by.
 *
 * The spec's own suggested factors, and they are *suggestions*: CSS leaves the
 * actual ratio to the user agent, so this is the value's meaning rather than a
 * promise about the pixels. An unknown value answers 1.0, which is the answer that
 * changes nothing.
 */
double CssFontSizeKeywordRatio(CssFontSizeKeyword size);

Result CssFontSizeKeywordWrite(CssFontSizeKeyword size,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               CssDiag *diag);

/** Capacity @ref CssFontSizeKeywordWrite needs ("xxx-large" plus a NUL). */
#define RoboCssFontSizeKeywordTextMax 12u

/* ------------------------------------------------------------------ */
/* @font-face descriptors                                              */
/* ------------------------------------------------------------------ */

/**
 * The name a `@font-face` descriptor is written as.
 *
 * This is deliberately **not** a list of properties, and the list is the point:
 * five of these names are also properties and mean something else inside a
 * `@font-face` block. `font-weight: 700` on an element *selects* a face; in a
 * `@font-face` block it *declares the weight of the face this rule defines*, so a
 * build that read one as the other would answer the request with itself. And six
 * names exist only here — `src` and `unicode-range` above all — which is why the
 * vocabulary is a table of *descriptors* rather than a second property registry.
 *
 * `CssFontFaceDescriptorIsPropertyShared` is that first fact as a function, so no
 * consumer has to know the list by heart.
 */
typedef enum CssFontFaceDescriptor {
    /** `src`: where the face comes from. Descriptor only. */
    RoboCssFontFaceDescriptorSrc = 0,
    /** `font-family`: the family this face is added to. Also a property. */
    RoboCssFontFaceDescriptorFontFamily = 1,
    /** `font-style`: the face's own style. Also a property. */
    RoboCssFontFaceDescriptorFontStyle = 2,
    /** `font-weight`: the face's own weight. Also a property. */
    RoboCssFontFaceDescriptorFontWeight = 3,
    /** `font-stretch`: the face's own width. Also a property. */
    RoboCssFontFaceDescriptorFontStretch = 4,
    /** `font-display`: when the face may be used. **Descriptor only.** */
    RoboCssFontFaceDescriptorFontDisplay = 5,
    /** `unicode-range`: the scalars this face covers. Descriptor only. */
    RoboCssFontFaceDescriptorUnicodeRange = 6,
    /** `font-feature-settings`: the face's default features. Also a property. */
    RoboCssFontFaceDescriptorFontFeatureSettings = 7,
    /** `font-variation-settings`: the face's default axes. Also a property. */
    RoboCssFontFaceDescriptorFontVariationSettings = 8,
    /** `font-named-instance`: which variation instance. **Descriptor only.** */
    RoboCssFontFaceDescriptorFontNamedInstance = 9,
    /** `ascent-override`: the ascent to use. Descriptor only. */
    RoboCssFontFaceDescriptorAscentOverride = 10,
    /** `descent-override`: the descent to use. Descriptor only. */
    RoboCssFontFaceDescriptorDescentOverride = 11,
    /** `line-gap-override`: the line gap to use. Descriptor only. */
    RoboCssFontFaceDescriptorLineGapOverride = 12,
    /** `size-adjust`: a scale for the face's glyphs and metrics. Descriptor only. */
    RoboCssFontFaceDescriptorSizeAdjust = 13,
    /** `font-language-override`: the language tag to shape with. Also a property. */
    RoboCssFontFaceDescriptorFontLanguageOverride = 14,
    /** `font-kerning`: the face's kerning default. Also a property. */
    RoboCssFontFaceDescriptorFontKerning = 15,
    /** `font-optical-sizing`: the face's optical sizing. Also a property. */
    RoboCssFontFaceDescriptorFontOpticalSizing = 16,
    /** `font-synthesis-weight`: synthesis permitted for this face. Also a property. */
    RoboCssFontFaceDescriptorFontSynthesisWeight = 17,
    /** `font-synthesis-style`: as above, for the slope. Also a property. */
    RoboCssFontFaceDescriptorFontSynthesisStyle = 18,
    /** `font-synthesis-small-caps`: as above, for small caps. Also a property. */
    RoboCssFontFaceDescriptorFontSynthesisSmallCaps = 19,
    /** `font-synthesis-position`: as above, for position. Also a property. */
    RoboCssFontFaceDescriptorFontSynthesisPosition = 20,
    /** `font-variant-caps`: the face's caps default. Also a property. */
    RoboCssFontFaceDescriptorFontVariantCaps = 21,
    /** `font-variant-east-asian`: the face's east-asian default. Also a property. */
    RoboCssFontFaceDescriptorFontVariantEastAsian = 22,
    /** `font-variant-ligatures`: the face's ligature default. Also a property. */
    RoboCssFontFaceDescriptorFontVariantLigatures = 23,
    /** `font-variant-numeric`: the face's numeric default. Also a property. */
    RoboCssFontFaceDescriptorFontVariantNumeric = 24,
    /** `font-variant-position`: the face's position default. Also a property. */
    RoboCssFontFaceDescriptorFontVariantPosition = 25,
    /** `font-palette`: the face's palette default. Also a property. */
    RoboCssFontFaceDescriptorFontPalette = 26
} CssFontFaceDescriptor;

/** Number of defined `@font-face` descriptor names. */
#define RoboCssFontFaceDescriptorCount 27u

int CssFontFaceDescriptorIsKnown(CssFontFaceDescriptor descriptor);
const char *CssFontFaceDescriptorName(CssFontFaceDescriptor descriptor);
size_t CssFontFaceDescriptorCount(void);
CssFontFaceDescriptor CssFontFaceDescriptorFromSlice(CssSlice keyword);
CssFontFaceDescriptor CssFontFaceDescriptorFromText(const char *keyword);
int CssFontFaceDescriptorSliceIsKnown(CssSlice keyword);

/**
 * Whether @p descriptor is **also** a property name.
 *
 * The one classification here, and the one a consumer needs: a name that is true
 * means the block's spelling and an element's spelling are two different statements
 * wearing one word. A name that is false exists only inside `@font-face`, so a
 * document that wrote it as a declaration has written something that is not a
 * property at all. An unknown value answers 0.
 */
int CssFontFaceDescriptorIsPropertyShared(CssFontFaceDescriptor descriptor);

Result CssFontFaceDescriptorWrite(CssFontFaceDescriptor descriptor,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag);

/** Capacity @ref CssFontFaceDescriptorWrite needs ("font-variation-settings" + NUL). */
#define RoboCssFontFaceDescriptorTextMax 32u

/* ------------------------------------------------------------------ */
/* font-display                                                        */
/* ------------------------------------------------------------------ */

/**
 * A `font-display` descriptor value.
 *
 * The one closed set in this module that is a *descriptor* and not a property:
 * `font-display` has no property form at all, so nothing in the cascade can set it
 * and no element can inherit it. It describes how a face may be used while it is
 * still being fetched, so what it carries is *time*: whether a block period
 * applies, how long it is, and whether the fallback is swapped out afterwards.
 *
 * The three facts are what a text renderer needs to decide what to draw on frame
 * one, and they are the reason this is a table rather than five keywords.
 */
typedef enum CssFontDisplay {
    /** `auto`: the user agent's own policy. */
    RoboCssFontDisplayAuto = 0,
    /** `block`: block, then swap. The longest block period. */
    RoboCssFontDisplayBlock = 1,
    /** `swap`: no block period at all. */
    RoboCssFontDisplaySwap = 2,
    /** `fallback`: a very short block, then a short swap window. */
    RoboCssFontDisplayFallback = 3,
    /** `optional`: a very short block, and never a swap. */
    RoboCssFontDisplayOptional = 4
} CssFontDisplay;

/** Number of defined `font-display` values. */
#define RoboCssFontDisplayCount 5u

int CssFontDisplayIsKnown(CssFontDisplay display);
const char *CssFontDisplayName(CssFontDisplay display);
size_t CssFontDisplayCount(void);
CssFontDisplay CssFontDisplayFromSlice(CssSlice keyword);
CssFontDisplay CssFontDisplayFromText(const char *keyword);
int CssFontDisplaySliceIsKnown(CssSlice keyword);

/**
 * Whether @p display holds the text invisible while the face is fetched.
 *
 * `block` and the two shorter-period rows do; `swap` does not — it draws the
 * fallback immediately — and `auto` is the user agent's choice, which this build
 * answers *yes* for because the answer that risks invisible text is the one a
 * consumer must not get for free. An unknown value answers 1 for the same reason.
 */
int CssFontDisplayBlocksRendering(CssFontDisplay display);

/**
 * The length of the block period in milliseconds, or 0 when there is none.
 *
 * `block` is 3000, `fallback` and `optional` are 100, and `swap` is 0 — the
 * numbers CSS names. `auto` answers 0 as well, and that is a *missing answer*
 * rather than a short period: it is the user agent's policy and this build has
 * none, which is why @ref CssFontDisplayIsAutomatic exists.
 */
int CssFontDisplayBlockPeriodMillis(CssFontDisplay display);

/**
 * Whether the fallback is replaced once the face arrives.
 *
 * `swap` and `fallback` do; `block` does not (a face that never arrived failed),
 * and `optional` does not (a face that missed its window is not used at all).
 */
int CssFontDisplaySwapsAfterBlockPeriod(CssFontDisplay display);

/** Whether @p display is `auto`, whose policy is the user agent's. */
int CssFontDisplayIsAutomatic(CssFontDisplay display);

Result CssFontDisplayWrite(CssFontDisplay display,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssFontDisplayWrite needs ("fallback" plus a NUL). */
#define RoboCssFontDisplayTextMax 12u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_FONT_H */
