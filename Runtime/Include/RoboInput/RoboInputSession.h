/*
 * The input session: hotplug and the transient reset (Phase 13.44, 13.78).
 *
 * ## Why this type exists
 *
 * Every module in this tree owns one table: the device registry knows which devices exist, the keyboard
 * state knows which keys are held, the pointer state knows where each pointer is, the capture table knows who
 * owns one, the touch state knows which contacts are down, the focus manager knows who has keyboard focus,
 * the text pipeline knows which accent is waiting, the drag session knows what is being dragged and the
 * gesture arena knows which recognizer is holding the sequence.
 *
 * A disconnect has to release **all of them at once**, and that is a requirement no single module can meet:
 * `InputKeyboardStateDisconnect` releases keys and cannot release a capture, `InputPointerStateRemoveDevice`
 * removes pointers and cannot cancel a drag. So the completeness claim -- "no stale state may remain" -- has
 * exactly one honest home, and this is it: **one object that owns every table a device can put state in, and
 * three doors that sweep them.**
 *
 * ## Three sweeps, one record
 *
 *   `Disconnect`          a device went away: everything attributed to it goes. Keys, pointers, clicks,
 *                         hovers, touches, captures held by those pointers, the repeat, and -- if they
 *                         belonged to it -- the pending accent;
 *
 *   `DeactivateWindow`    a window lost activation (13.28): the captures held in it, the focus it had, a drag
 *                         that was happening *in* it, and the transient gesture. The keys are deliberately
 *                         left alone: a user may still be holding them as the window changes, and a held key
 *                         belongs to the device rather than to a window;
 *
 *   `TargetReleased`      a target was destroyed (13.27): its focus and its captures go **by identity**.
 *                         Nothing else here is keyed by target, which is why this door returns a count rather
 *                         than a full record.
 *
 * Every door fills one `InputSessionSweep`: counts, not a history, because the question a caller and a test
 * both ask is "is anything still held?" -- and a leaked capture is invisible to a log but obvious in a count.
 *
 * ## What a reset does not do
 *
 * `ResetTransient` (13.78) cancels what the *user's input is doing* -- keys, pointers, captures, hovers,
 * clicks, contacts, a drag, a gesture and the pending accent. It does **not** touch focus, on purpose: focus
 * is not transient state, it is a relationship the shell and the user both see, and clearing it would be a
 * focus *change* that nobody asked for and no listener could name a reason for. A caller that means "the
 * focused target is gone" has `TargetReleased` for that.
 *
 * See `Documentation/Runtime/Input/Session.md`.
 */

#ifndef ROBO_INPUT_SESSION_H
#define ROBO_INPUT_SESSION_H

#include "RoboInput/RoboInputDevice.h"
#include "RoboInput/RoboInputDragDrop.h"
#include "RoboInput/RoboInputFocus.h"
#include "RoboInput/RoboInputGesture.h"
#include "RoboInput/RoboInputKeyboardState.h"
#include "RoboInput/RoboInputPointerState.h"
#include "RoboInput/RoboInputText.h"
#include "RoboInput/RoboInputTouch.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What a sweep released.
 *
 * A count per table, and the four flags are transitions rather than counts: there is one repeat, one pending
 * accent, one drag and one arena, so "how many" would be a number that can only ever be 0 or 1 written as a
 * count.
 */
typedef struct InputSessionSweep {
    /** Keys released (`InputKeyboardStateDisconnect` / `ReleaseAll`). */
    uint32_t keysReleased;
    /** Pointers removed from the live table. */
    uint32_t pointersRemoved;
    /** Captures released. */
    uint32_t capturesReleased;
    /** Click sequences reset. */
    uint32_t clicksReset;
    /** Hovers cleared. */
    uint32_t hoversCleared;
    /** Touch contacts removed. */
    uint32_t touchesRemoved;
    /** Focused or scoped targets released. */
    uint32_t focusReleased;
    /** Non-zero when a pending accent was abandoned. */
    int deadKeyCancelled;
    /** Non-zero when an armed auto-repeat was stopped. */
    int repeatStopped;
    /** Non-zero when a running drag was cancelled. */
    int dragCancelled;
    /** Non-zero when the gesture arena was reset while a sequence was unresolved. */
    int gesturesReset;
} InputSessionSweep;

/** Clear @p sweep. Safe on NULL. */
void InputSessionSweepInit(InputSessionSweep *sweep);

/** Non-zero when @p sweep released nothing at all: the assertion "no stale state remained". Safe on NULL (0). */
int InputSessionSweepIsEmpty(const InputSessionSweep *sweep);

/** One complete line for @p sweep, or @ref RoboNotSupported when it does not fit. */
Result InputSessionSweepWrite(const InputSessionSweep *sweep,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/**
 * One input system's whole state, composed.
 *
 * The tables are members rather than pointers because a session *is* the object that owns them: a caller that
 * wanted one table in isolation uses that module's own type, and a caller that wants hotplug and reset to be
 * complete uses this.
 */
typedef struct InputSession {
    /** Which devices exist. */
    InputDeviceRegistry devices;
    /** Which keys are held, and the repeat cadence. */
    InputKeyboardState keyboard;
    /** Where each pointer is, and which buttons it holds. */
    InputPointerState pointers;
    /** Click sequences, which a device's removal invalidates. */
    InputClickState clicks;
    /** Which targets own which pointers. */
    InputPointerCapture captures;
    /** Which target each pointer is over. */
    InputHoverState hover;
    /** Which contacts are down. */
    InputTouchState touches;
    /** Which target receives keyboard input. */
    InputFocusManager focus;
    /** The layout plus the one waiting accent. */
    InputTextPipeline text;
    /** A drag in progress, if any. */
    InputDragSession drag;
    /** The recognizers competing for the current sequence. */
    InputGestureArena gestures;

    /** Devices connected through this session. */
    uint32_t connects;
    /** Devices disconnected through it. */
    uint32_t disconnects;
    /** Sweeps run, of any of the three kinds. */
    uint32_t sweeps;
    /** Targets released by identity. */
    uint32_t targetsReleased;
    /** Windows deactivated. */
    uint32_t deactivations;
} InputSession;

/** Bring @p session to "no devices, nothing held, nobody focused". Safe on NULL. */
void InputSessionInit(InputSession *session);

/**
 * Connect a device through the session.
 *
 * The registry's own rules apply unchanged (a full table or a duplicate id is refused, and the reason is in
 * the diagnostic). Connecting is counted, so a caller can tell "this session has seen a device" from "this
 * session inherited a table".
 */
Result InputSessionConnect(InputSession *session,
                           const InputDeviceDescriptor *description,
                           InputDeviceId *outId,
                           InputDiag *diag);

/**
 * Disconnect @p id: remove it from the registry **and** sweep everything it held.
 *
 * @p out (may be NULL) receives the sweep. An id that is not in the table is `RoboNotFound` and does *not*
 * sweep: a caller that lost track of its own table should hear about it rather than have this layer quietly
 * release a stranger's keys.
 */
Result InputSessionDisconnect(InputSession *session,
                              InputDeviceId id,
                              InputSessionSweep *out,
                              InputDiag *diag);

/**
 * Sweep a device the session's registry does not own.
 *
 * The door for a caller whose device table lives elsewhere (a platform adapter, a test): the release half of
 * a disconnect without the registry half. It is deliberately separate rather than a flag on `Disconnect`,
 * because "remove this device" and "release what it held" are two claims and a caller may need exactly one.
 */
Result InputSessionDeviceLost(InputSession *session, InputDeviceId id, InputSessionSweep *out);

/**
 * Release everything attributed to @p target: its focus and its captures.
 *
 * Returns how many places it was found (0, 1 or 2). The door 13.27 requires -- a destroyed target may not
 * remain focused or captured -- and it is keyed by identity so it cannot miss a target that was focused in
 * one scope and captured for a pointer in another.
 */
uint32_t InputSessionTargetReleased(InputSession *session, const InputTarget *target);

/**
 * A window was deactivated (13.28).
 *
 * Releases the captures held in it, the focus it had, a drag that was happening in it, and the unresolved
 * gesture. Keys are left alone -- see this header's comment -- and so is a drag that merely *ended* in the
 * window but began elsewhere, which is keyed on both ends.
 *
 * @p out (may be NULL) receives the sweep.
 */
Result InputSessionWindowDeactivated(InputSession *session, WindowId window, InputSessionSweep *out);

/**
 * Cancel every transient thing at once (13.78).
 *
 * The whole sweep: keys, pointers, captures, hovers, clicks, contacts, the pending accent, the repeat, the
 * drag and the arena. Focus stays -- see this header's comment for why.
 */
Result InputSessionResetTransient(InputSession *session, InputSessionSweep *out);

/** Devices in the session's registry. */
uint32_t InputSessionDeviceCount(const InputSession *session);

/** Non-zero when @p id is connected. */
int InputSessionHasDevice(const InputSession *session, InputDeviceId id);

/** Non-zero when nothing is held anywhere: no keys, pointers, captures, contacts or drag. */
int InputSessionIsQuiescent(const InputSession *session);

/** One complete line for @p session, or @ref RoboNotSupported when it does not fit. */
Result InputSessionWrite(const InputSession *session,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_SESSION_H */
