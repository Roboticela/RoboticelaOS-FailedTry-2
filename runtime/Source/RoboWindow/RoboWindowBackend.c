#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static const robo_window_backend_vtbl_t *g_vtbl;
static void *g_userdata;
static int g_backend_live;
static robo_window_backend_caps_t g_caps_cache;

static const robo_window_backend_caps_t g_null_caps = {
    1, /* create_window */
    1, /* destroy_window */
    1, /* show_hide */
    1, /* resize */
    1, /* move */
    1, /* set_title */
    0, /* focus — no native focus */
    0, /* query_state */
    0, /* present */
    1, /* process_events (no-op OK) */
    0, /* reports_actual_state */
    0u, /* present_format_mask */
    0u, /* present_max_width */
    0u  /* present_max_height */
};

void robo_window_backend_caps_clear(robo_window_backend_caps_t *caps)
{
    if (caps) {
        memset(caps, 0, sizeof(*caps));
    }
}

void robo_window_backend_caps_init_null(robo_window_backend_caps_t *caps)
{
    if (caps) {
        *caps = g_null_caps;
    }
}

robo_result_t robo_window_backend_capabilities(robo_window_backend_caps_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_backend_live || !g_vtbl || !g_vtbl->capabilities) {
        robo_window_backend_caps_clear(out);
        return ROBO_NOT_SUPPORTED;
    }
    /* Live table — backends/tests may update the pointed-to caps. */
    *out = *g_vtbl->capabilities;
    g_caps_cache = *out;
    return ROBO_OK;
}

static robo_result_t null_initialize(void *userdata)
{
    (void)userdata;
    robo_log_debug("robo_window: null backend initialize");
    return ROBO_OK;
}

static void null_shutdown(void *userdata)
{
    (void)userdata;
    robo_log_debug("robo_window: null backend shutdown");
}

static robo_result_t null_create_window(void *userdata,
                                        robo_window_id_t id,
                                        const robo_window_config_t *cfg,
                                        robo_window_native_handle_t *out_native)
{
    (void)userdata;
    (void)id;
    (void)cfg;
    if (!out_native) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* No native OS window — empty handle is intentional. */
    robo_window_native_handle_clear(out_native);
    return ROBO_OK;
}

static robo_result_t null_destroy_window(void *userdata,
                                         robo_window_id_t id,
                                         robo_window_native_handle_t *native)
{
    (void)userdata;
    (void)id;
    if (native) {
        robo_window_native_handle_invalidate(native);
    }
    return ROBO_OK;
}

static robo_result_t null_show(void *userdata,
                               robo_window_id_t id,
                               robo_window_native_handle_t *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t null_hide(void *userdata,
                               robo_window_id_t id,
                               robo_window_native_handle_t *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t null_set_title(void *userdata,
                                    robo_window_id_t id,
                                    robo_window_native_handle_t *native,
                                    const char *utf8_title)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)utf8_title;
    /* In-process acknowledgment only — no OS title bar. */
    return ROBO_OK;
}

static robo_result_t null_set_size(void *userdata,
                                   robo_window_id_t id,
                                   robo_window_native_handle_t *native,
                                   int32_t width,
                                   int32_t height)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    return ROBO_OK;
}

static robo_result_t null_set_position(void *userdata,
                                       robo_window_id_t id,
                                       robo_window_native_handle_t *native,
                                       int32_t x,
                                       int32_t y)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t null_focus(void *userdata,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t null_present_surface(void *userdata,
                                          robo_window_id_t id,
                                          robo_window_native_handle_t *native,
                                          robo_gfx_surface_t *surface)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)surface;
    return ROBO_NOT_SUPPORTED;
}

static robo_result_t null_process_events(void *userdata)
{
    (void)userdata;
    return ROBO_OK;
}

static const robo_window_backend_vtbl_t g_null_vtbl = {
    "null",
    &g_null_caps,
    null_initialize,
    null_shutdown,
    null_create_window,
    null_destroy_window,
    null_show,
    null_hide,
    null_set_title,
    null_set_size,
    null_set_position,
    null_focus,
    NULL,
    null_present_surface,
    null_process_events};

const robo_window_backend_vtbl_t *robo_window_backend_null_vtbl(void)
{
    return &g_null_vtbl;
}

const robo_window_backend_vtbl_t *robo_window_backend_vtbl(void)
{
    return g_vtbl;
}

void *robo_window_backend_userdata(void)
{
    return g_userdata;
}

void robo_window_backend_clear(void)
{
    if (g_backend_live && g_vtbl && g_vtbl->shutdown) {
        g_vtbl->shutdown(g_userdata);
    }
    g_vtbl = NULL;
    g_userdata = NULL;
    g_backend_live = 0;
    robo_window_backend_caps_clear(&g_caps_cache);
}

robo_result_t robo_window_backend_register(const robo_window_backend_vtbl_t *vtbl,
                                           void *userdata)
{
    robo_result_t r;

    if (!vtbl || !vtbl->name || !vtbl->capabilities || !vtbl->initialize ||
        !vtbl->shutdown || !vtbl->create_window || !vtbl->destroy_window ||
        !vtbl->show || !vtbl->hide || !vtbl->set_title || !vtbl->focus ||
        !vtbl->present_surface || !vtbl->process_events) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    robo_window_backend_clear();
    r = vtbl->initialize(userdata);
    if (r != ROBO_OK) {
        robo_log_error("robo_window: backend '%s' initialize failed (%d)",
                       vtbl->name, (int)r);
        return r;
    }
    g_vtbl = vtbl;
    g_userdata = userdata;
    g_caps_cache = *vtbl->capabilities;
    g_backend_live = 1;
    robo_log_info("robo_window: backend registered '%s'", vtbl->name);
    return ROBO_OK;
}

robo_result_t robo_window_backend_use_null(void)
{
    return robo_window_backend_register(robo_window_backend_null_vtbl(), NULL);
}

int robo_window_backend_is_registered(void)
{
    return g_backend_live && g_vtbl != NULL;
}

const char *robo_window_backend_name(void)
{
    return (g_vtbl && g_vtbl->name) ? g_vtbl->name : NULL;
}
