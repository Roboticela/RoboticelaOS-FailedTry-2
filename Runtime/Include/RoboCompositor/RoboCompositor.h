/*
 * RoboCompositor — the module's front door, and Phase 15.1's architecture in one file.
 *
 * ## What a compositor is here
 *
 *     Compositor
 *     ├── surfaces   pixels somebody else rasterized, named by id, kept in a pool with a byte budget
 *     ├── layers     a tree: rectangle, offset, scale, opacity, clip, visibility, and a surface
 *     ├── damage     the regions that went stale, in composition space and per surface
 *     ├── tiles      a grid per damaged surface: what a frame must produce
 *     ├── jobs       the scheduler's queue, run against a caller-supplied raster callback
 *     ├── tracks     animations and smooth scrolls, as values over the caller's clock
 *     ├── posts      frame requests from anywhere, drained by the frame
 *     └── frames     the record of what each one cost, and the fence it completed
 *
 * ## The one schedule
 *
 *     post → advance time → step animations → damage → tiles → jobs → composite → record → fence
 *
 * `CompositorPumpFrame` is that schedule and `CompositorPresent` is its last three steps, so a
 * caller that wants to drive the parts itself can and a caller that wants one call can have it.
 * Nothing in the schedule allocates: every table is sized at construction from the configuration,
 * so a frame's cost is measurable and a test's failure mode is a number rather than a heap state.
 *
 * ## Fixed tables, chosen at construction
 *
 * `maxLayers`/`maxSurfaces`/`maxDamageRects`/`maxJobs`/`maxAnimations` size the module's tables, and
 * the compositor allocates them once. A shell therefore states its own limits rather than discovering
 * a growth curve at the wrong moment, and a refusal past a limit is `RoboOutOfMemory` with the domain
 * that refused — not a silent drop, which would be a frame that is wrong in a way nobody can see.
 *
 * ## Threading, stated rather than assumed
 *
 * One compositor is mutated by one thread. Device threads may **post** (`CompositorPostFrame`) and
 * that is the whole of their interface; everything else — layers, surfaces, tiles, jobs, the tree —
 * is the frame thread's. There is no lock in this module on purpose: a lock would make the
 * unsupported shape (two threads pumping) *seem* supported, and the supported shape needs none.
 */

#ifndef ROBO_COMPOSITOR_H
#define ROBO_COMPOSITOR_H

#include "RoboCompositor/RoboCompositorAnimation.h"
#include "RoboCompositor/RoboCompositorDamage.h"
#include "RoboCompositor/RoboCompositorDiagnostics.h"
#include "RoboCompositor/RoboCompositorFrame.h"
#include "RoboCompositor/RoboCompositorLayer.h"
#include "RoboCompositor/RoboCompositorSurface.h"
#include "RoboCompositor/RoboCompositorTiles.h"
#include "RoboCompositor/RoboCompositorTypes.h"
#include "RoboCompositor/RoboCompositorVersion.h"

#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Everything a compositor needs to exist. Zero-initialize, then ask for the defaults. */
typedef struct CompositorConfig {
    /** The composition's size. A present writes this region of the destination. */
    GfxSize size;
    /** The format new surfaces are created in. 0 (`RoboGfxPixelFormatUnknown`) means RGBA8. */
    GfxPixelFormat format;
    /** Table bounds. 0 means the default (`CompositorCapabilities`'s values). */
    unsigned maxLayers;
    unsigned maxSurfaces;
    unsigned maxDamageRects;
    unsigned maxJobs;
    unsigned maxAnimations;
    /** A tile's edge in pixels. 0 means the default. */
    unsigned tileEdge;
    /** Pixel budget in bytes; 0 means none. */
    size_t memoryBudgetBytes;
    /** Vsync interval in milliseconds; 0 means no vsync. */
    uint32_t vsyncIntervalMs;
    /** Start with reduced motion on, so a caller can honour a preference before its first frame. */
    int reducedMotion;
    /** 0 (the default) presents the whole composition; 1 presents only the damage. */
    int damagePresent;
} CompositorConfig;

/** This build's defaults: 1024x768, RGBA8, the capability table's bounds, no budget, no vsync. */
void CompositorConfigInit(CompositorConfig *config);

/**
 * Create a compositor.
 *
 * The configuration is copied, so the caller's record may be a temporary. A size with a zero or
 * negative dimension, or a format with no CPU layout, is `RoboInvalidArgument` /
 * `RoboNotSupported`; the tables failing to allocate is `RoboOutOfMemory` with @p diag filled.
 * @p out receives the compositor, which `CompositorDestroy` releases.
 */
Result CompositorCreate(const CompositorConfig *config,
                        Compositor **out,
                        CompositorDiag *diag);

/**
 * Destroy a compositor.
 *
 * Every surface it holds is released, every layer dropped, and the damage/tile/job tables freed —
 * one call, and nothing of the compositor survives it. Safe on NULL.
 */
void CompositorDestroy(Compositor *compositor);

/** The configuration in force (the copy the compositor owns). NULL when there is no compositor. */
const CompositorConfig *CompositorGetConfig(const Compositor *compositor);

/** The composition's size. */
GfxSize CompositorSize(const Compositor *compositor);

/**
 * Resize the composition.
 *
 * The new size is the present's bound from here on; the damage list becomes **full** (everything
 * that was on screen is stale, because the frame's geometry moved) and every grid's tiles are
 * invalidated, since a tile index means a different rectangle after a resize. Resizing to the size
 * already in force is a no-op success.
 */
Result CompositorResize(Compositor *compositor, GfxSize size, CompositorDiag *diag);

/** 1 when @p compositor is a live compositor, 0 for NULL. A cheap guard for a caller's assert. */
int CompositorIsLive(const Compositor *compositor);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_H */
