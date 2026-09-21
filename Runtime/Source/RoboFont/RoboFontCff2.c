/*
 * CFF2 Item Variation Store foundation (Goal 5.5).
 * Structural parse only — no instance evaluation (Goal 5.18).
 */

#include "RoboFont/RoboFontCff.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void FontCffVstoreClear(FontCffVstore *v)
{
    uint16_t i;
    if (!v) {
        return;
    }
    if (v->regions) {
        for (i = 0u; i < v->regionCount; i++) {
            RoboFree(v->regions[i].axes);
        }
        RoboFree(v->regions);
    }
    if (v->data) {
        for (i = 0u; i < v->dataCount; i++) {
            RoboFree(v->data[i].regionIndices);
        }
        RoboFree(v->data);
    }
    memset(v, 0, sizeof(*v));
}

Result FontCffVstoreParse(FontSpan table,
                                         size_t offset,
                                         FontCffVstore *out)
{
    FontReader r;
    uint16_t format;
    uint32_t regionListOffset;
    uint16_t dataCount;
    uint32_t *dataOffsets = NULL;
    size_t regionListAbs;
    uint16_t axisCount;
    uint16_t regionCount;
    uint16_t i;
    uint16_t j;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontCffVstoreClear(out);

    if (offset + 8u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &format) != RoboOk) {
        return RoboParseError;
    }
    out->format = format;
    if (format != 1u) {
        return RoboNotSupported;
    }

    {
        uint32_t tmp;
        if (FontReadU32be(&r, &tmp) != RoboOk) {
            return RoboParseError;
        }
        regionListOffset = tmp;
    }
    if (FontReadU16be(&r, &dataCount) != RoboOk) {
        return RoboParseError;
    }
    if (dataCount > RoboFontCffMaxVarRegions) {
        return RoboParseError;
    }
    out->dataCount = dataCount;

    if (dataCount > 0u) {
        dataOffsets = (uint32_t *)RoboCalloc(dataCount, sizeof(uint32_t));
        if (!dataOffsets) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < dataCount; i++) {
            if (FontReadU32be(&r, &dataOffsets[i]) != RoboOk) {
                RoboFree(dataOffsets);
                return RoboParseError;
            }
        }
    }

    /* RegionList is relative to VariationStore start */
    regionListAbs = offset + (size_t)regionListOffset;
    if (regionListAbs + 4u > table.size) {
        RoboFree(dataOffsets);
        return RoboParseError;
    }
    if (FontReaderSeek(&r, regionListAbs) != RoboOk ||
        FontReadU16be(&r, &axisCount) != RoboOk ||
        FontReadU16be(&r, &regionCount) != RoboOk) {
        RoboFree(dataOffsets);
        return RoboParseError;
    }
    out->axisCount = axisCount;
    out->regionCount = regionCount;
    if (regionCount > RoboFontCffMaxVarRegions || axisCount > 64u) {
        RoboFree(dataOffsets);
        return RoboParseError;
    }

    if (regionCount > 0u) {
        out->regions = (FontCffVarRegion *)RoboCalloc(
            regionCount, sizeof(FontCffVarRegion));
        if (!out->regions) {
            RoboFree(dataOffsets);
            return RoboOutOfMemory;
        }
        for (i = 0u; i < regionCount; i++) {
            out->regions[i].axisCount = axisCount;
            if (axisCount > 0u) {
                out->regions[i].axes = (FontCffAxisCoord *)RoboCalloc(
                    axisCount, sizeof(FontCffAxisCoord));
                if (!out->regions[i].axes) {
                    RoboFree(dataOffsets);
                    FontCffVstoreClear(out);
                    return RoboOutOfMemory;
                }
                for (j = 0u; j < axisCount; j++) {
                    if (FontReadI16be(&r, &out->regions[i].axes[j].startCoord) !=
                            RoboOk ||
                        FontReadI16be(&r, &out->regions[i].axes[j].peakCoord) !=
                            RoboOk ||
                        FontReadI16be(&r, &out->regions[i].axes[j].endCoord) !=
                            RoboOk) {
                        RoboFree(dataOffsets);
                        FontCffVstoreClear(out);
                        return RoboParseError;
                    }
                }
            }
        }
    }

    if (dataCount > 0u) {
        out->data = (FontCffVarData *)RoboCalloc(
            dataCount, sizeof(FontCffVarData));
        if (!out->data) {
            RoboFree(dataOffsets);
            FontCffVstoreClear(out);
            return RoboOutOfMemory;
        }
        for (i = 0u; i < dataCount; i++) {
            size_t abs = offset + (size_t)dataOffsets[i];
            uint16_t itemCount;
            uint16_t shortDeltaCount;
            uint16_t regionIndexCount;
            size_t deltaBytes;
            if (abs + 6u > table.size) {
                RoboFree(dataOffsets);
                FontCffVstoreClear(out);
                return RoboParseError;
            }
            if (FontReaderSeek(&r, abs) != RoboOk ||
                FontReadU16be(&r, &itemCount) != RoboOk ||
                FontReadU16be(&r, &shortDeltaCount) != RoboOk ||
                FontReadU16be(&r, &regionIndexCount) != RoboOk) {
                RoboFree(dataOffsets);
                FontCffVstoreClear(out);
                return RoboParseError;
            }
            out->data[i].itemCount = itemCount;
            out->data[i].shortDeltaCount = shortDeltaCount;
            out->data[i].regionIndexCount = regionIndexCount;
            if (regionIndexCount > regionCount ||
                shortDeltaCount > regionIndexCount) {
                RoboFree(dataOffsets);
                FontCffVstoreClear(out);
                return RoboParseError;
            }
            if (regionIndexCount > 0u) {
                out->data[i].regionIndices =
                    (uint16_t *)RoboCalloc(regionIndexCount, sizeof(uint16_t));
                if (!out->data[i].regionIndices) {
                    RoboFree(dataOffsets);
                    FontCffVstoreClear(out);
                    return RoboOutOfMemory;
                }
                for (j = 0u; j < regionIndexCount; j++) {
                    if (FontReadU16be(&r, &out->data[i].regionIndices[j]) !=
                        RoboOk) {
                        RoboFree(dataOffsets);
                        FontCffVstoreClear(out);
                        return RoboParseError;
                    }
                    if (out->data[i].regionIndices[j] >= regionCount) {
                        RoboFree(dataOffsets);
                        FontCffVstoreClear(out);
                        return RoboParseError;
                    }
                }
            }
            /* DeltaSet: per item: shortDeltaCount*int16 + (regionCount-short)*int8 */
            deltaBytes =
                (size_t)itemCount *
                ((size_t)shortDeltaCount * 2u +
                 (size_t)(regionIndexCount - shortDeltaCount));
            if (FontSpanSlice(table, FontReaderPos(&r), deltaBytes,
                                     &out->data[i].deltaSets) != RoboOk) {
                RoboFree(dataOffsets);
                FontCffVstoreClear(out);
                return RoboParseError;
            }
        }
    }

    RoboFree(dataOffsets);
    return RoboOk;
}
