/*
 * RoboWindow platform-independent events (Goal 7.1).
 *
 * Events reference robo_window_id_t — never native handles.
 * Keyboard/mouse/pointer belong to later Phase 7 / Phase 12 goals.
 *
 * Dispatch boundary:
 *   backend acquisition → translate → robo_window_event_push
 *   → application polls via robo_window_event_poll
 *
 * This is not a full event loop (Goal 7.9).
 */

#ifndef ROBO_WINDOW_EVENT_H
#define ROBO_WINDOW_EVENT_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_window_event_type {
    ROBO_WINDOW_EVENT_NONE = 0,
    ROBO_WINDOW_EVENT_CREATED = 1,
    ROBO_WINDOW_EVENT_SHOWN = 2,
    ROBO_WINDOW_EVENT_HIDDEN = 3,
    ROBO_WINDOW_EVENT_MOVED = 4,
    ROBO_WINDOW_EVENT_RESIZED = 5,
    ROBO_WINDOW_EVENT_CLOSE_REQUESTED = 6,
    ROBO_WINDOW_EVENT_CLOSED = 7,
    ROBO_WINDOW_EVENT_FOCUS_GAINED = 8,
    ROBO_WINDOW_EVENT_FOCUS_LOST = 9,
    ROBO_WINDOW_EVENT_MINIMIZED = 10,
    ROBO_WINDOW_EVENT_MAXIMIZED = 11,
    ROBO_WINDOW_EVENT_RESTORED = 12,
    ROBO_WINDOW_EVENT_DISPLAY_CHANGED = 13,
    ROBO_WINDOW_EVENT_SCALE_CHANGED = 14
} robo_window_event_type_t;

typedef struct robo_window_event {
    robo_window_event_type_t type;
    robo_window_id_t window_id;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    float scale;
    robo_display_id_t display_id;
} robo_window_event_t;

void robo_window_event_init(robo_window_event_t *ev);

int robo_window_event_type_is_known(robo_window_event_type_t type);

const char *robo_window_event_type_name(robo_window_event_type_t type);

/**
 * Backend / core → queue. Requires subsystem initialized.
 * Invalid type / NONE → ROBO_INVALID_ARGUMENT.
 * Full queue → ROBO_BUSY.
 */
robo_result_t robo_window_event_push(const robo_window_event_t *ev);

/**
 * Consumer dequeue (FIFO). Empty → ROBO_NOT_FOUND.
 */
robo_result_t robo_window_event_poll(robo_window_event_t *out);

/** Drop all queued events (tests / shutdown). */
void robo_window_event_queue_clear(void);

size_t robo_window_event_queue_size(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_EVENT_H */
