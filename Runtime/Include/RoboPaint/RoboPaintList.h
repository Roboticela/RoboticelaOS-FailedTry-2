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
 *
 * Since 11.6 a list owns **two** arrays, and the second one is the reason the first one's
 * "no pointers in it" rule can still hold. A text operation carries characters, which are
 * variable-length data no fixed record can hold, so the characters live in a **text pool**
 * (`RoboPaintText.h`) and the record carries a span into it -- two numbers, exactly as
 * pointer-free as the rest of the record. The pool is created with the list, cleared with
 * it, grown as `add_text` appends, and destroyed with it, so a list is still one object
 * with one owner and one lifetime; and the span rule is checked **here**, at the
 * container, because "does this span name characters in this pool?" is the one question a
 * record cannot answer about itself. Characters enter only through `add_text`, so a span
 * in a record in a list always names the pool that list owns.
 *
 * Since 12.16 a list owns **three** arrays, and the third one is the second one's argument
 * applied to a second kind of variable-length data. A shaped text run is a sequence of
 * glyphs, so the glyphs live in a **glyph pool** (`RoboPaintText.h`, beside the text pool it
 * belongs with) and a text operation carries a span into it as well. The two pools are the two
 * halves of one record and are therefore created, cleared and grown together -- a list
 * that reset its characters and kept its glyphs would hold records whose halves disagree
 * about which run they describe. The glyph span is checked at `append` for the same reason
 * the character span is: "do these glyphs exist in this list?" is a question about the
 * container, not about the record.
 *
 * The fourth array is the **faces** (`PaintListFontTable`), because a glyph id is a number in one font and
 * nothing in another: a record names its face by a token, and the table that mints tokens is the list's, so
 * a token in a record is always one the list it sits in can resolve.
 */
#ifndef ROBO_PAINT_LIST_H
#define ROBO_PAINT_LIST_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintFontTable.h"
#include "RoboPaint/RoboPaintOperation.h"
#include "RoboPaint/RoboPaintText.h"
#include "RoboPaint/RoboPaintTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Operations a list holds before its first growth. */
#define RoboPaintInitialCapacity 16u

/** An opaque, caller-owned operation list. */
typedef struct PaintList PaintList;

/**
 * Create an empty list with room for @ref RoboPaintInitialCapacity operations.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p outList; RoboOutOfMemory.
 */
Result PaintListCreate(PaintList **outList, PaintDiag *diag);

/** Destroy @p list and its operations. The tree it was built from is not touched.
 *  Safe on NULL. */
void PaintListDestroy(PaintList *list);

/**
 * Drop every operation, keeping the capacity.
 *
 * The door a per-frame renderer uses, and the reason it exists rather than "destroy and
 * create": the operations were a conclusion about one state of the tree and the next
 * pass concludes again from scratch, so what is reused is the *storage* and never the
 * answers. Safe on NULL.
 */
void PaintListClear(PaintList *list);

/**
 * Make room for at least @p capacity operations.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p list; RoboOutOfMemory. A
 *         request below the current capacity succeeds and changes nothing, so a caller
 *         may size a list before every pass without a branch.
 */
Result PaintListReserve(PaintList *list,
                                     size_t capacity,
                                     PaintDiag *diag);

/**
 * Append @p operation.
 *
 * **The boundary that keeps an invalid operation out of a list**, and the reason the
 * validation lives here rather than in the traversal: the traversal is one caller, and
 * a rule enforced in the container is a rule no future caller can forget -- the same
 * argument `RoboLayoutTree.h` makes for validating a parent link at the door rather
 * than in the builder. An operation that is not a record (see
 * `PaintOperationIsValid`) is **refused**, which is what makes every list a
 * consumer reads a list of records, and is the "geometry safety" requirement satisfied
 * at the only place it can be satisfied once instead of at every call site.
 *
 * A `TEXT` operation is held to a second rule, and it is the one this container is the
 * only possible place for: its span must lie **inside this list's pool**
 * (`PaintTextPoolContains`). A record's validity is a function of the record, so
 * a span that names nothing cannot be refused there -- but here, where the characters
 * are, "these are the characters this operation draws" is decidable, and refusing the
 * alternative is what makes every text operation a consumer reads one whose span names
 * real code points rather than whatever the pool happens to hold at those two numbers.
 *
 * The operation is copied by value; the caller keeps ownership of its argument.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, an operation that is not a
 *         record, a `TEXT` operation whose character span is not a run in this list's
 *         text pool, or one whose **non-empty** glyph span is not a run in this list's
 *         glyph pool (@p list is left untouched); RoboOutOfMemory.
 */
Result PaintListAppend(PaintList *list,
                                    const PaintOperation *operation,
                                    PaintDiag *diag);

/** Operations in @p list (0 for NULL). */
size_t PaintListCount(const PaintList *list);

/** Operations @p list can hold before growing (0 for NULL). */
size_t PaintListCapacity(const PaintList *list);

/**
 * Append @p count code points to the list's text pool and hand back the span naming them.
 *
 * The **only** way characters enter a list, so every span in every record of a list names
 * characters that list owns. A traversal reads a text run's characters, calls this, and
 * puts the span it returns into the operation it appends -- two calls that together state
 * one thing, which is what makes the append's span check a rule rather than a formality.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, a NULL @p codePoints with a
 *         non-zero count, or a count of zero (an empty span is a position, not a run);
 *         RoboOutOfMemory.
 */
Result PaintListAddText(PaintList *list,
                                      const uint32_t *codePoints,
                                      size_t count,
                                      PaintTextSpan *outSpan,
                                      PaintDiag *diag);

/**
 * The list's text pool, borrowed; NULL for a NULL list.
 *
 * The door a consumer needs to read a text operation's characters
 * (`PaintTextPoolRead`) and the door the display list's builder needs to copy them
 * across. It is borrowed and it is the list's: a pool that outlived its list would be a
 * pool whose spans name nothing.
 */
PaintTextPool *PaintListTextPool(const PaintList *list);

/**
 * Append @p count shaped glyphs to the list's glyph pool and hand back the span naming them.
 *
 * The **only** way glyphs enter a list, and the same pairing as `PaintListAddText`: a traversal
 * shapes a run, calls this, and puts the span it returns into the operation it appends. The two
 * calls together state one thing, which is what makes `append`'s glyph-span check a rule rather
 * than a formality.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, a NULL @p glyphs with a non-zero
 *         count, or a count of zero (an empty span is a position, not a run); RoboOutOfMemory.
 */
Result PaintListAddGlyphs(PaintList *list,
                          const PaintGlyph *glyphs,
                          size_t count,
                          PaintGlyphSpan *outSpan,
                          PaintDiag *diag);

/**
 * The list's glyph pool, borrowed; NULL for a NULL list.
 *
 * The door a consumer needs to read a text operation's glyphs (`PaintGlyphPoolRead`) and the door
 * the display list's builder needs to copy them across. Borrowed, and it is the list's: a pool that
 * outlived its list would be a pool whose spans name nothing.
 */
PaintGlyphPool *PaintListGlyphPool(const PaintList *list);

/**
 * The list's font table, borrowed; NULL for a NULL list.
 *
 * The faces its glyph spans were shaped with, as tokens (`RoboPaintFontTable.h`), owned here for the same
 * reason the pools are: a run registers the face it shaped with (`PaintFontTableAdd`) through this door and
 * the record it appends carries the token, so **the table a record's token is meaningful in is always the
 * table of the list that record is in**. That is the property the display list's builder depends on when it
 * copies each token across (`PaintFontTableCopy`), and the reason a caller with exactly one face never
 * builds a table at all: it records `PaintFontTokenPrimary` and the drawing pass uses its own `face`.
 */
PaintFontTable *PaintListFontTable(const PaintList *list);

/**
 * The operation at @p index, by value.
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound for an
 *         index at or past the count (@p out is untouched).
 */
Result PaintListAt(const PaintList *list,
                                size_t index,
                                PaintOperation *out);

/**
 * The operations, borrowed and **contiguous**, or NULL when the list is empty.
 *
 * The door a rasteriser wants -- a pointer and a count, no per-operation call -- and the
 * reason the record has no pointers in it: the array moves when the list grows, which is
 * a documented lifetime rule ("valid until the next append") rather than a hazard,
 * because nothing in an operation points anywhere.
 */
const PaintOperation *PaintListOperations(const PaintList *list);

/**
 * Non-zero when @p a and @p b hold the same operations in the same order.
 *
 * The determinism comparison, as one call: a suite that compared two lists by walking
 * both would be re-implementing this and would report "the fourth operation differs" as
 * its own bug at least once. Empty lists are equal; two NULLs are equal.
 */
int PaintListEqual(const PaintList *a, const PaintList *b);

/** How many operations in @p list have kind @p kind (0 for NULL). */
size_t PaintListCountOfKind(const PaintList *list, PaintOpKind kind);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_LIST_H */
