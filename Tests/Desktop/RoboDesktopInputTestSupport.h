/*
 * Shared fixtures for the shell's input integration (Phase 13.66--13.77).
 *
 * Every suite in `Tests/Desktop/SuiteRoboDesktopInput*.c` includes this. It sits on top of
 * `RoboDesktopTestSupport.h` for the same reason that header sits on top of `RoboRenderTestSupport.h`:
 * the input integration is the *shell* plus the model, so a fixture for it is a desktop fixture plus
 * RoboInput, and a second copy of "open a desktop with a face, build a document for a window" would be a
 * second thing to keep in step with the shell.
 *
 * It holds the five things those suites would otherwise each copy:
 *
 *   a system     a `DesktopInput` over a screen, with coalescing **off** by default. Off is the choice a
 *                suite wants: coalescing merges high-frequency moves (13.47), and a case that asserts the
 *                *order* of four moves wants four dispatches rather than three merged and one emitted. The
 *                case that is about coalescing turns it back on and says so.
 *
 *   a clock      two doors, not one: `DesktopInputOpen` leaves the clock absent (`clockMs` NULL, so the
 *                latency trace stays empty and `GetDesktopInputLatency` answers zeroes -- the honest
 *                answer for a build that supplied no timebase), and `DesktopInputOpenWithClock` installs a
 *                counter the test advances by hand. A *hand-advanced* clock is what makes a latency case an
 *                assertion rather than a stopwatch: the numbers are the test's, so "dispatch took two
 *                milliseconds" is a fact about the code and not about the machine it ran on.
 *
 *   a log        a recording `InputListener`, because "the target received the event" is a claim about the
 *                *target* and the only way to make it is to register one. The log keeps the event type, the
 *                target's kind and window and layout node, the phase, and the target's flags, so a case can
 *                assert all five without five listeners.
 *
 *   an editor    a `RoboTextEditor` with the text a case names, plus the read that turns it back into bytes.
 *                It is RoboText's own editor (12.22) and not a stub, because 13.72's whole claim is that the
 *                input layer reaches *that* editor and no other.
 *
 *   a document   a real `DesktopDocument` for a window's client area plus a real RoboHTML document, so the
 *                two halves of 13.68 can be told apart: the layout tree is what a press hit-tests against,
 *                and the DOM handle is the identity a *consumer* resolves. A case that asserted a node id
 *                and never resolved a handle would be asserting half of what the phase claims.
 *
 * Every helper asserts rather than returning a status, which is the convention every support header in this
 * tree sets: a suite that had to check each step of its own scaffolding would drown its subject in harness.
 */

#ifndef ROBO_DESKTOP_INPUT_TEST_SUPPORT_H
#define ROBO_DESKTOP_INPUT_TEST_SUPPORT_H

#include "RoboDesktopTestSupport.h"

#include "RoboDesktop/RoboDesktopInput.h"

#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"

#include "RoboText/RoboTextEditing.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                            */
/* ------------------------------------------------------------------ */

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t DesktopInputLiveBlocks(void)
{
    return MemDebugLiveCount();
}

/* ------------------------------------------------------------------ */
/* A clock a test drives                                                */
/* ------------------------------------------------------------------ */

/** A monotonic millisecond counter that only ever moves when a test moves it. */
typedef struct DesktopInputClock {
    uint64_t ms;
} DesktopInputClock;

static inline uint64_t DesktopInputClockRead(void *userdata)
{
    DesktopInputClock *clock = (DesktopInputClock *)userdata;

    return clock->ms;
}

/** Advance the clock by @p ms. */
static inline void DesktopInputClockAdvance(DesktopInputClock *clock, uint64_t ms)
{
    clock->ms += ms;
}

/* ------------------------------------------------------------------ */
/* The system                                                          */
/* ------------------------------------------------------------------ */

/**
 * This build's input configuration with **coalescing off**.
 *
 * Off because a suite asserts order; see this header's comment.
 */
static inline DesktopInputConfig GetRoboDesktopInputConfig(void)
{
    DesktopInputConfig config;

    DesktopInputConfigInit(&config);
    config.enableCoalescing = 0;
    return config;
}

/** A system over @p desktop, with no clock. Asserts. */
static inline DesktopInput *DesktopInputOpen(Desktop *desktop)
{
    DesktopInputConfig config = GetRoboDesktopInputConfig();
    DesktopInput *input = NULL;

    RoboCheckEqInt(DesktopInputCreate(desktop, &config, &input, NULL), RoboOk);
    RoboCheck(input != NULL);
    RoboCheckEqInt(DesktopInputIsLive(input), 1);
    /* The two devices the constructor connects: a bridged event carries a device identity because of them. */
    RoboCheckEqInt((int)DesktopInputDeviceCount(input), 2);
    return input;
}

/** The same, with @p clock supplying the timebase (13.48/13.77.12). */
static inline DesktopInput *DesktopInputOpenWithClock(Desktop *desktop, DesktopInputClock *clock)
{
    DesktopInputConfig config = GetRoboDesktopInputConfig();
    DesktopInput *input = NULL;

    clock->ms = 0u;
    config.clockMs = DesktopInputClockRead;
    config.clockUserdata = clock;
    RoboCheckEqInt(DesktopInputCreate(desktop, &config, &input, NULL), RoboOk);
    RoboCheck(input != NULL);
    return input;
}

static inline void DesktopInputClose(DesktopInput *input)
{
    DesktopInputDestroy(input);
}

/**
 * Push @p event and run one iteration with no wait. Asserts the push was accepted.
 *
 * The two-call shape a case wants, because "push then step" is what `GetDesktopInputStep` *is*: nothing here
 * has a second routing path, and a fixture that reached into the drain would be testing a private door.
 */
static inline void DesktopInputFeed(DesktopInput *input, const InputEvent *event, DesktopInputStep *outStep)
{
    DesktopInputStep step;

    DesktopInputStepInit(&step);
    RoboCheckEqInt(DesktopInputPush(input, event, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopInputStep(input, 0u, &step, NULL), RoboOk);
    if (outStep) {
        *outStep = step;
    }
}

/** Push @p event without stepping: for a case that wants to queue several first. */
static inline void DesktopInputQueue(DesktopInput *input, const InputEvent *event)
{
    RoboCheckEqInt(DesktopInputPush(input, event, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* A recording listener                                                */
/* ------------------------------------------------------------------ */

/** Calls one log holds. Enough for a whole scenario's worth of events. */
#define RoboDesktopInputLogMax 64u

/** What a listener saw, one row per call. */
typedef struct DesktopInputLog {
    uint32_t count;
    InputEventType types[RoboDesktopInputLogMax];
    InputTargetKind kinds[RoboDesktopInputLogMax];
    WindowId windows[RoboDesktopInputLogMax];
    LayoutNodeId nodes[RoboDesktopInputLogMax];
    InputPhase phases[RoboDesktopInputLogMax];
    uint32_t flags[RoboDesktopInputLogMax];
    /** Non-zero when any call saw the default action cancelled. */
    int prevented;
    /** The callback's answer, so a case can make a listener fail. */
    Result answer;
} DesktopInputLog;

static inline void DesktopInputLogInit(DesktopInputLog *log)
{
    memset(log, 0, sizeof(*log));
    log->answer = RoboOk;
}

static inline Result DesktopInputLogCallback(void *userdata,
                                             const InputEvent *event,
                                             InputEventContext *context)
{
    DesktopInputLog *log = (DesktopInputLog *)userdata;
    uint32_t index;

    if (!log || !event || !context) {
        return RoboInvalidArgument;
    }
    if (log->count < RoboDesktopInputLogMax) {
        index = log->count;
        log->types[index] = event->type;
        log->kinds[index] = context->currentTarget.kind;
        log->windows[index] = context->currentTarget.window;
        log->nodes[index] = context->currentTarget.layoutNode;
        log->phases[index] = context->phase;
        log->flags[index] = context->currentTarget.flags;
    }
    log->count++;
    if (context->defaultPrevented) {
        log->prevented = 1;
    }
    return log->answer;
}

/** How many calls saw @p type. */
static inline uint32_t DesktopInputLogCount(const DesktopInputLog *log, InputEventType type)
{
    uint32_t index;
    uint32_t found = 0u;

    for (index = 0u; index < log->count && index < RoboDesktopInputLogMax; index++) {
        if (log->types[index] == type) {
            found++;
        }
    }
    return found;
}

/** The index of the first call that saw @p type, or -1. */
static inline int DesktopInputLogFirst(const DesktopInputLog *log, InputEventType type)
{
    uint32_t index;

    for (index = 0u; index < log->count && index < RoboDesktopInputLogMax; index++) {
        if (log->types[index] == type) {
            return (int)index;
        }
    }
    return -1;
}

/** How many calls ran in @p phase. */
static inline uint32_t DesktopInputLogPhaseCount(const DesktopInputLog *log, InputPhase phase)
{
    uint32_t index;
    uint32_t found = 0u;

    for (index = 0u; index < log->count && index < RoboDesktopInputLogMax; index++) {
        if (log->phases[index] == phase) {
            found++;
        }
    }
    return found;
}

/** Register @p log on @p target for @p phase. Asserts. */
static inline void DesktopInputLogOn(DesktopInput *input,
                                     const InputTarget *target,
                                     InputPhase phase,
                                     DesktopInputLog *log)
{
    InputListener listener;

    memset(&listener, 0, sizeof(listener));
    listener.target = *target;
    listener.phase = phase;
    listener.callback = DesktopInputLogCallback;
    listener.userdata = log;
    RoboCheckEqInt(DesktopInputAddListener(input, &listener, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Targets                                                             */
/* ------------------------------------------------------------------ */

/** A `WINDOW` target for @p window in @p space. Asserts. */
static inline InputTarget DesktopInputWindowTarget(WindowId window, InputSpace space)
{
    InputTarget target;

    InputTargetInit(&target);
    RoboCheckEqInt(InputTargetSetWindow(&target, window, space, NULL), RoboOk);
    return target;
}

/** The `WINDOW` target of a shell window, in screen space. Asserts. */
static inline InputTarget DesktopInputShellWindowTarget(DesktopWindowId window)
{
    return DesktopInputWindowTarget((WindowId)window, RoboInputSpaceScreen);
}

/**
 * A route with only the frame rectangle: enough for screen and window, and not for a document conversion.
 *
 * Both halves of a route are flags rather than implied, which is what makes "a conversion refuses without
 * the geometry it needs" a case a caller can write.
 */
static inline InputRoute DesktopInputRouteFrameOnly(int32_t x, int32_t y, int32_t w, int32_t h)
{
    InputRoute route;

    InputRouteInit(&route);
    route.window = 7u;
    route.windowBounds = MakeGfxRect(x, y, w, h);
    route.hasWindowBounds = 1;
    return route;
}

/* ------------------------------------------------------------------ */
/* A DOM document                                                      */
/* ------------------------------------------------------------------ */

/** Parse @p html into a real RoboHTML document. Asserts. */
static inline HtmlDocumentId DesktopInputDocument(const char *html)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlSource source;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    HtmlSourceInit(&source);
    RoboCheckEqInt(HtmlSourceString(&source, html, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &source, NULL, NULL), RoboOk);
    return id;
}

static inline void DesktopInputDocumentClose(HtmlDocumentId id)
{
    (void)HtmlDocumentDestroy(id, NULL);
}

/** The document's root handle, asserted well-formed. */
static inline HtmlNodeHandle DesktopInputDocumentRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/* ------------------------------------------------------------------ */
/* An editor                                                           */
/* ------------------------------------------------------------------ */

/** An editor whose text is @p text (or empty for NULL). Asserts. */
static inline RoboTextEditor *DesktopInputEditor(const char *text)
{
    RoboTextEditor *editor = NULL;
    RoboTextDiag diag;

    RoboTextDiagReset(&diag);
    RoboCheckEqInt(RoboTextEditorCreate(&editor, &diag), RoboOk);
    RoboCheck(editor != NULL);
    if (text && text[0] != '\0') {
        RoboCheckEqInt(RoboTextEditorInsert(editor, text, strlen(text), NULL, &diag), RoboOk);
    }
    return editor;
}

static inline void DesktopInputEditorClose(RoboTextEditor *editor)
{
    RoboTextEditorDestroy(editor);
}

/** The editor's bytes; @p outLength receives the byte count. Never NULL. */
static inline const char *DesktopInputEditorBytes(const RoboTextEditor *editor, size_t *outLength)
{
    const RoboTextBuffer *buffer = RoboTextEditorText(editor);

    if (!buffer) {
        if (outLength) {
            *outLength = 0u;
        }
        return "";
    }
    if (outLength) {
        *outLength = RoboTextBufferByteLength(buffer);
    }
    return (const char *)RoboTextBufferBytes(buffer);
}

/** Assert the editor's text is exactly @p expected. */
static inline void DesktopInputEditorIs(const RoboTextEditor *editor, const char *expected, const char *what)
{
    size_t length = 0u;
    const char *bytes = DesktopInputEditorBytes(editor, &length);
    size_t expectedLength = strlen(expected);

    if (length != expectedLength || (length > 0u && memcmp(bytes, expected, length) != 0)) {
        TestFail(__FILE__, __LINE__, "%s: editor is \"%.*s\" (%u bytes), expected \"%s\" (%u bytes)", what,
                 (int)length, bytes, (unsigned)length, expected, (unsigned)expectedLength);
    }
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/**
 * A pointer event at @p x,@p y in screen space, built through the *public* doors.
 *
 * Built rather than filled in, so a fixture cannot construct a record the model's own validator would
 * refuse: a case that went wrong in a later assertion changed something *after* this call.
 */
static inline InputEvent DesktopInputPointerEventIn(InputEventType type,
                                                    int32_t x,
                                                    int32_t y,
                                                    MouseButton button,
                                                    uint32_t buttons,
                                                    WindowId window,
                                                    InputSpace space)
{
    InputEvent event;
    InputPointerData data;
    InputDiag diag;

    InputDiagReset(&diag);
    InputPointerDataInit(&data);
    RoboCheckEqInt(InputPointerDataSet(&data, RoboInputPointerIdPrimary, MakeGfxPoint(x, y), button,
                                      buttons),
                   RoboOk);
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetPointer(&event, type, RoboInputSourceMouse, &data, space, &diag),
                   RoboOk);
    InputEventSetWindow(&event, window);
    return event;
}

static inline InputEvent DesktopInputPointerEvent(InputEventType type,
                                                  int32_t x,
                                                  int32_t y,
                                                  MouseButton button,
                                                  uint32_t buttons,
                                                  WindowId window)
{
    return DesktopInputPointerEventIn(type, x, y, button, buttons, window, RoboInputSpaceScreen);
}

/** A pointer move at @p x,@p y in screen space, no buttons. */
static inline InputEvent DesktopInputMoveEvent(int32_t x, int32_t y, WindowId window)
{
    return DesktopInputPointerEvent(RoboInputEventPointerMove, x, y, RoboMouseButtonNone, 0u, window);
}

/** A pointer press of the left button at @p x,@p y in screen space. */
static inline InputEvent DesktopInputDownEvent(int32_t x, int32_t y, WindowId window)
{
    return DesktopInputPointerEvent(RoboInputEventPointerDown, x, y, RoboMouseButtonLeft,
                                    InputButtonMaskApply(0u, RoboMouseButtonLeft, 1), window);
}

/** A release of the left button at @p x,@p y in screen space. */
static inline InputEvent DesktopInputUpEvent(int32_t x, int32_t y, WindowId window)
{
    return DesktopInputPointerEvent(RoboInputEventPointerUp, x, y, RoboMouseButtonLeft, 0u, window);
}

/**
 * A key press of @p physical with @p modifiers, whose **text the layout derives**.
 *
 * No text is put on the event: 13.11's whole claim is that a consumer does not read a scan code and a
 * producer does not type a character, and the pipeline in between is what these cases are testing.
 */
static inline InputEvent DesktopInputKeyDown(PhysicalKey physical, InputModifiers modifiers)
{
    InputEvent event;
    InputKeyboardData data;
    InputDiag diag;

    InputDiagReset(&diag);
    InputKeyboardDataInit(&data);
    RoboCheckEqInt(InputKeyboardDataSet(&data, physical, RoboLogicalKeyUnknown, RoboKeyActionDown, 0,
                                       NULL),
                   RoboOk);
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyDown, RoboInputSourceKeyboard, &data,
                                        &diag),
                   RoboOk);
    InputEventSetModifiers(&event, modifiers, &diag);
    InputEventSetWindow(&event, 3u);
    return event;
}

/** A key *release* of @p physical with @p modifiers, for a case that wants the pair. */
static inline InputEvent DesktopInputKeyUp(PhysicalKey physical, InputModifiers modifiers)
{
    InputEvent event;
    InputKeyboardData data;
    InputDiag diag;

    InputDiagReset(&diag);
    InputKeyboardDataInit(&data);
    RoboCheckEqInt(InputKeyboardDataSet(&data, physical, RoboLogicalKeyUnknown, RoboKeyActionUp, 0,
                                       NULL),
                   RoboOk);
    InputEventInit(&event);
    RoboCheckEqInt(InputEventSetKeyboard(&event, RoboInputEventKeyUp, RoboInputSourceKeyboard, &data,
                                        &diag),
                   RoboOk);
    InputEventSetModifiers(&event, modifiers, &diag);
    InputEventSetWindow(&event, 3u);
    return event;
}

/** The mid-point of a shell window's title bar, where a move drag begins. */
static inline GfxPoint DesktopInputTitlePoint(Desktop *desktop, DesktopWindowId window)
{
    DesktopWindowMetrics metrics;
    GfxRect bounds;
    GfxPoint point;

    DesktopWindowMetricsInit(&metrics);
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    point.x = bounds.x + bounds.width / 2;
    /* Below the resize band, or the press would be a resize (13.67). */
    point.y = bounds.y + metrics.resizeBand + metrics.titleHeight / 2;
    return point;
}

/** The mid-point of a shell window's client area. */
static inline GfxPoint DesktopInputClientPoint(Desktop *desktop, DesktopWindowId window)
{
    GfxRect client;

    RoboCheckEqInt(DesktopWindowClientBounds(desktop, window, &client), RoboOk);
    return MakeGfxPoint(client.x + client.width / 2, client.y + client.height / 2);
}

#endif /* ROBO_DESKTOP_INPUT_TEST_SUPPORT_H */
