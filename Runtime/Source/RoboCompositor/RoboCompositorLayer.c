/*
 * The layer tree — Phase 15.2, plus the scrolling offset 15.11 sets on it.
 *
 * ## Child lists, one shape
 *
 * A layer holds `firstChild` and `nextSibling`; the roots are a list of the same shape led by
 * `firstRoot`. That is a child list where **creation order is paint order**, which is the rule the
 * module states and the document layer already uses, and it means there is exactly one piece of code
 * that appends to a list and one that unlinks from it — a second shape (a vector of children, say)
 * would be a second place for the order to be wrong.
 *
 * ## The geometry, resolved once
 *
 * Every question about where a layer is — its bounds, a hit test, the composite pass — needs the
 * same answer: the layer's rectangle origin in compositor space, the scale accumulated from the
 * root, and the clip accumulated on the way down. `ResolveLayer` answers it in one walk from the
 * roots, and every door in this file and in `RoboCompositorCompose.c` goes through it. A door that
 * added up origins itself would be a second answer to "where is this", and the two would disagree
 * the first time an ancestor was scaled.
 *
 * The transform is stated in one line: a point `p` in a layer's own rectangle space maps to
 *
 *     compositor = boxOrigin + offset * parentScale + p * scale
 *
 * so a **translate happens before the scale** (an offset is not multiplied by the layer's own scale)
 * and an ancestor's scale multiplies both. `CompositorLayerBoundsInCompositor` is that expression
 * with `p` at (0,0) and the size at the rectangle's, which is why a scaled layer's bounds grow and
 * its offset does not.
 *
 * ## A mutation damages, and does not post
 *
 * Every door here that changes something visible adds the affected region to the damage list. None
 * of them posts a frame: whether to draw is the caller's decision (and `CompositorPumpFrame` draws
 * whenever there is damage), so a batch of ten layer edits is one frame rather than ten. A scroll is
 * the one exception and only because it is the one mutation whose *reason* is worth recording
 * (`RoboCompositorFrameReasonScroll`).
 */

#include "RoboCompositorInternal.h"

/* ------------------------------------------------------------------ */
/* Child lists                                                         */
/* ------------------------------------------------------------------ */

/** The head of the list @p slot is in: its parent's children, or the roots. */
static CompositorLayerId *ListHead(Compositor *compositor, CompositorLayerSlot *slot)
{
    if (slot->parent == RoboCompositorLayerNone) {
        return &compositor->firstRoot;
    }
    {
        CompositorLayerSlot *parent = CompositorFindLayer(compositor, slot->parent);
        return parent ? &parent->firstChild : &compositor->firstRoot;
    }
}

/** Unlink @p slot from whichever list holds it. */
static void UnlinkLayer(Compositor *compositor, CompositorLayerSlot *slot)
{
    CompositorLayerId *head = ListHead(compositor, slot);
    CompositorLayerId cursor = *head;

    if (cursor == slot->id) {
        *head = slot->nextSibling;
        slot->nextSibling = RoboCompositorLayerNone;
        return;
    }
    while (cursor != RoboCompositorLayerNone) {
        CompositorLayerSlot *prev = CompositorFindLayer(compositor, cursor);
        if (!prev) {
            break;
        }
        if (prev->nextSibling == slot->id) {
            prev->nextSibling = slot->nextSibling;
            slot->nextSibling = RoboCompositorLayerNone;
            return;
        }
        cursor = prev->nextSibling;
    }
}

/** Append @p slot to the end of its parent's child list (creation order is paint order). */
static void AppendLayer(Compositor *compositor, CompositorLayerSlot *slot)
{
    CompositorLayerId *head = ListHead(compositor, slot);
    CompositorLayerId cursor = *head;

    slot->nextSibling = RoboCompositorLayerNone;
    if (cursor == RoboCompositorLayerNone) {
        *head = slot->id;
        return;
    }
    for (;;) {
        CompositorLayerSlot *node = CompositorFindLayer(compositor, cursor);
        if (!node) {
            *head = slot->id;
            return;
        }
        if (node->nextSibling == RoboCompositorLayerNone) {
            node->nextSibling = slot->id;
            return;
        }
        cursor = node->nextSibling;
    }
}

static int IsAncestor(const Compositor *compositor, CompositorLayerId maybeAncestor,
                      CompositorLayerId id)
{
    const CompositorLayerSlot *slot = CompositorFindLayerConst(compositor, id);
    unsigned guard = 0u;

    while (slot && guard <= compositor->layerSlots) {
        if (slot->parent == maybeAncestor) {
            return 1;
        }
        slot = CompositorFindLayerConst(compositor, slot->parent);
        guard++;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

static int ResolveWalk(const Compositor *compositor,
                       CompositorLayerId target,
                       CompositorLayerId node,
                       float px,
                       float py,
                       float ps,
                       GfxRect clip,
                       int hasClip,
                       unsigned depth,
                       CompositorLayerGeometry *out)
{
    const CompositorLayerSlot *slot;
    float nx;
    float ny;
    float ns;
    float cx;
    float cy;
    GfxRect nclip = clip;
    int nhas = hasClip;
    CompositorLayerId child;

    if (node == RoboCompositorLayerNone || depth > compositor->layerSlots) {
        return 0;
    }
    slot = CompositorFindLayerConst(compositor, node);
    if (!slot) {
        return 0;
    }
    nx = px + (float)slot->rect.x * ps;
    ny = py + (float)slot->rect.y * ps;
    ns = ps * slot->scale;
    /* A child is placed against its parent's **content** origin, so an offset (a scroll, a slide)
     * moves what is inside the layer as well as the layer's own pixels. */
    cx = nx + slot->offsetX * ps;
    cy = ny + slot->offsetY * ps;
    if (slot->hasClip && !GfxRectIsEmpty(slot->clip)) {
        /* The clip is stated in the layer's parent space, like its rectangle. */
        GfxRect own = MakeGfxRect(CompositorRound((float)px + (float)slot->clip.x * ps),
                                  CompositorRound((float)py + (float)slot->clip.y * ps),
                                  CompositorRound((float)slot->clip.width * ps),
                                  CompositorRound((float)slot->clip.height * ps));
        if (nhas) {
            GfxRect both;
            if (GfxRectIntersect(nclip, own, &both) == RoboOk) {
                nclip = both;
            } else {
                nclip = GfxRectEmpty();
            }
        } else {
            nclip = own;
            nhas = 1;
        }
    }
    if (node == target) {
        out->boxX = nx;
        out->boxY = ny;
        out->parentScale = ps;
        out->scale = ns;
        out->clip = nclip;
        out->hasClip = nhas;
        return 1;
    }
    for (child = slot->firstChild; child != RoboCompositorLayerNone;) {
        const CompositorLayerSlot *next = CompositorFindLayerConst(compositor, child);
        if (ResolveWalk(compositor, target, child, cx, cy, ns, nclip, nhas, depth + 1u, out)) {
            return 1;
        }
        if (!next) {
            break;
        }
        child = next->nextSibling;
    }
    return 0;
}

int CompositorResolveLayer(const Compositor *compositor, CompositorLayerId id,
                           CompositorLayerGeometry *out)
{
    CompositorLayerId root;

    if (!compositor || !out) {
        return 0;
    }
    if (!CompositorFindLayerConst(compositor, id)) {
        return 0;
    }
    for (root = compositor->firstRoot; root != RoboCompositorLayerNone;) {
        const CompositorLayerSlot *node = CompositorFindLayerConst(compositor, root);
        if (ResolveWalk(compositor, id, root, 0.0f, 0.0f, 1.0f, GfxRectEmpty(), 0, 0u, out)) {
            return 1;
        }
        if (!node) {
            break;
        }
        root = node->nextSibling;
    }
    return 0;
}

GfxRect CompositorLayerLocalBounds(const Compositor *compositor, CompositorLayerId id)
{
    CompositorLayerGeometry geometry;
    const CompositorLayerSlot *slot = CompositorFindLayerConst(compositor, id);

    if (!slot || !CompositorResolveLayer(compositor, id, &geometry)) {
        return GfxRectEmpty();
    }
    return MakeGfxRect(CompositorRound(geometry.boxX + slot->offsetX * geometry.parentScale),
                       CompositorRound(geometry.boxY + slot->offsetY * geometry.parentScale),
                       CompositorRound((float)slot->rect.width * geometry.scale),
                       CompositorRound((float)slot->rect.height * geometry.scale));
}

/* ------------------------------------------------------------------ */
/* Creation and mutation                                               */
/* ------------------------------------------------------------------ */

Result CompositorLayerCreate(Compositor *compositor,
                             CompositorLayerId id,
                             CompositorLayerId parent,
                             CompositorDiag *diag)
{
    unsigned i;
    CompositorLayerSlot *slot = NULL;
    CompositorLayerSlot *parentSlot = NULL;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (id == RoboCompositorLayerNone) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainLayer,
                       "a layer needs a non-zero id: names are what a caller rebinds");
        return RoboInvalidArgument;
    }
    if (CompositorFindLayer(compositor, id)) {
        CompositorFail(compositor, diag, RoboAlreadyExists, RoboCompositorDomainLayer,
                       "that layer id is already in the tree");
        return RoboAlreadyExists;
    }
    if (parent != RoboCompositorLayerNone) {
        parentSlot = CompositorFindLayer(compositor, parent);
        if (!parentSlot) {
            CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainLayer,
                           "the parent layer does not exist");
            return RoboNotFound;
        }
    }
    for (i = 0u; i < compositor->layerSlots; i++) {
        if (!compositor->layers[i].used) {
            slot = &compositor->layers[i];
            break;
        }
    }
    if (!slot) {
        CompositorFail(compositor, diag, RoboOutOfMemory, RoboCompositorDomainLayer,
                       "every layer slot is in use: raise maxLayers or destroy one");
        return RoboOutOfMemory;
    }
    MemSet(slot, 0, sizeof(*slot));
    slot->id = id;
    slot->parent = parent;
    slot->surface = RoboCompositorSurfaceNone;
    slot->rect = MakeGfxRect(0, 0, 0, 0);
    slot->offsetX = 0.0f;
    slot->offsetY = 0.0f;
    slot->scale = 1.0f;
    slot->opacity = 255u;
    slot->visible = 1;
    slot->clip = GfxRectEmpty();
    slot->hasClip = 0;
    slot->firstChild = RoboCompositorLayerNone;
    slot->nextSibling = RoboCompositorLayerNone;
    slot->used = 1;
    AppendLayer(compositor, slot);
    compositor->layerCount++;
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

static size_t DestroySubtree(Compositor *compositor, CompositorLayerId id)
{
    CompositorLayerSlot *slot = CompositorFindLayer(compositor, id);
    size_t removed = 0u;
    CompositorLayerId child;

    if (!slot) {
        return 0u;
    }
    child = slot->firstChild;
    while (child != RoboCompositorLayerNone) {
        CompositorLayerSlot *node = CompositorFindLayer(compositor, child);
        CompositorLayerId next = node ? node->nextSibling : RoboCompositorLayerNone;
        removed += DestroySubtree(compositor, child);
        child = next;
    }
    UnlinkLayer(compositor, slot);
    if (compositor->layerCount > 0u) {
        compositor->layerCount--;
    }
    /* The surface is not released: a surface belongs to the pool, and destroying a layer is a tree
     * edit rather than a pixel decision. */
    MemSet(slot, 0, sizeof(*slot));
    return removed + 1u;
}

Result CompositorLayerDestroy(Compositor *compositor,
                              CompositorLayerId id,
                              size_t *outRemoved,
                              CompositorDiag *diag)
{
    GfxRect bounds;
    size_t removed;

    if (outRemoved) {
        *outRemoved = 0u;
    }
    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!CompositorFindLayer(compositor, id)) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainLayer,
                       "no such layer to destroy");
        return RoboNotFound;
    }
    bounds = CompositorLayerLocalBounds(compositor, id);
    removed = DestroySubtree(compositor, id);
    if (!GfxRectIsEmpty(bounds)) {
        CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, bounds, NULL);
    }
    if (outRemoved) {
        *outRemoved = removed;
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerSetParent(Compositor *compositor,
                                CompositorLayerId id,
                                CompositorLayerId parent,
                                CompositorDiag *diag)
{
    CompositorLayerSlot *slot;
    CompositorLayerSlot *parentSlot = NULL;
    GfxRect before;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainLayer,
                       "no such layer to reparent");
        return RoboNotFound;
    }
    if (parent != RoboCompositorLayerNone) {
        parentSlot = CompositorFindLayer(compositor, parent);
        if (!parentSlot) {
            CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainLayer,
                           "the new parent layer does not exist");
            return RoboNotFound;
        }
        /* A cycle would make the tree untraversable, so it is refused rather than repaired. */
        if (parent == id || IsAncestor(compositor, id, parent)) {
            CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainLayer,
                           "a layer cannot be its own descendant");
            return RoboInvalidArgument;
        }
    }
    if (slot->parent == parent) {
        if (diag) {
            CompositorDiagReset(diag);
        }
        return RoboOk;
    }
    before = CompositorLayerLocalBounds(compositor, id);
    UnlinkLayer(compositor, slot);
    slot->parent = parent;
    AppendLayer(compositor, slot);
    if (!GfxRectIsEmpty(before)) {
        CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, before, NULL);
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerSetSurface(Compositor *compositor,
                                 CompositorLayerId id,
                                 CompositorSurfaceId surface,
                                 CompositorDiag *diag)
{
    CompositorLayerSlot *slot;
    CompositorSurfaceSlot *surfaceSlot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainLayer,
                       "no such layer to point at a surface");
        return RoboNotFound;
    }
    if (surface != RoboCompositorSurfaceNone) {
        surfaceSlot = CompositorFindSurface(compositor, surface);
        if (!surfaceSlot || !surfaceSlot->surface) {
            CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainSurface,
                           "no such surface to point a layer at");
            return RoboNotFound;
        }
    } else {
        surfaceSlot = NULL;
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->surface = surface;
    if (surfaceSlot) {
        /* The pool's count of the layers pointing at a name is derived here rather than
         * maintained field by field: one place to be wrong is one place to fix. */
        surfaceSlot->layerRefs = CompositorCountLayerRefs(compositor, surface);
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerSetRect(Compositor *compositor,
                              CompositorLayerId id,
                              GfxRect rect,
                              CompositorDiag *diag)
{
    CompositorLayerSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainLayer,
                       "no such layer to size");
        return RoboNotFound;
    }
    if (rect.width < 0 || rect.height < 0) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainLayer,
                       "a layer's rectangle cannot have a negative extent");
        return RoboInvalidArgument;
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->rect = rect;
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerSetOffset(Compositor *compositor,
                                CompositorLayerId id,
                                float offsetX,
                                float offsetY,
                                CompositorDiag *diag)
{
    CompositorLayerSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    if (!CompositorValueIsFinite(offsetX) || !CompositorValueIsFinite(offsetY)) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainLayer,
                       "a layer's offset must be a finite number");
        return RoboInvalidArgument;
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->offsetX = offsetX;
    slot->offsetY = offsetY;
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerSetScale(Compositor *compositor,
                               CompositorLayerId id,
                               float scale,
                               CompositorDiag *diag)
{
    CompositorLayerSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    if (!(scale > 0.0f) || !CompositorValueIsFinite(scale)) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainLayer,
                       "a layer's scale must be positive and finite");
        return RoboInvalidArgument;
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->scale = scale;
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerSetOpacity(Compositor *compositor,
                                 CompositorLayerId id,
                                 uint8_t opacity,
                                 CompositorDiag *diag)
{
    CompositorLayerSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    /* Opacity is a byte, so there is nothing to validate: the whole range is legal. */
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->opacity = opacity;
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerSetClip(Compositor *compositor,
                              CompositorLayerId id,
                              GfxRect clip,
                              CompositorDiag *diag)
{
    CompositorLayerSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    if (clip.width < 0 || clip.height < 0) {
        CompositorFail(compositor, diag, RoboInvalidArgument, RoboCompositorDomainLayer,
                       "a clip cannot have a negative extent");
        return RoboInvalidArgument;
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->clip = clip;
    slot->hasClip = 1;
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

Result CompositorLayerClearClip(Compositor *compositor, CompositorLayerId id)
{
    CompositorLayerSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->hasClip = 0;
    slot->clip = GfxRectEmpty();
    CompositorInvalidateLayer(compositor, id, NULL);
    return RoboOk;
}

Result CompositorLayerSetVisible(Compositor *compositor,
                                 CompositorLayerId id,
                                 int visible,
                                 CompositorDiag *diag)
{
    CompositorLayerSlot *slot;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    CompositorInvalidateLayer(compositor, id, NULL);
    slot->visible = visible ? 1 : 0;
    CompositorInvalidateLayer(compositor, id, NULL);
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

Result CompositorLayerGetInfo(const Compositor *compositor, CompositorLayerId id,
                              CompositorLayerInfo *out)
{
    const CompositorLayerSlot *slot;
    unsigned children = 0u;
    CompositorLayerId child;

    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayerConst(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    for (child = slot->firstChild; child != RoboCompositorLayerNone;) {
        const CompositorLayerSlot *node = CompositorFindLayerConst(compositor, child);
        children++;
        if (!node) {
            break;
        }
        child = node->nextSibling;
    }
    MemSet(out, 0, sizeof(*out));
    out->id = slot->id;
    out->parent = slot->parent;
    out->surface = slot->surface;
    out->rect = slot->rect;
    out->offsetX = slot->offsetX;
    out->offsetY = slot->offsetY;
    out->scale = slot->scale;
    out->opacity = slot->opacity;
    out->clip = slot->clip;
    out->hasClip = slot->hasClip;
    out->visible = slot->visible;
    out->children = children;
    return RoboOk;
}

unsigned CompositorLayerCount(const Compositor *compositor)
{
    return compositor ? compositor->layerCount : 0u;
}

unsigned CompositorLayerChildCount(const Compositor *compositor, CompositorLayerId id)
{
    const CompositorLayerSlot *slot;
    CompositorLayerId child;
    unsigned count = 0u;

    if (!compositor) {
        return 0u;
    }
    if (id == RoboCompositorLayerNone) {
        child = compositor->firstRoot;
    } else {
        slot = CompositorFindLayerConst(compositor, id);
        if (!slot) {
            return 0u;
        }
        child = slot->firstChild;
    }
    while (child != RoboCompositorLayerNone) {
        const CompositorLayerSlot *node = CompositorFindLayerConst(compositor, child);
        count++;
        if (!node) {
            break;
        }
        child = node->nextSibling;
    }
    return count;
}

Result CompositorLayerChild(const Compositor *compositor,
                            CompositorLayerId id,
                            unsigned index,
                            CompositorLayerId *out)
{
    const CompositorLayerSlot *slot;
    CompositorLayerId child;
    unsigned i = 0u;

    if (out) {
        *out = RoboCompositorLayerNone;
    }
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    if (id == RoboCompositorLayerNone) {
        child = compositor->firstRoot;
    } else {
        slot = CompositorFindLayerConst(compositor, id);
        if (!slot) {
            return RoboNotFound;
        }
        child = slot->firstChild;
    }
    while (child != RoboCompositorLayerNone) {
        const CompositorLayerSlot *node = CompositorFindLayerConst(compositor, child);
        if (i == index) {
            *out = child;
            return RoboOk;
        }
        if (!node) {
            break;
        }
        child = node->nextSibling;
        i++;
    }
    return RoboNotFound;
}

/** Depth-first, parent first, children in creation order — the paint order, appended to @p out. */
static void PaintOrderWalk(const Compositor *compositor,
                           CompositorLayerId node,
                           CompositorLayerId *out,
                           size_t capacity,
                           size_t *count,
                           unsigned depth)
{
    const CompositorLayerSlot *slot;
    CompositorLayerId child;

    if (node == RoboCompositorLayerNone || depth > compositor->layerSlots) {
        return;
    }
    slot = CompositorFindLayerConst(compositor, node);
    if (!slot) {
        return;
    }
    if (*count < capacity && out) {
        out[*count] = node;
    }
    (*count)++;
    for (child = slot->firstChild; child != RoboCompositorLayerNone;) {
        const CompositorLayerSlot *next = CompositorFindLayerConst(compositor, child);
        PaintOrderWalk(compositor, child, out, capacity, count, depth + 1u);
        if (!next) {
            break;
        }
        child = next->nextSibling;
    }
}

Result CompositorLayerPaintOrder(const Compositor *compositor,
                                 CompositorLayerId *out,
                                 size_t capacity,
                                 size_t *outCount)
{
    CompositorLayerId root;
    size_t count = 0u;

    if (outCount) {
        *outCount = 0u;
    }
    if (!compositor) {
        return RoboInvalidArgument;
    }
    for (root = compositor->firstRoot; root != RoboCompositorLayerNone;) {
        const CompositorLayerSlot *node = CompositorFindLayerConst(compositor, root);
        PaintOrderWalk(compositor, root, out, capacity, &count, 0u);
        if (!node) {
            break;
        }
        root = node->nextSibling;
    }
    if (outCount) {
        *outCount = count;
    }
    return RoboOk;
}

Result CompositorLayerBoundsInCompositor(const Compositor *compositor,
                                         CompositorLayerId id,
                                         GfxRect *out)
{
    if (out) {
        *out = GfxRectEmpty();
    }
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    if (!CompositorFindLayerConst(compositor, id)) {
        return RoboNotFound;
    }
    *out = CompositorLayerLocalBounds(compositor, id);
    return RoboOk;
}

Result CompositorLayerHitTest(const Compositor *compositor, GfxPoint point, CompositorLayerId *out)
{
    CompositorLayerId order[RoboCompositorHitOrderCapacity];
    size_t count = 0u;
    size_t i;
    CompositorLayerId hit = RoboCompositorLayerNone;

    if (out) {
        *out = RoboCompositorLayerNone;
    }
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    CompositorLayerPaintOrder(compositor, order, RoboCompositorHitOrderCapacity, &count);
    for (i = 0u; i < count && i < RoboCompositorHitOrderCapacity; i++) {
        const CompositorLayerSlot *slot = CompositorFindLayerConst(compositor, order[i]);
        CompositorLayerGeometry geometry;
        GfxRect bounds;
        if (!slot || !slot->visible) {
            continue;
        }
        if (!CompositorResolveLayer(compositor, order[i], &geometry)) {
            continue;
        }
        if (geometry.hasClip && !GfxRectContainsPoint(geometry.clip, point)) {
            continue;
        }
        bounds = CompositorLayerLocalBounds(compositor, order[i]);
        if (!GfxRectIsEmpty(bounds) && GfxRectContainsPoint(bounds, point)) {
            /* Last in paint order is topmost, so the walk keeps going and the last match wins. */
            hit = order[i];
        }
    }
    if (hit == RoboCompositorLayerNone) {
        return RoboNotFound;
    }
    *out = hit;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Scrolling (15.11)                                                   */
/* ------------------------------------------------------------------ */

Result CompositorLayerSetScroll(Compositor *compositor,
                                CompositorLayerId id,
                                float offsetX,
                                float offsetY,
                                CompositorDiag *diag)
{
    CompositorLayerSlot *slot;
    GfxRect bounds;
    Result result;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayer(compositor, id);
    if (!slot) {
        CompositorFail(compositor, diag, RoboNotFound, RoboCompositorDomainScroll,
                       "no such layer to scroll");
        return RoboNotFound;
    }
    result = CompositorLayerSetOffset(compositor, id, offsetX, offsetY, diag);
    if (result != RoboOk) {
        return result;
    }
    /* A scroll moves content across the layer's whole box, so the whole box is stale — not the
     * distance the content travelled, which would leave a band of old pixels behind. */
    bounds = CompositorLayerLocalBounds(compositor, id);
    if (!GfxRectIsEmpty(bounds)) {
        CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, bounds, NULL);
    }
    CompositorPostFrame(compositor, RoboCompositorFrameReasonScroll, NULL);
    return RoboOk;
}

Result CompositorLayerGetScroll(const Compositor *compositor,
                                CompositorLayerId id,
                                float *outOffsetX,
                                float *outOffsetY)
{
    const CompositorLayerSlot *slot;

    if (outOffsetX) {
        *outOffsetX = 0.0f;
    }
    if (outOffsetY) {
        *outOffsetY = 0.0f;
    }
    if (!compositor || !outOffsetX || !outOffsetY) {
        return RoboInvalidArgument;
    }
    slot = CompositorFindLayerConst(compositor, id);
    if (!slot) {
        return RoboNotFound;
    }
    *outOffsetX = slot->offsetX;
    *outOffsetY = slot->offsetY;
    return RoboOk;
}
