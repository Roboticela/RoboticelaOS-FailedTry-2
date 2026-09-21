/*
 * RoboticelaOS Runtime — the desktop shell on the QEMU display (Phase 11.13).
 *
 * The chain this program drives, end to end, inside the OS:
 *
 *   the checkpoint's markup and author sheet   (RoboDesktopScene)
 *     → RoboHTML tokenizer + tree builder
 *     → RoboCSS style context → RoboLayout → RoboPaint (a display list)
 *     → RoboRender rasteriser (a frame)
 *     → RoboDesktop (background, panel, the window and its decorations)
 *     → RoboWindow begin/end/present_frame
 *     → SYS_DISPLAY → the UEFI GOP framebuffer
 *
 * Input travels the other way: SYS_DISPLAY INJECT → the kernel's input queue → this program's RoboWindow
 * backend → the window queue → `RoboDesktopScene` → `robo_desktop_observe_pointer` → the shell's action.
 *
 * The host launcher (`RoboTest.host.exe --desktop`) drives the *same* scene into a Win32 client area, so
 * the two backends share one definition of what the desktop shows; only window creation and presentation
 * differ.
 *
 * What this program is not: it is not the demo layer that used to live here. That layer painted one HTML
 * page straight into the framebuffer with its own throwaway adapter and its own bitmap font, and it was
 * deleted when the shell landed — a page on a screen is not a desktop, and the shell is the thing the OS
 * actually boots to.
 *
 * Serial markers for automated validation:
 *   ROBO_QEMU_DISPLAY_OK                  the framebuffer is described (see the FB markers below)
 *   ROBO_QEMU_FB_W / _FB_H / _FB_PITCH / _FB_PIXFMT
 *   ROBO_QEMU_FACE_OK                     /System/Fonts/Inter.ttf was read, parsed and mapped
 *   ROBO_QEMU_FACE_FAIL <stage> <path>    the system face could not be loaded; <stage> is the step
 *                                         (stat, alloc, open, read, source, face) that refused
 *   ROBO_QEMU_DESKTOP_OK                  the shell, its window and the document came up
 *   ROBO_QEMU_DESKTOP_WINDOWS             windows painted in the reported frame
 *   ROBO_QEMU_DESKTOP_CONTENT_RUNS        display-list runs executed for the window's contents
 *   ROBO_QEMU_DESKTOP_LABELS              chrome labels drawn (identity, applications, status, title)
 *   ROBO_QEMU_DESKTOP_UNSHAPED            labels a missing face would have cost: **0** is the text verdict
 *   ROBO_QEMU_DESKTOP_FILLS               shell fills executed in the reported frame
 *   ROBO_QEMU_DESKTOP_PIXELS              window-content pixels composited into the frame
 *   ROBO_QEMU_PRESENT_OK                  the frame reached the window's backend
 *   ROBO_QEMU_PRESENT_COMPLETE            every frame pixel landed in the destination
 *   ROBO_QEMU_PRESENT_PIXELS              frame pixels written to the destination
 *   ROBO_QEMU_INPUT_OK / _INPUT_PARTIAL   a display-injected press reached the shell as an action
 *   ROBO_QEMU_INPUT_ACTIONS               presses the shell acted on
 *   ROBO_QEMU_DISPLAY_SHUTDOWN_OK         every layer came down cleanly
 *   ROBO_QEMU_DISPLAY_FAIL <stage>        a stage refused; nothing after it runs
 *
 * The counters are reported as facts rather than asserted here; the pipeline's own marker list is what
 * pins their values, because a guest that checked its own arithmetic against itself would be checking
 * nothing.
 */

#include "RoboBase/RoboBase.h"
#include "RoboCSS/RoboCSS.h"
#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopDocument.h"
#include "RoboDesktop/RoboDesktopScene.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendQemu.h"
#include "RoboDisplay/RoboQemuFramebuffer.h"
#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboLayout/RoboLayout.h"
#include "RoboMem/RoboMem.h"
#include "RoboPaint/RoboPaint.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendQemu.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <robosys/display.h>
#include <robosys/input.h>
#include <robosys/syscall.h>
#include <robosys/syscall_numbers.h>

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/** The system face, where the image build packs it (`scripts/roboboot-build-common.ps1`). */
#define GUEST_FACE_PATH "/System/Fonts/Inter.ttf"

/** Frames the loop is willing to drive before it reports: the first one paints, the rest answer input. */
#define GUEST_MAX_STEPS 48
#define GUEST_STEP_WAIT_MS 16u

/** The status label: the display's own size, written by this program rather than read from a clock. */
#define GUEST_STATUS_MAX 24u

static char g_status[GUEST_STATUS_MAX];

/* ------------------------------------------------------------------ */
/* Serial reporting                                                    */
/* ------------------------------------------------------------------ */

static unsigned slen(const char *s)
{
    unsigned n = 0;

    while (s && s[n]) {
        n++;
    }
    return n;
}

static void serial(const char *s)
{
    unsigned n = slen(s);

    if (n) {
        robo_write(1, s, n);
    }
}

static void serial_u32(const char *prefix, uint32_t value)
{
    static const char *hex = "0123456789ABCDEF";
    char buffer[16];
    int index;

    buffer[0] = '0';
    buffer[1] = 'x';
    for (index = 0; index < 8; index++) {
        buffer[2 + index] = hex[(value >> (28 - index * 4)) & 0xFu];
    }
    buffer[10] = '\0';
    serial(prefix);
    serial(buffer);
    serial("\n");
}

static void serial_text(const char *prefix, const char *text)
{
    serial(prefix);
    serial(text ? text : "(none)");
    serial("\n");
}

static void guest_fail(const char *stage)
{
    serial("ROBO_QEMU_DISPLAY_FAIL ");
    serial(stage);
    serial("\n");
}

static void guest_stop(void)
{
    for (;;) {
        robo_sleep(1000);
    }
}

/* ------------------------------------------------------------------ */
/* The display's size, as a label                                      */
/* ------------------------------------------------------------------ */

/** Write @p value as decimal into @p out (which must hold 11 bytes); returns the digits written. */
static unsigned decimal(char *out, uint32_t value)
{
    char digits[11];
    unsigned count = 0u;
    unsigned index;

    if (value == 0u) {
        out[0] = '0';
        return 1u;
    }
    while (value > 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    for (index = 0u; index < count; index++) {
        out[index] = digits[count - 1u - index];
    }
    return count;
}

/** "<width>x<height>", the panel's status label. */
static void guest_status_text(uint32_t width, uint32_t height)
{
    unsigned at = decimal(g_status, width);

    g_status[at++] = 'x';
    at += decimal(g_status + at, height);
    g_status[at] = '\0';
}

/* ------------------------------------------------------------------ */
/* The system face                                                     */
/* ------------------------------------------------------------------ */

/**
 * Read the system face out of the image and build a face from those bytes.
 *
 * The file is read whole into one buffer, copied into a `robo_font_source`, and the buffer is released: a
 * face keeps the source it was made from (`robo_font_face_source`), so what outlives this call is the
 * source and nothing else. A missing or unparseable file returns NULL, and the caller reports it rather
 * than pretending — the shell would still draw, with `labels_unshaped` counting every label it could not
 * shape, which is a *worse* checkpoint and must not pass as a good one.
 *
 * The size comes from `robo_stat` rather than from seeking the descriptor to its end, which is the idiom
 * every other program in this image reads a file with (`user/robo`, `user/terminal`): one syscall answers
 * "how long is it", and the read then loops, because a single `read` is allowed to return less than was
 * asked for.
 *
 * @param stage Receives the name of the step that failed, so a run that cannot load a face says *which*
 *              part of loading failed instead of leaving the reader to guess between five of them.
 */
static robo_font_face_t *guest_face_load(const char **stage)
{
    robo_font_source_t *source = NULL;
    robo_font_face_t *face = NULL;
    unsigned char *bytes;
    RoboStat info;
    size_t size;
    size_t got = 0u;
    int fd;

    *stage = "stat";
    if (robo_stat(GUEST_FACE_PATH, &info) != 0 || info.size == 0u) {
        return NULL;
    }
    size = (size_t)info.size;

    *stage = "alloc";
    bytes = (unsigned char *)robo_malloc(size);
    if (!bytes) {
        return NULL;
    }

    *stage = "open";
    fd = open(GUEST_FACE_PATH, O_RDONLY);
    if (fd < 0) {
        robo_free(bytes);
        return NULL;
    }

    *stage = "read";
    while (got < size) {
        ssize_t chunk = read(fd, bytes + got, size - got);

        if (chunk <= 0) {
            break;
        }
        got += (size_t)chunk;
    }
    close(fd);
    if (got != size) {
        robo_free(bytes);
        return NULL;
    }

    *stage = "source";
    if (robo_font_source_create_memory_copy(bytes, size, &source) != ROBO_OK) {
        robo_free(bytes);
        return NULL;
    }
    robo_free(bytes);

    *stage = "face";
    if (robo_font_face_create(source, 0, &face) != ROBO_OK) {
        robo_font_source_release(source);
        return NULL;
    }
    /* The face holds its own reference: the source's job here is done. */
    robo_font_source_release(source);
    *stage = "ok";
    return face;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/** Post one input event to the kernel's display queue, which the window backend polls. */
static void guest_inject(uint32_t type, uint32_t code, uint32_t value, int32_t x, int32_t y)
{
    RoboInputEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.device = (type == ROBO_MOUSE_MOVE || type == ROBO_MOUSE_BUTTON) ? ROBO_INPUT_MOUSE
                                                                         : ROBO_INPUT_KEYBOARD;
    event.code = code;
    event.value = value;
    event.x = x;
    event.y = y;
    (void)robo_display((int)ROBO_DISP_INJECT, 0, &event, sizeof(event), 0);
}

/**
 * A press on the checkpoint window's title bar.
 *
 * Its job is the shell's own: a press there raises and focuses that window, and `pointer_actions` is the
 * count of presses the shell acted on — so the marker that follows is about an *action* rather than about
 * an event having been posted. The key press beside it travels the same queue for the same reason: this
 * build routes keys to nothing (`RoboDesktop.h` reports `input_delivery == 0`), and a guest that claimed
 * a shell action for a key would be claiming a feature the shell does not have.
 */
static void guest_inject_input(robo_desktop_scene_t *scene)
{
    robo_desktop_t *desktop = robo_desktop_scene_desktop(scene);
    robo_desktop_window_id_t window = robo_desktop_scene_window(scene);
    robo_desktop_window_metrics_t metrics;
    robo_gfx_rect_t bounds;
    int32_t x;
    int32_t y;

    robo_desktop_window_metrics_init(&metrics);
    if (window == ROBO_DESKTOP_WINDOW_ID_NONE ||
        robo_desktop_window_bounds(desktop, window, &bounds) != ROBO_OK) {
        return;
    }
    /* The title band: `border .. border + title_height`, and the press belongs in the middle of it. */
    x = bounds.x + bounds.width / 2;
    y = bounds.y + metrics.border + metrics.title_height / 2;

    guest_inject(ROBO_KEY_DOWN, ROBO_KEY_A, 0u, 0, 0);
    guest_inject(ROBO_KEY_UP, ROBO_KEY_A, 0u, 0, 0);
    guest_inject(ROBO_MOUSE_MOVE, 0u, 0u, x, y);
    guest_inject(ROBO_MOUSE_BUTTON, ROBO_MBUTTON_LEFT, 1u, x, y);
    guest_inject(ROBO_MOUSE_BUTTON, ROBO_MBUTTON_LEFT, 0u, x, y);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    const robo_qemu_fb_info_t *framebuffer;
    robo_desktop_scene_config_t config;
    robo_desktop_scene_t *scene = NULL;
    robo_desktop_scene_step_t step;
    robo_font_face_t *face = NULL;
    const char *face_stage = "(none)";
    robo_desktop_stats_t stats;
    robo_desktop_diag_t diag;
    uint32_t painted_frames = 0u;
    uint32_t steps = 0u;
    int injected = 0;
    int saw_action = 0;
    robo_result_t result;

    serial("robo-runtime: the desktop shell (QEMU display)\n");

    if (!robo_base_is_initialized() && robo_base_init() != ROBO_OK) {
        guest_fail("base_init");
        guest_stop();
    }
    if (robo_graphics_init() != ROBO_OK || robo_font_init() != ROBO_OK || robo_html_init() != ROBO_OK ||
        robo_css_init() != ROBO_OK || robo_layout_init() != ROBO_OK || robo_paint_init() != ROBO_OK) {
        guest_fail("runtime_init");
        guest_stop();
    }
    if (robo_display_init() != ROBO_OK || robo_window_init() != ROBO_OK) {
        guest_fail("display_init");
        guest_stop();
    }

    /* The QEMU backends claim SYS_DISPLAY for this process and fill in the framebuffer's metadata. */
    if (robo_window_backend_use_qemu() != ROBO_OK || robo_display_backend_use_qemu() != ROBO_OK) {
        guest_fail("display_backend");
        guest_stop();
    }
    (void)robo_display_refresh();

    framebuffer = robo_qemu_fb_current();
    if (!framebuffer || (!framebuffer->ready && !framebuffer->headless)) {
        guest_fail("no_fb");
        guest_stop();
    }
    serial("ROBO_QEMU_DISPLAY_OK\n");
    serial_u32("ROBO_QEMU_FB_W=", framebuffer->ready ? framebuffer->width : 0u);
    serial_u32("ROBO_QEMU_FB_H=", framebuffer->ready ? framebuffer->height : 0u);
    serial_u32("ROBO_QEMU_FB_PITCH=", framebuffer->ready ? framebuffer->pitch_bytes : 0u);
    serial_u32("ROBO_QEMU_FB_PIXFMT=", framebuffer->ready ? framebuffer->pixfmt : 0u);

    face = guest_face_load(&face_stage);
    if (!face) {
        serial("ROBO_QEMU_FACE_FAIL ");
        serial(face_stage);
        serial(" ");
        serial(GUEST_FACE_PATH);
        serial("\n");
    } else {
        serial("ROBO_QEMU_FACE_OK\n");
    }

    guest_status_text(framebuffer->ready ? framebuffer->width : ROBO_DESKTOP_SCENE_DEFAULT_WIDTH,
                      framebuffer->ready ? framebuffer->height : ROBO_DESKTOP_SCENE_DEFAULT_HEIGHT);

    memset(&diag, 0, sizeof(diag));
    robo_desktop_scene_config_init(&config);
    config.width = framebuffer->ready ? (int32_t)framebuffer->width : 0;
    config.height = framebuffer->ready ? (int32_t)framebuffer->height : 0;
    config.face = face;
    config.create_window = 1;
    config.status_text = g_status;

    result = robo_desktop_scene_open(&config, &scene, &diag);
    if (result != ROBO_OK) {
        serial_text("ROBO_QEMU_DESKTOP_FAIL ", diag.message ? diag.message : "(none)");
        guest_fail("scene_open");
        guest_stop();
    }
    serial("ROBO_QEMU_DESKTOP_OK\n");
    serial_text("ROBO_QEMU_DESKTOP_WINDOW=", ROBO_DESKTOP_SCENE_WINDOW_TITLE);
    serial_text("ROBO_QEMU_DESKTOP_STATUS=", g_status);

    /*
     * Drive the scene until the picture has been painted once and the injected press has been *acted* on.
     * A press is asynchronous — it is posted into the kernel's queue and collected by the window backend on
     * a later pump — so the loop runs on rather than checking the counter at the moment of the injection.
     */
    while (steps < GUEST_MAX_STEPS) {
        steps++;
        if (robo_desktop_scene_step(scene, GUEST_STEP_WAIT_MS, &step, &diag) != ROBO_OK) {
            serial_text("ROBO_QEMU_DESKTOP_FAIL ", diag.message ? diag.message : "(none)");
            guest_fail("scene_step");
            guest_stop();
        }
        if (step.painted) {
            painted_frames++;
            serial_u32("ROBO_QEMU_DESKTOP_WINDOWS=", (uint32_t)step.frame.windows);
            serial_u32("ROBO_QEMU_DESKTOP_CONTENT_RUNS=", (uint32_t)step.frame.content_runs);
            serial_u32("ROBO_QEMU_DESKTOP_LABELS=", (uint32_t)step.frame.labels);
            serial_u32("ROBO_QEMU_DESKTOP_UNSHAPED=", (uint32_t)step.frame.labels_unshaped);
            serial_u32("ROBO_QEMU_DESKTOP_FILLS=", (uint32_t)step.frame.fills);
            serial_u32("ROBO_QEMU_DESKTOP_PIXELS=", (uint32_t)step.frame.content_pixels);
            if (step.presented) {
                serial("ROBO_QEMU_PRESENT_OK\n");
                serial_u32("ROBO_QEMU_PRESENT_COMPLETE=", (uint32_t)step.present.complete);
                serial_u32("ROBO_QEMU_PRESENT_PIXELS=", (uint32_t)step.present.presented_pixels);
            } else {
                /* A scene with nowhere to present is a desktop that renders; a *windowed* one that did not
                 * present is a failure and says so. */
                guest_fail("present");
                guest_stop();
            }
            if (!injected) {
                injected = 1;
                guest_inject_input(scene);
            }
        }
        if (step.pointer_actions > 0u) {
            saw_action = 1;
        }
        if (saw_action && painted_frames > 0u) {
            break;
        }
    }

    memset(&stats, 0, sizeof(stats));
    (void)robo_desktop_stats(robo_desktop_scene_desktop(scene), &stats);
    serial_u32("ROBO_QEMU_DESKTOP_FRAMES=", (uint32_t)stats.frames);
    serial_u32("ROBO_QEMU_INPUT_ACTIONS=", (uint32_t)stats.pointer_actions);
    if (saw_action) {
        serial("ROBO_QEMU_INPUT_OK\n");
    } else {
        serial("ROBO_QEMU_INPUT_PARTIAL\n");
    }

    robo_desktop_scene_close(scene);
    if (face) {
        robo_font_face_release(face);
    }
    (void)robo_window_shutdown();
    robo_display_shutdown();
    robo_paint_shutdown();
    robo_layout_shutdown();
    robo_css_shutdown();
    robo_html_shutdown();
    robo_font_shutdown();
    robo_graphics_shutdown();
    serial("ROBO_QEMU_DISPLAY_SHUTDOWN_OK\n");

    guest_stop();
    return 0;
}
