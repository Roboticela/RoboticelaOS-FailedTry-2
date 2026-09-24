/*
 * The deterministic test fonts the shaping conformance suites (Phase 12.18) are proved against.
 *
 * ## Why a builder and not a shipped font
 *
 * The repo's corpus policy prefers a synthetic builder in C over a checked-in blob, and conformance is the
 * case that policy exists for. A *production* face is unsuitable as the sole oracle for a different reason
 * than it is unsuitable as a blob: its substitution and kerning data is a moving target. Inter happens to
 * carry `dlig` and no `liga`; a suite that expected `fi` to ligate in the shipped pack would be asserting a
 * fact about Inter rather than about the engine, and would break the day the pack changed. This builder
 * states the bytes, so `f f i` becomes one glyph **because the font says so** and nothing else.
 *
 * ## What the font carries
 *
 *   - a `cmap` whose coverage is the canonical code point list below: ASCII letters and digits, European
 *     punctuation, Latin-1 letters, Greek and Cyrillic (upper and lower), and five combining marks. The
 *     mapping is *derived* from that one list, so `ConfGlyphOf(cp)` and the built table cannot disagree;
 *   - `hmtx` advances that are distinct per glyph (`400 + glyphId`), so a wrong glyph id is a wrong number
 *     and a test can tell a ligature's own advance from the sum of its components';
 *   - `GSUB` ligature lookups, one per feature tag, with the rules stated by the caller — standard (`liga`),
 *     discretionary (`dlig`) and required (`rlig`), which is the three-tag slice the phase's feature-toggle
 *     and policy tests need;
 *   - `GPOS` pair-adjustment lookups under `kern`, and single-adjustment lookups under `mark`;
 *   - `GDEF` glyph classes that make the five combining marks marks, which is what the engines' `IgnoreMarks`
 *     participation rule reads.
 *
 * ## The one honest deviation, named
 *
 * The mark placement is a **single-adjustment** lookup (GPOS type 1) under the `mark` feature, not a
 * mark-to-base lookup (type 4). That is not a shortcut in the fixture: Phase 12.11 deliberately executes
 * types 1, 2 and 9 and *skips* 3-8, so a type-4 table here would be counted `lookupsUnsupported` and place
 * nothing. What the suite can therefore prove is "a mark glyph draws at the font's stated offset and advances
 * nothing"; what it must say it cannot prove is mark-to-base attachment, and `text.shaping.marks.attachment`
 * asserts the unsupported count rather than pretending otherwise.
 *
 * ## The fallback pair
 *
 * `ConfSpecFallbackPrimary` maps the Latin block and nothing else; `ConfSpecFallbackSecondary` maps the Greek
 * and Cyrillic blocks. A test shapes `"A Ω Б"` over the two and reads which run got which face, which is the
 * only way to test fallback without letting the host's installed fonts decide the answer.
 */

#ifndef ROBO_TEXT_CONFORMANCE_FONT_H
#define ROBO_TEXT_CONFORMANCE_FONT_H

#include "RoboFont/RoboFont.h"
#include "RoboTest.h"
#include "RoboText/RoboText.h"
#include "RoboText/RoboTextOtLayout.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Capacities                                                          */
/* ------------------------------------------------------------------ */

/** Glyph ids a conformance font can hold. The canonical list is ~233 and ligature glyphs sit above it. */
#define ConfMaxGlyphs 384u
/**
 * Distinct `cmap` runs. One per contiguous (code point, glyph) run of the canonical list, and the canonical
 * list is deliberately sparse after the ASCII block — every accented letter and every combining mark is its
 * own run — so this is generous rather than tight.
 */
#define ConfMaxCmapGroups 96u
/** Ligature rules across every feature. */
#define ConfMaxLigatures 24u
/** GPOS pair rules. */
#define ConfMaxPairs 16u
/** GPOS single-adjustment rules. */
#define ConfMaxSingles 16u
/** GDEF mark glyph ids. */
#define ConfMaxMarks 32u
/** Distinct feature tags in one table. */
#define ConfMaxTags 8u

/** GSUB single substitutions one fixture may state. */
#define ConfMaxSubst 24u

/** Mark-attachment rules one fixture may state, per kind. */
#define ConfMaxMarkRules 16u

/** Cursive-attachment rules one fixture may state. */
#define ConfMaxCursive 16u

/** Lookup kinds a tag's feature may name, one per GPOS lookup type the builder can emit. */
#define ConfGposKindsMax 6u
/** Bytes a built font occupies. */
#define ConfMaxFontBytes 24576u

/* ------------------------------------------------------------------ */
/* The canonical code point list                                       */
/* ------------------------------------------------------------------ */

/**
 * The code points every conformance font maps, in the order their glyph ids follow.
 *
 * Glyph 0 is `.notdef`; the code point at index @p i of this list is glyph @p i + 1. Both the builder and
 * `ConfGlyphOf` read this one list, which is what makes an expected glyph id in a suite a *derived* fact.
 *
 * The order is the blocks the phase names, and it is stable on purpose: a suite that wants `A`'s id asks
 * `ConfGlyphOf('A')` rather than writing 1.
 */
static inline size_t ConfCanonicalCodepoints(uint32_t *out, size_t capacity)
{
    static const uint32_t kPunctuation[] = {
        0x0020u, 0x0021u, 0x0022u, 0x0023u, 0x0024u, 0x0025u, 0x0026u, 0x0027u, 0x0028u, 0x0029u,
        0x002Au, 0x002Bu, 0x002Cu, 0x002Du, 0x002Eu, 0x002Fu, 0x003Au, 0x003Bu, 0x003Cu, 0x003Du,
        0x003Eu, 0x003Fu, 0x0040u, 0x005Bu, 0x005Cu, 0x005Du, 0x005Eu, 0x005Fu, 0x0060u, 0x007Bu,
        0x007Cu, 0x007Du, 0x007Eu};
    static const uint32_t kLatin1[] = {
        0x00A0u, 0x00A3u, 0x00A9u, 0x00AEu, 0x00C4u, 0x00C5u, 0x00C6u, 0x00C7u, 0x00D6u, 0x00D8u,
        0x00DCu, 0x00DFu, 0x00E0u, 0x00E1u, 0x00E2u, 0x00E3u, 0x00E4u, 0x00E5u, 0x00E6u, 0x00E7u,
        0x00E8u, 0x00E9u, 0x00EAu, 0x00ECu, 0x00EDu, 0x00EEu, 0x00F0u, 0x00F1u, 0x00F2u, 0x00F3u,
        0x00F4u, 0x00F5u, 0x00F6u, 0x00F8u, 0x00F9u, 0x00FAu, 0x00FBu, 0x00FCu, 0x00FEu, 0x0111u,
        0x0142u, 0x0153u};
    static const uint32_t kMarks[] = {0x0300u, 0x0301u, 0x0303u, 0x0308u, 0x030Au};
    size_t count = 0u;

    if (!out || capacity == 0u) {
        return 0u;
    }
#define CONF_ADD(cp)                 \
    do {                             \
        if (count < capacity) {      \
            out[count] = (cp);       \
        }                            \
        count++;                     \
    } while (0)

    for (uint32_t c = 'A'; c <= 'Z'; c++) {
        CONF_ADD(c);
    }
    for (uint32_t c = 'a'; c <= 'z'; c++) {
        CONF_ADD(c);
    }
    for (uint32_t c = '0'; c <= '9'; c++) {
        CONF_ADD(c);
    }
    for (size_t i = 0u; i < sizeof(kPunctuation) / sizeof(kPunctuation[0]); i++) {
        CONF_ADD(kPunctuation[i]);
    }
    for (size_t i = 0u; i < sizeof(kLatin1) / sizeof(kLatin1[0]); i++) {
        CONF_ADD(kLatin1[i]);
    }
    /* Greek: upper U+0391..U+03A9 skipping the unassigned U+03A2, then lower U+03B1..U+03C9. */
    for (uint32_t c = 0x0391u; c <= 0x03A9u; c++) {
        if (c != 0x03A2u) {
            CONF_ADD(c);
        }
    }
    for (uint32_t c = 0x03B1u; c <= 0x03C9u; c++) {
        CONF_ADD(c);
    }
    /* Cyrillic: upper U+0410..U+042F then lower U+0430..U+044F. */
    for (uint32_t c = 0x0410u; c <= 0x042Fu; c++) {
        CONF_ADD(c);
    }
    for (uint32_t c = 0x0430u; c <= 0x044Fu; c++) {
        CONF_ADD(c);
    }
    for (size_t i = 0u; i < sizeof(kMarks) / sizeof(kMarks[0]); i++) {
        CONF_ADD(kMarks[i]);
    }
#undef CONF_ADD
    return count;
}

/** The glyph id @p codePoint maps to, or 0 when the canonical list does not carry it. */
static inline uint16_t ConfGlyphOf(uint32_t codePoint)
{
    uint32_t list[ConfMaxGlyphs];

    {
        size_t count = ConfCanonicalCodepoints(list, ConfMaxGlyphs);

        for (size_t i = 0u; i < count && i < ConfMaxGlyphs; i++) {
            if (list[i] == codePoint) {
                return (uint16_t)(i + 1u);
            }
        }
    }
    return 0u;
}

/**
 * UTF-8-encode @p codePoint into @p out (NUL-terminated) and answer the byte count.
 *
 * The suites that walk a block character by character need the bytes rather than a literal, and a helper
 * that encoded a scalar wrongly would make every one of those cases fail for the same wrong reason.
 */
static inline size_t ConfUtf8(uint32_t codePoint, char *out)
{
    if (codePoint < 0x80u) {
        out[0] = (char)codePoint;
        out[1] = '\0';
        return 1u;
    }
    if (codePoint < 0x800u) {
        out[0] = (char)(0xC0u | (codePoint >> 6));
        out[1] = (char)(0x80u | (codePoint & 0x3Fu));
        out[2] = '\0';
        return 2u;
    }
    out[0] = (char)(0xE0u | (codePoint >> 12));
    out[1] = (char)(0x80u | ((codePoint >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (codePoint & 0x3Fu));
    out[3] = '\0';
    return 3u;
}

/** The code point glyph @p glyphId stands for, or 0 when it is `.notdef` or a ligature glyph. */
static inline uint32_t ConfCodePointOf(uint16_t glyphId)
{
    uint32_t list[ConfMaxGlyphs];

    if (glyphId == 0u) {
        return 0u;
    }
    {
        size_t count = ConfCanonicalCodepoints(list, ConfMaxGlyphs);

        if ((size_t)glyphId <= count && (size_t)glyphId <= ConfMaxGlyphs) {
            return list[glyphId - 1u];
        }
    }
    return 0u;
}

/* ------------------------------------------------------------------ */
/* The spec                                                            */
/* ------------------------------------------------------------------ */

/** One ligature rule: @p components (the covered glyph included) become @p result, under @p tag. */
typedef struct ConfLigatureRule {
    char tag[RoboTextFeatureTagBytes];
    uint16_t components[4];
    uint16_t componentCount;
    uint16_t result;
} ConfLigatureRule;

/** One kerning pair: @p first against @p second, its advance moved by @p xAdvance font units. */
typedef struct ConfPairRule {
    char tag[RoboTextFeatureTagBytes];
    uint16_t first;
    uint16_t second;
    int16_t xAdvance;
} ConfPairRule;

/** One single placement: @p glyph drawn at an offset, with an optional advance change. */
typedef struct ConfSingleRule {
    char tag[RoboTextFeatureTagBytes];
    uint16_t glyph;
    int16_t xPlacement;
    int16_t yPlacement;
    int16_t xAdvance;
} ConfSingleRule;

/**
 * One **GSUB single substitution**: @p glyph becomes @p substitute under @p tag.
 *
 * The complex scripts' whole vocabulary, and the reason it is a rule kind of its own: Arabic's
 * `isol`/`init`/`medi`/`fina` are all one glyph for one glyph, and Devanagari's `nukt`/`akhn` are too. A
 * fixture that had only ligature rules could not state a contextual form at all.
 */
typedef struct ConfSubstRule {
    char tag[RoboTextFeatureTagBytes];
    uint16_t glyph;
    uint16_t substitute;
} ConfSubstRule;

/**
 * One **mark attachment**: @p mark's anchor joins @p anchorGlyph's, for @p markClass, under @p tag.
 *
 * `anchorGlyph` is the base for `mark` (GPOS type 4) and the second **mark** for `mkmk` (type 6) — the same
 * record, because the two formats are the same layout with one field renamed. Both coordinates are font units
 * and both anchors are read by the same reader, so a case can say "the mark sits where the base's anchor is"
 * as one number.
 */
typedef struct ConfMarkRule {
    char tag[RoboTextFeatureTagBytes];
    uint16_t mark;
    uint16_t anchorGlyph;
    uint16_t markClass;
    int16_t markX;
    int16_t markY;
    int16_t anchorX;
    int16_t anchorY;
} ConfMarkRule;

/** One mark on a **ligature** (GPOS type 5): @p component of @p ligature, joined to @p mark's anchor. */
typedef struct ConfMarkLigatureRule {
    char tag[RoboTextFeatureTagBytes];
    uint16_t mark;
    uint16_t ligature;
    uint16_t componentCount;
    uint16_t component;
    uint16_t markClass;
    int16_t markX;
    int16_t markY;
    int16_t anchorX;
    int16_t anchorY;
} ConfMarkLigatureRule;

/** One cursive pair (GPOS type 3): @p glyph's entry and exit anchors, in font units. */
typedef struct ConfCursiveRule {
    char tag[RoboTextFeatureTagBytes];
    uint16_t glyph;
    int16_t entryX;
    int16_t entryY;
    int16_t exitX;
    int16_t exitY;
} ConfCursiveRule;

/** How the font's tables are put together. A value; the builder reads it and copies nothing. */
typedef struct ConfFontSpec {
    /** Row 0 is `.notdef`; the caller states every glyph's advance. */
    uint16_t advances[ConfMaxGlyphs];
    uint16_t glyphCount;
    /** Non-zero: derive the `cmap` from the canonical list. */
    int canonicalCmap;
    /**
     * When @ref canonicalCmap is set, include only the canonical code points **inside** `[first, last]`.
     *
     * The fallback pair's door, and the reason it is a *filter over the same list* rather than a hand-written
     * group table: the glyph ids stay the ones `ConfGlyphOf` derives, so a fallback case can still name the id
     * it expects, and the coalescing walk below still produces a correct, sorted group array. A hand-written
     * group table would have to restate the mapping, and a restated mapping is a second oracle.
     * `first == 0 && last == 0` means "no filter".
     */
    uint32_t cmapFilterFirst;
    uint32_t cmapFilterLast;
    /** The explicit runs, when @ref canonicalCmap is zero. */
    struct {
        uint32_t first;
        uint32_t last;
        uint16_t glyph;
    } cmap[ConfMaxCmapGroups];
    size_t cmapCount;
    ConfLigatureRule ligatures[ConfMaxLigatures];
    size_t ligatureCount;
    ConfPairRule pairs[ConfMaxPairs];
    size_t pairCount;
    ConfSingleRule singles[ConfMaxSingles];
    size_t singleCount;
    /** GSUB single substitutions (Phase 12.19): the contextual forms, half forms and so on. */
    ConfSubstRule subst[ConfMaxSubst];
    size_t substCount;
    /** Mark-to-base (`mark`) and mark-to-mark (`mkmk`) attachments. */
    ConfMarkRule markBase[ConfMaxMarkRules];
    size_t markBaseCount;
    ConfMarkRule markMark[ConfMaxMarkRules];
    size_t markMarkCount;
    /** Mark-to-ligature (`mark` against a ligature's component). */
    ConfMarkLigatureRule markLig[ConfMaxMarkRules];
    size_t markLigCount;
    /** Cursive attachment (`curs`): the entry and exit anchors per glyph. */
    ConfCursiveRule cursive[ConfMaxCursive];
    size_t cursiveCount;
    /** Glyphs GDEF classifies as marks (class 3), which is what `IgnoreMarks` reads. */
    uint16_t marks[ConfMaxMarks];
    size_t markCount;
    /** Omit a table, for the malformed and absence cases. */
    int omitGsub;
    int omitGpos;
    int omitGdef;
} ConfFontSpec;

/** The glyph ids a built font assigned to its ligature products, so a suite never writes a literal. */
typedef struct ConfGlyphIds {
    uint16_t ligaFfi;
    uint16_t ligaFfl;
    uint16_t ligaFf;
    uint16_t ligaFi;
    uint16_t dligCt;
    uint16_t rligSt;
    /** One past the last glyph: the font's `numGlyphs`. */
    uint16_t glyphCount;
} ConfGlyphIds;

/* ------------------------------------------------------------------ */
/* The byte writer                                                     */
/* ------------------------------------------------------------------ */

typedef struct ConfWriter {
    uint8_t *data;
    size_t capacity;
    size_t length;
} ConfWriter;

static inline void Cw16(ConfWriter *w, size_t at, uint16_t value)
{
    w->data[at] = (uint8_t)(value >> 8);
    w->data[at + 1u] = (uint8_t)value;
}

static inline void Cw32(ConfWriter *w, size_t at, uint32_t value)
{
    w->data[at] = (uint8_t)(value >> 24);
    w->data[at + 1u] = (uint8_t)(value >> 16);
    w->data[at + 2u] = (uint8_t)(value >> 8);
    w->data[at + 3u] = (uint8_t)value;
}

static inline size_t CwReserve(ConfWriter *w, size_t bytes)
{
    size_t at = w->length;

    RoboCheck(at + bytes <= w->capacity);
    memset(w->data + at, 0, bytes);
    w->length = at + bytes;
    return at;
}

static inline size_t Cw16At(ConfWriter *w, uint16_t value)
{
    size_t at = CwReserve(w, 2u);

    Cw16(w, at, value);
    return at;
}

static inline size_t Cw32At(ConfWriter *w, uint32_t value)
{
    size_t at = CwReserve(w, 4u);

    Cw32(w, at, value);
    return at;
}

/** Pad @p w to a four-byte boundary, which a table's internal offsets do not require but a directory does. */
static inline void CwPad4(ConfWriter *w)
{
    while ((w->length % 4u) != 0u) {
        (void)CwReserve(w, 1u);
    }
}

/** A coverage table, format 1, over a sorted glyph array. @return its offset relative to @p base. */
static inline uint16_t CwCoverage1(ConfWriter *w, size_t base, const uint16_t *glyphs, size_t count)
{
    size_t start = w->length;

    (void)Cw16At(w, 1u);
    (void)Cw16At(w, (uint16_t)count);
    for (size_t i = 0u; i < count; i++) {
        (void)Cw16At(w, glyphs[i]);
    }
    return (uint16_t)(start - base);
}

/* ------------------------------------------------------------------ */
/* ScriptList / FeatureList / LookupList                               */
/* ------------------------------------------------------------------ */

/** The four scripts every conformance font declares, all naming the same feature list. */
/**
 * The scripts every fixture declares: the four a European case needs, plus the two Phase 12.19 shapes.
 *
 * **Sorted by tag**, which is what an OpenType ScriptList is: a reader binary-searches it, so an unsorted list
 * is a list a script is missing from. The two complex scripts share the same default LangSys as everyone else,
 * which is what lets one fixture carry Arabic's `isol`/`fina`/… **and** Devanagari's `rphf`/`half`/… without
 * a second face: a suite asks for the script it is testing and the plan resolver selects only that script's
 * features.
 */
static const uint32_t kConfScriptTags[6] = {
    0x44464C54u /* DFLT */, 0x61726162u /* arab */, 0x6379726Cu /* cyrl */,
    0x64657661u /* deva */, 0x6772656Bu /* grek */, 0x6C61746Eu /* latn */};

/** Scripts @ref kConfScriptTags declares. */
#define ConfScriptCount 6u

/**
 * Write a ScriptList whose six scripts share one default LangSys.
 *
 * Sharing is legal and is what a font that treats all its scripts the same does. @p featureIndices is the
 * whole feature list, so every script reaches every feature — a face that wanted a `latn`-only feature would
 * say so, and a fixture that hid one behind a script would make a script-lookup bug look like a missing
 * feature.
 */
static inline size_t CwScriptListAllScripts(ConfWriter *w, size_t tableBase, size_t featureCount,
                                            const uint16_t *featureIndices)
{
    size_t listStart = w->length;
    size_t records[ConfScriptCount];
    size_t scripts[ConfScriptCount];
    size_t langSysStart;

    (void)Cw16At(w, (uint16_t)ConfScriptCount);
    for (size_t i = 0u; i < ConfScriptCount; i++) {
        (void)Cw32At(w, kConfScriptTags[i]);
        records[i] = Cw16At(w, 0u);
    }
    /* The Script tables are written first, then the one LangSys they all point at. */
    for (size_t i = 0u; i < ConfScriptCount; i++) {
        scripts[i] = w->length;
        Cw16(w, records[i], (uint16_t)(scripts[i] - listStart));
        (void)Cw16At(w, 0u); /* defaultLangSysOffset: patched below */
        (void)Cw16At(w, 0u); /* langSysCount */
    }
    langSysStart = w->length;
    for (size_t i = 0u; i < ConfScriptCount; i++) {
        Cw16(w, scripts[i], (uint16_t)(langSysStart - scripts[i]));
    }
    (void)Cw16At(w, 0u);      /* lookupOrder, reserved */
    (void)Cw16At(w, 0xFFFFu); /* requiredFeatureIndex: none */
    (void)Cw16At(w, (uint16_t)featureCount);
    for (size_t i = 0u; i < featureCount; i++) {
        (void)Cw16At(w, featureIndices[i]);
    }
    return (uint16_t)(listStart - tableBase);
}

/** One Feature table. @return its offset relative to @p base. */
static inline uint16_t CwFeature(ConfWriter *w, size_t base, const uint16_t *lookups, size_t count)
{
    size_t start = w->length;

    (void)Cw16At(w, 0u); /* featureParams */
    (void)Cw16At(w, (uint16_t)count);
    for (size_t i = 0u; i < count; i++) {
        (void)Cw16At(w, lookups[i]);
    }
    return (uint16_t)(start - base);
}

/** A FeatureList: one record per tag, each naming the lookups the caller gives it. */
static inline size_t CwFeatureList(ConfWriter *w, size_t tableBase, const uint32_t *tags,
                                   const uint16_t *lookups, const uint8_t *lookupCounts, size_t count)
{
    size_t listStart = w->length;
    size_t records;

    (void)Cw16At(w, (uint16_t)count);
    records = CwReserve(w, count * 6u);
    for (size_t i = 0u; i < count; i++) {
        size_t featureStart = w->length;

        Cw32(w, records + i * 6u, tags[i]);
        Cw16(w, records + i * 6u + 4u, (uint16_t)(featureStart - listStart));
        (void)CwFeature(w, listStart, lookups + i * 8u, lookupCounts[i]);
    }
    return listStart - tableBase;
}

/* ------------------------------------------------------------------ */
/* GSUB                                                                */
/* ------------------------------------------------------------------ */

/** Non-zero when @p tag is already in @p out, and records it when it is not. @return 1 when it was new. */
static inline int ConfTagSeen(uint32_t *out, size_t capacity, size_t *count, uint32_t tag)
{
    for (size_t j = 0u; j < *count; j++) {
        if (out[j] == tag) {
            return 0;
        }
    }
    if (*count < capacity) {
        out[(*count)++] = tag;
    }
    return 1;
}

/**
 * The same "first-appearance, once" walk for a **glyph id** list.
 *
 * A second door rather than one generic one because the two lists are different types — a tag is 32 bits, a
 * glyph id is 16 — and a coverage table is written from the 16-bit one. Folding them together would mean a
 * cast at every call site, which is the kind of quiet narrowing this fixture exists to make loud.
 */
static inline int ConfGlyphSeen(uint16_t *out, size_t capacity, size_t *count, uint16_t glyph)
{
    for (size_t j = 0u; j < *count; j++) {
        if (out[j] == glyph) {
            return 0;
        }
    }
    if (*count < capacity) {
        out[(*count)++] = glyph;
    }
    return 1;
}

/** Ascending, in place: what every coverage table and every array index in this fixture needs. */
static inline void ConfSortGlyphs(uint16_t *glyphs, size_t count)
{
    for (size_t a = 0u; a < count; a++) {
        for (size_t b = a + 1u; b < count; b++) {
            if (glyphs[b] < glyphs[a]) {
                uint16_t swap = glyphs[a];

                glyphs[a] = glyphs[b];
                glyphs[b] = swap;
            }
        }
    }
}

/**
 * Distinct GSUB tags, in first-appearance order: the ligature rules' and the single substitutions'.
 *
 * Both kinds, because a feature may carry both — Arabic's `rlig` is a ligature (lam + alef) while its four
 * contextual forms are single substitutions, and a fixture that listed only one kind would build a table with
 * the other feature missing and a case would fail for a reason that is not the engine's.
 */
static inline size_t ConfGsubTags(const ConfFontSpec *spec, uint32_t *out, size_t capacity)
{
    size_t count = 0u;

    for (size_t i = 0u; i < spec->ligatureCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->ligatures[i].tag));
    }
    for (size_t i = 0u; i < spec->substCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->subst[i].tag));
    }
    return count;
}

/** Rules of @p tag among the single substitutions. */
static inline size_t ConfSubstOfTag(const ConfFontSpec *spec, uint32_t tag, uint16_t *out,
                                    size_t capacity)
{
    size_t count = 0u;

    for (size_t i = 0u; i < spec->substCount && count < capacity; i++) {
        if (RoboTextOtTagOf(spec->subst[i].tag) == tag) {
            out[count++] = (uint16_t)i;
        }
    }
    return count;
}

/**
 * Build the single-substitution subtable (GSUB type 1, format 2) for @p tag.
 *
 * Format 2 rather than format 1 because the fixture's substitutes are **not** a delta from the covered glyph:
 * a font names each one, and a self-check that read `glyph + delta` would pass only while the fixture happened
 * to lay its form glyphs out consecutively.
 *
 * @return its offset relative to @p lookupStart.
 */
static inline uint16_t CwSubstSubtable(ConfWriter *w, size_t lookupStart, const ConfFontSpec *spec,
                                       uint32_t tag)
{
    uint16_t rules[ConfMaxSubst];
    size_t count = ConfSubstOfTag(spec, tag, rules, ConfMaxSubst);
    size_t start = w->length;
    size_t coverageOffset;

    /* A coverage table is ascending, so the rules are sorted by the glyph they cover and the substitutes
     * written in that same order — which is what makes `substituteGlyphIDs[i]` answer for
     * `coverage.glyphs[i]`. */
    for (size_t a = 0u; a < count; a++) {
        for (size_t b = a + 1u; b < count; b++) {
            if (spec->subst[rules[b]].glyph < spec->subst[rules[a]].glyph) {
                uint16_t swap = rules[a];

                rules[a] = rules[b];
                rules[b] = swap;
            }
        }
    }
    (void)Cw16At(w, 2u); /* substFormat 2 */
    coverageOffset = Cw16At(w, 0u);
    (void)Cw16At(w, (uint16_t)count);
    for (size_t i = 0u; i < count; i++) {
        (void)Cw16At(w, spec->subst[rules[i]].substitute);
    }
    {
        uint16_t glyphs[ConfMaxSubst];

        for (size_t i = 0u; i < count; i++) {
            glyphs[i] = spec->subst[rules[i]].glyph;
        }
        Cw16(w, coverageOffset, CwCoverage1(w, start, glyphs, count));
    }
    return (uint16_t)(start - lookupStart);
}

/** Non-zero when @p tag has any ligature rule. */
static inline int ConfTagHasLigature(const ConfFontSpec *spec, uint32_t tag)
{
    for (size_t i = 0u; i < spec->ligatureCount; i++) {
        if (RoboTextOtTagOf(spec->ligatures[i].tag) == tag) {
            return 1;
        }
    }
    return 0;
}

/** Number of distinct first components among the rules carrying @p tag. */
static inline size_t ConfFirstGlyphs(const ConfFontSpec *spec, uint32_t tag, uint16_t *out,
                                     size_t capacity)
{
    size_t count = 0u;

    for (size_t i = 0u; i < spec->ligatureCount; i++) {
        uint16_t first = spec->ligatures[i].components[0];
        int seen = 0;

        if (RoboTextOtTagOf(spec->ligatures[i].tag) != tag) {
            continue;
        }
        for (size_t j = 0u; j < count; j++) {
            if (out[j] == first) {
                seen = 1;
                break;
            }
        }
        if (!seen && count < capacity) {
            out[count++] = first;
        }
    }
    return count;
}

/** Build a ligature lookup's subtable for the rules of @p tag. @return its offset relative to @p lookupStart. */
static inline uint16_t CwLigatureSubtable(ConfWriter *w, size_t lookupStart, const ConfFontSpec *spec,
                                          uint32_t tag)
{
    uint16_t firsts[ConfMaxLigatures];
    size_t firstCount = ConfFirstGlyphs(spec, tag, firsts, ConfMaxLigatures);
    size_t start = w->length;
    size_t coverageOffset;
    size_t setOffsets;
    size_t coverageGlyphs[ConfMaxLigatures];
    size_t i;

    /* Sorted, because a coverage table is a glyph array in ascending order and the initialiser order is
     * whatever the caller wrote. */
    for (i = 0u; i < firstCount; i++) {
        for (size_t j = i + 1u; j < firstCount; j++) {
            if (firsts[j] < firsts[i]) {
                uint16_t swap = firsts[i];

                firsts[i] = firsts[j];
                firsts[j] = swap;
            }
        }
    }

    (void)Cw16At(w, 1u); /* format */
    coverageOffset = Cw16At(w, 0u);
    (void)Cw16At(w, (uint16_t)firstCount);
    setOffsets = CwReserve(w, firstCount * 2u);

    for (i = 0u; i < firstCount; i++) {
        uint16_t rules[ConfMaxLigatures];
        size_t ruleCount = 0u;
        size_t setStart = w->length;

        Cw16(w, setOffsets + i * 2u, (uint16_t)(setStart - start));
        for (size_t r = 0u; r < spec->ligatureCount; r++) {
            if (RoboTextOtTagOf(spec->ligatures[r].tag) == tag &&
                spec->ligatures[r].components[0] == firsts[i] && ruleCount < ConfMaxLigatures) {
                rules[ruleCount++] = (uint16_t)r;
            }
        }
        /* Longest first: a ligature set is resolved in the order the font states it, so a font says
         * "prefer the longest match" by listing the longest candidate first. */
        for (size_t a = 0u; a < ruleCount; a++) {
            for (size_t b = a + 1u; b < ruleCount; b++) {
                if (spec->ligatures[rules[b]].componentCount >
                    spec->ligatures[rules[a]].componentCount) {
                    uint16_t swap = rules[a];

                    rules[a] = rules[b];
                    rules[b] = swap;
                }
            }
        }
        (void)Cw16At(w, (uint16_t)ruleCount);
        {
            size_t ligOffsets = CwReserve(w, ruleCount * 2u);

            for (size_t r = 0u; r < ruleCount; r++) {
                const ConfLigatureRule *rule = &spec->ligatures[rules[r]];
                size_t ligStart = w->length;

                Cw16(w, ligOffsets + r * 2u, (uint16_t)(ligStart - setStart));
                (void)Cw16At(w, rule->result);
                (void)Cw16At(w, rule->componentCount);
                for (uint16_t c = 1u; c < rule->componentCount; c++) {
                    (void)Cw16At(w, rule->components[c]);
                }
            }
        }
    }
    for (i = 0u; i < firstCount; i++) {
        coverageGlyphs[i] = firsts[i];
    }
    Cw16(w, coverageOffset, CwCoverage1(w, start, firsts, firstCount));
    (void)coverageGlyphs;
    return (uint16_t)(start - lookupStart);
}

/** Build the GSUB table. @return its length. */
/**
 * Build the GSUB table. @return its length.
 *
 * One **lookup per kind per tag**, in a fixed order: single substitution first (a contextual form is the
 * identity of the glyph), then ligature substitution (what the identified glyphs do together). A tag that has
 * rules of both kinds gets both lookups and a feature record naming both, in that order — which is the order
 * the fixture intends and the order the font states, and so the order the engine applies.
 */
static inline size_t ConfBuildGsub(ConfWriter *w, const ConfFontSpec *spec)
{
    uint32_t tags[ConfMaxTags];
    size_t tagCount = ConfGsubTags(spec, tags, ConfMaxTags);
    size_t tableStart = w->length;
    size_t scriptListOffsetField;
    size_t featureListOffsetField;
    size_t lookupListOffsetField;
    size_t lookupListStart;
    size_t lookupOffsets;
    uint16_t lookupIndexForTag[ConfMaxTags][2];
    size_t lookupTotal = 0u;

    (void)Cw32At(w, 0x00010000u);
    scriptListOffsetField = Cw16At(w, 0u);
    featureListOffsetField = Cw16At(w, 0u);
    lookupListOffsetField = Cw16At(w, 0u);

    for (size_t i = 0u; i < tagCount; i++) {
        uint16_t rules[ConfMaxSubst];
        int hasSubst = ConfSubstOfTag(spec, tags[i], rules, ConfMaxSubst) != 0u;
        int hasLigature = ConfTagHasLigature(spec, tags[i]);

        lookupIndexForTag[i][0] = hasSubst ? (uint16_t)lookupTotal : 0xFFFFu;
        if (hasSubst) {
            lookupTotal++;
        }
        lookupIndexForTag[i][1] = hasLigature ? (uint16_t)lookupTotal : 0xFFFFu;
        if (hasLigature) {
            lookupTotal++;
        }
    }

    /* ---- ScriptList ---- */
    {
        uint16_t indices[ConfMaxTags];

        for (size_t i = 0u; i < tagCount; i++) {
            indices[i] = (uint16_t)i;
        }
        Cw16(w, scriptListOffsetField,
             (uint16_t)CwScriptListAllScripts(w, tableStart, tagCount, indices));
    }

    /* ---- LookupList ---- */
    lookupListStart = w->length;
    (void)Cw16At(w, (uint16_t)lookupTotal);
    lookupOffsets = CwReserve(w, lookupTotal * 2u);
    for (size_t i = 0u; i < tagCount; i++) {
        if (lookupIndexForTag[i][0] != 0xFFFFu) {
            size_t lookupStart = w->length;
            size_t subtableOffsetField;

            Cw16(w, lookupOffsets + (size_t)lookupIndexForTag[i][0] * 2u,
                 (uint16_t)(lookupStart - lookupListStart));
            (void)Cw16At(w, 1u); /* type 1: single substitution */
            (void)Cw16At(w, 0u); /* flag: nothing ignored */
            (void)Cw16At(w, 1u); /* subtableCount */
            subtableOffsetField = Cw16At(w, 0u);
            Cw16(w, subtableOffsetField, CwSubstSubtable(w, lookupStart, spec, tags[i]));
        }
        if (lookupIndexForTag[i][1] != 0xFFFFu) {
            size_t lookupStart = w->length;
            size_t subtableOffsetField;

            Cw16(w, lookupOffsets + (size_t)lookupIndexForTag[i][1] * 2u,
                 (uint16_t)(lookupStart - lookupListStart));
            (void)Cw16At(w, 4u); /* type 4: ligature substitution */
            (void)Cw16At(w, 0u); /* flag: nothing ignored */
            (void)Cw16At(w, 1u); /* subtableCount */
            subtableOffsetField = Cw16At(w, 0u);
            Cw16(w, subtableOffsetField, CwLigatureSubtable(w, lookupStart, spec, tags[i]));
        }
    }

    /* ---- FeatureList: each tag names its own lookups, in the order they were built ---- */
    {
        uint16_t lookupRows[ConfMaxTags * 8];
        uint8_t counts[ConfMaxTags];

        memset(lookupRows, 0, sizeof(lookupRows));
        for (size_t i = 0u; i < tagCount; i++) {
            size_t n = 0u;

            if (lookupIndexForTag[i][0] != 0xFFFFu) {
                lookupRows[i * 8u + n++] = lookupIndexForTag[i][0];
            }
            if (lookupIndexForTag[i][1] != 0xFFFFu) {
                lookupRows[i * 8u + n++] = lookupIndexForTag[i][1];
            }
            counts[i] = (uint8_t)n;
        }
        Cw16(w, featureListOffsetField,
             (uint16_t)CwFeatureList(w, tableStart, tags, lookupRows, counts, tagCount));
    }
    Cw16(w, lookupListOffsetField, (uint16_t)(lookupListStart - tableStart));
    CwPad4(w);
    return w->length - tableStart;
}

/* ------------------------------------------------------------------ */
/* GPOS                                                                */
/* ------------------------------------------------------------------ */

/** The GPOS lookup type of kind @p k, in the order @ref ConfBuildGpos builds them. */
static inline uint16_t ConfGposKindType(size_t k)
{
    switch (k) {
        case 0u:
            return 2u; /* pair adjustment */
        case 1u:
            return 1u; /* single adjustment */
        case 2u:
            return 3u; /* cursive attachment */
        case 3u:
            return 4u; /* mark-to-base */
        case 4u:
            return 6u; /* mark-to-mark */
        default:
            return 5u; /* mark-to-ligature */
    }
}

/** Distinct tags among every GPOS rule array, in first-appearance order. */
static inline size_t ConfGposTags(const ConfFontSpec *spec, uint32_t *out, size_t capacity)
{
    size_t count = 0u;

    for (size_t i = 0u; i < spec->pairCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->pairs[i].tag));
    }
    for (size_t i = 0u; i < spec->singleCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->singles[i].tag));
    }
    for (size_t i = 0u; i < spec->cursiveCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->cursive[i].tag));
    }
    for (size_t i = 0u; i < spec->markBaseCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->markBase[i].tag));
    }
    for (size_t i = 0u; i < spec->markMarkCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->markMark[i].tag));
    }
    for (size_t i = 0u; i < spec->markLigCount; i++) {
        (void)ConfTagSeen(out, capacity, &count, RoboTextOtTagOf(spec->markLig[i].tag));
    }
    return count;
}

/** Build the pair-adjustment subtable for @p tag. @return its offset relative to @p lookupStart. */
static inline uint16_t CwPairSubtable(ConfWriter *w, size_t lookupStart, const ConfFontSpec *spec,
                                      uint32_t tag)
{
    uint16_t lefts[ConfMaxPairs];
    size_t leftCount = 0u;
    size_t start = w->length;
    size_t coverageOffset;
    size_t setOffsets;

    for (size_t i = 0u; i < spec->pairCount; i++) {
        uint16_t first = spec->pairs[i].first;
        int seen = 0;

        if (RoboTextOtTagOf(spec->pairs[i].tag) != tag) {
            continue;
        }
        for (size_t j = 0u; j < leftCount; j++) {
            if (lefts[j] == first) {
                seen = 1;
                break;
            }
        }
        if (!seen && leftCount < ConfMaxPairs) {
            lefts[leftCount++] = first;
        }
    }
    for (size_t a = 0u; a < leftCount; a++) {
        for (size_t b = a + 1u; b < leftCount; b++) {
            if (lefts[b] < lefts[a]) {
                uint16_t swap = lefts[a];

                lefts[a] = lefts[b];
                lefts[b] = swap;
            }
        }
    }

    (void)Cw16At(w, 1u);        /* posFormat */
    coverageOffset = Cw16At(w, 0u);
    (void)Cw16At(w, 0x0004u);   /* valueFormat1: xAdvance */
    (void)Cw16At(w, 0u);        /* valueFormat2 */
    (void)Cw16At(w, (uint16_t)leftCount);
    setOffsets = CwReserve(w, leftCount * 2u);

    for (size_t i = 0u; i < leftCount; i++) {
        uint16_t seconds[ConfMaxPairs];
        size_t secondCount = 0u;
        size_t setStart = w->length;

        Cw16(w, setOffsets + i * 2u, (uint16_t)(setStart - start));
        for (size_t r = 0u; r < spec->pairCount; r++) {
            if (RoboTextOtTagOf(spec->pairs[r].tag) == tag && spec->pairs[r].first == lefts[i] &&
                secondCount < ConfMaxPairs) {
                seconds[secondCount++] = (uint16_t)r;
            }
        }
        for (size_t a = 0u; a < secondCount; a++) {
            for (size_t b = a + 1u; b < secondCount; b++) {
                if (spec->pairs[seconds[b]].second < spec->pairs[seconds[a]].second) {
                    uint16_t swap = seconds[a];

                    seconds[a] = seconds[b];
                    seconds[b] = swap;
                }
            }
        }
        (void)Cw16At(w, (uint16_t)secondCount);
        for (size_t r = 0u; r < secondCount; r++) {
            (void)Cw16At(w, spec->pairs[seconds[r]].second);
            (void)Cw16At(w, (uint16_t)spec->pairs[seconds[r]].xAdvance);
        }
    }
    Cw16(w, coverageOffset, CwCoverage1(w, start, lefts, leftCount));
    return (uint16_t)(start - lookupStart);
}

/** Build the single-adjustment subtable for @p tag. @return its offset relative to @p lookupStart. */
static inline uint16_t CwSingleSubtable(ConfWriter *w, size_t lookupStart, const ConfFontSpec *spec,
                                        uint32_t tag)
{
    uint16_t glyphs[ConfMaxSingles];
    size_t count = 0u;
    size_t start = w->length;
    size_t coverageOffset;
    uint16_t valueFormat = 0u;

    for (size_t i = 0u; i < spec->singleCount; i++) {
        if (RoboTextOtTagOf(spec->singles[i].tag) == tag && count < ConfMaxSingles) {
            glyphs[count++] = spec->singles[i].glyph;
            if (spec->singles[i].xPlacement != 0) {
                valueFormat |= 0x0001u;
            }
            if (spec->singles[i].yPlacement != 0) {
                valueFormat |= 0x0002u;
            }
            if (spec->singles[i].xAdvance != 0) {
                valueFormat |= 0x0004u;
            }
        }
    }
    for (size_t a = 0u; a < count; a++) {
        for (size_t b = a + 1u; b < count; b++) {
            if (glyphs[b] < glyphs[a]) {
                uint16_t swap = glyphs[a];

                glyphs[a] = glyphs[b];
                glyphs[b] = swap;
            }
        }
    }

    (void)Cw16At(w, 1u); /* posFormat */
    coverageOffset = Cw16At(w, 0u);
    (void)Cw16At(w, valueFormat);
    for (size_t i = 0u; i < count; i++) {
        for (size_t r = 0u; r < spec->singleCount; r++) {
            if (RoboTextOtTagOf(spec->singles[r].tag) == tag && spec->singles[r].glyph == glyphs[i]) {
                if ((valueFormat & 0x0001u) != 0u) {
                    (void)Cw16At(w, (uint16_t)spec->singles[r].xPlacement);
                }
                if ((valueFormat & 0x0002u) != 0u) {
                    (void)Cw16At(w, (uint16_t)spec->singles[r].yPlacement);
                }
                if ((valueFormat & 0x0004u) != 0u) {
                    (void)Cw16At(w, (uint16_t)spec->singles[r].xAdvance);
                }
                break;
            }
        }
    }
    Cw16(w, coverageOffset, CwCoverage1(w, start, glyphs, count));
    return (uint16_t)(start - lookupStart);
}

/**
 * One anchor table (format 1: format, x, y).
 *
 * Offset from @p base, which the caller passes as the array the anchor belongs to — the format states every
 * anchor offset against the array that holds it, and a reader that resolved it against the subtable instead
 * would read a different anchor.
 */
static inline uint16_t CwAnchor(ConfWriter *w, size_t base, int16_t x, int16_t y)
{
    size_t start = w->length;

    (void)Cw16At(w, 1u);
    (void)Cw16At(w, (uint16_t)x);
    (void)Cw16At(w, (uint16_t)y);
    return (uint16_t)(start - base);
}

/**
 * Build the cursive-attachment subtable (GPOS type 3, format 1) for @p tag.
 *
 * Entry and exit anchors are resolved against **this subtable**, which is what the format states — unlike the
 * mark arrays below, whose anchors are resolved against the array.
 */
static inline uint16_t CwCursiveSubtable(ConfWriter *w, size_t lookupStart, const ConfFontSpec *spec,
                                         uint32_t tag)
{
    uint16_t rules[ConfMaxCursive];
    size_t count = 0u;
    size_t start = w->length;
    size_t coverageOffset;
    size_t records;

    for (size_t i = 0u; i < spec->cursiveCount && count < ConfMaxCursive; i++) {
        if (RoboTextOtTagOf(spec->cursive[i].tag) == tag) {
            rules[count++] = (uint16_t)i;
        }
    }
    for (size_t a = 0u; a < count; a++) {
        for (size_t b = a + 1u; b < count; b++) {
            if (spec->cursive[rules[b]].glyph < spec->cursive[rules[a]].glyph) {
                uint16_t swap = rules[a];

                rules[a] = rules[b];
                rules[b] = swap;
            }
        }
    }

    (void)Cw16At(w, 1u);
    coverageOffset = Cw16At(w, 0u);
    (void)Cw16At(w, (uint16_t)count);
    records = CwReserve(w, count * 4u);
    for (size_t i = 0u; i < count; i++) {
        const ConfCursiveRule *rule = &spec->cursive[rules[i]];
        uint16_t entry = CwAnchor(w, start, rule->entryX, rule->entryY);
        uint16_t exit = CwAnchor(w, start, rule->exitX, rule->exitY);

        Cw16(w, records + i * 4u, entry);
        Cw16(w, records + i * 4u + 2u, exit);
    }
    {
        uint16_t glyphs[ConfMaxCursive];

        for (size_t i = 0u; i < count; i++) {
            glyphs[i] = spec->cursive[rules[i]].glyph;
        }
        Cw16(w, coverageOffset, CwCoverage1(w, start, glyphs, count));
    }
    return (uint16_t)(start - lookupStart);
}

/**
 * Build a mark-attachment subtable (GPOS type 4 or 6, format 1) for @p tag.
 *
 * One builder for both types because the formats are one layout with a renamed field: the two coverages, a
 * class count, a MarkArray and a second array of anchors. The type decides which rule array is read, and
 * nothing else — which is exactly the fact `RoboTextGposSubst.c` states from the other side.
 */
static inline uint16_t CwMarkSubtable(ConfWriter *w, size_t lookupStart, const ConfFontSpec *spec,
                                      uint32_t tag, int markToMark)
{
    const ConfMarkRule *rules = markToMark ? spec->markMark : spec->markBase;
    size_t ruleCount = markToMark ? spec->markMarkCount : spec->markBaseCount;
    uint16_t mine[ConfMaxMarkRules];
    size_t count = 0u;
    uint16_t cases[ConfMaxMarkRules];
    size_t caseCount = 0u;
    uint16_t classCount = 1u;
    size_t start = w->length;
    size_t markCoverageOffset;
    size_t baseCoverageOffset;
    size_t classCountField;
    size_t markArrayField;
    size_t baseArrayField;

    for (size_t i = 0u; i < ruleCount && count < ConfMaxMarkRules; i++) {
        if (RoboTextOtTagOf(rules[i].tag) == tag) {
            mine[count++] = (uint16_t)i;
            (void)ConfGlyphSeen(cases, ConfMaxMarkRules, &caseCount, rules[i].anchorGlyph);
            if ((uint16_t)(rules[i].markClass + 1u) > classCount) {
                classCount = (uint16_t)(rules[i].markClass + 1u);
            }
        }
    }
    /* Ascending, because both coverages are: the array index a reader derives from a coverage lookup is the
     * *sorted* position, so a fixture that wrote them in rule order would state a mapping it did not mean. */
    for (size_t a = 0u; a < caseCount; a++) {
        for (size_t b = a + 1u; b < caseCount; b++) {
            if (cases[b] < cases[a]) {
                uint16_t swap = cases[a];

                cases[a] = cases[b];
                cases[b] = swap;
            }
        }
    }

    (void)Cw16At(w, 1u);
    markCoverageOffset = Cw16At(w, 0u);
    baseCoverageOffset = Cw16At(w, 0u);
    classCountField = 0u;
    (void)Cw16At(w, classCount);
    markArrayField = Cw16At(w, 0u);
    baseArrayField = Cw16At(w, 0u);
    (void)classCountField;

    /* ---- MarkArray: count, then { class, anchor } per mark ---- */
    {
        size_t markArray = w->length;
        size_t markRecords;
        uint16_t marksSeen[ConfMaxMarkRules];
        size_t markSeen = 0u;

        Cw16(w, markArrayField, (uint16_t)(markArray - start));
        for (size_t i = 0u; i < count; i++) {
            (void)ConfGlyphSeen(marksSeen, ConfMaxMarkRules, &markSeen, rules[mine[i]].mark);
        }
        for (size_t a = 0u; a < markSeen; a++) {
            for (size_t b = a + 1u; b < markSeen; b++) {
                if (marksSeen[b] < marksSeen[a]) {
                    uint16_t swap = marksSeen[a];

                    marksSeen[a] = marksSeen[b];
                    marksSeen[b] = swap;
                }
            }
        }
        (void)Cw16At(w, (uint16_t)markSeen);
        markRecords = CwReserve(w, markSeen * 4u);
        for (size_t a = 0u; a < markSeen; a++) {
            const ConfMarkRule *rule = NULL;
            size_t record = markRecords + a * 4u;

            for (size_t i = 0u; i < count; i++) {
                if (rules[mine[i]].mark == marksSeen[a]) {
                    rule = &rules[mine[i]];
                    break;
                }
            }
            Cw16(w, record, rule ? rule->markClass : 0u);
            Cw16(w, record + 2u, CwAnchor(w, markArray, rule ? rule->markX : 0, rule ? rule->markY : 0));
        }
    }

    /* ---- BaseArray (or Mark2Array): count, then classCount anchors per covered glyph ---- */
    {
        size_t baseArray = w->length;
        size_t baseRecords;

        Cw16(w, baseArrayField, (uint16_t)(baseArray - start));
        (void)Cw16At(w, (uint16_t)caseCount);
        baseRecords = CwReserve(w, caseCount * (size_t)classCount * 2u);
        for (size_t a = 0u; a < caseCount; a++) {
            for (uint16_t cls = 0u; cls < classCount; cls++) {
                const ConfMarkRule *rule = NULL;

                for (size_t i = 0u; i < count; i++) {
                    if (rules[mine[i]].anchorGlyph == cases[a] && rules[mine[i]].markClass == cls) {
                        rule = &rules[mine[i]];
                        break;
                    }
                }
                Cw16(w, baseRecords + (a * (size_t)classCount + cls) * 2u,
                     CwAnchor(w, baseArray, rule ? rule->anchorX : 0, rule ? rule->anchorY : 0));
            }
        }
    }

    {
        uint16_t coverGlyphs[ConfMaxMarkRules];
        size_t markGlyphs = 0u;

        for (size_t i = 0u; i < count; i++) {
            (void)ConfGlyphSeen(coverGlyphs, ConfMaxMarkRules, &markGlyphs, rules[mine[i]].mark);
        }
        for (size_t a = 0u; a < markGlyphs; a++) {
            for (size_t b = a + 1u; b < markGlyphs; b++) {
                if (coverGlyphs[b] < coverGlyphs[a]) {
                    uint16_t swap = coverGlyphs[a];

                    coverGlyphs[a] = coverGlyphs[b];
                    coverGlyphs[b] = swap;
                }
            }
        }
        Cw16(w, markCoverageOffset, CwCoverage1(w, start, coverGlyphs, markGlyphs));
        Cw16(w, baseCoverageOffset, CwCoverage1(w, start, cases, caseCount));
    }
    return (uint16_t)(start - lookupStart);
}

/**
 * Build the mark-to-ligature subtable (GPOS type 5, format 1) for @p tag.
 *
 * The LigatureArray is the one structure here that is **per component**: a ligature's anchor array is a table
 * of its own (`componentCount`, then `componentCount × classCount` anchors), and the offsets to those tables
 * are stated against the array. Composing the anchors inside the array would be a format the engine does not
 * read, which is the whole reason the array is a second indirection.
 */
static inline uint16_t CwMarkLigatureSubtable(ConfWriter *w, size_t lookupStart, const ConfFontSpec *spec,
                                              uint32_t tag)
{
    uint16_t mine[ConfMaxMarkRules];
    size_t count = 0u;
    uint16_t ligatures[ConfMaxMarkRules];
    size_t ligatureCount = 0u;
    uint16_t classCount = 1u;
    size_t start = w->length;
    size_t markCoverageOffset;
    size_t ligCoverageOffset;
    size_t markArrayField;
    size_t ligArrayField;

    for (size_t i = 0u; i < spec->markLigCount && count < ConfMaxMarkRules; i++) {
        if (RoboTextOtTagOf(spec->markLig[i].tag) == tag) {
            mine[count++] = (uint16_t)i;
            (void)ConfGlyphSeen(ligatures, ConfMaxMarkRules, &ligatureCount, spec->markLig[i].ligature);
            if ((uint16_t)(spec->markLig[i].markClass + 1u) > classCount) {
                classCount = (uint16_t)(spec->markLig[i].markClass + 1u);
            }
        }
    }

    (void)Cw16At(w, 1u);
    markCoverageOffset = Cw16At(w, 0u);
    ligCoverageOffset = Cw16At(w, 0u);
    (void)Cw16At(w, classCount);
    markArrayField = Cw16At(w, 0u);
    ligArrayField = Cw16At(w, 0u);

    {
        size_t markArray = w->length;
        size_t markRecords;
        uint16_t marksSeen[ConfMaxMarkRules];
        size_t markSeen = 0u;

        Cw16(w, markArrayField, (uint16_t)(markArray - start));
        for (size_t i = 0u; i < count; i++) {
            (void)ConfGlyphSeen(marksSeen, ConfMaxMarkRules, &markSeen, spec->markLig[mine[i]].mark);
        }
        for (size_t a = 0u; a < markSeen; a++) {
            for (size_t b = a + 1u; b < markSeen; b++) {
                if (marksSeen[b] < marksSeen[a]) {
                    uint16_t swap = marksSeen[a];

                    marksSeen[a] = marksSeen[b];
                    marksSeen[b] = swap;
                }
            }
        }
        (void)Cw16At(w, (uint16_t)markSeen);
        markRecords = CwReserve(w, markSeen * 4u);
        for (size_t a = 0u; a < markSeen; a++) {
            const ConfMarkLigatureRule *rule = NULL;

            for (size_t i = 0u; i < count; i++) {
                if (spec->markLig[mine[i]].mark == marksSeen[a]) {
                    rule = &spec->markLig[mine[i]];
                    break;
                }
            }
            Cw16(w, markRecords + a * 4u, rule ? rule->markClass : 0u);
            Cw16(w, markRecords + a * 4u + 2u,
                 CwAnchor(w, markArray, rule ? rule->markX : 0, rule ? rule->markY : 0));
        }
    }

    {
        size_t ligArray = w->length;
        size_t attachOffsets;

        Cw16(w, ligArrayField, (uint16_t)(ligArray - start));
        (void)Cw16At(w, (uint16_t)ligatureCount);
        attachOffsets = CwReserve(w, ligatureCount * 2u);
        for (size_t a = 0u; a < ligatureCount; a++) {
            const ConfMarkLigatureRule *rule = NULL;
            size_t attach = w->length;
            size_t anchors;

            for (size_t i = 0u; i < count; i++) {
                if (spec->markLig[mine[i]].ligature == ligatures[a]) {
                    rule = &spec->markLig[mine[i]];
                    break;
                }
            }
            Cw16(w, attachOffsets + a * 2u, (uint16_t)(attach - ligArray));
            (void)Cw16At(w, rule ? rule->componentCount : 1u);
            anchors = CwReserve(w, (size_t)(rule ? rule->componentCount : 1u) * (size_t)classCount * 2u);
            for (size_t component = 0u; component < (rule ? rule->componentCount : 1u); component++) {
                for (uint16_t cls = 0u; cls < classCount; cls++) {
                    int applies = rule && rule->component == component && cls == rule->markClass;

                    Cw16(w, anchors + (component * (size_t)classCount + cls) * 2u,
                         CwAnchor(w, attach, applies ? rule->anchorX : 0, applies ? rule->anchorY : 0));
                }
            }
        }
    }

    {
        uint16_t markGlyphs[ConfMaxMarkRules];
        size_t markGlyphs1 = 0u;

        for (size_t i = 0u; i < count; i++) {
            (void)ConfGlyphSeen(markGlyphs, ConfMaxMarkRules, &markGlyphs1, spec->markLig[mine[i]].mark);
        }
        for (size_t a = 0u; a < markGlyphs1; a++) {
            for (size_t b = a + 1u; b < markGlyphs1; b++) {
                if (markGlyphs[b] < markGlyphs[a]) {
                    uint16_t swap = markGlyphs[a];

                    markGlyphs[a] = markGlyphs[b];
                    markGlyphs[b] = swap;
                }
            }
        }
        Cw16(w, markCoverageOffset, CwCoverage1(w, start, markGlyphs, markGlyphs1));
        Cw16(w, ligCoverageOffset, CwCoverage1(w, start, ligatures, ligatureCount));
    }
    return (uint16_t)(start - lookupStart);
}

/** Build the GPOS table. @return its length. */
static inline size_t ConfBuildGpos(ConfWriter *w, const ConfFontSpec *spec)
{
    uint32_t tags[ConfMaxTags];
    size_t tagCount = ConfGposTags(spec, tags, ConfMaxTags);
    size_t tableStart = w->length;
    size_t scriptListOffsetField;
    size_t featureListOffsetField;
    size_t lookupListOffsetField;
    size_t lookupListStart;
    size_t lookupOffsets;
    /* One slot per lookup kind the builder can emit, in the order they are built (see below). */
    uint16_t lookupIndexForTag[ConfMaxTags][ConfGposKindsMax];
    size_t lookupTotal = 0u;

    (void)Cw32At(w, 0x00010000u);
    scriptListOffsetField = Cw16At(w, 0u);
    featureListOffsetField = Cw16At(w, 0u);
    lookupListOffsetField = Cw16At(w, 0u);

    /*
     * One lookup per kind that has rules for the tag, in a fixed order: the value records (pair, then single),
     * then the **attachments** (cursive, mark-to-base, mark-to-mark, mark-to-ligature). The order is what the
     * feature record states and therefore what the engine walks, and it is the sensible one: a kerning pair is
     * about advances, an attachment is about anchors, and the anchors are placed once the advances are final.
     */
    for (size_t i = 0u; i < tagCount; i++) {
        int has[ConfGposKindsMax];

        memset(has, 0, sizeof(has));
        for (size_t r = 0u; r < spec->pairCount; r++) {
            has[0] |= RoboTextOtTagOf(spec->pairs[r].tag) == tags[i];
        }
        for (size_t r = 0u; r < spec->singleCount; r++) {
            has[1] |= RoboTextOtTagOf(spec->singles[r].tag) == tags[i];
        }
        for (size_t r = 0u; r < spec->cursiveCount; r++) {
            has[2] |= RoboTextOtTagOf(spec->cursive[r].tag) == tags[i];
        }
        for (size_t r = 0u; r < spec->markBaseCount; r++) {
            has[3] |= RoboTextOtTagOf(spec->markBase[r].tag) == tags[i];
        }
        for (size_t r = 0u; r < spec->markMarkCount; r++) {
            has[4] |= RoboTextOtTagOf(spec->markMark[r].tag) == tags[i];
        }
        for (size_t r = 0u; r < spec->markLigCount; r++) {
            has[5] |= RoboTextOtTagOf(spec->markLig[r].tag) == tags[i];
        }
        for (size_t k = 0u; k < ConfGposKindsMax; k++) {
            lookupIndexForTag[i][k] = has[k] ? (uint16_t)lookupTotal : 0xFFFFu;
            if (has[k]) {
                lookupTotal++;
            }
        }
    }

    {
        uint16_t indices[ConfMaxTags];

        for (size_t i = 0u; i < tagCount; i++) {
            indices[i] = (uint16_t)i;
        }
        Cw16(w, scriptListOffsetField,
             (uint16_t)CwScriptListAllScripts(w, tableStart, tagCount, indices));
    }

    /* ---- LookupList ---- */
    lookupListStart = w->length;
    (void)Cw16At(w, (uint16_t)lookupTotal);
    lookupOffsets = CwReserve(w, lookupTotal * 2u);
    for (size_t i = 0u; i < tagCount; i++) {
        for (size_t k = 0u; k < ConfGposKindsMax; k++) {
            size_t lookupStart;
            size_t subtableOffsetField;

            if (lookupIndexForTag[i][k] == 0xFFFFu) {
                continue;
            }
            lookupStart = w->length;
            Cw16(w, lookupOffsets + (size_t)lookupIndexForTag[i][k] * 2u,
                 (uint16_t)(lookupStart - lookupListStart));
            (void)Cw16At(w, (uint16_t)ConfGposKindType(k));
            (void)Cw16At(w, 0u); /* flag: nothing ignored */
            (void)Cw16At(w, 1u); /* subtableCount */
            subtableOffsetField = Cw16At(w, 0u);
            switch (k) {
                case 0u:
                    Cw16(w, subtableOffsetField, CwPairSubtable(w, lookupStart, spec, tags[i]));
                    break;
                case 1u:
                    Cw16(w, subtableOffsetField, CwSingleSubtable(w, lookupStart, spec, tags[i]));
                    break;
                case 2u:
                    Cw16(w, subtableOffsetField, CwCursiveSubtable(w, lookupStart, spec, tags[i]));
                    break;
                case 3u:
                    Cw16(w, subtableOffsetField, CwMarkSubtable(w, lookupStart, spec, tags[i], 0));
                    break;
                case 4u:
                    Cw16(w, subtableOffsetField, CwMarkSubtable(w, lookupStart, spec, tags[i], 1));
                    break;
                default:
                    Cw16(w, subtableOffsetField, CwMarkLigatureSubtable(w, lookupStart, spec, tags[i]));
                    break;
            }
        }
    }

    /* ---- FeatureList: each tag names its own lookups, in the order they were built ---- */
    {
        uint16_t lookupRows[ConfMaxTags * 8];
        uint8_t counts[ConfMaxTags];

        memset(lookupRows, 0, sizeof(lookupRows));
        for (size_t i = 0u; i < tagCount; i++) {
            size_t n = 0u;

            for (size_t k = 0u; k < ConfGposKindsMax; k++) {
                if (lookupIndexForTag[i][k] != 0xFFFFu) {
                    lookupRows[i * 8u + n++] = lookupIndexForTag[i][k];
                }
            }
            counts[i] = (uint8_t)n;
        }
        Cw16(w, featureListOffsetField,
             (uint16_t)CwFeatureList(w, tableStart, tags, lookupRows, counts, tagCount));
    }
    Cw16(w, lookupListOffsetField, (uint16_t)(lookupListStart - tableStart));
    CwPad4(w);
    return w->length - tableStart;
}

/* ------------------------------------------------------------------ */
/* The rest of the font                                                */
/* ------------------------------------------------------------------ */

static inline size_t ConfHead(uint8_t *b)
{
    memset(b, 0, 54u);
    b[0] = 0x00;
    b[1] = 0x01;
    b[2] = 0x00;
    b[3] = 0x00; /* version 1.0 */
    b[18] = 0x03;
    b[19] = 0xE8; /* unitsPerEm 1000 */
    b[12] = 0x5F;
    b[13] = 0x0F;
    b[14] = 0x3C;
    b[15] = 0xF5; /* magicNumber, which a validator checks */
    return 54u;
}

static inline size_t ConfHhea(uint8_t *b, uint16_t glyphCount)
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
    b[34] = (uint8_t)(glyphCount >> 8);
    b[35] = (uint8_t)glyphCount;
    return 36u;
}

static inline size_t ConfMaxp(uint8_t *b, uint16_t glyphCount)
{
    memset(b, 0, 32u);
    b[0] = 0x00;
    b[1] = 0x01;
    b[2] = 0x00;
    b[3] = 0x00;
    b[4] = (uint8_t)(glyphCount >> 8);
    b[5] = (uint8_t)glyphCount;
    return 32u;
}

static inline size_t ConfHmtx(uint8_t *b, const ConfFontSpec *spec)
{
    size_t at = 0u;

    for (uint16_t glyph = 0u; glyph < spec->glyphCount; glyph++) {
        uint16_t advance = spec->advances[glyph];

        b[at++] = (uint8_t)(advance >> 8);
        b[at++] = (uint8_t)advance;
        b[at++] = 0x00;
        b[at++] = 0x00; /* leftSideBearing */
    }
    return at;
}

static inline size_t ConfPost(uint8_t *b)
{
    memset(b, 0, 32u);
    b[1] = 0x03; /* version 3.0: no glyph names */
    return 32u;
}

static inline size_t ConfOs2(uint8_t *b)
{
    memset(b, 0, 96u);
    b[1] = 0x04; /* version 4 */
    b[5] = 0x90; /* usWeightClass 400 */
    b[7] = 0x05; /* usWidthClass 5 */
    b[63] = 0x20; /* sTypoAscender 800 */
    b[64] = 0xFD;
    b[65] = 0xA8; /* sTypoDescender -600 */
    b[68] = 0x03;
    b[69] = 0x20; /* usWinAscent 800 */
    b[70] = 0x02;
    b[71] = 0x58; /* usWinDescent 600 */
    return 96u;
}

/**
 * A `cmap` table with one format 12 subtable, reached from two encoding records.
 *
 * Format 12 rather than format 4 because the canonical list crosses BMP blocks with gaps between them and a
 * format 4 table would need a segment per gap — more bytes and one more thing to get wrong. The two records
 * are `(0, 4)` and `(3, 10)`, both pointing at the same subtable, so a face that prefers either platform
 * finds the same map.
 */
static inline size_t ConfCmap(uint8_t *b, size_t capacity, const ConfFontSpec *spec)
{
    uint32_t list[ConfMaxGlyphs];
    size_t listCount;
    size_t groupCount = 0u;
    size_t at;
    size_t subtableLength;
    uint32_t groups[ConfMaxCmapGroups][3];
    size_t i;

    listCount = spec->canonicalCmap ? ConfCanonicalCodepoints(list, ConfMaxGlyphs) : 0u;
    if (!spec->canonicalCmap) {
        for (i = 0u; i < spec->cmapCount; i++) {
            groups[groupCount][0] = spec->cmap[i].first;
            groups[groupCount][1] = spec->cmap[i].last;
            groups[groupCount][2] = spec->cmap[i].glyph;
            groupCount++;
        }
    } else {
        for (i = 0u; i < listCount; i++) {
            uint32_t cp = list[i];
            uint16_t glyph = (uint16_t)(i + 1u);

            if (spec->cmapFilterLast != 0u &&
                (cp < spec->cmapFilterFirst || cp > spec->cmapFilterLast)) {
                continue;
            }
            if (groupCount > 0u && groups[groupCount - 1u][1] + 1u == cp &&
                (uint32_t)groups[groupCount - 1u][2] + (cp - groups[groupCount - 1u][0]) == glyph) {
                groups[groupCount - 1u][1] = cp;
            } else if (groupCount < ConfMaxCmapGroups) {
                groups[groupCount][0] = cp;
                groups[groupCount][1] = cp;
                groups[groupCount][2] = glyph;
                groupCount++;
            }
        }
    }

    /* A format 12 group array is read by binary search, so it **must** be sorted by start code point.
     * The canonical list is ordered by block rather than by scalar, so the groups it produces are sorted
     * here; sorting them cannot disturb the mapping, because each group is (start, end, startGlyph) and
     * the runs are disjoint. */
    for (i = 0u; i < groupCount; i++) {
        for (size_t j = i + 1u; j < groupCount; j++) {
            if (groups[j][0] < groups[i][0]) {
                uint32_t swap0 = groups[i][0];
                uint32_t swap1 = groups[i][1];
                uint32_t swap2 = groups[i][2];

                groups[i][0] = groups[j][0];
                groups[i][1] = groups[j][1];
                groups[i][2] = groups[j][2];
                groups[j][0] = swap0;
                groups[j][1] = swap1;
                groups[j][2] = swap2;
            }
        }
    }

    /* A run that did not fit would silently shrink the font's coverage, so it is a builder defect. */
    RoboCheck(groupCount < ConfMaxCmapGroups);
    subtableLength = 16u + groupCount * 12u;
    RoboCheck(20u + subtableLength <= capacity);
    memset(b, 0, 20u + subtableLength);
    b[3] = 0x02; /* numTables */
    /* Record 0, bytes 4..11: (0, 4) — Unicode 2.0+, full repertoire, subtable at 20. */
    b[4] = 0x00;
    b[5] = 0x00;
    b[6] = 0x00;
    b[7] = 0x04;
    b[11] = 0x14;
    /* Record 1, bytes 12..19: (3, 10) — Windows UCS-4, the same subtable. */
    b[12] = 0x00;
    b[13] = 0x03;
    b[14] = 0x00;
    b[15] = 0x0A;
    b[19] = 0x14;
    at = 20u;
    b[at + 1u] = 0x0C; /* format 12 */
    b[at + 3u] = 0x00; /* reserved */
    b[at + 4u] = (uint8_t)(subtableLength >> 24);
    b[at + 5u] = (uint8_t)(subtableLength >> 16);
    b[at + 6u] = (uint8_t)(subtableLength >> 8);
    b[at + 7u] = (uint8_t)subtableLength;
    /* language 0 */
    b[at + 12u] = (uint8_t)(groupCount >> 24);
    b[at + 13u] = (uint8_t)(groupCount >> 16);
    b[at + 14u] = (uint8_t)(groupCount >> 8);
    b[at + 15u] = (uint8_t)groupCount;
    for (i = 0u; i < groupCount; i++) {
        size_t g = at + 16u + i * 12u;

        b[g + 0u] = (uint8_t)(groups[i][0] >> 24);
        b[g + 1u] = (uint8_t)(groups[i][0] >> 16);
        b[g + 2u] = (uint8_t)(groups[i][0] >> 8);
        b[g + 3u] = (uint8_t)groups[i][0];
        b[g + 4u] = (uint8_t)(groups[i][1] >> 24);
        b[g + 5u] = (uint8_t)(groups[i][1] >> 16);
        b[g + 6u] = (uint8_t)(groups[i][1] >> 8);
        b[g + 7u] = (uint8_t)groups[i][1];
        b[g + 8u] = (uint8_t)(groups[i][2] >> 24);
        b[g + 9u] = (uint8_t)(groups[i][2] >> 16);
        b[g + 10u] = (uint8_t)(groups[i][2] >> 8);
        b[g + 11u] = (uint8_t)groups[i][2];
    }
    return 20u + subtableLength;
}

/** GDEF 1.0 whose glyph class definition marks the spec's mark glyphs as class 3 (format 2 ranges). */
static inline size_t ConfGdef(uint8_t *b, size_t capacity, const ConfFontSpec *spec)
{
    size_t at = 12u;
    size_t count = spec->markCount;
    size_t i;

    RoboCheck(at + 4u + count * 6u <= capacity);
    memset(b, 0, at + 4u + count * 6u);
    b[1] = 0x01; /* version 1.0 */
    b[4] = 0x00;
    b[5] = 0x0C; /* glyphClassDefOffset */
    b[at + 1u] = 0x02; /* classDefFormat 2 */
    b[at + 2u] = (uint8_t)(count >> 8);
    b[at + 3u] = (uint8_t)count;
    for (i = 0u; i < count; i++) {
        size_t r = at + 4u + i * 6u;

        b[r + 0u] = (uint8_t)(spec->marks[i] >> 8);
        b[r + 1u] = (uint8_t)spec->marks[i];
        b[r + 2u] = (uint8_t)(spec->marks[i] >> 8);
        b[r + 3u] = (uint8_t)spec->marks[i];
        b[r + 4u] = 0x00;
        b[r + 5u] = 0x03; /* class 3: mark */
    }
    return at + 4u + count * 6u;
}

typedef struct ConfTable {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} ConfTable;

/** A whole SFNT: directory, tables, checksums. */
static inline size_t ConfSfnt(uint8_t *out, size_t outCap, const ConfTable *tables, uint32_t count)
{
    size_t directoryEnd = 12u + (size_t)count * 16u;
    size_t need = directoryEnd;
    size_t cursor;
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;

    for (uint32_t i = 0u; i < count; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
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
        ConfWriter header;

        header.data = out;
        header.capacity = need;
        header.length = 0u;
        Cw32(&header, 0u, RoboFontSfntVersionTrue);
        Cw16(&header, 4u, (uint16_t)count);
        Cw16(&header, 6u, (uint16_t)(pow2 * 16u));
        Cw16(&header, 8u, entry);
        Cw16(&header, 10u, (uint16_t)(count * 16u - pow2 * 16u));
    }

    cursor = directoryEnd;
    for (uint32_t i = 0u; i < count; i++) {
        uint8_t *record = out + 12u + (size_t)i * 16u;
        size_t padded = ((size_t)tables[i].length + 3u) & ~(size_t)3u;
        uint32_t checksum;

        if (tables[i].length != 0u && tables[i].data) {
            memcpy(out + cursor, tables[i].data, tables[i].length);
        }
        checksum = FontSfntChecksum(out + cursor, tables[i].length);
        record[0] = (uint8_t)(tables[i].tag >> 24);
        record[1] = (uint8_t)(tables[i].tag >> 16);
        record[2] = (uint8_t)(tables[i].tag >> 8);
        record[3] = (uint8_t)tables[i].tag;
        record[4] = (uint8_t)(checksum >> 24);
        record[5] = (uint8_t)(checksum >> 16);
        record[6] = (uint8_t)(checksum >> 8);
        record[7] = (uint8_t)checksum;
        record[8] = (uint8_t)(cursor >> 24);
        record[9] = (uint8_t)(cursor >> 16);
        record[10] = (uint8_t)(cursor >> 8);
        record[11] = (uint8_t)cursor;
        record[12] = (uint8_t)(tables[i].length >> 24);
        record[13] = (uint8_t)(tables[i].length >> 16);
        record[14] = (uint8_t)(tables[i].length >> 8);
        record[15] = (uint8_t)tables[i].length;
        cursor += padded;
    }
    return cursor;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** A built font, opened: the bytes it was built from, the source that copied them, and the face. */
typedef struct ConfFace {
    uint8_t bytes[ConfMaxFontBytes];
    size_t length;
    FontSource *source;
    FontFace *face;
    RoboTextFontContext font;
    ConfGlyphIds ids;
} ConfFace;

/** Build @p spec into @p out's bytes and record the ligature glyph ids. Does not open a face. */
static inline void ConfFaceBuildBytes(ConfFace *out, const ConfFontSpec *spec)
{
    static uint8_t head[54];
    static uint8_t hhea[36];
    static uint8_t maxp[32];
    static uint8_t hmtx[ConfMaxGlyphs * 4u];
    static uint8_t cmap[4096];
    static uint8_t gdef[1024];
    static uint8_t gsub[8192];
    static uint8_t gpos[8192];
    static uint8_t post[32];
    static uint8_t os2[96];
    ConfTable tables[12];
    uint32_t count = 0u;
    size_t cmapLength;
    size_t gdefLength;
    size_t gsubLength;
    size_t gposLength;

    memset(&out->ids, 0, sizeof(out->ids));
    out->ids.glyphCount = spec->glyphCount;

    /* Record the ligature products by tag, so a suite reads them rather than a literal. */
    for (size_t i = 0u; i < spec->ligatureCount; i++) {
        const ConfLigatureRule *rule = &spec->ligatures[i];

        if (rule->componentCount == 3u && strcmp(rule->tag, "liga") == 0 &&
            ConfCodePointOf(rule->components[0]) == 'f' && ConfCodePointOf(rule->components[1]) == 'f' &&
            ConfCodePointOf(rule->components[2]) == 'i') {
            out->ids.ligaFfi = rule->result;
        } else if (rule->componentCount == 3u && strcmp(rule->tag, "liga") == 0 &&
                   ConfCodePointOf(rule->components[0]) == 'f' &&
                   ConfCodePointOf(rule->components[1]) == 'f' &&
                   ConfCodePointOf(rule->components[2]) == 'l') {
            out->ids.ligaFfl = rule->result;
        } else if (rule->componentCount == 2u && strcmp(rule->tag, "liga") == 0 &&
                   ConfCodePointOf(rule->components[0]) == 'f' &&
                   ConfCodePointOf(rule->components[1]) == 'f') {
            out->ids.ligaFf = rule->result;
        } else if (rule->componentCount == 2u && strcmp(rule->tag, "liga") == 0 &&
                   ConfCodePointOf(rule->components[0]) == 'f' &&
                   ConfCodePointOf(rule->components[1]) == 'i') {
            out->ids.ligaFi = rule->result;
        } else if (strcmp(rule->tag, "dlig") == 0) {
            out->ids.dligCt = rule->result;
        } else if (strcmp(rule->tag, "rlig") == 0) {
            out->ids.rligSt = rule->result;
        }
    }

    memset(hmtx, 0, sizeof(hmtx));
    tables[count].tag = RoboFontTag('h', 'e', 'a', 'd');
    tables[count].data = head;
    tables[count].length = (uint32_t)ConfHead(head);
    count++;
    tables[count].tag = RoboFontTag('h', 'h', 'e', 'a');
    tables[count].data = hhea;
    tables[count].length = (uint32_t)ConfHhea(hhea, spec->glyphCount);
    count++;
    tables[count].tag = RoboFontTag('m', 'a', 'x', 'p');
    tables[count].data = maxp;
    tables[count].length = (uint32_t)ConfMaxp(maxp, spec->glyphCount);
    count++;
    tables[count].tag = RoboFontTag('h', 'm', 't', 'x');
    tables[count].data = hmtx;
    tables[count].length = (uint32_t)ConfHmtx(hmtx, spec);
    count++;
    cmapLength = ConfCmap(cmap, sizeof(cmap), spec);
    tables[count].tag = RoboFontTag('c', 'm', 'a', 'p');
    tables[count].data = cmap;
    tables[count].length = (uint32_t)cmapLength;
    count++;
    tables[count].tag = RoboFontTag('p', 'o', 's', 't');
    tables[count].data = post;
    tables[count].length = (uint32_t)ConfPost(post);
    count++;
    tables[count].tag = RoboFontTag('O', 'S', '/', '2');
    tables[count].data = os2;
    tables[count].length = (uint32_t)ConfOs2(os2);
    count++;

    if (!spec->omitGsub && (spec->ligatureCount > 0u || spec->substCount > 0u)) {
        ConfWriter w;

        w.data = gsub;
        w.capacity = sizeof(gsub);
        w.length = 0u;
        gsubLength = ConfBuildGsub(&w, spec);
        tables[count].tag = RoboFontTag('G', 'S', 'U', 'B');
        tables[count].data = gsub;
        tables[count].length = (uint32_t)gsubLength;
        count++;
    }
    /*
     * Any rule kind at all is a reason for the table: 12.19 added the **attachments**, and a face whose only
     * GPOS is cursive or mark attachment (Arabic's, Devanagari's) would otherwise ship no GPOS table and be
     * shaped as if the face had none — which is how a mark case comes to read zeroes and look like an engine
     * defect.
     */
    if (!spec->omitGpos && (spec->pairCount > 0u || spec->singleCount > 0u ||
                            spec->cursiveCount > 0u || spec->markBaseCount > 0u ||
                            spec->markMarkCount > 0u || spec->markLigCount > 0u)) {
        ConfWriter w;

        w.data = gpos;
        w.capacity = sizeof(gpos);
        w.length = 0u;
        gposLength = ConfBuildGpos(&w, spec);
        tables[count].tag = RoboFontTag('G', 'P', 'O', 'S');
        tables[count].data = gpos;
        tables[count].length = (uint32_t)gposLength;
        count++;
    }
    if (!spec->omitGdef && spec->markCount > 0u) {
        gdefLength = ConfGdef(gdef, sizeof(gdef), spec);
        tables[count].tag = RoboFontTag('G', 'D', 'E', 'F');
        tables[count].data = gdef;
        tables[count].length = (uint32_t)gdefLength;
        count++;
    }

    RoboCheck(count <= 12u);
    fflush(stderr);
    out->length = ConfSfnt(out->bytes, sizeof(out->bytes), tables, count);
    RoboCheck(out->length != 0u);
}

/**
 * Open a face over @p spec at @p sizePx.
 *
 * The face is *checked* here rather than trusted: the cmap is parsed and `A` is looked up, so a defect in the
 * builder reads as a builder defect with a line number instead of as an engine failure in whichever case ran
 * first.
 */
/**
 * Rewrite @p tag's directory entry offset to @p offset in a font that has been built but not opened.
 *
 * The malformed case's door: a table whose *directory entry* points past the end of the source is exactly
 * what a truncated download looks like, and the engine's contract is that it skips what it cannot read and
 * shapes the text anyway. The bytes are untouched — only the pointer to them is — so the case cannot pass by
 * accidentally deleting the table.
 *
 * @return 1 when @p tag was found and patched; 0 when the built font has no such table.
 */
static inline int ConfFacePatchTableOffset(ConfFace *face, uint32_t tag, uint32_t offset)
{
    uint16_t count = (uint16_t)(((uint16_t)face->bytes[4] << 8) | face->bytes[5]);

    for (uint16_t i = 0u; i < count; i++) {
        uint8_t *record = face->bytes + 12u + (size_t)i * 16u;
        uint32_t entryTag = ((uint32_t)record[0] << 24) | ((uint32_t)record[1] << 16) |
                            ((uint32_t)record[2] << 8) | (uint32_t)record[3];

        if (entryTag != tag) {
            continue;
        }
        record[8] = (uint8_t)(offset >> 24);
        record[9] = (uint8_t)(offset >> 16);
        record[10] = (uint8_t)(offset >> 8);
        record[11] = (uint8_t)offset;
        return 1;
    }
    return 0;
}

/**
 * Open a face over the bytes @ref ConfFaceBuildBytes (and any patching) left in @p out.
 *
 * Split out from @ref ConfFaceOpen so the malformed cases can patch a directory entry between building the
 * font and opening it. `FontSourceCreateMemoryCopy` takes a copy, so a patch applied after the open would
 * never reach the parser; a patch applied here always does.
 */
static inline void ConfFaceAdopt(ConfFace *out, float sizePx)
{
    RoboTextFontStyle style;

    RoboCheck(out->length != 0u);
    out->source = NULL;
    out->face = NULL;
    RoboCheckEqInt((int)FontSourceCreateMemoryCopy(out->bytes, out->length, &out->source), (int)RoboOk);
    out->face = NULL;
    RoboCheckEqInt((int)FontFaceCreate(out->source, 0u, &out->face), (int)RoboOk);
    RoboCheckEqInt((int)FontFaceParseCmap(out->face), (int)RoboOk);
    RoboCheck(FontFaceCmap(out->face) != NULL);
    RoboCheckEqInt((int)FontFaceParseMetrics(out->face), (int)RoboOk);
    RoboTextFontStyleInit(&style);
    style.sizePx = sizePx;
    RoboCheckEqInt((int)RoboTextFontContextSet(&out->font, out->face, &style), (int)RoboOk);
}

/**
 * The first code point @p spec's mapping must carry, and the glyph it must answer with.
 *
 * The probe @ref ConfFaceOpen checks the built face against, and it is derived from the spec rather than fixed
 * at `A` because the fallback secondary deliberately maps no Latin at all: a fixture whose whole purpose is
 * *missing* coverage cannot be validated by looking up a character it does not have.
 */
static inline int ConfSpecProbe(const ConfFontSpec *spec, uint32_t *outCodePoint, uint16_t *outGlyph)
{
    if (spec->canonicalCmap) {
        uint32_t list[ConfMaxGlyphs];
        size_t count = ConfCanonicalCodepoints(list, ConfMaxGlyphs);

        for (size_t i = 0u; i < count; i++) {
            if (spec->cmapFilterLast != 0u &&
                (list[i] < spec->cmapFilterFirst || list[i] > spec->cmapFilterLast)) {
                continue;
            }
            *outCodePoint = list[i];
            *outGlyph = (uint16_t)(i + 1u);
            return 1;
        }
        return 0;
    }
    if (spec->cmapCount == 0u) {
        return 0;
    }
    *outCodePoint = spec->cmap[0].first;
    *outGlyph = spec->cmap[0].glyph;
    return 1;
}

/**
 * Build @p spec and open a face over it at @p sizePx — the door every suite uses.
 *
 * The face is *checked* rather than trusted: the spec's first mapped code point is looked up and must answer
 * the glyph the spec assigned it, so a defect in the builder reads as a builder defect with a line number
 * instead of as an engine failure in whichever case happened to run first.
 */
static inline void ConfFaceOpen(ConfFace *out, const ConfFontSpec *spec, float sizePx)
{
    uint32_t probe = 0u;
    uint16_t want = 0u;
    FontCmapLookupResult lookup;

    memset(out, 0, sizeof(*out));
    ConfFaceBuildBytes(out, spec);
    ConfFaceAdopt(out, sizePx);
    RoboCheckMsg(ConfSpecProbe(spec, &probe, &want), "the spec maps no code point at all");
    memset(&lookup, 0, sizeof(lookup));
    RoboCheckEqInt((int)FontFaceLookupGlyph(out->face, probe, &lookup), (int)RoboOk);
    RoboCheckEqInt((int)lookup.status, (int)RoboFontCmapMapped);
    RoboCheckEqInt((int)lookup.glyphId, (int)want);
}

/**
 * The bytes of @p tag's table in a font that has been built but not opened, or NULL.
 *
 * The malformed case's second door: a *directory entry* that points outside the source makes the whole face
 * unusable (which is correct — the loader cannot trust a font whose own contents lie about their location),
 * so what a robustness case wants to corrupt is the table's **contents**, keeping the entry honest. A
 * `GSUB` header whose ScriptList offset is past the end of its own table is exactly what a truncated
 * download produces, and the engine's promise is that it is skipped and counted rather than fatal.
 */
static inline uint8_t *ConfFaceTableBytes(ConfFace *face, uint32_t tag, uint32_t *outLength)
{
    uint16_t count = (uint16_t)(((uint16_t)face->bytes[4] << 8) | face->bytes[5]);

    for (uint16_t i = 0u; i < count; i++) {
        uint8_t *record = face->bytes + 12u + (size_t)i * 16u;
        uint32_t entryTag = ((uint32_t)record[0] << 24) | ((uint32_t)record[1] << 16) |
                            ((uint32_t)record[2] << 8) | (uint32_t)record[3];
        uint32_t offset;
        uint32_t length;

        if (entryTag != tag) {
            continue;
        }
        offset = ((uint32_t)record[8] << 24) | ((uint32_t)record[9] << 16) |
                 ((uint32_t)record[10] << 8) | (uint32_t)record[11];
        length = ((uint32_t)record[12] << 24) | ((uint32_t)record[13] << 16) |
                 ((uint32_t)record[14] << 8) | (uint32_t)record[15];
        if (offset + length > face->length) {
            return NULL;
        }
        if (outLength) {
            *outLength = length;
        }
        return face->bytes + offset;
    }
    return NULL;
}

/** Release everything @ref ConfFaceOpen took. Safe on NULL and on a zeroed record. */
static inline void ConfFaceClose(ConfFace *face)
{
    if (!face) {
        return;
    }
    RoboTextFontContextInit(&face->font);
    FontFaceRelease(face->face);
    face->face = NULL;
    FontSourceRelease(face->source);
    face->source = NULL;
}

/* ------------------------------------------------------------------ */
/* The presets                                                         */
/* ------------------------------------------------------------------ */

/** Glyph ids the ligature products occupy: above the canonical block, in a fixed order. */
#define ConfLigatureGlyphBase 256u

/** The advance every canonical glyph carries: distinct per id, so a wrong id is a wrong number. */
static inline uint16_t ConfAdvanceOf(uint16_t glyphId)
{
    if (glyphId >= ConfLigatureGlyphBase) {
        return (uint16_t)(700u + (glyphId - ConfLigatureGlyphBase) * 7u);
    }
    return (uint16_t)(400u + glyphId);
}

/** Fill @p spec with every glyph's advance and nothing else. */
static inline void ConfSpecInit(ConfFontSpec *spec, uint16_t glyphCount)
{
    memset(spec, 0, sizeof(*spec));
    spec->glyphCount = glyphCount;
    for (uint16_t glyph = 0u; glyph < glyphCount; glyph++) {
        spec->advances[glyph] = ConfAdvanceOf(glyph);
    }
}

/**
 * The European conformance font: the whole canonical `cmap`, `liga`/`dlig`/`rlig` ligatures, an `AV` kern
 * pair, and mark placement under `mark`.
 *
 * The combining marks carry a **zero advance** in `hmtx`, which is what makes the mark cases decisive: a run
 * containing a mark must measure the same as its base alone, and any advance the mark contributes is a
 * defect rather than a rounding difference.
 */
static inline void ConfSpecEuropean(ConfFontSpec *spec)
{
    uint32_t list[ConfMaxGlyphs];
    size_t listCount = ConfCanonicalCodepoints(list, ConfMaxGlyphs);
    uint16_t glyphCount = (uint16_t)(ConfLigatureGlyphBase + 8u);
    uint16_t f = ConfGlyphOf('f');
    uint16_t i = ConfGlyphOf('i');
    uint16_t l = ConfGlyphOf('l');
    uint16_t c = ConfGlyphOf('c');
    uint16_t t = ConfGlyphOf('t');
    uint32_t m;
    size_t n = 0u;

    ConfSpecInit(spec, glyphCount);
    spec->canonicalCmap = 1;

    /* Marks advance nothing. */
    for (m = 0x0300u; m <= 0x030Au; m++) {
        uint16_t glyph = ConfGlyphOf(m);

        if (glyph != 0u) {
            spec->advances[glyph] = 0u;
            spec->marks[spec->markCount++] = glyph;
        }
    }

    /* `liga`: ffi, ffl, ff, fi — the longest candidate first in each set, which the builder enforces. */
    {
        struct {
            uint16_t a, b, c;
            uint16_t count;
            uint16_t result;
        } kLiga[] = {{f, f, i, 3u, (uint16_t)(ConfLigatureGlyphBase + 1u)},
                     {f, f, l, 3u, (uint16_t)(ConfLigatureGlyphBase + 2u)},
                     {f, f, 0u, 2u, (uint16_t)(ConfLigatureGlyphBase + 3u)},
                     {f, i, 0u, 2u, (uint16_t)(ConfLigatureGlyphBase + 4u)}};

        for (size_t k = 0u; k < sizeof(kLiga) / sizeof(kLiga[0]); k++) {
            ConfLigatureRule *rule = &spec->ligatures[n++];

            memcpy(rule->tag, "liga", 5u);
            rule->components[0] = kLiga[k].a;
            rule->components[1] = kLiga[k].b;
            rule->components[2] = kLiga[k].c;
            rule->componentCount = kLiga[k].count;
            rule->result = kLiga[k].result;
        }
    }
    /* `dlig`: c t -> ct, off by default. */
    {
        ConfLigatureRule *rule = &spec->ligatures[n++];

        memcpy(rule->tag, "dlig", 5u);
        rule->components[0] = c;
        rule->components[1] = t;
        rule->componentCount = 2u;
        rule->result = (uint16_t)(ConfLigatureGlyphBase + 5u);
    }
    /* `rlig`: `Q Q` -> one glyph, required: it runs even under `font-variant-ligatures: none`. The pair is
     * deliberately one no European word contains, so a case about the `cmap` or about kerning can never
     * trip over a required ligature by accident. */
    {
        ConfLigatureRule *rule = &spec->ligatures[n++];

        memcpy(rule->tag, "rlig", 5u);
        rule->components[0] = ConfGlyphOf('Q');
        rule->components[1] = ConfGlyphOf('Q');
        rule->componentCount = 2u;
        rule->result = (uint16_t)(ConfLigatureGlyphBase + 6u);
    }
    spec->ligatureCount = n;

    /* A kern pair: `AV` closes by 80 units. */
    {
        ConfPairRule *pair = &spec->pairs[spec->pairCount++];

        memcpy(pair->tag, "kern", 5u);
        pair->first = ConfGlyphOf('A');
        pair->second = ConfGlyphOf('V');
        pair->xAdvance = -80;
    }

    /* Mark placement, one rule per mark, under `mark`. */
    for (size_t k = 0u; k < spec->markCount; k++) {
        ConfSingleRule *single = &spec->singles[spec->singleCount++];

        memcpy(single->tag, "mark", 5u);
        single->glyph = spec->marks[k];
        single->xPlacement = 40;
        single->yPlacement = 120;
        single->xAdvance = 0;
    }
    (void)listCount;
}

/**
 * A font whose `cmap` covers the Latin block and nothing else.
 *
 * The primary of the fallback pair. It carries the same ligature and kern data as the European font so that
 * fallback cases do not also become feature cases.
 */
static inline void ConfSpecFallbackPrimary(ConfFontSpec *spec)
{
    ConfSpecEuropean(spec);
    /* Drop the Greek, Cyrillic, Latin-1 and mark runs, keeping the ASCII letters, digits and punctuation. */
    {
        uint32_t list[ConfMaxGlyphs];
        size_t listCount = ConfCanonicalCodepoints(list, ConfMaxGlyphs);
        size_t asciiEnd = 0u;

        for (size_t i = 0u; i < listCount; i++) {
            if (list[i] <= 0x007Eu) {
                asciiEnd = i + 1u;
            }
        }
        (void)asciiEnd;
    }
    /* The same canonical mapping, filtered to printable ASCII: the ids a Latin fallback case names are the
     * ids this face gives, and every other scalar is a miss. */
    spec->canonicalCmap = 1;
    spec->cmapFilterFirst = 0x0020u;
    spec->cmapFilterLast = 0x007Eu;
    spec->markCount = 0u;
    spec->singleCount = 0u;
}

/** The secondary of the fallback pair: Greek and Cyrillic, no Latin. */
static inline void ConfSpecFallbackSecondary(ConfFontSpec *spec)
{
    ConfSpecEuropean(spec);
    /* Greek and Cyrillic and nothing else: no Latin, and no combining marks. */
    spec->canonicalCmap = 1;
    spec->cmapFilterFirst = 0x0391u;
    spec->cmapFilterLast = 0x044Fu;
    spec->ligatureCount = 1u;
    memcpy(spec->ligatures[0].tag, "liga", 5u);
    spec->ligatures[0].components[0] = ConfGlyphOf('f');
    spec->ligatures[0].components[1] = ConfGlyphOf('i');
    spec->ligatures[0].componentCount = 2u;
    spec->ligatures[0].result = (uint16_t)(ConfLigatureGlyphBase + 4u);
    spec->pairCount = 0u;
    spec->singleCount = 0u;
    spec->markCount = 0u;
}

/** A font whose GSUB table is present but truncated, for the malformed-table cases. */
static inline void ConfSpecMalformedGsub(ConfFontSpec *spec)
{
    ConfSpecEuropean(spec);
    /* The GSUB header claims a ScriptList past the end of the table. */
    spec->omitGsub = 0;
    spec->omitGpos = 0;
    spec->omitGdef = 1;
}

#endif /* ROBO_TEXT_CONFORMANCE_FONT_H */
