/*
 * Hit testing, the event path, and dispatch (Phase 13.29--13.36).
 *
 * ## The seam
 *
 * RoboInput must route an event without knowing what a window tree, a document or a compositor is. So
 * everything this file needs from the layer above arrives as **one or two function pointers**:
 *
 *   a **hit-test provider**   answers "what is under this point?" (13.29), because the geometry, the
 *                             transforms, the clipping, the z-order and the pointer-events behaviour all live
 *                             in the tree that owns them;
 *   a **path provider**       answers "what is the chain of targets from the root down to this one?" (13.30),
 *                             because the parent links are the tree's;
 *   a **listener provider**   answers "who is listening on this target, for this event?" (13.31), because the
 *                             listener list is the caller's storage and this module allocates nothing.
 *
 * Each is optional, and each absent provider degrades to a *stated* degenerate case rather than to a
 * failure: with no hit-test provider a point resolves to its window; with no path provider the path is the
 * target alone; with no listener provider nothing is listening. That is what makes this layer testable
 * without a window system, and it is the same shape RoboLayout's font provider has (`LayoutFontProvider`).
 *
 * ## The order (13.31/13.32)
 *
 *     capture   root → … → parent      (a listener here sees the event *before* the target)
 *     target    the target itself
 *     bubble    parent → … → root      (a listener here sees it *after*)
 *
 * `stopPropagation` finishes the current target's listeners and then stops; `stopImmediatePropagation`
 * stops in the middle of them; `preventDefault` records that the default action must not happen, and is
 * **refused and counted** when a passive listener tries it (13.33). The dispatcher never mutates the event:
 * the propagation state is the context's, which is what "immutable events" means in practice.
 *
 * ## What dispatch is not (13.34)
 *
 * Dispatching and *acting* are separate. The dispatcher reports the `InputDefaultAction` the event asks for
 * -- scroll on a wheel, focus on a press, activate on `Enter`, a text command on a letter -- and the caller
 * performs it. A scroll needs a scroller, a focus change needs a focus manager and a command needs an
 * editor; none of the three belongs in the loop that walks a path.
 *
 * See `Documentation/Runtime/Input/Dispatch.md`.
 */

#ifndef ROBO_INPUT_DISPATCH_H
#define ROBO_INPUT_DISPATCH_H

#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest path this build routes: a shell, a window, a document, ancestors, and the target. */
#define RoboInputMaxEventPath 32u

/** Listeners a target may have for one event, as far as one dispatch is concerned. */
#define RoboInputMaxListeners 16u

/* ============================================================================================== *
 * Hit testing (13.29)
 * ============================================================================================== */

/** What is under a point. */
typedef struct InputHitTestResult {
    /** Non-zero when @ref target is an answer. */
    int hit;
    /** What the point resolved to. */
    InputTarget target;
    /** The plane the point was asked in, kept so a reader can tell a conversion from a pass-through. */
    InputSpace space;
    /** Non-zero when the point was outside everything and the answer is the window: a *reachable* miss. */
    int fellThrough;
} InputHitTestResult;

/**
 * The tree answers questions about its own geometry.
 *
 * `hitTest` fills @p outTarget with the target under @p point and returns @ref RoboOk, or @ref RoboNotFound
 * for "nothing here". A *failure* (@ref RoboInvalidArgument, @ref RoboOutOfMemory) propagates: it is not
 * "no hit", and reading it as one is how a broken provider becomes a silent misfeature.
 */
typedef struct InputHitTestProvider {
    /** Whatever the provider needs; this module never reads it. */
    void *userdata;
    /** Resolve @p point, given in @p space, to a target. */
    Result (*hitTest)(void *userdata,
                      const InputRoute *route,
                      GfxPoint point,
                      InputSpace space,
                      InputTarget *outTarget,
                      InputDiag *diag);
} InputHitTestProvider;

/**
 * Resolve @p point to a target.
 *
 * The point must be in a plane that can be converted *out of* -- `InputSpaceIsConvertible` -- because a
 * point whose relationship to the screen nobody knows cannot be tested against geometry that is in screen
 * space. The route supplies the geometry the conversion needs (12.1).
 *
 * With no provider (@p provider NULL or its `hitTest` NULL), the answer is the route's own window: a
 * window-only system has exactly one target under every point, and saying so is more useful than refusing.
 * That case sets @ref fellThrough, so a caller can tell it apart from a tree that answered.
 */
Result InputHitTestPoint(const InputHitTestProvider *provider,
                         const InputRoute *route,
                         GfxPoint point,
                         InputSpace space,
                         InputHitTestResult *out,
                         InputDiag *diag);

/* ============================================================================================== *
 * The event path (13.30/13.31)
 * ============================================================================================== */

/**
 * The chain an event travels, **root first, target last**.
 *
 * Stored in that order rather than target-first because the capture phase reads it forward and the bubble
 * phase reads it backward, and one of the two had to be a loop with a decrement. @ref depth is the index of
 * the target itself, which is what separates "an ancestor" from "the target".
 */
typedef struct InputEventPath {
    /** The chain, root first. */
    InputTarget targets[RoboInputMaxEventPath];
    /** Entries in the chain. At least 1 in a built path. */
    uint32_t count;
    /** Index of the target itself: `count - 1`. */
    uint32_t depth;
    /** Entries the provider wanted to add and the path had no room for. Reported, never silent. */
    uint32_t truncated;
} InputEventPath;

/** The tree answers questions about its own parent links. */
typedef struct InputPathProvider {
    /** Whatever the provider needs; this module never reads it. */
    void *userdata;
    /** Fill @p out with root → … → @p target. */
    Result (*buildPath)(void *userdata,
                        const InputTarget *target,
                        InputEventPath *out,
                        InputDiag *diag);
} InputPathProvider;

/** Clear @p path. Safe on NULL. */
void InputEventPathInit(InputEventPath *path);

/**
 * Build the path for @p target.
 *
 * With no provider the path is one entry -- the target -- which is the whole of a system whose only target
 * kind is a window. A provider that returns a path whose last entry is not @p target is **refused**: an
 * event that never reaches the target it was addressed to is a routing bug this door can catch for free.
 */
Result InputEventPathBuild(const InputPathProvider *provider,
                           const InputTarget *target,
                           InputEventPath *out,
                           InputDiag *diag);

/** Entry @p index, or NULL. */
const InputTarget *InputEventPathAt(const InputEventPath *path, uint32_t index);

/** The target itself, or NULL. */
const InputTarget *InputEventPathTarget(const InputEventPath *path);

/** One complete line for @p path, or @ref RoboNotSupported when it does not fit. */
Result InputEventPathWrite(const InputEventPath *path,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           InputDiag *diag);

/* ============================================================================================== *
 * Propagation (13.31/13.32)
 * ============================================================================================== */

/** Which half of a dispatch a listener is being called in. */
typedef enum InputPhase {
    /** Before the target: root down to its parent. */
    RoboInputPhaseCapture = 0,
    /** The target itself. */
    RoboInputPhaseTarget = 1,
    /** After the target: its parent up to the root. */
    RoboInputPhaseBubble = 2
} InputPhase;

/** Number of defined phases. Asserted positionally by `input.dispatch.*`. */
#define RoboInputPhaseCount 3u

/** Non-zero if @p phase is a defined phase. */
int InputPhaseIsKnown(InputPhase phase);

/** Human-readable phase name; never NULL; `"unknown"` when undefined. */
const char *InputPhaseName(InputPhase phase);

/**
 * Propagation state: what a listener may say about the rest of the dispatch.
 *
 * A listener is handed this and *only* this to mutate. The event is `const` on purpose: propagation is not a
 * property of the event, and a build that let a listener write to the event would make a recording of it
 * depend on who listened.
 */
typedef struct InputEventContext {
    /** Which phase is running. */
    InputPhase phase;
    /** Which target's listeners are being called. */
    InputTarget currentTarget;
    /** Non-zero once a listener asked for propagation to stop after this target. */
    int stopped;
    /** Non-zero once a listener asked for the rest of *this target's* listeners to be skipped too. */
    int stoppedImmediate;
    /** Non-zero once a listener asked for the default action not to happen. */
    int defaultPrevented;
    /** Non-zero while the listener being called is passive, so `preventDefault` must be refused. */
    int passive;
    /** Listeners called so far. */
    uint32_t calls;
    /** `preventDefault` calls refused because the listener was passive. Reported, not swallowed. */
    uint32_t refusedDefault;
} InputEventContext;

/** Clear @p context. Safe on NULL. */
void InputEventContextInit(InputEventContext *context);

/**
 * Stop propagation after this target's remaining listeners.
 *
 * Returns @ref RoboOk, or @ref RoboInvalidArgument for a NULL context. Idempotent.
 */
Result InputEventContextStopPropagation(InputEventContext *context);

/** Skip this target's remaining listeners as well as everything after it. */
Result InputEventContextStopImmediatePropagation(InputEventContext *context);

/**
 * Ask that the event's default action not happen.
 *
 * **Refused** (@ref RoboInvalidArgument) when the listener is passive -- a passive listener promised it would
 * not cancel, so the call is a contract violation rather than a request -- and the refusal is counted into
 * @ref refusedDefault so a caller can find the listener that did it.
 */
Result InputEventContextPreventDefault(InputEventContext *context);

/** Non-zero when the default action has been cancelled. */
int InputEventContextIsDefaultPrevented(const InputEventContext *context);

/** One complete line for @p context, or @ref RoboNotSupported when it does not fit. */
Result InputEventContextWrite(const InputEventContext *context,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/* ============================================================================================== *
 * Listeners and the dispatcher
 * ============================================================================================== */

/** What a listener is. Called once per (target, listener) in the phase it is registered for. */
typedef Result (*InputListenerFn)(void *userdata,
                                  const InputEvent *event,
                                  InputEventContext *context);

/**
 * One listener.
 *
 * `phase` is a *field* rather than three lists, because a control that wants to see a wheel before its
 * scroller and a click after its children registers twice with two phases and one callback.
 */
typedef struct InputListener {
    /** The target it is registered on. Must be valid; an invalid one is skipped and counted by the caller. */
    InputTarget target;
    /** Which phase it is called in. */
    InputPhase phase;
    /** Non-zero when this listener promised not to cancel: `preventDefault` is refused in it (13.33). */
    int passive;
    /** Non-zero when the dispatcher must skip it: a listener that has been removed but not yet collected. */
    int removed;
    /** What to call. */
    InputListenerFn callback;
    /** Whatever the callback needs. */
    void *userdata;
} InputListener;

/**
 * The caller's listener storage, asked one target at a time.
 *
 * Fills @p out with at most @p capacity listeners registered on @p target for @p event and returns **how many
 * exist**, which may exceed @p capacity so a caller learns that it dropped some. This module never allocates
 * a listener list, which is why two windows of the same shell cannot disturb each other's.
 */
typedef struct InputListenerProvider {
    /** Whatever the provider needs; this module never reads it. */
    void *userdata;
    /** Listeners on @p target for @p event, in registration order. */
    uint32_t (*listenersFor)(void *userdata,
                             const InputTarget *target,
                             const InputEvent *event,
                             InputListener *out,
                             uint32_t capacity);
} InputListenerProvider;

/**
 * The loop.
 *
 * Holds the listener source and the counters a caller reads after a run. It allocates nothing and owns
 * nothing: a dispatcher is a value a caller can keep next to its tree.
 */
typedef struct InputDispatcher {
    /** Where listeners come from. NULL when nothing can be listening. */
    const InputListenerProvider *listeners;
    /** Dispatches run. */
    uint32_t dispatches;
    /** Listener calls made. */
    uint32_t calls;
    /** Listener calls a provider offered beyond @ref RoboInputMaxListeners and were not made. Reported. */
    uint32_t dropped;
    /** Callbacks that returned a failure. The dispatch **stops** on the first one. */
    uint32_t failures;
    /** The last callback failure, so a caller can name it after the fact. */
    Result lastFailure;
} InputDispatcher;

/** Point @p dispatcher at @p listeners (may be NULL) and clear its counters. */
void InputDispatcherInit(InputDispatcher *dispatcher, const InputListenerProvider *listeners);

/**
 * Walk @p path in capture, target and bubble order, calling listeners.
 *
 * @p out receives the propagation state -- including whether the default action was cancelled -- and is
 * filled even when nothing was listening, so a caller has one thing to read. Returns @ref RoboOk when the
 * walk completed (listeners included), the callback's failure when a listener failed, or
 * @ref RoboInvalidArgument for NULL arguments and an empty path.
 *
 * A callback that returns a failure stops the dispatch: a listener that could not do its job has left state
 * a later listener would act on, and continuing is how that becomes corruption.
 */
Result InputDispatch(InputDispatcher *dispatcher,
                     const InputEvent *event,
                     const InputEventPath *path,
                     InputEventContext *out,
                     InputDiag *diag);

/* ============================================================================================== *
 * The default action (13.34)
 * ============================================================================================== */

/**
 * What the target/application layer is expected to do after dispatch, when nothing cancelled it.
 *
 * The dispatcher *identifies*; it never performs. Each kind names the layer that owns the behaviour, which is
 * the whole point of separating the two: a scroll needs a scroller, a focus needs a focus manager, and a
 * command needs the command layer.
 */
typedef enum InputDefaultActionKind {
    /** Nothing follows from this event. */
    RoboInputDefaultActionNone = 0,
    /** Move focus to @ref InputDefaultAction::target -- the `PointerDown` rule (13.34). */
    RoboInputDefaultActionFocus = 1,
    /** Scroll by @ref InputDefaultAction::deltaX / `…deltaY` pixels -- the `Wheel` rule. */
    RoboInputDefaultActionScroll = 2,
    /** Activate the focused control -- `Enter` and `Space` (13.34). */
    RoboInputDefaultActionActivate = 3,
    /** Move focus -- an arrow, `Tab` or `Shift+Tab` (13.26). */
    RoboInputDefaultActionNavigate = 4,
    /** Hand the event to the command layer, which turns it into a `TextEditCommand` (13.12/13.36). */
    RoboInputDefaultActionCommand = 5
} InputDefaultActionKind;

/** Number of defined kinds. Asserted positionally by `input.dispatch.*`. */
#define RoboInputDefaultActionKindCount 6u

/** Non-zero if @p kind is a defined kind. */
int InputDefaultActionKindIsKnown(InputDefaultActionKind kind);

/** Human-readable kind name; never NULL; `"unknown"` when undefined. */
const char *InputDefaultActionKindName(InputDefaultActionKind kind);

/** What a caller should do about an event, once dispatch has finished with it. */
typedef struct InputDefaultAction {
    /** What to do. */
    InputDefaultActionKind kind;
    /** Which target it is about: the target under the point for a focus, the focused target for activate. */
    InputTarget target;
    /** The event that asked for it, so a caller can log or re-derive it. */
    InputEventType event;
    /** Scroll distance in pixels, already normalized (13.19). Zero for every other kind. */
    int32_t deltaX;
    int32_t deltaY;
    /** The button a click meant, for `Focus` and `Activate`. */
    MouseButton button;
    /** What moved focus, for `Focus` and `Navigate`. */
    InputFocusDirection direction;
    /** Non-zero when the keyboard event carried text for the command layer. */
    int hasText;
} InputDefaultAction;

/**
 * Identify the action @p event asks for.
 *
 * The four rules 13.34 names, in one table:
 *
 *     Wheel            → scroll, with the deltas converted to pixels through @p lineHeightPx
 *     PointerDown      → focus, on the target under the point
 *                        (a *secondary* button does not move focus: a context menu must not steal the caret)
 *     KeyDown Enter    → activate
 *     KeyDown Space    → activate, when the event carries no text (a space in a text field is a space)
 *     KeyDown Tab      → navigate next / previous
 *     KeyDown other    → command, when the event carries text
 *
 * `KeyUp`, `PointerMove`, hover and every device event answer @ref RoboInputDefaultActionNone. @p request
 * may be NULL, in which case the deltas and the text flag fall back to the event's own payload.
 */
InputDefaultAction InputDefaultActionFor(const InputEvent *event,
                                         const InputDefaultAction *request,
                                         int32_t lineHeightPx);

/** One complete line for @p action, or @ref RoboNotSupported when it does not fit. */
Result InputDefaultActionWrite(const InputDefaultAction *action,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_DISPATCH_H */
