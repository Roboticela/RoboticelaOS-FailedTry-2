/*
 * GPOS subtable readers (Phase 12.11) — one reader per lookup type this build executes.
 *
 * The file is small on purpose. Phase 5 owns the table, `RoboTextOtInternal.h`'s shared helpers own the
 * coverage cache, the class cache, the lookup flags and the participant walk, and what is left for this file
 * is the arithmetic of three formats: a value record (four signed font-unit fields and up to four device
 * offsets), the single adjustment, the pair adjustment in both of its layouts, and the extension wrapper.
 *
 * Two rules shape every reader here.
 *
 *   **Nothing is trusted.** A count, an offset and a class index are the font's numbers; each is checked
 *   against the table or the array it indexes before it is used, and a reader that finds a bad one returns
 *   0 — which the scan reads as "this subtable did not match". A malformed positioning subtable therefore
 *   costs the run its spacing, never its glyphs and never its life.
 *
 *   **A value record is font units, and the conversion happened once.** `RoboTextFontUnitsToDevice` is the
 *   module's only scaling door; the caller asks it for the *unit* and hands the engine
 *   @ref RoboTextGposOptions::deviceScale, which the readers multiply by. A record's device-table delta is
 *   evaluated only when the caller supplied a ppem, and is counted when it was not: a hinting delta applied
 *   at an unknown size is a worse answer than no delta.
 */

#include "RoboText/RoboTextGpos.h"

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboText/RoboTextOtLayout.h"
#include "RoboTextOtInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The reads, and the one mistake they must not allow                  */
/* ------------------------------------------------------------------ */

/*
 * `RoboTextOtReadU16/U32/I16` answer a `Result`, and `RoboOk` is **zero**: `if (!RoboTextOtReadU32(...))`
 * inverts the question and takes the failure branch on every successful read. Inverted reads are exactly
 * what hid single-substitution format 1, the extension wrapper and GDEF's mark filtering set in the
 * substitution engine, so no reader in this file calls one of those functions outside a macro.
 */
#define GposU16(table, offset, out) (RoboTextOtReadU16((table), (offset), (out)) == RoboOk)
#define GposI16(table, offset, out) (RoboTextOtReadI16((table), (offset), (out)) == RoboOk)
#define GposU32(table, offset, out) (RoboTextOtReadU32((table), (offset), (out)) == RoboOk)

/* ---- ValueRecord format bits (OpenType, chapter 2) ---- */

#define GposValueXPlacement 0x0001u
#define GposValueYPlacement 0x0002u
#define GposValueXAdvance 0x0004u
#define GposValueYAdvance 0x0008u
#define GposValueXPlacementDevice 0x0010u
#define GposValueYPlacementDevice 0x0020u
#define GposValueXAdvanceDevice 0x0040u
#define GposValueYAdvanceDevice 0x0080u
#define GposValueFieldMask 0x00FFu

/** Bytes a value record of @p format occupies: two per field the format names, in the order above. */
static size_t GposValueSize(uint16_t format)
{
    size_t size = 0u;

    for (uint16_t bit = 0x0001u; bit <= GposValueYAdvanceDevice; bit <<= 1) {
        if ((format & bit) != 0u) {
            size += 2u;
        }
    }
    return size;
}

/**
 * The device delta for @p ppem, or 0 when there is none to apply.
 *
 * OpenType packs a device table's deltas three ways (2, 4 and 8 bits per delta, right to left within each
 * word) and a variation index is a delta format of its own. The index is the size *within the recorded
 * range*: a size outside `[startSize, endSize]` has no correction by definition, which is not an error and
 * is why this answers "no delta" rather than "failed".
 */
static int GposDeviceDelta(const FontOtDevice *device, uint16_t ppem, int32_t *outDelta)
{
    uint32_t perWord;
    uint32_t bits;
    uint32_t index;
    uint32_t word;
    uint32_t shift;
    uint32_t raw;
    uint32_t signBit;

    if (!device || !outDelta || device->deltaFormat < 1u || device->deltaFormat > 3u) {
        return 0;
    }
    if (ppem < device->startSize || ppem > device->endSize) {
        return 0;
    }
    index = (uint32_t)ppem - (uint32_t)device->startSize;
    perWord = (device->deltaFormat == 1u) ? 8u : ((device->deltaFormat == 2u) ? 4u : 2u);
    bits = (device->deltaFormat == 1u) ? 2u : ((device->deltaFormat == 2u) ? 4u : 8u);
    if (device->deltaValues == NULL || (index / perWord) >= (uint32_t)device->deltaValueCount) {
        return 0;
    }
    word = (uint32_t)device->deltaValues[index / perWord];
    shift = (index % perWord) * bits;
    raw = (word >> shift) & ((1u << bits) - 1u);
    signBit = 1u << (bits - 1u);
    *outDelta = (raw & signBit) ? (int32_t)raw - (int32_t)(1u << bits) : (int32_t)raw;
    return 1;
}

/** The device delta at an offset inside @p base, or 0 when the offset is absent or unrunnable. */
static int GposDeviceAt(RoboTextOtCall *apply, size_t base, uint16_t offset, int32_t *outDelta)
{
    FontOtDevice device;
    size_t absolute;
    int applied = 0;

    if (offset == 0u || !apply || !apply->layout || apply->devicePpem == 0u) {
        return 0;
    }
    absolute = RoboTextOtOffset16(apply->layout->table, base, offset);
    if (absolute == 0u) {
        return 0;
    }
    memset(&device, 0, sizeof(device));
    if (FontOtDeviceParse(apply->layout->table, absolute, &device) == RoboOk) {
        applied = GposDeviceDelta(&device, apply->devicePpem, outDelta);
    }
    FontOtDeviceClear(&device);
    return applied;
}

/**
 * Read the value record at @p offset, whose device offsets are resolved against @p base.
 *
 * The four placement/advance fields become device pixels through the caller's scale; the four device
 * offsets are followed and folded in, or counted as unevaluated when no size was known. @return 1 when the
 * record read, 0 when the table is too short for the format it declares.
 */
static int GposReadValue(RoboTextOtCall *apply,
                         size_t base,
                         size_t offset,
                         uint16_t format,
                         RoboTextGposValue *out)
{
    FontSpan table = apply->layout->table;
    size_t cursor = offset;
    int16_t field = 0;
    int pendingDevice = 0;

    if (!out || (format & ~GposValueFieldMask) != 0u) {
        /* A format naming a bit OpenType does not define: refused rather than guessed at, because guessing
         * would read the next field's bytes as this one's. */
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (apply->deviceScale <= 0.0f) {
        apply->gposStats.recordsUnevaluated++;
        return 0;
    }
    if ((format & GposValueXPlacement) != 0u) {
        if (!GposI16(table, cursor, &field)) {
            return 0;
        }
        out->xPlacement = (float)field * apply->deviceScale;
        cursor += 2u;
    }
    if ((format & GposValueYPlacement) != 0u) {
        if (!GposI16(table, cursor, &field)) {
            return 0;
        }
        out->yPlacement = (float)field * apply->deviceScale;
        cursor += 2u;
    }
    if ((format & GposValueXAdvance) != 0u) {
        if (!GposI16(table, cursor, &field)) {
            return 0;
        }
        out->xAdvance = (float)field * apply->deviceScale;
        cursor += 2u;
    }
    if ((format & GposValueYAdvance) != 0u) {
        if (!GposI16(table, cursor, &field)) {
            return 0;
        }
        out->yAdvance = (float)field * apply->deviceScale;
        cursor += 2u;
    }
    /*
     * The device offsets, in the same order and for the same four fields. Each is an Offset16 to a Device
     * or VariationIndex table; a delta is a device unit (1/64 px at the recorded ppem) rather than a font
     * unit, so it is added *after* the scaling above and is not multiplied by it.
     */
    for (uint16_t bit = GposValueXPlacementDevice; bit <= GposValueYAdvanceDevice; bit <<= 1) {
        uint16_t deviceOffset = 0u;
        int32_t delta = 0;

        if ((format & bit) == 0u) {
            continue;
        }
        if (!GposU16(table, cursor, &deviceOffset)) {
            return 0;
        }
        cursor += 2u;
        if (deviceOffset == 0u) {
            continue;
        }
        if (GposDeviceAt(apply, base, deviceOffset, &delta)) {
            float pixels = (float)delta / 64.0f;

            if (bit == GposValueXPlacementDevice) {
                out->xPlacement += pixels;
            } else if (bit == GposValueYPlacementDevice) {
                out->yPlacement += pixels;
            } else if (bit == GposValueXAdvanceDevice) {
                out->xAdvance += pixels;
            } else {
                out->yAdvance += pixels;
            }
            out->deviceApplied = 1;
            apply->gposStats.deviceCorrections++;
        } else {
            pendingDevice = 1;
        }
    }
    if (pendingDevice) {
        apply->gposStats.deviceUnevaluated++;
    }
    return 1;
}

/** Non-zero when @p value would move a glyph or the pen: the test for "the font had an opinion". */
static int GposValueIsZero(const RoboTextGposValue *value)
{
    if (!value) {
        return 1;
    }
    return (value->xPlacement == 0.0f && value->yPlacement == 0.0f && value->xAdvance == 0.0f &&
            value->yAdvance == 0.0f)
               ? 1
               : 0;
}

/* ------------------------------------------------------------------ */
/* Applying one adjustment                                             */
/* ------------------------------------------------------------------ */

/** Write @p value onto buffer position @p position, remembering that the font positioned it. */
static void GposApplyTo(RoboTextOtCall *apply, size_t position, const RoboTextGposValue *value)
{
    RoboTextShapingGlyph *glyph = RoboTextShapingBufferAtMutable(apply->buffer, position);

    if (!glyph || !value) {
        return;
    }
    glyph->offsetAdjustX += value->xPlacement;
    glyph->offsetAdjustY += value->yPlacement;
    glyph->advanceAdjustX += value->xAdvance;
    glyph->advanceAdjustY += value->yAdvance;
    if (!GposValueIsZero(value) && (glyph->flags & RoboTextShapingGlyphPositioned) == 0u) {
        /* Counted once per glyph rather than once per record: the number a caller wants is "how many glyphs
         * the font moved", and two lookups moving one glyph is still one glyph. The flag is also what a
         * reader compares before and after the stage, so it is set even when the value is zero — the font
         * had an opinion, it said nothing. */
        apply->gposStats.adjustedGlyphs++;
    }
    glyph->flags |= RoboTextShapingGlyphPositioned;
}

/** The trace line, written by @ref RoboTextGposTrace. */
static void GposRecord(RoboTextOtCall *apply,
                       size_t position,
                       size_t secondPosition,
                       const RoboTextGposValue *first,
                       const RoboTextGposValue *second,
                       uint16_t secondGlyph,
                       uint16_t firstClass,
                       uint16_t secondClass)
{
    RoboTextGposTrace(apply, position, secondPosition, first, second, secondGlyph, firstClass, secondClass);
}

/** @return 1 when the glyph at @p index takes part in a lookup at all. A control or an ignored glyph does not. */
static int GposParticipates(RoboTextOtCall *apply, size_t index)
{
    if (index >= RoboTextShapingBufferCount(apply->buffer)) {
        return 0;
    }
    return RoboTextOtSkipped(apply, index) ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* Lookup type 1 — single adjustment                                   */
/* ------------------------------------------------------------------ */

static int GposApplySingle(RoboTextOtCall *apply, size_t base, size_t index, size_t *outNext)
{
    FontSpan table = apply->layout->table;
    const RoboTextShapingGlyph *glyph = RoboTextShapingBufferAt(apply->buffer, index);
    uint16_t format = 0u;
    uint16_t coverageOffset = 0u;
    uint16_t valueFormat = 0u;
    size_t coverage;
    RoboTextGposValue value;

    if (!glyph || !GposParticipates(apply, index)) {
        return 0;
    }
    /* One probe per position, before a byte of the subtable is read: the budget is what keeps a font with a
     * long subtable list from turning a scan into quadratic work, and this is the charge a single-adjustment
     * lookup makes for asking "is this glyph covered". */
    if (!RoboTextOtSpendProbes(apply, 1u)) {
        return 0;
    }
    if (!GposU16(table, base, &format) || !GposU16(table, base + 2u, &coverageOffset) ||
        !GposU16(table, base + 4u, &valueFormat)) {
        return 0;
    }
    coverage = RoboTextOtOffset16(table, base, coverageOffset);
    if (coverage == 0u) {
        return 0;
    }
    if (format == 1u) {
        /* One record, for every glyph the coverage names. */
        if (!RoboTextOtCovered(apply, coverage, glyph->glyphId, NULL)) {
            return 0;
        }
        if (!GposReadValue(apply, base, base + 6u, valueFormat, &value)) {
            return 0;
        }
        GposApplyTo(apply, index, &value);
        apply->gposStats.singleAdjustments++;
        GposRecord(apply, index, (size_t)-1, &value, NULL, RoboTextGposNoGlyph, RoboTextGposNoClass,
                   RoboTextGposNoClass);
        *outNext = index + 1u;
        return 1;
    }
    if (format == 2u) {
        uint16_t valueCount = 0u;
        uint16_t coverageIndex = 0u;
        size_t recordSize = GposValueSize(valueFormat);

        if (!GposU16(table, base + 6u, &valueCount)) {
            return 0;
        }
        if (!RoboTextOtCovered(apply, coverage, glyph->glyphId, &coverageIndex)) {
            return 0;
        }
        if (coverageIndex >= valueCount || recordSize == 0u) {
            return 0;
        }
        /* A record whose format names no field at all is legal and means "no adjustment": the count and the
         * coverage still say the font knows this glyph, which is why the record is read rather than skipped. */
        if (!GposReadValue(apply, base, base + 8u + (size_t)coverageIndex * recordSize, valueFormat, &value)) {
            return 0;
        }
        GposApplyTo(apply, index, &value);
        apply->gposStats.singleAdjustments++;
        GposRecord(apply, index, (size_t)-1, &value, NULL, RoboTextGposNoGlyph, RoboTextGposNoClass,
                   RoboTextGposNoClass);
        *outNext = index + 1u;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lookup type 2 — pair adjustment (the kerning path)                   */
/* ------------------------------------------------------------------ */

/**
 * Apply a pair adjustment found for the glyph at @p index and the next participating glyph.
 *
 * @param classPair non-zero for format 2, which is what the caller's statistics distinguish: a font that
 *                  kerns by class and a font that lists its pairs one by one are different engineering, and
 *                  a report that called both "pairs" would hide which one ran.
 */
static int GposApplyPairValues(RoboTextOtCall *apply,
                               size_t index,
                               size_t secondIndex,
                               const RoboTextGposValue *first,
                               const RoboTextGposValue *second,
                               int classPair,
                               uint16_t firstClass,
                               uint16_t secondClass)
{
    const RoboTextShapingGlyph *secondEntry = RoboTextShapingBufferAt(apply->buffer, secondIndex);

    GposApplyTo(apply, index, first);
    GposApplyTo(apply, secondIndex, second);
    if (classPair) {
        apply->gposStats.classPairAdjustments++;
    } else {
        apply->gposStats.pairAdjustments++;
    }
    GposRecord(apply, index, secondIndex, first, second,
               secondEntry ? secondEntry->glyphId : RoboTextGposNoGlyph, firstClass, secondClass);
    return 1;
}

static int GposApplyPairFormat1(RoboTextOtCall *apply,
                                size_t base,
                                size_t index,
                                size_t secondIndex,
                                size_t coverage,
                                uint16_t valueFormat1,
                                uint16_t valueFormat2)
{
    FontSpan table = apply->layout->table;
    const RoboTextShapingGlyph *first = RoboTextShapingBufferAt(apply->buffer, index);
    const RoboTextShapingGlyph *second = RoboTextShapingBufferAt(apply->buffer, secondIndex);
    uint16_t pairSetCount = 0u;
    uint16_t coverageIndex = 0u;
    uint16_t pairSetOffset = 0u;
    size_t pairSet;
    size_t recordSize;
    uint16_t pairValueCount = 0u;

    if (!first || !second) {
        return 0;
    }
    if (!GposU16(table, base + 8u, &pairSetCount)) {
        return 0;
    }
    if (!RoboTextOtCovered(apply, coverage, first->glyphId, &coverageIndex)) {
        return 0;
    }
    if (coverageIndex >= pairSetCount) {
        return 0;
    }
    if (!GposU16(table, base + 10u + (size_t)coverageIndex * 2u, &pairSetOffset)) {
        return 0;
    }
    pairSet = RoboTextOtOffset16(table, base, pairSetOffset);
    if (pairSet == 0u || !GposU16(table, pairSet, &pairValueCount)) {
        return 0;
    }
    recordSize = 2u + GposValueSize(valueFormat1) + GposValueSize(valueFormat2);
    for (uint16_t pair = 0u; pair < pairValueCount; pair++) {
        size_t record = pairSet + 2u + (size_t)pair * recordSize;
        uint16_t secondGlyph = 0u;
        RoboTextGposValue firstValue;
        RoboTextGposValue secondValue;

        if (!RoboTextOtSpendProbes(apply, 1u)) {
            return 0;
        }
        if (!GposU16(table, record, &secondGlyph)) {
            apply->gposStats.subtablesMalformed++;
            return 0;
        }
        if (secondGlyph != second->glyphId) {
            continue;
        }
        if (!GposReadValue(apply, base, record + 2u, valueFormat1, &firstValue) ||
            !GposReadValue(apply, base, record + 2u + GposValueSize(valueFormat1), valueFormat2,
                           &secondValue)) {
            apply->gposStats.subtablesMalformed++;
            return 0;
        }
        return GposApplyPairValues(apply, index, secondIndex, &firstValue, &secondValue, 0,
                                   RoboTextGposNoClass, RoboTextGposNoClass);
    }
    return 0;
}

static int GposApplyPairFormat2(RoboTextOtCall *apply,
                                size_t base,
                                size_t index,
                                size_t secondIndex,
                                size_t coverage,
                                uint16_t valueFormat1,
                                uint16_t valueFormat2)
{
    FontSpan table = apply->layout->table;
    const RoboTextShapingGlyph *first = RoboTextShapingBufferAt(apply->buffer, index);
    const RoboTextShapingGlyph *second = RoboTextShapingBufferAt(apply->buffer, secondIndex);
    uint16_t classDef1Offset = 0u;
    uint16_t classDef2Offset = 0u;
    uint16_t class1Count = 0u;
    uint16_t class2Count = 0u;
    size_t classDef1;
    size_t classDef2;
    uint16_t class1;
    uint16_t class2;
    size_t recordSize;
    size_t record;
    RoboTextGposValue firstValue;
    RoboTextGposValue secondValue;

    if (!first || !second) {
        return 0;
    }
    if (!GposU16(table, base + 8u, &classDef1Offset) || !GposU16(table, base + 10u, &classDef2Offset) ||
        !GposU16(table, base + 12u, &class1Count) || !GposU16(table, base + 14u, &class2Count)) {
        return 0;
    }
    if (!RoboTextOtCovered(apply, coverage, first->glyphId, NULL)) {
        return 0;
    }
    classDef1 = RoboTextOtOffset16(table, base, classDef1Offset);
    classDef2 = RoboTextOtOffset16(table, base, classDef2Offset);
    if (classDef1 == 0u || classDef2 == 0u || class1Count == 0u || class2Count == 0u) {
        return 0;
    }
    if (!RoboTextOtSpendProbes(apply, 2u)) {
        return 0;
    }
    /*
     * A glyph the class definition does not list is class 0, which is a real class in the matrix — an
     * unlisted glyph is not "no class", it is the default one, and a font that kerns everything-not-listed
     * against a class is expressing exactly that.
     */
    class1 = RoboTextOtClassOf(apply, classDef1, first->glyphId);
    class2 = RoboTextOtClassOf(apply, classDef2, second->glyphId);
    if (class1 >= class1Count || class2 >= class2Count) {
        return 0;
    }
    recordSize = GposValueSize(valueFormat1) + GposValueSize(valueFormat2);
    if (recordSize == 0u) {
        return 0;
    }
    record = base + 16u + ((size_t)class1 * (size_t)class2Count + (size_t)class2) * recordSize;
    if (!GposReadValue(apply, base, record, valueFormat1, &firstValue) ||
        !GposReadValue(apply, base, record + GposValueSize(valueFormat1), valueFormat2, &secondValue)) {
        apply->gposStats.subtablesMalformed++;
        return 0;
    }
    return GposApplyPairValues(apply, index, secondIndex, &firstValue, &secondValue, 1, class1, class2);
}

static int GposApplyPair(RoboTextOtCall *apply, size_t base, size_t index, size_t *outNext)
{
    FontSpan table = apply->layout->table;
    uint16_t format = 0u;
    uint16_t coverageOffset = 0u;
    uint16_t valueFormat1 = 0u;
    uint16_t valueFormat2 = 0u;
    size_t coverage;
    size_t secondIndex;
    int applied;

    if (!GposParticipates(apply, index)) {
        return 0;
    }
    /*
     * The pair's second glyph is the next glyph the lookup **sees**, not `index + 1`: a lookup that ignores
     * marks kerns `A` against the base after them, which is the whole reason the participant walk exists
     * (OpenType's `IgnoreMarks`, `IgnoreBaseGlyphs`, `IgnoreLigatures`). A buffer whose only remaining
     * glyphs are skipped has no pair here.
     */
    secondIndex = RoboTextOtNextParticipating(apply, index + 1u);
    if (secondIndex >= RoboTextShapingBufferCount(apply->buffer)) {
        return 0;
    }
    if (!GposU16(table, base, &format) || !GposU16(table, base + 2u, &coverageOffset) ||
        !GposU16(table, base + 4u, &valueFormat1) || !GposU16(table, base + 6u, &valueFormat2)) {
        return 0;
    }
    coverage = RoboTextOtOffset16(table, base, coverageOffset);
    if (coverage == 0u) {
        return 0;
    }
    if (!RoboTextOtSpendProbes(apply, 1u)) {
        return 0;
    }
    if (format == 1u) {
        applied = GposApplyPairFormat1(apply, base, index, secondIndex, coverage, valueFormat1, valueFormat2);
    } else if (format == 2u) {
        applied = GposApplyPairFormat2(apply, base, index, secondIndex, coverage, valueFormat1, valueFormat2);
    } else {
        applied = 0;
    }
    if (applied) {
        *outNext = index + 1u;
    }
    return applied;
}

/* ------------------------------------------------------------------ */
/* Lookup type 9 — extension                                           */
/* ------------------------------------------------------------------ */

static int GposApplyExtension(RoboTextOtCall *apply, size_t base, size_t index, size_t *outNext)
{
    FontSpan table = apply->layout->table;
    uint16_t format = 0u;
    uint16_t innerType = 0u;
    uint32_t innerOffset = 0u;
    size_t real;
    uint16_t outerType;
    int result;

    if (!GposU16(table, base, &format) || format != 1u) {
        return 0;
    }
    if (!GposU16(table, base + 2u, &innerType) || !GposU32(table, base + 4u, &innerOffset)) {
        return 0;
    }
    real = RoboTextOtOffset32(table, base, innerOffset);
    if (real == 0u) {
        return 0;
    }
    if (innerType == RoboTextGposLookupExtension) {
        /* A wrapper around a wrapper is malformed: OpenType's extension format exists to escape the 16-bit
         * offset space once, and following it twice would be following a font's mistake. */
        apply->gposStats.subtablesMalformed++;
        return 0;
    }
    if (apply->depth + 1u > apply->limits->maxNesting) {
        apply->limitReached = 1;
        return 0;
    }
    /*
     * The lookup type is swapped for the duration so that a trace reports the type that actually ran — the
     * inner one — rather than the wrapper every extension lookup would otherwise be recorded as.
     */
    outerType = apply->lookupType;
    apply->lookupType = innerType;
    apply->depth++;
    result = RoboTextGposApplySubtable(apply, innerType, real, index, outNext);
    apply->depth--;
    apply->lookupType = outerType;
    return result;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

int RoboTextGposApplySubtable(RoboTextOtCall *apply,
                              uint16_t lookupType,
                              size_t offset,
                              size_t index,
                              size_t *outNext)
{
    if (!apply || !apply->layout || !apply->buffer || !apply->limits || offset == 0u) {
        return 0;
    }
    if (index >= RoboTextShapingBufferCount(apply->buffer)) {
        return 0;
    }
    switch (lookupType) {
        case RoboTextGposLookupSingle:
            return GposApplySingle(apply, offset, index, outNext);
        case RoboTextGposLookupPair:
            return GposApplyPair(apply, offset, index, outNext);
        case RoboTextGposLookupExtension:
            return GposApplyExtension(apply, offset, index, outNext);
        default:
            /*
             * Types 3 to 8 reach the dispatch only if the caller skipped the engine's own "does this build run
             * it" question. Answering "no match" rather than reading them is what keeps an unimplemented
             * lookup from being interpreted as some other format.
             */
            return 0;
    }
}

/** Non-zero when this build executes lookup type @p type. Types 3–8 are counted, not read. */
int RoboTextGposTypeIsExecuted(uint16_t type)
{
    return (type == RoboTextGposLookupSingle || type == RoboTextGposLookupPair ||
            type == RoboTextGposLookupExtension)
               ? 1
               : 0;
}
