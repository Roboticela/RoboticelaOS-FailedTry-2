/*
 * RoboWindow sizing helpers (Goal 7.6).
 *
 * Logical client size is distinct from display resolution, framebuffer
 * dimensions, and DPI/scale (Goal 7.8). Uses robo_gfx_size_t — no duplicate
 * size type.
 *
 * Constraint semantics (rejection, not silent clamp at the public API):
 *   min_width/min_height/max_* == 0 means no constraint on that axis
 *   both set: min <= max required
 *   min == max: fixed size allowed
 *   resize outside constraints: ROBO_INVALID_ARGUMENT before backend
 * Backend may still report a different actual size (OS clamp); then core
 * stores requested vs actual separately when reports_actual_state is set.
 */

#ifndef ROBO_WINDOW_SIZE_H
#define ROBO_WINDOW_SIZE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_window_size_constraints {
    int32_t min_width;  /* 0 = none */
    int32_t min_height; /* 0 = none */
    int32_t max_width;  /* 0 = none */
    int32_t max_height; /* 0 = none */
} robo_window_size_constraints_t;

void robo_window_size_constraints_init(robo_window_size_constraints_t *c);

/**
 * Validate constraint fields alone (not against a candidate size).
 * Negative → ROBO_INVALID_ARGUMENT.
 * max > ROBO_WINDOW_MAX_DIM → ROBO_INVALID_ARGUMENT.
 * min > max (both > 0) → ROBO_INVALID_ARGUMENT.
 * Equal min/max (> 0) → OK.
 */
robo_result_t robo_window_size_constraints_validate(
    const robo_window_size_constraints_t *c);

/** Positive dimensions within ROBO_WINDOW_MAX_DIM. */
robo_result_t robo_window_size_validate(int32_t width, int32_t height);

/**
 * Validate @p width/@p height against @p c (NULL constraints → size only).
 */
robo_result_t robo_window_size_validate_against_constraints(
    int32_t width,
    int32_t height,
    const robo_window_size_constraints_t *c);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_SIZE_H */
