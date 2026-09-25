/*
 * RoboInput module doors (Phase 12.1) — see RoboInput.h.
 *
 * Version, capabilities and the model dump. There is no `InputInit` to go with these and that is the
 * design rather than an omission: this module owns no state, so there is nothing to initialize, nothing to
 * shut down, and no order in which a caller has to do either. What is here is the answer to "what is this
 * build?", which is three questions:
 *
 *   **which version is this?** -- `InputVersionString` / `InputGetVersion`, the pair every
 *   module in this tree exposes and the pair a caller storing an event could stamp beside it;
 *
 *   **what does it claim to do?** -- `InputGetCapabilities`, whose first block is what exists and
 *   whose second block is what deliberately does not. The zeros are the important half: a reader of this
 *   build finds device reading, dispatch, focus, composition, propagation, gestures and the event loop named
 *   and answered `0`, rather than left to be inferred from what is missing;
 *
 *   **what can its model express?** -- `InputWriteModel`, the vocabulary as text: every event type
 *   with the payload it owns and the facts about it, every source with whether it positions, every plane
 *   with whether this build can convert through it. A developer reads it once; a test asserts it whole, so a
 *   type silently gaining a payload kind or losing its position would fail rather than surprise.
 */

#include "RoboInput/RoboInput.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

const char *InputVersionString(void)
{
    return RoboInputVersionString;
}

Result InputGetVersion(I32 *outMajor, I32 *outMinor, I32 *outPatch)
{
    if (outMajor) {
        *outMajor = (I32)RoboInputVersionMajor;
    }
    if (outMinor) {
        *outMinor = (I32)RoboInputVersionMinor;
    }
    if (outPatch) {
        *outPatch = (I32)RoboInputVersionPatch;
    }
    return RoboOk;
}

int InputModelIsImplemented(void)
{
    return RoboInputModelImplemented ? 1 : 0;
}

int InputPlatformAdaptersAreImplemented(void)
{
    return RoboInputPlatformAdaptersImplemented ? 1 : 0;
}

Result InputGetCapabilities(InputCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->eventModel = RoboInputModelImplemented ? 1 : 0;
    out->coordinateSpaces = 1;
    out->targetIdentity = 1;
    out->eventQueue = 1;
    out->debugDump = 1;
    out->windowBridge = 1;

    out->deviceReading = 0;
    out->platformAdapters = RoboInputPlatformAdaptersImplemented ? 1 : 0;
    /* Phase 13.29--13.34: the *dispatcher* -- one hit test, the path an event travels, the three-phase walk
     * and the propagation context. What it claims is the *mechanism*; every dependency on the layer above is
     * one function pointer, and this module still resolves no node by itself. */
    out->dispatch = 1;
    /* Phase 13.25--13.28: *focus* -- one focused target per window scope, the lose-then-gain transition
     * record and the navigation chain a caller declares. */
    out->focus = 1;
    /* Phase 13.12/13.13: the *command layer*. `RoboInputCommand.c` is a platform-neutral vocabulary of
     * intentions that a keyboard, a menu, an accessibility tool or a script all speak; `RoboInputShortcut.c`
     * turns a chord into one of them without knowing what a scan code is; and `RoboInputTextBridge.c` is the
     * only file in this module that includes RoboText, which is what makes an editor reachable through a
     * command rather than through a key. It claims the *translation*: this layer has no caret, no selection
     * and no buffer, and `Undo`/`Redo` are named and refused where 12.22 has no history. */
    out->textEditing = 1;
    /* 13.37--13.39 supplies the *interface* an input method talks to, which is why `inputMethods` below is 1
     * -- but the wiring from that interface into RoboText's own `RoboTextComposition` is still a caller's, so
     * `textComposition` stays 0 rather than claiming a pipeline that does not exist end to end. */
    out->textComposition = 0;
    /* Phase 13.40/13.41: the clipboard, whose one by-value text buffer is what makes a copy of an *empty*
     * selection able to leave the clipboard alone rather than destroy it. */
    out->clipboard = 1;
    out->propagation = 1;
    /* Phase 13.23/13.24: the gesture sample, the arena and the shipped recognizer. */
    out->gestures = 1;
    out->eventLoop = 0;
    out->multiprocessInput = 0;

    /* Phase 13.2's first half: the device table. What it claims is a descriptor for every kind of hardware
     * the brief names, eleven capability bits, a connection state, and a fixed table whose disconnect
     * *removes* an entry -- so no stale device can be looked up, asked for as a type's primary, or left
     * holding a key. It does **not** claim that anything *reads* a device: `deviceReading` is still 0, and
     * stays 0 until a platform adapter exists (13.2's remaining half). */
    out->deviceTable = 1;
    /* And its second half: the keyboard *state*, whose held table is the one thing kept and whose modifiers
     * and locks are read off it -- plus the auto-repeat cadence, which is one event per call from a deadline
     * the state pushes forward. What it still does not claim is that anything reads a keyboard. */
    out->keyboardState = 1;

    /* Phase 13.9/13.10: the keyboard *layout* and its dead keys. What it claims is that a position plus a
     * modifier state becomes a character through a table, that caps lock is a case lock rather than a level,
     * that AltGr has two spellings and a control chord produces **no** text, and that one accent can wait for
     * the next key and be composed, repeated, replaced or rejected. What it does not claim is a layout for a
     * language whose table this build does not ship: `InputLayoutSetMapping` is how one arrives. */
    out->keyboardLayout = 1;
    out->deadKeys = 1;
    /* ... and 13.11: the text pipeline that turns a key into `TEXT_INPUT` events. It claims the separation
     * the brief makes mandatory, and it does **not** claim that the text reaches an editor -- `textEditing`
     * is still 0 above, and the command layer (13.12/13.13) is what will carry it there. */
    out->textInput = 1;

    /* Phase 13.14--13.19's first half: the *pointer state machine*. What it claims is that the pointers a
     * device has reported are tracked as state (position, button mask, plane, device) so a control can ask
     * "is the pointer still down over me?" without replaying the stream; that a click count is **derived**
     * from pointer, button, time and distance when the platform does not report one; that a capture belongs
     * to one target at a time and only its holder may release it; and that hover emits leave-then-enter with
     * a target never entered twice. What it does not claim is that anything *routes* or *hit-tests*:
     * `dispatch` is still 0 above, and the layer that owns the tree is the one that resolves a target and
     * tells this state about it. */
    out->pointerState = 1;
    out->pointerCapture = 1;
    out->pointerHover = 1;
    out->clickCounting = 1;
    out->wheelNormalization = 1;

    /* Phase 13.20/13.21: *touch* -- contacts keyed by the hardware's id, cancellation as an acknowledged
     * phase, and the one conversion into the pointer model. */
    out->touchInput = 1;
    /* Phase 13.22: the *pen* payload, whose pressure and tilt are capability-gated rather than assumed. */
    out->penInput = 1;
    /* Phase 13.53: the gamepad vocabulary and state. Its *navigation* mapping is 13.54 and is still 0. */
    out->gamepad = 1;
    out->gamepadNavigation = 0;
    /* Phase 13.43: the cursor, whose shape is decided by the hit test rather than by whichever control
     * happened to see the pointer last. */
    out->cursorManagement = 1;
    /* Phase 13.42: the drag session and its negotiation. */
    out->dragDrop = 1;
    /* Phase 13.37--13.39: the input-method context. The *method* and its candidate window are not here. */
    out->inputMethods = 1;
    /* Phase 13.35/13.36: scoped shortcut bindings and the routing of the commands they resolve to. */
    out->shortcuts = 1;
    out->commandRouting = 1;

    /* Phase 13.44/13.78: *hotplug and the reset* -- the session owns every table a device can put state in,
     * which is what makes "no stale state may remain" a claim with a number behind it. */
    out->deviceHotplug = 1;
    out->inputReset = 1;
    /* Phase 13.49--13.51: a recording, a replay through the same pipeline, synthetic input, and the rank
     * comparison that decides who may produce what. */
    out->inputReplay = 1;
    out->syntheticInput = 1;
    out->injectionBoundaries = RoboInputInjectionBoundariesImplemented ? 1 : 0;
    /* Named and not yet built, each in its own right rather than implied by a neighbour's flag. */
    out->eventCoalescing = 0;
    out->inputLatency = 0;
    out->accessibilityHooks = 0;

    out->maxPointers = RoboInputMaxPointers;
    out->maxCaptures = RoboInputMaxCaptures;
    out->scrollUnitCount = RoboInputScrollUnitCount;

    out->cursorTypeCount = RoboInputCursorTypeCount;
    out->maxCursorOverrides = RoboInputMaxCursorOverrides;
    out->dragOperationCount = RoboInputDragOperationCount;
    out->dragPhaseCount = RoboInputDragPhaseCount;
    out->dragPreviewBytes = RoboInputDragTextMax;
    out->imePurposeCount = RoboInputImeTextPurposeCount;
    out->imeMaxClauses = RoboInputImeMaxClauses;
    out->clipboardFormatsMax = RoboInputClipboardFormatsMax;
    out->clipboardFormatCount = RoboInputClipboardFormatCount;

    out->layoutCount = RoboInputLayoutIdCount;
    out->layoutLevelCount = RoboInputLayoutLevelCount;
    out->deadKeyCount = RoboInputDeadKeyCount;
    out->textEventsPerChord = RoboInputTextMaxEvents;

    out->deviceTypeCount = RoboInputDeviceTypeCount;
    out->deviceCapabilityCount = RoboInputDeviceCapabilityCount;
    out->eventTypeCount = RoboInputEventTypeCount;
    out->sourceCount = RoboInputSourceCount;
    out->spaceCount = RoboInputSpaceCount;
    out->version = RoboInputVersionString;
    return RoboOk;
}

/** One event type's line: what it is, which payload it owns, and the four facts about it. */
static void WriteTypeLine(InputWriter *text, InputEventType type)
{
    RiWriterText(text, "type ");
    RiWriterText(text, InputEventTypeName(type));
    RiWriterFieldText(text, " payload", InputPayloadKindName(InputEventPayloadKind(type)));
    RiWriterFieldU64(text, " position", InputEventTypeHasPosition(type) ? 1u : 0u);
    RiWriterFieldU64(text, " release", InputEventTypeIsRelease(type) ? 1u : 0u);
    RiWriterFieldU64(text, " key", InputEventTypeIsKey(type) ? 1u : 0u);
    RiWriterFieldU64(text, " touch", InputEventTypeIsTouch(type) ? 1u : 0u);
    RiWriterChar(text, '\n');
}

static void WriteSourceLine(InputWriter *text, InputSource source)
{
    RiWriterText(text, "source ");
    RiWriterText(text, InputSourceName(source));
    RiWriterFieldU64(text, " positioning", InputSourceIsPositioning(source) ? 1u : 0u);
    RiWriterChar(text, '\n');
}

static void WriteSpaceLine(InputWriter *text, InputSpace space)
{
    RiWriterText(text, "space ");
    RiWriterText(text, InputSpaceName(space));
    RiWriterFieldU64(text, " convertible", InputSpaceIsConvertible(space) ? 1u : 0u);
    RiWriterChar(text, '\n');
}

Result InputWriteModel(char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     InputDiag *diag)
{
    InputWriter text;
    uint32_t index;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                               "a model dump needs a buffer and somewhere to put the length");
    }

    RiWriterInit(&text, buffer, capacity);
    RiWriterText(&text, "input ");
    RiWriterText(&text, RoboInputVersionString);
    RiWriterFieldU64(&text, " model", InputModelIsImplemented() ? 1u : 0u);
    RiWriterFieldU64(&text, " adapters", InputPlatformAdaptersAreImplemented() ? 1u : 0u);
    RiWriterFieldU64(&text, " types", RoboInputEventTypeCount);
    RiWriterFieldU64(&text, " sources", RoboInputSourceCount);
    RiWriterFieldU64(&text, " spaces", RoboInputSpaceCount);
    RiWriterChar(&text, '\n');

    /* `NONE` is listed with the rest: it is part of the vocabulary -- the value a cleared record has -- and a
     * reader asking "what types exist?" wants to see it rather than to infer it from an absent line. */
    for (index = 0u; index < RoboInputEventTypeCount; index++) {
        WriteTypeLine(&text, (InputEventType)index);
    }
    for (index = 0u; index < RoboInputSourceCount; index++) {
        WriteSourceLine(&text, (InputSource)index);
    }
    for (index = 0u; index < RoboInputSpaceCount; index++) {
        WriteSpaceLine(&text, (InputSpace)index);
    }
    return RiWriterFinish(&text, outLength, diag);
}
