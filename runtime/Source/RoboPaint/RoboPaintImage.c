/*
 * RoboPaint image payload (Phase 11.7) — see RoboPaintImage.h.
 *
 * Four short functions and one rule, and the rule is the reason they are here rather than
 * spread across the four call sites that need it: **a payload names a resource, and the
 * container that stores it holds a reference.** `robo_paint_image_retain` is that sentence
 * with a function around it, so the paint list, the display list and the builder cannot
 * each have remembered the ownership discipline slightly differently.
 *
 * Nothing here reads a document, allocates, or derives a rectangle. The rectangle comes
 * from layout, the pixels come from RoboGraphics, and this file only decides which
 * combinations of the two are a drawing this build may attempt.
 */

#include "RoboPaint/RoboPaintImage.h"

#include <string.h>

void robo_paint_image_init(robo_paint_image_t *image)
{
    if (!image) {
        return;
    }
    memset(image, 0, sizeof(*image));
    /* `UNKNOWN` is RoboGraphics' own "no format", which is what a payload that names no
     * resource has: not a zero that a reader could mistake for a real format. */
    image->format = ROBO_GFX_FORMAT_UNKNOWN;
    image->source = robo_gfx_rect(0, 0, 0, 0);
}

int robo_paint_image_is_cleared(const robo_paint_image_t *image)
{
    if (!image) {
        return 1;
    }
    return image->resource == NULL;
}

int robo_paint_image_is_full_source(const robo_paint_image_t *image)
{
    if (!image || !image->resource) {
        return 0;
    }
    return image->source.x == 0 && image->source.y == 0 &&
           image->source.width > 0 && image->source.height > 0 &&
           (uint32_t)image->source.width == image->width &&
           (uint32_t)image->source.height == image->height;
}

int robo_paint_image_is_valid(const robo_paint_image_t *image)
{
    if (!image || !image->resource) {
        return 0;
    }
    /*
     * The surface is what a rasteriser will sample, so a resource without one is refused
     * here rather than at the draw: a metadata-only image is a legitimate RoboGraphics
     * object and an impossible drawing, and the record layer is the right place to say so.
     */
    if (!robo_gfx_image_surface(image->resource)) {
        return 0;
    }
    if (image->width == 0u || image->height == 0u) {
        return 0;
    }
    if (!robo_gfx_rect_is_valid(image->source) || image->source.width <= 0 ||
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

int robo_paint_image_equal(const robo_paint_image_t *a, const robo_paint_image_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (robo_paint_image_is_cleared(a) || robo_paint_image_is_cleared(b)) {
        return robo_paint_image_is_cleared(a) && robo_paint_image_is_cleared(b);
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
           robo_gfx_rect_equal(a->source, b->source);
}

void robo_paint_image_retain(robo_paint_image_t *image)
{
    if (!image || !image->resource) {
        return;
    }
    robo_gfx_image_retain(image->resource);
}

void robo_paint_image_release(robo_paint_image_t *image)
{
    if (!image || !image->resource) {
        return;
    }
    robo_gfx_image_release(image->resource);
}

robo_result_t robo_paint_image_from_resource(robo_gfx_image_t *resource,
                                             robo_paint_image_t *out,
                                             robo_paint_diag_t *diag)
{
    uint32_t width;
    uint32_t height;

    if (!resource || !out) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_INVALID_ARGUMENT,
                               "a resource and an output record are required");
    }
    if (!robo_gfx_image_surface(resource)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_NOT_SUPPORTED,
                               "the image resource holds no pixels");
    }
    width = robo_gfx_image_width(resource);
    height = robo_gfx_image_height(resource);
    if (width == 0u || height == 0u || width > ROBO_PAINT_IMAGE_MAX_DIMENSION ||
        height > ROBO_PAINT_IMAGE_MAX_DIMENSION) {
        /*
         * A zero-sized resource is a decode outcome and a resource above the bound is one
         * this build will not carry into a record. Both are refused here rather than
         * clamped: a record that quietly resized an image would be a drawing of something
         * nobody asked for, and the traversal counts the refusal instead.
         */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_TRAVERSAL, ROBO_NOT_SUPPORTED,
                               "the image resource's dimensions are not usable");
    }

    robo_paint_image_init(out);
    out->resource = resource;
    out->identity = robo_gfx_image_id(resource);
    out->width = width;
    out->height = height;
    out->format = robo_gfx_image_format(resource);
    out->source = robo_gfx_rect(0, 0, (int32_t)width, (int32_t)height);
    return ROBO_OK;
}

robo_gfx_image_id_t robo_paint_image_identity(const robo_paint_image_t *image)
{
    return image ? image->identity : (robo_gfx_image_id_t)0;
}
