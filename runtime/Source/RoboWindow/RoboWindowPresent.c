#include "RoboWindow/RoboWindowPresent.h"

#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackend.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static robo_result_t validate_format_and_size(uint32_t width,
                                              uint32_t height,
                                              robo_gfx_pixel_format_t format,
                                              size_t stride_bytes,
                                              const robo_window_backend_caps_t *caps)
{
    robo_gfx_pixel_format_info_t info;
    robo_result_t r;
    size_t min_stride = 0u;

    if (width == 0u || height == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (width > ROBO_GFX_MAX_SURFACE_DIM || height > ROBO_GFX_MAX_SURFACE_DIM) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_gfx_pixel_format_info(format, &info);
    if (r != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!info.is_implemented) {
        return ROBO_NOT_SUPPORTED;
    }
    r = robo_gfx_pixel_format_validate_stride(format, width, stride_bytes);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_gfx_pixel_format_min_stride(format, width, &min_stride);
    if (r != ROBO_OK) {
        return r;
    }
    (void)min_stride;

    if (caps) {
        if (caps->present_format_mask != 0u) {
            if ((caps->present_format_mask &
                 ROBO_WINDOW_PRESENT_FMT_BIT(format)) == 0u) {
                return ROBO_NOT_SUPPORTED;
            }
        } else if (caps->present) {
            /* present enabled but empty mask → nothing presentable */
            return ROBO_NOT_SUPPORTED;
        }
        if (caps->present_max_width != 0u && width > caps->present_max_width) {
            return ROBO_NOT_SUPPORTED;
        }
        if (caps->present_max_height != 0u && height > caps->present_max_height) {
            return ROBO_NOT_SUPPORTED;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_window_present_validate_framebuffer(
    const robo_gfx_framebuffer_t *fb,
    const robo_window_backend_caps_t *caps)
{
    const uint8_t *pixels;

    if (!fb) {
        return ROBO_INVALID_ARGUMENT;
    }
    pixels = robo_gfx_framebuffer_pixels_const(fb);
    if (!pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    return validate_format_and_size(robo_gfx_framebuffer_width(fb),
                                    robo_gfx_framebuffer_height(fb),
                                    robo_gfx_framebuffer_format(fb),
                                    robo_gfx_framebuffer_stride(fb),
                                    caps);
}

robo_result_t robo_window_present_validate_surface(
    const robo_gfx_surface_t *surface,
    const robo_window_backend_caps_t *caps)
{
    const uint8_t *pixels;
    robo_gfx_framebuffer_t *fb;

    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Require readable CPU pixels for Goal 7.4 synchronous present. */
    pixels = robo_gfx_surface_pixels_const(surface);
    if (!pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    fb = robo_gfx_surface_framebuffer((robo_gfx_surface_t *)surface);
    if (!fb) {
        return ROBO_INVALID_ARGUMENT;
    }
    return validate_format_and_size(robo_gfx_surface_width(surface),
                                    robo_gfx_surface_height(surface),
                                    robo_gfx_surface_format(surface),
                                    robo_gfx_surface_stride(surface),
                                    caps);
}

int robo_window_present_format_supported(robo_gfx_pixel_format_t format)
{
    robo_window_backend_caps_t caps;

    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.present) {
        return 0;
    }
    if (caps.present_format_mask == 0u) {
        return 0;
    }
    return (caps.present_format_mask & ROBO_WINDOW_PRESENT_FMT_BIT(format)) != 0u;
}

robo_result_t robo_window_present_framebuffer(robo_window_id_t id,
                                              robo_gfx_framebuffer_t *fb)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surf = NULL;
    robo_window_backend_caps_t caps;
    robo_result_t r;

    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    r = robo_window_thread_require_owner();
    if (r != ROBO_OK) {
        return r;
    }
    if (!fb) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    if (robo_window_backend_capabilities(&caps) != ROBO_OK || !caps.present) {
        return ROBO_NOT_SUPPORTED;
    }
    r = robo_window_present_validate_framebuffer(fb, &caps);
    if (r != ROBO_OK) {
        return r;
    }

    robo_gfx_surface_desc_init(&desc);
    desc.width = robo_gfx_framebuffer_width(fb);
    desc.height = robo_gfx_framebuffer_height(fb);
    desc.format = robo_gfx_framebuffer_format(fb);
    desc.stride_bytes = robo_gfx_framebuffer_stride(fb);
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
    r = robo_gfx_surface_create_borrowed(
        &desc, (void *)robo_gfx_framebuffer_pixels(fb), &surf);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_present(id, surf);
    robo_gfx_surface_release(surf);
    return r;
}
