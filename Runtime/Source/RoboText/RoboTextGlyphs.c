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
 *
 * ## 12.14: the run is finished, not just produced
 *
 * Generation used to end when the last glyph's advance was written. Since 12.14 it ends with
 * `RoboTextGlyphRunFinalize`, which derives the four records the glyph array implies: the **cluster** array
 * (the many-to-many mapping between scalars and glyphs), each glyph's **flag** word (which glyph begins a
 * cluster, which is a mark, which is a ligature, which may not begin a line), the run's **metrics** (so line
 * layout never asks the font parser again), and the run's own **totals**. The record is complete when the
 * function returns, and `RoboTextGlyphRunValidate` is the door that checks the four agree with the glyphs.
 *
 * The struct itself is in `RoboTextGlyphsInternal.h`, because `RoboTextRunList.c` merges two adjacent runs and
 * cannot do that through an accessor.
 */

#include "RoboText/RoboTextGlyphs.h"

#include "RoboMem/RoboMem.h"
#include "RoboText/RoboTextArabic.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextComplex.h"
#include "RoboText/RoboTextIndic.h"
#include "RoboText/RoboTextGpos.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboText/RoboTextUnicode.h"
#include "RoboTextGlyphsInternal.h"
#include "RoboTextInternal.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The generation mode                                                 */
/* ------------------------------------------------------------------ */

int RoboTextGlyphGenerationModeIsKnown(RoboTextGlyphGenerationMode mode)
{
    return mode == RoboTextGlyphGenerationNone || mode == RoboTextGlyphGenerationCmapPerScalar ||
           mode == RoboTextGlyphGenerationGsub || mode == RoboTextGlyphGenerationGpos;
}

const char *RoboTextGlyphGenerationModeName(RoboTextGlyphGenerationMode mode)
{
    switch (mode) {
        case RoboTextGlyphGenerationCmapPerScalar:
            return "cmap-per-scalar";
        case RoboTextGlyphGenerationGsub:
            return "cmap+gsub";
        case RoboTextGlyphGenerationGpos:
            return "cmap+gsub+gpos";
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
           a->codePoint == b->codePoint && a->cluster == b->cluster &&
           a->advanceUnits == b->advanceUnits &&
           a->advanceX == b->advanceX && a->advanceY == b->advanceY && a->offsetX == b->offsetX &&
           a->offsetY == b->offsetY && a->advanceAdjustX == b->advanceAdjustX &&
           a->advanceAdjustY == b->advanceAdjustY && a->offsetAdjustX == b->offsetAdjustX &&
           a->offsetAdjustY == b->offsetAdjustY && a->mapped == b->mapped &&
           a->control == b->control && a->mirrored == b->mirrored &&
           a->substituted == b->substituted && a->positioned == b->positioned &&
           a->ligatureComponents == b->ligatureComponents && a->flags == b->flags &&
           a->metricsStatus == b->metricsStatus;
}

/* ------------------------------------------------------------------ */
/* Glyph flags (Phase 12.14.22)                                        */
/* ------------------------------------------------------------------ */

int RoboTextGlyphFlagIsKnown(RoboTextGlyphFlag flag)
{
    switch (flag) {
        case RoboTextGlyphFlagNone:
        case RoboTextGlyphFlagClusterStart:
        case RoboTextGlyphFlagClusterContinuation:
        case RoboTextGlyphFlagMark:
        case RoboTextGlyphFlagLigature:
        case RoboTextGlyphFlagUnsafeToBreak:
        case RoboTextGlyphFlagUnsafeToConcat:
            return 1;
        default:
            return 0;
    }
}

const char *RoboTextGlyphFlagName(RoboTextGlyphFlag flag)
{
    switch (flag) {
        case RoboTextGlyphFlagNone:
            return "none";
        case RoboTextGlyphFlagClusterStart:
            return "cluster-start";
        case RoboTextGlyphFlagClusterContinuation:
            return "cluster-continuation";
        case RoboTextGlyphFlagMark:
            return "mark";
        case RoboTextGlyphFlagLigature:
            return "ligature";
        case RoboTextGlyphFlagUnsafeToBreak:
            return "unsafe-to-break";
        case RoboTextGlyphFlagUnsafeToConcat:
            return "unsafe-to-concat";
        default:
            return "unknown";
    }
}

/*
 * The flags a word is made of, in the order the enum lists them and never in the order they were set. A dump
 * whose field order depended on which flag was set first would print two equal words differently, and a test
 * that compared dumps would then be testing the setter rather than the value.
 */
/** The one way out of @ref RoboTextGlyphFlagsWrite when the word does not fit: empty buffer, no length. */
static Result FlagsWriteOverflow(size_t *outLength)
{
    if (outLength) {
        *outLength = 0u;
    }
    return RoboTextDiagMsg(NULL, RoboTextDomainGlyph, RoboNotSupported,
                           "the flags word does not fit the caller's buffer");
}

static const RoboTextGlyphFlag GlyphFlagOrder[] = {
    RoboTextGlyphFlagClusterStart,
    RoboTextGlyphFlagClusterContinuation,
    RoboTextGlyphFlagMark,
    RoboTextGlyphFlagLigature,
    RoboTextGlyphFlagUnsafeToBreak,
    RoboTextGlyphFlagUnsafeToConcat
};

Result RoboTextGlyphFlagsWrite(RoboTextGlyphFlags flags,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength)
{
    size_t length = 0u;
    int any = 0;

    if (!buffer || capacity == 0u) {
        return RoboTextDiagMsg(NULL, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a flags word needs a buffer");
    }
    buffer[0] = '\0';
    if (outLength) {
        *outLength = 0u;
    }

    for (size_t index = 0u; index < sizeof(GlyphFlagOrder) / sizeof(GlyphFlagOrder[0]); index++) {
        RoboTextGlyphFlags bit = (RoboTextGlyphFlags)GlyphFlagOrder[index];
        const char *name;

        if ((flags & bit) == 0u) {
            continue;
        }
        name = RoboTextGlyphFlagName(GlyphFlagOrder[index]);
        if (any) {
            length = RoboTextWriterText(buffer, capacity, length, "|");
            if (length == RoboTextWriterOverflow) {
                break;
            }
        }
        length = RoboTextWriterText(buffer, capacity, length, name);
        if (length == RoboTextWriterOverflow) {
            break;
        }
        any = 1;
    }
    if (length == RoboTextWriterOverflow) {
        /*
         * Abandon the word, like every dump in this module: a prefix of a flags word reads as a *different*
         * set of flags, which is the one thing a dump must not do. The buffer is left as the empty string so
         * that a caller which ignores the status prints nothing rather than a half-word.
         */
        return FlagsWriteOverflow(outLength);
    }
    if (!any) {
        length = RoboTextWriterText(buffer, capacity, length, "@");
        if (length == RoboTextWriterOverflow) {
            return FlagsWriteOverflow(outLength);
        }
    }
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Substitution (Phase 12.10)                                          */
/* ------------------------------------------------------------------ */

/**
 * The glyph run as a shaping buffer: one entry per glyph slot, in source order.
 *
 * A control is carried and flagged rather than dropped, because its slot is what keeps every later glyph's
 * index agreeing with its scalar; `RoboTextOtSkipped` is where the flag does its work.
 */
static Result ShapeBufferFromRun(const RoboTextGlyphRun *glyphRun,
                                RoboTextShapingBuffer **out,
                                RoboTextDiag *diag)
{
    RoboTextShapingBuffer *buffer = RoboTextShapingBufferCreate(glyphRun->count);

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
        shaping.codePoint = glyph->codePoint;
        shaping.cluster = glyph->cluster;
        shaping.bidiLevel = glyphRun->bidiLevel;
        if (glyph->control) {
            shaping.flags |= RoboTextShapingGlyphControl;
        }
        if (glyph->mirrored) {
            shaping.flags |= RoboTextShapingGlyphMirrored;
        }
        if (RoboTextShapingBufferAppend(buffer, &shaping) != RoboOk) {
            RoboTextShapingBufferDestroy(buffer);
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
static Result ShapeRunFromBuffer(RoboTextGlyphRun *glyphRun,
                                const RoboTextShapingBuffer *buffer,
                                const RoboTextRun *run,
                                RoboTextDiag *diag)
{
    size_t produced = RoboTextShapingBufferCount(buffer);
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
    glyphRun->positioned = 0u;
    glyphRun->advanceTotal = 0.0;

    for (size_t index = 0u; index < produced; index++) {
        const RoboTextShapingGlyph *shaping = RoboTextShapingBufferAt(buffer, index);
        RoboTextGlyph *glyph = &rebuilt[index];
        FontGlyphMetrics metrics;
        float advance = 0.0f;
        Result status;
        int mapped;

        RoboTextGlyphInit(glyph);
        glyph->glyphId = shaping->glyphId;
        glyph->source = shaping->source;
        /*
         * The character survives the rebuild, and is dropped to 0 exactly when the substitution gave the glyph
         * more than one scalar to stand for: a ligature's product is `ffi` and not the `f` it started as, and a
         * reader asking "what character is this" must get `no single character` rather than a plausible wrong
         * one. A single substitution and a multiple substitution both keep exactly one scalar, so for those the
         * character is still the character.
         */
        glyph->codePoint = RoboTextRangeLength(shaping->source) == 1u ? shaping->codePoint : 0u;
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
        if ((shaping->flags & RoboTextShapingGlyphPositioned) != 0u) {
            glyph->positioned = 1;
            glyphRun->positioned++;
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
        /*
         * The face's own advance **plus** whatever its GPOS said, and the placement as the offset the
         * rasterizer will apply. This is the one place the two are added, which is what makes a kerning pair
         * visible to the measurer (`advanceTotal` sums `advanceX`) and to the painter (it draws at
         * `offsetX`) without either of them knowing that kerning exists.
         */
        glyph->advanceX = advance + shaping->advanceAdjustX;
        glyph->advanceY = shaping->advanceAdjustY;
        glyph->offsetX = shaping->offsetAdjustX;
        glyph->offsetY = shaping->offsetAdjustY;
        /* The font's own part, kept beside the total rather than folded into it: see `advanceAdjustX`. */
        glyph->advanceAdjustX = shaping->advanceAdjustX;
        glyph->advanceAdjustY = shaping->advanceAdjustY;
        glyph->offsetAdjustX = shaping->offsetAdjustX;
        glyph->offsetAdjustY = shaping->offsetAdjustY;
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
 * Shape @p glyphRun in place: the face's GSUB, then the face's GPOS (Phases 12.10, 12.11).
 *
 * The order is the phase's, and it is not interchangeable: substitution changes *which* glyphs there are, so
 * positioning has to see its result — a ligature is one glyph with one advance to kern, and kerning the
 * three characters that made it would be kerning glyphs that no longer exist. Both stages resolve their
 * lookups against the same run (script, language, features) and read the same face tables, cached on the
 * face and immutable.
 *
 * The answers that are not a stage running: a face with no table, a table with no record for this run's
 * script, an empty plan and an empty run. Each leaves the cmap pass's result alone, and the mode stays
 * @ref RoboTextGlyphGenerationCmapPerScalar — a fact about the font rather than about the text.
 *
 * The buffer is built once, both engines mutate it, and the run is rebuilt once at the end. That is the
 * whole point of the intermediate: positioning writes adjustments onto the glyphs substitution produced,
 * and the rebuild is where an adjustment becomes an advance the pen and the measurer both see.
 */
/**
 * The strategy's position masks over @p run's range, or none when the script needs none.
 *
 * The one place a strategy's mask builder is called from, and the dispatch is by **strategy** rather than by
 * script: a script whose strategy is `None` asks nothing, a script whose strategy is Arabic or Indic asks its
 * own module, and the array is the caller's (`RoboFree` it) so the masks outlive the substitution stage they
 * were built for — the positioning stage reads the same words, which is what keeps a feature and its masks
 * describing one text.
 *
 * @param outMasks receives NULL when the script needs no masks.
 * @return RoboOk; whatever the strategy's builder answers, which is a refusal rather than a guess when the
 *         range is longer than the caller's array or the text cannot be decoded.
 */
static Result ShapeComplexMasks(RoboTextView view,
                                const RoboTextRun *run,
                                RoboTextComplexStrategy strategy,
                                uint32_t **outMasks,
                                size_t *outCount,
                                RoboTextDiag *diag)
{
    size_t scalars = RoboTextRangeLength(run->range);
    uint32_t *masks;
    size_t count = 0u;
    Result status;

    *outMasks = NULL;
    *outCount = 0u;
    if (strategy != RoboTextComplexStrategyArabic && strategy != RoboTextComplexStrategyIndic) {
        return RoboOk;
    }
    if (scalars == 0u) {
        return RoboOk;
    }
    masks = (uint32_t *)RoboCalloc(scalars, sizeof(*masks));
    if (!masks) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                               "the run's position masks could not be held");
    }
    if (strategy == RoboTextComplexStrategyArabic) {
        status = RoboTextArabicMasksBuild(view, run->range, masks, scalars, &count, diag);
    } else {
        status = RoboTextIndicMasksBuild(view, run->range, masks, scalars, &count, diag);
    }
    if (status != RoboOk) {
        RoboFree(masks);
        return status;
    }
    *outMasks = masks;
    *outCount = count;
    return RoboOk;
}

/**
 * Shape @p glyphRun in place: the face's GSUB, then the face's GPOS (Phases 12.10, 12.11).
 *
 * The order is the phase's, and it is not interchangeable: substitution changes *which* glyphs there are, so
 * positioning has to see its result — a ligature is one glyph with one advance to kern, and kerning the
 * three characters that made it would be kerning glyphs that no longer exist. Both stages resolve their
 * lookups against the same run (script, language, features) and read the same face tables, cached on the
 * face and immutable.
 *
 * The answers that are not a stage running: a face with no table, a table with no record for this run's
 * script, an empty plan and an empty run. Each leaves the cmap pass's result alone, and the mode stays
 * @ref RoboTextGlyphGenerationCmapPerScalar — a fact about the font rather than about the text.
 *
 * The buffer is built once, both engines mutate it, and the run is rebuilt once at the end. That is the
 * whole point of the intermediate: positioning writes adjustments onto the glyphs substitution produced,
 * and the rebuild is where an adjustment becomes an advance the pen and the measurer both see.
 *
 * ## The complex strategy, and what it adds to the line above
 *
 * A script whose plan has stages (Phase 12.19) does **not** run one GSUB plan: it runs the plan's stages in
 * order, with the strategy's reorder steps between them and the strategy's **position masks** handed to the
 * engine, so a feature with a position class (`init` here, `half` there) applies where the strategy said and
 * nowhere else. Everything else is unchanged, and that is deliberate: the same buffer, the same engines, and
 * a Latin run takes the single-plan path it always took, byte for byte.
 *
 * The masks outlive the substitution stage because positioning reads the same words: an Indic below-base
 * form is adjusted where it was substituted.
 */
static Result ShapeRun(RoboTextView view,
                       const RoboTextRun *run,
                       RoboTextGlyphRun *glyphRun,
                       RoboTextDiag *diag)
{
    const FontOtLayout *gsub = NULL;
    const FontOtLayout *gpos = NULL;
    const FontGdef *gdef = NULL;
    RoboTextOtPlan gsubPlan;
    RoboTextOtPlan gposPlan;
    RoboTextComplexPlan complex;
    RoboTextShapingBuffer *buffer = NULL;
    uint32_t *masks = NULL;
    size_t maskCount = 0u;
    Result status;
    int substitutes = 0;
    int positions = 0;
    int staged = 0;

    RoboTextOtPlanInit(&gsubPlan);
    RoboTextOtPlanInit(&gposPlan);
    RoboTextComplexPlanInit(&complex);

    status = RoboTextComplexPlanResolve(run->script, &run->features, &complex, diag);
    if (status != RoboOk) {
        return status;
    }
    staged = RoboTextComplexPlanHasStages(&complex) ? 1 : 0;

    status = RoboTextFontGsub(&run->font, &gsub, diag);
    if (status != RoboOk) {
        return status;
    }
    if (gsub && !staged) {
        status = RoboTextGsubPlanResolve(gsub, run->script, &run->language, &run->features, &gsubPlan, diag);
        if (status != RoboOk) {
            return status;
        }
        substitutes = (gsubPlan.empty || glyphRun->count == 0u) ? 0 : 1;
    } else if (gsub && staged && glyphRun->count != 0u) {
        substitutes = 1;
    }
    status = RoboTextFontGpos(&run->font, &gpos, diag);
    if (status != RoboOk) {
        return status;
    }
    if (gpos) {
        status = RoboTextGposPlanResolve(gpos, run->script, &run->language, &run->features, &gposPlan, diag);
        if (status != RoboOk) {
            return status;
        }
        positions = (gposPlan.empty || glyphRun->count == 0u) ? 0 : 1;
    }
    if (!substitutes && !positions) {
        return RoboOk;
    }

    status = RoboTextFontGdef(&run->font, &gdef, diag);
    if (status != RoboOk) {
        return status;
    }
    status = ShapeBufferFromRun(glyphRun, &buffer, diag);
    if (status != RoboOk) {
        return status;
    }
    if (staged) {
        status = ShapeComplexMasks(view, run, complex.strategy, &masks, &maskCount, diag);
        if (status != RoboOk) {
            RoboTextShapingBufferDestroy(buffer);
            return status;
        }
    }

    if (substitutes && staged) {
        for (size_t stageIndex = 0u; stageIndex < complex.stageCount; stageIndex++) {
            const RoboTextComplexStage *stage = RoboTextComplexPlanStage(&complex, stageIndex);
            RoboTextFeatureSet stageFeatures;
            RoboTextOtPlan stagePlan;
            RoboTextGsubOptions options;
            RoboTextGsubStats stats;

            if (!stage) {
                continue;
            }
            RoboTextOtPlanInit(&stagePlan);
            status = RoboTextComplexStageFeatureSet(stage, &stageFeatures, diag);
            if (status == RoboOk) {
                /*
                 * **No defaults**: this stage's feature set is the whole of what it runs. Handing GSUB's own
                 * defaults in would let `liga` run in every stage, which is how an Indic conjunct comes to be
                 * half-formed twice — the plan's stages are the complete answer, not a delta on one.
                 */
                status = RoboTextOtPlanResolve(gsub, run->script, &run->language, &stageFeatures, NULL,
                                               &stagePlan, diag);
            }
            if (status == RoboOk) {
                status = RoboTextComplexStageApplyMasks(&complex, stage, &stagePlan);
            }
            if (status == RoboOk && stage->reorderBefore != RoboTextComplexReorderNone) {
                status = RoboTextComplexReorderApply(&complex, stage->reorderBefore, buffer, view, run->range,
                                                     diag);
            }
            if (status != RoboOk) {
                RoboFree(masks);
                RoboTextShapingBufferDestroy(buffer);
                return status;
            }
            memset(&options, 0, sizeof(options));
            options.direction = run->direction;
            options.bidiLevel = glyphRun->bidiLevel;
            options.positionMasks = masks;
            options.positionMaskCount = maskCount;
            options.maskBase = run->range.start;
            status = RoboTextGsubApply(gsub, gdef, &stagePlan, &options, buffer, &stats, diag);
            if (status != RoboOk) {
                RoboFree(masks);
                RoboTextShapingBufferDestroy(buffer);
                return status;
            }
            if (stage->reorderAfter != RoboTextComplexReorderNone) {
                status = RoboTextComplexReorderApply(&complex, stage->reorderAfter, buffer, view, run->range,
                                                     diag);
                if (status != RoboOk) {
                    RoboFree(masks);
                    RoboTextShapingBufferDestroy(buffer);
                    return status;
                }
            }
        }
        glyphRun->mode = RoboTextGlyphGenerationGsub;
    } else if (substitutes) {
        RoboTextGsubOptions options;
        RoboTextGsubStats stats;

        memset(&options, 0, sizeof(options));
        options.direction = run->direction;
        options.bidiLevel = glyphRun->bidiLevel;
        status = RoboTextGsubApply(gsub, gdef, &gsubPlan, &options, buffer, &stats, diag);
        if (status != RoboOk) {
            RoboFree(masks);
            RoboTextShapingBufferDestroy(buffer);
            return status;
        }
        /* A plan resolved and ran, whatever it did: that is what `Gsub` records, and it is why a run of text
         * a font substitutes nothing in still reports the mode honestly. */
        glyphRun->mode = RoboTextGlyphGenerationGsub;
    }
    if (positions) {
        RoboTextGposOptions options;
        RoboTextGposStats stats;
        float unitScale = 0.0f;

        memset(&options, 0, sizeof(options));
        options.direction = run->direction;
        options.bidiLevel = glyphRun->bidiLevel;
        /*
         * The face context the **attachments** need (Phase 12.19): a cursive or mark attachment places a
         * glyph against another glyph's pen, and the pen's distance is the face's advance — the one quantity
         * GPOS does not carry. Kerning and single adjustments do not read it.
         */
        options.font = &run->font;
        options.positionMasks = masks;
        options.positionMaskCount = maskCount;
        options.maskBase = run->range.start;
        /*
         * The device scale, asked of the module's one scaling door: the engine multiplies by this rather than
         * scaling a font-unit number itself, so `RoboTextFontUnitsToDevice` stays the only place a font unit
         * becomes a pixel. The ppem is the context's own size rounded — the size a device table is recorded
         * for — and a face asked at a fractional size gets the nearest whole one, which is what device
         * corrections are defined against.
         */
        if (RoboTextFontUnitsToDevice(&run->font, 1.0, &unitScale, diag) != RoboOk) {
            unitScale = 0.0f;
        }
        options.deviceScale = unitScale;
        options.devicePpem = (uint16_t)(run->font.style.sizePx + 0.5f);
        status = RoboTextGposApply(gpos, gdef, &gposPlan, &options, buffer, &stats, diag);
        if (status != RoboOk) {
            RoboFree(masks);
            RoboTextShapingBufferDestroy(buffer);
            return status;
        }
        /* The furthest stage that ran, which is what the mode reports. */
        glyphRun->mode = RoboTextGlyphGenerationGpos;
    }

    RoboFree(masks);
    status = ShapeRunFromBuffer(glyphRun, buffer, run, diag);
    RoboTextShapingBufferDestroy(buffer);
    return status;
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
    RoboFree(run->clusters);
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
    glyphRun->features = run->features;
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
        /*
         * The character, recorded here because this is the one place it is at hand: the decode above already
         * produced @p scalar, and Phase 12.10's rebuild runs after the text is out of scope.
         */
        glyph->codePoint = (RoboTextCodePoint)scalar;
        /*
         * The cluster, still the identity mapping at this stage — and deliberately still, because the shaping
         * buffer carries this field and Phase 12.10's ligature work reads it while the glyphs are still one per
         * scalar. `RoboTextGlyphRunFinalize` replaces it with the run's own cluster ordinal once substitution
         * has settled which glyphs exist.
         */
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
        /* `advanceY`, `offsetX` and `offsetY` stay zero *here*, at the map stage: the face's GPOS writes them
         * when the run is positioned, a few lines below, and a value written here would be a number the font
         * never gave. */
        if (!mapped || metrics.status != RoboFontGlyphMetricsOk) {
            glyphRun->missing++;
        }
        glyphRun->advanceTotal += (double)glyph->advanceX;
    }

    glyphRun->count = (size_t)length;
    status = ShapeRun(view, run, glyphRun, diag);
    if (status != RoboOk) {
        RoboTextGlyphRunDestroy(glyphRun);
        return status;
    }
    /*
     * The run is not finished until this runs: the clusters, the flags, the metrics and the totals are all
     * functions of the glyph array *after* substitution and positioning, and computing them before `ShapeRun`
     * would derive them from a glyph array the face had not finished choosing.
     */
    status = RoboTextGlyphRunFinalize(glyphRun, diag);
    if (status != RoboOk) {
        RoboTextGlyphRunDestroy(glyphRun);
        return status;
    }
    *out = glyphRun;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Finalizing (Phase 12.14)                                            */
/* ------------------------------------------------------------------ */

/**
 * The scalar a glyph's source begins at, as a mark test: a glyph is a **mark** when its source is exactly one
 * scalar and that scalar is a combining mark.
 *
 * One scalar, not "any scalar", and the distinction is the reason the flag is trustworthy: a glyph that
 * folded a base and its accent together is not a mark — it is the *product* of mark attachment, and calling it
 * one would make a line breaker willing to split before it. `RoboTextUnicodeIsCombiningMark` is Phase 4's
 * answer and the module keeps no category table of its own (Phase 12.5).
 */
static int GlyphIsMark(const RoboTextGlyph *glyph)
{
    if (glyph->control || RoboTextRangeLength(glyph->source) != 1u) {
        return 0;
    }
    /*
     * The **character**, not the index. `source.start` addresses the text; asking a category table about a
     * scalar *index* answers about whichever character happens to live at that offset, so a mark on the first
     * line would be found and the same mark on the tenth would not. The length check above is what makes
     * @ref RoboTextGlyph::codePoint this glyph's sole character, and a glyph standing for several has 0 there.
     */
    return glyph->codePoint != 0u && RoboTextUnicodeIsCombiningMark(glyph->codePoint);
}

/**
 * Does the glyph at @p index continue the cluster that @p cluster covers?
 *
 * The rule, and the four cases it is written for:
 *
 * | glyphs | answer | because |
 * |---|---|---|
 * | `1 scalar → 1 glyph` | new cluster | its source begins where the last one ended |
 * | `1 scalar → many glyphs` | continues | its source *starts inside* the cluster's source |
 * | `many scalars → 1 glyph` | new cluster | one glyph, one cluster, however many scalars |
 * | a combining mark | continues | a mark belongs to the cluster of the base it attaches to |
 *
 * The mark rule is the one that is not a property of the ranges, and it is stated separately for that reason:
 * Unicode's grapheme clusters (UAX #29) put a mark with its base, and Phase 4 exposes the mark predicate
 * rather than a grapheme segmenter. So the approximation is exactly "a mark attaches left", which is correct
 * for the scripts this build shapes and is *named* rather than implied — a grapheme-cluster goal that adds
 * `Regional_Indicator` pairing or a spacing-mark rule extends this function.
 */
static int GlyphContinuesCluster(const RoboTextGlyph *glyph, const RoboTextGlyph *previous, int isMark)
{
    if (isMark) {
        return 1;
    }
    /* A source that begins before the previous glyph's ends is inside the same cluster: a decomposition, or a
     * glyph a font drew from part of the same scalar. */
    return glyph->source.start < previous->source.end;
}

Result RoboTextGlyphRunFinalize(RoboTextGlyphRun *run, RoboTextDiag *diag)
{
    RoboTextGlyphCluster *clusters = NULL;
    size_t clusterCount = 0u;
    size_t marks = 0u;
    size_t unsafeToBreak = 0u;
    double totalX = 0.0;
    double totalY = 0.0;
    Result status;

    if (!run) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run is required to finalize");
    }

    /*
     * The flags first, because the cluster pass reads the mark flag and the flag pass reads the clusters' own
     * answer for "which glyph is first in its cluster" — which is what the cluster pass computes. So the
     * order inside one pass below is: mark, cluster membership, then the two unsafe flags, which read both.
     */
    if (run->count != 0u) {
        clusters = (RoboTextGlyphCluster *)RoboCalloc(run->count, sizeof(*clusters));
        if (!clusters) {
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                                   "the cluster array could not be allocated");
        }
    }

    for (size_t index = 0u; index < run->count; index++) {
        RoboTextGlyph *glyph = &run->glyphs[index];
        int isMark = GlyphIsMark(glyph);
        int continues = 0;

        if (isMark) {
            glyph->flags |= (RoboTextGlyphFlags)RoboTextGlyphFlagMark;
            marks++;
        }
        if (glyph->ligatureComponents >= 2u) {
            glyph->flags |= (RoboTextGlyphFlags)RoboTextGlyphFlagLigature;
        }
        if (index != 0u) {
            continues = GlyphContinuesCluster(glyph, &run->glyphs[index - 1u], isMark);
        }
        if (!continues) {
            clusters[clusterCount].source = glyph->source;
            clusters[clusterCount].glyphStart = (RoboTextGlyphIndex)index;
            clusters[clusterCount].glyphEnd = (RoboTextGlyphIndex)index;
            clusterCount++;
        }
        glyph->flags |= (RoboTextGlyphFlags)(continues ? RoboTextGlyphFlagClusterContinuation
                                                      : RoboTextGlyphFlagClusterStart);
        /*
         * The cluster **index**, in the field that has meant "which cluster does this glyph belong to" since
         * 12.4. It used to be the glyph's own source start (the identity mapping, and the header said so); it
         * is now the index of the cluster the glyph's grouping rules put it in, which is the value a selection
         * walk wants and the reason the identity mapping was called "useless for selection" rather than wrong.
         */
        glyph->cluster = (RoboTextClusterIndex)(clusterCount - 1u);
        clusters[clusterCount - 1u].glyphEnd = (RoboTextGlyphIndex)(index + 1u);
        /*
         * The cluster's source is the **union** of its glyphs' sources, so it is extended and never replaced:
         * assigning `Make(start, end)` outright would *shrink* a cluster whose later glyph ended before the one
         * that opened it.
         *
         * Both ends are extended, because since Phase 12.19 a glyph's source may start **before** the glyph
         * that opened its cluster: a complex script's reorder step moves a glyph — Devanagari's pre-base vowel
         * sign is written after its consonant and shapes before it — and the syllable's cluster then holds a
         * glyph whose source is later and a glyph whose source is earlier. The text's order is not the
         * shaper's to change (§7), so the cluster covers the union of what its glyphs came from rather than
         * the span the first of them happened to start at, and `source.start` is what makes scalar 0 of that
         * syllable belong to a cluster at all.
         */
        if (glyph->source.end > clusters[clusterCount - 1u].source.end) {
            clusters[clusterCount - 1u].source.end = glyph->source.end;
        }
        if (glyph->source.start < clusters[clusterCount - 1u].source.start) {
            clusters[clusterCount - 1u].source.start = glyph->source.start;
        }
    }

    /*
     * The two unsafe flags, and what each honestly claims.
     *
     * `UnsafeToBreak` — **a line may not begin here.** True for a glyph that continues a cluster (breaking
     * before it would separate a mark from its base, or a ligature's part from the rest of it), and for a
     * glyph a font positioned relative to its neighbour (a GPOS placement is an attachment, so the pairing is
     * a unit). What it does **not** claim: that every boundary it leaves unset is safe. Line breaking needs
     * script-specific rules this phase has no vocabulary for.
     *
     * `UnsafeToConcat` — **a shape run may not be cut here.** True for a glyph a ligature produced and for a
     * glyph the font positioned: shaping the text on either side of it separately could resolve different
     * lookups, because a contextual lookup's context reaches across the boundary. Set on the glyph *before*
     * the boundary, so a caller cutting after the last glyph carrying it is cutting where the phase says.
     */
    for (size_t index = 0u; index < run->count; index++) {
        RoboTextGlyph *glyph = &run->glyphs[index];
        int attached = (glyph->offsetAdjustX != 0.0f || glyph->offsetAdjustY != 0.0f);

        if (index != 0u &&
            (glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagClusterContinuation) != 0u) {
            glyph->flags |= (RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToBreak;
        }
        if (attached && index != 0u) {
            glyph->flags |= (RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToBreak;
        }
        if ((glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToBreak) != 0u) {
            unsafeToBreak++;
        }
        if ((glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagLigature) != 0u || attached) {
            glyph->flags |= (RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToConcat;
        }
        totalX += (double)glyph->advanceX;
        totalY += (double)glyph->advanceY;
    }

    run->clusters = clusters;
    run->clusterCount = clusterCount;
    run->marks = marks;
    run->unsafeToBreak = unsafeToBreak;
    run->advanceTotal = totalX;
    run->advanceTotalY = totalY;
    run->ligatures = 0u;
    for (size_t index = 0u; index < run->count; index++) {
        if ((run->glyphs[index].flags & (RoboTextGlyphFlags)RoboTextGlyphFlagLigature) != 0u) {
            run->ligatures++;
        }
    }

    /*
     * The metrics, asked of the font context's own door rather than read from a face here: the same call the
     * measurement makes, so a line box built from a run's metrics and one built from the measurement agree by
     * construction. A face that cannot be asked is recorded as *not asked* (`haveMetrics`), never silently as
     * a zero ascent — the distinction matters because a zero ascent is a real (if useless) metric.
     */
    memset(&run->metrics, 0, sizeof(run->metrics));
    run->haveMetrics = 0;
    status = RoboTextFontContextMetrics(&run->font, &run->metrics, diag);
    if (status != RoboOk) {
        /*
         * Only one failure reaches here — a context with no face — and it is returned rather than recorded as
         * absent metrics, because the two states a caller could then not tell apart are "this face declares no
         * metrics" (a documented estimate, `fromFace == 0`) and "there is no face at all". Generation has
         * already refused the second before it allocated anything; this is the same answer at the same door.
         */
        return status;
    }
    run->haveMetrics = 1;
    return RoboOk;
}

Result RoboTextGlyphRunConcat(const RoboTextGlyphRun *first,
                              const RoboTextGlyphRun *second,
                              RoboTextGlyphRun **out,
                              RoboTextDiag *diag)
{
    RoboTextGlyphRun *merged;
    size_t total;
    Result status;

    if (!first || !second || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "two runs and an out pointer are required to merge");
    }
    *out = NULL;
    if (!first->font.face || !second->font.face) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run with no font context cannot be merged");
    }
    if (first->source.end != second->source.start) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "two runs can only be merged when their sources are contiguous");
    }
    if (first->script != second->script || first->direction != second->direction ||
        first->bidiLevel != second->bidiLevel || first->baseDirection != second->baseDirection ||
        first->mode != second->mode ||
        !RoboTextFontContextEqual(&first->font, &second->font) ||
        !RoboTextLanguageEqual(&first->language, &second->language) ||
        !RoboTextFeatureSetEqual(&first->features, &second->features)) {
        /*
         * Reported rather than repaired: a merge of two runs that disagreed about their script or their level
         * would produce a record whose own key is false, and every later reader of that key — the cache, the
         * coalescer, a diagnostic — would be reading a lie. The caller asks `RoboTextGlyphRunKeyEqual` first;
         * this is the second door of the same question, at the point where the answer becomes a record.
         */
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "two runs that disagree about their shaping state cannot be merged");
    }

    total = first->count + second->count;
    merged = (RoboTextGlyphRun *)RoboCalloc(1u, sizeof(*merged));
    if (!merged) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                               "the merged run could not be allocated");
    }
    if (total != 0u) {
        merged->glyphs = (RoboTextGlyph *)RoboCalloc(total, sizeof(*merged->glyphs));
        if (!merged->glyphs) {
            RoboTextGlyphRunDestroy(merged);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                                   "the merged glyph array could not be allocated");
        }
        if (first->count != 0u) {
            memcpy(merged->glyphs, first->glyphs, first->count * sizeof(*merged->glyphs));
        }
        if (second->count != 0u) {
            memcpy(merged->glyphs + first->count, second->glyphs,
                   second->count * sizeof(*merged->glyphs));
        }
    }
    merged->count = total;
    merged->source = RoboTextRangeMake(first->source.start, second->source.end);
    merged->script = first->script;
    merged->direction = first->direction;
    merged->bidiLevel = first->bidiLevel;
    merged->baseDirection = first->baseDirection;
    merged->font = first->font;
    merged->language = first->language;
    merged->features = first->features;
    merged->mode = first->mode;
    /*
     * The counters that are *not* functions of the glyph array are the two the glyphs cannot restate: a
     * missing glyph is `mapped` plus the metrics status, and both travel with the glyph, so the merge sums
     * them. The rest — the ligature count, the marks, the two unsafe flags — are derived below from the
     * concatenated glyphs, and deriving them here would be a second implementation of `Finalize`.
     */
    merged->missing = first->missing + second->missing;
    merged->mirrored = first->mirrored + second->mirrored;
    merged->controls = first->controls + second->controls;
    merged->substituted = first->substituted + second->substituted;
    merged->positioned = first->positioned + second->positioned;

    status = RoboTextGlyphRunFinalize(merged, diag);
    if (status != RoboOk) {
        RoboTextGlyphRunDestroy(merged);
        return status;
    }
    *out = merged;
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

/**
 * Glyphs the face's GPOS positioned in @p run (Phase 12.11).
 *
 * The count of glyphs a value record touched — which includes a pair whose adjustment the font recorded as
 * zero, because "the font said this pair is 0" is a font that positioned the pair and not a font that had
 * nothing to say.
 */
size_t RoboTextGlyphRunPositioned(const RoboTextGlyphRun *run)
{
    return run ? run->positioned : 0u;
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

/* ------------------------------------------------------------------ */
/* Clusters, flags, metrics (Phase 12.14)                              */
/* ------------------------------------------------------------------ */

size_t RoboTextGlyphRunClusterCount(const RoboTextGlyphRun *run)
{
    return run ? run->clusterCount : 0u;
}

const RoboTextGlyphCluster *RoboTextGlyphRunCluster(const RoboTextGlyphRun *run, RoboTextClusterIndex index)
{
    if (!run || (size_t)index >= run->clusterCount) {
        return NULL;
    }
    return &run->clusters[index];
}

Result RoboTextGlyphRunClusterForScalar(const RoboTextGlyphRun *run,
                                        RoboTextScalarIndex scalar,
                                        RoboTextClusterIndex *outCluster,
                                        RoboTextDiag *diag)
{
    if (!run || !outCluster) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run and an out pointer are required");
    }
    if (!RoboTextRangeContains(run->source, scalar)) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "that scalar is outside the run's source range");
    }
    /*
     * A walk rather than a binary search: a cluster array is at most one entry per scalar, a caller asking this
     * question is answering a click, and the invariant that makes a binary search valid (ascending, contiguous
     * clusters) is one `RoboTextGlyphRunValidate` proves rather than one this trusts. O(clusters), and stated.
     */
    for (size_t index = 0u; index < run->clusterCount; index++) {
        if (RoboTextRangeContains(run->clusters[index].source, scalar)) {
            *outCluster = (RoboTextClusterIndex)index;
            return RoboOk;
        }
    }
    return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboNotFound,
                           "no cluster covers that scalar");
}

size_t RoboTextGlyphRunMarks(const RoboTextGlyphRun *run)
{
    return run ? run->marks : 0u;
}

size_t RoboTextGlyphRunUnsafeToBreak(const RoboTextGlyphRun *run)
{
    return run ? run->unsafeToBreak : 0u;
}

Result RoboTextGlyphRunMetrics(const RoboTextGlyphRun *run, RoboTextFontMetrics *out)
{
    if (!run || !out) {
        return RoboTextDiagMsg(NULL, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run and an out pointer are required");
    }
    *out = run->metrics;
    if (!run->haveMetrics) {
        /*
         * A zeroed record whose `fromFace` is zero: the same "not available" a font context reports, and never
         * a zero ascent passed off as a measurement. `RoboTextGlyphRunFinalize` writes the flag, so this cannot
         * disagree with how the metrics were obtained.
         */
        memset(out, 0, sizeof(*out));
    }
    return RoboOk;
}

float RoboTextGlyphRunAscent(const RoboTextGlyphRun *run)
{
    return run ? run->metrics.ascentPx : 0.0f;
}

float RoboTextGlyphRunDescent(const RoboTextGlyphRun *run)
{
    return run ? run->metrics.descentPx : 0.0f;
}

float RoboTextGlyphRunLineGap(const RoboTextGlyphRun *run)
{
    return run ? run->metrics.lineGapPx : 0.0f;
}

float RoboTextGlyphRunLineHeight(const RoboTextGlyphRun *run)
{
    return run ? run->metrics.lineHeightPx : 0.0f;
}

float RoboTextGlyphRunAdvanceY(const RoboTextGlyphRun *run)
{
    return run ? (float)run->advanceTotalY : 0.0f;
}

const RoboTextFeatureSet *RoboTextGlyphRunFeatures(const RoboTextGlyphRun *run)
{
    return run ? &run->features : NULL;
}

/* ------------------------------------------------------------------ */
/* Validation (Phase 12.14.37)                                         */
/* ------------------------------------------------------------------ */

int RoboTextGlyphRunProblemIsKnown(RoboTextGlyphRunProblem problem)
{
    return problem >= RoboTextGlyphRunProblemNone && problem <= RoboTextGlyphRunProblemDirection;
}

const char *RoboTextGlyphRunProblemName(RoboTextGlyphRunProblem problem)
{
    switch (problem) {
        case RoboTextGlyphRunProblemNone:
            return "none";
        case RoboTextGlyphRunProblemRange:
            return "range";
        case RoboTextGlyphRunProblemGlyphCount:
            return "glyph-count";
        case RoboTextGlyphRunProblemFont:
            return "font";
        case RoboTextGlyphRunProblemGlyphSource:
            return "glyph-source";
        case RoboTextGlyphRunProblemVector:
            return "vector";
        case RoboTextGlyphRunProblemClusterTiling:
            return "cluster-tiling";
        case RoboTextGlyphRunProblemClusterSource:
            return "cluster-source";
        case RoboTextGlyphRunProblemClusterOrder:
            return "cluster-order";
        case RoboTextGlyphRunProblemClusterIndex:
            return "cluster-index";
        case RoboTextGlyphRunProblemTotal:
            return "total";
        case RoboTextGlyphRunProblemCounter:
            return "counter";
        case RoboTextGlyphRunProblemDirection:
            return "direction";
        default:
            return "unknown";
    }
}

/**
 * @p value is a number paint may receive: Phase 12.14.37's "no NaN or infinity may reach paint".
 *
 * `v == v` and a magnitude bound rather than `isfinite`, because this file is compiled into the guest program
 * and the freestanding target has no `math.h` — the same check `RoboGraphicsText.c` and `RoboFontStyle.c` make
 * for the same reason, and the same three-lines-not-a-header decision they made.
 */
static int ValueIsFinite(float value)
{
    return (value == value) && (value <= 3.402823466e+38f) && (value >= -3.402823466e+38f);
}

/** See @ref ValueIsFinite: both components of a vector. */
static int VectorIsFinite(float x, float y)
{
    return ValueIsFinite(x) && ValueIsFinite(y);
}

Result RoboTextGlyphRunValidate(const RoboTextGlyphRun *run,
                                RoboTextGlyphRunProblem *outProblem,
                                RoboTextDiag *diag)
{
    RoboTextGlyphRunProblem problem = RoboTextGlyphRunProblemNone;
    double sum = 0.0;
    size_t marks = 0u;
    size_t ligatures = 0u;
    size_t unsafeToBreak = 0u;
    size_t missing = 0u;
    size_t mirrored = 0u;
    size_t controls = 0u;
    size_t substituted = 0u;
    size_t positioned = 0u;
    RoboTextScalarIndex covered = 0u;

    if (!run || !outProblem) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a run and an out pointer are required to validate");
    }
    *outProblem = RoboTextGlyphRunProblemNone;

    if (RoboTextRangeIsInverted(run->source)) {
        problem = RoboTextGlyphRunProblemRange;
    } else if (!run->font.face) {
        problem = RoboTextGlyphRunProblemFont;
    } else if ((run->count == 0u) != RoboTextRangeIsEmpty(run->source)) {
        problem = RoboTextGlyphRunProblemGlyphCount;
    } else if (run->count != 0u && !run->glyphs) {
        problem = RoboTextGlyphRunProblemGlyphCount;
    }

    for (size_t index = 0u; problem == RoboTextGlyphRunProblemNone && index < run->count; index++) {
        const RoboTextGlyph *glyph = &run->glyphs[index];

        if (!RoboTextRangeContains(run->source, glyph->source.start) ||
            glyph->source.end > run->source.end || RoboTextRangeIsEmpty(glyph->source)) {
            problem = RoboTextGlyphRunProblemGlyphSource;
            break;
        }
        if (!VectorIsFinite(glyph->advanceX, glyph->advanceY) ||
            !VectorIsFinite(glyph->offsetX, glyph->offsetY) ||
            !VectorIsFinite(glyph->advanceAdjustX, glyph->advanceAdjustY) ||
            !VectorIsFinite(glyph->offsetAdjustX, glyph->offsetAdjustY)) {
            problem = RoboTextGlyphRunProblemVector;
            break;
        }
        sum += (double)glyph->advanceX;
        if ((glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagMark) != 0u) {
            marks++;
        }
        if ((glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagLigature) != 0u) {
            ligatures++;
        }
        if ((glyph->flags & (RoboTextGlyphFlags)RoboTextGlyphFlagUnsafeToBreak) != 0u) {
            unsafeToBreak++;
        }
        if (glyph->mapped && glyph->metricsStatus == RoboFontGlyphMetricsOk) {
            /* mapped and measurable, so not missing. */
        } else if (glyph->control) {
            /* a control is not a `.notdef` — see the header. */
        } else {
            missing++;
        }
        if (glyph->mirrored) {
            mirrored++;
        }
        if (glyph->control) {
            controls++;
        }
        if (glyph->substituted) {
            substituted++;
        }
        if (glyph->positioned) {
            positioned++;
        }
    }

    /*
     * The clusters, against all five of their invariants at once: they tile the glyph array (start 0, each
     * beginning where the last ended, the last ending at the count), they tile the run's source without
     * overlapping or gaps, a glyph's `cluster` names the cluster that contains it, and each cluster holds at
     * least one glyph. `covered` is the source cursor the second and third are checked against.
     */
    for (size_t index = 0u; problem == RoboTextGlyphRunProblemNone && index < run->clusterCount; index++) {
        const RoboTextGlyphCluster *cluster = &run->clusters[index];
        size_t expectedStart = (index == 0u) ? 0u : (size_t)run->clusters[index - 1u].glyphEnd;

        if ((size_t)cluster->glyphStart != expectedStart || cluster->glyphEnd <= cluster->glyphStart ||
            (index + 1u == run->clusterCount
                 ? (size_t)cluster->glyphEnd != run->count
                 : (size_t)cluster->glyphEnd != (size_t)run->clusters[index + 1u].glyphStart)) {
            problem = RoboTextGlyphRunProblemClusterTiling;
            break;
        }
        if (RoboTextRangeIsEmpty(cluster->source) ||
            !RoboTextRangeContains(run->source, cluster->source.start) ||
            cluster->source.end > run->source.end) {
            problem = RoboTextGlyphRunProblemClusterSource;
            break;
        }
        if (cluster->source.start < covered) {
            problem = RoboTextGlyphRunProblemClusterOrder;
            break;
        }
        for (RoboTextGlyphIndex glyphIndex = cluster->glyphStart; glyphIndex < cluster->glyphEnd; glyphIndex++) {
            if (run->glyphs[glyphIndex].cluster != (RoboTextClusterIndex)index) {
                problem = RoboTextGlyphRunProblemClusterIndex;
                break;
            }
        }
        covered = cluster->source.end;
    }

    if (problem == RoboTextGlyphRunProblemNone) {
        if ((run->count == 0u) != (run->clusterCount == 0u)) {
            problem = RoboTextGlyphRunProblemClusterTiling;
        } else if (run->clusterCount != 0u && covered != run->source.end) {
            problem = RoboTextGlyphRunProblemClusterSource;
        }
    }
    if (problem == RoboTextGlyphRunProblemNone &&
        (run->advanceTotal < sum - 1e-6 || run->advanceTotal > sum + 1e-6)) {
        problem = RoboTextGlyphRunProblemTotal;
    }
    if (problem == RoboTextGlyphRunProblemNone &&
        (run->marks != marks || run->ligatures != ligatures || run->unsafeToBreak != unsafeToBreak ||
         run->missing != missing || run->mirrored != mirrored || run->controls != controls ||
         run->substituted != substituted || run->positioned != positioned)) {
        problem = RoboTextGlyphRunProblemCounter;
    }
    /*
     * The direction against the level's parity, and only when a level was resolved: a run a caller built by
     * hand carries `RoboTextBidiLevelNone`, and "no level" is not a claim about parity. A run whose level is
     * odd must say right-to-left, and this is the check that catches a reordering pass which forgot to update
     * the direction it publishes beside the level it resolved.
     */
    if (problem == RoboTextGlyphRunProblemNone && run->bidiLevel != RoboTextBidiLevelNone &&
        run->direction != RoboTextDirectionUnknown) {
        RoboTextDirection implied = RoboTextBidiLevelDirection(run->bidiLevel);

        if (implied != run->direction) {
            problem = RoboTextGlyphRunProblemDirection;
        }
    }

    *outProblem = problem;
    if (problem != RoboTextGlyphRunProblemNone) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               RoboTextGlyphRunProblemName(problem));
    }
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
    out->marks = run->marks;
    out->clusters = run->clusterCount;
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
        if (glyph->positioned) {
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
    length = RoboTextWriterText(line, sizeof(line), length, " positioned=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->positioned);
    length = RoboTextWriterText(line, sizeof(line), length, " control=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)glyph->control);
    length = RoboTextWriterText(line, sizeof(line), length, " metrics=");
    length = RoboTextWriterText(line, sizeof(line), length,
                                glyph->metricsStatus == RoboFontGlyphMetricsOk         ? "ok"
                                : glyph->metricsStatus == RoboFontGlyphMetricsInvalidId ? "invalid-id"
                                : glyph->metricsStatus == RoboFontGlyphMetricsMalformed ? "malformed"
                                                                                       : "unavailable");
    /* The grouping flags (Phase 12.14.22), last because they are the newest answer on the line. */
    length = RoboTextWriterText(line, sizeof(line), length, " flags=");
    if (length != RoboTextWriterOverflow) {
        char flagsWord[80];

        if (RoboTextGlyphFlagsWrite(glyph->flags, flagsWord, sizeof(flagsWord), NULL) == RoboOk) {
            length = RoboTextWriterText(line, sizeof(line), length, flagsWord);
        } else {
            length = RoboTextWriterOverflow;
        }
    }
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

/**
 * Append @p text to a dump, collapsing the two failure spellings into one.
 *
 * The writer returns @ref RoboTextWriterOverflow for "did not fit", and every append in
 * @ref RoboTextGlyphRunWrite treats that as fatal for the whole dump: a block that stops halfway is a block a
 * reader cannot tell from a complete one, and the caller is left with the empty string so that "absent" is
 * unambiguous.
 */
static size_t RunWriteText(char *buffer, size_t capacity, size_t length, const char *text)
{
    return RoboTextWriterText(buffer, capacity, length, text);
}

Result RoboTextGlyphRunWrite(const RoboTextGlyphRun *run,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength)
{
    char line[256];
    size_t length = 0u;
    RoboTextGlyphRunProblem problem = RoboTextGlyphRunProblemNone;

    if (outLength) {
        *outLength = 0u;
    }
    if (!run || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    /*
     * Validity first, and only when it fails: a block that prints is a block a reader trusts, so a record that
     * breaks its own invariants says so on the first line rather than leaving it to the reader to spot an
     * advance that does not add up. `RoboTextGlyphRunValidate` is the same door a test calls, so the mark on
     * the block and the failure in a suite can never disagree about what "invalid" means.
     */
    if (RoboTextGlyphRunValidate(run, &problem, NULL) != RoboOk) {
        length = RunWriteText(buffer, capacity, length, "INVALID ");
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, RoboTextGlyphRunProblemName(problem));
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, "\n");
        }
        if (length == RoboTextWriterOverflow) {
            buffer[0] = '\0';
            return RoboNotSupported;
        }
    }

    length = RunWriteText(buffer, capacity, length, "GlyphRun {\n  source [");
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, run->source.start);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "..");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, run->source.end);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, ")  scalars ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)RoboTextGlyphRunScalarCount(run));
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "\n  script ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, RoboTextScriptName(run->script));
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  direction ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, RoboTextDirectionName(run->direction));
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  bidiLevel ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterSigned(buffer, capacity, length, (int32_t)run->bidiLevel);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  base ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, RoboTextDirectionName(run->baseDirection));
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "\n  mode ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, RoboTextGlyphGenerationModeName(run->mode));
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  features ");
    }
    if (length != RoboTextWriterOverflow) {
        char features[192];
        size_t featuresLength = 0u;

        if (RoboTextFeatureSetWrite(&run->features, features, sizeof(features), &featuresLength) == RoboOk) {
            length = RunWriteText(buffer, capacity, length, features);
        } else {
            length = RunWriteText(buffer, capacity, length, "@");
        }
        (void)featuresLength;
    }

    /*
     * The metrics line, with the four numbers line layout reads and the flag that says whether they came from
     * the face — a dump that printed `ascent 0.00` without it would report an unavailable metric as a measured
     * one, which is the mistake this whole record exists to prevent one level down.
     */
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "\n  metrics ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, run->haveMetrics ? "from-face " : "estimated ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "ascent ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, run->metrics.ascentPx);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  descent ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, run->metrics.descentPx);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  lineGap ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, run->metrics.lineGapPx);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  lineHeight ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, run->metrics.lineHeightPx);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  cap ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, run->metrics.capHeightPx);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  x ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, run->metrics.xHeightPx);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "\n  advance ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, RoboTextGlyphRunAdvance(run));
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, ", ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, RoboTextGlyphRunAdvanceY(run));
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  glyphs ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)run->count);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  clusters ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)run->clusterCount);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  missing ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)run->missing);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "\n  flags marks ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)run->marks);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  ligatures ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)run->ligatures);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  positioned ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)run->positioned);
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "  unsafeToBreak ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)run->unsafeToBreak);
    }

    for (size_t index = 0u; length != RoboTextWriterOverflow && index < run->count; index++) {
        size_t glyphLength = 0u;

        if (RoboTextGlyphWrite(&run->glyphs[index], line, sizeof(line), &glyphLength) != RoboOk) {
            length = RoboTextWriterOverflow;
            break;
        }
        length = RunWriteText(buffer, capacity, length, "\n  glyph[");
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)index);
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, "] ");
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, line);
        }
    }
    for (size_t index = 0u; length != RoboTextWriterOverflow && index < run->clusterCount; index++) {
        const RoboTextGlyphCluster *cluster = &run->clusters[index];

        length = RunWriteText(buffer, capacity, length, "\n  cluster[");
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)index);
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, "] source [");
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, cluster->source.start);
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, "..");
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, cluster->source.end);
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, ") glyphs [");
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)cluster->glyphStart);
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, "..");
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)cluster->glyphEnd);
        }
        if (length != RoboTextWriterOverflow) {
            length = RunWriteText(buffer, capacity, length, ")");
        }
    }
    if (length != RoboTextWriterOverflow) {
        length = RunWriteText(buffer, capacity, length, "\n}\n");
    }

    if (length == RoboTextWriterOverflow) {
        /* Nothing is left for the caller to mistake for a complete block. */
        buffer[0] = '\0';
        return RoboNotSupported;
    }
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
