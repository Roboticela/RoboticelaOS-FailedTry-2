/*
 * Gestures (Phase 13.23/13.24).
 *
 * ## What a gesture is here
 *
 * A gesture recognizer does **not** consume events. It consumes a **sample stream** -- one normalized sample
 * per pointer/touch update, plus the taps a caller's clock injects -- and it answers three things: whether
 * this sequence is its business (`Accept`), whether it is *not* (`Reject`), or whether it cannot tell yet
 * (`Defer`). That tri-state is the whole of 13.24: `Tap` and `Drag` both begin with a finger landing and a
 * still position, and neither can know which one it is until the finger either lifts or moves.
 *
 * ## The arena
 *
 * `InputGestureArena` holds a small, ordered set of recognizers and picks **one winner per sequence**. Until
 * a winner exists every recognizer is asked in registration order and the first `Accept` wins; a `Reject`
 * removes a recognizer from this sequence; a `Defer` leaves it in. Once a winner exists the arena feeds *only
 * the winner*, which is what stops a `Drag` from being reported as a `Pan` halfway through. The winner is
 * released when it reports an `End` or a `Cancel`, or when the caller resets.
 *
 * Registration order is therefore the priority order, and it is a *caller's* decision -- a shell that wants
 * long-press to beat drag registers it first. This layer never reorders the list, because a priority rule
 * invented here would be a rule the shell could not change.
 *
 * ## No second pointer stream
 *
 * A sample is built from what 13.14--13.21 already produced: `InputGestureSampleFromPointer` from a pointer
 * event's payload, `InputGestureSampleFromTouches` from the contact table (which is where the centroid, the
 * spread and the angle a pinch or a rotate needs come from -- computed once, here, rather than by each
 * recognizer). Gestures therefore sit *downstream* of normalization and never bypass it.
 *
 * ## What this build ships
 *
 * One recognizer, `InputGestureHeuristics`, implementing the seven types 13.23 names -- tap, double tap,
 * long press, drag, pan, pinch, rotate -- from the thresholds in `InputGestureHeuristicsConfig`. It is a
 * *starting point* rather than a library: `Pinch` and `Rotate` are driven by the caller's spread and angle
 * and nothing else, and there is no velocity, no inertia, no fling and no platform-specific feel. A caller
 * that wants those registers its own recognizer beside it, which is what the arena is for.
 *
 * See `Documentation/Runtime/Input/Gestures.md`.
 */

#ifndef ROBO_INPUT_GESTURE_H
#define ROBO_INPUT_GESTURE_H

#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputTouch.h"
#include "RoboInput/RoboInputTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Recognizers one arena holds. A shell's gesture layer, with room for a caller's own. */
#define RoboInputMaxRecognizers 8u

/** Events one feed may produce: a sequence can end a tap and begin a drag in one step. */
#define RoboInputMaxGestureEvents 4u

/** Gestures this build names. */
typedef enum InputGestureType {
    /** Nothing: a cleared event's value. */
    RoboInputGestureNone = 0,
    /** A finger landed and lifted without travelling far, quickly. */
    RoboInputGestureTap = 1,
    /** Two taps in the same place, close in time. */
    RoboInputGestureDoubleTap = 2,
    /** A finger stayed down without travelling for longer than the long-press delay. */
    RoboInputGestureLongPress = 3,
    /** One finger travelled past the drag threshold. */
    RoboInputGestureDrag = 4,
    /** Two or more fingers moved together past the pan threshold. */
    RoboInputGesturePan = 5,
    /** Two or more fingers changed their distance past the pinch ratio. */
    RoboInputGesturePinch = 6,
    /** Two or more fingers changed their angle past the rotate threshold. */
    RoboInputGestureRotate = 7
} InputGestureType;

/** Number of defined types. Asserted positionally by `input.gesture.*`. */
#define RoboInputGestureTypeCount 8u

/** Non-zero if @p type is a defined type. */
int InputGestureTypeIsKnown(InputGestureType type);

/** Human-readable type name; never NULL; `"unknown"` when undefined. */
const char *InputGestureTypeName(InputGestureType type);

/** Non-zero when @p type needs more than one contact (pan, pinch, rotate). */
int InputGestureTypeIsMultiTouch(InputGestureType type);

/** Where in its life a gesture is. */
typedef enum InputGesturePhase {
    /** The gesture has just been recognized. Everything before this was ambiguous. */
    RoboInputGesturePhaseBegin = 0,
    /** It continues. */
    RoboInputGesturePhaseUpdate = 1,
    /** It finished normally. */
    RoboInputGesturePhaseEnd = 2,
    /** It was cancelled: a device went away, a window deactivated, a contact was refused. */
    RoboInputGesturePhaseCancel = 3
} InputGesturePhase;

/** Number of defined phases. Asserted positionally by `input.gesture.*`. */
#define RoboInputGesturePhaseCount 4u

/** Non-zero if @p phase is a defined phase. */
int InputGesturePhaseIsKnown(InputGesturePhase phase);

/** Human-readable phase name; never NULL; `"unknown"` when undefined. */
const char *InputGesturePhaseName(InputGesturePhase phase);

/** Non-zero when @p phase finishes the sequence, so the arena releases its winner. */
int InputGesturePhaseIsTerminal(InputGesturePhase phase);

/** What a recognizer says about a sequence. */
typedef enum InputGestureVerdict {
    /** Not mine. The recognizer is removed from this sequence. */
    RoboInputGestureVerdictReject = 0,
    /** Mine. The arena makes it the winner and feeds only it from here on. */
    RoboInputGestureVerdictAccept = 1,
    /** I cannot tell yet. The recognizer stays in the running. */
    RoboInputGestureVerdictDefer = 2
} InputGestureVerdict;

/** Number of defined verdicts. Asserted positionally by `input.gesture.*`. */
#define RoboInputGestureVerdictCount 3u

/** Non-zero if @p verdict is a defined verdict. */
int InputGestureVerdictIsKnown(InputGestureVerdict verdict);

/** Human-readable verdict name; never NULL; `"unknown"` when undefined. */
const char *InputGestureVerdictName(InputGestureVerdict verdict);

/* ============================================================================================== *
 * The sample
 * ============================================================================================== */

/**
 * One normalized update, as a recognizer sees it.
 *
 * `tick` is the one field that is not a pointer update: a caller with a clock injects a tick after the
 * long-press delay so that a *stationary* finger can be recognized. A gesture engine that read a clock itself
 * would be untestable, which is the same reason key repeat takes its time as an argument (13.2).
 */
typedef struct InputGestureSample {
    /** The pointer this update is about. */
    InputPointerId pointer;
    /** The event type that produced it, or `NONE` for a tick. */
    InputEventType type;
    /** Where the pointer is, when the sample has a position. */
    GfxPoint position;
    /** Non-zero when @ref position is meaningful. */
    int hasPosition;
    /** Movement since the previous sample, when the platform reported it or it was derived. */
    float deltaX;
    float deltaY;
    /** The plane @ref position is in. */
    InputSpace space;
    /** The device that reported it. */
    InputDeviceId device;
    /** When, on the runtime's monotonic clock. */
    uint64_t timestampMs;
    /** Live contacts at this moment: 1 for a mouse, N for a digitizer. */
    uint32_t contactCount;
    /** Non-zero when this sample is a clock tick rather than a pointer update. */
    int tick;
    /** The midpoint of the live contacts; equal to @ref position for one pointer. */
    GfxPoint centroid;
    /** The mean distance of the contacts from @ref centroid, in @ref space. */
    float spread;
    /**
     * The direction from @ref centroid to the **first live contact**, as a unit vector. Zero when there is
     * nothing to measure one from (fewer than two contacts, or none of them away from the centroid).
     *
     * A unit vector rather than an angle, and deliberately: the guest target has no `math.h`, and the only
     * question a rotate asks is *how much has this line turned*, which is the **cross product of two unit
     * vectors**. An angle would need an arc tangent to build and another to compare, and the cross product is
     * the sine of the rotation -- which is the quantity the threshold is really about. So the recognizer
     * stays a multiply and a compare, and no transcendental function enters the freestanding runtime.
     */
    float axisX;
    float axisY;
    /** Non-zero when a cancel -- not an end -- produced this sample. */
    int cancelled;
} InputGestureSample;

/** Clear @p sample. Safe on NULL. */
void InputGestureSampleInit(InputGestureSample *sample);

/** Build a sample from a pointer payload. @p timestampMs is the caller's clock. */
Result InputGestureSampleFromPointer(InputPointerData *data,
                                     InputEventType type,
                                     InputSpace space,
                                     InputDeviceId device,
                                     uint64_t timestampMs,
                                     InputGestureSample *out,
                                     InputDiag *diag);

/**
 * Build a sample from the live contacts: the position of the primary contact, the centroid of them all, and
 * the spread and angle the multi-touch gestures need.
 *
 * This is the one place that geometry is computed, so a pinch and a rotate cannot disagree about what the
 * fingers were doing. With no live contact the sample has no position, which is a legitimate sample -- a
 * sequence that ended.
 */
Result InputGestureSampleFromTouches(const InputTouchState *touches,
                                     uint64_t timestampMs,
                                     InputGestureSample *out,
                                     InputDiag *diag);

/* ============================================================================================== *
 * The event
 * ============================================================================================== */

/** One recognized gesture step. */
typedef struct InputGestureEvent {
    /** What was recognized. */
    InputGestureType type;
    /** Where in its life it is. */
    InputGesturePhase phase;
    /** The pointer or contact the gesture is about. */
    InputPointerId pointer;
    /** Where it happened: the position of a tap, the current point of a drag. */
    GfxPoint position;
    /** Total movement since the gesture began, in @ref space. */
    float totalX;
    float totalY;
    /** Movement since the previous step. */
    float deltaX;
    float deltaY;
    /** How many contacts were involved. */
    uint32_t contactCount;
    /** Scale since the gesture began, 1.0 when nothing measured one. */
    float scale;
    /** Rotation since the gesture began, in radians; 0 when nothing measured one. */
    float rotation;
    /** How long the gesture has been going, in milliseconds. */
    uint32_t durationMs;
    /** The plane @ref position is in. */
    InputSpace space;
} InputGestureEvent;

/** Clear @p event to "a tap at the origin, 0 ms ago". Safe on NULL. */
void InputGestureEventInit(InputGestureEvent *event);

/** Non-zero when @p event is a step this build may hand to a caller. */
int InputGestureEventIsValid(const InputGestureEvent *event);

/** One complete line for @p event, or @ref RoboNotSupported when it does not fit. */
Result InputGestureEventWrite(const InputGestureEvent *event,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/* ============================================================================================== *
 * The recognizer and the arena (13.24)
 * ============================================================================================== */

/**
 * A recognizer.
 *
 * `offer` is called once per sample. It returns a verdict and may also emit events -- a recognizer that
 * accepts *and* has something to say (a tap that ends where it began) does both in one call, because the
 * alternative is a second pass over the same sample and a second chance to disagree with itself.
 *
 * `reset` is called when the arena drops the recognizer from a sequence, so it cannot carry state from one
 * sequence into the next. It may be NULL, and a recognizer with state that leaves it NULL is a bug that shows
 * up as a spurious second tap.
 */
typedef struct InputGestureRecognizer {
    /** What this recognizer produces. Informational: the arena never guesses from the type. */
    InputGestureType type;
    /** Whatever the recognizer needs; this layer never reads it. */
    void *userdata;
    /** Offer one sample. */
    InputGestureVerdict (*offer)(void *userdata,
                                 const InputGestureSample *sample,
                                 InputGestureEvent *out,
                                 uint32_t capacity,
                                 uint32_t *outCount);
    /** Drop any sequence state. May be NULL. */
    void (*reset)(void *userdata);
    /** How many times this recognizer was asked. Read after a run. */
    uint32_t offered;
    /** How many times it accepted a sequence. */
    uint32_t accepted;
    /** How many times it refused one. */
    uint32_t rejected;
} InputGestureRecognizer;

/** The order the arena asks recognizers in, and which one owns the current sequence. */
typedef struct InputGestureArena {
    /** The recognizers, in the caller's priority order. */
    InputGestureRecognizer entries[RoboInputMaxRecognizers];
    /** Entries in the list. */
    uint32_t count;
    /** Index of the winning recognizer, or -1 while a sequence is unresolved. */
    int winner;
    /** Sequences fed. */
    uint32_t sequences;
    /** Sequences that ended **unrecognized**: every recognizer refused, or all deferred forever. */
    uint32_t unresolved;
    /** Samples fed. */
    uint32_t samples;
} InputGestureArena;

/** Clear @p arena to "nobody registered, no sequence". Safe on NULL. */
void InputGestureArenaInit(InputGestureArena *arena);

/** Register @p recognizer at the end of the priority order. Returns its index, or -1 when the list is full. */
int InputGestureArenaAdd(InputGestureArena *arena, const InputGestureRecognizer *recognizer);

/**
 * Feed one sample.
 *
 * The walk: if a winner exists, only it is asked. Otherwise every entry still in the running is asked in
 * order -- the first `Accept` becomes the winner and the walk stops; a `Reject` drops that entry from this
 * sequence; a `Defer` leaves it in. When every entry has refused, the sequence is counted into
 * @ref unresolved and the arena is ready for a new one.
 *
 * @p out receives at most @p capacity events and @p outCount how many were produced (which may exceed
 * @p capacity, so a caller learns it dropped some -- and drops are *counted*, never silent). Returns
 * @ref RoboOk for a completed walk, @ref RoboInvalidArgument for NULL arguments.
 */
Result InputGestureArenaFeed(InputGestureArena *arena,
                             const InputGestureSample *sample,
                             InputGestureEvent *out,
                             uint32_t capacity,
                             uint32_t *outCount,
                             InputDiag *diag);

/** Release the winner and reset every recognizer: the door a device disconnect or a window change needs. */
void InputGestureArenaReset(InputGestureArena *arena);

/** The winning recognizer's index, or -1. */
int InputGestureArenaWinner(const InputGestureArena *arena);

/** One complete line for @p arena, or @ref RoboNotSupported when it does not fit. */
Result InputGestureArenaWrite(const InputGestureArena *arena,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/* ============================================================================================== *
 * The recognizer this build ships (13.23)
 * ============================================================================================== */

/** What counts as each gesture. Every distance is in the sample's own plane. */
typedef struct InputGestureHeuristicsConfig {
    /** How far a tap may travel and still be a tap. */
    float tapSlop;
    /** How long a tap may last. */
    uint32_t tapMaxMs;
    /** How long a stationary press must last to become a long press. */
    uint32_t longPressMs;
    /** How far one finger must travel to be a drag. */
    float dragThreshold;
    /** How far the centroid of two or more fingers must travel to be a pan. */
    float panThreshold;
    /** How much two fingers' distance must change, as a ratio, to be a pinch. */
    float pinchRatio;
    /**
     * How much the finger line must turn to be a rotate, as the **sine** of the angle: the cross product of
     * the sequence's starting axis and the current one. `0.1736` is ten degrees.
     */
    float rotateSin;
    /** Whether a double tap is recognized at all. */
    int doubleTap;
} InputGestureHeuristicsConfig;

/** The configuration this build ships: 8 units of slop, 300 ms, 500 ms, and the classic thresholds. */
InputGestureHeuristicsConfig InputGestureHeuristicsConfigDefault(void);

/** One heuristic recognizer's state. */
typedef struct InputGestureHeuristics {
    /** The thresholds. */
    InputGestureHeuristicsConfig config;
    /** Non-zero when a sequence is in progress. */
    int armed;
    /** Where it began. */
    GfxPoint start;
    /** When it began. */
    uint64_t startTime;
    /** Which pointer began it. */
    InputPointerId pointer;
    /** Contacts when it began. */
    uint32_t contacts;
    /** Spread and axis when it began: what a pinch's ratio and a rotate's cross product are measured from. */
    float spread;
    float axisX;
    float axisY;
    /** Whether a recognized gesture has already been reported for this sequence. */
    int reported;
    /** The type reported, so an update continues the same gesture. */
    InputGestureType type;
    /** Where the previous tap was, and when, for the double tap. */
    GfxPoint lastTapPosition;
    uint64_t lastTapTime;
    /** A tap waiting to see whether a second one follows. */
    int pendingTap;
    /** How many samples this recognizer has been offered. */
    uint32_t ticks;
} InputGestureHeuristics;

/** Set @p state up with @p config (NULL for the default). */
void InputGestureHeuristicsInit(InputGestureHeuristics *state,
                                const InputGestureHeuristicsConfig *config);

/**
 * The `offer` this recognizer implements, ready to put in an `InputGestureRecognizer`.
 *
 * @p userdata is an `InputGestureHeuristics *`.
 */
InputGestureVerdict InputGestureHeuristicsOffer(void *userdata,
                                                const InputGestureSample *sample,
                                                InputGestureEvent *out,
                                                uint32_t capacity,
                                                uint32_t *outCount);

/** The `reset` this recognizer implements. @p userdata is an `InputGestureHeuristics *`. */
void InputGestureHeuristicsReset(void *userdata);

/** One complete line for @p state's summary, or @ref RoboNotSupported when it does not fit. */
Result InputGestureHeuristicsWrite(const InputGestureHeuristics *state,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_GESTURE_H */
