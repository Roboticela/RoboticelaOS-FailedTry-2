/*
 * RoboInput gamepad vocabulary and payload (Phase 13.53) — a controller, normalized.
 *
 * ## The vocabulary is this module's, and that is not a duplication
 *
 * Keys are RoboWindow's (`PhysicalKey` / `LogicalKey`) because a window layer had to name them first.
 * Buttons and axes are **not**: nothing above this module has ever needed to say which stick moved, and a
 * second consumer arriving later would have to agree with a vocabulary that was invented for a different
 * question. So `InputGamepadButton` and `InputGamepadAxis` are defined here, with the two rules that make a
 * gamepad portable:
 *
 *   **position, not label.** `A`, `B`, `X` and `Y` name the four *positions* in the standard diamond, and
 *   deliberately not the letters printed on them. A PlayStation pad prints a cross where an Xbox pad prints
 *   an A; the position is the same, the glyph is not, and a runtime that carried the glyph would have to
 *   know which pad it was talking to. A title that wants the platform's own glyph maps this vocabulary to
 *   one itself, which is a decision about a *game*, not about input;
 *
 *   **normalized magnitude.** An axis value is a signed 16-bit range, `-32768`..`32767`, with `0` at rest,
 *   whatever the device's own resolution is. A trigger is an axis too, and it reports `-32768` at rest and
 *   `32767` fully pressed on the platforms where that is the convention -- stated here rather than assumed,
 *   because a consumer that expected `0`..`32767` from a trigger would read a released trigger as
 *   half-pressed on exactly half the hardware in the world.
 *
 * ## One payload, three event types
 *
 * `GamepadButtonDown`, `GamepadButtonUp` and `GamepadAxisChanged` share one record, and the record says
 * which of its two halves is live: a button event sets `button` and leaves `axis` `NONE`, an axis event
 * does the reverse. That is one field more than the union strictly needs, and it buys the same thing the
 * pointer payload's `buttons` mask buys: an event that is readable without knowing what the previous event
 * was, and a validator that can refuse a record whose halves disagree about what happened.
 *
 * ## What is absent
 *
 * No rumble, no vibration, no LED, no battery, no motion sensors, no touchpad-as-a-button, and no
 * "connected/charged" state beyond the device table's own connection state (RoboInputDevice.h). Each of
 * those is *output* or *device management* rather than input, and none of them is needed to know what the
 * player did -- which is the whole of this module's job.
 */
#ifndef ROBO_INPUT_GAMEPAD_H
#define ROBO_INPUT_GAMEPAD_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A controller button, by its **position** in the standard layout.
 *
 * The two pairs of names worth reading twice: `DPAD_*` is the directional pad as four separate buttons
 * (some platforms report it as an axis -- a backend that only has the axis reports `AXIS_DPAD_*` instead,
 * named below), and `START`/ `BACK` are the two menu buttons in their standard positions.
 */
typedef enum InputGamepadButton {
    /** Nothing: a cleared record's value, and the value of an axis event's button half. */
    RoboGamepadButtonNone = 0,
    /** The bottom face button of the diamond. */
    RoboGamepadButtonA = 1,
    /** The right face button. */
    RoboGamepadButtonB = 2,
    /** The left face button. */
    RoboGamepadButtonX = 3,
    /** The top face button. */
    RoboGamepadButtonY = 4,
    /** The shoulder button above the left trigger. */
    RoboGamepadButtonLeftBumper = 5,
    /** The shoulder button above the right trigger. */
    RoboGamepadButtonRightBumper = 6,
    /** The left menu button. */
    RoboGamepadButtonBack = 7,
    /** The right menu button. */
    RoboGamepadButtonStart = 8,
    /** The platform button in the middle. */
    RoboGamepadButtonGuide = 9,
    /** Pushing the left stick in. */
    RoboGamepadButtonLeftStick = 10,
    /** Pushing the right stick in. */
    RoboGamepadButtonRightStick = 11,
    /** The d-pad's up direction. */
    RoboGamepadButtonDpadUp = 12,
    /** The d-pad's down direction. */
    RoboGamepadButtonDpadDown = 13,
    /** The d-pad's left direction. */
    RoboGamepadButtonDpadLeft = 14,
    /** The d-pad's right direction. */
    RoboGamepadButtonDpadRight = 15
} InputGamepadButton;

/** Number of defined buttons, including `NONE`. Asserted positionally by `input.gamepad.vocabulary`. */
#define RoboGamepadButtonCount 16u

/**
 * A controller axis, normalized to `-32768`..`32767` with `0` at rest (see this header's comment).
 */
typedef enum InputGamepadAxis {
    /** Nothing: a cleared record's value, and the value of a button event's axis half. */
    RoboGamepadAxisNone = 0,
    /** Left stick, horizontal: negative left. */
    RoboGamepadAxisLeftX = 1,
    /** Left stick, vertical: negative up. */
    RoboGamepadAxisLeftY = 2,
    /** Right stick, horizontal: negative left. */
    RoboGamepadAxisRightX = 3,
    /** Right stick, vertical: negative up. */
    RoboGamepadAxisRightY = 4,
    /** Left trigger: `-32768` at rest, `32767` fully pressed. */
    RoboGamepadAxisLeftTrigger = 5,
    /** Right trigger: `-32768` at rest, `32767` fully pressed. */
    RoboGamepadAxisRightTrigger = 6,
    /** A d-pad reported as one horizontal axis, for hardware that has no separate d-pad buttons. */
    RoboGamepadAxisDpadX = 7,
    /** A d-pad reported as one vertical axis. */
    RoboGamepadAxisDpadY = 8
} InputGamepadAxis;

/** Number of defined axes, including `NONE`. Asserted positionally by `input.gamepad.vocabulary`. */
#define RoboGamepadAxisCount 9u

/** The axis is at rest. */
#define RoboInputGamepadFlagCentered ((uint32_t)(1u << 0))
/** The value is synthetic: a test, a replay or a script rather than hardware. */
#define RoboInputGamepadFlagSynthetic ((uint32_t)(1u << 1))

/** Every defined gamepad flag. */
#define RoboInputGamepadFlagAll (RoboInputGamepadFlagCentered | RoboInputGamepadFlagSynthetic)

/** The range an axis value lives in. */
#define RoboInputGamepadAxisMin (-32768)
#define RoboInputGamepadAxisMax 32767

/** Non-zero if @p button is a defined button (including `NONE`). */
int InputGamepadButtonIsKnown(InputGamepadButton button);

/** Human-readable button name; never NULL; `"unknown"` when undefined. The *position*'s name, not a
 *  glyph: `"a"`, `"left-bumper"`, `"dpad-up"`. */
const char *InputGamepadButtonName(InputGamepadButton button);

/** Non-zero if @p axis is a defined axis (including `NONE`). */
int InputGamepadAxisIsKnown(InputGamepadAxis axis);

/** Human-readable axis name; never NULL; `"unknown"` when undefined. */
const char *InputGamepadAxisName(InputGamepadAxis axis);

/** One gamepad event's payload, by value: a button transition or an axis sample, never both. */
typedef struct InputGamepadData {
    /** The button this event is about, or `NONE` for an axis event. */
    InputGamepadButton button;
    /** The axis this event is about, or `NONE` for a button event. */
    InputGamepadAxis axis;
    /** A button's state (0 or 1) or an axis's value in `RoboInputGamepadAxisMin`..`_MAX`. */
    int32_t value;
    /** `RoboInputGamepadFlag*` bits. */
    uint32_t flags;
} InputGamepadData;

/** Clear @p data: no button, no axis, no value. Safe on NULL. */
void InputGamepadDataInit(InputGamepadData *data);

/** Fill @p data as a **button** transition. @p value is normalized to 0/1. */
Result InputGamepadDataSetButton(InputGamepadData *data,
                                 InputGamepadButton button,
                                 int pressed,
                                 InputDiag *diag);

/** Fill @p data as an **axis** sample. @p value must be within the stated range; outside it is refused
 *  rather than clamped, because a clamped reading is a plausible wrong number. */
Result InputGamepadDataSetAxis(InputGamepadData *data,
                               InputGamepadAxis axis,
                               int32_t value,
                               InputDiag *diag);

/**
 * Non-zero when @p data is a record this build may carry on a gamepad event.
 *
 * The rules: exactly one of `button`/`axis` is set and the other is `NONE` (they are different questions
 * and an event that answered both would be two answers); the button or axis is defined; the flags are
 * defined bits; and a button's `value` is 0 or 1 while an axis's is within the stated range.
 */
int InputGamepadDataIsValid(const InputGamepadData *data);

/** Non-zero when @p data carries a button half. Safe on NULL (0). */
int InputGamepadDataIsButton(const InputGamepadData *data);

/** Non-zero when @p data carries an axis half. Safe on NULL (0). */
int InputGamepadDataIsAxis(const InputGamepadData *data);

/** Field-by-field equality. Safe on NULL; two NULLs are equal. */
int InputGamepadDataEqual(const InputGamepadData *a, const InputGamepadData *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_GAMEPAD_H */
