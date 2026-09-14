#include "RoboGraphics/RoboGraphicsPath.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif

#define ROBO_GFX_PATH_INITIAL_CAP 8u

struct robo_gfx_path {
    uint32_t refcount;
    robo_gfx_path_id_t id;
    robo_gfx_path_command_t *cmds;
    size_t count;
    size_t capacity;
    int has_current;
    int open_contour;
    robo_gfx_point_t current;
    robo_gfx_point_t contour_start;
};

static uint64_t g_path_id_seq = 1u;

static int ensure_capacity(robo_gfx_path_t *path, size_t need)
{
    size_t new_cap;
    robo_gfx_path_command_t *neu;

    if (need <= path->capacity) {
        return 1;
    }
    new_cap = path->capacity ? path->capacity : ROBO_GFX_PATH_INITIAL_CAP;
    while (new_cap < need) {
        if (new_cap > (SIZE_MAX / 2u)) {
            return 0;
        }
        new_cap *= 2u;
    }
    if (new_cap > SIZE_MAX / sizeof(robo_gfx_path_command_t)) {
        return 0;
    }
    neu = (robo_gfx_path_command_t *)robo_realloc(
        path->cmds, new_cap * sizeof(robo_gfx_path_command_t));
    if (!neu) {
        return 0;
    }
    path->cmds = neu;
    path->capacity = new_cap;
    return 1;
}

static robo_result_t append_cmd(robo_gfx_path_t *path,
                                robo_gfx_path_verb_t verb,
                                robo_gfx_point_t point)
{
    robo_gfx_path_command_t *slot;

    if (!ensure_capacity(path, path->count + 1u)) {
        return ROBO_OUT_OF_MEMORY;
    }
    slot = &path->cmds[path->count];
    slot->verb = verb;
    slot->point = point;
    path->count++;
    return ROBO_OK;
}

robo_result_t robo_gfx_path_create(robo_gfx_path_t **out)
{
    robo_gfx_path_t *path;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    path = (robo_gfx_path_t *)robo_calloc(1, sizeof(*path));
    if (!path) {
        return ROBO_OUT_OF_MEMORY;
    }
    path->refcount = 1u;
    path->id = g_path_id_seq++;
    if (path->id == ROBO_GFX_PATH_ID_NONE) {
        path->id = g_path_id_seq++;
    }
    *out = path;
    return ROBO_OK;
}

void robo_gfx_path_retain(robo_gfx_path_t *path)
{
    if (!path) {
        return;
    }
    path->refcount++;
}

void robo_gfx_path_release(robo_gfx_path_t *path)
{
    if (!path) {
        return;
    }
    if (path->refcount == 0u) {
        return;
    }
    path->refcount--;
    if (path->refcount != 0u) {
        return;
    }
    robo_free(path->cmds);
    robo_free(path);
}

robo_gfx_path_id_t robo_gfx_path_id(const robo_gfx_path_t *path)
{
    return path ? path->id : ROBO_GFX_PATH_ID_NONE;
}

void robo_gfx_path_reset(robo_gfx_path_t *path)
{
    if (!path) {
        return;
    }
    path->count = 0u;
    path->has_current = 0;
    path->open_contour = 0;
    memset(&path->current, 0, sizeof(path->current));
    memset(&path->contour_start, 0, sizeof(path->contour_start));
}

void robo_gfx_path_clear(robo_gfx_path_t *path)
{
    if (!path) {
        return;
    }
    robo_free(path->cmds);
    path->cmds = NULL;
    path->capacity = 0u;
    robo_gfx_path_reset(path);
}

int robo_gfx_path_is_empty(const robo_gfx_path_t *path)
{
    return !path || path->count == 0u;
}

size_t robo_gfx_path_command_count(const robo_gfx_path_t *path)
{
    return path ? path->count : 0u;
}

int robo_gfx_path_has_current_point(const robo_gfx_path_t *path)
{
    return path && path->has_current;
}

robo_result_t robo_gfx_path_current_point(const robo_gfx_path_t *path,
                                          robo_gfx_point_t *out)
{
    if (!path || !out || !path->has_current) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = path->current;
    return ROBO_OK;
}

int robo_gfx_path_has_open_contour(const robo_gfx_path_t *path)
{
    return path && path->open_contour;
}

robo_result_t robo_gfx_path_move_to(robo_gfx_path_t *path, robo_gfx_point_t p)
{
    robo_result_t res;

    if (!path) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = append_cmd(path, ROBO_GFX_PATH_MOVE_TO, p);
    if (res != ROBO_OK) {
        return res;
    }
    path->contour_start = p;
    path->current = p;
    path->has_current = 1;
    path->open_contour = 1;
    return ROBO_OK;
}

robo_result_t robo_gfx_path_line_to(robo_gfx_path_t *path, robo_gfx_point_t p)
{
    robo_result_t res;

    if (!path) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!path->open_contour) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = append_cmd(path, ROBO_GFX_PATH_LINE_TO, p);
    if (res != ROBO_OK) {
        return res;
    }
    path->current = p;
    path->has_current = 1;
    return ROBO_OK;
}

robo_result_t robo_gfx_path_close(robo_gfx_path_t *path)
{
    robo_result_t res;
    robo_gfx_point_t unused;

    if (!path) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!path->open_contour) {
        return ROBO_OK;
    }
    memset(&unused, 0, sizeof(unused));
    res = append_cmd(path, ROBO_GFX_PATH_CLOSE, unused);
    if (res != ROBO_OK) {
        return res;
    }
    /* Contour closed; current returns to contour start. Need MoveTo to continue. */
    path->current = path->contour_start;
    path->has_current = 1;
    path->open_contour = 0;
    return ROBO_OK;
}

robo_result_t robo_gfx_path_command_at(const robo_gfx_path_t *path,
                                       size_t index,
                                       robo_gfx_path_command_t *out)
{
    if (!path || !out || index >= path->count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = path->cmds[index];
    return ROBO_OK;
}

void robo_gfx_path_iter_begin(const robo_gfx_path_t *path,
                              robo_gfx_path_iter_t *iter)
{
    if (!iter) {
        return;
    }
    iter->path = path;
    iter->index = 0u;
}

int robo_gfx_path_iter_next(robo_gfx_path_iter_t *iter,
                            robo_gfx_path_command_t *out)
{
    if (!iter || !out || !iter->path) {
        return 0;
    }
    if (iter->index >= iter->path->count) {
        return 0;
    }
    *out = iter->path->cmds[iter->index];
    iter->index++;
    return 1;
}

robo_result_t robo_gfx_path_bounds(const robo_gfx_path_t *path,
                                   robo_gfx_rect_t *out_bounds)
{
    size_t i;
    int have = 0;
    int64_t min_x = 0;
    int64_t min_y = 0;
    int64_t max_x = 0;
    int64_t max_y = 0;

    if (!path || !out_bounds) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_bounds = robo_gfx_rect_empty();

    for (i = 0u; i < path->count; i++) {
        const robo_gfx_path_command_t *c = &path->cmds[i];
        int64_t x;
        int64_t y;

        if (c->verb != ROBO_GFX_PATH_MOVE_TO && c->verb != ROBO_GFX_PATH_LINE_TO) {
            continue;
        }
        x = (int64_t)c->point.x;
        y = (int64_t)c->point.y;
        if (!have) {
            min_x = max_x = x;
            min_y = max_y = y;
            have = 1;
        } else {
            if (x < min_x) {
                min_x = x;
            }
            if (x > max_x) {
                max_x = x;
            }
            if (y < min_y) {
                min_y = y;
            }
            if (y > max_y) {
                max_y = y;
            }
        }
    }

    if (!have) {
        return ROBO_OK;
    }

    /*
     * Half-open rect covering integer points [min, max]:
     * width = max - min + 1, height = max - min + 1.
     * Exclusive edges must fit int32 (right = max_x + 1).
     */
    if (max_x >= (int64_t)INT32_MAX || max_y >= (int64_t)INT32_MAX) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_rect_from_ltrb((int32_t)min_x, (int32_t)min_y,
                                   (int32_t)(max_x + 1),
                                   (int32_t)(max_y + 1), out_bounds);
}
