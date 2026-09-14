/*
 * RoboDisplay backend capabilities (Goal 7.3).
 */

#ifndef ROBO_DISPLAY_CAPABILITIES_H
#define ROBO_DISPLAY_CAPABILITIES_H

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_display_backend_caps {
    int enumerate;
    int primary;
    int get_info;
    int work_area;
    int refresh_rate;
    int scale_dpi;
    int orientation;
    int modes;
} robo_display_backend_caps_t;

void robo_display_backend_caps_clear(robo_display_backend_caps_t *caps);

robo_result_t robo_display_backend_capabilities(robo_display_backend_caps_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_CAPABILITIES_H */
