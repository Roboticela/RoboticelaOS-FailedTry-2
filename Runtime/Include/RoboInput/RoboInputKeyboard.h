/*
 * RoboInput keyboard payload (Phase 12.1).
 *
 * ## What it carries, and why it is not a second key vocabulary
 *
 * The physical and logical key identities are **RoboWindow's** (`PhysicalKey`, `LogicalKey`,
 * 7.10): a stable hardware position, and an interpreted identity under the active mapping. That vocabulary
 * is the runtime's answer to "which key", it is already the one a backend fills in, and a second one here
 * would be a second answer that could disagree. So this payload adds the three things a key *event* needs
 * and a key *identity* cannot carry, and nothing else:
 *
 *   **the action** -- down or up -- so that a payload alone says what happened rather than requiring the
 *   reader to look at the event's type. `InputKeyboardDataActionMatches` is the rule that keeps the
 *   two agreeing, and `InputEventIsValid` refuses an event where they do not;
 *
 *   **the repeat** -- a flag from the platform saying this down is an auto-repeat rather than a new press.
 *   It is a field and not a flag because a consumer that gets it wrong *inserts a character twice*, which is
 *   the kind of mistake that belongs in a named field a validator can insist on;
 *
 *   **the text** -- the printable character(s) this key produced, if any, bounded by
 *   `RoboInputTextMax`. Recorded here because a model that cannot record it would have to be widened
 *   later, and **not interpreted**: no composition, no dead keys, no IME, no layout mapping, and no
 *   guarantee that the buffer holds a whole grapheme -- those are Phase 12.8 and Phase 14, and this header's
 *   job is to have a place for their output rather than to produce it.
 *
 * ## What is deliberately absent
 *
 * No `code`/`key` string form (a JavaScript event object's shape is a later phase's business, and a
 * `robo.*` API would be built *from* this payload rather than instead of it); no repeat *count* (the
 * platform reports a flag, and inventing a count would mean inventing the timing that produces it); no
 * keymap, no modifier-only event, no composition state; and no unicode *code point* field separate from the
 * text, because a key that produces text produces it as UTF-8 and a caller that wants a code point can
 * decode it with RoboUnicode.
 *
 * ## What the modifiers mean here
 *
 * `InputEvent.modifiers` is the state **as of this event**, not after it: a `KeyDown` for the shift
 * key reports the modifiers in force *before* shift went down, which is what a consumer comparing a
 * keystroke against a binding needs. That is also what the platform reports, so there is no translation.
 */
#ifndef ROBO_INPUT_KEYBOARD_H
#define ROBO_INPUT_KEYBOARD_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTypes.h"

#include "RoboWindow/RoboWindowKey.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** This key is itself a modifier: shift, control, alt, meta, or one of the three locks. */
#define RoboInputKeyFlagModifier ((uint32_t)(1u << 0))
/** This key is on the numeric keypad. */
#define RoboInputKeyFlagNumpad ((uint32_t)(1u << 1))
/** The platform could not tell which physical key produced this (`physical` is `UNKNOWN`). */
#define RoboInputKeyFlagPhysicalUnknown ((uint32_t)(1u << 2))

/** Every defined key flag. */
#define RoboInputKeyFlagAll \
    (RoboInputKeyFlagModifier | RoboInputKeyFlagNumpad | RoboInputKeyFlagPhysicalUnknown)

/** One key event's payload, by value. */
typedef struct InputKeyboardData {
    /** Stable hardware position; `RoboPhysicalKeyUnknown` when the platform does not expose one. */
    PhysicalKey physical;
    /** Interpreted identity under the active mapping; `RoboLogicalKeyUnknown` when uninterpreted. */
    LogicalKey logical;
    /** Down or up. Must agree with the event type (`InputKeyboardDataActionMatches`). */
    KeyAction action;
    /** Non-zero when this down is an auto-repeat rather than a new press. Always 0 for an up. */
    int repeat;
    /** The backend's own scan code, or 0. Never a key id: it is not portable and nothing here reads it. */
    uint32_t scanCode;
    /** `ROBO_INPUT_KEY_FLAG_*` bits. */
    uint32_t flags;
    /** The printable text this key produced, or `""`. UTF-8, NUL-terminated, at most
     *  `RoboInputTextMax - 1` bytes. */
    char text[RoboInputTextMax];
} InputKeyboardData;

/** Clear @p data to "no key": unknown identities, no action, no text. Safe on NULL. */
void InputKeyboardDataInit(InputKeyboardData *data);

/**
 * Fill @p data.
 *
 * @param text may be NULL (no text); a fragment longer than the bound is **refused**, for the reason
 *             `RoboInputTypes.h` gives: a truncated keystroke is a wrong character.
 * @param repeat is normalized to 0/1, and forced to 0 for an `UP` action.
 *
 * `data` is cleared **first** and filled only if every argument is accepted, so a refusal leaves an empty
 * payload rather than a mixture of the refused call and the previous accepted one -- the rule the device and
 * target doors use as well. The `PHYSICAL_UNKNOWN` flag is derived from @p physical (`MODIFIER` and `NUMPAD`
 * too, from the key), so a caller cannot forget to say what the key is.
 */
Result InputKeyboardDataSet(InputKeyboardData *data,
                                           PhysicalKey physical,
                                           LogicalKey logical,
                                           KeyAction action,
                                           int repeat,
                                           const char *text);

/**
 * Non-zero when @p data is a payload this build may carry on a key event.
 *
 * The three rules, each of which a producer can get wrong: the action is defined (not `NONE`); the two key
 * identities are defined-or-`UNKNOWN` and the `PHYSICAL_UNKNOWN` flag agrees with `physical`; and the text
 * is terminated inside its capacity, with an `UP` carrying none (a release does not produce a character).
 */
int InputKeyboardDataIsValid(const InputKeyboardData *data);

/** The action a key event type implies, or `NONE` when @p type is not a key event. */
KeyAction InputKeyboardActionForEvent(int eventType);

/** Non-zero when @p type is a key event and @p action is the action it implies. */
int InputKeyboardDataActionMatches(const InputKeyboardData *data, int eventType);

/** Field-by-field equality, `text` compared as a string. Safe on NULL; two NULLs are equal. */
int InputKeyboardDataEqual(const InputKeyboardData *a,
                                   const InputKeyboardData *b);

/** Copy the payload's text into @p out, guaranteed terminated. Returns the length copied (excluding NUL). */
size_t InputKeyboardDataText(const InputKeyboardData *data,
                                     char *out,
                                     size_t capacity);

/** Non-zero when @p physical is one of the keys that is itself a modifier; 0 for `UNKNOWN` and for every
 *  ordinary key. Equivalent to `InputPhysicalKeyModifier(physical) != RoboKeyModNone`. */
int InputPhysicalKeyIsModifier(PhysicalKey physical);

/**
 * The modifier state @p physical produces, or `RoboKeyModNone` for an ordinary key.
 *
 * A side-specific key reports **both** its aggregate bit and its own -- `SHIFT_LEFT` yields
 * `RoboKeyModShift | RoboKeyModShiftLeft` -- because that is what a state update needs: a consumer
 * asking "is shift down?" and one asking "which shift?" get their answers from one value instead of two
 * fields that could disagree. `CAPS_LOCK`, `NUM_LOCK` and `SCROLL_LOCK` yield their own bits.
 */
KeyModifierFlags InputPhysicalKeyModifier(PhysicalKey physical);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_KEYBOARD_H */
