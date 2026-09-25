/*
 * Multi-touch (Phase 13.20/13.21).
 *
 * ## The one thing that makes touch different
 *
 * A mouse is one pointer that always exists. A touchscreen is **zero to N contacts that appear and
 * disappear**, and the identity of a contact is the whole game: a finger that lifts and a finger that lifts
 * *and another that lands* produce the same number of events and completely different gestures. So a contact
 * is keyed by the **touch id the hardware reports**, never by its index in an array -- an array index is a
 * position in a list that changes when somebody else lifts a finger, which is how a two-finger pinch becomes
 * a one-finger drag.
 *
 * ## Normalization, not a second vocabulary
 *
 * `InputTouchContact` is a *view* of one contact in the pointer model: 13.21 asks for a path from touch
 * hardware to pointer events, and `InputTouchToPointer` is that conversion -- the contact's own id becomes the
 * pointer id, its phase picks the event type, and its position and pressure ride the existing
 * `InputPointerData` and `InputPenData` records. There is no second event type and no second payload, because
 * a consumer that had to learn two would be a consumer that could get them out of step.
 *
 * ## Cancellation is a phase, not an absence
 *
 * `Cancel` is a real phase and it *keeps the contact* until the caller acknowledges it. A contact that simply
 * vanished would leave a gesture recognizer waiting for an end that never comes, which is why
 * `InputTouchStateEnd` removes a contact and `…Cancel` marks it -- the same distinction the pointer flags
 * already draw between "ended" and "cancelled".
 *
 * See `Documentation/Runtime/Input/Touch.md`.
 */

#ifndef ROBO_INPUT_TOUCH_H
#define ROBO_INPUT_TOUCH_H

#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputPen.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Contacts one digitizer may have down at once. Ten fingers, plus a palm and a stylus. */
#define RoboInputMaxTouches 32u

/** What a contact is doing. */
typedef enum InputTouchPhase {
    /** A finger landed. */
    RoboInputTouchPhaseBegin = 0,
    /** A finger moved. */
    RoboInputTouchPhaseMove = 1,
    /** A finger lifted: the contact is over and its state is gone. */
    RoboInputTouchPhaseEnd = 2,
    /** The sequence was cancelled -- a window deactivated, a device disconnected. */
    RoboInputTouchPhaseCancel = 3
} InputTouchPhase;

/** Number of defined phases. Asserted positionally by `input.touch.*`. */
#define RoboInputTouchPhaseCount 4u

/** Non-zero if @p phase is a defined phase. */
int InputTouchPhaseIsKnown(InputTouchPhase phase);

/** Human-readable phase; never NULL; `"unknown"` when undefined. */
const char *InputTouchPhaseName(InputTouchPhase phase);

/** Non-zero when @p phase removes the contact (`END` and `CANCEL` are both terminal). */
int InputTouchPhaseIsTerminal(InputTouchPhase phase);

/** One finger, as the trailing tracking layer knows it. */
typedef struct InputTouchContact {
    /** The hardware's id for this contact. Stable for its whole life, and never reused while it lives. */
    uint32_t id;
    /** What it is doing. */
    InputTouchPhase phase;
    /** Where it is, in @ref space. */
    GfxPoint position;
    /** Where it was, when the event carried it. Read when @ref hasPrevious. */
    GfxPoint previous;
    /** Non-zero when @ref previous is a position this contact has. */
    int hasPrevious;
    /** Contact pressure, 0..1. Zero when the digitizer does not report it. */
    float pressure;
    /** Contact size, in the digitizer's own units. Zero when unreported. */
    float size;
    /** The device that reported it. */
    InputDeviceId device;
    /** The plane @ref position is in. A touch moves between planes only when a caller says so. */
    InputSpace space;
    /** Non-zero when this contact is a stylus rather than a finger. */
    int pen;
    /** `RoboInputPointerFlag*` bits besides the primary flag, which is per-contact and derived. */
    uint32_t flags;
    /** Timestamp of the last update. */
    uint64_t timestampMs;
} InputTouchContact;

/** Every contact, as one value. */
typedef struct InputTouchState {
    /** The contacts. Sparse; @ref count is how many are live. */
    InputTouchContact contacts[RoboInputMaxTouches];
    /** Live contacts. */
    uint32_t count;
    /** Contacts this state refused because the table was full, or the id was not a defined contact. */
    uint32_t refused;
    /** `BEGIN` contacts that have ended since the last reset: how many gestures have started. */
    uint32_t sequences;
    /** The largest contact count seen, which is what a digitizer's real capability looks like. */
    uint32_t maxSeen;
} InputTouchState;

/** Clear @p state to "no fingers down". Safe on NULL. */
void InputTouchStateInit(InputTouchState *state);

/**
 * Apply one contact's update, creating it on `BEGIN` and removing it on `END`.
 *
 * The rules, and each is a way this goes wrong in the wild:
 *
 *   - a `BEGIN` for an id that is already live **replaces** it rather than adding a second entry, because two
 *     contacts with one id is not a thing hardware can mean;
 *   - a `MOVE`, `END` or `CANCEL` for an id that is *not* live is refused and counted (`RoboNotFound`) --
 *     inventing a contact for a move is how a lost `BEGIN` becomes a phantom finger that never lifts;
 *   - a `CANCEL` **keeps** the contact, marked cancelled, so a recognizer can see why its sequence stopped;
 *     the caller acknowledges with @ref InputTouchStateRemove;
 *   - a `BEGIN` when the table is full is refused and counted (`RoboOutOfMemory`) rather than dropping
 *     somebody else's finger.
 *
 * @p out (may be NULL) receives the contact as stored. A refused update changes nothing.
 */
Result InputTouchStateApply(InputTouchState *state,
                            uint32_t id,
                            InputTouchPhase phase,
                            GfxPoint position,
                            InputDeviceId device,
                            InputSpace space,
                            float pressure,
                            float size,
                            int pen,
                            uint64_t timestampMs,
                            InputTouchContact *out,
                            InputDiag *diag);

/** The contact with @p id, or NULL. */
const InputTouchContact *InputTouchStateFind(const InputTouchState *state, uint32_t id);

/** Remove @p id. Returns non-zero when it was live. The acknowledgement a `CANCEL` needs. */
int InputTouchStateRemove(InputTouchState *state, uint32_t id);

/** Remove every contact of @p device, reporting how many went -- the hotplug door (13.44). */
uint32_t InputTouchStateRemoveDevice(InputTouchState *state, InputDeviceId device);

/** Remove every contact. Returns how many were live. */
uint32_t InputTouchStateClear(InputTouchState *state);

/** Non-zero when any live contact is down (not cancelled). */
int InputTouchStateAnyDown(const InputTouchState *state);

/**
 * The **primary** contact: the first one still down, in the order the hardware reported them.
 *
 * A gesture layer needs one pointer to be *the* pointer -- a tap is one finger even when three are down --
 * and "the first live one" is the only rule that is stable while the others come and go.
 */
const InputTouchContact *InputTouchStatePrimary(const InputTouchState *state);

/* ============================================================================================== *
 * Normalization into the pointer model (13.21)
 * ============================================================================================== */

/**
 * Which pointer-event type @p phase becomes.
 *
 * `BEGIN` → `POINTER_DOWN`, `MOVE` → `POINTER_MOVE`, `END` → `POINTER_UP`, `CANCEL` → `POINTER_CANCEL`.
 * Stated as a door rather than left to a caller's switch, because the mapping is the *normalization* the brief
 * asks for and a second copy of it is a second opinion.
 */
InputEventType InputTouchPhaseToEventType(InputTouchPhase phase);

/**
 * Express @p contact as a pointer payload.
 *
 * The contact's id becomes the pointer id (`RoboInputPointerIdPrimary` for the first live contact, so a
 * one-finger tap is not a special case for a consumer), the phase picks the button-mask meaning a touch
 * already has -- a touch has no buttons, and the *primary* contact reports the left button down so that every
 * consumer that understands a mouse understands a tap -- and the pressure and size ride along in the payload's
 * wheel-free space.
 *
 * @p out receives the payload and @p outType the event type. Refused for NULL arguments or a contact with no
 * space.
 */
Result InputTouchToPointer(const InputTouchContact *contact,
                           int isPrimary,
                           InputPointerData *out,
                           InputEventType *outType,
                           InputDiag *diag);

/** One complete line for @p contact, or @ref RoboNotSupported when it does not fit. */
Result InputTouchContactWrite(const InputTouchContact *contact,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/** One complete line for @p state's summary, or @ref RoboNotSupported when it does not fit. */
Result InputTouchStateWrite(const InputTouchState *state,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_TOUCH_H */
