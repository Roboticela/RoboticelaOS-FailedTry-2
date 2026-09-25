/*
 * The fixtures the compositor suites share — Phase 15.
 *
 * Four things, and each one exists because every case needs it:
 *
 *   a **compositor** with a size a test can reason about (64×48 by default, so a full present is
 *   3072 pixels and a test that asserts "a 2×2 change wrote 4 pixels" is about the pixels rather
 *   than about the machine);
 *
 *   **surfaces** the suites can fill and read back, so a composite's result is proved by reading the
 *   destination rather than by trusting a return code;
 *
 *   a **raster callback** that fills a region with a colour, which is what the scheduling suite needs
 *   to tell "the job ran" from "the job was deferred" — the surface's own pixels answer;
 *
 *   and **colour counting**, because most of these cases ask "how much of the destination is this
 *   colour", which is a question a screenshot answers and a return code does not.
 *
 * The default format is RGBA8 and the default size is small on purpose: a compositor suite that
 * presented 1024×768 a hundred times would be a suite nobody runs.
 */

#ifndef ROBO_COMPOSITOR_TEST_SUPPORT_H
#define ROBO_COMPOSITOR_TEST_SUPPORT_H

#include "RoboCompositor/RoboCompositor.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

/** The suite's default composition size. */
#define CompositorFixtureWidth 64
#define CompositorFixtureHeight 48

static Compositor *CompositorFixtureWithSize(GfxSize size, size_t budget)
{
    CompositorConfig config;
    Compositor *compositor = NULL;
    Result result;

    CompositorConfigInit(&config);
    config.size = size;
    config.format = RoboGfxFormatRgba8;
    config.memoryBudgetBytes = budget;
    result = CompositorCreate(&config, &compositor, NULL);
    RoboCheck(result == RoboOk);
    RoboCheck(compositor != NULL);
    return compositor;
}

static Compositor *CompositorFixture(void)
{
    return CompositorFixtureWithSize(MakeGfxSize(CompositorFixtureWidth, CompositorFixtureHeight),
                                     0u);
}

static void CompositorFixtureDispose(Compositor *compositor)
{
    CompositorDestroy(compositor);
}

static GfxSurface *CompositorMakeSurface(int32_t width, int32_t height)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    Result result;

    GfxSurfaceDescInit(&desc);
    desc.width = (uint32_t)width;
    desc.height = (uint32_t)height;
    desc.format = RoboGfxFormatRgba8;
    desc.strideBytes = 0u;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    result = GfxSurfaceCreate(&desc, &surface);
    RoboCheck(result == RoboOk);
    RoboCheck(surface != NULL);
    return surface;
}

static void CompositorFillSurface(GfxSurface *surface, GfxColorRgba8 color)
{
    uint32_t x;
    uint32_t y;
    uint8_t bpp = GfxSurfaceBytesPerPixel(surface);
    GfxPixelFormat format = GfxSurfaceFormat(surface);
    uint8_t *pixels = GfxSurfacePixels(surface);

    RoboCheck(pixels != NULL);
    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        uint8_t *row = pixels + (size_t)y * GfxSurfaceStride(surface);
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            RoboCheck(GfxPixelPack(format, color, row + (size_t)x * bpp, bpp) == RoboOk);
        }
    }
}

static GfxColorRgba8 CompositorReadPixel(GfxSurface *surface, uint32_t x, uint32_t y)
{
    const uint8_t *pixel = NULL;
    GfxColorRgba8 color = GfxRgba8(0u, 0u, 0u, 0u);

    if (GfxSurfacePixelConst(surface, x, y, &pixel) != RoboOk || !pixel) {
        return color;
    }
    RoboCheck(GfxPixelUnpack(GfxSurfaceFormat(surface), pixel, GfxSurfaceBytesPerPixel(surface),
                             &color) == RoboOk);
    return color;
}

static uint32_t CompositorCountColor(GfxSurface *surface, GfxColorRgba8 color)
{
    uint32_t x;
    uint32_t y;
    uint32_t count = 0u;

    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            GfxColorRgba8 got = CompositorReadPixel(surface, x, y);
            if (got.r == color.r && got.g == color.g && got.b == color.b && got.a == color.a) {
                count++;
            }
        }
    }
    return count;
}

static int CompositorColorIs(GfxColorRgba8 a, GfxColorRgba8 b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* ---- A raster callback that paints a region, so "the job ran" is readable in the pixels ---- */

typedef struct CompositorRasterFixture {
    Compositor *compositor;
    CompositorSurfaceId surface; /* the surface this callback paints */
    GfxColorRgba8 color;
    unsigned calls;
    unsigned refusalsAfter; /* refuse a call once this many have happened (0 = never refuse) */
    CompositorSurfaceId lastSurface;
    GfxRect lastRegion;
    CompositorJobKind lastKind;
    unsigned lastJobsRun;
} CompositorRasterFixture;

/**
 * Paint @p region of the fixture's surface with the fixture's colour.
 *
 * The point of a callback that *writes pixels* rather than one that counts calls is that a suite can
 * then tell "the job ran" from "the job was deferred" by reading the surface, which is the claim
 * 15.6 actually makes. A fixture told to refuse answers `RoboBusy`, which is what a busy buffer is.
 */
static Result CompositorRasterPaint(void *user,
                                    CompositorSurfaceId surface,
                                    GfxRect region,
                                    CompositorJobKind kind,
                                    CompositorDiag *diag)
{
    CompositorRasterFixture *fixture = (CompositorRasterFixture *)user;
    GfxSurface *gfx = NULL;
    int32_t x;
    int32_t y;

    (void)diag;
    if (!fixture) {
        return RoboInvalidArgument;
    }
    if (fixture->refusalsAfter != 0u && fixture->calls >= fixture->refusalsAfter) {
        fixture->calls++;
        return RoboBusy;
    }
    fixture->calls++;
    fixture->lastSurface = surface;
    fixture->lastRegion = region;
    fixture->lastKind = kind;
    if (surface != fixture->surface) {
        /* A job for a surface this fixture does not know about: accepted, because refusing would
         * exercise the refusal path rather than the run. */
        return RoboOk;
    }
    if (CompositorSurfaceGet(fixture->compositor, surface, &gfx) != RoboOk || !gfx) {
        return RoboNotFound;
    }
    for (y = region.y; y < region.y + region.height; y++) {
        for (x = region.x; x < region.x + region.width; x++) {
            uint8_t *pixel = NULL;
            if (x < 0 || y < 0 || (uint32_t)x >= GfxSurfaceWidth(gfx) ||
                (uint32_t)y >= GfxSurfaceHeight(gfx)) {
                continue;
            }
            if (GfxSurfacePixel(gfx, (uint32_t)x, (uint32_t)y, &pixel) == RoboOk && pixel) {
                RoboCheck(GfxPixelPack(RoboGfxFormatRgba8, fixture->color, pixel,
                                       GfxSurfaceBytesPerPixel(gfx)) == RoboOk);
            }
        }
    }
    return RoboOk;
}

#endif /* ROBO_COMPOSITOR_TEST_SUPPORT_H */
