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
typedef uint64_t robo_window_id_t;

#define ROBO_WINDOW_ID_NONE ((robo_window_id_t)0u)

/** Soft ceiling for simultaneous windows in Goal 7.1. */
#define ROBO_WINDOW_MAX_WINDOWS 64u

/** Copied UTF-8 title capacity (including NUL). */
#define ROBO_WINDOW_TITLE_MAX 256u

/** Bounded in-process event queue depth (architecture; not a full pump). */
#define ROBO_WINDOW_EVENT_QUEUE_CAPACITY 128u

/** Soft max logical width/height accepted in configuration. */
#define ROBO_WINDOW_MAX_DIM 16384

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_TYPES_H */
