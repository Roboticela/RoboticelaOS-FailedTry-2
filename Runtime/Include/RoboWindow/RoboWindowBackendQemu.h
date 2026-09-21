/*
 * RoboWindow QEMU backend (Goal 7.12).
 *
 * Guest (RoboWindowHasQemu): present via SYS_DISPLAY; input via RoboDispInput
 * translated into RoboKeyboard / RoboMouse.
 *
 * Host: use with QemuSimInit — present blits into the sim framebuffer.
 */

#ifndef ROBO_WINDOW_BACKEND_QEMU_H
#define ROBO_WINDOW_BACKEND_QEMU_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowBackend.h"

#ifdef __cplusplus
extern "C" {
#endif

const WindowBackendVtbl *WindowBackendQemuVtbl(void);

/**
 * Register the QEMU window backend.
 * Prefers guest SYS_DISPLAY when RoboWindowHasQemu is set; otherwise
 * presents into QemuFbCurrent() (host sim).
 */
Result WindowBackendUseQemu(void);

/**
 * Convenience: sim FB + display + window backends.
 * Host/unit tests only (does not talk to real QEMU).
 */
Result WindowBackendUseQemuSim(uint32_t width, uint32_t height,
                                               uint32_t pixfmt);

/**
 * Drain pending platform input into observe_key / observe_mouse.
 * Guest: SYS_DISPLAY INPUT. Host sim: no-op unless inject helpers used.
 */
Result QemuInputPoll(void);

/**
 * Inject a Robosys-shaped input event into the same translation path
 * (tests / smoke). Does not require a real device.
 */
Result QemuInputInjectRaw(uint32_t type, uint32_t code,
                                         uint32_t value, int32_t x, int32_t y,
                                         int32_t dx, int32_t dy,
                                         uint32_t modifiers, uint32_t buttons);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_BACKEND_QEMU_H */
