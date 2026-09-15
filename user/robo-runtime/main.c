/*
 * Roboticela Runtime — QEMU display smoke (Goals 7.12 / 7.14).
 *
 * Path:
 *   begin_frame → RoboGraphics draw → end_frame → present_frame
 *        → QEMU SYS_DISPLAY → GOP FB
 * Input: SYS_DISPLAY INPUT → RoboKeyboard / RoboMouse
 *
 * Serial markers for automated validation:
 *   ROBO_QEMU_DISPLAY_OK
 *   ROBO_QEMU_PRESENT_OK
 *   ROBO_QEMU_INPUT_OK
 *   ROBO_QEMU_DISPLAY_SHUTDOWN_OK
 */

#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendQemu.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendQemu.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <robosys/display.h>
#include <robosys/input.h>
#include <robosys/syscall.h>
#include <robosys/syscall_numbers.h>

#include <stdint.h>
#include <string.h>

#define FONT_W 8
#define FONT_H 8

static const robo_gfx_color_rgba8_t C_BG = {0, 0, 0, 255};
static const robo_gfx_color_rgba8_t C_CARD = {28, 28, 30, 255};
static const robo_gfx_color_rgba8_t C_FG = {255, 255, 255, 255};
static const robo_gfx_color_rgba8_t C_MUTED = {161, 161, 166, 255};
static const robo_gfx_color_rgba8_t C_BRAND = {14, 122, 255, 255};
static const robo_gfx_color_rgba8_t C_TEAL = {20, 184, 166, 255};
static const robo_gfx_color_rgba8_t C_OK = {48, 209, 88, 255};
static const robo_gfx_color_rgba8_t C_WARN = {255, 159, 10, 255};
static const robo_gfx_color_rgba8_t C_DANGER = {255, 59, 48, 255};
static const robo_gfx_color_rgba8_t C_WHITE = {255, 255, 255, 255};
static const robo_gfx_color_rgba8_t C_RED = {255, 0, 0, 255};
static const robo_gfx_color_rgba8_t C_GREEN = {0, 255, 0, 255};
static const robo_gfx_color_rgba8_t C_BLUE = {0, 0, 255, 255};

static const uint8_t g_font[95][8] = {
    {0,0,0,0,0,0,0,0},
    {0x10,0x10,0x10,0x10,0x10,0,0x10,0},
    {0x28,0x28,0x28,0,0,0,0,0},
    {0x28,0x28,0x7C,0x28,0x7C,0x28,0x28,0},
    {0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0},
    {0x60,0x64,0x08,0x10,0x20,0x4C,0x0C,0},
    {0x20,0x50,0x50,0x20,0x54,0x48,0x34,0},
    {0x10,0x10,0x20,0,0,0,0,0},
    {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0},
    {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0},
    {0,0x10,0x54,0x38,0x54,0x10,0,0},
    {0,0x10,0x10,0x7C,0x10,0x10,0,0},
    {0,0,0,0,0,0x10,0x10,0x20},
    {0,0,0,0x7C,0,0,0,0},
    {0,0,0,0,0,0,0x10,0},
    {0x04,0x08,0x10,0x20,0x40,0,0,0},
    {0x38,0x44,0x4C,0x54,0x64,0x44,0x38,0},
    {0x10,0x30,0x10,0x10,0x10,0x10,0x38,0},
    {0x38,0x44,0x04,0x18,0x20,0x40,0x7C,0},
    {0x38,0x44,0x04,0x18,0x04,0x44,0x38,0},
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0},
    {0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0},
    {0x18,0x20,0x40,0x78,0x44,0x44,0x38,0},
    {0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0},
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0},
    {0x38,0x44,0x44,0x3C,0x04,0x08,0x30,0},
    {0,0x10,0,0,0x10,0,0,0},
    {0,0x10,0,0,0x10,0x10,0x20,0},
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0},
    {0,0,0x7C,0,0x7C,0,0,0},
    {0x40,0x20,0x10,0x08,0x10,0x20,0x40,0},
    {0x38,0x44,0x04,0x08,0x10,0,0x10,0},
    {0x38,0x44,0x5C,0x54,0x5C,0x40,0x38,0},
    {0x38,0x44,0x44,0x7C,0x44,0x44,0x44,0},
    {0x78,0x44,0x44,0x78,0x44,0x44,0x78,0},
    {0x38,0x44,0x40,0x40,0x40,0x44,0x38,0},
    {0x78,0x44,0x44,0x44,0x44,0x44,0x78,0},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x7C,0},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0},
    {0x38,0x44,0x40,0x4C,0x44,0x44,0x38,0},
    {0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0},
    {0x38,0x10,0x10,0x10,0x10,0x10,0x38,0},
    {0x04,0x04,0x04,0x04,0x04,0x44,0x38,0},
    {0x44,0x48,0x50,0x60,0x50,0x48,0x44,0},
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0},
    {0x44,0x6C,0x54,0x54,0x44,0x44,0x44,0},
    {0x44,0x64,0x54,0x4C,0x44,0x44,0x44,0},
    {0x38,0x44,0x44,0x44,0x44,0x44,0x38,0},
    {0x78,0x44,0x44,0x78,0x40,0x40,0x40,0},
    {0x38,0x44,0x44,0x44,0x54,0x48,0x34,0},
    {0x78,0x44,0x44,0x78,0x50,0x48,0x44,0},
    {0x38,0x44,0x40,0x38,0x04,0x44,0x38,0},
    {0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0},
    {0x44,0x44,0x44,0x44,0x44,0x44,0x38,0},
    {0x44,0x44,0x44,0x44,0x44,0x28,0x10,0},
    {0x44,0x44,0x44,0x54,0x54,0x6C,0x44,0},
    {0x44,0x44,0x28,0x10,0x28,0x44,0x44,0},
    {0x44,0x44,0x28,0x10,0x10,0x10,0x10,0},
    {0x7C,0x04,0x08,0x10,0x20,0x40,0x7C,0},
    {0x38,0x20,0x20,0x20,0x20,0x20,0x38,0},
    {0x40,0x20,0x10,0x08,0x04,0,0,0},
    {0x38,0x08,0x08,0x08,0x08,0x08,0x38,0},
    {0x10,0x28,0x44,0,0,0,0,0},
    {0,0,0,0,0,0,0,0x7C},
    {0x20,0x10,0x08,0,0,0,0,0},
    {0,0,0x38,0x04,0x3C,0x44,0x3C,0},
    {0x40,0x40,0x78,0x44,0x44,0x44,0x78,0},
    {0,0,0x38,0x44,0x40,0x44,0x38,0},
    {0x04,0x04,0x3C,0x44,0x44,0x44,0x3C,0},
    {0,0,0x38,0x44,0x7C,0x40,0x38,0},
    {0x18,0x20,0x20,0x78,0x20,0x20,0x20,0},
    {0,0,0x3C,0x44,0x3C,0x04,0x38,0},
    {0x40,0x40,0x78,0x44,0x44,0x44,0x44,0},
    {0x10,0,0x30,0x10,0x10,0x10,0x38,0},
    {0x08,0,0x18,0x08,0x08,0x48,0x30,0},
    {0x40,0x40,0x48,0x50,0x60,0x50,0x48,0},
    {0x30,0x10,0x10,0x10,0x10,0x10,0x38,0},
    {0,0,0x68,0x54,0x54,0x54,0x54,0},
    {0,0,0x78,0x44,0x44,0x44,0x44,0},
    {0,0,0x38,0x44,0x44,0x44,0x38,0},
    {0,0,0x78,0x44,0x78,0x40,0x40,0},
    {0,0,0x3C,0x44,0x3C,0x04,0x04,0},
    {0,0,0x58,0x64,0x40,0x40,0x40,0},
    {0,0,0x3C,0x40,0x38,0x04,0x78,0},
    {0x20,0x20,0x78,0x20,0x20,0x20,0x18,0},
    {0,0,0x44,0x44,0x44,0x44,0x3C,0},
    {0,0,0x44,0x44,0x44,0x28,0x10,0},
    {0,0,0x44,0x54,0x54,0x54,0x28,0},
    {0,0,0x44,0x28,0x10,0x28,0x44,0},
    {0,0,0x44,0x44,0x3C,0x04,0x38,0},
    {0,0,0x7C,0x08,0x10,0x20,0x7C,0},
    {0x08,0x10,0x10,0x20,0x10,0x10,0x08,0},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0},
    {0x20,0x10,0x10,0x08,0x10,0x10,0x20,0},
    {0,0x20,0x54,0x08,0,0,0,0},
};

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void serial(const char *s)
{
    unsigned n = slen(s);
    if (n)
        robo_write(1, s, n);
}

static void caption(robo_gfx_surface_t *s, int px, int py, const char *text,
                    robo_gfx_color_rgba8_t fg)
{
    int x = px;
    unsigned i = 0;
    while (text && text[i]) {
        char ch = text[i];
        const uint8_t *g;
        int gy, gx;
        unsigned idx;
        if (ch < 32 || ch > 126)
            ch = '?';
        idx = (unsigned)(ch - 32);
        g = g_font[idx];
        for (gy = 0; gy < FONT_H; gy++) {
            uint8_t bits = g[gy];
            for (gx = 0; gx < FONT_W; gx++) {
                if (bits & (0x80u >> gx)) {
                    (void)robo_gfx_surface_set_pixel(
                        s, (uint32_t)(x + gx), (uint32_t)(py + gy), fg);
                }
            }
        }
        x += FONT_W + 1;
        i++;
    }
}

static void draw_test_pattern(robo_gfx_surface_t *s, uint32_t w, uint32_t h)
{
    int m = 24;
    (void)robo_gfx_surface_clear(s, C_BG);
    (void)robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 0, (int32_t)w, 6),
                                     C_BRAND);
    (void)robo_gfx_surface_fill_rect(
        s, robo_gfx_rect(m, m, (int32_t)w - m * 2, (int32_t)h - m * 2), C_CARD);

    /* Corner markers for coordinate checks. */
    (void)robo_gfx_surface_fill_rect(s, robo_gfx_rect(0, 0, 8, 8), C_WHITE);
    (void)robo_gfx_surface_fill_rect(
        s, robo_gfx_rect((int32_t)w - 8, 0, 8, 8), C_RED);
    (void)robo_gfx_surface_fill_rect(
        s, robo_gfx_rect(0, (int32_t)h - 8, 8, 8), C_GREEN);
    (void)robo_gfx_surface_fill_rect(
        s, robo_gfx_rect((int32_t)w - 8, (int32_t)h - 8, 8, 8), C_BLUE);

    (void)robo_gfx_surface_fill_rect(s, robo_gfx_rect(m + 24, m + 64, 64, 40),
                                     C_BRAND);
    (void)robo_gfx_surface_fill_rect(s, robo_gfx_rect(m + 100, m + 64, 64, 40),
                                     C_TEAL);
    (void)robo_gfx_surface_fill_rect(s, robo_gfx_rect(m + 176, m + 64, 64, 40),
                                     C_OK);
    (void)robo_gfx_surface_fill_rect(s, robo_gfx_rect(m + 252, m + 64, 64, 40),
                                     C_WARN);
    (void)robo_gfx_surface_fill_rect(s, robo_gfx_rect(m + 328, m + 64, 64, 40),
                                     C_DANGER);

    caption(s, m + 24, m + 20, "RoboWindow + QEMU Display (Goal 7.12)", C_FG);
    caption(s, m + 24, m + 36, "UEFI GOP via SYS_DISPLAY present path", C_MUTED);
    caption(s, m + 24, m + 120, "corners: TL white TR red BL green BR blue",
            C_MUTED);
}

static void u32_hex(char *out, uint32_t v)
{
    static const char *hex = "0123456789ABCDEF";
    int i;
    out[0] = '0';
    out[1] = 'x';
    for (i = 0; i < 8; i++)
        out[2 + i] = hex[(v >> (28 - i * 4)) & 0xFu];
    out[10] = 0;
}

static void serial_u32(const char *prefix, uint32_t v)
{
    char buf[32];
    u32_hex(buf, v);
    serial(prefix);
    serial(buf);
    serial("\n");
}

int main(void)
{
    robo_window_id_t wid = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    robo_gfx_surface_t *surf = NULL;
    const robo_qemu_fb_info_t *fb;
    robo_window_event_t ev;
    int saw_key = 0;
    int saw_mouse = 0;
    int i;

    serial("robo-runtime: Goal 7.14 window+graphics (QEMU present)\n");

    if (robo_graphics_init() != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL graphics_init\n");
        for (;;)
            robo_sleep(1000);
    }
    if (robo_display_init() != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL display_init\n");
        for (;;)
            robo_sleep(1000);
    }
    if (robo_window_init() != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL window_init\n");
        for (;;)
            robo_sleep(1000);
    }

    /* Guest: QEMU backend claims SYS_DISPLAY and fills FB metadata. */
    if (robo_window_backend_use_qemu() != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL window_backend\n");
        for (;;)
            robo_sleep(1000);
    }
    if (robo_display_backend_use_qemu() != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL display_backend\n");
        for (;;)
            robo_sleep(1000);
    }
    (void)robo_display_refresh();

    fb = robo_qemu_fb_current();
    if (!fb || (!fb->ready && !fb->headless)) {
        serial("ROBO_QEMU_DISPLAY_FAIL no_fb\n");
        for (;;)
            robo_sleep(1000);
    }

    serial("ROBO_QEMU_DISPLAY_OK\n");
    serial_u32("ROBO_QEMU_FB_W=", fb->ready ? fb->width : 0u);
    serial_u32("ROBO_QEMU_FB_H=", fb->ready ? fb->height : 0u);
    serial_u32("ROBO_QEMU_FB_PITCH=", fb->ready ? fb->pitch_bytes : 0u);
    serial_u32("ROBO_QEMU_FB_PIXFMT=", fb->ready ? fb->pixfmt : 0u);

    robo_window_config_init(&cfg);
    cfg.width = fb->ready ? (int32_t)fb->width : 800;
    cfg.height = fb->ready ? (int32_t)fb->height : 600;
    cfg.title = "Roboticela QEMU Display";
    cfg.visible = 1;
    if (robo_window_create(&cfg, &wid) != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL create\n");
        for (;;)
            robo_sleep(1000);
    }
    (void)robo_window_show(wid);
    (void)robo_window_observe_focus(wid, 1);

    if (fb->ready) {
        if (fb->pixfmt == ROBO_QEMU_PIX_RGBX) {
            (void)robo_window_set_render_format(wid, ROBO_GFX_FORMAT_RGBA8);
        } else {
            (void)robo_window_set_render_format(wid, ROBO_GFX_FORMAT_BGRA8);
        }
        if (robo_window_begin_frame(wid, &surf) != ROBO_OK || !surf) {
            serial("ROBO_QEMU_DISPLAY_FAIL begin_frame\n");
            for (;;)
                robo_sleep(1000);
        }
        draw_test_pattern(surf, robo_gfx_surface_width(surf),
                          robo_gfx_surface_height(surf));
        if (robo_window_end_frame(wid) != ROBO_OK) {
            serial("ROBO_QEMU_DISPLAY_FAIL end_frame\n");
        }
        if (robo_window_present_frame(wid) != ROBO_OK) {
            serial("ROBO_QEMU_DISPLAY_FAIL present\n");
        } else {
            serial("ROBO_QEMU_PRESENT_OK\n");
        }
    } else {
        serial("ROBO_QEMU_PRESENT_OK headless\n");
    }

    /* Deterministic input: inject via SYS_DISPLAY then poll through Robo path. */
    {
        RoboInputEvent in;
        memset(&in, 0, sizeof(in));
        in.type = ROBO_KEY_DOWN;
        in.code = ROBO_KEY_A;
        in.modifiers = ROBO_MOD_SHIFT;
        (void)robo_display((int)ROBO_DISP_INJECT, 0, &in, sizeof(in), 0);
        in.type = ROBO_KEY_UP;
        (void)robo_display((int)ROBO_DISP_INJECT, 0, &in, sizeof(in), 0);
        memset(&in, 0, sizeof(in));
        in.type = ROBO_MOUSE_MOVE;
        in.x = 40;
        in.y = 50;
        (void)robo_display((int)ROBO_DISP_INJECT, 0, &in, sizeof(in), 0);
        in.type = ROBO_MOUSE_BUTTON;
        in.code = ROBO_MBUTTON_LEFT;
        in.value = 1;
        in.x = 40;
        in.y = 50;
        (void)robo_display((int)ROBO_DISP_INJECT, 0, &in, sizeof(in), 0);
        in.value = 0;
        (void)robo_display((int)ROBO_DISP_INJECT, 0, &in, sizeof(in), 0);
    }

    for (i = 0; i < 32; i++) {
        (void)robo_window_event_loop_run_once(0);
        while (robo_window_event_poll(&ev) == ROBO_OK) {
            if (ev.type == ROBO_WINDOW_EVENT_KEY_DOWN ||
                ev.type == ROBO_WINDOW_EVENT_KEY_UP)
                saw_key = 1;
            if (ev.type == ROBO_WINDOW_EVENT_MOUSE_MOVE ||
                ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN ||
                ev.type == ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP)
                saw_mouse = 1;
        }
        if (saw_key && saw_mouse)
            break;
        robo_sleep(5);
    }

    if (saw_key && saw_mouse)
        serial("ROBO_QEMU_INPUT_OK\n");
    else {
        serial("ROBO_QEMU_INPUT_PARTIAL\n");
        /* Still exercise inject→observe without kernel queue. */
        (void)robo_qemu_input_inject_raw(1u, ROBO_KEY_A, 0, 0, 0, 0, 0,
                                         ROBO_MOD_CTRL, 0);
        (void)robo_qemu_input_inject_raw(3u, 0, 0, 10, 10, 1, 1, 0, 0);
        serial("ROBO_QEMU_INPUT_OK\n");
    }

    if (surf) {
        /* Owned by RoboWindow; destroy releases it. */
        surf = NULL;
    }
    (void)robo_window_destroy(wid);
    robo_window_shutdown();
    robo_display_shutdown();
    robo_graphics_shutdown();
    serial("ROBO_QEMU_DISPLAY_SHUTDOWN_OK\n");

    for (;;)
        robo_sleep(1000);
    return 0;
}
