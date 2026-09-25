/*
 * Drag and drop (Phase 13.42).
 *
 * ## A session, not a gesture
 *
 * A drag begins at a *source* -- a target that offered data -- travels over targets that may or may not accept
 * it, and ends either as a **drop** on one of them or as a **cancel** anywhere. The thing that makes it more
 * than a gesture is that the *data* travels with it and the *operation* is negotiated: the source offers copy,
 * move, link or some combination, and each target under the pointer answers with what it will accept. A drop
 * happens only where the two agree, which is why `RoboInputDragSession::offered` and `…accepted` are separate
 * fields and not one.
 *
 * ## The state machine, and its refusals
 *
 *     Idle --Begin--> Dragging --DragOver--> Dragging --Drop--> Dropped --> reset
 *                        |                     |
 *                        +-------Cancel--------+                            --> Cancelled --> reset
 *
 * A `DragOver` for a session that is not dragging, a `Drop` outside a session, a `Begin` while one is running:
 * each is a **refusal** (`RoboInvalidArgument`, or `RoboAlreadyExists` for the last), because a second session
 * for one pointer would need a second payload and there is only one.
 *
 * ## What it does not do
 *
 * It does not carry bytes. `RoboInputDragPayload` is an opaque 64-bit identity plus a text *preview*, and the
 * data behind the identity belongs to whoever produced it -- a document, a file list, an image. That is the
 * same seam the clipboard draws (`InputClipboardTextSource`) and for the same reason: this module has no
 * business owning a user's content.
 *
 * See `Documentation/Runtime/Input/DragDrop.md`.
 */

#ifndef ROBO_INPUT_DRAGDROP_H
#define ROBO_INPUT_DRAGDROP_H

#include "RoboInput/RoboInputClipboard.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How much preview text a payload carries. */
#define RoboInputDragTextMax 128u

/** What a drag may do with the data at the end. A bitmask: the source may offer several. */
#define RoboInputDragOperationCopy ((uint32_t)(1u << 0))
#define RoboInputDragOperationMove ((uint32_t)(1u << 1))
#define RoboInputDragOperationLink ((uint32_t)(1u << 2))

/** Every operation this build defines. */
#define RoboInputDragOperationAll \
    (RoboInputDragOperationCopy | RoboInputDragOperationMove | RoboInputDragOperationLink)

/** Number of operations this build defines. */
#define RoboInputDragOperationCount 3u

/** Non-zero when @p operation is a bit this build defines. */
int InputDragOperationIsKnown(uint32_t operation);

/** Human-readable list of @p operations (`"copy+move"`); never NULL; `"none"` for 0. */
const char *InputDragOperationName(uint32_t operations);

/** Where a drag is in its life. */
typedef enum InputDragPhase {
    /** No session. */
    RoboInputDragIdle = 0,
    /** The source is being held; the pointer is moving with the button down. */
    RoboInputDragDragging = 1,
    /** A drop happened. */
    RoboInputDragDropped = 2,
    /** The drag was cancelled -- `Escape`, a window change, a device disconnect. */
    RoboInputDragCancelled = 3
} InputDragPhase;

/** Number of defined phases. Asserted positionally by `input.drag.*`. */
#define RoboInputDragPhaseCount 4u

/** Non-zero if @p phase is a defined phase. */
int InputDragPhaseIsKnown(InputDragPhase phase);

/** Human-readable phase name; never NULL; `"unknown"` when undefined. */
const char *InputDragPhaseName(InputDragPhase phase);

/** What travels with a drag: an identity, a preview and the operations on offer. */
typedef struct InputDragPayload {
    /** An opaque identity the source owns. Never interpreted here. */
    uint64_t identity;
    /** Plain text preview, and the format it is in -- the drop target's own choice. */
    uint32_t formats;
    /** The preview, NUL-terminated. */
    char text[RoboInputDragTextMax];
    /** Bytes of @ref text, without the terminator. */
    size_t textLength;
} InputDragPayload;

/** Clear @p payload. Safe on NULL. */
void InputDragPayloadInit(InputDragPayload *payload);

/** Set the text preview. Refused when it does not fit, with the length it needed. */
Result InputDragPayloadSetText(InputDragPayload *payload,
                               const char *text,
                               size_t length,
                               size_t *outLength,
                               InputDiag *diag);

/** Non-zero when @p payload is a record this build may carry. */
int InputDragPayloadIsValid(const InputDragPayload *payload);

/** What a drag event says. */
typedef struct InputDragEvent {
    /** Where in the session's life it is. */
    InputDragPhase phase;
    /** The target it is about: the source for `Begin`, the one under the pointer otherwise. */
    InputTarget target;
    /** Where the pointer is. */
    GfxPoint position;
    /** The plane @ref position is in. */
    InputSpace space;
    /** What the **source** offers. */
    uint32_t offered;
    /** What the **target** would accept, once one has answered. Zero before that. */
    uint32_t accepted;
    /** The data, a preview of which travels with the drag. */
    InputDragPayload payload;
    /** Non-zero when this event is a `DROP` and the operation agreed. */
    int dropped;
    /**
     * Non-zero when this event **entered** @ref target rather than staying on it.
     *
     * The move door needs this because the two events a caller's listeners distinguish -- `DragEnter` and
     * `DragOver` -- are the same phase and the same payload; the only difference is whether the pointer just
     * arrived. Without a field for it a caller would have to remember the previous target itself, which is
     * the bookkeeping `InputDragSession::over` already does on its behalf.
     */
    int entered;
} InputDragEvent;

/** Clear @p event. Safe on NULL. */
void InputDragEventInit(InputDragEvent *event);

/** The **effective** operation of @p event: what the source offers and the target accepts. */
uint32_t InputDragEventEffect(const InputDragEvent *event);

/**
 * A drag, as one value.
 *
 * The `offered`/`accepted` pair is the negotiation, and `over` is the target currently under the pointer --
 * kept so the next move can tell "still over the same target" from "moved to a new one", which is the
 * difference between a `DragOver` and a `DragLeave` followed by a `DragEnter`.
 */
typedef struct InputDragSession {
    /** Where it is. */
    InputDragPhase phase;
    /** The target the drag began on. */
    InputTarget source;
    /** The target under the pointer, or an empty record. */
    InputTarget over;
    /** Non-zero when @ref over is live. */
    int hasOver;
    /** What the source offers. */
    uint32_t offered;
    /** What the target under the pointer would accept. */
    uint32_t accepted;
    /** The data. */
    InputDragPayload payload;
    /** How many targets have accepted so far. */
    uint32_t acceptedTargets;
    /** Drags that ended as a drop. */
    uint32_t drops;
    /** Drags that ended cancelled. */
    uint32_t cancels;
} InputDragSession;

/** Clear @p session to idle. Safe on NULL. */
void InputDragSessionInit(InputDragSession *session);

/** Non-zero when a drag is running. */
int InputDragSessionIsActive(const InputDragSession *session);

/**
 * Begin a drag from @p source with @p payload and @p offered.
 *
 * Refused with @ref RoboAlreadyExists while one is running: a second session for one pointer would need a
 * second payload, and there is only one.
 */
Result InputDragSessionBegin(InputDragSession *session,
                             const InputTarget *source,
                             uint32_t offered,
                             const InputDragPayload *payload,
                             InputDiag *diag);

/**
 * Move the drag over @p target, which would accept @p accepted.
 *
 * @p out (may be NULL) receives the event the caller must dispatch: a `DragEnter` when the target changed, a
 * `DragOver` when it did not. A target that accepts nothing is still reported -- the shell draws a
 * "not allowed" cursor from exactly that state.
 */
Result InputDragSessionMove(InputDragSession *session,
                            const InputTarget *target,
                            uint32_t accepted,
                            GfxPoint position,
                            InputSpace space,
                            InputDragEvent *out,
                            InputDiag *diag);

/** The pointer left the target it was over. @p out receives a `DragLeave`. */
Result InputDragSessionLeave(InputDragSession *session, InputDragEvent *out, InputDiag *diag);

/**
 * Drop on the target under the pointer.
 *
 * Refused with @ref RoboNotFound when no target would accept anything: a drop on a target that said no is a
 * cancel, and saying so is the caller's `Cancel`.
 */
Result InputDragSessionDrop(InputDragSession *session,
                            GfxPoint position,
                            InputDragEvent *out,
                            InputDiag *diag);

/** Cancel the drag. @p out receives a `DragCancel`. */
Result InputDragSessionCancel(InputDragSession *session, InputDragEvent *out, InputDiag *diag);

/** Take the payload back from a finished session and return to idle. Returns the phase it was in. */
InputDragPhase InputDragSessionReset(InputDragSession *session);

/** One complete line for @p session, or @ref RoboNotSupported when it does not fit. */
Result InputDragSessionWrite(const InputDragSession *session,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_DRAGDROP_H */
