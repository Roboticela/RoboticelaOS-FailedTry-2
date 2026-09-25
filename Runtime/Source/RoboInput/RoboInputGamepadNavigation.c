/*
 * Gamepad navigation (Phase 13.54) — see `RoboInputGamepadNavigation.h`.
 *
 * The file is three pieces:
 *
 *   **`Resolve`**    the meaning of a held controller, with no clock in it at all: face buttons, then the
 *                    d-pad's buttons and axes, then the left stick, decided by the dominant axis.
 *   **`Feed`**       the clock: a changed action emits, a held direction repeats on the configured cadence,
 *                    and an edge action emits exactly once.
 *   **the mapping**  an action into this module's `InputCommand`, so a controller reaches the same target
 *                    chain a shortcut and an accessibility request do.
 *
 * Splitting `Resolve` out of `Feed` is what makes the rules testable without a timeline: the meaning of
 * "d-pad left plus stick down" is a pure function, and the cadence is a deadline.
 */

#include "RoboInput/RoboInputGamepadNavigation.h"

#include "RoboInputInternal.h"

#include <string.h>

static const char *const RiGamepadNavNames[RoboInputGamepadNavActionCount] = {
    "none",
    "focus-next",
    "focus-previous",
    "focus-up",
    "focus-down",
    "focus-left",
    "focus-right",
    "activate",
    "back"
};

int InputGamepadNavActionIsKnown(InputGamepadNavAction action)
{
    return (int)action >= 0 && (size_t)action < (size_t)RoboInputGamepadNavActionCount;
}

const char *InputGamepadNavActionName(InputGamepadNavAction action)
{
    if (!InputGamepadNavActionIsKnown(action)) {
        return "unknown";
    }
    return RiGamepadNavNames[(size_t)action];
}

int InputGamepadNavActionIsFocus(InputGamepadNavAction action)
{
    return action >= RoboGamepadNavFocusNext && action <= RoboGamepadNavFocusRight;
}

int InputGamepadNavActionIsEdge(InputGamepadNavAction action)
{
    return action == RoboGamepadNavActivate || action == RoboGamepadNavBack;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

void InputGamepadNavigationInit(InputGamepadNavigation *navigation)
{
    if (!navigation) {
        return;
    }
    memset(navigation, 0, sizeof(*navigation));
    navigation->deadzone = RoboInputGamepadNavDeadzoneDefault;
    navigation->repeatDelayMs = RoboInputGamepadNavRepeatDelayDefault;
    navigation->repeatIntervalMs = RoboInputGamepadNavRepeatIntervalDefault;
    navigation->current = RoboGamepadNavNone;
}

Result InputGamepadNavigationSetDeadzone(InputGamepadNavigation *navigation,
                                         int32_t deadzone,
                                         InputDiag *diag)
{
    if (!navigation) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a deadzone needs somewhere to live");
    }
    if (deadzone < 0 || deadzone > RoboInputGamepadNavDeadzoneMax) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a deadzone outside the axis scale is not a deadzone");
    }
    navigation->deadzone = deadzone;
    return RoboOk;
}

Result InputGamepadNavigationSetRepeat(InputGamepadNavigation *navigation,
                                       uint32_t repeatDelayMs,
                                       uint32_t repeatIntervalMs,
                                       InputDiag *diag)
{
    if (!navigation) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a cadence needs somewhere to live");
    }
    if (repeatIntervalMs == 0u) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "an interval of zero is an infinite event rate");
    }
    if (repeatDelayMs > RoboInputGamepadNavRepeatMax || repeatIntervalMs > RoboInputGamepadNavRepeatMax) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a delay or interval longer than a second is not a repeat cadence");
    }
    navigation->repeatDelayMs = repeatDelayMs;
    navigation->repeatIntervalMs = repeatIntervalMs;
    return RoboOk;
}

void InputGamepadNavigationReset(InputGamepadNavigation *navigation)
{
    if (!navigation) {
        return;
    }
    navigation->held = 0u;
    navigation->leftX = 0;
    navigation->leftY = 0;
    navigation->dpadX = 0;
    navigation->dpadY = 0;
    navigation->current = RoboGamepadNavNone;
    navigation->nextRepeatMs = 0u;
    navigation->lastSampleMs = 0u;
}

int InputGamepadNavigationButtonHeld(const InputGamepadNavigation *navigation,
                                     InputGamepadButton button)
{
    if (!navigation || !InputGamepadButtonIsKnown(button) || button == RoboGamepadButtonNone) {
        return 0;
    }
    return (navigation->held & (uint32_t)(1u << (uint32_t)button)) != 0u;
}

InputGamepadNavAction InputGamepadNavigationCurrent(const InputGamepadNavigation *navigation)
{
    return navigation ? navigation->current : RoboGamepadNavNone;
}

/* ------------------------------------------------------------------ */
/* Resolve: the meaning, without a clock                               */
/* ------------------------------------------------------------------ */

/** The d-pad button @p button means, or `NONE` for a button that is not a direction. */
static InputGamepadNavAction RiGamepadNavForDpadButton(InputGamepadButton button)
{
    switch (button) {
    case RoboGamepadButtonDpadUp:
        return RoboGamepadNavFocusUp;
    case RoboGamepadButtonDpadDown:
        return RoboGamepadNavFocusDown;
    case RoboGamepadButtonDpadLeft:
        return RoboGamepadNavFocusLeft;
    case RoboGamepadButtonDpadRight:
        return RoboGamepadNavFocusRight;
    default:
        return RoboGamepadNavNone;
    }
}

/**
 * The direction two axis values mean, or `NONE` when neither is outside @p deadzone.
 *
 * *At or inside* the deadzone is a rest -- the comparison is strict, so a configured `0` means only a real
 * movement counts and an all-zero pair is still nothing rather than a direction. Outside it on **both** axes,
 * the larger magnitude wins and a tie goes vertical: a stick at 45 degrees is one direction, not none, and
 * "the bigger one" is a rule a test can write down. Vertical is negative-up, matching
 * `InputGamepadAxisLeftY`'s own sign.
 */
static InputGamepadNavAction RiGamepadNavFromAxes(int32_t x, int32_t y, int32_t deadzone)
{
    int activeX = x < -deadzone || x > deadzone;
    int activeY = y < -deadzone || y > deadzone;

    if (!activeX && !activeY) {
        return RoboGamepadNavNone;
    }
    if (activeX && activeY) {
        int32_t magnitudeX = x < 0 ? -x : x;
        int32_t magnitudeY = y < 0 ? -y : y;

        if (magnitudeX > magnitudeY) {
            return x < 0 ? RoboGamepadNavFocusLeft : RoboGamepadNavFocusRight;
        }
        return y < 0 ? RoboGamepadNavFocusUp : RoboGamepadNavFocusDown;
    }
    if (activeX) {
        return x < 0 ? RoboGamepadNavFocusLeft : RoboGamepadNavFocusRight;
    }
    return y < 0 ? RoboGamepadNavFocusUp : RoboGamepadNavFocusDown;
}

InputGamepadNavAction InputGamepadNavigationResolve(const InputGamepadNavigation *navigation)
{
    InputGamepadNavAction action;

    if (!navigation) {
        return RoboGamepadNavNone;
    }

    /* A face button first: an activation must be one action rather than an activation interleaved with the
     * repeats of whatever direction was being held. */
    if (InputGamepadNavigationButtonHeld(navigation, RoboGamepadButtonA)) {
        return RoboGamepadNavActivate;
    }
    if (InputGamepadNavigationButtonHeld(navigation, RoboGamepadButtonB)) {
        return RoboGamepadNavBack;
    }

    /* The d-pad's own buttons, in a fixed order -- up, down, left, right. A controller cannot hold two of them
     * and mean both, and a synthetic source that does gets the earlier one deterministically. */
    action = RoboGamepadNavNone;
    {
        static const InputGamepadButton order[4] = {
            RoboGamepadButtonDpadUp,
            RoboGamepadButtonDpadDown,
            RoboGamepadButtonDpadLeft,
            RoboGamepadButtonDpadRight
        };
        size_t index;

        for (index = 0u; index < 4u; index++) {
            if (InputGamepadNavigationButtonHeld(navigation, order[index])) {
                action = RiGamepadNavForDpadButton(order[index]);
                break;
            }
        }
    }
    if (action != RoboGamepadNavNone) {
        return action;
    }

    /* A d-pad reported as axes, then the stick: the d-pad is the deliberate instrument, so it wins. */
    action = RiGamepadNavFromAxes(navigation->dpadX, navigation->dpadY, navigation->deadzone);
    if (action != RoboGamepadNavNone) {
        return action;
    }
    return RiGamepadNavFromAxes(navigation->leftX, navigation->leftY, navigation->deadzone);
}

/* ------------------------------------------------------------------ */
/* Feed: the meaning plus the clock                                    */
/* ------------------------------------------------------------------ */

Result InputGamepadNavigationFeed(InputGamepadNavigation *navigation,
                                  const InputGamepadData *data,
                                  uint64_t nowMs,
                                  InputGamepadNavAction *outAction,
                                  int *outRepeated,
                                  InputDiag *diag)
{
    InputGamepadNavAction resolved;

    if (!navigation || !data || !outAction) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a feed needs a navigation, a sample and somewhere to put the action");
    }
    *outAction = RoboGamepadNavNone;
    if (outRepeated) {
        *outRepeated = 0;
    }
    if (!InputGamepadDataIsValid(data)) {
        navigation->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a sample this build's own validator refuses is not navigated");
    }

    navigation->samplesTotal++;
    navigation->lastSampleMs = nowMs;

    if (InputGamepadDataIsButton(data)) {
        uint32_t bit = (uint32_t)(1u << (uint32_t)data->button);

        if (data->value != 0) {
            navigation->held |= bit;
        } else {
            navigation->held &= ~bit;
        }
    } else if (InputGamepadDataIsAxis(data)) {
        /* The `CENTERED` flag is the device saying "at rest", which is a stronger statement than a value that
         * happens to be small: it is recorded as exactly zero rather than as near it. */
        int32_t value = (data->flags & RoboInputGamepadFlagCentered) != 0u ? 0 : data->value;

        switch (data->axis) {
        case RoboGamepadAxisLeftX:
            navigation->leftX = value;
            break;
        case RoboGamepadAxisLeftY:
            navigation->leftY = value;
            break;
        case RoboGamepadAxisDpadX:
            navigation->dpadX = value;
            break;
        case RoboGamepadAxisDpadY:
            navigation->dpadY = value;
            break;
        default:
            /* Every other axis -- the right stick, the two triggers -- means nothing to focus. Ignoring them
             * here rather than mapping them is the point of a vocabulary this small. */
            break;
        }
    }

    resolved = InputGamepadNavigationResolve(navigation);
    if (resolved != navigation->current) {
        navigation->current = resolved;
        if (resolved == RoboGamepadNavNone) {
            navigation->nextRepeatMs = 0u;
            navigation->neutralTotal++;
            return RoboOk;
        }
        /* A direction starts its cadence; an edge has none, because a repeat on `A` is a second click. */
        navigation->nextRepeatMs = InputGamepadNavActionIsFocus(resolved)
                                       ? nowMs + (uint64_t)navigation->repeatDelayMs
                                       : 0u;
        navigation->actionsTotal++;
        *outAction = resolved;
        return RoboOk;
    }

    /* The meaning has not changed. A direction repeats when its deadline has come; an edge, or nothing at
     * all, asks for nothing. */
    if (resolved == RoboGamepadNavNone || !InputGamepadNavActionIsFocus(resolved)) {
        navigation->neutralTotal++;
        return RoboOk;
    }
    if (navigation->nextRepeatMs != 0u && nowMs >= navigation->nextRepeatMs) {
        navigation->nextRepeatMs = nowMs + (uint64_t)navigation->repeatIntervalMs;
        navigation->actionsTotal++;
        navigation->repeatTotal++;
        *outAction = resolved;
        if (outRepeated) {
            *outRepeated = 1;
        }
        return RoboOk;
    }
    navigation->neutralTotal++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The command mapping                                                 */
/* ------------------------------------------------------------------ */

InputFocusDirection InputGamepadNavigationActionDirection(InputGamepadNavAction action)
{
    switch (action) {
    case RoboGamepadNavFocusUp:
        return RoboInputFocusDirectionUp;
    case RoboGamepadNavFocusDown:
        return RoboInputFocusDirectionDown;
    case RoboGamepadNavFocusLeft:
        return RoboInputFocusDirectionLeft;
    case RoboGamepadNavFocusRight:
        return RoboInputFocusDirectionRight;
    default:
        return RoboInputFocusDirectionNone;
    }
}

Result InputGamepadNavigationActionCommand(InputGamepadNavAction action,
                                           InputCommand *outCommand,
                                           InputDiag *diag)
{
    InputFocusDirection direction;

    if (!outCommand) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a mapping needs somewhere to put the command");
    }
    if (!InputGamepadNavActionIsKnown(action) || action == RoboGamepadNavNone) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "a cleared action is not a command");
    }

    InputCommandInit(outCommand);
    switch (action) {
    case RoboGamepadNavFocusNext:
        return InputCommandSet(outCommand, RoboInputCommandFocusNext, diag);
    case RoboGamepadNavFocusPrevious:
        return InputCommandSet(outCommand, RoboInputCommandFocusPrevious, diag);
    case RoboGamepadNavActivate:
        return InputCommandSet(outCommand, RoboInputCommandActivate, diag);
    case RoboGamepadNavBack:
        return InputCommandSet(outCommand, RoboInputCommandBack, diag);
    default:
        break;
    }
    direction = InputGamepadNavigationActionDirection(action);
    if (direction == RoboInputFocusDirectionNone) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboInvalidArgument,
                             "an action without a command and without a direction is not navigable");
    }
    if (InputCommandSet(outCommand, RoboInputCommandFocusDirection, diag) != RoboOk) {
        return RoboInputDiag(diag, RoboInputDomainGamepad, RoboError,
                             "the focus-direction command could not be built");
    }
    return InputCommandSetDirection(outCommand, direction, diag);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result InputGamepadNavigationWrite(const InputGamepadNavigation *navigation,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   InputDiag *diag)
{
    InputWriter text;
    InputGamepadButton button;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!navigation) {
        RiWriterText(&text, "gamepad-nav <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "gamepad-nav");
    RiWriterFieldI32(&text, " deadzone", navigation->deadzone);
    RiWriterFieldU64(&text, " delay", (uint64_t)navigation->repeatDelayMs);
    RiWriterFieldU64(&text, " interval", (uint64_t)navigation->repeatIntervalMs);
    RiWriterFieldText(&text, " current", InputGamepadNavActionName(navigation->current));
    RiWriterFieldU64(&text, " held", (uint64_t)navigation->held);
    RiWriterFieldU64(&text, " samples", navigation->samplesTotal);
    RiWriterFieldU64(&text, " actions", navigation->actionsTotal);
    RiWriterFieldU64(&text, " repeats", navigation->repeatTotal);
    RiWriterFieldU64(&text, " neutral", navigation->neutralTotal);
    RiWriterFieldU64(&text, " refused", navigation->refusedTotal);
    RiWriterChar(&text, '\n');

    RiWriterText(&text, "  axes");
    RiWriterFieldI32(&text, " left-x", navigation->leftX);
    RiWriterFieldI32(&text, " left-y", navigation->leftY);
    RiWriterFieldI32(&text, " dpad-x", navigation->dpadX);
    RiWriterFieldI32(&text, " dpad-y", navigation->dpadY);
    RiWriterChar(&text, '\n');

    /* Which buttons are down, by name: a bitmask in a log line is a number nobody can read. */
    RiWriterText(&text, "  down");
    for (button = (InputGamepadButton)1; button < (InputGamepadButton)RoboGamepadButtonCount;
         button = (InputGamepadButton)((int)button + 1)) {
        if ((navigation->held & (uint32_t)(1u << (uint32_t)button)) == 0u) {
            continue;
        }
        RiWriterChar(&text, ' ');
        RiWriterText(&text, InputGamepadButtonName(button));
    }
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
