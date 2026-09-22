/*
 * RoboText types (Phase 12.4) — the units, ranges and vocabulary the rest of the text layer speaks.
 *
 * ## Why the units get names
 *
 * A text layer's first bug is always an index in the wrong space. `héllo` is 5 scalars and 6 bytes;
 * `∑` is 1 scalar and 3 bytes; a shape result's glyph 7 is neither. Three phases of this runtime have
 * handled that by *naming the unit in the type* — `LayoutTextRange` counts code points and says so —
 * and this header is where the text layer states the rule once for everybody above it:
 *
 *   RoboTextByteIndex      a byte offset into UTF-8. What a DOM node, a `string.h` call and a file
 *                          offset use.
 *   RoboTextScalarIndex    the ordinal of a Unicode **scalar value** in a buffer. What a run, a glyph's
 *                          source span and a selection range use.
 *   RoboTextGlyphIndex     the ordinal of a glyph inside a glyph run. What a painter uses, and what a
 *                          shaped run will use after substitution has changed the count.
 *   RoboTextClusterIndex   the ordinal of a user-perceived character. **Nothing produces one yet** —
 *                          grapheme segmentation is a later goal — and the type exists so that the
 *                          field which will hold one is not an `int` a reader has to guess about.
 *
 * They are distinct integer types rather than four `uint32_t`s, and the difference is not cosmetic:
 * C will let a caller pass a byte offset where a scalar index belongs, and the *only* defence a header
 * can offer is that the parameter's name says which it is and the conversion between them is a
 * function with a `Result` rather than an implicit narrowing. The conversions live in
 * `RoboTextBuffer.h`, because only the buffer knows where the boundaries are.
 *
 * ## Ranges are half-open, and the algebra is here rather than in three callers
 *
 * `RoboTextRange` is `[start, end)` in scalars, which is what makes `start == end` the empty range and
 * therefore a *position* — a caret, a glyph's source start — rather than a degenerate span. The set of
 * operations (`Contains`, `Intersects`, `Union`, `Slice`) is defined once here for the same reason
 * `LayoutTextRange` defines its own: a layer that re-derives containment at each use site eventually
 * disagrees with itself about whether the end is included.
 *
 * ## Script, direction and language are *classification*, not resolution
 *
 *   script      a `UnicodeScript`, straight from RoboUnicode. RoboText does not invent a script
 *               vocabulary; it reuses the UCD's, which is the same reason it does not carry a second
 *               Unicode database (Phase 4 owns that).
 *   direction   LTR/RTL/unknown, **classified per run**, not resolved. The Unicode bidirectional
 *               algorithm — which assigns levels, handles explicit embeddings and reorders — is
 *               Phase 12.8, and its flag in `RoboTextVersion.h` says so. What this phase produces is
 *               "these scalars are RTL-class, these are LTR-class", which is the input the algorithm
 *               will consume rather than a claim to have run it.
 *   language    a BCP 47 tag, carried and not interpreted. Shaping needs it (a `locl` feature is
 *               language-dependent); nothing in this phase reads it beyond echoing it into a run.
 *
 * ## The font context is *numbers plus a borrowed face*
 *
 * `RoboTextFontStyle` is the set of inputs a run is set at, and it is deliberately **not** the
 * resolved style of any one layer above: 12.3's `RoboTypographyStyle`, `RoboPaint`'s `LayoutTextStyle`
 * and CSS's computed style each carry a different subset, and a text layer that picked one of them as
 * its vocabulary would inherit that layer's coupling. So the fields are the *CSS font properties*,
 * spelled the way RoboFont spells them (`FontWeight`, `FontStyle`, `FontStretch`), plus the three
 * numbers CSS has no RoboFont equivalent for (size, line height, letter spacing) and the optical-size
 * class.
 *
 * The optical-size class is a five-value enum re-spelled here rather than shared with 12.3's
 * `RoboTypographyOpticalSize`, and that is a trade worth naming: the alternative is a link dependency
 * from the text layer to the typography layer for one enum, which would put a *cascade* module
 * underneath a module whose whole job is to be usable by the layers above it. The two vocabularies are
 * the same five classes in the same order and `RoboTextOpticalSizeIsKnown` is what a test pins.
 */
#ifndef ROBO_TEXT_TYPES_H
#define ROBO_TEXT_TYPES_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboUnicode/RoboUnicodeBidi.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Units                                                               */
/* ------------------------------------------------------------------ */

/** A byte offset into a buffer's UTF-8. */
typedef uint32_t RoboTextByteIndex;

/** The ordinal of a Unicode scalar value in a buffer. */
typedef uint32_t RoboTextScalarIndex;

/** The ordinal of a glyph inside a glyph run. */
typedef uint32_t RoboTextGlyphIndex;

/** The ordinal of a user-perceived character. Produced by nothing yet (see the header). */
typedef uint32_t RoboTextClusterIndex;

/** The largest byte index or scalar index this build names. */
#define RoboTextIndexMax ((uint32_t)0xFFFFFFFFu)

/* ------------------------------------------------------------------ */
/* Ranges                                                              */
/* ------------------------------------------------------------------ */

/**
 * A half-open span of Unicode **scalars**: @p start inclusive, @p end exclusive.
 *
 * An empty range (`start == end`) is legal and is a *position* rather than a span — a caret, a
 * glyph's source start, the place a run begins — which is why `RoboTextRangeIsEmpty` is not an error
 * test and `RoboTextRangeIsInverted` is.
 */
typedef struct RoboTextRange {
    RoboTextScalarIndex start;
    RoboTextScalarIndex end;
} RoboTextRange;

/** A half-open span of **bytes**. The other half of the buffer's two views, and never interchangeable
 *  with @ref RoboTextRange. */
typedef struct RoboTextByteRange {
    RoboTextByteIndex start;
    RoboTextByteIndex end;
} RoboTextByteRange;

/** The empty range at @p at: a position in the text rather than a span of it. */
RoboTextRange RoboTextRangeAt(RoboTextScalarIndex at);

/** The whole of a text @p scalars long. */
RoboTextRange RoboTextRangeFull(RoboTextScalarIndex scalars);

/** @p first to @p last, inclusive of @p first and exclusive of @p last. */
RoboTextRange RoboTextRangeMake(RoboTextScalarIndex first, RoboTextScalarIndex last);

/** Scalars the range covers; 0 for an inverted or empty range. */
RoboTextScalarIndex RoboTextRangeLength(RoboTextRange range);

/** Non-zero when the range covers nothing (`start >= end`). */
int RoboTextRangeIsEmpty(RoboTextRange range);

/** Non-zero when `start > end` — the one shape that is never a position. */
int RoboTextRangeIsInverted(RoboTextRange range);

/** Non-zero when @p range lies inside a text @p scalars long. */
int RoboTextRangeIsWithin(RoboTextRange range, RoboTextScalarIndex scalars);

/** Non-zero when @p index is inside @p range. The end is exclusive, so it is not. */
int RoboTextRangeContains(RoboTextRange range, RoboTextScalarIndex index);

/** Non-zero when @p range fully contains @p other. An empty @p other is contained by every range it
 *  is inside, which is what makes "is this caret inside this run?" answerable. */
int RoboTextRangeContainsRange(RoboTextRange range, RoboTextRange other);

/** Non-zero when the two ranges share at least one scalar. Two empty ranges never intersect. */
int RoboTextRangeIntersects(RoboTextRange a, RoboTextRange b);

/** The smallest range covering both. An inverted argument is ignored rather than propagated. */
RoboTextRange RoboTextRangeUnion(RoboTextRange a, RoboTextRange b);

/**
 * The part of @p range between @p from and @p to (counted from the range's own start).
 *
 * Clamped rather than refused: a slice is how a caller walks a run, and a slice past the end yielding
 * the empty range at the end is the same convention `string.h` has. Use @ref RoboTextRangeIsWithin on
 * the result when the caller needs to know the slice was short.
 */
RoboTextRange RoboTextRangeSlice(RoboTextRange range, RoboTextScalarIndex from, RoboTextScalarIndex to);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextRangeEqual(const RoboTextRange *a, const RoboTextRange *b);

/** @p range shifted by @p delta scalars, saturating at zero rather than wrapping. */
RoboTextRange RoboTextRangeShift(RoboTextRange range, int32_t delta);

/* ------------------------------------------------------------------ */
/* Direction                                                           */
/* ------------------------------------------------------------------ */

/**
 * The direction a run is **classified** as.
 *
 * Three values, and the third is the interesting one: `Unknown` is what a run of scalars with no strong
 * direction — digits, punctuation, whitespace — is classified as, and it is deliberately *not* folded
 * into LTR. A text layer that called `123` left-to-right would be asserting a resolution it has not
 * performed; the bidi algorithm (Phase 12.8) is what decides. A caller that needs an answer now uses
 * the *base* direction it passed in.
 */
typedef enum RoboTextDirection {
    RoboTextDirectionUnknown = 0,
    RoboTextDirectionLtr = 1,
    RoboTextDirectionRtl = 2
} RoboTextDirection;

/** Number of defined directions. */
#define RoboTextDirectionCount 3u

/** Non-zero if @p direction is a defined enumerator. */
int RoboTextDirectionIsKnown(RoboTextDirection direction);

/** Lower-case name (`"ltr"`, `"rtl"`, `"unknown"`); never NULL. */
const char *RoboTextDirectionName(RoboTextDirection direction);

/** Non-zero when the direction *requires* reordering the visual order (RTL only). */
int RoboTextDirectionIsRightToLeft(RoboTextDirection direction);

/* ------------------------------------------------------------------ */
/* Script                                                              */
/* ------------------------------------------------------------------ */

/**
 * A script, as the Unicode Character Database names it.
 *
 * A typedef rather than an enum, because RoboUnicode already owns the vocabulary and a second one here
 * would be a second table to keep in step with Unicode. `RoboTextScriptCommon`, `…Inherited` and
 * `…Unknown` are the three values the text layer names by hand: Common is a script shared by many
 * scripts (digits, punctuation), Inherited is a mark that takes its base's script, and Unknown is a
 * code point with no script at all.
 */
typedef UnicodeScript RoboTextScript;

#define RoboTextScriptCommon RoboUScriptCommon
#define RoboTextScriptInherited RoboUScriptInherited

/** Whether @p script is one of the two "not a script of its own" values. */
int RoboTextScriptIsNeutral(RoboTextScript script);

/**
 * Whether @p script **anchors** a script run.
 *
 * The complement of @ref RoboTextScriptIsNeutral, named separately because the resolver reasons in terms
 * of anchors: a scalar whose script is concrete *or* `Unknown` ends the neutral block before it, while a
 * `Common` or `Inherited` scalar waits for the next anchor to be resolved. `Unknown` is deliberately an
 * anchor — an unassigned scalar is a fact about the text, and treating it as neutral would let it be
 * absorbed into a neighbouring script, which is exactly the "do not assign Latin as a fallback" rule.
 */
int RoboTextScriptIsAnchor(RoboTextScript script);

/** The ISO 15924 short name (`"Latn"`) through RoboUnicode; never NULL. */
const char *RoboTextScriptName(RoboTextScript script);

/**
 * A stretch of scalars sharing one script.
 *
 * Declared here rather than beside the run set because the *script resolver* produces one before any run
 * set exists: `RoboTextScriptRunsResolve` (Phase 12.5) is the module's one implementation of the
 * neutrality rule, and the run set is one of its two consumers.
 */
typedef struct RoboTextScriptRun {
    RoboTextRange range;
    RoboTextScript script;
} RoboTextScriptRun;

/* ------------------------------------------------------------------ */
/* Optical size                                                        */
/* ------------------------------------------------------------------ */

/**
 * The optical-size class a run is set at.
 *
 * The same five classes 12.3's typography scale uses, in the same order, and **not applied** by this
 * build: the packaged face is variable and has an `opsz` axis, but applying an axis to an instance is
 * not something RoboFont does yet (`FontOutlineVariationApplied` is 0). The field is carried so that a
 * later goal which *does* apply the axis reads it from here rather than re-deriving it from a size.
 */
typedef enum RoboTextOpticalSize {
    RoboTextOpticalSmall = 0,
    RoboTextOpticalBody = 1,
    RoboTextOpticalUI = 2,
    RoboTextOpticalDisplay = 3,
    RoboTextOpticalLargeDisplay = 4
} RoboTextOpticalSize;

/** Number of defined classes. */
#define RoboTextOpticalSizeCount 5u

/** Non-zero if @p size is a defined class. */
int RoboTextOpticalSizeIsKnown(RoboTextOpticalSize size);

/** Lower-case hyphenated name (`"small"`, `"large-display"`); never NULL; `"unknown"` when undefined. */
const char *RoboTextOpticalSizeName(RoboTextOpticalSize size);

/* ------------------------------------------------------------------ */
/* Language                                                            */
/* ------------------------------------------------------------------ */

/** Bytes a language tag can hold, NUL included. BCP 47 tags are short by construction; this is room. */
#define RoboTextLanguageMaxBytes 24u

/**
 * An optional BCP 47 language tag, carried and not interpreted.
 *
 * Shaping needs it and this phase does not: a `locl` substitution, a language-specific line-break rule
 * and a locale's own glyph forms all key off it, and every one of those is a later goal. What exists
 * now is the field, the validation (`language tags are ASCII alphanumerics and hyphens`) and the
 * copy — so that the run a later goal shapes already carries the tag rather than acquiring one when
 * somebody remembers.
 *
 * An empty tag is the honest default: "no language was declared", which is a different statement from
 * `"und"` and is written as emptiness rather than as a sentinel string.
 */
typedef struct RoboTextLanguage {
    char tag[RoboTextLanguageMaxBytes];
} RoboTextLanguage;

/** Clear @p language to "no tag declared". Safe on NULL. */
void RoboTextLanguageInit(RoboTextLanguage *language);

/**
 * Copy @p tag (NUL-terminated, at most `RoboTextLanguageMaxBytes - 1` bytes) into @p language.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a tag that does not fit; RoboParseError
 *         for a tag that is not ASCII alphanumerics and hyphens.
 */
Result RoboTextLanguageSet(RoboTextLanguage *language, const char *tag);

/** Non-zero when @p language carries a tag. */
int RoboTextLanguageIsSet(const RoboTextLanguage *language);

/** The tag, or `""` for a NULL or unset record. Never NULL. */
const char *RoboTextLanguageTag(const RoboTextLanguage *language);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextLanguageEqual(const RoboTextLanguage *a, const RoboTextLanguage *b);

/* ------------------------------------------------------------------ */
/* Font inputs                                                         */
/* ------------------------------------------------------------------ */

/** The CSS font size this build assumes when nothing declares one (`medium`). */
#define RoboTextInitialFontSizePx 16.0f

/** A line height of zero means "the face's own metrics", which is CSS's `normal`. */
#define RoboTextLineHeightFromFontMetrics 0.0f

/**
 * The inputs a run is set at.
 *
 * **Not** a `FontFace` — the face lives in the context that owns it (`RoboTextFont.h`), because a
 * style record that carried a pointer would make every equality test a pointer comparison and every
 * copy a lifetime question. What is here is the numbers, and they are the CSS font properties spelled
 * with RoboFont's own types so that a caller holding a `FontWeight` from Phase 5 or 12.3 passes it
 * straight through.
 *
 * `letterSpacingPx` is carried and **not applied**: a run's advances are the face's own, and tracking
 * means adding a per-glyph amount that both the measurer and the painter read. This record is where
 * that amount will come from on the day something applies it; until then the field is a number a
 * caller can set and a test can see echoed, and `RoboTextCapabilities.letterSpacing`-style honesty
 * lives in the *typography* module's flags, which report the absence today.
 */
typedef struct RoboTextFontStyle {
    /** Font size in CSS pixels. Always finite and > 0 in a valid context. */
    float sizePx;
    /** CSS numeric weight (100..900 for the standard scale). */
    FontWeight weight;
    /** Upright or italic/oblique. */
    FontStyle style;
    /** CSS font stretch, as RoboFont's percentage (`RoboFontStretchNormal`). */
    FontStretch stretch;
    /** The role's explicit leading, or @ref RoboTextLineHeightFromFontMetrics for the face's own. */
    float lineHeightPx;
    /** Additional advance per glyph, in CSS pixels. Carried; not applied by this build. */
    float letterSpacingPx;
    /** The optical-size class the run is set at. Carried; not applied by this build. */
    RoboTextOpticalSize opticalSize;
} RoboTextFontStyle;

/** Fill @p style with the CSS initial values: 16px, weight 400, upright, normal stretch, no tracking. */
void RoboTextFontStyleInit(RoboTextFontStyle *style);

/** Non-zero when @p style is a setting this build can draw: a finite positive size, a known weight, a
 *  known stretch, a non-negative line height and tracking, and a defined optical class. Safe on NULL. */
int RoboTextFontStyleIsValid(const RoboTextFontStyle *style);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextFontStyleEqual(const RoboTextFontStyle *a, const RoboTextFontStyle *b);

/** Human-readable one-line summary. RoboNotSupported when it does not fit @p capacity (nothing is
 *  truncated, because a truncated setting line reads as a different setting). */
Result RoboTextFontStyleWrite(const RoboTextFontStyle *style,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength);

/* ------------------------------------------------------------------ */
/* Shape options                                                       */
/* ------------------------------------------------------------------ */

/** Bytes a feature tag holds, NUL included. OpenType tags are four ASCII bytes. */
#define RoboTextFeatureTagBytes 5u

/** Features a shape request can carry. Reserved: nothing reads them yet. */
#define RoboTextMaxFeatures 16u

/**
 * What a shaping request asks for beyond the text and the font.
 *
 * `baseDirection` is the paragraph direction a caller *declares* — CSS's `direction`, and what the
 * shell's RTL support would set. It is carried into every run so a caller that declared RTL can tell
 * "these scalars are RTL-class" apart from "no strong direction was found in them"; resolving the
 * paragraph level from the text (P2/P3) is the bidi algorithm's, in Phase 12.8.
 *
 * `features` is the OpenType feature list, and it is **present and unread**: the tags are validated and
 * stored so that the field exists in the shape request a later goal extends, and no generator consults
 * them. A `features` array that silently did nothing would be worse than no array — so
 * `RoboTextShapeOptionsInit` leaves the count at zero and a caller that sets one gets it echoed into
 * every run's options, where a test can see it.
 */
typedef struct RoboTextShapeOptions {
    /** The declared paragraph direction. `Unknown` means "nothing declared". */
    RoboTextDirection baseDirection;
    /** A script hint for the whole text; `Common` means "classify from the scalars". */
    RoboTextScript scriptHint;
    /** A language for the whole text; empty means "none declared". */
    RoboTextLanguage language;
    /** OpenType feature tags, validated and reserved. */
    char features[RoboTextMaxFeatures][RoboTextFeatureTagBytes];
    /** Tags set in @p features. Zero in this build. */
    size_t featureCount;
    /** Reserved; pass 0. */
    uint32_t flags;
} RoboTextShapeOptions;

/** Fill @p options with the defaults: no declared direction, no script hint, no language, no features. */
void RoboTextShapeOptionsInit(RoboTextShapeOptions *options);

/** Non-zero when @p options is usable. Safe on NULL. */
int RoboTextShapeOptionsIsValid(const RoboTextShapeOptions *options);

/**
 * Add a feature tag to @p options.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument, a tag that is not exactly four ASCII bytes,
 *         or options already holding @ref RoboTextMaxFeatures tags.
 */
Result RoboTextShapeOptionsAddFeature(RoboTextShapeOptions *options, const char *tag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_TYPES_H */
