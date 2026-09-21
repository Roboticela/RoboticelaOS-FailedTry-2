/*
 * RoboDesktop's host launcher (Phase 11.13) — the checkpoint on a real host desktop.
 *
 * One entry point, run by the unified test binary: `RoboTest.host.exe --desktop` opens the shared
 * checkpoint scene (`RoboDesktopScene.h`) in a native window, drives it through the production event
 * queue, and reports what it saw. The guest program (`user/robo-runtime`) drives the *same* scene into a
 * UEFI framebuffer; this launcher is the half that needs no emulator, and it is where a picture is written
 * to disk and where the frame's pixels are read back and asserted.
 *
 * Why it lives beside the module rather than inside it: this file owns a native window backend, a
 * `fopen`, and `printf`. The shell may do none of those, which is the boundary
 * `Assert-RoboDesktopBoundaries` enforces — a shell that could open a window could not be tested without
 * one.
 */
#ifndef ROBO_DESKTOP_HOST_H
#define ROBO_DESKTOP_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the checkpoint.
 *
 * Options: `--no-window` (renders and probes offscreen only), `--no-inject-input`, `--hold <ms>`,
 * `--shot <path>` (PNG of the frame). Environment overrides: `ROBO_DESKTOP_HOLD_MS`, `ROBO_DESKTOP_SHOT`,
 * `ROBO_DESKTOP_NO_WINDOW`.
 *
 * @return 0 all probes passed; 1 a stage failed or a probe did; 2 a usage error.
 */
int DesktopHostMain(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_HOST_H */
