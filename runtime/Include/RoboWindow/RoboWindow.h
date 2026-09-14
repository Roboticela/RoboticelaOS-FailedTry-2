/*
 * RoboWindow — platform-independent windowing (Goals 7.1–7.4).
 *
 * Dependency:
 *   RoboBase → RoboMem / RoboError → RoboGraphics → RoboDisplay → RoboWindow
 *     → Native Window Interface → presentation backend → future host/QEMU
 *
 * Goal 7.4 makes framebuffer/surface presentation functional through a
 * mock backend. Still no real OS windows or QEMU display output.
 *
 * Chromium Ozone / PlatformWindow are temporary technical reference only.
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
#include "RoboWindow/RoboWindowLifecycle.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowPresent.h"
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

robo_result_t robo_window_request_close(robo_window_id_t id);
robo_result_t robo_window_destroy(robo_window_id_t id);

robo_result_t robo_window_get_state(robo_window_id_t id, robo_window_state_t *out);

/** Borrowed UTF-8 title; valid until title change / destroy. */
robo_result_t robo_window_get_title(robo_window_id_t id, const char **out_title);

/**
 * Set UTF-8 title (copied). Invalid UTF-8 → ROBO_PARSE_ERROR.
 * NULL title → empty string.
 */
robo_result_t robo_window_set_title(robo_window_id_t id, const char *utf8_title);

robo_result_t robo_window_request_size(robo_window_id_t id,
                                       int32_t width,
                                       int32_t height);

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

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_H */
