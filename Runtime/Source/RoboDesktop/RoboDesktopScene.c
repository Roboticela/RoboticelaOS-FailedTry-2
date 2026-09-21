/*
 * RoboDesktop's checkpoint scene (Phase 11.13) — see RoboDesktopScene.h.
 *
 * Three things live here and nothing else: the checkpoint's own words (the markup, the author sheet, the
 * window's title), the order a scene is opened and released in, and the iteration a launcher drives.
 *
 * The iteration is the part worth reading. Every frame of a driven desktop runs the same four steps, and
 * they are in this order because each depends on the one before it:
 *
 *   pump    the window queue, bounded by the wait the caller named, and take what it holds;
 *   hand    the pointer to the shell — `DesktopObservePointer` is the whole of this build's input
 *           integration (RoboDesktop.h §16), and a scene must not invent more of one;
 *   follow  the destination: `DesktopSyncSize` matches the screen to the window's *physical* client
 *           size, which is how a user's resize reaches the shell;
 *   paint   and present, once, when there is something new to show.
 *
 * A scene keeps no clock and no frame counter: `needsPaint` is set by the things that change a picture
 * (a resize, an event, the caller's own `invalidate`) and cleared by the frame that answers them. So a
 * launcher that nothing happens to paints one frame and then waits, and a launcher under a mouse paints
 * exactly as often as it observes.
 */

#include "RoboDesktop/RoboDesktopScene.h"

#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"

#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowDisplay.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowMouse.h"

#include "Generated/DesktopPage.h"

#define SCENE_MARKUP kDesktopPageMarkup
#define SCENE_STYLE kDesktopPageStyle

struct DesktopScene {
    Desktop *desktop;
    DesktopDocument *document;
    DesktopWindowId window;
    /** Borrowed: the caller's face, kept only so a rebuilt document is built with the same one. */
    FontFace *face;
    /** Non-zero when the next step must paint a frame. */
    int needsPaint;
    /** Non-zero once the checkpoint is finished: the shell's window is gone or its native window closed. */
    int finished;
    /** Non-zero when mouse motion must repaint so the software cursor can move. */
    int showCursor;
};

/* ------------------------------------------------------------------ */
/* Own string helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * This file is compiled into the guest program, which is freestanding: `string.h` is not available to it
 * and a `strlen` that linked would be a hosted contract this module does not own. Two one-line readers are
 * cheaper than the dependency, and the same reason (and the same shape) as `RoboDesktopText.c`.
 */
static size_t SceneLength(const char *text)
{
    size_t length = 0u;

    if (!text) {
        return 0u;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

/* ------------------------------------------------------------------ */
/* The scene's words                                                   */
/* ------------------------------------------------------------------ */

const char *DesktopSceneMarkup(void)
{
    return SCENE_MARKUP;
}

const char *DesktopSceneStyle(void)
{
    return SCENE_STYLE;
}

size_t DesktopSceneMarkupLength(void)
{
    return SceneLength(SCENE_MARKUP);
}

void DesktopSceneConfigInit(DesktopSceneConfig *config)
{
    if (!config) {
        return;
    }
    config->width = RoboDesktopSceneDefaultWidth;
    config->height = RoboDesktopSceneDefaultHeight;
    config->face = NULL;
    config->RoboCreateWindow = 1;
    config->windowTitle = NULL;
    config->statusText = NULL;
    config->identityText = NULL;
    config->applicationsText = NULL;
    config->showCursor = 0;
}

void DesktopSceneStepInit(DesktopSceneStep *step)
{
    if (!step) {
        return;
    }
    step->events = 0u;
    step->pointerActions = 0u;
    step->resized = 0;
    step->rebuilt = 0;
    step->painted = 0;
    step->presented = 0;
    step->windowPresented = 0;
    step->closeRequested = 0;
    DesktopFrameRecordInit(&step->frame);
    RenderPresentRecordInit(&step->present);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

/**
 * Build the document the scene's window shows, at the client size the *shell* computed.
 *
 * The client rectangle is asked for rather than derived, and that is the same rule the desktop's own suite
 * follows: a document built for a size the shell did not compute would be composited at one size and laid
 * out for another, which is the defect this call exists to make unexpressible.
 *
 * The old document is released **before** the new contents are attached, because a window borrows a display
 * list: attaching the new list while the old document still owned nothing would be fine, but releasing the
 * old one *after* attaching a new list from it is the order that would leave a window pointing at freed
 * memory.
 */
static Result SceneBuildDocument(DesktopScene *scene, DesktopDiag *diag)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    GfxRect client;
    Result result;

    if (DesktopWindowClientBounds(scene->desktop, scene->window, &client) != RoboOk) {
        return RoboNotFound;
    }
    if (client.width <= 0 || client.height <= 0) {
        /* A window grown to nothing shows nothing; that is not a document that failed to build. */
        return RoboOk;
    }
    DesktopDocumentConfigInit(&config);
    config.html = SCENE_MARKUP;
    config.css = SCENE_STYLE;
    config.width = client.width;
    config.height = client.height;
    config.face = scene->face;

    result = DesktopDocumentBuild(&config, &document, diag);
    if (result != RoboOk) {
        return result;
    }
    DesktopDocumentDestroy(scene->document);
    scene->document = document;
    return DesktopWindowSetContent(scene->desktop, scene->window,
                                          DesktopDocumentDisplayList(document),
                                          scene->face, diag);
}

/**
 * The window's outer rectangle: the work area less a margin on every side.
 *
 * Not the whole work area, and not the screen: a window that filled the screen would hide the panel's
 * relationship to the surface behind it, which is one of the three depth levels the theme is arranged
 * around, and a checkpoint that hid the thing it demonstrates would be a checkpoint nobody could read. A
 * screen too small for the margin degrades to the work area itself rather than to a negative rectangle.
 */
static void SceneWindowBounds(const Desktop *desktop, int32_t *outWidth, int32_t *outHeight)
{
    GfxRect work = DesktopWorkArea(desktop);
    int32_t width = work.width - RoboDesktopSceneMargin * 2;
    int32_t height = work.height - RoboDesktopSceneMargin * 2;

    if (width < 1) {
        width = work.width;
    }
    if (height < 1) {
        height = work.height;
    }
    *outWidth = width;
    *outHeight = height;
}

Result DesktopSceneOpen(const DesktopSceneConfig *config,
                                      DesktopScene **out,
                                      DesktopDiag *diag)
{
    DesktopConfig shellConfig;
    DesktopWindowDesc desc;
    DesktopScene *scene;
    int32_t width;
    int32_t height;
    int32_t windowWidth;
    int32_t windowHeight;
    Result result;

    if (!config || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    width = (config->width > 0) ? config->width : RoboDesktopSceneDefaultWidth;
    height = (config->height > 0) ? config->height : RoboDesktopSceneDefaultHeight;

    scene = (DesktopScene *)Malloc(sizeof(*scene));
    if (!scene) {
        return RoboOutOfMemory;
    }
    scene->desktop = NULL;
    scene->document = NULL;
    scene->window = RoboDesktopWindowIdNone;
    scene->face = config->face;
    scene->needsPaint = 1;
    scene->finished = 0;
    scene->showCursor = config->showCursor ? 1 : 0;

    DesktopConfigInit(&shellConfig);
    shellConfig.width = width;
    shellConfig.height = height;
    shellConfig.face = config->face;
    shellConfig.RoboCreateWindow = config->RoboCreateWindow ? 1 : 0;
    shellConfig.windowTitle = config->windowTitle;
    shellConfig.statusText = config->statusText;
    shellConfig.showCursor = scene->showCursor;
    if (config->identityText) {
        shellConfig.identityText = config->identityText;
    }
    if (config->applicationsText) {
        shellConfig.applicationsText = config->applicationsText;
    }

    result = DesktopCreate(&shellConfig, &scene->desktop, diag);
    if (result != RoboOk) {
        DesktopSceneClose(scene);
        return result;
    }

    SceneWindowBounds(scene->desktop, &windowWidth, &windowHeight);
    DesktopWindowDescInit(&desc);
    desc.title = RoboDesktopSceneWindowTitle;
    /* Negative origin: the shell places the window (a third down the work area, centred horizontally),
     * which is what keeps the checkpoint's composition the shell's business rather than this file's. */
    desc.x = -1;
    desc.y = -1;
    desc.width = windowWidth;
    desc.height = windowHeight;

    result = DesktopWindowAdd(scene->desktop, &desc, &scene->window, diag);
    if (result != RoboOk) {
        DesktopSceneClose(scene);
        return result;
    }

    result = SceneBuildDocument(scene, diag);
    if (result != RoboOk) {
        DesktopSceneClose(scene);
        return result;
    }
    (void)DesktopWindowFocus(scene->desktop, scene->window);

    *out = scene;
    return RoboOk;
}

void DesktopSceneClose(DesktopScene *scene)
{
    if (!scene) {
        return;
    }
    /* Reverse of the borrowing order: the document's list is borrowed by the window, so the window goes
     * first, then the document, then the shell that owned neither. */
    DesktopDocumentDestroy(scene->document);
    scene->document = NULL;
    DesktopDestroy(scene->desktop);
    scene->desktop = NULL;
    RoboFree(scene);
}

int DesktopSceneIsLive(const DesktopScene *scene)
{
    return scene != NULL && scene->desktop != NULL;
}

Desktop *DesktopSceneDesktop(DesktopScene *scene)
{
    return scene ? scene->desktop : NULL;
}

DesktopWindowId DesktopSceneWindow(const DesktopScene *scene)
{
    return scene ? scene->window : RoboDesktopWindowIdNone;
}

const DesktopDocument *DesktopSceneDocument(const DesktopScene *scene)
{
    return scene ? scene->document : NULL;
}

void DesktopSceneInvalidate(DesktopScene *scene)
{
    if (scene) {
        scene->needsPaint = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Stepping                                                            */
/* ------------------------------------------------------------------ */

/** Hand one pointer state to the shell, counting it only when the shell acted on it. */
static void SceneObservePointer(DesktopScene *scene,
                                  int32_t x,
                                  int32_t y,
                                  int pressed,
                                  DesktopSceneStep *step,
                                  DesktopDiag *diag)
{
    DesktopStats before;
    DesktopStats after;

    before.pointerActions = 0u;
    after.pointerActions = 0u;
    (void)GetDesktopStats(scene->desktop, &before);
    (void)DesktopObservePointer(scene->desktop, MakeGfxPoint(x, y), pressed, diag);
    (void)GetDesktopStats(scene->desktop, &after);
    if (after.pointerActions > before.pointerActions) {
        step->pointerActions += after.pointerActions - before.pointerActions;
    }
}

/**
 * Take everything the window queue holds and act on it.
 *
 * Pointer motion and buttons go to the shell. Escape finishes the scene without inventing application
 * key routing (`inputDelivery` stays 0): it is the launcher's way to leave, the same as closing the
 * native window.
 */
static void SceneDrainEvents(DesktopScene *scene,
                               DesktopSceneStep *step,
                               DesktopDiag *diag)
{
    WindowEvent event;

    while (WindowEventPoll(&event) == RoboOk) {
        step->events++;
        switch (event.type) {
        case RoboWindowEventCloseRequested:
        case RoboWindowEventClosed:
            /* The shell's own window is closing: the checkpoint is over, whatever the app window is doing. */
            scene->finished = 1;
            step->closeRequested = 1;
            break;
        case RoboWindowEventResized:
        case RoboWindowEventScaleChanged:
            scene->needsPaint = 1;
            break;
        case RoboWindowEventKeyDown:
            if (event.key.logicalKey == RoboLogicalKeyEscape) {
                scene->finished = 1;
                step->closeRequested = 1;
                LogDebug("robo_desktop: escape finished the scene");
            }
            break;
        case RoboWindowEventMouseMove:
            SceneObservePointer(scene, event.mouse.clientX, event.mouse.clientY, 0, step, diag);
            if (scene->showCursor) {
                scene->needsPaint = 1;
            }
            break;
        case RoboWindowEventMouseButtonDown:
            if (event.mouse.button == RoboMouseButtonLeft) {
                SceneObservePointer(scene, event.mouse.clientX, event.mouse.clientY, 1, step, diag);
                if (scene->showCursor) {
                    scene->needsPaint = 1;
                }
            }
            break;
        case RoboWindowEventMouseButtonUp:
            if (event.mouse.button == RoboMouseButtonLeft) {
                SceneObservePointer(scene, event.mouse.clientX, event.mouse.clientY, 0, step, diag);
                if (scene->showCursor) {
                    scene->needsPaint = 1;
                }
            }
            break;
        case RoboWindowEventMouseWheel:
            SceneObservePointer(scene, event.mouse.clientX, event.mouse.clientY, 0, step, diag);
            break;
        default:
            break;
        }
    }
}

/** The application window's close button, honoured: the shell's window goes and the checkpoint is over. */
static void SceneRetireClosedWindow(DesktopScene *scene, DesktopSceneStep *step)
{
    DesktopWindowState state = RoboDesktopWindowOpen;

    if (scene->window == RoboDesktopWindowIdNone) {
        return;
    }
    if (GetDesktopWindowState(scene->desktop, scene->window, &state) != RoboOk ||
        state != RoboDesktopWindowClosing) {
        return;
    }
    DesktopDocumentDestroy(scene->document);
    scene->document = NULL;
    (void)DesktopWindowDestroy(scene->desktop, scene->window);
    scene->window = RoboDesktopWindowIdNone;
    scene->finished = 1;
    scene->needsPaint = 1;
    step->closeRequested = 1;
}

Result GetDesktopSceneStep(DesktopScene *scene,
                                      uint32_t waitMs,
                                      DesktopSceneStep *outStep,
                                      DesktopDiag *diag)
{
    DesktopSceneStep step;
    int resized = 0;
    Result result;

    if (!scene || !scene->desktop) {
        return RoboInvalidArgument;
    }
    DesktopSceneStepInit(&step);

    /*
     * The window queue is drained only when RoboWindow came up. A desktop without it is a desktop this
     * module supports (`RoboDesktop.h` reads RoboWindow rather than requiring it), and a scene that pumped
     * an uninitialised module's loop would be the one place that assumed differently.
     */
    if (WindowIsInitialized()) {
        (void)WindowEventLoopRunOnce(waitMs);
        SceneDrainEvents(scene, &step, diag);
        SceneRetireClosedWindow(scene, &step);
    }

    if (DesktopAttachedWindow(scene->desktop) != RoboWindowIdNone) {
        result = DesktopSyncSize(scene->desktop, &resized, diag);
        if (result == RoboOk && resized) {
            step.resized = 1;
            scene->needsPaint = 1;
            /* A document is laid out for a viewport: a resized client area gets a rebuilt picture, not a
             * stretched one. */
            if (scene->window != RoboDesktopWindowIdNone &&
                SceneBuildDocument(scene, diag) == RoboOk) {
                step.rebuilt = 1;
            }
        }
    }

    if (scene->needsPaint) {
        result = DesktopRender(scene->desktop, &step.frame, diag);
        if (result != RoboOk) {
            if (outStep) {
                *outStep = step;
            }
            return result;
        }
        scene->needsPaint = 0;
        step.painted = 1;

        if (DesktopAttachedWindow(scene->desktop) != RoboWindowIdNone) {
            result = DesktopPresent(scene->desktop, &step.present, &step.windowPresented, diag);
            if (result != RoboOk) {
                if (outStep) {
                    *outStep = step;
                }
                return result;
            }
            step.presented = 1;
        }
    }

    if (outStep) {
        *outStep = step;
    }
    return RoboOk;
}
