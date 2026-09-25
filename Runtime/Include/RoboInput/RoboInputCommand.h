/*
 * RoboInput commands (Phase 13.12/13.13/13.36) — what the user *meant*, in a vocabulary no device owns.
 *
 * ## Why a command is not a key
 *
 * `Ctrl+C` is a chord; **copy** is a command. The brief separates them and the separation is what makes a menu
 * item, an accessibility action, a command palette entry, a gamepad button, a voice phrase and a scripted test
 * all able to say the same thing without any of them knowing a scan code. So the vocabulary here is
 * *semantic*: it names intentions, and the mapping from a chord to an intention is the **shortcut manager's**
 * (`RoboInputShortcut.h`), which is the only place a `PhysicalKey` appears.
 *
 * ## One vocabulary, four families
 *
 * The ids are positional and appended, never inserted — they are what a saved shortcut table and a log line
 * carry. The families, each with a door that answers "is this one of mine?", are:
 *
 *   **text editing** (13.12)   the half that reaches RoboText's editor. It maps one to one onto
 *                              `RoboTextEditCommand`, and the mapping is a table in `RoboInputTextBridge.c`
 *                              rather than a switch here, because the editor's vocabulary is the editor's.
 *
 *   **focus** (13.26/13.34)    next/previous/directional movement and activation. These need a focus system,
 *                              which is 13.25's; the *command* exists now so a shortcut table written today
 *                              keeps meaning the same thing tomorrow.
 *
 *   **view** (13.19)           scrolling, in units rather than pixels, because a command that said "scroll 40
 *                              px" would be a command that depends on a device's wheel resolution.
 *
 *   **window / system**        close, minimise, the window menu, the debug overlay, a state reset. Named
 *                              because a shortcut table is where a user expects to rebind them.
 *
 * ## What a command carries
 *
 * A command is a **value**: an id and the arguments that id needs, in a fixed-capacity record with no owned
 * memory (`InputCommand`), so it can be copied into a queue, held by a shortcut table or handed to a test.
 * Three arguments cover every id: the **text** an insert or a paste brings, the **delta** a `SCROLL_BY`
 * carries, and the **direction** a focus move names. An id that needs none of them ignores them, and
 * `InputCommandIsValid` refuses the combinations that mean nothing (`INSERT_TEXT` with no text).
 *
 * ## Who executes a command
 *
 * Not this module. A command is *routed*: `InputCommandRouter` holds the focused chain — the innermost target
 * last pushed — and asks each one from the inside out until one answers. A target that is not interested says
 * `RoboNotSupported`, which is *invitation to keep walking* rather than a failure; a target that is interested
 * but cannot comply returns an error, which **stops the walk** and is reported. Nothing about that is
 * DOM-specific: a target is a function and a `userdata`, the same two-field shape the text metrics and font
 * resolution seams use, so a text editor, a focus navigator, a scroller and a document all plug in here.
 *
 * A command no target claims is `RoboNotFound` and is **counted**, because a shortcut that silently does
 * nothing is the single hardest input bug to see.
 */
#ifndef ROBO_INPUT_COMMAND_H
#define ROBO_INPUT_COMMAND_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/** Which way a focus request moves. Also what a `MOVE_*` command's `direction` argument says. */
typedef enum InputFocusDirection {
    /** Nothing: a cleared record's value. */
    RoboInputFocusDirectionNone = 0,
    /** The next target in the scope's order. */
    RoboInputFocusDirectionNext = 1,
    /** The previous one. */
    RoboInputFocusDirectionPrevious = 2,
    RoboInputFocusDirectionUp = 3,
    RoboInputFocusDirectionDown = 4,
    RoboInputFocusDirectionLeft = 5,
    RoboInputFocusDirectionRight = 6,
    /** The first focusable target in the scope, whatever the current one is. */
    RoboInputFocusDirectionFirst = 7,
    /** The last one. */
    RoboInputFocusDirectionLast = 8
} InputFocusDirection;

/** Number of defined directions, `NONE` included. Asserted positionally by `input.command.vocabulary`. */
#define RoboInputFocusDirectionCount 9u

/** Non-zero when @p direction is defined. */
int InputFocusDirectionIsKnown(InputFocusDirection direction);

/** Human-readable direction name; never NULL; `"unknown"` when undefined. */
const char *InputFocusDirectionName(InputFocusDirection direction);

/**
 * What the user meant.
 *
 * The order is **stable and positional**: a shortcut table, a saved recording and a log line all carry these
 * numbers, so a value may be appended and never inserted. The comment beside each says which door classifies
 * it (`InputCommandIdIsTextEdit`, `…IsFocus`, `…IsView`, `…IsWindow`).
 */
typedef enum InputCommandId {
    /** Nothing: a cleared record's value. Never a command a target is asked to execute. */
    RoboInputCommandNone = 0,

    /* ---- text editing (13.12) — the half RoboText's editor executes ---- */
    /** Insert @ref InputCommand::text at the caret, replacing the selection. **IsTextEdit.** */
    RoboInputCommandInsertText = 1,
    /** Delete the logical unit before the caret. */
    RoboInputCommandDeleteBackward = 2,
    /** Delete the logical unit after the caret. */
    RoboInputCommandDeleteForward = 3,
    /** Move the caret one unit left, collapsing the selection. */
    RoboInputCommandMoveLeft = 4,
    /** Move the caret one unit right. */
    RoboInputCommandMoveRight = 5,
    /** Move the caret one **visual** line up. Needs a layout: refused by the editor (12.22). */
    RoboInputCommandMoveUp = 6,
    /** Move the caret one visual line down. See @ref RoboInputCommandMoveUp. */
    RoboInputCommandMoveDown = 7,
    /** Move the caret to the start of its line. Needs a layout: refused by the editor. */
    RoboInputCommandMoveLineStart = 8,
    /** Move the caret to the end of its line. See @ref RoboInputCommandMoveLineStart. */
    RoboInputCommandMoveLineEnd = 9,
    /** Move the caret to the start of the document. Expressed through the editor's caret door. */
    RoboInputCommandMoveDocumentStart = 10,
    /** Move the caret to the end of the document. See @ref RoboInputCommandMoveDocumentStart. */
    RoboInputCommandMoveDocumentEnd = 11,
    /** Extend the selection one unit left. */
    RoboInputCommandSelectLeft = 12,
    /** Extend the selection one unit right. */
    RoboInputCommandSelectRight = 13,
    /** Select everything. */
    RoboInputCommandSelectAll = 14,
    /** Read the selection. **The clipboard owns what happens to it** (13.41); the editor hands it out. */
    RoboInputCommandCopy = 15,
    /** Read the selection and delete it. A cut that did not read first would lose the text. */
    RoboInputCommandCut = 16,
    /** Insert the text a caller already holds. A paste is an insert. */
    RoboInputCommandPaste = 17,
    /** Undo. The editor has the *record*; the history is still 12.23's, so this is refused by name. */
    RoboInputCommandUndo = 18,
    /** Redo. See @ref RoboInputCommandUndo. */
    RoboInputCommandRedo = 19,

    /* ---- focus and activation (13.26/13.34) ---- */
    /** Move focus to the next target in the current scope. **IsFocus.** */
    RoboInputCommandFocusNext = 20,
    /** Move focus to the previous target. */
    RoboInputCommandFocusPrevious = 21,
    /** Move focus in @ref InputCommand::direction. */
    RoboInputCommandFocusDirection = 22,
    /** Activate whatever is focused: a button presses, a link follows, `Enter` means this. */
    RoboInputCommandActivate = 23,
    /** Cancel or dismiss: `Escape`, a pending composition, a popup. */
    RoboInputCommandCancel = 24,
    /** Go back: a history step where the target has one, otherwise close the transient surface. */
    RoboInputCommandBack = 25,

    /* ---- view (13.19) ---- */
    /** Scroll one line up. **IsView.** */
    RoboInputCommandScrollLineUp = 26,
    RoboInputCommandScrollLineDown = 27,
    /** Scroll one page up. */
    RoboInputCommandScrollPageUp = 28,
    RoboInputCommandScrollPageDown = 29,
    RoboInputCommandScrollLineLeft = 30,
    RoboInputCommandScrollLineRight = 31,
    /** Scroll by @ref InputCommand::delta, whose unit the target chooses. */
    RoboInputCommandScrollBy = 32,

    /* ---- window and system ---- */
    /** Close the active window. **IsWindow.** */
    RoboInputCommandWindowClose = 33,
    RoboInputCommandWindowMinimize = 34,
    RoboInputCommandWindowToggleMaximize = 35,
    /** Open the active window's menu, or the menu bar. */
    RoboInputCommandWindowMenu = 36,

    /** Toggle the input debug overlay (13.56). **Not** a debug-only command: it is bindable on purpose. */
    RoboInputCommandToggleDebugOverlay = 37,
    /** Clear every input state (13.78): held keys, captures, contacts, gestures, the pending accent. */
    RoboInputCommandResetInputState = 38,
    /** Ask the runtime to quit. */
    RoboInputCommandQuit = 39
} InputCommandId;

/** Number of defined commands, `NONE` included. Asserted positionally by `input.command.vocabulary`. */
#define RoboInputCommandCount 40u

/** The id one past the last text-editing command: a range test rather than forty comparisons. */
#define RoboInputCommandTextEditLast RoboInputCommandRedo
/** The id one past the last focus command. */
#define RoboInputCommandFocusLast RoboInputCommandBack
/** The id one past the last view command. */
#define RoboInputCommandViewLast RoboInputCommandScrollBy
/** The id one past the last window command. */
#define RoboInputCommandWindowLast RoboInputCommandWindowMenu

/** Non-zero when @p id is a defined command other than `NONE`. */
int InputCommandIdIsKnown(InputCommandId id);

/** Human-readable command name (`"copy"`); never NULL; `"unknown"` when undefined. */
const char *InputCommandIdName(InputCommandId id);

/** Non-zero when @p id is one of the editor's — the family `RoboInputTextBridge.h` executes. */
int InputCommandIdIsTextEdit(InputCommandId id);

/** Non-zero when @p id moves focus or activates. */
int InputCommandIdIsFocus(InputCommandId id);

/** Non-zero when @p id scrolls. */
int InputCommandIdIsView(InputCommandId id);

/** Non-zero when @p id acts on the active window. */
int InputCommandIdIsWindow(InputCommandId id);

/* ------------------------------------------------------------------ */
/* A command and its arguments                                         */
/* ------------------------------------------------------------------ */

/** Room an `INSERT_TEXT` or `PASTE` command's text may take, NUL included. */
#define RoboInputCommandTextMax RoboInputTextMax

/** One command, by value: an id and the three arguments an id might need. */
typedef struct InputCommand {
    /** What the user meant. */
    InputCommandId id;
    /** The text an insert or a paste brings; empty when the id needs none. */
    char text[RoboInputCommandTextMax];
    /** A `SCROLL_BY` distance, in a unit the target chooses (a line, a pixel — its business). */
    int32_t delta;
    /** A focus move's direction. */
    InputFocusDirection direction;
} InputCommand;

/** Clear @p command: no id, no text, no arguments. Safe on NULL. */
void InputCommandInit(InputCommand *command);

/** Set only the id, clearing every argument. Refused for an undefined or `NONE` id. */
Result InputCommandSet(InputCommand *command, InputCommandId id, InputDiag *diag);

/** Set the text an insert or a paste carries. Refused for an id that takes none, or a fragment too long. */
Result InputCommandSetText(InputCommand *command, const char *text, InputDiag *diag);

/** Set a `SCROLL_BY` distance. Refused for any other id. */
Result InputCommandSetDelta(InputCommand *command, int32_t delta, InputDiag *diag);

/** Set a focus move's direction. Refused for any other id. */
Result InputCommandSetDirection(InputCommand *command,
                               InputFocusDirection direction,
                               InputDiag *diag);

/** Non-zero when @p command is a record a router may be asked to execute. */
int InputCommandIsValid(const InputCommand *command);

/** Copy @p text out of a command; never NULL. Naming it is what a diagnostics line reads. */
const char *InputCommandText(const InputCommand *command);

/** Field-by-field equality, text compared as a string. Safe on NULL; two NULLs are equal. */
int InputCommandEqual(const InputCommand *a, const InputCommand *b);

/* ------------------------------------------------------------------ */
/* Targets and the router                                              */
/* ------------------------------------------------------------------ */

/**
 * Something that can execute a command: a function and a `userdata`, and nothing else.
 *
 * The two-field shape is deliberate and is the same one the text metrics and font resolution seams use, because
 * it is the smallest honest answer to "who does this?" — a vtable here would need a lifetime, an owner and a
 * registration protocol, and a module that owns nothing would suddenly own a registry.
 *
 * `execute` returns:
 *
 *   `RoboOk`             claimed and done (or claimed and a legitimate no-op);
 *   `RoboNotSupported`   **not mine** — the walk continues;
 *   anything else        mine, and it failed — the walk **stops** and the error is reported.
 */
typedef struct InputCommandTarget {
    /** Whatever the function needs; borrowed, and never freed here. */
    void *userdata;
    /** Execute @p command, or decline it. */
    Result (*execute)(void *userdata, const InputCommand *command, InputDiag *diag);
} InputCommandTarget;

/** Clear @p target: no function, no userdata. Non-zero when it is usable. Safe on NULL. */
void InputCommandTargetInit(InputCommandTarget *target);
int InputCommandTargetIsValid(const InputCommandTarget *target);

/**
 * The focused chain, innermost last.
 *
 * Push the **outermost** target first: a window's target, then a document's, then the focused control's. The
 * last one pushed is asked first, which is what makes "a focused text field consumes `Ctrl+A` before the window
 * does" a property of the order rather than a special case.
 *
 * Held by value with a stated bound, like the device registry and the layout: nothing to destroy, no allocator,
 * and a chain deeper than the bound is refused rather than truncated (a silently dropped target is a command
 * that reaches the wrong one).
 */
#define RoboInputCommandMaxTargets 32u

typedef struct InputCommandRouter {
    /** The chain, outermost first. */
    InputCommandTarget targets[RoboInputCommandMaxTargets];
    /** How many are live. */
    uint32_t count;
    /** Commands a target claimed. Diagnostics. */
    uint64_t routedTotal;
    /** Commands no target claimed: the number that says whether a shortcut ever worked. */
    uint64_t unclaimedTotal;
    /** Commands a target claimed and failed. Diagnostics. */
    uint64_t failedTotal;
} InputCommandRouter;

/** Clear @p router. Safe on NULL. */
void InputCommandRouterInit(InputCommandRouter *router);

/** Push @p target as the innermost. Refused for an invalid target or a full chain. */
Result InputCommandRouterPush(InputCommandRouter *router,
                              const InputCommandTarget *target,
                              InputDiag *diag);

/** Drop the innermost target. Refused when the chain is empty. */
Result InputCommandRouterPop(InputCommandRouter *router, InputDiag *diag);

/** Drop every target. The counters are kept: a router is reset, not replaced. */
void InputCommandRouterClear(InputCommandRouter *router);

/** How many targets the chain holds. 0 for NULL. */
uint32_t InputCommandRouterDepth(const InputCommandRouter *router);

/** The target at @p level, 0 being the innermost. Refused past the end. */
Result InputCommandRouterTargetAt(const InputCommandRouter *router,
                                  uint32_t level,
                                  InputCommandTarget *out,
                                  InputDiag *diag);

/**
 * Ask the chain, innermost first, until one claims @p command.
 *
 * @param outLevel receives the **level** (0 = innermost) that claimed it, or -1 when none did. Optional.
 * @return `RoboOk` when a target claimed it; `RoboNotFound` when the chain is empty or nobody claimed it (the
 *         command is then *counted*, not silently dropped); the target's own error when one claimed it and
 *         failed.
 *
 * Refused for `NONE`, an invalid command, or a NULL router/command.
 */
Result InputCommandRouterRoute(InputCommandRouter *router,
                               const InputCommand *command,
                               int32_t *outLevel,
                               InputDiag *diag);

/**
 * Room `InputCommandRouterWrite` needs, NUL included: a summary line plus one line per target.
 */
#define RoboInputCommandRouterDumpMax (128u + (RoboInputCommandMaxTargets * 64u))

/** Write @p router as text: a summary line and the chain innermost first. `?` when it does not fit. */
Result InputCommandRouterWrite(const InputCommandRouter *router,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag);

/** Room `InputCommandWrite` needs: the id, its family, and whichever argument is present. */
#define RoboInputCommandDumpMax 160u

/**
 * Write @p command as one line:
 *
 *     command copy text= paste=0 delta=0 direction=none
 *
 * `?` and the needed length when the buffer is too small.
 */
Result InputCommandWrite(const InputCommand *command,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_COMMAND_H */
