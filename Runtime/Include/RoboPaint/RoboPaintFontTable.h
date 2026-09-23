/*
 * The faces a display list's glyph runs were shaped with, as tokens (Phase 12.16).
 *
 * ## Why a table rather than a pointer
 *
 * A `PaintGlyph` is a glyph **id**, and an id means nothing on its own: 42 in Inter and 42 in any other
 * face are different letters. So a glyph run has to say which face produced it, and the two ways to say it
 * are a borrowed `FontFace *` in the record or a number that names an entry in a table the caller owns.
 *
 *   11.6   a display list is a value container: every record has no pointers in it, so a list can be
 *          copied, discarded and rebuilt, and "no dangling pointer" is true by construction rather than by
 *          discipline. That is argued at length in `RoboPaintText.h` and `RoboPaintOperation.h`, and it is
 *          what makes a list safe to hand between a frame's recorder and its rasteriser.
 *   12.16  a display list carries glyph ids, which need a face -- and a pointer would trade the property
 *          above for the convenience of not having this file.
 *
 * The table is the third option, and it is the goal's own suggestion ("prefer a reference/ID if the
 * display-list architecture already has resource tables"): the **caller** owns the faces, the **table** maps
 * an opaque token to each of them, and a record carries a token. A token that does not resolve is a
 * *countable miss* on the drawing side -- a run counted and skipped, with the number a reader can see --
 * rather than a face someone guessed at or a pointer that outlived what it pointed to.
 *
 * ## One table, two passes
 *
 * The token is only meaningful if the recorder and the rasteriser use the **same** table, exactly as the
 * text pool is only meaningful to the list that owns it. So the caller creates one, hands it to the
 * recording pass (`PaintContextSetFontTable`) so a run can register the face it shaped with, and hands the
 * same one to the drawing pass so the glyphs can be drawn. The degenerate case -- one document, one face --
 * is spelled by the sentinel `PaintFontTokenPrimary`: a record carrying it is drawn with the drawing pass's
 * own `face` argument, so a caller with exactly one face never builds a table at all. That is the desktop's
 * path today, and it is *provably* the same object on both sides because the same caller supplies both.
 *
 * In practice the recording-side table is the paint list's own (`PaintListFontTable`), created with the list
 * exactly as the pools are, so "a list is one object with one owner" survives the arrival of faces -- and a
 * display list is handed a **copy** of it by the builder (`PaintFontTableCopy`) rather than the table
 * itself, which is what keeps the snapshot property true for faces as well as for characters and glyphs. A
 * copy is not ceremony: a caller that clears and refills its table every frame assigns the same token to a
 * *different* face the moment one frame's attendance differs, and a glyph id drawn with the wrong face is a
 * different letter. Copying makes that impossible rather than unlikely.
 *
 * ## Identity, not equality
 *
 * A face is added and resolved by **address**, and a table is a small array searched linearly: the number of
 * faces a document is drawn with is the number of families in its stylesheet, which is small by nature. Two
 * separately-parsed faces with the same family are two faces here, which is the same rule
 * `RoboTextFontContextEqual` states one layer up, and the reason a token identifies an *object* rather than
 * a name.
 *
 * Contract and limitations: `Documentation/Runtime/Text/PaintBridge.md`.
 */
#ifndef ROBO_PAINT_FONT_TABLE_H
#define ROBO_PAINT_FONT_TABLE_H

#include <stddef.h>

#include "RoboBase/RoboError.h"

#include "RoboFont/RoboFontFace.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintText.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An opaque, caller-owned set of faces with a token for each. */
typedef struct PaintFontTable PaintFontTable;

/**
 * Create an empty table.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p outTable; RoboOutOfMemory.
 */
Result PaintFontTableCreate(PaintFontTable **outTable,
                            PaintDiag *diag);

/** Destroy @p table. The faces are the caller's and are not touched. Safe on NULL. */
void PaintFontTableDestroy(PaintFontTable *table);

/**
 * Drop every face, keeping the capacity. Safe on NULL.
 *
 * A per-frame recorder calls this before it records, exactly as it clears the list's pools: the tokens it
 * assigns are then the *same* tokens for the same faces next frame, which is what makes a token stable
 * enough to compare across frames.
 */
void PaintFontTableClear(PaintFontTable *table);

/**
 * The token for @p face, adding it if it is not there yet.
 *
 * **Idempotent**, and that is what makes it usable per run: a recorder that asked once per text run would
 * otherwise grow the table by one entry per run of the document. The same face object answers with the same
 * token for as long as the table holds it.
 *
 * @p face may not be NULL: a run with no face records no glyphs (`PaintFontTokenNone`) rather than a token
 * for nothing, which is the invariant `PaintTextRunIsValid` states.
 *
 * @return RoboOk and @p outToken, always >= `PaintFontTokenFirstAssigned`; RoboInvalidArgument for a NULL
 *         argument or a NULL @p face; RoboOutOfMemory.
 */
Result PaintFontTableAdd(PaintFontTable *table,
                         FontFace *face,
                         PaintFontToken *outToken,
                         PaintDiag *diag);

/**
 * The face @p token names.
 *
 * @return RoboOk and @p outFace; RoboInvalidArgument for a NULL argument; RoboNotFound when @p token names
 *         nothing in this table -- including both sentinels, which are not entries: a caller resolves
 *         `PaintFontTokenPrimary` by using the face it was handed, and `PaintFontTokenNone` means there is
 *         no face at all.
 */
Result PaintFontTableResolve(const PaintFontTable *table,
                             PaintFontToken token,
                             FontFace **outFace,
                             PaintDiag *diag);

/**
 * The token @p face was added under, if it is in the table.
 *
 * The read-only twin of @ref PaintFontTableAdd, for a caller that wants to *ask* rather than register --
 * a diagnostic naming the face a record drew with, or a test asserting that two runs share one.
 *
 * @return RoboOk and @p outToken; RoboInvalidArgument for a NULL argument or a NULL @p face; RoboNotFound
 *         when @p face is not in the table.
 */
Result PaintFontTableTokenOf(const PaintFontTable *table,
                             const FontFace *face,
                             PaintFontToken *outToken,
                             PaintDiag *diag);

/** Faces in @p table (0 for NULL). */
size_t PaintFontTableCount(const PaintFontTable *table);

/**
 * Copy @p token's face from @p source into @p table, handing back the token that names it here.
 *
 * The one door between two tables, and it exists for the same reason `PaintTextPoolCopy` does: the
 * builder converts a record list into a display list, so every token in the output has to be renumbered
 * into the output's own table. Doing that as a resolve-then-add pair at the call site would spell the two
 * rules below twice, and both are easy to get wrong:
 *
 *   a **sentinel carries across** unchanged. `PaintFontTokenNone` and `PaintFontTokenPrimary` are not
 *   entries, they mean the same thing in every table, and asking the source to resolve one would fail with
 *   the source table blamed for a token that was never its to hold.
 *   an **assigned** token is resolved in @p source and added here, which is idempotent -- two runs shaped
 *   with one face land on one token here, so copying a document's runs does not grow this table with the
 *   text any more than recording them grew the other one.
 *
 * @return RoboOk and @p outToken; RoboInvalidArgument for NULL arguments; RoboNotFound when @p token is an
 *         assigned token the **source** does not contain; RoboOutOfMemory.
 */
Result PaintFontTableCopy(PaintFontTable *table,
                         const PaintFontTable *source,
                         PaintFontToken token,
                         PaintFontToken *outToken,
                         PaintDiag *diag);

/**
 * The first face added, or NULL.
 *
 * **Not** `PaintFontTokenPrimary`'s answer: the sentinel names the *drawing pass's* own face, which the
 * table does not know. This door exists for the one caller that has both -- a test or a diagnostic that
 * wants to say "the primary case and a one-entry table are the same picture".
 */
FontFace *PaintFontTablePrimary(const PaintFontTable *table);

/** Human-readable one-line dump: the count and each token's face address. Complete or absent. */
Result PaintFontTableWrite(const PaintFontTable *table,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_FONT_TABLE_H */
