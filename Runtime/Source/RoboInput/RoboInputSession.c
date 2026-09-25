/*
 * The input session (Phase 13.44, 13.78) — see `RoboInputSession.h`.
 *
 * The file is three pieces:
 *
 *   **the sweep**     one record, one writer, and the "did anything go?" question a test asks;
 *   **the helper**    `RiSessionSweepPointers`, which captures a device's live pointer ids *before* they are
 *                     removed, because a click sequence and a hover are keyed by pointer and both have to be
 *                     answered while the table still knows which pointers were that device's;
 *   **the doors**     connect, disconnect, device-lost, target-released, window-deactivated, reset.
 *
 * The order inside a sweep matters and is the same everywhere: read what is *there* first (a pending accent,
 * an armed repeat, a running drag, a captured pointer), then release, then record. A sweep that released
 * first and asked afterwards would answer "nothing" to every question -- which is exactly the defect a
 * counter is supposed to catch, so the reading is done first on purpose.
 */

#include "RoboInput/RoboInputSession.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * The sweep record
 * ============================================================================================== */

void InputSessionSweepInit(InputSessionSweep *sweep)
{
    if (!sweep) {
        return;
    }
    memset(sweep, 0, sizeof(*sweep));
}

int InputSessionSweepIsEmpty(const InputSessionSweep *sweep)
{
    if (!sweep) {
        return 0;
    }
    return sweep->keysReleased == 0u && sweep->pointersRemoved == 0u && sweep->capturesReleased == 0u &&
           sweep->clicksReset == 0u && sweep->hoversCleared == 0u && sweep->touchesRemoved == 0u &&
           sweep->focusReleased == 0u && sweep->deadKeyCancelled == 0 && sweep->repeatStopped == 0 &&
           sweep->dragCancelled == 0 && sweep->gesturesReset == 0;
}

Result InputSessionSweepWrite(const InputSessionSweep *sweep,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!sweep) {
        RiWriterText(&text, "sweep <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterFieldU64(&text, "sweep keys", (uint64_t)sweep->keysReleased);
    RiWriterFieldU64(&text, " pointers", (uint64_t)sweep->pointersRemoved);
    RiWriterFieldU64(&text, " captures", (uint64_t)sweep->capturesReleased);
    RiWriterFieldU64(&text, " clicks", (uint64_t)sweep->clicksReset);
    RiWriterFieldU64(&text, " hovers", (uint64_t)sweep->hoversCleared);
    RiWriterFieldU64(&text, " touches", (uint64_t)sweep->touchesRemoved);
    RiWriterFieldU64(&text, " focus", (uint64_t)sweep->focusReleased);
    RiWriterFieldU64(&text, " accent", (uint64_t)(sweep->deadKeyCancelled ? 1u : 0u));
    RiWriterFieldU64(&text, " repeat", (uint64_t)(sweep->repeatStopped ? 1u : 0u));
    RiWriterFieldU64(&text, " drag", (uint64_t)(sweep->dragCancelled ? 1u : 0u));
    RiWriterFieldU64(&text, " gestures", (uint64_t)(sweep->gesturesReset ? 1u : 0u));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

/* ============================================================================================== *
 * Fixtures
 * ============================================================================================== */

void InputSessionInit(InputSession *session)
{
    if (!session) {
        return;
    }
    memset(session, 0, sizeof(*session));
    InputDeviceRegistryInit(&session->devices);
    InputKeyboardStateInit(&session->keyboard);
    InputPointerStateInit(&session->pointers);
    InputClickStateInit(&session->clicks);
    InputPointerCaptureInit(&session->captures);
    InputHoverStateInit(&session->hover);
    InputTouchStateInit(&session->touches);
    InputFocusManagerInit(&session->focus);
    InputTextPipelineInit(&session->text);
    InputDragSessionInit(&session->drag);
    InputGestureArenaInit(&session->gestures);
}

/* ============================================================================================== *
 * The sweep
 * ============================================================================================== */

/**
 * The live pointer ids a device owns, copied out before they are removed.
 *
 * A click sequence and a hover are keyed by *pointer*, and neither table can be asked "which pointer?" after
 * the pointer table has forgotten. Copying the ids first is what makes "the clicks of a disconnected mouse
 * are reset" a fact rather than a hope.
 *
 * Returns how many were copied, which may exceed @p capacity.
 */
static uint32_t RiSessionDevicePointers(const InputPointerState *state,
                                       InputDeviceId device,
                                       InputPointerId *out,
                                       uint32_t capacity)
{
    uint32_t index;
    uint32_t count = 0u;

    if (!state || !out || device == RoboInputDeviceIdNone) {
        return 0u;
    }
    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (!state->tracks[index].active || state->tracks[index].device != device) {
            continue;
        }
        if (count < capacity) {
            out[count] = state->tracks[index].pointer;
        }
        count++;
    }
    return count;
}

/**
 * Release everything attributed to one device.
 *
 * The registry half is the caller's: this takes an id, not a descriptor, because a device may be gone from
 * the table by the time its state is released (and a caller whose table lives elsewhere has no descriptor to
 * pass).
 */
static Result RiSessionSweepDevice(InputSession *session,
                                   InputDeviceId device,
                                   InputSessionSweep *out)
{
    InputSessionSweep sweep;
    InputPointerId pointers[RoboInputMaxPointers];
    uint32_t pointerCount;
    uint32_t index;

    InputSessionSweepInit(&sweep);
    if (!session || device == RoboInputDeviceIdNone) {
        return RoboInvalidArgument;
    }

    pointerCount = RiSessionDevicePointers(&session->pointers, device, pointers, RoboInputMaxPointers);

    /* Read the transient facts *before* releasing them, so the record says what was there. */
    if (session->keyboard.repeatArmed && session->keyboard.repeatDevice == device) {
        sweep.repeatStopped = 1;
    }
    if (session->text.dead.id != RoboInputDeadKeyIdNone) {
        sweep.deadKeyCancelled = 1;
    }

    /*
     * Captures first, while the pointer table still knows who held what: a capture is released by pointer,
     * and after the pointers are gone there is nothing left to ask.
     */
    for (index = 0u; index < pointerCount; index++) {
        sweep.capturesReleased += InputPointerCaptureReleaseAll(&session->captures, pointers[index], NULL, 0u);
        sweep.hoversCleared += InputHoverStateRemove(&session->hover, pointers[index]) ? 1u : 0u;
        sweep.clicksReset += InputClickStateResetPointer(&session->clicks, pointers[index]);
    }

    sweep.keysReleased = InputKeyboardStateDisconnect(&session->keyboard, device);
    sweep.pointersRemoved = InputPointerStateRemoveDevice(&session->pointers, device);
    sweep.touchesRemoved = InputTouchStateRemoveDevice(&session->touches, device);

    /*
     * The accent and the drag are not attributed to a device in this model, and pretending otherwise would be
     * a guess. What is honest: a pending accent belongs to the *keyboard* that armed it, and a running drag
     * belongs to the pointer that started it -- so both go when their owner does, and a device that owns
     * neither leaves them alone.
     */
    if (sweep.deadKeyCancelled) {
        (void)InputTextPipelineCancelDeadKey(&session->text);
    }
    /*
     * A drag is keyed by *target*, not by device, so a disconnect cannot ask "was this drag yours?". The
     * honest answer is to cancel it and **say** that it was cancelled: a drag with no pointer behind it can
     * never be dropped, and leaving one running would be state no later event could resolve. The alternative
     * -- guessing from the payload -- would be a rule nobody could read off the model.
     */
    if (InputDragSessionIsActive(&session->drag)) {
        (void)InputDragSessionReset(&session->drag);
        sweep.dragCancelled = 1;
    }
    if (session->gestures.winner >= 0) {
        InputGestureArenaReset(&session->gestures);
        sweep.gesturesReset = 1;
    }

    session->sweeps++;
    if (out) {
        *out = sweep;
    }
    return RoboOk;
}

/* ============================================================================================== *
 * The doors
 * ============================================================================================== */

Result InputSessionConnect(InputSession *session,
                           const InputDeviceDescriptor *description,
                           InputDeviceId *outId,
                           InputDiag *diag)
{
    Result status;

    if (!session) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "an input session is required");
    }
    status = InputDeviceRegistryConnect(&session->devices, description, outId, diag);
    if (status == RoboOk) {
        session->connects++;
    }
    return status;
}

Result InputSessionDisconnect(InputSession *session,
                              InputDeviceId id,
                              InputSessionSweep *out,
                              InputDiag *diag)
{
    Result status;

    if (!session) {
        return RoboInputDiag(diag, RoboInputDomainSession, RoboInvalidArgument,
                             "an input session is required");
    }
    /* The registry owns the "is this device mine?" question, and it answers `RoboNotFound` -- which is why
     * the sweep runs only after it agrees the device was there. */
    status = InputDeviceRegistryDisconnect(&session->devices, id, NULL, diag);
    if (status != RoboOk) {
        return status;
    }
    session->disconnects++;
    return RiSessionSweepDevice(session, id, out);
}

Result InputSessionDeviceLost(InputSession *session, InputDeviceId id, InputSessionSweep *out)
{
    if (!session || id == RoboInputDeviceIdNone) {
        return RoboInvalidArgument;
    }
    return RiSessionSweepDevice(session, id, out);
}

uint32_t InputSessionTargetReleased(InputSession *session, const InputTarget *target)
{
    uint32_t released = 0u;

    if (!session || !InputTargetIsValid(target)) {
        return 0u;
    }
    /* Focus by identity: `InputFocusManagerReleaseTarget` answers both "was it the active focus?" and "was it
     * a scope's focus?", which is the difference a caller cannot see from outside. */
    released += InputFocusManagerReleaseTarget(&session->focus, target);
    /* ... and the captures: every pointer that held it lets go, which is why the return is not per-pointer. */
    released += InputPointerCaptureReleaseTarget(&session->captures, target);

    if (released > 0u) {
        session->targetsReleased++;
    }
    return released;
}

Result InputSessionWindowDeactivated(InputSession *session, WindowId window, InputSessionSweep *out)
{
    InputSessionSweep sweep;

    if (!session || window == 0u) {
        return RoboInvalidArgument;
    }
    InputSessionSweepInit(&sweep);

    sweep.capturesReleased = InputPointerCaptureReleaseWindow(&session->captures, window);
    if (InputFocusManagerReleaseWindow(&session->focus, window)) {
        sweep.focusReleased = 1u;
    }

    /* A drag that began in the window, or is currently over something in it, is happening *in* it. */
    if (InputDragSessionIsActive(&session->drag)) {
        int inside = InputTargetIsWindow(&session->drag.source, window);
        int over = session->drag.hasOver && InputTargetIsWindow(&session->drag.over, window);

        if (inside || over) {
            (void)InputDragSessionReset(&session->drag);
            sweep.dragCancelled = 1;
        }
    }
    /* A gesture half-way through a sequence is about a pointer that is no longer being watched for it. */
    if (session->gestures.winner >= 0) {
        InputGestureArenaReset(&session->gestures);
        sweep.gesturesReset = 1;
    }

    /*
     * A pending accent survives a window change on purpose: the user's next keystroke may well land in the
     * newly activated window's field, and abandoning the accent would silently lose it. It is cancelled only
     * when the *keyboard* goes away (a disconnect) or when a caller asks (`ResetTransient`).
     */
    session->deactivations++;
    session->sweeps++;
    if (out) {
        *out = sweep;
    }
    return RoboOk;
}

Result InputSessionResetTransient(InputSession *session, InputSessionSweep *out)
{
    InputSessionSweep sweep;
    uint32_t index;

    if (!session) {
        return RoboInvalidArgument;
    }
    InputSessionSweepInit(&sweep);

    /* Read the transient facts first, then release: the same order every sweep uses. */
    if (session->keyboard.repeatArmed) {
        sweep.repeatStopped = 1;
    }
    if (session->text.dead.id != RoboInputDeadKeyIdNone) {
        sweep.deadKeyCancelled = 1;
    }

    for (index = 0u; index < RoboInputMaxPointers; index++) {
        if (!session->pointers.tracks[index].active) {
            continue;
        }
        sweep.clicksReset += InputClickStateResetPointer(&session->clicks, session->pointers.tracks[index].pointer);
    }

    sweep.capturesReleased = InputPointerCaptureClear(&session->captures);
    sweep.hoversCleared = InputHoverStateClear(&session->hover);
    sweep.keysReleased = InputKeyboardStateHeldCount(&session->keyboard);
    InputKeyboardStateReleaseAll(&session->keyboard);
    sweep.pointersRemoved = InputPointerStateClear(&session->pointers);
    sweep.touchesRemoved = InputTouchStateClear(&session->touches);

    if (sweep.deadKeyCancelled) {
        (void)InputTextPipelineCancelDeadKey(&session->text);
    }
    if (InputDragSessionIsActive(&session->drag)) {
        (void)InputDragSessionReset(&session->drag);
        sweep.dragCancelled = 1;
    }
    if (session->gestures.winner >= 0) {
        InputGestureArenaReset(&session->gestures);
        sweep.gesturesReset = 1;
    }

    /* Focus and the lock bits are deliberately untouched: see this header's comment. Locks are the user's
     * keyboard state, not a transient interaction, and clearing caps lock under a user would be a change
     * they did not make. */

    session->sweeps++;
    if (out) {
        *out = sweep;
    }
    return RoboOk;
}

/* ============================================================================================== *
 * Questions
 * ============================================================================================== */

uint32_t InputSessionDeviceCount(const InputSession *session)
{
    return session ? InputDeviceRegistryCount(&session->devices) : 0u;
}

int InputSessionHasDevice(const InputSession *session, InputDeviceId id)
{
    return session ? InputDeviceRegistryContains(&session->devices, id) : 0;
}

int InputSessionIsQuiescent(const InputSession *session)
{
    if (!session) {
        return 0;
    }
    return InputKeyboardStateHeldCount(&session->keyboard) == 0u && session->pointers.count == 0u &&
           session->captures.count == 0u && session->touches.count == 0u &&
           !InputDragSessionIsActive(&session->drag);
}

Result InputSessionWrite(const InputSession *session,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!session) {
        RiWriterText(&text, "session <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "session devices=");
    RiWriterU64(&text, (uint64_t)InputSessionDeviceCount(session));
    RiWriterFieldU64(&text, " keys", (uint64_t)InputKeyboardStateHeldCount(&session->keyboard));
    RiWriterFieldU64(&text, " pointers", (uint64_t)session->pointers.count);
    RiWriterFieldU64(&text, " captures", (uint64_t)session->captures.count);
    RiWriterFieldU64(&text, " hovers", (uint64_t)session->hover.count);
    RiWriterFieldU64(&text, " touches", (uint64_t)session->touches.count);
    RiWriterFieldU64(&text, " focus", (uint64_t)(InputFocusManagerTarget(&session->focus) ? 1u : 0u));
    RiWriterFieldText(&text, " accent",
                      InputDeadKeyName((uint32_t)InputTextPipelinePendingDeadKey(&session->text)));
    RiWriterFieldText(&text, " drag", InputDragPhaseName(session->drag.phase));
    RiWriterFieldU64(&text, " connects", (uint64_t)session->connects);
    RiWriterFieldU64(&text, " disconnects", (uint64_t)session->disconnects);
    RiWriterFieldU64(&text, " sweeps", (uint64_t)session->sweeps);
    RiWriterFieldU64(&text, " targets", (uint64_t)session->targetsReleased);
    RiWriterFieldU64(&text, " deactivations", (uint64_t)session->deactivations);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
