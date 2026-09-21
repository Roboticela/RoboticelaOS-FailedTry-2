/*
 * RoboDisplay backend contract (Goal 7.3).
 *
 * Platform backends will enumerate real monitors later. Goal 7.3 ships
 * null (empty) and placeholder (one architecture display) backends only.
 */

#ifndef ROBO_DISPLAY_BACKEND_H
#define ROBO_DISPLAY_BACKEND_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplayCapabilities.h"
#include "RoboDisplay/RoboDisplayInfo.h"
#include "RoboDisplay/RoboDisplayTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DisplayBackendVtbl {
    const char *name;
    const DisplayBackendCaps *capabilities;

    Result (*initialize)(void *userdata);
    void (*shutdown)(void *userdata);

    /**
     * Fill up to @p capacity displays; write count to @p outCount.
     * Order is backend-defined; primary may not be index 0.
     */
    Result (*enumerate)(void *userdata,
                               DisplayInfo *out,
                               size_t capacity,
                               size_t *outCount);

    Result (*GetPrimary)(void *userdata, DisplayInfo *out);

    Result (*GetInfo)(void *userdata,
                              DisplayId id,
                              DisplayInfo *out);

    /**
     * Optional mode list. May be NULL → treated as RoboNotSupported.
     */
    Result (*GetModes)(void *userdata,
                               DisplayId id,
                               DisplayMode *out,
                               size_t capacity,
                               size_t *outCount);
} DisplayBackendVtbl;

Result DisplayBackendRegister(const DisplayBackendVtbl *vtbl,
                                            void *userdata);

void DisplayBackendClear(void);

int DisplayBackendIsRegistered(void);

const char *DisplayBackendName(void);

/** Empty enumeration (no host monitors). */
const DisplayBackendVtbl *DisplayBackendNullVtbl(void);
Result DisplayBackendUseNull(void);

/**
 * Single architecture placeholder primary (1280×800 @ scale 1).
 * Not a real host display.
 */
const DisplayBackendVtbl *DisplayBackendPlaceholderVtbl(void);
Result DisplayBackendUsePlaceholder(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_BACKEND_H */
