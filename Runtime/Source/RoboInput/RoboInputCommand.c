/*
 * RoboInput commands (Phase 13.12/13.13/13.36) — see `RoboInputCommand.h`.
 *
 * Three pieces:
 *
 *   **the vocabulary**   names, the four family tests (each a range test rather than forty comparisons) and
 *                        the direction names;
 *   **the record**       one command as a value, with the *argument rule* stated once: an id takes the
 *                        arguments its family takes and refuses the rest, so `MOVE_LEFT` with a text argument
 *                        is a caller's bug rather than a field nothing reads;
 *   **the router**       a bounded chain of two-field targets asked innermost-first, where "not mine" is
 *                        `RoboNotSupported` and continues the walk while a real failure stops it.
 *
 * Nothing here includes an editor, a window or a document: a command is a value and a target is a function, and
 * who can execute what is the caller's assembly (`RoboInputTextBridge.c` is the one concrete target this build
 * ships).
 */

#include "RoboInput/RoboInputCommand.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static const char *const inputCommandNames[RoboInputCommandCount] = {
    "none",
    "insert_text",
    "delete_backward",
    "delete_forward",
    "move_left",
    "move_right",
    "move_up",
    "move_down",
    "move_line_start",
    "move_line_end",
    "move_document_start",
    "move_document_end",
    "select_left",
    "select_right",
    "select_all",
    "copy",
    "cut",
    "paste",
    "undo",
    "redo",
    "focus_next",
    "focus_previous",
    "focus_direction",
    "activate",
    "cancel",
    "back",
    "scroll_line_up",
    "scroll_line_down",
    "scroll_page_up",
    "scroll_page_down",
    "scroll_line_left",
    "scroll_line_right",
    "scroll_by",
    "window_close",
    "window_minimize",
    "window_toggle_maximize",
    "window_menu",
    "toggle_debug_overlay",
    "reset_input_state",
    "quit"
};

int InputFocusDirectionIsKnown(InputFocusDirection direction)
{
    return (int)direction > (int)RoboInputFocusDirectionNone &&
           (int)direction < (int)RoboInputFocusDirectionCount;
}

const char *InputFocusDirectionName(InputFocusDirection direction)
{
    switch (direction) {
    case RoboInputFocusDirectionNone: return "none";
    case RoboInputFocusDirectionNext: return "next";
    case RoboInputFocusDirectionPrevious: return "previous";
    case RoboInputFocusDirectionUp: return "up";
    case RoboInputFocusDirectionDown: return "down";
    case RoboInputFocusDirectionLeft: return "left";
    case RoboInputFocusDirectionRight: return "right";
    case RoboInputFocusDirectionFirst: return "first";
    case RoboInputFocusDirectionLast: return "last";
    default: break;
    }
    return "unknown";
}

int InputCommandIdIsKnown(InputCommandId id)
{
    return (int)id > (int)RoboInputCommandNone && (int)id < (int)RoboInputCommandCount;
}

const char *InputCommandIdName(InputCommandId id)
{
    /* Bounded by the **count**, not by `IsKnown`: `NONE` is a name a dump prints (`"none"` for a cleared
     * record) while `IsKnown` answers the different question "could a target be asked to execute this?". Every
     * other vocabulary in this module separates the two the same way. */
    if ((int)id < 0 || (int)id >= (int)RoboInputCommandCount) {
        return "unknown";
    }
    return inputCommandNames[(size_t)id];
}

/* The four families are **contiguous ranges**, which is why each test is one comparison pair rather than a
 * table: a family that grew would be a range that grew, and a command that moved families would be a command
 * whose neighbours changed — both of which a reader sees in the header's own ordering. */
int InputCommandIdIsTextEdit(InputCommandId id)
{
    return (int)id >= (int)RoboInputCommandInsertText && (int)id <= (int)RoboInputCommandTextEditLast;
}

int InputCommandIdIsFocus(InputCommandId id)
{
    return (int)id >= (int)RoboInputCommandFocusNext && (int)id <= (int)RoboInputCommandFocusLast;
}

int InputCommandIdIsView(InputCommandId id)
{
    return (int)id >= (int)RoboInputCommandScrollLineUp && (int)id <= (int)RoboInputCommandViewLast;
}

int InputCommandIdIsWindow(InputCommandId id)
{
    return (int)id >= (int)RoboInputCommandWindowClose && (int)id <= (int)RoboInputCommandWindowLast;
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void InputCommandInit(InputCommand *command)
{
    if (!command) {
        return;
    }
    command->id = RoboInputCommandNone;
    command->text[0] = '\0';
    command->delta = 0;
    command->direction = RoboInputFocusDirectionNone;
}

Result InputCommandSet(InputCommand *command, InputCommandId id, InputDiag *diag)
{
    if (!command) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "nowhere to put the command");
    }
    /* Cleared **first**, so a refusal leaves an empty record rather than a mixture of the refused call and the
     * previous accepted one -- the rule every door in this module keeps. */
    InputCommandInit(command);
    /* `NONE` is refused rather than stored: a target must never be asked to execute nothing, because a walk
     * that asked would report a claimed command that did nothing. */
    if (!InputCommandIdIsKnown(id)) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "that command id is not one this build defines");
    }
    command->id = id;
    return RoboOk;
}

Result InputCommandSetText(InputCommand *command, const char *text, InputDiag *diag)
{
    if (!command) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "nowhere to put the command");
    }
    if (command->id != RoboInputCommandInsertText && command->id != RoboInputCommandPaste) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "only an insert or a paste carries text");
    }
    if (!RiTextFits(text, RoboInputCommandTextMax)) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "the command's text is longer than one command may carry");
    }
    RiTextCopy(command->text, RoboInputCommandTextMax, text);
    return RoboOk;
}

Result InputCommandSetDelta(InputCommand *command, int32_t delta, InputDiag *diag)
{
    if (!command) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "nowhere to put the command");
    }
    if (command->id != RoboInputCommandScrollBy) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "only a scroll-by command carries a distance");
    }
    command->delta = delta;
    return RoboOk;
}

Result InputCommandSetDirection(InputCommand *command,
                               InputFocusDirection direction,
                               InputDiag *diag)
{
    if (!command) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "nowhere to put the command");
    }
    if (command->id != RoboInputCommandFocusDirection) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "only a directional focus command carries a direction");
    }
    if (!InputFocusDirectionIsKnown(direction)) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "that focus direction is not one this build defines");
    }
    command->direction = direction;
    return RoboOk;
}

int InputCommandIsValid(const InputCommand *command)
{
    if (!command || !InputCommandIdIsKnown(command->id)) {
        return 0;
    }
    if (!RiTextFits(command->text, RoboInputCommandTextMax)) {
        return 0;
    }
    switch (command->id) {
    case RoboInputCommandInsertText:
        /* An insert with nothing to insert is not a command: it is a keystroke that produced no text, and the
         * pipeline (13.11) already refuses to produce one. */
        return command->text[0] != '\0';
    case RoboInputCommandPaste:
        /* A paste of an empty clipboard *is* a legitimate call -- the clipboard was empty -- and the editor
         * answers it as a no-op rather than the router failing to find a target. */
        return 1;
    case RoboInputCommandFocusDirection:
        return InputFocusDirectionIsKnown(command->direction);
    default:
        return 1;
    }
}

const char *InputCommandText(const InputCommand *command)
{
    return command ? command->text : "";
}

int InputCommandEqual(const InputCommand *a, const InputCommand *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->id == b->id && a->delta == b->delta && a->direction == b->direction &&
           strcmp(a->text, b->text) == 0;
}

/* ------------------------------------------------------------------ */
/* Targets                                                             */
/* ------------------------------------------------------------------ */

void InputCommandTargetInit(InputCommandTarget *target)
{
    if (!target) {
        return;
    }
    target->userdata = NULL;
    target->execute = NULL;
}

int InputCommandTargetIsValid(const InputCommandTarget *target)
{
    /* A target with no function is not a target: a chain that contained one would walk *through* it and report
     * the level of a target that answered nothing. */
    return target != NULL && target->execute != NULL;
}

/* ------------------------------------------------------------------ */
/* The router                                                          */
/* ------------------------------------------------------------------ */

void InputCommandRouterInit(InputCommandRouter *router)
{
    uint32_t index;

    if (!router) {
        return;
    }
    for (index = 0u; index < RoboInputCommandMaxTargets; index++) {
        InputCommandTargetInit(&router->targets[index]);
    }
    router->count = 0u;
    router->routedTotal = 0u;
    router->unclaimedTotal = 0u;
    router->failedTotal = 0u;
}

uint32_t InputCommandRouterDepth(const InputCommandRouter *router)
{
    return router ? router->count : 0u;
}

Result InputCommandRouterPush(InputCommandRouter *router,
                              const InputCommandTarget *target,
                              InputDiag *diag)
{
    if (!router || !target) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "a router and a target are both needed");
    }
    if (!InputCommandTargetIsValid(target)) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "the target has no function to call");
    }
    if (router->count >= RoboInputCommandMaxTargets) {
        /* Refused rather than truncated: a silently dropped target is a command that reaches the *wrong*
         * one, which is worse than a refusal a caller sees. */
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboNotSupported,
                             "the command chain is full");
    }
    router->targets[router->count] = *target;
    router->count++;
    return RoboOk;
}

Result InputCommandRouterPop(InputCommandRouter *router, InputDiag *diag)
{
    if (!router) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "no router to pop from");
    }
    if (router->count == 0u) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "the command chain is already empty");
    }
    router->count--;
    InputCommandTargetInit(&router->targets[router->count]);
    return RoboOk;
}

void InputCommandRouterClear(InputCommandRouter *router)
{
    uint32_t index;

    if (!router) {
        return;
    }
    for (index = 0u; index < router->count; index++) {
        InputCommandTargetInit(&router->targets[index]);
    }
    router->count = 0u;
}

Result InputCommandRouterTargetAt(const InputCommandRouter *router,
                                  uint32_t level,
                                  InputCommandTarget *out,
                                  InputDiag *diag)
{
    if (!router || !out) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "a router and somewhere to put the target are both needed");
    }
    if (level >= router->count) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboNotFound,
                             "no target at that level");
    }
    /* `level` counts from the **inside out**, because that is the order the router asks in and the order a
     * caller debugging a command reads -- the focused control is level 0, whichever end of the array it
     * happens to live at. */
    *out = router->targets[router->count - 1u - level];
    return RoboOk;
}

Result InputCommandRouterRoute(InputCommandRouter *router,
                               const InputCommand *command,
                               int32_t *outLevel,
                               InputDiag *diag)
{
    uint32_t level;

    if (outLevel) {
        *outLevel = -1;
    }
    if (!router || !command) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "a router and a command are both needed");
    }
    if (!InputCommandIsValid(command)) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "the command is not one a target may be asked to execute");
    }

    for (level = 0u; level < router->count; level++) {
        const InputCommandTarget *target = &router->targets[router->count - 1u - level];
        Result result = target->execute(target->userdata, command, diag);

        if (result == RoboNotSupported) {
            /* Not mine: keep walking outwards. This is what lets a window's target, a document's target and a
             * focused control's target share one chain without any of them knowing about a command that is not
             * its own. */
            continue;
        }
        if (result != RoboOk) {
            /* Mine, and it failed: stop, because handing the same command to a broader target afterwards would
             * undo the work the failing one may have already done. */
            router->failedTotal++;
            return result;
        }
        router->routedTotal++;
        if (outLevel) {
            *outLevel = (int32_t)level;
        }
        return RoboOk;
    }

    router->unclaimedTotal++;
    return RoboInputDiag(diag, RoboInputDomainCommand, RoboNotFound,
                         "no target in the chain claimed the command");
}

/* ------------------------------------------------------------------ */
/* Dumps                                                              */
/* ------------------------------------------------------------------ */

Result InputCommandWrite(const InputCommand *command,
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
    if (!command) {
        RiWriterText(&text, "command <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterFieldText(&text, "command", InputCommandIdName(command->id));
    RiWriterFieldU64(&text, " text_edit", InputCommandIdIsTextEdit(command->id) ? 1u : 0u);
    RiWriterFieldU64(&text, " focus", InputCommandIdIsFocus(command->id) ? 1u : 0u);
    RiWriterFieldU64(&text, " view", InputCommandIdIsView(command->id) ? 1u : 0u);
    RiWriterFieldU64(&text, " window", InputCommandIdIsWindow(command->id) ? 1u : 0u);
    RiWriterText(&text, " text=\"");
    RiWriterText(&text, command->text);
    RiWriterChar(&text, '"');
    RiWriterFieldI32(&text, " delta", command->delta);
    RiWriterFieldText(&text, " direction", InputFocusDirectionName(command->direction));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}

Result InputCommandRouterWrite(const InputCommandRouter *router,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag)
{
    InputWriter text;
    uint32_t level;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!router) {
        RiWriterText(&text, "router <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterFieldU64(&text, "router depth", (uint64_t)router->count);
    RiWriterFieldU64(&text, " routed", router->routedTotal);
    RiWriterFieldU64(&text, " unclaimed", router->unclaimedTotal);
    RiWriterFieldU64(&text, " failed", router->failedTotal);
    RiWriterChar(&text, '\n');
    /* The chain **innermost first**, which is the order a command is asked in -- a dump that listed the array
     * order would make a reader reverse it in their head to see why a command reached the wrong target. */
    for (level = 0u; level < router->count; level++) {
        const InputCommandTarget *target = &router->targets[router->count - 1u - level];

        RiWriterText(&text, "target ");
        RiWriterU64(&text, (uint64_t)level);
        RiWriterFieldU64(&text, " userdata", (uint64_t)(uintptr_t)target->userdata);
        RiWriterFieldU64(&text, " function", (uint64_t)(uintptr_t)target->execute);
        RiWriterChar(&text, '\n');
    }
    return RiWriterFinish(&text, outLength, diag);
}
