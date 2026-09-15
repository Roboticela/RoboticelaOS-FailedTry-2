/*
 * SuiteRoboWindowPhase7 — Goal 7.15 Phase 7 acceptance / regression.
 *
 * Consolidates lifecycle, graphics-through-window, input, stale-ID safety,
 * failure injection, multi-window, pixel validation, and light perf.
 * Host/QEMU real backends remain in window.host.* / window.qemu.* + smoke scripts.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsPath.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowBackendQemu.h"
#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboWindow/RoboWindowVersion.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- Mock window backend ---- */

typedef struct p7_mock {
    int present_count;
    int create_count;
    int destroy_count;
    int fail_create;
    int fail_present;
    int fail_size;
    uint32_t last_w;
    uint32_t last_h;
    int next_token;
} p7_mock_t;

static p7_mock_t g_p7;
static robo_window_backend_caps_t g_p7_caps;
static robo_window_backend_vtbl_t g_p7_vtbl;

static robo_result_t p7_ok(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void p7_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t p7_create(void *ud, robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out)
{
    p7_mock_t *m = (p7_mock_t *)ud;
    (void)id;
    (void)cfg;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m && m->fail_create) {
        return ROBO_ERROR;
    }
    if (m) {
        m->create_count++;
    }
    robo_window_native_handle_clear(out);
    out->token = (uint64_t)(m ? m->next_token++ : 1);
    out->generation = 1u;
    return ROBO_OK;
}

static robo_result_t p7_destroy(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    p7_mock_t *m = (p7_mock_t *)ud;
    (void)id;
    if (m) {
        m->destroy_count++;
    }
    robo_window_native_handle_invalidate(native);
    return ROBO_OK;
}

static robo_result_t p7_nop_win(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t p7_title(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *native,
                              const char *title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)title;
    return ROBO_OK;
}

static robo_result_t p7_size(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *native, int32_t w,
                             int32_t h)
{
    p7_mock_t *m = (p7_mock_t *)ud;
    (void)id;
    (void)native;
    (void)w;
    (void)h;
    if (m && m->fail_size) {
        return ROBO_ERROR;
    }
    return ROBO_OK;
}

static robo_result_t p7_constraints(void *ud, robo_window_id_t id,
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

static robo_result_t p7_pos(void *ud, robo_window_id_t id,
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

static robo_result_t p7_query(void *ud, robo_window_id_t id,
                              const robo_window_native_handle_t *native,
                              robo_window_state_t *inout)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)inout;
    return ROBO_OK;
}

static robo_result_t p7_present(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *native,
                                robo_gfx_surface_t *surface)
{
    p7_mock_t *m = (p7_mock_t *)ud;
    (void)id;
    (void)native;
    if (!m || !surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_present) {
        return ROBO_IO_ERROR;
    }
    m->present_count++;
    m->last_w = robo_gfx_surface_width(surface);
    m->last_h = robo_gfx_surface_height(surface);
    return ROBO_OK;
}

static robo_result_t p7_events(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void p7_bind(void)
{
    memset(&g_p7, 0, sizeof(g_p7));
    g_p7.next_token = 1;
    memset(&g_p7_caps, 0, sizeof(g_p7_caps));
    g_p7_caps.create_window = 1;
    g_p7_caps.destroy_window = 1;
    g_p7_caps.show_hide = 1;
    g_p7_caps.resize = 1;
    g_p7_caps.move = 1;
    g_p7_caps.set_title = 1;
    g_p7_caps.focus = 1;
    g_p7_caps.query_state = 1;
    g_p7_caps.present = 1;
    g_p7_caps.process_events = 1;
    g_p7_caps.size_constraints = 1;
    g_p7_caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT;
    g_p7_caps.present_max_width = 8192u;
    g_p7_caps.present_max_height = 8192u;

    memset(&g_p7_vtbl, 0, sizeof(g_p7_vtbl));
    g_p7_vtbl.name = "p7_mock";
    g_p7_vtbl.capabilities = &g_p7_caps;
    g_p7_vtbl.initialize = p7_ok;
    g_p7_vtbl.shutdown = p7_shutdown;
    g_p7_vtbl.create_window = p7_create;
    g_p7_vtbl.destroy_window = p7_destroy;
    g_p7_vtbl.show = p7_nop_win;
    g_p7_vtbl.hide = p7_nop_win;
    g_p7_vtbl.set_title = p7_title;
    g_p7_vtbl.set_size = p7_size;
    g_p7_vtbl.set_size_constraints = p7_constraints;
    g_p7_vtbl.set_position = p7_pos;
    g_p7_vtbl.focus = p7_nop_win;
    g_p7_vtbl.query_state = p7_query;
    g_p7_vtbl.present_surface = p7_present;
    g_p7_vtbl.process_events = p7_events;
}

static void p7_fixture(void)
{
    p7_bind();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_p7_vtbl, &g_p7), ROBO_OK);
}

static void p7_end(void)
{
    robo_window_shutdown();
}

static void drain_events(void)
{
    robo_window_event_t ev;
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
}

static robo_window_id_t p7_create_window(int32_t w, int32_t h, float scale)
{
    robo_window_config_t cfg;
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;

    robo_window_config_init(&cfg);
    cfg.width = w;
    cfg.height = h;
    cfg.visible = 0;
    cfg.title = "p7";
    if (scale > 0.0f) {
        cfg.scale_preference = scale;
    }
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    return id;
}

static int color_near(robo_gfx_color_rgba8_t c, uint8_t r, uint8_t g, uint8_t b,
                      int tol)
{
    int dr = (int)c.r - (int)r;
    int dg = (int)c.g - (int)g;
    int db = (int)c.b - (int)b;
    if (dr < 0) {
        dr = -dr;
    }
    if (dg < 0) {
        dg = -dg;
    }
    if (db < 0) {
        db = -db;
    }
    return dr <= tol && dg <= tol && db <= tol;
}

static void expect_pixel(robo_gfx_surface_t *s, uint32_t x, uint32_t y,
                         uint8_t r, uint8_t g, uint8_t b, int tol)
{
    robo_gfx_color_rgba8_t px;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, x, y, &px), ROBO_OK);
    if (!color_near(px, r, g, b, tol)) {
        char msg[160];
        (void)snprintf(msg, sizeof(msg),
                       "pixel (%u,%u) got rgb(%u,%u,%u) expected (~%u,%u,%u) "
                       "tol=%d backend=p7_mock",
                       (unsigned)x, (unsigned)y, (unsigned)px.r, (unsigned)px.g,
                       (unsigned)px.b, (unsigned)r, (unsigned)g, (unsigned)b,
                       tol);
        ROBO_CHECK_MSG(0, msg);
    }
}

/** Deterministic Phase 7 visual fixture (logical/physical coords). */
static void draw_phase7_pattern(robo_gfx_surface_t *surf)
{
    robo_gfx_raster_t *raster = NULL;
    robo_gfx_path_t *path = NULL;
    uint32_t w = robo_gfx_surface_width(surf);
    uint32_t h = robo_gfx_surface_height(surf);

    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(surf, &raster), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_clear(raster, (robo_gfx_color_rgba8_t){245, 245, 247, 255}),
        ROBO_OK);

    /* Border */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(raster, robo_gfx_rect(0, 0, (int32_t)w, 2),
                                  (robo_gfx_color_rgba8_t){29, 29, 31, 255}),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(
            raster, robo_gfx_rect(0, (int32_t)h - 2, (int32_t)w, 2),
            (robo_gfx_color_rgba8_t){29, 29, 31, 255}),
        ROBO_OK);

    /* Interior rectangle + diagonal (before corners so markers stay exact) */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(raster, robo_gfx_rect(20, 20, 40, 30),
                                  (robo_gfx_color_rgba8_t){14, 122, 255, 255}),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(
            raster, robo_gfx_point(8, 8),
            robo_gfx_point((int32_t)w - 9, (int32_t)h - 9),
            (robo_gfx_color_rgba8_t){255, 59, 48, 255}),
        ROBO_OK);

    /* Corner markers (exact pixels for validation) */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(raster, robo_gfx_rect(0, 0, 4, 4),
                                  (robo_gfx_color_rgba8_t){255, 59, 48, 255}),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(
            raster, robo_gfx_rect((int32_t)w - 4, 0, 4, 4),
            (robo_gfx_color_rgba8_t){48, 209, 88, 255}),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(
            raster, robo_gfx_rect(0, (int32_t)h - 4, 4, 4),
            (robo_gfx_color_rgba8_t){14, 122, 255, 255}),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(
            raster,
            robo_gfx_rect((int32_t)w - 4, (int32_t)h - 4, 4, 4),
            (robo_gfx_color_rgba8_t){255, 159, 10, 255}),
        ROBO_OK);

    /* Alpha region */
    robo_gfx_raster_set_opacity(raster, 128);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(raster, robo_gfx_rect(50, 40, 40, 30),
                                  (robo_gfx_color_rgba8_t){48, 209, 88, 255}),
        ROBO_OK);
    robo_gfx_raster_set_opacity(raster, 255);

    /* Path triangle (foundation): build path, stroke via line segments */
    ROBO_CHECK_EQ_INT(robo_gfx_path_create(&path), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_path_move_to(path, robo_gfx_point((int32_t)w / 2, 10)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_path_line_to(path, robo_gfx_point((int32_t)w / 2 + 20, 40)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_path_line_to(path, robo_gfx_point((int32_t)w / 2 - 20, 40)),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_path_close(path), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_path_command_count(path) >= 3, 1);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(
            raster, robo_gfx_point((int32_t)w / 2, 10),
            robo_gfx_point((int32_t)w / 2 + 20, 40),
            (robo_gfx_color_rgba8_t){110, 110, 115, 255}),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(
            raster, robo_gfx_point((int32_t)w / 2 + 20, 40),
            robo_gfx_point((int32_t)w / 2 - 20, 40),
            (robo_gfx_color_rgba8_t){110, 110, 115, 255}),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_draw_line(
            raster, robo_gfx_point((int32_t)w / 2 - 20, 40),
            robo_gfx_point((int32_t)w / 2, 10),
            (robo_gfx_color_rgba8_t){110, 110, 115, 255}),
        ROBO_OK);
    robo_gfx_path_release(path);

    /* Center marker */
    ROBO_CHECK_EQ_INT(
        robo_gfx_raster_fill_rect(
            raster,
            robo_gfx_rect((int32_t)w / 2 - 2, (int32_t)h / 2 - 2, 4, 4),
            (robo_gfx_color_rgba8_t){0, 0, 0, 255}),
        ROBO_OK);

    robo_gfx_raster_destroy(raster);
}

static void validate_phase7_pattern(robo_gfx_surface_t *surf)
{
    uint32_t w = robo_gfx_surface_width(surf);
    uint32_t h = robo_gfx_surface_height(surf);

    expect_pixel(surf, 0, 0, 255, 59, 48, 0);
    expect_pixel(surf, w - 1, 0, 48, 209, 88, 0);
    expect_pixel(surf, 0, h - 1, 14, 122, 255, 0);
    expect_pixel(surf, w - 1, h - 1, 255, 159, 10, 0);
    expect_pixel(surf, 25, 25, 14, 122, 255, 0);
    expect_pixel(surf, w / 2, h / 2, 0, 0, 0, 0);
    /* Background sample away from geometry */
    expect_pixel(surf, 10, h / 2, 245, 245, 247, 2);
}

/* ---- Tests ---- */

static void test_p7_version(void)
{
    ROBO_CHECK_STR_EQ(ROBO_WINDOW_VERSION_STRING, "0.15.0");
    ROBO_CHECK_STR_EQ(robo_window_version_string(), "0.15.0");
    ROBO_CHECK_EQ_INT(ROBO_WINDOW_API_FROZEN, 0);
}

static void test_p7_lifecycle_matrix(void)
{
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    size_t live0;

    p7_fixture();
    live0 = robo_mem_debug_is_enabled() ? robo_mem_debug_live_count() : 0u;

    /* create → destroy */
    id = p7_create_window(120, 80, 0.0f);
    ROBO_CHECK(robo_window_id_is_live(id));
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(!robo_window_id_is_live(id));

    /* create → draw → present → resize → redraw → close path */
    id = p7_create_window(160, 100, 0.0f);
    drain_events();
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    draw_phase7_pattern(surf);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_p7.present_count, 1);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 200, 140), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(surf), 200);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_height(surf), 140);
    draw_phase7_pattern(surf);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);

    /* repeated create/destroy cycles */
    {
        int i;
        for (i = 0; i < 8; i++) {
            id = p7_create_window(64 + i, 48 + i, 0.0f);
            ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
            (void)robo_gfx_surface_clear(
                surf, (robo_gfx_color_rgba8_t){0, 0, 0, 255});
            ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
        }
    }

    ROBO_CHECK_EQ_INT((int)robo_window_live_count(), 0);
    if (robo_mem_debug_is_enabled()) {
        ROBO_CHECK_EQ_INT((int)robo_mem_debug_live_count(), (int)live0);
    }
    p7_end();
}

static void test_p7_graphics_integration(void)
{
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_surface_t *shot_surf;

    p7_fixture();
    id = p7_create_window(128, 96, 0.0f);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    draw_phase7_pattern(surf);
    validate_phase7_pattern(surf);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_capture_surface(surf, NULL, &shot),
                      ROBO_OK);
    shot_surf = robo_gfx_screenshot_surface(shot);
    validate_phase7_pattern(shot_surf);
    robo_gfx_screenshot_release(shot);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    p7_end();
}

static void test_p7_scale_physical(void)
{
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_size_t logical;
    robo_gfx_size_t physical;
    float scale = 0.0f;

    p7_fixture();
    id = p7_create_window(100, 50, 2.0f);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &logical), ROBO_OK);
    ROBO_CHECK_EQ_INT(logical.width, 100);
    ROBO_CHECK_EQ_INT(logical.height, 50);
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &scale), ROBO_OK);
    ROBO_CHECK(scale > 1.9f && scale < 2.1f);
    ROBO_CHECK_EQ_INT(robo_window_get_size_physical(id, &physical), ROBO_OK);
    ROBO_CHECK_EQ_INT(physical.width, 200);
    ROBO_CHECK_EQ_INT(physical.height, 100);

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(surf), 200);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_height(surf), 100);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    p7_end();
}

static void test_p7_resize_safety(void)
{
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_surface_t *old = NULL;
    int i;

    p7_fixture();
    id = p7_create_window(80, 60, 0.0f);

    /* Resize before first frame */
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 90, 70), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(surf), 90);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    /* Repeated resize while dirty */
    ROBO_CHECK_EQ_INT(robo_window_invalidate(id), ROBO_OK);
    for (i = 0; i < 5; i++) {
        ROBO_CHECK_EQ_INT(robo_window_set_size(id, 100 + i * 10, 80 + i * 5),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(surf), 140);
    old = surf;
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 160, 120), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK(surf != old);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(surf), 160);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    /* Resize then close */
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 50, 40), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(robo_window_get_render_surface(id) == NULL);
    p7_end();
}

static void test_p7_stale_events(void)
{
    robo_window_id_t a;
    robo_window_id_t b;
    robo_keyboard_event_t ke;
    robo_mouse_event_t me;
    robo_window_event_t ev;
    int saw_closed_a = 0;
    int saw_key_for_a = 0;
    int saw_key_for_b = 0;

    p7_fixture();
    a = p7_create_window(100, 80, 0.0f);
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(a, 1), ROBO_OK);
    drain_events();

    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ke.physical_key = ROBO_PHYSICAL_KEY_A;
    ke.logical_key = ROBO_LOGICAL_KEY_A;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(a, &ke), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_window_event_queue_size() >= 1, 1);

    /* Destroy A: pending key for A must be purged; CLOSED remains. */
    ROBO_CHECK_EQ_INT(robo_window_destroy(a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_observe_key(a, &ke), ROBO_NOT_FOUND);

    b = p7_create_window(100, 80, 0.0f);
    ROBO_CHECK(b != a);
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(b, 1), ROBO_OK);

    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == ROBO_WINDOW_EVENT_CLOSED && ev.window_id == a) {
            saw_closed_a = 1;
        }
        if ((ev.type == ROBO_WINDOW_EVENT_KEY_DOWN ||
             ev.type == ROBO_WINDOW_EVENT_KEY_UP) &&
            ev.window_id == a) {
            saw_key_for_a = 1;
        }
        if ((ev.type == ROBO_WINDOW_EVENT_KEY_DOWN ||
             ev.type == ROBO_WINDOW_EVENT_KEY_UP) &&
            ev.window_id == b) {
            saw_key_for_b = 1;
        }
    }
    ROBO_CHECK(saw_closed_a);
    ROBO_CHECK(!saw_key_for_a);
    ROBO_CHECK(!saw_key_for_b); /* focus events drained; no key for B yet */

    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ke.physical_key = ROBO_PHYSICAL_KEY_B;
    ke.logical_key = ROBO_LOGICAL_KEY_B;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(b, &ke), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK(ev.window_id == b);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_KEY_DOWN);

    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_MOVE;
    me.client_x = 10;
    me.client_y = 12;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(a, &me), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(b, &me), ROBO_OK);

    ROBO_CHECK(robo_window_get_render_surface(a) == NULL);
    ROBO_CHECK_EQ_INT(robo_window_destroy(b), ROBO_OK);
    p7_end();
}

static void test_p7_input_integration(void)
{
    robo_window_id_t id;
    robo_keyboard_event_t ke;
    robo_mouse_event_t me;
    robo_window_event_t ev;

    p7_fixture();
    id = p7_create_window(200, 150, 0.0f);
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(id, 1), ROBO_OK);
    drain_events();

    /* Keyboard: press/release + modifiers */
    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ke.physical_key = ROBO_PHYSICAL_KEY_ENTER;
    ke.logical_key = ROBO_LOGICAL_KEY_ENTER;
    ke.modifiers = ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_CTRL;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_KEY_DOWN);
    ROBO_CHECK(ev.window_id == id);

    ke.action = ROBO_KEY_ACTION_UP;
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_KEY_UP);

    /* Mouse move / button / wheel */
    robo_mouse_event_init(&me);
    me.type = ROBO_MOUSE_EVENT_MOVE;
    me.client_x = 40;
    me.client_y = 50;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOUSE_MOVE);
    ROBO_CHECK_EQ_INT(ev.mouse.client_x, 40);
    ROBO_CHECK_EQ_INT(ev.mouse.client_y, 50);

    me.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
    me.button = ROBO_MOUSE_BUTTON_LEFT;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN);

    me.type = ROBO_MOUSE_EVENT_WHEEL;
    me.scroll_y = -120;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_poll(&ev), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)ev.type, (int)ROBO_WINDOW_EVENT_MOUSE_WHEEL);

    /* Resize then mouse coords still accepted */
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 300, 200), ROBO_OK);
    me.type = ROBO_MOUSE_EVENT_MOVE;
    me.client_x = 290;
    me.client_y = 190;
    me.button = ROBO_MOUSE_BUTTON_NONE;
    me.scroll_y = 0;
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_NOT_FOUND);
    p7_end();
}

static void test_p7_multi_window(void)
{
    robo_window_id_t a;
    robo_window_id_t b;
    robo_window_id_t c;
    robo_gfx_surface_t *sa = NULL;
    robo_gfx_surface_t *sb = NULL;
    robo_gfx_surface_t *sc = NULL;

    p7_fixture();
    a = p7_create_window(80, 60, 0.0f);
    b = p7_create_window(100, 70, 0.0f);
    c = p7_create_window(120, 90, 0.0f);
    ROBO_CHECK(a != b && b != c && a != c);
    ROBO_CHECK_EQ_INT((int)robo_window_live_count(), 3);

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(a, &sa), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(b, &sb), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(c, &sc), ROBO_OK);
    ROBO_CHECK(sa != sb && sb != sc);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(sa), 80);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(sb), 100);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(sc), 120);
    (void)robo_gfx_surface_clear(sa, (robo_gfx_color_rgba8_t){255, 0, 0, 255});
    (void)robo_gfx_surface_clear(sb, (robo_gfx_color_rgba8_t){0, 255, 0, 255});
    (void)robo_gfx_surface_clear(sc, (robo_gfx_color_rgba8_t){0, 0, 255, 255});
    ROBO_CHECK_EQ_INT(robo_window_end_frame(a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(b), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(c), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_destroy(b), ROBO_OK);
    ROBO_CHECK(robo_window_id_is_live(a));
    ROBO_CHECK(robo_window_id_is_live(c));
    ROBO_CHECK(!robo_window_id_is_live(b));

    ROBO_CHECK_EQ_INT(robo_window_destroy(c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(a), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_window_live_count(), 0);
    p7_end();
}

static void test_p7_failure_paths(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_gfx_surface_t *surf = NULL;

    p7_fixture();
    g_p7.fail_create = 1;
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 64;
    cfg.height = 64;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_window_live_count(), 0);
    g_p7.fail_create = 0;

    id = p7_create_window(64, 48, 0.0f);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    g_p7.fail_present = 1;
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_IO_ERROR);
    ROBO_CHECK(robo_window_is_dirty(id)); /* dirty retained on failure */
    g_p7.fail_present = 0;
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK(!robo_window_is_dirty(id));

    /* Present while frame open */
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    p7_end();

    /* Shutdown with live windows */
    p7_fixture();
    id = p7_create_window(32, 32, 0.0f);
    ROBO_CHECK_EQ_INT((int)robo_window_live_count(), 1);
    p7_end();
    ROBO_CHECK_EQ_INT((int)robo_window_live_count(), 0);
}

static void test_p7_shutdown_matrix(void)
{
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_keyboard_event_t ke;

    /* Idle */
    p7_fixture();
    p7_end();

    /* Active rendering */
    p7_fixture();
    id = p7_create_window(40, 30, 0.0f);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    p7_end();

    /* After input */
    p7_fixture();
    id = p7_create_window(40, 30, 0.0f);
    ROBO_CHECK_EQ_INT(robo_window_observe_focus(id, 1), ROBO_OK);
    robo_keyboard_event_init(&ke);
    ke.action = ROBO_KEY_ACTION_DOWN;
    ke.physical_key = ROBO_PHYSICAL_KEY_ESCAPE;
    ke.logical_key = ROBO_LOGICAL_KEY_ESCAPE;
    (void)robo_window_observe_key(id, &ke);
    p7_end();
    ROBO_CHECK(!robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_ESCAPE));

    /* After resize */
    p7_fixture();
    id = p7_create_window(40, 30, 0.0f);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 80, 60), ROBO_OK);
    p7_end();
}

static void test_p7_null_backend_contract(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;

    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_use_null(), ROBO_OK);
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 32;
    cfg.height = 32;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    /* Null backend: present unsupported / no-op path */
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, NULL), ROBO_INVALID_ARGUMENT);
    {
        robo_gfx_surface_t *surf = NULL;
        ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
        ROBO_CHECK(surf != NULL);
        ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
        /* present may be NOT_SUPPORTED on null */
        {
            robo_result_t r = robo_window_present_frame(id);
            ROBO_CHECK(r == ROBO_OK || r == ROBO_NOT_SUPPORTED);
        }
    }
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_p7_qemu_sim_contract(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_result_t r;

    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    r = robo_window_backend_use_qemu_sim(320, 200, 0u); /* ROBO_QEMU_PIX_BGRX */
    if (r != ROBO_OK) {
        robo_window_shutdown();
        ROBO_CHECK_MSG(0, "qemu_sim unavailable");
        return;
    }
    robo_window_config_init(&cfg);
    cfg.width = 320;
    cfg.height = 200;
    cfg.visible = 1;
    cfg.title = "p7-qemu-sim";
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    draw_phase7_pattern(surf);
    validate_phase7_pattern(surf);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_p7_event_loop_with_frame(void)
{
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_window_event_t ev;
    int saw_resize = 0;

    p7_fixture();
    id = p7_create_window(100, 80, 0.0f);
    drain_events();

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){1, 2, 3, 255});
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_observe_size(id, 140, 100), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_process_events(), ROBO_OK);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == ROBO_WINDOW_EVENT_RESIZED && ev.window_id == id) {
            saw_resize = 1;
            ROBO_CHECK_EQ_INT(ev.width, 140);
            ROBO_CHECK_EQ_INT(ev.height, 100);
        }
    }
    ROBO_CHECK(saw_resize);

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(surf), 140);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    p7_end();
}

static void test_p7_perf_sanity(void)
{
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    clock_t t0;
    clock_t t1;
    int i;

    p7_fixture();
    id = p7_create_window(128, 96, 0.0f);
    t0 = clock();
    for (i = 0; i < 40; i++) {
        ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
        (void)robo_gfx_surface_fill_rect(
            surf, robo_gfx_rect(0, 0, 128, 96),
            (robo_gfx_color_rgba8_t){(uint8_t)i, 40, 80, 255});
        ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
        if ((i % 5) == 0) {
            ROBO_CHECK_EQ_INT(robo_window_set_size(id, 128 + (i % 3), 96),
                              ROBO_OK);
        }
    }
    t1 = clock();
    ROBO_CHECK(t1 >= t0);
    ROBO_CHECK((t1 - t0) < 20 * CLOCKS_PER_SEC);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    p7_end();
}

static void test_p7_host_acceptance(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_result_t r;
    int i;

    if (!robo_window_backend_host_available()) {
        /* Pass when host GUI is unavailable (guest / CI without Win32). */
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    r = robo_window_backend_use_host();
    if (r != ROBO_OK) {
        robo_window_shutdown();
        return;
    }

    robo_window_config_init(&cfg);
    cfg.width = 320;
    cfg.height = 240;
    cfg.visible = 1;
    cfg.title = "Roboticela Phase 7.15";
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    for (i = 0; i < 3; i++) {
        ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
        draw_phase7_pattern(surf);
        validate_phase7_pattern(surf);
        ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_process_events(), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 400, 300), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    draw_phase7_pattern(surf);
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

void robo_test_register_window_phase7(void)
{
    robo_test_register("window.p7.version", test_p7_version);
    robo_test_register("window.p7.lifecycle", test_p7_lifecycle_matrix);
    robo_test_register("window.p7.graphics", test_p7_graphics_integration);
    robo_test_register("window.p7.scale", test_p7_scale_physical);
    robo_test_register("window.p7.resize", test_p7_resize_safety);
    robo_test_register("window.p7.stale", test_p7_stale_events);
    robo_test_register("window.p7.input", test_p7_input_integration);
    robo_test_register("window.p7.multi", test_p7_multi_window);
    robo_test_register("window.p7.failures", test_p7_failure_paths);
    robo_test_register("window.p7.shutdown", test_p7_shutdown_matrix);
    robo_test_register("window.p7.null_contract", test_p7_null_backend_contract);
    robo_test_register("window.p7.qemu_sim", test_p7_qemu_sim_contract);
    robo_test_register("window.p7.loop_frame", test_p7_event_loop_with_frame);
    robo_test_register("window.p7.perf", test_p7_perf_sanity);
    robo_test_register("window.p7.host", test_p7_host_acceptance);
}
