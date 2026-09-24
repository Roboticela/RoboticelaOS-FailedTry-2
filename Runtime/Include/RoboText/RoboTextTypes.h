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
 *   RoboTextClusterIndex   the ordinal of a user-perceived character. Since Phase 12.14 the cluster array
 *                          a glyph run carries is indexed by one, and `RoboTextGlyph::cluster` is a
 *                          glyph's cluster. The grouping is *not* grapheme segmentation (UAX #29): it is
 *                          "a combining mark attaches left", named as the approximation it is in
 *                          `RoboTextGlyphs.h` and `Documentation/Runtime/Text/GlyphRuns.md`.
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
 *               Phase 12.8, and its flag in `RoboTextVersion.h` says so. Phase 12.7 is the stage that
 *               does the classification (`RoboTextDirection.h`): a scalar's `Bidi_Class` projects to a
 *               `RoboTextDirectionClass` (strong / number / mark / neutral), the paragraph direction comes
 *               from a `RoboTextDirectionContext`, and the two meet in one pass that emits direction runs.
 *               What it produces is "these scalars are strong RTL, these are a number, these are neutral
 *               and here is the direction the context gives them" — the input the algorithm will consume,
 *               and *not* a level, an order or a claim to have run it. The one place the two stages touch
 *               is the paragraph direction: `Auto` is UAX #9's P2/P3 (first strong character), because
 *               consuming that single rule is what lets a run carry a direction at all before 12.8.
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
/* Writing mode (Phase 12.20)                                          */
/* ------------------------------------------------------------------ */

/**
 * The primary axis a text runs along, and — for the vertical two — which way its lines stack.
 *
 * The physical half of a text's setting, and the counterpart of @ref RoboTextDirection: direction is the
 * **logical inline** direction the bidi algorithm resolves, writing mode is the **axis** the inline
 * progression runs along. They compose. A `vertical-rl` run still carries a level and a resolved
 * direction; nothing here reorders anything, and 12.8's visual order is still the only one.
 *
 * Three values, and the third is not decoration: `vertical-rl` and `vertical-lr` differ in the **block**
 * direction, so they are different layouts of one text rather than two spellings of one. CSS's
 * `writing-mode` maps onto them directly (`horizontal-tb`, `vertical-rl`, `vertical-lr`).
 *
 * It is **data**, never inferred from the script: Japanese set horizontally and Japanese set vertically
 * are two settings of the same text, and a layer that picked one by looking at the characters could not
 * express the other.
 */
typedef enum RoboTextWritingMode {
    /** Lines stack downward; the inline progression is +x. CSS's `horizontal-tb`. */
    RoboTextWritingHorizontal = 0,
    /** Lines stack leftward; the inline progression is +y. CSS's `vertical-rl`, the CJK default. */
    RoboTextWritingVerticalRightToLeft = 1,
    /** Lines stack rightward; the inline progression is +y. CSS's `vertical-lr`. */
    RoboTextWritingVerticalLeftToRight = 2,
    RoboTextWritingModeCount = 3
} RoboTextWritingMode;

/** Non-zero if @p mode is a defined enumerator. */
int RoboTextWritingModeIsKnown(RoboTextWritingMode mode);

/** Hyphenated name (`"horizontal-tb"`, `"vertical-rl"`, `"vertical-lr"`); never NULL. */
const char *RoboTextWritingModeName(RoboTextWritingMode mode);

/** Non-zero when @p mode advances the pen along **y**. The one test a positioning or measuring site asks. */
int RoboTextWritingModeIsVertical(RoboTextWritingMode mode);

/** Non-zero when @p mode stacks lines along **-x** (the `vertical-rl` block direction). */
int RoboTextWritingModeBlockIsLeftward(RoboTextWritingMode mode);

/**
 * How a caller wants a vertical run's glyphs presented, in CSS's `text-orientation` vocabulary.
 *
 * `Mixed` is the interesting one and the default: *the data decides* — an ideographic character stands
 * up, a Latin one lies on its side — which is the rule `RoboTextVerticalOrientationOf` implements.
 * `Upright` and `Sideways` force the answer and are honest about it: `Sideways` on a Japanese column
 * rotates it and `Upright` on a Latin word stacks its letters unrotated, neither of which is an error.
 */
typedef enum RoboTextTextOrientation {
    /** The character decides: upright for ideographic scripts, sideways for everything else. CSS's `mixed`. */
    RoboTextTextOrientationMixed = 0,
    /** Every glyph stands up, whatever it is. CSS's `upright`. */
    RoboTextTextOrientationUpright = 1,
    /** Every glyph lies on its side, whatever it is. CSS's `sideways`. */
    RoboTextTextOrientationSideways = 2,
    RoboTextTextOrientationCount = 3
} RoboTextTextOrientation;

/** Non-zero if @p orientation is a defined enumerator. */
int RoboTextTextOrientationIsKnown(RoboTextTextOrientation orientation);

/** Lower-case name (`"mixed"`, `"upright"`, `"sideways"`); never NULL. */
const char *RoboTextTextOrientationName(RoboTextTextOrientation orientation);

/**
 * The **resolved** orientation of one glyph or one run: the answer, not the request.
 *
 * `Upright` is the glyph in its own vertical form — an ideograph as designed, and for a character whose
 * font carries a `vert`/`vrt2` alternate **that alternate** (which is why a CJK full stop is a different
 * glyph id in a vertical column rather than the same glyph rotated). `Sideways` is the glyph rotated a
 * quarter turn clockwise from its horizontal form, which is how Latin is set inside a CJK column.
 *
 * A run carries **one** of these, because a run is cut where the orientation changes: a vertical line of
 * `漢字ABC漢字` is three runs and no run has glyphs that disagree. That is what lets a painter read the
 * answer off one field of the run instead of asking each glyph, and why the enum is not per-glyph.
 */
typedef enum RoboTextGlyphOrientation {
    /** The glyph's own vertical form: ideographs, CJK punctuation, `vert` alternates. */
    RoboTextGlyphOrientationUpright = 0,
    /** The glyph rotated a quarter turn clockwise: Latin and most non-ideographic scripts. */
    RoboTextGlyphOrientationSideways = 1,
    RoboTextGlyphOrientationCount = 2
} RoboTextGlyphOrientation;

/** Non-zero if @p orientation is a defined enumerator. */
int RoboTextGlyphOrientationIsKnown(RoboTextGlyphOrientation orientation);

/** Lower-case name (`"upright"`, `"sideways"`); never NULL. */
const char *RoboTextGlyphOrientationName(RoboTextGlyphOrientation orientation);

/** Non-zero when @p orientation stands the glyph up. */
int RoboTextGlyphOrientationIsUpright(RoboTextGlyphOrientation orientation);

/* ------------------------------------------------------------------ */
/* Direction classes, modes and the paragraph context                  */
/* ------------------------------------------------------------------ */

/**
 * What a scalar's own `Bidi_Class` says about it, before any context is applied.
 *
 * The `Bidi_Class` itself is RoboUnicode's (23 values, `RoboUnicodeBidiClass`); this is the projection the
 * *direction stage* reasons in, and the projection is the phase's point: five kinds, because the five are
 * treated differently and because collapsing any two of them loses a decision.
 *
 *   Neutral        no directional evidence at all: `WS`, `ON`, `B`, `S`, `BN`, and every explicit
 *                  formatting control (`LRE`/`RLE`/`LRO`/`RLO`/`PDF`/`LRI`/`RLI`/`FSI`/`PDI`). The control
 *                  characters are neutral here **on purpose**: their *contents* are classified from the
 *                  contents' own classes, and honouring the embedding they open is 12.8's, which is what
 *                  `bidiAlgorithm` reports as absent.
 *   LeftToRight    `L`.
 *   RightToLeft    `R` and `AL` — two classes, one direction. They differ in what they do to adjacent
 *                  numbers (W2) and in their mirroring, both of which are 12.8's.
 *   Number         the weak classes `EN`, `AN`, `ES`, `ET` and `CS`. **A digit is not classified a
 *                  direction by its own class here** — it is classified a *number*, which is the honest
 *                  reading of `2` being `EN` rather than `L`.
 *   AttachLeft     `NSM`: a nonspacing mark takes the type of the character before it (UAX #9 W1).
 *
 * `ES`/`ET`/`CS` share `Number` with `EN`/`AN` because they only ever mean "part of a number" — the check
 * that a `Number` *block* actually contains an `EN` or an `AN` is what demotes a lone comma to a neutral,
 * and it happens on the block rather than here so that the class of a scalar stays a pure function of the
 * scalar.
 */
typedef enum RoboTextDirectionClass {
    RoboTextDirectionClassNeutral = 0,
    RoboTextDirectionClassLeftToRight = 1,
    RoboTextDirectionClassRightToLeft = 2,
    RoboTextDirectionClassNumber = 3,
    RoboTextDirectionClassAttachLeft = 4
} RoboTextDirectionClass;

/** Number of defined classes. */
#define RoboTextDirectionClassCount 5u

/** Non-zero if @p directionClass is a defined enumerator. */
int RoboTextDirectionClassIsKnown(RoboTextDirectionClass directionClass);

/** Lower-case name (`"neutral"`, `"ltr"`, `"rtl"`, `"number"`, `"attach-left"`); never NULL. */
const char *RoboTextDirectionClassName(RoboTextDirectionClass directionClass);

/**
 * How a paragraph's direction is arrived at.
 *
 * `Explicit` are CSS's `direction: ltr|rtl` and what a document's `dir` attribute means: the author stated
 * it, so the text does not get a vote. `Auto` is UAX #9's P2/P3 — the first strong character decides — and
 * its *provisional* status is the reason the mode is a caller's input rather than something the text layer
 * infers: 12.8 implements P2/P3 as part of the whole algorithm, and until then `Auto` falls back to the
 * context's own @ref RoboTextDirectionContext.fallbackDirection when the text has no strong character.
 */
typedef enum RoboTextDirectionMode {
    /** First strong character, else the context's fallback. */
    RoboTextDirectionModeAuto = 0,
    /** Stated left-to-right. */
    RoboTextDirectionModeExplicitLtr = 1,
    /** Stated right-to-left. */
    RoboTextDirectionModeExplicitRtl = 2
} RoboTextDirectionMode;

/** Number of defined modes. */
#define RoboTextDirectionModeCount 3u

/** Non-zero if @p mode is a defined enumerator. */
int RoboTextDirectionModeIsKnown(RoboTextDirectionMode mode);

/** Lower-case name (`"auto"`, `"ltr"`, `"rtl"`); never NULL. */
const char *RoboTextDirectionModeName(RoboTextDirectionMode mode);

/**
 * The direction context a stretch of text is analysed in.
 *
 * Two fields, and the second is the one worth explaining. `mode` says where the paragraph direction comes
 * from; `fallbackDirection` is what an `Auto` paragraph resolves to when nothing in the text has a strong
 * direction — and it is `Unknown` by default, **not** `Ltr`, because UAX #9's P3 default (`level 0`) is the
 * *algorithm's* default and this stage has not run the algorithm. A caller that is laying out UI declares
 * what it wants: the shell and the document both pass `{ Auto, Ltr }` (CSS's initial `direction: ltr`),
 * which is what makes `"123"` a left-to-right run on a desktop surface and leaves a bare analysis of it
 * unresolved.
 *
 * The context is also the seam for an **explicit override** (Phase 12.7 §"explicit direction override"):
 * a span that carries its own `dir` is analysed with `ExplicitLtr`/`ExplicitRtl`, so the override lives in
 * the context and never in a mutated Unicode property.
 */
typedef struct RoboTextDirectionContext {
    /** Where the paragraph direction comes from. */
    RoboTextDirectionMode mode;
    /** What an `Auto` paragraph with no strong character resolves to; may be `Unknown`. */
    RoboTextDirection fallbackDirection;
} RoboTextDirectionContext;

/** Fill @p context with `{ Auto, Unknown }`: first strong wins, and nothing is claimed when there is none. */
void RoboTextDirectionContextInit(RoboTextDirectionContext *context);

/**
 * Build the context for a *declared* paragraph direction.
 *
 * `Unknown` means "nothing was declared" and gives `{ Auto, Unknown }`; `Ltr`/`Rtl` give the explicit mode
 * with the same value as the fallback, which is how `RoboTextShapeOptions.baseDirection` — the field 12.4
 * declared — maps onto this stage without the caller learning a second vocabulary.
 */
RoboTextDirectionContext RoboTextDirectionContextForBase(RoboTextDirection declared);

/** The context that **states** @p direction for a span. Same as `…ForBase` for a known direction. */
RoboTextDirectionContext RoboTextDirectionContextExplicit(RoboTextDirection direction);

/** Non-zero when @p context names a defined mode and a defined fallback. Safe on NULL. */
int RoboTextDirectionContextIsValid(const RoboTextDirectionContext *context);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextDirectionContextEqual(const RoboTextDirectionContext *a, const RoboTextDirectionContext *b);

/**
 * A stretch of scalars sharing one classified direction.
 *
 * Declared here rather than beside the run set for the same reason @ref RoboTextScriptRun is: the direction
 * resolver produces one before any run set exists and both consumers must agree about the type.
 */
typedef struct RoboTextDirectionRun {
    RoboTextRange range;
    RoboTextDirection direction;
} RoboTextDirectionRun;

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
/* Feature settings                                                    */
/* ------------------------------------------------------------------ */

/** Bytes a feature tag holds, NUL included. OpenType tags are four ASCII bytes. */
#define RoboTextFeatureTagBytes 5u

/** Features one settings set (and therefore one shape request) can carry. */
#define RoboTextMaxFeatures 16u

/**
 * What a caller asked of one feature.
 *
 * Three states rather than a boolean, because "the caller said nothing" and "the caller turned it off"
 * are different requests: a feature the caller does not name takes the script's default (which is on for
 * `ccmp`, `liga` and `calt` and off for `dlig`, `ss01` and friends), and a feature the caller turns **off**
 * overrides a default that would otherwise have run it. A two-state model cannot express the second, and
 * the difference is exactly what a `font-feature-settings: "liga" 0` is for.
 */
typedef enum RoboTextFeatureState {
    /** The caller said nothing: the script's own default decides. */
    RoboTextFeatureDefault = 0,
    /** Run it, whether or not the script would have. */
    RoboTextFeatureOn = 1,
    /** Do not run it, even though the script's defaults would have. */
    RoboTextFeatureOff = 2
} RoboTextFeatureState;

/** Number of defined states. */
#define RoboTextFeatureStateCount 3u

/** Non-zero if @p state is a defined enumerator. */
int RoboTextFeatureStateIsKnown(RoboTextFeatureState state);

/** Lower-case name (`"default"`, `"on"`, `"off"`); never NULL. */
const char *RoboTextFeatureStateName(RoboTextFeatureState state);

/**
 * One feature and what was asked of it.
 *
 * `value` is the feature's *parameter*, and it is used by the one feature family that has one in this
 * build: an alternate-substitution feature (`aalt`, `salt`, `ss01`…) selects the `value`-th alternate, and
 * zero means "the first", which is the deterministic default the phase requires. It is not a switch
 * count: a caller that wants a different alternate writes `value = 2`, and a caller that wants the feature
 * off writes `state = Off`.
 */
typedef struct RoboTextFeatureSetting {
    /** The OpenType tag, four ASCII bytes and a terminator. */
    char tag[RoboTextFeatureTagBytes];
    /** What was asked of it. */
    RoboTextFeatureState state;
    /** The feature's parameter (the alternate index, for the alternate features). */
    uint32_t value;
} RoboTextFeatureSetting;

/**
 * A caller's feature settings — at most @ref RoboTextMaxFeatures of them.
 *
 * A fixed-capacity value rather than a list, for the same reason `RoboTextFontStyle` is one: it is copied
 * into every run (~200 bytes), and a run that owned a pointer would need a lifetime rule. The cap is
 * generous for real use — a CSS rule that names more than sixteen features is not a stylesheet anyone
 * writes — and `RoboTextFeatureSetSet` refuses the seventeenth rather than silently dropping it.
 */
typedef struct RoboTextFeatureSet {
    RoboTextFeatureSetting settings[RoboTextMaxFeatures];
    /** Settings held. Always @<= @ref RoboTextMaxFeatures for a set this module produced. */
    size_t count;
} RoboTextFeatureSet;

/** Clear @p set to "the caller said nothing". Safe on NULL. */
void RoboTextFeatureSetInit(RoboTextFeatureSet *set);

/** Non-zero when @p set is a set this build can act on. Safe on NULL. */
int RoboTextFeatureSetIsValid(const RoboTextFeatureSet *set);

/**
 * Add or replace the setting for @p tag.
 *
 * Replacing rather than appending is what makes the *last* word the caller's: a caller that enables `liga`
 * and then disables it holds one setting, and the resolution below reads one answer.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument, a tag that is not exactly four ASCII bytes, or
 *         an undefined state; RoboNotSupported when @p set is full of *other* tags.
 */
Result RoboTextFeatureSetSet(RoboTextFeatureSet *set,
                             const char *tag,
                             RoboTextFeatureState state,
                             uint32_t value);

/** Settings held. Zero for NULL. */
size_t RoboTextFeatureSetCount(const RoboTextFeatureSet *set);

/** Setting @p index, borrowed, or NULL when out of range. */
const RoboTextFeatureSetting *RoboTextFeatureSetAt(const RoboTextFeatureSet *set, size_t index);

/**
 * The setting for @p tag, borrowed, or NULL when the caller named it.
 *
 * The lookup the engine makes once per feature, and the reason a set is a value: a caller comparing two
 * runs' features asks this question rather than walking an array in its own order.
 */
const RoboTextFeatureSetting *RoboTextFeatureSetFind(const RoboTextFeatureSet *set, const char *tag);

/** Field-for-field equality, tag by tag and regardless of insertion order. Safe on NULL (NULL == NULL). */
int RoboTextFeatureSetEqual(const RoboTextFeatureSet *a, const RoboTextFeatureSet *b);

/** Space-separated `"liga" "kern=1"`-style listing (`"(none)"` for an empty set). */
Result RoboTextFeatureSetWrite(const RoboTextFeatureSet *set,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength);

/* ------------------------------------------------------------------ */
/* Ligature policy (Phase 12.12)                                        */
/* ------------------------------------------------------------------ */

/**
 * The kind of ligature a feature asks for (Phase 12.12).
 *
 * Five tags, and the kinds are named rather than the tags because the *policy* is about typography, not about
 * OpenType: a stylesheet says "no common ligatures", and which tag that lands on is this module's business.
 * @ref RoboTextLigatureKindTag is the one mapping, and `RoboTextLigature.c` is the only file that writes a
 * tag literal.
 */
typedef enum RoboTextLigatureKind {
    /** `liga` — the ligatures a reader expects to see: `fi`, `fl`, `ffi`. */
    RoboTextLigatureStandard = 0,
    /** `clig` — contextual ligatures: the font ligates only in a stated context. */
    RoboTextLigatureContextual = 1,
    /** `rlig` — required ligatures. Not optional typography: Arabic and Indic shaping depend on them. */
    RoboTextLigatureRequired = 2,
    /** `dlig` — discretionary ligatures: pretty, optional, and off unless asked for. */
    RoboTextLigatureDiscretionary = 3,
    /** `hlig` — historical ligatures: `ct`, `st`. Off unless asked for. */
    RoboTextLigatureHistorical = 4,
    RoboTextLigatureKindCount = 5
} RoboTextLigatureKind;

/**
 * The ligature policy a caller states, per kind.
 *
 * Each entry is a @ref RoboTextFeatureState, so `RoboTextFeatureDefault` means "say nothing about this kind
 * and let the font's own record decide" — which is not the same as `Off`, and is why the policy is not a
 * struct of booleans. The default policy is `RoboTextLigatureStandard`/`…Contextual`/`…Required` on and
 * `…Discretionary`/`…Historical` off, which is the same answer the GSUB plan's script defaults give: the
 * point of stating it is that it becomes a *decision a caller can make* rather than a table in an engine.
 */
typedef struct RoboTextLigaturePolicy {
    RoboTextFeatureState kinds[RoboTextLigatureKindCount];
} RoboTextLigaturePolicy;

/**
 * How ligatures are asked for, in the vocabulary a stylesheet uses (Phase 12.12 §"CSS compatibility hook").
 *
 * This is the `font-variant-ligatures` vocabulary, and the enum is spelled in full because the values are
 * not a scale: `Common` turns the common ligatures on *and* leaves the rest alone, while `None` turns the
 * optional kinds off and must not touch `rlig`. `RoboTextLigaturePolicyFromVariant` is that translation,
 * and it is the *only* place CSS lands — the CSS layer never writes an OpenType tag.
 */
typedef enum RoboTextLigatureVariant {
    /** `normal`: the default policy. */
    RoboTextLigatureVariantNormal = 0,
    /** `none`: the optional kinds off, `rlig` left alone. */
    RoboTextLigatureVariantNone = 1,
    /** `common-ligatures`. */
    RoboTextLigatureVariantCommon = 2,
    /** `no-common-ligatures`. */
    RoboTextLigatureVariantNoCommon = 3,
    /** `contextual`. */
    RoboTextLigatureVariantContextual = 4,
    /** `no-contextual`. */
    RoboTextLigatureVariantNoContextual = 5,
    /** `discretionary-ligatures`. */
    RoboTextLigatureVariantDiscretionary = 6,
    /** `historical-ligatures`. */
    RoboTextLigatureVariantHistorical = 7,
    RoboTextLigatureVariantCount = 8
} RoboTextLigatureVariant;

/* ------------------------------------------------------------------ */
/* Per-range feature overrides (Phase 12.9, applied by 12.13)          */
/* ------------------------------------------------------------------ */

/**
 * At most this many per-range feature overrides in one shape request.
 *
 * A bound rather than a convenience: the options are a value copied into every run, and the cut points a
 * range introduces multiply with the script and direction boundaries a run set already has. Eight is more
 * than a stylesheet writes (`font-feature-settings` on a span, a `font-kerning: none` on a pull quote) and it
 * is enough that the feature is real rather than aspirational.
 */
#define RoboTextMaxFeatureRanges 8u

/**
 * Feature settings that apply to one scalar range and to nothing else.
 *
 * The point of the type is that a range is a *more specific* statement than the whole-text settings beside
 * it, which is the same rule a stylesheet's cascade uses: a tag the range names is the range's, and a tag it
 * does not name keeps the whole-text answer. That is why the range carries a whole `RoboTextFeatureSet`
 * rather than a tag and a state — a span can state several, and the resolution is one merge.
 *
 * The range is in the text's **scalar** index space, half-open, and a range that reaches past the end of the
 * text simply stops at it.
 */
typedef struct RoboTextFeatureRange {
    RoboTextRange range;
    RoboTextFeatureSet features;
} RoboTextFeatureRange;

/* ------------------------------------------------------------------ */
/* Kerning policy (Phase 12.13)                                        */
/* ------------------------------------------------------------------ */

/**
 * Where a face's kerning may come from, in the order the two sources are allowed to be used.
 *
 * **One source, never both.** A face can carry kerning in its GPOS `kern` feature *and* in a legacy `kern`
 * table, and adding the two would kern the same pair twice — the same glyph pair moved by two tables that
 * were each written as the whole answer for their era. So the rule is a choice rather than an accumulation:
 * GPOS answers when it has anything to say, and a legacy table is a **fallback** for a face whose GPOS does
 * not, which is what `RoboTextKerningLegacyIfNoGposKern` means and why there is no "both" enumerator.
 */
typedef enum RoboTextKerningLegacy {
    /**
     * The legacy `kern` table is never consulted. **The default**, and the policy of a build that has not
     * implemented reading it: a fallback that does nothing would be a caller-visible lie, so this is what a
     * caller gets unless it explicitly asks for the fallback and the fallback is there.
     */
    RoboTextKerningLegacyNever = 0,
    /**
     * Consult the legacy `kern` table **only** when the face's GPOS carries no `kern` feature for the run's
     * script and language — the non-double-counting rule stated above.
     */
    RoboTextKerningLegacyIfNoGposKern = 1,
    RoboTextKerningLegacyCount = 2
} RoboTextKerningLegacy;

/**
 * What a caller asks of kerning, as data (Phase 12.13).
 *
 * The phase's `KerningPolicy`, and its whole job is to be **converted into the feature set**: `kern` is an
 * OpenType feature like any other, so a policy that has to be honoured by a special case in the paint layer
 * would be a policy in the wrong place. @ref RoboTextKerningPolicyApplyIfUnset is that conversion, and like
 * the ligature policy it is a **default under** the caller's own `features` rather than over them: a caller
 * that wrote `"kern" off` keeps its answer.
 */
typedef struct RoboTextKerningPolicy {
    /** What is asked of the `kern` feature when the caller's own features say nothing. */
    RoboTextFeatureState kern;
    /** Which source may answer. See @ref RoboTextKerningLegacy. */
    RoboTextKerningLegacy legacy;
} RoboTextKerningPolicy;

/* ------------------------------------------------------------------ */
/* Shape options                                                       */
/* ------------------------------------------------------------------ */

/**
 * What a shaping request asks for beyond the text and the font.
 *
 * `baseDirection` is the paragraph direction a caller *declares* — CSS's `direction`, and what the shell's
 * RTL support would set. It is carried into every run so a caller that declared RTL can tell "these scalars
 * are RTL-class" apart from "no strong direction was found in them"; resolving the paragraph level from the
 * text (P2/P3) is the bidi algorithm's, in Phase 12.8.
 *
 * `features` is the OpenType feature list, and since Phase 12.10 it is **read**: the substitution engine
 * resolves these settings against the face's own GSUB ScriptList/LangSys/FeatureList and runs the lookups of
 * the features that end up on. A caller that names nothing gets the script's defaults, which is what makes
 * `"office"` ligate without anyone asking and what makes `"liga" off` meaningful.
 *
 * `kerning` is the Phase 12.13 policy over the same rule: `kern` is a feature, so the policy becomes one
 * entry in `features` and reaches the positioning plan through the same door every other tag uses. That is
 * why `font-kerning: none` is `RoboTextShapeOptionsSetFeature(options, "kern", RoboTextFeatureOff, 0)` and
 * nothing else — no paint layer, no layout stage and no per-script branch ever learns that kerning exists.
 *
 * `ligatures` is the five-tag policy of Phase 12.12 applied **only where `features` says nothing** — a
 * policy entry is written into a run's features when @ref RoboTextFeatureSetFind of its tag answers
 * nothing, so `RoboTextShapeOptionsSetFeature(options, "liga", RoboTextFeatureOff, 0)` still wins over a
 * policy that turns standard ligatures on. That order is deliberate and tested: an explicit setting is a
 * caller's decision about one tag and a policy is a default about five, so the specific one comes first.
 */
typedef struct RoboTextShapeOptions {
    /** The declared paragraph direction. `Unknown` means "nothing declared". */
    RoboTextDirection baseDirection;
    /** A script hint for the whole text; `Common` means "classify from the scalars". */
    RoboTextScript scriptHint;
    /** A language for the whole text; empty means "none declared". */
    RoboTextLanguage language;
    /** The caller's OpenType feature settings. */
    RoboTextFeatureSet features;
    /** The ligature policy, for the tags @ref features does not state. */
    RoboTextLigaturePolicy ligatures;
    /** The kerning policy, for the `kern` tag @ref features does not state (Phase 12.13). */
    RoboTextKerningPolicy kerning;
    /**
     * The primary axis the text runs along (Phase 12.20). `Horizontal` unless a caller says otherwise.
     *
     * A vertical mode makes the run set cut where the **orientation** changes, makes shaping ask the
     * face's `vmtx` for a vertical advance, applies the run's `vert`/`vrt2` lookups through 12.10's
     * engine, and makes the measurement's `advanceY` the number a layout pass justifies against. It
     * changes no glyph id on its own, and it never reverses the text: the writing mode decides which
     * axis the pen moves on, and 12.8 still decides the visual order.
     */
    RoboTextWritingMode writingMode;
    /** How the glyphs of a vertical run are presented (Phase 12.20). Ignored when @ref writingMode is
     *  horizontal, because a horizontal run's glyphs are all upright by construction. */
    RoboTextTextOrientation textOrientation;
    /**
     * Settings that apply to one scalar range each, merged **over** @ref features for the scalars they cover.
     *
     * A run set is cut at every range boundary before shaping, so each run carries one feature set and the
     * engine resolves one plan per run — which is what makes "`kern` off in the first two scalars of
     * `AVATAR`" a fact about the runs rather than something a paint layer has to remember.
     */
    RoboTextFeatureRange featureRanges[RoboTextMaxFeatureRanges];
    /** Ranges held. Always @<= @ref RoboTextMaxFeatureRanges for options this module produced. */
    size_t featureRangeCount;
    /** Reserved; pass 0. */
    uint32_t flags;
} RoboTextShapeOptions;

/** Fill @p options with the defaults: no declared direction, no script hint, no language, no features,
 *  @ref RoboTextLigaturePolicyDefault for `ligatures`, and horizontal/mixed writing. */
void RoboTextShapeOptionsInit(RoboTextShapeOptions *options);

/** Non-zero when @p options is usable. Safe on NULL. */
int RoboTextShapeOptionsIsValid(const RoboTextShapeOptions *options);

/**
 * Turn a feature tag **on** in @p options.
 *
 * The door a caller that wants one more feature uses, and the same thing as
 * `RoboTextFeatureSetSet(&options->features, tag, RoboTextFeatureOn, 0)`.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a tag that is not exactly four ASCII bytes;
 *         RoboNotSupported when the options already carry @ref RoboTextMaxFeatures *other* tags.
 */
Result RoboTextShapeOptionsAddFeature(RoboTextShapeOptions *options, const char *tag);

/**
 * State a feature explicitly in @p options — including **off**, which `…AddFeature` cannot express.
 *
 * @param value the feature's parameter: for an alternate-substitution feature, the alternate to select
 *              (zero-based); ignored by every other feature.
 */
Result RoboTextShapeOptionsSetFeature(RoboTextShapeOptions *options,
                                      const char *tag,
                                      RoboTextFeatureState state,
                                      uint32_t value);

/**
 * State a feature for the scalars of @p range only.
 *
 * Adds the setting to the request's range for @p range — an existing entry with the same bounds is reused, so
 * two calls for one range compose the way two declarations on one rule do — and refuses an empty or inverted
 * range rather than storing a range that covers nothing.
 *
 * @param value the feature's parameter, as @ref RoboTextShapeOptionsSetFeature takes it.
 * @return RoboOk; RoboInvalidArgument for a NULL @p options, a bad tag, an undefined state or an empty range;
 *         RoboNotSupported when the request already holds @ref RoboTextMaxFeatureRanges *other* ranges.
 */
Result RoboTextShapeOptionsSetFeatureRange(RoboTextShapeOptions *options,
                                           RoboTextRange range,
                                           const char *tag,
                                           RoboTextFeatureState state,
                                           uint32_t value);

/** How many ranges @p options holds. Zero for NULL. */
size_t RoboTextShapeOptionsFeatureRangeCount(const RoboTextShapeOptions *options);

/** The @p index'th range, or NULL when @p index is out of range. */
const RoboTextFeatureRange *RoboTextShapeOptionsFeatureRange(const RoboTextShapeOptions *options,
                                                             size_t index);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_TYPES_H */
