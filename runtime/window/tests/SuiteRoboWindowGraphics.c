/*
 * SuiteRoboWindowGraphics — Goal 7.14 Window + RoboGraphics integration.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <stdio.h>
#include <string.h>

typedef struct gfx_mock {
    int present_count;
    uint32_t last_w;
    uint32_t last_h;
    robo_gfx_pixel_format_t last_fmt;
    uint32_t corner_tl;
    uint32_t corner_br;
} gfx_mock_t;

static gfx_mock_t g_gm;
static robo_window_backend_caps_t g_gm_caps;
static robo_window_backend_vtbl_t g_gm_vtbl;
static int g_gm_next_token = 1;

static uint32_t pack_rgba_sample(const robo_gfx_surface_t *s, uint32_t x,
                                 uint32_t y)
{
    robo_gfx_color_rgba8_t c;
    if (robo_gfx_surface_get_pixel(s, x, y, &c) != ROBO_OK) {
        return 0u;
    }
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static robo_result_t gm_init(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void gm_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t gm_create(void *ud, robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out)
{
    (void)ud;
    (void)id;
    (void)cfg;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_window_native_handle_clear(out);
    out->token = (uint64_t)g_gm_next_token++;
    out->generation = 1u;
    return ROBO_OK;
}

static robo_result_t gm_destroy(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    robo_window_native_handle_invalidate(native);
    return ROBO_OK;
}

static robo_result_t gm_show(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t gm_hide(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t gm_title(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *native,
                              const char *title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)title;
    return ROBO_OK;
}

static robo_result_t gm_size(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *native, int32_t w,
                             int32_t h)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)w;
    (void)h;
    return ROBO_OK;
}

static robo_result_t gm_constraints(void *ud, robo_window_id_t id,
                                    robo_window_native_handle_t *native,
                                    int32_t min_w, int32_t min_h, int32_t max_w,
                                    int32_t max_h)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)min_w;
    (void)min_h;
    (void)max_w;
    (void)max_h;
    return ROBO_OK;
}

static robo_result_t gm_pos(void *ud, robo_window_id_t id,
                            robo_window_native_handle_t *native, int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t gm_focus(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t gm_query(void *ud, robo_window_id_t id,
                              const robo_window_native_handle_t *native,
                              robo_window_state_t *inout)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)inout;
    return ROBO_OK;
}

static robo_result_t gm_present(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *native,
                                robo_gfx_surface_t *surface)
{
    gfx_mock_t *m = (gfx_mock_t *)ud;
    (void)id;
    (void)native;
    if (!m || !surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->present_count++;
    m->last_w = robo_gfx_surface_width(surface);
    m->last_h = robo_gfx_surface_height(surface);
    m->last_fmt = robo_gfx_surface_format(surface);
    m->corner_tl = pack_rgba_sample(surface, 0, 0);
    if (m->last_w > 0u && m->last_h > 0u) {
        m->corner_br = pack_rgba_sample(surface, m->last_w - 1u, m->last_h - 1u);
    }
    return ROBO_OK;
}

static robo_result_t gm_events(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void gm_bind(void)
{
    memset(&g_gm, 0, sizeof(g_gm));
    memset(&g_gm_caps, 0, sizeof(g_gm_caps));
    g_gm_caps.create_window = 1;
    g_gm_caps.destroy_window = 1;
    g_gm_caps.show_hide = 1;
    g_gm_caps.resize = 1;
    g_gm_caps.move = 1;
    g_gm_caps.set_title = 1;
    g_gm_caps.focus = 1;
    g_gm_caps.query_state = 1;
    g_gm_caps.present = 1;
    g_gm_caps.process_events = 1;
    g_gm_caps.size_constraints = 1;
    g_gm_caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT;
    g_gm_caps.present_max_width = 8192u;
    g_gm_caps.present_max_height = 8192u;

    memset(&g_gm_vtbl, 0, sizeof(g_gm_vtbl));
    g_gm_vtbl.name = "gfx_mock";
    g_gm_vtbl.capabilities = &g_gm_caps;
    g_gm_vtbl.initialize = gm_init;
    g_gm_vtbl.shutdown = gm_shutdown;
    g_gm_vtbl.create_window = gm_create;
    g_gm_vtbl.destroy_window = gm_destroy;
    g_gm_vtbl.show = gm_show;
    g_gm_vtbl.hide = gm_hide;
    g_gm_vtbl.set_title = gm_title;
    g_gm_vtbl.set_size = gm_size;
    g_gm_vtbl.set_size_constraints = gm_constraints;
    g_gm_vtbl.set_position = gm_pos;
    g_gm_vtbl.focus = gm_focus;
    g_gm_vtbl.query_state = gm_query;
    g_gm_vtbl.present_surface = gm_present;
    g_gm_vtbl.process_events = gm_events;
}

static int begin_gfx(robo_window_id_t *out_id, int32_t w, int32_t h)
{
    robo_window_config_t cfg;

    gm_bind();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_gm_vtbl, &g_gm), ROBO_OK);
    robo_window_config_init(&cfg);
    cfg.width = w;
    cfg.height = h;
    cfg.visible = 0;
    cfg.title = "gfx-int";
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, out_id), ROBO_OK);
    return 1;
}

static void test_gfx_frame_lifecycle(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_raster_t *raster = NULL;
    robo_gfx_size_t rsz;
    robo_gfx_color_rgba8_t px;

    begin_gfx(&id, 200, 100);
    ROBO_CHECK(robo_window_get_render_surface(id) == NULL);
    ROBO_CHECK(!robo_window_is_dirty(id));
    ROBO_CHECK(!robo_window_frame_is_open(id));

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK(surf != NULL);
    ROBO_CHECK(robo_window_frame_is_open(id));
    {
        robo_gfx_surface_t *nested = surf;
        ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &nested), ROBO_BUSY);
        ROBO_CHECK(nested == surf); /* out-param unchanged on BUSY */
    }

    ROBO_CHECK_EQ_INT(robo_window_get_render_size(id, &rsz), ROBO_OK);
    ROBO_CHECK_EQ_INT(rsz.width, 200);
    ROBO_CHECK_EQ_INT(rsz.height, 100);
    ROBO_CHECK(robo_gfx_surface_format(surf) == ROBO_GFX_FORMAT_BGRA8);

    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(surf, &raster), ROBO_OK);
    (void)robo_gfx_raster_clear(raster,
                                (robo_gfx_color_rgba8_t){245, 245, 247, 255});
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(10, 10, 40, 30),
        (robo_gfx_color_rgba8_t){14, 122, 255, 255});
    (void)robo_gfx_raster_draw_line(
        raster, robo_gfx_point(0, 0), robo_gfx_point(199, 99),
        (robo_gfx_color_rgba8_t){255, 59, 48, 255});
    (void)robo_gfx_raster_set_opacity(raster, 128);
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(80, 40, 50, 40),
        (robo_gfx_color_rgba8_t){48, 209, 88, 255});
    robo_gfx_raster_set_opacity(raster, 255);
    (void)robo_gfx_surface_set_pixel(surf, 199, 99,
                                     (robo_gfx_color_rgba8_t){0, 0, 255, 255});
    robo_gfx_raster_destroy(raster);

    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK(!robo_window_frame_is_open(id));
    ROBO_CHECK(robo_window_is_dirty(id));
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_gm.present_count, 1);
    ROBO_CHECK_EQ_INT((int)g_gm.last_w, 200);
    ROBO_CHECK_EQ_INT((int)g_gm.last_h, 100);
    ROBO_CHECK(!robo_window_is_dirty(id));
    /* Second present with no dirty → no backend call. */
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_gm.present_count, 1);

    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surf, 10, 10, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)px.r, 14);
    ROBO_CHECK_EQ_INT((int)px.g, 122);
    ROBO_CHECK_EQ_INT((int)px.b, 255);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(robo_window_get_render_surface(id) == NULL);
    robo_window_shutdown();
}

static void test_gfx_resize_recreate(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_size_t rsz;
    uint32_t first_w = 0u;
    uint32_t first_h = 0u;

    begin_gfx(&id, 160, 120);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    first_w = robo_gfx_surface_width(surf);
    first_h = robo_gfx_surface_height(surf);
    ROBO_CHECK_EQ_INT((int)first_w, 160);
    ROBO_CHECK_EQ_INT((int)first_h, 120);
    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){0, 0, 0, 255});
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 320, 240), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK(surf != NULL);
    ROBO_CHECK_EQ_INT(robo_window_get_render_size(id, &rsz), ROBO_OK);
    ROBO_CHECK_EQ_INT(rsz.width, 320);
    ROBO_CHECK_EQ_INT(rsz.height, 240);
    ROBO_CHECK(robo_gfx_surface_width(surf) == 320u);
    ROBO_CHECK(robo_gfx_surface_height(surf) == 240u);
    (void)robo_gfx_surface_clear(surf,
                                 (robo_gfx_color_rgba8_t){255, 255, 255, 255});
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g_gm.last_w, 320);
    ROBO_CHECK_EQ_INT((int)g_gm.last_h, 240);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_gfx_scale_physical(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_size_t rsz;
    float scale = 0.0f;

    gm_bind();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_gm_vtbl, &g_gm), ROBO_OK);
    robo_window_config_init(&cfg);
    cfg.width = 100;
    cfg.height = 50;
    cfg.scale_preference = 2.0f;
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &scale), ROBO_OK);
    ROBO_CHECK(scale == 2.0f);

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_render_size(id, &rsz), ROBO_OK);
    ROBO_CHECK_EQ_INT(rsz.width, 200);
    ROBO_CHECK_EQ_INT(rsz.height, 100);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)g_gm.last_w, 200);
    ROBO_CHECK_EQ_INT((int)g_gm.last_h, 100);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_gfx_invalidate_and_errors(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_gfx_surface_t *surf = NULL;

    begin_gfx(&id, 64, 64);
    ROBO_CHECK_EQ_INT(robo_window_invalidate(id), ROBO_OK);
    ROBO_CHECK(robo_window_is_dirty(id));
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_gm.present_count, 1);
    ROBO_CHECK(!robo_window_is_dirty(id));

    ROBO_CHECK_EQ_INT(robo_window_invalidate_rect(id, robo_gfx_rect(0, 0, 0, 0)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_invalidate_rect(id, robo_gfx_rect(1, 1, 8, 8)),
                      ROBO_OK);
    ROBO_CHECK(robo_window_is_dirty(id));
    ROBO_CHECK_EQ_INT(robo_window_clear_dirty(id), ROBO_OK);
    ROBO_CHECK(!robo_window_is_dirty(id));

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(robo_window_release_render_surface(id), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_set_render_format(id, ROBO_GFX_FORMAT_ALPHA8),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_window_set_render_format(id, ROBO_GFX_FORMAT_RGBA8),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_format(surf) == ROBO_GFX_FORMAT_RGBA8);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_NOT_FOUND);
    robo_window_shutdown();
}

static void test_gfx_present_null_uses_owned(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_gfx_surface_t *surf = NULL;

    begin_gfx(&id, 32, 32);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){1, 2, 3, 255});
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    /* Explicit present(NULL) falls back to owned render surface. */
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_gm.present_count, 1);
    ROBO_CHECK_EQ_INT((int)g_gm.last_w, 32);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_gfx_screenshot_pixels(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_surface_t *shot_surf = NULL;
    robo_gfx_color_rgba8_t px;

    begin_gfx(&id, 48, 48);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){0, 0, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, 0, 0,
                                     (robo_gfx_color_rgba8_t){255, 255, 255, 255});
    (void)robo_gfx_surface_fill_rect(
        surf, robo_gfx_rect(16, 16, 8, 8),
        (robo_gfx_color_rgba8_t){14, 122, 255, 255});
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_capture_surface(surf, NULL, &shot),
                      ROBO_OK);
    shot_surf = robo_gfx_screenshot_surface(shot);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(shot_surf, 0, 0, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)px.r, 255);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(shot_surf, 16, 16, &px),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)px.r, 14);
    ROBO_CHECK_EQ_INT((int)px.g, 122);
    robo_gfx_screenshot_release(shot);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

void robo_test_register_window_graphics(void)
{
    robo_test_register("window.graphics.frame_lifecycle",
                       test_gfx_frame_lifecycle);
    robo_test_register("window.graphics.resize_recreate",
                       test_gfx_resize_recreate);
    robo_test_register("window.graphics.scale_physical",
                       test_gfx_scale_physical);
    robo_test_register("window.graphics.invalidate_errors",
                       test_gfx_invalidate_and_errors);
    robo_test_register("window.graphics.present_owned",
                       test_gfx_present_null_uses_owned);
    robo_test_register("window.graphics.screenshot",
                       test_gfx_screenshot_pixels);
}
