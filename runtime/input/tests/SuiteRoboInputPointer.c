/*
 * RoboInput pointer suite (Phase 12.1) — `input.pointer.*`.
 *
 * Three cases about the one payload that carries a *place*: what a record holds, what a mask may say, and
 * what a wheel's deltas mean. The second is the one worth reading: a pressed-button mask is a bitmask over
 * RoboWindow's button identities, so the mapping between a button and its bit lives in exactly one function,
 * and this case pins that mapping rather than describing it.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* The payload                                                         */
/* ------------------------------------------------------------------ */

static void test_pointer_payload(void)
{
    robo_input_pointer_data_t data;

    robo_input_pointer_data_init(&data);
    ROBO_CHECK_EQ_INT(data.pointer, ROBO_INPUT_POINTER_ID_PRIMARY);
    ROBO_CHECK_EQ_INT(data.button, ROBO_MOUSE_BUTTON_NONE);
    ROBO_CHECK_EQ_INT(data.buttons, 0u);
    ROBO_CHECK_EQ_INT(data.position.x, 0);
    ROBO_CHECK_EQ_INT(data.has_previous, 0);
    ROBO_CHECK_EQ_INT(data.delta_x, 0);
    ROBO_CHECK_EQ_INT(data.click_count, 0);
    ROBO_CHECK_EQ_INT(data.wheel.unit, ROBO_MOUSE_SCROLL_UNIT_LINE);
    /* A cleared pointer is the primary one: an event that forgot to name a pointer means the mouse. */
    ROBO_CHECK(data.flags & ROBO_INPUT_POINTER_FLAG_PRIMARY);
    ROBO_CHECK(robo_input_pointer_data_is_valid(&data));
    robo_input_pointer_data_init(NULL);

    /* Position, button and mask in one door, so a press cannot be recorded without its place. */
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, ROBO_INPUT_POINTER_ID_PRIMARY,
                                                 robo_gfx_point(120, 84), ROBO_MOUSE_BUTTON_RIGHT,
                                                 robo_input_button_mask_apply(0u, ROBO_MOUSE_BUTTON_RIGHT,
                                                                              1)),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.position.x, 120);
    ROBO_CHECK_EQ_INT(data.position.y, 84);
    ROBO_CHECK_EQ_INT(data.button, ROBO_MOUSE_BUTTON_RIGHT);
    ROBO_CHECK(robo_input_button_mask_has(data.buttons, ROBO_MOUSE_BUTTON_RIGHT));

    /* What is refused: an unknown button, a mask bit no button stands for, and NULL. */
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, ROBO_INPUT_POINTER_ID_PRIMARY,
                                                 robo_gfx_point(1, 2), ROBO_MOUSE_BUTTON_UNKNOWN, 0u),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&data, ROBO_INPUT_POINTER_ID_PRIMARY,
                                                 robo_gfx_point(1, 2), ROBO_MOUSE_BUTTON_NONE,
                                                 (uint32_t)(1u << 6)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(NULL, ROBO_INPUT_POINTER_ID_PRIMARY,
                                                 robo_gfx_point(1, 2), ROBO_MOUSE_BUTTON_NONE, 0u),
                      ROBO_INVALID_ARGUMENT);

    /* A previous position derives the delta, so the two fields cannot disagree. */
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_previous(&data, robo_gfx_point(118, 84)), ROBO_OK);
    ROBO_CHECK_EQ_INT(data.has_previous, 1);
    ROBO_CHECK_EQ_INT(data.previous.x, 118);
    ROBO_CHECK_EQ_INT(data.delta_x, 2);
    ROBO_CHECK_EQ_INT(data.delta_y, 0);
    /* A stationary report is legitimate, and a *negative* movement too. */
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_previous(&data, robo_gfx_point(120, 84)), ROBO_OK);
    ROBO_CHECK_EQ_INT(data.delta_x, 0);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_previous(&data, robo_gfx_point(140, 100)), ROBO_OK);
    ROBO_CHECK_EQ_INT(data.delta_x, -20);
    ROBO_CHECK_EQ_INT(data.delta_y, -16);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_previous(NULL, robo_gfx_point(0, 0)),
                      ROBO_INVALID_ARGUMENT);

    /* Two representable positions can differ by more than a delta can hold. That is refused, and *nothing*
     * is written: the record still holds the previous position and delta of the last accepted call. */
    {
        robo_input_pointer_data_t extremes;

        robo_input_pointer_data_init(&extremes);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set(&extremes, ROBO_INPUT_POINTER_ID_PRIMARY,
                                                     robo_gfx_point(INT32_MIN, 0), ROBO_MOUSE_BUTTON_NONE,
                                                     0u),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_previous(&extremes,
                                                              robo_gfx_point(INT32_MAX, 0)),
                          ROBO_ERROR);
        ROBO_CHECK_EQ_INT(extremes.has_previous, 0);
        ROBO_CHECK_EQ_INT(extremes.delta_x, 0);
        ROBO_CHECK_EQ_INT(extremes.previous.x, 0);
        /* ... and the record is still one the module would carry. */
        ROBO_CHECK(robo_input_pointer_data_is_valid(&extremes));
    }

    /* The wheel: deltas and the unit they are in, with an undefined unit refused. */
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_wheel(&data, 0, 120, ROBO_MOUSE_SCROLL_UNIT_LINE),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.wheel.delta_y, 120);
    ROBO_CHECK_EQ_INT(data.wheel.unit, ROBO_MOUSE_SCROLL_UNIT_LINE);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_wheel(&data, -40, 0, ROBO_MOUSE_SCROLL_UNIT_PIXEL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(data.wheel.delta_x, -40);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_wheel(&data, 0, 0, (robo_mouse_scroll_unit_t)7),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(data.wheel.unit, ROBO_MOUSE_SCROLL_UNIT_PIXEL);
    ROBO_CHECK_EQ_INT(robo_input_pointer_data_set_wheel(NULL, 0, 0, ROBO_MOUSE_SCROLL_UNIT_LINE),
                      ROBO_INVALID_ARGUMENT);

    /* Identities. */
    ROBO_CHECK_EQ_INT(robo_input_scroll_unit_is_known(ROBO_MOUSE_SCROLL_UNIT_LINE), 1);
    ROBO_CHECK_EQ_INT(robo_input_scroll_unit_is_known(ROBO_MOUSE_SCROLL_UNIT_PIXEL), 1);
    ROBO_CHECK_EQ_INT(robo_input_scroll_unit_is_known((robo_mouse_scroll_unit_t)7), 0);
    ROBO_CHECK_STR_EQ(robo_input_scroll_unit_name(ROBO_MOUSE_SCROLL_UNIT_LINE), "line");
    ROBO_CHECK_STR_EQ(robo_input_scroll_unit_name(ROBO_MOUSE_SCROLL_UNIT_PIXEL), "pixel");
    ROBO_CHECK_STR_EQ(robo_input_scroll_unit_name((robo_mouse_scroll_unit_t)7), "unknown");
}

static void test_pointer_validation(void)
{
    robo_input_pointer_data_t data = input_pointer_data(5, 6, ROBO_MOUSE_BUTTON_NONE, 0u);

    ROBO_CHECK(robo_input_pointer_data_is_valid(&data));
    ROBO_CHECK(!robo_input_pointer_data_is_valid(NULL));

    /* A mask bit for a button that does not exist -- including "none" and "unknown", whose identities are
     * 0 and 6 and would be bits 0 and 5 if the mapping were taken literally. */
    data.buttons = (uint32_t)(1u << 5);
    ROBO_CHECK(!robo_input_pointer_data_is_valid(&data));
    data.buttons = 0u;

    /* An unknown button. */
    data.button = ROBO_MOUSE_BUTTON_UNKNOWN;
    ROBO_CHECK(!robo_input_pointer_data_is_valid(&data));
    data.button = ROBO_MOUSE_BUTTON_NONE;

    /* A unit this build does not define, a negative click count, an undefined flag, and a `has_previous`
     * that is neither yes nor no. */
    data.wheel.unit = (robo_mouse_scroll_unit_t)9;
    ROBO_CHECK(!robo_input_pointer_data_is_valid(&data));
    data.wheel.unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
    data.click_count = -1;
    ROBO_CHECK(!robo_input_pointer_data_is_valid(&data));
    data.click_count = 0;
    data.flags = (uint32_t)(1u << 20);
    ROBO_CHECK(!robo_input_pointer_data_is_valid(&data));
    data.flags = 0u;
    data.has_previous = 2;
    ROBO_CHECK(!robo_input_pointer_data_is_valid(&data));
    data.has_previous = 1;
    ROBO_CHECK(robo_input_pointer_data_is_valid(&data));
    data.has_previous = 0;

    /* A cleared payload is valid: it describes a pointer that has not been heard from, which is a fact. */
    robo_input_pointer_data_init(&data);
    ROBO_CHECK(robo_input_pointer_data_is_valid(&data));

    /* The payload does *not* judge the deltas: a wheel that scrolled nowhere is something a high-resolution
     * device reports, and refusing it would drop a real event. */
    data.wheel.delta_x = 0;
    data.wheel.delta_y = 0;
    ROBO_CHECK(robo_input_pointer_data_is_valid(&data));
}

static void test_pointer_buttons(void)
{
    /* The mapping, pinned: bit `n - 1` for button `n`, and nothing for the two identities that are not
     * buttons. Everything else in this module reads the mask through these doors. */
    ROBO_CHECK_EQ_INT(robo_input_button_mask(ROBO_MOUSE_BUTTON_LEFT), 1u);
    ROBO_CHECK_EQ_INT(robo_input_button_mask(ROBO_MOUSE_BUTTON_RIGHT), 2u);
    ROBO_CHECK_EQ_INT(robo_input_button_mask(ROBO_MOUSE_BUTTON_MIDDLE), 4u);
    ROBO_CHECK_EQ_INT(robo_input_button_mask(ROBO_MOUSE_BUTTON_X1), 8u);
    ROBO_CHECK_EQ_INT(robo_input_button_mask(ROBO_MOUSE_BUTTON_X2), 16u);
    ROBO_CHECK_EQ_INT(robo_input_button_mask(ROBO_MOUSE_BUTTON_NONE), 0u);
    ROBO_CHECK_EQ_INT(robo_input_button_mask(ROBO_MOUSE_BUTTON_UNKNOWN), 0u);

    /* Set, clear, query. */
    {
        uint32_t mask = 0u;

        ROBO_CHECK(!robo_input_button_mask_has(mask, ROBO_MOUSE_BUTTON_LEFT));
        mask = robo_input_button_mask_apply(mask, ROBO_MOUSE_BUTTON_LEFT, 1);
        mask = robo_input_button_mask_apply(mask, ROBO_MOUSE_BUTTON_X1, 1);
        ROBO_CHECK_EQ_INT(mask, 9u);
        ROBO_CHECK(robo_input_button_mask_has(mask, ROBO_MOUSE_BUTTON_LEFT));
        ROBO_CHECK(robo_input_button_mask_has(mask, ROBO_MOUSE_BUTTON_X1));
        ROBO_CHECK(!robo_input_button_mask_has(mask, ROBO_MOUSE_BUTTON_RIGHT));
        mask = robo_input_button_mask_apply(mask, ROBO_MOUSE_BUTTON_LEFT, 0);
        ROBO_CHECK_EQ_INT(mask, 8u);
        ROBO_CHECK(!robo_input_button_mask_has(mask, ROBO_MOUSE_BUTTON_LEFT));

        /* Applying a non-button leaves the mask alone rather than clearing a bit that is not there. */
        ROBO_CHECK_EQ_INT(robo_input_button_mask_apply(mask, ROBO_MOUSE_BUTTON_NONE, 1), mask);
        ROBO_CHECK_EQ_INT(robo_input_button_mask_apply(mask, ROBO_MOUSE_BUTTON_UNKNOWN, 1), mask);
        ROBO_CHECK(!robo_input_button_mask_has(mask, ROBO_MOUSE_BUTTON_NONE));
    }

    /* The text form a debug line reads, from nothing to everything. */
    {
        char buffer[64];
        size_t length = 0u;
        uint32_t all = 0u;
        int index;

        for (index = (int)ROBO_MOUSE_BUTTON_LEFT; index <= (int)ROBO_MOUSE_BUTTON_X2; index++) {
            all = robo_input_button_mask_apply(all, (robo_mouse_button_t)index, 1);
        }

        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(0u, buffer, sizeof(buffer), &length), ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, "none");
        ROBO_CHECK_EQ_INT((int)length, 4);
        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(robo_input_button_mask(ROBO_MOUSE_BUTTON_LEFT),
                                                      buffer, sizeof(buffer), &length),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, "left");
        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(3u, buffer, sizeof(buffer), &length), ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, "left+right");
        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(all, buffer, sizeof(buffer), &length), ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, "left+right+middle+x1+x2");

        /* A buffer too small refuses and reports what it needed, leaving a `?` rather than a cut-off list. */
        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(all, buffer, 4u, &length), ROBO_NOT_SUPPORTED);
        ROBO_CHECK_STR_EQ(buffer, "?");
        ROBO_CHECK_EQ_INT((int)length, 23);
        ROBO_CHECK_EQ_INT((int)strlen("left+right+middle+x1+x2"), 23);
        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(0u, NULL, sizeof(buffer), &length),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(0u, buffer, sizeof(buffer), NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_input_button_mask_write(0u, buffer, 0u, &length), ROBO_INVALID_ARGUMENT);
    }
}

void robo_test_register_input_pointer(void)
{
    robo_test_register("input.pointer.payload", test_pointer_payload);
    robo_test_register("input.pointer.validation", test_pointer_validation);
    robo_test_register("input.pointer.buttons", test_pointer_buttons);
}
