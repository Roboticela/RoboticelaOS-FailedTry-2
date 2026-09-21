/*
 * RoboInput module doors (Phase 12.1) — see RoboInput.h.
 *
 * Version, capabilities and the model dump. There is no `robo_input_init` to go with these and that is the
 * design rather than an omission: this module owns no state, so there is nothing to initialize, nothing to
 * shut down, and no order in which a caller has to do either. What is here is the answer to "what is this
 * build?", which is three questions:
 *
 *   **which version is this?** -- `robo_input_version_string` / `robo_input_get_version`, the pair every
 *   module in this tree exposes and the pair a caller storing an event could stamp beside it;
 *
 *   **what does it claim to do?** -- `robo_input_get_capabilities`, whose first block is what exists and
 *   whose second block is what deliberately does not. The zeros are the important half: a reader of this
 *   build finds device reading, dispatch, focus, composition, propagation, gestures and the event loop named
 *   and answered `0`, rather than left to be inferred from what is missing;
 *
 *   **what can its model express?** -- `robo_input_write_model`, the vocabulary as text: every event type
 *   with the payload it owns and the facts about it, every source with whether it positions, every plane
 *   with whether this build can convert through it. A developer reads it once; a test asserts it whole, so a
 *   type silently gaining a payload kind or losing its position would fail rather than surprise.
 */

#include "RoboInput/RoboInput.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

const char *robo_input_version_string(void)
{
    return ROBO_INPUT_VERSION_STRING;
}

robo_result_t robo_input_get_version(robo_i32 *out_major, robo_i32 *out_minor, robo_i32 *out_patch)
{
    if (out_major) {
        *out_major = (robo_i32)ROBO_INPUT_VERSION_MAJOR;
    }
    if (out_minor) {
        *out_minor = (robo_i32)ROBO_INPUT_VERSION_MINOR;
    }
    if (out_patch) {
        *out_patch = (robo_i32)ROBO_INPUT_VERSION_PATCH;
    }
    return ROBO_OK;
}

int robo_input_model_is_implemented(void)
{
    return ROBO_INPUT_MODEL_IMPLEMENTED ? 1 : 0;
}

int robo_input_platform_adapters_are_implemented(void)
{
    return ROBO_INPUT_PLATFORM_ADAPTERS_IMPLEMENTED ? 1 : 0;
}

robo_result_t robo_input_get_capabilities(robo_input_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->event_model = ROBO_INPUT_MODEL_IMPLEMENTED ? 1 : 0;
    out->coordinate_spaces = 1;
    out->target_identity = 1;
    out->event_queue = 1;
    out->debug_dump = 1;
    out->window_bridge = 1;

    out->device_reading = 0;
    out->platform_adapters = ROBO_INPUT_PLATFORM_ADAPTERS_IMPLEMENTED ? 1 : 0;
    out->dispatch = 0;
    out->focus = 0;
    out->text_editing = 0;
    out->text_composition = 0;
    out->clipboard = 0;
    out->propagation = 0;
    out->gestures = 0;
    out->event_loop = 0;
    out->multiprocess_input = 0;

    out->event_type_count = ROBO_INPUT_EVENT_TYPE_COUNT;
    out->source_count = ROBO_INPUT_SOURCE_COUNT;
    out->space_count = ROBO_INPUT_SPACE_COUNT;
    out->version = ROBO_INPUT_VERSION_STRING;
    return ROBO_OK;
}

/** One event type's line: what it is, which payload it owns, and the four facts about it. */
static void write_type_line(robo_input_writer_t *text, robo_input_event_type_t type)
{
    ri_writer_text(text, "type ");
    ri_writer_text(text, robo_input_event_type_name(type));
    ri_writer_field_text(text, " payload", robo_input_payload_kind_name(robo_input_event_payload_kind(type)));
    ri_writer_field_u64(text, " position", robo_input_event_type_has_position(type) ? 1u : 0u);
    ri_writer_field_u64(text, " release", robo_input_event_type_is_release(type) ? 1u : 0u);
    ri_writer_field_u64(text, " key", robo_input_event_type_is_key(type) ? 1u : 0u);
    ri_writer_field_u64(text, " touch", robo_input_event_type_is_touch(type) ? 1u : 0u);
    ri_writer_char(text, '\n');
}

static void write_source_line(robo_input_writer_t *text, robo_input_source_t source)
{
    ri_writer_text(text, "source ");
    ri_writer_text(text, robo_input_source_name(source));
    ri_writer_field_u64(text, " positioning", robo_input_source_is_positioning(source) ? 1u : 0u);
    ri_writer_char(text, '\n');
}

static void write_space_line(robo_input_writer_t *text, robo_input_space_t space)
{
    ri_writer_text(text, "space ");
    ri_writer_text(text, robo_input_space_name(space));
    ri_writer_field_u64(text, " convertible", robo_input_space_is_convertible(space) ? 1u : 0u);
    ri_writer_char(text, '\n');
}

robo_result_t robo_input_write_model(char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_input_diag_t *diag)
{
    robo_input_writer_t text;
    uint32_t index;

    if (!ri_writer_arguments_are_valid(buffer, capacity, out_length)) {
        return ROBO_INPUT_DIAG(diag, ROBO_INPUT_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                               "a model dump needs a buffer and somewhere to put the length");
    }

    ri_writer_init(&text, buffer, capacity);
    ri_writer_text(&text, "input ");
    ri_writer_text(&text, ROBO_INPUT_VERSION_STRING);
    ri_writer_field_u64(&text, " model", robo_input_model_is_implemented() ? 1u : 0u);
    ri_writer_field_u64(&text, " adapters", robo_input_platform_adapters_are_implemented() ? 1u : 0u);
    ri_writer_field_u64(&text, " types", ROBO_INPUT_EVENT_TYPE_COUNT);
    ri_writer_field_u64(&text, " sources", ROBO_INPUT_SOURCE_COUNT);
    ri_writer_field_u64(&text, " spaces", ROBO_INPUT_SPACE_COUNT);
    ri_writer_char(&text, '\n');

    /* `NONE` is listed with the rest: it is part of the vocabulary -- the value a cleared record has -- and a
     * reader asking "what types exist?" wants to see it rather than to infer it from an absent line. */
    for (index = 0u; index < ROBO_INPUT_EVENT_TYPE_COUNT; index++) {
        write_type_line(&text, (robo_input_event_type_t)index);
    }
    for (index = 0u; index < ROBO_INPUT_SOURCE_COUNT; index++) {
        write_source_line(&text, (robo_input_source_t)index);
    }
    for (index = 0u; index < ROBO_INPUT_SPACE_COUNT; index++) {
        write_space_line(&text, (robo_input_space_t)index);
    }
    return ri_writer_finish(&text, out_length, diag);
}
