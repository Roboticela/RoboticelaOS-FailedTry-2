/*
 * The surface pool, the byte budget and the resource policy — Phase 15.8, 15.16 and 15.7.
 *
 * ## Why a pool and not an allocator
 *
 * A compositor's surfaces are not a general-purpose allocation: they are a small set of
 * fixed-purpose buffers, each one acquired by name every frame. So this file is a **table of named
 * slots** with reference counts, and the two things that make that table safe are here rather than
 * in a caller's discipline:
 *
 *   **A release caches; it does not free.** The pixels are already paid for, and the next frame
 *   acquires the same name. Freeing on the last release would make every frame a fresh allocation,
 *   which is exactly the cost a compositor exists to avoid.
 *
 *   **The budget is checked on acquire, not counted down.** A compositor may hold anything it has
 *   already been given; the request that would *pass* the number is the one that fails, after the
 *   least-recently-released cached surfaces have been offered up. That ordering matters: refusing
 *   first and evicting second would refuse a request the pool could have satisfied.
 *
 * ## The three refusals are distinguished
 *
 * "The allocator said no" (`refusalsOom`), "the budget said no" (`refusalsBudget`) and "a GPU kind
 * was asked for by name" (`gpuRefusals`) are three counters rather than one, because the three mean
 * three different things to a caller: a leak, a limit set too low, and a feature this build does not
 * have. `RoboOutOfMemory` is the code for the first two, and that is the code every door in this
 * tree uses for "there is no room" — the counters are what tell the two apart.
 *
 * ## Replacing under a live name
 *
 * `CompositorSurfaceAcquire` with a different size or format does **not** fail and does not detach:
 * it creates the new storage, bumps the generation and drops the old pixels, so a window that
 * resized keeps its layers (which refer to the name, not the storage). That is the reason layers
 * hold an id rather than a pointer, and the reason `generation` exists at all: a dump can say
 * whether the pixels under a name changed.
 */

#include "RoboCompositorInternal.h"

/* ------------------------------------------------------------------ */
/* The pool                                                            */
/* ------------------------------------------------------------------ */

/** The cached slot whose pixels have gone unused longest, or NULL when there is none. */
static CompositorSurfaceSlot *OldestCached(Compositor *compositor)
{
    CompositorSurfaceSlot *best = NULL;
    unsigned i;

    for (i = 0u; i < compositor->surfaceSlots; i++) {
        CompositorSurfaceSlot *slot = &compositor->surfaces[i];
        if (!slot->held || slot->refs != 0u || !slot->surface) {
            continue;
        }
        if (!best || slot->lastReleaseFrame < best->lastReleaseFrame) {
            best = slot;
        }
    }
    return best;
}

/** Free a slot's pixels. The caller has already decided nobody wants them. */
static void FreeSlotPixels(Compositor *compositor, CompositorSurfaceSlot *slot)
{
    if (!slot->surface) {
        return;
    }
    GfxSurfaceRelease(slot->surface);
    slot->surface = NULL;
    if (slot->held) {
        if (slot->bytes <= compositor->bytesCached) {
            compositor->bytesCached -= slot->bytes;
        } else {
            compositor->bytesCached = 0u;
        }
        if (slot->bytes <= compositor->bytesLive) {
            compositor->bytesLive -= slot->bytes;
        } else {
            compositor->bytesLive = 0u;
        }
    }
    slot->held = 0;
    slot->refs = 0u;
    slot->layerRefs = 0u;
    slot->bytes = 0u;
    slot->placeholder = 0;
    compositor->surfaceCount--;
    compositor->statSurfacesDestroyed++;
}

/** Free cached pixels until at most @p keepBytes of them remain. Never touches a lived-in slot. */
static void TrimCached(Compositor *compositor, size_t keepBytes, size_t *outFreed)
{
    size_t freed = 0u;

    while (compositor->bytesCached > keepBytes) {
        CompositorSurfaceSlot *slot = OldestCached(compositor);
        size_t bytes;
        if (!slot) {
            break;
        }
        bytes = slot->bytes;
        FreeSlotPixels(compositor, slot);
        compositor->statEvictions++;
        freed += bytes;
    }
    if (outFreed) {
        *outFreed = freed;
    }
}

/** A free slot to take, or NULL when the pool is full and nothing may be evicted. */
static CompositorSurfaceSlot *TakeFreeSlot(Compositor *compositor)
{
    unsigned i;

    for (i = 0u; i < compositor->surfaceSlots; i++) {
        if (!compositor->surfaces[i].held) {
            return &compositor->surfaces[i];
        }
    }
    return NULL;
}

Result CompositorSurfaceAcquire(Compositor *compositor,
                                CompositorSurfaceId id,
                                GfxSize size,
                                GfxPixelFormat format,
                                GfxSurface **out,
                                CompositorDiag *diag)
{
    CompositorSurfaceSlot *slot;
    size_t bytes;
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    Result result;

    if (out) {
        *out = NULL;
    }
    if (!CompositorIsLive(compositor) || !out) {
        return RoboInvalidArgument;
    }
    if (id == RoboCompositorSurfaceNone) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainSurface,
                       "a surface needs a non-zero id: names are what a layer holds");
        return RoboInvalidArgument;
    }
    if (!GfxSizeIsValid(size) || GfxSizeIsEmpty(size)) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainSurface,
                       "a surface needs two positive dimensions");
        return RoboInvalidArgument;
    }
    if (format == RoboGfxFormatUnknown) {
        format = compositor->config.format;
    }
    bytes = CompositorSurfaceByteSize(size, format);
    if (bytes == 0u) {
        CompositorFail(compositor, diag, RoboNotSupported, RoboCompositorDomainSurface,
                       "that pixel format has no CPU layout in this build");
        return RoboNotSupported;
    }
    slot = CompositorFindSurface(compositor, id);
    if (slot && slot->surface && slot->size.width == size.width && slot->size.height == size.height &&
        slot->format == format) {
        slot->refs++;
        slot->placeholder = 0;
        compositor->statSurfaceAcquires++;
        *out = slot->surface;
        return RoboOk;
    }
    if (slot && slot->surface) {
        /* Same name, new pixels: the generation is what a dump reads to see this happened, and the
         * layers keep working because they hold the name rather than the storage. The reference
         * count survives the swap — the callers holding this name are still holding it. */
        unsigned refs = slot->refs;
        uint32_t generation = slot->generation;
        FreeSlotPixels(compositor, slot);
        slot->generation = generation + 1u;
        slot->refs = refs;
    }
    if (!slot) {
        slot = TakeFreeSlot(compositor);
    }
    if (!slot) {
        /* The pool bound is the first thing to give: cached pixels nobody is holding are exactly
         * what the bound is for. */
        TrimCached(compositor, 0u, NULL);
        slot = TakeFreeSlot(compositor);
    }
    if (!slot) {
        compositor->statRefusalsOom++;
        CompositorFail(compositor, diag, RoboOutOfMemory, RoboCompositorDomainSurface,
                       "every surface slot is held: raise maxSurfaces or release one");
        return RoboOutOfMemory;
    }
    if (compositor->bytesBudget && compositor->bytesLive + compositor->bytesCached + bytes >
                                       compositor->bytesBudget) {
        size_t room;

        TrimCached(compositor, 0u, NULL);
        room = (compositor->bytesLive < compositor->bytesBudget)
                   ? compositor->bytesBudget - compositor->bytesLive
                   : 0u;
        if (bytes > room) {
            compositor->statRefusalsBudget++;
            CompositorFail(compositor, diag, RoboOutOfMemory, RoboCompositorDomainMemory,
                           "the pixel budget cannot hold this surface");
            return RoboOutOfMemory;
        }
    }
    GfxSurfaceDescInit(&desc);
    desc.width = (uint32_t)size.width;
    desc.height = (uint32_t)size.height;
    desc.format = format;
    desc.strideBytes = 0u;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    result = GfxSurfaceCreate(&desc, &surface);
    if (result != RoboOk || !surface) {
        compositor->statRefusalsOom++;
        CompositorFail(compositor, diag, result == RoboOk ? RoboOutOfMemory : result,
                       RoboCompositorDomainSurface, "the pixels could not be allocated");
        return result == RoboOk ? RoboOutOfMemory : result;
    }
    slot->surface = surface;
    slot->id = id;
    slot->size = size;
    slot->format = format;
    slot->bytes = bytes;
    slot->held = 1;
    slot->placeholder = 0;
    slot->layerRefs = CompositorCountLayerRefs(compositor, id);
    slot->refs++;
    slot->lastReleaseFrame = (uint64_t)compositor->frameCount;
    compositor->surfaceCount++;
    compositor->bytesLive += bytes;
    if (compositor->bytesLive > compositor->bytesPeak) {
        compositor->bytesPeak = compositor->bytesLive;
    }
    compositor->statSurfacesCreated++;
    compositor->statSurfaceAcquires++;
    /* A new size means a tile index names different rectangles, so any grid over this name goes. */
    CompositorGridInvalidate(compositor, id);
    *out = surface;
    return RoboOk;
}

Result CompositorSurfaceRelease(Compositor *compositor, CompositorSurfaceId id, CompositorDiag *diag)
{
    CompositorSurfaceSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindSurface(compositor, id);
    if (!slot || !slot->surface) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainSurface,
                       "no such surface to release");
        return RoboNotFound;
    }
    if (slot->refs == 0u) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainSurface,
                       "that surface has no outstanding reference");
        return RoboNotFound;
    }
    slot->refs--;
    slot->lastReleaseFrame = (uint64_t)compositor->frameCount;
    compositor->statSurfaceReleases++;
    if (slot->refs == 0u) {
        /* Cached, not freed: the bytes move from "live" to "cached" and the budget keeps seeing them
         * until a trim, which is what makes the next acquire free. */
        if (slot->bytes <= compositor->bytesLive) {
            compositor->bytesLive -= slot->bytes;
        } else {
            compositor->bytesLive = 0u;
        }
        compositor->bytesCached += slot->bytes;
    }
    return RoboOk;
}

Result CompositorSurfaceGet(const Compositor *compositor, CompositorSurfaceId id, GfxSurface **out)
{
    const CompositorSurfaceSlot *slot;

    if (out) {
        *out = NULL;
    }
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindSurfaceConst(compositor, id);
    if (!slot || !slot->surface) {
        return RoboNotFound;
    }
    *out = slot->surface;
    return RoboOk;
}

int CompositorSurfaceExists(const Compositor *compositor, CompositorSurfaceId id)
{
    return CompositorSurfaceGet(compositor, id, NULL) == RoboOk ? 1 : 0;
}

unsigned CompositorSurfaceCount(const Compositor *compositor)
{
    unsigned i;
    unsigned count = 0u;

    if (!compositor) {
        return 0u;
    }
    for (i = 0u; i < compositor->surfaceSlots; i++) {
        if (compositor->surfaces[i].held && compositor->surfaces[i].surface) {
            count++;
        }
    }
    return count;
}

Result CompositorSurfaceDestroy(Compositor *compositor,
                                CompositorSurfaceId id,
                                size_t *outDetached,
                                CompositorDiag *diag)
{
    CompositorSurfaceSlot *slot;
    unsigned i;
    size_t detached = 0u;

    if (outDetached) {
        *outDetached = 0u;
    }
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindSurface(compositor, id);
    if (!slot) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainSurface,
                       "no such surface to destroy");
        return RoboNotFound;
    }
    /* Detaching first means the layers are gone from the tree's point of view before the pixels are:
     * a composite can never see a layer pointing at freed storage. */
    for (i = 0u; i < compositor->layerSlots; i++) {
        if (compositor->layers[i].used && compositor->layers[i].surface == id) {
            compositor->layers[i].surface = RoboCompositorSurfaceNone;
            detached++;
        }
    }
    FreeSlotPixels(compositor, slot);
    compositor->statLayersDetached += (uint64_t)detached;
    if (outDetached) {
        *outDetached = detached;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Stats, budget                                                       */
/* ------------------------------------------------------------------ */

void CompositorSurfaceStatsInit(CompositorSurfaceStats *stats)
{
    if (stats) {
        MemSet(stats, 0, sizeof(*stats));
        stats->bytesBudget = 0u;
    }
}

Result CompositorGetSurfaceStats(const Compositor *compositor, CompositorSurfaceStats *out)
{
    unsigned i;
    size_t live = 0u;
    size_t cached = 0u;

    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < compositor->surfaceSlots; i++) {
        const CompositorSurfaceSlot *slot = &compositor->surfaces[i];
        if (!slot->held || !slot->surface) {
            continue;
        }
        if (slot->refs > 0u) {
            live++;
        } else {
            cached++;
        }
    }
    CompositorSurfaceStatsInit(out);
    out->surfacesLive = live;
    out->surfacesCached = cached;
    out->bytesLive = compositor->bytesLive;
    out->bytesCached = compositor->bytesCached;
    out->bytesBudget = compositor->bytesBudget;
    out->bytesPeak = compositor->bytesPeak;
    out->surfacesCreated = compositor->statSurfacesCreated;
    out->surfacesDestroyed = compositor->statSurfacesDestroyed;
    out->acquires = compositor->statSurfaceAcquires;
    out->releases = compositor->statSurfaceReleases;
    out->evictions = compositor->statEvictions;
    out->refusalsOom = compositor->statRefusalsOom;
    out->refusalsBudget = compositor->statRefusalsBudget;
    out->gpuRefusals = compositor->statGpuRefusals;
    out->layersDetached = compositor->statLayersDetached;
    return RoboOk;
}

Result CompositorSetMemoryBudget(Compositor *compositor, size_t bytes, CompositorDiag *diag)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    compositor->bytesBudget = bytes;
    compositor->config.memoryBudgetBytes = bytes;
    if (bytes && compositor->bytesLive + compositor->bytesCached > bytes) {
        /* Shrinking below what is held is not a failure — the pixels are already given — but the
         * cached half is offered back immediately, which is the only way a smaller number can take
         * effect without waiting for a frame. */
        TrimCached(compositor, 0u, NULL);
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorTrim(Compositor *compositor, size_t keepBytes, size_t *outFreed)
{
    if (outFreed) {
        *outFreed = 0u;
    }
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    TrimCached(compositor, keepBytes, outFreed);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Resource policy (15.7)                                              */
/* ------------------------------------------------------------------ */

int CompositorResourceIsAvailable(CompositorResourceKind kind)
{
    return (kind == RoboCompositorResourceSurface || kind == RoboCompositorResourceTile) ? 1 : 0;
}

Result CompositorResourceCreate(Compositor *compositor,
                                CompositorResourceKind kind,
                                GfxSize size,
                                CompositorDiag *diag)
{
    GfxSurface *surface = NULL;
    CompositorSurfaceId id;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!CompositorResourceIsAvailable(kind)) {
        /* The GPU kinds exist in the vocabulary and are refused **by name**, with the refusal
         * counted: a caller that asked for a texture learns which of the two absences it hit. */
        compositor->statGpuRefusals++;
        CompositorFail(compositor, diag, RoboNotSupported, RoboCompositorDomainGpu,
                       "this build has no GPU: a texture or a buffer has nowhere to live");
        return RoboNotSupported;
    }
    id = compositor->nextResourceId + 1u;
    compositor->nextResourceId = id;
    return CompositorSurfaceAcquire(compositor, id, size, compositor->config.format, &surface, diag);
}

/* ------------------------------------------------------------------ */
/* Dump                                                                */
/* ------------------------------------------------------------------ */

Result CompositorWriteSurfaces(const Compositor *compositor,
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
    CompositorDumpPrintf(&dump, "surfaces %u live=%zu cached=%zu bytes=%zu/%zu peak=%zu\n",
                         CompositorSurfaceCount(compositor), compositor->bytesLive,
                         compositor->bytesCached, compositor->bytesLive + compositor->bytesCached,
                         compositor->bytesBudget, compositor->bytesPeak);
    for (i = 0u; i < compositor->surfaceSlots; i++) {
        const CompositorSurfaceSlot *slot = &compositor->surfaces[i];
        if (!slot->held || !slot->surface) {
            continue;
        }
        CompositorDumpPrintf(&dump, "surface id %llu generation %u %s refs %u format %d size %dx%d "
                                    "bytes %zu\n",
                             (unsigned long long)slot->id, slot->generation,
                             slot->refs > 0u ? "live" : "cached", slot->refs, (int)slot->format,
                             (int)slot->size.width, (int)slot->size.height, slot->bytes);
    }
    return CompositorDumpFinish(&dump, outNeeded);
}
