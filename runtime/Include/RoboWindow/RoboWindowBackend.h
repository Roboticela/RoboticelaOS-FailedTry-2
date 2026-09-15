/*
 * RoboWindow backend contract (Goals 7.1 / 7.2).
 *
 * Abstract vtable for future native / QEMU / host backends.
 * Goal 7.4 defines synchronous surface presentation through this vtable;
 * null backend still creates no OS window and does not present.
 *
 * Chromium Ozone PlatformWindow is technical reference only — not a dependency.
 */

#ifndef ROBO_WINDOW_BACKEND_H
#define ROBO_WINDOW_BACKEND_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowCapabilities.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_gfx_surface robo_gfx_surface_t;

typedef struct robo_window_backend_vtbl {
    const char *name;
    /** Required non-NULL capability table for this backend. */
    const robo_window_backend_caps_t *capabilities;

    robo_result_t (*initialize)(void *userdata);
    void (*shutdown)(void *userdata);

    /**
     * Create native resources for @p id.
     * Null/mock backends may leave the handle empty. Host backends create a
     * real OS window and assign a valid opaque handle.
     */
    robo_result_t (*create_window)(void *userdata,
                                   robo_window_id_t id,
                                   const robo_window_config_t *cfg,
                                   robo_window_native_handle_t *out_native);

    /** Destroy native resources and invalidate @p native synchronously. */
    robo_result_t (*destroy_window)(void *userdata,
                                    robo_window_id_t id,
                                    robo_window_native_handle_t *native);

    robo_result_t (*show)(void *userdata,
                          robo_window_id_t id,
                          robo_window_native_handle_t *native);
    robo_result_t (*hide)(void *userdata,
                          robo_window_id_t id,
                          robo_window_native_handle_t *native);

    /** UTF-8 title; pointer valid only for the duration of the call. */
    robo_result_t (*set_title)(void *userdata,
                               robo_window_id_t id,
                               robo_window_native_handle_t *native,
                               const char *utf8_title);

    robo_result_t (*set_size)(void *userdata,
                              robo_window_id_t id,
                              robo_window_native_handle_t *native,
                              int32_t width,
                              int32_t height);

    /**
     * Apply min/max constraints (0 = none on that axis). Optional: NULL →
     * core keeps local constraints only; runtime set returns NOT_SUPPORTED
     * when caps.size_constraints is set without this entry.
     */
    robo_result_t (*set_size_constraints)(void *userdata,
                                          robo_window_id_t id,
                                          robo_window_native_handle_t *native,
                                          int32_t min_width,
                                          int32_t min_height,
                                          int32_t max_width,
                                          int32_t max_height);

    robo_result_t (*set_position)(void *userdata,
                                  robo_window_id_t id,
                                  robo_window_native_handle_t *native,
                                  int32_t x,
                                  int32_t y);

    robo_result_t (*focus)(void *userdata,
                           robo_window_id_t id,
                           robo_window_native_handle_t *native);

    /** Optional: update actual fields in @p inout_state. */
    robo_result_t (*query_state)(void *userdata,
                                 robo_window_id_t id,
                                 const robo_window_native_handle_t *native,
                                 robo_window_state_t *inout_state);

    /**
     * Synchronously present @p surface to @p native.
     * Caller keeps @p surface valid for the duration of this call only.
     * Do not retain the pointer after return. No async present in Goal 7.4.
     * Unsupported → ROBO_NOT_SUPPORTED; format mismatch → ROBO_NOT_SUPPORTED.
     */
    robo_result_t (*present_surface)(void *userdata,
                                     robo_window_id_t id,
                                     robo_window_native_handle_t *native,
                                     robo_gfx_surface_t *surface);

    /**
     * Non-blocking drain of the native message source (single batch).
     * Translate supported OS messages via observe_* — never expose OS types.
     */
    robo_result_t (*process_events)(void *userdata);

    /**
     * Optional (NULL allowed): block until native work, wakeup, or timeout.
     * @p timeout_ms == ROBO_WINDOW_EVENT_WAIT_FOREVER → wait indefinitely.
     * After return, caller typically calls process_events.
     */
    robo_result_t (*wait_events)(void *userdata, uint32_t timeout_ms);

    /**
     * Optional (NULL allowed): interrupt a blocked wait_events.
     * May be invoked to unblock shutdown / cross-thread wake (minimal).
     */
    robo_result_t (*wakeup)(void *userdata);

    /**
     * Optional (NULL allowed, Goal 7.11): capture native mouse input to @p
     * native. Soft capture still works without these entries.
     */
    robo_result_t (*capture_mouse)(void *userdata,
                                   robo_window_id_t id,
                                   robo_window_native_handle_t *native);
    robo_result_t (*release_mouse_capture)(void *userdata);
} robo_window_backend_vtbl_t;

/**
 * Register the single active backend. Replaces prior backend after shutdown.
 * Requires capabilities + core ops (including set_title, focus, present,
 * process_events). wait_events / wakeup are optional. initialize() runs
 * immediately.
 */
robo_result_t robo_window_backend_register(const robo_window_backend_vtbl_t *vtbl,
                                           void *userdata);

void robo_window_backend_clear(void);

int robo_window_backend_is_registered(void);

const char *robo_window_backend_name(void);

const robo_window_backend_vtbl_t *robo_window_backend_null_vtbl(void);

robo_result_t robo_window_backend_use_null(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_BACKEND_H */
