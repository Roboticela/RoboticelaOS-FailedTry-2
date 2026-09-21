/*
 * RoboDisplay QEMU backend (Goal 7.12).
 *
 * Guest: metadata from SYS_DISPLAY INFO (UEFI GOP via kernel).
 * Host: optional in-memory simulator for unit tests.
 */

#ifndef ROBO_DISPLAY_BACKEND_QEMU_H
#define ROBO_DISPLAY_BACKEND_QEMU_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplayBackend.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Shared FB info used by display + window QEMU backends. */
const QemuFbInfo *QemuFbCurrent(void);

/**
 * Install / replace the active QEMU FB descriptor (copied).
 * Used by the window backend after claim/info or by the host sim.
 */
Result QemuFbSetCurrent(const QemuFbInfo *info);

void QemuFbClearCurrent(void);

const DisplayBackendVtbl *DisplayBackendQemuVtbl(void);

/**
 * Register QEMU display backend (enumerates from QemuFbCurrent).
 * Call after FB metadata is known (or use sim helpers first).
 */
Result DisplayBackendUseQemu(void);

/**
 * Host/unit-test simulator: allocate a CPU framebuffer and register backends.
 * Does not require QEMU or SYS_DISPLAY.
 */
Result QemuSimInit(uint32_t width, uint32_t height,
                                 uint32_t pixfmt);
void QemuSimShutdown(void);
int QemuSimIsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_BACKEND_QEMU_H */
