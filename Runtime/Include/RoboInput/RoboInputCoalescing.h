/*
 * Event coalescing (Phase 13.47).
 *
 * A mouse that reports at 1 kHz produces a thousand `POINTER_MOVE` events a second and a frame draws sixty
 * times. Merging the moves that happened *between two frames* into one is what keeps the queue and the
 * dispatch loop proportional to the frame rate rather than to the hardware -- and it is the single place in
 * this module where two events become one, so the rule that says **what may be merged** is worth more than
 * the mechanism that merges it.
 *
 * ## What may never be merged, and why the list is a complement
 *
 * A press, a release, a cancel, a key, a repeat, a text event, a composition change, a focus change, a
 * window activation, a hotplug and a gamepad button all **mean something on their own**. Merging two of them
 * would either drop one (a click that never happened) or invent one (a double click that did not). Moves
 * carry a *position*, and only the last position before a frame is ever drawn, so a move is the only kind of
 * event whose earlier instances are genuinely redundant.
 *
 * `InputEventTypeMayCoalesce` is therefore the *only* door that answers the question, and it answers `1` for
 * exactly three types: `POINTER_MOVE`, `TOUCH_MOVE` and `PEN_MOVE`. Everything else -- every other one of the
 * thirty-three, including the ones a later phase appends -- is a **barrier**, which is the honest default:
 * an unknown-to-the-rule event is preserved, never merged.
 *
 * ## The three answers a push has
 *
 * `InputCoalescerPush` does not dispatch anything. It says what the *caller* must do:
 *
 *   `EMIT`          this event goes to the queue now;
 *   `HOLD`          this event has been taken and must not be dispatched; more may follow;
 *   `FLUSH_FIRST`   whatever is held must reach the queue **before** this event does.
 *
 * `FLUSH_FIRST` is the whole ordering rule. A press that arrived after four held moves must not overtake
 * them, so the third answer is the only way a barrier is ever emitted: the caller drains, then dispatches.
 * A coalescer that instead dispatched the barrier immediately would hand a consumer a press at a position
 * the consumer had not been told the pointer moved to -- which is precisely the bug coalescing exists to
 * avoid intoducing.
 *
 * Every `HOLD` is paired with a `FLUSH_FIRST` or a caller's end-of-frame flush, so **nothing is ever
 * dropped**: the table being full is a `FLUSH_FIRST`, not a refusal.
 *
 * ## What a merged move says
 *
 * The merged record is the **newest** event, whole: its position, its button mask, its flags, its wheel and
 * its plane are the newest answers, because those are the ones a frame draws. Two fields differ, and each
 * says exactly what it is:
 *
 *   `deltaX`/`deltaY`   the **sum** of every held event's deltas: the movement over the whole merged window,
 *                       0 where the platform reported none (the field's own meaning);
 *   `previous`          left as the newest event reported it, so `position - previous` is the last step and
 *                       not a fiction about the first one.
 *
 * A merge is per **(device, pointer, type)**: two fingers on one screen are two slots and never one, and a
 * mouse and a stylus are two devices. That is also what keeps the merge cheap -- the key is already in the
 * event.
 *
 * ## The flush, and the one case that is not a drop
 *
 * `InputCoalescerFlush` pushes each held event, **in slot order**, into a queue the caller owns. Slot order
 * is table order, not arrival order, which is the deterministic answer to "which of two pointers' pending
 * moves goes first" -- a question with no right answer, answered the same way twice rather than by accident.
 *
 * A queue with no room stops the flush. What was pushed stays pushed, what was not **stays held**, and the
 * answer is the queue's own `RoboBusy`: a coalescer that discarded a held move because the queue was full
 * would turn a slow frame into a lost pointer. The caller flushes again after draining.
 */
#ifndef ROBO_INPUT_COALESCING_H
#define ROBO_INPUT_COALESCING_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDebug.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputQueue.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The rule                                                            */
/* ------------------------------------------------------------------ */

/**
 * Non-zero when a `HOLD` is allowed for @p type.
 *
 * `1` for `POINTER_MOVE`, `TOUCH_MOVE` and `PEN_MOVE`, and `0` for every other type -- including an
 * undefined one, which is the safe answer rather than the convenient one.
 */
int InputEventTypeMayCoalesce(InputEventType type);

/** Number of event types this build merges. Asserted by `input.coalescing.rule`. */
#define RoboInputCoalesceTypeCount 3u

/** The three coalescible types, in the order `input.coalescing.rule` asserts them. */
extern const InputEventType InputCoalesceTypes[RoboInputCoalesceTypeCount];

/** What a caller must do with an event it just offered. */
typedef enum InputCoalesceAction {
    /** Dispatch @p event now. */
    RoboInputCoalesceEmit = 0,
    /** @p event was taken for merging; do not dispatch it. */
    RoboInputCoalesceHold = 1,
    /** Drain the coalescer **before** dispatching @p event. */
    RoboInputCoalesceFlushFirst = 2
} InputCoalesceAction;

/** Number of defined actions. Asserted by `input.coalescing.rule`. */
#define RoboInputCoalesceActionCount 3u

/** Non-zero when @p action is a defined action. */
int InputCoalesceActionIsKnown(InputCoalesceAction action);

/** Human-readable action name (`"flush-first"`); never NULL; `"unknown"` when undefined. */
const char *InputCoalesceActionName(InputCoalesceAction action);

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

/** Pending moves one coalescer holds. Past this, a push is a `FLUSH_FIRST` rather than a refusal. */
#define RoboInputCoalesceSlots 8u

/** One pending move: the key it merged on, the newest event, and what it absorbed. */
typedef struct InputCoalesceSlot {
    /** Non-zero when this slot holds an event. */
    int active;
    /** The device the held event came from. */
    InputDeviceId device;
    /** The pointer or touch id it belongs to. */
    InputPointerId pointer;
    /** The type that may be merged (`POINTER_MOVE`, `TOUCH_MOVE`, `PEN_MOVE`). */
    InputEventType type;
    /** The **newest** held event, whole, with the accumulated deltas folded in. */
    InputEvent event;
    /** How many events were folded into it, the first one included. Never 0 for an active slot. */
    uint32_t merged;
    /** The first held event's timestamp, in milliseconds (`0` when the platform did not say). */
    uint64_t firstMs;
    /** The newest held event's timestamp, in milliseconds. */
    uint64_t lastMs;
} InputCoalesceSlot;

/**
 * The pending-move table and its counters.
 *
 * Held **by value**: nothing to destroy, no allocator, and no state that outlives the caller's scope. Like
 * every other table in this module it is a record a caller embeds, which is what makes "two coalescers in
 * one process cannot interfere" true rather than aspirational.
 */
typedef struct InputCoalescer {
    /** The pending slots, in the order a flush emits them. */
    InputCoalesceSlot slots[RoboInputCoalesceSlots];
    /** Slots live right now. `InputCoalescerHeld` reads it. */
    uint32_t live;
    /** Pushes that returned `HOLD`, merged or fresh. Diagnostics. */
    uint64_t heldTotal;
    /** Pushes that returned `FLUSH_FIRST`. Diagnostics: the barriers, and the full tables. */
    uint64_t flushTotal;
    /** Events **absorbed** into another: `heldTotal - fresh` over the table's life. */
    uint64_t mergedTotal;
    /** Events handed back to a queue. Diagnostics. */
    uint64_t emittedTotal;
    /** Pushes refused for a bad argument. Diagnostics -- and the only loss this layer can cause. */
    uint64_t refusedTotal;
} InputCoalescer;

/** Clear @p coalescer: no pending slots, every counter 0. Safe on NULL. */
void InputCoalescerInit(InputCoalescer *coalescer);

/** Pending slots; 0 for NULL. */
uint32_t InputCoalescerHeld(const InputCoalescer *coalescer);

/** Non-zero when @p coalescer holds nothing. A NULL coalescer holds nothing. */
int InputCoalescerIsEmpty(const InputCoalescer *coalescer);

/* ------------------------------------------------------------------ */
/* The two operations                                                  */
/* ------------------------------------------------------------------ */

/**
 * Offer @p event, and say what the caller must do with it.
 *
 * @param outAction receives the answer; required.
 * @return `RoboOk` for every valid event, `EMIT`, `HOLD` and `FLUSH_FIRST` included: the action is the
 *         answer, not a status. `RoboInvalidArgument` for NULL, for an event
 *         `InputEventIsValid` refuses, or for a coalescible type whose payload is not the pointer kind
 *         (a record and its type disagreeing is a defect upstream, and taking it would spread it).
 *
 * A barrier is `EMIT` when nothing is held and `FLUSH_FIRST` when something is, so the answer differs only
 * in whether a drain is needed first. A coalescible event whose key is already held is absorbed and answers
 * `HOLD`; one whose key is new takes a free slot and answers `HOLD`; one that has no slot left answers
 * `FLUSH_FIRST` and is **not** taken -- the caller drains and offers it again.
 *
 * One more `FLUSH_FIRST`: when folding a new delta into a held one would need a value a `deltaX` cannot
 * hold, the event is handed back rather than wrapped. Two movements stay two events, and a movement is
 * never reported backwards.
 */
Result InputCoalescerPush(InputCoalescer *coalescer,
                          const InputEvent *event,
                          InputCoalesceAction *outAction,
                          InputDiag *diag);

/**
 * Push every held event into @p queue, in slot order, emptying each slot as it goes.
 *
 * @param outEmitted receives how many reached the queue; optional.
 * @return `RoboOk` when everything held was emitted (including when nothing was held);
 *         `RoboBusy` when @p queue filled first, in which case **the rest stay held** and a later flush after
 *         draining finishes the job.
 *
 * Refused for NULL @p coalescer or @p queue.
 */
Result InputCoalescerFlush(InputCoalescer *coalescer,
                           InputQueue *queue,
                           uint32_t *outEmitted,
                           InputDiag *diag);

/**
 * Drop every held event, counting each into @ref InputCoalescer::refusedTotal. For a reset (13.78).
 *
 * A deliberate loss is still a loss: the counter is the one that says "events this layer caused to go
 * nowhere", and a total that hid an intentional cancellation would be the total nobody could explain.
 */
void InputCoalescerClear(InputCoalescer *coalescer);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/**
 * Room `InputCoalescerWrite` needs: a summary line, then one line per slot -- the held event through
 * `InputEventWrite` plus what the slot absorbed.
 *
 * Composed from the event dump's own bound rather than guessed, so a field added to an event line cannot
 * silently make this dump unobtainable (`input.coalescing.dump` writes a full table at exactly this size).
 */
#define RoboInputCoalescerDumpMax \
    (256u + (RoboInputCoalesceSlots * (RoboInputDumpLineMax + 96u)))

/**
 * Write @p coalescer as text: a summary line and one line per **live** slot.
 *
 *     coalescer slots=8 live=2 held=5 flush=3 merged=3 emitted=5 refused=0
 *     slot 0 device=2 pointer=0 type=PointerMove merged=3 span=100..116 PointerMove source=mouse ...
 *
 * Complete or absent: `?` and the length a complete line needs when the buffer is too small, and the buffer
 * is left empty rather than partly written.
 */
Result InputCoalescerWrite(const InputCoalescer *coalescer,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_COALESCING_H */
