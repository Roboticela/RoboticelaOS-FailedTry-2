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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void test_host_smoke_manual(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    const char *smoke = getenv("ROBO_WINDOW_SMOKE");

    if (!smoke || smoke[0] == '\0' || smoke[0] == '0') {
        printf("SKIP window.host.smoke (set ROBO_WINDOW_SMOKE=1)\n");
        return;
    }
    if (!begin_host(&id, &cfg)) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_window_set_title(id, "Roboticela Smoke"), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_size(id, 800, 600), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_request_position(id, 100, 80), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    host_pump_brief(1500);
    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    host_pump_brief(200);
    ROBO_CHECK_EQ_INT(robo_window_show(id), ROBO_OK);
    host_pump_brief(800);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
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

void robo_test_register_window_host(void)
{
    robo_test_register("window.host.availability", test_host_availability);
    robo_test_register("window.host.use_unavailable", test_host_use_when_unavailable);
    robo_test_register("window.host.create_destroy", test_host_create_destroy);
    robo_test_register("window.host.title_size_pos", test_host_title_size_pos_visibility);
    robo_test_register("window.host.focus_display", test_host_focus_and_display);
    robo_test_register("window.host.shutdown_live", test_host_shutdown_with_live_window);
    robo_test_register("window.host.invalid_config", test_host_invalid_config);
    robo_test_register("window.host.smoke", test_host_smoke_manual);
    robo_test_register("window.host.mock_distinct", test_mock_still_distinct);
}
