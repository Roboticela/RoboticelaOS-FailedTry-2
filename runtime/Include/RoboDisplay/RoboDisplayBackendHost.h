/*
 * Host native display backend selection (Goal 7.13).
 *
 * Platform-independent declarations. Win32 host builds with
 * ROBO_WINDOW_HAS_WIN32=1 register EnumDisplayMonitors discovery.
 * Guest / unsupported hosts return ROBO_NOT_SUPPORTED.
 *
 * Never includes OS headers.
 */

#ifndef ROBO_DISPLAY_BACKEND_HOST_H
#define ROBO_DISPLAY_BACKEND_HOST_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplayBackend.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Non-zero when a real host display backend is linked and usable. */
int robo_display_backend_host_available(void);

/**
 * Register the host display backend (Win32 monitors on Windows host builds).
 * Requires robo_display_init.
 */
robo_result_t robo_display_backend_use_host(void);

/** Vtable for the host display backend, or NULL if unavailable. */
const robo_display_backend_vtbl_t *robo_display_backend_host_vtbl(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_BACKEND_HOST_H */
