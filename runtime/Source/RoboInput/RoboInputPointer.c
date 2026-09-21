/*
 * RoboInput pointer payload (Phase 12.1) — see RoboInputPointer.h.
 *
 * A payload is a *record of a place and a button state*, and the two facts it carries that a consumer
 * cannot recompute are the ones this file takes care of: the pressed-button **mask** (which must never
 * claim a button that does not exist) and the **previous** position (which must not be invented when the
 * platform did not report one). Everything else is a copy with a rule attached.
 */

#include "RoboInput/RoboInputPointer.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowMouse.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Wheel                                                               */
/* ------------------------------------------------------------------ */

void robo_input_wheel_data_init(robo_input_wheel_data_t *wheel)
{
    if (!wheel) {
        return;
    }
    wheel->delta_x = 0;
    wheel->delta_y = 0;
    wheel->unit = ROBO_MOUSE_SCROLL_UNIT_LINE;
}

int robo_input_scroll_unit_is_known(robo_mouse_scroll_unit_t unit)
{
    return unit == ROBO_MOUSE_SCROLL_UNIT_LINE || unit == ROBO_MOUSE_SCROLL_UNIT_PIXEL;
}

const char *robo_input_scroll_unit_name(robo_mouse_scroll_unit_t unit)
{
    switch (unit) {
    case ROBO_MOUSE_SCROLL_UNIT_LINE:
        return "line";
    case ROBO_MOUSE_SCROLL_UNIT_PIXEL:
        return "pixel";
    default:
        break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Button masks                                                        */
/* ------------------------------------------------------------------ */

uint32_t robo_input_button_mask(robo_mouse_button_t button)
{
    if (!robo_mouse_button_is_known(button)) {
        return 0u;
    }
    /* Button identities start at `LEFT == 1` and the mask is per *bit*, so the mapping is `1 << (button - 1)`
     * and nothing else in this module is allowed to write it. */
    return (uint32_t)1u << (uint32_t)((int)button - 1);
}

int robo_input_button_mask_has(uint32_t mask, robo_mouse_button_t button)
{
    uint32_t bit = robo_input_button_mask(button);

    return bit != 0u && (mask & bit) != 0u;
}

uint32_t robo_input_button_mask_apply(uint32_t mask, robo_mouse_button_t button, int pressed)
{
    uint32_t bit = robo_input_button_mask(button);

    if (bit == 0u) {
        return mask;
    }
    return pressed ? (mask | bit) : (mask & ~bit);
}

/**
 * Every bit that stands for a button this build has.
 *
 * Written from the identities rather than as a literal so that a future button added to RoboWindow's enum is
 * included here by the loop in `robo_input_button_mask` and nowhere else.
 */
static uint32_t defined_button_mask(void)
{
    uint32_t defined = 0u;
    int button;

    for (button = (int)ROBO_MOUSE_BUTTON_LEFT; button <= (int)ROBO_MOUSE_BUTTON_X2; button++) {
        defined |= robo_input_button_mask((robo_mouse_button_t)button);
    }
    return defined;
}

robo_result_t robo_input_button_mask_write(uint32_t mask, char *buffer, size_t capacity, size_t *out_length)
{
    robo_input_writer_t text;
    int written = 0;
    int button;

    if (!buffer || capacity == 0u || !out_length) {
        return ROBO_INVALID_ARGUMENT;
    }

    ri_writer_init(&text, buffer, capacity);
    for (button = (int)ROBO_MOUSE_BUTTON_LEFT; button <= (int)ROBO_MOUSE_BUTTON_X2; button++) {
        if ((mask & robo_input_button_mask((robo_mouse_button_t)button)) == 0u) {
            continue;
        }
        if (written) {
            ri_writer_char(&text, '+');
        }
        ri_writer_text(&text, robo_mouse_button_name((robo_mouse_button_t)button));
        written = 1;
    }
    if (!written) {
        /* Lower case, like every other empty list in this module's dumps (`modifiers=none`, `flags=none`,
         * `previous=none`) and like the button names themselves, which are RoboWindow's. */
        ri_writer_text(&text, "none");
    }
    return ri_writer_finish(&text, out_length, NULL);
}

/* ------------------------------------------------------------------ */
/* Pointer data                                                        */
/* ------------------------------------------------------------------ */

void robo_input_pointer_data_init(robo_input_pointer_data_t *data)
{
    if (!data) {
        return;
    }
    data->pointer = ROBO_INPUT_POINTER_ID_PRIMARY;
    data->button = ROBO_MOUSE_BUTTON_NONE;
    data->buttons = 0u;
    data->position = robo_gfx_point(0, 0);
    data->previous = robo_gfx_point(0, 0);
    data->has_previous = 0;
    data->delta_x = 0;
    data->delta_y = 0;
    data->click_count = 0;
    robo_input_wheel_data_init(&data->wheel);
    /* A cleared pointer is the primary one, because that is what a click with no pointer id means: an
     * event that forgot to name its pointer names the mouse rather than nothing. */
    data->flags = ROBO_INPUT_POINTER_FLAG_PRIMARY;
}

robo_result_t robo_input_pointer_data_set(robo_input_pointer_data_t *data,
                                          robo_input_pointer_id_t pointer,
                                          robo_gfx_point_t position,
                                          robo_mouse_button_t button,
                                          uint32_t buttons)
{
    if (!data) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (button != ROBO_MOUSE_BUTTON_NONE && !robo_mouse_button_is_known(button)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if ((buttons & ~defined_button_mask()) != 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    data->pointer = pointer;
    data->position = position;
    data->button = button;
    data->buttons = buttons;
    return ROBO_OK;
}

robo_result_t robo_input_pointer_data_set_previous(robo_input_pointer_data_t *data,
                                                   robo_gfx_point_t previous)
{
    int64_t dx;
    int64_t dy;

    if (!data) {
        return ROBO_INVALID_ARGUMENT;
    }
    dx = (int64_t)data->position.x - (int64_t)previous.x;
    dy = (int64_t)data->position.y - (int64_t)previous.y;
    /* Two representable positions can differ by more than a delta can hold. That is *refused* rather than
     * truncated, because a wrapped delta is a movement in the wrong direction -- and refused before anything
     * is written, so a caller that gets an error still holds a coherent record. */
    if (dx < (int64_t)INT32_MIN || dx > (int64_t)INT32_MAX || dy < (int64_t)INT32_MIN ||
        dy > (int64_t)INT32_MAX) {
        return ROBO_ERROR;
    }
    data->previous = previous;
    data->has_previous = 1;
    /* The delta is the *derived* form here, so both fields answer the same question rather than one being
     * derived and the other remembered. */
    data->delta_x = (int32_t)dx;
    data->delta_y = (int32_t)dy;
    return ROBO_OK;
}

robo_result_t robo_input_pointer_data_set_wheel(robo_input_pointer_data_t *data,
                                                int32_t delta_x,
                                                int32_t delta_y,
                                                robo_mouse_scroll_unit_t unit)
{
    if (!data) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_input_scroll_unit_is_known(unit)) {
        return ROBO_INVALID_ARGUMENT;
    }
    data->wheel.delta_x = delta_x;
    data->wheel.delta_y = delta_y;
    data->wheel.unit = unit;
    return ROBO_OK;
}

int robo_input_pointer_data_is_valid(const robo_input_pointer_data_t *data)
{
    if (!data) {
        return 0;
    }
    if (data->button != ROBO_MOUSE_BUTTON_NONE && !robo_mouse_button_is_known(data->button)) {
        return 0;
    }
    /* The mask may carry only bits that stand for a real button: a bit for "none" or "unknown" would be an
     * event claiming a button no screen has. */
    if ((data->buttons & ~defined_button_mask()) != 0u) {
        return 0;
    }
    if (!robo_input_scroll_unit_is_known(data->wheel.unit)) {
        return 0;
    }
    if (data->click_count < 0) {
        return 0;
    }
    if ((data->flags & ~ROBO_INPUT_POINTER_FLAG_ALL) != 0u) {
        return 0;
    }
    if (data->has_previous != 0 && data->has_previous != 1) {
        return 0;
    }
    return 1;
}
