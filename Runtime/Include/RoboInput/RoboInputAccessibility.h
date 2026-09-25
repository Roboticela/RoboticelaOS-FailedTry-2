/*
 * Accessibility input hooks (Phase 13.52).
 *
 * An accessibility client — a screen reader, a switch device, a voice-control layer, a test harness standing
 * in for a user — wants to say *"activate the focused control"*, not *"press 0x1C"*. Those are different
 * requests, and the whole value of this layer is that it refuses to pretend otherwise: an accessibility
 * client that synthesized a scan code would have to guess a layout, a key and a modifier state, and would
 * then be indistinguishable from a keyboard in every log and every policy decision.
 *
 * So the lane is:
 *
 *     accessibility client → InputA11yRequest → InputCommand → InputCommandRouter → a target (13.36)
 *
 * and exactly one thing is new: the vocabulary an accessibility client speaks, and the **policy** that says
 * which of the module's forty commands one may reach.
 *
 * ## The policy is half the layer
 *
 * `InputA11yCommandAllowed` is the boundary. An accessibility client may edit text, move focus, activate,
 * scroll, select and cancel — the things a user does *to* the thing they are looking at. It may **not**
 * close the window, minimize it, toggle a debug overlay, reset every input state or quit the runtime:
 * those are window and system commands, they belong to the user's own chrome, and a client that could reach
 * them could dismiss the window it was reading. A refusal is `RoboNotSupported` and is **counted**, so
 * "the client asked for something it does not have" is a number rather than a mystery.
 *
 * ## What this is not
 *
 * Not an accessibility tree, not a descendant walk, not a name/role/value protocol, and not a screen
 * reader: Phase 31 owns all of that. What is here is the seam those will call — a semantic request, a
 * command, a privilege rank, a policy, and a router that already exists. Nothing is invented that Phase 31
 * would have to un-invent, which is why the vocabulary is small and the two things this build cannot express
 * (`SET_VALUE`, `TOGGLE`) are named as absences rather than approximated.
 *
 * ## `SET_VALUE` and `TOGGLE`, named
 *
 * `InputA11yCommandSetValue` has no command to map to: 13.13's vocabulary has no "set this control's value",
 * and inventing one here would put a control-specific operation in a platform-neutral list. `TOGGLE` maps to
 * `ACTIVATE`, because activating a thing with two states *is* how its state changes and the control owns the
 * second state. Both facts are asserted by `input.accessibility.mapping`.
 */
#ifndef ROBO_INPUT_ACCESSIBILITY_H
#define ROBO_INPUT_ACCESSIBILITY_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"
#include "RoboInput/RoboInputFocus.h"
#include "RoboInput/RoboInputReplay.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * What an accessibility client is asking for.
 *
 * Stable and positional: a client, a script and a log line all carry these numbers. The order is the brief's.
 */
typedef enum InputA11yCommand {
    /** Nothing: a cleared request's value. Never routed. */
    RoboInputA11yNone = 0,
    /** Act on whatever has focus: press the button, follow the link. → `ACTIVATE`. */
    RoboInputA11yActivate = 1,
    /** Give focus to the client's target. → `FOCUS_DIRECTION` toward it. */
    RoboInputA11yFocus = 2,
    /** Move focus the requested way. → `FOCUS_NEXT` / `FOCUS_PREVIOUS` / `FOCUS_DIRECTION`. */
    RoboInputA11yMoveFocus = 3,
    /** Set a control's value. **No command exists for this** (this header's comment). */
    RoboInputA11ySetValue = 4,
    /** Insert text at the caret. → `INSERT_TEXT`, carrying the text. */
    RoboInputA11yTextInsert = 5,
    /** Delete the logical unit at the caret. → `DELETE_FORWARD` (the character *at* the caret). */
    RoboInputA11yTextDelete = 6,
    /** Select everything. → `SELECT_ALL`. */
    RoboInputA11ySelect = 7,
    /** Scroll by the request's `delta`. → `SCROLL_BY`. */
    RoboInputA11yScroll = 8,
    /** Dismiss: a popup, a pending composition. → `CANCEL`. */
    RoboInputA11yDismiss = 9,
    /** Change a two-state control. → `ACTIVATE` (the control owns its second state). */
    RoboInputA11yToggle = 10
} InputA11yCommand;

/** Number of defined accessibility commands, `NONE` included. Asserted by `input.accessibility.mapping`. */
#define RoboInputA11yCommandCount 11u

/** Non-zero when @p command is defined and not `NONE`. */
int InputA11yCommandIsKnown(InputA11yCommand command);

/** Human-readable name (`"text-insert"`); never NULL; `"unknown"` when undefined. */
const char *InputA11yCommandName(InputA11yCommand command);

/** Room one request's text needs, NUL included. */
#define RoboInputA11yTextMax RoboInputCommandTextMax

/** One accessibility request, by value: what the client wants and the arguments it needs. */
typedef struct InputA11yRequest {
    /** What the client is asking for. */
    InputA11yCommand command;
    /** The text an insert carries; empty when the command needs none. */
    char text[RoboInputA11yTextMax];
    /** A `SCROLL` distance or a `SET_VALUE` value, in the target's own unit. */
    int32_t delta;
    /** A `MOVE_FOCUS` or `FOCUS` direction. */
    InputFocusDirection direction;
} InputA11yRequest;

/** Clear @p request to `NONE` with no arguments. Safe on NULL. */
void InputA11yRequestInit(InputA11yRequest *request);

/** Set @p request's command, clearing its arguments. Refused for an undefined or `NONE` command. */
Result InputA11yRequestSet(InputA11yRequest *request, InputA11yCommand command, InputDiag *diag);

/** Set the text an `INSERT` carries. Refused for any other command, or a fragment too long. */
Result InputA11yRequestSetText(InputA11yRequest *request, const char *text, InputDiag *diag);

/** Set a `SCROLL` distance. Refused for any other command. */
Result InputA11yRequestSetDelta(InputA11yRequest *request, int32_t delta, InputDiag *diag);

/** Set a focus direction. Refused for any command that takes none. */
Result InputA11yRequestSetDirection(InputA11yRequest *request,
                                    InputFocusDirection direction,
                                    InputDiag *diag);

/** Non-zero when @p request is a record this build will route: a defined command with the arguments it takes. */
int InputA11yRequestIsValid(const InputA11yRequest *request);

/* ------------------------------------------------------------------ */
/* The mapping and the policy                                          */
/* ------------------------------------------------------------------ */

/**
 * Map @p command onto the module's command vocabulary.
 *
 * @param outId receives the id; required, and set to `NONE` before any refusal.
 * @return `RoboOk`; `RoboInvalidArgument` for an undefined command or NULL;
 *         `RoboNotSupported` for `SET_VALUE`, which this build's command vocabulary cannot express -- the
 *         refusal the caller sees is the honest one rather than a command that would silently do nothing.
 *         `NONE` maps to `RoboInvalidArgument` rather than to a command: it is a cleared record, not a
 *         request.
 */
Result InputA11yCommandMap(InputA11yCommand command, InputCommandId *outId, InputDiag *diag);

/**
 * Non-zero when an accessibility client may invoke @p id at all.
 *
 * `1` for the text-editing family, the focus family, activation, cancellation and the scrolling family;
 * `0` for the window family and for `TOGGLE_DEBUG_OVERLAY`, `RESET_INPUT_STATE` and `QUIT`. A client that
 * could close the window it was reading would be able to dismiss the thing it was describing.
 */
int InputA11yCommandAllowed(InputCommandId id);

/* ------------------------------------------------------------------ */
/* Invocation                                                          */
/* ------------------------------------------------------------------ */

/** What one request did, for the counters and for a test. */
typedef enum InputA11yOutcome {
    /** Nothing: a cleared record's value. */
    RoboInputA11yOutcomeNone = 0,
    /** A target claimed the command and executed it. */
    RoboInputA11yOutcomeRouted = 1,
    /** The command reached the router and no target claimed it. */
    RoboInputA11yOutcomeUnclaimed = 2,
    /** A target claimed it and failed; the router's own error is what the caller receives. */
    RoboInputA11yOutcomeFailed = 3,
    /** Refused by the policy before any target was asked. */
    RoboInputA11yOutcomeRefused = 4,
    /** No command exists for the request (this header's `SET_VALUE`). */
    RoboInputA11yOutcomeUnsupported = 5
} InputA11yOutcome;

/** Number of defined outcomes. Asserted by `input.accessibility.mapping`. */
#define RoboInputA11yOutcomeCount 6u

/** Non-zero when @p outcome is defined. */
int InputA11yOutcomeIsKnown(InputA11yOutcome outcome);

/** Human-readable outcome name; never NULL; `"unknown"` when undefined. */
const char *InputA11yOutcomeName(InputA11yOutcome outcome);

/** The lane's record: what has been asked, how it ended, and which target answered. */
typedef struct InputA11yState {
    /** Requests that reached the router. */
    uint64_t invokedTotal;
    /** Requests a target claimed and executed. */
    uint64_t routedTotal;
    /** Requests the chain did not claim. */
    uint64_t unclaimedTotal;
    /** Requests a target claimed and failed. */
    uint64_t failedTotal;
    /** Requests refused by `InputA11yCommandAllowed` before anything was asked. */
    uint64_t refusedTotal;
    /** Requests with no command behind them (`SET_VALUE`). */
    uint64_t unsupportedTotal;
    /** Invalid requests. Diagnostics. */
    uint64_t rejectedTotal;
    /** The last outcome, and the level that produced it (-1 when nothing answered). */
    InputA11yOutcome lastOutcome;
    int32_t lastLevel;
    /** The last command a request mapped to, or `NONE`. */
    InputCommandId lastCommand;
    /** The privilege rank this lane acts at, recorded so a log line can say who asked. Always
     *  `RoboInputPrivilegeAccessibility` for a state this module initialised. */
    InputInjectionPrivilege privilege;
} InputA11yState;

/** Clear @p state: no counters, the accessibility rank, no last outcome. Safe on NULL. */
void InputA11yStateInit(InputA11yState *state);

/**
 * Route @p request through @p router as a semantic command.
 *
 * The one door. It maps, applies the policy, fills an @ref InputCommand -- the text for an insert, the
 * direction for a focus move, the delta for a scroll -- and calls `InputCommandRouterRoute`, so an
 * accessibility request reaches exactly the target chain a keyboard shortcut would, with the same
 * innermost-first order and the same "not mine keeps walking" rule.
 *
 * @param outOutcome receives the outcome; optional.
 * @return `RoboOk` when a target claimed the command; `RoboNotFound` when the chain is empty or nobody
 *         claimed it (counted, never silent); the target's own error when one claimed it and failed; and for
 *         a refusal of this layer's own -- an invalid request, a policy refusal, or `SET_VALUE` --
 *         `RoboInvalidArgument` or `RoboNotSupported` with the request untouched on the router's side.
 */
Result InputA11yInvoke(InputA11yState *state,
                       const InputA11yRequest *request,
                       InputCommandRouter *router,
                       InputA11yOutcome *outOutcome,
                       InputDiag *diag);

/**
 * Route an already-built command as an accessibility client.
 *
 * The seam Phase 31 will call: a layer that knows the module's own command vocabulary -- a name/role/value
 * protocol turning a screen reader's "invoke" into a command -- gets the same policy and the same router
 * without going through the request vocabulary above. `InputA11yInvoke` is this door plus a mapping.
 *
 * It is also what makes the policy **checkable** rather than a claim: a caller can hand it `WINDOW_CLOSE` and
 * be refused, which the ten-command vocabulary cannot express (no request maps to a window command).
 *
 * @return `RoboNotSupported` and `refusedTotal` for a command the policy bars -- and the router is never
 *         consulted, so a refused command cannot reach a target by another name; `RoboInvalidArgument` for an
 *         invalid command; otherwise exactly what `InputCommandRouterRoute` answers.
 */
Result InputA11yInvokeCommand(InputA11yState *state,
                              const InputCommand *command,
                              InputCommandRouter *router,
                              InputA11yOutcome *outOutcome,
                              InputDiag *diag);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** Room `InputA11yStateWrite` needs. */
#define RoboInputA11yDumpMax 512u

/**
 * Write @p state as text: summary line plus the last outcome.
 *
 * Complete or absent: `?` and the needed length when the buffer is too small, and the buffer left empty.
 */
Result InputA11yStateWrite(const InputA11yState *state,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_ACCESSIBILITY_H */
