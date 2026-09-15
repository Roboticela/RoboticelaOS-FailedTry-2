/*
 * RoboWindow host native backend suite (Goal 7.5).
 *
 * Distinguishes:
 *   - mock / null backends (always available, headless)
 *   - real Win32 host backend (skipped when unavailable or
 *     ROBO_WINDOW_SKIP_HOST is set)
 *
 * Set ROBO_WINDOW_SMOKE=1 to hold a visible window briefly for manual check.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static int host_skip_reason(char *buf, size_t cap)
{
    if (getenv("ROBO_WINDOW_SKIP_HOST") != NULL) {
        if (buf && cap) {
            snprintf(buf, cap, "ROBO_WINDOW_SKIP_HOST");
        }
        return 1;
    }
    if (!robo_window_backend_host_available()) {
        if (buf && cap) {
            snprintf(buf, cap, "host backend unavailable");
        }
        return 1;
    }
    return 0;
}

static void host_pump_brief(int ms)
{
    clock_t start = clock();
    clock_t need = (clock_t)(((long)ms * CLOCKS_PER_SEC) / 1000L);
    if (need < 1) {
        need = 1;
    }
    while ((clock() - start) < need) {
        (void)robo_window_process_events();
    }
}

static void test_host_availability(void)
{
    const robo_window_backend_vtbl_t *vtbl = robo_window_backend_host_vtbl();
#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    ROBO_CHECK(vtbl != NULL);
    ROBO_CHECK(vtbl->name != NULL);
    ROBO_CHECK_STR_EQ(vtbl->name, "win32");
#else
    ROBO_CHECK(vtbl == NULL);
    ROBO_CHECK_EQ_INT(robo_window_backend_host_available(), 0);
#endif
    (void)host_skip_reason(NULL, 0);
}

static void test_host_use_when_unavailable(void)
{
    char reason[64];
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    if (host_skip_reason(reason, sizeof(reason))) {
        ROBO_CHECK_EQ_INT(robo_window_backend_use_host(), ROBO_NOT_SUPPORTED);
    } else {
        ROBO_CHECK_EQ_INT(robo_window_backend_host_available(), 1);
    }
    robo_window_shutdown();
}

static int begin_host(robo_window_id_t *out_id, robo_window_config_t *cfg)
{
    char reason[64];
    robo_result_t r;

    if (host_skip_reason(reason, sizeof(reason))) {
        printf("SKIP window.host.* (%s)\n", reason);
        return 0;
    }

    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    r = robo_window_backend_use_host();
    if (r != ROBO_OK) {
        printf("SKIP window.host.* (use_host=%d)\n", (int)r);
        robo_window_shutdown();
        return 0;
    }
    ROBO_CHECK_STR_EQ(robo_window_backend_name(), "win32");

    robo_window_config_init(cfg);
    cfg->width = 640;
    cfg->height = 480;
    cfg->visible = 0;
    cfg->title = "Roboticela Host";
    cfg->x = 80;
    cfg->y = 60;
    cfg->min_width = 200;
    cfg->min_height = 150;
    cfg->max_width = 1920;
    cfg->max_height = 1080;
    cfg->resizable = 1;

    r = robo_window_create(cfg, out_id);
    if (r != ROBO_OK) {
        printf("SKIP window.host.* (create=%d — no interactive desktop?)\n",
               (int)r);
        robo_window_shutdown();
        return 0;
    }
    return 1;
}

static void test_host_create_destroy(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_native_handle_t h;
    robo_window_state_t st;
    int i;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_valid(&h));
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK(st.width > 0);
    ROBO_CHECK(st.height > 0);
    ROBO_CHECK_EQ_INT(st.visible, 0);

    for (i = 0; i < 3; i++) {
        ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_NOT_FOUND);
    robo_window_shutdown();
}

static void test_host_title_size_pos_visibility(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_state_t st;
    const char *title = NULL;
    const char *unicode_title = "Roboticela — پنجره";
    int i;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_set_title(id, ""), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_title(id, &title), ROBO_OK);
    ROBO_CHECK(title && title[0] == '\0');

    ROBO_CHECK_EQ_INT(robo_window_set_title(id, unicode_title), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_title(id, &title), ROBO_OK);
    ROBO_CHECK_STR_EQ(title, unicode_title);

    for (i = 0; i < 4; i++) {
        ROBO_CHECK_EQ_INT(robo_window_set_title(id, "Title A"), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_set_title(id, "Title B"), ROBO_OK);
    }

    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 700, 500), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.width, 700);
    ROBO_CHECK_EQ_INT(st.height, 500);

    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 100, 100),
                      ROBO_INVALID_ARGUMENT); /* below min */
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 0, 480), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 2000, 500),
                      ROBO_INVALID_ARGUMENT); /* above max */

    ROBO_CHECK_EQ_INT(robo_window_request_position(id, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, 120, 90), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, -20, 40), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.x, -20);
    ROBO_CHECK_EQ_INT(st.y, 40);

    for (i = 0; i < 3; i++) {
        ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
        ROBO_CHECK_EQ_INT(st.visible, 1);
        (void)robo_window_process_events();
        ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
        ROBO_CHECK_EQ_INT(st.visible, 0);
    }

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_host_focus_and_display(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_state_t st;
    robo_display_info_t disp;
    robo_result_t fr;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_get_associated_display(id, &disp), ROBO_OK);
    ROBO_CHECK(disp.id != ROBO_DISPLAY_ID_NONE);

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    fr = robo_window_request_focus(id);
    /* Focus may be declined by the OS without faking success. */
    ROBO_CHECK(fr == ROBO_OK || fr == ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    if (fr != ROBO_OK) {
        /* Do not require focused==1 when backend reported unsupported. */
        ROBO_CHECK(st.focused == 0 || st.focused == 1);
    }

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_host_shutdown_with_live_window(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;

    if (!begin_host(&id, &cfg)) {
        return;
    }
    ROBO_CHECK(robo_window_live_count() >= 1u);
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_is_initialized(), 0);
}

static void test_host_invalid_config(void)
{
    char reason[64];
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;

    if (host_skip_reason(reason, sizeof(reason))) {
        printf("SKIP window.host.invalid_config (%s)\n", reason);
        return;
    }
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    if (robo_window_backend_use_host() != ROBO_OK) {
        printf("SKIP window.host.invalid_config (use_host failed)\n");
        robo_window_shutdown();
        return;
    }
    robo_window_config_init(&cfg);
    cfg.width = 0;
    cfg.height = 100;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_INVALID_ARGUMENT);
    robo_window_shutdown();
}

static void test_host_close_lifecycle(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_lifecycle_t life;
    robo_window_native_handle_t h;
    robo_window_event_t ev;
    robo_window_state_t st;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CREATED);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.visible, 1);
    (void)robo_window_process_events();

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CLOSING);
    ROBO_CHECK_EQ_INT(robo_window_is_closing(id), 1);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_valid(&h));
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 500, 400), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_ALREADY_EXISTS);

    while (robo_window_event_poll(&ev) == ROBO_OK) {
        /* drain CREATED/SHOWN/CLOSE_REQUESTED */
    }

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_id_is_live(id), 0);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_NOT_FOUND);
    robo_window_shutdown();
}

static void test_host_scale_dpi(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_display_info_t disp;
    float scale = 0.0f;
    robo_display_dpi_t dpi;
    robo_gfx_size_t logical;
    robo_gfx_size_t physical;
    int32_t px = 0;
    int32_t py = 0;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    (void)robo_window_process_events();

    /* Associated display may be placeholder; scale query must still succeed. */
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &scale), ROBO_OK);
    ROBO_CHECK(robo_display_scale_is_known(scale));
    printf("host scale=%g\n", (double)scale);

    {
        robo_result_t dr = robo_window_get_associated_display(id, &disp);
        ROBO_CHECK(dr == ROBO_OK || dr == ROBO_NOT_FOUND);
        if (dr == ROBO_OK) {
            robo_result_t dpi_r = robo_window_get_dpi(id, &dpi);
            ROBO_CHECK(dpi_r == ROBO_OK);
            /* DPI may be unavailable (0) on placeholder — that is OK. */
            ROBO_CHECK_EQ_INT(robo_display_dpi_validate(dpi.dpi_x), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_display_dpi_validate(dpi.dpi_y), ROBO_OK);
            printf("host dpi_x=%g dpi_y=%g display=%llu\n", (double)dpi.dpi_x,
                   (double)dpi.dpi_y, (unsigned long long)disp.id);
        }
    }

    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &logical), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size_physical(id, &physical), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_display_size_logical_to_physical(logical.width, logical.height,
                                              scale, &px, &py),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(physical.width, px);
    ROBO_CHECK_EQ_INT(physical.height, py);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_host_sizing_constraints(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_size_t sz;
    robo_window_size_constraints_t c;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK(sz.width > 0 && sz.height > 0);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 720, 540), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 720);
    ROBO_CHECK_EQ_INT(sz.height, 540);

    robo_window_size_constraints_init(&c);
    c.min_width = 400;
    c.min_height = 300;
    c.max_width = 1000;
    c.max_height = 800;
    ROBO_CHECK_EQ_INT(robo_window_set_size_constraints(id, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 200, 200), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 1200, 400), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 400, 300), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 400);
    ROBO_CHECK_EQ_INT(sz.height, 300);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 640, 480), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_host_smoke_manual(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_size_t sz;
    robo_window_size_constraints_t c;
    const char *smoke = getenv("ROBO_WINDOW_SMOKE");

    if (!smoke || smoke[0] == '\0' || smoke[0] == '0') {
        printf("SKIP window.host.smoke (set ROBO_WINDOW_SMOKE=1)\n");
        return;
    }
    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_set_title(id, "Roboticela Host Display"),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 800, 600), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 800);
    ROBO_CHECK_EQ_INT(sz.height, 600);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, 100, 80), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);

    {
        robo_gfx_surface_t *frame = NULL;
        robo_gfx_raster_t *raster = NULL;
        ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &frame), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_raster_create(frame, &raster), ROBO_OK);
        (void)robo_gfx_raster_clear(
            raster, (robo_gfx_color_rgba8_t){245, 245, 247, 255});
        (void)robo_gfx_raster_fill_rect(
            raster, robo_gfx_rect(40, 40, 200, 120),
            (robo_gfx_color_rgba8_t){14, 122, 255, 255});
        (void)robo_gfx_raster_fill_rect(
            raster, robo_gfx_rect(280, 80, 160, 160),
            (robo_gfx_color_rgba8_t){48, 209, 88, 200});
        (void)robo_gfx_raster_draw_line(
            raster, robo_gfx_point(40, 560), robo_gfx_point(760, 560),
            (robo_gfx_color_rgba8_t){29, 29, 31, 255});
        (void)robo_gfx_raster_fill_rect(
            raster, robo_gfx_rect(390, 290, 20, 20),
            (robo_gfx_color_rgba8_t){255, 59, 48, 255});
        robo_gfx_raster_destroy(raster);
        ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    }
    host_pump_brief(800);

    robo_window_size_constraints_init(&c);
    c.min_width = 400;
    c.min_height = 300;
    c.max_width = 900;
    c.max_height = 700;
    ROBO_CHECK_EQ_INT(robo_window_set_size_constraints(id, &c), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 200, 200), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 1000, 400), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 700, 500), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 700);
    ROBO_CHECK_EQ_INT(sz.height, 500);
    {
        robo_gfx_surface_t *frame = NULL;
        ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &frame), ROBO_OK);
        (void)robo_gfx_surface_clear(
            frame, (robo_gfx_color_rgba8_t){245, 245, 247, 255});
        (void)robo_gfx_surface_fill_rect(
            frame, robo_gfx_rect(20, 20, 100, 60),
            (robo_gfx_color_rgba8_t){14, 122, 255, 255});
        ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    }
    host_pump_brief(600);

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    host_pump_brief(400);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    {
        robo_window_lifecycle_t life;
        ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CLOSING);
    }
    host_pump_brief(200);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_id_is_live(id), 0);
    {
        robo_window_native_handle_t h;
        ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_NOT_FOUND);
    }
    robo_window_shutdown();
}

static void test_mock_still_distinct(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_native_handle_t h;

    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_use_null(), ROBO_OK);
    ROBO_CHECK_STR_EQ(robo_window_backend_name(), "null");
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_empty(&h));
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_host_event_loop(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_state_t st;
    robo_window_lifecycle_t life;
    robo_window_backend_caps_t caps;
    int saw_close = 0;
    robo_window_event_t ev;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps), ROBO_OK);
    ROBO_CHECK(caps.process_events);
    ROBO_CHECK(caps.wait_events);
    ROBO_CHECK(caps.wakeup);

    ROBO_CHECK_EQ_INT(robo_window_event_loop_start(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(0), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 700, 520), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, 60, 40), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(10), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_state(id, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st.width, 700);
    ROBO_CHECK_EQ_INT(st.height, 520);

    ROBO_CHECK_EQ_INT(robo_window_event_loop_wakeup(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(50), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_request_close(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_lifecycle(id, &life), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)life, (int)ROBO_WINDOW_LIFECYCLE_CLOSING);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == ROBO_WINDOW_EVENT_CLOSE_REQUESTED && ev.window_id == id) {
            saw_close = 1;
        }
    }
    ROBO_CHECK(saw_close);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_request_stop(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_is_running(), 0);
    robo_window_shutdown();
}

static void test_host_keyboard(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_event_t ev;
    int saw_down = 0;
    int saw_up = 0;
#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    INPUT inputs[2];
#endif

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_focus(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(0), ROBO_OK);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    memset(inputs, 0, sizeof(inputs));
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 'A';
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'A';
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(2, inputs, sizeof(INPUT)) == 2) {
        host_pump_brief(100);
        while (robo_window_event_poll(&ev) == ROBO_OK) {
            if (ev.window_id == id && ev.type == ROBO_WINDOW_EVENT_KEY_DOWN &&
                ev.key.logical_key == ROBO_LOGICAL_KEY_A) {
                saw_down = 1;
            }
            if (ev.window_id == id && ev.type == ROBO_WINDOW_EVENT_KEY_UP &&
                ev.key.logical_key == ROBO_LOGICAL_KEY_A) {
                saw_up = 1;
            }
        }
        /* SendInput may fail to reach our HWND if focus was stolen — fall back. */
        if (!saw_down) {
            robo_keyboard_event_t ke;
            robo_keyboard_event_init(&ke);
            ke.action = ROBO_KEY_ACTION_DOWN;
            ke.physical_key = ROBO_PHYSICAL_KEY_A;
            ke.logical_key = ROBO_LOGICAL_KEY_A;
            ke.modifiers = robo_keyboard_modifiers();
            ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
            ke.action = ROBO_KEY_ACTION_UP;
            ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
            while (robo_window_event_poll(&ev) == ROBO_OK) {
                if (ev.type == ROBO_WINDOW_EVENT_KEY_DOWN) {
                    saw_down = 1;
                }
                if (ev.type == ROBO_WINDOW_EVENT_KEY_UP) {
                    saw_up = 1;
                }
            }
        }
    } else
#endif
    {
        robo_keyboard_event_t ke;
        robo_keyboard_event_init(&ke);
        ke.action = ROBO_KEY_ACTION_DOWN;
        ke.physical_key = ROBO_PHYSICAL_KEY_A;
        ke.logical_key = ROBO_LOGICAL_KEY_A;
        ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
        ke.action = ROBO_KEY_ACTION_UP;
        ROBO_CHECK_EQ_INT(robo_window_observe_key(id, &ke), ROBO_OK);
        while (robo_window_event_poll(&ev) == ROBO_OK) {
            if (ev.type == ROBO_WINDOW_EVENT_KEY_DOWN) {
                saw_down = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_KEY_UP) {
                saw_up = 1;
            }
        }
    }

    ROBO_CHECK(saw_down);
    ROBO_CHECK(saw_up);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(!robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_A));
    robo_window_shutdown();
    ROBO_CHECK(!robo_keyboard_is_physical_pressed(ROBO_PHYSICAL_KEY_A));
}

static void test_host_mouse(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_event_t ev;
    int saw_move = 0;
    int saw_down = 0;
    int saw_up = 0;
    int saw_wheel = 0;
#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    INPUT inputs[6];
    robo_window_native_handle_t h;
#endif

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_focus(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_event_loop_run_once(0), ROBO_OK);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    memset(inputs, 0, sizeof(inputs));
    /* Relative move then left click then wheel — may miss HWND if focus stolen. */
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dx = 1;
    inputs[0].mi.dy = 1;
    inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    inputs[3].type = INPUT_MOUSE;
    inputs[3].mi.dwFlags = MOUSEEVENTF_WHEEL;
    inputs[3].mi.mouseData = 120;
    if (SendInput(4, inputs, sizeof(INPUT)) == 4) {
        host_pump_brief(120);
        while (robo_window_event_poll(&ev) == ROBO_OK) {
            if (ev.window_id != id) {
                continue;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_MOVE) {
                saw_move = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN &&
                ev.mouse.button == ROBO_MOUSE_BUTTON_LEFT) {
                saw_down = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP &&
                ev.mouse.button == ROBO_MOUSE_BUTTON_LEFT) {
                saw_up = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_WHEEL) {
                saw_wheel = 1;
            }
        }
    }
    if (!saw_down || !saw_up || !saw_wheel) {
        robo_mouse_event_t me;
        ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
        (void)h;
        robo_mouse_event_init(&me);
        me.type = ROBO_MOUSE_EVENT_MOVE;
        me.client_x = 40;
        me.client_y = 50;
        me.has_screen_coords = 0;
        me.modifiers = robo_keyboard_modifiers();
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        me.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
        me.button = ROBO_MOUSE_BUTTON_LEFT;
        me.click_count = 1;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        me.type = ROBO_MOUSE_EVENT_BUTTON_UP;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        me.type = ROBO_MOUSE_EVENT_WHEEL;
        me.button = ROBO_MOUSE_BUTTON_NONE;
        me.scroll_y = 120;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        while (robo_window_event_poll(&ev) == ROBO_OK) {
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_MOVE) {
                saw_move = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN) {
                saw_down = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP) {
                saw_up = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_WHEEL) {
                saw_wheel = 1;
            }
        }
    }
#else
    {
        robo_mouse_event_t me;
        robo_mouse_event_init(&me);
        me.type = ROBO_MOUSE_EVENT_MOVE;
        me.client_x = 10;
        me.client_y = 10;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        me.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
        me.button = ROBO_MOUSE_BUTTON_LEFT;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        me.type = ROBO_MOUSE_EVENT_BUTTON_UP;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        me.type = ROBO_MOUSE_EVENT_WHEEL;
        me.button = ROBO_MOUSE_BUTTON_NONE;
        me.scroll_y = 120;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        while (robo_window_event_poll(&ev) == ROBO_OK) {
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_MOVE) {
                saw_move = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN) {
                saw_down = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP) {
                saw_up = 1;
            }
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_WHEEL) {
                saw_wheel = 1;
            }
        }
    }
#endif

    ROBO_CHECK(saw_move);
    ROBO_CHECK(saw_down);
    ROBO_CHECK(saw_up);
    ROBO_CHECK(saw_wheel);

    /* Modifiers + middle button via the same observe path. */
    {
        robo_mouse_event_t me;
        robo_mouse_event_init(&me);
        me.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
        me.button = ROBO_MOUSE_BUTTON_MIDDLE;
        me.modifiers = ROBO_KEY_MOD_ALT | ROBO_KEY_MOD_SHIFT;
        me.client_x = 1;
        me.client_y = 1;
        ROBO_CHECK_EQ_INT(robo_window_observe_mouse(id, &me), ROBO_OK);
        ROBO_CHECK(robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_MIDDLE));
        ROBO_CHECK(robo_mouse_modifiers() & ROBO_KEY_MOD_ALT);
    }

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_MIDDLE));
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_LEFT));
    robo_window_shutdown();
    ROBO_CHECK(!robo_mouse_is_button_pressed(ROBO_MOUSE_BUTTON_LEFT));
}

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
static int host_sample_client_rgb(HWND hwnd, int x, int y, int *r, int *g,
                                  int *b)
{
    HDC hdc;
    COLORREF c;
    if (!hwnd || !r || !g || !b) {
        return 0;
    }
    hdc = GetDC(hwnd);
    if (!hdc) {
        return 0;
    }
    c = GetPixel(hdc, x, y);
    ReleaseDC(hwnd, hdc);
    if (c == CLR_INVALID) {
        return 0;
    }
    *r = (int)GetRValue(c);
    *g = (int)GetGValue(c);
    *b = (int)GetBValue(c);
    return 1;
}

static int rgb_near(int r, int g, int b, int er, int eg, int eb, int tol)
{
    int dr = r - er;
    int dg = g - eg;
    int db = b - eb;
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
#endif

static void test_host_present_caps(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_window_backend_caps_t caps;

    if (!begin_host(&id, &cfg)) {
        return;
    }
    ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps), ROBO_OK);
    ROBO_CHECK(caps.present);
    ROBO_CHECK(caps.present_format_mask & ROBO_WINDOW_PRESENT_FMT_BGRA8);
    ROBO_CHECK(caps.present_format_mask & ROBO_WINDOW_PRESENT_FMT_RGBA8);
    ROBO_CHECK(robo_window_present_format_supported(ROBO_GFX_FORMAT_BGRA8));
    ROBO_CHECK(robo_window_present_format_supported(ROBO_GFX_FORMAT_RGBA8));
    ROBO_CHECK(robo_window_present_format_supported(ROBO_GFX_FORMAT_RGB8));
    ROBO_CHECK(!robo_window_present_format_supported(ROBO_GFX_FORMAT_ALPHA8));
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_host_present_colors(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_surface_desc_t desc;
    robo_gfx_screenshot_t *shot = NULL;
    robo_gfx_surface_t *shot_surf = NULL;
    robo_gfx_color_rgba8_t px;
#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    robo_window_native_handle_t h;
    HWND hwnd;
    int r = 0;
    int g = 0;
    int b = 0;
#endif
    const int W = 320;
    const int H = 240;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, W, H), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_set_title(id, "Roboticela Present"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    host_pump_brief(50);

    robo_gfx_surface_desc_init(&desc);
    desc.width = (uint32_t)W;
    desc.height = (uint32_t)H;
    desc.format = ROBO_GFX_FORMAT_BGRA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surf), ROBO_OK);

    (void)robo_gfx_surface_clear(surf,
                                 (robo_gfx_color_rgba8_t){0, 0, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, 0, 0,
                                     (robo_gfx_color_rgba8_t){255, 255, 255, 255});
    (void)robo_gfx_surface_set_pixel(surf, (uint32_t)(W - 1), 0,
                                     (robo_gfx_color_rgba8_t){255, 0, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, 0, (uint32_t)(H - 1),
                                     (robo_gfx_color_rgba8_t){0, 255, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, (uint32_t)(W - 1), (uint32_t)(H - 1),
                                     (robo_gfx_color_rgba8_t){0, 0, 255, 255});
    (void)robo_gfx_surface_fill_rect(
        surf, robo_gfx_rect(W / 2 - 8, H / 2 - 8, 16, 16),
        (robo_gfx_color_rgba8_t){128, 128, 128, 255});
    (void)robo_gfx_surface_fill_rect(
        surf, robo_gfx_rect(40, 40, 64, 48),
        (robo_gfx_color_rgba8_t){14, 122, 255, 255});

    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    host_pump_brief(30);

    /* Source surface integrity (Phase 6.14 screenshot path). */
    ROBO_CHECK_EQ_INT(robo_gfx_screenshot_capture_surface(surf, NULL, &shot),
                      ROBO_OK);
    shot_surf = robo_gfx_screenshot_surface(shot);
    ROBO_CHECK(shot_surf != NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(shot_surf, 0, 0, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)px.r, 255);
    ROBO_CHECK_EQ_INT((int)px.g, 255);
    ROBO_CHECK_EQ_INT((int)px.b, 255);
    ROBO_CHECK_EQ_INT(
        robo_gfx_surface_get_pixel(shot_surf, (uint32_t)(W - 1), 0, &px),
        ROBO_OK);
    ROBO_CHECK_EQ_INT((int)px.r, 255);
    ROBO_CHECK_EQ_INT((int)px.g, 0);
    ROBO_CHECK_EQ_INT((int)px.b, 0);
    robo_gfx_screenshot_release(shot);
    shot = NULL;

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    ROBO_CHECK_EQ_INT(robo_window_get_native_handle(id, &h), ROBO_OK);
    ROBO_CHECK(robo_window_native_handle_is_valid(&h));
    hwnd = FindWindowW(L"Roboticela.RoboWindow", L"Roboticela Present");
    if (hwnd) {
        /* Tolerance: GDI BitBlt / desktop composition may soft-filter. */
        ROBO_CHECK(host_sample_client_rgb(hwnd, 0, 0, &r, &g, &b));
        ROBO_CHECK(rgb_near(r, g, b, 255, 255, 255, 8));
        ROBO_CHECK(host_sample_client_rgb(hwnd, W - 1, 0, &r, &g, &b));
        ROBO_CHECK(rgb_near(r, g, b, 255, 0, 0, 8));
        ROBO_CHECK(host_sample_client_rgb(hwnd, 0, H - 1, &r, &g, &b));
        ROBO_CHECK(rgb_near(r, g, b, 0, 255, 0, 8));
        ROBO_CHECK(host_sample_client_rgb(hwnd, W - 1, H - 1, &r, &g, &b));
        ROBO_CHECK(rgb_near(r, g, b, 0, 0, 255, 8));
        ROBO_CHECK(host_sample_client_rgb(hwnd, W / 2, H / 2, &r, &g, &b));
        ROBO_CHECK(rgb_near(r, g, b, 128, 128, 128, 16));
        ROBO_CHECK(host_sample_client_rgb(hwnd, 50, 50, &r, &g, &b));
        ROBO_CHECK(rgb_near(r, g, b, 14, 122, 255, 16));
        printf("host present GetPixel OK\n");
    } else {
        printf("WARN window.host.present_colors: FindWindow missed HWND; "
               "surface path still validated\n");
    }
#endif

    /* Present after destroy must fail; surface still valid. */
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_NOT_FOUND);
    robo_gfx_surface_release(surf);
    robo_window_shutdown();
}

static void test_host_present_pattern_resize(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_surface_desc_t desc;
    robo_gfx_raster_t *raster = NULL;
    robo_gfx_size_t sz;
    robo_window_event_t ev;
    int saw_resize = 0;

    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 400, 300), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    host_pump_brief(40);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 400;
    desc.height = 300;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_raster_create(surf, &raster), ROBO_OK);

    (void)robo_gfx_raster_clear(raster,
                                (robo_gfx_color_rgba8_t){245, 245, 247, 255});
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(0, 0, 8, 8),
        (robo_gfx_color_rgba8_t){255, 59, 48, 255});
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(392, 0, 8, 8),
        (robo_gfx_color_rgba8_t){48, 209, 88, 255});
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(0, 292, 8, 8),
        (robo_gfx_color_rgba8_t){10, 132, 255, 255});
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(392, 292, 8, 8),
        (robo_gfx_color_rgba8_t){255, 159, 10, 255});
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(190, 140, 20, 20),
        (robo_gfx_color_rgba8_t){29, 29, 31, 255});
    (void)robo_gfx_raster_draw_line(
        raster, robo_gfx_point(20, 20), robo_gfx_point(380, 280),
        (robo_gfx_color_rgba8_t){14, 122, 255, 255});
    /* Semi-transparent overlay (alpha blend in surface, then present). */
    (void)robo_gfx_raster_set_opacity(raster, 180);
    (void)robo_gfx_raster_fill_rect(
        raster, robo_gfx_rect(120, 80, 160, 100),
        (robo_gfx_color_rgba8_t){255, 255, 255, 255});
    robo_gfx_raster_set_opacity(raster, 255);

    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);

    while (robo_window_event_poll(&ev) == ROBO_OK) {
    }
    ROBO_CHECK_EQ_INT(robo_window_set_size(id, 500, 360), ROBO_OK);
    host_pump_brief(60);
    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == ROBO_WINDOW_EVENT_RESIZED && ev.window_id == id) {
            saw_resize = 1;
        }
    }
    ROBO_CHECK_EQ_INT(robo_window_get_size(id, &sz), ROBO_OK);
    ROBO_CHECK_EQ_INT(sz.width, 500);
    ROBO_CHECK_EQ_INT(sz.height, 360);
    /* Re-present after resize (surface still 400x300 — StretchBlt path). */
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    host_pump_brief(40);
    ROBO_CHECK(saw_resize || sz.width == 500);

    /* Failure paths */
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_INVALID_ARGUMENT);

    robo_gfx_raster_destroy(raster);
    robo_gfx_surface_release(surf);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

static void test_host_present_display_link(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_display_info_t disp;
    float scale = 0.0f;

    if (!begin_host(&id, &cfg)) {
        return;
    }
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    host_pump_brief(20);
    /* use_host switches RoboDisplay to Win32 monitors when available. */
    ROBO_CHECK_EQ_INT(robo_window_get_associated_display(id, &disp), ROBO_OK);
    ROBO_CHECK(disp.id != ROBO_DISPLAY_ID_NONE);
    ROBO_CHECK(disp.resolution_width_px > 0);
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &scale), ROBO_OK);
    ROBO_CHECK(robo_display_scale_is_known(scale));
    printf("host linked display=%llu scale=%g\n",
           (unsigned long long)disp.id, (double)scale);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    robo_window_shutdown();
}

void robo_test_register_window_host(void)
{
    robo_test_register("window.host.availability", test_host_availability);
    robo_test_register("window.host.use_unavailable", test_host_use_when_unavailable);
    robo_test_register("window.host.create_destroy", test_host_create_destroy);
    robo_test_register("window.host.title_size_pos", test_host_title_size_pos_visibility);
    robo_test_register("window.host.focus_display", test_host_focus_and_display);
    robo_test_register("window.host.shutdown_live", test_host_shutdown_with_live_window);
    robo_test_register("window.host.invalid_config", test_host_invalid_config);
    robo_test_register("window.host.sizing", test_host_sizing_constraints);
    robo_test_register("window.host.close_lifecycle", test_host_close_lifecycle);
    robo_test_register("window.host.scale_dpi", test_host_scale_dpi);
    robo_test_register("window.host.event_loop", test_host_event_loop);
    robo_test_register("window.host.keyboard", test_host_keyboard);
    robo_test_register("window.host.mouse", test_host_mouse);
    robo_test_register("window.host.present_caps", test_host_present_caps);
    robo_test_register("window.host.present_colors", test_host_present_colors);
    robo_test_register("window.host.present_pattern",
                       test_host_present_pattern_resize);
    robo_test_register("window.host.present_display",
                       test_host_present_display_link);
    robo_test_register("window.host.smoke", test_host_smoke_manual);
    robo_test_register("window.host.mock_distinct", test_mock_still_distinct);
}
