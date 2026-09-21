/*
 * RoboInput fundamental types (Phase 12.1): where an event came from, which device reported it, what the
 * modifier state was, and the limits the model is built around.
 *
 * ## Three vocabularies, and one of them is borrowed
 *
 * **Sources** are this module's: a *category* a handler can switch on without knowing a platform
 * ("a pointer moved" versus "a finger moved" versus "a game controller's stick moved"). The list is the
 * phase brief's, and the two entries that are not physical -- `VIRTUAL` and `REMOTE` -- are the reason the
 * vocabulary exists at all: a runtime that could only describe hardware would have no way to say that a
 * test, a script or a remote session produced an event, and every later layer that wanted to trust an
 * event would have to guess.
 *
 * **Devices** are this module's too, and deliberately thin: an id the runtime assigns, the source category,
 * a bounded *copied* name and two flags. It is the identity an event carries so that a handler can tell two
 * keyboards apart without a pointer to a platform object -- and a *name* rather than a handle, because the
 * debug dump has to be able to print it and an address would make two runs differ.
 *
 * **Modifiers are borrowed, and that is the decision.** `KeyModifierFlags` and its bits are
 * RoboWindow's (7.10), where a keyboard event already had to say whether shift was down. A second modifier
 * representation in this module would be a second answer to "is shift down" that could disagree with the
 * first, so `InputModifiers` is an *alias* of that type and the bits keep their single definition
 * in `RoboWindowKey.h`. What this module adds is the query side -- `InputModifiersHas`,
 * `robo_input_modifiers_known` and the compact name a dump reads.
 *
 * ## The limits, and why each is a bound rather than a preference
 *
 * `RoboInputDeviceNameMax` is a copied capacity: a device name is filled once by whoever creates the
 * identity and read by a dump, so it must not be a pointer into platform memory that outlives nothing.
 *
 * `RoboInputTextMax` is one keystroke's worth of UTF-8 and no more, and the limit is the phase's
 * boundary stated as a number: composition, dead keys, IME and multi-character input are Phase 12.8 and
 * Phase 14, and a model that sized a buffer for them now would be promising a behaviour it does not have.
 * Text *longer* than this is refused rather than truncated, because a truncated keystroke is a wrong
 * character and not a shorter event.
 *
 * The queue capacities are here because they bound an object every module agrees about the size of; see
 * `RoboInputQueue.h` for what happens at the bound.
 */
#ifndef ROBO_INPUT_TYPES_H
#define ROBO_INPUT_TYPES_H

#include "RoboBase/RoboError.h"

#include "RoboGraphics/RoboGraphicsGeometry.h"

#include "RoboWindow/RoboWindowKey.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Where an event came from                                            */
/* ------------------------------------------------------------------ */

/**
 * The category of input that produced an event.
 *
 * A category rather than a device: one mouse is one device and `MOUSE` is what it reports, so a handler
 * that cares about "a pointer" asks for `POINTER`, `TOUCH` and `TABLET` and a handler that cares about "a
 * mouse specifically" asks for `MOUSE`. `POINTER` exists for exactly that reason -- a backend that cannot
 * tell which kind of pointing device it has must be able to say so rather than pick one and be wrong.
 *
 * `UNKNOWN` is 0 so that a cleared event says nothing rather than claiming to be a keyboard, the same
 * "the cleared state is the honest one" rule the rest of this tree follows.
 */
typedef enum InputSource {
    /** Nothing: a cleared record's value. Never a valid event source. */
    RoboInputSourceUnknown = 0,
    /** A keyboard. */
    RoboInputSourceKeyboard = 1,
    /** A mouse: a pointer with buttons and a wheel. */
    RoboInputSourceMouse = 2,
    /** A pointing device of a kind the source cannot name. */
    RoboInputSourcePointer = 3,
    /** A touch digitizer. */
    RoboInputSourceTouch = 4,
    /** A tablet / pen digitizer. */
    RoboInputSourceTablet = 5,
    /** A game controller. */
    RoboInputSourceGameController = 6,
    /** A remote session's input. */
    RoboInputSourceRemote = 7,
    /** Input this runtime produced: a test, a script, a replay. */
    RoboInputSourceVirtual = 8
} InputSource;

/** Number of defined sources. */
#define RoboInputSourceCount 9u

/** Non-zero if @p source is a defined source. */
int InputSourceIsKnown(InputSource source);

/** Human-readable source name; never NULL; `"unknown"` when undefined. */
const char *InputSourceName(InputSource source);

/**
 * Non-zero when @p source is a *positioning* source: one whose events carry a place on the screen.
 *
 * The predicate a validator and a coordinate conversion both need, written once: a keyboard reports no
 * position, and an event from one must not carry coordinates any more than a pointer event may leave them
 * out.
 */
int InputSourceIsPositioning(InputSource source);

/**
 * The source category a RoboWindow mouse event belongs to, or `UNKNOWN` for a non-mouse event.
 *
 * The bridge's one piece of vocabulary mapping, exposed because a caller wiring a backend wants to ask
 * "which source would this window event become" without building the event. Mouse is the only category the
 * window layer distinguishes; a wheel is a mouse event there (7.11) and `POINTER` is what a generic
 * pointing backend would report through the same door.
 */
InputSource InputSourceForWindowEvent(int windowEventType);

/* ------------------------------------------------------------------ */
/* Which device reported it                                            */
/* ------------------------------------------------------------------ */

/** Runtime-assigned device identity (0 = none). */
typedef uint64_t InputDeviceId;

#define RoboInputDeviceIdNone ((InputDeviceId)0u)

/** Copied device name capacity, including the NUL (see this header's comment). */
#define RoboInputDeviceNameMax 32u

/** The device is the primary one of its category: the mouse the pointer moves, the main keyboard. */
#define RoboInputDeviceFlagPrimary ((uint32_t)(1u << 0))
/** The device is synthetic: a test, a script or a replay rather than hardware. */
#define RoboInputDeviceFlagSynthetic ((uint32_t)(1u << 1))

/** Every defined device flag. */
#define RoboInputDeviceFlagAll \
    (RoboInputDeviceFlagPrimary | RoboInputDeviceFlagSynthetic)

/** A device identity, by value: no pointer to any platform object. */
typedef struct RoboInputDevice {
    /** The id the runtime assigned, or @ref RoboInputDeviceIdNone. */
    InputDeviceId id;
    /** The category this device reports as; may be `UNKNOWN` when the identity is not filled in. */
    InputSource source;
    /** `ROBO_INPUT_DEVICE_FLAG_*` bits. */
    uint32_t flags;
    /** Copied, NUL-terminated name; `""` when the device has none. */
    char name[RoboInputDeviceNameMax];
} InputDevice;

/** Clear @p device to "no device". Safe on NULL. */
void InputDeviceInit(InputDevice *device);

/**
 * Non-zero when @p device is a record this build may carry on an event.
 *
 * A *well-formed* record, not a *known* device -- this module has no registry to check an id against, and
 * inventing one to validate would be the device layer this phase does not have. The three rules are
 * therefore about the record's own consistency: the source is known or `UNKNOWN`, the flags are defined
 * bits, and the name is terminated within its capacity (a name that fills the whole buffer without a NUL is
 * refused rather than read).
 */
int InputDeviceIsValid(const InputDevice *device);

/**
 * Fill @p device from an id, a source and an optional name.
 *
 * A name **longer than the capacity is refused** with `RoboInvalidArgument` and `device` is left cleared,
 * for the reason this header gives for the text bound: a truncated name is a different device's name.
 *
 * @param name may be NULL, which leaves the name empty.
 */
Result InputDeviceSet(InputDevice *device,
                                    InputDeviceId id,
                                    InputSource source,
                                    const char *name);

/** Copy @p device's name into @p out, guaranteed terminated. Returns the length copied (excluding NUL). */
size_t InputDeviceName(const InputDevice *device, char *out, size_t capacity);

/** Field-by-field equality, `name` compared as a string. Safe on NULL; two NULLs are equal. */
int InputDeviceEqual(const InputDevice *a, const InputDevice *b);

/* ------------------------------------------------------------------ */
/* Modifier state                                                      */
/* ------------------------------------------------------------------ */

/**
 * Modifier bits for an input event.
 *
 * **The same type as RoboWindow's**, deliberately: see this header's comment. Every bit RoboWindow defines
 * is available here -- shift, control, alt, meta, the three locks, and the left/right variants -- so a
 * caller asks the same question of an input event and a window event and gets the same answer.
 */
typedef KeyModifierFlags InputModifiers;

/** No modifiers down. */
#define RoboInputModifiersNone ((InputModifiers)RoboKeyModNone)

/** Bits this build defines: the seven categories plus the left/right variants. */
#define RoboInputModifierAll \
    (RoboKeyModShift | RoboKeyModCtrl | RoboKeyModAlt | RoboKeyModMeta | \
     RoboKeyModCapsLock | RoboKeyModNumLock | RoboKeyModScrollLock | \
     RoboKeyModShiftLeft | RoboKeyModShiftRight | RoboKeyModCtrlLeft | \
     RoboKeyModCtrlRight | RoboKeyModAltLeft | RoboKeyModAltRight | \
     RoboKeyModMetaLeft | RoboKeyModMetaRight)

/** Non-zero when @p modifiers carries only bits this build defines. */
int InputModifiersAreKnown(InputModifiers modifiers);

/** Non-zero when every bit of @p mask is down in @p modifiers. A zero mask is down in everything. */
int InputModifiersHas(InputModifiers modifiers, InputModifiers mask);

/**
 * Non-zero when @p modifiers carries **no side-specific bit**: the state was reported as "shift is down"
 * rather than "the left shift is down".
 *
 * A platform that does not distinguish the two sides produces aggregate states, and a consumer that wants to
 * know whether it is being told about sides at all asks this rather than guessing from a missing bit.
 * `NONE` is aggregate by this definition -- no modifiers came from no side -- which is consistent and is
 * what the suite pins.
 */
int InputModifiersIsAggregate(InputModifiers modifiers);

/**
 * Write the modifier state as the compact list a debug line reads: `None`, `Shift`, `Control+Alt`.
 *
 * Writes a NUL-terminated string, never a partial one: when the buffer cannot hold the whole list, the
 * buffer receives `"?"` and the return is the length that *would* have been needed -- the contract every
 * `_write` door in this tree uses, so a caller can size from the refusal.
 *
 * @param outLength non-NULL; receives the length written (excluding the NUL).
 */
Result InputModifiersWrite(InputModifiers modifiers,
                                         char *buffer,
                                         size_t capacity,
                                         size_t *outLength);

/** The name of one modifier *bit* (`"Shift"`, `"Control"`, `"CapsLock"`, ...); `"?"` for a mask with no
 *  single bit; never NULL. */
const char *InputModifierName(InputModifiers modifier);

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

/**
 * Capacity of one keystroke's text, including the NUL.
 *
 * Thirty-one bytes is a full UTF-8 grapheme with room to spare and no room for a composed sequence -- see
 * this header's comment. A longer fragment is refused, never truncated.
 */
#define RoboInputTextMax 32u

/** Default queue capacity, matching the window layer's own queue depth (7.1). */
#define RoboInputQueueCapacityDefault 128u

/** Largest capacity a caller may ask for; a bound on one allocation, not a policy. */
#define RoboInputQueueCapacityMax 1024u

/** Pointer identities are dense small integers: 0 is the primary mouse, 1.. the rest in arrival order. */
typedef uint32_t InputPointerId;

#define RoboInputPointerIdPrimary ((InputPointerId)0u)

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_TYPES_H */
