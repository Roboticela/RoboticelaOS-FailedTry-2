/*
 * RoboDesktop's checkpoint scene (Phase 11.13) — one scene, two launchers.
 *
 * 		RoboDesktopHost.c (a native window on the host)  ┐
 * 		                                                ├→ this scene → the shell
 * 		user/robo-runtime (the QEMU framebuffer guest)  ┘
 *
 * ## Why this exists rather than living inside either launcher
 *
 * A desktop shell is a *product*: it draws what it is told. A scene is a *fixture*: the one arrangement
 * somebody looks at to decide whether the shell is right — a panel, and a window whose contents are a real
 * application document. Two launchers drive it (the host binary with a Win32 client area, the guest with a
 * UEFI framebuffer), and the whole value of having both is that they show the *same* thing: a scene written
 * twice would drift the moment one of them was adjusted, and the drift would be invisible, because each
 * launcher would still be internally consistent.
 *
 * So this file is the single definition of the checkpoint — the markup, the author sheet, the panel's
 * labels, the window's title and placement — and each launcher contributes only what a launcher is for:
 * which backend is registered, how long to run, what to print, and where a picture is written.
 *
 * ## What it owns
 *
 * A shell (`Desktop`), the application document that shell window shows
 * (`DesktopDocument`), and the window's id. The document is owned here rather than by the caller
 * because a window borrows its display list and must never outlive it: a scene that handed the document out
 * would hand out the order the two must be released in, which is the rule `RoboDesktopDocument.h` states at
 * length. `DesktopSceneClose` releases both, in the order the borrowing requires.
 *
 * ## Determinism, and the one thing a caller supplies
 *
 * A step is a function of the scene's state, the events the window queue holds and the wait it is given —
 * nothing else. This file reads **no clock**: the status label is a string the *caller* sets
 * (`config.statusText`), because a fixture that stamped its own time into a frame could not be compared
 * against another one, and the whole point of a scene is that two runs of it produce the same picture.
 *
 * ## What a scene is not
 *
 * It is not a launcher: it never creates a backend, never writes a file, never prints, and never reads an
 * argument. It is not a scene *engine*: there is one scene (this one), it is named in this header, and a
 * second would be a second thing to keep in step rather than a reason to grow a registry. And it is not on
 * the production path — `Tools/Build.ps1` compiles it into the host suite binary and the guest program only,
 * which is the rule the deleted demo layer kept and the reason it is a `platform`-shaped fixture rather
 * than a module: the shipping runtime has no scene.
 */
#ifndef ROBO_DESKTOP_SCENE_H
#define ROBO_DESKTOP_SCENE_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktop.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboDesktop/RoboDesktopDocument.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An open scene: a shell, one window, and that window's document. */
typedef struct DesktopScene DesktopScene;

/** What the scene's window is titled. */
#define RoboDesktopSceneWindowTitle "Welcome"

/** Space the scene leaves between its window and the work area's edges, in pixels. */
#define RoboDesktopSceneMargin 80

/** The scene's screen size when a caller does not choose one: the panel's own design size. */
#define RoboDesktopSceneDefaultWidth 1280
#define RoboDesktopSceneDefaultHeight 800

/** What a caller hands an open scene. */
typedef struct DesktopSceneConfig {
    /** Screen width in pixels; 0 means @ref RoboDesktopSceneDefaultWidth. */
    int32_t width;
    /** Screen height in pixels; 0 means @ref RoboDesktopSceneDefaultHeight. */
    int32_t height;
    /**
     * The face the shell's chrome and the document's text are drawn with. Borrowed, never retained, and
     * may be NULL — a scene with no face draws its geometry and its document's fills and counts the labels
     * it could not shape, which is a state `RoboDesktop.h` treats as first class rather than an error.
     */
    FontFace *face;
    /**
     * Create the shell's own native window instead of running screen-only. Needs a window backend to have
     * been registered by the *caller* (the host launcher registers the Win32 one, the guest the QEMU one):
     * a scene that chose a backend would be a scene that could only run in one place.
     */
    int RoboCreateWindow;
    /** The native window's title; NULL means @ref RoboDesktopDefaultTitle. */
    const char *windowTitle;
    /** The panel's status label — "the caller's clock is the caller's". NULL means none. */
    const char *statusText;
    /** The panel's identity label; NULL means @ref RoboDesktopDefaultIdentity. */
    const char *identityText;
    /** The panel's middle label; NULL means @ref RoboDesktopDefaultApplications. */
    const char *applicationsText;
} DesktopSceneConfig;

/** Fill @p config with the defaults: 1280x800, no face, create a window, the default labels, no status. */
void DesktopSceneConfigInit(DesktopSceneConfig *config);

/**
 * Open the scene: create the shell, place its window, and build the document that window shows.
 *
 * The window is sized from the *work area the shell computed* rather than from the screen, so the scene is
 * the same picture at any resolution: @ref RoboDesktopSceneMargin on every side, which is what makes a
 * 640x480 run of it recognisably the same desktop as a 1280x800 one.
 *
 * @param config Borrowed for the duration of the call only.
 * @param out Receives the scene; untouched on failure.
 * @param diag May be NULL.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p config or @p out or an unusable screen size;
 *         RoboNotSupported with a `SHELL` diagnostic when @p config asked for a native window and none
 *         could be created (no backend registered, no interactive session); RoboParseError with a
 *         `DOCUMENT` diagnostic when the window's document could not be built; RoboOutOfMemory.
 */
Result DesktopSceneOpen(const DesktopSceneConfig *config,
                                      DesktopScene **out,
                                      DesktopDiag *diag);

/** Close @p scene: its document, its window and its shell, in that order. Safe on NULL. */
void DesktopSceneClose(DesktopScene *scene);

/** Non-zero once a scene exists and has not been closed. */
int DesktopSceneIsLive(const DesktopScene *scene);

/** The scene's shell. NULL for NULL. */
Desktop *DesktopSceneDesktop(DesktopScene *scene);

/** The scene's application window, or @ref RoboDesktopWindowIdNone. */
DesktopWindowId DesktopSceneWindow(const DesktopScene *scene);

/** The window's document, borrowed. NULL before a document exists or for NULL. */
const DesktopDocument *DesktopSceneDocument(const DesktopScene *scene);

/* ------------------------------------------------------------------ */
/* The scene's own words                                               */
/* ------------------------------------------------------------------ */

/**
 * The markup and the author sheet the scene's window shows, borrowed and NUL-terminated.
 *
 * Exposed because a launcher reports them: the guest's serial log and the host's banner both name how many
 * bytes of markup this build parsed, and a launcher that copied the strings to do that would be reporting a
 * *second* copy's length. They are also what a suite asserts the document against, so what the scene builds
 * and what a test expects cannot become two different documents.
 */
const char *DesktopSceneMarkup(void);
const char *DesktopSceneStyle(void);

/** Byte length of @ref DesktopSceneMarkup (without the terminator). */
size_t DesktopSceneMarkupLength(void);

/* ------------------------------------------------------------------ */
/* Stepping                                                            */
/* ------------------------------------------------------------------ */

/** What one step did. */
typedef struct DesktopSceneStep {
    /** Events taken off the window queue during this step. */
    size_t events;
    /** Pointer presses the shell acted on (a raise, a focus or a close). */
    size_t pointerActions;
    /** Destination size changes the step brought the screen into line with, or 0. */
    int resized;
    /** Documents rebuilt because a resize changed the client area, or 0. */
    int rebuilt;
    /** Non-zero when the step painted a frame. `frame` describes it; 0 means nothing had changed. */
    int painted;
    /**
     * Non-zero when that frame reached the window's backend. Always 0 for a screen-only scene, which is a
     * desktop that renders and has nowhere to put its frames rather than a desktop that failed.
     */
    int presented;
    /** Non-zero when the *window* accepted the presented frame; a window that was not dirty is not an error. */
    int windowPresented;
    /**
     * Non-zero once the checkpoint is over: the shell's own window was closed, or the application window's
     * close button was pressed and the scene retired it. A launcher decides what that means for *it* (the
     * host one stops, the guest one keeps driving the shell it still has).
     */
    int closeRequested;
    /** The frame this step painted; a zeroed record when the step did not paint. */
    DesktopFrameRecord frame;
    /** The presentation this step performed; a zeroed record when the step did not present. */
    RenderPresentRecord present;
} DesktopSceneStep;

/** Clear @p step to a step that did nothing. Safe on NULL. */
void DesktopSceneStepInit(DesktopSceneStep *step);

/**
 * Advance the scene by one iteration.
 *
 * The order is the one every frame of this tree's life runs in, and it is the order because each step
 * depends on the one before it: pump the window queue (≤ @p waitMs) and take its events, hand any pointer
 * state to the shell (`DesktopObservePointer` — the whole of this build's input integration), match
 * the screen to the window's physical client size (`DesktopSyncSize`; a window the user resized
 * reports a new size and the shell follows it), and finally paint and present a frame when there is
 * something to show.
 *
 * A resize **rebuilds the document** rather than stretching the old one, and for the reason the document
 * layer states: a document is built for a viewport, and a picture laid out for 900 pixels shown in a
 * 600-pixel client area is not a smaller picture but a wrong one. Rebuilding costs one parse per resize and
 * buys a window that is always laid out for the size it has.
 *
 * @return RoboOk, including a step that painted nothing because nothing changed; RoboInvalidArgument
 *         for NULL @p scene; anything `DesktopRender`/`present` reports with @p step's fields left
 *         describing the part that happened.
 */
Result GetDesktopSceneStep(DesktopScene *scene,
                                      uint32_t waitMs,
                                      DesktopSceneStep *outStep,
                                      DesktopDiag *diag);

/** Mark the scene as needing a frame: a launcher that changed the status label calls this. */
void DesktopSceneInvalidate(DesktopScene *scene);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_SCENE_H */
