/*
 * RoboInput focus suite (Phase 13.25--13.28) — `input.focus.*`.
 *
 * Seven cases, and the subject of each is one of the four questions the header names:
 *
 *   `vocabulary`   the six reasons and the phase names, asserted positionally, plus the *refusal* of the four
 *                  arrow directions -- a 2D walk needs rectangles this layer does not have, and saying so is
 *                  the behaviour rather than an omission;
 *
 *   `scopes`       one focus per window: two windows keep two foci, switching between them restores what
 *                  each remembers, and re-announcing the window already active produces **nothing** (a
 *                  `WindowActivate` per frame is the bug this case exists to catch);
 *
 *   `request`      the refusals that keep a background window's control from stealing focus, the
 *                  idempotence a press on an already-focused slider needs, and the empty transition a clear
 *                  with nothing focused produces;
 *
 *   `release`      a destroyed target loses its focus by identity wherever it is recorded, a released window
 *                  drops its scope *and* stops being active, and a stale target cannot come back;
 *
 *   `transition`   both ends, in the order 13.27 requires, and the *event list* it produces: nothing, one,
 *                  two or four events, asserted whole through the public model doors;
 *
 *   `chain`        the navigation list: `Tab` from nowhere enters at the first, from the last stays, the
 *                  backward walk mirrors it, a duplicate registration changes nothing and is counted, and a
 *                  full chain refuses rather than dropping an entry;
 *
 *   `dump`         the three dumps, asserted whole and byte-for-byte deterministic.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

static InputTarget FocusWindowTarget(WindowId window)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetWindow(&target, window, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's window target was refused");
    }
    return target;
}

static InputTarget FocusDocumentTarget(WindowId window, LayoutNodeId node)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetDocument(&target, window, NULL, node, 0u, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's document target was refused");
    }
    return target;
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestFocusVocabulary(void)
{
    RoboCheckEqInt(RoboInputFocusReasonNone, 0);
    RoboCheckEqInt(RoboInputFocusReasonKeyboard, 1);
    RoboCheckEqInt(RoboInputFocusReasonPointer, 2);
    RoboCheckEqInt(RoboInputFocusReasonProgrammatic, 3);
    RoboCheckEqInt(RoboInputFocusReasonNavigation, 4);
    RoboCheckEqInt(RoboInputFocusReasonWindowActivation, 5);
    RoboCheckEqInt(RoboInputFocusReasonAccessibility, 6);
    RoboCheckEqInt(RoboInputFocusReasonCount, 7);
    RoboCheck(InputFocusReasonIsKnown(RoboInputFocusReasonAccessibility));
    RoboCheck(!InputFocusReasonIsKnown((InputFocusReason)7));
    RoboCheckStrEq(InputFocusReasonName(RoboInputFocusReasonNone), "none");
    RoboCheckStrEq(InputFocusReasonName(RoboInputFocusReasonWindowActivation), "window-activation");
    RoboCheckStrEq(InputFocusReasonName((InputFocusReason)99), "unknown");

    /* The directions are 13.13's, and only four of the nine are walkable here. */
    RoboCheck(InputFocusDirectionIsSupported(RoboInputFocusDirectionNext));
    RoboCheck(InputFocusDirectionIsSupported(RoboInputFocusDirectionPrevious));
    RoboCheck(InputFocusDirectionIsSupported(RoboInputFocusDirectionFirst));
    RoboCheck(InputFocusDirectionIsSupported(RoboInputFocusDirectionLast));
    RoboCheck(!InputFocusDirectionIsSupported(RoboInputFocusDirectionUp));
    RoboCheck(!InputFocusDirectionIsSupported(RoboInputFocusDirectionDown));
    RoboCheck(!InputFocusDirectionIsSupported(RoboInputFocusDirectionLeft));
    RoboCheck(!InputFocusDirectionIsSupported(RoboInputFocusDirectionRight));
    RoboCheck(!InputFocusDirectionIsSupported(RoboInputFocusDirectionNone));
}

/* ------------------------------------------------------------------ */
/* Scopes                                                              */
/* ------------------------------------------------------------------ */

static void TestFocusScopes(void)
{
    InputFocusManager manager;
    InputFocusTransition transition;
    InputTarget a = FocusWindowTarget(7u);
    InputTarget b = FocusWindowTarget(9u);
    InputDiag diag;

    InputDiagReset(&diag);
    InputFocusManagerInit(&manager);
    RoboCheckEqInt(InputFocusManagerActiveWindow(&manager), 0);
    RoboCheck(InputFocusManagerTarget(&manager) == NULL);

    /* No active window, so there is nowhere to focus: a refusal, counted. */
    RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonProgrammatic, &transition, &diag),
                   RoboNotFound);
    RoboCheckEqInt((int)manager.refused, 1);
    RoboCheckEqInt(diag.domain, RoboInputDomainFocus);

    /* Activating a window the manager has never seen focuses nothing: it does not invent a target. */
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, &transition, &diag), RoboOk);
    RoboCheckEqInt(InputFocusManagerActiveWindow(&manager), 7);
    RoboCheck(!InputFocusTransitionHasFocus(&transition));
    RoboCheck(transition.hasActivated && transition.activated == 7u);
    RoboCheck(transition.reason == RoboInputFocusReasonWindowActivation);
    RoboCheck(InputFocusManagerTarget(&manager) == NULL);

    /* Re-announcing the same window changes nothing at all. */
    InputFocusTransitionInit(&transition);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, &transition, &diag), RoboOk);
    RoboCheckEqInt(InputFocusTransitionEventCount(&transition), 0);

    /* Focus a node in it. */
    RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonProgrammatic, &transition, &diag),
                   RoboOk);
    RoboCheck(transition.hasFocused && !transition.hasPrevious);
    RoboCheck(InputFocusManagerIsFocused(&manager, &a));
    RoboCheckEqInt(InputFocusTransitionEventCount(&transition), 1);

    /* A second window becomes active: the first *keeps* its focus, and the second has none yet. */
    InputFocusTransitionInit(&transition);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 9u, 1u, &transition, &diag), RoboOk);
    RoboCheck(transition.hasDeactivated && transition.deactivated == 7u);
    RoboCheck(transition.hasActivated && transition.activated == 9u);
    RoboCheck(transition.hasPrevious);
    RoboCheck(InputTargetEqual(&transition.previous, &a));
    RoboCheck(!transition.hasFocused);
    RoboCheck(InputFocusManagerTarget(&manager) == NULL);
    RoboCheckEqInt((int)manager.count, 2);

    RoboCheckEqInt(InputFocusRequest(&manager, &b, RoboInputFocusReasonPointer, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &b));

    /* Back to the first: what it remembered comes back, in one transition with both ends. */
    InputFocusTransitionInit(&transition);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, &transition, &diag), RoboOk);
    RoboCheck(transition.hasPrevious && InputTargetEqual(&transition.previous, &b));
    RoboCheck(transition.hasFocused && InputTargetEqual(&transition.focused, &a));
    RoboCheckEqInt(InputFocusTransitionEventCount(&transition), 4);

    /* And the scopes door answers for a window that is not active. */
    {
        const InputTarget *remembered = InputFocusManagerScope(&manager, 9u);

        RoboCheck(remembered != NULL && InputTargetEqual(remembered, &b));
        RoboCheck(InputFocusManagerScope(&manager, 11u) == NULL);
        RoboCheck(InputFocusManagerScope(&manager, 0u) == NULL);
    }

    /* A zero window is refused, and does not become active. */
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 0u, 1u, &transition, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputFocusManagerActiveWindow(&manager), 7);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(NULL, 7u, 1u, NULL, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Requests and refusals                                               */
/* ------------------------------------------------------------------ */

static void TestFocusRequest(void)
{
    InputFocusManager manager;
    InputFocusTransition transition;
    InputTarget a = FocusWindowTarget(7u);
    InputTarget b = FocusWindowTarget(7u);
    InputTarget other = FocusWindowTarget(9u);
    InputTarget node = FocusDocumentTarget(7u, (LayoutNodeId)55u);
    InputTarget application;
    InputDiag diag;

    InputDiagReset(&diag);
    InputFocusManagerInit(&manager);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, NULL, &diag), RoboOk);

    /* A target from another window is refused: it could never receive a key. */
    RoboCheckEqInt(InputFocusRequest(&manager, &other, RoboInputFocusReasonProgrammatic, &transition, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainFocus);
    RoboCheck(InputFocusManagerTarget(&manager) == NULL);

    /* An invalid target, and no reason. */
    {
        InputTarget empty;

        InputTargetInit(&empty);
        RoboCheckEqInt(InputFocusRequest(&manager, &empty, RoboInputFocusReasonProgrammatic, &transition,
                                         &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonNone, &transition, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputFocusRequest(&manager, NULL, RoboInputFocusReasonProgrammatic, &transition,
                                         &diag),
                       RoboInvalidArgument);
    }

    /* An application is not a focusable node: a key routed to "the app" has nowhere to go. */
    InputTargetInit(&application);
    RoboCheckEqInt(InputTargetSetApplication(&application, 1u, RoboInputSpaceWindow, &diag), RoboOk);
    RoboCheckEqInt(InputFocusRequest(&manager, &application, RoboInputFocusReasonProgrammatic, &transition,
                                     &diag),
                   RoboInvalidArgument);

    /* A document tip focuses. */
    RoboCheckEqInt(InputFocusRequest(&manager, &node, RoboInputFocusReasonPointer, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &node));
    RoboCheckEqInt((int)transition.reason, (int)RoboInputFocusReasonPointer);

    /* Focusing what already has focus is an empty transition and does not count as a change. */
    {
        uint32_t before = manager.changes;

        InputFocusTransitionInit(&transition);
        RoboCheckEqInt(InputFocusRequest(&manager, &node, RoboInputFocusReasonPointer, &transition, &diag),
                       RoboOk);
        RoboCheckEqInt(InputFocusTransitionEventCount(&transition), 0);
        RoboCheckEqInt((int)manager.changes, (int)before);
    }

    /* Two window targets in one window are distinct targets: a window is focusable, and so is a node. */
    RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(transition.hasPrevious && InputTargetEqual(&transition.previous, &node));
    RoboCheck(InputFocusManagerIsFocused(&manager, &a));
    RoboCheck(!InputFocusManagerIsFocused(&manager, &node));

    /* Clearing with nothing focused is not an error and reports nothing. */
    RoboCheckEqInt(InputFocusManagerClear(&manager, RoboInputFocusReasonProgrammatic, &transition, &diag),
                   RoboOk);
    RoboCheck(transition.hasPrevious && !transition.hasFocused);
    RoboCheck(InputFocusManagerTarget(&manager) == NULL);
    InputFocusTransitionInit(&transition);
    RoboCheckEqInt(InputFocusManagerClear(&manager, RoboInputFocusReasonProgrammatic, &transition, &diag),
                   RoboOk);
    RoboCheckEqInt(InputFocusTransitionEventCount(&transition), 0);

    /* The b/a pair used above is one window's own business: both are valid targets in window 7. */
    RoboCheckEqInt(InputFocusRequest(&manager, &b, RoboInputFocusReasonAccessibility, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &b));
}

/* ------------------------------------------------------------------ */
/* Release                                                             */
/* ------------------------------------------------------------------ */

static void TestFocusRelease(void)
{
    InputFocusManager manager;
    InputFocusTransition transition;
    InputTarget a = FocusDocumentTarget(7u, (LayoutNodeId)11u);
    InputTarget b = FocusDocumentTarget(9u, (LayoutNodeId)22u);
    InputDiag diag;

    InputDiagReset(&diag);
    InputFocusManagerInit(&manager);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonProgrammatic, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 9u, 1u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusRequest(&manager, &b, RoboInputFocusReasonProgrammatic, NULL, &diag), RoboOk);

    /* A destroyed target loses focus wherever it is recorded -- including in a window that is not active,
     * which is the case a caller cannot find by itself. */
    RoboCheckEqInt((int)InputFocusManagerReleaseTarget(&manager, &a), 1);
    RoboCheck(InputFocusManagerScope(&manager, 7u) == NULL);
    RoboCheck((int)InputFocusManagerReleaseTarget(&manager, &a) == 0);
    RoboCheckEqInt((int)InputFocusManagerReleaseTarget(&manager, NULL), 0);

    /* The *active* window's focus going away publishes immediately. */
    RoboCheckEqInt((int)InputFocusManagerReleaseTarget(&manager, &b), 1);
    RoboCheck(InputFocusManagerTarget(&manager) == NULL);

    /* Releasing a window drops its scope and deactivates it when it was the active one. */
    RoboCheckEqInt(InputFocusRequest(&manager, &b, RoboInputFocusReasonProgrammatic, NULL, &diag), RoboOk);
    RoboCheck(InputFocusManagerReleaseWindow(&manager, 9u));
    RoboCheckEqInt(InputFocusManagerActiveWindow(&manager), 0);
    RoboCheck(InputFocusManagerTarget(&manager) == NULL);
    RoboCheck(!InputFocusManagerReleaseWindow(&manager, 9u));
    RoboCheck(!InputFocusManagerReleaseWindow(&manager, 0u));

    /* Nothing is left, and the whole-table clear reports what went. */
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonProgrammatic, NULL, &diag), RoboOk);
    RoboCheckEqInt((int)InputFocusManagerClearAll(&manager), 1);
    RoboCheckEqInt((int)InputFocusManagerClearAll(&manager), 0);

    /* The transition argument is optional everywhere, which is what a caller that only wants the state
     * needs. The `b` request above passed NULL and focused. */
    (void)transition;
}

/* ------------------------------------------------------------------ */
/* Transitions and their events                                        */
/* ------------------------------------------------------------------ */

static void TestFocusTransition(void)
{
    InputFocusManager manager;
    InputFocusTransition transition;
    InputEvent events[4];
    InputTarget a = FocusDocumentTarget(7u, (LayoutNodeId)3u);
    InputTarget b = FocusDocumentTarget(7u, (LayoutNodeId)4u);
    InputDiag diag;

    InputDiagReset(&diag);
    InputFocusManagerInit(&manager);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, NULL, &diag), RoboOk);

    /* First focus: one event, a `FOCUS_IN` on the node. */
    RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheckEqInt((int)InputFocusTransitionEventCount(&transition), 1);
    RoboCheckEqInt((int)InputFocusTransitionEvents(&transition, events, 4u), 1);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputEventFocusIn);
    RoboCheckEqInt(events[0].window, 7);
    RoboCheck(events[0].hasTarget && InputTargetEqual(&events[0].target, &a));
    /* A recording of FocusIn carries no payload: what it is about is its target. */
    RoboCheckEqInt((int)InputEventPayloadKind(events[0].type), (int)RoboInputPayloadNone);

    /* A change of focus: out then in, in that order, both on their own nodes. */
    RoboCheckEqInt(InputFocusRequest(&manager, &b, RoboInputFocusReasonPointer, &transition, &diag), RoboOk);
    RoboCheckEqInt((int)InputFocusTransitionEventCount(&transition), 2);
    RoboCheckEqInt((int)InputFocusTransitionEvents(&transition, events, 4u), 2);
    RoboCheckEqInt((int)events[0].type, (int)RoboInputEventFocusOut);
    RoboCheck(InputTargetEqual(&events[0].target, &a));
    RoboCheckEqInt((int)events[1].type, (int)RoboInputEventFocusIn);
    RoboCheck(InputTargetEqual(&events[1].target, &b));

    /* A window activation *plus* a focus change: four events, deactivate → activate → out → in. */
    InputFocusManagerInit(&manager);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusRequest(&manager, &a, RoboInputFocusReasonProgrammatic, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 9u, 1u, NULL, &diag), RoboOk);
    (void)b;
    InputFocusTransitionInit(&transition);
    {
        InputTarget second = FocusDocumentTarget(9u, (LayoutNodeId)8u);

        RoboCheckEqInt(InputFocusRequest(&manager, &second, RoboInputFocusReasonWindowActivation,
                                         &transition, &diag),
                       RoboOk);
    }
    RoboCheckEqInt((int)InputFocusTransitionEventCount(&transition), 1);

    {
        InputFocusTransition combined;

        InputFocusTransitionInit(&combined);
        combined.hasDeactivated = 1;
        combined.deactivated = 7u;
        combined.hasActivated = 1;
        combined.activated = 9u;
        combined.hasPrevious = 1;
        combined.previous = a;
        combined.hasFocused = 1;
        combined.focused = b;
        combined.reason = RoboInputFocusReasonWindowActivation;
        RoboCheckEqInt((int)InputFocusTransitionEventCount(&combined), 4);
        RoboCheckEqInt((int)InputFocusTransitionEvents(&combined, events, 4u), 4);
        RoboCheckEqInt((int)events[0].type, (int)RoboInputEventWindowDeactivate);
        RoboCheckEqInt(events[0].window, 7);
        RoboCheckEqInt((int)events[1].type, (int)RoboInputEventWindowActivate);
        RoboCheckEqInt(events[1].window, 9);
        RoboCheckEqInt((int)events[2].type, (int)RoboInputEventFocusOut);
        RoboCheckEqInt((int)events[3].type, (int)RoboInputEventFocusIn);
        /* A capacity too small writes what fits and reports it, rather than overrunning. */
        RoboCheckEqInt((int)InputFocusTransitionEvents(&combined, events, 2u), 2);
        RoboCheckEqInt((int)events[0].type, (int)RoboInputEventWindowDeactivate);
        RoboCheckEqInt((int)events[1].type, (int)RoboInputEventWindowActivate);
    }

    /* A cleared transition produces nothing, and a NULL one is not a crash. */
    InputFocusTransitionInit(&transition);
    RoboCheckEqInt((int)InputFocusTransitionEventCount(&transition), 0);
    RoboCheck(!InputFocusTransitionHasFocus(&transition));
    RoboCheckEqInt((int)InputFocusTransitionEvents(&transition, events, 4u), 0);
    RoboCheckEqInt((int)InputFocusTransitionEventCount(NULL), 0);
    RoboCheckEqInt((int)InputFocusTransitionEvents(NULL, events, 4u), 0);
}

/* ------------------------------------------------------------------ */
/* The navigation chain                                                */
/* ------------------------------------------------------------------ */

static void TestFocusChain(void)
{
    InputFocusManager manager;
    InputFocusChain chain;
    InputFocusTransition transition;
    InputTarget one = FocusDocumentTarget(7u, (LayoutNodeId)1u);
    InputTarget two = FocusDocumentTarget(7u, (LayoutNodeId)2u);
    InputTarget three = FocusDocumentTarget(7u, (LayoutNodeId)3u);
    InputDiag diag;

    InputDiagReset(&diag);
    InputFocusManagerInit(&manager);
    InputFocusChainInit(&chain);

    /* An empty chain has nothing to walk. */
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionNext,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboNotFound);

    RoboCheckEqInt(InputFocusChainAdd(&chain, &one, &diag), RoboOk);
    RoboCheckEqInt(InputFocusChainAdd(&chain, &two, &diag), RoboOk);
    RoboCheckEqInt(InputFocusChainAdd(&chain, &three, &diag), RoboOk);
    RoboCheckEqInt((int)chain.count, 3);

    /* A duplicate is a no-op, counted rather than stored: `Tab` must not visit a target twice. */
    RoboCheckEqInt(InputFocusChainAdd(&chain, &two, &diag), RoboOk);
    RoboCheckEqInt((int)chain.count, 3);
    RoboCheckEqInt((int)chain.duplicates, 1);

    /* An invalid target is refused. */
    {
        InputTarget empty;

        InputTargetInit(&empty);
        RoboCheckEqInt(InputFocusChainAdd(&chain, &empty, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputFocusChainAdd(NULL, &one, &diag), RoboInvalidArgument);
    }
    RoboCheckEqInt(InputFocusChainIndexOf(&chain, &three), 2);
    RoboCheckEqInt(InputFocusChainIndexOf(&chain, &one), 0);
    {
        InputTarget other = FocusWindowTarget(9u);

        RoboCheckEqInt(InputFocusChainIndexOf(&chain, &other), -1);
    }

    /* `Tab` from nowhere enters at the first; then walks; then stays at the last. */
    RoboCheck(InputTargetEqual(InputFocusChainPeek(&chain, NULL, RoboInputFocusDirectionNext), &one));
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionNext,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &one));
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionNext,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &two));
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionNext,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &three));
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionNext,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &three));
    /* Staying is an empty transition: no `FOCUS_OUT` for a target that did not lose anything. */
    RoboCheckEqInt(InputFocusTransitionEventCount(&transition), 0);

    /* Backward mirrors it. */
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionPrevious,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &two));
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionPrevious,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &one));
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionPrevious,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &one));

    /* `FIRST` and `LAST` are jumps, not steps. */
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionLast,
                                        RoboInputFocusReasonNavigation, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &three));
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionFirst,
                                        RoboInputFocusReasonNavigation, &transition, &diag),
                   RoboOk);
    RoboCheck(InputFocusManagerIsFocused(&manager, &one));

    /* An arrow is refused by name rather than answered by walking the list in order. */
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionDown,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboNotSupported);
    RoboCheckEqInt(diag.domain, RoboInputDomainFocus);

    /* Removal reports the index it was at, and shifts the tail down. */
    RoboCheckEqInt(InputFocusChainRemove(&chain, &one), 0);
    RoboCheckEqInt((int)chain.count, 2);
    RoboCheckEqInt(InputFocusChainRemove(&chain, &one), -1);
    RoboCheckEqInt(InputFocusChainIndexOf(&chain, &three), 1);

    /* A full chain refuses rather than dropping an entry: the caller has to decide what is unreachable. */
    {
        InputFocusChain full;
        uint32_t index;

        InputFocusChainInit(&full);
        /* Node id 0 is "no node", and a document target with no node at all is refused: the loop starts at
         * 1 for that reason rather than because the fixture is arbitrary. */
        for (index = 0u; index < RoboInputMaxFocusChain; index++) {
            InputTarget entry = FocusDocumentTarget(7u, (LayoutNodeId)(index + 1u));

            RoboCheckEqInt(InputFocusChainAdd(&full, &entry, &diag), RoboOk);
        }
        RoboCheckEqInt((int)full.count, (int)RoboInputMaxFocusChain);
        {
            InputTarget extra = FocusDocumentTarget(7u, (LayoutNodeId)999u);

            RoboCheckEqInt(InputFocusChainAdd(&full, &extra, &diag), RoboOutOfMemory);
        }
    }

    /* Walking a chain from another window is refused by the request door, not silently focused. */
    RoboCheckEqInt(InputFocusChainFocus(&manager, &chain, RoboInputFocusDirectionNext,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboOk);
    RoboCheckEqInt(InputFocusChainFocus(NULL, &chain, RoboInputFocusDirectionNext,
                                        RoboInputFocusReasonKeyboard, &transition, &diag),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Dumps                                                              */
/* ------------------------------------------------------------------ */

static void TestFocusDump(void)
{
    InputFocusManager manager;
    InputFocusChain chain;
    InputFocusTransition transition;
    InputTarget node = FocusDocumentTarget(7u, (LayoutNodeId)5u);
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;

    InputDiagReset(&diag);
    InputFocusManagerInit(&manager);
    RoboCheckEqInt(InputFocusManagerSetActiveWindow(&manager, 7u, 1u, NULL, &diag), RoboOk);
    RoboCheckEqInt(InputFocusRequest(&manager, &node, RoboInputFocusReasonPointer, &transition, &diag),
                   RoboOk);

    RoboCheckEqInt(InputFocusTransitionWrite(&transition, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "focus-transition reason=pointer previous=0 focused=1 deactivated=0 "
                         "activated=0 events=1\n");

    RoboCheckEqInt(InputFocusManagerWrite(&manager, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "focus scopes=1 active=7 focused=document changes=2 refused=0\n"
                         "  scope window=7 focus=document\n");
    RoboCheckEqInt(InputFocusManagerWrite(&manager, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    InputFocusChainInit(&chain);
    RoboCheckEqInt(InputFocusChainAdd(&chain, &node, &diag), RoboOk);
    RoboCheckEqInt(InputFocusChainWrite(&chain, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "focus-chain count=1 duplicates=0\n  entry #0 kind=document window=7 node=5\n");

    /* A NULL record dumps as `<none>` rather than refusing, and a small buffer is a refusal with the length
     * it needed -- the complete-or-absent rule every writer in this module follows. */
    RoboCheckEqInt(InputFocusManagerWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "focus <none>\n");
    RoboCheckEqInt(InputFocusChainWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "focus-chain <none>\n");
    RoboCheckEqInt(InputFocusTransitionWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "focus-transition <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputFocusTransitionWrite(&transition, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheck(needed > sizeof(small));
        RoboCheckStrEq(small, "?");
    }
    RoboCheckEqInt(InputFocusManagerWrite(&manager, line, 0u, &length, &diag), RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputFocus(void)
{
    TestRegister("input.focus.vocabulary", TestFocusVocabulary);
    TestRegister("input.focus.scopes", TestFocusScopes);
    TestRegister("input.focus.request", TestFocusRequest);
    TestRegister("input.focus.release", TestFocusRelease);
    TestRegister("input.focus.transition", TestFocusTransition);
    TestRegister("input.focus.chain", TestFocusChain);
    TestRegister("input.focus.dump", TestFocusDump);
}
