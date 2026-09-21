/*
 * Opaque native-handle abstraction (Goals 7.1 / 7.2).
 *
 * Typed token + generation — never OS structs, never Chromium types.
 * Ownership: the active backend owns the native resource identified by
 * @p token until RoboDestroyWindow / invalidate.
 *
 * Validity: token != 0 AND generation != 0.
 * After invalidate/destroy, copies of the handle must not be reused.
 */

#ifndef ROBO_WINDOW_NATIVE_H
#define ROBO_WINDOW_NATIVE_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WindowNativeHandle {
    uint64_t token;      /* 0 = none */
    uint32_t generation; /* 0 = none; assigned by backend on create */
} WindowNativeHandle;

void WindowNativeHandleClear(WindowNativeHandle *h);

/** Non-zero if token and generation are both non-zero. */
int WindowNativeHandleIsValid(const WindowNativeHandle *h);

/** Empty / invalid (same as !is_valid, including NULL). */
int WindowNativeHandleIsEmpty(const WindowNativeHandle *h);

/** Non-zero if both valid and token+generation match. */
int WindowNativeHandleEqual(const WindowNativeHandle *a,
                                    const WindowNativeHandle *b);

/**
 * Assign a backend token. token==0 or generation==0 → RoboInvalidArgument.
 */
Result WindowNativeHandleAssign(WindowNativeHandle *h,
                                               uint64_t token,
                                               uint32_t generation);

/** Clear to invalid (deterministic; safe on NULL). */
void WindowNativeHandleInvalidate(WindowNativeHandle *h);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_NATIVE_H */
