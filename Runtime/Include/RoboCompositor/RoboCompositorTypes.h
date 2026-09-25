/*
 * RoboCompositor's value vocabulary — Phase 15's first decision.
 *
 * ## Where this module sits
 *
 *     RoboGraphics   pixels: a surface, a rectangle, a source-over blend
 *     RoboCompositor surfaces arranged in a tree, damage, tiles, frames, presentation
 *     RoboRender /   what a *surface's* pixels are: a display list walked into ink
 *     RoboPaint
 *
 * The compositor is the module that answers "what is on screen, in what order, where, and how much
 * of it has to be produced this frame". It does **not** answer "what does this drawing command
 * mean" — that is RoboPaint's — and it does not walk a display list: a layer's content arrives as a
 * `GfxSurface` somebody else rasterized. That is the seam the whole module is built around, and it
 * is why a `Compositor` links against RoboGraphics and nothing above it.
 *
 * ## Values, not objects
 *
 * Everything here is a value a caller can copy, compare and print: an id, an enum, a rectangle, a
 * diagnostic. Ids are `uint64_t` with **0 as "none"**, so a zero-initialized record is a valid
 * "nothing" rather than a handle to slot 0, and a stale id is refused by a lookup rather than
 * resolving to a recycled slot.
 *
 * ## The clock
 *
 * `CompositorTime` is **milliseconds on a monotonic clock the caller owns**, exactly as RoboInput's
 * event timestamps are: the compositor never reads a wall clock, so a test advances time by calling
 * `CompositorAdvanceTime` and frames, animations and vsync deadlines are all a function of that
 * number (Phase 15.10, 15.12, 15.13).
 */

#ifndef ROBO_COMPOSITOR_TYPES_H
#define ROBO_COMPOSITOR_TYPES_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Identity ----------------------------------------------------------------------------- */

/** A layer in the compositor's tree. 0 is none. */
typedef uint64_t CompositorLayerId;
/** A surface in the compositor's pool. 0 is none. */
typedef uint64_t CompositorSurfaceId;
/** A present fence. 0 is none. */
typedef uint64_t CompositorFenceId;
/** A frame's index, counted from 1. */
typedef uint32_t CompositorFrameIndex;
/** An animation's id, counted from 1. */
typedef uint32_t CompositorAnimationId;
/** A tile's index in a surface's grid, counted from 0. */
typedef uint32_t CompositorTileIndex;

#define RoboCompositorLayerNone ((CompositorLayerId)0u)
#define RoboCompositorSurfaceNone ((CompositorSurfaceId)0u)
#define RoboCompositorFenceNone ((CompositorFenceId)0u)
#define RoboCompositorFrameNone ((CompositorFrameIndex)0u)
#define RoboCompositorAnimationNone ((CompositorAnimationId)0u)

/** Milliseconds on the caller's monotonic clock. */
typedef int64_t CompositorTime;

/* ---- Domains, and what a failure says ----------------------------------------------------- */

/**
 * Which part of the module a diagnostic came from.
 *
 * One enumerator per goal the phase names, because the reader of a failed composite is always asking
 * "which of these eighteen things refused" and a single domain would answer "the compositor did".
 */
typedef enum CompositorDiagnosticDomain {
    RoboCompositorDomainCore = 0,  /* 15.1  construction, configuration, capabilities */
    RoboCompositorDomainLayer,     /* 15.2  the tree */
    RoboCompositorDomainCompose,   /* 15.3  the composite pass */
    RoboCompositorDomainDamage,    /* 15.4  the stale-region list */
    RoboCompositorDomainTiles,     /* 15.5  the grid */
    RoboCompositorDomainJobs,      /* 15.6  scheduling and the raster seam */
    RoboCompositorDomainGpu,       /* 15.7  the resources this build does not have */
    RoboCompositorDomainSurface,   /* 15.8  the pool */
    RoboCompositorDomainFrame,     /* 15.9  frame requests and records */
    RoboCompositorDomainVsync,     /* 15.10 the clock */
    RoboCompositorDomainScroll,    /* 15.11 an offset applied at composite time */
    RoboCompositorDomainAnimation, /* 15.12/15.13 the tracks, and the smooth-scroll curve */
    RoboCompositorDomainThread,    /* 15.14 the pump and its post queue */
    RoboCompositorDomainFence,     /* 15.15 present completion */
    RoboCompositorDomainMemory,    /* 15.16 the budget */
    RoboCompositorDomainDump,      /* 15.17/15.55 the diagnostics */
    RoboCompositorDomainCount
} CompositorDiagnosticDomain;

/** The name of @p domain, or "?" — the string a dump and a failure message both carry. */
const char *CompositorDomainName(CompositorDiagnosticDomain domain);

/**
 * The failure a door reports: a code, the domain it came from, and a **static** message.
 *
 * The message is a string literal rather than a formatted one deliberately: a diagnostic is written
 * on the path that is already failing, so it allocates nothing and its lifetime is the build's.
 */
typedef struct CompositorDiag {
    Result code;
    CompositorDiagnosticDomain domain;
    const char *message;
    const char *file;
    int line;
} CompositorDiag;

/** Clear @p diag to success. Safe on NULL. */
void CompositorDiagReset(CompositorDiag *diag);

/**
 * Fill @p diag. NULL @p diag is `RoboInvalidArgument`; @p code `RoboOk` clears, like reset.
 */
Result CompositorDiagSet(CompositorDiag *diag,
                         Result code,
                         CompositorDiagnosticDomain domain,
                         const char *message,
                         const char *file,
                         int line);

/* ---- The vocabulary ---------------------------------------------------------------------- */

/** What a tile's pixels are: never produced, produced and current, or stale. */
typedef enum CompositorTileState {
    RoboCompositorTileEmpty = 0,
    RoboCompositorTileClean = 1,
    RoboCompositorTileDirty = 2
} CompositorTileState;

const char *CompositorTileStateName(CompositorTileState state);

/** What a resource request is for. The two GPU kinds are refused in this build (15.7). */
typedef enum CompositorResourceKind {
    RoboCompositorResourceSurface = 0,
    RoboCompositorResourceTile = 1,
    RoboCompositorResourceGpuTexture = 2,
    RoboCompositorResourceGpuBuffer = 3,
    RoboCompositorResourceKindCount
} CompositorResourceKind;

const char *CompositorResourceKindName(CompositorResourceKind kind);

/** A property an animation can drive on a layer. */
typedef enum CompositorProperty {
    RoboCompositorPropertyOpacity = 0,
    RoboCompositorPropertyOffsetX = 1,
    RoboCompositorPropertyOffsetY = 2,
    RoboCompositorPropertyScale = 3,
    RoboCompositorPropertyScrollX = 4,
    RoboCompositorPropertyScrollY = 5,
    RoboCompositorPropertyCount
} CompositorProperty;

const char *CompositorPropertyName(CompositorProperty property);

/** A timing curve. Three, and the phase's own names for them (14.8's motion tokens). */
typedef enum CompositorEasing {
    RoboCompositorEasingLinear = 0,
    RoboCompositorEasingEaseOut = 1,
    RoboCompositorEasingEaseInOut = 2,
    RoboCompositorEasingCount
} CompositorEasing;

const char *CompositorEasingName(CompositorEasing easing);

/** Why a frame was asked for. Recorded on the frame, because "what caused this paint" is asked. */
typedef enum CompositorFrameReason {
    RoboCompositorFrameReasonDamage = 0,
    RoboCompositorFrameReasonLayerChange = 1,
    RoboCompositorFrameReasonAnimation = 2,
    RoboCompositorFrameReasonScroll = 3,
    RoboCompositorFrameReasonVsync = 4,
    RoboCompositorFrameReasonResize = 5,
    RoboCompositorFrameReasonProgrammatic = 6,
    RoboCompositorFrameReasonCount
} CompositorFrameReason;

const char *CompositorFrameReasonName(CompositorFrameReason reason);

/**
 * How much of the destination a present writes.
 *
 * `Full` is every pixel of the composition; `Damage` is only the regions the damage list holds,
 * which is the phase's 15.4 in one enumerator: a frame's cost is a function of what changed, and a
 * test can read that as a number (`compositedPixels`).
 */
typedef enum CompositorPresentKind {
    RoboCompositorPresentFull = 0,
    RoboCompositorPresentDamage = 1
} CompositorPresentKind;

const char *CompositorPresentKindName(CompositorPresentKind kind);

/** What a raster job covers: a damage region, or one tile of a grid (15.5/15.6). */
typedef enum CompositorJobKind {
    RoboCompositorJobRegion = 0,
    RoboCompositorJobTile = 1
} CompositorJobKind;

const char *CompositorJobKindName(CompositorJobKind kind);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_TYPES_H */
