/*
 * RoboInput device abstraction (Phase 13.2) — what a device *is*, and the table that owns them.
 *
 * ## Two records, and why the identity is not the descriptor
 *
 * `RoboInputTypes.h` already has `InputDevice`: an id, a source, two flags and a copied name. It is what
 * an **event** carries, and it is deliberately small, because every event in the queue holds one and an
 * event must stay a value a caller can copy. What it cannot say is everything else about a device -- what
 * kind of hardware it is, what it can report, where it came from -- and putting those on the event would
 * make the common case pay for the rare one.
 *
 * So there are two records, and the rule between them is one sentence: **an event carries the identity; a
 * device table holds the description.** `InputDeviceDescriptor` is the description; `InputDevice` inside it
 * is the identity, so the two can never disagree about an id or a name, because there is only one place
 * they are written.
 *
 * ## Type is not source, and the difference is the whole reason both exist
 *
 * `InputSource` (RoboInputTypes.h) is the *category of input a device reports*: what a handler switches
 * on when it wants to know whether an event is a keystroke, a pointer or a stick. `InputDeviceType` is the
 * *kind of hardware*: what a user would call the thing. They are related and not the same, and the two
 * cases that prove it are the ones a runtime meets on its first day:
 *
 *   a **touchpad** is a `TOUCHPAD` that reports `MOUSE`/`POINTER` events -- it positions a pointer, it has
 *   no absolute coordinates and no contacts -- so a handler that tested the *type* to decide whether to
 *   expect touches would be wrong, and a handler that tested the *source* would be right;
 *
 *   a **pen** is a `PEN` that reports `TABLET` events carrying pressure and tilt; a tablet that also
 *   reports touch reports `TOUCH` for those contacts from the *same* identity, which is why the source
 *   is on the event rather than only on the device.
 *
 * `InputDeviceTypeDefaultSource` is the mapping for the ordinary case, stated once, and it is a *default*
 * rather than a constraint: a caller that knows better sets the source itself, which is what a combined
 * digitizer does.
 *
 * ## Capabilities are what a device *can* report, and absence is a fact
 *
 * Eleven bits, the phase brief's list in its order, and a bit clear means **this device will not report
 * that** -- not "this runtime does not understand it". That distinction is the one a consumer needs:
 * "this pointer has no pressure" and "pressure is not modelled yet" call for different code, and a model
 * whose only vocabulary was a capability bit could not tell them apart. The second of the two is answered
 * by `InputCapabilities` (RoboInput.h), which is about the *build*; the first is answered here, and it is
 * about the *hardware*. `InputDeviceDescriptorIsValid` enforces the consistency between the two that is
 * checkable: a device whose type is `KEYBOARD` must carry `KEYBOARD_KEYS`, and a `GAMEPAD` must carry at
 * least one of the two gamepad bits, because a descriptor that named a kind of hardware while denying
 * every capability of it would be a record that says two things at once.
 *
 * ## The registry, and why it is a value rather than an allocation
 *
 * `InputDeviceRegistry` is a **fixed-capacity table of descriptors held inline**, so it is a struct a
 * caller owns and initialises rather than a pointer it has to remember to destroy -- the same choice
 * `InputEvent` makes about being a value, applied to the one piece of state the input layer has that is
 * not a queue. Sixteen devices is the bound, and it is stated as a constant a caller can read
 * (`RoboInputDeviceRegistryMax`) rather than discovered by refusal, because a device table that grew
 * would need an allocator and this module has one allocation site (`InputQueue`) on purpose.
 *
 * **Disconnect removes, it does not mark.** The brief's rule for hotplug is that no stale state may
 * remain, and a table that kept a disconnected device around with a flag would leave exactly the state
 * that rule is about: a lookup by id that still succeeds, a primary-of-type that still answers, a key
 * still held. `InputDeviceRegistryDisconnect` therefore takes the entry out and hands the caller the
 * descriptor it removed, which is what a logger wants and what a cascade needs -- the caller knows which
 * id to release keys and captures for without having to remember it first.
 *
 * The id counter is in the registry and not on a device, because ids are the registry's to issue: a
 * caller connecting a device it has no id for passes `RoboInputDeviceIdNone` and receives one, which is
 * what makes two keyboards separable without a platform handle.
 */
#ifndef ROBO_INPUT_DEVICE_H
#define ROBO_INPUT_DEVICE_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Kind of hardware                                                    */
/* ------------------------------------------------------------------ */

/**
 * The kind of device this is, as a person would name it.
 *
 * Not the source (see this header's comment): the type describes the hardware, the source describes what
 * it reports. `UNKNOWN` is 0 so that a cleared descriptor claims no kind rather than claiming a keyboard.
 */
typedef enum InputDeviceType {
    /** Nothing: a cleared record's value. */
    RoboInputDeviceTypeUnknown = 0,
    /** A keyboard: a set of keys, no pointer. */
    RoboInputDeviceTypeKeyboard = 1,
    /** A mouse: a relative pointer with buttons and usually a wheel. */
    RoboInputDeviceTypeMouse = 2,
    /** A touchscreen: an absolute multi-contact digitizer over a display. */
    RoboInputDeviceTypeTouchscreen = 3,
    /** A touchpad: a relative pointer with contacts, and no absolute position. */
    RoboInputDeviceTypeTouchpad = 4,
    /** A pen / stylus digitizer: absolute, with pressure and tilt where the hardware has them. */
    RoboInputDeviceTypePen = 5,
    /** A game controller. */
    RoboInputDeviceTypeGamepad = 6
} InputDeviceType;

/** Number of defined device types. Asserted positionally by `input.device.vocabulary`. */
#define RoboInputDeviceTypeCount 7u

/** Non-zero if @p type is a defined device type (including `UNKNOWN`). */
int InputDeviceTypeIsKnown(InputDeviceType type);

/** Human-readable type name; never NULL; `"unknown"` when undefined. */
const char *InputDeviceTypeName(InputDeviceType type);

/**
 * The source a device of @p type reports as in the ordinary case, or `UNKNOWN` for `UNKNOWN`.
 *
 * The mapping this header's comment motivates: a keyboard reports `KEYBOARD`, a mouse and a touchpad
 * report `MOUSE`, a touchscreen reports `TOUCH`, a pen reports `TABLET`, a gamepad reports
 * `GAME_CONTROLLER`. It is a **default**, not a rule -- a combined pen-and-touch digitizer reports both
 * `TABLET` and `TOUCH` from one identity, so a descriptor's source is a field the caller sets and this
 * door only answers what it would have been.
 */
InputSource InputDeviceTypeDefaultSource(InputDeviceType type);

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

/** The device reports key presses and releases. */
#define RoboInputDeviceCapabilityKeyboardKeys ((uint32_t)(1u << 0))
/** The device positions a pointer. */
#define RoboInputDeviceCapabilityPointer ((uint32_t)(1u << 1))
/** The device's positions are absolute (a screen point), not a movement. */
#define RoboInputDeviceCapabilityAbsolutePosition ((uint32_t)(1u << 2))
/** The device's positions are a movement (a delta) rather than a place. */
#define RoboInputDeviceCapabilityRelativeMotion ((uint32_t)(1u << 3))
/** The device has buttons. */
#define RoboInputDeviceCapabilityButtons ((uint32_t)(1u << 4))
/** The device can report more than one contact at once. */
#define RoboInputDeviceCapabilityMultiTouch ((uint32_t)(1u << 5))
/** The device reports contact pressure. */
#define RoboInputDeviceCapabilityPressure ((uint32_t)(1u << 6))
/** The device reports pen tilt. */
#define RoboInputDeviceCapabilityTilt ((uint32_t)(1u << 7))
/** The device has a wheel or a scroll surface. */
#define RoboInputDeviceCapabilityWheel ((uint32_t)(1u << 8))
/** The device has gamepad buttons. */
#define RoboInputDeviceCapabilityGamepadButtons ((uint32_t)(1u << 9))
/** The device has gamepad axes. */
#define RoboInputDeviceCapabilityGamepadAxes ((uint32_t)(1u << 10))

/** Every defined capability. */
#define RoboInputDeviceCapabilityAll                                                    \
    (RoboInputDeviceCapabilityKeyboardKeys | RoboInputDeviceCapabilityPointer |          \
     RoboInputDeviceCapabilityAbsolutePosition | RoboInputDeviceCapabilityRelativeMotion | \
     RoboInputDeviceCapabilityButtons | RoboInputDeviceCapabilityMultiTouch |             \
     RoboInputDeviceCapabilityPressure | RoboInputDeviceCapabilityTilt |                  \
     RoboInputDeviceCapabilityWheel | RoboInputDeviceCapabilityGamepadButtons |           \
     RoboInputDeviceCapabilityGamepadAxes)

/** Number of defined capability bits. Asserted positionally by `input.device.vocabulary`. */
#define RoboInputDeviceCapabilityCount 11u

/** Non-zero when @p capabilities carries only bits this build defines. */
int InputDeviceCapabilitiesAreKnown(uint32_t capabilities);

/** Non-zero when every bit of @p mask is set in @p capabilities. A zero mask is set in everything. */
int InputDeviceCapabilitiesHas(uint32_t capabilities, uint32_t mask);

/** The name of the capability bit at @p index in the brief's order, or NULL past the last one. */
const char *InputDeviceCapabilityName(uint32_t index);

/** The single-bit mask at @p index in the brief's order, or 0 past the last one. */
uint32_t InputDeviceCapabilityMask(uint32_t index);

/**
 * Write @p capabilities as the compact list a debug line reads: `none`, `pointer+wheel+buttons`.
 *
 * Lower case, `+`-separated, `none` for an empty set -- the convention `InputButtonMaskWrite` uses, so a
 * reader scanning a line sees one vocabulary rather than two. `"?"` and the needed length when the buffer
 * is too small.
 */
Result InputDeviceCapabilitiesWrite(uint32_t capabilities,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength);

/* ------------------------------------------------------------------ */
/* Connection state                                                    */
/* ------------------------------------------------------------------ */

/** Where a device is in its lifecycle. */
typedef enum InputDeviceConnection {
    /** Nothing: a cleared record's value. */
    RoboInputDeviceConnectionUnknown = 0,
    /** The device is present and reporting. */
    RoboInputDeviceConnectionConnected = 1,
    /** The device is present but suspended: a lid switch, a configuration change. */
    RoboInputDeviceConnectionSuspended = 2
} InputDeviceConnection;

/** Number of defined connection states. Asserted positionally by `input.device.vocabulary`. */
#define RoboInputDeviceConnectionCount 3u

/** Non-zero if @p connection is a defined state. */
int InputDeviceConnectionIsKnown(InputDeviceConnection connection);

/** Human-readable connection name; never NULL; `"unknown"` when undefined. */
const char *InputDeviceConnectionName(InputDeviceConnection connection);

/* ------------------------------------------------------------------ */
/* The descriptor                                                      */
/* ------------------------------------------------------------------ */

/**
 * Everything this runtime knows about one device.
 *
 * The order is the order a reader asks: what is it (`device`), what kind of thing is it (`type`), what can
 * it do (`capabilities`), where did it come from (`vendorId`/`productId`), and is it there
 * (`connection`).
 *
 * The vendor and product ids are **informational and optional**: 0 means "the platform did not say", which
 * is the honest value for every synthetic and remote device and for most embedded ones. Nothing in this
 * module branches on them, and the reason to carry them at all is that a diagnostics page and an
 * allow-list are both things a runtime needs and neither can be reconstructed from a name.
 */
typedef struct InputDeviceDescriptor {
    /** What an event carries: id, source, flags, copied name (RoboInputTypes.h). */
    InputDevice device;
    /** The kind of hardware. */
    InputDeviceType type;
    /** `RoboInputDeviceCapability*` bits: what this device can report. */
    uint32_t capabilities;
    /** USB/PCI vendor id, or 0 when unknown. Informational; nothing branches on it. */
    uint16_t vendorId;
    /** Product id, or 0 when unknown. Informational. */
    uint16_t productId;
    /** Where the device is in its lifecycle. */
    InputDeviceConnection connection;
} InputDeviceDescriptor;

/** Clear @p descriptor: no device, `UNKNOWN` type, no capabilities, disconnected. Safe on NULL. */
void InputDeviceDescriptorInit(InputDeviceDescriptor *descriptor);

/**
 * Fill @p descriptor from its parts.
 *
 * @param id           the device id; @ref RoboInputDeviceIdNone asks the *registry* to issue one (this
 *                      door records `None` and the registry fills it), and any other value is taken as
 *                      given.
 * @param type         the kind of hardware; `UNKNOWN` is refused, because a descriptor with no kind could
 *                      not be validated against its own capabilities.
 * @param source       the category it reports as; `UNKNOWN` means "use `InputDeviceTypeDefaultSource`",
 *                      and any *defined* source is taken as given -- that is the combined-digitizer case.
 * @param name         copied; NULL leaves it empty; longer than `RoboInputDeviceNameMax - 1` bytes is
 *                      refused (a truncated name is a different device's name).
 * @param capabilities `RoboInputDeviceCapability*` bits; unknown bits are refused.
 *
 * `descriptor` is cleared **first** and filled only if every argument is accepted, so a refusal leaves an
 * empty descriptor rather than a mixture of the refused call and the previous accepted one -- the rule
 * `InputDeviceSet` and `InputTargetSet*` already follow.
 *
 * The `PRIMARY` and `SYNTHETIC` flags on the identity are set by the caller afterwards through
 * `InputDeviceDescriptorSetFlags`, because they are policy about a *table* (is this the main keyboard?)
 * rather than facts about a device.
 */
Result InputDeviceDescriptorSet(InputDeviceDescriptor *descriptor,
                                InputDeviceId id,
                                InputDeviceType type,
                                InputSource source,
                                const char *name,
                                uint32_t capabilities,
                                InputDiag *diag);

/** Set the vendor and product ids. Any value is accepted: 0 means "not said". */
Result InputDeviceDescriptorSetIds(InputDeviceDescriptor *descriptor,
                                   uint16_t vendorId,
                                   uint16_t productId);

/** Set the device flags (`RoboInputDeviceFlag*`). Refused for bits this build does not define. */
Result InputDeviceDescriptorSetFlags(InputDeviceDescriptor *descriptor,
                                     uint32_t flags,
                                     InputDiag *diag);

/** Set the connection state. Refused for an undefined state. */
Result InputDeviceDescriptorSetConnection(InputDeviceDescriptor *descriptor,
                                          InputDeviceConnection connection,
                                          InputDiag *diag);

/**
 * Non-zero when @p descriptor is a record this build may put in a table.
 *
 * The rules: the identity is well-formed (`InputDeviceIsValid`); the type is defined and not `UNKNOWN`;
 * the connection state is defined and not `UNKNOWN`; the capabilities are defined bits; the identity's
 * source is one this build defines; and the two consistency rules from this header's comment -- a
 * `KEYBOARD` carries `KEYBOARD_KEYS`, a `GAMEPAD` carries at least one gamepad capability. A NULL pointer
 * is invalid, not a crash.
 */
int InputDeviceDescriptorIsValid(const InputDeviceDescriptor *descriptor);

/** Non-zero when @p descriptor can report @p capability. Safe on NULL (0). */
int InputDeviceDescriptorHasCapability(const InputDeviceDescriptor *descriptor,
                                       uint32_t capability);

/** Field-by-field equality. Safe on NULL; two NULLs are equal. */
int InputDeviceDescriptorEqual(const InputDeviceDescriptor *a,
                               const InputDeviceDescriptor *b);

/**
 * Room `InputDeviceDescriptorWrite` needs for the longest descriptor, NUL included.
 *
 * One line: the id, the type, the source, the connection, the two ids, the capabilities list and the name.
 * Stated as a constant so a caller sizes a buffer once instead of learning the length from a refusal.
 */
#define RoboInputDeviceLineMax 192u

/**
 * Write @p descriptor as one line, the shape a device dump reads:
 *
 *     device #1 keyboard source=keyboard connection=connected vid=0 pid=0 caps=keyboard-keys name="AT keyboard"
 *
 * One line, ending in a newline, nothing that varies between two runs of the same build. `"?"` and the
 * needed length when the buffer is too small.
 */
Result InputDeviceDescriptorWrite(const InputDeviceDescriptor *descriptor,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  InputDiag *diag);

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

/** How many devices one registry holds. A bound on a caller's table, not a policy. */
#define RoboInputDeviceRegistryMax 16u

/**
 * A fixed table of connected devices, owned by a caller.
 *
 * Held **by value**: `InputDeviceRegistryInit` is the whole of its construction and there is nothing to
 * destroy, which is the same shape `InputEvent` has and the reason this module has one allocation site
 * rather than two. A registry that outlives its caller's frame is the caller's mistake to avoid, exactly
 * as a queue is.
 *
 * The table is **ordered**: entries sit in connection order and an index is stable until a disconnect
 * before it. A dump is therefore deterministic, and a test can assert the third device is the third
 * connected one without sorting.
 */
typedef struct InputDeviceRegistry {
    /** Connected devices, in connection order. */
    InputDeviceDescriptor devices[RoboInputDeviceRegistryMax];
    /** How many entries are live. */
    uint32_t count;
    /** The id the next connect with `RoboInputDeviceIdNone` will receive. Starts at 1. */
    InputDeviceId nextId;
    /** How many devices have ever been connected: a counter a dump reads, and a test asserts. */
    uint64_t connectedTotal;
    /** How many have been disconnected. */
    uint64_t disconnectedTotal;
} InputDeviceRegistry;

/** Clear @p registry: no devices, ids from 1. Safe on NULL. */
void InputDeviceRegistryInit(InputDeviceRegistry *registry);

/**
 * Add a device.
 *
 * The ordinary hotplug door. @p description's identity may carry `RoboInputDeviceIdNone`, in which case
 * the registry issues the next id and writes it back into the stored copy -- @p outId receives the id the
 * device actually got either way, so a caller connecting two keyboards ends up able to tell them apart.
 *
 * The **first** device of each type that connects receives the `PRIMARY` flag and any previous holder of
 * it loses the flag, which is the rule that makes "the main keyboard" a question with one answer rather
 * than a convention every caller re-invents. A caller that has already decided (a device the platform
 * named its main one) sets the flag itself and the registry does not take it away unless another connect
 * raises the same type's primary.
 *
 * Refused with a `DEVICE` diagnostic when: @p description is not valid; the table is full
 * (`RoboNotSupported`, with the count in the diagnostic); or the id is already present
 * (`RoboAlreadyExists`), because two devices with one id would make an event's device ambiguous.
 */
Result InputDeviceRegistryConnect(InputDeviceRegistry *registry,
                                  const InputDeviceDescriptor *description,
                                  InputDeviceId *outId,
                                  InputDiag *diag);

/**
 * Remove a device by id, and hand back what was removed.
 *
 * The door hotplug runs on, and it takes the entry **out** rather than marking it (this header's comment).
 * @p outRemoved may be NULL; when it is not, it receives the descriptor exactly as it was stored, which is
 * what a caller needs to release that device's keys, captures and contacts.
 *
 * Refused with a `DEVICE` diagnostic and `RoboNotFound` when the id is not in the table. A disconnect of a
 * device that is not there is *not* an error to swallow: it is the symptom of a caller that has lost track
 * of its own table, and reporting it is how that is found.
 */
Result InputDeviceRegistryDisconnect(InputDeviceRegistry *registry,
                                     InputDeviceId id,
                                     InputDeviceDescriptor *outRemoved,
                                     InputDiag *diag);

/** Suspend or resume a device by id, leaving it in the table. `SUSPENDED` is present-but-quiet. */
Result InputDeviceRegistrySetConnection(InputDeviceRegistry *registry,
                                        InputDeviceId id,
                                        InputDeviceConnection connection,
                                        InputDiag *diag);

/** How many devices are live. 0 for NULL. */
uint32_t InputDeviceRegistryCount(const InputDeviceRegistry *registry);

/** The descriptor at @p index in connection order, or `RoboNotFound` past the end. */
Result InputDeviceRegistryAt(const InputDeviceRegistry *registry,
                             uint32_t index,
                             InputDeviceDescriptor *out,
                             InputDiag *diag);

/** The descriptor for @p id, or `RoboNotFound`. */
Result InputDeviceRegistryFind(const InputDeviceRegistry *registry,
                               InputDeviceId id,
                               InputDeviceDescriptor *out,
                               InputDiag *diag);

/** The index of @p id in connection order, or -1 when absent. */
int32_t InputDeviceRegistryIndexOf(const InputDeviceRegistry *registry, InputDeviceId id);

/** The first connected device of @p type, preferring the one flagged `PRIMARY`, or `RoboNotFound`. */
Result InputDeviceRegistryPrimary(const InputDeviceRegistry *registry,
                                  InputDeviceType type,
                                  InputDeviceDescriptor *out,
                                  InputDiag *diag);

/** The first connected device that can report @p capability, or `RoboNotFound`. */
Result InputDeviceRegistryFindByCapability(const InputDeviceRegistry *registry,
                                           uint32_t capability,
                                           InputDeviceDescriptor *out,
                                           InputDiag *diag);

/** Non-zero when @p id is in @p registry. */
int InputDeviceRegistryContains(const InputDeviceRegistry *registry, InputDeviceId id);

/** Remove every device. The counters are kept: how many ever connected is a fact, not a live one. */
void InputDeviceRegistryClear(InputDeviceRegistry *registry);

/**
 * Room `InputDeviceRegistryWrite` needs for a full table, NUL included.
 *
 * A summary line plus one line per device: a count only this header knows, stated so a caller can size a
 * buffer once. `input.device.registry_dump` asserts the dump fits and that this constant is a ceiling.
 */
#define RoboInputDeviceRegistryDumpMax \
    (96u + (RoboInputDeviceRegistryMax * RoboInputDeviceLineMax))

/**
 * Write @p registry as text: a summary line, then one line per device in connection order.
 *
 *     devices count=2 connected=3 disconnected=1 next=4
 *     device #1 keyboard ...
 *     device #2 mouse ...
 *
 * `"?"` and the needed length when the buffer is too small.
 */
Result InputDeviceRegistryWrite(const InputDeviceRegistry *registry,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_DEVICE_H */
