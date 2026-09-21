/*
 * Host native window backend selection (Goal 7.5).
 *
 * Platform-independent declarations. On Windows host builds with
 * RoboWindowHasWin32=1, use_host() registers the Win32 backend.
 * Guest / unsupported hosts return RoboNotSupported.
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
int WindowBackendHostAvailable(void);

/**
 * Register the real host backend (Win32 on Windows host builds).
 * Requires WindowInit. Failure → RoboNotSupported / RoboInitError /
 * native failure codes.
 */
Result WindowBackendUseHost(void);

/** Vtable for the host backend, or NULL if unavailable. */
const WindowBackendVtbl *WindowBackendHostVtbl(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_BACKEND_HOST_H */
