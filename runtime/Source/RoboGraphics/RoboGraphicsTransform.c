#include "RoboGraphics/RoboGraphicsTransform.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif
#ifndef INT32_MIN
#define INT32_MIN (-2147483647 - 1)
#endif

/* Absolute |det| below this is treated as singular. */
#define ROBO_GFX_AFFINE_DET_EPS 1.0e-12f

static const float k_pi = 3.14159265358979323846f;
static const float k_two_pi = 6.28318530717958647692f;

static float absf_local(float v)
{
    return v < 0.0f ? -v : v;
}

/* Freestanding-safe float→int (no libm), matching Geometry.c. */
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

static float round_nearest_f(float v)
{
    if (v >= 0.0f) {
        return (float)floor_f_to_i32(v + 0.5f);
    }
    return (float)ceil_f_to_i32(v - 0.5f);
}

/**
 * Freestanding sin/cos — Taylor on [-π/2, π/2] after 2π reduction.
 * Adequate for affine foundation; not a full libm replacement.
 */
static float reduce_angle(float x)
{
    float n;
    float r;

    n = x / k_two_pi;
    n = round_nearest_f(n);
    r = x - n * k_two_pi;
    if (r > k_pi) {
        r -= k_two_pi;
    } else if (r < -k_pi) {
        r += k_two_pi;
    }
    return r;
}

static float sin_poly(float y)
{
    /* sin(y) ≈ y − y³/6 + y⁵/120 − y⁷/5040, |y| ≤ π/2 */
    float y2 = y * y;
    return y * (1.0f - y2 * (1.0f / 6.0f -
                             y2 * (1.0f / 120.0f - y2 * (1.0f / 5040.0f))));
}

static float robo_sinf(float x)
{
    float y = reduce_angle(x);

    if (y > (k_pi * 0.5f)) {
        y = k_pi - y;
    } else if (y < -(k_pi * 0.5f)) {
        y = -k_pi - y;
    }
    return sin_poly(y);
}

static float robo_cosf(float x)
{
    return robo_sinf(x + k_pi * 0.5f);
}

void robo_gfx_affine_set(robo_gfx_affine_t *out,
                         float m00,
                         float m01,
                         float m02,
                         float m10,
                         float m11,
                         float m12)
{
    if (!out) {
        return;
    }
    out->m00 = m00;
    out->m01 = m01;
    out->m02 = m02;
    out->m10 = m10;
    out->m11 = m11;
    out->m12 = m12;
}

static int near_multiple_of_90(float degrees, float *out_norm)
{
    float n = degrees;

    while (n > 180.0f) {
        n -= 360.0f;
    }
    while (n < -180.0f) {
        n += 360.0f;
    }
    if (absf_local(n) < 1.0e-4f) {
        *out_norm = 0.0f;
        return 1;
    }
    if (absf_local(n - 90.0f) < 1.0e-4f) {
        *out_norm = 90.0f;
        return 1;
    }
    if (absf_local(n + 90.0f) < 1.0e-4f) {
        *out_norm = -90.0f;
        return 1;
    }
    if (absf_local(n - 180.0f) < 1.0e-4f || absf_local(n + 180.0f) < 1.0e-4f) {
        *out_norm = 180.0f;
        return 1;
    }
    return 0;
}

void robo_gfx_affine_rotate_deg(robo_gfx_affine_t *out, float degrees)
{
    float norm;

    if (!out) {
        return;
    }
    if (near_multiple_of_90(degrees, &norm)) {
        if (norm == 0.0f) {
            robo_gfx_affine_identity(out);
        } else if (norm == 90.0f) {
            robo_gfx_affine_set(out, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f);
        } else if (norm == -90.0f) {
            robo_gfx_affine_set(out, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f);
        } else {
            robo_gfx_affine_set(out, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f);
        }
        return;
    }
    robo_gfx_affine_rotate_rad(out, degrees * (k_pi / 180.0f));
}

void robo_gfx_affine_rotate_rad(robo_gfx_affine_t *out, float radians)
{
    float c;
    float s;

    if (!out) {
        return;
    }
    c = robo_cosf(radians);
    s = robo_sinf(radians);
    robo_gfx_affine_set(out, c, -s, 0.0f, s, c, 0.0f);
}

void robo_gfx_affine_shear(robo_gfx_affine_t *out, float shx, float shy)
{
    if (!out) {
        return;
    }
    robo_gfx_affine_set(out, 1.0f, shx, 0.0f, shy, 1.0f, 0.0f);
}

int robo_gfx_affine_is_identity(const robo_gfx_affine_t *m)
{
    if (!m) {
        return 0;
    }
    return m->m00 == 1.0f && m->m01 == 0.0f && m->m02 == 0.0f &&
           m->m10 == 0.0f && m->m11 == 1.0f && m->m12 == 0.0f;
}

float robo_gfx_affine_determinant(const robo_gfx_affine_t *m)
{
    if (!m) {
        return 0.0f;
    }
    return m->m00 * m->m11 - m->m01 * m->m10;
}

int robo_gfx_affine_is_invertible(const robo_gfx_affine_t *m)
{
    if (!m) {
        return 0;
    }
    return absf_local(robo_gfx_affine_determinant(m)) > ROBO_GFX_AFFINE_DET_EPS;
}

int robo_gfx_affine_equal(const robo_gfx_affine_t *a, const robo_gfx_affine_t *b)
{
    if (!a || !b) {
        return 0;
    }
    return a->m00 == b->m00 && a->m01 == b->m01 && a->m02 == b->m02 &&
           a->m10 == b->m10 && a->m11 == b->m11 && a->m12 == b->m12;
}

robo_result_t robo_gfx_affine_compose(const robo_gfx_affine_t *a,
                                      const robo_gfx_affine_t *b,
                                      robo_gfx_affine_t *out)
{
    return robo_gfx_affine_multiply(a, b, out);
}

robo_result_t robo_gfx_affine_pre_concat(robo_gfx_affine_t *dst,
                                         const robo_gfx_affine_t *other)
{
    robo_gfx_affine_t tmp;
    robo_result_t res;

    if (!dst || !other) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_affine_multiply(dst, other, &tmp);
    if (res != ROBO_OK) {
        return res;
    }
    *dst = tmp;
    return ROBO_OK;
}

robo_result_t robo_gfx_affine_post_concat(robo_gfx_affine_t *dst,
                                          const robo_gfx_affine_t *other)
{
    robo_gfx_affine_t tmp;
    robo_result_t res;

    if (!dst || !other) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_affine_multiply(other, dst, &tmp);
    if (res != ROBO_OK) {
        return res;
    }
    *dst = tmp;
    return ROBO_OK;
}

robo_result_t robo_gfx_affine_invert(const robo_gfx_affine_t *m,
                                     robo_gfx_affine_t *out)
{
    float det;
    float inv;
    robo_gfx_affine_t r;

    if (!m || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    det = robo_gfx_affine_determinant(m);
    if (absf_local(det) <= ROBO_GFX_AFFINE_DET_EPS) {
        return ROBO_INVALID_ARGUMENT;
    }
    inv = 1.0f / det;
    /*
     * Inverse of [A t; 0 1] is [A^-1, -A^-1 t; 0 1].
     * A^-1 = (1/det) |  m11 -m01 |
     *                | -m10  m00 |
     */
    r.m00 = m->m11 * inv;
    r.m01 = -m->m01 * inv;
    r.m10 = -m->m10 * inv;
    r.m11 = m->m00 * inv;
    r.m02 = -(r.m00 * m->m02 + r.m01 * m->m12);
    r.m12 = -(r.m10 * m->m02 + r.m11 * m->m12);
    *out = r;
    return ROBO_OK;
}

robo_gfx_point_f_t robo_gfx_affine_map_point_f(const robo_gfx_affine_t *m,
                                               robo_gfx_point_f_t p)
{
    return robo_gfx_affine_map_point(m, p);
}

robo_gfx_point_t robo_gfx_affine_map_point_i(const robo_gfx_affine_t *m,
                                             robo_gfx_point_t p)
{
    robo_gfx_point_f_t pf;

    pf = robo_gfx_point_f((float)p.x, (float)p.y);
    pf = robo_gfx_affine_map_point(m, pf);
    return robo_gfx_point_f_round(pf);
}

robo_result_t robo_gfx_affine_map_rect_f(const robo_gfx_affine_t *m,
                                         robo_gfx_rect_f_t r,
                                         robo_gfx_rect_f_t *out)
{
    robo_gfx_point_f_t c0, c1, c2, c3;
    float min_x, min_y, max_x, max_y;

    if (!m || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (r.width <= 0.0f || r.height <= 0.0f) {
        *out = robo_gfx_rect_f(0.0f, 0.0f, 0.0f, 0.0f);
        return ROBO_OK;
    }

    c0 = robo_gfx_affine_map_point(m, robo_gfx_point_f(r.x, r.y));
    c1 = robo_gfx_affine_map_point(m, robo_gfx_point_f(r.x + r.width, r.y));
    c2 = robo_gfx_affine_map_point(m, robo_gfx_point_f(r.x, r.y + r.height));
    c3 = robo_gfx_affine_map_point(
        m, robo_gfx_point_f(r.x + r.width, r.y + r.height));

    min_x = max_x = c0.x;
    min_y = max_y = c0.y;
    if (c1.x < min_x) {
        min_x = c1.x;
    }
    if (c1.x > max_x) {
        max_x = c1.x;
    }
    if (c1.y < min_y) {
        min_y = c1.y;
    }
    if (c1.y > max_y) {
        max_y = c1.y;
    }
    if (c2.x < min_x) {
        min_x = c2.x;
    }
    if (c2.x > max_x) {
        max_x = c2.x;
    }
    if (c2.y < min_y) {
        min_y = c2.y;
    }
    if (c2.y > max_y) {
        max_y = c2.y;
    }
    if (c3.x < min_x) {
        min_x = c3.x;
    }
    if (c3.x > max_x) {
        max_x = c3.x;
    }
    if (c3.y < min_y) {
        min_y = c3.y;
    }
    if (c3.y > max_y) {
        max_y = c3.y;
    }

    *out = robo_gfx_rect_f(min_x, min_y, max_x - min_x, max_y - min_y);
    return ROBO_OK;
}

robo_result_t robo_gfx_affine_map_rect(const robo_gfx_affine_t *m,
                                       robo_gfx_rect_t r,
                                       robo_gfx_rect_t *out)
{
    robo_gfx_rect_f_t rf;
    robo_gfx_rect_f_t mapped;
    robo_result_t res;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;

    if (!m || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = robo_gfx_rect_empty();
    if (!robo_gfx_rect_is_valid(r) || robo_gfx_rect_is_empty(r)) {
        return ROBO_OK;
    }

    rf = robo_gfx_rect_f((float)r.x, (float)r.y, (float)r.width,
                         (float)r.height);
    res = robo_gfx_affine_map_rect_f(m, rf, &mapped);
    if (res != ROBO_OK) {
        return res;
    }

    left = floor_f_to_i32(mapped.x);
    top = floor_f_to_i32(mapped.y);
    right = ceil_f_to_i32(mapped.x + mapped.width);
    bottom = ceil_f_to_i32(mapped.y + mapped.height);
    return robo_gfx_rect_from_ltrb(left, top, right, bottom, out);
}

static robo_result_t append_transformed_cmd(robo_gfx_path_t *dst,
                                            const robo_gfx_affine_t *m,
                                            const robo_gfx_path_command_t *cmd)
{
    robo_gfx_point_t p;

    switch (cmd->verb) {
    case ROBO_GFX_PATH_MOVE_TO:
        p = robo_gfx_affine_map_point_i(m, cmd->point);
        return robo_gfx_path_move_to(dst, p);
    case ROBO_GFX_PATH_LINE_TO:
        p = robo_gfx_affine_map_point_i(m, cmd->point);
        return robo_gfx_path_line_to(dst, p);
    case ROBO_GFX_PATH_CLOSE:
        return robo_gfx_path_close(dst);
    case ROBO_GFX_PATH_QUAD_TO:
    case ROBO_GFX_PATH_CUBIC_TO:
        return ROBO_NOT_SUPPORTED;
    default:
        return ROBO_INVALID_ARGUMENT;
    }
}

robo_result_t robo_gfx_path_transformed(const robo_gfx_path_t *src,
                                        const robo_gfx_affine_t *m,
                                        robo_gfx_path_t **out)
{
    robo_gfx_path_t *dst = NULL;
    robo_gfx_path_iter_t it;
    robo_gfx_path_command_t cmd;
    robo_result_t res;

    if (!src || !m || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = robo_gfx_path_create(&dst);
    if (res != ROBO_OK) {
        return res;
    }

    robo_gfx_path_iter_begin(src, &it);
    while (robo_gfx_path_iter_next(&it, &cmd)) {
        res = append_transformed_cmd(dst, m, &cmd);
        if (res != ROBO_OK) {
            robo_gfx_path_release(dst);
            return res;
        }
    }

    *out = dst;
    return ROBO_OK;
}

robo_result_t robo_gfx_path_transform(robo_gfx_path_t *path,
                                      const robo_gfx_affine_t *m)
{
    robo_gfx_path_command_t *snapshot = NULL;
    size_t n;
    size_t i;
    robo_result_t res;

    if (!path || !m) {
        return ROBO_INVALID_ARGUMENT;
    }

    n = robo_gfx_path_command_count(path);
    if (n == 0u) {
        return ROBO_OK;
    }

    /* Reject unsupported verbs before mutating. */
    for (i = 0u; i < n; i++) {
        robo_gfx_path_command_t c;
        res = robo_gfx_path_command_at(path, i, &c);
        if (res != ROBO_OK) {
            return res;
        }
        if (c.verb == ROBO_GFX_PATH_QUAD_TO ||
            c.verb == ROBO_GFX_PATH_CUBIC_TO) {
            return ROBO_NOT_SUPPORTED;
        }
    }

    snapshot = (robo_gfx_path_command_t *)robo_calloc(
        n, sizeof(robo_gfx_path_command_t));
    if (!snapshot) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < n; i++) {
        res = robo_gfx_path_command_at(path, i, &snapshot[i]);
        if (res != ROBO_OK) {
            robo_free(snapshot);
            return res;
        }
    }

    robo_gfx_path_reset(path);
    for (i = 0u; i < n; i++) {
        res = append_transformed_cmd(path, m, &snapshot[i]);
        if (res != ROBO_OK) {
            /*
             * Restore original commands so failure does not corrupt the path.
             */
            robo_gfx_path_reset(path);
            for (i = 0u; i < n; i++) {
                robo_result_t r2;
                switch (snapshot[i].verb) {
                case ROBO_GFX_PATH_MOVE_TO:
                    r2 = robo_gfx_path_move_to(path, snapshot[i].point);
                    break;
                case ROBO_GFX_PATH_LINE_TO:
                    r2 = robo_gfx_path_line_to(path, snapshot[i].point);
                    break;
                case ROBO_GFX_PATH_CLOSE:
                    r2 = robo_gfx_path_close(path);
                    break;
                default:
                    r2 = ROBO_INVALID_ARGUMENT;
                    break;
                }
                (void)r2;
            }
            robo_free(snapshot);
            return res;
        }
    }

    robo_free(snapshot);
    return ROBO_OK;
}
