/*
 * RoboInput latency suite (Phase 13.48) — `input.latency.*`.
 *
 * Five cases, and the two that matter are the refusals rather than the arithmetic:
 *
 *   `vocabulary`    the five stages, positionally, and which two `Begin` takes: a caller that marked
 *                   `hardware` itself would be overwriting the stamp the record exists to hold;
 *
 *   `lifecycle`     a record's numbers at each step -- partial before the handler returns, complete at
 *                   `Present`, still readable afterwards, with the aggregates absorbing it exactly once;
 *
 *   `out_of_order`  a stamp earlier than a stage already marked is **refused** and counted, and so is a
 *                   `Begin` whose queue stamp precedes its hardware stamp. Saturating either would report a
 *                   suspiciously *good* latency, which is the one failure mode this layer must not have;
 *
 *   `bounds`        sixteen records, the seventeenth refused with `RoboBusy` rather than an eviction, and a
 *                   completed record freeing its slot -- so the last sixteen events stay answerable;
 *
 *   `dump`          two generations byte for byte, the counters matching what was done, and the buffer
 *                   bound: exactly `RoboInputLatencyDumpMax` fits a full table and one byte less is `?`.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** A pointer move at @p ms, with its timestamp set -- the record's identity and nothing more. */
static InputEvent LatencyEvent(InputDeviceId device, uint64_t ms)
{
    InputEvent event = InputPointerMove(10, 10, 0u, RoboInputSpaceWindow, RoboWindowIdNone);
    InputDevice identity = GetRoboInputDevice(device, RoboInputSourceMouse, "mouse");

    RoboCheckEqInt(InputEventSetDevice(&event, &identity, NULL), RoboOk);
    RoboCheckEqInt(InputEventSetTimestamp(&event, ms), RoboOk);
    return event;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestLatencyVocabulary(void)
{
    RoboCheckEqInt(RoboInputLatencyHardware, 0);
    RoboCheckEqInt(RoboInputLatencyQueue, 1);
    RoboCheckEqInt(RoboInputLatencyDispatch, 2);
    RoboCheckEqInt(RoboInputLatencyHandler, 3);
    RoboCheckEqInt(RoboInputLatencyPresent, 4);
    RoboCheckEqInt((int)RoboInputLatencyStageCount, 5);
    RoboCheckEqInt((int)RoboInputLatencyRecords, 16);

    RoboCheckEqInt(InputLatencyStageIsKnown(RoboInputLatencyHardware), 1);
    RoboCheckEqInt(InputLatencyStageIsKnown(RoboInputLatencyPresent), 1);
    RoboCheckEqInt(InputLatencyStageIsKnown((InputLatencyStage)RoboInputLatencyStageCount), 0);
    RoboCheckEqInt(InputLatencyStageIsKnown((InputLatencyStage)99), 0);
    RoboCheckEqInt(InputLatencyStageIsKnown((InputLatencyStage)-1), 0);

    RoboCheckStrEq(InputLatencyStageName(RoboInputLatencyHardware), "hardware");
    RoboCheckStrEq(InputLatencyStageName(RoboInputLatencyQueue), "queue");
    RoboCheckStrEq(InputLatencyStageName(RoboInputLatencyDispatch), "dispatch");
    RoboCheckStrEq(InputLatencyStageName(RoboInputLatencyHandler), "handler");
    RoboCheckStrEq(InputLatencyStageName(RoboInputLatencyPresent), "present");
    RoboCheckStrEq(InputLatencyStageName((InputLatencyStage)99), "unknown");

    /* `Begin` takes the first two, so the layer marks the last three. */
    RoboCheckEqInt(InputLatencyStageIsMarked(RoboInputLatencyHardware), 0);
    RoboCheckEqInt(InputLatencyStageIsMarked(RoboInputLatencyQueue), 0);
    RoboCheckEqInt(InputLatencyStageIsMarked(RoboInputLatencyDispatch), 1);
    RoboCheckEqInt(InputLatencyStageIsMarked(RoboInputLatencyHandler), 1);
    RoboCheckEqInt(InputLatencyStageIsMarked(RoboInputLatencyPresent), 1);
}

/* ------------------------------------------------------------------ */
/* The lifecycle                                                       */
/* ------------------------------------------------------------------ */

static void TestLatencyLifecycle(void)
{
    InputLatencyTrace trace;
    InputEvent event;
    InputLatencyDurations durations;
    uint32_t sequence = 0u;
    uint32_t first = 0u;

    InputLatencyTraceInit(&trace);
    RoboCheckEqInt((int)InputLatencyLive(&trace), 0);
    RoboCheckEqInt((uint64_t)trace.nextSequence, 1u);

    event = LatencyEvent(4u, 1000u);
    RoboCheckEqInt(InputLatencyBegin(&trace, &event, 1000u, 1002u, &sequence, NULL), RoboOk);
    RoboCheck(sequence != 0u);
    first = sequence;
    RoboCheckEqInt((int)InputLatencyLive(&trace), 1);

    /* Nothing marked yet: the four differences are zero and the record says so. */
    RoboCheckEqInt(InputLatencyMeasure(&trace, sequence, &durations, NULL), RoboOk);
    RoboCheckEqInt((int)durations.complete, 0);
    RoboCheckEqInt((uint64_t)durations.dispatchMs, 0u);
    RoboCheckEqInt((uint64_t)durations.presentMs, 0u);

    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyDispatch, 1004u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMeasure(&trace, sequence, &durations, NULL), RoboOk);
    RoboCheckEqInt((uint64_t)durations.dispatchMs, 4u);
    RoboCheckEqInt((int)durations.complete, 0);
    RoboCheckEqInt((uint64_t)durations.handlerMs, 0u);

    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHandler, 1009u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMeasure(&trace, sequence, &durations, NULL), RoboOk);
    RoboCheckEqInt((uint64_t)durations.dispatchMs, 4u);
    RoboCheckEqInt((uint64_t)durations.handlerMs, 5u);
    /* Frame is dispatch plus handler, by its own definition: input to the frame the result is drawn into. */
    RoboCheckEqInt((uint64_t)durations.frameMs, 9u);
    RoboCheckEqInt((int)durations.complete, 0);

    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyPresent, 1016u, NULL), RoboOk);
    RoboCheckEqInt((int)InputLatencyLive(&trace), 0);

    /* Still addressable, and final. */
    RoboCheckEqInt(InputLatencyMeasure(&trace, first, &durations, NULL), RoboOk);
    RoboCheckEqInt((int)durations.complete, 1);
    RoboCheckEqInt((uint64_t)durations.dispatchMs, 4u);
    RoboCheckEqInt((uint64_t)durations.handlerMs, 5u);
    RoboCheckEqInt((uint64_t)durations.frameMs, 9u);
    RoboCheckEqInt((uint64_t)durations.presentMs, 16u);

    RoboCheckEqInt(trace.completedTotal, 1u);
    RoboCheckEqInt(trace.dispatchLastMs, 4u);
    RoboCheckEqInt(trace.dispatchMaxMs, 4u);
    RoboCheckEqInt(trace.dispatchSumMs, 4u);
    RoboCheckEqInt(trace.presentLastMs, 16u);
    RoboCheckEqInt(trace.presentMaxMs, 16u);

    /* A second record, slower: the aggregates accumulate, the last is the newest and the maximum holds. */
    event = LatencyEvent(4u, 2000u);
    RoboCheckEqInt(InputLatencyBegin(&trace, &event, 2000u, 2000u, &sequence, NULL), RoboOk);
    RoboCheck(sequence != first);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyDispatch, 2012u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHandler, 2014u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyPresent, 2030u, NULL), RoboOk);
    RoboCheckEqInt(trace.dispatchLastMs, 12u);
    RoboCheckEqInt(trace.dispatchMaxMs, 12u);
    RoboCheckEqInt(trace.dispatchSumMs, 16u);
    RoboCheckEqInt(trace.completedTotal, 2u);
    RoboCheckEqInt(trace.begunTotal, 2u);
    RoboCheckEqInt(trace.markedTotal, 6u);
    RoboCheckEqInt((int)InputLatencyLive(&trace), 0);

    /* The first record is still readable after a second one completed. */
    RoboCheckEqInt(InputLatencyMeasure(&trace, first, &durations, NULL), RoboOk);
    RoboCheckEqInt((uint64_t)durations.presentMs, 16u);
}

/* ------------------------------------------------------------------ */
/* Ordering                                                            */
/* ------------------------------------------------------------------ */

static void TestLatencyOutOfOrder(void)
{
    InputLatencyTrace trace;
    InputEvent event;
    InputDiag diag = { 0 };
    uint32_t sequence = 0u;

    InputLatencyTraceInit(&trace);
    event = LatencyEvent(1u, 500u);

    /* The queue cannot receive an event before the device reported it. */
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputLatencyBegin(&trace, &event, 500u, 499u, &sequence, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSession);
    RoboCheckEqInt(trace.outOfOrderTotal, 1u);
    RoboCheckEqInt(trace.begunTotal, 0u);

    RoboCheckEqInt(InputLatencyBegin(&trace, &event, 500u, 500u, &sequence, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyDispatch, 506u, NULL), RoboOk);

    /* A stamp before the stage already marked is refused, and the clock that produced it is reported. */
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHandler, 505u, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSession);
    RoboCheckEqInt(trace.outOfOrderTotal, 2u);
    RoboCheckEqInt(trace.markedTotal, 1u);

    /* The same stamp is *not* out of order: equal is ordered. */
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHandler, 506u, NULL), RoboOk);

    /* A stage may not be marked twice. */
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHandler, 520u, NULL),
                   RoboAlreadyExists);
    RoboCheckEqInt(trace.markedTotal, 2u);

    /* The two stages `Begin` owns are refused as marks rather than silently overwritten. */
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHardware, 900u, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyQueue, 901u, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, (InputLatencyStage)99, 950u, NULL),
                   RoboInvalidArgument);

    /* An unknown sequence: no record owns it, and no record may be invented. */
    RoboCheckEqInt(InputLatencyMark(&trace, 4242u, RoboInputLatencyPresent, 960u, NULL), RoboNotFound);
    /* Zero is not a handle: handles start at one, so a caller that lost its sequence is told so rather than
     * told "no record owns it". */
    RoboCheckEqInt(InputLatencyMark(&trace, 0u, RoboInputLatencyPresent, 960u, NULL), RoboInvalidArgument);
    {
        InputLatencyDurations durations;

        RoboCheckEqInt(InputLatencyMeasure(&trace, 4242u, &durations, NULL), RoboNotFound);
        RoboCheckEqInt(InputLatencyMeasure(&trace, 0u, &durations, NULL), RoboNotFound);
        RoboCheckEqInt(InputLatencyMeasure(NULL, 1u, &durations, NULL), RoboInvalidArgument);
        RoboCheckEqInt(InputLatencyMeasure(&trace, 1u, NULL, NULL), RoboInvalidArgument);
    }

    RoboCheckEqInt(InputLatencyBegin(NULL, &event, 0u, 0u, &sequence, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputLatencyBegin(&trace, NULL, 0u, 0u, &sequence, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputLatencyBegin(&trace, &event, 0u, 0u, NULL, NULL), RoboInvalidArgument);

    /* An invalid event is refused and counted, like every other door in this module. */
    event.space = RoboInputSpaceUnknown;
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputLatencyBegin(&trace, &event, 0u, 0u, &sequence, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainSession);
    /* Every refusal this case caused, counted: the backwards begin, the backwards mark, the double mark,
     * the two `Begin`-owned stages, the undefined stage, the unknown sequence, the zero sequence and the
     * invalid event. Nothing that was merely *answered* is in here -- a `RoboNotFound` from a measurement is
     * a question, not a rejection. */
    RoboCheckEqInt(trace.refusedTotal, 9u);

    /* A refused mark leaves the record exactly as it was: the rule is stated once, in one direction. */
    RoboCheckEqInt(trace.completedTotal, 0u);
    RoboCheckEqInt((int)InputLatencyLive(&trace), 1);
}

/* ------------------------------------------------------------------ */
/* Bounds                                                              */
/* ------------------------------------------------------------------ */

static void TestLatencyBounds(void)
{
    InputLatencyTrace trace;
    InputEvent event;
    uint32_t sequence = 0u;
    uint32_t index;

    InputLatencyTraceInit(&trace);
    event = LatencyEvent(1u, 10u);

    for (index = 0u; index < (uint32_t)RoboInputLatencyRecords; index++) {
        RoboCheckEqInt(InputLatencyBegin(&trace, &event, 10u, 11u, &sequence, NULL), RoboOk);
    }
    RoboCheckEqInt((int)InputLatencyLive(&trace), (int)RoboInputLatencyRecords);
    RoboCheckEqInt(trace.begunTotal, (uint64_t)RoboInputLatencyRecords);

    /* Full: refused rather than evicting a measurement that was already paid for. */
    {
        InputDiag diag = { 0 };

        RoboCheckEqInt(InputLatencyBegin(&trace, &event, 10u, 11u, &sequence, &diag), RoboBusy);
        RoboCheckEqInt(diag.domain, RoboInputDomainSession);
        RoboCheckEqInt(trace.refusedTotal, 1u);
    }

    /* Completing one frees a slot, and the freed slot takes the next record -- so a frame's worth of events
     * is addressable rather than sixteen forever. */
    sequence = trace.records[3].sequence;
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyDispatch, 12u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHandler, 13u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyPresent, 14u, NULL), RoboOk);
    RoboCheckEqInt((int)InputLatencyLive(&trace), (int)RoboInputLatencyRecords - 1);

    {
        uint32_t fresh = 0u;

        /* Every slot is held, so the oldest **finished** record gives way -- a fresh handle, a full table
         * of live records, and one question that no longer has an answer. */
        RoboCheckEqInt(InputLatencyBegin(&trace, &event, 20u, 21u, &fresh, NULL), RoboOk);
        RoboCheck(fresh != sequence);
        RoboCheckEqInt((int)InputLatencyLive(&trace), (int)RoboInputLatencyRecords);
        {
            InputLatencyDurations durations;

            RoboCheckEqInt(InputLatencyMeasure(&trace, sequence, &durations, NULL), RoboNotFound);
        }
        /* Its numbers are not lost: they joined the aggregates when it completed. */
        RoboCheckEqInt(trace.completedTotal, 1u);
        RoboCheckEqInt(trace.presentSumMs, 4u);
    }

    /* All sixteen live and none finished: that is a caller timing more at once than the trace holds, and it
     * is told so rather than having a measurement discarded under it. */
    {
        InputDiag diag = { 0 };
        uint32_t refused = 0u;

        RoboCheckEqInt(InputLatencyBegin(&trace, &event, 30u, 31u, &refused, &diag), RoboBusy);
        RoboCheckEqInt(diag.domain, RoboInputDomainSession);
    }
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

static void TestLatencyDump(void)
{
    InputLatencyTrace trace;
    InputEvent event;
    char first[RoboInputLatencyDumpMax];
    char second[RoboInputLatencyDumpMax];
    char small[64];
    size_t firstLength = 0u;
    size_t secondLength = 0u;
    size_t needed = 0u;
    uint32_t sequence = 0u;

    InputLatencyTraceInit(&trace);
    event = LatencyEvent(2u, 100u);
    RoboCheckEqInt(InputLatencyBegin(&trace, &event, 100u, 101u, &sequence, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyDispatch, 108u, NULL), RoboOk);

    RoboCheckEqInt(InputLatencyWrite(&trace, first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyWrite(&trace, second, sizeof(second), &secondLength, NULL), RoboOk);
    RoboCheckEqInt((int)firstLength, (int)secondLength);
    RoboCheckStrEq(first, second);
    RoboCheck(strstr(first, "latency records=16 live=1") != NULL);
    RoboCheck(strstr(first, "out-of-order=0") != NULL);
    RoboCheck(strstr(first, "state=live") != NULL);
    RoboCheck(strstr(first, "hardware=100") != NULL);
    RoboCheck(strstr(first, "dispatch=108") != NULL);
    /* A stage that has not arrived has no field at all: a zero stamp would read as "instant". */
    RoboCheck(strstr(first, "handler=") == NULL);

    /* Complete it, and the dump says so. */
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyHandler, 110u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyMark(&trace, sequence, RoboInputLatencyPresent, 120u, NULL), RoboOk);
    RoboCheckEqInt(InputLatencyWrite(&trace, first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheck(strstr(first, "latency records=16 live=0") != NULL);
    RoboCheck(strstr(first, "state=complete") != NULL);
    RoboCheck(strstr(first, "completed=1") != NULL);
    RoboCheck(strstr(first, "present=120") != NULL);

    /* A full table at exactly the stated bound, then one byte less. */
    {
        uint32_t index;

        for (index = 0u; index < (uint32_t)RoboInputLatencyRecords; index++) {
            InputEvent one = LatencyEvent(1u, 200u + index);

            RoboCheckEqInt(InputLatencyBegin(&trace, &one, 200u + index, 200u + index, &sequence, NULL),
                           RoboOk);
        }
    }
    RoboCheckEqInt(InputLatencyWrite(&trace, first, sizeof(first), &needed, NULL), RoboOk);
    /* The stated bound is an upper bound on a full table, which is the claim the constant makes. */
    RoboCheck((int)needed <= (int)RoboInputLatencyDumpMax);
    RoboCheckEqInt(InputLatencyWrite(&trace, small, sizeof(small), &needed, NULL), RoboNotSupported);
    RoboCheck(small[0] == '?');
    RoboCheck(needed > sizeof(small));

    RoboCheckEqInt(InputLatencyWrite(NULL, first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheckStrEq(first, "latency <none>\n");
    RoboCheckEqInt(InputLatencyWrite(&trace, NULL, 0u, &firstLength, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputLatencyWrite(&trace, first, sizeof(first), NULL, NULL), RoboInvalidArgument);
}

void TestRegisterInputLatency(void)
{
    TestRegister("input.latency.vocabulary", TestLatencyVocabulary);
    TestRegister("input.latency.lifecycle", TestLatencyLifecycle);
    TestRegister("input.latency.out_of_order", TestLatencyOutOfOrder);
    TestRegister("input.latency.bounds", TestLatencyBounds);
    TestRegister("input.latency.dump", TestLatencyDump);
}
