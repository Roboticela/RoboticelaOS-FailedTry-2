/*
 * RoboGraphics glyph coverage rasterization (Phase 12.1) — see RoboGraphicsGlyph.h.
 *
 * The file is four things, in the order the header argues them:
 *
 *   the lattice       a 1/256 px quantizer and the integer division helpers the scanline walk
 *                     needs. Everything below that line is integer, which is what makes the
 *                     module's determinism a property of the code rather than of the platform's
 *                     floating point;
 *
 *   the edges         an outline becomes a list of non-horizontal fixed-point edges. Quads and
 *                     cubics are flattened adaptively — half-split until the control polygon is
 *                     flat to a quarter pixel or the depth cap is reached — and no vertex is
 *                     snapped, which is what a fractional origin needs;
 *
 *   the scanlines     `supersample × height` sub-scanlines, each intersected once with the edge
 *                     list, sorted, and walked with a non-zero winding rule. A span becomes sample
 *                     counts in the mask's own bytes, so the accumulator *is* the output buffer and
 *                     a rasterization is two allocations;
 *
 *   the model         the coverage → alpha tables and the compositing loop, which is the only
 *                     place coverage, colour alpha and raster opacity meet.
 *
 * There is no `sqrt`, `pow`, `floor`, `fabs` or `isnan` anywhere in it: the guest target is
 * freestanding with `-nostdlib`, so the standard library's math is not there to call. The flatness
 * test compares squared quantities, the quantizer clamps before it casts (the cast of an
 * out-of-range float is undefined behaviour, so the clamp is a correctness requirement and not a
 * nicety), and the sRGB transfer function is evaluated once by hand and embedded as a table rather
 * than at run time.
 */

#include "RoboGraphics/RoboGraphicsGlyph.h"

#include "RoboFont/RoboFontOutline.h"
#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The lattice                                                         */
/* ------------------------------------------------------------------ */

/**
 * Largest device coordinate this module will represent, in pixels.
 *
 * A bound rather than an overflow guard bolted on later: at 1/256 px it keeps every fixed-point
 * coordinate inside `int32`, and a scanline intersection's numerator — a coordinate delta times a
 * coordinate delta — inside `int64` with room to spare. A glyph that reaches this far outside its
 * clip is ink no caller can see, so clamping it there changes no pixel of any picture this module
 * can produce.
 */
#define GlyphMaxCoordinatePx 4000000.0f
#define GlyphMaxCoordinateFixed 1024000000

/** Curve flatness tolerance, in device pixels. A quarter pixel is below what 8×8 resolves. */
#define GlyphFlatnessPx 0.25f

/** Half-split depth cap per curve, so a pathological control polygon cannot recurse forever. */
#define GlyphMaxSubdivision 12u

/** Flattened edges one glyph may hold. Exceeding it draws nothing rather than growing forever. */
#define GlyphMaxEdges 262144u

/** Non-zero when @p v is a usable finite float; `v == v` is the freestanding NaN test. */
static int GlyphFloatOk(float v)
{
    return (v == v) && (v <= 3.402823466e+38f) && (v >= -3.402823466e+38f);
}

/** @p v, made finite and inside @ref GlyphMaxCoordinatePx. NaN becomes 0. */
static float GlyphClampCoordinate(float v)
{
    if (!GlyphFloatOk(v)) {
        return (v != v) ? 0.0f : (v > 0.0f ? GlyphMaxCoordinatePx : -GlyphMaxCoordinatePx);
    }
    if (v > GlyphMaxCoordinatePx) {
        return GlyphMaxCoordinatePx;
    }
    if (v < -GlyphMaxCoordinatePx) {
        return -GlyphMaxCoordinatePx;
    }
    return v;
}

/** @p value in device pixels → the 1/256 px lattice, rounded half away from zero. */
static int32_t GlyphQuantize(float value)
{
    float scaled = GlyphClampCoordinate(value) * (float)RoboGfxGlyphSubpixelScale;

    if (scaled >= (float)GlyphMaxCoordinateFixed) {
        return GlyphMaxCoordinateFixed;
    }
    if (scaled <= -(float)GlyphMaxCoordinateFixed) {
        return -GlyphMaxCoordinateFixed;
    }
    /* `floor` is not available; a half-unit bias plus C's truncating cast is the same thing for
     * both signs, and the magnitude is bounded above so the cast is defined. */
    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

/** floor(@p a / @p b) for a positive @p b. */
static int64_t GlyphFloorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;

    if ((a % b) != 0 && a < 0) {
        q--;
    }
    return q;
}

/** ceil(@p a / @p b) for a positive @p b. */
static int64_t GlyphCeilDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;

    if ((a % b) != 0 && a > 0) {
        q++;
    }
    return q;
}

/** @p a / @p b rounded half away from zero. @p b must be non-zero. */
static int64_t GlyphRoundDiv(int64_t a, int64_t b)
{
    if (b < 0) {
        a = -a;
        b = -b;
    }
    if (a >= 0) {
        return (a + b / 2) / b;
    }
    return -((-a + b / 2) / b);
}

/* ------------------------------------------------------------------ */
/* The edges                                                           */
/* ------------------------------------------------------------------ */

typedef struct GlyphVertex {
    int32_t x;
    int32_t y;
} GlyphVertex;

typedef struct GlyphEdge {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
} GlyphEdge;

typedef struct GlyphEdgeList {
    GlyphEdge *items;
    size_t count;
    size_t capacity;
    /** Non-zero once the edge budget was reached: the glyph draws nothing rather than half. */
    int overflow;
} GlyphEdgeList;

typedef struct GlyphBuilder {
    GlyphEdgeList edges;
    GlyphVertex subStart;
    GlyphVertex cursor;
    int haveSub;
    int32_t minX;
    int32_t minY;
    int32_t maxX;
    int32_t maxY;
    int haveBounds;
} GlyphBuilder;

static void GlyphBuilderInit(GlyphBuilder *builder)
{
    memset(builder, 0, sizeof(*builder));
}

static void GlyphBuilderDispose(GlyphBuilder *builder)
{
    RoboFree(builder->edges.items);
    builder->edges.items = NULL;
    builder->edges.count = 0u;
    builder->edges.capacity = 0u;
}

/** Include (@p x, @p y) in the builder's ink bounds. */
static void GlyphBuilderExtend(GlyphBuilder *builder, int32_t x, int32_t y)
{
    if (!builder->haveBounds) {
        builder->minX = x;
        builder->maxX = x;
        builder->minY = y;
        builder->maxY = y;
        builder->haveBounds = 1;
        return;
    }
    if (x < builder->minX) {
        builder->minX = x;
    }
    if (x > builder->maxX) {
        builder->maxX = x;
    }
    if (y < builder->minY) {
        builder->minY = y;
    }
    if (y > builder->maxY) {
        builder->maxY = y;
    }
}

/**
 * Append edge @p a → @p b.
 *
 * A **horizontal** edge is dropped rather than stored, and that is an equivalence rather than an
 * optimization: the scanline crossing test is `(y0 > y) != (y1 > y)`, which a horizontal edge can
 * never satisfy, and the winding it would contribute is zero over the whole row. Dropping it here
 * also keeps it out of the sort, where it would cost without ever mattering.
 */
static Result GlyphEdgesPush(GlyphBuilder *builder, GlyphVertex a, GlyphVertex b)
{
    GlyphEdgeList *list = &builder->edges;

    if (a.y == b.y) {
        return RoboOk;
    }
    if (list->count >= GlyphMaxEdges) {
        list->overflow = 1;
        return RoboNotSupported;
    }
    if (list->count == list->capacity) {
        size_t next = (list->capacity == 0u) ? 256u : list->capacity * 2u;
        GlyphEdge *grown;

        if (next > GlyphMaxEdges) {
            next = GlyphMaxEdges;
        }
        grown = (GlyphEdge *)Realloc(list->items, next * sizeof(GlyphEdge));
        if (!grown) {
            return RoboOutOfMemory;
        }
        list->items = grown;
        list->capacity = next;
    }
    list->items[list->count].x0 = a.x;
    list->items[list->count].y0 = a.y;
    list->items[list->count].x1 = b.x;
    list->items[list->count].y1 = b.y;
    list->count++;
    return RoboOk;
}

static Result GlyphBuilderMoveTo(GlyphBuilder *builder, GlyphVertex v)
{
    Result res;

    if (builder->haveSub) {
        res = GlyphEdgesPush(builder, builder->cursor, builder->subStart);
        if (res != RoboOk) {
            return res;
        }
    }
    GlyphBuilderExtend(builder, v.x, v.y);
    builder->subStart = v;
    builder->cursor = v;
    builder->haveSub = 1;
    return RoboOk;
}

static Result GlyphBuilderLineTo(GlyphBuilder *builder, GlyphVertex v)
{
    Result res;

    if (!builder->haveSub) {
        return GlyphBuilderMoveTo(builder, v);
    }
    GlyphBuilderExtend(builder, v.x, v.y);
    res = GlyphEdgesPush(builder, builder->cursor, v);
    if (res != RoboOk) {
        return res;
    }
    builder->cursor = v;
    return RoboOk;
}

static Result GlyphBuilderClose(GlyphBuilder *builder)
{
    Result res;

    if (!builder->haveSub) {
        return RoboOk;
    }
    builder->haveSub = 0;
    res = GlyphEdgesPush(builder, builder->cursor, builder->subStart);
    if (res != RoboOk) {
        return res;
    }
    return RoboOk;
}

/*
 * Curve flattening.
 *
 * Both tests compare **squared** quantities, because the guest target has no `sqrt`: a quadratic is
 * flat when its control point is within tolerance of the chord, and `d² ≤ tol² · len²` says exactly
 * that with `d` the cross product of the chord and the control offset. A cubic is flattened through
 * de Casteljau's midpoint split and each half is tested as its own control polygon — the standard
 * enough construction that the subdivision, not the test, is what a curve's smoothness depends on.
 *
 * The subdivision always terminates: the depth cap turns a pathological control polygon into a
 * straight line rather than into an unbounded number of segments.
 */

static int GlyphQuadIsFlat(float x0, float y0, float cx, float cy, float x1, float y1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float px = cx - x0;
    float py = cy - y0;
    float cross = px * dy - py * dx;
    float len2 = dx * dx + dy * dy;
    float d = (cross < 0.0f) ? -cross : cross;
    float tol2 = GlyphFlatnessPx * GlyphFlatnessPx;

    if (len2 <= 0.0f) {
        /* Degenerate chord: the control point's own distance is the whole error. */
        return (px * px + py * py) <= tol2;
    }
    return d * d <= tol2 * len2;
}

static int GlyphCubicIsFlat(float x0, float y0,
                            float c1x, float c1y,
                            float c2x, float c2y,
                            float x1, float y1)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float tol2 = GlyphFlatnessPx * GlyphFlatnessPx;
    float o1x = c1x - x0;
    float o1y = c1y - y0;
    float o2x = c2x - x1;
    float o2y = c2y - y1;
    float cross1 = o1x * dy - o1y * dx;
    float cross2 = o2x * dy - o2y * dx;
    float d1 = (cross1 < 0.0f) ? -cross1 : cross1;
    float d2 = (cross2 < 0.0f) ? -cross2 : cross2;

    if (len2 <= 0.0f) {
        return (o1x * o1x + o1y * o1y) <= tol2 && (o2x * o2x + o2y * o2y) <= tol2;
    }
    return (d1 * d1 <= tol2 * len2) && (d2 * d2 <= tol2 * len2);
}

static Result GlyphFlattenQuad(GlyphBuilder *builder,
                               float x0, float y0,
                               float cx, float cy,
                               float x1, float y1,
                               unsigned depth)
{
    GlyphVertex end;
    float mx, my, nx, ny, sx, sy;
    Result res;

    if (depth >= GlyphMaxSubdivision || GlyphQuadIsFlat(x0, y0, cx, cy, x1, y1)) {
        end.x = GlyphQuantize(x1);
        end.y = GlyphQuantize(y1);
        return GlyphBuilderLineTo(builder, end);
    }
    mx = 0.5f * (x0 + cx);
    my = 0.5f * (y0 + cy);
    nx = 0.5f * (cx + x1);
    ny = 0.5f * (cy + y1);
    sx = 0.5f * (mx + nx);
    sy = 0.5f * (my + ny);

    res = GlyphFlattenQuad(builder, x0, y0, mx, my, sx, sy, depth + 1u);
    if (res != RoboOk) {
        return res;
    }
    return GlyphFlattenQuad(builder, sx, sy, nx, ny, x1, y1, depth + 1u);
}

static Result GlyphFlattenCubic(GlyphBuilder *builder,
                                float x0, float y0,
                                float c1x, float c1y,
                                float c2x, float c2y,
                                float x1, float y1,
                                unsigned depth)
{
    GlyphVertex end;
    float m1x, m1y, m2x, m2y, m3x, m3y;
    float lx, ly, rx, ry, midx, midy;
    Result res;

    if (depth >= GlyphMaxSubdivision ||
        GlyphCubicIsFlat(x0, y0, c1x, c1y, c2x, c2y, x1, y1)) {
        end.x = GlyphQuantize(x1);
        end.y = GlyphQuantize(y1);
        return GlyphBuilderLineTo(builder, end);
    }

    /* de Casteljau at t = 1/2: m1, m2, m3 are the midpoints of the three chords, l and r the
     * midpoints of the outer pairs, and mid the shared split point. */
    m1x = 0.5f * (x0 + c1x);
    m1y = 0.5f * (y0 + c1y);
    m2x = 0.5f * (c1x + c2x);
    m2y = 0.5f * (c1y + c2y);
    m3x = 0.5f * (c2x + x1);
    m3y = 0.5f * (c2y + y1);
    lx = 0.5f * (m1x + m2x);
    ly = 0.5f * (m1y + m2y);
    rx = 0.5f * (m2x + m3x);
    ry = 0.5f * (m2y + m3y);
    midx = 0.5f * (lx + rx);
    midy = 0.5f * (ly + ry);

    res = GlyphFlattenCubic(builder, x0, y0, m1x, m1y, lx, ly, midx, midy, depth + 1u);
    if (res != RoboOk) {
        return res;
    }
    return GlyphFlattenCubic(builder, midx, midy, rx, ry, m3x, m3y, x1, y1, depth + 1u);
}

/* ------------------------------------------------------------------ */
/* The mask                                                            */
/* ------------------------------------------------------------------ */

struct GfxGlyphMask {
    uint32_t refs;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t *coverage;
};

static GfxGlyphMask *GlyphMaskCreate(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    GfxGlyphMask *mask;
    size_t pixels = (size_t)width * (size_t)height;

    mask = (GfxGlyphMask *)RoboCalloc(1u, sizeof(*mask));
    if (!mask) {
        return NULL;
    }
    mask->refs = 1u;
    mask->x = x;
    mask->y = y;
    mask->width = width;
    mask->height = height;
    if (pixels > 0u) {
        mask->coverage = (uint8_t *)RoboCalloc(pixels, 1u);
        if (!mask->coverage) {
            RoboFree(mask);
            return NULL;
        }
    }
    return mask;
}

void GfxGlyphMaskRetain(GfxGlyphMask *mask)
{
    if (mask) {
        mask->refs++;
    }
}

void GfxGlyphMaskRelease(GfxGlyphMask *mask)
{
    if (!mask || mask->refs == 0u) {
        return;
    }
    mask->refs--;
    if (mask->refs == 0u) {
        RoboFree(mask->coverage);
        mask->coverage = NULL;
        RoboFree(mask);
    }
}

int32_t GfxGlyphMaskX(const GfxGlyphMask *mask)
{
    return mask ? mask->x : 0;
}

int32_t GfxGlyphMaskY(const GfxGlyphMask *mask)
{
    return mask ? mask->y : 0;
}

uint32_t GfxGlyphMaskWidth(const GfxGlyphMask *mask)
{
    return mask ? mask->width : 0u;
}

uint32_t GfxGlyphMaskHeight(const GfxGlyphMask *mask)
{
    return mask ? mask->height : 0u;
}

int GfxGlyphMaskIsEmpty(const GfxGlyphMask *mask)
{
    return mask == NULL || mask->width == 0u || mask->height == 0u || mask->coverage == NULL;
}

uint8_t GfxGlyphMaskCoverageAt(const GfxGlyphMask *mask, uint32_t x, uint32_t y)
{
    if (GfxGlyphMaskIsEmpty(mask) || x >= mask->width || y >= mask->height) {
        return 0u;
    }
    return mask->coverage[(size_t)y * (size_t)mask->width + (size_t)x];
}

const uint8_t *GfxGlyphMaskCoverage(const GfxGlyphMask *mask)
{
    if (GfxGlyphMaskIsEmpty(mask)) {
        return NULL;
    }
    return mask->coverage;
}

/* ------------------------------------------------------------------ */
/* The scanlines                                                       */
/* ------------------------------------------------------------------ */

typedef struct GlyphCrossing {
    int64_t x;
    int32_t dir;
} GlyphCrossing;

/** Shell sort: allocation-free, deterministic, and comfortably fast for one glyph's crossings. */
static void GlyphSortCrossings(GlyphCrossing *items, size_t count)
{
    size_t gap;

    for (gap = count / 2u; gap > 0u; gap /= 2u) {
        size_t i;

        for (i = gap; i < count; i++) {
            GlyphCrossing tmp = items[i];
            size_t j = i;

            while (j >= gap && items[j - gap].x > tmp.x) {
                items[j] = items[j - gap];
                j -= gap;
            }
            items[j] = tmp;
        }
    }
}

/**
 * Add one inside span — @p xs to @p xe in the 1/256 lattice — to @p coverage's sample counts.
 *
 * A sample column's centre sits at `column · subDiv + subDiv/2` in the lattice, so the covered
 * columns are exactly those whose centre lies strictly inside the span, and both ends are closed
 * forms over integer division. A span with no width adds nothing, which is what makes a zero-area
 * contour cost ink no pixels.
 *
 * @p originX is the mask's own left edge in the lattice, and the two subtractions are what make a
 * column index a *mask-relative* one: the crossings arrive in device coordinates, the buffer is
 * indexed from the mask's corner, and forgetting the difference silently shifts every glyph by its
 * own bounding box. A span with no width adds nothing, which is what makes a zero-area contour cost
 * ink no pixels.
 */
static void GlyphAddSpan(uint8_t *coverage,
                         size_t rowOffset,
                         uint32_t width,
                         int64_t xs,
                         int64_t xe,
                         int64_t originX,
                         int32_t sub,
                         int32_t subDiv)
{
    int64_t half = (int64_t)subDiv / 2;
    int64_t first = GlyphFloorDiv(xs - originX - half, subDiv) + 1;
    int64_t last = GlyphCeilDiv(xe - originX - half, subDiv);
    int64_t total = (int64_t)width * (int64_t)sub;

    if (first < 0) {
        first = 0;
    }
    if (last > total) {
        last = total;
    }
    while (first < last) {
        int64_t pixel = first / sub;
        int64_t next = (pixel + 1) * sub;
        int64_t stop = (next < last) ? next : last;

        coverage[rowOffset + (size_t)pixel] += (uint8_t)(stop - first);
        first = stop;
    }
}

/**
 * The scanline walk.
 *
 * @p sub sub-scanlines per pixel row, each intersected with every edge once. The crossings are
 * sorted and walked with a non-zero winding rule, so a contour wound the other way — which is how
 * TrueType and CFF spell a counter, the hole in an `o` — *subtracts* from the winding and the span
 * it bounds is never emitted.
 */
static void GlyphRasterizeEdges(const GlyphEdgeList *edges,
                                uint8_t *coverage,
                                uint32_t width,
                                uint32_t height,
                                int32_t left,
                                int32_t top,
                                int32_t sub,
                                int32_t subDiv,
                                GlyphCrossing *crossings)
{
    int32_t rows = (int32_t)height * sub;
    int64_t originX = (int64_t)left * RoboGfxGlyphSubpixelScale;
    int32_t row;

    for (row = 0; row < rows; row++) {
        int64_t sampleY = (int64_t)top * RoboGfxGlyphSubpixelScale +
                          (int64_t)row * (int64_t)subDiv + (int64_t)subDiv / 2;
        size_t found = 0u;
        size_t i;
        int32_t winding = 0;
        int64_t spanStart = 0;
        size_t rowOffset;

        for (i = 0u; i < edges->count; i++) {
            const GlyphEdge *edge = &edges->items[i];

            if (((int64_t)edge->y0 > sampleY) == ((int64_t)edge->y1 > sampleY)) {
                continue;
            }
            {
                int64_t num = (int64_t)(edge->x1 - edge->x0) * (sampleY - (int64_t)edge->y0);
                int64_t den = (int64_t)edge->y1 - (int64_t)edge->y0;

                crossings[found].x = (int64_t)edge->x0 + GlyphRoundDiv(num, den);
                crossings[found].dir = (edge->y1 > edge->y0) ? 1 : -1;
                found++;
            }
        }
        if (found == 0u) {
            continue;
        }
        GlyphSortCrossings(crossings, found);

        rowOffset = (size_t)(row / sub) * (size_t)width;
        for (i = 0u; i < found; i++) {
            int32_t dir = crossings[i].dir;

            if (winding == 0) {
                spanStart = crossings[i].x;
            }
            winding += dir;
            if (winding == 0) {
                GlyphAddSpan(coverage, rowOffset, width, spanStart, crossings[i].x, originX,
                             sub, subDiv);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* The public rasterizer                                               */
/* ------------------------------------------------------------------ */

void GfxGlyphRasterParamsInit(GfxGlyphRasterParams *params)
{
    if (!params) {
        return;
    }
    params->gamma = RoboGfxCoverageGammaDefault;
    params->supersample = (uint8_t)RoboGfxGlyphSamplesPerPixel;
    params->rotation = (uint8_t)RoboGfxGlyphRotationNone;
    params->reserved1 = 0u;
}

/** The effective supersample of @p params: its own if it is a power of two in range, else default. */
static int GlyphSupersampleOf(const GfxGlyphRasterParams *params, int *outValid)
{
    int requested;

    *outValid = 1;
    if (!params || params->supersample == 0u) {
        return RoboGfxGlyphSamplesPerPixel;
    }
    requested = (int)params->supersample;
    if (requested != 1 && requested != 2 && requested != 4 && requested != 8 && requested != 16) {
        *outValid = 0;
        return 0;
    }
    return requested;
}

static Result GlyphOutlineStatusToResult(const FontOutline *outline)
{
    switch (MakeFontOutlineStatus(outline)) {
    case RoboFontOutlineStatusOk:
    case RoboFontOutlineStatusEmpty:
        return RoboOk;
    case RoboFontOutlineStatusInvalidId:
    case RoboFontOutlineStatusMalformed:
    case RoboFontOutlineStatusLimit:
    case RoboFontOutlineStatusOverflow:
        return RoboInvalidArgument;
    case RoboFontOutlineStatusUnavailable:
    case RoboFontOutlineStatusUnsupported:
        return RoboNotSupported;
    default:
        return RoboInvalidArgument;
    }
}

/**
 * The device transform: a scale, a baseline origin, and the quarter turns the glyph is drawn at.
 *
 * The rotation is applied **here**, in the one place a font-unit point becomes a device point, rather
 * than to a finished mask: every verb in the walk below asks these two functions (or `GlyphDevicePoint`,
 * which asks them), so a rotated glyph's control points are rotated before flattening — the curve is
 * turned, not the pixels it was flattened into.
 *
 * The transform, for a font-unit point `(fx, fy)` at @p scale `s` with the face's Y up and the device's
 * Y down, is
 *
 * ```text
 * (dx, dy) = (fx * s, -fy * s)                  // the unrotated device offset from the origin
 * (dx, dy) -> (-dy, dx)                         // applied `rotation` times: a quarter turn clockwise
 * device   = origin + (dx, dy)
 * ```
 *
 * so one turn puts the glyph's advance direction along **+y** (down the column) and its ascender towards
 * **+x**, which is what a sideways letter in a vertical run is. Two and three turns are the same
 * arithmetic applied twice and three times, and a quarter turn is exact in the 1/256 px lattice because
 * the lattice is preserved by a swap and a negation.
 */
typedef struct GlyphDevice {
    float scale;
    GfxPointF origin;
    uint8_t rotation;
} GlyphDevice;

/** The unrotated device offset of @p fontX from the origin: `fx * scale`, rotated `rotation` times. */
static void GlyphDeviceOffset(const GlyphDevice *device, FontUnit fontX, FontUnit fontY,
                              float *outX, float *outY)
{
    float dx = (float)fontX * device->scale;
    float dy = -(float)fontY * device->scale;
    uint8_t turns = (uint8_t)(device->rotation & 3u);

    for (uint8_t turn = 0u; turn < turns; turn++) {
        float previousX = dx;

        dx = -dy;
        dy = previousX;
    }
    *outX = dx;
    *outY = dy;
}

/**
 * The same point as @ref GlyphDevicePoint, **unquantized**. The curve walk needs float control points
 * (the flattening subdivides them before anything is snapped), and it needs both coordinates at once: a
 * rotated transform mixes x and y, so asking for "the device x of `fontX`" is no longer a question that
 * has an answer.
 */
static void GlyphDevicePointF(const GlyphDevice *device, FontUnit fontX, FontUnit fontY,
                              float *outX, float *outY)
{
    float x = 0.0f;
    float y = 0.0f;

    GlyphDeviceOffset(device, fontX, fontY, &x, &y);
    *outX = device->origin.x + x;
    *outY = device->origin.y + y;
}

static GlyphVertex GlyphDevicePoint(const GlyphDevice *device, FontUnit fontX, FontUnit fontY)
{
    GlyphVertex vertex;
    float x = 0.0f;
    float y = 0.0f;

    GlyphDeviceOffset(device, fontX, fontY, &x, &y);
    vertex.x = GlyphQuantize(device->origin.x + x);
    vertex.y = GlyphQuantize(device->origin.y + y);
    return vertex;
}

/**
 * Walk @p outline's contours, filling @p builder with fixed-point edges.
 *
 * The structure mirrors `GfxOutlineToPath`'s walk on purpose — same verbs, same closing rule, same
 * "a curve with no current point is malformed" refusal — because the two must agree about what an
 * outline *is*. What differs is the output: this one quantizes at the leaf of a flattening recursion
 * that no longer snaps, so a fractional origin survives all the way into the coverage.
 */
static Result GlyphBuildEdges(const FontOutline *outline,
                              const GlyphDevice *device,
                              GlyphBuilder *builder)
{
    uint32_t contourIndex;
    uint32_t contourCount = FontOutlineContourCount(outline);

    for (contourIndex = 0u; contourIndex < contourCount; contourIndex++) {
        const FontContour *contour = FontOutlineContour(outline, contourIndex);
        float curX = 0.0f;
        float curY = 0.0f;
        int haveCur = 0;
        uint32_t segIndex;
        Result res;

        if (!contour) {
            return RoboInvalidArgument;
        }
        res = GlyphBuilderClose(builder);
        if (res != RoboOk) {
            return res;
        }
        for (segIndex = 0u; segIndex < contour->segCount; segIndex++) {
            const FontPathSeg *seg = &contour->segs[segIndex];

            switch (seg->op) {
            case RoboFontPathMove:
                res = GlyphBuilderMoveTo(builder, GlyphDevicePoint(device, seg->p.x, seg->p.y));
                if (res != RoboOk) {
                    return res;
                }
                GlyphDevicePointF(device, seg->p.x, seg->p.y, &curX, &curY);
                haveCur = 1;
                break;
            case RoboFontPathLine:
                res = GlyphBuilderLineTo(builder, GlyphDevicePoint(device, seg->p.x, seg->p.y));
                if (res != RoboOk) {
                    return res;
                }
                GlyphDevicePointF(device, seg->p.x, seg->p.y, &curX, &curY);
                haveCur = 1;
                break;
            case RoboFontPathQuad:
                if (!haveCur) {
                    return RoboInvalidArgument;
                }
                {
                    float cx = 0.0f;
                    float cy = 0.0f;
                    float px = 0.0f;
                    float py = 0.0f;

                    GlyphDevicePointF(device, seg->c1.x, seg->c1.y, &cx, &cy);
                    GlyphDevicePointF(device, seg->p.x, seg->p.y, &px, &py);
                    res = GlyphFlattenQuad(builder, curX, curY, cx, cy, px, py, 0u);
                    if (res != RoboOk) {
                        return res;
                    }
                    curX = px;
                    curY = py;
                }
                break;
            case RoboFontPathCubic:
                if (!haveCur) {
                    return RoboInvalidArgument;
                }
                {
                    float c1x = 0.0f;
                    float c1y = 0.0f;
                    float c2x = 0.0f;
                    float c2y = 0.0f;
                    float px = 0.0f;
                    float py = 0.0f;

                    GlyphDevicePointF(device, seg->c1.x, seg->c1.y, &c1x, &c1y);
                    GlyphDevicePointF(device, seg->c2.x, seg->c2.y, &c2x, &c2y);
                    GlyphDevicePointF(device, seg->p.x, seg->p.y, &px, &py);
                    res = GlyphFlattenCubic(builder, curX, curY, c1x, c1y, c2x, c2y, px, py, 0u);
                    if (res != RoboOk) {
                        return res;
                    }
                    curX = px;
                    curY = py;
                }
                break;
            case RoboFontPathClose:
                res = GlyphBuilderClose(builder);
                if (res != RoboOk) {
                    return res;
                }
                haveCur = 0;
                break;
            default:
                return RoboNotSupported;
            }
        }
        if (contour->closed) {
            res = GlyphBuilderClose(builder);
            if (res != RoboOk) {
                return res;
            }
        }
    }
    return RoboOk;
}

/** A mask with no pixels — the answer for ink that touches nothing, and never an error. */
static Result GlyphReturnEmptyMask(int32_t x, int32_t y, GfxGlyphMask **outMask)
{
    GfxGlyphMask *mask = GlyphMaskCreate(x, y, 0u, 0u);

    if (!mask) {
        return RoboOutOfMemory;
    }
    *outMask = mask;
    return RoboOk;
}

Result GfxGlyphMaskRasterizeOutline(const FontOutline *outline,
                                    uint16_t unitsPerEm,
                                    float sizePx,
                                    GfxPointF origin,
                                    const GfxRect *clip,
                                    const GfxGlyphRasterParams *params,
                                    GfxGlyphMask **outMask,
                                    GfxGlyphRasterStats *outStats)
{
    GlyphBuilder builder;
    GlyphDevice device;
    GlyphCrossing *crossings = NULL;
    GfxGlyphMask *mask;
    int sub;
    int validSub = 1;
    int32_t subDiv;
    int32_t left;
    int32_t top;
    int64_t right;
    int64_t bottom;
    int64_t width;
    int64_t height;
    int clipped = 0;
    int overflowed;
    uint32_t samples;
    Result status;

    if (!outMask || !outline) {
        return RoboInvalidArgument;
    }
    *outMask = NULL;

    sub = GlyphSupersampleOf(params, &validSub);
    if (!validSub) {
        return RoboInvalidArgument;
    }
    subDiv = (int32_t)(RoboGfxGlyphSubpixelScale / sub);
    samples = (uint32_t)(sub * sub);
    if (outStats) {
        memset(outStats, 0, sizeof(*outStats));
        outStats->samples = samples;
    }

    if (!GlyphFloatOk(origin.x) || !GlyphFloatOk(origin.y)) {
        return RoboInvalidArgument;
    }
    if (clip && !GfxRectIsValid(*clip)) {
        return RoboInvalidArgument;
    }

    status = GlyphOutlineStatusToResult(outline);
    if (status != RoboOk) {
        return status;
    }
    if (MakeFontOutlineStatus(outline) == RoboFontOutlineStatusEmpty) {
        return GlyphReturnEmptyMask(0, 0, outMask);
    }

    /* The scale, with the same refusal `GfxFontScale` makes: a size or an em that cannot mean a
     * usable scale is a caller's mistake rather than a glyph that draws nothing. */
    if (unitsPerEm == 0u || !GlyphFloatOk(sizePx) || sizePx <= 0.0f) {
        return RoboInvalidArgument;
    }
    device.scale = sizePx / (float)unitsPerEm;
    device.origin = origin;
    /*
     * The rotation, defaulted rather than refused: a value outside 0..3 is a caller whose table disagrees
     * with this one, and the glyph it belongs to still has ink to draw, so it is drawn unrotated — which is
     * what every caller before 12.20 asked for and what the header states. Wrapping the value instead (a
     * `& 3`) would silently turn a typo into a quarter turn, which is the failure this default avoids.
     */
    device.rotation = (params && params->rotation <= (uint8_t)RoboGfxGlyphRotationClockwise270)
                          ? params->rotation
                          : (uint8_t)RoboGfxGlyphRotationNone;
    if (!GlyphFloatOk(device.scale) || device.scale <= 0.0f) {
        return RoboInvalidArgument;
    }

    GlyphBuilderInit(&builder);
    status = GlyphBuildEdges(outline, &device, &builder);
    overflowed = builder.edges.overflow;
    if (status != RoboOk || overflowed) {
        GlyphBuilderDispose(&builder);
        if (overflowed) {
            /* A glyph whose flattening exceeds the edge budget draws nothing rather than failing
             * the pass it belongs to. */
            return GlyphReturnEmptyMask(0, 0, outMask);
        }
        return status;
    }
    if (outStats) {
        outStats->segments = (uint32_t)builder.edges.count;
    }

    if (!builder.haveBounds || builder.edges.count == 0u) {
        GlyphBuilderDispose(&builder);
        return GlyphReturnEmptyMask(0, 0, outMask);
    }

    left = (int32_t)GlyphFloorDiv(builder.minX, RoboGfxGlyphSubpixelScale);
    top = (int32_t)GlyphFloorDiv(builder.minY, RoboGfxGlyphSubpixelScale);
    right = GlyphCeilDiv(builder.maxX, RoboGfxGlyphSubpixelScale);
    bottom = GlyphCeilDiv(builder.maxY, RoboGfxGlyphSubpixelScale);

    if (clip) {
        int64_t clipLeft = clip->x;
        int64_t clipTop = clip->y;
        int64_t clipRight = (int64_t)clip->x + (int64_t)clip->width;
        int64_t clipBottom = (int64_t)clip->y + (int64_t)clip->height;

        if (clipLeft > left || clipTop > top || clipRight < right || clipBottom < bottom) {
            clipped = 1;
        }
        if (clipLeft > left) {
            left = (int32_t)clipLeft;
        }
        if (clipTop > top) {
            top = (int32_t)clipTop;
        }
        if (clipRight < right) {
            right = clipRight;
        }
        if (clipBottom < bottom) {
            bottom = clipBottom;
        }
    }

    width = right - left;
    height = bottom - top;
    if (width <= 0 || height <= 0) {
        GlyphBuilderDispose(&builder);
        if (outStats) {
            outStats->clipped = (uint32_t)clipped;
        }
        return GlyphReturnEmptyMask(left, top, outMask);
    }

    if ((uint64_t)width * (uint64_t)height > (uint64_t)RoboGfxGlyphMaxMaskPixels) {
        /* Only reachable without a clip: with one, the mask is bounded by the clip, and a clip
         * that large is a surface this build cannot make. Refused rather than truncated, because
         * half a glyph is not a glyph. */
        GlyphBuilderDispose(&builder);
        return RoboNotSupported;
    }

    mask = GlyphMaskCreate(left, top, (uint32_t)width, (uint32_t)height);
    if (!mask) {
        GlyphBuilderDispose(&builder);
        return RoboOutOfMemory;
    }
    crossings = (GlyphCrossing *)RoboCalloc(builder.edges.count, sizeof(GlyphCrossing));
    if (!crossings) {
        GlyphBuilderDispose(&builder);
        GfxGlyphMaskRelease(mask);
        return RoboOutOfMemory;
    }

    GlyphRasterizeEdges(&builder.edges, mask->coverage, mask->width, mask->height, left, top,
                        sub, subDiv, crossings);

    /*
     * Counts (0..supersample²) become coverage (0..255) in place, and the same walk collects the
     * two statistics a reader wants about the ink: how many pixels it touches at all and how many
     * it fills entirely. `outStats` may be NULL; the conversion is unconditional because the counts
     * are not a coverage anyone could blend.
     */
    {
        size_t total = (size_t)mask->width * (size_t)mask->height;
        size_t index;
        uint32_t covered = 0u;
        uint32_t full = 0u;

        for (index = 0u; index < total; index++) {
            uint32_t count = mask->coverage[index];

            if (count != 0u) {
                uint32_t value = (count * 255u + samples / 2u) / samples;

                if (value > 255u) {
                    value = 255u;
                }
                mask->coverage[index] = (uint8_t)value;
                covered++;
                if (value == 255u) {
                    full++;
                }
            }
        }
        if (outStats) {
            outStats->pixels = (uint32_t)total;
            outStats->covered = covered;
            outStats->full = full;
            outStats->clipped = (uint32_t)clipped;
        }
    }

    RoboFree(crossings);
    GlyphBuilderDispose(&builder);
    *outMask = mask;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

/*
 * The sRGB opto-electronic transfer function, linear → encoded, sampled at the 256 8-bit linear
 * levels. `Esrgb(u) = 12.92u` below 0.0031308 and `1.055·u^(1/2.4) − 0.055` above it; the values
 * below are `round(Esrgb(n/255) · 255)`, computed once by hand and pasted, so the conversion is a
 * table lookup rather than a `pow` the guest target cannot link.
 */
static const uint8_t GlyphSrgbEncode[256] = {
      0,  13,  22,  28,  34,  38,  42,  46,  50,  53,  56,  59,  61,  64,  66,  69,
     71,  73,  75,  77,  79,  81,  83,  85,  86,  88,  90,  92,  93,  95,  96,  98,
     99, 101, 102, 104, 105, 106, 108, 109, 110, 112, 113, 114, 115, 117, 118, 119,
    120, 121, 122, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136,
    137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 148, 149, 150, 151,
    152, 153, 154, 155, 155, 156, 157, 158, 159, 159, 160, 161, 162, 163, 163, 164,
    165, 166, 167, 167, 168, 169, 170, 170, 171, 172, 173, 173, 174, 175, 175, 176,
    177, 178, 178, 179, 180, 180, 181, 182, 182, 183, 184, 185, 185, 186, 187, 187,
    188, 189, 189, 190, 190, 191, 192, 192, 193, 194, 194, 195, 196, 196, 197, 197,
    198, 199, 199, 200, 200, 201, 202, 202, 203, 203, 204, 205, 205, 206, 206, 207,
    208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216,
    216, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 222, 223, 223, 224, 224,
    225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233,
    233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 238, 239, 239, 240, 240,
    241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 246, 246, 246, 247, 247, 248,
    248, 249, 249, 250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 255, 255,
};

uint8_t GfxGammaCoverageToAlpha(uint8_t coverage, GfxCoverageGamma gamma)
{
    switch (gamma) {
    case RoboGfxCoverageGammaLightOnDark:
        /* Bright ink over a dark surface: the linear-light mixture is Esrgb(coverage). */
        return GlyphSrgbEncode[coverage];
    case RoboGfxCoverageGammaDarkOnLight:
        /* Dark ink over a light surface: the mirror image, 1 − Esrgb(1 − coverage). */
        return (uint8_t)(255u - (uint32_t)GlyphSrgbEncode[255u - coverage]);
    case RoboGfxCoverageGammaLinear:
    default:
        return coverage;
    }
}

/**
 * Composite a mask, one pixel at a time, through the module's source-over blend.
 *
 * The write is bounded twice and for two different reasons: the mask's own rectangle decides which
 * pixels are candidates, and the raster's clip — always inside the surface's bounds — decides which
 * of those may be written. A pixel whose effective source alpha is zero is skipped rather than
 * blended, so a transparent glyph, a fully transparent colour and an empty mask all leave the
 * destination byte-identical.
 */
Result GfxRasterDrawGlyphMask(GfxRaster *raster,
                              const GfxGlyphMask *mask,
                              GfxColorRgba8 color,
                              GfxCoverageGamma gamma)
{
    GfxSurface *target;
    GfxRect clip;
    uint8_t rasterOpacity;
    uint32_t row;

    if (!raster || !mask) {
        return RoboInvalidArgument;
    }
    if (GfxGlyphMaskIsEmpty(mask)) {
        /* A mask with no pixels is a document's shape -- "the ink touches nothing here" -- and is
         * a successful no-op, the same answer an empty rectangle gets from a fill. */
        return RoboOk;
    }
    target = GfxRasterTarget(raster);
    if (!target) {
        return RoboInvalidArgument;
    }
    if (GfxRasterClip(raster, &clip) != RoboOk) {
        return RoboInvalidArgument;
    }
    if (GfxRectIsEmpty(clip)) {
        return RoboOk;
    }
    rasterOpacity = GfxRasterOpacity(raster);
    if (color.a == 0u || rasterOpacity == 0u) {
        return RoboOk;
    }

    for (row = 0u; row < mask->height; row++) {
        int64_t deviceY = (int64_t)mask->y + (int64_t)row;
        uint32_t column;

        if (deviceY < (int64_t)clip.y || deviceY >= (int64_t)clip.y + (int64_t)clip.height) {
            continue;
        }
        for (column = 0u; column < mask->width; column++) {
            int64_t deviceX = (int64_t)mask->x + (int64_t)column;
            uint8_t coverage;
            uint8_t alpha;
            uint8_t opacity;

            if (deviceX < (int64_t)clip.x || deviceX >= (int64_t)clip.x + (int64_t)clip.width) {
                continue;
            }
            coverage = mask->coverage[(size_t)row * (size_t)mask->width + (size_t)column];
            if (coverage == 0u) {
                continue;
            }
            alpha = GfxGammaCoverageToAlpha(coverage, gamma);
            opacity = (alpha == 255u) ? rasterOpacity : GfxMulDiv255(alpha, rasterOpacity);
            if (opacity == 0u) {
                continue;
            }
            if (opacity == 255u && color.a == 255u) {
                /* Fully covered, fully opaque, full strength: the blend's own answer is the
                 * source, so the read-modify-write the compositor would do is skipped. */
                if (GfxSurfaceSetPixel(target, (uint32_t)deviceX, (uint32_t)deviceY, color) !=
                    RoboOk) {
                    return RoboError;
                }
                continue;
            }
            if (GfxSurfaceCompositePixel(target, (uint32_t)deviceX, (uint32_t)deviceY, color,
                                         opacity) != RoboOk) {
                return RoboError;
            }
        }
    }
    return RoboOk;
}
