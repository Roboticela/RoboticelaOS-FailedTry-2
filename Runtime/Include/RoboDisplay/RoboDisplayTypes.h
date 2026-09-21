/*
 * RoboDisplay identity and enumerations (Goal 7.3).
 *
 * Display IDs are runtime tokens — never OS handles, never window IDs.
 * Index in an enumeration is not a permanent identity.
 */

#ifndef ROBO_DISPLAY_TYPES_H
#define ROBO_DISPLAY_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t DisplayId;

#define RoboDisplayIdNone ((DisplayId)0u)

#define RoboDisplayMaxDisplays 16u
#define RoboDisplayNameMax 128u
#define RoboDisplayMaxModes 32u

typedef enum DisplayOrientation {
    RoboDisplayOrientationUnknown = 0,
    RoboDisplayOrientationLandscape = 1,
    RoboDisplayOrientationPortrait = 2,
    RoboDisplayOrientationLandscapeFlipped = 3,
    RoboDisplayOrientationPortraitFlipped = 4
} DisplayOrientation;

typedef enum DisplayConnection {
    RoboDisplayConnectionUnknown = 0,
    RoboDisplayConnectionConnected = 1,
    RoboDisplayConnectionDisconnected = 2,
    RoboDisplayConnectionUnavailable = 3
} DisplayConnection;

int DisplayIdIsNone(DisplayId id);
int DisplayIdEqual(DisplayId a, DisplayId b);

int DisplayOrientationIsKnown(DisplayOrientation o);
const char *DisplayOrientationName(DisplayOrientation o);

const char *DisplayConnectionName(DisplayConnection c);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_TYPES_H */
