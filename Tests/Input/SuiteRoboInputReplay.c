/*
 * RoboInput replay and injection suite (Phase 13.49--13.51) — `input.replay.*`.
 *
 * Seven cases, and three of them are the security boundary rather than the plumbing:
 *
 *   `ranks`       the five ranks, positionally and by name, with the comparison that the boundary is made of
 *                 -- including an undefined rank, which is refused rather than treated as zero;
 *
 *   `required`    the **table**: every event type's required rank, asserted type by type rather than by the
 *                 counts, because a type that moved from `APPLICATION` to `ACCESSIBILITY` is a security
 *                 change and a suite that read a summary would not see it;
 *
 *   `recording`   the bounded by-value stream: the rank only *rises*, a full recording refuses and **counts**
 *                 what it dropped, and an at-index read past the end is not-found rather than an empty event;
 *
 *   `replay`      the gate: an application may not replay a recording the system made, rank equality is
 *                 enough, an empty recording begins finished, and a cleared recording invalidates a state
 *                 that was reading it;
 *
 *   `injector`    the door: a rank below what the type needs is refused **and named**, a refusal does not
 *                 stop the injector counting later successes, and an event the model itself would refuse is
 *                 refused here too -- synthetic input has no shortcut around validation;
 *
 *   `dumps`       the three lines and their determinism.
 */

#include "RoboInputTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Ranks                                                               */
/* ------------------------------------------------------------------ */

static void TestReplayRanks(void)
{
    RoboCheckEqInt((int)RoboInputPrivilegeNone, 0);
    RoboCheckEqInt((int)RoboInputPrivilegeApplication, 1);
    RoboCheckEqInt((int)RoboInputPrivilegeAccessibility, 2);
    RoboCheckEqInt((int)RoboInputPrivilegeAutomation, 3);
    RoboCheckEqInt((int)RoboInputPrivilegeSystem, 4);
    RoboCheckEqInt((int)RoboInputInjectionPrivilegeCount, 5);

    RoboCheckStrEq(InputInjectionPrivilegeName(RoboInputPrivilegeNone), "none");
    RoboCheckStrEq(InputInjectionPrivilegeName(RoboInputPrivilegeApplication), "application");
    RoboCheckStrEq(InputInjectionPrivilegeName(RoboInputPrivilegeAccessibility), "accessibility");
    RoboCheckStrEq(InputInjectionPrivilegeName(RoboInputPrivilegeAutomation), "automation");
    RoboCheckStrEq(InputInjectionPrivilegeName(RoboInputPrivilegeSystem), "system");
    RoboCheckStrEq(InputInjectionPrivilegeName((InputInjectionPrivilege)9), "unknown");
    RoboCheck(InputInjectionPrivilegeIsKnown(RoboInputPrivilegeSystem));
    RoboCheck(!InputInjectionPrivilegeIsKnown((InputInjectionPrivilege)9));

    RoboCheckEqInt(InputInjectionPrivilegeRank(RoboInputPrivilegeNone), 0);
    RoboCheckEqInt(InputInjectionPrivilegeRank(RoboInputPrivilegeSystem), 4);
    /* An undefined rank has no number, which is *-1* rather than 0: a zero would compare as "page content",
     * the most permissive answer, for a value nobody defined. */
    RoboCheckEqInt(InputInjectionPrivilegeRank((InputInjectionPrivilege)9), -1);

    RoboCheck(InputInjectionPrivilegeAllows(RoboInputPrivilegeSystem, RoboInputPrivilegeApplication));
    RoboCheck(InputInjectionPrivilegeAllows(RoboInputPrivilegeApplication, RoboInputPrivilegeApplication));
    RoboCheck(!InputInjectionPrivilegeAllows(RoboInputPrivilegeApplication, RoboInputPrivilegeAccessibility));
    /*
     * Page content may do nothing at all -- but that is the **table**'s answer, not this comparison's: the
     * lowest rank any event type asks for is `APPLICATION`, so a `NONE` actor fails every real check. The
     * comparison itself is a pure rank order, which is why `NONE` satisfies `NONE` here and why the suite
     * asserts the *table* for the security claim.
     */
    RoboCheck(!InputInjectionPrivilegeAllows(RoboInputPrivilegeNone, RoboInputPrivilegeApplication));
    RoboCheck(InputInjectionPrivilegeAllows(RoboInputPrivilegeNone, RoboInputPrivilegeNone));
    RoboCheck(!InputInjectionPrivilegeAllows((InputInjectionPrivilege)9, RoboInputPrivilegeNone));
    RoboCheck(!InputInjectionPrivilegeAllows(RoboInputPrivilegeSystem, (InputInjectionPrivilege)9));
}

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

static void TestReplayRequired(void)
{
    /* Pointer-family events: an application may synthesize them on its own surface. */
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPointerMove),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPointerDown),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPointerUp),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPointerCancel),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPointerEnter),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPointerLeave),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventWheel),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventTouchBegin),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventTouchMove),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventTouchEnd),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventTouchCancel),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPenDown),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPenMove),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPenUp),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventPenCancel),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventGamepadButtonDown),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventGamepadButtonUp),
                   (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventGamepadAxisChanged),
                   (int)RoboInputPrivilegeApplication);

    /* What reaches whatever holds focus: the rank assistive tools hold. */
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventKeyDown),
                   (int)RoboInputPrivilegeAccessibility);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventKeyUp),
                   (int)RoboInputPrivilegeAccessibility);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventKeyRepeat),
                   (int)RoboInputPrivilegeAccessibility);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventTextInput),
                   (int)RoboInputPrivilegeAccessibility);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventCompositionStart),
                   (int)RoboInputPrivilegeAccessibility);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventCompositionUpdate),
                   (int)RoboInputPrivilegeAccessibility);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventCompositionCommit),
                   (int)RoboInputPrivilegeAccessibility);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventCompositionCancel),
                   (int)RoboInputPrivilegeAccessibility);

    /* The system's own transitions, and hotplug. */
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventFocusIn),
                   (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventFocusOut),
                   (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventWindowActivate),
                   (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventWindowDeactivate),
                   (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventDeviceConnected),
                   (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventDeviceDisconnected),
                   (int)RoboInputPrivilegeSystem);

    /* An event this layer cannot name -- including the cleared value -- asks for the highest rank, so the
     * default is "refused" rather than "allowed". */
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor(RoboInputEventNone),
                   (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputInjectionPrivilegeRequiredFor((InputEventType)999),
                   (int)RoboInputPrivilegeSystem);

    /* *Every* type in the vocabulary asks for a rank this build defines: the table has no hole, which is the
     * property that makes "the default is system" a fallback rather than a hole being leaned on. */
    {
        int index;

        for (index = 1; index < (int)RoboInputEventTypeCount; index++) {
            RoboCheck(InputInjectionPrivilegeIsKnown(InputInjectionPrivilegeRequiredFor((InputEventType)index)));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Recording                                                           */
/* ------------------------------------------------------------------ */

static void TestReplayRecording(void)
{
    InputRecording recording;
    InputRecording other;
    InputEvent event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    InputEvent read;
    uint64_t timestamp = 0u;
    InputDiag diag;
    char line[512];
    size_t length = 0u;

    InputDiagReset(&diag);
    InputRecordingInit(&recording);
    RoboCheckEqInt((int)InputRecordingCount(&recording), 0);
    RoboCheckEqInt((int)recording.privilege, (int)RoboInputPrivilegeNone);
    RoboCheckEqInt((int)recording.serial, 1);

    /* Stored by **value**: an event is immutable, so a recorded one cannot drift from the one delivered. */
    RoboCheckEqInt(InputRecordingRecord(&recording, &event, 100u, RoboInputPrivilegeApplication, &diag),
                   RoboOk);
    RoboCheckEqInt((int)InputRecordingCount(&recording), 1);
    RoboCheckEqInt(InputRecordingAt(&recording, 0u, &read, &timestamp), RoboOk);
    RoboCheckEqInt((int)timestamp, 100);
    RoboCheckEqInt((int)read.type, (int)event.type);
    RoboCheckEqInt((int)read.payload.keyboard.physical, (int)event.payload.keyboard.physical);

    /* The rank rises and never falls: one system event makes the whole recording system. */
    RoboCheckEqInt(recording.privilege, (int)RoboInputPrivilegeApplication);
    RoboCheckEqInt(InputRecordingRecord(&recording, &event, 200u, RoboInputPrivilegeSystem, &diag), RoboOk);
    RoboCheckEqInt(recording.privilege, (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt(InputRecordingRecord(&recording, &event, 300u, RoboInputPrivilegeNone, &diag), RoboOk);
    RoboCheckEqInt(recording.privilege, (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputRecordingCount(&recording), 3);
    RoboCheckEqInt((int)recording.firstMs, 100);
    RoboCheckEqInt((int)recording.lastMs, 300);

    /* Undefined ranks and missing arguments are refusals. */
    RoboCheckEqInt(InputRecordingRecord(&recording, &event, 400u, (InputInjectionPrivilege)9, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputRecordingRecord(&recording, NULL, 400u, RoboInputPrivilegeApplication, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputRecordingRecord(NULL, &event, 400u, RoboInputPrivilegeApplication, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt((int)InputRecordingCount(&recording), 3);

    /* Past the end is not-found rather than an empty event. */
    RoboCheckEqInt(InputRecordingAt(&recording, 3u, &read, &timestamp), RoboNotFound);
    RoboCheckEqInt(InputRecordingAt(NULL, 0u, &read, &timestamp), RoboNotFound);

    /* A full recording refuses and **counts** what it dropped: a long session's recording is not silently
     * truncated to the first N events and then mistaken for the whole thing. */
    InputRecordingInit(&other);
    {
        uint32_t index;

        for (index = 0u; index < RoboInputRecordingMax; index++) {
            RoboCheckEqInt(InputRecordingRecord(&other, &event, (uint64_t)index,
                                                RoboInputPrivilegeApplication, &diag),
                           RoboOk);
        }
        RoboCheckEqInt((int)InputRecordingCount(&other), (int)RoboInputRecordingMax);
        RoboCheckEqInt(other.dropped, 0u);
        RoboCheckEqInt(InputRecordingRecord(&other, &event, 1000u, RoboInputPrivilegeApplication, &diag),
                       RoboOutOfMemory);
        RoboCheckEqInt(other.dropped, 1u);
        RoboCheckEqInt((int)InputRecordingCount(&other), (int)RoboInputRecordingMax);
    }

    RoboCheckEqInt(InputRecordingWrite(&recording, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line,
                   "recording serial=1 events=3 dropped=0 privilege=system first=100 last=300\n");
    RoboCheckEqInt(InputRecordingWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "recording <none>\n");
    InputRecordingInit(NULL);
}

/* ------------------------------------------------------------------ */
/* Replay                                                              */
/* ------------------------------------------------------------------ */

static void TestReplayState(void)
{
    InputRecording recording;
    InputReplayState state;
    InputEvent event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    InputEvent read;
    uint64_t timestamp = 0u;
    InputDiag diag;
    char line[512];
    size_t length = 0u;

    InputDiagReset(&diag);
    InputRecordingInit(&recording);
    InputReplayInit(&state);
    RoboCheckEqInt(InputReplayRemaining(&state), 0);

    /* An application's attempt at a recording the system made: refused, and that refusal is the reason a
     * recording is a capability rather than a file anybody can press play on. */
    RoboCheckEqInt(InputRecordingRecord(&recording, &event, 100u, RoboInputPrivilegeSystem, &diag), RoboOk);
    RoboCheckEqInt(InputRecordingRecord(&recording, &event, 200u, RoboInputPrivilegeApplication, &diag), RoboOk);
    RoboCheckEqInt(InputReplayBegin(&state, &recording, RoboInputPrivilegeApplication, &diag),
                   RoboNotSupported);
    RoboCheckEqInt((int)state.delivered, 0);
    RoboCheckEqInt(InputReplayBegin(&state, &recording, RoboInputPrivilegeNone, &diag), RoboNotSupported);

    /* Equality is enough -- a system actor may replay what another system actor recorded -- and so is
     * anything above it. */
    RoboCheckEqInt(InputReplayBegin(&state, &recording, RoboInputPrivilegeSystem, &diag), RoboOk);
    RoboCheckEqInt((int)state.actor, (int)RoboInputPrivilegeSystem);
    RoboCheckEqInt((int)InputReplayRemaining(&state), 2);
    RoboCheckEqInt(state.finished, 0);

    RoboCheckEqInt(InputReplayNext(&state, &read, &timestamp), RoboOk);
    RoboCheckEqInt((int)timestamp, 100);
    RoboCheckEqInt((int)read.type, (int)event.type);
    RoboCheckEqInt((int)InputReplayRemaining(&state), 1);
    RoboCheckEqInt(InputReplayNext(&state, &read, &timestamp), RoboOk);
    RoboCheckEqInt((int)timestamp, 200);
    /* Finished is a *stated* end: the next call is not-found, not an empty event. */
    RoboCheckEqInt(InputReplayNext(&state, &read, &timestamp), RoboNotFound);
    RoboCheckEqInt(state.finished, 1);
    RoboCheckEqInt((int)InputReplayRemaining(&state), 0);
    RoboCheckEqInt((int)state.delivered, 2);
    RoboCheckEqInt(InputReplayNext(&state, &read, NULL), RoboNotFound);

    /* An empty recording begins finished, which is a legitimate replay of nothing. */
    {
        InputRecording empty;

        InputRecordingInit(&empty);
        RoboCheckEqInt(InputReplayBegin(&state, &empty, RoboInputPrivilegeApplication, &diag), RoboOk);
        RoboCheckEqInt(state.finished, 1);
        RoboCheckEqInt(InputReplayNext(&state, &read, NULL), RoboNotFound);
    }

    /*
     * The gate again, from the other side: this recording still holds its two **system** events, so an
     * automation actor is refused -- a test harness may do anything a *user* could do, and a device
     * disconnect is not something a user does.
     */
    RoboCheckEqInt(InputReplayBegin(&state, &recording, RoboInputPrivilegeAutomation, &diag),
                   RoboNotSupported);

    /* A recording cleared while a replay is reading it: the indices a caller holds no longer mean what they
     * meant, and the replay says so rather than handing out somebody else's event. */
    RoboCheckEqInt(InputReplayBegin(&state, &recording, RoboInputPrivilegeSystem, &diag), RoboOk);
    InputRecordingInit(&recording);
    RoboCheckEqInt(InputReplayNext(&state, &read, &timestamp), RoboNotFound);
    RoboCheckEqInt(state.finished, 1);

    /* Undefined ranks and NULL arguments. */
    RoboCheckEqInt(InputReplayBegin(&state, &recording, (InputInjectionPrivilege)9, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputReplayBegin(&state, NULL, RoboInputPrivilegeSystem, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputReplayBegin(NULL, &recording, RoboInputPrivilegeSystem, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputReplayNext(NULL, &read, NULL), RoboInvalidArgument);
    /* A replay that has handed out everything answers not-found, which is a *stated* end. */
    RoboCheckEqInt(InputReplayNext(&state, &read, NULL), RoboNotFound);

    RoboCheckEqInt(InputReplayStateWrite(&state, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "replay actor=system privilege=none delivered=0 remaining=0 finished=1\n");
    RoboCheckEqInt(InputReplayStateWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "replay <none>\n");
    InputReplayInit(NULL);
}

/* ------------------------------------------------------------------ */
/* Injection                                                           */
/* ------------------------------------------------------------------ */

static void TestReplayInjector(void)
{
    InputInjector injector;
    InputEvent pointer = InputPointerMove(10, 20, 0u, RoboInputSpaceWindow, 7u);
    InputEvent key = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    InputEvent cleared;
    InputEvent invalid;
    InputDiag diag;
    char line[512];
    size_t length = 0u;

    InputDiagReset(&diag);
    InputEventInit(&cleared);
    InputEventInit(&invalid);

    /* Page content may inject nothing, and the answer is named rather than a bare status. */
    RoboCheckEqInt(InputInjectorInit(&injector, RoboInputPrivilegeNone, &diag), RoboOk);
    RoboCheckEqInt(InputInjectorMayInject(&injector, RoboInputEventPointerDown), 0);
    RoboCheckEqInt(InputInjectorCheck(&injector, &pointer), RoboNotSupported);
    /* `Check` asks without changing the numbers. */
    RoboCheckEqInt((int)injector.refused, 0);
    RoboCheckEqInt(InputInjectorInject(&injector, &pointer, &diag), RoboNotSupported);
    RoboCheckEqInt((int)injector.refused, 1);
    RoboCheckStrEq(injector.lastRefusal, "the injector's privilege is below what the event requires");
    RoboCheckEqInt((int)injector.injected, 0);

    /* An application: its own surface's pointer events, and not a keystroke -- which would reach whatever
     * holds focus, possibly another application's editor. */
    RoboCheckEqInt(InputInjectorInit(&injector, RoboInputPrivilegeApplication, &diag), RoboOk);
    RoboCheck(InputInjectorMayInject(&injector, RoboInputEventPointerDown));
    RoboCheck(!InputInjectorMayInject(&injector, RoboInputEventKeyDown));
    RoboCheckEqInt(InputInjectorInject(&injector, &pointer, &diag), RoboOk);
    RoboCheckEqInt((int)injector.injected, 1);
    RoboCheckEqInt(InputInjectorInject(&injector, &key, &diag), RoboNotSupported);
    RoboCheckEqInt((int)injector.refused, 1);
    RoboCheckEqInt((int)injector.injected, 1);
    /* A refusal does not stop the injector: the next allowed event still lands. */
    RoboCheckEqInt(InputInjectorInject(&injector, &pointer, &diag), RoboOk);
    RoboCheckEqInt((int)injector.injected, 2);

    /* An assistive tool: keys and text, but not a fabricated device disconnect. */
    RoboCheckEqInt(InputInjectorInit(&injector, RoboInputPrivilegeAccessibility, &diag), RoboOk);
    RoboCheckEqInt(InputInjectorInject(&injector, &key, &diag), RoboOk);
    RoboCheckEqInt(InputInjectorInject(&injector, &pointer, &diag), RoboOk);
    {
        InputEvent disconnect;

        InputDeviceDescriptor descriptor = InputFixtureDescriptor(RoboInputDeviceTypeKeyboard,
                                                                  (InputDeviceId)3u, "kbd2",
                                                                  RoboInputDeviceCapabilityKeyboardKeys);

        InputEventInit(&disconnect);
        RoboCheckEqInt(InputEventSetDeviceHotplug(&disconnect, RoboInputEventDeviceDisconnected,
                                                  RoboInputSourceKeyboard, &descriptor, &diag),
                       RoboOk);
        RoboCheckEqInt(InputInjectorInject(&injector, &disconnect, &diag), RoboNotSupported);
    }

    /* The system: everything, including hotplug. */
    RoboCheckEqInt(InputInjectorInit(&injector, RoboInputPrivilegeSystem, &diag), RoboOk);
    {
        InputEvent disconnect;

        InputDeviceDescriptor descriptor = InputFixtureDescriptor(RoboInputDeviceTypeKeyboard,
                                                                  (InputDeviceId)3u, "kbd2",
                                                                  RoboInputDeviceCapabilityKeyboardKeys);

        InputEventInit(&disconnect);
        RoboCheckEqInt(InputEventSetDeviceHotplug(&disconnect, RoboInputEventDeviceDisconnected,
                                                  RoboInputSourceKeyboard, &descriptor, &diag),
                       RoboOk);
        RoboCheckEqInt(InputInjectorInject(&injector, &disconnect, &diag), RoboOk);
    }
    RoboCheckEqInt(InputInjectorInject(&injector, &key, &diag), RoboOk);

    /*
     * Synthetic input has **no shortcut**: an event the model itself would refuse is refused here too, even
     * for the highest rank. That is the property that keeps "the same pipeline" true.
     */
    RoboCheckEqInt(InputInjectorInject(&injector, &cleared, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)injector.refused, 1);
    RoboCheckStrEq(injector.lastRefusal, "the event is not one this build can express");
    invalid.type = RoboInputEventKeyDown;
    RoboCheckEqInt(InputInjectorInject(&injector, &invalid, &diag), RoboInvalidArgument);
    RoboCheckStrEq(injector.lastRefusal, "the event failed the model's own validation");

    RoboCheckEqInt(InputInjectorInject(&injector, NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputInjectorCheck(NULL, &key), RoboInvalidArgument);
    RoboCheckEqInt(InputInjectorCheck(&injector, NULL), RoboInvalidArgument);
    RoboCheckEqInt(InputInjectorMayInject(NULL, RoboInputEventPointerDown), 0);
    RoboCheckEqInt(InputInjectorInit(NULL, RoboInputPrivilegeSystem, &diag), RoboInvalidArgument);
    {
        InputInjector fresh;

        RoboCheckEqInt(InputInjectorInit(&fresh, (InputInjectionPrivilege)9, &diag), RoboInvalidArgument);
    }

    RoboCheckEqInt(InputInjectorWrite(&injector, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line,
                   "injector privilege=system injected=2 refused=2 last="
                   "the event failed the model's own validation\n");
    RoboCheckEqInt(InputInjectorWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "injector <none>\n");
}

/* ------------------------------------------------------------------ */
/* Dumps                                                               */
/* ------------------------------------------------------------------ */

static void TestReplayDumps(void)
{
    InputRecording recording;
    InputRecording same;
    InputReplayState state;
    InputInjector injector;
    InputEvent event = InputKeyEvent(RoboPhysicalKeyA, RoboLogicalKeyA, 1, RoboInputModifiersNone);
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;

    InputDiagReset(&diag);
    InputRecordingInit(&recording);
    RoboCheckEqInt(InputRecordingRecord(&recording, &event, 1u, RoboInputPrivilegeAutomation, &diag), RoboOk);

    /* The same creation twice produces the same bytes: a reviewer diffs these. */
    InputRecordingInit(&same);
    RoboCheckEqInt(InputRecordingRecord(&same, &event, 1u, RoboInputPrivilegeAutomation, &diag), RoboOk);
    RoboCheckEqInt(InputRecordingWrite(&recording, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt(InputRecordingWrite(&same, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    RoboCheckEqInt(InputReplayBegin(&state, &recording, RoboInputPrivilegeAutomation, &diag), RoboOk);
    RoboCheckEqInt(InputReplayStateWrite(&state, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "replay actor=automation privilege=automation delivered=0 remaining=1 finished=0\n");

    RoboCheckEqInt(InputInjectorInit(&injector, RoboInputPrivilegeAutomation, &diag), RoboOk);
    RoboCheckEqInt(InputInjectorWrite(&injector, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line, "injector privilege=automation injected=0 refused=0 last=\n");

    /* Every dump refuses a buffer one byte short rather than emitting a prefix. */
    {
        char small[12];
        size_t needed = 0u;

        RoboCheckEqInt(InputRecordingWrite(&recording, small, sizeof(small), &needed, &diag), RoboNotSupported);
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt(InputReplayStateWrite(&state, small, sizeof(small), &needed, &diag), RoboNotSupported);
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt(InputInjectorWrite(&injector, small, sizeof(small), &needed, &diag), RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputReplay(void)
{
    TestRegister("input.replay.ranks", TestReplayRanks);
    TestRegister("input.replay.required", TestReplayRequired);
    TestRegister("input.replay.recording", TestReplayRecording);
    TestRegister("input.replay.state", TestReplayState);
    TestRegister("input.replay.injector", TestReplayInjector);
    TestRegister("input.replay.dumps", TestReplayDumps);
}
