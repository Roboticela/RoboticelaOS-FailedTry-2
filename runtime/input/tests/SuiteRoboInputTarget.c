/*
 * RoboInput target suite (Phase 12.1) — `input.target.*`.
 *
 * A target is *identity*, and the case that makes that concrete is `from_hit_test`: the three flags a target
 * carries are the three flags a paint hit test concludes (`RoboPaintHitTest.h`), written down in this module
 * so that a dispatcher may copy the word unchanged. The suite includes the paint header **only to compare the
 * constants**, which is the one way a mirrored constant is allowed to exist in this tree: if paint renumbers
 * its flags, this case fails here rather than a dispatcher silently misreading them later.
 *
 * The rest is the kind discipline: which fields a kind carries, which it must *not* carry, and the four
 * records a producer actually gets wrong (a document tip with no window, a window kind with no id, a type
 * that is neither, and a plane that is unset).
 */

#include "RoboInputTestSupport.h"

/* The mirrored constants' origin. This is the *only* reason this suite can see RoboPaint: the boundary scan
 * keeps the module itself well below it, and a test that proves a mirror is not the module depending on it. */
#include "RoboPaint/RoboPaintHitTest.h"

/* ------------------------------------------------------------------ */
/* Kinds                                                               */
/* ------------------------------------------------------------------ */

static void test_target_kinds(void)
{
    robo_input_target_t target;
    robo_input_target_t other;
    robo_html_node_handle_t handle = { 9u, 12u, 2u };
    robo_input_diag_t diag;

    robo_input_target_init(&target);
    ROBO_CHECK_EQ_INT(target.kind, ROBO_INPUT_TARGET_NONE);
    ROBO_CHECK_EQ_INT(target.window, ROBO_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT(target.application, ROBO_INPUT_APPLICATION_ID_NONE);
    ROBO_CHECK_EQ_INT(target.space, ROBO_INPUT_SPACE_UNKNOWN);
    ROBO_CHECK_EQ_INT(target.flags, 0u);
    ROBO_CHECK_EQ_INT(target.layout_node, ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&target.dom_node));
    robo_input_target_init(NULL);

    /* A cleared target has no plane, and a target without a plane is refused: a target whose plane is unset
     * could not be compared against the event it belongs to, which is the check that makes it trustworthy. */
    ROBO_CHECK(!robo_input_target_is_valid(&target));
    ROBO_CHECK(!robo_input_target_is_valid(NULL));
    ROBO_CHECK_EQ_INT(robo_input_target_set_none(&target, ROBO_INPUT_SPACE_UNKNOWN, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_TARGET);
    ROBO_CHECK_EQ_INT(robo_input_target_set_none(&target, ROBO_INPUT_SPACE_SCREEN, NULL), ROBO_OK);
    ROBO_CHECK(robo_input_target_is_valid(&target));
    ROBO_CHECK_EQ_INT(target.kind, ROBO_INPUT_TARGET_NONE);
    ROBO_CHECK_EQ_INT(target.space, ROBO_INPUT_SPACE_SCREEN);
    ROBO_CHECK(!robo_input_target_has_document(&target));
    ROBO_CHECK(!robo_input_target_has_document(NULL));

    /* A window target: a window, and nothing inside it. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 0u, ROBO_INPUT_SPACE_SCREEN, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_TARGET);
    ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 3u, ROBO_INPUT_SPACE_SCREEN, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(target.kind, ROBO_INPUT_TARGET_WINDOW);
    ROBO_CHECK(robo_input_target_is_valid(&target));
    ROBO_CHECK_EQ_INT(robo_input_target_is_window(&target, 3u), 1);
    ROBO_CHECK_EQ_INT(robo_input_target_is_window(&target, 4u), 0);
    ROBO_CHECK_EQ_INT(robo_input_target_is_window(NULL, 3u), 0);

    /* An application target: an application, and no window of it yet. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_application(&target, 0u, ROBO_INPUT_SPACE_WINDOW, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_target_set_application(&target, 5u, ROBO_INPUT_SPACE_WINDOW, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(target.kind, ROBO_INPUT_TARGET_APPLICATION);
    ROBO_CHECK_EQ_INT(target.window, ROBO_WINDOW_ID_NONE);
    ROBO_CHECK(robo_input_target_is_valid(&target));

    /* A document target: a window **and** the node under the point. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 0u, &handle, 7u, 0u,
                                                    ROBO_INPUT_SPACE_DOCUMENT, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_TARGET);
    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, NULL, ROBO_LAYOUT_NODE_ID_NONE, 0u,
                                                    ROBO_INPUT_SPACE_DOCUMENT, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, &handle, 7u,
                                                    ROBO_INPUT_TARGET_FLAG_ALL | (uint32_t)(1u << 20),
                                                    ROBO_INPUT_SPACE_DOCUMENT, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_INPUT_DOMAIN_TARGET);

    /* Either identity alone is enough: a handle with no layout id, and a layout id with no handle. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, &handle, ROBO_LAYOUT_NODE_ID_NONE,
                                                    ROBO_INPUT_TARGET_FLAG_TEXT,
                                                    ROBO_INPUT_SPACE_DOCUMENT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(target.kind, ROBO_INPUT_TARGET_DOCUMENT);
    ROBO_CHECK(robo_input_target_has_document(&target));
    ROBO_CHECK(robo_input_target_is_valid(&target));
    ROBO_CHECK(robo_input_target_has_flag(&target, ROBO_INPUT_TARGET_FLAG_TEXT));
    ROBO_CHECK(!robo_input_target_has_flag(&target, ROBO_INPUT_TARGET_FLAG_CONTENT));
    ROBO_CHECK(!robo_input_target_has_flag(NULL, ROBO_INPUT_TARGET_FLAG_TEXT));
    {
        robo_html_node_handle_t out_handle;
        robo_layout_node_id_t out_node = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_input_target_document(&target, &out_handle, &out_node), 1);
        ROBO_CHECK_EQ_INT(out_handle.document_id, 9u);
        ROBO_CHECK_EQ_INT(out_handle.node_id, 12u);
        ROBO_CHECK_EQ_INT(out_handle.generation, 2u);
        ROBO_CHECK_EQ_INT(out_node, ROBO_LAYOUT_NODE_ID_NONE);
    }

    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, NULL, 41u, 0u, ROBO_INPUT_SPACE_DOCUMENT,
                                                    NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_input_target_is_valid(&target));
    ROBO_CHECK_EQ_INT(target.layout_node, 41u);

    /* The document identity read back is all-or-nothing: a target of another kind answers "no document" and
     * clears what it was given. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 3u, ROBO_INPUT_SPACE_SCREEN, NULL), ROBO_OK);
    {
        robo_html_node_handle_t out_handle = { 1u, 1u, 1u };
        robo_layout_node_id_t out_node = 9u;

        ROBO_CHECK_EQ_INT(robo_input_target_document(&target, &out_handle, &out_node), 0);
        ROBO_CHECK(!robo_html_node_handle_is_wellformed(&out_handle));
        ROBO_CHECK_EQ_INT(out_node, ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(robo_input_target_document(&target, NULL, NULL), 0);
    }

    /* The four records a producer gets wrong, each refused by the *validator* rather than only by a setter --
     * a target can arrive from anywhere. */
    {
        robo_input_target_t broken;

        /* A document kind with no window. */
        robo_input_target_init(&broken);
        broken.kind = ROBO_INPUT_TARGET_DOCUMENT;
        broken.space = ROBO_INPUT_SPACE_DOCUMENT;
        broken.layout_node = 3u;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));

        /* ... with a window but no node at all: a window target wearing a document's kind. */
        broken.window = 3u;
        broken.layout_node = ROBO_LAYOUT_NODE_ID_NONE;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));

        /* A window kind with everything a window target must not carry. */
        robo_input_target_init(&broken);
        broken.kind = ROBO_INPUT_TARGET_WINDOW;
        broken.space = ROBO_INPUT_SPACE_SCREEN;
        broken.window = 3u;
        broken.application = 4u;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));
        broken.application = ROBO_INPUT_APPLICATION_ID_NONE;
        broken.flags = (uint32_t)ROBO_INPUT_TARGET_FLAG_TEXT;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));
        broken.flags = 0u;
        broken.layout_node = 8u;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));
        broken.layout_node = ROBO_LAYOUT_NODE_ID_NONE;
        ROBO_CHECK(robo_input_target_is_valid(&broken));

        /* An application kind that also names a window, and a kind that is not one. */
        broken.kind = ROBO_INPUT_TARGET_APPLICATION;
        broken.application = 4u;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));
        broken.window = ROBO_WINDOW_ID_NONE;
        ROBO_CHECK(robo_input_target_is_valid(&broken));
        broken.kind = (robo_input_target_kind_t)9;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));

        /* A plane that is not one. */
        robo_input_target_init(&broken);
        broken.space = (robo_input_space_t)9;
        ROBO_CHECK(!robo_input_target_is_valid(&broken));
    }

    /* Equality is field-by-field, and the two records are not the same one. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 3u, ROBO_INPUT_SPACE_SCREEN, NULL), ROBO_OK);
    other = target;
    ROBO_CHECK(robo_input_target_equal(&target, &other));
    ROBO_CHECK(robo_input_target_equal(NULL, NULL));
    ROBO_CHECK(!robo_input_target_equal(&target, NULL));
    other.window = 4u;
    ROBO_CHECK(!robo_input_target_equal(&target, &other));
    other = target;
    other.space = ROBO_INPUT_SPACE_WINDOW;
    ROBO_CHECK(!robo_input_target_equal(&target, &other));

    /* Setting a target through a door resets a record that had something else in it, so a caller that reuses
     * one record per event cannot leave a stale field behind. */
    ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 3u, &handle, 12u, 0u,
                                                    ROBO_INPUT_SPACE_DOCUMENT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_input_target_set_window(&target, 6u, ROBO_INPUT_SPACE_SCREEN, NULL), ROBO_OK);
    ROBO_CHECK(!robo_html_node_handle_is_wellformed(&target.dom_node));
    ROBO_CHECK_EQ_INT(target.layout_node, ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(target.flags, 0u);
    ROBO_CHECK_EQ_INT(target.space, ROBO_INPUT_SPACE_SCREEN);
}

/* ------------------------------------------------------------------ */
/* The mirrored hit-test facts                                         */
/* ------------------------------------------------------------------ */

static void test_target_from_hit_test(void)
{
    /* The three facts a hit test concludes, mirrored with the same bit positions. Asserted *equal* to
     * paint's, which is what makes the mirror a checked copy rather than a second vocabulary. */
    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_FLAG_TEXT, ROBO_PAINT_HIT_FLAG_TEXT);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_FLAG_CONTENT, ROBO_PAINT_HIT_FLAG_CONTENT);
    ROBO_CHECK_EQ_INT(ROBO_INPUT_TARGET_FLAG_TRANSPARENT, ROBO_PAINT_HIT_FLAG_TRANSPARENT);

    /* A word a hit test produced can therefore be carried unchanged, including "all three". */
    {
        robo_input_target_t target;
        robo_html_node_handle_t handle = { 2u, 3u, 1u };
        uint32_t from_hit_test = ROBO_PAINT_HIT_FLAG_TEXT | ROBO_PAINT_HIT_FLAG_CONTENT |
                                 ROBO_PAINT_HIT_FLAG_TRANSPARENT;

        ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, 1u, &handle, 5u, from_hit_test,
                                                        ROBO_INPUT_SPACE_DOCUMENT, NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_input_target_has_flag(&target, ROBO_INPUT_TARGET_FLAG_TEXT));
        ROBO_CHECK(robo_input_target_has_flag(&target, ROBO_INPUT_TARGET_FLAG_CONTENT));
        ROBO_CHECK(robo_input_target_has_flag(&target, ROBO_INPUT_TARGET_FLAG_TRANSPARENT));
        ROBO_CHECK(robo_input_target_is_valid(&target));
    }

    /* The integration contract, asserted end to end without a dispatcher existing: an input event carries
     * the window a hit test was performed in, the document plane a hit test reads, and the identity a hit
     * test answered with -- so the next phase can route it and this phase has not routed anything. */
    {
        robo_input_event_t event = input_pointer_move(120, 84, 0u, ROBO_INPUT_SPACE_DOCUMENT, 3u);
        robo_input_target_t target;
        robo_html_node_handle_t handle = { 4u, 17u, 3u };

        ROBO_CHECK_EQ_INT(robo_input_target_set_document(&target, event.window, &handle, 41u,
                                                        ROBO_INPUT_TARGET_FLAG_CONTENT,
                                                        event.space, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_event_set_target(&event, &target, NULL), ROBO_OK);
        ROBO_CHECK(robo_input_event_is_valid(&event));
        ROBO_CHECK_EQ_INT(robo_input_target_is_window(&event.target, event.window), 1);
        ROBO_CHECK(robo_input_target_has_document(&event.target));
    }
}

void robo_test_register_input_target(void)
{
    robo_test_register("input.target.kinds", test_target_kinds);
    robo_test_register("input.target.from_hit_test", test_target_from_hit_test);
}
