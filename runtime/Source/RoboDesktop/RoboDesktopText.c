/*
 * RoboDesktop's own string helpers (Phase 11.13).
 *
 * Three functions, and they exist rather than `string.h` for the reason the whole of the guest target
 * exists: this module is compiled into a freestanding binary with no hosted C library, so a `strlen`, a
 * `strcmp` or a `strncpy` would either not link or - worse - link against something whose contract (a
 * `strncpy` that does not terminate an over-long copy, for one) is not the contract this module wants.
 *
 * The copy is the interesting one: `robo_desktop_string_copy` **refuses** an over-long string instead of
 * truncating it, because every caller of it is storing something a caller named (a title, a status label)
 * and a truncated label would be a label the user did not ask for. The bound is what a *caller* is told
 * about rather than a silent repair.
 */

#include "RoboDesktopInternal.h"

int robo_desktop_string_equal(const char *a, const char *b)
{
    size_t index = 0u;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    while (a[index] != '\0' && b[index] != '\0') {
        if (a[index] != b[index]) {
            return 0;
        }
        index++;
    }
    return a[index] == b[index];
}

size_t robo_desktop_string_length(const char *text)
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

robo_result_t robo_desktop_string_copy(char *out, size_t capacity, const char *text)
{
    size_t length;

    if (!out || capacity == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!text) {
        out[0] = '\0';
        return ROBO_OK;
    }
    length = robo_desktop_string_length(text);
    if (length + 1u > capacity) {
        /* Nothing is written on a refusal, so a caller that ignored the result still holds its old
         * string rather than half of a new one. */
        return ROBO_INVALID_ARGUMENT;
    }
    {
        size_t index;

        for (index = 0u; index < length; index++) {
            out[index] = text[index];
        }
        out[length] = '\0';
    }
    return ROBO_OK;
}
