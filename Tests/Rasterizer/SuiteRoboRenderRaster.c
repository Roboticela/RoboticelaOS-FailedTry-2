/*
 * RoboRender raster suite (Phase 11.12) — `render.raster.*`.
 *
 * `render.scene.*` beside it renders a *document*; this suite is about the **primitive** claims a
 * rasteriser makes, and every one of them is checked at a pixel or a byte rather than argued:
 *
 *   **a frame that draws nothing changes nothing**   a document with no background, no border, no text
 *                                                   and no image executes its whole list
 *                                                   (`commands > 0`) and leaves every byte of the
 *                                                   target as it was (`draws == 0`), which is the
 *                                                   statement that structural commands are not
 *                                                   drawings;
 *
 *   **a fill is a rectangle, half-open**            the border box's own edges are painted and the
 *                                                   pixel one past each edge is not -- pinned at
 *                                                   all four edges, because a rasteriser that is
 *                                                   off by one looks right in the middle;
 *
 *   **alpha and opacity are two numbers**           a half-transparent fill and a half-opaque element
 *                                                   each land *between* the page and the source, and
 *                                                   each equals a reference built by driving the
 *                                                   graphics raster directly with the same colour,
 *                                                   opacity and rectangle. That equality is the
 *                                                   plumbing claim: the frame passed the right
 *                                                   colour, strength and geometry down;
 *
 *   **the boundary clips, and the target bounds**   a child that overhangs an `overflow: hidden`
 *                                                   ancestor is cut at the ancestor's padding edge,
 *                                                   and a box that overhangs the *target* is cut at
 *                                                   the target -- with the suite run under a
 *                                                   sanitizer, which is what makes "no write outside
 *                                                   the buffer" checkable rather than asserted;
 *
 *   **identical input, identical bytes**            the same list into two targets, built in
 *                                                   different orders and at different times, is
 *                                                   compared byte for byte, with the record and the
 *                                                   dump compared beside it;
 *
 *   **a list this build cannot execute refuses**    a hand-built list with an unbalanced scope pair
 *                                                   is refused *before a pixel is written* -- the
 *                                                   target's checksum is unchanged -- because half a
 *                                                   frame is not a frame;
 *
 *   **a region is clamped, and a region that misses does nothing**  both are the arithmetic a
 *                                                   partial repaint depends on, and both are
 *                                                   asserted on the *record* (which rectangle was
 *                                                   executed) and on the pixels;
 *
 *   **a frame lands where it is told to**            11.13's half, and it is asserted the only way a
 *                                                   presentation can be: the *destination* is read
 *                                                   back. A frame presented at an offset puts a named
 *                                                   pixel of the picture at the same offset in the
 *                                                   destination, the destination's own pixels outside
 *                                                   the frame are untouched, and the record's two pixel
 *                                                   counts sum to the frame's own. A frame that does
 *                                                   not fit is clipped rather than refused, and the
 *                                                   destination proves which pixels survived.
 *
 * Every case builds a real document through the real chain and runs a real frame. Hand-built lists
 * appear only where the question *is* the list's shape. Chromium is reference only.
 */

#include "RoboRenderTestSupport.h"

#include "RoboGraphics/RoboGraphicsRaster.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/* The page every pixel case starts from, and the two colours a case names. */
#define RASTER_WHITE GfxRgba8(0xff, 0xff, 0xff, 255)
#define RASTER_RED GfxRgba8(0xcc, 0x00, 0x00, 255)
#define RASTER_BLUE GfxRgba8(0x33, 0x66, 0xcc, 255)
#define RASTER_GREEN GfxRgba8(0x00, 0xd0, 0x40, 255)

#define RASTER_BASE_CSS "html, body, div, p, span { display: block }"

/* ------------------------------------------------------------------ */
/* A reference frame, driven through the graphics raster by hand       */
/* ------------------------------------------------------------------ */

/**
 * Fill @p rect into @p target with @p color at @p opacity, through RoboGraphics' own raster.
 *
 * The reference the alpha and opacity cases compare against, and it is not a second implementation of
 * the blend: it is the *same* primitive (`GfxRasterFillRect`) reached by a different road. What
 * the comparison actually proves is that the frame handed that primitive the right rectangle, the
 * right colour and the right strength -- which is precisely what this module is responsible for and
 * what a hard-coded expected byte would not have distinguished.
 */
static void RasterReference(RenderTarget *target,
                             LayoutRect rect,
                             GfxColorRgba8 color,
                             uint8_t opacity)
{
    GfxRaster *raster = NULL;

    RoboCheckEqInt(GfxRasterCreate(RenderTargetSurface(target), &raster), RoboOk);
    GfxRasterSetOpacity(raster, opacity);
    RoboCheckEqInt(GfxRasterFillRect(raster, rect, color), RoboOk);
    GfxRasterDestroy(raster);
}

/* ------------------------------------------------------------------ */
/* An empty frame                                                      */
/* ------------------------------------------------------------------ */

static void TestRenderRasterEmptyFrame(void)
{
    RenderScene scene;
    RenderTarget *target;
    RenderFrame frame;
    uint64_t before;

    RenderFixture();
    /* A document with structure and nothing to draw: the display list carries the three node markers
     * per box and not one drawing command. */
    RenderSceneBegin(&scene,
                       "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS);
    target = RenderPage(48u, 32u, RASTER_WHITE);
    before = RenderTargetChecksum(target);

    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, &frame, NULL), RoboOk);
    RoboCheckEqInt(RenderFrameIsValid(&frame), 1);
    RoboCheck(frame.pass.commands > 0u);
    RoboCheckEqInt((int)frame.pass.draws, 0);
    RoboCheckEqInt((int)frame.pass.filled, 0);
    RoboCheckEqInt((int)frame.pass.skipped, (int)frame.pass.commands);
    RoboCheckEqInt(frame.limited, 0);
    RoboCheckEqInt((int)frame.regions, 0);
    PaintRectIs(0, 0, 48, 32, frame.region, "a full frame's region");
    RoboCheckEqInt((int)frame.regionPixels, 48 * 32);
    RoboCheckEqInt((int)frame.targetPixels, 48 * 32);
    /* Not one byte moved, and the record says why: nothing in that list is a drawing. */
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));
    RenderPixelIs(target, 0u, 0u, RASTER_WHITE, "an empty frame");
    RenderPixelIs(target, 47u, 31u, RASTER_WHITE, "an empty frame");

    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

static void TestRenderRasterArgumentRules(void)
{
    RenderScene scene;
    RenderTarget *target;
    RenderFrame frame;
    RenderDiag diag;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body></body></html>", RASTER_BASE_CSS);
    target = RenderPage(16u, 16u, RASTER_WHITE);
    RenderDiagReset(&diag);

    RoboCheckEqInt(RenderRasterize(NULL, target, NULL, &frame, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainFrame);
    RoboCheckEqInt(RenderRasterize(scene.display, NULL, NULL, &frame, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderRasterizeRegions(NULL, target, NULL, NULL, &frame, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderRasterizeRegions(scene.display, NULL, NULL, NULL, &frame, &diag),
                      RoboInvalidArgument);

    /* A NULL record is legal: a caller that only wants pixels is not made to hold a record. */
    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, NULL, NULL), RoboOk);

    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* Rectangles                                                          */
/* ------------------------------------------------------------------ */

static void TestRenderRasterBackgroundPixels(void)
{
    RenderScene scene;
    RenderTarget *target;
    RenderFrame frame;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px;"
                       " background-color: #3366cc }");
    target = RenderPage(64u, 40u, RASTER_WHITE);

    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, &frame, NULL), RoboOk);
    RoboCheck(frame.pass.backgrounds >= 1u);
    RoboCheck(frame.pass.filled >= 1u);

    /* The box's border box is `(4, 4, 40, 24)`: half-open, so `x` is inside and `x + width` is
     * not. All four edges, because a rasteriser that is off by one is still right in the middle. */
    RenderPixelIs(target, 4u, 4u, RASTER_BLUE, "the box's own top-left corner");
    RenderPixelIs(target, 43u, 27u, RASTER_BLUE, "the box's own bottom-right corner");
    RenderPixelIs(target, 3u, 4u, RASTER_WHITE, "one pixel left of the box");
    RenderPixelIs(target, 44u, 4u, RASTER_WHITE, "one pixel right of the box");
    RenderPixelIs(target, 4u, 3u, RASTER_WHITE, "one pixel above the box");
    RenderPixelIs(target, 4u, 28u, RASTER_WHITE, "one pixel below the box");
    /* And the page colour really is the *body's*, not a leftover: the body fills the viewport. */
    RenderPixelIs(target, 60u, 36u, RASTER_WHITE, "the page beyond the box");

    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* Alpha and opacity                                                   */
/* ------------------------------------------------------------------ */

static void TestRenderRasterAlphaAndOpacity(void)
{
    RenderScene scene;
    RenderTarget *alphaFrame;
    RenderTarget *alphaReference;
    RenderTarget *opacityFrame;
    RenderTarget *opacityReference;
    RenderFrame frame;
    GfxColorRgba8 blended;

    RenderFixture();

    /*
     * A half-transparent background: the colour's own alpha, with no `opacity` in the document at
     * all. The rectangle is `(4, 4, 40, 24)` and the reference drives the same fill with the same
     * channels -- including alpha -- at full strength.
     */
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       " .box { width: 40px; height: 24px; margin: 4px;"
                       " background-color: rgba(0, 0, 0, 0.5) }");
    alphaFrame = RenderPage(64u, 40u, RASTER_WHITE);
    alphaReference = RenderPage(64u, 40u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, alphaFrame, NULL, &frame, NULL),
                      RoboOk);
    RasterReference(alphaReference, MakeGfxRect(4, 4, 40, 24), GfxRgba8(0, 0, 0, 128),
                     255u);
    RenderTargetsEqual(alphaFrame, alphaReference, "a half-transparent fill");
    blended = RenderPixel(alphaFrame, 10u, 10u);
    /* And the pixel really is *between* the two colours -- otherwise the equality above would be a
     * statement about a fill that did nothing. */
    RoboCheck(blended.r != 0u && blended.r != 0xff);
    RoboCheck(blended.r == blended.g && blended.g == blended.b);
    RenderPixelIs(alphaFrame, 2u, 2u, RASTER_WHITE, "outside a half-transparent fill");
    RenderTargetEnd(alphaReference);
    RenderTargetEnd(alphaFrame);
    RenderSceneEnd(&scene);

    /*
     * The same picture through the other door: an **opaque** colour inside an element at
     * `opacity: 0.5`. Two numbers, one result -- which is the claim `RoboPaintOpacity.h` makes and
     * the reason the reference passes the strength to the raster instead of the test blending by
     * hand.
     */
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       " .box { width: 40px; height: 24px; margin: 4px; opacity: 0.5;"
                       " background-color: #000000 }");
    opacityFrame = RenderPage(64u, 40u, RASTER_WHITE);
    opacityReference = RenderPage(64u, 40u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, opacityFrame, NULL, &frame, NULL),
                      RoboOk);
    RoboCheck(frame.pass.faded >= 1u);
    RasterReference(opacityReference, MakeGfxRect(4, 4, 40, 24), GfxRgba8(0, 0, 0, 255),
                     128u);
    RenderTargetsEqual(opacityFrame, opacityReference, "a half-opaque element");
    blended = RenderPixel(opacityFrame, 10u, 10u);
    RoboCheck(blended.r != 0u && blended.r != 0xff);
    RenderPixelIs(opacityFrame, 2u, 2u, RASTER_WHITE, "outside a half-opaque element");
    RenderTargetEnd(opacityReference);
    RenderTargetEnd(opacityFrame);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* Boundaries                                                          */
/* ------------------------------------------------------------------ */

static void TestRenderRasterClipAndBounds(void)
{
    RenderScene scene;
    RenderTarget *target;
    RenderFrame frame;

    RenderFixture();
    /*
     * An `overflow: hidden` parent whose child is far wider than it: CSS clips the *contents* at the
     * padding edge, and the parent's own background is drawn under the boundary the parent was entered
     * with -- which is the arrangement `paint/docs/Clipping.md` argues and the one a rasteriser can
     * get wrong invisibly.
     */
    RenderSceneBegin(&scene,
                       "<html><body><div class=\"clip\"><div class=\"wide\"></div></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .clip { width: 40px; height: 20px; overflow: hidden;"
                       " background-color: #3300cc }"
                       " .wide { width: 200px; height: 20px; background-color: #cc0000 }");
    target = RenderPage(80u, 40u, RASTER_WHITE);

    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, &frame, NULL), RoboOk);
    RoboCheck(frame.pass.clipScopes >= 1u);
    RoboCheck(frame.pass.clipped >= 1u);

    /* Inside the boundary the child is drawn over its parent's background. */
    RenderPixelIs(target, 0u, 0u, RASTER_RED, "the child inside the clip");
    RenderPixelIs(target, 39u, 19u, RASTER_RED, "the last clipped pixel");
    /* And outside it -- one pixel past the parent's padding edge -- the child is gone, while the page
     * beyond it is untouched. */
    RenderPixelIs(target, 40u, 0u, RASTER_WHITE, "one pixel past the clip");
    RenderPixelIs(target, 60u, 10u, RASTER_WHITE, "the page past the clip");

    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

static void TestRenderRasterOffscreenAndEdges(void)
{
    RenderScene scene;
    RenderTarget *target;
    RenderFrame frame;

    RenderFixture();
    /*
     * Three boxes and a target smaller than any of them: one hanging off the top-left (negative
     * offsets), one entirely off the right, and one inside. Under the sanitizer gate this is the case
     * that proves "never writes outside the pixel buffer", and without it the visible part of the
     * first box must still be exactly where the geometry says.
     */
    RenderSceneBegin(&scene,
                       "<html><body><div class=\"off\"></div><div class=\"away\"></div>"
                       "<div class=\"on\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .off { width: 40px; height: 20px; margin-left: -12px; margin-top: -6px;"
                       " background-color: #00d040 }"
                       " .away { width: 30px; height: 10px; margin-left: 200px;"
                       " background-color: #cc0000 }"
                       " .on { width: 8px; height: 8px; margin-left: 4px;"
                       " background-color: #3366cc }");
    target = RenderPage(32u, 24u, RASTER_WHITE);

    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, &frame, NULL), RoboOk);
    /* The box that hangs off the corner is drawn from the target's own origin: the part that fits is
     * at the geometry's coordinates, and nothing wrapped around. */
    RenderPixelIs(target, 0u, 0u, RASTER_GREEN, "a box hanging off the origin");
    RenderPixelIs(target, 27u, 13u, RASTER_GREEN, "the last pixel of that box");
    RenderPixelIs(target, 28u, 14u, RASTER_WHITE, "one pixel past it");
    /* The box entirely outside drew nothing at all, anywhere. */
    RenderPixelIs(target, 31u, 23u, RASTER_WHITE, "a box entirely off the target");

    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

static void TestRenderRasterDeterminism(void)
{
    RenderScene scene;
    RenderTarget *first;
    RenderTarget *second;
    RenderTarget *noise;
    RenderFrame frameA;
    RenderFrame frameB;
    char dumpA[2048];
    char dumpB[2048];
    size_t lengthA = 0u;
    size_t lengthB = 0u;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 30px; height: 18px; margin: 5px; opacity: 0.75;"
                       " background-color: rgba(0, 0, 0, 0.5) }");

    first = RenderPage(48u, 32u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, first, NULL, &frameA, NULL), RoboOk);

    /* Allocation order between the two runs is deliberately different: a target made, used and
     * released, so the second run's buffer is a different buffer at a different address. Nothing in
     * the output may depend on that. */
    noise = RenderPage(20u, 20u, RASTER_RED);
    RenderTargetEnd(noise);

    second = RenderPage(48u, 32u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, second, NULL, &frameB, NULL), RoboOk);

    RenderTargetsEqual(first, second, "two runs of one list");
    RoboCheckEqInt(RenderFrameEqual(&frameA, &frameB), 1);
    RoboCheck(RenderTargetChecksum(first) != 0u);
    RoboCheckEqInt(RenderFrameWrite(&frameA, dumpA, sizeof(dumpA), &lengthA, NULL),
                      RoboOk);
    RoboCheckEqInt(RenderFrameWrite(&frameB, dumpB, sizeof(dumpB), &lengthB, NULL),
                      RoboOk);
    RoboCheckEqInt((int)lengthA, (int)lengthB);
    RoboCheckStrEq(dumpA, dumpB);

    /* Re-running the frame into the *same* target twice is also stable, which is the stronger
     * statement: a second execution over its own output must not drift, and a `clear` in between must
     * make it identical to the first run. */
    RoboCheckEqInt(RenderRasterize(scene.display, second, NULL, NULL, NULL), RoboOk);
    RenderTargetsEqual(first, second, "a re-run over its own output");
    RoboCheckEqInt(RenderTargetClear(second, RASTER_WHITE, NULL), RoboOk);
    RoboCheckEqInt(RenderRasterize(scene.display, second, NULL, NULL, NULL), RoboOk);
    RenderTargetsEqual(first, second, "a clear and re-run");

    RenderTargetEnd(second);
    RenderTargetEnd(first);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* The preflight                                                       */
/* ------------------------------------------------------------------ */

static void TestRenderRasterPreflightRefuses(void)
{
    PaintDisplayList *list = NULL;
    PaintDisplayCommand command;
    RenderTarget *target;
    RenderFrame frame;
    RenderDiag diag;
    uint64_t before;

    RenderFixture();
    target = RenderPage(16u, 16u, RASTER_WHITE);
    before = RenderTargetChecksum(target);

    /* A list a builder would never produce: a `pop-clip` that closes a scope nothing opened. The
     * *record* is valid (a known kind carrying a boundary in force), so the list's own append accepts
     * it -- which is what makes the balance rule the rasteriser's to check. A clip command's validity
     * rule is the one 11.8 states: it must carry an **active** boundary, because a scope that bounds
     * neither axis is a command whose fields disagree about what it is. */
    RoboCheckEqInt(PaintDisplayListCreate(&list, NULL), RoboOk);
    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdPopClip;
    command.node = (LayoutNodeId)1;
    command.rect = MakeGfxRect(0, 0, 16, 16);
    command.clip.horizontal.mode = RoboCssOverflowHidden;
    command.clip.horizontal.start = 0;
    command.clip.horizontal.end = 16;
    command.clip.vertical.mode = RoboCssOverflowHidden;
    command.clip.vertical.start = 0;
    command.clip.vertical.end = 16;
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboOk);

    RenderFrameInit(&frame);
    frame.checksum = 0xfeedu;
    RoboCheckEqInt(RenderRasterize(list, target, NULL, &frame, &diag),
                      RoboNotSupported);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainList);
    RoboCheck(diag.message != NULL);
    /* **All-or-nothing**: the refusal happened before a pixel was written, and the record the caller
     * passed in was not overwritten with a half-made one. */
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));
    RoboCheckEqInt((int)frame.checksum, (int)0xfeed);
    RoboCheckEqInt(RenderFrameIsValid(&frame), 0);
    PaintDisplayListClear(list);

    /* The other half of the pair: a `push-clip` nothing closes, which would leave every following
     * drawing bounded by a scope the list never ends. */
    PaintDisplayCommandInit(&command);
    command.kind = RoboPaintDisplayCmdPushClip;
    command.node = (LayoutNodeId)1;
    command.rect = MakeGfxRect(0, 0, 16, 16);
    command.clip.horizontal.mode = RoboCssOverflowHidden;
    command.clip.horizontal.start = 0;
    command.clip.horizontal.end = 16;
    command.clip.vertical.mode = RoboCssOverflowHidden;
    command.clip.vertical.start = 0;
    command.clip.vertical.end = 16;
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboOk);
    RoboCheckEqInt(RenderRasterize(list, target, NULL, &frame, &diag), RoboNotSupported);
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));
    PaintDisplayListClear(list);

    /* And the *other* half of the preflight -- an unknown command kind -- is unreachable through the
     * public API, which is asserted rather than assumed: the list's own append refuses such a command,
     * so the check exists for a list this build did not produce. */
    PaintDisplayCommandInit(&command);
    command.kind = (PaintDisplayCommandKind)99;
    command.node = (LayoutNodeId)1;
    command.rect = MakeGfxRect(0, 0, 16, 16);
    RoboCheckEqInt(PaintDisplayListAppend(list, &command, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)PaintDisplayListCount(list), 0);
    PaintDisplayListDestroy(list);

    RenderTargetEnd(target);
}

/* ------------------------------------------------------------------ */
/* Text without a face                                                 */
/* ------------------------------------------------------------------ */

static void TestRenderRasterNoFontIsANumber(void)
{
    RenderScene scene;
    RenderFace face;
    RenderTarget *target;
    RenderFrame frame;
    uint64_t before;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><p class=\"line\">AA</p></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .line { font-size: 100px; color: #cc0000 }");
    target = RenderPage(64u, 32u, RASTER_WHITE);
    before = RenderTargetChecksum(target);

    /* No face: nothing is drawn, and the count says so rather than the frame being silently blank. */
    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, &frame, NULL), RoboOk);
    RoboCheck(frame.pass.texts >= 1u);
    RoboCheckEqInt((int)frame.pass.noFont, (int)frame.pass.texts);
    RoboCheckEqInt((int)frame.pass.glyphs, 0);
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));

    /* With the face, the run's code points resolve through its cmap and its glyphs stroke the square
     * outline the fixture states -- so a pixel changes and the count of glyph draws equals the count of
     * characters. */
    RenderFaceCreate(&face);
    RoboCheckEqInt(RenderRasterize(scene.display, target, face.face, &frame, NULL),
                      RoboOk);
    RoboCheckEqInt((int)frame.pass.noFont, 0);
    RoboCheckEqInt((int)frame.pass.glyphs, 2);
    RoboCheckEqInt((int)frame.pass.glyphsMissing, 0);
    RoboCheck(RenderTargetChecksum(target) != before);
    /* The run's own box, scanned for ink rather than pinned to a pixel: at `font-size: 100px` the one
     * mapped glyph is a square **outline** ten pixels on a side, so its middle is background and its
     * edges are the run. Naming a pixel here would be asserting about the glyph's interior. */
    RenderRegionHasInkIs(target, 0, 0, 16, 14, RASTER_WHITE, "a run's glyphs");
    RenderFaceDestroy(&face);

    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* Regions                                                             */
/* ------------------------------------------------------------------ */

static void TestRenderRasterRegionClamping(void)
{
    RenderScene scene;
    PaintInvalidationSet *set = NULL;
    RenderTarget *target;
    RenderFrame frame;
    uint64_t before;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    target = RenderPage(64u, 40u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, NULL, NULL), RoboOk);

    RoboCheckEqInt(PaintInvalidationSetCreate(&set, NULL), RoboOk);

    /* A region that runs off the target: executed at the intersection, and the record reports the
     * rectangle it *executed* rather than the one it was handed -- so "did my region reach the
     * surface?" is answerable from the frame alone. */
    RoboCheckEqInt(PaintInvalidationSetAddRect(set, MakeGfxRect(48, 20, 100, 100),
                                                           RoboLayoutNodeIdNone,
                                                           RoboPaintInvalidationReasonStyle, NULL,
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(RenderRasterizeRegions(scene.display, target, NULL, set, &frame, NULL),
                      RoboOk);
    RoboCheckEqInt(frame.limited, 1);
    RoboCheckEqInt((int)frame.regions, 1);
    PaintRectIs(48, 20, 16, 20, frame.region, "a region clamped to the target");
    RoboCheckEqInt((int)frame.regionPixels, 16 * 20);
    RoboCheck(frame.regionPixels < frame.targetPixels);
    /* The box is at `(4, 4, 40, 24)` and the region starts at x = 48, so the run executed the list and
     * drew nothing of it: `boundedOut` is the counter that says the work was outside the window. */
    RoboCheck(frame.pass.boundedOut >= 1u);
    RenderPixelIs(target, 10u, 10u, RASTER_BLUE, "the box outside the region");

    /* A region that misses the target entirely: success, nothing executed, not one byte moved. */
    before = RenderTargetChecksum(target);
    PaintInvalidationSetClear(set);
    RoboCheckEqInt(PaintInvalidationSetAddRect(set, MakeGfxRect(500, 500, 10, 10),
                                                           RoboLayoutNodeIdNone,
                                                           RoboPaintInvalidationReasonStyle, NULL,
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(RenderRasterizeRegions(scene.display, target, NULL, set, &frame, NULL),
                      RoboOk);
    RoboCheckEqInt((int)frame.regionPixels, 0);
    RoboCheckEqInt((int)frame.pass.commands, 0);
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));
    RenderPixelIs(target, 10u, 10u, RASTER_BLUE, "a region that missed the target");

    /* And a region *inside* the target repaints exactly the box: the same list, a smaller window. */
    PaintInvalidationSetClear(set);
    RoboCheckEqInt(PaintInvalidationSetAddRect(set, MakeGfxRect(0, 0, 48, 32),
                                                           RoboLayoutNodeIdNone,
                                                           RoboPaintInvalidationReasonStyle, NULL,
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(RenderTargetClear(target, RASTER_WHITE, NULL), RoboOk);
    RoboCheckEqInt(RenderRasterizeRegions(scene.display, target, NULL, set, &frame, NULL),
                      RoboOk);
    PaintRectIs(0, 0, 48, 32, frame.region, "a region inside the target");
    RenderPixelIs(target, 10u, 10u, RASTER_BLUE, "the box inside the region");

    PaintInvalidationSetDestroy(set);
    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* Presentation (11.13)                                                */
/* ------------------------------------------------------------------ */

/** A page colour for the two destinations, deliberately not one the scenes use. */
#define PRESENT_DESK GfxRgba8(0xf5, 0xf5, 0xf7, 255)

/** An owned destination surface, cleared to @p fill. */
static GfxSurface *PresentSurface(uint32_t width,
                                          uint32_t height,
                                          GfxPixelFormat format,
                                          GfxColorRgba8 fill)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = format;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    RoboCheck(surface != NULL);
    RoboCheckEqInt(GfxSurfaceClearRgba8(surface, fill), RoboOk);
    return surface;
}

/** A destination pixel, through the graphics layer's own read (format-independent). */
static GfxColorRgba8 PresentPixel(const GfxSurface *surface,
                                           uint32_t x,
                                           uint32_t y)
{
    GfxColorRgba8 color;

    memset(&color, 0, sizeof(color));
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
    return color;
}

static void PresentPixelIs(const GfxSurface *surface,
                            uint32_t x,
                            uint32_t y,
                            GfxColorRgba8 expected,
                            const char *what)
{
    GfxColorRgba8 actual = PresentPixel(surface, x, y);

    if (!RenderSameColor(actual, expected)) {
        TestFail(__FILE__, __LINE__,
                       "%s: destination pixel %u,%u is %u,%u,%u,%u, expected %u,%u,%u,%u", what,
                       (unsigned)x, (unsigned)y, (unsigned)actual.r, (unsigned)actual.g,
                       (unsigned)actual.b, (unsigned)actual.a, (unsigned)expected.r,
                       (unsigned)expected.g, (unsigned)expected.b, (unsigned)expected.a);
    }
}

/*
 * The region door on its own: the arithmetic a caller asks *before* it decides to present. Every case
 * here is one rectangle, and the point of all of them is that the answer is the frame's own rectangle
 * moved by the origin and cut to the destination -- including the two shapes that are easy to get
 * wrong and impossible to notice: a frame hanging off an edge, and a frame with a negative origin.
 */
static void TestRenderPresentRegion(void)
{
    RenderTarget *frame;
    GfxSurface *dest;
    LayoutRect region;
    RenderDiag diag;

    RenderFixture();
    RenderDiagReset(&diag);
    frame = RenderPage(16u, 8u, RASTER_BLUE);
    dest = PresentSurface(64u, 32u, RoboGfxFormatRgba8, PRESENT_DESK);

    RoboCheckEqInt(RenderPresentRegion(frame, dest, 0, 0, &region, &diag), RoboOk);
    PaintRectIs(0, 0, 16, 8, region, "a frame at the destination's origin");

    RoboCheckEqInt(RenderPresentRegion(frame, dest, 48, 24, &region, &diag), RoboOk);
    PaintRectIs(48, 24, 16, 8, region, "a frame inside the destination");

    /* Off the right edge and the bottom edge: the meeting, not the request. */
    RoboCheckEqInt(RenderPresentRegion(frame, dest, 60, 30, &region, &diag), RoboOk);
    PaintRectIs(60, 30, 4, 2, region, "a frame hanging off the destination");

    /* A negative origin cuts the *left* edge, so the rectangle starts at zero and is narrower. */
    RoboCheckEqInt(RenderPresentRegion(frame, dest, -4, -4, &region, &diag), RoboOk);
    PaintRectIs(0, 0, 12, 4, region, "a frame with a negative origin");

    /* Entirely off: an empty rectangle and a success, which is the answer "nothing to show here". */
    RoboCheckEqInt(RenderPresentRegion(frame, dest, 64, 0, &region, &diag), RoboOk);
    RoboCheckEqInt((int)GfxRectIsEmpty(region), 1);
    RoboCheckEqInt(RenderPresentRegion(frame, dest, 0, 32, &region, &diag), RoboOk);
    RoboCheckEqInt((int)GfxRectIsEmpty(region), 1);

    /* The region door is a question, so it answers for a read-only destination too -- it writes
     * nothing, and refusing to answer would make a caller allocate a surface it does not need. */
    {
        GfxSurfaceDesc desc;
        GfxSurface *readOnly = NULL;

        GfxSurfaceDescInit(&desc);
        desc.width = 8u;
        desc.height = 8u;
        desc.format = RoboGfxFormatRgba8;
        desc.access = RoboGfxSurfaceAccessRead;
        RoboCheckEqInt(GfxSurfaceCreate(&desc, &readOnly), RoboOk);
        RoboCheckEqInt(RenderPresentRegion(frame, readOnly, 0, 0, &region, &diag),
                          RoboOk);
        PaintRectIs(0, 0, 8, 8, region, "a frame into a read-only destination");
        GfxSurfaceRelease(readOnly);
    }

    RoboCheckEqInt(RenderPresentRegion(NULL, dest, 0, 0, &region, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainFrame);
    RoboCheckEqInt(RenderPresentRegion(frame, NULL, 0, 0, &region, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderPresentRegion(frame, dest, 0, 0, NULL, &diag),
                      RoboInvalidArgument);

    GfxSurfaceRelease(dest);
    RenderTargetEnd(frame);
}

/*
 * The presentation itself, read back from the destination. The frame is a real document's frame -- a
 * white page with a blue box at (4, 4, 40, 24) -- so the pixel assertions are about a *picture* rather
 * than about a fill this suite wrote itself.
 */
static void TestRenderPresentPixels(void)
{
    RenderScene scene;
    RenderTarget *frame;
    GfxSurface *dest;
    GfxSurface *other;
    RenderPresentRecord record;
    RenderPresentRecord again;
    uint64_t targetChecksum;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    frame = RenderPage(64u, 40u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, frame, NULL, NULL, NULL), RoboOk);
    targetChecksum = RenderTargetChecksum(frame);

    dest = PresentSurface(128u, 96u, RoboGfxFormatRgba8, PRESENT_DESK);

    /* The record first: four facts a caller reads, and all four are the frame's own. */
    RoboCheckEqInt(RenderPresent(frame, dest, 32, 24, &record, NULL), RoboOk);
    RoboCheckEqInt(RenderPresentRecordIsValid(&record), 1);
    RoboCheckEqInt((int)record.frameWidth, 64);
    RoboCheckEqInt((int)record.frameHeight, 40);
    RoboCheckEqInt((int)record.destWidth, 128);
    RoboCheckEqInt((int)record.destHeight, 96);
    RoboCheckEqInt((int)record.destFormat, (int)RoboGfxFormatRgba8);
    RoboCheckEqInt(record.offsetX, 32);
    RoboCheckEqInt(record.offsetY, 24);
    PaintRectIs(32, 24, 64, 40, record.presented, "a frame that fits");
    RoboCheckEqInt((int)record.presentedPixels, 64 * 40);
    RoboCheckEqInt((int)record.clippedPixels, 0);
    RoboCheckEqInt(record.complete, 1);
    /* The checksum is the *picture*, taken from the frame rather than re-read from the destination. */
    RoboCheck((int)record.frameChecksum == (int)targetChecksum);

    /* Then the pixels, at the offset: the box's corner, the page around it, and the destination's own
     * colour everywhere the frame did not reach. */
    PresentPixelIs(dest, 32u + 10u, 24u + 10u, RASTER_BLUE, "the box, presented at an offset");
    PresentPixelIs(dest, 32u + 60u, 24u + 36u, RASTER_WHITE, "the page, presented at an offset");
    PresentPixelIs(dest, 0u, 0u, PRESENT_DESK, "the destination above the frame");
    PresentPixelIs(dest, 31u, 24u, PRESENT_DESK, "the destination one pixel left of the frame");
    PresentPixelIs(dest, 96u, 24u, PRESENT_DESK, "the destination one pixel right of the frame");
    /* `y = 24 + 40` is the first row *past* the frame: the rectangle is half-open, so the frame's last
     * row is 63 and 64 is the destination's own colour again. */
    PresentPixelIs(dest, 32u, 64u, PRESENT_DESK, "the destination one pixel below the frame");
    PresentPixelIs(dest, 32u, 63u, RASTER_WHITE, "the frame's own last row");
    PresentPixelIs(dest, 127u, 95u, PRESENT_DESK, "the destination's far corner");

    /*
     * Every frame pixel equals the destination pixel it was copied to -- the strong form of "the frame
     * landed", and cheap enough at this size to do in full rather than by sampling.
     */
    {
        uint32_t x;
        uint32_t y;
        int mismatches = 0;

        for (y = 0u; y < 40u; y++) {
            for (x = 0u; x < 64u; x++) {
                if (!RenderSameColor(RenderPixel(frame, x, y),
                                       PresentPixel(dest, x + 32u, y + 24u))) {
                    mismatches++;
                }
            }
        }
        RoboCheckEqInt(mismatches, 0);
    }

    /* Presenting the same frame again is the same presentation: no state, no "already shown" flag,
     * and an opaque frame over its own pixels is a fixed point. */
    RoboCheckEqInt(RenderPresent(frame, dest, 32, 24, &again, NULL), RoboOk);
    RoboCheckEqInt(RenderPresentRecordEqual(&record, &again), 1);
    PresentPixelIs(dest, 32u + 10u, 24u + 10u, RASTER_BLUE, "a frame presented twice");

    /* A second destination at a *different* offset holds the same picture, moved: the door has no
     * memory of the first one. */
    other = PresentSurface(128u, 96u, RoboGfxFormatRgba8, PRESENT_DESK);
    RoboCheckEqInt(RenderPresent(frame, other, 0, 0, &again, NULL), RoboOk);
    RoboCheckEqInt(again.offsetX, 0);
    PresentPixelIs(other, 10u, 10u, RASTER_BLUE, "the same picture at the origin");
    PresentPixelIs(other, 32u + 10u, 24u + 10u, RASTER_WHITE,
                     "the page where the first destination had the box");

    /* And a BGRA8 destination holds the same picture, logically: the conversion is the graphics
     * module's, and this assertion is what proves the presenter did not lay the channels out itself. */
    {
        GfxSurface *bgra = PresentSurface(64u, 40u, RoboGfxFormatBgra8, PRESENT_DESK);

        RoboCheckEqInt(RenderPresent(frame, bgra, 0, 0, &again, NULL), RoboOk);
        RoboCheckEqInt(again.complete, 1);
        PresentPixelIs(bgra, 10u, 10u, RASTER_BLUE, "a box in a BGRA8 destination");
        PresentPixelIs(bgra, 60u, 36u, RASTER_WHITE, "a page in a BGRA8 destination");
        GfxSurfaceRelease(bgra);
    }

    GfxSurfaceRelease(other);
    GfxSurfaceRelease(dest);
    RenderTargetEnd(frame);
    RenderSceneEnd(&scene);
}

/*
 * A frame that does not fit: clipped rather than refused, and the counts have to add up. This is the
 * resize case -- a window that shrank between the frame being rasterised and the frame being shown --
 * and the case that would silently lose a picture if the record's arithmetic were wrong.
 */
static void TestRenderPresentClipping(void)
{
    RenderScene scene;
    RenderTarget *frame;
    GfxSurface *dest;
    RenderPresentRecord record;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    frame = RenderPage(64u, 40u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, frame, NULL, NULL, NULL), RoboOk);
    dest = PresentSurface(32u, 16u, RoboGfxFormatRgba8, PRESENT_DESK);

    RoboCheckEqInt(RenderPresent(frame, dest, 0, 0, &record, NULL), RoboOk);
    RoboCheckEqInt(RenderPresentRecordIsValid(&record), 1);
    PaintRectIs(0, 0, 32, 16, record.presented, "the part of the frame that fits");
    RoboCheckEqInt((int)record.presentedPixels, 32 * 16);
    RoboCheckEqInt((int)record.clippedPixels, 64 * 40 - 32 * 16);
    /* The two counts are the frame, split: a caller can check the arithmetic rather than trust it. */
    RoboCheckEqInt((int)(record.presentedPixels + record.clippedPixels),
                      (int)RenderTargetPixelCount(frame));
    RoboCheckEqInt(record.complete, 0);

    /* The surviving corner: the frame's own (31, 15), which the box covers. */
    PresentPixelIs(dest, 31u, 15u, RASTER_BLUE, "the last pixel of a clipped frame");
    PresentPixelIs(dest, 0u, 0u, RASTER_WHITE, "the frame's own origin in a small destination");

    /*
     * And a placement that puts the frame *entirely* off the destination: success, nothing written, and
     * every pixel counted as clipped. Not a failure -- a caller that has just resized should be told
     * "nothing of this frame is visible" rather than handed an error it cannot act on.
     */
    {
        GfxSurface *far = PresentSurface(32u, 16u, RoboGfxFormatRgba8, PRESENT_DESK);

        RoboCheckEqInt(RenderPresent(frame, far, 40, 20, &record, NULL), RoboOk);
        RoboCheckEqInt((int)GfxRectIsEmpty(record.presented), 1);
        RoboCheckEqInt((int)record.presentedPixels, 0);
        RoboCheckEqInt((int)record.clippedPixels, 64 * 40);
        RoboCheckEqInt(record.complete, 0);
        /* A record that describes a presentation of nothing is still *valid*: the counts agree. */
        RoboCheckEqInt(RenderPresentRecordIsValid(&record), 1);
        PresentPixelIs(far, 0u, 0u, PRESENT_DESK, "a destination a frame fell outside of");
        PresentPixelIs(far, 31u, 15u, PRESENT_DESK, "a destination a frame fell outside of");
        GfxSurfaceRelease(far);
    }

    GfxSurfaceRelease(dest);
    RenderTargetEnd(frame);
    RenderSceneEnd(&scene);
}

/*
 * The framebuffer door: the display-side shape, where the pixels are a mapping somebody else owns.
 */
static void TestRenderPresentFramebuffer(void)
{
    RenderScene scene;
    RenderTarget *frame;
    GfxFramebufferDesc desc;
    GfxFramebuffer *fb = NULL;
    RenderPresentRecord record;
    GfxColorRgba8 color;
    const uint8_t *pixel = NULL;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       RASTER_BASE_CSS
                       "html, body { background-color: #ffffff }"
                       " .box { width: 40px; height: 24px; margin: 4px; background-color: #3366cc }");
    frame = RenderPage(64u, 40u, RASTER_WHITE);
    RoboCheckEqInt(RenderRasterize(scene.display, frame, NULL, NULL, NULL), RoboOk);

    GfxFramebufferDescInit(&desc);
    desc.width = 64u;
    desc.height = 40u;
    desc.format = RoboGfxFormatRgba8;
    RoboCheckEqInt(GfxFramebufferCreate(&desc, &fb), RoboOk);

    RoboCheckEqInt(RenderPresentFramebuffer(frame, fb, 0, 0, &record, NULL), RoboOk);
    RoboCheckEqInt(record.complete, 1);
    PaintRectIs(0, 0, 64, 40, record.presented, "a frame filling a framebuffer");

    /* Read one pixel the framebuffer's own way, so the assertion is about the mapping and not about a
     * surface this file wrapped around it. */
    RoboCheckEqInt(GfxFramebufferPixelConst(fb, 10u, 10u, &pixel), RoboOk);
    RoboCheck(pixel != NULL);
    memset(&color, 0, sizeof(color));
    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatRgba8, pixel, 4u, &color), RoboOk);
    RoboCheckEqInt((int)color.r, (int)RASTER_BLUE.r);
    RoboCheckEqInt((int)color.g, (int)RASTER_BLUE.g);
    RoboCheckEqInt((int)color.b, (int)RASTER_BLUE.b);
    /* The box is a *margin* box, so its top-left pixel is at (4, 4) and the pixel one before it is
     * the page: the same half-open rule the raster applied, seen through the display-side mapping. */
    RoboCheckEqInt(GfxFramebufferPixelConst(fb, 3u, 4u, &pixel), RoboOk);
    RoboCheckEqInt(GfxPixelUnpack(RoboGfxFormatRgba8, pixel, 4u, &color), RoboOk);
    RoboCheckEqInt((int)color.r, 255);
    RoboCheckEqInt((int)color.b, 255);

    RoboCheckEqInt(RenderPresentFramebuffer(frame, NULL, 0, 0, &record, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderPresentFramebuffer(NULL, fb, 0, 0, &record, NULL),
                      RoboInvalidArgument);

    GfxFramebufferRelease(fb);
    RenderTargetEnd(frame);
    RenderSceneEnd(&scene);
}

/*
 * The record's own rules and the developer's dump. A record is what a caller logs, so a record whose
 * fields disagree has to be refusable -- and the dump is the text a person reads back.
 */
static void TestRenderPresentRecordAndDump(void)
{
    RenderTarget *frame;
    GfxSurface *dest;
    RenderPresentRecord record;
    RenderPresentRecord bad;
    char text[512];
    size_t needed = 0u;
    size_t count = 0u;

    RenderFixture();
    frame = RenderPage(16u, 8u, RASTER_BLUE);
    dest = PresentSurface(64u, 32u, RoboGfxFormatRgba8, PRESENT_DESK);
    RoboCheckEqInt(RenderPresent(frame, dest, 8, 8, &record, NULL), RoboOk);
    RoboCheckEqInt(RenderPresentRecordIsValid(&record), 1);

    /* Field-for-field equality, and the two rules every comparison door in this tree keeps. */
    bad = record;
    RoboCheckEqInt(RenderPresentRecordEqual(&record, &bad), 1);
    bad.presentedPixels = 1u;
    RoboCheckEqInt(RenderPresentRecordEqual(&record, &bad), 0);
    RoboCheckEqInt(RenderPresentRecordEqual(&record, NULL), 0);
    RoboCheckEqInt(RenderPresentRecordEqual(NULL, NULL), 1);

    /* One clause at a time: each of these is a record two fields of which disagree about what it is. */
    bad = record;
    bad.presentedPixels = record.presentedPixels - 1u;
    RoboCheckEqInt(RenderPresentRecordIsValid(&bad), 0);
    bad = record;
    bad.clippedPixels = 1u;
    RoboCheckEqInt(RenderPresentRecordIsValid(&bad), 0);
    bad = record;
    bad.complete = 0;
    RoboCheckEqInt(RenderPresentRecordIsValid(&bad), 0);
    bad = record;
    bad.frameWidth = 0u;
    RoboCheckEqInt(RenderPresentRecordIsValid(&bad), 0);
    bad = record;
    bad.destFormat = RoboGfxFormatBgra8;
    RoboCheckEqInt(RenderPresentRecordIsValid(&bad), 0);
    bad = record;
    bad.presented.width = -1;
    RoboCheckEqInt(RenderPresentRecordIsValid(&bad), 0);

    /* The dump: four lines, one per group of facts. */
    RoboCheckEqInt(RenderPresentWrite(&record, text, sizeof(text), &needed, NULL), RoboOk);
    RoboCheck(needed > 0u);
    RoboCheckEqInt((int)needed, (int)strlen(text));
    {
        const char *at = text;

        while (*at != '\0') {
            if (*at == '\n') {
                count++;
            }
            at++;
        }
    }
    RoboCheckEqInt((int)count, 4);
    /* The format names come from the pixel-format layer, which spells them in the case the frame dump
     * beside this suite already uses -- read rather than restated. */
    RoboCheck(strstr(text, "present 16x8 RGBA8 -> 64x32 RGBA8 at 8,8") != NULL);
    RoboCheck(strstr(text, "presented=8,8,16,8") != NULL);
    RoboCheck(strstr(text, "pixels=128 clipped=0 complete=1") != NULL);
    RoboCheck(strstr(text, "record=valid") != NULL);
    /* No address, ever -- `=0x` and not a bare `0x`, so the assertion is about a printed pointer and
     * not about the substring `16x8` happens to contain. */
    RoboCheck(strstr(text, "=0x") == NULL);

    /* A buffer that is too small reports the length it needed rather than truncating. */
    {
        char small[8];
        RenderDiag diag;

        RenderDiagReset(&diag);
        RoboCheckEqInt(RenderPresentWrite(&record, small, sizeof(small), &count, &diag),
                          RoboNotSupported);
        RoboCheckEqInt((int)count, (int)needed);
        RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainDump);
        RoboCheck(small[0] != '\0');
    }
    RoboCheckEqInt(RenderPresentWrite(NULL, text, sizeof(text), &needed, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderPresentWrite(&record, NULL, sizeof(text), &needed, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderPresentWrite(&record, text, sizeof(text), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderPresentWrite(&record, text, 0u, &needed, NULL),
                      RoboInvalidArgument);

    GfxSurfaceRelease(dest);
    RenderTargetEnd(frame);
}

/*
 * What the door refuses, and the reason a read-only destination is one of them: presenting *writes*, so
 * a destination that cannot be written is a caller mistake rather than an empty presentation.
 */
static void TestRenderPresentRefusals(void)
{
    RenderTarget *frame;
    GfxSurface *dest;
    GfxSurface *readOnly = NULL;
    GfxSurfaceDesc desc;
    RenderPresentRecord record;
    RenderDiag diag;

    RenderFixture();
    RenderDiagReset(&diag);
    frame = RenderPage(8u, 8u, RASTER_BLUE);
    dest = PresentSurface(16u, 16u, RoboGfxFormatRgba8, PRESENT_DESK);

    RoboCheckEqInt(RenderPresent(NULL, dest, 0, 0, &record, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainFrame);
    RoboCheck(diag.message != NULL);
    RoboCheckEqInt(RenderPresent(frame, NULL, 0, 0, &record, &diag),
                      RoboInvalidArgument);
    /* The record is optional: a caller that only wants pixels is not made to hold one. */
    RoboCheckEqInt(RenderPresent(frame, dest, 0, 0, NULL, &diag), RoboOk);

    GfxSurfaceDescInit(&desc);
    desc.width = 16u;
    desc.height = 16u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessRead;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &readOnly), RoboOk);
    RoboCheckEqInt(RenderPresent(frame, readOnly, 0, 0, &record, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainFrame);
    GfxSurfaceRelease(readOnly);

    GfxSurfaceRelease(dest);
    RenderTargetEnd(frame);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterRenderRaster(void)
{
    TestRegister("render.raster.empty_frame", TestRenderRasterEmptyFrame);
    TestRegister("render.raster.argument_rules", TestRenderRasterArgumentRules);
    TestRegister("render.raster.background_pixels", TestRenderRasterBackgroundPixels);
    TestRegister("render.raster.alpha_and_opacity", TestRenderRasterAlphaAndOpacity);
    TestRegister("render.raster.clip_and_bounds", TestRenderRasterClipAndBounds);
    TestRegister("render.raster.offscreen_and_edges", TestRenderRasterOffscreenAndEdges);
    TestRegister("render.raster.determinism", TestRenderRasterDeterminism);
    TestRegister("render.raster.preflight_refuses", TestRenderRasterPreflightRefuses);
    TestRegister("render.raster.no_font_is_a_number",
                       TestRenderRasterNoFontIsANumber);
    TestRegister("render.raster.region_clamping", TestRenderRasterRegionClamping);
    TestRegister("render.present_region", TestRenderPresentRegion);
    TestRegister("render.present_pixels", TestRenderPresentPixels);
    TestRegister("render.present_clipping", TestRenderPresentClipping);
    TestRegister("render.present_framebuffer", TestRenderPresentFramebuffer);
    TestRegister("render.present_record_and_dump", TestRenderPresentRecordAndDump);
    TestRegister("render.present_refusals", TestRenderPresentRefusals);
}
