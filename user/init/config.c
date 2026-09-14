#include "config.h"

/*
 * Boot: RoboGraphics visual preview on the framebuffer (QEMU).
 * Compositor / window-server return when that stack is ready.
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
