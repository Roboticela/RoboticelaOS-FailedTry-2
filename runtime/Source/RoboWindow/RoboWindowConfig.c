#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowSize.h"
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

robo_result_t robo_window_config_validate(const robo_window_config_t *cfg)
{
    robo_window_size_constraints_t c;
    robo_result_t r;

    if (!cfg) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* 0 = unspecified; otherwise must be a known valid scale. */
    if (cfg->scale_preference != 0.0f &&
        robo_display_scale_validate(cfg->scale_preference) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    c.min_width = cfg->min_width;
    c.min_height = cfg->min_height;
    c.max_width = cfg->max_width;
    c.max_height = cfg->max_height;
    r = robo_window_size_constraints_validate(&c);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_window_size_validate_against_constraints(cfg->width, cfg->height,
                                                         &c);
}
