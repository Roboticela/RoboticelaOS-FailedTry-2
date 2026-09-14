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
    ROBO_GFX_CLIP_LEFT = 1,
    ROBO_GFX_CLIP_RIGHT = 2,
    ROBO_GFX_CLIP_TOP = 4,
    ROBO_GFX_CLIP_BOTTOM = 8
};

robo_gfx_line_t robo_gfx_line(robo_gfx_point_t start, robo_gfx_point_t end)
{
    robo_gfx_line_t line;
    line.start = start;
    line.end = end;
    return line;
}

static int surface_can_write(const robo_gfx_surface_t *surface)
{
    robo_gfx_surface_access_t a;

    if (!surface) {
        return 0;
    }
    a = robo_gfx_surface_access(surface);
    return a == ROBO_GFX_SURFACE_ACCESS_WRITE ||
           a == ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
}

static unsigned clip_code(int64_t x,
                          int64_t y,
                          int64_t xmin,
                          int64_t ymin,
                          int64_t xmax,
                          int64_t ymax)
{
    unsigned c = 0u;
    if (x < xmin) {
        c |= ROBO_GFX_CLIP_LEFT;
    } else if (x > xmax) {
        c |= ROBO_GFX_CLIP_RIGHT;
    }
    if (y < ymin) {
        c |= ROBO_GFX_CLIP_TOP;
    } else if (y > ymax) {
        c |= ROBO_GFX_CLIP_BOTTOM;
    }
    return c;
}

/* Divide toward -inf for consistent edge intersections (int64). */
static int64_t div_floor(int64_t num, int64_t den)
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
static int clip_line_inclusive(int32_t *x0,
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
    int64_t x_min = xmin;
    int64_t y_min = ymin;
    int64_t x_max = xmax;
    int64_t y_max = ymax;
    unsigned c0;
    unsigned c1;

    for (;;) {
        c0 = clip_code(ax, ay, x_min, y_min, x_max, y_max);
        c1 = clip_code(bx, by, x_min, y_min, x_max, y_max);
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

            if (out & ROBO_GFX_CLIP_TOP) {
                if (dy == 0) {
                    return 0;
                }
                x = ax + div_floor(dx * (y_min - ay), dy);
                y = y_min;
            } else if (out & ROBO_GFX_CLIP_BOTTOM) {
                if (dy == 0) {
                    return 0;
                }
                x = ax + div_floor(dx * (y_max - ay), dy);
                y = y_max;
            } else if (out & ROBO_GFX_CLIP_RIGHT) {
                if (dx == 0) {
                    return 0;
                }
                y = ay + div_floor(dy * (x_max - ax), dx);
                x = x_max;
            } else { /* LEFT */
                if (dx == 0) {
                    return 0;
                }
                y = ay + div_floor(dy * (x_min - ax), dx);
                x = x_min;
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

static robo_result_t write_pixel_packed(robo_gfx_surface_t *surface,
                                        uint32_t x,
                                        uint32_t y,
                                        const uint8_t *packed,
                                        uint8_t bpp)
{
    uint8_t *px = NULL;
    robo_result_t res = robo_gfx_surface_pixel(surface, x, y, &px);
    if (res != ROBO_OK) {
        return res;
    }
    memcpy(px, packed, bpp);
    return ROBO_OK;
}

typedef struct line_plot_ctx {
    robo_gfx_surface_t *surface;
    const uint8_t *packed;
    uint8_t bpp;
    robo_gfx_color_rgba8_t color;
    uint8_t opacity;
    int blend;
} line_plot_ctx_t;

static robo_result_t plot_line_pixel(line_plot_ctx_t *ctx, uint32_t x, uint32_t y)
{
    if (ctx->blend) {
        return robo_gfx_surface_composite_pixel(ctx->surface, x, y, ctx->color,
                                                ctx->opacity);
    }
    return write_pixel_packed(ctx->surface, x, y, ctx->packed, ctx->bpp);
}

static robo_result_t bresenham_plot(line_plot_ctx_t *ctx,
                                    int32_t x0,
                                    int32_t y0,
                                    int32_t x1,
                                    int32_t y1)
{
    int64_t dx;
    int64_t dy;
    int64_t abs_dx;
    int64_t abs_dy;
    int32_t sx;
    int32_t sy;
    int64_t err;
    int32_t x;
    int32_t y;
    robo_result_t res;

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
    abs_dx = dx >= 0 ? dx : -dx;
    abs_dy = dy >= 0 ? dy : -dy;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    x = x0;
    y = y0;

    res = plot_line_pixel(ctx, (uint32_t)x, (uint32_t)y);
    if (res != ROBO_OK) {
        return res;
    }
    if (x0 == x1 && y0 == y1) {
        return ROBO_OK;
    }

    if (abs_dx >= abs_dy) {
        err = abs_dx / 2;
        while (x != x1) {
            err -= abs_dy;
            if (err < 0) {
                y += sy;
                err += abs_dx;
            }
            x += sx;
            res = plot_line_pixel(ctx, (uint32_t)x, (uint32_t)y);
            if (res != ROBO_OK) {
                return res;
            }
        }
    } else {
        err = abs_dy / 2;
        while (y != y1) {
            err -= abs_dx;
            if (err < 0) {
                x += sx;
                err += abs_dy;
            }
            y += sy;
            res = plot_line_pixel(ctx, (uint32_t)x, (uint32_t)y);
            if (res != ROBO_OK) {
                return res;
            }
        }
    }
    return ROBO_OK;
}

robo_result_t robo_gfx_surface_draw_line_clipped(robo_gfx_surface_t *surface,
                                                 robo_gfx_point_t start,
                                                 robo_gfx_point_t end,
                                                 robo_gfx_color_rgba8_t color,
                                                 robo_gfx_rect_t clip,
                                                 uint8_t opacity,
                                                 int composite)
{
    robo_gfx_rect_t bounds;
    robo_gfx_rect_t effective;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t xmax;
    int32_t ymax;
    uint8_t packed[8];
    uint8_t bpp = 0u;
    line_plot_ctx_t ctx;
    robo_result_t res;
    int blend;

    if (!surface || !surface_can_write(surface)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(clip)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_pixel_format_is_implemented(robo_gfx_surface_format(surface))) {
        return ROBO_NOT_SUPPORTED;
    }

    res = robo_gfx_surface_pixel_bounds(surface, &bounds);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_gfx_rect_clip(clip, bounds, &effective);
    if (res != ROBO_OK) {
        return res;
    }
    if (robo_gfx_rect_is_empty(effective)) {
        return ROBO_OK;
    }

    xmax = effective.x + effective.width - 1;
    ymax = effective.y + effective.height - 1;
    x0 = start.x;
    y0 = start.y;
    x1 = end.x;
    y1 = end.y;

    if (!clip_line_inclusive(&x0, &y0, &x1, &y1, effective.x, effective.y, xmax,
                             ymax)) {
        return ROBO_OK;
    }

    blend = composite != 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.surface = surface;
    ctx.color = color;
    ctx.opacity = opacity;
    ctx.blend = blend;

    if (!blend) {
        res = robo_gfx_pixel_format_bytes_per_pixel(
            robo_gfx_surface_format(surface), &bpp);
        if (res != ROBO_OK || bpp == 0u || bpp > sizeof(packed)) {
            return ROBO_NOT_SUPPORTED;
        }
        res = robo_gfx_pixel_pack(robo_gfx_surface_format(surface), color, packed,
                                  sizeof(packed));
        if (res != ROBO_OK) {
            return res;
        }
        ctx.packed = packed;
        ctx.bpp = bpp;
    }

    return bresenham_plot(&ctx, x0, y0, x1, y1);
}

robo_result_t robo_gfx_surface_draw_line(robo_gfx_surface_t *surface,
                                         robo_gfx_point_t start,
                                         robo_gfx_point_t end,
                                         robo_gfx_color_rgba8_t color)
{
    robo_gfx_rect_t bounds;
    robo_result_t res;

    if (!surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_surface_pixel_bounds(surface, &bounds);
    if (res != ROBO_OK) {
        return res;
    }
    /* Goal 6.7: always replace, never blend. */
    return robo_gfx_surface_draw_line_clipped(surface, start, end, color, bounds,
                                              255u, 0);
}

robo_result_t robo_gfx_surface_draw_line_seg(robo_gfx_surface_t *surface,
                                             robo_gfx_line_t line,
                                             robo_gfx_color_rgba8_t color)
{
    return robo_gfx_surface_draw_line(surface, line.start, line.end, color);
}
