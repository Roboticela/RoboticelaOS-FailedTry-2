/*
 * Tile grids and the raster scheduler — Phase 15.5 and 15.6.
 *
 * ## A grid per surface, created lazily
 *
 * A grid is `ceil(w/edge) × ceil(h/edge)` tiles over one surface, and it appears the first time that
 * surface is damaged rather than with the surface: a compositor with one stale surface should not pay
 * for a grid over every surface in the pool. The tiles' rectangles are clipped to the surface's right
 * and bottom edges (`CompositorTileRect` is the only door that computes one), so a 100×100 surface
 * with a 64-pixel edge is 2×2 tiles and the last row and column are smaller — not one tile that
 * overhangs.
 *
 * ## The queue is a ring, and draining it is a separate step
 *
 * `CompositorScheduleFrame` fills the queue and `CompositorRunScheduled` empties it, and the two are
 * separate because that is what makes a budget meaningful. A job that the budget did not reach stays
 * queued **across the frame boundary** — the ring means that costs nothing — and its tile stays
 * `Dirty`, so nothing is lost and nothing is scheduled twice: the scheduler skips a tile that already
 * has a job waiting.
 *
 * ## The seam: this file decides *what*, never *how*
 *
 * `CompositorRunScheduled` calls the caller's `CompositorRasterFn`, one job at a time, and marks a
 * tile `Clean` only when that call answered `RoboOk`. It rasterizes nothing itself and knows nothing
 * about display lists, fonts or paint. A callback that fails **stops the run**: the job stays queued,
 * the tile stays dirty and the refusal is counted, so a buffer that was busy means "try again next
 * frame" rather than a stale pixel nobody can explain.
 *
 * ## Why the composition's own damage does not dirty a tile
 *
 * A `(none, rect)` damage region means something *moved*: the pixels are fine and only their
 * arrangement is stale. So it produces no raster work — it makes the frame's composite smaller
 * (`CompositorPresent` with `RoboCompositorPresentDamage`) and nothing else. That is the whole reason
 * the two spaces share one list.
 */

#include "RoboCompositorInternal.h"
#include "RoboCompositor/RoboCompositorTiles.h"

/* ------------------------------------------------------------------ */
/* Grids                                                               */
/* ------------------------------------------------------------------ */

static unsigned TileIndex(const CompositorTileGrid *grid, unsigned column, unsigned row)
{
    return row * grid->columns + column;
}

void CompositorTileStatsInit(CompositorTileStats *stats)
{
    if (stats) {
        MemSet(stats, 0, sizeof(*stats));
    }
}

Result CompositorTilesEnsure(Compositor *compositor,
                             CompositorSurfaceId surface,
                             unsigned *outColumns,
                             unsigned *outRows,
                             CompositorDiag *diag)
{
    CompositorTileGrid *grid;

    if (outColumns) {
        *outColumns = 0u;
    }
    if (outRows) {
        *outRows = 0u;
    }
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!CompositorFindSurfaceConst(compositor, surface)) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainTiles,
                       "no such surface to tile");
        return RoboNotFound;
    }
    grid = CompositorFindGrid(compositor, surface, 1);
    if (!grid) {
        CompositorFail(compositor, diag, RoboOutOfMemory, RoboCompositorDomainTiles,
                       "no room for another tile grid: raise maxSurfaces or reset one");
        return RoboOutOfMemory;
    }
    if (outColumns) {
        *outColumns = grid->columns;
    }
    if (outRows) {
        *outRows = grid->rows;
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorTileGridSize(const Compositor *compositor,
                              CompositorSurfaceId surface,
                              unsigned *outColumns,
                              unsigned *outRows)
{
    const CompositorSurfaceSlot *slot;
    unsigned columns;
    unsigned rows;

    if (outColumns) {
        *outColumns = 0u;
    }
    if (outRows) {
        *outRows = 0u;
    }
    if (!compositor) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindSurfaceConst(compositor, surface);
    if (!slot || !slot->surface) {
        return RoboNotFound;
    }
    columns = ((unsigned)slot->size.width + compositor->config.tileEdge - 1u) /
              compositor->config.tileEdge;
    rows = ((unsigned)slot->size.height + compositor->config.tileEdge - 1u) /
           compositor->config.tileEdge;
    if (outColumns) {
        *outColumns = columns ? columns : 1u;
    }
    if (outRows) {
        *outRows = rows ? rows : 1u;
    }
    return RoboOk;
}

Result CompositorTileRect(const Compositor *compositor,
                          CompositorSurfaceId surface,
                          unsigned column,
                          unsigned row,
                          GfxRect *out)
{
    const CompositorSurfaceSlot *slot;
    unsigned columns;
    unsigned rows;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (out) {
        *out = GfxRectEmpty();
    }
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindSurfaceConst(compositor, surface);
    if (!slot || !slot->surface) {
        return RoboNotFound;
    }
    columns = ((unsigned)slot->size.width + compositor->config.tileEdge - 1u) /
              compositor->config.tileEdge;
    rows = ((unsigned)slot->size.height + compositor->config.tileEdge - 1u) /
           compositor->config.tileEdge;
    if (columns == 0u) {
        columns = 1u;
    }
    if (rows == 0u) {
        rows = 1u;
    }
    if (column >= columns || row >= rows) {
        return RoboNotFound;
    }
    x0 = (int32_t)(column * compositor->config.tileEdge);
    y0 = (int32_t)(row * compositor->config.tileEdge);
    x1 = x0 + (int32_t)compositor->config.tileEdge;
    y1 = y0 + (int32_t)compositor->config.tileEdge;
    if (x1 > slot->size.width) {
        x1 = slot->size.width;
    }
    if (y1 > slot->size.height) {
        y1 = slot->size.height;
    }
    *out = MakeGfxRect(x0, y0, x1 - x0, y1 - y0);
    return RoboOk;
}

Result CompositorTileStateOf(const Compositor *compositor,
                             CompositorSurfaceId surface,
                             unsigned column,
                             unsigned row,
                             CompositorTileState *out)
{
    const CompositorTileGrid *grid;

    if (out) {
        *out = RoboCompositorTileEmpty;
    }
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    grid = CompositorFindGrid((Compositor *)compositor, surface, 0);
    if (!grid) {
        /* No grid yet means nothing has ever been produced for this surface, which is `Empty` rather
         * than an error: a caller asking about an untouched surface has a right answer. */
        return CompositorFindSurfaceConst(compositor, surface) ? RoboOk : RoboNotFound;
    }
    if (column >= grid->columns || row >= grid->rows) {
        return RoboNotFound;
    }
    *out = (CompositorTileState)grid->states[TileIndex(grid, column, row)];
    return RoboOk;
}

size_t CompositorTileDirtyCount(const Compositor *compositor, CompositorSurfaceId surface)
{
    size_t dirty = 0u;
    unsigned i;
    unsigned j;

    if (!compositor) {
        return 0u;
    }
    for (i = 0u; i < compositor->gridSlots; i++) {
        const CompositorTileGrid *grid = &compositor->grids[i];
        if (!grid->used || (surface != RoboCompositorSurfaceNone && grid->surface != surface)) {
            continue;
        }
        for (j = 0u; j < grid->columns * grid->rows; j++) {
            if (grid->states[j] == (uint8_t)RoboCompositorTileDirty) {
                dirty++;
            }
        }
    }
    return dirty;
}

Result CompositorTilesReset(Compositor *compositor)
{
    unsigned i;
    unsigned j;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < compositor->gridSlots; i++) {
        CompositorTileGrid *grid = &compositor->grids[i];
        if (!grid->used) {
            continue;
        }
        for (j = 0u; j < grid->columns * grid->rows; j++) {
            grid->states[j] = (uint8_t)RoboCompositorTileEmpty;
        }
    }
    return RoboOk;
}

/** Mark every tile intersecting @p rect with @p state. Returns the number touched. */
static unsigned MarkTiles(Compositor *compositor,
                          CompositorSurfaceId surface,
                          GfxRect rect,
                          CompositorTileState state)
{
    CompositorTileGrid *grid = CompositorFindGrid(compositor, surface, 0);
    unsigned touched = 0u;
    unsigned column;
    unsigned row;

    if (!grid) {
        return 0u;
    }
    for (row = 0u; row < grid->rows; row++) {
        for (column = 0u; column < grid->columns; column++) {
            GfxRect tile;
            if (CompositorTileRect(compositor, surface, column, row, &tile) != RoboOk) {
                continue;
            }
            if (GfxRectIntersects(tile, rect)) {
                grid->states[TileIndex(grid, column, row)] = (uint8_t)state;
                touched++;
            }
        }
    }
    return touched;
}

Result CompositorTileMarkDirty(Compositor *compositor,
                               CompositorSurfaceId surface,
                               GfxRect rect,
                               CompositorDiag *diag)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!CompositorFindSurfaceConst(compositor, surface)) {
        return RoboNotFound;
    }
    if (GfxRectIsEmpty(rect)) {
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }
    if (!CompositorFindGrid(compositor, surface, 1)) {
        CompositorFail(compositor, diag, RoboOutOfMemory, RoboCompositorDomainTiles,
                       "no room for a tile grid over that surface");
        return RoboOutOfMemory;
    }
    MarkTiles(compositor, surface, rect, RoboCompositorTileDirty);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorTileMarkClean(Compositor *compositor, CompositorSurfaceId surface, GfxRect rect)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!CompositorFindSurfaceConst(compositor, surface)) {
        return RoboNotFound;
    }
    MarkTiles(compositor, surface, rect, RoboCompositorTileClean);
    return RoboOk;
}

Result CompositorTilesDirtyFromDamage(Compositor *compositor, CompositorDiag *diag)
{
    unsigned i;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < compositor->damageCount; i++) {
        const CompositorDamageEntry *entry = &compositor->damage[i];
        if (entry->surface == RoboCompositorSurfaceNone) {
            continue; /* arrangement, not pixels: no raster work */
        }
        if (!CompositorFindSurfaceConst(compositor, entry->surface)) {
            continue; /* the surface went away between the damage and the scheduler */
        }
        if (!CompositorFindGrid(compositor, entry->surface, 1)) {
            CompositorFail(compositor, diag, RoboOutOfMemory, RoboCompositorDomainTiles,
                           "no room for a tile grid over a damaged surface");
            return RoboOutOfMemory;
        }
        MarkTiles(compositor, entry->surface, entry->rect, RoboCompositorTileDirty);
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorGetTileStats(const Compositor *compositor, CompositorTileStats *out)
{
    unsigned i;
    unsigned j;

    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    CompositorTileStatsInit(out);
    out->tileEdge = compositor->config.tileEdge;
    for (i = 0u; i < compositor->gridSlots; i++) {
        const CompositorTileGrid *grid = &compositor->grids[i];
        if (!grid->used) {
            continue;
        }
        out->grids++;
        for (j = 0u; j < grid->columns * grid->rows; j++) {
            out->total++;
            switch ((CompositorTileState)grid->states[j]) {
            case RoboCompositorTileClean:
                out->clean++;
                break;
            case RoboCompositorTileDirty:
                out->dirty++;
                break;
            default:
                out->empty++;
                break;
            }
        }
    }
    out->rasters = compositor->statRasters;
    out->rasterRefusals = compositor->statRasterRefusals;
    out->rasterPixels = compositor->statRasterPixels;
    return RoboOk;
}

void CompositorResetTileStats(Compositor *compositor)
{
    if (!compositor) {
        return;
    }
    compositor->statRasters = 0u;
    compositor->statRasterRefusals = 0u;
    compositor->statRasterPixels = 0u;
}

/* ------------------------------------------------------------------ */
/* Jobs                                                                */
/* ------------------------------------------------------------------ */

void CompositorScheduleStatsInit(CompositorScheduleStats *stats)
{
    if (stats) {
        MemSet(stats, 0, sizeof(*stats));
    }
}

Result CompositorSetRasterFn(Compositor *compositor, CompositorRasterFn fn, void *user)
{
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    compositor->rasterFn = fn;
    compositor->rasterUser = user;
    return RoboOk;
}

/** Non-zero when a job for @p rect of @p surface is already waiting in the ring. */
static int JobQueued(const Compositor *compositor, CompositorSurfaceId surface, GfxRect rect)
{
    unsigned i;

    for (i = 0u; i < compositor->jobCount; i++) {
        const CompositorJob *job =
            &compositor->jobs[(compositor->jobHead + i) % compositor->jobSlots];
        if (job->surface == surface && job->rect.x == rect.x && job->rect.y == rect.y &&
            job->rect.width == rect.width && job->rect.height == rect.height) {
            return 1;
        }
    }
    return 0;
}

Result CompositorScheduleFrame(Compositor *compositor,
                               unsigned budget,
                               CompositorScheduleStats *out,
                               CompositorDiag *diag)
{
    unsigned i;
    unsigned j;
    unsigned scheduled = 0u;
    unsigned deferred = 0u;

    (void)diag;
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (out) {
        CompositorScheduleStatsInit(out);
    }
    /* The dirty set is already the damage's answer: surface damage is recorded as dirty tiles when
     * it is *added* (`NoteSurfaceDamage`), not when a frame happens, which is what keeps a tile a
     * budget cut from being re-rasterized on every pass until something presents.
     * `CompositorTilesDirtyFromDamage` remains a door a caller may invoke deliberately. */
    if (!compositor->rasterFn) {
        /* Nothing to call: a compositor whose content never changes is a supported configuration, so
         * this is not an error and produces no jobs. */
        if (out) {
            out->pending = compositor->jobCount;
            out->budget = budget;
        }
        return RoboOk;
    }
    for (i = 0u; i < compositor->gridSlots; i++) {
        CompositorTileGrid *grid = &compositor->grids[i];
        if (!grid->used) {
            continue;
        }
        for (j = 0u; j < grid->columns * grid->rows; j++) {
            GfxRect tile;
            unsigned column = j % grid->columns;
            unsigned row = j / grid->columns;
            if (grid->states[j] != (uint8_t)RoboCompositorTileDirty) {
                continue;
            }
            if (budget != 0u && scheduled >= budget) {
                deferred++;
                continue;
            }
            if (CompositorTileRect(compositor, grid->surface, column, row, &tile) != RoboOk) {
                continue;
            }
            if (JobQueued(compositor, grid->surface, tile)) {
                /* Already waiting from a frame the budget did not finish: not a new job, and not a
                 * deferral either — it is work the queue is holding. */
                continue;
            }
            if (compositor->jobCount >= compositor->jobSlots) {
                deferred++;
                continue;
            }
            {
                unsigned tail = (compositor->jobHead + compositor->jobCount) % compositor->jobSlots;
                compositor->jobs[tail].surface = grid->surface;
                compositor->jobs[tail].rect = tile;
                compositor->jobs[tail].kind = RoboCompositorJobTile;
                compositor->jobCount++;
            }
            scheduled++;
        }
    }
    compositor->statJobsScheduled += scheduled;
    compositor->statJobsDeferred += deferred;
    if (out) {
        out->scheduled = scheduled;
        out->deferred = deferred;
        out->pending = compositor->jobCount;
        out->budget = budget;
    }
    return RoboOk;
}

Result CompositorRunScheduled(Compositor *compositor,
                              CompositorScheduleStats *out,
                              CompositorDiag *diag)
{
    CompositorScheduleStats local;
    unsigned executed = 0u;
    unsigned refused = 0u;
    uint64_t pixels = 0u;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    CompositorScheduleStatsInit(&local);
    local.budget = out ? out->budget : 0u;
    if (compositor->rasterFn) {
        while (compositor->jobCount > 0u) {
            CompositorJob *job = &compositor->jobs[compositor->jobHead];
            CompositorDiag jobDiag;
            Result result;

            CompositorDiagReset(&jobDiag);
            result = compositor->rasterFn(compositor->rasterUser, job->surface, job->rect, job->kind,
                                          &jobDiag);
            if (result != RoboOk) {
                /* Stop rather than skip: the job stays queued and its tile stays dirty, so the next
                 * frame tries again and nothing is silently lost. */
                refused++;
                compositor->statRasterRefusals++;
                if (diag) {
                    *diag = jobDiag;
                    diag->code = result;
                    diag->domain = RoboCompositorDomainJobs;
                    diag->message = jobDiag.message[0] ? jobDiag.message
                                                       : "the rasterizer refused a job";
                }
                break;
            }
            CompositorTileMarkClean(compositor, job->surface, job->rect);
            /* Newly produced pixels are not on screen until the composite that places them runs, so
             * every layer using this surface is invalidated in composition space. This is the join
             * between "a surface's content changed" and "the frame must be redrawn", and without it
             * a raster job would produce pixels nothing ever presented. */
            {
                unsigned k;
                for (k = 0u; k < compositor->layerSlots; k++) {
                    if (compositor->layers[k].used &&
                        compositor->layers[k].surface == job->surface) {
                        CompositorInvalidateLayer(compositor, compositor->layers[k].id, NULL);
                    }
                }
            }
            pixels += (uint64_t)(uint32_t)(job->rect.width > 0 ? job->rect.width : 0) *
                      (uint64_t)(uint32_t)(job->rect.height > 0 ? job->rect.height : 0);
            compositor->jobHead = (compositor->jobHead + 1u) % compositor->jobSlots;
            compositor->jobCount--;
            executed++;
            compositor->statRasters++;
            compositor->statJobsRun++;
        }
    }
    compositor->statRasterPixels += pixels;
    if (out) {
        out->executed = executed;
        out->refused = refused;
        out->pixels = pixels;
        out->pending = compositor->jobCount;
        /* `out->scheduled` is what the caller's scheduler pass put there, and is preserved. */
    }
    if (refused == 0u && diag) {
        CompositorDiagReset(diag);
    }
    /* A refused job is reported (stats and diag) rather than raised: the frame continues with the
     * tiles that were produced, and the rest are dirty for the next one. */
    return RoboOk;
}

unsigned CompositorPendingJobCount(const Compositor *compositor)
{
    return compositor ? compositor->jobCount : 0u;
}

unsigned CompositorClearJobs(Compositor *compositor)
{
    unsigned cleared;

    if (!compositor) {
        return 0u;
    }
    cleared = compositor->jobCount;
    compositor->jobCount = 0u;
    compositor->jobHead = 0u;
    return cleared;
}

Result CompositorWriteTiles(const Compositor *compositor,
                            char *buffer,
                            size_t bufferSize,
                            size_t *outNeeded)
{
    CompositorDump dump;
    unsigned i;
    unsigned j;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorDumpInit(&dump, buffer, bufferSize);
    for (i = 0u; i < compositor->gridSlots; i++) {
        const CompositorTileGrid *grid = &compositor->grids[i];
        if (!grid->used) {
            continue;
        }
        for (j = 0u; j < grid->columns * grid->rows; j++) {
            GfxRect tile;
            unsigned column = j % grid->columns;
            unsigned row = j / grid->columns;
            if (CompositorTileRect(compositor, grid->surface, column, row, &tile) != RoboOk) {
                continue;
            }
            CompositorDumpPrintf(&dump, "surface %llu tile %u,%u [%d,%d,%d,%d] %s\n",
                                 (unsigned long long)grid->surface, column, row, (int)tile.x,
                                 (int)tile.y, (int)tile.width, (int)tile.height,
                                 CompositorTileStateName((CompositorTileState)grid->states[j]));
        }
    }
    return CompositorDumpFinish(&dump, outNeeded);
}
