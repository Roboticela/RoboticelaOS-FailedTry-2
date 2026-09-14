#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static robo_window_event_t g_queue[ROBO_WINDOW_EVENT_QUEUE_CAPACITY];
static size_t g_head;
static size_t g_tail;
static size_t g_count;

void robo_window_event_queue_reset(void)
{
    g_head = 0u;
    g_tail = 0u;
    g_count = 0u;
    memset(g_queue, 0, sizeof(g_queue));
}

void robo_window_event_init(robo_window_event_t *ev)
{
    if (!ev) {
        return;
    }
    memset(ev, 0, sizeof(*ev));
    ev->type = ROBO_WINDOW_EVENT_NONE;
    ev->window_id = ROBO_WINDOW_ID_NONE;
    ev->display_id = ROBO_DISPLAY_ID_NONE;
}

int robo_window_event_type_is_known(robo_window_event_type_t type)
{
    return type >= ROBO_WINDOW_EVENT_CREATED &&
           type <= ROBO_WINDOW_EVENT_SCALE_CHANGED;
}

const char *robo_window_event_type_name(robo_window_event_type_t type)
{
    switch (type) {
    case ROBO_WINDOW_EVENT_NONE:
        return "none";
    case ROBO_WINDOW_EVENT_CREATED:
        return "created";
    case ROBO_WINDOW_EVENT_SHOWN:
        return "shown";
    case ROBO_WINDOW_EVENT_HIDDEN:
        return "hidden";
    case ROBO_WINDOW_EVENT_MOVED:
        return "moved";
    case ROBO_WINDOW_EVENT_RESIZED:
        return "resized";
    case ROBO_WINDOW_EVENT_CLOSE_REQUESTED:
        return "close_requested";
    case ROBO_WINDOW_EVENT_CLOSED:
        return "closed";
    case ROBO_WINDOW_EVENT_FOCUS_GAINED:
        return "focus_gained";
    case ROBO_WINDOW_EVENT_FOCUS_LOST:
        return "focus_lost";
    case ROBO_WINDOW_EVENT_MINIMIZED:
        return "minimized";
    case ROBO_WINDOW_EVENT_MAXIMIZED:
        return "maximized";
    case ROBO_WINDOW_EVENT_RESTORED:
        return "restored";
    case ROBO_WINDOW_EVENT_DISPLAY_CHANGED:
        return "display_changed";
    case ROBO_WINDOW_EVENT_SCALE_CHANGED:
        return "scale_changed";
    default:
        return "unknown";
    }
}

robo_result_t robo_window_event_push(const robo_window_event_t *ev)
{
    if (!ev || !robo_window_event_type_is_known(ev->type)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    if (g_count >= ROBO_WINDOW_EVENT_QUEUE_CAPACITY) {
        robo_log_warn("robo_window: event queue full (type=%s)",
                      robo_window_event_type_name(ev->type));
        return ROBO_BUSY;
    }
    g_queue[g_tail] = *ev;
    g_tail = (g_tail + 1u) % ROBO_WINDOW_EVENT_QUEUE_CAPACITY;
    g_count++;
    return ROBO_OK;
}

robo_result_t robo_window_event_poll(robo_window_event_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_window_is_initialized()) {
        return ROBO_INIT_ERROR;
    }
    if (g_count == 0u) {
        return ROBO_NOT_FOUND;
    }
    *out = g_queue[g_head];
    g_head = (g_head + 1u) % ROBO_WINDOW_EVENT_QUEUE_CAPACITY;
    g_count--;
    return ROBO_OK;
}

void robo_window_event_queue_clear(void)
{
    robo_window_event_queue_reset();
}

size_t robo_window_event_queue_size(void)
{
    return g_count;
}
