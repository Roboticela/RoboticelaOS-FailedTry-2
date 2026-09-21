/*
 * RoboWindow — platform-independent windowing (Goals 7.1–7.11).
 *
 * Dependency:
 *   RoboBase → RoboMem / RoboError → RoboGraphics → RoboDisplay → RoboWindow
 *     → Native Window Interface → host/mock/null backend
 *
 * Goal 7.10 keyboard; Goal 7.11 mouse foundation. Touch / stylus / pointer
 * unification and text-IME remain later goals.
 *
 * Chromium Ozone / PlatformWindow / KeyEvent / WebMouseEvent are temporary
 * technical reference only.
 *
 * See window/README.md and window/docs/.
 */

#ifndef ROBO_WINDOW_H
#define ROBO_WINDOW_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindowCapabilities.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowDisplay.h"
#include "RoboWindow/RoboWindowEvent.h"
#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindowKey.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowLifecycle.h"
#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowPresent.h"
#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboWindow/RoboWindowSize.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowThread.h"
#include "RoboWindow/RoboWindowTypes.h"
#include "RoboWindow/RoboWindowVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *WindowVersionString(void);

Result WindowInit(void);
void WindowShutdown(void);
int WindowIsInitialized(void);

Result WindowCreate(const WindowConfig *cfg,
                                 WindowId *outId);

Result WindowShow(WindowId id);
Result WindowHide(WindowId id);

/**
 * Request close (CREATED → CLOSING). Does not destroy the native window.
 * Emits CLOSE_REQUESTED. Irreversible — no cancel API in Goal 7.7.
 * Already CLOSING → RoboAlreadyExists. Destroyed / unknown → RoboNotFound.
 */
Result WindowRequestClose(WindowId id);

/**
 * Apply an observed native close request (WM_CLOSE / mock inject).
 * CREATED → same as request_close. Already CLOSING → RoboOk (idempotent).
 * Does not destroy native resources.
 */
Result WindowObserveCloseRequest(WindowId id);

/**
 * Destroy native resources and retire the window id (→ CLOSED).
 * Allowed from CREATED or CLOSING. Double destroy → RoboNotFound.
 * Native destroy failure leaves lifecycle unchanged.
 */
Result WindowDestroy(WindowId id);

/** Copy lifecycle enumerator; requires a live window (CREATED or CLOSING). */
Result WindowGetLifecycle(WindowId id,
                                        WindowLifecycle *out);

/** Non-zero if lifecycle is CLOSING. */
int WindowIsClosing(WindowId id);

Result WindowGetState(WindowId id, WindowState *out);

/** Borrowed UTF-8 title; valid until title change / destroy. */
Result WindowGetTitle(WindowId id, const char **outTitle);

/**
 * Set UTF-8 title (copied). Invalid UTF-8 → RoboParseError.
 * NULL title → empty string.
 */
Result WindowSetTitle(WindowId id, const char *utf8Title);

/**
 * Request a new logical client size. Alias of the sizing entry point used by
 * Goal 7.6 tests (`SetSize` / `get_size` naming).
 * Invalid / out-of-constraint → RoboInvalidArgument (no silent clamp).
 * On backend failure, requested size is left unchanged.
 */
Result WindowSetSize(WindowId id,
                                   int32_t width,
                                   int32_t height);

/** Same as WindowSetSize (historical Goal 7.1 name). */
Result WindowRequestSize(WindowId id,
                                       int32_t width,
                                       int32_t height);

/** Actual logical size (syncs from backend when reportsActualState). */
Result WindowGetSize(WindowId id, GfxSize *out);

/** Last successfully requested size (may differ from actual). */
Result WindowGetRequestedSize(WindowId id,
                                             GfxSize *out);

Result WindowGetSizeConstraints(
    WindowId id,
    WindowSizeConstraints *out);

/**
 * Replace min/max constraints (0 = none). Rejects inconsistent sets.
 * If current actual size violates the new constraints, attempts a corrective
 * resize to the nearest valid size (deterministic).
 */
Result WindowSetSizeConstraints(
    WindowId id,
    const WindowSizeConstraints *constraints);

/**
 * Apply an observed native size (event-loop path). Updates actual size without
 * calling backend SetSize. Does not change requested size or constraints.
 * Emits RESIZED when dimensions change. Skipped while applying geometry APIs
 * (avoids resize loops / duplicate events).
 */
Result WindowObserveSize(WindowId id,
                                       int32_t width,
                                       int32_t height);

/**
 * Apply an observed native position. Negative coordinates allowed.
 * Emits MOVED when position changes. Does not call backend SetPosition.
 */
Result WindowObservePosition(WindowId id,
                                           int32_t x,
                                           int32_t y);

/**
 * Apply an observed native visibility (shown=1 / hidden=0).
 * Only when the platform reports a real visibility change — do not fabricate.
 * Emits SHOWN / HIDDEN on change.
 */
Result WindowObserveVisibility(WindowId id, int visible);

/**
 * Apply an observed native focus change (gained=1 / lost=0).
 * Emits FOCUS_GAINED / FOCUS_LOST on change. Not keyboard routing.
 */
Result WindowObserveFocus(WindowId id, int focused);

Result WindowRequestPosition(WindowId id,
                                           int32_t x,
                                           int32_t y);

/** Request focus via backend; may be RoboNotSupported. */
Result WindowRequestFocus(WindowId id);

/** Copy current native handle (may be empty for null backend). */
Result WindowGetNativeHandle(WindowId id,
                                            WindowNativeHandle *out);

int WindowIdIsLive(WindowId id);
size_t WindowLiveCount(void);

/**
 * Non-blocking native drain (same as WindowEventLoopPoll).
 * Nested while already pumping → RoboBusy.
 */
Result WindowProcessEvents(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_H */
