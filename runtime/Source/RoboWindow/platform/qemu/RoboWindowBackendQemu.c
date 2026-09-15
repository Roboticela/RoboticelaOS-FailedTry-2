/*
 * RoboWindow QEMU backend (Goal 7.12).
 *
 * Guest: SYS_DISPLAY claim/info/present/input (UEFI GOP via kernel).
 * Host sim: blit into robo_qemu_sim framebuffer.
 */

#include "RoboWindow/RoboWindowBackendQemu.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendQemu.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowPresent.h"
#include "RoboWindow/RoboWindowState.h"

#include <string.h>

#if defined(ROBO_WINDOW_HAS_QEMU) && ROBO_WINDOW_HAS_QEMU
#include <robosys/display.h>
#include <robosys/input.h>
#include <robosys/syscall.h>
#include <robosys/syscall_numbers.h>
#endif

#define ROBO_QEMU_WIN_TOKEN 1u
#define ROBO_QEMU_WIN_GEN 1u

typedef struct qemu_win {
    int live;
    int claimed;
    robo_window_id_t window_id;
    int32_t width;
    int32_t height;
} qemu_win_t;

static qemu_win_t g_win;
static robo_window_backend_caps_t g_caps;
static int g_guest_mode;

#if defined(ROBO_WINDOW_HAS_QEMU) && ROBO_WINDOW_HAS_QEMU
static int sys_is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static robo_result_t guest_claim(void)
{
    robo_long r;
    RoboUserDisplay info;
    robo_qemu_fb_info_t fb;

    r = robo_display((int)ROBO_DISP_CLAIM, 0, 0, 0,
                     (int)(ROBO_DISP_ROLE_OUTPUT | ROBO_DISP_ROLE_INPUT));
    if (sys_is_err(r)) {
        robo_log_error("robo_window.qemu: SYS_DISPLAY CLAIM failed");
        return ROBO_IO_ERROR;
    }
    memset(&info, 0, sizeof(info));
    r = robo_display((int)ROBO_DISP_INFO, 0, &info, sizeof(info), 0);
    if (sys_is_err(r)) {
        (void)robo_display((int)ROBO_DISP_RELEASE, 0, 0, 0, 0);
        return ROBO_IO_ERROR;
    }
    robo_qemu_fb_info_init(&fb);
    if (!info.ready || !info.width || !info.height) {
        fb.headless = 1;
        fb.ready = 0;
        (void)robo_qemu_fb_set_current(&fb);
        g_win.claimed = 1;
        robo_log_warn("robo_window.qemu: headless display claim");
        return ROBO_OK;
    }
    fb.width = info.width;
    fb.height = info.height;
    fb.pitch_bytes = info.pitch ? info.pitch : info.width * 4u;
    fb.bpp = info.bpp ? info.bpp : 32u;
    fb.pixfmt = info.pixfmt;
    fb.pixels = NULL; /* present via syscall */
    fb.byte_size = 0u;
    fb.ready = 1;
    fb.headless = 0;
    if (robo_qemu_fb_set_current(&fb) != ROBO_OK) {
        (void)robo_display((int)ROBO_DISP_RELEASE, 0, 0, 0, 0);
        return ROBO_INVALID_ARGUMENT;
    }
    g_win.claimed = 1;
    robo_log_info("robo_window.qemu: claimed %ux%u pitch=%u pixfmt=%u",
                  (unsigned)fb.width, (unsigned)fb.height,
                  (unsigned)fb.pitch_bytes, (unsigned)fb.pixfmt);
    return ROBO_OK;
}

static void guest_release(void)
{
    if (g_win.claimed) {
        (void)robo_display((int)ROBO_DISP_RELEASE, 0, 0, 0, 0);
        g_win.claimed = 0;
    }
}

static robo_result_t guest_present(const robo_gfx_surface_t *surface)
{
    RoboDisplayPresent req;
    const uint8_t *px;
    robo_long r;
    const robo_qemu_fb_info_t *fb = robo_qemu_fb_current();

    if (!fb || fb->headless || !fb->ready) {
        return ROBO_OK; /* headless accept */
    }
    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    px = robo_gfx_surface_pixels_const(surface);
    if (!px) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(&req, 0, sizeof(req));
    req.pixels = (uint64_t)(uintptr_t)px;
    req.width = robo_gfx_surface_width(surface);
    req.height = robo_gfx_surface_height(surface);
    {
        /* Kernel SYS_DISPLAY PRESENT: stride is in pixels (32 bpp rows). */
        size_t stride_bytes = robo_gfx_surface_stride(surface);
        uint8_t bpp = 4u;
        (void)robo_gfx_pixel_format_bytes_per_pixel(
            robo_gfx_surface_format(surface), &bpp);
        if (bpp == 0u) {
            bpp = 4u;
        }
        req.stride = (uint32_t)(stride_bytes / (size_t)bpp);
        if (req.stride < req.width) {
            req.stride = req.width;
        }
    }
    if (robo_qemu_fb_pixfmt_from_gfx(robo_gfx_surface_format(surface),
                                     &req.pixfmt) != ROBO_OK) {
        req.pixfmt = fb->pixfmt;
    }
    req.dest_x = 0;
    req.dest_y = 0;
    r = robo_display((int)ROBO_DISP_PRESENT, 0, &req, sizeof(req), 0);
    if (sys_is_err(r)) {
        robo_log_warn("robo_window.qemu: PRESENT failed");
        return ROBO_IO_ERROR;
    }
    return ROBO_OK;
}
#endif /* ROBO_WINDOW_HAS_QEMU */

static void caps_init(void)
{
    memset(&g_caps, 0, sizeof(g_caps));
    g_caps.create_window = 1;
    g_caps.destroy_window = 1;
    g_caps.show_hide = 1;
    g_caps.resize = 1;
    g_caps.move = 1;
    g_caps.set_title = 1;
    g_caps.focus = 1;
    g_caps.query_state = 1;
    g_caps.present = 1;
    g_caps.process_events = 1;
    g_caps.reports_actual_state = 1;
    g_caps.size_constraints = 1;
    g_caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT;
    g_caps.present_max_width = 8192u;
    g_caps.present_max_height = 8192u;
    g_caps.wait_events = 0;
    g_caps.wakeup = 0;
    g_caps.mouse_capture = 0;
}

static robo_result_t qemu_initialize(void *ud)
{
    (void)ud;
    memset(&g_win, 0, sizeof(g_win));
    caps_init();
#if defined(ROBO_WINDOW_HAS_QEMU) && ROBO_WINDOW_HAS_QEMU
    g_guest_mode = 1;
    return guest_claim();
#else
    g_guest_mode = 0;
    if (!robo_qemu_fb_current() || !robo_qemu_fb_current()->ready) {
        robo_log_error("robo_window.qemu: no framebuffer (call sim_init first)");
        return ROBO_INIT_ERROR;
    }
    return ROBO_OK;
#endif
}

static void qemu_shutdown(void *ud)
{
    (void)ud;
#if defined(ROBO_WINDOW_HAS_QEMU) && ROBO_WINDOW_HAS_QEMU
    guest_release();
#endif
    memset(&g_win, 0, sizeof(g_win));
    if (!robo_qemu_sim_is_active()) {
        robo_qemu_fb_clear_current();
    }
}

static robo_result_t qemu_create_window(void *ud, robo_window_id_t id,
                                        const robo_window_config_t *cfg,
                                        robo_window_native_handle_t *out)
{
    const robo_qemu_fb_info_t *fb = robo_qemu_fb_current();
    (void)ud;
    (void)cfg;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (g_win.live) {
        return ROBO_BUSY;
    }
    if (!fb || (!fb->ready && !fb->headless)) {
        return ROBO_NOT_FOUND;
    }
    g_win.live = 1;
    g_win.window_id = id;
    if (fb->ready) {
        g_win.width = (int32_t)fb->width;
        g_win.height = (int32_t)fb->height;
    } else {
        g_win.width = cfg && cfg->width > 0 ? cfg->width : 640;
        g_win.height = cfg && cfg->height > 0 ? cfg->height : 480;
    }
    return robo_window_native_handle_assign(out, ROBO_QEMU_WIN_TOKEN,
                                            ROBO_QEMU_WIN_GEN);
}

static robo_result_t qemu_destroy_window(void *ud, robo_window_id_t id,
                                         robo_window_native_handle_t *native)
{
    (void)ud;
    if (g_win.live && g_win.window_id == id) {
        g_win.live = 0;
        g_win.window_id = ROBO_WINDOW_ID_NONE;
    }
    if (native) {
        robo_window_native_handle_invalidate(native);
    }
    return ROBO_OK;
}

static robo_result_t qemu_ok(void *ud, robo_window_id_t id,
                             robo_window_native_handle_t *n)
{
    (void)ud;
    (void)id;
    (void)n;
    return ROBO_OK;
}

static robo_result_t qemu_title(void *ud, robo_window_id_t id,
                                robo_window_native_handle_t *n, const char *t)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)t;
    return ROBO_OK;
}

static robo_result_t qemu_size(void *ud, robo_window_id_t id,
                               robo_window_native_handle_t *n, int32_t w,
                               int32_t h)
{
    (void)ud;
    (void)n;
    if (!g_win.live || g_win.window_id != id) {
        return ROBO_NOT_FOUND;
    }
    /* Fixed FB: keep actual size; note request for observe path. */
    (void)w;
    (void)h;
    return ROBO_OK;
}

static robo_result_t qemu_cons(void *ud, robo_window_id_t id,
                               robo_window_native_handle_t *n, int32_t a,
                               int32_t b, int32_t c, int32_t d)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return ROBO_OK;
}

static robo_result_t qemu_pos(void *ud, robo_window_id_t id,
                              robo_window_native_handle_t *n, int32_t x,
                              int32_t y)
{
    (void)ud;
    (void)id;
    (void)n;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t qemu_query(void *ud, robo_window_id_t id,
                                const robo_window_native_handle_t *n,
                                robo_window_state_t *st)
{
    (void)ud;
    (void)n;
    if (!st || !g_win.live || g_win.window_id != id) {
        return ROBO_NOT_FOUND;
    }
    st->width = g_win.width;
    st->height = g_win.height;
    st->x = 0;
    st->y = 0;
    st->visible = 1;
    st->focused = 1;
    st->scale = 1.0f;
    return ROBO_OK;
}

static robo_result_t qemu_present(void *ud, robo_window_id_t id,
                                  robo_window_native_handle_t *n,
                                  robo_gfx_surface_t *surface)
{
    const robo_qemu_fb_info_t *fb;
    (void)ud;
    (void)n;
    if (!g_win.live || g_win.window_id != id) {
        return ROBO_NOT_FOUND;
    }
    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
#if defined(ROBO_WINDOW_HAS_QEMU) && ROBO_WINDOW_HAS_QEMU
    if (g_guest_mode) {
        return guest_present(surface);
    }
#endif
    fb = robo_qemu_fb_current();
    if (!fb) {
        return ROBO_NOT_FOUND;
    }
    return robo_qemu_fb_present_surface(fb, surface, 0, 0);
}

/* --- Input translation (Robosys HID → RoboKeyboard / RoboMouse) --- */

static robo_key_modifier_flags_t map_mods(uint32_t m)
{
    robo_key_modifier_flags_t out = ROBO_KEY_MOD_NONE;
#if defined(ROBO_WINDOW_HAS_QEMU) && ROBO_WINDOW_HAS_QEMU
    if (m & ROBO_MOD_SHIFT) {
        out |= ROBO_KEY_MOD_SHIFT;
    }
    if (m & ROBO_MOD_CTRL) {
        out |= ROBO_KEY_MOD_CTRL;
    }
    if (m & ROBO_MOD_ALT) {
        out |= ROBO_KEY_MOD_ALT;
    }
    if (m & ROBO_MOD_META) {
        out |= ROBO_KEY_MOD_META;
    }
#else
    /* Host inject uses the same bit layout as robosys/input.h. */
    if (m & 1u) {
        out |= ROBO_KEY_MOD_SHIFT;
    }
    if (m & 2u) {
        out |= ROBO_KEY_MOD_CTRL;
    }
    if (m & 4u) {
        out |= ROBO_KEY_MOD_ALT;
    }
    if (m & 8u) {
        out |= ROBO_KEY_MOD_META;
    }
#endif
    return out;
}

static void map_hid_key(uint32_t code, robo_physical_key_t *phys,
                        robo_logical_key_t *log)
{
    *phys = ROBO_PHYSICAL_KEY_UNKNOWN;
    *log = ROBO_LOGICAL_KEY_UNKNOWN;
    /* HID usage → Robo keys (subset; unknown codes stay UNKNOWN). */
    if (code >= 0x04u && code <= 0x1Du) {
        /* A..Z */
        unsigned i = (unsigned)(code - 0x04u);
        *phys = (robo_physical_key_t)(ROBO_PHYSICAL_KEY_A + (int)i);
        *log = (robo_logical_key_t)(ROBO_LOGICAL_KEY_A + (int)i);
        return;
    }
    if (code >= 0x1Eu && code <= 0x26u) {
        unsigned i = (unsigned)(code - 0x1Eu);
        *phys = (robo_physical_key_t)(ROBO_PHYSICAL_KEY_DIGIT_1 + (int)i);
        *log = (robo_logical_key_t)(ROBO_LOGICAL_KEY_DIGIT_1 + (int)i);
        return;
    }
    if (code == 0x27u) {
        *phys = ROBO_PHYSICAL_KEY_DIGIT_0;
        *log = ROBO_LOGICAL_KEY_DIGIT_0;
        return;
    }
    switch (code) {
    case 0x28u:
        *phys = ROBO_PHYSICAL_KEY_ENTER;
        *log = ROBO_LOGICAL_KEY_ENTER;
        break;
    case 0x29u:
        *phys = ROBO_PHYSICAL_KEY_ESCAPE;
        *log = ROBO_LOGICAL_KEY_ESCAPE;
        break;
    case 0x2Au:
        *phys = ROBO_PHYSICAL_KEY_BACKSPACE;
        *log = ROBO_LOGICAL_KEY_BACKSPACE;
        break;
    case 0x2Bu:
        *phys = ROBO_PHYSICAL_KEY_TAB;
        *log = ROBO_LOGICAL_KEY_TAB;
        break;
    case 0x2Cu:
        *phys = ROBO_PHYSICAL_KEY_SPACE;
        *log = ROBO_LOGICAL_KEY_SPACE;
        break;
    default:
        break;
    }
}

static robo_result_t translate_input(uint32_t type, uint32_t code,
                                     uint32_t value, int32_t x, int32_t y,
                                     int32_t dx, int32_t dy, uint32_t modifiers,
                                     uint32_t buttons)
{
    robo_window_id_t wid =
        g_win.live ? g_win.window_id : ROBO_WINDOW_ID_NONE;
    robo_key_modifier_flags_t mods = map_mods(modifiers);

    (void)buttons;
    (void)value;

    /* Keyboard */
    if (type == 1u /* ROBO_KEY_DOWN */ || type == 2u /* ROBO_KEY_UP */) {
        robo_keyboard_event_t ke;
        robo_physical_key_t phys;
        robo_logical_key_t log;
        map_hid_key(code, &phys, &log);
        robo_keyboard_event_init(&ke);
        ke.action =
            (type == 1u) ? ROBO_KEY_ACTION_DOWN : ROBO_KEY_ACTION_UP;
        ke.physical_key = phys;
        ke.logical_key = log;
        ke.modifiers = mods;
        return robo_window_observe_key(wid, &ke);
    }

    /* Mouse move */
    if (type == 3u) {
        robo_mouse_event_t me;
        robo_mouse_event_init(&me);
        me.type = ROBO_MOUSE_EVENT_MOVE;
        me.client_x = x;
        me.client_y = y;
        me.delta_x = dx;
        me.delta_y = dy;
        me.modifiers = mods;
        return robo_window_observe_mouse(wid, &me);
    }

    /* Mouse button: code = button mask bit, value = 1 down / 0 up */
    if (type == 4u) {
        robo_mouse_event_t me;
        robo_mouse_button_t btn = ROBO_MOUSE_BUTTON_UNKNOWN;
        if (code & 1u) {
            btn = ROBO_MOUSE_BUTTON_LEFT;
        } else if (code & 2u) {
            btn = ROBO_MOUSE_BUTTON_RIGHT;
        } else if (code & 4u) {
            btn = ROBO_MOUSE_BUTTON_MIDDLE;
        }
        robo_mouse_event_init(&me);
        me.type = value ? ROBO_MOUSE_EVENT_BUTTON_DOWN
                        : ROBO_MOUSE_EVENT_BUTTON_UP;
        me.button = btn;
        me.client_x = x;
        me.client_y = y;
        me.modifiers = mods;
        me.click_count = value ? 1 : 0;
        return robo_window_observe_mouse(wid, &me);
    }

    /* Scroll: dy/dx in notches * 120 convention when value unused */
    if (type == 5u) {
        robo_mouse_event_t me;
        robo_mouse_event_init(&me);
        me.type = ROBO_MOUSE_EVENT_WHEEL;
        me.client_x = x;
        me.client_y = y;
        me.scroll_x = dx;
        me.scroll_y = dy != 0 ? dy : (int32_t)value;
        me.scroll_unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
        me.modifiers = mods;
        return robo_window_observe_mouse(wid, &me);
    }

    return ROBO_OK;
}

robo_result_t robo_qemu_input_inject_raw(uint32_t type, uint32_t code,
                                         uint32_t value, int32_t x, int32_t y,
                                         int32_t dx, int32_t dy,
                                         uint32_t modifiers, uint32_t buttons)
{
    return translate_input(type, code, value, x, y, dx, dy, modifiers, buttons);
}

robo_result_t robo_qemu_input_poll(void)
{
#if defined(ROBO_WINDOW_HAS_QEMU) && ROBO_WINDOW_HAS_QEMU
    int n = 0;
    if (!g_win.claimed) {
        return ROBO_OK;
    }
    for (;;) {
        RoboInputEvent ev;
        robo_long r;
        memset(&ev, 0, sizeof(ev));
        r = robo_display((int)ROBO_DISP_INPUT, 0, &ev, sizeof(ev),
                         (int)ROBO_DISP_NOWAIT);
        if (sys_is_err(r)) {
            break;
        }
        (void)translate_input(ev.type, ev.code, ev.value, ev.x, ev.y, ev.dx,
                              ev.dy, ev.modifiers, ev.buttons);
        n++;
        if (n > 64) {
            break;
        }
    }
#else
    /* Host: nothing to poll unless tests inject. */
#endif
    return ROBO_OK;
}

static robo_result_t qemu_process_events(void *ud)
{
    (void)ud;
    return robo_qemu_input_poll();
}

static const robo_window_backend_vtbl_t g_qemu_vtbl = {
    "qemu",
    &g_caps,
    qemu_initialize,
    qemu_shutdown,
    qemu_create_window,
    qemu_destroy_window,
    qemu_ok, /* show */
    qemu_ok, /* hide */
    qemu_title,
    qemu_size,
    qemu_cons,
    qemu_pos,
    qemu_ok, /* focus */
    qemu_query,
    qemu_present,
    qemu_process_events,
    NULL,
    NULL,
    NULL,
    NULL
};

const robo_window_backend_vtbl_t *robo_window_backend_qemu_vtbl(void)
{
    return &g_qemu_vtbl;
}

robo_result_t robo_window_backend_use_qemu(void)
{
    caps_init();
    return robo_window_backend_register(&g_qemu_vtbl, NULL);
}

robo_result_t robo_window_backend_use_qemu_sim(uint32_t width, uint32_t height,
                                               uint32_t pixfmt)
{
    robo_result_t r;
    r = robo_qemu_sim_init(width, height, pixfmt);
    if (r != ROBO_OK) {
        return r;
    }
    if (!robo_display_is_initialized()) {
        r = robo_display_init();
        if (r != ROBO_OK) {
            robo_qemu_sim_shutdown();
            return r;
        }
    }
    r = robo_display_backend_use_qemu();
    if (r != ROBO_OK) {
        robo_qemu_sim_shutdown();
        return r;
    }
    (void)robo_display_refresh();
    caps_init();
    g_guest_mode = 0;
    return robo_window_backend_register(&g_qemu_vtbl, NULL);
}
