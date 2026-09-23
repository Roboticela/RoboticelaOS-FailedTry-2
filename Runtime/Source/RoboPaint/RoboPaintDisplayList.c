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
 * `PaintDisplayCommandIsValid`, the one place that decides what a command is.
 */

#include "RoboPaint/RoboPaintDisplayList.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/** The list: a contiguous array, its two counts, and the characters its text commands
 *  index (11.6). */
struct PaintDisplayList {
    PaintDisplayCommand *commands;
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
    PaintTextPool *text;
    /**
     * The **shaped** glyphs this list's `DRAW_TEXT` commands name, owned here (12.16).
     *
     * The record list's third array at this artifact's element type, and the snapshot argument
     * is the whole reason it is here: a command's glyph span names the pool the *builder* filled
     * it from, and that pool belongs to the record list. Copying the glyphs is what makes "a
     * display list is a snapshot of shaped text" true at the same strength as it already was for
     * geometry -- a display list can outlive the paint pass that produced it.
     */
    PaintGlyphPool *glyphs;
    /**
     * The faces this list's `DRAW_TEXT` commands name, owned here (12.16).
     *
     * The same snapshot at one remove: a glyph says *which* glyph in *which* face, and the face
     * is the resource a renderer needs to rasterize it. The table is a value list of borrowed
     * faces and pointer-free tokens (`RoboPaintFontTable.h`), so copying it across gives this
     * list its own view of the faces its commands draw with, and gives each command a token that
     * is meaningful in **this** list's table rather than in the record list's.
     */
    PaintFontTable *fonts;
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
static void PaintDisplayListReleaseCommands(PaintDisplayList *list,
                                                     size_t count)
{
    size_t index;

    for (index = 0u; index < count; index++) {
        PaintImageRelease(&list->commands[index].image);
    }
}

Result PaintDisplayListCreate(PaintDisplayList **outList,
                                       PaintDiag *diag)
{
    PaintDisplayList *list;
    PaintDisplayCommand *commands;

    if (!outList) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "out_list is NULL");
    }
    *outList = NULL;

    list = (PaintDisplayList *)RoboCalloc(1u, sizeof(*list));
    if (!list) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the display list");
    }

    commands = (PaintDisplayCommand *)RoboCalloc(RoboPaintDisplayInitialCapacity,
                                                     sizeof(*commands));
    if (!commands) {
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the display list's first block");
    }
    list->commands = commands;
    list->count = 0u;
    list->capacity = RoboPaintDisplayInitialCapacity;
    if (PaintTextPoolCreate(&list->text, diag) != RoboOk) {
        RoboFree(commands);
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the display list's text pool");
    }
    if (PaintGlyphPoolCreate(&list->glyphs, diag) != RoboOk) {
        PaintTextPoolDestroy(list->text);
        RoboFree(commands);
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the display list's glyph pool");
    }
    if (PaintFontTableCreate(&list->fonts, diag) != RoboOk) {
        PaintGlyphPoolDestroy(list->glyphs);
        PaintTextPoolDestroy(list->text);
        RoboFree(commands);
        RoboFree(list);
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not allocate the display list's font table");
    }
    *outList = list;
    return RoboOk;
}

void PaintDisplayListDestroy(PaintDisplayList *list)
{
    if (!list) {
        return;
    }
    /*
     * Since 11.7 a command can name a **retained resource**, so the release is the paint list's
     * walk and then the two frees -- including the text pool, whose code points are values and
     * need no walk of their own.
     */
    PaintDisplayListReleaseCommands(list, list->count);
    PaintTextPoolDestroy(list->text);
    PaintGlyphPoolDestroy(list->glyphs);
    PaintFontTableDestroy(list->fonts);
    RoboFree(list->commands);
    RoboFree(list);
}

void PaintDisplayListClear(PaintDisplayList *list)
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
    PaintDisplayListReleaseCommands(list, list->count);
    list->count = 0u;
    PaintTextPoolClear(list->text);
    PaintGlyphPoolClear(list->glyphs);
    PaintFontTableClear(list->fonts);
}

Result PaintDisplayListReserve(PaintDisplayList *list,
                                       size_t capacity,
                                       PaintDiag *diag)
{
    PaintDisplayCommand *grown;

    if (!list) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "list is NULL");
    }
    if (capacity <= list->capacity) {
        return RoboOk;
    }
    grown = (PaintDisplayCommand *)Realloc(list->commands,
                                                   capacity * sizeof(*grown));
    if (!grown) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboOutOfMemory,
                               "could not grow the display list");
    }
    /* Zeroed from the old end onward, so the region a caller can reach through
     * `commands()` is deterministic past the count. */
    memset(grown + list->capacity, 0, (capacity - list->capacity) * sizeof(*grown));
    list->commands = grown;
    list->capacity = capacity;
    return RoboOk;
}

Result PaintDisplayListAppend(PaintDisplayList *list,
                                      const PaintDisplayCommand *command,
                                      PaintDiag *diag)
{
    Result result;

    if (!list || !command) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "the list and the command are required");
    }
    if (!PaintDisplayCommandIsValid(command)) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "the command is not a record: it names no node, its "
                               "rectangle is not one, or its colour does not match its "
                               "kind");
    }
    /* The paint list's span rule at the same boundary, and for the same reason: a command
     * says how many characters it draws and where they start, and this is where the
     * characters are -- so a `DRAW_TEXT` a consumer reads can always be read through. */
    if (command->kind == RoboPaintDisplayCmdDrawText &&
        !PaintTextPoolContains(list->text, command->text.span)) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "the command's text span is not a run in this list's pool");
    }
    /*
     * The glyphs and the face, and they are one rule: the command's characters are the ones this
     * list copied, its glyphs are the ones this list copied, and its face token is an index into
     * **this** list's font table. A consumer that draws a command therefore reads all three from
     * the list it was handed, which is the snapshot property stated as three checks rather than a
     * promise. Both are skipped for the "no face" state, whose glyph span is empty and whose token
     * is no token (`PaintTextRunIsValid` is where that pairing was established).
     */
    if (command->kind == RoboPaintDisplayCmdDrawText &&
        !PaintGlyphSpanIsEmpty(command->text.glyphs) &&
        !PaintGlyphPoolContains(list->glyphs, command->text.glyphs)) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "the command's glyph span is not a run in this list's glyph pool");
    }
    if (command->kind == RoboPaintDisplayCmdDrawText &&
        command->text.fontToken >= PaintFontTokenFirstAssigned) {
        FontFace *face = NULL;

        if (PaintFontTableResolve(list->fonts, command->text.fontToken, &face, diag) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                                   "the command's font token is not a face in this list's table");
        }
    }
    if (list->count == list->capacity) {
        size_t wanted = list->capacity < 1u ? 1u : list->capacity * 2u;

        result = PaintDisplayListReserve(list, wanted, diag);
        if (result != RoboOk) {
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
    PaintImageRetain(&list->commands[list->count].image);
    list->count++;
    return RoboOk;
}

size_t PaintDisplayListCount(const PaintDisplayList *list)
{
    return list ? list->count : 0u;
}

size_t PaintDisplayListCapacity(const PaintDisplayList *list)
{
    return list ? list->capacity : 0u;
}

Result PaintDisplayListAt(const PaintDisplayList *list,
                                  size_t index,
                                  PaintDisplayCommand *out)
{
    if (!list || !out) {
        return RoboInvalidArgument;
    }
    if (index >= list->count) {
        return RoboNotFound;
    }
    *out = list->commands[index];
    return RoboOk;
}

const PaintDisplayCommand *PaintDisplayListCommands(const PaintDisplayList *list)
{
    if (!list || list->count == 0u) {
        return NULL;
    }
    return list->commands;
}

int PaintDisplayListEqual(const PaintDisplayList *a, const PaintDisplayList *b)
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
        if (!PaintDisplayCommandEqual(&a->commands[index], &b->commands[index])) {
            return 0;
        }
    }
    return 1;
}

PaintTextPool *PaintDisplayListTextPool(const PaintDisplayList *list)
{
    return list ? list->text : NULL;
}

PaintGlyphPool *PaintDisplayListGlyphPool(const PaintDisplayList *list)
{
    return list ? list->glyphs : NULL;
}

PaintFontTable *PaintDisplayListFontTable(const PaintDisplayList *list)
{
    return list ? list->fonts : NULL;
}

size_t PaintDisplayListCountOfKind(const PaintDisplayList *list,
                                       PaintDisplayCommandKind kind)
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
