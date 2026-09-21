/*
 * RoboPaint image suite (Phase 11.7) — paint.image.*
 *
 * The fourth drawing kind, and the first whose appearance this module does **not** resolve: a
 * background's colour comes from a style, a run's characters from a text node, and an image's
 * pixels from a **resource** the document attached to an element. Every claim the release makes
 * is pinned where it can be observed rather than argued:
 *
 *   **the resource is the document's**       an `<img>` whose `src` was resolved through
 *                                            RoboHTML's own fetch seam paints the pixels that
 *                                            were decoded — asserted at the pixel, against a
 *                                            surface this suite encoded itself;
 *
 *   **the destination is layout's**          the image's rectangle is the **content box**
 *                                            RoboLayout concluded for its element, checked
 *                                            against the tree with the box suites' own helper
 *                                            and then read back off the display command;
 *
 *   **the source is the whole image**        a payload names `(0, 0, width, height)` and says so
 *                                            (`PaintImageIsFullSource`); the scaling into
 *                                            the destination is the graphics module's
 *                                            nearest-neighbour sampler, pinned to the pixel at
 *                                            a ratio where the mapping is unmistakable;
 *
 *   **alpha survives**                       a source pixel with `a == 0` leaves the target
 *                                            untouched, one at `a == 255` writes its colour, and
 *                                            one in between blends — measured, not assumed;
 *
 *   **the order is CSS's**                   background, border, image: an image is a replaced
 *                                            element's content, so its record sits inside its own
 *                                            element's group and before that box's `CONTENT`;
 *
 *   **a missing image is a number**          a `src` that names nothing, a resource nothing
 *                                            resolved, bytes that failed to decode and a scheme
 *                                            this runtime refuses all paint **nothing** and are
 *                                            counted (`imageElements` against `images` and
 *                                            `imageSkipped`) rather than crashing or drawing a
 *                                            placeholder this runtime does not have;
 *
 *   **the artifacts are snapshots**           mutating the DOM, re-resolving the element and
 *                                            releasing the document's own reference afterwards
 *                                            cannot change a record or a command, and clearing
 *                                            the record list cannot change a pixel the display
 *                                            list draws;
 *
 *   **identical input, identical output**     two passes over one document produce equal lists,
 *                                            equal dumps (with **no address** in them) and equal
 *                                            pixels.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → records →
 * display list) and, where pixels are the question, paints it into a real RoboGraphics surface.
 * Hand-built records and commands appear only where the question is about the payload's own
 * contract — the validity rules, the constructors, the accessors — and never in place of a layout
 * result.
 *
 * The image bytes are produced by the real encoder (Goal 6.13) and decoded by the real decoder
 * (8.12), because that is what makes the pixel assertions meaningful: the suite knows the surface
 * it encoded, so it can compare the picture that came out of the document against the one that
 * went in. Nothing here is fetched, and no Chromium, Skia or Blink header, library or test
 * framework is involved.
 *
 * **What this suite leaves to the suites that own it.** The boundary (11.8), the strength (11.9)
 * and the inverse query (11.10) each apply at one place that does not branch on a drawing's kind
 * -- the decoration pass bounds and fades every command before the kind's own branch, and a hit
 * test's region comes from `PaintBoxesOf` rather than from a payload -- so a picture is
 * clipped, faded and *hit* by construction, and this suite asserts only what is specific to the
 * kind: the resource, the two rectangles, the sampler, the alpha and the missing-image policy.
 * Those three suites landed before this goal, so each one's header records the pixel case for an
 * image as *owed* rather than counting it as covered; the goal that closes them is a later pass
 * over `paint.*`, not a change to this release.
 */

#include "RoboPaintTestSupport.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLTag.h"

#include "RoboPaint/RoboPaintColor.h"
#include "RoboPaint/RoboPaintDecoration.h"
#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDump.h"

#include <string.h>

/*
 * The same base sheet the other paint suites use, with `img` added as a **block** -- because what
 * this build paints is the content box layout produced, and a replaced element with no CSS size
 * is zero-sized until layout does intrinsic sizing (which it does not, and
 * `paint/docs/Images.md` says so).
 */
#define IMAGE_BASE_CSS \
    "html, body, div, section, p, span, img { display: block }"

/* The document every pixel case uses: a box, a picture in it, sized by the sheet. */
#define IMAGE_DOCUMENT \
    "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Logo.png\"></div></body></html>"

#define IMAGE_WHITE GfxRgba8(0xff, 0xff, 0xff, 255)

/* ------------------------------------------------------------------ */
/* A resource, encoded and decoded for real                            */
/* ------------------------------------------------------------------ */

/**
 * A 4x4 surface with a fixed, non-uniform pattern.
 *
 * Non-uniform on purpose: a sampler that used one pixel for the whole image, or transposed the
 * axes, would pass a solid fixture and fail this one. The values are `x * 60 + 1`,
 * `y * 60 + 2`, `(x + y) * 30 + 3`, so no two pixels are equal and none is a round number.
 */
static GfxSurface *ImageLogoSurface(void)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    uint32_t x;
    uint32_t y;

    GfxSurfaceDescInit(&desc);
    desc.width = 4u;
    desc.height = 4u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            GfxColorRgba8 color;

            color.r = (uint8_t)(x * 60u + 1u);
            color.g = (uint8_t)(y * 60u + 2u);
            color.b = (uint8_t)((x + y) * 30u + 3u);
            color.a = 255u;
            RoboCheckEqInt(GfxSurfaceSetPixel(surface, x, y, color), RoboOk);
        }
    }
    return surface;
}

/** The colour of @p surface at @p x,@p y, asserted to be readable. */
static GfxColorRgba8 ImageSourcePixel(GfxSurface *surface,
                                                 uint32_t x,
                                                 uint32_t y)
{
    GfxColorRgba8 color;

    memset(&color, 0, sizeof(color));
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
    return color;
}

static void ImageColorEq(GfxColorRgba8 actual,
                           GfxColorRgba8 expected,
                           const char *what)
{
    if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
        actual.a != expected.a) {
        TestFail(__FILE__, __LINE__,
                       "%s: expected rgba(%u,%u,%u,%u), got rgba(%u,%u,%u,%u)", what,
                       (unsigned)expected.r, (unsigned)expected.g, (unsigned)expected.b,
                       (unsigned)expected.a, (unsigned)actual.r, (unsigned)actual.g,
                       (unsigned)actual.b, (unsigned)actual.a);
    }
}

/**
 * One image document's resources: the surface encoded, the bytes it encoded to, and the asset
 * table those bytes are registered in.
 *
 * The encoded buffer must outlive the table and the table must outlive every resolver, which is
 * the lifetime contract `RoboHTMLImage.h` documents -- so they are bundled here and torn down in
 * reverse order.
 */
typedef struct imageAssets {
    GfxSurface *surface;
    GfxEncodedBuffer encoded;
    HtmlImageAssets *assets;
} ImageAssets;

/** Encode @p surface to PNG and register it as `Assets/Logo.png` in a fresh table. */
static void ImageAssetsBegin(ImageAssets *assets, GfxSurface *surface)
{
    GfxEncodeOptions options;

    memset(assets, 0, sizeof(*assets));
    assets->surface = surface;
    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&assets->encoded, 0, sizeof(assets->encoded));
    RoboCheckEqInt(GfxSurfaceEncode(surface, &options, &assets->encoded), RoboOk);
    RoboCheck(assets->encoded.size > 0u);
    RoboCheckEqInt(HtmlImageAssetsCreate(NULL, 0u, &assets->assets), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(assets->assets, "Assets/Logo.png",
                                                 assets->encoded.data, assets->encoded.size,
                                                 NULL),
                      RoboOk);
}

static void ImageAssetsEnd(ImageAssets *assets)
{
    HtmlImageAssetsDestroy(assets->assets);
    assets->assets = NULL;
    GfxEncodedBufferRelease(&assets->encoded);
    GfxSurfaceRelease(assets->surface);
    assets->surface = NULL;
}

/** An empty table: every reference resolves to `NOT_FOUND`, so the resource state is FAILED. */
static void ImageAssetsEmpty(ImageAssets *assets)
{
    memset(assets, 0, sizeof(*assets));
    RoboCheckEqInt(HtmlImageAssetsCreate(NULL, 0u, &assets->assets), RoboOk);
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** The element with class @p name, asserted to be a live element. */
static HtmlNodeHandle ImageElement(PaintFixtureDoc *fixture, const char *name)
{
    HtmlNodeHandle element = PaintFind(fixture, name);

    RoboCheckEqInt((int)HtmlNodeHandleIsWellformed(&element), 1);
    return element;
}

/**
 * Build @p html, resolve the image element with class @p name through @p assets, then lay the
 * document out.
 *
 * The resolve happens **before** the layout pass, which is both the order a real document loads
 * in and the one that keeps the test honest: the resource is attached to the element before
 * anything reads a box, so nothing here can be relying on a paint-time mutation to be visible.
 * Passing NULL @p assets skips the resolve entirely, which is the "a `src` nobody fetched" case.
 */
static void ImageBegin(PaintFixtureDoc *fixture,
                        const char *html,
                        const char *css,
                        const char *name,
                        const ImageAssets *assets)
{
    PaintBegin(fixture, html, css);
    if (assets != NULL) {
        HtmlNodeHandle element = ImageElement(fixture, name);
        HtmlImageResolver resolver = HtmlImageAssetsResolver(assets->assets);

        RoboCheckEqInt(HtmlElementImageResolve(&element, &resolver, NULL), RoboOk);
    }
    PaintLayout(fixture, 800, 600);
}

/** A surface the paint pass can write into. */
static GfxSurface *ImageSurface(uint32_t width, uint32_t height)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    return surface;
}

/** Fill @p surface with one colour, so "nothing was drawn here" is a colour and not a zero. */
static void ImageFill(GfxSurface *surface, GfxColorRgba8 color)
{
    uint32_t x;
    uint32_t y;

    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            RoboCheckEqInt(GfxSurfaceSetPixel(surface, x, y, color), RoboOk);
        }
    }
}

/** Build a display list from @p records, asserting the build succeeds. */
static void ImageBuild(const PaintResult *records,
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
        TestFail(__FILE__, __LINE__, "the display build refused the records: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    PaintDisplayListBuilderDestroy(builder);
}

/** Paint @p list into @p target, asserting the pass succeeds. No font is needed for images. */
static void ImagePaint(const PaintDisplayList *list,
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

/** The @p nth command of kind @p kind, or 0 when there is none. */
static int ImageCommandOfKind(const PaintDisplayList *list,
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

/** The first image command, asserted to exist. */
static PaintDisplayCommand ImageRequireCommand(const PaintDisplayList *list)
{
    PaintDisplayCommand command;

    PaintDisplayCommandInit(&command);
    RoboCheckEqInt(ImageCommandOfKind(list, RoboPaintDisplayCmdDrawImage, 0u,
                                            &command),
                      1);
    return command;
}

/** The index of the command that draws @p node's image, or `(size_t)-1`. */
static size_t ImageIndexOf(const PaintDisplayList *list,
                             PaintDisplayCommandKind kind,
                             LayoutNodeId node)
{
    size_t index;

    for (index = 0u; index < PaintDisplayListCount(list); index++) {
        PaintDisplayCommand command;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            break;
        }
        if (command.kind == kind && command.node == node) {
            return index;
        }
    }
    return (size_t)-1;
}

/** The whole dump of @p list, asserted to fit @p buffer. */
static void ImageDumpRecords(const PaintList *list, char *buffer, size_t capacity)
{
    size_t length = 0u;
    Result status;

    memset(buffer, 0, capacity);
    status = PaintListWrite(list, buffer, capacity, &length, NULL);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the record dump did not fit %zu bytes", capacity);
    }
}

static void ImageDumpCommands(const PaintDisplayList *list,
                                char *buffer,
                                size_t capacity)
{
    size_t length = 0u;
    Result status;

    memset(buffer, 0, capacity);
    status = PaintDisplayListWrite(list, buffer, capacity, &length, NULL);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the command dump did not fit %zu bytes", capacity);
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * A resolved image produces exactly one record, and that record carries the resource's own facts.
 *
 * The identity is compared against the **resource's** own id rather than against a number written
 * here, because that is what makes the comparison a statement about the document: a suite that
 * hard-coded an id would pass for a payload that named a different picture.
 */
static void TestImageRecordShape(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintOperation operation;
    HtmlNodeHandle element;
    HtmlImage *resource = NULL;
    GfxImage *graphics;
    GfxSurface *surface = ImageLogoSurface();

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #3366cc }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    element = ImageElement(&fixture, "pic");
    RoboCheckEqInt(HtmlElementImageGet(&element, &resource, NULL), RoboOk);
    RoboCheck(resource != NULL);

    PaintOnce(&fixture, &result);

    /* The counters: an image element was reached, and it drew. */
    RoboCheckEqInt((int)result.stats.imageElements, 1);
    RoboCheckEqInt((int)result.stats.images, 1);
    RoboCheckEqInt((int)result.stats.imageSkipped, 0);
    RoboCheckEqInt((int)result.stats.imageElements,
                      (int)(result.stats.images + result.stats.imageSkipped));
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpImage), 1);

    operation = PaintRequire(&result, RoboPaintOpImage);

    /* The payload, read through the record's own door rather than off the field. */
    {
        PaintImage payload = PaintOperationImage(&operation);

        RoboCheck(PaintImageIsCleared(&payload) == 0);
        RoboCheck(PaintImageIsValid(&payload) == 1);
        RoboCheck(PaintImageIsFullSource(&payload) == 1);
        graphics = HtmlImageGraphics(resource);
        RoboCheck(graphics != NULL);
        RoboCheckEqInt((int)PaintImageIdentity(&payload),
                          (int)GetGfxImageId(graphics));
        RoboCheckEqInt((int)payload.width, 4);
        RoboCheckEqInt((int)payload.height, 4);
        RoboCheckEqInt((int)payload.format, (int)RoboGfxFormatRgba8);
        RoboCheckEqInt((int)payload.source.x, 0);
        RoboCheckEqInt((int)payload.source.y, 0);
        RoboCheckEqInt((int)payload.source.width, 4);
        RoboCheckEqInt((int)payload.source.height, 4);
        /* And the accessor's own rule: a kind that is not an image answers "no resource". */
        {
            PaintOperation content = PaintRequire(&result, RoboPaintOpContent);
            PaintImage none = PaintOperationImage(&content);

            RoboCheck(PaintImageIsCleared(&none) == 1);
        }
    }

    /* No colour and no style: an image's appearance is its resource's pixels. */
    RoboCheckEqInt((int)operation.color.kind, (int)RoboCssColorNone);
    RoboCheckEqInt((int)operation.style, (int)RoboCssBorderStyleNone);
    RoboCheck(operation.node == PaintBox(&fixture, "pic"));

    /* The boxes are layout's, through the assertion the box suites use. */
    PaintLocalBoxesMatchLayout(&fixture, operation);
    PaintRectIs(0, 0, 64, 48, operation.boxes.content, "the image's content box");

    PaintResultDispose(&result);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* The display command                                                 */
/* ------------------------------------------------------------------ */

/**
 * The record resolves to one `draw-image` command whose rectangle is the element's content box.
 *
 * This is the phase's integration claim in one case: **layout's** box becomes the record's
 * destination and the command's one rectangle, with the same resource named at both stages.
 */
static void TestImageCommandAndDestination(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    PaintResult again;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintOperation operation;
    PaintDisplayCommand command;
    LayoutNodeId box;
    GfxSurface *surface = ImageLogoSurface();

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; padding: 5px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);

    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);

    box = PaintBox(&fixture, "pic");
    operation = PaintRequire(&result, RoboPaintOpImage);

    RoboCheckEqInt((int)stats.images, 1);
    RoboCheckEqInt((int)stats.draws, (int)(stats.backgrounds + stats.borders + stats.texts +
                                              stats.images));
    RoboCheckEqInt((int)PaintDisplayListCountOfKind(
                          list, RoboPaintDisplayCmdDrawImage), 1);

    command = ImageRequireCommand(list);
    RoboCheck(command.node == box);
    /* The command's one rectangle **is** the record's content box -- the derivation this phase
     * adds and the reason a consumer never has to choose between three rectangles. */
    PaintRectIs(operation.boxes.content.x, operation.boxes.content.y,
                  operation.boxes.content.width, operation.boxes.content.height, command.rect,
                  "the command's destination");
    /* And the payload travelled: the same resource, by identity. */
    {
        PaintImage payload = PaintDisplayCommandImage(&command);
        PaintImage fromRecord = PaintOperationImage(&operation);

        RoboCheck(PaintImageEqual(&payload, &fromRecord) == 1);
        RoboCheck(PaintImageIsFullSource(&payload) == 1);
        RoboCheckEqInt((int)payload.source.width, 4);
        RoboCheckEqInt((int)payload.source.height, 4);
    }
    RoboCheckEqInt((int)PaintDisplayCommandIsDrawing(&command), 1);
    RoboCheckEqInt((int)PaintDisplayCommandIsStructural(&command), 0);

    /* The same document paints twice: same records, same commands. */
    PaintOnce(&fixture, &again);
    RoboCheckEqInt(PaintListEqual(result.list, again.list), 1);
    PaintResultDispose(&again);

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* Order                                                               */
/* ------------------------------------------------------------------ */

/**
 * Background, border, image -- and the image inside its own element's group.
 *
 * The two placements are checked separately because they are two different facts: an image record
 * is emitted **after** the box's decorations and **before** its `CONTENT`, and the image **is** a
 * drawing of that box rather than of anything inside it.
 */
static void TestImageOrder(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    LayoutNodeId box;
    LayoutNodeId pic;
    size_t background;
    size_t content;
    size_t image;
    size_t end;
    size_t border;
    size_t picBegin;
    size_t picContent;
    size_t picEnd;
    GfxSurface *surface = ImageLogoSurface();

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture,
                "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Logo.png\">"
                "</div></body></html>",
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #3366cc;"
                " border: 3px solid #111111 }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);

    box = PaintBox(&fixture, "box");
    pic = PaintBox(&fixture, "pic");

    background = ImageIndexOf(list, RoboPaintDisplayCmdDrawBackground, box);
    border = ImageIndexOf(list, RoboPaintDisplayCmdDrawBorder, box);
    content = ImageIndexOf(list, RoboPaintDisplayCmdContent, box);
    end = ImageIndexOf(list, RoboPaintDisplayCmdEndNode, box);
    image = ImageIndexOf(list, RoboPaintDisplayCmdDrawImage, pic);
    picBegin = ImageIndexOf(list, RoboPaintDisplayCmdBeginNode, pic);
    picContent = ImageIndexOf(list, RoboPaintDisplayCmdContent, pic);
    picEnd = ImageIndexOf(list, RoboPaintDisplayCmdEndNode, pic);

    RoboCheck(background != (size_t)-1);
    RoboCheck(border != (size_t)-1);
    RoboCheck(content != (size_t)-1);
    RoboCheck(end != (size_t)-1);
    RoboCheck(image != (size_t)-1);
    RoboCheck(picBegin != (size_t)-1);
    RoboCheck(picContent != (size_t)-1);
    RoboCheck(picEnd != (size_t)-1);

    /*
     * CSS's paint order, in two statements that are two different facts. The container's own
     * background and border come first, before anything its contents draw -- and inside the
     * image's **own** element's group the picture is drawn after that box's own records and
     * before its content boundary, because an image is its element's content rather than
     * something inside it. A `draw-text` is placed the other way round (a level below its box),
     * so this pair of inequalities is what distinguishes the two drawing kinds in a stream.
     */
    RoboCheck(background < border);
    RoboCheck(border < image);
    RoboCheck(picBegin < image);
    RoboCheck(image < picContent);
    RoboCheck(picContent < picEnd);
    /* And the image's box is a child of the container, so its whole group is inside the
     * container's own `[content .. end]` window. */
    RoboCheck(content < picBegin);
    RoboCheck(picEnd < end);

    /* And the image names its **own** element's box, one level below the container. */
    {
        PaintDisplayCommand command = ImageRequireCommand(list);
        PaintDisplayCommand container;

        RoboCheck(command.node == pic);
        PaintDisplayCommandInit(&container);
        /* The first `begin-node` is the root's, and every level of the document adds one: the
         * image's box is a child of the container, which is why the record carries a depth its
         * own element's box owns rather than its parent's. */
        RoboCheckEqInt(ImageCommandOfKind(list, RoboPaintDisplayCmdBeginNode, 0u,
                                                &container),
                          1);
        RoboCheck(command.depth > container.depth);
    }

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/**
 * Two images, two resources, two destinations -- and each command names its own.
 *
 * The second asset is a **different surface** encoded under a second name, so a builder that
 * resolved either reference to the first resource would be caught by the identity comparison
 * rather than by a pixel count.
 */
static void TestImageMultiple(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    GfxSurfaceDesc desc;
    GfxSurface *second = NULL;
    GfxEncodedBuffer encoded;
    GfxEncodeOptions options;
    PaintOperation first;
    PaintOperation other;
    GfxSurface *surface = ImageLogoSurface();

    ImageAssetsBegin(&assets, surface);
    /* A 2x2 surface, encoded under a second name. */
    GfxSurfaceDescInit(&desc);
    desc.width = 2u;
    desc.height = 2u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &second), RoboOk);
    ImageFill(second, GfxRgba8(0x10, 0x20, 0x30, 0xff));
    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&encoded, 0, sizeof(encoded));
    RoboCheckEqInt(GfxSurfaceEncode(second, &options, &encoded), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(assets.assets, "Assets/Small.png",
                                                 encoded.data, encoded.size, NULL),
                      RoboOk);

    PaintBegin(&fixture,
                "<html><body><div class=\"box\">"
                "<img class=\"pic\" src=\"Assets/Logo.png\">"
                "<img class=\"pic2\" src=\"Assets/Small.png\">"
                "</div></body></html>",
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }"
                " .pic2 { width: 8px; height: 8px }");
    {
        HtmlImageResolver resolver = HtmlImageAssetsResolver(assets.assets);
        HtmlNodeHandle firstElement = ImageElement(&fixture, "pic");
        HtmlNodeHandle secondElement = ImageElement(&fixture, "pic2");

        RoboCheckEqInt(HtmlElementImageResolve(&firstElement, &resolver, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementImageResolve(&secondElement, &resolver, NULL),
                          RoboOk);
    }
    PaintLayout(&fixture, 800, 600);
    PaintOnce(&fixture, &result);

    RoboCheckEqInt((int)result.stats.imageElements, 2);
    RoboCheckEqInt((int)result.stats.images, 2);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpImage), 2);

    first = PaintRequire(&result, RoboPaintOpImage);
    RoboCheckEqInt(PaintNthOfKind(&result, RoboPaintOpImage, 1u, &other), 1);
    RoboCheck(first.node == PaintBox(&fixture, "pic"));
    RoboCheck(other.node == PaintBox(&fixture, "pic2"));
    RoboCheck(PaintOperationImage(&first).identity !=
               PaintOperationImage(&other).identity);
    RoboCheckEqInt((int)PaintOperationImage(&first).width, 4);
    RoboCheckEqInt((int)PaintOperationImage(&other).width, 2);
    /* Two block-level pictures in flow order: the first at the container's content top, the
     * second immediately below it. The geometry is layout's, and this is the assertion that says
     * the destination really is **each element's own** content box rather than the container's. */
    PaintRectIs(0, 0, 64, 48, first.boxes.content, "the first image's content box");
    PaintRectIs(0, 48, 8, 8, other.boxes.content, "the second image's content box");

    PaintResultDispose(&result);
    PaintEnd(&fixture);
    GfxEncodedBufferRelease(&encoded);
    GfxSurfaceRelease(second);
    ImageAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* Pixels                                                              */
/* ------------------------------------------------------------------ */

/**
 * The visual smoke test: the document's picture reaches the surface.
 *
 * The destination is read off the **command** rather than written down here, so the case proves
 * the rectangle the pass used is the one the command states -- and the pixels inside it are the
 * source's own, scaled by the graphics module's nearest-neighbour mapping, while everything
 * outside it is still the page's colour.
 */
static void TestImagePixels(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    PaintDisplayCommand command;
    GfxSurface *surface = ImageLogoSurface();
    GfxSurface *target;
    uint32_t outside = 0u;
    uint32_t inside = 0u;
    uint32_t x;
    uint32_t y;

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #ffffff }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);

    command = ImageRequireCommand(list);
    target = ImageSurface(200u, 200u);
    ImageFill(target, IMAGE_WHITE);

    memset(&decoration, 0, sizeof(decoration));
    ImagePaint(list, target, &decoration);

    RoboCheckEqInt((int)decoration.images, 1);
    RoboCheckEqInt((int)decoration.noResource, 0);
    RoboCheckEqInt((int)decoration.commands,
                      (int)(decoration.draws + decoration.skipped));
    RoboCheckEqInt((int)decoration.draws,
                      (int)(decoration.backgrounds + decoration.borders + decoration.texts +
                            decoration.images));
    RoboCheckEqInt((int)decoration.draws,
                      (int)(decoration.filled + decoration.contextual + decoration.transparent +
                            decoration.empty + decoration.noFont + decoration.noResource));
    /* The image counts as a drawing that put pixels down -- the reason `images` is a subset of
     * `filled` rather than a term of the partition. */
    RoboCheck(decoration.images <= decoration.filled);

    /* Every pixel of the destination is the source's own colour, and every pixel outside the
     * page's. */
    for (y = 0u; y < 200u; y++) {
        for (x = 0u; x < 200u; x++) {
            GfxColorRgba8 got;
            int in = (int32_t)x >= command.rect.x && (int32_t)x < command.rect.x + command.rect.width &&
                     (int32_t)y >= command.rect.y && (int32_t)y < command.rect.y + command.rect.height;

            RoboCheckEqInt(GfxSurfaceGetPixel(target, x, y, &got), RoboOk);
            if (in) {
                uint32_t sx = (uint32_t)(((int32_t)x - command.rect.x) * 4) / (uint32_t)command.rect.width;
                uint32_t sy = (uint32_t)(((int32_t)y - command.rect.y) * 4) / (uint32_t)command.rect.height;
                GfxColorRgba8 want = ImageSourcePixel(surface, sx, sy);

                ImageColorEq(got, want, "an image pixel");
                inside++;
            } else {
                ImageColorEq(got, IMAGE_WHITE, "a pixel outside the image");
                outside++;
            }
        }
    }
    RoboCheckEqInt((int)inside, 64 * 48);
    RoboCheck(outside > 0u);

    GfxSurfaceRelease(target);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/**
 * Scaling: a 4x4 source into a 16x16 destination, one source pixel per four-by-four block.
 *
 * The ratio is chosen so the mapping is unambiguous: block `(bx, by)` of the destination must be
 * the source pixel `(bx, by)`, so a sampler that transposed the axes or rounded the wrong way
 * fails on the first block it reaches.
 */
static void TestImageScaling(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand command;
    GfxSurface *surface = ImageLogoSurface();
    GfxSurface *target;
    uint32_t bx;
    uint32_t by;

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 100px; height: 100px }"
                " .pic { width: 16px; height: 16px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);

    command = ImageRequireCommand(list);
    PaintRectIs(0, 0, 16, 16, command.rect, "the scaled destination");
    target = ImageSurface(16u, 16u);
    ImageFill(target, IMAGE_WHITE);
    ImagePaint(list, target, NULL);

    for (by = 0u; by < 4u; by++) {
        for (bx = 0u; bx < 4u; bx++) {
            GfxColorRgba8 want = ImageSourcePixel(surface, bx, by);

            /* Every pixel of the block, because a nearest-neighbour sampler has no interior to
             * interpolate and an off-by-one in the mapping shows up at the block's last column
             * and row. */
            ImageColorEq(ImageSourcePixel(target, bx * 4u, by * 4u), want,
                           "a block's first pixel");
            ImageColorEq(ImageSourcePixel(target, bx * 4u + 3u, by * 4u + 3u), want,
                           "a block's last pixel");
        }
    }

    GfxSurfaceRelease(target);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/**
 * Alpha: a transparent source pixel writes nothing, an opaque one writes its colour, and a
 * half-transparent one blends.
 *
 * The three source pixels are in **one** image, over one opaque white destination, so the three
 * answers come from one draw and cannot be explained by a pass that skipped the image.
 */
static void TestImageAlpha(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    GfxSurfaceDesc desc;
    GfxSurface *source = NULL;
    GfxEncodedBuffer encoded;
    GfxEncodeOptions options;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand command;
    GfxSurface *target;
    GfxColorRgba8 got;
    GfxColorRgba8 opaque = GfxRgba8(0xff, 0x00, 0x00, 255);
    GfxColorRgba8 clear = GfxRgba8(0x00, 0xff, 0x00, 0);
    GfxColorRgba8 half = GfxRgba8(0x00, 0x00, 0xff, 128);

    /* A 3x1 image: transparent, opaque, half. */
    GfxSurfaceDescInit(&desc);
    desc.width = 3u;
    desc.height = 1u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &source), RoboOk);
    RoboCheckEqInt(GfxSurfaceSetPixel(source, 0u, 0u, clear), RoboOk);
    RoboCheckEqInt(GfxSurfaceSetPixel(source, 1u, 0u, opaque), RoboOk);
    RoboCheckEqInt(GfxSurfaceSetPixel(source, 2u, 0u, half), RoboOk);

    ImageAssetsBegin(&assets, source);
    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&encoded, 0, sizeof(encoded));
    RoboCheckEqInt(GfxSurfaceEncode(source, &options, &encoded), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(assets.assets, "Assets/Alpha.png",
                                                 encoded.data, encoded.size, NULL),
                      RoboOk);

    ImageBegin(&fixture,
                "<html><body><div class=\"box\">"
                "<img class=\"pic\" src=\"Assets/Alpha.png\"></div></body></html>",
                IMAGE_BASE_CSS
                " .box { width: 100px; height: 100px }"
                " .pic { width: 3px; height: 1px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.images, 1);
    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);

    command = ImageRequireCommand(list);
    PaintRectIs(0, 0, 3, 1, command.rect, "the alpha fixture's destination");
    target = ImageSurface(3u, 1u);
    ImageFill(target, IMAGE_WHITE);
    ImagePaint(list, target, NULL);

    /* Transparent: the destination is exactly what it was. */
    ImageColorEq(ImageSourcePixel(target, 0u, 0u), IMAGE_WHITE, "a transparent source pixel");
    /* Opaque: the source's own colour, alpha and all. */
    ImageColorEq(ImageSourcePixel(target, 1u, 0u), opaque, "an opaque source pixel");
    /* Half: between the two, and neither endpoint -- which is the whole of what alpha means. */
    got = ImageSourcePixel(target, 2u, 0u);
    RoboCheck(got.r > 0u && got.r < 255u);
    RoboCheck(got.g > 0u && got.g < 255u);
    RoboCheck(got.b == 255u);

    GfxSurfaceRelease(target);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    GfxEncodedBufferRelease(&encoded);
    ImageAssetsEnd(&assets);
}

/**
 * A destination with no area is a counted drawing rather than a failure.
 *
 * `width: 0` is a document a browser paints nothing for and this build must too -- and the two
 * counters that describe it are different facts: the traversal emitted a record (the resource is
 * fine), and the pass counted it as `empty` (there is nowhere to put it). Nothing crashes, and
 * the record and command both exist.
 */
static void TestImageEmptyDestination(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDecorationStats decoration;
    GfxSurface *surface = ImageLogoSurface();
    GfxSurface *target;

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 50px; height: 50px }"
                " .pic { width: 0px; height: 0px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);

    RoboCheckEqInt((int)result.stats.imageElements, 1);
    RoboCheckEqInt((int)result.stats.images, 1);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpImage), 1);

    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);
    RoboCheckEqInt((int)stats.images, 1);

    target = ImageSurface(8u, 8u);
    ImageFill(target, IMAGE_WHITE);
    memset(&decoration, 0, sizeof(decoration));
    ImagePaint(list, target, &decoration);
    RoboCheckEqInt((int)decoration.images, 1);
    RoboCheckEqInt((int)decoration.empty, 1);
    RoboCheckEqInt((int)decoration.filled, 0);
    /* Nothing was drawn: the surface is still the page colour. */
    ImageColorEq(ImageSourcePixel(target, 0u, 0u), IMAGE_WHITE, "an empty destination");

    GfxSurfaceRelease(target);
    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* Missing images                                                      */
/* ------------------------------------------------------------------ */

/**
 * Every way an image can be absent is a **number**, not a crash and not a placeholder.
 *
 * Four documents, four reasons, one answer each: a `src` attribute nobody wrote, a `src` nothing
 * resolved, a reference the resolver could not find, and a scheme this runtime refuses. Each
 * leaves the element's resource in a state that is legitimate, each produces **no** record, and
 * each is counted -- so a reader can tell a document with no images from one whose images drew
 * nothing.
 */
static void TestImageMissing(void)
{
    static const struct {
        const char *what;
        const char *html;
        int resolve;
    } cases[] = {
        { "no source",
          "<html><body><div class=\"box\"><img class=\"pic\" alt=\"nothing\"></div></body></html>",
          0 },
        { "never resolved",
          "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Logo.png\"></div>"
          "</body></html>",
          0 },
        { "not found",
          "<html><body><div class=\"box\"><img class=\"pic\" src=\"Assets/Missing.png\"></div>"
          "</body></html>",
          1 },
        { "refused scheme",
          "<html><body><div class=\"box\"><img class=\"pic\" "
          "src=\"https://example.com/Logo.png\"></div></body></html>",
          1 }
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PaintFixtureDoc fixture;
        PaintResult result;
        ImageAssets assets;
        HtmlNodeHandle element;

        ImageAssetsEmpty(&assets);
        ImageBegin(&fixture, cases[index].html,
                    IMAGE_BASE_CSS
                    " .box { width: 200px; height: 100px; background-color: #3366cc }"
                    " .pic { width: 64px; height: 48px }",
                    "pic", cases[index].resolve ? &assets : NULL);

        /* The resource state said so before the paint pass ever ran. */
        element = ImageElement(&fixture, "pic");
        if (strcmp(cases[index].what, "no source") == 0) {
            RoboCheckEqInt((int)HtmlElementImageState(&element),
                              (int)RoboHtmlImageStateNone);
        } else if (cases[index].resolve) {
            HtmlImageState state =
                HtmlElementImageState(&element);

            RoboCheck(state == (strcmp(cases[index].what, "not found") == 0
                                     ? RoboHtmlImageStateFailed
                                     : RoboHtmlImageStateUnsupported));
        } else {
            RoboCheckEqInt((int)HtmlElementImageState(&element),
                              (int)RoboHtmlImageStatePending);
        }

        PaintOnce(&fixture, &result);
        /* Every one of the four reasons is still an **image element**: the traversal reached it
         * and counted it, which is what tells a reader the document asked for a picture. */
        RoboCheckEqInt((int)result.stats.imageElements, 1);
        RoboCheckEqInt((int)result.stats.images, 0);
        RoboCheckEqInt((int)result.stats.imageSkipped, 1);
        RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpImage), 0);
        /* The box's own background still drew, so the document was painted rather than skipped. */
        RoboCheckEqInt((int)result.stats.backgrounds, 1);

        PaintResultDispose(&result);
        PaintEnd(&fixture);
        ImageAssetsEnd(&assets);
    }
}

/**
 * A document with no `<img>` at all leaves all three counters at zero.
 *
 * The other half of the pair above, and the reason `imageElements` exists: without it, "the
 * document has no images" and "every image failed" would be the same silence.
 */
static void TestImageNone(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;

    PaintBeginLaidOut(&fixture,
                         "<html><body><div class=\"box\">nothing to see</div></body></html>",
                         IMAGE_BASE_CSS " .box { width: 100px; height: 40px }");
    PaintOnce(&fixture, &result);
    RoboCheckEqInt((int)result.stats.imageElements, 0);
    RoboCheckEqInt((int)result.stats.images, 0);
    RoboCheckEqInt((int)result.stats.imageSkipped, 0);
    RoboCheckEqInt((int)PaintCountOf(&result, RoboPaintOpImage), 0);
    RoboCheck(result.stats.boxes > 0u);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The payload's own contract                                          */
/* ------------------------------------------------------------------ */

/**
 * The constructors, the predicates and the validity rules, on hand-built values.
 *
 * The only case here that does not go through a document, because the question is about the
 * **payload's** contract rather than about a traversal: what `PaintImageFromResource`
 * refuses, what a record requires of its payload, and what the accessors answer for a kind that is
 * not an image.
 */
static void TestImageHandBuiltRules(void)
{
    GfxSurfaceDesc surfaceDesc;
    GfxImageDesc imageDesc;
    GfxSurface *surface = NULL;
    GfxImage *raster = NULL;
    GfxImage *metadata = NULL;
    PaintImage payload;
    PaintImage copy;
    PaintImage other;
    PaintOperation operation;
    PaintDiag diag;

    /* A resource with pixels, and one without: an `EMPTY` image is a legitimate RoboGraphics
     * object and an impossible drawing. */
    GfxSurfaceDescInit(&surfaceDesc);
    surfaceDesc.width = 2u;
    surfaceDesc.height = 3u;
    surfaceDesc.format = RoboGfxFormatRgba8;
    surfaceDesc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&surfaceDesc, &surface), RoboOk);
    RoboCheckEqInt(GfxImageCreateFromSurface(surface, &raster), RoboOk);
    GfxSurfaceRelease(surface);
    GfxImageDescInit(&imageDesc);
    imageDesc.width = 2u;
    imageDesc.height = 3u;
    imageDesc.format = RoboGfxFormatRgba8;
    imageDesc.kind = RoboGfxImageKindEmpty;
    RoboCheckEqInt(GfxImageCreateEmpty(&imageDesc, &metadata), RoboOk);

    /* A cleared payload: names nothing, is not valid, and every door answers safely. */
    PaintImageInit(&payload);
    RoboCheckEqInt(PaintImageIsCleared(&payload), 1);
    RoboCheckEqInt(PaintImageIsValid(&payload), 0);
    RoboCheckEqInt(PaintImageIsFullSource(&payload), 0);
    RoboCheckEqInt((int)PaintImageIdentity(&payload), 0);
    RoboCheckEqInt(PaintImageIsCleared(NULL), 1);
    RoboCheckEqInt(PaintImageIsValid(NULL), 0);
    RoboCheckEqInt(PaintImageIsFullSource(NULL), 0);

    /* The constructor's refusals, then its answer. */
    RoboCheckEqInt(PaintImageFromResource(NULL, &payload, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(PaintImageFromResource(raster, NULL, NULL),
                      RoboInvalidArgument);
    PaintDiagReset(&diag);
    RoboCheckEqInt(PaintImageFromResource(metadata, &payload, &diag),
                      RoboNotSupported);
    RoboCheckEqInt(PaintImageFromResource(raster, &payload, &diag), RoboOk);

    /* What it filled: the resource's own facts, and the full source. */
    RoboCheckEqInt(PaintImageIsValid(&payload), 1);
    RoboCheckEqInt(PaintImageIsFullSource(&payload), 1);
    RoboCheckEqInt((int)payload.width, 2);
    RoboCheckEqInt((int)payload.height, 3);
    RoboCheckEqInt((int)payload.source.width, 2);
    RoboCheckEqInt((int)payload.source.height, 3);

    /* Equality is by identity and by rectangle, and a record's copy is equal to its original. */
    copy = payload;
    RoboCheckEqInt(PaintImageEqual(&payload, &copy), 1);
    other = payload;
    other.identity = payload.identity + 1u;
    RoboCheckEqInt(PaintImageEqual(&payload, &other), 0);
    other = payload;
    other.source.width = 1;
    RoboCheckEqInt(PaintImageEqual(&payload, &other), 0);
    RoboCheckEqInt(PaintImageEqual(NULL, NULL), 1);

    /* A crop that runs off the resource is refused, and a full source that no longer matches the
     * dimensions is neither full nor valid. */
    other = payload;
    other.source = MakeGfxRect(1, 0, 2, 1);
    RoboCheckEqInt(PaintImageIsValid(&other), 0);
    other = payload;
    other.source = MakeGfxRect(0, 0, 1, 1);
    RoboCheckEqInt(PaintImageIsValid(&other), 1);
    RoboCheckEqInt(PaintImageIsFullSource(&other), 0);
    other = payload;
    other.source = MakeGfxRect(0, 0, 0, 0);
    RoboCheckEqInt(PaintImageIsValid(&other), 0);

    /*
     * An `IMAGE` record requires a payload that draws, no colour and no style. The boxes are set
     * by hand as three nested, non-negative rectangles -- a record with un-nested boxes is refused
     * before its payload is ever read, which would make every assertion below pass for the wrong
     * reason.
     */
    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpImage;
    operation.node = 1u;
    operation.boxes.border = MakeGfxRect(0, 0, 10, 10);
    operation.boxes.padding = MakeGfxRect(1, 1, 8, 8);
    operation.boxes.content = MakeGfxRect(2, 2, 6, 6);
    operation.boxes.insetsMeasured = 1;
    RoboCheckEqInt(PaintBoxesIsValid(&operation.boxes), 1);
    operation.origin = MakeGfxPoint(0, 0);
    PaintClipInit(&operation.clip);
    PaintOpacityInit(&operation.opacity);
    operation.color = CssColorNone();
    operation.style = RoboCssBorderStyleNone;
    operation.image = payload;
    RoboCheckEqInt(PaintOperationIsValid(&operation), 1);
    {
        PaintImage read = PaintOperationImage(&operation);

        RoboCheck(PaintImageIsCleared(&read) == 0);
        RoboCheck(PaintImageEqual(&read, &payload) == 1);
    }

    /* Cleared payload: refused. */
    operation.image = copy;
    PaintImageInit(&operation.image);
    RoboCheckEqInt(PaintOperationIsValid(&operation), 0);
    /* A colour on an image record: refused, because its appearance is not a colour. */
    operation.image = payload;
    operation.color = CssColorMake(0xffu, 0x00u, 0x00u, 0xffu);
    RoboCheckEqInt(PaintOperationIsValid(&operation), 0);
    /* A style on an image record: refused, for the same reason one field along. */
    operation.color = CssColorNone();
    operation.style = RoboCssBorderStyleSolid;
    RoboCheckEqInt(PaintOperationIsValid(&operation), 0);
    /* A background carrying a payload: refused from the other side. */
    operation.style = RoboCssBorderStyleNone;
    operation.kind = RoboPaintOpBackground;
    RoboCheckEqInt(PaintOperationIsValid(&operation), 0);
    operation.kind = RoboPaintOpImage;
    operation.image = payload;
    RoboCheckEqInt(PaintOperationIsValid(&operation), 1);

    /* The vocabularies: an image draws, and it is not a kind whose look is a colour. */
    RoboCheckEqInt(PaintOpKindDraws(RoboPaintOpImage), 1);
    RoboCheckEqInt(PaintOpKindUsesColor(RoboPaintOpImage), 0);
    RoboCheckEqInt(PaintOpKindIsStructural(RoboPaintOpImage), 0);
    RoboCheckEqInt(PaintOpKindIsBorder(RoboPaintOpImage), 0);
    RoboCheck(strcmp(PaintOpKindName(RoboPaintOpImage), "image") == 0);
    RoboCheckEqInt(PaintDisplayCommandKindDraws(
                          RoboPaintDisplayCmdDrawImage),
                      1);
    RoboCheckEqInt(PaintDisplayCommandKindUsesColor(
                          RoboPaintDisplayCmdDrawImage),
                      0);
    RoboCheck(strcmp(PaintDisplayCommandKindName(RoboPaintDisplayCmdDrawImage),
                      "draw-image") == 0);

    /* And the display command's own rule, from both ends. */
    {
        PaintDisplayCommand command;

        PaintDisplayCommandInit(&command);
        command.kind = RoboPaintDisplayCmdDrawImage;
        command.node = 1u;
        command.rect = MakeGfxRect(0, 0, 10, 10);
        PaintClipInit(&command.clip);
        PaintOpacityInit(&command.opacity);
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 0);
        PaintImage read;

        command.image = payload;
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 1);
        read = PaintDisplayCommandImage(&command);
        RoboCheck(PaintImageEqual(&read, &payload) == 1);
        command.color = CssColorMake(0x00u, 0x00u, 0xffu, 0xffu);
        RoboCheckEqInt(PaintDisplayCommandIsValid(&command), 0);
        /* The accessor's rule for a kind that is not `draw-image`. */
        command.kind = RoboPaintDisplayCmdContent;
        command.color = CssColorNone();
        read = PaintDisplayCommandImage(&command);
        RoboCheck(PaintImageIsCleared(&read) == 1);
        RoboCheck(PaintImageEqual(&read, &payload) == 0);
    }

    GfxImageRelease(raster);
    GfxImageRelease(metadata);
}

/* ------------------------------------------------------------------ */
/* Ownership and isolation                                             */
/* ------------------------------------------------------------------ */

/**
 * One whole ownership cycle: build a picture, paint it, let the document go, destroy the records,
 * paint again, tear everything down. Returns the live block count **after** the teardown.
 *
 * The cycle exists as a function so the case below can run it twice and compare the two ends: a
 * single "back to the baseline" assertion would be measuring the harness as much as the module, and
 * what the phase actually owes is that repeating a cycle does not grow the heap. Everything the
 * module owns -- the encoded bytes, the decoded resource, the asset table, the record list, the
 * display list, the surfaces -- is created and released inside one call, so a missing release shows
 * up as a difference between two identical cycles.
 */
static size_t ImageLifetimeCycle(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintDisplayCommand command;
    HtmlNodeHandle element;
    GfxSurface *surface = ImageLogoSurface();
    GfxSurface *target;
    GfxColorRgba8 expected;

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);
    command = ImageRequireCommand(list);

    target = ImageSurface(64u, 48u);
    ImageFill(target, IMAGE_WHITE);
    ImagePaint(list, target, NULL);
    expected = ImageSourcePixel(target, 0u, 0u);
    {
        GfxColorRgba8 want = ImageSourcePixel(surface, 0u, 0u);

        ImageColorEq(expected, want, "the first paint");
    }

    /*
     * The document lets its reference go -- and the two lists keep theirs. This is the whole of
     * the phase's ownership rule in three lines: `detach` releases what the element (and so the
     * DOM) owed, and what is still drawing afterwards is the reference the containers took.
     */
    element = ImageElement(&fixture, "pic");
    RoboCheckEqInt(HtmlElementImageDetach(&element, NULL), RoboOk);
    RoboCheckEqInt((int)HtmlElementImageState(&element),
                      (int)RoboHtmlImageStatePending);

    /* The record list is destroyed: the display list is not a view of it. */
    PaintResultDispose(&result);
    {
        GfxSurface *again = ImageSurface(64u, 48u);

        ImageFill(again, IMAGE_WHITE);
        ImagePaint(list, again, NULL);
        ImageColorEq(ImageSourcePixel(again, 0u, 0u), expected,
                       "the same pixel after the DOM and the records were done with it");
        GfxSurfaceRelease(again);
    }
    RoboCheckEqInt((int)PaintDisplayCommandIsValid(&command), 1);

    GfxSurfaceRelease(target);
    PaintDisplayListDestroy(list);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
    return PaintLiveBlocks();
}

/**
 * The resources a record list and a display list hold outlive the document that named them, and
 * repeating the whole cycle does not grow the heap.
 *
 * Two claims in one case because they are one question asked from two sides: the first cycle
 * proves the references are real (the picture is still drawn after the DOM released it and the
 * records were destroyed), and the comparison of the two ends proves they are **given back** -- a
 * container that retained without releasing would pass the first and fail the second.
 */
static void TestImageResourceLifetime(void)
{
    size_t first;
    size_t second;

    /* Bring the modules up before the first cycle, so neither count includes a one-off
     * initialisation that only the first cycle would pay. */
    PaintFixture();
    first = ImageLifetimeCycle();
    second = ImageLifetimeCycle();
    RoboCheckEqInt((int)second, (int)first);
}

/**
 * Mutating the document cannot change a record or a command.
 *
 * After the pass, the element's `src` is rewritten and the picture re-resolved to a **different**
 * image, and the payload the record and the command carry is still the first one -- compared by
 * identity, which is the answer a suite can check without knowing an address.
 */
static void TestImageMutationIsolation(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintOperation operation;
    PaintDisplayCommand command;
    HtmlNodeHandle element;
    GfxImageId before;
    GfxSurfaceDesc desc;
    GfxSurface *otherSurface = NULL;
    GfxEncodedBuffer encoded;
    GfxEncodeOptions options;
    GfxSurface *surface = ImageLogoSurface();

    ImageAssetsBegin(&assets, surface);
    /* A second asset with **different dimensions**, so a re-resolve is unmistakable. */
    GfxSurfaceDescInit(&desc);
    desc.width = 8u;
    desc.height = 8u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &otherSurface), RoboOk);
    ImageFill(otherSurface, GfxRgba8(0x44, 0x55, 0x66, 0xff));
    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&encoded, 0, sizeof(encoded));
    RoboCheckEqInt(GfxSurfaceEncode(otherSurface, &options, &encoded), RoboOk);
    RoboCheckEqInt(HtmlImageAssetsAdd(assets.assets, "Assets/Other.png",
                                                 encoded.data, encoded.size, NULL),
                      RoboOk);

    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);
    command = ImageRequireCommand(list);
    operation = PaintRequire(&result, RoboPaintOpImage);
    {
        PaintImage payload = PaintOperationImage(&operation);

        before = PaintImageIdentity(&payload);
    }

    /* Mutate the document and re-resolve it to a different picture. */
    element = ImageElement(&fixture, "pic");
    RoboCheckEqInt(HtmlElementSetAttribute(&element, "src", "Assets/Other.png", NULL),
                      RoboOk);
    {
        HtmlImageResolver resolver = HtmlImageAssetsResolver(assets.assets);

        RoboCheckEqInt(HtmlElementImageResolve(&element, &resolver, NULL), RoboOk);
    }
    {
        HtmlImage *now = NULL;
        uint32_t width = 0u;
        uint32_t height = 0u;

        RoboCheckEqInt(HtmlElementImageGet(&element, &now, NULL), RoboOk);
        RoboCheck(now != NULL);
        if (now) {
            /* The element really did change picture: 8x8, and a different resource. */
            RoboCheckEqInt(HtmlImageIntrinsicSize(now, &width, &height), 1);
            RoboCheckEqInt((int)width, 8);
            RoboCheckEqInt((int)height, 8);
            RoboCheck(GetGfxImageId(HtmlImageGraphics(now)) != before);
        }
    }

    /* The record and the command still name the first picture, and still say 4x4. */
    {
        PaintImage fromRecord = PaintOperationImage(&operation);
        PaintImage fromCommand = PaintDisplayCommandImage(&command);

        RoboCheck(PaintImageIdentity(&fromRecord) == before);
        RoboCheck(PaintImageIdentity(&fromCommand) == before);
        RoboCheckEqInt((int)fromCommand.width, 4);
        RoboCheckEqInt((int)fromCommand.height, 4);
    }

    /* And a list rebuilt from the same records is equal to the one built before the mutation. */
    {
        PaintDisplayList *again = NULL;
        PaintDisplayListStats againStats;

        memset(&againStats, 0, sizeof(againStats));
        ImageBuild(&result, &again, &againStats);
        RoboCheckEqInt(PaintDisplayListEqual(list, again), 1);
        PaintDisplayListDestroy(again);
    }

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    GfxEncodedBufferRelease(&encoded);
    GfxSurfaceRelease(otherSurface);
    ImageAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

/**
 * One document, two passes: equal records, equal commands, equal dumps, equal pixels.
 *
 * The display list is built from **each** pass's records and compared, which is the stronger
 * statement: a builder that read anything mutable would agree with itself here but not with the
 * other list. The dump is compared too, and asserted to contain no address -- the one thing in
 * this module that would differ between two runs of one document.
 */
static void TestImageDeterminism(void)
{
    PaintFixtureDoc fixture;
    PaintResult first;
    PaintResult second;
    ImageAssets assets;
    PaintDisplayList *listA = NULL;
    PaintDisplayList *listB = NULL;
    PaintDisplayListStats statsA;
    PaintDisplayListStats statsB;
    GfxSurface *surface = ImageLogoSurface();
    char dumpA[4096];
    char dumpB[4096];
    char commandsA[4096];
    char commandsB[4096];

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px; background-color: #ffffff }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    PaintOnce(&fixture, &first);
    PaintOnce(&fixture, &second);

    RoboCheckEqInt(PaintListEqual(first.list, second.list), 1);
    RoboCheckEqInt((int)first.stats.images, (int)second.stats.images);
    ImageDumpRecords(first.list, dumpA, sizeof(dumpA));
    ImageDumpRecords(second.list, dumpB, sizeof(dumpB));
    RoboCheckStrEq(dumpA, dumpB);
    RoboCheck(strstr(dumpA, "image") != NULL);
    /* **No address**: a dump with a pointer in it would differ between two runs of one
     * document, which is the property the whole module is built to have. */
    RoboCheck(strstr(dumpA, "0x") == NULL);

    memset(&statsA, 0, sizeof(statsA));
    memset(&statsB, 0, sizeof(statsB));
    ImageBuild(&first, &listA, &statsA);
    ImageBuild(&second, &listB, &statsB);
    RoboCheckEqInt(PaintDisplayListEqual(listA, listB), 1);
    RoboCheckEqInt((int)statsA.images, (int)statsB.images);
    ImageDumpCommands(listA, commandsA, sizeof(commandsA));
    ImageDumpCommands(listB, commandsB, sizeof(commandsB));
    RoboCheckStrEq(commandsA, commandsB);
    RoboCheck(strstr(commandsA, "draw-image") != NULL);
    RoboCheck(strstr(commandsA, "0x") == NULL);

    /* And the pixels, which is the claim made concrete. */
    {
        GfxSurface *targetA = ImageSurface(80u, 80u);
        GfxSurface *targetB = ImageSurface(80u, 80u);
        uint32_t x;
        uint32_t y;

        ImageFill(targetA, IMAGE_WHITE);
        ImageFill(targetB, IMAGE_WHITE);
        ImagePaint(listA, targetA, NULL);
        ImagePaint(listB, targetB, NULL);
        for (y = 0u; y < 80u; y++) {
            for (x = 0u; x < 80u; x++) {
                GfxColorRgba8 one;
                GfxColorRgba8 two;

                RoboCheckEqInt(GfxSurfaceGetPixel(targetA, x, y, &one), RoboOk);
                RoboCheckEqInt(GfxSurfaceGetPixel(targetB, x, y, &two), RoboOk);
                ImageColorEq(two, one, "a pixel from the second pass");
            }
        }
        GfxSurfaceRelease(targetA);
        GfxSurfaceRelease(targetB);
    }

    PaintDisplayListDestroy(listA);
    PaintDisplayListDestroy(listB);
    PaintResultDispose(&first);
    PaintResultDispose(&second);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* The dumps                                                           */
/* ------------------------------------------------------------------ */

/**
 * The dumps describe the image, and describe it without an address.
 *
 * The record's line names the resource, the source rectangle and the resource's own size; the
 * command's names the same resource and the destination it resolved. Both are read here as text,
 * because that is what a person diffs -- and a dump that printed a pointer would be a diff that
 * changed for no reason.
 */
static void TestImageDump(void)
{
    PaintFixtureDoc fixture;
    PaintResult result;
    ImageAssets assets;
    PaintDisplayList *list = NULL;
    PaintDisplayListStats stats;
    PaintOperation operation;
    char records[4096];
    char commands[4096];
    char line[512];
    size_t length = 0u;
    GfxSurface *surface = ImageLogoSurface();

    ImageAssetsBegin(&assets, surface);
    ImageBegin(&fixture, IMAGE_DOCUMENT,
                IMAGE_BASE_CSS
                " .box { width: 200px; height: 100px }"
                " .pic { width: 64px; height: 48px }",
                "pic", &assets);
    PaintOnce(&fixture, &result);
    memset(&stats, 0, sizeof(stats));
    ImageBuild(&result, &list, &stats);
    operation = PaintRequire(&result, RoboPaintOpImage);

    ImageDumpRecords(result.list, records, sizeof(records));
    RoboCheck(strstr(records, "image") != NULL);
    RoboCheck(strstr(records, "src=0,0,4x4") != NULL);
    RoboCheck(strstr(records, "source_size=4x4") != NULL);
    RoboCheck(strstr(records, "resource=") != NULL);
    RoboCheck(strstr(records, "0x") == NULL);

    ImageDumpCommands(list, commands, sizeof(commands));
    RoboCheck(strstr(commands, "draw-image") != NULL);
    RoboCheck(strstr(commands, "resource=") != NULL);
    /* The destination is the command's `rect`, printed the same way for every kind. */
    RoboCheck(strstr(commands, "rect=0,0,64,48") != NULL);

    /* One record's own line, through its writer. */
    {
        PaintDiag diag;

        PaintDiagReset(&diag);
        RoboCheckEqInt(PaintOperationWrite(&operation, line, sizeof(line), &length,
                                                     &diag),
                          RoboOk);
        RoboCheck(strstr(line, "image") != NULL);
        RoboCheck(strstr(line, "src=") != NULL);
    }

    PaintDisplayListDestroy(list);
    PaintResultDispose(&result);
    PaintEnd(&fixture);
    ImageAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterPaintImage(void)
{
    TestRegister("paint.image.record_shape", TestImageRecordShape);
    TestRegister("paint.image.command_and_destination", TestImageCommandAndDestination);
    TestRegister("paint.image.order", TestImageOrder);
    TestRegister("paint.image.multiple", TestImageMultiple);
    TestRegister("paint.image.pixels", TestImagePixels);
    TestRegister("paint.image.scaling", TestImageScaling);
    TestRegister("paint.image.alpha", TestImageAlpha);
    TestRegister("paint.image.empty_destination", TestImageEmptyDestination);
    TestRegister("paint.image.missing", TestImageMissing);
    TestRegister("paint.image.none", TestImageNone);
    TestRegister("paint.image.hand_built_rules", TestImageHandBuiltRules);
    TestRegister("paint.image.resource_lifetime", TestImageResourceLifetime);
    TestRegister("paint.image.mutation_isolation", TestImageMutationIsolation);
    TestRegister("paint.image.determinism", TestImageDeterminism);
    TestRegister("paint.image.dump", TestImageDump);
}
