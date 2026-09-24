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
/* The anchor, and the quantity an attachment needs from outside GPOS  */
/* ------------------------------------------------------------------ */

/**
 * Read the anchor at @p offset, resolved against @p base, in device pixels.
 *
 * The three anchor formats differ in what they carry *besides* the two coordinates: format 2 names a contour
 * point (which this build does not use — the outline is not here and the coordinate is the answer either
 * way), and format 3 appends device offsets, evaluated against the caller's ppem exactly as a value
 * record's are. A format-3 anchor at an unknown size keeps its font-unit coordinates and counts the skipped
 * delta rather than applying half of it.
 *
 * The coordinates are font units and become pixels through the caller's scale, the module's one conversion.
 * A call with no scale has no answer: it returns 0 and counts the record, the same refusal a value record
 * gets.
 */
static int GposReadAnchor(RoboTextOtCall *apply,
                          size_t base,
                          uint16_t offset,
                          float *outX,
                          float *outY)
{
    FontSpan table;
    uint16_t format = 0u;
    int16_t x = 0;
    int16_t y = 0;
    int32_t dx = 0;
    int32_t dy = 0;

    if (offset == 0u || !outX || !outY) {
        /* A zero offset is OpenType's "no anchor" — a glyph that does not attach — and not a failure. */
        return 0;
    }
    if (apply->deviceScale == 0.0f) {
        apply->gposStats.recordsUnevaluated++;
        return 0;
    }
    table = apply->layout->table;
    base = RoboTextOtOffset16(table, base, offset);
    if (base == 0u) {
        return 0;
    }
    if (!GposU16(table, base, &format) || !GposI16(table, base + 2u, &x) ||
        !GposI16(table, base + 4u, &y)) {
        return 0;
    }
    if (format == 3u) {
        uint16_t xDevice = 0u;
        uint16_t yDevice = 0u;

        if (GposU16(table, base + 6u, &xDevice) && GposU16(table, base + 8u, &yDevice)) {
            if (xDevice != 0u) {
                if (GposDeviceAt(apply, base, xDevice, &dx)) {
                    apply->gposStats.deviceCorrections++;
                }
            }
            if (yDevice != 0u) {
                if (GposDeviceAt(apply, base, yDevice, &dy)) {
                    apply->gposStats.deviceCorrections++;
                }
            }
        }
    } else if (format != 1u && format != 2u) {
        return 0;
    }
    *outX = (float)(x + dx) * apply->deviceScale;
    *outY = (float)(y + dy) * apply->deviceScale;
    return 1;
}

/**
 * The pen's distance from buffer position @p index to the next glyph, in device pixels.
 *
 * The face's advance plus whatever positioning has already adjusted it by, and the reason @ref
 * RoboTextGposOptions carries a font context at all: `RoboTextFontGlyphAdvance` is asked here and nowhere
 * else in this engine. **1** is returned when nothing else can be — a glyph whose advance is unknown or
 * unavailable — and the caller treats that as "the attachments of this call cannot be placed", because a
 * distance invented for a chain would put glyphs somewhere no font asked for.
 */
static int GposPenDistance(RoboTextOtCall *apply, size_t index, float *outDistance)
{
    const RoboTextShapingGlyph *glyph;
    float advance = 0.0f;

    if (!apply->gposFont || !outDistance) {
        return 0;
    }
    glyph = RoboTextShapingBufferAt(apply->buffer, index);
    if (!glyph) {
        return 0;
    }
    /*
     * A `RoboFontGlyphMetricsUnavailable` answer is not an error and is still not a distance: a face with no
     * metrics table gives every glyph zero, and attaching a chain to zeroes would stack a word on one point.
     * So the status is read, not just the `Result`.
     */
    {
        FontGlyphMetricsStatus status = RoboFontGlyphMetricsUnavailable;

        if (RoboTextFontGlyphAdvance(apply->gposFont, glyph->glyphId, &advance, &status, NULL) != RoboOk ||
            status != RoboFontGlyphMetricsOk) {
            return 0;
        }
    }
    *outDistance = advance + glyph->advanceAdjustX;
    return 1;
}

/** Write an **absolute** placement onto @p index, the form every attachment states its answer in. */
static void GposSetPlacement(RoboTextOtCall *apply, size_t index, float x, float y)
{
    RoboTextShapingGlyph *glyph = RoboTextShapingBufferAtMutable(apply->buffer, index);

    if (!glyph) {
        return;
    }
    if ((glyph->flags & RoboTextShapingGlyphPositioned) == 0u) {
        apply->gposStats.adjustedGlyphs++;
    }
    glyph->offsetAdjustX = x;
    glyph->offsetAdjustY = y;
    glyph->flags |= RoboTextShapingGlyphPositioned;
}

/** The current placement of @p index, or zeroes. */
static void GposPlacementOf(RoboTextOtCall *apply, size_t index, float *outX, float *outY)
{
    const RoboTextShapingGlyph *glyph = RoboTextShapingBufferAt(apply->buffer, index);

    *outX = glyph ? glyph->offsetAdjustX : 0.0f;
    *outY = glyph ? glyph->offsetAdjustY : 0.0f;
}

/* ------------------------------------------------------------------ */
/* Cursive attachment (type 3)                                         */
/* ------------------------------------------------------------------ */

/**
 * Cursive attachment: two anchors that must coincide — the exit of one glyph and the entry of the next.
 *
 * The rule is the format's own, written as two statements about the pen and the placement (which is exactly
 * how OpenType describes a cursive chain):
 *
 *   the pen after the first glyph lands **on the first glyph's exit anchor**;
 *   the second glyph's placement puts its **entry anchor** there.
 *
 * The first is an advance and the second is a placement, and the reason this lookup needs the face's own
 * advance is the first statement: "the pen lands there" is a *distance*, and GPOS carries none.
 *
 * The direction picks which anchor is the outgoing one — left-to-right text leaves by the right-hand (exit)
 * anchor and right-to-left text leaves by the entry one — which is the format's `RightToLeft` convention and
 * not a heuristic on the script.
 */
static int GposApplyCursive(RoboTextOtCall *apply, size_t base, size_t index, size_t *outNext)
{
    FontSpan table = apply->layout->table;
    uint16_t format = 0u;
    uint16_t coverageField = 0u;
    uint16_t count = 0u;
    uint16_t firstEntry = 0u;
    uint16_t firstExit = 0u;
    uint16_t secondEntry = 0u;
    uint16_t secondExit = 0u;
    uint16_t coverageIndex = 0u;
    size_t coverage = 0u;
    size_t record;
    size_t second;
    float outX = 0.0f;
    float outY = 0.0f;
    float inX = 0.0f;
    float inY = 0.0f;
    float penX = 0.0f;
    const RoboTextShapingGlyph *firstGlyph;
    RoboTextShapingGlyph *mutableFirst;
    int rightToLeft = RoboTextDirectionIsRightToLeft(apply->direction);

    if (outNext) {
        *outNext = index;
    }
    if (!apply->gposFont) {
        /* The apply loop refuses a font-less attachment lookup **once per lookup**, before the scan; this is
         * the belt-and-braces answer for a caller that reached the reader anyway, and it is deliberately not
         * counted — a counter that follows the text is a counter about the text. */
        return 0;
    }
    firstGlyph = RoboTextShapingBufferAt(apply->buffer, index);
    if (!firstGlyph) {
        return 0;
    }
    if (!GposU16(table, base, &format) || format != 1u) {
        return 0;
    }
    if (!GposU16(table, base + 2u, &coverageField) || !GposU16(table, base + 4u, &count)) {
        return 0;
    }
    if (count == 0u) {
        return 0;
    }
    coverage = RoboTextOtOffset16(table, base, coverageField);
    if (!RoboTextOtCovered(apply, coverage, firstGlyph->glyphId, &coverageIndex) ||
        coverageIndex >= count) {
        return 0;
    }
    record = base + 6u + (size_t)coverageIndex * 4u;
    if (!GposU16(table, record, &firstEntry) || !GposU16(table, record + 2u, &firstExit)) {
        return 0;
    }
    /*
     * The **next** participating glyph, and `index + 1u` rather than `index`: the walk answers "the first
     * participating glyph at or after here", so asking it at the first glyph answers the first glyph and a
     * chain would attach a glyph to itself — which the fixture caught as two identical corrections on a
     * two-letter word. It is the same off-by-one `RoboTextOtPrevParticipating`'s comment names, one direction
     * over, and the pair reader next door states it as `index + 1u` for the same reason.
     */
    second = RoboTextOtNextParticipating(apply, index + 1u);
    if (second >= RoboTextShapingBufferCount(apply->buffer)) {
        return 0;
    }
    {
        const RoboTextShapingGlyph *secondGlyph = RoboTextShapingBufferAt(apply->buffer, second);

        if (!secondGlyph ||
            !RoboTextOtCovered(apply, coverage, secondGlyph->glyphId, &coverageIndex) ||
            coverageIndex >= count) {
            return 0;
        }
    }
    record = base + 6u + (size_t)coverageIndex * 4u;
    if (!GposU16(table, record, &secondEntry) || !GposU16(table, record + 2u, &secondExit)) {
        return 0;
    }
    /*
     * The two anchors, and which of the pair each belongs to: left-to-right text hands over at the first
     * glyph's exit to the second's entry, and right-to-left text at the first's entry to the second's exit.
     * That is the format's `RightToLeft` convention rather than a heuristic on the script.
     */
    if (!GposReadAnchor(apply, base, rightToLeft ? firstEntry : firstExit, &outX, &outY)) {
        return 0;
    }
    if (!GposReadAnchor(apply, base, rightToLeft ? secondExit : secondEntry, &inX, &inY)) {
        return 0;
    }
    if (!GposPenDistance(apply, index, &penX)) {
        return 0;
    }
    /*
     * Two statements, which is the whole of a cursive chain: the pen after the first glyph lands on the
     * first glyph's outgoing anchor, and the second glyph's placement puts its incoming anchor where that
     * pen is. The first is why the face's own advance is needed — a distance GPOS does not carry — and the
     * second is why the placement is set rather than added: an attachment is an *answer*, not a nudge.
     */
    mutableFirst = RoboTextShapingBufferAtMutable(apply->buffer, index);
    if (!mutableFirst) {
        return 0;
    }
    if ((mutableFirst->flags & RoboTextShapingGlyphPositioned) == 0u) {
        apply->gposStats.adjustedGlyphs++;
    }
    mutableFirst->flags |= RoboTextShapingGlyphPositioned;
    mutableFirst->advanceAdjustX += outX - penX;
    GposSetPlacement(apply, second, -inX, outY - inY);
    if (outNext) {
        *outNext = second + 1u;
    }
    apply->gposStats.cursiveAttachments++;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The mark attachments (types 4, 5 and 6)                             */
/* ------------------------------------------------------------------ */

/**
 * A mark attached to the glyph it belongs to, by mark class.
 *
 * One reader for the three types, because the three differ in exactly one place — what the *base* array
 * describes — and everything else is the same question: a mark's class and anchor from the MarkArray, a base
 * glyph found by walking back to the nearest participating glyph, the base's anchor for that class, and a
 * placement that puts the two anchors on top of one another.
 *
 *   **4** the base array is indexed by the base coverage's own index, one anchor per class;
 *   **6** the same against the preceding **mark** (`mkmk`), which is what lets two diacritics stack;
 *   **5** the base is a ligature and its array names a component per class, and the component this build
 *         reads is the **last** one — see the header's note on @ref RoboTextShapingGlyph::ligatureComponents
 *         and the limitation named in `Documentation/Runtime/Text/Positioning.md`.
 *
 * The placement is absolute: a mark's position relative to its base *is* the font's answer, so a second
 * attachment of the same mark states the second answer rather than summing the two.
 */
static int GposApplyMark(RoboTextOtCall *apply, size_t base, size_t index, size_t *outNext, uint16_t type)
{
    FontSpan table = apply->layout->table;
    uint16_t format = 0u;
    uint16_t markCoverage = 0u;
    uint16_t baseCoverage = 0u;
    uint16_t classCount = 0u;
    uint16_t markArray = 0u;
    uint16_t baseArray = 0u;
    uint16_t markIndex = 0u;
    uint16_t markClass = 0u;
    uint16_t markAnchorOffset = 0u;
    uint16_t baseIndex = 0u;
    uint16_t anchorOffset = 0u;
    const RoboTextShapingGlyph *markGlyph;
    const RoboTextShapingGlyph *baseGlyph = NULL;
    size_t basePosition = (size_t)-1;
    size_t markArrayAbsolute;
    size_t baseArrayAbsolute;
    float markX = 0.0f;
    float markY = 0.0f;
    float baseAnchorX = 0.0f;
    float baseAnchorY = 0.0f;
    float basePlacementX = 0.0f;
    float basePlacementY = 0.0f;
    float distance = 0.0f;
    size_t component = 0u;

    if (outNext) {
        *outNext = index;
    }
    if (!apply->gposFont) {
        return 0;
    }
    markGlyph = RoboTextShapingBufferAt(apply->buffer, index);
    if (!markGlyph) {
        return 0;
    }
    if (!GposU16(table, base, &format) || format != 1u) {
        return 0;
    }
    if (!GposU16(table, base + 2u, &markCoverage) || !GposU16(table, base + 4u, &baseCoverage) ||
        !GposU16(table, base + 6u, &classCount) || !GposU16(table, base + 8u, &markArray) ||
        !GposU16(table, base + 10u, &baseArray)) {
        return 0;
    }
    if (classCount == 0u || markArray == 0u || baseArray == 0u) {
        return 0;
    }
    if (!RoboTextOtCovered(apply, RoboTextOtOffset16(table, base, markCoverage), markGlyph->glyphId,
                           &markIndex)) {
        return 0;
    }
    markArrayAbsolute = RoboTextOtOffset16(table, base, markArray);
    if (markArrayAbsolute == 0u) {
        return 0;
    }
    /* MarkArray: a count, then { class, anchor offset } per covered mark. */
    {
        uint16_t markCount = 0u;

        if (!GposU16(table, markArrayAbsolute, &markCount) || markIndex >= markCount) {
            return 0;
        }
        if (!GposU16(table, markArrayAbsolute + 2u + (size_t)markIndex * 4u, &markClass) ||
            !GposU16(table, markArrayAbsolute + 4u + (size_t)markIndex * 4u, &markAnchorOffset)) {
            return 0;
        }
        if (markClass >= classCount) {
            return 0;
        }
        if (!GposReadAnchor(apply, markArrayAbsolute, markAnchorOffset, &markX, &markY)) {
            return 0;
        }
    }
    /*
     * The base: the nearest glyph **before** the mark that the base coverage names.
     *
     * One walk serves all three types, because the types differ only in what the coverage contains: a
     * mark-to-base lookup's coverage names bases, so walking back past the intervening marks lands on the
     * base; a mark-to-mark lookup's names marks, so the first covered glyph back is the previous mark — which
     * is exactly what stacking two diacritics needs. The walk is the participant walk the pair readers use,
     * so an ignored glyph is stepped over here too. Bounded, because a font may name a base far away and a
     * mark is not a reason to scan a paragraph.
     */
    for (size_t steps = 1u; steps <= 32u; steps++) {
        size_t candidate = RoboTextOtPrevParticipating(apply, index, steps);
        const RoboTextShapingGlyph *glyph;

        if (candidate >= RoboTextShapingBufferCount(apply->buffer)) {
            break;
        }
        glyph = RoboTextShapingBufferAt(apply->buffer, candidate);
        if (glyph && RoboTextOtCovered(apply, RoboTextOtOffset16(table, base, baseCoverage),
                                       glyph->glyphId, &baseIndex)) {
            baseGlyph = glyph;
            basePosition = candidate;
            break;
        }
    }
    if (!baseGlyph || basePosition == (size_t)-1) {
        return 0;
    }
    baseArrayAbsolute = RoboTextOtOffset16(table, base, baseArray);
    if (baseArrayAbsolute == 0u) {
        return 0;
    }
    if (type == RoboTextGposLookupMarkToLigature) {
        /*
         * LigatureArray: a count, then one offset per covered ligature to a LigatureAttach, whose own first
         * word is its component count and whose records are componentCount × classCount anchor offsets.
         *
         * The `+ 2u` is the **count word**: every one of these three arrays (`BaseArray`, `Mark2Array`,
         * `LigatureArray`) begins with a `uint16` count whose records then follow, and addressing the records
         * from the array's first byte instead of from after that word is how a font's second anchor comes to
         * be read as its first.
         */
        uint16_t ligatureOffset = 0u;
        size_t attach;

        if (!GposU16(table, baseArrayAbsolute + 2u + (size_t)baseIndex * 2u, &ligatureOffset)) {
            return 0;
        }
        attach = RoboTextOtOffset16(table, baseArrayAbsolute, ligatureOffset);
        if (attach == 0u) {
            return 0;
        }
        {
            uint16_t components = 0u;

            if (!GposU16(table, attach, &components) || components == 0u) {
                return 0;
            }
            component = (size_t)components - 1u;
        }
        if (!GposU16(table, attach + 2u + (component * classCount + markClass) * 2u, &anchorOffset)) {
            return 0;
        }
        if (!GposReadAnchor(apply, attach, anchorOffset, &baseAnchorX, &baseAnchorY)) {
            return 0;
        }
    } else {
        /* BaseArray (type 4) or Mark2Array (type 6): one count word, then `classCount` anchor offsets per
         * covered glyph. See the mark-to-ligature branch for why the count word is skipped rather than read. */
        if (!GposU16(table, baseArrayAbsolute + 2u + ((size_t)baseIndex * classCount + markClass) * 2u,
                     &anchorOffset)) {
            return 0;
        }
        if (!GposReadAnchor(apply, baseArrayAbsolute, anchorOffset, &baseAnchorX, &baseAnchorY)) {
            return 0;
        }
    }
    /*
     * The pen's distance from the base to the mark: every glyph between them advances the pen, and a mark
     * between them advances it by zero, which is why a second mark stacks on the first without arithmetic of
     * its own. The base's own placement is added because it is where the base was actually drawn.
     */
    for (size_t walk = basePosition; walk < index; walk++) {
        float step = 0.0f;

        if (!GposPenDistance(apply, walk, &step)) {
            return 0;
        }
        distance += step;
    }
    GposPlacementOf(apply, basePosition, &basePlacementX, &basePlacementY);
    GposSetPlacement(apply, index, (basePlacementX + baseAnchorX) - (distance + markX),
                     (basePlacementY + baseAnchorY) - markY);
    switch (type) {
        case RoboTextGposLookupMarkToBase:
            apply->gposStats.markToBaseAttachments++;
            break;
        case RoboTextGposLookupMarkToLigature:
            apply->gposStats.markToLigatureAttachments++;
            break;
        default:
            apply->gposStats.markToMarkAttachments++;
            break;
    }
    (void)baseGlyph;
    return 1;
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
        case RoboTextGposLookupCursive:
            return GposApplyCursive(apply, offset, index, outNext);
        case RoboTextGposLookupMarkToBase:
        case RoboTextGposLookupMarkToLigature:
        case RoboTextGposLookupMarkToMark:
            return GposApplyMark(apply, offset, index, outNext, lookupType);
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

/**
 * Non-zero when this build executes lookup type @p type.
 *
 * Types 1, 2, 3, 4, 5, 6 and 9 since Phase 12.19; 7 and 8 (the contextual forms) are recognised and not run,
 * each counted in @ref RoboTextGposStats::lookupsUnsupported so a caller can see what a face wanted and did
 * not get. A type this build does not run leaves the glyph where its advance put it rather than reading bytes
 * as a format they are not.
 *
 * A cursive or mark attachment also needs a **font context** (@ref RoboTextGposOptions::font): without one
 * its answer depends on a distance GPOS does not carry, so it is counted unsupported too. The list here is
 * about the table; the call's own refusal is in the reader.
 */
int RoboTextGposTypeIsExecuted(uint16_t type)
{
    switch (type) {
        case RoboTextGposLookupSingle:
        case RoboTextGposLookupPair:
        case RoboTextGposLookupCursive:
        case RoboTextGposLookupMarkToBase:
        case RoboTextGposLookupMarkToLigature:
        case RoboTextGposLookupMarkToMark:
        case RoboTextGposLookupExtension:
            return 1;
        default:
            return 0;
    }
}
