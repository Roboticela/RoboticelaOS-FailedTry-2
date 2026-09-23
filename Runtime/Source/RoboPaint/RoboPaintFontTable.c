/*
 * The paint font table (Phase 12.16) — see RoboPaintFontTable.h.
 *
 * A small array of borrowed faces and one rule: a token is a face's **position in it**, shifted past the two
 * sentinels. That is the whole engine, and the four decisions worth stating are:
 *
 *   **Idempotence.** `PaintFontTableAdd` of a face already in the table answers with the token it already
 *   has. A recorder asks once per text run, so a document with two hundred runs of one face would otherwise
 *   add two hundred entries -- and the table would grow with the *text* rather than with the faces.
 *
 *   **Identity, not equality.** Entries are compared by **address**. Two separately-parsed faces with the
 *   same family are two faces, which is the rule `RoboTextFontContextEqual` states one layer up and the
 *   reason a token names an object: a glyph id drawn with the wrong *instance* of a face is still a
 *   different letter wherever the instances differ at all.
 *
 *   **Clear keeps the storage.** `PaintFontTableClear` moves the count, so the same faces land on the same
 *   tokens next frame -- which is what makes a token worth comparing across frames, and what keeps a
 *   per-frame recorder from reallocating.
 *
 *   **A miss is an answer.** `Resolve` of a token that names nothing reports `RoboNotFound`, including both
 *   sentinels: they are not entries this table could hold, because `PaintFontTokenPrimary` names the
 *   *drawing pass's* own face (which the table never sees) and `PaintFontTokenNone` names no face at all.
 *   A caller that flattened those into "some face" would draw one document's glyphs with another's font.
 */

#include "RoboPaint/RoboPaintFontTable.h"

#include "RoboMem/RoboMem.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Faces a table holds before its first growth. */
#define RoboPaintFontInitialCapacity 4u

struct PaintFontTable {
    FontFace **faces;
    /** Faces held. */
    size_t count;
    /** Faces the array has room for. */
    size_t capacity;
};

Result PaintFontTableCreate(PaintFontTable **outTable,
                            PaintDiag *diag)
{
    PaintFontTable *table;

    if (!outTable) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "out_table is NULL");
    }
    *outTable = NULL;
    table = (PaintFontTable *)RoboCalloc(1u, sizeof(*table));
    if (!table) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the font table");
    }
    table->faces = (FontFace **)RoboCalloc(RoboPaintFontInitialCapacity, sizeof(*table->faces));
    if (!table->faces) {
        RoboFree(table);
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                               "could not allocate the font table's first block");
    }
    table->capacity = RoboPaintFontInitialCapacity;
    *outTable = table;
    return RoboOk;
}

void PaintFontTableDestroy(PaintFontTable *table)
{
    if (!table) {
        return;
    }
    /* The faces are the caller's, exactly as a display list's resources are: this table holds borrowed
     * pointers, so releasing them is releasing the array that holds them. */
    RoboFree(table->faces);
    RoboFree(table);
}

void PaintFontTableClear(PaintFontTable *table)
{
    if (!table) {
        return;
    }
    table->count = 0u;
}

Result PaintFontTableAdd(PaintFontTable *table,
                         FontFace *face,
                         PaintFontToken *outToken,
                         PaintDiag *diag)
{
    PaintFontToken existing;

    if (outToken) {
        *outToken = PaintFontTokenNone;
    }
    if (!table || !outToken) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the table and the output token are required");
    }
    if (!face) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "a face is required to name one");
    }
    /* Already here? Then this is the same answer, and the table does not grow. The linear scan is the point
     * rather than a shortcut: a document is drawn with as many faces as its stylesheet names, which is small
     * by nature, and a hash would be a second identity for the same objects. */
    if (PaintFontTableTokenOf(table, face, &existing, diag) == RoboOk) {
        *outToken = existing;
        return RoboOk;
    }
    if (table->count >= (size_t)0xFFFFFFFFu - PaintFontTokenFirstAssigned) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the font table cannot name another face");
    }
    if (table->count + 1u > table->capacity) {
        size_t wanted = table->capacity < 1u ? 1u : table->capacity * 2u;
        FontFace **grown = (FontFace **)Realloc(table->faces, wanted * sizeof(*grown));

        if (!grown) {
            return RoboPaintDiag(diag, RoboPaintDomainList, RoboOutOfMemory,
                                   "could not grow the font table");
        }
        memset(grown + table->capacity, 0, (wanted - table->capacity) * sizeof(*grown));
        table->faces = grown;
        table->capacity = wanted;
    }
    table->faces[table->count] = face;
    table->count++;
    *outToken = (PaintFontToken)(PaintFontTokenFirstAssigned + (table->count - 1u));
    return RoboOk;
}

Result PaintFontTableResolve(const PaintFontTable *table,
                             PaintFontToken token,
                             FontFace **outFace,
                             PaintDiag *diag)
{
    size_t index;

    if (outFace) {
        *outFace = NULL;
    }
    if (!table || !outFace) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the table and the output face are required");
    }
    /* Both sentinels are misses, and each for its own reason: `None` names no face, and `Primary` names one
     * this table has never seen. Leaving them to "not found in the array" would be right by accident. */
    if (token < PaintFontTokenFirstAssigned) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboNotFound,
                               "the token names no face in this table");
    }
    index = (size_t)(token - PaintFontTokenFirstAssigned);
    if (index >= table->count) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboNotFound,
                               "the token names no face in this table");
    }
    *outFace = table->faces[index];
    return RoboOk;
}

Result PaintFontTableTokenOf(const PaintFontTable *table,
                             const FontFace *face,
                             PaintFontToken *outToken,
                             PaintDiag *diag)
{
    if (outToken) {
        *outToken = PaintFontTokenNone;
    }
    if (!table || !outToken) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the table and the output token are required");
    }
    if (!face) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "a face is required to name one");
    }
    for (size_t index = 0u; index < table->count; index++) {
        if (table->faces[index] == face) {
            *outToken = (PaintFontToken)(PaintFontTokenFirstAssigned + index);
            return RoboOk;
        }
    }
    return RoboPaintDiag(diag, RoboPaintDomainList, RoboNotFound,
                           "the face is not in this table");
}

Result PaintFontTableCopy(PaintFontTable *table,
                         const PaintFontTable *source,
                         PaintFontToken token,
                         PaintFontToken *outToken,
                         PaintDiag *diag)
{
    FontFace *face = NULL;

    if (outToken) {
        *outToken = PaintFontTokenNone;
    }
    if (!table || !source || !outToken) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboInvalidArgument,
                               "the table, the source and the output token are required");
    }
    /*
     * The two sentinels carry straight across, and they must: they are *not* entries, they mean the same
     * thing in every table, and resolving one would fail with the table it came from wrongly blamed. A
     * caller copying a run's token has one question -- "is this token an entry?" -- and this is where the
     * answer is, once.
     */
    if (token < PaintFontTokenFirstAssigned) {
        *outToken = token;
        return RoboOk;
    }
    if (PaintFontTableResolve(source, token, &face, diag) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainList, RoboNotFound,
                               "the token names no face in the source table");
    }
    return PaintFontTableAdd(table, face, outToken, diag);
}

size_t PaintFontTableCount(const PaintFontTable *table)
{
    return table ? table->count : 0u;
}

FontFace *PaintFontTablePrimary(const PaintFontTable *table)
{
    if (!table || table->count == 0u) {
        return NULL;
    }
    return table->faces[0];
}

Result PaintFontTableWrite(const PaintFontTable *table,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength)
{
    PaintWriter written;

    if (outLength) {
        *outLength = 0u;
    }
    if (!table || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    PaintWriterInit(&written, buffer, capacity);
    PaintWriterString(&written, "faces ");
    PaintWriterU64(&written, (uint64_t)table->count);
    for (size_t index = 0u; index < table->count; index++) {
        PaintWriterString(&written, " token ");
        PaintWriterU64(&written, (uint64_t)(PaintFontTokenFirstAssigned + index));
        PaintWriterString(&written, " -> ");
        /* The **address**, because that is the identity a token carries: a reader comparing two dumps needs
         * the same object to spell the same way, and a name would spell two instances alike. */
        PaintWriterString(&written, "0x");
        PaintWriterU64(&written, (uint64_t)(uintptr_t)table->faces[index]);
    }
    if (outLength) {
        *outLength = written.length;
    }
    if (written.length + 1u > capacity) {
        /* Complete or absent, like every dump in this module: a caller is told the room it would need
         * rather than handed a prefix that reads as a different table. */
        buffer[0] = '\0';
        return RoboNotSupported;
    }
    return RoboOk;
}
