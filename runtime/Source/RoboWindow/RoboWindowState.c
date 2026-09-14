#include "RoboWindow/RoboWindowState.h"

#include <string.h>

void robo_window_state_init(robo_window_state_t *st)
{
    if (!st) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->lifecycle = ROBO_WINDOW_LIFECYCLE_UNINITIALIZED;
    st->x = ROBO_WINDOW_COORD_UNKNOWN;
    st->y = ROBO_WINDOW_COORD_UNKNOWN;
    st->requested_x = ROBO_WINDOW_COORD_UNKNOWN;
    st->requested_y = ROBO_WINDOW_COORD_UNKNOWN;
    st->scale = 0.0f;
    st->display_id = ROBO_DISPLAY_ID_NONE;
}
