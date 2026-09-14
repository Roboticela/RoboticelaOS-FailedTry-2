/*
 * RoboGraphics Goal 6.14 — surface/framebuffer capture + 6.13 export.
 */

#include "RoboGraphics/RoboGraphicsScreenshot.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_gfx_screenshot {
    uint32_t refcount;
    robo_gfx_surface_t *surface; /* owned snapshot */
};

static robo_result_t resolve_region(const robo_gfx_rect_t *bounds,
                                    const robo_gfx_capture_options_t *opts,
                                    robo_gfx_rect_t *out_region)
{
    robo_gfx_rect_t requested;
    robo_gfx_rect_t clipped;
    robo_result_t res;

    if (!bounds || !opts || !out_region) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (opts->use_full_extent) {
        *out_region = *bounds;
        if (robo_gfx_rect_is_empty(*out_region)) {
            return ROBO_INVALID_ARGUMENT;
        }
        return ROBO_OK;
    }

    requested = opts->region;
    if (!robo_gfx_rect_is_valid(requested)) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (opts->region_policy == ROBO_GFX_CAPTURE_REGION_STRICT) {
        if (robo_gfx_rect_is_empty(requested)) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (!robo_gfx_rect_encloses(*bounds, requested)) {
            return ROBO_INVALID_ARGUMENT;
        }
        *out_region = requested;
        return ROBO_OK;
    }

    if (opts->region_policy != ROBO_GFX_CAPTURE_REGION_CLIP) {
        return ROBO_INVALID_ARGUMENT;
    }

    res = robo_gfx_rect_clip(requested, *bounds, &clipped);
    if (res != ROBO_OK) {
        return res;
    }
    if (robo_gfx_rect_is_empty(clipped)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_region = clipped;
    return ROBO_OK;
}

static robo_result_t copy_rect_to_owned_surface(
    const robo_gfx_surface_t *src,
    robo_gfx_rect_t region,
    robo_gfx_surface_t **out_snap)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *snap = NULL;
    robo_result_t res;

    if (!src || !out_snap) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_snap = NULL;

    if (robo_gfx_rect_is_empty(region) || region.width < 0 ||
        region.height < 0 || region.x < 0 || region.y < 0) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_gfx_surface_desc_init(&desc);
    desc.width = (uint32_t)region.width;
    desc.height = (uint32_t)region.height;
    desc.format = robo_gfx_surface_format(src);
    desc.stride_bytes = 0u;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;

    res = robo_gfx_surface_create(&desc, &snap);
    if (res != ROBO_OK) {
        return res;
    }

    res = robo_gfx_surface_copy_rect(src, region, snap, robo_gfx_point(0, 0));
    if (res != ROBO_OK) {
        robo_gfx_surface_release(snap);
        return res;
    }

    *out_snap = snap;
    return ROBO_OK;
}

static robo_result_t wrap_framebuffer_as_surface(
    const robo_gfx_framebuffer_t *fb,
    robo_gfx_surface_t **out)
{
    robo_gfx_surface_desc_t desc;
    const uint8_t *pixels;

    if (!fb || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    pixels = robo_gfx_framebuffer_pixels_const(fb);
    if (!pixels) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_gfx_surface_desc_init(&desc);
    desc.width = robo_gfx_framebuffer_width(fb);
    desc.height = robo_gfx_framebuffer_height(fb);
    desc.format = robo_gfx_framebuffer_format(fb);
    desc.stride_bytes = robo_gfx_framebuffer_stride(fb);
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;

    /* Borrowed view is read-only for the capture copy; storage lifetime = fb. */
    return robo_gfx_surface_create_borrowed(&desc, (void *)pixels, out);
}

static robo_result_t make_screenshot(robo_gfx_surface_t *snap,
                                     robo_gfx_screenshot_t **out)
{
    robo_gfx_screenshot_t *shot;

    if (!snap || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    shot = (robo_gfx_screenshot_t *)robo_calloc(1, sizeof(*shot));
    if (!shot) {
        return ROBO_OUT_OF_MEMORY;
    }
    shot->refcount = 1u;
    shot->surface = snap;
    *out = shot;
    return ROBO_OK;
}

static robo_result_t capture_from_surface(const robo_gfx_surface_t *surface,
                                          const robo_gfx_capture_options_t *opts,
                                          robo_gfx_screenshot_t **out)
{
    robo_gfx_rect_t bounds;
    robo_gfx_rect_t region;
    robo_gfx_surface_t *snap = NULL;
    robo_result_t res;
    robo_gfx_capture_options_t local;

    if (!surface || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    if (!opts) {
        robo_gfx_capture_options_init(&local);
        opts = &local;
    }

    if (!robo_gfx_pixel_format_is_implemented(
            robo_gfx_surface_format(surface))) {
        return ROBO_NOT_SUPPORTED;
    }
    if (robo_gfx_surface_width(surface) == 0u ||
        robo_gfx_surface_height(surface) == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_surface_pixels_const(surface)) {
        return ROBO_INVALID_ARGUMENT;
    }

    res = robo_gfx_surface_pixel_bounds(surface, &bounds);
    if (res != ROBO_OK) {
        return res;
    }
    res = resolve_region(&bounds, opts, &region);
    if (res != ROBO_OK) {
        return res;
    }
    res = copy_rect_to_owned_surface(surface, region, &snap);
    if (res != ROBO_OK) {
        return res;
    }
    res = make_screenshot(snap, out);
    if (res != ROBO_OK) {
        robo_gfx_surface_release(snap);
    }
    return res;
}

void robo_gfx_capture_options_init(robo_gfx_capture_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->region_policy = ROBO_GFX_CAPTURE_REGION_CLIP;
    opts->use_full_extent = 1;
    opts->region = robo_gfx_rect(0, 0, 0, 0);
}

robo_result_t robo_gfx_screenshot_capture_surface(
    const robo_gfx_surface_t *surface,
    const robo_gfx_capture_options_t *opts,
    robo_gfx_screenshot_t **out)
{
    return capture_from_surface(surface, opts, out);
}

robo_result_t robo_gfx_screenshot_capture_framebuffer(
    const robo_gfx_framebuffer_t *framebuffer,
    const robo_gfx_capture_options_t *opts,
    robo_gfx_screenshot_t **out)
{
    robo_gfx_surface_t *view = NULL;
    robo_result_t res;

    if (!framebuffer || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    if (!robo_gfx_pixel_format_is_implemented(
            robo_gfx_framebuffer_format(framebuffer))) {
        return ROBO_NOT_SUPPORTED;
    }
    if (robo_gfx_framebuffer_width(framebuffer) == 0u ||
        robo_gfx_framebuffer_height(framebuffer) == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    res = wrap_framebuffer_as_surface(framebuffer, &view);
    if (res != ROBO_OK) {
        return res;
    }
    res = capture_from_surface(view, opts, out);
    robo_gfx_surface_release(view);
    return res;
}

robo_result_t robo_gfx_screenshot_capture(
    robo_gfx_capture_source_t source,
    const robo_gfx_surface_t *surface,
    const robo_gfx_framebuffer_t *framebuffer,
    const robo_gfx_capture_options_t *opts,
    robo_gfx_screenshot_t **out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    switch (source) {
    case ROBO_GFX_CAPTURE_SURFACE:
        if (!surface) {
            return ROBO_INVALID_ARGUMENT;
        }
        return robo_gfx_screenshot_capture_surface(surface, opts, out);
    case ROBO_GFX_CAPTURE_FRAMEBUFFER:
        if (!framebuffer) {
            return ROBO_INVALID_ARGUMENT;
        }
        return robo_gfx_screenshot_capture_framebuffer(framebuffer, opts, out);
    case ROBO_GFX_CAPTURE_FRONT_BUFFER:
    case ROBO_GFX_CAPTURE_BACK_BUFFER:
        /* No present/swap-chain buffers in Goal 6.14. */
        (void)opts;
        return ROBO_NOT_SUPPORTED;
    default:
        return ROBO_INVALID_ARGUMENT;
    }
}

void robo_gfx_screenshot_retain(robo_gfx_screenshot_t *shot)
{
    if (!shot) {
        return;
    }
    shot->refcount++;
}

void robo_gfx_screenshot_release(robo_gfx_screenshot_t *shot)
{
    if (!shot) {
        return;
    }
    if (shot->refcount == 0u) {
        return;
    }
    shot->refcount--;
    if (shot->refcount != 0u) {
        return;
    }
    if (shot->surface) {
        robo_gfx_surface_release(shot->surface);
    }
    robo_free(shot);
}

uint32_t robo_gfx_screenshot_width(const robo_gfx_screenshot_t *shot)
{
    return shot && shot->surface ? robo_gfx_surface_width(shot->surface) : 0u;
}

uint32_t robo_gfx_screenshot_height(const robo_gfx_screenshot_t *shot)
{
    return shot && shot->surface ? robo_gfx_surface_height(shot->surface) : 0u;
}

robo_gfx_pixel_format_t robo_gfx_screenshot_format(
    const robo_gfx_screenshot_t *shot)
{
    return shot && shot->surface ? robo_gfx_surface_format(shot->surface)
                                 : ROBO_GFX_FORMAT_UNKNOWN;
}

robo_gfx_surface_t *robo_gfx_screenshot_surface(robo_gfx_screenshot_t *shot)
{
    return shot ? shot->surface : NULL;
}

robo_result_t robo_gfx_screenshot_to_image(robo_gfx_screenshot_t *shot,
                                           robo_gfx_image_t **out)
{
    if (!shot || !shot->surface || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_image_create_from_surface(shot->surface, out);
}

robo_result_t robo_gfx_screenshot_export(
    const robo_gfx_screenshot_t *shot,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encoded_buffer_t *out)
{
    if (!shot || !shot->surface || !encode_opts || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_surface_encode(shot->surface, encode_opts, out);
}

robo_result_t robo_gfx_screenshot_export_to_sink(
    const robo_gfx_screenshot_t *shot,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encode_sink_fn sink,
    void *user)
{
    robo_gfx_rect_t bounds;
    robo_result_t res;

    if (!shot || !shot->surface || !encode_opts || !sink) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_surface_pixel_bounds(shot->surface, &bounds);
    if (res != ROBO_OK) {
        return res;
    }
    return robo_gfx_surface_encode_to_sink(shot->surface, bounds, encode_opts,
                                           sink, user);
}

robo_result_t robo_gfx_surface_export_screenshot(
    const robo_gfx_surface_t *surface,
    const robo_gfx_capture_options_t *capture_opts,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encoded_buffer_t *out)
{
    robo_gfx_screenshot_t *shot = NULL;
    robo_result_t res;

    if (!encode_opts || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->data = NULL;
    out->size = 0u;
    res = robo_gfx_screenshot_capture_surface(surface, capture_opts, &shot);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_gfx_screenshot_export(shot, encode_opts, out);
    robo_gfx_screenshot_release(shot);
    return res;
}

robo_result_t robo_gfx_framebuffer_export_screenshot(
    const robo_gfx_framebuffer_t *framebuffer,
    const robo_gfx_capture_options_t *capture_opts,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encoded_buffer_t *out)
{
    robo_gfx_screenshot_t *shot = NULL;
    robo_result_t res;

    if (!encode_opts || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->data = NULL;
    out->size = 0u;
    res = robo_gfx_screenshot_capture_framebuffer(framebuffer, capture_opts,
                                                  &shot);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_gfx_screenshot_export(shot, encode_opts, out);
    robo_gfx_screenshot_release(shot);
    return res;
}
