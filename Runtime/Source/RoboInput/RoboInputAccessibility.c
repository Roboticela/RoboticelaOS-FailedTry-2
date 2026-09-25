/*
 * Accessibility input hooks (Phase 13.52) — see `RoboInputAccessibility.h`.
 *
 * Three pieces: the request vocabulary (values and their argument rules), the mapping plus the policy (which
 * command, and whether this lane may reach it), and `InputA11yInvoke`, which is the only place anything is
 * routed.
 *
 * `InputA11yInvoke` deliberately does **not** build an `InputEvent`. The lane's whole point is that it speaks
 * meanings rather than hardware, and a synthetic keypress would arrive at a target that had to guess a
 * layout, a modifier and a repeat state. What it builds is the same `InputCommand` a shortcut resolves to,
 * handed to the same router.
 */

#include "RoboInput/RoboInputAccessibility.h"

#include "RoboInputInternal.h"

#include <string.h>

static const char *const RiA11yCommandNames[RoboInputA11yCommandCount] = {
    "none",
    "activate",
    "focus",
    "move-focus",
    "set-value",
    "text-insert",
    "text-delete",
    "select",
    "scroll",
    "dismiss",
    "toggle"
};

static const char *const RiA11yOutcomeNames[RoboInputA11yOutcomeCount] = {
    "none",
    "routed",
    "unclaimed",
    "failed",
    "refused",
    "unsupported"
};

int InputA11yCommandIsKnown(InputA11yCommand command)
{
    return command != RoboInputA11yNone && (int)command > 0 &&
           (size_t)command < (size_t)RoboInputA11yCommandCount;
}

const char *InputA11yCommandName(InputA11yCommand command)
{
    if ((int)command < 0 || (size_t)command >= (size_t)RoboInputA11yCommandCount) {
        return "unknown";
    }
    return RiA11yCommandNames[(size_t)command];
}

int InputA11yOutcomeIsKnown(InputA11yOutcome outcome)
{
    return (int)outcome >= 0 && (size_t)outcome < (size_t)RoboInputA11yOutcomeCount;
}

const char *InputA11yOutcomeName(InputA11yOutcome outcome)
{
    if (!InputA11yOutcomeIsKnown(outcome)) {
        return "unknown";
    }
    return RiA11yOutcomeNames[(size_t)outcome];
}

/* ------------------------------------------------------------------ */
/* The request                                                         */
/* ------------------------------------------------------------------ */

void InputA11yRequestInit(InputA11yRequest *request)
{
    if (!request) {
        return;
    }
    memset(request, 0, sizeof(*request));
    request->command = RoboInputA11yNone;
    request->direction = RoboInputFocusDirectionNone;
}

Result InputA11yRequestSet(InputA11yRequest *request, InputA11yCommand command, InputDiag *diag)
{
    if (!request) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "a request needs somewhere to live");
    }
    if (!InputA11yCommandIsKnown(command)) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "an undefined accessibility command is not a request");
    }
    InputA11yRequestInit(request);
    request->command = command;
    return RoboOk;
}

Result InputA11yRequestSetText(InputA11yRequest *request, const char *text, InputDiag *diag)
{
    if (!request || !text) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "setting text needs a request and a string");
    }
    if (request->command != RoboInputA11yTextInsert) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "only an insert carries text; the command must be set first");
    }
    if (!RiTextFits(text, RoboInputA11yTextMax)) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "the text is longer than one insert may carry");
    }
    RiTextCopy(request->text, sizeof(request->text), text);
    return RoboOk;
}

Result InputA11yRequestSetDelta(InputA11yRequest *request, int32_t delta, InputDiag *diag)
{
    if (!request) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "a scroll needs somewhere to live");
    }
    if (request->command != RoboInputA11yScroll) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "only a scroll carries a delta; the command must be set first");
    }
    request->delta = delta;
    return RoboOk;
}

Result InputA11yRequestSetDirection(InputA11yRequest *request,
                                    InputFocusDirection direction,
                                    InputDiag *diag)
{
    if (!request) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "a focus request needs somewhere to live");
    }
    if (!InputFocusDirectionIsKnown(direction)) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "an undefined direction is not a focus move");
    }
    if (request->command != RoboInputA11yMoveFocus && request->command != RoboInputA11yFocus) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "only a focus request carries a direction; the command must be set first");
    }
    request->direction = direction;
    return RoboOk;
}

int InputA11yRequestIsValid(const InputA11yRequest *request)
{
    if (!request || !InputA11yCommandIsKnown(request->command)) {
        return 0;
    }
    if (!RiTextFits(request->text, sizeof(request->text))) {
        return 0;
    }
    if (request->direction != RoboInputFocusDirectionNone &&
        !InputFocusDirectionIsKnown(request->direction)) {
        return 0;
    }
    /* An argument where the command takes none is the defect this checks for: text on a scroll, a delta on an
     * insert, a direction on a dismiss. Each case states the arguments its command *owns*, and nothing is
     * ignored, because an ignored argument is a request that meant something the layer did not do. */
    switch (request->command) {
    case RoboInputA11yFocus:
    case RoboInputA11yMoveFocus:
        return request->text[0] == '\0' && request->delta == 0;
    case RoboInputA11yTextInsert:
        return request->delta == 0 && request->direction == RoboInputFocusDirectionNone;
    case RoboInputA11yScroll:
        return request->text[0] == '\0' && request->direction == RoboInputFocusDirectionNone;
    default:
        return request->text[0] == '\0' && request->delta == 0 &&
               request->direction == RoboInputFocusDirectionNone;
    }
}

/* ------------------------------------------------------------------ */
/* The mapping and the policy                                          */
/* ------------------------------------------------------------------ */

Result InputA11yCommandMap(InputA11yCommand command, InputCommandId *outId, InputDiag *diag)
{
    if (!outId) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "a mapping needs somewhere to put the command");
    }
    *outId = RoboInputCommandNone;

    switch (command) {
    case RoboInputA11yActivate:
        *outId = RoboInputCommandActivate;
        return RoboOk;
    case RoboInputA11yFocus:
    case RoboInputA11yMoveFocus:
        /* Both become a direction move: a focus request names the direction it wants, and the four
         * directional commands are the vocabulary for it. A request with no direction asks for the next
         * target, which is what `TAB` means to every focus system this tree has. */
        *outId = RoboInputCommandFocusDirection;
        return RoboOk;
    case RoboInputA11yTextInsert:
        *outId = RoboInputCommandInsertText;
        return RoboOk;
    case RoboInputA11yTextDelete:
        *outId = RoboInputCommandDeleteForward;
        return RoboOk;
    case RoboInputA11ySelect:
        *outId = RoboInputCommandSelectAll;
        return RoboOk;
    case RoboInputA11yScroll:
        *outId = RoboInputCommandScrollBy;
        return RoboOk;
    case RoboInputA11yDismiss:
        *outId = RoboInputCommandCancel;
        return RoboOk;
    case RoboInputA11yToggle:
        *outId = RoboInputCommandActivate;
        return RoboOk;
    case RoboInputA11ySetValue:
        /* Named, not approximated. 13.13's vocabulary has no "set this control's value", and a control's
         * value is not a platform-neutral meaning. */
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboNotSupported,
                             "the command vocabulary has no set-value operation; the control or its owner "
                             "must expose one");
    default:
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "an undefined or cleared accessibility command has no command behind it");
    }
}

int InputA11yCommandAllowed(InputCommandId id)
{
    if (!InputCommandIdIsKnown(id)) {
        return 0;
    }
    /* The window family and the system commands are the other half of the boundary. A client that could
     * reach `WINDOW_CLOSE` could dismiss the surface it was describing; a client that could reach `QUIT`
     * could end the session. */
    if (InputCommandIdIsWindow(id)) {
        return 0;
    }
    if (id == RoboInputCommandToggleDebugOverlay || id == RoboInputCommandResetInputState ||
        id == RoboInputCommandQuit) {
        return 0;
    }
    return InputCommandIdIsTextEdit(id) || InputCommandIdIsFocus(id) || InputCommandIdIsView(id);
}

/* ------------------------------------------------------------------ */
/* Invoke                                                              */
/* ------------------------------------------------------------------ */

void InputA11yStateInit(InputA11yState *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->privilege = RoboInputPrivilegeAccessibility;
    state->lastOutcome = RoboInputA11yOutcomeNone;
    state->lastLevel = -1;
    state->lastCommand = RoboInputCommandNone;
}

/** Record an outcome and answer with @p result. */
static Result RiA11yAnswer(InputA11yState *state,
                           InputA11yOutcome outcome,
                           int32_t level,
                           Result result,
                           InputA11yOutcome *outOutcome)
{
    if (state) {
        state->lastOutcome = outcome;
        state->lastLevel = level;
    }
    if (outOutcome) {
        *outOutcome = outcome;
    }
    return result;
}

Result InputA11yInvoke(InputA11yState *state,
                       const InputA11yRequest *request,
                       InputCommandRouter *router,
                       InputA11yOutcome *outOutcome,
                       InputDiag *diag)
{
    InputCommandId id = RoboInputCommandNone;
    InputCommand command;
    Result result;

    if (outOutcome) {
        *outOutcome = RoboInputA11yOutcomeNone;
    }
    if (!state || !router) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "an accessibility request needs a state and a router");
    }
    if (!InputA11yRequestIsValid(request)) {
        state->rejectedTotal++;
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "the request is not a record this build routes");
    }

    result = InputA11yCommandMap(request->command, &id, diag);
    if (result != RoboOk) {
        state->unsupportedTotal++;
        state->lastOutcome = RoboInputA11yOutcomeUnsupported;
        state->lastLevel = -1;
        state->lastCommand = RoboInputCommandNone;
        if (outOutcome) {
            *outOutcome = RoboInputA11yOutcomeUnsupported;
        }
        return result;
    }

    InputCommandInit(&command);
    result = InputCommandSet(&command, id, diag);
    if (result != RoboOk) {
        state->rejectedTotal++;
        return result;
    }

    switch (request->command) {
    case RoboInputA11yTextInsert:
        result = InputCommandSetText(&command, request->text, diag);
        break;
    case RoboInputA11yScroll:
        result = InputCommandSetDelta(&command, request->delta, diag);
        break;
    case RoboInputA11yFocus:
    case RoboInputA11yMoveFocus:
        /* No direction means "the next one", which is the meaning `TAB` has everywhere else. */
        result = InputCommandSetDirection(&command,
                                          request->direction == RoboInputFocusDirectionNone
                                              ? RoboInputFocusDirectionNext
                                              : request->direction,
                                          diag);
        break;
    default:
        result = RoboOk;
        break;
    }
    if (result != RoboOk) {
        state->rejectedTotal++;
        return result;
    }

    return InputA11yInvokeCommand(state, &command, router, outOutcome, diag);
}

Result InputA11yInvokeCommand(InputA11yState *state,
                              const InputCommand *command,
                              InputCommandRouter *router,
                              InputA11yOutcome *outOutcome,
                              InputDiag *diag)
{
    Result result;
    int32_t level = -1;

    if (outOutcome) {
        *outOutcome = RoboInputA11yOutcomeNone;
    }
    if (!state || !router || !command) {
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "a routed command needs a state, a command and a router");
    }
    if (!InputCommandIsValid(command)) {
        state->rejectedTotal++;
        return RoboInputDiag(diag, RoboInputDomainAccessibility, RoboInvalidArgument,
                             "the command is not a record this build routes");
    }

    /* The policy, before the router: a refused command cannot reach a target by another name, and the
     * refusal is counted rather than merely answered. */
    if (!InputA11yCommandAllowed(command->id)) {
        state->refusedTotal++;
        state->lastCommand = command->id;
        result = RoboInputDiag(diag, RoboInputDomainAccessibility, RoboNotSupported,
                               "an accessibility client may not invoke a window or system command");
        return RiA11yAnswer(state, RoboInputA11yOutcomeRefused, -1, result, outOutcome);
    }

    state->invokedTotal++;
    state->lastCommand = command->id;

    /* The same router a shortcut uses, so an accessibility request and a keypress reach the same chain in
     * the same order -- and a refusal to claim is the router's own counted `RoboNotFound`. */
    result = InputCommandRouterRoute(router, command, &level, diag);
    if (result == RoboOk) {
        state->routedTotal++;
        return RiA11yAnswer(state, RoboInputA11yOutcomeRouted, level, RoboOk, outOutcome);
    }
    if (result == RoboNotFound) {
        state->unclaimedTotal++;
        return RiA11yAnswer(state, RoboInputA11yOutcomeUnclaimed, level, RoboNotFound, outOutcome);
    }
    state->failedTotal++;
    return RiA11yAnswer(state, RoboInputA11yOutcomeFailed, level, result, outOutcome);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result InputA11yStateWrite(const InputA11yState *state,
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
    if (!state) {
        RiWriterText(&text, "accessibility <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "accessibility privilege=");
    RiWriterText(&text, InputInjectionPrivilegeName(state->privilege));
    RiWriterFieldU64(&text, " invoked", state->invokedTotal);
    RiWriterFieldU64(&text, " routed", state->routedTotal);
    RiWriterFieldU64(&text, " unclaimed", state->unclaimedTotal);
    RiWriterFieldU64(&text, " failed", state->failedTotal);
    RiWriterFieldU64(&text, " refused", state->refusedTotal);
    RiWriterFieldU64(&text, " unsupported", state->unsupportedTotal);
    RiWriterFieldU64(&text, " rejected", state->rejectedTotal);
    RiWriterChar(&text, '\n');

    RiWriterText(&text, "  last outcome=");
    RiWriterText(&text, InputA11yOutcomeName(state->lastOutcome));
    RiWriterFieldText(&text, " command", InputCommandIdName(state->lastCommand));
    RiWriterFieldI32(&text, " level", state->lastLevel);
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
