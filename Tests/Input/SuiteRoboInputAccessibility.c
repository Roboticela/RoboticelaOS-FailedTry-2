/*
 * RoboInput accessibility suite (Phase 13.52) — `input.accessibility.*`.
 *
 * Six cases, and the interesting ones are about what this lane may **not** do:
 *
 *   `vocabulary`     the eleven commands and the six outcomes, positionally, with `NONE` refused rather than
 *                    treated as a request;
 *
 *   `mapping`        every command onto the module's own vocabulary, including the two the brief names:
 *                    `SET_VALUE` has no command behind it (`RoboNotSupported`, named) and `TOGGLE` is an
 *                    activation;
 *
 *   `policy`         which command ids an accessibility client may reach: the text, focus and view families
 *                    plus cancellation, and **not** the window family, the debug overlay, the reset or quit;
 *
 *   `invoke`         the vocabulary door, end to end: a target records the command it was handed -- text and
 *                    all -- and a declined command is `RoboNotFound` and counted rather than silent;
 *
 *   `invoke_command` the seam door, which is what makes the policy checkable: a `WINDOW_CLOSE` built by a
 *                    caller is refused *before* the router, so it cannot reach a target by another name;
 *
 *   `dump`           two generations byte for byte, the counters matching what was done, and the bound.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* A recording target                                                  */
/* ------------------------------------------------------------------ */

typedef struct A11yRecorder {
    unsigned calls;
    InputCommandId lastId;
    char lastText[RoboInputCommandTextMax];
    int32_t lastDelta;
    InputFocusDirection lastDirection;
    /* What the target answers: `RoboOk` to claim, `RoboNotSupported` to decline the walk. */
    Result answer;
} A11yRecorder;

static Result A11yTargetExecute(void *userdata, const InputCommand *command, InputDiag *diag)
{
    A11yRecorder *recorder = (A11yRecorder *)userdata;

    (void)diag;
    recorder->calls++;
    recorder->lastId = command->id;
    recorder->lastDelta = command->delta;
    recorder->lastDirection = command->direction;
    memcpy(recorder->lastText, command->text, sizeof(recorder->lastText));
    return recorder->answer;
}

static void A11yRecorderInit(A11yRecorder *recorder)
{
    memset(recorder, 0, sizeof(*recorder));
    recorder->answer = RoboOk;
}

static InputCommandTarget A11yRecorderTarget(A11yRecorder *recorder)
{
    InputCommandTarget target;

    InputCommandTargetInit(&target);
    target.userdata = recorder;
    target.execute = A11yTargetExecute;
    return target;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestA11yVocabulary(void)
{
    RoboCheckEqInt(RoboInputA11yNone, 0);
    RoboCheckEqInt(RoboInputA11yActivate, 1);
    RoboCheckEqInt(RoboInputA11yFocus, 2);
    RoboCheckEqInt(RoboInputA11yMoveFocus, 3);
    RoboCheckEqInt(RoboInputA11ySetValue, 4);
    RoboCheckEqInt(RoboInputA11yTextInsert, 5);
    RoboCheckEqInt(RoboInputA11yTextDelete, 6);
    RoboCheckEqInt(RoboInputA11ySelect, 7);
    RoboCheckEqInt(RoboInputA11yScroll, 8);
    RoboCheckEqInt(RoboInputA11yDismiss, 9);
    RoboCheckEqInt(RoboInputA11yToggle, 10);
    RoboCheckEqInt((int)RoboInputA11yCommandCount, 11);

    RoboCheckEqInt(RoboInputA11yOutcomeNone, 0);
    RoboCheckEqInt(RoboInputA11yOutcomeRouted, 1);
    RoboCheckEqInt(RoboInputA11yOutcomeUnclaimed, 2);
    RoboCheckEqInt(RoboInputA11yOutcomeFailed, 3);
    RoboCheckEqInt(RoboInputA11yOutcomeRefused, 4);
    RoboCheckEqInt(RoboInputA11yOutcomeUnsupported, 5);
    RoboCheckEqInt((int)RoboInputA11yOutcomeCount, 6);

    RoboCheckEqInt(InputA11yCommandIsKnown(RoboInputA11yActivate), 1);
    RoboCheckEqInt(InputA11yCommandIsKnown(RoboInputA11yToggle), 1);
    /* `NONE` is a cleared record, not a request. */
    RoboCheckEqInt(InputA11yCommandIsKnown(RoboInputA11yNone), 0);
    RoboCheckEqInt(InputA11yCommandIsKnown((InputA11yCommand)RoboInputA11yCommandCount), 0);
    RoboCheckEqInt(InputA11yCommandIsKnown((InputA11yCommand)-1), 0);

    RoboCheckStrEq(InputA11yCommandName(RoboInputA11yActivate), "activate");
    RoboCheckStrEq(InputA11yCommandName(RoboInputA11yMoveFocus), "move-focus");
    RoboCheckStrEq(InputA11yCommandName(RoboInputA11ySetValue), "set-value");
    RoboCheckStrEq(InputA11yCommandName(RoboInputA11yTextInsert), "text-insert");
    RoboCheckStrEq(InputA11yCommandName(RoboInputA11yNone), "none");
    RoboCheckStrEq(InputA11yCommandName((InputA11yCommand)99), "unknown");

    RoboCheckEqInt(InputA11yOutcomeIsKnown(RoboInputA11yOutcomeRefused), 1);
    RoboCheckEqInt(InputA11yOutcomeIsKnown((InputA11yOutcome)RoboInputA11yOutcomeCount), 0);
    RoboCheckStrEq(InputA11yOutcomeName(RoboInputA11yOutcomeUnclaimed), "unclaimed");
    RoboCheckStrEq(InputA11yOutcomeName(RoboInputA11yOutcomeRefused), "refused");
    RoboCheckStrEq(InputA11yOutcomeName((InputA11yOutcome)99), "unknown");

    /* The lane's rank is recorded, not implied: a log line has to be able to say who asked. */
    {
        InputA11yState state;

        InputA11yStateInit(&state);
        RoboCheckEqInt((int)state.privilege, (int)RoboInputPrivilegeAccessibility);
        RoboCheckEqInt(state.lastLevel, -1);
        RoboCheckEqInt((int)state.lastCommand, (int)RoboInputCommandNone);
    }
}

/* ------------------------------------------------------------------ */
/* The mapping                                                         */
/* ------------------------------------------------------------------ */

static void TestA11yMapping(void)
{
    InputCommandId id = (InputCommandId)999;
    InputDiag diag = { 0 };

    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yActivate, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandActivate);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yFocus, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandFocusDirection);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yMoveFocus, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandFocusDirection);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yTextInsert, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandInsertText);
    /* A delete removes the unit *at* the caret, which is the forward delete's own meaning. */
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yTextDelete, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandDeleteForward);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11ySelect, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandSelectAll);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yScroll, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandScrollBy);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yDismiss, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandCancel);
    /* A toggle is an activation: activating a two-state control *is* how its state changes, and the control
     * owns the second state. */
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yToggle, &id, NULL), RoboOk);
    RoboCheckEqInt((int)id, (int)RoboInputCommandActivate);

    /* `SET_VALUE` is named rather than approximated. */
    id = (InputCommandId)999;
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11ySetValue, &id, &diag), RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainAccessibility);
    RoboCheckEqInt((int)id, (int)RoboInputCommandNone);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yNone, &id, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputA11yCommandMap((InputA11yCommand)99, &id, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputA11yCommandMap(RoboInputA11yActivate, NULL, NULL), RoboInvalidArgument);

    /* The request's own argument rules: a setter is refused for a command that does not own the field. */
    {
        InputA11yRequest request;

        InputA11yRequestInit(&request);
        RoboCheckEqInt((int)request.command, (int)RoboInputA11yNone);
        RoboCheckEqInt(InputA11yRequestIsValid(&request), 0);

        RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yTextInsert, NULL), RoboOk);
        RoboCheckEqInt(InputA11yRequestIsValid(&request), 1);
        RoboCheckEqInt(InputA11yRequestSetDelta(&request, 5, NULL), RoboInvalidArgument);
        RoboCheckEqInt(InputA11yRequestSetText(&request, "hello", NULL), RoboOk);
        RoboCheckEqInt(InputA11yRequestIsValid(&request), 1);
        RoboCheckStrEq(request.text, "hello");

        RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yScroll, NULL), RoboOk);
        RoboCheckEqInt(InputA11yRequestSetText(&request, "hello", NULL), RoboInvalidArgument);
        RoboCheckEqInt(InputA11yRequestSetDelta(&request, -40, NULL), RoboOk);
        RoboCheckEqInt(request.delta, -40);

        RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yNone, NULL), RoboInvalidArgument);
        RoboCheckEqInt(InputA11yRequestSet(&request, (InputA11yCommand)99, NULL), RoboInvalidArgument);

        RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yMoveFocus, NULL), RoboOk);
        RoboCheckEqInt(InputA11yRequestSetDirection(&request, RoboInputFocusDirectionLeft, NULL), RoboOk);
        RoboCheckEqInt((int)request.direction, (int)RoboInputFocusDirectionLeft);
        RoboCheckEqInt(InputA11yRequestSetDirection(&request, (InputFocusDirection)99, NULL),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yDismiss, NULL), RoboOk);
        RoboCheckEqInt(InputA11yRequestSetDirection(&request, RoboInputFocusDirectionLeft, NULL),
                       RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* The policy                                                          */
/* ------------------------------------------------------------------ */

static void TestA11yPolicy(void)
{
    /* What a user does to the thing they are looking at. */
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandInsertText), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandDeleteBackward), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandSelectAll), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandCopy), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandPaste), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandFocusNext), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandFocusDirection), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandActivate), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandCancel), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandBack), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandScrollBy), 1);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandScrollPageDown), 1);

    /* And what it may not: the window family, so a client cannot dismiss the surface it was describing. */
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandWindowClose), 0);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandWindowMinimize), 0);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandWindowToggleMaximize), 0);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandWindowMenu), 0);
    /* Nor the system commands, nor a cleared record. */
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandToggleDebugOverlay), 0);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandResetInputState), 0);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandQuit), 0);
    RoboCheckEqInt(InputA11yCommandAllowed(RoboInputCommandNone), 0);
    RoboCheckEqInt(InputA11yCommandAllowed((InputCommandId)99), 0);

    /* Every command in the vocabulary is classified: no id is left in a third state, and the ones refused
     * are exactly the window family plus three named system commands. */
    {
        int id;
        int refused = 0;

        for (id = 1; id < (int)RoboInputCommandCount; id++) {
            int allowed = InputA11yCommandAllowed((InputCommandId)id);

            if (!allowed) {
                refused++;
                RoboCheck(InputCommandIdIsWindow((InputCommandId)id) ||
                          (InputCommandId)id == RoboInputCommandToggleDebugOverlay ||
                          (InputCommandId)id == RoboInputCommandResetInputState ||
                          (InputCommandId)id == RoboInputCommandQuit);
            }
        }
        /* Seven: the four window commands and the three named system ones, and nothing else. */
        RoboCheckEqInt(refused, 7);
    }
}

/* ------------------------------------------------------------------ */
/* Invoke                                                              */
/* ------------------------------------------------------------------ */

static void TestA11yInvoke(void)
{
    InputA11yState state;
    InputA11yRequest request;
    InputCommandRouter router;
    A11yRecorder recorder;
    InputCommandTarget target;
    InputA11yOutcome outcome = RoboInputA11yOutcomeNone;
    InputDiag diag = { 0 };

    InputA11yStateInit(&state);
    InputCommandRouterInit(&router);
    A11yRecorderInit(&recorder);
    target = A11yRecorderTarget(&recorder);
    RoboCheckEqInt(InputCommandRouterPush(&router, &target, NULL), RoboOk);

    /* An insert carries its text all the way to the target. */
    InputA11yRequestInit(&request);
    RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yTextInsert, NULL), RoboOk);
    RoboCheckEqInt(InputA11yRequestSetText(&request, "typed by a screen reader", NULL), RoboOk);
    outcome = RoboInputA11yOutcomeNone;
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)outcome, (int)RoboInputA11yOutcomeRouted);
    RoboCheckEqInt((int)recorder.calls, 1);
    RoboCheckEqInt((int)recorder.lastId, (int)RoboInputCommandInsertText);
    RoboCheckStrEq(recorder.lastText, "typed by a screen reader");
    RoboCheckEqInt((int)state.routedTotal, 1);
    RoboCheckEqInt((int)state.lastOutcome, (int)RoboInputA11yOutcomeRouted);
    RoboCheckEqInt((int)state.lastCommand, (int)RoboInputCommandInsertText);
    RoboCheckEqInt(state.lastLevel, 0);

    /* A focus request with no direction means the next one, which is what `TAB` means everywhere. */
    RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yMoveFocus, NULL), RoboOk);
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)recorder.lastId, (int)RoboInputCommandFocusDirection);
    RoboCheckEqInt((int)recorder.lastDirection, (int)RoboInputFocusDirectionNext);
    /* A named direction is carried rather than defaulted. */
    RoboCheckEqInt(InputA11yRequestSetDirection(&request, RoboInputFocusDirectionDown, NULL), RoboOk);
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)recorder.lastDirection, (int)RoboInputFocusDirectionDown);

    /* A scroll carries its distance. */
    RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yScroll, NULL), RoboOk);
    RoboCheckEqInt(InputA11yRequestSetDelta(&request, 120, NULL), RoboOk);
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)recorder.lastId, (int)RoboInputCommandScrollBy);
    RoboCheckEqInt(recorder.lastDelta, 120);
    RoboCheckEqInt((int)state.routedTotal, 4);

    /* `SET_VALUE` never reaches a target. */
    RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11ySetValue, NULL), RoboOk);
    diag = (InputDiag){ 0 };
    outcome = RoboInputA11yOutcomeNone;
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, &diag), RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainAccessibility);
    RoboCheckEqInt((int)outcome, (int)RoboInputA11yOutcomeUnsupported);
    RoboCheckEqInt((int)state.unsupportedTotal, 1);
    RoboCheckEqInt((int)recorder.calls, 4);

    /* A target that declines is counted rather than silent: the walk ends in `RoboNotFound`. */
    recorder.answer = RoboNotSupported;
    RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yActivate, NULL), RoboOk);
    outcome = RoboInputA11yOutcomeNone;
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, NULL), RoboNotFound);
    RoboCheckEqInt((int)outcome, (int)RoboInputA11yOutcomeUnclaimed);
    RoboCheckEqInt((int)state.unclaimedTotal, 1);
    RoboCheckEqInt((int)router.unclaimedTotal, 1);
    RoboCheckEqInt((int)recorder.calls, 5);

    /* A target that claims and *fails* stops the walk, and the error is the caller's. */
    recorder.answer = RoboError;
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, NULL), RoboError);
    RoboCheckEqInt((int)outcome, (int)RoboInputA11yOutcomeFailed);
    RoboCheckEqInt((int)state.failedTotal, 1);
    RoboCheckEqInt((int)router.failedTotal, 1);

    /* No target at all: the same unclaimed path, and nothing invented. */
    InputCommandRouterInit(&router);
    recorder.answer = RoboOk;
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, NULL), RoboNotFound);
    RoboCheckEqInt((int)state.unclaimedTotal, 2);

    /* An invalid request is refused before anything is mapped. */
    InputA11yRequestInit(&request);
    diag = (InputDiag){ 0 };
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, &outcome, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainAccessibility);
    RoboCheckEqInt((int)state.rejectedTotal, 1);
    RoboCheckEqInt(InputA11yInvoke(NULL, &request, &router, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputA11yInvoke(&state, &request, NULL, NULL, NULL), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The seam door                                                       */
/* ------------------------------------------------------------------ */

static void TestA11yInvokeCommand(void)
{
    InputA11yState state;
    InputCommandRouter router;
    A11yRecorder recorder;
    InputCommandTarget target;
    InputCommand command;
    InputA11yOutcome outcome = RoboInputA11yOutcomeNone;
    InputDiag diag = { 0 };

    InputA11yStateInit(&state);
    InputCommandRouterInit(&router);
    A11yRecorderInit(&recorder);
    target = A11yRecorderTarget(&recorder);
    RoboCheckEqInt(InputCommandRouterPush(&router, &target, NULL), RoboOk);

    /* A command this lane may issue: routed, and the target is asked. */
    InputCommandInit(&command);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandSelectAll, NULL), RoboOk);
    RoboCheckEqInt(InputA11yInvokeCommand(&state, &command, &router, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)outcome, (int)RoboInputA11yOutcomeRouted);
    RoboCheckEqInt((int)recorder.calls, 1);
    RoboCheckEqInt((int)recorder.lastId, (int)RoboInputCommandSelectAll);

    /* A window command: refused, counted, and the target is **never asked** -- which is the whole
     * boundary, and the reason this door exists rather than only the vocabulary above. */
    InputCommandInit(&command);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandWindowClose, NULL), RoboOk);
    diag = (InputDiag){ 0 };
    outcome = RoboInputA11yOutcomeNone;
    RoboCheckEqInt(InputA11yInvokeCommand(&state, &command, &router, &outcome, &diag), RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainAccessibility);
    RoboCheckEqInt((int)outcome, (int)RoboInputA11yOutcomeRefused);
    RoboCheckEqInt((int)state.refusedTotal, 1);
    RoboCheckEqInt((int)state.invokedTotal, 1);
    RoboCheckEqInt((int)recorder.calls, 1);
    RoboCheckEqInt((int)state.lastCommand, (int)RoboInputCommandWindowClose);

    /* The three system commands, the same way. */
    InputCommandInit(&command);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandQuit, NULL), RoboOk);
    RoboCheckEqInt(InputA11yInvokeCommand(&state, &command, &router, &outcome, NULL), RoboNotSupported);
    InputCommandInit(&command);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandResetInputState, NULL), RoboOk);
    RoboCheckEqInt(InputA11yInvokeCommand(&state, &command, &router, &outcome, NULL), RoboNotSupported);
    RoboCheckEqInt((int)state.refusedTotal, 3);
    RoboCheckEqInt((int)recorder.calls, 1);

    /* A cleared or invalid command is a record defect, not a refusal. */
    InputCommandInit(&command);
    RoboCheckEqInt(InputA11yInvokeCommand(&state, &command, &router, &outcome, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)state.rejectedTotal, 1);
    RoboCheckEqInt(InputA11yInvokeCommand(&state, NULL, &router, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputA11yInvokeCommand(NULL, &command, &router, NULL, NULL), RoboInvalidArgument);

    /* The lane's counters and outcome names, from a state that has done all of the above. */
    RoboCheckEqInt((int)state.routedTotal, 1);
    RoboCheckEqInt((int)state.unsupportedTotal, 0);
    RoboCheckStrEq(InputA11yOutcomeName(state.lastOutcome), "refused");
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

static void TestA11yDump(void)
{
    InputA11yState state;
    InputCommandRouter router;
    InputA11yRequest request;
    char first[RoboInputA11yDumpMax];
    char second[RoboInputA11yDumpMax];
    char small[32];
    size_t firstLength = 0u;
    size_t secondLength = 0u;
    size_t needed = 0u;

    InputA11yStateInit(&state);
    InputCommandRouterInit(&router);
    InputA11yRequestInit(&request);
    RoboCheckEqInt(InputA11yRequestSet(&request, RoboInputA11yActivate, NULL), RoboOk);
    RoboCheckEqInt(InputA11yInvoke(&state, &request, &router, NULL, NULL), RoboNotFound);

    RoboCheckEqInt(InputA11yStateWrite(&state, first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheckEqInt(InputA11yStateWrite(&state, second, sizeof(second), &secondLength, NULL), RoboOk);
    RoboCheckEqInt((int)firstLength, (int)secondLength);
    RoboCheckStrEq(first, second);
    RoboCheck(strstr(first, "accessibility privilege=accessibility") != NULL);
    RoboCheck(strstr(first, "invoked=1") != NULL);
    RoboCheck(strstr(first, "unclaimed=1") != NULL);
    RoboCheck(strstr(first, "outcome=unclaimed") != NULL);
    RoboCheck(strstr(first, "command=activate") != NULL);
    RoboCheck(strstr(first, "level=") != NULL);

    RoboCheckEqInt(InputA11yStateWrite(&state, small, sizeof(small), &needed, NULL), RoboNotSupported);
    RoboCheck(small[0] == '?');
    RoboCheck(needed > (size_t)sizeof(small));
    RoboCheck((int)needed <= (int)RoboInputA11yDumpMax);

    RoboCheckEqInt(InputA11yStateWrite(NULL, first, sizeof(first), &firstLength, NULL), RoboOk);
    RoboCheckStrEq(first, "accessibility <none>\n");
    RoboCheckEqInt(InputA11yStateWrite(&state, NULL, 0u, &firstLength, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputA11yStateWrite(&state, first, sizeof(first), NULL, NULL), RoboInvalidArgument);
}

void TestRegisterInputAccessibility(void)
{
    TestRegister("input.accessibility.vocabulary", TestA11yVocabulary);
    TestRegister("input.accessibility.mapping", TestA11yMapping);
    TestRegister("input.accessibility.policy", TestA11yPolicy);
    TestRegister("input.accessibility.invoke", TestA11yInvoke);
    TestRegister("input.accessibility.invoke_command", TestA11yInvokeCommand);
    TestRegister("input.accessibility.dump", TestA11yDump);
}
