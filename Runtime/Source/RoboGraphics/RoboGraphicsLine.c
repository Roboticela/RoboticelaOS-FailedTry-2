#include "RoboGraphics/RoboGraphicsLine.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"

#include <stdint.h>
#include <string.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

enum {
    RoboGfxClipLeft = 1,
    RoboGfxClipRight = 2,
    RoboGfxClipTop = 4,
    RoboGfxClipBottom = 8
};

GfxLine MakeGfxLine(GfxPoint start, GfxPoint end)
{
    GfxLine line;
    line.start = start;
    line.end = end;
    return line;
}

static int SurfaceCanWrite(const GfxSurface *surface)
{
    GfxSurfaceAccess a;

    if (!surface) {
        return 0;
    }
    a = MakeGfxSurfaceAccess(surface);
    return a == RoboGfxSurfaceAccessWrite ||
           a == RoboGfxSurfaceAccessReadWrite;
}

static unsigned ClipCode(int64_t x,
                          int64_t y,
                          int64_t xmin,
                          int64_t ymin,
                          int64_t xmax,
                          int64_t ymax)
{
    unsigned c = 0u;
    if (x < xmin) {
        c |= RoboGfxClipLeft;
    } else if (x > xmax) {
        c |= RoboGfxClipRight;
    }
    if (y < ymin) {
        c |= RoboGfxClipTop;
    } else if (y > ymax) {
        c |= RoboGfxClipBottom;
    }
    return c;
}

/* Divide toward -inf for consistent edge intersections (int64). */
static int64_t DivFloor(int64_t num, int64_t den)
{
    int64_t q;
    int64_t r;
    if (den == 0) {
        return 0;
    }
    q = num / den;
    r = num % den;
    if (r != 0 && ((num < 0) ^ (den < 0))) {
        q -= 1;
    }
    return q;
}

/**
 * Cohen–Sutherland clip to inclusive pixel window [xmin,xmax]×[ymin,ymax].
 * Returns 0 if fully outside. Mutates endpoints in place.
 */
static int ClipLineInclusive(int32_t *x0,
                               int32_t *y0,
                               int32_t *x1,
                               int32_t *y1,
                               int32_t xmin,
                               int32_t ymin,
                               int32_t xmax,
                               int32_t ymax)
{
    int64_t ax = *x0;
    int64_t ay = *y0;
    int64_t bx = *x1;
    int64_t by = *y1;
    int64_t xMin = xmin;
    int64_t yMin = ymin;
    int64_t xMax = xmax;
    int64_t yMax = ymax;
    unsigned c0;
    unsigned c1;

    for (;;) {
        c0 = ClipCode(ax, ay, xMin, yMin, xMax, yMax);
        c1 = ClipCode(bx, by, xMin, yMin, xMax, yMax);
        if ((c0 | c1) == 0u) {
            if (ax < INT32_MIN || ax > INT32_MAX || ay < INT32_MIN ||
                ay > INT32_MAX || bx < INT32_MIN || bx > INT32_MAX ||
                by < INT32_MIN || by > INT32_MAX) {
                return 0;
            }
            *x0 = (int32_t)ax;
            *y0 = (int32_t)ay;
            *x1 = (int32_t)bx;
            *y1 = (int32_t)by;
            return 1;
        }
        if ((c0 & c1) != 0u) {
            return 0;
        }
        {
            unsigned out = c0 ? c0 : c1;
            int64_t x = 0;
            int64_t y = 0;
            int64_t dx = bx - ax;
            int64_t dy = by - ay;

            if (out & RoboGfxClipTop) {
                if (dy == 0) {
                    return 0;
                }
                x = ax + DivFloor(dx * (yMin - ay), dy);
                y = yMin;
            } else if (out & RoboGfxClipBottom) {
                if (dy == 0) {
                    return 0;
                }
                x = ax + DivFloor(dx * (yMax - ay), dy);
                y = yMax;
            } else if (out & RoboGfxClipRight) {
                if (dx == 0) {
                    return 0;
                }
                y = ay + DivFloor(dy * (xMax - ax), dx);
                x = xMax;
            } else { /* LEFT */
                if (dx == 0) {
                    return 0;
                }
                y = ay + DivFloor(dy * (xMin - ax), dx);
                x = xMin;
            }

            if (out == c0) {
                ax = x;
                ay = y;
            } else {
                bx = x;
                by = y;
            }
        }
    }
}

static Result WritePixelPacked(GfxSurface *surface,
                                        uint32_t x,
                                        uint32_t y,
                                        const uint8_t *packed,
                                        uint8_t bpp)
{
    uint8_t *px = NULL;
    Result res = GfxSurfacePixel(surface, x, y, &px);
    if (res != RoboOk) {
        return res;
    }
    memcpy(px, packed, bpp);
    return RoboOk;
}

typedef struct linePlotCtx {
    GfxSurface *surface;
    const uint8_t *packed;
    uint8_t bpp;
    GfxColorRgba8 color;
    uint8_t opacity;
    int blend;
} LinePlotCtx;

static Result PlotLinePixel(LinePlotCtx *ctx, uint32_t x, uint32_t y)
{
    if (ctx->blend) {
        return GfxSurfaceCompositePixel(ctx->surface, x, y, ctx->color,
                                                ctx->opacity);
    }
    return WritePixelPacked(ctx->surface, x, y, ctx->packed, ctx->bpp);
}

static Result BresenhamPlot(LinePlotCtx *ctx,
                                    int32_t x0,
                                    int32_t y0,
                                    int32_t x1,
                                    int32_t y1)
{
    int64_t dx;
    int64_t dy;
    int64_t absDx;
    int64_t absDy;
    int32_t sx;
    int32_t sy;
    int64_t err;
    int32_t x;
    int32_t y;
    Result res;

    /*
     * Canonicalize endpoints so A→B and B→A paint the same pixels
     * (classic Bresenham is not direction-symmetric).
     */
    if (x0 > x1 || (x0 == x1 && y0 > y1)) {
        int32_t tx = x0;
        int32_t ty = y0;
        x0 = x1;
        y0 = y1;
        x1 = tx;
        y1 = ty;
    }

    dx = (int64_t)x1 - (int64_t)x0;
    dy = (int64_t)y1 - (int64_t)y0;
    absDx = dx >= 0 ? dx : -dx;
    absDy = dy >= 0 ? dy : -dy;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    x = x0;
    y = y0;

    res = PlotLinePixel(ctx, (uint32_t)x, (uint32_t)y);
    if (res != RoboOk) {
        return res;
    }
    if (x0 == x1 && y0 == y1) {
        return RoboOk;
    }

    if (absDx >= absDy) {
        err = absDx / 2;
        while (x != x1) {
            err -= absDy;
            if (err < 0) {
                y += sy;
                err += absDx;
            }
            x += sx;
            res = PlotLinePixel(ctx, (uint32_t)x, (uint32_t)y);
            if (res != RoboOk) {
                return res;
            }
        }
    } else {
        err = absDy / 2;
        while (y != y1) {
            err -= absDx;
            if (err < 0) {
                x += sx;
                err += absDy;
            }
            y += sy;
            res = PlotLinePixel(ctx, (uint32_t)x, (uint32_t)y);
            if (res != RoboOk) {
                return res;
            }
        }
    }
    return RoboOk;
}

Result GfxSurfaceDrawLineClipped(GfxSurface *surface,
                                                 GfxPoint start,
                                                 GfxPoint end,
                                                 GfxColorRgba8 color,
                                                 GfxRect clip,
                                                 uint8_t opacity,
                                                 int composite)
{
    GfxRect bounds;
    GfxRect effective;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t xmax;
    int32_t ymax;
    uint8_t packed[8];
    uint8_t bpp = 0u;
    LinePlotCtx ctx;
    Result res;
    int blend;

    if (!surface || !SurfaceCanWrite(surface)) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(clip)) {
        return RoboInvalidArgument;
    }
    if (!GfxPixelFormatIsImplemented(GfxSurfaceFormat(surface))) {
        return RoboNotSupported;
    }

    res = GfxSurfacePixelBounds(surface, &bounds);
    if (res != RoboOk) {
        return res;
    }
    res = GfxRectClip(clip, bounds, &effective);
    if (res != RoboOk) {
        return res;
    }
    if (GfxRectIsEmpty(effective)) {
        return RoboOk;
    }

    xmax = effective.x + effective.width - 1;
    ymax = effective.y + effective.height - 1;
    x0 = start.x;
    y0 = start.y;
    x1 = end.x;
    y1 = end.y;

    if (!ClipLineInclusive(&x0, &y0, &x1, &y1, effective.x, effective.y, xmax,
                             ymax)) {
        return RoboOk;
    }

    blend = composite != 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.surface = surface;
    ctx.color = color;
    ctx.opacity = opacity;
    ctx.blend = blend;

    if (!blend) {
        res = GfxPixelFormatBytesPerPixel(
            GfxSurfaceFormat(surface), &bpp);
        if (res != RoboOk || bpp == 0u || bpp > sizeof(packed)) {
            return RoboNotSupported;
        }
        res = GfxPixelPack(GfxSurfaceFormat(surface), color, packed,
                                  sizeof(packed));
        if (res != RoboOk) {
            return res;
        }
        ctx.packed = packed;
        ctx.bpp = bpp;
    }

    return BresenhamPlot(&ctx, x0, y0, x1, y1);
}

Result GfxSurfaceDrawLine(GfxSurface *surface,
                                         GfxPoint start,
                                         GfxPoint end,
                                         GfxColorRgba8 color)
{
    GfxRect bounds;
    Result res;

    if (!surface) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(surface, &bounds);
    if (res != RoboOk) {
        return res;
    }
    /* Goal 6.7: always replace, never blend. */
    return GfxSurfaceDrawLineClipped(surface, start, end, color, bounds,
                                              255u, 0);
}

Result GfxSurfaceDrawLineSeg(GfxSurface *surface,
                                             GfxLine line,
                                             GfxColorRgba8 color)
{
    return GfxSurfaceDrawLine(surface, line.start, line.end, color);
}
