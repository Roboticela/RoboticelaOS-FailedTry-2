#include "RoboFont/RoboFontSfnt.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>

struct FontSfnt {
    uint32_t refs;
    FontSource *source;
    FontSfntHeader header;
    FontSfntTableRecord *tables; /* numTables entries, sorted by tag */
    uint32_t tableCount;
    size_t sfntBase; /* offset of this face's SFNT header in source */
    uint32_t flags;
};

int FontSfntVersionIsSupported(uint32_t version)
{
    return version == RoboFontSfntVersionTrue ||
           version == RoboFontSfntVersionTrueTag ||
           version == RoboFontSfntVersionTyp1 ||
           version == RoboFontSfntVersionOtto;
}

uint32_t FontSfntChecksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0u;
    size_t i;
    if (!data && length != 0u) {
        return 0u;
    }
    for (i = 0u; i + 4u <= length; i += 4u) {
        sum += ((uint32_t)data[i] << 24) | ((uint32_t)data[i + 1u] << 16) |
               ((uint32_t)data[i + 2u] << 8) | (uint32_t)data[i + 3u];
    }
    if (i < length) {
        uint32_t word = 0u;
        size_t rem = length - i;
        if (rem >= 1u) {
            word |= (uint32_t)data[i] << 24;
        }
        if (rem >= 2u) {
            word |= (uint32_t)data[i + 1u] << 16;
        }
        if (rem >= 3u) {
            word |= (uint32_t)data[i + 2u] << 8;
        }
        sum += word;
    }
    return sum;
}

int FontSfntSearchFieldsConsistent(const FontSfntHeader *h)
{
    uint16_t n;
    uint16_t pow2;
    uint16_t entry;
    uint16_t search;
    uint16_t shift;
    if (!h) {
        return 0;
    }
    n = h->numTables;
    if (n == 0u) {
        return h->searchRange == 0u && h->entrySelector == 0u && h->rangeShift == 0u;
    }
    pow2 = 1u;
    entry = 0u;
    while ((uint16_t)(pow2 * 2u) <= n) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
        if (entry > 15u) {
            break;
        }
    }
    search = (uint16_t)(pow2 * 16u);
    shift = (uint16_t)(n * 16u - search);
    return h->searchRange == search && h->entrySelector == entry &&
           h->rangeShift == shift;
}

Result FontTagFormat(FontTag tag, char *buf, size_t bufCap)
{
    int i;
    if (!buf || bufCap < 5u) {
        return RoboInvalidArgument;
    }
    for (i = 0; i < 4; i++) {
        uint8_t c = (uint8_t)((tag >> (24 - 8 * i)) & 0xFFu);
        buf[i] = (c >= 0x20u && c <= 0x7Eu) ? (char)c : '.';
    }
    buf[4] = '\0';
    return RoboOk;
}

/* Insertion sort — table count is small (≤4095). */
static void SortTables(FontSfntTableRecord *t, uint32_t n)
{
    uint32_t i;
    for (i = 1u; i < n; i++) {
        FontSfntTableRecord key = t[i];
        uint32_t j = i;
        while (j > 0u && t[j - 1u].tag > key.tag) {
            t[j] = t[j - 1u];
            j--;
        }
        t[j] = key;
    }
}

static int HasDuplicateTags(const FontSfntTableRecord *t, uint32_t n)
{
    uint32_t i;
    for (i = 1u; i < n; i++) {
        if (t[i].tag == t[i - 1u].tag) {
            return 1;
        }
    }
    return 0;
}

typedef struct {
    uint32_t offset;
    uint8_t isEnd;
} Edge;

/* Ends before starts at the same offset so adjacent (non-overlapping) tables pass. */
static int EdgeLess(const Edge *a, const Edge *b)
{
    if (a->offset != b->offset) {
        return a->offset < b->offset;
    }
    return a->isEnd > b->isEnd;
}

static void SortEdges(Edge *e, uint32_t n)
{
    uint32_t i;
    for (i = 1u; i < n; i++) {
        Edge key = e[i];
        uint32_t j = i;
        while (j > 0u && EdgeLess(&key, &e[j - 1u])) {
            e[j] = e[j - 1u];
            j--;
        }
        e[j] = key;
    }
}

static Result RejectOverlaps(const FontSfntTableRecord *tables,
                                     uint32_t n)
{
    Edge *edges;
    uint32_t i;
    uint32_t active;
    uint32_t en;
    uint32_t ecount;

    if (n == 0u) {
        return RoboOk;
    }
    en = n * 2u;
    edges = (Edge *)RoboCalloc(en, sizeof(Edge));
    if (!edges) {
        return RoboOutOfMemory;
    }
    ecount = 0u;
    for (i = 0u; i < n; i++) {
        uint32_t end = tables[i].offset + tables[i].length;
        if (end < tables[i].offset) {
            RoboFree(edges);
            return RoboParseError;
        }
        /* Zero-length tables occupy no byte range. */
        if (tables[i].length == 0u) {
            continue;
        }
        edges[ecount].offset = tables[i].offset;
        edges[ecount].isEnd = 0u;
        ecount++;
        edges[ecount].offset = end;
        edges[ecount].isEnd = 1u;
        ecount++;
    }
    SortEdges(edges, ecount);
    active = 0u;
    for (i = 0u; i < ecount; i++) {
        if (edges[i].isEnd) {
            if (active == 0u) {
                RoboFree(edges);
                return RoboParseError;
            }
            active--;
        } else {
            active++;
            if (active > 1u) {
                RoboFree(edges);
                return RoboParseError;
            }
        }
    }
    RoboFree(edges);
    return RoboOk;
}

static Result ParseSfntAt(FontSource *source,
                                   size_t base,
                                   uint32_t flags,
                                   FontSfnt **out)
{
    FontReader r;
    FontSfnt *sfnt;
    const uint8_t *bytes;
    size_t size;
    uint32_t version;
    uint16_t numTables;
    uint16_t searchRange;
    uint16_t entrySelector;
    uint16_t rangeShift;
    uint32_t i;
    size_t dirEnd;
    size_t headerEnd;
    Result res;

    if (!out || !source) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    bytes = FontSourceBytes(source);
    size = FontSourceSize(source);
    if (!bytes || size == 0u) {
        return RoboInvalidArgument;
    }
    if (size > RoboFontSfntMaxSourceSize) {
        return RoboParseError;
    }
    if (base > size || size - base < 12u) {
        return RoboParseError;
    }

    FontReaderInit(&r, bytes, size);
    if (FontReaderSeek(&r, base) != RoboOk) {
        return RoboParseError;
    }

    if (FontReadU32be(&r, &version) != RoboOk) {
        return RoboParseError;
    }
    if (!FontSfntVersionIsSupported(version)) {
        return RoboNotSupported;
    }
    if (FontReadU16be(&r, &numTables) != RoboOk ||
        FontReadU16be(&r, &searchRange) != RoboOk ||
        FontReadU16be(&r, &entrySelector) != RoboOk ||
        FontReadU16be(&r, &rangeShift) != RoboOk) {
        return RoboParseError;
    }

    if (numTables == 0u || numTables > RoboFontSfntMaxTables) {
        return RoboParseError;
    }

    headerEnd = base + 12u;
    /* directory size = numTables * 16, check overflow */
    if ((size_t)numTables > (SIZE_MAX / 16u)) {
        return RoboParseError;
    }
    dirEnd = headerEnd + (size_t)numTables * 16u;
    if (dirEnd < headerEnd || dirEnd > size) {
        return RoboParseError;
    }

    sfnt = (FontSfnt *)RoboCalloc(1, sizeof(*sfnt));
    if (!sfnt) {
        return RoboOutOfMemory;
    }
    sfnt->tables = (FontSfntTableRecord *)RoboCalloc(
        numTables, sizeof(FontSfntTableRecord));
    if (!sfnt->tables) {
        RoboFree(sfnt);
        return RoboOutOfMemory;
    }

    for (i = 0u; i < numTables; i++) {
        uint32_t tag;
        uint32_t checksum;
        uint32_t offset;
        uint32_t length;
        if (FontReadU32be(&r, &tag) != RoboOk ||
            FontReadU32be(&r, &checksum) != RoboOk ||
            FontReadU32be(&r, &offset) != RoboOk ||
            FontReadU32be(&r, &length) != RoboOk) {
            RoboFree(sfnt->tables);
            RoboFree(sfnt);
            return RoboParseError;
        }
        /* Offset must be 4-byte aligned per OpenType. */
        if ((offset & 3u) != 0u) {
            RoboFree(sfnt->tables);
            RoboFree(sfnt);
            return RoboParseError;
        }
        if (FontCheckedRange(size, offset, length) != RoboOk) {
            RoboFree(sfnt->tables);
            RoboFree(sfnt);
            return RoboParseError;
        }
        /* Reject tables that overlap this face's header + directory.
         * Earlier absolute offsets are allowed (TTC shared tables). */
        if (length != 0u) {
            size_t tend = (size_t)offset + (size_t)length;
            if ((size_t)offset < dirEnd && base < tend) {
                RoboFree(sfnt->tables);
                RoboFree(sfnt);
                return RoboParseError;
            }
        } else if ((size_t)offset >= base && (size_t)offset < dirEnd) {
            RoboFree(sfnt->tables);
            RoboFree(sfnt);
            return RoboParseError;
        }
        sfnt->tables[i].tag = tag;
        sfnt->tables[i].checksum = checksum;
        sfnt->tables[i].offset = offset;
        sfnt->tables[i].length = length;
    }

    res = RejectOverlaps(sfnt->tables, numTables);
    if (res != RoboOk) {
        RoboFree(sfnt->tables);
        RoboFree(sfnt);
        return res;
    }

    SortTables(sfnt->tables, numTables);
    if (HasDuplicateTags(sfnt->tables, numTables)) {
        RoboFree(sfnt->tables);
        RoboFree(sfnt);
        return RoboParseError;
    }

    if ((flags & RoboFontSfntFlagsVerifyChecksums) != 0u) {
        for (i = 0u; i < numTables; i++) {
            const FontSfntTableRecord *tr = &sfnt->tables[i];
            uint32_t calc =
                FontSfntChecksum(bytes + tr->offset, (size_t)tr->length);
            /* head checksumAdjustment is not applied here (Goal 5.3+). */
            if (calc != tr->checksum) {
                RoboFree(sfnt->tables);
                RoboFree(sfnt);
                return RoboParseError;
            }
        }
    }

    sfnt->refs = 1u;
    FontSourceRetain(source);
    sfnt->source = source;
    sfnt->header.sfntVersion = version;
    sfnt->header.numTables = numTables;
    sfnt->header.searchRange = searchRange;
    sfnt->header.entrySelector = entrySelector;
    sfnt->header.rangeShift = rangeShift;
    sfnt->tableCount = numTables;
    sfnt->sfntBase = base;
    sfnt->flags = flags;
    *out = sfnt;
    return RoboOk;
}

static Result ResolveFaceBase(FontSource *source,
                                       uint32_t faceIndex,
                                       size_t *outBase)
{
    FontReader r;
    const uint8_t *bytes;
    size_t size;
    uint32_t tag;
    uint32_t ttcVersion;
    uint32_t numFonts;
    uint32_t offset;

    if (!outBase) {
        return RoboInvalidArgument;
    }
    *outBase = 0u;
    bytes = FontSourceBytes(source);
    size = FontSourceSize(source);
    if (!bytes || size < 4u) {
        return RoboParseError;
    }

    FontReaderInit(&r, bytes, size);
    if (FontPeekU32be(&r, &tag) != RoboOk) {
        return RoboParseError;
    }

    if (tag != RoboFontSfntVersionTtcf) {
        if (faceIndex != 0u) {
            return RoboNotFound;
        }
        *outBase = 0u;
        return RoboOk;
    }

    /* TTC header: 'ttcf', uint32 version, uint32 numFonts, offsets... */
    if (size < 12u) {
        return RoboParseError;
    }
    if (FontReadU32be(&r, &tag) != RoboOk ||
        FontReadU32be(&r, &ttcVersion) != RoboOk ||
        FontReadU32be(&r, &numFonts) != RoboOk) {
        return RoboParseError;
    }
    if (ttcVersion != 0x00010000u && ttcVersion != 0x00020000u) {
        return RoboNotSupported;
    }
    if (numFonts == 0u || numFonts > RoboFontSfntMaxTables) {
        return RoboParseError;
    }
    if (faceIndex >= numFonts) {
        return RoboNotFound;
    }
    if (FontCheckedRange(size, 12u, (size_t)numFonts * 4u) != RoboOk) {
        return RoboParseError;
    }
    if (FontReaderSeek(&r, 12u + (size_t)faceIndex * 4u) != RoboOk ||
        FontReadU32be(&r, &offset) != RoboOk) {
        return RoboParseError;
    }
    if ((size_t)offset > size || size - (size_t)offset < 12u) {
        return RoboParseError;
    }
    *outBase = (size_t)offset;
    return RoboOk;
}

Result FontSfntParse(FontSource *source,
                                   uint32_t faceIndex,
                                   uint32_t flags,
                                   FontSfnt **out)
{
    size_t base = 0u;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!source) {
        return RoboInvalidArgument;
    }
    if (FontSourceSize(source) > RoboFontSfntMaxSourceSize) {
        return RoboParseError;
    }

    res = ResolveFaceBase(source, faceIndex, &base);
    if (res != RoboOk) {
        return res;
    }
    return ParseSfntAt(source, base, flags, out);
}

void FontSfntRetain(FontSfnt *sfnt)
{
    if (!sfnt) {
        return;
    }
    sfnt->refs++;
}

void FontSfntRelease(FontSfnt *sfnt)
{
    if (!sfnt) {
        return;
    }
    if (sfnt->refs == 0u) {
        return;
    }
    sfnt->refs--;
    if (sfnt->refs != 0u) {
        return;
    }
    FontSourceRelease(sfnt->source);
    sfnt->source = NULL;
    RoboFree(sfnt->tables);
    sfnt->tables = NULL;
    RoboFree(sfnt);
}

const FontSource *FontSfntSource(const FontSfnt *sfnt)
{
    return sfnt ? sfnt->source : NULL;
}

const FontSfntHeader *GetFontSfntHeader(const FontSfnt *sfnt)
{
    return sfnt ? &sfnt->header : NULL;
}

uint32_t FontSfntTableCount(const FontSfnt *sfnt)
{
    return sfnt ? sfnt->tableCount : 0u;
}

static const FontSfntTableRecord *FindRec(const FontSfnt *sfnt,
                                                     FontTag tag)
{
    uint32_t lo;
    uint32_t hi;
    if (!sfnt || !sfnt->tables) {
        return NULL;
    }
    lo = 0u;
    hi = sfnt->tableCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (sfnt->tables[mid].tag < tag) {
            lo = mid + 1u;
        } else if (sfnt->tables[mid].tag > tag) {
            hi = mid;
        } else {
            return &sfnt->tables[mid];
        }
    }
    return NULL;
}

int FontSfntHasTable(const FontSfnt *sfnt, FontTag tag)
{
    return FindRec(sfnt, tag) != NULL;
}

Result FontSfntFindTable(const FontSfnt *sfnt,
                                        FontTag tag,
                                        FontSfntTableRecord *out)
{
    const FontSfntTableRecord *rec;
    if (!sfnt) {
        return RoboInvalidArgument;
    }
    rec = FindRec(sfnt, tag);
    if (!rec) {
        return RoboNotFound;
    }
    if (out) {
        *out = *rec;
    }
    return RoboOk;
}

Result FontSfntTableBytes(const FontSfnt *sfnt,
                                         FontTag tag,
                                         FontSpan *out)
{
    const FontSfntTableRecord *rec;
    FontSpan parent;
    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    out->data = NULL;
    out->size = 0u;
    rec = FindRec(sfnt, tag);
    if (!rec) {
        return RoboNotFound;
    }
    parent.data = FontSourceBytes(sfnt->source);
    parent.size = FontSourceSize(sfnt->source);
    return FontSpanSlice(parent, rec->offset, rec->length, out);
}

Result FontSfntTableAt(const FontSfnt *sfnt,
                                      uint32_t index,
                                      FontSfntTableRecord *out)
{
    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    if (index >= sfnt->tableCount) {
        return RoboNotFound;
    }
    *out = sfnt->tables[index];
    return RoboOk;
}

Result FontSfntVerifyTableChecksum(const FontSfnt *sfnt,
                                                   FontTag tag)
{
    const FontSfntTableRecord *rec;
    const uint8_t *bytes;
    uint32_t calc;
    if (!sfnt) {
        return RoboInvalidArgument;
    }
    rec = FindRec(sfnt, tag);
    if (!rec) {
        return RoboNotFound;
    }
    bytes = FontSourceBytes(sfnt->source);
    if (!bytes) {
        return RoboParseError;
    }
    calc = FontSfntChecksum(bytes + rec->offset, (size_t)rec->length);
    if (calc != rec->checksum) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontSfntFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    Result res;

    if (!data || size == 0u) {
        return RoboInvalidArgument;
    }
    if (size > RoboFontSfntMaxSourceSize) {
        return RoboParseError;
    }
    res = FontSourceCreateMemoryBorrow(data, size, &src);
    if (res != RoboOk) {
        return res;
    }
    res = FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt);
    if (sfnt) {
        FontSfntRelease(sfnt);
    }
    FontSourceRelease(src);
    return res;
}

Result FontSfntProbeFaceCount(const uint8_t *data,
                                              size_t size,
                                              uint32_t *outCount)
{
    uint32_t tag;
    uint32_t ttcVersion;
    uint32_t numFonts;

    if (!outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0u;
    if (!data || size < 4u) {
        return RoboParseError;
    }
    tag = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
          ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    if (tag == RoboFontSfntVersionTtcf) {
        if (size < 12u) {
            return RoboParseError;
        }
        ttcVersion = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                      ((uint32_t)data[6] << 8) | (uint32_t)data[7];
        numFonts = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) |
                    ((uint32_t)data[10] << 8) | (uint32_t)data[11];
        if (ttcVersion != 0x00010000u && ttcVersion != 0x00020000u) {
            return RoboNotSupported;
        }
        if (numFonts == 0u || numFonts > RoboFontSfntMaxTables) {
            return RoboParseError;
        }
        *outCount = numFonts;
        return RoboOk;
    }
    if (!FontSfntVersionIsSupported(tag)) {
        return RoboNotSupported;
    }
    *outCount = 1u;
    return RoboOk;
}

Result FontSfntProbeFaceCountSource(const FontSource *source,
                                                     uint32_t *outCount)
{
    if (!source) {
        return RoboInvalidArgument;
    }
    return FontSfntProbeFaceCount(FontSourceBytes(source),
                                           FontSourceSize(source), outCount);
}
