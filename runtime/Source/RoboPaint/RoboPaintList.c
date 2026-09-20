/*
 * RoboPaint list (Phase 11.1) — see RoboPaintList.h.
 *
 * One growable array of value records, and the whole of this module's allocation.
 * The struct is deliberately the plainest thing that can hold a sequence:
 *
 *     operations -> [ op ][ op ][ op ] ...        count items
 *                                              capacity items
 *
 * `count` and `capacity` are separate because the two questions a caller asks are
 * separate -- "how many operations do I have?" and "how many may I append before this
 * reallocates?" -- and because `clear` is defined as "count = 0, capacity unchanged",
 * which is only expressible when they are two fields.
 *
 * The one rule that lives here rather than at the call site is the **append's
 * validation**: an operation that is not a record (RoboPaintOperation.h) is refused, so
 * every list a consumer reads is a list of records and no future caller can forget to
 * check. That is the same argument RoboLayoutTree.h makes for validating a parent link
 * at the door; a rule in the container is enforced once, a rule in the traversal is
 * enforced per traversal.
 */

#include "RoboPaint/RoboPaintList.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/** The list: a contiguous array and its two counts. */
struct robo_paint_list {
    robo_paint_operation_t *operations;
    /** Operations held. */
    size_t count;
    /** Operations the array has room for. */
    size_t capacity;
};

robo_result_t robo_paint_list_create(robo_paint_list_t **out_list, robo_paint_diag_t *diag)
{
    robo_paint_list_t *list;
    robo_paint_operation_t *operations;

    if (!out_list) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "out_list is NULL");
    }
    *out_list = NULL;

    list = (robo_paint_list_t *)robo_calloc(1u, sizeof(*list));
    if (!list) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the paint list");
    }

    /*
     * The first block is taken at creation rather than at the first append, and the
     * reason is the contract `robo_paint_list_capacity` states: a caller that sizes a
     * list before a pass may rely on `ROBO_PAINT_INITIAL_CAPACITY` being there without a
     * branch. A list that allocated nothing until the first append would make that
     * door's answer 0 and mean "reserve first" at every call site.
     */
    operations = (robo_paint_operation_t *)robo_calloc(ROBO_PAINT_INITIAL_CAPACITY,
                                                       sizeof(*operations));
    if (!operations) {
        robo_free(list);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the paint list's first block");
    }
    list->operations = operations;
    list->count = 0u;
    list->capacity = ROBO_PAINT_INITIAL_CAPACITY;
    *out_list = list;
    return ROBO_OK;
}

void robo_paint_list_destroy(robo_paint_list_t *list)
{
    if (!list) {
        return;
    }
    /* The operations are values with no pointers in them, so freeing the array is the
     * whole of the release -- there is nothing to walk and nothing to unlink, which is
     * exactly what makes a list safe to discard in any order relative to a tree. */
    robo_free(list->operations);
    robo_free(list);
}

void robo_paint_list_clear(robo_paint_list_t *list)
{
    if (!list) {
        return;
    }
    /*
     * Only the count moves. The storage is reused and the old operations are left in
     * it -- overwritten by the next append, and never reachable through a door again,
     * because every read door clamps to `count`. A `memset` here would be work whose
     * only effect is invisible, and it would make `clear` cost a document's worth of
     * bytes per frame for nothing.
     */
    list->count = 0u;
}

robo_result_t robo_paint_list_reserve(robo_paint_list_t *list,
                                     size_t capacity,
                                     robo_paint_diag_t *diag)
{
    robo_paint_operation_t *grown;

    if (!list) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "list is NULL");
    }
    if (capacity <= list->capacity) {
        /* A request below the current capacity is success and a no-op, so a caller may
         * size a list before every pass without asking what it already holds. */
        return ROBO_OK;
    }
    grown = (robo_paint_operation_t *)robo_realloc(list->operations,
                                                   capacity * sizeof(*grown));
    if (!grown) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY,
                               "could not grow the paint list");
    }
    /*
     * Zeroed only from the old end onward, so that the block a caller can reach through
     * `operations()` is deterministic: a consumer reading `capacity` slots -- which the
     * door's documentation invites -- sees cleared records past the count rather than
     * whatever the allocator had. The live prefix is untouched, because its records are
     * answers the pass already concluded.
     */
    memset(grown + list->capacity, 0,
           (capacity - list->capacity) * sizeof(*grown));
    list->operations = grown;
    list->capacity = capacity;
    return ROBO_OK;
}

robo_result_t robo_paint_list_append(robo_paint_list_t *list,
                                    const robo_paint_operation_t *operation,
                                    robo_paint_diag_t *diag)
{
    robo_result_t result;

    if (!list || !operation) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the list and the operation are required");
    }
    /*
     * The rule, checked once and at the container: an operation that is not a record is
     * refused and the list is left untouched. This is what makes **every** list a
     * consumer can reach a list of records -- there is no other door that puts an
     * operation in -- and it is why the traversal does not have to defend itself
     * against its own arithmetic at every append.
     */
    if (!robo_paint_operation_is_valid(operation)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the operation is not a record: it names no box, its boxes "
                               "are not nested, or its colour does not match its kind");
    }
    if (list->count == list->capacity) {
        /* Doubled, and floored at one step so that a list somehow created with no room
         * still grows rather than spinning: `ROBO_PAINT_INITIAL_CAPACITY` is 16, so this
         * branch is unreachable in practice and correct if it ever is not. */
        size_t wanted = list->capacity < 1u ? 1u : list->capacity * 2u;

        result = robo_paint_list_reserve(list, wanted, diag);
        if (result != ROBO_OK) {
            return result;
        }
    }
    list->operations[list->count] = *operation;
    list->count++;
    return ROBO_OK;
}

size_t robo_paint_list_count(const robo_paint_list_t *list)
{
    return list ? list->count : 0u;
}

size_t robo_paint_list_capacity(const robo_paint_list_t *list)
{
    return list ? list->capacity : 0u;
}

robo_result_t robo_paint_list_at(const robo_paint_list_t *list,
                                size_t index,
                                robo_paint_operation_t *out)
{
    if (!list || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (index >= list->count) {
        return ROBO_NOT_FOUND;
    }
    *out = list->operations[index];
    return ROBO_OK;
}

const robo_paint_operation_t *robo_paint_list_operations(const robo_paint_list_t *list)
{
    if (!list || list->count == 0u) {
        return NULL;
    }
    return list->operations;
}

int robo_paint_list_equal(const robo_paint_list_t *a, const robo_paint_list_t *b)
{
    size_t index;

    if (a == b) {
        /* Two NULLs, or one list compared with itself: equal, and the second case is not
         * an optimization but the definition -- a list holds its own operations. */
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->count != b->count) {
        return 0;
    }
    for (index = 0u; index < a->count; index++) {
        if (!robo_paint_operation_equal(&a->operations[index], &b->operations[index])) {
            return 0;
        }
    }
    return 1;
}

size_t robo_paint_list_count_of_kind(const robo_paint_list_t *list, robo_paint_op_kind_t kind)
{
    size_t index;
    size_t total = 0u;

    if (!list) {
        return 0u;
    }
    for (index = 0u; index < list->count; index++) {
        if (list->operations[index].kind == kind) {
            total++;
        }
    }
    return total;
}
