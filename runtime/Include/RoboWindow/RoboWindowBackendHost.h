/*
 * Host native window backend selection (Goal 7.5).
 *
 * Platform-independent declarations. On Windows host builds with
 * ROBO_WINDOW_HAS_WIN32=1, use_host() registers the Win32 backend.
 * Guest / unsupported hosts return ROBO_NOT_SUPPORTED.
 *
 * Never includes OS headers. Win32 lives in platform/win32/.
 */

#ifndef ROBO_WINDOW_BACKEND_HOST_H
#define ROBO_WINDOW_BACKEND_HOST_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowBackend.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Non-zero when a real native host window backend is linked and usable.
 * Guest freestanding builds and mock-only hosts return 0.
 */
int robo_window_backend_host_available(void);

/**
 * Register the real host backend (Win32 on Windows host builds).
 * Requires robo_window_init. Failure → ROBO_NOT_SUPPORTED / ROBO_INIT_ERROR /
 * native failure codes.
 */
robo_result_t robo_window_backend_use_host(void);

/** Vtable for the host backend, or NULL if unavailable. */
const robo_window_backend_vtbl_t *robo_window_backend_host_vtbl(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_BACKEND_HOST_H */
