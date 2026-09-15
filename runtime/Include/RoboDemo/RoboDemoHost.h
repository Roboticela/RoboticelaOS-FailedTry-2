/*
 * RoboDemo host launcher (visual checkpoint).
 *
 * Declares the entry point the host test executable exposes as
 * `--demo-html`. The implementation lives in Source/RoboDemo/platform/host
 * because it owns a real native window (Goal 7.13 host backend) and writes
 * files, neither of which belong in the portable runtime.
 *
 * This is a developer milestone entry point, not a shipping application
 * API. It disappears with the rest of the demo layer once Phase 8.3 and the
 * real window/compositor path take over.
 */

#ifndef ROBO_DEMO_HOST_H
#define ROBO_DEMO_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the HTML visual checkpoint on the host.
 *
 * @param argc Argument count, argv[0] being the triggering flag itself
 *             (e.g. "--demo-html").
 * @param argv Argument vector.
 *
 * Flags:
 *   --no-window        Skip the native window; render offscreen only.
 *   --hold <ms>        Keep the window up for <ms> (default 4000, 0 = exit
 *                      after the first validated frame).
 *   --shot <path>      Write the rendered frame as a deterministic PNG.
 *
 * Environment fallbacks: ROBO_DEMO_HOLD_MS, ROBO_DEMO_SHOT, and
 * ROBO_DEMO_NO_WINDOW=1.
 *
 * @return 0 when the page rendered and every pixel probe matched; 1 on a
 *         rendering, presentation, or validation failure; 2 on bad usage.
 */
int robo_demo_html_host_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DEMO_HOST_H */
