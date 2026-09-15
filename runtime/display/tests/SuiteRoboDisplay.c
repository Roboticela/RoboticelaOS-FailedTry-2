/*
 * SuiteRoboDisplay — Goal 7.3 display abstraction tests.
 *
 * Mock backend is test-only; not a production platform implementation.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#define MOCK_MAX 8

typedef struct mock_display_backend {
    robo_display_info_t displays[MOCK_MAX];
    size_t count;
    int fail_init;
    int fail_enumerate;
    int provide_modes;
    robo_display_mode_t modes[4];
    size_t mode_count;
    robo_display_id_t modes_for_id;
} mock_display_backend_t;

static mock_display_backend_t g_mock;
static robo_display_backend_caps_t g_mock_caps;
static robo_display_backend_vtbl_t g_mock_vtbl;

static void fill_display(robo_display_info_t *d,
                         robo_display_id_t id,
                         int32_t x,
                         int32_t y,
                         int32_t w,
                         int32_t h,
                         int32_t work_inset,
                         int32_t res_w,
                         int32_t res_h,
                         float hz,
                         float scale,
                         float dpi,
                         robo_display_orientation_t ori,
                         int primary)
{
    robo_display_info_init(d);
    d->id = id;
    d->bounds = robo_gfx_rect(x, y, w, h);
    d->work_area = robo_gfx_rect(x + work_inset, y + work_inset,
                                 w - 2 * work_inset, h - 2 * work_inset);
    d->resolution_width_px = res_w;
    d->resolution_height_px = res_h;
    d->refresh_hz = hz;
    d->scale = scale;
    d->dpi_x = dpi;
    d->dpi_y = dpi;
    d->orientation = ori;
    d->is_primary = primary;
    d->connection = ROBO_DISPLAY_CONNECTION_CONNECTED;
}

static robo_result_t mock_init(void *ud)
{
    mock_display_backend_t *m = (mock_display_backend_t *)ud;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_init) {
        return ROBO_IO_ERROR;
    }
    return ROBO_OK;
}

static void mock_shutdown(void *ud)
{
    (void)ud;
}

static robo_result_t mock_enumerate(void *ud,
                                    robo_display_info_t *out,
                                    size_t capacity,
                                    size_t *out_count)
{
    mock_display_backend_t *m = (mock_display_backend_t *)ud;
    size_t n;
    size_t i;

    if (!m || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_enumerate) {
        return ROBO_IO_ERROR;
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

static robo_result_t mock_primary(void *ud, robo_display_info_t *out)
{
    mock_display_backend_t *m = (mock_display_backend_t *)ud;
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

static robo_result_t mock_get_info(void *ud,
                                   robo_display_id_t id,
                                   robo_display_info_t *out)
{
    mock_display_backend_t *m = (mock_display_backend_t *)ud;
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

static robo_result_t mock_get_modes(void *ud,
                                    robo_display_id_t id,
                                    robo_display_mode_t *out,
                                    size_t capacity,
                                    size_t *out_count)
{
    mock_display_backend_t *m = (mock_display_backend_t *)ud;
    size_t n;
    size_t i;

    if (!m || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!m->provide_modes || id != m->modes_for_id) {
        return ROBO_NOT_FOUND;
    }
    n = m->mode_count;
    *out_count = n;
    if (!out || capacity == 0u) {
        return ROBO_OK;
    }
    if (n > capacity) {
        n = capacity;
    }
    for (i = 0u; i < n; i++) {
        out[i] = m->modes[i];
    }
    *out_count = n;
    return ROBO_OK;
}

static void mock_prepare_vtbl(int with_modes)
{
    memset(&g_mock_caps, 0, sizeof(g_mock_caps));
    g_mock_caps.enumerate = 1;
    g_mock_caps.primary = 1;
    g_mock_caps.get_info = 1;
    g_mock_caps.work_area = 1;
    g_mock_caps.refresh_rate = 1;
    g_mock_caps.scale_dpi = 1;
    g_mock_caps.orientation = 1;
    g_mock_caps.modes = with_modes ? 1 : 0;

    memset(&g_mock_vtbl, 0, sizeof(g_mock_vtbl));
    g_mock_vtbl.name = "mock_display";
    g_mock_vtbl.capabilities = &g_mock_caps;
    g_mock_vtbl.initialize = mock_init;
    g_mock_vtbl.shutdown = mock_shutdown;
    g_mock_vtbl.enumerate = mock_enumerate;
    g_mock_vtbl.get_primary = mock_primary;
    g_mock_vtbl.get_info = mock_get_info;
    g_mock_vtbl.get_modes = with_modes ? mock_get_modes : NULL;
}

static void display_fixture_reset(void)
{
    robo_window_shutdown();
    robo_display_shutdown();
    memset(&g_mock, 0, sizeof(g_mock));
}

static void display_fixture_mock(void)
{
    display_fixture_reset();
    ROBO_CHECK_EQ_INT(robo_display_init(), ROBO_OK);
    mock_prepare_vtbl(0);
    ROBO_CHECK_EQ_INT(robo_display_backend_register(&g_mock_vtbl, &g_mock),
                      ROBO_OK);
}

static void test_display_identity(void)
{
    robo_display_info_t info;

    display_fixture_mock();
    fill_display(&g_mock.displays[0], (robo_display_id_t)42u, 0, 0, 100, 100, 0,
                 100, 100, 60.0f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    g_mock.count = 1u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);

    ROBO_CHECK(robo_display_id_is_none(ROBO_DISPLAY_ID_NONE));
    ROBO_CHECK(!robo_display_id_is_none((robo_display_id_t)42u));
    ROBO_CHECK(robo_display_id_equal((robo_display_id_t)42u,
                                     (robo_display_id_t)42u));
    ROBO_CHECK(!robo_display_id_equal(ROBO_DISPLAY_ID_NONE, ROBO_DISPLAY_ID_NONE));
    ROBO_CHECK(!robo_display_id_equal((robo_display_id_t)1u,
                                      (robo_display_id_t)2u));
    ROBO_CHECK(robo_display_id_is_known((robo_display_id_t)42u));
    ROBO_CHECK(!robo_display_id_is_known((robo_display_id_t)99u));
    ROBO_CHECK(!robo_display_id_is_known(ROBO_DISPLAY_ID_NONE));
    ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)42u, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)99u, &info),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_display_get(ROBO_DISPLAY_ID_NONE, &info),
                      ROBO_NOT_FOUND);
    display_fixture_reset();
}

static void test_display_geometry(void)
{
    robo_display_info_t a;
    robo_display_info_t b;

    display_fixture_mock();
    fill_display(&g_mock.displays[0], (robo_display_id_t)10u, 0, 0, 1920, 1080,
                 40, 1920, 1080, 60.0f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    fill_display(&g_mock.displays[1], (robo_display_id_t)20u, -1280, 100, 1280,
                 800, 0, 2560, 1600, 120.0f, 2.0f, 192.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 0);
    g_mock.count = 2u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 2);
    ROBO_CHECK_EQ_INT(robo_display_at(0u, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_at(1u, &b), ROBO_OK);
    ROBO_CHECK_EQ_INT(a.bounds.x, 0);
    ROBO_CHECK_EQ_INT(a.work_area.y, 40);
    ROBO_CHECK_EQ_INT(b.bounds.x, -1280);
    ROBO_CHECK_EQ_INT(b.bounds.y, 100);
    ROBO_CHECK_EQ_INT(b.resolution_width_px, 2560);
    ROBO_CHECK_EQ_INT(b.resolution_height_px, 1600);
    display_fixture_reset();
}

static void test_display_refresh_rate(void)
{
    float nan_bits;
    uint32_t u = 0x7fc00000u;

    ROBO_CHECK_EQ_INT(robo_display_refresh_hz_validate(60.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_refresh_hz_validate(59.94f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_refresh_hz_validate(144.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_refresh_hz_validate(0.0f), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_refresh_hz_validate(-1.0f),
                      ROBO_INVALID_ARGUMENT);
    memcpy(&nan_bits, &u, sizeof(nan_bits));
    ROBO_CHECK_EQ_INT(robo_display_refresh_hz_validate(nan_bits),
                      ROBO_INVALID_ARGUMENT);

    display_fixture_mock();
    fill_display(&g_mock.displays[0], (robo_display_id_t)1u, 0, 0, 100, 100, 0,
                 100, 100, 59.94f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    g_mock.count = 1u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    {
        robo_display_info_t info;
        ROBO_CHECK_EQ_INT(robo_display_primary(&info), ROBO_OK);
        ROBO_CHECK(info.refresh_hz > 59.9f && info.refresh_hz < 60.0f);
    }
    display_fixture_reset();
}

static void test_display_scale_dpi(void)
{
    int32_t phys = 0;
    int32_t logical = 0;

    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(100, 2.0f, &phys),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(phys, 200);
    ROBO_CHECK_EQ_INT(robo_display_physical_to_logical(200, 2.0f, &logical),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(logical, 100);
    ROBO_CHECK_EQ_INT(robo_display_logical_to_physical(100, 0.0f, &phys),
                      ROBO_INVALID_ARGUMENT);

    display_fixture_mock();
    fill_display(&g_mock.displays[0], (robo_display_id_t)7u, 0, 0, 1280, 800, 0,
                 2560, 1600, 60.0f, 2.0f, 192.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    g_mock.count = 1u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    {
        robo_display_info_t info;
        ROBO_CHECK_EQ_INT(robo_display_primary(&info), ROBO_OK);
        ROBO_CHECK(info.scale > 1.9f && info.scale < 2.1f);
        ROBO_CHECK(info.dpi_x > 191.0f && info.dpi_x < 193.0f);
        ROBO_CHECK_EQ_INT(info.bounds.width, 1280);
        ROBO_CHECK_EQ_INT(info.resolution_width_px, 2560);
    }
    display_fixture_reset();
}

static void test_display_orientation(void)
{
    ROBO_CHECK(robo_display_orientation_is_known(
        ROBO_DISPLAY_ORIENTATION_LANDSCAPE));
    ROBO_CHECK(robo_display_orientation_is_known(
        ROBO_DISPLAY_ORIENTATION_PORTRAIT));
    ROBO_CHECK(robo_display_orientation_is_known(
        ROBO_DISPLAY_ORIENTATION_LANDSCAPE_FLIPPED));
    ROBO_CHECK(robo_display_orientation_is_known(
        ROBO_DISPLAY_ORIENTATION_PORTRAIT_FLIPPED));
    ROBO_CHECK(!robo_display_orientation_is_known(
        ROBO_DISPLAY_ORIENTATION_UNKNOWN));
    ROBO_CHECK(!robo_display_orientation_is_known(
        (robo_display_orientation_t)99));
    ROBO_CHECK(strcmp(robo_display_orientation_name(
                          ROBO_DISPLAY_ORIENTATION_PORTRAIT),
                      "portrait") == 0);
    ROBO_CHECK(strcmp(robo_display_connection_name(
                          ROBO_DISPLAY_CONNECTION_CONNECTED),
                      "connected") == 0);
}

static void test_display_primary(void)
{
    robo_display_info_t info;

    display_fixture_mock();
    g_mock.count = 0u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 0);
    ROBO_CHECK_EQ_INT(robo_display_primary(&info), ROBO_NOT_FOUND);

    fill_display(&g_mock.displays[0], (robo_display_id_t)5u, 0, 0, 800, 600, 0,
                 800, 600, 60.0f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 0);
    fill_display(&g_mock.displays[1], (robo_display_id_t)9u, 800, 0, 800, 600, 0,
                 800, 600, 75.0f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    g_mock.count = 2u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_primary(&info), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)info.id, 9);
    ROBO_CHECK(info.id != (robo_display_id_t)0u);
    display_fixture_reset();
}

static void test_display_enumeration(void)
{
    robo_display_info_t info;

    display_fixture_mock();
    g_mock.count = 0u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 0);
    ROBO_CHECK_EQ_INT(robo_display_at(0u, &info), ROBO_NOT_FOUND);

    fill_display(&g_mock.displays[0], (robo_display_id_t)1u, 0, 0, 10, 10, 0, 10,
                 10, 60.0f, 1.0f, 96.0f, ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    g_mock.count = 1u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 1);

    fill_display(&g_mock.displays[1], (robo_display_id_t)2u, 10, 0, 10, 10, 0,
                 10, 10, 60.0f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 0);
    g_mock.count = 2u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 2);

    g_mock.fail_enumerate = 1;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_IO_ERROR);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 0);
    display_fixture_reset();
}

static void test_display_backend_lifecycle(void)
{
    robo_display_backend_caps_t caps;

    display_fixture_reset();
    ROBO_CHECK_EQ_INT(robo_display_backend_use_null(), ROBO_INIT_ERROR);
    ROBO_CHECK_EQ_INT(robo_display_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_backend_use_null(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 0);
    ROBO_CHECK(robo_display_backend_is_registered());
    ROBO_CHECK(strcmp(robo_display_backend_name(), "null") == 0);

    ROBO_CHECK_EQ_INT(robo_display_backend_use_placeholder(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 1);
    ROBO_CHECK(strcmp(robo_display_backend_name(), "placeholder") == 0);

    robo_display_backend_clear();
    ROBO_CHECK(!robo_display_backend_is_registered());
    ROBO_CHECK_EQ_INT(robo_display_backend_capabilities(&caps),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_NOT_SUPPORTED);

    mock_prepare_vtbl(0);
    g_mock.fail_init = 1;
    ROBO_CHECK_EQ_INT(robo_display_backend_register(&g_mock_vtbl, &g_mock),
                      ROBO_IO_ERROR);
    ROBO_CHECK(!robo_display_backend_is_registered());

    robo_display_shutdown();
    robo_display_shutdown();
    ROBO_CHECK(!robo_display_is_initialized());
    display_fixture_reset();
}

static void test_display_capabilities(void)
{
    robo_display_backend_caps_t caps;
    robo_display_mode_t modes[4];
    size_t n = 0u;

    display_fixture_mock();
    fill_display(&g_mock.displays[0], (robo_display_id_t)3u, 0, 0, 100, 100, 0,
                 100, 100, 60.0f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    g_mock.count = 1u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_backend_capabilities(&caps), ROBO_OK);
    ROBO_CHECK(caps.enumerate);
    ROBO_CHECK(caps.primary);
    ROBO_CHECK(caps.get_info);
    ROBO_CHECK(!caps.modes);
    ROBO_CHECK_EQ_INT(robo_display_get_modes((robo_display_id_t)3u, modes, 4u,
                                             &n),
                      ROBO_NOT_SUPPORTED);

    mock_prepare_vtbl(1);
    g_mock.provide_modes = 1;
    g_mock.modes_for_id = (robo_display_id_t)3u;
    g_mock.mode_count = 2u;
    g_mock.modes[0].width_px = 100;
    g_mock.modes[0].height_px = 100;
    g_mock.modes[0].refresh_hz = 60.0f;
    g_mock.modes[1].width_px = 100;
    g_mock.modes[1].height_px = 100;
    g_mock.modes[1].refresh_hz = 120.0f;
    ROBO_CHECK_EQ_INT(robo_display_backend_register(&g_mock_vtbl, &g_mock),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get_modes((robo_display_id_t)3u, modes, 4u,
                                             &n),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)n, 2);
    display_fixture_reset();
}

static void test_display_lookup(void)
{
    robo_display_info_t info;

    display_fixture_mock();
    fill_display(&g_mock.displays[0], (robo_display_id_t)11u, 0, 0, 50, 50, 0,
                 50, 50, 60.0f, 1.0f, 96.0f,
                 ROBO_DISPLAY_ORIENTATION_LANDSCAPE, 1);
    g_mock.count = 1u;
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)11u, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_display_get((robo_display_id_t)12u, &info),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_display_get(ROBO_DISPLAY_ID_NONE, &info),
                      ROBO_NOT_FOUND);
    display_fixture_reset();
}

static void test_display_window_integration(void)
{
    robo_window_config_t cfg;
    robo_window_id_t wid = ROBO_WINDOW_ID_NONE;
    robo_display_info_t info;
    robo_window_state_t st;

    display_fixture_reset();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_use_null(), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_display_count(), 1);
    ROBO_CHECK_EQ_INT(robo_display_primary(&info), ROBO_OK);

    robo_window_config_init(&cfg);
    cfg.width = 320;
    cfg.height = 240;
    cfg.title = "display-assoc";
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &wid), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_associated_display(wid, &info), ROBO_OK);
    ROBO_CHECK(info.id != ROBO_DISPLAY_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_window_get_state(wid, &st), ROBO_OK);
    ROBO_CHECK(robo_display_id_equal(st.display_id, info.id));

    ROBO_CHECK_EQ_INT(
        robo_window_set_associated_display(wid, (robo_display_id_t)999u),
        ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(
        robo_window_set_associated_display(wid, ROBO_DISPLAY_ID_NONE), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_associated_display(wid, &info),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_set_associated_display(wid, st.display_id),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_get_associated_display(wid, &info), ROBO_OK);

    display_fixture_reset();
}

static void test_display_version(void)
{
    ROBO_CHECK(strcmp(robo_display_version_string(), "0.4.0") == 0);
    ROBO_CHECK_EQ_INT(ROBO_DISPLAY_API_FROZEN, 0);
}

void robo_test_register_display(void)
{
    robo_test_register("display.version", test_display_version);
    robo_test_register("display.identity", test_display_identity);
    robo_test_register("display.geometry", test_display_geometry);
    robo_test_register("display.refresh_rate", test_display_refresh_rate);
    robo_test_register("display.scale_dpi", test_display_scale_dpi);
    robo_test_register("display.orientation", test_display_orientation);
    robo_test_register("display.primary", test_display_primary);
    robo_test_register("display.enumeration", test_display_enumeration);
    robo_test_register("display.backend_lifecycle",
                       test_display_backend_lifecycle);
    robo_test_register("display.capabilities", test_display_capabilities);
    robo_test_register("display.lookup", test_display_lookup);
    robo_test_register("display.window_integration",
                       test_display_window_integration);
}
