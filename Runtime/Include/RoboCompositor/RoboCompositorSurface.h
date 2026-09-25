/*
 * Surfaces, the pool they live in, and the budget they are spent from — Phase 15.8, 15.16 and 15.7.
 *
 * ## A surface is content, and it is the caller's to fill
 *
 * A compositor surface is a `GfxSurface` with a **name**: the compositor holds it, a layer refers to
 * it by id, and the pixels inside it belong to whoever rasterizes content. That is the seam: this
 * module decides *which* surface is on screen and *where*, and RoboRender/RoboPaint decide what the
 * bytes are. A surface is therefore acquired (created or retained), painted into by the caller, and
 * composited by `CompositorPresent`.
 *
 * ## Reference counts, caching and the budget
 *
 * `CompositorSurfaceAcquire` returns the surface and adds a reference; `CompositorSurfaceRelease`
 * drops one. A released surface is **cached**, not freed: the pixels are already paid for, and a
 * shell re-acquiring the same id next frame is the common case. The budget is what stops the cache
 * from being a leak — when a new allocation would pass `memoryBudgetBytes`, unreferenced surfaces
 * are evicted least-recently-released first, and if that is not enough the acquire **fails** with
 * `RoboOutOfMemory` rather than overshooting a number the caller set on purpose.
 *
 * Destroying a surface that a layer refers to is allowed and is not a dangling reference: the layer
 * is detached (its `surface` becomes none, it stops being painted) and the count of layers that went
 * with it is reported, because "a surface I destroyed took two layers off screen" is exactly the
 * fact a caller needs to see rather than infer.
 *
 * ## The GPU kinds are refused by name
 *
 * `CompositorResourceCreate` takes a `CompositorResourceKind`. The two CPU kinds acquire a surface
 * or a tile; `GpuTexture` and `GpuBuffer` answer `RoboNotSupported` from a door that exists and
 * counts the refusal, which is this build's answer to 15.7: the *policy* — who may allocate what,
 * what happens when it is refused, how it is counted — is implemented, and the allocator behind it
 * is the part that arrives with a GPU backend.
 */

#ifndef ROBO_COMPOSITOR_SURFACE_H
#define ROBO_COMPOSITOR_SURFACE_H

#include "RoboCompositor/RoboCompositorTypes.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque compositor. Defined in `RoboCompositorInternal.h`. */
typedef struct Compositor Compositor;

/** What the pool has done, as counters rather than as a feeling. */
typedef struct CompositorSurfaceStats {
    size_t surfacesLive;      /* acquired, still referenced by a caller or a layer */
    size_t surfacesCached;    /* released, pixels retained for the next acquire */
    size_t bytesLive;
    size_t bytesCached;
    size_t bytesBudget;       /* 0 = no budget */
    size_t bytesPeak;
    uint64_t surfacesCreated;
    uint64_t surfacesDestroyed;
    uint64_t acquires;
    uint64_t releases;
    uint64_t evictions;        /* cached surfaces freed to make room */
    uint64_t refusalsOom;      /* the allocator said no */
    uint64_t refusalsBudget;   /* the budget said no */
    uint64_t gpuRefusals;      /* a GPU kind was asked for by name */
    uint64_t layersDetached;   /* layers left without a surface by a destroy */
} CompositorSurfaceStats;

void CompositorSurfaceStatsInit(CompositorSurfaceStats *stats);

/**
 * The surface named @p id, created on first use.
 *
 * A size or format change under the same id **replaces** the surface: the old pixels are freed, the
 * generation is bumped, and every layer that referred to the id is re-pointed at the new storage
 * (which is what makes a resize a data change rather than a detach). Refusals: a size whose area is
 * zero or whose stride would overflow is `RoboInvalidArgument`; a format with no CPU path is
 * `RoboNotSupported`; neither the pool limit nor the budget can be satisfied is `RoboOutOfMemory`.
 * @p out receives a borrowed pointer that stays valid until the matching release — do not free it.
 */
Result CompositorSurfaceAcquire(Compositor *compositor,
                                CompositorSurfaceId id,
                                GfxSize size,
                                GfxPixelFormat format,
                                GfxSurface **out,
                                CompositorDiag *diag);

/**
 * Drop one reference. The last release **caches** the pixels; `CompositorTrim` frees them.
 * An id that does not exist, or a release with no outstanding reference, is `RoboNotFound`.
 */
Result CompositorSurfaceRelease(Compositor *compositor,
                                CompositorSurfaceId id,
                                CompositorDiag *diag);

/**
 * The surface named @p id **without** changing its reference count, for a caller that already holds
 * one and for a layer that is about to be pointed at it. Unknown id, or a cached surface that has
 * been trimmed, is `RoboNotFound`.
 */
Result CompositorSurfaceGet(const Compositor *compositor,
                            CompositorSurfaceId id,
                            GfxSurface **out);

/** 1 when @p id names a surface this compositor holds (live or cached). */
int CompositorSurfaceExists(const Compositor *compositor, CompositorSurfaceId id);

/** How many surfaces the pool holds, live and cached. */
unsigned CompositorSurfaceCount(const Compositor *compositor);

/**
 * Free the pixels of @p id and detach every layer that referred to it.
 *
 * Unlike a release this is final: the id may be acquired again later, which creates a *new* surface
 * (a new generation) rather than resurrecting the old one. @p outDetached, when non-NULL, receives
 * the number of layers that lost their surface.
 */
Result CompositorSurfaceDestroy(Compositor *compositor,
                                CompositorSurfaceId id,
                                size_t *outDetached,
                                CompositorDiag *diag);

/** The current surface statistics. NULL @p compositor or @p out -> `RoboInvalidArgument`. */
Result CompositorGetSurfaceStats(const Compositor *compositor, CompositorSurfaceStats *out);

/**
 * Set the pixel budget, in bytes. 0 removes the budget (the default).
 *
 * The budget is checked on acquire, not counted down: a compositor may hold anything it has already
 * been given, and the next request that would pass the number is what fails.
 */
Result CompositorSetMemoryBudget(Compositor *compositor, size_t bytes, CompositorDiag *diag);

/**
 * Free cached surfaces until at most @p keepBytes of cached pixels remain.
 *
 * Least-recently-released first, and never a surface somebody still holds a reference to. This is
 * the door a shell calls when the system is under pressure; it is also what an acquire calls
 * automatically when the budget would be passed. @p outFreed, when non-NULL, receives the bytes
 * returned.
 */
Result CompositorTrim(Compositor *compositor, size_t keepBytes, size_t *outFreed);

/**
 * Allocate a resource of @p kind.
 *
 * `RoboCompositorResourceSurface` and `…Tile` are this build's; `…GpuTexture` and `…GpuBuffer` are
 * refused with `RoboNotSupported` and counted, which is 15.7's honest half (see this file's
 * comment). A refused request leaves the compositor unchanged.
 */
Result CompositorResourceCreate(Compositor *compositor,
                                CompositorResourceKind kind,
                                GfxSize size,
                                CompositorDiag *diag);

/** 1 when @p kind is a resource this build can allocate. */
int CompositorResourceIsAvailable(CompositorResourceKind kind);

/** The bytes a surface of @p size and @p format occupies, or 0 when there is no CPU layout for it. */
size_t CompositorSurfaceByteSize(GfxSize size, GfxPixelFormat format);

/** One line per surface: `id generation state refs format size bytes held`. Complete or absent. */
Result CompositorWriteSurfaces(const Compositor *compositor,
                               char *buffer,
                               size_t bufferSize,
                               size_t *outNeeded);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_SURFACE_H */
