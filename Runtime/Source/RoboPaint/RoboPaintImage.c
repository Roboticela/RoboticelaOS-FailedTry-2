/*
 * RoboPaint image payload (Phase 11.7) — see RoboPaintImage.h.
 *
 * Four short functions and one rule, and the rule is the reason they are here rather than
 * spread across the four call sites that need it: **a payload names a resource, and the
 * container that stores it holds a reference.** `PaintImageRetain` is that sentence
 * with a function around it, so the paint list, the display list and the builder cannot
 * each have remembered the ownership discipline slightly differently.
 *
 * Nothing here reads a document, allocates, or derives a rectangle. The rectangle comes
 * from layout, the pixels come from RoboGraphics, and this file only decides which
 * combinations of the two are a drawing this build may attempt.
 */

#include "RoboPaint/RoboPaintImage.h"

#include <string.h>

void PaintImageInit(PaintImage *image)
{
    if (!image) {
        return;
    }
    memset(image, 0, sizeof(*image));
    /* `UNKNOWN` is RoboGraphics' own "no format", which is what a payload that names no
     * resource has: not a zero that a reader could mistake for a real format. */
    image->format = RoboGfxFormatUnknown;
    image->source = MakeGfxRect(0, 0, 0, 0);
}

int PaintImageIsCleared(const PaintImage *image)
{
    if (!image) {
        return 1;
    }
    return image->resource == NULL;
}

int PaintImageIsFullSource(const PaintImage *image)
{
    if (!image || !image->resource) {
        return 0;
    }
    return image->source.x == 0 && image->source.y == 0 &&
           image->source.width > 0 && image->source.height > 0 &&
           (uint32_t)image->source.width == image->width &&
           (uint32_t)image->source.height == image->height;
}

int PaintImageIsValid(const PaintImage *image)
{
    if (!image || !image->resource) {
        return 0;
    }
    /*
     * The surface is what a rasteriser will sample, so a resource without one is refused
     * here rather than at the draw: a metadata-only image is a legitimate RoboGraphics
     * object and an impossible drawing, and the record layer is the right place to say so.
     */
    if (!GfxImageSurface(image->resource)) {
        return 0;
    }
    if (image->width == 0u || image->height == 0u) {
        return 0;
    }
    if (!GfxRectIsValid(image->source) || image->source.width <= 0 ||
        image->source.height <= 0) {
        return 0;
    }
    /*
     * The source has to be **inside** the resource, checked in 64 bits for the reason every
     * sum in this module is: two bounded numbers have an unbounded sum, and a comparison
     * that wrapped would admit a crop running off the image.
     */
    if (image->source.x < 0 || image->source.y < 0 ||
        (int64_t)image->source.x + (int64_t)image->source.width > (int64_t)image->width ||
        (int64_t)image->source.y + (int64_t)image->source.height > (int64_t)image->height) {
        return 0;
    }
    return 1;
}

int PaintImageEqual(const PaintImage *a, const PaintImage *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (PaintImageIsCleared(a) || PaintImageIsCleared(b)) {
        return PaintImageIsCleared(a) && PaintImageIsCleared(b);
    }
    /*
     * The resource is compared **by identity**, and the header says why in full: an address
     * is a fact about this process's heap rather than about the drawing, and a comparison
     * that used it would make two runs of one document disagree. The dimensions and the
     * format are compared as well, because two records can name the same identity only by
     * naming the same resource -- and a *hand-built* record can claim any identity it likes,
     * which is exactly the case a suite exercises.
     */
    return a->identity == b->identity && a->width == b->width && a->height == b->height &&
           a->format == b->format &&
           GfxRectEqual(a->source, b->source);
}

void PaintImageRetain(PaintImage *image)
{
    if (!image || !image->resource) {
        return;
    }
    GfxImageRetain(image->resource);
}

void PaintImageRelease(PaintImage *image)
{
    if (!image || !image->resource) {
        return;
    }
    GfxImageRelease(image->resource);
}

Result PaintImageFromResource(GfxImage *resource,
                                             PaintImage *out,
                                             PaintDiag *diag)
{
    uint32_t width;
    uint32_t height;

    if (!resource || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "a resource and an output record are required");
    }
    if (!GfxImageSurface(resource)) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboNotSupported,
                               "the image resource holds no pixels");
    }
    width = GfxImageWidth(resource);
    height = GfxImageHeight(resource);
    if (width == 0u || height == 0u || width > RoboPaintImageMaxDimension ||
        height > RoboPaintImageMaxDimension) {
        /*
         * A zero-sized resource is a decode outcome and a resource above the bound is one
         * this build will not carry into a record. Both are refused here rather than
         * clamped: a record that quietly resized an image would be a drawing of something
         * nobody asked for, and the traversal counts the refusal instead.
         */
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboNotSupported,
                               "the image resource's dimensions are not usable");
    }

    PaintImageInit(out);
    out->resource = resource;
    out->identity = GetGfxImageId(resource);
    out->width = width;
    out->height = height;
    out->format = GfxImageFormat(resource);
    out->source = MakeGfxRect(0, 0, (int32_t)width, (int32_t)height);
    return RoboOk;
}

GfxImageId PaintImageIdentity(const PaintImage *image)
{
    return image ? image->identity : (GfxImageId)0;
}
