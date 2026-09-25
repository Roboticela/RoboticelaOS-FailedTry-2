/*
 * Recording, replay, injection and the privilege boundary (Phase 13.49--13.51).
 *
 * ## Two claims, and why they are one file
 *
 * A **recording** is a stream of events a caller can feed back later, and **synthetic input** is an event a
 * caller made up rather than one hardware reported. The two meet at the only question that matters for
 * security -- *who is allowed to produce this event?* -- and they meet at the same place in the pipeline:
 * **the same queue, the same routing, the same dispatch.** A replay that took a shortcut around routing
 * would be a test that proves nothing, which is why this header has no "replay path" and no test-only door:
 * `InputReplayNext` hands a caller the next recorded event and the caller delivers it exactly as it delivers
 * a real one.
 *
 * ## The privilege ladder, as a number
 *
 * A note in a document is not a boundary; a comparison is. Five actors, ranked, and every event type names
 * the rank it needs:
 *
 *     NONE          0   page content: may inject **nothing**
 *     APPLICATION   1   an application: synthetic input confined to what it owns
 *     ACCESSIBILITY 2   an assistive tool: may reach the focused editor
 *     AUTOMATION    3   a test harness: anything a user could do
 *     SYSTEM        4   the runtime itself: hotplug, window activation, focus transitions
 *
 * `InputInjectionPrivilegeRequiredFor` is the table on the *event* side -- a `PointerDown` is what an
 * application may synthesize on its own surface, a `KeyDown` reaches a focused editor and needs
 * accessibility rank, a `DeviceDisconnected` is the system's -- and `InputInjector` is the door that
 * compares the two. Refusals are counted and named, because "who tried to inject this?" is the question a
 * security boundary exists to answer.
 *
 * ## Replay may not increase privilege
 *
 * A recording remembers the **highest** rank any of its events came from, and `InputReplayBegin` refuses an
 * actor below it. A stream the system recorded -- device connections, window switches -- may therefore not be
 * replayed by an application, which is the one rule that makes a recording a *capability* rather than a file
 * of keystrokes anybody can press play on.
 *
 * ## What this build does not claim
 *
 * It does not capture from hardware (`deviceReading` is 0), it does not persist a recording anywhere -- a
 * recording is a bounded array in the caller's own memory -- and it implements no authentication beyond the
 * rank its caller states. `RoboInputInjectionBoundariesImplemented` says the *rules* exist in code; it does
 * not say every caller is honest about its own rank.
 *
 * See `Documentation/Runtime/Input/Security.md` and `…/Replay.md`.
 */

#ifndef ROBO_INPUT_REPLAY_H
#define ROBO_INPUT_REPLAY_H

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputEvent.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Events one recording holds. Past this a record refuses and **counts**, so a lost event is visible. */
#define RoboInputRecordingMax 128u

/** Who is producing an event. Ranked: `NONE` < `APPLICATION` < `ACCESSIBILITY` < `AUTOMATION` < `SYSTEM`. */
typedef enum InputInjectionPrivilege {
    /** Page content, a script, a document: may inject **nothing**. */
    RoboInputPrivilegeNone = 0,
    /** An application: may synthesize input on what it owns -- its own window's surface. */
    RoboInputPrivilegeApplication = 1,
    /** An assistive tool: may reach the focused target, including its editor. */
    RoboInputPrivilegeAccessibility = 2,
    /** A test harness, a macro runner: anything a user could do. */
    RoboInputPrivilegeAutomation = 3,
    /** The runtime itself: device hotplug, window activation, focus transitions. */
    RoboInputPrivilegeSystem = 4
} InputInjectionPrivilege;

/** Number of defined ranks. Asserted positionally by `input.replay.*`. */
#define RoboInputInjectionPrivilegeCount 5u

/** Non-zero when @p privilege is a defined rank. */
int InputInjectionPrivilegeIsKnown(InputInjectionPrivilege privilege);

/** Human-readable rank name; never NULL; `"unknown"` when undefined. */
const char *InputInjectionPrivilegeName(InputInjectionPrivilege privilege);

/** The rank as a number, or -1 when undefined. The comparison a boundary is made of. */
int InputInjectionPrivilegeRank(InputInjectionPrivilege privilege);

/** Non-zero when an @p actor may do something that requires @p required. */
int InputInjectionPrivilegeAllows(InputInjectionPrivilege actor, InputInjectionPrivilege required);

/**
 * The rank an event **of this type** needs, whoever produced it.
 *
 * The table, and each row has a reason:
 *
 *   - pointer, wheel, touch, pen and gamepad events are `APPLICATION`: an application may synthesize them on
 *     its own surface, which is how a UI test drives a widget;
 *   - key, text-input and composition events are `ACCESSIBILITY`: they land in whatever holds focus, which may
 *     be another application's editor, so the rank that may produce them is the one assistive tools hold;
 *   - `FOCUS_IN`/`FOCUS_OUT` and `WINDOW_ACTIVATE`/`WINDOW_DEACTIVATE` are `SYSTEM`: they *are* the system's
 *     own transitions, and an application that could fabricate them could take focus from anybody;
 *   - `DEVICE_CONNECTED`/`DEVICE_DISCONNECTED` are `SYSTEM` for the same reason: a fabricated disconnect
 *     would release the real device's state.
 *
 * An undefined type -- including the cleared `NONE` -- asks for `SYSTEM`, because an event this layer cannot
 * name is not an event it will let a caller inject.
 */
InputInjectionPrivilege InputInjectionPrivilegeRequiredFor(InputEventType type);

/* ============================================================================================== *
 * A recording
 * ============================================================================================== */

/**
 * A bounded stream of events, owned by the caller.
 *
 * The events are stored **by value**, and that is the model's own guarantee rather than a copy of
 * convenience: an event is immutable after creation, so a recorded one cannot drift from the one that was
 * delivered.
 */
typedef struct InputRecording {
    /** The events, in the order they happened. */
    InputEvent events[RoboInputRecordingMax];
    /** Each event's timestamp, in the same order. */
    uint64_t timestamps[RoboInputRecordingMax];
    /** Events stored. */
    uint32_t count;
    /** Events this recording was asked to store and could not: reported, never silent. */
    uint32_t dropped;
    /** The **highest** rank any event came from, which is what a replay has to match. */
    InputInjectionPrivilege privilege;
    /** The first and last timestamps stored, for a caller that wants the span. */
    uint64_t firstMs;
    uint64_t lastMs;
    /** Bumped on every clear, so a stale handle to a recording's contents is detectable. */
    uint32_t serial;
} InputRecording;

/** Clear @p recording. Safe on NULL. */
void InputRecordingInit(InputRecording *recording);

/**
 * Record @p event at @p timestampMs, saying what produced it.
 *
 * A recording that is full refuses with `RoboOutOfMemory` and increments @ref InputRecording::dropped: the
 * alternative -- silently keeping the first N events of a long session -- is a recording that looks complete.
 * @p privilege **raises** the recording's own rank and never lowers it, so a stream that contained one system
 * event stays system.
 */
Result InputRecordingRecord(InputRecording *recording,
                            const InputEvent *event,
                            uint64_t timestampMs,
                            InputInjectionPrivilege privilege,
                            InputDiag *diag);

/** Events stored. */
uint32_t InputRecordingCount(const InputRecording *recording);

/** Event @p index and its timestamp, either out-pointer optional. `RoboNotFound` past the end. */
Result InputRecordingAt(const InputRecording *recording,
                        uint32_t index,
                        InputEvent *outEvent,
                        uint64_t *outTimestamp);

/** One complete line for @p recording, or @ref RoboNotSupported when it does not fit. */
Result InputRecordingWrite(const InputRecording *recording,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           InputDiag *diag);

/* ============================================================================================== *
 * A replay
 * ============================================================================================== */

/**
 * Feeding a recording back in.
 *
 * Borrows the recording -- it does not own it -- and hands the caller one event at a time. The caller
 * delivers each one *the same way* it delivers a hardware event, which is the whole point: there is no second
 * pipeline for a replay to drift from.
 */
typedef struct InputReplayState {
    /** The recording being replayed; borrowed, never freed here. */
    const InputRecording *recording;
    /** The next event to hand out. */
    uint32_t index;
    /** Who is replaying. */
    InputInjectionPrivilege actor;
    /** The serial the recording had when the replay began, so an edited recording is detected. */
    uint32_t serial;
    /** Events handed out. */
    uint32_t delivered;
    /** Non-zero once every event has been handed out. */
    int finished;
} InputReplayState;

/** Clear @p state to "nothing to replay". Safe on NULL. */
void InputReplayInit(InputReplayState *state);

/**
 * Begin replaying @p recording as @p actor.
 *
 * Refused with `RoboNotSupported` when @p actor ranks below the recording's own privilege -- a stream the
 * system recorded may not be replayed by an application -- and that refusal is what makes a recording a
 * capability rather than a file anybody may press play on. Refused with `RoboInvalidArgument` for an
 * undefined rank or a NULL recording.
 */
Result InputReplayBegin(InputReplayState *state,
                        const InputRecording *recording,
                        InputInjectionPrivilege actor,
                        InputDiag *diag);

/**
 * Hand out the next event.
 *
 * @p outEvent receives the event and @p outTimestamp its timestamp (both optional, though an event is the
 * point). `RoboNotFound` when the replay is finished, which is a *stated* end rather than an empty event.
 * Refused with `RoboNotFound` too when the recording was cleared since the replay began: the serial is how a
 * caller learns that its indices no longer mean what they meant.
 */
Result InputReplayNext(InputReplayState *state, InputEvent *outEvent, uint64_t *outTimestamp);

/** Events not yet handed out. */
uint32_t InputReplayRemaining(const InputReplayState *state);

/** One complete line for @p state, or @ref RoboNotSupported when it does not fit. */
Result InputReplayStateWrite(const InputReplayState *state,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             InputDiag *diag);

/* ============================================================================================== *
 * Injection
 * ============================================================================================== */

/**
 * The one door synthetic input goes through.
 *
 * It holds the actor's rank, counts what it injected and what it refused, and names the last refusal so a
 * failure a test does not expect says *why* rather than "returned 4".
 */
typedef struct InputInjector {
    /** Who this injector is. */
    InputInjectionPrivilege privilege;
    /** Events it produced. */
    uint32_t injected;
    /** Events it was not allowed to produce. */
    uint32_t refused;
    /** What the last refusal was called. Never NULL; empty when none. */
    const char *lastRefusal;
} InputInjector;

/**
 * Make @p injector an actor of @p privilege.
 *
 * @p privilege is the **caller's own claim about itself**, and that is a limit stated rather than hidden: a
 * build with a platform adapter would set it from the platform's identity, and this one has no way to check
 * it. `RoboInputInjectionBoundariesImplemented` claims the *rules*, not the honesty of a caller.
 */
Result InputInjectorInit(InputInjector *injector, InputInjectionPrivilege privilege, InputDiag *diag);

/** Non-zero when this injector may produce an event of @p type. */
int InputInjectorMayInject(const InputInjector *injector, InputEventType type);

/**
 * Check one event against the boundary without producing it.
 *
 * `RoboOk` when the rank is enough, `RoboNotSupported` when it is not -- and the injector's `refused` and
 * `lastRefusal` are updated by **`Inject`** and not by this door, so a caller may ask "would this work?"
 * without changing the numbers.
 */
Result InputInjectorCheck(const InputInjector *injector, const InputEvent *event);

/**
 * Produce one synthetic event.
 *
 * The event is validated and checked against the rank its *type* requires. A refusal is counted and named,
 * and changes nothing else: an injector that refused is still an injector.
 */
Result InputInjectorInject(InputInjector *injector, const InputEvent *event, InputDiag *diag);

/** One complete line for @p injector, or @ref RoboNotSupported when it does not fit. */
Result InputInjectorWrite(const InputInjector *injector,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_REPLAY_H */
