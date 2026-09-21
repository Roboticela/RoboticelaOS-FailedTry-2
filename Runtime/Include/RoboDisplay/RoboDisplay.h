/*
 * RoboDisplay — platform-independent display manager (Goal 7.3).
 *
 * Dependency:
 *   RoboBase → RoboGraphics geometry → RoboDisplay → future OS backends
 *
 * Independent from RoboWindow. Windows may query/associate displays via
 * RoboWindow helpers without owning the display backend.
 *
 * Chromium ui/display is temporary technical reference only.
 */

#ifndef ROBO_DISPLAY_H
#define ROBO_DISPLAY_H

#include "RoboBase/RoboError.h"
#include "RoboDisplay/RoboDisplayBackend.h"
#include "RoboDisplay/RoboDisplayCapabilities.h"
#include "RoboDisplay/RoboDisplayInfo.h"
#include "RoboDisplay/RoboDisplayScale.h"
#include "RoboDisplay/RoboDisplayTypes.h"
#include "RoboDisplay/RoboDisplayVersion.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *DisplayVersionString(void);

Result DisplayInit(void);
void DisplayShutdown(void);
int DisplayIsInitialized(void);

/** Re-enumerate from the active backend into the manager cache. */
Result DisplayRefresh(void);

size_t DisplayCount(void);

Result DisplayAt(size_t index, DisplayInfo *out);

Result DisplayGet(DisplayId id, DisplayInfo *out);

/** Primary connected display, or RoboNotFound. */
Result DisplayPrimary(DisplayInfo *out);

/** Non-zero if @p id is present in the current cache. */
int DisplayIdIsKnown(DisplayId id);

/**
 * Current modes for @p id. No modes capability → RoboNotSupported.
 */
Result DisplayGetModes(DisplayId id,
                                     DisplayMode *out,
                                     size_t capacity,
                                     size_t *outCount);

/**
 * Effective scale for @p id (Goal 7.8).
 * Uses info.scale, else derives from dpiX. Unavailable → RoboNotFound.
 */
Result DisplayGetScale(DisplayId id, float *outScale);

/**
 * DPI pair for @p id. Axes may be individually unavailable (0).
 * Unknown display → RoboNotFound. Corrupt stored DPI → INVALID_ARGUMENT.
 */
Result DisplayGetDpi(DisplayId id,
                                   DisplayDpi *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_H */
