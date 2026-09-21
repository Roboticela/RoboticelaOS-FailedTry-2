/*
 * RoboInput fundamental types (Phase 12.1) — see RoboInputTypes.h.
 *
 * Everything in this file is a *query*, a *copy*, or a name. That is the whole content of the module's
 * bottom layer: a source is a number with a name, a device is a copied record with two rules about what may
 * be in it, and the modifiers are RoboWindow's own bit type with the questions a handler asks. Nothing here
 * allocates, caches, or remembers anything between calls, which is what lets every door be called from any
 * thread on any record.
 */

#include "RoboInput/RoboInputTypes.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowEvent.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Sources                                                             */
/* ------------------------------------------------------------------ */

int robo_input_source_is_known(robo_input_source_t source)
{
    return source > ROBO_INPUT_SOURCE_UNKNOWN && source < (robo_input_source_t)ROBO_INPUT_SOURCE_COUNT;
}

const char *robo_input_source_name(robo_input_source_t source)
{
    switch (source) {
    case ROBO_INPUT_SOURCE_UNKNOWN:
        return "unknown";
    case ROBO_INPUT_SOURCE_KEYBOARD:
        return "keyboard";
    case ROBO_INPUT_SOURCE_MOUSE:
        return "mouse";
    case ROBO_INPUT_SOURCE_POINTER:
        return "pointer";
    case ROBO_INPUT_SOURCE_TOUCH:
        return "touch";
    case ROBO_INPUT_SOURCE_TABLET:
        return "tablet";
    case ROBO_INPUT_SOURCE_GAME_CONTROLLER:
        return "game-controller";
    case ROBO_INPUT_SOURCE_REMOTE:
        return "remote";
    case ROBO_INPUT_SOURCE_VIRTUAL:
        return "virtual";
    default:
        break;
    }
    return "unknown";
}

int robo_input_source_is_positioning(robo_input_source_t source)
{
    switch (source) {
    case ROBO_INPUT_SOURCE_MOUSE:
    case ROBO_INPUT_SOURCE_POINTER:
    case ROBO_INPUT_SOURCE_TOUCH:
    case ROBO_INPUT_SOURCE_TABLET:
    case ROBO_INPUT_SOURCE_GAME_CONTROLLER:
    case ROBO_INPUT_SOURCE_REMOTE:
    case ROBO_INPUT_SOURCE_VIRTUAL:
        return 1;
    default:
        break;
    }
    return 0;
}

robo_input_source_t robo_input_source_for_window_event(int window_event_type)
{
    switch ((robo_window_event_type_t)window_event_type) {
    case ROBO_WINDOW_EVENT_KEY_DOWN:
    case ROBO_WINDOW_EVENT_KEY_UP:
        return ROBO_INPUT_SOURCE_KEYBOARD;
    case ROBO_WINDOW_EVENT_MOUSE_MOVE:
    case ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN:
    case ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP:
    case ROBO_WINDOW_EVENT_MOUSE_WHEEL:
        return ROBO_INPUT_SOURCE_MOUSE;
    default:
        break;
    }
    return ROBO_INPUT_SOURCE_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* Devices                                                             */
/* ------------------------------------------------------------------ */

void robo_input_device_init(robo_input_device_t *device)
{
    if (!device) {
        return;
    }
    device->id = ROBO_INPUT_DEVICE_ID_NONE;
    device->source = ROBO_INPUT_SOURCE_UNKNOWN;
    device->flags = 0u;
    device->name[0] = '\0';
}

int robo_input_device_is_valid(const robo_input_device_t *device)
{
    if (!device) {
        return 0;
    }
    if (device->source != ROBO_INPUT_SOURCE_UNKNOWN && !robo_input_source_is_known(device->source)) {
        return 0;
    }
    if ((device->flags & ~ROBO_INPUT_DEVICE_FLAG_ALL) != 0u) {
        return 0;
    }
    /* A name must be terminated inside its capacity: a record whose name filled the whole array with no NUL
     * is a record a dump would read past the end of. */
    return ri_bounded_strlen(device->name, ROBO_INPUT_DEVICE_NAME_MAX) < ROBO_INPUT_DEVICE_NAME_MAX;
}

robo_result_t robo_input_device_set(robo_input_device_t *device,
                                    robo_input_device_id_t id,
                                    robo_input_source_t source,
                                    const char *name)
{
    if (!device) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (source != ROBO_INPUT_SOURCE_UNKNOWN && !robo_input_source_is_known(source)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!ri_text_fits(name, ROBO_INPUT_DEVICE_NAME_MAX)) {
        /* Cleared, not half-filled: a truncated name is a *different* device's name. */
        robo_input_device_init(device);
        robo_log_debug("robo_input: device name rejected, too long for %u bytes",
                       (unsigned)ROBO_INPUT_DEVICE_NAME_MAX);
        return ROBO_INVALID_ARGUMENT;
    }

    robo_input_device_init(device);
    device->id = id;
    device->source = source;
    ri_text_copy(device->name, ROBO_INPUT_DEVICE_NAME_MAX, name);
    return ROBO_OK;
}

size_t robo_input_device_name(const robo_input_device_t *device, char *out, size_t capacity)
{
    size_t length;

    if (!out || capacity == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (!device) {
        return 0u;
    }
    length = ri_bounded_strlen(device->name, ROBO_INPUT_DEVICE_NAME_MAX);
    ri_text_copy(out, capacity, device->name);
    if (length >= capacity) {
        return capacity - 1u;
    }
    return length;
}

int robo_input_device_equal(const robo_input_device_t *a, const robo_input_device_t *b)
{
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->id != b->id || a->source != b->source || a->flags != b->flags) {
        return 0;
    }
    for (index = 0u; index < ROBO_INPUT_DEVICE_NAME_MAX; index++) {
        if (a->name[index] != b->name[index]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Modifiers                                                           */
/* ------------------------------------------------------------------ */

int robo_input_modifiers_are_known(robo_input_modifiers_t modifiers)
{
    return (modifiers & ~((robo_input_modifiers_t)ROBO_INPUT_MODIFIER_ALL)) == 0u;
}

int robo_input_modifiers_has(robo_input_modifiers_t modifiers, robo_input_modifiers_t mask)
{
    return (modifiers & mask) == mask;
}

int robo_input_modifiers_is_aggregate(robo_input_modifiers_t modifiers)
{
    return (modifiers & (ROBO_KEY_MOD_SHIFT_LEFT | ROBO_KEY_MOD_SHIFT_RIGHT |
                         ROBO_KEY_MOD_CTRL_LEFT | ROBO_KEY_MOD_CTRL_RIGHT |
                         ROBO_KEY_MOD_ALT_LEFT | ROBO_KEY_MOD_ALT_RIGHT |
                         ROBO_KEY_MOD_META_LEFT | ROBO_KEY_MOD_META_RIGHT)) == 0u;
}

const char *robo_input_modifier_name(robo_input_modifiers_t modifier)
{
    switch (modifier) {
    case ROBO_KEY_MOD_NONE:
        return "none";
    case ROBO_KEY_MOD_SHIFT:
        return "shift";
    case ROBO_KEY_MOD_CTRL:
        return "control";
    case ROBO_KEY_MOD_ALT:
        return "alt";
    case ROBO_KEY_MOD_META:
        return "meta";
    case ROBO_KEY_MOD_CAPS_LOCK:
        return "caps-lock";
    case ROBO_KEY_MOD_NUM_LOCK:
        return "num-lock";
    case ROBO_KEY_MOD_SCROLL_LOCK:
        return "scroll-lock";
    case ROBO_KEY_MOD_SHIFT_LEFT:
        return "shift-left";
    case ROBO_KEY_MOD_SHIFT_RIGHT:
        return "shift-right";
    case ROBO_KEY_MOD_CTRL_LEFT:
        return "control-left";
    case ROBO_KEY_MOD_CTRL_RIGHT:
        return "control-right";
    case ROBO_KEY_MOD_ALT_LEFT:
        return "alt-left";
    case ROBO_KEY_MOD_ALT_RIGHT:
        return "alt-right";
    case ROBO_KEY_MOD_META_LEFT:
        return "meta-left";
    case ROBO_KEY_MOD_META_RIGHT:
        return "meta-right";
    default:
        break;
    }
    return "?";
}

/** The bits, in the order a modifier list is written: the seven categories, then the side-specific ones. */
static robo_input_modifiers_t modifier_order(size_t index)
{
    static const robo_input_modifiers_t order[] = {
        ROBO_KEY_MOD_SHIFT,       ROBO_KEY_MOD_CTRL,        ROBO_KEY_MOD_ALT,
        ROBO_KEY_MOD_META,        ROBO_KEY_MOD_CAPS_LOCK,   ROBO_KEY_MOD_NUM_LOCK,
        ROBO_KEY_MOD_SCROLL_LOCK, ROBO_KEY_MOD_SHIFT_LEFT,  ROBO_KEY_MOD_SHIFT_RIGHT,
        ROBO_KEY_MOD_CTRL_LEFT,   ROBO_KEY_MOD_CTRL_RIGHT,  ROBO_KEY_MOD_ALT_LEFT,
        ROBO_KEY_MOD_ALT_RIGHT,   ROBO_KEY_MOD_META_LEFT,   ROBO_KEY_MOD_META_RIGHT
    };

    if (index >= (sizeof(order) / sizeof(order[0]))) {
        return ROBO_KEY_MOD_NONE;
    }
    return order[index];
}

robo_result_t robo_input_modifiers_write(robo_input_modifiers_t modifiers,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *out_length)
{
    robo_input_writer_t text;
    size_t index;
    int written = 0;
    robo_input_modifiers_t state = modifiers & (robo_input_modifiers_t)ROBO_INPUT_MODIFIER_ALL;

    if (!buffer || capacity == 0u || !out_length) {
        return ROBO_INVALID_ARGUMENT;
    }

    ri_writer_init(&text, buffer, capacity);
    if (state == ROBO_KEY_MOD_NONE) {
        ri_writer_text(&text, "none");
    } else {
        for (index = 0u;; index++) {
            robo_input_modifiers_t bit = modifier_order(index);

            if (bit == ROBO_KEY_MOD_NONE) {
                break;
            }
            if ((state & bit) == 0u) {
                continue;
            }
            if (written) {
                ri_writer_char(&text, '+');
            }
            ri_writer_text(&text, robo_input_modifier_name(bit));
            written = 1;
        }
    }
    return ri_writer_finish(&text, out_length, NULL);
}
