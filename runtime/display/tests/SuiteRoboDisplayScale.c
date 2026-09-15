/*
 * RoboDisplay / RoboWindow DPI & scale suite (Goal 7.8).
 * Headless mock displays — configurable scale/DPI/bounds.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#define MOCK_MAX 8

typedef struct scale_mock {
    robo_display_info_t displays[MOCK_MAX];
    size_t count;
} scale_mock_t;

static scale_mock_t g_sm;
static robo_display_backend_caps_t g_sm_caps;
static robo_display_backend_vtbl_t g_sm_vtbl;

static void fill_disp(robo_display_info_t *d,
                      robo_display_id_t id,
                      int32_t x,
                      int32_t y,
                      int32_t w,
                      int32_t h,
                      int32_t res_w,
                      int32_t res_h,
                      float scale,
                      float dpi_x,
                      float dpi_y,
                      int primary)
{
    robo_display_info_init(d);
    d->id = id;
    d->bounds = robo_gfx_rect(x, y, w, h);
    d->work_area = robo_gfx_rect(x, y, w, h - 40);
    d->resolution_width_px = res_w;
    d->resolution_height_px = res_h;
    d->refresh_hz = 60.0f;
    d->scale = scale;
    d->dpi_x = dpi_x;
    d->dpi_y = dpi_y;
    d->orientation = ROBO_DISPLAY_ORIENTATION_LANDSCAPE;
    d->is_primary = primary;
    d->connection = ROBO_DISPLAY_CONNECTION_CONNECTED;
}

static robo_result_t sm_init(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void sm_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t sm_enumerate(void *ud,
                                  robo_display_info_t *out,
                                  size_t capacity,
                                  size_t *out_count)
{
    scale_mock_t *m = (scale_mock_t *)ud;
    size_t n;
    size_t i;

    if (!m || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    n = m->count;
    *out_count = n;
    if (!out || capacity == 0u) {
        return ROBO_OK;
    }
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; i++) {
        out[i] = m->displays[i];
    }
    *out_count = n;
    return ROBO_OK;
}

static robo_result_t sm_primary(void *ud, robo_display_info_t *out)
{
    scale_mock_t *m = (scale_mock_t *)ud;
    size_t i;

    if (!m || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < m->count; i++) {
        if (m->displays[i].is_primary) {
            *out = m->displays[i];
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

static robo_result_t sm_get_info(void *ud,
                                 robo_display_id_t id,
                                 robo_display_info_t *out)
{
    scale_mock_t *m = (scale_mock_t *)ud;
    size_t i;

    if (!m || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < m->count; i++) {
        if (m->displays[i].id == id) {
            *out = m->displays[i];
            return ROBO_OK;
        }
    }
    return ROBO_NOT_FOUND;
}

static void scale_fixture(void)
{
    memset(&g_sm, 0, sizeof(g_sm));
    memset(&g_sm_caps, 0, sizeof(g_sm_caps));
    g_sm_caps.enumerate = 1;
    g_sm_caps.primary = 1;
    g_sm_caps.get_info = 1;
    g_sm_caps.work_area = 1;
    g_sm_caps.scale_dpi = 1;

    memset(&g_sm_vtbl, 0, sizeof(g_sm_vtbl));
    g_sm_vtbl.name = "scale_mock";
    g_sm_vtbl.capabilities = &g_sm_caps;
    g_sm_vtbl.initialize = sm_init;
    g_sm_vtbl.shutdown = sm_shutdown;
    g_sm_vtbl.enumerate = sm_enumerate;
    g_sm_vtbl.get_primary = sm_primary;
    g_sm_vtbl.get_info = sm_get_info;

    robo_window_shutdown();
    robo_display_shutdown();
    ROBO_CHECK_EQ_INT(robo_display_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_backend_register(&g_sm_vtbl, &g_sm), ROBO_OK);
}

static void scale_fixture_end(void)
{
    robo_window_shutdown();
    robo_display_shutdown();
}

static void test_scale_validate(void)
{
    union {
        float f;
        uint32_t u;
    } nan;
    union {
        float f;
        uint32_t u;
    } inf;

    ROBO_CHECK_EQ_INT(robo_display_scale_validate(1.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(1.25f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(1.5f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(1.75f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(2.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(2.5f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(3.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(0.25f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(8.0f), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_display_scale_validate(0.0f), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(-1.0f), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(0.1f), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(9.0f), ROBO_INVALID_ARGUMENT);

    nan.u = 0x7FC00000u; /* quiet NaN */
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(nan.f), ROBO_INVALID_ARGUMENT);

    inf.u = 0x7F800000u; /* +Inf */
    ROBO_CHECK_EQ_INT(robo_display_scale_validate(inf.f), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_display_scale_is_known(0.0f), 0);
    ROBO_CHECK_EQ_INT(robo_display_scale_is_known(1.5f), 1);
}

static void test_dpi_validate(void)
{
    union {
        float f;
        uint32_t u;
    } nan;
    robo_display_dpi_t pair;

    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(0.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_dpi_is_unavailable(0.0f), 1);
    ROBO_CHECK_EQ_INT(robo_display_dpi_is_known(0.0f), 0);
    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(96.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(120.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(144.5f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_dpi_is_known(96.0f), 1);

    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(-1.0f), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(0.5f), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(2000.0f), ROBO_INVALID_ARGUMENT);
    nan.u = 0x7FC00000u;
    ROBO_CHECK_EQ_INT(robo_display_dpi_validate(nan.f), ROBO_INVALID_ARGUMENT);

    robo_display_dpi_init(&pair);
    ROBO_CHECK(pair.dpi_x == 0.0f);
    ROBO_CHECK_EQ_INT(robo_display_dpi_pair_validate(&pair), ROBO_OK);
    pair.dpi_x = 96.0f;
    pair.dpi_y = -1.0f;
    ROBO_CHECK_EQ_INT(robo_display_dpi_pair_validate(&pair),
                      ROBO_INVALID_ARGUMENT);
}

static void test_scale_from_dpi(void)
{
    float s = 0.0f;

    ROBO_CHECK_EQ_INT(robo_display_scale_from_dpi(96.0f, &s), ROBO_OK);
    ROBO_CHECK(s > 0.99f && s < 1.01f);
    ROBO_CHECK_EQ_INT(robo_display_scale_from_dpi(144.0f, &s), ROBO_OK);
    ROBO_CHECK(s > 1.49f && s < 1.51f);
    ROBO_CHECK_EQ_INT(robo_display_scale_from_dpi(192.0f, &s), ROBO_OK);
    ROBO_CHECK(s > 1.99f && s < 2.01f);
    ROBO_CHECK_EQ_INT(robo_display_scale_from_dpi(0.0f, &s),
                      ROBO_INVALID_ARGUMENT);
}

static void check_roundtrip(int32_t logical, float scale)
{
    int32_t phys = 0;
    int32_t back = 0;

    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(logical, scale, &phys),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_physical_to_logical(phys, scale, &back),
                      ROBO_OK);
    /* Nearest rounding: round-trip may differ by at most 1 for odd products. */
    {
        int32_t delta = back - logical;
        if (delta < 0) {
            delta = -delta;
        }
        ROBO_CHECK(delta <= 1);
    }
}

static void test_convert_scalars(void)
{
    int32_t phys = 0;
    int32_t logical = 0;
    float scales[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f};
    size_t i;

    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(100, 1.25f, &phys),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(phys, 125);
    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(100, 1.5f, &phys),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(phys, 150);
    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(100, 1.75f, &phys),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(phys, 175);
    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(10, 2.5f, &phys),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(phys, 25);

    /* Nearest; ties away from zero */
    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(1, 1.5f, &phys), ROBO_OK);
    ROBO_CHECK_EQ_INT(phys, 2);
    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(-1, 1.5f, &phys),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(phys, -2);

    ROBO_CHECK_EQ_INT(robo_display_physical_to_logical(125, 1.25f, &logical),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(logical, 100);

    for (i = 0u; i < sizeof(scales) / sizeof(scales[0]); i++) {
        check_roundtrip(800, scales[i]);
        check_roundtrip(-200, scales[i]);
        check_roundtrip(0, scales[i]);
    }

    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(100, 0.0f, &phys),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(
        robo_display_logical_to_physical(2000000000, 8.0f, &phys),
        ROBO_INVALID_ARGUMENT);
}

static void test_convert_size_point_rect(void)
{
    int32_t pw = 0;
    int32_t ph = 0;
    int32_t px = 0;
    int32_t py = 0;
    robo_gfx_rect_t in;
    robo_gfx_rect_t out;

    ROBO_CHECK_EQ_INT(
        robo_display_size_logical_to_physical(640, 480, 1.5f, &pw, &ph),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(pw, 960);
    ROBO_CHECK_EQ_INT(ph, 720);

    ROBO_CHECK_EQ_INT(
        robo_display_point_logical_to_physical(-100, 50, 2.0f, &px, &py),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(px, -200);
    ROBO_CHECK_EQ_INT(py, 100);

    in = robo_gfx_rect(-80, 40, 200, 100);
    ROBO_CHECK_EQ_INT(robo_display_rect_logical_to_physical(&in, 1.25f, &out),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, -100);
    ROBO_CHECK_EQ_INT(out.y, 50);
    ROBO_CHECK_EQ_INT(out.width, 250);
    ROBO_CHECK_EQ_INT(out.height, 125);

    ROBO_CHECK_EQ_INT(robo_display_rect_physical_to_logical(&out, 1.25f, &in),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(in.x, -80);
    ROBO_CHECK_EQ_INT(in.y, 40);
    ROBO_CHECK_EQ_INT(in.width, 200);
    ROBO_CHECK_EQ_INT(in.height, 100);
}

static void test_multi_display_scale(void)
{
    float s = 0.0f;
    robo_display_dpi_t dpi;
    robo_gfx_rect_t phys;

    scale_fixture();
    fill_disp(&g_sm.displays[0], (robo_display_id_t)1u, 0, 0, 1280, 800, 1280,
              800, 1.0f, 96.0f, 96.0f, 1);
    fill_disp(&g_sm.displays[1], (robo_display_id_t)2u, 1280, 0, 1280, 800,
              1920, 1200, 1.5f, 144.0f, 144.0f, 0);
    fill_disp(&g_sm.displays[2], (robo_display_id_t)3u, -1920, 0, 1920, 1080,
              3840, 2160, 2.0f, 192.0f, 192.0f, 0);
    g_sm.count = 3u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_count(), 3);

    ROBO_CHECK_EQ_INT(robo_display_get_scale((robo_display_id_t)1u, &s),
                      ROBO_OK);
    ROBO_CHECK(s > 0.99f && s < 1.01f);
    ROBO_CHECK_EQ_INT(robo_display_get_scale((robo_display_id_t)2u, &s),
                      ROBO_OK);
    ROBO_CHECK(s > 1.49f && s < 1.51f);
    ROBO_CHECK_EQ_INT(robo_display_get_scale((robo_display_id_t)3u, &s),
                      ROBO_OK);
    ROBO_CHECK(s > 1.99f && s < 2.01f);

    ROBO_CHECK_EQ_INT(robo_display_get_dpi((robo_display_id_t)2u, &dpi),
                      ROBO_OK);
    ROBO_CHECK(dpi.dpi_x > 143.0f && dpi.dpi_x < 145.0f);

    {
        robo_display_info_t info;
        ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)3u, &info),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_display_info_physical_bounds(&info, &phys),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(phys.x, -3840);
        ROBO_CHECK_EQ_INT(phys.width, 3840);
        ROBO_CHECK_EQ_INT(phys.height, 2160);
    }

    ROBO_CHECK_EQ_INT(robo_display_get_scale((robo_display_id_t)99u, &s),
                      ROBO_NOT_FOUND);
    scale_fixture_end();
}

static void test_dpi_unavailable_vs_invalid(void)
{
    robo_display_info_t info;
    float s = 0.0f;
    robo_display_dpi_t dpi;

    scale_fixture();
    fill_disp(&g_sm.displays[0], (robo_display_id_t)1u, 0, 0, 800, 600, 0, 0,
              1.5f, 0.0f, 0.0f, 1);
    g_sm.count = 1u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)1u, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_info_get_dpi(&info, &dpi), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_dpi_is_unavailable(dpi.dpi_x), 1);
    ROBO_CHECK_EQ_INT(robo_display_info_effective_scale(&info, &s), ROBO_OK);
    ROBO_CHECK(s > 1.49f && s < 1.51f);

    /* Scale unavailable; derive from DPI */
    fill_disp(&g_sm.displays[0], (robo_display_id_t)1u, 0, 0, 800, 600, 1600,
              1200, 0.0f, 192.0f, 192.0f, 1);
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)1u, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_info_effective_scale(&info, &s), ROBO_OK);
    ROBO_CHECK(s > 1.99f && s < 2.01f);

    /* Both unavailable */
    fill_disp(&g_sm.displays[0], (robo_display_id_t)1u, 0, 0, 800, 600, 0, 0,
              0.0f, 0.0f, 0.0f, 1);
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)1u, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_info_effective_scale(&info, &s),
                      ROBO_NOT_FOUND);
    scale_fixture_end();
}

static void test_window_scale(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    float s = 0.0f;
    robo_display_dpi_t dpi;
    robo_gfx_size_t phys;

    /* Window init installs placeholder display; replace with scale mock. */
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_use_null(), ROBO_OK);

    memset(&g_sm, 0, sizeof(g_sm));
    memset(&g_sm_caps, 0, sizeof(g_sm_caps));
    g_sm_caps.enumerate = 1;
    g_sm_caps.primary = 1;
    g_sm_caps.get_info = 1;
    g_sm_caps.work_area = 1;
    g_sm_caps.scale_dpi = 1;
    memset(&g_sm_vtbl, 0, sizeof(g_sm_vtbl));
    g_sm_vtbl.name = "scale_mock";
    g_sm_vtbl.capabilities = &g_sm_caps;
    g_sm_vtbl.initialize = sm_init;
    g_sm_vtbl.shutdown = sm_shutdown;
    g_sm_vtbl.enumerate = sm_enumerate;
    g_sm_vtbl.get_primary = sm_primary;
    g_sm_vtbl.get_info = sm_get_info;
    ROBO_CHECK_EQ_INT(robo_display_backend_register(&g_sm_vtbl, &g_sm), ROBO_OK);

    fill_disp(&g_sm.displays[0], (robo_display_id_t)10u, 0, 0, 1280, 800, 1280,
              800, 1.0f, 96.0f, 96.0f, 1);
    fill_disp(&g_sm.displays[1], (robo_display_id_t)20u, 1280, 0, 1280, 800,
              2560, 1600, 2.0f, 192.0f, 192.0f, 0);
    g_sm.count = 2u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);

    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    cfg.display_id = (robo_display_id_t)10u;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &s), ROBO_OK);
    ROBO_CHECK(s > 0.99f && s < 1.01f);
    ROBO_CHECK_EQ_INT(robo_window_get_dpi(id, &dpi), ROBO_OK);
    ROBO_CHECK(dpi.dpi_x > 95.0f && dpi.dpi_x < 97.0f);

    ROBO_CHECK_EQ_INT(
        robo_window_set_associated_display(id, (robo_display_id_t)20u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &s), ROBO_OK);
    ROBO_CHECK(s > 1.99f && s < 2.01f);
    ROBO_CHECK_EQ_INT(robo_window_get_size_physical(id, &phys), ROBO_OK);
    ROBO_CHECK_EQ_INT(phys.width, 800);
    ROBO_CHECK_EQ_INT(phys.height, 600);

    ROBO_CHECK_EQ_INT(robo_window_set_associated_display(id, ROBO_DISPLAY_ID_NONE),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_dpi(id, &dpi), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_window_set_associated_display(id, (robo_display_id_t)99u),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);

    robo_window_config_init(&cfg);
    cfg.visible = 0;
    cfg.width = 400;
    cfg.height = 300;
    cfg.scale_preference = 1.25f;
    cfg.display_id = (robo_display_id_t)10u;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &s), ROBO_OK);
    ROBO_CHECK(s > 1.24f && s < 1.26f);
    ROBO_CHECK_EQ_INT(
        robo_window_set_associated_display(id, (robo_display_id_t)20u),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_scale(id, &s), ROBO_OK);
    ROBO_CHECK(s > 1.24f && s < 1.26f);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);

    cfg.scale_preference = -1.0f;
    ROBO_CHECK_EQ_INT(robo_window_config_validate(&cfg), ROBO_INVALID_ARGUMENT);
    cfg.scale_preference = 99.0f;
    ROBO_CHECK_EQ_INT(robo_window_config_validate(&cfg), ROBO_INVALID_ARGUMENT);

    scale_fixture_end();
}

void robo_test_register_display_scale(void)
{
    robo_test_register("display.scale.validate", test_scale_validate);
    robo_test_register("display.scale.dpi_validate", test_dpi_validate);
    robo_test_register("display.scale.from_dpi", test_scale_from_dpi);
    robo_test_register("display.scale.convert", test_convert_scalars);
    robo_test_register("display.scale.geom", test_convert_size_point_rect);
    robo_test_register("display.scale.multi", test_multi_display_scale);
    robo_test_register("display.scale.unavailable", test_dpi_unavailable_vs_invalid);
    robo_test_register("display.scale.window", test_window_scale);
}
