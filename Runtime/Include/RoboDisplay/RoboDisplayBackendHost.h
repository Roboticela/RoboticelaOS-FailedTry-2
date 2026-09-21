/*
 * Host native display backend selection (Goal 7.13).
 *
 * Platform-independent declarations. Win32 host builds with
 * RoboWindowHasWin32=1 register EnumDisplayMonitors discovery.
 * Guest / unsupported hosts return RoboNotSupported.
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
int DisplayBackendHostAvailable(void);

/**
 * Register the host display backend (Win32 monitors on Windows host builds).
 * Requires DisplayInit.
 */
Result DisplayBackendUseHost(void);

/** Vtable for the host display backend, or NULL if unavailable. */
const DisplayBackendVtbl *DisplayBackendHostVtbl(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_BACKEND_HOST_H */
