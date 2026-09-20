/*
 * RoboDisplay list (Phase 11.3) — see RoboPaintDisplayList.h.
 *
 * One growable array of value commands. The struct is the paint list's, typed for a
 * different element because C types an array: a display command is not a paint operation,
 * so the storage cannot be shared even though the *shape* is deliberately identical.
 *
 * The rule that lives here rather than at the call site is the **append's validation**,
 * the paint list's rule at the same boundary: a command that is not a record is refused,
 * so every list a consumer reads is a list of commands and no future caller can forget to
 * check. It is not a second implementation of the record's rule -- `append` asks
 * `robo_paint_display_command_is_valid`, the one place that decides what a command is.
 */

#include "RoboPaint/RoboPaintDisplayList.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/** The list: a contiguous array and its two counts. */
struct robo_paint_display_list {
    robo_paint_display_command_t *commands;
    /** Commands held. */
    size_t count;
    /** Commands the array has room for. */
    size_t capacity;
};

robo_result_t robo_paint_display_list_create(robo_paint_display_list_t **out_list,
                                       robo_paint_diag_t *diag)
{
    robo_paint_display_list_t *list;
    robo_paint_display_command_t *commands;

    if (!out_list) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "out_list is NULL");
    }
    *out_list = NULL;

    list = (robo_paint_display_list_t *)robo_calloc(1u, sizeof(*list));
    if (!list) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the display list");
    }

    commands = (robo_paint_display_command_t *)robo_calloc(ROBO_PAINT_DISPLAY_INITIAL_CAPACITY,
                                                     sizeof(*commands));
    if (!commands) {
        robo_free(list);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the display list's first block");
    }
    list->commands = commands;
    list->count = 0u;
    list->capacity = ROBO_PAINT_DISPLAY_INITIAL_CAPACITY;
    *out_list = list;
    return ROBO_OK;
}

void robo_paint_display_list_destroy(robo_paint_display_list_t *list)
{
    if (!list) {
        return;
    }
    /* A command has no pointers in it, so freeing the array is the whole of the release. */
    robo_free(list->commands);
    robo_free(list);
}

void robo_paint_display_list_clear(robo_paint_display_list_t *list)
{
    if (!list) {
        return;
    }
    /* Only the count moves: the storage is reused, and every read door clamps to it. */
    list->count = 0u;
}

robo_result_t robo_paint_display_list_reserve(robo_paint_display_list_t *list,
                                       size_t capacity,
                                       robo_paint_diag_t *diag)
{
    robo_paint_display_command_t *grown;

    if (!list) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "list is NULL");
    }
    if (capacity <= list->capacity) {
        return ROBO_OK;
    }
    grown = (robo_paint_display_command_t *)robo_realloc(list->commands,
                                                   capacity * sizeof(*grown));
    if (!grown) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not grow the display list");
    }
    /* Zeroed from the old end onward, so the region a caller can reach through
     * `commands()` is deterministic past the count. */
    memset(grown + list->capacity, 0, (capacity - list->capacity) * sizeof(*grown));
    list->commands = grown;
    list->capacity = capacity;
    return ROBO_OK;
}

robo_result_t robo_paint_display_list_append(robo_paint_display_list_t *list,
                                      const robo_paint_display_command_t *command,
                                      robo_paint_diag_t *diag)
{
    robo_result_t result;

    if (!list || !command) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "the list and the command are required");
    }
    if (!robo_paint_display_command_is_valid(command)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "the command is not a record: it names no node, its "
                               "rectangle is not one, or its colour does not match its "
                               "kind");
    }
    if (list->count == list->capacity) {
        size_t wanted = list->capacity < 1u ? 1u : list->capacity * 2u;

        result = robo_paint_display_list_reserve(list, wanted, diag);
        if (result != ROBO_OK) {
            return result;
        }
    }
    list->commands[list->count] = *command;
    list->count++;
    return ROBO_OK;
}

size_t robo_paint_display_list_count(const robo_paint_display_list_t *list)
{
    return list ? list->count : 0u;
}

size_t robo_paint_display_list_capacity(const robo_paint_display_list_t *list)
{
    return list ? list->capacity : 0u;
}

robo_result_t robo_paint_display_list_at(const robo_paint_display_list_t *list,
                                  size_t index,
                                  robo_paint_display_command_t *out)
{
    if (!list || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= list->count) {
        return ROBO_NOT_FOUND;
    }
    *out = list->commands[index];
    return ROBO_OK;
}

const robo_paint_display_command_t *robo_paint_display_list_commands(const robo_paint_display_list_t *list)
{
    if (!list || list->count == 0u) {
        return NULL;
    }
    return list->commands;
}

int robo_paint_display_list_equal(const robo_paint_display_list_t *a, const robo_paint_display_list_t *b)
{
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->count != b->count) {
        return 0;
    }
    for (index = 0u; index < a->count; index++) {
        if (!robo_paint_display_command_equal(&a->commands[index], &b->commands[index])) {
            return 0;
        }
    }
    return 1;
}

size_t robo_paint_display_list_count_of_kind(const robo_paint_display_list_t *list,
                                       robo_paint_display_command_kind_t kind)
{
    size_t index;
    size_t total = 0u;

    if (!list) {
        return 0u;
    }
    for (index = 0u; index < list->count; index++) {
        if (list->commands[index].kind == kind) {
            total++;
        }
    }
    return total;
}
