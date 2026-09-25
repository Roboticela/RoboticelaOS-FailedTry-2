/*
 * RoboDesktop's checkpoint scene (Phase 11.13) — see RoboDesktopScene.h.
 *
 * Three things live here and nothing else: the checkpoint's own words (the markup, the author sheet, the
 * window's title), the order a scene is opened and released in, and the iteration a launcher drives.
 *
 * The iteration is the part worth reading. Every frame of a driven desktop runs the same four steps, and
 * they are in this order because each depends on the one before it:
 *
 *   pump    the window queue, bounded by the wait the caller named, through `GetDesktopInputStep`
 *           (Phase 13.76): the queue goes through the RoboInput bridge, into RoboInput's own queue, out
 *           through the dispatcher the shell supplies the three providers for, and lands as an action;
 *   follow  the destination: `DesktopSyncSize` matches the screen to the window's *physical* client
 *           size, which is how a user's resize reaches the shell;
 *   paint   and present, once, when there is something new to show.
 *
 * Since Phase 13.66 the pointer is no longer handed to the shell by *this* file: the shell owns two
 * listeners (raise or close on a press, end a drag on its release) and this scene owns one more, and all
 * three are reached through RoboInput's dispatcher. That is what makes "the shell consumes RoboInput" true
 * rather than aspirational, and it is why `DesktopObservePointer` — the direct door that came before it —
 * survives only for a caller that has no input system to build (and for the suite that asserts its own two
 * decisions in isolation).
 *
 * A scene keeps no clock and no frame counter: `needsPaint` is set by the things that change a picture
 * (a resize, an event, the caller's own `invalidate`) and cleared by the frame that answers them. So a
 * launcher that nothing happens to paints one frame and then waits, and a launcher under a mouse paints
 * exactly as often as it observes.
 */

#include "RoboDesktop/RoboDesktopScene.h"

#include "RoboBase/RoboLog.h"
#include "RoboDesktop/RoboDesktopInput.h"
#include "RoboDesktopInternal.h"
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
    /**
     * The shell's input system (Phase 13.66--13.77), owned here.
     *
     * Owned rather than borrowed because a scene *is* the thing that drives the shell: a launcher that
     * created the system itself would be publishing the same join the module already publishes, and two
     * launchers of one scene are the reason this file exists at all.
     */
    DesktopInput *input;
    /** Borrowed: the caller's face, kept only so a rebuilt document is built with the same one. */
    FontFace *face;
    /** Borrowed: the caller's typography resolver (Phase 12.3), or NULL for the single-face binding. */
    TypographyResolver *typography;
    /** Non-zero when the next step must paint a full shell frame. */
    int needsPaint;
    /**
     * Non-zero when only the software cursor moved: a region restore/draw +
     * dirty present, not a chrome, AA text, or window re-raster.
     */
    int needsCursorPaint;
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
    config->typography = NULL;
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
    config.typography = scene->typography;

    result = DesktopDocumentBuild(&config, &document, diag);
    if (result != RoboOk) {
        return result;
    }
    DesktopDocumentDestroy(scene->document);
    scene->document = document;
    /*
     * The window's contents are rasterised with **the face the document was measured with** rather than
     * with the scene's own (Phase 12.3, §13): `DesktopDocumentFace` returns the face the layout pass
     * actually read metrics from, so a resolver that chose a different face than the caller expected
     * cannot produce a run whose width was computed from one face and drawn with another. It is the same
     * object the caller passed when there is no resolver.
     */
    result = DesktopWindowSetContent(scene->desktop, scene->window,
                                            DesktopDocumentDisplayList(document),
                                            DesktopDocumentFace(document), diag);
    if (result != RoboOk) {
        return result;
    }

    /*
     * Tell the input system what this window's contents are (Phase 13.68/13.70): the document's own layout
     * tree, so a press in the client area resolves to the **node** under it through `PaintHitTest` rather
     * than to the window. Re-registered on every rebuild, because a rebuild is a new tree and the old one
     * is about to be freed.
     */
    if (scene->input) {
        DesktopInputContent content;

        DesktopInputContentInit(&content);
        content.window = scene->window;
        content.tree = DesktopDocumentTree(document);
        (void)DesktopInputSetContent(scene->input, &content, diag);
    }
    return RoboOk;
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

static Result SceneOnKeyDown(void *userdata, const InputEvent *event, InputEventContext *context);

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
    scene->input = NULL;
    scene->face = config->face;
    scene->typography = config->typography;
    scene->needsPaint = 1;
    scene->needsCursorPaint = 0;
    scene->finished = 0;
    scene->showCursor = config->showCursor ? 1 : 0;

    DesktopConfigInit(&shellConfig);
    shellConfig.width = width;
    shellConfig.height = height;
    shellConfig.face = config->face;
    shellConfig.typography = config->typography;
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

    /*
     * The input system (Phase 13.66--13.77), built **after** the window exists because its route geometry
     * and the shell's window identity both come from that window, and before the first document because the
     * document's tree is what a client-area press is resolved against.
     */
    {
        DesktopInputConfig inputConfig;
        InputListener listener;
        InputTarget listenerTarget;
        InputDiag idiag;

        DesktopInputConfigInit(&inputConfig);
        inputConfig.window = DesktopAttachedWindow(scene->desktop);
        result = DesktopInputCreate(scene->desktop, &inputConfig, &scene->input, diag);
        if (result != RoboOk) {
            DesktopSceneClose(scene);
            return result;
        }

        /* The scene's own listener, on the window it will be handed events for. */
        InputDiagReset(&idiag);
        InputTargetInit(&listenerTarget);
        if (InputTargetSetWindow(&listenerTarget, (WindowId)scene->window, RoboInputSpaceScreen,
                                 &idiag) == RoboOk) {
            MemSet(&listener, 0, sizeof(listener));
            listener.target = listenerTarget;
            listener.phase = RoboInputPhaseTarget;
            listener.passive = 0;
            listener.removed = 0;
            listener.callback = SceneOnKeyDown;
            listener.userdata = scene;
            (void)DesktopInputAddListener(scene->input, &listener, diag);
        }
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
    /* Reverse of the borrowing order: the input system names the shell's windows (so it goes first), the
     * document's list is borrowed by the window (so the window goes before the document), and the shell owned
     * neither. */
    DesktopInputDestroy(scene->input);
    scene->input = NULL;
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
        scene->needsCursorPaint = 0;
        if (scene->desktop) {
            /*
             * Recompose chrome and window *frames*, but leave contentDirty alone:
             * contentRuns counts display-list raster work, and an unchanged document
             * must stay cached so two forced paints of one scene can still match.
             */
            DesktopComposeInvalidateAll(scene->desktop);
            {
                size_t index;

                for (index = 0u; index < scene->desktop->windowCount; index++) {
                    scene->desktop->windows[index].frameDirty = 1;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Stepping                                                            */
/* ------------------------------------------------------------------ */

/** Non-zero when the shell has something the next frame must redraw. */
static int SceneDesktopNeedsPaint(const Desktop *desktop)
{
    size_t index;

    if (!desktop) {
        return 0;
    }
    if (desktop->wallpaperDirty || desktop->panelDirty || desktop->windowOrderDirty) {
        return 1;
    }
    for (index = 0u; index < desktop->windowCount; index++) {
        if (desktop->windows[index].contentDirty || desktop->windows[index].frameDirty) {
            return 1;
        }
    }
    return 0;
}

/**
 * The scene's own input listener (Phase 13.66).
 *
 * Two things, and both are the *fixture's* rather than the shell's: `Escape` finishes the checkpoint, and
 * any other key writes its name into the panel status so a run under a keyboard shows what was pressed.
 *
 * It is a listener rather than a branch on a `WindowEvent` because that is what the model makes a
 * consumer: the shell raises and closes, this scene reports and leaves, and neither of them reads the
 * window queue. A press never rewrites the HUD — reshaping the panel label on every click was what made
 * the frame freeze under the cursor — so a pointer event is ignored here by construction.
 */
static Result SceneOnKeyDown(void *userdata, const InputEvent *event, InputEventContext *context)
{
    DesktopScene *scene = (DesktopScene *)userdata;
    char status[RoboDesktopStatusMax];
    const char *name;
    unsigned index = 0u;
    unsigned n;
    InputModifiers mods;

    (void)context;
    if (!scene || !event || event->type != RoboInputEventKeyDown) {
        return RoboOk;
    }
    if (event->payload.keyboard.logical == RoboLogicalKeyEscape) {
        scene->finished = 1;
        LogDebug("robo_desktop: escape finished the scene");
        return RoboOk;
    }
    name = LogicalKeyName(event->payload.keyboard.logical);
    if (!name) {
        name = "?";
    }
    status[index++] = 'k';
    status[index++] = 'e';
    status[index++] = 'y';
    status[index++] = ' ';
    for (n = 0u; name[n] != 0 && index + 1u < sizeof(status); n++) {
        status[index++] = name[n];
    }
    mods = event->modifiers;
    if ((mods & RoboKeyModShift) != 0u && index + 3u < sizeof(status)) {
        status[index++] = ' ';
        status[index++] = 'S';
    }
    if ((mods & RoboKeyModCtrl) != 0u && index + 3u < sizeof(status)) {
        status[index++] = ' ';
        status[index++] = 'C';
    }
    if ((mods & RoboKeyModAlt) != 0u && index + 3u < sizeof(status)) {
        status[index++] = ' ';
        status[index++] = 'A';
    }
    if ((mods & RoboKeyModMeta) != 0u && index + 3u < sizeof(status)) {
        status[index++] = ' ';
        status[index++] = 'M';
    }
    status[index] = 0;
    (void)DesktopSetStatus(scene->desktop, status, (DesktopDiag *)NULL);
    return RoboOk;
}

/**
 * One iteration of the input half of the frame (Phase 13.76).
 *
 * The window queue goes through RoboInput's bridge into its own queue, comes out through the dispatcher the
 * shell supplied three providers for, and lands as a *decision* — a raise, a close, a focus change, a
 * command, a key report. This file only translates the record into the two things a frame cares about: "is
 * the checkpoint over" and "must something be repainted".
 */
static void SceneStepInput(DesktopScene *scene,
                             uint32_t waitMs,
                             DesktopSceneStep *step,
                             DesktopDiag *diag)
{
    DesktopInputStep inputStep;

    if (!scene->input) {
        return;
    }
    DesktopInputStepInit(&inputStep);
    (void)GetDesktopInputStep(scene->input, waitMs, &inputStep, diag);
    step->events += inputStep.windowEvents;
    step->pointerActions += inputStep.actions;

    if (inputStep.closeRequested) {
        /* The shell's own window is closing: the checkpoint is over, whatever the app window is doing. */
        scene->finished = 1;
        step->closeRequested = 1;
    }
    if (inputStep.resized || inputStep.scaleChanged) {
        scene->needsPaint = 1;
        scene->needsCursorPaint = 0;
    }
    if (scene->finished) {
        step->closeRequested = 1;
    }
    if (SceneDesktopNeedsPaint(scene->desktop) && !scene->needsPaint) {
        scene->needsPaint = 1;
        scene->needsCursorPaint = 0;
    } else if (inputStep.cursorChanges > 0u && scene->showCursor && !scene->needsPaint) {
        scene->needsCursorPaint = 1;
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
        /*
         * The wait is the *scene's* to own: it is skipped when something must be painted, because a frame
         * that is owed must not be delayed by a sleep nobody needs. `GetDesktopInputStep` polls once with a
         * zero wait regardless, so a pending IRQ is never held behind the idle sleep.
         */
        SceneStepInput(scene, (scene->needsPaint || scene->needsCursorPaint) ? 0u : waitMs, &step, diag);
        SceneRetireClosedWindow(scene, &step);
    }

    if (DesktopAttachedWindow(scene->desktop) != RoboWindowIdNone) {
        result = DesktopSyncSize(scene->desktop, &resized, diag);
        if (result == RoboOk && resized) {
            step.resized = 1;
            scene->needsPaint = 1;
            scene->needsCursorPaint = 0;
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
        scene->needsCursorPaint = 0;
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
    } else if (scene->needsCursorPaint) {
        GfxRect damage = GfxRectEmpty();

        result = DesktopRepaintCursor(scene->desktop, &damage, diag);
        if (result == RoboNotSupported) {
            /* No prior shell frame yet — fall through to a full paint. */
            scene->needsPaint = 1;
            scene->needsCursorPaint = 0;
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
                result = DesktopPresent(scene->desktop, &step.present, &step.windowPresented,
                                              diag);
                if (result != RoboOk) {
                    if (outStep) {
                        *outStep = step;
                    }
                    return result;
                }
                step.presented = 1;
            }
        } else if (result != RoboOk) {
            if (outStep) {
                *outStep = step;
            }
            return result;
        } else {
            scene->needsCursorPaint = 0;
            step.painted = 1;
            if (DesktopAttachedWindow(scene->desktop) != RoboWindowIdNone &&
                damage.width > 0 && damage.height > 0) {
                result = DesktopPresentDamage(scene->desktop, damage, &step.present,
                                                     &step.windowPresented, diag);
                if (result != RoboOk) {
                    if (outStep) {
                        *outStep = step;
                    }
                    return result;
                }
                step.presented = 1;
            }
        }
    }

    if (outStep) {
        *outStep = step;
    }
    return RoboOk;
}
