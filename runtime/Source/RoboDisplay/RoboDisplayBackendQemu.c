#include "RoboDisplay/RoboDisplayBackendQemu.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayInfo.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define ROBO_QEMU_DISPLAY_ID ((robo_display_id_t)1u)

static robo_qemu_fb_info_t g_fb;
static int g_fb_set;
static int g_sim_active;
static void *g_sim_pixels;

static const robo_display_backend_caps_t g_caps = {
    1, /* enumerate */
    1, /* primary */
    1, /* get_info */
    1, /* work_area */
    0, /* refresh_rate — unknown on GOP FB */
    1, /* scale_dpi — report scale=1 fallback */
    0, /* orientation */
    1  /* modes — single current mode */
};

const robo_qemu_fb_info_t *robo_qemu_fb_current(void)
{
    return g_fb_set ? &g_fb : NULL;
}

robo_result_t robo_qemu_fb_set_current(const robo_qemu_fb_info_t *info)
{
    robo_result_t r;
    if (!info) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_qemu_fb_info_validate(info);
    if (r != ROBO_OK && !info->headless) {
        return r;
    }
    g_fb = *info;
    g_fb_set = 1;
    return ROBO_OK;
}

void robo_qemu_fb_clear_current(void)
{
    memset(&g_fb, 0, sizeof(g_fb));
    g_fb_set = 0;
}

static void fill_info(robo_display_info_t *out)
{
    int32_t w;
    int32_t h;

    robo_display_info_init(out);
    out->id = ROBO_QEMU_DISPLAY_ID;
    strncpy(out->name, "QEMU Framebuffer", sizeof(out->name) - 1u);
    out->name[sizeof(out->name) - 1u] = '\0';
    if (!g_fb_set || g_fb.headless || !g_fb.ready) {
        out->connection = ROBO_DISPLAY_CONNECTION_UNAVAILABLE;
        out->is_primary = 1;
        out->scale = 1.0f;
        return;
    }
    w = (int32_t)g_fb.width;
    h = (int32_t)g_fb.height;
    out->bounds = robo_gfx_rect(0, 0, w, h);
    out->work_area = out->bounds;
    out->resolution_width_px = w;
    out->resolution_height_px = h;
    out->refresh_hz = 0.0f;
    out->scale = 1.0f; /* no physical DPI from GOP — logical == physical */
    out->dpi_x = 0.0f;
    out->dpi_y = 0.0f;
    out->orientation = ROBO_DISPLAY_ORIENTATION_LANDSCAPE;
    out->is_primary = 1;
    out->connection = ROBO_DISPLAY_CONNECTION_CONNECTED;
    out->physical_width_mm = 0;
    out->physical_height_mm = 0;
}

static robo_result_t qemu_init(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void qemu_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t qemu_enumerate(void *ud, robo_display_info_t *out,
                                    size_t capacity, size_t *out_count)
{
    (void)ud;
    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    if (!g_fb_set) {
        return ROBO_OK;
    }
    if (capacity == 0u || !out) {
        *out_count = 1u;
        return capacity == 0u ? ROBO_OK : ROBO_INVALID_ARGUMENT;
    }
    fill_info(&out[0]);
    *out_count = 1u;
    return ROBO_OK;
}

static robo_result_t qemu_primary(void *ud, robo_display_info_t *out)
{
    (void)ud;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_fb_set) {
        return ROBO_NOT_FOUND;
    }
    fill_info(out);
    return ROBO_OK;
}

static robo_result_t qemu_get_info(void *ud, robo_display_id_t id,
                                   robo_display_info_t *out)
{
    (void)ud;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!g_fb_set || id != ROBO_QEMU_DISPLAY_ID) {
        return ROBO_NOT_FOUND;
    }
    fill_info(out);
    return ROBO_OK;
}

static robo_result_t qemu_get_modes(void *ud, robo_display_id_t id,
                                    robo_display_mode_t *out, size_t capacity,
                                    size_t *out_count)
{
    (void)ud;
    if (!out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    if (!g_fb_set || id != ROBO_QEMU_DISPLAY_ID || g_fb.headless) {
        return ROBO_NOT_FOUND;
    }
    if (capacity == 0u) {
        *out_count = 1u;
        return ROBO_OK;
    }
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_display_mode_init(&out[0]);
    out[0].width_px = (int32_t)g_fb.width;
    out[0].height_px = (int32_t)g_fb.height;
    out[0].refresh_hz = 0.0f;
    *out_count = 1u;
    return ROBO_OK;
}

static const robo_display_backend_vtbl_t g_qemu_display_vtbl = {
    "qemu",
    &g_caps,
    qemu_init,
    qemu_shutdown,
    qemu_enumerate,
    qemu_primary,
    qemu_get_info,
    qemu_get_modes
};

const robo_display_backend_vtbl_t *robo_display_backend_qemu_vtbl(void)
{
    return &g_qemu_display_vtbl;
}

robo_result_t robo_display_backend_use_qemu(void)
{
    return robo_display_backend_register(&g_qemu_display_vtbl, NULL);
}

robo_result_t robo_qemu_sim_init(uint32_t width, uint32_t height,
                                 uint32_t pixfmt)
{
    robo_qemu_fb_info_t info;
    size_t bytes;
    uint32_t pitch;

    robo_qemu_sim_shutdown();
    if (width == 0u || height == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (pixfmt != ROBO_QEMU_PIX_BGRX && pixfmt != ROBO_QEMU_PIX_RGBX) {
        return ROBO_NOT_SUPPORTED;
    }
    pitch = width * 4u;
    bytes = (size_t)pitch * (size_t)height;
    g_sim_pixels = robo_calloc(1, bytes);
    if (!g_sim_pixels) {
        return ROBO_OUT_OF_MEMORY;
    }
    robo_qemu_fb_info_init(&info);
    info.width = width;
    info.height = height;
    info.pitch_bytes = pitch;
    info.bpp = 32u;
    info.pixfmt = pixfmt;
    info.pixels = g_sim_pixels;
    info.byte_size = bytes;
    info.ready = 1;
    info.headless = 0;
    if (robo_qemu_fb_set_current(&info) != ROBO_OK) {
        robo_free(g_sim_pixels);
        g_sim_pixels = NULL;
        return ROBO_INVALID_ARGUMENT;
    }
    g_sim_active = 1;
    robo_log_info("robo_qemu_sim: %ux%u pixfmt=%u", (unsigned)width,
                  (unsigned)height, (unsigned)pixfmt);
    return ROBO_OK;
}

void robo_qemu_sim_shutdown(void)
{
    if (g_sim_pixels) {
        robo_free(g_sim_pixels);
        g_sim_pixels = NULL;
    }
    g_sim_active = 0;
    robo_qemu_fb_clear_current();
}

int robo_qemu_sim_is_active(void)
{
    return g_sim_active;
}
