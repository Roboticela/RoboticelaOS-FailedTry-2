/*
 * RoboInput event model (Phase 12.1) — one event, one payload, one space.
 *
 * ## The shape
 *
 * An event is a **value**: `InputEvent` is trivially copyable, holds no pointer to anything the
 * runtime owns, and can therefore sit in a queue, be copied into a log, or be handed to a test without any
 * lifetime agreement. Everything a consumer needs to decide what it is looking at is *inside* it:
 *
 *   `type`       what happened (the vocabulary in §4 of the phase brief);
 *   `source`     which category of input reported it (RoboInputTypes.h);
 *   `device`     which device, by id and copied name (RoboInputTypes.h);
 *   `timestamp`  when, in milliseconds, or 0 when the platform does not say;
 *   `modifiers`  the modifier state **as of this event** (RoboInputTypes.h);
 *   `space`      the plane every position in the payload is on (RoboInputCoordinates.h), unset when the
 *                event carries no position at all;
 *   `window` / `display`  where the runtime believes it happened, as ids;
 *   `target`     what it is *about*, as inert identity (RoboInputTarget.h), filled in by a dispatcher;
 *   `payload`    the one payload the type owns, in a union.
 *
 * ## Why the payload is a union, and not three fields
 *
 * RoboWindow's event carries a keyboard payload *and* a mouse payload with a comment beside each saying
 * which types make it meaningful (7.9). That is affordable in a window layer whose payloads are small, and
 * it is not what this model wants: an input event is the thing a dispatcher *switches on*, so the mapping
 * from "what happened" to "which fields may be read" wants to be one fact rather than a convention. A union
 * makes the other payloads **absent** rather than merely meaningless, so a reader who reaches for
 * `payload.pointer` on a key event has written code the validator would already have refused, and the
 * debug dump cannot print a stale wheel delta that a previous event left in the struct.
 *
 * `InputEventPayloadKind()` is the public statement of that mapping, so a consumer that would
 * rather ask than switch has one door to ask through.
 *
 * ## Which fields are meaningful, and how strictly that is enforced
 *
 * `InputEventIsValid` refuses a record whose fields disagree about what it is. The rules, in full:
 *
 *   1. the type is defined and not `NONE`;
 *   2. the source is defined, and the type *allows* that source
 *      (`InputEventTypeAllowsSource`) -- a key event from a touch digitizer is refused;
 *   3. the device record is well-formed, and a device that names a source names *this* event's source;
 *   4. the modifiers are defined bits;
 *   5. the payload the type owns is **present, valid, and coherent with the type**:
 *      -- a key event's payload (`RoboInputKeyboard.h`) is valid *and* its action is the one the type
 *      implies, so a `KeyUp` cannot carry a press;
 *      -- a positioned event's payload (`RoboInputPointer.h`) is valid *and* suits the type: a move names
 *      no button, a press names one **and has it down in its mask**, a release names one and does **not**,
 *      a wheel names none, the `CANCELLED` flag is set exactly for the two cancel types, and a touch
 *      contact holds no buttons at all;
 *      -- a `TEXT_INPUT` event carries at least one byte (its *payload* may be empty -- a key that produced
 *      no text is ordinary -- but an event whose whole point is text may not be wordless);
 *      -- a hotplug event's descriptor is valid, and the identity on the event **is** its identity, so the
 *      event and the record it carries cannot name two different devices;
 *      -- a pen event's pen record is valid *and* its pointer half suits the type exactly as a pointer
 *      event's would (a pen move names no button, a pen down has its button down): a stylus is a pointer,
 *      and the rule is not relaxed because the hardware has more to say;
 *      -- a gamepad event's record is valid *and* carries the half its type implies (`BUTTON_DOWN`/
 *      `_UP` carry a button, `AXIS_CHANGED` carries an axis);
 *      -- a composition event's preedit record is valid, and a `COMMIT` or `CANCEL` event is not required
 *      to be empty -- a method may commit text it reports here;
 *   6. **the space is set exactly when the event has a position**: a pointer, wheel or touch event must
 *      name a space, and a key or text event must leave it unset. This is the rule that makes "no silent
 *      mixing" checkable: the commonest mistake is not converting between two planes, it is never saying
 *      which plane a number is in, and there is no honest answer to give for a keystroke;
 *   7. if a target is attached it is valid, it names no window the event does not, and -- for a **positioned**
 *      event -- its space equals the event's, because a target concluded in another plane than the
 *      positions it was concluded from would be two answers to one question. A key or text event has no
 *      plane of its own, so its target may name any: the plane a focused node's routing was concluded in is
 *      a different fact from where a pointer is, and refusing to carry it would make a key event untargetable.
 *
 * ## Ownership
 *
 * Value semantics throughout, the philosophy `RoboPaint`'s records and the display list already use: an
 * event is copied *into* the queue, a copy is what comes *out*, and no event ever holds a borrowed pointer
 * into a caller's stack, a layout tree or a document arena. Two identity types inside it are other
 * modules' (`HtmlNodeHandle`, `LayoutNodeId`) and both are inert by their own design;
 * nothing here resolves either of them.
 *
 * ## The vocabulary is appended to, never reordered
 *
 * The values are the numbers a backend writes into a record and a saved recording replays, so they are
 * **stable and positional**: a type may be appended and may never be inserted, because inserting one would
 * silently reinterpret every event already produced. Phase 13.2 appended twenty types to 12.1's thirteen --
 * device hotplug, auto-repeat, hover, pen, gamepad, composition, focus and window activation -- and
 * `input.conformance.vocabulary` asserts every value and the count, so the day that rule is broken is the
 * day the suite fails rather than the day a recording replays wrongly.
 *
 * ## What is absent
 *
 * No dispatch, no propagation, no capture, no gesture state, no event-object shape for JavaScript, and no
 * click synthesis: a `clickCount` is *carried* when a platform reports one and never invented here. Those
 * are the consumers of this model (13.4 onward, and Phase 15) rather than parts of it -- an event model
 * that dispatched would be the thing it is supposed to describe.
 */
#ifndef ROBO_INPUT_EVENT_H
#define ROBO_INPUT_EVENT_H

#include "RoboBase/RoboError.h"

#include "RoboDisplay/RoboDisplayTypes.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include "RoboInput/RoboInputComposition.h"
#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDevice.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputGamepad.h"
#include "RoboInput/RoboInputKeyboard.h"
#include "RoboInput/RoboInputPen.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include "RoboWindow/RoboWindowTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Event type vocabulary                                               */
/* ------------------------------------------------------------------ */

/**
 * What happened.
 *
 * The numbering is **stable and positional**: the values come from §4 of the phase brief in its order, and
 * `input.conformance.vocabulary` asserts each one, because a later phase that reordered them would silently
 * reinterpret every event a backend already produced. New types are appended and never inserted; a reader
 * that cannot be sure of that cannot write a switch over this type at all.
 *
 * `NONE` is 0 so a cleared event says nothing rather than claiming a press. It is refused by
 * `InputEventIsValid`, exactly as `UNKNOWN` is refused for a source.
 */
typedef enum InputEventType {
    /** Nothing: a cleared record's value. Never a valid event type. */
    RoboInputEventNone = 0,
    /** A key was pressed (including an auto-repeat; see the payload's `repeat`). */
    RoboInputEventKeyDown = 1,
    /** A key was released. */
    RoboInputEventKeyUp = 2,
    /** A pointer moved without any button state change. */
    RoboInputEventPointerMove = 3,
    /** A pointer's button went down. */
    RoboInputEventPointerDown = 4,
    /** A pointer's button came up. */
    RoboInputEventPointerUp = 5,
    /** The pointer left the system's tracking: a *cancellation*, not a release. */
    RoboInputEventPointerCancel = 6,
    /** A wheel or trackpad scrolled. */
    RoboInputEventWheel = 7,
    /** Printable text was produced. Producing it is later work; recording it is this one's. */
    RoboInputEventTextInput = 8,
    /** A touch contact began. */
    RoboInputEventTouchBegin = 9,
    /** A touch contact moved. */
    RoboInputEventTouchMove = 10,
    /** A touch contact ended normally. */
    RoboInputEventTouchEnd = 11,
    /** A touch contact ended abnormally: the system took it over. */
    RoboInputEventTouchCancel = 12,

    /* ---- Phase 13.2: appended, never inserted (see this header's comment). ---- */

    /** A device arrived and is now in the table. Carries the descriptor (`payload.device`). */
    RoboInputEventDeviceConnected = 13,
    /** A device left the table. Carries the descriptor it left with. */
    RoboInputEventDeviceDisconnected = 14,
    /** A key's auto-repeat sample: the key is still down. A distinct type from `KEY_DOWN` because the two
     *  difference is what a *consumer* needs -- a text field inserts on one and not the other. */
    RoboInputEventKeyRepeat = 15,
    /** A pointer entered the runtime's tracked area. A pointer payload, so where it entered is carried. */
    RoboInputEventPointerEnter = 16,
    /** A pointer left the runtime's tracked area, without being cancelled. */
    RoboInputEventPointerLeave = 17,
    /** A pen touched the surface. */
    RoboInputEventPenDown = 18,
    /** A pen moved: touching, or hovering when the payload says so. */
    RoboInputEventPenMove = 19,
    /** A pen left the surface. */
    RoboInputEventPenUp = 20,
    /** The pen's samples stopped being reported: a *cancellation*, as for a pointer. */
    RoboInputEventPenCancel = 21,
    /** A controller button went down. */
    RoboInputEventGamepadButtonDown = 22,
    /** A controller button came up. */
    RoboInputEventGamepadButtonUp = 23,
    /** A controller axis moved. */
    RoboInputEventGamepadAxisChanged = 24,
    /** An input method began composing. */
    RoboInputEventCompositionStart = 25,
    /** The composing text, its clause or its caret changed. */
    RoboInputEventCompositionUpdate = 26,
    /** The composition was accepted: the preedit becomes committed text. */
    RoboInputEventCompositionCommit = 27,
    /** The composition was discarded: the editor restores what it began over. */
    RoboInputEventCompositionCancel = 28,
    /** A focus target gained input focus. The target is the event's own (`target`). */
    RoboInputEventFocusIn = 29,
    /** A focus target lost input focus. The target is the one that lost it. */
    RoboInputEventFocusOut = 30,
    /** A window became the active one. */
    RoboInputEventWindowActivate = 31,
    /** A window stopped being the active one. */
    RoboInputEventWindowDeactivate = 32
} InputEventType;

/** Number of defined event types, including `NONE`. Asserted positionally by the conformance suite. */
#define RoboInputEventTypeCount 33u

/** Which payload an event type owns; the live member of `InputEvent.payload`. */
typedef enum InputPayloadKind {
    /** No payload: `NONE`. */
    RoboInputPayloadNone = 0,
    /** `payload.keyboard`. */
    RoboInputPayloadKeyboard = 1,
    /** `payload.pointer`: moves, buttons, cancels, wheels and every touch phase. */
    RoboInputPayloadPointer = 2,
    /** `payload.text`. */
    RoboInputPayloadText = 3,
    /** `payload.device`: the descriptor a hotplug event carries (13.2). */
    RoboInputPayloadDevice = 4,
    /** `payload.pen`: the pointer sample plus pressure, tilt and rotation (13.22). */
    RoboInputPayloadPen = 5,
    /** `payload.gamepad`: a button transition or an axis sample (13.53). */
    RoboInputPayloadGamepad = 6,
    /** `payload.composition`: an input method's preedit (13.37). */
    RoboInputPayloadComposition = 7
} InputPayloadKind;

/** Number of defined payload kinds. Asserted positionally by the conformance suite. */
#define RoboInputPayloadKindCount 8u

/** One text event's payload, by value. */
typedef struct InputTextData {
    /** The produced text, UTF-8, NUL-terminated, at most `RoboInputTextMax - 1` bytes. */
    char text[RoboInputTextMax];
} InputTextData;

/** Clear @p data to empty text. Safe on NULL. */
void InputTextDataInit(InputTextData *data);

/** Copy @p text; a NULL pointer clears it; a fragment longer than the bound is refused (this header's rule
 *  about truncated keystrokes, `RoboInputTypes.h`). */
Result InputTextDataSet(InputTextData *data, const char *text);

/** Non-zero when @p data is terminated inside its capacity. Empty text **is** valid here: an event that
 *  produced no character is a legitimate report (`KeyDown` of a modifier, a composition start). */
int InputTextDataIsValid(const InputTextData *data);

/** Length of @p data's text, excluding the NUL; 0 for NULL. */
size_t InputTextDataLength(const InputTextData *data);

/** Field-by-field equality, text compared as a string. Safe on NULL; two NULLs are equal. */
int InputTextDataEqual(const InputTextData *a, const InputTextData *b);

/**
 * A device that arrived or left, by value: the whole descriptor, not just its identity.
 *
 * The event's own `device` field already carries the identity -- and the door that fills this fills *that*
 * from the same record, so the two cannot disagree. What this adds is everything the identity cannot say:
 * the **kind of hardware**, what it **can report**, and where it came from. That is what makes a hotplug
 * event self-describing: a consumer or a log can interpret it without holding a registry, which is exactly
 * the question a reader of a device dump is asking.
 */
typedef struct InputDeviceData {
    /** The descriptor, whole (RoboInputDevice.h). */
    InputDeviceDescriptor descriptor;
} InputDeviceData;

/** Clear @p data: everything @ref InputDeviceDescriptorInit clears. Safe on NULL. */
void InputDeviceDataInit(InputDeviceData *data);

/** Non-zero when @p data carries a descriptor this build may put on a hotplug event. */
int InputDeviceDataIsValid(const InputDeviceData *data);

/** Field-by-field equality. Safe on NULL; two NULLs are equal. */
int InputDeviceDataEqual(const InputDeviceData *a, const InputDeviceData *b);

/** An event's payload, by value: exactly one member is live, chosen by the event's type. */
typedef union InputPayload {
    /** Live for `KEY_DOWN` / `KEY_UP` / `KEY_REPEAT`. */
    InputKeyboardData keyboard;
    /** Live for every pointer, wheel, enter, leave and touch type. */
    InputPointerData pointer;
    /** Live for `TEXT_INPUT`. */
    InputTextData text;
    /** Live for `DEVICE_CONNECTED` / `DEVICE_DISCONNECTED`. */
    InputDeviceData device;
    /** Live for the four pen types. */
    InputPenData pen;
    /** Live for the three gamepad types. */
    InputGamepadData gamepad;
    /** Live for the four composition types. */
    InputCompositionData composition;
} InputPayload;

/**
 * One input event, by value.
 *
 * Field order is deliberate: what happened, where it came from, when, in what state, on which plane, about
 * which window -- and then the payload. A reader who only wants to route reads the first eight fields; a
 * reader who wants to interpret reads the payload last.
 */
typedef struct InputEvent {
    /** What happened. */
    InputEventType type;
    /** Which category reported it. */
    InputSource source;
    /** Which device, as a copied identity (see `RoboInputTypes.h`). */
    InputDevice device;
    /** Milliseconds since an arbitrary epoch the runtime chose; 0 when unavailable. Never a wall clock. */
    uint64_t timestampMs;
    /** Modifier state as of this event. */
    InputModifiers modifiers;
    /** The plane every position in the payload is on; unset when the event carries no position. */
    InputSpace space;
    /** The window the runtime believes this event is about, or `RoboWindowIdNone`. */
    WindowId window;
    /** The display it happened on, or `RoboDisplayIdNone` when unknown. Informational. */
    DisplayId display;
    /** What the event is about, filled in by a dispatcher; read when @ref hasTarget. */
    InputTarget target;
    /** Non-zero when @ref target was set. */
    int hasTarget;
    /** The payload this event's type owns; see @ref InputPayloadKind. */
    InputPayload payload;
} InputEvent;

/* ------------------------------------------------------------------ */
/* Type queries                                                        */
/* ------------------------------------------------------------------ */

/** Non-zero if @p type is a defined event type (including `NONE`). */
int InputEventTypeIsKnown(InputEventType type);

/** Non-zero if @p type could be delivered: defined and not `NONE`. */
int InputEventTypeIsDeliverable(InputEventType type);

/** Human-readable type name; never NULL; `"unknown"` when undefined. */
const char *InputEventTypeName(InputEventType type);

/** Which payload @p type owns; `RoboInputPayloadNone` for `NONE` and undefined types. */
InputPayloadKind InputEventPayloadKind(InputEventType type);

/** Human-readable payload-kind name; never NULL; `"unknown"` when undefined. */
const char *InputPayloadKindName(InputPayloadKind kind);

/** Non-zero for `KEY_DOWN` / `KEY_UP`. */
int InputEventTypeIsKey(InputEventType type);

/** Non-zero for `POINTER_MOVE` / `_DOWN` / `_UP` / `_CANCEL` (not the touch phases, not the wheel). */
int InputEventTypeIsPointerButton(InputEventType type);

/** Non-zero for the four touch types. */
int InputEventTypeIsTouch(InputEventType type);

/** Non-zero for the four pen types. */
int InputEventTypeIsPen(InputEventType type);

/** Non-zero for the three gamepad types. */
int InputEventTypeIsGamepad(InputEventType type);

/** Non-zero for the four composition types. */
int InputEventTypeIsComposition(InputEventType type);

/** Non-zero for `DEVICE_CONNECTED` / `DEVICE_DISCONNECTED`. */
int InputEventTypeIsDevice(InputEventType type);

/** Non-zero for `FOCUS_IN` / `FOCUS_OUT`. */
int InputEventTypeIsFocus(InputEventType type);

/** Non-zero for `WINDOW_ACTIVATE` / `WINDOW_DEACTIVATE`. */
int InputEventTypeIsWindow(InputEventType type);

/** Non-zero for a type that ends a pointer, touch or pen sequence: `POINTER_UP`/`_CANCEL`,
 *  `TOUCH_END`/`_CANCEL`, `PEN_UP`/`_CANCEL`. */
int InputEventTypeIsTerminal(InputEventType type);

/** Non-zero when the type's payload carries a position: every pointer, wheel and touch type. */
int InputEventTypeHasPosition(InputEventType type);

/** Non-zero for `POINTER_UP` / `POINTER_CANCEL` / `TOUCH_END` / `TOUCH_CANCEL`: the terminal phases. */
int InputEventTypeIsRelease(InputEventType type);

/**
 * Non-zero when @p type may come from @p source.
 *
 * The rule, written once so the validator and a caller agree:
 *
 *   a key, repeat or text event comes from a keyboard, a remote session or something synthetic, and never
 *   from a pointer;
 *
 *   a pointer, wheel, enter or leave event comes from any *positioning* source;
 *
 *   a touch event comes from a positioning source too, but not from a mouse -- a `MOUSE` that reports a
 *   touch would be a backend mislabelling itself, and a validator that accepted it would hide the
 *   mislabelling from every layer above;
 *
 *   a pen event comes from a tablet, a remote session or something synthetic -- and **not** from a touch
 *   digitizer, for the reason a touch may not come from a mouse: a finger cannot report pressure or tilt,
 *   and a runtime that let a touch source claim one would be hiding a mislabelled stylus;
 *
 *   a gamepad event comes from a game controller, a remote session or something synthetic;
 *
 *   a composition event comes from where keystrokes come from, because a composition is what an input
 *   method does *with* keystrokes;
 *
 *   a hotplug, focus or window event comes from **any defined source**: they describe the system rather
 *   than a device category, and the source on them says which category the subject belongs to -- the
 *   keyboard that arrived, the pointer that moved focus -- which is a fact worth carrying rather than one
 *   to refuse.
 */
int InputEventTypeAllowsSource(InputEventType type, InputSource source);

/** The event type a mouse window event type maps to, or `NONE`. The bridge's vocabulary, exposed so a
 *  caller can ask without building an event. */
InputEventType InputEventTypeForWindowEvent(int windowEventType);

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

/** Clear @p event: no type, no position, no payload. Safe on NULL. */
void InputEventInit(InputEvent *event);

/**
 * Fill @p event as a key event.
 *
 * @param type     `KEY_DOWN` or `KEY_UP`; anything else is refused.
 * @param keyboard the payload; `InputKeyboardDataIsValid` must hold, and its action must match
 *                 @p type (`RoboInputKeyboard.h`).
 *
 * The device, timestamp, modifiers, window and target are left as they were, so a caller can set the
 * common fields once and then fill event after event -- which is how the bridge uses it.
 */
Result InputEventSetKeyboard(InputEvent *event,
                                            InputEventType type,
                                            InputSource source,
                                            const InputKeyboardData *keyboard,
                                            InputDiag *diag);

/**
 * Fill @p event as a positioned event: a pointer, wheel or touch phase.
 *
 * @param type    one of the nine positioned types; a key or text type is refused.
 * @param pointer the payload; `InputPointerDataIsValid` must hold.
 * @param space   the plane @p pointer's positions are on; `UNKNOWN` is refused (the rule of §9: an event
 *                that does not say which plane it is on is exactly the mistake this phase exists to
 *                prevent).
 */
Result InputEventSetPointer(InputEvent *event,
                                           InputEventType type,
                                           InputSource source,
                                           const InputPointerData *pointer,
                                           InputSpace space,
                                           InputDiag *diag);

/** Fill @p event as a `TEXT_INPUT` event. A NULL @p text is an empty event, which is valid (see the text
 *  payload's rule); a fragment longer than `RoboInputTextMax - 1` bytes is refused. */
Result InputEventSetText(InputEvent *event,
                                        InputSource source,
                                        const char *text,
                                        InputDiag *diag);

/**
 * Fill @p event as a key event, including an auto-repeat.
 *
 * The same door as @ref InputEventSetKeyboard except for one thing, and it is the reason the type exists
 * at all: @p type may be `KEY_REPEAT`, whose payload is a *down* with `repeat` set. A caller that has a
 * payload already saying `repeat` asks for `KEY_REPEAT` and the type is checked against the payload rather
 * than trusted: a `KEY_DOWN` carrying `repeat` is refused, because a consumer that switches on the type to
 * decide whether to insert a character would insert it twice.
 */
Result InputEventSetKeyRepeat(InputEvent *event,
                              InputSource source,
                              const InputKeyboardData *keyboard,
                              InputDiag *diag);

/**
 * Fill @p event as a pen event: a positioned type whose payload is the pointer sample plus what a stylus
 * knows.
 *
 * @param type  one of the four pen types; anything else is refused.
 * @param pen   the sample; `InputPenDataIsValid` must hold, and its pointer half must suit @p type exactly
 *              as a pointer event's would (a move names no button, a down has its button down, an up does
 *              not, a cancel says it is one).
 * @param space the plane; `UNKNOWN` is refused, as for any positioned event.
 */
Result InputEventSetPen(InputEvent *event,
                        InputEventType type,
                        InputSource source,
                        const InputPenData *pen,
                        InputSpace space,
                        InputDiag *diag);

/**
 * Fill @p event as a gamepad event.
 *
 * @param type one of `GAMEPAD_BUTTON_DOWN` / `_UP` / `GAMEPAD_AXIS_CHANGED`; anything else is refused.
 * @param data the record; it must carry the half @p type implies -- a button event with an axis, or the
 *             reverse, is refused because it would say two things at once.
 */
Result InputEventSetGamepad(InputEvent *event,
                            InputEventType type,
                            InputSource source,
                            const InputGamepadData *data,
                            InputDiag *diag);

/**
 * Fill @p event as a composition event.
 *
 * @param type    one of the four composition types; anything else is refused.
 * @param preedit the record; see @ref InputCompositionDataSet for what is refused.
 */
Result InputEventSetComposition(InputEvent *event,
                                InputEventType type,
                                InputSource source,
                                const InputCompositionData *preedit,
                                InputDiag *diag);

/**
 * Fill @p event as a device hotplug event, and fill its **identity** from @p descriptor.
 *
 * The door that makes the event and the record it carries agree by construction: the identity on the event
 * is copied out of the descriptor rather than passed beside it, so a caller cannot name one device on the
 * event and a different one in the payload. @p type is `DEVICE_CONNECTED` or `DEVICE_DISCONNECTED`.
 */
Result InputEventSetDeviceHotplug(InputEvent *event,
                                  InputEventType type,
                                  InputSource source,
                                  const InputDeviceDescriptor *descriptor,
                                  InputDiag *diag);

/**
 * Fill @p event as a `FOCUS_IN`, `FOCUS_OUT`, `WINDOW_ACTIVATE` or `WINDOW_DEACTIVATE` event.
 *
 * These four carry **no payload**, and the reason is worth stating: what they are about is the event's own
 * `target` (the node gaining or losing focus, filled in through @ref InputEventSetTarget) and `window`,
 * and the *other* end of the transition -- what had focus before, which window was active -- is knowledge
 * the layer that made the change already has, because it made it. A payload carrying both ends would be a
 * second copy of a fact with two owners, which is what this module refuses everywhere else.
 */
Result InputEventSetFocusOrWindow(InputEvent *event,
                                  InputEventType type,
                                  InputSource source,
                                  InputDiag *diag);

/* ------------------------------------------------------------------ */
/* Common fields                                                       */
/* ------------------------------------------------------------------ */

/** Copy @p device onto @p event. Refused for an invalid device, or one whose source disagrees with the
 *  event's (rule 3). */
Result InputEventSetDevice(InputEvent *event,
                                          const InputDevice *device,
                                          InputDiag *diag);

/** Set the timestamp. Any value is accepted; 0 means "the platform does not say", which is a fact and not
 *  an error. */
Result InputEventSetTimestamp(InputEvent *event, uint64_t timestampMs);

/** Set the modifier state. Refused for bits this build does not define. */
Result InputEventSetModifiers(InputEvent *event,
                                             InputModifiers modifiers,
                                             InputDiag *diag);

/** Set the window the event is about (the *routing* window, not the target's document tip). A zero id
 *  means "no window", which is a legitimate value: an event can arrive before routing has happened. */
Result InputEventSetWindow(InputEvent *event, WindowId window);

/** Set the display the event happened on. `RoboDisplayIdNone` means "unknown". */
Result InputEventSetDisplay(InputEvent *event, DisplayId display);

/**
 * Attach a routing target.
 *
 * Refused when: @p target is not valid; the event is positioned and @p target's space differs from the
 * event's (rule 7); or the target names a window other than the event's when the event already names one.
 * That last rule is the one that stops a dispatcher quietly retargeting an event at a window the runtime
 * never believed it was about -- a dispatcher that *means* to retarget sets the window first, which is a
 * deliberate second step rather than an accident.
 */
Result InputEventSetTarget(InputEvent *event,
                                          const InputTarget *target,
                                          InputDiag *diag);

/** Detach whatever target was attached. */
void InputEventClearTarget(InputEvent *event);

/* ------------------------------------------------------------------ */
/* Validation, copying, comparison                                     */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p event is a record this build may deliver.
 *
 * The seven rules are listed in full at the top of this header, and each one is asserted by a case in
 * `input.event.validation`. A NULL pointer is invalid, not a crash.
 */
int InputEventIsValid(const InputEvent *event);

/** Copy @p source onto @p destination, whole. Refused for NULL. Trivial by construction -- that an input
 *  event copies like a struct is the ownership answer this phase gives. */
Result InputEventCopy(InputEvent *destination, const InputEvent *source);

/** Field-by-field equality, payload compared per its kind. Safe on NULL; two NULLs are equal. */
int InputEventEqual(const InputEvent *a, const InputEvent *b);

/** The event's position in the payload's own plane, or `RoboNotSupported` for a type that has none. */
Result InputEventPosition(const InputEvent *event,
                                        GfxPoint *out,
                                        InputDiag *diag);

/**
 * The event's position converted into @p to, using @p route's geometry.
 *
 * The door a dispatcher calls: it refuses a type with no position (`RoboNotSupported`) and otherwise
 * forwards to `InputConvertPoint`, so the conversion exists once. The event is **not** modified --
 * an event stays in the plane it arrived in, and where a consumer wants it is the consumer's business.
 */
Result InputEventPositionIn(const InputEvent *event,
                                           InputSpace to,
                                           const InputRoute *route,
                                           GfxPoint *out,
                                           InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_EVENT_H */
