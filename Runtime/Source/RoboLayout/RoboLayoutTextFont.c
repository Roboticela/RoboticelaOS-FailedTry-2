/*
 * RoboLayout's text measurer (Phase 10.5) — see RoboLayoutTextFont.h — over RoboText (Phase 12.4).
 *
 * A range's bytes go to the text layer; the text layer answers a `RoboTextMeasurement`; this file
 * turns that into a `LayoutTextRun`. What used to be here — a walk over code points, an
 * accumulation in font units, one conversion, and a line extent computed from three separately
 * scaled components -- is now RoboText's, and the reason that is a *fix* rather than a
 * reorganisation is the painter one layer along:
 *
 *     before   measurer sums font units and rounds once;  painter accumulates float advances
 *              -> one font, two arithmetic chains, a fraction of a pixel apart at the end of a run
 *     now      both consume the same `RoboTextGlyph` records -> one advance implementation
 *
 * The `RoboLayoutTextFont.h` header named that as the one failure no later phase could detect. It
 * is now undetectable for a better reason: there is nothing to detect.
 *
 * What the conversion still has to get right is the **unit boundary**, and it is the only
 * interesting thing in the file. `LayoutTextRun` counts code points and integer pixels, because
 * that is what the layout engine's geometry is in; RoboText counts scalars and answers in `float`
 * device pixels. The four conversions below are the whole seam: scalars -> `codePoints`, the width
 * accumulation -> one rounded `int32_t`, and the face's ascent/descent/line height -> the two
 * integers a baseline is placed with plus the height a line box gets.
 */

#include "RoboLayout/RoboLayoutTextFont.h"

#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboText/RoboText.h"
#include "RoboUnicode/RoboUnicode.h"

#include "RoboLayout/RoboLayoutText.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

void LayoutTextFontInit(LayoutTextFont *font)
{
    if (!font) {
        return;
    }
    memset(font, 0, sizeof(*font));
}

Result LayoutTextFontAttach(LayoutTextFont *font,
                                          FontFace *face)
{
    if (!font || !face) {
        return RoboInvalidArgument;
    }
    font->face = face;
    font->parsed = 0;
    font->runsMeasured = 0u;
    font->glyphsMissing = 0u;
    return RoboOk;
}

void LayoutTextFontDetach(LayoutTextFont *font)
{
    if (!font) {
        return;
    }
    font->face = NULL;
    font->parsed = 0;
}

void LayoutTextFontSetProvider(LayoutTextFont *font, const LayoutFontProvider *provider)
{
    if (!font) {
        return;
    }
    if (provider) {
        font->provider = *provider;
    } else {
        LayoutFontProviderInit(&font->provider);
    }
}

int LayoutTextFontIsAttached(const LayoutTextFont *font)
{
    return font && font->face != NULL;
}

/* ------------------------------------------------------------------ */
/* The face's tables, once per attach                                  */
/* ------------------------------------------------------------------ */

/**
 * Parse what measuring needs: the cmap and the layout metrics.
 *
 * Both are RoboFont's lazy, idempotent parses, so asking twice would be correct
 * and merely wasteful -- which is why the flag exists as an optimisation and not
 * as a correctness device, and why it is reset by `detach` rather than trusted
 * across faces.
 */
static Result MeasurePrepare(LayoutTextFont *font,
                                     FontFace *face,
                                     LayoutDiag *diag)
{
    if (!face) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                "text measurement needs an attached font face");
    }
    if (face == font->face && font->parsed) {
        return RoboOk;
    }
    if (FontFaceParseCmap(face) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                "the font face has no usable character map");
    }
    if (FontFaceParseMetrics(face) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                "the font face has no usable metrics");
    }
    if (face == font->face) {
        /* The fast path is only meaningful for the attached face; a per-run face is parsed idempotently
         * each time rather than remembered in one flag shared across faces. */
        font->parsed = 1;
    }
    return RoboOk;
}

/**
 * The face this run will be measured with (Phase 12.17).
 *
 * The attached face, or -- when a provider is installed and the request carries a computed style -- the
 * face the run's own font request resolves to. A request no family answers leaves the attached face in
 * place, which is the honest degradation: a document still draws in its system face rather than drawing
 * nothing because an author named a family this build does not have.
 */
static FontFace *MeasureSelectFace(LayoutTextFont *font,
                                   const LayoutTextRequest *request,
                                   int *outFallback,
                                   LayoutDiag *diag)
{
    LayoutFontRequest fontRequest;
    LayoutFontSelection selection;

    if (outFallback) {
        *outFallback = 0;
    }
    if (!LayoutFontProviderIsUsable(&font->provider) || !request->computed) {
        return font->face;
    }
    LayoutFontRequestResolve(request->computed, &fontRequest);
    if (LayoutFontRequestSelect(&fontRequest, &font->provider, &selection, diag) != RoboOk) {
        return font->face;
    }
    if (LayoutFontSelectionIsSet(&selection)) {
        if (outFallback) {
            *outFallback = selection.fallback;
        }
        return selection.face;
    }
    return font->face;
}

/* ------------------------------------------------------------------ */
/* The setting, and the line extent                                    */
/* ------------------------------------------------------------------ */

/**
 * The text layer's setting for @p style, over @p face.
 *
 * The four inputs `LayoutTextStyle` resolved are the four the context wants, spelled with RoboFont's
 * own types, which is why this is a copy rather than a translation -- except for the weight, which is
 * clamped into the nine-step scale the text layer can name. CSS allows a numeric weight outside
 * 100..900; a text context refuses one it cannot name, and refusing the *run* would trade a
 * wrong-looking weight for no measured text at all.
 *
 * `lineHeightPx` is left at `RoboTextLineHeightFromFontMetrics`, which is CSS's `normal`: layout has
 * its own line-box model (10.6) and a measured run's own height is the face's, not a leading the run
 * was never given.
 */
static Result MeasureContext(LayoutTextFont *font,
                             FontFace *face,
                             const LayoutTextStyle *style,
                             RoboTextFontContext *out,
                             LayoutDiag *diag)
{
    RoboTextFontStyle textStyle;

    (void)font;
    RoboTextFontStyleInit(&textStyle);
    textStyle.sizePx = (float)style->fontSizePx;
    textStyle.weight = (FontWeight)(style->fontWeight < 100 ? 100
                                    : style->fontWeight > 900 ? 900
                                                              : style->fontWeight);
    textStyle.style = style->italic ? RoboFontStyleItalic : RoboFontStyleNormal;
    RoboTextFontContextInit(out);
    if (RoboTextFontContextSet(out, face, &textStyle) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "a text run's font inputs are not a setting this build can measure");
    }
    return RoboOk;
}

/**
 * Fill @p run's line extent from the face's metrics, through RoboText's own metrics door.
 *
 * `ascent + descent + lineGap` as three *components* rather than a total, because 10.7 places a
 * baseline with the first two. RoboText normalises the descent's sign and the gap's floor at the
 * source (`RoboTextFont.h`), which is where the font-unit version of this function did it, so the
 * magnitude set is the same and the arithmetic is one call instead of three.
 *
 * Each magnitude rounds to the nearest pixel on its own and the height is their sum, exactly as
 * before: `RoboTextFontMetrics.lineHeightPx` is the unrounded sum and is deliberately not used,
 * because a box whose three parts do not add up to its height puts the next line's baseline a pixel
 * off.
 */
static Result MeasureLineExtent(LayoutTextFont *font,
                                FontFace *face,
                                const LayoutTextStyle *style,
                                LayoutTextRun *run,
                                LayoutDiag *diag)
{
    RoboTextFontContext context;
    RoboTextFontMetrics metrics;
    Result status;

    status = MeasureContext(font, face, style, &context, diag);
    if (status != RoboOk) {
        return status;
    }
    if (RoboTextFontContextMetrics(&context, &metrics, NULL) != RoboOk) {
        /*
         * No usable line metrics. The honest answer is the *font size* as the extent, which is what
         * CSS's own `normal` line height reduces to for a face nothing is known about -- and not
         * zero, which would silently collapse every line a document has.
         */
        run->ascent = (int32_t)style->fontSizePx;
        run->descent = 0;
        run->height = (int32_t)style->fontSizePx;
        return RoboOk;
    }
    run->ascent = (int32_t)(metrics.ascentPx + 0.5f);
    run->descent = (int32_t)(metrics.descentPx + 0.5f);
    run->height = run->ascent + run->descent + (int32_t)(metrics.lineGapPx + 0.5f);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Measuring                                                           */
/* ------------------------------------------------------------------ */

Result LayoutTextFontMeasure(LayoutTextFont *font,
                                           const LayoutTextRequest *request,
                                           LayoutTextRun *outRun,
                                           LayoutDiag *diag)
{
    HtmlNodeView view;
    FontFace *face;
    int fallback = 0;
    size_t rangeStartByte = 0u;
    size_t rangeByteLength = 0u;
    float width = 0.0f;
    Result status;

    if (!font || !request || !outRun) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "measuring needs a measurer, a request and a run");
    }
    LayoutTextRunInit(outRun);
    if (!request->style) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "measuring needs resolved font inputs");
    }

    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;

    if (HtmlNodeGetView(&request->textNode, &view, NULL) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboNotFound,
                                "the text node to measure did not resolve");
    }
    if (view.kind != RoboHtmlNodeKindText) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                "a text run can only be measured inside a text node");
    }

    /*
     * The range's validity first, through RoboLayoutText.h's own bridge, so that
     * "both endpoints land on code-point boundaries and inside the text" is
     * established once by the code that owns the range type. The byte span itself
     * is discarded -- this measurer walks to decode, because it needs the code
     * points and not the bytes -- but the *check* is worth the call rather than a
     * second copy of the boundary arithmetic below.
     */
    status = LayoutTextRangeBytes(&request->textNode, request->range,
                                         &rangeStartByte, &rangeByteLength, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * The face, selected once for this run and recorded on it (12.17). The painter draws with the face the
     * run carries rather than resolving the request again, so a run's width and its glyphs come from one
     * selection.
     */
    face = MeasureSelectFace(font, request, &fallback, diag);
    status = MeasurePrepare(font, face, diag);
    if (status != RoboOk) {
        return status;
    }
    outRun->face = face;
    outRun->fontFallback = fallback;

    /*
     * The range's **bytes**, handed to the text layer as a view.
     *
     * This is the seam, and the whole of it: the bridge above has already turned a code-point range
     * into a byte span (and refused one that does not land on a code-point boundary), the text layer
     * measures the bytes it is given, and no code point is walked here at all. The old loop began at
     * byte zero and skipped `range.start` code points to find the range's first byte; a view built
     * from the span does not have to, which is the second thing the change fixed rather than moved.
     */
    {
        RoboTextView rangeView = RoboTextViewMake(view.text + rangeStartByte, rangeByteLength);
        RoboTextFontContext context;
        RoboTextMeasurement measurement;

        status = MeasureContext(font, face, request->style, &context, diag);
        if (status != RoboOk) {
            return status;
        }
        status = RoboTextMeasureText(rangeView, &context, NULL, &measurement, NULL);
        if (status != RoboOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, status,
                                    "the text layer could not measure the run");
        }

        /*
         * `codePoints` is what the text layer counted as scalars, which is what this record has
         * always meant by it -- the misses come from the same count, so the two cannot disagree
         * about how many characters the range holds.
         */
        outRun->codePoints = measurement.scalarCount;
        outRun->glyphsMissing = measurement.missingGlyphs;
        font->glyphsMissing += measurement.missingGlyphs;
        /*
         * One rounding, at the boundary, of a sum the painter accumulates from the same records --
         * the property that replaced "two implementations of advance sharing one font". The `fabsf`
         * of the old code's negative guard is folded into the same expression: a width cannot be
         * negative, and `RoboText` never produces one, so the clamp is an assertion rather than a
         * branch.
         */
        width = measurement.width;
    }

    if (!(width > 0.0f)) {
        width = 0.0f;
    }
    outRun->width = (int32_t)(width + 0.5f);
    status = MeasureLineExtent(font, face, request->style, outRun, diag);
    if (status != RoboOk) {
        return status;
    }
    if (outRun->height < 0) {
        outRun->height = 0;
    }

    font->runsMeasured++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The source                                                          */
/* ------------------------------------------------------------------ */

static Result TextFontMeasureCb(void *userdata,
                                          const LayoutTextRequest *request,
                                          LayoutTextRun *outRun)
{
    return LayoutTextFontMeasure((LayoutTextFont *)userdata, request, outRun,
                                        NULL);
}

LayoutTextMetrics LayoutTextFontMetrics(LayoutTextFont *font)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    /*
     * A **detached** measurer yields an empty record, not a record whose callback
     * fails. That is what makes "no source" and "a source that answered nothing"
     * the same build behaviour with a count attached (RoboLayoutText.h): the seam
     * reads an absent callback as *measures nothing* and the pass counts the run as
     * unmeasured, where a callback that returned an error would abort the pass for a
     * document whose only sin is having no font attached.
     */
    if (font && LayoutTextFontIsAttached(font)) {
        metrics.userdata = font;
        metrics.measure = TextFontMeasureCb;
    }
    return metrics;
}
