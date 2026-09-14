#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplayInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static int g_initialized;
static robo_display_info_t g_cache[ROBO_DISPLAY_MAX_DISPLAYS];
static size_t g_count;

const char *robo_display_version_string(void)
{
    return ROBO_DISPLAY_VERSION_STRING;
}

int robo_display_is_initialized(void)
{
    return g_initialized;
}

robo_result_t robo_display_init(void)
{
    if (g_initialized) {
        return ROBO_OK;
    }
    memset(g_cache, 0, sizeof(g_cache));
    g_count = 0u;
    g_initialized = 1;
    robo_log_info("robo_display: init (version %s)", ROBO_DISPLAY_VERSION_STRING);
    return ROBO_OK;
}

void robo_display_shutdown(void)
{
    if (!g_initialized) {
        return;
    }
    robo_display_backend_clear();
    memset(g_cache, 0, sizeof(g_cache));
    g_count = 0u;
    g_initialized = 0;
    robo_log_info("robo_display: shutdown");
}

robo_result_t robo_display_refresh(void)
{
    const robo_display_backend_vtbl_t *vtbl;
    size_t n = 0u;
    robo_result_t r;
    robo_display_backend_caps_t caps;

    if (!g_initialized) {
        return ROBO_INIT_ERROR;
    }
    if (!robo_display_backend_is_registered()) {
        g_count = 0u;
        return ROBO_NOT_SUPPORTED;
    }
    if (robo_display_backend_capabilities(&caps) != ROBO_OK || !caps.enumerate) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_display_backend_vtbl();
    r = vtbl->enumerate(robo_display_backend_userdata(), g_cache,
                        ROBO_DISPLAY_MAX_DISPLAYS, &n);
    if (r != ROBO_OK) {
        g_count = 0u;
        return r;
    }
    if (n > ROBO_DISPLAY_MAX_DISPLAYS) {
        n = ROBO_DISPLAY_MAX_DISPLAYS;
    }
    g_count = n;
    robo_log_debug("robo_display: refreshed count=%u", (unsigned)g_count);
    return ROBO_OK;
}

size_t robo_display_count(void)
{
    return g_count;
}

robo_result_t robo_display_at(size_t index, robo_display_info_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return ROBO_INIT_ERROR;
    }
    if (index >= g_count) {
        return ROBO_NOT_FOUND;
    }
    *out = g_cache[index];
    return ROBO_OK;
}

robo_result_t robo_display_get(robo_display_id_t id, robo_display_info_t *out)
{
    size_t i;
    const robo_display_backend_vtbl_t *vtbl;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return ROBO_INIT_ERROR;
    }
    if (id == ROBO_DISPLAY_ID_NONE) {
        return ROBO_NOT_FOUND;
    }
    for (i = 0u; i < g_count; i++) {
        if (g_cache[i].id == id) {
            *out = g_cache[i];
            return ROBO_OK;
        }
    }
    /* Fall through to backend get_info if registered. */
    vtbl = robo_display_backend_vtbl();
    if (vtbl && vtbl->get_info) {
        return vtbl->get_info(robo_display_backend_userdata(), id, out);
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_display_primary(robo_display_info_t *out)
{
    size_t i;
    const robo_display_backend_vtbl_t *vtbl;
    robo_result_t r;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return ROBO_INIT_ERROR;
    }
    for (i = 0u; i < g_count; i++) {
        if (g_cache[i].is_primary &&
            g_cache[i].connection != ROBO_DISPLAY_CONNECTION_DISCONNECTED) {
            *out = g_cache[i];
            return ROBO_OK;
        }
    }
    vtbl = robo_display_backend_vtbl();
    if (vtbl && vtbl->get_primary) {
        r = vtbl->get_primary(robo_display_backend_userdata(), out);
        if (r == ROBO_OK) {
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

int robo_display_id_is_known(robo_display_id_t id)
{
    size_t i;

    if (id == ROBO_DISPLAY_ID_NONE) {
        return 0;
    }
    for (i = 0u; i < g_count; i++) {
        if (g_cache[i].id == id) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_display_get_modes(robo_display_id_t id,
                                     robo_display_mode_t *out,
                                     size_t capacity,
                                     size_t *out_count)
{
    const robo_display_backend_vtbl_t *vtbl;
    robo_display_backend_caps_t caps;

    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return ROBO_INIT_ERROR;
    }
    if (robo_display_backend_capabilities(&caps) != ROBO_OK || !caps.modes) {
        return ROBO_NOT_SUPPORTED;
    }
    vtbl = robo_display_backend_vtbl();
    if (!vtbl || !vtbl->get_modes) {
        return ROBO_NOT_SUPPORTED;
    }
    return vtbl->get_modes(robo_display_backend_userdata(), id, out, capacity,
                           out_count);
}
