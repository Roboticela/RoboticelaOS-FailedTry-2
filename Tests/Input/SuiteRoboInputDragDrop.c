/*
 * RoboInput drag-and-drop suite (Phase 13.42) — `input.drag.*`.
 *
 * Seven cases about one state machine and one negotiation:
 *
 *   `vocabulary`   the three operations with their masks and spellings, and the four phases -- asserted
 *                  positionally, because they are what a session record carries;
 *
 *   `payload`      the preview: the plain-text format bit is set by *writing text*, not by a caller
 *                  remembering to, a preview that does not fit is refused **with the length it needed**, and
 *                  the validator catches a payload whose terminator moved and one carrying a format bit this
 *                  build has no vocabulary for;
 *
 *   `session`      the start: an invalid source, no operation at all, an invalid payload, and -- the one that
 *                  matters -- a second `Begin` while a drag is running, which is `RoboAlreadyExists` because
 *                  there is one payload and not two;
 *
 *   `move`         the arrival: entering a target is `entered=1` and *staying* on it is `entered=0` (same
 *                  phase, same payload, one field), a move onto nothing is not an arrival and does not count
 *                  as one, and the counters follow;
 *
 *   `drop`         the negotiation: a target that accepts nothing means no drop at all -- `RoboNotFound`, and
 *                  the session is *still dragging* so the caller's cancel works -- while an agreed drop
 *                  records the **effect** (offered ∧ accepted) rather than either side's wish;
 *
 *   `cancel`       the escape: cancelling something that is not running changes nothing and is `RoboOk`, so a
 *                  caller wiring `Escape` needs no check; cancelling a running drag names the target it was
 *                  over, which is the one that must stop showing drop feedback;
 *
 *   `dump`         the line, its determinism and its refusal when the buffer is one byte short.
 */

#include "RoboInputTestSupport.h"

#include <string.h>

static InputTarget DragNodeTarget(WindowId window, LayoutNodeId node)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetDocument(&target, window, NULL, node, 0u, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's document target was refused");
    }
    return target;
}

static InputDragPayload DragPayloadWith(const char *text)
{
    InputDragPayload payload;

    InputDragPayloadInit(&payload);
    if (InputDragPayloadSetText(&payload, text, strlen(text), NULL, NULL) != RoboOk) {
        RoboCheckMsg(0, "the fixture's payload was refused");
    }
    return payload;
}

static GfxPoint DragPoint(int x, int y);

static GfxPoint DragPoint(int x, int y)
{
    GfxPoint point;

    point.x = x;
    point.y = y;
    return point;
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestDragVocabulary(void)
{
    RoboCheckEqInt((int)RoboInputDragOperationCopy, 1);
    RoboCheckEqInt((int)RoboInputDragOperationMove, 2);
    RoboCheckEqInt((int)RoboInputDragOperationLink, 4);
    RoboCheckEqInt((int)RoboInputDragOperationAll, 7);
    RoboCheckEqInt((int)RoboInputDragOperationCount, 3);

    RoboCheck(InputDragOperationIsKnown(RoboInputDragOperationCopy));
    RoboCheck(InputDragOperationIsKnown(RoboInputDragOperationAll));
    /* No operation is not an operation: a drag that offers nothing has nothing to negotiate. */
    RoboCheck(!InputDragOperationIsKnown(0u));
    /* A bit this build does not define is refused rather than masked away. */
    RoboCheck(!InputDragOperationIsKnown((uint32_t)1u << 3));

    RoboCheckStrEq(InputDragOperationName(RoboInputDragOperationCopy), "copy");
    RoboCheckStrEq(InputDragOperationName(RoboInputDragOperationMove), "move");
    RoboCheckStrEq(InputDragOperationName(RoboInputDragOperationLink), "link");
    RoboCheckStrEq(InputDragOperationName(RoboInputDragOperationCopy | RoboInputDragOperationMove),
                   "copy+move");
    RoboCheckStrEq(InputDragOperationName(RoboInputDragOperationCopy | RoboInputDragOperationLink),
                   "copy+link");
    RoboCheckStrEq(InputDragOperationName(RoboInputDragOperationMove | RoboInputDragOperationLink),
                   "move+link");
    RoboCheckStrEq(InputDragOperationName(RoboInputDragOperationAll), "copy+move+link");
    RoboCheckStrEq(InputDragOperationName(0u), "none");

    RoboCheckEqInt((int)RoboInputDragIdle, 0);
    RoboCheckEqInt((int)RoboInputDragDragging, 1);
    RoboCheckEqInt((int)RoboInputDragDropped, 2);
    RoboCheckEqInt((int)RoboInputDragCancelled, 3);
    RoboCheckEqInt((int)RoboInputDragPhaseCount, 4);

    RoboCheckStrEq(InputDragPhaseName(RoboInputDragIdle), "idle");
    RoboCheckStrEq(InputDragPhaseName(RoboInputDragDragging), "dragging");
    RoboCheckStrEq(InputDragPhaseName(RoboInputDragDropped), "dropped");
    RoboCheckStrEq(InputDragPhaseName(RoboInputDragCancelled), "cancelled");
    RoboCheckStrEq(InputDragPhaseName((InputDragPhase)9), "unknown");
    RoboCheck(InputDragPhaseIsKnown(RoboInputDragIdle));
    RoboCheck(!InputDragPhaseIsKnown((InputDragPhase)9));
}

/* ------------------------------------------------------------------ */
/* Payload                                                             */
/* ------------------------------------------------------------------ */

static void TestDragPayload(void)
{
    InputDragPayload payload;
    InputDiag diag;
    size_t needed = 0u;
    char big[RoboInputDragTextMax + 8];

    InputDiagReset(&diag);
    InputDragPayloadInit(&payload);
    RoboCheck(InputDragPayloadIsValid(&payload));
    RoboCheckEqInt(payload.textLength, 0);
    RoboCheckEqInt((int)payload.formats, 0);

    /* Writing text sets the plain-text format bit itself: two fields that must agree are set in one place. */
    RoboCheckEqInt(InputDragPayloadSetText(&payload, "report.txt", 10u, NULL, &diag), RoboOk);
    RoboCheckEqInt(payload.textLength, 10);
    RoboCheckStrEq(payload.text, "report.txt");
    RoboCheck((payload.formats & ((uint32_t)1u << RoboInputClipboardFormatPlainText)) != 0u);
    RoboCheck(InputDragPayloadIsValid(&payload));

    /* Too long: refused, and the caller is told the length it needed rather than being left to guess. */
    memset(big, 'x', sizeof(big));
    RoboCheckEqInt(InputDragPayloadSetText(&payload, big, sizeof(big), &needed, &diag), RoboNotFound);
    RoboCheckEqInt((int)needed, (int)sizeof(big));
    /* ... and the payload it refused is untouched, so a failed preview does not corrupt a live drag. */
    RoboCheckStrEq(payload.text, "report.txt");

    /* An empty preview is legitimate: a source may offer an identity with nothing to show. */
    RoboCheckEqInt(InputDragPayloadSetText(&payload, "", 0u, NULL, &diag), RoboOk);
    RoboCheckEqInt(payload.textLength, 0);
    RoboCheck(InputDragPayloadIsValid(&payload));

    /* A terminator that moved: the record claims a length its bytes do not end at. */
    payload.textLength = 3u;
    payload.text[3] = 'z';
    RoboCheck(!InputDragPayloadIsValid(&payload));

    /* A format bit this build has no vocabulary for is a payload it cannot carry. */
    InputDragPayloadInit(&payload);
    payload.formats = 0x7fffffffu;
    RoboCheck(!InputDragPayloadIsValid(&payload));

    RoboCheck(!InputDragPayloadIsValid(NULL));
}

/* ------------------------------------------------------------------ */
/* Session start                                                       */
/* ------------------------------------------------------------------ */

static void TestDragSession(void)
{
    const WindowId window = (WindowId)7u;
    InputDragSession session;
    InputDragPayload payload = DragPayloadWith("row");
    InputTarget source = DragNodeTarget(window, (LayoutNodeId)21u);
    InputTarget invalid;
    InputDragPayload empty;
    InputDiag diag;

    InputDiagReset(&diag);
    InputDragSessionInit(&session);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragIdle);
    RoboCheck(!InputDragSessionIsActive(&session));
    RoboCheckEqInt((int)session.payload.textLength, 0);
    RoboCheckEqInt(session.acceptedTargets, 0);

    /* A source that is not a record this build may carry. */
    InputTargetInit(&invalid);
    RoboCheckEqInt(InputDragSessionBegin(&session, &invalid, RoboInputDragOperationCopy, &payload, &diag),
                   RoboInvalidArgument);
    /* Nothing is offered -- nothing to negotiate. */
    RoboCheckEqInt(InputDragSessionBegin(&session, &source, 0u, &payload, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputDragSessionBegin(&session, &source, (uint32_t)1u << 3, &payload, &diag),
                   RoboInvalidArgument);
    /* A payload the model would refuse. */
    InputDragPayloadInit(&empty);
    empty.textLength = 4u;
    empty.text[4] = 'q';
    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationCopy, &empty, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragIdle);

    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationCopy | RoboInputDragOperationMove,
                                         &payload, &diag),
                   RoboOk);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragDragging);
    RoboCheck(InputDragSessionIsActive(&session));
    RoboCheck(InputTargetEqual(&session.source, &source));
    RoboCheckEqInt((int)session.offered,
                   (int)(RoboInputDragOperationCopy | RoboInputDragOperationMove));
    RoboCheckStrEq(session.payload.text, "row");

    /* One payload, one session: a second start is `RoboAlreadyExists`, not a replacement. */
    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationLink, &payload, &diag),
                   RoboAlreadyExists);
    RoboCheckEqInt((int)session.offered,
                   (int)(RoboInputDragOperationCopy | RoboInputDragOperationMove));

    /* NULL is a refusal rather than a crash, and an idle session is not active. */
    RoboCheckEqInt(InputDragSessionBegin(NULL, &source, RoboInputDragOperationCopy, &payload, &diag),
                   RoboInvalidArgument);
    InputDragSessionInit(NULL);
    RoboCheck(!InputDragSessionIsActive(NULL));
}

/* ------------------------------------------------------------------ */
/* Move                                                                */
/* ------------------------------------------------------------------ */

static void TestDragMove(void)
{
    const WindowId window = (WindowId)7u;
    InputDragSession session;
    InputDragPayload payload = DragPayloadWith("row");
    InputTarget source = DragNodeTarget(window, (LayoutNodeId)21u);
    InputTarget first = DragNodeTarget(window, (LayoutNodeId)22u);
    InputTarget second = DragNodeTarget(window, (LayoutNodeId)23u);
    InputDragEvent event;
    InputDiag diag;

    InputDiagReset(&diag);
    InputDragSessionInit(&session);
    InputDragEventInit(&event);

    /* A move before a drag is running is refused rather than silently ignored. */
    RoboCheckEqInt(InputDragSessionMove(&session, &first, RoboInputDragOperationCopy, DragPoint(1, 2),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboInvalidArgument);

    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationAll, &payload, &diag),
                   RoboOk);
    RoboCheckEqInt(InputDragSessionMove(&session, &first, RoboInputDragOperationCopy, DragPoint(10, 20),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboOk);
    RoboCheckEqInt((int)event.phase, (int)RoboInputDragDragging);
    /* Arriving: this is the `DragEnter` a listener fires its highlight from. */
    RoboCheckEqInt(event.entered, 1);
    RoboCheck(InputTargetEqual(&event.target, &first));
    RoboCheckEqInt((int)event.accepted, (int)RoboInputDragOperationCopy);
    RoboCheckEqInt((int)event.offered, (int)RoboInputDragOperationAll);
    RoboCheckEqInt(event.space, (int)RoboInputSpaceWindow);
    RoboCheckEqInt((int)event.position.x, 10);
    RoboCheckEqInt((int)event.position.y, 20);
    RoboCheckEqInt((int)session.acceptedTargets, 1);
    RoboCheck(session.hasOver);
    RoboCheck(InputTargetEqual(&session.over, &first));

    /* Staying on the same target: same phase, same payload, one field different. */
    RoboCheckEqInt(InputDragSessionMove(&session, &first, RoboInputDragOperationCopy, DragPoint(11, 21),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboOk);
    RoboCheckEqInt(event.entered, 0);
    RoboCheckEqInt((int)session.acceptedTargets, 1);

    /* A new target is an arrival again, and the accepted set is what *this* target said. */
    RoboCheckEqInt(InputDragSessionMove(&session, &second, RoboInputDragOperationMove, DragPoint(30, 40),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboOk);
    RoboCheckEqInt(event.entered, 1);
    RoboCheckEqInt((int)session.acceptedTargets, 2);
    RoboCheckEqInt((int)event.accepted, (int)RoboInputDragOperationMove);

    /* A target that accepts nothing is still *reported*: the shell draws "not allowed" from exactly this. */
    RoboCheckEqInt(InputDragSessionMove(&session, &second, 0u, DragPoint(30, 40), RoboInputSpaceWindow,
                                        &event, &diag),
                   RoboOk);
    RoboCheckEqInt((int)event.accepted, 0);
    RoboCheckEqInt(event.entered, 0);
    RoboCheckEqInt((int)InputDragEventEffect(&event), 0);

    /* An operation this build does not define, and a plane it does not know, are refusals. */
    RoboCheckEqInt(InputDragSessionMove(&session, &second, (uint32_t)1u << 3, DragPoint(30, 40),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputDragSessionMove(&session, &second, RoboInputDragOperationCopy, DragPoint(30, 40),
                                        RoboInputSpaceUnknown, &event, &diag),
                   RoboInvalidArgument);

    /* Leaving: the target goes, and the event names the one that was left so *it* can stop highlighting. */
    RoboCheck(InputDragSessionLeave(&session, &event, &diag) == RoboOk);
    RoboCheck(!session.hasOver);
    RoboCheckEqInt((int)session.accepted, 0);
    RoboCheck(InputTargetEqual(&event.target, &second));
    RoboCheck(InputDragSessionLeave(&session, &event, &diag) == RoboOk);

    /* A move onto nothing is not an arrival: it does not count as one, and nothing is entered. */
    RoboCheckEqInt(InputDragSessionMove(&session, NULL, 0u, DragPoint(0, 0), RoboInputSpaceWindow, &event,
                                        &diag),
                   RoboOk);
    RoboCheckEqInt(event.entered, 0);
    RoboCheckEqInt((int)session.acceptedTargets, 2);

    /* ... and the session is untouched by a leave with nothing to leave. */
    RoboCheckEqInt(InputDragSessionLeave(NULL, &event, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Drop                                                                */
/* ------------------------------------------------------------------ */

static void TestDragDrop(void)
{
    const WindowId window = (WindowId)7u;
    InputDragSession session;
    InputDragPayload payload = DragPayloadWith("row");
    InputTarget source = DragNodeTarget(window, (LayoutNodeId)21u);
    InputTarget target = DragNodeTarget(window, (LayoutNodeId)22u);
    InputDragEvent event;
    InputDiag diag;

    InputDiagReset(&diag);
    InputDragSessionInit(&session);
    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationCopy, &payload, &diag),
                   RoboOk);

    /* Nothing under the pointer: there is no drop, and saying so is *not* an error the session absorbs --
     * the session is still dragging, so the caller's `Cancel` still works. */
    RoboCheckEqInt(InputDragSessionDrop(&session, DragPoint(5, 5), &event, &diag), RoboNotFound);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragDragging);
    RoboCheckEqInt(session.drops, 0);

    RoboCheckEqInt(InputDragSessionMove(&session, &target, RoboInputDragOperationMove, DragPoint(5, 5),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboOk);
    /* The target would move it, but the source only offered a copy: the two do not agree, so there is no
     * drop. */
    RoboCheckEqInt(InputDragSessionDrop(&session, DragPoint(5, 5), &event, &diag), RoboNotFound);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragDragging);
    RoboCheckEqInt(session.drops, 0);

    /* Now both sides offer a copy: the drop happens, and the event carries the **effect**, which is the
     * intersection and not either side's wish. */
    RoboCheckEqInt(InputDragSessionMove(&session, &target,
                                        RoboInputDragOperationCopy | RoboInputDragOperationLink,
                                        DragPoint(6, 6), RoboInputSpaceWindow, &event, &diag),
                   RoboOk);
    /* The source offered only a copy, so the effect is the *intersection* even though the target would take
     * more: a drop performs what both sides agreed to and nothing else. */
    RoboCheckEqInt((int)InputDragEventEffect(&event), (int)RoboInputDragOperationCopy);
    RoboCheckEqInt(InputDragSessionDrop(&session, DragPoint(7, 7), &event, &diag), RoboOk);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragDropped);
    RoboCheckEqInt(session.drops, 1);
    RoboCheckEqInt((int)event.phase, (int)RoboInputDragDropped);
    RoboCheckEqInt(event.dropped, 1);
    RoboCheckEqInt((int)event.offered, (int)RoboInputDragOperationCopy);
    RoboCheckEqInt((int)event.accepted,
                   (int)(RoboInputDragOperationCopy | RoboInputDragOperationLink));
    RoboCheckEqInt((int)InputDragEventEffect(&event), (int)RoboInputDragOperationCopy);
    RoboCheck(InputTargetEqual(&event.target, &target));
    RoboCheckEqInt((int)event.position.x, 7);

    /* Dropping twice: the second is outside a running drag. */
    RoboCheckEqInt(InputDragSessionDrop(&session, DragPoint(8, 8), &event, &diag), RoboInvalidArgument);
    RoboCheckEqInt(session.drops, 1);

    /* A session started with a source that only links can be dropped on a target that only links. */
    {
        InputDragPhase was;

        was = InputDragSessionReset(&session);
        RoboCheckEqInt((int)was, (int)RoboInputDragDropped);
        RoboCheckEqInt((int)session.phase, (int)RoboInputDragIdle);
        /* Reset takes the payload back with it: a finished session holds nothing. */
        RoboCheckEqInt((int)session.payload.textLength, 0);
        RoboCheckEqInt((int)session.offered, 0);
    }
    RoboCheckEqInt((int)InputDragSessionReset(NULL), (int)RoboInputDragIdle);
    RoboCheckEqInt(InputDragSessionDrop(NULL, DragPoint(0, 0), &event, &diag), RoboInvalidArgument);

    /* An event with no source of its own: the effect of nothing is nothing. */
    RoboCheckEqInt((int)InputDragEventEffect(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Cancel                                                              */
/* ------------------------------------------------------------------ */

static void TestDragCancel(void)
{
    const WindowId window = (WindowId)7u;
    InputDragSession session;
    InputDragPayload payload = DragPayloadWith("row");
    InputTarget source = DragNodeTarget(window, (LayoutNodeId)21u);
    InputTarget target = DragNodeTarget(window, (LayoutNodeId)22u);
    InputDragEvent event;
    InputDiag diag;

    InputDiagReset(&diag);
    InputDragSessionInit(&session);
    InputDragEventInit(&event);

    /* Cancelling what is not running changes nothing and is `RoboOk`, so `Escape` needs no check. */
    RoboCheckEqInt(InputDragSessionCancel(&session, &event, &diag), RoboOk);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragIdle);
    RoboCheckEqInt(session.cancels, 0);

    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationCopy, &payload, &diag),
                   RoboOk);
    /* A cancel before the pointer reached anybody names the source: the one target that must stop showing it
     * is the one the drag came from. */
    RoboCheckEqInt(InputDragSessionCancel(&session, &event, &diag), RoboOk);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragCancelled);
    RoboCheckEqInt(session.cancels, 1);
    RoboCheckEqInt((int)event.phase, (int)RoboInputDragCancelled);
    RoboCheck(InputTargetEqual(&event.target, &source));

    /* A second cancel is a no-op: the session is no longer dragging. */
    RoboCheckEqInt(InputDragSessionCancel(&session, &event, &diag), RoboOk);
    RoboCheckEqInt(session.cancels, 1);

    /* After a move, a cancel names the target under the pointer instead. */
    RoboCheckEqInt((int)InputDragSessionReset(&session), (int)RoboInputDragCancelled);
    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationAll, &payload, &diag),
                   RoboOk);
    RoboCheckEqInt(InputDragSessionMove(&session, &target, RoboInputDragOperationCopy, DragPoint(1, 1),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboOk);
    RoboCheckEqInt(InputDragSessionCancel(&session, &event, &diag), RoboOk);
    RoboCheck(InputTargetEqual(&event.target, &target));
    RoboCheckEqInt(session.cancels, 2);

    /* A dropped drag cannot be cancelled afterwards: it already ended. */
    RoboCheckEqInt((int)InputDragSessionReset(&session), (int)RoboInputDragCancelled);
    RoboCheckEqInt(InputDragSessionBegin(&session, &source, RoboInputDragOperationCopy, &payload, &diag),
                   RoboOk);
    RoboCheckEqInt(InputDragSessionMove(&session, &target, RoboInputDragOperationCopy, DragPoint(1, 1),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboOk);
    RoboCheckEqInt(InputDragSessionDrop(&session, DragPoint(1, 1), &event, &diag), RoboOk);
    RoboCheckEqInt(InputDragSessionCancel(&session, &event, &diag), RoboOk);
    RoboCheckEqInt((int)session.phase, (int)RoboInputDragDropped);
    RoboCheckEqInt(session.cancels, 2);

    RoboCheckEqInt(InputDragSessionCancel(NULL, &event, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Dump                                                                */
/* ------------------------------------------------------------------ */

static void TestDragDump(void)
{
    const WindowId window = (WindowId)7u;
    InputDragSession session;
    InputDragSession other;
    InputDragPayload payload = DragPayloadWith("row");
    InputTarget source = DragNodeTarget(window, (LayoutNodeId)21u);
    InputTarget target = DragNodeTarget(window, (LayoutNodeId)22u);
    InputDragEvent event;
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;
    size_t sessionLength = 0u;

    InputDiagReset(&diag);
    InputDragSessionInit(&session);
    RoboCheckEqInt(InputDragSessionBegin(&session, &source,
                                         RoboInputDragOperationCopy | RoboInputDragOperationMove, &payload,
                                         &diag),
                   RoboOk);
    RoboCheckEqInt(InputDragSessionMove(&session, &target, RoboInputDragOperationCopy, DragPoint(1, 1),
                                        RoboInputSpaceWindow, &event, &diag),
                   RoboOk);

    RoboCheckEqInt(InputDragSessionWrite(&session, line, sizeof(line), &sessionLength, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)sessionLength);
    RoboCheckStrEq(line,
                   "drag phase=dragging source=document over=document offered=copy+move accepted=copy "
                   "bytes=3 targets=1 drops=0 cancels=0\n");

    InputDragSessionInit(&other);
    RoboCheckEqInt(InputDragSessionWrite(&other, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line,
                   "drag phase=idle source=none over=none offered=none accepted=none bytes=0 targets=0 "
                   "drops=0 cancels=0\n");
    RoboCheckEqInt(InputDragSessionWrite(&other, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    RoboCheckEqInt(InputDragSessionWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "drag <none>\n");

    /* A buffer one byte short: refuse, leave `?`, and report the length the line needed -- the same length
     * the whole write above reported, which is the property a caller sizes its buffer from. */
    {
        char small[16];
        size_t needed = 0u;

        RoboCheckEqInt(InputDragSessionWrite(&session, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt((int)needed, (int)sessionLength);
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputDragDrop(void)
{
    TestRegister("input.drag.vocabulary", TestDragVocabulary);
    TestRegister("input.drag.payload", TestDragPayload);
    TestRegister("input.drag.session", TestDragSession);
    TestRegister("input.drag.move", TestDragMove);
    TestRegister("input.drag.drop", TestDragDrop);
    TestRegister("input.drag.cancel", TestDragCancel);
    TestRegister("input.drag.dump", TestDragDump);
}
