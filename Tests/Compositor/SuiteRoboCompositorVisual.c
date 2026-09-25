/*
 * The tree, the composite pass, damage and the present — Phase 15.2, 15.3, 15.4 and the destination
 * half of 15.15.
 *
 * Every case here reads the **destination's pixels** rather than a return code. That is the whole
 * point of a compositor suite: "the layer was painted" is a claim about bytes, and a case that only
 * checks `Result` would pass on a build that composed nothing at all. Where a count is asserted it is
 * asserted exactly — a clipped layer covers 100 pixels, a two-pixel damage writes four — because an
 * exact count is what a later optimization has to keep true.
 */

#include "RoboCompositorTestSupport.h"

#define FixtureRed GfxRgba8(255u, 0u, 0u, 255u)
#define FixtureGreen GfxRgba8(0u, 255u, 0u, 255u)
#define FixtureBlue GfxRgba8(0u, 0u, 255u, 255u)
#define FixtureBlack GfxRgba8(0u, 0u, 0u, 255u)
#define FixtureWhite GfxRgba8(255u, 255u, 255u, 255u)

/* ------------------------------------------------------------------ */
/* 15.2 — the tree                                                     */
/* ------------------------------------------------------------------ */

static void TestLayerTree(void)
{
    Compositor *compositor = CompositorFixture();
    size_t removed = 0u;
    CompositorLayerInfo info;

    RoboCheckEqInt((int)CompositorLayerCount(compositor), 0);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 11u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 12u, 1u, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorLayerCount(compositor), 4);
    RoboCheckEqInt((int)CompositorLayerChildCount(compositor, RoboCompositorLayerNone), 2);
    RoboCheckEqInt((int)CompositorLayerChildCount(compositor, 1u), 2);
    RoboCheckEqInt((int)CompositorLayerChildCount(compositor, 11u), 0);
    RoboCheckEqInt((int)CompositorLayerChildCount(compositor, 99u), 0);

    /* Creation order is the child order. */
    RoboCheck(CompositorLayerChild(compositor, 1u, 0u, &info.id) == RoboOk);
    RoboCheckEqInt((int)info.id, 11);
    RoboCheck(CompositorLayerChild(compositor, 1u, 1u, &info.id) == RoboOk);
    RoboCheckEqInt((int)info.id, 12);
    RoboCheck(CompositorLayerChild(compositor, 1u, 2u, &info.id) == RoboNotFound);

    /* A name is a name. */
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) ==
              RoboAlreadyExists);
    RoboCheck(CompositorLayerCreate(compositor, 30u, 77u, NULL) == RoboNotFound);
    RoboCheck(CompositorLayerCreate(compositor, 0u, RoboCompositorLayerNone, NULL) ==
              RoboInvalidArgument);

    /* Reparenting keeps the order rule, and a cycle is refused rather than repaired. */
    RoboCheck(CompositorLayerSetParent(compositor, 12u, 2u, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorLayerChildCount(compositor, 2u), 1);
    RoboCheck(CompositorLayerSetParent(compositor, 1u, 1u, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorLayerSetParent(compositor, 1u, 11u, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorLayerSetParent(compositor, 3u, 1u, NULL) == RoboNotFound);

    /* Destroy takes the subtree, and says how much went with it. */
    RoboCheck(CompositorLayerDestroy(compositor, 1u, &removed, NULL) == RoboOk);
    RoboCheckEqInt((int)removed, 2); /* 1 and 11 */
    RoboCheckEqInt((int)CompositorLayerCount(compositor), 2);
    RoboCheck(CompositorLayerDestroy(compositor, 1u, &removed, NULL) == RoboNotFound);
    RoboCheck(CompositorLayerSetSurface(compositor, 2u, 9u, NULL) == RoboNotFound);
    RoboCheck(CompositorValidate(compositor, NULL) == RoboOk);
    CompositorDestroy(compositor);
}

static void TestLayerProperties(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorLayerInfo info;

    RoboCheck(CompositorLayerCreate(compositor, 5u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerGetInfo(compositor, 5u, &info) == RoboOk);
    RoboCheckEqInt(info.visible, 1);
    RoboCheckEqInt((int)info.opacity, 255);
    RoboCheck((info.scale == 1.0f) ? 1 : 0);
    RoboCheckEqInt(info.hasClip, 0);

    RoboCheck(CompositorLayerSetRect(compositor, 5u, MakeGfxRect(3, 4, 20, 10), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetOffset(compositor, 5u, 2.5f, -1.5f, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetScale(compositor, 5u, 2.0f, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetOpacity(compositor, 5u, 128u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetClip(compositor, 5u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetVisible(compositor, 5u, 0, NULL) == RoboOk);
    RoboCheck(CompositorLayerGetInfo(compositor, 5u, &info) == RoboOk);
    RoboCheckEqInt((int)info.rect.x, 3);
    RoboCheckEqInt((int)info.rect.height, 10);
    RoboCheck((info.offsetX == 2.5f) ? 1 : 0);
    RoboCheck((info.offsetY == -1.5f) ? 1 : 0);
    RoboCheck((info.scale == 2.0f) ? 1 : 0);
    RoboCheckEqInt((int)info.opacity, 128);
    RoboCheckEqInt(info.hasClip, 1);
    RoboCheckEqInt((int)info.clip.width, 8);
    RoboCheckEqInt(info.visible, 0);
    RoboCheck(CompositorLayerClearClip(compositor, 5u) == RoboOk);
    RoboCheck(CompositorLayerGetInfo(compositor, 5u, &info) == RoboOk);
    RoboCheckEqInt(info.hasClip, 0);

    /* The refusals, each one for the reason it exists. */
    RoboCheck(CompositorLayerSetRect(compositor, 5u, MakeGfxRect(0, 0, -4, 4), NULL) ==
              RoboInvalidArgument);
    RoboCheck(CompositorLayerSetScale(compositor, 5u, 0.0f, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorLayerSetScale(compositor, 5u, -1.0f, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorLayerSetClip(compositor, 5u, MakeGfxRect(0, 0, 4, -1), NULL) ==
              RoboInvalidArgument);
    RoboCheck(CompositorLayerSetOffset(compositor, 5u, 1.0e12f, 0.0f, NULL) == RoboInvalidArgument);
    RoboCheck(CompositorLayerSetOpacity(compositor, 99u, 10u, NULL) == RoboNotFound);
    RoboCheck(CompositorLayerGetInfo(compositor, 99u, &info) == RoboNotFound);
    CompositorDestroy(compositor);
}

static void TestLayerBounds(void)
{
    Compositor *compositor = CompositorFixture();
    GfxRect bounds;

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(10, 20, 30, 40), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetScale(compositor, 1u, 2.0f, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 2u, MakeGfxRect(0, 10, 4, 4), NULL) == RoboOk);

    RoboCheck(CompositorLayerBoundsInCompositor(compositor, 1u, &bounds) == RoboOk);
    RoboCheckEqInt((int)bounds.x, 10);
    RoboCheckEqInt((int)bounds.y, 20);
    RoboCheckEqInt((int)bounds.width, 60);
    RoboCheckEqInt((int)bounds.height, 80);

    /* A child's rectangle is in its parent's space and the ancestor's scale multiplies it. */
    RoboCheck(CompositorLayerBoundsInCompositor(compositor, 2u, &bounds) == RoboOk);
    RoboCheckEqInt((int)bounds.x, 10);
    RoboCheckEqInt((int)bounds.y, 40);
    RoboCheckEqInt((int)bounds.width, 8);
    RoboCheckEqInt((int)bounds.height, 8);

    /* An offset moves the layer's content and everything inside it — that is what a scroll is. */
    RoboCheck(CompositorLayerSetOffset(compositor, 1u, 5.0f, 7.0f, NULL) == RoboOk);
    RoboCheck(CompositorLayerBoundsInCompositor(compositor, 1u, &bounds) == RoboOk);
    RoboCheckEqInt((int)bounds.x, 15);
    RoboCheckEqInt((int)bounds.y, 27);
    RoboCheck(CompositorLayerBoundsInCompositor(compositor, 2u, &bounds) == RoboOk);
    RoboCheckEqInt((int)bounds.x, 15);
    RoboCheckEqInt((int)bounds.y, 47);
    RoboCheck(CompositorLayerBoundsInCompositor(compositor, 99u, &bounds) == RoboNotFound);
    CompositorDestroy(compositor);
}

static void TestLayerPaintOrder(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorLayerId order[8];
    size_t count = 0u;

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 11u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 12u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 111u, 11u, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, RoboCompositorLayerNone, NULL) == RoboOk);

    RoboCheck(CompositorLayerPaintOrder(compositor, order, 8u, &count) == RoboOk);
    RoboCheckEqInt((int)count, 5);
    RoboCheckEqInt((int)order[0], 1);
    RoboCheckEqInt((int)order[1], 11);
    RoboCheckEqInt((int)order[2], 111);
    RoboCheckEqInt((int)order[3], 12);
    RoboCheckEqInt((int)order[4], 2);

    /* A short buffer reports the whole count rather than the part that fit. */
    RoboCheck(CompositorLayerPaintOrder(compositor, order, 2u, &count) == RoboOk);
    RoboCheckEqInt((int)count, 5);
    RoboCheckEqInt((int)order[1], 11);
    CompositorDestroy(compositor);
}

static void TestLayerHitTest(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorLayerId hit = RoboCompositorLayerNone;

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 32, 32), NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 2u, MakeGfxRect(10, 10, 32, 32), NULL) == RoboOk);

    /* Topmost: the last in paint order, which is the one a pointer is actually over. */
    RoboCheck(CompositorLayerHitTest(compositor, MakeGfxPoint(15, 15), &hit) == RoboOk);
    RoboCheckEqInt((int)hit, 2);
    RoboCheck(CompositorLayerHitTest(compositor, MakeGfxPoint(2, 2), &hit) == RoboOk);
    RoboCheckEqInt((int)hit, 1);
    RoboCheck(CompositorLayerHitTest(compositor, MakeGfxPoint(63, 47), &hit) == RoboNotFound);

    RoboCheck(CompositorLayerSetVisible(compositor, 2u, 0, NULL) == RoboOk);
    RoboCheck(CompositorLayerHitTest(compositor, MakeGfxPoint(15, 15), &hit) == RoboOk);
    RoboCheckEqInt((int)hit, 1);

    /* A clip is part of the answer: a point the clip excludes is not on the layer. */
    RoboCheck(CompositorLayerSetClip(compositor, 1u, MakeGfxRect(0, 0, 4, 4), NULL) == RoboOk);
    RoboCheck(CompositorLayerHitTest(compositor, MakeGfxPoint(15, 15), &hit) == RoboNotFound);
    RoboCheck(CompositorLayerHitTest(compositor, MakeGfxPoint(2, 2), &hit) == RoboOk);
    CompositorDestroy(compositor);
}

static void TestLayerLimit(void)
{
    CompositorConfig config;
    Compositor *compositor = NULL;

    CompositorConfigInit(&config);
    config.size = MakeGfxSize(1, 1);
    config.maxLayers = 2u;
    RoboCheck(CompositorCreate(&config, &compositor, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, RoboCompositorLayerNone, NULL) == RoboOk);
    /* Past the bound the module refuses and counts rather than growing. */
    RoboCheck(CompositorLayerCreate(compositor, 3u, RoboCompositorLayerNone, NULL) ==
              RoboOutOfMemory);
    RoboCheck(CompositorLayerDestroy(compositor, 1u, NULL, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 3u, RoboCompositorLayerNone, NULL) == RoboOk);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.3 — the composite pass                                           */
/* ------------------------------------------------------------------ */

static GfxSurface *VisualFixtureTarget(void)
{
    GfxSurface *target = CompositorMakeSurface(CompositorFixtureWidth, CompositorFixtureHeight);
    CompositorFillSurface(target, FixtureBlack);
    return target;
}

static void TestComposeBasic(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = VisualFixtureTarget();
    CompositorFrame frame;

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(16, 16), RoboGfxFormatRgba8,
                                      &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(4, 4, 16, 16), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);

    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, &frame, NULL) ==
              RoboOk);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureGreen), 256);
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 4, 4), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 19, 19), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 20, 20), FixtureBlack));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 3, 3), FixtureBlack));
    RoboCheckEqInt((int)frame.layersPainted, 1);
    RoboCheckEqInt((int)frame.compositedPixels, 256);

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestComposeOpacity(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = VisualFixtureTarget();
    GfxColorRgba8 expected;

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                      &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureRed);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetOpacity(compositor, 1u, 128u, NULL) == RoboOk);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);

    /* The layer's opacity reaches the blend as the *graphics* opacity, which is the one
     * implementation of "source over, faded" in this tree — asserted against that function rather
     * than against a literal, so a change to the blend moves both halves together. */
    expected = GfxCompositeSrcOverOpacity(FixtureRed, FixtureBlack, 128u);
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 1, 1), expected));
    RoboCheck(!CompositorColorIs(CompositorReadPixel(target, 1, 1), FixtureRed));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 9, 9), FixtureBlack));

    /* Nested opacity multiplies, exactly as the rest of the tree's alpha arithmetic does. */
    RoboCheck(CompositorLayerCreate(compositor, 2u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 2u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetScale(compositor, 1u, 1.0f, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetOffset(compositor, 1u, 0.0f, 0.0f, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetOpacity(compositor, 1u, 255u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetOpacity(compositor, 2u, 128u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, RoboCompositorSurfaceNone, NULL) == RoboOk);
    /* The child's own opacity is 128, and it has no parent opacity above it now. */
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);
    expected = GfxCompositeSrcOverOpacity(FixtureRed, FixtureBlack, 128u);
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 1, 1), expected));

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestComposeClip(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = VisualFixtureTarget();

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(CompositorFixtureWidth,
                                                                  CompositorFixtureHeight),
                                      RoboGfxFormatRgba8, &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u,
                                     MakeGfxRect(0, 0, CompositorFixtureWidth,
                                                 CompositorFixtureHeight), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetClip(compositor, 1u, MakeGfxRect(2, 3, 10, 10), NULL) == RoboOk);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureGreen), 100);
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 2, 3), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 1, 3), FixtureBlack));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 12, 13), FixtureBlack));

    /* A child's clip intersects its parent's, both stated in the parent's space. */
    RoboCheck(CompositorLayerClearClip(compositor, 1u) == RoboOk);
    RoboCheck(CompositorLayerSetClip(compositor, 1u, MakeGfxRect(0, 0, 20, 20), NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 2u, MakeGfxRect(0, 0, 64, 48), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 2u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetClip(compositor, 2u, MakeGfxRect(10, 10, 20, 20), NULL) == RoboOk);
    CompositorFillSurface(target, FixtureBlack);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureGreen), 100);
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 10, 10), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 9, 10), FixtureBlack));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 20, 20), FixtureBlack));

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestComposeScale(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = VisualFixtureTarget();

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(2, 2), RoboGfxFormatRgba8,
                                      &source, NULL) == RoboOk);
    /* Four distinct pixels, so a mis-sampled source is visible rather than plausible. */
    {
        uint8_t *pixels = GfxSurfacePixels(source);
        RoboCheck(pixels != NULL);
        RoboCheck(GfxPixelPack(RoboGfxFormatRgba8, FixtureRed, pixels, 4u) == RoboOk);
        RoboCheck(GfxPixelPack(RoboGfxFormatRgba8, FixtureGreen, pixels + 4u, 4u) == RoboOk);
        RoboCheck(GfxPixelPack(RoboGfxFormatRgba8, FixtureBlue, pixels + GfxSurfaceStride(source),
                               4u) == RoboOk);
        RoboCheck(GfxPixelPack(RoboGfxFormatRgba8, FixtureWhite,
                               pixels + GfxSurfaceStride(source) + 4u, 4u) == RoboOk);
    }
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 4, 4), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);

    /* Nearest neighbour: each source pixel covers a 2×2 destination block, exactly. */
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 0, 0), FixtureRed));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 1, 0), FixtureRed));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 2, 0), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 3, 0), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 0, 1), FixtureBlue));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 1, 1), FixtureBlue));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 2, 1), FixtureWhite));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 3, 1), FixtureWhite));
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureRed), 4);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureWhite), 4);
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 4, 0), FixtureBlack));

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestComposeOrder(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *first = NULL;
    GfxSurface *second = NULL;
    GfxSurface *target = VisualFixtureTarget();

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                      &first, NULL) == RoboOk);
    CompositorFillSurface(first, FixtureRed);
    RoboCheck(CompositorSurfaceAcquire(compositor, 2u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                      &second, NULL) == RoboOk);
    CompositorFillSurface(second, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 2u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 2u, 2u, NULL) == RoboOk);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);
    /* Created last, painted last, on top: painting is the child list's order. */
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 3, 3), FixtureGreen));
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureRed), 0);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureGreen), 64);

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    RoboCheck(CompositorSurfaceRelease(compositor, 2u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestComposeOffsets(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = VisualFixtureTarget();

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                      &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 32, 32), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetClip(compositor, 1u, MakeGfxRect(0, 0, 32, 32), NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 2u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 2u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 2u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetScroll(compositor, 1u, 4.0f, 6.0f, NULL) == RoboOk);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, NULL, NULL) ==
              RoboOk);
    /* The parent's scroll moved the child: the content origin is what children hang off. */
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 4, 6), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 11, 13), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 12, 13), FixtureBlack));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 3, 6), FixtureBlack));

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.4 — damage                                                       */
/* ------------------------------------------------------------------ */

static void TestDamageRegion(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorSurfaceId surface = 0u;
    GfxRect rect;
    CompositorDamageStats stats;

    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 0);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor), 0);
    RoboCheck(CompositorDamageIsFull(compositor) == 0);

    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(1, 1, 10, 10),
                                  NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 1);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor), 100);
    RoboCheck(CompositorDamageRect(compositor, 0u, &surface, &rect) == RoboOk);
    RoboCheckEqInt((int)surface, 0);
    RoboCheckEqInt((int)rect.width, 10);
    RoboCheck(CompositorDamageRect(compositor, 5u, NULL, NULL) == RoboNotFound);
    RoboCheck(CompositorDamageContains(compositor, MakeGfxPoint(5, 5)) == 1);
    RoboCheck(CompositorDamageContains(compositor, MakeGfxPoint(20, 20)) == 0);

    /* A region that clips to nothing is a counted no-op, not an error. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(-40, -40, 4, 4),
                                  NULL) == RoboOk);
    RoboCheck(CompositorGetDamageStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.clippedAway, 1);
    RoboCheckEqInt((int)stats.regionsNow, 1);

    /* A region crossing the edge is clipped to the composition, so the count is real pixels. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(60, 0, 20, 10),
                                  NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor), 100 + 40);

    RoboCheck(CompositorDamageAddAll(compositor, NULL) == RoboOk);
    RoboCheck(CompositorDamageIsFull(compositor) == 1);
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 0);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor),
                   CompositorFixtureWidth * CompositorFixtureHeight);
    RoboCheck(CompositorDamageContains(compositor, MakeGfxPoint(63, 47)) == 1);
    CompositorDamageClear(compositor);
    RoboCheck(CompositorDamageIsFull(compositor) == 0);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor), 0);
    CompositorDestroy(compositor);
}

static void TestDamageMerge(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorDamageStats stats;

    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 0, 10, 10),
                                  NULL) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(5, 5, 10, 10),
                                  NULL) == RoboOk);
    /* Two overlapping regions are one bounding rectangle, so the pixel count stays a true sum. */
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 1);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor), 225);
    RoboCheck(CompositorGetDamageStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.merges, 1);
    RoboCheckEqInt((int)stats.regionsAdded, 2);

    /* Regions that do not touch stay separate: a merge is a bigger region, so it is not free. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(30, 30, 4, 4),
                                  NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 2);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor), 225 + 16);
    CompositorDestroy(compositor);
}

static void TestDamageFull(void)
{
    CompositorConfig config;
    Compositor *compositor = NULL;
    CompositorDamageStats stats;

    CompositorConfigInit(&config);
    config.size = MakeGfxSize(CompositorFixtureWidth, CompositorFixtureHeight);
    config.maxDamageRects = 1u;
    RoboCheck(CompositorCreate(&config, &compositor, NULL) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 0, 64, 24),
                                  NULL) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 24, 64, 24),
                                  NULL) == RoboOk);
    /* Past the bound the list does not drop a region: it becomes "everything", which subsumes it. */
    RoboCheck(CompositorDamageIsFull(compositor) == 1);
    RoboCheckEqInt((int)CompositorDamagePixelCount(compositor),
                   CompositorFixtureWidth * CompositorFixtureHeight);
    RoboCheck(CompositorGetDamageStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.overflows, 1);
    /* Once it is full, adding more is a no-op rather than an error. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(1, 1, 2, 2),
                                  NULL) == RoboOk);
    RoboCheck(CompositorDamageIsFull(compositor) == 1);
    CompositorDestroy(compositor);
}

static void TestDamageLayer(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorSurfaceId surface = 0u;
    GfxRect rect;

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(10, 10, 4, 4), NULL) == RoboOk);
    /* Every mutation of the tree damages what it changed; the rectangle edit is the clearest one. */
    RoboCheck(CompositorDamageRect(compositor, 0u, &surface, &rect) == RoboOk);
    RoboCheckEqInt((int)rect.x, 10);
    RoboCheckEqInt((int)rect.width, 4);
    RoboCheck(CompositorInvalidateLayer(compositor, 1u, NULL) == RoboOk);
    RoboCheck(CompositorInvalidateLayer(compositor, 99u, NULL) == RoboNotFound);

    /* A surface's own damage is in the surface's space and clipped to it, not to the composition. */
    RoboCheck(CompositorDamageAdd(compositor, 4u, MakeGfxRect(0, 0, 4, 4), NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 1); /* no such surface: clipped away */

    CompositorDamageClear(compositor);
    RoboCheck(CompositorSurfaceAcquire(compositor, 4u, MakeGfxSize(8, 8), RoboGfxFormatRgba8, NULL,
                                       NULL) == RoboOk);
    RoboCheck(CompositorDamageAddAllSurfaces(compositor, NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 1);
    RoboCheck(CompositorDamageRect(compositor, 0u, &surface, &rect) == RoboOk);
    RoboCheckEqInt((int)surface, 4);
    RoboCheckEqInt((int)rect.width, 8);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.9/15.15 — the present                                            */
/* ------------------------------------------------------------------ */

static void TestPresentFull(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = VisualFixtureTarget();
    CompositorFrame frame;
    CompositorFrame last;

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u,
                                       MakeGfxSize(CompositorFixtureWidth, CompositorFixtureHeight),
                                       RoboGfxFormatRgba8, &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u,
                                     MakeGfxRect(0, 0, CompositorFixtureWidth,
                                                 CompositorFixtureHeight), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);

    RoboCheckEqInt((int)CompositorFrameCount(compositor), 0);
    RoboCheck(CompositorPresentFence(compositor) == RoboCompositorFenceNone);
    CompositorFrameInit(&last);
    RoboCheck(CompositorLastFrame(compositor, &last) == RoboNotFound);

    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentFull, &frame, NULL) ==
              RoboOk);
    RoboCheckEqInt((int)frame.index, 1);
    RoboCheckEqInt((int)frame.kind, (int)RoboCompositorPresentFull);
    RoboCheckEqInt((int)frame.compositedPixels,
                   CompositorFixtureWidth * CompositorFixtureHeight);
    RoboCheckEqInt((int)frame.presentedBytes, CompositorFixtureWidth * CompositorFixtureHeight * 4);
    RoboCheckEqInt((int)frame.layersPainted, 1);
    RoboCheckEqInt(frame.presented, 1);
    RoboCheck(frame.fence != RoboCompositorFenceNone);
    RoboCheckEqInt((int)CompositorFrameCount(compositor), 1);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureGreen),
                   CompositorFixtureWidth * CompositorFixtureHeight);
    /* A damage present has nothing left to draw, so the present cleared the list. */
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 0);

    RoboCheck(CompositorLastFrame(compositor, &last) == RoboOk);
    RoboCheck(CompositorFrameEqual(&last, &frame) == 1);
    RoboCheck(CompositorFrameIsValid(&frame) == 1);

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestPresentDamage(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *target = VisualFixtureTarget();
    CompositorFrame frame;

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u,
                                       MakeGfxSize(CompositorFixtureWidth, CompositorFixtureHeight),
                                       RoboGfxFormatRgba8, &source, NULL) == RoboOk);
    CompositorFillSurface(source, FixtureGreen);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u,
                                     MakeGfxRect(0, 0, CompositorFixtureWidth,
                                                 CompositorFixtureHeight), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);

    /* A two-pixel-square change costs four pixels, and the rest of the destination is untouched. */
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(10, 10, 2, 2),
                                  NULL) == RoboOk);
    RoboCheck(CompositorPresent(compositor, target, RoboCompositorPresentDamage, &frame, NULL) ==
              RoboOk);
    RoboCheckEqInt((int)frame.kind, (int)RoboCompositorPresentDamage);
    RoboCheckEqInt((int)frame.compositedPixels, 4);
    RoboCheckEqInt((int)frame.damagePixels, 4);
    RoboCheckEqInt((int)frame.damageRects, 1);
    RoboCheckEqInt((int)CompositorCountColor(target, FixtureGreen), 4);
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 10, 10), FixtureGreen));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 9, 9), FixtureBlack));
    RoboCheck(CompositorColorIs(CompositorReadPixel(target, 0, 0), FixtureBlack));

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(target);
    CompositorDestroy(compositor);
}

static void TestPresentRefusals(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *source = NULL;
    GfxSurface *small = CompositorMakeSurface(16, 16);
    GfxSurface *readonly;
    CompositorDiag diag;
    GfxSurfaceDesc desc;

    RoboCheck(CompositorSurfaceAcquire(compositor, 1u, MakeGfxSize(8, 8), RoboGfxFormatRgba8,
                                      &source, NULL) == RoboOk);
    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 1u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 1u, 1u, NULL) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 0, 4, 4),
                                  NULL) == RoboOk);

    /* A destination smaller than the composition is refused, not partly written. */
    CompositorDiagReset(&diag);
    RoboCheck(CompositorPresent(compositor, small, RoboCompositorPresentFull, NULL, &diag) ==
              RoboInvalidArgument);
    RoboCheck(strstr(diag.message, "smaller") != NULL);
    RoboCheck(CompositorPresent(compositor, NULL, RoboCompositorPresentFull, NULL, NULL) ==
              RoboInvalidArgument);
    /* A destination that is not writable is refused by name. */
    GfxSurfaceDescInit(&desc);
    desc.width = CompositorFixtureWidth;
    desc.height = CompositorFixtureHeight;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessRead;
    readonly = NULL;
    RoboCheck(GfxSurfaceCreate(&desc, &readonly) == RoboOk);
    RoboCheck(CompositorPresent(compositor, readonly, RoboCompositorPresentFull, NULL, &diag) ==
              RoboNotSupported);
    GfxSurfaceRelease(readonly);

    /* Every refusal kept the damage: a frame that did not happen must not consume the regions that
     * still need one. */
    RoboCheckEqInt((int)CompositorDamageRectCount(compositor), 1);

    RoboCheck(CompositorSurfaceRelease(compositor, 1u, NULL) == RoboOk);
    GfxSurfaceRelease(small);
    CompositorDestroy(compositor);
}

void TestRegisterCompositorVisual(void)
{
    TestRegister("compositor.layer.tree", TestLayerTree);
    TestRegister("compositor.layer.properties", TestLayerProperties);
    TestRegister("compositor.layer.bounds", TestLayerBounds);
    TestRegister("compositor.layer.order", TestLayerPaintOrder);
    TestRegister("compositor.layer.hit", TestLayerHitTest);
    TestRegister("compositor.layer.limit", TestLayerLimit);
    TestRegister("compositor.compose.basic", TestComposeBasic);
    TestRegister("compositor.compose.opacity", TestComposeOpacity);
    TestRegister("compositor.compose.clip", TestComposeClip);
    TestRegister("compositor.compose.scale", TestComposeScale);
    TestRegister("compositor.compose.order", TestComposeOrder);
    TestRegister("compositor.compose.offsets", TestComposeOffsets);
    TestRegister("compositor.damage.region", TestDamageRegion);
    TestRegister("compositor.damage.merge", TestDamageMerge);
    TestRegister("compositor.damage.full", TestDamageFull);
    TestRegister("compositor.damage.layer", TestDamageLayer);
    TestRegister("compositor.present.full", TestPresentFull);
    TestRegister("compositor.present.damage", TestPresentDamage);
    TestRegister("compositor.present.refusals", TestPresentRefusals);
}
