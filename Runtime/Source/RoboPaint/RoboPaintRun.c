/*
 * The shaped text run (Phase 12.24) — see `RoboPaintRun.h` for the contract.
 *
 * The file is three pieces, in the order a painter meets them:
 *
 *   **the style**   a caller's four numbers become the text layer's own font context, once, so the size a
 *                   label is measured at and the size it is drawn at cannot disagree.
 *   **the shape**   a view (or a caller's own scalars) is shaped by RoboText and its glyph runs are
 *                   converted into this module's pointer-free `PaintGlyph` records. One conversion, and it
 *                   is the one that decides the three facts only shaping knows: whether a scalar was mapped,
 *                   how many scalars a glyph covers, and whether the run lies on its side.
 *   **the pen**     a two-axis accumulation over those records, branching on nothing.
 *
 * The conversion is 12.16's, lifted out of `RoboPaintContext.c`'s private static unchanged; the pen is
 * 12.20's, lifted out of `RoboPaintDecoration.c`'s private static unchanged. Both were moved rather than
 * rewritten, which is why the two callers' records are bit-for-bit what they were.
 */

#include "RoboPaint/RoboPaintRun.h"

#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsText.h"
#include "RoboMem/RoboMem.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboText/RoboText.h"

/**
 * The text layer's font context for @p face at @p style.
 *
 * @p style's weight is clamped into the nine-step scale, deliberately: `PaintRunStyle` carries the number CSS
 * allows and this is the one place it becomes the number `RoboText` can name. A NULL @p style is the initial
 * one, so a caller that has nothing to say gets `RoboText`'s own defaults.
 *
 * @return non-zero when @p out is usable — a face at a setting this build can draw.
 */
static int PaintRunFontContext(RoboTextFontContext *out,
                               FontFace *face,
                               const PaintRunStyle *style)
{
    PaintRunStyle initial;
    RoboTextFontStyle textStyle;

    if (!style) {
        PaintRunStyleInit(&initial);
        style = &initial;
    }

    RoboTextFontStyleInit(&textStyle);
    textStyle.sizePx = style->sizePx;
    textStyle.weight = (FontWeight)(style->weight < 100 ? 100
                                       : style->weight > 900 ? 900
                                                             : style->weight);
    textStyle.style = style->italic ? RoboFontStyleItalic : RoboFontStyleNormal;

    RoboTextFontContextInit(out);
    if (RoboTextFontContextSet(out, face, &textStyle) != RoboOk) {
        /* A setting this build cannot draw: the context is left unusable rather than half set, and the
         * caller's own answer (`PaintRunShape`'s "nothing to draw") is what a caller sees. */
        return 0;
    }
    return RoboTextFontContextIsUsable(out);
}

/** The shape options a run is shaped under: the paragraph's base direction and nothing else. */
static void PaintRunOptions(RoboTextShapeOptions *options, const PaintRunStyle *style)
{
    RoboTextShapeOptionsInit(options);
    options->baseDirection = (style && style->rtl) ? RoboTextDirectionRtl : RoboTextDirectionLtr;
}

/** Convert @p result's glyph runs into a fresh `PaintGlyph` array. */
static Result PaintRunConvert(const RoboTextShapeResult *result,
                              size_t capacity,
                              PaintGlyph *glyphs,
                              size_t *outWritten,
                              PaintDiag *diag)
{
    size_t runCount = RoboTextShapeResultGlyphRunCount(result);
    size_t written = 0u;
    size_t runIndex;

    for (runIndex = 0u; runIndex < runCount && written < capacity; runIndex++) {
        const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, runIndex);
        RoboTextRange source;
        size_t count;
        size_t index;
        int sideways;

        if (!run) {
            continue;
        }
        (void)diag;
        source = RoboTextGlyphRunSourceRange(run);
        count = RoboTextGlyphRunCount(run);
        /*
         * Whether this run's glyphs lie on their side, **asked of the text layer** rather than decided here
         * (Phase 12.20): `RoboTextGlyphRunOrientation` is the resolved answer for the run — `sideways` for
         * the scripts that lie down in a vertical column, `upright` for the ideographs and for every
         * horizontal run — and it travels to each glyph of the run as the flag the pen reads. One answer per
         * run, one copy per glyph, and no code point is inspected anywhere in this file.
         */
        sideways = RoboTextGlyphOrientationIsUpright(RoboTextGlyphRunOrientation(run)) ? 0 : 1;
        for (index = 0u; index < count && written < capacity; index++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, index);
            PaintGlyph *out = &glyphs[written];
            RoboTextRange glyphSource;

            if (!glyph) {
                continue;
            }
            glyphSource = glyph->source;
            PaintGlyphInit(out);
            out->glyphId = glyph->control ? RoboGlyphIdNone : (uint16_t)glyph->glyphId;
            out->cluster = (uint32_t)glyph->cluster;
            /*
             * The source, **relative to the run's own span** — never document offsets: the record's span sits
             * beside it, so an absolute range is the sum of the two and a relative one is the only range that
             * stays right if the pool is ever renumbered (`RoboPaintText.h` states the rule). A glyph whose
             * source starts before its run's is a text layer bug rather than something to saturate, and it
             * cannot happen: a run's glyphs come from its own scalars.
             */
            out->sourceFirst = glyphSource.start >= source.start
                                        ? (uint32_t)(glyphSource.start - source.start)
                                        : 0u;
            out->sourceCount = glyphSource.end > glyphSource.start
                                        ? (uint32_t)(glyphSource.end - glyphSource.start)
                                        : 0u;
            out->advanceX = glyph->advanceX;
            out->advanceY = glyph->advanceY;
            out->offsetX = glyph->offsetX;
            out->offsetY = glyph->offsetY;
            if (glyph->control) {
                out->flags |= (uint32_t)PaintGlyphFlagControl;
            }
            if (glyph->ligatureComponents >= 2u) {
                out->flags |= (uint32_t)PaintGlyphFlagLigature;
            }
            if (glyph->flags & RoboTextGlyphFlagMark) {
                out->flags |= (uint32_t)PaintGlyphFlagMark;
            }
            if (sideways) {
                out->flags |= (uint32_t)PaintGlyphFlagSideways;
            }
            /*
             * The miss, decided **here** because this is where the answer exists: "the face had no glyph for
             * these scalars" is RoboText's `mapped`/`metricsStatus`, and after this call nothing in the
             * pipeline can ask again — there is no cmap, no metrics table and no text left, only ids. A
             * control is not a miss: it is not a character the face failed to cover.
             */
            if (!glyph->control &&
                (!glyph->mapped || glyph->metricsStatus != RoboFontGlyphMetricsOk)) {
                out->flags |= (uint32_t)PaintGlyphFlagMissing;
            }
            written++;
        }
    }
    *outWritten = written;
    return RoboOk;
}

/** Shape @p view: the one implementation the two entry points and the measurer share. */
static Result PaintRunShapeView(FontFace *face,
                                RoboTextView view,
                                const PaintRunStyle *style,
                                int keepGlyphs,
                                PaintGlyph **outGlyphs,
                                size_t *outCount,
                                float *outWidth,
                                PaintDiag *diag)
{
    RoboTextShapeOptions options;
    RoboTextShapeResult *result = NULL;
    RoboTextFontContext font;
    PaintGlyph *glyphs = NULL;
    const RoboTextMeasurement *measurement;
    size_t glyphCount = 0u;
    size_t runCount;
    size_t runIndex;
    size_t written = 0u;
    Result status;

    if (outGlyphs) {
        *outGlyphs = NULL;
    }
    if (outCount) {
        *outCount = 0u;
    }
    if (outWidth) {
        *outWidth = 0.0f;
    }

    /*
     * No face and no text are **answered**, not refused: a document built with no face keeps its characters
     * and records an empty glyph span (`RoboPaintText.h`), and the shell counts a label it could not shape
     * rather than failing its frame. Both are callers' states.
     */
    if (!face || view.length == 0u || !view.bytes) {
        return RoboOk;
    }

    if (!PaintRunFontContext(&font, face, style)) {
        return RoboOk;
    }
    PaintRunOptions(&options, style);
    status = RoboTextShape(view, &font, &options, &result, NULL);
    if (status != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, status,
                               "a run's text could not be shaped");
    }

    measurement = RoboTextShapeResultMeasurement(result);
    if (outWidth && measurement) {
        *outWidth = measurement->width;
    }

    if (keepGlyphs && outGlyphs && outCount) {
        /* One allocation for the whole run, sized by counting the runs' glyphs first — because the array is
         * one contiguous run and appending glyph-by-glyph would be the same count computed twice. */
        runCount = RoboTextShapeResultGlyphRunCount(result);
        for (runIndex = 0u; runIndex < runCount; runIndex++) {
            const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, runIndex);

            if (run) {
                glyphCount += RoboTextGlyphRunCount(run);
            }
        }
        if (glyphCount > 0u) {
            glyphs = (PaintGlyph *)RoboCalloc(glyphCount, sizeof(*glyphs));
            if (!glyphs) {
                RoboTextShapeResultDestroy(result);
                return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboOutOfMemory,
                                       "could not hold a run's glyphs");
            }
            (void)PaintRunConvert(result, glyphCount, glyphs, &written, diag);
        }
    }

    RoboTextShapeResultDestroy(result);

    if (written == 0u) {
        /* A run the text layer produced no glyph for at all: not a failure, and not a face either — the
         * record keeps its characters and its empty "was not shaped" state. */
        RoboFree(glyphs);
        return RoboOk;
    }
    *outGlyphs = glyphs;
    *outCount = written;
    return RoboOk;
}

void PaintRunStyleInit(PaintRunStyle *style)
{
    if (!style) {
        return;
    }
    style->sizePx = 0.0f;
    style->weight = (int32_t)RoboFontWeightNormal;
    style->italic = 0;
    style->rtl = 0;
}

Result PaintRunShape(FontFace *face,
                     const char *text,
                     size_t length,
                     const PaintRunStyle *style,
                     PaintGlyph **outGlyphs,
                     size_t *outCount,
                     float *outWidth,
                     PaintDiag *diag)
{
    if (!outGlyphs || !outCount) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "shaping a run needs somewhere to put its glyphs");
    }
    if (!text && length > 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "a run's bytes may not be NULL with a length");
    }
    return PaintRunShapeView(face, RoboTextViewMake(text, length), style, 1u,
                             outGlyphs, outCount, outWidth, diag);
}

Result PaintRunShapeScalars(FontFace *face,
                            const uint32_t *codePoints,
                            size_t count,
                            const PaintRunStyle *style,
                            PaintGlyph **outGlyphs,
                            size_t *outCount,
                            float *outWidth,
                            PaintDiag *diag)
{
    RoboTextBuffer *buffer = NULL;
    RoboTextView view;
    Result status;

    if (!outGlyphs || !outCount) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "shaping a run needs somewhere to put its glyphs");
    }
    if (!codePoints && count > 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "a run's characters may not be NULL with a count");
    }
    if (count == 0u || !face) {
        *outGlyphs = NULL;
        *outCount = 0u;
        if (outWidth) {
            *outWidth = 0.0f;
        }
        return RoboOk;
    }

    /*
     * The characters the caller already holds become a buffer **through the text layer's own encoder**, so
     * this path decodes nothing and the two entry points hand the shaper the same kind of view. A scalar that
     * is not a Unicode scalar is refused by that door rather than replaced, which is the answer a caller
     * holding code points deserves — the paint list's pool came from a decode, so this is a check rather than
     * a recovery.
     */
    status = RoboTextBufferCreateFromScalars(&buffer, (const Uchar32 *)codePoints, count, NULL);
    if (status != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, status,
                               "a run's characters are not text this build can shape");
    }
    view = RoboTextViewMake(RoboTextBufferBytes(buffer), (size_t)RoboTextBufferByteLength(buffer));
    status = PaintRunShapeView(face, view, style, 1u, outGlyphs, outCount, outWidth, diag);
    RoboTextBufferDestroy(buffer);
    return status;
}

Result PaintRunMeasure(FontFace *face,
                       const char *text,
                       size_t length,
                       const PaintRunStyle *style,
                       float *outWidth,
                       PaintDiag *diag)
{
    if (!outWidth) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "measuring a run needs somewhere to put its width");
    }
    if (!text && length > 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "a run's bytes may not be NULL with a length");
    }
    return PaintRunShapeView(face, RoboTextViewMake(text, length), style, 0u,
                             NULL, NULL, outWidth, diag);
}

Result PaintRunDraw(GfxRaster *raster,
                    FontFace *face,
                    float sizePx,
                    const PaintGlyph *glyphs,
                    size_t count,
                    float x,
                    float baselineY,
                    GfxColorRgba8 color,
                    PaintRunDrawStats *out,
                    PaintDiag *diag)
{
    GfxGlyphDraw draw;
    float penX;
    float penY;
    size_t index;

    if (out) {
        out->drawn = 0u;
        out->missing = 0u;
        out->controls = 0u;
    }
    if (!raster || !face) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "drawing a run needs a raster and a face");
    }
    if (!glyphs && count > 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "a run's glyphs may not be NULL with a count");
    }
    if (count == 0u) {
        /* An empty run is a legal record and draws nothing; the caller counts it as a run that produced no
         * glyph rather than as a failure. */
        return RoboOk;
    }
    if (!(sizePx > 0.0f)) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "a run's size is not a setting this build can draw");
    }

    /*
     * The pen is a **2D** accumulation and a **2D** baseline: `penY` starts at zero and every step adds the
     * glyph's own pair, so a horizontal run (`advanceY == 0`) descends by nothing and this is bit-for-bit the
     * single-axis pen the document pass used before vertical writing existed, while a column descends because
     * the shaper's own advances say it does. There is no branch on a writing mode, and that is the point: a
     * run's advance pair *is* the writing mode, already resolved.
     */
    penX = x;
    penY = 0.0f;
    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.sizePx = sizePx;
    draw.color = color;
    /*
     * **Coverage antialiasing**, named here rather than inherited: `GfxGlyphDrawInit` leaves the mode at
     * `OUTLINE_STROKE`, which traces a glyph's contour with one-pixel lines and is not what text should look
     * like. `ALPHA` is the same glyph rasterized into a grayscale coverage mask and blended, and it is
     * selected **here** because this is the only thing in the tree that knows it is drawing UI text rather
     * than auditing an outline. A caller that wants the outline path calls `GfxRasterDrawGlyph` itself.
     */
    draw.mode = RoboGfxGlyphModeAlpha;

    for (index = 0u; index < count; index++) {
        const PaintGlyph *glyph = &glyphs[index];

        /*
         * A bidi formatting control is not drawn and does not move the pen (Phase 12.8). It is a glyph all
         * the same, so the source mapping stays one-per-scalar — and because `.notdef` is id 0 as well, this
         * is the `control` flag and **not** "the id is zero": an unmapped character is drawn as a box and
         * advanced over, which is what `.notdef`'s advance is for.
         */
        if (PaintGlyphIsControl(glyph)) {
            if (out) {
                out->controls++;
            }
            continue;
        }
        draw.glyphId = glyph->glyphId;
        draw.origin = MakeGfxPointF(penX + glyph->offsetX, baselineY + penY + glyph->offsetY);
        /*
         * **A sideways glyph is drawn turned**, by exactly one quarter turn clockwise — the angle
         * `RoboTextGlyphOrientationRotation(RoboTextGlyphOrientationSideways)` states and the one CSS's
         * `text-orientation: sideways` means. The two are the same quarter turn by construction
         * (`RoboTextVertical.c`), and the pen still advances by the same pair either way: the advance is the
         * shaper's answer about the **pen** and the rotation is its answer about the **ink**, which is why
         * neither is derived from the other here.
         */
        draw.rotation = PaintGlyphIsSideways(glyph)
                            ? (uint8_t)RoboGfxGlyphRotationClockwise90
                            : (uint8_t)RoboGfxGlyphRotationNone;
        if (GfxRasterDrawGlyph(raster, &draw) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                                   "a glyph could not be drawn");
        }
        if (out) {
            out->drawn++;
            if (glyph->flags & (uint32_t)PaintGlyphFlagMissing) {
                out->missing++;
            }
        }
        penX += glyph->advanceX;
        penY += glyph->advanceY;
    }

    return RoboOk;
}
