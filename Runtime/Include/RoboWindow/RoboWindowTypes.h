/*
 * RoboWindow fundamental IDs and limits (Goal 7.1).
 *
 * Window and display identities are runtime-owned integers — never native
 * OS handles. Native resources attach via RoboWindowNative when backends exist.
 */

#ifndef ROBO_WINDOW_TYPES_H
#define ROBO_WINDOW_TYPES_H

#include "RoboDisplay/RoboDisplayTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque window identity (stable while the window exists; 0 = none). */
typedef uint64_t WindowId;

#define RoboWindowIdNone ((WindowId)0u)

/** Soft ceiling for simultaneous windows in Goal 7.1. */
#define RoboWindowMaxWindows 64u

/** Copied UTF-8 title capacity (including NUL). */
#define RoboWindowTitleMax 256u

/** Bounded in-process event queue depth (architecture; not a full pump). */
#define RoboWindowEventQueueCapacity 128u

/** Soft max logical width/height accepted in configuration. */
#define RoboWindowMaxDim 16384

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_TYPES_H */
