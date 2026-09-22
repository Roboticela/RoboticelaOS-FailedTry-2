/*
 * RoboText's font context (Phase 12.4) — a borrowed face plus the numbers a run is set at, and the one
 * door every device-space number in this module comes through.
 *
 * ## Why a context rather than a face
 *
 * Every question this module asks a font is a question about a *pair*: "how wide is glyph 41" is
 * meaningless without "at what size", and "what is the ascent" without a size is a number in font units
 * that no geometry can use. So the unit the text layer passes around is the pair, and it is a **value**:
 * a context owns nothing, holds a borrowed `FontFace`, and can therefore be copied into a run, compared
 * field by field, and dropped without a lifetime question. Who owns the face — 12.2's pack, 12.3's
 * resolver, a test's fixture — is deliberately not this header's business, which is what keeps the
 * arrow from the text layer to the typography layer from existing at all.
 *
 * ## One scaling door
 *
 * `RoboTextFontUnitsToDevice` is the only place in this module where a font unit becomes a pixel. That
 * is not tidiness: the pen the painter accumulates and the width the measurer reports have to agree, and
 * the way they disagree is two multiplications written slightly differently. The arithmetic is
 * `(value * sizePx) / unitsPerEm` accumulated in `double`, and the *result* is a `float` — which is the
 * same expression RoboGraphics' own device-metrics bridge uses, so a glyph's advance here and the same
 * glyph's advance one layer down are the same number rather than two roundings of one.
 *
 * ## What is not here
 *
 * No loading, no parsing policy, no fallback selection, no variable-axis instancing. A face that is not
 * attached is a `RoboNotSupported` from the door that needed it rather than a silent zero, because a
 * zero advance is a *plausible* number and this layer's whole contract is that it does not produce
 * plausible numbers it does not have.
 */
#ifndef ROBO_TEXT_FONT_H
#define ROBO_TEXT_FONT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A font *context*: the face a run is drawn with and the numbers it is set at.
 *
 * Borrowed, so a context is valid for exactly as long as the face it names. `style.sizePx` is the
 * authoritative size: a face has no size of its own, and every device number this module produces is a
 * function of this field.
 */
typedef struct RoboTextFontContext {
    /** Borrowed face; NULL means "no font", which is a state this build supports and reports. */
    FontFace *face;
    /** The setting the face is used at. */
    RoboTextFontStyle style;
} RoboTextFontContext;

/**
 * The face's layout metrics, in device pixels at the context's own size.
 *
 * Every field is a **positive magnitude** except `lineHeightPx`, which is the sum the caller places a
 * line box with. Normalising here rather than at each use is what makes `ascent + descent + gap ==
 * lineHeight` true by construction: a face in the wild may carry its descent either sign, and a caller
 * that had to know which would be reading a table rather than a metric.
 */
typedef struct RoboTextFontMetrics {
    /** The face's em square, from `head`. */
    uint16_t unitsPerEm;
    /** Non-zero when the metrics came from the face; 0 when a fallback estimate was used. */
    int fromFace;
    /** Above the baseline, in px. */
    float ascentPx;
    /** Below the baseline, in px, as a magnitude. */
    float descentPx;
    /** The face's own line gap, in px; never negative. */
    float lineGapPx;
    /**
     * The leading a line box uses: the context's own `lineHeightPx` when it declares one, else
     * `ascent + descent + gap`.
     */
    float lineHeightPx;
    /**
     * The face's cap height, in px, or 0 when the face does not declare one (`OS/2` absent or v1).
     *
     * A **declared** zero is impossible — cap height is a positive design measurement — so zero is the
     * honest "not available", exactly as `FontOptionalUnit` spells it one layer down, and a caller that
     * centres a line on its caps can fall back to the ascent rather than centring on nothing.
     */
    float capHeightPx;
    /** See @ref capHeightPx: the face's x-height in px, or 0 when it declares none. */
    float xHeightPx;
} RoboTextFontMetrics;

/** Clear @p context to "no face, CSS initial setting". Safe on NULL. */
void RoboTextFontContextInit(RoboTextFontContext *context);

/**
 * Bind @p face at @p style.
 *
 * @p style is copied. @p face may be NULL, which is a *supported* context — every door that needs a
 * face reports `RoboNotSupported` rather than crashing, and the run set still segments (a run's script
 * and direction are properties of the text, not of the font).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p context or a @p style that is not a setting this
 *         build can draw (`RoboTextFontStyleIsValid`).
 */
Result RoboTextFontContextSet(RoboTextFontContext *context,
                              FontFace *face,
                              const RoboTextFontStyle *style);

/** Non-zero when @p context names a face and a valid setting. */
int RoboTextFontContextIsUsable(const RoboTextFontContext *context);

/** The borrowed face, or NULL. */
FontFace *RoboTextFontContextFace(const RoboTextFontContext *context);

/** The setting, by value. An initial one for a NULL context. */
RoboTextFontStyle RoboTextFontContextStyle(const RoboTextFontContext *context);

/** Field-for-field equality: the same face object at an equal setting. Safe on NULL. */
int RoboTextFontContextEqual(const RoboTextFontContext *a, const RoboTextFontContext *b);

/**
 * Lexicographic order, for the determinism checks a test and a run-set walk need: face by address, then
 * the setting field by field. Never returns 0 for two different contexts.
 */
int RoboTextFontContextCompare(const RoboTextFontContext *a, const RoboTextFontContext *b);

/* ------------------------------------------------------------------ */
/* Device space                                                        */
/* ------------------------------------------------------------------ */

/**
 * Scale a value in font units to device pixels at @p context's own size.
 *
 * The module's **one** scaling door (see the header). @p value is a `double` so that a caller summing
 * in font units can pass the sum, and the answer is a `float` because every geometry above this layer
 * is.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument, a context with no face, a face with no
 *         usable `upem`, or a size that is not finite and positive; RoboNotSupported when the face's
 *         metrics will not parse.
 */
Result RoboTextFontUnitsToDevice(const RoboTextFontContext *context,
                                 double value,
                                 float *outPx,
                                 RoboTextDiag *diag);

/**
 * The face's layout metrics at the context's size.
 *
 * A face with no usable metrics yields `fromFace == 0` and a documented estimate — ascent = size,
 * descent = 0, gap = 0 — which is the CSS `normal` reduction this tree already makes for the same
 * absence in `RoboLayoutTextFont.c`, and *not* zero, which would collapse every line.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when there is no face.
 */
Result RoboTextFontContextMetrics(const RoboTextFontContext *context,
                                  RoboTextFontMetrics *out,
                                  RoboTextDiag *diag);

/**
 * The glyph a scalar maps to.
 *
 * The cmap door, wrapped rather than reimplemented: the parse happens once per face
 * (`FontFaceParseCmap` is lazy and idempotent) and the *status* is reported rather than folded into an
 * id, because "the face has no glyph for this scalar" and "the face has glyph 0" are different facts and
 * a caller that conflated them would draw `.notdef` for text that had a glyph.
 *
 * @return RoboOk with @p outGlyphId and @p outMapped set (the map may still be absent, in which case
 *         `outMapped` is 0 and `outGlyphId` is `RoboGlyphIdNone`); RoboInvalidArgument for a NULL
 *         argument; RoboNotSupported with no face; RoboError when the cmap will not parse.
 */
Result RoboTextFontLookupGlyph(const RoboTextFontContext *context,
                               Uchar32 scalar,
                               GlyphId *outGlyphId,
                               int *outMapped,
                               RoboTextDiag *diag);

/**
 * A glyph's advance in device pixels.
 *
 * The status matters and is reported: `RoboFontGlyphMetricsInvalidId`, `…Unavailable` (no `hmtx`) and
 * `…Malformed` are three different reasons a glyph has no advance, and *none* of them is zero. A caller
 * that treated them as a measurement of zero would make a broken font produce a narrower run than a
 * working one, which reads as a layout bug rather than as a missing table.
 *
 * @return RoboOk with @p outAdvancePx and @p outStatus filled; RoboInvalidArgument for a NULL argument;
 *         RoboNotSupported with no face; RoboError when the metrics will not parse.
 */
Result RoboTextFontGlyphAdvance(const RoboTextFontContext *context,
                                GlyphId glyphId,
                                float *outAdvancePx,
                                FontGlyphMetricsStatus *outStatus,
                                RoboTextDiag *diag);

/**
 * A glyph's full advance record, in font units and in device pixels.
 *
 * The door `RoboTextGlyphs.c` uses: it wants the font-unit advance (for the record and for a caller
 * summing in font units) *and* the device advance (for the pen), and asking twice would scale twice.
 *
 * @return RoboOk with @p outMetrics filled; RoboInvalidArgument for a NULL argument; RoboNotSupported
 *         with no face; RoboError when the metrics table **will not parse**. A face that carries no
 *         metrics information at all is *not* an error: the record comes back with status
 *         `RoboFontGlyphMetricsUnavailable` and a zero advance, which is the same answer a glyph
 *         with no advance gets from a face that has one.
 */
Result RoboTextFontGlyphMetrics(const RoboTextFontContext *context,
                                GlyphId glyphId,
                                FontGlyphMetrics *outMetrics,
                                float *outAdvancePx,
                                RoboTextDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_FONT_H */
