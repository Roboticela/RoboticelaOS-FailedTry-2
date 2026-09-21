#include "config.h"

/*
 * Boot: the RoboticelaOS desktop on the framebuffer (QEMU). `robo-runtime` is the one
 * program started on a normal boot -- it drives the shared checkpoint scene over the whole
 * runtime stack (markup -> CSS -> layout -> paint -> rasteriser) and presents the shell to the
 * display; see `user/robo-runtime/main.c` and `docs/userspace/desktop-runtime.md`.
 *
 * Compositor / window-server return when that stack is ready; today they are selftest-only.
 */
static const RoboServiceConfig gServices[] = {
    {
        "robo-runtime",
        "/System/runtime/robo-runtime",
        RoboSvcEnabled,
        RoboRestartAlways,
        0
    },
    {
        "test-service",
        "/System/services/test-service",
        RoboSvcEnabled | RoboSvcSelftest,
        RoboRestartNever,
        0
    },
    {
        "isol-test",
        "/System/services/isol-test",
        RoboSvcEnabled | RoboSvcSelftest,
        RoboRestartNever,
        0
    },
    {
        "crash-test",
        "/System/services/crash-test",
        RoboSvcEnabled | RoboSvcSelftest,
        RoboRestartOnFailure,
        2
    },
    {
        "window-test",
        "/bin/window-test",
        RoboSvcEnabled | RoboSvcSelftest,
        RoboRestartNever,
        0
    },
    {
        "nettest",
        "/bin/nettest",
        RoboSvcEnabled | RoboSvcSelftest,
        RoboRestartNever,
        0
    },
    {
        "libc-test",
        "/bin/libc-test",
        RoboSvcEnabled | RoboSvcSelftest,
        RoboRestartNever,
        0
    }
};

const RoboServiceConfig *ServiceConfig(unsigned *count)
{
    if (count)
        *count = (unsigned)(sizeof(gServices) / sizeof(gServices[0]));
    return gServices;
}
