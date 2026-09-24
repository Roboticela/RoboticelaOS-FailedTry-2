/*
 * RoboText's glyph positioning engine (Phase 12.11) — see `RoboTextGpos.h`.
 *
 * The file is four pieces, in the order a shaping call meets them:
 *
 *   **the defaults**  the features a script's text gets when the caller names none. It is *positioning's*
 *                     list, not substitution's: `kern` above all, then `mark`, `mkmk`, `curs` and `dist`,
 *                     and the two Indic mark-above/below features for the scripts that use them. This is the
 *                     one place the two tables disagree, and it is why the shared plan resolver takes the
 *                     defaults as an argument instead of owning them.
 *
 *   **the call**      `RoboTextGposApply` walks the plan's lookup order once and tries each lookup's
 *                     subtables at each buffer position. A lookup that fails, a type this build does not
 *                     execute and a malformed subtable all **skip** — counted in `RoboTextGposStats` — and
 *                     the scan continues. Nothing about a font can fail a run of text.
 *
 *   **the trace**     an optional array of adjustments, one per value record applied. `RoboTextGposTrace`
 *                     is called by the readers rather than written by them, so a diagnostic sees exactly
 *                     what the buffer received, in order.
 *
 *   **the writing**   counters and events, both complete-or-absent like every dump in this module: a
 *                     truncated table reads as a *different* table, and a diagnostic that lies is worse
 *                     than one that refuses.
 *
 * Positioning never changes the glyph count, so unlike substitution this engine has no buffer rebuild and no
 * recursion into itself. The only per-call allocation is the parsed-coverage cache the shared helpers
 * already keep (`RoboTextOtInternal.h`), which is why a run of text with kerning allocates nothing here.
 */

#include "RoboText/RoboTextGpos.h"

#include "RoboText/RoboTextOtLayout.h"
#include "RoboTextOtInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The positioning defaults                                            */
/* ------------------------------------------------------------------ */

/**
 * The features a script's text gets when the caller names none.
 *
 * Five tags, and the reasoning for each: `kern` is the phase's whole point; `mark` and `mkmk` place a
 * combining mark on its base and on the mark before it; `curs` is cursive attachment, which a font uses to
 * join a connected script's glyphs; `dist` is the "adjust to a distance" feature Indic fonts use for
 * matras. A script whose marks are positioned by name adds the two mark-above/below features.
 *
 * `kern` is deliberately unconditional, and it is the one entry a caller is most likely to override: a
 * `font-kerning: none` is an `Off` setting for this tag, and a caller's setting always wins over a default
 * (`RoboTextOtPlanResolve`). Nothing here has to know that CSS exists.
 */
void RoboTextGposDefaultFeatureSet(RoboTextScript script, RoboTextFeatureSet *out)
{
    static const char *const kGeneric[] = {"kern", "mark", "mkmk", "curs", "dist"};
    static const char *const kMarkOnBase[] = {"abvm", "blwm"};
    int marksByScript = 0;

    if (!out) {
        return;
    }
    RoboTextFeatureSetInit(out);
    for (size_t index = 0u; index < sizeof(kGeneric) / sizeof(kGeneric[0]); index++) {
        (void)RoboTextFeatureSetSet(out, kGeneric[index], RoboTextFeatureOn, 0u);
    }
    switch (script) {
        case RoboUScriptDevanagari:
        case RoboUScriptBengali:
        case RoboUScriptGurmukhi:
        case RoboUScriptGujarati:
        case RoboUScriptOriya:
        case RoboUScriptTamil:
        case RoboUScriptTelugu:
        case RoboUScriptKannada:
        case RoboUScriptMalayalam:
        case RoboUScriptSinhala:
        case RoboUScriptKhmer:
        case RoboUScriptMyanmar:
        case RoboUScriptTibetan:
            marksByScript = 1;
            break;
        default:
            marksByScript = 0;
            break;
    }
    if (marksByScript) {
        for (size_t index = 0u; index < sizeof(kMarkOnBase) / sizeof(kMarkOnBase[0]); index++) {
            (void)RoboTextFeatureSetSet(out, kMarkOnBase[index], RoboTextFeatureOn, 0u);
        }
    }
}

/**
 * GPOS's door onto the shared layout core, with positioning's defaults for the script.
 *
 * Same resolution as `RoboTextGsubPlanResolve` and one argument apart: a GPOS plan and a GSUB plan are the
 * same question asked of a different table, which is the property 12.13 §"OpenType infrastructure" asks for.
 */
Result RoboTextGposPlanResolve(const FontOtLayout *gpos,
                               RoboTextScript script,
                               const RoboTextLanguage *language,
                               const RoboTextFeatureSet *features,
                               RoboTextOtPlan *out,
                               RoboTextDiag *diag)
{
    RoboTextFeatureSet defaults;

    RoboTextGposDefaultFeatureSet(script, &defaults);
    return RoboTextOtPlanResolve(gpos, script, language, features, &defaults, out, diag);
}

/* ------------------------------------------------------------------ */
/* Stats                                                               */
/* ------------------------------------------------------------------ */

void RoboTextGposStatsInit(RoboTextGposStats *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

/* ------------------------------------------------------------------ */
/* The trace                                                           */
/* ------------------------------------------------------------------ */

void RoboTextGposTrace(RoboTextOtCall *apply,
                       size_t position,
                       size_t secondPosition,
                       const RoboTextGposValue *first,
                       const RoboTextGposValue *second,
                       uint16_t secondGlyph,
                       uint16_t firstClass,
                       uint16_t secondClass)
{
    RoboTextGposEvent *event;
    const RoboTextShapingGlyph *glyph;

    if (!apply || !apply->gposTrace || apply->gposTraceCount >= apply->gposTraceCapacity) {
        return;
    }
    event = &apply->gposTrace[apply->gposTraceCount++];
    memset(event, 0, sizeof(*event));
    memcpy(event->feature, apply->featureTag, 4u);
    event->lookupIndex = apply->lookupIndex;
    event->lookupType = apply->lookupType;
    event->position = position;
    event->secondPosition = secondPosition;
    event->secondGlyph = secondGlyph;
    event->firstClass = firstClass;
    event->secondClass = secondClass;
    glyph = RoboTextShapingBufferAt(apply->buffer, position);
    event->firstGlyph = glyph ? glyph->glyphId : RoboTextGposNoGlyph;
    event->source = glyph ? glyph->source : RoboTextRangeMake(0u, 0u);
    if (first) {
        event->first = *first;
    }
    if (second) {
        event->second = *second;
    }
}

/* ------------------------------------------------------------------ */
/* Application                                                         */
/* ------------------------------------------------------------------ */

/** The lookup at @p index in @p gpos, or NULL when the index is outside the Lookup List. */
static const FontOtLookup *GposLookupAt(const FontOtLayout *gpos, uint16_t index)
{
    if (!gpos || index >= gpos->lookups.lookupCount) {
        return NULL;
    }
    return &gpos->lookups.lookups[index];
}

/**
 * Apply one lookup, at every buffer position.
 *
 * Positioning never changes the glyph count, so the scan is a plain walk: every position is asked, and the
 * next position is always the next glyph. That is also what makes overlapping pairs work — `AVA` kerns `A V`
 * and then `V A`, because the `V` is the first glyph of a pair of its own.
 *
 * @return 1 when the lookup adjusted anything.
 */
static int GposApplyLookupScan(RoboTextOtCall *apply)
{
    size_t index = 0u;
    int applied = 0;

    /*
     * A subtable offset that lands on the lookup's own header is refused **once**, before the scan: how many
     * times a run of text would have stepped over it is a fact about the text, not about the font, and a
     * counter that grew with the paragraph would be no use for diagnosing the paragraph's font.
     */
    for (uint16_t subtable = 0u; subtable < apply->lookup->subtableCount; subtable++) {
        const FontOtLookupSubtable *entry = &apply->lookup->subtables[subtable];

        if (entry->offset == 0u || RoboTextOtSubtableInsideLookup(apply->lookup, entry->offset)) {
            apply->gposStats.subtablesMalformed++;
        }
    }

    while (index < RoboTextShapingBufferCount(apply->buffer)) {
        if (!RoboTextOtMaskAllows(apply, apply->lookupIndex, index)) {
            /* The strategy's positional condition (Phase 12.19), asked exactly where the substitution engine
             * asks it: a positioning lookup whose stage names a mask runs only at the positions that mask
             * selects, and a stage that names none — every Latin, Greek and Cyrillic run — never reaches this
             * branch's second half. */
            index++;
            continue;
        }
        for (uint16_t subtable = 0u; subtable < apply->lookup->subtableCount; subtable++) {
            const FontOtLookupSubtable *entry = &apply->lookup->subtables[subtable];
            size_t resolved = index + 1u;

            if (entry->offset == 0u || RoboTextOtSubtableInsideLookup(apply->lookup, entry->offset)) {
                /* No subtable here — counted above, and stepped over here. */
                continue;
            }
            apply->gposStats.subtablesTried++;
            if (RoboTextGposApplySubtable(apply, apply->lookupType, entry->offset, index, &resolved)) {
                applied = 1;
                break;
            }
            if (apply->failed) {
                return applied;
            }
        }
        index++;
    }
    return applied;
}

Result RoboTextGposApply(const FontOtLayout *gpos,
                         const FontGdef *gdef,
                         const RoboTextOtPlan *plan,
                         const RoboTextGposOptions *options,
                         RoboTextShapingBuffer *buffer,
                         RoboTextGposStats *outStats,
                         RoboTextDiag *diag)
{
    RoboTextOtCall apply;

    if (!buffer || !outStats) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboInvalidArgument,
                               "a buffer and an out stats record are required");
    }
    RoboTextGposStatsInit(outStats);
    memset(&apply, 0, sizeof(apply));
    apply.layout = gpos;
    apply.gdef = gdef;
    apply.positioning = 1;
    apply.plan = plan;
    apply.buffer = buffer;
    apply.limits = (options && options->limits) ? options->limits : &RoboTextOtDefaultLimits;
    apply.direction = options ? options->direction : RoboTextDirectionUnknown;
    apply.bidiLevel = options ? options->bidiLevel : RoboTextBidiLevelNone;
    if (options) {
        apply.positionMasks = options->positionMasks;
        apply.positionMaskCount = options->positionMaskCount;
        apply.maskBase = options->maskBase;
        apply.gposFont = options->font;
        apply.devicePpem = options->devicePpem;
        apply.deviceScale = options->deviceScale;
        apply.gposTrace = options->trace;
        apply.gposTraceCapacity = options->traceCapacity;
    }
    apply.gposStats.glyphsBefore = RoboTextShapingBufferCount(buffer);
    apply.gposStats.glyphsAfter = apply.gposStats.glyphsBefore;

    if (gpos && plan && !plan->empty) {
        if (gpos->kind != RoboFontOtLayoutGpos) {
            return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboInvalidArgument,
                                   "the layout handed to the positioning engine is not GPOS");
        }
        for (size_t index = 0u; index < plan->lookupCount; index++) {
            const FontOtLookup *lookup = GposLookupAt(gpos, plan->lookupOrder[index]);
            uint16_t type;

            if (!lookup) {
                apply.gposStats.lookupsSkipped++;
                continue;
            }
            type = lookup->lookupType;
            if (type == 0u || !FontGposLookupTypeIsKnown(type)) {
                apply.gposStats.lookupsSkipped++;
                continue;
            }
            if (!RoboTextGposTypeIsExecuted(type)) {
                /* A real lookup type this build does not run: the two contextual forms (7 and 8). Counted
                 * where a caller can see it, and shaped past — the glyphs keep the positions their advances
                 * gave them, which is what this runtime drew before 12.11. */
                apply.gposStats.lookupsUnsupported++;
                continue;
            }
            if ((type == RoboTextGposLookupCursive || type == RoboTextGposLookupMarkToBase ||
                 type == RoboTextGposLookupMarkToLigature || type == RoboTextGposLookupMarkToMark) &&
                !apply.gposFont) {
                /*
                 * An **attachment** places a glyph against another glyph's pen, and the pen's distance is the
                 * face's advance — the one quantity GPOS does not carry (Phase 12.19). A caller that supplied
                 * no font context gets the lookup counted unsupported **once per lookup**, not once per
                 * position: how many times a paragraph would have asked is a fact about the paragraph.
                 *
                 * Kerning and single adjustments need no advance and run either way.
                 */
                apply.gposStats.lookupsUnsupported++;
                continue;
            }
            apply.lookup = lookup;
            apply.lookupIndex = plan->lookupOrder[index];
            apply.lookupType = type;
            memcpy(apply.featureTag, RoboTextOtFeatureForLookup(plan, apply.lookupIndex), 4u);
            if (!RoboTextOtLookupAppliesToDirection(&apply)) {
                /* The font's own condition, not a failure: a right-to-left-only lookup on left-to-right
                 * text simply does not apply. */
                continue;
            }
            if (apply.gposStats.lookupsApplied >= (size_t)apply.limits->maxLookupApplications) {
                apply.limitReached = 1;
                break;
            }
            apply.depth = 0u;
            apply.gposStats.lookupsApplied++;
            (void)GposApplyLookupScan(&apply);
            if (apply.failed) {
                break;
            }
        }
    }

    apply.gposStats.glyphsAfter = RoboTextShapingBufferCount(buffer);
    apply.gposStats.traceEvents = apply.gposTraceCount;
    apply.gposStats.limitReached = (apply.limitReached || apply.failed) ? 1 : 0;
    *outStats = apply.gposStats;
    RoboTextOtCacheClear(&apply);
    if (apply.failed) {
        return RoboTextDiagMsg(diag, RoboTextDomainGpos, RoboOutOfMemory,
                               "the shaping buffer could not grow");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The writers                                                         */
/* ------------------------------------------------------------------ */

/** The dump sink both writers below share: measure with a NULL buffer, then write. */
typedef struct GposDumpSink {
    char *buffer;
    size_t capacity;
    size_t length;
    int failed;
} GposDumpSink;

static void GposDumpSinkLine(GposDumpSink *sink, const char *line, size_t lineLength)
{
    if (sink->failed) {
        return;
    }
    if (lineLength == RoboTextWriterOverflow) {
        sink->failed = 1;
        return;
    }
    if (sink->buffer) {
        if (sink->length + lineLength + 2u > sink->capacity + 1u) {
            sink->failed = 1;
            return;
        }
        memcpy(sink->buffer + sink->length, line, lineLength);
        sink->length += lineLength;
        sink->buffer[sink->length++] = '\n';
        sink->buffer[sink->length] = '\0';
    } else {
        sink->length += lineLength + 1u;
    }
}

/** A counter line: `label value`. */
static void GposCounterLine(GposDumpSink *sink, const char *label, size_t value)
{
    char line[96];
    size_t length = 0u;

    length = RoboTextWriterText(line, sizeof(line), length, label);
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)value);
    GposDumpSinkLine(sink, line, length);
}

Result RoboTextGposStatsWrite(const RoboTextGposStats *stats,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength)
{
    GposDumpSink measure;
    GposDumpSink write;
    GposDumpSink *sinks[2];

    if (outLength) {
        *outLength = 0u;
    }
    if (!stats || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    memset(&measure, 0, sizeof(measure));
    memset(&write, 0, sizeof(write));
    write.buffer = buffer;
    write.capacity = capacity;
    sinks[0] = &measure;
    sinks[1] = &write;
    for (int pass = 0; pass < 2; pass++) {
        GposDumpSink *sink = sinks[pass];

        GposCounterLine(sink, "glyphs before ", stats->glyphsBefore);
        GposCounterLine(sink, "glyphs after ", stats->glyphsAfter);
        GposCounterLine(sink, "lookups applied ", stats->lookupsApplied);
        GposCounterLine(sink, "lookups skipped ", stats->lookupsSkipped);
        GposCounterLine(sink, "lookups unsupported ", stats->lookupsUnsupported);
        GposCounterLine(sink, "subtables tried ", stats->subtablesTried);
        GposCounterLine(sink, "subtables malformed ", stats->subtablesMalformed);
        GposCounterLine(sink, "single ", stats->singleAdjustments);
        GposCounterLine(sink, "pairs ", stats->pairAdjustments);
        GposCounterLine(sink, "class pairs ", stats->classPairAdjustments);
        GposCounterLine(sink, "device corrections ", stats->deviceCorrections);
        GposCounterLine(sink, "device unevaluated ", stats->deviceUnevaluated);
        GposCounterLine(sink, "records unevaluated ", stats->recordsUnevaluated);
        GposCounterLine(sink, "adjusted glyphs ", stats->adjustedGlyphs);
        GposCounterLine(sink, "probes ", (size_t)stats->probes);
        GposCounterLine(sink, "trace events ", stats->traceEvents);
        GposCounterLine(sink, "limit reached ", stats->limitReached ? 1u : 0u);
        if (sink->failed) {
            return RoboNotSupported;
        }
        if (pass == 0 && measure.length + 1u > capacity) {
            if (outLength) {
                *outLength = measure.length;
            }
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = write.length;
    }
    return RoboOk;
}

/** One value record as `xPla=<f> yPla=<f> xAdv=<f> yAdv=<f>`, with a marker when a device delta joined in. */
static size_t GposWriteValue(char *line, size_t capacity, size_t length, const RoboTextGposValue *value)
{
    length = RoboTextWriterText(line, capacity, length, " xPla=");
    length = RoboTextWriterFloat(line, capacity, length, value->xPlacement);
    length = RoboTextWriterText(line, capacity, length, " yPla=");
    length = RoboTextWriterFloat(line, capacity, length, value->yPlacement);
    length = RoboTextWriterText(line, capacity, length, " xAdv=");
    length = RoboTextWriterFloat(line, capacity, length, value->xAdvance);
    length = RoboTextWriterText(line, capacity, length, " yAdv=");
    length = RoboTextWriterFloat(line, capacity, length, value->yAdvance);
    if (value->deviceApplied) {
        length = RoboTextWriterText(line, capacity, length, " device");
    }
    return length;
}

Result RoboTextGposEventsWrite(const RoboTextGposEvent *events,
                               size_t count,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength)
{
    GposDumpSink measure;
    GposDumpSink write;
    GposDumpSink *sinks[2];

    if (outLength) {
        *outLength = 0u;
    }
    if ((count != 0u && !events) || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    memset(&measure, 0, sizeof(measure));
    memset(&write, 0, sizeof(write));
    write.buffer = buffer;
    write.capacity = capacity;
    sinks[0] = &measure;
    sinks[1] = &write;
    for (int pass = 0; pass < 2; pass++) {
        GposDumpSink *sink = sinks[pass];
        char line[384];
        size_t length = 0u;

        GposCounterLine(sink, "events ", count);
        for (size_t index = 0u; index < count; index++) {
            const RoboTextGposEvent *event = &events[index];
            char tag[RoboTextFeatureTagBytes];

            memcpy(tag, event->feature, 4u);
            tag[4] = '\0';
            length = 0u;
            length = RoboTextWriterText(line, sizeof(line), length, tag);
            length = RoboTextWriterText(line, sizeof(line), length, " lookup ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->lookupIndex);
            length = RoboTextWriterText(line, sizeof(line), length, " (type ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->lookupType);
            length = RoboTextWriterText(line, sizeof(line), length, ") at ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)event->position);
            length = RoboTextWriterText(line, sizeof(line), length, " gid ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->firstGlyph);
            if (event->secondGlyph != RoboTextGposNoGlyph) {
                length = RoboTextWriterText(line, sizeof(line), length, "/");
                length = RoboTextWriterUnsigned(line, sizeof(line), length, event->secondGlyph);
            }
            if (event->firstClass != RoboTextGposNoClass) {
                length = RoboTextWriterText(line, sizeof(line), length, " class ");
                length = RoboTextWriterUnsigned(line, sizeof(line), length, event->firstClass);
                length = RoboTextWriterText(line, sizeof(line), length, "/");
                length = RoboTextWriterUnsigned(line, sizeof(line), length, event->secondClass);
            }
            length = GposWriteValue(line, sizeof(line), length, &event->first);
            if (event->secondGlyph != RoboTextGposNoGlyph) {
                length = GposWriteValue(line, sizeof(line), length, &event->second);
            }
            length = RoboTextWriterText(line, sizeof(line), length, " src=[");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->source.start);
            length = RoboTextWriterText(line, sizeof(line), length, ",");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, event->source.end);
            length = RoboTextWriterText(line, sizeof(line), length, ")");
            GposDumpSinkLine(sink, line, length);
        }
        if (sink->failed) {
            return RoboNotSupported;
        }
        if (pass == 0 && measure.length + 1u > capacity) {
            if (outLength) {
                *outLength = measure.length;
            }
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = write.length;
    }
    return RoboOk;
}
