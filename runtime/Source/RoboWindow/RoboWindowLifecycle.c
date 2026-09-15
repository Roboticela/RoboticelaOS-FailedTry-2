#include "RoboWindow/RoboWindowLifecycle.h"

#include <stddef.h>

int robo_window_lifecycle_is_known(robo_window_lifecycle_t life)
{
    return life == ROBO_WINDOW_LIFECYCLE_UNINITIALIZED ||
           life == ROBO_WINDOW_LIFECYCLE_CREATED ||
           life == ROBO_WINDOW_LIFECYCLE_CLOSING ||
           life == ROBO_WINDOW_LIFECYCLE_CLOSED;
}

int robo_window_lifecycle_can_transition(robo_window_lifecycle_t from,
                                         robo_window_lifecycle_t to)
{
    if (!robo_window_lifecycle_is_known(from) ||
        !robo_window_lifecycle_is_known(to)) {
        return 0;
    }
    if (from == ROBO_WINDOW_LIFECYCLE_UNINITIALIZED &&
        to == ROBO_WINDOW_LIFECYCLE_CREATED) {
        return 1;
    }
    if (from == ROBO_WINDOW_LIFECYCLE_CREATED &&
        (to == ROBO_WINDOW_LIFECYCLE_CLOSING ||
         to == ROBO_WINDOW_LIFECYCLE_CLOSED)) {
        return 1;
    }
    if (from == ROBO_WINDOW_LIFECYCLE_CLOSING &&
        to == ROBO_WINDOW_LIFECYCLE_CLOSED) {
        return 1;
    }
    if (from == ROBO_WINDOW_LIFECYCLE_CLOSED &&
        to == ROBO_WINDOW_LIFECYCLE_CLOSED) {
        return 1;
    }
    return 0;
}

robo_result_t robo_window_lifecycle_check_transition(robo_window_lifecycle_t from,
                                                     robo_window_lifecycle_t to)
{
    return robo_window_lifecycle_can_transition(from, to) ? ROBO_OK
                                                          : ROBO_INVALID_ARGUMENT;
}

const char *robo_window_lifecycle_name(robo_window_lifecycle_t life)
{
    switch (life) {
    case ROBO_WINDOW_LIFECYCLE_UNINITIALIZED:
        return "uninitialized";
    case ROBO_WINDOW_LIFECYCLE_CREATED:
        return "created";
    case ROBO_WINDOW_LIFECYCLE_CLOSING:
        return "closing";
    case ROBO_WINDOW_LIFECYCLE_CLOSED:
        return "closed";
    default:
        return "unknown";
    }
}

int robo_window_lifecycle_allows_mutation(robo_window_lifecycle_t life)
{
    return life == ROBO_WINDOW_LIFECYCLE_CREATED;
}
