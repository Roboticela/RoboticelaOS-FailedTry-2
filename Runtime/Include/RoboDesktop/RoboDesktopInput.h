/*
 * The shell's input integration (Phase 13.66--13.77) — see `RoboDesktop.h` §16.
 *
 * ## Why this exists
 *
 * `DesktopObservePointer` was the whole of this build's input integration: a point and a pressed flag. That
 * is enough for two decisions about windows (raise, close) and no more — no buttons, no wheel, no keyboard,
 * no hover, no capture, no focus, no shortcuts. Meanwhile `Runtime/Source/RoboInput/` grew the whole model
 * and **nothing outside itself included it**, so all of it was unreachable.
 *
 * This file is the join. It is the one place in the tree where the shell and the input model meet, and it
 * exists as a *file* rather than as code inside `RoboDesktopScene.c` because the same join is what a real
 * application shell needs: the scene drives it, and so will every later consumer.
 *
 * ## What it owns, and what it does not
 *
 * It owns one `InputSession` (devices, held keys, pointer state, captures, hover, focus, the text pipeline,
 * the drag, the gesture arena), one `InputQueue`, one `InputDispatcher`, the shell's listener store, one
 * `InputShortcutManager`, one `InputCommandRouter`, one `InputCursorManager`, one `InputCoalescer` and one
 * `InputLatencyTrace` -- the last of which is stamped only when the caller supplies a clock through
 * @ref DesktopInputConfig::clockMs, because this module has no business owning a timebase
 * (13.77.12: a measurement a test can make, or an honest zero). It **borrows** the `Desktop` and never
 * draws: a frame is still `DesktopRender`'s, and this layer only marks what must be redrawn.
 *
 * It is not a second input system. Every event it sees arrives as a `WindowEvent` and goes through
 * `InputEventFromWindowEvent` — the same bridge a future platform adapter will feed. Every decision it makes
 * is one of RoboInput's own doors. There is no `DesktopMouse` here and no `DesktopKey`: the shell is a
 * *consumer*.
 *
 * ## The loop, in the order it runs (13.76)
 *
 *     pump     the window queue, bounded by the caller's wait, into an InputQueue through the bridge;
 *     drain    one InputEvent at a time;
 *       route  build the InputRoute from the event window's own geometry (13.71) and convert the position
 *              into the plane the hit test wants;
 *       target the hit test (13.70), or the focused target for a key event (13.69);
 *       walk   capture → target → bubble through the caller's listeners (13.31/13.32);
 *       act    the DefaultAction RoboInput identified (13.34): focus, navigate, activate, scroll, command;
 *     report   what happened, as counts.
 *
 * ## What the shell is, in the model's terms
 *
 * **A listener.** The shell's own two decisions about windows — raise on a press, close on the close
 * button — are a registered `InputListener` on the window target for `POINTER_DOWN`, and which *part* of the
 * window was pressed is answered by the shell's own `DesktopHitTest`, exactly as a control re-derives its
 * sub-regions from its own geometry. That is what makes 13.66/13.67 true without inventing a second path.
 *
 * **A provider.** The three seams `RoboInputDispatch.h` names — hit test, path, listeners — are supplied
 * from here over the shell's window list and its listener store.
 *
 * **A command target.** The clipboard the shell installs is an `InputCommandTarget`, so `Ctrl+C` reaches it
 * through the same router an application command would.
 *
 * **A window manager, in the one sense this shell has one** (13.67/13.75). The two gestures a window system
 * cannot be without -- a **move** dragged by the title bar and a **resize** dragged by an edge or a corner --
 * are this layer's, expressed as actions on the *shell's* window list (`DesktopWindowSetBounds`) under the
 * model's own pointer capture (13.17). The zones are the shell's (`DesktopArea`, eight of them since
 * 13.67), so there is one hit test and not two; the grab is `DesktopChromeResizeArea`'s answer, and the
 * cursor that says which edge is under the pointer is a *declared* one (13.43's override), removed when the
 * gesture ends rather than left to the shape of the path the mouse took.
 *
 * ## The one thing this build does not have, stated rather than hidden
 *
 * The shell's windows hold a `PaintDisplayList`, not a `LayoutTree` — so **nothing here can hit-test a node
 * inside a window**, and a press in a window's client area resolves to a `WINDOW` target. A caller that has a
 * real document registers it through @ref DesktopInputSetContent, which supplies the layout tree, the DOM
 * root handle, the editor a text command reaches, and the command targets for application and clipboard
 * commands. With one registered, the same press resolves to a `DOCUMENT` target with a node, the editor
 * receives text, and `Ctrl+C` copies the selection.
 *
 * ## Document
 *
 * `Documentation/Runtime/Desktop/InputIntegration.md`.
 */

#ifndef ROBO_DESKTOP_INPUT_H
#define ROBO_DESKTOP_INPUT_H

#include "RoboBase/RoboError.h"

#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"

#include "RoboInput/RoboInput.h"

#include "RoboText/RoboTextEditing.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque layout tree (Phase 10) — borrowed by @ref DesktopInputContent when a window has real
 * document contents. The full `RoboLayout/RoboLayoutTree.h` stays out of this public header so the
 * shell's boundary scan keeps holding: a document-layer include belongs in the bridge
 * (`RoboDesktopDocument`) or in this module's `.c`, never on every DesktopInput consumer.
 */
typedef struct LayoutTree LayoutTree;

/** Events the queue holds before it starts refusing (`InputQueueConfig.capacity`). */
#define RoboDesktopInputDefaultQueueCapacity 64u

/** Listeners the shell's own store holds. One table, one shell, one number. */
#define RoboDesktopInputMaxListeners 64u

/** Windows whose content a caller may describe (13.68). */
#define RoboDesktopInputMaxContents 16u

/** Longest status line the shell's action handler writes into its own report. */
#define RoboDesktopInputNoteMax 64u

/* ============================================================================================== *
 * Configuration (13.66)
 * ============================================================================================== */

/** How the shell's input system is built. */
typedef struct DesktopInputConfig {
    /** Events the queue holds; 0 means @ref RoboDesktopInputDefaultQueueCapacity. */
    uint32_t queueCapacity;/**
 * The pixel height one *line* of a wheel event converts to (`InputDefaultActionFor`). 0 means 16, which
 * is what the model's own default assumes.
 */
    int32_t lineHeightPx;
    /**
     * The caller's **monotonic** clock in milliseconds, or NULL (the default) for "this layer has no
     * timebase".
     *
     * Supplied rather than owned because a clock is a platform fact this module deliberately does not
     * reach for: with one, every dispatched event opens an `InputLatencyTrace` record (13.48) whose four
     * differences a caller reads through @ref GetDesktopInputLatency; without one, the trace stays empty
     * and that door answers zeroes. Read as many times per event as the stamps need, so it must be a
     * `GetTickCount64`-shaped read and not a sleep.
     */
    uint64_t (*clockMs)(void *userdata);
    /** Passed to @ref clockMs. Never read by this layer otherwise. */
    void *clockUserdata;
    /**
     * Non-zero (the default) to merge high-frequency moves through `InputCoalescer` before dispatch (13.47).
     * Turning it off makes every move its own dispatch, which is what a case that asserts *ordering* wants.
     */
    int enableCoalescing;
    /**
     * The native window the shell presents into, or `RoboWindowIdNone` for a screen-only shell. Copied onto
     * the routes and targets this layer builds, so an event's `window` names the shell's own window even when
     * the window queue did not say.
     */
    WindowId window;
} DesktopInputConfig;

/** Fill @p config with the defaults this header describes. Safe on NULL. */
void DesktopInputConfigInit(DesktopInputConfig *config);

/* ============================================================================================== *
 * Registered content (13.68 / 13.70 / 13.72 / 13.73 / 13.74)
 * ============================================================================================== */

/**
 * What a window's content is, when the caller has one.
 *
 * Every field is optional and every absent one degrades to a *stated* answer rather than a failure — the same
 * shape RoboLayout's font provider and RoboInput's own three providers use. A window with no record at all is
 * simply a window, which is a legitimate thing for a window to be.
 */
typedef struct DesktopInputContent {
    /** Which window this describes. */
    DesktopWindowId window;
    /**
     * The document's layout tree, borrowed. With one, a press in the client area performs a real
     * `PaintHitTest` (13.70) and the target carries the node's `LayoutNodeId`.
     */
    const LayoutTree *tree;
    /**
     * The document root's DOM identity, by value. With one, a client-area hit answers a `DOCUMENT` target
     * even when the layout tree is absent (`RoboInputTargetSetDocument` needs at least one identity).
     * An unformed handle means none.
     */
    HtmlNodeHandle documentNode;
    /**
     * The editor a text command reaches, borrowed (13.72). With one, an `InsertText`/`DeleteBackward`/arrow
     * command from the focused window lands in it, and `Ctrl+C`/`Ctrl+X`/`Ctrl+V` read and write it (13.74).
     */
    RoboTextEditor *editor;
    /**
     * A target that claims this window's application commands (13.73), pushed onto the router's chain
     * **inside** the editor so a focused text field wins a contested chord. May be unset.
     */
    InputCommandTarget commands;
} DesktopInputContent;

/**
 * Clear @p content to "a window with nothing inside it". Safe on NULL.
 *
 * Every optional field empty, which is a legitimate record rather than a degenerate one: a window whose
 * contents nothing can describe still takes focus, still raises and still takes its share of the keyboard.
 */
void DesktopInputContentInit(DesktopInputContent *content);

/* ============================================================================================== *
 * The system
 * ============================================================================================== */

typedef struct DesktopInput DesktopInput;

/**
 * Build the shell's input system over @p desktop.
 *
 * Connects one keyboard and one mouse through the session — not because anything reads hardware, but so a
 * bridged event carries a *device identity* rather than an empty one, and so `DesktopInputDisconnect` has
 * something to sweep. Registers the shell's own listener and the default chord table (13.35): the four
 * clipboard chords, select-all and undo, all in the `WINDOW` scope.
 *
 * @param desktop borrowed, must outlive the system.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an unusable screen; RoboOutOfMemory.
 */
Result DesktopInputCreate(Desktop *desktop,
                          const DesktopInputConfig *config,
                          DesktopInput **out,
                          DesktopDiag *diag);

/** Destroy @p input: the queue, the listener store, the session, the router, the clipboard. Safe on NULL. */
void DesktopInputDestroy(DesktopInput *input);

/** Non-zero once a system exists and has not been destroyed. */
int DesktopInputIsLive(const DesktopInput *input);

/** The shell @p input was built over, borrowed. NULL for NULL. */
Desktop *DesktopInputDesktop(DesktopInput *input);

/* ============================================================================================== *
 * The loop (13.76)
 * ============================================================================================== */

/** What one iteration of the shell's input loop did. Counts, because that is what a reader compares. */
typedef struct DesktopInputStep {
    /** Window events taken off the window queue. */
    uint32_t windowEvents;
    /** Of those, the ones the bridge turned into an input event and enqueued. */
    uint32_t translated;
    /** Of those, the lifecycle events with no input equivalent, skipped by name rather than by accident. */
    uint32_t skipped;
    /** Input events taken off the queue and dispatched. */
    uint32_t dispatched;
    /** Moves merged into a held slot rather than dispatched (13.47). Never counted as `dispatched`. */
    uint32_t coalesced;
    /** Events dropped because the queue was full (`InputQueueStats.dropped` advanced). */
    uint32_t dropped;
    /** Listener calls the dispatcher made. */
    uint32_t listenerCalls;
    /** Dispatches a passive or `preventDefault` listener cancelled (13.33). */
    uint32_t prevented;
    /** Default actions identified and performed. */
    uint32_t actions;
    /** Focus changes the shell made (13.69). */
    uint32_t focusChanges;
    /** Target changes the pointer's hover produced (13.18). */
    uint32_t hoverChanges;
    /** Cursor shape changes (13.43/13.75). */
    uint32_t cursorChanges;
    /**
     * Positioned pointer events that moved the shell's tracked pointer (software cursor). Distinct from
     * @ref cursorChanges, which is the *shape* the hit test asked for.
     */
    uint32_t pointerMoves;
    /** Chords resolved by the shortcut table (13.35). */
    uint32_t shortcuts;
    /** Commands a target claimed (13.36/13.73). */
    uint32_t commands;
    /** Commands nobody claimed. The number that says whether a binding was ever reachable. */
    uint32_t unclaimed;
    /** Characters delivered to a registered editor (13.72). */
    uint32_t textInputs;
    /** Clipboard operations (13.74). */
    uint32_t clipboardOps;
    /** Drag sessions begun / ended (13.42/13.74). Counted where they happen and **not** derived from one
     *  another: a drag that a cancel ended began once and ended once, which a difference cannot say. */
    uint32_t dragsBegan;
    uint32_t dragsEnded;
    /** Window moves a title-bar drag performed, and window resizes an edge drag performed (13.67). */
    uint32_t windowMoves;
    uint32_t windowResizes;
    /** Events whose latency record was opened this step (13.48/13.77.12); 0 with no clock. */
    uint32_t traced;
    /** The worst dispatch-to-handler time among this step's events, in milliseconds. 0 with no clock. */
    uint64_t slowestHandlerMs;
    /** Non-zero when the *native* window asked to close, or closed. Not an input event; the scene's. */
    int closeRequested;
    /** Non-zero when the destination changed size or scale. Not an input event; the scene's. */
    int resized;
    int scaleChanged;
    /** The last command id a target claimed, or `RoboInputCommandNone`. Read after the fact. */
    InputCommandId lastCommand;
    /**
     * Non-zero when this step dispatched a `POINTER_UP` onto a `DOCUMENT` target with a layout node
     * (Phase 16: the scene posts a RoboJS `"click"` from these fields). Cleared by
     * @ref DesktopInputStepInit.
     */
    int documentClick;
    /** The layout node under that release, or `RoboLayoutNodeIdNone`. */
    LayoutNodeId documentClickNode;
    /** Document-space coordinates of that release (the plane `PaintHitTest` used). */
    double documentClickX;
    double documentClickY;
} DesktopInputStep;

/** Clear @p step to a step that did nothing. Safe on NULL. */
void DesktopInputStepInit(DesktopInputStep *step);

/** Non-zero when @p step did nothing at all: no events, no dispatch, no action, no state change. */
int DesktopInputStepIsEmpty(const DesktopInputStep *step);

/** One complete line for @p step, or @ref RoboNotSupported when it does not fit. */
Result DesktopInputStepWrite(const DesktopInputStep *step,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             DesktopDiag *diag);

/**
 * Advance the shell's input by one iteration: pump, drain, route, dispatch, act.
 *
 * @param waitMs how long the window pump may wait for a first event.
 * @return RoboOk, including an iteration that found nothing to do; RoboInvalidArgument for NULL @p input.
 *         A listener's failure stops that one dispatch and is reported in @p diag, because a listener that
 *         could not do its job has left state a later listener would act on.
 */
Result GetDesktopInputStep(DesktopInput *input,
                           uint32_t waitMs,
                           DesktopInputStep *outStep,
                           DesktopDiag *diag);

/** The pump alone: window queue → bridge → input queue. Fills the scene's own flags in @p outStep. */
Result DesktopInputPump(DesktopInput *input,
                        uint32_t waitMs,
                        DesktopInputStep *outStep,
                        DesktopDiag *diag);

/** The drain alone: every queued event routed, dispatched and acted on. */
Result DesktopInputDrain(DesktopInput *input,
                         DesktopInputStep *outStep,
                         DesktopDiag *diag);

/**
 * Enqueue @p event on the same path a bridged event takes (13.50).
 *
 * The door a synthetic device, a replay (13.49) and a test all use: nothing here has a second routing path,
 * which is the whole claim those two goals make. The event is **copied**, so the caller keeps its own.
 */
Result DesktopInputPush(DesktopInput *input, const InputEvent *event, DesktopDiag *diag);

/* ============================================================================================== *
 * What the shell registers (13.66 / 13.68 / 13.73)
 * ============================================================================================== */

/**
 * Describe @p content's window. Replaces any earlier record for the same window.
 *
 * An editor is installed as a command target on the router's chain immediately; a `commands` target is
 * installed *inside* it, so the chain reads outermost → innermost as window → application → editor.
 */
Result DesktopInputSetContent(DesktopInput *input,
                              const DesktopInputContent *content,
                              DesktopDiag *diag);

/** Forget @p window's content, and drop its command targets. `RoboNotFound` when it had none. */
Result DesktopInputClearContent(DesktopInput *input,
                                DesktopWindowId window,
                                DesktopDiag *diag);

/**
 * Register @p listener.
 *
 * The shell's own two listeners are installed at creation; this is for a caller adding a control's. The
 * listener is **copied**, and rejected if the table is full — a silently dropped listener is a control that
 * never hears anything.
 */
Result DesktopInputAddListener(DesktopInput *input,
                               const InputListener *listener,
                               DesktopDiag *diag);

/** Mark every listener on @p target removed (13.27's "a destroyed target hears nothing"). Returns the count. */
uint32_t DesktopInputRemoveListeners(DesktopInput *input, const InputTarget *target);

/** Listeners the shell's store holds. */
uint32_t DesktopInputListenerCount(const DesktopInput *input);

/** Bind one chord (13.35). `scope` is the shell's to choose; `WINDOW` is what the defaults use. */
Result DesktopInputBindShortcut(DesktopInput *input,
                                InputCommandId command,
                                PhysicalKey physical,
                                InputModifiers modifiers,
                                InputShortcutScope scope,
                                DesktopDiag *diag);

/** A chord → the command it means, without performing it. */
Result DesktopInputResolveShortcut(DesktopInput *input,
                                   PhysicalKey physical,
                                   InputModifiers modifiers,
                                   InputShortcut *out,
                                   DesktopDiag *diag);

/* ============================================================================================== *
 * Devices (13.44)
 * ============================================================================================== */

/** Connect a keyboard through the session. @p outId may be NULL. */
Result DesktopInputConnectKeyboard(DesktopInput *input, InputDeviceId *outId, DesktopDiag *diag);

/** Connect a mouse through the session. @p outId may be NULL. */
Result DesktopInputConnectMouse(DesktopInput *input, InputDeviceId *outId, DesktopDiag *diag);

/**
 * Disconnect @p id: the registry entry **and** everything it held (13.44's "no stale state may remain").
 *
 * @p out may be NULL. `RoboNotFound` for an id the session does not own, and *no* sweep happens — a caller
 * that lost track of its own table should hear about it rather than have this layer release a stranger's keys.
 */
Result DesktopInputDisconnect(DesktopInput *input,
                              InputDeviceId id,
                              InputSessionSweep *out,
                              DesktopDiag *diag);

/** Devices the session's registry holds. */
uint32_t DesktopInputDeviceCount(const DesktopInput *input);

/** Cancel every transient thing at once (13.78). Focus stays, on purpose — see `RoboInputSession.h`. */
Result DesktopInputResetTransient(DesktopInput *input, InputSessionSweep *out, DesktopDiag *diag);

/* ============================================================================================== *
 * Geometry and the model (13.70 / 13.71)
 * ============================================================================================== */

/**
 * What is at @p point, as a target, and what the hit test needed.
 *
 * @param outTarget  receives the target; a point outside every window answers the shell's own window when
 *                   one is configured, otherwise `RoboNotFound`.
 * @param outClientPoint receives the point in the hit window's **document** plane, when there is one; the
 *                   point itself otherwise. Optional.
 * @param outHit     receives the shell's own `DesktopHit`, so a caller can tell chrome from content. Optional.
 * @param outWindow  receives the window the point resolved to, or `RoboDesktopWindowIdNone`. Optional.
 *
 * This is the door 13.70 names and it is also what a test asserts against without a window queue.
 */
Result DesktopInputResolvePoint(DesktopInput *input,
                                GfxPoint point,
                                InputSpace space,
                                InputTarget *outTarget,
                                GfxPoint *outClientPoint,
                                DesktopHit *outHit,
                                DesktopWindowId *outWindow,
                                DesktopDiag *diag);

/**
 * The route for @p window: its frame in screen space and its client rectangle in window space (13.71).
 *
 * `RoboNotFound` for a window the shell does not hold, which is the honest answer for an event about a
 * window that has gone.
 */
Result DesktopInputRouteForWindow(const DesktopInput *input,
                                  DesktopWindowId window,
                                  InputRoute *out,
                                  DesktopDiag *diag);

/** The plane a bridged event's positions are on, as this build configured the bridge. */
InputSpace DesktopInputEventSpace(const DesktopInput *input);

/* ============================================================================================== *
 * Focus, cursor and commands (13.69 / 13.73 / 13.75)
 * ============================================================================================== */

/** The focused target, by value. Non-zero when there is one. */
int DesktopInputFocusedTarget(const DesktopInput *input, InputTarget *out);

/** Move focus to @p target (13.69). Records the lose-then-gain transition on @p out. */
Result DesktopInputFocusTarget(DesktopInput *input,
                               const InputTarget *target,
                               InputFocusReason reason,
                               InputFocusTransition *out,
                               DesktopDiag *diag);

/** Walk the shell's window chain in @p direction (13.26/13.69). */
Result DesktopInputFocusMove(DesktopInput *input,
                             InputFocusDirection direction,
                             InputFocusReason reason,
                             InputFocusTransition *out,
                             DesktopDiag *diag);

/** The cursor shape the last hit test asked for (13.43/13.75). */
InputCursorType DesktopInputCursor(const DesktopInput *input);

/**
 * Route @p command through the chain the shell assembled (13.36/13.73).
 *
 * @return the claiming target's answer; `RoboNotFound` when nobody claimed it, counted into the next step's
 *         `unclaimed`. Node: a text edit is offered to the focused window's editor first, which is what makes
 *         "a focused field consumes `Ctrl+A` before the window does" true.
 */
Result DesktopInputRouteCommand(DesktopInput *input,
                                const InputCommand *command,
                                int32_t *outLevel,
                                DesktopDiag *diag);

/** Perform one of the shell's own actions directly — the door a menu or an accessibility client uses. */
Result DesktopInputPerformCommand(DesktopInput *input,
                                  InputCommandId id,
                                  DesktopDiag *diag);

/* ============================================================================================== *
 * Drag (13.42 / 13.74) and the window gestures (13.67)
 * ============================================================================================== */

/** Non-zero while a drag is running. */
int DesktopInputIsDragging(const DesktopInput *input);

/** The drag's phase (`IDLE` when none). */
InputDragPhase DesktopInputDragPhase(const DesktopInput *input);

/**
 * Non-zero while a **window move** is running: a title-bar drag that began on @ref RoboDesktopAreaWindowTitle.
 *
 * Distinct from @ref DesktopInputIsDragging on purpose. Every move is a drag (the model owns the gesture, the
 * payload and the phases), and not every drag is a move -- a drag a document starts carries selections and
 * nothing here moves a window. The two questions have two doors so a caller cannot read one as the other.
 */
int DesktopInputIsMovingWindow(const DesktopInput *input);

/** Non-zero while a **window resize** is running (13.67). */
int DesktopInputIsResizingWindow(const DesktopInput *input);

/**
 * The resize zone the running gesture is anchored to, or @ref RoboDesktopAreaNone.
 *
 * The zone is decided **once**, at the press, and never re-derived from the pointer: a resize that asked
 * "which edge am I on now?" would flip to whichever edge the pointer had wandered into, which is the bug
 * every ad-hoc implementation of this has. It is also what the cursor reads, so the shape stays the one the
 * gesture began with.
 */
DesktopArea DesktopInputResizeArea(const DesktopInput *input);

/** The window the running move or resize is changing, or @ref RoboDesktopWindowIdNone. */
DesktopWindowId DesktopInputGestureWindow(const DesktopInput *input);

/* ============================================================================================== *
 * Lifecycle: a target or a window that has gone (13.77.8 / 13.77.13)
 * ============================================================================================== */

/**
 * What forgetting a target or a window released.
 *
 * Counts rather than a boolean, because `nothing was holding it` and `this layer released it` are
 * different answers, and the second is the one a leak report needs. Every field is a door of the model's own
 * (13.27/13.28): this layer decides *what* to forget, never *how* to sweep.
 */
typedef struct DesktopInputForget {
    /** Listeners the shell's store marked removed. */
    uint32_t listenersRemoved;
    /** Places the session found the target focused or captured in (0, 1 or 2). */
    uint32_t sessionTargetsReleased;
    /** Captures released by window. */
    uint32_t capturesReleased;
    /** Focus scopes dropped. */
    uint32_t focusReleased;
    /** Keys released because their device held them in the window that went. */
    uint32_t keysReleased;
    /** Non-zero when a drag anchored on the window was cancelled. */
    int dragCancelled;
    /** Non-zero when the gesture arena was reset while a sequence was unresolved. */
    int gesturesReset;
    /** Non-zero when a registered content record was dropped. */
    int contentCleared;
} DesktopInputForget;

/** Clear @p out. Safe on NULL. */
void DesktopInputForgetInit(DesktopInputForget *out);

/**
 * @p target has been destroyed: stop delivering to it, and let go of it everywhere.
 *
 * The door 13.27/13.77.13 names -- "a destroyed target hears nothing and stays nowhere" -- and it is keyed
 * by identity, so a target focused in one scope and captured for a pointer in another is released in both.
 * The shell's own listener store is *not* the model's, which is why this layer has its own removal beside
 * the session's sweep.
 *
 * @p out may be NULL. A target nothing referred to is a report of zeroes rather than an error.
 */
Result DesktopInputForgetTarget(DesktopInput *input,
                                const InputTarget *target,
                                DesktopInputForget *out,
                                DesktopDiag *diag);

/**
 * @p window has been destroyed: everything this layer attributed to it, released.
 *
 * The whole row 13.77.8 asks for, in one call: its listeners, its focus scope and any target focused in it,
 * a pointer it captured, a move or resize anchored on it, a drag that began in it, and its **registered
 * content record**. Deliberately stronger than `DesktopInputClearContent` alone, because a window that has
 * gone is not a window whose contents changed.
 *
 * Keys are left alone, with the reason the session states: a key held by a keyboard device is not the
 * window's, and releasing one because a window closed would swallow a keystroke the user is still holding.
 */
Result DesktopInputForgetWindow(DesktopInput *input,
                                DesktopWindowId window,
                                DesktopInputForget *out,
                                DesktopDiag *diag);

/* ============================================================================================== *
 * Measurement (13.77.12)
 * ============================================================================================== */

/**
 * What this layer measured, from the latency records its own loop opened.
 *
 * All zero with no clock (@ref DesktopInputConfig::clockMs): a build that did not supply one measured
 * nothing, and zeroes say so rather than reporting a plausible small number.
 */
typedef struct DesktopInputLatency {
    /** Hardware-to-dispatch: the delay before anything could react. */
    uint64_t dispatchLastMs;
    uint64_t dispatchMaxMs;
    uint64_t dispatchSumMs;
    /** Dispatch-to-handler: the work the event caused. */
    uint64_t handlerLastMs;
    uint64_t handlerMaxMs;
    uint64_t handlerSumMs;
    /** Hardware-to-handler: the delay a user feels. */
    uint64_t frameLastMs;
    uint64_t frameMaxMs;
    uint64_t frameSumMs;
    /** Records opened, and the ones that reached a handler. */
    uint64_t traced;
    uint64_t completed;
} DesktopInputLatency;

/** Read @p input's measured latency. All zeroes when no clock was configured. */
Result GetDesktopInputLatency(const DesktopInput *input, DesktopInputLatency *out);

/**
 * One complete line for @p latency, or @ref RoboNotSupported when it does not fit.
 *
 * The shape a performance baseline is *diffed* from rather than eyeballed:
 *
 *     latency traced=1000 completed=1000 dispatch last=0 max=1 sum=8 handler last=0 max=2 sum=12
 */
Result DesktopInputLatencyWrite(const DesktopInputLatency *latency,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                DesktopDiag *diag);

/* ============================================================================================== *
 * Capabilities (13.77)
 * ============================================================================================== */

/**
 * What **this integration** claims, one field per consumer seam, so a reader can tell a finished seam from
 * an absent one without reading this file's prose.
 *
 * It lives here and not on `Desktop` because that table describes the *shell object*: a `Desktop` driven by
 * `DesktopObservePointer` and nothing else genuinely delivers no input, and 13.66 is the phase that says the
 * *consumer* is what changes. Every flag below is a seam a suite drives end to end; the absences are named
 * at the bottom and each one is a structure rather than a flag somebody forgot.
 */
typedef struct DesktopInputCapabilities {
    /** **13.66** The shell reads a normalized event stream and no raw device event. */
    int shellConsumption;
    /** **13.67** Window raise / close / activate / focus / minimize / move / resize through this layer. */
    int windowManagement;
    /** **13.67** A move dragged by the title bar, under pointer capture. */
    int windowMoving;
    /** **13.67** A resize dragged by an edge or a corner, anchored to the zone the press named. */
    int windowResizing;
    /** **13.68** A press in a registered window's client area resolves to a DOM node target. */
    int documentDelivery;
    /** **13.69** Focus scopes (windows) and targets, with the lose/gain transition published. */
    int focusDelivery;
    /** **13.70** Pointer points resolved by the shell's hit test, then paint's inside a registered tree. */
    int pointerHitTesting;
    /** **13.70** A captured pointer's moves reach its holder and nobody else. */
    int pointerCapture;
    /** **13.71** Positions converted through `InputRoute` between screen, window and document space. */
    int coordinateSpaces;
    /** **13.72** Keys reach the focused target; text reaches a registered `RoboTextEditor`. */
    int keyboardDelivery;
    /** **13.72** The text pipeline's characters land as one `InsertText` command, never a raw scan code. */
    int textDelivery;
    /** **13.73** The chord table, and the scoped command chain the shell assembles. */
    int shortcutRouting;
    /** **13.73** Commands claimed innermost-first, with an unclaimed count rather than a silence. */
    int commandRouting;
    /** **13.74** Copy / cut / paste over the focused editor's selection and this layer's clipboard. */
    int clipboard;
    /** **13.74** A drag session the model owns, on the pointer path the shell already routes. */
    int dragDrop;
    /** **13.75** One shape, decided by the hit test, with the shell's gestures declared as overrides. */
    int cursorManagement;
    /** **13.76** The pump, the drain, the coalescer and the latency trace, in one bounded iteration. */
    int eventLoop;
    /** **13.76** High-frequency moves merged before dispatch (13.47), counted separately. */
    int inputCoalescing;
    /** **13.76** A caller-supplied clock turns every dispatched event into a timed record (13.48). */
    int inputLatency;
    /** **13.77.8** A destroyed window's listeners, focus and captures are released, not left dangling. */
    int lifecycleSweep;
    /** **13.77.13** A dump a reader can diff, and a quiescence read. */
    int introspection;

    /** **0, and named**: window snapping / tiling. The model has no policy for it and this layer has no
     *  gesture (Phase 14.17's row). */
    int windowSnapping;
    /** **0, and named**: a compositor-driven frame scheduling hook. This layer marks what to repaint; it
     *  does not own when a frame runs. */
    int frameScheduling;
    /** **0, and named**: cursor *images*. This layer names a shape (`InputCursorType`); drawing one is the
     *  shell's, and a custom bitmap would be a variant in RoboInput rather than a drawing call here. */
    int cursorImages;

    /** Events the default queue holds (`RoboDesktopInputDefaultQueueCapacity`). */
    uint32_t defaultQueueCapacity;
    /** Windows whose content a caller may describe (`RoboDesktopInputMaxContents`). */
    uint32_t maxContents;
    /** Listeners the shell's store holds (`RoboDesktopInputMaxListeners`). */
    uint32_t maxListeners;
    /** Cursor shapes the model names (`RoboInputCursorTypeCount`); 13.75 added `grabbing`. */
    uint32_t cursorShapes;
    /** Resize zones the shell's hit test answers (`DesktopArea`'s eight). */
    uint32_t resizeZones;
    /** The shipped version string (`RoboInputVersionString`), so a report names one number. */
    const char *version;
} DesktopInputCapabilities;

/** Fill @p out. `RoboInvalidArgument` for NULL; otherwise every field set. */
Result GetDesktopInputCapabilities(DesktopInputCapabilities *out);

/* ============================================================================================== *
 * Reporting
 * ============================================================================================== */

/** The counters a caller reads after a run. */
typedef struct DesktopInputStats {
    /** Steps run through @ref GetDesktopInputStep. */
    uint64_t steps;
    /** Window events pumped. */
    uint64_t windowEvents;
    /** Events the bridge translated. */
    uint64_t translated;
    /** Queue pushes refused because the queue was full. */
    uint64_t dropped;
    /** Events dispatched. */
    uint64_t dispatched;
    /** Listener calls made. */
    uint64_t listenerCalls;
    /** Default actions performed. */
    uint64_t actions;
    /** Focus changes. */
    uint64_t focusChanges;
    /** Cursor changes. */
    uint64_t cursorChanges;
    /** Shortcuts resolved. */
    uint64_t shortcuts;
    /** Commands routed and claimed. */
    uint64_t commands;
    /** Commands nobody claimed. */
    uint64_t unclaimed;
    /** Characters delivered to an editor. */
    uint64_t textInputs;
    /** Clipboard operations performed (13.74). An empty copy is not one: the clipboard kept what it had. */
    uint64_t clipboardOps;
    /** Drag sessions begun and ended (13.42/13.74). */
    uint64_t dragsBegan;
    uint64_t dragsEnded;
    /** Window moves a title-bar drag performed, and window resizes an edge drag performed (13.67). */
    uint64_t windowMoves;
    uint64_t windowResizes;
    /** Devices connected / disconnected through the session. */
    uint64_t connects;
    uint64_t disconnects;
} DesktopInputStats;

Result GetDesktopInputStats(const DesktopInput *input, DesktopInputStats *out);

/** Non-zero when nothing is held: no keys, pointers, captures, contacts or drag. */
int DesktopInputIsQuiescent(const DesktopInput *input);

/** Room @ref DesktopInputWrite needs, NUL included. */
#define RoboDesktopInputDumpMax (6144u + (RoboDesktopInputMaxContents * 128u))

/** Write @p input as text: the session, the router, the shortcuts, the contents and every counter. */
Result DesktopInputWrite(const DesktopInput *input,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         DesktopDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_INPUT_H */
