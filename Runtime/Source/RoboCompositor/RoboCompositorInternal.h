/*
 * The compositor's private state — one struct, its fixed tables, and the helpers the module's files
 * share.
 *
 * ## Why everything is a fixed table
 *
 * A frame is the one place in a UI where an allocation is a defect rather than an inconvenience: it
 * happens between an input and a present, it is where a shell's latency lives, and (Phase 15.17)
 * it is what a performance number is measured from. So every table here is allocated **once**, at
 * construction, from a bound the configuration states (`maxLayers`, `maxSurfaces`, `maxDamageRects`,
 * `maxJobs`, `maxAnimations`), and no frame-time door allocates anything. A compositor past a bound
 * refuses (`RoboOutOfMemory`) and counts the refusal; it never grows, and it never drops work
 * silently.
 *
 * ## Structure of the module's files
 *
 *   `RoboCompositor.c`           construction, configuration, capabilities, the clock and stats
 *   `RoboCompositorSurface.c`    the pool, the budget, the resource kinds
 *   `RoboCompositorLayer.c`      the tree, paint order, bounds, hit testing, scrolling
 *   `RoboCompositorDamage.c`     the region list, merging, queries
 *   `RoboCompositorTiles.c`      the grids, the dirty set, jobs and the raster seam
 *   `RoboCompositorAnimation.c`  the tracks, the easings, smooth scrolling
 *   `RoboCompositorFrame.c`      posts, vsync, present, frames, fences, the pump
 *   `RoboCompositorCompose.c`    the composite pass
 *   `RoboCompositorDump.c`       every diagnostic writer, and the fuzz door
 *
 * A file that needs a table declaration reads this header; a file that needs a *door* reads the
 * public header, so a private function never becomes an accidental API.
 */

#ifndef ROBO_COMPOSITOR_INTERNAL_H
#define ROBO_COMPOSITOR_INTERNAL_H

#include "RoboCompositor/RoboCompositor.h"

#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboMem/RoboMem.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The value `magic` holds while a compositor is live; checked by `CompositorIsLive`. */
#define RoboCompositorMagic ((uint32_t)0x434F4D50u) /* 'COMP' */

/**
 * How many layers `CompositorLayerHitTest` will walk at once.
 *
 * A fixed cap rather than an allocation, like every other table in this module: a hit test is on the
 * path between an input event and a frame, so it may not allocate. A tree deeper than this answers
 * the topmost of the layers it saw, which is the honest thing to do with a bound a caller chose.
 */
#define RoboCompositorHitOrderCapacity 256u

/**
 * A layer's place in the composition, resolved once — see `RoboCompositorLayer.c`.
 *
 * `boxX`/`boxY` are the layer's rectangle origin in compositor space (every ancestor's origin and
 * scale applied, this layer's own offset **not**), `scale` includes this layer's own, `parentScale`
 * is the ancestors' alone (which is what an offset is multiplied by), and the clip pair is the
 * intersection of every clip from the root down.
 */
typedef struct CompositorLayerGeometry {
    float boxX;
    float boxY;
    float parentScale;
    float scale;
    GfxRect clip;
    int hasClip;
} CompositorLayerGeometry;

/* ---- Tables -------------------------------------------------------------------------------- */

/** One surface in the pool: the pixels, who holds them, and when they were last touched. */
typedef struct CompositorSurfaceSlot {
    GfxSurface *surface;
    CompositorSurfaceId id;
    uint32_t generation;      /* bumped when an id's storage is replaced */
    unsigned refs;            /* acquires minus releases */
    unsigned layerRefs;       /* layers pointing at it, so a destroy can report the detach */
    uint64_t lastReleaseFrame;/* the LRU key for eviction */
    size_t bytes;
    GfxSize size;
    GfxPixelFormat format;
    int held;                 /* the slot has pixels */
    int placeholder;          /* reserved by an acquire that has not created pixels yet */
} CompositorSurfaceSlot;

/** One layer: its own properties, and the two links that make the tree. */
typedef struct CompositorLayerSlot {
    CompositorLayerId id;
    CompositorLayerId parent;
    CompositorSurfaceId surface;
    GfxRect rect;
    float offsetX;
    float offsetY;
    float scale;
    uint8_t opacity;
    GfxRect clip;
    int hasClip;
    int visible;
    CompositorLayerId firstChild;
    CompositorLayerId nextSibling; /* within the parent's child list, or the root list */
    int used;
} CompositorLayerSlot;

/** One damaged region: a surface (none = the composition) and a rectangle. */
typedef struct CompositorDamageEntry {
    CompositorSurfaceId surface;
    GfxRect rect;
} CompositorDamageEntry;

/** One surface's tile grid. Created lazily, on the surface's first damage. */
typedef struct CompositorTileGrid {
    CompositorSurfaceId surface;
    unsigned columns;
    unsigned rows;
    uint8_t *states; /* `CompositorTileState` per tile, row-major */
    int used;
} CompositorTileGrid;

/** One queued raster job. */
typedef struct CompositorJob {
    CompositorSurfaceId surface;
    GfxRect rect;
    CompositorJobKind kind;
} CompositorJob;

/** One animation track. */
typedef struct CompositorAnimationSlot {
    CompositorAnimationId id;
    CompositorLayerId layer;
    CompositorProperty property;
    float from;
    float to;
    CompositorTime startMs;
    uint32_t durationMs;
    CompositorEasing easing;
    int scroll;
    int active;
} CompositorAnimationSlot;

/** A present's own accounting, accumulated by the compose pass and read by the frame door. */
typedef struct CompositorComposeResult {
    unsigned layersPainted;
    unsigned layersCulled;   /* outside the present region, or invisible */
    unsigned layersClipped;  /* drawn through a clip */
    uint64_t pixelsWritten;
    unsigned regions;        /* present regions the pass walked */
} CompositorComposeResult;

/* ---- The compositor ------------------------------------------------------------------------ */

struct Compositor {
    uint32_t magic;
    CompositorConfig config;

    /* surfaces */
    CompositorSurfaceSlot *surfaces;
    unsigned surfaceCount;
    unsigned surfaceSlots;
    /** The next id `CompositorResourceCreate` hands out, so a policy allocation has a name. */
    CompositorSurfaceId nextResourceId;
    size_t bytesLive;
    size_t bytesCached;
    size_t bytesPeak;
    size_t bytesBudget;

    /* layers */
    CompositorLayerSlot *layers;
    unsigned layerCount;
    unsigned layerSlots;
    CompositorLayerId firstRoot;

    /* damage */
    CompositorDamageEntry *damage;
    unsigned damageCount;
    unsigned damageSlots;
    int damageFull;

    /* tiles */
    CompositorTileGrid *grids;
    unsigned gridCount;
    unsigned gridSlots;

    /* jobs: a ring, so a deferred job survives the frame boundary at no cost */
    CompositorJob *jobs;
    unsigned jobSlots;
    unsigned jobHead;
    unsigned jobCount;
    CompositorRasterFn rasterFn;
    void *rasterUser;

    /* animations */
    CompositorAnimationSlot *animations;
    unsigned animationSlots;
    CompositorAnimationId nextAnimationId;

    /* frames */
    CompositorFrameIndex frameCount;
    CompositorFrame lastFrame;
    int hasLastFrame;
    CompositorFenceId fence;
    int pending;
    CompositorFrameReason pendingReason;
    /** Job/tile totals as of the last present, so a frame's record can say what *it* ran. */
    uint64_t presentedJobsBaseline;
    uint64_t presentedTilesBaseline;

    /* clock */
    CompositorTime now;
    uint32_t vsyncInterval;
    CompositorTime nextVsync;
    int vsyncArmed;
    /**
     * Set when the clock passed a deadline, cleared by the frame that answers it.
     *
     * `nextVsync` always points at a deadline *in the future* after `CompositorAdvanceTime`, so a
     * due-ness test against it alone would answer "not yet" to a frame that is already late. This
     * flag is the other half: a deadline passed, so a frame is owed. It is the difference between
     * "don't paint faster than the panel" and "never paint again".
     */
    int vsyncDue;

    /* counters (15.17) */
    uint64_t statFrames;
    uint64_t statFramesCoalesced;
    uint64_t statFramesLate;
    uint64_t statPresents;
    uint64_t statPresentsFull;
    uint64_t statPresentsDamage;
    uint64_t statDamageAdded;
    uint64_t statDamagePixels;
    uint64_t statPixelsComposited;
    uint64_t statBytesPresented;
    uint64_t statCompositedPeak;
    uint64_t statJobsScheduled;
    uint64_t statJobsRun;
    uint64_t statJobsDeferred;
    uint64_t statLayersPainted;
    uint64_t statPosts;
    uint64_t statPostsDrained;
    uint64_t statPostsCoalesced;
    uint64_t statVsyncTicks;
    uint64_t statFencesWaited;
    uint64_t statRefusals;
    uint64_t statDamageMerges;
    uint64_t statDamageOverflows;
    uint64_t statDamageClippedAway;
    uint64_t statRefusalsByDomain[RoboCompositorDomainCount];

    /* tiles */
    uint64_t statRasters;
    uint64_t statRasterRefusals;
    uint64_t statRasterPixels;

    /* animations */
    uint64_t statAnimationsStarted;
    uint64_t statAnimationsCompleted;
    uint64_t statAnimationsCancelled;
    uint64_t statAnimationsReplaced;
    uint64_t statAnimationsSnapped;
    uint64_t statAnimationSteps;
    uint64_t statSmoothScrolls;
    int reducedMotion;

    /* surfaces */
    uint64_t statSurfacesCreated;
    uint64_t statSurfacesDestroyed;
    uint64_t statSurfaceAcquires;
    uint64_t statSurfaceReleases;
    uint64_t statEvictions;
    uint64_t statRefusalsOom;
    uint64_t statRefusalsBudget;
    uint64_t statGpuRefusals;
    uint64_t statLayersDetached;

    /* the last refusal, so a NULL diag still leaves an answer behind */
    CompositorDiag lastDiag;
    int hasLastDiag;
};

/* ---- Shared helpers ------------------------------------------------------------------------ */

/**
 * Fill @p diag and record the refusal.
 *
 * Every door that can fail goes through this, which is why the counters and the last-diag record are
 * complete: a failure path that forgot to record itself would be invisible in a dump, and a dump is
 * what a person reads when a frame stops happening.
 */
void CompositorFailAt(Compositor *compositor,
                      CompositorDiag *diag,
                      Result code,
                      CompositorDiagnosticDomain domain,
                      const char *message,
                      const char *file,
                      int line);

#define CompositorFail(compositor, diag, code, domain, message) \
    CompositorFailAt((compositor), (diag), (code), (domain), (message), __FILE__, __LINE__)

/** Record a refusal (and the last diag) without a `Result` return, for a counted no-op. */
void CompositorNoteRefusal(Compositor *compositor,
                           CompositorDiagnosticDomain domain,
                           const char *message);

/** The surface slot named @p id, or NULL. */
CompositorSurfaceSlot *CompositorFindSurface(Compositor *compositor, CompositorSurfaceId id);
const CompositorSurfaceSlot *CompositorFindSurfaceConst(const Compositor *compositor,
                                                        CompositorSurfaceId id);

/** The layer slot named @p id, or NULL. */
CompositorLayerSlot *CompositorFindLayer(Compositor *compositor, CompositorLayerId id);
const CompositorLayerSlot *CompositorFindLayerConst(const Compositor *compositor,
                                                    CompositorLayerId id);

/** The tile grid over @p surface; @p create makes one when it is missing. */
CompositorTileGrid *CompositorFindGrid(Compositor *compositor,
                                       CompositorSurfaceId surface,
                                       int create);

/** @p slot's bounds: (0, 0, w, h). */
GfxRect CompositorSurfaceBounds(const CompositorSurfaceSlot *slot);

/** The composition's bounds: (0, 0, size). */
GfxRect CompositorCompositionBounds(const Compositor *compositor);

/**
 * Clip @p rect to the composition. Returns 0 when the result is empty (which is a no-op, not a
 * failure) and writes the clipped rectangle to @p out.
 */
int CompositorClipToComposition(const Compositor *compositor, GfxRect rect, GfxRect *out);

/** `round(f)`, refusing a value outside the int32 range by saturating rather than wrapping. */
int32_t CompositorRound(float value);

/** Non-zero when @p value is a usable scale or offset (finite, and not absurdly large). */
int CompositorValueIsFinite(float value);

/** The `Result` a door should report for a `diag` whose code is set; `RoboOk` when unset. */
Result CompositorDiagCode(const CompositorDiag *diag);

/** Mark every tile of @p surface's grid empty, dropping the grid if it is not worth keeping. */
void CompositorGridInvalidate(Compositor *compositor, CompositorSurfaceId surface);

/** Free a grid's states and zero the slot. */
void CompositorGridRelease(CompositorTileGrid *grid);

/** Number of layers pointing at @p surface. */
unsigned CompositorCountLayerRefs(const Compositor *compositor, CompositorSurfaceId surface);

/** Fill @p out with @p id's resolved geometry. 0 when @p id is not in the tree. */
int CompositorResolveLayer(const Compositor *compositor,
                           CompositorLayerId id,
                           CompositorLayerGeometry *out);

/**
 * @p id's destination rectangle in compositor space: the box origin plus the layer's own offset
 * (scaled by its ancestors), and the rectangle's size scaled by every scale on the path.
 * Empty for an unknown id.
 */
GfxRect CompositorLayerLocalBounds(const Compositor *compositor, CompositorLayerId id);

/** The effective opacity of @p layer's ancestor chain, including its own (255 for a root). */
uint8_t CompositorLayerEffectiveOpacity(const Compositor *compositor, CompositorLayerId id);

/**
 * The composite pass: draw @p layer and its subtree into @p target.
 *
 * @p originX/@p originY are the layer's box origin on screen (before its own offset), @p parentOpacity
 * and the clip pair are what the chain above it contributes, and @p region (when non-NULL) is the
 * only rectangle worth writing — the pass's own damage filter.
 *
 * See `RoboCompositorCompose.c` for the two rules this implements (offset moves content, not the
 * box; clip and opacity multiply down the chain), which are the whole of 15.2/15.3.
 */
/**
 * Draw the whole tree into @p target, optionally limited to @p region.
 *
 * The recursion is private to `RoboCompositorCompose.c`: what a caller needs is one door, and the
 * per-layer step takes eight arguments it has no business assembling.
 */
Result CompositorComposeTree(Compositor *compositor,
                             GfxSurface *target,
                             const GfxRect *region,
                             CompositorComposeResult *result,
                             CompositorDiag *diag);

/* ---- The dump writer ----------------------------------------------------------------------- */

/**
 * A "complete or absent" text writer.
 *
 * `DumpPrintf` accumulates the length the text needs and appends only while it fits; a writer that
 * overflows its buffer stops appending, and `DumpFinish` empties the buffer and reports
 * `RoboNotSupported` with the length the whole block wanted. A NULL buffer is the "how long is it"
 * query and always answers `RoboOk`.
 */
typedef struct CompositorDump {
    char *buffer;
    size_t size;
    size_t needed;
    int overflow;
} CompositorDump;

void CompositorDumpInit(CompositorDump *dump, char *buffer, size_t size);
void CompositorDumpPrintf(CompositorDump *dump, const char *format, ...);
Result CompositorDumpFinish(CompositorDump *dump, size_t *outNeeded);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_INTERNAL_H */
