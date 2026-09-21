/*
 * RoboDisplay backend capabilities (Goal 7.3).
 */

#ifndef ROBO_DISPLAY_CAPABILITIES_H
#define ROBO_DISPLAY_CAPABILITIES_H

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DisplayBackendCaps {
    int enumerate;
    int primary;
    int GetInfo;
    int workArea;
    int refreshRate;
    int scaleDpi;
    int orientation;
    int modes;
} DisplayBackendCaps;

void DisplayBackendCapsClear(DisplayBackendCaps *caps);

Result DisplayBackendCapabilities(DisplayBackendCaps *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_CAPABILITIES_H */
