/*
 * RoboCompositor's version pin and capability table — Phase 15 in one record.
 *
 * ## The rule this table follows
 *
 * Every other module in this tree states its capabilities the same way, and the reason is in
 * `RoboInput`'s comment: **an untested flag is a flag that stops being true.** So this table is two
 * blocks, and the second one is not an apology — it is the list of things a consumer would otherwise
 * assume. `gpuResources`, `compositorThread` and `doubleBuffering` are `0` because this build has no
 * GPU, its pump runs on the caller's thread, and a present writes the destination rather than
 * swapping two of them. A flag whose whole point is to say "not here" is worth more than a flag
 * nobody reads.
 *
 * ## What landed
 *
 * All eighteen goals of the phase, at the level this tree can honour: a **software** compositor.
 *
 *   15.1  the module, the `Compositor`, and the composition schedule
 *   15.2  a layer tree: id, parent, surface, rectangle, offset, scale, opacity, clip, visibility
 *   15.3  the composite pass: paint order, inherited origin/opacity/clip, source-over
 *   15.4  damage: a region list with merging, a full-surface fallback and a pixel count
 *   15.5  tiles: a grid per surface, three states, and a dirty set derived from damage
 *   15.6  raster scheduling: jobs, a per-frame budget, and a raster callback the caller installs
 *   15.7  resource kinds, one of which is this build's: GPU requests are refused **by name**
 *   15.8  a surface pool: acquire, release, retain, destroy, and layers detached on destroy
 *   15.9  frames: the request, the record, and the reasons a frame happens
 *   15.10 vsync: an interval on the caller's clock, ticks, alignment, and late frames counted
 *   15.11 scrolling: an offset applied at composite time, with the damage it implies
 *   15.12 smooth scrolling: the same offset driven by an eased track over frames
 *   15.13 animations: five properties, three easings, and reduced motion that snaps
 *   15.14 the frame pump and its post queue — the thread model, with ownership documented
 *   15.15 present fences: what "the destination has these pixels" means on a CPU compositor
 *   15.16 a memory budget: bytes counted, cached surfaces evicted by least-recent use
 *   15.17 counters: frames, damaged pixels, composited pixels, jobs, bytes, and a baseline
 *   15.18 visual conformance: exact pixels, two-generation determinism, and a PNG to look at
 *
 * ## What does not
 *
 *   **GPU.** `gpuResources`/`gpuSync` are 0 and `CompositorResourceCreate` refuses the two GPU kinds
 *   with `RoboNotSupported` rather than pretending to allocate a texture. The day a GPU backend
 *   exists, that door is where it starts and this table's first block is where it is announced.
 *
 *   **A compositor thread.** 15.14's model is a **pump**, not a thread: `CompositorPumpFrame` runs
 *   one frame at a time on the caller's thread, and the ownership rule the model exists for —
 *   device threads *post*, the frame runs on one thread — is enforced by a post queue with a
 *   documented drain (posts, drained, coalesced). `compositorThread` is 0 because this build has no
 *   second frame thread, and a flag claiming one would be false.
 *
 *   **Double buffering.** There is no swap chain: a present writes the destination the caller
 *   handed it. `doubleBuffering` is 0, and 15.15's fence therefore reports the honest thing — a
 *   completed present *is* the destination's pixels — which the conformance suite proves by reading
 *   them.
 *
 *   **Rotation, and sampling off the pixel grid.** A layer has a translate, a uniform scale and an
 *   axis-aligned clip. A scaled layer is sampled **nearest neighbour** (`subpixelSampling` 0), and
 *   `rotation` is 0: a quarter turn is a rasterizer's business (12.20's `GfxGlyphRasterParams`
 *   already has one), not a compositor's resample.
 *
 *   **Colour management.** No ICC, no transfer-function conversion at composite time
 *   (`colorManagement` 0): the blend is the source-over RoboGraphics states, on stored values.
 *
 *   **Presenting to a display.** The destination is a `GfxSurface` the caller owns
 *   (`presentToSurface` 1, `presentToDisplay` 0). Handing the composition to a framebuffer or a
 *   display is `RenderPresentFramebuffer`/RoboDisplay's door, above this module.
 */

#ifndef ROBO_COMPOSITOR_VERSION_H
#define ROBO_COMPOSITOR_VERSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboCompositorVersionMajor 1
#define RoboCompositorVersionMinor 0
#define RoboCompositorVersionPatch 0

/** `"1.0.0"` — the phase's own narrative is in this file's comment, not in the string. */
const char *CompositorVersionString(void);

/**
 * What this build can do, in two blocks.
 *
 * The first block is what a caller may rely on; the second is what it may *not*, stated so a
 * consumer can refuse to build a path on it rather than discovering the absence at runtime. The
 * numeric fields are the bounds every fixed-size table in the module is allocated from, so a caller
 * can size its own bookkeeping — and, more usefully, a test reads them instead of hard-coding a
 * limit that a later phase might move.
 */
typedef struct CompositorCapabilities {
    /* ---- Present ---- */
    int layerTree;            /* 15.2 a tree, not a flat list */
    int damageTracking;       /* 15.4 */
    int tiles;                /* 15.5 */
    int rasterScheduling;     /* 15.6 jobs with a per-frame budget */
    int surfacePool;          /* 15.8 */
    int frameScheduling;      /* 15.9 */
    int vsync;                /* 15.10 an interval on the caller's clock */
    int scrolling;            /* 15.11 */
    int smoothScrolling;      /* 15.12 */
    int animations;           /* 15.13 */
    int reducedMotion;        /* 15.13 a track that snaps instead of animating */
    int framePump;            /* 15.14 one frame per call, on the caller's thread */
    int cpuFences;            /* 15.15 present completion */
    int memoryBudget;         /* 15.16 */
    int performanceCounters;  /* 15.17 */
    int diagnostics;          /* a dump per view, complete or absent */
    int visualConformance;    /* 15.18 exact pixels and two-generation determinism */
    int presentToSurface;     /* the destination is the caller's surface */
    int presentRegions;       /* a present may write only the damage */
    int layerClips;           /* an axis-aligned clip, inherited */

    /* ---- Deliberately absent (see this file's comment) ---- */
    int gpuResources;
    int gpuSync;
    int doubleBuffering;
    int compositorThread;
    int rotation;
    int subpixelSampling;
    int colorManagement;
    int presentToDisplay;
    int layerBlur;
    int toneMapping;

    /* ---- Bounds and geometry ---- */
    unsigned maxLayers;       /* layers a compositor may hold */
    unsigned maxSurfaces;     /* surfaces, live or cached */
    unsigned maxDamageRects;  /* regions before the list merges or falls back to full */
    unsigned maxJobs;         /* scheduled raster jobs per frame */
    unsigned maxAnimations;   /* active tracks */
    unsigned tileEdge;        /* a tile's edge in pixels */
    unsigned propertyCount;   /* `RoboCompositorPropertyCount` */
    unsigned domainCount;     /* `RoboCompositorDomainCount` */
    /** 1 when an overfull damage list becomes a full-surface present rather than dropping regions. */
    int damageOverflowToFull;
} CompositorCapabilities;

/** Fill @p capabilities from this build. NULL -> `RoboInvalidArgument`. */
Result CompositorGetCapabilities(CompositorCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_VERSION_H */
