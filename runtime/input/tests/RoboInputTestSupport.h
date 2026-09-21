/*
 * Shared RoboInput test fixtures (Phase 12.1).
 *
 * Every suite in `input/tests` includes this. Like the paint, render and desktop support headers it is
 * **not** a framework -- the runtime has exactly one of those (`Tests/Framework/RoboTest.h`) and this
 * release adds none -- and it holds the four things an input suite would otherwise copy:
 *
 *   bring-up    RoboBase, so a diagnostic or a log line behaves as it does in the runtime. There is no
 *               `robo_input_init` to call, and that is this module's design rather than an omission: it owns
 *               no state, so a fixture is a `robo_base_init` and nothing else. Nothing here allocates a
 *               module-wide object, which is why two suites cannot disturb each other's input state;
 *
 *   an event    the two kinds of event a case usually wants -- a pointer event at a point, and a key event
 *               -- built through the *public* doors (`robo_input_event_set_pointer`,
 *               `robo_input_keyboard_data_set`, ...) rather than by filling the struct, so a fixture cannot
 *               construct something the model's own validator would refuse. `input_pointer_event` in
 *               particular is the case's one-liner for "a valid move at this point";
 *
 *   a route     the geometry a conversion needs, for a window at a chosen place with a chosen client inset.
 *               Two doors, because the two pairs of conversions need two different halves: a frame rectangle
 *               for screen ⟷ window, a client rectangle for window ⟷ document;
 *
 *   a face      none. This module has no pixels, no fonts and no surfaces: an input event is a record, and
 *               the whole suite is about records. That absence is worth stating, because the three support
 *               headers beside this one all exist to build a document and this one exists to build nothing.
 *
 * Every helper asserts rather than returning a status, which is the convention the layout, paint, render and
 * desktop suites set: a suite that had to check each step of its own scaffolding would drown its subject in
 * harness.
 */

#ifndef ROBO_INPUT_TEST_SUPPORT_H
#define ROBO_INPUT_TEST_SUPPORT_H

#include "RoboTest.h"

#include "RoboBase/RoboBase.h"
#include "RoboMem/RoboMemDebug.h"

#include "RoboInput/RoboInput.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

/** Bring the base layer up (once) and leave it up. */
static inline void input_fixture(void)
{
    robo_result_t r = robo_base_init();

    if (r != ROBO_OK && r != ROBO_ALREADY_EXISTS) {
        ROBO_CHECK_MSG(0, "robo_base_init failed");
    }
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t input_live_blocks(void)
{
    return robo_mem_debug_live_count();
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/**
 * A route for a window at @p frame_x,@p frame_y on the screen, @p frame_w by @p frame_h, whose client area
 * starts @p inset_x,@p inset_y into the frame and is @p client_w by @p client_h.
 *
 * Both halves are filled in, which is what a *dispatcher* would do; a case that wants to prove a conversion
 * refuses without the geometry it needs clears the half it is testing, which is why the record's two flags
 * are public rather than implied.
 */
static inline robo_input_route_t input_route(int32_t frame_x,
                                             int32_t frame_y,
                                             int32_t frame_w,
                                             int32_t frame_h,
                                             int32_t inset_x,
                                             int32_t inset_y,
                                             int32_t client_w,
                                             int32_t client_h)
{
    robo_input_route_t route;

    robo_input_route_init(&route);
    route.window = 7u;
    route.window_bounds = robo_gfx_rect(frame_x, frame_y, frame_w, frame_h);
    route.has_window_bounds = 1;
    route.client_bounds = robo_gfx_rect(inset_x, inset_y, client_w, client_h);
    route.has_client_bounds = 1;
    return route;
}

/** A route with only the frame rectangle: enough for screen ⟷ window, and not for a document conversion. */
static inline robo_input_route_t input_route_frame_only(int32_t x, int32_t y, int32_t w, int32_t h)
{
    robo_input_route_t route;

    robo_input_route_init(&route);
    route.window = 7u;
    route.window_bounds = robo_gfx_rect(x, y, w, h);
    route.has_window_bounds = 1;
    return route;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/** A pointer payload at @p x,@p y with @p button held in a mask of @p buttons. Asserts on refusal. */
static inline robo_input_pointer_data_t input_pointer_data(int32_t x,
                                                           int32_t y,
                                                           robo_mouse_button_t button,
                                                           uint32_t buttons)
{
    robo_input_pointer_data_t data;

    robo_input_pointer_data_init(&data);
    if (robo_input_pointer_data_set(&data, ROBO_INPUT_POINTER_ID_PRIMARY, robo_gfx_point(x, y), button,
                                    buttons) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's pointer payload was refused");
    }
    return data;
}

/** A key payload. Asserts on refusal. */
static inline robo_input_keyboard_data_t input_key_data(robo_physical_key_t physical,
                                                        robo_logical_key_t logical,
                                                        robo_key_action_t action,
                                                        int repeat,
                                                        const char *text)
{
    robo_input_keyboard_data_t data;

    robo_input_keyboard_data_init(&data);
    if (robo_input_keyboard_data_set(&data, physical, logical, action, repeat, text) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's keyboard payload was refused");
    }
    return data;
}

/**
 * A mouse move at @p x,@p y in @p space, with @p buttons down, in window @p window.
 *
 * The one-liner most cases want, and it goes through the public door so the result is an event the model
 * itself would accept: a case that went wrong in a later assertion changed something *after* this call.
 */
static inline robo_input_event_t input_pointer_move(int32_t x,
                                                    int32_t y,
                                                    uint32_t buttons,
                                                    robo_input_space_t space,
                                                    robo_window_id_t window)
{
    robo_input_event_t event;
    robo_input_pointer_data_t data = input_pointer_data(x, y, ROBO_MOUSE_BUTTON_NONE, buttons);

    robo_input_event_init(&event);
    if (robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_POINTER_MOVE, ROBO_INPUT_SOURCE_MOUSE, &data,
                                     space, NULL) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's pointer event was refused");
    }
    robo_input_event_set_window(&event, window);
    return event;
}

/** A button press or release of @p button at @p x,@p y. @p pressed chooses the type; the mask follows. */
static inline robo_input_event_t input_button_event(int32_t x,
                                                    int32_t y,
                                                    robo_mouse_button_t button,
                                                    int pressed,
                                                    robo_window_id_t window)
{
    robo_input_event_t event;
    robo_input_pointer_data_t data;
    uint32_t mask = 0u;

    if (pressed) {
        mask = robo_input_button_mask_apply(mask, button, 1);
    }
    data = input_pointer_data(x, y, button, mask);
    robo_input_event_init(&event);
    if (robo_input_event_set_pointer(&event,
                                     pressed ? ROBO_INPUT_EVENT_POINTER_DOWN : ROBO_INPUT_EVENT_POINTER_UP,
                                     ROBO_INPUT_SOURCE_MOUSE, &data, ROBO_INPUT_SPACE_SCREEN, NULL) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's button event was refused");
    }
    robo_input_event_set_window(&event, window);
    return event;
}

/** A key event at @p physical, with the given modifier state. */
static inline robo_input_event_t input_key_event(robo_physical_key_t physical,
                                                 robo_logical_key_t logical,
                                                 int pressed,
                                                 robo_input_modifiers_t modifiers)
{
    robo_input_event_t event;
    robo_input_keyboard_data_t data = input_key_data(physical, logical,
                                                     pressed ? ROBO_KEY_ACTION_DOWN : ROBO_KEY_ACTION_UP,
                                                     0, NULL);

    robo_input_event_init(&event);
    if (robo_input_event_set_keyboard(&event,
                                      pressed ? ROBO_INPUT_EVENT_KEY_DOWN : ROBO_INPUT_EVENT_KEY_UP,
                                      ROBO_INPUT_SOURCE_KEYBOARD, &data, NULL) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's key event was refused");
    }
    robo_input_event_set_modifiers(&event, modifiers, NULL);
    robo_input_event_set_window(&event, 3u);
    return event;
}

/** A device identity a case can stamp on an event. Asserts on refusal. */
static inline robo_input_device_t input_device(robo_input_device_id_t id,
                                               robo_input_source_t source,
                                               const char *name)
{
    robo_input_device_t device;

    robo_input_device_init(&device);
    if (robo_input_device_set(&device, id, source, name) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's device was refused");
    }
    return device;
}

/* ------------------------------------------------------------------ */
/* Queues                                                              */
/* ------------------------------------------------------------------ */

/** Create a queue through the public door, with @p capacity (0 = default). Asserts on refusal. */
static inline robo_input_queue_t *input_queue(uint32_t capacity)
{
    robo_input_queue_config_t config;
    robo_input_queue_t *queue = NULL;

    robo_input_queue_config_init(&config);
    config.capacity = capacity;
    if (robo_input_queue_create(&config, &queue, NULL) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's queue was refused");
        return NULL;
    }
    return queue;
}

/** Push through the public door, asserting the push was accepted. */
static inline void input_push(robo_input_queue_t *queue, const robo_input_event_t *event)
{
    if (robo_input_queue_push(queue, event, NULL) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's push was refused");
    }
}

/** Dump a queue into a caller's buffer, asserting the dump fit. Returns the length written. */
static inline size_t input_queue_text(const robo_input_queue_t *queue, char *buffer, size_t capacity)
{
    size_t length = 0u;

    if (robo_input_queue_write(queue, buffer, capacity, &length, NULL) != ROBO_OK) {
        ROBO_CHECK_MSG(0, "the fixture's queue dump did not fit its buffer");
        return 0u;
    }
    return length;
}

#endif /* ROBO_INPUT_TEST_SUPPORT_H */
