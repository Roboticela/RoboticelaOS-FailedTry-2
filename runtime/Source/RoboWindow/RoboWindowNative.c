#include "RoboWindow/RoboWindowNative.h"

#include <stddef.h>

void robo_window_native_handle_clear(robo_window_native_handle_t *h)
{
    if (!h) {
        return;
    }
    h->token = 0u;
    h->generation = 0u;
}

int robo_window_native_handle_is_valid(const robo_window_native_handle_t *h)
{
    return h != NULL && h->token != 0u && h->generation != 0u;
}

int robo_window_native_handle_is_empty(const robo_window_native_handle_t *h)
{
    return !robo_window_native_handle_is_valid(h);
}

int robo_window_native_handle_equal(const robo_window_native_handle_t *a,
                                    const robo_window_native_handle_t *b)
{
    if (!robo_window_native_handle_is_valid(a) ||
        !robo_window_native_handle_is_valid(b)) {
        return 0;
    }
    return a->token == b->token && a->generation == b->generation;
}

robo_result_t robo_window_native_handle_assign(robo_window_native_handle_t *h,
                                               uint64_t token,
                                               uint32_t generation)
{
    if (!h || token == 0u || generation == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    h->token = token;
    h->generation = generation;
    return ROBO_OK;
}

void robo_window_native_handle_invalidate(robo_window_native_handle_t *h)
{
    robo_window_native_handle_clear(h);
}
