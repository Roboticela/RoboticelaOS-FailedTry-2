/*
 * Internal RoboWindow glue (Goals 7.1 / 7.2 / 7.14) — not a public API.
 */
#ifndef ROBO_WINDOW_INTERNAL_H
#define ROBO_WINDOW_INTERNAL_H

#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowTypes.h"

typedef struct robo_window_slot {
    int in_use;
    robo_window_id_t id;
    robo_window_state_t state;
    robo_window_config_t config_snapshot;
    char title[ROBO_WINDOW_TITLE_MAX];
    robo_window_native_handle_t native;
    /** Goal 7.4 borrowed present association (not retained). */
    robo_gfx_surface_t *present_surface;
    /** Goal 7.14 owned render surface (retained by window). */
    robo_gfx_surface_t *render_surface;
    int render_dirty;
    int render_dirty_full;
    robo_gfx_rect_t render_dirty_rect;
    int render_frame_open;
    int render_stale;
    robo_gfx_pixel_format_t render_format;
    /** Non-zero: state.scale came from config.scale_preference. */
    int scale_from_preference;
} robo_window_slot_t;

void robo_window_display_setup_placeholders(void);
void robo_window_display_reset_placeholders(void);

void robo_window_event_queue_reset(void);

/** Drop queued events for @p id (destroy path — keeps later CLOSED emission). */
void robo_window_event_purge_window(robo_window_id_t id);

void robo_window_event_loop_reset(void);

void robo_window_thread_reset(void);

/** First live window with state.focused, or ROBO_WINDOW_ID_NONE. */
robo_window_id_t robo_window_find_focused_id(void);

const robo_window_backend_vtbl_t *robo_window_backend_vtbl(void);
void *robo_window_backend_userdata(void);

robo_window_slot_t *robo_window_slot_find(robo_window_id_t id);

robo_result_t robo_window_require_ready(void);

robo_result_t robo_window_require_mutable_slot(robo_window_id_t id,
                                               robo_window_slot_t **out);

/** Mark owned render surface stale after resize / scale change. */
void robo_window_render_mark_stale(robo_window_slot_t *slot);

/** Release owned render surface + clear frame/dirty (destroy path). */
void robo_window_render_teardown(robo_window_slot_t *slot);

void robo_window_render_init_slot(robo_window_slot_t *slot);

#endif /* ROBO_WINDOW_INTERNAL_H */
