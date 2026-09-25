/*
 * The shell's input integration (Phase 13.66--13.77) — see `RoboDesktopInput.h`.
 *
 * The file is six pieces, in the order an event meets them:
 *
 *   **the providers**    hit test, path and listeners — the three seams `RoboInputDispatch.h` names,
 *                        answered from the shell's own window list and its own listener store;
 *   **the pump**         the window queue through the bridge and the coalescer into the input queue;
 *   **the target**       what an event is about, from the hit test (a pointer) or from the focus (a key);
 *   **the dispatch**     capture → target → bubble through the caller's listeners;
 *   **the action**       what RoboInput said to do: focus, navigate, activate, scroll, command;
 *   **the shell**        the two listeners and the two command targets that make this a *consumer* rather
 *                        than a second input system.
 *
 * Two numbers decide most of what this file does, and they are worth stating once:
 *
 *   **A `DesktopWindowId` is the identity an `InputTarget` carries.** This build has one window system — the
 *   shell's — so a target naming a shell window and a target naming a native window are the same identity
 *   written once. A later phase with real application windows maps between them instead of casting; nothing
 *   here would have to change shape to accept that, because the model was built for opaque ids.
 *
 *   **The active scope is the window the event is about.** Focus in the model is per-window, and this build
 *   has one shell window per scope, so `DesktopWindowRaise` (which already moves focus) and the model's focus
 *   manager are kept in step from one door — `ActivateWindow` — rather than from two that could drift.
 */

#include "RoboDesktop/RoboDesktopInput.h"

#include "RoboBase/RoboLog.h"
#include "RoboDesktopInternal.h"
#include "RoboMem/RoboMem.h"

#include "RoboInput/RoboInputBridge.h"
#include "RoboInput/RoboInputClipboard.h"
#include "RoboInput/RoboInputCoalescing.h"
#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputCoordinates.h"
#include "RoboInput/RoboInputCursor.h"
#include "RoboInput/RoboInputDispatch.h"
#include "RoboInput/RoboInputDragDrop.h"
#include "RoboInput/RoboInputFocus.h"
#include "RoboInput/RoboInputKeyboard.h"
#include "RoboInput/RoboInputLayout.h"
#include "RoboInput/RoboInputPointer.h"
#include "RoboInput/RoboInputPointerState.h"
#include "RoboInput/RoboInputQueue.h"
#include "RoboInput/RoboInputSession.h"
#include "RoboInput/RoboInputShortcut.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputText.h"
#include "RoboInput/RoboInputTextBridge.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintHitTest.h"

#include "RoboText/RoboTextBuffer.h"

#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowEventLoop.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Internal shape                                                      */
/* ------------------------------------------------------------------ */

/**
 * One shell listener.
 *
 * `wildcard` never leaves this table: the shell needs one listener for *the window under the pointer*, which
 * is a different target on every event, so the store keeps a sentinel and the provider substitutes the
 * **actual** queried target. What the dispatcher receives is therefore always a valid `InputListener`, which
 * is the only thing it promised to assume.
 */
typedef struct DesktopInputListenerEntry {
    InputListener listener;
    int wildcard;
} DesktopInputListenerEntry;

typedef struct DesktopInputContentEntry {
    DesktopInputContent content;
    int live;
    InputEditorTarget editor;
    int editorLive;
} DesktopInputContentEntry;

struct DesktopInput {
    int live;
    /** Borrowed: the shell never outlives the system that drives it. */
    Desktop *desktop;
    DesktopInputConfig config;

    InputQueue *queue;
    InputSession session;
    InputDispatcher dispatcher;
    InputListenerProvider provider;
    DesktopInputListenerEntry listeners[RoboDesktopInputMaxListeners];
    uint32_t listenerCount;

    InputHitTestProvider hitTest;
    InputPathProvider path;

    InputShortcutManager shortcuts;
    InputCommandRouter router;
    InputCursorManager cursors;
    InputCoalescer coalescer;
    InputBridgeOptions bridge;

    DesktopInputContentEntry contents[RoboDesktopInputMaxContents];

    InputLayout layout;
    int layoutLive;
    InputClipboardData clipboard;
    int clipboardLive;
    InputEditorTarget *clipboardEditor;

    InputDeviceId keyboardId;
    InputDeviceId mouseId;

    /** The pointer this build tracks. A mouse has exactly one. */
    InputPointerId pointer;
    /** The subject of a running window drag, if any. */
    DesktopWindowId dragWindow;
    int32_t dragGrabX;
    int32_t dragGrabY;

    DesktopInputStats stats;
    /** The last action the shell performed, for a caller that reports it. */
    char lastNote[RoboDesktopInputNoteMax];
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static DesktopWindowId AsDesktopWindow(WindowId window)
{
    return (DesktopWindowId)window;
}

static WindowId AsWindowId(DesktopWindowId window)
{
    return (WindowId)window;
}

static int WindowIsNone(DesktopWindowId window)
{
    return window == RoboDesktopWindowIdNone ? 1 : 0;
}

static void NoteSet(DesktopInput *input, const char *text)
{
    size_t index = 0u;

    if (!input) {
        return;
    }
    if (!text) {
        input->lastNote[0] = '\0';
        return;
    }
    while (text[index] != '\0' && index + 1u < sizeof(input->lastNote)) {
        input->lastNote[index] = text[index];
        index++;
    }
    input->lastNote[index] = '\0';
}

/** Report a RoboInput refusal through the shell's own diagnostic type. */
static Result ShellDiag(DesktopDiag *diag, const InputDiag *from, Result status, const char *what)
{
    if (diag) {
        diag->code = status;
        diag->domain = RoboDesktopDomainSurface;
        diag->message = (from && from->message) ? from->message : what;
        diag->file = __FILE__;
        diag->line = __LINE__;
    }
    return status;
}

void DesktopInputConfigInit(DesktopInputConfig *config)
{
    if (!config) {
        return;
    }
    config->queueCapacity = RoboDesktopInputDefaultQueueCapacity;
    config->lineHeightPx = 16;
    config->enableCoalescing = 1;
    config->window = RoboWindowIdNone;
}

/* ------------------------------------------------------------------ */
/* Route and target resolution (13.70 / 13.71)                         */
/* ------------------------------------------------------------------ */

Result DesktopInputRouteForWindow(const DesktopInput *input,
                                  DesktopWindowId window,
                                  InputRoute *out,
                                  DesktopDiag *diag)
{
    GfxRect bounds;
    GfxRect client;

    if (!input || !out) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a route are required");
    }
    InputRouteInit(out);
    if (WindowIsNone(window)) {
        /* The shell's own native window, when it has one: the route an event about the shell itself needs. */
        if (input->config.window != RoboWindowIdNone) {
            out->window = (uint64_t)input->config.window;
        }
        return RoboOk;
    }
    if (DesktopWindowBounds(input->desktop, window, &bounds) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                               "the window is not one this shell holds");
    }
    out->window = (uint64_t)window;
    out->windowBounds = bounds;
    out->hasWindowBounds = 1;
    if (DesktopWindowClientBounds(input->desktop, window, &client) == RoboOk) {
        out->clientBounds = client;
        out->hasClientBounds = 1;
    }
    return RoboOk;
}

static const DesktopInputContentEntry *FindContent(const DesktopInput *input, DesktopWindowId window)
{
    uint32_t index;

    for (index = 0u; index < RoboDesktopInputMaxContents; index++) {
        if (input->contents[index].live && input->contents[index].content.window == window) {
            return &input->contents[index];
        }
    }
    return NULL;
}

static DesktopInputContentEntry *FindContentMutable(DesktopInput *input, DesktopWindowId window)
{
    uint32_t index;

    for (index = 0u; index < RoboDesktopInputMaxContents; index++) {
        if (input->contents[index].live && input->contents[index].content.window == window) {
            return &input->contents[index];
        }
    }
    return NULL;
}

/** The front, visible window: what a screen-only shell calls "the active one". */
static DesktopWindowId FrontWindow(const DesktopInput *input)
{
    const Desktop *desktop = input->desktop;
    size_t index;

    if (!desktop) {
        return RoboDesktopWindowIdNone;
    }
    for (index = desktop->windowCount; index > 0u; index--) {
        const DesktopWindow *window = &desktop->windows[index - 1u];

        if (window->visible && window->state == RoboDesktopWindowOpen) {
            return window->id;
        }
    }
    return RoboDesktopWindowIdNone;
}

/**
 * What a point resolves to, as a target.
 *
 * The shell's own hit test first (which window, and which part of it), then — when a caller registered
 * content with a layout tree — `PaintHitTest` inside that window's client area, which is the only way an
 * answer can name a *node*. The second step is skipped entirely when there is no tree, and that absence is
 * reported rather than papered over: a press in a window with no document resolves to the window.
 */
static Result ResolvePointInternal(DesktopInput *input,
                                   GfxPoint point,
                                   InputSpace space,
                                   InputTarget *outTarget,
                                   GfxPoint *outDocPoint,
                                   DesktopHit *outHit,
                                   DesktopWindowId *outWindow,
                                   int *outResolved,
                                   DesktopDiag *diag)
{
    DesktopHit hit;
    DesktopWindowId window;
    InputRoute route;
    InputDiag idiag;
    InputSpace targetSpace;
    GfxPoint screen;
    GfxPoint docPoint;
    Result status;

    if (!input || !outTarget) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a target are required");
    }
    InputDiagReset(&idiag);
    DesktopHitInit(&hit);
    InputRouteInit(&route);

    targetSpace = InputSpaceIsKnown(space) ? space : RoboInputSpaceScreen;
    docPoint = point;

    /* The plane the shell tests in: screen. A point that came in on another plane is converted first, and a
     * conversion that cannot be done is refused rather than guessed at. */
    screen = point;
    if (targetSpace != RoboInputSpaceScreen) {
        status = InputConvertPoint(point, targetSpace, RoboInputSpaceScreen, NULL, &screen, &idiag);
        if (status != RoboOk) {
            return ShellDiag(diag, &idiag, status, "the point is on a plane this build cannot convert");
        }
    }
    status = DesktopHitTest(input->desktop, screen, &hit);
    if (status != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                               "the shell could not resolve the point");
    }

    window = hit.window;
    if (WindowIsNone(window)) {
        window = AsDesktopWindow(input->config.window);
    }
    if (WindowIsNone(window)) {
        window = FrontWindow(input);
    }
    if (WindowIsNone(window)) {
        /* A screen-only shell with no window at all: there is no target this build can name, and saying so
         * is more useful than inventing one. */
        InputTargetInit(outTarget);
        if (outDocPoint) {
            *outDocPoint = point;
        }
        if (outHit) {
            *outHit = hit;
        }
        if (outWindow) {
            *outWindow = RoboDesktopWindowIdNone;
        }
        if (outResolved) {
            *outResolved = 0;
        }
        return RoboNotFound;
    }

    (void)DesktopInputRouteForWindow(input, window, &route, NULL);
    if (route.hasClientBounds) {
        status = InputConvertPoint(screen, RoboInputSpaceScreen, RoboInputSpaceDocument, &route,
                                   &docPoint, &idiag);
        if (status != RoboOk) {
            docPoint = point;
        }
    }

    status = InputTargetSetWindow(outTarget, AsWindowId(window), targetSpace, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the resolved window is not a target");
    }

    /* A registered document turns a client-area answer into a node answer (13.68/13.70). */
    if (hit.area == RoboDesktopAreaWindowContent) {
        const DesktopInputContentEntry *entry = FindContent(input, window);

        if (entry && (entry->content.tree ||
                      HtmlNodeHandleIsWellformed(&entry->content.documentNode))) {
            LayoutNodeId node = RoboLayoutNodeIdNone;
            uint32_t flags = 0u;

            if (entry->content.tree) {
                PaintHitTestResult result;
                PaintDiag paintDiag;
                LayoutPoint layoutPoint;

                layoutPoint.x = docPoint.x;
                layoutPoint.y = docPoint.y;
                PaintHitTestResultInit(&result);
                PaintDiagReset(&paintDiag);
                if (PaintHitTest(entry->content.tree, layoutPoint, &result, NULL, &paintDiag) ==
                        RoboOk &&
                    PaintHitTestResultIsHit(&result)) {
                    node = PaintHitTestResultNode(&result);
                    /* The input model's target flags deliberately mirror paint's, bit for bit — the
                     * `RoboInputTarget.h` comment states the rule and `input.target.fromHitTest` is the case
                     * that keeps them equal. So the mirrored constants are the right ones to test here. */
                    if (PaintHitTestResultHasFlag(&result, RoboInputTargetFlagText)) {
                        flags |= RoboInputTargetFlagText;
                    }
                    if (PaintHitTestResultHasFlag(&result, RoboInputTargetFlagContent)) {
                        flags |= RoboInputTargetFlagContent;
                    }
                    if (PaintHitTestResultHasFlag(&result, RoboInputTargetFlagTransparent)) {
                        flags |= RoboInputTargetFlagTransparent;
                    }
                }
            }
            if (InputTargetSetDocument(outTarget, AsWindowId(window),
                                       &entry->content.documentNode, node, flags, targetSpace,
                                       &idiag) != RoboOk) {
                /* A document target that cannot be formed is the window's, which is still an answer. */
                (void)InputTargetSetWindow(outTarget, AsWindowId(window), targetSpace, &idiag);
            }
        }
    }

    if (outDocPoint) {
        *outDocPoint = docPoint;
    }
    if (outHit) {
        *outHit = hit;
    }
    if (outWindow) {
        *outWindow = window;
    }
    if (outResolved) {
        *outResolved = 1;
    }
    return RoboOk;
}

Result DesktopInputResolvePoint(DesktopInput *input,
                                GfxPoint point,
                                InputSpace space,
                                InputTarget *outTarget,
                                GfxPoint *outClientPoint,
                                DesktopHit *outHit,
                                DesktopWindowId *outWindow,
                                DesktopDiag *diag)
{
    int resolved = 0;

    return ResolvePointInternal(input, point, space, outTarget, outClientPoint, outHit, outWindow,
                                &resolved, diag);
}

InputSpace DesktopInputEventSpace(const DesktopInput *input)
{
    if (!input) {
        return RoboInputSpaceUnknown;
    }
    return input->bridge.preferScreenCoords ? RoboInputSpaceScreen : RoboInputSpaceDocument;
}

/* ------------------------------------------------------------------ */
/* The three providers (13.29--13.31)                                  */
/* ------------------------------------------------------------------ */

static Result HitTestProvider(void *userdata,
                              const InputRoute *route,
                              GfxPoint point,
                              InputSpace space,
                              InputTarget *outTarget,
                              InputDiag *diag)
{
    DesktopInput *input = (DesktopInput *)userdata;
    GfxPoint docPoint;
    int resolved = 0;

    if (!input || !outTarget) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a system and a target are required");
    }
    (void)route;
    if (ResolvePointInternal(input, point, space, outTarget, &docPoint, NULL, NULL, &resolved,
                             NULL) != RoboOk ||
        !resolved) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboNotFound,
                             "the point is over nothing this shell can name");
    }
    return RoboOk;
}

static Result PathProvider(void *userdata,
                           const InputTarget *target,
                           InputEventPath *out,
                           InputDiag *diag)
{
    DesktopInput *input = (DesktopInput *)userdata;
    InputTarget chain[RoboInputMaxEventPath];
    uint32_t depth = 0u;

    if (!input || !target || !out) {
        return RoboInputDiag(diag, RoboInputDomainDispatch, RoboInvalidArgument,
                             "a system, a target and a path are required");
    }
    InputEventPathInit(out);

    /*
     * Built root first: the shell's own window when it has one and it is not the target, then the window a
     * document target sits in, then the target itself. The last entry **is** the target, which is what
     * `InputEventPathBuild` checks — an event that never reached what it was addressed to is a routing bug
     * this door catches for free.
     */
    if (input->config.window != RoboWindowIdNone &&
        !(target->kind == RoboInputTargetWindow && target->window == input->config.window)) {
        InputTarget shell;

        if (InputTargetSetWindow(&shell, input->config.window, target->space, NULL) == RoboOk) {
            chain[depth++] = shell;
        }
    }
    if (target->kind == RoboInputTargetDocument && target->window != RoboWindowIdNone) {
        InputTarget window;

        if (InputTargetSetWindow(&window, target->window, target->space, NULL) == RoboOk) {
            chain[depth++] = window;
        }
    }
    chain[depth++] = *target;

    {
        uint32_t index;

        for (index = 0u; index < depth && index < RoboInputMaxEventPath; index++) {
            out->targets[index] = chain[index];
        }
        out->count = index;
        out->truncated = depth > index ? depth - index : 0u;
        out->depth = out->count > 0u ? out->count - 1u : 0u;
    }
    return RoboOk;
}

/** Non-zero when @p a (a stored listener's target) is about @p b (the queried one). */
static int TargetMatches(const InputTarget *a, const InputTarget *b)
{
    if (a->kind == RoboInputTargetWindow && a->window == RoboWindowIdNone) {
        /* The sentinel: the shell's own listener, registered once and matching every window. */
        return (b->kind == RoboInputTargetWindow || b->kind == RoboInputTargetDocument) ? 1 : 0;
    }
    return InputTargetEqual(a, b);
}

static uint32_t ListenerProvider(void *userdata,
                                 const InputTarget *target,
                                 const InputEvent *event,
                                 InputListener *out,
                                 uint32_t capacity)
{
    DesktopInput *input = (DesktopInput *)userdata;
    uint32_t index;
    uint32_t found = 0u;

    (void)event;
    if (!input || !target || !out) {
        return 0u;
    }
    for (index = 0u; index < input->listenerCount; index++) {
        const DesktopInputListenerEntry *entry = &input->listeners[index];
        InputListener copy;

        if (entry->listener.removed || !entry->listener.callback) {
            continue;
        }
        if (!TargetMatches(&entry->listener.target, target)) {
            continue;
        }
        copy = entry->listener;
        copy.target = *target;
        copy.removed = 0;
        if (found < capacity) {
            out[found] = copy;
        }
        found++;
    }
    return found;
}

void DesktopInputContentInit(DesktopInputContent *content)
{
    if (!content) {
        return;
    }
    MemSet(content, 0, sizeof(DesktopInputContent));
    content->window = RoboDesktopWindowIdNone;
    HtmlNodeHandleClear(&content->documentNode);
    InputCommandTargetInit(&content->commands);
}

/* ------------------------------------------------------------------ */
/* The shell's own listeners (13.66 / 13.67 / 13.74)                    */
/* ------------------------------------------------------------------ */

/**
 * A press: raise or close, and begin a window drag on a title bar.
 *
 * The shell's whole input behaviour before this phase, now one listener. Which *part* of the window was
 * pressed is asked of the shell's own geometry — a control re-deriving its own sub-region — rather than
 * carried on the target, because the target model names a window and the chrome is the window's business.
 */
static Result ShellOnPointerDown(void *userdata, const InputEvent *event, InputEventContext *context)
{
    DesktopInput *input = (DesktopInput *)userdata;
    DesktopHit hit;
    GfxPoint screen;
    InputDiag idiag;

    (void)context;
    if (!input || !event) {
        return RoboInvalidArgument;
    }
    InputDiagReset(&idiag);

    screen = event->payload.pointer.position;
    if (InputSpaceIsKnown(event->space) && event->space != RoboInputSpaceScreen) {
        if (InputConvertPoint(screen, event->space, RoboInputSpaceScreen, NULL, &screen, &idiag) !=
            RoboOk) {
            /* A press on a plane the shell cannot place is a press it cannot act on. */
            return RoboOk;
        }
    }
    if (DesktopHitTest(input->desktop, screen, &hit) != RoboOk) {
        return RoboOk;
    }

    if (hit.area == RoboDesktopAreaWindowClose) {
        if (DesktopWindowRequestClose(input->desktop, hit.window) == RoboOk) {
            NoteSet(input, "close");
        }
        return RoboOk;
    }
    if (hit.area == RoboDesktopAreaWindowTitle || hit.area == RoboDesktopAreaWindowContent) {
        if (DesktopWindowRaise(input->desktop, hit.window) == RoboOk) {
            NoteSet(input, "raise");
        }
        if (hit.area == RoboDesktopAreaWindowTitle) {
            const char *title = DesktopWindowTitle(input->desktop, hit.window);
            InputDragPayload payload;
            GfxRect bounds;

            InputDragPayloadInit(&payload);
            (void)InputDragPayloadSetText(&payload, title, DesktopStringLength(title), NULL, &idiag);
            payload.identity = (uint64_t)hit.window;
            if (InputDragSessionBegin(&input->session.drag, &event->target, RoboInputDragOperationMove,
                                      &payload, &idiag) == RoboOk) {
                input->dragWindow = hit.window;
                input->dragGrabX = screen.x;
                input->dragGrabY = screen.y;
                if (DesktopWindowBounds(input->desktop, hit.window, &bounds) == RoboOk) {
                    input->dragGrabX = screen.x - bounds.x;
                    input->dragGrabY = screen.y - bounds.y;
                }
            }
        }
    }
    return RoboOk;
}

/** A release: end the drag, and nothing else. */
static Result ShellOnPointerUp(void *userdata, const InputEvent *event, InputEventContext *context)
{
    DesktopInput *input = (DesktopInput *)userdata;
    InputDragEvent dragEvent;
    InputDiag idiag;

    (void)context;
    if (!input || !event) {
        return RoboInvalidArgument;
    }
    InputDiagReset(&idiag);
    InputDragEventInit(&dragEvent);
    if (!InputDragSessionIsActive(&input->session.drag)) {
        return RoboOk;
    }
    if (InputDragSessionDrop(&input->session.drag, event->payload.pointer.position, &dragEvent,
                             &idiag) == RoboOk) {
        NoteSet(input, "drop");
    } else {
        (void)InputDragSessionCancel(&input->session.drag, &dragEvent, &idiag);
        NoteSet(input, "drag-cancel");
    }
    (void)InputDragSessionReset(&input->session.drag);
    input->dragWindow = RoboDesktopWindowIdNone;
    return RoboOk;
}

/** A move while a window drag is running moves the window under the grab. */
static void ShellFollowDrag(DesktopInput *input, const InputEvent *event)
{
    GfxPoint screen;
    GfxRect bounds;
    InputDiag idiag;
    InputTarget over;
    InputDragEvent dragEvent;

    if (WindowIsNone(input->dragWindow) || !InputDragSessionIsActive(&input->session.drag)) {
        return;
    }
    InputDiagReset(&idiag);
    InputDragEventInit(&dragEvent);
    screen = event->payload.pointer.position;
    if (InputSpaceIsKnown(event->space) && event->space != RoboInputSpaceScreen) {
        if (InputConvertPoint(screen, event->space, RoboInputSpaceScreen, NULL, &screen, &idiag) !=
            RoboOk) {
            return;
        }
    }
    if (DesktopWindowBounds(input->desktop, input->dragWindow, &bounds) != RoboOk) {
        input->dragWindow = RoboDesktopWindowIdNone;
        (void)InputDragSessionCancel(&input->session.drag, &dragEvent, &idiag);
        return;
    }
    bounds.x = screen.x - input->dragGrabX;
    bounds.y = screen.y - input->dragGrabY;
    InputTargetInit(&over);
    if (InputTargetSetWindow(&over, AsWindowId(input->dragWindow), RoboInputSpaceScreen, &idiag) ==
        RoboOk) {
        (void)InputDragSessionMove(&input->session.drag, &over, RoboInputDragOperationMove, screen,
                                   RoboInputSpaceScreen, &dragEvent, &idiag);
    }
    (void)DesktopWindowSetBounds(input->desktop, input->dragWindow, bounds, NULL);
}

/* ------------------------------------------------------------------ */
/* The command targets the shell installs (13.73 / 13.74)               */
/* ------------------------------------------------------------------ */

/** The one text source the clipboard reads: the focused window's editor's selection. */
static Result ClipboardReadSelection(void *userdata, uint32_t start, uint32_t end, char *out,
                                     size_t capacity, size_t *outLength)
{
    RoboTextEditor *editor = (RoboTextEditor *)userdata;
    RoboTextBuffer *buffer = NULL;
    RoboTextDiag textDiag;
    char *text = NULL;
    size_t length;
    size_t index;

    (void)start;
    (void)end;
    if (!editor || !out || !outLength) {
        return RoboInvalidArgument;
    }
    RoboTextDiagReset(&textDiag);
    if (RoboTextEditorSelectedText(editor, &buffer, &textDiag) != RoboOk || !buffer) {
        RoboTextBufferDestroy(buffer);
        *outLength = 0u;
        return RoboNotFound;
    }
    if (RoboTextBufferToCString(buffer, &text, &textDiag) != RoboOk || !text) {
        RoboTextBufferDestroy(buffer);
        *outLength = 0u;
        return RoboNotFound;
    }
    length = DesktopStringLength(text);
    if (length >= capacity) {
        RoboTextBufferDestroy(buffer);
        *outLength = length;
        return RoboNotSupported;
    }
    for (index = 0u; index < length; index++) {
        out[index] = text[index];
    }
    out[length] = '\0';
    *outLength = length;
    RoboTextBufferDestroy(buffer);
    return RoboOk;
}

static Result ClipboardInsertText(void *userdata, const char *text, size_t length)
{
    DesktopInput *input = (DesktopInput *)userdata;
    InputCommand command;
    InputDiag idiag;

    (void)length;
    if (!input || !text) {
        return RoboInvalidArgument;
    }
    InputDiagReset(&idiag);
    InputCommandInit(&command);
    if (InputCommandSet(&command, RoboInputCommandInsertText, &idiag) != RoboOk) {
        return RoboInvalidArgument;
    }
    if (InputCommandSetText(&command, text, &idiag) != RoboOk) {
        return RoboInvalidArgument;
    }
    return DesktopInputRouteCommand(input, &command, NULL, NULL);
}

/**
 * Copy, cut and paste over the focused window's editor and this system's clipboard.
 *
 * The clipboard is the model's — one by-value buffer, deliberately not an allocator — and the text it holds
 * is the logical source text of a selection, never a glyph. A copy of an empty selection leaves the
 * clipboard alone rather than destroying it, which is why that case is answered rather than refused.
 */
static Result ShellClipboardExecute(void *userdata, const InputCommand *command, InputDiag *diag)
{
    DesktopInput *input = (DesktopInput *)userdata;
    InputClipboardTextSource source;
    InputClipboardResult result;
    size_t length = 0u;

    if (!input || !command) {
        return RoboInvalidArgument;
    }
    if (command->id != RoboInputCommandCopy && command->id != RoboInputCommandCut &&
        command->id != RoboInputCommandPaste) {
        return RoboNotSupported;
    }
    if (!input->clipboardEditor) {
        /* No focused editor: a copy of nothing is nothing, and `NotSupported` lets the walk continue. */
        return RoboNotSupported;
    }

    if (command->id == RoboInputCommandPaste) {
        if (!input->clipboardLive) {
            return RoboNotSupported;
        }
        result = InputClipboardPaste(&input->clipboard, ClipboardInsertText, input);
        return result == RoboInputClipboardCopied || result == RoboInputClipboardNothing
                   ? RoboOk
                   : RoboNotSupported;
    }

    source.userdata = input->clipboardEditor->editor;
    source.readRange = ClipboardReadSelection;
    result = InputClipboardCopy(&source, 0u, 0xFFFFFFFFu, RoboInputApplicationIdNone,
                                &input->clipboard, &length, diag);
    if (result == RoboInputClipboardNothing) {
        return RoboOk; /* An empty selection: the clipboard keeps what it had. */
    }
    if (result != RoboInputClipboardCopied) {
        return RoboNotSupported;
    }
    input->clipboardLive = 1;
    if (command->id == RoboInputCommandCut) {
        InputCommand cut;

        InputCommandInit(&cut);
        if (InputCommandSet(&cut, RoboInputCommandCut, diag) != RoboOk) {
            return RoboInvalidArgument;
        }
        return InputCommandApplyToEditor(&cut, input->clipboardEditor->editor, NULL, NULL, diag);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Focus delivery (13.69)                                              */
/* ------------------------------------------------------------------ */

/** Dispatch a synthesized event at @p target: `FOCUS_IN` / `FOCUS_OUT` / `POINTER_ENTER` / `…LEAVE`. */
static Result DispatchAtTarget(DesktopInput *input,
                               InputEventType type,
                               const InputTarget *target,
                               DesktopInputStep *step,
                               DesktopDiag *diag)
{
    InputEvent event;
    InputEventPath path;
    InputEventContext context;
    InputDiag idiag;
    Result status;

    if (!InputTargetIsValid(target)) {
        return RoboOk;
    }
    InputDiagReset(&idiag);
    InputEventInit(&event);
    status = InputEventSetFocusOrWindow(&event, type, RoboInputSourceKeyboard, &idiag);
    if (status != RoboOk) {
        return RoboOk;
    }
    status = InputEventSetTarget(&event, target, &idiag);
    if (status != RoboOk) {
        return RoboOk;
    }
    status = InputEventPathBuild(&input->path, target, &path, &idiag);
    if (status != RoboOk) {
        return RoboOk;
    }
    InputEventContextInit(&context);
    (void)InputDispatch(&input->dispatcher, &event, &path, &context, &idiag);
    if (step) {
        step->listenerCalls += context.calls;
    }
    (void)diag;
    return RoboOk;
}

/** Publish a transition: the old target hears `FOCUS_OUT`, then the new one hears `FOCUS_IN`. */
static Result PublishTransition(DesktopInput *input,
                                const InputFocusTransition *transition,
                                DesktopInputStep *step,
                                DesktopDiag *diag)
{
    if (!transition || InputFocusTransitionEventCount(transition) == 0u) {
        return RoboOk;
    }
    if (transition->hasPrevious) {
        (void)DispatchAtTarget(input, RoboInputEventFocusOut, &transition->previous, step, diag);
    }
    if (transition->hasFocused) {
        (void)DispatchAtTarget(input, RoboInputEventFocusIn, &transition->focused, step, diag);
    }
    return RoboOk;
}

/** Make @p window the active focus scope, from one door with the shell's own notion of front. */
static Result ActivateWindow(DesktopInput *input,
                             DesktopWindowId window,
                             DesktopInputStep *step,
                             DesktopDiag *diag)
{
    InputFocusTransition transition;
    InputDiag idiag;
    Result status;

    if (WindowIsNone(window)) {
        return RoboOk;
    }
    InputDiagReset(&idiag);
    InputFocusTransitionInit(&transition);
    status = InputFocusManagerSetActiveWindow(&input->session.focus, AsWindowId(window),
                                              RoboInputApplicationIdNone, &transition, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the window could not be made the active scope");
    }
    (void)PublishTransition(input, &transition, step, diag);
    return RoboOk;
}

Result DesktopInputFocusTarget(DesktopInput *input,
                               const InputTarget *target,
                               InputFocusReason reason,
                               InputFocusTransition *out,
                               DesktopDiag *diag)
{
    InputFocusTransition transition;
    InputDiag idiag;
    Result status;

    if (!input || !target) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a target are required");
    }
    InputDiagReset(&idiag);
    InputFocusTransitionInit(&transition);
    if (input->session.focus.activeWindow != target->window && target->window != RoboWindowIdNone) {
        status = ActivateWindow(input, AsDesktopWindow(target->window), NULL, diag);
        if (status != RoboOk) {
            return status;
        }
    }
    status = InputFocusRequest(&input->session.focus, target, reason, &transition, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the target could not take focus");
    }
    if (out) {
        *out = transition;
    }
    (void)PublishTransition(input, &transition, NULL, diag);
    return RoboOk;
}

Result DesktopInputFocusMove(DesktopInput *input,
                             InputFocusDirection direction,
                             InputFocusReason reason,
                             InputFocusTransition *out,
                             DesktopDiag *diag)
{
    InputFocusChain chain;
    InputFocusTransition transition;
    InputDiag idiag;
    size_t index;
    Result status;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    if (!InputFocusDirectionIsSupported(direction)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "the direction is not one this build walks");
    }
    InputDiagReset(&idiag);
    InputFocusChainInit(&chain);
    InputFocusTransitionInit(&transition);

    /* The chain is the shell's windows, back to front: the order `Tab` visits them. */
    for (index = 0u; index < input->desktop->windowCount; index++) {
        const DesktopWindow *window = &input->desktop->windows[index];
        InputTarget target;

        if (!window->visible || window->state != RoboDesktopWindowOpen) {
            continue;
        }
        InputTargetInit(&target);
        if (InputTargetSetWindow(&target, AsWindowId(window->id), RoboInputSpaceScreen, &idiag) !=
            RoboOk) {
            continue;
        }
        (void)InputFocusChainAdd(&chain, &target, &idiag);
    }
    if (chain.count == 0u) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotFound,
                               "no window of this shell can take focus");
    }
    status = InputFocusChainFocus(&input->session.focus, &chain, direction, reason, &transition,
                                  &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "focus could not be moved");
    }
    if (transition.hasFocused && transition.focused.window != RoboWindowIdNone) {
        (void)DesktopWindowRaise(input->desktop, AsDesktopWindow(transition.focused.window));
    }
    if (out) {
        *out = transition;
    }
    (void)PublishTransition(input, &transition, NULL, diag);
    return RoboOk;
}

int DesktopInputFocusedTarget(const DesktopInput *input, InputTarget *out)
{
    if (!input || !input->session.focus.hasFocus) {
        return 0;
    }
    if (out) {
        *out = input->session.focus.focused;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Command routing (13.36 / 13.72 / 13.73)                             */
/* ------------------------------------------------------------------ */

Result DesktopInputRouteCommand(DesktopInput *input,
                                const InputCommand *command,
                                int32_t *outLevel,
                                DesktopDiag *diag)
{
    InputDiag idiag;
    Result status;

    if (!input || !command) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a command are required");
    }
    InputDiagReset(&idiag);
    status = InputCommandRouterRoute(&input->router, command, outLevel, &idiag);
    if (status == RoboOk) {
        input->stats.commands++;
    } else if (status == RoboNotFound) {
        input->stats.unclaimed++;
    } else {
        return ShellDiag(diag, &idiag, status, "the command's target failed");
    }
    return status;
}

/**
 * A key event: the chord table first, then the text.
 *
 * The order is the contract 13.35/13.36 states. A chord that resolves is **consumed** — a letter a shortcut
 * claimed does not also type itself — and a key that produced text and matched nothing becomes an
 * `InsertText` the focused editor executes.
 */
static void HandleKeyEvent(DesktopInput *input,
                           const InputEvent *event,
                           DesktopInputStep *step,
                           DesktopDiag *diag)
{
    InputShortcut shortcut;
    InputCommand command;
    InputDiag idiag;
    InputEvent textEvents[RoboInputTextMaxEvents];
    InputTextOrigin origin = RoboInputTextOriginNone;
    uint32_t textCount = 0u;
    uint32_t index;

    if (event->type != RoboInputEventKeyDown && event->type != RoboInputEventKeyRepeat) {
        return;
    }
    InputDiagReset(&idiag);
    InputShortcutInit(&shortcut);
    InputCommandInit(&command);

    if (event->type == RoboInputEventKeyDown) {
        if (InputShortcutManagerResolveEvent(&input->shortcuts, event, &shortcut, &idiag) == RoboOk) {
            step->shortcuts++;
            input->stats.shortcuts++;
            if (InputCommandSet(&command, shortcut.command, &idiag) == RoboOk &&
                DesktopInputRouteCommand(input, &command, NULL, diag) == RoboOk) {
                step->commands++;
                step->lastCommand = shortcut.command;
                return;
            }
            step->unclaimed++;
            return;
        }
    }

    if (!input->layoutLive) {
        return;
    }
    if (InputTextPipelineTranslate(&input->session.text, event, textEvents, RoboInputTextMaxEvents,
                                   &textCount, &origin, &idiag) != RoboOk) {
        return;
    }
    for (index = 0u; index < textCount; index++) {
        InputCommandInit(&command);
        if (InputCommandSet(&command, RoboInputCommandInsertText, &idiag) != RoboOk) {
            continue;
        }
        if (InputCommandSetText(&command, textEvents[index].payload.text.text, &idiag) != RoboOk) {
            continue;
        }
        if (DesktopInputRouteCommand(input, &command, NULL, diag) == RoboOk) {
            step->textInputs++;
            step->commands++;
            step->lastCommand = RoboInputCommandInsertText;
            input->stats.textInputs++;
        } else {
            step->unclaimed++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Default actions (13.34)                                             */
/* ------------------------------------------------------------------ */

static Result PerformFocusAction(DesktopInput *input,
                                 const InputDefaultAction *action,
                                 DesktopInputStep *step,
                                 DesktopDiag *diag)
{
    InputFocusTransition transition;
    InputDiag idiag;
    Result status;

    InputDiagReset(&idiag);
    InputFocusTransitionInit(&transition);
    if (action->target.window != RoboWindowIdNone &&
        AsDesktopWindow(action->target.window) != input->session.focus.activeWindow) {
        status = ActivateWindow(input, AsDesktopWindow(action->target.window), step, diag);
        if (status != RoboOk) {
            return status;
        }
    }
    status = InputFocusRequest(&input->session.focus, &action->target,
                               RoboInputFocusReasonPointer, &transition, &idiag);
    if (status != RoboOk) {
        /* A press that cannot focus is a legitimate press, not a failure. */
        return RoboOk;
    }
    if (InputFocusTransitionEventCount(&transition) > 0u) {
        step->focusChanges++;
        input->stats.focusChanges++;
    }
    return PublishTransition(input, &transition, step, diag);
}

static Result PerformAction(DesktopInput *input,
                            const InputDefaultAction *action,
                            DesktopInputStep *step,
                            DesktopDiag *diag)
{
    step->actions++;
    input->stats.actions++;

    switch (action->kind) {
    case RoboInputDefaultActionFocus:
        return PerformFocusAction(input, action, step, diag);
    case RoboInputDefaultActionNavigate:
        if (DesktopInputFocusMove(input, action->direction, RoboInputFocusReasonNavigation, NULL,
                                  diag) == RoboOk) {
            step->focusChanges++;
            input->stats.focusChanges++;
        }
        return RoboOk;
    case RoboInputDefaultActionActivate:
        /*
         * `Enter` and `Space`: the focused target is asked to activate. This is the split 13.36 draws — the
         * chord table turns `Ctrl+C` into `Copy`, and *this* turns `Enter` into `Activate`; neither invents
         * the other. `HandleKeyEvent` runs after this and is where a letter's text actually lands, which is
         * why `RoboInputDefaultActionCommand` needs no work here.
         */
        {
            InputCommand command;
            InputDiag idiag;

            InputDiagReset(&idiag);
            InputCommandInit(&command);
            if (InputCommandSet(&command, RoboInputCommandActivate, &idiag) == RoboOk &&
                DesktopInputRouteCommand(input, &command, NULL, diag) == RoboOk) {
                step->commands++;
                step->lastCommand = RoboInputCommandActivate;
            } else {
                step->unclaimed++;
            }
        }
        return RoboOk;
    case RoboInputDefaultActionCommand:
        /* The command layer is `HandleKeyEvent`'s, which runs immediately after this returns. Identifying
         * the action is this door's whole job (13.34). */
        return RoboOk;
    case RoboInputDefaultActionScroll:
        /*
         * The shell has no scroller yet (its panel is fixed), so the action is *identified* and performed by
         * nobody — which is exactly the split 13.34 asks for, and the reason this is a no-op rather than an
         * invented scroll.
         */
        return RoboOk;
    default:
        return RoboOk;
    }
}

/* ------------------------------------------------------------------ */
/* One event: target, path, dispatch, action                           */
/* ------------------------------------------------------------------ */

static Result DrainOne(DesktopInput *input,
                       const InputEvent *source,
                       DesktopInputStep *step,
                       DesktopDiag *diag)
{
    InputEvent event = *source;
    InputDefaultAction action;
    InputEventContext context;
    InputEventPath path;
    InputTarget target;
    InputRoute route;
    InputDiag idiag;
    DesktopWindowId window;
    GfxPoint docPoint;
    Result status = RoboOk;
    int resolved = 0;
    int positioned;

    InputDiagReset(&idiag);
    InputEventContextInit(&context);
    InputEventPathInit(&path);
    InputTargetInit(&target);
    InputRouteInit(&route);
    positioned = InputEventTypeHasPosition(event.type) ? 1 : 0;

    window = AsDesktopWindow(event.window);
    if (!WindowIsNone(window)) {
        (void)DesktopInputRouteForWindow(input, window, &route, NULL);
    }

    if (positioned) {
        if (ResolvePointInternal(input, event.payload.pointer.position, event.space, &target,
                                 &docPoint, NULL, &window, &resolved, NULL) != RoboOk ||
            !resolved) {
            step->dropped++;
            return RoboOk;
        }
        event.window = AsWindowId(window);

        /*
         * Software cursor + pointer readback (Phase 13.66): DesktopInput owns the pointer path, so it
         * must update the shell's tracked point. Without this, `cursorVisible` stays 0 and the
         * overlay never draws — `DesktopObservePointer` is only for callers without an input system.
         */
        {
            GfxPoint screen = event.payload.pointer.position;

            if (InputSpaceIsKnown(event.space) && event.space != RoboInputSpaceScreen) {
                if (InputConvertPoint(screen, event.space, RoboInputSpaceScreen, NULL, &screen,
                                      &idiag) != RoboOk) {
                    screen = event.payload.pointer.position;
                }
            }
            {
                GfxPoint before = input->desktop->pointer.point;
                int wasVisible = input->desktop->cursorVisible;

                if (DesktopTrackPointer(input->desktop, screen, diag) == RoboOk) {
                    input->desktop->pointer.pressed =
                        (event.type == RoboInputEventPointerDown) ? 1
                        : (event.type == RoboInputEventPointerUp ||
                           event.type == RoboInputEventPointerCancel)
                              ? 0
                              : input->desktop->pointer.pressed;
                    if (event.type == RoboInputEventPointerMove ||
                        before.x != screen.x || before.y != screen.y ||
                        (input->desktop->showCursor && !wasVisible)) {
                        step->pointerMoves++;
                        input->desktop->stats.pointerMoves++;
                    }
                }
            }
        }

        /* 13.18: hover, through the model's own transition record — leave then enter, at most one of each. */
        {
            InputTarget leave;
            InputTarget enter;
            int emitted;

            InputTargetInit(&leave);
            InputTargetInit(&enter);
            emitted = InputHoverSet(&input->session.hover, event.payload.pointer.pointer, &target,
                                    &leave, &enter);
            if (emitted >= 1) {
                step->hoverChanges++;
                (void)DispatchAtTarget(input, RoboInputEventPointerLeave, &leave, step, diag);
            }
            if (emitted >= 2) {
                (void)DispatchAtTarget(input, RoboInputEventPointerEnter, &enter, step, diag);
            }
        }

        /* 13.43/13.75: the shape the hit test asks for. */
        {
            int changed = 0;

            (void)InputCursorManagerUpdate(&input->cursors, &target, &changed);
            if (changed) {
                step->cursorChanges++;
                input->stats.cursorChanges++;
            }
        }

        /* 13.17: a captured pointer's moves and its release go to its holder, whatever is under it. */
        {
            const InputTarget *holder =
                InputPointerCaptureTarget(&input->session.captures, event.payload.pointer.pointer);

            if (holder &&
                (event.type == RoboInputEventPointerMove || event.type == RoboInputEventPointerUp)) {
                target = *holder;
            }
        }
        if (event.type == RoboInputEventPointerDown) {
            (void)InputPointerCaptureSet(&input->session.captures, event.payload.pointer.pointer,
                                         &target, &idiag);
        } else if (event.type == RoboInputEventPointerUp ||
                   event.type == RoboInputEventPointerCancel) {
            (void)InputPointerCaptureRelease(&input->session.captures,
                                             event.payload.pointer.pointer, &target);
        }
        /* 13.42/13.74: the drag follows the pointer and the window follows the drag. */
        if (event.type == RoboInputEventPointerMove) {
            ShellFollowDrag(input, &event);
        }
    } else {
        /* A key event is about whatever has focus — that is what focus means. */
        if (input->session.focus.hasFocus) {
            target = input->session.focus.focused;
            event.window = target.window;
        } else {
            window = FrontWindow(input);
            if (WindowIsNone(window)) {
                step->dropped++;
                return RoboOk;
            }
            if (InputTargetSetWindow(&target, AsWindowId(window),
                                     InputSpaceIsKnown(event.space) ? event.space
                                                                    : RoboInputSpaceScreen,
                                     &idiag) != RoboOk) {
                step->dropped++;
                return RoboOk;
            }
            event.window = AsWindowId(window);
        }
    }

    if (InputEventSetTarget(&event, &target, &idiag) != RoboOk) {
        step->dropped++;
        return RoboOk;
    }
    if (InputEventPathBuild(&input->path, &target, &path, &idiag) != RoboOk) {
        step->dropped++;
        return RoboOk;
    }

    status = InputDispatch(&input->dispatcher, &event, &path, &context, &idiag);
    step->dispatched++;
    step->listenerCalls += context.calls;
    input->stats.dispatched++;
    input->stats.listenerCalls += context.calls;
    if (context.defaultPrevented) {
        step->prevented++;
    }

    /*
     * A document click for the page script (Phase 16): a `POINTER_UP` on a DOCUMENT target with a
     * layout node is the moment a browser would fire `click`. The scene reads these fields and posts
     * a `JsEventRecord` — this file never includes RoboJS.
     */
    if (event.type == RoboInputEventPointerUp && target.kind == RoboInputTargetDocument &&
        target.layoutNode != RoboLayoutNodeIdNone) {
        step->documentClick = 1;
        step->documentClickNode = target.layoutNode;
        if (positioned) {
            step->documentClickX = (double)docPoint.x;
            step->documentClickY = (double)docPoint.y;
        } else {
            step->documentClickX = (double)event.payload.pointer.position.x;
            step->documentClickY = (double)event.payload.pointer.position.y;
        }
    }

    action = InputDefaultActionFor(&event, NULL, input->config.lineHeightPx);
    if (!context.defaultPrevented && action.kind != RoboInputDefaultActionNone) {
        (void)PerformAction(input, &action, step, diag);
    }

    /* The text a key produced, and the chords, after the default action had its say. */
    HandleKeyEvent(input, &event, step, diag);

    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "a listener failed and the dispatch stopped");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The pump (13.76)                                                    */
/* ------------------------------------------------------------------ */

static Result Pump(DesktopInput *input, uint32_t waitMs, DesktopInputStep *step, DesktopDiag *diag)
{
    WindowEvent event;

    (void)diag;
    if (!WindowIsInitialized()) {
        return RoboOk;
    }
    /* Poll first so a pending IRQ is not delayed by the idle sleep, which is the order the scene used. */
    (void)WindowEventLoopRunOnce(0u);
    for (;;) {
        InputEvent translated;
        InputDiag idiag;
        InputCoalesceAction action;
        Result status;

        if (WindowEventPoll(&event) != RoboOk) {
            break;
        }
        step->windowEvents++;
        input->stats.windowEvents++;

        if (event.type == RoboWindowEventCloseRequested || event.type == RoboWindowEventClosed) {
            step->closeRequested = 1;
            step->skipped++;
            continue;
        }
        if (event.type == RoboWindowEventResized) {
            step->resized = 1;
        }
        if (event.type == RoboWindowEventScaleChanged) {
            step->scaleChanged = 1;
        }
        if (!InputBridgeCanTranslate(event.type)) {
            step->skipped++;
            continue;
        }

        InputDiagReset(&idiag);
        status = InputEventFromWindowEventWithOptions(&input->bridge, &event, &translated, &idiag);
        if (status != RoboOk) {
            step->skipped++;
            continue;
        }
        if (translated.window == RoboWindowIdNone && input->config.window != RoboWindowIdNone) {
            translated.window = input->config.window;
        }
        step->translated++;
        input->stats.translated++;

        action = RoboInputCoalesceEmit;
        if (input->config.enableCoalescing) {
            if (InputCoalescerPush(&input->coalescer, &translated, &action, &idiag) != RoboOk) {
                action = RoboInputCoalesceEmit;
            }
            if (action == RoboInputCoalesceFlushFirst) {
                uint32_t emitted = 0u;

                if (InputCoalescerFlush(&input->coalescer, input->queue, &emitted, &idiag) == RoboOk) {
                    step->coalesced += emitted;
                }
                if (InputCoalescerPush(&input->coalescer, &translated, &action, &idiag) != RoboOk) {
                    action = RoboInputCoalesceEmit;
                }
            }
        }
        if (action != RoboInputCoalesceHold && action != RoboInputCoalesceFlushFirst) {
            if (InputQueuePush(input->queue, &translated, &idiag) != RoboOk) {
                step->dropped++;
                input->stats.dropped++;
            }
        }
    }

    if (waitMs != 0u) {
        (void)WindowEventLoopRunOnce(waitMs);
        (void)Pump(input, 0u, step, diag);
    }
    return RoboOk;
}

Result DesktopInputPump(DesktopInput *input,
                        uint32_t waitMs,
                        DesktopInputStep *outStep,
                        DesktopDiag *diag)
{
    DesktopInputStep step;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    DesktopInputStepInit(&step);
    (void)Pump(input, waitMs, &step, diag);
    if (outStep) {
        *outStep = step;
    }
    return RoboOk;
}

Result DesktopInputDrain(DesktopInput *input, DesktopInputStep *outStep, DesktopDiag *diag)
{
    DesktopInputStep step;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    DesktopInputStepInit(&step);
    if (input->config.enableCoalescing) {
        InputDiag idiag;
        uint32_t emitted = 0u;

        InputDiagReset(&idiag);
        if (InputCoalescerFlush(&input->coalescer, input->queue, &emitted, &idiag) == RoboOk) {
            step.coalesced += emitted;
        }
    }
    for (;;) {
        InputEvent event;
        InputDiag idiag;

        InputDiagReset(&idiag);
        if (InputQueuePop(input->queue, &event, &idiag) != RoboOk) {
            break;
        }
        (void)DrainOne(input, &event, &step, diag);
    }
    if (outStep) {
        *outStep = step;
    }
    return RoboOk;
}

Result GetDesktopInputStep(DesktopInput *input,
                           uint32_t waitMs,
                           DesktopInputStep *outStep,
                           DesktopDiag *diag)
{
    DesktopInputStep pumped;
    DesktopInputStep drained;
    DesktopInputStep step;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    DesktopInputStepInit(&pumped);
    DesktopInputStepInit(&drained);
    DesktopInputStepInit(&step);

    (void)DesktopInputPump(input, waitMs, &pumped, diag);
    (void)DesktopInputDrain(input, &drained, diag);

    step.windowEvents = pumped.windowEvents;
    step.translated = pumped.translated;
    step.skipped = pumped.skipped;
    step.closeRequested = pumped.closeRequested;
    step.resized = pumped.resized;
    step.scaleChanged = pumped.scaleChanged;
    step.coalesced = pumped.coalesced + drained.coalesced;
    step.dropped = pumped.dropped + drained.dropped;
    step.dispatched = drained.dispatched;
    step.listenerCalls = drained.listenerCalls;
    step.prevented = drained.prevented;
    step.actions = drained.actions;
    step.focusChanges = drained.focusChanges;
    step.hoverChanges = drained.hoverChanges;
    step.cursorChanges = drained.cursorChanges;
    step.shortcuts = drained.shortcuts;
    step.commands = drained.commands;
    step.unclaimed = drained.unclaimed;
    step.textInputs = drained.textInputs;
    step.clipboardOps = drained.clipboardOps;
    step.dragsBegan = input->session.drag.drops + input->session.drag.cancels;
    step.dragsEnded = input->session.drag.drops + input->session.drag.cancels;
    step.lastCommand = drained.lastCommand;

    input->stats.steps++;
    if (outStep) {
        *outStep = step;
    }
    return RoboOk;
}

Result DesktopInputPush(DesktopInput *input, const InputEvent *event, DesktopDiag *diag)
{
    InputDiag idiag;
    Result status;

    if (!input || !event) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and an event are required");
    }
    InputDiagReset(&idiag);
    status = InputQueuePush(input->queue, event, &idiag);
    if (status != RoboOk) {
        input->stats.dropped++;
        return ShellDiag(diag, &idiag, status, "the input queue refused the event");
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static Result InstallShellListener(DesktopInput *input, InputListenerFn callback, InputPhase phase)
{
    DesktopInputListenerEntry *entry;

    if (input->listenerCount >= RoboDesktopInputMaxListeners) {
        return RoboNotSupported;
    }
    entry = &input->listeners[input->listenerCount++];
    InputTargetInit(&entry->listener.target);
    /* The sentinel: kind `WINDOW` with a zero id is a target `InputTargetIsValid` refuses, and that is the
     * point — it never leaves this table. `ListenerProvider` substitutes the target it was asked about, so
     * what the dispatcher receives is always valid. */
    entry->listener.target.kind = RoboInputTargetWindow;
    entry->listener.target.space = RoboInputSpaceScreen;
    entry->listener.phase = phase;
    entry->listener.passive = 0;
    entry->listener.removed = 0;
    entry->listener.callback = callback;
    entry->listener.userdata = input;
    entry->wildcard = 1;
    return RoboOk;
}

static void InstallDefaults(DesktopInput *input)
{
    static const struct {
        InputCommandId command;
        PhysicalKey physical;
        InputModifiers modifiers;
    } defaults[] = {
        { RoboInputCommandCopy, RoboPhysicalKeyC, RoboKeyModCtrl },
        { RoboInputCommandCut, RoboPhysicalKeyX, RoboKeyModCtrl },
        { RoboInputCommandPaste, RoboPhysicalKeyV, RoboKeyModCtrl },
        { RoboInputCommandSelectAll, RoboPhysicalKeyA, RoboKeyModCtrl },
        { RoboInputCommandUndo, RoboPhysicalKeyZ, RoboKeyModCtrl }
    };
    size_t index;

    for (index = 0u; index < sizeof(defaults) / sizeof(defaults[0]); index++) {
        InputShortcut shortcut;
        InputDiag idiag;

        InputDiagReset(&idiag);
        InputShortcutInit(&shortcut);
        if (InputShortcutSet(&shortcut, defaults[index].command, defaults[index].physical,
                             defaults[index].modifiers, RoboInputShortcutScopeWindow, &idiag) !=
            RoboOk) {
            continue;
        }
        (void)InputShortcutManagerAdd(&input->shortcuts, &shortcut, &idiag);
    }
}

static Result ConnectDevice(DesktopInput *input,
                            InputDeviceType type,
                            const char *name,
                            uint32_t capabilities,
                            InputDeviceId *outId,
                            InputDevice *outIdentity,
                            DesktopDiag *diag)
{
    InputDeviceDescriptor descriptor;
    InputDiag idiag;
    InputDeviceId id = RoboInputDeviceIdNone;
    Result status;

    InputDiagReset(&idiag);
    InputDeviceDescriptorInit(&descriptor);
    status = InputDeviceDescriptorSet(&descriptor, RoboInputDeviceIdNone, type,
                                      InputDeviceTypeDefaultSource(type), name, capabilities, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the device descriptor could not be built");
    }
    (void)InputDeviceDescriptorSetConnection(&descriptor, RoboInputDeviceConnectionConnected, &idiag);
    status = InputSessionConnect(&input->session, &descriptor, &id, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the device could not be connected");
    }
    input->stats.connects++;

    /*
     * A second descriptor, carrying the id the session just issued, is what an *event* needs: the bridge
     * stamps it on every translated event, which is what makes a bridged event say which device it came from
     * rather than nothing.
     */
    {
        InputDeviceDescriptor stamped;

        InputDeviceDescriptorInit(&stamped);
        status = InputDeviceDescriptorSet(&stamped, id, type, InputDeviceTypeDefaultSource(type), name,
                                          capabilities, &idiag);
        if (status != RoboOk) {
            return ShellDiag(diag, &idiag, status, "the device identity could not be built");
        }
        (void)InputDeviceDescriptorSetConnection(&stamped, RoboInputDeviceConnectionConnected, &idiag);
        if (outIdentity) {
            *outIdentity = stamped.device;
        }
    }
    if (outId) {
        *outId = id;
    }
    return RoboOk;
}

Result DesktopInputCreate(Desktop *desktop,
                          const DesktopInputConfig *config,
                          DesktopInput **out,
                          DesktopDiag *diag)
{
    DesktopInput *input;
    InputQueueConfig queueConfig;
    InputDiag idiag;
    Result status;
    InputCommandTarget clipboard;

    if (!desktop || !out) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a desktop and a destination are required");
    }
    *out = NULL;
    InputDiagReset(&idiag);

    input = (DesktopInput *)Malloc(sizeof(DesktopInput));
    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboOutOfMemory,
                               "the input system could not be allocated");
    }
    MemSet(input, 0, sizeof(DesktopInput));

    input->desktop = desktop;
    DesktopInputConfigInit(&input->config);
    if (config) {
        input->config = *config;
        if (input->config.queueCapacity == 0u) {
            input->config.queueCapacity = RoboDesktopInputDefaultQueueCapacity;
        }
        if (input->config.lineHeightPx <= 0) {
            input->config.lineHeightPx = 16;
        }
    }
    if (input->config.window == RoboWindowIdNone) {
        input->config.window = DesktopAttachedWindow(desktop);
    }
    input->pointer = RoboInputPointerIdPrimary;

    InputQueueConfigInit(&queueConfig);
    queueConfig.capacity = input->config.queueCapacity;
    status = InputQueueCreate(&queueConfig, &input->queue, &idiag);
    if (status != RoboOk) {
        RoboFree(input);
        return ShellDiag(diag, &idiag, status, "the input queue could not be created");
    }

    InputSessionInit(&input->session);
    InputDispatcherInit(&input->dispatcher, &input->provider);
    InputShortcutManagerInit(&input->shortcuts);
    InputCommandRouterInit(&input->router);
    InputCursorManagerInit(&input->cursors);
    InputCoalescerInit(&input->coalescer);
    InputTextPipelineInit(&input->session.text);
    InputClipboardDataInit(&input->clipboard);
    InputBridgeOptionsInit(&input->bridge);

    input->provider.userdata = input;
    input->provider.listenersFor = ListenerProvider;
    input->dispatcher.listeners = &input->provider;

    input->hitTest.userdata = input;
    input->hitTest.hitTest = HitTestProvider;
    input->path.userdata = input;
    input->path.buildPath = PathProvider;

    status = ConnectDevice(input, RoboInputDeviceTypeKeyboard, "shell keyboard",
                           RoboInputDeviceCapabilityKeyboardKeys, &input->keyboardId,
                           &input->bridge.keyboardDevice, diag);
    if (status != RoboOk) {
        DesktopInputDestroy(input);
        return status;
    }
    status = ConnectDevice(input, RoboInputDeviceTypeMouse, "shell mouse",
                           RoboInputDeviceCapabilityPointer |
                               RoboInputDeviceCapabilityRelativeMotion |
                               RoboInputDeviceCapabilityButtons | RoboInputDeviceCapabilityWheel,
                           &input->mouseId, &input->bridge.mouseDevice, diag);
    if (status != RoboOk) {
        DesktopInputDestroy(input);
        return status;
    }

    if (InputLayoutCreateBuiltin(&input->layout, RoboInputLayoutUsAscii, &idiag) == RoboOk) {
        input->layoutLive = 1;
        (void)InputTextPipelineSetLayout(&input->session.text, &input->layout, &idiag);
    }

    /* The shell's own two listeners: raise or close on a press, end the drag on its release. */
    (void)InstallShellListener(input, ShellOnPointerDown, RoboInputPhaseTarget);
    (void)InstallShellListener(input, ShellOnPointerUp, RoboInputPhaseTarget);
    InstallDefaults(input);

    /*
     * The shell's clipboard target, pushed **first** so the router — which asks the innermost target first —
     * asks it last. A caller's own command target and a focused editor therefore get the first say, which is
     * the ordering rule 13.36 states rather than a special case.
     */
    InputCommandTargetInit(&clipboard);
    clipboard.userdata = input;
    clipboard.execute = ShellClipboardExecute;
    (void)InputCommandRouterPush(&input->router, &clipboard, &idiag);

    input->live = 1;
    *out = input;
    return RoboOk;
}

void DesktopInputDestroy(DesktopInput *input)
{
    uint32_t index;

    if (!input) {
        return;
    }
    for (index = 0u; index < RoboDesktopInputMaxContents; index++) {
        if (input->contents[index].editorLive) {
            InputEditorTargetRelease(&input->contents[index].editor);
            input->contents[index].editorLive = 0;
        }
    }
    InputQueueDestroy(input->queue);
    input->queue = NULL;
    input->live = 0;
    RoboFree(input);
}

int DesktopInputIsLive(const DesktopInput *input)
{
    return (input && input->live) ? 1 : 0;
}

Desktop *DesktopInputDesktop(DesktopInput *input)
{
    return input ? input->desktop : NULL;
}

/* ------------------------------------------------------------------ */
/* Content (13.68 / 13.72 / 13.73)                                     */
/* ------------------------------------------------------------------ */

Result DesktopInputSetContent(DesktopInput *input,
                              const DesktopInputContent *content,
                              DesktopDiag *diag)
{
    DesktopInputContentEntry *entry;
    InputDiag idiag;
    uint32_t index;

    if (!input || !content) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a content record are required");
    }
    if (WindowIsNone(content->window)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                               "a content record must name a window");
    }
    if (!DesktopWindowExists(input->desktop, content->window)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                               "the window is not one this shell holds");
    }
    InputDiagReset(&idiag);

    entry = FindContentMutable(input, content->window);
    if (!entry) {
        for (index = 0u; index < RoboDesktopInputMaxContents; index++) {
            if (!input->contents[index].live) {
                entry = &input->contents[index];
                break;
            }
        }
    }
    if (!entry) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotSupported,
                               "the content table is full");
    }
    if (entry->editorLive) {
        InputEditorTargetRelease(&entry->editor);
        entry->editorLive = 0;
    }
    entry->content = *content;
    entry->live = 1;

    /* The editor goes on the chain **inside** any application target, so a focused text field wins a
     * contested chord. The application target is pushed after the editor, which makes it the innermost. */
    if (content->editor) {
        if (InputEditorTargetInit(&entry->editor, content->editor, &idiag) == RoboOk) {
            InputCommandTarget target;

            if (InputEditorTargetAsCommandTarget(&entry->editor, &target, &idiag) == RoboOk) {
                (void)InputCommandRouterPush(&input->router, &target, &idiag);
                entry->editorLive = 1;
                input->clipboardEditor = &entry->editor;
            }
        }
    }
    if (InputCommandTargetIsValid(&content->commands)) {
        (void)InputCommandRouterPush(&input->router, &content->commands, &idiag);
    }
    return RoboOk;
}

Result DesktopInputClearContent(DesktopInput *input, DesktopWindowId window, DesktopDiag *diag)
{
    DesktopInputContentEntry *entry;
    uint32_t index;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    entry = FindContentMutable(input, window);
    if (!entry) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                               "the window has no registered content");
    }
    if (input->clipboardEditor == &entry->editor) {
        input->clipboardEditor = NULL;
    }
    if (entry->editorLive) {
        InputEditorTargetRelease(&entry->editor);
        entry->editorLive = 0;
    }
    entry->live = 0;
    for (index = 0u; index < RoboDesktopInputMaxContents; index++) {
        if (input->contents[index].editorLive) {
            input->clipboardEditor = &input->contents[index].editor;
            break;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Listeners, shortcuts and devices                                    */
/* ------------------------------------------------------------------ */

Result DesktopInputAddListener(DesktopInput *input,
                               const InputListener *listener,
                               DesktopDiag *diag)
{
    DesktopInputListenerEntry *entry;

    if (!input || !listener) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a listener are required");
    }
    if (!listener->callback) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a listener must have a callback");
    }
    if (!InputTargetIsValid(&listener->target)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a listener must name a valid target");
    }
    if (input->listenerCount >= RoboDesktopInputMaxListeners) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotSupported,
                               "the listener table is full");
    }
    entry = &input->listeners[input->listenerCount++];
    entry->listener = *listener;
    entry->listener.removed = 0;
    entry->wildcard = 0;
    return RoboOk;
}

uint32_t DesktopInputRemoveListeners(DesktopInput *input, const InputTarget *target)
{
    uint32_t index;
    uint32_t removed = 0u;

    if (!input || !target) {
        return 0u;
    }
    for (index = 0u; index < input->listenerCount; index++) {
        if (InputTargetEqual(&input->listeners[index].listener.target, target)) {
            input->listeners[index].listener.removed = 1;
            removed++;
        }
    }
    return removed;
}

uint32_t DesktopInputListenerCount(const DesktopInput *input)
{
    uint32_t index;
    uint32_t live = 0u;

    if (!input) {
        return 0u;
    }
    for (index = 0u; index < input->listenerCount; index++) {
        if (!input->listeners[index].listener.removed) {
            live++;
        }
    }
    return live;
}

Result DesktopInputBindShortcut(DesktopInput *input,
                                InputCommandId command,
                                PhysicalKey physical,
                                InputModifiers modifiers,
                                InputShortcutScope scope,
                                DesktopDiag *diag)
{
    InputShortcut shortcut;
    InputDiag idiag;
    Result status;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    InputDiagReset(&idiag);
    InputShortcutInit(&shortcut);
    status = InputShortcutSet(&shortcut, command, physical, modifiers, scope, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the chord could not be bound");
    }
    status = InputShortcutManagerAdd(&input->shortcuts, &shortcut, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the shortcut table refused the binding");
    }
    return RoboOk;
}

Result DesktopInputResolveShortcut(DesktopInput *input,
                                   PhysicalKey physical,
                                   InputModifiers modifiers,
                                   InputShortcut *out,
                                   DesktopDiag *diag)
{
    InputDiag idiag;
    Result status;

    if (!input || !out) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a destination are required");
    }
    InputDiagReset(&idiag);
    status = InputShortcutManagerResolve(&input->shortcuts, physical, modifiers, out, &idiag);
    if (status == RoboOk) {
        input->stats.shortcuts++;
    }
    return status;
}

Result DesktopInputConnectKeyboard(DesktopInput *input, InputDeviceId *outId, DesktopDiag *diag)
{
    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    return ConnectDevice(input, RoboInputDeviceTypeKeyboard, "keyboard",
                         RoboInputDeviceCapabilityKeyboardKeys, outId, &input->bridge.keyboardDevice,
                         diag);
}

Result DesktopInputConnectMouse(DesktopInput *input, InputDeviceId *outId, DesktopDiag *diag)
{
    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    return ConnectDevice(input, RoboInputDeviceTypeMouse, "mouse",
                         RoboInputDeviceCapabilityPointer | RoboInputDeviceCapabilityRelativeMotion |
                             RoboInputDeviceCapabilityButtons | RoboInputDeviceCapabilityWheel,
                         outId, &input->bridge.mouseDevice, diag);
}

Result DesktopInputDisconnect(DesktopInput *input,
                              InputDeviceId id,
                              InputSessionSweep *out,
                              DesktopDiag *diag)
{
    InputSessionSweep sweep;
    InputDiag idiag;
    Result status;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    InputDiagReset(&idiag);
    InputSessionSweepInit(&sweep);
    status = InputSessionDisconnect(&input->session, id, &sweep, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the device is not one this session owns");
    }
    input->stats.disconnects++;
    if (out) {
        *out = sweep;
    }
    return RoboOk;
}

uint32_t DesktopInputDeviceCount(const DesktopInput *input)
{
    return input ? InputSessionDeviceCount(&input->session) : 0u;
}

Result DesktopInputResetTransient(DesktopInput *input, InputSessionSweep *out, DesktopDiag *diag)
{
    InputSessionSweep sweep;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    InputSessionSweepInit(&sweep);
    (void)InputSessionResetTransient(&input->session, &sweep);
    InputCoalescerClear(&input->coalescer);
    input->dragWindow = RoboDesktopWindowIdNone;
    if (out) {
        *out = sweep;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Cursor, drag and the shell's own commands (13.67 / 13.74 / 13.75)   */
/* ------------------------------------------------------------------ */

InputCursorType DesktopInputCursor(const DesktopInput *input)
{
    if (!input) {
        return RoboInputCursorDefault;
    }
    return InputCursorManagerCurrent(&input->cursors);
}

int DesktopInputIsDragging(const DesktopInput *input)
{
    return (input && InputDragSessionIsActive(&input->session.drag)) ? 1 : 0;
}

InputDragPhase DesktopInputDragPhase(const DesktopInput *input)
{
    return input ? input->session.drag.phase : RoboInputDragIdle;
}

Result DesktopInputPerformCommand(DesktopInput *input, InputCommandId id, DesktopDiag *diag)
{
    InputCommand command;
    InputDiag idiag;
    Result status;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    switch (id) {
    case RoboInputCommandWindowClose: {
        DesktopWindowId window = FrontWindow(input);

        if (WindowIsNone(window)) {
            return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                                   "no window is open");
        }
        return DesktopWindowRequestClose(input->desktop, window);
    }
    case RoboInputCommandWindowMinimize: {
        DesktopWindowId window = FrontWindow(input);

        if (WindowIsNone(window)) {
            return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotFound,
                                   "no window is open");
        }
        return DesktopWindowSetVisible(input->desktop, window, 0, diag);
    }
    case RoboInputCommandFocusNext:
        return DesktopInputFocusMove(input, RoboInputFocusDirectionNext,
                                     RoboInputFocusReasonNavigation, NULL, diag);
    case RoboInputCommandFocusPrevious:
        return DesktopInputFocusMove(input, RoboInputFocusDirectionPrevious,
                                     RoboInputFocusReasonNavigation, NULL, diag);
    case RoboInputCommandResetInputState: {
        InputSessionSweep sweep;

        InputSessionSweepInit(&sweep);
        return DesktopInputResetTransient(input, &sweep, diag);
    }
    default:
        break;
    }

    InputDiagReset(&idiag);
    InputCommandInit(&command);
    if (InputCommandSet(&command, id, &idiag) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "the command id is not one of this build's");
    }
    status = DesktopInputRouteCommand(input, &command, NULL, diag);
    if (status == RoboNotFound) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotFound,
                               "no target claimed the command");
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

void DesktopInputStepInit(DesktopInputStep *step)
{
    if (!step) {
        return;
    }
    MemSet(step, 0, sizeof(DesktopInputStep));
    step->lastCommand = RoboInputCommandNone;
    step->documentClickNode = RoboLayoutNodeIdNone;
}

int DesktopInputStepIsEmpty(const DesktopInputStep *step)
{
    if (!step) {
        return 1;
    }
    return (step->windowEvents == 0u && step->dispatched == 0u && step->actions == 0u &&
            step->focusChanges == 0u && step->hoverChanges == 0u && step->cursorChanges == 0u &&
            step->pointerMoves == 0u &&
            step->shortcuts == 0u && step->commands == 0u && step->textInputs == 0u &&
            step->closeRequested == 0 && step->resized == 0 && step->scaleChanged == 0)
               ? 1
               : 0;
}

Result DesktopInputStepWrite(const DesktopInputStep *step,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             DesktopDiag *diag)
{
    int written;

    if (!step || !buffer) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboInvalidArgument,
                               "a step and a buffer are required");
    }
    written = snprintf(buffer, capacity,
                       "step window=%u translated=%u skipped=%u dispatched=%u coalesced=%u "
                       "dropped=%u calls=%u actions=%u focus=%u hover=%u cursor=%u moves=%u "
                       "shortcuts=%u "
                       "commands=%u unclaimed=%u text=%u close=%d resized=%d scale=%d\n",
                       (unsigned)step->windowEvents, (unsigned)step->translated,
                       (unsigned)step->skipped, (unsigned)step->dispatched,
                       (unsigned)step->coalesced, (unsigned)step->dropped,
                       (unsigned)step->listenerCalls, (unsigned)step->actions,
                       (unsigned)step->focusChanges, (unsigned)step->hoverChanges,
                       (unsigned)step->cursorChanges, (unsigned)step->pointerMoves,
                       (unsigned)step->shortcuts,
                       (unsigned)step->commands, (unsigned)step->unclaimed,
                       (unsigned)step->textInputs, step->closeRequested, step->resized,
                       step->scaleChanged);
    if (written < 0 || (size_t)written >= capacity) {
        if (outLength) {
            *outLength = 640u;
        }
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboNotSupported,
                               "the step line did not fit");
    }
    if (outLength) {
        *outLength = (size_t)written;
    }
    return RoboOk;
}

Result GetDesktopInputStats(const DesktopInput *input, DesktopInputStats *out)
{
    if (!input || !out) {
        return RoboInvalidArgument;
    }
    *out = input->stats;
    return RoboOk;
}

int DesktopInputIsQuiescent(const DesktopInput *input)
{
    if (!input) {
        return 1;
    }
    if (input->queue && InputQueueCount(input->queue) > 0u) {
        return 0;
    }
    if (!InputCoalescerIsEmpty(&input->coalescer)) {
        return 0;
    }
    if (InputDragSessionIsActive(&input->session.drag)) {
        return 0;
    }
    return InputSessionIsQuiescent(&input->session);
}

/** Append @p text at @p used, refusing when it does not fit. */
static Result Append(const DesktopInput *input,
                     char *buffer,
                     size_t capacity,
                     size_t *used,
                     const char *text,
                     DesktopDiag *diag)
{
    size_t length;
    size_t index;

    (void)input;
    if (!text) {
        return RoboOk;
    }
    length = DesktopStringLength(text);
    if (*used + length + 1u > capacity) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboNotSupported,
                               "the input dump did not fit");
    }
    for (index = 0u; index < length; index++) {
        buffer[*used + index] = text[index];
    }
    *used += length;
    buffer[*used] = '\0';
    return RoboOk;
}

Result DesktopInputWrite(const DesktopInput *input,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         DesktopDiag *diag)
{
    InputDiag idiag;
    InputQueueStats queueStats;
    char sessionLine[1024];
    char routerLine[RoboInputCommandRouterDumpMax];
    char shortcutLine[RoboInputShortcutManagerDumpMax];
    char cursorLine[256];
    char clipboardLine[256];
    char head[1024];
    size_t sessionLength = 0u;
    size_t routerLength = 0u;
    size_t shortcutLength = 0u;
    size_t cursorLength = 0u;
    size_t clipboardLength = 0u;
    size_t used = 0u;
    int written;
    uint32_t index;

    if (!input || !buffer) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboInvalidArgument,
                               "a system and a buffer are required");
    }
    InputDiagReset(&idiag);
    sessionLine[0] = '\0';
    routerLine[0] = '\0';
    shortcutLine[0] = '\0';
    cursorLine[0] = '\0';
    clipboardLine[0] = '\0';

    (void)InputSessionWrite(&input->session, sessionLine, sizeof(sessionLine), &sessionLength, &idiag);
    (void)InputCommandRouterWrite(&input->router, routerLine, sizeof(routerLine), &routerLength,
                                  &idiag);
    (void)InputShortcutManagerWrite(&input->shortcuts, shortcutLine, sizeof(shortcutLine),
                                    &shortcutLength, &idiag);
    (void)InputCursorManagerWrite(&input->cursors, cursorLine, sizeof(cursorLine), &cursorLength,
                                  &idiag);
    if (input->clipboardLive) {
        (void)InputClipboardDataWrite(&input->clipboard, clipboardLine, sizeof(clipboardLine),
                                      &clipboardLength, &idiag);
    }

    GetInputQueueStats(input->queue, &queueStats);
    written = snprintf(head, sizeof(head),
                       "desktop-input %s live=%u devices=%u listeners=%u queue=%u/%u dropped=%llu\n"
                       "input stats steps=%llu window=%llu translated=%llu dispatched=%llu "
                       "calls=%llu actions=%llu focus=%llu cursor=%llu shortcuts=%llu "
                       "commands=%llu unclaimed=%llu text=%llu connects=%llu disconnects=%llu\n",
                       RoboInputVersionString, input->live ? 1u : 0u,
                       (unsigned)InputSessionDeviceCount(&input->session),
                       (unsigned)DesktopInputListenerCount(input), (unsigned)queueStats.count,
                       (unsigned)queueStats.capacity, (unsigned long long)queueStats.dropped,
                       (unsigned long long)input->stats.steps,
                       (unsigned long long)input->stats.windowEvents,
                       (unsigned long long)input->stats.translated,
                       (unsigned long long)input->stats.dispatched,
                       (unsigned long long)input->stats.listenerCalls,
                       (unsigned long long)input->stats.actions,
                       (unsigned long long)input->stats.focusChanges,
                       (unsigned long long)input->stats.cursorChanges,
                       (unsigned long long)input->stats.shortcuts,
                       (unsigned long long)input->stats.commands,
                       (unsigned long long)input->stats.unclaimed,
                       (unsigned long long)input->stats.textInputs,
                       (unsigned long long)input->stats.connects,
                       (unsigned long long)input->stats.disconnects);
    if (written < 0 || (size_t)written >= sizeof(head)) {
        if (outLength) {
            *outLength = RoboDesktopInputDumpMax;
        }
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboNotSupported,
                               "the input dump did not fit");
    }
    if (Append(input, buffer, capacity, &used, head, diag) != RoboOk) {
        if (outLength) {
            *outLength = RoboDesktopInputDumpMax;
        }
        return RoboNotSupported;
    }

    /* One line per registered content: what the caller told this layer about a window. */
    for (index = 0u; index < RoboDesktopInputMaxContents; index++) {
        const DesktopInputContentEntry *entry = &input->contents[index];
        char line[160];
        int lineLength;

        if (!entry->live) {
            continue;
        }
        lineLength = snprintf(line, sizeof(line),
                              "content window=%llu tree=%d node=%d editor=%d\n",
                              (unsigned long long)entry->content.window,
                              entry->content.tree ? 1 : 0,
                              HtmlNodeHandleIsWellformed(&entry->content.documentNode) ? 1 : 0,
                              entry->content.editor ? 1 : 0);
        if (lineLength < 0 || (size_t)lineLength >= sizeof(line) ||
            Append(input, buffer, capacity, &used, line, diag) != RoboOk) {
            if (outLength) {
                *outLength = RoboDesktopInputDumpMax;
            }
            return RoboNotSupported;
        }
    }
    if (Append(input, buffer, capacity, &used, sessionLine, diag) != RoboOk ||
        Append(input, buffer, capacity, &used, routerLine, diag) != RoboOk ||
        Append(input, buffer, capacity, &used, shortcutLine, diag) != RoboOk ||
        Append(input, buffer, capacity, &used, cursorLine, diag) != RoboOk ||
        Append(input, buffer, capacity, &used, clipboardLine, diag) != RoboOk) {
        if (outLength) {
            *outLength = RoboDesktopInputDumpMax;
        }
        return RoboNotSupported;
    }
    if (outLength) {
        *outLength = used;
    }
    return RoboOk;
}
