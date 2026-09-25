/*
 * Gamepad navigation (Phase 13.54).
 *
 * 13.53 landed the gamepad as *hardware*: sixteen buttons, nine normalized axes and a payload that carries one
 * button transition or one axis sample. This file is the other half the brief names separately, and the two
 * flags exist because they are two different claims: `gamepad` says a controller is understood, and
 * `gamepadNavigation` says a controller can **move focus and activate** — the same semantic layer a keyboard
 * and a screen reader reach.
 *
 * The mapping is the brief's, and it is a *mapping*, not a driver:
 *
 *     D-pad / left stick  →  FocusNext / FocusPrevious / FocusUp / Down / Left / Right
 *     A                   →  Activate
 *     B                   →  Back
 *
 * Every one of those is a semantic action, and `InputGamepadNavigationActionCommand` turns it into the
 * module's own `InputCommand` — so a controller reaches the same target chain, in the same order, as the
 * shortcut and the accessibility lane (13.35/13.36/13.52). Nothing about a controller's button numbers
 * escapes this file.
 *
 * ## The four rules, stated once each
 *
 *   **A deadzone is a number, not a feel.** `deadzone` is in the axis scale (0..32767) and an axis at or
 *   inside it is at rest. Outside it, a direction is decided by the **dominant axis** — greater magnitude
 *   wins, ties go vertical — because a stick at 45° must produce one direction rather than none, and "the
 *   bigger one" is a rule a test can state.
 *
 *   **The d-pad beats the stick, and a face button beats both.** The d-pad is checked first (in the order
 *   up, down, left, right), then the stick, and a pressed A or B suppresses whatever direction is being held.
 *   That is what makes an activation *one* action instead of an activation interleaved with repeats.
 *
 *   **Held directions repeat; edges do not.** A direction emits when it changes, then again after
 *   `repeatDelayMs`, then every `repeatIntervalMs` — the same cadence shape as 13.8, driven by the caller's
 *   clock so a test simulates time instead of sleeping. An `Activate` or a `Back` is an **edge**: it is
 *   emitted once when the button goes down and never repeats, because a repeat on `A` is a second click.
 *
 *   **Nothing is inferred from a device.** A disconnect, a focus change or a reset calls
 *   `InputGamepadNavigationReset`: a held direction whose release never arrived would otherwise repeat
 *   forever, which is the gamepad shape of 13.44's "a device disconnect must not leave keys stuck".
 *
 * ## What is not here
 *
 * No polling, no device handle, no rumble, no analog interpretation beyond a direction, no per-application
 * binding table, and no guest-side controller driver: 13.6's platform adapter is still the missing half, so
 * this layer is fed by whoever produces 13.53 events. It also does not *apply* an action — it maps one, and
 * hands the caller a command, which is what keeps it free of the focus manager, the dispatcher and the shell.
 */
#ifndef ROBO_INPUT_GAMEPAD_NAVIGATION_H
#define ROBO_INPUT_GAMEPAD_NAVIGATION_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputGamepad.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The action vocabulary                                               */
/* ------------------------------------------------------------------ */

/**
 * What a controller's state means to the focus system.
 *
 * Stable and positional: a log line, a script and a test all carry these numbers. Every one of the eight maps
 * onto a command (`InputGamepadNavigationActionCommand`); `NONE` maps onto nothing.
 */
typedef enum InputGamepadNavAction {
    /** Nothing is being asked for. */
    RoboGamepadNavNone = 0,
    /** The next target in the current scope. */
    RoboGamepadNavFocusNext = 1,
    /** The previous one. */
    RoboGamepadNavFocusPrevious = 2,
    /** One target up. */
    RoboGamepadNavFocusUp = 3,
    /** One target down. */
    RoboGamepadNavFocusDown = 4,
    /** One target left. */
    RoboGamepadNavFocusLeft = 5,
    /** One target right. */
    RoboGamepadNavFocusRight = 6,
    /** Act on whatever has focus. */
    RoboGamepadNavActivate = 7,
    /** Go back, or dismiss the transient surface. */
    RoboGamepadNavBack = 8
} InputGamepadNavAction;

/** Number of defined actions, `NONE` included. Asserted by `input.gamepadnav.vocabulary`. */
#define RoboInputGamepadNavActionCount 9u

/** Non-zero when @p action is defined. */
int InputGamepadNavActionIsKnown(InputGamepadNavAction action);

/** Human-readable name (`"focus-left"`, `"activate"`); never NULL; `"unknown"` when undefined. */
const char *InputGamepadNavActionName(InputGamepadNavAction action);

/** Non-zero when @p action moves focus -- the six directions, and not `ACTIVATE`, `BACK` or `NONE`. */
int InputGamepadNavActionIsFocus(InputGamepadNavAction action);

/** Non-zero when @p action is an **edge**: emitted on a button's press and never repeated. */
int InputGamepadNavActionIsEdge(InputGamepadNavAction action);

/** An axis at or inside this is at rest. The default, chosen to match the usual platform deadzone. */
#define RoboInputGamepadNavDeadzoneDefault 8000

/** The largest deadzone this build accepts: the axis scale's own maximum. */
#define RoboInputGamepadNavDeadzoneMax RoboInputGamepadAxisMax

/** Default hold before a direction starts repeating, in milliseconds. */
#define RoboInputGamepadNavRepeatDelayDefault 500u

/** Default gap between repeats, in milliseconds. */
#define RoboInputGamepadNavRepeatIntervalDefault 100u

/** The longest delay or interval a caller may set: a second, either way. */
#define RoboInputGamepadNavRepeatMax 1000u

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

/**
 * The held controller state and its cadence.
 *
 * Held by value: nothing to destroy, one per consumer, and no global controller registry -- the same shape
 * every other table in this module has. The held buttons are a bitmask over `InputGamepadButton`, so
 * `InputGamepadNavigationButtonHeld` is one shift rather than a search.
 */
typedef struct InputGamepadNavigation {
    /** An axis at or inside this is at rest. */
    int32_t deadzone;
    /** How long a held direction waits before its first repeat, in milliseconds. */
    uint32_t repeatDelayMs;
    /** The gap between repeats after that. */
    uint32_t repeatIntervalMs;

    /** `1 << button` for every button currently down. */
    uint32_t held;
    /** The left stick's horizontal axis, as last reported. */
    int32_t leftX;
    /** The left stick's vertical axis: negative is up. */
    int32_t leftY;
    /** A d-pad reported as an axis, horizontal. */
    int32_t dpadX;
    /** A d-pad reported as an axis, vertical: negative is up. */
    int32_t dpadY;

    /** The action being held right now, or `NONE`. */
    InputGamepadNavAction current;
    /** When the held action's next repeat is due, or 0 when nothing is repeating. */
    uint64_t nextRepeatMs;
    /** The timestamp of the last sample fed in. Diagnostics. */
    uint64_t lastSampleMs;

    /** Samples fed in. Diagnostics. */
    uint64_t samplesTotal;
    /** Actions emitted, repeats included. Diagnostics. */
    uint64_t actionsTotal;
    /** Emitted actions that were repeats. Diagnostics. */
    uint64_t repeatTotal;
    /** Samples that asked for nothing (`NONE`), which is most of them. Diagnostics. */
    uint64_t neutralTotal;
    /** Samples refused for a bad argument. Diagnostics. */
    uint64_t refusedTotal;
} InputGamepadNavigation;

/** Clear @p navigation to the defaults: the standard deadzone, the standard cadence, nothing held. */
void InputGamepadNavigationInit(InputGamepadNavigation *navigation);

/** Set the deadzone. Refused outside `0..RoboInputGamepadNavDeadzoneMax`. */
Result InputGamepadNavigationSetDeadzone(InputGamepadNavigation *navigation,
                                         int32_t deadzone,
                                         InputDiag *diag);

/**
 * Set the repeat cadence. Refused for an interval of 0, or a delay or interval above
 * `RoboInputGamepadNavRepeatMax` -- an interval of 0 is an infinite event rate, and a bound is how this
 * build refuses one rather than trusting a caller not to mean it.
 */
Result InputGamepadNavigationSetRepeat(InputGamepadNavigation *navigation,
                                       uint32_t repeatDelayMs,
                                       uint32_t repeatIntervalMs,
                                       InputDiag *diag);

/**
 * Forget every held button, every axis and the repeat deadline, keeping the configuration and the counters.
 *
 * For a disconnect, a window deactivation and the reset command (13.44/13.78): a release that never arrived
 * would otherwise leave a direction repeating forever.
 */
void InputGamepadNavigationReset(InputGamepadNavigation *navigation);

/** Non-zero when @p button is currently down. 0 for a NULL navigation or an undefined button. */
int InputGamepadNavigationButtonHeld(const InputGamepadNavigation *navigation,
                                     InputGamepadButton button);

/** The action held right now, or `NONE`. 0 for NULL. */
InputGamepadNavAction InputGamepadNavigationCurrent(const InputGamepadNavigation *navigation);

/* ------------------------------------------------------------------ */
/* The one update door                                                 */
/* ------------------------------------------------------------------ */

/**
 * Feed one gamepad sample and ask what it means.
 *
 * @param data      the sample: a button transition or an axis value, exactly as 13.53 built it.
 * @param nowMs     the caller's clock, in milliseconds. The cadence is driven by it, so a test simulates time
 *                  instead of waiting for it.
 * @param outAction receives the action to perform, or `NONE`; required.
 * @param outRepeated receives non-zero when @p outAction is a repeat rather than a fresh action; optional.
 * @return `RoboOk` for every valid sample, `NONE` included -- the action is the answer, not a status.
 *         `RoboInvalidArgument` for NULL arguments or a sample `InputGamepadDataIsValid` refuses.
 *
 * The default is a live-zone sample whose meaning has not changed and whose repeat is not yet due: it answers
 * `NONE`, which is most of what a 100 Hz controller produces.
 */
Result InputGamepadNavigationFeed(InputGamepadNavigation *navigation,
                                  const InputGamepadData *data,
                                  uint64_t nowMs,
                                  InputGamepadNavAction *outAction,
                                  int *outRepeated,
                                  InputDiag *diag);

/**
 * The action @p navigation's held state means, ignoring the clock.
 *
 * The mapping without the cadence, exposed because it is the half a caller can reason about statically -- what
 * does the D-pad plus a stick mean -- and because a fixture can then assert a meaning without inventing a
 * timeline. `NONE` for NULL or an undefined action.
 */
InputGamepadNavAction InputGamepadNavigationResolve(const InputGamepadNavigation *navigation);

/* ------------------------------------------------------------------ */
/* The command mapping                                                 */
/* ------------------------------------------------------------------ */

/**
 * Turn @p action into the module's own command.
 *
 *     focus-*    → `FOCUS_DIRECTION` with the direction, or `FOCUS_NEXT`/`FOCUS_PREVIOUS`
 *     activate   → `ACTIVATE`
 *     back       → `BACK`
 *
 * @return `RoboOk`; `RoboInvalidArgument` for NULL arguments, an undefined action or `NONE` (which is a
 *         cleared record rather than a command to execute).
 */
Result InputGamepadNavigationActionCommand(InputGamepadNavAction action,
                                           InputCommand *outCommand,
                                           InputDiag *diag);

/** The focus direction @p action means, or `RoboInputFocusDirectionNone`. */
InputFocusDirection InputGamepadNavigationActionDirection(InputGamepadNavAction action);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** Room `InputGamepadNavigationWrite` needs. */
#define RoboInputGamepadNavDumpMax 512u

/**
 * Write @p navigation as text: the configuration, what is held, and the counters.
 *
 * Complete or absent: `?` and the needed length when the buffer is too small, and the buffer left empty.
 */
Result InputGamepadNavigationWrite(const InputGamepadNavigation *navigation,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_GAMEPAD_NAVIGATION_H */
