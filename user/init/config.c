#include "config.h"

/*
 * v1 starts only services that exist on RoboFS. Future paths such as
 * /System/services/device-manager are documented, not shipped empty.
 */
static const RoboServiceConfig g_services[] = {
    {
        "test-service",
        "/System/services/test-service",
        ROBO_SVC_ENABLED,
        ROBO_RESTART_NEVER,
        0
    },
    {
        "isol-test",
        "/System/services/isol-test",
        ROBO_SVC_ENABLED,
        ROBO_RESTART_NEVER,
        0
    },
    {
        "crash-test",
        "/System/services/crash-test",
        ROBO_SVC_ENABLED,
        ROBO_RESTART_ON_FAILURE,
        2
    }
};

const RoboServiceConfig *robo_service_config(unsigned *count)
{
    if (count)
        *count = (unsigned)(sizeof(g_services) / sizeof(g_services[0]));
    return g_services;
}
