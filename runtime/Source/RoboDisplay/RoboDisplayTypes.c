#include "RoboDisplay/RoboDisplayTypes.h"

int robo_display_id_is_none(robo_display_id_t id)
{
    return id == ROBO_DISPLAY_ID_NONE;
}

int robo_display_id_equal(robo_display_id_t a, robo_display_id_t b)
{
    return a == b && a != ROBO_DISPLAY_ID_NONE;
}

int robo_display_orientation_is_known(robo_display_orientation_t o)
{
    return o == ROBO_DISPLAY_ORIENTATION_LANDSCAPE ||
           o == ROBO_DISPLAY_ORIENTATION_PORTRAIT ||
           o == ROBO_DISPLAY_ORIENTATION_LANDSCAPE_FLIPPED ||
           o == ROBO_DISPLAY_ORIENTATION_PORTRAIT_FLIPPED;
}

const char *robo_display_orientation_name(robo_display_orientation_t o)
{
    switch (o) {
    case ROBO_DISPLAY_ORIENTATION_UNKNOWN:
        return "unknown";
    case ROBO_DISPLAY_ORIENTATION_LANDSCAPE:
        return "landscape";
    case ROBO_DISPLAY_ORIENTATION_PORTRAIT:
        return "portrait";
    case ROBO_DISPLAY_ORIENTATION_LANDSCAPE_FLIPPED:
        return "landscape_flipped";
    case ROBO_DISPLAY_ORIENTATION_PORTRAIT_FLIPPED:
        return "portrait_flipped";
    default:
        return "invalid";
    }
}

const char *robo_display_connection_name(robo_display_connection_t c)
{
    switch (c) {
    case ROBO_DISPLAY_CONNECTION_UNKNOWN:
        return "unknown";
    case ROBO_DISPLAY_CONNECTION_CONNECTED:
        return "connected";
    case ROBO_DISPLAY_CONNECTION_DISCONNECTED:
        return "disconnected";
    case ROBO_DISPLAY_CONNECTION_UNAVAILABLE:
        return "unavailable";
    default:
        return "invalid";
    }
}
