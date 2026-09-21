/*
 * CFF INDEX + offset helpers (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <stdint.h>
#include <string.h>

Result FontCffReadOffset(FontReader *r,
                                        uint8_t offSize,
                                        uint32_t *out)
{
    uint32_t v = 0u;
    uint8_t i;
    uint8_t b;

    if (!r || !out) {
        return RoboInvalidArgument;
    }
    if (offSize < 1u || offSize > 4u) {
        return RoboParseError;
    }
    for (i = 0u; i < offSize; i++) {
        if (FontReadU8(r, &b) != RoboOk) {
            return RoboParseError;
        }
        v = (v << 8) | (uint32_t)b;
    }
    *out = v;
    return RoboOk;
}

void FontCffIndexClear(FontCffIndex *idx)
{
    if (!idx) {
        return;
    }
    RoboFree(idx->dataOffsets);
    RoboFree(idx->dataLengths);
    memset(idx, 0, sizeof(*idx));
}

static Result IndexParseCommon(FontSpan table,
                                        size_t offset,
                                        uint32_t count,
                                        size_t afterCount,
                                        FontCffIndex *out,
                                        size_t *outEnd)
{
    FontReader r;
    uint8_t offSize;
    uint32_t *rawOffs = NULL;
    uint32_t i;
    uint32_t dataSize;
    size_t offsetArrayBytes;
    size_t dataBase;
    size_t end;

    if (!out || !outEnd) {
        return RoboInvalidArgument;
    }
    FontCffIndexClear(out);

    if (count == 0u) {
        out->count = 0u;
        out->table = table;
        out->dataBase = afterCount;
        *outEnd = afterCount;
        return RoboOk;
    }

    if (count > RoboFontCffMaxIndexCount) {
        return RoboParseError;
    }

    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, afterCount) != RoboOk ||
        FontReadU8(&r, &offSize) != RoboOk) {
        return RoboParseError;
    }
    if (offSize < 1u || offSize > 4u) {
        return RoboParseError;
    }

    /* count+1 offsets */
    if ((size_t)(count + 1u) > (SIZE_MAX / (size_t)offSize)) {
        return RoboParseError;
    }
    offsetArrayBytes = (size_t)(count + 1u) * (size_t)offSize;
    if (FontCheckedRange(table.size, afterCount + 1u, offsetArrayBytes) !=
        RoboOk) {
        return RoboParseError;
    }

    rawOffs = (uint32_t *)RoboCalloc(count + 1u, sizeof(uint32_t));
    if (!rawOffs) {
        return RoboOutOfMemory;
    }

    for (i = 0u; i <= count; i++) {
        if (FontCffReadOffset(&r, offSize, &rawOffs[i]) != RoboOk) {
            RoboFree(rawOffs);
            return RoboParseError;
        }
        if (i > 0u && rawOffs[i] < rawOffs[i - 1u]) {
            RoboFree(rawOffs);
            return RoboParseError;
        }
        if (rawOffs[i] == 0u) {
            RoboFree(rawOffs);
            return RoboParseError; /* offsets are 1-based */
        }
    }

    dataSize = rawOffs[count] - 1u;
    if (dataSize > RoboFontCffMaxIndexObject * (count > 0u ? count : 1u) &&
        dataSize > (1u << 24)) {
        /* soft bound: total data region */
        if (dataSize > (32u << 20)) {
            RoboFree(rawOffs);
            return RoboParseError;
        }
    }

    dataBase = afterCount + 1u + offsetArrayBytes;
    if (FontCheckedRange(table.size, dataBase, dataSize) != RoboOk) {
        RoboFree(rawOffs);
        return RoboParseError;
    }
    end = dataBase + dataSize;

    out->dataOffsets = (uint32_t *)RoboCalloc(count, sizeof(uint32_t));
    out->dataLengths = (uint32_t *)RoboCalloc(count, sizeof(uint32_t));
    if (!out->dataOffsets || !out->dataLengths) {
        RoboFree(rawOffs);
        FontCffIndexClear(out);
        return RoboOutOfMemory;
    }

    for (i = 0u; i < count; i++) {
        uint32_t start = rawOffs[i] - 1u;
        uint32_t stop = rawOffs[i + 1u] - 1u;
        uint32_t len = stop - start;
        if (len > RoboFontCffMaxIndexObject) {
            RoboFree(rawOffs);
            FontCffIndexClear(out);
            return RoboParseError;
        }
        out->dataOffsets[i] = (uint32_t)(dataBase + start);
        out->dataLengths[i] = len;
    }

    RoboFree(rawOffs);
    out->count = count;
    out->offSize = offSize;
    out->table = table;
    out->dataBase = dataBase;
    *outEnd = end;
    (void)offset;
    return RoboOk;
}

Result FontCffIndexParse(FontSpan table,
                                        size_t offset,
                                        FontCffIndex *out,
                                        size_t *outEnd)
{
    FontReader r;
    uint16_t count16;

    if (!out || !outEnd) {
        return RoboInvalidArgument;
    }
    if (offset > table.size || table.size - offset < 2u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &count16) != RoboOk) {
        return RoboParseError;
    }
    return IndexParseCommon(table, offset, (uint32_t)count16, offset + 2u, out,
                              outEnd);
}

Result FontCff2IndexParse(FontSpan table,
                                         size_t offset,
                                         FontCffIndex *out,
                                         size_t *outEnd)
{
    FontReader r;
    uint32_t count32;

    if (!out || !outEnd) {
        return RoboInvalidArgument;
    }
    if (offset > table.size || table.size - offset < 4u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU32be(&r, &count32) != RoboOk) {
        return RoboParseError;
    }
    return IndexParseCommon(table, offset, count32, offset + 4u, out, outEnd);
}

uint32_t FontCffIndexCount(const FontCffIndex *idx)
{
    return idx ? idx->count : 0u;
}

Result FontCffIndexGet(const FontCffIndex *idx,
                                      uint32_t i,
                                      FontSpan *out)
{
    if (!idx || !out) {
        return RoboInvalidArgument;
    }
    if (i >= idx->count) {
        return RoboParseError;
    }
    return FontSpanSlice(idx->table, idx->dataOffsets[i],
                                idx->dataLengths[i], out);
}

Result FontCffIndexGetOffset(const FontCffIndex *idx,
                                             uint32_t i,
                                             uint32_t *outAbs)
{
    if (!idx || !outAbs) {
        return RoboInvalidArgument;
    }
    if (i >= idx->count) {
        return RoboParseError;
    }
    *outAbs = idx->dataOffsets[i];
    return RoboOk;
}

Result FontCffIndexGetLength(const FontCffIndex *idx,
                                             uint32_t i,
                                             uint32_t *outLen)
{
    if (!idx || !outLen) {
        return RoboInvalidArgument;
    }
    if (i >= idx->count) {
        return RoboParseError;
    }
    *outLen = idx->dataLengths[i];
    return RoboOk;
}

uint32_t FontCffSubrBias(uint32_t nSubrs)
{
    if (nSubrs < 1240u) {
        return 107u;
    }
    if (nSubrs < 33900u) {
        return 1131u;
    }
    return 32768u;
}
