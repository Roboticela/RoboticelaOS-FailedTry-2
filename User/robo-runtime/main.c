/*
 * RoboticelaOS Runtime â€” the desktop shell on the QEMU display (Phase 11.13).
 *
 * The chain this program drives, end to end, inside the OS:
 *
 *   the checkpoint's markup and author sheet   (RoboDesktopScene)
 *     â†’ RoboHTML tokenizer + tree builder
 *     â†’ RoboCSS style context â†’ RoboLayout â†’ RoboPaint (a display list)
 *     â†’ RoboRender rasteriser (a frame)
 *     â†’ RoboDesktop (background, panel, the window and its decorations)
 *     â†’ RoboWindow begin/end/present_frame
 *     â†’ SYS_DISPLAY â†’ the UEFI GOP framebuffer
 *
 * Input travels the other way: SYS_DISPLAY INJECT â†’ the kernel's input queue â†’ this program's RoboWindow
 * backend â†’ the window queue â†’ `RoboDesktopScene` â†’ `DesktopObservePointer` â†’ the shell's action.
 *
 * The host launcher (`RoboTest.host.exe --desktop`) drives the *same* scene into a Win32 client area, so
 * the two backends share one definition of what the desktop shows; only window creation and presentation
 * differ.
 *
 * What this program is not: it is not the demo layer that used to live here. That layer painted one HTML
 * page straight into the framebuffer with its own throwaway adapter and its own bitmap font, and it was
 * deleted when the shell landed â€” a page on a screen is not a desktop, and the shell is the thing the OS
 * actually boots to.
 *
 * Serial markers for automated validation (only when argv is
 * `checkpoint` or `selftest`; a normal boot does not print them):
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
 *   ROBO_QEMU_DESKTOP_LIVE                checkpoint done; the shell is pumping PS/2 keyboard and mouse
 *   ROBO_QEMU_DISPLAY_FAIL <stage>        a stage refused; nothing after it runs
 *
 * The counters are reported as facts rather than asserted here; the pipeline's own marker list is what
 * pins their values, because a guest that checked its own arithmetic against itself would be checking
 * nothing.
 */

#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
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
#include <robosys/syscallNumbers.h>

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/** The system face, where the image build packs it (`scripts/roboboot-build-common.ps1`). */
#define GUEST_FACE_PATH "/System/Fonts/Inter.ttf"

/** Frames the checkpoint is willing to drive before it reports. The live shell continues after that. */
#define GUEST_MAX_STEPS 48
#define GUEST_STEP_WAIT_MS 1u

/** The status label: the display's own size, written by this program rather than read from a clock. */
#define GUEST_STATUS_MAX 24u

static char gStatus[GUEST_STATUS_MAX];

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

static int seq(const char *a, const char *b)
{
    unsigned i = 0;

    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int GuestCheckpoint(int argc, char **argv)
{
    int i;

    if (argc < 2 || !argv) {
        return 0;
    }
    for (i = 1; i < argc; i++) {
        if (seq(argv[i], "selftest") || seq(argv[i], "checkpoint")) {
            return 1;
        }
    }
    return 0;
}

static void serial(const char *s)
{
    unsigned n = slen(s);

    if (n) {
        Write(1, s, n);
    }
}

static void SerialU32(const char *prefix, uint32_t value)
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

static void SerialText(const char *prefix, const char *text)
{
    serial(prefix);
    serial(text ? text : "(none)");
    serial("\n");
}

static void GuestFail(const char *stage)
{
    serial("ROBO_QEMU_DISPLAY_FAIL ");
    serial(stage);
    serial("\n");
}

static void GuestStop(void)
{
    for (;;) {
        RoboSleep(1000);
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
static void GuestStatusText(uint32_t width, uint32_t height)
{
    unsigned at = decimal(gStatus, width);

    gStatus[at++] = 'x';
    at += decimal(gStatus + at, height);
    gStatus[at] = '\0';
}

/* ------------------------------------------------------------------ */
/* The system face                                                     */
/* ------------------------------------------------------------------ */

/**
 * Read the system face out of the image and build a face from those bytes.
 *
 * The file is read whole into one buffer, copied into a `FontSource`, and the buffer is released: a
 * face keeps the source it was made from (`FontFaceSource`), so what outlives this call is the
 * source and nothing else. A missing or unparseable file returns NULL, and the caller reports it rather
 * than pretending â€” the shell would still draw, with `labelsUnshaped` counting every label it could not
 * shape, which is a *worse* checkpoint and must not pass as a good one.
 *
 * The size comes from `Stat` rather than from seeking the descriptor to its end, which is the idiom
 * every other program in this image reads a file with (`user/robo`, `user/terminal`): one syscall answers
 * "how long is it", and the read then loops, because a single `read` is allowed to return less than was
 * asked for.
 *
 * @param stage Receives the name of the step that failed, so a run that cannot load a face says *which*
 *              part of loading failed instead of leaving the reader to guess between five of them.
 */
static FontFace *GuestFaceLoad(const char **stage)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    unsigned char *bytes;
    RoboStat info;
    size_t size;
    size_t got = 0u;
    int fd;

    *stage = "stat";
    if (Stat(GUEST_FACE_PATH, &info) != 0 || info.size == 0u) {
        return NULL;
    }
    size = (size_t)info.size;

    *stage = "alloc";
    bytes = (unsigned char *)Malloc(size);
    if (!bytes) {
        return NULL;
    }

    *stage = "open";
    fd = open(GUEST_FACE_PATH, O_RDONLY);
    if (fd < 0) {
        RoboFree(bytes);
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
        RoboFree(bytes);
        return NULL;
    }

    *stage = "source";
    if (FontSourceCreateMemoryCopy(bytes, size, &source) != RoboOk) {
        RoboFree(bytes);
        return NULL;
    }
    RoboFree(bytes);

    *stage = "face";
    if (FontFaceCreate(source, 0, &face) != RoboOk) {
        FontSourceRelease(source);
        return NULL;
    }
    /* The face holds its own reference: the source's job here is done. */
    FontSourceRelease(source);
    *stage = "ok";
    return face;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/** Post one input event to the kernel's display queue, which the window backend polls. */
static void GuestInject(uint32_t type, uint32_t code, uint32_t value, int32_t x, int32_t y)
{
    RoboInputEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.device = (type == RoboMouseMove || type == RoboMouseButton) ? RoboInputMouse
                                                                         : RoboInputKeyboard;
    event.code = code;
    event.value = value;
    event.x = x;
    event.y = y;
    (void)Display((int)RoboDispInject, 0, &event, sizeof(event), 0);
}

/**
 * A press on the checkpoint window's title bar.
 *
 * Its job is the shell's own: a press there raises and focuses that window, and `pointerActions` is the
 * count of presses the shell acted on â€” so the marker that follows is about an *action* rather than about
 * an event having been posted. The key press beside it travels the same queue for the same reason: this
 * build routes keys to nothing (`RoboDesktop.h` reports `inputDelivery == 0`), and a guest that claimed
 * a shell action for a key would be claiming a feature the shell does not have.
 */
static void GuestInjectInput(DesktopScene *scene)
{
    Desktop *desktop = DesktopSceneDesktop(scene);
    DesktopWindowId window = DesktopSceneWindow(scene);
    DesktopWindowMetrics metrics;
    GfxRect bounds;
    int32_t x;
    int32_t y;

    DesktopWindowMetricsInit(&metrics);
    if (window == RoboDesktopWindowIdNone ||
        DesktopWindowBounds(desktop, window, &bounds) != RoboOk) {
        return;
    }
    /* The title band: `border .. border + titleHeight`, and the press belongs in the middle of it. */
    x = bounds.x + bounds.width / 2;
    y = bounds.y + metrics.border + metrics.titleHeight / 2;

    GuestInject(RoboKeyDown, RoboKeyA, 0u, 0, 0);
    GuestInject(RoboKeyUp, RoboKeyA, 0u, 0, 0);
    GuestInject(RoboMouseMove, 0u, 0u, x, y);
    GuestInject(RoboMouseButton, RoboMbuttonLeft, 1u, x, y);
    GuestInject(RoboMouseButton, RoboMbuttonLeft, 0u, x, y);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const QemuFbInfo *framebuffer;
    DesktopSceneConfig config;
    DesktopScene *scene = NULL;
    DesktopSceneStep step;
    FontFace *face = NULL;
    const char *faceStage = "(none)";
    DesktopStats stats;
    DesktopDiag diag;
    uint32_t paintedFrames = 0u;
    uint32_t steps = 0u;
    int injected = 0;
    int sawAction = 0;
    int checkpoint = GuestCheckpoint(argc, argv);
    Result result;

    if (checkpoint) {
        LogSetMinLevel(RoboLogDebug);
        serial("robo-runtime: the desktop shell (QEMU display)\n");
    }

    if (!BaseIsInitialized() && BaseInit() != RoboOk) {
        GuestFail("base_init");
        GuestStop();
    }
    if (GraphicsInit() != RoboOk || FontInit() != RoboOk || HtmlInit() != RoboOk ||
        CssInit() != RoboOk || LayoutInit() != RoboOk || PaintInit() != RoboOk) {
        GuestFail("runtime_init");
        GuestStop();
    }
    if (DisplayInit() != RoboOk || WindowInit() != RoboOk) {
        GuestFail("display_init");
        GuestStop();
    }

    /* The QEMU backends claim SYS_DISPLAY for this process and fill in the framebuffer's metadata. */
    if (WindowBackendUseQemu() != RoboOk || DisplayBackendUseQemu() != RoboOk) {
        GuestFail("display_backend");
        GuestStop();
    }
    (void)DisplayRefresh();

    framebuffer = QemuFbCurrent();
    if (!framebuffer || (!framebuffer->ready && !framebuffer->headless)) {
        GuestFail("no_fb");
        GuestStop();
    }
    if (checkpoint) {
        serial("ROBO_QEMU_DISPLAY_OK\n");
        SerialU32("ROBO_QEMU_FB_W=", framebuffer->ready ? framebuffer->width : 0u);
        SerialU32("ROBO_QEMU_FB_H=", framebuffer->ready ? framebuffer->height : 0u);
        SerialU32("ROBO_QEMU_FB_PITCH=", framebuffer->ready ? framebuffer->pitchBytes : 0u);
        SerialU32("ROBO_QEMU_FB_PIXFMT=", framebuffer->ready ? framebuffer->pixfmt : 0u);
    }

    face = GuestFaceLoad(&faceStage);
    if (checkpoint) {
        if (!face) {
            serial("ROBO_QEMU_FACE_FAIL ");
            serial(faceStage);
            serial(" ");
            serial(GUEST_FACE_PATH);
            serial("\n");
        } else {
            serial("ROBO_QEMU_FACE_OK\n");
        }
    }

    GuestStatusText(framebuffer->ready ? framebuffer->width : RoboDesktopSceneDefaultWidth,
                      framebuffer->ready ? framebuffer->height : RoboDesktopSceneDefaultHeight);

    memset(&diag, 0, sizeof(diag));
    DesktopSceneConfigInit(&config);
    config.width = framebuffer->ready ? (int32_t)framebuffer->width : 0;
    config.height = framebuffer->ready ? (int32_t)framebuffer->height : 0;
    config.face = face;
    config.RoboCreateWindow = 1;
    config.showCursor = 1;
    config.statusText = gStatus;

    result = DesktopSceneOpen(&config, &scene, &diag);
    if (result != RoboOk) {
        SerialText("ROBO_QEMU_DESKTOP_FAIL ", diag.message ? diag.message : "(none)");
        GuestFail("scene_open");
        GuestStop();
    }
    if (checkpoint) {
        serial("ROBO_QEMU_DESKTOP_OK\n");
        SerialText("ROBO_QEMU_DESKTOP_WINDOW=", RoboDesktopSceneWindowTitle);
        SerialText("ROBO_QEMU_DESKTOP_STATUS=", gStatus);
    }

    if (checkpoint) {
        while (steps < GUEST_MAX_STEPS) {
            steps++;
            if (GetDesktopSceneStep(scene, GUEST_STEP_WAIT_MS, &step, &diag) != RoboOk) {
                SerialText("ROBO_QEMU_DESKTOP_FAIL ", diag.message ? diag.message : "(none)");
                GuestFail("scene_step");
                GuestStop();
            }
            if (step.painted) {
                paintedFrames++;
                SerialU32("ROBO_QEMU_DESKTOP_WINDOWS=", (uint32_t)step.frame.windows);
                SerialU32("ROBO_QEMU_DESKTOP_CONTENT_RUNS=", (uint32_t)step.frame.contentRuns);
                SerialU32("ROBO_QEMU_DESKTOP_LABELS=", (uint32_t)step.frame.labels);
                SerialU32("ROBO_QEMU_DESKTOP_UNSHAPED=", (uint32_t)step.frame.labelsUnshaped);
                SerialU32("ROBO_QEMU_DESKTOP_FILLS=", (uint32_t)step.frame.fills);
                SerialU32("ROBO_QEMU_DESKTOP_PIXELS=", (uint32_t)step.frame.contentPixels);
                if (step.presented) {
                    serial("ROBO_QEMU_PRESENT_OK\n");
                    SerialU32("ROBO_QEMU_PRESENT_COMPLETE=", (uint32_t)step.present.complete);
                    SerialU32("ROBO_QEMU_PRESENT_PIXELS=", (uint32_t)step.present.presentedPixels);
                } else {
                    GuestFail("present");
                    GuestStop();
                }
                if (!injected) {
                    injected = 1;
                    GuestInjectInput(scene);
                }
            }
            if (step.pointerActions > 0u) {
                sawAction = 1;
            }
            if (sawAction && paintedFrames > 0u) {
                break;
            }
        }

        memset(&stats, 0, sizeof(stats));
        (void)GetDesktopStats(DesktopSceneDesktop(scene), &stats);
        SerialU32("ROBO_QEMU_DESKTOP_FRAMES=", (uint32_t)stats.frames);
        SerialU32("ROBO_QEMU_INPUT_ACTIONS=", (uint32_t)stats.pointerActions);
        if (sawAction) {
            serial("ROBO_QEMU_INPUT_OK\n");
        } else {
            serial("ROBO_QEMU_INPUT_PARTIAL\n");
        }
        serial("ROBO_QEMU_DESKTOP_LIVE\n");
    }

    for (;;) {
        if (GetDesktopSceneStep(scene, GUEST_STEP_WAIT_MS, &step, &diag) != RoboOk) {
            SerialText("ROBO_QEMU_DESKTOP_FAIL ", diag.message ? diag.message : "(none)");
            GuestFail("scene_live");
            GuestStop();
        }
    }
}
