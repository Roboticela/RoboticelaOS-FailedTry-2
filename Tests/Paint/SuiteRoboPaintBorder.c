/*
 * RoboPaint border suite (Phase 11.5) — paint.border.*
 *
 * The second drawing this module does, and the first one with a **shape**. The claims the
 * release makes, each pinned where it can be observed rather than argued:
 *
 *   **the style travelled**   a border side's `border-style` reaches the display command, and
 *                             the case asserts it end to end -- the record's own accessor, the
 *                             command's field -- because the fact that a `dashed` band is not a
 *                             `solid` one is not derivable from a band's rectangle, so dropping
 *                             it one stage early would be a silent loss of the only thing that
 *                             makes a band render-ready;
 *
 *   **the band's geometry**   the four sides of a box are **bands**: the region of the border
 *                             box outside the padding box on that edge. Each case computes its
 *                             expectation from the box's own border box and its declared
 *                             insets rather than from absolutes, so the assertion is about the
 *                             derivation and not about this build's default margin;
 *
 *   **pixels**                every band is filled where the display list said it is, in the
 *                             side's own colour, asserted by reading the surface back -- and
 *                             the corner belongs to the side drawn **last**, which is the one
 *                             place the four-band order is observable as a colour;
 *
 *   **one pass, one order**   the decisive case is a relatively positioned child that spills
 *                             over its parent's border: the child's background is filled
 *                             **after** the parent's bands, because the list's order is the
 *                             traversal's and CSS paints a descendant's background over its
 *                             ancestor's border. Two passes would fill every background in the
 *                             document before every border and get exactly this wrong;
 *
 *   **pattern styles**        `dashed`, `dotted`, `double`, `groove`, `ridge`, `inset` and
 *                             `outset` are drawn as their **band**, in the side's colour, and
 *                             counted (`unshaped`) rather than silently reported as shaped --
 *                             because a border drawn as a solid band when the author asked for
 *                             dots is the kind of quiet wrongness this module counts instead of
 *                             shipping;
 *
 *   **snapshot**              once a display list exists, clearing the record list and laying
 *                             the tree out again does not change a single pixel it paints.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and then paints it into a real RoboGraphics surface. Hand-built *commands*
 * appear only where the question is about the painter's own contract -- the style rule, the
 * counters, the clip application -- and never in place of a layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboCSS/RoboCSSBox.h"
#include "RoboGraphics/RoboGraphicsBitmap.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block containers (so a
 * fixture's boxes stack in flow order), and `<head>` stays the one box nothing styles. */
#define BORDER_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* The near-black a band usually is, and a colour a pixel assertion can name. */
#define BORDER_BLACK GfxRgba8(0x11, 0x11, 0x11, 255)
#define BORDER_RED GfxRgba8(0xff, 0x00, 0x00, 255)
#define BORDER_GREEN GfxRgba8(0x00, 0xff, 0x00, 255)

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/**
 * Compose a stylesheet from @p declaration in @p buffer.
 *
 * The clip and absent-side cases differ in **one declaration** and in nothing else, so the
 * document is built from a part that never changes and a part that is the whole difference --
 * a reader can see exactly what varies. Hand-built rather than with `stdio`, because the
 * runtime's guest target has none and these suites compile for it.
 */
static const char *BorderSheet(char *buffer,
                                size_t capacity,
                                const char *declaration)
{
    const char *parts[3];
    size_t length = 0u;
    size_t part;

    parts[0] = BORDER_BASE_CSS " .box { width: 60px; height: 30px; ";
    parts[1] = declaration;
    parts[2] = " }";

    buffer[0] = '\0';
    for (part = 0u; part < 3u; part++) {
        const char *cursor = parts[part];

        while (*cursor != '\0' && length + 1u < capacity) {
            buffer[length++] = *cursor++;
            buffer[length] = '\0';
        }
    }
    return buffer;
}

/** A CPU surface for a decoration pass: owned, RGBA8, zero-filled (so "untouched" is
 *  transparent and an unpainted pixel is distinguishable from a black one). */
static GfxSurface *BorderSurface(uint32_t width,
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
static void BorderBuild(const PaintResult *records,
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
 * The pass is *not* asserted to fill anything -- several of the cases below are about a document
 * that draws nothing -- but a failure is always a bug in a case, so the diagnostic is reported.
 */
static void BorderPaint(const PaintDisplayList *list,
                         GfxSurface *target,
                         PaintDecorationStats *outStats)
{
    PaintDiag diag;
    Result status;

    PaintDiagReset(&diag);
    status = PaintDecorationPaint(list, target, NULL, outStats, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the decoration pass refused the list: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The @p nth command of kind @p kind whose node is @p node, or 0 when there is none. */
static int BorderCommandOfKindFor(const PaintDisplayList *list,
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

/** The index of the first command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t BorderIndexOfKindFor(const PaintDisplayList *list,
                                       PaintDisplayCommandKind kind,
                                       LayoutNodeId node)
{
    size_t index;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return (size_t)-1;
        }
        if (command.kind == kind && command.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/** The index of the **last** command of @p kind whose node is @p node, or `(size_t)-1`. */
static size_t BorderLastIndexOfKindFor(const PaintDisplayList *list,
                                            PaintDisplayCommandKind kind,
                                            LayoutNodeId node)
{
    size_t index;
    size_t found = (size_t)-1;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return (size_t)-1;
        }
        if (command.kind == kind && command.node == node) {
            found = index;
        }
    }
    return found;
}

/** Read the pixel at (x, y), failing when the point is outside the surface. */
static GfxColorRgba8 BorderRead(GfxSurface *surface, int32_t x, int32_t y)
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
static void BorderPixelIs(GfxSurface *surface,
                            int32_t x,
                            int32_t y,
                            GfxColorRgba8 expected,
                            const char *what)
{
    GfxColorRgba8 actual = BorderRead(surface, x, y);

    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        TestFail(__FILE__, __LINE__,
                       "%s: expected rgba(%u,%u,%u,%u) at %d,%d, got rgba(%u,%u,%u,%u)", what,
                       expected.r, expected.g, expected.b, expected.a, (int)x, (int)y, actual.r,
                       actual.g, actual.b, actual.a);
    }
}

/** Assert the pixel at (x, y) holds @p r, @p g, @p b, @p a -- the same door, spelled for a
 *  colour no macro names. */
static void BorderPixelIsRgba(GfxSurface *surface,
                                 int32_t x,
                                 int32_t y,
                                 uint8_t r,
                                 uint8_t g,
                                 uint8_t b,
                                 uint8_t a,
                                 const char *what)
{
    BorderPixelIs(surface, x, y, GfxRgba8(r, g, b, a), what);
}

/** The centre of @p rect, asserted to be a non-empty rectangle. */
static void BorderCentre(LayoutRect rect, int32_t *outX, int32_t *outY)
{
    RoboCheck(rect.width > 0 && rect.height > 0);
    *outX = rect.x + rect.width / 2;
    *outY = rect.y + rect.height / 2;
}

/** Assert the pixel at the centre of @p rect is @p expected. */
static void BorderCentreIs(GfxSurface *surface,
                             LayoutRect rect,
                             GfxColorRgba8 expected,
                             const char *what)
{
    int32_t x = 0;
    int32_t y = 0;

    BorderCentre(rect, &x, &y);
    BorderPixelIs(surface, x, y, expected, what);
}

/** Assert every field of two pass records agrees. */
static void BorderStatsEq(const PaintDecorationStats *a,
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

/** A clip whose horizontal axis is bounded and whose vertical axis is not. */
static PaintClip BorderClipX(int32_t start, int32_t end)
{
    PaintClip clip = PaintClipNone();

    clip.horizontal.mode = RoboCssOverflowHidden;
    clip.horizontal.start = start;
    clip.horizontal.end = end;
    return clip;
}

/** A hand-built `draw-border`: kind, node, band, clip, colour **and style**, everything else
 *  cleared. The builder is bypassed on purpose -- these cases are about the painter's own
 *  contract, and the list's validity rule is the door that keeps them legal. */
static PaintDisplayCommand BorderMakeBand(LayoutNodeId node,
                                                     LayoutRect rect,
                                                     PaintClip clip,
                                                     CssColor color,
                                                     CssBorderStyle style)
{
    PaintDisplayCommand command;

    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdDrawBorder;
    command.node = node;
    command.depth = 0u;
    command.rect = rect;
    command.clip = clip;
    command.color = color;
    command.style = style;
    return command;
}

/** A hand-built structural command: a `begin-node` marker that draws nothing. */
static PaintDisplayCommand BorderMakeStructural(LayoutNodeId node)
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
static void BorderAppend(PaintDisplayList *list,
                          PaintDisplayCommand command)
{
    RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboOk);
}

/**
 * The four bands a box with uniform insets must produce, computed from its own border box.
 *
 * A reader can check these four lines against CSS's box model without knowing anything about
 * this build, which is the point: a band is the region of the border box outside the padding box
 * on one edge, so with a uniform @p inset the expectation is the border box with one edge of the
 * given thickness.
 */
static void BorderUniformBands(LayoutRect borderBox,
                                 int32_t inset,
                                 LayoutRect *outTop,
                                 LayoutRect *outRight,
                                 LayoutRect *outBottom,
                                 LayoutRect *outLeft)
{
    outTop->x = borderBox.x;
    outTop->y = borderBox.y;
    outTop->width = borderBox.width;
    outTop->height = inset;

    outRight->x = borderBox.x + borderBox.width - inset;
    outRight->y = borderBox.y;
    outRight->width = inset;
    outRight->height = borderBox.height;

    outBottom->x = borderBox.x;
    outBottom->y = borderBox.y + borderBox.height - inset;
    outBottom->width = borderBox.width;
    outBottom->height = inset;

    outLeft->x = borderBox.x;
    outLeft->y = borderBox.y;
    outLeft->width = inset;
    outLeft->height = borderBox.height;
}

/** The four band commands of @p node, in order top, right, bottom, left, asserted to exist. */
static void BorderFourBands(const PaintDisplayList *list,
                              LayoutNodeId node,
                              PaintDisplayCommand *outBands)
{
    size_t side;

    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        RoboCheckEqInt(BorderCommandOfKindFor(list, RoboPaintDisplayCmdDrawBorder,
                                                     node, side, &outBands[side]),
                          1);
    }
}

/** The border box of @p node, read out of its opening marker rather than assumed. */
static LayoutRect BorderBoxOf(const PaintDisplayList *list,
                                        LayoutNodeId node)
{
    PaintDisplayCommand marker;

    RoboCheckEqInt(BorderCommandOfKindFor(list, RoboPaintDisplayCmdBeginNode, node,
                                                 0u, &marker),
                      1);
    return marker.rect;
}

/* ------------------------------------------------------------------ */
/* 1. The style travels the chain                                      */
/* ------------------------------------------------------------------ */

static void TestBorderStyleReachesTheCommand(void)
{
    static const struct {
        const char *declaration;
        CssBorderStyle expected;
    } kCases[] = {
        { "border: 4px solid #111111", RoboCssBorderStyleSolid },
        { "border: 4px dashed #ff0000", RoboCssBorderStyleDashed },
        { "border: 4px dotted #ff0000", RoboCssBorderStyleDotted },
        { "border: 4px double #ff0000", RoboCssBorderStyleDouble }
    };
    size_t which;

    PaintFixture();

    /*
     * The claim in one loop: for each of the styles this half of CSS is exercised with, the
     * **record** carries it, the **command** carries it, and they agree -- on all four sides, so
     * a bug that read one side twice cannot pass. The record's accessor is asserted too, because
     * the interesting failure this phase prevents is a style that survives the traversal and is
     * then dropped by the builder.
     */
    for (which = 0u; which < sizeof(kCases) / sizeof(kCases[0]); which++) {
        PaintFixtureDoc fixture;
        PaintResult result;
        PaintDisplayList *list = NULL;
        PaintDisplayListStats listStats;
        LayoutNodeId box;
        char sheet[256];
        size_t bands = 0u;
        size_t index;

        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"box\"></div></body></html>",
                             BorderSheet(sheet, sizeof(sheet), kCases[which].declaration));
        PaintOnce(&fixture, &result);
        BorderBuild(&result, &list, &listStats);
        box = PaintBox(&fixture, "box");

        /* The record layer: the traversal's own accessor answers the declared style for every
         * border record of this box, or `NONE` for a kind that is not a border. */
        for (index = 0u; index < PaintListCount(result.list); index++) {
            PaintOperation operation;

            RoboCheckEqInt(PaintListAt(result.list, index, &operation), RoboOk);
            if (operation.node != box) {
                continue;
            }
            if (PaintOpKindIsBorder(operation.kind)) {
                bands++;
                RoboCheckEqInt((int)PaintOperationStyle(&operation),
                                  (int)kCases[which].expected);
            } else {
                RoboCheckEqInt((int)PaintOperationStyle(&operation), 0);
            }
        }
        RoboCheckEqInt((int)bands, 4);

        /* And the command layer, which is where a style that was carried could still be lost. */
        bands = 0u;
        for (index = 0u; index < PaintDisplayListCount(list); index++) {
            PaintDisplayCommand command;

            RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
            if (command.node != box) {
                continue;
            }
            if (command.kind == RoboPaintDisplayCmdDrawBorder) {
                bands++;
                RoboCheckEqInt((int)PaintDisplayCommandStyle(&command),
                                  (int)kCases[which].expected);
            } else {
                /* A non-border command carries no style, which is the mirror half of the rule. */
                RoboCheckEqInt((int)PaintDisplayCommandStyle(&command), 0);
            }
        }
        RoboCheckEqInt((int)bands, 4);
        RoboCheckEqInt((int)listStats.borders, 4);

        PaintDisplayListDestroy(list);
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 2. The four bands                                                   */
/* ------------------------------------------------------------------ */

static void TestBorderFourBands(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand bands[RoboCssBoxSideCount];
    PaintDisplayCommand content;
    LayoutRect borderBox;
    LayoutRect expected[RoboCssBoxSideCount];
    LayoutNodeId box;
    size_t side;

    /* `width: 200px; height: 100px` is the **content** box, so the border box is 208x108: the box
     * model is layout's conclusion and this case reads it back rather than assuming it. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 200px; height: 100px; border: 4px solid #111111 }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    box = PaintBox(&fixture, "box");
    RoboCheckEqInt((int)listStats.borders, 4);
    RoboCheckEqInt((int)listStats.backgrounds, 0);
    RoboCheckEqInt((int)listStats.draws, 4);

    borderBox = BorderBoxOf(list, box);
    RoboCheckEqInt((int)borderBox.width, 208);
    RoboCheckEqInt((int)borderBox.height, 108);

    BorderFourBands(list, box, bands);
    BorderUniformBands(borderBox, 4, &expected[0], &expected[1], &expected[2], &expected[3]);
    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        PaintRectEq(bands[side].rect, expected[side], "a border band");
        RoboCheckEqInt((int)bands[side].style, (int)RoboCssBorderStyleSolid);
    }

    /* And the band is not the content box: the two rectangles a band is the difference of are
     * both still in the list, and they are 4px apart on every edge. */
    RoboCheckEqInt(BorderCommandOfKindFor(list, RoboPaintDisplayCmdContent, box, 0u,
                                                 &content),
                      1);
    RoboCheckEqInt((int)content.rect.x, (int)borderBox.x + 4);
    RoboCheckEqInt((int)content.rect.y, (int)borderBox.y + 4);
    RoboCheckEqInt((int)content.rect.width, (int)borderBox.width - 8);
    RoboCheckEqInt((int)content.rect.height, (int)borderBox.height - 8);

    /* Every band reaches the surface, in the border's colour. */
    surface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.draws, 4);
    RoboCheckEqInt((int)stats.borders, 4);
    RoboCheckEqInt((int)stats.filled, 4);
    RoboCheckEqInt((int)stats.empty, 0);
    RoboCheckEqInt((int)stats.unshaped, 0);
    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        BorderCentreIs(surface, bands[side].rect, BORDER_BLACK, "a filled border band");
    }

    /* One pixel past the left band's inner edge is the content box, where nothing drew. */
    BorderPixelIs(surface, borderBox.x + 4, borderBox.y + borderBox.height / 2,
                    GfxRgba8(0, 0, 0, 0),
                    "the first pixel of the content box, where no background was declared");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Per-side widths                                                  */
/* ------------------------------------------------------------------ */

static void TestBorderPerSideWidths(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand bands[RoboCssBoxSideCount];
    LayoutRect borderBox;
    LayoutNodeId box;

    /*
     * The four-value shorthand, which RoboCSS distributes as top, right, bottom, left. The
     * assertion is that the *paint* layer reads the per-side used widths rather than one uniform
     * number: each band's facing extent is its own side's width, and its other extent is the
     * whole border box on that axis -- which is what makes a box with two borders have two
     * full-height bands rather than two half-height ones.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 100px; height: 50px;"
                         "   border-width: 1px 2px 3px 4px; border-style: solid;"
                         "   border-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    box = PaintBox(&fixture, "box");
    RoboCheckEqInt((int)listStats.borders, 4);
    borderBox = BorderBoxOf(list, box);
    /* 100 + 2 + 4 and 50 + 1 + 3. */
    RoboCheckEqInt((int)borderBox.width, 106);
    RoboCheckEqInt((int)borderBox.height, 54);

    BorderFourBands(list, box, bands);
    /* Top: full width, 1px tall. Right: 2px wide, full height. Bottom: full width, 3px tall.
     * Left: 4px wide, full height. */
    PaintRectIs(borderBox.x, borderBox.y, borderBox.width, 1, bands[0].rect, "the top band");
    PaintRectIs(borderBox.x + borderBox.width - 2, borderBox.y, 2, borderBox.height,
                  bands[1].rect, "the right band");
    PaintRectIs(borderBox.x, borderBox.y + borderBox.height - 3, borderBox.width, 3,
                  bands[2].rect, "the bottom band");
    PaintRectIs(borderBox.x, borderBox.y, 4, borderBox.height, bands[3].rect,
                  "the left band");

    surface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.borders, 4);
    RoboCheckEqInt((int)stats.filled, 4);
    RoboCheckEqInt((int)stats.empty, 0);
    BorderCentreIs(surface, bands[0].rect, BORDER_RED, "the 1px top band");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Sides that are not there                                         */
/* ------------------------------------------------------------------ */

static void TestBorderAbsentSides(void)
{
    static const struct {
        const char *declaration;
        size_t expectedBands;
    } kCases[] = {
        /* A `none` style and a zero width are *different* declarations and the same conclusion:
         * a side whose used width is zero emits no operation, which is RoboLayout's conclusion
         * and not a decision this layer makes. The two "some sides" rows are the complement --
         * the layer does not require all four. */
        { "border-style: none; border-width: 4px", 0u },
        { "border-style: solid; border-width: 0px", 0u },
        { "border: 4px none #ff0000", 0u },
        { "border: 4px hidden #ff0000", 0u },
        { "border-top: 4px solid #ff0000", 1u },
        { "border: 4px solid #ff0000; border-left-width: 0px", 3u }
    };
    size_t which;

    PaintFixture();

    for (which = 0u; which < sizeof(kCases) / sizeof(kCases[0]); which++) {
        PaintFixtureDoc fixture;
        PaintResult result;
        PaintDisplayList *list = NULL;
        PaintDisplayListStats listStats;
        PaintDecorationStats stats;
        GfxSurface *surface = NULL;
        char sheet[256];

        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"box\"></div></body></html>",
                             BorderSheet(sheet, sizeof(sheet), kCases[which].declaration));
        PaintOnce(&fixture, &result);
        BorderBuild(&result, &list, &listStats);
        RoboCheckEqInt((int)listStats.borders, (int)kCases[which].expectedBands);

        surface = BorderSurface(64u, 64u, RoboGfxSurfaceAccessReadWrite);
        BorderPaint(list, surface, &stats);
        RoboCheckEqInt((int)stats.borders, (int)kCases[which].expectedBands);
        RoboCheckEqInt((int)stats.filled, (int)kCases[which].expectedBands);
        RoboCheckEqInt((int)stats.backgrounds, 0);
        RoboCheckEqInt((int)stats.commands, (int)(stats.draws + stats.skipped));

        if (kCases[which].expectedBands == 0u) {
            /* Nothing was declared to draw, so the surface is what the caller handed over. */
            BorderPixelIs(surface, 8, 8, GfxRgba8(0, 0, 0, 0),
                            "a surface no side drew into");
        }

        GfxSurfaceRelease(surface);
        PaintDisplayListDestroy(list);
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 5. Colours, and which side owns a corner                            */
/* ------------------------------------------------------------------ */

static void TestBorderColorsAndCorners(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand bands[RoboCssBoxSideCount];
    LayoutRect borderBox;
    LayoutNodeId box;

    /* Four colours in the shorthand's top, right, bottom, left order. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 100px; height: 60px; border: 5px solid #000000;"
                         "   border-color: #ff0000 #00ff00 #0000ff #ffff00 }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    box = PaintBox(&fixture, "box");
    BorderFourBands(list, box, bands);
    borderBox = BorderBoxOf(list, box);

    PaintColorEq(bands[0].color, CssColorMake(0xff, 0x00, 0x00, 255), "the top band");
    PaintColorEq(bands[1].color, CssColorMake(0x00, 0xff, 0x00, 255), "the right band");
    PaintColorEq(bands[2].color, CssColorMake(0x00, 0x00, 0xff, 255), "the bottom band");
    PaintColorEq(bands[3].color, CssColorMake(0xff, 0xff, 0x00, 255), "the left band");

    surface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.filled, 4);

    /* Each side's own middle is its own colour. */
    BorderCentreIs(surface, bands[0].rect, BORDER_RED, "the top");
    BorderCentreIs(surface, bands[1].rect, BORDER_GREEN, "the right");
    BorderCentreIs(surface, bands[2].rect, GfxRgba8(0x00, 0x00, 0xff, 255), "the bottom");
    BorderCentreIs(surface, bands[3].rect, GfxRgba8(0xff, 0xff, 0x00, 255), "the left");

    /*
     * And the corners, where two bands overlap by design. The four bands are emitted top, right,
     * bottom, left -- the order the sides are numbered -- and each spans its axis's full extent,
     * so the band drawn **last** owns the corner. That is a statement about this build's
     * simplification rather than about CSS (a real renderer mitres the corners), so it is pinned
     * where it is observable: the top-left corner is the left band's yellow (left is drawn after
     * top), the top-right is the right band's green (right after top), the bottom-right is the
     * bottom band's blue (bottom after right) and the bottom-left is yellow again (left after
     * bottom).
     */
    BorderPixelIsRgba(surface, borderBox.x, borderBox.y, 0xff, 0xff, 0x00, 255,
                         "the top-left corner, where left is drawn after top");
    BorderPixelIsRgba(surface, borderBox.x + borderBox.width - 1, borderBox.y, 0x00, 0xff,
                         0x00, 255, "the top-right corner, where right is drawn after top");
    BorderPixelIsRgba(surface, borderBox.x + borderBox.width - 1,
                         borderBox.y + borderBox.height - 1, 0x00, 0x00, 0xff, 255,
                         "the bottom-right corner, where bottom is drawn after right");
    BorderPixelIsRgba(surface, borderBox.x, borderBox.y + borderBox.height - 1, 0xff, 0xff,
                         0x00, 255, "the bottom-left corner, where left is drawn after bottom");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Background and border together                                   */
/* ------------------------------------------------------------------ */

static void TestBorderWithBackground(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand bands[RoboCssBoxSideCount];
    PaintDisplayCommand background;
    LayoutRect borderBox;
    LayoutNodeId box;

    /* The pipeline's headline case: 11.4's background and 11.5's border in one box, from one
     * list, through one pass. */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         BORDER_BASE_CSS
                         " .box { width: 200px; height: 100px; padding: 8px;"
                         "   background-color: #3366cc; border: 4px solid #111111 }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    box = PaintBox(&fixture, "box");
    RoboCheckEqInt((int)listStats.backgrounds, 1);
    RoboCheckEqInt((int)listStats.borders, 4);
    RoboCheckEqInt((int)listStats.draws, 5);

    borderBox = BorderBoxOf(list, box);
    RoboCheckEqInt(BorderCommandOfKindFor(list, RoboPaintDisplayCmdDrawBackground,
                                                 box, 0u, &background),
                      1);
    BorderFourBands(list, box, bands);

    /* The background is the whole border box -- CSS's initial `background-clip` -- and it is
     * emitted **before** the four bands, which is what makes the border visible over it. */
    PaintRectEq(background.rect, borderBox, "a background over the border box");
    {
        size_t backgroundIndex = BorderIndexOfKindFor(list,
                                                           RoboPaintDisplayCmdDrawBackground,
                                                           box);
        size_t firstBand = BorderIndexOfKindFor(list, RoboPaintDisplayCmdDrawBorder,
                                                    box);

        RoboCheck(backgroundIndex != (size_t)-1);
        RoboCheck(firstBand != (size_t)-1);
        RoboCheck(backgroundIndex < firstBand);
    }

    surface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.draws, 5);
    RoboCheckEqInt((int)stats.backgrounds, 1);
    RoboCheckEqInt((int)stats.borders, 4);
    RoboCheckEqInt((int)stats.filled, 5);

    /* The ring is the border and inside it the background: two colours, one box, and the
     * boundary between them is exactly the padding box's edge. */
    BorderPixelIs(surface, borderBox.x + 2, borderBox.y + 2, BORDER_BLACK,
                    "inside the top band");
    BorderPixelIs(surface, borderBox.x + borderBox.width / 2, borderBox.y + 2, BORDER_BLACK,
                    "inside the top band, at its middle");
    BorderPixelIsRgba(surface, borderBox.x + 4, borderBox.y + 4, 0x33, 0x66, 0xcc, 255,
                         "the background's first pixel, just inside the border");
    BorderPixelIs(surface, 0, (int32_t)GfxSurfaceHeight(surface) - 1,
                    GfxRgba8(0, 0, 0, 0), "a pixel outside the box entirely");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. One pass, one order: a child over its parent's border            */
/* ------------------------------------------------------------------ */

static void TestBorderChildOverParentBorder(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand parentBands[RoboCssBoxSideCount];
    PaintDisplayCommand childBackground;
    LayoutRect parentBorderBox;
    LayoutNodeId parentId;
    LayoutNodeId childId;

    /*
     * The decisive case for the phase's one architectural decision. The child is moved by a
     * relative offset until it spills over its parent's border, and the pixel over the border is
     * the child's colour -- because the list's order is the traversal's, so a descendant's
     * background is filled *after* its ancestor's bands. A painter that walked the list twice,
     * once per kind, would fill every background in the document before every border and paint
     * the parent's ring over the child's spill.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"parent\"><div class=\"child\"></div>"
                         "</div></body></html>",
                         BORDER_BASE_CSS
                         " .parent { width: 120px; height: 60px; border: 4px solid #111111 }"
                         " .child { width: 40px; height: 20px; background-color: #00ff00;"
                         "   position: relative; left: -20px; top: -20px }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    parentId = PaintBox(&fixture, "parent");
    childId = PaintBox(&fixture, "child");
    BorderFourBands(list, parentId, parentBands);
    parentBorderBox = BorderBoxOf(list, parentId);
    RoboCheckEqInt(BorderCommandOfKindFor(list, RoboPaintDisplayCmdDrawBackground,
                                                 childId, 0u, &childBackground),
                      1);

    /* The order, as indices: the parent's four bands, then the child's background -- the
     * traversal's order and the whole reason there is one pass. */
    {
        size_t firstBand = BorderIndexOfKindFor(list, RoboPaintDisplayCmdDrawBorder,
                                                     parentId);
        size_t lastBand = BorderLastIndexOfKindFor(list, RoboPaintDisplayCmdDrawBorder,
                                                         parentId);
        size_t childIndex =
            BorderIndexOfKindFor(list, RoboPaintDisplayCmdDrawBackground, childId);

        RoboCheck(firstBand != (size_t)-1);
        RoboCheck(lastBand != (size_t)-1);
        RoboCheck(childIndex != (size_t)-1);
        RoboCheck(lastBand < childIndex);
    }

    surface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.filled, 5);
    RoboCheckEqInt((int)stats.draws, 5);

    /* The child's rectangle is the *moved* one: 20px left and 20px up from where flow put it,
     * which was inside the parent's padding box -- so it now covers the parent's top and left
     * bands. Those pixels are the child's green, not the parent's black. */
    {
        int32_t probeX = parentBorderBox.x + 10;
        int32_t probeY = parentBorderBox.y + 2;

        RoboCheck(probeX >= childBackground.rect.x);
        RoboCheck(probeX < childBackground.rect.x + childBackground.rect.width);
        RoboCheck(probeY >= childBackground.rect.y);
        RoboCheck(probeY < childBackground.rect.y + childBackground.rect.height);
        BorderPixelIs(surface, probeX, probeY, BORDER_GREEN,
                        "the child's spill over the parent's top band");
        BorderPixelIs(surface, parentBorderBox.x + 80, parentBorderBox.y + 2, BORDER_BLACK,
                        "the parent's top band where the child does not reach");
    }

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Clipping                                                         */
/* ------------------------------------------------------------------ */

/** What one run of the clipping document produced: the spill's top band and the pass record. */
typedef struct borderClipProbe {
    LayoutRect band;
    PaintDecorationStats stats;
} BorderClipProbe;

/**
 * Lay out the clipping document with @p hidden and paint it.
 *
 * The two runs differ in **one declaration** and in nothing else -- same markup, same sheet,
 * same order -- so a difference in the pixels is attributable to the overflow mode.
 */
static void BorderClipRun(int hidden, BorderClipProbe *out, GfxSurface *surface)
{
    static const char *const kMarkup =
        "<html><body><aside class=\"clip\"><figure class=\"spill\"></figure>"
        "</aside></body></html>";
    /* The one declaration the two runs differ in, both spelled out in full: a reader can see the
     * whole difference between the documents and check that it is one property. */
    static const char *const kHiddenSheet =
        BORDER_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: hidden }"
        " .spill { width: 200px; height: 200px; border: 4px solid #ff0000 }";
    static const char *const kVisibleSheet =
        BORDER_BASE_CSS
        " .clip { width: 60px; height: 30px; overflow: visible }"
        " .spill { width: 200px; height: 200px; border: 4px solid #ff0000 }";
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDisplayCommand band;
    LayoutNodeId spillId;

    PaintBeginLaidOut(&fixture, kMarkup, hidden ? kHiddenSheet : kVisibleSheet);
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    spillId = PaintBox(&fixture, "spill");
    RoboCheckEqInt(BorderCommandOfKindFor(list, RoboPaintDisplayCmdDrawBorder,
                                                 spillId, 0u, &band),
                      1);
    out->band = band.rect;

    if (hidden) {
        /* The boundary is *on the command*, which is why a rasteriser applies it from the
         * command rather than from a separate operation. */
        RoboCheckEqInt(PaintClipIsActive(&band.clip), 1);
    } else {
        RoboCheckEqInt(PaintClipIsActive(&band.clip), 0);
    }

    BorderPaint(list, surface, &out->stats);
    RoboCheckEqInt((int)out->stats.borders, 4);
    RoboCheckEqInt((int)out->stats.filled, 4);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

static void TestBorderClipping(void)
{
    BorderClipProbe clipped;
    BorderClipProbe visible;
    GfxSurface *clippedSurface = NULL;
    GfxSurface *visibleSurface = NULL;

    PaintFixture();

    clippedSurface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    visibleSurface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderClipRun(1, &clipped, clippedSurface);
    BorderClipRun(0, &visible, visibleSurface);

    /* The same band either way -- the spill declares the same border and the same box. */
    RoboCheckEqInt((int)clipped.band.height, 4);
    RoboCheckEqInt((int)clipped.band.x, (int)visible.band.x);
    RoboCheckEqInt((int)clipped.band.y, (int)visible.band.y);

    /* Only the hidden run bounds a fill, and it says so in the record. */
    RoboCheck((int)clipped.stats.clipped >= 1);
    RoboCheckEqInt((int)visible.stats.clipped, 0);

    /* Inside the ancestor, both runs fill the band red. */
    BorderPixelIs(clippedSurface, clipped.band.x + 5, clipped.band.y + 2, BORDER_RED,
                    "inside the clip, hidden");
    BorderPixelIs(visibleSurface, visible.band.x + 5, visible.band.y + 2, BORDER_RED,
                    "inside the clip, visible");

    /* Far to the right, inside the spill's own band and outside the 60px ancestor: the visible
     * run fills it and the hidden run leaves it untouched. That difference **is** the clip, and
     * it is the same mechanism a background is bounded by -- a band is a fill like any other. */
    BorderPixelIs(visibleSurface, visible.band.x + 150, visible.band.y + 2, BORDER_RED,
                    "the band outside its ancestor, visible overflow");
    BorderPixelIs(clippedSurface, clipped.band.x + 150, clipped.band.y + 2,
                    GfxRgba8(0, 0, 0, 0),
                    "the band outside its ancestor, hidden overflow");

    /* The mechanism, without a document to infer it from: a hand-built band whose clip bounds one
     * axis. The axis the clip leaves open is the surface's own interval, which is what makes
     * "bounded horizontally, free vertically" expressible at all. */
    {
        PaintDisplayList *list = NULL;
        PaintDecorationStats stats;
        GfxSurface *plain = BorderSurface(32u, 32u, RoboGfxSurfaceAccessReadWrite);

        RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
        BorderAppend(list,
                      BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 0, 10, 32),
                                       BorderClipX(0, 5), CssColorMake(0, 0, 255, 255),
                                       RoboCssBorderStyleSolid));
        BorderPaint(list, plain, &stats);
        RoboCheckEqInt((int)stats.filled, 1);
        RoboCheckEqInt((int)stats.clipped, 1);
        BorderPixelIs(plain, 0, 0, GfxRgba8(0, 0, 255, 255), "inside the bounded axis");
        BorderPixelIs(plain, 4, 31, GfxRgba8(0, 0, 255, 255),
                        "the interval's last column, at the bottom of the surface");
        BorderPixelIs(plain, 5, 31, GfxRgba8(0, 0, 0, 0), "one pixel past the interval");
        BorderPixelIs(plain, 9, 9, GfxRgba8(0, 0, 0, 0), "the far side of the band");
        PaintDisplayListDestroy(list);
        GfxSurfaceRelease(plain);
    }

    GfxSurfaceRelease(clippedSurface);
    GfxSurfaceRelease(visibleSurface);
}

/* ------------------------------------------------------------------ */
/* 9. Pattern styles are bands, and counted as such                    */
/* ------------------------------------------------------------------ */

static void TestBorderPatternStyles(void)
{
    static const struct {
        const char *name;
        CssBorderStyle style;
    } kStyles[] = {
        { "dashed", RoboCssBorderStyleDashed },
        { "dotted", RoboCssBorderStyleDotted },
        { "double", RoboCssBorderStyleDouble },
        { "groove", RoboCssBorderStyleGroove },
        { "ridge", RoboCssBorderStyleRidge },
        { "inset", RoboCssBorderStyleInset },
        { "outset", RoboCssBorderStyleOutset }
    };
    size_t which;

    PaintFixture();

    /*
     * The seven pattern styles, each in its own case: the band is **drawn**, in the side's
     * colour, and the simplification is a number rather than a silence. `unshaped` is what keeps
     * `borderPainting == 1` from being read as "this build draws every border the way it looks"
     * -- the style is on the command for the rasteriser that will shape it, and counted here for
     * the reader who needs to know this build does not.
     */
    for (which = 0u; which < sizeof(kStyles) / sizeof(kStyles[0]); which++) {
        PaintFixtureDoc fixture;
        PaintResult result;
        PaintDisplayList *list = NULL;
        PaintDisplayListStats listStats;
        PaintDecorationStats stats;
        GfxSurface *surface = NULL;
        PaintDisplayCommand bands[RoboCssBoxSideCount];
        char declaration[64];
        char sheet[256];
        size_t length = 0u;
        const char *part;
        size_t side;
        LayoutNodeId box;

        declaration[0] = '\0';
        for (part = "border: 4px "; *part != '\0' && length + 1u < sizeof(declaration); part++) {
            declaration[length++] = *part;
            declaration[length] = '\0';
        }
        for (part = kStyles[which].name; *part != '\0' && length + 1u < sizeof(declaration);
             part++) {
            declaration[length++] = *part;
            declaration[length] = '\0';
        }
        for (part = " #ff0000"; *part != '\0' && length + 1u < sizeof(declaration); part++) {
            declaration[length++] = *part;
            declaration[length] = '\0';
        }

        PaintBeginLaidOut(&fixture,
                             "<html><body><div class=\"box\"></div></body></html>",
                             BorderSheet(sheet, sizeof(sheet), declaration));
        PaintOnce(&fixture, &result);
        BorderBuild(&result, &list, &listStats);
        box = PaintBox(&fixture, "box");
        BorderFourBands(list, box, bands);

        /* The style is on every band command, unmangled: this is the fact that makes a band
         * render-ready rather than merely placed. */
        for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
            RoboCheckEqInt((int)bands[side].style, (int)kStyles[which].style);
        }

        surface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
        BorderPaint(list, surface, &stats);
        RoboCheckEqInt((int)stats.borders, 4);
        RoboCheckEqInt((int)stats.filled, 4);
        /* Drawn as the band, and counted as simplified -- both, because both are true. */
        RoboCheckEqInt((int)stats.unshaped, 4);
        BorderCentreIs(surface, bands[0].rect, BORDER_RED, "a pattern style's band");

        GfxSurfaceRelease(surface);
        PaintDisplayListDestroy(list);
        PaintResultDispose(&result);
        PaintEnd(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* 10. The painter's own contract                                      */
/* ------------------------------------------------------------------ */

static void TestBorderHandBuiltKinds(void)
{
    PaintDisplayList *list = NULL;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand band;

    PaintFixture();

    /* The style rule, from the record layer's side: a `draw-border` with no style is not a
     * command, because a band whose shape is unstated is one its consumer could only guess at. */
    band = BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 0, 8, 2),
                            PaintClipNone(), CssColorMake(0, 255, 0, 255),
                            RoboCssBorderStyleNone);
    RoboCheckEqInt(PaintDisplayCommandIsValid(&band), 0);
    /* `hidden` is a style a *side* may have and one that generates no border, so a command
     * cannot carry it either. */
    band = BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 0, 8, 2),
                            PaintClipNone(), CssColorMake(0, 255, 0, 255),
                            RoboCssBorderStyleHidden);
    RoboCheckEqInt(PaintDisplayCommandIsValid(&band), 0);

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    surface = BorderSurface(32u, 32u, RoboGfxSurfaceAccessReadWrite);

    /* A structural command -- which draws nothing -- then a solid band, a pattern band whose band
     * is filled anyway, a `currentColor` band, a transparent band and a zero-area band. */
    BorderAppend(list, BorderMakeStructural((LayoutNodeId)1u));
    BorderAppend(list, BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 0, 8, 2),
                                        PaintClipNone(),
                                        CssColorMake(0, 255, 0, 255),
                                        RoboCssBorderStyleSolid));
    BorderAppend(list, BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 2, 8, 2),
                                        PaintClipNone(),
                                        CssColorMake(255, 0, 0, 255),
                                        RoboCssBorderStyleDashed));
    BorderAppend(list, BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 4, 8, 2),
                                        PaintClipNone(), CssColorCurrent(),
                                        RoboCssBorderStyleSolid));
    BorderAppend(list, BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 6, 8, 2),
                                        PaintClipNone(),
                                        CssColorMake(0, 0, 255, 0),
                                        RoboCssBorderStyleSolid));
    BorderAppend(list, BorderMakeBand((LayoutNodeId)1u, MakeGfxRect(0, 8, 8, 0),
                                        PaintClipNone(),
                                        CssColorMake(0, 0, 255, 255),
                                        RoboCssBorderStyleSolid));

    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.commands, 6);
    RoboCheckEqInt((int)stats.draws, 5);
    RoboCheckEqInt((int)stats.borders, 5);
    RoboCheckEqInt((int)stats.backgrounds, 0);
    RoboCheckEqInt((int)stats.skipped, 1);
    RoboCheckEqInt((int)stats.contextual, 1);
    RoboCheckEqInt((int)stats.transparent, 1);
    RoboCheckEqInt((int)stats.empty, 1);
    RoboCheckEqInt((int)stats.unshaped, 1);
    RoboCheckEqInt((int)stats.filled, 2);
    RoboCheckEqInt((int)stats.clipped, 0);
    /* The three identities, on a list this file built by hand. */
    RoboCheckEqInt((int)stats.commands, (int)(stats.draws + stats.skipped));
    RoboCheckEqInt((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    RoboCheckEqInt((int)stats.draws, (int)(stats.filled + stats.contextual +
                                               stats.transparent + stats.empty));

    /* The solid band and the pattern band both reached the surface, in their own colours. */
    BorderPixelIs(surface, 3, 0, BORDER_GREEN, "the solid band");
    BorderPixelIs(surface, 3, 3, BORDER_RED, "the dashed band, drawn as its band");
    BorderPixelIs(surface, 3, 5, GfxRgba8(0, 0, 0, 0), "the currentColor band");
    BorderPixelIs(surface, 3, 7, GfxRgba8(0, 0, 0, 0), "the transparent band");

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
}

/* ------------------------------------------------------------------ */
/* 11. Determinism                                                     */
/* ------------------------------------------------------------------ */

static void TestBorderDeterminism(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayList *rebuilt = NULL;
    PaintDisplayListStats listStats;
    PaintDisplayListStats rebuiltStats;
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
                         BORDER_BASE_CSS
                         " .card { width: 120px; height: 60px; padding: 6px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456;"
                         "   border: 1px dashed #ff0000 }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    one = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    two = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(list, one, &first);
    BorderPaint(list, two, &second);
    BorderStatsEq(&first, &second, "two passes over one list");

    /* Every fill landed in the same place, pixel for pixel -- the sample is each drawing
     * command's own rectangle, so the comparison follows the document rather than a fixed handful
     * of coordinates. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;
        GfxColorRgba8 left;
        GfxColorRgba8 right;
        int32_t x = 0;
        int32_t y = 0;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        BorderCentre(command.rect, &x, &y);
        left = BorderRead(one, x, y);
        right = BorderRead(two, x, y);
        RoboCheckEqInt((int)left.r, (int)right.r);
        RoboCheckEqInt((int)left.g, (int)right.g);
        RoboCheckEqInt((int)left.b, (int)right.b);
        RoboCheckEqInt((int)left.a, (int)right.a);
    }

    /* And an independently built list for the same document is the *same* list: same order, same
     * geometry, same styles -- which is the strongest form of determinism available, and the one
     * that would catch a field travelling through the builder by accident. */
    BorderBuild(&result, &rebuilt, &rebuiltStats);
    RoboCheckEqInt(PaintDisplayListEqual(list, rebuilt), 1);
    RoboCheckEqInt((int)rebuiltStats.borders, (int)listStats.borders);

    /* A second pass over the *same* surface leaves it as it was: an opaque fill is idempotent,
     * which is what makes a rebuilt display list safe to repaint in place. */
    BorderPaint(list, one, &again);
    BorderStatsEq(&first, &again, "a repeat pass over one surface");

    GfxSurfaceRelease(one);
    GfxSurfaceRelease(two);
    PaintDisplayListDestroy(rebuilt);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* 12. Snapshot isolation                                              */
/* ------------------------------------------------------------------ */

static void TestBorderMutationIsolation(void)
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
                         BORDER_BASE_CSS
                         " .box { height: 40px; border: 6px solid #ff0000 }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &before, &beforeStats);

    one = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(before, one, &first);

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and lay the
     * *same* tree out at a different viewport, which changes every box's geometry and therefore
     * every band. A display list is a snapshot, so the one already built paints exactly as it did
     * -- and the mutation is *proved* observable by building a second display list afterwards and
     * asserting it differs.
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
    BorderBuild(&result, &after, &afterStats);
    RoboCheckEqInt(PaintDisplayListEqual(before, after), 0);

    two = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(before, two, &second);
    BorderStatsEq(&first, &second, "a paint after the sources changed");

    for (index = 0u; index < PaintDisplayListCount(before); index++) {
        PaintDisplayCommand command;
        GfxColorRgba8 left;
        GfxColorRgba8 right;
        int32_t x = 0;
        int32_t y = 0;

        RoboCheckEqInt(PaintDisplayListAt(before, index, &command), RoboOk);
        if (!PaintDisplayCommandKindDraws(command.kind)) {
            continue;
        }
        BorderCentre(command.rect, &x, &y);
        left = BorderRead(one, x, y);
        right = BorderRead(two, x, y);
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
/* 13. Empty, degenerate and refusing cases                            */
/* ------------------------------------------------------------------ */

static void TestBorderEmptyCases(void)
{
    PaintDisplayList *list = NULL;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    GfxSurface *readonly = NULL;
    PaintDiag diag;

    PaintFixture();

    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    surface = BorderSurface(16u, 16u, RoboGfxSurfaceAccessReadWrite);

    /* An empty list is a legitimate pass: nothing to do, and every counter nought. */
    memset(&stats, 0xff, sizeof(stats));
    PaintDecorationStatsInit(NULL); /* safe */
    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.commands, 0);
    RoboCheckEqInt((int)stats.draws, 0);
    RoboCheckEqInt((int)stats.borders, 0);
    RoboCheckEqInt((int)stats.filled, 0);
    RoboCheckEqInt((int)stats.skipped, 0);

    /* A list of structural commands alone: visited, skipped, and nothing drawn. */
    BorderAppend(list, BorderMakeStructural((LayoutNodeId)1u));
    BorderAppend(list, BorderMakeStructural((LayoutNodeId)1u));
    BorderPaint(list, surface, &stats);
    RoboCheckEqInt((int)stats.commands, 2);
    RoboCheckEqInt((int)stats.borders, 0);
    RoboCheckEqInt((int)stats.skipped, 2);
    BorderPixelIs(surface, 8, 8, GfxRgba8(0, 0, 0, 0), "a surface no band touched");

    /* The arguments are required, and a refusal says which. */
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintDecorationPaint(NULL, surface, NULL, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheck(diag.message != NULL);
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintDecorationPaint(list, NULL, NULL, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheck(diag.message != NULL);

    /* And a target that cannot take a write is refused with the code the header names, before any
     * fill is attempted -- the graphics layer's own rule, mapped once. */
    readonly = BorderSurface(16u, 16u, RoboGfxSurfaceAccessRead);
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintDecorationPaint(list, readonly, NULL, NULL, &diag),
                      RoboNotSupported);
    RoboCheck(diag.message != NULL);
    GfxSurfaceRelease(readonly);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
}

/* ------------------------------------------------------------------ */
/* 14. End to end                                                      */
/* ------------------------------------------------------------------ */

static void TestBorderEndToEnd(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats listStats;
    PaintDecorationStats stats;
    GfxSurface *surface = NULL;
    PaintDisplayCommand cardBands[RoboCssBoxSideCount];
    PaintDisplayCommand content;
    LayoutNodeId card;
    LayoutNodeId inner;
    char dump[8192];
    size_t dumpLength = 0u;
    size_t index;
    size_t clippedDrawings = 0u;

    /*
     * The whole pipeline in one case: HTML → CSS → layout → paint records → display list →
     * surface. The document exercises the box model (width/height/margin/padding/border), a
     * nested child, a relative offset, a pattern style and hidden overflow, so the walk it
     * produces has every shape a consumer will meet.
     */
    PaintBeginLaidOut(&fixture,
                         "<html><body>"
                         "<div class=\"card\">"
                         "  <div class=\"inner\"></div>"
                         "</div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         BORDER_BASE_CSS
                         " .card { width: 200px; height: 80px; padding: 8px; margin: 4px;"
                         "   border: 3px dashed #111111; background-color: #eeeeee }"
                         " .inner { width: 100px; height: 40px; margin: 6px;"
                         "   background-color: #00ff00; position: relative; left: 5px;"
                         "   border: 2px solid #0000ff }"
                         " .clip { width: 60px; height: 30px; overflow: hidden;"
                         "   border-left: 6px solid #ff0000 }"
                         " .spill { width: 200px; height: 200px; background-color: #ff0000 }");
    PaintOnce(&fixture, &result);
    BorderBuild(&result, &list, &listStats);

    card = PaintBox(&fixture, "card");
    inner = PaintBox(&fixture, "inner");
    BorderFourBands(list, card, cardBands);
    RoboCheckEqInt((int)cardBands[0].style, (int)RoboCssBorderStyleDashed);
    RoboCheckEqInt(BorderCommandOfKindFor(list, RoboPaintDisplayCmdContent, card, 0u,
                                                 &content),
                      1);

    surface = BorderSurface(800u, 600u, RoboGfxSurfaceAccessReadWrite);
    BorderPaint(list, surface, &stats);

    /* The pass's own identities hold for a real document, not just a hand-built one -- and the
     * display list's own counters agree with the pass's, which is the cross-check that keeps "the
     * list says X" and "the pass did X" from drifting. */
    RoboCheckEqInt((int)stats.commands, (int)PaintDisplayListCount(list));
    RoboCheckEqInt((int)stats.commands, (int)(stats.draws + stats.skipped));
    RoboCheckEqInt((int)stats.draws, (int)(stats.backgrounds + stats.borders));
    RoboCheckEqInt((int)stats.draws, (int)listStats.draws);
    RoboCheckEqInt((int)stats.backgrounds, (int)listStats.backgrounds);
    RoboCheckEqInt((int)stats.borders, (int)listStats.borders);
    RoboCheckEqInt((int)stats.filled + (int)stats.contextual + (int)stats.transparent +
                          (int)stats.empty,
                      (int)stats.draws);
    /* The card's four sides, the inner box's four and the clip box's left: nine bands. */
    RoboCheckEqInt((int)stats.borders, 9);
    RoboCheckEqInt((int)stats.backgrounds, 3);
    RoboCheckEqInt((int)stats.filled, (int)stats.draws);
    /* The card's four dashed sides are the patterns this build draws as bands. */
    RoboCheckEqInt((int)stats.unshaped, 4);

    /* Every command is a record, and the ones under an ancestor's boundary are the ones the pass
     * counted as clipped -- a drawing of either kind, because a band under a hidden ancestor is
     * bounded exactly as a background is. */
    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        RoboCheckEqInt(PaintDisplayListAt(list, index, &command), RoboOk);
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
        if (PaintDisplayCommandKindDraws(command.kind) &&
            PaintClipIsActive(&command.clip)) {
            clippedDrawings++;
        }
    }
    RoboCheckEqInt((int)clippedDrawings, (int)stats.clipped);
    RoboCheck((int)stats.clipped >= 1);

    /* The pixels: the card's ring is its dashed border's colour; inside it, past the inner child,
     * the card's own background -- three statements about one box that only a real pass makes. */
    BorderPixelIs(surface, cardBands[0].rect.x + 10, cardBands[0].rect.y + 1, BORDER_BLACK,
                    "the card's top band");
    BorderPixelIsRgba(surface, content.rect.x + 40, content.rect.y + 20, 0x00, 0xff, 0x00, 255,
                         "the inner box's background, inside the card's content box");
    BorderPixelIsRgba(surface, content.rect.x + 150, content.rect.y + 70, 0xee, 0xee, 0xee,
                         255, "the card's background, past the inner box");
    /* And the inner box's own border is a ring inside the card: 2px of blue at its left edge. */
    {
        PaintDisplayCommand innerBands[RoboCssBoxSideCount];

        BorderFourBands(list, inner, innerBands);
        BorderPixelIs(surface, innerBands[3].rect.x + 1,
                        innerBands[3].rect.y + innerBands[3].rect.height / 2,
                        GfxRgba8(0x00, 0x00, 0xff, 255), "the inner box's left band");
    }

    /* The developer's door still reads the input the painter consumed -- and a band's style is
     * in the dump, because a command the painter reads has no secrets from the reader. */
    memset(dump, 0, sizeof(dump));
    RoboCheckEqInt(PaintDisplayListWrite(list, dump, sizeof(dump), &dumpLength, NULL),
                      RoboOk);
    RoboCheck(dumpLength > 0u);
    RoboCheck(strstr(dump, "draw-border") != NULL);
    RoboCheck(strstr(dump, "dashed") != NULL);

    GfxSurfaceRelease(surface);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintBorder(void)
{
    TestRegister("paint.border.style_reaches_the_command",
                       TestBorderStyleReachesTheCommand);
    TestRegister("paint.border.four_bands", TestBorderFourBands);
    TestRegister("paint.border.per_side_widths", TestBorderPerSideWidths);
    TestRegister("paint.border.absent_sides", TestBorderAbsentSides);
    TestRegister("paint.border.colors_and_corners", TestBorderColorsAndCorners);
    TestRegister("paint.border.with_background", TestBorderWithBackground);
    TestRegister("paint.border.child_over_parent_border",
                       TestBorderChildOverParentBorder);
    TestRegister("paint.border.clipping", TestBorderClipping);
    TestRegister("paint.border.pattern_styles", TestBorderPatternStyles);
    TestRegister("paint.border.hand_built_kinds", TestBorderHandBuiltKinds);
    TestRegister("paint.border.determinism", TestBorderDeterminism);
    TestRegister("paint.border.mutation_isolation", TestBorderMutationIsolation);
    TestRegister("paint.border.empty_cases", TestBorderEmptyCases);
    TestRegister("paint.border.end_to_end", TestBorderEndToEnd);
}
