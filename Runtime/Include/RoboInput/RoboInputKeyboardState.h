/*
 * RoboInput keyboard state (Phase 13.2) — which keys are held, which modifiers are down, and when the
 * held key repeats.
 *
 * ## The state is *derived*, and that is the decision this header is about
 *
 * A keyboard's state has four things in it that look independent and are not: the keys held, the modifier
 * bits, the lock bits, and the repeating key. The tempting design is to keep all four as fields a caller
 * updates -- set the shift bit on a shift press, clear it on the release, and so on -- and it is exactly
 * the design that leaves a stuck modifier behind when a device is unplugged, when focus is lost, or when a
 * release is simply never delivered (which happens: a key held while the window system grabs the keyboard
 * produces a press and no release).
 *
 * So this record keeps **one** thing: the set of keys held, each with the device that holds it. Everything
 * else is computed:
 *
 *   **modifiers** are the union of `InputPhysicalKeyModifier` over the held keys, so a shift that is
 *   released clears its bit because the *key* went away rather than because somebody remembered to clear a
 *   flag. A left and a right shift held together yield the aggregate bit once, which is what
 *   `RoboKeyModShift | RoboKeyModShiftLeft` per key and a union over them gives;
 *
 *   **locks** are the exception that proves the rule, and the reason they are *not* derived: caps lock is
 *   on after its key is released, so a lock is a bit this record **toggles** on the press of a lock key and
 *   keeps. `InputPhysicalKeyModifier` still names the bit for a lock key -- it is what the key *is* -- and
 *   the lock bits are the ones this record masks out of the derived set and supplies itself.
 *
 * The consequence is the property the brief asks for: **a device disconnect cannot leave a key stuck**,
 * because releasing a device's keys is what *is* releasing them (`InputKeyboardStateDisconnect`), and the
 * modifiers that followed from them follow automatically.
 *
 * ## Auto-repeat is generated here, not by the controls
 *
 * A text field must not implement key repeat, and neither must a list, a menu or a game: the brief's rule
 * is that repeat is this layer's, and the reason is that a control implementing its own repeat is a control
 * whose repeat differs from the next control's. So the timing lives here:
 *
 *   `InputKeyboardStateApply` **arms** a repeat when a key is pressed, setting the moment the first repeat
 *   is due to `event->timestampMs + delay`;
 *
 *   `InputKeyboardStateRepeat` is the door a loop calls with *its* clock. It produces **one** repeat event
 *   per call when one is due and pushes the next deadline forward, so a caller that was away for a second
 *   catches up by calling until it says no -- which is what makes the behaviour testable without sleeping
 *   and deterministic under load;
 *
 *   a release, a repeat's own key changing, a disconnect and a reset all cancel it.
 *
 * The delay and interval are a *config* the caller sets, because the right numbers are a platform's
 * (a desktop's initial delay is longer than a console's). Zero interval means **no repeat**, which is a
 * configuration and not an error -- the honest way to say "this build does not auto-repeat".
 *
 * ## What the repeat event does *not* carry
 *
 * No text. The state knows which key repeats and when; **what that key means** is the keyboard layout's
 * answer (13.2d), and a repeat event carries the key identities with an empty text so that the pipeline
 * derives the character exactly as it would for a fresh press. That split is deliberate: a state that
 * remembered the text of a press would be a second place a character is decided, and it would be wrong the
 * moment a modifier changed while the key was held.
 *
 * ## The bound
 *
 * `RoboInputKeyboardHeldMax` simultaneous keys are tracked. A press beyond the bound is refused with a
 * diagnostic rather than silently dropped, because a dropped press is a key that will never be released in
 * this record and therefore a modifier that stays down until a reset -- the exact failure the derivation
 * above exists to prevent.
 */
#ifndef ROBO_INPUT_KEYBOARD_STATE_H
#define ROBO_INPUT_KEYBOARD_STATE_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputKeyboard.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many keys one state tracks at once. A bound on one small table, not a policy. */
#define RoboInputKeyboardHeldMax 16u

/** The three lock bits, together: the ones a lock key toggles and this record supplies. */
#define RoboInputKeyboardLockMask                                                     \
    ((InputModifiers)(RoboKeyModCapsLock | RoboKeyModNumLock | RoboKeyModScrollLock))

/**
 * The default auto-repeat timing: 500 ms before the first repeat, then one every 33 ms.
 *
 * A desktop's numbers, and a *default* rather than a rule: `InputKeyboardStateSetRepeat` replaces them, and
 * an interval of zero turns repeat off entirely.
 */
#define RoboInputKeyboardRepeatDelayMsDefault 500u
#define RoboInputKeyboardRepeatIntervalMsDefault 33u

/** Auto-repeat timing, as a value a caller owns. */
typedef struct InputKeyboardRepeatConfig {
    /** Milliseconds a key is held before the first repeat is due. */
    uint32_t delayMs;
    /** Milliseconds between repeats. **0 disables auto-repeat.** */
    uint32_t intervalMs;
} InputKeyboardRepeatConfig;

/** Fill @p config with the defaults above. Safe on NULL. */
void InputKeyboardRepeatConfigInit(InputKeyboardRepeatConfig *config);

/** Non-zero when @p config asks for repeat at all (an interval of zero does not). Safe on NULL (0). */
int InputKeyboardRepeatConfigIsEnabled(const InputKeyboardRepeatConfig *config);

/** One held key: what it is, who is holding it, and whether it is the repeating one. */
typedef struct InputKeyboardHeldKey {
    /** The stable hardware position. */
    PhysicalKey physical;
    /** The interpreted identity as it was reported. */
    LogicalKey logical;
    /** The device holding it, or @ref RoboInputDeviceIdNone when the press named none. */
    InputDeviceId device;
} InputKeyboardHeldKey;

/**
 * A keyboard's state, by value and owned by a caller.
 *
 * Held **by value** like `InputDeviceRegistry` and for the same reason: it has a stated bound, so it needs
 * no allocator and nothing to destroy. A caller with two keyboards keeps two of these, or one -- the record
 * tracks which device holds each key, so a single state can serve a whole system while still being able to
 * release one device's keys and nobody else's.
 */
typedef struct InputKeyboardState {
    /** The keys currently held, in press order. */
    InputKeyboardHeldKey held[RoboInputKeyboardHeldMax];
    /** How many are live. */
    uint32_t heldCount;
    /** `RoboInputKeyboardLockMask` bits: the locks that are on, which persist after their key is released. */
    InputModifiers locks;
    /** Repeat timing. */
    InputKeyboardRepeatConfig repeat;
    /** The key the pending repeat belongs to, or `RoboInputPhysicalKeyUnknown` when none is armed. */
    PhysicalKey repeatPhysical;
    /** The repeat's logical identity, for the event it produces. */
    LogicalKey repeatLogical;
    /** The device the repeating key came from. */
    InputDeviceId repeatDevice;
    /** When the next repeat is due, in the clock's own milliseconds. Read when @ref repeatArmed. */
    uint64_t repeatDueMs;
    /** Non-zero when a repeat is armed. */
    int repeatArmed;
} InputKeyboardState;

/** Clear @p state: nothing held, no locks, the default repeat timing, nothing armed. Safe on NULL. */
void InputKeyboardStateInit(InputKeyboardState *state);

/** Replace the repeat timing. Refused for NULL. A zero interval is a legitimate "repeat is off". */
Result InputKeyboardStateSetRepeat(InputKeyboardState *state,
                                   const InputKeyboardRepeatConfig *config,
                                   InputDiag *diag);

/**
 * Apply one key event: a press, a release or a repeat.
 *
 * The door a loop calls for every key event it delivers, *before* routing it, and the one place the held
 * set changes. @p event must be a key event (`InputEventTypeIsKey`); anything else is refused, because a
 * state that accepted a pointer event would have to decide what a pointer meant to a keyboard.
 *
 * A **press** of a lock key toggles its bit. A **press** of any other key adds the key and arms the repeat
 * (with the delay). A **press of a key already held** is not an error -- platforms resend -- and leaves the
 * set alone while re-arming the repeat. A **release** removes it and cancels the repeat when it was that
 * key's. A **repeat** event changes nothing in the held set; the timing is `InputKeyboardStateRepeat`'s.
 *
 * Refused with `RoboNotSupported` when the held table is full (see this header's comment) and with
 * `RoboInvalidArgument` for a non-key event or a NULL argument. A refused press changes **nothing**.
 */
Result InputKeyboardStateApply(InputKeyboardState *state,
                               const InputEvent *event,
                               InputDiag *diag);

/**
 * What the state looks like now.
 *
 * Derived on demand rather than stored (this header's comment), so there is no `modifiers` field to keep in
 * step: the answer is a function of the held set and the locks, and two callers asking twice cannot get two
 * answers.
 */
InputModifiers InputKeyboardStateModifiers(const InputKeyboardState *state);

/** Non-zero when every bit of @p mask is down. A zero mask is down in everything. */
int InputKeyboardStateHasModifier(const InputKeyboardState *state, InputModifiers mask);

/** The locks that are on (`RoboInputKeyboardLockMask` bits). */
InputModifiers InputKeyboardStateLocks(const InputKeyboardState *state);

/** Non-zero when @p physical is held. Safe on NULL (0). */
int InputKeyboardStateIsKeyDown(const InputKeyboardState *state, PhysicalKey physical);

/** Non-zero when any held key has @p logical as its interpreted identity. Safe on NULL (0). */
int InputKeyboardStateIsLogicalKeyDown(const InputKeyboardState *state, LogicalKey logical);

/** How many keys are held. 0 for NULL. */
uint32_t InputKeyboardStateHeldCount(const InputKeyboardState *state);

/** The held key at @p index in press order, or `RoboNotFound` past the end. */
Result InputKeyboardStateHeldAt(const InputKeyboardState *state,
                                uint32_t index,
                                InputKeyboardHeldKey *out,
                                InputDiag *diag);

/** Non-zero when any key from @p device is held. Safe on NULL (0). A device of `None` answers 0. */
int InputKeyboardStateHoldsDevice(const InputKeyboardState *state, InputDeviceId device);

/**
 * Release every key held by @p device, and disarm the repeat when it was that device's.
 *
 * The door hotplug runs on, and the whole of "a disconnect must not leave a key stuck": the keys go away,
 * and the modifiers that were derived from them go with them because they were derived. Returns the number
 * of keys released, so a caller can report *what* it let go of rather than that it tried.
 *
 * Not an error when the device holds nothing: a caller releasing a device it is not sure about is being
 * careful, and the count it gets back says so.
 */
uint32_t InputKeyboardStateDisconnect(InputKeyboardState *state, InputDeviceId device);

/**
 * Release every key and disarm the repeat, keeping the locks and the timing.
 *
 * The boot/reset door (13.78). Locks are kept deliberately: `ResetInputState` is about a lost keyboard
 * rather than about forgetting what the user asked for, and a caps lock that turned itself off when a window
 * was deactivated would be a bug with a plausible-looking cause.
 */
void InputKeyboardStateReleaseAll(InputKeyboardState *state);

/** Clear everything, including the locks, and restore the default timing: boot, not a lost keyboard. */
void InputKeyboardStateReset(InputKeyboardState *state);

/**
 * Non-zero when a repeat is due at @p nowMs.
 *
 * The door a loop uses to decide whether to call @ref InputKeyboardStateRepeat at all, which is the shape a
 * frame wants: ask until it says no, then go on.
 */
int InputKeyboardStateRepeatPending(const InputKeyboardState *state, uint64_t nowMs);

/**
 * Produce one repeat event when one is due, and push the next deadline forward.
 *
 * @param nowMs     the caller's monotonic clock, in the same units as the timestamps on the events it
 *                  applied. **Never a wall clock** (the module's standing rule).
 * @param outEvent  filled with a `KEY_REPEAT` event for the repeating key when one is due: the key
 *                  identities, the device, the **current** modifiers and an **empty** text (this header's
 *                  comment: the layout decides the character).
 * @param outHas    receives 1 when @p outEvent was filled, 0 when nothing was due.
 *
 * One repeat per call, and the next deadline is `nowMs + intervalMs` -- so a caller that was away for a
 * second catches up by calling until @p outHas is 0, and a caller that calls every frame sees the honest
 * cadence. Returns `RoboOk` with @p outHas 0 when nothing is due, which is the common answer and not an
 * error.
 */
Result InputKeyboardStateRepeat(InputKeyboardState *state,
                                uint64_t nowMs,
                                InputEvent *outEvent,
                                int *outHas,
                                InputDiag *diag);

/**
 * Room `InputKeyboardStateWrite` needs for the longest state, NUL included.
 *
 * A summary line plus one line per held key: a count only this header knows.
 */
#define RoboInputKeyboardStateDumpMax (128u + (RoboInputKeyboardHeldMax * 64u))

/**
 * Write @p state as text: a summary line, then one line per held key.
 *
 *     keyboard held=2 modifiers=shift+shift-left locks=none repeat=armed due=1500
 *     held A device=1
 *     held ShiftLeft device=1
 *
 * `"?"` and the needed length when the buffer is too small.
 */
Result InputKeyboardStateWrite(const InputKeyboardState *state,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag);

/**
 * Field-by-field equality.
 *
 * The deadline is **not** compared, and that is deliberate: two states that agree about what is held, what
 * is locked and which key will repeat are the same state, and the exact millisecond the next repeat lands on
 * is a detail of when the caller asked. A recording that compared it would fail on a slow machine.
 */
int InputKeyboardStateEqual(const InputKeyboardState *a, const InputKeyboardState *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_KEYBOARD_STATE_H */
