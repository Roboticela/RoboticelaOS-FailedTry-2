/*
 * RoboPaint background suite (Phase 11.4, through the 11.5 decoration pass) —
 * paint.background.*
 *
 * The first place in this module where a colour reaches a **surface**, and the claims the
 * release makes, each pinned where it can be observed rather than argued. The door these
 * cases walk is `PaintDecorationPaint`, which 11.5 generalised from the
 * background-only painter of 11.4 to fill a box's border bands in the same pass -- the
 * cases here are about the **background** half of it, and the border suite beside this one
 * covers the other:
 *
 *   **the colour bridge**  one field-for-field copy between two 8-bit sRGB records, with the
 *                          three kinds that are *not* colours a surface can take decided
 *                          rather than flattened -- so `transparent` draws nothing,
 *                          `currentColor` is refused, `none` is "there was no colour" and
 *                          `invalid` is a mistake;
 *
 *   **pixels**             a document's `background-color` lands where the display list said
 *                          its rectangle is, and *only* there -- asserted by reading the
 *                          surface back, which is the strongest form of "the colour reached
 *                          RoboGraphics" available without a screenshot;
 *
 *   **paint order**        the traversal's order is the order of the fills, so a child's
 *                          background covers its parent's where they overlap -- the classic
 *                          proof that the sequence is depth-first and not a set;
 *
 *   **clipping**           the boundary an ancestor's `overflow: hidden` put in force bounds
 *                          a descendant's fill, demonstrated against a control document with
 *                          the same boxes and `visible` overflow;
 *
 *   **the fence**          the three **structural** commands are *skipped and counted*: they
 *                          draw nothing, by definition, which is what `raster == 0` means.
 *                          A border band is *not* skipped -- it is a drawing, and since 11.5
 *                          it is filled by this same pass -- so the case that used to pin
 *                          "a border-only document paints nothing" now pins the opposite,
 *                          which is the fence moving one command kind along;
 *
 *   **snapshot**           once a display list exists, clearing the record list and laying the
 *                          tree out again does not change a single pixel it paints.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and then paints it into a real RoboGraphics surface. Hand-built *commands*
 * appear only where the question is about the painter's own contract -- the four counters, the
 * clip application -- and never in place of a layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on
 * no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers
 * (so a fixture's boxes stack in flow order), and `<head>` stays the one box nothing styles. */
#define BACKGROUND_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* Two colours a pixel assertion can name, and one that is deliberately not opaque. */
#define BACKGROUND_RED GfxRgba8(255, 0, 0, 255)
#define BACKGROUND_BLUE GfxRgba8(0, 0, 255, 255)

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/** A CPU surface for a background pass: owned, RGBA8, zero-filled (so "untouched" is
 *  transparent and an unpainted pixel is distinguishable from a black one). */
static GfxSurface *BackgroundSurface(uint32_t width,
                                              uint32_t height,
                                              GfxSurfaceAccess access)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    desc.access = access;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    return surface;
}

/** Build a display list from @p records, asserting the build succeeds. */
static void BackgroundBuild(const PaintResult *records,
                             PaintDisplayList **outList,
                             PaintDisplayListStats *outStats)
{
    PaintDisplayListBuilder *builder = NULL;
    PaintDiag diag;
    Result status;

    RoboCheckEqInt(PaintDisplayListCreate(outList, NULL), RoboOk);
    RoboCheckEqInt(PaintDisplayListBuilderCreate(&builder, NULL), RoboOk);
    PaintDiagReset(&diag);
    status = PaintDisplayListBuilderBuild(builder, records->list, *outList, outStats,
                                                   &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    PaintDisplayListBuilderDestroy(builder);
}

/**
 * Paint @p list into @p target, asserting the pass succeeds.
 *
 * The pass is *not* asserted to fill anything -- three of the cases below are about a document
 * that draws nothing -- but a failure is always a bug in a case, so the diagnostic is reported.
 */
static void BackgroundPaint(const PaintDisplayList *list,
                             GfxSurface *target,
                             PaintDecorationStats *outStats)
{
    PaintDiag diag;
    Result status;

    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, target, NULL, outStats, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the background pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The @p nth command of kind @p kind whose node is @p node, or 0 when there is none. */
static int BackgroundCommandOfKindFor(const PaintDisplayList *list,
                                          PaintDisplayCommandKind kind,
                                          LayoutNodeId node,
                                          size_t nth,
                                          PaintDisplayCommand *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return 0;
        }
        if (command.kind != kind || command.node != node) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = command;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** The @p nth `draw-background` command whose node is @p node, or 0 when there is none. */
static int BackgroundCommandFor(const PaintDisplayList *list,
                                  LayoutNodeId node,
                                  size_t nth,
                                  PaintDisplayCommand *out)
{
    return BackgroundCommandOfKindFor(list, RoboPaintDisplayCmdDrawBackground, node, nth,
                                          out);
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int BackgroundNthOfKind(const PaintDisplayList *list,
                                  PaintDisplayCommandKind kind,
                                  size_t nth,
                                  PaintDisplayCommand *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return 0;
        }
        if (command.kind != kind) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = command;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** Read the pixel at (x, y), failing when the point is outside the surface. */
static GfxColorRgba8 BackgroundRead(GfxSurface *surface, int32_t x, int32_t y)
{
    GfxColorRgba8 color;
    int32_t width = (int32_t)GfxSurfaceWidth(surface);
    int32_t height = (int32_t)GfxSurfaceHeight(surface);

    memset(&color, 0, sizeof(color));
    if (x < 0 || y < 0 || x >= width || y >= height) {
        TestFail(__FILE__, __LINE__, "the sample %d,%d is outside the %dx%d surface", (int)x,
                       (int)y, (int)width, (int)height);
        return color;
    }
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &color),
                      RoboOk);
    return color;
}

/** Assert the pixel at (x, y) is @p expected. */
static void BackgroundPixelIs(GfxSurface *surface,
                                int32_t x,
                                int32_t y,
                                GfxColorRgba8 expected,
                                const char *what)
{
    GfxColorRgba8 actual = BackgroundRead(surface, x, y);

    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        TestFail(__FILE__, __LINE__,
                       "%s: expected rgba(%u,%u,%u,%u) at %d,%d, got rgba(%u,%u,%u,%u)", what,
                       expected.r, expected.g, expected.b, expected.a, (int)x, (int)y, actual.r,
                       actual.g, actual.b, actual.a);
    }
}

/** The centre of @p rect, asserted to be a non-empty rectangle. */
static void BackgroundCentre(LayoutRect rect, int32_t *outX, int32_t *outY)
{
    RoboCheck(rect.width > 0 && rect.height > 0);
    *outX = rect.x + rect.width / 2;
    *outY = rect.y + rect.height / 2;
}

/** Assert the pixel at the centre of @p rect is @p expected. */
static void BackgroundCentreIs(GfxSurface *surface,
                                 LayoutRect rect,
                                 GfxColorRgba8 expected,
                                 const char *what)
{
    int32_t x = 0;
    int32_t y = 0;

    BackgroundCentre(rect, &x, &y);
    BackgroundPixelIs(surface, x, y, expected, what);
}

/** Assert every field of two pass records agrees -- all eleven, so a counter added to the
 *  record without a line here is a field two passes could disagree about unnoticed. */
static void BackgroundStatsEq(const PaintDecorationStats *a,
                                const PaintDecorationStats *b,
                                const char *what)
{
    if (a->commands != b->commands || a->draws != b->draws ||
        a->backgrounds != b->backgrounds || a->borders != b->borders ||
        a->filled != b->filled || a->skipped != b->skipped || a->contextual != b->contextual ||
        a->transparent != b->transparent || a->empty != b->empty || a->unshaped != b->unshaped ||
        a->clipped != b->clipped) {
        TestFail(__FILE__, __LINE__,
                       "%s: commands %d/%d draws %d/%d backgrounds %d/%d borders %d/%d "
                       "filled %d/%d skipped %d/%d contextual %d/%d transparent %d/%d "
                       "empty %d/%d unshaped %d/%d clipped %d/%d",
                       what, (int)a->commands, (int)b->commands, (int)a->draws, (int)b->draws,
                       (int)a->backgrounds, (int)b->backgrounds, (int)a->borders, (int)b->borders,
                       (int)a->filled, (int)b->filled, (int)a->skipped, (int)b->skipped,
                       (int)a->contextual, (int)b->contextual, (int)a->transparent,
                       (int)b->transparent, (int)a->empty, (int)b->empty, (int)a->unshaped,
                       (int)b->unshaped, (int)a->clipped, (int)b->clipped);
    }
}

/** Non-zero when @p clip lets a fill reach the point (x, y). */
static int BackgroundClipAdmits(const PaintClip *clip, int32_t x, int32_t y)
{
    if (PaintClipAxisIsActive(&clip->horizontal) &&
        (x < clip->horizontal.start || x >= clip->horizontal.end)) {
        return 0;
    }
    if (PaintClipAxisIsActive(&clip->vertical) &&
        (y < clip->vertical.start || y >= clip->vertical.end)) {
        return 0;
    }
    return 1;
}

/**
 * The colour a pass must have left at (x, y): the **last drawing** command -- a background or
 * a border band -- in list order, whose rectangle contains the point and whose clip admits it.
 *
 * It is the expectation a pixel assertion needs when fills overlap, and it is an *oracle*
 * rather than a second painter: it reads only the command's own rectangle, clip and colour, and
 * does no blending or clipping arithmetic -- so it can check the pass without repeating the
 * logic the pass is being checked for. A point no drawable command covers is transparent, which
 * is what an untouched surface holds.
 *
 * Every **drawing** kind rather than backgrounds alone, because 11.5 made a border band a fill
 * this pass performs: an oracle that ignored bands would expect an ancestor's background under
 * a band that is really drawn over it. The clip is consulted exactly as the pass does -- a
 * command whose boundary hides the point contributes nothing -- which is what makes "the
 * clipped pixel is untouched" an assertion rather than an absence.
 */
static GfxColorRgba8 BackgroundExpectedAt(const PaintDisplayList *list,
                                                     int32_t x,
                                                     int32_t y)
{
    GfxColorRgba8 color = GfxRgba8(0, 0, 0, 0);
    size_t index;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return color;
        }
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        if (!PaintColorIsDrawable(command.color)) {
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            continue;
        }
        if (x < command.rect.x || y < command.rect.y ||
            x >= command.rect.x + command.rect.width ||
            y >= command.rect.y + command.rect.height) {
            continue;
        }
        if (!BackgroundClipAdmits(&command.clip, x, y)) {
            continue;
        }
        color = GfxRgba8(command.color.red, command.color.green, command.color.blue,
                               command.color.alpha);
    }
    return color;
}

/**
 * Assert the surface against the oracle at every **drawing** command's centre.
 *
 * This is the "and the pixels followed the list" claim for a document whose fills overlap: a
 * reader who wants to know which colour should be at a point asks the list, and the pass must
 * agree. Points outside the surface are skipped rather than asserted into it.
 */
static void BackgroundPixelsMatch(const PaintDisplayList *list,
                                    GfxSurface *surface,
                                    const char *what)
{
    size_t index;
    size_t sampled = 0u;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;
        int32_t x;
        int32_t y;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            continue;
        }
        x = command.rect.x + command.rect.width / 2;
        y = command.rect.y + command.rect.height / 2;
        if (x < 0 || y < 0 || x >= (int32_t)GfxSurfaceWidth(surface) ||
            y >= (int32_t)GfxSurfaceHeight(surface)) {
            continue;
        }
        BackgroundPixelIs(surface, x, y, BackgroundExpectedAt(list, x, y), what);
        sampled++;
    }
    RoboCheck(sampled > 0u);
}

/** A clip whose horizontal axis is bounded and whose vertical axis is not. */
static PaintClip BackgroundClipX(int32_t start, int32_t end)
{
    PaintClip clip = PaintClipNone();

    clip.horizontal.mode = RoboCssOverflowHidden;
    clip.horizontal.start = start;
    clip.horizontal.end = end;
    return clip;
}

/** A hand-built `draw-background`: kind, node, rectangle, clip and colour, everything else
 *  cleared. The builder is bypassed on purpose -- these cases are about the painter's
 *  contract, and the list's own validity rule is the door that keeps these legal. */
static PaintDisplayCommand BackgroundMakeDraw(LayoutNodeId node,
                                                         LayoutRect rect,
                                                         PaintClip clip,
                                                         CssColor color)
{
    PaintDisplayCommand command;

    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdDrawBackground;
    command.node = node;
    command.depth = 0u;
    command.rect = rect;
    command.clip = clip;
    command.color = color;
    return command;
}

/** A hand-built structural command: a `begin-node` marker that draws nothing. */
static PaintDisplayCommand BackgroundMakeStructural(LayoutNodeId node)
{
    PaintDisplayCommand command;

    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdBeginNode;
    command.node = node;
    command.depth = 0u;
    command.rect = MakeGfxRect(0, 0, 10, 10);
    return command;
}

/** Append @p command to @p list, asserting it is a record the list accepts. */
static void BackgroundAppend(PaintDisplayList *list,
                              PaintDisplayCommand command)
{
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 1. The colour bridge                                                */
/* ------------------------------------------------------------------ */

static void TestBackgroundColorBridge(void)
{
    CssColor red = CssColorMake(255, 0, 0, 255);
    CssColor translucent = CssColorMake(51, 102, 204, 128);
    CssColor clearColor = CssColorMake(0, 0, 0, 0);
    GfxColorRgba8 out;

    PaintFixture();

    /* A resolved colour draws, and the conversion is a copy: four channels, no arithmetic and
     * no colour space, so nothing can be lost or invented. */
    RoboCheckEqInt(PaintColorIsDrawable(red), 1);
    memset(&out, 0xff, sizeof(out));
    RoboCheckEqInt(PaintColorToRgba8(red, &out), RoboOk);
    RoboCheckEqInt((int)out.r, 255);
    RoboCheckEqInt((int)out.g, 0);
    RoboCheckEqInt((int)out.b, 0);
    RoboCheckEqInt((int)out.a, 255);

    /* Alpha survives rather than being flattened -- the channel a later compositor needs. */
    RoboCheckEqInt(PaintColorIsDrawable(translucent), 1);
    RoboCheckEqInt(PaintColorToRgba8(translucent, &out), RoboOk);
    RoboCheckEqInt((int)out.r, 51);
    RoboCheckEqInt((int)out.g, 102);
    RoboCheckEqInt((int)out.b, 204);
    RoboCheckEqInt((int)out.a, 128);

    /* Fully transparent: a colour, and one that occupies no pixel, so it neither draws nor
     * fails -- the distinction the predicate exists to state. */
    RoboCheckEqInt(PaintColorIsDrawable(clearColor), 0);
    RoboCheckEqInt(PaintColorToRgba8(clearColor, &out), RoboOk);
    RoboCheckEqInt((int)out.a, 0);

    /* The three kinds that are not colours a surface can take, each with its own answer. */
    RoboCheckEqInt(PaintColorIsDrawable(CssColorNone()), 0);
    RoboCheckEqInt(PaintColorToRgba8(CssColorNone(), &out), RoboNotFound);
    RoboCheckEqInt(PaintColorIsDrawable(CssColorCurrent()), 0);
    RoboCheckEqInt(PaintColorToRgba8(CssColorCurrent(), &out),
                      RoboNotSupported);
    RoboCheckEqInt(PaintColorIsDrawable(CssColorInvalid()), 0);
    RoboCheckEqInt(PaintColorToRgba8(CssColorInvalid(), &out),
                      RoboInvalidArgument);

    /* The door's own argument rule. */
    RoboCheckEqInt(PaintColorToRgba8(red, NULL), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* 2. A colour reaches a surface                                       */
/* ------------------------------------------------------------------ */

static void TestBackgroundPixelFill(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand background;
    LayoutNodeId box;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .box { width: 120px; height: 60px; background-color: #3366cc }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    box = PaintBox(&fixture, "box");
    RoboCheckEqInt(BackgroundCommandFor(list, box, 0u, &background), 1);

    surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, surface, &stats);

    /* The document asked for one background and it was filled: the rectangle the display list
     * named is the rectangle the colour landed in. */
    RoboCheckEqInt((int)stats.backgrounds, 1);
    RoboCheckEqInt((int)stats.filled, 1);
    RoboCheckEqInt((int)stats.clipped, 0);
    BackgroundCentreIs(surface, background.rect, GfxRgba8(0x33, 0x66, 0xcc, 255),
                         "the box's background");

    /* And the fill is bounded by the rectangle: one pixel past its right edge is untouched.
     * The read is skipped when that pixel is off the surface rather than asserted into it. */
    if (background.rect.x + background.rect.width < (int32_t)GfxSurfaceWidth(surface) &&
        background.rect.y + background.rect.height / 2 < (int32_t)GfxSurfaceHeight(surface)) {
        BackgroundPixelIs(surface,
                            background.rect.x + background.rect.width,
                            background.rect.y + background.rect.height / 2,
                            GfxRgba8(0, 0, 0, 0), "one pixel past the fill");
    } else {
        BackgroundPixelIs(surface, 0, (int32_t)GfxSurfaceHeight(surface) - 1,
                            GfxRgba8(0, 0, 0, 0), "a pixel no box covers");
    }

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Paint order                                                      */
/* ------------------------------------------------------------------ */

static void TestBackgroundOrdering(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand parent;
    PaintDisplayCommand child;
    LayoutNodeId parentId;
    LayoutNodeId childId;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div></div>"
                         "</body></html>",
                         BACKGROUND_BASE_CSS
                         " .parent { width: 100px; height: 100px; background-color: #ff0000 }"
                         " .child { width: 40px; height: 40px; background-color: #0000ff }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    parentId = PaintBox(&fixture, "parent");
    childId = PaintBox(&fixture, "child");
    RoboCheckEqInt(BackgroundCommandFor(list, parentId, 0u, &parent), 1);
    RoboCheckEqInt(BackgroundCommandFor(list, childId, 0u, &child), 1);

    /* The order is the tree's: the parent's fill comes first, so it cannot cover the child. */
    {
        size_t parentIndex = (size_t)-1;
        size_t childIndex = (size_t)-1;
        size_t index;

        for (index = 0u; index < PaintDisplayListCount(list); index++) {
            PaintDisplayCommand command;

            RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
            if (command.kind != RoboPaintDisplayCmdDrawBackground) {
                continue;
            }
            if (command.node == parentId) {
                parentIndex = index;
            } else if (command.node == childId) {
                childIndex = index;
            }
        }
        RoboCheck(parentIndex != (size_t)-1 && childIndex != (size_t)-1);
        RoboCheck(parentIndex < childIndex);
    }

    surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.filled, 2);

    /* Where the two overlap, the later fill wins -- so the child's corner is blue and the
     * parent's far corner, which the child does not reach, is still red. */
    BackgroundCentreIs(surface, child.rect, BACKGROUND_BLUE, "the child's background");
    BackgroundPixelIs(surface, parent.rect.x + parent.rect.width - 1,
                        parent.rect.y + parent.rect.height - 1, BACKGROUND_RED,
                        "the parent's far corner");
    BackgroundPixelIs(surface, child.rect.x, child.rect.y, BACKGROUND_BLUE,
                        "the child's top-left corner");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Clipping                                                         */
/* ------------------------------------------------------------------ */

/** What one run of the clipping document produced: the spill's own rectangle and the pass
 *  record, so the two runs can be compared rather than guessed at. */
typedef struct backgroundClipProbe {
    LayoutRect spillRect;
    PaintDecorationStats stats;
} BackgroundClipProbe;

/**
 * Lay out the clipping document with @p overflow ("hidden" or "visible") and paint it.
 *
 * The two runs differ in **one declaration** and in nothing else -- same markup, same sheet,
 * same order -- so a difference in the pixels is attributable to the overflow mode and not to a
 * second change smuggled in beside it.
 */
static void BackgroundClipRun(int hidden,
                                BackgroundClipProbe *out,
                                GfxSurface *surface)
{
    static const char *const kMarkup =
        "<html><body><aside class=\"clip\"><figure class=\"spill\"></figure>"
        "</aside></body></html>";
    /* The one declaration the two runs differ in, spelled out rather than built: a reader can
     * see the whole difference between the documents and check that it is one property. */
    static const char *const kHiddenSheet =
        BACKGROUND_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: hidden; background-color: #00ff00 }"
        " .spill { width: 200px; height: 200px; background-color: #ff0000 }";
    static const char *const kVisibleSheet =
        BACKGROUND_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: visible; background-color: #00ff00 }"
        " .spill { width: 200px; height: 200px; background-color: #ff0000 }";
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDisplayCommand spill;
    LayoutNodeId spillId;

    PaintBeginLaidOut(&fixture, kMarkup, hidden ? kHiddenSheet : kVisibleSheet);
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    spillId = PaintBox(&fixture, "spill");
    RoboCheckEqInt(BackgroundCommandFor(list, spillId, 0u, &spill), 1);
    out->spillRect = spill.rect;

    if (hidden) {
        /* The boundary an ancestor's overflow put in force is *on the command*, which is why
         * a rasteriser applies it from the command rather than from a separate operation. */
        RoboCheckEqInt(PaintClipIsActive(&spill.clip), 1);
    } else {
        RoboCheckEqInt(PaintClipIsActive(&spill.clip), 0);
    }

    BackgroundPaint(list, surface, &out->stats);
    RoboCheckEqInt((int)out->stats.filled, 2);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestBackgroundClipping(void)
{
    BackgroundClipProbe clipped;
    BackgroundClipProbe visible;
    GfxSurface *clippedSurface = NULL;
    GfxSurface *visibleSurface = NULL;

    PaintFixture();

    clippedSurface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    visibleSurface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundClipRun(1, &clipped, clippedSurface);
    BackgroundClipRun(0, &visible, visibleSurface);

    /* The same boxes, one property apart: the spill's own rectangle is 200x200 in both. */
    RoboCheckEqInt((int)clipped.spillRect.width, 200);
    RoboCheckEqInt((int)clipped.spillRect.height, 200);
    RoboCheckEqInt((int)clipped.spillRect.x, (int)visible.spillRect.x);
    RoboCheckEqInt((int)clipped.spillRect.y, (int)visible.spillRect.y);

    /* Only the hidden run bounds a fill, and it says so in the record. */
    RoboCheckEqInt((int)clipped.stats.clipped, 1);
    RoboCheckEqInt((int)visible.stats.clipped, 0);

    /* Inside the ancestor, both runs paint the spill's red over the ancestor's green -- the
     * later fill wins. */
    BackgroundPixelIs(clippedSurface, clipped.spillRect.x + 5, clipped.spillRect.y + 5,
                        BACKGROUND_RED, "inside the clip, hidden");
    BackgroundPixelIs(visibleSurface, visible.spillRect.x + 5, visible.spillRect.y + 5,
                        BACKGROUND_RED, "inside the clip, visible");

    /* Far outside the ancestor but well inside the spill's own rectangle: the visible run
     * fills it, and the hidden run leaves it untouched. That difference **is** the clip. */
    BackgroundPixelIs(visibleSurface, visible.spillRect.x + 150, visible.spillRect.y + 150,
                        BACKGROUND_RED, "the spill outside its ancestor, visible overflow");
    BackgroundPixelIs(clippedSurface, clipped.spillRect.x + 150, clipped.spillRect.y + 150,
                        GfxRgba8(0, 0, 0, 0),
                        "the spill outside its ancestor, hidden overflow");

    /* The mechanism, without a document to infer it from: a hand-built command whose clip
     * bounds one axis. The axis the clip leaves open is the surface's own interval, which is
     * what makes "bounded horizontally, free vertically" expressible at all. */
    {
        PaintDisplayList *list = NULL;
        PaintDecorationStats stats;
        /* A surface of its own: the document's fills are on the two above, and an assertion
         * about "untouched" needs somewhere nothing has been painted yet. */
        GfxSurface *plain = BackgroundSurface(32u, 32u, RoboGfxSurfaceAccessReadWrite);

        RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
        /* A rectangle taller than the surface, so that "the open axis was not bounded" is a
         * pixel that can be read rather than a claim about an interval nobody can see. */
        BackgroundAppend(list,
                          BackgroundMakeDraw((LayoutNodeId)1u, MakeGfxRect(0, 0, 10, 32),
                                               BackgroundClipX(0, 5),
                                               CssColorMake(0, 0, 255, 255)));
        BackgroundPaint(list, plain, &stats);
        RoboCheckEqInt((int)stats.filled, 1);
        RoboCheckEqInt((int)stats.clipped, 1);
        BackgroundPixelIs(plain, 0, 0, BACKGROUND_BLUE, "inside the bounded axis");
        BackgroundPixelIs(plain, 4, 31, BACKGROUND_BLUE,
                            "the interval's last column, at the bottom of the surface");
        BackgroundPixelIs(plain, 5, 31, GfxRgba8(0, 0, 0, 0),
                            "one pixel past the interval");
        BackgroundPixelIs(plain, 9, 9, GfxRgba8(0, 0, 0, 0),
                            "the far side of the rectangle");
        PaintDisplayListDestroy(list);
        GfxSurfaceRelease(plain);
    }

    GfxSurfaceRelease(clippedSurface);
    GfxSurfaceRelease(visibleSurface);
}

/* ------------------------------------------------------------------ */
/* 5. Nothing to paint                                                 */
/* ------------------------------------------------------------------ */

static void TestBackgroundNothingToPaint(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand content;
    LayoutNodeId box;

    /* An undeclared background is `transparent` (its initial value) and a declared
     * `transparent` is the same colour, so neither emits a background command at all -- the
     * list's own length says so, before the painter is asked anything. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"plain\"></div>"
                         "<div class=\"clear\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .plain { width: 50px; height: 50px }"
                         " .clear { width: 50px; height: 50px; background-color: transparent }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    RoboCheckEqInt((int)listStats.backgrounds, 0);
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawBackground),
                      0);

    surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, surface, &stats);

    /* A legitimate pass that fills nothing, and every box visited anyway: the structural
     * records are still skipped and counted, which is what makes "the document has no
     * backgrounds" visible rather than silent. */
    RoboCheckEqInt((int)stats.commands, (int)PaintDisplayListCount(list));
    RoboCheckEqInt((int)stats.backgrounds, 0);
    RoboCheckEqInt((int)stats.filled, 0);
    RoboCheckEqInt((int)stats.skipped, (int)stats.commands);
    RoboCheck((int)stats.skipped > 0);

    /* Where the boxes are, the surface is still the transparent it started as. */
    box = PaintBox(&fixture, "clear");
    RoboCheckEqInt(BackgroundCommandOfKindFor(list, RoboPaintDisplayCmdContent,
                                                     box, 0u, &content),
                      1);
    BackgroundCentreIs(surface, content.rect, GfxRgba8(0, 0, 0, 0),
                         "a box with no background");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/**
 * A document whose only decoration is a **border**: four bands, no background, and the pass
 * fills all four.
 *
 * This is the case that pins where the fence moved at 11.5. It used to state the opposite --
 * "a border band is resolved and not painted" -- because 11.4's painter consumed exactly one
 * command kind. The pass now consumes every **drawing** kind, so the same document that
 * produced a full display list and an untouched surface produces a full display list and four
 * coloured bands. What has *not* moved is the fence itself, and the two pixels below are where
 * that is visible: the band is coloured, and the box's own middle -- which no command covers,
 * because a border with no `background-color` draws nothing there -- still holds what the
 * surface started with.
 */
static void TestBackgroundBorderOnlyDocument(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand band;
    PaintDisplayCommand content;
    LayoutNodeId framed;
    int32_t x = 0;
    int32_t y = 0;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"framed\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .framed { width: 80px; height: 40px; border: 4px solid #00ff00 }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    RoboCheckEqInt((int)listStats.borders, 4);
    RoboCheckEqInt((int)listStats.backgrounds, 0);
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawBackground),
                      0);

    surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, surface, &stats);

    /* Four drawings, four fills, no background and no pattern: `solid` is the one style whose
     * band *is* its shape, so nothing here is a simplification being counted. */
    RoboCheckEqInt((int)stats.draws, 4);
    RoboCheckEqInt((int)stats.borders, 4);
    RoboCheckEqInt((int)stats.backgrounds, 0);
    RoboCheckEqInt((int)stats.filled, 4);
    RoboCheckEqInt((int)stats.unshaped, 0);
    RoboCheckEqInt((int)stats.empty, 0);
    RoboCheckEqInt((int)stats.commands, (int)(stats.draws + stats.skipped));
    /* The document is three placed boxes -- `<html>`, `<body>` and the one `<div>` -- and each
     * contributes its three structural commands (`begin-node`, `content`, `end-node`), which draw
     * nothing by definition. So `skipped` is nine here, and every drawing is a band. */
    RoboCheckEqInt((int)stats.skipped, 9);
    RoboCheckEqInt((int)stats.skipped, (int)(stats.commands - stats.draws));

    /* The first band is the top side: the full width of the border box, four pixels tall. */
    RoboCheckEqInt(BackgroundNthOfKind(list, RoboPaintDisplayCmdDrawBorder, 0u, &band),
                      1);
    RoboCheckEqInt((int)band.rect.height, 4);
    RoboCheckEqInt((int)band.style, (int)RoboCssBorderStyleSolid);
    BackgroundCentre(band.rect, &x, &y);
    BackgroundPixelIs(surface, x, y, GfxRgba8(0x00, 0xff, 0x00, 255),
                        "the middle of the box's top band");

    /* And the box's own middle, which no command draws into: a border is a ring, so what it
     * encloses is the surface the caller handed over. */
    framed = PaintBox(&fixture, "framed");
    RoboCheckEqInt(BackgroundCommandOfKindFor(list, RoboPaintDisplayCmdContent,
                                                     framed, 0u, &content),
                      1);
    BackgroundCentre(content.rect, &x, &y);
    BackgroundPixelIs(surface, x, y, GfxRgba8(0, 0, 0, 0),
                        "inside the border ring, where no command drew");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestBackgroundZeroArea(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand background;
    LayoutNodeId box;

    /* A declared, opaque background on a box with no height: the *record* is legitimate ("I
     * would fill this rectangle") and the *fill* is not ("there is no rectangle"), and the two
     * counters are what keep those apart. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"zero\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .zero { width: 30px; height: 0px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    box = PaintBox(&fixture, "zero");
    RoboCheckEqInt(BackgroundCommandFor(list, box, 0u, &background), 1);
    RoboCheckEqInt((int)background.rect.height, 0);

    surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, surface, &stats);

    RoboCheckEqInt((int)stats.backgrounds, 1);
    RoboCheckEqInt((int)stats.empty, 1);
    RoboCheckEqInt((int)stats.filled, 0);
    RoboCheckEqInt((int)stats.clipped, 0);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Positioned rectangles                                            */
/* ------------------------------------------------------------------ */

static void TestBackgroundPositions(void)
{
    PaintDisplayListStats listStats;

    PaintFixture();

    /* Two documents, one declaration apart. The first is the control; the second moves the box
     * with a relative offset, and the two are compared rather than a coordinate being guessed,
     * so the assertion is about the offset and not about this build's default margin. */
    {
        PaintFixtureDoc fixture;
        PaintResult result;
        PaintDisplayList *list = NULL;
        PaintDisplayCommand background;
        PaintDecorationStats stats;
        GfxSurface *surface = NULL;
        LayoutNodeId box;
        LayoutRect base;

        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"moved\"></div></body></html>",
                             BACKGROUND_BASE_CSS
                             " .moved { width: 50px; height: 20px; background-color: #112233 }");
        PaintOnce(&fixture, &result);
        BackgroundBuild(&result, &list, &listStats);
        box = PaintBox(&fixture, "moved");
        RoboCheckEqInt(BackgroundCommandFor(list, box, 0u, &background), 1);
        base = background.rect;

        surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
        BackgroundPaint(list, surface, &stats);
        RoboCheckEqInt((int)stats.filled, 1);
        BackgroundCentreIs(surface, base, GfxRgba8(0x11, 0x22, 0x33, 255),
                             "the unmoved box's background");
        /* The probe the moved document is checked against: inside this rectangle here. */
        BackgroundPixelIs(surface, base.x + 2, base.y + 2, GfxRgba8(0x11, 0x22, 0x33, 255),
                            "the unmoved box's top-left corner");

        GfxSurfaceRelease(surface);
        PaintDisplayListDestroy(list);
        PaintResultDispose(&result);
        PaintEnd(&fixture);

        /* The same box, relatively positioned: the final geometry carries the offset, so the
         * rectangle the display list resolves is the *moved* one. */
        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"moved\"></div></body></html>",
                             BACKGROUND_BASE_CSS
                             " .moved { width: 50px; height: 20px; background-color: #112233;"
                             "   position: relative; left: 20px; top: 10px }");
        PaintOnce(&fixture, &result);
        BackgroundBuild(&result, &list, &listStats);
        box = PaintBox(&fixture, "moved");
        RoboCheckEqInt(BackgroundCommandFor(list, box, 0u, &background), 1);

        RoboCheckEqInt((int)background.rect.x, (int)base.x + 20);
        RoboCheckEqInt((int)background.rect.y, (int)base.y + 10);
        /* The size is the size: an offset moves a box, it does not grow one. */
        RoboCheckEqInt((int)background.rect.width, (int)base.width);
        RoboCheckEqInt((int)background.rect.height, (int)base.height);

        surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
        BackgroundPaint(list, surface, &stats);
        RoboCheckEqInt((int)stats.filled, 1);

        /* The fill followed the box, and the place the box would have been is untouched --
         * read at the control's own top-left corner, which the 20px offset moved the fill
         * away from. */
        BackgroundCentreIs(surface, background.rect, GfxRgba8(0x11, 0x22, 0x33, 255),
                             "the moved box's background");
        BackgroundPixelIs(surface, base.x + 2, base.y + 2, GfxRgba8(0, 0, 0, 0),
                            "where the box would have been, unmoved");

        GfxSurfaceRelease(surface);
        PaintDisplayListDestroy(list);
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 7. The counters                                                     */
/* ------------------------------------------------------------------ */

static void TestBackgroundCounters(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    size_t index;
    size_t opaqueCentres = 0u;

    /* A document that uses the whole vocabulary at once: backgrounds, a border, a nested child
     * and a box with nothing declared. */
    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<div class=\"plain\"></div>"
                         "</body></html>",
                         BACKGROUND_BASE_CSS
                         " .card { width: 120px; height: 60px; padding: 6px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }"
                         " .plain { width: 20px; height: 20px; border: 1px solid #ff0000 }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, surface, &stats);

    /* Every command was visited, and each was one of the two things the pass can do with it --
     * which is the triple of identities the record's own header states, asserted here so that
     * `skipped` is a statement rather than a remainder nobody checked:
     *
     *   commands = draws + skipped
     *   draws    = backgrounds + borders
     *   draws    = filled + contextual + transparent + empty
     */
    RoboCheckEqInt((int)stats.commands, (int)PaintDisplayListCount(list));
    RoboCheckEqInt((int)stats.commands, (int)(stats.draws + stats.skipped));
    RoboCheckEqInt((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    RoboCheckEqInt((int)stats.backgrounds,
                      (int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawBackground));
    RoboCheckEqInt((int)stats.borders,
                      (int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawBorder));
    RoboCheckEqInt((int)stats.filled + (int)stats.contextual + (int)stats.transparent +
                          (int)stats.empty,
                      (int)stats.draws);
    RoboCheckEqInt((int)stats.backgrounds, 2);
    /* The card's four sides at 2px and the plain box's four at 1px: eight bands, all of them
     * drawings, which is what 11.5 added to this pass. */
    RoboCheckEqInt((int)stats.borders, 8);
    RoboCheckEqInt((int)stats.filled, 10);
    RoboCheckEqInt((int)stats.contextual, 0);
    RoboCheckEqInt((int)stats.transparent, 0);
    RoboCheckEqInt((int)stats.empty, 0);
    /* Every border here is `solid`, so nothing is being drawn as a simplification. */
    RoboCheckEqInt((int)stats.unshaped, 0);
    RoboCheckEqInt((int)stats.clipped, 0);
    RoboCheck((int)listStats.borders == 8);
    RoboCheck((int)stats.skipped > 0);

    /* And each of those two fills reached the surface -- asserted against the list's own
     * record of who writes where, rather than by a total that could be right for the wrong
     * reason or a centre that a later fill legitimately covers. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (command.kind != RoboPaintDisplayCmdDrawBackground) {
            continue;
        }
        RoboCheckEqInt(PaintClipIsActive(&command.clip), 0);
        opaqueCentres++;
    }
    RoboCheckEqInt((int)opaqueCentres, 2);
    BackgroundPixelsMatch(list, surface, "a background command's own colour");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. The painter's own contract                                       */
/* ------------------------------------------------------------------ */

static void TestBackgroundHandBuiltKinds(void)
{
    PaintDisplayList *list = NULL;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;

    PaintFixture();

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    surface = BackgroundSurface(32u, 32u, RoboGfxSurfaceAccessReadWrite);

    /* A structural command -- which draws nothing -- and then a border band, which is the
     * other **drawing** kind this pass fills: it needs its style, because that is the fact a
     * band's band-plus-colour cannot state (11.5). */
    BackgroundAppend(list, BackgroundMakeStructural((LayoutNodeId)1u));
    {
        PaintDisplayCommand band;

        PaintDisplayCommandInit(&band);
        band.kind = RoboPaintDisplayCmdDrawBorder;
        band.node = (LayoutNodeId)1u;
        band.rect = MakeGfxRect(0, 0, 8, 2);
        band.color = CssColorMake(0, 255, 0, 255);
        band.style = RoboCssBorderStyleSolid;
        BackgroundAppend(list, band);
    }

    /* The three ways a background draws nothing, then one that does. */
    BackgroundAppend(list, BackgroundMakeDraw((LayoutNodeId)1u, MakeGfxRect(0, 0, 8, 8),
                                                 PaintClipNone(), CssColorCurrent()));
    BackgroundAppend(list, BackgroundMakeDraw((LayoutNodeId)1u, MakeGfxRect(0, 0, 8, 8),
                                                 PaintClipNone(),
                                                 CssColorMake(0, 0, 0, 0)));
    BackgroundAppend(list, BackgroundMakeDraw((LayoutNodeId)1u, MakeGfxRect(4, 4, 0, 0),
                                                 PaintClipNone(),
                                                 CssColorMake(255, 0, 0, 255)));
    BackgroundAppend(list, BackgroundMakeDraw((LayoutNodeId)1u, MakeGfxRect(2, 2, 4, 4),
                                                 PaintClipNone(),
                                                 CssColorMake(0, 0, 255, 255)));

    BackgroundPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.commands, 6);
    RoboCheckEqInt((int)stats.draws, 5);
    RoboCheckEqInt((int)stats.backgrounds, 4);
    RoboCheckEqInt((int)stats.borders, 1);
    RoboCheckEqInt((int)stats.skipped, 1);
    RoboCheckEqInt((int)stats.contextual, 1);
    RoboCheckEqInt((int)stats.transparent, 1);
    RoboCheckEqInt((int)stats.empty, 1);
    RoboCheckEqInt((int)stats.filled, 2);
    RoboCheckEqInt((int)stats.unshaped, 0);
    RoboCheckEqInt((int)stats.clipped, 0);

    /* The band was filled where the command put it, and the drawable background where its own
     * rectangle is -- a photo of the two kinds in list order. */
    BackgroundPixelIs(surface, 3, 0, GfxRgba8(0, 255, 0, 255),
                        "the hand-built border band");
    BackgroundPixelIs(surface, 3, 3, BACKGROUND_BLUE, "the one drawable background");
    BackgroundPixelIs(surface, 9, 9, GfxRgba8(0, 0, 0, 0), "past every background");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
}

/* ------------------------------------------------------------------ */
/* 9. Determinism                                                      */
/* ------------------------------------------------------------------ */

static void TestBackgroundDeterminism(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats first;
    PaintDecorationStats second;
    PaintDecorationStats again;
    GfxSurface *one = NULL;
    GfxSurface *two = NULL;
    size_t index;

    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "</body></html>",
                         BACKGROUND_BASE_CSS
                         " .card { width: 120px; height: 60px; padding: 6px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    one = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    two = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, one, &first);
    BackgroundPaint(list, two, &second);
    BackgroundStatsEq(&first, &second, "two passes over one list");

    /* Every fill landed in the same place, pixel for pixel -- the sample is each background
     * command's own rectangle, so the comparison follows the document rather than a fixed
     * handful of coordinates. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;
        GfxColorRgba8 left;
        GfxColorRgba8 right;
        int32_t x = 0;
        int32_t y = 0;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (command.kind != RoboPaintDisplayCmdDrawBackground) {
            continue;
        }
        BackgroundCentre(command.rect, &x, &y);
        left = BackgroundRead(one, x, y);
        right = BackgroundRead(two, x, y);
        RoboCheckEqInt((int)left.r, (int)right.r);
        RoboCheckEqInt((int)left.g, (int)right.g);
        RoboCheckEqInt((int)left.b, (int)right.b);
        RoboCheckEqInt((int)left.a, (int)right.a);
    }

    /* And a second pass over the *same* surface leaves it as it was: an opaque fill is
     * idempotent, which is what makes a rebuilt display list safe to repaint in place. */
    BackgroundPaint(list, one, &again);
    BackgroundStatsEq(&first, &again, "a repeat pass over one surface");
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (command.kind != RoboPaintDisplayCmdDrawBackground) {
            continue;
        }
        BackgroundCentreIs(one, command.rect,
                             GfxRgba8(command.color.red, command.color.green,
                                            command.color.blue, command.color.alpha),
                             "a fill after the surface was painted twice");
    }

    GfxSurfaceRelease(one);
    GfxSurfaceRelease(two);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 10. Snapshot isolation                                              */
/* ------------------------------------------------------------------ */

static void TestBackgroundMutationIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *before = NULL;
    PaintDisplayList *after = NULL;
    PaintDisplayListStats beforeStats;
    PaintDisplayListStats afterStats;
    PaintDecorationStats first;
    PaintDecorationStats second;
    GfxSurface *one = NULL;
    GfxSurface *two = NULL;
    size_t index;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BACKGROUND_BASE_CSS
                         " .box { height: 40px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &before, &beforeStats);

    one = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(before, one, &first);

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and lay the
     * *same* tree out at a different viewport, which changes every box's geometry. A display
     * list is a snapshot, so the one already built paints exactly as it did -- and the mutation
     * is *proved* observable by building a second display list afterwards and asserting it
     * differs.
     */
    PaintListClear(result.list);
    RoboCheckEqInt((int)PaintListCount(result.list), 0);

    PaintLayout(&fixture, 400, 600);
    {
        PaintContext *context = NULL;

        RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
        PaintRun(&fixture, context, result.list);
        PaintContextDestroy(context);
    }
    BackgroundBuild(&result, &after, &afterStats);
    RoboCheckEqInt(PaintDisplayListEqual(before, after), 0);

    two = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(before, two, &second);
    BackgroundStatsEq(&first, &second, "a paint after the sources changed");

    for (index = 0u; index < PaintDisplayListCount(before); index++) {
        PaintDisplayCommand command;
        GfxColorRgba8 left;
        GfxColorRgba8 right;
        int32_t x = 0;
        int32_t y = 0;

        RoboCheckEqInt(PaintDisplayListAt(before, index, &command), RoboOk);
        if (command.kind != RoboPaintDisplayCmdDrawBackground) {
            continue;
        }
        BackgroundCentre(command.rect, &x, &y);
        left = BackgroundRead(one, x, y);
        right = BackgroundRead(two, x, y);
        RoboCheckEqInt((int)left.r, (int)right.r);
        RoboCheckEqInt((int)left.g, (int)right.g);
        RoboCheckEqInt((int)left.b, (int)right.b);
        RoboCheckEqInt((int)left.a, (int)right.a);
    }

    GfxSurfaceRelease(one);
    GfxSurfaceRelease(two);
    PaintDisplayListDestroy(before);
    PaintDisplayListDestroy(after);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 11. Empty, degenerate and refusing cases                            */
/* ------------------------------------------------------------------ */

static void TestBackgroundEmptyCases(void)
{
    PaintDisplayList *list = NULL;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    GfxSurface *readonly = NULL;
    PaintDiag diag;

    PaintFixture();

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    surface = BackgroundSurface(16u, 16u, RoboGfxSurfaceAccessReadWrite);

    /* An empty list is a legitimate pass: nothing to do, and every counter nought. */
    memset(&stats, 0xff, sizeof(stats));
    PaintDecorationStatsInit(NULL); /* safe */
    BackgroundPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.commands, 0);
    RoboCheckEqInt((int)stats.backgrounds, 0);
    RoboCheckEqInt((int)stats.filled, 0);
    RoboCheckEqInt((int)stats.skipped, 0);

    /* A list of structural commands alone: visited, skipped, and nothing drawn. */
    BackgroundAppend(list, BackgroundMakeStructural((LayoutNodeId)1u));
    BackgroundAppend(list, BackgroundMakeStructural((LayoutNodeId)1u));
    BackgroundPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.commands, 2);
    RoboCheckEqInt((int)stats.backgrounds, 0);
    RoboCheckEqInt((int)stats.skipped, 2);
    BackgroundPixelIs(surface, 8, 8, GfxRgba8(0, 0, 0, 0),
                        "a surface no background touched");

    /* The arguments are required, and a refusal says which. */
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintDecorationPaint(NULL, surface, NULL, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheck(diag.message != NULL);
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintDecorationPaint(list, NULL, NULL, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheck(diag.message != NULL);

    /* And a target that cannot take a write is refused with the code the header names, before
     * any fill is attempted -- the graphics layer's own rule, mapped once. */
    readonly = BackgroundSurface(16u, 16u, RoboGfxSurfaceAccessRead);
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintDecorationPaint(list, readonly, NULL, NULL, &diag),
                      RoboNotSupported);
    RoboCheck(diag.message != NULL);
    GfxSurfaceRelease(readonly);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
}

/* ------------------------------------------------------------------ */
/* 12. End to end                                                      */
/* ------------------------------------------------------------------ */

static void TestBackgroundEndToEnd(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    char dump[8192];
    size_t dumpLength = 0u;
    size_t index;
    size_t clippedCommands = 0u;

    /*
     * The whole pipeline in one case: HTML → CSS → layout → paint records → display list →
     * surfaces. The document exercises the box model (width/height/margin/padding/border), a
     * nested child, a relative offset and both overflow modes, so the walk it produces has
     * every shape a consumer will meet.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"outer\">"
                         "  <div class=\"inner\"></div>"
                         "</div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         BACKGROUND_BASE_CSS
                         " .outer { width: 200px; height: 80px; padding: 8px; margin: 4px;"
                         "   border: 2px solid #000000; background-color: #eeeeee }"
                         " .inner { width: 100px; height: 40px; margin: 6px;"
                         "   background-color: #00ff00; position: relative; left: 5px }"
                         " .clip { width: 60px; height: 30px; overflow: hidden;"
                         "   background-color: #00ff00 }"
                         " .spill { width: 200px; height: 200px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    BackgroundBuild(&result, &list, &listStats);

    surface = BackgroundSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BackgroundPaint(list, surface, &stats);

    /* The pass's own identities hold for a real document, not just a hand-built one. */
    RoboCheckEqInt((int)stats.commands, (int)PaintDisplayListCount(list));
    RoboCheckEqInt((int)stats.commands, (int)(stats.draws + stats.skipped));
    RoboCheckEqInt((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    RoboCheckEqInt((int)stats.backgrounds,
                      (int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawBackground));
    RoboCheckEqInt((int)stats.borders,
                      (int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawBorder));
    RoboCheckEqInt((int)stats.filled + (int)stats.contextual + (int)stats.transparent +
                          (int)stats.empty,
                      (int)stats.draws);
    /* Four backgrounds, the outer box's four bands, and every one of them opaque and of a
     * non-empty rectangle -- so every drawing reaches the surface. */
    RoboCheck((int)stats.backgrounds >= 2);
    RoboCheck((int)stats.borders >= 4);
    RoboCheckEqInt((int)stats.filled, (int)stats.draws);
    RoboCheck((int)listStats.borders >= 4);

    /* Every command is a record, and the ones under an ancestor's boundary are the ones the
     * pass counted as clipped -- a drawing of either kind, because a band under a hidden
     * ancestor is bounded exactly as a background is. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
        if (PaintDisplayCommandKindDraws(command.kind) &&
            PaintClipIsActive(&command.clip)) {
            clippedCommands++;
        }
    }
    RoboCheckEqInt((int)clippedCommands, (int)stats.clipped);
    RoboCheck((int)stats.clipped >= 1);

    /* The pixels follow the list -- including where a descendant's fill covers an ancestor's,
     * and where the clip correctly stops one short of the descendant's own rectangle. */
    BackgroundPixelsMatch(list, surface, "a background in a mixed document");

    /* The developer's door still reads the input the painter consumed. */
    memset(dump, 0, sizeof(dump));
    RoboCheckEqInt(PaintDisplayListWrite(list, dump, sizeof(dump), &dumpLength, NULL),
                      RoboOk);
    RoboCheck(dumpLength > 0u);
    RoboCheck(strstr(dump, "draw-background") != NULL);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintBackground(void)
{
    TestRegister("paint.background.color_bridge", TestBackgroundColorBridge);
    TestRegister("paint.background.pixel_fill", TestBackgroundPixelFill);
    TestRegister("paint.background.ordering", TestBackgroundOrdering);
    TestRegister("paint.background.clipping", TestBackgroundClipping);
    TestRegister("paint.background.nothing_to_paint", TestBackgroundNothingToPaint);
    TestRegister("paint.background.border_only_document",
                       TestBackgroundBorderOnlyDocument);
    TestRegister("paint.background.zero_area", TestBackgroundZeroArea);
    TestRegister("paint.background.positions", TestBackgroundPositions);
    TestRegister("paint.background.counters", TestBackgroundCounters);
    TestRegister("paint.background.hand_built_kinds", TestBackgroundHandBuiltKinds);
    TestRegister("paint.background.determinism", TestBackgroundDeterminism);
    TestRegister("paint.background.mutation_isolation",
                       TestBackgroundMutationIsolation);
    TestRegister("paint.background.empty_cases", TestBackgroundEmptyCases);
    TestRegister("paint.background.end_to_end", TestBackgroundEndToEnd);
}
