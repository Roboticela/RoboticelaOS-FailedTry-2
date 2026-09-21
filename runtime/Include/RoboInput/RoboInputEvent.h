/*
 * RoboInput event model (Phase 12.1) — one event, one payload, one space.
 *
 * ## The shape
 *
 * An event is a **value**: `robo_input_event_t` is trivially copyable, holds no pointer to anything the
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
 * `robo_input_event_payload_kind()` is the public statement of that mapping, so a consumer that would
 * rather ask than switch has one door to ask through.
 *
 * ## Which fields are meaningful, and how strictly that is enforced
 *
 * `robo_input_event_is_valid` refuses a record whose fields disagree about what it is. The rules, in full:
 *
 *   1. the type is defined and not `NONE`;
 *   2. the source is defined, and the type *allows* that source
 *      (`robo_input_event_type_allows_source`) -- a key event from a touch digitizer is refused;
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
 * modules' (`robo_html_node_handle_t`, `robo_layout_node_id_t`) and both are inert by their own design;
 * nothing here resolves either of them.
 *
 * ## What is absent
 *
 * No dispatch, no propagation, no capture, no focus, no hover, no click synthesis, no gesture state, no
 * composition, no event-object shape for JavaScript. `TextInput` exists in the vocabulary because the
 * keyboard payload must be able to record text at all; producing it -- IME, dead keys, remapping -- is
 * Phase 12.8 and Phase 14.
 */
#ifndef ROBO_INPUT_EVENT_H
#define ROBO_INPUT_EVENT_H

#include "RoboBase/RoboError.h"

#include "RoboDisplay/RoboDisplayTypes.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputKeyboard.h"
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
 * `robo_input_event_is_valid`, exactly as `UNKNOWN` is refused for a source.
 */
typedef enum robo_input_event_type {
    /** Nothing: a cleared record's value. Never a valid event type. */
    ROBO_INPUT_EVENT_NONE = 0,
    /** A key was pressed (including an auto-repeat; see the payload's `repeat`). */
    ROBO_INPUT_EVENT_KEY_DOWN = 1,
    /** A key was released. */
    ROBO_INPUT_EVENT_KEY_UP = 2,
    /** A pointer moved without any button state change. */
    ROBO_INPUT_EVENT_POINTER_MOVE = 3,
    /** A pointer's button went down. */
    ROBO_INPUT_EVENT_POINTER_DOWN = 4,
    /** A pointer's button came up. */
    ROBO_INPUT_EVENT_POINTER_UP = 5,
    /** The pointer left the system's tracking: a *cancellation*, not a release. */
    ROBO_INPUT_EVENT_POINTER_CANCEL = 6,
    /** A wheel or trackpad scrolled. */
    ROBO_INPUT_EVENT_WHEEL = 7,
    /** Printable text was produced. Producing it is a later phase; recording it is this one's. */
    ROBO_INPUT_EVENT_TEXT_INPUT = 8,
    /** A touch contact began. */
    ROBO_INPUT_EVENT_TOUCH_BEGIN = 9,
    /** A touch contact moved. */
    ROBO_INPUT_EVENT_TOUCH_MOVE = 10,
    /** A touch contact ended normally. */
    ROBO_INPUT_EVENT_TOUCH_END = 11,
    /** A touch contact ended abnormally: the system took it over. */
    ROBO_INPUT_EVENT_TOUCH_CANCEL = 12
} robo_input_event_type_t;

/** Number of defined event types, including `NONE`. Asserted positionally by the conformance suite. */
#define ROBO_INPUT_EVENT_TYPE_COUNT 13u

/** Which payload an event type owns; the live member of `robo_input_event_t.payload`. */
typedef enum robo_input_payload_kind {
    /** No payload: `NONE`. */
    ROBO_INPUT_PAYLOAD_NONE = 0,
    /** `payload.keyboard`. */
    ROBO_INPUT_PAYLOAD_KEYBOARD = 1,
    /** `payload.pointer`: moves, buttons, cancels, wheels and every touch phase. */
    ROBO_INPUT_PAYLOAD_POINTER = 2,
    /** `payload.text`. */
    ROBO_INPUT_PAYLOAD_TEXT = 3
} robo_input_payload_kind_t;

/** Number of defined payload kinds. Asserted positionally by the conformance suite. */
#define ROBO_INPUT_PAYLOAD_KIND_COUNT 4u

/** One text event's payload, by value. */
typedef struct robo_input_text_data {
    /** The produced text, UTF-8, NUL-terminated, at most `ROBO_INPUT_TEXT_MAX - 1` bytes. */
    char text[ROBO_INPUT_TEXT_MAX];
} robo_input_text_data_t;

/** Clear @p data to empty text. Safe on NULL. */
void robo_input_text_data_init(robo_input_text_data_t *data);

/** Copy @p text; a NULL pointer clears it; a fragment longer than the bound is refused (this header's rule
 *  about truncated keystrokes, `RoboInputTypes.h`). */
robo_result_t robo_input_text_data_set(robo_input_text_data_t *data, const char *text);

/** Non-zero when @p data is terminated inside its capacity. Empty text **is** valid here: an event that
 *  produced no character is a legitimate report (`KeyDown` of a modifier, a composition start). */
int robo_input_text_data_is_valid(const robo_input_text_data_t *data);

/** Length of @p data's text, excluding the NUL; 0 for NULL. */
size_t robo_input_text_data_length(const robo_input_text_data_t *data);

/** Field-by-field equality, text compared as a string. Safe on NULL; two NULLs are equal. */
int robo_input_text_data_equal(const robo_input_text_data_t *a, const robo_input_text_data_t *b);

/** An event's payload, by value: exactly one member is live, chosen by the event's type. */
typedef union robo_input_payload {
    /** Live for `KEY_DOWN` / `KEY_UP`. */
    robo_input_keyboard_data_t keyboard;
    /** Live for every pointer, wheel and touch type. */
    robo_input_pointer_data_t pointer;
    /** Live for `TEXT_INPUT`. */
    robo_input_text_data_t text;
} robo_input_payload_t;

/**
 * One input event, by value.
 *
 * Field order is deliberate: what happened, where it came from, when, in what state, on which plane, about
 * which window -- and then the payload. A reader who only wants to route reads the first eight fields; a
 * reader who wants to interpret reads the payload last.
 */
typedef struct robo_input_event {
    /** What happened. */
    robo_input_event_type_t type;
    /** Which category reported it. */
    robo_input_source_t source;
    /** Which device, as a copied identity (see `RoboInputTypes.h`). */
    robo_input_device_t device;
    /** Milliseconds since an arbitrary epoch the runtime chose; 0 when unavailable. Never a wall clock. */
    uint64_t timestamp_ms;
    /** Modifier state as of this event. */
    robo_input_modifiers_t modifiers;
    /** The plane every position in the payload is on; unset when the event carries no position. */
    robo_input_space_t space;
    /** The window the runtime believes this event is about, or `ROBO_WINDOW_ID_NONE`. */
    robo_window_id_t window;
    /** The display it happened on, or `ROBO_DISPLAY_ID_NONE` when unknown. Informational. */
    robo_display_id_t display;
    /** What the event is about, filled in by a dispatcher; read when @ref has_target. */
    robo_input_target_t target;
    /** Non-zero when @ref target was set. */
    int has_target;
    /** The payload this event's type owns; see @ref robo_input_payload_kind_t. */
    robo_input_payload_t payload;
} robo_input_event_t;

/* ------------------------------------------------------------------ */
/* Type queries                                                        */
/* ------------------------------------------------------------------ */

/** Non-zero if @p type is a defined event type (including `NONE`). */
int robo_input_event_type_is_known(robo_input_event_type_t type);

/** Non-zero if @p type could be delivered: defined and not `NONE`. */
int robo_input_event_type_is_deliverable(robo_input_event_type_t type);

/** Human-readable type name; never NULL; `"unknown"` when undefined. */
const char *robo_input_event_type_name(robo_input_event_type_t type);

/** Which payload @p type owns; `ROBO_INPUT_PAYLOAD_NONE` for `NONE` and undefined types. */
robo_input_payload_kind_t robo_input_event_payload_kind(robo_input_event_type_t type);

/** Human-readable payload-kind name; never NULL; `"unknown"` when undefined. */
const char *robo_input_payload_kind_name(robo_input_payload_kind_t kind);

/** Non-zero for `KEY_DOWN` / `KEY_UP`. */
int robo_input_event_type_is_key(robo_input_event_type_t type);

/** Non-zero for `POINTER_MOVE` / `_DOWN` / `_UP` / `_CANCEL` (not the touch phases, not the wheel). */
int robo_input_event_type_is_pointer_button(robo_input_event_type_t type);

/** Non-zero for the four touch types. */
int robo_input_event_type_is_touch(robo_input_event_type_t type);

/** Non-zero when the type's payload carries a position: every pointer, wheel and touch type. */
int robo_input_event_type_has_position(robo_input_event_type_t type);

/** Non-zero for `POINTER_UP` / `POINTER_CANCEL` / `TOUCH_END` / `TOUCH_CANCEL`: the terminal phases. */
int robo_input_event_type_is_release(robo_input_event_type_t type);

/**
 * Non-zero when @p type may come from @p source.
 *
 * The rule, written once so the validator and a caller agree: a key or text event comes from a keyboard, a
 * remote session or something synthetic, and never from a pointer; a pointer or wheel event comes from any
 * *positioning* source; a touch event comes from a positioning source too, but not from a mouse -- a
 * `MOUSE` that reports a touch would be a backend mislabelling itself, and a validator that accepted it
 * would hide the mislabelling from every layer above.
 */
int robo_input_event_type_allows_source(robo_input_event_type_t type, robo_input_source_t source);

/** The event type a mouse window event type maps to, or `NONE`. The bridge's vocabulary, exposed so a
 *  caller can ask without building an event. */
robo_input_event_type_t robo_input_event_type_for_window_event(int window_event_type);

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

/** Clear @p event: no type, no position, no payload. Safe on NULL. */
void robo_input_event_init(robo_input_event_t *event);

/**
 * Fill @p event as a key event.
 *
 * @param type     `KEY_DOWN` or `KEY_UP`; anything else is refused.
 * @param keyboard the payload; `robo_input_keyboard_data_is_valid` must hold, and its action must match
 *                 @p type (`RoboInputKeyboard.h`).
 *
 * The device, timestamp, modifiers, window and target are left as they were, so a caller can set the
 * common fields once and then fill event after event -- which is how the bridge uses it.
 */
robo_result_t robo_input_event_set_keyboard(robo_input_event_t *event,
                                            robo_input_event_type_t type,
                                            robo_input_source_t source,
                                            const robo_input_keyboard_data_t *keyboard,
                                            robo_input_diag_t *diag);

/**
 * Fill @p event as a positioned event: a pointer, wheel or touch phase.
 *
 * @param type    one of the nine positioned types; a key or text type is refused.
 * @param pointer the payload; `robo_input_pointer_data_is_valid` must hold.
 * @param space   the plane @p pointer's positions are on; `UNKNOWN` is refused (the rule of §9: an event
 *                that does not say which plane it is on is exactly the mistake this phase exists to
 *                prevent).
 */
robo_result_t robo_input_event_set_pointer(robo_input_event_t *event,
                                           robo_input_event_type_t type,
                                           robo_input_source_t source,
                                           const robo_input_pointer_data_t *pointer,
                                           robo_input_space_t space,
                                           robo_input_diag_t *diag);

/** Fill @p event as a `TEXT_INPUT` event. A NULL @p text is an empty event, which is valid (see the text
 *  payload's rule); a fragment longer than `ROBO_INPUT_TEXT_MAX - 1` bytes is refused. */
robo_result_t robo_input_event_set_text(robo_input_event_t *event,
                                        robo_input_source_t source,
                                        const char *text,
                                        robo_input_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Common fields                                                       */
/* ------------------------------------------------------------------ */

/** Copy @p device onto @p event. Refused for an invalid device, or one whose source disagrees with the
 *  event's (rule 3). */
robo_result_t robo_input_event_set_device(robo_input_event_t *event,
                                          const robo_input_device_t *device,
                                          robo_input_diag_t *diag);

/** Set the timestamp. Any value is accepted; 0 means "the platform does not say", which is a fact and not
 *  an error. */
robo_result_t robo_input_event_set_timestamp(robo_input_event_t *event, uint64_t timestamp_ms);

/** Set the modifier state. Refused for bits this build does not define. */
robo_result_t robo_input_event_set_modifiers(robo_input_event_t *event,
                                             robo_input_modifiers_t modifiers,
                                             robo_input_diag_t *diag);

/** Set the window the event is about (the *routing* window, not the target's document tip). A zero id
 *  means "no window", which is a legitimate value: an event can arrive before routing has happened. */
robo_result_t robo_input_event_set_window(robo_input_event_t *event, robo_window_id_t window);

/** Set the display the event happened on. `ROBO_DISPLAY_ID_NONE` means "unknown". */
robo_result_t robo_input_event_set_display(robo_input_event_t *event, robo_display_id_t display);

/**
 * Attach a routing target.
 *
 * Refused when: @p target is not valid; the event is positioned and @p target's space differs from the
 * event's (rule 7); or the target names a window other than the event's when the event already names one.
 * That last rule is the one that stops a dispatcher quietly retargeting an event at a window the runtime
 * never believed it was about -- a dispatcher that *means* to retarget sets the window first, which is a
 * deliberate second step rather than an accident.
 */
robo_result_t robo_input_event_set_target(robo_input_event_t *event,
                                          const robo_input_target_t *target,
                                          robo_input_diag_t *diag);

/** Detach whatever target was attached. */
void robo_input_event_clear_target(robo_input_event_t *event);

/* ------------------------------------------------------------------ */
/* Validation, copying, comparison                                     */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when @p event is a record this build may deliver.
 *
 * The seven rules are listed in full at the top of this header, and each one is asserted by a case in
 * `input.event.validation`. A NULL pointer is invalid, not a crash.
 */
int robo_input_event_is_valid(const robo_input_event_t *event);

/** Copy @p source onto @p destination, whole. Refused for NULL. Trivial by construction -- that an input
 *  event copies like a struct is the ownership answer this phase gives. */
robo_result_t robo_input_event_copy(robo_input_event_t *destination, const robo_input_event_t *source);

/** Field-by-field equality, payload compared per its kind. Safe on NULL; two NULLs are equal. */
int robo_input_event_equal(const robo_input_event_t *a, const robo_input_event_t *b);

/** The event's position in the payload's own plane, or `ROBO_NOT_SUPPORTED` for a type that has none. */
robo_result_t robo_input_event_position(const robo_input_event_t *event,
                                        robo_gfx_point_t *out,
                                        robo_input_diag_t *diag);

/**
 * The event's position converted into @p to, using @p route's geometry.
 *
 * The door a dispatcher calls: it refuses a type with no position (`ROBO_NOT_SUPPORTED`) and otherwise
 * forwards to `robo_input_convert_point`, so the conversion exists once. The event is **not** modified --
 * an event stays in the plane it arrived in, and where a consumer wants it is the consumer's business.
 */
robo_result_t robo_input_event_position_in(const robo_input_event_t *event,
                                           robo_input_space_t to,
                                           const robo_input_route_t *route,
                                           robo_gfx_point_t *out,
                                           robo_input_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_EVENT_H */
