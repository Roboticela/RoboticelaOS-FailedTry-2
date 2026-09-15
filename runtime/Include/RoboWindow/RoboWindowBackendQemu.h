/*
 * RoboWindow QEMU backend (Goal 7.12).
 *
 * Guest (ROBO_WINDOW_HAS_QEMU): present via SYS_DISPLAY; input via ROBO_DISP_INPUT
 * translated into RoboKeyboard / RoboMouse.
 *
 * Host: use with robo_qemu_sim_init — present blits into the sim framebuffer.
 */

#ifndef ROBO_WINDOW_BACKEND_QEMU_H
#define ROBO_WINDOW_BACKEND_QEMU_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowBackend.h"

#ifdef __cplusplus
extern "C" {
#endif

const robo_window_backend_vtbl_t *robo_window_backend_qemu_vtbl(void);

/**
 * Register the QEMU window backend.
 * Prefers guest SYS_DISPLAY when ROBO_WINDOW_HAS_QEMU is set; otherwise
 * presents into robo_qemu_fb_current() (host sim).
 */
robo_result_t robo_window_backend_use_qemu(void);

/**
 * Convenience: sim FB + display + window backends.
 * Host/unit tests only (does not talk to real QEMU).
 */
robo_result_t robo_window_backend_use_qemu_sim(uint32_t width, uint32_t height,
                                               uint32_t pixfmt);

/**
 * Drain pending platform input into observe_key / observe_mouse.
 * Guest: SYS_DISPLAY INPUT. Host sim: no-op unless inject helpers used.
 */
robo_result_t robo_qemu_input_poll(void);

/**
 * Inject a Robosys-shaped input event into the same translation path
 * (tests / smoke). Does not require a real device.
 */
robo_result_t robo_qemu_input_inject_raw(uint32_t type, uint32_t code,
                                         uint32_t value, int32_t x, int32_t y,
                                         int32_t dx, int32_t dy,
                                         uint32_t modifiers, uint32_t buttons);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_BACKEND_QEMU_H */
