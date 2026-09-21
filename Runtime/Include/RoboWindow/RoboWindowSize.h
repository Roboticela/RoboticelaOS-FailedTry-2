/*
 * RoboWindow sizing helpers (Goal 7.6).
 *
 * Logical client size is distinct from display resolution, framebuffer
 * dimensions, and DPI/scale (Goal 7.8). Uses GfxSize — no duplicate
 * size type.
 *
 * Constraint semantics (rejection, not silent clamp at the public API):
 *   minWidth/minHeight/max_* == 0 means no constraint on that axis
 *   both set: min <= max required
 *   min == max: fixed size allowed
 *   resize outside constraints: RoboInvalidArgument before backend
 * Backend may still report a different actual size (OS clamp); then core
 * stores requested vs actual separately when reportsActualState is set.
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

typedef struct WindowSizeConstraints {
    int32_t minWidth;  /* 0 = none */
    int32_t minHeight; /* 0 = none */
    int32_t maxWidth;  /* 0 = none */
    int32_t maxHeight; /* 0 = none */
} WindowSizeConstraints;

void WindowSizeConstraintsInit(WindowSizeConstraints *c);

/**
 * Validate constraint fields alone (not against a candidate size).
 * Negative → RoboInvalidArgument.
 * max > RoboWindowMaxDim → RoboInvalidArgument.
 * min > max (both > 0) → RoboInvalidArgument.
 * Equal min/max (> 0) → OK.
 */
Result WindowSizeConstraintsValidate(
    const WindowSizeConstraints *c);

/** Positive dimensions within RoboWindowMaxDim. */
Result WindowSizeValidate(int32_t width, int32_t height);

/**
 * Validate @p width/@p height against @p c (NULL constraints → size only).
 */
Result WindowSizeValidateAgainstConstraints(
    int32_t width,
    int32_t height,
    const WindowSizeConstraints *c);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_SIZE_H */
