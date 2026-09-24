/*
 * Text editing (Phase 12.22) — see `RoboTextEditing.h` for the contract.
 *
 * The file is seven pieces, in the order an edit meets them:
 *
 *   **the vocabulary**   a command's name, a caret, a boundary's name, the invalidation record. Values with
 *                        an equality or a name, and nothing that allocates.
 *   **the operation**    one edit as a durable record — the range, the text removed, the text inserted, and
 *                        the selection and caret on both sides. The record undo needs, and the reason an
 *                        edit keeps the text it deleted instead of asking a layout what used to be there.
 *   **boundaries**       where a delete or a movement may land: one scalar, extended over the marks that
 *                        hang off a base, or a shaped cluster when a run list is available. A word and a
 *                        line are refused by name rather than faked with `± 1`.
 *   **the editor**       the buffer it owns, the selection, the caret, and `EditorApply` — the one place
 *                        text changes, and therefore the one place the caret, the selection and the
 *                        invalidation are decided.
 *   **composition**      an IME's pending text as a *range in the source*, with the slice it replaced kept
 *                        so that cancelling restores text rather than guessing at glyphs.
 *   **transactions**     several edits that no consumer may see half of, published as one operation.
 *   **transformation**   where a position or a range that was valid before an edit is afterwards.
 *
 * Nothing here shapes, measures, lays out or paints, and nothing here reads a device. `EditorApply` builds
 * the new text in a **copy** and installs it only on success, so a failure leaves the editor exactly as it
 * was; that costs one copy per edit and buys the atomicity every door below is written in terms of, and it
 * is the trade 12.22.54 states ("correctness comes first; the architecture must permit later
 * optimization").
 */

#include "RoboText/RoboTextEditing.h"

#include "RoboText/RoboTextDirection.h"
#include "RoboText/RoboTextUnicode.h"
#include "RoboTextInternal.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

int RoboTextEditCommandIsKnown(RoboTextEditCommand command)
{
    return command >= RoboTextEditCommandInsertText && command <= RoboTextEditCommandRedo;
}

const char *RoboTextEditCommandName(RoboTextEditCommand command)
{
    switch (command) {
        case RoboTextEditCommandNone:
            return "none";
        case RoboTextEditCommandInsertText:
            return "insert_text";
        case RoboTextEditCommandDeleteBackward:
            return "delete_backward";
        case RoboTextEditCommandDeleteForward:
            return "delete_forward";
        case RoboTextEditCommandMoveLeft:
            return "move_left";
        case RoboTextEditCommandMoveRight:
            return "move_right";
        case RoboTextEditCommandMoveUp:
            return "move_up";
        case RoboTextEditCommandMoveDown:
            return "move_down";
        case RoboTextEditCommandMoveToLineStart:
            return "move_to_line_start";
        case RoboTextEditCommandMoveToLineEnd:
            return "move_to_line_end";
        case RoboTextEditCommandSelectLeft:
            return "select_left";
        case RoboTextEditCommandSelectRight:
            return "select_right";
        case RoboTextEditCommandSelectAll:
            return "select_all";
        case RoboTextEditCommandCopy:
            return "copy";
        case RoboTextEditCommandCut:
            return "cut";
        case RoboTextEditCommandPaste:
            return "paste";
        case RoboTextEditCommandUndo:
            return "undo";
        case RoboTextEditCommandRedo:
            return "redo";
        default:
            /*
             * A value from beyond the enum is *named* (`"unknown"`) rather than reported as one of the
             * eighteen, for the reason every other `Name` door in this module gives: a dump that said
             * `insert_text` for a word out of range would hide the bug that produced it.
             */
            return "unknown";
    }
}

int RoboTextEditBoundaryIsKnown(RoboTextEditBoundary boundary)
{
    return boundary >= RoboTextEditBoundaryScalar && boundary <= RoboTextEditBoundaryLineEnd;
}

const char *RoboTextEditBoundaryName(RoboTextEditBoundary boundary)
{
    switch (boundary) {
        case RoboTextEditBoundaryScalar:
            return "scalar";
        case RoboTextEditBoundaryCharacter:
            return "character";
        case RoboTextEditBoundaryCluster:
            return "cluster";
        case RoboTextEditBoundaryWord:
            return "word";
        case RoboTextEditBoundaryLineStart:
            return "line_start";
        case RoboTextEditBoundaryLineEnd:
            return "line_end";
        default:
            return "unknown";
    }
}

int RoboTextEditBoundaryIsSupported(RoboTextEditBoundary boundary)
{
    /*
     * The three this build answers, and the three it refuses. `Word` needs UAX #29's word segmentation and
     * the two line boundaries need a layout; both are named here, in the header and in the capability record
     * rather than approximated with `position ± 1`, which would be a wrong answer dressed as a right one.
     */
    return boundary == RoboTextEditBoundaryScalar || boundary == RoboTextEditBoundaryCharacter ||
           boundary == RoboTextEditBoundaryCluster;
}

RoboTextCaret RoboTextCaretMake(RoboTextPosition position, RoboTextAffinity affinity)
{
    RoboTextCaret caret;

    caret.position = position;
    caret.affinity = affinity;
    caret.visible = 1;
    caret.enabled = 1;
    return caret;
}

void RoboTextCaretInit(RoboTextCaret *caret)
{
    if (!caret) {
        return;
    }
    memset(caret, 0, sizeof(*caret));
    caret->position = RoboTextPositionMake(0u);
    caret->affinity = RoboTextAffinityDownstream;
    caret->visible = 1;
    caret->enabled = 1;
}

int RoboTextCaretEqual(const RoboTextCaret *a, const RoboTextCaret *b)
{
    if (!a || !b) {
        return !a && !b;
    }
    return RoboTextPositionEqual(a->position, b->position) && a->affinity == b->affinity &&
           a->visible == b->visible && a->enabled == b->enabled;
}

int RoboTextCaretIsValid(const RoboTextCaret *caret)
{
    if (!caret) {
        return 0;
    }
    return RoboTextAffinityIsKnown(caret->affinity);
}

void RoboTextEditInvalidationInit(RoboTextEditInvalidation *invalidation)
{
    if (!invalidation) {
        return;
    }
    memset(invalidation, 0, sizeof(*invalidation));
}

int RoboTextEditInvalidationIsEmpty(const RoboTextEditInvalidation *invalidation)
{
    if (!invalidation) {
        return 1;
    }
    return !invalidation->changed && !invalidation->requiresReshape && !invalidation->requiresReanalysis &&
           !invalidation->requiresBidiReanalysis && !invalidation->requiresFontReselection &&
           !invalidation->requiresRelayout && !invalidation->requiresRepaint &&
           !invalidation->requiresSelectionRemap;
}

/* ------------------------------------------------------------------ */
/* The operation                                                       */
/* ------------------------------------------------------------------ */

void RoboTextEditOperationInit(RoboTextEditOperation *operation)
{
    if (!operation) {
        return;
    }
    memset(operation, 0, sizeof(*operation));
    operation->replacedRange = RoboTextRangeAt(0u);
    operation->selectionBefore = RoboTextSelectionAt(RoboTextPositionMake(0u));
    operation->selectionAfter = operation->selectionBefore;
    RoboTextCaretInit(&operation->caretBefore);
    operation->caretAfter = operation->caretBefore;
}

void RoboTextEditOperationDestroy(RoboTextEditOperation *operation)
{
    if (!operation) {
        return;
    }
    RoboTextBufferDestroy(operation->deleted);
    RoboTextBufferDestroy(operation->inserted);
    RoboTextEditOperationInit(operation);
}

Result RoboTextEditOperationClone(const RoboTextEditOperation *operation,
                                  RoboTextEditOperation *out,
                                  RoboTextDiag *diag)
{
    if (!operation || !out) {
        return RoboInvalidArgument;
    }
    RoboTextEditOperationInit(out);
    out->replacedRange = operation->replacedRange;
    out->selectionBefore = operation->selectionBefore;
    out->selectionAfter = operation->selectionAfter;
    out->caretBefore = operation->caretBefore;
    out->caretAfter = operation->caretAfter;
    if (RoboTextBufferClone(operation->deleted, &out->deleted, diag) != RoboOk) {
        RoboTextEditOperationDestroy(out);
        return RoboOutOfMemory;
    }
    if (RoboTextBufferClone(operation->inserted, &out->inserted, diag) != RoboOk) {
        RoboTextEditOperationDestroy(out);
        return RoboOutOfMemory;
    }
    return RoboOk;
}

RoboTextByteIndex RoboTextEditOperationDeletedBytes(const RoboTextEditOperation *operation)
{
    return operation ? RoboTextBufferByteLength(operation->deleted) : 0u;
}

RoboTextByteIndex RoboTextEditOperationInsertedBytes(const RoboTextEditOperation *operation)
{
    return operation ? RoboTextBufferByteLength(operation->inserted) : 0u;
}

int RoboTextEditOperationChanged(const RoboTextEditOperation *operation)
{
    if (!operation) {
        return 0;
    }
    return RoboTextBufferByteLength(operation->deleted) != 0u ||
           RoboTextBufferByteLength(operation->inserted) != 0u;
}

void RoboTextEditResultInit(RoboTextEditResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->status = RoboOk;
    result->affectedRange = RoboTextRangeAt(0u);
    RoboTextEditOperationInit(&result->operation);
    RoboTextEditInvalidationInit(&result->invalidation);
    RoboTextCaretInit(&result->caret);
}

void RoboTextEditResultDestroy(RoboTextEditResult *result)
{
    if (!result) {
        return;
    }
    RoboTextEditOperationDestroy(&result->operation);
    RoboTextEditResultInit(result);
}

int RoboTextEditResultChanged(const RoboTextEditResult *result)
{
    if (!result) {
        return 0;
    }
    return result->status == RoboOk && result->invalidation.changed ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

void RoboTextCompositionInit(RoboTextComposition *composition)
{
    if (!composition) {
        return;
    }
    memset(composition, 0, sizeof(*composition));
    composition->range = RoboTextRangeAt(0u);
    composition->selectedRange = RoboTextRangeAt(0u);
    composition->clauseRange = RoboTextRangeAt(0u);
}

void RoboTextCompositionRelease(RoboTextComposition *composition)
{
    if (!composition) {
        return;
    }
    RoboTextBufferDestroy(composition->text);
    RoboTextBufferDestroy(composition->textBefore);
    RoboTextCompositionInit(composition);
}

/* ------------------------------------------------------------------ */
/* Boundaries                                                          */
/* ------------------------------------------------------------------ */

/** Non-zero when @p codePoint is a combining mark: `General_Category` `Mn`, `Mc` or `Me`. */
static int CodePointIsCombiningMark(RoboTextCodePoint codePoint)
{
    UnicodeGeneralCategory category = RoboTextUnicodeGeneralCategory(codePoint);

    return category == RoboUGcMn || category == RoboUGcMc || category == RoboUGcMe;
}

/** Scalars of @p view, or 0 when it cannot be decoded. */
static RoboTextScalarIndex ViewScalars(RoboTextView view)
{
    RoboTextScalarIndex scalars = 0u;

    if (RoboTextViewScalarCount(view, &scalars, NULL) != RoboOk) {
        return 0u;
    }
    return scalars;
}

/** The scalar at @p index, or U+FFFD when it cannot be read. A refusal here is not the caller's answer. */
static RoboTextCodePoint ViewScalarAt(RoboTextView view, RoboTextScalarIndex index)
{
    Uchar32 scalar = (Uchar32)RoboTextCodePointReplacement;

    if (RoboTextViewScalarAt(view, index, &scalar, NULL) != RoboOk) {
        return RoboTextCodePointReplacement;
    }
    return (RoboTextCodePoint)scalar;
}

/**
 * The boundary strictly before @p position, in the character unit.
 *
 * The rule, and it is asymmetric on purpose: step back one scalar, then keep stepping back while the scalar
 * landed on is a **combining mark**. A mark belongs to the character before it (12.14's cluster rule), so the
 * boundary before a base is before its marks too — which is what makes a base and its three accents one
 * backspace. The forward half is the mirror image: step forward one scalar, then over the marks that follow
 * it. `"e" + acute` therefore has exactly the boundaries 0 and 2, and an offset inside it is not one a
 * delete may land on.
 */
static RoboTextScalarIndex CharacterPrevious(RoboTextView view, RoboTextScalarIndex at)
{
    RoboTextScalarIndex scalar = at;

    if (scalar == 0u) {
        return 0u;
    }
    scalar--;
    while (scalar > 0u && CodePointIsCombiningMark(ViewScalarAt(view, scalar))) {
        scalar--;
    }
    return scalar;
}

/** See @ref CharacterPrevious: forward one scalar, then over the marks that follow it. */
static RoboTextScalarIndex CharacterNext(RoboTextView view, RoboTextScalarIndex at)
{
    RoboTextScalarIndex scalars = ViewScalars(view);
    RoboTextScalarIndex scalar = at;

    if (scalar >= scalars) {
        return scalars;
    }
    scalar++;
    while (scalar < scalars && CodePointIsCombiningMark(ViewScalarAt(view, scalar))) {
        scalar++;
    }
    return scalar;
}

/**
 * One safe boundary step over a view.
 *
 * The one entry point both public doors and the editor use, so a change to the rule cannot reach one caller
 * and not another. `Cluster` is refused here rather than silently answered as `Character`: a caller that
 * asked for clusters and got character boundaries would delete half an Indic conjunct and never know.
 */
static Result BoundaryStep(RoboTextView view,
                           RoboTextPosition position,
                           RoboTextEditBoundary boundary,
                           int forward,
                           RoboTextPosition *out,
                           RoboTextDiag *diag)
{
    RoboTextScalarIndex scalars;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument, "an out pointer is required");
    }
    if (!RoboTextEditBoundaryIsKnown(boundary)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument, "that is not a boundary name");
    }
    if (!RoboTextEditBoundaryIsSupported(boundary)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboNotSupported,
                               boundary == RoboTextEditBoundaryCluster
                                   ? "cluster boundaries need a shaped run list"
                                   : "that boundary needs segmentation this build does not have");
    }
    scalars = ViewScalars(view);
    if (!RoboTextPositionIsWithin(position, scalars)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "that position is outside the text");
    }
    if (boundary == RoboTextEditBoundaryScalar) {
        if (forward) {
            *out = RoboTextPositionMake(position.offset >= scalars ? scalars : position.offset + 1u);
        } else {
            *out = RoboTextPositionMake(position.offset == 0u ? 0u : position.offset - 1u);
        }
        return RoboOk;
    }
    *out = RoboTextPositionMake(forward ? CharacterNext(view, position.offset)
                                        : CharacterPrevious(view, position.offset));
    return RoboOk;
}

Result RoboTextEditBoundaryPrevious(RoboTextView view,
                                    RoboTextPosition position,
                                    RoboTextEditBoundary boundary,
                                    RoboTextPosition *out,
                                    RoboTextDiag *diag)
{
    return BoundaryStep(view, position, boundary, 0, out, diag);
}

Result RoboTextEditBoundaryNext(RoboTextView view,
                                RoboTextPosition position,
                                RoboTextEditBoundary boundary,
                                RoboTextPosition *out,
                                RoboTextDiag *diag)
{
    return BoundaryStep(view, position, boundary, 1, out, diag);
}

/*
 * The cluster resolver: a run list's clusters, flattened once, so a query is a walk over one ascending array
 * rather than a walk per run.
 */
struct RoboTextEditBoundaries {
    /** Borrowed: the list whose clusters these are. */
    const RoboTextGlyphRunList *list;
    /** Owned: one half-open source range per cluster, in ascending order. */
    RoboTextRange *clusters;
    size_t count;
};

RoboTextEditBoundaries *RoboTextEditBoundariesFromRuns(const RoboTextGlyphRunList *list)
{
    RoboTextEditBoundaries *boundaries;
    size_t runCount;
    size_t index;
    size_t total = 0u;
    size_t at = 0u;

    if (!list || RoboTextGlyphRunListCount(list) == 0u) {
        return NULL;
    }
    runCount = RoboTextGlyphRunListCount(list);
    for (index = 0u; index < runCount; index++) {
        total += RoboTextGlyphRunClusterCount(RoboTextGlyphRunListRun(list, index));
    }
    if (total == 0u) {
        return NULL;
    }
    boundaries = (RoboTextEditBoundaries *)RoboCalloc(1u, sizeof(*boundaries));
    if (!boundaries) {
        return NULL;
    }
    boundaries->clusters = (RoboTextRange *)RoboCalloc(total, sizeof(*boundaries->clusters));
    if (!boundaries->clusters) {
        RoboFree(boundaries);
        return NULL;
    }
    boundaries->count = total;
    for (index = 0u; index < runCount; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);
        size_t clusters = RoboTextGlyphRunClusterCount(run);
        size_t cluster;

        for (cluster = 0u; cluster < clusters && at < total; cluster++) {
            const RoboTextGlyphCluster *entry = RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)cluster);

            if (!entry) {
                continue;
            }
            boundaries->clusters[at++] = entry->source;
        }
    }
    boundaries->count = at;
    boundaries->list = list;
    return boundaries;
}

void RoboTextEditBoundariesDestroy(RoboTextEditBoundaries *boundaries)
{
    if (!boundaries) {
        return;
    }
    RoboFree(boundaries->clusters);
    RoboFree(boundaries);
}

size_t RoboTextEditBoundariesClusterCount(const RoboTextEditBoundaries *boundaries)
{
    return boundaries ? boundaries->count : 0u;
}

RoboTextRange RoboTextEditBoundariesClusterAt(const RoboTextEditBoundaries *boundaries,
                                              RoboTextPosition position)
{
    size_t index;

    if (!boundaries) {
        return RoboTextRangeAt(position.offset);
    }
    for (index = 0u; index < boundaries->count; index++) {
        if (RoboTextRangeContains(boundaries->clusters[index], position.offset)) {
            return boundaries->clusters[index];
        }
    }
    /* Exactly at the text's end there is no cluster: an empty range there is the honest answer. */
    return RoboTextRangeAt(position.offset);
}

Result RoboTextEditBoundariesPrevious(const RoboTextEditBoundaries *boundaries,
                                      RoboTextPosition position,
                                      RoboTextPosition *out,
                                      RoboTextDiag *diag)
{
    size_t index;

    if (!boundaries || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a resolver and an out pointer are required");
    }
    for (index = 0u; index < boundaries->count; index++) {
        RoboTextRange cluster = boundaries->clusters[index];

        if (position.offset > cluster.start && position.offset <= cluster.end) {
            *out = RoboTextPositionMake(cluster.start);
            return RoboOk;
        }
    }
    *out = RoboTextPositionMake(0u);
    return RoboOk;
}

Result RoboTextEditBoundariesNext(const RoboTextEditBoundaries *boundaries,
                                  RoboTextPosition position,
                                  RoboTextPosition *out,
                                  RoboTextDiag *diag)
{
    size_t index;

    if (!boundaries || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "a resolver and an out pointer are required");
    }
    for (index = 0u; index < boundaries->count; index++) {
        RoboTextRange cluster = boundaries->clusters[index];

        if (position.offset >= cluster.start && position.offset < cluster.end) {
            *out = RoboTextPositionMake(cluster.end);
            return RoboOk;
        }
    }
    if (boundaries->count > 0u) {
        *out = RoboTextPositionMake(boundaries->clusters[boundaries->count - 1u].end);
        return RoboOk;
    }
    *out = RoboTextPositionMake(position.offset);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The editor                                                          */
/* ------------------------------------------------------------------ */

struct RoboTextEditor {
    /** Owned: the text. Replaced wholesale by a successful edit, so a failure changes nothing. */
    RoboTextBuffer *text;
    RoboTextSelection selection;
    RoboTextCaret caret;
    /** The running composition, if any. Its buffers are owned while it is active. */
    RoboTextComposition composition;
    /** The last published operation, owned, so a caller can ask for it after the result is gone. */
    RoboTextEditOperation last;
    /** Borrowed: the caller's cluster resolver, or NULL. */
    RoboTextEditBoundaries *boundaries;
    /** Edits applied and no-ops seen, published because a fuzzer and a profile both want them. */
    uint64_t edits;
    uint64_t noops;
};

static RoboTextView EditorView(const RoboTextEditor *editor)
{
    return RoboTextViewMake(RoboTextBufferBytes(editor->text), RoboTextBufferByteLength(editor->text));
}

Result RoboTextEditorCreate(RoboTextEditor **out, RoboTextDiag *diag)
{
    return RoboTextEditorCreateFrom(RoboTextViewMake(NULL, 0u), out, diag);
}

Result RoboTextEditorCreateFrom(RoboTextView view, RoboTextEditor **out, RoboTextDiag *diag)
{
    RoboTextEditor *editor;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an out pointer is required");
    }
    *out = NULL;
    editor = (RoboTextEditor *)RoboCalloc(1u, sizeof(*editor));
    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory, "the editor could not be allocated");
    }
    RoboTextCompositionInit(&editor->composition);
    RoboTextEditOperationInit(&editor->last);
    if (RoboTextBufferCreateFrom(&editor->text, view.bytes, view.length, diag) != RoboOk) {
        RoboFree(editor);
        return RoboOutOfMemory;
    }
    /*
     * A fresh editor's caret is at 0 and its selection is collapsed there: an editor with no caret has no
     * answer to "what would typing insert", which is the first question a caller asks.
     */
    editor->caret = RoboTextCaretMake(RoboTextPositionMake(0u), RoboTextAffinityDownstream);
    editor->selection = RoboTextSelectionAt(editor->caret.position);
    *out = editor;
    return RoboOk;
}

Result RoboTextEditorCreateFromBuffer(const RoboTextBuffer *buffer,
                                      RoboTextEditor **out,
                                      RoboTextDiag *diag)
{
    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "a buffer is required");
    }
    return RoboTextEditorCreateFrom(
        RoboTextViewMake(RoboTextBufferBytes(buffer), RoboTextBufferByteLength(buffer)), out, diag);
}

void RoboTextEditorDestroy(RoboTextEditor *editor)
{
    if (!editor) {
        return;
    }
    RoboTextBufferDestroy(editor->text);
    RoboTextCompositionRelease(&editor->composition);
    RoboTextEditOperationDestroy(&editor->last);
    RoboFree(editor);
}

const RoboTextBuffer *RoboTextEditorText(const RoboTextEditor *editor)
{
    return editor ? editor->text : NULL;
}

RoboTextScalarIndex RoboTextEditorScalarCount(const RoboTextEditor *editor)
{
    return editor ? RoboTextBufferScalarCount(editor->text) : 0u;
}

RoboTextSelection RoboTextEditorSelection(const RoboTextEditor *editor)
{
    return editor ? editor->selection : RoboTextSelectionAt(RoboTextPositionMake(0u));
}

RoboTextCaret RoboTextEditorCaret(const RoboTextEditor *editor)
{
    RoboTextCaret caret;

    if (!editor) {
        RoboTextCaretInit(&caret);
        return caret;
    }
    return editor->caret;
}

RoboTextRange RoboTextEditorSelectionRange(const RoboTextEditor *editor)
{
    return editor ? RoboTextSelectionRange(editor->selection) : RoboTextRangeAt(0u);
}

int RoboTextEditorIsComposing(const RoboTextEditor *editor)
{
    return editor ? editor->composition.active : 0;
}

RoboTextComposition RoboTextEditorComposition(const RoboTextEditor *editor)
{
    RoboTextComposition composition;

    if (!editor) {
        RoboTextCompositionInit(&composition);
        return composition;
    }
    return editor->composition;
}

const RoboTextEditOperation *RoboTextEditorLastOperation(const RoboTextEditor *editor)
{
    return editor ? &editor->last : NULL;
}

Result RoboTextEditorSetSelection(RoboTextEditor *editor, RoboTextSelection selection)
{
    RoboTextScalarIndex scalars;

    if (!editor) {
        return RoboInvalidArgument;
    }
    scalars = RoboTextBufferScalarCount(editor->text);
    editor->selection = RoboTextSelectionClamp(selection, scalars);
    editor->caret.position = editor->selection.focus;
    return RoboOk;
}

Result RoboTextEditorSetCaret(RoboTextEditor *editor, RoboTextPosition position, RoboTextAffinity affinity)
{
    if (!editor) {
        return RoboInvalidArgument;
    }
    if (!RoboTextAffinityIsKnown(affinity)) {
        return RoboInvalidArgument;
    }
    if (RoboTextEditorSetSelection(editor, RoboTextSelectionAt(position)) != RoboOk) {
        return RoboInvalidArgument;
    }
    editor->caret.affinity = affinity;
    return RoboOk;
}

Result RoboTextEditorSetBoundaries(RoboTextEditor *editor, RoboTextEditBoundaries *boundaries)
{
    if (!editor) {
        return RoboInvalidArgument;
    }
    editor->boundaries = boundaries;
    return RoboOk;
}

RoboTextEditBoundary RoboTextEditorBoundary(const RoboTextEditor *editor)
{
    if (editor && editor->boundaries) {
        return RoboTextEditBoundaryCluster;
    }
    return RoboTextEditBoundaryCharacter;
}

/* ------------------------------------------------------------------ */
/* Invalidation                                                        */
/* ------------------------------------------------------------------ */

/** Line terminators this build treats as paragraph breaks: `LF`, `CR`, `LS` and `PS`. */
static int ScalarIsParagraphBreak(RoboTextCodePoint codePoint)
{
    return codePoint == 0x000Au || codePoint == 0x000Du || codePoint == 0x2028u || codePoint == 0x2029u;
}

/**
 * The paragraph @p at is in: the range between the terminators around it.
 *
 * The unit the invalidation widens to, and the reason it is not simply "the whole text": Unicode analysis,
 * script resolution and the bidirectional algorithm are all at least paragraph-scoped, so a paragraph is the
 * smallest range that is provably sufficient without a shaping context this layer does not have. A finer
 * range (the shaping run, the cluster) needs the analysis and the run list — which is exactly the layering
 * 12.22.30 asks to preserve.
 */
static RoboTextRange ParagraphRangeOf(RoboTextView view, RoboTextScalarIndex at)
{
    RoboTextScalarIndex scalars = ViewScalars(view);
    RoboTextScalarIndex start = at;
    RoboTextScalarIndex end = at;

    while (start > 0u && !ScalarIsParagraphBreak(ViewScalarAt(view, start - 1u))) {
        start--;
    }
    while (end < scalars && !ScalarIsParagraphBreak(ViewScalarAt(view, end))) {
        end++;
    }
    return RoboTextRangeMake(start, end);
}

/** Non-zero when @p buffer holds a scalar whose level a paragraph does not simply inherit from its base. */
static int TextNeedsBidiReanalysis(const RoboTextBuffer *buffer)
{
    RoboTextScalarIndex scalars;
    RoboTextScalarIndex index;

    if (!buffer) {
        return 0;
    }
    scalars = RoboTextBufferScalarCount(buffer);
    for (index = 0u; index < scalars; index++) {
        Uchar32 scalar = 0;
        RoboTextDirectionClass directionClass;

        if (RoboTextBufferScalarAt(buffer, index, &scalar, NULL) != RoboOk) {
            continue;
        }
        directionClass = RoboTextDirectionClassOf((RoboTextCodePoint)scalar);
        if (directionClass == RoboTextDirectionClassRightToLeft ||
            directionClass == RoboTextDirectionClassNumber) {
            return 1;
        }
    }
    return 0;
}

/**
 * Fill @p out from @p operation and the **new** text's scalar count.
 *
 * The ranges are all paragraph-widened and the flags are the cheap reads of the same facts. `changed` is the
 * operation's own answer, and a no-op operation (an empty range replaced by nothing) produces a record with
 * every flag 0 — which is the honest answer to a backspace at the start of the text.
 */
static void InvalidationOf(const RoboTextEditOperation *operation,
                           RoboTextView newView,
                           RoboTextEditInvalidation *out)
{
    RoboTextScalarIndex insertedScalars = RoboTextBufferScalarCount(operation->inserted);
    RoboTextScalarIndex scalars = ViewScalars(newView);
    RoboTextScalarIndex seam = operation->replacedRange.start + insertedScalars;
    RoboTextRange paragraph;

    RoboTextEditInvalidationInit(out);
    out->changed = RoboTextEditOperationChanged(operation);
    out->textRange = RoboTextRangeMake(operation->replacedRange.start,
                                       operation->replacedRange.start + insertedScalars);
    if (!out->changed) {
        return;
    }
    if (seam > scalars) {
        seam = scalars;
    }
    paragraph = ParagraphRangeOf(newView, seam);
    out->analysisRange = paragraph;
    out->shapingRange = paragraph;
    out->layoutRange = paragraph;
    out->paintRange = paragraph;
    out->requiresReshape = 1;
    out->requiresReanalysis = 1;
    out->requiresRelayout = 1;
    out->requiresRepaint = 1;
    out->requiresSelectionRemap = 1;
    /*
     * Inserting a right-to-left scalar into a left-to-right paragraph can split a level run and inserting a
     * left-to-right one into it cannot — which is the discrimination the flag exists for. A number is
     * included because UAX #9's I1/I2 give it a level the paragraph's base does not decide on its own.
     */
    out->requiresBidiReanalysis =
        (TextNeedsBidiReanalysis(operation->inserted) || TextNeedsBidiReanalysis(operation->deleted)) ? 1 : 0;
    /*
     * Font selection is invalidated whenever the text changed at all: a scalar the previous shaping never saw
     * may be covered by no face the previous selection used, and this layer has no font database to ask.
     */
    out->requiresFontReselection = 1;
}

/* ------------------------------------------------------------------ */
/* The one mutation                                                    */
/* ------------------------------------------------------------------ */

/** The next caret after an edit: collapsed after the inserted text, downstream. */
static RoboTextCaret CaretAfterEdit(RoboTextScalarIndex at, RoboTextScalarIndex scalars)
{
    if (at > scalars) {
        at = scalars;
    }
    return RoboTextCaretMake(RoboTextPositionMake(at), RoboTextAffinityDownstream);
}

/** Point @p out at an empty `RoboOk` result with no buffers: the shape of every refusal's answer. */
static Result EditingNothing(RoboTextEditResult *out)
{
    if (out) {
        RoboTextEditResultDestroy(out);
    }
    return RoboOk;
}

/**
 * The one place the text changes.
 *
 * Everything else in this file is a way of computing @p range and @p bytes and then describing what
 * happened. The new text is built in a clone and installed only when every step has succeeded, so the editor
 * is never left half-edited — which is what makes a transaction's cancel a *replace* rather than a replay.
 */
static Result EditorApply(RoboTextEditor *editor,
                          RoboTextRange range,
                          const char *bytes,
                          size_t length,
                          RoboTextEditResult *out,
                          RoboTextDiag *diag)
{
    RoboTextBuffer *next = NULL;
    RoboTextBuffer *deleted = NULL;
    RoboTextBuffer *inserted = NULL;
    RoboTextBuffer *old;
    RoboTextEditOperation operation;
    RoboTextEditInvalidation invalidation;
    RoboTextView newView;
    RoboTextScalarIndex scalars;
    RoboTextScalarIndex insertedScalars;
    RoboTextCaret caretAfter;

    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    if (out) {
        RoboTextEditResultDestroy(out);
    }
    if ((!bytes && length != 0u) || !RoboTextRangeIsWithin(range, RoboTextBufferScalarCount(editor->text))) {
        return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboInvalidArgument,
                               "the range is not inside the text");
    }
    /*
     * The bytes are a caller's, so they are checked rather than repaired: a text this layer is *given* that is
     * not UTF-8 is a caller's bug, and replacing its bytes with U+FFFD would hide it. (Bytes the DOM supplied
     * already went through the buffer door's own REPLACE policy, so the two agree about what valid means.)
     */
    if (length != 0u && !UnicodeUtf8Validate(bytes, length)) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "the inserted text is not valid UTF-8");
    }
    old = editor->text;
    if (RoboTextBufferClone(old, &next, diag) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (RoboTextBufferSlice(old, range, &deleted, diag) != RoboOk ||
        RoboTextBufferCreateFrom(&inserted, bytes, length, diag) != RoboOk) {
        RoboTextBufferDestroy(deleted);
        RoboTextBufferDestroy(next);
        return RoboOutOfMemory;
    }
    if (RoboTextBufferRemove(next, range, diag) != RoboOk ||
        RoboTextBufferInsert(next, range.start, bytes, length, diag) != RoboOk) {
        RoboTextBufferDestroy(deleted);
        RoboTextBufferDestroy(inserted);
        RoboTextBufferDestroy(next);
        return RoboOutOfMemory;
    }
    insertedScalars = RoboTextBufferScalarCount(inserted);

    RoboTextEditOperationInit(&operation);
    operation.replacedRange = range;
    operation.deleted = deleted;
    operation.inserted = inserted;
    operation.selectionBefore = editor->selection;
    operation.caretBefore = editor->caret;
    scalars = RoboTextBufferScalarCount(next);
    caretAfter = CaretAfterEdit(range.start + insertedScalars, scalars);
    operation.caretAfter = caretAfter;
    operation.selectionAfter = RoboTextSelectionAt(caretAfter.position);

    newView = RoboTextViewMake(RoboTextBufferBytes(next), RoboTextBufferByteLength(next));
    InvalidationOf(&operation, newView, &invalidation);

    /* Only now does the editor change. */
    editor->text = next;
    editor->caret = caretAfter;
    editor->selection = operation.selectionAfter;
    RoboTextEditOperationDestroy(&editor->last);
    if (RoboTextEditOperationClone(&operation, &editor->last, NULL) != RoboOk) {
        /*
         * A clone failure here is out of memory, and the edit itself has already succeeded. Keeping the
         * *editor* correct matters more than the convenience record, so the last-operation slot stays empty
         * and the caller still gets the operation in its own result — rather than undoing a real edit to
         * report a failure of a record nothing depends on.
         */
        RoboTextEditOperationInit(&editor->last);
    }
    if (invalidation.changed) {
        editor->edits++;
    } else {
        editor->noops++;
    }
    if (out) {
        out->operation = operation;
        out->affectedRange = invalidation.textRange;
        out->selection = operation.selectionAfter;
        out->caret = caretAfter;
        out->invalidation = invalidation;
        out->status = RoboOk;
        RoboTextEditOperationInit(&operation);
    }
    RoboTextEditOperationDestroy(&operation);
    return RoboOk;
}

Result RoboTextEditorReplace(RoboTextEditor *editor,
                             RoboTextRange range,
                             const char *text,
                             size_t length,
                             RoboTextEditResult *out,
                             RoboTextDiag *diag)
{
    return EditorApply(editor, range, text, length, out, diag);
}

Result RoboTextEditorReplaceBuffer(RoboTextEditor *editor,
                                   RoboTextRange range,
                                   const RoboTextBuffer *text,
                                   RoboTextEditResult *out,
                                   RoboTextDiag *diag)
{
    if (!text) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "a buffer is required");
    }
    return EditorApply(editor, range, RoboTextBufferBytes(text), RoboTextBufferByteLength(text), out, diag);
}

Result RoboTextEditorInsert(RoboTextEditor *editor,
                            const char *text,
                            size_t length,
                            RoboTextEditResult *out,
                            RoboTextDiag *diag)
{
    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    return EditorApply(editor, RoboTextEditorSelectionRange(editor), text, length, out, diag);
}

Result RoboTextEditorReplaceSelection(RoboTextEditor *editor,
                                      const char *text,
                                      size_t length,
                                      RoboTextEditResult *out,
                                      RoboTextDiag *diag)
{
    return RoboTextEditorInsert(editor, text, length, out, diag);
}

/**
 * The boundary step the editor's own deletes use.
 *
 * A cluster resolver wins when one is installed, because it is the caller saying "the units are what the
 * shaped text says they are"; otherwise the character rule, whose view is the editor's text.
 */
static Result EditorBoundaryStep(const RoboTextEditor *editor,
                                 RoboTextPosition position,
                                 int forward,
                                 RoboTextPosition *out,
                                 RoboTextDiag *diag)
{
    if (editor->boundaries) {
        return forward ? RoboTextEditBoundariesNext(editor->boundaries, position, out, diag)
                       : RoboTextEditBoundariesPrevious(editor->boundaries, position, out, diag);
    }
    return BoundaryStep(EditorView(editor), position, RoboTextEditBoundaryCharacter, forward, out, diag);
}

Result RoboTextEditorDeleteBackward(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag)
{
    RoboTextRange range;
    RoboTextPosition previous;
    Result status;

    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    /*
     * With a selection, a backspace deletes the selection rather than a unit before it — the behaviour every
     * editor has, and the one case where "backspace" is not a boundary question at all.
     */
    range = RoboTextEditorSelectionRange(editor);
    if (!RoboTextRangeIsEmpty(range)) {
        return EditorApply(editor, range, NULL, 0u, out, diag);
    }
    status = EditorBoundaryStep(editor, editor->caret.position, 0, &previous, diag);
    if (status != RoboOk) {
        return status;
    }
    if (previous.offset >= editor->caret.position.offset) {
        /* The start of the text: a legitimate no-op, not an error. */
        if (out) {
            RoboTextEditResultDestroy(out);
        }
        return RoboOk;
    }
    return EditorApply(editor, RoboTextRangeMake(previous.offset, editor->caret.position.offset), NULL, 0u, out,
                       diag);
}

Result RoboTextEditorDeleteForward(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag)
{
    RoboTextRange range;
    RoboTextPosition next;
    Result status;

    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    range = RoboTextEditorSelectionRange(editor);
    if (!RoboTextRangeIsEmpty(range)) {
        return EditorApply(editor, range, NULL, 0u, out, diag);
    }
    status = EditorBoundaryStep(editor, editor->caret.position, 1, &next, diag);
    if (status != RoboOk) {
        return status;
    }
    if (next.offset <= editor->caret.position.offset) {
        return EditingNothing(out);
    }
    return EditorApply(editor, RoboTextRangeMake(editor->caret.position.offset, next.offset), NULL, 0u, out,
                       diag);
}

Result RoboTextEditorSelectedText(const RoboTextEditor *editor, RoboTextBuffer **out, RoboTextDiag *diag)
{
    if (!editor || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an editor and an out pointer are required");
    }
    return RoboTextBufferSlice(editor->text, RoboTextEditorSelectionRange(editor), out, diag);
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

/** Move the caret one unit, extending the selection when @p extend. Moves no text, so nothing is invalid. */
static Result EditorMove(RoboTextEditor *editor,
                         int forward,
                         int extend,
                         RoboTextEditResult *out,
                         RoboTextDiag *diag)
{
    RoboTextPosition to;
    Result status = EditorBoundaryStep(editor, editor->caret.position, forward, &to, diag);

    if (status != RoboOk) {
        return status;
    }
    if (extend) {
        editor->selection.focus = to;
    } else {
        editor->selection = RoboTextSelectionAt(to);
    }
    editor->caret.position = to;
    if (out) {
        RoboTextEditResultDestroy(out);
        out->affectedRange = RoboTextRangeAt(to.offset);
        out->selection = editor->selection;
        out->caret = editor->caret;
    }
    return RoboOk;
}

static Result EditorSelectAll(RoboTextEditor *editor, RoboTextEditResult *out)
{
    RoboTextScalarIndex scalars = RoboTextBufferScalarCount(editor->text);

    editor->selection = RoboTextSelectionMake(RoboTextPositionMake(0u), RoboTextPositionMake(scalars));
    editor->caret.position = RoboTextPositionMake(scalars);
    if (out) {
        RoboTextEditResultDestroy(out);
        out->affectedRange = RoboTextRangeFull(scalars);
        out->selection = editor->selection;
        out->caret = editor->caret;
    }
    return RoboOk;
}

Result RoboTextEditorCommand(RoboTextEditor *editor,
                             RoboTextEditCommand command,
                             const char *text,
                             size_t length,
                             RoboTextBuffer **outText,
                             RoboTextEditResult *out,
                             RoboTextDiag *diag)
{
    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    if (!RoboTextEditCommandIsKnown(command)) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "that is not a command name");
    }
    switch (command) {
        case RoboTextEditCommandInsertText:
        case RoboTextEditCommandPaste:
            return RoboTextEditorInsert(editor, text, length, out, diag);
        case RoboTextEditCommandDeleteBackward:
            return RoboTextEditorDeleteBackward(editor, out, diag);
        case RoboTextEditCommandDeleteForward:
            return RoboTextEditorDeleteForward(editor, out, diag);
        case RoboTextEditCommandMoveLeft:
            return EditorMove(editor, 0, 0, out, diag);
        case RoboTextEditCommandMoveRight:
            return EditorMove(editor, 1, 0, out, diag);
        case RoboTextEditCommandSelectLeft:
            return EditorMove(editor, 0, 1, out, diag);
        case RoboTextEditCommandSelectRight:
            return EditorMove(editor, 1, 1, out, diag);
        case RoboTextEditCommandSelectAll:
            return EditorSelectAll(editor, out);
        case RoboTextEditCommandCopy:
            if (!outText) {
                return RoboOk;
            }
            return RoboTextEditorSelectedText(editor, outText, diag);
        case RoboTextEditCommandCut:
            if (outText && RoboTextEditorSelectedText(editor, outText, diag) != RoboOk) {
                return RoboOutOfMemory;
            }
            return RoboTextEditorInsert(editor, NULL, 0u, out, diag);
        case RoboTextEditCommandUndo:
        case RoboTextEditCommandRedo:
            /*
             * Refused by name. The *record* an undo manager needs is in every result — the range, the text on
             * both sides, the selection and the caret — but the history itself is 12.23's, and answering this
             * command with "nothing happened" would be worse than saying no.
             */
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboNotSupported,
                                   "the undo history is a later phase; the record is in every result");
        case RoboTextEditCommandMoveUp:
        case RoboTextEditCommandMoveDown:
        case RoboTextEditCommandMoveToLineStart:
        case RoboTextEditCommandMoveToLineEnd:
            return RoboTextDiagMsg(diag, RoboTextDomainRange, RoboNotSupported,
                                   "a line movement needs a layout, which this layer does not have");
        case RoboTextEditCommandNone:
        default:
            return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                                   "that is not a command name");
    }
}

/* ------------------------------------------------------------------ */
/* Composition                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextEditorUpdateComposition(RoboTextEditor *editor,
                                       const char *text,
                                       size_t length,
                                       RoboTextRange selectedRange,
                                       int hasSelection,
                                       RoboTextRange clauseRange,
                                       int hasClause,
                                       RoboTextEditResult *out,
                                       RoboTextDiag *diag)
{
    RoboTextRange target;
    RoboTextEditResult local;
    Result status;

    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    if (!editor->composition.active) {
        /*
         * A composition begins where the caller is: over the selection, so an IME replacing a selected word
         * starts by replacing it, and at the caret otherwise. The slice it replaced is kept — that is the
         * whole of what cancelling needs.
         */
        RoboTextComposition state;

        RoboTextCompositionInit(&state);
        target = RoboTextEditorSelectionRange(editor);
        state.selectionBefore = editor->selection;
        state.caretBefore = editor->caret;
        if (RoboTextBufferSlice(editor->text, target, &state.textBefore, diag) != RoboOk) {
            RoboTextCompositionRelease(&state);
            return RoboOutOfMemory;
        }
        state.active = 1;
        state.range = target;
        state.selectedRange = selectedRange;
        state.hasSelection = hasSelection ? 1 : 0;
        state.clauseRange = clauseRange;
        state.hasClause = hasClause ? 1 : 0;
        editor->composition = state;
    } else {
        target = editor->composition.range;
        editor->composition.selectedRange = selectedRange;
        editor->composition.hasSelection = hasSelection ? 1 : 0;
        editor->composition.clauseRange = clauseRange;
        editor->composition.hasClause = hasClause ? 1 : 0;
    }
    RoboTextEditResultInit(&local);
    status = EditorApply(editor, target, text, length, &local, diag);
    if (status != RoboOk) {
        RoboTextEditResultDestroy(&local);
        /*
         * The update failed and the text did not change. The composition's own record is left exactly as it
         * was — including for a first update, where "as it was" means the range the composition would have
         * replaced is still the selection's, and `textBefore` still holds its slice. Cancelling that state is
         * a replace of the text with itself, which is a no-op rather than a wrong answer.
         */
        return status;
    }
    /* The composition's range grows (or shrinks) with the text the IME just supplied. */
    editor->composition.range = local.operation.replacedRange;
    editor->composition.range.end = local.operation.replacedRange.start +
                                    RoboTextBufferScalarCount(local.operation.inserted);
    if (editor->composition.text) {
        RoboTextBufferDestroy(editor->composition.text);
    }
    if (RoboTextBufferClone(local.operation.inserted, &editor->composition.text, diag) != RoboOk) {
        RoboTextEditResultDestroy(&local);
        return RoboOutOfMemory;
    }
    if (out) {
        RoboTextEditResultDestroy(out);
        out->status = RoboOk;
        out->operation = local.operation;
        out->affectedRange = local.affectedRange;
        out->selection = local.selection;
        out->caret = local.caret;
        out->invalidation = local.invalidation;
        RoboTextEditOperationInit(&local.operation);
    }
    RoboTextEditResultDestroy(&local);
    return RoboOk;
}

Result RoboTextEditorCommitComposition(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag)
{
    RoboTextEditOperation operation;

    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    if (!editor->composition.active) {
        return EditingNothing(out);
    }
    /*
     * The composition as **one** operation, which is what a history wants and what an undo of a five-keypress
     * IME session has to be: the range it began over, the text that was there, the text that is there now,
     * and the caret and selection on both sides. The text itself does not change here — the composing text
     * has been the text all along — so the invalidation asks only for a **repaint**: what stopped being true
     * is the composition *boundary*, which a renderer drawing an underline or a clause highlight must redraw.
     */
    RoboTextEditOperationInit(&operation);
    /*
     * The range the composition began over. Its **start** never moves — every update replaces at the same
     * place and the composing text grows forward from it — so the original span is that start extended by the
     * slice that was there, `textBefore`. Subtracting the slice's length instead would read the range
     * backwards from wherever the composition currently ends, which is the range *after* the first keystroke
     * rather than the one the IME started by replacing.
     */
    operation.replacedRange =
        RoboTextRangeMake(editor->composition.range.start,
                          editor->composition.range.start +
                              RoboTextBufferScalarCount(editor->composition.textBefore));
    operation.selectionBefore = editor->composition.selectionBefore;
    operation.caretBefore = editor->composition.caretBefore;
    operation.selectionAfter = editor->selection;
    operation.caretAfter = editor->caret;
    if (RoboTextBufferClone(editor->composition.textBefore, &operation.deleted, diag) != RoboOk ||
        RoboTextBufferClone(editor->composition.text, &operation.inserted, diag) != RoboOk) {
        RoboTextEditOperationDestroy(&operation);
        return RoboOutOfMemory;
    }
    if (out) {
        RoboTextEditResultDestroy(out);
        out->status = RoboOk;
        out->operation = operation;
        out->affectedRange = editor->composition.range;
        out->selection = editor->selection;
        out->caret = editor->caret;
        out->invalidation.changed = RoboTextEditOperationChanged(&operation);
        out->invalidation.textRange = editor->composition.range;
        out->invalidation.requiresRepaint = out->invalidation.changed ? 1 : 0;
        RoboTextEditOperationInit(&operation);
    }
    RoboTextEditOperationDestroy(&operation);
    RoboTextCompositionRelease(&editor->composition);
    return RoboOk;
}

Result RoboTextEditorCancelComposition(RoboTextEditor *editor, RoboTextEditResult *out, RoboTextDiag *diag)
{
    RoboTextComposition state;
    RoboTextEditResult local;
    RoboTextSelection selectionBefore;
    RoboTextCaret caretBefore;
    Result status;

    if (!editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "an editor is required");
    }
    if (!editor->composition.active) {
        return EditingNothing(out);
    }
    /*
     * Detach the state: the buffers become this frame's to release, and the editor is no longer composing.
     *
     * The selection and the caret to restore are the ones the composition **began** over, which is why they
     * are read here and not from the replace's own operation: that operation's `selectionBefore` is the state
     * as of the *last keystroke* of the composition, and restoring that would put a reader mid-composition
     * instead of where they were — a five-keypress composition would cancel to a collapsed caret at its own
     * end rather than to the word it started by replacing.
     */
    state = editor->composition;
    selectionBefore = state.selectionBefore;
    caretBefore = state.caretBefore;
    RoboTextCompositionInit(&editor->composition);
    /*
     * Restoring is a **replace** of the slice the composition replaced, not a reconstruction: the text that
     * was there is in `textBefore` because it was copied before the first update, so a cancellation after
     * five updates puts back exactly what the first one found.
     */
    RoboTextEditResultInit(&local);
    status = EditorApply(editor, state.range, RoboTextBufferBytes(state.textBefore),
                         RoboTextBufferByteLength(state.textBefore), &local, diag);
    RoboTextCompositionRelease(&state);
    if (status != RoboOk) {
        RoboTextEditResultDestroy(&local);
        return status;
    }
    editor->selection = selectionBefore;
    editor->caret = caretBefore;
    /*
     * The caller's record describes the **reversal**: what the composition left there is what was deleted, and
     * the text that came back is what was inserted. The caret and selection are the ones from before the
     * composition, which is where a cancellation leaves a reader.
     */
    local.selection = editor->selection;
    local.caret = editor->caret;
    if (out) {
        RoboTextEditResultDestroy(out);
        out->status = RoboOk;
        out->operation = local.operation;
        out->affectedRange = local.affectedRange;
        out->selection = local.selection;
        out->caret = local.caret;
        out->invalidation = local.invalidation;
        RoboTextEditOperationInit(&local.operation);
    }
    RoboTextEditResultDestroy(&local);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Transactions                                                        */
/* ------------------------------------------------------------------ */

struct RoboTextEditTransaction {
    /** Borrowed: the editor this transaction edits. */
    RoboTextEditor *editor;
    /** Owned: the text as it was — what a cancel restores and the union operation's `deleted` half. */
    RoboTextBuffer *snapshot;
    RoboTextSelection selectionBefore;
    RoboTextCaret caretBefore;
    /** Owned: the member operations, in the order they were added. */
    RoboTextEditOperation *members;
    size_t count;
    size_t capacity;
    /** The union of the members' replaced ranges, in the **original** text's coordinates. */
    RoboTextRange span;
    /** Non-zero once @ref span means something. */
    int hasSpan;
    /** Scalars inserted and removed across the members: what maps the span into the new text. */
    RoboTextScalarIndex scalarsInserted;
    RoboTextScalarIndex scalarsRemoved;
    /** The union of the members' invalidations. */
    RoboTextEditInvalidation invalidation;
    int open;
    /** Non-zero when a member could not be recorded, which makes the union operation best-effort. */
    int incomplete;
};

Result RoboTextEditTransactionCreate(RoboTextEditor *editor,
                                     RoboTextEditTransaction **out,
                                     RoboTextDiag *diag)
{
    RoboTextEditTransaction *transaction;

    if (!editor || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "an editor and an out pointer are required");
    }
    *out = NULL;
    transaction = (RoboTextEditTransaction *)RoboCalloc(1u, sizeof(*transaction));
    if (!transaction) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboOutOfMemory,
                               "the transaction could not be allocated");
    }
    if (RoboTextBufferClone(editor->text, &transaction->snapshot, diag) != RoboOk) {
        RoboFree(transaction);
        return RoboOutOfMemory;
    }
    transaction->editor = editor;
    transaction->selectionBefore = editor->selection;
    transaction->caretBefore = editor->caret;
    transaction->span = RoboTextRangeAt(editor->caret.position.offset);
    RoboTextEditInvalidationInit(&transaction->invalidation);
    transaction->open = 1;
    *out = transaction;
    return RoboOk;
}

void RoboTextEditTransactionDestroy(RoboTextEditTransaction *transaction)
{
    size_t index;

    if (!transaction) {
        return;
    }
    for (index = 0u; index < transaction->count; index++) {
        RoboTextEditOperationDestroy(&transaction->members[index]);
    }
    RoboFree(transaction->members);
    RoboTextBufferDestroy(transaction->snapshot);
    RoboFree(transaction);
}

int RoboTextEditTransactionIsOpen(const RoboTextEditTransaction *transaction)
{
    return transaction ? transaction->open : 0;
}

size_t RoboTextEditTransactionCount(const RoboTextEditTransaction *transaction)
{
    return transaction ? transaction->count : 0u;
}

const RoboTextEditOperation *RoboTextEditTransactionOperationAt(const RoboTextEditTransaction *transaction,
                                                                size_t index)
{
    if (!transaction || index >= transaction->count) {
        return NULL;
    }
    return &transaction->members[index];
}

/** Widen @p out with @p add: every range unioned, every flag or-ed. */
static void InvalidationUnion(RoboTextEditInvalidation *out, const RoboTextEditInvalidation *add)
{
    out->textRange = RoboTextRangeUnion(out->textRange, add->textRange);
    out->analysisRange = RoboTextRangeUnion(out->analysisRange, add->analysisRange);
    out->shapingRange = RoboTextRangeUnion(out->shapingRange, add->shapingRange);
    out->layoutRange = RoboTextRangeUnion(out->layoutRange, add->layoutRange);
    out->paintRange = RoboTextRangeUnion(out->paintRange, add->paintRange);
    out->changed = out->changed || add->changed;
    out->requiresReshape = out->requiresReshape || add->requiresReshape;
    out->requiresReanalysis = out->requiresReanalysis || add->requiresReanalysis;
    out->requiresBidiReanalysis = out->requiresBidiReanalysis || add->requiresBidiReanalysis;
    out->requiresFontReselection = out->requiresFontReselection || add->requiresFontReselection;
    out->requiresRelayout = out->requiresRelayout || add->requiresRelayout;
    out->requiresRepaint = out->requiresRepaint || add->requiresRepaint;
    out->requiresSelectionRemap = out->requiresSelectionRemap || add->requiresSelectionRemap;
}

/**
 * Record one member: its operation, the union span in **original** coordinates, and the union invalidation.
 *
 * The span is tracked in the original text's coordinates because that is the space an undo speaks: a member's
 * range is in the text as of that member, and the only difference between the two is how much the members
 * before it inserted and removed. That delta is exactly `scalarsInserted - scalarsRemoved`.
 */
static void TransactionAdd(RoboTextEditTransaction *transaction, const RoboTextEditResult *result)
{
    RoboTextEditOperation *grown;
    RoboTextRange original;
    int64_t shift = (int64_t)transaction->scalarsInserted - (int64_t)transaction->scalarsRemoved;

    if (transaction->count == transaction->capacity) {
        size_t capacity = transaction->capacity == 0u ? 4u : transaction->capacity * 2u;
        size_t bytes = capacity * sizeof(*grown);

        grown = (RoboTextEditOperation *)RoboCalloc(capacity, sizeof(*grown));
        if (!grown) {
            /*
             * Out of memory recording a member the editor has already applied. The edit is real, so the
             * transaction marks itself incomplete rather than pretending the member never happened; a cancel
             * still restores the snapshot, which is the answer that matters.
             */
            transaction->incomplete = 1;
            return;
        }
        if (transaction->members) {
            memcpy(grown, transaction->members, transaction->count * sizeof(*grown));
            RoboFree(transaction->members);
        }
        (void)bytes;
        transaction->members = grown;
        transaction->capacity = capacity;
    }
    RoboTextEditOperationInit(&transaction->members[transaction->count]);
    if (RoboTextEditOperationClone(&result->operation, &transaction->members[transaction->count], NULL) !=
        RoboOk) {
        RoboTextEditOperationDestroy(&transaction->members[transaction->count]);
        transaction->incomplete = 1;
        return;
    }
    transaction->count++;

    original = RoboTextRangeMake((RoboTextScalarIndex)((int64_t)result->operation.replacedRange.start - shift),
                                 (RoboTextScalarIndex)((int64_t)result->operation.replacedRange.end - shift));
    if (!transaction->hasSpan) {
        transaction->span = original;
        transaction->hasSpan = 1;
    } else {
        transaction->span = RoboTextRangeUnion(transaction->span, original);
    }
    transaction->scalarsRemoved += result->operation.replacedRange.end - result->operation.replacedRange.start;
    transaction->scalarsInserted += RoboTextBufferScalarCount(result->operation.inserted);
    InvalidationUnion(&transaction->invalidation, &result->invalidation);
}

Result RoboTextEditTransactionReplace(RoboTextEditTransaction *transaction,
                                      RoboTextRange range,
                                      const char *text,
                                      size_t length,
                                      RoboTextEditResult *out,
                                      RoboTextDiag *diag)
{
    RoboTextEditResult local;
    Result status;

    if (!transaction || !transaction->editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "a transaction is required");
    }
    if (!transaction->open) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "the transaction is closed");
    }
    RoboTextEditResultInit(&local);
    status = EditorApply(transaction->editor, range, text, length, &local, diag);
    if (status != RoboOk) {
        RoboTextEditResultDestroy(&local);
        return status;
    }
    TransactionAdd(transaction, &local);
    if (out) {
        RoboTextEditResultDestroy(out);
        out->status = RoboOk;
        out->operation = local.operation;
        out->affectedRange = local.affectedRange;
        out->selection = local.selection;
        out->caret = local.caret;
        out->invalidation = local.invalidation;
        RoboTextEditOperationInit(&local.operation);
    }
    RoboTextEditResultDestroy(&local);
    return RoboOk;
}

Result RoboTextEditTransactionInsert(RoboTextEditTransaction *transaction,
                                     const char *text,
                                     size_t length,
                                     RoboTextEditResult *out,
                                     RoboTextDiag *diag)
{
    if (!transaction || !transaction->editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "a transaction is required");
    }
    return RoboTextEditTransactionReplace(transaction, RoboTextEditorSelectionRange(transaction->editor), text,
                                          length, out, diag);
}

Result RoboTextEditTransactionReplaceSelection(RoboTextEditTransaction *transaction,
                                               const char *text,
                                               size_t length,
                                               RoboTextEditResult *out,
                                               RoboTextDiag *diag)
{
    return RoboTextEditTransactionInsert(transaction, text, length, out, diag);
}

Result RoboTextEditTransactionDeleteBackward(RoboTextEditTransaction *transaction,
                                             RoboTextEditResult *out,
                                             RoboTextDiag *diag)
{
    RoboTextEditor *editor;
    RoboTextRange range;
    RoboTextPosition previous;
    Result status;

    if (!transaction || !transaction->editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "a transaction is required");
    }
    if (!transaction->open) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "the transaction is closed");
    }
    editor = transaction->editor;
    range = RoboTextEditorSelectionRange(editor);
    if (RoboTextRangeIsEmpty(range)) {
        status = EditorBoundaryStep(editor, editor->caret.position, 0, &previous, diag);
        if (status != RoboOk) {
            return status;
        }
        range = RoboTextRangeMake(previous.offset, editor->caret.position.offset);
    }
    return RoboTextEditTransactionReplace(transaction, range, NULL, 0u, out, diag);
}

Result RoboTextEditTransactionCommit(RoboTextEditTransaction *transaction,
                                     RoboTextEditResult *out,
                                     RoboTextDiag *diag)
{
    RoboTextEditOperation operation;
    RoboTextBuffer *inserted = NULL;
    RoboTextScalarIndex scalars;
    RoboTextRange newSpan;

    if (!transaction) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "a transaction is required");
    }
    if (transaction->count == 0u) {
        /*
         * Nothing was added: the transaction is the identity, and its result says so. A caller that opened
         * one and changed its mind gets the answer an empty edit deserves rather than a fabricated span.
         */
        transaction->open = 0;
        return EditingNothing(out);
    }
    scalars = RoboTextBufferScalarCount(transaction->editor->text);
    newSpan = RoboTextRangeMake(transaction->span.start,
                                transaction->span.start + transaction->scalarsInserted);
    if (newSpan.end > scalars) {
        newSpan.end = scalars;
    }
    RoboTextEditOperationInit(&operation);
    operation.replacedRange = transaction->span;
    operation.selectionBefore = transaction->selectionBefore;
    operation.caretBefore = transaction->caretBefore;
    operation.selectionAfter = transaction->editor->selection;
    operation.caretAfter = transaction->editor->caret;
    /* The whole transaction as one edit: the snapshot's slice as `deleted`, the current text's as `inserted`. */
    if (RoboTextBufferSlice(transaction->snapshot, transaction->span, &operation.deleted, diag) != RoboOk ||
        RoboTextBufferSlice(transaction->editor->text, newSpan, &inserted, diag) != RoboOk) {
        RoboTextBufferDestroy(inserted);
        RoboTextEditOperationDestroy(&operation);
        return RoboOutOfMemory;
    }
    operation.inserted = inserted;
    transaction->open = 0;
    if (out) {
        RoboTextEditResultDestroy(out);
        out->status = RoboOk;
        out->operation = operation;
        out->affectedRange = transaction->invalidation.textRange;
        out->selection = transaction->editor->selection;
        out->caret = transaction->editor->caret;
        out->invalidation = transaction->invalidation;
        RoboTextEditOperationInit(&operation);
    }
    RoboTextEditOperationDestroy(&operation);
    return RoboOk;
}

Result RoboTextEditTransactionCancel(RoboTextEditTransaction *transaction, RoboTextDiag *diag)
{
    RoboTextEditor *editor;
    RoboTextBuffer *restore = NULL;
    RoboTextRange current;
    Result status;

    if (!transaction || !transaction->editor) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument, "a transaction is required");
    }
    editor = transaction->editor;
    /*
     * One replace puts the whole span back. The slice comes from the **snapshot**, not from a reverse replay of
     * the members: each member's range is in the coordinates of its own moment, and reconstructing the
     * sequence backwards would have to be right about every one of them. The text as it was is one call.
     */
    if (RoboTextBufferSlice(transaction->snapshot, transaction->span, &restore, diag) != RoboOk) {
        return RoboOutOfMemory;
    }
    current = RoboTextRangeMake(transaction->span.start, transaction->span.start + transaction->scalarsInserted);
    if (current.end > RoboTextBufferScalarCount(editor->text)) {
        current.end = RoboTextBufferScalarCount(editor->text);
    }
    status = EditorApply(editor, current, RoboTextBufferBytes(restore), RoboTextBufferByteLength(restore), NULL,
                         diag);
    RoboTextBufferDestroy(restore);
    if (status != RoboOk) {
        return status;
    }
    editor->selection = transaction->selectionBefore;
    editor->caret = transaction->caretBefore;
    transaction->open = 0;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Transformation                                                      */
/* ------------------------------------------------------------------ */

RoboTextPosition RoboTextPositionTransformAfterEdit(RoboTextPosition position,
                                                   const RoboTextEditOperation *operation)
{
    RoboTextScalarIndex removedScalars;
    RoboTextScalarIndex insertedScalars;

    if (!operation) {
        return position;
    }
    removedScalars = operation->replacedRange.end - operation->replacedRange.start;
    insertedScalars = RoboTextBufferScalarCount(operation->inserted);
    if (position.offset < operation->replacedRange.start) {
        return position;
    }
    if (position.offset <= operation->replacedRange.end) {
        /*
         * The seam — including the range's own end — belongs to the text the edit put there. That rule is what
         * makes "insert at the caret, and the caret is after what you typed" fall out of the algebra rather
         * than being special-cased in the editing door.
         */
        return RoboTextPositionMake(operation->replacedRange.start + insertedScalars);
    }
    return RoboTextPositionMake(position.offset - removedScalars + insertedScalars);
}

RoboTextRange RoboTextRangeTransformAfterEdit(RoboTextRange range, const RoboTextEditOperation *operation)
{
    RoboTextPosition start = RoboTextPositionTransformAfterEdit(RoboTextPositionMake(range.start), operation);
    RoboTextPosition end = RoboTextPositionTransformAfterEdit(RoboTextPositionMake(range.end), operation);

    /*
     * Both rules are monotone — a later position never maps to an earlier one — so a valid range stays valid
     * and never inverts; nothing needs "repairing" here, and repairing it would hide a broken rule.
     */
    return RoboTextRangeMake(start.offset, end.offset);
}

RoboTextPosition RoboTextPositionTransformBeforeEdit(RoboTextPosition position,
                                                     const RoboTextEditOperation *operation)
{
    RoboTextScalarIndex removedScalars;
    RoboTextScalarIndex insertedScalars;
    RoboTextScalarIndex seam;

    if (!operation) {
        return position;
    }
    removedScalars = operation->replacedRange.end - operation->replacedRange.start;
    insertedScalars = RoboTextBufferScalarCount(operation->inserted);
    seam = operation->replacedRange.start + insertedScalars;
    if (position.offset < operation->replacedRange.start) {
        return position;
    }
    if (position.offset <= seam) {
        /* Inside what the edit inserted: the nearest position before the edit is where the edit began. */
        return RoboTextPositionMake(operation->replacedRange.start);
    }
    return RoboTextPositionMake(position.offset - insertedScalars + removedScalars);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** Publish @p scratch, or refuse: complete or absent, like every dump in this module. */
static Result EditingPublish(const char *scratch,
                             size_t length,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength)
{
    if (!buffer || !outLength) {
        return RoboInvalidArgument;
    }
    *outLength = length;
    buffer[0] = '\0';
    if (length + 1u > capacity) {
        return RoboNotSupported;
    }
    memcpy(buffer, scratch, length);
    buffer[length] = '\0';
    return RoboOk;
}

static size_t WriteRange(char *buffer, size_t capacity, size_t length, RoboTextRange range)
{
    length = RoboTextWriterUnsigned(buffer, capacity, length, range.start);
    length = RoboTextWriterText(buffer, capacity, length, "..");
    return RoboTextWriterUnsigned(buffer, capacity, length, range.end);
}

static size_t WriteFlag(char *buffer, size_t capacity, size_t length, const char *name, int value)
{
    length = RoboTextWriterText(buffer, capacity, length, " ");
    length = RoboTextWriterText(buffer, capacity, length, name);
    return RoboTextWriterText(buffer, capacity, length, value ? "=1" : "=0");
}

Result RoboTextEditOperationWrite(const RoboTextEditOperation *operation,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    char scratch[384];
    size_t length = 0u;

    if (!operation) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "replace ");
    length = WriteRange(scratch, sizeof(scratch), length, operation->replacedRange);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " deleted ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length,
                                    RoboTextEditOperationDeletedBytes(operation));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "B inserted ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length,
                                    RoboTextEditOperationInsertedBytes(operation));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "B caret ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, operation->caretBefore.position.offset);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "->");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, operation->caretAfter.position.offset);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " ");
    length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                RoboTextAffinityName(operation->caretBefore.affinity));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "->");
    length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                RoboTextAffinityName(operation->caretAfter.affinity));
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return EditingPublish(scratch, length, buffer, capacity, outLength);
}

Result RoboTextEditInvalidationWrite(const RoboTextEditInvalidation *invalidation,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength)
{
    char scratch[512];
    size_t length = 0u;

    if (!invalidation) {
        return RoboInvalidArgument;
    }
    length = WriteFlag(scratch, sizeof(scratch), length, "changed", invalidation->changed);
    length = WriteFlag(scratch, sizeof(scratch), length, "reshape", invalidation->requiresReshape);
    length = WriteFlag(scratch, sizeof(scratch), length, "reanalysis", invalidation->requiresReanalysis);
    length = WriteFlag(scratch, sizeof(scratch), length, "bidi", invalidation->requiresBidiReanalysis);
    length = WriteFlag(scratch, sizeof(scratch), length, "fonts", invalidation->requiresFontReselection);
    length = WriteFlag(scratch, sizeof(scratch), length, "relayout", invalidation->requiresRelayout);
    length = WriteFlag(scratch, sizeof(scratch), length, "repaint", invalidation->requiresRepaint);
    length = WriteFlag(scratch, sizeof(scratch), length, "selection", invalidation->requiresSelectionRemap);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " text ");
    length = WriteRange(scratch, sizeof(scratch), length, invalidation->textRange);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " shaping ");
    length = WriteRange(scratch, sizeof(scratch), length, invalidation->shapingRange);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " layout ");
    length = WriteRange(scratch, sizeof(scratch), length, invalidation->layoutRange);
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return EditingPublish(scratch, length, buffer, capacity, outLength);
}

Result RoboTextCompositionWrite(const RoboTextComposition *composition,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    char scratch[384];
    size_t length = 0u;

    if (!composition) {
        return RoboInvalidArgument;
    }
    if (!composition->active) {
        return EditingPublish("none", 4u, buffer, capacity, outLength);
    }
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "composition ");
    length = WriteRange(scratch, sizeof(scratch), length, composition->range);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " text ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length,
                                    RoboTextBufferScalarCount(composition->text));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " scalars before ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length,
                                    RoboTextBufferScalarCount(composition->textBefore));
    length = WriteFlag(scratch, sizeof(scratch), length, "selected", composition->hasSelection);
    length = WriteFlag(scratch, sizeof(scratch), length, "clause", composition->hasClause);
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return EditingPublish(scratch, length, buffer, capacity, outLength);
}

Result RoboTextEditorWrite(const RoboTextEditor *editor,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength)
{
    char scratch[768];
    size_t length = 0u;

    if (!editor) {
        return RoboInvalidArgument;
    }
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "editor ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, RoboTextBufferScalarCount(editor->text));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " scalars ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, RoboTextBufferByteLength(editor->text));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, "B selection ");
    length = WriteRange(scratch, sizeof(scratch), length, RoboTextSelectionRange(editor->selection));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " direction ");
    length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                RoboTextSelectionDirectionName(RoboTextSelectionDirectionOf(editor->selection)));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " caret ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, editor->caret.position.offset);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " ");
    length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                RoboTextAffinityName(editor->caret.affinity));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " boundary ");
    length = RoboTextWriterText(scratch, sizeof(scratch), length,
                                RoboTextEditBoundaryName(RoboTextEditorBoundary(editor)));
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " composing ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, editor->composition.active ? 1u : 0u);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " edits ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)editor->edits);
    length = RoboTextWriterText(scratch, sizeof(scratch), length, " noops ");
    length = RoboTextWriterUnsigned(scratch, sizeof(scratch), length, (uint32_t)editor->noops);
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    return EditingPublish(scratch, length, buffer, capacity, outLength);
}

/* ------------------------------------------------------------------ */
/* Fuzzing                                                             */
/* ------------------------------------------------------------------ */

/** Every invariant an editor must hold, whatever was done to it. */
static int EditingInvariantsHold(const RoboTextEditor *editor)
{
    RoboTextScalarIndex scalars = RoboTextBufferScalarCount(editor->text);
    RoboTextRange selection = RoboTextSelectionRange(editor->selection);

    if (!RoboTextPositionIsWithin(editor->caret.position, scalars)) {
        return 0;
    }
    if (selection.end > scalars || RoboTextRangeIsInverted(selection)) {
        return 0;
    }
    if (!RoboTextAffinityIsKnown(editor->caret.affinity)) {
        return 0;
    }
    if (editor->composition.active) {
        if (editor->composition.range.end > scalars ||
            editor->composition.range.start > editor->composition.range.end) {
            return 0;
        }
    }
    return 1;
}

/** The text grows only while it is small, so a fuzz input cannot make the run quadratic in its own size. */
#define EditingFuzzMaxScalars 2048u

Result RoboTextEditingFuzz(const uint8_t *data, size_t size)
{
    RoboTextEditor *editor = NULL;
    RoboTextBuffer *payload = NULL;
    RoboTextEditResult result;
    size_t offset = 0u;
    size_t steps;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    /*
     * The bytes are *text* as well as commands, so they go through the buffer door once: arbitrary bytes
     * become valid UTF-8 by the module's one REPLACE policy, and the editor's own "an insertion must be valid
     * UTF-8" rule is therefore exercised rather than bypassed.
     */
    if (RoboTextBufferCreateFrom(&payload, (const char *)data, size, NULL) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (RoboTextEditorCreateFromBuffer(payload, &editor, NULL) != RoboOk) {
        RoboTextBufferDestroy(payload);
        return RoboOutOfMemory;
    }
    RoboTextEditResultInit(&result);
    /*
     * A bounded number of steps from a bounded input: a fuzz entry that could loop is a fuzz entry that hangs
     * a test runner, and this invariant list is about the *state* the editor reaches rather than about
     * visiting every byte.
     */
    steps = size < 512u ? size : 512u;
    while (offset + 2u < steps) {
        RoboTextScalarIndex scalars = RoboTextEditorScalarCount(editor);
        uint8_t op = data[offset];
        uint32_t a = (uint32_t)data[offset + 1u];
        uint32_t b = (uint32_t)data[offset + 2u];
        RoboTextScalarIndex start = (RoboTextScalarIndex)(a % (scalars + 1u));
        RoboTextScalarIndex end = (RoboTextScalarIndex)(b % (scalars + 1u));

        if (start > end) {
            RoboTextScalarIndex swap = start;

            start = end;
            end = swap;
        }
        switch (op % 9u) {
            case 0u:
                (void)RoboTextEditorReplace(editor, RoboTextRangeMake(start, end),
                                            RoboTextBufferBytes(payload), RoboTextBufferByteLength(payload),
                                            &result, NULL);
                break;
            case 1u:
                if (scalars < EditingFuzzMaxScalars) {
                    (void)RoboTextEditorInsert(editor, RoboTextBufferBytes(payload),
                                               RoboTextBufferByteLength(payload), &result, NULL);
                }
                break;
            case 2u:
                (void)RoboTextEditorDeleteBackward(editor, &result, NULL);
                break;
            case 3u:
                (void)RoboTextEditorDeleteForward(editor, &result, NULL);
                break;
            case 4u:
                (void)RoboTextEditorUpdateComposition(editor, RoboTextBufferBytes(payload),
                                                      RoboTextBufferByteLength(payload), RoboTextRangeAt(0u), 1,
                                                      RoboTextRangeAt(0u), 1, &result, NULL);
                break;
            case 5u:
                (void)RoboTextEditorCommitComposition(editor, &result, NULL);
                break;
            case 6u:
                (void)RoboTextEditorCancelComposition(editor, &result, NULL);
                break;
            case 7u:
                (void)RoboTextEditorSetSelection(
                    editor, RoboTextSelectionMake(RoboTextPositionMake(start), RoboTextPositionMake(end)));
                break;
            default:
                (void)RoboTextEditorSetCaret(editor, RoboTextPositionMake(end),
                                             (offset & 1u) ? RoboTextAffinityUpstream
                                                           : RoboTextAffinityDownstream);
                break;
        }
        if (!EditingInvariantsHold(editor)) {
            RoboTextEditResultDestroy(&result);
            RoboTextEditorDestroy(editor);
            RoboTextBufferDestroy(payload);
            return RoboError;
        }
        offset += 3u;
    }
    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
    RoboTextBufferDestroy(payload);
    return RoboOk;
}
