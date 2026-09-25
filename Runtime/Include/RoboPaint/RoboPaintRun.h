/*
 * The shaped text run, shared by every painter in the tree (Phase 12.24).
 *
 * ## Why this file exists
 *
 * 12.16 made the *document* path shape its text while the document is recorded, so a replay walks numbers
 * and no `cmap` is consulted below `RoboPaintContext.c`. The shell already had a text path of its own, and
 * it was the older one: a private UTF-8 decoder, one `cmap` lookup per code point, its own pen, and its own
 * advance sum for a label's width (`RoboDesktopChrome.c` before this phase). That is two implementations of
 * "what does this string look like", which is exactly what `RoboText` was built to remove.
 *
 * These three doors are the seam that removal needed, and they are **new in this phase rather than moved**:
 * the shape conversion below `RoboPaintContext.c`'s private static and the pen inside
 * `RoboPaintDecoration.c`'s private static were the same work written twice, and a third copy was about to be
 * written for the shell. They now live once, here, and both callers come through them.
 *
 * ## What is here, and what is deliberately not
 *
 * **Shaping is a call, not a state.** `PaintRunShape` builds a text view, shapes it once with `RoboText`, and
 * converts the resulting glyph runs into this module's pointer-free `PaintGlyph` records — the record a
 * display list can carry and a rasterizer can draw. It owns the one mapping from `RoboTextGlyph` to
 * `PaintGlyph` (the source range made relative to the run, the missing flag decided while the text layer's
 * answer still exists, the ligature and mark flags, the sideways flag from the run's own orientation).
 *
 * **The pen is a pen.** `PaintRunDraw` moves a two-axis pen by the glyphs' own `advanceX`/`advanceY` and
 * branches on nothing: a vertical run's pair *is* the writing mode, already resolved in RoboText, so this
 * loop cannot learn a script, a code point or a writing mode. It skips a control glyph without moving the
 * pen, draws a sideways glyph turned by exactly one quarter turn, and counts the misses it was told about
 * rather than looking any up.
 *
 * **What is not here.** No `cmap`, no UTF-8 decoder, no font parser, no fallback, no bidi, no measurement
 * engine: those are `RoboFont`, `RoboUnicode` and `RoboText`'s, and a caller that needs one calls it. This
 * file exists to be the *only* place between shaping and a rasterized glyph.
 *
 * ## Ownership
 *
 * `PaintRunShape` hands back a **freshly allocated** array the caller owns and releases with `RoboFree`, or
 * `NULL` with a count of zero when there was nothing to shape — an empty string, or no face. That is why it
 * returns a pointer rather than filling the caller's buffer: the glyph count is only known after shaping, and
 * a document with two hundred runs of one face should not shape twice to learn how much room it needs.
 *
 * References (`face`, `text`, `glyphs`) are borrowed for the duration of the call and are never retained.
 *
 * @see Documentation/Runtime/Text/Overview.md — where this module sits in the frozen pipeline.
 */
#ifndef ROBO_PAINT_RUN_H
#define ROBO_PAINT_RUN_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboGraphics/RoboGraphicsText.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintText.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The font setting a run is shaped at, as a caller states it.
 *
 * Deliberately the *document-independent* half of a style: a size, a weight, a slope and the paragraph's own
 * base direction — the four facts shaping needs. A caller with a `LayoutTextStyle` or a `RoboTextFontStyle`
 * passes those numbers; a caller with neither (the shell) passes its own. There is no fifth field, and in
 * particular no family: **which face** is a separate argument, because choosing it is font selection and
 * happens above this module.
 *
 * `weight` is clamped into the nine-step scale here, the same way the run record's own reader narrows it: a
 * `font-weight` CSS allows but `RoboText` cannot name is drawn at the nearest weight it can rather than
 * costing the caller its text.
 */
typedef struct PaintRunStyle {
    /** The size in pixels. A run at a size this build cannot draw shapes nothing and says so. */
    float sizePx;
    /** 100..900; clamped into the scale. */
    int32_t weight;
    /** Non-zero for an italic setting. */
    int italic;
    /** Non-zero when the paragraph's base direction is right-to-left. */
    int rtl;
} PaintRunStyle;

/** An initial style: `RoboText`'s own defaults at zero size. */
void PaintRunStyleInit(PaintRunStyle *style);

/** What a draw did, by count. All three are outputs of one call, not accumulators. */
typedef struct PaintRunDrawStats {
    /** Glyphs drawn. A control glyph is not among them. */
    size_t drawn;
    /** Glyphs whose face had no usable glyph — `PaintGlyphFlagMissing`, carried from shaping. */
    size_t missing;
    /** Control glyphs skipped: a bidi formatting character draws nothing and moves nothing. */
    size_t controls;
} PaintRunDrawStats;

/**
 * Shape @p text (UTF-8, @p length bytes) with @p face at @p style into a fresh glyph array.
 *
 * @param outGlyphs non-NULL; receives the glyphs, or NULL when nothing was shaped.
 * @param outCount  non-NULL; receives the glyph count.
 * @param outWidth  may be NULL; receives the measured advance in pixels, from the shaping call's **own**
 *                  measurement (12.15), so a width read here and a width read from the glyph array are the
 *                  same number rather than two sums.
 *
 * A NULL @p face, an empty @p text and a style this build cannot draw are all **answered**: no glyphs, count
 * zero, width `0.0f`, `RoboOk`. They are states of a caller, not failures of this module — a document built
 * with no face keeps its characters and its empty glyph span (`RoboPaintText.h`).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p outGlyphs or @p outCount, or @p text NULL with a
 *         non-zero @p length; RoboOutOfMemory; RoboError when the face's own tables will not parse — the one
 *         answer that must not be read as "nothing to draw".
 */
Result PaintRunShape(FontFace *face,
                     const char *text,
                     size_t length,
                     const PaintRunStyle *style,
                     PaintGlyph **outGlyphs,
                     size_t *outCount,
                     float *outWidth,
                     PaintDiag *diag);

/**
 * `PaintRunShape` over scalars a caller already has.
 *
 * The document path decodes a node's bytes once, through `RoboUnicode`, and stores the code points in the
 * paint list's pool (`RoboPaintContext.c`); re-encoding them only to decode them again would be one round trip
 * this door exists to skip. The conversion and the shaping are the same call — there is one implementation and
 * the byte entry point is written on top of it.
 */
Result PaintRunShapeScalars(FontFace *face,
                            const uint32_t *codePoints,
                            size_t count,
                            const PaintRunStyle *style,
                            PaintGlyph **outGlyphs,
                            size_t *outCount,
                            float *outWidth,
                            PaintDiag *diag);

/**
 * Measure @p text at @p style without keeping the glyphs.
 *
 * The door a caller wants when all it needs is a width — a label's centring, a hit test, a reserved band.
 * It runs the **same** pipeline as `PaintRunShape` (the number is the shaping measurement, not a second
 * advance sum) and simply does not hand back the array.
 *
 * @return the same answers as `PaintRunShape`, with @p outWidth written for a NULL face and empty text too.
 */
Result PaintRunMeasure(FontFace *face,
                       const char *text,
                       size_t length,
                       const PaintRunStyle *style,
                       float *outWidth,
                       PaintDiag *diag);

/**
 * Draw @p count shaped glyphs into @p raster, the pen starting at (@p x, @p baselineY).
 *
 * The one pen. Its whole arithmetic is the two-axis accumulation the writing-mode work left behind:
 *
 *   - a control glyph is skipped and moves nothing;
 *   - each drawn glyph is placed at `(x + offsetX, baselineY + offsetY)` and then the pen moves by
 *     `(advanceX, advanceY)` — so a horizontal run's `advanceY` of zero makes this bit-for-bit the
 *     single-axis pen it replaced, and a column descends because the shaper said so;
 *   - a glyph carrying `PaintGlyphFlagSideways` is drawn turned one quarter turn clockwise, the angle
 *     `RoboTextGlyphOrientationRotation` states and CSS's `text-orientation: sideways` means.
 *
 * Coverage antialiasing (`RoboGfxGlyphModeAlpha`) is selected **here**, and it is the same argument
 * `RoboPaintDecoration.c` makes one stage along: this is the only thing in the tree that knows it is drawing
 * text rather than auditing an outline, so it is the only place that can say so. `OUTLINE_STROKE` traces a
 * glyph with one-pixel lines and is not what text should look like.
 *
 * @param out may be NULL; the three counts of what happened.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p raster, a NULL @p face, @p glyphs NULL with a non-zero
 *         @p count, or a @p sizePx this build cannot draw; whatever a glyph draw returned when it failed.
 */
Result PaintRunDraw(GfxRaster *raster,
                    FontFace *face,
                    float sizePx,
                    const PaintGlyph *glyphs,
                    size_t count,
                    float x,
                    float baselineY,
                    GfxColorRgba8 color,
                    PaintRunDrawStats *out,
                    PaintDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_RUN_H */
