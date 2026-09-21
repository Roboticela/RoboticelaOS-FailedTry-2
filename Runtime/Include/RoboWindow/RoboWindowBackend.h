/*
 * RoboWindow backend contract (Goals 7.1 / 7.2).
 *
 * Abstract vtable for future native / QEMU / host backends.
 * Goal 7.4 defines synchronous surface presentation through this vtable;
 * null backend still creates no OS window and does not present.
 *
 * Chromium Ozone PlatformWindow is technical reference only — not a dependency.
 */

#ifndef ROBO_WINDOW_BACKEND_H
#define ROBO_WINDOW_BACKEND_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowCapabilities.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GfxSurface GfxSurface;

typedef struct WindowBackendVtbl {
    const char *name;
    /** Required non-NULL capability table for this backend. */
    const WindowBackendCaps *capabilities;

    Result (*initialize)(void *userdata);
    void (*shutdown)(void *userdata);

    /**
     * Create native resources for @p id.
     * Null/mock backends may leave the handle empty. Host backends create a
     * real OS window and assign a valid opaque handle.
     */
    Result (*RoboCreateWindow)(void *userdata,
                                   WindowId id,
                                   const WindowConfig *cfg,
                                   WindowNativeHandle *outNative);

    /** Destroy native resources and invalidate @p native synchronously. */
    Result (*RoboDestroyWindow)(void *userdata,
                                    WindowId id,
                                    WindowNativeHandle *native);

    Result (*show)(void *userdata,
                          WindowId id,
                          WindowNativeHandle *native);
    Result (*hide)(void *userdata,
                          WindowId id,
                          WindowNativeHandle *native);

    /** UTF-8 title; pointer valid only for the duration of the call. */
    Result (*SetTitle)(void *userdata,
                               WindowId id,
                               WindowNativeHandle *native,
                               const char *utf8Title);

    Result (*SetSize)(void *userdata,
                              WindowId id,
                              WindowNativeHandle *native,
                              int32_t width,
                              int32_t height);

    /**
     * Apply min/max constraints (0 = none on that axis). Optional: NULL →
     * core keeps local constraints only; runtime set returns NOT_SUPPORTED
     * when caps.sizeConstraints is set without this entry.
     */
    Result (*SetSizeConstraints)(void *userdata,
                                          WindowId id,
                                          WindowNativeHandle *native,
                                          int32_t minWidth,
                                          int32_t minHeight,
                                          int32_t maxWidth,
                                          int32_t maxHeight);

    Result (*SetPosition)(void *userdata,
                                  WindowId id,
                                  WindowNativeHandle *native,
                                  int32_t x,
                                  int32_t y);

    Result (*focus)(void *userdata,
                           WindowId id,
                           WindowNativeHandle *native);

    /** Optional: update actual fields in @p inoutState. */
    Result (*QueryState)(void *userdata,
                                 WindowId id,
                                 const WindowNativeHandle *native,
                                 WindowState *inoutState);

    /**
     * Synchronously present @p surface to @p native.
     * Caller keeps @p surface valid for the duration of this call only.
     * Do not retain the pointer after return. No async present in Goal 7.4.
     * Unsupported → RoboNotSupported; format mismatch → RoboNotSupported.
     */
    Result (*PresentSurface)(void *userdata,
                                     WindowId id,
                                     WindowNativeHandle *native,
                                     GfxSurface *surface);

    /**
     * Non-blocking drain of the native message source (single batch).
     * Translate supported OS messages via observe_* — never expose OS types.
     */
    Result (*ProcessEvents)(void *userdata);

    /**
     * Optional (NULL allowed): block until native work, wakeup, or timeout.
     * @p timeoutMs == RoboWindowEventWaitForever → wait indefinitely.
     * After return, caller typically calls ProcessEvents.
     */
    Result (*WaitEvents)(void *userdata, uint32_t timeoutMs);

    /**
     * Optional (NULL allowed): interrupt a blocked WaitEvents.
     * May be invoked to unblock shutdown / cross-thread wake (minimal).
     */
    Result (*wakeup)(void *userdata);

    /**
     * Optional (NULL allowed, Goal 7.11): capture native mouse input to @p
     * native. Soft capture still works without these entries.
     */
    Result (*CaptureMouse)(void *userdata,
                                   WindowId id,
                                   WindowNativeHandle *native);
    Result (*ReleaseMouseCapture)(void *userdata);
} WindowBackendVtbl;

/**
 * Register the single active backend. Replaces prior backend after shutdown.
 * Requires capabilities + core ops (including SetTitle, focus, present,
 * ProcessEvents). WaitEvents / wakeup are optional. initialize() runs
 * immediately.
 */
Result WindowBackendRegister(const WindowBackendVtbl *vtbl,
                                           void *userdata);

void WindowBackendClear(void);

int WindowBackendIsRegistered(void);

const char *WindowBackendName(void);

const WindowBackendVtbl *WindowBackendNullVtbl(void);

Result WindowBackendUseNull(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_BACKEND_H */
