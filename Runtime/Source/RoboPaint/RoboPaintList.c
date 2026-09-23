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
struct PaintList {
    PaintOperation *operations;
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
    PaintTextPool *text;
    /**
     * The **shaped** glyphs the list's `TEXT` operations name, owned here (Phase 12.16).
     *
     * The third array, and the same argument as the second one above: a glyph run is variable-length data,
     * so a record can only name it, and the pool is the one thing that knows whether a span names anything.
     * It is created with the list and destroyed with it, so the list is still exactly one object with one
     * owner.
     *
     * The two pools are cleared and grown together and **never** independently: an operation's character
     * span and its glyph span are two halves of one record, so a container that reset one and kept the
     * other would hold records whose halves disagree about which run they describe. That is why `clear`
     * clears both and every growth path takes both -- the invariant `PaintListAppend` then checks is the
     * same one this pairing establishes.
     */
    PaintGlyphPool *glyphs;
    /**
     * The faces those glyphs came from, owned here (Phase 12.16).
     *
     * The **fourth** array, and it is here rather than at the call site for the reason the other three are:
     * a glyph id means nothing without a face, so a record has to name one, and a token only means something
     * against a table. Owning the table makes the list the one object that can answer "was this face one I
     * recorded?" -- and it is what the display list's builder copies from, so a token is meaningful in the
     * list it was minted in and in the one it was copied to, and nowhere else.
     *
     * It is cleared with the pools and for the same reason: the next recording's runs register the faces they
     * shaped with, and the tokens they assign are then the same tokens for the same faces -- a property a
     * per-frame recorder relies on and gets from `PaintFontTableClear`.
     */
    PaintFontTable *fonts;
};

/**
 * Release the resources the first @p count operations of @p list hold.
 *
 * `clear` and `destroy` share it so that "a container gives back what it took" is one sentence in
 * one place. It walks **only** the live prefix, and it is safe on a list whose operations hold
 * nothing -- which is every list this build produced before 11.7 and every one it produces for a
 * document with no images.
 */
static void PaintListReleaseOperations(PaintList *list, size_t count)
{
    size_t index;

    for (index = 0u; index < count; index++) {
        PaintImageRelease(&list->operations[index].image);
    }
}

Result PaintListCreate(PaintList **outList, PaintDiag *diag)
{
    PaintList *list;
    PaintOperation *operations;

    if (!outList) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "out_list is NULL");
    }
    *outList = NULL;

    list = (PaintList *)RoboCalloc(1u, sizeof(*list));
    if (!list) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the paint list");
    }

    /*
     * The first block is taken at creation rather than at the first append, and the
     * reason is the contract `PaintListCapacity` states: a caller that sizes a
     * list before a pass may rely on `RoboPaintInitialCapacity` being there without a
     * branch. A list that allocated nothing until the first append would make that
     * door's answer 0 and mean "reserve first" at every call site.
     */
    operations = (PaintOperation *)RoboCalloc(RoboPaintInitialCapacity,
                                                       sizeof(*operations));
    if (!operations) {
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the paint list's first block");
    }
    list->operations = operations;
    list->count = 0u;
    list->capacity = RoboPaintInitialCapacity;
    /*
     * The text pool is taken at creation too, and for the same reason the first block is:
     * a caller that sizes a list before a pass may rely on the door answering rather than
     * on a lazily created pool that would make "add a run's characters" a call that can
     * fail for a reason unrelated to the characters. It costs one empty allocation for a
     * document with no text, which is the trade this module makes everywhere else.
     */
    if (PaintTextPoolCreate(&list->text, diag) != RoboOk) {
        RoboFree(operations);
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the paint list's text pool");
    }
    if (PaintGlyphPoolCreate(&list->glyphs, diag) != RoboOk) {
        PaintTextPoolDestroy(list->text);
        RoboFree(operations);
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the paint list's glyph pool");
    }
    if (PaintFontTableCreate(&list->fonts, diag) != RoboOk) {
        PaintGlyphPoolDestroy(list->glyphs);
        PaintTextPoolDestroy(list->text);
        RoboFree(operations);
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the paint list's font table");
    }
    *outList = list;
    return RoboOk;
}

void PaintListDestroy(PaintList *list)
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
    PaintListReleaseOperations(list, list->count);
    PaintTextPoolDestroy(list->text);
    PaintGlyphPoolDestroy(list->glyphs);
    PaintFontTableDestroy(list->fonts);
    RoboFree(list->operations);
    RoboFree(list);
}

void PaintListClear(PaintList *list)
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
    PaintListReleaseOperations(list, list->count);
    list->count = 0u;
    PaintTextPoolClear(list->text);
    PaintGlyphPoolClear(list->glyphs);
    PaintFontTableClear(list->fonts);
}

Result PaintListReserve(PaintList *list,
                                     size_t capacity,
                                     PaintDiag *diag)
{
    PaintOperation *grown;

    if (!list) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "list is NULL");
    }
    if (capacity <= list->capacity) {
        /* A request below the current capacity is success and a no-op, so a caller may
         * size a list before every pass without asking what it already holds. */
        return RoboOk;
    }
    grown = (PaintOperation *)Realloc(list->operations,
                                                   capacity * sizeof(*grown));
    if (!grown) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
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
    return RoboOk;
}

Result PaintListAppend(PaintList *list,
                                    const PaintOperation *operation,
                                    PaintDiag *diag)
{
    Result result;

    if (!list || !operation) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the list and the operation are required");
    }
    /*
     * The rule, checked once and at the container: an operation that is not a record is
     * refused and the list is left untouched. This is what makes **every** list a
     * consumer can reach a list of records -- there is no other door that puts an
     * operation in -- and it is why the traversal does not have to defend itself
     * against its own arithmetic at every append.
     */
    if (!PaintOperationIsValid(operation)) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the operation is not a record: it names no box, its boxes "
                               "are not nested, or its colour does not match its kind");
    }
    /*
     * The span rule, and it is the one thing about a text record only the container can
     * decide: the record says how many characters it draws and where they start, and this
     * is where the characters are. A span that is not a run in this pool is refused, so a
     * consumer that reads a text operation can read its characters and always get them.
     */
    if (operation->kind == RoboPaintOpText &&
        !PaintTextPoolContains(list->text, operation->text.span)) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the operation's text span is not a run in this list's pool");
    }
    /*
     * The glyphs, and this is the half of the same rule 12.16 added: a run recorded **with** a face names
     * glyphs in this list's glyph pool, so a consumer that draws from a text operation can read its glyphs
     * and always get them. An **empty** glyph span is the "no face" state and is not asked about here --
     * `PaintTextRunIsValid` already established that the span and the token agree, and a pool cannot
     * contain an empty span by design (`PaintGlyphPoolContains`).
     */
    if (operation->kind == RoboPaintOpText &&
        !PaintGlyphSpanIsEmpty(operation->text.glyphs) &&
        !PaintGlyphPoolContains(list->glyphs, operation->text.glyphs)) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the operation's glyph span is not a run in this list's glyph pool");
    }
    if (list->count == list->capacity) {
        /* Doubled, and floored at one step so that a list somehow created with no room
         * still grows rather than spinning: `RoboPaintInitialCapacity` is 16, so this
         * branch is unreachable in practice and correct if it ever is not. */
        size_t wanted = list->capacity < 1u ? 1u : list->capacity * 2u;

        result = PaintListReserve(list, wanted, diag);
        if (result != RoboOk) {
            return result;
        }
    }
    /*
     * The copy, and then the **one** place the ownership rule is stated: a stored operation
     * holds a reference of its own to whatever resource it names, so that the list can outlive
     * the tree it came from and the payload it was handed can stay borrowed
     * (`PaintImageRetain` is that sentence with a function around it). Copying first and
     * retaining the copy is deliberate -- the caller's operation is untouched, so a caller that
     * appends the same record twice ends up with two references and not one that was counted
     * twice.
     */
    list->operations[list->count] = *operation;
    PaintImageRetain(&list->operations[list->count].image);
    list->count++;
    return RoboOk;
}

size_t PaintListCount(const PaintList *list)
{
    return list ? list->count : 0u;
}

size_t PaintListCapacity(const PaintList *list)
{
    return list ? list->capacity : 0u;
}

Result PaintListAt(const PaintList *list,
                                size_t index,
                                PaintOperation *out)
{
    if (!list || !out) {
        return RoboInvalidArgument;
    }
    if (index >= list->count) {
        return RoboNotFound;
    }
    *out = list->operations[index];
    return RoboOk;
}

const PaintOperation *PaintListOperations(const PaintList *list)
{
    if (!list || list->count == 0u) {
        return NULL;
    }
    return list->operations;
}

int PaintListEqual(const PaintList *a, const PaintList *b)
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
        if (!PaintOperationEqual(&a->operations[index], &b->operations[index])) {
            return 0;
        }
    }
    return 1;
}

Result PaintListAddText(PaintList *list,
                                      const uint32_t *codePoints,
                                      size_t count,
                                      PaintTextSpan *outSpan,
                                      PaintDiag *diag)
{
    if (!list) {
        if (outSpan) {
            *outSpan = PaintTextSpanAt(0u);
        }
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "list is NULL");
    }
    return PaintTextPoolAppend(list->text, codePoints, count, outSpan, diag);
}

Result PaintListAddGlyphs(PaintList *list,
                          const PaintGlyph *glyphs,
                          size_t count,
                          PaintGlyphSpan *outSpan,
                          PaintDiag *diag)
{
    if (!list) {
        if (outSpan) {
            *outSpan = PaintGlyphSpanAt(0u);
        }
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "list is NULL");
    }
    return PaintGlyphPoolAppend(list->glyphs, glyphs, count, outSpan, diag);
}

PaintTextPool *PaintListTextPool(const PaintList *list)
{
    return list ? list->text : NULL;
}

PaintGlyphPool *PaintListGlyphPool(const PaintList *list)
{
    return list ? list->glyphs : NULL;
}

PaintFontTable *PaintListFontTable(const PaintList *list)
{
    return list ? list->fonts : NULL;
}

size_t PaintListCountOfKind(const PaintList *list, PaintOpKind kind)
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
