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
 *   deltas, because `deltaY=120` means one notch in line units and a fifth of a pixel in pixel units.
 */

#include "RoboInput/RoboInputDebug.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

/** A device, as `none`, `7` or `7:Keyboard`. */
static void WriteDevice(InputWriter *text, const InputDevice *device)
{
    RiWriterText(text, "device=");
    if (!device || device->id == RoboInputDeviceIdNone) {
        if (!device || device->name[0] == '\0') {
            RiWriterText(text, "none");
            return;
        }
        RiWriterText(text, "0:");
        RiWriterText(text, device->name);
        return;
    }
    RiWriterU64(text, device->id);
    if (device->name[0] != '\0') {
        RiWriterChar(text, ':');
        RiWriterText(text, device->name);
    }
}

static void WriteModifiers(InputWriter *text, InputModifiers modifiers)
{
    char buffer[160];
    size_t length = 0u;

    RiWriterText(text, "modifiers=");
    if (InputModifiersWrite(modifiers, buffer, sizeof(buffer), &length) == RoboOk) {
        RiWriterText(text, buffer);
        return;
    }
    /* Unreachable for a state whose bits are defined -- the list is at most fifteen names -- and printed
     * rather than assumed away. */
    RiWriterText(text, "?");
}

/** The keyboard payload: identity, what happened, and the text the key produced, if any. */
static void WriteKeyboardPayload(InputWriter *text, const InputKeyboardData *data)
{
    RiWriterFieldText(text, " physical", PhysicalKeyName(data->physical));
    RiWriterFieldText(text, " logical", LogicalKeyName(data->logical));
    RiWriterFieldText(text, " action", KeyActionName(data->action));
    RiWriterText(text, " repeat=");
    RiWriterU64(text, (uint64_t)(data->repeat ? 1 : 0));
    RiWriterText(text, " flags=");
    if (data->flags == 0u) {
        RiWriterText(text, "none");
    } else {
        int written = 0;

        if ((data->flags & RoboInputKeyFlagModifier) != 0u) {
            RiWriterText(text, "modifier");
            written = 1;
        }
        if ((data->flags & RoboInputKeyFlagNumpad) != 0u) {
            if (written) {
                RiWriterChar(text, '+');
            }
            RiWriterText(text, "numpad");
            written = 1;
        }
        if ((data->flags & RoboInputKeyFlagPhysicalUnknown) != 0u) {
            if (written) {
                RiWriterChar(text, '+');
            }
            RiWriterText(text, "physical-unknown");
        }
    }
    RiWriterText(text, " text=\"");
    RiWriterText(text, data->text);
    RiWriterChar(text, '"');
}

/** The pointer payload: which pointer, where, what is down, and how much it scrolled. */
static void WritePointerPayload(InputWriter *text, const InputPointerData *data)
{
    char mask[64];
    size_t maskLength = 0u;

    RiWriterFieldU64(text, " pointer", (uint64_t)data->pointer);
    RiWriterFieldPoint(text, " position", data->position.x, data->position.y);
    if (data->hasPrevious) {
        RiWriterFieldPoint(text, " previous", data->previous.x, data->previous.y);
    } else {
        RiWriterText(text, " previous=none");
    }
    RiWriterFieldPoint(text, " delta", data->deltaX, data->deltaY);
    RiWriterText(text, " buttons=");
    if (InputButtonMaskWrite(data->buttons, mask, sizeof(mask), &maskLength) == RoboOk) {
        RiWriterText(text, mask);
    } else {
        RiWriterText(text, "?");
    }
    RiWriterFieldText(text, " button", MouseButtonName(data->button));
    RiWriterFieldI32(text, " clicks", data->clickCount);
    RiWriterFieldPoint(text, " wheel", data->wheel.deltaX, data->wheel.deltaY);
    RiWriterFieldText(text, " unit", InputScrollUnitName(data->wheel.unit));
    RiWriterText(text, " flags=");
    if (data->flags == 0u) {
        RiWriterText(text, "none");
    } else {
        int written = 0;

        if ((data->flags & RoboInputPointerFlagPrimary) != 0u) {
            RiWriterText(text, "primary");
            written = 1;
        }
        if ((data->flags & RoboInputPointerFlagCancelled) != 0u) {
            if (written) {
                RiWriterChar(text, '+');
            }
            RiWriterText(text, "cancelled");
            written = 1;
        }
        if ((data->flags & RoboInputPointerFlagSynthetic) != 0u) {
            if (written) {
                RiWriterChar(text, '+');
            }
            RiWriterText(text, "synthetic");
        }
    }
}

/** What the event is about, when a dispatcher has filled a target in. */
static void WriteTarget(InputWriter *text, const InputTarget *target)
{
    RiWriterFieldText(text, " target", InputTargetKindName(target->kind));
    if (target->window != RoboWindowIdNone) {
        RiWriterFieldU64(text, " target_window", target->window);
    }
    if (target->application != RoboInputApplicationIdNone) {
        RiWriterFieldU64(text, " target_app", target->application);
    }
    if (InputTargetHasDocument(target)) {
        RiWriterText(text, " target_node=");
        RiWriterU64(text, target->domNode.documentId);
        RiWriterChar(text, ':');
        RiWriterU64(text, target->domNode.nodeId);
        RiWriterChar(text, ':');
        RiWriterU64(text, target->domNode.generation);
        RiWriterFieldU64(text, " target_layout", target->layoutNode);
        RiWriterText(text, " target_flags=");
        if (target->flags == 0u) {
            RiWriterText(text, "none");
        } else {
            int written = 0;

            if ((target->flags & RoboInputTargetFlagText) != 0u) {
                RiWriterText(text, "text");
                written = 1;
            }
            if ((target->flags & RoboInputTargetFlagContent) != 0u) {
                if (written) {
                    RiWriterChar(text, '+');
                }
                RiWriterText(text, "content");
                written = 1;
            }
            if ((target->flags & RoboInputTargetFlagTransparent) != 0u) {
                if (written) {
                    RiWriterChar(text, '+');
                }
                RiWriterText(text, "transparent");
            }
        }
    }
}

Result InputEventWrite(const InputEvent *event,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     InputDiag *diag)
{
    InputPayloadKind kind;
    InputWriter text;

    if (!event || !RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                               "an event dump needs an event, a buffer and somewhere to put the length");
    }

    RiWriterInit(&text, buffer, capacity);
    RiWriterText(&text, InputEventTypeName(event->type));
    RiWriterFieldText(&text, " source", InputSourceName(event->source));
    RiWriterChar(&text, ' ');
    WriteDevice(&text, &event->device);
    RiWriterFieldU64(&text, " window", event->window);
    RiWriterFieldU64(&text, " display", event->display);
    RiWriterFieldU64(&text, " time", event->timestampMs);
    RiWriterChar(&text, ' ');
    WriteModifiers(&text, event->modifiers);
    RiWriterFieldText(&text, " space", InputSpaceName(event->space));

    kind = InputEventPayloadKind(event->type);
    switch (kind) {
    case RoboInputPayloadKeyboard:
        WriteKeyboardPayload(&text, &event->payload.keyboard);
        break;
    case RoboInputPayloadPointer:
        WritePointerPayload(&text, &event->payload.pointer);
        break;
    case RoboInputPayloadText:
        RiWriterText(&text, " text=\"");
        RiWriterText(&text, event->payload.text.text);
        RiWriterChar(&text, '"');
        break;
    default:
        /* No payload: the type has none, and printing "the payload is empty" would be printing a field
         * that does not exist on this event. */
        break;
    }

    if (event->hasTarget) {
        WriteTarget(&text, &event->target);
    }
    return RiWriterFinish(&text, outLength, diag);
}

Result InputQueueWrite(const InputQueue *queue,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     InputDiag *diag)
{
    InputQueueStats stats;
    InputWriter text;
    uint32_t index;

    if (!queue || !RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                               "a queue dump needs a queue, a buffer and somewhere to put the length");
    }
    if (GetInputQueueStats(queue, &stats) != RoboOk) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                               "the queue's statistics could not be read");
    }

    RiWriterInit(&text, buffer, capacity);
    RiWriterText(&text, "queue");
    RiWriterFieldU64(&text, " capacity", stats.capacity);
    RiWriterFieldU64(&text, " count", stats.count);
    RiWriterFieldU64(&text, " pushed", stats.pushed);
    RiWriterFieldU64(&text, " popped", stats.popped);
    RiWriterFieldU64(&text, " dropped", stats.dropped);
    RiWriterFieldU64(&text, " cleared", stats.cleared);
    RiWriterFieldU64(&text, " high_water", stats.highWater);
    RiWriterChar(&text, '\n');

    for (index = 0u; index < stats.count; index++) {
        /* One line at a time, into the bound-sized scratch buffer, and then appended to the caller's
         * buffer through the ordinary writer. That way a caller's buffer can be too small for the *dump*
         * without any single event line being too small for the *event* -- so the reported length is always
         * the length the whole text needs, and never a partial measurement. */
        char line[RoboInputDumpLineMax];
        InputEvent event;
        InputDiag lineDiag;
        size_t lineLength = 0u;

        InputDiagReset(&lineDiag);
        if (InputQueuePeekAt(queue, index, &event, &lineDiag) != RoboOk) {
            /* Cannot happen while the count above is the queue's own, and written as a refusal rather than
             * as an assumption: a dump that silently skipped an event would be worse than one that says it
             * could not read it. */
            return RoboInputDiag(diag, RoboInputDomainDump, RoboError,
                                   "a queued event could not be read");
        }
        if (InputEventWrite(&event, line, sizeof(line), &lineLength, &lineDiag) != RoboOk) {
            /* The stated bound is a promise: a line that did not fit it would mean the constant in the
             * public header is wrong, and the dump says so rather than emitting a truncated line. */
            return RoboInputDiag(diag, RoboInputDomainDump, RoboError,
                                   "an event line exceeded the module's stated bound");
        }
        RiWriterText(&text, line);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
