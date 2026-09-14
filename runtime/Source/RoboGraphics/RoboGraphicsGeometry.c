#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <stddef.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

static int i32_add(int32_t a, int32_t b, int32_t *out)
{
    int64_t s = (int64_t)a + (int64_t)b;
    if (s > INT32_MAX || s < INT32_MIN) {
        return 0;
    }
    *out = (int32_t)s;
    return 1;
}

static int i32_sub(int32_t a, int32_t b, int32_t *out)
{
    int64_t d = (int64_t)a - (int64_t)b;
    if (d > INT32_MAX || d < INT32_MIN) {
        return 0;
    }
    *out = (int32_t)d;
    return 1;
}

static int32_t i32_saturate(int64_t v)
{
    if (v > INT32_MAX) {
        return INT32_MAX;
    }
    if (v < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)v;
}

static int rect_edges(robo_gfx_rect_t r,
                      int64_t *left,
                      int64_t *top,
                      int64_t *right,
                      int64_t *bottom)
{
    if (!robo_gfx_rect_is_valid(r)) {
        return 0;
    }
    *left = (int64_t)r.x;
    *top = (int64_t)r.y;
    *right = (int64_t)r.x + (int64_t)r.width;
    *bottom = (int64_t)r.y + (int64_t)r.height;
    return 1;
}

static robo_result_t rect_from_edges(int64_t left,
                                     int64_t top,
                                     int64_t right,
                                     int64_t bottom,
                                     robo_gfx_rect_t *out)
{
    int64_t w;
    int64_t h;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (right < left || bottom < top) {
        return ROBO_INVALID_ARGUMENT;
    }
    w = right - left;
    h = bottom - top;
    if (w > INT32_MAX || h > INT32_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (left > INT32_MAX || left < INT32_MIN || top > INT32_MAX ||
        top < INT32_MIN) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->x = (int32_t)left;
    out->y = (int32_t)top;
    out->width = (int32_t)w;
    out->height = (int32_t)h;
    return ROBO_OK;
}

robo_gfx_point_t robo_gfx_point(robo_gfx_pixel_x_t x, robo_gfx_pixel_y_t y)
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

robo_gfx_rect_t robo_gfx_rect(robo_gfx_pixel_x_t x,
                              robo_gfx_pixel_y_t y,
                              int32_t width,
                              int32_t height)
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

robo_gfx_rect_t robo_gfx_rect_empty(void)
{
    return robo_gfx_rect(0, 0, 0, 0);
}

robo_result_t robo_gfx_rect_from_point_size(robo_gfx_point_t origin,
                                            robo_gfx_size_t size,
                                            robo_gfx_rect_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_size_is_valid(size)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = robo_gfx_rect(origin.x, origin.y, size.width, size.height);
    return ROBO_OK;
}

robo_result_t robo_gfx_rect_from_ltrb(robo_gfx_pixel_x_t left,
                                      robo_gfx_pixel_y_t top,
                                      robo_gfx_pixel_x_t right,
                                      robo_gfx_pixel_y_t bottom,
                                      robo_gfx_rect_t *out)
{
    return rect_from_edges((int64_t)left, (int64_t)top, (int64_t)right,
                           (int64_t)bottom, out);
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

int robo_gfx_point_equal(robo_gfx_point_t a, robo_gfx_point_t b)
{
    return a.x == b.x && a.y == b.y;
}

robo_result_t robo_gfx_point_add(robo_gfx_point_t a,
                                 robo_gfx_point_t b,
                                 robo_gfx_point_t *out)
{
    int32_t x;
    int32_t y;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!i32_add(a.x, b.x, &x) || !i32_add(a.y, b.y, &y)) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->x = x;
    out->y = y;
    return ROBO_OK;
}

robo_result_t robo_gfx_point_sub(robo_gfx_point_t a,
                                 robo_gfx_point_t b,
                                 robo_gfx_point_t *out)
{
    int32_t x;
    int32_t y;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!i32_sub(a.x, b.x, &x) || !i32_sub(a.y, b.y, &y)) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->x = x;
    out->y = y;
    return ROBO_OK;
}

robo_result_t robo_gfx_point_translate(robo_gfx_point_t p,
                                       int32_t dx,
                                       int32_t dy,
                                       robo_gfx_point_t *out)
{
    return robo_gfx_point_add(p, robo_gfx_point(dx, dy), out);
}

int robo_gfx_size_equal(robo_gfx_size_t a, robo_gfx_size_t b)
{
    return a.width == b.width && a.height == b.height;
}

int robo_gfx_size_is_empty(robo_gfx_size_t s)
{
    return s.width <= 0 || s.height <= 0;
}

int robo_gfx_size_is_valid(robo_gfx_size_t s)
{
    return s.width >= 0 && s.height >= 0;
}

robo_result_t robo_gfx_size_area(robo_gfx_size_t s, uint64_t *out_area)
{
    if (!out_area) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_area = 0u;
    if (!robo_gfx_size_is_valid(s)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_size_is_empty(s)) {
        return ROBO_OK;
    }
    *out_area = (uint64_t)(uint32_t)s.width * (uint64_t)(uint32_t)s.height;
    return ROBO_OK;
}

int robo_gfx_rect_equal(robo_gfx_rect_t a, robo_gfx_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

int robo_gfx_rect_is_empty(robo_gfx_rect_t r)
{
    return r.width <= 0 || r.height <= 0;
}

int robo_gfx_rect_is_valid(robo_gfx_rect_t r)
{
    return r.width >= 0 && r.height >= 0;
}

robo_gfx_pixel_x_t robo_gfx_rect_left(robo_gfx_rect_t r)
{
    return r.x;
}

robo_gfx_pixel_y_t robo_gfx_rect_top(robo_gfx_rect_t r)
{
    return r.y;
}

int32_t robo_gfx_rect_width(robo_gfx_rect_t r)
{
    return r.width;
}

int32_t robo_gfx_rect_height(robo_gfx_rect_t r)
{
    return r.height;
}

int32_t robo_gfx_rect_right(robo_gfx_rect_t r)
{
    return i32_saturate((int64_t)r.x + (int64_t)r.width);
}

int32_t robo_gfx_rect_bottom(robo_gfx_rect_t r)
{
    return i32_saturate((int64_t)r.y + (int64_t)r.height);
}

robo_result_t robo_gfx_rect_right_checked(robo_gfx_rect_t r, int32_t *out)
{
    int32_t v;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!i32_add(r.x, r.width, &v)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = v;
    return ROBO_OK;
}

robo_result_t robo_gfx_rect_bottom_checked(robo_gfx_rect_t r, int32_t *out)
{
    int32_t v;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!i32_add(r.y, r.height, &v)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = v;
    return ROBO_OK;
}

int robo_gfx_rect_contains_point(robo_gfx_rect_t r, robo_gfx_point_t p)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (robo_gfx_rect_is_empty(r) || !rect_edges(r, &left, &top, &right, &bottom)) {
        return 0;
    }
    return (int64_t)p.x >= left && (int64_t)p.x < right && (int64_t)p.y >= top &&
           (int64_t)p.y < bottom;
}

int robo_gfx_rect_encloses(robo_gfx_rect_t outer, robo_gfx_rect_t inner)
{
    int64_t ol, ot, oright, ob;
    int64_t il, it, ir, ib;

    if (!rect_edges(outer, &ol, &ot, &oright, &ob)) {
        return 0;
    }
    if (!rect_edges(inner, &il, &it, &ir, &ib)) {
        return 0;
    }
    if (robo_gfx_rect_is_empty(inner)) {
        return 1;
    }
    if (robo_gfx_rect_is_empty(outer)) {
        return 0;
    }
    return il >= ol && it >= ot && ir <= oright && ib <= ob;
}

int robo_gfx_rect_intersects(robo_gfx_rect_t a, robo_gfx_rect_t b)
{
    robo_gfx_rect_t tmp;
    if (robo_gfx_rect_intersect(a, b, &tmp) != ROBO_OK) {
        return 0;
    }
    return !robo_gfx_rect_is_empty(tmp);
}

robo_gfx_point_t robo_gfx_rect_center(robo_gfx_rect_t r)
{
    return robo_gfx_point(r.x + r.width / 2, r.y + r.height / 2);
}

robo_result_t robo_gfx_rect_intersect(robo_gfx_rect_t a,
                                      robo_gfx_rect_t b,
                                      robo_gfx_rect_t *out)
{
    int64_t al, at, ar, ab;
    int64_t bl, bt, br, bb;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = robo_gfx_rect_empty();
    if (robo_gfx_rect_is_empty(a) || robo_gfx_rect_is_empty(b)) {
        return ROBO_OK;
    }
    if (!rect_edges(a, &al, &at, &ar, &ab) || !rect_edges(b, &bl, &bt, &br, &bb)) {
        return ROBO_INVALID_ARGUMENT;
    }
    left = al > bl ? al : bl;
    top = at > bt ? at : bt;
    right = ar < br ? ar : br;
    bottom = ab < bb ? ab : bb;
    if (right <= left || bottom <= top) {
        *out = robo_gfx_rect_empty();
        return ROBO_OK;
    }
    return rect_from_edges(left, top, right, bottom, out);
}

robo_result_t robo_gfx_rect_bounding(robo_gfx_rect_t a,
                                     robo_gfx_rect_t b,
                                     robo_gfx_rect_t *out)
{
    int64_t al, at, ar, ab;
    int64_t bl, bt, br, bb;
    int a_empty;
    int b_empty;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    a_empty = robo_gfx_rect_is_empty(a);
    b_empty = robo_gfx_rect_is_empty(b);
    if (a_empty && b_empty) {
        *out = robo_gfx_rect_empty();
        return ROBO_OK;
    }
    if (a_empty) {
        if (!robo_gfx_rect_is_valid(b)) {
            return ROBO_INVALID_ARGUMENT;
        }
        *out = b;
        return ROBO_OK;
    }
    if (b_empty) {
        if (!robo_gfx_rect_is_valid(a)) {
            return ROBO_INVALID_ARGUMENT;
        }
        *out = a;
        return ROBO_OK;
    }
    if (!rect_edges(a, &al, &at, &ar, &ab) || !rect_edges(b, &bl, &bt, &br, &bb)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return rect_from_edges(al < bl ? al : bl, at < bt ? at : bt,
                           ar > br ? ar : br, ab > bb ? ab : bb, out);
}

robo_result_t robo_gfx_rect_translate(robo_gfx_rect_t r,
                                      int32_t dx,
                                      int32_t dy,
                                      robo_gfx_rect_t *out)
{
    int32_t nx;
    int32_t ny;
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(r)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!i32_add(r.x, dx, &nx) || !i32_add(r.y, dy, &ny)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = robo_gfx_rect(nx, ny, r.width, r.height);
    return ROBO_OK;
}

robo_result_t robo_gfx_rect_clip(robo_gfx_rect_t rect,
                                 robo_gfx_rect_t bounds,
                                 robo_gfx_rect_t *out)
{
    return robo_gfx_rect_intersect(rect, bounds, out);
}

robo_result_t robo_gfx_rect_inset(robo_gfx_rect_t r,
                                  int32_t amount,
                                  robo_gfx_rect_t *out)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(r)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_rect_is_empty(r)) {
        *out = r;
        return ROBO_OK;
    }
    if (!rect_edges(r, &left, &top, &right, &bottom)) {
        return ROBO_INVALID_ARGUMENT;
    }
    left += (int64_t)amount;
    top += (int64_t)amount;
    right -= (int64_t)amount;
    bottom -= (int64_t)amount;
    if (right <= left || bottom <= top) {
        *out = robo_gfx_rect_empty();
        return ROBO_OK;
    }
    return rect_from_edges(left, top, right, bottom, out);
}

robo_result_t robo_gfx_rect_outset(robo_gfx_rect_t r,
                                   int32_t amount,
                                   robo_gfx_rect_t *out)
{
    if (amount == INT32_MIN) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_rect_inset(r, -amount, out);
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
