/*
 * RoboInput gamepad-navigation suite (Phase 13.54) — `input.gamepadnav.*`.
 *
 * Eight cases. The cadence is driven by the *caller's* clock, so every one of them simulates time rather
 * than sleeping:
 *
 *   `vocabulary`   the nine actions positionally, which of them move focus, which are edges, and the three
 *                  defaults -- a deadzone, a delay and an interval, each with the rule they exist for;
 *
 *   `deadzone`     inside it is at rest, at it and beyond it is a direction, and the two ends of the scale
 *                  are the two extremes -- plus the bound: a deadzone outside the axis scale is refused;
 *
 *   `dominant`     a stick at 45 degrees means **one** direction (the larger magnitude wins, ties go
 *                  vertical), the d-pad beats the stick, and a face button beats both;
 *
 *   `repeat`       the cadence: a fresh direction emits at once, holds until the delay, then repeats on the
 *                  interval -- and a change of meaning restarts the delay rather than continuing another
 *                  direction's cadence;
 *
 *   `edges`        A activates once however long it is held, B goes back once, and releasing one lets the
 *                  direction underneath resume;
 *
 *   `sticks`       the four axes by name, `CENTERED` treated as exactly zero, and the axes that mean nothing
 *                  to focus (right stick, triggers) ignored rather than mapped;
 *
 *   `mapping`      every action onto a command, including the four directions;
 *
 *   `rules`        reset (the shape of a disconnect: no stuck direction repeating forever), the refusals, and
 *                  the dump's determinism, counters and bound.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

static InputGamepadData NavButton(InputGamepadButton button, int pressed)
{
    InputGamepadData data;

    InputGamepadDataInit(&data);
    if (InputGamepadDataSetButton(&data, button, pressed, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's gamepad button was refused");
    }
    return data;
}

static InputGamepadData NavAxis(InputGamepadAxis axis, int32_t value)
{
    InputGamepadData data;

    InputGamepadDataInit(&data);
    if (InputGamepadDataSetAxis(&data, axis, value, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's gamepad axis was refused");
    }
    return data;
}

/** Feed @p data at @p ms and return the action, asserting `RoboOk`. */
static InputGamepadNavAction NavFeed(InputGamepadNavigation *navigation,
                                     const InputGamepadData *data,
                                     uint64_t ms,
                                     int *outRepeated)
{
    InputGamepadNavAction action = RoboGamepadNavNone;

    if (InputGamepadNavigationFeed(navigation, data, ms, &action, outRepeated, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's feed was refused");
    }
    return action;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestNavVocabulary(void)
{
    InputGamepadNavigation navigation;

    RoboCheckEqInt(RoboGamepadNavNone, 0);
    RoboCheckEqInt(RoboGamepadNavFocusNext, 1);
    RoboCheckEqInt(RoboGamepadNavFocusPrevious, 2);
    RoboCheckEqInt(RoboGamepadNavFocusUp, 3);
    RoboCheckEqInt(RoboGamepadNavFocusDown, 4);
    RoboCheckEqInt(RoboGamepadNavFocusLeft, 5);
    RoboCheckEqInt(RoboGamepadNavFocusRight, 6);
    RoboCheckEqInt(RoboGamepadNavActivate, 7);
    RoboCheckEqInt(RoboGamepadNavBack, 8);
    RoboCheckEqInt((int)RoboInputGamepadNavActionCount, 9);

    RoboCheckEqInt(InputGamepadNavActionIsKnown(RoboGamepadNavNone), 1);
    RoboCheckEqInt(InputGamepadNavActionIsKnown(RoboGamepadNavBack), 1);
    RoboCheckEqInt(InputGamepadNavActionIsKnown((InputGamepadNavAction)9), 0);
    RoboCheckEqInt(InputGamepadNavActionIsKnown((InputGamepadNavAction)-1), 0);

    RoboCheckStrEq(InputGamepadNavActionName(RoboGamepadNavNone), "none");
    RoboCheckStrEq(InputGamepadNavActionName(RoboGamepadNavFocusNext), "focus-next");
    RoboCheckStrEq(InputGamepadNavActionName(RoboGamepadNavFocusLeft), "focus-left");
    RoboCheckStrEq(InputGamepadNavActionName(RoboGamepadNavActivate), "activate");
    RoboCheckStrEq(InputGamepadNavActionName(RoboGamepadNavBack), "back");
    RoboCheckStrEq(InputGamepadNavActionName((InputGamepadNavAction)99), "unknown");

    /* The six directions move focus; an activation and a go-back are something else. */
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavFocusNext), 1);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavFocusPrevious), 1);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavFocusUp), 1);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavFocusDown), 1);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavFocusLeft), 1);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavFocusRight), 1);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavActivate), 0);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavBack), 0);
    RoboCheckEqInt(InputGamepadNavActionIsFocus(RoboGamepadNavNone), 0);

    /* And the two edges are the two that must never repeat. */
    RoboCheckEqInt(InputGamepadNavActionIsEdge(RoboGamepadNavActivate), 1);
    RoboCheckEqInt(InputGamepadNavActionIsEdge(RoboGamepadNavBack), 1);
    RoboCheckEqInt(InputGamepadNavActionIsEdge(RoboGamepadNavFocusLeft), 0);
    RoboCheckEqInt(InputGamepadNavActionIsEdge(RoboGamepadNavNone), 0);

    /* Defaults, and the first thing a fresh navigation says. */
    InputGamepadNavigationInit(&navigation);
    RoboCheckEqInt(navigation.deadzone, RoboInputGamepadNavDeadzoneDefault);
    RoboCheckEqInt((int)navigation.repeatDelayMs, (int)RoboInputGamepadNavRepeatDelayDefault);
    RoboCheckEqInt((int)navigation.repeatIntervalMs, (int)RoboInputGamepadNavRepeatIntervalDefault);
    RoboCheckEqInt((int)InputGamepadNavigationCurrent(&navigation), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)InputGamepadNavigationResolve(&navigation), (int)RoboGamepadNavNone);
    RoboCheckEqInt(InputGamepadNavigationButtonHeld(&navigation, RoboGamepadButtonA), 0);
    /* The deadzone is under a quarter of the scale: enough to swallow a loose stick, not a push. */
    RoboCheck(RoboInputGamepadNavDeadzoneDefault < RoboInputGamepadAxisMax / 4);
}

/* ------------------------------------------------------------------ */
/* The deadzone                                                        */
/* ------------------------------------------------------------------ */

static void TestNavDeadzone(void)
{
    InputGamepadNavigation navigation;
    InputGamepadData stick = NavAxis(RoboGamepadAxisLeftX, 7999);
    InputDiag diag = { 0 };

    InputGamepadNavigationInit(&navigation);

    /* Inside the deadzone: at rest, and it stays at rest. */
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 0u, NULL), (int)RoboGamepadNavNone);
    stick = NavAxis(RoboGamepadAxisLeftX, -7999);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 1u, NULL), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)navigation.current, (int)RoboGamepadNavNone);

    /* At the deadzone itself: still a rest -- "at or inside it" is the rule, and the header says so. One
     * step past it is a direction. */
    stick = NavAxis(RoboGamepadAxisLeftX, 8000);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 2u, NULL), (int)RoboGamepadNavNone);
    stick = NavAxis(RoboGamepadAxisLeftX, 8001);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 3u, NULL), (int)RoboGamepadNavFocusRight);
    stick = NavAxis(RoboGamepadAxisLeftX, -8001);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 4u, NULL), (int)RoboGamepadNavFocusLeft);

    /* The vertical axis is negative-up, which is the axis's own sign and not a flipped one. The stick is
     * still held at -8001 horizontally, and 20000 is the larger magnitude, so vertical wins. */
    stick = NavAxis(RoboGamepadAxisLeftY, -20000);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 5u, NULL), (int)RoboGamepadNavFocusUp);
    stick = NavAxis(RoboGamepadAxisLeftY, 20000);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 6u, NULL), (int)RoboGamepadNavFocusDown);

    /* Release both axes -- the horizontal one from several samples ago -- and the held direction ends. */
    stick = NavAxis(RoboGamepadAxisLeftX, 0);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 7u, NULL), (int)RoboGamepadNavNone);
    stick = NavAxis(RoboGamepadAxisLeftY, 0);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 8u, NULL), (int)RoboGamepadNavNone);

    /* Then a configured deadzone is what the rule reads: with a `0` deadzone a single step is a direction,
     * and an all-zero pair is still a rest. */
    RoboCheckEqInt(InputGamepadNavigationSetDeadzone(&navigation, 0, NULL), RoboOk);
    stick = NavAxis(RoboGamepadAxisLeftX, 1);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 9u, NULL), (int)RoboGamepadNavFocusRight);
    stick = NavAxis(RoboGamepadAxisLeftX, 0);
    RoboCheckEqInt((int)NavFeed(&navigation, &stick, 10u, NULL), (int)RoboGamepadNavNone);

    /* Outside the axis scale is refused rather than clamped: a clamped deadzone is a wrong rule that looks
     * like a working one. */
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputGamepadNavigationSetDeadzone(&navigation, -1, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainGamepad);
    RoboCheckEqInt(InputGamepadNavigationSetDeadzone(&navigation, RoboInputGamepadNavDeadzoneMax + 1, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(navigation.deadzone, 0);
    RoboCheckEqInt(InputGamepadNavigationSetDeadzone(&navigation, RoboInputGamepadNavDeadzoneMax, NULL),
                   RoboOk);
    RoboCheckEqInt(navigation.deadzone, RoboInputGamepadNavDeadzoneMax);
    RoboCheckEqInt(InputGamepadNavigationSetDeadzone(NULL, 0, NULL), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Which direction wins                                                */
/* ------------------------------------------------------------------ */

static void TestNavDominant(void)
{
    InputGamepadNavigation navigation;
    InputGamepadData data;

    InputGamepadNavigationInit(&navigation);

    /* A stick at 45 degrees is one direction, not none: the larger magnitude wins. */
    data = NavAxis(RoboGamepadAxisLeftX, 20000);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 0u, NULL), (int)RoboGamepadNavFocusRight);
    data = NavAxis(RoboGamepadAxisLeftY, 20000);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 1u, NULL), (int)RoboGamepadNavFocusDown);
    /* A tie goes vertical. */
    navigation.leftX = 20000;
    navigation.leftY = 20000;
    RoboCheckEqInt((int)InputGamepadNavigationResolve(&navigation), (int)RoboGamepadNavFocusDown);
    /* And back to horizontal when x is the bigger one. */
    navigation.leftY = 12000;
    RoboCheckEqInt((int)InputGamepadNavigationResolve(&navigation), (int)RoboGamepadNavFocusRight);

    /* A d-pad reported as axes beats the stick. */
    navigation.dpadX = -20000;
    navigation.dpadY = 0;
    RoboCheckEqInt((int)InputGamepadNavigationResolve(&navigation), (int)RoboGamepadNavFocusLeft);

    /* A d-pad's own buttons beat both. */
    InputGamepadNavigationReset(&navigation);
    navigation.dpadX = -20000;
    data = NavButton(RoboGamepadButtonDpadUp, 1);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 2u, NULL), (int)RoboGamepadNavFocusUp);
    RoboCheckEqInt(InputGamepadNavigationButtonHeld(&navigation, RoboGamepadButtonDpadUp), 1);
    RoboCheckEqInt((int)InputGamepadNavigationResolve(&navigation), (int)RoboGamepadNavFocusUp);

    /* A face button beats everything: an activation is one action, not an activation interleaved with the
     * repeats of whatever direction was held. */
    data = NavButton(RoboGamepadButtonA, 1);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 3u, NULL), (int)RoboGamepadNavActivate);
    data = NavButton(RoboGamepadButtonB, 1);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 4u, NULL), (int)RoboGamepadNavNone);
    /* Releasing A lets B through, which is a change and therefore an action. */
    data = NavButton(RoboGamepadButtonA, 0);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 5u, NULL), (int)RoboGamepadNavBack);
}

/* ------------------------------------------------------------------ */
/* The cadence                                                         */
/* ------------------------------------------------------------------ */

static void TestNavRepeat(void)
{
    InputGamepadNavigation navigation;
    InputGamepadData right = NavAxis(RoboGamepadAxisDpadX, 32767);
    InputGamepadData neutral = NavAxis(RoboGamepadAxisDpadX, 0);
    InputGamepadData left = NavAxis(RoboGamepadAxisDpadX, -32767);
    int repeated = 0;

    InputGamepadNavigationInit(&navigation);

    /* A fresh direction emits at once and does not repeat until the delay has passed. */
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 1000u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheckEqInt(repeated, 0);
    RoboCheckEqInt((uint64_t)navigation.nextRepeatMs, 1500u);

    /* Identical samples inside the delay are `NONE`, which is most of what a controller produces. */
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 1100u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 1499u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)navigation.repeatTotal, 0);

    /* At the deadline, and not before. */
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 1500u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheckEqInt(repeated, 1);
    RoboCheckEqInt((uint64_t)navigation.nextRepeatMs, 1600u);
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 1599u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 1600u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheckEqInt(repeated, 1);
    RoboCheckEqInt((int)navigation.repeatTotal, 2);

    /* Releasing stops it, and the release itself asks for nothing. */
    RoboCheckEqInt((int)NavFeed(&navigation, &neutral, 1700u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((uint64_t)navigation.nextRepeatMs, 0u);
    RoboCheckEqInt((int)NavFeed(&navigation, &neutral, 5000u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)navigation.repeatTotal, 2);

    /* A change of meaning restarts the delay rather than inheriting the other direction's cadence. */
    RoboCheckEqInt((int)NavFeed(&navigation, &left, 6000u, &repeated), (int)RoboGamepadNavFocusLeft);
    RoboCheckEqInt(repeated, 0);
    RoboCheckEqInt((uint64_t)navigation.nextRepeatMs, 6500u);
    RoboCheckEqInt((int)NavFeed(&navigation, &left, 6499u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)NavFeed(&navigation, &left, 6500u, &repeated), (int)RoboGamepadNavFocusLeft);
    RoboCheckEqInt(repeated, 1);

    /* A configured cadence is what the deadline uses. */
    RoboCheckEqInt(InputGamepadNavigationSetRepeat(&navigation, 50u, 25u, NULL), RoboOk);
    RoboCheckEqInt((int)NavFeed(&navigation, &neutral, 7000u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 7100u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheckEqInt((uint64_t)navigation.nextRepeatMs, 7150u);
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 7150u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheckEqInt(repeated, 1);

    /* Seven emissions over the fourteen samples above, four of them repeats. */
    RoboCheckEqInt((int)navigation.actionsTotal, 7);
    RoboCheckEqInt((int)navigation.repeatTotal, 4);
    RoboCheck(navigation.samplesTotal > 0u);
}

/* ------------------------------------------------------------------ */
/* Edges                                                               */
/* ------------------------------------------------------------------ */

static void TestNavEdges(void)
{
    InputGamepadNavigation navigation;
    InputGamepadData press = NavButton(RoboGamepadButtonA, 1);
    InputGamepadData release = NavButton(RoboGamepadButtonA, 0);
    int repeated = 0;

    InputGamepadNavigationInit(&navigation);
    RoboCheckEqInt((int)NavFeed(&navigation, &press, 0u, &repeated), (int)RoboGamepadNavActivate);
    RoboCheckEqInt(repeated, 0);
    /* No cadence is armed for an edge, and none may appear however long it is held. */
    RoboCheckEqInt((uint64_t)navigation.nextRepeatMs, 0u);
    RoboCheckEqInt((int)NavFeed(&navigation, &press, 10000u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt(repeated, 0);
    RoboCheckEqInt((int)navigation.actionsTotal, 1);
    RoboCheckEqInt((int)navigation.repeatTotal, 0);

    /* The release asks for nothing, and a second press is a second activation -- not a repeat. */
    RoboCheckEqInt((int)NavFeed(&navigation, &release, 10100u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)NavFeed(&navigation, &press, 10200u, &repeated), (int)RoboGamepadNavActivate);
    RoboCheckEqInt(repeated, 0);
    RoboCheckEqInt((int)navigation.actionsTotal, 2);

    /* B is the back edge. */
    RoboCheckEqInt((int)NavFeed(&navigation, &release, 10300u, &repeated), (int)RoboGamepadNavNone);
    {
        InputGamepadData back = NavButton(RoboGamepadButtonB, 1);
        InputGamepadData done = NavButton(RoboGamepadButtonB, 0);

        RoboCheckEqInt((int)NavFeed(&navigation, &back, 10400u, &repeated), (int)RoboGamepadNavBack);
        RoboCheckEqInt((int)NavFeed(&navigation, &done, 10401u, &repeated), (int)RoboGamepadNavNone);
    }

    /* Buttons that mean nothing to focus change no meaning. */
    {
        InputGamepadData start = NavButton(RoboGamepadButtonStart, 1);
        InputGamepadData shoulder = NavButton(RoboGamepadButtonLeftBumper, 1);

        RoboCheckEqInt((int)NavFeed(&navigation, &start, 10500u, &repeated), (int)RoboGamepadNavNone);
        RoboCheckEqInt((int)NavFeed(&navigation, &shoulder, 10600u, &repeated), (int)RoboGamepadNavNone);
        RoboCheckEqInt(InputGamepadNavigationButtonHeld(&navigation, RoboGamepadButtonStart), 1);
        RoboCheckEqInt((int)navigation.actionsTotal, 3);
    }
}

/* ------------------------------------------------------------------ */
/* The axes                                                            */
/* ------------------------------------------------------------------ */

static void TestNavSticks(void)
{
    InputGamepadNavigation navigation;
    InputGamepadData data;
    int repeated = 0;

    InputGamepadNavigationInit(&navigation);

    /* Each axis this layer reads, by name. */
    data = NavAxis(RoboGamepadAxisLeftX, 30000);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 0u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheckEqInt(navigation.leftX, 30000);
    data = NavAxis(RoboGamepadAxisDpadY, -30000);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 1u, &repeated), (int)RoboGamepadNavFocusUp);
    RoboCheckEqInt(navigation.dpadY, -30000);

    /* `CENTERED` is the device saying "at rest", which is a stronger statement than a small value: it is
     * recorded as exactly zero rather than as near it. The flag and the value must agree -- the model's own
     * validator refuses a centred sample that carries a number, which is asserted just below. */
    data = NavAxis(RoboGamepadAxisLeftX, 0);
    data.flags |= RoboInputGamepadFlagCentered;
    RoboCheckEqInt(InputGamepadDataIsValid(&data), 1);
    /* The meaning is unchanged -- the d-pad's vertical is still the winner -- so a rest sample asks for
     * nothing, and the axis is what says the zero was recorded. */
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 2u, &repeated), (int)RoboGamepadNavNone);
    RoboCheckEqInt(navigation.leftX, 0);

    /* The axes that mean nothing to focus are ignored rather than guessed at. */
    data = NavAxis(RoboGamepadAxisRightX, 32767);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 3u, &repeated), (int)RoboGamepadNavNone);
    data = NavAxis(RoboGamepadAxisLeftTrigger, 32767);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 4u, &repeated), (int)RoboGamepadNavNone);
    data = NavAxis(RoboGamepadAxisRightY, -32767);
    RoboCheckEqInt((int)NavFeed(&navigation, &data, 5u, &repeated), (int)RoboGamepadNavNone);

    /* A sample this build's validator refuses is refused, and counted. Two shapes: a value outside the
     * scale, and a `CENTERED` flag disagreeing with the value it came with -- both of which are the model's
     * own rules rather than this layer's. */
    {
        InputGamepadData broken = NavAxis(RoboGamepadAxisLeftX, 1000);
        InputGamepadNavAction action = RoboGamepadNavNone;
        InputDiag diag = { 0 };

        broken.flags |= RoboInputGamepadFlagCentered;
        RoboCheckEqInt(InputGamepadDataIsValid(&broken), 0);
        RoboCheckEqInt(InputGamepadNavigationFeed(&navigation, &broken, 6u, &action, NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(diag.domain, RoboInputDomainGamepad);
        RoboCheckEqInt((int)navigation.refusedTotal, 1);
        RoboCheckEqInt(action, RoboGamepadNavNone);

        broken = NavAxis(RoboGamepadAxisLeftX, 1000);
        broken.value = RoboInputGamepadAxisMax + 1;
        RoboCheckEqInt(InputGamepadNavigationFeed(&navigation, &broken, 7u, &action, NULL, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt((int)navigation.refusedTotal, 2);
        /* A refusal changes no state: the axis the last accepted sample set is still there. */
        RoboCheckEqInt(navigation.leftX, 0);
    }

    RoboCheckEqInt(InputGamepadNavigationFeed(NULL, &data, 0u, &repeated, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadNavigationFeed(&navigation, NULL, 0u, &repeated, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadNavigationFeed(&navigation, &data, 0u, NULL, NULL, NULL),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The command mapping                                                 */
/* ------------------------------------------------------------------ */

static void TestNavMapping(void)
{
    InputCommand command;

    InputCommandInit(&command);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavFocusNext, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandFocusNext);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavFocusPrevious, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandFocusPrevious);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavActivate, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandActivate);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavBack, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandBack);

    /* The four directions are one command with a direction argument, which is the vocabulary's own shape. */
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavFocusUp, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandFocusDirection);
    RoboCheckEqInt((int)command.direction, (int)RoboInputFocusDirectionUp);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavFocusDown, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.direction, (int)RoboInputFocusDirectionDown);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavFocusLeft, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.direction, (int)RoboInputFocusDirectionLeft);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavFocusRight, &command, NULL), RoboOk);
    RoboCheckEqInt((int)command.direction, (int)RoboInputFocusDirectionRight);

    RoboCheckEqInt((int)InputGamepadNavigationActionDirection(RoboGamepadNavFocusLeft),
                   (int)RoboInputFocusDirectionLeft);
    RoboCheckEqInt((int)InputGamepadNavigationActionDirection(RoboGamepadNavActivate),
                   (int)RoboInputFocusDirectionNone);
    RoboCheckEqInt((int)InputGamepadNavigationActionDirection(RoboGamepadNavNone),
                   (int)RoboInputFocusDirectionNone);

    /* A cleared action is a record, not a command. */
    InputCommandInit(&command);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavNone, &command, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandNone);
    RoboCheckEqInt(InputGamepadNavigationActionCommand((InputGamepadNavAction)99, &command, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadNavigationActionCommand(RoboGamepadNavActivate, NULL, NULL),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Reset, and the dump                                                 */
/* ------------------------------------------------------------------ */

static void TestNavRules(void)
{
    InputGamepadNavigation navigation;
    InputGamepadData right = NavAxis(RoboGamepadAxisDpadX, 32767);
    InputGamepadData press = NavButton(RoboGamepadButtonA, 1);
    InputDiag diag = { 0 };
    int repeated = 0;

    InputGamepadNavigationInit(&navigation);
    RoboCheckEqInt(InputGamepadNavigationSetRepeat(&navigation, 100u, 0u, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainGamepad);
    RoboCheckEqInt(InputGamepadNavigationSetRepeat(&navigation, 0u, 0u, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadNavigationSetRepeat(&navigation, 5000u, 100u, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputGamepadNavigationSetRepeat(&navigation, 100u, 5000u, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)navigation.repeatDelayMs, (int)RoboInputGamepadNavRepeatDelayDefault);
    RoboCheckEqInt(InputGamepadNavigationSetRepeat(NULL, 1u, 1u, NULL), RoboInvalidArgument);

    /* The shape of a disconnect: a direction whose release never arrived would repeat forever. */
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 100u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheck((navigation.nextRepeatMs != 0u));
    RoboCheckEqInt((int)NavFeed(&navigation, &press, 110u, &repeated), (int)RoboGamepadNavActivate);
    RoboCheckEqInt(InputGamepadNavigationButtonHeld(&navigation, RoboGamepadButtonA), 1);

    InputGamepadNavigationReset(&navigation);
    RoboCheckEqInt((int)InputGamepadNavigationCurrent(&navigation), (int)RoboGamepadNavNone);
    RoboCheckEqInt((uint64_t)navigation.nextRepeatMs, 0u);
    RoboCheckEqInt(navigation.held, 0u);
    RoboCheckEqInt(navigation.dpadX, 0);
    RoboCheckEqInt(navigation.leftY, 0);
    RoboCheckEqInt(InputGamepadNavigationButtonHeld(&navigation, RoboGamepadButtonA), 0);
    /* The configuration and the counters survive: a disconnect is not a reconfiguration. */
    RoboCheckEqInt(navigation.deadzone, RoboInputGamepadNavDeadzoneDefault);
    RoboCheckEqInt((int)navigation.repeatDelayMs, (int)RoboInputGamepadNavRepeatDelayDefault);
    RoboCheckEqInt((int)navigation.actionsTotal, 2);
    RoboCheckEqInt((int)navigation.samplesTotal, 2);
    /* And nothing repeats after it. */
    RoboCheckEqInt((int)NavFeed(&navigation, &right, 90000u, &repeated), (int)RoboGamepadNavFocusRight);
    RoboCheckEqInt(repeated, 0);

    /* An undefined button is not held, whatever the mask says. */
    RoboCheckEqInt(InputGamepadNavigationButtonHeld(&navigation, RoboGamepadButtonNone), 0);
    RoboCheckEqInt(InputGamepadNavigationButtonHeld(&navigation, (InputGamepadButton)99), 0);
    RoboCheckEqInt(InputGamepadNavigationButtonHeld(NULL, RoboGamepadButtonA), 0);
    RoboCheckEqInt((int)InputGamepadNavigationCurrent(NULL), (int)RoboGamepadNavNone);
    RoboCheckEqInt((int)InputGamepadNavigationResolve(NULL), (int)RoboGamepadNavNone);
    InputGamepadNavigationReset(NULL);

    /* The dump: two generations byte for byte, the counters verbatim, and the bound. */
    {
        InputGamepadNavigation fresh;
        char first[RoboInputGamepadNavDumpMax];
        char second[RoboInputGamepadNavDumpMax];
        char small[32];
        size_t firstLength = 0u;
        size_t secondLength = 0u;
        size_t needed = 0u;

        InputGamepadNavigationInit(&fresh);
        RoboCheckEqInt((int)NavFeed(&fresh, &press, 10u, &repeated), (int)RoboGamepadNavActivate);
        RoboCheckEqInt((int)NavFeed(&fresh, &right, 20u, &repeated), (int)RoboGamepadNavNone);

        RoboCheckEqInt(InputGamepadNavigationWrite(&fresh, first, sizeof(first), &firstLength, NULL),
                       RoboOk);
        RoboCheckEqInt(InputGamepadNavigationWrite(&fresh, second, sizeof(second), &secondLength, NULL),
                       RoboOk);
        RoboCheckEqInt((int)firstLength, (int)secondLength);
        RoboCheckStrEq(first, second);
        RoboCheck(strstr(first, "gamepad-nav deadzone=8000") != NULL);
        RoboCheck(strstr(first, "current=activate") != NULL);
        RoboCheck(strstr(first, "samples=2") != NULL);
        RoboCheck(strstr(first, "actions=1") != NULL);
        /* Which buttons are down, by name: a bitmask in a log line is a number nobody can read. */
        RoboCheck(strstr(first, "down a") != NULL);

        RoboCheckEqInt(InputGamepadNavigationWrite(&fresh, small, sizeof(small), &needed, NULL),
                       RoboNotSupported);
        RoboCheck(small[0] == '?');
        RoboCheck(needed > (size_t)sizeof(small));
        RoboCheck((int)needed <= (int)RoboInputGamepadNavDumpMax);

        RoboCheckEqInt(InputGamepadNavigationWrite(NULL, first, sizeof(first), &firstLength, NULL), RoboOk);
        RoboCheckStrEq(first, "gamepad-nav <none>\n");
        RoboCheckEqInt(InputGamepadNavigationWrite(&fresh, NULL, 0u, &firstLength, NULL),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputGamepadNavigationWrite(&fresh, first, sizeof(first), NULL, NULL),
                       RoboInvalidArgument);
    }
}

void TestRegisterInputGamepadNavigation(void)
{
    TestRegister("input.gamepadnav.vocabulary", TestNavVocabulary);
    TestRegister("input.gamepadnav.deadzone", TestNavDeadzone);
    TestRegister("input.gamepadnav.dominant", TestNavDominant);
    TestRegister("input.gamepadnav.repeat", TestNavRepeat);
    TestRegister("input.gamepadnav.edges", TestNavEdges);
    TestRegister("input.gamepadnav.sticks", TestNavSticks);
    TestRegister("input.gamepadnav.mapping", TestNavMapping);
    TestRegister("input.gamepadnav.rules", TestNavRules);
}
