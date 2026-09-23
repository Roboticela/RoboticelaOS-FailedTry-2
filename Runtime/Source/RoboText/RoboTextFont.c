/*
 * RoboText's font context (Phase 12.4) — see RoboTextFont.h.
 *
 * The file is one idea applied four times: **ask the face, and report what it said**. Each door below
 * parses lazily through RoboFont's own idempotent parsers, converts through the one scaling door, and
 * passes the face's status upward instead of collapsing it into a number. The three places this is
 * visible in code:
 *
 *   `UnitsToDevice`   refuses with RoboNotSupported rather than dividing by a zero `upem`, because
 *                     `value / 0` is an infinity and an infinity in a pen position is a bug three
 *                     layers away from its cause;
 *   `GlyphAdvance`    returns the *status* beside the number, so `RoboFontGlyphMetricsUnavailable` and
 *                     a genuine advance of zero are two answers rather than one;
 *   `ContextMetrics`  answers with a documented estimate when the face has no usable `head`/`hhea`,
 *                     and marks `fromFace = 0` so a caller can tell an estimate from a measurement.
 *
 * The estimate is `ascent = size, descent = 0, gap = 0` — what `RoboLayoutTextFont.c` already reduces to
 * when a face has no metrics, kept identical here on purpose: a second, differently-shaped estimate
 * would make the measurer and the painter disagree about a face that is broken rather than about a face
 * that is fine, which is the harder disagreement to find.
 */

#include "RoboText/RoboTextFont.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The context                                                         */
/* ------------------------------------------------------------------ */

void RoboTextFontContextInit(RoboTextFontContext *context)
{
    if (!context) {
        return;
    }
    context->face = NULL;
    RoboTextFontStyleInit(&context->style);
}

Result RoboTextFontContextSet(RoboTextFontContext *context,
                              FontFace *face,
                              const RoboTextFontStyle *style)
{
    if (!context || !style) {
        return RoboInvalidArgument;
    }
    if (!RoboTextFontStyleIsValid(style)) {
        /* The setting is refused *before* the face is stored, so a context is never left holding a face
         * at a setting this build cannot draw. */
        return RoboInvalidArgument;
    }
    context->face = face;
    context->style = *style;
    return RoboOk;
}

int RoboTextFontContextIsUsable(const RoboTextFontContext *context)
{
    return context != NULL && context->face != NULL && RoboTextFontStyleIsValid(&context->style);
}

FontFace *RoboTextFontContextFace(const RoboTextFontContext *context)
{
    return context ? context->face : NULL;
}

RoboTextFontStyle RoboTextFontContextStyle(const RoboTextFontContext *context)
{
    RoboTextFontStyle style;

    if (!context) {
        RoboTextFontStyleInit(&style);
        return style;
    }
    return context->style;
}

int RoboTextFontContextEqual(const RoboTextFontContext *a, const RoboTextFontContext *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->face == b->face && RoboTextFontStyleEqual(&a->style, &b->style);
}

int RoboTextFontContextCompare(const RoboTextFontContext *a, const RoboTextFontContext *b)
{
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    if (a->face != b->face) {
        return (uintptr_t)a->face < (uintptr_t)b->face ? -1 : 1;
    }
    /*
     * Field by field in declaration order, which is what makes this a *total* order rather than a partial
     * one: a caller sorting contexts for a determinism check needs two different settings to compare
     * unequal, and comparing only the fields a test happens to set would let two different contexts tie.
     */
    if (a->style.sizePx != b->style.sizePx) {
        return a->style.sizePx < b->style.sizePx ? -1 : 1;
    }
    if ((uint32_t)a->style.weight != (uint32_t)b->style.weight) {
        return (uint32_t)a->style.weight < (uint32_t)b->style.weight ? -1 : 1;
    }
    if ((int)a->style.style != (int)b->style.style) {
        return (int)a->style.style < (int)b->style.style ? -1 : 1;
    }
    if ((int)a->style.stretch != (int)b->style.stretch) {
        return (int)a->style.stretch < (int)b->style.stretch ? -1 : 1;
    }
    if (a->style.lineHeightPx != b->style.lineHeightPx) {
        return a->style.lineHeightPx < b->style.lineHeightPx ? -1 : 1;
    }
    if (a->style.letterSpacingPx != b->style.letterSpacingPx) {
        return a->style.letterSpacingPx < b->style.letterSpacingPx ? -1 : 1;
    }
    if ((int)a->style.opticalSize != (int)b->style.opticalSize) {
        return (int)a->style.opticalSize < (int)b->style.opticalSize ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Device space                                                        */
/* ------------------------------------------------------------------ */

/**
 * The face's `upem`, or 0 when the face has no usable one.
 *
 * `FontFaceLayoutMetrics` is RoboFont's own door and it is const, so this neither parses a table into a
 * mutable field nor costs a cast: a face whose metrics are unparsable simply answers 0, and every caller
 * above refuses on that.
 */
static uint16_t FontUpem(FontFace *face)
{
    FontMetrics metrics;

    memset(&metrics, 0, sizeof(metrics));
    if (FontFaceLayoutMetrics(face, &metrics) != RoboOk) {
        return 0u;
    }
    return metrics.unitsPerEm;
}

Result RoboTextFontUnitsToDevice(const RoboTextFontContext *context,
                                 double value,
                                 float *outPx,
                                 RoboTextDiag *diag)
{
    uint16_t upem;

    if (!context || !outPx) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out value are required");
    }
    *outPx = 0.0f;
    if (!context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }
    if (!(context->style.sizePx > 0.0f)) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a size that is not finite and positive cannot scale anything");
    }
    upem = FontUpem(context->face);
    if (upem == 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "the face declares no usable units-per-em");
    }
    /*
     * `double` in, `float` out, and the multiplication before the division — the same order
     * RoboGraphics' device-metrics bridge uses, so a glyph's advance here and one layer down are the
     * same number rather than two roundings of one.
     */
    *outPx = (float)((value * (double)context->style.sizePx) / (double)upem);
    return RoboOk;
}

Result RoboTextFontContextMetrics(const RoboTextFontContext *context,
                                  RoboTextFontMetrics *out,
                                  RoboTextDiag *diag)
{
    FontMetrics metrics;
    uint16_t upem;

    if (!context || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out record are required");
    }
    memset(out, 0, sizeof(*out));
    if (!context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }
    memset(&metrics, 0, sizeof(metrics));
    upem = 0u;
    if (FontFaceLayoutMetrics(context->face, &metrics) == RoboOk) {
        upem = metrics.unitsPerEm;
    }
    out->unitsPerEm = upem;
    if (upem == 0u || metrics.valid == 0u) {
        /*
         * The documented estimate (see the file header). `valid == 0` means the face never set
         * ascent/descent deliberately, and a face with a `upem` but no line metrics would otherwise give
         * a line box of exactly zero height — a line of text with no height is not a smaller line, it is
         * an invisible one.
         */
        out->fromFace = 0;
        out->ascentPx = context->style.sizePx;
        out->descentPx = 0.0f;
        out->lineGapPx = 0.0f;
    } else {
        out->fromFace = 1;
        /* `descent` is already a magnitude in RoboFont's record, which is why this door does not take an
         * absolute value: the normalisation happened at the source. */
        out->ascentPx = (float)(((double)metrics.ascent * (double)context->style.sizePx) / (double)upem);
        out->descentPx =
            (float)(((double)metrics.descent * (double)context->style.sizePx) / (double)upem);
        out->lineGapPx =
            (float)(((double)metrics.lineGap * (double)context->style.sizePx) / (double)upem);
        if (out->lineGapPx < 0.0f) {
            out->lineGapPx = 0.0f;
        }
    }
    if (context->style.lineHeightPx > 0.0f) {
        out->lineHeightPx = context->style.lineHeightPx;
    } else {
        /* `ascent + descent + gap == lineHeight` by construction, for both the measured and the estimated
         * branch, because both set the three fields above before this line runs. */
        out->lineHeightPx = out->ascentPx + out->descentPx + out->lineGapPx;
    }

    /*
     * The two optional metrics (Phase 12.5). They live in `OS/2`, not in the `head`/`hhea` pair the layout
     * metrics come from, so they need the metrics *info* object rather than `FontFaceLayoutMetrics` — and
     * they are optional in exactly the sense RoboFont's `FontOptionalUnit` means: `Present` is a
     * measurement, `Unavailable` is a zero here rather than a guess. A face whose `OS/2` is absent is not a
     * failure; it simply has no cap height to report.
     */
    if (upem != 0u) {
        const FontMetricsInfo *info = FontFaceMetricsInfo(context->face);
        const FontMetricsGlobal *global = info ? FontMetricsInfoGlobal(info) : NULL;

        if (global) {
            if (global->capHeight.avail == RoboFontMetricPresent) {
                out->capHeightPx = (float)(((double)global->capHeight.value *
                                            (double)context->style.sizePx) /
                                           (double)upem);
            }
            if (global->xHeight.avail == RoboFontMetricPresent) {
                out->xHeightPx = (float)(((double)global->xHeight.value *
                                          (double)context->style.sizePx) /
                                         (double)upem);
            }
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Glyphs                                                              */
/* ------------------------------------------------------------------ */

Result RoboTextFontLookupGlyph(const RoboTextFontContext *context,
                               Uchar32 scalar,
                               GlyphId *outGlyphId,
                               int *outMapped,
                               RoboTextDiag *diag)
{
    FontCmapLookupResult lookup;

    if (!context || !outGlyphId) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out glyph id are required");
    }
    *outGlyphId = RoboGlyphIdNone;
    if (outMapped) {
        *outMapped = 0;
    }
    if (!context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }
    if (!UnicodeIsScalar(scalar)) {
        /* Not a lookup that can fail — a value that is not a scalar is not a character this layer was
         * asked about. Refusing keeps a lone surrogate from reaching the cmap's own validation and coming
         * back as `InvalidCodePoint`, which would read as a font problem. */
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a value that is not a Unicode scalar has no glyph");
    }
    memset(&lookup, 0, sizeof(lookup));
    if (FontFaceLookupGlyph(context->face, (uint32_t)scalar, &lookup) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboError,
                               "the face's cmap could not be parsed");
    }
    if (lookup.status == RoboFontCmapMapped) {
        *outGlyphId = lookup.glyphId;
        if (outMapped) {
            *outMapped = 1;
        }
        return RoboOk;
    }
    /*
     * Unmapped is not a failure: it is the answer, and the caller's job is to count it (the measurement
     * does) or to fall back (12.2's fallback does, one layer up). `InvalidFont` is the one status that is
     * a real error here, and it means the cmap was parsed but could not be used — the same condition the
     * `RoboError` above reports, reached by a different door.
     */
    if (lookup.status == RoboFontCmapInvalidFont) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboError,
                               "the face's cmap is not usable");
    }
    return RoboOk;
}

Result RoboTextFontGlyphMetrics(const RoboTextFontContext *context,
                                GlyphId glyphId,
                                FontGlyphMetrics *outMetrics,
                                float *outAdvancePx,
                                RoboTextDiag *diag)
{
    FontGlyphMetrics metrics;
    Result status;

    if (!context || !outMetrics) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out record are required");
    }
    if (outAdvancePx) {
        *outAdvancePx = 0.0f;
    }
    if (!context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }
    memset(&metrics, 0, sizeof(metrics));
    status = FontFaceGlyphMetrics(context->face, glyphId, &metrics);
    if (status == RoboNotSupported) {
        /*
         * The face carries no metrics information at all -- no `hmtx`, or a `head`/`hhea` that would
         * not parse far enough to reach one. That is an **answer** rather than a failure, and the
         * answer is `Unavailable`: the glyph has no advance. Refusing here would make a face without
         * `hmtx` a face nothing can be drawn with, when what is true is that its glyphs do not
         * advance -- a fact a caller counts (it is one of the three in `FontGlyphMetricsStatus`)
         * rather than crashes on.
         */
        metrics.status = RoboFontGlyphMetricsUnavailable;
        metrics.glyphId = glyphId;
        *outMetrics = metrics;
        return RoboOk;
    }
    if (status != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboError,
                               "the face's glyph metrics could not be parsed");
    }
    *outMetrics = metrics;
    if (outAdvancePx && metrics.status == RoboFontGlyphMetricsOk) {
        /*
         * `advanceWidth` is `uint16_t` font units, so the scaling door is entered with the raw number and
         * the caller's `RoboTextGlyph.advanceUnits` keeps the same number for a dump. A status other than
         * Ok leaves the advance at zero **and** the status non-Ok, which is the pair a caller needs: the
         * number is what the pen must move (nowhere), the status is why.
         */
        Result status = RoboTextFontUnitsToDevice(context, (double)metrics.advanceWidth, outAdvancePx,
                                                  diag);
        if (status != RoboOk) {
            return status;
        }
    }
    return RoboOk;
}

Result RoboTextFontGlyphAdvance(const RoboTextFontContext *context,
                                GlyphId glyphId,
                                float *outAdvancePx,
                                FontGlyphMetricsStatus *outStatus,
                                RoboTextDiag *diag)
{
    FontGlyphMetrics metrics;
    Result status;
    float advance = 0.0f;

    if (!context || !outAdvancePx) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out advance are required");
    }
    *outAdvancePx = 0.0f;
    if (outStatus) {
        *outStatus = RoboFontGlyphMetricsUnavailable;
    }
    status = RoboTextFontGlyphMetrics(context, glyphId, &metrics, &advance, diag);
    if (status != RoboOk) {
        return status;
    }
    *outAdvancePx = advance;
    if (outStatus) {
        *outStatus = metrics.status;
    }
    return RoboOk;
}

Result RoboTextFontGsub(const RoboTextFontContext *context,
                        const FontOtLayout **out,
                        RoboTextDiag *diag)
{
    Result status;

    if (!context || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out table are required");
    }
    *out = NULL;
    if (!context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }
    status = FontFaceParseOtLayout(context->face);
    if (status != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, status,
                               "the face's OpenType layout tables could not be parsed");
    }
    /* A face with no GSUB is a face without substitution, which is not an error: the caller gets NULL and
     * shapes with the glyphs the cmap gave it. */
    *out = FontFaceGsub(context->face);
    return RoboOk;
}

Result RoboTextFontGdef(const RoboTextFontContext *context, const FontGdef **out, RoboTextDiag *diag)
{
    Result status;

    if (!context || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out table are required");
    }
    *out = NULL;
    if (!context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }
    status = FontFaceParseOtLayout(context->face);
    if (status != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, status,
                               "the face's OpenType layout tables could not be parsed");
    }
    *out = FontFaceGdef(context->face);
    return RoboOk;
}

Result RoboTextFontGpos(const RoboTextFontContext *context,
                        const FontOtLayout **out,
                        RoboTextDiag *diag)
{
    Result status;

    if (!context || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out table are required");
    }
    *out = NULL;
    if (!context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }
    status = FontFaceParseOtLayout(context->face);
    if (status != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, status,
                               "the face's OpenType layout tables could not be parsed");
    }
    /* A face with no GPOS is a face without positioning, which is not an error: the caller gets NULL and the
     * pen follows the advances the metrics gave it, which is what this runtime drew before 12.11. */
    *out = FontFaceGpos(context->face);
    return RoboOk;
}
