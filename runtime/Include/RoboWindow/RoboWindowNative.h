/*
 * Opaque native-handle abstraction (Goals 7.1 / 7.2).
 *
 * Typed token + generation — never OS structs, never Chromium types.
 * Ownership: the active backend owns the native resource identified by
 * @p token until destroy_window / invalidate.
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

typedef struct robo_window_native_handle {
    uint64_t token;      /* 0 = none */
    uint32_t generation; /* 0 = none; assigned by backend on create */
} robo_window_native_handle_t;

void robo_window_native_handle_clear(robo_window_native_handle_t *h);

/** Non-zero if token and generation are both non-zero. */
int robo_window_native_handle_is_valid(const robo_window_native_handle_t *h);

/** Empty / invalid (same as !is_valid, including NULL). */
int robo_window_native_handle_is_empty(const robo_window_native_handle_t *h);

/** Non-zero if both valid and token+generation match. */
int robo_window_native_handle_equal(const robo_window_native_handle_t *a,
                                    const robo_window_native_handle_t *b);

/**
 * Assign a backend token. token==0 or generation==0 → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_window_native_handle_assign(robo_window_native_handle_t *h,
                                               uint64_t token,
                                               uint32_t generation);

/** Clear to invalid (deterministic; safe on NULL). */
void robo_window_native_handle_invalidate(robo_window_native_handle_t *h);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_NATIVE_H */
