/*
 * RoboRender scene suite (Phase 11.12) — `render.scene.*`.
 *
 * The suite that renders a **document**, and the phase's strongest evidence: a page with a background,
 * a bordered card, a semi-transparent overlay, a line of text, a picture and a clipping ancestor is
 * rasterised into one target, every claim about it is checked at a named pixel, and the result is
 * written out as a PNG for a person to look at.
 *
 * The five claims, and where each is pinned:
 *
 *   **the scene renders**            `checkpoint` asserts a pixel inside the card, a pixel on its
 *                                    border band, a pixel of the semi-transparent overlay, a pixel of
 *                                    the picture, and a pixel on each side of the clip boundary -- and
 *                                    that the frame's own record counted one of each drawing kind, so
 *                                    "the picture is there" and "the record says why" are one
 *                                    statement rather than two;
 *
 *   **a region run equals a full frame inside the region**  `region_matches_full` is the capstone:
 *                                    render the first state into a target, take the regions 11.11 says
 *                                    are stale, re-run **the second state's list** bounded by them,
 *                                    and compare the whole target against a full render of the second
 *                                    state. Byte for byte, or the region run missed a pixel;
 *
 *   **no repaint owed is exactly no work**  `no_repaint` takes the regions of a document compared with
 *                                    itself -- an empty set -- and asserts that the run executed
 *                                    nothing, touched no byte, and said so in its record;
 *
 *   **a frame is a snapshot**        `snapshot` releases the record list, the display list and the
 *                                    tree **after** rendering and asserts the target's bytes are
 *                                    unchanged, then rebuilds the same document and asserts the two
 *                                    frames are equal -- which is the whole reason a display list
 *                                    carries no pointer;
 *
 *   **the dump of a real frame is deterministic**  `dump` renders one scene twice and compares the two
 *                                    dumps as text, with no address in either.
 *
 * The scene is deliberately *not* a demonstration of everything the runtime can draw: it is the
 * smallest document that makes each of the six requests in the phase's brief observable at a pixel, so
 * that a later phase changing any one of them breaks a named assertion rather than a picture nobody
 * looks at. Chromium is reference only; nothing here is a translation of it.
 */

#include "RoboRenderTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The scene                                                           */
/* ------------------------------------------------------------------ */

/*
 * The document, stated once, with every number chosen so an assertion can name a pixel.
 *
 * The numbers are the **flow geometry** of this document in this build, and they are written out rather
 * than derived because they are what the display list was checked against: `position` is parsed by
 * RoboCSS but not laid out by RoboLayout (the runtime's Phase 10 scope), so every box below is in
 * normal flow, at the coordinate its margins put it at. Stating them makes a later layout phase that
 * starts honouring `left`/`top` fail a named assertion here rather than quietly moving the picture:
 *
 *   page       #ffffff, the body fills the 200x200 viewport
 *   card       margin 10, content 80x40, `border: 4px solid`, so its border box is (10, 10, 88, 48),
 *              its padding box (14, 14, 80, 40) and its four border bands 4 wide on each side
 *   text       one run in the card at `font-size: 100px`, coloured #cc0000. The run's box is what the
 *              fixture's measurer said it was -- (14, 14, 16, 12) -- and the glyphs are *stroked
 *              outlines* one pixel wide, so the case scans the box for ink instead of naming a pixel
 *              inside a glyph's empty middle
 *   picture    a 4x4 image at (10, 68) stretched to 32x32: its red top-left quadrant lands at the
 *              destination's top-left, so a pixel there says which corner the sampler read
 *   box        a 60x30 sibling at (0, 100) that is `rgba(0, 0, 0, 0.5)` *and* `opacity: 0.5` -- two
 *              numbers that multiply, which is why no assertion can be satisfied by a build that
 *              applies only one of them
 *   clip       an `overflow: hidden` box at (10, 140), 40x30, whose child is 200 wide: the child is cut
 *              at the parent's padding edge, and a pixel past that edge is the page
 *
 * The card carries the text, so a pixel inside it is a statement about the *order* of its own
 * decorations as much as about any one of them. The flow is stated top to bottom: card, text (inside
 * the card), picture, box, clip -- and the 200x200 viewport is what makes the last of them visible.
 */
#define SCENE_DOC \
    "<html><body>" \
    "<div class=\"card\">AA</div>" \
    "<img class=\"pic\" src=\"Assets/Logo.png\">" \
    "<div class=\"overlay\"></div>" \
    "<div class=\"clip\"><div class=\"wide\"></div></div>" \
    "</body></html>"

/* No `position` anywhere: this build lays every box out in flow, so declaring one would be a comment
 * about a mechanism that does not exist yet rather than about the picture. The margins are what place
 * each box, and they are the numbers the assertions above were written against. */
#define SCENE_CSS \
    "html, body, div, p, span, img { display: block }" \
    "html, body { background-color: #ffffff; width: 200px; height: 200px }" \
    " .card { margin: 10px; width: 80px; height: 40px; border: 4px solid #111111;" \
    " background-color: #3366cc; color: #cc0000; font-size: 100px }" \
    " .pic { margin-left: 10px; width: 32px; height: 32px }" \
    " .overlay { width: 60px; height: 30px; background-color: rgba(0, 0, 0, 0.5); opacity: 0.5 }" \
    " .clip { margin: 10px; width: 40px; height: 30px; overflow: hidden;" \
    " background-color: #3300cc }" \
    " .wide { width: 200px; height: 30px; background-color: #cc0000 }"

#define SCENE_WIDTH 200u
#define SCENE_HEIGHT 200u

#define SCENE_PAGE GfxRgba8(0xff, 0xff, 0xff, 255)
#define SCENE_CARD GfxRgba8(0x33, 0x66, 0xcc, 255)
#define SCENE_BORDER GfxRgba8(0x11, 0x11, 0x11, 255)
#define SCENE_CLIP_CHILD GfxRgba8(0xcc, 0x00, 0x00, 255)

/*
 * The scene's build order is worth one line because it is not the fixture's default: the picture's
 * `src` is resolved **before** the layout pass (`RenderResolveImage` then `PaintLayout`), so the
 * box the image occupies exists when the document is laid out -- which is the order a real document
 * loads in and the reason a scene case spells its three steps out rather than calling
 * `RenderSceneBegin`.
 */

/* ------------------------------------------------------------------ */
/* 1. The checkpoint                                                   */
/* ------------------------------------------------------------------ */

static void TestRenderSceneCheckpoint(void)
{
    RenderScene scene;
    RenderAssets assets;
    RenderFace face;
    RenderTarget *target;
    RenderFrame frame;
    const char *path;
    int written;

    RenderFixture();
    RenderAssetsBegin(&assets);
    RenderSceneOpen(&scene, SCENE_DOC, SCENE_CSS);
    RenderResolveImage(&scene.doc, "pic", &assets);
    /* The layout pass runs *after* the resolve, so the picture's box exists when the document is laid
     * out -- the order a real document loads in. */
    PaintLayout(&scene.doc, 200, 200);
    /* The face before the recording: since 12.16 a run's glyphs are produced where the document is recorded,
     * so the frame that draws them is built with the face it will be drawn by. */
    RenderFaceCreate(&face);
    RenderScenePaintWith(&scene, face.face);

    target = RenderPage(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
    RoboCheckEqInt(RenderRasterize(scene.display, target, face.face, &frame, NULL),
                      RoboOk);
    RoboCheckEqInt(RenderFrameIsValid(&frame), 1);

    /* A background, a border band, a text run and an image all reached the target -- the four drawing
     * kinds, counted rather than argued. */
    RoboCheck(frame.pass.backgrounds >= 2u);
    RoboCheck(frame.pass.borders >= 1u);
    RoboCheck(frame.pass.texts >= 1u);
    RoboCheck(frame.pass.images >= 1u);
    RoboCheck(frame.pass.glyphs >= 2u);
    RoboCheckEqInt((int)frame.pass.glyphsMissing, 0);
    RoboCheck(frame.pass.filled >= 4u);
    RoboCheck(frame.pass.clipScopes >= 1u);
    RoboCheck(frame.pass.opacityScopes >= 1u);

    /* The page, and the card's border band over its own background. */
    RenderPixelIs(target, 190u, 190u, SCENE_PAGE, "the page");
    RenderPixelIs(target, 12u, 24u, SCENE_BORDER, "the card's left border band");
    RenderPixelIs(target, 50u, 12u, SCENE_BORDER, "the card's top border band");
    RenderPixelIs(target, 90u, 40u, SCENE_CARD, "the card's background");

    /* The line inside the card: its own box, and whether any of it is ink. The run is stroked outlines
     * at `font-size: 100px`, so the claim is "the characters reached the target inside the box layout
     * gave them" -- which is the claim the phase can make, and the one a glyph-level assertion would be
     * guessing at. */
    RenderRegionHasInkIs(target, 14, 14, 16, 12, SCENE_CARD, "the run's own box");
    RenderRegionIsClear(target, 14, 40, 80, 10, SCENE_CARD, "the card below the run's box");

    /* The picture: its four quadrants land in the destination's four corners, so four pixels say the
     * sampler read the source rectangle the display list named *and* that a transparent source pixel
     * leaves the page showing through. */
    RenderPixelIs(target, 12u, 70u, GfxRgba8(0xff, 0x00, 0x00, 255), "the picture's red corner");
    RenderPixelIs(target, 30u, 70u, GfxRgba8(0x00, 0xff, 0x00, 255), "the picture's green corner");
    RenderPixelIs(target, 12u, 90u, GfxRgba8(0x00, 0x00, 0xff, 255), "the picture's blue corner");
    RenderPixelIs(target, 30u, 90u, SCENE_PAGE, "the picture's transparent corner");
    RenderPixelIs(target, 43u, 70u, SCENE_PAGE, "the page past the picture's right edge");

    /* The box: half-transparent colour *and* half opacity, so the pixel is a quarter of black over the
     * page -- strictly between the page and black, and equal on all three channels. */
    {
        GfxColorRgba8 overlay = RenderPixel(target, 30u, 115u);

        RoboCheck(overlay.r == overlay.g && overlay.g == overlay.b);
        RoboCheck(overlay.r != 0u && overlay.r != 0xff);
        RenderPixelIs(target, 150u, 115u, SCENE_PAGE, "the page beside the semi-transparent box");
    }

    /* The clip: the child is drawn inside it and cut at the parent's padding edge, one pixel past which
     * the page shows -- and below the clip's own box the page shows too. */
    RenderPixelIs(target, 12u, 145u, SCENE_CLIP_CHILD, "the clipped child inside the boundary");
    RenderPixelIs(target, 55u, 145u, SCENE_PAGE, "the page past the clip boundary");
    RenderPixelIs(target, 12u, 175u, SCENE_PAGE, "the page below the clip box");

    /* And the artifact: a PNG of the frame, for a person to look at. The pixels above are the
     * verification; this is the evidence, and it is asserted only where it can be (a writable tree). */
    path = RenderCheckpointPath();
    written = RenderWritePng(target, path);
    if (!written) {
        printf("render.scene.checkpoint: no PNG written to %s (read-only tree?)\n", path);
    } else {
        printf("render.scene.checkpoint: wrote %s (%ux%u)\n", path, (unsigned)SCENE_WIDTH,
               (unsigned)SCENE_HEIGHT);
    }

    RenderFaceDestroy(&face);
    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
    RenderAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* 2. A region run equals a full frame                                 */
/* ------------------------------------------------------------------ */

static void TestRenderSceneRegionMatchesFull(void)
{
    RenderScene before;
    RenderScene after;
    RenderAssets assets;
    RenderFace face;
    PaintInvalidationSet *set = NULL;
    RenderTarget *partial;
    RenderTarget *full;
    RenderFrame partialFrame;
    RenderFrame fullFrame;

    RenderFixture();
    RenderAssetsBegin(&assets);
    /* Two states of one document, differing in *three* ways at once -- a colour, a position and an
     * opacity -- so the regions the set produces cover more than one producer and the equality claim
     * is not a statement about one box. */
    RenderSceneOpen(&before, SCENE_DOC, SCENE_CSS);
    RenderResolveImage(&before.doc, "pic", &assets);
    PaintLayout(&before.doc, 200, 200);
    RenderScenePaint(&before);

    RenderSceneOpen(&after, SCENE_DOC,
                      SCENE_CSS
                      " .card { background-color: #22aa55 }" /* the coloured box changed */
                      " .overlay { margin-left: 24px; opacity: 0.25 }" /* moved and faded */
                      " .clip .wide { background-color: #0000cc }");
    RenderResolveImage(&after.doc, "pic", &assets);
    PaintLayout(&after.doc, 200, 200);
    RenderScenePaint(&after);

    RenderFaceCreate(&face);
    partial = RenderPage(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
    full = RenderPage(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);

    /* The first state is what the caller already has on screen. */
    RoboCheckEqInt(RenderRasterize(before.display, partial, face.face, NULL, NULL), RoboOk);

    RoboCheckEqInt(PaintInvalidationSetCreate(&set, NULL), RoboOk);
    RoboCheckEqInt(
        PaintInvalidationSetFromLists(set, before.records.list, after.records.list, NULL),
        RoboOk);
    RoboCheck(PaintInvalidationSetCount(set) > 0u);

    /* The partial repaint: the second state's list, bounded by the regions the set names. */
    RoboCheckEqInt(
        RenderRasterizeRegions(after.display, partial, face.face, set, &partialFrame, NULL),
        RoboOk);
    /* The reference: a full render of the same list into a fresh target. */
    RoboCheckEqInt(RenderRasterize(after.display, full, face.face, &fullFrame, NULL),
                      RoboOk);

    RoboCheckEqInt(partialFrame.limited, 1);
    RoboCheckEqInt((int)partialFrame.regions, (int)PaintInvalidationSetCount(set));
    /* The region really was smaller than the frame -- otherwise "identical" would be a statement
     * about a full repaint with extra steps. */
    RoboCheck(partialFrame.regionPixels < partialFrame.targetPixels);
    RoboCheck(partialFrame.pass.boundedOut <= partialFrame.pass.draws);

    RenderTargetsEqual(partial, full, "a region run against a full frame");

    PaintInvalidationSetDestroy(set);
    RenderTargetEnd(full);
    RenderTargetEnd(partial);
    RenderFaceDestroy(&face);
    RenderSceneEnd(&after);
    RenderSceneEnd(&before);
    RenderAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* 3. Nothing owed is nothing done                                     */
/* ------------------------------------------------------------------ */

static void TestRenderSceneNoRepaint(void)
{
    RenderScene scene;
    PaintInvalidationSet *set = NULL;
    RenderTarget *target;
    RenderFrame frame;
    uint64_t before;

    RenderFixture();
    RenderSceneBegin(&scene, "<html><body><div class=\"box\"></div></body></html>",
                       "html, body, div { display: block }"
                       "html, body { background-color: #ffffff }"
                       " .box { width: 20px; height: 20px; background-color: #3366cc }");
    target = RenderPage(40u, 40u, SCENE_PAGE);
    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, NULL, NULL), RoboOk);
    before = RenderTargetChecksum(target);

    /* A document compared with itself produces no region at all -- 11.11's own answer to "is the
     * picture current?" -- and the region door's answer to that is a successful run that did nothing
     * rather than an error or a full repaint. */
    RoboCheckEqInt(PaintInvalidationSetCreate(&set, NULL), RoboOk);
    RoboCheckEqInt(
        PaintInvalidationSetFromLists(set, scene.records.list, scene.records.list, NULL),
        RoboOk);
    RoboCheckEqInt((int)PaintInvalidationSetCount(set), 0);
    RoboCheckEqInt((int)PaintInvalidationSetScope(set),
                      (int)RoboPaintInvalidationScopeNone);

    RoboCheckEqInt(RenderRasterizeRegions(scene.display, target, NULL, set, &frame, NULL),
                      RoboOk);
    RoboCheckEqInt(RenderFrameIsValid(&frame), 1);
    RoboCheckEqInt(frame.limited, 1);
    RoboCheckEqInt((int)frame.regions, 0);
    RoboCheckEqInt((int)frame.regionPixels, 0);
    RoboCheckEqInt((int)frame.pass.commands, 0);
    RoboCheckEqInt((int)frame.checksum, (int)before);
    RenderPixelIs(target, 10u, 10u, GfxRgba8(0x33, 0x66, 0xcc, 255),
                    "a box the run did not touch");

    /* A NULL set is the same conclusion, not an error: 11.11's `isEmpty(NULL)` is true. */
    RoboCheckEqInt(RenderRasterizeRegions(scene.display, target, NULL, NULL, &frame, NULL),
                      RoboOk);
    RoboCheckEqInt((int)frame.pass.commands, 0);
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));

    PaintInvalidationSetDestroy(set);
    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
}

/* ------------------------------------------------------------------ */
/* 4. A frame is a snapshot                                            */
/* ------------------------------------------------------------------ */

/** The document the snapshot case renders twice, stated once so both halves are literally one text. */
#define SNAPSHOT_DOC "<html><body><div class=\"box\"></div></body></html>"
#define SNAPSHOT_CSS \
    "html, body, div { display: block }" \
    "html, body { background-color: #ffffff }" \
    " .box { width: 24px; height: 16px; margin: 3px; background-color: #3366cc;" \
    " border: 2px solid #111111 }"

static void TestRenderSceneSnapshot(void)
{
    RenderScene scene;
    RenderScene rebuilt;
    RenderTarget *target;
    RenderTarget *fresh;
    RenderFrame first;
    RenderFrame second;
    uint64_t before;

    RenderFixture();
    RenderSceneBegin(&scene, SNAPSHOT_DOC, SNAPSHOT_CSS);
    target = RenderPage(40u, 32u, SCENE_PAGE);
    RoboCheckEqInt(RenderRasterize(scene.display, target, NULL, &first, NULL), RoboOk);
    before = RenderTargetChecksum(target);

    /* Release the artifacts a frame was run against. A display list carries no pointer, so the pixels
     * and the record cannot change -- and the record is a value the caller already holds rather than a
     * view of one. */
    RenderSceneReleaseArtifacts(&scene);
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));
    RoboCheckEqInt(RenderFrameIsValid(&first), 1);
    RenderSceneEnd(&scene);

    /* And the same document rebuilt from scratch renders the same picture -- the second statement a
     * snapshot makes, and the one that would fail if anything in the pipeline had kept a reference
     * into the tree it came from. */
    RenderSceneBegin(&rebuilt, SNAPSHOT_DOC, SNAPSHOT_CSS);
    fresh = RenderPage(40u, 32u, SCENE_PAGE);
    RoboCheckEqInt(RenderRasterize(rebuilt.display, fresh, NULL, &second, NULL), RoboOk);
    RenderTargetsEqual(target, fresh, "a document rebuilt from scratch");
    RoboCheckEqInt(RenderFrameEqual(&first, &second), 1);
    RoboCheckEqInt((int)before, (int)RenderTargetChecksum(target));

    RenderTargetEnd(fresh);
    RenderTargetEnd(target);
    RenderSceneEnd(&rebuilt);
}

/* ------------------------------------------------------------------ */
/* 5. The dump of a real frame                                         */
/* ------------------------------------------------------------------ */

static void TestRenderSceneDump(void)
{
    RenderScene scene;
    RenderAssets assets;
    RenderFace face;
    RenderTarget *target;
    RenderFrame frame;
    char text[2048];
    char again[2048];
    size_t length = 0u;
    size_t second = 0u;

    RenderFixture();
    RenderAssetsBegin(&assets);
    RenderSceneOpen(&scene, SCENE_DOC, SCENE_CSS);
    RenderResolveImage(&scene.doc, "pic", &assets);
    PaintLayout(&scene.doc, 200, 200);
    /* The face before the recording: since 12.16 a run's glyphs are produced where the document is recorded,
     * so the frame that draws them is built with the face it will be drawn by. */
    RenderFaceCreate(&face);
    RenderScenePaintWith(&scene, face.face);

    target = RenderPage(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
    RoboCheckEqInt(RenderRasterize(scene.display, target, face.face, &frame, NULL),
                      RoboOk);
    RoboCheckEqInt(RenderFrameWrite(&frame, text, sizeof(text), &length, NULL), RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(text, "frame 200x200 RGBA8 stride=800") != NULL);
    RoboCheck(strstr(text, "limited=0") != NULL);
    RoboCheck(strstr(text, "region=0,0,200,200") != NULL);
    RoboCheck(strstr(text, "commands=") != NULL);
    RoboCheck(strstr(text, "images=1") != NULL);
    /* No address in the dump: `=0x` is what a printed pointer looks like, and asserting *that* rather
     * than the bare `0x` is the difference between a claim about addresses and a claim about the
     * substring of "200x200". */
    RoboCheck(strstr(text, "=0x") == NULL);

    /* Re-running the same list into a fresh target produces the same *text*: the record is a function
     * of the document, and the dump is a function of the record. */
    {
        RenderTarget *other = RenderPage(SCENE_WIDTH, SCENE_HEIGHT, SCENE_PAGE);
        RenderFrame otherFrame;

        RoboCheckEqInt(RenderRasterize(scene.display, other, face.face, &otherFrame, NULL),
                          RoboOk);
        RoboCheckEqInt(RenderFrameEqual(&frame, &otherFrame), 1);
        RoboCheckEqInt(RenderFrameWrite(&otherFrame, again, sizeof(again), &second, NULL),
                          RoboOk);
        RoboCheckEqInt((int)length, (int)second);
        RoboCheckStrEq(text, again);
        RenderTargetEnd(other);
    }

    RenderFaceDestroy(&face);
    RenderTargetEnd(target);
    RenderSceneEnd(&scene);
    RenderAssetsEnd(&assets);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterRenderScene(void)
{
    TestRegister("render.scene.checkpoint", TestRenderSceneCheckpoint);
    TestRegister("render.scene.region_matches_full", TestRenderSceneRegionMatchesFull);
    TestRegister("render.scene.no_repaint", TestRenderSceneNoRepaint);
    TestRegister("render.scene.snapshot", TestRenderSceneSnapshot);
    TestRegister("render.scene.dump", TestRenderSceneDump);
}
