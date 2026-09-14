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

typedef uint64_t robo_display_id_t;

#define ROBO_DISPLAY_ID_NONE ((robo_display_id_t)0u)

#define ROBO_DISPLAY_MAX_DISPLAYS 16u
#define ROBO_DISPLAY_NAME_MAX 128u
#define ROBO_DISPLAY_MAX_MODES 32u

typedef enum robo_display_orientation {
    ROBO_DISPLAY_ORIENTATION_UNKNOWN = 0,
    ROBO_DISPLAY_ORIENTATION_LANDSCAPE = 1,
    ROBO_DISPLAY_ORIENTATION_PORTRAIT = 2,
    ROBO_DISPLAY_ORIENTATION_LANDSCAPE_FLIPPED = 3,
    ROBO_DISPLAY_ORIENTATION_PORTRAIT_FLIPPED = 4
} robo_display_orientation_t;

typedef enum robo_display_connection {
    ROBO_DISPLAY_CONNECTION_UNKNOWN = 0,
    ROBO_DISPLAY_CONNECTION_CONNECTED = 1,
    ROBO_DISPLAY_CONNECTION_DISCONNECTED = 2,
    ROBO_DISPLAY_CONNECTION_UNAVAILABLE = 3
} robo_display_connection_t;

int robo_display_id_is_none(robo_display_id_t id);
int robo_display_id_equal(robo_display_id_t a, robo_display_id_t b);

int robo_display_orientation_is_known(robo_display_orientation_t o);
const char *robo_display_orientation_name(robo_display_orientation_t o);

const char *robo_display_connection_name(robo_display_connection_t c);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DISPLAY_TYPES_H */
