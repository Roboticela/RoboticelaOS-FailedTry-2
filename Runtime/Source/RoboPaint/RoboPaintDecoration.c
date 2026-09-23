/*
 * RoboPaint decoration and text painting (Phase 11.4–11.8) — see RoboPaintDecoration.h.
 *
 * The pass: walk the display list once, in list order, and draw each **drawing** command into
 * a RoboGraphics raster -- a background's border box, a border side's band, a text run's
 * glyphs, and an image's destination rectangle -- while counting everything a command turns out to be. The raster is the graphics module's own
 * immediate-mode CPU context, so the clip and the source-over blend are its, and the three
 * things this file owns are the walk, the clip **scope stack**, and the boundary a drawing is
 * applied under.
 *
 * **One walk, not two.** A background-only pass followed by a border-only pass would fill
 * every background in the document before every border in it, which paints a descendant's
 * background *under* its ancestor's border where CSS puts it over -- so the order here is the
 * list's own order, which is the traversal's, which is CSS's.
 *
 * The clip is applied with the raster's own `reset_clip`/`clip`/`set_clip` rather than by
 * intersecting rectangles here, and the reason is that the raster's clip is *already* the one
 * place a draw is bounded: reset to the surface bounds, replace the axis the boundary hides,
 * and let `set_clip` intersect -- so "the boundary bounds the fill" is the graphics module's
 * arithmetic, not a second copy of it.
 *
 * **Since 11.8 that boundary comes from the walk's own clip stack** rather than only from the
 * command's field. A `push-clip` command pushes, a `pop-clip` pops, and a drawing inside a scope
 * is bounded by the scope -- which is the same value its own field carries, because the builder
 * refuses a command inside a scope that disagrees, so the two paths are one number by
 * construction. The command's own field remains the fallback for a list with no clip commands
 * in it (a hand-built one, or one from a document that clips nothing), where it is the only
 * statement of clipping the list carries. The stack is what makes the *scope* the authority for
 * what is drawn inside it, and the counters say how often that happened.
 *
 * Nothing here reads a tree, a computed style or the box model: a display command carries the
 * one rectangle to fill, the boundary in force and, for a band, the style that shapes it. The
 * only conversions left are the colour (`RoboPaintColor.h`'s) and, since 11.6, a character into
 * a glyph -- which is the graphics module's bridge, one call per character.
 *
 * **Since 11.7 an image is the one drawing whose appearance is not a colour**, and the pass holds
 * it to a different rule in exactly one place: the four colour terms below apply to a command
 * whose look *is* a colour, and an image command is instead asked whether its payload is one this
 * build can draw. Everything else is shared with a fill -- the same clip from the walk's stack,
 * the same strength on the raster, the same geometry check -- so there is no second drawing path
 * here, only one more kind of appearance. The sampling, the scaling and the alpha are all
 * `GfxRasterDrawImage`'s, because pixels are RoboGraphics' business and a caller that
 * sampled an image itself would be re-deciding the blend and the clip this module already owns.
 *
 * **Since 11.12 the pass can be bounded.** The same walk takes an optional rectangle and executes
 * the whole list inside it -- every drawing visited, every scope honoured, every strength applied
 * -- with the raster's own clip narrowed by the rectangle as well as by the command's boundary. It
 * is a second door onto *this* code rather than a second pass, and the three functions that make
 * that work are the two arithmetic helpers above (`DecorationApplyBound`, the four-edge
 * intersection, and `DecorationRectReachesBound`, the diagnosis that feeds `boundedOut`) plus
 * the `bound` parameter threaded through `DecorationApplyClip`. The decision to bound with the
 * clip rather than to skip on the rectangle is argued in `RoboPaintDecoration.h`; the frame that
 * drives it is `RoboRender` (`render/docs/Rasterization.md`).
 */

#include "RoboPaint/RoboPaintDecoration.h"

#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboPaint/RoboPaintClip.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsText.h"

#include "RoboCSS/RoboCSSBorder.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboText/RoboText.h"

#include "RoboPaint/RoboPaintColor.h"


#include "RoboPaintInternal.h"

#include <string.h>

void PaintDecorationStatsInit(PaintDecorationStats *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

/**
 * Release the pass's three owned resources and return @p status unchanged.
 *
 * The pass has many ways out -- every failure plus the end -- and the raster and the two scope
 * stacks are created together and must be released together. A helper rather than a pair of
 * calls at every exit, because a cleanup that is written out at every exit is a cleanup that is
 * eventually written out at all but one of them: the leak a missing pair would leave is in a
 * *failure* path, which is exactly the path a test that only paints good documents never walks.
 *
 * Both stacks are released here, and the second one's arrival in 11.9 is why this signature
 * grew rather than why a second helper appeared: the three resources have one end.
 */
static Result DecorationFinish(GfxRaster *raster,
                                       PaintClipStack *clips,
                                       PaintOpacityStack *opacities,
                                       Result status)
{
    PaintOpacityStackDestroy(opacities);
    PaintClipStackDestroy(clips);
    GfxRasterDestroy(raster);
    return status;
}

/** The style a filled band was shaped by is one this build draws as `solid`. */
static int DecorationStyleIsShaped(CssBorderStyle style)
{
    return style == RoboCssBorderStyleSolid;
}

/*
 * Drawing a run (11.6), through RoboText (12.4).
 *
 * This function used to be the tree's *transitional* text path, and it said so: "text run -> the
 * font's cmap, per code point, in logical order -> paint", with `RoboPaintText.h` naming the
 * successor it expected -- "Phase 14: text run -> RoboText shaping -> a glyph run -> paint". That
 * successor arrived early, in 12.4, and this is where the substitution happened:
 *
 *     before   for each code point:  cmap -> glyph id;  hmtx -> glyph 0's fallback;  draw; pen += advance
 *     now      RoboText -> text runs -> glyph runs -> for each glyph: draw at the pen; pen += its advance
 *
 * The loop that remains is four lines and owns **no font knowledge at all**: it does not know what
 * a cmap is, it does not resolve an id, it does not scale an advance, and it cannot disagree with
 * the measurer about any of the three because the measurer runs the same glyph generation
 * (`RoboLayoutTextFont.c`). The one thing left to this layer is the two facts a text layer has no
 * business knowing: where the run's origin is on *this* surface, and that UI text is drawn
 * coverage-antialiased.
 *
 * **Sub-pixel pen, sub-pixel glyph.** The pen is a `float` and, since 12.1, the glyph is *placed*
 * at that fraction rather than snapped to a whole pixel. The glyph carries its own offset (zero in
 * this build -- nothing positions anything yet, see `RoboTextGlyphs.h`) and its own device advance,
 * so the position is accumulated exactly the way RoboText summed it. That is the change that
 * removes the old file's stated limit -- "two implementations of advance sharing one font" -- and
 * what it removes is the *last glyph sitting a fraction of a pixel from the width the line was
 * justified to*.
 *
 * `GfxGlyphSnapPoint`, the graphics module's whole-pixel policy, is still there for a caller that
 * wants it; a *run* does not, because rounding each origin is exactly the drift the accumulation
 * exists to avoid.
 */

/**
 * Draw @p command's run through @p face into @p raster.
 *
 * @param outGlyphs  receives the glyph draws attempted.
 * @param outMissing receives the glyphs drawn as `.notdef` or with unusable metrics -- RoboText's
 *                   own count, which is one per glyph rather than the old path's two for a mapped
 *                   glyph whose metrics table was broken. One glyph, one miss.
 * @return RoboOk; RoboInvalidArgument when the command is not a drawable text command;
 *         RoboNotFound when the command's span names no run in @p pool, which the list's own
 *         append rule makes unreachable and is reported rather than trusted; RoboNotSupported
 *         when the pass has no face to draw with; whatever a glyph draw returned when it failed.
 */
static Result DecorationDrawText(GfxRaster *raster,
                                          FontFace *face,
                                          const PaintTextPool *pool,
                                          const PaintDisplayCommand *command,
                                          GfxColorRgba8 color,
                                          size_t *outGlyphs,
                                          size_t *outMissing,
                                          PaintDiag *diag)
{
    const uint32_t *codePoints;
    GfxGlyphDraw draw;
    PaintTextRun run;
    RoboTextBuffer *buffer = NULL;
    RoboTextShapeResult *result = NULL;
    RoboTextFontContext font;
    RoboTextFontStyle style;
    RoboTextShapeOptions shapeOptions;
    RoboTextView view;
    float penX;
    float baselineY;
    size_t runIndex;
    Result status;

    *outGlyphs = 0u;
    *outMissing = 0u;

    run = PaintDisplayCommandText(command);
    if (!pool || !PaintTextPoolContains(pool, run.span)) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboNotFound,
                               "a text command's span names no run in the list's pool");
    }
    codePoints = PaintTextPoolCodePoints(pool);
    if (!codePoints) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboNotFound,
                               "the list's text pool holds no characters");
    }
    if (!face) {
        /*
         * A face is what turns characters into glyphs, and the pass tolerates a missing one
         * everywhere a *fill* is drawn. For text it cannot: every glyph would be `.notdef`
         * drawn into nothing. The old loop failed at the first glyph draw here; this says the
         * reason once, at the door, before allocating anything.
         */
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboNotSupported,
                               "a text run needs a font face to be drawn with");
    }

    /*
     * The run's characters enter the text layer as the **scalars** the pool already holds, and
     * that is the seam working as designed: the pool stores characters rather than glyph ids
     * (`RoboPaintText.h` argues why), so the text layer encodes them once, hands back glyphs,
     * and the display list stays a statement about the document rather than about one font.
     */
    status = RoboTextBufferCreateFromScalars(&buffer,
                                             codePoints + (size_t)run.span.first,
                                             (size_t)run.span.count, NULL);
    if (status != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, status,
                               "a run's characters are not text this build can draw");
    }

    /*
     * The setting the run was **measured with**, copied rather than re-derived: the measurer's
     * `LayoutTextStyle` and this context are the same four numbers, which is what keeps "the size
     * it was measured at is the size it is drawn at" a copy rather than an agreement to maintain.
     *
     * The weight is clamped into the nine-step scale, and that is a deliberate narrowing rather
     * than a silent repair: `PaintTextRunIsValid` carries a weight CSS allows outside 100..900, and
     * a text context refuses one it cannot name. Refusing the *run* here instead would trade a
     * wrong-looking weight for no text at all, which is a worse answer for a document whose only
     * sin is an unusual `font-weight`.
     */
    RoboTextFontStyleInit(&style);
    style.sizePx = (float)run.font.fontSizePx;
    style.weight = (FontWeight)(run.font.fontWeight < 100 ? 100
                                : run.font.fontWeight > 900 ? 900
                                                            : run.font.fontWeight);
    style.style = run.font.italic ? RoboFontStyleItalic : RoboFontStyleNormal;
    RoboTextFontContextInit(&font);
    if (RoboTextFontContextSet(&font, face, &style) != RoboOk) {
        RoboTextBufferDestroy(buffer);
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "a run's font inputs are not a setting this build can draw");
    }

    view = RoboTextViewMake(RoboTextBufferBytes(buffer),
                            (size_t)RoboTextBufferByteLength(buffer));
    /*
     * The shell's surfaces are declared left-to-right (Phase 12.7 §"default behaviour"), and declared
     * rather than assumed: a document that carries its own `dir` will pass its own context here, and a
     * label whose text is only digits or punctuation ('100%', '—') has no strong scalar to derive a
     * direction from, so leaving the base undeclared would classify it `Unknown` rather than
     * left-to-right. Nothing draws differently today — the glyph order is logical either way, and
     * reordering is 12.8's — but the *declaration* is what makes this run's direction a fact about the
     * surface rather than an accident of its characters.
     */
    RoboTextShapeOptionsInit(&shapeOptions);
    shapeOptions.baseDirection = RoboTextDirectionLtr;
    status = RoboTextShape(view, &font, &shapeOptions, &result, NULL);
    if (status != RoboOk) {
        RoboTextBufferDestroy(buffer);
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, status,
                               "a run's glyphs could not be resolved");
    }

    /* The pen starts at the run rectangle's own left edge and the baseline is the one number
     * the command carries beyond it -- a `TEXT` command's rectangle is the placement, so the
     * baseline is at `rect.y + baselineOffset` in the same space. */
    penX = (float)command->rect.x;
    baselineY = (float)PaintAddSaturated(command->rect.y, run.baselineOffset);
    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.sizePx = (float)run.font.fontSizePx;
    draw.color = color;
    /*
     * **Coverage antialiasing** (Phase 12.1), named here rather than inherited.
     *
     * `GfxGlyphDrawInit` leaves the mode at `OUTLINE_STROKE`, which is the bridge's
     * documented default and not what a document's text should look like: it traces the
     * glyph's contour with one-pixel lines. `ALPHA` is the same glyph rasterized into a
     * grayscale coverage mask and blended, and it is selected **by this pass** because this
     * pass is the only thing in the tree that knows it is drawing UI text rather than
     * auditing an outline.
     */
    draw.mode = RoboGfxGlyphModeAlpha;

    for (runIndex = 0u; runIndex < RoboTextShapeResultGlyphRunCount(result); runIndex++) {
        const RoboTextGlyphRun *glyphRun = RoboTextShapeResultGlyphRun(result, runIndex);
        RoboTextGlyphIndex glyphIndex;

        /*
         * Logical order, through one pen: an RTL run's glyphs are drawn left to right from its
         * origin because no bidi exists yet, and `stats.rtl` names how many runs that happened
         * to. RoboText preserves the logical order deliberately (`RoboTextGlyphs.h`), so this
         * loop has nothing to reorder and nothing to guess.
         */
        for (glyphIndex = 0u; glyphIndex < RoboTextGlyphRunCount(glyphRun); glyphIndex++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(glyphRun, glyphIndex);

            draw.glyphId = glyph->glyphId;
            draw.origin = MakeGfxPointF(penX + glyph->offsetX, baselineY + glyph->offsetY);
            if (GfxRasterDrawGlyph(raster, &draw) != RoboOk) {
                RoboTextShapeResultDestroy(result);
                RoboTextBufferDestroy(buffer);
                return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                                       "a glyph could not be drawn");
            }
            (*outGlyphs)++;
            /*
             * The miss is RoboText's own: `.notdef`, or a glyph whose metrics table would not
             * parse. An unmapped character still moves the pen -- by `.notdef`'s advance, which
             * is what the face says the pen does there and what keeps a drawn box from being
             * measured as nothing.
             */
            if (!glyph->mapped || glyph->metricsStatus != RoboFontGlyphMetricsOk) {
                (*outMissing)++;
            }
            penX += glyph->advanceX;
        }
    }

    RoboTextShapeResultDestroy(result);
    RoboTextBufferDestroy(buffer);
    return RoboOk;
}

/**
 * Narrow @p wanted -- the interval the command's boundary establishes -- by @p bound.
 *
 * The whole of a bounded pass's extra geometry (11.12), and it is arithmetic rather than a
 * second `set_clip` because the raster's clip is *already* the intersection of everything
 * applied to it: setting it twice would be the same subtraction written twice, and the second
 * call could only narrow what the first established. A NULL @p bound is the unbounded pass and
 * returns having touched nothing, which is why the unbounded door costs one comparison per
 * drawing.
 *
 * **An empty bound is applied as an empty interval** rather than refused, which is the rule
 * every rectangle in this module follows: "there is nowhere to draw" is a caller's or a
 * document's shape rather than a failure, and a fill against an empty clip is the graphics
 * module's own no-op. The four edges are subtracted in 64 bits and the extents saturate for the
 * reason every accumulation here does -- each operand is a bounded `int32` and none of the sums
 * is (`RoboPaintInternal.h`).
 */
static void DecorationApplyBound(const LayoutRect *bound, GfxRect *inoutWanted)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (bound == NULL) {
        return;
    }
    left = (int64_t)PaintMax(inoutWanted->x, bound->x);
    top = (int64_t)PaintMax(inoutWanted->y, bound->y);
    right = (int64_t)PaintMin((int64_t)inoutWanted->x + (int64_t)inoutWanted->width,
                                    (int64_t)bound->x + (int64_t)bound->width);
    bottom = (int64_t)PaintMin((int64_t)inoutWanted->y + (int64_t)inoutWanted->height,
                                     (int64_t)bound->y + (int64_t)bound->height);

    inoutWanted->x = PaintClampI64(left);
    inoutWanted->y = PaintClampI64(top);
    inoutWanted->width = PaintExtent(right - left);
    inoutWanted->height = PaintExtent(bottom - top);
}

/**
 * Non-zero when @p rect shares any area with @p bound: whether a bounded pass has anything to
 * do for this drawing (11.12).
 *
 * The test behind `boundedOut`, and it is deliberately a *diagnosis of work* rather than a
 * decision about drawing: the walk executes the drawing either way and lets the raster's clip
 * decide, because a text run's ink can overhang the advance box this rectangle is. An empty
 * rectangle on either side shares nothing.
 */
static int DecorationRectReachesBound(LayoutRect rect, const LayoutRect *bound)
{
    int64_t left = (int64_t)PaintMax(rect.x, bound->x);
    int64_t top = (int64_t)PaintMax(rect.y, bound->y);
    int64_t right = (int64_t)PaintMin((int64_t)rect.x + (int64_t)rect.width,
                                            (int64_t)bound->x + (int64_t)bound->width);
    int64_t bottom = (int64_t)PaintMin((int64_t)rect.y + (int64_t)rect.height,
                                             (int64_t)bound->y + (int64_t)bound->height);

    return right > left && bottom > top;
}

/**
 * Reset @p raster to the surface bounds, then narrow it by the command's boundary and, in a
 * bounded pass, by @p bound (11.12).
 *
 * Reset-then-replace rather than intersect, because the raster's own clip is the one place a
 * fill is bounded: the axis the boundary leaves open keeps the surface's own interval, and the
 * axis it hides takes the boundary's. @p outApplied is set only when a **boundary** was
 * applied, so "drawn under a clip" stays a fact about the command rather than about the surface
 * -- the bound a region run carries is not a boundary the document declared, and the counter it
 * belongs to is `boundedOut` rather than `clipped`.
 */
static Result DecorationApplyClip(GfxRaster *raster,
                                           const PaintClip *clip,
                                           const LayoutRect *bound,
                                           int *outApplied,
                                           PaintDiag *diag)
{
    GfxRect full;
    GfxRect wanted;
    int boundary = PaintClipIsActive(clip);

    *outApplied = 0;

    /* Every command starts from the surface bounds, so a boundary from the previous command
     * cannot leak onto this one. */
    if (GfxRasterResetClip(raster) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                               "the raster could not reset its clip to the surface bounds");
    }
    if (!boundary && bound == NULL) {
        return RoboOk;
    }
    if (GfxRasterClip(raster, &full) != RoboOk) {
        /* The surface's drawable area is not representable as int32 pixel bounds -- which the
         * raster's own creation already refused -- so this is a graphics-layer failure rather
         * than a document one. */
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                               "the surface bounds are not representable");
    }

    wanted = full;
    if (PaintClipAxisIsActive(&clip->horizontal)) {
        /* The interval is half-open and validated non-negative by the clip record, and the
         * extent saturates rather than wrapping for the reason every accumulation in this
         * module does. */
        wanted.x = clip->horizontal.start;
        wanted.width = PaintExtent((int64_t)clip->horizontal.end -
                                         (int64_t)clip->horizontal.start);
    }
    if (PaintClipAxisIsActive(&clip->vertical)) {
        wanted.y = clip->vertical.start;
        wanted.height = PaintExtent((int64_t)clip->vertical.end -
                                          (int64_t)clip->vertical.start);
    }
    DecorationApplyBound(bound, &wanted);
    if (GfxRasterSetClip(raster, wanted) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                               "the boundary could not be applied to the raster clip");
    }
    *outApplied = boundary;
    return RoboOk;
}

/**
 * The one walk both doors are (11.12).
 *
 * @p bound is the region a bounded pass executes inside, in paint coordinates, or NULL for the
 * unbounded pass -- and the two doors below differ in nothing else, which is the decision rather
 * than an economy: a filtered list, a second loop or a per-kind skip would each be a *second*
 * statement of "what this pass does" and the two could disagree about a document. Passing the
 * rectangle down to `DecorationApplyClip` makes a partial repaint the same pass with a smaller
 * window, so everything the unbounded pass guarantees -- the order, the scopes, the strengths,
 * the bucket identities -- holds unchanged inside the window by construction.
 */
static Result DecorationPaint(const PaintDisplayList *list,
                                      GfxSurface *target,
                                      FontFace *face,
                                      const LayoutRect *bound,
                                      PaintDecorationStats *outStats,
                                      PaintDiag *diag)
{
    PaintDecorationStats stats;
    PaintTextPool *pool;
    PaintClipStack *clips = NULL;
    PaintOpacityStack *opacities = NULL;
    GfxRaster *raster = NULL;
    Result status;
    size_t index;
    size_t count;

    if (!list || !target) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "the display list and the target surface are required");
    }

    PaintDecorationStatsInit(&stats);
    /* The list's own characters, borrowed for the pass -- the pool a `draw-text`'s span
     * names, which is the list's because a span is meaningless apart from one. */
    pool = PaintDisplayListTextPool(list);

    /*
     * The raster is the graphics module's, so the clip and the blend are its rules. Its
     * creation enforces that the target is write-capable and of an implemented format, which
     * is why a read-only surface refuses here rather than at the first fill.
     *
     * Its two refusals arrive as **different codes** -- a target that cannot take a write is
     * `RoboInvalidArgument` to the graphics module and an unimplemented format is
     * `RoboNotSupported` -- and both are the same answer one level up: there is nothing to
     * paint into. This door says so once, with the code this header names, which is why the
     * mapping is here rather than two branches that would each have to be read separately.
     * `RoboOutOfMemory` is **not** folded in: that is this build failing rather than the
     * caller's surface being wrong, and a caller retrying after freeing something deserves to
     * be told which it was.
     */
    status = GfxRasterCreate(target, &raster);
    if (status == RoboOutOfMemory) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboOutOfMemory,
                               "a raster could not be allocated for the target surface");
    }
    if (status != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboNotSupported,
                               "the target surface cannot be rasterised (it is read-only or "
                               "of a format this build does not implement)");
    }

    /*
     * The face's **tables**, prepared once for the pass, and since 12.4 that is two of them
     * rather than one. A `FontFace` is created without either -- `FontFaceLookupGlyph` answers
     * `RoboNotSupported` for a face whose cmap has not been parsed, and RoboText's metrics door
     * answers `Unavailable` for a face whose metrics have not been -- so the pass asks for both
     * here, through the same doors the layout measurer uses before it resolves a single glyph
     * (`RoboLayoutTextFont.c`). One font layer builds the tables; this is the layer that says
     * when, and it must say so before the first lookup rather than after it.
     *
     * The metrics parse is the *new* half, and it is worth naming why it moved here: the old
     * glyph loop asked `GetGfxGlyphMetricsDevice` for each advance, and that door parses the
     * metrics itself as a side effect (`RoboGraphicsText.c`). The loop no longer calls it -- the
     * advance now comes from RoboText -- so the preparation has to be *said* rather than
     * inherited from a door the pass no longer uses.
     *
     * A face whose tables **will not parse** is not refused. At worst every lookup misses,
     * every glyph is `.notdef`, and `glyphsMissing` names the fact per code point -- which is
     * the answer this pass already gives an unmapped character, so a broken font degrades to
     * the same shape as a font with no glyph for the text rather than to a failed pass.
     */
    if (face != NULL) {
        (void)FontFaceParseCmap(face);
        (void)FontFaceParseMetrics(face);
    }

    /*
     * The walk's clip stack (11.8). Created **after** the raster rather than before it, so
     * that a target the graphics layer refuses costs no allocation, and empty at the start
     * because a document that declares no overflow stands inside no scope at all.
     */
    status = PaintClipStackCreate(&clips, NULL);
    if (status != RoboOk) {
        return DecorationFinish(raster, NULL, NULL,
                                 RoboPaintDiag(diag, RoboPaintDomainClip,
                                                 RoboOutOfMemory,
                                                 "could not allocate the pass's clip stack"));
    }
    /*
     * The walk's opacity stack (11.9), for the strength the same reason the clip stack is for the
     * boundary: a scope is stated **once**, at its push, and every drawing under it is drawn at
     * the strength in force rather than at a copy of it. Empty at the start, because a document
     * that declares no opacity stands inside no scope at all -- which is the state in which the
     * raster's own opacity is untouched for the whole pass, and a document that reads exactly as
     * it did before this phase.
     */
    status = PaintOpacityStackCreate(&opacities, NULL);
    if (status != RoboOk) {
        return DecorationFinish(raster, clips, opacities,
                                 RoboPaintDiag(diag, RoboPaintDomainOpacity,
                                                 RoboOutOfMemory,
                                                 "could not allocate the pass's opacity "
                                                 "stack"));
    }

    count = PaintDisplayListCount(list);
    for (index = 0u; index < count; index++) {
        PaintDisplayCommand command;
        GfxColorRgba8 color;
        int clipped = 0;
        /*
         * Whether this command's appearance is a colour at all (11.7) -- declared here rather
         * than at each of the four questions below, because the four are one question asked
         * about the same command and a second spelling would be a second answer.
         */
        int hasColor = 0;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            /* Unreachable: the index came from the list's own count. */
            return DecorationFinish(raster, clips, opacities,
                                     RoboPaintDiag(diag, RoboPaintDomainGeometry,
                                                     RoboError,
                                                     "the display list changed under the "
                                                     "pass"));
        }
        stats.commands++;

        /*
         * The **clip scope** (11.8) is maintained before the drawing branch, because a scope
         * is not a drawing: a push becomes the boundary the pass stands inside and a pop
         * restores the one outside it. Neither touches the raster -- the clip a drawing is
         * bounded by is applied when that drawing is drawn, from the stack below -- so a scope
         * with nothing inside it costs two commands and no graphics work at all.
         *
         * Both are counted with the commands this pass does not draw, so
         * `commands = draws + skipped` stays exact; `clipScopes` is the number that says how
         * many of those skips were a boundary rather than a node marker.
         *
         * A **pop with nothing to pop** is the one list this pass can be handed that no
         * builder produces, and it is refused rather than tolerated: a consumer that closed a
         * scope that was never opened would go on drawing under a boundary that is not in force.
         * The refusal is reported with the pass's own domain, which is right -- the *list* is
         * malformed, and a caller holding it is the one who can fix it.
         */
        if (PaintDisplayCommandKindIsClip(command.kind)) {
            stats.skipped++;
            if (command.kind == RoboPaintDisplayCmdPushClip) {
                if (PaintClipStackPush(clips, &command.clip, diag) != RoboOk) {
                    return DecorationFinish(raster, clips, opacities,
                                             diag ? diag->code : RoboError);
                }
                stats.clipScopes++;
                if (PaintClipStackDepth(clips) > stats.clipDepthMax) {
                    stats.clipDepthMax = PaintClipStackDepth(clips);
                }
            } else if (PaintClipStackPop(clips, diag) != RoboOk) {
                return DecorationFinish(raster, clips, opacities, diag ? diag->code : RoboError);
            }
            continue;
        }

        /*
         * The **opacity scope** (11.9) is the same shape one field along: a structural command,
         * so it draws nothing and is one of `skipped`; a push becomes the strength the pass
         * stands inside and a pop restores the one outside it; and neither touches the raster,
         * because the strength a drawing is drawn at is applied **when that drawing is drawn**,
         * from the stack below. A scope with nothing inside it therefore costs two commands and
         * no graphics work at all -- and, because the raster's own default is full strength, a
         * document that declares no opacity never has its opacity touched once.
         *
         * A push of an **invalid** record cannot arrive: the display list's own append refuses
         * one, so this is the list's rule rather than a check to repeat. A **pop with nothing to
         * pop** is refused, for the clip's reason: a consumer that closed a strength that was
         * never established would go on drawing at a strength that is not in force.
         */
        if (PaintDisplayCommandKindIsOpacity(command.kind)) {
            stats.skipped++;
            if (command.kind == RoboPaintDisplayCmdPushOpacity) {
                if (PaintOpacityStackPush(opacities, &command.opacity, diag) != RoboOk) {
                    return DecorationFinish(raster, clips, opacities,
                                             diag ? diag->code : RoboError);
                }
                stats.opacityScopes++;
                if (PaintOpacityStackDepth(opacities) > stats.opacityDepthMax) {
                    stats.opacityDepthMax = PaintOpacityStackDepth(opacities);
                }
            } else if (PaintOpacityStackPop(opacities, diag) != RoboOk) {
                return DecorationFinish(raster, clips, opacities, diag ? diag->code : RoboError);
            }
            continue;
        }

        /*
         * A structural command draws nothing, by definition -- so `skipped` is a statement
         * about the list rather than a gap in this pass.
         */
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            stats.skipped++;
            continue;
        }

        /*
         * The colour, before the geometry, because "there was nothing to draw" and "there was
         * nowhere to draw it" are different facts and this is the one a document controls.
         *
         * A colour the record layer's own rule refuses (`none`, `invalid`) is not a drawing
         * this pass can attempt at all, so it is counted with the commands that are not
         * drawings rather than with the ones that are -- which is what keeps
         * `commands = draws + skipped` exact for a hand-built list whose command the list's
         * validity rule would have refused.
         *
         * It is asked of the kinds that **have** a colour, which 11.7 makes a narrower set than the
         * drawing kinds: an image command carries `none` and is asked the same question about its
         * payload instead, a few lines down. Writing this as "every drawing command" would refuse
         * every image in the document -- the one mistake that would make this phase look like it did
         * nothing at all. The answer is read once into a local flag rather than re-spelled at each
         * of the four places below that ask it, because four spellings of "does this command have a
         * colour" are four places for the answer to drift.
         */
        hasColor = PaintDisplayCommandKindUsesColor(command.kind);
        if (hasColor && command.color.kind != RoboCssColorRgba &&
            command.color.kind != RoboCssColorCurrent) {
            stats.skipped++;
            continue;
        }
        /*
         * And the image's own version of that question (11.7), asked where the colour's is and for
         * the same reason: a payload this build cannot draw is not a drawing this pass can attempt,
         * so it is a statement about the document -- `noResource` -- rather than a failed pass. A
         * list this build produced never reaches here, because the command layer's validity rule
         * refuses such a command at append; the term exists so that a hand-built list is counted
         * rather than silently swallowing the remainder of an identity.
         */
        if (!hasColor && !PaintImageIsValid(&command.image)) {
            stats.noResource++;
            continue;
        }
        /*
         * And the same rule for the one thing a text command needs that a fill does not: its
         * characters. The list's own append refuses a span that names no run, so this is
         * unreachable through the public API -- and it is checked here anyway, and beside the
         * colour rather than after the counters, so that a command this pass cannot draw is
         * reported as skipped rather than landing in a bucket that would then not add up.
         */
        if (command.kind == RoboPaintDisplayCmdDrawText &&
            !PaintTextPoolContains(pool, command.text.span)) {
            stats.skipped++;
            continue;
        }

        stats.draws++;
        if (command.kind == RoboPaintDisplayCmdDrawBackground) {
            stats.backgrounds++;
        } else if (command.kind == RoboPaintDisplayCmdDrawBorder) {
            stats.borders++;
            if (!DecorationStyleIsShaped(command.style)) {
                stats.unshaped++;
            }
        } else if (command.kind == RoboPaintDisplayCmdDrawImage) {
            stats.images++;
        } else {
            stats.texts++;
        }

        /*
         * The four reasons a *drawing* draws nothing, each named for the document fact it is:
         * a `currentColor` this layer cannot resolve, a colour that occupies no pixel, a
         * rectangle with no area, and the blank between them. They are the terms of
         * `draws = filled + contextual + transparent + empty` taken in order, so every
         * drawing lands in exactly one bucket and the counter is a statement rather than a
         * remainder.
         */
        if (hasColor && command.color.kind == RoboCssColorCurrent) {
            stats.contextual++;
            continue;
        }
        if (hasColor && command.color.alpha == 0) {
            stats.transparent++;
            continue;
        }
        /*
         * A text command is **not** held to the rectangle rule, and the exception is a fact
         * about text rather than a special case: a run's rectangle is its *advance* box, and a
         * glyph's ink can overhang it -- so a rectangle with no area is not "nothing to draw"
         * for a run the way it is for a fill, and the glyphs are drawn and the raster's clip
         * decides what of them lands. The fill half keeps the rule because a fill with no area
         * genuinely has no pixels to put a colour on.
         */
        if (command.kind == RoboPaintDisplayCmdDrawText) {
            if (face == NULL) {
                stats.noFont++;
                continue;
            }
        } else if (command.rect.width <= 0 || command.rect.height <= 0) {
            stats.empty++;
            continue;
        }
        if (hasColor && PaintColorToRgba8(command.color, &color) != RoboOk) {
            /* Unreachable: an opaque `RGBA` is exactly the colour `to_rgba8` copies. Reported
             * rather than counted, because a fill that could not be prepared is this build
             * failing and no bucket above would be telling the truth about it. */
            return DecorationFinish(raster, clips, opacities,
                                     RoboPaintDiag(diag, RoboPaintDomainGeometry,
                                                     RoboError,
                                                     "a drawing command's resolved colour "
                                                     "could not be converted"));
        }

        /*
         * The boundary this drawing is bounded by. Inside a scope it is the **scope's** clip --
         * the list states it once, at the push, and every drawing under it is bounded by that
         * one fact rather than by its own copy of it. Outside any scope it is the command's own
         * clip, which is the only statement of clipping a list without clip commands carries
         * (a hand-built one). The builder refuses a command inside a scope that disagrees with
         * it, so for every list this build produced the two values are equal and this choice is
         * not observable -- which is exactly what makes it safe to read from the stack.
         */
        {
            PaintClip effective;

            if (PaintClipStackIsEmpty(clips)) {
                effective = command.clip;
            } else {
                PaintClipStackCurrent(clips, &effective);
            }
            status = DecorationApplyClip(raster, &effective, bound, &clipped, diag);
        }
        if (status != RoboOk) {
            return DecorationFinish(raster, clips, opacities, status);
        }
        if (clipped) {
            stats.clipped++;
        }

        /*
         * The **strength** this drawing reaches the surface at, read from the same place the
         * boundary is and for the same reason: inside a scope it is the **scope's** record --
         * the list states it once, at the push -- and outside any scope it is the command's own,
         * which is the only statement of opacity a list without opacity commands carries. The
         * builder refuses a command inside a scope that disagrees with it, so for every list
         * this build produced the two are equal and the choice is not observable -- which is
         * what makes reading from the stack safe.
         *
         * It is set **per drawing** rather than saved and restored around a scope, and that is
         * a decision worth stating: the raster's opacity is one number, so leaving it at whatever
         * the last scope set is impossible to get wrong only if every drawing writes it. A
         * drawing therefore never inherits a strength from the drawing before it, and a scope
         * that contains nothing leaves the raster exactly as it found it. Setting it to `255` --
         * what an unscoped drawing gets -- is the raster's own default, so a document with no
         * opacity in it performs the same store a document with none would have skipped, and
         * `faded` counts the stores that were not to `255`.
         *
         * The colour's own alpha is **not** touched here. `final_alpha = colour_alpha *
         * effective_opacity` is the raster's arithmetic (`RoboGraphicsRaster.h`: global opacity
         * multiplies source alpha before the blend), so a half-transparent fill inside a half
         * opaque element composites the way both numbers say it should, and neither number is
         * rewritten into the other.
         */
        {
            PaintOpacity effective;
            uint8_t byte;

            if (PaintOpacityStackIsEmpty(opacities)) {
                effective = command.opacity;
            } else {
                PaintOpacityStackCurrent(opacities, &effective);
            }
            byte = PaintOpacityToByte(&effective);
            GfxRasterSetOpacity(raster, byte);
            if (byte != 255u) {
                stats.faded++;
            }
        }

        /*
         * The **bound** this drawing is executed inside (11.12), counted rather than acted on.
         *
         * A drawing whose rectangle cannot reach the bound is still executed -- the clip applied
         * a few lines above is what decides that it puts nothing down -- and the reason it is
         * still executed is the reason this counter is a number rather than a filter: a text
         * run's rectangle is its advance box, so a run can ink inside a bound its rectangle never
         * reaches, and a pass that skipped on the rectangle would be wrong about a document
         * rather than about a rectangle. So the decision stays with the pixels and the *work* is
         * what is reported, which is exactly the comparison a caller weighing "repaint
         * everything" against "repaint this region" is making.
         */
        if (bound != NULL && !DecorationRectReachesBound(command.rect, bound)) {
            stats.boundedOut++;
        }

        /*
         * The image, drawn through the graphics module's own primitive -- the *scaled* half of
         * its 1:1 copy -- and the three facts it is handed are the three the command carries: the
         * resource, the rectangle of it to sample, and the destination. Everything else has already
         * been done for it by the time the walk reaches here: the clip is the raster's, the strength
         * is the raster's, and the geometry rule above refused a destination with no area.
         *
         * A refusal is reported rather than counted, and the asymmetry with the terms above is
         * deliberate: those are statements about the *document*, where a primitive that refused a
         * payload its own validity rule admitted is this build disagreeing with itself -- the same
         * shape as a rectangle that will not fill, one branch below.
         */
        if (command.kind == RoboPaintDisplayCmdDrawImage) {
            status = GfxRasterDrawImage(raster, command.image.resource,
                                                command.image.source, command.rect);
            if (status != RoboOk) {
                return DecorationFinish(raster, clips, opacities,
                                         RoboPaintDiag(diag, RoboPaintDomainGeometry,
                                                         RoboError,
                                                         "an image command's drawing could "
                                                         "not be performed"));
            }
            stats.filled++;
            continue;
        }

        if (command.kind == RoboPaintDisplayCmdDrawText) {
            size_t glyphs = 0u;
            size_t missing = 0u;

            if (command.text.font.direction == RoboLayoutTextDirectionRtl) {
                /* No bidi in this build: an RTL run's characters are drawn left to right
                 * from its origin, and the count says how many runs that happened to. */
                stats.rtl++;
            }
            status = DecorationDrawText(raster, face, pool, &command, color, &glyphs,
                                          &missing, diag);
            if (status != RoboOk) {
                return DecorationFinish(raster, clips, opacities, status);
            }
            stats.glyphs += glyphs;
            stats.glyphsMissing += missing;
            stats.filled++;
            continue;
        }

        /*
         * The band a border side occupies, and the box a background fills: the same fill,
         * because a display command has already resolved the difference. Only the **shape**
         * inside the band is a border's own, and this build draws it as its band -- the
         * simplification `stats.unshaped` counts rather than hides.
         */
        status = GfxRasterFillRect(raster, command.rect, color);
        if (status != RoboOk) {
            return DecorationFinish(raster, clips, opacities,
                                     RoboPaintDiag(diag, RoboPaintDomainGeometry,
                                                     RoboError,
                                                     "a drawing command's rectangle could "
                                                     "not be filled"));
        }
        stats.filled++;
    }

    if (outStats) {
        *outStats = stats;
    }
    return DecorationFinish(raster, clips, opacities, RoboOk);
}

Result PaintDecorationPaint(const PaintDisplayList *list,
                                          GfxSurface *target,
                                          FontFace *face,
                                          PaintDecorationStats *outStats,
                                          PaintDiag *diag)
{
    return DecorationPaint(list, target, face, NULL, outStats, diag);
}

Result PaintDecorationPaintBounded(const PaintDisplayList *list,
                                                  GfxSurface *target,
                                                  FontFace *face,
                                                  const LayoutRect *bound,
                                                  PaintDecorationStats *outStats,
                                                  PaintDiag *diag)
{
    return DecorationPaint(list, target, face, bound, outStats, diag);
}
