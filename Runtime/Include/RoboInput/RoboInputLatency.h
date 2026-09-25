/*
 * Input latency metrics (Phase 13.48).
 *
 * An input system that cannot say **how long the user waited** cannot be improved, and the question has
 * exactly one shape: five stamps taken at five places in the pipeline, and the differences between them.
 *
 *     Hardware ──→ Queue ──→ Dispatch ──→ Handler ──→ Present
 *         │           │          │            │           │
 *         │           │          │            │           └─ the frame carrying the result was submitted
 *         │           │          │            └─ the target's handler returned
 *         │           │          └─ routing began: something may now happen
 *         │           └─ the event reached the queue
 *         └─ when the device says it happened
 *
 * The three numbers the brief names are `dispatchMs` (input to dispatch), `frameMs` (input to the frame that
 * carries the result, which is the handler's return) and `presentMs` (input to present). `handlerMs` is the
 * fourth, and it is the one that says whether the delay was *waiting* or *working* -- a distinction without
 * which the first three are unactionable.
 *
 * ## Milliseconds, because that is what the event's own timestamp is
 *
 * A stamp is a `uint64_t` in milliseconds on the same arbitrary epoch `InputEvent::timestampMs` uses. A
 * recorder that kept its own timebase would be measuring a different clock than the events it is describing,
 * and the first thing a reader would have to check is whether the two agree.
 *
 * ## Out of order is a refusal, and it is counted
 *
 * A stamp earlier than a stage already marked on the same record is **refused** (`RoboInvalidArgument`) and
 * counted. Saturating it to zero instead would produce a plausible small number -- a call site that measured
 * from the wrong clock would report a *good* latency, which is the one failure mode this layer must not have.
 * Likewise a `Begin` whose queue stamp precedes its hardware stamp.
 *
 * ## A micro trace, not a profiler
 *
 * The records are a fixed, small table (`RoboInputLatencyRecords`): enough for one frame's worth of events,
 * no allocator, and a table that is full refuses (`RoboBusy`) rather than evicting -- an evicted record is a
 * measurement that was silently paid for and thrown away. The aggregates are the point: a caller reads the
 * last, the maximum and the sum per stage, which is what a diagnostics overlay prints.
 *
 * This is deliberately not Phase 41. There is no sampling policy, no histogram bucket, no per-target
 * breakdown, and no storage beyond the process: the brief asks for five stamps and three differences, and
 * saying what is absent is how that stays true.
 */
#ifndef ROBO_INPUT_LATENCY_H
#define ROBO_INPUT_LATENCY_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The five stamps                                                     */
/* ------------------------------------------------------------------ */

/**
 * Where a stamp was taken.
 *
 * The order is the pipeline's order, and it is what makes "later" checkable: a mark must not precede a mark
 * already taken. Stable and positional -- a diagnostics line and a saved trace carry these numbers.
 */
typedef enum InputLatencyStage {
    /** The device's own report: when it says the input happened. Always set by @ref InputLatencyBegin. */
    RoboInputLatencyHardware = 0,
    /** The event reached the queue. Always set by @ref InputLatencyBegin. */
    RoboInputLatencyQueue = 1,
    /** Routing began: the dispatcher took the event. */
    RoboInputLatencyDispatch = 2,
    /** The target's handler returned: the work is done and a frame may be composed. */
    RoboInputLatencyHandler = 3,
    /** The frame carrying the result was submitted for presentation. The last stage. */
    RoboInputLatencyPresent = 4
} InputLatencyStage;

/** Number of defined stages. Asserted by `input.latency.vocabulary`. */
#define RoboInputLatencyStageCount 5u

/** Non-zero when @p stage is defined. */
int InputLatencyStageIsKnown(InputLatencyStage stage);

/** Human-readable stage name (`"hardware"`, `"dispatch"`); never NULL; `"unknown"` when undefined. */
const char *InputLatencyStageName(InputLatencyStage stage);

/** Non-zero when @p stage is a mark a caller supplies, rather than one `Begin` sets. */
int InputLatencyStageIsMarked(InputLatencyStage stage);

/* ------------------------------------------------------------------ */
/* The trace                                                           */
/* ------------------------------------------------------------------ */

/**
 * Records one trace keeps: one frame's worth.
 *
 * A record stays **addressable** after it completes -- its five stamps are readable until its slot is reused
 * -- so "what did that event cost?" is answerable one event later. `Begin` takes a never-used slot first and
 * the oldest **finished** record after that; a table whose records are all still live refuses with
 * `RoboBusy`, because that is a caller timing more events at once than the trace holds and discarding one
 * would throw away a measurement nobody has read yet.
 */
#define RoboInputLatencyRecords 16u

/** One event's five stamps and the identity they belong to. */
typedef struct InputLatencyRecord {
    /** Non-zero while this record is unfinished (`Present` has not arrived). */
    int active;
    /** Non-zero once `Present` has arrived. The slot may be reused; the record stays readable until it is. */
    int complete;
    /** The handle `Begin` handed out; never 0. Stable while the record lives. */
    uint32_t sequence;
    /** What the event was, so a reader sees which kinds are slow. */
    InputEventType type;
    /** Which device reported it. */
    InputDeviceId device;
    /** One stamp per stage, in stage order. Meaningless where @ref setMask has no bit. */
    uint64_t stamps[RoboInputLatencyStageCount];
    /** Bit `1 << stage` for every stage that has a stamp. */
    uint32_t setMask;
} InputLatencyRecord;

/** The four differences a finished -- or partial -- record yields. All 0 where their stamps are missing. */
typedef struct InputLatencyDurations {
    /** Input to dispatch: `Dispatch - Hardware`. The delay before anything could react. */
    uint64_t dispatchMs;
    /** Dispatch to the handler's return: the work the event caused. */
    uint64_t handlerMs;
    /** Input to the frame that carries the result: `Handler - Hardware`. Equal to `dispatchMs +
     *  handlerMs` by construction, and named separately because it is the number a user feels. The brief's
     *  *input-to-frame*. */
    uint64_t frameMs;
    /** Input to present: `Present - Hardware`. */
    uint64_t presentMs;
    /** Non-zero when every stage is in, so these four are final. */
    int complete;
} InputLatencyDurations;

/**
 * The records and their aggregates.
 *
 * Held by value, like every other table in this module: nothing to destroy, and two traces in one process
 * cannot interfere with each other.
 */
typedef struct InputLatencyTrace {
    /** The live records, reused in order once one completes. */
    InputLatencyRecord records[RoboInputLatencyRecords];
    /** The next handle `Begin` will hand out; starts at 1 and never 0. */
    uint32_t nextSequence;
    /** Records live right now. */
    uint32_t live;
    /** Records begun. Diagnostics. */
    uint64_t begunTotal;
    /** Marks accepted. Diagnostics. */
    uint64_t markedTotal;
    /** Records that reached `Present`. Diagnostics. */
    uint64_t completedTotal;
    /** Calls refused for a bad argument or a full table. Diagnostics. */
    uint64_t refusedTotal;
    /** Stamps refused for arriving before a stage already marked. Diagnostics, and the one that matters. */
    uint64_t outOfOrderTotal;

    /** Sum, maximum and last of each measured difference, over completed records. */
    uint64_t dispatchSumMs;
    uint64_t dispatchMaxMs;
    uint64_t dispatchLastMs;
    uint64_t handlerSumMs;
    uint64_t handlerMaxMs;
    uint64_t handlerLastMs;
    uint64_t frameSumMs;
    uint64_t frameMaxMs;
    uint64_t frameLastMs;
    uint64_t presentSumMs;
    uint64_t presentMaxMs;
    uint64_t presentLastMs;
} InputLatencyTrace;

/** Clear @p trace: no live records, every counter and aggregate 0. Safe on NULL. */
void InputLatencyTraceInit(InputLatencyTrace *trace);

/** Live records; 0 for NULL. */
uint32_t InputLatencyLive(const InputLatencyTrace *trace);

/**
 * Begin a record for @p event, taking its first two stamps.
 *
 * @param hardwareMs when the device reported it; @p queuedMs when it reached the queue.
 * @param outSequence receives the handle the later marks and the measurement use; required.
 * @return `RoboOk`; `RoboInvalidArgument` for NULL arguments, an invalid @p event, or a @p queuedMs earlier
 *         than @p hardwareMs (which is counted as out of order); `RoboBusy` when every record is still live,
 *         which is a refusal rather than a discard.
 */
Result InputLatencyBegin(InputLatencyTrace *trace,
                         const InputEvent *event,
                         uint64_t hardwareMs,
                         uint64_t queuedMs,
                         uint32_t *outSequence,
                         InputDiag *diag);

/**
 * Take one more stamp for @p sequence.
 *
 * @return `RoboOk`; `RoboNotFound` for a sequence no record owns; `RoboAlreadyExists` for a stage that
 *         already has a stamp; `RoboInvalidArgument` for `0` (not a handle -- `Begin` hands them out from
 *         one), an unknown stage, a stage `Begin` owns, or a @p stampMs that precedes the latest stage
 *         already marked (counted as out of order).
 *
 * Marking `Present` completes the record: it stops being live, and its four differences join the aggregates.
 */
Result InputLatencyMark(InputLatencyTrace *trace,
                        uint32_t sequence,
                        InputLatencyStage stage,
                        uint64_t stampMs,
                        InputDiag *diag);

/**
 * Read @p sequence's four differences.
 *
 * Answers for an **unfinished** record too: the stages that have not arrived contribute 0 and @ref
 * InputLatencyDurations::complete says so, which is what a partial read during a long handler needs. A
 * completed record answers for as long as its slot has not been reused. Refused only for NULL arguments or a
 * sequence no addressable record owns.
 */
Result InputLatencyMeasure(const InputLatencyTrace *trace,
                           uint32_t sequence,
                           InputLatencyDurations *out,
                           InputDiag *diag);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** Room `InputLatencyWrite` needs: a summary line, three aggregate lines, and one line per record. */
#define RoboInputLatencyDumpMax (320u + (RoboInputLatencyRecords * 192u))

/**
 * Write @p trace as text: a summary line, the aggregates, then one line per **addressable** record -- live or
 * finished -- with whichever of the five stamps it has.
 *
 * Complete or absent: `?` and the needed length when the buffer is too small, and the buffer left empty.
 */
Result InputLatencyWrite(const InputLatencyTrace *trace,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_LATENCY_H */
