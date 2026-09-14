/*
 * RoboWindow configuration (Goal 7.1) — requested properties at create time.
 *
 * Configuration is what the caller asks for. Runtime state (RoboWindowState)
 * is what the system currently reports. Backends may not honor every field.
 */

#ifndef ROBO_WINDOW_CONFIG_H
#define ROBO_WINDOW_CONFIG_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_window_config {
    int32_t width;  /* required > 0 */
    int32_t height; /* required > 0 */
    int32_t x; /* ROBO_WINDOW_COORD_UNKNOWN → OS default placement */
    int32_t y;
    int32_t min_width;  /* 0 = none */
    int32_t min_height; /* 0 = none */
    int32_t max_width;  /* 0 = none */
    int32_t max_height; /* 0 = none */
    const char *title;  /* borrowed UTF-8; NULL → empty; copied on create */
    int visible;        /* non-zero → show after create when backend allows */
    int resizable;      /* non-zero → resizable preference */
    int fullscreen;     /* non-zero → fullscreen preference (may be deferred) */
    robo_display_id_t display_id; /* NONE → default/primary */
    float scale_preference;       /* 0 → unspecified; else desired scale */
} robo_window_config_t;

/** Fill defaults: 800×600, visible, resizable, empty title, no min/max. */
void robo_window_config_init(robo_window_config_t *cfg);

/**
 * Validate dimensions and min/max consistency.
 * Does not require a live backend.
 */
robo_result_t robo_window_config_validate(const robo_window_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_CONFIG_H */
