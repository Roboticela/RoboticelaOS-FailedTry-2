/*
 * RoboLayout's RoboFont-backed text measurer (Phase 10.5) — see
 * RoboLayoutTextFont.h.
 *
 * One walk over the code points of a range, one accumulation in font units, one
 * conversion. The header argues the accumulation; this file is where it has to
 * survive a face that is missing glyphs, a face with no hmtx, and a text node
 * whose bytes are not what its author meant.
 *
 * What is deliberately not here: shaping, kerning, fallback, bidi, and any cache
 * of a measurement. Two runs of the same text measure the same because the
 * arithmetic is a function of the face and the range -- not because anything
 * remembered.
 */

#include "RoboLayout/RoboLayoutTextFont.h"

#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontMetrics.h"
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
                                     LayoutDiag *diag)
{
    if (!LayoutTextFontIsAttached(font)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                "text measurement needs an attached font face");
    }
    if (font->parsed) {
        return RoboOk;
    }
    if (FontFaceParseCmap(font->face) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                "the font face has no usable character map");
    }
    if (FontFaceParseMetrics(font->face) != RoboOk) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                "the font face has no usable metrics");
    }
    font->parsed = 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

/**
 * Sum one code point's advance into @p outUnits, and report whether the face had
 * a glyph for it.
 *
 * Three answers from RoboFont, and each is handled rather than collapsed:
 *
 *   MAPPED     the glyph's own `hmtx` advance. The common case.
 *   UNMAPPED   no glyph: the run is measured at **glyph 0**'s advance, which is
 *              what every engine does with `.notdef`, and the miss is counted so
 *              a reader learns the width is for text the font cannot draw.
 *   INVALID    not a Unicode scalar, or not a face this build read. Counted as a
 *              miss as well, because from the run's point of view it is the same
 *              fact: nothing here corresponds to anything the font can draw.
 *
 * A glyph whose metrics will not parse is a miss for the same reason, and NOT a
 * zero: falling back to zero would make a broken font produce a *narrower* run
 * than a working one, which reads as a plausible measurement and is exactly the
 * failure this phase is built to avoid.
 */
static void MeasureCodePoint(LayoutTextFont *font,
                               uint32_t codePoint,
                               int64_t *ioUnits,
                               LayoutTextRun *run)
{
    FontCmapLookupResult lookup;
    FontGlyphMetrics metrics;
    GlyphId glyph;
    int mapped;

    memset(&lookup, 0, sizeof(lookup));
    mapped = FontFaceLookupGlyph(font->face, codePoint, &lookup) == RoboOk &&
             lookup.status == RoboFontCmapMapped;
    if (!mapped) {
        /*
         * No glyph. The *code point* is counted, once, and the measurement
         * continues at glyph 0 -- which is what `.notdef` is and what every engine
         * measures undrawable text at. Counting here rather than skipping is the
         * whole point: the width this run ends up with is a real width for text the
         * font cannot draw, and this counter is the only thing that says so.
         */
        glyph = RoboGlyphIdNone;
        run->glyphsMissing++;
        font->glyphsMissing++;
    } else {
        glyph = lookup.glyphId;
    }

    /*
     * The glyph's own metrics, and the *status* rather than only the result code:
     * RoboFont answers RoboOk with a status field for the three ways a glyph can
     * have no advance (`INVALID_ID` for an id past the face's own glyph count,
     * `UNAVAILABLE` for a face with no `hmtx`, `MALFORMED` for a table that will not
     * parse), and treating those as a measurement of zero would make a broken font
     * produce a *narrower* run than a working one -- a plausible-looking number, and
     * therefore the one failure a later phase could not detect.
     */
    memset(&metrics, 0, sizeof(metrics));
    if (FontFaceGlyphMetrics(font->face, glyph, &metrics) != RoboOk ||
        metrics.status != RoboFontGlyphMetricsOk) {
        /* Counted only when the cmap had *claimed* the code point: an unmapped one
         * is already counted above, and a count of two for one character would make
         * the number mean "problems" rather than "characters". */
        if (mapped) {
            run->glyphsMissing++;
            font->glyphsMissing++;
        }
        return;
    }
    *ioUnits += (int64_t)metrics.advanceWidth;
}

/**
 * The line extent of the face, in pixels.
 *
 * `ascent + descent + lineGap`, which is what RoboFont records as the canonical
 * layout metrics (`FontMetricsLineHeight`) and what the header calls the
 * run's height. Each term is scaled separately rather than summing in font units
 * first, and the difference is deliberate: a line extent is reported to a caller
 * as three *components* -- above the baseline, below it, and the leading between
 * lines -- and 10.7 needs the first two to place a baseline. Summing first would
 * produce the same total and lose the halves.
 */
static void MeasureLineExtent(LayoutTextFont *font,
                                const LayoutTextStyle *style,
                                LayoutTextRun *run)
{
    FontMetrics line;
    float ascent = 0.0f;
    float descent = 0.0f;
    float gap = 0.0f;

    memset(&line, 0, sizeof(line));
    if (FontFaceLayoutMetrics(font->face, &line) != RoboOk ||
        !FontUnitsPerEmIsValid(line.unitsPerEm)) {
        /*
         * No usable line metrics. The honest answer is the *font size* as the
         * extent, which is what CSS's own `normal` line height reduces to for a
         * face nothing is known about -- and not zero, which would silently
         * collapse every line a document has.
         */
        run->ascent = (int32_t)style->fontSizePx;
        run->descent = 0;
        run->height = (int32_t)style->fontSizePx;
        return;
    }

    if (FontScaleFontUnits(line.ascent, line.unitsPerEm,
                                   (FontPixelSize)style->fontSizePx,
                                   RoboFontRoundNearest, &ascent) != RoboOk) {
        ascent = (float)style->fontSizePx;
    }
    if (FontScaleFontUnits(line.descent, line.unitsPerEm,
                                   (FontPixelSize)style->fontSizePx,
                                   RoboFontRoundNearest, &descent) != RoboOk) {
        descent = 0.0f;
    }
    if (FontScaleFontUnits(line.lineGap, line.unitsPerEm,
                                   (FontPixelSize)style->fontSizePx,
                                   RoboFontRoundNearest, &gap) != RoboOk) {
        gap = 0.0f;
    }

    /*
     * A font whose descent is authored positive is reported as a positive
     * magnitude by RoboFont (RoboFontMetrics.h says so), but a face in the wild
     * can carry either sign convention. Normalising to a magnitude here keeps the
     * record's stated meaning true whatever the table said: `descent` is a
     * magnitude, so `height` is always ascent + descent + gap and never a
     * subtraction.
     */
    if (descent < 0.0f) {
        descent = -descent;
    }
    if (gap < 0.0f) {
        gap = 0.0f;
    }
    run->ascent = (int32_t)(ascent + 0.5f);
    run->descent = (int32_t)(descent + 0.5f);
    run->height = run->ascent + run->descent + (int32_t)(gap + 0.5f);
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
    LayoutTextRange range;
    size_t offset = 0u;
    size_t index = 0u;
    size_t rangeStartByte = 0u;
    size_t rangeByteLength = 0u;
    int64_t units = 0;
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
    range = request->range;

    status = MeasurePrepare(font, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * Advance the byte cursor to the range's first code point, then walk the
     * range's own code points. The first loop exists so the sum starts at the
     * right place: a run is measured from its own beginning, and starting at byte
     * zero would measure the whole node whatever range was asked for.
     */
    while (index < range.start) {
        size_t next = offset;
        Uchar32 codePoint = 0;

        if (offset >= view.textLength) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                    "a text range starts past the end of the text");
        }
        if (UnicodeUtf8Decode(view.text, view.textLength, offset, RoboUnicodeReplace,
                                    &codePoint, &next) != RoboUnicodeOk ||
            next <= offset) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                    "the text node's own bytes could not be decoded");
        }
        offset = next;
        index++;
    }

    while (index < range.end) {
        size_t next = offset;
        Uchar32 codePoint = 0;

        if (offset >= view.textLength) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                    "a text range ends past the end of the text");
        }
        if (UnicodeUtf8Decode(view.text, view.textLength, offset, RoboUnicodeReplace,
                                    &codePoint, &next) != RoboUnicodeOk ||
            next <= offset) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboError,
                                    "the text node's own bytes could not be decoded");
        }
        MeasureCodePoint(font, (uint32_t)codePoint, &units, outRun);
        outRun->codePoints++;
        offset = next;
        index++;
    }

    /*
     * One conversion for the whole run, which is the header's central decision.
     * `FontScaleFontUnits` carries the overflow check and the rounding
     * policy, so an absurd font size is refused rather than wrapped, and the
     * conversion is the same one RoboFont uses everywhere else.
     */
    {
        FontMetrics line;
        uint16_t upem = 0u;

        memset(&line, 0, sizeof(line));
        if (FontFaceLayoutMetrics(font->face, &line) == RoboOk) {
            upem = line.unitsPerEm;
        }
        if (!FontUnitsPerEmIsValid(upem)) {
            upem = 1000u; /* The SFNT convention for a font that did not say. */
        }
        if (FontScaleFontUnits((FontUnit)units, upem,
                                       (FontPixelSize)request->style->fontSizePx,
                                       RoboFontRoundNearest, &width) != RoboOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTextLayout, RoboInvalidArgument,
                                    "a run's advance does not fit the pixel space");
        }
    }

    if (width < 0.0f) {
        width = 0.0f;
    }
    outRun->width = (int32_t)(width + 0.5f);
    MeasureLineExtent(font, request->style, outRun);
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
