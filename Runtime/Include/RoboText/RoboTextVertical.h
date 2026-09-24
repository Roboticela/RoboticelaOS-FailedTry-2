/*
 * RoboText's vertical writing (Phase 12.20) — the writing mode, the orientation, the two axes and the
 * `vert`/`vrt2` hook.
 *
 * ## The rule this header exists for
 *
 * > RoboText owns writing-mode-aware shaping and glyph positioning; the renderer only paints the
 * > resulting `GlyphRun`.
 *
 * Everything below is the *decision* half of that sentence. Which axis the pen advances on, whether a
 * glyph stands up or lies on its side, which OpenType features turn `一` into a vertical comma — all of
 * it is answered here, once, and the painter reads the answer off the run. A rasterizer that had to ask
 * "is this Latin?" to know whether to rotate would be the second implementation of this file.
 *
 * ## Writing mode is data, never inferred
 *
 * `RoboTextWritingMode` is a field on the shape options and on every run, and it is **not** derived from
 * the script. Japanese set horizontally and Japanese set vertically are two different settings of the
 * same text, and a text layer that picked one by looking at the characters could not express the other.
 * The four combinations the phase names — Japanese+horizontal, Japanese+vertical, Latin+vertical,
 * Arabic+vertical — are all reachable, and each is a `RoboTextShapeOptions` the caller fills in.
 *
 * ## Direction and writing mode are different questions
 *
 * `RoboTextDirection` (12.4/12.7/12.8) is the **logical inline** direction: `ltr` or `rtl`, the thing the
 * bidi algorithm resolves. `RoboTextWritingMode` is the **physical axis**: horizontal or vertical. They
 * compose rather than replace each other. A vertical run still carries its bidi level and its resolved
 * direction; the vertical layout then establishes which physical axis the inline progression runs along.
 * Nothing in this header reorders anything, and 12.8's `visualOrder` is still the only visual ordering.
 *
 * ## Axes are explicit
 *
 * `RoboTextWritingAxes` is the inline/block pair, spelled as two unit vectors rather than as a boolean.
 * A layout pass that wrote `if (vertical) { swap x and y }` gets the *left-to-right* vertical case right
 * and the right-to-left one wrong, because the block axis differs between them. Naming both axes makes
 * the two vertical modes one table row each.
 *
 * ## Orientation, and how much of UAX #50 is here
 *
 * `RoboTextGlyphOrientation` is the per-glyph answer: a glyph stands **upright** (its own vertical form,
 * used by CJK) or **sideways** (the glyph rotated a quarter turn, used by Latin inside a Japanese column).
 *
 * The classification is **this module's own table plus RoboUnicode's script property**, and it is an
 * approximation of UAX #50's `Vertical_Orientation`, not that property: RoboUnicode does not carry
 * `Vertical_Orientation` yet, and inventing a second Unicode table inside RoboText would violate the
 * boundary Phase 12.5 set. What is implemented is the part that matters for the scripts this build
 * shapes — ideographic scripts upright, everything else sideways, CJK punctuation and vertical forms
 * upright, halfwidth Katakana rotated — and `RoboTextVerticalScriptIsUpright` is the single row per
 * script. When RoboUnicode gains `Vertical_Orientation`, this table becomes a read of it and the doors
 * here do not change.
 */
#ifndef ROBO_TEXT_VERTICAL_H
#define ROBO_TEXT_VERTICAL_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboText/RoboTextUnicode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Writing axes                                                        */
/* ------------------------------------------------------------------ */

/*
 * The vocabulary — `RoboTextWritingMode`, `RoboTextTextOrientation` and `RoboTextGlyphOrientation`, with
 * their `IsKnown`/`Name` doors — is in `RoboTextTypes.h`, because that is the header the rest of the layer
 * speaks and `RoboTextShapeOptions` carries two of the three by value. What lives *here* is what a writing
 * mode **does*: its axes, the orientation tables, the feature hook and the dump.
 */

/**
 * The inline and block axes of @p mode, as unit vectors.
 *
 * A value rather than a function's outputs, so a layout pass can hold it and pass it down. Each vector
 * is one of `±1` on exactly one of the two axes; `inlineX`/`inlineY` is the direction the pen advances
 * within a line, and `blockX`/`blockY` is the direction the *next line* is placed in.
 *
 * ```text
 * horizontal-tb     inline (+1, 0)   block  (0, +1)
 * vertical-rl       inline (0, +1)   block (-1, 0)
 * vertical-lr       inline (0, +1)   block (+1, 0)
 * ```
 */
typedef struct RoboTextWritingAxes {
    float inlineX;
    float inlineY;
    float blockX;
    float blockY;
} RoboTextWritingAxes;

/** The axes of @p mode. An undefined @p mode answers the horizontal axes rather than a zero vector. */
RoboTextWritingAxes RoboTextWritingModeAxes(RoboTextWritingMode mode);

/** Non-zero when `RoboTextWritingModeAxes(mode)` is the horizontal pair. Safe on NULL. */
int RoboTextWritingAxesIsHorizontal(const RoboTextWritingAxes *axes);

/** Non-zero when @p mode and @p orientation are a setting this build can shape. */
int RoboTextWritingModeIsValid(RoboTextWritingMode mode, RoboTextTextOrientation orientation);

/**
 * @p value along the inline axis, as a physical vector.
 *
 * The one door a layout pass or a measurer uses to turn "the pen moved 12px" into "x moved 12, y moved
 * 0" (horizontal) or "x moved 0, y moved 12" (vertical), so that no call site writes the swap itself. A
 * vertical mode's inline vector is `(0, +1)` in both of its variants; it is the **block** vector that
 * tells `vertical-rl` from `vertical-lr`.
 */
Result RoboTextWritingAxesInline(const RoboTextWritingAxes *axes,
                                 float value,
                                 float *outX,
                                 float *outY);

/** See @ref RoboTextWritingAxesInline: @p value along the block axis (where the next line goes). */
Result RoboTextWritingAxesBlock(const RoboTextWritingAxes *axes,
                                float value,
                                float *outX,
                                float *outY);

/* ------------------------------------------------------------------ */
/* Orientation                                                         */
/* ------------------------------------------------------------------ */

/**
 * The rotation a sideways glyph is drawn at, in radians, **clockwise**.
 *
 * `0` for upright; `π/2` for sideways. Given in radians because that is what a rasterizer's transform
 * takes, and clockwise because that is what "rotate Latin 90° in a Japanese column" means when the
 * inline axis is `+y` and the block axis is `-x`.
 *
 * This is deliberately the *whole* of what a painter needs to know about orientation: a painter that also
 * chose the angle would be deciding it, which is this layer's job. Since 12.20's closeout the angle is a
 * value the painter composes (`PaintGlyphFlagSideways` on the record, `GfxGlyphRotation` at the rasterizer,
 * where the outline is turned) rather than a comment: `text.vertical.*`, `paint.text.sideways`,
 * `graphics.glyph.rotation` and `graphics.typography.vertical_specimen` are the chain.
 */
float RoboTextGlyphOrientationRotation(RoboTextGlyphOrientation orientation);

/**
 * Non-zero when @p script is written upright in a vertical column.
 *
 * The row-per-script table the header describes, and the one place a script's vertical behaviour is
 * stated: the ideographic scripts — Han, Hiragana, Katakana, Bopomofo, Hangul, Yi — stand up, and every
 * other script (Latin, Greek, Cyrillic, Arabic, Hebrew, Devanagari, …) lies on its side. `Common` and
 * `Inherited` are **false** here because they name no script of their own; the characters inside them
 * are decided by `RoboTextVerticalCodePointIsUpright`.
 */
int RoboTextVerticalScriptIsUpright(RoboTextScript script);

/**
 * Non-zero when @p codePoint has an upright vertical form, by its own code point.
 *
 * The second half of the rule above, for the characters whose script is `Common`: CJK punctuation
 * (U+3000–U+303F), the vertical forms block (U+FE10–U+FE1F), CJK compatibility forms (U+FE30–U+FE4F),
 * fullwidth forms (U+FF01–U+FF60, U+FFE0–U+FFE6), enclosed ideographic supplement (U+1F200–U+1F2FF) and
 * the CJK ideograph extension blocks above the BMP. Halfwidth Katakana (U+FF61–U+FFDC) is deliberately
 * **not** upright: UAX #50 gives those characters a rotated form, and a halfwidth `ｶ` in a vertical
 * column is a rotated `ｶ`.
 */
int RoboTextVerticalCodePointIsUpright(RoboTextCodePoint codePoint);

/**
 * Non-zero when @p codePoint has a **rotated** vertical form although its script stands up.
 *
 * Halfwidth Katakana (U+FF61–U+FFDC) and the halfwidth Hangul and marks beside it (U+FFE8–U+FFEE). UAX #50
 * rotates these; their *script* is Katakana or Hangul, which would otherwise make them upright. This table is
 * therefore consulted **before** the script in @ref RoboTextVerticalOrientationOf, and it is a door of its
 * own so the ordering is a thing a test can prove rather than a comment a reader has to trust.
 */
int RoboTextVerticalCodePointIsRotated(RoboTextCodePoint codePoint);

/** Non-zero when @p codePoint is whitespace, which takes its neighbours' orientation. */
int RoboTextVerticalCodePointIsWhitespace(RoboTextCodePoint codePoint);

/**
 * The orientation of one character, by its own code point and script.
 *
 * The per-character answer, and `requested` overrides it: `Upright`/`Sideways` return themselves, and
 * `Mixed` asks three tables in order — the **rotated** one first (halfwidth Katakana lies down although its
 * script stands up), then the script (an ideograph stands up whatever its code point), then the code-point
 * ranges (a CJK full stop stands up although its script is `Common`).
 *
 * This is the door a caller uses when it has a character and wants the honest per-character answer. The
 * **contextual** door beside it is what segmentation uses, because whitespace needs a neighbour.
 */
RoboTextGlyphOrientation RoboTextVerticalOrientationOf(RoboTextCodePoint codePoint,
                                                     RoboTextScript script,
                                                     RoboTextTextOrientation requested);

/**
 * The orientation of a character **in context**: the door the run set cuts on.
 *
 * Identical to @ref RoboTextVerticalOrientationOf for every character except whitespace, which takes
 * `preceding` — the orientation of the scalars before it in the same coherent stretch. A space between
 * two ideographs therefore stays upright rather than opening a one-scalar sideways run, which is what
 * CSS Writing Modes specifies for `text-orientation: mixed` and what every reader of a Japanese column
 * expects. `preceding` at the start of a paragraph is @ref RoboTextGlyphOrientationUpright.
 */
RoboTextGlyphOrientation RoboTextVerticalOrientationInContext(RoboTextCodePoint codePoint,
                                                             RoboTextScript script,
                                                             RoboTextGlyphOrientation preceding,
                                                             RoboTextTextOrientation requested);

/* ------------------------------------------------------------------ */
/* The OpenType hook                                                   */
/* ------------------------------------------------------------------ */

/**
 * Turn @p mode's vertical features on in @p features, leaving everything else alone.
 *
 * The whole of vertical OpenType integration, and it is two tags: `vert` and `vrt2`. There is no
 * vertical shaping engine, no vertical substitution table and no second GSUB pass — a font that carries
 * a vertical alternates feature gets it applied through 12.10's engine like any other feature, which is
 * what the phase means by "use the existing GSUB infrastructure".
 *
 * **Both tags**, because they are two spellings of one job: `vert` is the original and `vrt2` its
 * modern replacement, faces carry one or the other, and enabling both lets the font's own FeatureList
 * decide which exists. A feature a face does not carry resolves to nothing, so naming both is not
 * "enabling two features" in any way a caller can observe.
 *
 * Horizontal mode writes nothing: a horizontal run's features are the caller's, untouched.
 *
 * @param features receives the settings; tags already named by the caller are **left alone** (the same
 *                 "specific beats default" rule the ligature and kerning policies follow).
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an unknown mode.
 */
Result RoboTextWritingModeApplyFeatures(RoboTextWritingMode mode, RoboTextFeatureSet *features);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/**
 * A one-line dump of a writing setting: `"vertical-rl mixed"`, `"horizontal-tb mixed"`.
 *
 * Complete or absent, like every dump in this module: a setting that does not fit @p capacity leaves the
 * buffer as the empty string, and @p outLength receives the length a complete line needs either way.
 *
 * @return RoboOk and @p outLength; RoboInvalidArgument for a NULL argument; RoboNotSupported when
 *         @p buffer cannot hold the line.
 */
Result RoboTextWritingModeWrite(RoboTextWritingMode mode,
                                RoboTextTextOrientation orientation,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_VERTICAL_H */
