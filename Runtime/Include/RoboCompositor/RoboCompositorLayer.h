/*
 * The layer tree — Phase 15.2, and the offsets and clips 15.11 needs.
 *
 * ## Why a tree rather than a list
 *
 * A shell's own composition is a tree: a window owns a title bar and a body, a scrolled panel owns
 * its rows, a menu hangs off the bar. Two things only a tree can express are what this module needs
 * from it — **an origin that accumulates** (a child's rectangle is relative to its parent, so moving
 * a window moves what is inside it without touching a single child) and **an opacity and a clip that
 * multiply down the chain** (a fading window fades what it contains). A flat list with absolute
 * rectangles forces every caller to recompute both, which is the arithmetic this module exists to
 * own.
 *
 * ## The rules, stated once
 *
 *   **Paint order is depth-first, parent first, children in creation order.** That is the same rule
 *   the document layer uses (document order *is* paint order in this build), and it is why a caller
 *   that cares about stacking writes children in the order it wants them drawn.
 *
 *   **A child's `rect` is in its parent's coordinate space**, and a layer with no parent's rectangle
 *   is in the compositor's. `CompositorLayerBoundsInCompositor` accumulates the chain and applies
 *   each ancestor's scale, so a caller never adds up origins itself.
 *
 *   **`offsetX`/`offsetY` are the cheap transform**: they move a layer's content without touching its
 *   rectangle, which is what a scroll (15.11) and a slide animation (15.13) both are. `scale` is
 *   uniform and multiplies down the chain. There is no rotation in this build.
 *
 *   **`clip` is an axis-aligned rectangle in the layer's own parent space** and intersects down the
 *   chain. Unset means "no clip of my own", not "clip to nothing".
 *
 * ## Bounds of the tree
 *
 * A compositor allocated `maxLayers` slots; creating one past that is `RoboOutOfMemory` and is
 * counted, not silently dropped. Ids are the caller's — a shell names its layers, which is what
 * makes a dump readable and a rebind after a surface change possible.
 */

#ifndef ROBO_COMPOSITOR_LAYER_H
#define ROBO_COMPOSITOR_LAYER_H

#include "RoboCompositor/RoboCompositorSurface.h"
#include "RoboCompositor/RoboCompositorTypes.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A layer's whole state, by value — what a dump writes and a test reads back. */
typedef struct CompositorLayerInfo {
    CompositorLayerId id;
    CompositorLayerId parent;      /* `RoboCompositorLayerNone` for a root */
    CompositorSurfaceId surface;   /* none for a layer that has only children */
    GfxRect rect;                  /* in the parent's space (or the compositor's, for a root) */
    float offsetX;
    float offsetY;
    float scale;                   /* > 0 */
    uint8_t opacity;               /* 255 = opaque; multiplies with ancestors at composite time */
    GfxRect clip;                  /* valid only when hasClip */
    int hasClip;
    int visible;
    unsigned children;             /* direct children */
} CompositorLayerInfo;

/**
 * Create a layer named @p id, optionally under @p parent.
 *
 * A duplicate id is `RoboAlreadyExists`; an unknown parent is `RoboNotFound`; no free slot is
 * `RoboOutOfMemory`. The new layer is visible, opaque, unscaled and unsized — a caller sets a
 * rectangle before it is worth painting, and a zero-sized layer paints nothing rather than being
 * refused.
 */
Result CompositorLayerCreate(Compositor *compositor,
                             CompositorLayerId id,
                             CompositorLayerId parent,
                             CompositorDiag *diag);

/**
 * Remove @p id and its whole subtree.
 *
 * The count of layers removed (including @p id) is written to @p outRemoved when non-NULL, because a
 * caller that destroyed a group wants to know how many went with it. The layers' surfaces are
 * **not** released: a surface belongs to the pool, and a tree edit is not a pixel decision.
 */
Result CompositorLayerDestroy(Compositor *compositor,
                              CompositorLayerId id,
                              size_t *outRemoved,
                              CompositorDiag *diag);

/** Move @p id under @p parent (or to the root when @p parent is none). A cycle is refused. */
Result CompositorLayerSetParent(Compositor *compositor,
                                CompositorLayerId id,
                                CompositorLayerId parent,
                                CompositorDiag *diag);

/** Point a layer at content, or at nothing. An unknown surface is `RoboNotFound`. */
Result CompositorLayerSetSurface(Compositor *compositor,
                                 CompositorLayerId id,
                                 CompositorSurfaceId surface,
                                 CompositorDiag *diag);

Result CompositorLayerSetRect(Compositor *compositor,
                              CompositorLayerId id,
                              GfxRect rect,
                              CompositorDiag *diag);

Result CompositorLayerSetOffset(Compositor *compositor,
                                CompositorLayerId id,
                                float offsetX,
                                float offsetY,
                                CompositorDiag *diag);

/** A scale of zero or less, or not finite, is `RoboInvalidArgument`. */
Result CompositorLayerSetScale(Compositor *compositor,
                               CompositorLayerId id,
                               float scale,
                               CompositorDiag *diag);

Result CompositorLayerSetOpacity(Compositor *compositor,
                                 CompositorLayerId id,
                                 uint8_t opacity,
                                 CompositorDiag *diag);

Result CompositorLayerSetClip(Compositor *compositor,
                              CompositorLayerId id,
                              GfxRect clip,
                              CompositorDiag *diag);

Result CompositorLayerClearClip(Compositor *compositor, CompositorLayerId id);

Result CompositorLayerSetVisible(Compositor *compositor,
                                 CompositorLayerId id,
                                 int visible,
                                 CompositorDiag *diag);

/** @p id's whole state. Unknown id -> `RoboNotFound`. */
Result CompositorLayerGetInfo(const Compositor *compositor,
                              CompositorLayerId id,
                              CompositorLayerInfo *out);

/** Layers in the tree, at any depth. */
unsigned CompositorLayerCount(const Compositor *compositor);

/** Direct children of @p id (or of the root when @p id is none). */
unsigned CompositorLayerChildCount(const Compositor *compositor, CompositorLayerId id);

/** The @p index'th direct child of @p id, in creation order. */
Result CompositorLayerChild(const Compositor *compositor,
                            CompositorLayerId id,
                            unsigned index,
                            CompositorLayerId *out);

/**
 * Paint order, root first and depth-first, into @p out.
 *
 * Writes at most @p capacity ids, reports how many the whole order holds in @p outCount (which is
 * how a caller learns it needs a bigger buffer), and returns `RoboOk` either way — a truncated read
 * is a caller's decision, not a failure. This is the order `CompositorPresent` draws in, so a dump
 * of it is a dump of the frame's stacking.
 */
Result CompositorLayerPaintOrder(const Compositor *compositor,
                                 CompositorLayerId *out,
                                 size_t capacity,
                                 size_t *outCount);

/**
 * @p id's rectangle in compositor space, with every ancestor's origin and scale applied and the
 * layer's own offset included. Unknown id -> `RoboNotFound`.
 */
Result CompositorLayerBoundsInCompositor(const Compositor *compositor,
                                         CompositorLayerId id,
                                         GfxRect *out);

/**
 * The topmost **visible** layer whose accumulated bounds contain @p point, or none.
 *
 * Topmost means last in paint order, which is the layer a pointer is actually over: a hit test that
 * answered the first match would answer the layer underneath the window the user clicked.
 */
Result CompositorLayerHitTest(const Compositor *compositor,
                              GfxPoint point,
                              CompositorLayerId *out);

/* ---- Scrolling (15.11) -------------------------------------------------------------------- */

/**
 * Set a layer's scroll offset, in pixels.
 *
 * The offset moves the content **up and left** as it grows, which is the direction every scroll
 * offset in this tree has (`LayoutScroll`), so a positive `offsetY` reveals what is below the
 * layer's box. The damage the change implies is added for the layer's whole box, because a scroll
 * moves content across all of it.
 */
Result CompositorLayerSetScroll(Compositor *compositor,
                                CompositorLayerId id,
                                float offsetX,
                                float offsetY,
                                CompositorDiag *diag);

/** The current scroll offset of @p id. */
Result CompositorLayerGetScroll(const Compositor *compositor,
                                CompositorLayerId id,
                                float *outOffsetX,
                                float *outOffsetY);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_COMPOSITOR_LAYER_H */
