/*
 * The GSUB subtable readers (Phase 12.10) — see `RoboTextGsubInternal.h` for the contract and
 * `RoboTextGsub.h` for the module's rule.
 *
 * One GSUB lookup type per reader, plus the machinery they share. The file's shape follows the binary
 * formats rather than taste, and four decisions run through all of it:
 *
 *   **Every offset is resolved before it is read, and every read is bounds-checked.** `GsubOffset16` is
 *   Phase 5's `FontOtResolveOffset16`; the readers are `RoboFontBinary.h`'s `FontReader`. A font is
 *   untrusted input, so a malformed table answers "this subtable did not match" (0), which the engine
 *   counts as a skip, rather than reading whatever followed the offset.
 *
 *   **Matching walks *participating* glyphs, never adjacent ones.** `RoboTextGsubNextParticipating` steps
 *   over glyphs the lookup's flags exclude, so a ligature whose components are separated by a mark still
 *   matches, and the mark is still in the buffer afterwards — the phase's §"ignored glyphs" and §"ligature
 *   matching" in one helper. The positions a pattern matched on are then the ones a lookup record applies
 *   at, which is why matching fills an array rather than counting.
 *
 *   **A substitution says where the scan continues.** `RoboTextGsubApplySubtable` reports it through its
 *   `outNext`, and each reader knows its own answer: a single substitution consumed one glyph, a ligature
 *   consumed its components, a context consumed its whole input. Leaving that to the caller to guess is how
 *   an engine re-matches its own output.
 *
 *   **Nothing here decides anything about a script.** A ligature is a ligature whether it joins Latin
 *   `ffi`, Arabic joining forms, or an Indic reordering. The font's tables say which lookups run for which
 *   script and language (the plan), and this file only executes what a lookup's bytes say.
 *
 * Position bookkeeping deserves one note, because it is the thing a real shaper gets subtly wrong. When a
 * contextual lookup applies several lookup records at different sequence indices, an earlier record's
 * substitution changes the buffer's length, so every position after it has *moved*. `GsubApplyRecords`
 * therefore applies records in the order the font lists them (the format guarantees non-decreasing sequence
 * index) and shifts the positions that follow each substitution by the buffer's actual growth — which is
 * the only way to keep later records pointing at the glyphs the font meant.
 *
 * The formats themselves are read exactly as the OpenType specification states them, including the two
 * asymmetries a reader has to notice: a context or chaining *class* rule's input array lists
 * `count - 1` entries because the first input glyph is the one the coverage matched, and a chaining rule's
 * three parts are matched against three different class definitions.
 */

#include "RoboTextGsubInternal.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Reads                                                               */
/* ------------------------------------------------------------------ */

static Result GsubReadU16(FontSpan table, size_t offset, uint16_t *out)
{
    FontReader reader;

    if (!out || FontCheckedRange(table.size, offset, 2u) != RoboOk) {
        return RoboParseError;
    }
    FontReaderInitSpan(&reader, table);
    if (FontReaderSeek(&reader, offset) != RoboOk) {
        return RoboParseError;
    }
    return FontReadU16be(&reader, out);
}

static Result GsubReadI16(FontSpan table, size_t offset, int16_t *out)
{
    FontReader reader;

    if (!out || FontCheckedRange(table.size, offset, 2u) != RoboOk) {
        return RoboParseError;
    }
    FontReaderInitSpan(&reader, table);
    if (FontReaderSeek(&reader, offset) != RoboOk) {
        return RoboParseError;
    }
    return FontReadI16be(&reader, out);
}

static Result GsubReadU32(FontSpan table, size_t offset, uint32_t *out)
{
    FontReader reader;

    if (!out || FontCheckedRange(table.size, offset, 4u) != RoboOk) {
        return RoboParseError;
    }
    FontReaderInitSpan(&reader, table);
    if (FontReaderSeek(&reader, offset) != RoboOk) {
        return RoboParseError;
    }
    return FontReadU32be(&reader, out);
}

/** A `uint16` field at @p offset. Every field read below is one of these, and each guards the next. */
#define GsubU16(table, offset, out) (GsubReadU16((table), (offset), (out)) == RoboOk)

/**
 * @p offset resolved against @p base, or 0 when the field is 0.
 *
 * A zero offset is OpenType's "there is no such table here" in every format below, and Phase 5's resolver
 * cannot express it: it would answer `base` itself, which is a real location in the parent and would then be
 * parsed as a coverage table. So absent is answered as absent.
 */
static size_t GsubOffset16(FontSpan table, size_t base, uint16_t offset)
{
    size_t absolute = 0u;

    if (offset == 0u || FontOtResolveOffset16(table, base, offset, &absolute) != RoboOk) {
        return 0u;
    }
    return absolute;
}

static size_t GsubOffset32(FontSpan table, size_t base, uint32_t offset)
{
    size_t absolute = 0u;

    if (offset == 0u || FontOtResolveOffset32(table, base, offset, &absolute) != RoboOk) {
        return 0u;
    }
    return absolute;
}

/* ------------------------------------------------------------------ */
/* The parsed-subtable cache                                           */
/* ------------------------------------------------------------------ */

static void GsubCacheDropOldest(RoboTextGsubCall *apply)
{
    if (apply->cacheCount == 0u) {
        return;
    }
    if (apply->cache[0].kind == 1) {
        FontOtCoverageClear(&apply->cache[0].coverage);
    } else if (apply->cache[0].kind == 2) {
        FontOtClassDefClear(&apply->cache[0].classDef);
    }
    for (size_t index = 1u; index < apply->cacheCount; index++) {
        apply->cache[index - 1u] = apply->cache[index];
    }
    apply->cacheCount--;
}

void RoboTextGsubCacheClear(RoboTextGsubCall *apply)
{
    if (!apply) {
        return;
    }
    while (apply->cacheCount != 0u) {
        GsubCacheDropOldest(apply);
    }
    apply->cacheCount = 0u;
}

const FontOtCoverage *RoboTextGsubCacheCoverage(RoboTextGsubCall *apply, size_t offset)
{
    RoboTextGsubCacheEntry *entry;

    if (!apply || !apply->gsub || offset == 0u) {
        return NULL;
    }
    for (size_t index = 0u; index < apply->cacheCount; index++) {
        if (apply->cache[index].kind == 1 && apply->cache[index].offset == offset) {
            return &apply->cache[index].coverage;
        }
    }
    if (apply->cacheCount >= RoboTextGsubCacheSlots) {
        GsubCacheDropOldest(apply);
    }
    entry = &apply->cache[apply->cacheCount];
    memset(entry, 0, sizeof(*entry));
    if (FontOtCoverageParse(apply->gsub->table, offset, &entry->coverage) != RoboOk) {
        FontOtCoverageClear(&entry->coverage);
        memset(entry, 0, sizeof(*entry));
        return NULL;
    }
    entry->offset = offset;
    entry->kind = 1;
    apply->cacheCount++;
    return &entry->coverage;
}

const FontOtClassDef *RoboTextGsubCacheClassDef(RoboTextGsubCall *apply, size_t offset)
{
    RoboTextGsubCacheEntry *entry;

    if (!apply || !apply->gsub || offset == 0u) {
        return NULL;
    }
    for (size_t index = 0u; index < apply->cacheCount; index++) {
        if (apply->cache[index].kind == 2 && apply->cache[index].offset == offset) {
            return &apply->cache[index].classDef;
        }
    }
    if (apply->cacheCount >= RoboTextGsubCacheSlots) {
        GsubCacheDropOldest(apply);
    }
    entry = &apply->cache[apply->cacheCount];
    memset(entry, 0, sizeof(*entry));
    if (FontOtClassDefParse(apply->gsub->table, offset, &entry->classDef) != RoboOk) {
        FontOtClassDefClear(&entry->classDef);
        memset(entry, 0, sizeof(*entry));
        return NULL;
    }
    entry->offset = offset;
    entry->kind = 2;
    apply->cacheCount++;
    return &entry->classDef;
}

int RoboTextGsubCovered(RoboTextGsubCall *apply, size_t offset, GlyphId glyphId, uint16_t *outIndex)
{
    const FontOtCoverage *coverage = RoboTextGsubCacheCoverage(apply, offset);
    uint16_t index = 0u;

    if (!coverage) {
        return 0;
    }
    if (FontOtCoverageIndex(coverage, (uint16_t)glyphId, &index) != RoboOk) {
        return 0;
    }
    if (outIndex) {
        *outIndex = index;
    }
    return 1;
}

uint16_t RoboTextGsubClassOf(RoboTextGsubCall *apply, size_t offset, GlyphId glyphId)
{
    const FontOtClassDef *classDef = RoboTextGsubCacheClassDef(apply, offset);
    uint16_t value = 0u;

    if (!classDef) {
        return 0u;
    }
    if (FontOtClassDefGet(classDef, (uint16_t)glyphId, &value) != RoboOk) {
        return 0u;
    }
    return value;
}

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

int RoboTextGsubSpendProbes(RoboTextGsubCall *apply, uint32_t count)
{
    if (!apply || !apply->limits || count > apply->limits->maxGlyphProbes) {
        return 0;
    }
    if (apply->stats.probes > apply->limits->maxGlyphProbes - count) {
        apply->limitReached = 1;
        return 0;
    }
    apply->stats.probes += count;
    return 1;
}

int RoboTextGsubExceedsGlyphLimit(const RoboTextGsubCall *apply, size_t after)
{
    if (!apply || !apply->limits) {
        return 1;
    }
    return after > apply->limits->maxGlyphs ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Which glyphs a lookup sees                                          */
/* ------------------------------------------------------------------ */

int RoboTextGsubSkipped(RoboTextGsubCall *apply, size_t index)
{
    const FontOtLookup *lookup;
    const RoboTextShapingGlyph *at;
    uint16_t glyphClass = 0u;
    GlyphId glyphId;

    if (!apply || !(lookup = apply->lookup)) {
        return 0;
    }
    at = RoboTextGsubBufferAt(apply->buffer, index);
    if (!at) {
        return 1;
    }
    if ((at->flags & RoboTextShapingGlyphControl) != 0u) {
        /* A bidi formatting control is not a character a lookup may match, and it is not an *ignored* glyph
         * either: nothing edits its slot, so the source positions of everything after it stay aligned. */
        return 1;
    }
    glyphId = (GlyphId)at->glyphId;
    /*
     * GDEF's glyph class definition is the vocabulary the flags are stated in: 1 base, 2 ligature, 3 mark,
     * 4 component. A face without one has no classes, so the class flags cannot exclude anything — the
     * honest answer rather than a guess about the glyph.
     */
    if (apply->gdef && apply->gdef->hasGlyphClassDef) {
        (void)FontOtClassDefGet(&apply->gdef->glyphClassDef, (uint16_t)glyphId, &glyphClass);
    }
    if (glyphClass == 1u && (lookup->lookupFlag & RoboFontOtLookupFlagIgnoreBase) != 0u) {
        return 1;
    }
    if (glyphClass == 2u && (lookup->lookupFlag & RoboFontOtLookupFlagIgnoreLigatures) != 0u) {
        return 1;
    }
    if (glyphClass != 3u) {
        return 0;
    }
    if ((lookup->lookupFlag & RoboFontOtLookupFlagIgnoreMarks) != 0u) {
        return 1;
    }
    {
        /* `markAttachmentType`: a lookup that names one only processes marks of that class. */
        uint16_t attachmentType =
            (uint16_t)((lookup->lookupFlag & RoboFontOtLookupFlagMarkAttachmentTypeMask) >> 8);

        if (attachmentType != 0u && apply->gdef && apply->gdef->hasMarkAttachClassDef) {
            uint16_t attached = 0u;

            (void)FontOtClassDefGet(&apply->gdef->markAttachClassDef, (uint16_t)glyphId, &attached);
            if (attached != attachmentType) {
                return 1;
            }
        }
    }
    if ((lookup->lookupFlag & RoboFontOtLookupFlagUseMarkFilteringSet) != 0u && apply->gdef &&
        apply->gdef->hasMarkGlyphSets && apply->gdef->markGlyphSetsOffset != 0u) {
        /*
         * GDEF's mark glyph sets table: a `uint16` format, a count, then a `uint32` offset array, all
         * relative to the table's own start inside GDEF. A mark **not** in the set the lookup named is not
         * this lookup's mark. A table that cannot be read leaves the lookup processing every mark, which is
         * what it would have done without the flag — a font's defect must not change the answer silently.
         */
        FontSpan gdefTable = apply->gdef->table;
        size_t sets = apply->gdef->markGlyphSetsOffset;
        uint16_t format = 0u;
        uint16_t setCount = 0u;
        uint32_t coverageField = 0u;
        size_t coverage;

        if (!GsubU16(gdefTable, sets, &format) || format != 1u ||
            !GsubU16(gdefTable, sets + 2u, &setCount) || lookup->markFilteringSet >= setCount ||
            !GsubReadU32(gdefTable, sets + 4u + (size_t)lookup->markFilteringSet * 4u, &coverageField)) {
            return 0;
        }
        coverage = GsubOffset32(gdefTable, sets, coverageField);
        if (coverage == 0u || !RoboTextGsubCovered(apply, coverage, glyphId, NULL)) {
            return 1;
        }
    }
    return 0;
}

size_t RoboTextGsubNextParticipating(RoboTextGsubCall *apply, size_t index)
{
    size_t count = RoboTextGsubBufferCount(apply->buffer);

    while (index < count) {
        if (!RoboTextGsubSkipped(apply, index)) {
            return index;
        }
        index++;
    }
    return count;
}

size_t RoboTextGsubPrevParticipating(RoboTextGsubCall *apply, size_t index, size_t steps)
{
    size_t count = RoboTextGsubBufferCount(apply->buffer);
    size_t position = index;

    while (steps != 0u) {
        if (position == 0u) {
            return count;
        }
        if (RoboTextGsubSkipped(apply, position)) {
            /* Walk down over the glyphs this lookup does not see; exhaustion ends it. */
            while (position != 0u && RoboTextGsubSkipped(apply, position)) {
                position--;
            }
            if (RoboTextGsubSkipped(apply, position)) {
                return count;
            }
        }
        steps--;
    }
    return position;
}

/* ------------------------------------------------------------------ */
/* Pattern matching                                                    */
/* ------------------------------------------------------------------ */

/** The participating buffer indices a pattern matched at, in ascending order. */
typedef struct GsubMatch {
    size_t positions[RoboTextGsubMaxMatchGlyphs];
    size_t count;
} GsubMatch;

/*
 * The matchers **append**: a caller that already knows its first position (a coverage match, a context's
 * covered glyph) sets it and the matcher adds the rest. They return 1 on a full match and 0 on the first
 * mismatch, leaving `count` at whatever they reached — which is why a caller must not read `count` after a
 * failure.
 */

static int GsubMatchGlyphIds(RoboTextGsubCall *apply,
                             size_t start,
                             const GlyphId *wanted,
                             size_t count,
                             GsubMatch *match)
{
    size_t at = start;

    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph;

        at = RoboTextGsubNextParticipating(apply, at);
        if (at >= RoboTextGsubBufferCount(apply->buffer) || match->count >=
                                                              RoboTextGsubMaxMatchGlyphs ||
            !RoboTextGsubSpendProbes(apply, 1u)) {
            return 0;
        }
        glyph = RoboTextGsubBufferAt(apply->buffer, at);
        if (!glyph || glyph->glyphId != wanted[index]) {
            return 0;
        }
        match->positions[match->count++] = at;
        at++;
    }
    return 1;
}

static int GsubMatchClasses(RoboTextGsubCall *apply,
                            size_t start,
                            size_t classDefOffset,
                            const uint16_t *wanted,
                            size_t count,
                            GsubMatch *match)
{
    size_t at = start;

    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph;

        at = RoboTextGsubNextParticipating(apply, at);
        if (at >= RoboTextGsubBufferCount(apply->buffer) ||
            match->count >= RoboTextGsubMaxMatchGlyphs || !RoboTextGsubSpendProbes(apply, 1u)) {
            return 0;
        }
        glyph = RoboTextGsubBufferAt(apply->buffer, at);
        if (!glyph || RoboTextGsubClassOf(apply, classDefOffset, glyph->glyphId) != wanted[index]) {
            return 0;
        }
        match->positions[match->count++] = at;
        at++;
    }
    return 1;
}

/** Match @p count coverages, one per position — the format 3 rule shape. */
static int GsubMatchCoverages(RoboTextGsubCall *apply,
                              size_t start,
                              const size_t *coverageOffsets,
                              size_t count,
                              GsubMatch *match)
{
    size_t at = start;

    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph;

        at = RoboTextGsubNextParticipating(apply, at);
        if (at >= RoboTextGsubBufferCount(apply->buffer) ||
            match->count >= RoboTextGsubMaxMatchGlyphs || !RoboTextGsubSpendProbes(apply, 1u)) {
            return 0;
        }
        glyph = RoboTextGsubBufferAt(apply->buffer, at);
        if (!glyph || coverageOffsets[index] == 0u ||
            !RoboTextGsubCovered(apply, coverageOffsets[index], glyph->glyphId, NULL)) {
            return 0;
        }
        match->positions[match->count++] = at;
        at++;
    }
    return 1;
}

/** Match @p count participating glyphs *backwards* from @p before, nearest first. */
static int GsubMatchGlyphIdsBackward(RoboTextGsubCall *apply,
                                     size_t before,
                                     const GlyphId *wanted,
                                     size_t count)
{
    size_t at = before;

    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph;

        at = RoboTextGsubPrevParticipating(apply, at, 1u);
        if (at >= RoboTextGsubBufferCount(apply->buffer) || !RoboTextGsubSpendProbes(apply, 1u)) {
            return 0;
        }
        glyph = RoboTextGsubBufferAt(apply->buffer, at);
        if (!glyph || glyph->glyphId != wanted[index]) {
            return 0;
        }
    }
    return 1;
}

static int GsubMatchClassesBackward(RoboTextGsubCall *apply,
                                    size_t before,
                                    size_t classDefOffset,
                                    const uint16_t *wanted,
                                    size_t count)
{
    size_t at = before;

    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph;

        at = RoboTextGsubPrevParticipating(apply, at, 1u);
        if (at >= RoboTextGsubBufferCount(apply->buffer) || !RoboTextGsubSpendProbes(apply, 1u)) {
            return 0;
        }
        glyph = RoboTextGsubBufferAt(apply->buffer, at);
        if (!glyph || RoboTextGsubClassOf(apply, classDefOffset, glyph->glyphId) != wanted[index]) {
            return 0;
        }
    }
    return 1;
}

static int GsubMatchCoveragesBackward(RoboTextGsubCall *apply,
                                      size_t before,
                                      const size_t *coverageOffsets,
                                      size_t count)
{
    size_t at = before;

    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph;

        at = RoboTextGsubPrevParticipating(apply, at, 1u);
        if (at >= RoboTextGsubBufferCount(apply->buffer) || !RoboTextGsubSpendProbes(apply, 1u)) {
            return 0;
        }
        glyph = RoboTextGsubBufferAt(apply->buffer, at);
        if (!glyph || coverageOffsets[index] == 0u ||
            !RoboTextGsubCovered(apply, coverageOffsets[index], glyph->glyphId, NULL)) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Substitution housekeeping                                           */
/* ------------------------------------------------------------------ */

/** Record a substitution in the trace and tell the caller where its scan continues. */
static int GsubFinish(RoboTextGsubCall *apply,
                      size_t *outNext,
                      size_t index,
                      const GlyphId *from,
                      size_t fromCount,
                      const GlyphId *to,
                      size_t toCount,
                      RoboTextRange source,
                      size_t next)
{
    RoboTextGsubTrace(apply, index, from, fromCount, to, toCount, source);
    if (outNext) {
        *outNext = next;
    }
    return 1;
}

/** The source range of the glyphs at @p positions: the first's start to the last's end. */
static RoboTextRange GsubSpanOf(const RoboTextGsubCall *apply, const size_t *positions, size_t count)
{
    RoboTextRange span = RoboTextRangeMake(0u, 0u);

    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, positions[index]);

        if (!glyph) {
            continue;
        }
        if (index == 0u) {
            span = glyph->source;
        } else {
            if (glyph->source.start < span.start) {
                span.start = glyph->source.start;
            }
            if (glyph->source.end > span.end) {
                span.end = glyph->source.end;
            }
        }
    }
    return span;
}

/* ------------------------------------------------------------------ */
/* Single (type 1)                                                     */
/* ------------------------------------------------------------------ */

static int GsubApplySingle(RoboTextGsubCall *apply,
                           FontSpan table,
                           size_t offset,
                           size_t index,
                           size_t *outNext)
{
    uint16_t format = 0u;
    uint16_t coverageOffset = 0u;
    size_t coverage;
    const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, index);
    uint16_t coverageIndex = 0u;
    GlyphId replaced;
    GlyphId substitute;

    if (!glyph || !GsubU16(table, offset, &format) || !GsubU16(table, offset + 2u, &coverageOffset)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex)) {
        return 0;
    }
    replaced = (GlyphId)glyph->glyphId;

    if (format == 1u) {
        /* The format that stores one delta for the whole coverage: compact, and signed, so the substitute
         * may sit below the input glyph. */
        int16_t delta = 0;

        if (!GsubReadI16(table, offset + 4u, &delta)) {
            return 0;
        }
        substitute = (GlyphId)((int32_t)replaced + (int32_t)delta);
    } else if (format == 2u) {
        uint16_t glyphCount = 0u;
        uint16_t value = 0u;

        if (!GsubU16(table, offset + 4u, &glyphCount) || coverageIndex >= glyphCount ||
            !GsubU16(table, offset + 6u + (size_t)coverageIndex * 2u, &value)) {
            return 0;
        }
        substitute = (GlyphId)value;
    } else {
        return 0;
    }
    if (RoboTextGsubBufferSetGlyph(apply->buffer, index, substitute) != RoboOk) {
        return 0;
    }
    {
        /* The id changes and nothing else does: same source, same cluster, same level. That is what makes
         * `A -> A.alt` invisible to selection and to every later stage. */
        RoboTextShapingGlyph *edited = RoboTextGsubBufferAtMutable(apply->buffer, index);

        if (edited) {
            edited->inputGlyphId = replaced;
            edited->flags |= RoboTextShapingGlyphSubstituted;
        }
    }
    apply->stats.singleSubstitutions++;
    {
        /* The trace records glyph ids at the width the font uses, which is wider than the field read. */
        GlyphId produced = substitute;

        return GsubFinish(apply, outNext, index, &replaced, 1u, &produced, 1u, glyph->source,
                          index + 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Multiple (type 2)                                                   */
/* ------------------------------------------------------------------ */

static int GsubApplyMultiple(RoboTextGsubCall *apply,
                             FontSpan table,
                             size_t offset,
                             size_t index,
                             size_t *outNext)
{
    uint16_t format = 0u;
    uint16_t coverageOffset = 0u;
    size_t coverage;
    const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, index);
    uint16_t coverageIndex = 0u;
    uint16_t sequenceCount = 0u;
    uint16_t sequenceOffset = 0u;
    size_t sequence;
    uint16_t glyphCount = 0u;
    RoboTextShapingGlyph *products;
    GlyphId *ids;
    GlyphId replaced;
    int result;

    if (!glyph || !GsubU16(table, offset, &format) || format != 1u ||
        !GsubU16(table, offset + 2u, &coverageOffset) || !GsubU16(table, offset + 4u, &sequenceCount)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex) ||
        coverageIndex >= sequenceCount ||
        !GsubU16(table, offset + 6u + (size_t)coverageIndex * 2u, &sequenceOffset)) {
        return 0;
    }
    sequence = GsubOffset16(table, offset, sequenceOffset);
    if (sequence == 0u || !GsubU16(table, sequence, &glyphCount) || glyphCount == 0u) {
        return 0;
    }
    /*
     * The one place the buffer grows, so the one place the glyph limit has to be checked — before the
     * allocation, because the count comes from the font.
     */
    if (RoboTextGsubExceedsGlyphLimit(apply,
                                      RoboTextGsubBufferCount(apply->buffer) - 1u + (size_t)glyphCount)) {
        apply->limitReached = 1;
        return 0;
    }
    if (!RoboTextGsubSpendProbes(apply, glyphCount)) {
        return 0;
    }
    products = (RoboTextShapingGlyph *)RoboCalloc(glyphCount, sizeof(*products));
    ids = (GlyphId *)RoboCalloc(glyphCount, sizeof(*ids));
    if (!products || !ids) {
        RoboFree(products);
        RoboFree(ids);
        apply->failed = 1;
        return 0;
    }
    replaced = (GlyphId)glyph->glyphId;
    for (uint16_t product = 0u; product < glyphCount; product++) {
        uint16_t value = 0u;

        if (!GsubU16(table, sequence + 2u + (size_t)product * 2u, &value)) {
            RoboFree(products);
            RoboFree(ids);
            return 0;
        }
        /*
         * Provenance for a one-to-many substitution: each product names the **same** source range as the
         * glyph it came from, so `X -> G1 G2 G3` leaves three glyphs that all answer "X" to a selection, a
         * caret or a copy. Nothing invents a source scalar for a glyph the font produced.
         */
        products[product].glyphId = (GlyphId)value;
        products[product].source = glyph->source;
        products[product].cluster = glyph->cluster;
        products[product].bidiLevel = glyph->bidiLevel;
        products[product].inputGlyphId = replaced;
        products[product].flags = RoboTextShapingGlyphSubstituted | RoboTextShapingGlyphMultiple;
        ids[product] = (GlyphId)value;
    }
    if (RoboTextGsubBufferReplace(apply->buffer, index, 1u, products, glyphCount) != RoboOk) {
        RoboFree(products);
        RoboFree(ids);
        apply->failed = 1;
        return 0;
    }
    apply->stats.multipleSubstitutions++;
    result = GsubFinish(apply, outNext, index, &replaced, 1u, ids, glyphCount, glyph->source,
                        index + glyphCount);
    RoboFree(products);
    RoboFree(ids);
    return result;
}

/* ------------------------------------------------------------------ */
/* Alternate (type 3)                                                  */
/* ------------------------------------------------------------------ */

/** The alternate index the caller asked of the feature that named the running lookup; 0 by default. */
static uint32_t GsubAlternateIndex(const RoboTextGsubCall *apply)
{
    if (!apply || !apply->plan) {
        return 0u;
    }
    for (size_t index = 0u; index < apply->plan->featureCount; index++) {
        const RoboTextGsubFeature *feature = &apply->plan->features[index];

        if (memcmp(feature->tag, apply->featureTag, 4u) == 0) {
            return feature->value;
        }
    }
    return 0u;
}

static int GsubApplyAlternate(RoboTextGsubCall *apply,
                              FontSpan table,
                              size_t offset,
                              size_t index,
                              size_t *outNext)
{
    uint16_t format = 0u;
    uint16_t coverageOffset = 0u;
    size_t coverage;
    const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, index);
    uint16_t coverageIndex = 0u;
    uint16_t setCount = 0u;
    uint16_t setOffset = 0u;
    size_t set;
    uint16_t glyphCount = 0u;
    uint32_t wanted;
    uint16_t chosen = 0u;
    GlyphId replaced;

    if (!glyph || !GsubU16(table, offset, &format) || format != 1u ||
        !GsubU16(table, offset + 2u, &coverageOffset) || !GsubU16(table, offset + 4u, &setCount)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex) ||
        coverageIndex >= setCount ||
        !GsubU16(table, offset + 6u + (size_t)coverageIndex * 2u, &setOffset)) {
        return 0;
    }
    set = GsubOffset16(table, offset, setOffset);
    if (set == 0u || !GsubU16(table, set, &glyphCount) || glyphCount == 0u) {
        return 0;
    }
    /*
     * The selection policy is the caller's or the first: a feature setting's value is the alternate index,
     * clamped to what the font offers, and "no preference" means 0. Deterministic by construction — there is
     * no randomness and no font-order dependence, because an arbitrary alternate would make shaping
     * unverifiable.
     */
    wanted = GsubAlternateIndex(apply);
    if (wanted >= glyphCount) {
        wanted = 0u;
    }
    if (!GsubU16(table, set + 2u + (size_t)wanted * 2u, &chosen)) {
        return 0;
    }
    replaced = (GlyphId)glyph->glyphId;
    if (RoboTextGsubBufferSetGlyph(apply->buffer, index, (GlyphId)chosen) != RoboOk) {
        return 0;
    }
    {
        RoboTextShapingGlyph *edited = RoboTextGsubBufferAtMutable(apply->buffer, index);

        if (edited) {
            edited->inputGlyphId = replaced;
            edited->flags |= RoboTextShapingGlyphSubstituted | RoboTextShapingGlyphAlternate;
        }
    }
    apply->stats.alternateSubstitutions++;
    {
        GlyphId produced = chosen;

        return GsubFinish(apply, outNext, index, &replaced, 1u, &produced, 1u, glyph->source,
                          index + 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Ligature (type 4)                                                   */
/* ------------------------------------------------------------------ */

static int GsubApplyLigature(RoboTextGsubCall *apply,
                             FontSpan table,
                             size_t offset,
                             size_t index,
                             size_t *outNext)
{
    uint16_t format = 0u;
    uint16_t coverageOffset = 0u;
    size_t coverage;
    const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, index);
    uint16_t coverageIndex = 0u;
    uint16_t setCount = 0u;
    uint16_t setOffset = 0u;
    size_t set;
    uint16_t ligatureCount = 0u;

    if (!glyph || !GsubU16(table, offset, &format) || format != 1u ||
        !GsubU16(table, offset + 2u, &coverageOffset) || !GsubU16(table, offset + 4u, &setCount)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex) ||
        coverageIndex >= setCount ||
        !GsubU16(table, offset + 6u + (size_t)coverageIndex * 2u, &setOffset)) {
        return 0;
    }
    set = GsubOffset16(table, offset, setOffset);
    if (set == 0u || !GsubU16(table, set, &ligatureCount)) {
        return 0;
    }

    /*
     * The font's order decides, and the first ligature that matches wins: OpenType says a LigatureSet is
     * ordered so that a longer match precedes any shorter one that is its prefix, which is how `ffi`
     * resolves to one glyph rather than to `ff` plus `i`. Choosing the shortest or the longest ourselves
     * would be a second opinion about a font's own data.
     */
    for (uint16_t candidate = 0u; candidate < ligatureCount; candidate++) {
        uint16_t ligatureOffset = 0u;
        size_t ligature;
        uint16_t ligatureGlyph = 0u;
        uint16_t componentCount = 0u;
        GlyphId components[RoboTextGsubMaxMatchGlyphs];
        GsubMatch match;
        RoboTextRange span;
        GlyphId replaced;

        if (!GsubU16(table, set + 2u + (size_t)candidate * 2u, &ligatureOffset)) {
            return 0;
        }
        ligature = GsubOffset16(table, set, ligatureOffset);
        if (ligature == 0u || !GsubU16(table, ligature, &ligatureGlyph) ||
            !GsubU16(table, ligature + 2u, &componentCount)) {
            return 0;
        }
        if (componentCount < 2u || componentCount > RoboTextGsubMaxMatchGlyphs) {
            continue;
        }
        for (uint16_t component = 0u; component + 1u < componentCount; component++) {
            uint16_t value = 0u;

            if (!GsubU16(table, ligature + 4u + (size_t)component * 2u, &value)) {
                return 0;
            }
            components[component] = (GlyphId)value;
        }
        /*
         * The ligature's own glyph is the first component and the covered glyph *is* it, so it is in the
         * match by construction and the array starts with it.
         */
        match.count = 1u;
        match.positions[0] = index;
        if (!GsubMatchGlyphIds(apply, index + 1u, components, (size_t)componentCount - 1u, &match)) {
            continue;
        }
        span = GsubSpanOf(apply, match.positions, match.count);
        replaced = (GlyphId)glyph->glyphId;
        {
            /*
             * The product carries the union of its components' source ranges — the phase's §"ligature
             * provenance" — and the components it consumed leave the buffer. Glyphs the lookup's flags
             * skipped over (a mark between two components) stay where they are: they were never part of the
             * match, and reattaching them is a positioning job.
             */
            RoboTextShapingGlyph product;

            RoboTextShapingGlyphInit(&product);
            product.glyphId = (GlyphId)ligatureGlyph;
            product.source = span;
            product.cluster = glyph->cluster;
            product.bidiLevel = glyph->bidiLevel;
            product.flags = RoboTextShapingGlyphSubstituted | RoboTextShapingGlyphLigature;
            product.inputGlyphId = replaced;
            product.ligatureComponents = componentCount;
            /* Highest position first, so the earlier indices stay valid while the later ones go. */
            for (size_t drop = match.count; drop > 1u; drop--) {
                if (RoboTextGsubBufferReplace(apply->buffer, match.positions[drop - 1u], 1u, NULL, 0u) !=
                    RoboOk) {
                    apply->failed = 1;
                    return 0;
                }
            }
            if (RoboTextGsubBufferReplace(apply->buffer, index, 1u, &product, 1u) != RoboOk) {
                apply->failed = 1;
                return 0;
            }
        }
        apply->stats.ligatureSubstitutions++;
        {
            GlyphId produced = ligatureGlyph;

            return GsubFinish(apply, outNext, index, &replaced, 1u, &produced, 1u, span, index + 1u);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lookup records                                                      */
/* ------------------------------------------------------------------ */

/**
 * Apply a contextual rule's lookup records at the positions its input matched.
 *
 * The positions move as substitutions happen, and that is the subtlety this function exists for: a record's
 * sequence index names a *position in the input sequence*, so once an earlier record has changed the
 * buffer's length, every later position has shifted by exactly that change. Positions above the substitution
 * point are shifted by the difference the substitution made — measured, not assumed, because a lookup may
 * consume several glyphs or produce several.
 *
 * @param outContinue receives the position after the last input glyph the rule matched, so the engine's
 *                    scan resumes past the whole context rather than inside work it just did.
 * @return non-zero when at least one record substituted.
 */
static int GsubApplyRecords(RoboTextGsubCall *apply,
                            GsubMatch *match,
                            FontSpan table,
                            size_t records,
                            uint16_t recordCount,
                            size_t *outContinue)
{
    int applied = 0;
    size_t furthest = match->count != 0u ? match->positions[0] : 0u;

    for (uint16_t record = 0u; record < recordCount; record++) {
        uint16_t sequenceIndex = 0u;
        uint16_t lookupIndex = 0u;
        size_t before;
        size_t after;
        size_t at;
        size_t resolved = 0u;

        if (!GsubU16(table, records + (size_t)record * 4u, &sequenceIndex) ||
            !GsubU16(table, records + (size_t)record * 4u + 2u, &lookupIndex)) {
            break;
        }
        if (sequenceIndex >= match->count) {
            continue;
        }
        at = match->positions[sequenceIndex];
        if (at >= RoboTextGsubBufferCount(apply->buffer)) {
            continue;
        }
        before = RoboTextGsubBufferCount(apply->buffer);
        if (RoboTextGsubApplyLookupAt(apply, lookupIndex, at, &resolved)) {
            applied = 1;
        }
        after = RoboTextGsubBufferCount(apply->buffer);
        if (after != before) {
            long long delta = (long long)after - (long long)before;

            for (size_t position = 0u; position < match->count; position++) {
                if (match->positions[position] > at) {
                    /* The growth began at `at`, which is below this position, so the result cannot go
                     * negative. */
                    match->positions[position] =
                        (size_t)((long long)match->positions[position] + delta);
                }
            }
        }
        if (apply->failed) {
            break;
        }
    }
    for (size_t position = 0u; position < match->count; position++) {
        if (match->positions[position] > furthest) {
            furthest = match->positions[position];
        }
    }
    if (outContinue) {
        *outContinue = furthest + 1u;
    }
    if (applied) {
        apply->stats.contextualMatches++;
    }
    return applied;
}

/* ------------------------------------------------------------------ */
/* Context (type 5)                                                    */
/* ------------------------------------------------------------------ */

static int GsubApplyContextFormat1(RoboTextGsubCall *apply,
                                   FontSpan table,
                                   size_t offset,
                                   size_t index,
                                   const RoboTextShapingGlyph *glyph,
                                   size_t *outNext)
{
    uint16_t coverageOffset = 0u;
    uint16_t setCount = 0u;
    size_t coverage;
    uint16_t coverageIndex = 0u;
    uint16_t setOffset = 0u;
    size_t set;
    uint16_t ruleCount = 0u;

    if (!GsubU16(table, offset + 2u, &coverageOffset) || !GsubU16(table, offset + 4u, &setCount)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex) ||
        coverageIndex >= setCount ||
        !GsubU16(table, offset + 6u + (size_t)coverageIndex * 2u, &setOffset)) {
        return 0;
    }
    set = GsubOffset16(table, offset, setOffset);
    if (set == 0u || !GsubU16(table, set, &ruleCount)) {
        return 0;
    }
    for (uint16_t candidate = 0u; candidate < ruleCount; candidate++) {
        uint16_t ruleOffset = 0u;
        size_t rule;
        uint16_t glyphCount = 0u;
        uint16_t lookupCount = 0u;
        GlyphId input[RoboTextGsubMaxMatchGlyphs];
        GsubMatch match;
        size_t continueAt = 0u;

        if (!GsubU16(table, set + 2u + (size_t)candidate * 2u, &ruleOffset)) {
            return 0;
        }
        rule = GsubOffset16(table, set, ruleOffset);
        if (rule == 0u || !GsubU16(table, rule, &glyphCount) || glyphCount == 0u ||
            glyphCount > RoboTextGsubMaxMatchGlyphs) {
            continue;
        }
        for (uint16_t glyphIndex = 0u; glyphIndex + 1u < glyphCount; glyphIndex++) {
            uint16_t value = 0u;

            if (!GsubU16(table, rule + 2u + (size_t)glyphIndex * 2u, &value)) {
                return 0;
            }
            input[glyphIndex] = (GlyphId)value;
        }
        match.count = 1u;
        match.positions[0] = index;
        if (!GsubMatchGlyphIds(apply, index + 1u, input, (size_t)glyphCount - 1u, &match)) {
            continue;
        }
        if (!GsubU16(table, rule + 2u + (size_t)(glyphCount - 1u) * 2u, &lookupCount)) {
            return 0;
        }
        if (GsubApplyRecords(apply, &match, table, rule + 4u + (size_t)(glyphCount - 1u) * 2u,
                             lookupCount, &continueAt)) {
            if (outNext) {
                *outNext = continueAt;
            }
            return 1;
        }
    }
    return 0;
}

static int GsubApplyContextFormat2(RoboTextGsubCall *apply,
                                   FontSpan table,
                                   size_t offset,
                                   size_t index,
                                   const RoboTextShapingGlyph *glyph,
                                   size_t *outNext)
{
    uint16_t coverageOffset = 0u;
    uint16_t classDefOffset = 0u;
    uint16_t setCount = 0u;
    size_t coverage;
    size_t classDef;
    uint16_t coverageIndex = 0u;
    uint16_t setOffset = 0u;
    size_t set;
    uint16_t ruleCount = 0u;

    if (!GsubU16(table, offset + 2u, &coverageOffset) ||
        !GsubU16(table, offset + 4u, &classDefOffset) || !GsubU16(table, offset + 6u, &setCount)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    classDef = GsubOffset16(table, offset, classDefOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex) ||
        coverageIndex >= setCount || classDef == 0u ||
        !GsubU16(table, offset + 8u + (size_t)coverageIndex * 2u, &setOffset)) {
        return 0;
    }
    set = GsubOffset16(table, offset, setOffset);
    if (set == 0u || !GsubU16(table, set, &ruleCount)) {
        return 0;
    }
    for (uint16_t candidate = 0u; candidate < ruleCount; candidate++) {
        uint16_t ruleOffset = 0u;
        size_t rule;
        uint16_t glyphCount = 0u;
        uint16_t classes[RoboTextGsubMaxMatchGlyphs];
        uint16_t lookupCount = 0u;
        GsubMatch match;
        size_t continueAt = 0u;

        if (!GsubU16(table, set + 2u + (size_t)candidate * 2u, &ruleOffset)) {
            return 0;
        }
        rule = GsubOffset16(table, set, ruleOffset);
        if (rule == 0u || !GsubU16(table, rule, &glyphCount) || glyphCount == 0u ||
            glyphCount > RoboTextGsubMaxMatchGlyphs) {
            continue;
        }
        for (uint16_t glyphIndex = 0u; glyphIndex + 1u < glyphCount; glyphIndex++) {
            uint16_t value = 0u;

            if (!GsubU16(table, rule + 2u + (size_t)glyphIndex * 2u, &value)) {
                return 0;
            }
            classes[glyphIndex] = value;
        }
        match.count = 1u;
        match.positions[0] = index;
        if (!GsubMatchClasses(apply, index + 1u, classDef, classes, (size_t)glyphCount - 1u, &match)) {
            continue;
        }
        if (!GsubU16(table, rule + 2u + (size_t)(glyphCount - 1u) * 2u, &lookupCount)) {
            return 0;
        }
        if (GsubApplyRecords(apply, &match, table, rule + 4u + (size_t)(glyphCount - 1u) * 2u,
                             lookupCount, &continueAt)) {
            if (outNext) {
                *outNext = continueAt;
            }
            return 1;
        }
    }
    return 0;
}

static int GsubApplyContextFormat3(RoboTextGsubCall *apply,
                                   FontSpan table,
                                   size_t offset,
                                   size_t index,
                                   size_t *outNext)
{
    uint16_t glyphCount = 0u;
    uint16_t lookupCount = 0u;
    size_t coverages[RoboTextGsubMaxMatchGlyphs];
    GsubMatch match;
    size_t continueAt = 0u;

    if (!GsubU16(table, offset + 2u, &glyphCount) || !GsubU16(table, offset + 4u, &lookupCount)) {
        return 0;
    }
    if (glyphCount == 0u || glyphCount > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    for (uint16_t glyphIndex = 0u; glyphIndex < glyphCount; glyphIndex++) {
        uint16_t value = 0u;

        if (!GsubU16(table, offset + 6u + (size_t)glyphIndex * 2u, &value)) {
            return 0;
        }
        coverages[glyphIndex] = GsubOffset16(table, offset, value);
    }
    match.count = 0u;
    if (!GsubMatchCoverages(apply, index, coverages, glyphCount, &match)) {
        return 0;
    }
    if (GsubApplyRecords(apply, &match, table, offset + 6u + (size_t)glyphCount * 2u, lookupCount,
                         &continueAt)) {
        if (outNext) {
            *outNext = continueAt;
        }
        return 1;
    }
    return 0;
}

static int GsubApplyContext(RoboTextGsubCall *apply,
                            FontSpan table,
                            size_t offset,
                            size_t index,
                            size_t *outNext)
{
    uint16_t format = 0u;
    const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, index);

    if (!glyph || !GsubU16(table, offset, &format)) {
        return 0;
    }
    if (format == 1u) {
        return GsubApplyContextFormat1(apply, table, offset, index, glyph, outNext);
    }
    if (format == 2u) {
        return GsubApplyContextFormat2(apply, table, offset, index, glyph, outNext);
    }
    if (format == 3u) {
        return GsubApplyContextFormat3(apply, table, offset, index, outNext);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Chained context (type 6)                                            */
/* ------------------------------------------------------------------ */

/** A chaining rule's three parts, as read from either a glyph or a class rule body. */
typedef struct GsubChainPattern {
    GlyphId backtrackIds[RoboTextGsubMaxMatchGlyphs];
    uint16_t backtrackClasses[RoboTextGsubMaxMatchGlyphs];
    size_t backtrackCount;
    GlyphId inputIds[RoboTextGsubMaxMatchGlyphs];
    uint16_t inputClasses[RoboTextGsubMaxMatchGlyphs];
    size_t inputCount;
    GlyphId lookaheadIds[RoboTextGsubMaxMatchGlyphs];
    uint16_t lookaheadClasses[RoboTextGsubMaxMatchGlyphs];
    size_t lookaheadCount;
    size_t records;
    uint16_t recordCount;
} GsubChainPattern;

static int GsubReadChainGlyphRule(FontSpan table, size_t rule, GsubChainPattern *pattern)
{
    uint16_t count = 0u;
    size_t at = rule;

    if (!GsubU16(table, at, &count) || count > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    pattern->backtrackCount = count;
    at += 2u;
    for (size_t index = 0u; index < pattern->backtrackCount; index++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + index * 2u, &value)) {
            return 0;
        }
        pattern->backtrackIds[index] = (GlyphId)value;
    }
    at += pattern->backtrackCount * 2u;
    if (!GsubU16(table, at, &count) || count == 0u || count > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    pattern->inputCount = count;
    at += 2u;
    for (size_t index = 0u; index + 1u < pattern->inputCount; index++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + index * 2u, &value)) {
            return 0;
        }
        pattern->inputIds[index] = (GlyphId)value;
    }
    at += (pattern->inputCount - 1u) * 2u;
    if (!GsubU16(table, at, &count) || count > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    pattern->lookaheadCount = count;
    at += 2u;
    for (size_t index = 0u; index < pattern->lookaheadCount; index++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + index * 2u, &value)) {
            return 0;
        }
        pattern->lookaheadIds[index] = (GlyphId)value;
    }
    at += pattern->lookaheadCount * 2u;
    if (!GsubU16(table, at, &pattern->recordCount)) {
        return 0;
    }
    pattern->records = at + 2u;
    return 1;
}

static int GsubReadChainClassRule(FontSpan table, size_t rule, GsubChainPattern *pattern)
{
    uint16_t count = 0u;
    size_t at = rule;

    if (!GsubU16(table, at, &count) || count > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    pattern->backtrackCount = count;
    at += 2u;
    for (size_t index = 0u; index < pattern->backtrackCount; index++) {
        if (!GsubU16(table, at + index * 2u, &pattern->backtrackClasses[index])) {
            return 0;
        }
    }
    at += pattern->backtrackCount * 2u;
    if (!GsubU16(table, at, &count) || count == 0u || count > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    pattern->inputCount = count;
    at += 2u;
    for (size_t index = 0u; index + 1u < pattern->inputCount; index++) {
        if (!GsubU16(table, at + index * 2u, &pattern->inputClasses[index])) {
            return 0;
        }
    }
    at += (pattern->inputCount - 1u) * 2u;
    if (!GsubU16(table, at, &count) || count > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    pattern->lookaheadCount = count;
    at += 2u;
    for (size_t index = 0u; index < pattern->lookaheadCount; index++) {
        if (!GsubU16(table, at + index * 2u, &pattern->lookaheadClasses[index])) {
            return 0;
        }
    }
    at += pattern->lookaheadCount * 2u;
    if (!GsubU16(table, at, &pattern->recordCount)) {
        return 0;
    }
    pattern->records = at + 2u;
    return 1;
}

/**
 * Match a chain pattern's three parts and, on success, apply its records.
 *
 * The three class definitions are separate parameters because they are separate tables in the font: a
 * lookup's backtrack, input and lookahead may each be defined by class. A zero parameter means that part is
 * matched by glyph id instead, which is what the format 1 (and 3) rules state.
 */
static int GsubMatchChain(RoboTextGsubCall *apply,
                          FontSpan table,
                          const GsubChainPattern *pattern,
                          size_t index,
                          size_t inputClassDef,
                          size_t backtrackClassDef,
                          size_t lookaheadClassDef,
                          size_t *outNext)
{
    GsubMatch match;
    size_t continueAt = 0u;

    match.count = 1u;
    match.positions[0] = index;
    if (pattern->backtrackCount != 0u) {
        int matched = (backtrackClassDef != 0u)
                          ? GsubMatchClassesBackward(apply, index, backtrackClassDef,
                                                     pattern->backtrackClasses,
                                                     pattern->backtrackCount)
                          : GsubMatchGlyphIdsBackward(apply, index, pattern->backtrackIds,
                                                      pattern->backtrackCount);

        if (!matched) {
            return 0;
        }
    }
    if (pattern->inputCount > 1u) {
        int matched = (inputClassDef != 0u)
                          ? GsubMatchClasses(apply, index + 1u, inputClassDef, pattern->inputClasses,
                                             pattern->inputCount - 1u, &match)
                          : GsubMatchGlyphIds(apply, index + 1u, pattern->inputIds,
                                              pattern->inputCount - 1u, &match);

        if (!matched) {
            return 0;
        }
    }
    if (pattern->lookaheadCount != 0u) {
        size_t at = (pattern->inputCount > 1u) ? match.positions[match.count - 1u] + 1u : index + 1u;

        for (size_t step = 0u; step < pattern->lookaheadCount; step++) {
            const RoboTextShapingGlyph *glyph;
            int ok;

            at = RoboTextGsubNextParticipating(apply, at);
            if (at >= RoboTextGsubBufferCount(apply->buffer) || !RoboTextGsubSpendProbes(apply, 1u)) {
                return 0;
            }
            glyph = RoboTextGsubBufferAt(apply->buffer, at);
            if (!glyph) {
                return 0;
            }
            if (lookaheadClassDef != 0u) {
                ok = RoboTextGsubClassOf(apply, lookaheadClassDef, glyph->glyphId) ==
                     pattern->lookaheadClasses[step];
            } else {
                ok = glyph->glyphId == pattern->lookaheadIds[step];
            }
            if (!ok) {
                return 0;
            }
            at++;
        }
    }
    if (GsubApplyRecords(apply, &match, table, pattern->records, pattern->recordCount, &continueAt)) {
        if (outNext) {
            *outNext = continueAt;
        }
        return 1;
    }
    return 0;
}

static int GsubApplyChainFormat1(RoboTextGsubCall *apply,
                                 FontSpan table,
                                 size_t offset,
                                 size_t index,
                                 const RoboTextShapingGlyph *glyph,
                                 size_t *outNext)
{
    uint16_t coverageOffset = 0u;
    uint16_t setCount = 0u;
    size_t coverage;
    uint16_t coverageIndex = 0u;
    uint16_t setOffset = 0u;
    size_t set;
    uint16_t ruleCount = 0u;

    if (!GsubU16(table, offset + 2u, &coverageOffset) || !GsubU16(table, offset + 4u, &setCount)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex) ||
        coverageIndex >= setCount ||
        !GsubU16(table, offset + 6u + (size_t)coverageIndex * 2u, &setOffset)) {
        return 0;
    }
    set = GsubOffset16(table, offset, setOffset);
    if (set == 0u || !GsubU16(table, set, &ruleCount)) {
        return 0;
    }
    for (uint16_t candidate = 0u; candidate < ruleCount; candidate++) {
        uint16_t ruleOffset = 0u;
        size_t rule;
        GsubChainPattern pattern;

        if (!GsubU16(table, set + 2u + (size_t)candidate * 2u, &ruleOffset)) {
            return 0;
        }
        rule = GsubOffset16(table, set, ruleOffset);
        memset(&pattern, 0, sizeof(pattern));
        if (rule == 0u || !GsubReadChainGlyphRule(table, rule, &pattern)) {
            continue;
        }
        if (GsubMatchChain(apply, table, &pattern, index, 0u, 0u, 0u, outNext)) {
            return 1;
        }
    }
    return 0;
}

static int GsubApplyChainFormat2(RoboTextGsubCall *apply,
                                 FontSpan table,
                                 size_t offset,
                                 size_t index,
                                 const RoboTextShapingGlyph *glyph,
                                 size_t *outNext)
{
    uint16_t coverageOffset = 0u;
    uint16_t backtrackDefOffset = 0u;
    uint16_t inputDefOffset = 0u;
    uint16_t lookaheadDefOffset = 0u;
    uint16_t setCount = 0u;
    size_t coverage;
    size_t backtrackDef;
    size_t inputDef;
    size_t lookaheadDef;
    uint16_t coverageIndex = 0u;
    uint16_t setOffset = 0u;
    size_t set;
    uint16_t ruleCount = 0u;

    if (!GsubU16(table, offset + 2u, &coverageOffset) ||
        !GsubU16(table, offset + 4u, &backtrackDefOffset) ||
        !GsubU16(table, offset + 6u, &inputDefOffset) ||
        !GsubU16(table, offset + 8u, &lookaheadDefOffset) || !GsubU16(table, offset + 10u, &setCount)) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    backtrackDef = GsubOffset16(table, offset, backtrackDefOffset);
    inputDef = GsubOffset16(table, offset, inputDefOffset);
    lookaheadDef = GsubOffset16(table, offset, lookaheadDefOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex) ||
        coverageIndex >= setCount ||
        !GsubU16(table, offset + 12u + (size_t)coverageIndex * 2u, &setOffset)) {
        return 0;
    }
    set = GsubOffset16(table, offset, setOffset);
    if (set == 0u || !GsubU16(table, set, &ruleCount)) {
        return 0;
    }
    for (uint16_t candidate = 0u; candidate < ruleCount; candidate++) {
        uint16_t ruleOffset = 0u;
        size_t rule;
        GsubChainPattern pattern;

        if (!GsubU16(table, set + 2u + (size_t)candidate * 2u, &ruleOffset)) {
            return 0;
        }
        rule = GsubOffset16(table, set, ruleOffset);
        memset(&pattern, 0, sizeof(pattern));
        if (rule == 0u || !GsubReadChainClassRule(table, rule, &pattern)) {
            continue;
        }
        if (GsubMatchChain(apply, table, &pattern, index, inputDef, backtrackDef, lookaheadDef,
                           outNext)) {
            return 1;
        }
    }
    return 0;
}

static int GsubApplyChainFormat3(RoboTextGsubCall *apply,
                                 FontSpan table,
                                 size_t offset,
                                 size_t index,
                                 size_t *outNext)
{
    uint16_t backtrackCount = 0u;
    uint16_t inputCount = 0u;
    uint16_t lookaheadCount = 0u;
    uint16_t recordCount = 0u;
    size_t at = offset + 2u;
    size_t backtracks[RoboTextGsubMaxMatchGlyphs];
    size_t inputs[RoboTextGsubMaxMatchGlyphs];
    size_t lookaheads[RoboTextGsubMaxMatchGlyphs];
    GsubMatch match;
    size_t continueAt = 0u;

    if (!GsubU16(table, at, &backtrackCount) || backtrackCount > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    at += 2u;
    for (size_t step = 0u; step < backtrackCount; step++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + step * 2u, &value)) {
            return 0;
        }
        backtracks[step] = GsubOffset16(table, offset, value);
    }
    at += (size_t)backtrackCount * 2u;
    if (!GsubU16(table, at, &inputCount) || inputCount == 0u ||
        inputCount > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    at += 2u;
    for (size_t step = 0u; step < inputCount; step++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + step * 2u, &value)) {
            return 0;
        }
        inputs[step] = GsubOffset16(table, offset, value);
    }
    at += (size_t)inputCount * 2u;
    if (!GsubU16(table, at, &lookaheadCount) || lookaheadCount > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    at += 2u;
    for (size_t step = 0u; step < lookaheadCount; step++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + step * 2u, &value)) {
            return 0;
        }
        lookaheads[step] = GsubOffset16(table, offset, value);
    }
    at += (size_t)lookaheadCount * 2u;
    if (!GsubU16(table, at, &recordCount)) {
        return 0;
    }
    at += 2u;

    if (backtrackCount != 0u && !GsubMatchCoveragesBackward(apply, index, backtracks, backtrackCount)) {
        return 0;
    }
    match.count = 0u;
    if (!GsubMatchCoverages(apply, index, inputs, inputCount, &match)) {
        return 0;
    }
    if (lookaheadCount != 0u) {
        size_t look = (inputCount > 1u) ? match.positions[match.count - 1u] + 1u : index + 1u;

        for (size_t step = 0u; step < lookaheadCount; step++) {
            const RoboTextShapingGlyph *seen;

            look = RoboTextGsubNextParticipating(apply, look);
            if (look >= RoboTextGsubBufferCount(apply->buffer) ||
                !RoboTextGsubSpendProbes(apply, 1u)) {
                return 0;
            }
            seen = RoboTextGsubBufferAt(apply->buffer, look);
            if (!seen || lookaheads[step] == 0u ||
                !RoboTextGsubCovered(apply, lookaheads[step], seen->glyphId, NULL)) {
                return 0;
            }
            look++;
        }
    }
    if (GsubApplyRecords(apply, &match, table, at, recordCount, &continueAt)) {
        if (outNext) {
            *outNext = continueAt;
        }
        return 1;
    }
    return 0;
}

static int GsubApplyChainingContext(RoboTextGsubCall *apply,
                                    FontSpan table,
                                    size_t offset,
                                    size_t index,
                                    size_t *outNext)
{
    uint16_t format = 0u;
    const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, index);

    if (!glyph || !GsubU16(table, offset, &format)) {
        return 0;
    }
    if (format == 1u) {
        return GsubApplyChainFormat1(apply, table, offset, index, glyph, outNext);
    }
    if (format == 2u) {
        return GsubApplyChainFormat2(apply, table, offset, index, glyph, outNext);
    }
    if (format == 3u) {
        return GsubApplyChainFormat3(apply, table, offset, index, outNext);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reverse chaining (type 8)                                           */
/* ------------------------------------------------------------------ */

/**
 * Reverse chaining single substitution.
 *
 * The format stores a *list* of substitutes, one per covered glyph, which is what makes a reversed scan
 * possible: the substitute is chosen by the input glyph's coverage index rather than by a rule the buffer
 * would have to be searched for. Nothing about the direction of the *text* follows from the scan order —
 * that is the bidi stage's, and this lookup's own name is the only thing that is backwards.
 */
static int GsubApplyReverse(RoboTextGsubCall *apply,
                            FontSpan table,
                            size_t offset,
                            size_t index,
                            size_t *outNext)
{
    uint16_t format = 0u;
    uint16_t coverageOffset = 0u;
    uint16_t backtrackCount = 0u;
    uint16_t lookaheadCount = 0u;
    uint16_t glyphCount = 0u;
    size_t coverage;
    size_t at;
    const RoboTextShapingGlyph *glyph = RoboTextGsubBufferAt(apply->buffer, index);
    uint16_t coverageIndex = 0u;
    size_t backtracks[RoboTextGsubMaxMatchGlyphs];
    size_t lookaheads[RoboTextGsubMaxMatchGlyphs];
    uint16_t substitute = 0u;
    GlyphId replaced;

    if (!glyph || !GsubU16(table, offset, &format) || format != 1u ||
        !GsubU16(table, offset + 2u, &coverageOffset) || !GsubU16(table, offset + 4u, &backtrackCount) ||
        backtrackCount > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    coverage = GsubOffset16(table, offset, coverageOffset);
    if (!RoboTextGsubCovered(apply, coverage, glyph->glyphId, &coverageIndex)) {
        return 0;
    }
    at = offset + 6u;
    for (size_t step = 0u; step < backtrackCount; step++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + step * 2u, &value)) {
            return 0;
        }
        backtracks[step] = GsubOffset16(table, offset, value);
    }
    at += (size_t)backtrackCount * 2u;
    if (!GsubU16(table, at, &lookaheadCount) || lookaheadCount > RoboTextGsubMaxMatchGlyphs) {
        return 0;
    }
    at += 2u;
    for (size_t step = 0u; step < lookaheadCount; step++) {
        uint16_t value = 0u;

        if (!GsubU16(table, at + step * 2u, &value)) {
            return 0;
        }
        lookaheads[step] = GsubOffset16(table, offset, value);
    }
    at += (size_t)lookaheadCount * 2u;
    if (!GsubU16(table, at, &glyphCount) || coverageIndex >= glyphCount ||
        !GsubU16(table, at + 2u + (size_t)coverageIndex * 2u, &substitute)) {
        return 0;
    }
    if (backtrackCount != 0u && !GsubMatchCoveragesBackward(apply, index, backtracks, backtrackCount)) {
        return 0;
    }
    if (lookaheadCount != 0u) {
        size_t look = index + 1u;

        for (size_t step = 0u; step < lookaheadCount; step++) {
            const RoboTextShapingGlyph *seen;

            look = RoboTextGsubNextParticipating(apply, look);
            if (look >= RoboTextGsubBufferCount(apply->buffer) ||
                !RoboTextGsubSpendProbes(apply, 1u)) {
                return 0;
            }
            seen = RoboTextGsubBufferAt(apply->buffer, look);
            if (!seen || lookaheads[step] == 0u ||
                !RoboTextGsubCovered(apply, lookaheads[step], seen->glyphId, NULL)) {
                return 0;
            }
            look++;
        }
    }
    replaced = (GlyphId)glyph->glyphId;
    if (RoboTextGsubBufferSetGlyph(apply->buffer, index, (GlyphId)substitute) != RoboOk) {
        return 0;
    }
    {
        RoboTextShapingGlyph *edited = RoboTextGsubBufferAtMutable(apply->buffer, index);

        if (edited) {
            edited->inputGlyphId = replaced;
            edited->flags |= RoboTextShapingGlyphSubstituted;
        }
    }
    apply->stats.reverseSubstitutions++;
    {
        GlyphId produced = substitute;

        return GsubFinish(apply, outNext, index, &replaced, 1u, &produced, 1u, glyph->source,
                          index + 1u);
    }
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

int RoboTextGsubApplySubtable(RoboTextGsubCall *apply,
                              uint16_t lookupType,
                              size_t offset,
                              size_t index,
                              size_t *outNext)
{
    FontSpan table;

    if (!apply || !apply->gsub || !apply->buffer || offset == 0u ||
        index >= RoboTextGsubBufferCount(apply->buffer)) {
        return 0;
    }
    table = apply->gsub->table;
    switch (lookupType) {
        case RoboTextGsubLookupSingle:
            return GsubApplySingle(apply, table, offset, index, outNext);
        case RoboTextGsubLookupMultiple:
            return GsubApplyMultiple(apply, table, offset, index, outNext);
        case RoboTextGsubLookupAlternate:
            return GsubApplyAlternate(apply, table, offset, index, outNext);
        case RoboTextGsubLookupLigature:
            return GsubApplyLigature(apply, table, offset, index, outNext);
        case RoboTextGsubLookupContext:
            return GsubApplyContext(apply, table, offset, index, outNext);
        case RoboTextGsubLookupChainingContext:
            return GsubApplyChainingContext(apply, table, offset, index, outNext);
        case RoboTextGsubLookupReverseChaining:
            return GsubApplyReverse(apply, table, offset, index, outNext);
        case RoboTextGsubLookupExtension: {
            /*
             * An extension lookup is a wrapper: a `uint16` format 1, the real `uint16` type and a `uint32`
             * offset to the real subtable, both relative to the wrapper. It exists because a lookup's
             * subtable offsets are 16-bit while a large font's subtables can sit beyond 64 KiB from the
             * LookupList — so unwrapping is the whole implementation, and the real type is what the trace
             * has to report.
             */
            uint16_t format = 0u;
            uint16_t innerType = 0u;
            uint32_t innerOffset = 0u;
            size_t resolved;
            uint16_t outerType;
            int result;

            if (!GsubU16(table, offset, &format) || format != 1u ||
                !GsubU16(table, offset + 2u, &innerType) ||
                !GsubReadU32(table, offset + 4u, &innerOffset)) {
                return 0;
            }
            if (innerType == RoboTextGsubLookupExtension || !FontGsubLookupTypeIsKnown(innerType)) {
                return 0;
            }
            resolved = GsubOffset32(table, offset, innerOffset);
            if (resolved == 0u) {
                return 0;
            }
            outerType = apply->lookupType;
            apply->lookupType = innerType;
            result = RoboTextGsubApplySubtable(apply, innerType, resolved, index, outNext);
            apply->lookupType = outerType;
            return result;
        }
        default:
            return 0;
    }
}
