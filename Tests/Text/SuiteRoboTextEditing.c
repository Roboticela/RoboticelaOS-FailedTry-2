/*
 * Text editing — `text.editing.*` (Phase 12.22).
 *
 * ## The claim this suite is arranged around
 *
 * > Editing operates on logical source text; shaping and rendering are downstream consequences of text
 * > mutations.
 *
 * Every case below is one of three things: a **mutation** (an edit expressed as a replacement of a source
 * range), a **unit** (the boundary a delete or a movement uses), or a **consequence** (the record a consumer
 * of shaped text needs to redo its work). No case asks a glyph buffer anything, and a case that did would be
 * testing the layer below this one.
 *
 * The two directions are asked in the same file and meet in the middle, which is the whole point:
 *
 *   - **text → shape**: after an edit, the editor's own bytes are shaped and compared with the shape of the
 *     same text written directly (`ConfResultsEqual`), so "the edit reshapes" is a fact about the text rather
 *     than about a glyph someone remembered;
 *   - **shape → text**: a shaped run list supplies the cluster boundaries (`RoboTextEditBoundariesFromRuns`),
 *     so a ligature, an Arabic joining sequence or a Devanagari conjunct is **one** editing unit — and the
 *     cases that do not install a resolver show the character rule standing on its own.
 *
 * ## Where the numbers in this file come from
 *
 * Ranges, caret offsets and byte counts are arithmetic over the scalars the case itself spells in UTF-8, so a
 * reader can count them. Glyph ids and cluster spans come from the **fixture's builder** (`face.ids.ligaFfi`)
 * or from `ConfExpectClusters`, never from a session. Affinity and the safe-boundary rule come from the
 * module's own doors (12.19's `RoboTextComplexIsSafeBoundary`), which is what makes "an edit never lands
 * inside a cluster" a claim the layer below already answers.
 *
 * ## What is deliberately absent
 *
 * `RoboInput`, keyboard scan codes, mouse events, a clipboard backend, an undo *manager* (the record is here,
 * the history is 12.23's and `Undo`/`Redo` are refused by name), word and line boundaries (refused rather
 * than approximated with `± 1`), rich text, spell checking and autocorrect.
 */

#include "RoboTextComplexFont.h"
#include "RoboTextConformanceFont.h"
#include "RoboTextConformanceSupport.h"

#include "RoboUnicode/RoboUnicode.h"

#include <stdio.h>
#include <string.h>

#define EditingSizePx 16.0f

/* The scalars, spelled in UTF-8 so a reader can see which character each case is about. */
#define EditingAscii "Hello"
#define EditingFfi "ffi"
/*
 * One of every width the decoder has: one byte, two, three and four, each followed by a case that would
 * tempt a byte-wise step. The order is the order the text has, because the deletes below walk it backwards.
 */
#define EditingMixed "A" EditingLatinE1 EditingGreek EditingCyrillic EditingCjk EditingEmoji EditingArabic \
                     EditingIndic
#define EditingLatinE1 "\xC3\xA9"        /* U+00E9, two bytes — Latin Extended */
#define EditingGreek "\xCE\xA9"          /* U+03A9, two bytes  */
#define EditingCyrillic "\xD0\x91"       /* U+0411, two bytes  */
#define EditingCjk "\xE4\xB8\x80"        /* U+4E00, three bytes */
#define EditingArabic "\xD8\xA8"         /* U+0628, two bytes  */
#define EditingIndic "\xE0\xA4\x95"      /* U+0915, three bytes */
#define EditingEmoji "\xF0\x9F\x98\x80"  /* U+1F600, four bytes */
/* `e` + U+0301 + U+0302: one character, three scalars, five bytes. */
#define EditingBaseMark "e\xCC\x81\xCC\x82"
/* The two marks on their own: no base, and still not a boundary inside them. */
#define EditingMarks "\xCC\x81\xCC\x82"

/* The complex fixture's texts, in logical order and never reversed — see `SuiteRoboTextArabic.c`. */
#define EditingLamAlef "\xD9\x84\xD8\xA7"                  /* `ل` + `ا`, the ligature */
#define EditingLam "\xD9\x84"                              /* `ل` alone */
#define EditingBehBehBeh "\xD8\xA8\xD8\xA8\xD8\xA8"        /* three dual-joining letters */
#define EditingBehBeh "\xD8\xA8\xD8\xA8"
#define EditingKa "\xE0\xA4\x95"                           /* U+0915 */
#define EditingVirama "\xE0\xA5\x8D"                       /* U+094D */
#define EditingSsa "\xE0\xA4\xB7"                          /* U+0937 */
#define EditingKaViramaSsa EditingKa EditingVirama EditingSsa

/* The vertical fixture's texts. `あ` + U+3099 is one character in a column. */
#define EditingKanaVoicing "\xE3\x81\x82\xE3\x82\x99"
#define EditingLatinAB "AB"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** An editor over a NUL-terminated string, asserting the call. */
static RoboTextEditor *EditingEditorOf(const char *text)
{
    RoboTextEditor *editor = NULL;

    RoboCheckEqInt((int)RoboTextEditorCreateFrom(ConfView(text), &editor, NULL), (int)RoboOk);
    RoboCheckMsg(editor != NULL, "an editor was not created");
    return editor;
}

/**
 * Assert the editor's text is @p expected — and, in the same breath, that it is **valid UTF-8**.
 *
 * The second half is the invariant the whole phase is arranged around, so it is checked at every mutation
 * rather than once at the end: a delete that split a sequence would show up here and nowhere else.
 */
static void EditingExpectText(const RoboTextEditor *editor, const char *expected, const char *label)
{
    const RoboTextBuffer *buffer = RoboTextEditorText(editor);
    const char *bytes = RoboTextBufferBytes(buffer);
    size_t length = RoboTextBufferByteLength(buffer);
    size_t want = strlen(expected);
    int same = length == want && (want == 0u || memcmp(bytes, expected, want) == 0);

    if (!same) {
        fprintf(stdout, "    %s: the editor holds \"%.*s\" (%u bytes), expected \"%s\" (%u bytes)\n", label,
                (int)length, bytes, (unsigned)length, expected, (unsigned)want);
    }
    RoboCheck(same);
    RoboCheckMsg(UnicodeUtf8Validate(bytes, length) != 0, "an edit left the text ill-formed");
}

/** Assert the caret's offset **and** that the selection is collapsed on it. */
static void EditingExpectCaret(const RoboTextEditor *editor, RoboTextScalarIndex offset, const char *label)
{
    RoboTextCaret caret = RoboTextEditorCaret(editor);

    if (caret.position.offset != offset) {
        fprintf(stdout, "    %s: the caret is at %u, expected %u\n", label, (unsigned)caret.position.offset,
                (unsigned)offset);
    }
    RoboCheckEqInt((int)caret.position.offset, (int)offset);
    RoboCheckEqInt(RoboTextSelectionIsEmpty(RoboTextEditorSelection(editor)), 1);
}

/** Shape a view through the defaults every case uses. */
static RoboTextShapeResult *EditingShapeView(const ConfFace *face, RoboTextView view)
{
    RoboTextShapeOptions options;
    RoboTextShapeResult *result = NULL;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    RoboCheckEqInt((int)RoboTextShape(view, &face->font, &options, &result, NULL), (int)RoboOk);
    RoboCheckMsg(result != NULL, "shaping produced no result");
    return result;
}

/** Shape **the editor's own text** — the direction `text → shape` every post-edit case asks. */
static RoboTextShapeResult *EditingShapeEditor(const ConfFace *face, const RoboTextEditor *editor)
{
    const RoboTextBuffer *buffer = RoboTextEditorText(editor);

    return EditingShapeView(face, RoboTextViewMake(RoboTextBufferBytes(buffer), RoboTextBufferByteLength(buffer)));
}

/** Shape @p text and read the cluster resolver off the result's own run list. */
static RoboTextEditBoundaries *EditingBoundariesOf(const ConfFace *face,
                                                   const char *text,
                                                   RoboTextShapeResult **out)
{
    RoboTextShapeResult *result = ConfShapeDefault(face, text);
    RoboTextEditBoundaries *boundaries = RoboTextEditBoundariesFromRuns(RoboTextShapeResultGlyphRunList(result));

    RoboCheckMsg(boundaries != NULL, "a shaped list produced no clusters");
    if (out) {
        *out = result;
    } else {
        RoboTextShapeResultDestroy(result);
    }
    return boundaries;
}

/* ------------------------------------------------------------------ */
/* §1–13 — the vocabulary: commands, boundaries, caret, invalidation   */
/* ------------------------------------------------------------------ */

static void TestEditingVocabulary(void)
{
    RoboTextCaret caret;
    RoboTextCaret other;
    RoboTextEditInvalidation invalidation;

    /* Commands: eighteen named, and a value from beyond the enum is named rather than mistaken. */
    RoboCheckEqInt(RoboTextEditCommandIsKnown(RoboTextEditCommandNone), 0);
    RoboCheckEqInt(RoboTextEditCommandIsKnown(RoboTextEditCommandInsertText), 1);
    RoboCheckEqInt(RoboTextEditCommandIsKnown(RoboTextEditCommandRedo), 1);
    RoboCheckEqInt(RoboTextEditCommandIsKnown((RoboTextEditCommand)99), 0);
    RoboCheckStrEq(RoboTextEditCommandName(RoboTextEditCommandNone), "none");
    RoboCheckStrEq(RoboTextEditCommandName(RoboTextEditCommandDeleteBackward), "delete_backward");
    RoboCheckStrEq(RoboTextEditCommandName(RoboTextEditCommandMoveToLineStart), "move_to_line_start");
    RoboCheckStrEq(RoboTextEditCommandName((RoboTextEditCommand)99), "unknown");

    /* Boundaries: three answered, three refused **by name**. */
    RoboCheckEqInt(RoboTextEditBoundaryIsKnown(RoboTextEditBoundaryScalar), 1);
    RoboCheckEqInt(RoboTextEditBoundaryIsKnown(RoboTextEditBoundaryLineEnd), 1);
    RoboCheckEqInt(RoboTextEditBoundaryIsKnown((RoboTextEditBoundary)9), 0);
    RoboCheckStrEq(RoboTextEditBoundaryName(RoboTextEditBoundaryScalar), "scalar");
    RoboCheckStrEq(RoboTextEditBoundaryName(RoboTextEditBoundaryCluster), "cluster");
    RoboCheckStrEq(RoboTextEditBoundaryName((RoboTextEditBoundary)9), "unknown");
    RoboCheckEqInt(RoboTextEditBoundaryIsSupported(RoboTextEditBoundaryScalar), 1);
    RoboCheckEqInt(RoboTextEditBoundaryIsSupported(RoboTextEditBoundaryCharacter), 1);
    RoboCheckEqInt(RoboTextEditBoundaryIsSupported(RoboTextEditBoundaryCluster), 1);
    RoboCheckEqInt(RoboTextEditBoundaryIsSupported(RoboTextEditBoundaryWord), 0);
    RoboCheckEqInt(RoboTextEditBoundaryIsSupported(RoboTextEditBoundaryLineStart), 0);
    RoboCheckEqInt(RoboTextEditBoundaryIsSupported(RoboTextEditBoundaryLineEnd), 0);

    /* The caret is a logical position, an affinity and two flags — and no geometry. */
    RoboTextCaretInit(&caret);
    RoboCheckEqInt((int)caret.position.offset, 0);
    RoboCheckEqInt((int)caret.affinity, (int)RoboTextAffinityDownstream);
    RoboCheckEqInt(caret.visible, 1);
    RoboCheckEqInt(caret.enabled, 1);
    RoboCheckEqInt(RoboTextCaretIsValid(&caret), 1);
    other = RoboTextCaretMake(RoboTextPositionMake(3u), RoboTextAffinityUpstream);
    RoboCheckEqInt(RoboTextCaretEqual(&caret, &other), 0);
    RoboCheckEqInt(RoboTextCaretEqual(&caret, NULL), 0);
    RoboCheckEqInt(RoboTextCaretEqual(NULL, NULL), 1);
    caret.affinity = (RoboTextAffinity)7;
    RoboCheckEqInt(RoboTextCaretIsValid(&caret), 0);

    /* An uninitialised invalidation asks for nothing; one flag set makes it ask for something. */
    RoboTextEditInvalidationInit(&invalidation);
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(&invalidation), 1);
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(NULL), 1);
    invalidation.requiresRepaint = 1;
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(&invalidation), 0);
}

/* ------------------------------------------------------------------ */
/* §2–8, 35 — one mutation, and the four ways to reach it              */
/* ------------------------------------------------------------------ */

static void TestEditingBasic(void)
{
    RoboTextEditor *editor = NULL;
    RoboTextEditResult result;

    RoboCheckEqInt((int)RoboTextEditorCreate(&editor, NULL), (int)RoboOk);
    RoboCheck(editor != NULL);
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 0);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 0);
    RoboCheckEqInt(RoboTextEditorIsComposing(editor), 0);
    RoboCheck(RoboTextEditorLastOperation(editor) != NULL);
    RoboCheckEqInt(RoboTextEditOperationChanged(RoboTextEditorLastOperation(editor)), 0);
    RoboTextEditResultInit(&result);

    /* Insert into an empty document: the selection is collapsed after what was typed. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "Hello", 5u, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 1);
    EditingExpectText(editor, "Hello", "editing.basic");
    EditingExpectCaret(editor, 5u, "editing.basic");
    RoboCheckEqInt((int)result.affectedRange.start, 0);
    RoboCheckEqInt((int)result.affectedRange.end, 5);
    RoboCheckEqInt((int)result.operation.replacedRange.start, 0);
    RoboCheckEqInt((int)result.operation.replacedRange.end, 0);
    RoboCheckEqInt((int)RoboTextEditOperationInsertedBytes(&result.operation), 5);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 0);
    RoboCheckEqInt(RoboTextEditOperationChanged(&result.operation), 1);
    RoboCheckEqInt((int)result.selection.focus.offset, 5);
    RoboCheckEqInt((int)result.caret.affinity, (int)RoboTextAffinityDownstream);
    /* The record undo will need: nothing was deleted, five bytes arrived, and the caret moved 0 → 5. */
    RoboCheckEqInt((int)result.operation.caretBefore.position.offset, 0);
    RoboCheckEqInt((int)result.operation.caretAfter.position.offset, 5);
    RoboCheckEqInt((int)result.operation.replacedRange.start, 0);

    /* In the middle, and at the end. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityUpstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).affinity, (int)RoboTextAffinityUpstream);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "-", 1u, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "He-llo", "editing.basic");
    EditingExpectCaret(editor, 3u, "editing.basic");
    /* An insert collapses the selection and takes the affinity the door documents, not the caller's. */
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).affinity, (int)RoboTextAffinityDownstream);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(6u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "!", 1u, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "He-llo!", "editing.basic");

    /* A replace of a range: the deletion and the insertion are one operation on one range. */
    RoboCheckEqInt((int)RoboTextEditorReplace(editor, RoboTextRangeMake(3u, 6u), "LLO", 3u, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "He-LLO!", "editing.basic");
    EditingExpectCaret(editor, 6u, "editing.basic");
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 3);
    RoboCheckEqInt((int)RoboTextEditOperationInsertedBytes(&result.operation), 3);
    RoboCheckEqInt((int)RoboTextBufferByteLength(result.operation.deleted), 3);
    RoboCheckEqInt((int)RoboTextBufferByteLength(result.operation.inserted), 3);

    /* Backspace and forward delete over one character. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(6u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "He-LL!", "editing.basic");
    EditingExpectCaret(editor, 5u, "editing.basic");
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(0u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteForward(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "e-LL!", "editing.basic");
    EditingExpectCaret(editor, 0u, "editing.basic");

    /* A non-empty selection makes typing replace it (12.22.8). */
    RoboCheckEqInt((int)RoboTextEditorSetSelection(
                        editor, RoboTextSelectionMake(RoboTextPositionMake(0u), RoboTextPositionMake(3u))),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorReplaceSelection(editor, "XY", 2u, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "XYL!", "editing.basic");
    EditingExpectCaret(editor, 2u, "editing.basic");

    /* …and a backspace with a selection deletes the selection, not a unit before it. */
    RoboCheckEqInt((int)RoboTextEditorSetSelection(
                        editor, RoboTextSelectionMake(RoboTextPositionMake(2u), RoboTextPositionMake(0u))),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "L!", "editing.basic");
    EditingExpectCaret(editor, 0u, "editing.basic");

    /* At the start of the text a backspace is `RoboOk` with `changed` 0 — a no-op, not an error. */
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)result.status, (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 0);
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(&result.invalidation), 1);
    EditingExpectText(editor, "L!", "editing.basic");

    /* …and at the end, a forward delete is too. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteForward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 0);
    EditingExpectText(editor, "L!", "editing.basic");

    /* The selection's own bytes, which is what a clipboard will be handed (12.22.38). */
    {
        RoboTextBuffer *selected = NULL;

        RoboCheckEqInt((int)RoboTextEditorSetSelection(
                            editor, RoboTextSelectionMake(RoboTextPositionMake(0u), RoboTextPositionMake(1u))),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTextEditorSelectedText(editor, &selected, NULL), (int)RoboOk);
        RoboCheck(selected != NULL);
        RoboCheckEqInt((int)RoboTextBufferByteLength(selected), 1);
        RoboCheckEqInt(memcmp(RoboTextBufferBytes(selected), "L", 1u), 0);
        RoboTextBufferDestroy(selected);
        selected = NULL;
        /* A collapsed selection copies nothing rather than failing. */
        RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(1u), RoboTextAffinityDownstream), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextEditorSelectedText(editor, &selected, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTextBufferByteLength(selected), 0);
        RoboTextBufferDestroy(selected);
    }

    /* The whole document, deleted in one replace. */
    RoboCheckEqInt((int)RoboTextEditorReplace(editor, RoboTextRangeFull(RoboTextEditorScalarCount(editor)), NULL,
                                              0u, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "", "editing.basic");
    EditingExpectCaret(editor, 0u, "editing.basic");
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 2);
    RoboCheckEqInt((int)RoboTextEditOperationInsertedBytes(&result.operation), 0);

    /* The misuses: no editor, no bytes, a range outside the text, and a construction with no out pointer. */
    RoboCheckEqInt((int)RoboTextEditorInsert(NULL, "x", 1u, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, NULL, 1u, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorReplace(editor, RoboTextRangeMake(0u, 5u), "x", 1u, &result, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(NULL, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorSelectedText(editor, NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorSetSelection(NULL, RoboTextSelectionAt(RoboTextPositionMake(0u))),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorCreateFrom(ConfView("x"), NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorCreateFromBuffer(NULL, &editor, NULL), (int)RoboInvalidArgument);
    /* The edit that failed changed nothing, and the editor is still usable. */
    EditingExpectText(editor, "", "editing.basic");
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "ok", 2u, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "ok", "editing.basic");

    /* A selection is clamped to the text rather than refused: an editor told the text shrank is not a bug. */
    RoboCheckEqInt((int)RoboTextEditorSetSelection(
                        editor, RoboTextSelectionMake(RoboTextPositionMake(0u), RoboTextPositionMake(99u))),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).end, 2);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 2);
    /* And an affinity out of range is refused rather than stored. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(1u), (RoboTextAffinity)7),
                   (int)RoboInvalidArgument);

    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
    RoboTextEditorDestroy(NULL);
}

/* ------------------------------------------------------------------ */
/* §39–42 — the command layer, which is input's boundary and not input */
/* ------------------------------------------------------------------ */

static void TestEditingCommands(void)
{
    RoboTextEditor *editor = EditingEditorOf("Hello");
    RoboTextEditResult result;
    RoboTextSelection selection;

    RoboTextEditResultInit(&result);

    /* The names this build refuses, and the misuse that is not a command at all. */
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, (RoboTextEditCommand)99, NULL, 0u, NULL, &result, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandNone, NULL, 0u, NULL, &result, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandUndo, NULL, 0u, NULL, &result, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandRedo, NULL, 0u, NULL, &result, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveUp, NULL, 0u, NULL, &result, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveDown, NULL, 0u, NULL, &result, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveToLineStart, NULL, 0u, NULL, &result, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveToLineEnd, NULL, 0u, NULL, &result, NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditorCommand(NULL, RoboTextEditCommandCopy, NULL, 0u, NULL, &result, NULL),
                   (int)RoboInvalidArgument);
    /* A refusal changes nothing: the text and the caret are where they were. */
    EditingExpectText(editor, "Hello", "editing.commands");
    EditingExpectCaret(editor, 0u, "editing.commands");

    /* A movement moves no text, so it asks for no work. */
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveRight, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectCaret(editor, 1u, "editing.commands");
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(&result.invalidation), 1);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 0);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveLeft, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectCaret(editor, 0u, "editing.commands");
    /* At either end it stays: a boundary, not a wrap. */
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveLeft, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectCaret(editor, 0u, "editing.commands");
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(5u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandMoveRight, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectCaret(editor, 5u, "editing.commands");

    /* An extending move keeps the anchor and moves the focus: the direction is preserved, not discarded. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(1u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandSelectRight, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    selection = RoboTextEditorSelection(editor);
    RoboCheckEqInt((int)selection.anchor.offset, 1);
    RoboCheckEqInt((int)selection.focus.offset, 2);
    RoboCheckEqInt((int)RoboTextSelectionDirectionOf(selection), (int)RoboTextSelectionDirectionForward);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandSelectLeft, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    selection = RoboTextEditorSelection(editor);
    RoboCheckEqInt((int)selection.anchor.offset, 1);
    RoboCheckEqInt((int)selection.focus.offset, 1);
    RoboCheckEqInt((int)RoboTextSelectionDirectionOf(selection), (int)RoboTextSelectionDirectionNone);

    /* Select all: the whole text, focus at its end. */
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandSelectAll, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).start, 0);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).end, 5);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 5);

    /* Copy reads the selection; with nowhere to put it, it is a no-op rather than a failure. */
    {
        RoboTextBuffer *copied = NULL;

        RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandCopy, NULL, 0u, &copied, &result, NULL),
                       (int)RoboOk);
        RoboCheck(copied != NULL);
        RoboCheckEqInt((int)RoboTextBufferByteLength(copied), 5);
        RoboCheckEqInt(memcmp(RoboTextBufferBytes(copied), "Hello", 5u), 0);
        RoboTextBufferDestroy(copied);
        copied = NULL;
        RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandCopy, NULL, 0u, NULL, &result, NULL),
                       (int)RoboOk);
        EditingExpectText(editor, "Hello", "editing.commands");

        /* Cut copies **and** deletes. */
        RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandCut, NULL, 0u, &copied, &result, NULL),
                       (int)RoboOk);
        RoboCheck(copied != NULL);
        RoboCheckEqInt((int)RoboTextBufferByteLength(copied), 5);
        RoboCheckEqInt(RoboTextEditResultChanged(&result), 1);
        EditingExpectText(editor, "", "editing.commands");
        RoboTextBufferDestroy(copied);
        copied = NULL;
    }

    /* Paste inserts what the caller holds — a paste is an insert. */
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandPaste, "Robo", 4u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "Robo", "editing.commands");
    EditingExpectCaret(editor, 4u, "editing.commands");
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandInsertText, "!", 1u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "Robo!", "editing.commands");

    /* The two deletes through the command layer reach the same doors. */
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandDeleteBackward, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "Robo", "editing.commands");
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(0u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorCommand(editor, RoboTextEditCommandDeleteForward, NULL, 0u, NULL, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "obo", "editing.commands");

    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §12, 46 — a UTF-8 sequence is never split                           */
/* ------------------------------------------------------------------ */

static void TestEditingUtf8(void)
{
    RoboTextView view = ConfView(EditingMixed);
    RoboTextEditor *editor = EditingEditorOf(EditingMixed);
    RoboTextEditResult result;
    RoboTextPosition out;

    RoboTextEditResultInit(&result);
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 8);
    RoboCheckEqInt((int)RoboTextBufferByteLength(RoboTextEditorText(editor)), 19);

    /*
     * The scalar step is one **scalar**: never one byte, and never half a sequence. That is what makes
     * `DeleteBackward` safe without knowing UTF-8 — and the counts below are the proof that it is.
     */
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(0u), RoboTextEditBoundaryScalar, &out,
                                                 NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 1);
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(1u), RoboTextEditBoundaryScalar, &out,
                                                 NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 2);
    /* Past the last scalar it stops at the end rather than running off it. */
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(8u), RoboTextEditBoundaryScalar, &out,
                                                 NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 8);
    RoboCheckEqInt((int)RoboTextEditBoundaryPrevious(view, RoboTextPositionMake(0u), RoboTextEditBoundaryScalar,
                                                     &out, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 0);

    /* A position outside the text is refused rather than clamped, and a NULL out with it. */
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(9u), RoboTextEditBoundaryScalar, &out,
                                                 NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(0u), RoboTextEditBoundaryScalar, NULL,
                                                 NULL),
                   (int)RoboInvalidArgument);
    /* The two refusals that are about segmentation, and the one that is about naming. */
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(0u), RoboTextEditBoundaryWord, &out,
                                                 NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(0u), RoboTextEditBoundaryLineStart, &out,
                                                 NULL),
                   (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(0u), (RoboTextEditBoundary)9, &out,
                                                 NULL),
                   (int)RoboInvalidArgument);

    /* Backwards from the end, one scalar at a time: three bytes, then two, then four, then three, then two,
     * two, two — each scalar's own width, and never a byte of the one before it. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(8u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 3);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.deleted), 1);
    EditingExpectText(editor, "A" EditingLatinE1 EditingGreek EditingCyrillic EditingCjk EditingEmoji EditingArabic,
                      "editing.utf8");
    RoboCheckEqInt((int)RoboTextBufferByteLength(RoboTextEditorText(editor)), 16);

    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 2);
    EditingExpectText(editor, "A" EditingLatinE1 EditingGreek EditingCyrillic EditingCjk EditingEmoji,
                      "editing.utf8");
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 4);
    EditingExpectText(editor, "A" EditingLatinE1 EditingGreek EditingCyrillic EditingCjk, "editing.utf8");
    RoboCheckEqInt((int)RoboTextBufferByteLength(RoboTextEditorText(editor)), 10);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 3);
    EditingExpectText(editor, "A" EditingLatinE1 EditingGreek EditingCyrillic, "editing.utf8");
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 2);
    EditingExpectText(editor, "A" EditingLatinE1 EditingGreek, "editing.utf8");
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 2);
    EditingExpectText(editor, "A" EditingLatinE1, "editing.utf8");
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 2);
    EditingExpectText(editor, "A", "editing.utf8");
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 1);
    EditingExpectText(editor, "", "editing.utf8");
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 0);

    /* An insertion that is not UTF-8 is **refused**, not repaired: the editor does not change. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "\x80", 1u, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "\xE4\xB8", 2u, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "\xF0\x9F\x98", 3u, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 0);
    EditingExpectText(editor, "", "editing.utf8");

    /*
     * A **buffer** handed ill-formed bytes keeps them: it is a byte store, and silently rewriting a caller's
     * text is the repair this module's other doors refuse. The two halves are therefore different doors — an
     * insertion *by a caller* is validated, a buffer *given* the bytes is read with the decoder's REPLACE
     * policy — and the scalar half still answers, so an editor over such a buffer is usable rather than wedged.
     */
    {
        RoboTextEditor *raw = EditingEditorOf("\x80");

        RoboCheckEqInt((int)RoboTextBufferByteLength(RoboTextEditorText(raw)), 1);
        RoboCheckEqInt(UnicodeUtf8Validate(RoboTextBufferBytes(RoboTextEditorText(raw)), 1u), 0);
        RoboCheckEqInt((int)RoboTextEditorScalarCount(raw), 1);
        /* The insertion door still refuses, over that very buffer. */
        RoboCheckEqInt((int)RoboTextEditorInsert(raw, "\x80", 1u, &result, NULL), (int)RoboInvalidArgument);
        RoboTextEditorDestroy(raw);
    }

    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §18 — a base and its marks are one editing unit                      */
/* ------------------------------------------------------------------ */

static void TestEditingMarks(void)
{
    RoboTextView view = ConfView(EditingBaseMark);
    RoboTextView marks = ConfView(EditingMarks);
    RoboTextEditor *editor = EditingEditorOf(EditingBaseMark);
    RoboTextEditResult result;
    RoboTextPosition out;

    RoboTextEditResultInit(&result);
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 3);
    RoboCheckEqInt((int)RoboTextBufferByteLength(RoboTextEditorText(editor)), 5);

    /* One step back from the end is the whole character: the base **and** both marks. */
    RoboCheckEqInt((int)RoboTextEditBoundaryPrevious(view, RoboTextPositionMake(3u),
                                                     RoboTextEditBoundaryCharacter, &out, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 0);
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(view, RoboTextPositionMake(0u), RoboTextEditBoundaryCharacter, &out,
                                                 NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 3);
    /* The scalar unit does split it, which is exactly the defect the character unit exists to prevent. */
    RoboCheckEqInt((int)RoboTextEditBoundaryPrevious(view, RoboTextPositionMake(3u), RoboTextEditBoundaryScalar,
                                                     &out, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 2);

    /* So one backspace takes three scalars, and the deleted record says so. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 5);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.deleted), 3);
    EditingExpectText(editor, "", "editing.marks");
    RoboTextEditorDestroy(editor);

    /* The forward half is the mirror image. */
    editor = EditingEditorOf(EditingBaseMark);
    RoboCheckEqInt((int)RoboTextEditorDeleteForward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&result.operation), 5);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.deleted), 3);
    EditingExpectText(editor, "", "editing.marks");
    RoboTextEditorDestroy(editor);

    /* Marks with no base are still not a boundary inside them: nothing here offers to cut between them. */
    RoboCheckEqInt((int)RoboTextEditBoundaryPrevious(marks, RoboTextPositionMake(2u),
                                                     RoboTextEditBoundaryCharacter, &out, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 0);
    RoboCheckEqInt((int)RoboTextEditBoundaryNext(marks, RoboTextPositionMake(0u), RoboTextEditBoundaryCharacter,
                                                 &out, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)out.offset, 2);
    /* And a caret inside a character is a position the editor will still hold — it moves no text. */
    editor = EditingEditorOf(EditingBaseMark);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(1u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 1);
    RoboCheckEqInt((int)RoboTextEditorDeleteForward(editor, &result, NULL), (int)RoboOk);
    /* …and a forward delete from inside still takes whole scalars, so the text stays well-formed. */
    EditingExpectText(editor, "e", "editing.marks");
    RoboTextEditorDestroy(editor);

    RoboTextEditResultDestroy(&result);
}

/* ------------------------------------------------------------------ */
/* §17, 48 — a ligature is one *cluster*, never one unit of text        */
/* ------------------------------------------------------------------ */

static void TestEditingLigature(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *shaped = NULL;
    RoboTextEditBoundaries *boundaries;
    RoboTextEditor *editor;
    RoboTextEditResult edit;
    RoboTextRange cluster;
    uint16_t expected[1];
    uint32_t expectedClusters[3];

    memset(&spec, 0, sizeof(spec));
    ConfSpecEuropean(&spec);
    ConfFaceOpen(&face, &spec, EditingSizePx);
    RoboTextEditResultInit(&edit);

    /* The font's own bytes state the ligature, and the run says its source is all three scalars. */
    expected[0] = face.ids.ligaFfi;
    shaped = ConfShapeDefault(&face, EditingFfi);
    ConfExpectGlyphs(shaped, expected, 1u, "editing.ligature", EditingFfi);
    expectedClusters[0] = 0u;
    expectedClusters[1] = 3u;
    expectedClusters[2] = 1u;
    ConfExpectClusters(shaped, 0u, expectedClusters, 1u, "editing.ligature", EditingFfi);

    boundaries = RoboTextEditBoundariesFromRuns(RoboTextShapeResultGlyphRunList(shaped));
    RoboCheck(boundaries != NULL);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(boundaries), 1);
    cluster = RoboTextEditBoundariesClusterAt(boundaries, RoboTextPositionMake(0u));
    RoboCheckEqInt((int)cluster.start, 0);
    RoboCheckEqInt((int)cluster.end, 3);
    /* At the text's end there is no cluster, and the resolver says so with an empty range. */
    cluster = RoboTextEditBoundariesClusterAt(boundaries, RoboTextPositionMake(3u));
    RoboCheckEqInt(RoboTextRangeIsEmpty(cluster), 1);
    /* Its two steps are the clusters' edges, and a NULL resolver or a NULL out is a misuse. */
    {
        RoboTextPosition before;

        RoboCheckEqInt((int)RoboTextEditBoundariesPrevious(boundaries, RoboTextPositionMake(3u), &before, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)before.offset, 0);
        RoboCheckEqInt((int)RoboTextEditBoundariesNext(boundaries, RoboTextPositionMake(0u), &before, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)before.offset, 3);
        RoboCheckEqInt((int)RoboTextEditBoundariesNext(NULL, RoboTextPositionMake(0u), &before, NULL),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTextEditBoundariesPrevious(boundaries, RoboTextPositionMake(3u), NULL, NULL),
                       (int)RoboInvalidArgument);
    }

    /* Installed, the ligature is one editing unit: one backspace, three scalars. */
    editor = EditingEditorOf(EditingFfi);
    RoboCheckEqInt((int)RoboTextEditorBoundary(editor), (int)RoboTextEditBoundaryCharacter);
    RoboCheckEqInt((int)RoboTextEditorSetBoundaries(editor, boundaries), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorBoundary(editor), (int)RoboTextEditBoundaryCluster);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 3);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(edit.operation.deleted), 3);
    EditingExpectText(editor, "", "editing.ligature");

    /*
     * And the typed path in the same editor: the source text is what the editor changes, and the ligature is
     * the **shaper's** answer about it. The comparison is against the same text built directly, so it cannot
     * pass by remembering an id.
     */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "f", 1u, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "f", 1u, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "i", 1u, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 3);
    EditingExpectText(editor, EditingFfi, "editing.ligature");
    {
        RoboTextShapeResult *typed = EditingShapeEditor(&face, editor);
        RoboTextShapeResult *direct = ConfShapeDefault(&face, EditingFfi);

        RoboCheckMsg(ConfResultsEqual(typed, direct), "the typed text does not shape like the text");
        ConfExpectGlyphs(typed, expected, 1u, "editing.ligature", "typed ffi");
        RoboTextShapeResultDestroy(typed);
        RoboTextShapeResultDestroy(direct);
    }
    /* Uninstalling the resolver puts the character rule back, and a NULL is how a caller does it. */
    RoboCheckEqInt((int)RoboTextEditorSetBoundaries(editor, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorBoundary(editor), (int)RoboTextEditBoundaryCharacter);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 1);
    EditingExpectText(editor, "ff", "editing.ligature");
    RoboTextEditorDestroy(editor);

    /* Without a resolver at all, the same backspace is one scalar: `ffi` is three characters of text. */
    editor = EditingEditorOf(EditingFfi);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 1);
    EditingExpectText(editor, "ff", "editing.ligature");
    RoboTextEditorDestroy(editor);

    /* A single-scalar run list still gives one cluster per scalar, so the resolver is not a downgrade. */
    {
        RoboTextShapeResult *plain = ConfShapeDefault(&face, "AB");
        RoboTextEditBoundaries *two = RoboTextEditBoundariesFromRuns(RoboTextShapeResultGlyphRunList(plain));

        RoboCheck(two != NULL);
        RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(two), 2);
        RoboCheckEqInt((int)RoboTextEditBoundariesClusterAt(two, RoboTextPositionMake(1u)).start, 1);
        RoboCheckEqInt((int)RoboTextEditBoundariesClusterAt(two, RoboTextPositionMake(1u)).end, 2);
        RoboTextEditBoundariesDestroy(two);
        RoboTextShapeResultDestroy(plain);
    }

    /* An empty list and a NULL list have no clusters, and the resolver refuses to be built over one. */
    RoboCheck(RoboTextEditBoundariesFromRuns(NULL) == NULL);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(NULL), 0);
    RoboTextEditBoundariesDestroy(NULL);

    RoboTextEditBoundariesDestroy(boundaries);
    RoboTextShapeResultDestroy(shaped);
    RoboTextEditResultDestroy(&edit);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §16, 47 — Arabic: joining is reshaped, and a cluster is one edit     */
/* ------------------------------------------------------------------ */

static void TestEditingArabic(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *shaped = NULL;
    RoboTextEditBoundaries *boundaries;
    RoboTextEditor *editor;
    RoboTextEditResult edit;
    uint32_t expectedClusters[3];

    memset(&spec, 0, sizeof(spec));
    CplxSpecComplex(&spec);
    ConfFaceOpen(&face, &spec, EditingSizePx);
    RoboTextEditResultInit(&edit);

    /* `ل` + `ا` is one ligature glyph over **two** scalars — the shortest text where cluster and character
     * disagree, and therefore the case that distinguishes them. */
    boundaries = EditingBoundariesOf(&face, EditingLamAlef, &shaped);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(boundaries), 1);
    expectedClusters[0] = 0u;
    expectedClusters[1] = 2u;
    expectedClusters[2] = 1u;
    ConfExpectClusters(shaped, 0u, expectedClusters, 1u, "editing.arabic", EditingLamAlef);

    /*
     * The two layers answer two **different** questions, and this is the case that shows it: the complex
     * module's clusters group a joining sequence, so `ل` and `ا` are two one-scalar clusters and a *line* may
     * break between them; the shaped run's cluster is the ligature glyph the font's bytes state, which is one
     * unit of shaped output. The editor reads the run's answer, because an edit that removed one letter of a
     * ligature would cut it in half — while a line breaker is allowed to break there.
     */
    {
        RoboTextComplexCluster clusters[8];
        size_t count = 0u;

        RoboCheckEqInt((int)RoboTextComplexClustersResolve(ConfView(EditingLamAlef), RoboTextRangeFull(2u),
                                                           clusters, 8u, &count, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 2);
        RoboCheckEqInt((int)clusters[0].source.start, 0);
        RoboCheckEqInt((int)clusters[0].source.end, 1);
        RoboCheckEqInt((int)clusters[1].source.start, 1);
        RoboCheckEqInt((int)clusters[1].source.end, 2);
        /* The door answers about a **cluster's own start**, so 0 and 1 are boundaries and 2 is not one. */
        RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 0u), 1);
        RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 1u), 1);
        RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 2u), 0);
    }

    /* With the resolver: one backspace removes both scalars, and lands on a safe boundary. */
    editor = EditingEditorOf(EditingLamAlef);
    RoboCheckEqInt((int)RoboTextEditorSetBoundaries(editor, boundaries), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 4);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(edit.operation.deleted), 2);
    EditingExpectText(editor, "", "editing.arabic");
    RoboTextEditorDestroy(editor);

    /* Without it: one scalar, and the text left behind is a lam the shaper re-forms. */
    editor = EditingEditorOf(EditingLamAlef);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 2);
    EditingExpectText(editor, EditingLam, "editing.arabic");
    {
        RoboTextShapeResult *left = EditingShapeEditor(&face, editor);
        RoboTextShapeResult *direct = ConfShapeDefault(&face, EditingLam);

        RoboCheckMsg(ConfResultsEqual(left, direct), "the lam left behind does not shape like a lam");
        ConfExpectGlyphCount(left, 1u, "editing.arabic", "the lam that is left");
        RoboTextShapeResultDestroy(left);
        RoboTextShapeResultDestroy(direct);
    }
    RoboTextEditorDestroy(editor);

    RoboTextEditBoundariesDestroy(boundaries);
    RoboTextShapeResultDestroy(shaped);
    shaped = NULL;
    boundaries = EditingBoundariesOf(&face, EditingBehBehBeh, &shaped);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(boundaries), 3);

    /* One cluster deleted from three joining letters leaves **text**, and the text reshapes as two letters
     * in the forms the new neighbours ask for — the editor never touched a contextual glyph. */
    editor = EditingEditorOf(EditingBehBehBeh);
    RoboCheckEqInt((int)RoboTextEditorSetBoundaries(editor, boundaries), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 2);
    EditingExpectText(editor, EditingBehBeh, "editing.arabic");
    {
        RoboTextShapeResult *after = EditingShapeEditor(&face, editor);
        RoboTextShapeResult *direct = ConfShapeDefault(&face, EditingBehBeh);

        ConfExpectGlyphCount(after, 2u, "editing.arabic", "two joining letters after a delete");
        RoboCheckMsg(ConfResultsEqual(after, direct), "the edited text does not shape like the text");
        RoboTextShapeResultDestroy(after);
        RoboTextShapeResultDestroy(direct);
    }
    /* …and typing the letter back restores the three-letter shaping exactly. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, EditingArabic, 2u, &edit, NULL), (int)RoboOk);
    EditingExpectText(editor, EditingBehBehBeh, "editing.arabic");
    {
        RoboTextShapeResult *restored = EditingShapeEditor(&face, editor);
        RoboTextShapeResult *direct = ConfShapeDefault(&face, EditingBehBehBeh);

        RoboCheckMsg(ConfResultsEqual(restored, direct), "the typed letter did not restore the shaping");
        ConfExpectGlyphCount(restored, 3u, "editing.arabic", "three joining letters after a re-insert");
        RoboTextShapeResultDestroy(restored);
        RoboTextShapeResultDestroy(direct);
    }
    RoboTextEditorDestroy(editor);

    RoboTextEditBoundariesDestroy(boundaries);
    RoboTextShapeResultDestroy(shaped);
    RoboTextEditResultDestroy(&edit);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §16, 47 — Indic: a conjunct is one cluster and one edit              */
/* ------------------------------------------------------------------ */

static void TestEditingIndic(void)
{
    ConfFontSpec spec;
    ConfFace face;
    RoboTextShapeResult *shaped = NULL;
    RoboTextEditBoundaries *boundaries;
    RoboTextEditor *editor;
    RoboTextEditResult edit;
    uint32_t expectedClusters[3];

    memset(&spec, 0, sizeof(spec));
    CplxSpecComplex(&spec);
    ConfFaceOpen(&face, &spec, EditingSizePx);
    RoboTextEditResultInit(&edit);

    /* `क` `्` `ष` is one conjunct glyph over three scalars. */
    boundaries = EditingBoundariesOf(&face, EditingKaViramaSsa, &shaped);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(boundaries), 1);
    expectedClusters[0] = 0u;
    expectedClusters[1] = 3u;
    expectedClusters[2] = 1u;
    ConfExpectClusters(shaped, 0u, expectedClusters, 1u, "editing.indic", EditingKaViramaSsa);

    /* And the safety rule refuses every index inside the conjunct, which is why the resolver exists. */
    {
        RoboTextComplexCluster clusters[8];
        size_t count = 0u;

        RoboCheckEqInt((int)RoboTextComplexClustersResolve(ConfView(EditingKaViramaSsa), RoboTextRangeFull(3u),
                                                           clusters, 8u, &count, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)count, 1);
        /* Every index *inside* the syllable is refused — 1 and 2 are the virama and the base — and 0 is the
         * cluster's own start. The text's end is not a cluster start, so the door does not call it one. */
        RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 0u), 1);
        RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 1u), 0);
        RoboCheckEqInt(RoboTextComplexIsSafeBoundary(clusters, count, 2u), 0);
    }

    /* One backspace removes all three scalars — nine bytes — and not a third of a conjunct. */
    editor = EditingEditorOf(EditingKaViramaSsa);
    RoboCheckEqInt((int)RoboTextEditorSetBoundaries(editor, boundaries), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 9);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(edit.operation.deleted), 3);
    EditingExpectText(editor, "", "editing.indic");
    RoboTextEditorDestroy(editor);

    /* Without it, one scalar: the conjunct becomes a consonant with a virama, which reshapes. */
    editor = EditingEditorOf(EditingKaViramaSsa);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 3);
    EditingExpectText(editor, EditingKa EditingVirama, "editing.indic");
    {
        RoboTextShapeResult *after = EditingShapeEditor(&face, editor);
        RoboTextShapeResult *direct = ConfShapeDefault(&face, EditingKa EditingVirama);

        RoboCheckMsg(ConfResultsEqual(after, direct), "the half form does not shape like the text");
        RoboTextShapeResultDestroy(after);
        RoboTextShapeResultDestroy(direct);
    }
    RoboTextEditorDestroy(editor);

    /* The typed path: the virama and the second consonant are inserted as **text**, and the conjunct is the
     * shaper's answer about it. Re-inserting the conjunct's own letters reforms it exactly. */
    editor = EditingEditorOf(EditingKa);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(1u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, EditingVirama, 3u, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, EditingSsa, 3u, &edit, NULL), (int)RoboOk);
    EditingExpectText(editor, EditingKaViramaSsa, "editing.indic");
    {
        RoboTextShapeResult *typed = EditingShapeEditor(&face, editor);
        RoboTextShapeResult *direct = ConfShapeDefault(&face, EditingKaViramaSsa);

        RoboCheckMsg(ConfResultsEqual(typed, direct), "the typed conjunct does not shape like the text");
        ConfExpectGlyphCount(typed, 1u, "editing.indic", "the conjunct two insertions produced");
        RoboTextShapeResultDestroy(typed);
        RoboTextShapeResultDestroy(direct);
    }
    /* And the resolver now sees the conjunct it created — the boundary is a function of the text. */
    {
        RoboTextShapeResult *typed = EditingShapeEditor(&face, editor);
        RoboTextEditBoundaries *now = RoboTextEditBoundariesFromRuns(RoboTextShapeResultGlyphRunList(typed));

        RoboCheck(now != NULL);
        RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(now), 1);
        RoboCheckEqInt((int)RoboTextEditBoundariesClusterAt(now, RoboTextPositionMake(0u)).end, 3);
        RoboTextEditBoundariesDestroy(now);
        RoboTextShapeResultDestroy(typed);
    }
    RoboTextEditorDestroy(editor);

    RoboTextEditBoundariesDestroy(boundaries);
    RoboTextShapeResultDestroy(shaped);
    RoboTextEditResultDestroy(&edit);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §31 — bidi is invalidated when the direction of the text can change  */
/* ------------------------------------------------------------------ */

static void TestEditingBidi(void)
{
    RoboTextEditor *editor = EditingEditorOf("Hello");
    RoboTextEditResult result;

    RoboTextEditResultInit(&result);

    /* Latin into Latin: a level run cannot move, so the flag is off. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "x", 1u, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(result.invalidation.changed, 1);
    RoboCheckEqInt(result.invalidation.requiresReshape, 1);
    RoboCheckEqInt(result.invalidation.requiresReanalysis, 1);
    RoboCheckEqInt(result.invalidation.requiresRelayout, 1);
    RoboCheckEqInt(result.invalidation.requiresRepaint, 1);
    RoboCheckEqInt(result.invalidation.requiresSelectionRemap, 1);
    RoboCheckEqInt(result.invalidation.requiresFontReselection, 1);
    /* The one that must **not** fire: inserting Latin cannot split an LTR paragraph's level runs. */
    RoboCheckEqInt(result.invalidation.requiresBidiReanalysis, 0);

    /* Inserting Arabic into a Latin paragraph can: the flag is the discrimination the field exists for. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, EditingArabic, 2u, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(result.invalidation.requiresBidiReanalysis, 1);
    /* A digit is included by name (UAX #9's I1/I2 give it a level the base does not decide), so it fires. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "7", 1u, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(result.invalidation.requiresBidiReanalysis, 1);

    /*
     * The layout is `x` `ب` `7` `Hello` — each insert landed at the caret, which follows what was typed. The
     * two deletes below are chosen so that the **deleted** scalar is the only thing that can move the flag:
     * the first removes the Arabic letter, the second a Latin one.
     */
    EditingExpectText(editor, "x" EditingArabic "7" EditingAscii, "editing.bidi");
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 8);

    /* Deleting the right-to-left scalar fires it too — the **removed** text is read as well. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "x7" EditingAscii, "editing.bidi");
    RoboCheckEqInt(result.invalidation.requiresBidiReanalysis, 1);

    /* …and a purely Latin delete does not, even though it still reshapes. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "x7ello", "editing.bidi");
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.deleted), 1);
    RoboCheckEqInt(result.invalidation.requiresBidiReanalysis, 0);
    RoboCheckEqInt(result.invalidation.requiresReshape, 1);

    /* A no-op asks for nothing at all: no reshape, no reanalysis, no fonts. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(0u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(&result.invalidation), 1);
    RoboCheckEqInt((int)result.invalidation.textRange.start, 0);
    RoboCheckEqInt((int)result.invalidation.textRange.end, 0);
    EditingExpectText(editor, "x7ello", "editing.bidi");

    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §32 — one editor for both writing modes                              */
/* ------------------------------------------------------------------ */

static void TestEditingVertical(void)
{
    ConfFontSpec spec;
    ConfVerticalGlyphIds ids;
    ConfFace face;
    RoboTextShapeOptions options;
    RoboTextShapeResult *shaped = NULL;
    RoboTextEditBoundaries *boundaries;
    RoboTextEditor *editor;
    RoboTextEditResult edit;

    memset(&spec, 0, sizeof(spec));
    memset(&ids, 0, sizeof(ids));
    ConfSpecVertical(&spec, &ids);
    ConfFaceOpen(&face, &spec, EditingSizePx);
    RoboTextEditResultInit(&edit);

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    options.writingMode = RoboTextWritingVerticalRightToLeft;

    /*
     * `あ` + U+3099: **two** scalars, two glyphs, one cluster. The marks do not add a unit, and the cluster —
     * not the glyph — is what the editor counts, which is the case a column makes visible.
     */
    shaped = ConfShape(&face, EditingKanaVoicing, &options);
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(shaped), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(RoboTextShapeResultGlyphRun(shaped, 0u)), 1);
    RoboCheckEqInt((int)RoboTextGlyphRunWritingMode(RoboTextShapeResultGlyphRun(shaped, 0u)),
                   (int)RoboTextWritingVerticalRightToLeft);
    boundaries = RoboTextEditBoundariesFromRuns(RoboTextShapeResultGlyphRunList(shaped));
    RoboCheck(boundaries != NULL);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(boundaries), 1);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterAt(boundaries, RoboTextPositionMake(0u)).start, 0);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterAt(boundaries, RoboTextPositionMake(0u)).end, 2);

    /* The same editor doors, over a column: one backspace takes the character, not half of it. */
    editor = EditingEditorOf(EditingKanaVoicing);
    RoboCheckEqInt((int)RoboTextEditorSetBoundaries(editor, boundaries), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorBoundary(editor), (int)RoboTextEditBoundaryCluster);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(&edit.operation), 6);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(edit.operation.deleted), 2);
    EditingExpectText(editor, "", "editing.vertical");
    /* The edit must also **reshapes** in the column's own mode: the text it left is shaped vertically. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, EditingLatinAB, 2u, &edit, NULL), (int)RoboOk);
    EditingExpectText(editor, EditingLatinAB, "editing.vertical");
    {
        RoboTextShapeResult *after = EditingShapeView(&face, ConfView(EditingLatinAB));
        RoboTextShapeOptions vertical;
        RoboTextShapeResult *asColumn;

        RoboTextShapeOptionsInit(&vertical);
        vertical.baseDirection = RoboTextDirectionLtr;
        vertical.writingMode = RoboTextWritingVerticalRightToLeft;
        asColumn = ConfShape(&face, EditingLatinAB, &vertical);
        /* Two letters, two clusters, in the column and in the row alike — the mode moves the pen, not the units. */
        ConfExpectGlyphCount(after, 2u, "editing.vertical", "two letters in a row");
        ConfExpectGlyphCount(asColumn, 2u, "editing.vertical", "two letters in a column");
        RoboCheckEqInt((int)RoboTextGlyphRunClusterCount(RoboTextShapeResultGlyphRun(asColumn, 0u)), 2);
        RoboTextShapeResultDestroy(after);
        RoboTextShapeResultDestroy(asColumn);
    }
    RoboTextEditorDestroy(editor);

    /* And a three-letter column edits one cluster at a time, in **source** order — the inline axis. */
    RoboTextShapeResultDestroy(shaped);
    shaped = ConfShape(&face, "ABC", &options);
    boundaries = RoboTextEditBoundariesFromRuns(RoboTextShapeResultGlyphRunList(shaped));
    RoboCheck(boundaries != NULL);
    RoboCheckEqInt((int)RoboTextEditBoundariesClusterCount(boundaries), 3);
    editor = EditingEditorOf("ABC");
    RoboCheckEqInt((int)RoboTextEditorSetBoundaries(editor, boundaries), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    EditingExpectText(editor, EditingLatinAB, "editing.vertical");
    /* And again: the resolver's next cluster back is the first letter's, so an edit can walk a column one
     * cluster at a time. Its clusters are the text the resolver was built over — installing a resolver over
     * *other* text is the one contract this layer cannot check, so no case here does it. */
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &edit, NULL), (int)RoboOk);
    EditingExpectText(editor, "A", "editing.vertical");
    RoboTextEditorDestroy(editor);

    RoboTextEditBoundariesDestroy(boundaries);
    RoboTextShapeResultDestroy(shaped);
    RoboTextEditResultDestroy(&edit);
    ConfFaceClose(&face);
}

/* ------------------------------------------------------------------ */
/* §19–23 — composition: a range in the source text, and a slice kept   */
/* ------------------------------------------------------------------ */

static void TestEditingComposition(void)
{
    RoboTextEditor *editor = EditingEditorOf("Hello world");
    RoboTextEditResult result;
    RoboTextComposition state;

    RoboTextEditResultInit(&result);

    /* Nothing running: the two doors are no-ops rather than errors. */
    RoboCheckEqInt((int)RoboTextEditorCommitComposition(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 0);
    RoboCheckEqInt((int)RoboTextEditorCancelComposition(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 0);
    EditingExpectText(editor, "Hello world", "editing.composition");

    /* A composition begins **over the selection**, so an IME replacing a word starts by replacing it. */
    RoboCheckEqInt((int)RoboTextEditorSetSelection(
                        editor, RoboTextSelectionMake(RoboTextPositionMake(6u), RoboTextPositionMake(11u))),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "ka", 2u, RoboTextRangeAt(0u), 1,
                                                        RoboTextRangeAt(0u), 1, &result, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextEditorIsComposing(editor), 1);
    /* The composing text **is** text — a reader can put a caret in it — and it is in the buffer. */
    EditingExpectText(editor, "Hello ka", "editing.composition");
    EditingExpectCaret(editor, 8u, "editing.composition");
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 1);
    state = RoboTextEditorComposition(editor);
    RoboCheckEqInt(state.active, 1);
    RoboCheckEqInt((int)state.range.start, 6);
    RoboCheckEqInt((int)state.range.end, 8);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(state.text), 2);
    /* What it replaced is kept, which is the whole of what cancelling needs. */
    RoboCheckEqInt((int)RoboTextBufferScalarCount(state.textBefore), 5);
    RoboCheckEqInt(state.hasSelection, 1);
    RoboCheckEqInt(state.hasClause, 1);
    RoboCheckEqInt((int)state.selectedRange.start, 0);

    /* An update replaces the composition's own range, so one update is one result and the range grows. */
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "kan", 3u, RoboTextRangeAt(1u), 1,
                                                        RoboTextRangeAt(0u), 1, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "Hello kan", "editing.composition");
    state = RoboTextEditorComposition(editor);
    RoboCheckEqInt((int)state.range.start, 6);
    RoboCheckEqInt((int)state.range.end, 9);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(state.text), 3);
    RoboCheckEqInt((int)state.selectedRange.start, 1);

    /* Cancelling puts back **the slice that was there**, not something reconstructed: "world" returns. */
    RoboCheckEqInt((int)RoboTextEditorCancelComposition(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditorIsComposing(editor), 0);
    EditingExpectText(editor, "Hello world", "editing.composition");
    /* …and the caret and selection go back with it, in logical coordinates. */
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).start, 6);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).end, 11);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 11);
    RoboCheckEqInt((int)result.operation.replacedRange.start, 6);
    RoboCheckEqInt((int)result.operation.replacedRange.end, 9);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.deleted), 3);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.inserted), 5);

    /* Cancelling a five-update composition restores exactly what the first one found. */
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "k", 1u, RoboTextRangeAt(0u), 0,
                                                        RoboTextRangeAt(0u), 0, &result, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "ka", 2u, RoboTextRangeAt(0u), 0,
                                                        RoboTextRangeAt(0u), 0, &result, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "kan", 3u, RoboTextRangeAt(0u), 0,
                                                        RoboTextRangeAt(0u), 0, &result, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorCancelComposition(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "Hello world", "editing.composition");
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).start, 6);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).end, 11);

    /* Committing leaves the text where it is and only asks for a **repaint**: what stopped being true is the
     * composition boundary, which a renderer drawing an underline has to redraw. */
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "kan", 3u, RoboTextRangeAt(0u), 0,
                                                        RoboTextRangeAt(0u), 0, &result, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorCommitComposition(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditorIsComposing(editor), 0);
    EditingExpectText(editor, "Hello kan", "editing.composition");
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 1);
    RoboCheckEqInt(result.invalidation.requiresRepaint, 1);
    RoboCheckEqInt(result.invalidation.requiresReshape, 0);
    RoboCheckEqInt((int)result.operation.replacedRange.start, 6);
    RoboCheckEqInt((int)result.operation.replacedRange.end, 11);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.deleted), 5);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.inserted), 3);
    /* Committing again is nothing at all. */
    RoboCheckEqInt((int)RoboTextEditorCommitComposition(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 0);

    /* A composition with no selection and no clause says so rather than carrying a stale one. */
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "x", 1u, RoboTextRangeAt(0u), 0,
                                                        RoboTextRangeAt(0u), 0, &result, NULL),
                   (int)RoboOk);
    state = RoboTextEditorComposition(editor);
    RoboCheckEqInt(state.hasSelection, 0);
    RoboCheckEqInt(state.hasClause, 0);
    RoboCheckEqInt((int)RoboTextEditorCancelComposition(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "Hello kan", "editing.composition");

    /* An empty composition at the caret is legal and inserts nothing. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(0u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "", 0u, RoboTextRangeAt(0u), 0,
                                                        RoboTextRangeAt(0u), 0, &result, NULL),
                   (int)RoboOk);
    RoboCheckEqInt(RoboTextEditorIsComposing(editor), 1);
    EditingExpectText(editor, "Hello kan", "editing.composition");
    RoboCheckEqInt((int)RoboTextEditorCancelComposition(editor, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "Hello kan", "editing.composition");

    /* The misuses. */
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(NULL, "x", 1u, RoboTextRangeAt(0u), 0,
                                                        RoboTextRangeAt(0u), 0, &result, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorCommitComposition(NULL, &result, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorCancelComposition(NULL, &result, NULL), (int)RoboInvalidArgument);

    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §25–27 — a transaction is one logical mutation                       */
/* ------------------------------------------------------------------ */

static void TestEditingTransaction(void)
{
    RoboTextEditor *editor = EditingEditorOf("Hello");
    RoboTextEditTransaction *transaction = NULL;
    RoboTextEditResult member;
    RoboTextEditResult published;

    RoboTextEditResultInit(&member);
    RoboTextEditResultInit(&published);

    RoboCheckEqInt((int)RoboTextEditTransactionCreate(NULL, &transaction, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditTransactionCreate(editor, NULL, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditTransactionCreate(editor, &transaction, NULL), (int)RoboOk);
    RoboCheck(transaction != NULL);
    RoboCheckEqInt(RoboTextEditTransactionIsOpen(transaction), 1);
    RoboCheckEqInt((int)RoboTextEditTransactionCount(transaction), 0);
    RoboCheck(RoboTextEditTransactionOperationAt(transaction, 0u) == NULL);

    /* The members apply as they are added, so every intermediate state is a real text. */
    RoboCheckEqInt((int)RoboTextEditTransactionInsert(transaction, "A", 1u, &member, NULL), (int)RoboOk);
    EditingExpectText(editor, "AHello", "editing.transaction");
    RoboCheckEqInt((int)RoboTextEditTransactionInsert(transaction, "B", 1u, &member, NULL), (int)RoboOk);
    EditingExpectText(editor, "ABHello", "editing.transaction");
    RoboCheckEqInt((int)RoboTextEditTransactionCount(transaction), 2);
    RoboCheck(RoboTextEditTransactionOperationAt(transaction, 0u) != NULL);
    RoboCheck(RoboTextEditTransactionOperationAt(transaction, 1u) != NULL);
    RoboCheck(RoboTextEditTransactionOperationAt(transaction, 2u) == NULL);
    RoboCheckEqInt((int)RoboTextEditTransactionOperationAt(transaction, 0u)->replacedRange.start, 0);
    RoboCheckEqInt(RoboTextEditOperationChanged(RoboTextEditTransactionOperationAt(transaction, 0u)), 1);

    /* Committing publishes **one** operation spanning the whole transaction. */
    RoboCheckEqInt((int)RoboTextEditTransactionCommit(transaction, &published, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditTransactionIsOpen(transaction), 0);
    RoboCheckEqInt(RoboTextEditResultChanged(&published), 1);
    RoboCheckEqInt((int)published.operation.replacedRange.start, 0);
    RoboCheckEqInt((int)published.operation.replacedRange.end, 0);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(published.operation.inserted), 2);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(published.operation.deleted), 0);
    RoboCheckEqInt(memcmp(RoboTextBufferBytes(published.operation.inserted), "AB", 2u), 0);
    /* It reads from both ends: before the transaction and after it. */
    RoboCheckEqInt((int)published.operation.caretBefore.position.offset, 0);
    RoboCheckEqInt((int)published.operation.caretAfter.position.offset, 2);
    /* And a closed transaction refuses more work by name. */
    RoboCheckEqInt((int)RoboTextEditTransactionInsert(transaction, "C", 1u, &member, NULL),
                   (int)RoboInvalidArgument);
    EditingExpectText(editor, "ABHello", "editing.transaction");
    RoboTextEditTransactionDestroy(transaction);
    transaction = NULL;
    RoboTextEditorDestroy(editor);

    /* Cancel puts the **whole span** back in one replace, and the selection and caret with it. */
    editor = EditingEditorOf("Hello");
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(5u), RoboTextAffinityUpstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionCreate(editor, &transaction, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionInsert(transaction, "!", 1u, &member, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionInsert(transaction, "!", 1u, &member, NULL), (int)RoboOk);
    EditingExpectText(editor, "Hello!!", "editing.transaction");
    RoboCheckEqInt((int)RoboTextEditTransactionCount(transaction), 2);
    RoboCheckEqInt((int)RoboTextEditTransactionCancel(transaction, NULL), (int)RoboOk);
    EditingExpectText(editor, "Hello", "editing.transaction");
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 5);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).affinity, (int)RoboTextAffinityUpstream);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).start, 5);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).end, 5);
    RoboCheckEqInt(RoboTextEditTransactionIsOpen(transaction), 0);
    /* A member's own record survives the whole of it, which is what a per-keystroke undo needs. */
    RoboCheck(RoboTextEditTransactionOperationAt(transaction, 0u) != NULL);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(RoboTextEditTransactionOperationAt(transaction, 0u)->inserted), 1);
    RoboTextEditTransactionDestroy(transaction);
    RoboTextEditorDestroy(editor);

    /* A transaction with no members is the identity, and one that is neither committed nor cancelled leaves
     * its edits in place — stated, because the alternative would be a silent undo. */
    editor = EditingEditorOf("Hi");
    RoboCheckEqInt((int)RoboTextEditTransactionCreate(editor, &transaction, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionCommit(transaction, &published, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&published), 0);
    RoboCheckEqInt(RoboTextEditTransactionIsOpen(transaction), 0);
    RoboCheckEqInt((int)RoboTextEditTransactionReplace(transaction, RoboTextRangeFull(2u), "x", 1u, &member, NULL),
                   (int)RoboInvalidArgument);
    RoboTextEditTransactionDestroy(transaction);
    transaction = NULL;
    RoboCheckEqInt((int)RoboTextEditTransactionCreate(editor, &transaction, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionReplace(transaction, RoboTextRangeMake(0u, 2u), "Yo", 2u, &member, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "Yo", "editing.transaction");
    RoboTextEditTransactionDestroy(transaction);
    EditingExpectText(editor, "Yo", "editing.transaction");

    /* A member that deletes reads the boundary the editor's own rule gives it. */
    RoboCheckEqInt((int)RoboTextEditTransactionCreate(editor, &transaction, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(2u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionDeleteBackward(transaction, &member, NULL), (int)RoboOk);
    EditingExpectText(editor, "Y", "editing.transaction");
    RoboCheckEqInt((int)RoboTextEditTransactionCommit(transaction, &published, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(published.operation.deleted), 1);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(published.operation.inserted), 0);
    RoboTextEditTransactionDestroy(transaction);

    /* And a `ReplaceSelection` member is the same door, so a paste of nothing is a delete. */
    RoboCheckEqInt((int)RoboTextEditorSetSelection(
                        editor, RoboTextSelectionMake(RoboTextPositionMake(0u), RoboTextPositionMake(1u))),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionCreate(editor, &transaction, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditTransactionReplaceSelection(transaction, "Z", 1u, &member, NULL), (int)RoboOk);
    EditingExpectText(editor, "Z", "editing.transaction");
    RoboCheckEqInt((int)RoboTextEditTransactionCancel(transaction, NULL), (int)RoboOk);
    EditingExpectText(editor, "Y", "editing.transaction");
    RoboTextEditTransactionDestroy(transaction);

    RoboTextEditResultDestroy(&member);
    RoboTextEditResultDestroy(&published);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §36–37 — transformation, in logical source coordinates              */
/* ------------------------------------------------------------------ */

static void TestEditingTransform(void)
{
    RoboTextEditor *editor = EditingEditorOf("ABCDEFG");
    RoboTextEditResult result;
    RoboTextSelection selection;

    RoboTextEditResultInit(&result);

    /* `ABCDEFG` → replace `[1,4)` with `xy` → `AxyEFG`: three scalars removed, two inserted. */
    RoboCheckEqInt((int)RoboTextEditorReplace(editor, RoboTextRangeMake(1u, 4u), "xy", 2u, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "AxyEFG", "editing.transform");
    RoboCheckEqInt((int)result.operation.replacedRange.start, 1);
    RoboCheckEqInt((int)result.operation.replacedRange.end, 4);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.inserted), 2);
    RoboCheckEqInt((int)RoboTextBufferScalarCount(result.operation.deleted), 3);

    /* Before the range: unchanged. Inside it, and at its start: the end of what was inserted. At its end:
     * the same place, because a boundary belongs to the range before it. After it: shifted by the delta. */
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(0u), &result.operation).offset, 0);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(1u), &result.operation).offset, 3);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(2u), &result.operation).offset, 3);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(4u), &result.operation).offset, 3);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(5u), &result.operation).offset, 4);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(7u), &result.operation).offset, 6);
    /* With no operation there is nothing to map through, so a position is its own answer. */
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(5u), NULL).offset, 5);

    /* The inverse: inside what was inserted maps back to where the edit began. */
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(0u), &result.operation).offset, 0);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(2u), &result.operation).offset, 1);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(3u), &result.operation).offset, 1);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(4u), &result.operation).offset, 5);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(6u), &result.operation).offset, 7);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(2u), NULL).offset, 2);
    /* Round trip outside the edit: a position before it and a position after it both come back unchanged. */
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(
                       RoboTextPositionTransformAfterEdit(RoboTextPositionMake(0u), &result.operation),
                       &result.operation)
                       .offset,
                   0);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(
                       RoboTextPositionTransformAfterEdit(RoboTextPositionMake(7u), &result.operation),
                       &result.operation)
                       .offset,
                   7);

    /* Ranges transform at both ends, and stay non-inverted because both rules are monotone. */
    {
        RoboTextRange after = RoboTextRangeTransformAfterEdit(RoboTextRangeMake(5u, 7u), &result.operation);

        RoboCheckEqInt((int)after.start, 4);
        RoboCheckEqInt((int)after.end, 6);
        after = RoboTextRangeTransformAfterEdit(RoboTextRangeAt(1u), &result.operation);
        RoboCheckEqInt((int)after.start, 3);
        RoboCheckEqInt((int)after.end, 3);
        /* The whole text maps to the whole text. */
        after = RoboTextRangeTransformAfterEdit(RoboTextRangeFull(7u), &result.operation);
        RoboCheckEqInt((int)after.start, 0);
        RoboCheckEqInt((int)after.end, 6);
        /* With no operation, unchanged. */
        after = RoboTextRangeTransformAfterEdit(RoboTextRangeMake(2u, 5u), NULL);
        RoboCheckEqInt((int)after.start, 2);
        RoboCheckEqInt((int)after.end, 5);
    }

    /* A caret that was at the end of the replaced range follows the text it was after, so a selection that
     * ended where an edit began keeps selecting the same characters (12.22.35). */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(6u), RoboTextAffinityDownstream), (int)RoboOk);
    selection = RoboTextEditorSelection(editor);
    RoboCheckEqInt((int)selection.focus.offset, 6);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextEditorCaret(editor).position, &result.operation).offset, 5);

    /* A pure insertion maps the seam onto itself and shifts everything after it. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(0u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "#", 1u, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(0u), &result.operation).offset, 1);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(1u), &result.operation).offset, 2);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(1u), &result.operation).offset, 0);
    RoboCheckEqInt((int)RoboTextPositionTransformBeforeEdit(RoboTextPositionMake(2u), &result.operation).offset, 1);

    /* And a pure deletion maps everything inside it onto its start. */
    RoboCheckEqInt((int)RoboTextEditorReplace(editor, RoboTextRangeMake(1u, 4u), NULL, 0u, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "#EFG", "editing.transform");
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(1u), &result.operation).offset, 1);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(3u), &result.operation).offset, 1);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(4u), &result.operation).offset, 1);
    RoboCheckEqInt((int)RoboTextPositionTransformAfterEdit(RoboTextPositionMake(7u), &result.operation).offset, 4);

    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §28–33, 43 — what a consumer of shaped text must redo                */
/* ------------------------------------------------------------------ */

static void TestEditingInvalidation(void)
{
    RoboTextEditor *editor = EditingEditorOf("one\ntwo");
    RoboTextEditResult result;
    RoboTextEditInvalidation invalidation;
    char block[512];
    size_t need = 0u;
    size_t small = 0u;

    RoboTextEditResultInit(&result);
    RoboCheckEqInt((int)RoboTextEditorScalarCount(editor), 7);

    /* An edit inside the first paragraph widens to **that paragraph** and stops at the terminator: the
     * analysis, the shaping, the layout and the paint all take the same range, because the unit Unicode
     * analysis, script resolution and the bidi algorithm are at least is a paragraph. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(3u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, "X", 1u, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "oneX\ntwo", "editing.invalidation");
    invalidation = result.invalidation;
    RoboCheckEqInt(invalidation.changed, 1);
    RoboCheckEqInt((int)invalidation.textRange.start, 3);
    RoboCheckEqInt((int)invalidation.textRange.end, 4);
    RoboCheckEqInt((int)invalidation.analysisRange.start, 0);
    RoboCheckEqInt((int)invalidation.analysisRange.end, 4);
    RoboCheckEqInt((int)invalidation.shapingRange.start, (int)invalidation.analysisRange.start);
    RoboCheckEqInt((int)invalidation.shapingRange.end, (int)invalidation.analysisRange.end);
    RoboCheckEqInt((int)invalidation.layoutRange.end, (int)invalidation.analysisRange.end);
    RoboCheckEqInt((int)invalidation.paintRange.end, (int)invalidation.analysisRange.end);
    /*
     * Conservative is allowed; **insufficient** is not. The range is half-open and its end is the seam's
     * position (the paragraph's terminator), so the inserted scalar is inside it and the end covers where the
     * edit stopped — the two things a consumer indexes with.
     */
    RoboCheckEqInt(RoboTextRangeContains(invalidation.shapingRange, 3u), 1);
    RoboCheckEqInt(RoboTextRangeContainsRange(invalidation.shapingRange, invalidation.textRange), 1);
    RoboCheckEqInt((int)invalidation.shapingRange.end >= (int)invalidation.textRange.end, 1);
    /* …and it is a paragraph rather than the document, which is the honest smaller answer. */
    RoboCheckMsg(invalidation.analysisRange.end < 8u, "the invalidation claimed the whole document");
    RoboCheckEqInt(invalidation.requiresReshape, 1);
    RoboCheckEqInt(invalidation.requiresReanalysis, 1);
    RoboCheckEqInt(invalidation.requiresFontReselection, 1);
    RoboCheckEqInt(invalidation.requiresRelayout, 1);
    RoboCheckEqInt(invalidation.requiresRepaint, 1);
    RoboCheckEqInt(invalidation.requiresSelectionRemap, 1);
    /* Latin only, so the level runs cannot have moved. */
    RoboCheckEqInt(invalidation.requiresBidiReanalysis, 0);

    /* Editing the **second** paragraph widens to it, not back over the first. */
    RoboCheckEqInt((int)RoboTextEditorReplace(editor, RoboTextRangeAt(8u), "!", 1u, &result, NULL), (int)RoboOk);
    EditingExpectText(editor, "oneX\ntwo!", "editing.invalidation");
    RoboCheckEqInt((int)result.invalidation.analysisRange.start, 5);
    RoboCheckEqInt((int)result.invalidation.analysisRange.end, 9);
    RoboCheckEqInt((int)result.invalidation.textRange.start, 8);
    RoboCheckEqInt((int)result.invalidation.textRange.end, 9);

    /* A no-op's record is the empty one, and it is the same record whether the edit changed anything. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(0u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorDeleteBackward(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(&result.invalidation), 1);
    RoboCheckEqInt(result.invalidation.changed, 0);
    RoboCheckEqInt((int)result.invalidation.shapingRange.start, 0);
    RoboCheckEqInt((int)result.invalidation.shapingRange.end, 0);
    RoboCheckEqInt((int)result.invalidation.layoutRange.end, 0);

    /* A record from before can be unioned by hand, which is what a transaction does with its members. */
    RoboTextEditInvalidationInit(&invalidation);
    invalidation.changed = 1;
    invalidation.shapingRange = RoboTextRangeMake(4u, 6u);
    RoboCheckEqInt(RoboTextEditInvalidationIsEmpty(&invalidation), 0);

    /* §43 — the record as a block, with every range and every flag named. */
    RoboCheckEqInt((int)RoboTextEditorInsert(editor, EditingArabic, 2u, &result, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditInvalidationWrite(&result.invalidation, block, sizeof(block), &need), (int)RoboOk);
    RoboCheck(need > 0u);
    RoboCheckMsg(strstr(block, "changed=1") != NULL, "the block does not name the change");
    RoboCheckMsg(strstr(block, "reshape=1") != NULL, "the block does not name the reshaping");
    RoboCheckMsg(strstr(block, "bidi=1") != NULL, "the block does not name the bidi reanalysis");
    RoboCheckMsg(strstr(block, "fonts=1") != NULL, "the block does not name the font reselection");
    RoboCheckMsg(strstr(block, "selection=1") != NULL, "the block does not name the selection remap");
    RoboCheckMsg(strstr(block, " text ") != NULL, "the block does not name the text range");
    RoboCheckMsg(strstr(block, " shaping ") != NULL, "the block does not name the shaping range");
    RoboCheckMsg(strstr(block, " layout ") != NULL, "the block does not name the layout range");
    /* Complete or absent: a buffer that cannot hold it is refused and reports the length it needs. */
    block[0] = 'x';
    RoboCheckEqInt((int)RoboTextEditInvalidationWrite(&result.invalidation, block, 4u, &small), (int)RoboNotSupported);
    RoboCheckEqInt((int)small, (int)need);
    RoboCheckEqInt((int)block[0], 0);
    RoboCheckEqInt((int)RoboTextEditInvalidationWrite(&result.invalidation, block, sizeof(block), &small), (int)RoboOk);
    RoboCheckEqInt((int)small, (int)need);
    RoboCheckEqInt((int)RoboTextEditInvalidationWrite(NULL, block, sizeof(block), &need), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditInvalidationWrite(&result.invalidation, NULL, sizeof(block), &need),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditInvalidationWrite(&result.invalidation, block, sizeof(block), NULL),
                   (int)RoboInvalidArgument);

    RoboTextEditResultDestroy(&result);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §6, 27, 43 — the operation, the result and the composition as lines   */
/* ------------------------------------------------------------------ */

static void TestEditingDiagnostics(void)
{
    RoboTextEditor *editor = EditingEditorOf("Hello world");
    RoboTextEditResult result;
    char block[1024];
    char narrow[8];
    size_t need = 0u;
    size_t narrowNeed = 0u;

    RoboTextEditResultInit(&result);
    block[0] = '\0';

    /* The editor's own block: the counts, the selection, the caret, the boundary, the edit counters. */
    RoboCheckEqInt((int)RoboTextEditorWrite(editor, block, sizeof(block), &need), (int)RoboOk);
    RoboCheckMsg(strstr(block, "editor ") != NULL, "the block does not name the editor");
    RoboCheckMsg(strstr(block, " scalars ") != NULL, "the block does not name the scalar count");
    RoboCheckMsg(strstr(block, " selection ") != NULL, "the block does not name the selection");
    RoboCheckMsg(strstr(block, " caret ") != NULL, "the block does not name the caret");
    RoboCheckMsg(strstr(block, " boundary character") != NULL, "the block does not name the boundary");
    RoboCheckMsg(strstr(block, " composing 0") != NULL, "the block does not name the composition state");
    RoboCheckMsg(strstr(block, " edits 0 noops 0") != NULL, "the block does not name the counters");
    /* The same length is reported whether the buffer fits or not, and a narrow one is left empty. */
    narrow[0] = 'x';
    RoboCheckEqInt((int)RoboTextEditorWrite(editor, narrow, sizeof(narrow), &narrowNeed), (int)RoboNotSupported);
    RoboCheckEqInt((int)narrowNeed, (int)need);
    RoboCheckEqInt((int)narrow[0], 0);
    RoboCheckEqInt((int)RoboTextEditorWrite(NULL, block, sizeof(block), &need), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorWrite(editor, NULL, sizeof(block), &need), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditorWrite(editor, block, sizeof(block), NULL), (int)RoboInvalidArgument);

    /* A mutation, and the operation it left behind. The counters move with it. */
    RoboCheckEqInt((int)RoboTextEditorReplace(editor, RoboTextRangeMake(6u, 11u), "RoboText", 8u, &result, NULL),
                   (int)RoboOk);
    EditingExpectText(editor, "Hello RoboText", "editing.diagnostics");
    RoboCheckEqInt((int)RoboTextEditOperationWrite(&result.operation, block, sizeof(block), &need), (int)RoboOk);
    RoboCheckMsg(strstr(block, "replace 6..11") != NULL, "the operation does not name the range");
    RoboCheckMsg(strstr(block, " deleted 5B") != NULL, "the operation does not name the deleted bytes");
    RoboCheckMsg(strstr(block, " inserted 8B") != NULL, "the operation does not name the inserted bytes");
    RoboCheckMsg(strstr(block, " caret 0->14") != NULL, "the operation does not name the caret");
    RoboCheckMsg(strstr(block, "downstream->downstream") != NULL, "the operation does not name the affinity");
    RoboCheckEqInt((int)RoboTextEditOperationWrite(NULL, block, sizeof(block), &need), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditOperationWrite(&result.operation, block, sizeof(block), NULL),
                   (int)RoboInvalidArgument);

    /* The last operation is kept by the editor, so a caller can read it after the result is gone. */
    RoboCheck(RoboTextEditorLastOperation(editor) != NULL);
    RoboCheckEqInt((int)RoboTextEditorLastOperation(editor)->replacedRange.start, 6);
    RoboCheckEqInt(RoboTextEditOperationChanged(RoboTextEditorLastOperation(editor)), 1);
    RoboCheckEqInt((int)RoboTextEditOperationInsertedBytes(RoboTextEditorLastOperation(editor)), 8);
    RoboCheckEqInt((int)RoboTextEditOperationDeletedBytes(RoboTextEditorLastOperation(editor)), 5);

    /* The editor's block now reports the mutation. */
    RoboCheckEqInt((int)RoboTextEditorWrite(editor, block, sizeof(block), &need), (int)RoboOk);
    RoboCheckMsg(strstr(block, "caret 14") != NULL, "the block does not read the caret back");
    RoboCheckMsg(strstr(block, "edits 1 noops 0") != NULL, "the block does not count the edit");

    /* A composition that is not running is the word "none" rather than an empty line. */
    {
        RoboTextComposition state = RoboTextEditorComposition(editor);

        RoboCheckEqInt((int)RoboTextCompositionWrite(&state, block, sizeof(block), &need), (int)RoboOk);
        RoboCheckEqInt((int)need, 4);
        RoboCheckStrEq(block, "none");
        RoboCheckEqInt((int)RoboTextCompositionWrite(NULL, block, sizeof(block), &need), (int)RoboInvalidArgument);
    }
    /* And while it runs, the range, the composing scalars and the slice that was replaced. */
    RoboCheckEqInt((int)RoboTextEditorSetCaret(editor, RoboTextPositionMake(5u), RoboTextAffinityDownstream), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditorUpdateComposition(editor, "ka", 2u, RoboTextRangeAt(0u), 1,
                                                        RoboTextRangeAt(0u), 1, &result, NULL),
                   (int)RoboOk);
    {
        RoboTextComposition state = RoboTextEditorComposition(editor);

        RoboCheckEqInt((int)RoboTextCompositionWrite(&state, block, sizeof(block), &need), (int)RoboOk);
        RoboCheckMsg(strstr(block, "composition 5..7") != NULL, "the block does not name the range");
        RoboCheckMsg(strstr(block, " text 2 scalars before 0") != NULL,
                     "the block does not name the composing text");
        RoboCheckMsg(strstr(block, "selected=1 clause=1") != NULL, "the block does not name the flags");
    }
    /* The editor's block names the boundary a delete would use, and that it is composing. */
    RoboCheckEqInt((int)RoboTextEditorWrite(editor, block, sizeof(block), &need), (int)RoboOk);
    RoboCheckMsg(strstr(block, "composing 1") != NULL, "the block does not name the composition state");
    /* A result that did not change is not a change, read through the door rather than the field. */
    RoboCheckEqInt((int)RoboTextEditorCancelComposition(editor, &result, NULL), (int)RoboOk);
    RoboCheckEqInt(RoboTextEditResultChanged(&result), 1);
    RoboCheckEqInt(RoboTextEditResultChanged(NULL), 0);
    RoboCheckEqInt((int)RoboTextEditBoundaryName(RoboTextEditBoundaryCluster)[0], 'c');

    RoboTextEditResultDestroy(&result);
    RoboTextEditResultDestroy(NULL);
    RoboTextEditOperationInit(NULL);
    RoboTextEditOperationDestroy(NULL);
    RoboTextEditInvalidationInit(NULL);
    RoboTextCaretInit(NULL);
    RoboTextCompositionInit(NULL);
    RoboTextCompositionRelease(NULL);
    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* §53 — arbitrary bytes, one invariant                                 */
/* ------------------------------------------------------------------ */

static void TestEditingFuzz(void)
{
    static const uint8_t junk[] = {0x41u, 0xC3u, 0xA9u, 0xE4u, 0xB8u, 0x80u, 0xF0u, 0x9Fu, 0x98u, 0x80u,
                                   0x80u, 0xFFu, 0x09u, 0x0Au, 0x0Du, 0x7Fu, 0x00u, 0xE0u, 0xA4u, 0x95u,
                                   0xD8u, 0xA8u, 0x20u, 0xE2u, 0x80u, 0x8Cu, 0xCCu, 0x81u, 0xCBu, 0x8Bu,
                                   0x41u, 0x42u, 0x43u, 0x44u, 0x45u};
    uint8_t pattern[32];
    size_t index;

    /* The contract, stated where a reader looks for it: NULL with a size is refused, and so are two NULLs. */
    RoboCheckEqInt((int)RoboTextEditingFuzz(NULL, 8u), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextEditingFuzz(junk, sizeof(junk)), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextEditingFuzz(NULL, 0u), (int)RoboOk);
    /* Every prefix, so a byte count that lands mid-scalar and a truncated command stream are both exercised. */
    for (index = 0u; index <= sizeof(junk); index++) {
        RoboCheckEqInt((int)RoboTextEditingFuzz(junk, index), (int)RoboOk);
    }
    /* And a deterministic sweep of configurations, because the first bytes steer which door runs. */
    for (index = 0u; index < sizeof(pattern); index++) {
        pattern[index] = (uint8_t)(index * 5u + 11u);
    }
    for (index = 0u; index < 8u; index++) {
        RoboCheckEqInt((int)RoboTextEditingFuzz(pattern + index, sizeof(pattern) - index), (int)RoboOk);
    }
    /* A long input is bounded by the entry itself rather than by the caller's patience. */
    for (index = 0u; index < sizeof(pattern); index++) {
        pattern[index] = (uint8_t)(index ^ 0x5Au);
    }
    RoboCheckEqInt((int)RoboTextEditingFuzz(pattern, sizeof(pattern)), (int)RoboOk);
}

void TestRegisterTextEditing(void)
{
    TestRegister("text.editing.vocabulary", TestEditingVocabulary);
    TestRegister("text.editing.basic", TestEditingBasic);
    TestRegister("text.editing.commands", TestEditingCommands);
    TestRegister("text.editing.utf8", TestEditingUtf8);
    TestRegister("text.editing.marks", TestEditingMarks);
    TestRegister("text.editing.ligature", TestEditingLigature);
    TestRegister("text.editing.arabic", TestEditingArabic);
    TestRegister("text.editing.indic", TestEditingIndic);
    TestRegister("text.editing.bidi", TestEditingBidi);
    TestRegister("text.editing.vertical", TestEditingVertical);
    TestRegister("text.editing.composition", TestEditingComposition);
    TestRegister("text.editing.transaction", TestEditingTransaction);
    TestRegister("text.editing.transform", TestEditingTransform);
    TestRegister("text.editing.invalidation", TestEditingInvalidation);
    TestRegister("text.editing.diagnostics", TestEditingDiagnostics);
    TestRegister("text.editing.fuzz", TestEditingFuzz);
}
