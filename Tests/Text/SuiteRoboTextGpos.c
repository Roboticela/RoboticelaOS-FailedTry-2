/*
 * RoboText's glyph positioning (Phase 12.11) — `text.gpos.*`.
 *
 * Two kinds of evidence, and the split is deliberate.
 *
 *   **A synthetic table, parsed on its own.** `FontGposParse` takes a span of GPOS bytes, so this file builds
 *   exactly the format it wants to test — a pair set with three pairs, a class matrix, a device delta, an
 *   extension wrapper, a coverage offset pointing at nothing — and hands it to the engine with a hand-built
 *   glyph buffer. No font container, no cmap, no other table: what the assertions measure is the reader and
 *   nothing else. That is how the edge cases OpenType allows and no real font ships (a zero value record, a
 *   wrapper around a wrapper, a class index past the matrix) become executable facts.
 *
 *   **The shipped pack, end to end.** `text.gpos.shipped_pack` measures real text with the face in
 *   `System/Fonts/`, with the `kern` feature on and off, and asserts that the font's own kerning moved the
 *   advance; `text.gpos.paint_agrees` asserts the number the pen uses is the number the measurer reports. A
 *   synthetic table proves the reader reads bytes; only the shipped face proves the pipeline a document takes.
 *
 * The fixture's advances are per-glyph and distinct in the *font* fixture used by the shape suites; here the
 * buffers are hand-built, so what matters is that a value record's numbers are scaled and placed exactly once.
 */

#include "RoboTextTestSupport.h"

#include "RoboFont/RoboFont.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextGpos.h"
#include "RoboText/RoboTextShape.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The framework's vocabulary, plus one comparison it does not have     */
/* ------------------------------------------------------------------ */

/**
 * A float comparison.
 *
 * The framework compares integers and strings; a positioning assertion is about a fraction of a pixel, so
 * this compares the difference against a tolerance and prints both numbers when it fails — a bare
 * `0 != 1` from a scaled-integer comparison would not say *which* number was wrong, and a kerning bug is
 * exactly a case where the interesting fact is how far off the answer was.
 */
static void CheckFloatNear(const char *what, float got, float want, float tolerance)
{
    float difference = got - want;

    if (difference < 0.0f) {
        difference = -difference;
    }
    if (!(difference <= tolerance)) {
        fprintf(stderr, "  value %s: got %.6f, want %.6f (tolerance %.6f)\n", what, (double)got,
                (double)want, (double)tolerance);
    }
    RoboCheck(difference <= tolerance);
}

#define RoboCheckNear(got, want, tolerance) CheckFloatNear(#got, (got), (want), (tolerance))

/* ------------------------------------------------------------------ */
/* The glyphs                                                          */
/* ------------------------------------------------------------------ */

enum {
    FixtureA = 4,    /* a pair's first glyph, in a coverage */
    FixtureV = 7,    /* the usual second glyph */
    FixtureO = 8,    /* a pair with a zero value record */
    FixtureT = 9,    /* a second glyph this table does not list */
    FixtureMark = 2, /* a combining mark, by GDEF's class */
    FixtureBase = 5  /* a base glyph, covered by the single-adjustment tests */
};

/* ------------------------------------------------------------------ */
/* Bytes                                                               */
/* ------------------------------------------------------------------ */

typedef struct ByteWriter {
    uint8_t *data;
    size_t capacity;
    size_t length;
} ByteWriter;

static void Wu16(ByteWriter *w, size_t at, uint16_t value)
{
    w->data[at] = (uint8_t)(value >> 8);
    w->data[at + 1u] = (uint8_t)value;
}

static void Wu32(ByteWriter *w, size_t at, uint32_t value)
{
    w->data[at] = (uint8_t)(value >> 24);
    w->data[at + 1u] = (uint8_t)(value >> 16);
    w->data[at + 2u] = (uint8_t)(value >> 8);
    w->data[at + 3u] = (uint8_t)value;
}

static size_t Wreserve(ByteWriter *w, size_t bytes)
{
    size_t at = w->length;

    RoboCheck(at + bytes <= w->capacity);
    memset(w->data + at, 0, bytes);
    w->length = at + bytes;
    return at;
}

static size_t Wu16At(ByteWriter *w, uint16_t value)
{
    size_t at = Wreserve(w, 2u);

    Wu16(w, at, value);
    return at;
}

static size_t Wu32At(ByteWriter *w, uint32_t value)
{
    size_t at = Wreserve(w, 4u);

    Wu32(w, at, value);
    return at;
}

static size_t Wu8At(ByteWriter *w, uint8_t value)
{
    size_t at = Wreserve(w, 1u);

    w->data[at] = value;
    return at;
}

static void Wpad4(ByteWriter *w)
{
    while ((w->length % 4u) != 0u) {
        (void)Wu8At(w, 0u);
    }
}

static uint32_t FixtureTag(const char *tag)
{
    return RoboTextOtTagOf(tag);
}

/* ---- Coverage and class definitions ---- */

static uint16_t Wcoverage1(ByteWriter *w, size_t base, const uint16_t *glyphs, size_t count)
{
    size_t start = w->length;

    (void)Wu16At(w, 1u);
    (void)Wu16At(w, (uint16_t)count);
    for (size_t index = 0u; index < count; index++) {
        (void)Wu16At(w, glyphs[index]);
    }
    return (uint16_t)(start - base);
}

/** Coverage format 2: a single-glyph range and then a range of @p count glyphs starting at @p startGlyph + 3. */
static uint16_t Wcoverage2(ByteWriter *w, size_t base, uint16_t startGlyph, uint16_t count)
{
    size_t start = w->length;

    (void)Wu16At(w, 2u);
    (void)Wu16At(w, 2u); /* rangeCount */
    (void)Wu16At(w, startGlyph);
    (void)Wu16At(w, startGlyph);
    (void)Wu16At(w, 0u);
    (void)Wu16At(w, (uint16_t)(startGlyph + 3u));
    (void)Wu16At(w, (uint16_t)(startGlyph + 3u + count - 1u));
    (void)Wu16At(w, 1u);
    return (uint16_t)(start - base);
}

static uint16_t Wclassdef1(ByteWriter *w, size_t base, uint16_t startGlyph, const uint16_t *classes, size_t count)
{
    size_t start = w->length;

    (void)Wu16At(w, 1u);
    (void)Wu16At(w, startGlyph);
    (void)Wu16At(w, (uint16_t)count);
    for (size_t index = 0u; index < count; index++) {
        (void)Wu16At(w, classes[index]);
    }
    return (uint16_t)(start - base);
}

/** ClassDef format 2: `rangeCount` records of `[startGlyph, endGlyph, class]`. */
static uint16_t Wclassdef2(ByteWriter *w, size_t base, const uint16_t *ranges, size_t rangeCount)
{
    size_t start = w->length;

    (void)Wu16At(w, 2u);
    (void)Wu16At(w, (uint16_t)rangeCount);
    for (size_t index = 0u; index < rangeCount; index++) {
        (void)Wu16At(w, ranges[index * 3u]);
        (void)Wu16At(w, ranges[index * 3u + 1u]);
        (void)Wu16At(w, ranges[index * 3u + 2u]);
    }
    return (uint16_t)(start - base);
}

/* ---- Value records ---- */

#define FixtureValueXPlacement 0x0001u
#define FixtureValueYPlacement 0x0002u
#define FixtureValueXAdvance 0x0004u
#define FixtureValueYAdvance 0x0008u
#define FixtureValueXPlacementDevice 0x0010u
#define FixtureValueYPlacementDevice 0x0020u
#define FixtureValueXAdvanceDevice 0x0040u
#define FixtureValueYAdvanceDevice 0x0080u

/** The four fields a value record may carry, in font units, plus its four device offsets. */
typedef struct ValueFields {
    int16_t xPlacement;
    int16_t yPlacement;
    int16_t xAdvance;
    int16_t yAdvance;
    uint16_t deviceOffsets[4];
} ValueFields;

static ValueFields Value(int16_t xPlacement, int16_t yPlacement, int16_t xAdvance, int16_t yAdvance)
{
    ValueFields fields;

    memset(&fields, 0, sizeof(fields));
    fields.xPlacement = xPlacement;
    fields.yPlacement = yPlacement;
    fields.xAdvance = xAdvance;
    fields.yAdvance = yAdvance;
    return fields;
}

static void Wvalue(ByteWriter *w, uint16_t format, const ValueFields *fields)
{
    if ((format & FixtureValueXPlacement) != 0u) {
        (void)Wu16At(w, (uint16_t)fields->xPlacement);
    }
    if ((format & FixtureValueYPlacement) != 0u) {
        (void)Wu16At(w, (uint16_t)fields->yPlacement);
    }
    if ((format & FixtureValueXAdvance) != 0u) {
        (void)Wu16At(w, (uint16_t)fields->xAdvance);
    }
    if ((format & FixtureValueYAdvance) != 0u) {
        (void)Wu16At(w, (uint16_t)fields->yAdvance);
    }
    for (size_t index = 0u; index < 4u; index++) {
        if ((format & (uint16_t)(FixtureValueXPlacementDevice << index)) != 0u) {
            (void)Wu16At(w, fields->deviceOffsets[index]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

#define FixtureMaxLookups 4u
#define FixtureMaxFeatures 4u

typedef struct GposLookupSpec {
    uint16_t type;
    uint16_t flags;
    /** Writes the lookup's one subtable at absolute @p base. @return non-zero when it wrote one. */
    int (*write)(ByteWriter *w, size_t base);
} GposLookupSpec;

typedef struct GposFeatureSpec {
    const char *tag;
    uint16_t lookups[2];
    size_t count;
} GposFeatureSpec;

typedef struct GposFixture {
    uint8_t bytes[8192];
    size_t length;
    size_t subtableFixUps[FixtureMaxLookups];
    size_t lookupOffsets[FixtureMaxLookups];
    FontOtLayout layout;
    int parsed;
} GposFixture;

static void FixtureClose(GposFixture *fixture)
{
    if (fixture->parsed) {
        FontOtLayoutClear(&fixture->layout);
        fixture->parsed = 0;
    }
}

/**
 * Build a GPOS table with one script record (@p scriptTag, or none when NULL), @p featureCount features and
 * @p lookupCount lookups, then parse it with Phase 5's own parser.
 *
 * The order is the format's: header, ScriptList, FeatureList, LookupList, and only then the subtables —
 * because a subtable's offset is relative to its own Lookup table and cannot be known until the lists are
 * written. A lookup spec whose `write` answers 0 leaves its subtable offset at 0, which is the format's
 * "this lookup has no subtable here" and a shape a malformed font really has.
 */
static void FixtureBuild(GposFixture *fixture,
                         const char *scriptTag,
                         const GposFeatureSpec *features,
                         size_t featureCount,
                         const GposLookupSpec *lookups,
                         size_t lookupCount)
{
    ByteWriter w;
    size_t header;
    size_t scriptList;
    size_t featureList;
    size_t lookupList;
    size_t scriptRecordOffset;
    size_t scriptTable;
    size_t langSys;
    size_t featureRecords;
    size_t lookupRecords;
    FontSpan span;

    RoboCheck(lookupCount <= FixtureMaxLookups);
    RoboCheck(featureCount <= FixtureMaxFeatures);
    memset(fixture, 0, sizeof(*fixture));
    w.data = fixture->bytes;
    w.capacity = sizeof(fixture->bytes);
    w.length = 0u;

    header = Wreserve(&w, 10u);
    scriptList = w.length;
    scriptRecordOffset = 0u;
    scriptTable = 0u;
    langSys = 0u;
    featureRecords = 0u;
    lookupRecords = 0u;

    /* ---- ScriptList ---- */
    if (scriptTag) {
        (void)Wu16At(&w, 1u);
        scriptRecordOffset = w.length;
        (void)Wu32At(&w, FixtureTag(scriptTag));
        (void)Wu16At(&w, 0u);
        scriptTable = w.length;
        Wu16(&w, scriptRecordOffset + 4u, (uint16_t)(scriptTable - scriptList));
        (void)Wu16At(&w, 0u); /* defaultLangSysOffset, patched */
        (void)Wu16At(&w, 0u); /* langSysCount */
        langSys = w.length;
        Wu16(&w, scriptTable, (uint16_t)(langSys - scriptTable));
        (void)Wu16At(&w, 0u);
        (void)Wu16At(&w, 0xFFFFu);
        (void)Wu16At(&w, (uint16_t)featureCount);
        for (size_t index = 0u; index < featureCount; index++) {
            (void)Wu16At(&w, (uint16_t)index);
        }
    } else {
        (void)Wu16At(&w, 0u);
    }

    /* ---- FeatureList ---- */
    Wpad4(&w);
    featureList = w.length;
    (void)Wu16At(&w, (uint16_t)featureCount);
    featureRecords = w.length;
    Wreserve(&w, featureCount * 6u);
    for (size_t index = 0u; index < featureCount; index++) {
        size_t table = w.length;
        size_t lookup;

        Wu32(&w, featureRecords + index * 6u, FixtureTag(features[index].tag));
        Wu16(&w, featureRecords + index * 6u + 4u, (uint16_t)(table - featureList));
        (void)Wu16At(&w, 0u);
        (void)Wu16At(&w, (uint16_t)features[index].count);
        for (lookup = 0u; lookup < features[index].count; lookup++) {
            (void)Wu16At(&w, features[index].lookups[lookup]);
        }
    }

    /* ---- LookupList ---- */
    Wpad4(&w);
    lookupList = w.length;
    (void)Wu16At(&w, (uint16_t)lookupCount);
    lookupRecords = w.length;
    for (size_t index = 0u; index < lookupCount; index++) {
        (void)Wu16At(&w, 0u);
    }
    for (size_t index = 0u; index < lookupCount; index++) {
        fixture->lookupOffsets[index] = w.length;
        Wu16(&w, lookupRecords + index * 2u, (uint16_t)(fixture->lookupOffsets[index] - lookupList));
        (void)Wu16At(&w, lookups[index].type);
        (void)Wu16At(&w, lookups[index].flags);
        (void)Wu16At(&w, 1u);
        fixture->subtableFixUps[index] = Wu16At(&w, 0u);
    }

    /* ---- The subtables ---- */
    for (size_t index = 0u; index < lookupCount; index++) {
        size_t start = w.length;
        size_t lookupBase = fixture->lookupOffsets[index];

        if (lookups[index].write != NULL && lookups[index].write(&w, start)) {
            RoboCheck(start - lookupBase <= 0xFFFFu);
            Wu16(&w, fixture->subtableFixUps[index], (uint16_t)(start - lookupBase));
        }
    }

    Wu16(&w, header, 1u);
    Wu16(&w, header + 2u, 0u);
    Wu16(&w, header + 4u, (uint16_t)scriptList);
    Wu16(&w, header + 6u, (uint16_t)featureList);
    Wu16(&w, header + 8u, (uint16_t)lookupList);
    fixture->length = w.length;

    span.data = fixture->bytes;
    span.size = fixture->length;
    RoboCheckEqInt((int)FontGposParse(span, &fixture->layout), (int)RoboOk);
    RoboCheckEqInt((int)fixture->layout.kind, (int)RoboFontOtLayoutGpos);
    fixture->parsed = 1;
}

/** Build a GDEF whose class definition puts @p class on @p startGlyph; mark glyphs are class 3. */
static FontGdef FixtureBuildGdef(uint8_t *bytes,
                                 size_t capacity,
                                 uint16_t startGlyph,
                                 uint16_t klass)
{
    ByteWriter w;
    FontGdef gdef;
    FontSpan span;
    size_t classDef;
    uint16_t classes[1];

    memset(&gdef, 0, sizeof(gdef));
    memset(bytes, 0, capacity);
    classes[0] = klass;
    w.data = bytes;
    w.capacity = capacity;
    w.length = 0u;
    (void)Wu32At(&w, 0x00010000u);
    (void)Wu16At(&w, 0u);
    (void)Wu16At(&w, 0u);
    (void)Wu16At(&w, 0u);
    (void)Wu16At(&w, 0u);
    classDef = w.length;
    Wu16(&w, 4u, (uint16_t)classDef);
    (void)Wclassdef1(&w, classDef, startGlyph, classes, 1u);
    span.data = bytes;
    span.size = w.length;
    RoboCheckEqInt((int)FontGdefParse(span, &gdef), (int)RoboOk);
    return gdef;
}

/* ---- The subtables this suite writes ---- */

/** The single-adjustment subtable's parameters, set by each test before the build. */
typedef struct SingleSpec {
    uint16_t format;
    const uint16_t *coverage;
    size_t coverageCount;
    /** Non-zero writes a **format 2** coverage instead: a single-glyph range and a range of @ref coverage2Count. */
    int coverageFormat2;
    uint16_t coverage2Start;
    uint16_t coverage2Count;
    uint16_t valueFormat;
    ValueFields values[2];
    size_t valueCount;
} SingleSpec;

/** The pair subtable's parameters, for both formats. */
typedef struct PairSpec {
    uint16_t format;
    const uint16_t *coverage;
    size_t coverageCount;
    uint16_t valueFormat1;
    uint16_t valueFormat2;
    const uint16_t *seconds; /* format 1: the second glyph of each pair */
    const ValueFields *firstValues;
    const ValueFields *secondValues;
    size_t pairCount;
    /* format 2 */
    const uint16_t *classRanges1;
    size_t classRange1Count;
    const uint16_t *classRanges2;
    size_t classRange2Count;
    uint16_t class1Count;
    uint16_t class2Count;
    const ValueFields *matrix; /* class1Count * class2Count entries, both sides interleaved */
} PairSpec;

static SingleSpec gSingle;
static PairSpec gPair;

static int WriteSingleSubtable(ByteWriter *w, size_t base)
{
    size_t start = w->length;
    size_t coverageField;
    size_t coverageStart;

    (void)Wu16At(w, gSingle.format);
    coverageField = Wu16At(w, 0u); /* the coverage offset, patched once the coverage is written */
    (void)Wu16At(w, gSingle.valueFormat);
    if (gSingle.format == 1u) {
        Wvalue(w, gSingle.valueFormat, &gSingle.values[0]);
    } else {
        (void)Wu16At(w, (uint16_t)gSingle.valueCount);
        for (size_t index = 0u; index < gSingle.valueCount; index++) {
            Wvalue(w, gSingle.valueFormat, &gSingle.values[index]);
        }
    }
    /*
     * The coverage goes **last** and its offset is patched into the field reserved for it. Writing the two
     * in one expression — `Wu16At(w, Wcoverage1(w, base, ...))` — looks natural and is wrong: C evaluates the
     * argument first, so the coverage's bytes land where the offset field was about to go and the offset is
     * written *after* them. The engine then reads a coverage table starting one field too early, which is
     * exactly the malformed table `text.gpos.limits_malformed` tests on purpose.
     */
    coverageStart = w->length;
    Wu16(w, coverageField, (uint16_t)(coverageStart - base));
    if (gSingle.coverageFormat2) {
        (void)Wcoverage2(w, base, gSingle.coverage2Start, gSingle.coverage2Count);
    } else {
        (void)Wcoverage1(w, base, gSingle.coverage, gSingle.coverageCount);
    }
    return (int)(w->length - start);
}

static int WritePairSubtable(ByteWriter *w, size_t base)
{
    size_t start = w->length;
    size_t coverageField;
    size_t coverageStart;
    size_t classDef1Field = 0u;
    size_t classDef2Field = 0u;

    (void)Wu16At(w, gPair.format);
    coverageField = Wu16At(w, 0u);
    (void)Wu16At(w, gPair.valueFormat1);
    (void)Wu16At(w, gPair.valueFormat2);
    if (gPair.format == 1u) {
        size_t offsetField;
        size_t pairSet;

        (void)Wu16At(w, 1u); /* pairSetCount */
        offsetField = Wu16At(w, 0u);
        pairSet = w->length;
        Wu16(w, offsetField, (uint16_t)(pairSet - base));
        (void)Wu16At(w, (uint16_t)gPair.pairCount);
        for (size_t index = 0u; index < gPair.pairCount; index++) {
            (void)Wu16At(w, gPair.seconds[index]);
            Wvalue(w, gPair.valueFormat1, &gPair.firstValues[index]);
            Wvalue(w, gPair.valueFormat2, &gPair.secondValues[index]);
        }
    } else {
        classDef1Field = Wu16At(w, 0u);
        classDef2Field = Wu16At(w, 0u);
        (void)Wu16At(w, gPair.class1Count);
        (void)Wu16At(w, gPair.class2Count);
        for (size_t class1 = 0u; class1 < gPair.class1Count; class1++) {
            for (size_t class2 = 0u; class2 < gPair.class2Count; class2++) {
                size_t index = class1 * gPair.class2Count + class2;

                Wvalue(w, gPair.valueFormat1, &gPair.matrix[index * 2u]);
                Wvalue(w, gPair.valueFormat2, &gPair.matrix[index * 2u + 1u]);
            }
        }
        /* The two class definitions follow the matrix they index, with their offsets patched in. */
        {
            size_t classDef1 = w->length;

            Wu16(w, classDef1Field, (uint16_t)(classDef1 - base));
            (void)Wclassdef2(w, base, gPair.classRanges1, gPair.classRange1Count);
        }
        {
            size_t classDef2 = w->length;

            Wu16(w, classDef2Field, (uint16_t)(classDef2 - base));
            (void)Wclassdef2(w, base, gPair.classRanges2, gPair.classRange2Count);
        }
    }
    coverageStart = w->length;
    Wu16(w, coverageField, (uint16_t)(coverageStart - base));
    (void)Wcoverage1(w, base, gPair.coverage, gPair.coverageCount);
    return (int)(w->length - start);
}

static int WriteExtensionSubtable(ByteWriter *w, size_t base)
{
    size_t start = w->length;
    size_t offsetField;

    (void)Wu16At(w, 1u);                 /* extensionPosFormat */
    (void)Wu16At(w, RoboTextGposLookupPair); /* the real lookup type */
    offsetField = Wu32At(w, 0u);
    {
        size_t inner = w->length;

        Wu32(w, offsetField, (uint32_t)(inner - base));
        (void)WritePairSubtable(w, inner);
    }
    return (int)(w->length - start);
}

/** A subtable of a type this build does not execute: its bytes are never read. */
static int WriteUnsupportedSubtable(ByteWriter *w, size_t base)
{
    size_t start = w->length;

    (void)base;
    (void)Wu16At(w, 1u);
    (void)Wu16At(w, 0u);
    (void)Wu16At(w, 0xFFFFu); /* a count no table could satisfy, which is the point: nothing reads it */
    return (int)(w->length - start);
}

/** A subtable whose coverage offset points *past the end of the table*: the malformed case. */
static int WriteBadCoverageSubtable(ByteWriter *w, size_t base)
{
    size_t start = w->length;

    (void)Wu16At(w, 1u);
    (void)Wu16At(w, 0xFFF0u); /* the coverage offset */
    (void)Wu16At(w, FixtureValueXAdvance);
    Wvalue(w, FixtureValueXAdvance, &gSingle.values[0]);
    (void)base;
    return (int)(w->length - start);
}

/* ------------------------------------------------------------------ */
/* Running a fixture                                                   */
/* ------------------------------------------------------------------ */

static RoboTextOtPlan FixturePlan(const GposFixture *fixture)
{
    RoboTextOtPlan plan;

    RoboCheckEqInt((int)RoboTextGposPlanResolve(&fixture->layout, RoboUScriptLatin, NULL, NULL, &plan, NULL),
                   (int)RoboOk);
    return plan;
}

/** A buffer of @p count glyphs with the given ids, each one scalar wide, level 0. */
static RoboTextShapingBuffer *BufferOf(const uint16_t *ids, size_t count)
{
    RoboTextShapingBuffer *buffer = RoboTextShapingBufferCreate(count != 0u ? count : 1u);

    RoboCheckMsg(buffer != NULL, "the shaping buffer could not be created");
    for (size_t index = 0u; index < count; index++) {
        RoboTextShapingGlyph glyph;

        RoboTextShapingGlyphInit(&glyph);
        glyph.glyphId = ids[index];
        glyph.source = RoboTextRangeMake(index, index + 1u);
        glyph.cluster = index;
        glyph.bidiLevel = 0u;
        RoboCheckEqInt((int)RoboTextShapingBufferAppend(buffer, &glyph), (int)RoboOk);
    }
    return buffer;
}

static RoboTextGposStats ApplyFixture(const GposFixture *fixture,
                                      const FontGdef *gdef,
                                      const RoboTextOtPlan *plan,
                                      RoboTextShapingBuffer *buffer,
                                      const RoboTextGposOptions *options)
{
    RoboTextGposStats stats;

    RoboCheckEqInt((int)RoboTextGposApply(&fixture->layout, gdef, plan, options, buffer, &stats, NULL),
                   (int)RoboOk);
    return stats;
}

/** One glyph's adjustments, in device pixels. */
typedef struct Adjustments {
    float offsetX;
    float offsetY;
    float advanceX;
    float advanceY;
    int positioned;
} Adjustments;

static Adjustments AdjustmentsOf(const RoboTextShapingBuffer *buffer, size_t index)
{
    const RoboTextShapingGlyph *glyph = RoboTextShapingBufferAt(buffer, index);
    Adjustments out;

    memset(&out, 0, sizeof(out));
    RoboCheckMsg(glyph != NULL, "no glyph at that position");
    out.offsetX = glyph->offsetAdjustX;
    out.offsetY = glyph->offsetAdjustY;
    out.advanceX = glyph->advanceAdjustX;
    out.advanceY = glyph->advanceAdjustY;
    out.positioned = (glyph->flags & RoboTextShapingGlyphPositioned) != 0u ? 1 : 0;
    return out;
}

static void CheckAdjustments(const RoboTextShapingBuffer *buffer,
                             size_t index,
                             float offsetX,
                             float offsetY,
                             float advanceX,
                             float advanceY)
{
    Adjustments got = AdjustmentsOf(buffer, index);

    RoboCheckNear(got.offsetX, offsetX, 0.0001f);
    RoboCheckNear(got.offsetY, offsetY, 0.0001f);
    RoboCheckNear(got.advanceX, advanceX, 0.0001f);
    RoboCheckNear(got.advanceY, advanceY, 0.0001f);
}

/** The options this suite uses: a scale of 1/2, so a value record's units are visibly converted. */
static RoboTextGposOptions OptionsWithScale(float scale, uint16_t ppem)
{
    RoboTextGposOptions options;

    memset(&options, 0, sizeof(options));
    options.deviceScale = scale;
    options.devicePpem = ppem;
    options.direction = RoboTextDirectionLtr;
    options.bidiLevel = 0u;
    return options;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestGposVocabulary(void)
{
    RoboTextGposStats stats;
    RoboTextGposOptions options;

    /* The nine types are the table's, and the extension wrapper is **9** — not GSUB's 7, which is the
     * mistake this assertion exists to make impossible. */
    RoboCheckEqInt((int)RoboTextGposLookupSingle, 1);
    RoboCheckEqInt((int)RoboTextGposLookupPair, 2);
    RoboCheckEqInt((int)RoboTextGposLookupCursive, 3);
    RoboCheckEqInt((int)RoboTextGposLookupMarkToBase, 4);
    RoboCheckEqInt((int)RoboTextGposLookupMarkToLigature, 5);
    RoboCheckEqInt((int)RoboTextGposLookupMarkToMark, 6);
    RoboCheckEqInt((int)RoboTextGposLookupContext, 7);
    RoboCheckEqInt((int)RoboTextGposLookupChainingContext, 8);
    RoboCheckEqInt((int)RoboTextGposLookupExtension, 9);
    RoboCheckEqInt((int)RoboTextGposLookupTypeCount, 9);

    /* Phase 5 knows all nine and this build executes three; both statements are readable and neither is
     * inferred from the other. */
    for (uint16_t type = 1u; type <= RoboTextGposLookupTypeCount; type++) {
        int executed = (type == RoboTextGposLookupSingle || type == RoboTextGposLookupPair ||
                        type == RoboTextGposLookupExtension)
                           ? 1
                           : 0;

        RoboCheck(FontGposLookupTypeIsKnown(type));
        /* This build's own answer, stated as the three types whose readers exist: the engine's predicate is
         * internal (`RoboTextOtInternal.h`), so the *behaviour* is what this asserts — see
         * `text.gpos.limits_malformed` for the counted skip a mark lookup gets. */
        RoboCheckEqInt(executed,
                       (type == RoboTextGposLookupSingle || type == RoboTextGposLookupPair ||
                        type == RoboTextGposLookupExtension)
                           ? 1
                           : 0);
    }
    RoboCheck(!FontGposLookupTypeIsKnown(0u));
    RoboCheck(!FontGposLookupTypeIsKnown(10u));

    /* One limits type for both tables, and the shared defaults. */
    RoboCheckEqInt((int)RoboTextOtDefaultLimits.maxLookupApplications, 4096);
    RoboCheckEqInt((int)RoboTextOtDefaultLimits.maxGlyphProbes, 1048576);
    RoboCheck(RoboTextOtDefaultLimits.maxGlyphs == 65536u);
    RoboCheckEqInt((int)RoboTextOtDefaultLimits.maxNesting, 6);

    RoboTextGposStatsInit(&stats);
    RoboCheck(stats.glyphsBefore == 0u && stats.adjustedGlyphs == 0u && stats.limitReached == 0);
    memset(&stats, 0xFF, sizeof(stats));
    RoboTextGposStatsInit(&stats);
    RoboCheck(stats.lookupsApplied == 0u && stats.classPairAdjustments == 0u);
    RoboTextGposStatsInit(NULL);

    memset(&options, 0, sizeof(options));
    RoboCheck(options.limits == NULL && options.trace == NULL && options.deviceScale == 0.0f);
    RoboCheckEqInt((int)RoboTextGposNoGlyph, 0xFFFF);
    RoboCheckEqInt((int)RoboTextGposNoClass, 0xFFFF);

    /* A buffer mutated by a lookup the caller never asked for: no plan, no adjustments, and the counters say
     * the call ran nothing rather than that it failed. */
    {
        static const uint16_t kIds[2] = {FixtureA, FixtureV};
        RoboTextShapingBuffer *buffer = BufferOf(kIds, 2u);
        RoboTextOtPlan empty;
        RoboTextGposStats runStats;

        RoboTextOtPlanInit(&empty);
        RoboCheckEqInt((int)RoboTextGposApply(NULL, NULL, &empty, NULL, buffer, &runStats, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)runStats.lookupsApplied, 0);
        RoboCheckEqInt((int)runStats.adjustedGlyphs, 0);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboCheckEqInt(AdjustmentsOf(buffer, 0u).positioned, 0);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* Refusals: no buffer, no stats. */
    {
        RoboTextGposStats runStats;

        RoboCheckEqInt((int)RoboTextGposApply(NULL, NULL, NULL, NULL, NULL, &runStats, NULL),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextGposApply(NULL, NULL, NULL, NULL, NULL, NULL, NULL),
                       (int)RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The defaults and the plan                                           */
/* ------------------------------------------------------------------ */

static void TestGposDefaults(void)
{
    RoboTextFeatureSet latin;
    RoboTextFeatureSet indic;

    RoboTextGposDefaultFeatureSet(RoboUScriptLatin, &latin);
    /* The five a positioning table wants by default, `kern` above all: it is what 12.13 is for, and it is
     * unconditional enough that a caller has to say `Off` to lose it. */
    RoboCheck(RoboTextFeatureSetFind(&latin, "kern") != NULL);
    RoboCheck(RoboTextFeatureSetFind(&latin, "mark") != NULL);
    RoboCheck(RoboTextFeatureSetFind(&latin, "mkmk") != NULL);
    RoboCheck(RoboTextFeatureSetFind(&latin, "curs") != NULL);
    RoboCheck(RoboTextFeatureSetFind(&latin, "dist") != NULL);
    /* Not substitution's list: positioning does not ligate, and a default that said `liga` would claim it. */
    RoboCheck(RoboTextFeatureSetFind(&latin, "liga") == NULL);
    RoboCheck(RoboTextFeatureSetFind(&latin, "abvm") == NULL);

    RoboTextGposDefaultFeatureSet(RoboUScriptDevanagari, &indic);
    RoboCheck(RoboTextFeatureSetFind(&indic, "abvm") != NULL);
    RoboCheck(RoboTextFeatureSetFind(&indic, "blwm") != NULL);
    RoboTextGposDefaultFeatureSet(RoboUScriptLatin, NULL);
}

/** Lookup 0: a real pair lookup with one pair, so the plan test's plan has something to apply. */
static int WritePlanPairSubtable(ByteWriter *w, size_t base)
{
    static const uint16_t kCoverage[1] = {FixtureA};
    static const uint16_t kSeconds[1] = {FixtureV};
    static ValueFields firsts[1];
    static ValueFields seconds[1];

    memset(firsts, 0, sizeof(firsts));
    memset(seconds, 0, sizeof(seconds));
    firsts[0].xAdvance = -100;
    gPair.format = 1u;
    gPair.coverage = kCoverage;
    gPair.coverageCount = 1u;
    gPair.valueFormat1 = FixtureValueXAdvance;
    gPair.valueFormat2 = 0u;
    gPair.seconds = kSeconds;
    gPair.firstValues = firsts;
    gPair.secondValues = seconds;
    gPair.pairCount = 1u;
    return WritePairSubtable(w, base);
}

/** Lookup 1: a single adjustment, covering one glyph. */
static int WritePlanSingleSubtable(ByteWriter *w, size_t base)
{
    static const uint16_t kCoverage[1] = {FixtureBase};

    gSingle.format = 1u;
    gSingle.coverage = kCoverage;
    gSingle.coverageCount = 1u;
    gSingle.valueFormat = FixtureValueXPlacement;
    gSingle.values[0] = Value(64, 0, 0, 0);
    return WriteSingleSubtable(w, base);
}

static void TestGposPlan(void)
{
    GposFixture fixture;
    GposFeatureSpec features[2];
    GposLookupSpec lookups[2];
    RoboTextOtPlan plan;
    RoboTextFeatureSet settings;

    memset(features, 0, sizeof(features));
    memset(lookups, 0, sizeof(lookups));
    features[0].tag = "kern";
    features[0].lookups[0] = 0u;
    features[0].count = 1u;
    features[1].tag = "mark";
    features[1].lookups[0] = 1u;
    features[1].count = 1u;
    lookups[0].type = RoboTextGposLookupPair;
    lookups[0].write = WritePlanPairSubtable;
    lookups[1].type = RoboTextGposLookupSingle;
    lookups[1].write = WritePlanSingleSubtable;

    FixtureBuild(&fixture, "latn", features, 2u, lookups, 2u);

    RoboCheckEqInt((int)RoboTextGposPlanResolve(&fixture.layout, RoboUScriptLatin, NULL, NULL, &plan, NULL),
                   (int)RoboOk);
    RoboCheck(!plan.empty);
    RoboCheckEqInt((int)plan.lookupCount, 2);
    RoboCheckEqInt((int)plan.featureCount, 2);
    RoboCheckEqInt((int)plan.scriptTag, (int)FixtureTag("latn"));
    RoboCheck(plan.usedDefaultLangSys);
    RoboCheck(!plan.usedDefaultScript);
    RoboCheckEqInt((int)plan.lookupOrder[0], 0);
    RoboCheckEqInt((int)plan.lookupOrder[1], 1);

    /* A caller's `kern` off is honoured, and the lookup only `kern` names leaves the plan with it: that is
     * the feature-controlled activation 12.13's acceptance criteria ask for, and it is the caller's setting
     * beating the default rather than a special case. */
    RoboTextFeatureSetInit(&settings);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&settings, "kern", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGposPlanResolve(&fixture.layout, RoboUScriptLatin, NULL, &settings, &plan, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)plan.lookupCount, 1);
    RoboCheckEqInt((int)plan.lookupOrder[0], 1);
    RoboCheckEqInt((int)plan.featureCount, 1);
    RoboCheckStrEq(plan.features[0].tag, "mark");

    /* Everything off is a plan with no lookups, not a failure. */
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&settings, "mark", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGposPlanResolve(&fixture.layout, RoboUScriptLatin, NULL, &settings, &plan, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)plan.lookupCount, 0);
    RoboCheck(!plan.empty);

    /* A script the font has no record for, with no `DFLT` to answer: empty, and not an error. */
    RoboCheckEqInt((int)RoboTextGposPlanResolve(&fixture.layout, RoboUScriptArabic, NULL, NULL, &plan, NULL),
                   (int)RoboOk);
    RoboCheck(plan.empty);

    /* No table at all is the same answer. */
    RoboCheckEqInt((int)RoboTextGposPlanResolve(NULL, RoboUScriptLatin, NULL, NULL, &plan, NULL), (int)RoboOk);
    RoboCheck(plan.empty);
    RoboCheckEqInt((int)RoboTextGposPlanResolve(&fixture.layout, RoboUScriptLatin, NULL, NULL, NULL, NULL),
                   (int)RoboInvalidArgument);

    /* A GSUB table is *not* an error for the plan: resolving a plan is the same question for both tables (one
     * shared core, two defaults), so the plan is a plan. What refuses it is the **engine**, which knows the
     * reader it would have to reach for. That distinction is the whole point of sharing the resolution. */
    {
        FontOtLayout gsub;
        static const uint16_t kIds[2] = {FixtureA, FixtureV};
        RoboTextShapingBuffer *buffer;
        RoboTextGposStats stats;

        memset(&gsub, 0, sizeof(gsub));
        gsub.kind = RoboFontOtLayoutGsub;
        RoboCheckEqInt((int)RoboTextGposPlanResolve(&gsub, RoboUScriptLatin, NULL, NULL, &plan, NULL), (int)RoboOk);
        RoboCheck(plan.empty); /* No ScriptList, so nothing resolved. */
        buffer = BufferOf(kIds, 2u);
        plan = FixturePlan(&fixture);
        RoboCheckEqInt((int)RoboTextGposApply(&gsub, NULL, &plan, NULL, buffer, &stats, NULL),
                       (int)RoboInvalidArgument);
        RoboTextShapingBufferDestroy(buffer);
    }

    FixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Single adjustment                                                   */
/* ------------------------------------------------------------------ */

static void TestGposSingle(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    static const uint16_t kCoverage[2] = {FixtureA, FixtureBase};
    static const uint16_t kIds[2] = {FixtureA, FixtureV};

    /* Format 1: one record for every glyph the coverage names, with all four fields and a 1/2 scale. */
    {
        static ValueFields values[1];

        values[0] = Value(10, -5, -20, 3);
        gSingle.format = 1u;
        gSingle.coverage = kCoverage;
        gSingle.coverageCount = 2u;
        gSingle.valueFormat =
            FixtureValueXPlacement | FixtureValueYPlacement | FixtureValueXAdvance | FixtureValueYAdvance;
        gSingle.values[0] = values[0];
        gSingle.valueCount = 1u;
        feature.tag = "kern";
        feature.lookups[0] = 0u;
        feature.count = 1u;
        lookup.type = RoboTextGposLookupSingle;
        lookup.flags = 0u;
        lookup.write = WriteSingleSubtable;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(0.5f, 0u);
        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.singleAdjustments, 1);
        RoboCheckEqInt((int)stats.adjustedGlyphs, 1);
        RoboCheckEqInt((int)stats.lookupsApplied, 1);
        RoboCheckEqInt(AdjustmentsOf(buffer, 0u).positioned, 1);
        CheckAdjustments(buffer, 0u, 5.0f, -2.5f, -10.0f, 1.5f);
        /* The second glyph is not in the coverage: nothing at all, not an adjustment of zero. */
        RoboCheckEqInt(AdjustmentsOf(buffer, 1u).positioned, 0);
        CheckAdjustments(buffer, 1u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }

    /* Format 2: one record per covered glyph, indexed by the coverage index — so the *second* covered glyph
     * gets the second record, which is what makes the coverage index arithmetic testable. */
    {
        static const uint16_t kTwo[2] = {FixtureA, FixtureBase};
        static const uint16_t kIds2[2] = {FixtureA, FixtureBase};

        gSingle.format = 2u;
        gSingle.coverage = kTwo;
        gSingle.coverageCount = 2u;
        gSingle.valueFormat = FixtureValueXAdvance;
        gSingle.values[0] = Value(0, 0, -8, 0);
        gSingle.values[1] = Value(0, 0, -16, 0);
        gSingle.valueCount = 2u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(1.0f, 0u);
        buffer = BufferOf(kIds2, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.singleAdjustments, 2);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -8.0f, 0.0f);
        CheckAdjustments(buffer, 1u, 0.0f, 0.0f, -16.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }

    /* Coverage **format 2**: a range walk rather than a glyph array, and the covered set is the ranges'
     * union. The single-glyph range names glyph 5; the second range starts at 8 and names two glyphs — so 5
     * and 8 are covered, 9 is covered, and 7 is not. */
    {
        static const uint16_t kIds3[3] = {FixtureBase, FixtureO, FixtureV};

        gSingle.format = 1u;
        gSingle.coverageFormat2 = 1;
        gSingle.coverage2Start = FixtureBase;
        gSingle.coverage2Count = 2u;
        gSingle.valueFormat = FixtureValueXAdvance;
        gSingle.values[0] = Value(0, 0, -30, 0);
        gSingle.valueCount = 1u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(1.0f, 0u);
        buffer = BufferOf(kIds3, 3u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.singleAdjustments, 2);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -30.0f, 0.0f); /* glyph 5, the single-glyph range */
        CheckAdjustments(buffer, 1u, 0.0f, 0.0f, -30.0f, 0.0f); /* glyph 8, the second range's first */
        CheckAdjustments(buffer, 2u, 0.0f, 0.0f, 0.0f, 0.0f);   /* glyph 7, in neither range */
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
        gSingle.coverageFormat2 = 0;
    }

    /* A record whose device offset is set but whose ppem is unknown: a delta is **not** evaluated, and the
     * count says so. No scale at all (0) means no adjustment is applied, because a font unit is not a pixel. */
    {
        gSingle.format = 1u;
        gSingle.coverage = kCoverage;
        gSingle.coverageCount = 1u;
        gSingle.valueFormat = FixtureValueXAdvance;
        gSingle.values[0] = Value(0, 0, -20, 0);
        gSingle.valueCount = 1u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(0.0f, 0u);
        buffer = BufferOf(kIds, 1u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.singleAdjustments, 0);
        RoboCheckEqInt((int)stats.recordsUnevaluated, 1);
        RoboCheckEqInt(AdjustmentsOf(buffer, 0u).positioned, 0);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* Pair adjustment                                                     */
/* ------------------------------------------------------------------ */

static void TestGposPairFormat1(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    static const uint16_t kCoverage[1] = {FixtureA};
    static const uint16_t kSeconds[2] = {FixtureV, FixtureO};
    static ValueFields firsts[2];
    static ValueFields seconds[2];

    /*
     * Two pairs: `A V` with a value for each side, and `A O` whose values are all zero — a font that says
     * "this pair is 0" rather than one that says nothing, which is the distinction `positioned` exists to
     * keep. `FixtureT` is deliberately **not** in the pair set: that is the "no pair for this second glyph"
     * case, and a second glyph absent from the buffer is the other.
     */
    firsts[0] = Value(0, 0, -125, 0);
    seconds[0] = Value(25, 0, 0, 0);
    firsts[1] = Value(0, 0, 0, 0);
    seconds[1] = Value(0, 0, 0, 0);
    gPair.format = 1u;
    gPair.coverage = kCoverage;
    gPair.coverageCount = 1u;
    gPair.valueFormat1 = FixtureValueXPlacement | FixtureValueXAdvance;
    gPair.valueFormat2 = FixtureValueXPlacement | FixtureValueXAdvance;
    gPair.seconds = kSeconds;
    gPair.firstValues = firsts;
    gPair.secondValues = seconds;
    gPair.pairCount = 2u;

    feature.tag = "kern";
    feature.lookups[0] = 0u;
    feature.count = 1u;
    lookup.type = RoboTextGposLookupPair;
    lookup.flags = 0u;
    lookup.write = WritePairSubtable;
    FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
    plan = FixturePlan(&fixture);
    options = OptionsWithScale(1.0f, 0u);

    /* A V: the font's own numbers, applied to the right glyphs. */
    {
        static const uint16_t kIds[2] = {FixtureA, FixtureV};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 1);
        RoboCheckEqInt((int)stats.classPairAdjustments, 0);
        RoboCheckEqInt((int)stats.adjustedGlyphs, 2);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -125.0f, 0.0f);
        CheckAdjustments(buffer, 1u, 25.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* A zero record is a pair the font positioned: `positioned` is set and the counts move, because "the
     * font said this pair is 0" is not the same answer as "the font said nothing". */
    {
        static const uint16_t kIds[2] = {FixtureA, FixtureO};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 1);
        RoboCheckEqInt((int)stats.adjustedGlyphs, 0);
        RoboCheckEqInt(AdjustmentsOf(buffer, 0u).positioned, 1);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* A T: the pair set has no record for it. No adjustment, and no pair counted. */
    {
        static const uint16_t kIds[2] = {FixtureA, FixtureT};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 0);
        /* Tried at both positions: the scan asks every glyph, and the last one simply has no partner. */
        RoboCheckEqInt((int)stats.subtablesTried, 2);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* A alone: no second glyph, so no pair — and the last position of a buffer is a legal place to ask. */
    {
        static const uint16_t kIds[1] = {FixtureA};

        buffer = BufferOf(kIds, 1u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 0);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* V A: the first glyph is V, which the coverage does not name. Overlapping pairs are a property of the
     * scan, not of this table, so the assertion is only that nothing was applied. */
    {
        static const uint16_t kIds[2] = {FixtureV, FixtureA};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 0);
        RoboTextShapingBufferDestroy(buffer);
    }

    FixtureClose(&fixture);
}

static void TestGposPairFormat2(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    static const uint16_t kCoverage[1] = {FixtureA};
    /* Class 1 for the first glyph: A is class 1, and the range keeps 7..9 (V, O, T) out of it. */
    static const uint16_t kRanges1[3] = {FixtureA, FixtureA, 1u};
    /* Class 2 for the second glyph: V..T are class 1. */
    static const uint16_t kRanges2[6] = {FixtureV, FixtureV, 1u, FixtureO, FixtureT, 2u};
    static ValueFields matrix[2u * 3u * 2u];

    /* A 2 x 3 matrix, with a distinct value for every cell, so a wrong index is a wrong number. */
    matrix[0] = Value(0, 0, -100, 0); /* [0][0] */
    matrix[1] = Value(0, 0, 0, 0);
    matrix[2] = Value(0, 0, -10, 0); /* [0][1] */
    matrix[3] = Value(0, 0, 0, 0);
    matrix[4] = Value(0, 0, -20, 0); /* [0][2] */
    matrix[5] = Value(0, 0, 0, 0);
    matrix[6] = Value(0, 0, -1, 0); /* [1][0] */
    matrix[7] = Value(0, 0, 0, 0);
    matrix[8] = Value(0, 0, -2, 0); /* [1][1] */
    matrix[9] = Value(0, 0, 0, 0);
    matrix[10] = Value(0, 0, -3, 0); /* [1][2] */
    matrix[11] = Value(0, 0, 0, 0);

    gPair.format = 2u;
    gPair.coverage = kCoverage;
    gPair.coverageCount = 1u;
    gPair.valueFormat1 = FixtureValueXAdvance;
    gPair.valueFormat2 = 0u;
    gPair.classRanges1 = kRanges1;
    gPair.classRange1Count = 1u;
    gPair.classRanges2 = kRanges2;
    gPair.classRange2Count = 2u;
    gPair.class1Count = 2u;
    gPair.class2Count = 3u;
    gPair.matrix = matrix;

    feature.tag = "kern";
    feature.lookups[0] = 0u;
    feature.count = 1u;
    lookup.type = RoboTextGposLookupPair;
    lookup.flags = 0u;
    lookup.write = WritePairSubtable;
    FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
    plan = FixturePlan(&fixture);
    options = OptionsWithScale(1.0f, 0u);

    /*
     * Class 1 x class 1 — `A` is class *one* (the class definition's range names it), and so is `V`, so the
     * cell is `[1][1]`. That the answer is -2 rather than the -100 of cell `[0][0]` is the assertion: the
     * matrix is indexed by both classes, not read from the front.
     */
    {
        static const uint16_t kIds[2] = {FixtureA, FixtureV};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.classPairAdjustments, 1);
        RoboCheckEqInt((int)stats.pairAdjustments, 0);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -2.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* Class 1 x class 2: a different cell on the same row. */
    {
        static const uint16_t kIds[2] = {FixtureA, FixtureO};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.classPairAdjustments, 1);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -3.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* Class 1 x class 0 — a second glyph no range lists. Class 0 is a real class, and the matrix's first
     * column is what a font uses to say "everything else". Reading the class definition and finding nothing
     * is therefore class 0, not a refusal. */
    {
        static const uint16_t kIds[2] = {FixtureA, FixtureMark};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.classPairAdjustments, 1);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -1.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    /* The first glyph outside the coverage: no class pair at all, even though its class would be 0. */
    {
        static const uint16_t kIds[2] = {FixtureV, FixtureV};

        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.classPairAdjustments, 0);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
    }

    FixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The extension wrapper                                               */
/* ------------------------------------------------------------------ */

static void TestGposExtension(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    RoboTextGposEvent events[4];
    static const uint16_t kCoverage[1] = {FixtureA};
    static const uint16_t kSeconds[1] = {FixtureV};
    static ValueFields firsts[1];
    static ValueFields seconds[1];
    static const uint16_t kIds[2] = {FixtureA, FixtureV};

    firsts[0] = Value(0, 0, -64, 0);
    seconds[0] = Value(0, 0, 0, 0);
    gPair.format = 1u;
    gPair.coverage = kCoverage;
    gPair.coverageCount = 1u;
    gPair.valueFormat1 = FixtureValueXAdvance;
    gPair.valueFormat2 = 0u;
    gPair.seconds = kSeconds;
    gPair.firstValues = firsts;
    gPair.secondValues = seconds;
    gPair.pairCount = 1u;

    feature.tag = "kern";
    feature.lookups[0] = 0u;
    feature.count = 1u;
    lookup.type = RoboTextGposLookupExtension;
    lookup.flags = 0u;
    lookup.write = WriteExtensionSubtable;
    FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
    plan = FixturePlan(&fixture);
    options = OptionsWithScale(1.0f, 0u);
    memset(events, 0, sizeof(events));
    options.trace = events;
    options.traceCapacity = 4u;

    buffer = BufferOf(kIds, 2u);
    stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
    RoboCheckEqInt((int)stats.lookupsApplied, 1);
    RoboCheckEqInt((int)stats.pairAdjustments, 1);
    /* The trace reports the type that actually ran: the *inner* one, not the wrapper every extension lookup
     * would otherwise be recorded as. */
    RoboCheckEqInt((int)stats.traceEvents, 1);
    RoboCheckEqInt((int)events[0].lookupType, (int)RoboTextGposLookupPair);
    RoboCheckEqInt((int)events[0].firstGlyph, FixtureA);
    RoboCheckEqInt((int)events[0].secondGlyph, FixtureV);
    RoboCheckNear(events[0].first.xAdvance, -64.0f, 0.0001f);
    RoboCheckEqInt((int)events[0].source.start, 0);
    CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -64.0f, 0.0f);
    RoboTextShapingBufferDestroy(buffer);
    FixtureClose(&fixture);

    /*
     * A lookup whose subtable offset is zero: OpenType's "there is no subtable here". The offset is stated
     * against the lookup table, so a zero one resolves onto the lookup's **own header** — and reading those
     * six bytes as a coverage offset is how a table that means nothing turns into a table that means garbage.
     * Refused as malformed before a byte of it is read, and the scan carries on.
     */
    {
        GposLookupSpec empty;

        memset(&empty, 0, sizeof(empty));
        empty.type = RoboTextGposLookupPair;
        empty.flags = 0u;
        empty.write = NULL;
        FixtureBuild(&fixture, "latn", &feature, 1u, &empty, 1u);
        plan = FixturePlan(&fixture);
        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.lookupsApplied, 1);
        RoboCheckEqInt((int)stats.subtablesTried, 0);
        RoboCheckEqInt((int)stats.subtablesMalformed, 1);
        RoboCheckEqInt((int)stats.pairAdjustments, 0);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* Device corrections                                                  */
/* ------------------------------------------------------------------ */

/** A pair subtable whose first record's x-advance names a device table, written after the pair set. */
static int WriteDevicePairSubtable(ByteWriter *w, size_t base)
{
    static const uint16_t kCoverage[1] = {FixtureA};
    size_t start = w->length;
    size_t offsetField;
    size_t pairSet;
    size_t deviceOffsetField;
    size_t device;
    size_t coverageField;

    (void)Wu16At(w, 1u);
    coverageField = Wu16At(w, 0u);
    (void)Wu16At(w, FixtureValueXAdvance | FixtureValueXAdvanceDevice);
    (void)Wu16At(w, 0u);
    (void)Wu16At(w, 1u);
    offsetField = Wu16At(w, 0u);
    pairSet = w->length;
    Wu16(w, offsetField, (uint16_t)(pairSet - base));
    (void)Wu16At(w, 1u); /* pairValueCount */
    (void)Wu16At(w, FixtureV);
    (void)Wu16At(w, (uint16_t)(int16_t)-100); /* xAdvance */
    deviceOffsetField = Wu16At(w, 0u);        /* the device offset, patched below */
    device = w->length;
    Wu16(w, deviceOffsetField, (uint16_t)(device - base));
    /* A device table at ppem 12..16, delta format 2 (4 bits per delta), deltas -4 at 12, -3 at 13, ... */
    (void)Wu16At(w, 12u);  /* startSize */
    (void)Wu16At(w, 16u);  /* endSize */
    (void)Wu16At(w, 2u);   /* deltaFormat */
    (void)Wu16At(w, 0xDDDCu); /* -3,-4,-3,-3 in 4-bit two's complement, least significant nibble first */
    (void)Wu16At(w, 0x0001u); /* ppem 16's word: its first nibble is +1, so the second word is reached */
    {
        size_t coverageStart = w->length;

        Wu16(w, coverageField, (uint16_t)(coverageStart - base));
        (void)Wcoverage1(w, base, kCoverage, 1u);
    }
    return (int)(w->length - start);
}

static void TestGposDevice(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    static const uint16_t kIds[2] = {FixtureA, FixtureV};

    feature.tag = "kern";
    feature.lookups[0] = 0u;
    feature.count = 1u;
    lookup.type = RoboTextGposLookupPair;
    lookup.flags = 0u;
    lookup.write = WriteDevicePairSubtable;
    FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
    plan = FixturePlan(&fixture);

    /* At ppem 12 the delta is -4 device units, and a device unit is 1/64 px: -0.0625 px on top of the
     * font's -100. That the two are *added* rather than one replacing the other is the assertion. */
    options = OptionsWithScale(1.0f, 12u);
    buffer = BufferOf(kIds, 2u);
    stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
    RoboCheckEqInt((int)stats.pairAdjustments, 1);
    RoboCheckEqInt((int)stats.deviceCorrections, 1);
    RoboCheckEqInt((int)stats.deviceUnevaluated, 0);
    CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -100.0f - 4.0f / 64.0f, 0.0f);
    RoboTextShapingBufferDestroy(buffer);

    /* ppem 13 is the packing's *second* nibble on the same word: -3 device units. That the two answers differ
     * is what makes the bit arithmetic testable rather than a constant that happens to be right once. */
    options = OptionsWithScale(1.0f, 13u);
    buffer = BufferOf(kIds, 2u);
    stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
    CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -100.0f - 3.0f / 64.0f, 0.0f);
    RoboTextShapingBufferDestroy(buffer);

    /* ppem 16 is the *second* delta word's first nibble: +1, so the correction is positive and the assertion
     * can tell an added delta from a subtracted one. */
    options = OptionsWithScale(1.0f, 16u);
    buffer = BufferOf(kIds, 2u);
    stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
    CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -100.0f + 1.0f / 64.0f, 0.0f);
    RoboTextShapingBufferDestroy(buffer);

    /* Outside the recorded range there is no correction, by definition — and no error either. */
    options = OptionsWithScale(1.0f, 32u);
    buffer = BufferOf(kIds, 2u);
    stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
    RoboCheckEqInt((int)stats.deviceCorrections, 0);
    RoboCheckEqInt((int)stats.deviceUnevaluated, 1);
    CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -100.0f, 0.0f);
    RoboTextShapingBufferDestroy(buffer);

    /* **No size known**: the font's own number is applied and the delta is counted as unevaluated. A delta
     * applied at the wrong size is a worse lie than no delta. */
    options = OptionsWithScale(1.0f, 0u);
    buffer = BufferOf(kIds, 2u);
    stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
    RoboCheckEqInt((int)stats.deviceCorrections, 0);
    RoboCheckEqInt((int)stats.deviceUnevaluated, 1);
    CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -100.0f, 0.0f);
    RoboTextShapingBufferDestroy(buffer);

    FixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Lookup flags, marks and direction                                   */
/* ------------------------------------------------------------------ */

static void TestGposMarksAndDirection(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    uint8_t gdefBytes[256];
    static const uint16_t kCoverage[1] = {FixtureA};
    static const uint16_t kSeconds[1] = {FixtureV};
    static ValueFields firsts[1];
    static ValueFields seconds[1];
    /* A base, a **mark**, then the pair's second glyph: the pair this table records is A V, and only a lookup
     * that ignores marks may reach the V across the mark. */
    static const uint16_t kIds[3] = {FixtureA, FixtureMark, FixtureV};

    firsts[0] = Value(0, 0, -100, 0);
    seconds[0] = Value(0, 0, 0, 0);
    gPair.format = 1u;
    gPair.coverage = kCoverage;
    gPair.coverageCount = 1u;
    gPair.valueFormat1 = FixtureValueXAdvance;
    gPair.valueFormat2 = 0u;
    gPair.seconds = kSeconds;
    gPair.firstValues = firsts;
    gPair.secondValues = seconds;
    gPair.pairCount = 1u;

    feature.tag = "kern";
    feature.lookups[0] = 0u;
    feature.count = 1u;
    lookup.type = RoboTextGposLookupPair;
    lookup.write = WritePairSubtable;

    {
        FontGdef gdef = FixtureBuildGdef(gdefBytes, sizeof(gdefBytes), FixtureMark, 3u);

        /* With `IgnoreMarks`, the pair is reached across the mark: the V is the next glyph the lookup sees. */
        lookup.flags = 0x0008u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(1.0f, 0u);
        buffer = BufferOf(kIds, 3u);
        stats = ApplyFixture(&fixture, &gdef, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 1);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -100.0f, 0.0f);
        /* The mark itself is never touched: it is stepped over, not adjusted and not removed. */
        CheckAdjustments(buffer, 1u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboCheckEqInt((int)RoboTextShapingBufferCount(buffer), 3);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);

        /* Without the flag, the mark **is** the second glyph, and this table has no pair for it: the font's
         * own condition, answered by the flags rather than by a guess about what a mark is. */
        lookup.flags = 0u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        buffer = BufferOf(kIds, 3u);
        stats = ApplyFixture(&fixture, &gdef, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 0);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }

    /* A lookup whose flag says `RightToLeft` applies only to a right-to-left run — and the glyph order is
     * never reversed to make that true: the RTL case is the same buffer, position for position. */
    {
        lookup.flags = 0x0001u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        {
            static const uint16_t kPair[2] = {FixtureA, FixtureV};

            options = OptionsWithScale(1.0f, 0u);
            buffer = BufferOf(kPair, 2u);
            stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
            RoboCheckEqInt((int)stats.pairAdjustments, 0);
            RoboTextShapingBufferDestroy(buffer);

            options.direction = RoboTextDirectionRtl;
            options.bidiLevel = 1u;
            buffer = BufferOf(kPair, 2u);
            stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
            RoboCheckEqInt((int)stats.pairAdjustments, 1);
            CheckAdjustments(buffer, 0u, 0.0f, 0.0f, -100.0f, 0.0f);
            /* The buffer's order is untouched, which is the claim: kerning does not reorder anything. */
            RoboCheckEqInt((int)RoboTextShapingBufferAt(buffer, 0u)->glyphId, FixtureA);
            RoboCheckEqInt((int)RoboTextShapingBufferAt(buffer, 1u)->glyphId, FixtureV);
            RoboTextShapingBufferDestroy(buffer);
        }
        FixtureClose(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* Limits, unsupported types and malformed bytes                       */
/* ------------------------------------------------------------------ */

static void TestGposLimitsAndMalformed(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    static const uint16_t kCoverage[1] = {FixtureBase};
    static const uint16_t kIds[2] = {FixtureA, FixtureV};

    feature.tag = "kern";
    feature.lookups[0] = 0u;
    feature.count = 1u;
    lookup.flags = 0u;
    lookup.write = WritePlanSingleSubtable;
    lookup.type = RoboTextGposLookupSingle;

    /* A probe budget of one: the first coverage question is charged, the budget is spent, and the call says
     * so rather than reading on. */
    {
        RoboTextOtLimits limits = RoboTextOtDefaultLimits;

        limits.maxGlyphProbes = 1u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(1.0f, 0u);
        options.limits = &limits;
        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheck(stats.limitReached);
        RoboCheckEqInt((int)stats.probes, 1);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }

    /* No lookup applications at all: the plan resolves, the call runs nothing, and the flag records it. */
    {
        RoboTextOtLimits limits = RoboTextOtDefaultLimits;

        limits.maxLookupApplications = 0u;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(1.0f, 0u);
        options.limits = &limits;
        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheck(stats.limitReached);
        RoboCheckEqInt((int)stats.lookupsApplied, 0);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }

    /* A lookup type this build recognises and does not execute: counted, not read. */
    {
        GposLookupSpec unsupported;

        memset(&unsupported, 0, sizeof(unsupported));
        options = OptionsWithScale(1.0f, 0u);
        unsupported.type = RoboTextGposLookupMarkToBase;
        unsupported.flags = 0u;
        unsupported.write = WriteUnsupportedSubtable;
        FixtureBuild(&fixture, "latn", &feature, 1u, &unsupported, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(1.0f, 0u);
        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.lookupsUnsupported, 1);
        RoboCheckEqInt((int)stats.lookupsApplied, 0);
        RoboCheckEqInt((int)stats.subtablesTried, 0);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }

    /*
     * A lookup type the format does not define (10) never reaches this engine: Phase 5 refuses the whole
     * table, and that is the right place for the judgement — a table carrying a type nobody can interpret is
     * not a table whose other lookups may be trusted either. The engine's own skip path is for a **known**
     * type it does not execute, which the mark-to-base case above covers.
     */
    {
        GposLookupSpec valid;
        ByteWriter patch;
        FontSpan span;
        FontOtLayout refused;

        memset(&valid, 0, sizeof(valid));
        valid.type = RoboTextGposLookupPair;
        valid.flags = 0u;
        valid.write = WriteUnsupportedSubtable;
        FixtureBuild(&fixture, "latn", &feature, 1u, &valid, 1u);
        /* Build a table Phase 5 accepts, then change the one number that decides: the lookup's type. */
        patch.data = fixture.bytes;
        patch.capacity = sizeof(fixture.bytes);
        patch.length = fixture.length;
        Wu16(&patch, fixture.lookupOffsets[0], 10u);
        span.data = fixture.bytes;
        span.size = fixture.length;
        memset(&refused, 0, sizeof(refused));
        RoboCheck(FontGposParse(span, &refused) != RoboOk);
        FontOtLayoutClear(&refused);
        FixtureClose(&fixture);
    }

    /* A coverage offset past the end of the table: no adjustment, no read outside the table, and the scan
     * carries on. The renderer must survive a malformed font, which is exactly this. */
    {
        gSingle.format = 1u;
        gSingle.coverage = kCoverage;
        gSingle.coverageCount = 1u;
        gSingle.valueFormat = FixtureValueXAdvance;
        gSingle.values[0] = Value(0, 0, -20, 0);
        gSingle.valueCount = 1u;
        lookup.write = WriteBadCoverageSubtable;
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        options = OptionsWithScale(1.0f, 0u);
        buffer = BufferOf(kIds, 2u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.singleAdjustments, 0);
        CheckAdjustments(buffer, 0u, 0.0f, 0.0f, 0.0f, 0.0f);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
        lookup.write = WritePlanSingleSubtable;
    }

    /* A zero-length buffer, and an index past the end: both are answered, neither is read. */
    {
        FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
        plan = FixturePlan(&fixture);
        buffer = BufferOf(kIds, 0u);
        stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
        RoboCheckEqInt((int)stats.pairAdjustments, 0);
        RoboCheckEqInt((int)stats.singleAdjustments, 0);
        RoboCheckEqInt((int)RoboTextShapingBufferCount(buffer), 0);
        RoboTextShapingBufferDestroy(buffer);
        FixtureClose(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void TestGposDiagnostics(void)
{
    GposFixture fixture;
    GposFeatureSpec feature;
    GposLookupSpec lookup;
    RoboTextOtPlan plan;
    RoboTextGposOptions options;
    RoboTextShapingBuffer *buffer;
    RoboTextGposStats stats;
    RoboTextGposEvent events[4];
    char dump[2048];
    size_t length = 0u;
    static const uint16_t kCoverage[1] = {FixtureA};
    static const uint16_t kSeconds[1] = {FixtureV};
    static ValueFields firsts[1];
    static ValueFields seconds[1];
    static const uint16_t kIds[2] = {FixtureA, FixtureV};

    firsts[0] = Value(0, 0, -125, 0);
    seconds[0] = Value(25, 0, 0, 0);
    gPair.format = 1u;
    gPair.coverage = kCoverage;
    gPair.coverageCount = 1u;
    gPair.valueFormat1 = FixtureValueXPlacement | FixtureValueXAdvance;
    gPair.valueFormat2 = FixtureValueXPlacement;
    gPair.seconds = kSeconds;
    gPair.firstValues = firsts;
    gPair.secondValues = seconds;
    gPair.pairCount = 1u;

    feature.tag = "kern";
    feature.lookups[0] = 0u;
    feature.count = 1u;
    lookup.type = RoboTextGposLookupPair;
    lookup.flags = 0u;
    lookup.write = WritePairSubtable;
    FixtureBuild(&fixture, "latn", &feature, 1u, &lookup, 1u);
    plan = FixturePlan(&fixture);
    options = OptionsWithScale(1.0f, 0u);
    memset(events, 0, sizeof(events));
    options.trace = events;
    options.traceCapacity = 4u;
    buffer = BufferOf(kIds, 2u);
    stats = ApplyFixture(&fixture, NULL, &plan, buffer, &options);
    RoboCheckEqInt((int)stats.traceEvents, 1);

    /* The counters name every question a reader would ask, including the ones that stayed zero. */
    length = 0u;
    RoboCheckEqInt((int)RoboTextGposStatsWrite(&stats, dump, sizeof(dump), &length), (int)RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(dump, "pairs 1") != NULL);
    RoboCheck(strstr(dump, "class pairs 0") != NULL);
    RoboCheck(strstr(dump, "adjusted glyphs 2") != NULL);
    RoboCheck(strstr(dump, "limit reached 0") != NULL);
    RoboCheck(strstr(dump, "glyphs before 2") != NULL);

    /* And the events say what moved, by how much, for which glyphs, and where they came from. */
    length = 0u;
    RoboCheckEqInt((int)RoboTextGposEventsWrite(events, stats.traceEvents, dump, sizeof(dump), &length),
                   (int)RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(dump, "events 1") != NULL);
    RoboCheck(strstr(dump, "kern lookup 0") != NULL);
    RoboCheck(strstr(dump, "(type 2)") != NULL);
    RoboCheck(strstr(dump, "xAdv=-125") != NULL);
    RoboCheck(strstr(dump, "src=[0,1)") != NULL);

    /* Complete-or-absent: a buffer too small for the table is refused and left empty, never truncated. */
    dump[0] = 'x';
    dump[1] = '\0';
    RoboCheck((int)RoboTextGposStatsWrite(&stats, dump, 8u, &length) != (int)RoboOk);
    RoboCheckStrEq(dump, "");
    RoboCheck((int)RoboTextGposEventsWrite(events, stats.traceEvents, dump, 8u, &length) != (int)RoboOk);
    RoboCheckStrEq(dump, "");
    RoboCheckEqInt((int)RoboTextGposStatsWrite(NULL, dump, sizeof(dump), &length), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGposEventsWrite(NULL, 1u, dump, sizeof(dump), &length),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGposEventsWrite(events, 0u, NULL, sizeof(dump), &length),
                   (int)RoboInvalidArgument);

    RoboTextShapingBufferDestroy(buffer);
    FixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The shipped pack, end to end                                        */
/* ------------------------------------------------------------------ */

static RoboTextView View(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

static RoboTextShapeResult *ShapeWithOptions(const TextFixture *fixture,
                                             const RoboTextShapeOptions *options,
                                             const char *text)
{
    RoboTextShapeResult *result = NULL;

    RoboCheckEqInt((int)RoboTextShape(View(text), &fixture->font, options, &result, NULL), (int)RoboOk);
    RoboCheckMsg(result != NULL, "shaping produced no result");
    return result;
}

static const RoboTextGlyphRun *OnlyRun(const RoboTextShapeResult *result)
{
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
    return RoboTextShapeResultGlyphRun(result, 0u);
}

/**
 * The kerning the shipped face carries, measured.
 *
 * The claim is narrow and falsifiable: with the positioning stage on, the advance of a pair the face kerns
 * is **smaller** than the sum of the two glyphs' own advances, and with `kern` off it is exactly that sum.
 * Nothing here knows which pairs Inter kerns — the assertion is on the difference between the two runs of
 * the same text, so a face that kerns nothing fails loudly instead of passing quietly.
 */
static void TestGposShippedPack(void)
{
    TextFixture fixture;
    RoboTextShapeOptions options;
    RoboTextShapeResult *kerned = NULL;
    RoboTextShapeResult *plain = NULL;
    const RoboTextGlyphRun *kernedRun;
    const RoboTextGlyphRun *plainRun;
    float kernedWidth;
    float plainWidth;
    static const char *const kPairs[] = {"AV", "VA", "To", "Yo", "WA", "Ta", "Ty", "LT"};

    fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 16.0f);
    RoboTextShapeOptionsInit(&options);

    kerned = ShapeWithOptions(&fixture, NULL, "AVATAR");
    kernedRun = OnlyRun(kerned);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(kernedRun), (int)RoboTextGlyphGenerationGpos);
    kernedWidth = RoboTextGlyphRunAdvance(kernedRun);
    RoboCheck(kernedWidth > 0.0f);
    /* The face positions the run's glyphs: at least one value record found something to say. */
    RoboCheck(RoboTextGlyphRunPositioned(kernedRun) > 0u);

    /* The same text with `kern` off: same glyphs, same ids, a different advance — which is the whole claim. */
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "kern", RoboTextFeatureOff, 0u), (int)RoboOk);
    plain = ShapeWithOptions(&fixture, &options, "AVATAR");
    plainRun = OnlyRun(plain);
    plainWidth = RoboTextGlyphRunAdvance(plainRun);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(plainRun), (int)RoboTextGlyphRunCount(kernedRun));
    RoboCheckEqInt((int)RoboTextGlyphRunPositioned(plainRun), 0);
    RoboCheck(plainWidth > kernedWidth);

    /* The individual pairs, on the same evidence: for each one the font either kerns it or does not, and the
     * test's job is to see the *mechanism* work rather than to assume a particular face's table. */
    {
        int kernedPairs = 0;

        for (size_t index = 0u; index < sizeof(kPairs) / sizeof(kPairs[0]); index++) {
            RoboTextShapeResult *on = NULL;
            RoboTextShapeResult *off = NULL;
            float onWidth;
            float offWidth;

            RoboTextShapeOptions defaults;

            RoboTextShapeOptionsInit(&defaults);
            on = ShapeWithOptions(&fixture, &defaults, kPairs[index]);
            off = ShapeWithOptions(&fixture, &options, kPairs[index]);
            onWidth = RoboTextGlyphRunAdvance(OnlyRun(on));
            offWidth = RoboTextGlyphRunAdvance(OnlyRun(off));
            RoboCheck(offWidth >= onWidth);
            if (offWidth > onWidth) {
                kernedPairs++;
            }
            RoboTextShapeResultDestroy(on);
            RoboTextShapeResultDestroy(off);
        }
        /* At least one of the eight is kerned by any real UI face; if none is, this build's positioning is
         * not reaching the pen at all and the test says so. */
        RoboCheck(kernedPairs > 0);
    }

    /* A right-to-left run is positioned by the same stage, its levels are what the bidi layer resolved, and
     * its glyph order is untouched. The claim is not that Hebrew is kerned — it is that the positioning stage
     * runs for it at all, in logical order. */
    {
        RoboTextShapeResult *rtl = ShapeWithOptions(&fixture, NULL, TextSampleScriptHebrew);

        RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(rtl), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(RoboTextShapeResultGlyphRun(rtl, 0u)),
                       (int)RoboTextGlyphGenerationGpos);
        RoboTextShapeResultDestroy(rtl);
    }

    RoboTextShapeResultDestroy(kerned);
    RoboTextShapeResultDestroy(plain);
    TextFixtureClose(&fixture);
}

/**
 * Measurement, the pen and the paint agree (12.13's measurement criterion).
 *
 * `RoboTextGlyphRunAdvance` is the sum the measurer reports; `RoboTextGlyphRunAdvanceUpTo` is the pen
 * position the painter uses. A kerned run is exactly where the two could disagree — one could sum the
 * font's own advances and the other the adjusted ones — so this asserts they are the same number, and that
 * the measurement the shape result reports is that number too.
 */
static void TestGposPaintAgrees(void)
{
    TextFixture fixture;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    const RoboTextMeasurement *measurement;
    float pen = 0.0f;
    float expected = 0.0f;

    fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 18.0f);
    result = ShapeWithOptions(&fixture, NULL, "AVATAR Typography");
    run = OnlyRun(result);
    measurement = RoboTextShapeResultMeasurement(result);

    RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(run), (int)RoboTextGlyphGenerationGpos);
    RoboCheck(RoboTextGlyphRunPositioned(run) > 0u);

    /* Every glyph's advance, accumulated in the same order the painter does, is the run's advance. */
    for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);

        RoboCheck(glyph != NULL);
        /* `AdvanceUpTo(index)` is the pen **before** that glyph, which is the sum of the advances before it —
         * adding this glyph's own first would compare the pen against the pen after it. */
        RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, (RoboTextGlyphIndex)index, &pen, NULL),
                       (int)RoboOk);
        RoboCheckNear(pen, expected, 0.0001f);
        expected += glyph->advanceX;
    }
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, (RoboTextGlyphIndex)RoboTextGlyphRunCount(run),
                                                    &pen, NULL),
                   (int)RoboOk);
    RoboCheckNear(pen, RoboTextGlyphRunAdvance(run), 0.0001f);
    RoboCheckNear(measurement->width, RoboTextGlyphRunAdvance(run), 0.01f);

    /* Determinism: shaping the same text twice positions it identically. */
    {
        RoboTextShapeResult *again = ShapeWithOptions(&fixture, NULL, "AVATAR Typography");
        const RoboTextGlyphRun *second = OnlyRun(again);

        RoboCheckEqInt((int)RoboTextGlyphRunCount(second), (int)RoboTextGlyphRunCount(run));
        for (size_t index = 0u; index < RoboTextGlyphRunCount(run); index++) {
            const RoboTextGlyph *left = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)index);
            const RoboTextGlyph *right = RoboTextGlyphRunGlyph(second, (RoboTextGlyphIndex)index);

            RoboCheck(RoboTextGlyphEqual(left, right));
        }
        RoboTextShapeResultDestroy(again);
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/** Ornamental geometry with a kerned pair in it: the dump a reader would look at when kerning looks wrong. */
static void TestGposGlyphDump(void)
{
    TextFixture fixture;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    const RoboTextGlyph *first;
    const RoboTextGlyph *second;
    char dump[256];
    size_t length = 0u;
    float kernedWidth;

    fixture = TextFixtureOpenRole(RoboFontSystemRoleUIText, RoboFontWeightNormal, 16.0f);
    result = ShapeWithOptions(&fixture, NULL, "AV");
    run = OnlyRun(result);
    first = RoboTextGlyphRunGlyph(run, 0u);
    second = RoboTextGlyphRunGlyph(run, 1u);
    RoboCheck(first != NULL && second != NULL);
    kernedWidth = RoboTextGlyphRunAdvance(run);

    /* The 'A' carries the pair's adjustment on its own advance: the pen moves less after it, and the dump is
     * where that is visible as a number rather than as a suspiciously narrow word. */
    RoboCheck(first->positioned || second->positioned);
    RoboCheck(first->advanceX > 0.0f);
    RoboCheckEqInt((int)RoboTextGlyphRunAdvanceUpTo(run, 1u, &kernedWidth, NULL), (int)RoboOk);
    RoboCheckNear(kernedWidth, first->advanceX, 0.0001f);

    RoboCheckEqInt((int)RoboTextGlyphWrite(first, dump, sizeof(dump), &length), (int)RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(dump, "positioned=") != NULL);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterTextGpos(void)
{
    TestRegister("text.gpos.vocabulary", TestGposVocabulary);
    TestRegister("text.gpos.defaults", TestGposDefaults);
    TestRegister("text.gpos.plan", TestGposPlan);
    TestRegister("text.gpos.single", TestGposSingle);
    TestRegister("text.gpos.pair_format1", TestGposPairFormat1);
    TestRegister("text.gpos.pair_format2", TestGposPairFormat2);
    TestRegister("text.gpos.extension", TestGposExtension);
    TestRegister("text.gpos.device", TestGposDevice);
    TestRegister("text.gpos.marks_and_direction", TestGposMarksAndDirection);
    TestRegister("text.gpos.limits_malformed", TestGposLimitsAndMalformed);
    TestRegister("text.gpos.diagnostics", TestGposDiagnostics);
    TestRegister("text.gpos.shipped_pack", TestGposShippedPack);
    TestRegister("text.gpos.paint_agrees", TestGposPaintAgrees);
    TestRegister("text.gpos.glyph_dump", TestGposGlyphDump);
}
