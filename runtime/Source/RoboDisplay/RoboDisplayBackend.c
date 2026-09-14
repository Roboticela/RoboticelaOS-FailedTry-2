#include "RoboDisplay/RoboDisplayBackend.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplayInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static const robo_display_backend_vtbl_t *g_vtbl;
static void *g_userdata;
static int g_live;
static robo_display_backend_caps_t g_caps;

static const robo_display_backend_caps_t g_null_caps = {1, 1, 1, 0, 0, 0, 0, 0};

static const robo_display_backend_caps_t g_placeholder_caps = {
    1, 1, 1, 1, 1, 1, 1, 0};

void robo_display_backend_caps_clear(robo_display_backend_caps_t *caps)
{
    if (caps) {
        memset(caps, 0, sizeof(*caps));
    }
}

robo_result_t robo_display_backend_capabilities(robo_display_backend_caps_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_live || !g_vtbl) {
        robo_display_backend_caps_clear(out);
        return ROBO_NOT_SUPPORTED;
    }
    *out = g_caps;
    return ROBO_OK;
}

static robo_result_t null_init(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void null_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t null_enumerate(void *ud,
                                    robo_display_info_t *out,
                                    size_t capacity,
                                    size_t *out_count)
{
    (void)ud;
    (void)out;
    (void)capacity;
    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    return ROBO_OK;
}

static robo_result_t null_primary(void *ud, robo_display_info_t *out)
{
    (void)ud;
    (void)out;
    return ROBO_NOT_FOUND;
}

static robo_result_t null_get_info(void *ud,
                                   robo_display_id_t id,
                                   robo_display_info_t *out)
{
    (void)ud;
    (void)id;
    (void)out;
    return ROBO_NOT_FOUND;
}

static const robo_display_backend_vtbl_t g_null_vtbl = {
    "null",
    &g_null_caps,
    null_init,
    null_shutdown,
    null_enumerate,
    null_primary,
    null_get_info,
    NULL};

#define ROBO_DISPLAY_PLACEHOLDER_ID ((robo_display_id_t)1u)

static void fill_placeholder(robo_display_info_t *out)
{
    robo_display_info_init(out);
    out->id = ROBO_DISPLAY_PLACEHOLDER_ID;
    {
        static const char name[] = "Architecture Placeholder";
        size_t i;
        for (i = 0u; i + 1u < ROBO_DISPLAY_NAME_MAX && name[i]; i++) {
            out->name[i] = name[i];
        }
        out->name[i] = '\0';
    }
    out->bounds = robo_gfx_rect(0, 0, 1280, 800);
    out->work_area = robo_gfx_rect(0, 0, 1280, 800);
    out->resolution_width_px = 1280;
    out->resolution_height_px = 800;
    out->refresh_hz = 60.0f;
    out->scale = 1.0f;
    out->dpi_x = 96.0f;
    out->dpi_y = 96.0f;
    out->orientation = ROBO_DISPLAY_ORIENTATION_LANDSCAPE;
    out->is_primary = 1;
    out->connection = ROBO_DISPLAY_CONNECTION_CONNECTED;
}

static robo_result_t ph_init(void *ud)
{
    (void)ud;
    robo_log_debug("robo_display: placeholder backend initialize");
    return ROBO_OK;
}

static void ph_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t ph_enumerate(void *ud,
                                  robo_display_info_t *out,
                                  size_t capacity,
                                  size_t *out_count)
{
    (void)ud;
    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (capacity == 0u || !out) {
        *out_count = 1u;
        return ROBO_OK;
    }
    fill_placeholder(&out[0]);
    *out_count = 1u;
    return ROBO_OK;
}

static robo_result_t ph_primary(void *ud, robo_display_info_t *out)
{
    (void)ud;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    fill_placeholder(out);
    return ROBO_OK;
}

static robo_result_t ph_get_info(void *ud,
                                 robo_display_id_t id,
                                 robo_display_info_t *out)
{
    (void)ud;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (id != ROBO_DISPLAY_PLACEHOLDER_ID) {
        return ROBO_NOT_FOUND;
    }
    fill_placeholder(out);
    return ROBO_OK;
}

static const robo_display_backend_vtbl_t g_placeholder_vtbl = {
    "placeholder",
    &g_placeholder_caps,
    ph_init,
    ph_shutdown,
    ph_enumerate,
    ph_primary,
    ph_get_info,
    NULL};

const robo_display_backend_vtbl_t *robo_display_backend_null_vtbl(void)
{
    return &g_null_vtbl;
}

const robo_display_backend_vtbl_t *robo_display_backend_placeholder_vtbl(void)
{
    return &g_placeholder_vtbl;
}

void robo_display_backend_clear(void)
{
    if (g_live && g_vtbl && g_vtbl->shutdown) {
        g_vtbl->shutdown(g_userdata);
    }
    g_vtbl = NULL;
    g_userdata = NULL;
    g_live = 0;
    robo_display_backend_caps_clear(&g_caps);
}

robo_result_t robo_display_backend_register(const robo_display_backend_vtbl_t *vtbl,
                                            void *userdata)
{
    robo_result_t r;

    if (!vtbl || !vtbl->name || !vtbl->capabilities || !vtbl->initialize ||
        !vtbl->shutdown || !vtbl->enumerate || !vtbl->get_primary ||
        !vtbl->get_info) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_display_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    robo_display_backend_clear();
    r = vtbl->initialize(userdata);
    if (r != ROBO_OK) {
        return r;
    }
    g_vtbl = vtbl;
    g_userdata = userdata;
    g_caps = *vtbl->capabilities;
    g_live = 1;
    robo_log_info("robo_display: backend registered '%s'", vtbl->name);
    return ROBO_OK;
}

robo_result_t robo_display_backend_use_null(void)
{
    return robo_display_backend_register(robo_display_backend_null_vtbl(), NULL);
}

robo_result_t robo_display_backend_use_placeholder(void)
{
    return robo_display_backend_register(robo_display_backend_placeholder_vtbl(),
                                         NULL);
}

int robo_display_backend_is_registered(void)
{
    return g_live && g_vtbl != NULL;
}

const char *robo_display_backend_name(void)
{
    return (g_vtbl && g_vtbl->name) ? g_vtbl->name : NULL;
}

/* Internal accessors for manager */
const robo_display_backend_vtbl_t *robo_display_backend_vtbl(void)
{
    return g_vtbl;
}

void *robo_display_backend_userdata(void)
{
    return g_userdata;
}
