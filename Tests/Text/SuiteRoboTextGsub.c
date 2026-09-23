/*
 * RoboText's glyph substitution (Phase 12.10) — `text.gsub.*`.
 *
 * The fixture is **built in this file**, byte by byte, the way the RoboFont suites build theirs: the repo's
 * corpus policy prefers a synthetic builder in C over a checked-in blob, and GSUB is exactly the case that
 * policy is for — a real font's substitution data is a moving target, while this suite needs to know that
 * `f f i` becomes one glyph because the *bytes* say so.
 *
 * The fixture font carries nine lookups, one per shape the engine can read (and the extension wrapper's
 * inner subtable shared with the plain single lookup, which is legal and is what a real font does):
 *
 *   | # | type        | what it does                                              | feature |
 *   |---|-------------|-----------------------------------------------------------|---------|
 *   | 0 | single      | `B` -> `B.alt`                                            | tst1   |
 *   | 1 | multiple    | `X` -> `B C`                                              | ccmp    |
 *   | 2 | alternate   | `A` -> {`A.alt1`, `A.alt2`}                               | salt    |
 *   | 3 | ligature    | `f f i` -> `ffi`, `f f f i` -> `fffi` (longest first)     | liga    |
 *   | 4 | context     | `B C` -> `B.alt C` (nested lookup 0)                      | calt    |
 *   | 5 | chain       | `D B C` -> `D B.alt C`                                    | tst2   |
 *   | 6 | reverse     | `C D` -> `X D`                                            | tst3   |
 *   | 7 | extension   | wraps lookup 0's subtable                                 | tst4   |
 *   | 8 | context     | lookup 4's subtable, `IgnoreMarks` set                    | tst6   |
 *
 * Three things are deliberate rather than incidental. The script list has **two** records — `latn` with the
 * whole feature list and `DFLT` with only `liga` — so the same font proves both the script lookup and the
 * `DFLT` fallback. The `latn` record has a `TRK ` language system listing only `tst1`, so a Turkish run
 * proves the language system is *chosen* rather than ignored (its text does not ligate). And lookup 8 shares
 * lookup 4's subtable bytes with a different flag, which is what makes the mark test decisive: the same font
 * data, the same text, two lookups, two answers.
 *
 * The glyphs are placeholders: this fixture is about which glyph id comes out, not about what it looks like.
 * Advances are per-glyph and distinct, so the tests can prove the run's advance came from the *substitute's*
 * own `hmtx` entry rather than from the glyph it replaced.
 */

#include "RoboTextTestSupport.h"

#include "RoboFont/RoboFont.h"
#include "RoboText/RoboTextGsub.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The glyphs                                                          */
/* ------------------------------------------------------------------ */

enum {
    FixtureNotdef = 0,
    FixtureF = 1,
    FixtureI = 2,
    FixtureFfi = 3,
    FixtureA = 4,
    FixtureAAlt1 = 5,
    FixtureAAlt2 = 6,
    FixtureB = 7,
    FixtureC = 8,
    FixtureX = 9,
    FixtureD = 10,
    FixtureBAlt = 12,
    FixtureM = 13,
    FixtureFffi = 14,
    FixtureGlyphCount = 15
};

/** The advance each glyph carries, in font units: `0x100 + glyph id`, so a wrong id is a wrong number. */
static uint16_t FixtureAdvance(uint16_t glyphId)
{
    return (uint16_t)(0x100u + glyphId);
}

/* ------------------------------------------------------------------ */
/* Byte helpers                                                        */
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

/** Reserve @p bytes zeroed and answer the offset they begin at. */
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

/** Pad @p w to a four-byte boundary; the offsets this test writes are table-relative, so this is layout. */
static void Wpad4(ByteWriter *w)
{
    while ((w->length % 4u) != 0u) {
        (void)Wu8At(w, 0u);
    }
}

static uint32_t FixtureTag(const char *tag)
{
    return RoboTextGsubTagOf(tag);
}

/* ------------------------------------------------------------------ */
/* Coverage (format 1: a sorted glyph array)                           */
/* ------------------------------------------------------------------ */

/** @return the offset @p coverage begins at, relative to @p base. */
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

/* ------------------------------------------------------------------ */
/* The GSUB table                                                      */
/* ------------------------------------------------------------------ */

#define FixtureFeatureCount 10u
#define FixtureLookupCount 10u

/* Subtable starts, recorded while building so the extension wrapper can point at the single's. */
typedef struct FixtureLayout {
    size_t singleSubtable;
    size_t multipleSubtable;
    size_t contextSubtable;
    size_t extensionSubtable;
} FixtureLayout;

/** One Feature table: a tag's lookup list. @return its offset, relative to @p base. */
static uint16_t Wfeature(ByteWriter *w, size_t base, const uint16_t *lookups, size_t count)
{
    size_t start = w->length;

    (void)Wu16At(w, 0u); /* featureParams */
    (void)Wu16At(w, (uint16_t)count);
    for (size_t index = 0u; index < count; index++) {
        (void)Wu16At(w, lookups[index]);
    }
    return (uint16_t)(start - base);
}

/** One LangSys: a feature index list, no required feature. @return its offset, relative to @p base. */
static uint16_t WlangSys(ByteWriter *w, size_t base, const uint16_t *features, size_t count)
{
    size_t start = w->length;

    (void)Wu16At(w, 0u);            /* lookupOrder, reserved */
    (void)Wu16At(w, 0xFFFFu);       /* requiredFeatureIndex: none */
    (void)Wu16At(w, (uint16_t)count);
    for (size_t index = 0u; index < count; index++) {
        (void)Wu16At(w, features[index]);
    }
    return (uint16_t)(start - base);
}

/**
 * Build the fixture's GSUB table into @p w.
 *
 * The layout is: header, ScriptList, FeatureList, LookupList, then the subtables in lookup order. Offsets are
 * patched as each child lands, which is why every builder answers the offset it wrote at.
 */
static void BuildGsub(ByteWriter *w, FixtureLayout *layout)
{
    static const uint16_t kLatnFeatures[FixtureFeatureCount] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};
    static const uint16_t kDfltFeatures[1] = {3u};
    static const uint16_t kTrkFeatures[1] = {0u};

    size_t tableStart = w->length;
    size_t scriptList;
    size_t featureList;
    size_t lookupList;
    size_t scriptOffsets;

    /* ---- header ---- */
    (void)Wu32At(w, 0x00010000u);
    scriptList = Wu16At(w, 0u);
    featureList = Wu16At(w, 0u);
    lookupList = Wu16At(w, 0u);

    /* ---- ScriptList: DFLT (liga only) and latn (everything), latn with a TRK language system ---- */
    {
        size_t base = w->length;
        size_t dfltRecord;
        size_t latnRecord;
        uint16_t dfltScript;
        uint16_t latnScript;

        Wu16(w, scriptList, (uint16_t)(base - tableStart));
        (void)Wu16At(w, 2u); /* scriptCount */
        dfltRecord = w->length;
        (void)Wu32At(w, FixtureTag("DFLT"));
        (void)Wu16At(w, 0u); /* scriptOffset: patched below */
        latnRecord = w->length;
        (void)Wu32At(w, FixtureTag("latn"));
        (void)Wu16At(w, 0u); /* scriptOffset: patched below */
        scriptOffsets = w->length;

        /* DFLT: one default language system, no records. */
        {
            size_t script = w->length;
            size_t langSys;

            Wu16(w, dfltRecord + 4u, (uint16_t)(script - base));
            (void)Wu16At(w, 0u); /* defaultLangSysOffset: patched below */
            (void)Wu16At(w, 0u); /* langSysCount */
            langSys = WlangSys(w, script, kDfltFeatures, 1u);
            Wu16(w, script, (uint16_t)langSys);
            dfltScript = (uint16_t)(script - base);
        }
        /* latn: a default language system plus `TRK `. */
        {
            size_t script = w->length;
            size_t langSys;
            size_t record;

            Wu16(w, latnRecord + 4u, (uint16_t)(script - base));
            latnScript = (uint16_t)(script - base);
            (void)Wu16At(w, 0u); /* defaultLangSysOffset: patched below */
            (void)Wu16At(w, 1u); /* langSysCount */
            record = Wreserve(w, 6u);
            langSys = WlangSys(w, script, kLatnFeatures, FixtureFeatureCount);
            Wu16(w, script, (uint16_t)langSys);
            {
                size_t trk = WlangSys(w, script, kTrkFeatures, 1u);

                Wu32(w, record, FixtureTag("TRK "));
                Wu16(w, record + 4u, (uint16_t)trk);
            }
        }
        (void)dfltScript;
        (void)latnScript;
        (void)scriptOffsets;
    }

    /* ---- FeatureList ---- */
    {
        size_t base = w->length;

        Wu16(w, featureList, (uint16_t)(base - tableStart));
        (void)Wu16At(w, FixtureFeatureCount);
        (void)Wreserve(w, FixtureFeatureCount * 6u);

        for (uint16_t index = 0u; index < FixtureFeatureCount; index++) {
            static const char *const kTags[FixtureFeatureCount] = {"tst1", "ccmp", "salt", "liga",
                                                                   "calt", "tst2", "tst3", "tst4",
                                                                   "tst6", "tst5"};
            uint16_t feature[1] = {index};
            size_t record = base + 2u + (size_t)index * 6u;
            size_t featureStart = w->length;

            Wu32(w, record, FixtureTag(kTags[index]));
            Wu16(w, record + 4u, (uint16_t)(featureStart - base));
            (void)Wfeature(w, base, feature, 1u);
        }
    }

    /* ---- LookupList ---- */
    {
        size_t base = w->length;
        size_t subtableOffsets;

        Wu16(w, lookupList, (uint16_t)(base - tableStart));
        (void)Wu16At(w, FixtureLookupCount);
        subtableOffsets = Wreserve(w, FixtureLookupCount * 2u);

        for (uint16_t index = 0u; index < FixtureLookupCount; index++) {
            uint16_t type;
            uint16_t flag;
            size_t lookupStart = w->length;
            size_t subtable = 0u;

            switch (index) {
                case 4:
                case 8:
                    /* The context lookup, and its `IgnoreMarks` twin: `B C` with a mark between. */
                    type = 5u;
                    flag = (index == 8u) ? 0x0008u : 0u;
                    break;
                case 5:
                    type = 6u;
                    flag = 0u;
                    break;
                case 6:
                    type = 8u;
                    flag = 0u;
                    break;
                case 7:
                    type = 7u; /* Extension */
                    flag = 0u;
                    break;
                case 9:
                    type = 1u; /* the second Single, in the *delta* format */
                    flag = 0u;
                    break;
                default:
                    type = (uint16_t)(index + 1u);
                    flag = 0u;
                    break;
            }
            (void)Wu16At(w, type);
            (void)Wu16At(w, flag);
            (void)Wu16At(w, 1u);
            (void)Wu16At(w, 0u); /* subtableOffset: patched below */

            switch (index) {
                case 0: {
                    /* Single, format 2: B -> B.alt. Also the extension wrapper's payload. */
                    static const uint16_t kCoverage[1] = {FixtureB};
                    size_t start = w->length;
                    size_t offsets;

                    (void)Wu16At(w, 2u);
                    offsets = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u);
                    (void)Wu16At(w, FixtureBAlt);
                    Wu16(w, offsets, Wcoverage1(w, start, kCoverage, 1u));
                    subtable = start;
                    layout->singleSubtable = start;
                    break;
                }
                case 1: {
                    /* Multiple: X -> B C. */
                    static const uint16_t kCoverage[1] = {FixtureX};
                    size_t start = w->length;
                    size_t coverageOffset;
                    size_t sequenceOffset;
                    size_t sequence;

                    (void)Wu16At(w, 1u);
                    coverageOffset = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u);
                    sequenceOffset = Wu16At(w, 0u);
                    /* The sequence is where the writer *is*, not where it ends up: the coverage table is
                     * appended below and would otherwise push this offset past the sequences. */
                    sequence = w->length;
                    (void)Wu16At(w, 2u);
                    (void)Wu16At(w, FixtureB);
                    (void)Wu16At(w, FixtureC);
                    Wu16(w, sequenceOffset, (uint16_t)(sequence - start));
                    Wu16(w, coverageOffset, Wcoverage1(w, start, kCoverage, 1u));
                    subtable = start;
                    layout->multipleSubtable = start;
                    break;
                }
                case 2: {
                    /* Alternate: A -> {A.alt1, A.alt2}. */
                    static const uint16_t kCoverage[1] = {FixtureA};
                    size_t start = w->length;
                    size_t coverageOffset;
                    size_t setOffset;
                    size_t set;

                    (void)Wu16At(w, 1u);
                    coverageOffset = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u);
                    setOffset = Wu16At(w, 0u);
                    set = w->length;
                    (void)Wu16At(w, 2u);
                    (void)Wu16At(w, FixtureAAlt1);
                    (void)Wu16At(w, FixtureAAlt2);
                    Wu16(w, coverageOffset, Wcoverage1(w, start, kCoverage, 1u));
                    Wu16(w, setOffset, (uint16_t)(set - start));
                    subtable = start;
                    break;
                }
                case 3: {
                    /*
                     * Ligature: `f f i` -> `ffi` and `f f f i` -> `fffi`, in one set. The four-glyph
                     * candidate is listed **first** because a ligature set is resolved in the order the font
                     * states it — which is how a font says "prefer the longest match" without the engine
                     * knowing anything about `ffi`. Ordering the other way is what makes `fffi` come out as
                     * `f` + `ffi` instead of one glyph, and that difference is the test.
                     */
                    static const uint16_t kCoverage[1] = {FixtureF};
                    size_t start = w->length;
                    size_t coverageOffset;
                    size_t setOffset;
                    size_t set;
                    size_t four;
                    size_t three;

                    (void)Wu16At(w, 1u);
                    coverageOffset = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u);
                    setOffset = Wu16At(w, 0u);
                    set = w->length;
                    (void)Wu16At(w, 2u); /* ligatureCount */
                    four = Wu16At(w, 0u);  /* ligatureOffsets[0]: patched below */
                    three = Wu16At(w, 0u); /* ligatureOffsets[1]: patched below */
                    {
                        size_t ligature = w->length;

                        (void)Wu16At(w, FixtureFffi);
                        (void)Wu16At(w, 4u); /* componentCount, the covered glyph included */
                        (void)Wu16At(w, FixtureF);
                        (void)Wu16At(w, FixtureF);
                        (void)Wu16At(w, FixtureI);
                        Wu16(w, four, (uint16_t)(ligature - set));
                    }
                    {
                        size_t ligature = w->length;

                        (void)Wu16At(w, FixtureFfi);
                        (void)Wu16At(w, 3u);
                        (void)Wu16At(w, FixtureF);
                        (void)Wu16At(w, FixtureI);
                        Wu16(w, three, (uint16_t)(ligature - set));
                    }
                    Wu16(w, coverageOffset, Wcoverage1(w, start, kCoverage, 1u));
                    Wu16(w, setOffset, (uint16_t)(set - start));
                    subtable = start;
                    break;
                }
                case 4:
                case 8: {
                    /*
                     * Context, format 3: input [`B`, `C`] with `B` -> `B.alt` through lookup 0. The two
                     * lookups share these bytes; only their flags differ.
                     */
                    static const uint16_t kFirst[1] = {FixtureB};
                    static const uint16_t kSecond[1] = {FixtureC};
                    size_t start = w->length;
                    size_t coverages;

                    (void)Wu16At(w, 3u);
                    (void)Wu16At(w, 2u); /* glyphCount */
                    (void)Wu16At(w, 1u); /* lookupRecordCount */
                    /* The spec's field order: a coverage per input glyph, *then* the lookup records. */
                    coverages = Wreserve(w, 4u);
                    (void)Wu16At(w, 0u); /* SequenceLookupRecord.sequenceIndex */
                    (void)Wu16At(w, 0u); /* SequenceLookupRecord.lookupListIndex */
                    Wu16(w, coverages, Wcoverage1(w, start, kFirst, 1u));
                    Wu16(w, coverages + 2u, Wcoverage1(w, start, kSecond, 1u));
                    subtable = start;
                    layout->contextSubtable = start;
                    break;
                }
                case 5: {
                    /* Chained context, format 1: `D` before, `C` after, `B` -> `B.alt`. */
                    static const uint16_t kCoverage[1] = {FixtureB};
                    size_t start = w->length;
                    size_t coverageOffset;
                    size_t setOffset;
                    size_t set;
                    size_t rule;

                    (void)Wu16At(w, 1u);
                    coverageOffset = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u); /* chainRuleSetCount */
                    setOffset = Wu16At(w, 0u);
                    set = w->length;
                    (void)Wu16At(w, 1u); /* chainRuleCount */
                    (void)Wu16At(w, 0u); /* ruleOffset: patched below */
                    rule = w->length;
                    (void)Wu16At(w, 1u); /* backtrackGlyphCount */
                    (void)Wu16At(w, FixtureD);
                    (void)Wu16At(w, 1u); /* inputGlyphCount, the covered glyph included */
                    (void)Wu16At(w, 1u); /* lookaheadGlyphCount */
                    (void)Wu16At(w, FixtureC);
                    (void)Wu16At(w, 1u); /* seqLookupCount */
                    (void)Wu16At(w, 0u); /* sequenceIndex */
                    (void)Wu16At(w, 0u); /* lookupListIndex */
                    Wu16(w, set + 2u, (uint16_t)(rule - set));
                    Wu16(w, coverageOffset, Wcoverage1(w, start, kCoverage, 1u));
                    Wu16(w, setOffset, (uint16_t)(set - start));
                    subtable = start;
                    break;
                }
                case 6: {
                    /* Reverse chaining: `C` with `D` after becomes `X`. */
                    static const uint16_t kCoverage[1] = {FixtureC};
                    static const uint16_t kLookahead[1] = {FixtureD};
                    size_t start = w->length;
                    size_t coverageOffset;
                    size_t lookahead;

                    (void)Wu16At(w, 1u);
                    coverageOffset = Wu16At(w, 0u);
                    (void)Wu16At(w, 0u); /* backtrackGlyphCount */
                    (void)Wu16At(w, 1u); /* lookaheadGlyphCount */
                    lookahead = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u); /* glyphCount */
                    (void)Wu16At(w, FixtureX);
                    Wu16(w, lookahead, Wcoverage1(w, start, kLookahead, 1u));
                    Wu16(w, coverageOffset, Wcoverage1(w, start, kCoverage, 1u));
                    subtable = start;
                    break;
                }
                case 7: {
                    /*
                     * Extension, format 1: a wrapper whose payload — a plain single substitution — sits
                     * *after* it. That order is the point: every OpenType offset is unsigned and measured
                     * from the table it lives in, so a payload written *before* the wrapper could not be
                     * expressed at all. Lookup 0's subtable is the same shape; this one is self-contained so
                     * the wrapper's `uint32` offset has a real forward value to carry.
                     */
                    static const uint16_t kCoverage[1] = {FixtureB};
                    size_t start = w->length;
                    size_t extensionOffset;
                    size_t payload;
                    size_t offsets;

                    (void)Wu16At(w, 1u); /* format */
                    (void)Wu16At(w, 1u); /* the real lookup type: single */
                    extensionOffset = Wu32At(w, 0u); /* patched below, once the payload has landed */
                    payload = w->length;
                    Wu32(w, extensionOffset, (uint32_t)(payload - start));
                    (void)Wu16At(w, 2u); /* the payload: single, format 2 */
                    offsets = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u);
                    (void)Wu16At(w, FixtureBAlt);
                    Wu16(w, offsets, Wcoverage1(w, payload, kCoverage, 1u));
                    subtable = start;
                    layout->extensionSubtable = start;
                    break;
                }
                case 9: {
                    /*
                     * Single, format 1: one signed delta for the whole coverage — `A` + 1 = `A.alt1`.
                     * Format 2 is the other single substitution and lookup 0 carries it; this one exists
                     * because format 1's delta is the shape a font uses when a covered run shifts by a
                     * constant, and a reader that gets its signedness wrong is invisible otherwise.
                     */
                    static const uint16_t kCoverage[1] = {FixtureA};
                    size_t start = w->length;
                    size_t coverageOffset;

                    (void)Wu16At(w, 1u); /* format */
                    coverageOffset = Wu16At(w, 0u);
                    (void)Wu16At(w, 1u); /* delta: +1, signed */
                    Wu16(w, coverageOffset, Wcoverage1(w, start, kCoverage, 1u));
                    subtable = start;
                    break;
                }
                default:
                    break;
            }

            /* A Lookup's subtable offsets are relative to the *Lookup* table, which is the one offset in
             * this table that is not relative to the subtable or the list. */
            Wu16(w, lookupStart + 6u, (uint16_t)(subtable - lookupStart));
            Wu16(w, subtableOffsets + (size_t)index * 2u, (uint16_t)(lookupStart - base));
        }

    }
    (void)layout->contextSubtable;
    Wpad4(w);
}

/* ------------------------------------------------------------------ */
/* The rest of the font                                                */
/* ------------------------------------------------------------------ */

static size_t BuildHead(uint8_t *b)
{
    memset(b, 0, 54u);
    b[0] = 0x00;
    b[1] = 0x01;
    b[2] = 0x00;
    b[3] = 0x00; /* version 1.0 */
    b[12] = 0x5F;
    b[13] = 0x0F;
    b[14] = 0x3C;
    b[15] = 0xF5; /* magicNumber, which a validator checks */
    b[18] = 0x03;
    b[19] = 0xE8; /* unitsPerEm 1000 */
    /* indexToLocFormat 0 (short); numberOfHMetrics lives in hhea. */
    return 54u;
}

static size_t BuildHhea(uint8_t *b)
{
    memset(b, 0, 36u);
    b[0] = 0x00;
    b[1] = 0x01;
    b[2] = 0x00;
    b[3] = 0x00;
    b[4] = 0x03;
    b[5] = 0x20; /* ascender 800 */
    b[6] = 0xFD;
    b[7] = 0xA8; /* descender -600 */
    b[8] = 0x00;
    b[9] = 0x00; /* lineGap */
    /* advanceWidthMax, min/max lsb, xMaxExtent, caret slope, reserved… all zero */
    b[34] = (uint8_t)(FixtureGlyphCount >> 8);
    b[35] = (uint8_t)FixtureGlyphCount; /* numberOfHMetrics */
    return 36u;
}

static size_t BuildMaxp(uint8_t *b)
{
    memset(b, 0, 32u);
    b[0] = 0x00;
    b[1] = 0x01;
    b[2] = 0x00;
    b[3] = 0x00;
    b[4] = (uint8_t)(FixtureGlyphCount >> 8);
    b[5] = (uint8_t)FixtureGlyphCount;
    return 32u;
}

static size_t BuildHmtx(uint8_t *b)
{
    size_t at = 0u;

    for (uint16_t glyph = 0u; glyph < FixtureGlyphCount; glyph++) {
        uint16_t advance = FixtureAdvance(glyph);

        b[at++] = (uint8_t)(advance >> 8);
        b[at++] = (uint8_t)advance;
        b[at++] = 0x00;
        b[at++] = 0x00; /* leftSideBearing */
    }
    return at;
}

/** `post` version 3.0: a sixteen-byte header and five zeroed `uint32` fields. */
static size_t BuildPost(uint8_t *b)
{
    memset(b, 0, 32u);
    b[0] = 0x00;
    b[1] = 0x03;
    b[2] = 0x00;
    b[3] = 0x00; /* version 3.0 */
    b[8] = 0xFF;
    b[9] = 0x83; /* underlinePosition -125 */
    b[10] = 0x00;
    b[11] = 0x32; /* underlineThickness 50 */
    return 32u;
}

/** `OS/2` version 4: the line metrics and the two character indices, everything else zeroed. */
static size_t BuildOs2(uint8_t *b)
{
    memset(b, 0, 96u);
    b[0] = 0x00;
    b[1] = 0x04; /* version 4 */
    b[4] = 0x01;
    b[5] = 0x90; /* usWeightClass 400 */
    b[6] = 0x00;
    b[7] = 0x05; /* usWidthClass 5 */
    b[62] = 0x03;
    b[63] = 0x20; /* sTypoAscender 800 */
    b[64] = 0xFD;
    b[65] = 0xA8; /* sTypoDescender -600 */
    b[68] = 0x03;
    b[69] = 0x20; /* usWinAscent 800 */
    b[70] = 0x02;
    b[71] = 0x58; /* usWinDescent 600 */
    b[86] = 0x00;
    b[87] = 0x20; /* usDefaultChar: space */
    b[88] = 0x00;
    b[89] = 0x3F; /* usBreakChar: question mark, the usual choice */
    return 96u;
}

/** One cmap format 6 subtable over `U+0020`..`U+007F`, with the fixture's eight mappings. */
static size_t BuildCmap6(uint8_t *b)
{
    const uint16_t first = 0x0020u;
    const uint16_t count = 0x0060u;
    uint16_t entryCount = 0u;

    memset(b, 0, 10u + 2u * count);
    b[0] = 0x00;
    b[1] = 0x06; /* format 6 */
    {
        uint16_t length = (uint16_t)(10u + 2u * count);

        b[2] = (uint8_t)(length >> 8);
        b[3] = (uint8_t)length;
    }
    b[6] = (uint8_t)(first >> 8);
    b[7] = (uint8_t)first;
    b[8] = (uint8_t)(count >> 8);
    b[9] = (uint8_t)count;
    {
        struct {
            char scalar;
            uint16_t glyph;
        } kMap[] = {{'f', FixtureF}, {'i', FixtureI}, {'A', FixtureA}, {'B', FixtureB},
                    {'C', FixtureC}, {'X', FixtureX}, {'D', FixtureD}, {'m', FixtureM}};

        for (size_t index = 0u; index < sizeof(kMap) / sizeof(kMap[0]); index++) {
            uint16_t slot = (uint16_t)((uint8_t)kMap[index].scalar - first);

            b[10u + (size_t)slot * 2u] = (uint8_t)(kMap[index].glyph >> 8);
            b[11u + (size_t)slot * 2u] = (uint8_t)kMap[index].glyph;
            entryCount++;
        }
    }
    (void)entryCount;
    return 10u + 2u * (size_t)count;
}

/**
 * A cmap table: one format 6 subtable for platform 3 / encoding 1, plus a `(0, 3)` record pointing at the
 * same bytes so the face cannot complain about a missing Unicode platform.
 */
static size_t BuildCmap(uint8_t *b, const uint8_t *subtable, size_t subtableLength)
{
    size_t at = 0u;

    b[0] = 0x00;
    b[1] = 0x00; /* version */
    b[2] = 0x00;
    b[3] = 0x02; /* numTables */
    at = 4u;
    b[at + 0u] = 0x00;
    b[at + 1u] = 0x00; /* platform 0 */
    b[at + 2u] = 0x00;
    b[at + 3u] = 0x03; /* Unicode 2.0 full */
    b[at + 4u] = 0x00;
    b[at + 5u] = 0x00;
    b[at + 6u] = 0x00;
    b[at + 7u] = 0x14u; /* offset to the subtable: after both records */
    at += 8u;
    b[at + 0u] = 0x00;
    b[at + 1u] = 0x03; /* platform 3 */
    b[at + 2u] = 0x00;
    b[at + 3u] = 0x01; /* encoding 1 */
    b[at + 4u] = 0x00;
    b[at + 5u] = 0x00;
    b[at + 6u] = 0x00;
    b[at + 7u] = 0x14u;
    at += 8u;
    memcpy(b + at, subtable, subtableLength);
    return at + subtableLength;
}

/** GDEF 1.0 with a glyph class definition that makes `m` a mark (class 3) and nothing else. */
static size_t BuildGdef(uint8_t *b)
{
    size_t classDef = 12u;

    memset(b, 0, 32u);
    b[0] = 0x00;
    b[1] = 0x01;
    b[2] = 0x00;
    b[3] = 0x00; /* version 1.0 */
    b[4] = 0x00;
    b[5] = 0x0C; /* glyphClassDefOffset */
    b[6] = 0x00;
    b[7] = 0x00; /* attachList */
    b[8] = 0x00;
    b[9] = 0x00; /* ligCaretList */
    b[10] = 0x00;
    b[11] = 0x00; /* markAttachClassDef */
    b[classDef] = 0x00;
    b[classDef + 1u] = 0x01u; /* format 1 */
    b[classDef + 2u] = (uint8_t)(FixtureM >> 8);
    b[classDef + 3u] = (uint8_t)FixtureM;
    b[classDef + 4u] = 0x00;
    b[classDef + 5u] = 0x01u; /* glyphCount */
    b[classDef + 6u] = 0x00;
    b[classDef + 7u] = 0x03u; /* class 3: mark */
    return classDef + 8u;
}

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} SynthTable;

static size_t BuildSfnt(uint8_t *out,
                        size_t outCap,
                        const SynthTable *tables,
                        uint32_t count)
{
    size_t directoryEnd = 12u + (size_t)count * 16u;
    size_t need = directoryEnd;
    size_t cursor;
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;

    for (uint32_t index = 0u; index < count; index++) {
        need += ((size_t)tables[index].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        RoboCheck(0);
        return 0u;
    }
    memset(out, 0, need);
    while ((uint16_t)(pow2 * 2u) <= (uint16_t)count) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    {
        ByteWriter header;

        header.data = out;
        header.capacity = need;
        header.length = 0u;
        Wu32(&header, 0u, RoboFontSfntVersionTrue);
        Wu16(&header, 4u, (uint16_t)count);
        Wu16(&header, 6u, (uint16_t)(pow2 * 16u));
        Wu16(&header, 8u, entry);
        Wu16(&header, 10u, (uint16_t)(count * 16u - pow2 * 16u));
    }

    cursor = directoryEnd;
    for (uint32_t index = 0u; index < count; index++) {
        uint8_t *record = out + 12u + (size_t)index * 16u;
        size_t padded = ((size_t)tables[index].length + 3u) & ~(size_t)3u;
        uint32_t checksum;

        if (tables[index].length != 0u && tables[index].data) {
            memcpy(out + cursor, tables[index].data, tables[index].length);
        }
        checksum = FontSfntChecksum(out + cursor, tables[index].length);
        record[0] = (uint8_t)(tables[index].tag >> 24);
        record[1] = (uint8_t)(tables[index].tag >> 16);
        record[2] = (uint8_t)(tables[index].tag >> 8);
        record[3] = (uint8_t)tables[index].tag;
        record[4] = (uint8_t)(checksum >> 24);
        record[5] = (uint8_t)(checksum >> 16);
        record[6] = (uint8_t)(checksum >> 8);
        record[7] = (uint8_t)checksum;
        record[8] = (uint8_t)(cursor >> 24);
        record[9] = (uint8_t)(cursor >> 16);
        record[10] = (uint8_t)(cursor >> 8);
        record[11] = (uint8_t)cursor;
        record[12] = (uint8_t)(tables[index].length >> 24);
        record[13] = (uint8_t)(tables[index].length >> 16);
        record[14] = (uint8_t)(tables[index].length >> 8);
        record[15] = (uint8_t)tables[index].length;
        cursor += padded;
    }
    return cursor;
}

/* ------------------------------------------------------------------ */
/* Opening the fixture                                                 */
/* ------------------------------------------------------------------ */

/** Static because the face borrows the bytes and the tests are single-threaded. */
static uint8_t g_fontBytes[8192];
static size_t g_fontLength;

/** Build the fixture font and answer the GSUB layout it recorded. */
static void FixtureBuildFont(FixtureLayout *layout)
{
    static uint8_t head[54];
    static uint8_t hhea[36];
    static uint8_t maxp[32];
    static uint8_t hmtx[FixtureGlyphCount * 4u];
    static uint8_t cmapSubtable[10u + 2u * 0x60u];
    static uint8_t cmap[256];
    static uint8_t gdef[32];
    static uint8_t post[32];
    static uint8_t os2[96];
    static uint8_t gsub[4096];
    SynthTable tables[9];
    ByteWriter writer;
    size_t cmapLength;
    size_t gdefLength;
    size_t gsubLength;

    memset(layout, 0, sizeof(*layout));
    writer.data = gsub;
    writer.capacity = sizeof(gsub);
    writer.length = 0u;
    BuildGsub(&writer, layout);
    gsubLength = writer.length;
    {
        size_t subtable = BuildCmap6(cmapSubtable);

        cmapLength = BuildCmap(cmap, cmapSubtable, subtable);
    }
    gdefLength = BuildGdef(gdef);
    tables[0].tag = RoboFontTag('h', 'e', 'a', 'd');
    tables[0].data = head;
    tables[0].length = (uint32_t)BuildHead(head);
    tables[1].tag = RoboFontTag('h', 'h', 'e', 'a');
    tables[1].data = hhea;
    tables[1].length = (uint32_t)BuildHhea(hhea);
    tables[2].tag = RoboFontTag('m', 'a', 'x', 'p');
    tables[2].data = maxp;
    tables[2].length = (uint32_t)BuildMaxp(maxp);
    tables[3].tag = RoboFontTag('h', 'm', 't', 'x');
    tables[3].data = hmtx;
    tables[3].length = (uint32_t)BuildHmtx(hmtx);
    tables[4].tag = RoboFontTag('c', 'm', 'a', 'p');
    tables[4].data = cmap;
    tables[4].length = (uint32_t)cmapLength;
    tables[5].tag = RoboFontTag('G', 'D', 'E', 'F');
    tables[5].data = gdef;
    tables[5].length = (uint32_t)gdefLength;
    tables[6].tag = RoboFontTag('G', 'S', 'U', 'B');
    tables[6].data = gsub;
    tables[6].length = (uint32_t)gsubLength;
    tables[7].tag = RoboFontTag('p', 'o', 's', 't');
    tables[7].data = post;
    tables[7].length = (uint32_t)BuildPost(post);
    tables[8].tag = RoboFontTag('O', 'S', '/', '2');
    tables[8].data = os2;
    tables[8].length = (uint32_t)BuildOs2(os2);
    g_fontLength = BuildSfnt(g_fontBytes, sizeof(g_fontBytes), tables, 9u);
    RoboCheck(g_fontLength != 0u);
}

typedef struct GsubFixture {
    FontSource *source;
    FontFace *face;
    RoboTextFontContext font;
} GsubFixture;

/**
 * Open a face over whatever @ref g_fontBytes currently holds. Separate from @ref FixtureOpen so the
 * malformed-table case can patch the bytes *before* the face copies them — `FontSourceCreateMemoryCopy`
 * takes a copy, so a patch applied after the open never reaches the parser.
 */
static void FixtureAdopt(GsubFixture *fixture)
{
    RoboTextFontStyle style;

    fixture->source = NULL;
    RoboCheckEqInt((int)FontSourceCreateMemoryCopy(g_fontBytes, g_fontLength, &fixture->source),
                   (int)RoboOk);
    fixture->face = NULL;
    RoboCheckEqInt((int)FontFaceCreate(fixture->source, 0u, &fixture->face), (int)RoboOk);
    /* A fixture font must be a font: the tables the text layer reads are checked here, once, so that a
     * defect in the builder reads as a builder defect rather than as an engine failure later. */
    RoboCheckEqInt((int)FontFaceParseCmap(fixture->face), (int)RoboOk);
    RoboCheck(FontFaceCmap(fixture->face) != NULL);
    RoboCheckEqInt((int)FontFaceParseMetrics(fixture->face), (int)RoboOk);
    {
        FontCmapLookupResult lookup;

        memset(&lookup, 0, sizeof(lookup));
        RoboCheckEqInt((int)FontFaceLookupGlyph(fixture->face, (uint32_t)'B', &lookup), (int)RoboOk);
        RoboCheckEqInt((int)lookup.status, (int)RoboFontCmapMapped);
        RoboCheckEqInt((int)lookup.glyphId, FixtureB);
    }
    RoboTextFontStyleInit(&style);
    style.sizePx = 16.0f;
    RoboCheckEqInt((int)RoboTextFontContextSet(&fixture->font, fixture->face, &style), (int)RoboOk);
}

static void FixtureOpen(GsubFixture *fixture, FixtureLayout *layout)
{
    FixtureBuildFont(layout);
    FixtureAdopt(fixture);
}

static void FixtureClose(GsubFixture *fixture)
{
    FontFaceRelease(fixture->face);
    fixture->face = NULL;
    FontSourceRelease(fixture->source);
    fixture->source = NULL;
}

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */

/** The plan for @p script with @p features, resolved against @p face's own GSUB. */
static void PlanFor(FontFace *face,
                    RoboTextScript script,
                    const RoboTextLanguage *language,
                    const RoboTextFeatureSet *features,
                    RoboTextGsubPlan *out)
{
    const FontOtLayout *gsub = NULL;

    RoboCheckEqInt((int)FontFaceParseOtLayout(face), (int)RoboOk);
    gsub = FontFaceGsub(face);
    RoboCheck(gsub != NULL);
    RoboCheckEqInt((int)RoboTextGsubPlanResolve(gsub, script, language, features, out, NULL),
                   (int)RoboOk);
    RoboCheck(RoboTextGsubPlanIsValid(out));
}

/** Apply @p plan to the glyph ids of @p text (through the face's cmap) and answer the buffer. */
static RoboTextGsubBuffer *RunPlan(FontFace *face,
                                   const RoboTextGsubPlan *plan,
                                   const char *text,
                                   const RoboTextFeatureSet *features,
                                   RoboTextGsubStats *stats,
                                   RoboTextGsubEvent *trace,
                                   size_t traceCapacity,
                                   RoboTextGsubLimits *limits)
{
    const FontOtLayout *gsub = NULL;
    const FontGdef *gdef = NULL;
    RoboTextGsubBuffer *buffer = NULL;
    RoboTextRunSet *set = NULL;
    RoboTextFontContext font;
    RoboTextFontStyle style;
    RoboTextShapeOptions options;
    RoboTextView view = RoboTextViewMake(text, strlen(text));

    RoboTextFontStyleInit(&style);
    style.sizePx = 16.0f;
    RoboCheckEqInt((int)RoboTextFontContextSet(&font, face, &style), (int)RoboOk);
    RoboTextShapeOptionsInit(&options);
    if (features) {
        options.features = *features;
    }
    RoboCheckEqInt((int)RoboTextRunSetCreate(view, &font, &options, &set, NULL), (int)RoboOk);
    RoboCheck(RoboTextRunSetTextRunCount(set) >= 1u);
    gsub = FontFaceGsub(face);
    gdef = FontFaceGdef(face);

    buffer = RoboTextGsubBufferCreate(16u);
    RoboCheck(buffer != NULL);
    for (RoboTextScalarIndex scalar = 0u; scalar < (RoboTextScalarIndex)strlen(text); scalar++) {
        RoboTextShapingGlyph glyph;
        GlyphId glyphId = RoboGlyphIdNone;
        int mapped = 0;

        /* The ids come from the face's own cmap, one per scalar — the pass this suite's plan rewrites. */
        RoboCheckEqInt((int)RoboTextFontLookupGlyph(&font, (RoboTextCodePoint)text[scalar], &glyphId,
                                                    &mapped, NULL),
                       (int)RoboOk);
        RoboCheck(mapped);
        RoboTextShapingGlyphInit(&glyph);
        glyph.glyphId = glyphId;
        glyph.source = RoboTextRangeMake(scalar, scalar + 1u);
        glyph.cluster = scalar;
        glyph.bidiLevel = 0u;
        RoboCheckEqInt((int)RoboTextGsubBufferAppend(buffer, &glyph), (int)RoboOk);
    }
    {
        RoboTextGsubOptions applyOptions;
        RoboTextGsubStats local;

        memset(&applyOptions, 0, sizeof(applyOptions));
        applyOptions.direction = RoboTextDirectionLtr;
        applyOptions.bidiLevel = 0u;
        applyOptions.trace = trace;
        applyOptions.traceCapacity = traceCapacity;
        applyOptions.limits = limits;
        RoboCheckEqInt((int)RoboTextGsubApply(gsub, gdef, plan, &applyOptions, buffer, &local, NULL),
                       (int)RoboOk);
        if (stats) {
            *stats = local;
        }
    }
    RoboTextRunSetDestroy(set);
    return buffer;
}

/* ------------------------------------------------------------------ */
/* The engine's own vocabulary                                          */
/* ------------------------------------------------------------------ */

static void TestGsubVocabulary(void)
{
    RoboTextGsubPlan plan;
    RoboTextGsubStats stats;
    RoboTextShapingGlyph glyph;
    RoboTextShapingGlyph other;
    char tag[RoboTextFeatureTagBytes];

    RoboCheckEqInt((int)RoboTextGsubTagOf("liga"), (int)0x6C696761u);
    RoboCheckEqInt((int)RoboTextGsubTagOf("li"), 0);
    RoboCheckEqInt((int)RoboTextGsubTagOf("ligaa"), 0);
    RoboCheckEqInt((int)RoboTextGsubTagOf(NULL), 0);
    RoboTextGsubTagWrite(0x6C696761u, tag);
    RoboCheckStrEq(tag, "liga");

    /* The script tag is the ISO 15924 code in lower case, with the registry's five exceptions. */
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptLatin), (int)RoboFontTag('l', 'a', 't', 'n'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptArabic), (int)RoboFontTag('a', 'r', 'a', 'b'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptHebrew), (int)RoboFontTag('h', 'e', 'b', 'r'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptDevanagari),
                   (int)RoboFontTag('d', 'e', 'v', 'a'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptHan), (int)RoboFontTag('h', 'a', 'n', 'i'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptHangul), (int)RoboFontTag('h', 'a', 'n', 'g'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptLao), (int)RoboFontTag('l', 'a', 'o', ' '));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptNko), (int)RoboFontTag('n', 'k', 'o', ' '));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptYi), (int)RoboFontTag('y', 'i', ' ', ' '));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptVai), (int)RoboFontTag('v', 'a', 'i', ' '));
    /* Hiragana shares Katakana's record; Common, Inherited and Unknown ask for `DFLT` (0). */
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptHiragana), (int)RoboFontTag('k', 'a', 'n', 'a'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptKatakana), (int)RoboFontTag('k', 'a', 'n', 'a'));
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptCommon), 0);
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptInherited), 0);
    RoboCheckEqInt((int)RoboTextGsubScriptTag(RoboUScriptUnknown), 0);

    /* A glyph record's own contract: a source is required, and equality is field for field. */
    RoboTextShapingGlyphInit(&glyph);
    RoboCheck(!RoboTextShapingGlyphIsValid(&glyph));
    RoboCheck(RoboTextShapingGlyphIsValid(NULL) == 0);
    glyph.source = RoboTextRangeMake(2u, 4u);
    glyph.cluster = 2u;
    glyph.bidiLevel = 0u;
    RoboCheck(RoboTextShapingGlyphIsValid(&glyph));
    other = glyph;
    RoboCheck(RoboTextShapingGlyphEqual(&glyph, &other));
    other.glyphId = 7u;
    RoboCheck(!RoboTextShapingGlyphEqual(&glyph, &other));
    other = glyph;
    other.source = RoboTextRangeMake(3u, 4u);
    RoboCheck(!RoboTextShapingGlyphEqual(&glyph, &other));
    RoboCheck(RoboTextShapingGlyphEqual(NULL, NULL));

    RoboTextGsubPlanInit(&plan);
    RoboCheck(RoboTextGsubPlanIsValid(&plan));
    RoboCheck(plan.empty);
    RoboCheck(!RoboTextGsubPlanIsValid(NULL));
    RoboTextGsubStatsInit(&stats);
    RoboCheckEqInt((int)stats.glyphsBefore, 0);
    RoboTextGsubStatsInit(NULL); /* Safe. */

    /* The buffer refuses what it cannot answer for, and deletes only when told to. */
    {
        RoboTextGsubBuffer *buffer = RoboTextGsubBufferCreate(2u);
        RoboTextShapingGlyph empty;

        RoboTextShapingGlyphInit(&empty);
        RoboCheckEqInt((int)RoboTextGsubBufferAppend(buffer, &empty), (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextGsubBufferAppend(NULL, &glyph), (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextGsubBufferAppend(buffer, &glyph), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGsubBufferCount(buffer), 1);
        RoboCheckEqInt((int)RoboTextGsubBufferSetGlyph(buffer, 0u, 9u), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGsubBufferAt(buffer, 0u)->glyphId, 9);
        RoboCheckEqInt((int)RoboTextGsubBufferSetGlyph(buffer, 5u, 9u), (int)RoboInvalidArgument);
        RoboCheck(RoboTextGsubBufferAt(buffer, 1u) == NULL);
        RoboCheck(RoboTextGsubBufferAtMutable(buffer, 0u) != NULL);
        RoboCheckEqInt((int)RoboTextGsubBufferReplace(buffer, 4u, 1u, NULL, 0u),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextGsubBufferReplace(buffer, 0u, 1u, NULL, 0u), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextGsubBufferCount(buffer), 0);
        RoboTextGsubBufferDestroy(buffer);
        RoboTextGsubBufferDestroy(NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Plan resolution                                                     */
/* ------------------------------------------------------------------ */

static void TestGsubPlan(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan;

    FixtureOpen(&fixture, &layout);

    /* No features asked: the script's defaults, which are `ccmp`, `liga` and `calt` in this font. */
    RoboTextGsubPlanInit(&plan);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, NULL, &plan);
    RoboCheck(!plan.empty);
    RoboCheck(!plan.usedDefaultScript);
    RoboCheckEqInt((int)plan.scriptTag, (int)RoboFontTag('l', 'a', 't', 'n'));
    RoboCheck(plan.usedDefaultLangSys);
    /* Ascending and deduplicated: 1 (ccmp), 3 (liga), 4 (calt), which is the Lookup List's order. */
    RoboCheckEqInt((int)plan.lookupCount, 3);
    RoboCheckEqInt((int)plan.lookupOrder[0], 1);
    RoboCheckEqInt((int)plan.lookupOrder[1], 3);
    RoboCheckEqInt((int)plan.lookupOrder[2], 4);
    RoboCheckEqInt((int)plan.featureCount, 3);

    /* A feature the caller asks for joins them, and the lookups stay sorted. */
    RoboTextFeatureSetInit(&features);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst1", RoboTextFeatureOn, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    RoboCheckEqInt((int)plan.lookupCount, 4);
    RoboCheckEqInt((int)plan.lookupOrder[0], 0);

    /* `Off` overrides the script's default: `liga` off drops lookup 3 and nothing else. */
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "liga", RoboTextFeatureOff, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    RoboCheckEqInt((int)plan.lookupCount, 3);
    RoboCheckEqInt((int)plan.lookupOrder[0], 0);
    RoboCheckEqInt((int)plan.lookupOrder[1], 1);
    RoboCheckEqInt((int)plan.lookupOrder[2], 4);

    /* A script the font has no record for falls back to `DFLT`, which here carries only `liga`. */
    PlanFor(fixture.face, RoboUScriptGreek, NULL, NULL, &plan);
    RoboCheck(!plan.empty);
    RoboCheck(plan.usedDefaultScript);
    RoboCheckEqInt((int)plan.scriptTag, (int)RoboFontTag('D', 'F', 'L', 'T'));
    RoboCheckEqInt((int)plan.lookupCount, 1);
    RoboCheckEqInt((int)plan.lookupOrder[0], 3);
    RoboCheck(plan.usedDefaultLangSys);

    /* Common text has no tag of its own, so it asks the `DFLT` record the same way. */
    PlanFor(fixture.face, RoboUScriptCommon, NULL, NULL, &plan);
    RoboCheck(plan.usedDefaultScript);
    RoboCheckEqInt((int)plan.lookupCount, 1);

    /* A language system the font names is *chosen*: `TRK ` lists only `tst1`. */
    {
        RoboTextLanguage language;
        RoboTextFeatureSet none;

        RoboTextFeatureSetInit(&none);
        RoboCheckEqInt((int)RoboTextFeatureSetSet(&none, "tst1", RoboTextFeatureOn, 0u), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextLanguageSet(&language, "tr"), (int)RoboOk);
        PlanFor(fixture.face, RoboUScriptLatin, &language, &none, &plan);
        RoboCheck(!plan.usedDefaultLangSys);
        RoboCheckEqInt((int)plan.languageTag, (int)RoboFontTag('T', 'R', 'K', ' '));
        /* `TRK ` lists `tst1` and nothing else, so asking for `tst1` resolves one lookup. */
        RoboCheckEqInt((int)plan.lookupCount, 1);
        RoboCheckEqInt((int)plan.lookupOrder[0], 0);

        /*
         * The decisive half: the *same* feature set against the script's default language system turns
         * `liga` on as well, because `liga` is a script default and the default LangSys lists it. `TRK `
         * does not, so the language system — not the feature set — is what decided.
         */
        RoboCheckEqInt((int)RoboTextLanguageSet(&language, "en-US"), (int)RoboOk);
        PlanFor(fixture.face, RoboUScriptLatin, &language, &none, &plan);
        RoboCheck(plan.usedDefaultLangSys);
        RoboCheckEqInt((int)plan.lookupCount, 4); /* ccmp, liga, calt and the asked-for tst1. */
    }

    /* A face with no GSUB at all resolves an empty plan rather than failing. */
    {
        RoboTextGsubPlan empty;
        RoboTextGsubPlanInit(&empty);
        RoboCheckEqInt((int)RoboTextGsubPlanResolve(NULL, RoboUScriptLatin, NULL, NULL, &empty, NULL),
                       (int)RoboOk);
        RoboCheck(empty.empty);
        RoboCheckEqInt((int)RoboTextGsubPlanResolve(NULL, RoboUScriptLatin, NULL, NULL, NULL, NULL),
                       (int)RoboInvalidArgument);
    }

    FixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The lookup types                                                    */
/* ------------------------------------------------------------------ */

/** Answer the buffer for @p text with every feature off except @p tag (and the B letters `calt` sees). */
static RoboTextGsubBuffer *RunIsolated(GsubFixture *fixture,
                                       const char *tag,
                                       const char *text,
                                       RoboTextGsubStats *stats)
{
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};

    RoboTextFeatureSetInit(&features);
    /* The three script defaults this font carries are turned off, so the lookup under test is the only one
     * that can substitute: an isolated observation rather than a cascade. */
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "ccmp", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "liga", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "calt", RoboTextFeatureOff, 0u), (int)RoboOk);
    if (tag) {
        RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, tag, RoboTextFeatureOn, 0u), (int)RoboOk);
    }
    PlanFor(fixture->face, RoboUScriptLatin, NULL, &features, &plan);
    return RunPlan(fixture->face, &plan, text, &features, stats, NULL, 0u, NULL);
}

/** The buffer for @p text with the **script's defaults** (ccmp, liga, calt) and nothing asked for. */
static RoboTextGsubBuffer *RunDefaults(GsubFixture *fixture, const char *text, RoboTextGsubStats *stats)
{
    RoboTextGsubPlan plan = {0};

    PlanFor(fixture->face, RoboUScriptLatin, NULL, NULL, &plan);
    return RunPlan(fixture->face, &plan, text, NULL, stats, NULL, 0u, NULL);
}

/** The glyph ids the buffer holds, into @p out. @return how many. */
static size_t BufferIds(const RoboTextGsubBuffer *buffer, uint32_t *out, size_t capacity)
{
    size_t count = RoboTextGsubBufferCount(buffer);

    RoboCheck(count <= capacity);
    for (size_t index = 0u; index < count; index++) {
        out[index] = (uint32_t)RoboTextGsubBufferAt(buffer, index)->glyphId;
    }
    return count;
}

static void TestGsubSingle(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextGsubEvent trace[4];
    uint32_t ids[8];
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};
    const RoboTextShapingGlyph *glyph;

    FixtureOpen(&fixture, &layout);
    RoboTextFeatureSetInit(&features);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst1", RoboTextFeatureOn, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "B", &features, &stats, trace, 4u, NULL);

    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 1);
    RoboCheckEqInt((int)ids[0], FixtureBAlt);
    RoboCheckEqInt((int)RoboTextGsubBufferCount(buffer), 1); /* One in, one out. */
    glyph = RoboTextGsubBufferAt(buffer, 0u);
    RoboCheckEqInt((int)glyph->source.start, 0);
    RoboCheckEqInt((int)glyph->source.end, 1);
    RoboCheckEqInt((int)glyph->cluster, 0);
    RoboCheckEqInt((int)glyph->inputGlyphId, FixtureB);
    RoboCheck((glyph->flags & RoboTextShapingGlyphSubstituted) != 0u);
    RoboCheck((glyph->flags & RoboTextShapingGlyphLigature) == 0u);
    RoboCheckEqInt((int)stats.singleSubstitutions, 1);
    RoboCheckEqInt((int)stats.glyphsBefore, 1);
    RoboCheckEqInt((int)stats.glyphsAfter, 1);
    /* The plan is the font's Latin defaults (`ccmp`, `liga`, `calt`) plus the asked-for `tst1`, and every
     * one of them is applied even when it substitutes nothing — only one of the four fires here. */
    RoboCheckEqInt((int)stats.lookupsApplied, 4);
    RoboCheckEqInt((int)stats.traceEvents, 1);
    RoboCheckEqInt((int)trace[0].from[0], FixtureB);
    RoboCheckEqInt((int)trace[0].to[0], FixtureBAlt);
    /* The record's four bytes are unterminated by design; a reader copies them out. */
    {
        char eventTag[RoboTextFeatureTagBytes];

        memcpy(eventTag, trace[0].feature, 4u);
        eventTag[4] = '\0';
        RoboCheckStrEq(eventTag, "tst1");
    }
    RoboCheckEqInt((int)trace[0].lookupType, 1);
    {
        char eventTag[RoboTextFeatureTagBytes];

        memcpy(eventTag, trace[0].feature, 4u);
        eventTag[4] = '\0';
        RoboCheckStrEq(eventTag, "tst1");
    }
    RoboTextGsubBufferDestroy(buffer);

    /* With the feature off the id is the cmap's, and nothing is recorded. */
    buffer = RunDefaults(&fixture, "B", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 1);
    RoboCheckEqInt((int)ids[0], FixtureB);
    RoboCheckEqInt((int)stats.singleSubstitutions, 0);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

/**
 * Single substitution, format 1 — the one signed delta for a whole coverage.
 *
 * This test exists because nothing else in the suite reached the path: every other single here is format 2,
 * and the reader's `Result` question was inverted for format 1 (`if (!GsubReadI16(...))` always returned), so
 * the delta branch was dead code that a green suite reported as working.
 */
static void TestGsubSingleDelta(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    uint32_t ids[4];

    FixtureOpen(&fixture, &layout);

    /* `A` is covered, so the substitute is `A` plus the delta the font states: 4 + 1. */
    buffer = RunIsolated(&fixture, "tst5", "A", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 4u), 1);
    RoboCheckEqInt((int)ids[0], FixtureAAlt1);
    RoboCheckEqInt((int)stats.singleSubstitutions, 1);
    RoboTextGsubBufferDestroy(buffer);

    /* A glyph the coverage does not list is untouched: the delta is not a blanket shift. */
    buffer = RunIsolated(&fixture, "tst5", "D", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 4u), 1);
    RoboCheckEqInt((int)ids[0], FixtureD);
    RoboCheckEqInt((int)stats.singleSubstitutions, 0);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubMultiple(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    uint32_t ids[8];
    const RoboTextShapingGlyph *first;
    const RoboTextShapingGlyph *second;

    FixtureOpen(&fixture, &layout);
    /* `ccmp` is a script default, so a Latin run gets it without asking. */
    buffer = RunIsolated(&fixture, "ccmp", "X", &stats);    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureB);
    RoboCheckEqInt((int)ids[1], FixtureC);
    first = RoboTextGsubBufferAt(buffer, 0u);
    second = RoboTextGsubBufferAt(buffer, 1u);
    /* Both products name the one scalar that produced them: one in, two out, same source. */
    RoboCheckEqInt((int)first->source.start, 0);
    RoboCheckEqInt((int)first->source.end, 1);
    RoboCheckEqInt((int)second->source.start, 0);
    RoboCheckEqInt((int)second->source.end, 1);
    RoboCheckEqInt((int)first->inputGlyphId, FixtureX);
    RoboCheckEqInt((int)second->inputGlyphId, FixtureX);
    RoboCheck((first->flags & RoboTextShapingGlyphMultiple) != 0u);
    RoboCheck((second->flags & RoboTextShapingGlyphMultiple) != 0u);
    RoboCheckEqInt((int)stats.multipleSubstitutions, 1);
    RoboCheckEqInt((int)stats.glyphsBefore, 1);
    RoboCheckEqInt((int)stats.glyphsAfter, 2);
    RoboTextGsubBufferDestroy(buffer);
    FixtureClose(&fixture);
}

static void TestGsubAlternate(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};
    uint32_t ids[4];

    FixtureOpen(&fixture, &layout);

    /* No preference: the first alternate, which is the documented default. */
    RoboTextFeatureSetInit(&features);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "salt", RoboTextFeatureOn, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "A", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 4u), 1);
    RoboCheckEqInt((int)ids[0], FixtureAAlt1);
    RoboCheckEqInt((int)stats.alternateSubstitutions, 1);
    RoboTextGsubBufferDestroy(buffer);

    /* The second, asked for by the feature's value. */
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "salt", RoboTextFeatureOn, 1u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "A", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 4u), 1);
    RoboCheckEqInt((int)ids[0], FixtureAAlt2);
    RoboTextGsubBufferDestroy(buffer);

    /* Out of range clamps to the first rather than reading past the set. */
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "salt", RoboTextFeatureOn, 9u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "A", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 4u), 1);
    RoboCheckEqInt((int)ids[0], FixtureAAlt1);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubLigature(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};
    uint32_t ids[8];
    const RoboTextShapingGlyph *glyph;

    FixtureOpen(&fixture, &layout);
    RoboTextFeatureSetInit(&features);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "ffi", &features, &stats, NULL, 0u, NULL);

    /* Three in, one out — and the one names all three scalars. */
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 1);
    RoboCheckEqInt((int)ids[0], FixtureFfi);
    glyph = RoboTextGsubBufferAt(buffer, 0u);
    RoboCheckEqInt((int)glyph->source.start, 0);
    RoboCheckEqInt((int)glyph->source.end, 3);
    RoboCheckEqInt((int)glyph->cluster, 0);
    RoboCheckEqInt((int)glyph->inputGlyphId, FixtureF);
    RoboCheckEqInt((int)glyph->ligatureComponents, 3);
    RoboCheck((glyph->flags & RoboTextShapingGlyphLigature) != 0u);
    RoboCheckEqInt((int)stats.ligatureSubstitutions, 1);
    RoboCheckEqInt((int)stats.glyphsAfter, 1);
    RoboTextGsubBufferDestroy(buffer);

    /*
     * The longest match wins because the font listed it first (Phase 12.12 §"longest-match behavior"): four
     * scalars in, one glyph out, and the *four*-component candidate is the one that produced it. Had the set
     * been ordered the other way this would be `f` plus `ffi`, which is what the pair below proves.
     */
    buffer = RunPlan(fixture.face, &plan, "fffi", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 1);
    RoboCheckEqInt((int)ids[0], FixtureFffi);
    glyph = RoboTextGsubBufferAt(buffer, 0u);
    RoboCheckEqInt((int)glyph->ligatureComponents, 4);
    RoboCheckEqInt((int)glyph->source.start, 0);
    RoboCheckEqInt((int)glyph->source.end, 4);
    RoboCheckEqInt((int)stats.ligatureSubstitutions, 1);
    RoboTextGsubBufferDestroy(buffer);

    /* Two glyphs are not a ligature in this font, and no candidate matches at all. */
    buffer = RunPlan(fixture.face, &plan, "ff", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureF);
    RoboCheckEqInt((int)ids[1], FixtureF);
    RoboCheckEqInt((int)stats.ligatureSubstitutions, 0);
    RoboTextGsubBufferDestroy(buffer);

    /* The feature toggles it: `liga` off leaves the three glyphs alone (12.10 §62). */
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "liga", RoboTextFeatureOff, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "ffi", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 3);
    RoboCheckEqInt((int)ids[0], FixtureF);
    RoboCheckEqInt((int)ids[1], FixtureF);
    RoboCheckEqInt((int)ids[2], FixtureI);
    RoboCheckEqInt((int)stats.ligatureSubstitutions, 0);
    RoboTextGsubBufferDestroy(buffer);

    /* `fi` alone is not a ligature in this font: the font's own data decides, not the engine. */
    RoboTextFeatureSetInit(&features);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "fi", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureF);
    RoboCheckEqInt((int)ids[1], FixtureI);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubContext(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    uint32_t ids[8];

    FixtureOpen(&fixture, &layout);

    /* `calt` is a script default: `BC` becomes `B.alt C` through the context's nested lookup 0, even though
     * `tst1` — the feature that owns lookup 0 — is not enabled. That is OpenType's model: a record applies
     * the lookup it names. */
    buffer = RunIsolated(&fixture, "calt", "BC", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureBAlt);
    RoboCheckEqInt((int)ids[1], FixtureC);
    RoboCheckEqInt((int)stats.contextualMatches, 1);
    RoboCheckEqInt((int)stats.singleSubstitutions, 1);
    RoboTextGsubBufferDestroy(buffer);

    /* The context fails and nothing happens. */
    buffer = RunIsolated(&fixture, "calt", "BD", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureB);
    RoboCheckEqInt((int)stats.contextualMatches, 0);
    RoboTextGsubBufferDestroy(buffer);

    /* `B` at the end has no lookahead. */
    buffer = RunIsolated(&fixture, "calt", "B", &stats);
    RoboCheckEqInt((int)ids[0], FixtureB);
    RoboCheckEqInt((int)stats.contextualMatches, 0);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

/** The mark test: the same bytes, two lookups, one with `IgnoreMarks`. */
static void TestGsubIgnoredGlyphs(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    uint32_t ids[8];

    FixtureOpen(&fixture, &layout);

    /* `B m C` — `m` is a GDEF mark. The plain context lookup (`calt`, no flags) does not see the mark as
     * skippable, so it tries to match `C` against the mark and fails. */
    buffer = RunIsolated(&fixture, "calt", "BmC", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 3);
    RoboCheckEqInt((int)ids[0], FixtureB);
    RoboCheckEqInt((int)ids[1], FixtureM);
    RoboCheckEqInt((int)ids[2], FixtureC);
    RoboCheckEqInt((int)stats.contextualMatches, 0);
    RoboTextGsubBufferDestroy(buffer);

    /* The `IgnoreMarks` twin shares the same subtable bytes and matches *across* the mark. */
    buffer = RunIsolated(&fixture, "tst6", "BmC", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 3);
    RoboCheckEqInt((int)ids[0], FixtureBAlt);
    RoboCheckEqInt((int)ids[1], FixtureM); /* The mark is stepped over, never removed. */
    RoboCheckEqInt((int)ids[2], FixtureC);
    RoboCheckEqInt((int)stats.contextualMatches, 1);

    /* The mark itself survives with its own source, which is what keeps selection honest. */
    RoboCheckEqInt((int)RoboTextGsubBufferAt(buffer, 1u)->source.start, 1);
    RoboCheckEqInt((int)RoboTextGsubBufferAt(buffer, 1u)->source.end, 2);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubChain(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    uint32_t ids[8];

    FixtureOpen(&fixture, &layout);

    /* `D B C`: backtrack `D`, input `B`, lookahead `C`. */
    buffer = RunIsolated(&fixture, "tst2", "DBC", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 3);
    RoboCheckEqInt((int)ids[0], FixtureD);
    RoboCheckEqInt((int)ids[1], FixtureBAlt);
    RoboCheckEqInt((int)ids[2], FixtureC);
    RoboCheckEqInt((int)stats.contextualMatches, 1);
    RoboTextGsubBufferDestroy(buffer);

    /* No backtrack: the rule does not apply. */
    buffer = RunIsolated(&fixture, "tst2", "BC", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureB);
    RoboCheckEqInt((int)stats.contextualMatches, 0);
    RoboTextGsubBufferDestroy(buffer);

    /* Backtrack present, lookahead wrong. */
    buffer = RunIsolated(&fixture, "tst2", "DBA", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 3);
    RoboCheckEqInt((int)ids[1], FixtureB);
    RoboCheckEqInt((int)stats.contextualMatches, 0);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubReverse(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    uint32_t ids[8];

    FixtureOpen(&fixture, &layout);

    buffer = RunIsolated(&fixture, "tst3", "CD", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureX);
    RoboCheckEqInt((int)ids[1], FixtureD);
    RoboCheckEqInt((int)stats.reverseSubstitutions, 1);
    RoboTextGsubBufferDestroy(buffer);

    /* Without the lookahead the reverse lookup does not fire. */
    buffer = RunIsolated(&fixture, "tst3", "C", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 1);
    RoboCheckEqInt((int)ids[0], FixtureC);
    RoboCheckEqInt((int)stats.reverseSubstitutions, 0);
    RoboTextGsubBufferDestroy(buffer);

    /* And the scan is a scan, not a single probe: the matching position is the one that is covered. */
    buffer = RunIsolated(&fixture, "tst3", "CCD", &stats);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 3);
    RoboCheckEqInt((int)ids[0], FixtureC);
    RoboCheckEqInt((int)ids[1], FixtureX);
    RoboCheckEqInt((int)ids[2], FixtureD);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubExtension(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextGsubEvent trace[2];
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};
    uint32_t ids[4];
    const RoboTextShapingGlyph *glyph;

    FixtureOpen(&fixture, &layout);
    RoboTextFeatureSetInit(&features);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst4", RoboTextFeatureOn, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    /* The plan names the *wrapper's* lookup; the wrapper's inner type is what the trace must report. */
    RoboCheckEqInt((int)plan.lookupCount, 4);
    RoboCheckEqInt((int)plan.lookupOrder[3], 7); /* Last of the Lookup List's order, wrapper and all. */
    buffer = RunPlan(fixture.face, &plan, "B", &features, &stats, trace, 2u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 4u), 1);
    RoboCheckEqInt((int)ids[0], FixtureBAlt);
    glyph = RoboTextGsubBufferAt(buffer, 0u);
    RoboCheckEqInt((int)glyph->inputGlyphId, FixtureB);
    RoboCheckEqInt((int)stats.singleSubstitutions, 1);
    RoboCheckEqInt((int)stats.traceEvents, 1);
    RoboCheckEqInt((int)trace[0].lookupType, 1); /* Not 7: the real type, after unwrapping. */
    RoboCheckEqInt((int)trace[0].lookupIndex, 7);
    RoboTextGsubBufferDestroy(buffer);
    FixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Order, controls, limits and malformed data                           */
/* ------------------------------------------------------------------ */

static void TestGsubLookupOrder(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    uint32_t ids[8];

    FixtureOpen(&fixture, &layout);

    /*
     * Every feature at once, which puts lookups 0..8 in one plan. The order the plan applies them is the
     * Lookup List's, and this font shows why that matters: lookup 1 (`X` -> `B C`) runs before lookups 4/5/8,
     * which would match the `B C` it produced. That is the font's intent — an engine applying lookups in
     * feature order would give a different answer.
     */
    RoboTextFeatureSetInit(&features);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst1", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "salt", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst2", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst3", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst4", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst6", RoboTextFeatureOn, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    RoboCheckEqInt((int)plan.lookupCount, 9);
    for (size_t index = 0u; index < plan.lookupCount; index++) {
        RoboCheckEqInt((int)plan.lookupOrder[index], (int)index);
    }
    /* One lookup two features name (0, by `tst1` and by the context records) runs once: eight lookups, and
     * the cascade is visible in the result — `X` becomes `B C` and then the context substitutes the `B`. */
    buffer = RunPlan(fixture.face, &plan, "X", &features, &stats, NULL, 0u, NULL);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 2);
    RoboCheckEqInt((int)ids[0], FixtureBAlt);
    RoboCheckEqInt((int)ids[1], FixtureC);
    /* Nine top-level lookups, and the context's record names lookup 0 again: nested applications are
     * applications, which is what the counter documents. */
    RoboCheckEqInt((int)stats.lookupsApplied, 10);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubControls(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextGsubPlan plan;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextFeatureSet features;
    const RoboTextShapingGlyph *control;

    FixtureOpen(&fixture, &layout);
    RoboTextFeatureSetInit(&features);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);

    /* A control is in the buffer — so every later source position still has a slot — and is flagged so no
     * lookup sees it. The engine is handed one by hand here because the *generator* is what builds them. */
    buffer = RoboTextGsubBufferCreate(4u);
    {
        RoboTextShapingGlyph glyph;

        RoboTextShapingGlyphInit(&glyph);
        glyph.glyphId = RoboGlyphIdNone;
        glyph.source = RoboTextRangeMake(0u, 1u);
        glyph.cluster = 0u;
        glyph.flags = RoboTextShapingGlyphControl;
        RoboCheckEqInt((int)RoboTextGsubBufferAppend(buffer, &glyph), (int)RoboOk);
        glyph.flags = 0u;
        glyph.glyphId = FixtureB;
        glyph.source = RoboTextRangeMake(1u, 2u);
        glyph.cluster = 1u;
        RoboCheckEqInt((int)RoboTextGsubBufferAppend(buffer, &glyph), (int)RoboOk);
        glyph.glyphId = FixtureC;
        glyph.source = RoboTextRangeMake(2u, 3u);
        glyph.cluster = 2u;
        RoboCheckEqInt((int)RoboTextGsubBufferAppend(buffer, &glyph), (int)RoboOk);
    }
    {
        RoboTextGsubOptions options;
        const FontOtLayout *gsub = FontFaceGsub(fixture.face);

        memset(&options, 0, sizeof(options));
        options.direction = RoboTextDirectionLtr;
        options.bidiLevel = 0u;
        RoboCheckEqInt(
            (int)RoboTextGsubApply(gsub, FontFaceGdef(fixture.face), &plan, &options, buffer, &stats,
                                   NULL),
            (int)RoboOk);
    }
    /* Three slots in, three out: the control held its place and `B C` matched across it. */
    RoboCheckEqInt((int)RoboTextGsubBufferCount(buffer), 3);
    control = RoboTextGsubBufferAt(buffer, 0u);
    RoboCheckEqInt((int)control->glyphId, (int)RoboGlyphIdNone);
    RoboCheck((control->flags & RoboTextShapingGlyphControl) != 0u);
    RoboCheck((control->flags & RoboTextShapingGlyphSubstituted) == 0u);
    RoboCheckEqInt((int)RoboTextGsubBufferAt(buffer, 1u)->glyphId, FixtureBAlt);
    RoboCheckEqInt((int)RoboTextGsubBufferAt(buffer, 2u)->glyphId, FixtureC);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);
}

static void TestGsubLimitsAndMalformed(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextGsubLimits limits;
    uint32_t ids[8];

    FixtureOpen(&fixture, &layout);
    RoboTextFeatureSetInit(&features);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);

    /* A probe budget of one refuses the context lookup's second comparison: the lookup is abandoned, the
     * call still succeeds, and the limit is *reported* rather than silently swallowed. */
    limits = RoboTextGsubDefaultLimits;
    limits.maxGlyphProbes = 1u;
    buffer = RunPlan(fixture.face, &plan, "ffi", &features, &stats, NULL, 0u, &limits);
    RoboCheckEqInt((int)RoboTextGsubBufferCount(buffer), 3);
    RoboCheckEqInt((int)stats.ligatureSubstitutions, 0);
    RoboCheck(stats.limitReached);
    RoboTextGsubBufferDestroy(buffer);

    /* A glyph ceiling of one refuses the multiple substitution before it allocates. */
    limits = RoboTextGsubDefaultLimits;
    limits.maxGlyphs = 1u;
    buffer = RunPlan(fixture.face, &plan, "X", &features, &stats, NULL, 0u, &limits);
    RoboCheckEqInt((int)RoboTextGsubBufferCount(buffer), 1);
    RoboCheckEqInt((int)stats.multipleSubstitutions, 0);
    RoboCheck(stats.limitReached);
    RoboTextGsubBufferDestroy(buffer);

    /* A lookup-application budget of one runs `ccmp` and stops. */
    limits = RoboTextGsubDefaultLimits;
    limits.maxLookupApplications = 1u;
    buffer = RunPlan(fixture.face, &plan, "Xffi", &features, &stats, NULL, 0u, &limits);
    RoboCheckEqInt((int)BufferIds(buffer, ids, 8u), 5); /* `X` became `B C`; the ligature never ran. */
    RoboCheckEqInt((int)ids[0], FixtureB);
    RoboCheckEqInt((int)ids[1], FixtureC);
    RoboCheckEqInt((int)ids[2], FixtureF);
    RoboCheckEqInt((int)stats.multipleSubstitutions, 1);
    RoboCheckEqInt((int)stats.ligatureSubstitutions, 0);
    RoboCheck(stats.limitReached);
    RoboTextGsubBufferDestroy(buffer);

    FixtureClose(&fixture);

    /*
     * A malformed table is a skip, not a failure. This is the same fixture with one field corrupted: the
     * single substitution's coverage offset points past the end of the table, which is what a truncated
     * download or a hand-edited font looks like. The lookup must be abandoned, the call must succeed, and
     * the glyphs the cmap produced must be what the caller gets.
     */
    {
        GsubFixture broken;
        RoboTextFeatureSet features;
        RoboTextGsubPlan plan = {0};
        RoboTextGsubBuffer *buffer;
        RoboTextGsubStats stats;
        uint32_t ids[4];
        size_t gsubOffset = 0u;
        size_t entry;

        /* Built, then patched, then opened: the copy the face parses is made by `FixtureAdopt`. */
        FixtureBuildFont(&layout);
        /* Find the GSUB table's file offset in the directory the builder wrote. */
        for (entry = 0u; entry < 9u; entry++) {
            const uint8_t *record = g_fontBytes + 12u + entry * 16u;

            if (memcmp(record, "GSUB", 4u) == 0) {
                gsubOffset = ((size_t)record[8] << 24) | ((size_t)record[9] << 16) |
                             ((size_t)record[10] << 8) | (size_t)record[11];
                break;
            }
        }
        RoboCheck(gsubOffset != 0u);
        /* The coverage offset field of lookup 0's subtable, now pointing past the table's end. */
        g_fontBytes[gsubOffset + layout.singleSubtable + 2u] = 0xFFu;
        g_fontBytes[gsubOffset + layout.singleSubtable + 3u] = 0xFEu;
        FixtureAdopt(&broken);

        RoboTextFeatureSetInit(&features);
        RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst1", RoboTextFeatureOn, 0u),
                       (int)RoboOk);
        PlanFor(broken.face, RoboUScriptLatin, NULL, &features, &plan);
        buffer = RunPlan(broken.face, &plan, "B", &features, &stats, NULL, 0u, NULL);
        RoboCheckEqInt((int)BufferIds(buffer, ids, 4u), 1);
        RoboCheckEqInt((int)ids[0], FixtureB); /* Not substituted: the lookup was skipped. */
        RoboCheckEqInt((int)stats.singleSubstitutions, 0);
        /* The plan's four lookups all ran; only the corrupted one could not read what it needed. */
        RoboCheckEqInt((int)stats.lookupsApplied, 4);
        RoboTextGsubBufferDestroy(buffer);
        FixtureClose(&broken);
    }
}

/* ------------------------------------------------------------------ */
/* The generator's integration and the shipped pack                     */
/* ------------------------------------------------------------------ */

static void TestGsubRunIntegration(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextFontContext font;
    RoboTextFontStyle style;
    RoboTextShapeOptions options;
    RoboTextRunSet *set = NULL;
    RoboTextGlyphRun *run = NULL;
    const char *text = "ffi";
    RoboTextView view = RoboTextViewMake(text, 3u);

    FixtureOpen(&fixture, &layout);
    RoboTextFontStyleInit(&style);
    style.sizePx = 1000.0f; /* One em equals one font unit, so advances read as whole numbers. */
    RoboCheckEqInt((int)RoboTextFontContextSet(&font, fixture.face, &style), (int)RoboOk);
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextRunSetCreate(view, &font, &options, &set, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, RoboTextRunSetTextRun(set, 0u), &run, NULL),
                   (int)RoboOk);

    /* `liga` is a script default, so the generated run is already the ligature: one glyph for three scalars. */
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(run), 3);
    RoboCheckEqInt((int)RoboTextGlyphRunSubstituted(run), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(run), (int)RoboTextGlyphGenerationGsub);
    {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, 0u);

        RoboCheckEqInt((int)glyph->glyphId, FixtureFfi);
        RoboCheckEqInt((int)glyph->advanceUnits, FixtureAdvance(FixtureFfi));
        /* The advance is the *substitute's*, not the sum of the three it replaced: that is the number a
         * reader would notice if it were wrong. */
        RoboCheckEqInt((int)glyph->advanceX, (int)FixtureAdvance(FixtureFfi));
        RoboCheck(glyph->substituted);
        RoboCheckEqInt((int)glyph->source.start, 0);
        RoboCheckEqInt((int)glyph->source.end, 3);
    }
    /* The measurement is the sum of the glyphs' own advances, which is now one glyph's. */
    RoboCheckEqInt((int)RoboTextGlyphRunAdvance(run), (int)FixtureAdvance(FixtureFfi));
    RoboTextGlyphRunDestroy(run);

    /* The toggle, end to end: `liga` off and the run has three glyphs, each with its own advance. */
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "liga", RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    RoboTextRunSetDestroy(set);
    set = NULL;
    RoboCheckEqInt((int)RoboTextRunSetCreate(view, &font, &options, &set, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, RoboTextRunSetTextRun(set, 0u), &run, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 3);
    RoboCheckEqInt((int)RoboTextGlyphRunSubstituted(run), 0);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 0u)->glyphId, FixtureF);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 1u)->glyphId, FixtureF);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyph(run, 2u)->glyphId, FixtureI);
    RoboTextGlyphRunDestroy(run);
    RoboTextRunSetDestroy(set);
    FixtureClose(&fixture);
}

/** The shipped pack: a real font's own GSUB must not break a run, and the mode must say it ran. */
static void TestGsubShippedPack(void)
{
    TextFixture fixture = TextFixtureOpen();
    const char *text = "Hello, world!";
    RoboTextView view = RoboTextViewMake(text, strlen(text));
    RoboTextRunSet *set = NULL;
    RoboTextGlyphRun *run = NULL;
    const FontOtLayout *gsub = NULL;
    RoboTextGsubPlan plan;
    RoboTextGsubStats stats;
    RoboTextScalarIndex scalars = 0u;
    size_t index;

    RoboCheckEqInt((int)RoboTextViewScalarCount(view, &scalars, NULL), (int)RoboOk);

    RoboCheckEqInt((int)RoboTextFontGsub(&fixture.font, &gsub, NULL), (int)RoboOk);
    if (gsub) {
        RoboCheckEqInt((int)RoboTextGsubPlanResolve(gsub, RoboUScriptLatin, NULL, NULL, &plan, NULL),
                       (int)RoboOk);
        RoboCheck(RoboTextGsubPlanIsValid(&plan));
        /* Whatever this face's plan is, it must be one the engine can walk: ascending, in range. */
        for (index = 0u; index < plan.lookupCount; index++) {
            RoboCheck(plan.lookupOrder[index] < gsub->lookups.lookupCount);
            if (index != 0u) {
                RoboCheck(plan.lookupOrder[index] > plan.lookupOrder[index - 1u]);
            }
        }
    }
    RoboCheckEqInt((int)RoboTextRunSetCreate(view, &fixture.font, NULL, &set, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextGlyphRunGenerate(view, RoboTextRunSetTextRun(set, 0u), &run, NULL),
                   (int)RoboOk);
    /* Nothing in this string belongs together: the run is one glyph per scalar, and every glyph keeps its
     * own source. A face's GSUB may substitute nothing here and must not invent anything either. */
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), (int)scalars);
    for (index = 0u; index < scalars; index++) {
        const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, index);

        RoboCheckEqInt((int)glyph->source.start, (int)index);
        RoboCheckEqInt((int)glyph->source.end, (int)index + 1);
    }
    RoboCheckEqInt((int)RoboTextGlyphRunControls(run), 0);
    RoboTextGsubStatsInit(&stats);
    RoboCheckEqInt((int)stats.limitReached, 0);
    RoboTextGlyphRunDestroy(run);
    RoboTextRunSetDestroy(set);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void TestGsubDiagnostics(void)
{
    GsubFixture fixture;
    FixtureLayout layout;
    RoboTextFeatureSet features;
    RoboTextGsubPlan plan = {0};
    RoboTextGsubStats stats;
    RoboTextGsubEvent trace[4];
    RoboTextGsubBuffer *buffer;
    char text[1024];
    size_t length = 0u;

    FixtureOpen(&fixture, &layout);
    RoboTextFeatureSetInit(&features);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&features, "tst1", RoboTextFeatureOn, 0u), (int)RoboOk);
    PlanFor(fixture.face, RoboUScriptLatin, NULL, &features, &plan);
    buffer = RunPlan(fixture.face, &plan, "B", &features, &stats, trace, 4u, NULL);

    /* The plan reads as the font's script, language and lookups. */
    RoboCheckEqInt((int)RoboTextGsubPlanWrite(&plan, text, sizeof(text), &length), (int)RoboOk);
    RoboCheck(length != 0u);
    RoboCheck(strstr(text, "latn") != NULL);
    RoboCheck(strstr(text, "tst1") != NULL);
    RoboCheck(strstr(text, "lookups:") != NULL);

    /* The buffer, the counters and the trace each print one line per thing. */
    RoboCheckEqInt((int)RoboTextGsubBufferWrite(buffer, text, sizeof(text), &length), (int)RoboOk);
    RoboCheck(strstr(text, "glyphs 1") != NULL);
    RoboCheck(strstr(text, "from=") != NULL);
    RoboCheckEqInt((int)RoboTextGsubStatsWrite(&stats, text, sizeof(text), &length), (int)RoboOk);
    RoboCheck(strstr(text, "single 1") != NULL);
    RoboCheckEqInt((int)RoboTextGsubEventsWrite(trace, stats.traceEvents, text, sizeof(text), &length),
                   (int)RoboOk);
    RoboCheck(strstr(text, "tst1") != NULL);
    RoboCheck(strstr(text, "->") != NULL);

    /* Complete-or-absent: a buffer that cannot hold the table is left as the empty string. */
    RoboCheckEqInt((int)RoboTextGsubPlanWrite(&plan, text, 8u, &length), (int)RoboNotSupported);
    RoboCheckStrEq(text, "");
    RoboCheckEqInt((int)RoboTextGsubStatsWrite(&stats, text, 8u, &length), (int)RoboNotSupported);
    RoboCheckStrEq(text, "");
    RoboCheckEqInt((int)RoboTextGsubBufferWrite(buffer, text, 8u, &length), (int)RoboNotSupported);
    RoboCheckStrEq(text, "");
    RoboCheckEqInt((int)RoboTextGsubPlanWrite(NULL, text, sizeof(text), &length),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGsubStatsWrite(&stats, NULL, sizeof(text), &length),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGsubEventsWrite(trace, 1u, NULL, sizeof(text), &length),
                   (int)RoboInvalidArgument);

    RoboTextGsubBufferDestroy(buffer);
    FixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterTextGsub(void)
{
    TestRegister("text.gsub.vocabulary", TestGsubVocabulary);
    TestRegister("text.gsub.plan", TestGsubPlan);
    TestRegister("text.gsub.single", TestGsubSingle);
    TestRegister("text.gsub.single_delta", TestGsubSingleDelta);
    TestRegister("text.gsub.multiple", TestGsubMultiple);
    TestRegister("text.gsub.alternate", TestGsubAlternate);
    TestRegister("text.gsub.ligature", TestGsubLigature);
    TestRegister("text.gsub.context", TestGsubContext);
    TestRegister("text.gsub.ignored_glyphs", TestGsubIgnoredGlyphs);
    TestRegister("text.gsub.chain", TestGsubChain);
    TestRegister("text.gsub.reverse", TestGsubReverse);
    TestRegister("text.gsub.extension", TestGsubExtension);
    TestRegister("text.gsub.lookup_order", TestGsubLookupOrder);
    TestRegister("text.gsub.controls", TestGsubControls);
    TestRegister("text.gsub.limits", TestGsubLimitsAndMalformed);
    TestRegister("text.gsub.run_integration", TestGsubRunIntegration);
    TestRegister("text.gsub.shipped_pack", TestGsubShippedPack);
    TestRegister("text.gsub.diagnostics", TestGsubDiagnostics);
}
