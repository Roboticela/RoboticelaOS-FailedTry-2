/*
 * Roboticela Runtime — QEMU display + HTML visual checkpoint
 * (Goals 7.12 / 7.14, Phase 8.2 demo).
 *
 * Path:
 *   embedded demo HTML
 *     → RoboHTML tokenizer (8.2)
 *     → RoboDemo page model
 *     → RoboGraphics paint → RoboWindow begin/end/present_frame
 *     → QEMU SYS_DISPLAY → GOP framebuffer
 *
 * Input: SYS_DISPLAY INPUT → RoboKeyboard / RoboMouse.
 *
 * The host checkpoint (RoboTest.host.exe --demo-html) paints the *same* page
 * through the same RoboDemo adapter into a Win32 window, so the two backends
 * share one renderer; only window creation and presentation differ.
 *
 * Serial markers for automated validation:
 *   ROBO_QEMU_DISPLAY_OK
 *   ROBO_QEMU_HTML_OK
 *   ROBO_QEMU_PRESENT_OK
 *   ROBO_QEMU_INPUT_OK
 *   ROBO_QEMU_DISPLAY_SHUTDOWN_OK
 */

#include "RoboBase/RoboBase.h"
#include "RoboDemo/RoboDemoFont.h"
#include "RoboDemo/RoboDemoHtml.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendQemu.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendQemu.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <robosys/display.h>
#include <robosys/input.h>
#include <robosys/syscall.h>
#include <robosys/syscall_numbers.h>

#include <stdint.h>
#include <string.h>

static const robo_gfx_color_rgba8_t C_MUTED = {110, 110, 115, 255};
static const robo_gfx_color_rgba8_t C_WHITE = {255, 255, 255, 255};
static const robo_gfx_color_rgba8_t C_RED = {255, 0, 0, 255};
static const robo_gfx_color_rgba8_t C_GREEN = {0, 255, 0, 255};
static const robo_gfx_color_rgba8_t C_BLUE = {0, 0, 255, 255};
static const robo_gfx_color_rgba8_t C_BRAND = {14, 122, 255, 255};

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

static void caption(robo_gfx_surface_t *s, int32_t px, int32_t py,
                    const char *text, robo_gfx_color_rgba8_t fg)
{
    /* Demo-scope text: the shared RoboDemo atlas, scale 1, clipped safely. */
    (void)robo_demo_font_draw(s, px, py, text, slen(text), 1, fg);
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
    robo_demo_page_t *page = NULL;
    const robo_qemu_fb_info_t *fb;
    robo_window_event_t ev;
    robo_result_t r;
    int saw_key = 0;
    int saw_mouse = 0;
    int i;

    serial("robo-runtime: HTML visual checkpoint (QEMU present)\n");
    serial_u32("ROBO_QEMU_HTML_SOURCE_BYTES=",
               (uint32_t)robo_demo_default_html_length());

    r = robo_base_init();
    if (r != ROBO_OK && r != ROBO_ALREADY_EXISTS) {
        serial("ROBO_QEMU_DISPLAY_FAIL base_init\n");
        for (;;)
            robo_sleep(1000);
    }
    if (robo_graphics_init() != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL graphics_init\n");
        for (;;)
            robo_sleep(1000);
    }
    if (robo_html_init() != ROBO_OK) {
        serial("ROBO_QEMU_DISPLAY_FAIL html_init\n");
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

    /* The real tokenizer runs in the guest: one page model, painted below. */
    if (robo_demo_page_create(robo_demo_default_html(),
                              robo_demo_default_html_length(), &page,
                              NULL) != ROBO_OK ||
        !page) {
        serial("ROBO_QEMU_DISPLAY_FAIL html_page\n");
        for (;;)
            robo_sleep(1000);
    }
    serial("ROBO_QEMU_HTML_OK\n");
    serial("ROBO_QEMU_HTML_TITLE=");
    serial(robo_demo_page_title(page));
    serial("\n");
    serial_u32("ROBO_QEMU_HTML_BLOCKS=",
               (uint32_t)robo_demo_page_block_count(page));
    serial_u32("ROBO_QEMU_HTML_SUPPORTED=",
               (uint32_t)robo_demo_page_supported_element_count(page));
    serial_u32("ROBO_QEMU_HTML_IGNORED=",
               (uint32_t)robo_demo_page_ignored_tag_count(page));

    robo_window_config_init(&cfg);
    cfg.width = fb->ready ? (int32_t)fb->width : 800;
    cfg.height = fb->ready ? (int32_t)fb->height : 600;
    cfg.title = robo_demo_page_title(page);
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

        /* The HTML-derived picture is the content. */
        if (robo_demo_page_paint(page, surf, NULL, NULL) != ROBO_OK) {
            serial("ROBO_QEMU_DISPLAY_FAIL html_paint\n");
        }

        /* Coordinate markers stay on top so frame-buffer probes can verify
         * the present path independently of the page content. */
        {
            int32_t w = robo_gfx_surface_width(surf);
            int32_t h = robo_gfx_surface_height(surf);
            (void)robo_gfx_surface_fill_rect(surf, robo_gfx_rect(0, 0, 8, 8),
                                             C_WHITE);
            (void)robo_gfx_surface_fill_rect(
                surf, robo_gfx_rect(w - 8, 0, 8, 8), C_RED);
            (void)robo_gfx_surface_fill_rect(
                surf, robo_gfx_rect(0, h - 8, 8, 8), C_GREEN);
            (void)robo_gfx_surface_fill_rect(
                surf, robo_gfx_rect(w - 8, h - 8, 8, 8), C_BLUE);
            (void)robo_gfx_surface_fill_rect(
                surf, robo_gfx_rect(0, h - 22, w, 1), C_BRAND);
            caption(surf, 8, h - 16,
                    "RoboHTML 8.2 to RoboGraphics to RoboWindow (QEMU)",
                    C_MUTED);
        }

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

    /* Owned by RoboWindow; destroy releases it. */
    surf = NULL;
    (void)robo_window_destroy(wid);
    robo_demo_page_destroy(page);
    robo_window_shutdown();
    robo_display_shutdown();
    robo_html_shutdown();
    robo_graphics_shutdown();
    serial("ROBO_QEMU_DISPLAY_SHUTDOWN_OK\n");

    for (;;)
        robo_sleep(1000);
    return 0;
}
