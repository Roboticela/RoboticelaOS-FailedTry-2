#include "RoboDisplay/RoboDisplayTypes.h"

int DisplayIdIsNone(DisplayId id)
{
    return id == RoboDisplayIdNone;
}

int DisplayIdEqual(DisplayId a, DisplayId b)
{
    return a == b && a != RoboDisplayIdNone;
}

int DisplayOrientationIsKnown(DisplayOrientation o)
{
    return o == RoboDisplayOrientationLandscape ||
           o == RoboDisplayOrientationPortrait ||
           o == RoboDisplayOrientationLandscapeFlipped ||
           o == RoboDisplayOrientationPortraitFlipped;
}

const char *DisplayOrientationName(DisplayOrientation o)
{
    switch (o) {
    case RoboDisplayOrientationUnknown:
        return "unknown";
    case RoboDisplayOrientationLandscape:
        return "landscape";
    case RoboDisplayOrientationPortrait:
        return "portrait";
    case RoboDisplayOrientationLandscapeFlipped:
        return "landscape_flipped";
    case RoboDisplayOrientationPortraitFlipped:
        return "portrait_flipped";
    default:
        return "invalid";
    }
}

const char *DisplayConnectionName(DisplayConnection c)
{
    switch (c) {
    case RoboDisplayConnectionUnknown:
        return "unknown";
    case RoboDisplayConnectionConnected:
        return "connected";
    case RoboDisplayConnectionDisconnected:
        return "disconnected";
    case RoboDisplayConnectionUnavailable:
        return "unavailable";
    default:
        return "invalid";
    }
}
