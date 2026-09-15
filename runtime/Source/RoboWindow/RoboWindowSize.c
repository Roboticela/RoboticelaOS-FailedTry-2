#include "RoboWindow/RoboWindowSize.h"

#include <string.h>

void robo_window_size_constraints_init(robo_window_size_constraints_t *c)
{
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
}

robo_result_t robo_window_size_constraints_validate(
    const robo_window_size_constraints_t *c)
{
    if (!c) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->min_width < 0 || c->min_height < 0 || c->max_width < 0 ||
        c->max_height < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->min_width > ROBO_WINDOW_MAX_DIM || c->min_height > ROBO_WINDOW_MAX_DIM) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->max_width > ROBO_WINDOW_MAX_DIM || c->max_height > ROBO_WINDOW_MAX_DIM) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->min_width > 0 && c->max_width > 0 && c->min_width > c->max_width) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->min_height > 0 && c->max_height > 0 &&
        c->min_height > c->max_height) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_window_size_validate(int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (width > ROBO_WINDOW_MAX_DIM || height > ROBO_WINDOW_MAX_DIM) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_window_size_validate_against_constraints(
    int32_t width,
    int32_t height,
    const robo_window_size_constraints_t *c)
{
    robo_result_t r = robo_window_size_validate(width, height);
    if (r != ROBO_OK) {
        return r;
    }
    if (!c) {
        return ROBO_OK;
    }
    r = robo_window_size_constraints_validate(c);
    if (r != ROBO_OK) {
        return r;
    }
    if (c->min_width > 0 && width < c->min_width) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->min_height > 0 && height < c->min_height) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->max_width > 0 && width > c->max_width) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (c->max_height > 0 && height > c->max_height) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}
