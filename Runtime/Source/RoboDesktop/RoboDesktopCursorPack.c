/*
 * RoboDesktopCursorPack — `.rcur` loader and frame resolver.
 *
 * Binary layout matches `Tools/Cursors/PackRoboCursors.py` / the pack spec:
 *   "RCUR" + major + minor + flags + reserved
 *   uint32 metaLen + UTF-8 JSON
 *   uint16 cursorCount
 *   repeated: uint8 stateId, uint8 nameLen, name, uint32 offset, uint32 length
 *   entry: uint8 themeCount; per theme hotspot@32, frames, PNG blobs
 */

#include "RoboDesktop/RoboDesktopCursorPack.h"

#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsDecode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define RoboDesktopCursorPackMaxEntries 32u
#define RoboDesktopCursorPackMaxThemes 2u
#define RoboDesktopCursorPackMaxFrames 16u
#define RoboDesktopCursorPackMaxResolutions 4u
#define RoboDesktopCursorPackMaxName 48u

typedef struct cursorPngSlot {
    const uint8_t *bytes;
    uint32_t length;
    uint16_t pixelSize;
    GfxImage *image;
} CursorPngSlot;

typedef struct cursorThemeData {
    int16_t hotspotX32;
    int16_t hotspotY32;
    uint8_t frameCount;
    uint16_t frameDurationMs;
    uint8_t loop;
    uint8_t resolutionCount;
    CursorPngSlot slots[RoboDesktopCursorPackMaxFrames * RoboDesktopCursorPackMaxResolutions];
} CursorThemeData;

typedef struct cursorEntry {
    uint8_t stateId;
    char name[RoboDesktopCursorPackMaxName];
    uint8_t themeCount;
    CursorThemeData themes[RoboDesktopCursorPackMaxThemes];
} CursorEntry;

struct DesktopCursorPack {
    uint8_t *ownedBytes;
    size_t ownedLength;
    uint32_t logicalPt;
    uint32_t entryCount;
    CursorEntry entries[RoboDesktopCursorPackMaxEntries];
};

static uint32_t ReadU32Le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t ReadU16Le(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static int16_t ReadI16Le(const uint8_t *p)
{
    return (int16_t)ReadU16Le(p);
}

static void CursorEntryClear(CursorEntry *entry)
{
    uint32_t t;
    uint32_t s;

    if (!entry) {
        return;
    }
    for (t = 0u; t < entry->themeCount && t < RoboDesktopCursorPackMaxThemes; t++) {
        for (s = 0u; s < (uint32_t)(entry->themes[t].frameCount * entry->themes[t].resolutionCount) &&
                     s < (uint32_t)(RoboDesktopCursorPackMaxFrames * RoboDesktopCursorPackMaxResolutions);
             s++) {
            if (entry->themes[t].slots[s].image) {
                GfxImageRelease(entry->themes[t].slots[s].image);
                entry->themes[t].slots[s].image = NULL;
            }
        }
    }
    memset(entry, 0, sizeof(*entry));
}

static Result DecodeSlot(CursorPngSlot *slot)
{
    GfxImage *image = NULL;
    Result status;

    if (!slot || !slot->bytes || slot->length == 0u) {
        return RoboInvalidArgument;
    }
    if (slot->image) {
        return RoboOk;
    }
    status = GfxImageDecode(slot->bytes, slot->length, NULL, &image);
    if (status != RoboOk || !image) {
        return status != RoboOk ? status : RoboError;
    }
    slot->image = image;
    return RoboOk;
}

static Result ParseEntryBody(CursorEntry *entry, const uint8_t *body, uint32_t bodyLen)
{
    size_t off = 0u;
    uint8_t themeCount;
    uint8_t ti;

    if (!entry || !body || bodyLen < 1u) {
        return RoboParseError;
    }
    themeCount = body[off++];
    if (themeCount == 0u || themeCount > RoboDesktopCursorPackMaxThemes) {
        return RoboParseError;
    }
    entry->themeCount = themeCount;

    for (ti = 0u; ti < themeCount; ti++) {
        CursorThemeData *theme = &entry->themes[ti];
        uint8_t themeId;
        uint8_t fi;
        uint32_t slotIndex = 0u;

        if (off + 1u + 2u + 2u + 1u + 2u + 1u > bodyLen) {
            return RoboParseError;
        }
        themeId = body[off++];
        (void)themeId;
        theme->hotspotX32 = ReadI16Le(body + off);
        off += 2u;
        theme->hotspotY32 = ReadI16Le(body + off);
        off += 2u;
        theme->frameCount = body[off++];
        theme->frameDurationMs = ReadU16Le(body + off);
        off += 2u;
        theme->loop = body[off++];
        if (theme->frameCount == 0u || theme->frameCount > RoboDesktopCursorPackMaxFrames) {
            return RoboParseError;
        }

        for (fi = 0u; fi < theme->frameCount; fi++) {
            uint8_t resCount;
            uint8_t ri;

            if (off + 1u > bodyLen) {
                return RoboParseError;
            }
            resCount = body[off++];
            if (resCount == 0u || resCount > RoboDesktopCursorPackMaxResolutions) {
                return RoboParseError;
            }
            if (fi == 0u) {
                theme->resolutionCount = resCount;
            } else if (resCount != theme->resolutionCount) {
                return RoboParseError;
            }
            for (ri = 0u; ri < resCount; ri++) {
                uint16_t px;
                uint32_t pngLen;
                CursorPngSlot *slot;

                if (off + 2u + 4u > bodyLen) {
                    return RoboParseError;
                }
                px = ReadU16Le(body + off);
                off += 2u;
                pngLen = ReadU32Le(body + off);
                off += 4u;
                if (off + pngLen > bodyLen ||
                    slotIndex >= (uint32_t)(RoboDesktopCursorPackMaxFrames *
                                            RoboDesktopCursorPackMaxResolutions)) {
                    return RoboParseError;
                }
                slot = &theme->slots[slotIndex++];
                slot->bytes = body + off;
                slot->length = pngLen;
                slot->pixelSize = px;
                slot->image = NULL;
                off += pngLen;
            }
        }
    }
    return RoboOk;
}

static const CursorEntry *FindByShape(const DesktopCursorPack *pack, InputCursorType shape)
{
    const char *want;
    uint32_t i;

    if (!pack || InputCursorTypeIsHidden(shape) || !InputCursorTypeIsKnown(shape)) {
        return NULL;
    }
    want = InputCursorTypeName(shape);
    for (i = 0u; i < pack->entryCount; i++) {
        if (strcmp(pack->entries[i].name, want) == 0) {
            return &pack->entries[i];
        }
    }
    return NULL;
}

static const CursorEntry *FindByName(const DesktopCursorPack *pack, const char *stateName)
{
    uint32_t i;

    if (!pack || !stateName || stateName[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < pack->entryCount; i++) {
        if (strcmp(pack->entries[i].name, stateName) == 0) {
            return &pack->entries[i];
        }
    }
    return NULL;
}

static const CursorThemeData *PickTheme(const CursorEntry *entry, DesktopCursorTheme theme)
{
    uint8_t want = theme == RoboDesktopCursorThemeDark ? 1u : 0u;
    uint8_t i;

    if (!entry || entry->themeCount == 0u) {
        return NULL;
    }
    for (i = 0u; i < entry->themeCount; i++) {
        /* Theme id was consumed but not stored; light is index 0, dark is 1 by packer order. */
        if (i == want) {
            return &entry->themes[i];
        }
    }
    return &entry->themes[0];
}

static uint32_t NearestSize(const CursorThemeData *theme, uint32_t want)
{
    uint32_t best = 0u;
    uint32_t bestDist = 0xffffffffu;
    uint8_t ri;

    if (!theme || theme->resolutionCount == 0u) {
        return 32u;
    }
    for (ri = 0u; ri < theme->resolutionCount; ri++) {
        uint32_t px = theme->slots[ri].pixelSize;
        uint32_t dist = px > want ? px - want : want - px;

        if (dist < bestDist) {
            bestDist = dist;
            best = px;
        }
    }
    return best == 0u ? theme->slots[0].pixelSize : best;
}

static Result ResolveTheme(const CursorThemeData *theme,
                           uint32_t wantPixels,
                           uint32_t animMs,
                           DesktopCursorFrame *out)
{
    uint32_t size;
    uint32_t frameIndex = 0u;
    uint32_t ri;
    CursorPngSlot *slot = NULL;
    Result status;

    if (!theme || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    size = NearestSize(theme, wantPixels == 0u ? 32u : wantPixels);
    if (theme->frameCount > 1u && theme->frameDurationMs > 0u) {
        frameIndex = (animMs / (uint32_t)theme->frameDurationMs) % (uint32_t)theme->frameCount;
    }
    for (ri = 0u; ri < theme->resolutionCount; ri++) {
        uint32_t index = frameIndex * (uint32_t)theme->resolutionCount + ri;

        if (index >= (uint32_t)(RoboDesktopCursorPackMaxFrames * RoboDesktopCursorPackMaxResolutions)) {
            break;
        }
        if (theme->slots[index].pixelSize == size) {
            slot = (CursorPngSlot *)&theme->slots[index];
            break;
        }
    }
    if (!slot) {
        slot = (CursorPngSlot *)&theme->slots[frameIndex * (uint32_t)theme->resolutionCount];
    }
    status = DecodeSlot(slot);
    if (status != RoboOk) {
        return status;
    }
    out->surface = GfxImageSurface(slot->image);
    out->pixelSize = slot->pixelSize;
    out->frameCount = theme->frameCount;
    out->frameDurationMs = theme->frameDurationMs;
    /* Hotspot authored at 32px; scale to the chosen bitmap. */
    out->hotspot.x = (int32_t)((int64_t)theme->hotspotX32 * (int64_t)slot->pixelSize / 32);
    out->hotspot.y = (int32_t)((int64_t)theme->hotspotY32 * (int64_t)slot->pixelSize / 32);
    return RoboOk;
}

Result DesktopCursorPackCreateFromRcur(const void *bytes, size_t length, DesktopCursorPack **out)
{
    DesktopCursorPack *pack = NULL;
    const uint8_t *p;
    size_t off;
    uint32_t metaLen;
    uint16_t cursorCount;
    uint16_t i;
    Result status;

    if (out) {
        *out = NULL;
    }
    if (!bytes || length < 16u || !out) {
        return RoboInvalidArgument;
    }
    p = (const uint8_t *)bytes;
    if (p[0] != 'R' || p[1] != 'C' || p[2] != 'U' || p[3] != 'R') {
        return RoboParseError;
    }
    if (p[4] != 1u) {
        return RoboNotSupported;
    }

    pack = (DesktopCursorPack *)RoboCalloc(1u, sizeof(DesktopCursorPack));
    if (!pack) {
        return RoboOutOfMemory;
    }
    pack->ownedBytes = (uint8_t *)Malloc(length);
    if (!pack->ownedBytes) {
        RoboFree(pack);
        return RoboOutOfMemory;
    }
    memcpy(pack->ownedBytes, bytes, length);
    pack->ownedLength = length;
    pack->logicalPt = 16u;
    p = pack->ownedBytes;

    off = 8u;
    metaLen = ReadU32Le(p + off);
    off += 4u;
    if (off + metaLen + 2u > length) {
        DesktopCursorPackDestroy(pack);
        return RoboParseError;
    }
    /* Optional: pull logicalPt from JSON if present. */
    {
        const char *meta = (const char *)(p + off);
        const char *found = strstr(meta, "\"logicalPt\":");

        if (found && (size_t)(found - meta) + 12u < metaLen) {
            unsigned long v = 0ul;
            const char *n = found + 12;

            while (*n == ' ') {
                n++;
            }
            while (*n >= '0' && *n <= '9') {
                v = v * 10ul + (unsigned long)(*n - '0');
                n++;
            }
            if (v > 0ul && v < 128ul) {
                pack->logicalPt = (uint32_t)v;
            }
        }
    }
    off += metaLen;
    cursorCount = ReadU16Le(p + off);
    off += 2u;
    if (cursorCount == 0u || cursorCount > RoboDesktopCursorPackMaxEntries) {
        DesktopCursorPackDestroy(pack);
        return RoboParseError;
    }

    for (i = 0u; i < cursorCount; i++) {
        uint8_t stateId;
        uint8_t nameLen;
        uint32_t byteOffset;
        uint32_t byteLength;
        CursorEntry *entry;

        if (off + 2u > length) {
            DesktopCursorPackDestroy(pack);
            return RoboParseError;
        }
        stateId = p[off++];
        nameLen = p[off++];
        if (nameLen == 0u || nameLen >= RoboDesktopCursorPackMaxName || off + nameLen + 8u > length) {
            DesktopCursorPackDestroy(pack);
            return RoboParseError;
        }
        entry = &pack->entries[pack->entryCount];
        entry->stateId = stateId;
        memcpy(entry->name, p + off, nameLen);
        entry->name[nameLen] = '\0';
        off += nameLen;
        byteOffset = ReadU32Le(p + off);
        off += 4u;
        byteLength = ReadU32Le(p + off);
        off += 4u;
        if (byteOffset >= length || byteLength == 0u ||
            (size_t)byteOffset + (size_t)byteLength > length) {
            DesktopCursorPackDestroy(pack);
            return RoboParseError;
        }
        status = ParseEntryBody(entry, p + byteOffset, byteLength);
        if (status != RoboOk) {
            DesktopCursorPackDestroy(pack);
            return status;
        }
        pack->entryCount++;
    }

    *out = pack;
    return RoboOk;
}

void DesktopCursorPackDestroy(DesktopCursorPack *pack)
{
    uint32_t i;

    if (!pack) {
        return;
    }
    for (i = 0u; i < pack->entryCount; i++) {
        CursorEntryClear(&pack->entries[i]);
    }
    if (pack->ownedBytes) {
        RoboFree(pack->ownedBytes);
        pack->ownedBytes = NULL;
    }
    RoboFree(pack);
}

int DesktopCursorPackIsLive(const DesktopCursorPack *pack)
{
    return pack && pack->entryCount > 0u ? 1 : 0;
}

uint32_t DesktopCursorPackEntryCount(const DesktopCursorPack *pack)
{
    return pack ? pack->entryCount : 0u;
}

uint32_t DesktopCursorPackLogicalPt(const DesktopCursorPack *pack)
{
    return pack && pack->logicalPt > 0u ? pack->logicalPt : 16u;
}

Result DesktopCursorPackResolve(const DesktopCursorPack *pack,
                                InputCursorType shape,
                                DesktopCursorTheme theme,
                                uint32_t wantPixels,
                                uint32_t animMs,
                                DesktopCursorFrame *out)
{
    const CursorEntry *entry;
    const CursorThemeData *td;

    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!pack || !out) {
        return RoboInvalidArgument;
    }
    if (InputCursorTypeIsHidden(shape)) {
        return RoboOk;
    }
    entry = FindByShape(pack, shape);
    if (!entry) {
        return RoboNotSupported;
    }
    td = PickTheme(entry, theme);
    if (!td) {
        return RoboNotSupported;
    }
    return ResolveTheme(td, wantPixels, animMs, out);
}

Result DesktopCursorPackResolveByName(const DesktopCursorPack *pack,
                                      const char *stateName,
                                      DesktopCursorTheme theme,
                                      uint32_t wantPixels,
                                      uint32_t animMs,
                                      DesktopCursorFrame *out)
{
    const CursorEntry *entry;
    const CursorThemeData *td;

    if (out) {
        memset(out, 0, sizeof(*out));
    }
    if (!pack || !out || !stateName) {
        return RoboInvalidArgument;
    }
    entry = FindByName(pack, stateName);
    if (!entry) {
        return RoboNotSupported;
    }
    td = PickTheme(entry, theme);
    if (!td) {
        return RoboNotSupported;
    }
    return ResolveTheme(td, wantPixels, animMs, out);
}

Result DesktopCursorPackBlit(GfxSurface *dst, GfxPoint pointer, const DesktopCursorFrame *frame)
{
    GfxPoint origin;

    if (!dst || !frame || !frame->surface) {
        return RoboOk;
    }
    origin.x = pointer.x - frame->hotspot.x;
    origin.y = pointer.y - frame->hotspot.y;
    return GfxSurfaceComposite(frame->surface, dst, origin, 255u);
}

GfxRect DesktopCursorPackBounds(GfxPoint pointer, const DesktopCursorFrame *frame, GfxRect screen)
{
    GfxRect raw;
    GfxRect clipped;

    if (!frame || !frame->surface || frame->pixelSize == 0u) {
        return GfxRectEmpty();
    }
    raw = MakeGfxRect(pointer.x - frame->hotspot.x, pointer.y - frame->hotspot.y,
                      (int32_t)frame->pixelSize, (int32_t)frame->pixelSize);
    clipped = GfxRectEmpty();
    if (GfxRectClip(raw, screen, &clipped) != RoboOk) {
        return GfxRectEmpty();
    }
    return clipped;
}
