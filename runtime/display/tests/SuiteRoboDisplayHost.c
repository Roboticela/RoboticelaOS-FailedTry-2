/*
 * Host RoboDisplay backend suite (Goal 7.13).
 *
 * Real EnumDisplayMonitors path when Win32 host is available.
 * Skips cleanly under ROBO_WINDOW_SKIP_HOST or no interactive desktop.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendHost.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int host_display_skip(char *buf, size_t cap)
{
    if (getenv("ROBO_WINDOW_SKIP_HOST") != NULL) {
        if (buf && cap) {
            snprintf(buf, cap, "ROBO_WINDOW_SKIP_HOST");
        }
        return 1;
    }
    if (!robo_display_backend_host_available()) {
        if (buf && cap) {
            snprintf(buf, cap, "host display unavailable");
        }
        return 1;
    }
    return 0;
}

static void test_host_display_availability(void)
{
    const robo_display_backend_vtbl_t *vtbl = robo_display_backend_host_vtbl();
#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32
    if (host_display_skip(NULL, 0)) {
        ROBO_CHECK(vtbl != NULL); /* vtbl exists even when skip env set */
        ROBO_CHECK_STR_EQ(vtbl->name, "win32");
        return;
    }
    ROBO_CHECK(vtbl != NULL);
    ROBO_CHECK_STR_EQ(vtbl->name, "win32");
    ROBO_CHECK_EQ_INT(robo_display_backend_host_available(), 1);
#else
    ROBO_CHECK(vtbl == NULL);
    ROBO_CHECK_EQ_INT(robo_display_backend_host_available(), 0);
    ROBO_CHECK_EQ_INT(robo_display_backend_use_host(), ROBO_NOT_SUPPORTED);
#endif
}

static int begin_host_display(void)
{
    char reason[64];
    robo_result_t r;

    if (host_display_skip(reason, sizeof(reason))) {
        printf("SKIP display.host.* (%s)\n", reason);
        return 0;
    }
    ROBO_CHECK_EQ_INT(robo_display_init(), ROBO_OK);
    r = robo_display_backend_use_host();
    if (r != ROBO_OK) {
        printf("SKIP display.host.* (use_host=%d)\n", (int)r);
        robo_display_shutdown();
        return 0;
    }
    ROBO_CHECK_EQ_INT(robo_display_refresh(), ROBO_OK);
    return 1;
}

static void test_host_display_enumerate(void)
{
    robo_display_info_t infos[16];
    size_t count = 0u;
    size_t i;
    int saw_primary = 0;
    robo_display_backend_caps_t caps;
    robo_display_info_t primary;

    if (!begin_host_display()) {
        return;
    }

    ROBO_CHECK_EQ_INT(robo_display_backend_capabilities(&caps), ROBO_OK);
    ROBO_CHECK(caps.enumerate);
    ROBO_CHECK(caps.primary);
    ROBO_CHECK(caps.get_info);
    ROBO_CHECK(caps.work_area);
    ROBO_CHECK(caps.scale_dpi);
    ROBO_CHECK(!caps.refresh_rate); /* Win32 path does not fabricate Hz */

    count = robo_display_count();
    ROBO_CHECK(count >= 1u);
    ROBO_CHECK(count <= 16u);
    printf("host displays=%u\n", (unsigned)count);

    for (i = 0u; i < count; i++) {
        float scale = 0.0f;
        ROBO_CHECK_EQ_INT(robo_display_at(i, &infos[i]), ROBO_OK);
        ROBO_CHECK(infos[i].id != ROBO_DISPLAY_ID_NONE);
        ROBO_CHECK(infos[i].resolution_width_px > 0);
        ROBO_CHECK(infos[i].resolution_height_px > 0);
        ROBO_CHECK(infos[i].bounds.width > 0);
        ROBO_CHECK(infos[i].bounds.height > 0);
        ROBO_CHECK(infos[i].work_area.width > 0);
        ROBO_CHECK(infos[i].work_area.height > 0);
        ROBO_CHECK_EQ_INT(robo_display_info_effective_scale(&infos[i], &scale),
                          ROBO_OK);
        ROBO_CHECK(robo_display_scale_is_known(scale));
        ROBO_CHECK(infos[i].dpi_x > 0.0f);
        ROBO_CHECK(infos[i].dpi_y > 0.0f);
        ROBO_CHECK(infos[i].refresh_hz == 0.0f); /* unknown, not faked */
        if (infos[i].is_primary) {
            saw_primary = 1;
        }
        printf("  display id=%llu name=%s %dx%d scale=%g dpi=%g\n",
               (unsigned long long)infos[i].id,
               infos[i].name[0] ? infos[i].name : "(unnamed)",
               (int)infos[i].resolution_width_px,
               (int)infos[i].resolution_height_px, (double)scale,
               (double)infos[i].dpi_x);
    }
    ROBO_CHECK(saw_primary);

    ROBO_CHECK_EQ_INT(robo_display_primary(&primary), ROBO_OK);
    ROBO_CHECK(primary.is_primary);
    ROBO_CHECK_EQ_INT(robo_display_get(primary.id, &infos[0]), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)infos[0].id, (int)primary.id);

    {
        robo_display_mode_t modes[4];
        size_t mode_count = 0u;
        ROBO_CHECK_EQ_INT(
            robo_display_get_modes(primary.id, modes, 4, &mode_count), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)mode_count, 1);
        ROBO_CHECK_EQ_INT(modes[0].width_px, primary.resolution_width_px);
        ROBO_CHECK_EQ_INT(modes[0].height_px, primary.resolution_height_px);
    }

    robo_display_shutdown();
}

static void test_host_display_reinit(void)
{
    if (!begin_host_display()) {
        return;
    }
    robo_display_shutdown();
    ROBO_CHECK_EQ_INT(robo_display_is_initialized(), 0);
    if (!begin_host_display()) {
        return;
    }
    {
        size_t count = 0u;
        count = robo_display_count();
        ROBO_CHECK(count >= 1u);
    }
    robo_display_shutdown();
}

static void test_host_display_use_when_skipped(void)
{
    char reason[64];
    ROBO_CHECK_EQ_INT(robo_display_init(), ROBO_OK);
    if (host_display_skip(reason, sizeof(reason))) {
        ROBO_CHECK_EQ_INT(robo_display_backend_use_host(), ROBO_NOT_SUPPORTED);
    }
    robo_display_shutdown();
}

void robo_test_register_display_host(void)
{
    robo_test_register("display.host.availability",
                       test_host_display_availability);
    robo_test_register("display.host.enumerate", test_host_display_enumerate);
    robo_test_register("display.host.reinit", test_host_display_reinit);
    robo_test_register("display.host.use_skipped",
                       test_host_display_use_when_skipped);
}
