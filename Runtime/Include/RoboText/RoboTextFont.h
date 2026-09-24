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
#include "RoboFont/RoboFontOt.h"
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

/**
 * A glyph's **vertical** metrics at the context's own size (Phase 12.20).
 *
 * The four numbers a vertical pen needs, and the reason they are a record of their own rather than four
 * more fields on @ref RoboTextFontMetrics: they are per *glyph*, not per face, and a vertical run asks
 * them once per glyph the way a horizontal run asks `hmtx`.
 */
typedef struct RoboTextFontVerticalMetrics {
    /** Non-zero when the face carried `vhea`/`vmtx` and the glyph had an entry. */
    int fromFace;
    /** The face's em square, from `head`; 0 when the metrics could not be read. */
    uint16_t unitsPerEm;
    /** The glyph's `vmtx` advance height, in font units — the pen's step along +y. */
    uint16_t advanceHeightUnits;
    /** The glyph's `vmtx` top side bearing, in font units, signed. Kept for a dump and for the origin. */
    int16_t topSideBearing;
    /** The advance the vertical pen moves after this glyph, in device pixels. */
    float advanceHeightPx;
    /**
     * The glyph's vertical origin, in device pixels, measured **upward from the horizontal baseline**.
     *
     * The line a vertical column is centred on. Derived as the OpenType specification requires for a
     * `glyf` face: `vertOriginY = yMax + topSideBearing`, with `yMax` the glyph's own outline box. A face
     * whose glyph has no outline box leaves this at the `vmtx` top side bearing alone, and
     * @ref originFromOutline says which of the two happened rather than passing a derived number off as a
     * measured one.
     */
    float originYPx;
    /**
     * The glyph's horizontal placement relative to its own horizontal origin, in device pixels: the
     * distance from the glyph's left side bearing to the middle of its horizontal advance.
     *
     * The OpenType specification's `vertOriginX` in the `glyf` case
     * (`x = horiBearingX + horiAdvance / 2`), and what a glyph is translated by so that a vertical
     * column is centred on the glyphs' horizontal advances rather than on their left edges.
     */
    float originXPx;
    /** Non-zero when @ref originYPx came from the glyph's outline box (`glyf`'s `yMax + TSB`). */
    int originFromOutline;
    /**
     * Non-zero when @ref advanceHeightPx was **synthesised** from the face's own line extent because the
     * face carries no `vmtx`.
     *
     * A stated absence rather than a silent one: a CJK face without `vmtx` is shaped with the em box's
     * height as its vertical advance — which is what the specification requires of a conforming font, and
     * is a *fallback* for the fonts that are not — and a caller comparing two faces needs to know which of
     * the two numbers it is holding.
     */
    int advanceSynthesised;
} RoboTextFontVerticalMetrics;

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

/**
 * A glyph's vertical metrics at @p context's size (Phase 12.20).
 *
 * Reads the face's `vmtx`/`vhea` through RoboFont (which parses them once, lazily, like every other table)
 * and derives the vertical origin the way the OpenType specification states for a `glyf` face. **No VORG
 * parsing**: `VORG` is a CFF-face table, RoboFont does not parse it, and a `glyf`-derived origin is the
 * correct answer for the faces this build ships and tests. A CFF face therefore gets the `vmtx` top side
 * bearing as its origin with @ref RoboTextFontVerticalMetrics::originFromOutline clear, which is stated
 * rather than passed off as a measurement.
 *
 * A face with no `vmtx` is **not** refused: the advance is synthesised from the face's line extent and
 * `advanceSynthesised` is set. Vertical text over a face that has no vertical metrics is a supported
 * configuration, exactly as a face with no GSUB is one, and the flag is what keeps it from looking like a
 * measured answer.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument; RoboNotSupported when the context
 *         names no face; RoboError when the face's metrics will not parse at all.
 */
Result RoboTextFontGlyphVerticalMetrics(const RoboTextFontContext *context,
                                       GlyphId glyphId,
                                       RoboTextFontVerticalMetrics *out,
                                       RoboTextDiag *diag);

/**
 * The face's GSUB table, parsed once (Phase 12.10).
 *
 * The font owns the table and this door borrows it: the parse is Phase 5's (`FontGsubParse`, reached
 * through `FontFaceParseOtLayout`), it is cached on the face, and it is immutable afterwards — which is
 * what lets a shaping call read it without owning anything and what makes concurrent runs safe
 * (Phase 12.10 §"thread safety"). RoboText does **not** parse a second copy and must not start: the
 * subtable readers in `RoboTextGsub*.c` interpret offsets *within* this table's own bytes, which is a
 * different job from finding the table in the container.
 *
 * @param out receives the table, or NULL when the face carries none.
 * @return RoboOk (including the "no GSUB" answer, which is not an error — most text never needs one);
 *         RoboInvalidArgument for a NULL argument; RoboNotSupported when no face is bound; RoboError when
 *         the container will not parse.
 */
Result RoboTextFontGsub(const RoboTextFontContext *context,
                        const FontOtLayout **out,
                        RoboTextDiag *diag);

/**
 * The face's GDEF table, parsed once, or NULL when it carries none (Phase 12.10).
 *
 * The lookup-flag machinery needs exactly two things from it: the glyph class definition (so `IgnoreMarks`, `IgnoreBaseGlyphs` and `IgnoreLigatures` can tell a mark from a base) and the mark attachment
 * class definition (so a lookup that filters by `markAttachmentType` can). A face without GDEF still
 * shapes; it simply has no classes to filter by.
 */
Result RoboTextFontGdef(const RoboTextFontContext *context, const FontGdef **out, RoboTextDiag *diag);

/**
 * The face's GPOS table, parsed once (Phase 12.11).
 *
 * The positioning half of @ref RoboTextFontGsub and the same bargain: Phase 5 parses the table
 * (`FontGposParse`, through `FontFaceParseOtLayout`), the face caches it, and it is immutable afterwards, so
 * a shaping call borrows it and concurrent runs are safe. A face with no GPOS is not an error — the caller
 * gets NULL and the glyphs keep the positions their own advances gave them.
 *
 * @param out receives the table, or NULL when the face carries none.
 * @return RoboOk (including the "no GPOS" answer); RoboInvalidArgument for a NULL argument; RoboNotSupported
 *         when no face is bound; RoboError when the container will not parse.
 */
Result RoboTextFontGpos(const RoboTextFontContext *context,
                        const FontOtLayout **out,
                        RoboTextDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_FONT_H */
