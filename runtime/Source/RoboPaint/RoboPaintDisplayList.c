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

/** The list: a contiguous array, its two counts, and the characters its text commands
 *  index (11.6). */
struct robo_paint_display_list {
    robo_paint_display_command_t *commands;
    /** Commands held. */
    size_t count;
    /** Commands the array has room for. */
    size_t capacity;
    /**
     * The code points this list's `DRAW_TEXT` commands name, owned here.
     *
     * The paint list's second array, for the same reason and with one extra one: a display
     * command carries a **span** into a pool (`RoboPaintDisplayCommand.h`), and the pool the
     * builder filled it from belongs to the *other* list -- so a display list that did not
     * copy the characters would be a list whose commands named a pool that could be cleared
     * or destroyed under it. Copying them is what makes "a display list is a snapshot" true
     * for text as it already was for geometry. */
    robo_paint_text_pool_t *text;
};

/**
 * Release the resources the first @p count commands of @p list hold.
 *
 * The paint list's walk at this artifact's element type, and it exists for the same reason: a
 * container that took a reference gives it back on **both** its paths, and "both" is stated once
 * here rather than twice below. It walks only the live prefix and is safe on a list whose
 * commands hold nothing -- which is every list built before 11.7 and every one built for a
 * document with no images.
 */
static void robo_paint_display_list_release_commands(robo_paint_display_list_t *list,
                                                     size_t count)
{
    size_t index;

    for (index = 0u; index < count; index++) {
        robo_paint_image_release(&list->commands[index].image);
    }
}

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
    if (robo_paint_text_pool_create(&list->text, diag) != ROBO_OK) {
        robo_free(commands);
        robo_free(list);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the display list's text pool");
    }
    *out_list = list;
    return ROBO_OK;
}

void robo_paint_display_list_destroy(robo_paint_display_list_t *list)
{
    if (!list) {
        return;
    }
    /*
     * Since 11.7 a command can name a **retained resource**, so the release is the paint list's
     * walk and then the two frees -- including the text pool, whose code points are values and
     * need no walk of their own.
     */
    robo_paint_display_list_release_commands(list, list->count);
    robo_paint_text_pool_destroy(list->text);
    robo_free(list->commands);
    robo_free(list);
}

void robo_paint_display_list_clear(robo_paint_display_list_t *list)
{
    if (!list) {
        return;
    }
    /*
     * The resources first, then the count -- the order the record list states and for the same
     * reason: an image command's payload is retained by the list that holds it, and this is where
     * the reference `append` took is given back, once per command that holds one. Then only the
     * count moves: the storage is reused, and every read door clamps to it. The text pool is
     * cleared with it, because the commands that named its characters are gone -- leaving them
     * would be storage for spans nothing can read, and the next build's runs would pile up behind
     * them.
     */
    robo_paint_display_list_release_commands(list, list->count);
    list->count = 0u;
    robo_paint_text_pool_clear(list->text);
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
    /* The paint list's span rule at the same boundary, and for the same reason: a command
     * says how many characters it draws and where they start, and this is where the
     * characters are -- so a `DRAW_TEXT` a consumer reads can always be read through. */
    if (command->kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT &&
        !robo_paint_text_pool_contains(list->text, command->text.span)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "the command's text span is not a run in this list's pool");
    }
    if (list->count == list->capacity) {
        size_t wanted = list->capacity < 1u ? 1u : list->capacity * 2u;

        result = robo_paint_display_list_reserve(list, wanted, diag);
        if (result != ROBO_OK) {
            return result;
        }
    }
    /*
     * The copy and then the reference, exactly as the record list's append does it: a stored
     * command owns whatever resource it names, so a display list can outlive the record list it
     * was built from and the builder can hand out borrowed payloads. Copying before retaining
     * leaves the caller's command untouched, so building one command into two lists yields two
     * references rather than one that was counted twice.
     */
    list->commands[list->count] = *command;
    robo_paint_image_retain(&list->commands[list->count].image);
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

robo_paint_text_pool_t *robo_paint_display_list_text_pool(const robo_paint_display_list_t *list)
{
    return list ? list->text : NULL;
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
