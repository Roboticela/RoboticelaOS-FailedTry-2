/*
 * The composite pass — Phase 15.3, and the three rules the whole module is built on.
 *
 * ## 1. Paint order is the tree, depth-first
 *
 * A layer is painted, then its children in creation order. That is the same rule the document layer
 * uses, it is what `CompositorLayerPaintOrder` reports, and it is why a caller that wants A over B
 * creates A after B. There is no z-index in this module: a `z` is a number two callers can disagree
 * about, and an ordered child list is an order only one of them can state.
 *
 * ## 2. Opacity and clip multiply down the chain
 *
 * A layer's opacity is an ancestor's times its own (`GfxMulDiv255`, the same rounding the rest of the
 * tree uses), and a clip is the intersection of every clip from the root down. Both are computed on
 * the way in and handed to the recursion, so nothing is recomputed and no call site can forget one.
 *
 * ## 3. A layer's rectangle is where its surface's pixels go
 *
 * The rectangle *is* the destination: a layer whose rect is the surface's size draws it one to one,
 * and a rect that differs **samples** the surface to fill it. The sampling is nearest neighbour
 * (this build's `subpixelSampling` is 0), by integer arithmetic — `sx = (dx − x) * srcW / w` — which
 * is exact at 1:1 and at every integer magnification, and never reads outside the source.
 *
 * ## What the pass costs, and how it says so
 *
 * `CompositorComposeResult` counts layers painted, layers culled (invisible, or transparent, or
 * outside the region), layers drawn through a clip, and **pixels written**. The last one is the
 * frame's cost: a two-pixel damage presents two pixels, and a test can assert exactly that rather
 * than timing a frame.
 *
 * ## Format conversion is not a second blend
 *
 * A source and a destination in different formats are bridged through straight RGBA8 with
 * `GfxPixelUnpack`/`GfxPixelPack`, and the blend is `GfxCompositeSrcOverOpacity` — the one
 * source-over in the tree. There is no second blending path here, and no premultiplication the
 * compositor does for itself.
 */

#include "RoboCompositorInternal.h"

/** The composition-space rectangle a present region limits the pass to (NULL = all of it). */
static int RegionAllows(const GfxRect *region, GfxRect rect)
{
    return region ? GfxRectIntersects(*region, rect) : 1;
}

/** Clip @p dest to the accumulated clip, the region and the composition, or return 0. */
static int ResolveDestination(const Compositor *compositor,
                              GfxRect dest,
                              GfxRect clip,
                              int hasClip,
                              const GfxRect *region,
                              GfxRect *out)
{
    GfxRect current = dest;

    if (hasClip && !GfxRectIntersects(current, clip)) {
        return 0;
    }
    if (region && !GfxRectIntersects(current, *region)) {
        return 0;
    }
    if (!CompositorClipToComposition(compositor, current, &current)) {
        return 0;
    }
    if (hasClip) {
        GfxRect both;
        if (GfxRectIntersect(current, clip, &both) != RoboOk) {
            return 0;
        }
        current = both;
    }
    if (region) {
        GfxRect both;
        if (GfxRectIntersect(current, *region, &both) != RoboOk) {
            return 0;
        }
        current = both;
    }
    if (GfxRectIsEmpty(current)) {
        return 0;
    }
    *out = current;
    return 1;
}

/** One layer's surface into @p target, sampled to fill @p dest. Returns pixels written. */
static uint64_t BlitLayer(Compositor *compositor,
                          CompositorLayerId id,
                          CompositorLayerSlot *layer,
                          GfxRect dest,
                          uint8_t opacity,
                          GfxSurface *target)
{
    const CompositorSurfaceSlot *sourceSlot;
    GfxSurface *source;
    const uint8_t *srcPixels;
    uint8_t *dstPixels;
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
    size_t srcStride;
    size_t dstStride;
    uint8_t srcBpp;
    uint8_t dstBpp;
    GfxPixelFormat srcFormat;
    GfxPixelFormat dstFormat;
    int32_t dx;
    int32_t dy;
    uint64_t written = 0u;

    if (layer->surface == RoboCompositorSurfaceNone) {
        return 0u;
    }
    sourceSlot = CompositorFindSurfaceConst(compositor, layer->surface);
    if (!sourceSlot || !sourceSlot->surface) {
        return 0u;
    }
    source = sourceSlot->surface;
    if (source == target) {
        /* A composite whose source is its own destination is not a transform this build performs,
         * and reading what it is writing would make the result order-dependent. */
        return 0u;
    }
    srcPixels = GfxSurfacePixelsConst(source);
    dstPixels = GfxSurfacePixels(target);
    if (!srcPixels || !dstPixels) {
        return 0u;
    }
    srcWidth = GfxSurfaceWidth(source);
    srcHeight = GfxSurfaceHeight(source);
    dstWidth = GfxSurfaceWidth(target);
    dstHeight = GfxSurfaceHeight(target);
    if (srcWidth == 0u || srcHeight == 0u || dstWidth == 0u || dstHeight == 0u) {
        return 0u;
    }
    srcStride = GfxSurfaceStride(source);
    dstStride = GfxSurfaceStride(target);
    srcBpp = GfxSurfaceBytesPerPixel(source);
    dstBpp = GfxSurfaceBytesPerPixel(target);
    srcFormat = GfxSurfaceFormat(source);
    dstFormat = GfxSurfaceFormat(target);
    if (srcBpp == 0u || dstBpp == 0u || dest.width <= 0 || dest.height <= 0) {
        return 0u;
    }
    for (dy = dest.y; dy < dest.y + dest.height; dy++) {
        uint32_t sy;
        uint8_t *dstRow;
        if (dy < 0 || (uint32_t)dy >= dstHeight) {
            continue;
        }
        sy = (uint32_t)(((int64_t)(dy - dest.y) * (int64_t)srcHeight) / (int64_t)dest.height);
        if (sy >= srcHeight) {
            sy = srcHeight - 1u;
        }
        dstRow = dstPixels + (size_t)dy * dstStride;
        for (dx = dest.x; dx < dest.x + dest.width; dx++) {
            uint32_t sx;
            const uint8_t *srcPixel;
            uint8_t *dstPixel;
            GfxColorRgba8 srcColor;
            GfxColorRgba8 dstColor;
            GfxColorRgba8 blended;
            if (dx < 0 || (uint32_t)dx >= dstWidth) {
                continue;
            }
            sx = (uint32_t)(((int64_t)(dx - dest.x) * (int64_t)srcWidth) / (int64_t)dest.width);
            if (sx >= srcWidth) {
                sx = srcWidth - 1u;
            }
            srcPixel = srcPixels + (size_t)sy * srcStride + (size_t)sx * srcBpp;
            if (GfxPixelUnpack(srcFormat, srcPixel, srcBpp, &srcColor) != RoboOk) {
                continue;
            }
            dstPixel = dstRow + (size_t)dx * dstBpp;
            if (GfxPixelUnpack(dstFormat, dstPixel, dstBpp, &dstColor) != RoboOk) {
                continue;
            }
            blended = GfxCompositeSrcOverOpacity(srcColor, dstColor, opacity);
            if (GfxPixelPack(dstFormat, blended, dstPixel, dstBpp) != RoboOk) {
                continue;
            }
            written++;
        }
    }
    (void)id;
    return written;
}

static void ComposeNode(Compositor *compositor,
                        CompositorLayerId id,
                        float px,
                        float py,
                        float ps,
                        uint8_t opacity,
                        GfxRect clip,
                        int hasClip,
                        GfxSurface *target,
                        const GfxRect *region,
                        CompositorComposeResult *result,
                        unsigned depth)
{
    CompositorLayerSlot *layer;
    float boxX;
    float boxY;
    float childScale;
    uint8_t ownOpacity;
    GfxRect ownClip = clip;
    int ownHasClip = hasClip;
    GfxRect dest;
    GfxRect clipped;
    CompositorLayerId child;

    if (id == RoboCompositorLayerNone || depth > compositor->layerSlots) {
        return;
    }
    layer = CompositorFindLayer(compositor, id);
    if (!layer) {
        return;
    }
    if (!layer->visible) {
        /* Visibility is inherited: a hidden layer hides its subtree, and walking it would be work
         * whose pixels nobody can see. */
        result->layersCulled++;
        return;
    }
    if (layer->hasClip && !GfxRectIsEmpty(layer->clip)) {
        GfxRect own = MakeGfxRect(CompositorRound(px + (float)layer->clip.x * ps),
                                  CompositorRound(py + (float)layer->clip.y * ps),
                                  CompositorRound((float)layer->clip.width * ps),
                                  CompositorRound((float)layer->clip.height * ps));
        if (ownHasClip) {
            GfxRect both;
            if (GfxRectIntersect(ownClip, own, &both) == RoboOk) {
                ownClip = both;
            } else {
                ownClip = GfxRectEmpty();
            }
        } else {
            ownClip = own;
            ownHasClip = 1;
        }
    }
    ownOpacity = GfxMulDiv255(opacity, layer->opacity);
    if (ownOpacity == 0u) {
        result->layersCulled++;
        return;
    }
    boxX = px + (float)layer->rect.x * ps;
    boxY = py + (float)layer->rect.y * ps;
    childScale = ps * layer->scale;
    dest = MakeGfxRect(CompositorRound(boxX + layer->offsetX * ps),
                       CompositorRound(boxY + layer->offsetY * ps),
                       CompositorRound((float)layer->rect.width * childScale),
                       CompositorRound((float)layer->rect.height * childScale));
    if (layer->surface != RoboCompositorSurfaceNone) {
        if (layer->rect.width <= 0 || layer->rect.height <= 0) {
            result->layersCulled++;
        } else if (!RegionAllows(region, dest)) {
            result->layersCulled++;
        } else if (ResolveDestination(compositor, dest, ownClip, ownHasClip, region, &clipped)) {
            uint64_t pixels;
            if (ownHasClip) {
                result->layersClipped++;
            }
            pixels = BlitLayer(compositor, id, layer, clipped, ownOpacity, target);
            if (pixels > 0u) {
                result->layersPainted++;
                result->pixelsWritten += pixels;
            } else {
                /* No pixels means no readable source (or a self-composite): counted as culled rather
                 * than painted, so `layersPainted` never overstates what a frame did. */
                result->layersCulled++;
            }
        } else {
            result->layersClipped++;
        }
    }
    for (child = layer->firstChild; child != RoboCompositorLayerNone;) {
        CompositorLayerSlot *next = CompositorFindLayer(compositor, child);
        /* Children hang off the layer's **content** origin, so an offset scrolls what is inside as
         * well as the layer's own pixels — which is the whole point of a scroll offset. */
        ComposeNode(compositor, child, boxX + layer->offsetX * ps, boxY + layer->offsetY * ps,
                    childScale, ownOpacity, ownClip, ownHasClip, target, region, result, depth + 1u);
        if (!next) {
            break;
        }
        child = next->nextSibling;
    }
}

Result CompositorComposeTree(Compositor *compositor,
                             GfxSurface *target,
                             const GfxRect *region,
                             CompositorComposeResult *result,
                             CompositorDiag *diag)
{
    CompositorComposeResult local;
    CompositorSurfaceId root;

    if (!CompositorIsLive(compositor) || !target) {
        return RoboInvalidArgument;
    }
    MemSet(&local, 0, sizeof(local));
    for (root = compositor->firstRoot; root != RoboCompositorLayerNone;) {
        CompositorLayerSlot *node = CompositorFindLayer(compositor, root);
        local.regions++;
        ComposeNode(compositor, root, 0.0f, 0.0f, 1.0f, 255u, GfxRectEmpty(), 0, target, region,
                    &local, 0u);
        if (!node) {
            break;
        }
        root = node->nextSibling;
    }
    /* `regions` counts the roots walked, not the present's regions: the present's own count comes
     * from the damage list, and a root count would be a different fact under the same name. */
    local.regions = 0u;
    if (region) {
        local.regions = 1u;
    }
    if (result) {
        *result = local;
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}
