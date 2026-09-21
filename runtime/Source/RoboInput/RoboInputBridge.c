/*
 * RoboInput bridge (Phase 12.1) — see RoboInputBridge.h.
 *
 * The one direction this build can travel: a `robo_window_event_t` -- what RoboWindow's backends already
 * produce -- becomes a RoboInput event. Nothing here reads a device, opens a handle, or remembers a previous
 * event, so the translation is a pure function of the window event and the options a caller passes.
 *
 * Two things are *decided* rather than copied, and both are in the header's comment because a reader needs
 * them before trusting the output:
 *
 *   **which plane a pointer event lands in** -- RoboWindow's payload carries client and screen coordinates
 *   with a flag saying whether the second is real, and this model carries one plane per event, so the bridge
 *   takes the more precise plane that exists and says so in the event's `space`;
 *
 *   **whether a previous position exists** -- RoboWindow's delta is zero both for "did not move" and for
 *   "first sample", so `previous` is filled only where the arithmetic is provably right.
 *
 * The pressed-button mask is the third: it comes from the runtime's tracked button state rather than from
 * the event alone, because a move carries no mask and "nothing is down" would be a lie a consumer could not
 * detect. The event's own button is then applied on top, so the mask is coherent with the event even if the
 * window layer's state changed after the event was queued.
 */

#include "RoboInput/RoboInputBridge.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowMouse.h"

#include <stddef.h>
#include <stdint.h>

void robo_input_bridge_options_init(robo_input_bridge_options_t *options)
{
    if (!options) {
        return;
    }
    options->mouse_source = ROBO_INPUT_SOURCE_MOUSE;
    options->keyboard_source = ROBO_INPUT_SOURCE_KEYBOARD;
    options->prefer_screen_coords = 1;
    robo_input_device_init(&options->mouse_device);
    robo_input_device_init(&options->keyboard_device);
}

int robo_input_bridge_can_translate(robo_window_event_type_t type)
{
    return robo_input_event_type_for_window_event((int)type) != ROBO_INPUT_EVENT_NONE;
}

robo_input_space_t robo_input_bridge_space_for_window_mouse(const robo_mouse_event_t *source,
                                                            int prefer_screen_coords)
{
    if (!source) {
        return ROBO_INPUT_SPACE_UNKNOWN;
    }
    if (prefer_screen_coords && source->has_screen_coords) {
        return ROBO_INPUT_SPACE_SCREEN;
    }
    /* Client coordinates are this module's *document* plane: the plane the window's content is laid out
     * and painted on, which is the one a hit test wants. */
    return ROBO_INPUT_SPACE_DOCUMENT;
}

/* ------------------------------------------------------------------ */
/* Payloads                                                            */
/* ------------------------------------------------------------------ */

robo_result_t robo_input_keyboard_data_from_window_keyboard(const robo_keyboard_event_t *source,
                                                            robo_input_keyboard_data_t *out,
                                                            robo_input_diag_t *diag)
{
    robo_result_t r;

    if (!source || !out) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_INVALID_ARGUMENT,
                               "a keyboard translation needs a payload and somewhere to put it");
    }
    if (source->action != ROBO_KEY_ACTION_DOWN && source->action != ROBO_KEY_ACTION_UP) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                               "the window key payload has no action this model can carry");
    }
    /* The text is deliberately absent: RoboWindow's payload has no text at all, and producing text is a
     * later phase's work. A bridged key event therefore says "this key did this", not "this character was
     * typed", which is exactly what it knows. */
    r = robo_input_keyboard_data_set(out, source->physical_key, source->logical_key, source->action,
                                     source->is_repeat, NULL);
    if (r != ROBO_OK) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, r,
                               "the window key payload is not one this model may carry");
    }
    return ROBO_OK;
}

/** Non-zero when the runtime currently tracks @p button as pressed (RoboWindow's own state, 7.11). */
static int tracked_button(int button)
{
    return robo_mouse_is_button_pressed((robo_mouse_button_t)button) != 0;
}

robo_result_t robo_input_pointer_data_from_window_mouse(const robo_mouse_event_t *source,
                                                        int prefer_screen_coords,
                                                        robo_input_pointer_data_t *out,
                                                        robo_input_space_t *out_space,
                                                        robo_input_diag_t *diag)
{
    robo_input_space_t space;
    robo_gfx_point_t position;
    robo_mouse_button_t button = ROBO_MOUSE_BUTTON_NONE;
    uint32_t mask = 0u;
    int index;

    if (!source || !out || !out_space) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_INVALID_ARGUMENT,
                               "a pointer translation needs a payload and somewhere to put it");
    }
    switch (source->type) {
    case ROBO_MOUSE_EVENT_MOVE:
        break;
    case ROBO_MOUSE_EVENT_BUTTON_DOWN:
        button = source->button;
        break;
    case ROBO_MOUSE_EVENT_BUTTON_UP:
        button = source->button;
        break;
    case ROBO_MOUSE_EVENT_WHEEL:
        if (!robo_input_scroll_unit_is_known(source->scroll_unit)) {
            return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                                   "the window wheel payload has no unit this model can carry");
        }
        break;
    default:
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                               "the window mouse payload has no type this model can carry");
    }
    if ((source->type == ROBO_MOUSE_EVENT_BUTTON_DOWN || source->type == ROBO_MOUSE_EVENT_BUTTON_UP) &&
        (button == ROBO_MOUSE_BUTTON_NONE || button == ROBO_MOUSE_BUTTON_UNKNOWN ||
         !robo_mouse_button_is_known(button))) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                               "a button event without a button this model can name");
    }

    space = robo_input_bridge_space_for_window_mouse(source, prefer_screen_coords);
    if (space == ROBO_INPUT_SPACE_SCREEN) {
        position = robo_gfx_point(source->screen_x, source->screen_y);
    } else {
        position = robo_gfx_point(source->client_x, source->client_y);
    }

    /* What is down right now, per the window layer's own tracking, then the event's own button applied on
     * top so that the mask cannot contradict the event it is on. */
    for (index = (int)ROBO_MOUSE_BUTTON_LEFT; index <= (int)ROBO_MOUSE_BUTTON_X2; index++) {
        if (tracked_button(index)) {
            mask = robo_input_button_mask_apply(mask, (robo_mouse_button_t)index, 1);
        }
    }
    if (source->type == ROBO_MOUSE_EVENT_BUTTON_DOWN) {
        mask = robo_input_button_mask_apply(mask, button, 1);
    } else if (source->type == ROBO_MOUSE_EVENT_BUTTON_UP) {
        mask = robo_input_button_mask_apply(mask, button, 0);
    }

    robo_input_pointer_data_init(out);
    out->pointer = ROBO_INPUT_POINTER_ID_PRIMARY;
    out->click_count = source->click_count;
    if (robo_input_pointer_data_set(out, ROBO_INPUT_POINTER_ID_PRIMARY, position, button, mask) != ROBO_OK) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_ERROR,
                               "the translated pointer payload is not one this model may carry");
    }
    if (source->type == ROBO_MOUSE_EVENT_WHEEL) {
        if (robo_input_pointer_data_set_wheel(out, source->scroll_x, source->scroll_y,
                                              source->scroll_unit) != ROBO_OK) {
            return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                                   "the window wheel payload has no unit this model can carry");
        }
    }
    if (source->delta_x != 0 || source->delta_y != 0) {
        /* A non-zero delta is proof that a previous position exists, and the previous position is that
         * delta back along the movement. A *zero* delta cannot be told apart from a first sample, so no
         * previous position is claimed. */
        robo_gfx_point_t previous = robo_gfx_point(position.x - source->delta_x, position.y - source->delta_y);

        if (robo_input_pointer_data_set_previous(out, previous) != ROBO_OK) {
            return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_ERROR,
                                   "the translated previous position is not representable");
        }
    }
    if (!robo_input_pointer_data_is_valid(out)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_ERROR,
                               "the translated pointer payload is not one this model may carry");
    }
    *out_space = space;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Whole events                                                        */
/* ------------------------------------------------------------------ */

/** Non-zero when @p device carries anything at all: an id, or a name. */
static int device_is_filled(const robo_input_device_t *device)
{
    return device->id != ROBO_INPUT_DEVICE_ID_NONE || device->name[0] != '\0';
}

robo_result_t robo_input_event_from_window_event_with_options(
    const robo_input_bridge_options_t *options,
    const robo_window_event_t *window_event,
    robo_input_event_t *out,
    robo_input_diag_t *diag)
{
    robo_input_event_type_t type;
    robo_input_source_t source;
    robo_window_id_t window;
    uint64_t timestamp;
    robo_input_modifiers_t modifiers;
    robo_result_t r;

    if (!window_event || !out) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_INVALID_ARGUMENT,
                               "a translation needs a window event and somewhere to put the result");
    }
    type = robo_input_event_type_for_window_event((int)window_event->type);
    if (type == ROBO_INPUT_EVENT_NONE) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                               "that window event is not an input event");
    }

    robo_input_event_init(out);
    out->type = type;
    window = window_event->window_id;
    out->display = window_event->display_id;

    if (robo_input_event_type_is_key(type)) {
        robo_input_keyboard_data_t data;

        source = options ? options->keyboard_source : ROBO_INPUT_SOURCE_KEYBOARD;
        if (window == ROBO_WINDOW_ID_NONE) {
            window = window_event->key.window_id;
        }
        timestamp = window_event->key.timestamp_ms;
        modifiers = window_event->key.modifiers;
        if (robo_input_keyboard_action_for_event((int)type) != window_event->key.action) {
            return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                                   "the window key payload's action disagrees with its event type");
        }
        r = robo_input_keyboard_data_from_window_keyboard(&window_event->key, &data, diag);
        if (r != ROBO_OK) {
            return r;
        }
        r = robo_input_event_set_keyboard(out, type, source, &data, diag);
        if (r != ROBO_OK) {
            return r;
        }
        if (options && device_is_filled(&options->keyboard_device)) {
            r = robo_input_event_set_device(out, &options->keyboard_device, diag);
            if (r != ROBO_OK) {
                return r;
            }
        }
    } else {
        robo_input_pointer_data_t data;
        robo_input_space_t space = ROBO_INPUT_SPACE_UNKNOWN;

        source = options ? options->mouse_source : ROBO_INPUT_SOURCE_MOUSE;
        if (window == ROBO_WINDOW_ID_NONE) {
            window = window_event->mouse.window_id;
        }
        timestamp = window_event->mouse.timestamp_ms;
        modifiers = window_event->mouse.modifiers;
        r = robo_input_pointer_data_from_window_mouse(&window_event->mouse,
                                                      options ? options->prefer_screen_coords : 1, &data,
                                                      &space, diag);
        if (r != ROBO_OK) {
            return r;
        }
        r = robo_input_event_set_pointer(out, type, source, &data, space, diag);
        if (r != ROBO_OK) {
            return r;
        }
        if (options && device_is_filled(&options->mouse_device)) {
            r = robo_input_event_set_device(out, &options->mouse_device, diag);
            if (r != ROBO_OK) {
                return r;
            }
        }
    }

    out->window = window;
    out->timestamp_ms = timestamp;
    /* A modifier state the window layer filled with bits this build does not define would make the whole
     * event invalid, so it is checked here rather than left to fail later at the queue. */
    if (!robo_input_modifiers_are_known(modifiers)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_BRIDGE, ROBO_NOT_SUPPORTED,
                               "the window payload's modifier state has bits this build does not define");
    }
    out->modifiers = modifiers;
    return ROBO_OK;
}

robo_result_t robo_input_event_from_window_event(const robo_window_event_t *window_event,
                                                 robo_input_event_t *out,
                                                 robo_input_diag_t *diag)
{
    return robo_input_event_from_window_event_with_options(NULL, window_event, out, diag);
}

robo_result_t robo_input_queue_push_window_event(robo_input_queue_t *queue,
                                                 const robo_input_bridge_options_t *options,
                                                 const robo_window_event_t *window_event,
                                                 int *out_translated,
                                                 robo_input_diag_t *diag)
{
    robo_input_event_t event;
    robo_result_t r;

    if (out_translated) {
        *out_translated = 0;
    }
    r = robo_input_event_from_window_event_with_options(options, window_event, &event, diag);
    if (r != ROBO_OK) {
        return r;
    }
    r = robo_input_queue_push(queue, &event, diag);
    if (r != ROBO_OK) {
        return r;
    }
    if (out_translated) {
        *out_translated = 1;
    }
    return ROBO_OK;
}
