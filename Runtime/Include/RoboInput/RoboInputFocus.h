/*
 * Focus: which target keyboard input is about (Phase 13.25--13.28).
 *
 * ## The four questions this layer answers, and the one it refuses
 *
 *   **Who has focus?** One target per *scope*, where a scope is a window. A shell with three windows keeps
 *   three foci and shows one of them, so switching back to a window restores what was focused in it rather
 *   than starting over. That is why the manager holds a small table keyed by window instead of one field.
 *
 *   **Why did it change?** Every transition carries an `InputFocusReason`. A control that focuses itself on
 *   a pointer press and one that focuses itself on `Tab` need to behave differently (the first selects the
 *   text under the point, the second selects all of it), and a reason is the only honest way to tell them
 *   apart after the fact.
 *
 *   **What happened?** `InputFocusTransition` names both ends -- the target that lost focus and the one that
 *   gained it -- in the order 13.27 requires, plus the window activation beside them. It is a *value*, so a
 *   caller can build the `FOCUS_OUT` / `FOCUS_IN` events from it, and it is the only place the "before" is
 *   available: by the time a listener runs, the manager has already moved on.
 *
 *   **What is focusable, and in what order?** `InputFocusChain` is a bounded, caller-built, caller-ordered
 *   list. Tab order belongs to the layer that knows the tree; this module walks the list and nothing else.
 *   It never invents an order, and an empty chain is a legitimate answer rather than an error.
 *
 * What it refuses: **hit testing, and reading a tree.** `InputFocusRequest` takes a target a caller has
 * already resolved. A manager that walked a tree to find a focusable ancestor would need the tree, and the
 * tree is a layer above this one.
 *
 * ## Focus is not hover, capture or activation
 *
 *
 *   hover (13.18)      follows the pointer, and a window may be full of hovered targets with no focus;
 *   capture (13.17)    routes *pointer* events to one target, and says nothing about the keyboard;
 *   active window      is the shell's notion of which window is in front. It *picks* a scope, and a window
 *                      becoming active is how a scope's remembered focus comes back.
 *
 * Keeping the four apart is what makes "the pointer moved over a text field" not move the caret.
 *
 * See `Documentation/Runtime/Input/Focus.md`.
 */

#ifndef ROBO_INPUT_FOCUS_H
#define ROBO_INPUT_FOCUS_H

#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many windows may remember a focus at once. A shell's window count, with room to spare. */
#define RoboInputMaxFocusScopes 16u

/** How many focusable targets a navigation chain holds. */
#define RoboInputMaxFocusChain 64u

/* ============================================================================================== *
 * Why focus changed (13.25)
 * ============================================================================================== */

/**
 * What moved the focus.
 *
 * A vocabulary rather than a bitmask, because the reasons are mutually exclusive: a change came from one
 * place. `None` is the cleared value -- "nothing moved it", which is what a transition with no change
 * carries -- and it is deliberately *not* spelled `Programmatic`: a programmatic focus is a caller that
 * called this door, and a record that could not tell that apart from "no reason recorded" would be a
 * record nobody can trust.
 */
typedef enum InputFocusReason {
    /** Nothing moved it: the cleared value, and what a no-op transition carries. */
    RoboInputFocusReasonNone = 0,
    /** Keyboard navigation: `Tab`, `Shift+Tab`, an arrow (13.26). */
    RoboInputFocusReasonKeyboard = 1,
    /** A pointer press landed on the target (13.16 / 13.34's `PointerDown` default action). */
    RoboInputFocusReasonPointer = 2,
    /** A caller asked for it directly. */
    RoboInputFocusReasonProgrammatic = 3,
    /** A navigation *command* moved it -- the same walk as `Keyboard`, a different origin. */
    RoboInputFocusReasonNavigation = 4,
    /** The window became active and this is what it remembers (13.28). */
    RoboInputFocusReasonWindowActivation = 5,
    /** An accessibility client moved it (13.52). */
    RoboInputFocusReasonAccessibility = 6
} InputFocusReason;

/** Number of defined reasons. Asserted positionally by `input.focus.*`. */
#define RoboInputFocusReasonCount 7u

/** Non-zero if @p reason is a defined reason. */
int InputFocusReasonIsKnown(InputFocusReason reason);

/** Human-readable reason; never NULL; `"unknown"` when undefined. */
const char *InputFocusReasonName(InputFocusReason reason);

/* ============================================================================================== *
 * The transition (13.27)
 * ============================================================================================== */

/**
 * Both ends of a focus change, in the order a listener sees them.
 *
 * The order is the contract, not a convenience: 13.27 asks for deterministic ordering, and the ordering that
 * is deterministic is *lose then gain* -- the old target is told it is out before the new one is told it is
 * in, with nothing focused in between. A caller reads `previous` (when @ref hasPrevious), dispatches the
 * `FOCUS_OUT`, then reads @ref focused (when @ref hasFocused) and dispatches the `FOCUS_IN`.
 *
 * A window activation rides alongside rather than instead: a `WindowActivate` and a focus change are two
 * events that happen together, so both are on the record and @ref reason says which caused which.
 */
typedef struct InputFocusTransition {
    /** Non-zero when @ref previous is a target this change actually defocused. */
    int hasPrevious;
    /** What lost focus. */
    InputTarget previous;
    /** Non-zero when @ref focused is a target this change actually focused. */
    int hasFocused;
    /** What gained focus. */
    InputTarget focused;
    /** Non-zero when @ref deactivated is a window that stopped being active. */
    int hasDeactivated;
    /** The window that stopped being active. */
    WindowId deactivated;
    /** Non-zero when @ref activated is a window that became active. */
    int hasActivated;
    /** The window that became active. */
    WindowId activated;
    /** What moved the focus. `NONE` when nothing did. */
    InputFocusReason reason;
} InputFocusTransition;

/** Clear @p transition to "nothing changed". Safe on NULL. */
void InputFocusTransitionInit(InputFocusTransition *transition);

/** Non-zero when @p transition describes a focus change (either end). */
int InputFocusTransitionHasFocus(const InputFocusTransition *transition);

/**
 * How many events @p transition must produce: 0, 1, 2 or 4.
 *
 * The count is *stated* rather than left to the caller to add up, because forgetting the activation half is
 * exactly the bug this door exists to make impossible: a transition that activated a window and focused a
 * node is four events (deactivate, activate, out, in) in an order this door also fixes.
 */
uint32_t InputFocusTransitionEventCount(const InputFocusTransition *transition);

/**
 * Build the events @p transition requires, in order, into @p out (capacity @p capacity).
 *
 * Returns the number written, which is @ref InputFocusTransitionEventCount clipped to @p capacity -- so a
 * caller can size from the count and call once. Every event is built through the *public* model doors
 * (`InputEventSetFocusOrWindow`, `InputEventSetTarget`, `InputEventSetWindow`), so nothing here can
 * produce an event the model would refuse.
 */
uint32_t InputFocusTransitionEvents(const InputFocusTransition *transition,
                                    InputEvent *out,
                                    uint32_t capacity);

/** One complete line for @p transition, or @ref RoboNotSupported when it does not fit. */
Result InputFocusTransitionWrite(const InputFocusTransition *transition,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength,
                                 InputDiag *diag);

/* ============================================================================================== *
 * The manager (13.25/13.28)
 * ============================================================================================== */

/** One window's remembered focus. */
typedef struct InputFocusScope {
    /** The window this scope belongs to. */
    WindowId window;
    /** What is focused in it. */
    InputTarget target;
    /** Non-zero when @ref target is a live focus. */
    int active;
} InputFocusScope;

/**
 * Focus, as one value.
 *
 * `activeWindow` picks which scope is *live*; the other scopes keep their targets so a shell can bring a
 * window back without losing the caret position the user left. `focused`/`scope` are the live answer and are
 * kept in step with the table, so a caller asking "who has focus?" reads one field rather than searching.
 */
typedef struct InputFocusManager {
    /** The scopes, one per window that has ever held a focus. Sparse; @ref count is how many are live. */
    InputFocusScope scopes[RoboInputMaxFocusScopes];
    /** Live scopes. */
    uint32_t count;
    /** The window whose scope is live, or 0 for none. */
    WindowId activeWindow;
    /** The application the live scope belongs to, or `RoboInputApplicationIdNone`. */
    InputApplicationId activeApplication;
    /** Who has focus right now. Meaningful only when @ref hasFocus. */
    InputTarget focused;
    /** Non-zero when @ref focused is live. */
    int hasFocus;
    /** How many focus changes have happened: a counter, not a history. */
    uint32_t changes;
    /** Requests refused because the target was not valid, or there was no live scope. Reported. */
    uint32_t refused;
} InputFocusManager;

/** Clear @p manager to "no window, nothing focused". Safe on NULL. */
void InputFocusManagerInit(InputFocusManager *manager);

/**
 * Make @p window the active one, which *selects* its remembered focus (13.28).
 *
 * A window the manager has not seen gets an empty scope: becoming active focuses nothing rather than
 * inventing a target. The old window keeps its scope, so activating it again restores what was focused.
 *
 * Returns @ref RoboOk, or @ref RoboInvalidArgument for a NULL manager or a zero window id. @p out (may be
 * NULL) receives the transition -- a deactivation, an activation, and whatever focus change follows from
 * the new scope.
 */
Result InputFocusManagerSetActiveWindow(InputFocusManager *manager,
                                        WindowId window,
                                        InputApplicationId application,
                                        InputFocusTransition *out,
                                        InputDiag *diag);

/** The active window, or 0. Safe on NULL. */
WindowId InputFocusManagerActiveWindow(const InputFocusManager *manager);

/**
 * Focus @p target in the active scope, reporting both ends (13.25).
 *
 * The target must be valid and must name the active window (`InputTargetIsWindow`), because a focus that
 * belonged to a window the shell is not showing could not receive a key: this layer *refuses* it rather
 * than storing it where nothing would look. There is no active window is likewise a refusal, counted.
 *
 * Focusing what is already focused is @ref RoboOk with an **empty** transition -- the idempotence a slider
 * taking focus on every press needs -- and it does not bump @ref changes.
 */
Result InputFocusRequest(InputFocusManager *manager,
                         const InputTarget *target,
                         InputFocusReason reason,
                         InputFocusTransition *out,
                         InputDiag *diag);

/**
 * Focus nothing in the active scope (13.28).
 *
 * The door a deactivating window and a closing document both need. The scope's target is *dropped* rather
 * than remembered, because "focus left this window" and "this window forgot its focus" are different
 * statements, and only the second should survive a reactivation.
 */
Result InputFocusManagerClear(InputFocusManager *manager,
                              InputFocusReason reason,
                              InputFocusTransition *out,
                              InputDiag *diag);

/** The focused target, or NULL. Safe on NULL. */
const InputTarget *InputFocusManagerTarget(const InputFocusManager *manager);

/** What @p window remembers, or NULL. Safe on NULL. */
const InputTarget *InputFocusManagerScope(const InputFocusManager *manager, WindowId window);

/** Non-zero when @p target has focus. Safe on NULL (0). */
int InputFocusManagerIsFocused(const InputFocusManager *manager, const InputTarget *target);

/**
 * Drop @p target's focus wherever it is recorded, reporting how many scopes lost it.
 *
 * The door a **destroyed** target needs (13.27): no stale focused target may remain after destruction. The
 * caller does not need to know which scope it was in -- a document tip destroyed by a layout change has no
 * idea -- and a target that is not focused is a 0, not an error.
 */
uint32_t InputFocusManagerReleaseTarget(InputFocusManager *manager, const InputTarget *target);

/** Drop @p window's scope entirely. Returns non-zero when one was live. */
int InputFocusManagerReleaseWindow(InputFocusManager *manager, WindowId window);

/** Forget everything. Returns how many scopes were live. */
uint32_t InputFocusManagerClearAll(InputFocusManager *manager);

/* ============================================================================================== *
 * Navigation (13.26)
 * ============================================================================================== */

/**
 * Non-zero when this build can walk @p direction.
 *
 * The vocabulary is 13.13's (`RoboInputCommand.h`), which is the point: a `MoveFocus` command names a
 * direction and this layer answers for it, rather than a second enum existing beside the first.
 *
 * **`NEXT` / `PREVIOUS` / `FIRST` / `LAST` are supported; the four arrows are refused** (`RoboNotSupported`)
 * rather than guessed. A geometric walk is a question about rectangles -- "the nearest focusable target
 * above this one" -- and this layer has no tree and no geometry. Walking the chain in order and calling the
 * answer "up" would be a wrong answer that looks like a right one, which is worse than the refusal.
 */
int InputFocusDirectionIsSupported(InputFocusDirection direction);

/**
 * A navigation chain: the focusable targets, in the order `Tab` visits them.
 *
 * Built by the caller from its own tree. Additions are **deduplicated** rather than rejected: a caller that
 * registers the same target twice (a document re-styled, a rebuild that did not clear first) means "this
 * target is focusable", and a chain with it twice would make `Tab` visit it twice. Entries are removed by
 * identity, so a destroyed target leaves the chain when the caller says so.
 */
typedef struct InputFocusChain {
    /** The targets, in visit order. */
    InputTarget targets[RoboInputMaxFocusChain];
    /** Entries in the chain. */
    uint32_t count;
    /** Additions that were duplicates and changed nothing. Reported. */
    uint32_t duplicates;
} InputFocusChain;

/** Clear @p chain to "nothing is focusable". Safe on NULL. */
void InputFocusChainInit(InputFocusChain *chain);

/** Append @p target. Refused for an invalid target or a full chain; a duplicate is a no-op. */
Result InputFocusChainAdd(InputFocusChain *chain, const InputTarget *target, InputDiag *diag);

/** Remove @p target. Returns its index, or -1 when it was not there. */
int InputFocusChainRemove(InputFocusChain *chain, const InputTarget *target);

/** The index of @p target, or -1. */
int InputFocusChainIndexOf(const InputFocusChain *chain, const InputTarget *target);

/** Walk @p chain and focus the answer. See @ref InputFocusDirectionForward / `…Backward`. */
Result InputFocusChainFocus(InputFocusManager *manager,
                            const InputFocusChain *chain,
                            InputFocusDirection direction,
                            InputFocusReason reason,
                            InputFocusTransition *out,
                            InputDiag *diag);

/** Walk @p chain and return the target the direction names, without changing focus. NULL when none. */
const InputTarget *InputFocusChainPeek(const InputFocusChain *chain,
                                       const InputTarget *current,
                                       InputFocusDirection direction);

/* ============================================================================================== *
 * Diagnostics
 * ============================================================================================== */

/** One complete line for @p manager, or @ref RoboNotSupported when it does not fit. */
Result InputFocusManagerWrite(const InputFocusManager *manager,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              InputDiag *diag);

/** One complete line for @p chain, or @ref RoboNotSupported when it does not fit. */
Result InputFocusChainWrite(const InputFocusChain *chain,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_FOCUS_H */
