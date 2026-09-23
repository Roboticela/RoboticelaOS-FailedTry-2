/*
 * RoboText's glyphs (Phase 12.4) — see RoboTextGlyphs.h.
 *
 * This is the file the phase is named for, and the whole of it is one loop:
 *
 *     for each scalar in the run:
 *         glyph id  = the face's cmap
 *         advance   = the face's hmtx, scaled once through the font context
 *         source    = that one scalar
 *
 * Four lines, and the phase's claim is not that they are clever — they are not, and
 * `RoboTextGlyphGenerationCmapPerScalar` is named the placeholder it is precisely because of that. The
 * claim is that after this file the *painter no longer owns them*. The old path ran the same four lines
 * inside `RoboPaintText.c`, which meant the paintable unit was a `const char *` and a pen, and a ligature
 * or a kern or a `locl` could not be added without changing the painter's contract with every caller.
 * Now the paintable unit is a `RoboTextGlyph` carrying its own id, its own advance and its own source
 * span, and Phase 12.10's substitution pass replaces this loop's *body* — one glyph from two scalars, one
 * glyph for two scalars, a mark with no advance — without any of the five layers above noticing.
 *
 * Two details are load-bearing rather than incidental:
 *
 *   **The unmapped case draws `.notdef`.** A scalar with no glyph still moves the pen, by glyph 0's own
 *   advance, because that is what the face says the pen does there and what every other text engine does.
 *   The alternative — a zero advance — would silently reflow a line around a character the reader can see
 *   (the box is drawn), and would make the measurement of undrawable text narrower than its ink.
 *
 *   **The decode walks, it does not index.** `ScalarAt` per scalar would be O(n²) over a run, and the
 *   range is contiguous, so the loop converts both endpoints once and then walks the bytes between them.
 */

#include "RoboText/RoboTextGlyphs.h"

#include "RoboMem/RoboMem.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboTextInternal.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

struct RoboTextGlyphRun {
    RoboTextGlyph *glyphs;
    size_t count;
    RoboTextRange source;
    RoboTextScript script;
    RoboTextDirection direction;
    /** The resolved level the run was generated at (Phase 12.8). */
    RoboTextBidiLevel bidiLevel;
    RoboTextDirection baseDirection;
    RoboTextFontContext font;
    RoboTextLanguage language;
    RoboTextGlyphGenerationMode mode;
    size_t missing;
    /** Glyphs whose id came from a mirrored code point (UAX #9 L4). */
    size_t mirrored;
    /** Glyphs that stand for a bidi formatting control: no id, no advance, nothing drawn. */
    size_t controls;
    /** Glyphs whose id a GSUB lookup produced rather than the `cmap` (Phase 12.10). */
    size_t substituted;
    /** Glyphs a ligature produced: several scalars folded into one glyph (Phase 12.12). */
    size_t ligatures;
    /** Accumulated in `double` from the glyphs' own `float` advances, so a long run's total is the sum of
     *  what the painter accumulates rather than a second rounding of it. */
    double advanceTotal;
};

/* ------------------------------------------------------------------ */
/* The generation mode                                                 */
/* ------------------------------------------------------------------ */

int RoboTextGlyphGenerationModeIsKnown(RoboTextGlyphGenerationMode mode)
{
    return mode == RoboTextGlyphGenerationNone || mode == RoboTextGlyphGenerationCmapPerScalar ||
           mode == RoboTextGlyphGenerationGsub;
}

const char *RoboTextGlyphGenerationModeName(RoboTextGlyphGenerationMode mode)
{
    switch (mode) {
        case RoboTextGlyphGenerationCmapPerScalar:
            return "cmap-per-scalar";
        case RoboTextGlyphGenerationGsub:
            return "cmap+gsub";
        case RoboTextGlyphGenerationNone:
        default:
            return "none";
    }
}

/* ------------------------------------------------------------------ */
/* One glyph                                                           */
/* ------------------------------------------------------------------ */

void RoboTextGlyphInit(RoboTextGlyph *glyph)
{
    if (!glyph) {
        return;
    }
    memset(glyph, 0, sizeof(*glyph));
    glyph->glyphId = RoboGlyphIdNone;
    glyph->source = RoboTextRangeAt(0u);
    glyph->metricsStatus = RoboFontGlyphMetricsUnavailable;
}

int RoboTextGlyphIsValid(const RoboTextGlyph *glyph)
{
    if (!glyph) {
        return 0;
    }
    if (RoboTextRangeIsInverted(glyph->source)) {
        return 0;
    }
    /*
     * An empty source is *not* a valid glyph: a glyph exists because something produced it, and a glyph
     * with no source scalars is a record that cannot answer "where did this come from" — which is the one
     * question the record exists to answer. A null glyph (`RoboTextGlyphInit`) is therefore invalid by
     * construction rather than by a flag.
     */
    return !RoboTextRangeIsEmpty(glyph->source);
}

int RoboTextGlyphEqual(const RoboTextGlyph *a, const RoboTextGlyph *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->glyphId == b->glyphId && RoboTextRangeEqual(&a->source, &b->source) &&
           a->cluster == b->cluster && a->advanceUnits == b->advanceUnits &&
           a->advanceX == b->advanceX && a->advanceY == b->advanceY && a->offsetX == b->offsetX &&
           a->offsetY == b->offsetY && a->mapped == b->mapped &&
           a->control == b->control && a->mirrored == b->mirrored &&
           a->substituted == b->substituted && a->metricsStatus == b->metricsStatus;
}

/* ------------------------------------------------------------------ */
/* Substitution (Phase 12.10)                                          */
/* ------------------------------------------------------------------ */

/**
 * The glyph run as a shaping buffer: one entry per glyph slot, in source order.
 *
 * A control is carried and flagged rather than dropped, because its slot is what keeps every later glyph's
 * index agreeing with its scalar; `RoboTextGsubSkipped` is where the flag does its work.
 */
static Result GsubBufferFromRun(const RoboTextGlyphRun *glyphRun,
                                RoboTextGsubBuffer **out,
                                RoboTextDiag *diag)
{
    RoboTextGsubBuffer *buffer = RoboTextGsubBufferCreate(glyphRun->count);

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                               "the shaping buffer could not be allocated");
    }
    for (size_t index = 0u; index < glyphRun->count; index++) {
        const RoboTextGlyph *glyph = &glyphRun->glyphs[index];
        RoboTextShapingGlyph shaping;

        RoboTextShapingGlyphInit(&shaping);
        shaping.glyphId = glyph->glyphId;
        shaping.source = glyph->source;
        shaping.cluster = glyph->cluster;
        shaping.bidiLevel = glyphRun->bidiLevel;
        if (glyph->control) {
            shaping.flags |= RoboTextShapingGlyphControl;
        }
        if (glyph->mirrored) {
            shaping.flags |= RoboTextShapingGlyphMirrored;
        }
        if (RoboTextGsubBufferAppend(buffer, &shaping) != RoboOk) {
            RoboTextGsubBufferDestroy(buffer);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                                   "the shaping buffer could not grow");
        }
    }
    *out = buffer;
    return RoboOk;
}

/**
 * Rebuild @p glyphRun from @p buffer, after a substitution changed the glyph count.
 *
 * Every glyph's advance is asked of the face **for its own id**, which is the one number substitution
 * changes that a reader would notice: a ligature's advance is not the sum of its components'.
 */
static Result GsubRunFromBuffer(RoboTextGlyphRun *glyphRun,
                                const RoboTextGsubBuffer *buffer,
                                const RoboTextRun *run,
                                RoboTextDiag *diag)
{
    size_t produced = RoboTextGsubBufferCount(buffer);
    RoboTextGlyph *rebuilt =
        (RoboTextGlyph *)RoboCalloc(produced != 0u ? produced : 1u, sizeof(*rebuilt));

    if (!rebuilt) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                               "the rebuilt glyph array could not be allocated");
    }
    glyphRun->missing = 0u;
    glyphRun->mirrored = 0u;
    glyphRun->controls = 0u;
    glyphRun->substituted = 0u;
    glyphRun->ligatures = 0u;
    glyphRun->advanceTotal = 0.0;

    for (size_t index = 0u; index < produced; index++) {
        const RoboTextShapingGlyph *shaping = RoboTextGsubBufferAt(buffer, index);
        RoboTextGlyph *glyph = &rebuilt[index];
        FontGlyphMetrics metrics;
        float advance = 0.0f;
        Result status;
        int mapped;

        RoboTextGlyphInit(glyph);
        glyph->glyphId = shaping->glyphId;
        glyph->source = shaping->source;
        glyph->cluster = shaping->cluster;
        if ((shaping->flags & RoboTextShapingGlyphControl) != 0u) {
            /* A control keeps its slot and stays empty; it is not a `.notdef` and is not counted as one. */
            glyph->control = 1;
            glyphRun->controls++;
            continue;
        }
        glyph->mirrored = (shaping->flags & RoboTextShapingGlyphMirrored) != 0u ? 1 : 0;
        if (glyph->mirrored) {
            glyphRun->mirrored++;
        }
        if ((shaping->flags & RoboTextShapingGlyphSubstituted) != 0u) {
            glyph->substituted = 1;
            glyphRun->substituted++;
        }
        glyph->ligatureComponents = shaping->ligatureComponents;
        if (glyph->ligatureComponents >= 2u) {
            /*
             * A ligature, by the only definition that needs no extra bookkeeping: a substitution that folded
             * several glyphs into this one. The engine records the count as it substitutes, so nothing here
             * re-derives it from a glyph count that a later lookup could have changed.
             */
            glyphRun->ligatures++;
        }
        mapped = (shaping->glyphId != RoboGlyphIdNone) ? 1 : 0;
        glyph->mapped = mapped;

        status = RoboTextFontGlyphMetrics(&run->font, glyph->glyphId, &metrics, &advance, diag);
        if (status != RoboOk) {
            RoboFree(rebuilt);
            return status;
        }
        glyph->metricsStatus = metrics.status;
        if (metrics.status == RoboFontGlyphMetricsOk) {
            glyph->advanceUnits = metrics.advanceWidth;
        }
        glyph->advanceX = advance;
        if (!mapped || metrics.status != RoboFontGlyphMetricsOk) {
            glyphRun->missing++;
        }
        glyphRun->advanceTotal += (double)glyph->advanceX;
    }

    RoboFree(glyphRun->glyphs);
    glyphRun->glyphs = rebuilt;
    glyphRun->count = produced;
    return RoboOk;
}

/**
 * Apply the face's GSUB to @p glyphRun, in place.
 *
 * The three answers that are not a substitution: a face with no GSUB, a face whose GSUB has no record for
 * this run's script, and an empty run — each leaves the cmap pass's result alone, and the mode stays
 * @ref RoboTextGlyphGenerationCmapPerScalar, which is a fact about the font rather than about the text.
 */
static Result GsubShapeRun(const RoboTextRun *run, RoboTextGlyphRun *glyphRun, RoboTextDiag *diag)
{
    const FontOtLayout *gsub = NULL;
    const FontGdef *gdef = NULL;
    RoboTextGsubPlan plan;
    RoboTextGsubOptions options;
    RoboTextGsubStats stats;
    RoboTextGsubBuffer *buffer = NULL;
    Result status;

    status = RoboTextFontGsub(&run->font, &gsub, diag);
    if (status != RoboOk || !gsub) {
        return status;
    }
    status = RoboTextFontGdef(&run->font, &gdef, diag);
    if (status != RoboOk) {
        return status;
    }
    status = RoboTextGsubPlanResolve(gsub, run->script, &run->language, &run->features, &plan, diag);
    if (status != RoboOk || plan.empty || glyphRun->count == 0u) {
        return status;
    }
    status = GsubBufferFromRun(glyphRun, &buffer, diag);
    if (status != RoboOk) {
        return status;
    }
    memset(&options, 0, sizeof(options));
    options.direction = run->direction;
    options.bidiLevel = glyphRun->bidiLevel;
    status = RoboTextGsubApply(gsub, gdef, &plan, &options, buffer, &stats, diag);
    if (status == RoboOk) {
        status = GsubRunFromBuffer(glyphRun, buffer, run, diag);
    }
    RoboTextGsubBufferDestroy(buffer);
    if (status != RoboOk) {
        return status;
    }
    /* A plan resolved and ran, whatever it did: that is what `Gsub` records, and it is why a run of text a
     * font substitutes nothing in still reports the mode honestly. */
    glyphRun->mode = RoboTextGlyphGenerationGsub;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Generation                                                          */
/* ------------------------------------------------------------------ */

size_t RoboTextGlyphRunControls(const RoboTextGlyphRun *run)
{
    return run ? run->controls : 0u;
}

void RoboTextGlyphRunDestroy(RoboTextGlyphRun *run)
{
    if (!run) {
        return;
    }
    RoboFree(run->glyphs);
    RoboFree(run);
}

Result RoboTextGlyphRunGenerate(RoboTextView view,
                                const RoboTextRun *run,
                                RoboTextGlyphRun **out,
                                RoboTextDiag *diag)
{
    RoboTextGlyphRun *glyphRun;
    RoboTextByteIndex startByte = 0u;
    RoboTextByteIndex endByte = 0u;
    RoboTextScalarIndex length;
    size_t offset;
    Result status;
    int mapped = 0;

    if (!out || !run) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run and an out pointer are required");
    }
    *out = NULL;
    if (RoboTextRangeIsInverted(run->range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run whose range ends before it starts has no glyphs");
    }
    if (!run->font.face) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotSupported,
                               "the run's font context names no face");
    }
    /*
     * Both endpoints through `ScalarToByte`, which is total for in-range indices and refuses the rest —
     * so this *is* the "does the range lie in the text" check rather than a separate one that could
     * disagree with the conversion.
     */
    status = RoboTextViewScalarToByte(view, run->range.start, &startByte, diag);
    if (status != RoboOk) {
        return status;
    }
    status = RoboTextViewScalarToByte(view, run->range.end, &endByte, diag);
    if (status != RoboOk) {
        return status;
    }
    if (endByte < startByte) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run's byte span is inverted");
    }
    length = RoboTextRangeLength(run->range);

    glyphRun = (RoboTextGlyphRun *)RoboCalloc(1u, sizeof(*glyphRun));
    if (!glyphRun) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                               "the glyph run could not be allocated");
    }
    glyphRun->source = run->range;
    glyphRun->script = run->script;
    glyphRun->direction = run->direction;
    glyphRun->bidiLevel = run->bidiLevel;
    glyphRun->baseDirection = run->baseDirection;
    glyphRun->font = run->font;
    glyphRun->language = run->language;
    glyphRun->mode = RoboTextGlyphGenerationCmapPerScalar;
    if (length != 0u) {
        glyphRun->glyphs = (RoboTextGlyph *)RoboCalloc(length, sizeof(*glyphRun->glyphs));
        if (!glyphRun->glyphs) {
            RoboTextGlyphRunDestroy(glyphRun);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                                   "the glyph array could not be allocated");
        }
    }

    offset = (size_t)startByte;
    for (RoboTextScalarIndex index = 0u; index < length; index++) {
        RoboTextGlyph *glyph = &glyphRun->glyphs[index];
        Uchar32 scalar = 0u;
        Uchar32 draw = 0u;
        size_t next = offset;
        FontGlyphMetrics metrics;
        float advance = 0.0f;

        if (UnicodeUtf8Decode(view.bytes, view.length, offset, RoboUnicodeReplace, &scalar, &next) !=
                RoboUnicodeOk ||
            next <= offset) {
            RoboTextGlyphRunDestroy(glyphRun);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboParseError,
                                   "the run's text could not be decoded");
        }
        offset = next;

        RoboTextGlyphInit(glyph);
        glyph->source = RoboTextRangeMake(run->range.start + index, run->range.start + index + 1u);
        /* Identity clustering, stated rather than implied (see the header): with no grapheme
         * segmentation a cluster is one scalar, and the field is filled now so that the goal which adds
         * segmentation fills it differently rather than adding it. */
        glyph->cluster = glyph->source.start;

        /*
         * A bidi formatting control is not a character: Phase 12.8 §"formatting characters" asks that the
         * nine controls create no visible glyph while their source positions are preserved for editing and
         * selection. Both halves are satisfied by *keeping the slot* — one glyph per scalar, so
         * `source`/`cluster` still map exactly — and emptying it (no id, no advance, `control` set). The
         * alternative, dropping the slot, would make every later glyph's index disagree with its scalar;
         * the alternative of drawing it would put a `.notdef` box in the reader's text.
         */
        if (RoboTextBidiScalarIsFormattingControl((RoboTextCodePoint)scalar)) {
            glyph->control = 1;
            glyph->mapped = 0;
            glyph->glyphId = RoboGlyphIdNone;
            glyph->advanceUnits = 0u;
            glyph->advanceX = 0.0f;
            glyphRun->controls++;
            continue;
        }

        /*
         * UAX #9 L4, and the one place this build honours it: a scalar in a right-to-left context is
         * depicted by its mirrored code point, so the *lookup* uses the mirror while the glyph's `source`
         * stays the scalar. That split is the phase's rule — "do not mutate the source text" — and it is
         * what keeps a copy, a selection and a hit test reading the text the author wrote.
         *
         * Both readings of "this run is right-to-left" are honoured, because a run built before 12.8
         * carries only the direction and one built from the analysis carries a level as well; a caller that
         * hand-builds a run gets the direction's answer, which is what it declared.
         */
        draw = scalar;
        if (RoboTextDirectionIsRightToLeft(run->direction) ||
            RoboTextBidiLevelIsRightToLeft(run->bidiLevel)) {
            RoboTextCodePoint mirror = (RoboTextCodePoint)scalar;

            if (RoboTextBidiMirrorOf((RoboTextCodePoint)scalar, &mirror) && mirror != (RoboTextCodePoint)scalar) {
                draw = (Uchar32)mirror;
                glyph->mirrored = 1;
                glyphRun->mirrored++;
            }
        }

        status = RoboTextFontLookupGlyph(&run->font, draw, &glyph->glyphId, &mapped, diag);
        if (status != RoboOk) {
            RoboTextGlyphRunDestroy(glyphRun);
            return status;
        }
        glyph->mapped = mapped;

        status = RoboTextFontGlyphMetrics(&run->font, glyph->glyphId, &metrics, &advance, diag);
        if (status != RoboOk) {
            RoboTextGlyphRunDestroy(glyphRun);
            return status;
        }
        glyph->metricsStatus = metrics.status;
        if (metrics.status == RoboFontGlyphMetricsOk) {
            glyph->advanceUnits = metrics.advanceWidth;
        }
        glyph->advanceX = advance;
        /* `advanceY`, `offsetX` and `offsetY` stay zero: nothing positions anything yet, and a field that
         * carried a plausible-looking value would be a claim no goal has made. */
        if (!mapped || metrics.status != RoboFontGlyphMetricsOk) {
            glyphRun->missing++;
        }
        glyphRun->advanceTotal += (double)glyph->advanceX;
    }

    glyphRun->count = (size_t)length;
    status = GsubShapeRun(run, glyphRun, diag);
    if (status != RoboOk) {
        RoboTextGlyphRunDestroy(glyphRun);
        return status;
    }
    *out = glyphRun;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

size_t RoboTextGlyphRunCount(const RoboTextGlyphRun *run)
{
    return run ? run->count : 0u;
}

const RoboTextGlyph *RoboTextGlyphRunGlyph(const RoboTextGlyphRun *run, RoboTextGlyphIndex index)
{
    if (!run || (size_t)index >= run->count) {
        return NULL;
    }
    return &run->glyphs[index];
}

RoboTextRange RoboTextGlyphRunSourceRange(const RoboTextGlyphRun *run)
{
    return run ? run->source : RoboTextRangeAt(0u);
}

RoboTextScript RoboTextGlyphRunScript(const RoboTextGlyphRun *run)
{
    return run ? run->script : RoboTextScriptCommon;
}

RoboTextDirection RoboTextGlyphRunDirection(const RoboTextGlyphRun *run)
{
    return run ? run->direction : RoboTextDirectionUnknown;
}

RoboTextBidiLevel RoboTextGlyphRunBidiLevel(const RoboTextGlyphRun *run)
{
    return run ? run->bidiLevel : RoboTextBidiLevelNone;
}

size_t RoboTextGlyphRunMirrored(const RoboTextGlyphRun *run)
{
    return run ? run->mirrored : 0u;
}

size_t RoboTextGlyphRunSubstituted(const RoboTextGlyphRun *run)
{
    return run ? run->substituted : 0u;
}

size_t RoboTextGlyphRunLigatures(const RoboTextGlyphRun *run)
{
    return run ? run->ligatures : 0u;
}

RoboTextDirection RoboTextGlyphRunBaseDirection(const RoboTextGlyphRun *run)
{
    return run ? run->baseDirection : RoboTextDirectionUnknown;
}

const RoboTextFontContext *RoboTextGlyphRunFont(const RoboTextGlyphRun *run)
{
    return run ? &run->font : NULL;
}

const RoboTextLanguage *RoboTextGlyphRunLanguage(const RoboTextGlyphRun *run)
{
    static const RoboTextLanguage empty = {{0}};

    return run ? &run->language : &empty;
}

RoboTextGlyphGenerationMode RoboTextGlyphRunGenerationMode(const RoboTextGlyphRun *run)
{
    return run ? run->mode : RoboTextGlyphGenerationNone;
}

RoboTextScalarIndex RoboTextGlyphRunScalarCount(const RoboTextGlyphRun *run)
{
    return run ? RoboTextRangeLength(run->source) : 0u;
}

size_t RoboTextGlyphRunMissingGlyphs(const RoboTextGlyphRun *run)
{
    return run ? run->missing : 0u;
}

float RoboTextGlyphRunAdvance(const RoboTextGlyphRun *run)
{
    return run ? (float)run->advanceTotal : 0.0f;
}

Result RoboTextGlyphRunAdvanceUpTo(const RoboTextGlyphRun *run,
                                   RoboTextGlyphIndex index,
                                   float *outPx,
                                   RoboTextDiag *diag)
{
    double total = 0.0;

    if (!run || !outPx) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a glyph run and an out advance are required");
    }
    if ((size_t)index > run->count) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "an index past the end of the run has no pen position");
    }
    /*
     * Recomputed rather than cached per glyph, and the reason is the contract: this is the prefix sum the
     * *painter* accumulates, and summing here in the same order with the same accumulation width is what
     * makes the two provably equal. A cached prefix would be a second computation to keep in step.
     */
    for (size_t step = 0u; step < (size_t)index; step++) {
        total += (double)run->glyphs[step].advanceX;
    }
    *outPx = (float)total;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Source mapping                                                      */
/* ------------------------------------------------------------------ */

Result RoboTextGlyphRunSourceOf(const RoboTextGlyphRun *run,
                                RoboTextGlyphIndex index,
                                RoboTextRange *out,
                                RoboTextDiag *diag)
{
    if (!run || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a glyph run and an out range are required");
    }
    *out = RoboTextRangeAt(0u);
    if ((size_t)index >= run->count) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a glyph index past the end of the run");
    }
    *out = run->glyphs[index].source;
    return RoboOk;
}

Result RoboTextGlyphRunGlyphsForScalar(const RoboTextGlyphRun *run,
                                       RoboTextScalarIndex scalar,
                                       RoboTextGlyphIndex *outFirstIndex,
                                       RoboTextGlyphIndex *outCount,
                                       RoboTextDiag *diag)
{
    if (!run || !outFirstIndex || !outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a glyph run and two out values are required");
    }
    *outFirstIndex = 0u;
    *outCount = 0u;
    if (!RoboTextRangeContains(run->source, scalar)) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a scalar outside the run has no glyphs in it");
    }
    /*
     * A scan, and it is a scan on purpose: the mapping is contiguous and monotone today, but after a
     * substitution a glyph's source may repeat, overlap or be skipped, and a binary search written against
     * today's shape would be quietly wrong then. The runs are short and the caller is a caret or a hit
     * test, not a per-frame inner loop over a document.
     */
    for (size_t index = 0u; index < run->count; index++) {
        if (!RoboTextRangeContains(run->glyphs[index].source, scalar)) {
            continue;
        }
        if (*outCount == 0u) {
            *outFirstIndex = (RoboTextGlyphIndex)index;
        }
        (*outCount)++;
    }
    return RoboOk;
}

Result RoboTextGlyphRunGlyphForScalarIndex(const RoboTextGlyphRun *run,
                                           RoboTextScalarIndex index,
                                           RoboTextGlyphIndex *outGlyphIndex,
                                           RoboTextDiag *diag)
{
    RoboTextGlyphIndex first = 0u;
    RoboTextGlyphIndex count = 0u;
    Result status;

    if (!run || !outGlyphIndex) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a glyph run and an out index are required");
    }
    *outGlyphIndex = 0u;
    status = RoboTextGlyphRunGlyphsForScalar(run, index, &first, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    if (count == 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotFound,
                               "no glyph covers that scalar");
    }
    *outGlyphIndex = first;
    return RoboOk;
}

Result RoboTextGlyphRunGetStats(const RoboTextGlyphRun *run, RoboTextGlyphStats *out)
{
    if (!run || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->glyphRuns = 1u;
    out->glyphs = run->count;
    out->missing = run->missing;
    out->mirrored = run->mirrored;
    out->controls = run->controls;
    out->advancePx = run->advanceTotal;
    for (size_t index = 0u; index < run->count; index++) {
        const RoboTextGlyph *glyph = &run->glyphs[index];

        if (RoboTextRangeLength(glyph->source) > 1u) {
            out->multiScalarSources++;
        }
        if (glyph->ligatureComponents >= 2u) {
            out->ligatureGlyphs++;
            out->ligatureScalars += (size_t)RoboTextRangeLength(glyph->source);
        }
        if (glyph->offsetX != 0.0f || glyph->offsetY != 0.0f) {
            out->positioned++;
        }
    }
    return RoboOk;
}

Result RoboTextGlyphWrite(const RoboTextGlyph *glyph, char *buffer, size_t capacity, size_t *outLength)
{
    char line[192];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!glyph || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    length = RoboTextWriterText(line, sizeof(line), length, "id=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->glyphId);
    length = RoboTextWriterText(line, sizeof(line), length, " scalar=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->source.start);
    length = RoboTextWriterText(line, sizeof(line), length, "..");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->source.end);
    length = RoboTextWriterText(line, sizeof(line), length, " cluster=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->cluster);
    length = RoboTextWriterText(line, sizeof(line), length, " advance=");
    length = RoboTextWriterFloat(line, sizeof(line), length, glyph->advanceX);
    length = RoboTextWriterText(line, sizeof(line), length, " units=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, glyph->advanceUnits);
    length = RoboTextWriterText(line, sizeof(line), length, " offset=");
    length = RoboTextWriterFloat(line, sizeof(line), length, glyph->offsetX);
    length = RoboTextWriterText(line, sizeof(line), length, ",");
    length = RoboTextWriterFloat(line, sizeof(line), length, glyph->offsetY);
    length = RoboTextWriterText(line, sizeof(line), length, " mapped=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->mapped);
    /* How many glyphs this one was substituted from: two or more is a ligature, and a dump that showed only
     * `scalar=0..3` could not tell a ligature from a decomposition. */
    length = RoboTextWriterText(line, sizeof(line), length, " from=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->ligatureComponents);
    length = RoboTextWriterText(line, sizeof(line), length, " mirrored=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->mirrored);
    /* The bidi-control flag, because it is the reason a glyph with `id=0` must not be drawn. */
    length = RoboTextWriterText(line, sizeof(line), length, " control=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->control);
    length = RoboTextWriterText(line, sizeof(line), length, " metrics=");
    length = RoboTextWriterText(line, sizeof(line), length,
                                glyph->metricsStatus == RoboFontGlyphMetricsOk         ? "ok"
                                : glyph->metricsStatus == RoboFontGlyphMetricsInvalidId ? "invalid-id"
                                : glyph->metricsStatus == RoboFontGlyphMetricsMalformed ? "malformed"
                                                                                       : "unavailable");
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }

    if (length + 1u > capacity) {
        /* Nothing was written to the caller's buffer (it holds the empty string): a truncated dump reads
         * as a different glyph, which is the one thing a dump must not do. */
        if (outLength) {
            *outLength = length;
        }
        return RoboNotSupported;
    }
    memcpy(buffer, line, length + 1u);
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

Result RoboTextFuzzGlyphs(const uint8_t *data, size_t size)
{
    RoboTextView view;
    RoboTextRunSet *set = NULL;
    RoboTextRunStats stats;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    view = RoboTextViewMake((const char *)data, size);
    /*
     * No face, so every text run's glyph generation would refuse — and the *segmentation* is still
     * exercised, which is the point: the decoder, the neutrality pass and the run walks all run over
     * attacker-controlled bytes with a font context whose face is NULL, and none of them may read a scalar
     * index out of range. Generating with a face is `RoboTextFuzzShape`'s job in the suite, through a real
     * shipped face.
     */
    if (RoboTextRunSetCreate(view, NULL, NULL, &set, NULL) != RoboOk) {
        return RoboError;
    }
    (void)RoboTextRunSetGetStats(set, &stats);
    for (size_t index = 0u; index < RoboTextRunSetTextRunCount(set); index++) {
        const RoboTextRun *run = RoboTextRunSetTextRun(set, index);
        RoboTextGlyphRun *glyphs = NULL;

        if (RoboTextGlyphRunGenerate(view, run, &glyphs, NULL) != RoboOk) {
            continue; /* Expected: no face. The refusal is the tested behaviour. */
        }
        RoboTextGlyphRunDestroy(glyphs);
    }
    RoboTextRunSetDestroy(set);
    return RoboOk;
}
