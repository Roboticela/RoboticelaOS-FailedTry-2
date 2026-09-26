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
#include "RoboInput/RoboInputLatency.h"
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
    /** The system this slot belongs to: what tells "my window has the keyboard" from "it does not". */
    DesktopInput *owner;
    InputEditorTarget editor;
    int editorLive;
    /**
     * Non-zero once this slot's **one** router entry has been pushed.
     *
     * One entry per slot, reused rather than re-pushed, and that is a lifetime rule rather than a tidiness
     * one: the router holds its targets **by value**, so an entry whose `userdata` is the *caller's* command
     * target or a short-lived editor is a pointer that outlives nothing. This slot lives as long as the
     * system does, so the entry's address is stable and re-registering a window's contents updates it in
     * place -- no duplicate chain entries, and nothing dangling when a content record is replaced.
     */
    int pushed;
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

    /** Empty unless the caller supplied a clock; every dispatched event opens a record in it (13.48). */
    InputLatencyTrace latency;
    int latencyLive;

    InputDeviceId keyboardId;
    InputDeviceId mouseId;

    /** The pointer this build tracks. A mouse has exactly one. */
    InputPointerId pointer;
    /** The subject of a running window drag, if any. */
    DesktopWindowId dragWindow;
    int32_t dragGrabX;
    int32_t dragGrabY;

    /**
     * The running **resize**, if any (13.67).
     *
     * `resizeArea` and `resizeStart` are recorded at the press and never re-derived: the zone is what the
     * press named, and the frame is what the window looked like before the gesture, which is what makes
     * "drag the right edge 40px left" clamp against the *original* left edge rather than against itself.
     */
    DesktopWindowId resizeWindow;
    DesktopArea resizeArea;
    GfxRect resizeStart;
    int32_t resizeGrabX;
    int32_t resizeGrabY;

    /**
     * The target whose declared cursor shape is in force for the running gesture, if any.
     *
     * Kept so the override can be *removed* by the gesture that installed it. Without the record the only way
     * to clear it would be to clear every override, which would drop a control's own declaration with it.
     */
    InputTarget gestureCursor;
    int gestureCursorLive;

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
    config->clockMs = NULL;
    config->clockUserdata = NULL;
}

/** The caller's clock, or 0 when there is none. One place, so no stamp can read a different clock. */
static uint64_t NowMs(const DesktopInput *input)
{
    if (!input || !input->config.clockMs) {
        return 0u;
    }
    return input->config.clockMs(input->config.clockUserdata);
}

/**
 * The cursor shape a resize zone asks for: a horizontal split on a vertical edge, a vertical split on a
 * horizontal one, a diagonal on a corner.
 *
 * Note the *swap*, which is the whole reason this is a function rather than a table lookup a reader would
 * mis-remember: the **left** edge is dragged horizontally, so the shape is `resize-h`; the **top** edge is
 * dragged vertically, so it is `resize-v`. A build that named the shape after the edge rather than after the
 * drag would draw the wrong arrow on all four edges and still pass every test that only asked for "a resize
 * cursor".
 */
static InputCursorType CursorForResizeArea(DesktopArea area)
{
    switch (area) {
    case RoboDesktopAreaWindowResizeLeft:
    case RoboDesktopAreaWindowResizeRight:
        return RoboInputCursorResizeHorizontal;
    case RoboDesktopAreaWindowResizeTop:
    case RoboDesktopAreaWindowResizeBottom:
        return RoboInputCursorResizeVertical;
    case RoboDesktopAreaWindowResizeTopLeft:
    case RoboDesktopAreaWindowResizeBottomRight:
    case RoboDesktopAreaWindowResizeTopRight:
    case RoboDesktopAreaWindowResizeBottomLeft:
        return RoboInputCursorResizeDiagonal;
    default:
        return RoboInputCursorDefault;
    }
}

/**
 * The frame a running resize asks for, from the pointer's screen position.
 *
 * Both edges of an axis are moved **relative to the frame the gesture began with** (`resizeStart`) rather than
 * to the current one, which is what makes the two directions inverse: dragging the left edge 40 px right and
 * then 40 px back returns the original rectangle exactly, where an implementation that accumulated deltas
 * from the last frame would drift by every rounding.
 */
static GfxRect ResizeBounds(const DesktopInput *input, GfxPoint screen)
{
    const DesktopWindowMetrics *m = &input->desktop->windowMetrics;
    GfxRect frame = input->resizeStart;
    int32_t dx = screen.x - input->resizeGrabX;
    int32_t dy = screen.y - input->resizeGrabY;
    DesktopArea area = input->resizeArea;
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;

    if (area == RoboDesktopAreaWindowResizeLeft || area == RoboDesktopAreaWindowResizeTopLeft ||
        area == RoboDesktopAreaWindowResizeBottomLeft) {
        left = 1;
    }
    if (area == RoboDesktopAreaWindowResizeRight || area == RoboDesktopAreaWindowResizeTopRight ||
        area == RoboDesktopAreaWindowResizeBottomRight) {
        right = 1;
    }
    if (area == RoboDesktopAreaWindowResizeTop || area == RoboDesktopAreaWindowResizeTopLeft ||
        area == RoboDesktopAreaWindowResizeTopRight) {
        top = 1;
    }
    if (area == RoboDesktopAreaWindowResizeBottom ||
        area == RoboDesktopAreaWindowResizeBottomLeft ||
        area == RoboDesktopAreaWindowResizeBottomRight) {
        bottom = 1;
    }

    /*
     * One axis at a time, and each with the same two rules: an edge move changes the origin and the extent by
     * the same amount (so the opposite edge stays where it was), and the extent is **clamped** to the style's
     * minimum by moving the origin instead -- which is what makes a window dragged past its minimum stop
     * rather than invert. `DesktopWindowSetBounds` would clamp the extent too, but clamping *here* is what
     * keeps the origin honest while the pointer is still down.
     */
    if (left) {
        int32_t newX = frame.x + dx;
        int32_t maxX = frame.x + frame.width - m->minWidth;

        if (newX > maxX) {
            newX = maxX;
        }
        frame.width += frame.x - newX;
        frame.x = newX;
    } else if (right) {
        frame.width += dx;
        if (frame.width < m->minWidth) {
            frame.width = m->minWidth;
        }
    }
    if (top) {
        int32_t newY = frame.y + dy;
        int32_t maxY = frame.y + frame.height - m->minHeight;

        if (newY > maxY) {
            newY = maxY;
        }
        frame.height += frame.y - newY;
        frame.y = newY;
    } else if (bottom) {
        frame.height += dy;
        if (frame.height < m->minHeight) {
            frame.height = m->minHeight;
        }
    }
    return frame;
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
        /*
         * `InputRoute::clientBounds` is the client rectangle in **window** space (its documented meaning),
         * and `DesktopWindowClientBounds` answers in screen space -- so the frame's origin comes off here,
         * once, rather than making every window\u2194document conversion add it back.
         */
        client.x -= bounds.x;
        client.y -= bounds.y;
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

/**
 * Non-zero when the focused target's window is still one the shell holds (or the shell's own native window).
 *
 * A native window id is not a shell window, so the two namespaces are told apart rather than compared: the
 * shell's own window is always live while it exists, and a document or window target naming a shell window
 * has to be checked. A target naming neither (an application, a window this shell never had) is left alone --
 * refusing to route for it would be this layer inventing an owner rule it does not have.
 */
static int FocusWindowIsLive(const DesktopInput *input)
{
    WindowId window;

    if (!input->session.focus.hasFocus) {
        return 0;
    }
    window = input->session.focus.focused.window;
    if (window == RoboWindowIdNone) {
        return 1;
    }
    if (window == input->config.window) {
        return 1;
    }
    return DesktopWindowExists(input->desktop, AsDesktopWindow(window)) ? 1 : 0;
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
        /* A point on another plane needs that plane's window geometry to reach the screen; the event's own
         * window when there is one, else the front-most window, which is the only window a caller could have
         * meant when the event named none. */
        InputRoute spaceRoute;
        WindowId spaceWindow = input->config.window;

        InputRouteInit(&spaceRoute);
        if (spaceWindow == RoboWindowIdNone) {
            spaceWindow = AsWindowId(FrontWindow(input));
        }
        (void)DesktopInputRouteForWindow(input, AsDesktopWindow(spaceWindow), &spaceRoute, NULL);
        status = InputConvertPoint(point, targetSpace, RoboInputSpaceScreen, &spaceRoute, &screen, &idiag);
        if (status != RoboOk) {
            return ShellDiag(diag, &idiag, status, "the point is on a plane this build cannot convert");
        }
    }
    status = DesktopHitTest(input->desktop, screen, &hit);
    if (status != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                               "the shell could not resolve the point");
    }

    /*
     * A point outside the *screen* is `RoboNotFound`: there is nothing this layer can name, and inventing a
     * window for it would be the bug 13.70 exists to prevent. The hit is still reported, so a caller can see
     * `inside == 0` rather than a bare refusal.
     */
    if (!hit.inside) {
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

    /*
     * Inside the screen but over no window: the panel or the background. That is the shell's own surface, so
     * the answer names the shell's native window when it has one and *no window* when it does not -- the
     * front-most window is deliberately not substituted, because a click on the panel is not a click on
     * whatever window happens to be on top.
     */
    window = hit.window;
    if (WindowIsNone(window)) {
        window = AsDesktopWindow(input->config.window);
    }
    if (WindowIsNone(window)) {
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
            *outResolved = 1;
        }
        return RoboOk;
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
                PaintHitTestStats paintStats;
                PaintDiag paintDiag;
                LayoutPoint layoutPoint;

                layoutPoint.x = docPoint.x;
                layoutPoint.y = docPoint.y;
                PaintHitTestResultInit(&result);
                PaintHitTestStatsInit(&paintStats);
                PaintDiagReset(&paintDiag);
                if (PaintHitTest(entry->content.tree, layoutPoint, &result, &paintStats, &paintDiag) ==
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

static void GestureCursorClear(DesktopInput *input);

/**
 * Declare a shape for the duration of a gesture, replacing any earlier gesture's.
 *
 * The one place the shell changes the cursor *directly*, and it goes through the model's own override table
 * (13.43) rather than around it: the manager still decides, and a control's declaration for the same target
 * still wins by being registered later. The previous gesture's override is removed **first** so a move that
 * began before an earlier one ended cannot leave two live declarations for one target.
 */
static void GestureCursorDeclare(DesktopInput *input,
                                 const InputTarget *target,
                                 InputCursorType shape,
                                 InputDiag *diag)
{
    GestureCursorClear(input);
    if (!InputTargetIsValid(target)) {
        return;
    }
    if (InputCursorManagerSetOverride(&input->cursors, target, shape, diag) == RoboOk) {
        input->gestureCursor = *target;
        input->gestureCursorLive = 1;
    }
}

/** Remove the running gesture's cursor declaration, if it made one. */
static void GestureCursorClear(DesktopInput *input)
{
    if (!input->gestureCursorLive) {
        return;
    }
    (void)InputCursorManagerRemoveOverride(&input->cursors, &input->gestureCursor);
    input->gestureCursorLive = 0;
    InputTargetInit(&input->gestureCursor);
}

/**
 * A press: raise or close, begin a window drag on a title bar, or anchor a resize on an edge.
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
    /* The listener store has no per-type mask, so a callback hears every event on its target; the shell's
     * two listeners therefore state the type they answer, which is what keeps a press from also being read
     * as a release (and ending the gesture it just began). */
    if (event->type != RoboInputEventPointerDown) {
        return RoboOk;
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

    /*
     * A resize edge or corner (13.67): raise, anchor the zone, and declare the cursor the gesture will keep.
     * No drag session is begun -- a resize carries no payload and drops nothing, so a drag would be a gesture
     * about the wrong thing -- and the *capture* is set by the drain, which is what makes every later move
     * reach this window even when the pointer leaves the edge it started on.
     */
    if (DesktopAreaIsResize(hit.area)) {
        GfxRect bounds;

        if (DesktopWindowRaise(input->desktop, hit.window) == RoboOk) {
            NoteSet(input, "resize");
        }
        input->resizeWindow = hit.window;
        input->resizeArea = hit.area;
        input->resizeGrabX = screen.x;
        input->resizeGrabY = screen.y;
        if (DesktopWindowBounds(input->desktop, hit.window, &bounds) == RoboOk) {
            input->resizeStart = bounds;
        } else {
            input->resizeStart = MakeGfxRect(screen.x, screen.y, 0, 0);
        }
        GestureCursorDeclare(input, &event->target, CursorForResizeArea(hit.area), &idiag);
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
                input->stats.dragsBegan++;
                /*
                 * 13.75: the shape a *carry* shows, declared for the gesture and removed when it ends. The
                 * default table never answers `grabbing`, because no target is being grabbed -- a gesture is,
                 * and a gesture is exactly what an override is for.
                 */
                GestureCursorDeclare(input, &event->target, RoboInputCursorGrabbing, &idiag);
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
    if (event->type != RoboInputEventPointerUp && event->type != RoboInputEventPointerCancel) {
        return RoboOk;
    }
    InputDiagReset(&idiag);
    InputDragEventInit(&dragEvent);

    /* A release ends whichever gesture is running, and both are ended here rather than by the frame's own
     * bookkeeping -- a gesture that outlived its release would keep the pointer captured for good. */
    if (!WindowIsNone(input->resizeWindow)) {
        input->resizeWindow = RoboDesktopWindowIdNone;
        input->resizeArea = RoboDesktopAreaNone;
        NoteSet(input, "resize-end");
    }
    GestureCursorClear(input);

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
    input->stats.dragsEnded++;
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
    if (DesktopWindowSetBounds(input->desktop, input->dragWindow, bounds, NULL) == RoboOk) {
        input->stats.windowMoves++;
    }
}

/**
 * A move while a resize is running resizes the window the press anchored.
 *
 * The zone is the one the *press* named (`input->resizeArea`) rather than whatever the pointer is over now,
 * and the frame is derived from `resizeStart`, which is the pair that makes the gesture stable: a resize that
 * re-hit-tested on every move would jump to another edge the moment the pointer crossed one, and one that
 * accumulated deltas would drift.
 */
static void ShellFollowResize(DesktopInput *input, const InputEvent *event)
{
    GfxPoint screen;
    InputDiag idiag;
    GfxRect bounds;

    if (WindowIsNone(input->resizeWindow)) {
        return;
    }
    InputDiagReset(&idiag);
    screen = event->payload.pointer.position;
    if (InputSpaceIsKnown(event->space) && event->space != RoboInputSpaceScreen) {
        if (InputConvertPoint(screen, event->space, RoboInputSpaceScreen, NULL, &screen, &idiag) !=
            RoboOk) {
            return;
        }
    }
    bounds = ResizeBounds(input, screen);
    if (DesktopWindowSetBounds(input->desktop, input->resizeWindow, bounds, NULL) == RoboOk) {
        input->stats.windowResizes++;
        NoteSet(input, "resize");
    }
}

/* ------------------------------------------------------------------ */
/* The command targets the shell installs (13.73 / 13.74)               */
/* ------------------------------------------------------------------ */

/**
 * One content slot's router entry: the focused editor first, the window's own application commands second.
 *
 * `RoboNotSupported` from both is exactly what "this window does not handle that command" means, so the walk
 * continues to the shell's own clipboard target and then to nothing -- which is `RoboNotFound` upstream, a
 * *counted* unclaimed command rather than an error. A slot whose content record has been cleared is not live
 * and declines, which is why a window that has gone cannot stop a command that was never about it.
 */
static Result ContentCommandsExecute(void *userdata, const InputCommand *command, InputDiag *diag)
{
    DesktopInputContentEntry *entry = (DesktopInputContentEntry *)userdata;

    if (!entry || !entry->live) {
        return RoboNotSupported;
    }
    /*
     * A window's contents answer the keyboard only while that window *has* the keyboard. Without this an editor
     * in a background window would type the characters a key aimed at the focused one produced -- which is the
     * one failure mode a window system must not have. `SetContent` pairs every slot with its system, so the
     * check is a comparison rather than a second focus rule.
     */
    if (entry->owner) {
        WindowId active = entry->owner->session.focus.activeWindow;

        if (entry->owner->session.focus.hasFocus) {
            active = entry->owner->session.focus.focused.window;
        }
        if (active != (WindowId)entry->content.window) {
            return RoboNotSupported;
        }
    }
    /*
     * Copy, cut and paste belong to the **shell's** clipboard, not the editor's own: the editor's bridge does
     * map them (a text engine's internal clipboard), but letting it claim them would shadow the system
     * clipboard the shell installs one link further out. Declining them here is what makes `Ctrl+C` reach
     * `ShellClipboardExecute`, which reads the editor's selection and writes the one clipboard the OS has.
     */
    if (command && (command->id == RoboInputCommandCopy || command->id == RoboInputCommandCut ||
                    command->id == RoboInputCommandPaste)) {
        return RoboNotSupported;
    }
    if (entry->editorLive) {
        Result status = InputEditorTargetExecute(&entry->editor, command, diag);

        if (status != RoboNotSupported) {
            return status;
        }
    }
    if (InputCommandTargetIsValid(&entry->content.commands)) {
        return entry->content.commands.execute(entry->content.commands.userdata, command, diag);
    }
    return RoboNotSupported;
}

/**
 * The editor a clipboard operation talks to: the **active** window's, resolved now rather than remembered.
 *
 * Resolved rather than stored, and that is the fix for a real lifetime bug rather than a preference: a
 * remembered `InputEditorTarget *` survives the content record it points into, so a window whose contents
 * were replaced -- or destroyed -- left the clipboard talking to a freed editor. The focused window answers
 * first (a clipboard belongs to whatever has the keyboard), then the front-most window that has one, which
 * is what "the active window" means before anything has been clicked.
 */
static InputEditorTarget *ActiveEditor(DesktopInput *input)
{
    uint32_t index;

    if (input->session.focus.hasFocus) {
        DesktopWindowId window = AsDesktopWindow(input->session.focus.focused.window);
        DesktopInputContentEntry *entry = WindowIsNone(window) ? NULL : FindContentMutable(input, window);

        if (entry && entry->editorLive) {
            return &entry->editor;
        }
    }
    for (index = input->desktop->windowCount; index > 0u; index--) {
        const DesktopWindow *window = &input->desktop->windows[index - 1u];
        DesktopInputContentEntry *entry;

        if (!window->visible || window->state != RoboDesktopWindowOpen) {
            continue;
        }
        entry = FindContentMutable(input, window->id);
        if (entry && entry->editorLive) {
            return &entry->editor;
        }
    }
    return NULL;
}

/** The one text source the clipboard reads: the active window's editor's selection. */
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
    InputEditorTarget *editor;
    size_t length = 0u;

    if (!input || !command) {
        return RoboInvalidArgument;
    }
    if (command->id != RoboInputCommandCopy && command->id != RoboInputCommandCut &&
        command->id != RoboInputCommandPaste) {
        return RoboNotSupported;
    }
    editor = ActiveEditor(input);
    if (!editor) {
        /* No active editor: a copy of nothing is nothing, and `NotSupported` lets the walk continue. */
        return RoboNotSupported;
    }

    if (command->id == RoboInputCommandPaste) {
        if (!input->clipboardLive) {
            return RoboNotSupported;
        }
        result = InputClipboardPaste(&input->clipboard, ClipboardInsertText, input);
        if (result == RoboInputClipboardCopied || result == RoboInputClipboardNothing) {
            input->stats.clipboardOps++;
            return RoboOk;
        }
        return RoboNotSupported;
    }

    source.userdata = editor->editor;
    source.readRange = ClipboardReadSelection;
    result = InputClipboardCopy(&source, 0u, 0xFFFFFFFFu, RoboInputApplicationIdNone,
                                &input->clipboard, &length, diag);
    if (result == RoboInputClipboardNothing) {
        return RoboOk; /* An empty selection: the clipboard keeps what it had, and nothing was performed. */
    }
    input->stats.clipboardOps++;
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
        return InputCommandApplyToEditor(&cut, editor->editor, NULL, NULL, diag);
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
    InputFocusTransition transition;
    InputDiag idiag;
    DesktopWindowId windows[RoboDesktopMaxWindows];
    size_t count = 0u;
    size_t index;
    int current = -1;
    size_t next;
    WindowId active;
    InputTarget target;
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
    InputFocusTransitionInit(&transition);

    /*
     * The shell's windows, back to front: the order `Tab` visits them. This is a *list of windows*, not the
     * model's focus chain, and the difference is the whole reason this door exists: a chain target must belong
     * to the manager's active window (it names a control *inside* one window), so walking a chain cannot
     * express "the next window" -- the shell activates the window first and then asks it to take focus, which
     * is exactly what a pointer press does.
     */
    for (index = 0u; index < input->desktop->windowCount && count < RoboDesktopMaxWindows; index++) {
        const DesktopWindow *window = &input->desktop->windows[index];

        if (!window->visible || window->state != RoboDesktopWindowOpen) {
            continue;
        }
        windows[count++] = window->id;
    }
    if (count == 0u) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotFound,
                               "no window of this shell can take focus");
    }
    active = input->session.focus.hasFocus ? input->session.focus.focused.window
                                           : input->session.focus.activeWindow;
    for (index = 0u; index < count; index++) {
        if ((WindowId)windows[index] == active) {
            current = (int)index;
            break;
        }
    }
    switch (direction) {
    case RoboInputFocusDirectionFirst:
        next = 0u;
        break;
    case RoboInputFocusDirectionLast:
        next = count - 1u;
        break;
    case RoboInputFocusDirectionPrevious:
        next = (current < 0) ? count - 1u : ((current == 0) ? 0u : (size_t)(current - 1));
        break;
    default:
        next = (current < 0) ? 0u
                             : (((size_t)(current + 1) < count) ? (size_t)(current + 1) : count - 1u);
        break;
    }

    if (AsDesktopWindow(active) != windows[next]) {
        status = ActivateWindow(input, windows[next], NULL, diag);
        if (status != RoboOk) {
            return status;
        }
    }
    InputTargetInit(&target);
    status = InputTargetSetWindow(&target, AsWindowId(windows[next]), RoboInputSpaceScreen, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the window is not a target");
    }
    status = InputFocusRequest(&input->session.focus, &target, reason, &transition, &idiag);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "focus could not be moved");
    }
    (void)DesktopWindowRaise(input->desktop, windows[next]);
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
            /* Through the *command* door, not the router: a shell command (`Tab`'s navigation, a window
             * action) is answered by this layer itself, while every other command walks the router. */
            if (DesktopInputPerformCommand(input, shortcut.command, diag) == RoboOk) {
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
        const InputTarget *capturedTarget =
            InputPointerCaptureTarget(&input->session.captures, event.payload.pointer.pointer);
        int captured = (capturedTarget && (event.type == RoboInputEventPointerMove ||
                                           event.type == RoboInputEventPointerUp ||
                                           event.type == RoboInputEventPointerCancel))
                           ? 1
                           : 0;

        if (captured) {
            /*
             * A captured pointer is delivered to its holder whatever is under it, *including a position
             * outside the screen* -- which is exactly what a window drag produces when the pointer leaves
             * the desktop. That is what capture means; dropping such a move at the hit test would leave the
             * gesture stuck, so no hit test runs for it.
             */
            target = *capturedTarget;
            window = AsDesktopWindow(target.window);
            docPoint = event.payload.pointer.position;
            resolved = 1;
        } else if (ResolvePointInternal(input, event.payload.pointer.position, event.space, &target,
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

        /*
         * 13.17: a captured pointer's moves and its release go to its holder, whatever is under it, and the
         * **cursor** goes there too (13.75). Asking the holder first is what makes a hover shape a function
         * of the capture rather than of what the pointer passed over: a window being dragged across a text
         * field keeps its `grabbing` shape, because the pointer belongs to the drag.
         */
        {
            const InputTarget *holder =
                InputPointerCaptureTarget(&input->session.captures, event.payload.pointer.pointer);

            if (holder &&
                (event.type == RoboInputEventPointerMove || event.type == RoboInputEventPointerUp)) {
                target = *holder;
            }

            /* 13.43/13.75: the shape the hit test asks for -- or the one the gesture declared. */
            {
                int changed = 0;

                (void)InputCursorManagerUpdate(&input->cursors, &target, &changed);
                if (changed) {
                    step->cursorChanges++;
                    input->stats.cursorChanges++;
                }
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
        /*
         * 13.42/13.74/13.67: the drag follows the pointer, the window follows the drag, and a resize
         * follows the pointer. Both are driven from here rather than from a listener so the order is fixed:
         * the model's own state (hover, capture, cursor) is already updated, and the *frame* the pointer is
         * requesting is applied last -- the same order the frame loop reads them in.
         */
        if (event.type == RoboInputEventPointerMove) {
            ShellFollowDrag(input, &event);
            ShellFollowResize(input, &event);
        }
    } else {
        /* A key event is about whatever has focus — that is what focus means. */
        if (input->session.focus.hasFocus && FocusWindowIsLive(input)) {
            target = input->session.focus.focused;
            event.window = target.window;
        } else {
            if (input->session.focus.hasFocus) {
                /*
                 * The focused window has gone and nobody told this layer (13.77.8's defensive half): a key
                 * must not be routed at a window the shell no longer holds, so the focus is dropped and the
                 * event falls through to the front window below. `DesktopInputForgetWindow` is the door a
                 * caller uses when it knows; this is what makes forgetting a *courtesy* rather than a
                 * requirement.
                 */
                InputFocusTransition dropped;

                InputFocusTransitionInit(&dropped);
                (void)InputFocusManagerClear(&input->session.focus, RoboInputFocusReasonProgrammatic,
                                             &dropped, NULL);
            }
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

    /*
     * 13.48/13.77.12: one latency record per dispatched event, and only when the caller supplied a clock.
     *
     * The two stamps are the event's **own** timestamp (what the device said) and the caller's clock (when
     * this loop saw it), which is exactly the pair `dispatchMs` is defined as the difference of -- so a
     * caller whose clock is on a different epoch than their events gets a *refused* record counted in
     * `outOfOrderTotal` rather than a plausible small number. That is the module's rule and this file keeps
     * it instead of normalizing the two into agreement.
     */
    {
        uint32_t latencySequence = 0u;

        if (input->latencyLive &&
            InputLatencyBegin(&input->latency, &event, event.timestampMs, NowMs(input),
                              &latencySequence, &idiag) == RoboOk) {
            step->traced++;
            (void)InputLatencyMark(&input->latency, latencySequence, RoboInputLatencyDispatch,
                                   NowMs(input), &idiag);
        } else {
            latencySequence = 0u;
        }

        status = InputDispatch(&input->dispatcher, &event, &path, &context, &idiag);
        step->dispatched++;
        step->listenerCalls += context.calls;
        input->stats.dispatched++;
        input->stats.listenerCalls += context.calls;
        if (context.defaultPrevented) {
            step->prevented++;
        }

        if (latencySequence != 0u) {
            InputLatencyDurations durations;

            MemSet(&durations, 0, sizeof(durations));
            if (InputLatencyMark(&input->latency, latencySequence, RoboInputLatencyHandler,
                                 NowMs(input), &idiag) == RoboOk &&
                InputLatencyMeasure(&input->latency, latencySequence, &durations, &idiag) == RoboOk) {
                if (durations.handlerMs > step->slowestHandlerMs) {
                    step->slowestHandlerMs = durations.handlerMs;
                }
            }
            /*
             * `Present` is marked here rather than by a frame, and that is a *choice*: this layer does not
             * own when a frame runs (13.77.6 keeps that the compositor's and the scene's), so the honest
             * completion it can stamp is "the handler returned and this event's work is done". A build with a
             * frame scheduler would move this mark out and the record's `frameMs` would stay the same
             * number, which is why `handlerMs` and `frameMs` are stored separately.
             */
            (void)InputLatencyMark(&input->latency, latencySequence, RoboInputLatencyPresent,
                                   NowMs(input), &idiag);
        }
    }

    /*
     * The shell's own listeners run inside the dispatch above and may have declared or removed a gesture
     * cursor override (13.75). The manager decides the shape in *one* place, so it is asked again now that
     * they have run; otherwise the shape a gesture installed would not be in force until the next move.
     */
    if (positioned) {
        int cursorChanged = 0;

        (void)InputCursorManagerUpdate(&input->cursors, &target, &cursorChanged);
        if (cursorChanged) {
            step->cursorChanges++;
            input->stats.cursorChanges++;
        }
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
    /**
     * Deltas, because these four are *lifetime* counters on the system and a step reports what it did.
     * Reading the count and subtracting is what keeps one place the truth: a step field maintained by hand
     * beside a lifetime counter is two numbers that can disagree.
     */
    uint64_t clipboardBefore;
    uint64_t dragsBeganBefore;
    uint64_t dragsEndedBefore;
    uint64_t movesBefore;
    uint64_t resizesBefore;

    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system is required");
    }
    clipboardBefore = input->stats.clipboardOps;
    dragsBeganBefore = input->stats.dragsBegan;
    dragsEndedBefore = input->stats.dragsEnded;
    movesBefore = input->stats.windowMoves;
    resizesBefore = input->stats.windowResizes;
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
    step.clipboardOps = (uint32_t)(input->stats.clipboardOps - clipboardBefore);
    step.dragsBegan = (uint32_t)(input->stats.dragsBegan - dragsBeganBefore);
    step.dragsEnded = (uint32_t)(input->stats.dragsEnded - dragsEndedBefore);
    step.windowMoves = (uint32_t)(input->stats.windowMoves - movesBefore);
    step.windowResizes = (uint32_t)(input->stats.windowResizes - resizesBefore);
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
    step.dragsBegan = drained.dragsBegan;
    step.dragsEnded = drained.dragsEnded;
    step.windowMoves = drained.windowMoves;
    step.windowResizes = drained.windowResizes;
    step.pointerMoves = drained.pointerMoves;
    step.documentClick = drained.documentClick;
    step.documentClickNode = drained.documentClickNode;
    step.documentClickX = drained.documentClickX;
    step.documentClickY = drained.documentClickY;
    step.traced = drained.traced;
    step.slowestHandlerMs = drained.slowestHandlerMs;
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

    /*
     * The **editing keys** (13.72), as a second table rather than a second mechanism: these are chords too --
     * the model's own table resolves them, `HandleKeyEvent` consumes the key, and the command reaches the
     * focused editor through the chain 13.36 built. That is what makes `Backspace` a command rather than a
     * character, and what keeps a build from having to invent a key-to-command function beside the table the
     * Ctrl chords already use.
     *
     * Every right-hand side is a *movement* or a *deletion* the editor names (`RoboInputTextBridge.h`), so a
     * key that the editor refuses (`Up`/`Down` and the line movements need a layout, 12.22) answers
     * `NotSupported` and the walk continues -- which is a *reported* absence rather than a silent horizontal
     * approximation. None of these keys produces text in any shipped layout, so binding them cannot stop a
     * character from being typed.
     */
    static const struct {
        InputCommandId command;
        PhysicalKey physical;
        InputModifiers modifiers;
    } editing[] = {
        { RoboInputCommandDeleteBackward, RoboPhysicalKeyBackspace, 0u },
        { RoboInputCommandDeleteForward, RoboPhysicalKeyDelete, 0u },
        { RoboInputCommandMoveLeft, RoboPhysicalKeyArrowLeft, 0u },
        { RoboInputCommandMoveRight, RoboPhysicalKeyArrowRight, 0u },
        { RoboInputCommandMoveUp, RoboPhysicalKeyArrowUp, 0u },
        { RoboInputCommandMoveDown, RoboPhysicalKeyArrowDown, 0u },
        { RoboInputCommandMoveLineStart, RoboPhysicalKeyHome, 0u },
        { RoboInputCommandMoveLineEnd, RoboPhysicalKeyEnd, 0u },
        { RoboInputCommandMoveDocumentStart, RoboPhysicalKeyHome, RoboKeyModCtrl },
        { RoboInputCommandMoveDocumentEnd, RoboPhysicalKeyEnd, RoboKeyModCtrl },
        { RoboInputCommandSelectLeft, RoboPhysicalKeyArrowLeft, RoboKeyModShift },
        { RoboInputCommandSelectRight, RoboPhysicalKeyArrowRight, RoboKeyModShift },
        { RoboInputCommandCancel, RoboPhysicalKeyEscape, 0u },
        { RoboInputCommandScrollPageUp, RoboPhysicalKeyPageUp, 0u },
        { RoboInputCommandScrollPageDown, RoboPhysicalKeyPageDown, 0u }
    };

    for (index = 0u; index < sizeof(editing) / sizeof(editing[0]); index++) {
        InputShortcut shortcut;
        InputDiag idiag;

        InputDiagReset(&idiag);
        InputShortcutInit(&shortcut);
        if (InputShortcutSet(&shortcut, editing[index].command, editing[index].physical,
                             editing[index].modifiers, RoboInputShortcutScopeWindow, &idiag) !=
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
    InputLatencyTraceInit(&input->latency);
    /* A clock, or no measurement at all. There is no third state: an unsupplied clock measuring against a
     * zero timebase would report every event as instantaneous. */
    input->latencyLive = input->config.clockMs ? 1 : 0;
    input->resizeWindow = RoboDesktopWindowIdNone;
    input->resizeArea = RoboDesktopAreaNone;
    InputTargetInit(&input->gestureCursor);

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
    entry->owner = input;
    entry->live = 1;
    if (content->editor &&
        InputEditorTargetInit(&entry->editor, content->editor, &idiag) == RoboOk) {
        entry->editorLive = 1;
    }

    /*
     * **One** router entry for this slot, pushed on first use and reused after that, whose `userdata` is the
     * slot itself.
     *
     * The slot asks the editor first and the caller's application target second, which is the order the two
     * questions have: a focused text field consumes `Ctrl+A` before the window does (13.36's own rule, and
     * the router asks the *last* pushed target first, so pushing one entry that asks in this order is what
     * keeps the priority readable instead of depending on how many times a window was re-registered).
     */
    if (!entry->pushed) {
        InputCommandTarget target;

        InputCommandTargetInit(&target);
        target.userdata = entry;
        target.execute = ContentCommandsExecute;
        if (InputCommandRouterPush(&input->router, &target, &idiag) == RoboOk) {
            entry->pushed = 1;
        }
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
    (void)index;
    if (entry->editorLive) {
        InputEditorTargetRelease(&entry->editor);
        entry->editorLive = 0;
    }
    /*
     * The slot goes dark rather than away: the router's copy of this entry is still on the chain, and a
     * record that answered with a *destroyed* editor would be the dangling pointer this design exists to
     * avoid. `ContentCommandsExecute` reads @ref DesktopInputContentEntry::live and declines while it is 0,
     * so the walk continues past a window whose contents have gone instead of stopping on a failure.
     */
    entry->live = 0;
    entry->content.editor = NULL;
    InputCommandTargetInit(&entry->content.commands);
    HtmlNodeHandleClear(&entry->content.documentNode);
    entry->content.tree = NULL;
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
    if (input->desktop) {
        input->desktop->pointer.pressed = 0;
    }
    input->dragWindow = RoboDesktopWindowIdNone;
    /* A cancelled gesture ends here too: the pointer's capture is the session's and the two window gestures are
     * this layer's, so the sweep has to be the one place *both* halves are forgotten. */
    input->resizeWindow = RoboDesktopWindowIdNone;
    input->resizeArea = RoboDesktopAreaNone;
    GestureCursorClear(input);
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

int DesktopInputIsMovingWindow(const DesktopInput *input)
{
    return (input && !WindowIsNone(input->dragWindow) &&
            InputDragSessionIsActive(&input->session.drag))
               ? 1
               : 0;
}

int DesktopInputIsResizingWindow(const DesktopInput *input)
{
    return (input && !WindowIsNone(input->resizeWindow)) ? 1 : 0;
}

DesktopArea DesktopInputResizeArea(const DesktopInput *input)
{
    return input ? input->resizeArea : RoboDesktopAreaNone;
}

DesktopWindowId DesktopInputGestureWindow(const DesktopInput *input)
{
    if (!input) {
        return RoboDesktopWindowIdNone;
    }
    if (!WindowIsNone(input->resizeWindow)) {
        return input->resizeWindow;
    }
    return input->dragWindow;
}

Result GetDesktopInputCapabilities(DesktopInputCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    MemSet(out, 0, sizeof(*out));

    /*
     * Every seam 13.66--13.77 names, and each is a *door* rather than an aspiration: a flag is 1 exactly
     * when a suite in `Tests/Desktop/SuiteRoboDesktopInput*.c` drives it end to end.
     */
    out->shellConsumption = 1;
    out->windowManagement = 1;
    out->windowMoving = 1;
    out->windowResizing = 1;
    out->documentDelivery = 1;
    out->focusDelivery = 1;
    out->pointerHitTesting = 1;
    out->pointerCapture = 1;
    out->coordinateSpaces = 1;
    out->keyboardDelivery = 1;
    out->textDelivery = 1;
    out->shortcutRouting = 1;
    out->commandRouting = 1;
    out->clipboard = 1;
    out->dragDrop = 1;
    out->cursorManagement = 1;
    out->eventLoop = 1;
    out->inputCoalescing = 1;
    out->inputLatency = 1;
    out->lifecycleSweep = 1;
    out->introspection = 1;

    /* The three absences, named rather than implied -- the same discipline `RoboInputVersion.h` keeps one
     * module down. Each is a structure this layer does not have, not a flag somebody forgot to set. */
    out->windowSnapping = 0;    /* no tiling policy and no gesture (Phase 14.17) */
    out->frameScheduling = 0;   /* the frame's *when* is the compositor's and the scene's (13.77.6) */
    out->cursorImages = 0;      /* this layer names a shape; drawing one is the shell's */

    out->defaultQueueCapacity = RoboDesktopInputDefaultQueueCapacity;
    out->maxContents = RoboDesktopInputMaxContents;
    out->maxListeners = RoboDesktopInputMaxListeners;
    out->cursorShapes = RoboInputCursorTypeCount;
    out->resizeZones = 8u;
    out->version = RoboInputVersionString;
    return RoboOk;
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

void DesktopInputForgetInit(DesktopInputForget *out)
{
    if (out) {
        MemSet(out, 0, sizeof(*out));
    }
}

Result DesktopInputForgetTarget(DesktopInput *input,
                                const InputTarget *target,
                                DesktopInputForget *out,
                                DesktopDiag *diag)
{
    if (out) {
        DesktopInputForgetInit(out);
    }
    if (!input || !target) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                               "a system and a target are required");
    }
    /*
     * Both halves, because a listener store and a session are two tables that can each hold a destroyed
     * target: the shell's own listeners are this layer's, and the focus and the captures are the model's.
     */
    if (out) {
        out->listenersRemoved = DesktopInputRemoveListeners(input, target);
        out->sessionTargetsReleased = InputSessionTargetReleased(&input->session, target);
        out->focusReleased = InputFocusManagerReleaseTarget(&input->session.focus, target);
    } else {
        (void)DesktopInputRemoveListeners(input, target);
        (void)InputSessionTargetReleased(&input->session, target);
        (void)InputFocusManagerReleaseTarget(&input->session.focus, target);
    }
    return RoboOk;
}

Result DesktopInputForgetWindow(DesktopInput *input,
                                DesktopWindowId window,
                                DesktopInputForget *out,
                                DesktopDiag *diag)
{
    InputSessionSweep sweep;
    InputTarget target;
    InputDiag idiag;
    Result status;

    if (out) {
        DesktopInputForgetInit(out);
    }
    if (!input) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument, "a system is required");
    }
    if (WindowIsNone(window)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboInvalidArgument,
                               "a window is required");
    }
    InputDiagReset(&idiag);
    InputSessionSweepInit(&sweep);

    /* 1. The window's own target: what a listener was registered on and what a pointer captured. */
    InputTargetInit(&target);
    if (InputTargetSetWindow(&target, AsWindowId(window), RoboInputSpaceScreen, &idiag) == RoboOk) {
        uint32_t listeners = DesktopInputRemoveListeners(input, &target);
        uint32_t released = InputSessionTargetReleased(&input->session, &target);

        if (out) {
            out->listenersRemoved = listeners;
            out->sessionTargetsReleased += released;
        }
    }

    /* 2. A move or a resize anchored on it: a gesture cannot outlive the window it is changing. */
    if (input->dragWindow == window) {
        InputDragEvent dragEvent;

        InputDragEventInit(&dragEvent);
        if (InputDragSessionIsActive(&input->session.drag)) {
            (void)InputDragSessionCancel(&input->session.drag, &dragEvent, &idiag);
            input->stats.dragsEnded++;
        }
        (void)InputDragSessionReset(&input->session.drag);
        input->dragWindow = RoboDesktopWindowIdNone;
        if (out) {
            out->dragCancelled = 1;
        }
    }
    if (input->resizeWindow == window) {
        input->resizeWindow = RoboDesktopWindowIdNone;
        input->resizeArea = RoboDesktopAreaNone;
    }
    GestureCursorClear(input);

    /* 3. The model's own window sweep: the captures held in it, its focus scope, the drag, the arena. */
    status = InputSessionWindowDeactivated(&input->session, AsWindowId(window), &sweep);
    if (status != RoboOk) {
        return ShellDiag(diag, &idiag, status, "the window's session state could not be released");
    }
    if (out) {
        out->capturesReleased = sweep.capturesReleased;
        out->focusReleased += sweep.focusReleased;
        out->keysReleased = sweep.keysReleased;
        out->gesturesReset = sweep.gesturesReset;
        if (sweep.dragCancelled) {
            out->dragCancelled = 1;
        }
    }
    {
        int scopes = InputFocusManagerReleaseWindow(&input->session.focus, AsWindowId(window));

        if (out && scopes > 0) {
            out->focusReleased += (uint32_t)scopes;
        }
    }

    /* 4. Its contents: a document tree about to be freed must not be reachable from here. */
    if (FindContentMutable(input, window)) {
        (void)DesktopInputClearContent(input, window, diag);
        if (out) {
            out->contentCleared = 1;
        }
    }
    return RoboOk;
}

Result GetDesktopInputLatency(const DesktopInput *input, DesktopInputLatency *out)
{
    if (!input || !out) {
        return RoboInvalidArgument;
    }
    MemSet(out, 0, sizeof(*out));
    out->dispatchLastMs = input->latency.dispatchLastMs;
    out->dispatchMaxMs = input->latency.dispatchMaxMs;
    out->dispatchSumMs = input->latency.dispatchSumMs;
    out->handlerLastMs = input->latency.handlerLastMs;
    out->handlerMaxMs = input->latency.handlerMaxMs;
    out->handlerSumMs = input->latency.handlerSumMs;
    out->frameLastMs = input->latency.frameLastMs;
    out->frameMaxMs = input->latency.frameMaxMs;
    out->frameSumMs = input->latency.frameSumMs;
    out->traced = input->latency.begunTotal;
    out->completed = input->latency.completedTotal;
    return RoboOk;
}

Result DesktopInputLatencyWrite(const DesktopInputLatency *latency,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                DesktopDiag *diag)
{
    int written;

    if (!latency || !buffer) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboInvalidArgument,
                               "a latency record and a buffer are required");
    }
    written = snprintf(buffer, capacity,
                       "latency traced=%llu completed=%llu "
                       "dispatch last=%llu max=%llu sum=%llu "
                       "handler last=%llu max=%llu sum=%llu "
                       "frame last=%llu max=%llu sum=%llu\n",
                       (unsigned long long)latency->traced,
                       (unsigned long long)latency->completed,
                       (unsigned long long)latency->dispatchLastMs,
                       (unsigned long long)latency->dispatchMaxMs,
                       (unsigned long long)latency->dispatchSumMs,
                       (unsigned long long)latency->handlerLastMs,
                       (unsigned long long)latency->handlerMaxMs,
                       (unsigned long long)latency->handlerSumMs,
                       (unsigned long long)latency->frameLastMs,
                       (unsigned long long)latency->frameMaxMs,
                       (unsigned long long)latency->frameSumMs);
    if (written < 0 || (size_t)written >= capacity) {
        if (outLength) {
            *outLength = 256u;
        }
        return RoboDesktopDiag(diag, RoboDesktopDomainDump, RoboNotSupported,
                               "the latency line did not fit");
    }
    if (outLength) {
        *outLength = (size_t)written;
    }
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
    /* A button held down is a transient thing even when the press resolved to no target: the pointer's own
     * state is what `DesktopInputResetTransient` is for, so quiescence has to see it. */
    if (input->desktop && input->desktop->pointer.pressed) {
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
                       "gesture moving=%d resizing=%d window=%llu zone=%s captured=%d drag=%s\n"
                       "input stats steps=%llu window=%llu translated=%llu dispatched=%llu "
                       "calls=%llu actions=%llu focus=%llu cursor=%llu shortcuts=%llu "
                       "commands=%llu unclaimed=%llu text=%llu clipboard=%llu "
                       "drags=%llu/%llu moves=%llu resizes=%llu connects=%llu disconnects=%llu\n",
                       RoboInputVersionString, input->live ? 1u : 0u,
                       (unsigned)InputSessionDeviceCount(&input->session),
                       (unsigned)DesktopInputListenerCount(input), (unsigned)queueStats.count,
                       (unsigned)queueStats.capacity, (unsigned long long)queueStats.dropped,
                       DesktopInputIsMovingWindow(input), DesktopInputIsResizingWindow(input),
                       (unsigned long long)DesktopInputGestureWindow(input),
                       DesktopAreaName(input->resizeArea),
                       InputPointerCaptureTarget(&input->session.captures, input->pointer) ? 1 : 0,
                       InputDragPhaseName(input->session.drag.phase),
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
                       (unsigned long long)input->stats.clipboardOps,
                       (unsigned long long)input->stats.dragsBegan,
                       (unsigned long long)input->stats.dragsEnded,
                       (unsigned long long)input->stats.windowMoves,
                       (unsigned long long)input->stats.windowResizes,
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
    /* The measurement, last, so a reader looking for "how long did that take" finds it after the state. */
    {
        DesktopInputLatency latency;
        char latencyLine[256];
        size_t latencyLength = 0u;

        if (GetDesktopInputLatency(input, &latency) == RoboOk &&
            DesktopInputLatencyWrite(&latency, latencyLine, sizeof(latencyLine), &latencyLength,
                                     diag) == RoboOk &&
            Append(input, buffer, capacity, &used, latencyLine, diag) != RoboOk) {
            if (outLength) {
                *outLength = RoboDesktopInputDumpMax;
            }
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = used;
    }
    return RoboOk;
}
