/*
 * RoboWindow owned render surface + frame lifecycle (Goal 7.14).
 */

#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowPresent.h"

#include <string.h>

#define ROBO_WINDOW_RENDER_MAX_EDGE 8192u

void robo_window_render_init_slot(robo_window_slot_t *slot)
{
    if (!slot) {
        return;
    }
    slot->render_surface = NULL;
    slot->render_dirty = 0;
    slot->render_dirty_full = 0;
    slot->render_dirty_rect = robo_gfx_rect(0, 0, 0, 0);
    slot->render_frame_open = 0;
    slot->render_stale = 0;
    slot->render_format = ROBO_GFX_FORMAT_BGRA8;
}

void robo_window_render_teardown(robo_window_slot_t *slot)
{
    if (!slot) {
        return;
    }
    if (slot->render_surface) {
        robo_gfx_surface_release(slot->render_surface);
        slot->render_surface = NULL;
    }
    slot->render_dirty = 0;
    slot->render_dirty_full = 0;
    slot->render_dirty_rect = robo_gfx_rect(0, 0, 0, 0);
    slot->render_frame_open = 0;
    slot->render_stale = 0;
}

void robo_window_render_mark_stale(robo_window_slot_t *slot)
{
    if (!slot) {
        return;
    }
    slot->render_stale = 1;
    /* Stale surface must not be presented; force recreate before draw. */
    if (slot->render_surface) {
        slot->render_dirty = 1;
        slot->render_dirty_full = 1;
    }
}

static int format_is_presentable(robo_gfx_pixel_format_t fmt)
{
    return fmt == ROBO_GFX_FORMAT_BGRA8 || fmt == ROBO_GFX_FORMAT_RGBA8 ||
           fmt == ROBO_GFX_FORMAT_RGB8;
}

static robo_result_t physical_dims(robo_window_id_t id, uint32_t *out_w,
                                   uint32_t *out_h)
{
    robo_gfx_size_t phys;
    robo_result_t r;

    r = robo_window_get_size_physical(id, &phys);
    if (r != ROBO_OK) {
        return r;
    }
    if (phys.width <= 0 || phys.height <= 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((uint32_t)phys.width > ROBO_WINDOW_RENDER_MAX_EDGE ||
        (uint32_t)phys.height > ROBO_WINDOW_RENDER_MAX_EDGE) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_w = (uint32_t)phys.width;
    *out_h = (uint32_t)phys.height;
    return ROBO_OK;
}

static int surface_matches(const robo_gfx_surface_t *surf, uint32_t w,
                           uint32_t h, robo_gfx_pixel_format_t fmt)
{
    if (!surf) {
        return 0;
    }
    return robo_gfx_surface_width(surf) == w &&
           robo_gfx_surface_height(surf) == h &&
           robo_gfx_surface_format(surf) == fmt;
}

static robo_result_t recreate_owned(robo_window_slot_t *slot, uint32_t w,
                                    uint32_t h)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surf = NULL;
    robo_result_t r;

    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = slot->render_format;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    r = robo_gfx_surface_create(&desc, &surf);
    if (r != ROBO_OK) {
        robo_log_error("robo_window.gfx: surface create %ux%u fmt=%d failed (%d)",
                       (unsigned)w, (unsigned)h, (int)slot->render_format,
                       (int)r);
        return r;
    }
    if (slot->render_surface) {
        robo_gfx_surface_release(slot->render_surface);
        slot->render_surface = NULL;
    }
    slot->render_surface = surf;
    slot->render_stale = 0;
    slot->render_dirty = 1;
    slot->render_dirty_full = 1;
    slot->render_dirty_rect = robo_gfx_rect(0, 0, (int32_t)w, (int32_t)h);
    robo_log_debug("robo_window.gfx: owned surface id=%llu %ux%u fmt=%d",
                   (unsigned long long)slot->id, (unsigned)w, (unsigned)h,
                   (int)slot->render_format);
    return ROBO_OK;
}

robo_result_t robo_window_ensure_render_surface(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    uint32_t w = 0u;
    uint32_t h = 0u;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (slot->render_frame_open) {
        /* Allow ensure during frame only if dimensions already match. */
    }
    r = physical_dims(id, &w, &h);
    if (r != ROBO_OK) {
        return r;
    }
    if (!slot->render_stale &&
        surface_matches(slot->render_surface, w, h, slot->render_format)) {
        return ROBO_OK;
    }
    if (slot->render_frame_open && slot->render_stale) {
        /* Recreate invalidates the caller's draw target mid-frame. */
        robo_log_warn("robo_window.gfx: recreate while frame open id=%llu",
                      (unsigned long long)id);
    }
    return recreate_owned(slot, w, h);
}

robo_gfx_surface_t *robo_window_get_render_surface(robo_window_id_t id)
{
    robo_window_slot_t *slot = robo_window_slot_find(id);

    if (!slot || !robo_window_id_is_live(id)) {
        return NULL;
    }
    return slot->render_surface;
}

robo_result_t robo_window_release_render_surface(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (slot->render_frame_open) {
        return ROBO_BUSY;
    }
    robo_window_render_teardown(slot);
    return ROBO_OK;
}

robo_result_t robo_window_set_render_format(robo_window_id_t id,
                                            robo_gfx_pixel_format_t format)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (!format_is_presentable(format)) {
        return ROBO_NOT_SUPPORTED;
    }
    if (slot->render_format == format) {
        return ROBO_OK;
    }
    slot->render_format = format;
    robo_window_render_mark_stale(slot);
    return ROBO_OK;
}

robo_result_t robo_window_get_render_format(robo_window_id_t id,
                                            robo_gfx_pixel_format_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = robo_window_slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    *out = slot->render_format;
    return ROBO_OK;
}

robo_result_t robo_window_get_render_size(robo_window_id_t id,
                                          robo_gfx_size_t *out)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    slot = robo_window_slot_find(id);
    if (!slot || !robo_window_id_is_live(id)) {
        return ROBO_NOT_FOUND;
    }
    if (!slot->render_surface) {
        out->width = 0;
        out->height = 0;
        return ROBO_OK;
    }
    out->width = (int32_t)robo_gfx_surface_width(slot->render_surface);
    out->height = (int32_t)robo_gfx_surface_height(slot->render_surface);
    return ROBO_OK;
}

robo_result_t robo_window_begin_frame(robo_window_id_t id,
                                      robo_gfx_surface_t **out_surface)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    if (!out_surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Leave *out_surface unchanged on failure (nested begin must not wipe). */
    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (slot->render_frame_open) {
        return ROBO_BUSY;
    }
    r = robo_window_ensure_render_surface(id);
    if (r != ROBO_OK) {
        return r;
    }
    if (!slot->render_surface) {
        return ROBO_IO_ERROR;
    }
    slot->render_frame_open = 1;
    *out_surface = slot->render_surface;
    robo_log_debug("robo_window.gfx: begin_frame id=%llu %ux%u",
                   (unsigned long long)id,
                   (unsigned)robo_gfx_surface_width(slot->render_surface),
                   (unsigned)robo_gfx_surface_height(slot->render_surface));
    return ROBO_OK;
}

robo_result_t robo_window_end_frame(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (!slot->render_frame_open) {
        return ROBO_INVALID_ARGUMENT;
    }
    slot->render_frame_open = 0;
    slot->render_dirty = 1;
    slot->render_dirty_full = 1;
    if (slot->render_surface) {
        slot->render_dirty_rect = robo_gfx_rect(
            0, 0, (int32_t)robo_gfx_surface_width(slot->render_surface),
            (int32_t)robo_gfx_surface_height(slot->render_surface));
    }
    robo_log_debug("robo_window.gfx: end_frame id=%llu dirty=1",
                   (unsigned long long)id);
    return ROBO_OK;
}

robo_result_t robo_window_present_frame(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (slot->render_frame_open) {
        return ROBO_BUSY;
    }
    if (!slot->render_dirty) {
        return ROBO_OK;
    }
    if (slot->render_stale || !slot->render_surface) {
        r = robo_window_ensure_render_surface(id);
        if (r != ROBO_OK) {
            return r;
        }
    }
    if (!slot->render_surface) {
        return ROBO_IO_ERROR;
    }
    r = robo_window_present(id, slot->render_surface);
    if (r != ROBO_OK) {
        return r;
    }
    slot->render_dirty = 0;
    slot->render_dirty_full = 0;
    slot->render_dirty_rect = robo_gfx_rect(0, 0, 0, 0);
    return ROBO_OK;
}

robo_result_t robo_window_invalidate(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    slot->render_dirty = 1;
    slot->render_dirty_full = 1;
    if (slot->render_surface) {
        slot->render_dirty_rect = robo_gfx_rect(
            0, 0, (int32_t)robo_gfx_surface_width(slot->render_surface),
            (int32_t)robo_gfx_surface_height(slot->render_surface));
    }
    return ROBO_OK;
}

robo_result_t robo_window_invalidate_rect(robo_window_id_t id,
                                          robo_gfx_rect_t rect)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    if (rect.width <= 0 || rect.height <= 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    slot->render_dirty = 1;
    /* Simple model: any region dirties the full surface for present. */
    slot->render_dirty_full = 1;
    slot->render_dirty_rect = rect;
    (void)slot->render_dirty_rect;
    return ROBO_OK;
}

int robo_window_is_dirty(robo_window_id_t id)
{
    robo_window_slot_t *slot = robo_window_slot_find(id);

    if (!slot || !robo_window_id_is_live(id)) {
        return 0;
    }
    return slot->render_dirty ? 1 : 0;
}

robo_result_t robo_window_clear_dirty(robo_window_id_t id)
{
    robo_window_slot_t *slot;
    robo_result_t r;

    r = robo_window_require_ready();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_require_mutable_slot(id, &slot);
    if (r != ROBO_OK) {
        return r;
    }
    slot->render_dirty = 0;
    slot->render_dirty_full = 0;
    slot->render_dirty_rect = robo_gfx_rect(0, 0, 0, 0);
    return ROBO_OK;
}

int robo_window_frame_is_open(robo_window_id_t id)
{
    robo_window_slot_t *slot = robo_window_slot_find(id);

    if (!slot || !robo_window_id_is_live(id)) {
        return 0;
    }
    return slot->render_frame_open ? 1 : 0;
}
