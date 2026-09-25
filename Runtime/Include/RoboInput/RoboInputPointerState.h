/*
 * Pointer state: which pointers exist, where they are, who has them, and what they are over.
 *
 * `RoboInputPointer.h` owns the *payload* of one positioning event. This header owns the **state that
 * outlives an event**, which is the half a UI actually asks questions of:
 *
 *   the pointer table   which pointers a device has reported, where each one is, and which buttons are
 *                       down -- so a control can ask "is the pointer still down over me?" without having
 *                       watched every event since;
 *   click counting      how many clicks this press completes, **derived here** when the platform does not
 *                       say (13.16). A platform that reports `clickCount` directly keeps it: this layer is
 *                       the fallback, not a second opinion;
 *   pointer capture     which target, if any, owns each pointer (13.17), so a drag that leaves its bounds
 *                       still delivers;
 *   hover               which target each pointer is over, and the enter/leave pair a change produces
 *                       (13.18), with the one rule that matters: **a target is never entered twice**.
 *
 * ## What this module deliberately is not
 *
 * It routes nothing and it hit-tests nothing. Whoever owns the tree resolves the target under the point
 * and *tells* this layer (`InputHoverSet`); the layer's job is remembering it and stating the transition.
 * That keeps the derived state testable without a window, a compositor or a render target, and it is what
 * makes hover correct across a device disconnect rather than merely usually correct.
 *
 * Positions are stored in the **space the caller applied them in**. This module never converts: a
 * conversion needs a route, and a route is the routing layer's (13.31). A position whose space changed
 * without a conversion is a caller's bug, and the state records the space so it can be seen.
 *
 * See `Documentation/Runtime/Input/PointerState.md`.
 */

#ifndef ROBO_INPUT_POINTER_STATE_H
#define ROBO_INPUT_POINTER_STATE_H

#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDevice.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many live pointers one state table holds. A mouse plus several fingers, with room to spare. */
#define RoboInputMaxPointers 16u

/** How many pointers may hold a capture at once. One per slot, so the table is the same size. */
#define RoboInputMaxCaptures RoboInputMaxPointers

/* ============================================================================================== *
 * Wheel normalization (13.19)
 * ============================================================================================== */

/**
 * Convert a wheel's deltas into **pixels**, given the two distances only the caller knows.
 *
 * A wheel arrives in one of RoboWindow's two units and a scroller consumes pixels, so the conversion needs
 * a line's height and a page's height in the document being scrolled. They are arguments rather than
 * constants on purpose: this build has no opinion about how tall a line is, and a hard-coded 40 here
 * would be a number some other module already owns.
 *
 * The arithmetic is exact integer arithmetic in every unit -- pixel deltas pass through, line and page
 * deltas multiply -- so a high-resolution wheel's fractional notch is preserved by the *caller* encoding
 * it in pixels, and no rounding is invented here. A `PAGE` delta with no page height converts as zero,
 * which is the honest answer to "how far is a page in a document with no pages".
 */
typedef struct InputWheelPixels {
    /** Horizontal distance in pixels. */
    int32_t deltaX;
    /** Vertical distance in pixels. */
    int32_t deltaY;
    /** The unit the input was in, kept so a reader can tell a pass-through from a conversion. */
    MouseScrollUnit unit;
} InputWheelPixels;

/** Convert @p wheel to pixels. @p lineHeightPx / @p pageHeightPx below 1 are treated as 0. */
InputWheelPixels InputWheelToPixels(const InputWheelData *wheel,
                                    int32_t lineHeightPx,
                                    int32_t pageHeightPx);

/* ============================================================================================== *
 * The live pointer table (13.14/13.15)
 * ============================================================================================== */

/** One pointer this build is tracking. */
typedef struct InputPointerTrack {
    /** Which pointer. `RoboInputPointerIdPrimary` for a mouse. */
    InputPointerId pointer;
    /** What kind of pointer it is. */
    InputDeviceType type;
    /** The device that reported it, so a disconnect can be answered. */
    InputDeviceId device;
    /** Mask of buttons now down: bit `n - 1` for button `n`. */
    uint32_t buttons;
    /** Where it last was, in @ref space. */
    GfxPoint position;
    /** The plane @ref position is in. Never converted here. */
    InputSpace space;
    /** Modifier state as of the last event this pointer produced. */
    InputModifiers modifiers;
    /** `ROBO_INPUT_POINTER_FLAG_*` bits. */
    uint32_t flags;
    /** Non-zero when this slot is live. */
    int active;
} InputPointerTrack;

/** Every live pointer, by value. Fixed size: a pointer table that grew could not be one field. */
typedef struct InputPointerState {
    /** The live slots. Slots may be sparse; @ref count is how many are live. */
    InputPointerTrack tracks[RoboInputMaxPointers];
    /** Live slots. */
    uint32_t count;
    /** Pointers this state refused because the table was full. Reported, never silent. */
    uint32_t overflowed;
} InputPointerState;

/** Clear @p state to "no pointers". Safe on NULL. */
void InputPointerStateInit(InputPointerState *state);

/**
 * Apply one positioning event's payload, creating the track it names on first sight.
 *
 * The button mask and the position are **taken from the event**, because the event is the newer truth: a
 * `POINTER_UP` for a button the event does not carry in its mask is what releases it, and a state that
 * merged the two would keep a button down forever after a press whose release it saw only as a position.
 *
 * Returns @ref RoboOk with @p outTrack (may be NULL) filled, @ref RoboInvalidArgument for a NULL state, or
 * @ref RoboOutOfMemory when every slot is live and this is a *new* pointer -- counted into @ref overflowed
 * rather than failing silently. A refused event changes nothing.
 */
Result InputPointerStateApply(InputPointerState *state,
                              InputDeviceId device,
                              InputDeviceType type,
                              const InputPointerData *data,
                              InputSpace space,
                              InputModifiers modifiers,
                              InputPointerTrack *outTrack,
                              InputDiag *diag);

/** The track for @p pointer, or NULL. */
const InputPointerTrack *InputPointerStateFind(const InputPointerState *state, InputPointerId pointer);

/** Non-zero when @p pointer is live **and** @p button is down. Safe on NULL (0). */
int InputPointerStateIsButtonDown(const InputPointerState *state,
                                  InputPointerId pointer,
                                  MouseButton button);

/** Modifiers of @p pointer, or none when it is not live. */
InputModifiers InputPointerStateModifiers(const InputPointerState *state, InputPointerId pointer);

/** Drop @p pointer. Returns non-zero when it was live. Its hover and capture entries go with it. */
int InputPointerStateRemove(InputPointerState *state, InputPointerId pointer);

/**
 * Drop every pointer reported by @p device, and report how many went.
 *
 * This is the hotplug door (13.44): an unplugged mouse must not leave a button down or a caret dragging,
 * so the caller runs this **and** the capture release that follows it. A count of zero is normal.
 */
uint32_t InputPointerStateRemoveDevice(InputPointerState *state, InputDeviceId device);

/** Drop every pointer. Returns how many were live. */
uint32_t InputPointerStateClear(InputPointerState *state);

/* ============================================================================================== *
 * Click counting (13.16)
 * ============================================================================================== */

/** How close, and how soon, two presses must be to be one gesture. */
typedef struct InputClickConfig {
    /** Milliseconds within which a second press on the same button continues the sequence. */
    uint32_t doubleClickTimeMs;
    /** How far the pointer may travel, in the position's own units, and still count as the same place. */
    float doubleClickDistance;
} InputClickConfig;

/** The initial configuration: 500 ms and 4 units, the platform-typical pair. */
InputClickConfig InputClickConfigDefault(void);

/** The click sequence for one (pointer, button) pair. */
typedef struct InputClickSequence {
    /** Which pointer. */
    InputPointerId pointer;
    /** Which button. */
    MouseButton button;
    /** The count the last press produced. */
    int count;
    /** When the last press happened. */
    uint64_t lastTime;
    /** Where the last press happened. */
    GfxPoint lastPosition;
    /** Non-zero when @ref lastTime / @ref lastPosition are a press this sequence knows. */
    int active;
} InputClickSequence;

/**
 * Every click sequence this build is tracking, as one field.
 *
 * A sequence is keyed by **(pointer, button)**, which is the whole point of 13.16: a right press between
 * two left presses does not join them, and neither does a second finger. The table is bounded; a new pair
 * when it is full recycles the oldest, because a click sequence that has not been touched for the longest
 * is the one least likely to matter.
 */
typedef struct InputClickState {
    /** The sequences. Sparse; @ref count is how many are live. */
    InputClickSequence sequences[RoboInputMaxPointers * 3u];
    /** Live sequences. */
    uint32_t count;
} InputClickState;

/** Clear @p state to "no sequences". Safe on NULL. */
void InputClickStateInit(InputClickState *state);

/**
 * Record a press and return the click count it completes -- 1 for a first click, 2 for a double, 3 for a
 * triple, and so on.
 *
 * The count continues while the same (pointer, button) pressed again within `doubleClickTimeMs` **and**
 * within `doubleClickDistance` of the previous press; the first condition that fails starts a new
 * sequence at 1, which is the reset the brief asks for. Time is an argument: the suite simulates it, and a
 * build that read a clock here could not be tested.
 *
 * A zero `doubleClickTimeMs` disables continuation entirely -- every press is a single click -- which is
 * the honest way to express "this platform counts its own".
 */
int InputClickStatePress(InputClickState *state,
                         InputPointerId pointer,
                         MouseButton button,
                         GfxPoint position,
                         uint64_t timestamp,
                         const InputClickConfig *config);

/** Forget the sequence for @p pointer / @p button. Returns non-zero when one was live. */
int InputClickStateReset(InputClickState *state, InputPointerId pointer, MouseButton button);

/** Forget every sequence @p pointer owns. Returns how many went. */
uint32_t InputClickStateResetPointer(InputClickState *state, InputPointerId pointer);

/** The count @p pointer / @p button last produced, or 0 when there is no live sequence. */
int InputClickStateCount(const InputClickState *state, InputPointerId pointer, MouseButton button);

/* ============================================================================================== *
 * Pointer capture (13.17)
 * ============================================================================================== */

/** One pointer's capture. */
typedef struct InputCapture {
    /** Which pointer. */
    InputPointerId pointer;
    /** The target that holds it. */
    InputTarget target;
    /** Non-zero when this slot is live. */
    int active;
} InputCapture;

/** Every capture, as one field. */
typedef struct InputPointerCapture {
    /** The captures. Sparse; @ref count is how many are live. */
    InputCapture captures[RoboInputMaxCaptures];
    /** Live captures. */
    uint32_t count;
    /** Captures refused because the target was not a target, or the table was full. Reported. */
    uint32_t refused;
} InputPointerCapture;

/** Clear @p capture to "nobody holds anything". Safe on NULL. */
void InputPointerCaptureInit(InputPointerCapture *capture);

/**
 * Give @p pointer to @p target. Replaces an existing holder -- a caller that captures an already-captured
 * pointer has decided the old holder should stop receiving, and that is a statement, not a conflict.
 * A target that is not valid is @ref RoboInvalidArgument rather than a silent no-op.
 */
Result InputPointerCaptureSet(InputPointerCapture *capture,
                              InputPointerId pointer,
                              const InputTarget *target,
                              InputDiag *diag);

/**
 * Release @p pointer **only if @p target holds it**.
 *
 * The condition is the point: a target that lost a capture to a newer holder must not be able to release
 * the newer holder's capture when its own `up` event finally arrives. Returns non-zero when a capture was
 * released; zero when @p target does not hold it, which is not an error.
 */
int InputPointerCaptureRelease(InputPointerCapture *capture,
                               InputPointerId pointer,
                               const InputTarget *target);

/** The target holding @p pointer, or NULL. */
const InputTarget *InputPointerCaptureTarget(const InputPointerCapture *capture, InputPointerId pointer);

/** Non-zero when @p target holds @p pointer. */
int InputPointerCaptureHolds(const InputPointerCapture *capture,
                             InputPointerId pointer,
                             const InputTarget *target);

/** Release every capture @p target holds -- the door a destroyed target needs (13.17). Returns how many. */
uint32_t InputPointerCaptureReleaseTarget(InputPointerCapture *capture, const InputTarget *target);

/** Release every capture in @p window -- the door a deactivating window needs (13.28). Returns how many. */
uint32_t InputPointerCaptureReleaseWindow(InputPointerCapture *capture, WindowId window);

/**
 * Release every capture of @p pointer one at a time, reporting the target that held each.
 *
 * A pointer that ended or was cancelled must lose its captures, and the layer that owns the tree needs to
 * *know which target* so it can deliver the terminal event there. Copies at most @p capacity entries into
 * @p outTargets and returns the number of captures released, which may exceed @p capacity.
 */
uint32_t InputPointerCaptureReleaseAll(InputPointerCapture *capture,
                                       InputPointerId pointer,
                                       InputTarget *outTargets,
                                       uint32_t capacity);

/** Release everything. Returns how many went. */
uint32_t InputPointerCaptureClear(InputPointerCapture *capture);

/* ============================================================================================== *
 * Hover (13.18)
 * ============================================================================================== */

/** One pointer's hover. */
typedef struct InputHover {
    /** Which pointer. */
    InputPointerId pointer;
    /** What it is over. */
    InputTarget target;
    /** Non-zero when this slot is live. */
    int active;
} InputHover;

/** Every hover, as one field. */
typedef struct InputHoverState {
    /** The hovers. Sparse; @ref count is how many are live. */
    InputHover hovers[RoboInputMaxPointers];
    /** Live hovers. */
    uint32_t count;
    /** Enter events suppressed because the target was already entered -- a non-zero count is a caller
     *  answering a question it already had answered, and it is reported so it can be found. */
    uint32_t deduplicated;
} InputHoverState;

/** Clear @p state to "over nothing". Safe on NULL. */
void InputHoverStateInit(InputHoverState *state);

/**
 * Record that @p pointer is now over @p target (NULL for "over nothing") and report the transition.
 *
 * The order is **leave, then enter**, and the pair is at most one of each, which is the model the brief
 * asks for: a move from A to B produces `Leave(A)` and `Enter(B)` in that order, a move inside A produces
 * neither, and re-reporting A produces neither. A target is therefore never entered twice and never left
 * twice, and `deduplicated` counts the redundant reports rather than hiding them.
 *
 * @p outLeave / @p outEnter are written only when that half of the transition happened; pass NULL to
 * ignore it. Returns 0, 1 or 2 -- how many events the caller must dispatch -- or -1 for a NULL state.
 */
int InputHoverSet(InputHoverState *state,
                  InputPointerId pointer,
                  const InputTarget *target,
                  InputTarget *outLeave,
                  InputTarget *outEnter);

/** What @p pointer is over, or NULL. */
const InputTarget *InputHoverTarget(const InputHoverState *state, InputPointerId pointer);

/** Non-zero when @p pointer is over @p target. */
int InputHoverIsOver(const InputHoverState *state, InputPointerId pointer, const InputTarget *target);

/** Forget @p pointer's hover **without** emitting a leave. The door a disconnect needs. Returns non-zero. */
int InputHoverStateRemove(InputHoverState *state, InputPointerId pointer);

/** Forget every hover. Returns how many were live. */
uint32_t InputHoverStateClear(InputHoverState *state);

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

/** Longest line any writer in this header emits: one track's fields, bounded. */
#define RoboInputPointerLineMax 256u

/**
 * One complete line for @p track, or @ref RoboNotSupported when it does not fit.
 *
 * The convention every writer in this module follows: **complete or absent**. A buffer too small for the
 * whole line is left empty and the length it would have needed is written to @p outLength, because half a
 * record is worse than none -- a reader cannot tell a truncation from a field it does not know.
 */
Result InputPointerTrackWrite(const InputPointerTrack *track,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/** One complete line for @p state's summary, or @ref RoboNotSupported. */
Result InputPointerStateWrite(const InputPointerState *state,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/** One complete line for @p capture's summary, or @ref RoboNotSupported. */
Result InputPointerCaptureWrite(const InputPointerCapture *capture,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                InputDiag *diag);

/** One complete line for @p state's summary, or @ref RoboNotSupported. */
Result InputHoverStateWrite(const InputHoverState *state,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            InputDiag *diag);

/** One complete line for @p state's click table, or @ref RoboNotSupported. */
Result InputClickStateWrite(const InputClickState *state,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_POINTER_STATE_H */
