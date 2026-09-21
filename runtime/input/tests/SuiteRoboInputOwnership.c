/*
 * RoboInput ownership suite (Phase 12.1) — `input.ownership.*`.
 *
 * The phase brief's §12 asks for one property above all: a queued event must stay meaningful while the world
 * around it changes -- a caller reuses its record, a document is mutated, a window is destroyed, a layout is
 * rebuilt. The way this module gets that is **value semantics**: an event is a struct, a target holds ids and
 * an inert handle rather than a pointer, and the queue copies on the way in and on the way out. That is a
 * claim about the code, and these cases are how the claim is checked rather than asserted:
 *
 *   `snapshot`        the caller mutates *every* field of the event it pushed after the push, and the queued
 *                     copy is unchanged -- which is what "the queue owns its copy" means;
 *
 *   `target_snapshot` the same for the target, which is the record most likely to be shared by accident, and
 *                     the one whose DOM handle is the identity most likely to be mistaken for a reference;
 *
 *   `geometry`        the geometry a conversion needs is handed in per call and never retained: two calls
 *                     with two routes answer differently, which proves the module caches no window table;
 *
 *   `lifetime`        a queue survives the caller's event going out of scope entirely, and the module holds
 *                     nothing after its last object is destroyed.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* The queued copy is the caller's to forget                            */
/* ------------------------------------------------------------------ */

static void test_ownership_snapshot(void)
{
    robo_input_queue_t *queue = input_queue(4u);
    robo_input_event_t caller = input_button_event(30, 40, ROBO_MOUSE_BUTTON_LEFT, 1, 3u);
    robo_input_event_t expected;
    robo_input_event_t out;
    size_t index;

    ROBO_CHECK_EQ_INT(robo_input_event_set_timestamp(&caller, 77u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_device(&caller,
                                                  &(robo_input_device_t){ 0 }, NULL),
                      ROBO_OK);
    caller.device = input_device(2u, ROBO_INPUT_SOURCE_MOUSE, "Trackpad");
    expected = caller;

    input_push(queue, &caller);

    /* Now wreck the caller's record: every field a producer could reuse, in the way a producer reuses them.
     * The queued event must not move. */
    caller.type = ROBO_INPUT_EVENT_TEXT_INPUT;
    caller.source = ROBO_INPUT_SOURCE_VIRTUAL;
    robo_input_keyboard_data_init(&caller.payload.keyboard);
    caller.payload.pointer.position = robo_gfx_point(-1, -1);
    caller.payload.pointer.buttons = 0u;
    caller.space = ROBO_INPUT_SPACE_UNKNOWN;
    caller.window = 99u;
    caller.timestamp_ms = 0u;
    caller.modifiers = ROBO_KEY_MOD_SHIFT;
    robo_input_device_init(&caller.device);
    caller.device.name[0] = 'x';

    ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 1u);
    ROBO_CHECK_EQ_INT(robo_input_queue_peek(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &expected));
    ROBO_CHECK_EQ_INT(out.payload.pointer.position.x, 30);
    ROBO_CHECK_EQ_INT(out.payload.pointer.buttons,
                      robo_input_button_mask(ROBO_MOUSE_BUTTON_LEFT));
    ROBO_CHECK_EQ_INT(out.timestamp_ms, 77u);
    ROBO_CHECK_STR_EQ(out.device.name, "Trackpad");
    /* ... and the copy is still a *valid* event: an input event copies like the plain record it is. */
    ROBO_CHECK(robo_input_event_is_valid(&out));

    /* The event the caller pushed had gone out of scope in a real caller, so this is the same check without
     * the wrecking: a fresh event with the same contents compares equal to what came back. */
    {
        robo_input_queue_t *second = input_queue(2u);
        robo_input_event_t produced = input_pointer_move(5, 6, 0u, ROBO_INPUT_SPACE_DOCUMENT, 3u);

        input_push(second, &produced);
        produced.payload.pointer.position = robo_gfx_point(0, 0);
        ROBO_CHECK_EQ_INT(robo_input_queue_pop(second, &out, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(out.payload.pointer.position.x, 5);
        ROBO_CHECK_EQ_INT(out.payload.pointer.position.y, 6);
        robo_input_queue_destroy(second);
    }

    /* A whole sequence, pushed from one reused buffer, comes back as the sequence -- which is the pattern a
     * backend actually uses and the one a copy-on-push makes safe. */
    {
        robo_input_queue_t *sequence = input_queue(8u);
        char line[ROBO_INPUT_DUMP_LINE_MAX];

        for (index = 0u; index < 6u; index++) {
            robo_input_event_t step = input_pointer_move((int32_t)index, (int32_t)(index * 2), 0u,
                                                         ROBO_INPUT_SPACE_SCREEN, 3u);

            input_push(sequence, &step);
        }
        for (index = 0u; index < 6u; index++) {
            ROBO_CHECK_EQ_INT(robo_input_queue_pop(sequence, &out, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT(out.payload.pointer.position.x, (int32_t)index);
            ROBO_CHECK_EQ_INT(out.payload.pointer.position.y, (int32_t)(index * 2));
        }
        /* Drained: the dump is the summary line alone, and it says the queue holds nothing. */
        ROBO_CHECK(input_queue_text(sequence, line, sizeof(line)) > 0u);
        ROBO_CHECK(strstr(line, "count=0") != NULL);
        robo_input_queue_destroy(sequence);
    }

    robo_input_queue_destroy(queue);
}

/* ------------------------------------------------------------------ */
/* A target is identity, never a reference                              */
/* ------------------------------------------------------------------ */

static void test_ownership_target_snapshot(void)
{
    robo_input_queue_t *queue = input_queue(4u);
    robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_DOCUMENT, 3u);
    robo_input_event_t expected;
    robo_input_event_t out;
    robo_input_target_t target;
    robo_html_node_handle_t handle = { 4u, 17u, 3u };

    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, &handle, 41u,
                                                     ROBO_INPUT_TARGET_FLAG_CONTENT |
                                                     ROBO_INPUT_TARGET_FLAG_TEXT,
                                                     ROBO_INPUT_SPACE_DOCUMENT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
    expected = event;
    input_push(queue, &event);

    /* The caller now reuses both records -- clears the target, scrubs the handle it passed in, edits the
     * event. None of it may reach the queue. */
    robo_input_target_init(&target);
    robo_html_node_handle_clear(&handle);
    handle.node_id = 999u;
    robo_input_event_clear_target(&event);
    event.payload.pointer.position = robo_gfx_point(0, 0);

    /* `handle` is a *value* the target copied, so scrubbing the caller's copy changes nothing: there is no
     * pointer between them to dangle. */
    handle.document_id = 88u;

    ROBO_CHECK_EQ_INT(robo_input_queue_peek(queue, &out, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_event_equal(&out, &expected));
    ROBO_CHECK_EQ_INT(out.has_target, 1);
    ROBO_CHECK_EQ_INT(out.target.kind, ROBO_INPUT_TARGET_DOCUMENT);
    ROBO_CHECK_EQ_INT(out.target.dom_node.document_id, 4u);
    ROBO_CHECK_EQ_INT(out.target.dom_node.node_id, 17u);
    ROBO_CHECK_EQ_INT(out.target.dom_node.generation, 3u);
    ROBO_CHECK_EQ_INT(out.target.layout_node, 41u);
    ROBO_CHECK(robo_input_target_has_flag(&out.target, ROBO_INPUT_TARGET_FLAG_CONTENT));
    ROBO_CHECK(robo_input_event_is_valid(&out));

    /* A handle that is not well-formed travels too: a consumer with no document identity (a cached
     * document, a synthetic event) still gets the layout node, which is the identity a hit test answered
     * with. */
    {
        robo_input_target_t layout_only;
        robo_input_event_t second = input_pointer_move(1, 1, 0u, ROBO_INPUT_SPACE_DOCUMENT, 3u);

        ROBO_CHECK_EQ_INT(robo_input_target_set_document(&layout_only, 3u, NULL, 7u, 0u,
                                                        ROBO_INPUT_SPACE_DOCUMENT, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_set_target(&second, &layout_only, NULL), ROBO_OK);
        ROBO_CHECK(robo_input_event_is_valid(&second));
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&second.target.dom_node));
        ROBO_CHECK_EQ_INT(second.target.layout_node, 7u);
    }

    robo_input_queue_destroy(queue);
}

/* ------------------------------------------------------------------ */
/* Geometry is handed in, not held                                     */
/* ------------------------------------------------------------------ */

static void test_ownership_geometry(void)
{
    robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_WINDOW, 3u);
    robo_gfx_point_t out = { 0, 0 };
    robo_input_route_t first = input_route(10, 20, 400, 300, 1, 25, 398, 274);
    robo_input_route_t second = input_route(1000, 2000, 400, 300, 1, 25, 398, 274);

    /* The same event, the same conversion, two routes: the answer follows the route that was handed in. A
     * module that had cached a window table would answer the same thing twice. */
    ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_SCREEN, &first, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 130);
    ROBO_CHECK_EQ_INT(out.y, 104);
    ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_SCREEN, &second, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 1120);
    ROBO_CHECK_EQ_INT(out.y, 2084);

    /* The route is a caller's local, and mutating it between calls is legitimate: it is read per call and
     * nothing about the previous call survives. */
    second.window_bounds.x = 5;
    second.window_bounds.y = 6;
    ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_SCREEN, &second, &out, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(out.x, 125);
    ROBO_CHECK_EQ_INT(out.y, 90);

    /* Clearing the geometry it needs turns the next call into a refusal rather than a cached answer. */
    second.has_window_bounds = 0;
    ROBO_CHECK_EQ_INT(robo_input_event_position_in(&event, ROBO_INPUT_SPACE_SCREEN, &second, &out, NULL),
                      ROBO_NOT_SUPPORTED);

    /* ... and the event itself is untouched through all of it. */
    ROBO_CHECK_EQ_INT(event.space, ROBO_INPUT_SPACE_WINDOW);
    ROBO_CHECK_EQ_INT(event.payload.pointer.position.x, 120);
    ROBO_CHECK(robo_input_event_is_valid(&event));
}

/* ------------------------------------------------------------------ */
/* Nothing outlives the last object                                    */
/* ------------------------------------------------------------------ */

static void test_ownership_lifetime(void)
{
    size_t baseline = input_live_blocks();

    /* Queues holding events with targets and text: the three records with copied storage. Destroying the
     * queue must give back exactly what creating and filling it took, with nothing retained per event. */
    {
        int round;

        for (round = 0; round < 3; round++) {
            robo_input_queue_t *queue = input_queue(8u);
            /* A device whose source agrees with the events it is stamped on: a pointer device for a pointer
             * event, because the door refuses the pair disagreeing. */
            robo_input_device_t device = input_device(1u, ROBO_INPUT_SOURCE_MOUSE, "Mouse");
            robo_html_node_handle_t handle = { 1u, 2u, 1u };
            int index;

            for (index = 0; index < 8; index++) {
                robo_input_event_t event = input_pointer_move(index, index, 0u, ROBO_INPUT_SPACE_DOCUMENT, 3u);
                robo_input_target_t target;

                ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, &handle, (robo_layout_node_id_t)index,
                                                                 ROBO_INPUT_TARGET_FLAG_CONTENT,
                                                                 ROBO_INPUT_SPACE_DOCUMENT, NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_input_event_set_device(&event, &device, NULL), ROBO_OK);
                input_push(queue, &event);
            }
            ROBO_CHECK_EQ_INT(robo_input_queue_count(queue), 8u);
            robo_input_queue_destroy(queue);
            ROBO_CHECK_EQ_INT((int)input_live_blocks(), (int)baseline);
        }
    }

    /* An event is a value with no storage of its own: taking one apart costs nothing and frees nothing. */
    {
        robo_input_event_t event = input_key_event(ROBO_PHYSICAL_KEY_A, ROBO_LOGICAL_KEY_A, 1,
                                                   ROBO_INPUT_MODIFIERS_NONE);
        robo_input_event_t copy;

        ROBO_CHECK_EQ_INT(robo_input_event_copy(&copy, &event), ROBO_OK);
        ROBO_CHECK(robo_input_event_equal(&event, &copy));
        ROBO_CHECK_EQ_INT((int)input_live_blocks(), (int)baseline);
    }

    /* A queue can be cleared, destroyed, and a new one made at a different capacity without anything of the
     * old one surviving. */
    {
        robo_input_queue_t *small = input_queue(2u);
        robo_input_queue_t *large;

        robo_input_queue_destroy(small);
        large = input_queue(16u);
        ROBO_CHECK_EQ_INT(robo_input_queue_capacity(large), 16u);
        ROBO_CHECK_EQ_INT(robo_input_queue_count(large), 0u);
        robo_input_queue_destroy(large);
        ROBO_CHECK_EQ_INT((int)input_live_blocks(), (int)baseline);
    }
}

void robo_test_register_input_ownership(void)
{
    robo_test_register("input.ownership.snapshot", test_ownership_snapshot);
    robo_test_register("input.ownership.target_snapshot", test_ownership_target_snapshot);
    robo_test_register("input.ownership.geometry", test_ownership_geometry);
    robo_test_register("input.ownership.lifetime", test_ownership_lifetime);
}
