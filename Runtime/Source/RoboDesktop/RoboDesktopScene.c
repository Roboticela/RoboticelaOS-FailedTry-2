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
 *           a document `POINTER_UP` is posted into RoboJS as a `"click"`, then `DesktopScriptPump`
 *           drains the page's loop;
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
#include "RoboDesktop/RoboDesktopScript.h"
#include "RoboDesktopInternal.h"
#include "RoboMem/RoboMem.h"

#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowDisplay.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowMouse.h"

#include "Generated/DesktopPage.h"

#define SCENE_STYLE kDesktopPageStyle
#define SCENE_SCRIPT kDesktopPageScript

/** One application window the scene owns. */
typedef struct SceneApp {
    DesktopWindowId window;
    DesktopDocument *document;
    DesktopScript *script;
    const char *markup;
    const char *title;
} SceneApp;

struct DesktopScene {
    Desktop *desktop;
    SceneApp apps[RoboDesktopSceneAppCount];
    size_t appCount;
    /** Index of the focused / primary app for DesktopSceneWindow / DesktopSceneDocument. */
    size_t primary;
    /**
     * The shell's input system (Phase 13.66--13.77), owned here.
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
    return kDesktopPageMarkup;
}

const char *DesktopSceneStyle(void)
{
    return SCENE_STYLE;
}

size_t DesktopSceneMarkupLength(void)
{
    return SceneLength(kDesktopPageMarkup);
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
 * Build one application document into @p app, at the client size the shell computed for its window.
 */
static Result SceneBuildAppDocument(DesktopScene *scene, SceneApp *app, DesktopDiag *diag)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    GfxRect client;
    Result result;

    if (app->window == RoboDesktopWindowIdNone || !app->markup) {
        return RoboOk;
    }
    if (DesktopWindowClientBounds(scene->desktop, app->window, &client) != RoboOk) {
        return RoboNotFound;
    }
    if (client.width <= 0 || client.height <= 0) {
        return RoboOk;
    }
    DesktopDocumentConfigInit(&config);
    config.html = app->markup;
    config.css = SCENE_STYLE;
    config.width = client.width;
    config.height = client.height;
    config.face = scene->face;
    config.typography = scene->typography;

    result = DesktopDocumentBuild(&config, &document, diag);
    if (result != RoboOk) {
        return result;
    }
    DesktopScriptDestroy(app->script);
    app->script = NULL;
    DesktopDocumentDestroy(app->document);
    app->document = document;

    {
        DesktopScript *script = NULL;
        HtmlDocumentId html = DesktopDocumentHtml(document);

        result = DesktopScriptCreate(html, SCENE_SCRIPT, (size_t)-1, &script, diag);
        if (result != RoboOk) {
            return result;
        }
        app->script = script;
        /* Pick up class changes the page script made at evaluate (e.g. jsReady). */
        if (DesktopDocumentRefresh(document, diag) != RoboOk) {
            LogDebug("robo_desktop: document refresh after script attach declined");
        }
    }

    result = DesktopWindowSetContent(scene->desktop, app->window,
                                     DesktopDocumentDisplayList(document),
                                     DesktopDocumentFace(document), diag);
    if (result != RoboOk) {
        return result;
    }

    if (scene->input) {
        DesktopInputContent content;

        DesktopInputContentInit(&content);
        content.window = app->window;
        content.tree = DesktopDocumentTree(document);
        (void)DesktopInputSetContent(scene->input, &content, diag);
    }
    return RoboOk;
}

/** Build every live application document. */
static Result SceneBuildAllDocuments(DesktopScene *scene, DesktopDiag *diag)
{
    size_t index;
    Result result;

    for (index = 0u; index < scene->appCount; index++) {
        result = SceneBuildAppDocument(scene, &scene->apps[index], diag);
        if (result != RoboOk) {
            return result;
        }
    }
    return RoboOk;
}

/**
 * Refresh documents after a page-script mutation so attribute/class changes become pixels.
 */
static void SceneRefreshAfterScript(DesktopScene *scene, DesktopDiag *diag)
{
    size_t index;

    for (index = 0u; index < scene->appCount; index++) {
        SceneApp *app = &scene->apps[index];

        if (!app->document || app->window == RoboDesktopWindowIdNone) {
            continue;
        }
        if (DesktopDocumentRefresh(app->document, diag) != RoboOk) {
            continue;
        }
        (void)DesktopWindowSetContent(scene->desktop, app->window,
                                      DesktopDocumentDisplayList(app->document),
                                      DesktopDocumentFace(app->document), diag);
        if (scene->input) {
            DesktopInputContent content;

            DesktopInputContentInit(&content);
            content.window = app->window;
            content.tree = DesktopDocumentTree(app->document);
            (void)DesktopInputSetContent(scene->input, &content, diag);
        }
    }
    scene->needsPaint = 1;
    scene->needsCursorPaint = 0;
}

/**
 * Place the two product windows inside the work area — Overview left, Input Lab raised and offset.
 */
static void ScenePlaceWindow(const Desktop *desktop,
                             int32_t offsetX,
                             int32_t offsetY,
                             int32_t width,
                             int32_t height,
                             DesktopWindowDesc *desc)
{
    GfxRect work = DesktopWorkArea(desktop);
    int32_t maxW = work.width - RoboDesktopSceneMargin;
    int32_t maxH = work.height - RoboDesktopSceneMargin;

    if (maxW < 160) {
        maxW = work.width > 0 ? work.width : 320;
    }
    if (maxH < 120) {
        maxH = work.height > 0 ? work.height : 240;
    }
    if (width > maxW) {
        width = maxW;
    }
    if (height > maxH) {
        height = maxH;
    }
    if (width < 160) {
        width = maxW < 160 ? maxW : 160;
    }
    if (height < 120) {
        height = maxH < 120 ? maxH : 120;
    }
    desc->x = work.x + RoboDesktopSceneMargin + offsetX;
    desc->y = work.y + RoboDesktopSceneMargin + offsetY;
    if (desc->x + width > work.x + work.width) {
        desc->x = work.x + (work.width > width ? work.width - width : 0);
    }
    if (desc->y + height > work.y + work.height) {
        desc->y = work.y + (work.height > height ? work.height - height : 0);
    }
    desc->width = width;
    desc->height = height;
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
    Result result;
    size_t index;

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
    scene->appCount = 0u;
    scene->primary = 1u; /* Input Lab is focused, matching the session model. */
    for (index = 0u; index < RoboDesktopSceneAppCount; index++) {
        scene->apps[index].window = RoboDesktopWindowIdNone;
        scene->apps[index].document = NULL;
        scene->apps[index].script = NULL;
        scene->apps[index].markup = NULL;
        scene->apps[index].title = NULL;
    }
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
    shellConfig.statusText = config->statusText ? config->statusText : "Ready";
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

    /* Overview */
    scene->apps[0].markup = kDesktopPageMarkup;
    scene->apps[0].title = RoboDesktopSceneWindowTitle;
    DesktopWindowDescInit(&desc);
    desc.title = scene->apps[0].title;
    ScenePlaceWindow(scene->desktop, 0, 0, 420, 520, &desc);
    result = DesktopWindowAdd(scene->desktop, &desc, &scene->apps[0].window, diag);
    if (result != RoboOk) {
        DesktopSceneClose(scene);
        return result;
    }
    scene->appCount = 1u;

    /* Input Lab */
    scene->apps[1].markup = kDesktopDocInputLabMarkup;
    scene->apps[1].title = RoboDesktopSceneInputLabTitle;
    DesktopWindowDescInit(&desc);
    desc.title = scene->apps[1].title;
    ScenePlaceWindow(scene->desktop, 96, 48, 560, 560, &desc);
    result = DesktopWindowAdd(scene->desktop, &desc, &scene->apps[1].window, diag);
    if (result != RoboOk) {
        DesktopSceneClose(scene);
        return result;
    }
    scene->appCount = 2u;

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

        InputDiagReset(&idiag);
        InputTargetInit(&listenerTarget);
        if (InputTargetSetWindow(&listenerTarget, (WindowId)scene->apps[scene->primary].window,
                                 RoboInputSpaceScreen, &idiag) == RoboOk) {
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

    result = SceneBuildAllDocuments(scene, diag);
    if (result != RoboOk) {
        DesktopSceneClose(scene);
        return result;
    }
    (void)DesktopWindowFocus(scene->desktop, scene->apps[scene->primary].window);

    *out = scene;
    return RoboOk;
}

void DesktopSceneClose(DesktopScene *scene)
{
    size_t index;

    if (!scene) {
        return;
    }
    DesktopInputDestroy(scene->input);
    scene->input = NULL;
    for (index = 0u; index < scene->appCount; index++) {
        DesktopScriptDestroy(scene->apps[index].script);
        scene->apps[index].script = NULL;
        DesktopDocumentDestroy(scene->apps[index].document);
        scene->apps[index].document = NULL;
        scene->apps[index].window = RoboDesktopWindowIdNone;
    }
    scene->appCount = 0u;
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
    if (!scene || scene->appCount == 0u || scene->primary >= scene->appCount) {
        return RoboDesktopWindowIdNone;
    }
    return scene->apps[scene->primary].window;
}

const DesktopDocument *DesktopSceneDocument(const DesktopScene *scene)
{
    if (!scene || scene->appCount == 0u || scene->primary >= scene->appCount) {
        return NULL;
    }
    return scene->apps[scene->primary].document;
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
    SceneApp *primary;
    size_t index;

    if (!scene->input) {
        return;
    }
    DesktopInputStepInit(&inputStep);
    (void)GetDesktopInputStep(scene->input, waitMs, &inputStep, diag);
    step->events += inputStep.windowEvents;
    step->pointerActions += inputStep.actions;

    primary = (scene->primary < scene->appCount) ? &scene->apps[scene->primary] : NULL;

    /* Prefer the focused window's script; fall back to any app that owns the click target. */
    if (inputStep.documentClick) {
        SceneApp *clicked = NULL;

        for (index = 0u; index < scene->appCount; index++) {
            if (scene->apps[index].window == DesktopWindowFocused(scene->desktop)) {
                clicked = &scene->apps[index];
                scene->primary = index;
                break;
            }
        }
        if (!clicked) {
            clicked = primary;
        }
        if (clicked && clicked->script && clicked->document) {
            (void)DesktopScriptPostClick(clicked->script,
                                         DesktopDocumentTree(clicked->document),
                                         inputStep.documentClickNode,
                                         inputStep.documentClickX,
                                         inputStep.documentClickY,
                                         diag);
            (void)DesktopScriptPump(clicked->script, 16u, diag);
            SceneRefreshAfterScript(scene, diag);
        }
    } else if (primary && primary->script) {
        (void)DesktopScriptPump(primary->script, 16u, diag);
    }

    if (inputStep.closeRequested) {
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
    } else if (scene->showCursor && !scene->needsPaint &&
               (inputStep.cursorChanges > 0u || inputStep.pointerMoves > 0u)) {
        scene->needsCursorPaint = 1;
    }
}

/** Retire application windows that asked to close; finish when none remain. */
static void SceneRetireClosedWindow(DesktopScene *scene, DesktopSceneStep *step)
{
    size_t index;
    size_t live = 0u;

    for (index = 0u; index < scene->appCount; index++) {
        SceneApp *app = &scene->apps[index];
        DesktopWindowState state = RoboDesktopWindowOpen;

        if (app->window == RoboDesktopWindowIdNone) {
            continue;
        }
        if (GetDesktopWindowState(scene->desktop, app->window, &state) != RoboOk ||
            state != RoboDesktopWindowClosing) {
            live++;
            continue;
        }
        DesktopScriptDestroy(app->script);
        app->script = NULL;
        DesktopDocumentDestroy(app->document);
        app->document = NULL;
        (void)DesktopWindowDestroy(scene->desktop, app->window);
        app->window = RoboDesktopWindowIdNone;
        scene->needsPaint = 1;
    }
    if (live == 0u) {
        scene->finished = 1;
        step->closeRequested = 1;
        return;
    }
    if (scene->apps[scene->primary].window == RoboDesktopWindowIdNone) {
        for (index = 0u; index < scene->appCount; index++) {
            if (scene->apps[index].window != RoboDesktopWindowIdNone) {
                scene->primary = index;
                (void)DesktopWindowFocus(scene->desktop, scene->apps[index].window);
                break;
            }
        }
    }
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
            if (SceneBuildAllDocuments(scene, diag) == RoboOk) {
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
