/*
 * The pool, the budget, the tile grid, the raster scheduler, the animation tracks and the frame
 * pump — Phase 15.6 through 15.16.
 *
 * The two cases that carry the most weight are the scheduling ones, because they are the only place
 * the module's *model* is visible: `compositor.job.deferred` proves that a budget cut a frame's work
 * in half and that the other half survived the frame boundary, and `compositor.job.refusal` proves
 * that a callback saying "busy" leaves the tile dirty and the job queued rather than losing a pixel.
 * Both are read off the surface's own pixels, so neither can pass on a build that merely counted
 * calls.
 *
 * Every time-sensitive case drives `CompositorAdvanceTime`/`CompositorPumpFrame` with integers, which
 * is why nothing here sleeps and nothing here is flaky.
 *
 * The last case is 15.17's baseline: it records how long a frame costs and *asserts* the module's own
 * cost model — under region presents the pixels a frame writes are the damage and the layers over it,
 * exactly — so a build that started re-presenting whole frames would fail rather than merely slow down.
 */

#include "RoboCompositorTestSupport.h"

#include <stdio.h>
#include <time.h>

#define FixtureGreen GfxRgba8(0u, 255u, 0u, 255u)
#define FixtureBlack GfxRgba8(0u, 0u, 0u, 255u)

/* ------------------------------------------------------------------ */
/* 15.8/15.16 — the pool and the budget                                */
/* ------------------------------------------------------------------ */

static void TestSurfacePool(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *first = NULL;
    GfxSurface *again = NULL;
    GfxSurface *got = NULL;
    CompositorSurfaceStats stats;
    size_t detached = 0u;

    RoboCheckEqInt((int)CompositorSurfaceCount(compositor), 0);
    RoboCheck(CompositorSurfaceExists(compositor, 1u) == 0);
    RoboCheck(CompositorSurfaceGet(compositor, 1u, &got) == RoboNotFound);

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                       &first, NULL) == RoboOk);
    RoboCheck(first != NULL);
    /* The same name acquires the same storage: a frame that re-acquires pays nothing. */
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                       &again, NULL) == RoboOk);
    RoboCheck(first == again);
    RoboCheck(CompositorSurfaceExists(compositor, 1u) == 1);
    RoboCheck(CompositorSurfaceGet(compositor, 1u, &got) == RoboOk);
    RoboCheck(got == first);
    RoboCheckEqInt((int)CompositorSurfaceCount(compositor), 1);

    /* A layer pointing at the surface, so a destroy has something to detach. */
    RoboCheck(CompositorLayerCreate(compositor, 5u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 5u, 1u, NULL) == RoboOk);

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    RoboCheck(CompositorGetSurfaceStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.surfacesLive, 1);
    RoboCheckEqInt((int)stats.surfacesCached, 0);
    /* A second release has nothing to drop. */
    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    RoboCheck(CompositorGetSurfaceStats(compositor, &stats) == RoboOk);
    /* Released and cached, not freed: the pixels are already paid for. */
    RoboCheckEqInt((int)stats.surfacesCached, 1);
    RoboCheck(stats.bytesCached > 0u);
    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboNotFound);
    RoboCheck(CompositorSurfaceRelease(compositor, 9u, NULL) == RoboNotFound);

    /* A destroy is final, and reports the layers it took off screen. */
    RoboCheck(CompositorSurfaceDestroy(compositor, 1u, &detached, NULL) == RoboOk);
    RoboCheckEqInt((int)detached, 1);
    RoboCheck(CompositorSurfaceExists(compositor, 1u) == 0);
    RoboCheck(CompositorLayerGetInfo(compositor, 5u, NULL) == RoboInvalidArgument);
    {
        CompositorLayerInfo info;
        RoboCheck(CompositorLayerGetInfo(compositor, 5u, &info) == RoboOk);
        RoboCheck(info.surface == RoboCompositorSurfaceNone);
    }
    RoboCheck(CompositorGetSurfaceStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.layersDetached, 1);
    RoboCheck(CompositorValidate(compositor, NULL) == RoboOk);
    CompositorDestroy(compositor);
}

static void TestSurfaceReplace(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *surface = NULL;
    CompositorLayerInfo info;
    GfxSize size;

    RoboCheck(CompositorLayerCreate(compositor, 2u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 2u, 1u, NULL) == RoboOk);
    /* The same name with a new size replaces the storage rather than failing or detaching. */
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(16, 12), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk);
    size = GfxSurfaceSize(surface);
    RoboCheckEqInt((int)size.width, 16);
    RoboCheckEqInt((int)size.height, 12);
    RoboCheck(CompositorLayerGetInfo(compositor, 2u, &info) == RoboOk);
    RoboCheck(info.surface == 1u); /* the layer holds the name, not the storage */
    /* The generation moved, which is how a dump says the pixels under a name changed. */
    {
        char buffer[512];
        RoboCheck(CompositorWriteSurfaces(compositor, buffer, sizeof(buffer), NULL) == RoboOk);
        RoboCheck(strstr(buffer, "generation 1") != NULL);
    }
    /* A zero-sized surface is refused before any pixels are asked for. */
    RoboCheck(CompositorSurfaceAcquire(compositor, 3u, MakeGfxSize(0, 4), RoboGfxFormatRgba8, NULL,
                                       NULL) == RoboInvalidArgument);
    RoboCheck(CompositorSurfaceAcquire(compositor, 0u, MakeGfxSize(4, 4), RoboGfxFormatRgba8, NULL,
                                       NULL) == RoboInvalidArgument);
    RoboCheck(CompositorSurfaceAcquire(compositor, 4u, MakeGfxSize(4, 4), RoboGfxFormatRgba16, NULL,
                                       NULL) == RoboNotSupported);
    RoboCheck(CompositorValidate(compositor, NULL) == RoboOk);
    CompositorDestroy(compositor);
}

static void TestSurfaceResource(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorSurfaceStats stats;

    RoboCheck(CompositorResourceIsAvailable(RoboCompositorResourceSurface) == 1);
    RoboCheck(CompositorResourceIsAvailable(RoboCompositorResourceTile) == 1);
    RoboCheck(CompositorResourceIsAvailable(RoboCompositorResourceGpuTexture) == 0);
    RoboCheck(CompositorResourceIsAvailable(RoboCompositorResourceGpuBuffer) == 0);
    RoboCheck(CompositorResourceCreate(compositor, RoboCompositorResourceSurface, MakeGfxSize(8, 8),
                                       NULL) == RoboOk);
    /* The GPU kinds are refused by name and counted, which is 15.7's honest half. */
    RoboCheck(CompositorResourceCreate(compositor, RoboCompositorResourceGpuTexture,
                                       MakeGfxSize(8, 8), NULL) == RoboNotSupported);
    RoboCheck(CompositorResourceCreate(compositor, RoboCompositorResourceGpuBuffer,
                                       MakeGfxSize(8, 8), NULL) == RoboNotSupported);
    RoboCheck(CompositorGetSurfaceStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.gpuRefusals, 2);
    CompositorDestroy(compositor);
}

static void TestMemoryBudget(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorSurfaceStats stats;
    GfxSurface *surface = NULL;
    size_t freed = 0u;
    size_t i;

    RoboCheck(CompositorSetMemoryBudget(compositor, 4096u, NULL) == RoboOk);
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(16, 16), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk); /* 1024 bytes */
    /* Past the number the request fails, and it is the *budget* that said no. */
    RoboCheck(CompositorSurfaceAcquire(compositor, 2u, MakeGfxSize(64, 64), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOutOfMemory);
    RoboCheck(CompositorGetSurfaceStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.refusalsBudget, 1);
    RoboCheckEqInt((int)stats.refusalsOom, 0);

    /* Released pixels are cached, and a trim returns them without touching a lived-in surface. */
    for (i = 0u; i < 8u; i++) {
        CompositorSurfaceId id = (CompositorSurfaceId)(10u + i);
        RoboCheck(CompositorSurfaceAcquire(compositor, id, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                           &surface, NULL) == RoboOk);
        RoboCheck(CompositorSurfaceRelease(compositor, id, NULL) == RoboOk);
    }
    RoboCheck(CompositorGetSurfaceStats(compositor, &stats) == RoboOk);
    RoboCheck(stats.bytesCached >= 8u * 256u);
    RoboCheck(CompositorTrim(compositor, 0u, &freed) == RoboOk);
    RoboCheck(freed >= 8u * 256u);
    RoboCheck(CompositorGetSurfaceStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.bytesCached, 0);
    RoboCheck(stats.evictions >= 8u);
    RoboCheckEqInt((int)stats.surfacesLive, 1); /* the one caller still holds */

    /* Removing the budget lets the pool grow again. */
    RoboCheck(CompositorSetMemoryBudget(compositor, 0u, NULL) == RoboOk);
    RoboCheck(CompositorSurfaceAcquire(compositor, 2u, MakeGfxSize(64, 64), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.5/15.6 — tiles and the scheduler                                 */
/* ------------------------------------------------------------------ */

static void TestTileGrid(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *surface = NULL;
    unsigned columns = 0u;
    unsigned rows = 0u;
    GfxRect rect;
    CompositorTileState state;

    /* 100×60 with a 64-pixel edge is 2 columns and 1 row, so the second tile is clipped. */
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(100, 60), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk);
    RoboCheck(CompositorTileGridSize(compositor, 1u, &columns, &rows) == RoboOk);
    RoboCheckEqInt((int)columns, 2);
    RoboCheckEqInt((int)rows, 1);
    RoboCheck(CompositorTileGridSize(compositor, 9u, &columns, &rows) == RoboNotFound);

    /* No grid yet: every tile is Empty, which is a real answer rather than an error. */
    RoboCheck(CompositorTileStateOf(compositor, 1u, 0u, 0u, &state) == RoboOk);
    RoboCheckEqInt((int)state, (int)RoboCompositorTileEmpty);
    RoboCheck(CompositorTilesEnsure(compositor, 1u, &columns, &rows, NULL) == RoboOk);
    RoboCheckEqInt((int)columns, 2);
    RoboCheck(CompositorTileRect(compositor, 1u, 0u, 0u, &rect) == RoboOk);
    RoboCheckEqInt((int)rect.width, 64);
    RoboCheckEqInt((int)rect.height, 60);
    RoboCheck(CompositorTileRect(compositor, 1u, 1u, 0u, &rect) == RoboOk);
    RoboCheckEqInt((int)rect.x, 64);
    RoboCheckEqInt((int)rect.width, 36); /* clipped to the surface's edge, not 64 */
    RoboCheck(CompositorTileRect(compositor, 1u, 2u, 0u, &rect) == RoboNotFound);
    RoboCheck(CompositorTileStateOf(compositor, 1u, 2u, 0u, &state) == RoboNotFound);
    RoboCheck(CompositorTilesEnsure(compositor, 9u, NULL, NULL, NULL) == RoboNotFound);
    CompositorDestroy(compositor);
}

static void TestTileDirty(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *surface = NULL;
    CompositorTileState state;
    CompositorTileStats stats;

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(100, 60), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, RoboCompositorSurfaceNone), 0);
    /* A rectangle touching one tile dirties one tile; a wide one dirties both. */
    RoboCheck(CompositorTileMarkDirty(compositor, 1u, MakeGfxRect(0, 0, 10, 10), NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, 1u), 1);
    RoboCheck(CompositorTileStateOf(compositor, 1u, 0u, 0u, &state) == RoboOk);
    RoboCheckEqInt((int)state, (int)RoboCompositorTileDirty);
    RoboCheck(CompositorTileMarkDirty(compositor, 1u, MakeGfxRect(60, 0, 40, 60), NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, 1u), 2);
    RoboCheck(CompositorGetTileStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.grids, 1);
    RoboCheckEqInt((int)stats.total, 2);
    RoboCheckEqInt((int)stats.dirty, 2);
    RoboCheckEqInt((int)stats.tileEdge, 64);

    RoboCheck(CompositorTileMarkClean(compositor, 1u, MakeGfxRect(0, 0, 10, 10)) == RoboOk);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, 1u), 1);
    RoboCheck(CompositorTileStateOf(compositor, 1u, 0u, 0u, &state) == RoboOk);
    RoboCheckEqInt((int)state, (int)RoboCompositorTileClean);
    RoboCheck(CompositorTilesReset(compositor) == RoboOk);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, RoboCompositorSurfaceNone), 0);
    RoboCheck(CompositorTileMarkDirty(compositor, 9u, MakeGfxRect(0, 0, 4, 4), NULL) ==
              RoboNotFound);
    CompositorDestroy(compositor);
}

/** A compositor with one 100×60 surface and a layer over it, for the scheduling cases. */
static Compositor *SchedulingFixture(CompositorRasterFixture *fixture, CompositorSurfaceId id)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *surface = NULL;

    RoboCheck(CompositorSurfaceAcquire(compositor, id, MakeGfxSize(100, 60), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk);
    CompositorFillSurface(surface, FixtureBlack);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 64, 48), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, id, NULL) == RoboOk);
    fixture->compositor = compositor;
    fixture->surface = id;
    fixture->color = FixtureGreen;
    fixture->calls = 0u;
    fixture->refusalsAfter = 0u;
    fixture->lastJobsRun = 0u;
    return compositor;
}

static void TestJobSchedule(void)
{
    CompositorRasterFixture fixture;
    Compositor *compositor = SchedulingFixture(&fixture, 1u);
    CompositorScheduleStats stats;
    GfxSurface *surface = NULL;
    CompositorTileStats tiles;

    RoboCheck(CompositorSetRasterFn(compositor, CompositorRasterPaint, &fixture) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, 1u, MakeGfxRect(0, 0, 100, 60), NULL) == RoboOk);
    RoboCheck(CompositorScheduleFrame(compositor, 0u, &stats, NULL) == RoboOk);
    RoboCheckEqInt((int)stats.scheduled, 2);
    RoboCheckEqInt((int)stats.deferred, 0);
    RoboCheckEqInt((int)CompositorPendingJobCount(compositor), 2);
    RoboCheckEqInt((int)stats.pending, 2);

    RoboCheck(CompositorRunScheduled(compositor, &stats, NULL) == RoboOk);
    RoboCheckEqInt((int)stats.executed, 2);
    RoboCheckEqInt((int)stats.refused, 0);
    RoboCheckEqInt((int)CompositorPendingJobCount(compositor), 0);
    /* The jobs ran: the surface's own pixels say so. */
    RoboCheck(CompositorSurfaceGet(compositor, 1u, &surface) == RoboOk);
    RoboCheckEqInt((int)CompositorCountColor(surface, FixtureGreen), 100 * 60);
    RoboCheck(CompositorGetTileStats(compositor, &tiles) == RoboOk);
    RoboCheckEqInt((int)tiles.dirty, 0);
    RoboCheckEqInt((int)tiles.clean, 2);
    RoboCheckEqInt((int)tiles.rasters, 2);
    /* A produced tile is on screen only through a composite, so the layers that use it went stale. */
    RoboCheck(CompositorDamageRectCount(compositor) > 0u);
    CompositorDestroy(compositor);
}

static void TestJobDeferred(void)
{
    CompositorRasterFixture fixture;
    Compositor *compositor = SchedulingFixture(&fixture, 1u);
    CompositorScheduleStats first;
    CompositorScheduleStats second;
    GfxSurface *surface = NULL;

    RoboCheck(CompositorSetRasterFn(compositor, CompositorRasterPaint, &fixture) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, 1u, MakeGfxRect(0, 0, 100, 60), NULL) == RoboOk);
    /* A budget of one job: the frame does half the work and *says* which half it deferred. */
    RoboCheck(CompositorScheduleFrame(compositor, 1u, &first, NULL) == RoboOk);
    RoboCheckEqInt((int)first.scheduled, 1);
    RoboCheckEqInt((int)first.deferred, 1);
    RoboCheck(CompositorRunScheduled(compositor, &first, NULL) == RoboOk);
    RoboCheckEqInt((int)first.executed, 1);
    RoboCheckEqInt((int)CompositorPendingJobCount(compositor), 0);
    RoboCheck(CompositorSurfaceGet(compositor, 1u, &surface) == RoboOk);
    RoboCheckEqInt((int)CompositorCountColor(surface, FixtureGreen), 64 * 60);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, 1u), 1);

    /* The next frame finds the tile that was cut, and nothing is scheduled twice. */
    RoboCheck(CompositorScheduleFrame(compositor, 0u, &second, NULL) == RoboOk);
    RoboCheckEqInt((int)second.scheduled, 1);
    RoboCheck(CompositorRunScheduled(compositor, &second, NULL) == RoboOk);
    RoboCheckEqInt((int)second.executed, 1);
    RoboCheckEqInt((int)CompositorCountColor(surface, FixtureGreen), 100 * 60);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, 1u), 0);
    CompositorDestroy(compositor);
}

static void TestJobRefusal(void)
{
    CompositorRasterFixture fixture;
    Compositor *compositor = SchedulingFixture(&fixture, 1u);
    CompositorScheduleStats stats;
    GfxSurface *surface = NULL;

    fixture.refusalsAfter = 1u; /* the first job runs, the second is told "busy" */
    RoboCheck(CompositorSetRasterFn(compositor, CompositorRasterPaint, &fixture) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, 1u, MakeGfxRect(0, 0, 100, 60), NULL) == RoboOk);
    RoboCheck(CompositorScheduleFrame(compositor, 0u, &stats, NULL) == RoboOk);
    RoboCheck(CompositorRunScheduled(compositor, &stats, NULL) == RoboOk);
    RoboCheckEqInt((int)stats.executed, 1);
    RoboCheckEqInt((int)stats.refused, 1);
    /* A refusal is not a lost pixel: the job stays queued and its tile stays dirty. */
    RoboCheckEqInt((int)CompositorPendingJobCount(compositor), 1);
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, 1u), 1);
    RoboCheck(CompositorSurfaceGet(compositor, 1u, &surface) == RoboOk);
    RoboCheckEqInt((int)CompositorCountColor(surface, FixtureGreen), 64 * 60);

    /* The next attempt succeeds. */
    fixture.refusalsAfter = 0u;
    RoboCheck(CompositorRunScheduled(compositor, &stats, NULL) == RoboOk);
    RoboCheckEqInt((int)stats.executed, 1);
    RoboCheckEqInt((int)CompositorPendingJobCount(compositor), 0);
    RoboCheckEqInt((int)CompositorCountColor(surface, FixtureGreen), 100 * 60);
    CompositorDestroy(compositor);
}

static void TestJobNoCallback(void)
{
    CompositorRasterFixture fixture;
    Compositor *compositor = SchedulingFixture(&fixture, 1u);
    CompositorScheduleStats stats;

    /* No raster callback is a supported configuration: a shell whose content never changes. */
    RoboCheck(CompositorDamageAdd(compositor, 1u, MakeGfxRect(0, 0, 100, 60), NULL) == RoboOk);
    RoboCheck(CompositorScheduleFrame(compositor, 0u, &stats, NULL) == RoboOk);
    RoboCheckEqInt((int)stats.scheduled, 0);
    RoboCheckEqInt((int)CompositorPendingJobCount(compositor), 0);
    /* The damage still dirtied the tiles, so a callback installed later has work waiting. */
    RoboCheckEqInt((int)CompositorTileDirtyCount(compositor, 1u), 2);
    RoboCheck(CompositorSetRasterFn(compositor, CompositorRasterPaint, &fixture) == RoboOk);
    RoboCheck(CompositorScheduleFrame(compositor, 0u, &stats, NULL) == RoboOk);
    RoboCheckEqInt((int)stats.scheduled, 2);
    RoboCheckEqInt((int)CompositorClearJobs(compositor), 2);
    RoboCheckEqInt((int)CompositorPendingJobCount(compositor), 0);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.12/15.13 — animations and smooth scrolling                       */
/* ------------------------------------------------------------------ */

static void TestAnimationEasings(void)
{
    RoboCheck((CompositorEasingApply(RoboCompositorEasingLinear, 0.5f) == 0.5f) ? 1 : 0);
    RoboCheck((CompositorEasingApply(RoboCompositorEasingLinear, -1.0f) == 0.0f) ? 1 : 0);
    RoboCheck((CompositorEasingApply(RoboCompositorEasingLinear, 2.0f) == 1.0f) ? 1 : 0);
    RoboCheck((CompositorEasingApply(RoboCompositorEasingEaseOut, 0.0f) == 0.0f) ? 1 : 0);
    RoboCheck((CompositorEasingApply(RoboCompositorEasingEaseOut, 1.0f) == 1.0f) ? 1 : 0);
    RoboCheck((CompositorEasingApply(RoboCompositorEasingEaseInOut, 0.5f) == 0.5f) ? 1 : 0);
    /* Ease-out is ahead of linear in the middle: it decelerates. */
    RoboCheck(CompositorEasingApply(RoboCompositorEasingEaseOut, 0.5f) >
              CompositorEasingApply(RoboCompositorEasingLinear, 0.5f));
    RoboCheck(CompositorEasingApply(RoboCompositorEasingEaseInOut, 0.25f) <
              CompositorEasingApply(RoboCompositorEasingLinear, 0.25f));
}

static void TestAnimationTrack(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorAnimationId id = RoboCompositorAnimationNone;
    CompositorAnimationInfo info;
    CompositorAnimationStats stats;
    CompositorLayerInfo layer;
    unsigned applied = 0u;

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorActiveAnimationCount(compositor), 0);
    RoboCheck(CompositorAnimate(compositor, 1u, RoboCompositorPropertyOpacity, 255.0f, 0.0f, 0, 100u,
                                RoboCompositorEasingLinear, &id, NULL) == RoboOk);
    RoboCheck(id != RoboCompositorAnimationNone);
    RoboCheckEqInt((int)CompositorActiveAnimationCount(compositor), 1);
    RoboCheck(CompositorGetAnimationInfo(compositor, id, &info) == RoboOk);
    RoboCheckEqInt((int)info.property, (int)RoboCompositorPropertyOpacity);
    RoboCheck((info.from == 255.0f) ? 1 : 0);
    RoboCheck((info.to == 0.0f) ? 1 : 0);
    RoboCheckEqInt((int)info.durationMs, 100);
    RoboCheckEqInt(info.scroll, 0);
    RoboCheck(CompositorGetAnimationInfo(compositor, 999u, &info) == RoboNotFound);

    /* Halfway: the linear curve is exactly half, and the byte is rounded rather than truncated. */
    RoboCheck(CompositorAnimationStep(compositor, 50, &applied, NULL) == RoboOk);
    RoboCheckEqInt((int)applied, 1);
    RoboCheck(CompositorLayerGetInfo(compositor, 1u, &layer) == RoboOk);
    RoboCheckEqInt((int)layer.opacity, 128);
    /* The end lands exactly, and the track is finished. */
    RoboCheck(CompositorAnimationStep(compositor, 100, &applied, NULL) == RoboOk);
    RoboCheckEqInt((int)applied, 1);
    RoboCheck(CompositorLayerGetInfo(compositor, 1u, &layer) == RoboOk);
    RoboCheckEqInt((int)layer.opacity, 0);
    RoboCheckEqInt((int)CompositorActiveAnimationCount(compositor), 0);
    RoboCheck(CompositorAnimationStep(compositor, 150, &applied, NULL) == RoboOk);
    RoboCheckEqInt((int)applied, 0);
    RoboCheck(CompositorGetAnimationStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.started, 1);
    RoboCheckEqInt((int)stats.completed, 1);
    RoboCheckEqInt((int)stats.steps, 2);

    /* The refusals. */
    RoboCheck(CompositorAnimate(compositor, 9u, RoboCompositorPropertyOpacity, 0.0f, 1.0f, 0, 10u,
                                RoboCompositorEasingLinear, NULL, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorAnimate(compositor, 1u, (CompositorProperty)99, 0.0f, 1.0f, 200, 10u,
                                RoboCompositorEasingLinear, NULL, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorAnimationCancel(compositor, 999u, NULL) == RoboNotFound);
    CompositorDestroy(compositor);
}

static void TestAnimationReplaceAndCancel(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorAnimationId first = RoboCompositorAnimationNone;
    CompositorAnimationId second = RoboCompositorAnimationNone;
    CompositorAnimationStats stats;
    CompositorLayerInfo layer;
    unsigned applied = 0u;

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorAnimate(compositor, 1u, RoboCompositorPropertyOpacity, 255.0f, 0.0f, 0, 100u,
                                RoboCompositorEasingLinear, &first, NULL) == RoboOk);
    /* A second track on the same property continues rather than fighting the first. */
    RoboCheck(CompositorAnimate(compositor, 1u, RoboCompositorPropertyOpacity, 128.0f, 64.0f, 0, 100u,
                                RoboCompositorEasingLinear, &second, NULL) == RoboOk);
    RoboCheck(second != first);
    RoboCheckEqInt((int)CompositorActiveAnimationCount(compositor), 1);
    RoboCheck(CompositorGetAnimationInfo(compositor, first, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorGetAnimationStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.replaced, 1);
    RoboCheck(CompositorAnimationStep(compositor, 50, &applied, NULL) == RoboOk);
    RoboCheck(CompositorLayerGetInfo(compositor, 1u, &layer) == RoboOk);
    RoboCheckEqInt((int)layer.opacity, 96); /* 128 + (64-128)*0.5 */

    /* A cancel leaves the property where the last step left it: no end value is applied. */
    RoboCheck(CompositorAnimationCancel(compositor, second, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorActiveAnimationCount(compositor), 0);
    RoboCheck(CompositorAnimationStep(compositor, 200, &applied, NULL) == RoboOk);
    RoboCheckEqInt((int)applied, 0);
    RoboCheck(CompositorLayerGetInfo(compositor, 1u, &layer) == RoboOk);
    RoboCheckEqInt((int)layer.opacity, 96);
    RoboCheck(CompositorGetAnimationStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.cancelled, 1);
    /* A zero duration is a one-frame transition rather than a refusal. */
    RoboCheck(CompositorAnimate(compositor, 1u, RoboCompositorPropertyOpacity, 96.0f, 200.0f, 200,
                                0u, RoboCompositorEasingLinear, NULL, NULL) == RoboOk);
    RoboCheck(CompositorAnimationStep(compositor, 200, &applied, NULL) == RoboOk);
    RoboCheckEqInt((int)applied, 1);
    RoboCheck(CompositorLayerGetInfo(compositor, 1u, &layer) == RoboOk);
    RoboCheckEqInt((int)layer.opacity, 200);
    CompositorDestroy(compositor);
}

static void TestAnimationReducedMotion(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorAnimationId id = RoboCompositorAnimationNone;
    CompositorAnimationStats stats;
    CompositorLayerInfo layer;

    RoboCheckEqInt(CompositorReducedMotion(compositor), 0);
    RoboCheck(CompositorSetReducedMotion(compositor, 1, NULL) == RoboOk);
    RoboCheckEqInt(CompositorReducedMotion(compositor), 1);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    /* Reduced motion snaps to the end value: the state the caller asked for *arrives*, it simply
     * does not travel there. Dropping the track would leave the property where it started. */
    RoboCheck(CompositorAnimate(compositor, 1u, RoboCompositorPropertyOffsetY, 0.0f, 40.0f, 0, 250u,
                                RoboCompositorEasingEaseOut, &id, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorActiveAnimationCount(compositor), 0);
    RoboCheck(CompositorLayerGetInfo(compositor, 1u, &layer) == RoboOk);
    RoboCheck((layer.offsetY == 40.0f) ? 1 : 0);
    RoboCheck(CompositorGetAnimationStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.snapped, 1);
    RoboCheckEqInt((int)stats.completed, 1);
    RoboCheck(CompositorSetReducedMotion(compositor, 0, NULL) == RoboOk);
    RoboCheckEqInt(CompositorReducedMotion(compositor), 0);
    CompositorDestroy(compositor);
}

static void TestSmoothScroll(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorAnimationId scrollY = RoboCompositorAnimationNone;
    CompositorAnimationId scrollX = RoboCompositorAnimationNone;
    CompositorAnimationStats stats = {0};
    float x = 0.0f;
    float y = 0.0f;
    CompositorLayerInfo layer;
    unsigned applied = 0u;

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 20, 20), NULL) == RoboOk);
    CompositorDamageClear(compositor);

    RoboCheck(CompositorScrollAnimate(compositor, 1u, 0.0f, 40.0f, 80u, RoboCompositorEasingLinear,
                                      &scrollX, &scrollY, NULL) == RoboOk);
    RoboCheck(scrollX == RoboCompositorAnimationNone); /* no horizontal delta, no horizontal track */
    RoboCheck(scrollY != RoboCompositorAnimationNone);
    RoboCheck(CompositorGetAnimationStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.smoothScrolls, 1);
    {
        CompositorAnimationInfo info;
        RoboCheck(CompositorGetAnimationInfo(compositor, scrollY, &info) == RoboOk);
        RoboCheckEqInt(info.scroll, 1); /* a smooth scroll is a flagged track, not a second engine */
        RoboCheckEqInt((int)info.property, (int)RoboCompositorPropertyScrollY);
    }
    /* A scroll moves content across the layer's whole box, so the whole box is stale. */
    RoboCheck(CompositorDamageRectCount(compositor) >= 1u);
    RoboCheck(CompositorDamageContains(compositor, MakeGfxPoint(10, 10)) == 1);

    RoboCheck(CompositorAnimationStep(compositor, 40, &applied, NULL) == RoboOk);
    RoboCheckEqInt((int)applied, 1);
    RoboCheck(CompositorLayerGetScroll(compositor, 1u, &x, &y) == RoboOk);
    RoboCheck((y == 20.0f) ? 1 : 0);
    RoboCheck((x == 0.0f) ? 1 : 0);
    RoboCheck(CompositorAnimationStep(compositor, 80, &applied, NULL) == RoboOk);
    RoboCheck(CompositorLayerGetScroll(compositor, 1u, &x, &y) == RoboOk);
    RoboCheck((y == 40.0f) ? 1 : 0);
    RoboCheck(CompositorLayerGetInfo(compositor, 1u, &layer) == RoboOk);
    RoboCheck((layer.offsetY == 40.0f) ? 1 : 0);

    RoboCheck(CompositorScrollAnimate(compositor, 99u, 1.0f, 1.0f, 10u, RoboCompositorEasingLinear,
                                      NULL, NULL, NULL) == RoboNotFound);
    RoboCheck(CompositorLayerGetScroll(compositor, 99u, &x, &y) == RoboNotFound);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.9/15.10/15.14/15.15 — frames                                     */
/* ------------------------------------------------------------------ */

static void TestFramePosts(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorStats stats;

    RoboCheckEqInt(CompositorFramePending(compositor), 0);
    RoboCheckEqInt((int)CompositorPendingReason(compositor),
                   (int)RoboCompositorFrameReasonVsync);
    RoboCheck(CompositorPostFrame(compositor, RoboCompositorFrameReasonDamage, NULL) == RoboOk);
    RoboCheckEqInt(CompositorFramePending(compositor), 1);
    RoboCheckEqInt((int)CompositorPendingReason(compositor),
                   (int)RoboCompositorFrameReasonDamage);
    /* A second post before a pump is coalesced: it adds nothing a frame does not already do. */
    RoboCheck(CompositorPostFrame(compositor, RoboCompositorFrameReasonScroll, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorPendingReason(compositor),
                   (int)RoboCompositorFrameReasonDamage);
    RoboCheck(CompositorPostFrame(compositor, (CompositorFrameReason)99, NULL) ==
              RoboInvalidArgument);
    RoboCheckEqInt((int)CompositorDrainPosts(compositor), 1);
    RoboCheckEqInt((int)CompositorDrainPosts(compositor), 0);
    RoboCheck(CompositorGetStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.posts, 2);
    RoboCheckEqInt((int)stats.postsCoalesced, 1);
    RoboCheckEqInt((int)stats.postsDrained, 1);
    CompositorDestroy(compositor);
}

static void TestFramePump(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = CompositorMakeSurface(CompositorFixtureWidth, CompositorFixtureHeight);
    CompositorFrame frame;
    CompositorStats stats;

    CompositorFillSurface(target, FixtureBlack);
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u,
                                       MakeGfxSize(CompositorFixtureWidth, CompositorFixtureHeight),
                                       RoboGfxFormatRgba8, &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u,
                                     MakeGfxRect(0, 0, CompositorFixtureWidth,
                                                 CompositorFixtureHeight), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);

    /* A frame with damage and no post still draws: damage is what a frame is owed for. */
    RoboCheck(CompositorPumpFrame(compositor, target, 0, 0u, &frame, NULL) == RoboOk);
    RoboCheckEqInt((int)frame.index, 1);
    RoboCheckEqInt(frame.presented, 1);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureGreen),
                   CompositorFixtureWidth * CompositorFixtureHeight);

    /* An idle pump coalesces rather than presenting an identical screen. */
    {
        GfxSurface *second = CompositorMakeSurface(CompositorFixtureWidth, CompositorFixtureHeight);
        CompositorFillSurface(second, FixtureBlack);
        RoboCheck(CompositorPumpFrame(compositor, second, 0, 0u, NULL, NULL) == RoboBusy);
        RoboCheckEqInt((int)CompositorCountColor(second, FixtureGreen), 0);
        GfxSurfaceRelease(second);
    }
    /* A post makes one due, whatever the damage is. */
    RoboCheck(CompositorPostFrame(compositor, RoboCompositorFrameReasonProgrammatic, NULL) == RoboOk);
    RoboCheck(CompositorPumpFrame(compositor, target, 0, 0u, &frame, NULL) == RoboOk);
    RoboCheckEqInt((int)frame.reason, (int)RoboCompositorFrameReasonProgrammatic);
    RoboCheckEqInt((int)frame.index, 2);
    RoboCheckEqInt(CompositorFramePending(compositor), 0);

    RoboCheck(CompositorGetStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.frames, 2);
    RoboCheckEqInt((int)stats.framesCoalesced, 1);
    RoboCheckEqInt((int)stats.framesLate, 0);

    /* A pump with now -1 leaves the clock alone, which is what a caller with its own loop wants. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 0, 2, 2),
                                  NULL) == RoboOk);
    RoboCheck(CompositorPumpFrame(compositor, target, -1, 0u, &frame, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorTimeNow(compositor), 0);
    RoboCheck(CompositorPumpFrame(compositor, NULL, 0, 0u, NULL, NULL) == RoboInvalidArgument);

    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestFrameVsync(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = CompositorMakeSurface(CompositorFixtureWidth, CompositorFixtureHeight);
    CompositorFrame frame;
    CompositorStats stats;
    unsigned ticks = 0u;

    CompositorFillSurface(target, FixtureBlack);
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u,
                                       MakeGfxSize(CompositorFixtureWidth, CompositorFixtureHeight),
                                       RoboGfxFormatRgba8, &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u,
                                     MakeGfxRect(0, 0, CompositorFixtureWidth,
                                                 CompositorFixtureHeight), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorSetVsync(compositor, 16u) == 16u);

    RoboCheck(CompositorPumpFrame(compositor, target, 0, 0u, &frame, NULL) == RoboOk);
    /* A pump before the deadline coalesces: don't paint faster than the panel. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 0, 4, 4),
                                  NULL) == RoboOk);
    RoboCheck(CompositorPumpFrame(compositor, target, 8, 0u, NULL, NULL) == RoboBusy);
    /* On the deadline it draws, and says it was aligned. */
    RoboCheck(CompositorPumpFrame(compositor, target, 16, 0u, &frame, NULL) == RoboOk);
    RoboCheckEqInt(frame.vsyncAligned, 1);
    RoboCheckEqInt(frame.late, 0);
    /* Past the deadline it still draws — and says it was late rather than pretending otherwise. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 0, 4, 4),
                                  NULL) == RoboOk);
    RoboCheck(CompositorPumpFrame(compositor, target, 40, 0u, &frame, NULL) == RoboOk);
    RoboCheckEqInt(frame.late, 1);
    RoboCheckEqInt(frame.vsyncAligned, 0);

    RoboCheck(CompositorAdvanceTime(compositor, 96, &ticks, NULL) == RoboOk);
    RoboCheck(ticks > 0u);
    RoboCheck(CompositorGetStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.framesLate, 1);
    RoboCheckEqInt((int)stats.framesCoalesced, 1);
    RoboCheck(stats.vsyncTicks >= 2u);

    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestFrameFences(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *target = CompositorMakeSurface(CompositorFixtureWidth, CompositorFixtureHeight);
    CompositorFrame frame;
    CompositorStats stats;

    CompositorFillSurface(target, FixtureBlack);
    RoboCheck(CompositorPresentFence(compositor) == RoboCompositorFenceNone);
    RoboCheck(CompositorWaitFence(compositor, 1u, NULL) == RoboTimeout);
    RoboCheck(CompositorWaitFence(compositor, RoboCompositorFenceNone, NULL) == RoboNotFound);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, &frame, NULL) ==
              RoboOk);
    RoboCheck(CompositorPresentFence(compositor) == frame.fence);
    RoboCheck(CompositorWaitFence(compositor, frame.fence, NULL) == RoboOk);
    RoboCheck(CompositorWaitFence(compositor, frame.fence + 1u, NULL) == RoboTimeout);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, &frame, NULL) ==
              RoboOk);
    RoboCheck(CompositorWaitFence(compositor, frame.fence - 1u, NULL) == RoboOk);
    RoboCheck(CompositorGetStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.fencesWaited, 2);
    /* The fence is not a promise about the future: it names a present that already wrote pixels. */
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureBlack),
                   CompositorFixtureWidth * CompositorFixtureHeight);

    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestFrameWriters(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *target = CompositorMakeSurface(CompositorFixtureWidth, CompositorFixtureHeight);
    char buffer[4096];
    size_t needed = 0u;

    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);
    RoboCheck(CompositorWriteStats(compositor, NULL, 0u, &needed) == RoboOk);
    RoboCheck(needed > 40u);
    RoboCheck(CompositorWriteStats(compositor, buffer, sizeof(buffer), &needed) == RoboOk);
    RoboCheck(strstr(buffer, "frames=1") != NULL);
    RoboCheck(strstr(buffer, "presents=") != NULL);

    RoboCheck(CompositorWriteFrame(compositor, 1u, buffer, sizeof(buffer), &needed) == RoboOk);
    RoboCheck(strstr(buffer, "frame 1") != NULL);
    RoboCheck(strstr(buffer, "full") != NULL);
    RoboCheck(CompositorWriteFrame(compositor, 2u, buffer, sizeof(buffer), &needed) ==
              RoboNotFound);
    /* Complete or absent, like every other writer in the module. */
    buffer[0] = 'x';
    RoboCheck(CompositorWriteFrame(compositor, 1u, buffer, 4u, &needed) == RoboNotSupported);
    RoboCheckEqInt((int)buffer[0], 0);

    RoboCheck(CompositorWriteJobs(compositor, buffer, sizeof(buffer), &needed) == RoboOk);
    RoboCheck(strstr(buffer, "jobs pending=0") != NULL);
    RoboCheck(CompositorWriteAnimations(compositor, buffer, sizeof(buffer), &needed) == RoboOk);
    RoboCheck(strstr(buffer, "animations active=0") != NULL);
    RoboCheck(CompositorWriteTiles(compositor, buffer, sizeof(buffer), &needed) == RoboOk);
    RoboCheck(CompositorWriteLayers(compositor, buffer, sizeof(buffer), &needed) == RoboOk);
    RoboCheck(strstr(buffer, "layers 0") != NULL);
    RoboCheck(CompositorWriteHitTest(compositor, MakeGfxPoint(1, 1), buffer, sizeof(buffer),
                                     &needed) == RoboOk);
    RoboCheck(strstr(buffer, "hit test [1,1]") != NULL);

    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.17 — what a frame costs, recorded rather than asserted            */
/* ------------------------------------------------------------------ */

/**
 * The baseline is the point of the case, not a threshold — a frame here is 120 "damage a 16×8 region
 * and pump" iterations over a two-layer scene, timed once with a small damage and once with twice
 * the damage.
 *
 * The claim it *asserts* is the module's own model rather than a wall-clock number: under region
 * presents — `config.damagePresent`, the setting a frame loop that cares about cost would use — the
 * pixels a frame composites are the damage and the layers over it, exactly. Doubling the damaged
 * area doubles the work, and the hundred-and-twentieth frame costs what the first did. A build that
 * re-presented the whole composition per pump, or that re-composited a clean tree, fails here instead
 * of merely being slow.
 */
static void TestPerformanceBaseline(void)
{
    const int32_t width = CompositorFixtureWidth;
    const int32_t height = CompositorFixtureHeight;
    CompositorConfig config;
    Compositor *compositor = NULL;
    GfxSurface *target = CompositorMakeSurface(width, height);
    GfxSurface *base = NULL;
    GfxSurface *over = NULL;
    CompositorStats before;
    CompositorStats after;
    uint64_t smallPixels;
    uint64_t smallBytes;
    uint64_t largePixels;
    clock_t started;
    clock_t finished;
    double smallMillis;
    double largeMillis;
    unsigned frames = 120u;
    unsigned index;

    CompositorFillSurface(target, FixtureBlack);
    CompositorConfigInit(&config);
    config.size = MakeGfxSize(width, height);
    config.format = RoboGfxFormatRgba8;
    config.damagePresent = 1;
    RoboCheck(CompositorCreate(&config, &compositor, NULL) == RoboOk);
    RoboCheck(compositor != NULL);
    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(width, height),
                                       RoboGfxFormatRgba8, &base, NULL) == RoboOk);
    CompositorFillSurface(base, FixtureBlack);
    RoboCheck(CompositorSurfaceAcquire(compositor, 2u, MakeGfxSize(width, height),
                                       RoboGfxFormatRgba8, &over, NULL) == RoboOk);
    CompositorFillSurface(over, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, width, height),
                                     NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);
    /* A translucent child over the opaque base: two layers over every damaged pixel, so "pixels
     * written" is a number the scene states rather than the module's internals. */
    RoboCheck(CompositorLayerCreate(compositor, 2u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 2u, MakeGfxRect(0, 0, width, height),
                                     NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 2u, 2u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetOpacity(compositor, 2u, 128u, NULL) == RoboOk);
    /* Building the tree marked everything stale; the measurement starts from a settled screen. */
    CompositorDamageClear(compositor);

    RoboCheck(CompositorGetStats(compositor, &before) == RoboOk);
    started = clock();
    for (index = 0u; index < frames; index++) {
        RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone,
                                      MakeGfxRect(0, 0, 16, 8), NULL) == RoboOk);
        RoboCheck(CompositorPumpFrame(compositor, target, -1, 0u, NULL, NULL) == RoboOk);
    }
    finished = clock();
    smallMillis = ((double)(finished - started) * 1000.0) / (double)CLOCKS_PER_SEC;
    RoboCheck(CompositorGetStats(compositor, &after) == RoboOk);
    smallPixels = after.pixelsComposited - before.pixelsComposited;
    smallBytes = after.bytesPresented - before.bytesPresented;
    RoboCheckEqInt((int)(after.frames - before.frames), (int)frames);
    /* 16×8 damaged pixels, two layers over them, every frame. */
    RoboCheckEqInt((int)smallPixels, (int)(frames * 16u * 8u * 2u));
    /* Four bytes per written pixel — the frame's bytes are the pixels it wrote, not the destination's size. */
    RoboCheckEqInt((int)smallBytes, (int)(smallPixels * 4u));

    /* The same loop with twice the damaged area: twice the pixels, and the same per-frame shape. */
    RoboCheck(CompositorGetStats(compositor, &before) == RoboOk);
    started = clock();
    for (index = 0u; index < frames; index++) {
        RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone,
                                      MakeGfxRect(0, 0, 32, 8), NULL) == RoboOk);
        RoboCheck(CompositorPumpFrame(compositor, target, -1, 0u, NULL, NULL) == RoboOk);
    }
    finished = clock();
    largeMillis = ((double)(finished - started) * 1000.0) / (double)CLOCKS_PER_SEC;
    RoboCheck(CompositorGetStats(compositor, &after) == RoboOk);
    largePixels = after.pixelsComposited - before.pixelsComposited;
    RoboCheckEqInt((int)(after.frames - before.frames), (int)frames);
    RoboCheckEqInt((int)largePixels, (int)(smallPixels * 2u));

    printf("           compositor.performance.baseline: layers=%u surfaces=%u frames=%d "
           "damage=128px composite=%d px/frame small=%.3fms large=%.3fms\n",
           after.layers, after.surfaces, (int)frames, (int)(16u * 8u * 2u), smallMillis, largeMillis);

    RoboCheck(CompositorSurfaceRelease(compositor, 2u, NULL) == RoboOk);
    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

void TestRegisterCompositorScheduling(void)
{
    TestRegister("compositor.surface.pool", TestSurfacePool);
    TestRegister("compositor.surface.replace", TestSurfaceReplace);
    TestRegister("compositor.surface.resource", TestSurfaceResource);
    TestRegister("compositor.memory.budget", TestMemoryBudget);
    TestRegister("compositor.tile.grid", TestTileGrid);
    TestRegister("compositor.tile.dirty", TestTileDirty);
    TestRegister("compositor.job.schedule", TestJobSchedule);
    TestRegister("compositor.job.deferred", TestJobDeferred);
    TestRegister("compositor.job.refusal", TestJobRefusal);
    TestRegister("compositor.job.nocallback", TestJobNoCallback);
    TestRegister("compositor.animation.easings", TestAnimationEasings);
    TestRegister("compositor.animation.track", TestAnimationTrack);
    TestRegister("compositor.animation.replace", TestAnimationReplaceAndCancel);
    TestRegister("compositor.animation.reduced", TestAnimationReducedMotion);
    TestRegister("compositor.scroll.smooth", TestSmoothScroll);
    TestRegister("compositor.frame.post", TestFramePosts);
    TestRegister("compositor.frame.pump", TestFramePump);
    TestRegister("compositor.frame.vsync", TestFrameVsync);
    TestRegister("compositor.frame.fences", TestFrameFences);
    TestRegister("compositor.frame.writers", TestFrameWriters);
    TestRegister("compositor.performance.baseline", TestPerformanceBaseline);
}
