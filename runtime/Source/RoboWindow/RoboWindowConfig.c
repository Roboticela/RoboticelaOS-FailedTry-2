#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <string.h>

void robo_window_config_init(robo_window_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->width = 800;
    cfg->height = 600;
    cfg->x = ROBO_WINDOW_COORD_UNKNOWN;
    cfg->y = ROBO_WINDOW_COORD_UNKNOWN;
    cfg->visible = 1;
    cfg->resizable = 1;
    cfg->fullscreen = 0;
    cfg->title = NULL;
    cfg->display_id = ROBO_DISPLAY_ID_NONE;
    cfg->scale_preference = 0.0f;
}

static int dim_ok(int32_t v)
{
    return v > 0 && v <= ROBO_WINDOW_MAX_DIM;
}

robo_result_t robo_window_config_validate(const robo_window_config_t *cfg)
{
    if (!cfg) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!dim_ok(cfg->width) || !dim_ok(cfg->height)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (cfg->min_width < 0 || cfg->min_height < 0 || cfg->max_width < 0 ||
        cfg->max_height < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (cfg->min_width > 0 && cfg->min_width > cfg->width) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (cfg->min_height > 0 && cfg->min_height > cfg->height) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (cfg->max_width > 0) {
        if (cfg->max_width > ROBO_WINDOW_MAX_DIM) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (cfg->width > cfg->max_width) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (cfg->min_width > 0 && cfg->min_width > cfg->max_width) {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    if (cfg->max_height > 0) {
        if (cfg->max_height > ROBO_WINDOW_MAX_DIM) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (cfg->height > cfg->max_height) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (cfg->min_height > 0 && cfg->min_height > cfg->max_height) {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    if (cfg->scale_preference < 0.0f) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}
