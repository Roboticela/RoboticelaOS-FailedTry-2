/*
 * Input latency (Phase 13.48) — see `RoboInputLatency.h`.
 *
 * Four pieces, in the order a call meets them: the stage vocabulary, `Begin` (which takes two of the five
 * stamps and hands out a handle), `Mark` (which takes the other three and refuses a stamp that went
 * backwards), and `Measure` (which subtracts). The aggregates are folded at completion rather than at read
 * time, so a reader of a long session costs nothing.
 *
 * `input.latency.rules` is the case that pins the two rules this file exists to keep: an out-of-order stamp
 * is refused instead of saturated, and a full table refuses instead of evicting.
 */

#include "RoboInput/RoboInputLatency.h"

#include "RoboInputInternal.h"

#include <string.h>

static const char *const RiLatencyStageNames[RoboInputLatencyStageCount] = {
    "hardware",
    "queue",
    "dispatch",
    "handler",
    "present"
};

int InputLatencyStageIsKnown(InputLatencyStage stage)
{
    return (int)stage >= 0 && (size_t)stage < (size_t)RoboInputLatencyStageCount;
}

const char *InputLatencyStageName(InputLatencyStage stage)
{
    if (!InputLatencyStageIsKnown(stage)) {
        return "unknown";
    }
    return RiLatencyStageNames[(size_t)stage];
}

int InputLatencyStageIsMarked(InputLatencyStage stage)
{
    return stage == RoboInputLatencyDispatch || stage == RoboInputLatencyHandler ||
           stage == RoboInputLatencyPresent;
}

/* ------------------------------------------------------------------ */
/* The records                                                         */
/* ------------------------------------------------------------------ */

/** The addressable record @p sequence owns -- unfinished or finished. NULL when no slot holds it. */
static InputLatencyRecord *RiLatencyFind(InputLatencyTrace *trace, uint32_t sequence)
{
    uint32_t index;

    if (!trace || sequence == 0u) {
        return NULL;
    }
    for (index = 0u; index < RoboInputLatencyRecords; index++) {
        if (trace->records[index].sequence == sequence) {
            return &trace->records[index];
        }
    }
    return NULL;
}

/** The newest stage already marked, or -1 when none is. */
static int RiLatencyNewestStage(const InputLatencyRecord *record)
{
    int stage;

    for (stage = (int)RoboInputLatencyStageCount - 1; stage >= 0; stage--) {
        if ((record->setMask & (uint32_t)(1u << (uint32_t)stage)) != 0u) {
            return stage;
        }
    }
    return -1;
}

void InputLatencyTraceInit(InputLatencyTrace *trace)
{
    if (!trace) {
        return;
    }
    memset(trace, 0, sizeof(*trace));
    trace->nextSequence = 1u;
}

uint32_t InputLatencyLive(const InputLatencyTrace *trace)
{
    return trace ? trace->live : 0u;
}

/* ------------------------------------------------------------------ */
/* Begin                                                               */
/* ------------------------------------------------------------------ */

Result InputLatencyBegin(InputLatencyTrace *trace,
                         const InputEvent *event,
                         uint64_t hardwareMs,
                         uint64_t queuedMs,
                         uint32_t *outSequence,
                         InputDiag *diag)
{
    uint32_t index;
    InputLatencyRecord *record = NULL;

    if (!trace || !event || !outSequence) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "a latency record needs a trace, an event and a handle");
    }
    *outSequence = 0u;

    if (!InputEventIsValid(event)) {
        trace->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "an invalid event is not timed");
    }
    /* The queue cannot receive an event before the device reported it. Refusing rather than reordering the
     * pair keeps "a stamp never goes backwards" true of the whole record, not just of the marks. */
    if (queuedMs < hardwareMs) {
        trace->refusedTotal++;
        trace->outOfOrderTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "the queue stamped an event before the device did");
    }

    /* A never-used slot first, so a quiet trace never overwrites an answer. */
    for (index = 0u; index < RoboInputLatencyRecords; index++) {
        if (trace->records[index].sequence == 0u) {
            record = &trace->records[index];
            break;
        }
    }
    if (!record) {
        /* Nothing unused: the oldest **finished** record is reused (its numbers have already joined the
         * aggregates, so nothing is lost but its per-event line). A table whose records are all still
         * *live* is a different thing -- that is a caller timing more events at once than the trace holds,
         * and it says so rather than discarding a measurement nobody has read yet. */
        int found = 0;
        uint32_t oldest = 0u;

        for (index = 0u; index < RoboInputLatencyRecords; index++) {
            if (!trace->records[index].complete) {
                continue;
            }
            if (!found || trace->records[index].sequence < trace->records[oldest].sequence) {
                oldest = index;
                found = 1;
            }
        }
        if (!found) {
            trace->refusedTotal++;
            return RoboInputDiag(diag, RoboInputDomainSession, RoboBusy,
                                 "every latency record is live; a trace refuses rather than discarding "
                                 "a measurement nobody has read");
        }
        record = &trace->records[oldest];
    }

    memset(record, 0, sizeof(*record));
    record->active = 1;
    record->sequence = trace->nextSequence;
    record->type = event->type;
    record->device = event->device.id;
    record->stamps[(size_t)RoboInputLatencyHardware] = hardwareMs;
    record->stamps[(size_t)RoboInputLatencyQueue] = queuedMs;
    record->setMask = (uint32_t)((1u << (uint32_t)RoboInputLatencyHardware) |
                                 (1u << (uint32_t)RoboInputLatencyQueue));

    trace->nextSequence++;
    if (trace->nextSequence == 0u) {
        trace->nextSequence = 1u;
    }
    trace->live++;
    trace->begunTotal++;
    *outSequence = record->sequence;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Mark                                                                */
/* ------------------------------------------------------------------ */

/** Fold a completed record's four differences into the aggregates. */
static void RiLatencyAbsorb(InputLatencyTrace *trace, const InputLatencyDurations *durations)
{
    trace->dispatchSumMs += durations->dispatchMs;
    trace->dispatchLastMs = durations->dispatchMs;
    if (durations->dispatchMs > trace->dispatchMaxMs) {
        trace->dispatchMaxMs = durations->dispatchMs;
    }

    trace->handlerSumMs += durations->handlerMs;
    trace->handlerLastMs = durations->handlerMs;
    if (durations->handlerMs > trace->handlerMaxMs) {
        trace->handlerMaxMs = durations->handlerMs;
    }

    trace->frameSumMs += durations->frameMs;
    trace->frameLastMs = durations->frameMs;
    if (durations->frameMs > trace->frameMaxMs) {
        trace->frameMaxMs = durations->frameMs;
    }

    trace->presentSumMs += durations->presentMs;
    trace->presentLastMs = durations->presentMs;
    if (durations->presentMs > trace->presentMaxMs) {
        trace->presentMaxMs = durations->presentMs;
    }
}

Result InputLatencyMark(InputLatencyTrace *trace,
                        uint32_t sequence,
                        InputLatencyStage stage,
                        uint64_t stampMs,
                        InputDiag *diag)
{
    InputLatencyRecord *record;
    int newest;

    if (!trace) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "a mark needs a trace");
    }
    if (!InputLatencyStageIsKnown(stage)) {
        trace->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "an undefined latency stage is not marked");
    }
    if (!InputLatencyStageIsMarked(stage)) {
        trace->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "begin takes the hardware and queue stamps; a caller marks the other three");
    }

    if (sequence == 0u) {
        /* Not a handle at all: handles start at 1, so this is a caller bug rather than a missing record. */
        trace->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "zero is not a latency handle; begin hands them out from one");
    }
    record = RiLatencyFind(trace, sequence);
    if (!record) {
        trace->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboNotFound,
                             "no latency record owns that sequence");
    }
    if ((record->setMask & (uint32_t)(1u << (uint32_t)stage)) != 0u) {
        trace->refusedTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboAlreadyExists,
                             "that stage already has a stamp on this record");
    }

    /* The rule this layer exists to keep. A stamp that went backwards is a call site measuring from the
     * wrong clock; saturated to zero it would report a suspiciously *good* latency, and a plausible wrong
     * number is worse than a refusal. */
    newest = RiLatencyNewestStage(record);
    if (newest >= 0 && stampMs < record->stamps[(size_t)newest]) {
        trace->refusedTotal++;
        trace->outOfOrderTotal++;
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "a latency stamp may not precede a stage already marked");
    }

    record->stamps[(size_t)stage] = stampMs;
    record->setMask |= (uint32_t)(1u << (uint32_t)stage);
    trace->markedTotal++;

    if (stage == RoboInputLatencyPresent) {
        InputLatencyDurations durations;

        memset(&durations, 0, sizeof(durations));
        durations.dispatchMs = record->stamps[(size_t)RoboInputLatencyDispatch] -
                               record->stamps[(size_t)RoboInputLatencyHardware];
        durations.handlerMs = record->stamps[(size_t)RoboInputLatencyHandler] -
                              record->stamps[(size_t)RoboInputLatencyDispatch];
        durations.frameMs = record->stamps[(size_t)RoboInputLatencyHandler] -
                            record->stamps[(size_t)RoboInputLatencyHardware];
        durations.presentMs = record->stamps[(size_t)RoboInputLatencyPresent] -
                              record->stamps[(size_t)RoboInputLatencyHardware];
        durations.complete = 1;
        RiLatencyAbsorb(trace, &durations);
        trace->completedTotal++;
        /* The record stays where it is: its five stamps and four differences are readable until a later
         * `Begin` takes the slot. `active` goes 0, `complete` goes 1. */
        record->active = 0;
        record->complete = 1;
        trace->live--;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Measure                                                             */
/* ------------------------------------------------------------------ */

Result InputLatencyMeasure(const InputLatencyTrace *trace,
                           uint32_t sequence,
                           InputLatencyDurations *out,
                           InputDiag *diag)
{
    const InputLatencyRecord *record;
    int hasDispatch;
    int hasHandler;
    int hasPresent;

    if (!trace || !out) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "a measurement needs a trace and somewhere to put it");
    }
    memset(out, 0, sizeof(*out));

    record = RiLatencyFind((InputLatencyTrace *)trace, sequence);
    if (!record) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboNotFound,
                             "no addressable latency record owns that sequence");
    }

    hasDispatch = (record->setMask & (uint32_t)(1u << (uint32_t)RoboInputLatencyDispatch)) != 0u;
    hasHandler = (record->setMask & (uint32_t)(1u << (uint32_t)RoboInputLatencyHandler)) != 0u;
    hasPresent = (record->setMask & (uint32_t)(1u << (uint32_t)RoboInputLatencyPresent)) != 0u;

    if (hasDispatch) {
        out->dispatchMs = record->stamps[(size_t)RoboInputLatencyDispatch] -
                          record->stamps[(size_t)RoboInputLatencyHardware];
    }
    if (hasDispatch && hasHandler) {
        out->handlerMs = record->stamps[(size_t)RoboInputLatencyHandler] -
                         record->stamps[(size_t)RoboInputLatencyDispatch];
        out->frameMs = record->stamps[(size_t)RoboInputLatencyHandler] -
                       record->stamps[(size_t)RoboInputLatencyHardware];
    }
    if (hasPresent) {
        out->presentMs = record->stamps[(size_t)RoboInputLatencyPresent] -
                         record->stamps[(size_t)RoboInputLatencyHardware];
    }
    out->complete = hasDispatch && hasHandler && hasPresent;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result InputLatencyWrite(const InputLatencyTrace *trace,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         InputDiag *diag)
{
    InputWriter text;
    uint32_t index;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!trace) {
        RiWriterText(&text, "latency <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "latency records=");
    RiWriterU64(&text, (uint64_t)RoboInputLatencyRecords);
    RiWriterFieldU64(&text, " live", (uint64_t)trace->live);
    RiWriterFieldU64(&text, " begun", trace->begunTotal);
    RiWriterFieldU64(&text, " marked", trace->markedTotal);
    RiWriterFieldU64(&text, " completed", trace->completedTotal);
    RiWriterFieldU64(&text, " refused", trace->refusedTotal);
    RiWriterFieldU64(&text, " out-of-order", trace->outOfOrderTotal);
    RiWriterChar(&text, '\n');

    RiWriterText(&text, "  aggregate dispatch");
    RiWriterFieldU64(&text, " sum", trace->dispatchSumMs);
    RiWriterFieldU64(&text, " max", trace->dispatchMaxMs);
    RiWriterFieldU64(&text, " last", trace->dispatchLastMs);
    RiWriterText(&text, " handler");
    RiWriterFieldU64(&text, " sum", trace->handlerSumMs);
    RiWriterFieldU64(&text, " max", trace->handlerMaxMs);
    RiWriterFieldU64(&text, " last", trace->handlerLastMs);
    RiWriterChar(&text, '\n');
    RiWriterText(&text, "  aggregate frame");
    RiWriterFieldU64(&text, " sum", trace->frameSumMs);
    RiWriterFieldU64(&text, " max", trace->frameMaxMs);
    RiWriterFieldU64(&text, " last", trace->frameLastMs);
    RiWriterText(&text, " present");
    RiWriterFieldU64(&text, " sum", trace->presentSumMs);
    RiWriterFieldU64(&text, " max", trace->presentMaxMs);
    RiWriterFieldU64(&text, " last", trace->presentLastMs);
    RiWriterChar(&text, '\n');

    for (index = 0u; index < RoboInputLatencyRecords; index++) {
        const InputLatencyRecord *record = &trace->records[index];
        int stage;

        if (record->sequence == 0u) {
            continue;
        }
        RiWriterText(&text, "  record ");
        RiWriterU64(&text, (uint64_t)record->sequence);
        RiWriterFieldText(&text, " state", record->complete ? "complete" : "live");
        RiWriterFieldText(&text, " type", InputEventTypeName(record->type));
        RiWriterFieldU64(&text, " device", (uint64_t)record->device);
        for (stage = 0; stage < (int)RoboInputLatencyStageCount; stage++) {
            if ((record->setMask & (uint32_t)(1u << (uint32_t)stage)) == 0u) {
                continue;
            }
            RiWriterChar(&text, ' ');
            RiWriterText(&text, RiLatencyStageNames[(size_t)stage]);
            RiWriterChar(&text, '=');
            RiWriterU64(&text, record->stamps[(size_t)stage]);
        }
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
