/*
 * RoboInput keyboard payload (Phase 12.1) — see RoboInputKeyboard.h.
 *
 * The payload is a record of what a key *did*, using RoboWindow's own two key vocabularies and adding only
 * what those vocabularies cannot carry: the action, the repeat flag, and the text. Two facts are derived
 * rather than demanded of the caller -- `PHYSICAL_UNKNOWN` from the key identity, and which keys are
 * modifiers -- because a caller that had to remember them would be a caller that could forget them, and the
 * dump would then print a key as mapped when it is not.
 */

#include "RoboInput/RoboInputKeyboard.h"

#include "RoboInput/RoboInputEvent.h"
#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowKey.h"

#include <stddef.h>
#include <stdint.h>

void robo_input_keyboard_data_init(robo_input_keyboard_data_t *data)
{
    if (!data) {
        return;
    }
    data->physical = ROBO_PHYSICAL_KEY_UNKNOWN;
    data->logical = ROBO_LOGICAL_KEY_UNKNOWN;
    data->action = ROBO_KEY_ACTION_NONE;
    data->repeat = 0;
    data->scan_code = 0u;
    data->flags = 0u;
    data->text[0] = '\0';
}

/** Non-zero for the physical keys on the numeric keypad, which a consumer may want to distinguish. */
static int physical_key_is_numpad(robo_physical_key_t physical)
{
    return physical >= ROBO_PHYSICAL_KEY_NUMPAD_DIVIDE && physical <= ROBO_PHYSICAL_KEY_NUMPAD_DECIMAL;
}

int robo_input_physical_key_is_modifier(robo_physical_key_t physical)
{
    return robo_input_physical_key_modifier(physical) != ROBO_KEY_MOD_NONE;
}

robo_key_modifier_flags_t robo_input_physical_key_modifier(robo_physical_key_t physical)
{
    switch (physical) {
    case ROBO_PHYSICAL_KEY_SHIFT_LEFT:
        return ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_SHIFT_LEFT;
    case ROBO_PHYSICAL_KEY_SHIFT_RIGHT:
        return ROBO_KEY_MOD_SHIFT | ROBO_KEY_MOD_SHIFT_RIGHT;
    case ROBO_PHYSICAL_KEY_CONTROL_LEFT:
        return ROBO_KEY_MOD_CTRL | ROBO_KEY_MOD_CTRL_LEFT;
    case ROBO_PHYSICAL_KEY_CONTROL_RIGHT:
        return ROBO_KEY_MOD_CTRL | ROBO_KEY_MOD_CTRL_RIGHT;
    case ROBO_PHYSICAL_KEY_ALT_LEFT:
        return ROBO_KEY_MOD_ALT | ROBO_KEY_MOD_ALT_LEFT;
    case ROBO_PHYSICAL_KEY_ALT_RIGHT:
        return ROBO_KEY_MOD_ALT | ROBO_KEY_MOD_ALT_RIGHT;
    case ROBO_PHYSICAL_KEY_META_LEFT:
        return ROBO_KEY_MOD_META | ROBO_KEY_MOD_META_LEFT;
    case ROBO_PHYSICAL_KEY_META_RIGHT:
        return ROBO_KEY_MOD_META | ROBO_KEY_MOD_META_RIGHT;
    case ROBO_PHYSICAL_KEY_CAPS_LOCK:
        return ROBO_KEY_MOD_CAPS_LOCK;
    case ROBO_PHYSICAL_KEY_NUM_LOCK:
        return ROBO_KEY_MOD_NUM_LOCK;
    case ROBO_PHYSICAL_KEY_SCROLL_LOCK:
        return ROBO_KEY_MOD_SCROLL_LOCK;
    default:
        break;
    }
    return ROBO_KEY_MOD_NONE;
}

robo_result_t robo_input_keyboard_data_set(robo_input_keyboard_data_t *data,
                                           robo_physical_key_t physical,
                                           robo_logical_key_t logical,
                                           robo_key_action_t action,
                                           int repeat,
                                           const char *text)
{
    if (!data) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Cleared *first*, so that every refusal below leaves an empty record rather than the caller's previous
     * one: a payload that half-describes the refused call and half-describes the last accepted one is the
     * one outcome nobody can reason about. The same rule `robo_input_device_set` and the target doors use. */
    robo_input_keyboard_data_init(data);
    if (action != ROBO_KEY_ACTION_DOWN && action != ROBO_KEY_ACTION_UP) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (physical != ROBO_PHYSICAL_KEY_UNKNOWN && !robo_physical_key_is_known(physical)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (logical != ROBO_LOGICAL_KEY_UNKNOWN && !robo_logical_key_is_known(logical)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!ri_text_fits(text, ROBO_INPUT_TEXT_MAX)) {
        robo_log_debug("robo_input: key text rejected, longer than %u bytes",
                       (unsigned)ROBO_INPUT_TEXT_MAX);
        return ROBO_INVALID_ARGUMENT;
    }

    data->physical = physical;
    data->logical = logical;
    data->action = action;
    /* A release is not a repeat, whatever the caller says: normalizing here means the rule lives in one
     * place rather than in every consumer. */
    data->repeat = (action == ROBO_KEY_ACTION_UP) ? 0 : (repeat ? 1 : 0);
    if (physical == ROBO_PHYSICAL_KEY_UNKNOWN) {
        data->flags |= ROBO_INPUT_KEY_FLAG_PHYSICAL_UNKNOWN;
    }
    if (robo_input_physical_key_is_modifier(physical)) {
        data->flags |= ROBO_INPUT_KEY_FLAG_MODIFIER;
    }
    if (physical_key_is_numpad(physical)) {
        data->flags |= ROBO_INPUT_KEY_FLAG_NUMPAD;
    }
    ri_text_copy(data->text, ROBO_INPUT_TEXT_MAX, text);
    return ROBO_OK;
}

int robo_input_keyboard_data_is_valid(const robo_input_keyboard_data_t *data)
{
    if (!data) {
        return 0;
    }
    if (data->action != ROBO_KEY_ACTION_DOWN && data->action != ROBO_KEY_ACTION_UP) {
        return 0;
    }
    if (data->physical != ROBO_PHYSICAL_KEY_UNKNOWN && !robo_physical_key_is_known(data->physical)) {
        return 0;
    }
    if (data->logical != ROBO_LOGICAL_KEY_UNKNOWN && !robo_logical_key_is_known(data->logical)) {
        return 0;
    }
    if ((data->flags & ~ROBO_INPUT_KEY_FLAG_ALL) != 0u) {
        return 0;
    }
    /* The flag and the identity have to agree in both directions: a key with no physical identity that does
     * not say so is a record a consumer would read as "this key is A". */
    if (((data->flags & ROBO_INPUT_KEY_FLAG_PHYSICAL_UNKNOWN) != 0u) !=
        (data->physical == ROBO_PHYSICAL_KEY_UNKNOWN)) {
        return 0;
    }
    if (data->repeat != 0 && data->repeat != 1) {
        return 0;
    }
    if (data->action == ROBO_KEY_ACTION_UP && data->text[0] != '\0') {
        return 0;
    }
    return ri_bounded_strlen(data->text, ROBO_INPUT_TEXT_MAX) < ROBO_INPUT_TEXT_MAX;
}

robo_key_action_t robo_input_keyboard_action_for_event(int event_type)
{
    switch ((robo_input_event_type_t)event_type) {
    case ROBO_INPUT_EVENT_KEY_DOWN:
        return ROBO_KEY_ACTION_DOWN;
    case ROBO_INPUT_EVENT_KEY_UP:
        return ROBO_KEY_ACTION_UP;
    default:
        break;
    }
    return ROBO_KEY_ACTION_NONE;
}

int robo_input_keyboard_data_action_matches(const robo_input_keyboard_data_t *data, int event_type)
{
    robo_key_action_t action;

    if (!data) {
        return 0;
    }
    action = robo_input_keyboard_action_for_event(event_type);
    return action != ROBO_KEY_ACTION_NONE && data->action == action;
}

int robo_input_keyboard_data_equal(const robo_input_keyboard_data_t *a,
                                   const robo_input_keyboard_data_t *b)
{
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->physical != b->physical || a->logical != b->logical || a->action != b->action ||
        a->repeat != b->repeat || a->scan_code != b->scan_code || a->flags != b->flags) {
        return 0;
    }
    for (index = 0u; index < ROBO_INPUT_TEXT_MAX; index++) {
        if (a->text[index] != b->text[index]) {
            return 0;
        }
    }
    return 1;
}

size_t robo_input_keyboard_data_text(const robo_input_keyboard_data_t *data,
                                     char *out,
                                     size_t capacity)
{
    size_t length;

    if (!out || capacity == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (!data) {
        return 0u;
    }
    length = ri_bounded_strlen(data->text, ROBO_INPUT_TEXT_MAX);
    ri_text_copy(out, capacity, data->text);
    if (length >= capacity) {
        return capacity - 1u;
    }
    return length;
}
