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

static const char *const desktopAreaNames[RoboDesktopAreaCount] = {
    "none",
    "background",
    "panel",
    "window-title",
    "window-content",
    "window-close"
};

int DesktopAreaIsKnown(DesktopArea area)
{
    size_t index = (size_t)area;

    return index < (size_t)RoboDesktopAreaCount;
}

const char *DesktopAreaName(DesktopArea area)
{
    if (!DesktopAreaIsKnown(area)) {
        return "unknown";
    }
    return desktopAreaNames[(size_t)area];
}

int DesktopWindowStateIsKnown(DesktopWindowState state)
{
    return state == RoboDesktopWindowOpen || state == RoboDesktopWindowClosing;
}

const char *DesktopWindowStateName(DesktopWindowState state)
{
    switch (state) {
    case RoboDesktopWindowOpen:
        return "open";
    case RoboDesktopWindowClosing:
        return "closing";
    default:
        return "unknown";
    }
}
