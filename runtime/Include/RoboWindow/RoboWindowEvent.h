/*
 * RoboWindow platform-independent events (Goals 7.1 / 7.9 / 7.10 / 7.11).
 *
 * Events reference robo_window_id_t — never native handles.
 * Touch / stylus / generic pointer / IME deferred.
 *
 * Dispatch boundary:
 *   native OS → backend process_events → observe_* → push
 *   → application polls via robo_window_event_poll
 */

#ifndef ROBO_WINDOW_EVENT_H
#define ROBO_WINDOW_EVENT_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowMouse.h"
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
    ROBO_WINDOW_EVENT_SCALE_CHANGED = 14,
    ROBO_WINDOW_EVENT_KEY_DOWN = 15,
    ROBO_WINDOW_EVENT_KEY_UP = 16,
    ROBO_WINDOW_EVENT_MOUSE_MOVE = 17,
    ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN = 18,
    ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP = 19,
    ROBO_WINDOW_EVENT_MOUSE_WHEEL = 20
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
    /** Valid when type is KEY_DOWN / KEY_UP. */
    robo_keyboard_event_t key;
    /** Valid when type is MOUSE_MOVE / BUTTON_DOWN / BUTTON_UP / WHEEL. */
    robo_mouse_event_t mouse;
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

/**
 * Peek at the next event without dequeue. Empty → ROBO_NOT_FOUND.
 */
robo_result_t robo_window_event_peek(robo_window_event_t *out);

/** Non-zero when the queue has no events. */
int robo_window_event_queue_is_empty(void);

/** Drop all queued events (tests / shutdown). */
void robo_window_event_queue_clear(void);

size_t robo_window_event_queue_size(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_EVENT_H */
