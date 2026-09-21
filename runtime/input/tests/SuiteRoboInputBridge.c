/*
 * RoboInput bridge suite (Phase 12.1) — `input.bridge.*`.
 *
 * The bridge is the one thing in this module that turns *today's* events into the model, so its cases are
 * about the three decisions its header states rather than about the plumbing:
 *
 *   **which plane** a bridged pointer event lands in, which is a choice between RoboWindow's two coordinate
 *   pairs and depends on whether the payload's screen coordinates are real;
 *
 *   **whether a previous position exists**, which is knowable only from a non-zero delta;
 *
 *   **what is left empty**, because the window layer has no text and no device: an empty text and an unset
 *   device are *valid* records, and a case asserts both rather than trusting that they are.
 *
 * The last case is the refusal behaviour: a lifecycle window event (a window being closed, focused or
 * resized) is not an input event, and the bridge says so **without writing the caller's output**, so a loop
 * over a window queue cannot mistake a skipped event for a translated one.
 */

#include "RoboInputTestSupport.h"

#include "RoboInput/RoboInputBridge.h"

#include "RoboWindow/RoboWindowMouse.h"

/* ------------------------------------------------------------------ */
/* Which events translate                                              */
/* ------------------------------------------------------------------ */

static void test_bridge_reach(void)
{
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_KEY_DOWN), 1);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_KEY_UP), 1);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_MOUSE_MOVE), 1);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN), 1);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP), 1);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_MOUSE_WHEEL), 1);

    /* Lifecycle events are the window system's business, and there is no input event for them: focus in
     * particular is a later phase's, by the brief's own §22. */
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_CREATED), 0);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_CLOSED), 0);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_FOCUS_GAINED), 0);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_RESIZED), 0);
    ROBO_CHECK_EQ_INT(robo_input_bridge_can_translate(ROBO_WINDOW_EVENT_NONE), 0);

    /* The options: defaults first, then a caller's own. */
    {
        robo_input_bridge_options_t options;

        robo_input_bridge_options_init(&options);
        ROBO_CHECK_EQ_INT(options.mouse_source, ROBO_INPUT_SOURCE_MOUSE);
        ROBO_CHECK_EQ_INT(options.keyboard_source, ROBO_INPUT_SOURCE_KEYBOARD);
        ROBO_CHECK_EQ_INT(options.prefer_screen_coords, 1);
        ROBO_CHECK_EQ_INT(options.mouse_device.id, ROBO_INPUT_DEVICE_ID_NONE);
        ROBO_CHECK_EQ_INT(options.keyboard_device.id, ROBO_INPUT_DEVICE_ID_NONE);
        robo_input_bridge_options_init(NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Keys                                                                */
/* ------------------------------------------------------------------ */

static void test_bridge_keyboard(void)
{
    robo_window_event_t window_event;
    robo_input_event_t event;
    robo_input_diag_t diag;

    robo_window_event_init(&window_event);
    window_event.type = ROBO_WINDOW_EVENT_KEY_DOWN;
    window_event.window_id = 3u;
    window_event.key.action = ROBO_KEY_ACTION_DOWN;
    window_event.key.physical_key = ROBO_PHYSICAL_KEY_A;
    window_event.key.logical_key = ROBO_LOGICAL_KEY_A;
    window_event.key.modifiers = ROBO_KEY_MOD_SHIFT;
    window_event.key.timestamp_ms = 512u;
    window_event.key.window_id = 3u;

    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_is_valid(&event));
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_KEY_DOWN);
    ROBO_CHECK_EQ_INT(event.source, ROBO_INPUT_SOURCE_KEYBOARD);
    ROBO_CHECK_EQ_INT(event.window, 3u);
    ROBO_CHECK_EQ_INT(event.timestamp_ms, 512u);
    ROBO_CHECK_EQ_INT(event.modifiers, ROBO_KEY_MOD_SHIFT);
    /* A keystroke has no position, so it names no plane -- rule 6 of the event model, visible here. */
    ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_UNKNOWN);
    ROBO_CHECK_EQ_INT(event.payload.keyboard.physical, ROBO_PHYSICAL_KEY_A);
    ROBO_CHECK_EQ_INT(event.payload.keyboard.logical, ROBO_LOGICAL_KEY_A);
    ROBO_CHECK_EQ_INT(event.payload.keyboard.action, ROBO_KEY_ACTION_DOWN);
    /* Nothing told the bridge which device this came from, so the record is empty -- and empty is valid. */
    ROBO_CHECK_EQ_INT(event.device.id, ROBO_INPUT_DEVICE_ID_NONE);
    ROBO_CHECK_STR_EQ(event.payload.keyboard.text, "");

    /* A key up, with the window event's own id unset so the payload's id is what identifies the window. */
    window_event.type = ROBO_WINDOW_EVENT_KEY_UP;
    window_event.window_id = ROBO_WINDOW_ID_NONE;
    window_event.key.action = ROBO_KEY_ACTION_UP;
    window_event.key.is_repeat = 0;
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_KEY_UP);
    ROBO_CHECK_EQ_INT(event.window, 3u);
    ROBO_CHECK_EQ_INT(event.payload.keyboard.action, ROBO_KEY_ACTION_UP);

    /* A payload whose action disagrees with the window event's type is refused rather than guessed: the
     * bridge will not decide which of the two the producer meant. */
    window_event.key.action = ROBO_KEY_ACTION_DOWN;
    diag = (robo_input_diag_t){ 0 };
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, &diag), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_BRIDGE);
    ROBO_CHECK(diag.message != NULL);

    /* ... and a payload with no action at all is refused by the payload translator directly. */
    {
        robo_keyboard_event_t bare;
        robo_input_keyboard_data_t data;

        robo_keyboard_event_init(&bare);
        bare.physical_key = ROBO_PHYSICAL_KEY_A;
        bare.logical_key = ROBO_LOGICAL_KEY_A;
        ROBO_CHECK_EQ_INT(robo_input_keyboard_data_from_window_keyboard(&bare, &data, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_BRIDGE);
        ROBO_CHECK_EQ_INT(robo_input_keyboard_data_from_window_keyboard(NULL, &data, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_keyboard_data_from_window_keyboard(&bare, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);

        /* The translator itself: identities and the repeat copied, the text deliberately empty. */
        bare.action = ROBO_KEY_ACTION_DOWN;
        bare.is_repeat = 3;
        ROBO_CHECK_EQ_INT(robo_input_keyboard_data_from_window_keyboard(&bare, &data, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(data.repeat, 1);
        ROBO_CHECK_EQ_INT(data.scan_code, 0u);
        ROBO_CHECK_STR_EQ(data.text, "");
        ROBO_CHECK(robo_input_keyboard_data_is_valid(&data));
    }

    /* A caller's own source and device are stamped on the event, which is how a real backend will report a
     * device identity through this door before an adapter exists. */
    {
        robo_input_bridge_options_t options;

        robo_input_bridge_options_init(&options);
        options.keyboard_source = ROBO_INPUT_SOURCE_REMOTE;
        options.keyboard_device = input_device(9u, ROBO_INPUT_SOURCE_REMOTE, "Remote keyboard");
        window_event.type = ROBO_WINDOW_EVENT_KEY_DOWN;
        window_event.key.action = ROBO_KEY_ACTION_DOWN;
        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_from_window_event_with_options(&options, &window_event, &event,
                                                                        NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(event.source, ROBO_INPUT_SOURCE_REMOTE);
        ROBO_CHECK_EQ_INT(event.device.id, 9u);
        ROBO_CHECK_STR_EQ(event.device.name, "Remote keyboard");
        ROBO_CHECK(robo_input_event_is_valid(&event));

        /* A device whose source disagrees with the event's is refused rather than stamped. */
        options.keyboard_device = input_device(9u, ROBO_INPUT_SOURCE_MOUSE, "Mouse");
        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_from_window_event_with_options(&options, &window_event, &event,
                                                                        &diag),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_EVENT);

        /* A modifier state with bits the runtime does not define is refused: the record would be invalid
         * everywhere above, and the bridge is where it can still be named. */
        robo_input_bridge_options_init(&options);
        window_event.key.modifiers = (robo_key_modifier_flags_t)(1u << 25);
        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_from_window_event_with_options(&options, &window_event, &event,
                                                                        &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_BRIDGE);
    }

    /* NULL and an unknown type. */
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(NULL, &event, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, NULL, NULL), ROBO_INVALID_ARGUMENT);
    window_event.type = ROBO_WINDOW_EVENT_NONE;
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL),
                      ROBO_NOT_SUPPORTED);
}

/* ------------------------------------------------------------------ */
/* Pointers                                                            */
/* ------------------------------------------------------------------ */

static void test_bridge_pointer(void)
{
    robo_window_event_t window_event;
    robo_input_event_t event;
    robo_input_diag_t diag;

    robo_window_event_init(&window_event);
    window_event.type = ROBO_WINDOW_EVENT_MOUSE_MOVE;
    window_event.window_id = 3u;
    window_event.mouse.type = ROBO_MOUSE_EVENT_MOVE;
    window_event.mouse.client_x = 120;
    window_event.mouse.client_y = 84;
    window_event.mouse.screen_x = 130;
    window_event.mouse.screen_y = 104;
    window_event.mouse.has_screen_coords = 1;
    window_event.mouse.window_id = 3u;
    window_event.mouse.modifiers = ROBO_KEY_MOD_CTRL;
    window_event.mouse.timestamp_ms = 1841u;

    /* With real screen coordinates the event is in **screen** space, and the position is the screen one. */
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_is_valid(&event));
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_POINTER_MOVE);
    ROBO_CHECK_EQ_INT(event.source, ROBO_INPUT_SOURCE_MOUSE);
    ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_SCREEN);
    ROBO_CHECK_EQ_INT(event.payload.pointer.position.x, 130);
    ROBO_CHECK_EQ_INT(event.payload.pointer.position.y, 104);
    ROBO_CHECK_EQ_INT(event.modifiers, ROBO_KEY_MOD_CTRL);
    ROBO_CHECK_EQ_INT(event.payload.pointer.pointer, ROBO_INPUT_POINTER_ID_PRIMARY);
    /* A zero delta cannot be told apart from a first sample, so no previous position is claimed -- and the
     * dump says `previous=none` rather than inventing a point. */
    ROBO_CHECK_EQ_INT(event.payload.pointer.has_previous, 0);
    ROBO_CHECK_EQ_INT(event.payload.pointer.delta_x, 0);

    /* Without them the event is in **document** space, and the position is the client one; a caller that
     * needs screen space converts with geometry it supplies. */
    window_event.mouse.has_screen_coords = 0;
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_DOCUMENT);
    ROBO_CHECK_EQ_INT(event.payload.pointer.position.x, 120);
    ROBO_CHECK_EQ_INT(event.payload.pointer.position.y, 84);
    ROBO_CHECK_EQ_INT(robo_input_bridge_space_for_window_mouse(&window_event.mouse, 1),
                      ROBO_INPUT_SPACE_DOCUMENT);

    /* A caller may refuse the screen pair even when it is there. */
    {
        robo_input_bridge_options_t options;

        robo_input_bridge_options_init(&options);
        options.prefer_screen_coords = 0;
        window_event.mouse.has_screen_coords = 1;
        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_from_window_event_with_options(&options, &window_event, &event,
                                                                        NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_DOCUMENT);
        ROBO_CHECK_EQ_INT(event.payload.pointer.position.x, 120);
        ROBO_CHECK_EQ_INT(robo_input_bridge_space_for_window_mouse(&window_event.mouse, 0),
                          ROBO_INPUT_SPACE_DOCUMENT);
        ROBO_CHECK_EQ_INT(robo_input_bridge_space_for_window_mouse(NULL, 1), ROBO_INPUT_SPACE_UNKNOWN);
    }

    /* A non-zero delta *is* proof of a previous position, and the previous point is the delta back along
     * the movement -- in the plane the event landed in, which is why the client pair is the one asked for
     * here. */
    window_event.mouse.has_screen_coords = 0;
    window_event.mouse.delta_x = 2;
    window_event.mouse.delta_y = -4;
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(event.payload.pointer.has_previous, 1);
    ROBO_CHECK_EQ_INT(event.payload.pointer.previous.x, 118);
    ROBO_CHECK_EQ_INT(event.payload.pointer.previous.y, 88);
    ROBO_CHECK_EQ_INT(event.payload.pointer.delta_x, 2);
    ROBO_CHECK_EQ_INT(event.payload.pointer.delta_y, -4);
    window_event.mouse.delta_x = 0;
    window_event.mouse.delta_y = 0;

    /* A button event carries the button, and its mask follows the runtime's tracked state plus the event
     * itself -- so a press has its bit and a release does not. */
    window_event.type = ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN;
    window_event.mouse.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
    window_event.mouse.button = ROBO_MOUSE_BUTTON_RIGHT;
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_POINTER_DOWN);
    ROBO_CHECK_EQ_INT(event.payload.pointer.button, ROBO_MOUSE_BUTTON_RIGHT);
    ROBO_CHECK(robo_input_button_mask_has(event.payload.pointer.buttons, ROBO_MOUSE_BUTTON_RIGHT));
    ROBO_CHECK_EQ_INT(event.payload.pointer.click_count, 0);

    window_event.type = ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP;
    window_event.mouse.type = ROBO_MOUSE_EVENT_BUTTON_UP;
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_POINTER_UP);
    ROBO_CHECK(!robo_input_button_mask_has(event.payload.pointer.buttons, ROBO_MOUSE_BUTTON_RIGHT));

    /* A button event with no button this build can name is refused: "something was pressed, unknown what"
     * is not a record the model may carry. */
    window_event.mouse.button = ROBO_MOUSE_BUTTON_UNKNOWN;
    diag = (robo_input_diag_t){ 0 };
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, &diag), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_BRIDGE);
    window_event.mouse.button = ROBO_MOUSE_BUTTON_NONE;
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, &diag), ROBO_NOT_SUPPORTED);

    /* The wheel: deltas and their unit, and a unit this build does not define refused. */
    window_event.type = ROBO_WINDOW_EVENT_MOUSE_WHEEL;
    window_event.mouse.type = ROBO_MOUSE_EVENT_WHEEL;
    window_event.mouse.scroll_x = 0;
    window_event.mouse.scroll_y = 120;
    window_event.mouse.scroll_unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_WHEEL);
    ROBO_CHECK_EQ_INT(event.payload.pointer.wheel.delta_y, 120);
    ROBO_CHECK_EQ_INT(event.payload.pointer.wheel.unit, ROBO_MOUSE_SCROLL_UNIT_LINE);
    ROBO_CHECK_EQ_INT(event.payload.pointer.button, ROBO_MOUSE_BUTTON_NONE);

    window_event.mouse.scroll_unit = (robo_mouse_scroll_unit_t)7;
    diag = (robo_input_diag_t){ 0 };
    ROBO_CHECK_EQ_INT(robo_input_event_from_window_event(&window_event, &event, &diag), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_BRIDGE);

    /* A mouse payload with no type at all, and the translator's own argument checks. */
    {
        robo_mouse_event_t bare;
        robo_input_pointer_data_t data;
        robo_input_space_t space = ROBO_INPUT_SPACE_UNKNOWN;

        robo_mouse_event_init(&bare);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_from_window_mouse(&bare, 1, &data, &space, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_BRIDGE);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_from_window_mouse(NULL, 1, &data, &space, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_from_window_mouse(&bare, 1, NULL, &space, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_from_window_mouse(&bare, 1, &data, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
}

/* ------------------------------------------------------------------ */
/* Into a queue, and leaving lifecycle events alone                    */
/* ------------------------------------------------------------------ */

static void test_bridge_queue(void)
{
    robo_input_queue_t *queue = input_queue(8u);
    robo_window_event_t window_event;
    robo_input_event_t out;
    int translated = -1;
    robo_input_diag_t diag;

    robo_window_event_init(&window_event);
    window_event.type = ROBO_WINDOW_EVENT_MOUSE_MOVE;
    window_event.window_id = 3u;
    window_event.mouse.type = ROBO_MOUSE_EVENT_MOVE;
    window_event.mouse.client_x = 5;
    window_event.mouse.client_y = 6;
    window_event.mouse.window_id = 3u;

    ROBO_CHECK_EQ_INT(robo_input_queue_push_window_event(queue, NULL, &window_event, &translated, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(translated, 1);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 1u);
    ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.type, ROBO_INPUT_EVENT_POINTER_MOVE);
    ROBO_CHECK_EQ_INT(out.payload.pointer.position.x, 5);

    /* A lifecycle event translates to nothing, pushes nothing, and says so through the flag a caller
     * looping over a window queue actually reads. */
    window_event.type = ROBO_WINDOW_EVENT_CLOSED;
    translated = -1;
    diag = (robo_input_diag_t){ 0 };
    ROBO_CHECK_EQ_INT(robo_input_queue_push_window_event(queue, NULL, &window_event, &translated, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(translated, 0);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_BRIDGE);
    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 0u);

    /* A full queue's refusal is the *queue's*, and the caller can tell the two apart by the domain. */
    {
        robo_input_queue_t *small = input_queue(1u);
        robo_input_event_t event = input_pointer_move(1, 1, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);

        input_push(small, &event);
        window_event.type = ROBO_WINDOW_EVENT_MOUSE_MOVE;
        translated = -1;
        diag = (robo_input_diag_t){ 0 };
        ROBO_CHECK_EQ_INT(robo_input_queue_push_window_event(small, NULL, &window_event, &translated,
                                                            &diag),
                          ROBO_BUSY);
        ROBO_CHECK_EQ_INT(translated, 0);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_QUEUE);
        robo_input_queue_destroy(small);
    }

    /* The whole traversal a consumer writes: drain a window queue through the bridge, and see exactly the
     * input events -- in order -- with the lifecycle traffic filtered out. */
    {
        robo_input_queue_t *sink = input_queue(8u);
        robo_window_event_t sequence[4];
        int index;
        int pushed = 0;

        robo_window_event_init(&sequence[0]);
        sequence[0].type = ROBO_WINDOW_EVENT_FOCUS_GAINED;
        sequence[0].window_id = 3u;
        robo_window_event_init(&sequence[1]);
        sequence[1].type = ROBO_WINDOW_EVENT_KEY_DOWN;
        sequence[1].window_id = 3u;
        sequence[1].key.action = ROBO_KEY_ACTION_DOWN;
        sequence[1].key.physical_key = ROBO_PHYSICAL_KEY_A;
        sequence[1].key.logical_key = ROBO_LOGICAL_KEY_A;
        sequence[1].key.window_id = 3u;
        robo_window_event_init(&sequence[2]);
        sequence[2].type = ROBO_WINDOW_EVENT_RESIZED;
        sequence[2].window_id = 3u;
        robo_window_event_init(&sequence[3]);
        sequence[3].type = ROBO_WINDOW_EVENT_KEY_UP;
        sequence[3].window_id = 3u;
        sequence[3].key.action = ROBO_KEY_ACTION_UP;
        sequence[3].key.physical_key = ROBO_PHYSICAL_KEY_A;
        sequence[3].key.logical_key = ROBO_LOGICAL_KEY_A;
        sequence[3].key.window_id = 3u;

        for (index = 0; index < 4; index++) {
            int was_translated = 0;

            if (robo_input_queue_push_window_event(sink, NULL, &sequence[index], &was_translated, NULL) ==
                ROBO_OK) {
                pushed++;
                ROBO_CHECK_EQ_INT(was_translated, 1);
            }
        }
        ROBO_CHECK_EQ_INT(pushed, 2);
        ROBO_CHECK_EQ_INT(robo_input_queue_count(sink), 2u);

        ROBO_CHECK_EQ_INT(robo_input_queue_pop(sink, &out, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(out.type, ROBO_INPUT_EVENT_KEY_DOWN);
        ROBO_CHECK_EQ_INT(out.payload.keyboard.action, ROBO_KEY_ACTION_DOWN);
        ROBO_CHECK_EQ_INT(robo_input_queue_pop(sink, &out, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(out.type, ROBO_INPUT_EVENT_KEY_UP);
        ROBO_CHECK_EQ_INT(robo_input_queue_peek(sink, &out, NULL), ROBO_NOT_FOUND);

        robo_input_queue_destroy(sink);
    }

    window_event.type = ROBO_WINDOW_EVENT_MOUSE_MOVE;
    window_event.mouse.type = ROBO_MOUSE_EVENT_MOVE;
    ROBO_CHECK_EQ_INT(robo_input_queue_push_window_event(NULL, NULL, &window_event, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_queue_push_window_event(queue, NULL, NULL, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_input_queue_destroy(queue);
}

void robo_test_register_input_bridge(void)
{
    robo_test_register("input.bridge.reach", test_bridge_reach);
    robo_test_register("input.bridge.keyboard", test_bridge_keyboard);
    robo_test_register("input.bridge.pointer", test_bridge_pointer);
    robo_test_register("input.bridge.queue", test_bridge_queue);
}
