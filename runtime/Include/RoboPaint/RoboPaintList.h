/*
 * RoboPaint list (Phase 11.1).
 *
 * The ordered sequence of operations one pass produced, and the **only** thing this
 * module allocates. Everything else here is a value record.
 *
 * Ownership is stated once and is simple on purpose: a list is created by a caller,
 * owns one growable array of operations, and frees exactly that on destroy. It borrows
 * nothing -- an operation is a value with no pointers in it, so a list has no reference
 * to the tree it was built from and no lifetime relation to it. That is what makes the
 * three properties a consumer needs true by construction rather than by discipline:
 *
 *   a list is **safe to discard and regenerate**, in any order relative to the tree,
 *   because it holds no reference to it;
 *
 *   a list **cannot contain a dangling pointer**, because it contains no pointers;
 *
 *   a list **cannot mutate layout geometry**, because it never had access to it. The
 *   traversal reads the tree through RoboLayout's own read doors, which take the tree
 *   by `const` pointer, so "paint does not modify layout" is the signature rather than
 *   a promise (`RoboPaintContext.h` states it where the pass is).
 *
 * The one rule a caller must know is the `clear`/`reserve` pair, and it is about
 * frames rather than about correctness: clearing keeps the capacity, so a renderer that
 * repaints every frame calls `clear` and reuses one list rather than allocating a new
 * one per frame. That is deliberately the *only* concession this phase makes to
 * performance, and it is not an incremental mechanism -- the next pass recomputes every
 * operation from scratch, because a partially updated list would be a cache of
 * conclusions this module has no way to validate.
 */
#ifndef ROBO_PAINT_LIST_H
#define ROBO_PAINT_LIST_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintOperation.h"
#include "RoboPaint/RoboPaintTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Operations a list holds before its first growth. */
#define ROBO_PAINT_INITIAL_CAPACITY 16u

/** An opaque, caller-owned operation list. */
typedef struct robo_paint_list robo_paint_list_t;

/**
 * Create an empty list with room for @ref ROBO_PAINT_INITIAL_CAPACITY operations.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out_list; ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_paint_list_create(robo_paint_list_t **out_list, robo_paint_diag_t *diag);

/** Destroy @p list and its operations. The tree it was built from is not touched.
 *  Safe on NULL. */
void robo_paint_list_destroy(robo_paint_list_t *list);

/**
 * Drop every operation, keeping the capacity.
 *
 * The door a per-frame renderer uses, and the reason it exists rather than "destroy and
 * create": the operations were a conclusion about one state of the tree and the next
 * pass concludes again from scratch, so what is reused is the *storage* and never the
 * answers. Safe on NULL.
 */
void robo_paint_list_clear(robo_paint_list_t *list);

/**
 * Make room for at least @p capacity operations.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p list; ROBO_OUT_OF_MEMORY. A
 *         request below the current capacity succeeds and changes nothing, so a caller
 *         may size a list before every pass without a branch.
 */
robo_result_t robo_paint_list_reserve(robo_paint_list_t *list,
                                     size_t capacity,
                                     robo_paint_diag_t *diag);

/**
 * Append @p operation.
 *
 * **The boundary that keeps an invalid operation out of a list**, and the reason the
 * validation lives here rather than in the traversal: the traversal is one caller, and
 * a rule enforced in the container is a rule no future caller can forget -- the same
 * argument `RoboLayoutTree.h` makes for validating a parent link at the door rather
 * than in the builder. An operation that is not a record (see
 * `robo_paint_operation_is_valid`) is **refused**, which is what makes every list a
 * consumer reads a list of records, and is the "geometry safety" requirement satisfied
 * at the only place it can be satisfied once instead of at every call site.
 *
 * The operation is copied by value; the caller keeps ownership of its argument.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an operation that is not
 *         a record (@p list is left untouched); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_paint_list_append(robo_paint_list_t *list,
                                    const robo_paint_operation_t *operation,
                                    robo_paint_diag_t *diag);

/** Operations in @p list (0 for NULL). */
size_t robo_paint_list_count(const robo_paint_list_t *list);

/** Operations @p list can hold before growing (0 for NULL). */
size_t robo_paint_list_capacity(const robo_paint_list_t *list);

/**
 * The operation at @p index, by value.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND for an
 *         index at or past the count (@p out is untouched).
 */
robo_result_t robo_paint_list_at(const robo_paint_list_t *list,
                                size_t index,
                                robo_paint_operation_t *out);

/**
 * The operations, borrowed and **contiguous**, or NULL when the list is empty.
 *
 * The door a rasteriser wants -- a pointer and a count, no per-operation call -- and the
 * reason the record has no pointers in it: the array moves when the list grows, which is
 * a documented lifetime rule ("valid until the next append") rather than a hazard,
 * because nothing in an operation points anywhere.
 */
const robo_paint_operation_t *robo_paint_list_operations(const robo_paint_list_t *list);

/**
 * Non-zero when @p a and @p b hold the same operations in the same order.
 *
 * The determinism comparison, as one call: a suite that compared two lists by walking
 * both would be re-implementing this and would report "the fourth operation differs" as
 * its own bug at least once. Empty lists are equal; two NULLs are equal.
 */
int robo_paint_list_equal(const robo_paint_list_t *a, const robo_paint_list_t *b);

/** How many operations in @p list have kind @p kind (0 for NULL). */
size_t robo_paint_list_count_of_kind(const robo_paint_list_t *list, robo_paint_op_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_LIST_H */
