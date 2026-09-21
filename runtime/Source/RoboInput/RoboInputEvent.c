/*
 * RoboInput event model (Phase 12.1) — see RoboInputEvent.h.
 *
 * Two things live here and nothing else: the **type vocabulary** (what a type is, which payload it owns,
 * which sources may report it, whether it has a position) and the **validator** that refuses a record whose
 * fields disagree about what it is. Every `set_*` door is a guarded assignment; the guards are the point.
 *
 * The validator is deliberately the strictest thing in the module, because an input event is the one record
 * in this runtime that is produced by hardware-adjacent code, crosses a boundary, and is then *trusted* by
 * everything above it. A rule that is not enforced here is a rule every consumer would have to re-check, and
 * the one that would be forgotten is always the same one: which plane a coordinate is on.
 */

#include "RoboInput/RoboInputEvent.h"

#include "RoboInputInternal.h"

#include "RoboWindow/RoboWindowEvent.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Text payload                                                        */
/* ------------------------------------------------------------------ */

void robo_input_text_data_init(robo_input_text_data_t *data)
{
    if (!data) {
        return;
    }
    data->text[0] = '\0';
}

robo_result_t robo_input_text_data_set(robo_input_text_data_t *data, const char *text)
{
    if (!data) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!ri_text_fits(text, ROBO_INPUT_TEXT_MAX)) {
        robo_input_text_data_init(data);
        return ROBO_INVALID_ARGUMENT;
    }
    ri_text_copy(data->text, ROBO_INPUT_TEXT_MAX, text);
    return ROBO_OK;
}

int robo_input_text_data_is_valid(const robo_input_text_data_t *data)
{
    return data != NULL && ri_bounded_strlen(data->text, ROBO_INPUT_TEXT_MAX) < ROBO_INPUT_TEXT_MAX;
}

size_t robo_input_text_data_length(const robo_input_text_data_t *data)
{
    return data ? ri_bounded_strlen(data->text, ROBO_INPUT_TEXT_MAX) : 0u;
}

int robo_input_text_data_equal(const robo_input_text_data_t *a, const robo_input_text_data_t *b)
{
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    for (index = 0u; index < ROBO_INPUT_TEXT_MAX; index++) {
        if (a->text[index] != b->text[index]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The type vocabulary                                                 */
/* ------------------------------------------------------------------ */

int robo_input_event_type_is_known(robo_input_event_type_t type)
{
    return type >= ROBO_INPUT_EVENT_NONE && type < (robo_input_event_type_t)ROBO_INPUT_EVENT_TYPE_COUNT;
}

int robo_input_event_type_is_deliverable(robo_input_event_type_t type)
{
    return type > ROBO_INPUT_EVENT_NONE && type < (robo_input_event_type_t)ROBO_INPUT_EVENT_TYPE_COUNT;
}

const char *robo_input_event_type_name(robo_input_event_type_t type)
{
    switch (type) {
    case ROBO_INPUT_EVENT_NONE:
        return "None";
    case ROBO_INPUT_EVENT_KEY_DOWN:
        return "KeyDown";
    case ROBO_INPUT_EVENT_KEY_UP:
        return "KeyUp";
    case ROBO_INPUT_EVENT_POINTER_MOVE:
        return "PointerMove";
    case ROBO_INPUT_EVENT_POINTER_DOWN:
        return "PointerDown";
    case ROBO_INPUT_EVENT_POINTER_UP:
        return "PointerUp";
    case ROBO_INPUT_EVENT_POINTER_CANCEL:
        return "PointerCancel";
    case ROBO_INPUT_EVENT_WHEEL:
        return "Wheel";
    case ROBO_INPUT_EVENT_TEXT_INPUT:
        return "TextInput";
    case ROBO_INPUT_EVENT_TOUCH_BEGIN:
        return "TouchBegin";
    case ROBO_INPUT_EVENT_TOUCH_MOVE:
        return "TouchMove";
    case ROBO_INPUT_EVENT_TOUCH_END:
        return "TouchEnd";
    case ROBO_INPUT_EVENT_TOUCH_CANCEL:
        return "TouchCancel";
    default:
        break;
    }
    return "unknown";
}

robo_input_payload_kind_t robo_input_event_payload_kind(robo_input_event_type_t type)
{
    switch (type) {
    case ROBO_INPUT_EVENT_KEY_DOWN:
    case ROBO_INPUT_EVENT_KEY_UP:
        return ROBO_INPUT_PAYLOAD_KEYBOARD;
    case ROBO_INPUT_EVENT_POINTER_MOVE:
    case ROBO_INPUT_EVENT_POINTER_DOWN:
    case ROBO_INPUT_EVENT_POINTER_UP:
    case ROBO_INPUT_EVENT_POINTER_CANCEL:
    case ROBO_INPUT_EVENT_WHEEL:
    case ROBO_INPUT_EVENT_TOUCH_BEGIN:
    case ROBO_INPUT_EVENT_TOUCH_MOVE:
    case ROBO_INPUT_EVENT_TOUCH_END:
    case ROBO_INPUT_EVENT_TOUCH_CANCEL:
        return ROBO_INPUT_PAYLOAD_POINTER;
    case ROBO_INPUT_EVENT_TEXT_INPUT:
        return ROBO_INPUT_PAYLOAD_TEXT;
    default:
        break;
    }
    return ROBO_INPUT_PAYLOAD_NONE;
}

const char *robo_input_payload_kind_name(robo_input_payload_kind_t kind)
{
    switch (kind) {
    case ROBO_INPUT_PAYLOAD_NONE:
        return "none";
    case ROBO_INPUT_PAYLOAD_KEYBOARD:
        return "keyboard";
    case ROBO_INPUT_PAYLOAD_POINTER:
        return "pointer";
    case ROBO_INPUT_PAYLOAD_TEXT:
        return "text";
    default:
        break;
    }
    return "unknown";
}

int robo_input_event_type_is_key(robo_input_event_type_t type)
{
    return type == ROBO_INPUT_EVENT_KEY_DOWN || type == ROBO_INPUT_EVENT_KEY_UP;
}

int robo_input_event_type_is_pointer_button(robo_input_event_type_t type)
{
    return type == ROBO_INPUT_EVENT_POINTER_MOVE || type == ROBO_INPUT_EVENT_POINTER_DOWN ||
           type == ROBO_INPUT_EVENT_POINTER_UP || type == ROBO_INPUT_EVENT_POINTER_CANCEL;
}

int robo_input_event_type_is_touch(robo_input_event_type_t type)
{
    return type == ROBO_INPUT_EVENT_TOUCH_BEGIN || type == ROBO_INPUT_EVENT_TOUCH_MOVE ||
           type == ROBO_INPUT_EVENT_TOUCH_END || type == ROBO_INPUT_EVENT_TOUCH_CANCEL;
}

int robo_input_event_type_has_position(robo_input_event_type_t type)
{
    return robo_input_event_payload_kind(type) == ROBO_INPUT_PAYLOAD_POINTER;
}

int robo_input_event_type_is_release(robo_input_event_type_t type)
{
    return type == ROBO_INPUT_EVENT_POINTER_UP || type == ROBO_INPUT_EVENT_POINTER_CANCEL ||
           type == ROBO_INPUT_EVENT_TOUCH_END || type == ROBO_INPUT_EVENT_TOUCH_CANCEL;
}

int robo_input_event_type_allows_source(robo_input_event_type_t type, robo_input_source_t source)
{
    if (!robo_input_source_is_known(source)) {
        return 0;
    }
    switch (type) {
    case ROBO_INPUT_EVENT_KEY_DOWN:
    case ROBO_INPUT_EVENT_KEY_UP:
    case ROBO_INPUT_EVENT_TEXT_INPUT:
        return source == ROBO_INPUT_SOURCE_KEYBOARD || source == ROBO_INPUT_SOURCE_REMOTE ||
               source == ROBO_INPUT_SOURCE_VIRTUAL;
    case ROBO_INPUT_EVENT_POINTER_MOVE:
    case ROBO_INPUT_EVENT_POINTER_DOWN:
    case ROBO_INPUT_EVENT_POINTER_UP:
    case ROBO_INPUT_EVENT_POINTER_CANCEL:
    case ROBO_INPUT_EVENT_WHEEL:
        return robo_input_source_is_positioning(source) != 0;
    case ROBO_INPUT_EVENT_TOUCH_BEGIN:
    case ROBO_INPUT_EVENT_TOUCH_MOVE:
    case ROBO_INPUT_EVENT_TOUCH_END:
    case ROBO_INPUT_EVENT_TOUCH_CANCEL:
        /* A positioning source that is not a mouse: a backend reporting `MOUSE` for a touch contact has
         * mislabelled itself, and accepting that would hide the mistake from every layer above. */
        return source == ROBO_INPUT_SOURCE_TOUCH || source == ROBO_INPUT_SOURCE_TABLET ||
               source == ROBO_INPUT_SOURCE_POINTER || source == ROBO_INPUT_SOURCE_VIRTUAL ||
               source == ROBO_INPUT_SOURCE_REMOTE;
    default:
        break;
    }
    return 0;
}

robo_input_event_type_t robo_input_event_type_for_window_event(int window_event_type)
{
    switch ((robo_window_event_type_t)window_event_type) {
    case ROBO_WINDOW_EVENT_KEY_DOWN:
        return ROBO_INPUT_EVENT_KEY_DOWN;
    case ROBO_WINDOW_EVENT_KEY_UP:
        return ROBO_INPUT_EVENT_KEY_UP;
    case ROBO_WINDOW_EVENT_MOUSE_MOVE:
        return ROBO_INPUT_EVENT_POINTER_MOVE;
    case ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN:
        return ROBO_INPUT_EVENT_POINTER_DOWN;
    case ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP:
        return ROBO_INPUT_EVENT_POINTER_UP;
    case ROBO_WINDOW_EVENT_MOUSE_WHEEL:
        return ROBO_INPUT_EVENT_WHEEL;
    default:
        break;
    }
    return ROBO_INPUT_EVENT_NONE;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

void robo_input_event_init(robo_input_event_t *event)
{
    if (!event) {
        return;
    }
    event->type = ROBO_INPUT_EVENT_NONE;
    event->source = ROBO_INPUT_SOURCE_UNKNOWN;
    robo_input_device_init(&event->device);
    event->timestamp_ms = 0u;
    event->modifiers = ROBO_INPUT_MODIFIERS_NONE;
    event->space = ROBO_INPUT_SPACE_UNKNOWN;
    event->window = ROBO_WINDOW_ID_NONE;
    event->display = ROBO_DISPLAY_ID_NONE;
    robo_input_target_init(&event->target);
    event->has_target = 0;
    robo_input_keyboard_data_init(&event->payload.keyboard);
    robo_input_pointer_data_init(&event->payload.pointer);
}

robo_result_t robo_input_event_set_keyboard(robo_input_event_t *event,
                                            robo_input_event_type_t type,
                                            robo_input_source_t source,
                                            const robo_input_keyboard_data_t *keyboard,
                                            robo_input_diag_t *diag)
{
    if (!event || !keyboard) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "a key event needs an event and a payload");
    }
    if (!robo_input_event_type_is_key(type)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "only KeyDown and KeyUp carry a keyboard payload");
    }
    if (!robo_input_keyboard_data_is_valid(keyboard) ||
        !robo_input_keyboard_data_action_matches(keyboard, (int)type)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "the keyboard payload is invalid or its action disagrees with the event");
    }
    if (!robo_input_event_type_allows_source(type, source)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "that source cannot report a key event");
    }
    event->type = type;
    event->source = source;
    event->payload.keyboard = *keyboard;
    return ROBO_OK;
}

robo_result_t robo_input_event_set_pointer(robo_input_event_t *event,
                                           robo_input_event_type_t type,
                                           robo_input_source_t source,
                                           const robo_input_pointer_data_t *pointer,
                                           robo_input_space_t space,
                                           robo_input_diag_t *diag)
{
    if (!event || !pointer) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "a positioned event needs an event and a payload");
    }
    if (robo_input_event_payload_kind(type) != ROBO_INPUT_PAYLOAD_POINTER) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "that type does not carry a pointer payload");
    }
    if (!robo_input_pointer_data_is_valid(pointer)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "the pointer payload is not a payload this build may carry");
    }
    if (!robo_input_space_is_known(space)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "a positioned event must say which plane its coordinates are on");
    }
    if (!robo_input_event_type_allows_source(type, source)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "that source cannot report that kind of positioning event");
    }
    event->type = type;
    event->source = source;
    event->space = space;
    event->payload.pointer = *pointer;
    return ROBO_OK;
}

robo_result_t robo_input_event_set_text(robo_input_event_t *event,
                                        robo_input_source_t source,
                                        const char *text,
                                        robo_input_diag_t *diag)
{
    if (!event) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "a text event needs an event to fill");
    }
    if (!robo_input_event_type_allows_source(ROBO_INPUT_EVENT_TEXT_INPUT, source)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "that source cannot report text");
    }
    if (!ri_text_fits(text, ROBO_INPUT_TEXT_MAX)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "the text is longer than one event may carry");
    }
    if (text == NULL || text[0] == '\0') {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "a TextInput event must carry at least one byte");
    }
    event->type = ROBO_INPUT_EVENT_TEXT_INPUT;
    event->source = source;
    robo_input_text_data_set(&event->payload.text, text);
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Common fields                                                       */
/* ------------------------------------------------------------------ */

robo_result_t robo_input_event_set_device(robo_input_event_t *event,
                                          const robo_input_device_t *device,
                                          robo_input_diag_t *diag)
{
    if (!event || !device) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "a device needs an event and a record");
    }
    if (!robo_input_device_is_valid(device)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "the device record is not one this build may carry");
    }
    if (device->source != ROBO_INPUT_SOURCE_UNKNOWN && device->source != event->source) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "the device's source disagrees with the event's");
    }
    event->device = *device;
    return ROBO_OK;
}

robo_result_t robo_input_event_set_timestamp(robo_input_event_t *event, uint64_t timestamp_ms)
{
    if (!event) {
        return ROBO_INVALID_ARGUMENT;
    }
    event->timestamp_ms = timestamp_ms;
    return ROBO_OK;
}

robo_result_t robo_input_event_set_modifiers(robo_input_event_t *event,
                                             robo_input_modifiers_t modifiers,
                                             robo_input_diag_t *diag)
{
    if (!event) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "a modifier state needs an event");
    }
    if (!robo_input_modifiers_are_known(modifiers)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_EVENT, ROBO_INVALID_ARGUMENT,
                               "the modifier state carries bits this build does not define");
    }
    event->modifiers = modifiers;
    return ROBO_OK;
}

robo_result_t robo_input_event_set_window(robo_input_event_t *event, robo_window_id_t window)
{
    if (!event) {
        return ROBO_INVALID_ARGUMENT;
    }
    event->window = window;
    return ROBO_OK;
}

robo_result_t robo_input_event_set_display(robo_input_event_t *event, robo_display_id_t display)
{
    if (!event) {
        return ROBO_INVALID_ARGUMENT;
    }
    event->display = display;
    return ROBO_OK;
}

robo_result_t robo_input_event_set_target(robo_input_event_t *event,
                                          const robo_input_target_t *target,
                                          robo_input_diag_t *diag)
{
    if (!event || !target) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "a target needs an event and a record");
    }
    if (!robo_input_target_is_valid(target)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "the target record is not one this build may carry");
    }
    if (robo_input_event_type_has_position(event->type) && target->space != event->space) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "the target was concluded in another plane than the event's");
    }
    if (event->window != ROBO_WINDOW_ID_NONE && target->window != ROBO_WINDOW_ID_NONE &&
        target->window != event->window) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_TARGET, ROBO_INVALID_ARGUMENT,
                               "the target names a window the event is not about");
    }
    event->target = *target;
    event->has_target = 1;
    return ROBO_OK;
}

void robo_input_event_clear_target(robo_input_event_t *event)
{
    if (!event) {
        return;
    }
    robo_input_target_init(&event->target);
    event->has_target = 0;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p data is coherent with a positioned event of @p type.
 *
 * The rules are about *agreement between the event and its payload*, which is the class of mistake that
 * produces an event a consumer acts on wrongly rather than one it can detect: a move whose mask claims a
 * press, a release that still has its own button down, a cancel that does not say it is one, or a touch
 * contact with a mouse button held.
 */
static int pointer_payload_matches_type(const robo_input_pointer_data_t *data,
                                        robo_input_event_type_t type)
{
    int cancelled = (data->flags & ROBO_INPUT_POINTER_FLAG_CANCELLED) != 0u;

    if (cancelled != (type == ROBO_INPUT_EVENT_POINTER_CANCEL ||
                      type == ROBO_INPUT_EVENT_TOUCH_CANCEL)) {
        return 0;
    }
    if (robo_input_event_type_is_touch(type)) {
        /* A touch contact is a finger: it has no buttons, and an event claiming one would be a stylus
         * mislabelled as a finger (or the reverse). */
        return data->button == ROBO_MOUSE_BUTTON_NONE && data->buttons == 0u;
    }
    switch (type) {
    case ROBO_INPUT_EVENT_POINTER_MOVE:
        return data->button == ROBO_MOUSE_BUTTON_NONE;
    case ROBO_INPUT_EVENT_POINTER_DOWN:
        return data->button != ROBO_MOUSE_BUTTON_NONE &&
               robo_input_button_mask_has(data->buttons, data->button);
    case ROBO_INPUT_EVENT_POINTER_UP:
        return data->button != ROBO_MOUSE_BUTTON_NONE &&
               !robo_input_button_mask_has(data->buttons, data->button);
    case ROBO_INPUT_EVENT_POINTER_CANCEL:
        return data->button == ROBO_MOUSE_BUTTON_NONE ||
               !robo_input_button_mask_has(data->buttons, data->button);
    case ROBO_INPUT_EVENT_WHEEL:
        return data->button == ROBO_MOUSE_BUTTON_NONE;
    default:
        break;
    }
    return 0;
}

int robo_input_event_is_valid(const robo_input_event_t *event)
{
    robo_input_payload_kind_t kind;

    if (!event) {
        return 0;
    }
    /* 1. a type that could be delivered */
    if (!robo_input_event_type_is_deliverable(event->type)) {
        return 0;
    }
    /* 2. a source that may report it */
    if (!robo_input_event_type_allows_source(event->type, event->source)) {
        return 0;
    }
    /* 3. a device record, agreeing with the source when it names one */
    if (!robo_input_device_is_valid(&event->device)) {
        return 0;
    }
    if (event->device.source != ROBO_INPUT_SOURCE_UNKNOWN && event->device.source != event->source) {
        return 0;
    }
    /* 4. defined modifier bits */
    if (!robo_input_modifiers_are_known(event->modifiers)) {
        return 0;
    }
    /* 5. the payload the type owns, present and coherent with the type */
    kind = robo_input_event_payload_kind(event->type);
    switch (kind) {
    case ROBO_INPUT_PAYLOAD_KEYBOARD:
        if (!robo_input_keyboard_data_is_valid(&event->payload.keyboard) ||
            !robo_input_keyboard_data_action_matches(&event->payload.keyboard, (int)event->type)) {
            return 0;
        }
        break;
    case ROBO_INPUT_PAYLOAD_POINTER:
        if (!robo_input_pointer_data_is_valid(&event->payload.pointer) ||
            !pointer_payload_matches_type(&event->payload.pointer, event->type)) {
            return 0;
        }
        break;
    case ROBO_INPUT_PAYLOAD_TEXT:
        /* A text event that produced no text is not an event; the *payload* may be empty, and the rule that
         * says so lives here rather than there because it is a fact about the type, not about the field. */
        if (!robo_input_text_data_is_valid(&event->payload.text) ||
            event->payload.text.text[0] == '\0') {
            return 0;
        }
        break;
    default:
        return 0;
    }
    /* 6. a plane exactly when there is a position to be on it */
    if (robo_input_event_type_has_position(event->type)) {
        if (!robo_input_space_is_known(event->space)) {
            return 0;
        }
    } else if (event->space != ROBO_INPUT_SPACE_UNKNOWN) {
        return 0;
    }
    /* 7. a target that agrees with the event about the plane and the window */
    if (event->has_target != 0 && event->has_target != 1) {
        return 0;
    }
    if (event->has_target) {
        if (!robo_input_target_is_valid(&event->target)) {
            return 0;
        }
        if (robo_input_event_type_has_position(event->type) && event->target.space != event->space) {
            return 0;
        }
        if (event->window != ROBO_WINDOW_ID_NONE && event->target.window != ROBO_WINDOW_ID_NONE &&
            event->target.window != event->window) {
            return 0;
        }
    }
    return 1;
}

robo_result_t robo_input_event_copy(robo_input_event_t *destination, const robo_input_event_t *source)
{
    if (!destination || !source) {
        return ROBO_INVALID_ARGUMENT;
    }
    *destination = *source;
    return ROBO_OK;
}

int robo_input_event_equal(const robo_input_event_t *a, const robo_input_event_t *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->type != b->type || a->source != b->source || a->timestamp_ms != b->timestamp_ms ||
        a->modifiers != b->modifiers || a->space != b->space || a->window != b->window ||
        a->display != b->display || a->has_target != b->has_target) {
        return 0;
    }
    if (!robo_input_device_equal(&a->device, &b->device)) {
        return 0;
    }
    if (a->has_target && !robo_input_target_equal(&a->target, &b->target)) {
        return 0;
    }
    switch (robo_input_event_payload_kind(a->type)) {
    case ROBO_INPUT_PAYLOAD_KEYBOARD:
        return robo_input_keyboard_data_equal(&a->payload.keyboard, &b->payload.keyboard);
    case ROBO_INPUT_PAYLOAD_POINTER:
        return a->payload.pointer.pointer == b->payload.pointer.pointer &&
               a->payload.pointer.button == b->payload.pointer.button &&
               a->payload.pointer.buttons == b->payload.pointer.buttons &&
               robo_gfx_point_equal(a->payload.pointer.position, b->payload.pointer.position) &&
               robo_gfx_point_equal(a->payload.pointer.previous, b->payload.pointer.previous) &&
               a->payload.pointer.has_previous == b->payload.pointer.has_previous &&
               a->payload.pointer.delta_x == b->payload.pointer.delta_x &&
               a->payload.pointer.delta_y == b->payload.pointer.delta_y &&
               a->payload.pointer.click_count == b->payload.pointer.click_count &&
               a->payload.pointer.wheel.delta_x == b->payload.pointer.wheel.delta_x &&
               a->payload.pointer.wheel.delta_y == b->payload.pointer.wheel.delta_y &&
               a->payload.pointer.wheel.unit == b->payload.pointer.wheel.unit &&
               a->payload.pointer.flags == b->payload.pointer.flags;
    case ROBO_INPUT_PAYLOAD_TEXT:
        return robo_input_text_data_equal(&a->payload.text, &b->payload.text);
    default:
        break;
    }
    /* No payload: two events of an undefined type carry no record that could differ, so what was compared
     * above is the whole comparison. */
    return 1;
}

/* ------------------------------------------------------------------ */
/* Positions                                                           */
/* ------------------------------------------------------------------ */

robo_result_t robo_input_event_position(const robo_input_event_t *event,
                                        robo_gfx_point_t *out,
                                        robo_input_diag_t *diag)
{
    if (!event || !out) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_INVALID_ARGUMENT,
                               "a position needs an event and somewhere to put it");
    }
    if (!robo_input_event_type_has_position(event->type)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_SPACE, ROBO_NOT_SUPPORTED,
                               "that event type carries no position");
    }
    *out = event->payload.pointer.position;
    return ROBO_OK;
}

robo_result_t robo_input_event_position_in(const robo_input_event_t *event,
                                           robo_input_space_t to,
                                           const robo_input_route_t *route,
                                           robo_gfx_point_t *out,
                                           robo_input_diag_t *diag)
{
    robo_gfx_point_t point;
    robo_result_t r = robo_input_event_position(event, &point, diag);

    if (r != ROBO_OK) {
        return r;
    }
    return robo_input_convert_point(point, event->space, to, route, out, diag);
}
