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
 *   ROBO_QEMU_FACE_OK                     the system face under /System/Fonts was read, parsed and mapped
 *   ROBO_QEMU_FACE_FAIL <stage> <path>    the system face could not be loaded; <stage> is the step
 *                                         (stat, alloc, open, read, source, face) that refused
 *   ROBO_QEMU_TYPOGRAPHY_OK               the pack under /System/Fonts opened and every role resolved
 *   ROBO_QEMU_TYPOGRAPHY_ROLES            roles the resolver answered
 *   ROBO_QEMU_TYPOGRAPHY_FACES            registered faces in the opened pack
 *   ROBO_QEMU_TYPOGRAPHY_FAIL <stage>     the pack, the fallback or the resolver refused, so every
 *                                         surface drew with the single fallback face instead (a *worse*
 *                                         checkpoint, which the unshaped count below will confirm)
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
 *   ROBO_QEMU_SHAPING_OK                  the §44 specimen document laid out and painted off-screen
 *   ROBO_QEMU_SHAPING_RUNS                text runs the specimen's display list executed
 *   ROBO_QEMU_SHAPING_GLYPHS              glyphs those runs carry
 *   ROBO_QEMU_SHAPING_MISSING             glyphs with no face coverage (the pack ships one family)
 *   ROBO_QEMU_SHAPING_DRAWN               glyphs the drawing pass actually drew
 *   ROBO_QEMU_SHAPING_PIXELS              ink pixels the specimen wrote into its surface
 *   ROBO_QEMU_SHAPING_FAMILIES            `font-family` entries the per-run walk resolved (12.17)
 *   ROBO_QEMU_SHAPING_UNRESOLVED          entries it could not (0 is the verdict)
 *   ROBO_QEMU_SHAPING_FAIL <stage>        the specimen refused; the shell markers above still stand
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
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontLoader.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontSystemPack.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboLayout/RoboLayout.h"
#include "RoboMem/RoboMem.h"
#include "RoboPaint/RoboPaint.h"
#include "RoboTypography/RoboTypography.h"
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

/**
 * The system typeface pack, where the image installs it (Phase 12.2).
 *
 * `/System/Fonts/` is the pack root, `system-fonts.manifest` is its manifest and `UI/Inter.ttf` is the
 * face the manifest names for the UI Display and UI Text roles. Phase 12.3 opens the *pack* and draws
 * from its roles; the direct path below stays as the fallback for a pack that will not open, so a guest
 * build with a damaged manifest is a worse checkpoint rather than a dead one.
 */
#define GUEST_FACE_PATH "/System/Fonts/UI/Inter.ttf"
#define GUEST_PACK_ROOT "/System/Fonts"

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
/* The system typeface pack, and the typography resolver over it        */
/* ------------------------------------------------------------------ */

/**
 * The guest's typography stack: the pack under `/System/Fonts` and the resolver the shell draws from.
 *
 * Phase 12.2's pack owns the manifest, the registrations and the loader cache; the resolver is built
 * *over* it and owns nothing it borrows (see `RoboTypographyResolver.h`). One struct keeps the four
 * handles alive in the order they must die in, so `GuestTypographyClose` cannot get it wrong.
 */
typedef struct guestTypography {
    FontRegistry *registry;
    FontLoader *loader;
    FontSystemPack *pack;
    FontFallbackResolver *fallback;
    TypographyResolver *resolver;
} GuestTypography;

/**
 * Read @p path whole for the pack, the way every other program in this image reads a file.
 *
 * The pack's only IO is "read this path whole", so this is `GuestFaceLoad`'s stat/alloc/open/read loop
 * factored out and answering in `FontDiscoveryIo`'s vocabulary instead of returning a face: a missing
 * file is `RoboNotFound`, a file that will not read whole is `RoboParseError`, and a buffer that would
 * not allocate is `RoboOutOfMemory`. A file larger than @p maxSize is **refused rather than truncated**,
 * because a truncated manifest would parse as a different manifest â€” a silent partial pack is exactly
 * what this phase must not ship.
 *
 * The buffer is allocated with `Malloc` because `FontDiscoveryIo`'s contract says the caller releases it
 * with `RoboFree`; the two agree here, as they do everywhere in this runtime.
 */
static Result GuestPackRead(void *userdata,
                            const char *path,
                            size_t maxSize,
                            uint8_t **outBytes,
                            size_t *outSize)
{
    uint8_t *bytes;
    RoboStat info;
    size_t size;
    size_t got = 0u;
    int fd;

    (void)userdata;
    if (!path || !outBytes || !outSize) {
        return RoboInvalidArgument;
    }
    *outBytes = NULL;
    *outSize = 0u;
    if (Stat(path, &info) != 0 || info.size == 0u) {
        return RoboNotFound;
    }
    size = (size_t)info.size;
    if (size > maxSize) {
        return RoboParseError;
    }
    bytes = (uint8_t *)Malloc(size);
    if (!bytes) {
        return RoboOutOfMemory;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        RoboFree(bytes);
        return RoboNotFound;
    }
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
        return RoboParseError;
    }
    *outBytes = bytes;
    *outSize = size;
    return RoboOk;
}

static void GuestTypographyClose(GuestTypography *typography);

/**
 * Open the system typeface pack and build the shell's typography resolver.

 * A pack that will not open is **not** fatal, and that is deliberate: the checkpoint then falls back to
 * drawing every surface with the caller's single face, `labelsUnshaped` records the labels it could not
 * shape, and a human reading the serial says "worse", not "dead". What the return value buys the caller
 * is the difference between those two checkpoints being *reported* instead of inferred.
 *
 * @param stage Receives the name of the step that refused (registry, loader, pack, fallback, resolver).
 * @return 1 opened, 0 unavailable.
 */
static int GuestTypographyOpen(GuestTypography *out, const char **stage)
{
    FontLoaderConfig loaderConfig;
    FontSystemPackConfig packConfig;
    FontDiscoveryIo io;
    FontFallbackResolver *fallback = NULL;
    TypographyResolver *resolver = NULL;

    memset(out, 0, sizeof(*out));
    *stage = "registry";
    if (FontRegistryCreate(&out->registry) != RoboOk) {
        return 0;
    }
    *stage = "loader";
    FontLoaderConfigInit(&loaderConfig);
    if (FontLoaderCreate(out->registry, &loaderConfig, &out->loader) != RoboOk) {
        GuestTypographyClose(out);
        return 0;
    }
    *stage = "pack";
    memset(&io, 0, sizeof(io));
    io.RoboReadFile = GuestPackRead;
    FontSystemPackConfigInit(&packConfig);
    packConfig.root = GUEST_PACK_ROOT;
    packConfig.io = &io;
    if (FontSystemPackOpen(out->registry, out->loader, &packConfig, &out->pack) != RoboOk) {
        GuestTypographyClose(out);
        return 0;
    }
    *stage = "fallback";
    if (FontFallbackResolverCreate(out->registry, NULL, &fallback) != RoboOk) {
        GuestTypographyClose(out);
        return 0;
    }
    out->fallback = fallback;
    *stage = "resolver";
    {
        RoboTypographyResolverConfig resolverConfig;

        RoboTypographyResolverConfigInit(&resolverConfig);
        resolverConfig.pack = out->pack;
        resolverConfig.fallback = out->fallback;
        if (RoboTypographyResolverCreate(&resolverConfig, &resolver, NULL) != RoboOk) {
            GuestTypographyClose(out);
            return 0;
        }
    }
    out->resolver = resolver;
    *stage = "ok";
    return 1;
}

/** Tear the typography stack down in the reverse of the order it was built. Safe on a partial struct. */
static void GuestTypographyClose(GuestTypography *typography)
{
    if (!typography) {
        return;
    }
    RoboTypographyResolverDestroy(typography->resolver);
    typography->resolver = NULL;
    FontFallbackResolverDestroy(typography->fallback);
    typography->fallback = NULL;
    FontSystemPackClose(typography->pack);
    typography->pack = NULL;
    FontLoaderDestroy(typography->loader);
    typography->loader = NULL;
    FontRegistryDestroy(typography->registry);
    typography->registry = NULL;
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
/* The shaping conformance specimen (Phase 12.18 §44)                   */
/* ------------------------------------------------------------------ */

/**
 * The specimen's page: the phase's §33 list, one line per claim.
 *
 * Latin and Latin Extended, the words a ligature and a pair live in, Greek, Cyrillic, combining marks,
 * punctuation, and one RTL line beside Latin. It is a *guest* page and not the shared checkpoint scene
 * (`RoboDesktopScene.h`) on purpose: that scene's frame numbers are pinned by the pipeline's marker list for
 * both launchers, and a conformance specimen is a second render rather than a reason to move them.
 *
 * The last line is honest about coverage rather than decorative. The shipped pack is Inter, which carries no
 * Arabic or Hebrew, so those glyphs are the fallback boundary: what they must do here is lay out, paint and
 * not corrupt the lines around them, and `ROBO_QEMU_SHAPING_MISSING` is the number that says how many of them
 * had no glyph -- a fact a reader can see rather than a conformance claim.
 */
static const char kShapingSpecimen[] =
    "<html><body>"
    "<p>The quick brown fox jumps over the lazy dog.</p>"
    "<p>office ffl ffi configuration AVAVA To Ta Wa Yo LT Ty</p>"
    "<p>Latin Extended: \xC3\xA4 \xC3\xB6 \xC3\xBC \xC3\xA9 \xC3\xA0 \xC3\xA7 \xC3\xB1 \xC3\x9F \xC3\xB8"
    " \xC5\x82 \xC5\x93</p>"
    "<p>Greek: \xCE\x91\xCE\xBB\xCF\x86\xCE\xB1 \xCE\xB1\xCE\xB2\xCE\xB3</p>"
    "<p>Cyrillic: \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82</p>"
    "<p>Marks: e\xCC\x81 a\xCC\x80 o\xCC\x88 n\xCC\x83</p>"
    "<p>Punctuation: Hello, world. \"quoted\" (parens) [brackets] - dash</p>"
    "<p class=\"mono\">RTL: \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7"
    " beside Latin</p>"
    "</body></html>";

/** Geometry only, plus one `font-family` so the per-run family walk (12.17) is exercised too. */
static const char kShapingSpecimenSheet[] =
    "html, body, p { display: block; margin: 0; padding: 0; }"
    ".mono { font-family: ui-monospace, monospace; }";

#define GUEST_SHAPING_WIDTH 900
#define GUEST_SHAPING_HEIGHT 420

/**
 * Render the specimen off-screen, through the real chain, and report what happened.
 *
 * This is the phase's §44 evidence: not a host-side shape, and not a screenshot — the guest builds a
 * document (HTML → CSS → layout → records → display list) and executes that display list into a
 * RoboGraphics surface with the *pack's* face, then reports the counts a reader can check against the
 * pinned marker list. `ROBO_QEMU_SHAPING_MISSING` is deliberately a reported number rather than an asserted
 * zero: the pack ships one family, so the RTL line above has no glyphs, and pretending otherwise would be a
 * marker that lied.
 *
 * Nothing is presented: there is no window and no backend here, so the checkpoint's own frame numbers stay
 * the numbers the display test pins.
 */
static void GuestShapingCheckpoint(FontFace *face, const GuestTypography *typography)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    DesktopDocumentReport report;
    DesktopDiag diag;
    const PaintDisplayList *list;
    const PaintGlyphPool *pool;
    const PaintGlyph *glyphs;
    PaintDecorationStats stats;
    PaintDiag paintDiag;
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    uint32_t runs = 0u;
    uint32_t glyphCount = 0u;
    uint32_t missing = 0u;
    uint32_t ink = 0u;
    uint32_t y;
    size_t index;

    memset(&config, 0, sizeof(config));
    DesktopDocumentConfigInit(&config);
    config.html = kShapingSpecimen;
    config.css = kShapingSpecimenSheet;
    config.width = GUEST_SHAPING_WIDTH;
    config.height = GUEST_SHAPING_HEIGHT;
    config.face = face;
    if (typography) {
        config.typography = typography->resolver;
        /* The per-run family walk: a named family through the registry, a generic through the pack. */
        config.registry = typography->registry;
    }

    memset(&diag, 0, sizeof(diag));
    if (DesktopDocumentBuild(&config, &document, &diag) != RoboOk) {
        SerialText("ROBO_QEMU_SHAPING_FAIL ", diag.message ? diag.message : "(none)");
        return;
    }

    list = DesktopDocumentDisplayList(document);
    pool = PaintDisplayListGlyphPool(list);
    glyphs = PaintGlyphPoolGlyphs(pool);
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        memset(&command, 0, sizeof(command));
        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            continue;
        }
        if (command.kind != RoboPaintDisplayCmdDrawText) {
            continue;
        }
        runs++;
        glyphCount += command.text.glyphs.count;
        if (glyphs) {
            uint32_t g;

            for (g = 0u; g < command.text.glyphs.count; g++) {
                if (glyphs[command.text.glyphs.first + g].flags & PaintGlyphFlagMissing) {
                    missing++;
                }
            }
        }
    }

    memset(&desc, 0, sizeof(desc));
    GfxSurfaceDescInit(&desc);
    desc.width = GUEST_SHAPING_WIDTH;
    desc.height = GUEST_SHAPING_HEIGHT;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    if (GfxSurfaceCreate(&desc, &surface) != RoboOk || !surface) {
        serial("ROBO_QEMU_SHAPING_FAIL surface\n");
        DesktopDocumentDestroy(document);
        return;
    }

    memset(&stats, 0, sizeof(stats));
    memset(&paintDiag, 0, sizeof(paintDiag));
    /* No face argument: the list carries its glyphs *and* the table its token names (12.16), so a replay
     * resolves the faces the document selected rather than a face this program hands it. */
    if (PaintDecorationPaint(list, surface, NULL, &stats, &paintDiag) != RoboOk) {
        SerialText("ROBO_QEMU_SHAPING_FAIL ",
                   paintDiag.message ? paintDiag.message : "(no message)");
        GfxSurfaceRelease(surface);
        DesktopDocumentDestroy(document);
        return;
    }

    for (y = 0u; y < GUEST_SHAPING_HEIGHT; y++) {
        uint32_t x;

        for (x = 0u; x < GUEST_SHAPING_WIDTH; x++) {
            GfxColorRgba8 color;

            memset(&color, 0, sizeof(color));
            if (GfxSurfaceGetPixel(surface, x, y, &color) != RoboOk) {
                continue;
            }
            if (color.r != 0u || color.g != 0u || color.b != 0u || color.a != 0u) {
                ink++;
            }
        }
    }

    memset(&report, 0, sizeof(report));
    (void)GetDesktopDocumentReport(document, &report);

    serial("ROBO_QEMU_SHAPING_OK\n");
    SerialU32("ROBO_QEMU_SHAPING_RUNS=", runs);
    SerialU32("ROBO_QEMU_SHAPING_GLYPHS=", glyphCount);
    SerialU32("ROBO_QEMU_SHAPING_MISSING=", missing);
    SerialU32("ROBO_QEMU_SHAPING_DRAWN=", (uint32_t)stats.glyphs);
    SerialU32("ROBO_QEMU_SHAPING_PIXELS=", ink);
    SerialU32("ROBO_QEMU_SHAPING_FAMILIES=", (uint32_t)report.fontFamiliesResolved);
    SerialU32("ROBO_QEMU_SHAPING_UNRESOLVED=", (uint32_t)report.fontFamiliesUnresolved);

    GfxSurfaceRelease(surface);
    DesktopDocumentDestroy(document);
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
    GuestTypography typography;
    FontFace *face = NULL;
    const char *faceStage = "(none)";
    const char *typographyStage = "(none)";
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

    /*
     * Phase 12.3: the shell draws from the pack's *roles*, not from the single face above.
     *
     * The face load stays because a pack that will not open must still leave a renderable desktop: the
     * scene treats a NULL resolver as "draw everything with `config.face`", which is a worse checkpoint
     * rather than a dead one. Opening the pack second is intentional -- its own registrations replace the
     * hand-built face for every role, and `ROBO_QEMU_DESKTOP_UNSHAPED` is what tells the two apart.
     */
    if (!GuestTypographyOpen(&typography, &typographyStage)) {
        if (checkpoint) {
            serial("ROBO_QEMU_TYPOGRAPHY_FAIL ");
            serial(typographyStage);
            serial("\n");
        }
    } else if (checkpoint) {
        serial("ROBO_QEMU_TYPOGRAPHY_OK\n");
        SerialU32("ROBO_QEMU_TYPOGRAPHY_ROLES=", (uint32_t)RoboTypographyRoleCount);
        SerialU32("ROBO_QEMU_TYPOGRAPHY_FACES=",
                  (uint32_t)FontSystemPackRegisteredFaces(typography.pack));
    }

    GuestStatusText(framebuffer->ready ? framebuffer->width : RoboDesktopSceneDefaultWidth,
                      framebuffer->ready ? framebuffer->height : RoboDesktopSceneDefaultHeight);

    memset(&diag, 0, sizeof(diag));
    DesktopSceneConfigInit(&config);
    config.width = framebuffer->ready ? (int32_t)framebuffer->width : 0;
    config.height = framebuffer->ready ? (int32_t)framebuffer->height : 0;
    config.face = face;
    config.typography = typography.resolver;
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
        /*
         * The shaping specimen (Phase 12.18 §44), after the frame markers and before the terminal one: a
         * second, off-screen render whose numbers are its own, so the display test's pinned scene numbers
         * stay the scene's.
         */
        GuestShapingCheckpoint(face, &typography);
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
