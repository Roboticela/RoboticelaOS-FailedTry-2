/*
 * RoboInput debug suite (Phase 12.1) — `input.debug.*`.
 *
 * The dump is a *door*, not a `printf`: it is asserted whole, line by line, against the exact text this build
 * produces. Two consequences that are the point of the suite:
 *
 *   a field that stops being printed, or moves, fails here rather than being noticed by whoever was reading
 *   the text at the time -- which is why the pointer, key, text and target cases each compare a complete
 *   line instead of searching for a substring;
 *
 *   **determinism** is a case, not a claim. The same sequence dumped twice is byte-identical, dumped from two
 *   different queues is byte-identical, and nothing in the text is a function of an address -- the three
 *   properties a developer relies on when they diff two runs.
 *
 * `line_bound` is the fourth: the public constant `ROBO_INPUT_DUMP_LINE_MAX` is a promise about the longest
 * line this build can emit, so the widest event each payload kind can express is built and written through a
 * buffer of exactly that size.
 */

#include "RoboInputTestSupport.h"

#include <limits.h>

/* ------------------------------------------------------------------ */
/* One line, exactly                                                   */
/* ------------------------------------------------------------------ */

static void test_debug_pointer_line(void)
{
    robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    char line[ROBO_INPUT_DUMP_LINE_MAX];
    size_t length = 0u;

    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(line,
                      "PointerMove source=mouse device=none window=3 display=0 time=0 modifiers=none "
                      "space=screen pointer=0 position=120,84 previous=none delta=0,0 buttons=none "
                      "button=none clicks=0 wheel=0,0 unit=line flags=primary");
    ROBO_CHECK_EQ_INT((int)length, (int)strlen(line));
    /* No newline inside a line: the only newline a writer emits is the one that ends a line of a *queue*. */
    ROBO_CHECK(strchr(line, '\n') == NULL);

    /* A wheel with a device, a modifier, a previous position, a held button and a click count: every field
     * of the payload, in their fixed order. */
    {
        robo_input_pointer_data_t data;
        robo_input_device_t device = input_device(1u, ROBO_INPUT_SOURCE_MOUSE, "Mouse");

        robo_input_pointer_data_init(&data);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, ROBO_INPUT_POINTER_ID_PRIMARY,
                                                     robo_gfx_point(120, 84), ROBO_MOUSE_BUTTON_NONE,
                                                     robo_input_button_mask(ROBO_MOUSE_BUTTON_LEFT)),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_previous(&data, robo_gfx_point(118, 84)), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_wheel(&data, 0, 120, ROBO_MOUSE_SCROLL_UNIT_LINE),
                          ROBO_OK);
        data.click_count = 1;

        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_WHEEL,
                                                      ROBO_INPUT_SOURCE_MOUSE, &data,
                                                      ROBO_INPUT_SPACE_SCREEN, NULL),
                          ROBO_OK);
        robo_input_event_set_window(&event, 3u);
        robo_input_event_set_timestamp(&event, 1841u);
        robo_input_event_set_modifiers(&event, ROBO_KEY_MOD_CTRL, NULL);
        ROBO_CHECK_EQ_INT(robo_input_event_set_device(&event, &device, NULL), ROBO_OK);

        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
        ROBO_CHECK_STR_EQ(line,
                          "Wheel source=mouse device=1:Mouse window=3 display=0 time=1841 modifiers=control "
                          "space=screen pointer=0 position=120,84 previous=118,84 delta=2,0 buttons=left "
                          "button=none clicks=1 wheel=0,120 unit=line flags=primary");
    }

    /* A release names its button, and a mask with several buttons reads as the list a developer scans. */
    {
        robo_input_pointer_data_t data;

        robo_input_pointer_data_init(&data);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, ROBO_INPUT_POINTER_ID_PRIMARY,
                                                     robo_gfx_point(-5, 6), ROBO_MOUSE_BUTTON_X1,
                                                     robo_input_button_mask(ROBO_MOUSE_BUTTON_MIDDLE) |
                                                     robo_input_button_mask(ROBO_MOUSE_BUTTON_RIGHT)),
                          ROBO_OK);
        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_POINTER_UP,
                                                      ROBO_INPUT_SOURCE_MOUSE, &data,
                                                      ROBO_INPUT_SPACE_WINDOW, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
        ROBO_CHECK_STR_EQ(line,
                          "PointerUp source=mouse device=none window=0 display=0 time=0 modifiers=none "
                          "space=window pointer=0 position=-5,6 previous=none delta=0,0 "
                          "buttons=right+middle button=x1 clicks=0 wheel=0,0 unit=line flags=primary");
    }
}

static void test_debug_key_lines(void)
{
    robo_input_event_t event;
    robo_input_keyboard_data_t key = input_key_data(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A,
                                                    ROBO_KEY_ACTION_DOWN, 0, "a");
    char line[ROBO_INPUT_DUMP_LINE_MAX];
    size_t length = 0u;

    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_KEY_DOWN,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, &key, NULL),
                      ROBO_OK);
    robo_input_event_set_window(&event, 3u);
    robo_input_event_set_modifiers(&event, ROBO_KEY_MOD_SHIFT, NULL);
    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    /* `space=unknown` is the honest answer for a keystroke: it has no position, so it names no plane. */
    ROBO_CHECK_STR_EQ(line,
                      "KeyDown source=keyboard device=none window=3 display=0 time=0 modifiers=shift "
                      "space=unknown physical=A logical=A action=down repeat=0 flags=none text=\"a\"");

    /* A repeat and a physical identity the platform could not tell: the two flags, in one line. The
     * `MODIFIER` flag is derived from the *physical* key, which is exactly why it is absent here -- a key
     * whose position the platform did not report cannot be *known* to be a modifier, whatever its logical
     * identity says. */
    key = input_key_data(ROBO_PHYSICAL_KEY_UNKNOWN, ROBO_LOGICAL_KEY_SHIFT, ROBO_KEY_ACTION_DOWN, 1, NULL);
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_KEY_DOWN,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, &key, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(line,
                      "KeyDown source=keyboard device=none window=0 display=0 time=0 modifiers=none "
                      "space=unknown physical=unknown logical=Shift action=down repeat=1 "
                      "flags=physical-unknown text=\"\"");

    /* A key up carries no text, and the two keys a case is most likely to mix up read differently. */
    key = input_key_data(ROBO_PHYSICAL_KEY_NUMPAD_5, ROBO_LOGICAL_KEY_NUMPAD_5, ROBO_KEY_ACTION_UP, 0, NULL);
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_KEY_UP,
                                                   ROBO_INPUT_SOURCE_KEYBOARD, &key, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(line,
                      "KeyUp source=keyboard device=none window=0 display=0 time=0 modifiers=none "
                      "space=unknown physical=Numpad5 logical=Numpad5 action=up repeat=0 flags=numpad "
                      "text=\"\"");

    /* Text input is an event with no payload but a string, and a target is the suffix a routed event adds. */
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_text(&event, ROBO_INPUT_SOURCE_KEYBOARD, "A", NULL), ROBO_OK);
    robo_input_event_set_window(&event, 3u);
    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(line,
                      "TextInput source=keyboard device=none window=3 display=0 time=0 modifiers=none "
                      "space=unknown text=\"A\"");
}

static void test_debug_target_suffix(void)
{
    robo_input_event_t event;
    robo_input_pointer_data_t data;
    robo_input_target_t target;
    robo_html_node_handle_t handle = { 4u, 17u, 3u };
    char line[ROBO_INPUT_DUMP_LINE_MAX];
    size_t length = 0u;

    /* A cancel, so that the flag list has two entries and the type's own name says which phase ended. */
    robo_input_pointer_data_init(&data);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, 1u, robo_gfx_point(40, 50), ROBO_MOUSE_BUTTON_NONE,
                                                 0u),
                      ROBO_OK);
    data.flags |= ROBO_INPUT_POINTER_FLAG_CANCELLED;
    robo_input_event_init(&event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_TOUCH_CANCEL,
                                                  ROBO_INPUT_SOURCE_TOUCH, &data,
                                                  ROBO_INPUT_SPACE_DOCUMENT, NULL),
                      ROBO_OK);
    robo_input_event_set_window(&event, 3u);
    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, &handle, 41u,
                                                    ROBO_INPUT_TARGET_FLAG_TEXT |
                                                    ROBO_INPUT_TARGET_FLAG_CONTENT,
                                                    ROBO_INPUT_SPACE_DOCUMENT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    ROBO_CHECK_STR_EQ(line,
                      "TouchCancel source=touch device=none window=3 display=0 time=0 modifiers=none "
                      "space=document pointer=1 position=40,50 previous=none delta=0,0 buttons=none "
                      "button=none clicks=0 wheel=0,0 unit=line flags=primary+cancelled target=document "
                      "target_window=3 target_node=4:17:3 target_layout=41 target_flags=text+content");

    /* A window target prints no node and no flags, because a window target does not carry them. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 3u, ROBO_INPUT_SPACE_DOCUMENT, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    ROBO_CHECK(strstr(line, " target=window target_window=3") != NULL);
    ROBO_CHECK(strstr(line, "target_node") == NULL);
    ROBO_CHECK(strstr(line, "target_flags") == NULL);

    /* An application target prints its application and nothing else. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_application(&target, 7u, ROBO_INPUT_SPACE_DOCUMENT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
    ROBO_CHECK(strstr(line, " target=application target_app=7") != NULL);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

/** Push a fixed sequence of six events -- every payload kind and a target -- into @p queue. */
static void debug_sequence(robo_input_queue_t *queue)
{
    robo_input_event_t event;
    robo_input_target_t target;
    robo_html_node_handle_t handle = { 1u, 4u, 1u };
    int index;

    for (index = 0; index < 4; index++) {
        event = input_pointer_move(index * 10, index * 20, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);

        ROBO_CHECK_EQ_INT(robo_input_event_set_timestamp(&event, (uint64_t)index), ROBO_OK);
        input_push(queue, &event);
    }
    event = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1, ROBO_INPUT_MODIFIERS_NONE);
    input_push(queue, &event);
    ROBO_CHECK_EQ_INT(robo_input_event_set_text(&event, ROBO_INPUT_SOURCE_KEYBOARD, "x", NULL), ROBO_OK);
    input_push(queue, &event);

    event = input_pointer_move(7, 7, 0u, ROBO_INPUT_SPACE_DOCUMENT, 3u);
    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, &handle, 12u,
                                                    ROBO_INPUT_TARGET_FLAG_CONTENT,
                                                    ROBO_INPUT_SPACE_DOCUMENT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
    input_push(queue, &event);
}

static void test_debug_determinism(void)
{
    robo_input_queue_t *first = input_queue(8u);
    robo_input_queue_t *second = input_queue(8u);
    char first_text[4096];
    char second_text[4096];
    size_t first_length;
    size_t second_length;
    size_t index;

    debug_sequence(first);
    debug_sequence(second);

    first_length = input_queue_text(first, first_text, sizeof(first_text));
    second_length = input_queue_text(second, second_text, sizeof(second_text));

    /* Two queues, the same sequence, byte-identical text: nothing in a dump is a function of an address or
     * of which object it came from. */
    ROBO_CHECK(first_length > 0u);
    ROBO_CHECK_EQ_INT((int)second_length, (int)first_length);
    ROBO_CHECK_STR_EQ(first_text, second_text);

    /* ... and dumping one queue twice is byte-identical too, which is the property a developer diffs. */
    ROBO_CHECK_EQ_INT((int)input_queue_text(first, second_text, sizeof(second_text)), (int)first_length);
    ROBO_CHECK_STR_EQ(first_text, second_text);

    /* The summary line first, then one line per event, each terminated. */
    ROBO_CHECK(strncmp(first_text, "queue capacity=8 count=7", 24) == 0);
    ROBO_CHECK_EQ_INT((int)robo_input_queue_count(first), 7u);
    {
        int newlines = 0;

        for (index = 0u; index < first_length; index++) {
            if (first_text[index] == '\n') {
                newlines++;
            }
        }
        ROBO_CHECK_EQ_INT(newlines, 8);
    }
    ROBO_CHECK_EQ_INT(first_text[first_length - 1u], '\n');

    /* `0x` is the one shape an address would take, and the dump never contains it. */
    ROBO_CHECK(strstr(first_text, "0x") == NULL);

    /* Reading it does not change the queue: a dump is a look. */
    ROBO_CHECK_EQ_INT((int)robo_input_queue_count(first), 7u);

    robo_input_queue_destroy(first);
    robo_input_queue_destroy(second);
}

/* ------------------------------------------------------------------ */
/* The line bound, and refusing rather than truncating                  */
/* ------------------------------------------------------------------ */

static void test_debug_line_bound(void)
{
    char line[ROBO_INPUT_DUMP_LINE_MAX];
    size_t length = 0u;

    /* The widest pointer event this build can express: the most negative coordinates, a full button mask,
     * every flag, the longest unit name, the longest space name, the longest device name, every modifier
     * bit, and a document target whose ids are at their maximum. */
    {
        robo_input_pointer_data_t data;
        robo_input_event_t event;
        robo_input_target_t target;
        robo_input_device_t device;
        char name[ROBO_INPUT_DEVICE_NAME_MAX];
        size_t index;

        for (index = 0u; index + 1u < ROBO_INPUT_DEVICE_NAME_MAX; index++) {
            name[index] = 'q';
        }
        name[ROBO_INPUT_DEVICE_NAME_MAX - 1u] = '\0';
        device = input_device((robo_input_device_id_t)-1, ROBO_INPUT_SOURCE_MOUSE, name);

        robo_input_pointer_data_init(&data);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, (robo_input_pointer_id_t)-1,
                                                     robo_gfx_point(INT32_MIN, INT32_MIN),
                                                     ROBO_MOUSE_BUTTON_RIGHT,
                                                     (uint32_t)-1),
                          ROBO_INVALID_ARGUMENT);
        {
            uint32_t all = 0u;
            int button;

            for (button = (int)ROBO_MOUSE_BUTTON_LEFT; button <= (int)ROBO_MOUSE_BUTTON_X2; button++) {
                all = robo_input_button_mask_apply(all, (robo_mouse_button_t)button, 1);
            }
            /* No button on the event itself: a cancel is not *about* one button, and its own button would
             * have to be absent from the mask it holds. */
            ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, (robo_input_pointer_id_t)-1,
                                                         robo_gfx_point(INT32_MIN, INT32_MIN),
                                                         ROBO_MOUSE_BUTTON_NONE, all),
                              ROBO_OK);
        }
        /* A previous position at the same most-negative point: the delta is zero, which is the widest form
         * of the field that stays representable. (A previous position at the opposite end of the range would
         * be refused, because the delta between the two does not fit -- see `input.pointer.payload`.) */
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_previous(&data,
                                                              robo_gfx_point(INT32_MIN, INT32_MIN)),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_wheel(&data, INT32_MIN, INT32_MIN,
                                                           ROBO_MOUSE_SCROLL_UNIT_PIXEL),
                          ROBO_OK);
        /* A click count cannot be negative -- the payload's own rule -- so the widest line carries the
         * largest count there is. */
        data.click_count = INT32_MAX;
        data.flags = ROBO_INPUT_POINTER_FLAG_ALL;

        /* A cancel, because that is the type whose payload may carry all three flags at once: a press may not
         * be cancelled, so the widest *valid* line is the cancel's. */
        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_set_pointer(&event, ROBO_INPUT_EVENT_POINTER_CANCEL,
                                                      ROBO_INPUT_SOURCE_MOUSE, &data,
                                                      ROBO_INPUT_SPACE_APPLICATION, NULL),
                          ROBO_OK);
        robo_input_event_set_window(&event, (robo_window_id_t)-1);
        robo_input_event_set_display(&event, (robo_display_id_t)-1);
        robo_input_event_set_timestamp(&event, (uint64_t)-1);
        ROBO_CHECK_EQ_INT(robo_input_event_set_modifiers(&event, ROBO_INPUT_MODIFIER_ALL, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_set_device(&event, &device, NULL), ROBO_OK);
        {
            robo_html_node_handle_t handle = { (robo_html_document_id_t)-1, (robo_html_node_id_t)-1,
                                               (uint32_t)-1 };

            ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, (robo_window_id_t)-1, &handle,
                                                            (robo_layout_node_id_t)-1, ROBO_INPUT_TARGET_FLAG_ALL,
                                                            ROBO_INPUT_SPACE_APPLICATION, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
        }
        ROBO_CHECK(robo_input_event_is_valid(&event));

        /* Written through a buffer of exactly the stated bound: the promise holds for the widest line. */
        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
        ROBO_CHECK(length < (size_t)ROBO_INPUT_DUMP_LINE_MAX);
        ROBO_CHECK_EQ_INT((int)length, (int)strlen(line));
        /* The bound is comfortable rather than exact, and it is *stated* rather than described: three
         * quarters of it is already a line no honest event reaches. */
        ROBO_CHECK(length < (size_t)(ROBO_INPUT_DUMP_LINE_MAX - 100u));

        /* A buffer one byte short of the line refuses, reports what it needed, and leaves a `?` rather than a
         * prefix that reads as a whole event. */
        {
            char small[ROBO_INPUT_DUMP_LINE_MAX];
            size_t needed = 0u;

            ROBO_CHECK_EQ_INT(robo_input_event_write(&event, small, (size_t)length, &needed, NULL),
                              ROBO_NOT_SUPPORTED);
            ROBO_CHECK_EQ_INT((int)needed, (int)length);
            ROBO_CHECK_STR_EQ(small, "?");
            ROBO_CHECK_EQ_INT(robo_input_event_write(&event, small, 1u, &needed, NULL), ROBO_NOT_SUPPORTED);
            ROBO_CHECK_STR_EQ(small, "?");
        }
    }

    /* A key event and a text event at their widest, for the same promise. */
    {
        robo_input_event_t event;
        robo_input_keyboard_data_t key;
        char text[ROBO_INPUT_TEXT_MAX];
        size_t index;

        for (index = 0u; index + 1u < ROBO_INPUT_TEXT_MAX; index++) {
            text[index] = 'w';
        }
        text[ROBO_INPUT_TEXT_MAX - 1u] = '\0';

        key = input_key_data(ROBO_PHYSICAL_KEY_UNKNOWN, ROBO_LOGICAL_KEY_UNKNOWN, ROBO_KEY_ACTION_DOWN, 1,
                             text);
        key.flags = ROBO_INPUT_KEY_FLAG_ALL;
        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_set_keyboard(&event, ROBO_INPUT_EVENT_KEY_DOWN,
                                                       ROBO_INPUT_SOURCE_REMOTE, &key, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
        ROBO_CHECK(length < (size_t)ROBO_INPUT_DUMP_LINE_MAX);

        robo_input_event_init(&event);
        ROBO_CHECK_EQ_INT(robo_input_event_set_text(&event, ROBO_INPUT_SOURCE_REMOTE, text, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), &length, NULL), ROBO_OK);
        ROBO_CHECK(length < (size_t)ROBO_INPUT_DUMP_LINE_MAX);
    }

    /* Arguments: NULL everywhere is a refusal, and a refused dump leaves the length at what it needed. */
    {
        robo_input_event_t event = input_pointer_move(1, 2, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
        size_t needed = 0u;

        ROBO_CHECK_EQ_INT(robo_input_event_write(NULL, line, sizeof(line), &length, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, NULL, sizeof(line), &length, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, 0u, &length, NULL), ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_event_write(&event, line, sizeof(line), NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_queue_write(NULL, line, sizeof(line), &needed, NULL),
                          ROBO_INVALID_ARGUMENT);
    }
}

/* ------------------------------------------------------------------ */
/* The queue dump                                                      */
/* ------------------------------------------------------------------ */

static void test_debug_queue_dump(void)
{
    robo_input_queue_t *queue = input_queue(4u);
    char text[4096];
    size_t length = 0u;
    robo_input_event_t first = input_pointer_move(1, 2, 0u, ROBO_INPUT_SPACE_SCREEN, 3u);
    robo_input_event_t second = input_key_event(ROBO_PHYSICAL_KEY_B, ROBO_LOGICAL_KEY_B, 1,
                                                ROBO_INPUT_MODIFIERS_NONE);

    /* An empty queue writes its summary alone: a legitimate dump, not a refusal. */
    ROBO_CHECK_EQ_INT(robo_input_queue_write(queue, text, sizeof(text), &length, NULL), ROBO_OK);
    ROBO_CHECK(strncmp(text, "queue capacity=4 count=0", 23) == 0);
    ROBO_CHECK(strstr(text, "pushed=0 popped=0 dropped=0 cleared=0 high_water=0") != NULL);
    ROBO_CHECK_EQ_INT((int)length, (int)strlen(text));

    input_push(queue, &first);
    input_push(queue, &second);
    {
        robo_input_event_t out;

        ROBO_CHECK_EQ_INT(robo_input_queue_pop(queue, &out, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_input_queue_push(queue, &first, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_input_queue_write(queue, text, sizeof(text), &length, NULL), ROBO_OK);
    ROBO_CHECK(strncmp(text, "queue capacity=4 count=2 pushed=3 popped=1", 37) == 0);
    ROBO_CHECK(strstr(text, "dropped=0 cleared=0 high_water=2") != NULL);
    /* One line per queued event, in FIFO order: the key event that is still queued, then the pointer event
     * pushed back on the end. */
    {
        const char *first_line = strchr(text, '\n');

        ROBO_CHECK(first_line != NULL);
        ROBO_CHECK(strncmp(first_line + 1, "KeyDown ", 8) == 0);
        ROBO_CHECK(strstr(first_line + 1, "\nPointerMove source=mouse") != NULL);
        /* Nothing after the last event line: the text ends with the newline that terminated it. */
        ROBO_CHECK_EQ_INT(text[length - 1u], '\n');
    }

    /* A buffer too small for the whole text refuses and reports what the *whole* text needs, not what the
     * first line needs. */
    {
        char small[64];
        size_t needed = 0u;

        ROBO_CHECK_EQ_INT(robo_input_queue_write(queue, small, sizeof(small), &needed, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT((int)needed, (int)length);
        ROBO_CHECK_STR_EQ(small, "?");
    }

    robo_input_queue_destroy(queue);
}

void robo_test_register_input_debug(void)
{
    robo_test_register("input.debug.pointer_line", test_debug_pointer_line);
    robo_test_register("input.debug.key_lines", test_debug_key_lines);
    robo_test_register("input.debug.target_suffix", test_debug_target_suffix);
    robo_test_register("input.debug.determinism", test_debug_determinism);
    robo_test_register("input.debug.line_bound", test_debug_line_bound);
    robo_test_register("input.debug.queue_dump", test_debug_queue_dump);
}
