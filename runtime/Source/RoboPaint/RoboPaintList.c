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

/** The list: a contiguous array and its two counts, plus the characters its text
 *  operations index (11.6). */
struct robo_paint_list {
    robo_paint_operation_t *operations;
    /** Operations held. */
    size_t count;
    /** Operations the array has room for. */
    size_t capacity;
    /**
     * The code points the list's `TEXT` operations name, owned here.
     *
     * The second array, and it is a **pool rather than a field** for the reason
     * RoboPaintText.h argues: characters are variable-length, so a record can only name
     * them, and the pool is the one thing that knows whether a span names anything. It is
     * created with the list and destroyed with it, so a list is still exactly one object
     * with one owner -- and a caller that never paints text never notices it is there. */
    robo_paint_text_pool_t *text;
};

/**
 * Release the resources the first @p count operations of @p list hold.
 *
 * `clear` and `destroy` share it so that "a container gives back what it took" is one sentence in
 * one place. It walks **only** the live prefix, and it is safe on a list whose operations hold
 * nothing -- which is every list this build produced before 11.7 and every one it produces for a
 * document with no images.
 */
static void robo_paint_list_release_operations(robo_paint_list_t *list, size_t count)
{
    size_t index;

    for (index = 0u; index < count; index++) {
        robo_paint_image_release(&list->operations[index].image);
    }
}

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
    /*
     * The text pool is taken at creation too, and for the same reason the first block is:
     * a caller that sizes a list before a pass may rely on the door answering rather than
     * on a lazily created pool that would make "add a run's characters" a call that can
     * fail for a reason unrelated to the characters. It costs one empty allocation for a
     * document with no text, which is the trade this module makes everywhere else.
     */
    if (robo_paint_text_pool_create(&list->text, diag) != ROBO_OK) {
        robo_free(operations);
        robo_free(list);
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_OUT_OF_MEMORY,
                               "could not allocate the paint list's text pool");
    }
    *out_list = list;
    return ROBO_OK;
}

void robo_paint_list_destroy(robo_paint_list_t *list)
{
    if (!list) {
        return;
    }
    /*
     * Every operation the list holds is a value, and since 11.7 one of them can name a
     * **retained resource** -- so the release is a walk, not just two frees. It is the same
     * walk `clear` makes, extracted here and called from both doors because a container that
     * released on one path and not the other is the leak this module would then spend an evening
     * finding.
     *
     * The text pool is the other half of the same argument one level down: its code points are
     * values, so destroying it needs no walk of its own.
     */
    robo_paint_list_release_operations(list, list->count);
    robo_paint_text_pool_destroy(list->text);
    robo_free(list->operations);
    robo_free(list);
}

void robo_paint_list_clear(robo_paint_list_t *list)
{
    if (!list) {
        return;
    }
    /*
     * The resources first, then the count -- in that order, so that the slots the release walks
     * are still the live ones.
     *
     * An image operation's payload is **borrowed by the record and retained by the list**
     * (`RoboPaintImage.h` states the table), and this is the half of that rule the container
     * owns: the reference `append` took is given back here, once per operation that holds one.
     * For every other kind there is nothing to release, so a document that paints no images walks
     * its operations and frees nothing -- which is why the cost of this rule is a load and a
     * branch rather than a second structure to keep in step.
     *
     * Only the count then moves. The storage is reused and the old operations are left in it --
     * overwritten by the next append, and never reachable through a door again, because every read
     * door clamps to `count`. A `memset` here would be work whose only effect is invisible, and it
     * would make `clear` cost a document's worth of bytes per frame for nothing.
     *
     * The text pool is cleared with it and **must** be: the operations that named its characters
     * are gone, so leaving the code points would be storage for spans nothing can read -- and the
     * next pass's runs would be appended after them, which is the slow leak that looks like
     * working code.
     */
    robo_paint_list_release_operations(list, list->count);
    list->count = 0u;
    robo_paint_text_pool_clear(list->text);
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
    /*
     * The span rule, and it is the one thing about a text record only the container can
     * decide: the record says how many characters it draws and where they start, and this
     * is where the characters are. A span that is not a run in this pool is refused, so a
     * consumer that reads a text operation can read its characters and always get them.
     */
    if (operation->kind == ROBO_PAINT_OP_TEXT &&
        !robo_paint_text_pool_contains(list->text, operation->text.span)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "the operation's text span is not a run in this list's pool");
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
    /*
     * The copy, and then the **one** place the ownership rule is stated: a stored operation
     * holds a reference of its own to whatever resource it names, so that the list can outlive
     * the tree it came from and the payload it was handed can stay borrowed
     * (`robo_paint_image_retain` is that sentence with a function around it). Copying first and
     * retaining the copy is deliberate -- the caller's operation is untouched, so a caller that
     * appends the same record twice ends up with two references and not one that was counted
     * twice.
     */
    list->operations[list->count] = *operation;
    robo_paint_image_retain(&list->operations[list->count].image);
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

robo_result_t robo_paint_list_add_text(robo_paint_list_t *list,
                                      const uint32_t *code_points,
                                      size_t count,
                                      robo_paint_text_span_t *out_span,
                                      robo_paint_diag_t *diag)
{
    if (!list) {
        if (out_span) {
            *out_span = robo_paint_text_span_at(0u);
        }
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_LIST, ROBO_INVALID_ARGUMENT,
                               "list is NULL");
    }
    return robo_paint_text_pool_append(list->text, code_points, count, out_span, diag);
}

robo_paint_text_pool_t *robo_paint_list_text_pool(const robo_paint_list_t *list)
{
    return list ? list->text : NULL;
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
