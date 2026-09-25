/*
 * Damage — Phase 15.4, the list of what went stale.
 *
 * ## What a frame costs is what changed
 *
 * A compositor that repaints the screen every frame is a compositor whose cost is a function of the
 * screen; one that knows what changed is a function of the change. That single idea is what this
 * header is: a bounded list of rectangles, two ways to fill it (a region, or everything), and the
 * queries a scheduler and a presenter need — how many regions, how many pixels, is a point inside,
 * and is the answer "all of it".
 *
 * ## Regions are named, and the name is a surface
 *
 * A damaged region is `(surface, rect)`. `surface` **none means the composition itself**: something
 * moved, an opacity changed, a clip changed — a layer's pixels are fine and only the arrangement is
 * stale, so the present has work and the rasterizer has none. A named surface means that surface's
 * content is stale, which is what produces raster jobs (15.6). Keeping the two in one list with one
 * field is deliberate: the two are the same *question* ("what needs doing this frame") with two
 * answers, and a second list would be a second place for them to disagree.
 *
 * ## Bounded, and honest about it
 *
 * The list holds `maxDamageRects` entries. Past that it does not drop regions — dropping one is a
 * visibly stale pixel — it **merges**: two entries are replaced by their bounding rectangle, and the
 * merge is counted (`damageMerges`). When the merge can no longer help (the whole composition is
 * already covered), the list becomes **full** (`CompositorDamageIsFull`), which the presenter reads
 * as "present everything" and the count of which is `damageOverflows`. A merge is a bigger region
 * than it needs to be — work, never a wrong pixel — which is the trade this module makes on purpose.
 *
 * ## Rectangles are clipped, never trusted
 *
 * A region outside the composition is intersected with it before it is added, so the pixel counts a
 * frame reports are pixels that exist. A region that vanishes under the clip is a no-op success
 * (`RoboOk`), not an error: a scroll near an edge legitimately damages nothing.
 */

#ifndef ROBO_COMPOSITOR_DAMAGE_H
#define ROBO_COMPOSITOR_DAMAGE_H

#include "RoboCompositor/RoboCompositorLayer.h"
#include "RoboCompositor/RoboCompositorTypes.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mark @p rect of @p surface stale (or of the composition, when @p surface is none).
 *
 * The rectangle is clipped to the surface's stored size when it names one, and to the compositor's
 * size otherwise. Merging past the list's bound is described in this file's comment.
 */
Result CompositorDamageAdd(Compositor *compositor,
                           CompositorSurfaceId surface,
                           GfxRect rect,
                           CompositorDiag *diag);

/** Mark the whole composition stale. The next present is a full one by construction. */
Result CompositorDamageAddAll(Compositor *compositor, CompositorDiag *diag);

/** Empty the list, and clear the full flag. Called by a present when the frame is done with it. */
void CompositorDamageClear(Compositor *compositor);

/** Number of regions the list holds. 0 when nothing is stale. */
unsigned CompositorDamageRectCount(const Compositor *compositor);

/**
 * The @p index'th region. @p outSurface (when non-NULL) receives the surface it belongs to, which is
 * none for a composition region.
 */
Result CompositorDamageRect(const Compositor *compositor,
                            unsigned index,
                            CompositorSurfaceId *outSurface,
                            GfxRect *out);

/**
 * Damaged pixels, counted **without double-counting overlaps**.
 *
 * A region is clipped to the composition and to the surface it names, so the number is pixels the
 * frame will actually write — which is the number a test wants when it asserts that a two-pixel
 * change cost two pixels rather than a screen.
 */
uint64_t CompositorDamagePixelCount(const Compositor *compositor);

/** 1 when the list is "everything" rather than a set of regions. */
int CompositorDamageIsFull(const Compositor *compositor);

/** 1 when @p point is inside a region (or when the list is full). */
int CompositorDamageContains(const Compositor *compositor, GfxPoint point);

/** Mark @p id's accumulated bounds stale — "this layer moved, or changed how it looks". */
Result CompositorInvalidateLayer(Compositor *compositor, CompositorLayerId id, CompositorDiag *diag);

/** Mark every surface's own bounds stale, which is what a resize of the composition is. */
Result CompositorDamageAddAllSurfaces(Compositor *compositor, CompositorDiag *diag);

/** Damage-list counters, for a dump and for the phase's scheduling assertions. */
typedef struct CompositorDamageStats {
    uint64_t regionsAdded;
    uint64_t merges;        /* a region replaced by a bounding rectangle */
    uint64_t overflows;     /* the list became full */
    uint64_t pixelsInvalidated;
    uint64_t clippedAway;   /* regions that clipped to nothing */
    unsigned regionsNow;
    int full;
} CompositorDamageStats;

void CompositorDamageStatsInit(CompositorDamageStats *stats);
Result CompositorGetDamageStats(const Compositor *compositor, CompositorDamageStats *out);
void CompositorResetDamageStats(Compositor *compositor);

/** One line per region: `region i surface s [x,y,w,h] pixels`. Complete or absent. */
Result CompositorWriteDamage(const Compositor *compositor,
                             char *buffer,
                             size_t bufferSize,
                             size_t *outNeeded);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_DAMAGE_H */
