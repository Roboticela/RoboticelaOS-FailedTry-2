/*
 * Tiles and raster scheduling — Phase 15.5 and 15.6.
 *
 * ## Why tiles at all
 *
 * Damage answers *which pixels* are stale; a tile grid answers *what unit of work* is produced. The
 * two are not the same: damage is a hint and jobs are a budget. A grid also gives the scheduler a
 * fixed unit it can count, cut off mid-frame and resume next frame without losing the region it
 * deferred — which is exactly what a per-frame budget (15.6) has to be able to do, and what a bare
 * rectangle list cannot express.
 *
 * A grid is created **lazily, per surface**, the first time that surface is damaged: a compositor
 * with one stale surface does not pay for a grid over every surface in the pool. A grid's tiles are
 * `tileEdge` square, with the right and bottom edges clipped to the surface, and each tile is
 * `Empty` (never produced), `Clean` (produced and current) or `Dirty` (stale).
 *
 * ## The scheduler, and the seam it keeps
 *
 * `CompositorScheduleFrame` turns the damage list into jobs and is where 15.6's policy lives: one
 * job per dirty tile of every damaged surface, capped at the caller's `budget`. Tile order is
 * row-major within a surface, smallest first, so a frame's work is deterministic and a test can say
 * *which* tiles were produced and which were deferred.
 *
 * `CompositorRunScheduled` executes them. It does not rasterize anything itself — it calls the
 * `CompositorRasterFn` the caller installed, one job at a time, and marks the tiles clean only when
 * the callback reports success. **This is the module's most important boundary**: scheduling is
 * RoboCompositor's, pixels are RoboRender's, and a compositor that called RoboRender directly would
 * be a compositor that cannot be tested without it.
 *
 * A callback that fails stops the run: the job stays scheduled, its tile stays dirty, and the count
 * of refused jobs is reported rather than swallowed — the next frame tries again, which is what a
 * buffer that was busy is supposed to mean.
 */

#ifndef ROBO_COMPOSITOR_TILES_H
#define ROBO_COMPOSITOR_TILES_H

#include "RoboCompositor/RoboCompositorDamage.h"
#include "RoboCompositor/RoboCompositorSurface.h"
#include "RoboCompositor/RoboCompositorTypes.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What every grid in the compositor adds up to. */
typedef struct CompositorTileStats {
    size_t grids;            /* surfaces with a grid */
    unsigned tileEdge;
    size_t total;            /* tiles across every grid */
    size_t empty;
    size_t clean;
    size_t dirty;
    uint64_t rasters;        /* successful jobs */
    uint64_t rasterRefusals; /* jobs a callback refused */
    uint64_t rasterPixels;
} CompositorTileStats;

void CompositorTileStatsInit(CompositorTileStats *stats);

/**
 * The grid over @p surface, or a size with zero dimensions when it has none yet.
 *
 * A grid appears the first time damage names the surface; `CompositorTilesEnsure` is the door that
 * creates it deliberately (and is what a caller uses to prime one before a first frame).
 */
Result CompositorTilesEnsure(Compositor *compositor,
                             CompositorSurfaceId surface,
                             unsigned *outColumns,
                             unsigned *outRows,
                             CompositorDiag *diag);

/** @p surface's grid dimensions, whether or not a grid exists yet. */
Result CompositorTileGridSize(const Compositor *compositor,
                              CompositorSurfaceId surface,
                              unsigned *outColumns,
                              unsigned *outRows);

/** The rectangle of tile (@p column, @p row) on @p surface, clipped to the surface's edges. */
Result CompositorTileRect(const Compositor *compositor,
                          CompositorSurfaceId surface,
                          unsigned column,
                          unsigned row,
                          GfxRect *out);

/**
 * The state of one tile. A grid that does not exist yet is all `Empty`.
 *
 * Named `…StateOf` because `CompositorTileState` is the *enum*'s name and a C function and a typedef
 * share one identifier namespace: the door could not have kept the spelling.
 */
Result CompositorTileStateOf(const Compositor *compositor,
                             CompositorSurfaceId surface,
                             unsigned column,
                             unsigned row,
                             CompositorTileState *out);

/** Dirty tiles of @p surface, or across every grid when @p surface is none. */
size_t CompositorTileDirtyCount(const Compositor *compositor, CompositorSurfaceId surface);

/** Every tile of every grid. The next frame's scheduler finds nothing to do. */
Result CompositorTilesReset(Compositor *compositor);

/** Mark the tiles that intersect @p rect on @p surface dirty. */
Result CompositorTileMarkDirty(Compositor *compositor,
                               CompositorSurfaceId surface,
                               GfxRect rect,
                               CompositorDiag *diag);

/** Mark the tiles that intersect @p rect clean — what a successful raster job ends with. */
Result CompositorTileMarkClean(Compositor *compositor,
                               CompositorSurfaceId surface,
                               GfxRect rect);

/** Turn the current damage list into dirty tiles. Called by the scheduler; exposed for a test. */
Result CompositorTilesDirtyFromDamage(Compositor *compositor, CompositorDiag *diag);

Result CompositorGetTileStats(const Compositor *compositor, CompositorTileStats *out);
void CompositorResetTileStats(Compositor *compositor);

/* ---- Raster jobs (15.6) -------------------------------------------------------------------- */

/**
 * Rasterize @p region of @p surface, on behalf of the compositor.
 *
 * @p kind is the job's own classification (a whole damage region or one tile) so a callback can
 * choose a cheaper path for a tile. Return `RoboOk` when the region's pixels are current; any other
 * code leaves the job scheduled and its tiles dirty.
 */
typedef Result (*CompositorRasterFn)(void *user,
                                     CompositorSurfaceId surface,
                                     GfxRect region,
                                     CompositorJobKind kind,
                                     CompositorDiag *diag);

/** Install the raster callback. Passing NULL removes it, and scheduling then produces no jobs. */
Result CompositorSetRasterFn(Compositor *compositor, CompositorRasterFn fn, void *user);

/** What one scheduling pass and one run did. */
typedef struct CompositorScheduleStats {
    uint64_t scheduled;   /* jobs queued this pass */
    uint64_t executed;    /* jobs a callback completed */
    uint64_t deferred;    /* jobs the budget cut, left for the next frame */
    uint64_t refused;     /* jobs a callback refused */
    uint64_t pixels;      /* pixels the executed jobs covered */
    unsigned pending;     /* jobs queued and not yet run */
    unsigned budget;
} CompositorScheduleStats;

void CompositorScheduleStatsInit(CompositorScheduleStats *stats);

/**
 * Fill the job queue from the current damage and tiles, up to @p budget jobs.
 *
 * Also dirties tiles from the damage first, so one call is the whole scheduling decision. A budget
 * of 0 means "no limit this frame" and is what a test uses when it wants the whole set. Jobs already
 * queued from a previous frame are **not** dropped: the queue is drained by
 * `CompositorRunScheduled`, and this door appends behind them, which is what makes a deferred tile
 * survive the frame boundary.
 */
Result CompositorScheduleFrame(Compositor *compositor,
                               unsigned budget,
                               CompositorScheduleStats *out,
                               CompositorDiag *diag);

/**
 * Run every queued job through the raster callback.
 *
 * Marks each executed job's region clean on its surface's grid. With no callback installed this is a
 * no-op that reports the pending count rather than an error: a compositor with nothing to rasterize
 * is a supported configuration (a shell whose content never changes).
 */
Result CompositorRunScheduled(Compositor *compositor,
                              CompositorScheduleStats *out,
                              CompositorDiag *diag);

/** Jobs queued and not yet run. */
unsigned CompositorPendingJobCount(const Compositor *compositor);

/** Throw away the queue without running it, and report how many jobs went. */
unsigned CompositorClearJobs(Compositor *compositor);

/** One line per dirty tile: `surface s tile c,r [x,y,w,h] state`. Complete or absent. */
Result CompositorWriteTiles(const Compositor *compositor,
                            char *buffer,
                            size_t bufferSize,
                            size_t *outNeeded);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_TILES_H */
