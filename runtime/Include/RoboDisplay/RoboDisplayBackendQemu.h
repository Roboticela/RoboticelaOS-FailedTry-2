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
const robo_qemu_fb_info_t *robo_qemu_fb_current(void);

/**
 * Install / replace the active QEMU FB descriptor (copied).
 * Used by the window backend after claim/info or by the host sim.
 */
robo_result_t robo_qemu_fb_set_current(const robo_qemu_fb_info_t *info);

void robo_qemu_fb_clear_current(void);

const robo_display_backend_vtbl_t *robo_display_backend_qemu_vtbl(void);

/**
 * Register QEMU display backend (enumerates from robo_qemu_fb_current).
 * Call after FB metadata is known (or use sim helpers first).
 */
robo_result_t robo_display_backend_use_qemu(void);

/**
 * Host/unit-test simulator: allocate a CPU framebuffer and register backends.
 * Does not require QEMU or SYS_DISPLAY.
 */
robo_result_t robo_qemu_sim_init(uint32_t width, uint32_t height,
                                 uint32_t pixfmt);
void robo_qemu_sim_shutdown(void);
int robo_qemu_sim_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_BACKEND_QEMU_H */
