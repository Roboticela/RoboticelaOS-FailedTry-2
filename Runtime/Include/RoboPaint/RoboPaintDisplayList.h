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
 * list is a *container*: `PaintListAppend` validates each operation, so every list
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
#include "RoboPaint/RoboPaintText.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Commands a list holds before its first growth. */
#define RoboPaintDisplayInitialCapacity 16u

/** An opaque, caller-owned display command list. */
typedef struct PaintDisplayList PaintDisplayList;

/**
 * What one build produced.
 *
 * A record by value, owned by nobody, and its fields are the *pairs* rather than the
 * totals: `nodes` against `records` is how a reader learns how much of the record list
 * was structure; `backgrounds` and `borders` against `draws` is how much of the document
 * puts a pixel down; `maxDepth` is what a rasteriser sizing a clip stack reads.
 */
typedef struct PaintDisplayListStats {
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
    /** Text commands: one per run the record list carried (11.6). */
    size_t texts;
    /**
     * `PUSH_CLIP` commands: one per clip scope the record list opened (11.8), and therefore
     * one per `POP_CLIP` beside them. Counted rather than derived because the number is a
     * statement about the *document* -- how many boxes actually change the boundary in force --
     * and the two commands that carry it are otherwise indistinguishable from the structural
     * skeleton a reader is entitled to ignore.
     */
    size_t clipScopes;
    /**
     * `PUSH_OPACITY` commands: one per opacity scope the record list opened (11.9), and
     * therefore one per `POP_OPACITY` beside them.
     *
     * Beside `clipScopes` rather than folded into it, because the two are statements about
     * two different properties of the document and a document can have one without the other.
     * It is the display-list half of the paint pass's `opacityScopes`, and the two agree by
     * construction -- this build converts one record per command and drops none.
     */
    size_t opacityScopes;
    /**
     * Image commands: one per `IMAGE` record the record list carried (11.7).
     *
     * Beside `texts`, because the two are the drawing kinds whose *appearance* a command does
     * not own: a run names its characters in the list's pool and an image names a resource, and
     * a reader asking "how much of this document's pixels arrived from outside?" wants this
     * number rather than `draws`. It counts the record list's `IMAGE` operations exactly -- this
     * build converts one record per command and drops none -- so a discrepancy between the two
     * numbers is a builder bug rather than a document property.
     */
    size_t images;
    /** Drawing commands: backgrounds plus border bands plus text runs plus images. */
    size_t draws;
    /** Deepest node seen. */
    uint32_t maxDepth;
} PaintDisplayListStats;

/** Create an empty list with room for @ref RoboPaintDisplayInitialCapacity commands.
 *  @return RoboOk; RoboInvalidArgument for NULL @p outList; RoboOutOfMemory. */
Result PaintDisplayListCreate(PaintDisplayList **outList,
                                       PaintDiag *diag);

/** Destroy @p list and its commands. The records and tree it was built from are not
 *  touched. Safe on NULL. */
void PaintDisplayListDestroy(PaintDisplayList *list);

/** Drop every command, keeping the capacity. Safe on NULL. */
void PaintDisplayListClear(PaintDisplayList *list);

/** Make room for at least @p capacity commands.
 *  @return RoboOk; RoboInvalidArgument for a NULL @p list; RoboOutOfMemory. A
 *          request below the current capacity is success and a no-op. */
Result PaintDisplayListReserve(PaintDisplayList *list,
                                       size_t capacity,
                                       PaintDiag *diag);

/**
 * Append @p command.
 *
 * **The boundary that keeps an invalid command out of a list** (the paint list's rule at
 * the same place): a command that is not a record (`PaintDisplayCommandIsValid`) is
 * refused, so every list a consumer reads is a list of commands. The command is copied by
 * value; the caller keeps ownership of its argument.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an invalid command
 *         (@p list is left untouched); RoboOutOfMemory.
 */
Result PaintDisplayListAppend(PaintDisplayList *list,
                                      const PaintDisplayCommand *command,
                                      PaintDiag *diag);

/** Commands in @p list (0 for NULL). */
size_t PaintDisplayListCount(const PaintDisplayList *list);

/** Commands @p list can hold before growing (0 for NULL). */
size_t PaintDisplayListCapacity(const PaintDisplayList *list);

/**
 * The list's text pool, borrowed; NULL for a NULL list.
 *
 * A display list owns the characters its `DRAW_TEXT` commands index, exactly as the record
 * list owns the characters its `TEXT` records index -- because a command carries a span and
 * a span is meaningless without the pool it came from. The pool is filled by the builder,
 * which copies each run's code points across so that the display list is a **snapshot** in
 * the same sense the commands are: clearing the record list or re-laying the tree out
 * afterwards cannot change a character this list draws.
 *
 * The door a consumer reads through, with `PaintTextPoolRead`.
 */
PaintTextPool *PaintDisplayListTextPool(const PaintDisplayList *list);

/** The command at @p index, by value.
 *  @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound for an
 *          index at or past the count (@p out is untouched). */
Result PaintDisplayListAt(const PaintDisplayList *list,
                                  size_t index,
                                  PaintDisplayCommand *out);

/** The commands, borrowed and **contiguous**, or NULL when the list is empty. Valid until
 *  the next append, a documented rule because nothing in a command points anywhere. */
const PaintDisplayCommand *PaintDisplayListCommands(const PaintDisplayList *list);

/** Non-zero when @p a and @p b hold the same commands in the same order. Empty lists are
 *  equal; two NULLs are equal. */
int PaintDisplayListEqual(const PaintDisplayList *a, const PaintDisplayList *b);

/** How many commands in @p list have kind @p kind (0 for NULL). */
size_t PaintDisplayListCountOfKind(const PaintDisplayList *list,
                                       PaintDisplayCommandKind kind);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DISPLAY_LIST_H */
