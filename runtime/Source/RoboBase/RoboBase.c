/*
 * RoboBase — Goal 1.1 implementation (errors via RoboError).
 */

#include "RoboBase/RoboBase.h"

#define ROBO_BASE_VERSION_MAJOR 0
#define ROBO_BASE_VERSION_MINOR 1
#define ROBO_BASE_VERSION_PATCH 0

static const char robo_base_version_string[] = "0.1.0";

/* Process-wide init gate — documented minimal global for lifecycle. */
static robo_i32 g_robo_base_initialized = 0;

robo_result_t robo_base_init(void)
{
    if (g_robo_base_initialized) {
        return ROBO_ALREADY_EXISTS;
    }
    g_robo_base_initialized = 1;
    return ROBO_OK;
}

robo_result_t robo_base_shutdown(void)
{
    if (!g_robo_base_initialized) {
        return ROBO_INIT_ERROR;
    }
    g_robo_base_initialized = 0;
    return ROBO_OK;
}

const char *robo_base_version(void)
{
    return robo_base_version_string;
}

robo_result_t robo_base_get_version(robo_version_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->major = ROBO_BASE_VERSION_MAJOR;
    out->minor = ROBO_BASE_VERSION_MINOR;
    out->patch = ROBO_BASE_VERSION_PATCH;
    return ROBO_OK;
}

robo_i32 robo_base_is_initialized(void)
{
    return g_robo_base_initialized ? 1 : 0;
}
