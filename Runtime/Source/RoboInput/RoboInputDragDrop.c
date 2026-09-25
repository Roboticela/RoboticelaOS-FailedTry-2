/*
 * Drag and drop (Phase 13.42) — see `RoboInputDragDrop.h`.
 *
 * One state machine and one payload. The state machine's whole substance is the *negotiation*: what the source
 * offers, what the target accepts, and the fact that a drop only happens where the two agree.
 */

#include "RoboInput/RoboInputDragDrop.h"

#include "RoboInputInternal.h"

#include <string.h>

/* ============================================================================================== *
 * Vocabulary
 * ============================================================================================== */

int InputDragOperationIsKnown(uint32_t operation)
{
    return operation != 0u && (operation & ~RoboInputDragOperationAll) == 0u;
}

const char *InputDragOperationName(uint32_t operations)
{
    switch (operations & RoboInputDragOperationAll) {
    case RoboInputDragOperationCopy:
        return "copy";
    case RoboInputDragOperationMove:
        return "move";
    case RoboInputDragOperationLink:
        return "link";
    case RoboInputDragOperationCopy | RoboInputDragOperationMove:
        return "copy+move";
    case RoboInputDragOperationCopy | RoboInputDragOperationLink:
        return "copy+link";
    case RoboInputDragOperationMove | RoboInputDragOperationLink:
        return "move+link";
    case RoboInputDragOperationAll:
        return "copy+move+link";
    default:
        return "none";
    }
}

static const char *const RiDragPhaseNames[RoboInputDragPhaseCount] = {
    "idle", "dragging", "dropped", "cancelled"
};

int InputDragPhaseIsKnown(InputDragPhase phase)
{
    return (int)phase >= 0 && (size_t)phase < (size_t)RoboInputDragPhaseCount;
}

const char *InputDragPhaseName(InputDragPhase phase)
{
    if (!InputDragPhaseIsKnown(phase)) {
        return "unknown";
    }
    return RiDragPhaseNames[(size_t)phase];
}

/* ============================================================================================== *
 * The payload
 * ============================================================================================== */

void InputDragPayloadInit(InputDragPayload *payload)
{
    if (!payload) {
        return;
    }
    memset(payload, 0, sizeof(*payload));
}

Result InputDragPayloadSetText(InputDragPayload *payload,
                               const char *text,
                               size_t length,
                               size_t *outLength,
                               InputDiag *diag)
{
    if (outLength) {
        *outLength = length;
    }
    if (!payload) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a payload is required");
    }
    if (!text && length > 0u) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "text of a non-zero length is required");
    }
    if (length >= RoboInputDragTextMax) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboNotFound,
                             "the preview does not fit the payload's buffer");
    }
    memset(payload->text, 0, sizeof(payload->text));
    if (length > 0u) {
        memcpy(payload->text, text, length);
        payload->text[length] = '\0';
        payload->formats |= (uint32_t)1u << (uint32_t)RoboInputClipboardFormatPlainText;
    }
    payload->textLength = length;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

int InputDragPayloadIsValid(const InputDragPayload *payload)
{
    if (!payload) {
        return 0;
    }
    if (payload->textLength >= RoboInputDragTextMax) {
        return 0;
    }
    if (payload->text[payload->textLength] != '\0') {
        return 0;
    }
    /*
     * A format bit this build defines is `index < FormatCount`, so the defined set is the low bits and
     * anything outside them is a payload whose formats a caller could not name. Getting this mask wrong is
     * not a cosmetic mistake: a plain-text payload sets bit `PlainText`, and a mask that excluded it would
     * refuse every drag that carries a preview.
     */
    if ((payload->formats & ~(((uint32_t)1u << RoboInputClipboardFormatCount) - 1u)) != 0u) {
        return 0;
    }
    return 1;
}

void InputDragEventInit(InputDragEvent *event)
{
    if (!event) {
        return;
    }
    memset(event, 0, sizeof(*event));
    event->phase = RoboInputDragIdle;
    InputTargetInit(&event->target);
    InputDragPayloadInit(&event->payload);
    event->space = RoboInputSpaceUnknown;
}

uint32_t InputDragEventEffect(const InputDragEvent *event)
{
    if (!event) {
        return 0u;
    }
    /* What both sides agree on. A drop carries the *effect* the target performed, which is why the two fields
     * are separate rather than one "operation". */
    return event->offered & event->accepted;
}

/* ============================================================================================== *
 * The session
 * ============================================================================================== */

void InputDragSessionInit(InputDragSession *session)
{
    if (!session) {
        return;
    }
    memset(session, 0, sizeof(*session));
    session->phase = RoboInputDragIdle;
    InputTargetInit(&session->source);
    InputTargetInit(&session->over);
    InputDragPayloadInit(&session->payload);
}

int InputDragSessionIsActive(const InputDragSession *session)
{
    return session && session->phase == RoboInputDragDragging ? 1 : 0;
}

Result InputDragSessionBegin(InputDragSession *session,
                             const InputTarget *source,
                             uint32_t offered,
                             const InputDragPayload *payload,
                             InputDiag *diag)
{
    if (!session) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a session is required");
    }
    if (session->phase == RoboInputDragDragging) {
        /* A second session for one pointer would need a second payload, and there is only one. */
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboAlreadyExists,
                             "a drag is already running");
    }
    if (!InputTargetIsValid(source)) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a drag begins at a valid source");
    }
    if (!InputDragOperationIsKnown(offered)) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a drag offers at least one defined operation");
    }
    if (!InputDragPayloadIsValid(payload)) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a drag carries a payload this build may hold");
    }

    session->phase = RoboInputDragDragging;
    session->source = *source;
    InputTargetInit(&session->over);
    session->hasOver = 0;
    session->offered = offered;
    session->accepted = 0u;
    session->payload = *payload;
    session->acceptedTargets = 0u;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

/** Fill @p out with the session's common half. */
static void RiDragFill(const InputDragSession *session,
                       InputDragEvent *out,
                       InputDragPhase phase,
                       const InputTarget *target,
                       GfxPoint position,
                       InputSpace space,
                       int entered)
{
    InputDragEventInit(out);
    out->phase = phase;
    if (target) {
        out->target = *target;
    }
    out->position = position;
    out->space = space;
    out->offered = session->offered;
    out->accepted = session->accepted;
    out->payload = session->payload;
    out->entered = entered;
}

Result InputDragSessionMove(InputDragSession *session,
                            const InputTarget *target,
                            uint32_t accepted,
                            GfxPoint position,
                            InputSpace space,
                            InputDragEvent *out,
                            InputDiag *diag)
{
    InputDragPhase phase;
    int changed;

    if (!session) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a session is required");
    }
    if (session->phase != RoboInputDragDragging) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a drag-over is only meaningful while a drag is running");
    }
    if (accepted != 0u && !InputDragOperationIsKnown(accepted)) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a target answers with a defined operation or none");
    }
    if (!InputSpaceIsKnown(space)) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a drag-over needs a known plane");
    }

    /*
     * "Entered" is only claimed for a **real** target: a move onto nothing from nothing is not an arrival,
     * and counting it would make `acceptedTargets` a count of moves rather than of targets.
     */
    changed = InputTargetIsValid(target) && (!session->hasOver || !InputTargetEqual(&session->over, target));
    /* A move to a **new** target is a `DragEnter` and a move on the same one is a `DragOver`: same phase, same
     * payload, and the difference is the one field the event carries rather than a decision each listener
     * would have to reconstruct. */
    phase = RoboInputDragDragging;
    if (changed) {
        session->acceptedTargets++;
    }
    if (target) {
        session->over = *target;
        session->hasOver = 1;
    } else {
        InputTargetInit(&session->over);
        session->hasOver = 0;
    }
    session->accepted = accepted;

    if (out) {
        RiDragFill(session, out, phase, target, position, space, changed);
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputDragSessionLeave(InputDragSession *session, InputDragEvent *out, InputDiag *diag)
{
    InputTarget previous;

    if (!session) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a session is required");
    }
    if (session->phase != RoboInputDragDragging) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a drag-leave is only meaningful while a drag is running");
    }
    previous = session->over;
    if (out) {
        /* The position is the last one the leave knows: the pointer has not moved, it has *left*. A caller
         * that needs the exact point has it from the pointer event that caused the leave. */
        RiDragFill(session, out, RoboInputDragDragging, &previous, MakeGfxPoint(0, 0),
                   session->hasOver ? session->over.space : RoboInputSpaceWindow, 0);
    }
    InputTargetInit(&session->over);
    session->hasOver = 0;
    session->accepted = 0u;
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputDragSessionDrop(InputDragSession *session,
                            GfxPoint position,
                            InputDragEvent *out,
                            InputDiag *diag)
{
    if (!session) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a session is required");
    }
    if (session->phase != RoboInputDragDragging) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a drop is only meaningful while a drag is running");
    }
    /* The negotiation, checked in one place: nothing under the pointer, or nothing it would accept, means
     * there is no drop. A drop that performed nothing is not a thing a caller can act on, so the honest
     * answer is `RoboNotFound` and the caller's `Cancel`. */
    if (!session->hasOver || (session->offered & session->accepted) == 0u) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboNotFound,
                             "no target would accept the drop");
    }

    session->phase = RoboInputDragDropped;
    session->drops++;
    if (out) {
        RiDragFill(session, out, RoboInputDragDropped, &session->over, position,
                   session->over.space, 0);
        out->dropped = 1;
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

Result InputDragSessionCancel(InputDragSession *session, InputDragEvent *out, InputDiag *diag)
{
    if (!session) {
        return RoboInputDiag(diag, RoboInputDomainDragDrop, RoboInvalidArgument,
                             "a session is required");
    }
    if (session->phase != RoboInputDragDragging) {
        /* Cancelling something that is not running changes nothing, and that is `RoboOk`: a caller wiring
         * `Escape` to this must not have to check first. */
        if (diag) {
            InputDiagReset(diag);
        }
        return RoboOk;
    }
    session->phase = RoboInputDragCancelled;
    session->cancels++;
    if (out) {
        RiDragFill(session, out, RoboInputDragCancelled,
                   session->hasOver ? &session->over : &session->source,
                   MakeGfxPoint(0, 0),
                   session->hasOver ? session->over.space : RoboInputSpaceWindow, 0);
    }
    if (diag) {
        InputDiagReset(diag);
    }
    return RoboOk;
}

InputDragPhase InputDragSessionReset(InputDragSession *session)
{
    InputDragPhase was;

    if (!session) {
        return RoboInputDragIdle;
    }
    was = session->phase;
    session->phase = RoboInputDragIdle;
    session->hasOver = 0;
    session->accepted = 0u;
    session->offered = 0u;
    InputTargetInit(&session->over);
    InputDragPayloadInit(&session->payload);
    return was;
}

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

Result InputDragSessionWrite(const InputDragSession *session,
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
        RiWriterText(&text, "drag <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "drag phase=");
    RiWriterText(&text, InputDragPhaseName(session->phase));
    RiWriterFieldText(&text, " source", InputTargetKindName(session->source.kind));
    RiWriterFieldText(&text, " over",
                      session->hasOver ? InputTargetKindName(session->over.kind) : "none");
    RiWriterFieldText(&text, " offered", InputDragOperationName(session->offered));
    RiWriterFieldText(&text, " accepted", InputDragOperationName(session->accepted));
    RiWriterFieldU64(&text, " bytes", (uint64_t)session->payload.textLength);
    RiWriterFieldU64(&text, " targets", (uint64_t)session->acceptedTargets);
    RiWriterFieldU64(&text, " drops", (uint64_t)session->drops);
    RiWriterFieldU64(&text, " cancels", (uint64_t)session->cancels);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
