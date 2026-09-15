/*
 * RoboDisplay — platform-independent display manager (Goal 7.3).
 *
 * Dependency:
 *   RoboBase → RoboGraphics geometry → RoboDisplay → future OS backends
 *
 * Independent from RoboWindow. Windows may query/associate displays via
 * RoboWindow helpers without owning the display backend.
 *
 * Chromium ui/display is temporary technical reference only.
 */

#ifndef ROBO_DISPLAY_H
#define ROBO_DISPLAY_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplayBackend.h"
#include "RoboDisplay/RoboDisplayCapabilities.h"
#include "RoboDisplay/RoboDisplayInfo.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboDisplay/RoboDisplayTypes.h"
#include "RoboDisplay/RoboDisplayVersion.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_display_version_string(void);

robo_result_t robo_display_init(void);
void robo_display_shutdown(void);
int robo_display_is_initialized(void);

/** Re-enumerate from the active backend into the manager cache. */
robo_result_t robo_display_refresh(void);

size_t robo_display_count(void);

robo_result_t robo_display_at(size_t index, robo_display_info_t *out);

robo_result_t robo_display_get(robo_display_id_t id, robo_display_info_t *out);

/** Primary connected display, or ROBO_NOT_FOUND. */
robo_result_t robo_display_primary(robo_display_info_t *out);

/** Non-zero if @p id is present in the current cache. */
int robo_display_id_is_known(robo_display_id_t id);

/**
 * Current modes for @p id. No modes capability → ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_display_get_modes(robo_display_id_t id,
                                     robo_display_mode_t *out,
                                     size_t capacity,
                                     size_t *out_count);

/**
 * Effective scale for @p id (Goal 7.8).
 * Uses info.scale, else derives from dpi_x. Unavailable → ROBO_NOT_FOUND.
 */
robo_result_t robo_display_get_scale(robo_display_id_t id, float *out_scale);

/**
 * DPI pair for @p id. Axes may be individually unavailable (0).
 * Unknown display → ROBO_NOT_FOUND. Corrupt stored DPI → INVALID_ARGUMENT.
 */
robo_result_t robo_display_get_dpi(robo_display_id_t id,
                                   robo_display_dpi_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_H */
