/*
 * RoboDesktop's small vocabularies (Phase 11.13) — areas and window states.
 *
 * Both are names for numbers a reader meets in a diagnostic and in the shell dump, and both answer
 * `"unknown"` rather than NULL for a value outside their range: a dump that printed `(null)` for an area
 * would be a dump a reader could not tell from a missing field, which is the rule RoboRender's domain
 * names state and this file keeps.
 *
 * The area names are spelled the way a *log* line reads (`window-content`, `window-close`) rather than
 * the way an enumerator is spelled, and the state names are single words a person would use. That
 * difference is deliberate: a state is a value a caller compares, and an area is a place a person reads
 * about in a log.
 */

#include "RoboDesktop/RoboDesktopTypes.h"

static const char *const desktop_area_names[ROBO_DESKTOP_AREA_COUNT] = {
    "none",
    "background",
    "panel",
    "window-title",
    "window-content",
    "window-close"
};

int robo_desktop_area_is_known(robo_desktop_area_t area)
{
    size_t index = (size_t)area;

    return index < (size_t)ROBO_DESKTOP_AREA_COUNT;
}

const char *robo_desktop_area_name(robo_desktop_area_t area)
{
    if (!robo_desktop_area_is_known(area)) {
        return "unknown";
    }
    return desktop_area_names[(size_t)area];
}

int robo_desktop_window_state_is_known(robo_desktop_window_state_t state)
{
    return state == ROBO_DESKTOP_WINDOW_OPEN || state == ROBO_DESKTOP_WINDOW_CLOSING;
}

const char *robo_desktop_window_state_name(robo_desktop_window_state_t state)
{
    switch (state) {
    case ROBO_DESKTOP_WINDOW_OPEN:
        return "open";
    case ROBO_DESKTOP_WINDOW_CLOSING:
        return "closing";
    default:
        return "unknown";
    }
}
