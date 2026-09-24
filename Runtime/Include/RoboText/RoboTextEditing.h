/*
 * Text editing: the logical editing contract (Phase 12.22) — see
 * `Documentation/Runtime/Text/TextEditing.md`.
 *
 * ## The rule this header exists to state
 *
 *     Editing operates on logical source text; shaping and rendering are downstream consequences of text
 *     mutations.
 *
 * So every door below takes and returns `RoboTextRange` — offsets into the **source text** — and not one of
 * them takes a glyph index, a glyph run, a cluster index or a pixel. A caller that deletes "the `ffi`
 * ligature" deletes the scalars `f`, `f`, `i`; whether a ligature reappears is the shaper's answer on the
 * next shape, not an edit. That is not a stylistic preference: a glyph index cannot be edited, because the
 * source text that produced it would be left describing something the font no longer draws.
 *
 * ## The shape of an edit
 *
 * Every high-level operation reduces to one door — @ref RoboTextEditorReplace — and afterwards the editor
 * reports one record:
 *
 *     TextEditCommand ──▶ TextEditor ──▶ TextEditOperation ──▶ TextEditResult
 *                                            │                      │
 *                                            │                      └─▶ TextEditInvalidation
 *                                            └─▶ the deleted and inserted text, so undo needs no layout
 *
 * The **operation** is the durable record: the range it replaced, the text it removed and the text it put
 * there, each of the last two an owned buffer. The **result** is the transient one: what changed, where the
 * caret and selection went, and what a consumer must redo (analysis, shaping, layout, paint, selection
 * mapping).
 *
 * ## Boundaries, and what is deliberately refused
 *
 * A backspace cannot be `offset - 1`. A scalar is the unit of this module's index space, so stepping by a
 * scalar can never split a UTF-8 sequence — but it *can* split a cluster, and a cluster is what an editing
 * unit has to be. @ref RoboTextEditBoundaryCharacter is that unit at its weakest: one scalar, extended over
 * the combining marks that follow a base, which is 12.14's cluster rule. @ref RoboTextEditBoundaryCluster
 * asks a **shaped run list** for the same answer, so an Arabic joining cluster or an Indic conjunct is one
 * unit where the shaping layer says it is.
 *
 * Two of the spec's boundaries are **refused rather than faked**, because nothing in this build can answer
 * them: `Word` needs UAX #29's word segmentation and `LineStart`/`LineEnd` need a layout that this layer
 * does not have. @ref RoboTextEditBoundaryResolve answers `RoboNotSupported` for them and
 * @ref RoboTextEditingCapabilities says so field by field, exactly as the module reports every other absent
 * algorithm — a `position ± 1` word movement would be a wrong answer dressed as one.
 *
 * ## Composition is a logical range, never a glyph range
 *
 * A composition that is running is a range in the source text plus the text currently in it, and the editor
 * keeps what was there **before** it began, so cancelling restores that slice rather than guessing what the
 * glyphs used to be. The committed text and the composing text are distinguished by being different
 * records, not by being different glyphs.
 *
 * ## What is deliberately not here
 *
 * No `RoboInput`, no keyboard scan codes, no mouse events, no clipboard backend, no undo/redo *manager*, no
 * spell checking, autocorrect or prediction, no rich-text attributes, no DOM `contenteditable` and no
 * accessibility editing. The editor is the layer those systems call; it reads no device and owns no UI.
 */
#ifndef ROBO_TEXT_EDITING_H
#define ROBO_TEXT_EDITING_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextAnalysis.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextRunList.h"
#include "RoboText/RoboTextSelection.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

/**
 * An editing command: what a caller asks for, named once, in input-neutral terms.
 *
 * This is the boundary 12.22.39/40 asks for. A key event, a menu item, a shortcut, an IME action and a
 * future accessibility action all reach the editor as one of these values, and **no keyboard scan code
 * appears anywhere in this module**. The enumerators are the whole vocabulary rather than the subset this
 * build executes: `Undo` and `Redo` are here because 12.22.27 promises the *record* an undo manager needs,
 * and the editor refuses them by name until 12.23 owns a history.
 */
typedef enum RoboTextEditCommand {
    /** No command. @ref RoboTextEditCommandIsKnown is false for it. */
    RoboTextEditCommandNone = 0,
    /** Insert text at the caret, replacing the selection if there is one. */
    RoboTextEditCommandInsertText,
    /** Delete the logical unit before the caret. */
    RoboTextEditCommandDeleteBackward,
    /** Delete the logical unit after the caret. */
    RoboTextEditCommandDeleteForward,
    /** Move the caret one logical unit left, extending the selection when asked. */
    RoboTextEditCommandMoveLeft,
    /** Move the caret one logical unit right. */
    RoboTextEditCommandMoveRight,
    /** Move the caret one visual line up. Needs a layout: refused here. */
    RoboTextEditCommandMoveUp,
    /** Move the caret one visual line down. Needs a layout: refused here. */
    RoboTextEditCommandMoveDown,
    /** Move the caret to the start of its line. Needs a layout: refused here. */
    RoboTextEditCommandMoveToLineStart,
    /** Move the caret to the end of its line. Needs a layout: refused here. */
    RoboTextEditCommandMoveToLineEnd,
    /** Extend the selection one logical unit left. */
    RoboTextEditCommandSelectLeft,
    /** Extend the selection one logical unit right. */
    RoboTextEditCommandSelectRight,
    /** Select the whole text. */
    RoboTextEditCommandSelectAll,
    /** Read the selected text; the caller puts it wherever it likes. */
    RoboTextEditCommandCopy,
    /** Copy and then delete the selection. */
    RoboTextEditCommandCut,
    /** Insert text the caller already holds — a paste is an insert. */
    RoboTextEditCommandPaste,
    /** Undo the last operation. The record exists; the manager is 12.23's. */
    RoboTextEditCommandUndo,
    /** Redo. See @ref RoboTextEditCommandUndo. */
    RoboTextEditCommandRedo
} RoboTextEditCommand;

/** Non-zero when @p command is one of the names above. False for @ref RoboTextEditCommandNone. */
int RoboTextEditCommandIsKnown(RoboTextEditCommand command);

/** Name of @p command (`"delete_backward"`); `"unknown"` for a value from beyond the enum. Never NULL. */
const char *RoboTextEditCommandName(RoboTextEditCommand command);

/* ------------------------------------------------------------------ */
/* Caret                                                               */
/* ------------------------------------------------------------------ */

/**
 * The editor's caret: a **logical** position, an affinity and two flags.
 *
 * There is deliberately no geometry here. A caret's visual form is a query against a selection map
 * (12.21's @ref RoboTextSelectionMapCaret), which needs the placements a layout pass supplies; the editor
 * has no layout and must not invent coordinates it cannot see. `visible`/`enabled` are the two facts only
 * the editing layer can know — whether the caret should be drawn at all, and whether it may move.
 */
typedef struct RoboTextCaret {
    /** Where in the source text the caret is. */
    RoboTextPosition position;
    /** Which of two visual locations a shared boundary meant (12.21). */
    RoboTextAffinity affinity;
    /** Non-zero when the caret should be drawn. */
    int visible;
    /** Non-zero when the caret is allowed to move. */
    int enabled;
} RoboTextCaret;

/** The caret at @p position with @p affinity, visible and enabled — what a fresh editor installs. */
RoboTextCaret RoboTextCaretMake(RoboTextPosition position, RoboTextAffinity affinity);

/** Reset @p caret: position 0, downstream, visible and enabled. Safe on NULL. */
void RoboTextCaretInit(RoboTextCaret *caret);

/** Field-for-field equality. Safe on NULL (NULL == NULL; a NULL and a non-NULL are different). */
int RoboTextCaretEqual(const RoboTextCaret *a, const RoboTextCaret *b);

/** Non-zero when @p caret is one this module produced: a position and a defined affinity. */
int RoboTextCaretIsValid(const RoboTextCaret *caret);

/* ------------------------------------------------------------------ */
/* Invalidation                                                        */
/* ------------------------------------------------------------------ */

/**
 * What a consumer of shaped text must redo after an edit (Phase 12.22.28–33).
 *
 * The ranges and the flags are separate answers on purpose, because they are separate questions:
 *
 *   - `textRange` is the range the edit produced, in the **new** text's own coordinates — the inserted
 *     text's span, or an empty range at the seam when the edit only deleted;
 *   - `shapingRange` is the range that must be re-shaped and `analysisRange` the range whose Unicode,
 *     script or direction analysis is stale. Both are **paragraph-widened**: an edit that changes one
 *     scalar can change the shaping of its neighbours (an `fi` that becomes a ligature, an Arabic letter
 *     that changes form, a Devanagari conjunct that reforms), and this build widens to the whole text
 *     rather than reshaping the exact bytes. 12.22.29–33 permit exactly that, and correctness first is what
 *     makes a later, finer contract a change of *range* rather than of behaviour.
 *   - `layoutRange` and `paintRange` follow, because a width or a line count can change.
 *
 * The flags are the cheap reads of the same facts, and four of the six are discriminating rather than
 * always-on: `requiresBidiReanalysis` is set only when the edited text carries a scalar that is not
 * left-to-right (inserting Arabic into an English paragraph can split a level run and inserting Latin into
 * one cannot, which is the case the flag is for), and `requiresFontReselection` only when text was
 * inserted or deleted at all, because a scalar the previous shaping never saw can be covered by no face the
 * previous selection used.
 */
typedef struct RoboTextEditInvalidation {
    /** The edit's own range in the **new** text. */
    RoboTextRange textRange;
    /** The range whose Unicode/script/direction analysis is stale. */
    RoboTextRange analysisRange;
    /** The range that must be re-shaped. */
    RoboTextRange shapingRange;
    /** The range whose line breaking and geometry are stale. */
    RoboTextRange layoutRange;
    /** The range that must be repainted. */
    RoboTextRange paintRange;
    /** Non-zero when the text changed at all, i.e. the edit was not a no-op. */
    int changed;
    /** Non-zero when shaping must run again over @ref shapingRange. */
    int requiresReshape;
    /** Non-zero when analysis (Unicode, script, direction, bidi) must run again. */
    int requiresReanalysis;
    /** Non-zero when the level runs may have moved: the edited text carries a non-left-to-right scalar. */
    int requiresBidiReanalysis;
    /** Non-zero when font selection must be redone — a newly inserted scalar may be uncovered. */
    int requiresFontReselection;
    /** Non-zero when line breaking and geometry must be redone. */
    int requiresRelayout;
    /** Non-zero when pixels must be redrawn. */
    int requiresRepaint;
    /** Non-zero when a selection map built over the old runs must be rebuilt. */
    int requiresSelectionRemap;
} RoboTextEditInvalidation;

/** Reset @p invalidation to "nothing changed": empty ranges, every flag 0. Safe on NULL. */
void RoboTextEditInvalidationInit(RoboTextEditInvalidation *invalidation);

/** Non-zero when @p invalidation asks for no work at all (a no-op edit). Safe on NULL (answers 1). */
int RoboTextEditInvalidationIsEmpty(const RoboTextEditInvalidation *invalidation);

/* ------------------------------------------------------------------ */
/* The operation                                                       */
/* ------------------------------------------------------------------ */

/**
 * One logical edit: the range it replaced, the text it removed and the text it inserted.
 *
 * The record undo needs (12.22.27) and the reason an edit is not just a mutation: `deleted` is kept, so a
 * history can put it back without asking a layout what used to be there — which it could not answer, since
 * the old text is gone by then. `inserted` is kept as well, because a redo is the same record again.
 *
 * The selection and caret before and after are in the same record, because "undo" means the caret goes
 * back too and a caller that had to reconstruct that from widths would be deriving logical state from
 * geometry, which this module's whole design refuses.
 *
 * Both texts are **owned** by the operation and released by @ref RoboTextEditOperationDestroy. An empty
 * insertion (a pure deletion) is an empty buffer, never NULL.
 */
typedef struct RoboTextEditOperation {
    /** The scalars this operation replaced, in the **old** text's coordinates. */
    RoboTextRange replacedRange;
    /** The text that was removed. Owned; never NULL; empty for a pure insertion. */
    RoboTextBuffer *deleted;
    /** The text that was put there. Owned; never NULL; empty for a pure deletion. */
    RoboTextBuffer *inserted;
    /** The selection before the edit. */
    RoboTextSelection selectionBefore;
    /** The selection after it. */
    RoboTextSelection selectionAfter;
    /** The caret before the edit. */
    RoboTextCaret caretBefore;
    /** The caret after it. */
    RoboTextCaret caretAfter;
} RoboTextEditOperation;

/** Reset @p operation to the empty one. Does **not** release the buffers: see `…Destroy`. Safe on NULL. */
void RoboTextEditOperationInit(RoboTextEditOperation *operation);

/** Release @p operation's `deleted` and `inserted` buffers and clear it. Safe on NULL and repeatable. */
void RoboTextEditOperationDestroy(RoboTextEditOperation *operation);

/**
 * Copy @p operation into @p out, duplicating both buffers.
 *
 * The door a transaction and a caller's own history use: an operation that came out of a result is owned by
 * that result, and a history that kept the pointer would hold text the result is about to release.
 */
Result RoboTextEditOperationClone(const RoboTextEditOperation *operation,
                                  RoboTextEditOperation *out,
                                  RoboTextDiag *diag);

/** Bytes the operation removed. 0 for a pure insertion. */
RoboTextByteIndex RoboTextEditOperationDeletedBytes(const RoboTextEditOperation *operation);

/** Bytes the operation inserted. 0 for a pure deletion. */
RoboTextByteIndex RoboTextEditOperationInsertedBytes(const RoboTextEditOperation *operation);

/** Non-zero when @p operation changed the text: something was deleted or something was inserted. */
int RoboTextEditOperationChanged(const RoboTextEditOperation *operation);

/* ------------------------------------------------------------------ */
/* The result                                                          */
/* ------------------------------------------------------------------ */

/**
 * What an edit did: the operation, where the caret and selection ended up, and what must be redone.
 *
 * `status` is the call's own answer, so a caller can tell a refused edit (a backspace at the start of the
 * text is `RoboOk` with `changed` 0 — it is a legitimate no-op, not an error) from a malformed one
 * (`RoboInvalidArgument`) and from one this build does not do (`RoboNotSupported`).
 *
 * `operation` is owned by the result: a caller that wants to keep it calls @ref
 * RoboTextEditOperationClone, and a caller that does not calls @ref RoboTextEditResultDestroy.
 */
typedef struct RoboTextEditResult {
    /** The call's status; the same value the door returned. */
    Result status;
    /** The edit, with both texts owned by this record. */
    RoboTextEditOperation operation;
    /** The range the edit touched, in the **new** text. Empty for a deletion's seam and for a no-op. */
    RoboTextRange affectedRange;
    /** The selection after the edit, which is collapsed for every operation this build performs. */
    RoboTextSelection selection;
    /** The caret after the edit. */
    RoboTextCaret caret;
    /** What a consumer of shaped text must redo. */
    RoboTextEditInvalidation invalidation;
} RoboTextEditResult;

/** Reset @p result to an empty `RoboOk` record with no buffers. Safe on NULL. */
void RoboTextEditResultInit(RoboTextEditResult *result);

/** Release @p result's operation buffers and clear it. Safe on NULL and repeatable. */
void RoboTextEditResultDestroy(RoboTextEditResult *result);

/** Non-zero when @p result succeeded **and** changed the text. Safe on NULL (answers 0). */
int RoboTextEditResultChanged(const RoboTextEditResult *result);

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

/**
 * A running composition: a range in the **source text** and the text currently in it.
 *
 * The composing text lives in the editor's buffer while it runs, because that is what makes the caret, the
 * selection and every query about the text work during composition — an IME's pending text is text, and a
 * reader can put a caret in it. What keeps it *distinguishable* from committed text is that the editor also
 * holds @ref RoboTextComposition::textBefore: the slice the composition replaced, empty for a fresh one.
 * Cancelling restores that slice; committing drops it. Neither guesses what the glyphs used to be.
 *
 * `selectedRange` and `clauseRange` are **relative to `range.start`** — IMEs report clause boundaries
 * relative to the composition, and storing them absolutely would make every one of them stale the moment
 * the composition's own start moved.
 */
typedef struct RoboTextComposition {
    /** Non-zero while a composition is running. */
    int active;
    /** The composition's own range in the source text. */
    RoboTextRange range;
    /** The text currently in that range. Owned; never NULL when active. */
    RoboTextBuffer *text;
    /** The IME's selection inside the composition, relative to @ref range's start. */
    RoboTextRange selectedRange;
    /** The IME's current clause, relative to @ref range's start. */
    RoboTextRange clauseRange;
    /** Non-zero when the composition carries a clause the IME named. */
    int hasClause;
    /** Non-zero when the composition carries a selection the IME named. */
    int hasSelection;
    /** What the composition replaced: the committed text it must be put back to on cancel. Owned. */
    RoboTextBuffer *textBefore;
    /** The selection before the composition began. */
    RoboTextSelection selectionBefore;
    /** The caret before the composition began. */
    RoboTextCaret caretBefore;
} RoboTextComposition;

/** Reset @p composition to "not active". Does not release buffers: see `RoboTextCompositionRelease`. */
void RoboTextCompositionInit(RoboTextComposition *composition);

/** Release @p composition's buffers and clear it. Safe on NULL and repeatable. */
void RoboTextCompositionRelease(RoboTextComposition *composition);

/* ------------------------------------------------------------------ */
/* The editor                                                          */
/* ------------------------------------------------------------------ */

typedef struct RoboTextEditor RoboTextEditor;

/** An editor over empty text, caret at 0. */
Result RoboTextEditorCreate(RoboTextEditor **out, RoboTextDiag *diag);

/**
 * An editor over a copy of @p view's bytes — or of @p buffer, when @p buffer is non-NULL.
 *
 * The text is **copied** (through `RoboTextBufferCreateFrom`), so a caller's bytes may be released the
 * moment this returns, which is what an editor over a DOM node's text needs.
 */
Result RoboTextEditorCreateFrom(RoboTextView view, RoboTextEditor **out, RoboTextDiag *diag);

/** See @ref RoboTextEditorCreateFrom: the owned-buffer overload, which takes precedence. */
Result RoboTextEditorCreateFromBuffer(const RoboTextBuffer *buffer,
                                      RoboTextEditor **out,
                                      RoboTextDiag *diag);

/** Release @p editor. Safe on NULL. */
void RoboTextEditorDestroy(RoboTextEditor *editor);

/** The editor's text, borrowed. NULL for a NULL editor. The pointer changes when the text does. */
const RoboTextBuffer *RoboTextEditorText(const RoboTextEditor *editor);

/** The text's scalar count — the upper bound of every position this editor accepts. */
RoboTextScalarIndex RoboTextEditorScalarCount(const RoboTextEditor *editor);

/** The current selection, by value. */
RoboTextSelection RoboTextEditorSelection(const RoboTextEditor *editor);

/** The current caret, by value. */
RoboTextCaret RoboTextEditorCaret(const RoboTextEditor *editor);

/** The range the selection covers, normalized. */
RoboTextRange RoboTextEditorSelectionRange(const RoboTextEditor *editor);

/** Non-zero while a composition is running. */
int RoboTextEditorIsComposing(const RoboTextEditor *editor);

/** The running composition, or an inactive record. The buffers stay the editor's. */
RoboTextComposition RoboTextEditorComposition(const RoboTextEditor *editor);

/** The **last** operation the editor published, or an empty one. Borrowed; cloned by the caller. */
const RoboTextEditOperation *RoboTextEditorLastOperation(const RoboTextEditor *editor);

/**
 * Install a selection, clamping it to the text and moving the caret to its focus.
 *
 * Clamping is not repair of a bug: an editor that has just been told the text shrank legitimately holds a
 * selection from before that, and the door's contract is "the nearest position this text has". An inverted
 * pair stays inverted — the anchor/focus distinction is the caller's and is preserved.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p editor.
 */
Result RoboTextEditorSetSelection(RoboTextEditor *editor, RoboTextSelection selection);

/** See @ref RoboTextEditorSetSelection: a caret's position and affinity, with the selection collapsed on it. */
Result RoboTextEditorSetCaret(RoboTextEditor *editor, RoboTextPosition position, RoboTextAffinity affinity);

/**
 * Replace scalars `[range.start, range.end)` with the UTF-8 bytes in @p text (or a whole buffer).
 *
 * The one mutation every other door is written in terms of. `range` must be inside the text, the bytes must
 * be valid UTF-8 (an ill-formed sequence is `RoboInvalidArgument` rather than being silently replaced —
 * a text a caller built is a caller's bug to hear about), and an empty range at the text's end is a plain
 * append.
 *
 * The edit is **atomic**: the new text is built in a copy and installed only on success, so a failure
 * leaves the editor exactly as it was, and a caller holding a pointer from before keeps a valid one.
 */
Result RoboTextEditorReplace(RoboTextEditor *editor,
                             RoboTextRange range,
                             const char *text,
                             size_t length,
                             RoboTextEditResult *out,
                             RoboTextDiag *diag);

/** See @ref RoboTextEditorReplace: the owned-buffer overload. */
Result RoboTextEditorReplaceBuffer(RoboTextEditor *editor,
                                   RoboTextRange range,
                                   const RoboTextBuffer *text,
                                   RoboTextEditResult *out,
                                   RoboTextDiag *diag);

/** Insert at the caret, replacing the selection if it is not empty — the typing path. */
Result RoboTextEditorInsert(RoboTextEditor *editor,
                            const char *text,
                            size_t length,
                            RoboTextEditResult *out,
                            RoboTextDiag *diag);

/** Replace the selection with @p text. A collapsed selection makes this an insert. */
Result RoboTextEditorReplaceSelection(RoboTextEditor *editor,
                                      const char *text,
                                      size_t length,
                                      RoboTextEditResult *out,
                                      RoboTextDiag *diag);

/**
 * Delete the unit before the caret: `Replace(previous boundary .. caret, "")`.
 *
 * At the start of the text this is `RoboOk` with `changed` 0 — a backspace at position 0 is a legitimate
 * no-op, not an error, and reporting it as one would make every caller special-case it.
 */
Result RoboTextEditorDeleteBackward(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag);

/** See @ref RoboTextEditorDeleteBackward: the unit after the caret. */
Result RoboTextEditorDeleteForward(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag);

/** Copy the selection's scalars into a new owned buffer. An empty selection yields an empty buffer. */
Result RoboTextEditorSelectedText(const RoboTextEditor *editor, RoboTextBuffer **out, RoboTextDiag *diag);

/**
 * Run a command against the editor (Phase 12.22.39).
 *
 * `InsertText`/`Paste` insert @p text; `Cut` copies the selection to @p outText when it is non-NULL and
 * deletes it; `Copy` only copies; `DeleteBackward`/`DeleteForward` delete a unit; the four selects and
 * `SelectLeft`/`SelectRight` move or extend the caret; `SelectAll` selects everything. `Undo`/`Redo` are
 * `RoboNotSupported` by name, because the history they would need is 12.23's — the *record* they will use is
 * already in every result. The four line/vertical movements are `RoboNotSupported` too: they need a layout.
 */
Result RoboTextEditorCommand(RoboTextEditor *editor,
                             RoboTextEditCommand command,
                             const char *text,
                             size_t length,
                             RoboTextBuffer **outText,
                             RoboTextEditResult *out,
                             RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Boundaries                                                          */
/* ------------------------------------------------------------------ */

/** Which unit a boundary question is asked in. */
typedef enum RoboTextEditBoundary {
    /** A scalar: the index space's own step. Never splits a UTF-8 sequence, may split a cluster. */
    RoboTextEditBoundaryScalar = 0,
    /** A character: one scalar, extended over the combining marks that follow a base. */
    RoboTextEditBoundaryCharacter = 1,
    /** A shaped cluster, answered by a run list. Needs `RoboTextEditBoundariesFromRuns` to have run. */
    RoboTextEditBoundaryCluster = 2,
    /** A word (UAX #29). **Refused**: nothing in this build segments words. */
    RoboTextEditBoundaryWord = 3,
    /** The start of a visual line. **Refused**: this layer has no layout. */
    RoboTextEditBoundaryLineStart = 4,
    /** The end of a visual line. **Refused**: @ref RoboTextEditBoundaryLineStart. */
    RoboTextEditBoundaryLineEnd = 5
} RoboTextEditBoundary;

/** Non-zero when @p boundary is one of the names above. */
int RoboTextEditBoundaryIsKnown(RoboTextEditBoundary boundary);

/** Name of @p boundary (`"character"`); `"unknown"` from beyond the enum. Never NULL. */
const char *RoboTextEditBoundaryName(RoboTextEditBoundary boundary);

/** Non-zero when this build answers @p boundary rather than refusing it. */
int RoboTextEditBoundaryIsSupported(RoboTextEditBoundary boundary);

/**
 * The nearest editing boundary strictly before @p position, or the text's start.
 *
 * The step is one scalar — so a multi-byte sequence is never split — and then, for
 * @ref RoboTextEditBoundaryCharacter, back over any combining marks that hang off the scalar before, so that
 * `e` and its acute are one unit and a backspace over a base takes its marks with it. Position 0 answers 0.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument or a position outside the text;
 *         RoboNotSupported for a boundary this build refuses (a word, a line).
 */
Result RoboTextEditBoundaryPrevious(RoboTextView view,
                                    RoboTextPosition position,
                                    RoboTextEditBoundary boundary,
                                    RoboTextPosition *out,
                                    RoboTextDiag *diag);

/** See @ref RoboTextEditBoundaryPrevious: the boundary strictly after @p position, or the text's end. */
Result RoboTextEditBoundaryNext(RoboTextView view,
                                RoboTextPosition position,
                                RoboTextEditBoundary boundary,
                                RoboTextPosition *out,
                                RoboTextDiag *diag);

/**
 * Ask a **shaped run list** where the cluster boundaries are (Phase 12.22.12/13).
 *
 * The resolver a caller installs when it has shaped text, which is the case that makes an Arabic joining
 * cluster or an Indic conjunct one editing unit. Without it @ref RoboTextEditBoundaryCluster is
 * `RoboNotSupported` rather than quietly falling back to the character rule: a caller that asked for
 * clusters and got character boundaries would be deleting half a conjunct and never know.
 *
 * The list is **borrowed** and must outlive the resolver (and any boundary query made with it).
 */
typedef struct RoboTextEditBoundaries RoboTextEditBoundaries;

/** A resolver over @p list, or NULL when @p list is NULL or has no runs. The caller destroys it. */
RoboTextEditBoundaries *RoboTextEditBoundariesFromRuns(const RoboTextGlyphRunList *list);

/** Release @p boundaries. The list stays the caller's. Safe on NULL. */
void RoboTextEditBoundariesDestroy(RoboTextEditBoundaries *boundaries);

/** Clusters the resolver knows, or 0. */
size_t RoboTextEditBoundariesClusterCount(const RoboTextEditBoundaries *boundaries);

/** The cluster covering @p position — the half-open source range — or an empty range when it covers none. */
RoboTextRange RoboTextEditBoundariesClusterAt(const RoboTextEditBoundaries *boundaries,
                                              RoboTextPosition position);

/** See @ref RoboTextEditBoundaryPrevious, answered by @p boundaries rather than by the character rule. */
Result RoboTextEditBoundariesPrevious(const RoboTextEditBoundaries *boundaries,
                                      RoboTextPosition position,
                                      RoboTextPosition *out,
                                      RoboTextDiag *diag);

/** See @ref RoboTextEditBoundariesPrevious: the boundary after @p position. */
Result RoboTextEditBoundariesNext(const RoboTextEditBoundaries *boundaries,
                                  RoboTextPosition position,
                                  RoboTextPosition *out,
                                  RoboTextDiag *diag);

/**
 * Install @p boundaries as the editor's cluster resolver, so its edits follow shaped clusters.
 *
 * The editor does not own @p boundaries: the caller keeps it alive for as long as it is installed, and
 * passes NULL to uninstall. The text the boundaries were built over must be the editor's current text —
 * this module cannot check that, and a resolver over other text would answer about the wrong scalars, so it
 * is stated here rather than discovered by a test.
 */
Result RoboTextEditorSetBoundaries(RoboTextEditor *editor, RoboTextEditBoundaries *boundaries);

/** The boundary a delete or a movement uses: the cluster resolver when installed, else characters. */
RoboTextEditBoundary RoboTextEditorBoundary(const RoboTextEditor *editor);

/* ------------------------------------------------------------------ */
/* Composition doors                                                   */
/* ------------------------------------------------------------------ */

/**
 * Start or update a composition at the current selection (Phase 12.22.19–22).
 *
 * The first call remembers what the composition replaced and where the caret and selection were; every call
 * replaces the composition's own range with @p text, so an IME that updates on every keystroke produces one
 * result per update and one range that grows. `selectedRange`/`clauseRange` are **relative to the
 * composition's start**; pass a `has*` flag of 0 to leave each unset.
 */
Result RoboTextEditorUpdateComposition(RoboTextEditor *editor,
                                       const char *text,
                                       size_t length,
                                       RoboTextRange selectedRange,
                                       int hasSelection,
                                       RoboTextRange clauseRange,
                                       int hasClause,
                                       RoboTextEditResult *out,
                                       RoboTextDiag *diag);

/**
 * Commit the running composition: it stops being a composition and its text stays as committed text.
 *
 * @return RoboOk and @p out with `changed` 1 when a composition was running; `RoboOk` with `changed` 0 when
 *         none was (committing nothing is not an error — it is what a caller that lost track does).
 */
Result RoboTextEditorCommitComposition(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag);

/**
 * Cancel the running composition, restoring the slice it replaced and the selection and caret from before.
 *
 * The restore is a `Replace` of the pre-composition slice, which is why cancelling in the middle of a
 * five-keypress composition puts back exactly what was there and not something reconstructed.
 *
 * **The selection and caret restored are the ones the composition began over** — @ref RoboTextComposition's
 * own `selectionBefore`/`caretBefore`, captured before its first update — and deliberately not the replace's
 * own `selectionBefore`, which is the state as of the *last* keystroke. The difference is visible: a
 * composition that started by replacing a selected word cancels to that word **still selected**, rather than
 * to a collapsed caret at the composition's own end.
 */
Result RoboTextEditorCancelComposition(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Transactions                                                        */
/* ------------------------------------------------------------------ */

/**
 * A group of edits that appears as **one** logical mutation (Phase 12.22.25/26).
 *
 * What it is for: an IME commit, a paste, an auto-replacement and a macro all perform several replacements
 * that no consumer should see half of. The transaction applies each one to the editor as it is added — so
 * every intermediate state is a real, valid text a caller could query — and then:
 *
 *   - @ref RoboTextEditTransactionCommit publishes a **single** `RoboTextEditResult` whose operation spans
 *     the whole transaction (the original range, the text that was there, the text that is there now) and
 *     whose invalidation is the union of its members';
 *   - @ref RoboTextEditTransactionCancel puts the text back the way it was, in one replace, so no consumer
 *     ever saw the intermediate states *as a committed result*.
 *
 * The member operations stay readable (`…Count`/`…OperationAt`), which is what a history that wants to undo
 * one keystroke of a paste needs, and the span-covering operation is what it wants for the whole of it.
 */
typedef struct RoboTextEditTransaction RoboTextEditTransaction;

/** Begin a transaction over @p editor. The caller destroys it (commit or cancel both do not). */
Result RoboTextEditTransactionCreate(RoboTextEditor *editor,
                                     RoboTextEditTransaction **out,
                                     RoboTextDiag *diag);

/** Release @p transaction. A transaction that was neither committed nor cancelled leaves its edits in place. */
void RoboTextEditTransactionDestroy(RoboTextEditTransaction *transaction);

/** Non-zero when @p transaction is open: begun and not committed or cancelled. */
int RoboTextEditTransactionIsOpen(const RoboTextEditTransaction *transaction);

/** Operations added so far. */
size_t RoboTextEditTransactionCount(const RoboTextEditTransaction *transaction);

/** Member operation @p index, borrowed, or NULL. */
const RoboTextEditOperation *RoboTextEditTransactionOperationAt(const RoboTextEditTransaction *transaction,
                                                                size_t index);

/** Replace `[range.start, range.end)` with @p text, as part of the transaction. */
Result RoboTextEditTransactionReplace(RoboTextEditTransaction *transaction,
                                      RoboTextRange range,
                                      const char *text,
                                      size_t length,
                                      RoboTextEditResult *out,
                                      RoboTextDiag *diag);

/** Insert at the caret, as part of the transaction. */
Result RoboTextEditTransactionInsert(RoboTextEditTransaction *transaction,
                                     const char *text,
                                     size_t length,
                                     RoboTextEditResult *out,
                                     RoboTextDiag *diag);

/** Replace the selection, as part of the transaction. */
Result RoboTextEditTransactionReplaceSelection(RoboTextEditTransaction *transaction,
                                               const char *text,
                                               size_t length,
                                               RoboTextEditResult *out,
                                               RoboTextDiag *diag);

/** Delete the unit before the caret, as part of the transaction. */
Result RoboTextEditTransactionDeleteBackward(RoboTextEditTransaction *transaction,
                                             RoboTextEditResult *out,
                                             RoboTextDiag *diag);

/** Publish the transaction as one result. The transaction stays alive (and closed) until destroyed. */
Result RoboTextEditTransactionCommit(RoboTextEditTransaction *transaction,
                                     RoboTextEditResult *out,
                                     RoboTextDiag *diag);

/** Put the text and the selection back the way they were before the transaction began. */
Result RoboTextEditTransactionCancel(RoboTextEditTransaction *transaction, RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Transformation                                                      */
/* ------------------------------------------------------------------ */

/**
 * Where a position that was valid **before** @p operation is afterwards (Phase 12.22.36).
 *
 * The rule, spelled out because every later layer depends on it:
 *
 *   - before `replacedRange` → unchanged;
 *   - inside it, or exactly at its start → the **end** of the inserted text (`replacedRange.start +
 *     insertedLength`), which is where a caret inside a deleted word belongs;
 *   - exactly at its end → also the end of the inserted text, because a boundary is owned by the range
 *     before it — a selection that ended where the edit began follows the text it selected;
 *   - after it → shifted by `inserted − deleted`.
 */
RoboTextPosition RoboTextPositionTransformAfterEdit(RoboTextPosition position,
                                                   const RoboTextEditOperation *operation);

/** See @ref RoboTextPositionTransformAfterEdit, applied to both ends of @p range. May invert, and does not. */
RoboTextRange RoboTextRangeTransformAfterEdit(RoboTextRange range, const RoboTextEditOperation *operation);

/** Where a position that is valid **after** @p operation was before. The inverse of the forward rule. */
RoboTextPosition RoboTextPositionTransformBeforeEdit(RoboTextPosition position,
                                                     const RoboTextEditOperation *operation);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/**
 * The editor as a multi-line block: the text's scalar and byte counts, the selection, the caret, the
 * composition and the last operation.
 *
 * Complete or absent, like every dump in this module: a block that does not fit leaves the buffer empty and
 * reports the length a complete one needs either way.
 */
Result RoboTextEditorWrite(const RoboTextEditor *editor,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength);

/** One operation as a line: the ranges, the two texts' byte counts, the selections and the carets. */
Result RoboTextEditOperationWrite(const RoboTextEditOperation *operation,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

/** One invalidation as a line: the five ranges and the seven flags, each named. */
Result RoboTextEditInvalidationWrite(const RoboTextEditInvalidation *invalidation,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength);

/** One composition as a line, or `"none"` when it is not active. */
Result RoboTextCompositionWrite(const RoboTextComposition *composition,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength);

/* ------------------------------------------------------------------ */
/* Fuzzing                                                             */
/* ------------------------------------------------------------------ */

/**
 * Drive an editor from arbitrary @p data: random ranges, random insertions the data decodes to, deletes,
 * composition updates, commits, cancels and transforms.
 *
 * One invariant, and it is the whole point: whatever the bytes are, the editor's text is **valid UTF-8**,
 * every position it reports is inside that text, its selection is not inverted by the module itself, no
 * allocated buffer leaks, and no loop runs forever. A zero-length call is valid and returns @ref RoboOk; a
 * NULL pointer with a non-zero size is @ref RoboInvalidArgument, as `RoboTextFuzzGlyphs` reports it.
 */
Result RoboTextEditingFuzz(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_EDITING_H */
