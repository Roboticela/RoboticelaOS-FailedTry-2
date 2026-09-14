#include "RoboGraphics/RoboGraphicsGeometry.h"

robo_gfx_point_t robo_gfx_point(int32_t x, int32_t y)
{
    robo_gfx_point_t p;
    p.x = x;
    p.y = y;
    return p;
}

robo_gfx_size_t robo_gfx_size(int32_t width, int32_t height)
{
    robo_gfx_size_t s;
    s.width = width;
    s.height = height;
    return s;
}

robo_gfx_vector_t robo_gfx_vector(int32_t dx, int32_t dy)
{
    robo_gfx_vector_t v;
    v.dx = dx;
    v.dy = dy;
    return v;
}

robo_gfx_rect_t robo_gfx_rect(int32_t x, int32_t y, int32_t width, int32_t height)
{
    robo_gfx_rect_t r;
    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}

robo_gfx_rect_t robo_gfx_rect_from_size(robo_gfx_size_t size)
{
    return robo_gfx_rect(0, 0, size.width, size.height);
}

robo_gfx_point_f_t robo_gfx_point_f(float x, float y)
{
    robo_gfx_point_f_t p;
    p.x = x;
    p.y = y;
    return p;
}

robo_gfx_size_f_t robo_gfx_size_f(float width, float height)
{
    robo_gfx_size_f_t s;
    s.width = width;
    s.height = height;
    return s;
}

robo_gfx_rect_f_t robo_gfx_rect_f(float x, float y, float width, float height)
{
    robo_gfx_rect_f_t r;
    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}

int robo_gfx_size_is_empty(robo_gfx_size_t s)
{
    return s.width <= 0 || s.height <= 0;
}

int robo_gfx_size_is_valid(robo_gfx_size_t s)
{
    return s.width >= 0 && s.height >= 0;
}

int robo_gfx_rect_is_empty(robo_gfx_rect_t r)
{
    return r.width <= 0 || r.height <= 0;
}

int robo_gfx_rect_is_valid(robo_gfx_rect_t r)
{
    return r.width >= 0 && r.height >= 0;
}

int32_t robo_gfx_rect_right(robo_gfx_rect_t r)
{
    return r.x + r.width;
}

int32_t robo_gfx_rect_bottom(robo_gfx_rect_t r)
{
    return r.y + r.height;
}

int robo_gfx_rect_contains_point(robo_gfx_rect_t r, robo_gfx_point_t p)
{
    if (robo_gfx_rect_is_empty(r)) {
        return 0;
    }
    return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height;
}

robo_gfx_point_t robo_gfx_rect_center(robo_gfx_rect_t r)
{
    return robo_gfx_point(r.x + r.width / 2, r.y + r.height / 2);
}

/* Freestanding-safe float→int helpers (no libm). */
static int32_t floor_f_to_i32(float v)
{
    int32_t i = (int32_t)v;
    if ((float)i > v) {
        i -= 1;
    }
    return i;
}

static int32_t ceil_f_to_i32(float v)
{
    int32_t i = (int32_t)v;
    if ((float)i < v) {
        i += 1;
    }
    return i;
}

static int32_t round_f_to_i32(float v)
{
    if (v >= 0.0f) {
        return floor_f_to_i32(v + 0.5f);
    }
    return ceil_f_to_i32(v - 0.5f);
}

robo_gfx_point_t robo_gfx_point_f_floor(robo_gfx_point_f_t p)
{
    return robo_gfx_point(floor_f_to_i32(p.x), floor_f_to_i32(p.y));
}

robo_gfx_point_t robo_gfx_point_f_round(robo_gfx_point_f_t p)
{
    return robo_gfx_point(round_f_to_i32(p.x), round_f_to_i32(p.y));
}

robo_gfx_rect_t robo_gfx_rect_f_floor(robo_gfx_rect_f_t r)
{
    int32_t x0 = floor_f_to_i32(r.x);
    int32_t y0 = floor_f_to_i32(r.y);
    int32_t x1 = floor_f_to_i32(r.x + r.width);
    int32_t y1 = floor_f_to_i32(r.y + r.height);
    return robo_gfx_rect(x0, y0, x1 - x0, y1 - y0);
}

robo_gfx_rect_t robo_gfx_rect_f_round_out(robo_gfx_rect_f_t r)
{
    int32_t x0 = floor_f_to_i32(r.x);
    int32_t y0 = floor_f_to_i32(r.y);
    int32_t x1 = ceil_f_to_i32(r.x + r.width);
    int32_t y1 = ceil_f_to_i32(r.y + r.height);
    return robo_gfx_rect(x0, y0, x1 - x0, y1 - y0);
}

void robo_gfx_affine_identity(robo_gfx_affine_t *out)
{
    if (!out) {
        return;
    }
    out->m00 = 1.0f;
    out->m01 = 0.0f;
    out->m02 = 0.0f;
    out->m10 = 0.0f;
    out->m11 = 1.0f;
    out->m12 = 0.0f;
}

void robo_gfx_affine_translate(robo_gfx_affine_t *out, float tx, float ty)
{
    robo_gfx_affine_identity(out);
    if (out) {
        out->m02 = tx;
        out->m12 = ty;
    }
}

void robo_gfx_affine_scale(robo_gfx_affine_t *out, float sx, float sy)
{
    robo_gfx_affine_identity(out);
    if (out) {
        out->m00 = sx;
        out->m11 = sy;
    }
}

robo_result_t robo_gfx_affine_multiply(const robo_gfx_affine_t *a,
                                       const robo_gfx_affine_t *b,
                                       robo_gfx_affine_t *out)
{
    robo_gfx_affine_t r;
    if (!a || !b || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r.m00 = a->m00 * b->m00 + a->m01 * b->m10;
    r.m01 = a->m00 * b->m01 + a->m01 * b->m11;
    r.m02 = a->m00 * b->m02 + a->m01 * b->m12 + a->m02;
    r.m10 = a->m10 * b->m00 + a->m11 * b->m10;
    r.m11 = a->m10 * b->m01 + a->m11 * b->m11;
    r.m12 = a->m10 * b->m02 + a->m11 * b->m12 + a->m12;
    *out = r;
    return ROBO_OK;
}

robo_gfx_point_f_t robo_gfx_affine_map_point(const robo_gfx_affine_t *m,
                                             robo_gfx_point_f_t p)
{
    robo_gfx_point_f_t o;
    if (!m) {
        return p;
    }
    o.x = m->m00 * p.x + m->m01 * p.y + m->m02;
    o.y = m->m10 * p.x + m->m11 * p.y + m->m12;
    return o;
}
