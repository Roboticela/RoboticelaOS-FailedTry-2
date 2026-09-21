/*
 * RoboFont cmap mapping engine (Goal 5.6).
 */

#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/* ---- Internal subtable representations ---- */

typedef struct {
    uint16_t start;
    uint16_t end;
    int16_t idDelta;
    uint16_t idRangeOffset; /* bytes; 0 = delta-only */
    uint32_t glyphArrayIndex; /* into fmt4 glyph array when idRangeOffset != 0 */
} CmapFmt4Seg;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t startGlyph; /* format 12: start GID; format 13: constant GID */
} CmapGroup;

typedef struct {
    uint32_t startUnicode;
    uint8_t additionalCount; /* range covers start .. start+additional */
} CmapUvsRange;

typedef struct {
    uint32_t unicode;
    uint16_t glyphId;
} CmapUvsMapping;

typedef struct {
    uint32_t varSelector;
    uint32_t numDefaultRanges;
    CmapUvsRange *defaultRanges;
    uint32_t numNonDefault;
    CmapUvsMapping *nonDefault;
} CmapUvsSelector;

typedef struct {
    uint16_t format;
    uint16_t platform;
    uint16_t encoding;
    uint32_t language;

    /* format 0 */
    uint8_t *fmt0Glyphs; /* 256 */

    /* format 2 — keep raw subtable for careful lookup */
    FontSpan fmt2Span;

    /* format 4 */
    uint16_t fmt4SegCount;
    CmapFmt4Seg *fmt4Segs;
    uint16_t *fmt4GlyphIds;
    uint32_t fmt4GlyphCount;

    /* format 6 */
    uint16_t fmt6First;
    uint16_t fmt6Count;
    uint16_t *fmt6Glyphs;

    /* format 10 */
    uint32_t fmt10Start;
    uint32_t fmt10Count;
    uint16_t *fmt10Glyphs;

    /* format 12 / 13 */
    uint32_t groupCount;
    CmapGroup *groups;
    int groupsAreFmt13;

    /* format 14 */
    uint32_t uvsCount;
    CmapUvsSelector *uvs;
} CmapBody;

struct FontCmapMap {
    uint32_t refs;
    FontSfnt *sfnt; /* may be NULL for span-only maps */
    uint32_t numGlyphs;
    CmapBody primary;
    CmapBody uvsBody; /* format 14 only; format field 14 or 0 */
    int hasPrimary;
    int hasUvs;
    FontCmap structural; /* encoding list retained for introspection */
};

int FontCmapIsUnicodeScalar(uint32_t codePoint)
{
    if (codePoint > 0x10FFFFu) {
        return 0;
    }
    if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) {
        return 0;
    }
    return 1;
}

static void ClearUvsSelector(CmapUvsSelector *s)
{
    if (!s) {
        return;
    }
    RoboFree(s->defaultRanges);
    RoboFree(s->nonDefault);
    memset(s, 0, sizeof(*s));
}

static void ClearBody(CmapBody *b)
{
    uint32_t i;
    if (!b) {
        return;
    }
    RoboFree(b->fmt0Glyphs);
    RoboFree(b->fmt4Segs);
    RoboFree(b->fmt4GlyphIds);
    RoboFree(b->fmt6Glyphs);
    RoboFree(b->fmt10Glyphs);
    RoboFree(b->groups);
    if (b->uvs) {
        for (i = 0u; i < b->uvsCount; i++) {
            ClearUvsSelector(&b->uvs[i]);
        }
        RoboFree(b->uvs);
    }
    memset(b, 0, sizeof(*b));
}

static int GidOk(uint32_t numGlyphs, uint32_t gid)
{
    return gid < numGlyphs;
}

static Result FinalizeGid(uint32_t numGlyphs,
                                  uint32_t gid,
                                  FontCmapLookupResult *out)
{
    if (!GidOk(numGlyphs, gid)) {
        out->status = RoboFontCmapInvalidFont;
        out->glyphId = 0u;
        return RoboOk;
    }
    if (gid == 0u) {
        /* Mapped to .notdef — still a successful mapping. */
        out->status = RoboFontCmapMapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    out->status = RoboFontCmapMapped;
    out->glyphId = gid;
    return RoboOk;
}

/* ---- Format parsers ---- */

static Result ParseFmt0(FontSpan sub, CmapBody *b)
{
    FontReader r;
    uint16_t i;
    if (sub.size < 262u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, sub);
    if (FontReaderSeek(&r, 6u) != RoboOk) {
        return RoboParseError;
    }
    b->fmt0Glyphs = (uint8_t *)RoboCalloc(256u, 1u);
    if (!b->fmt0Glyphs) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < 256u; i++) {
        if (FontReadU8(&r, &b->fmt0Glyphs[i]) != RoboOk) {
            return RoboParseError;
        }
    }
    b->format = 0u;
    return RoboOk;
}

static Result ParseFmt2(FontSpan sub, CmapBody *b)
{
    if (sub.size < 518u) { /* format+len+lang + 256*2 keys minimum-ish */
        return RoboParseError;
    }
    b->fmt2Span = sub;
    b->format = 2u;
    return RoboOk;
}

static Result LookupFmt2(const CmapBody *b,
                                 uint32_t cp,
                                 uint32_t numGlyphs,
                                 FontCmapLookupResult *out)
{
    FontReader r;
    uint16_t high;
    uint16_t low;
    uint16_t key;
    uint16_t headerKeysOff = 6u;
    uint16_t subheaderIndex;
    size_t subheadersOff;
    size_t shOff;
    uint16_t firstCode;
    uint16_t entryCount;
    int16_t idDelta;
    uint16_t idRangeOffset;
    uint32_t gid;

    if (cp > 0xFFFFu) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    high = (uint16_t)((cp >> 8) & 0xFFu);
    low = (uint16_t)(cp & 0xFFu);
    FontReaderInitSpan(&r, b->fmt2Span);
    if (FontReaderSeek(&r, headerKeysOff + (size_t)high * 2u) != RoboOk ||
        FontReadU16be(&r, &key) != RoboOk) {
        return RoboParseError;
    }
    /* key is offset into subHeaders in bytes; /8 = index */
    if ((key % 8u) != 0u) {
        return RoboParseError;
    }
    subheaderIndex = (uint16_t)(key / 8u);
    subheadersOff = 6u + 256u * 2u;
    shOff = subheadersOff + (size_t)subheaderIndex * 8u;
    if (shOff + 8u > b->fmt2Span.size) {
        return RoboParseError;
    }
    if (FontReaderSeek(&r, shOff) != RoboOk ||
        FontReadU16be(&r, &firstCode) != RoboOk ||
        FontReadU16be(&r, &entryCount) != RoboOk ||
        FontReadI16be(&r, &idDelta) != RoboOk ||
        FontReadU16be(&r, &idRangeOffset) != RoboOk) {
        return RoboParseError;
    }
    if (low < firstCode || (uint16_t)(low - firstCode) >= entryCount) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    if (idRangeOffset == 0u) {
        gid = (uint32_t)((uint16_t)(low + (uint16_t)idDelta));
        return FinalizeGid(numGlyphs, gid, out);
    }
    {
        /* glyphIndexAddress = idRangeOffset + 2*(low-firstCode) + &idRangeOffset */
        size_t idRangeField = shOff + 6u;
        size_t glyphOff =
            idRangeField + (size_t)idRangeOffset +
            (size_t)(low - firstCode) * 2u;
        uint16_t g16;
        if (glyphOff + 2u > b->fmt2Span.size) {
            return RoboParseError;
        }
        if (FontReaderSeek(&r, glyphOff) != RoboOk ||
            FontReadU16be(&r, &g16) != RoboOk) {
            return RoboParseError;
        }
        if (g16 == 0u) {
            out->status = RoboFontCmapUnmapped;
            out->glyphId = 0u;
            return RoboOk;
        }
        gid = (uint32_t)((uint16_t)(g16 + (uint16_t)idDelta));
        return FinalizeGid(numGlyphs, gid, out);
    }
}

static Result ParseFmt4(FontSpan sub, CmapBody *b)
{
    FontReader r;
    uint16_t segCountX2;
    uint16_t segCount;
    uint16_t i;
    size_t endOff;
    size_t startOff;
    size_t deltaOff;
    size_t rangeOff;
    uint32_t glyphSlots = 0u;

    if (sub.size < 16u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, sub);
    if (FontReaderSeek(&r, 6u) != RoboOk ||
        FontReadU16be(&r, &segCountX2) != RoboOk) {
        return RoboParseError;
    }
    if ((segCountX2 & 1u) != 0u) {
        return RoboParseError;
    }
    segCount = (uint16_t)(segCountX2 / 2u);
    if (segCount == 0u || segCount > RoboFontCmapMaxSegments) {
        return RoboParseError;
    }
    /* skip searchRange, entrySelector, rangeShift */
    if (FontReaderSeek(&r, 14u) != RoboOk) {
        return RoboParseError;
    }
    endOff = 14u;
    startOff = endOff + (size_t)segCount * 2u + 2u; /* + reservedPad */
    deltaOff = startOff + (size_t)segCount * 2u;
    rangeOff = deltaOff + (size_t)segCount * 2u;
    if (rangeOff + (size_t)segCount * 2u > sub.size) {
        return RoboParseError;
    }

    b->fmt4Segs = (CmapFmt4Seg *)RoboCalloc(segCount, sizeof(CmapFmt4Seg));
    if (!b->fmt4Segs) {
        return RoboOutOfMemory;
    }
    b->fmt4SegCount = segCount;

    for (i = 0u; i < segCount; i++) {
        uint16_t endC;
        if (FontReaderSeek(&r, endOff + (size_t)i * 2u) != RoboOk ||
            FontReadU16be(&r, &endC) != RoboOk) {
            return RoboParseError;
        }
        b->fmt4Segs[i].end = endC;
        if (i > 0u && endC <= b->fmt4Segs[i - 1u].end && endC != 0xFFFFu) {
            /* allow equal only for malformed rejection — must be increasing */
            if (endC < b->fmt4Segs[i - 1u].end) {
                return RoboParseError;
            }
        }
    }
    for (i = 0u; i < segCount; i++) {
        uint16_t startC;
        int16_t delta;
        uint16_t ro;
        if (FontReaderSeek(&r, startOff + (size_t)i * 2u) != RoboOk ||
            FontReadU16be(&r, &startC) != RoboOk) {
            return RoboParseError;
        }
        if (FontReaderSeek(&r, deltaOff + (size_t)i * 2u) != RoboOk ||
            FontReadI16be(&r, &delta) != RoboOk) {
            return RoboParseError;
        }
        if (FontReaderSeek(&r, rangeOff + (size_t)i * 2u) != RoboOk ||
            FontReadU16be(&r, &ro) != RoboOk) {
            return RoboParseError;
        }
        if (startC > b->fmt4Segs[i].end) {
            return RoboParseError;
        }
        b->fmt4Segs[i].start = startC;
        b->fmt4Segs[i].idDelta = delta;
        b->fmt4Segs[i].idRangeOffset = ro;
        if (ro != 0u && b->fmt4Segs[i].end != 0xFFFFu) {
            uint32_t span = (uint32_t)b->fmt4Segs[i].end - (uint32_t)startC + 1u;
            if (glyphSlots > UINT32_MAX - span) {
                return RoboParseError;
            }
            b->fmt4Segs[i].glyphArrayIndex = glyphSlots;
            glyphSlots += span;
        }
    }
    if (segCount == 0u || b->fmt4Segs[segCount - 1u].end != 0xFFFFu) {
        return RoboParseError;
    }

    if (glyphSlots > RoboFontCmapMaxGlyphArray) {
        return RoboParseError;
    }
    if (glyphSlots > 0u) {
        uint32_t g;
        b->fmt4GlyphIds =
            (uint16_t *)RoboCalloc(glyphSlots, sizeof(uint16_t));
        if (!b->fmt4GlyphIds) {
            return RoboOutOfMemory;
        }
        b->fmt4GlyphCount = glyphSlots;
        /* Fill by reading from original glyphIdArray using OT formula. */
        for (i = 0u; i < segCount; i++) {
            CmapFmt4Seg *seg = &b->fmt4Segs[i];
            uint32_t c;
            if (seg->idRangeOffset == 0u || seg->end == 0xFFFFu) {
                continue;
            }
            for (c = seg->start; c <= seg->end; c++) {
                size_t idRangeField = rangeOff + (size_t)i * 2u;
                size_t goff = idRangeField + (size_t)seg->idRangeOffset +
                              (size_t)(c - seg->start) * 2u;
                uint16_t g16;
                uint32_t slot = seg->glyphArrayIndex + (c - seg->start);
                if (goff + 2u > sub.size || slot >= glyphSlots) {
                    return RoboParseError;
                }
                if (FontReaderSeek(&r, goff) != RoboOk ||
                    FontReadU16be(&r, &g16) != RoboOk) {
                    return RoboParseError;
                }
                b->fmt4GlyphIds[slot] = g16;
                (void)g;
            }
        }
    }
    b->format = 4u;
    return RoboOk;
}

static Result LookupFmt4(const CmapBody *b,
                                 uint32_t cp,
                                 uint32_t numGlyphs,
                                 FontCmapLookupResult *out)
{
    uint16_t lo;
    uint16_t hi;
    uint16_t mid;
    const CmapFmt4Seg *seg;
    uint32_t gid;

    if (cp > 0xFFFFu) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    lo = 0u;
    hi = b->fmt4SegCount;
    while (lo < hi) {
        mid = (uint16_t)(lo + (hi - lo) / 2u);
        if (cp > b->fmt4Segs[mid].end) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    if (lo >= b->fmt4SegCount) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    seg = &b->fmt4Segs[lo];
    if (cp < seg->start || cp > seg->end) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    if (seg->end == 0xFFFFu && seg->start == 0xFFFFu) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    if (seg->idRangeOffset == 0u) {
        gid = (uint32_t)((uint16_t)((uint16_t)cp + (uint16_t)seg->idDelta));
        return FinalizeGid(numGlyphs, gid, out);
    }
    {
        uint32_t slot = seg->glyphArrayIndex + ((uint16_t)cp - seg->start);
        uint16_t g16;
        if (slot >= b->fmt4GlyphCount) {
            return RoboParseError;
        }
        g16 = b->fmt4GlyphIds[slot];
        if (g16 == 0u) {
            out->status = RoboFontCmapUnmapped;
            out->glyphId = 0u;
            return RoboOk;
        }
        gid = (uint32_t)((uint16_t)(g16 + (uint16_t)seg->idDelta));
        return FinalizeGid(numGlyphs, gid, out);
    }
}

static Result ParseFmt6(FontSpan sub, CmapBody *b)
{
    FontReader r;
    uint16_t first;
    uint16_t count;
    uint16_t i;
    if (sub.size < 10u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, sub);
    if (FontReaderSeek(&r, 6u) != RoboOk ||
        FontReadU16be(&r, &first) != RoboOk ||
        FontReadU16be(&r, &count) != RoboOk) {
        return RoboParseError;
    }
    if ((uint32_t)count > RoboFontCmapMaxGlyphArray) {
        return RoboParseError;
    }
    if (FontCheckedRange(sub.size, 10u, (size_t)count * 2u) != RoboOk) {
        return RoboParseError;
    }
    b->fmt6First = first;
    b->fmt6Count = count;
    if (count > 0u) {
        b->fmt6Glyphs = (uint16_t *)RoboCalloc(count, sizeof(uint16_t));
        if (!b->fmt6Glyphs) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < count; i++) {
            if (FontReadU16be(&r, &b->fmt6Glyphs[i]) != RoboOk) {
                return RoboParseError;
            }
        }
    }
    b->format = 6u;
    return RoboOk;
}

static Result LookupFmt6(const CmapBody *b,
                                 uint32_t cp,
                                 uint32_t numGlyphs,
                                 FontCmapLookupResult *out)
{
    if (cp > 0xFFFFu || cp < b->fmt6First ||
        (cp - b->fmt6First) >= b->fmt6Count) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    return FinalizeGid(numGlyphs, b->fmt6Glyphs[cp - b->fmt6First], out);
}

static Result ParseFmt10(FontSpan sub, CmapBody *b)
{
    FontReader r;
    uint32_t start;
    uint32_t count;
    uint32_t i;
    if (sub.size < 20u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, sub);
    if (FontReaderSeek(&r, 12u) != RoboOk ||
        FontReadU32be(&r, &start) != RoboOk ||
        FontReadU32be(&r, &count) != RoboOk) {
        return RoboParseError;
    }
    if (count > RoboFontCmapMaxGlyphArray) {
        return RoboParseError;
    }
    if (start > 0x10FFFFu ||
        (count > 0u && start + count - 1u > 0x10FFFFu) ||
        (count > 0u && start + count < start)) {
        return RoboParseError;
    }
    if (FontCheckedRange(sub.size, 20u, (size_t)count * 2u) != RoboOk) {
        return RoboParseError;
    }
    b->fmt10Start = start;
    b->fmt10Count = count;
    if (count > 0u) {
        b->fmt10Glyphs = (uint16_t *)RoboCalloc(count, sizeof(uint16_t));
        if (!b->fmt10Glyphs) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < count; i++) {
            if (FontReadU16be(&r, &b->fmt10Glyphs[i]) != RoboOk) {
                return RoboParseError;
            }
        }
    }
    b->format = 10u;
    return RoboOk;
}

static Result LookupFmt10(const CmapBody *b,
                                  uint32_t cp,
                                  uint32_t numGlyphs,
                                  FontCmapLookupResult *out)
{
    if (cp < b->fmt10Start || (cp - b->fmt10Start) >= b->fmt10Count) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    return FinalizeGid(numGlyphs, b->fmt10Glyphs[cp - b->fmt10Start], out);
}

static Result ParseFmt12Or13(FontSpan sub, CmapBody *b, int is13)
{
    FontReader r;
    uint32_t nGroups;
    uint32_t i;
    if (sub.size < 16u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, sub);
    if (FontReaderSeek(&r, 12u) != RoboOk ||
        FontReadU32be(&r, &nGroups) != RoboOk) {
        return RoboParseError;
    }
    if (nGroups > RoboFontCmapMaxGroups) {
        return RoboParseError;
    }
    if (FontCheckedRange(sub.size, 16u, (size_t)nGroups * 12u) != RoboOk) {
        return RoboParseError;
    }
    b->groupCount = nGroups;
    b->groupsAreFmt13 = is13;
    if (nGroups == 0u) {
        b->format = is13 ? 13u : 12u;
        return RoboOk;
    }
    b->groups = (CmapGroup *)RoboCalloc(nGroups, sizeof(CmapGroup));
    if (!b->groups) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < nGroups; i++) {
        uint32_t start;
        uint32_t end;
        uint32_t g;
        if (FontReadU32be(&r, &start) != RoboOk ||
            FontReadU32be(&r, &end) != RoboOk ||
            FontReadU32be(&r, &g) != RoboOk) {
            return RoboParseError;
        }
        if (start > end || end > 0x10FFFFu) {
            return RoboParseError;
        }
        if ((start >= 0xD800u && start <= 0xDFFFu) ||
            (end >= 0xD800u && end <= 0xDFFFu) ||
            (start < 0xD800u && end > 0xDFFFu)) {
            return RoboParseError;
        }
        if (i > 0u && start <= b->groups[i - 1u].end) {
            return RoboParseError; /* must be sorted, non-overlapping */
        }
        if (!is13) {
            /* startGlyph + (end-start) must not overflow uint32 meaningfully for GID */
            if (g > 0xFFFFFFFFu - (end - start)) {
                return RoboParseError;
            }
        }
        b->groups[i].start = start;
        b->groups[i].end = end;
        b->groups[i].startGlyph = g;
    }
    b->format = is13 ? 13u : 12u;
    return RoboOk;
}

static Result LookupFmt1213(const CmapBody *b,
                                     uint32_t cp,
                                     uint32_t numGlyphs,
                                     FontCmapLookupResult *out)
{
    uint32_t lo = 0u;
    uint32_t hi = b->groupCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (cp > b->groups[mid].end) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= b->groupCount || cp < b->groups[lo].start || cp > b->groups[lo].end) {
        out->status = RoboFontCmapUnmapped;
        out->glyphId = 0u;
        return RoboOk;
    }
    if (b->groupsAreFmt13) {
        return FinalizeGid(numGlyphs, b->groups[lo].startGlyph, out);
    }
    return FinalizeGid(numGlyphs,
                        b->groups[lo].startGlyph + (cp - b->groups[lo].start), out);
}

static Result ParseFmt14(FontSpan sub, CmapBody *b)
{
    FontReader r;
    uint32_t numRec;
    uint32_t i;

    if (sub.size < 10u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, sub);
    if (FontReaderSeek(&r, 6u) != RoboOk ||
        FontReadU32be(&r, &numRec) != RoboOk) {
        return RoboParseError;
    }
    if (numRec > RoboFontCmapMaxUvsSelectors) {
        return RoboParseError;
    }
    if (FontCheckedRange(sub.size, 10u, (size_t)numRec * 11u) != RoboOk) {
        return RoboParseError;
    }
    b->uvsCount = numRec;
    b->format = 14u;
    if (numRec == 0u) {
        return RoboOk;
    }
    b->uvs = (CmapUvsSelector *)RoboCalloc(numRec, sizeof(CmapUvsSelector));
    if (!b->uvs) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < numRec; i++) {
        uint8_t vsBytes[3];
        uint32_t vs;
        uint32_t defOff;
        uint32_t nondefOff;
        uint32_t j;
        if (FontReadU8(&r, &vsBytes[0]) != RoboOk ||
            FontReadU8(&r, &vsBytes[1]) != RoboOk ||
            FontReadU8(&r, &vsBytes[2]) != RoboOk ||
            FontReadU32be(&r, &defOff) != RoboOk ||
            FontReadU32be(&r, &nondefOff) != RoboOk) {
            return RoboParseError;
        }
        vs = ((uint32_t)vsBytes[0] << 16) | ((uint32_t)vsBytes[1] << 8) |
             (uint32_t)vsBytes[2];
        b->uvs[i].varSelector = vs;

        if (defOff != 0u) {
            size_t abs = (size_t)defOff;
            FontReader dr;
            uint32_t nRanges;
            if (abs + 4u > sub.size) {
                return RoboParseError;
            }
            FontReaderInitSpan(&dr, sub);
            if (FontReaderSeek(&dr, abs) != RoboOk ||
                FontReadU32be(&dr, &nRanges) != RoboOk) {
                return RoboParseError;
            }
            if (nRanges > RoboFontCmapMaxUvsRanges) {
                return RoboParseError;
            }
            if (FontCheckedRange(sub.size, abs + 4u, (size_t)nRanges * 4u) !=
                RoboOk) {
                return RoboParseError;
            }
            b->uvs[i].numDefaultRanges = nRanges;
            if (nRanges > 0u) {
                b->uvs[i].defaultRanges = (CmapUvsRange *)RoboCalloc(
                    nRanges, sizeof(CmapUvsRange));
                if (!b->uvs[i].defaultRanges) {
                    return RoboOutOfMemory;
                }
                for (j = 0u; j < nRanges; j++) {
                    uint8_t u[3];
                    uint8_t add;
                    if (FontReadU8(&dr, &u[0]) != RoboOk ||
                        FontReadU8(&dr, &u[1]) != RoboOk ||
                        FontReadU8(&dr, &u[2]) != RoboOk ||
                        FontReadU8(&dr, &add) != RoboOk) {
                        return RoboParseError;
                    }
                    b->uvs[i].defaultRanges[j].startUnicode =
                        ((uint32_t)u[0] << 16) | ((uint32_t)u[1] << 8) | u[2];
                    b->uvs[i].defaultRanges[j].additionalCount = add;
                }
            }
        }
        if (nondefOff != 0u) {
            size_t abs = (size_t)nondefOff;
            FontReader nr;
            uint32_t nMap;
            if (abs + 4u > sub.size) {
                return RoboParseError;
            }
            FontReaderInitSpan(&nr, sub);
            if (FontReaderSeek(&nr, abs) != RoboOk ||
                FontReadU32be(&nr, &nMap) != RoboOk) {
                return RoboParseError;
            }
            if (nMap > RoboFontCmapMaxUvsMappings) {
                return RoboParseError;
            }
            if (FontCheckedRange(sub.size, abs + 4u, (size_t)nMap * 5u) !=
                RoboOk) {
                return RoboParseError;
            }
            b->uvs[i].numNonDefault = nMap;
            if (nMap > 0u) {
                b->uvs[i].nonDefault = (CmapUvsMapping *)RoboCalloc(
                    nMap, sizeof(CmapUvsMapping));
                if (!b->uvs[i].nonDefault) {
                    return RoboOutOfMemory;
                }
                for (j = 0u; j < nMap; j++) {
                    uint8_t u[3];
                    uint16_t gid;
                    if (FontReadU8(&nr, &u[0]) != RoboOk ||
                        FontReadU8(&nr, &u[1]) != RoboOk ||
                        FontReadU8(&nr, &u[2]) != RoboOk ||
                        FontReadU16be(&nr, &gid) != RoboOk) {
                        return RoboParseError;
                    }
                    b->uvs[i].nonDefault[j].unicode =
                        ((uint32_t)u[0] << 16) | ((uint32_t)u[1] << 8) | u[2];
                    b->uvs[i].nonDefault[j].glyphId = gid;
                }
            }
        }
    }
    return RoboOk;
}

static Result ParseSubtableBody(FontSpan table,
                                         const FontCmapEncoding *enc,
                                         CmapBody *b)
{
    FontSpan sub;
    Result res;
    if (enc->length == 0u ||
        FontSpanSlice(table, enc->subtableOffset, enc->length, &sub) !=
            RoboOk) {
        return RoboParseError;
    }
    ClearBody(b);
    b->platform = enc->platformId;
    b->encoding = enc->encodingId;
    b->language = enc->language;
    switch (enc->format) {
    case 0:
        res = ParseFmt0(sub, b);
        break;
    case 2:
        res = ParseFmt2(sub, b);
        break;
    case 4:
        res = ParseFmt4(sub, b);
        break;
    case 6:
        res = ParseFmt6(sub, b);
        break;
    case 10:
        res = ParseFmt10(sub, b);
        break;
    case 12:
        res = ParseFmt12Or13(sub, b, 0);
        break;
    case 13:
        res = ParseFmt12Or13(sub, b, 1);
        break;
    case 14:
        res = ParseFmt14(sub, b);
        break;
    default:
        return RoboNotSupported;
    }
    if (res != RoboOk) {
        ClearBody(b);
    }
    return res;
}

static int EncodingScore(const FontCmapEncoding *e)
{
    int score = 0;
    uint16_t p = e->platformId;
    uint16_t enc = e->encodingId;
    uint16_t fmt = e->format;

    if (fmt == 14u || fmt == 8u) {
        return -1; /* not for primary Unicode lookup */
    }
    if (!FontCmapFormatIsRecognized(fmt)) {
        return -1;
    }

    /* Prefer modern Unicode over legacy / symbol. */
    if (p == 0u && (enc == 4u || enc == 6u)) {
        score += 1000;
    } else if (p == 3u && enc == 10u) {
        score += 1000;
    } else if (p == 0u && enc == 3u) {
        score += 500;
    } else if (p == 3u && enc == 1u) {
        score += 500;
    } else if (p == 0u && (enc == 0u || enc == 1u || enc == 2u)) {
        score += 400;
    } else if (p == 3u && enc == 0u) {
        score += 200; /* symbol */
    } else if (p == 1u && enc == 0u) {
        score += 100; /* Mac Roman */
    } else {
        score += 10;
    }

    if (fmt == 12u || fmt == 13u) {
        score += 50;
    } else if (fmt == 10u) {
        score += 40;
    } else if (fmt == 4u || fmt == 6u) {
        score += 30;
    } else if (fmt == 0u) {
        score += 10;
    } else if (fmt == 2u) {
        score += 5;
    }
    return score;
}

static int PickPrimary(const FontCmap *c, uint16_t *outIndex)
{
    int best = -1;
    uint16_t bestI = 0u;
    uint16_t i;
    for (i = 0u; i < c->numTables; i++) {
        int s = EncodingScore(&c->encodings[i]);
        if (s > best) {
            best = s;
            bestI = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    *outIndex = bestI;
    return 1;
}

static int PickUvs(const FontCmap *c, uint16_t *outIndex)
{
    uint16_t i;
    for (i = 0u; i < c->numTables; i++) {
        if (c->encodings[i].format == 14u) {
            *outIndex = i;
            return 1;
        }
    }
    return 0;
}

static Result LookupBody(const CmapBody *b,
                                 uint32_t cp,
                                 uint32_t numGlyphs,
                                 FontCmapLookupResult *out)
{
    switch (b->format) {
    case 0:
        if (cp > 0xFFu) {
            out->status = RoboFontCmapUnmapped;
            out->glyphId = 0u;
            return RoboOk;
        }
        return FinalizeGid(numGlyphs, b->fmt0Glyphs[cp], out);
    case 2:
        return LookupFmt2(b, cp, numGlyphs, out);
    case 4:
        return LookupFmt4(b, cp, numGlyphs, out);
    case 6:
        return LookupFmt6(b, cp, numGlyphs, out);
    case 10:
        return LookupFmt10(b, cp, numGlyphs, out);
    case 12:
    case 13:
        return LookupFmt1213(b, cp, numGlyphs, out);
    default:
        out->status = RoboFontCmapInvalidFont;
        out->glyphId = 0u;
        return RoboOk;
    }
}

static Result MapBuildFromStructural(FontCmap *structural,
                                               uint32_t numGlyphs,
                                               FontSfnt *sfnt,
                                               FontCmapMap **out)
{
    FontCmapMap *m;
    uint16_t pri = 0u;
    uint16_t uvs = 0u;
    Result res;

    if (!structural || !out || numGlyphs == 0u) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    m = (FontCmapMap *)RoboCalloc(1u, sizeof(*m));
    if (!m) {
        return RoboOutOfMemory;
    }
    m->refs = 1u;
    m->numGlyphs = numGlyphs;
    m->structural = *structural;
    memset(structural, 0, sizeof(*structural)); /* ownership moved */
    if (sfnt) {
        FontSfntRetain(sfnt);
        m->sfnt = sfnt;
    }

    if (PickPrimary(&m->structural, &pri)) {
        res = ParseSubtableBody(m->structural.table, &m->structural.encodings[pri],
                                  &m->primary);
        if (res != RoboOk) {
            FontCmapMapRelease(m);
            return res;
        }
        m->hasPrimary = 1;
    }
    if (PickUvs(&m->structural, &uvs)) {
        res = ParseSubtableBody(m->structural.table, &m->structural.encodings[uvs],
                                  &m->uvsBody);
        if (res != RoboOk) {
            FontCmapMapRelease(m);
            return res;
        }
        m->hasUvs = 1;
    }
    if (!m->hasPrimary && !m->hasUvs) {
        FontCmapMapRelease(m);
        return RoboNotFound;
    }
    *out = m;
    return RoboOk;
}

Result FontCmapMapCreateFromSpan(FontSpan cmapTable,
                                                  uint32_t numGlyphs,
                                                  FontCmapMap **out)
{
    FontCmap structural;
    Result res;
    memset(&structural, 0, sizeof(structural));
    res = FontCmapParse(cmapTable, &structural);
    if (res != RoboOk) {
        return res;
    }
    return MapBuildFromStructural(&structural, numGlyphs, NULL, out);
}

Result FontSfntNumGlyphs(const FontSfnt *sfnt,
                                        uint32_t *outCount)
{
    FontSpan maxp;
    FontMaxp m;
    if (!sfnt || !outCount) {
        return RoboInvalidArgument;
    }
    if (FontSfntTableBytes(sfnt, RoboFontTagMaxp, &maxp) != RoboOk) {
        return RoboNotFound;
    }
    if (FontMaxpParse(maxp, &m) != RoboOk) {
        return RoboParseError;
    }
    *outCount = m.numGlyphs;
    return RoboOk;
}

Result FontCmapMapCreate(FontSfnt *sfnt,
                                        uint32_t numGlyphs,
                                        FontCmapMap **out)
{
    FontSpan cmap;
    FontCmap structural;
    Result res;
    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    if (numGlyphs == 0u) {
        if (FontSfntNumGlyphs(sfnt, &numGlyphs) != RoboOk ||
            numGlyphs == 0u) {
            return RoboInvalidArgument;
        }
    }
    if (FontSfntTableBytes(sfnt, RoboFontTagCmap, &cmap) != RoboOk) {
        return RoboNotFound;
    }
    memset(&structural, 0, sizeof(structural));
    res = FontCmapParse(cmap, &structural);
    if (res != RoboOk) {
        return res;
    }
    return MapBuildFromStructural(&structural, numGlyphs, sfnt, out);
}

void FontCmapMapRetain(FontCmapMap *map)
{
    if (map) {
        map->refs++;
    }
}

void FontCmapMapRelease(FontCmapMap *map)
{
    if (!map) {
        return;
    }
    if (map->refs > 1u) {
        map->refs--;
        return;
    }
    ClearBody(&map->primary);
    ClearBody(&map->uvsBody);
    FontCmapClear(&map->structural);
    if (map->sfnt) {
        FontSfntRelease(map->sfnt);
    }
    RoboFree(map);
}

uint32_t FontCmapMapNumGlyphs(const FontCmapMap *map)
{
    return map ? map->numGlyphs : 0u;
}

uint16_t FontCmapMapSelectedFormat(const FontCmapMap *map)
{
    return (map && map->hasPrimary) ? map->primary.format : 0u;
}

uint16_t FontCmapMapSelectedPlatform(const FontCmapMap *map)
{
    return (map && map->hasPrimary) ? map->primary.platform : 0u;
}

uint16_t FontCmapMapSelectedEncoding(const FontCmapMap *map)
{
    return (map && map->hasPrimary) ? map->primary.encoding : 0u;
}

int FontCmapMapHasUvs(const FontCmapMap *map)
{
    return map && map->hasUvs;
}

Result FontCmapLookup(const FontCmapMap *map,
                                    uint32_t codePoint,
                                    FontCmapLookupResult *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (!map || !map->hasPrimary) {
        out->status = RoboFontCmapInvalidFont;
        return RoboOk;
    }
    if (!FontCmapIsUnicodeScalar(codePoint)) {
        out->status = RoboFontCmapInvalidCodePoint;
        return RoboOk;
    }
    return LookupBody(&map->primary, codePoint, map->numGlyphs, out);
}

GlyphId FontCmapLookupGlyph(const FontCmapMap *map,
                                            uint32_t codePoint,
                                            FontCmapLookupStatus *outStatus)
{
    FontCmapLookupResult r;
    if (FontCmapLookup(map, codePoint, &r) != RoboOk) {
        if (outStatus) {
            *outStatus = RoboFontCmapInvalidFont;
        }
        return 0u;
    }
    if (outStatus) {
        *outStatus = r.status;
    }
    return (r.status == RoboFontCmapMapped) ? r.glyphId : 0u;
}

Result FontCmapLookupUvs(const FontCmapMap *map,
                                        uint32_t baseCodePoint,
                                        uint32_t variationSelector,
                                        FontCmapUvsResult *out)
{
    uint32_t i;
    const CmapUvsSelector *sel = NULL;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->baseCodePoint = baseCodePoint;
    out->variationSelector = variationSelector;
    if (!map) {
        out->status = RoboFontCmapUvsInvalidInput;
        return RoboOk;
    }
    if (!FontCmapIsUnicodeScalar(baseCodePoint) ||
        !FontCmapIsUnicodeScalar(variationSelector)) {
        out->status = RoboFontCmapUvsInvalidInput;
        return RoboOk;
    }
    if (!map->hasUvs) {
        out->status = RoboFontCmapUvsNone;
        return RoboOk;
    }
    for (i = 0u; i < map->uvsBody.uvsCount; i++) {
        if (map->uvsBody.uvs[i].varSelector == variationSelector) {
            sel = &map->uvsBody.uvs[i];
            break;
        }
    }
    if (!sel) {
        out->status = RoboFontCmapUvsNone;
        return RoboOk;
    }
    /* non-default first */
    {
        uint32_t lo = 0u;
        uint32_t hi = sel->numNonDefault;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (baseCodePoint > sel->nonDefault[mid].unicode) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        if (lo < sel->numNonDefault &&
            sel->nonDefault[lo].unicode == baseCodePoint) {
            uint32_t gid = sel->nonDefault[lo].glyphId;
            if (!GidOk(map->numGlyphs, gid)) {
                out->status = RoboFontCmapUvsInvalidInput;
                return RoboOk;
            }
            out->status = RoboFontCmapUvsNonDefault;
            out->glyphId = gid;
            return RoboOk;
        }
    }
    /* default UVS ranges */
    for (i = 0u; i < sel->numDefaultRanges; i++) {
        uint32_t start = sel->defaultRanges[i].startUnicode;
        uint32_t end = start + (uint32_t)sel->defaultRanges[i].additionalCount;
        if (baseCodePoint >= start && baseCodePoint <= end) {
            out->status = RoboFontCmapUvsDefault;
            return RoboOk;
        }
    }
    out->status = RoboFontCmapUvsUnsupported;
    return RoboOk;
}

static Result ForeachEmitMapped(const CmapBody *b,
                                         uint32_t numGlyphs,
                                         uint32_t cp,
                                         FontCmapMappedFn fn,
                                         void *user)
{
    FontCmapLookupResult r;
    Result res = LookupBody(b, cp, numGlyphs, &r);
    if (res != RoboOk) {
        return res;
    }
    if (r.status != RoboFontCmapMapped) {
        return RoboOk;
    }
    return fn(cp, r.glyphId, user);
}

static Result ForeachRange(const CmapBody *b,
                                   uint32_t numGlyphs,
                                   uint32_t first,
                                   uint32_t last,
                                   FontCmapMappedFn fn,
                                   void *user)
{
    uint32_t cp;
    if (last < first) {
        return RoboOk;
    }
    for (cp = first; cp <= last; cp++) {
        Result res = ForeachEmitMapped(b, numGlyphs, cp, fn, user);
        if (res != RoboOk) {
            return res;
        }
        if (cp == 0xFFFFFFFFu) {
            break;
        }
    }
    return RoboOk;
}

Result FontCmapForeachMapped(const FontCmapMap *map,
                                            FontCmapMappedFn fn,
                                            void *user)
{
    const CmapBody *b;
    uint32_t i;

    if (!map || !fn || !map->hasPrimary) {
        return RoboInvalidArgument;
    }
    b = &map->primary;
    switch (b->format) {
    case 0:
        return ForeachRange(b, map->numGlyphs, 0u, 0xFFu, fn, user);
    case 2:
        /* Format 2 domain is BMP; sparse — scan with lookup. */
        return ForeachRange(b, map->numGlyphs, 0u, 0xFFFFu, fn, user);
    case 4:
        for (i = 0u; i < b->fmt4SegCount; i++) {
            uint16_t start = b->fmt4Segs[i].start;
            uint16_t end = b->fmt4Segs[i].end;
            Result res;
            if (end == 0xFFFFu && start == 0xFFFFu) {
                continue;
            }
            res = ForeachRange(b, map->numGlyphs, start, end, fn, user);
            if (res != RoboOk) {
                return res;
            }
        }
        return RoboOk;
    case 6:
        if (b->fmt6Count == 0u) {
            return RoboOk;
        }
        return ForeachRange(b, map->numGlyphs, b->fmt6First,
                             (uint32_t)b->fmt6First + (uint32_t)b->fmt6Count - 1u,
                             fn, user);
    case 10:
        if (b->fmt10Count == 0u) {
            return RoboOk;
        }
        return ForeachRange(b, map->numGlyphs, b->fmt10Start,
                             b->fmt10Start + b->fmt10Count - 1u, fn, user);
    case 12:
    case 13:
        for (i = 0u; i < b->groupCount; i++) {
            Result res = ForeachRange(b, map->numGlyphs, b->groups[i].start,
                                              b->groups[i].end, fn, user);
            if (res != RoboOk) {
                return res;
            }
        }
        return RoboOk;
    default:
        return RoboNotSupported;
    }
}

Result FontCmapFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontCmapMap *map = NULL;
    Result res;
    uint32_t nglyphs = 1u;

    if (!data && size > 0u) {
        return RoboInvalidArgument;
    }
    /* Try as full SFNT first. */
    res = FontSourceCreateMemoryCopy(data, size, &src);
    if (res != RoboOk) {
        return res;
    }
    res = FontSfntParse(src, 0u, 0u, &sfnt);
    if (res == RoboOk) {
        (void)FontSfntNumGlyphs(sfnt, &nglyphs);
        if (nglyphs == 0u) {
            nglyphs = 1u;
        }
        res = FontCmapMapCreate(sfnt, nglyphs, &map);
        if (map) {
            FontCmapLookupResult lr;
            (void)FontCmapLookup(map, 0x41u, &lr);
            FontCmapMapRelease(map);
        }
        FontSfntRelease(sfnt);
        FontSourceRelease(src);
        return res == RoboOutOfMemory ? res : RoboOk;
    }
    FontSourceRelease(src);
    /* Raw cmap span */
    {
        FontSpan span = {data, size};
        res = FontCmapMapCreateFromSpan(span, 256u, &map);
        if (map) {
            FontCmapMapRelease(map);
        }
        return res == RoboOutOfMemory ? res : RoboOk;
    }
}
