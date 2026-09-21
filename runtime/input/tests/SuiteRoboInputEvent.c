/*
 * RoboInput event-model suite (Phase 12.1) — `input.event.*`.
 *
 * Four cases, and the third is the one that matters: `validation` walks the seven rules of
 * `robo_input_event_is_valid` and proves each one *refuses*, by building a good event through the public
 * doors and then breaking exactly one thing about it. A validator whose refusals are untested is a validator
 * that quietly accepts, and what it accepts is the record a consumer trusts.
 *
 * The rules are tested against hand-edited records rather than through the setters, deliberately: the setters
 * are the *politeness* layer (they refuse before an event is malformed), and the validator is the *trust*
 * layer, which is what protects a caller that received an event from somewhere else. Both exist, and this
 * case is about the second.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void test_event_creation(void)
{
    robo_input_event_t event;

    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_NONE);
    ROBO_CHECK_EQ_INT(event.source, ROBO_INPUT_SOURCE_UNKNOWN);
    ROBO_CHECK_EQ_INT(event.device.id, ROBO_INPUT_DEVICE_ID_NONE);
    ROBO_CHECK_EQ_INT(event.device.source, ROBO_INPUT_SOURCE_UNKNOWN);
    ROBO_CHECK_EQ_INT(event.timestamp_ms, 0u);
    ROBO_CHECK_EQ_INT(event.modifiers, ROBO_INPUT_MODIFIERS_NONE);
    ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_UNKNOWN);
    ROBO_CHECK_EQ_INT(event.window, ROBO_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT(event.display, ROBO_DISPLAY_ID_NONE);
    ROBO_CHECK_EQ_INT(event.has_target, 0);
    ROBO_CHECK_EQ_INT(event.target.kind, ROBO_INPUT_TARGET_NONE);
    ROBO_CHECK_EQ_INT(event.payload.pointer.pointer, ROBO_INPUT_POINTER_ID_PRIMARY);

    /* A cleared event is *not* a deliverable event: `NONE` is the honest cleared value and is refused, which
     * is what stops an uninitialized record from being queued as if it were something. */
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    ROBO_CHECK(!robo_input_event_is_valid(NULL));

    /* Init is safe on NULL and never writes to a record it was not given. */
    robo_input_event_init(NULL);
    robo_input_event_clear_target(NULL);
}

static void test_event_type_queries(void)
{
    /* The table, asserted rather than inferred: one row per type, and the four facts about each. */
    struct {
        robo_input_event_type_t type;
        robo_input_payload_kind_t payload;
        int position;
        int release;
        int key;
        int touch;
    } rows[] = {
        { ROBO_INPUT_EVENT_NONE, ROBO_INPUT_PAYLOAD_NONE, 0, 0, 0, 0 },
        { ROBO_INPUT_EVENT_KEY_DOWN, ROBO_INPUT_PAYLOAD_KEYBOARD, 0, 0, 1, 0 },
        { ROBO_INPUT_EVENT_KEY_UP, ROBO_INPUT_PAYLOAD_KEYBOARD, 0, 0, 1, 0 },
        { ROBO_INPUT_EVENT_POINTER_MOVE, ROBO_INPUT_PAYLOAD_POINTER, 1, 0, 0, 0 },
        { ROBO_INPUT_EVENT_POINTER_DOWN, ROBO_INPUT_PAYLOAD_POINTER, 1, 0, 0, 0 },
        { ROBO_INPUT_EVENT_POINTER_UP, ROBO_INPUT_PAYLOAD_POINTER, 1, 1, 0, 0 },
        { ROBO_INPUT_EVENT_POINTER_CANCEL, ROBO_INPUT_PAYLOAD_POINTER, 1, 1, 0, 0 },
        { ROBO_INPUT_EVENT_WHEEL, ROBO_INPUT_PAYLOAD_POINTER, 1, 0, 0, 0 },
        { ROBO_INPUT_EVENT_TEXT_INPUT, ROBO_INPUT_PAYLOAD_TEXT, 0, 0, 0, 0 },
        { ROBO_INPUT_EVENT_TOUCH_BEGIN, ROBO_INPUT_PAYLOAD_POINTER, 1, 0, 0, 1 },
        { ROBO_INPUT_EVENT_TOUCH_MOVE, ROBO_INPUT_PAYLOAD_POINTER, 1, 0, 0, 1 },
        { ROBO_INPUT_EVENT_TOUCH_END, ROBO_INPUT_PAYLOAD_POINTER, 1, 1, 0, 1 },
        { ROBO_INPUT_EVENT_TOUCH_CANCEL, ROBO_INPUT_PAYLOAD_POINTER, 1, 1, 0, 1 }
    };
    size_t index;

    for (index = 0u; index < (sizeof(rows) / sizeof(rows[0])); index++) {
        ROBO_CHECK_EQ_INT(robo_input_event_payload_kind(rows[index].type), rows[index].payload);
        ROBO_CHECK_EQ_INT(robo_input_event_type_has_position(rows[index].type), rows[index].position);
        ROBO_CHECK_EQ_INT(robo_input_event_type_is_release(rows[index].type), rows[index].release);
        ROBO_CHECK_EQ_INT(robo_input_event_type_is_key(rows[index].type), rows[index].key);
        ROBO_CHECK_EQ_INT(robo_input_event_type_is_touch(rows[index].type), rows[index].touch);
    }

    /* A pointer *button* type excludes the touch phases and the wheel: the predicate is about a pointer with
     * buttons, which is what a dispatcher asking "is this a click-ish event?" wants. */
    ROBO_CHECK(robo_input_event_type_is_pointer_button(ROBO_INPUT_EVENT_POINTER_DOWN));
    ROBO_CHECK(robo_input_event_type_is_pointer_button(ROBO_INPUT_EVENT_POINTER_CANCEL));
    ROBO_CHECK(!robo_input_event_type_is_pointer_button(ROBO_INPUT_EVENT_WHEEL));
    ROBO_CHECK(!robo_input_event_type_is_pointer_button(ROBO_INPUT_EVENT_TOUCH_BEGIN));

    /* Which sources may report which types: the table that stops a touch event from a mouse. */
    ROBO_CHECK(robo_input_event_type_allows_source(ROBO_INPUT_EVENT_KEY_DOWN, ROBO_INPUT_SOURCE_KEYBOARD));
    ROBO_CHECK(robo_input_event_type_allows_source(ROBO_INPUT_EVENT_KEY_DOWN, ROBO_INPUT_SOURCE_VIRTUAL));
    ROBO_CHECK(!robo_input_event_type_allows_source(ROBO_INPUT_EVENT_KEY_DOWN, ROBO_INPUT_SOURCE_MOUSE));
    ROBO_CHECK(!robo_input_event_type_allows_source(ROBO_INPUT_EVENT_KEY_DOWN, ROBO_INPUT_SOURCE_UNKNOWN));
    ROBO_CHECK(robo_input_event_type_allows_source(ROBO_INPUT_EVENT_POINTER_MOVE, ROBO_INPUT_SOURCE_TOUCH));
    ROBO_CHECK(robo_input_event_type_allows_source(ROBO_INPUT_EVENT_WHEEL, ROBO_INPUT_SOURCE_TABLET));
    ROBO_CHECK(robo_input_event_type_allows_source(ROBO_INPUT_EVENT_TOUCH_BEGIN, ROBO_INPUT_SOURCE_TOUCH));
    ROBO_CHECK(robo_input_event_type_allows_source(ROBO_INPUT_EVENT_TOUCH_BEGIN, ROBO_INPUT_SOURCE_TABLET));
    ROBO_CHECK(!robo_input_event_type_allows_source(ROBO_INPUT_EVENT_TOUCH_BEGIN, ROBO_INPUT_SOURCE_MOUSE));
    ROBO_CHECK(!robo_input_event_type_allows_source(ROBO_INPUT_EVENT_TOUCH_BEGIN, ROBO_INPUT_SOURCE_KEYBOARD));
    ROBO_CHECK(!robo_input_event_type_allows_source(ROBO_INPUT_EVENT_NONE, ROBO_INPUT_SOURCE_MOUSE));

    /* The window-layer vocabulary mapping the bridge uses, exposed as a query. */
    ROBO_CHECK_EQ_INT(robo_input_event_type_for_window_event(ROBO_WINDOW_EVENT_KEY_DOWN),
                      ROBO_INPUT_EVENT_KEY_DOWN);
    ROBO_CHECK_EQ_INT(robo_input_event_type_for_window_event(ROBO_WINDOW_EVENT_MOUSE_WHEEL),
                      ROBO_INPUT_EVENT_WHEEL);
    ROBO_CHECK_EQ_INT(robo_input_event_type_for_window_event(ROBO_WINDOW_EVENT_CLOSED),
                      ROBO_INPUT_EVENT_NONE);
    ROBO_CHECK_EQ_INT(robo_input_source_for_window_event(ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN),
                      ROBO_INPUT_SOURCE_MOUSE);
    ROBO_CHECK_EQ_INT(robo_input_source_for_window_event(ROBO_WINDOW_EVENT_KEY_UP),
                      ROBO_INPUT_SOURCE_KEYBOARD);
    ROBO_CHECK_EQ_INT(robo_input_source_for_window_event(ROBO_WINDOW_EVENT_RESIZED),
                      ROBO_INPUT_SOURCE_UNKNOWN);
}

/* ------------------------------------------------------------------ */
/* Construction through the doors                                      */
/* ------------------------------------------------------------------ */

static void test_event_construction(void)
{
    robo_input_diag_t diag;
    robo_input_event_t event;
    robo_input_pointer_data_t pointer = input_pointer_data(10, 20, ROBO_MOUSE_BUTTON_NONE, 0u);
    robo_input_keyboard_data_t key = input_key_data(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                    ROBO_KEY_ACTION_DOWN, 0, "a");
    robo_input_device_t device;

    /* A key event: the payload, the type it implies, and a valid event at the end. */
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_KEY_DOWN,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, &key, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_input_event_is_valid(&event));
    ROBO_CHECK_EQ_INT(event.payload.keyboard.logical, ROBO_LOGICAL_KEY_A);

    /* A key *up* with a down payload is refused by the door, and the event keeps what it had. */
    diag = (robo_input_diag_t){ 0 };
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_KEY_UP,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, &key, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_EVENT);
    ROBO_CHECK(diag.message != NULL);
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_KEY_DOWN);

    /* A pointer event needs a plane, and the door says so in the *space* domain rather than the event's. */
    robo_input_event_init(&event);
    diag = (robo_input_diag_t){ 0 };
    ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_POINTER_MOVE,
                                                  ROBO_INPUT_SOURCE_MOUSE, &pointer,
                                                  ROBO_INPUT_SPACE_UNKNOWN, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
    ROBO_CHECK_EQ_INT(event.type, ROBO_INPUT_EVENT_NONE);

    ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_POINTER_MOVE,
                                                  ROBO_INPUT_SOURCE_MOUSE, &pointer,
                                                  ROBO_INPUT_SPACE_SCREEN, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_input_event_is_valid(&event));
    ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_SCREEN);
    ROBO_CHECK_EQ_INT(event.payload.pointer.position.x, 10);

    /* The wrong payload for the type, in both directions. */
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_POINTER_MOVE,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, &key, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_KEY_DOWN,
                                                  ROBO_INPUT_SOURCE_MOUSE, &pointer,
                                                  ROBO_INPUT_SPACE_SCREEN, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Text: a door that refuses an empty string and one that refuses text longer than one event may carry. */
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_text(&event, ROBO_INPUT_SOURCE_KEYBOARD, "", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_text(&event, ROBO_INPUT_SOURCE_KEYBOARD, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_text(&event, ROBO_INPUT_SOURCE_KEYBOARD,
                                               "0123456789012345678901234567890123", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_text(&event, ROBO_INPUT_SOURCE_KEYBOARD, "A", NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_is_valid(&event));
    ROBO_CHECK_STR_EQ(event.payload.text.text, "A");

    /* Common fields: a device that agrees with the source is accepted, one that disagrees is not. */
    device = input_device(1u, ROBO_INPUT_SOURCE_MOUSE, "Mouse");
    ROBO_CHECK_EQ_INT(robo_input_event_set_device(&event, &device, NULL), ROBO_INVALID_ARGUMENT);
    device = input_device(1u, ROBO_INPUT_SOURCE_KEYBOARD, "Keyboard");
    ROBO_CHECK_EQ_INT(robo_input_event_set_device(&event, &device, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(event.device.id, 1u);
    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* Modifiers, timestamps, window and display accept what they are given except for undefined bits. */
    ROBO_CHECK_EQ_INT(robo_input_event_set_modifiers(&event, ROBO_KEY_MOD_SHIFT, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_modifiers(&event, (robo_input_modifiers_t)(1u << 20), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(event.modifiers, ROBO_KEY_MOD_SHIFT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_timestamp(&event, 1841u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_window(&event, 3u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_display(&event, 1u), ROBO_OK);
    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* Every door tolerates NULL by refusing. */
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(NULL, ROBO_INPUT_EVENT_KEY_DOWN,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, &key, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_KEY_DOWN,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(NULL, ROBO_INPUT_EVENT_POINTER_MOVE,
                                                  ROBO_INPUT_SOURCE_MOUSE, &pointer,
                                                  ROBO_INPUT_SPACE_SCREEN, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_text(NULL, ROBO_INPUT_SOURCE_KEYBOARD, "A", NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_timestamp(NULL, 1u), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_window(NULL, 1u), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_display(NULL, 1u), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_device(NULL, &device, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_set_target(NULL, NULL, NULL), ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Validation: the seven rules, each refusing                                 */
/* ------------------------------------------------------------------ */

static void test_event_validation(void)
{
    robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    robo_input_diag_t diag;

    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* 1. a deliverable type. */
    event.type = ROBO_INPUT_EVENT_NONE;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.type = (robo_input_event_type_t)99;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.type = ROBO_INPUT_EVENT_POINTER_MOVE;

    /* 2. a source that may report it: a pointer move from a keyboard is refused. */
    event.source = ROBO_INPUT_SOURCE_KEYBOARD;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.source = ROBO_INPUT_SOURCE_UNKNOWN;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.source = ROBO_INPUT_SOURCE_MOUSE;
    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* 3. a device record, agreeing with the source when it names one. */
    event.device.flags = (uint32_t)(1u << 20);
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.device.flags = 0u;
    event.device.source = ROBO_INPUT_SOURCE_TOUCH;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.device = input_device(4u, ROBO_INPUT_SOURCE_MOUSE, "Mouse");
    ROBO_CHECK(robo_input_event_is_valid(&event));
    event.device = input_device(0u, ROBO_INPUT_SOURCE_UNKNOWN, "");
    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* 4. defined modifier bits. */
    event.modifiers = (robo_input_modifiers_t)(1u << 20);
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.modifiers = ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_CTRL;
    ROBO_CHECK(robo_input_event_is_valid(&event));
    event.modifiers = ROBO_INPUT_MODIFIERS_NONE;

    /* 5. payload coherence, one rule at a time. */
    event.payload.pointer.flags |= ROBO_INPUT_POINTER_FLAG_CANCELLED;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.flags &= ~ROBO_INPUT_POINTER_FLAG_CANCELLED;
    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* ... a move that names a button. */
    event.payload.pointer.button = ROBO_MOUSE_BUTTON_LEFT;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.button = ROBO_MOUSE_BUTTON_NONE;

    /* ... a press whose own button is not in its mask. */
    event.type = ROBO_INPUT_EVENT_POINTER_DOWN;
    event.payload.pointer.button = ROBO_MOUSE_BUTTON_LEFT;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.buttons = robo_input_button_mask_apply(0u, ROBO_MOUSE_BUTTON_LEFT, 1);
    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* ... a release whose own button is still down. */
    event.type = ROBO_INPUT_EVENT_POINTER_UP;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.buttons = 0u;
    ROBO_CHECK(robo_input_event_is_valid(&event));

    /* ... a wheel that names a button, and one whose unit this build does not define. */
    event.type = ROBO_INPUT_EVENT_WHEEL;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.button = ROBO_MOUSE_BUTTON_NONE;
    ROBO_CHECK(robo_input_event_is_valid(&event));
    event.payload.pointer.wheel.unit = (robo_mouse_scroll_unit_t)7;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.wheel.unit = ROBO_MOUSE_SCROLL_UNIT_LINE;

    /* ... a cancel that does not say it is one, and a non-cancel that does. */
    event.type = ROBO_INPUT_EVENT_POINTER_CANCEL;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.flags |= ROBO_INPUT_POINTER_FLAG_CANCELLED;
    ROBO_CHECK(robo_input_event_is_valid(&event));
    event.type = ROBO_INPUT_EVENT_POINTER_MOVE;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.flags &= ~ROBO_INPUT_POINTER_FLAG_CANCELLED;

    /* ... a touch contact holding a button, and a touch event from a mouse. */
    event.type = ROBO_INPUT_EVENT_TOUCH_BEGIN;
    event.source = ROBO_INPUT_SOURCE_TOUCH;
    ROBO_CHECK(robo_input_event_is_valid(&event));
    event.payload.pointer.button = ROBO_MOUSE_BUTTON_LEFT;
    event.payload.pointer.buttons = robo_input_button_mask_apply(0u, ROBO_MOUSE_BUTTON_LEFT, 1);
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.pointer.button = ROBO_MOUSE_BUTTON_NONE;
    event.payload.pointer.buttons = 0u;
    event.source = ROBO_INPUT_SOURCE_MOUSE;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.source = ROBO_INPUT_SOURCE_MOUSE;
    event.type = ROBO_INPUT_EVENT_POINTER_MOVE;

    /* ... a key event whose payload's action disagrees with its type, and a wordless text event. */
    event = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1, ROBO_INPUT_MODIFIERS_NONE);
    ROBO_CHECK(robo_input_event_is_valid(&event));
    event.payload.keyboard.action = ROBO_KEY_ACTION_UP;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.payload.keyboard.action = ROBO_KEY_ACTION_DOWN;
    ROBO_CHECK(robo_input_event_is_valid(&event));
    {
        /* A text event built through its own door, then emptied: the *type* may not be wordless, even
         * though the payload may (a key that produced no text is ordinary). Hand-editing a key event's type
         * would not test this: the union would hand the text payload the keyboard record's own bytes. */
        robo_input_event_t text_event;

        robo_input_event_init(&text_event);
        ROBO_CHECK_EQ_INT(robo_input_event_set_text(&text_event, ROBO_INPUT_SOURCE_KEYBOARD, "A", NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_input_event_is_valid(&text_event));
        text_event.payload.text.text[0] = '\0';
        ROBO_CHECK(!robo_input_event_is_valid(&text_event));
    }

    /* 6. a plane exactly when there is a position to be on it, both ways. */
    event = input_pointer_move(4, 5, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    event.space = ROBO_INPUT_SPACE_UNKNOWN;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event.space = (robo_input_space_t)9;
    ROBO_CHECK(!robo_input_event_is_valid(&event));
    event = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1, ROBO_INPUT_MODIFIERS_NONE);
    event.space = ROBO_INPUT_SPACE_SCREEN;
    ROBO_CHECK(!robo_input_event_is_valid(&event));

    /* 7. a target that agrees with the event. */
    event = input_pointer_move(4, 5, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event,
                                                 &(robo_input_target_t){
                                                     ROBO_INPUT_TARGET_WINDOW, 3u, 0u, ROBO_DISPLAY_ID_NONE,
                                                     { 0u, 0u, 0u }, ROBO_LAYOUT_NODE_ID_NONE, 0u,
                                                     ROBO_INPUT_SPACE_DOCUMENT
                                                 },
                                                 &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
    ROBO_CHECK(!event.has_target);
    {
        robo_input_target_t target;

        ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 3u, ROBO_INPUT_SPACE_SCREEN, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(event.has_target, 1);
        ROBO_CHECK(robo_input_event_is_valid(&event));

        /* A target for another window is refused, and the event keeps the one it had. */
        ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 9u, ROBO_INPUT_SPACE_SCREEN, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, &diag), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_TARGET);
        ROBO_CHECK_EQ_INT(event.target.window, 3u);

        /* Hand-editing the target into another plane leaves an event the validator refuses. */
        event.target.space = ROBO_INPUT_SPACE_DOCUMENT;
        ROBO_CHECK(!robo_input_event_is_valid(&event));
        event.target.space = ROBO_INPUT_SPACE_SCREEN;
        ROBO_CHECK(robo_input_event_is_valid(&event));

        robo_input_event_clear_target(&event);
        ROBO_CHECK_EQ_INT(event.has_target, 0);
        ROBO_CHECK_EQ_INT(event.target.kind, ROBO_INPUT_TARGET_NONE);
        ROBO_CHECK(robo_input_event_is_valid(&event));
    }

    /* A key event *may* carry a target: the plane is the one its routing was concluded in, and a keystroke
     * has no plane of its own to disagree with. */
    event = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1, ROBO_INPUT_MODIFIERS_NONE);
    {
        robo_input_target_t target;

        ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 3u, ROBO_INPUT_SPACE_WINDOW, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
        ROBO_CHECK(robo_input_event_is_valid(&event));
        /* ... but not one naming a window the event is not about. */
        ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 8u, ROBO_INPUT_SPACE_WINDOW, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_INVALID_ARGUMENT);
    }
}

/* ------------------------------------------------------------------ */
/* Copies and equality                                                 */
/* ------------------------------------------------------------------ */

static void test_event_copy_equal(void)
{
    robo_input_event_t original = input_button_event(30, 40, ROBO_MOUSE_BUTTON_LEFT, 1, 3u);
    robo_input_event_t copy;
    robo_input_event_t other;

    ROBO_CHECK_EQ_INT(robo_input_event_copy(&copy, &original), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&original, &copy));
    ROBO_CHECK(robo_input_event_equal(&original, &original));
    ROBO_CHECK(robo_input_event_equal(NULL, NULL));
    ROBO_CHECK(!robo_input_event_equal(&original, NULL));
    ROBO_CHECK_EQ_INT(robo_input_event_copy(NULL, &original), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_event_copy(&copy, NULL), ROBO_INVALID_ARGUMENT);

    /* Every field an event carries is part of its identity: changing any one of them makes them unequal. */
    other = original;
    other.timestamp_ms = 1u;
    ROBO_CHECK(!robo_input_event_equal(&original, &other));
    other = original;
    other.modifiers = ROBO_KEY_MOD_CTRL;
    ROBO_CHECK(!robo_input_event_equal(&original, &other));
    other = original;
    other.display = 2u;
    ROBO_CHECK(!robo_input_event_equal(&original, &other));
    other = original;
    other.payload.pointer.position.x = 31;
    ROBO_CHECK(!robo_input_event_equal(&original, &other));
    other = original;
    other.payload.pointer.click_count = 2;
    ROBO_CHECK(!robo_input_event_equal(&original, &other));
    other = original;
    other.payload.pointer.buttons = 0u;
    ROBO_CHECK(!robo_input_event_equal(&original, &other));
    other = original;
    other.device = input_device(2u, ROBO_INPUT_SOURCE_MOUSE, "Mouse");
    ROBO_CHECK(!robo_input_event_equal(&original, &other));
    other = original;
    ROBO_CHECK_EQ_INT(robo_input_target_set_window(&other.target, 3u, ROBO_INPUT_SPACE_SCREEN, NULL),
                      ROBO_OK);
    other.has_target = 1;
    ROBO_CHECK(!robo_input_event_equal(&original, &other));

    /* Two key events compare their text, which is the payload field most likely to differ by one byte. */
    original = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1, ROBO_INPUT_MODIFIERS_NONE);
    copy = original;
    ROBO_CHECK(robo_input_event_equal(&original, &copy));
    copy.payload.keyboard.text[0] = 'b';
    ROBO_CHECK(!robo_input_event_equal(&original, &copy));
    copy = original;
    copy.payload.keyboard.repeat = 1;
    ROBO_CHECK(!robo_input_event_equal(&original, &copy));
}

/* ------------------------------------------------------------------ */
/* Positions                                                           */
/* ------------------------------------------------------------------ */

static void test_event_position(void)
{
    robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_WINDOW, 3u);
    robo_input_route_t route = input_route(10, 20, 400, 300, 1, 25, 398, 274);
    robo_gfx_point_t point = { 0, 0 };
    robo_input_diag_t diag;

    ROBO_CHECK_EQ_INT(robo_input_event_position(&event, &point, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(point.x, 120);
    ROBO_CHECK_EQ_INT(point.y, 84);

    /* A type with no position says so rather than answering with a zeroed point. */
    {
        robo_input_event_t key = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1,
                                                 ROBO_INPUT_MODIFIERS_NONE);

        ROBO_CHECK_EQ_INT(robo_input_event_position(&key, &point, &diag), ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        ROBO_CHECK_EQ_INT(robo_input_event_position(NULL, &point, NULL), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_event_position(&event, NULL, NULL), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_event_position_in(&key, ROBO_INPUT_SPACE_DOCUMENT, &route, &point,
                                                      NULL),
                          ROBO_NOT_SUPPORTED);
    }

    /* Window → document, the conversion a hit test needs; the frame's borders and title band leave the
     * position, and the client inset is what leaves it. */
    ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_DOCUMENT, &route, &point, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(point.x, 119);
    ROBO_CHECK_EQ_INT(point.y, 59);

    /* Window → screen: the frame's origin is added. */
    ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_SCREEN, &route, &point, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(point.x, 130);
    ROBO_CHECK_EQ_INT(point.y, 104);

    /* The event is not modified: where it was is where it stays. */
    ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_WINDOW);
    ROBO_CHECK_EQ_INT(event.payload.pointer.position.x, 120);

    /* A conversion that needs geometry the caller did not supply is refused, and says which geometry. */
    {
        robo_input_route_t frame_only = input_route_frame_only(10, 20, 400, 300);
        robo_input_diag_t space_diag;

        ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_DOCUMENT, &frame_only,
                                                      &point, &space_diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT(space_diag.domain, ROBO_INPUT_DOMAIN_SPACE);
        ROBO_CHECK(space_diag.message != NULL);
    }
}

void robo_test_register_input_event(void)
{
    robo_test_register("input.event.creation", test_event_creation);
    robo_test_register("input.event.type_queries", test_event_type_queries);
    robo_test_register("input.event.construction", test_event_construction);
    robo_test_register("input.event.validation", test_event_validation);
    robo_test_register("input.event.copy_equal", test_event_copy_equal);
    robo_test_register("input.event.position", test_event_position);
}
