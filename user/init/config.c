#include "config.h"

/*
 * Boot: the RoboticelaOS desktop on the framebuffer (QEMU). `robo-runtime` is the one
 * program started on a normal boot -- it drives the shared checkpoint scene over the whole
 * runtime stack (markup -> CSS -> layout -> paint -> rasteriser) and presents the shell to the
 * display; see `user/robo-runtime/main.c` and `docs/userspace/desktop-runtime.md`.
 *
 * Compositor / window-server return when that stack is ready; today they are selftest-only.
 */
static const RoboServiceConfig g_services[] = {
    {
        "robo-runtime",
        "/System/runtime/robo-runtime",
        ROBO_SVC_ENABLED,
        ROBO_RESTART_ALWAYS,
        0
    },
    {
        "test-service",
        "/System/services/test-service",
        ROBO_SVC_ENABLED | ROBO_SVC_SELFTEST,
        ROBO_RESTART_NEVER,
        0
    },
    {
        "isol-test",
        "/System/services/isol-test",
        ROBO_SVC_ENABLED | ROBO_SVC_SELFTEST,
        ROBO_RESTART_NEVER,
        0
    },
    {
        "crash-test",
        "/System/services/crash-test",
        ROBO_SVC_ENABLED | ROBO_SVC_SELFTEST,
        ROBO_RESTART_ON_FAILURE,
        2
    },
    {
        "window-test",
        "/bin/window-test",
        ROBO_SVC_ENABLED | ROBO_SVC_SELFTEST,
        ROBO_RESTART_NEVER,
        0
    },
    {
        "nettest",
        "/bin/nettest",
        ROBO_SVC_ENABLED | ROBO_SVC_SELFTEST,
        ROBO_RESTART_NEVER,
        0
    },
    {
        "libc-test",
        "/bin/libc-test",
        ROBO_SVC_ENABLED | ROBO_SVC_SELFTEST,
        ROBO_RESTART_NEVER,
        0
    }
};

const RoboServiceConfig *robo_service_config(unsigned *count)
{
    if (count)
        *count = (unsigned)(sizeof(g_services) / sizeof(g_services[0]));
    return g_services;
}
