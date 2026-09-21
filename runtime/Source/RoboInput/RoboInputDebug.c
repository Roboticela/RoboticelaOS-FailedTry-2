/*
 * RoboInput debug text (Phase 12.1) — see RoboInputDebug.h.
 *
 * One line per event, fields in a fixed order, every field present, nothing that varies between two runs of
 * the same sequence. The order is *common fields first, payload after*, because the reader who is looking
 * for "which window did this go to" reads the first half of the line and the reader who is looking for "what
 * did the key do" reads the second half.
 *
 * Three formatting decisions worth naming, because each one is a decision rather than a detail:
 *
 *   `previous=none` when the payload has no previous position. Printing `previous=0,0` would be a lie of the
 *   kind this whole phase is written against -- a first sample is not a sample at the origin;
 *
 *   `space=unknown` on an event that carries no position. The event genuinely names no plane, and the dump
 *   says so rather than borrowing the plane the last event was in;
 *
 *   the button mask reads as `Left`, `Left+Right` or `None`, and the *unit* of a wheel is printed beside its
 *   deltas, because `delta_y=120` means one notch in line units and a fifth of a pixel in pixel units.
 */

#include "RoboInput/RoboInputDebug.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

/** A device, as `none`, `7` or `7:Keyboard`. */
static void write_device(robo_input_writer_t *text, const robo_input_device_t *device)
{
    ri_writer_text(text, "device=");
    if (!device || device->id == ROBO_INPUT_DEVICE_ID_NONE) {
        if (!device || device->name[0] == '\0') {
            ri_writer_text(text, "none");
            return;
        }
        ri_writer_text(text, "0:");
        ri_writer_text(text, device->name);
        return;
    }
    ri_writer_u64(text, device->id);
    if (device->name[0] != '\0') {
        ri_writer_char(text, ':');
        ri_writer_text(text, device->name);
    }
}

static void write_modifiers(robo_input_writer_t *text, robo_input_modifiers_t modifiers)
{
    char buffer[160];
    size_t length = 0u;

    ri_writer_text(text, "modifiers=");
    if (robo_input_modifiers_write(modifiers, buffer, sizeof(buffer), &length) == ROBO_OK) {
        ri_writer_text(text, buffer);
        return;
    }
    /* Unreachable for a state whose bits are defined -- the list is at most fifteen names -- and printed
     * rather than assumed away. */
    ri_writer_text(text, "?");
}

/** The keyboard payload: identity, what happened, and the text the key produced, if any. */
static void write_keyboard_payload(robo_input_writer_t *text, const robo_input_keyboard_data_t *data)
{
    ri_writer_field_text(text, " physical", robo_physical_key_name(data->physical));
    ri_writer_field_text(text, " logical", robo_logical_key_name(data->logical));
    ri_writer_field_text(text, " action", robo_key_action_name(data->action));
    ri_writer_text(text, " repeat=");
    ri_writer_u64(text, (uint64_t)(data->repeat ? 1 : 0));
    ri_writer_text(text, " flags=");
    if (data->flags == 0u) {
        ri_writer_text(text, "none");
    } else {
        int written = 0;

        if ((data->flags & ROBO_INPUT_KEY_FLAG_MODIFIER) != 0u) {
            ri_writer_text(text, "modifier");
            written = 1;
        }
        if ((data->flags & ROBO_INPUT_KEY_FLAG_NUMPAD) != 0u) {
            if (written) {
                ri_writer_char(text, '+');
            }
            ri_writer_text(text, "numpad");
            written = 1;
        }
        if ((data->flags & ROBO_INPUT_KEY_FLAG_PHYSICAL_UNKNOWN) != 0u) {
            if (written) {
                ri_writer_char(text, '+');
            }
            ri_writer_text(text, "physical-unknown");
        }
    }
    ri_writer_text(text, " text=\"");
    ri_writer_text(text, data->text);
    ri_writer_char(text, '"');
}

/** The pointer payload: which pointer, where, what is down, and how much it scrolled. */
static void write_pointer_payload(robo_input_writer_t *text, const robo_input_pointer_data_t *data)
{
    char mask[64];
    size_t mask_length = 0u;

    ri_writer_field_u64(text, " pointer", (uint64_t)data->pointer);
    ri_writer_field_point(text, " position", data->position.x, data->position.y);
    if (data->has_previous) {
        ri_writer_field_point(text, " previous", data->previous.x, data->previous.y);
    } else {
        ri_writer_text(text, " previous=none");
    }
    ri_writer_field_point(text, " delta", data->delta_x, data->delta_y);
    ri_writer_text(text, " buttons=");
    if (robo_input_button_mask_write(data->buttons, mask, sizeof(mask), &mask_length) == ROBO_OK) {
        ri_writer_text(text, mask);
    } else {
        ri_writer_text(text, "?");
    }
    ri_writer_field_text(text, " button", robo_mouse_button_name(data->button));
    ri_writer_field_i32(text, " clicks", data->click_count);
    ri_writer_field_point(text, " wheel", data->wheel.delta_x, data->wheel.delta_y);
    ri_writer_field_text(text, " unit", robo_input_scroll_unit_name(data->wheel.unit));
    ri_writer_text(text, " flags=");
    if (data->flags == 0u) {
        ri_writer_text(text, "none");
    } else {
        int written = 0;

        if ((data->flags & ROBO_INPUT_POINTER_FLAG_PRIMARY) != 0u) {
            ri_writer_text(text, "primary");
            written = 1;
        }
        if ((data->flags & ROBO_INPUT_POINTER_FLAG_CANCELLED) != 0u) {
            if (written) {
                ri_writer_char(text, '+');
            }
            ri_writer_text(text, "cancelled");
            written = 1;
        }
        if ((data->flags & ROBO_INPUT_POINTER_FLAG_SYNTHETIC) != 0u) {
            if (written) {
                ri_writer_char(text, '+');
            }
            ri_writer_text(text, "synthetic");
        }
    }
}

/** What the event is about, when a dispatcher has filled a target in. */
static void write_target(robo_input_writer_t *text, const robo_input_target_t *target)
{
    ri_writer_field_text(text, " target", robo_input_target_kind_name(target->kind));
    if (target->window != ROBO_WINDOW_ID_NONE) {
        ri_writer_field_u64(text, " target_window", target->window);
    }
    if (target->application != ROBO_INPUT_APPLICATION_ID_NONE) {
        ri_writer_field_u64(text, " target_app", target->application);
    }
    if (robo_input_target_has_document(target)) {
        ri_writer_text(text, " target_node=");
        ri_writer_u64(text, target->dom_node.document_id);
        ri_writer_char(text, ':');
        ri_writer_u64(text, target->dom_node.node_id);
        ri_writer_char(text, ':');
        ri_writer_u64(text, target->dom_node.generation);
        ri_writer_field_u64(text, " target_layout", target->layout_node);
        ri_writer_text(text, " target_flags=");
        if (target->flags == 0u) {
            ri_writer_text(text, "none");
        } else {
            int written = 0;

            if ((target->flags & ROBO_INPUT_TARGET_FLAG_TEXT) != 0u) {
                ri_writer_text(text, "text");
                written = 1;
            }
            if ((target->flags & ROBO_INPUT_TARGET_FLAG_CONTENT) != 0u) {
                if (written) {
                    ri_writer_char(text, '+');
                }
                ri_writer_text(text, "content");
                written = 1;
            }
            if ((target->flags & ROBO_INPUT_TARGET_FLAG_TRANSPARENT) != 0u) {
                if (written) {
                    ri_writer_char(text, '+');
                }
                ri_writer_text(text, "transparent");
            }
        }
    }
}

robo_result_t robo_input_event_write(const robo_input_event_t *event,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_input_diag_t *diag)
{
    robo_input_payload_kind_t kind;
    robo_input_writer_t text;

    if (!event || !ri_writer_arguments_are_valid(buffer, capacity, out_length)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                               "an event dump needs an event, a buffer and somewhere to put the length");
    }

    ri_writer_init(&text, buffer, capacity);
    ri_writer_text(&text, robo_input_event_type_name(event->type));
    ri_writer_field_text(&text, " source", robo_input_source_name(event->source));
    ri_writer_char(&text, ' ');
    write_device(&text, &event->device);
    ri_writer_field_u64(&text, " window", event->window);
    ri_writer_field_u64(&text, " display", event->display);
    ri_writer_field_u64(&text, " time", event->timestamp_ms);
    ri_writer_char(&text, ' ');
    write_modifiers(&text, event->modifiers);
    ri_writer_field_text(&text, " space", robo_input_space_name(event->space));

    kind = robo_input_event_payload_kind(event->type);
    switch (kind) {
    case ROBO_INPUT_PAYLOAD_KEYBOARD:
        write_keyboard_payload(&text, &event->payload.keyboard);
        break;
    case ROBO_INPUT_PAYLOAD_POINTER:
        write_pointer_payload(&text, &event->payload.pointer);
        break;
    case ROBO_INPUT_PAYLOAD_TEXT:
        ri_writer_text(&text, " text=\"");
        ri_writer_text(&text, event->payload.text.text);
        ri_writer_char(&text, '"');
        break;
    default:
        /* No payload: the type has none, and printing "the payload is empty" would be printing a field
         * that does not exist on this event. */
        break;
    }

    if (event->has_target) {
        write_target(&text, &event->target);
    }
    return ri_writer_finish(&text, out_length, diag);
}

robo_result_t robo_input_queue_write(const robo_input_queue_t *queue,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_input_diag_t *diag)
{
    robo_input_queue_stats_t stats;
    robo_input_writer_t text;
    uint32_t index;

    if (!queue || !ri_writer_arguments_are_valid(buffer, capacity, out_length)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                               "a queue dump needs a queue, a buffer and somewhere to put the length");
    }
    if (robo_input_queue_stats(queue, &stats) != ROBO_OK) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                               "the queue's statistics could not be read");
    }

    ri_writer_init(&text, buffer, capacity);
    ri_writer_text(&text, "queue");
    ri_writer_field_u64(&text, " capacity", stats.capacity);
    ri_writer_field_u64(&text, " count", stats.count);
    ri_writer_field_u64(&text, " pushed", stats.pushed);
    ri_writer_field_u64(&text, " popped", stats.popped);
    ri_writer_field_u64(&text, " dropped", stats.dropped);
    ri_writer_field_u64(&text, " cleared", stats.cleared);
    ri_writer_field_u64(&text, " high_water", stats.high_water);
    ri_writer_char(&text, '\n');

    for (index = 0u; index < stats.count; index++) {
        /* One line at a time, into the bound-sized scratch buffer, and then appended to the caller's
         * buffer through the ordinary writer. That way a caller's buffer can be too small for the *dump*
         * without any single event line being too small for the *event* -- so the reported length is always
         * the length the whole text needs, and never a partial measurement. */
        char line[ROBO_INPUT_DUMP_LINE_MAX];
        robo_input_event_t event;
        robo_input_diag_t line_diag;
        size_t line_length = 0u;

        robo_input_diag_reset(&line_diag);
        if (robo_input_queue_peek_at(queue, index, &event, &line_diag) != ROBO_OK) {
            /* Cannot happen while the count above is the queue's own, and written as a refusal rather than
             * as an assumption: a dump that silently skipped an event would be worse than one that says it
             * could not read it. */
            return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_DUMP, ROBO_ERROR,
                                   "a queued event could not be read");
        }
        if (robo_input_event_write(&event, line, sizeof(line), &line_length, &line_diag) != ROBO_OK) {
            /* The stated bound is a promise: a line that did not fit it would mean the constant in the
             * public header is wrong, and the dump says so rather than emitting a truncated line. */
            return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_DUMP, ROBO_ERROR,
                                   "an event line exceeded the module's stated bound");
        }
        ri_writer_text(&text, line);
        ri_writer_char(&text, '\n');
    }
    return ri_writer_finish(&text, out_length, diag);
}
