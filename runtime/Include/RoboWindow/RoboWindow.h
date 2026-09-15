/*
 * RoboWindow — platform-independent windowing (Goals 7.1–7.11).
 *
 * Dependency:
 *   RoboBase → RoboMem / RoboError → RoboGraphics → RoboDisplay → RoboWindow
 *     → Native Window Interface → host/mock/null backend
 *
 * Goal 7.10 keyboard; Goal 7.11 mouse foundation. Touch / stylus / pointer
 * unification and text-IME remain later goals.
 *
 * Chromium Ozone / PlatformWindow / KeyEvent / WebMouseEvent are temporary
 * technical reference only.
 *
 * See window/README.md and window/docs/.
 */

#ifndef ROBO_WINDOW_H
#define ROBO_WINDOW_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindowCapabilities.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowDisplay.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowLifecycle.h"
#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowPresent.h"
#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboWindow/RoboWindowSize.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowThread.h"
#include "RoboWindow/RoboWindowTypes.h"
#include "RoboWindow/RoboWindowVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_window_version_string(void);

robo_result_t robo_window_init(void);
void robo_window_shutdown(void);
int robo_window_is_initialized(void);

robo_result_t robo_window_create(const robo_window_config_t *cfg,
                                 robo_window_id_t *out_id);

robo_result_t robo_window_show(robo_window_id_t id);
robo_result_t robo_window_hide(robo_window_id_t id);

/**
 * Request close (CREATED → CLOSING). Does not destroy the native window.
 * Emits CLOSE_REQUESTED. Irreversible — no cancel API in Goal 7.7.
 * Already CLOSING → ROBO_ALREADY_EXISTS. Destroyed / unknown → ROBO_NOT_FOUND.
 */
robo_result_t robo_window_request_close(robo_window_id_t id);

/**
 * Apply an observed native close request (WM_CLOSE / mock inject).
 * CREATED → same as request_close. Already CLOSING → ROBO_OK (idempotent).
 * Does not destroy native resources.
 */
robo_result_t robo_window_observe_close_request(robo_window_id_t id);

/**
 * Destroy native resources and retire the window id (→ CLOSED).
 * Allowed from CREATED or CLOSING. Double destroy → ROBO_NOT_FOUND.
 * Native destroy failure leaves lifecycle unchanged.
 */
robo_result_t robo_window_destroy(robo_window_id_t id);

/** Copy lifecycle enumerator; requires a live window (CREATED or CLOSING). */
robo_result_t robo_window_get_lifecycle(robo_window_id_t id,
                                        robo_window_lifecycle_t *out);

/** Non-zero if lifecycle is CLOSING. */
int robo_window_is_closing(robo_window_id_t id);

robo_result_t robo_window_get_state(robo_window_id_t id, robo_window_state_t *out);

/** Borrowed UTF-8 title; valid until title change / destroy. */
robo_result_t robo_window_get_title(robo_window_id_t id, const char **out_title);

/**
 * Set UTF-8 title (copied). Invalid UTF-8 → ROBO_PARSE_ERROR.
 * NULL title → empty string.
 */
robo_result_t robo_window_set_title(robo_window_id_t id, const char *utf8_title);

/**
 * Request a new logical client size. Alias of the sizing entry point used by
 * Goal 7.6 tests (`set_size` / `get_size` naming).
 * Invalid / out-of-constraint → ROBO_INVALID_ARGUMENT (no silent clamp).
 * On backend failure, requested size is left unchanged.
 */
robo_result_t robo_window_set_size(robo_window_id_t id,
                                   int32_t width,
                                   int32_t height);

/** Same as robo_window_set_size (historical Goal 7.1 name). */
robo_result_t robo_window_request_size(robo_window_id_t id,
                                       int32_t width,
                                       int32_t height);

/** Actual logical size (syncs from backend when reports_actual_state). */
robo_result_t robo_window_get_size(robo_window_id_t id, robo_gfx_size_t *out);

/** Last successfully requested size (may differ from actual). */
robo_result_t robo_window_get_requested_size(robo_window_id_t id,
                                             robo_gfx_size_t *out);

robo_result_t robo_window_get_size_constraints(
    robo_window_id_t id,
    robo_window_size_constraints_t *out);

/**
 * Replace min/max constraints (0 = none). Rejects inconsistent sets.
 * If current actual size violates the new constraints, attempts a corrective
 * resize to the nearest valid size (deterministic).
 */
robo_result_t robo_window_set_size_constraints(
    robo_window_id_t id,
    const robo_window_size_constraints_t *constraints);

/**
 * Apply an observed native size (event-loop path). Updates actual size without
 * calling backend set_size. Does not change requested size or constraints.
 * Emits RESIZED when dimensions change. Skipped while applying geometry APIs
 * (avoids resize loops / duplicate events).
 */
robo_result_t robo_window_observe_size(robo_window_id_t id,
                                       int32_t width,
                                       int32_t height);

/**
 * Apply an observed native position. Negative coordinates allowed.
 * Emits MOVED when position changes. Does not call backend set_position.
 */
robo_result_t robo_window_observe_position(robo_window_id_t id,
                                           int32_t x,
                                           int32_t y);

/**
 * Apply an observed native visibility (shown=1 / hidden=0).
 * Only when the platform reports a real visibility change — do not fabricate.
 * Emits SHOWN / HIDDEN on change.
 */
robo_result_t robo_window_observe_visibility(robo_window_id_t id, int visible);

/**
 * Apply an observed native focus change (gained=1 / lost=0).
 * Emits FOCUS_GAINED / FOCUS_LOST on change. Not keyboard routing.
 */
robo_result_t robo_window_observe_focus(robo_window_id_t id, int focused);

robo_result_t robo_window_request_position(robo_window_id_t id,
                                           int32_t x,
                                           int32_t y);

/** Request focus via backend; may be ROBO_NOT_SUPPORTED. */
robo_result_t robo_window_request_focus(robo_window_id_t id);

/** Copy current native handle (may be empty for null backend). */
robo_result_t robo_window_get_native_handle(robo_window_id_t id,
                                            robo_window_native_handle_t *out);

int robo_window_id_is_live(robo_window_id_t id);
size_t robo_window_live_count(void);

/**
 * Non-blocking native drain (same as robo_window_event_loop_poll).
 * Nested while already pumping → ROBO_BUSY.
 */
robo_result_t robo_window_process_events(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_H */
