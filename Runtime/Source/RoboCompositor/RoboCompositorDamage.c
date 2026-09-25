/*
 * Damage — Phase 15.4, and the place a frame's cost is decided.
 *
 * ## One list, two spaces
 *
 * An entry is `(surface, rect)`: a name and a rectangle. `surface` **none means the composition**,
 * whose rectangles are in compositor space; a named surface's rectangle is in that surface's own
 * pixel space. Two spaces in one list is deliberate — the list answers one question, "what needs
 * doing this frame", and both answers are regions with a name.
 *
 * ## Never drop a region
 *
 * Dropping a region is a visibly stale pixel, so this file never does. Three things happen instead,
 * and the order matters:
 *
 *   1. **Intersecting regions of the same space are unioned on insert.** Two overlapping rectangles
 *      are one, which keeps the list short and makes the pixel count exact rather than
 *      double-counted — the count is a sum over entries, and it is only a true sum because entries
 *      in a space never overlap.
 *   2. **A full list merges.** The entry whose union with the new region grows the least is replaced
 *      by that union (`merges`), which is more work than strictly needed and never a missing pixel.
 *   3. **A full list with nothing to merge into becomes `full`** for a composition region (the whole
 *      composition is stale, which subsumes the region), and for a surface region it dirties every
 *      tile of that surface (which also subsumes it) — `overflows` counts both.
 *
 * So `CompositorDamageIsFull` is a real state rather than an error, and the two counters tell a
 * reader which of the three happened.
 *
 * ## The clip is the whole reason the counts are trustworthy
 *
 * Every rectangle is clipped to the space it names at insert time, so a region that does not exist
 * cannot be counted. A region that clips to nothing is a counted no-op success (`clippedAway`),
 * because a scroll near an edge legitimately damages nothing — and a caller that got `RoboOk`
 * should be able to find out that nothing happened.
 */

#include "RoboCompositorInternal.h"
#include "RoboCompositor/RoboCompositorTiles.h"

static GfxRect BoundingRect(GfxRect a, GfxRect b)
{
    int32_t x0 = a.x < b.x ? a.x : b.x;
    int32_t y0 = a.y < b.y ? a.y : b.y;
    int32_t x1 = a.x + a.width;
    int32_t y1 = a.y + a.height;
    int32_t bx1 = b.x + b.width;
    int32_t by1 = b.y + b.height;

    if (bx1 > x1) {
        x1 = bx1;
    }
    if (by1 > y1) {
        y1 = by1;
    }
    return MakeGfxRect(x0, y0, x1 - x0, y1 - y0);
}

static uint64_t RectArea(GfxRect rect)
{
    if (rect.width <= 0 || rect.height <= 0) {
        return 0u;
    }
    return (uint64_t)(uint32_t)rect.width * (uint64_t)(uint32_t)rect.height;
}

/** 1 when two rectangles share at least one pixel. */
static int RectsOverlap(GfxRect a, GfxRect b)
{
    return (a.x < b.x + b.width) && (b.x < a.x + a.width) && (a.y < b.y + b.height) &&
           (b.y < a.y + a.height);
}

/** The bounds a region is clipped to: the surface's own size, or the composition's. */
static int ClipToSpace(const Compositor *compositor, CompositorSurfaceId surface, GfxRect rect,
                       GfxRect *out)
{
    GfxRect clipped;

    if (surface == RoboCompositorSurfaceNone) {
        return CompositorClipToComposition(compositor, rect, out);
    }
    {
        const CompositorSurfaceSlot *slot = CompositorFindSurfaceConst(compositor, surface);
        if (!slot || !slot->surface) {
            return 0;
        }
        if (GfxRectIntersect(rect, CompositorSurfaceBounds(slot), &clipped) != RoboOk) {
            return 0;
        }
        if (GfxRectIsEmpty(clipped)) {
            return 0;
        }
        *out = clipped;
        return 1;
    }
}

/** 1 when @p rect covers the whole composition. */
static int CoversComposition(const Compositor *compositor, GfxRect rect)
{
    GfxRect composition = CompositorCompositionBounds(compositor);
    return rect.x <= composition.x && rect.y <= composition.y &&
           rect.x + rect.width >= composition.x + composition.width &&
           rect.y + rect.height >= composition.y + composition.height;
}

static void MarkFull(Compositor *compositor)
{
    compositor->damageFull = 1;
    compositor->damageCount = 0u;
    compositor->statDamageOverflows++;
}

Result CompositorDamageAdd(Compositor *compositor,
                           CompositorSurfaceId surface,
                           GfxRect rect,
                           CompositorDiag *diag)
{
    GfxRect clipped;
    unsigned i;
    int slot = -1;
    unsigned bestIndex = 0u;
    uint64_t bestGrowth = 0u;
    int haveBest = 0;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!ClipToSpace(compositor, surface, rect, &clipped)) {
        compositor->statDamageClippedAway++;
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }
    if (surface == RoboCompositorSurfaceNone && compositor->damageFull) {
        /* Already everything: a region inside it adds nothing. */
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }
    compositor->statDamageAdded++;
    compositor->statDamagePixels += RectArea(clipped);

    /* 1. Merge into an intersecting region of the same space. */
    for (i = 0u; i < compositor->damageCount; i++) {
        if (compositor->damage[i].surface != surface) {
            continue;
        }
        if (RectsOverlap(compositor->damage[i].rect, clipped)) {
            GfxRect grown = BoundingRect(compositor->damage[i].rect, clipped);
            if (surface == RoboCompositorSurfaceNone && CoversComposition(compositor, grown)) {
                MarkFull(compositor);
                if (diag) {
                    CompositorDiagReset(diag);
                }
                return RoboOk;
            }
            compositor->statDamageMerges++;
            compositor->damage[i].rect = grown;
            if (diag) {
                CompositorDiagReset(diag);
            }
            return RoboOk;
        }
    }

    /* 2. Room: append. */
    if (compositor->damageCount < compositor->damageSlots) {
        slot = (int)compositor->damageCount;
    }
    if (slot >= 0) {
        compositor->damage[slot].surface = surface;
        compositor->damage[slot].rect = clipped;
        compositor->damageCount++;
        if (surface == RoboCompositorSurfaceNone && CoversComposition(compositor, clipped)) {
            MarkFull(compositor);
        }
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }

    /* 3. Full: merge with the same-space region whose union grows least. */
    for (i = 0u; i < compositor->damageCount; i++) {
        uint64_t growth;
        GfxRect grown;
        if (compositor->damage[i].surface != surface) {
            continue;
        }
        grown = BoundingRect(compositor->damage[i].rect, clipped);
        growth = RectArea(grown) + 1u;
        if (!haveBest || growth < bestGrowth) {
            haveBest = 1;
            bestGrowth = growth;
            bestIndex = i;
        }
    }
    if (haveBest) {
        GfxRect grown = BoundingRect(compositor->damage[bestIndex].rect, clipped);
        compositor->statDamageMerges++;
        if (surface == RoboCompositorSurfaceNone && CoversComposition(compositor, grown)) {
            MarkFull(compositor);
        } else {
            compositor->damage[bestIndex].rect = grown;
        }
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }
    if (surface == RoboCompositorSurfaceNone) {
        MarkFull(compositor);
    } else {
        /* Nothing of this surface's to merge into and no room: dirtying every tile of it is a
         * superset of the region we could not hold, so no pixel goes stale. */
        const CompositorSurfaceSlot *surfaceSlot = CompositorFindSurfaceConst(compositor, surface);
        compositor->statDamageOverflows++;
        if (surfaceSlot) {
            CompositorTileMarkDirty(compositor, surface, CompositorSurfaceBounds(surfaceSlot), NULL);
        }
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorDamageAddAll(Compositor *compositor, CompositorDiag *diag)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    MarkFull(compositor);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

void CompositorDamageClear(Compositor *compositor)
{
    if (!compositor) {
        return;
    }
    compositor->damageCount = 0u;
    compositor->damageFull = 0;
}

unsigned CompositorDamageRectCount(const Compositor *compositor)
{
    return compositor ? compositor->damageCount : 0u;
}

Result CompositorDamageRect(const Compositor *compositor,
                            unsigned index,
                            CompositorSurfaceId *outSurface,
                            GfxRect *out)
{
    if (outSurface) {
        *outSurface = RoboCompositorSurfaceNone;
    }
    if (out) {
        *out = GfxRectEmpty();
    }
    if (!compositor) {
        return RoboInvalidArgument;
    }
    if (index >= compositor->damageCount) {
        return RoboNotFound;
    }
    if (outSurface) {
        *outSurface = compositor->damage[index].surface;
    }
    if (out) {
        *out = compositor->damage[index].rect;
    }
    return RoboOk;
}

uint64_t CompositorDamagePixelCount(const Compositor *compositor)
{
    uint64_t pixels = 0u;
    unsigned i;

    if (!compositor) {
        return 0u;
    }
    if (compositor->damageFull) {
        return RectArea(CompositorCompositionBounds(compositor));
    }
    /* Same-space entries never overlap (insert unions them), so a sum is the true count. */
    for (i = 0u; i < compositor->damageCount; i++) {
        pixels += RectArea(compositor->damage[i].rect);
    }
    return pixels;
}

int CompositorDamageIsFull(const Compositor *compositor)
{
    return compositor ? compositor->damageFull : 0;
}

int CompositorDamageContains(const Compositor *compositor, GfxPoint point)
{
    unsigned i;

    if (!compositor) {
        return 0;
    }
    if (compositor->damageFull) {
        return 1;
    }
    /* Composition regions only: a surface's rectangle is in that surface's own space, and a point in
     * compositor space cannot be tested against it without a transform this list does not hold. */
    for (i = 0u; i < compositor->damageCount; i++) {
        if (compositor->damage[i].surface != RoboCompositorSurfaceNone) {
            continue;
        }
        if (GfxRectContainsPoint(compositor->damage[i].rect, point)) {
            return 1;
        }
    }
    return 0;
}

Result CompositorInvalidateLayer(Compositor *compositor, CompositorLayerId id, CompositorDiag *diag)
{
    GfxRect bounds;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!CompositorFindLayerConst(compositor, id)) {
        return RoboNotFound;
    }
    bounds = CompositorLayerLocalBounds(compositor, id);
    if (GfxRectIsEmpty(bounds)) {
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }
    return CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, bounds, diag);
}

Result CompositorDamageAddAllSurfaces(Compositor *compositor, CompositorDiag *diag)
{
    unsigned i;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < compositor->surfaceSlots; i++) {
        const CompositorSurfaceSlot *slot = &compositor->surfaces[i];
        if (!slot->held || !slot->surface) {
            continue;
        }
        CompositorDamageAdd(compositor, slot->id, CompositorSurfaceBounds(slot), NULL);
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Stats and dump                                                      */
/* ------------------------------------------------------------------ */

void CompositorDamageStatsInit(CompositorDamageStats *stats)
{
    if (stats) {
        MemSet(stats, 0, sizeof(*stats));
    }
}

Result CompositorGetDamageStats(const Compositor *compositor, CompositorDamageStats *out)
{
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    CompositorDamageStatsInit(out);
    out->regionsAdded = compositor->statDamageAdded;
    out->merges = compositor->statDamageMerges;
    out->overflows = compositor->statDamageOverflows;
    out->pixelsInvalidated = compositor->statDamagePixels;
    out->clippedAway = compositor->statDamageClippedAway;
    out->regionsNow = compositor->damageCount;
    out->full = compositor->damageFull;
    return RoboOk;
}

void CompositorResetDamageStats(Compositor *compositor)
{
    if (!compositor) {
        return;
    }
    compositor->statDamageAdded = 0u;
    compositor->statDamageMerges = 0u;
    compositor->statDamageOverflows = 0u;
    compositor->statDamagePixels = 0u;
    compositor->statDamageClippedAway = 0u;
}

Result CompositorWriteDamage(const Compositor *compositor,
                             char *buffer,
                             size_t bufferSize,
                             size_t *outNeeded)
{
    CompositorDump dump;
    unsigned i;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump, "damage regions=%u full=%d pixels=%llu\n", compositor->damageCount,
                         compositor->damageFull, (unsigned long long)CompositorDamagePixelCount(compositor));
    for (i = 0u; i < compositor->damageCount; i++) {
        const CompositorDamageEntry *entry = &compositor->damage[i];
        CompositorDumpPrintf(&dump, "region %u surface %llu [%d,%d,%d,%d] pixels %llu\n", i,
                             (unsigned long long)entry->surface, (int)entry->rect.x,
                             (int)entry->rect.y, (int)entry->rect.width, (int)entry->rect.height,
                             (unsigned long long)RectArea(entry->rect));
    }
    return CompositorDumpFinish(&dump, outNeeded);
}
