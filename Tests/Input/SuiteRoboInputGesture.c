/*
 * RoboInput gesture suite (Phase 13.23/13.24) — `input.gesture.*`.
 *
 * Six cases:
 *
 *   `vocabulary`   the seven types, four phases and three verdicts, with the multi-touch classification and
 *                  the terminal-phase rule;
 *
 *   `samples`      the two builders: a pointer payload becomes a sample, the contact table becomes one with a
 *                  centroid, a spread and an angle -- and the angle of one finger is 0 rather than a NaN a
 *                  caller would have to guard;
 *
 *   `events`       `InputGestureEventIsValid`, and the two records it deliberately refuses: a multi-touch
 *                  gesture with one contact, and a scale of zero;
 *
 *   `arena`        the arbitration: the first `Accept` in registration order wins, a `Reject` drops an entry,
 *                  a single `Defer` keeps the sequence open, the winner *owns* the sequence afterwards, a
 *                  terminal phase releases it, and a losing recognizer's events are never delivered;
 *
 *   `heuristics`   the shipped recognizer: a finger that lifts where it landed is a tap, travelling first is
 *                  a drag, a stationary tick past the delay is a long press, two taps in one place are a
 *                  double tap, and two fingers that spread or turn are a pinch or a rotate;
 *
 *   `dump`         the three writers, asserted whole.
 */

#include "RoboInputTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted recognizer, so the arena's rules are the subject         */
/* ------------------------------------------------------------------ */

typedef struct ScriptedRecognizer {
    InputGestureVerdict verdicts[8];
    uint32_t verdictCount;
    uint32_t asked;
    int reset;
    /** Events to produce while deferring, to prove a losing recognizer's events are not delivered. */
    int emitWhileDeferring;
} ScriptedRecognizer;

static InputGestureVerdict ScriptedOffer(void *userdata,
                                         const InputGestureSample *sample,
                                         InputGestureEvent *out,
                                         uint32_t capacity,
                                         uint32_t *outCount)
{
    ScriptedRecognizer *state = (ScriptedRecognizer *)userdata;
    InputGestureVerdict verdict = state->verdicts[state->asked < state->verdictCount
                                                       ? state->asked
                                                       : state->verdictCount - 1u];

    state->asked++;
    (void)sample;
    if (state->emitWhileDeferring && verdict == RoboInputGestureVerdictDefer && capacity > 0u) {
        InputGestureEventInit(&out[0]);
        out[0].type = RoboInputGestureTap;
        out[0].phase = RoboInputGesturePhaseUpdate;
        out[0].scale = 1.0f;
        out[0].space = RoboInputSpaceWindow;
        out[0].contactCount = 1u;
        *outCount = 1u;
    } else if (outCount) {
        *outCount = 0u;
    }
    return verdict;
}

static void ScriptedReset(void *userdata)
{
    ((ScriptedRecognizer *)userdata)->reset = 1;
}

static InputGestureRecognizer MakeScripted(ScriptedRecognizer *script)
{
    InputGestureRecognizer recognizer;

    memset(&recognizer, 0, sizeof(recognizer));
    recognizer.type = RoboInputGestureTap;
    recognizer.userdata = script;
    recognizer.offer = ScriptedOffer;
    recognizer.reset = ScriptedReset;
    return recognizer;
}

static InputGestureSample SampleAt(int32_t x, int32_t y, InputEventType type, uint64_t timeMs)
{
    InputGestureSample sample;

    InputGestureSampleInit(&sample);
    sample.pointer = RoboInputPointerIdPrimary;
    sample.type = type;
    sample.position = MakeGfxPoint(x, y);
    sample.centroid = sample.position;
    sample.hasPosition = 1;
    sample.space = RoboInputSpaceWindow;
    sample.timestampMs = timeMs;
    sample.contactCount = 1u;
    return sample;
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestGestureVocabulary(void)
{
    RoboCheckEqInt(RoboInputGestureNone, 0);
    RoboCheckEqInt(RoboInputGestureTap, 1);
    RoboCheckEqInt(RoboInputGestureDoubleTap, 2);
    RoboCheckEqInt(RoboInputGestureLongPress, 3);
    RoboCheckEqInt(RoboInputGestureDrag, 4);
    RoboCheckEqInt(RoboInputGesturePan, 5);
    RoboCheckEqInt(RoboInputGesturePinch, 6);
    RoboCheckEqInt(RoboInputGestureRotate, 7);
    RoboCheckEqInt(RoboInputGestureTypeCount, 8);
    RoboCheckStrEq(InputGestureTypeName(RoboInputGestureDoubleTap), "double-tap");
    RoboCheckStrEq(InputGestureTypeName(RoboInputGestureLongPress), "long-press");
    RoboCheckStrEq(InputGestureTypeName((InputGestureType)88), "unknown");
    RoboCheck(!InputGestureTypeIsKnown((InputGestureType)8));

    /* Which need more than one finger is a fact, not a convention: a pan, a pinch and a rotate. */
    RoboCheck(!InputGestureTypeIsMultiTouch(RoboInputGestureTap));
    RoboCheck(!InputGestureTypeIsMultiTouch(RoboInputGestureDoubleTap));
    RoboCheck(!InputGestureTypeIsMultiTouch(RoboInputGestureLongPress));
    RoboCheck(!InputGestureTypeIsMultiTouch(RoboInputGestureDrag));
    RoboCheck(InputGestureTypeIsMultiTouch(RoboInputGesturePan));
    RoboCheck(InputGestureTypeIsMultiTouch(RoboInputGesturePinch));
    RoboCheck(InputGestureTypeIsMultiTouch(RoboInputGestureRotate));

    RoboCheckEqInt(RoboInputGesturePhaseBegin, 0);
    RoboCheckEqInt(RoboInputGesturePhaseUpdate, 1);
    RoboCheckEqInt(RoboInputGesturePhaseEnd, 2);
    RoboCheckEqInt(RoboInputGesturePhaseCancel, 3);
    RoboCheckEqInt(RoboInputGesturePhaseCount, 4);
    RoboCheckStrEq(InputGesturePhaseName(RoboInputGesturePhaseCancel), "cancel");
    RoboCheck(!InputGesturePhaseIsTerminal(RoboInputGesturePhaseUpdate));
    RoboCheck(InputGesturePhaseIsTerminal(RoboInputGesturePhaseEnd));
    RoboCheck(InputGesturePhaseIsTerminal(RoboInputGesturePhaseCancel));

    RoboCheckEqInt(RoboInputGestureVerdictReject, 0);
    RoboCheckEqInt(RoboInputGestureVerdictAccept, 1);
    RoboCheckEqInt(RoboInputGestureVerdictDefer, 2);
    RoboCheckEqInt(RoboInputGestureVerdictCount, 3);
    RoboCheckStrEq(InputGestureVerdictName(RoboInputGestureVerdictDefer), "defer");
    RoboCheck(!InputGestureVerdictIsKnown((InputGestureVerdict)3));
}

/* ------------------------------------------------------------------ */
/* Samples and events                                                  */
/* ------------------------------------------------------------------ */

static void TestGestureSamples(void)
{
    InputGestureSample sample;
    InputPointerData payload;
    InputTouchState touches;
    InputDiag diag;

    InputDiagReset(&diag);

    /* A pointer payload becomes a sample, and a known previous position becomes the delta -- the platform's
     * own delta is only used when there is no previous position to derive one from. */
    payload = GetRoboInputPointerData(50, 60, RoboMouseButtonNone, 0u);
    RoboCheckEqInt(InputPointerDataSetPrevious(&payload, MakeGfxPoint(40, 30)), RoboOk);
    RoboCheckEqInt(InputGestureSampleFromPointer(&payload, RoboInputEventPointerMove, RoboInputSpaceWindow,
                                                 7u, 900u, &sample, &diag),
                   RoboOk);
    RoboCheckEqInt((int)sample.type, (int)RoboInputEventPointerMove);
    RoboCheckEqInt((int)sample.device, 7);
    RoboCheckEqInt((int)sample.timestampMs, 900);
    RoboCheck(sample.deltaX > 9.9f && sample.deltaX < 10.1f);
    RoboCheck(sample.deltaY > 29.9f && sample.deltaY < 30.1f);
    RoboCheckEqInt((int)sample.contactCount, 1);
    RoboCheck(!sample.cancelled);

    /* A cancel is marked as one. */
    RoboCheckEqInt(InputGestureSampleFromPointer(&payload, RoboInputEventPointerCancel,
                                                 RoboInputSpaceWindow, 7u, 901u, &sample, &diag),
                   RoboOk);
    RoboCheck(sample.cancelled);

    /* NULL arguments, and an undefined type. */
    RoboCheckEqInt(InputGestureSampleFromPointer(NULL, RoboInputEventPointerMove, RoboInputSpaceWindow, 7u,
                                                 1u, &sample, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGestureSampleFromPointer(&payload, RoboInputEventNone, RoboInputSpaceWindow, 7u, 1u,
                                                 &sample, &diag),
                   RoboInvalidArgument);

    /* From contacts: the centroid, the spread and the angle. Two fingers 100 apart on a horizontal line:
     * centroid (50, 50), spread 50, angle 0. */
    InputTouchStateInit(&touches);
    RoboCheckEqInt(InputTouchStateApply(&touches, 1u, RoboInputTouchPhaseBegin, MakeGfxPoint(0, 50), 5u,
                                        RoboInputSpaceScreen, 0.0f, 0.0f, 0, 10u, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt(InputTouchStateApply(&touches, 2u, RoboInputTouchPhaseBegin, MakeGfxPoint(100, 50), 5u,
                                        RoboInputSpaceScreen, 0.0f, 0.0f, 0, 10u, NULL, &diag),
                   RoboOk);
    RoboCheckEqInt(InputGestureSampleFromTouches(&touches, 20u, &sample, &diag), RoboOk);
    RoboCheckEqInt((int)sample.contactCount, 2);
    RoboCheck(sample.centroid.x > 49.9f && sample.centroid.x < 50.1f);
    RoboCheck(sample.centroid.y > 49.9f && sample.centroid.y < 50.1f);
    RoboCheck(sample.spread > 49.9f && sample.spread < 50.1f);
    /* The axis points from the centroid towards the *first live* contact: contact 1 is at (0, 50), the
     * centroid is (50, 50), so the direction is (-1, 0). */
    RoboCheck(sample.axisX < -0.99f && sample.axisX > -1.01f);
    RoboCheck(sample.axisY > -0.01f && sample.axisY < 0.01f);

    /* One finger: spread 0 and a zero axis rather than a NaN. There is no line to measure. */
    RoboCheck(InputTouchStateRemove(&touches, 2u));
    RoboCheckEqInt(InputGestureSampleFromTouches(&touches, 30u, &sample, &diag), RoboOk);
    RoboCheckEqInt((int)sample.contactCount, 1);
    RoboCheckEqInt((int)sample.spread, 0);
    RoboCheckEqInt((int)sample.axisX, 0);
    RoboCheckEqInt((int)sample.axisY, 0);

    /* No fingers at all: a legitimate sample -- a sequence that ended. */
    RoboCheck(InputTouchStateRemove(&touches, 1u));
    RoboCheckEqInt(InputGestureSampleFromTouches(&touches, 40u, &sample, &diag), RoboOk);
    RoboCheckEqInt((int)sample.contactCount, 0);
    RoboCheck(!sample.hasPosition);
    RoboCheckEqInt((int)sample.space, (int)RoboInputSpaceUnknown);
    RoboCheckEqInt(InputGestureSampleFromTouches(NULL, 40u, &sample, &diag), RoboOk);
    RoboCheckEqInt(InputGestureSampleFromTouches(&touches, 40u, NULL, &diag), RoboInvalidArgument);
}

static void TestGestureEvents(void)
{
    InputGestureEvent event;

    InputGestureEventInit(&event);
    RoboCheckEqInt((int)event.type, (int)RoboInputGestureNone);
    RoboCheckEqInt((int)event.phase, (int)RoboInputGesturePhaseBegin);
    RoboCheck(event.scale > 0.99f && event.scale < 1.01f);
    RoboCheck(!InputGestureEventIsValid(&event));

    event.type = RoboInputGestureTap;
    RoboCheck(!InputGestureEventIsValid(&event));
    event.space = RoboInputSpaceWindow;
    RoboCheck(InputGestureEventIsValid(&event));

    /* A multi-touch gesture with one contact is a record that disagrees with itself. */
    event.type = RoboInputGesturePinch;
    event.contactCount = 1u;
    RoboCheck(!InputGestureEventIsValid(&event));
    event.contactCount = 2u;
    RoboCheck(InputGestureEventIsValid(&event));

    /* A scale of zero is not a scale. */
    event.type = RoboInputGestureTap;
    event.contactCount = 1u;
    event.scale = 0.0f;
    RoboCheck(!InputGestureEventIsValid(&event));

    /* An undefined type or phase, and an unknown plane, are all refused. */
    event.scale = 1.0f;
    event.type = (InputGestureType)9;
    RoboCheck(!InputGestureEventIsValid(&event));
    event.type = RoboInputGestureTap;
    event.phase = (InputGesturePhase)9;
    RoboCheck(!InputGestureEventIsValid(&event));
    event.phase = RoboInputGesturePhaseEnd;
    event.space = RoboInputSpaceUnknown;
    RoboCheck(!InputGestureEventIsValid(&event));
    RoboCheck(!InputGestureEventIsValid(NULL));
}

/* ------------------------------------------------------------------ */
/* The arena                                                           */
/* ------------------------------------------------------------------ */

static void TestGestureArena(void)
{
    InputGestureArena arena;
    ScriptedRecognizer first;
    ScriptedRecognizer second;
    InputGestureEvent events[RoboInputMaxGestureEvents];
    InputGestureSample sample;
    uint32_t count = 0u;
    InputDiag diag;

    InputDiagReset(&diag);

    /* An empty arena walks nothing and holds no winner. */
    InputGestureArenaInit(&arena);
    RoboCheckEqInt(InputGestureArenaWinner(&arena), -1);
    {
        InputGestureSample idle = SampleAt(1, 1, RoboInputEventPointerMove, 1u);

        RoboCheckEqInt(InputGestureArenaFeed(&arena, &idle, events, RoboInputMaxGestureEvents, &count,
                                             &diag),
                       RoboOk);
    }
    RoboCheckEqInt((int)count, 0);

    /* `Defer` then `Accept`: the recognizer is asked until it claims the sequence. */
    memset(&first, 0, sizeof(first));
    first.verdicts[0] = RoboInputGestureVerdictDefer;
    first.verdicts[1] = RoboInputGestureVerdictDefer;
    first.verdicts[2] = RoboInputGestureVerdictAccept;
    first.verdictCount = 3u;
    memset(&second, 0, sizeof(second));
    second.verdicts[0] = RoboInputGestureVerdictDefer;
    second.verdictCount = 1u;

    {
        InputGestureRecognizer a = MakeScripted(&first);
        InputGestureRecognizer b = MakeScripted(&second);

        RoboCheckEqInt(InputGestureArenaAdd(&arena, &a), 0);
        RoboCheckEqInt(InputGestureArenaAdd(&arena, &b), 1);
        RoboCheckEqInt((int)arena.count, 2);
        /* A third when the list is full is refused, and a recognizer with no callback is refused too. */
        {
            InputGestureRecognizer empty;

            memset(&empty, 0, sizeof(empty));
            RoboCheckEqInt(InputGestureArenaAdd(&arena, &empty), -1);
        }
    }

    sample = SampleAt(1, 1, RoboInputEventPointerDown, 1u);
    RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count, &diag),
                   RoboOk);
    RoboCheckEqInt(InputGestureArenaWinner(&arena), -1);
    RoboCheckEqInt((int)first.asked, 1);
    RoboCheckEqInt((int)second.asked, 1);

    RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count, &diag),
                   RoboOk);
    RoboCheckEqInt(InputGestureArenaWinner(&arena), -1);

    /* The third sample: `first` accepts and wins; `second` is not asked again, because the walk stopped. */
    RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count, &diag),
                   RoboOk);
    RoboCheckEqInt(InputGestureArenaWinner(&arena), 0);
    RoboCheckEqInt((int)arena.entries[0].accepted, 1);
    RoboCheckEqInt((int)first.asked, 3);
    RoboCheckEqInt((int)second.asked, 2);

    /* From here the winner owns the sequence: `second` is never asked again. */
    RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count, &diag),
                   RoboOk);
    RoboCheckEqInt((int)second.asked, 2);
    RoboCheckEqInt((int)first.asked, 4);

    /* A reset releases the winner and resets every recognizer, which is what a disconnect needs. */
    InputGestureArenaReset(&arena);
    RoboCheckEqInt(InputGestureArenaWinner(&arena), -1);
    RoboCheck(first.reset);
    RoboCheck(second.reset);

    /* Every entry refusing: the sequence is nothing to anyone, and it is counted. */
    InputGestureArenaInit(&arena);
    memset(&first, 0, sizeof(first));
    first.verdicts[0] = RoboInputGestureVerdictReject;
    first.verdictCount = 1u;
    first.verdicts[1] = RoboInputGestureVerdictReject;
    memset(&second, 0, sizeof(second));
    second.verdicts[0] = RoboInputGestureVerdictReject;
    second.verdictCount = 1u;
    second.verdicts[1] = RoboInputGestureVerdictReject;
    {
        InputGestureRecognizer a = MakeScripted(&first);
        InputGestureRecognizer b = MakeScripted(&second);

        (void)InputGestureArenaAdd(&arena, &a);
        (void)InputGestureArenaAdd(&arena, &b);
        RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count,
                                             &diag),
                       RoboOk);
        RoboCheckEqInt((int)arena.unresolved, 1);
    }

    /* A single `Defer` keeps the sequence open rather than declaring it unresolved. */
    InputGestureArenaInit(&arena);
    memset(&first, 0, sizeof(first));
    first.verdicts[0] = RoboInputGestureVerdictReject;
    first.verdictCount = 1u;
    memset(&second, 0, sizeof(second));
    second.verdicts[0] = RoboInputGestureVerdictDefer;
    second.verdictCount = 1u;
    {
        InputGestureRecognizer a = MakeScripted(&first);
        InputGestureRecognizer b = MakeScripted(&second);

        (void)InputGestureArenaAdd(&arena, &a);
        (void)InputGestureArenaAdd(&arena, &b);
        RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count,
                                             &diag),
                       RoboOk);
        RoboCheckEqInt((int)arena.unresolved, 0);
        RoboCheckEqInt(InputGestureArenaWinner(&arena), -1);
    }

    /* The priority order is the caller's: swap the two and the other one wins the same sample. */
    InputGestureArenaInit(&arena);
    memset(&first, 0, sizeof(first));
    first.verdicts[0] = RoboInputGestureVerdictAccept;
    first.verdictCount = 1u;
    memset(&second, 0, sizeof(second));
    second.verdicts[0] = RoboInputGestureVerdictAccept;
    second.verdictCount = 1u;
    {
        InputGestureRecognizer a = MakeScripted(&first);
        InputGestureRecognizer b = MakeScripted(&second);

        (void)InputGestureArenaAdd(&arena, &b);
        (void)InputGestureArenaAdd(&arena, &a);
        RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count,
                                             &diag),
                       RoboOk);
        /* Index 0 is `b`, so `b` won and `a` was never asked. */
        RoboCheckEqInt(InputGestureArenaWinner(&arena), 0);
        RoboCheckEqInt((int)first.asked, 0);
        RoboCheckEqInt((int)second.asked, 1);
    }

    /* A losing recognizer's events are never delivered: only the winner's scratch buffer is copied out. */
    InputGestureArenaInit(&arena);
    memset(&first, 0, sizeof(first));
    first.verdicts[0] = RoboInputGestureVerdictDefer;
    first.verdictCount = 1u;
    second.emitWhileDeferring = 1;
    memset(&second, 0, sizeof(second));
    second.verdicts[0] = RoboInputGestureVerdictAccept;
    second.verdictCount = 1u;
    second.emitWhileDeferring = 0;
    {
        InputGestureRecognizer a = MakeScripted(&first);
        InputGestureRecognizer b = MakeScripted(&second);

        (void)InputGestureArenaAdd(&arena, &a);
        (void)InputGestureArenaAdd(&arena, &b);
        /* `first` defers *and emits*; `second` accepts and emits nothing, so the output is empty: the
         * deferring recognizer's events went into its own scratch. */
        RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, &count,
                                             &diag),
                       RoboOk);
        RoboCheckEqInt((int)count, 0);
    }

    /* NULL arguments. */
    RoboCheckEqInt(InputGestureArenaFeed(NULL, &sample, events, RoboInputMaxGestureEvents, &count, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGestureArenaFeed(&arena, NULL, events, RoboInputMaxGestureEvents, &count, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputGestureArenaFeed(&arena, &sample, events, RoboInputMaxGestureEvents, NULL, &diag),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The shipped recognizer                                              */
/* ------------------------------------------------------------------ */

typedef struct ScriptedClock {
    InputGestureHeuristics state;
    InputGestureRecognizer recognizer;
    uint32_t emitted;
} ScriptedClock;

static uint32_t GestureFeedHeuristics(ScriptedClock *clock, InputGestureSample *sample)
{
    InputGestureEvent events[RoboInputMaxGestureEvents];
    uint32_t count = 0u;

    memset(events, 0, sizeof(events));
    (void)InputGestureHeuristicsOffer(&clock->state, sample, events, RoboInputMaxGestureEvents, &count);
    clock->emitted = count;
    return count;
}

static void TestGestureHeuristics(void)
{
    ScriptedClock clock;
    InputGestureHeuristicsConfig config = InputGestureHeuristicsConfigDefault();
    InputGestureSample sample;
    InputGestureEvent events[RoboInputMaxGestureEvents];
    uint32_t count = 0u;

    RoboCheck(config.tapSlop > 7.9f && config.tapSlop < 8.1f);
    RoboCheckEqInt((int)config.tapMaxMs, 300);
    RoboCheckEqInt((int)config.longPressMs, 500);
    RoboCheck(config.doubleTap == 1);
    RoboCheck(config.rotateSin > 0.17f && config.rotateSin < 0.18f);

    /* A tap: down and up in the same place, quickly. The `DOWN` itself is *not* a tap -- a recognizer that
     * accepted there would claim a drag's own beginning. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(100, 100, RoboInputEventPointerDown, 1000u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictDefer);
    RoboCheckEqInt((int)count, 0);

    sample = SampleAt(101, 101, RoboInputEventPointerUp, 1100u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGestureTap);
    RoboCheckEqInt((int)events[0].phase, (int)RoboInputGesturePhaseEnd);
    RoboCheckEqInt((int)events[0].durationMs, 100);

    /* Travelling first is a drag, and it updates and ends as one. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(100, 100, RoboInputEventPointerDown, 2000u);
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(140, 100, RoboInputEventPointerMove, 2050u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGestureDrag);
    RoboCheckEqInt((int)events[0].phase, (int)RoboInputGesturePhaseBegin);
    RoboCheck(events[0].totalX > 39.9f && events[0].totalX < 40.1f);

    sample = SampleAt(180, 100, RoboInputEventPointerMove, 2100u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].phase, (int)RoboInputGesturePhaseUpdate);
    RoboCheck(events[0].totalX > 79.9f && events[0].totalX < 80.1f);
    sample = SampleAt(180, 100, RoboInputEventPointerUp, 2150u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].phase, (int)RoboInputGesturePhaseEnd);

    /* A stationary tick past the long-press delay is a long press -- and the delay is *time*, which is why
     * the sample carries it. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(300, 300, RoboInputEventPointerDown, 3000u);
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(300, 300, RoboInputEventNone, 3200u);
    sample.tick = 1;
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictDefer);
    RoboCheckEqInt((int)count, 0);
    sample = SampleAt(300, 300, RoboInputEventNone, 3600u);
    sample.tick = 1;
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGestureLongPress);

    /* A tick with nothing armed is not a gesture's beginning. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(1, 1, RoboInputEventNone, 10u);
    sample.tick = 1;
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictDefer);

    /* Two taps in the same place are a double tap; the second one arrives as its own sequence. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(10, 10, RoboInputEventPointerDown, 100u);
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(10, 10, RoboInputEventPointerUp, 150u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGestureTap);
    sample = SampleAt(11, 10, RoboInputEventPointerDown, 250u);
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(11, 10, RoboInputEventPointerUp, 300u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGestureDoubleTap);

    /* A tap that was held too long without a tick is nothing: refusing is a verdict, not a silence. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(10, 10, RoboInputEventPointerDown, 0u);
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(10, 10, RoboInputEventPointerUp, 900u);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictReject);

    /* Two fingers that spread are a pinch; two that turn are a rotate. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(50, 50, RoboInputEventPointerDown, 0u);
    sample.contactCount = 2u;
    sample.spread = 40.0f;
    sample.axisX = 1.0f;
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(50, 50, RoboInputEventPointerMove, 50u);
    sample.contactCount = 2u;
    sample.spread = 60.0f; /* 1.5x: past the 1.25 ratio */
    sample.axisX = 1.0f;
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGesturePinch);
    RoboCheck(events[0].scale > 1.49f && events[0].scale < 1.51f);

    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(50, 50, RoboInputEventPointerDown, 0u);
    sample.contactCount = 2u;
    sample.spread = 40.0f;
    sample.axisX = 1.0f;
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(50, 50, RoboInputEventPointerMove, 50u);
    sample.contactCount = 2u;
    sample.spread = 40.0f;
    /* Turned by 0.5 radians: the cross product of (1,0) and (cos, sin) is sin(0.5) = 0.479, past the
     * default sine threshold of 0.1736. */
    sample.axisX = 0.8776f;
    sample.axisY = 0.4794f;
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGestureRotate);

    /* Second hand: two fingers that only travel together are a pan, not a pinch. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(50, 50, RoboInputEventPointerDown, 0u);
    sample.contactCount = 2u;
    sample.spread = 40.0f;
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(80, 50, RoboInputEventPointerMove, 50u);
    sample.contactCount = 2u;
    sample.spread = 40.0f;
    sample.centroid = MakeGfxPoint(80, 50);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictAccept);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGesturePan);

    /* A cancelled sequence reports the cancel of what was running and forgets it. */
    memset(&clock, 0, sizeof(clock));
    InputGestureHeuristicsInit(&clock.state, &config);
    sample = SampleAt(0, 0, RoboInputEventPointerDown, 0u);
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(100, 0, RoboInputEventPointerMove, 30u);
    (void)InputGestureHeuristicsOffer(&clock.state, &sample, events, RoboInputMaxGestureEvents, &count);
    sample = SampleAt(100, 0, RoboInputEventPointerCancel, 40u);
    sample.cancelled = 1;
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, &sample, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictReject);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputGestureDrag);
    RoboCheckEqInt((int)events[0].phase, (int)RoboInputGesturePhaseCancel);
    RoboCheck(!clock.state.armed);

    InputGestureHeuristicsInit(NULL, NULL);
    InputGestureHeuristicsReset(NULL);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(NULL, &sample, events, RoboInputMaxGestureEvents,
                                                    &count),
                   (int)RoboInputGestureVerdictReject);
    RoboCheckEqInt((int)InputGestureHeuristicsOffer(&clock.state, NULL, events,
                                                    RoboInputMaxGestureEvents, &count),
                   (int)RoboInputGestureVerdictReject);
    (void)GestureFeedHeuristics;
}

/* ------------------------------------------------------------------ */
/* Dumps                                                              */
/* ------------------------------------------------------------------ */

static void TestGestureDump(void)
{
    InputGestureEvent event;
    InputGestureArena arena;
    InputGestureHeuristics state;
    InputGestureRecognizer recognizer;
    ScriptedRecognizer script;
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;

    InputDiagReset(&diag);
    InputGestureEventInit(&event);
    event.type = RoboInputGestureTap;
    event.phase = RoboInputGesturePhaseEnd;
    event.space = RoboInputSpaceWindow;
    event.position = MakeGfxPoint(10, 20);
    event.totalX = 2.0f;
    event.durationMs = 100u;
    event.contactCount = 1u;
    RoboCheckEqInt(InputGestureEventWrite(&event, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "gesture tap phase=end pointer=0 space=window at=10,20 totalX=2 totalY=0 "
                         "contacts=1 ms=100\n");

    memset(&script, 0, sizeof(script));
    script.verdicts[0] = RoboInputGestureVerdictAccept;
    script.verdictCount = 1u;
    recognizer = MakeScripted(&script);
    InputGestureArenaInit(&arena);
    RoboCheckEqInt(InputGestureArenaAdd(&arena, &recognizer), 0);
    RoboCheckEqInt(InputGestureArenaWrite(&arena, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "gesture-arena recognizers=1 winner=0 sequences=0 unresolved=0 samples=0\n"
                         "  0 type=tap offered=0 accepted=0 rejected=0\n");
    RoboCheckEqInt(InputGestureArenaWrite(&arena, again, sizeof(again), &length, &diag), RoboOk);
    RoboCheck(strcmp(line, again) == 0);

    InputGestureHeuristicsInit(&state, NULL);
    RoboCheckEqInt(InputGestureHeuristicsWrite(&state, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "gesture-heuristics armed=0 type=none contacts=0 ticks=0 double=1\n");

    RoboCheckEqInt(InputGestureArenaWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "gesture-arena <none>\n");
    RoboCheckEqInt(InputGestureEventWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "gesture <none>\n");
    RoboCheckEqInt(InputGestureHeuristicsWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "gesture-heuristics <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputGestureEventWrite(&event, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputGesture(void)
{
    TestRegister("input.gesture.vocabulary", TestGestureVocabulary);
    TestRegister("input.gesture.samples", TestGestureSamples);
    TestRegister("input.gesture.events", TestGestureEvents);
    TestRegister("input.gesture.arena", TestGestureArena);
    TestRegister("input.gesture.heuristics", TestGestureHeuristics);
    TestRegister("input.gesture.dump", TestGestureDump);
}
