/*
 * RoboGraphics paths foundation (Goal 6.8).
 *
 * Geometry/data-model only: MoveTo / LineTo / Close (+ reserved curve verbs).
 * No fill, stroke, flatten, or rasterization.
 *
 * Ownership: path owns its command storage via RoboMem. retain/release.
 * Threading: not concurrently mutable unless the caller synchronizes.
 *
 * See graphics/docs/Paths.md.
 */

#ifndef ROBO_GRAPHICS_PATH_H
#define ROBO_GRAPHICS_PATH_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Path command verbs. Values are stable for ABI growth.
 * Curve verbs are reserved; construction APIs for them are not in Goal 6.8.
 */
typedef enum robo_gfx_path_verb {
    ROBO_GFX_PATH_MOVE_TO = 1,
    ROBO_GFX_PATH_LINE_TO = 2,
    ROBO_GFX_PATH_CLOSE = 3,
    ROBO_GFX_PATH_QUAD_TO = 4,  /* reserved — not constructible in 6.8 */
    ROBO_GFX_PATH_CUBIC_TO = 5  /* reserved — not constructible in 6.8 */
} robo_gfx_path_verb_t;

typedef uint64_t robo_gfx_path_id_t;
#define ROBO_GFX_PATH_ID_NONE ((robo_gfx_path_id_t)0u)

typedef struct robo_gfx_path robo_gfx_path_t;

/**
 * One recorded command for iteration.
 * MOVE_TO / LINE_TO: @p point is valid.
 * CLOSE: @p point is undefined (ignore).
 */
typedef struct robo_gfx_path_command {
    robo_gfx_path_verb_t verb;
    robo_gfx_point_t point;
} robo_gfx_path_command_t;

typedef struct robo_gfx_path_iter {
    const robo_gfx_path_t *path;
    size_t index;
} robo_gfx_path_iter_t;

/** Create an empty owned path. OOM → ROBO_OUT_OF_MEMORY. */
robo_result_t robo_gfx_path_create(robo_gfx_path_t **out);

void robo_gfx_path_retain(robo_gfx_path_t *path);
void robo_gfx_path_release(robo_gfx_path_t *path);

robo_gfx_path_id_t robo_gfx_path_id(const robo_gfx_path_t *path);

/** Remove all commands; keep allocated capacity. */
void robo_gfx_path_reset(robo_gfx_path_t *path);

/** Remove all commands and free command storage capacity. */
void robo_gfx_path_clear(robo_gfx_path_t *path);

int robo_gfx_path_is_empty(const robo_gfx_path_t *path);
size_t robo_gfx_path_command_count(const robo_gfx_path_t *path);

/** 1 if a current point exists (open or just-closed contour). */
int robo_gfx_path_has_current_point(const robo_gfx_path_t *path);
robo_result_t robo_gfx_path_current_point(const robo_gfx_path_t *path,
                                          robo_gfx_point_t *out);

/** 1 if the latest subpath is open (accepts LineTo / Close). */
int robo_gfx_path_has_open_contour(const robo_gfx_path_t *path);

/**
 * Begin a new subpath at @p p. Always succeeds for a valid path.
 * Ends any open contour without inserting Close.
 */
robo_result_t robo_gfx_path_move_to(robo_gfx_path_t *path, robo_gfx_point_t p);

/**
 * Append a line to @p p.
 * Requires an open contour (after MoveTo / LineTo). Otherwise →
 * ROBO_INVALID_ARGUMENT. After Close, call MoveTo before LineTo.
 */
robo_result_t robo_gfx_path_line_to(robo_gfx_path_t *path, robo_gfx_point_t p);

/**
 * Close the current open contour (append Close verb).
 * No open contour → ROBO_OK no-op (no verb appended).
 * Close after MoveTo alone is valid (records Close).
 * After Close, LineTo is invalid until the next MoveTo.
 */
robo_result_t robo_gfx_path_close(robo_gfx_path_t *path);

/**
 * Read command at @p index (0 .. count-1).
 * Out of range → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_path_command_at(const robo_gfx_path_t *path,
                                       size_t index,
                                       robo_gfx_path_command_t *out);

void robo_gfx_path_iter_begin(const robo_gfx_path_t *path,
                              robo_gfx_path_iter_t *iter);

/**
 * Advance iterator. Returns 1 and fills @p out when a command remains;
 * returns 0 at end. NULL args → 0.
 */
int robo_gfx_path_iter_next(robo_gfx_path_iter_t *iter,
                            robo_gfx_path_command_t *out);

/**
 * Axis-aligned bounds of all MoveTo/LineTo points (half-open Rect).
 * Empty path or MoveTo-only with no points stored oddly → empty rect, ROBO_OK.
 * Close does not add points; start/end already included via MoveTo/LineTo.
 * Overflow when forming width/height → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_gfx_path_bounds(const robo_gfx_path_t *path,
                                   robo_gfx_rect_t *out_bounds);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_PATH_H */
