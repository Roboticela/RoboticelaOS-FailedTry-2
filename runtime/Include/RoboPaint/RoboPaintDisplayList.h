/*
 * RoboDisplay list (Phase 11.3).
 *
 * The ordered sequence of display commands a build produced, and the **artifact** Phase
 * 11.3 adds: a render-ready list a rasteriser walks once, in order, with no box model and
 * no tree.
 *
 * Ownership is the paint list's, stated once and simple on purpose (`RoboPaintList.h`):
 * a list is created by a caller, owns one growable array of commands, and frees exactly
 * that on destroy. It borrows nothing -- a command is a value with no pointers in it --
 * so a display list has no reference to the paint list it was built from, no reference to
 * the tree, and no lifetime relation to either. That is what makes the three properties a
 * consumer needs true by construction rather than by discipline:
 *
 *   safe to discard and regenerate, in any order relative to the tree and the records;
 *   cannot contain a dangling pointer, because it contains no pointers;
 *   cannot mutate layout geometry, because it never had access to it.
 *
 * **A display list is *regular*, and that is a guarantee rather than a hope.** A paint
 * list is a *container*: `robo_paint_list_append` validates each operation, so every list
 * is a list of records, but nothing *sequences* them -- a caller may append operations in
 * any order and get a list that is a list of records and not a traversable document. The
 * builder (`RoboPaintDisplayListBuilder.h`) is the door that turns the traversal's *convention*
 * (one structural triple per box, in nested order) into an *invariant* of the display
 * list, and it refuses a list that does not obey it. So a consumer of a display list may
 * group commands by `depth` and by the `BEGIN_NODE`/`CONTENT`/`END_NODE` boundaries
 * without re-deriving anything.
 *
 * The `clear`/`reserve` pair is the paint list's, for the same reason: clearing keeps
 * capacity so a renderer that rebuilds every frame reuses one list. The next build
 * recomputes every command from scratch, because a partially updated list would be a
 * cache of conclusions nothing can validate.
 */
#ifndef ROBO_PAINT_DISPLAY_LIST_H
#define ROBO_PAINT_DISPLAY_LIST_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDisplayCommand.h"
#include "RoboPaint/RoboPaintDiagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Commands a list holds before its first growth. */
#define ROBO_PAINT_DISPLAY_INITIAL_CAPACITY 16u

/** An opaque, caller-owned display command list. */
typedef struct robo_paint_display_list robo_paint_display_list_t;

/**
 * What one build produced.
 *
 * A record by value, owned by nobody, and its fields are the *pairs* rather than the
 * totals: `nodes` against `records` is how a reader learns how much of the record list
 * was structure; `backgrounds` and `borders` against `draws` is how much of the document
 * puts a pixel down; `max_depth` is what a rasteriser sizing a clip stack reads.
 */
typedef struct robo_paint_display_list_stats {
    /** Paint operations consumed. */
    size_t records;
    /** Display commands produced. */
    size_t commands;
    /** `BEGIN_NODE` commands: one per box in the display list. */
    size_t nodes;
    /** Background commands. */
    size_t backgrounds;
    /** Border band commands. */
    size_t borders;
    /** Drawing commands: backgrounds plus border bands. */
    size_t draws;
    /** Deepest node seen. */
    uint32_t max_depth;
} robo_paint_display_list_stats_t;

/** Create an empty list with room for @ref ROBO_PAINT_DISPLAY_INITIAL_CAPACITY commands.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out_list; ROBO_OUT_OF_MEMORY. */
robo_result_t robo_paint_display_list_create(robo_paint_display_list_t **out_list,
                                       robo_paint_diag_t *diag);

/** Destroy @p list and its commands. The records and tree it was built from are not
 *  touched. Safe on NULL. */
void robo_paint_display_list_destroy(robo_paint_display_list_t *list);

/** Drop every command, keeping the capacity. Safe on NULL. */
void robo_paint_display_list_clear(robo_paint_display_list_t *list);

/** Make room for at least @p capacity commands.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p list; ROBO_OUT_OF_MEMORY. A
 *          request below the current capacity is success and a no-op. */
robo_result_t robo_paint_display_list_reserve(robo_paint_display_list_t *list,
                                       size_t capacity,
                                       robo_paint_diag_t *diag);

/**
 * Append @p command.
 *
 * **The boundary that keeps an invalid command out of a list** (the paint list's rule at
 * the same place): a command that is not a record (`robo_paint_display_command_is_valid`) is
 * refused, so every list a consumer reads is a list of commands. The command is copied by
 * value; the caller keeps ownership of its argument.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an invalid command
 *         (@p list is left untouched); ROBO_OUT_OF_MEMORY.
 */
robo_result_t robo_paint_display_list_append(robo_paint_display_list_t *list,
                                      const robo_paint_display_command_t *command,
                                      robo_paint_diag_t *diag);

/** Commands in @p list (0 for NULL). */
size_t robo_paint_display_list_count(const robo_paint_display_list_t *list);

/** Commands @p list can hold before growing (0 for NULL). */
size_t robo_paint_display_list_capacity(const robo_paint_display_list_t *list);

/** The command at @p index, by value.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND for an
 *          index at or past the count (@p out is untouched). */
robo_result_t robo_paint_display_list_at(const robo_paint_display_list_t *list,
                                  size_t index,
                                  robo_paint_display_command_t *out);

/** The commands, borrowed and **contiguous**, or NULL when the list is empty. Valid until
 *  the next append, a documented rule because nothing in a command points anywhere. */
const robo_paint_display_command_t *robo_paint_display_list_commands(const robo_paint_display_list_t *list);

/** Non-zero when @p a and @p b hold the same commands in the same order. Empty lists are
 *  equal; two NULLs are equal. */
int robo_paint_display_list_equal(const robo_paint_display_list_t *a, const robo_paint_display_list_t *b);

/** How many commands in @p list have kind @p kind (0 for NULL). */
size_t robo_paint_display_list_count_of_kind(const robo_paint_display_list_t *list,
                                       robo_paint_display_command_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DISPLAY_LIST_H */
