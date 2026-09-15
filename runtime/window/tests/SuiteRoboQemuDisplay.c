/*
 * QEMU display backend suite (Goal 7.12).
 * Host simulator — does not require a live QEMU process.
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendQemu.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendQemu.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <string.h>

static void teardown(void)
{
    if (robo_window_is_initialized()) {
        robo_window_shutdown();
    }
    if (robo_display_is_initialized()) {
        robo_display_shutdown();
    }
    robo_qemu_sim_shutdown();
    robo_graphics_shutdown();
}

static robo_result_t boot_sim(uint32_t w, uint32_t h, uint32_t pixfmt)
{
    robo_result_t r;
    teardown();
    r = robo_graphics_init();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_display_init();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_init();
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_window_backend_use_qemu_sim(w, h, pixfmt);
    return r;
}

static void test_qemu_fb_validate(void)
{
    robo_qemu_fb_info_t info;
    uint8_t buf[64];

    robo_qemu_fb_info_init(&info);
    ROBO_CHECK_EQ_INT(robo_qemu_fb_info_validate(&info), ROBO_NOT_FOUND);

    info.ready = 1;
    info.width = 4;
    info.height = 4;
    info.pitch_bytes = 16;
    info.bpp = 32;
    info.pixfmt = ROBO_QEMU_PIX_BGRX;
    info.pixels = buf;
    info.byte_size = sizeof(buf);
    ROBO_CHECK_EQ_INT(robo_qemu_fb_info_validate(&info), ROBO_OK);

    info.pitch_bytes = 8; /* too small */
    ROBO_CHECK_EQ_INT(robo_qemu_fb_info_validate(&info), ROBO_INVALID_ARGUMENT);

    info.pitch_bytes = 16;
    info.bpp = 16;
    ROBO_CHECK_EQ_INT(robo_qemu_fb_info_validate(&info), ROBO_NOT_SUPPORTED);

    info.bpp = 32;
    info.pixfmt = 99u;
    ROBO_CHECK_EQ_INT(robo_qemu_fb_info_validate(&info), ROBO_NOT_SUPPORTED);

    info.headless = 1;
    info.ready = 0;
    ROBO_CHECK_EQ_INT(robo_qemu_fb_info_validate(&info), ROBO_OK);
}

static void test_qemu_sim_present_colors(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_surface_desc_t desc;
    const robo_qemu_fb_info_t *fb;
    uint32_t px = 0u;
    robo_display_info_t di;

    ROBO_CHECK_EQ_INT(boot_sim(64, 48, ROBO_QEMU_PIX_BGRX), ROBO_OK);
    ROBO_CHECK(robo_qemu_sim_is_active());
    fb = robo_qemu_fb_current();
    ROBO_CHECK(fb && fb->ready && fb->pixels);
    ROBO_CHECK_EQ_INT((int)fb->width, 64);
    ROBO_CHECK_EQ_INT((int)fb->height, 48);

    ROBO_CHECK_EQ_INT(robo_display_primary(&di), ROBO_OK);
    ROBO_CHECK_EQ_INT(di.resolution_width_px, 64);
    ROBO_CHECK_EQ_INT(di.resolution_height_px, 48);
    ROBO_CHECK(di.scale == 1.0f);
    ROBO_CHECK(di.dpi_x == 0.0f);

    robo_window_config_init(&cfg);
    cfg.width = 64;
    cfg.height = 48;
    cfg.title = "qemu-sim";
    cfg.visible = 1;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 64;
    desc.height = 48;
    desc.format = ROBO_GFX_FORMAT_BGRA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surf), ROBO_OK);

    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){0, 0, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, 0, 0,
                                     (robo_gfx_color_rgba8_t){255, 255, 255, 255});
    (void)robo_gfx_surface_set_pixel(surf, 63, 0,
                                     (robo_gfx_color_rgba8_t){255, 0, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, 0, 47,
                                     (robo_gfx_color_rgba8_t){0, 255, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, 63, 47,
                                     (robo_gfx_color_rgba8_t){0, 0, 255, 255});
    (void)robo_gfx_surface_fill_rect(
        surf, robo_gfx_rect(20, 20, 8, 8),
        (robo_gfx_color_rgba8_t){14, 122, 255, 255});

    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);

    /* pack_bgrx → LE uint32 0xXXRRGGBB */
    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 0, 0, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0xFFFFFFu);
    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 63, 0, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0xFF0000); /* red */
    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 0, 47, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0x00FF00);
    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 63, 47, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0x0000FF); /* blue */
    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 20, 20, &px), ROBO_OK);
    /* brand #0E7AFF → 0x000E7AFF */
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0x0E7AFF);

    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 64, 0, &px),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(surf);
    (void)robo_window_destroy(id);
    teardown();
}

static void test_qemu_sim_rgbx_and_input(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_surface_desc_t desc;
    const robo_qemu_fb_info_t *fb;
    uint32_t px = 0u;
    robo_window_event_t ev;
    int saw_key = 0;
    int saw_mouse = 0;

    ROBO_CHECK_EQ_INT(boot_sim(32, 32, ROBO_QEMU_PIX_RGBX), ROBO_OK);
    fb = robo_qemu_fb_current();
    ROBO_CHECK(fb && fb->pixfmt == ROBO_QEMU_PIX_RGBX);

    robo_window_config_init(&cfg);
    cfg.width = 32;
    cfg.height = 32;
    cfg.title = "qemu-rgbx";
    cfg.visible = 1;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    (void)robo_window_observe_focus(id, 1);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 32;
    desc.height = 32;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surf), ROBO_OK);
    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){255, 0, 0, 255});
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 0, 0, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0x0000FF); /* RGBX red */

    ROBO_CHECK_EQ_INT(
        robo_qemu_input_inject_raw(1u /* KEY_DOWN */, 0x04u /* A */, 0, 0, 0, 0,
                                   0, 1u /* SHIFT */, 0),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_qemu_input_inject_raw(2u /* KEY_UP */, 0x04u, 0, 0, 0, 0, 0, 0, 0),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_qemu_input_inject_raw(3u /* MOVE */, 0, 0, 5, 7, 1, 2, 0, 0),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_qemu_input_inject_raw(4u /* BUTTON */, 1u, 1u, 5, 7, 0, 0, 0, 0),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_qemu_input_inject_raw(4u, 1u, 0u, 5, 7, 0, 0, 0, 0), ROBO_OK);
    ROBO_CHECK_EQ_INT(
        robo_qemu_input_inject_raw(5u /* SCROLL */, 0, 120, 5, 7, 0, 120, 0, 0),
        ROBO_OK);

    while (robo_window_event_poll(&ev) == ROBO_OK) {
        if (ev.type == ROBO_WINDOW_EVENT_KEY_DOWN ||
            ev.type == ROBO_WINDOW_EVENT_KEY_UP) {
            saw_key = 1;
        }
        if (ev.type == ROBO_WINDOW_EVENT_MOUSE_MOVE ||
            ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN ||
            ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP ||
            ev.type == ROBO_WINDOW_EVENT_MOUSE_WHEEL) {
            saw_mouse = 1;
        }
    }
    ROBO_CHECK(saw_key);
    ROBO_CHECK(saw_mouse);

    robo_gfx_surface_release(surf);
    (void)robo_window_destroy(id);
    teardown();
}

static void test_qemu_bounds_and_cleanup(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_surface_desc_t desc;

    ROBO_CHECK_EQ_INT(boot_sim(16, 16, ROBO_QEMU_PIX_BGRX), ROBO_OK);
    robo_window_config_init(&cfg);
    cfg.width = 16;
    cfg.height = 16;
    cfg.title = "qemu-bounds";
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 32;
    desc.height = 32;
    desc.format = ROBO_GFX_FORMAT_BGRA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surf), ROBO_OK);
    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){255, 255, 255, 255});
    /* Larger surface still presents clipped — must not crash. */
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);

    robo_gfx_surface_release(surf);
    (void)robo_window_destroy(id);
    teardown();
    ROBO_CHECK(!robo_qemu_sim_is_active());
    ROBO_CHECK(robo_qemu_fb_current() == NULL);
}

static void test_qemu_sim_frame_lifecycle(void)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    const robo_qemu_fb_info_t *fb;
    uint32_t px = 0u;

    ROBO_CHECK_EQ_INT(boot_sim(80, 60, ROBO_QEMU_PIX_BGRX), ROBO_OK);
    fb = robo_qemu_fb_current();
    ROBO_CHECK(fb && fb->ready);

    robo_window_config_init(&cfg);
    cfg.width = 80;
    cfg.height = 60;
    cfg.title = "qemu-frame";
    cfg.visible = 1;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_begin_frame(id, &surf), ROBO_OK);
    ROBO_CHECK(surf != NULL);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(surf), 80);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_height(surf), 60);
    (void)robo_gfx_surface_clear(surf, (robo_gfx_color_rgba8_t){0, 0, 0, 255});
    (void)robo_gfx_surface_set_pixel(surf, 0, 0,
                                     (robo_gfx_color_rgba8_t){255, 255, 255, 255});
    (void)robo_gfx_surface_fill_rect(
        surf, robo_gfx_rect(20, 20, 10, 10),
        (robo_gfx_color_rgba8_t){14, 122, 255, 255});
    ROBO_CHECK_EQ_INT(robo_window_end_frame(id), ROBO_OK);
    ROBO_CHECK(robo_window_is_dirty(id));
    ROBO_CHECK_EQ_INT(robo_window_present_frame(id), ROBO_OK);
    ROBO_CHECK(!robo_window_is_dirty(id));

    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 0, 0, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0xFFFFFFu);
    ROBO_CHECK_EQ_INT(robo_qemu_fb_get_pixel(fb, 20, 20, &px), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)(px & 0xFFFFFFu), 0x0E7AFF);

    (void)robo_window_destroy(id);
    teardown();
}

void robo_test_register_window_qemu_display(void)
{
    robo_test_register("window.qemu.fb_validate", test_qemu_fb_validate);
    robo_test_register("window.qemu.sim_present_colors",
                       test_qemu_sim_present_colors);
    robo_test_register("window.qemu.sim_rgbx_input",
                       test_qemu_sim_rgbx_and_input);
    robo_test_register("window.qemu.bounds_cleanup",
                       test_qemu_bounds_and_cleanup);
    robo_test_register("window.qemu.sim_frame_lifecycle",
                       test_qemu_sim_frame_lifecycle);
}
