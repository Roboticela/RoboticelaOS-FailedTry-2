/*
 * The RoboInput-command to RoboText-editor bridge (Phase 13.12) — see `RoboInputTextBridge.h`.
 *
 * Three pieces:
 *
 *   **the table**   one row per editor command, and the mapping written once rather than as a switch that a
 *                   reader has to hold in their head beside 12.22's own enum;
 *   **the call**    `RoboTextEditorCommand` for everything the editor names, and the editor's **caret door**
 *                   for the two document movements it does not — because placing a caret at the start of a text
 *                   is a position, not an editing operation the editor is missing;
 *   **the target**  the same bridge wired as an `InputCommandTarget`, so a text field joins a router's chain.
 *
 * What this file deliberately does not do is decide anything: the text an insert carries comes from the command,
 * the removed text of a cut goes *out* through the caller's sink, and every refusal is 12.22's own answer passed
 * through unchanged. A bridge that softened a refusal would be a second opinion about editing.
 */

#include "RoboInput/RoboInputTextBridge.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* The table                                                           */
/* ------------------------------------------------------------------ */

/** One row: a RoboInput command and the editor command it means, or `NONE` for "by other doors". */
typedef struct TextBridgeRow {
    InputCommandId command;
    RoboTextEditCommand edit;
    /** Non-zero when the editor names it, so `NONE` in @ref edit is not a gap. */
    int named;
} TextBridgeRow;

static const TextBridgeRow textBridgeRows[] = {
    { RoboInputCommandInsertText, RoboTextEditCommandInsertText, 1 },
    { RoboInputCommandDeleteBackward, RoboTextEditCommandDeleteBackward, 1 },
    { RoboInputCommandDeleteForward, RoboTextEditCommandDeleteForward, 1 },
    { RoboInputCommandMoveLeft, RoboTextEditCommandMoveLeft, 1 },
    { RoboInputCommandMoveRight, RoboTextEditCommandMoveRight, 1 },
    /* The vertical and line movements are named here and **refused by the editor**, which needs a layout
     * (12.22). Routing them means a caller gets that refusal rather than a horizontal approximation of a
     * vertical move. */
    { RoboInputCommandMoveUp, RoboTextEditCommandMoveUp, 1 },
    { RoboInputCommandMoveDown, RoboTextEditCommandMoveDown, 1 },
    { RoboInputCommandMoveLineStart, RoboTextEditCommandMoveToLineStart, 1 },
    { RoboInputCommandMoveLineEnd, RoboTextEditCommandMoveToLineEnd, 1 },
    /* Expressed through the caret doors below, so the editor's vocabulary did not have to grow two names for two
     * positions it can already reach. */
    { RoboInputCommandMoveDocumentStart, RoboTextEditCommandNone, 0 },
    { RoboInputCommandMoveDocumentEnd, RoboTextEditCommandNone, 0 },
    { RoboInputCommandSelectLeft, RoboTextEditCommandSelectLeft, 1 },
    { RoboInputCommandSelectRight, RoboTextEditCommandSelectRight, 1 },
    { RoboInputCommandSelectAll, RoboTextEditCommandSelectAll, 1 },
    { RoboInputCommandCopy, RoboTextEditCommandCopy, 1 },
    { RoboInputCommandCut, RoboTextEditCommandCut, 1 },
    { RoboInputCommandPaste, RoboTextEditCommandPaste, 1 },
    { RoboInputCommandUndo, RoboTextEditCommandUndo, 1 },
    { RoboInputCommandRedo, RoboTextEditCommandRedo, 1 }
};

static const TextBridgeRow *TextBridgeRowFor(InputCommandId id)
{
    size_t index;
    size_t count = sizeof(textBridgeRows) / sizeof(textBridgeRows[0]);

    for (index = 0u; index < count; index++) {
        if (textBridgeRows[index].command == id) {
            return &textBridgeRows[index];
        }
    }
    return NULL;
}

RoboTextEditCommand InputCommandToTextEdit(InputCommandId id)
{
    const TextBridgeRow *row = TextBridgeRowFor(id);

    return row ? row->edit : RoboTextEditCommandNone;
}

int InputCommandIdIsEditorCommand(InputCommandId id)
{
    return TextBridgeRowFor(id) != NULL;
}

/* ------------------------------------------------------------------ */
/* The call                                                            */
/* ------------------------------------------------------------------ */

Result InputCommandApplyToEditor(const InputCommand *command,
                                RoboTextEditor *editor,
                                RoboTextBuffer **outText,
                                RoboTextEditResult *out,
                                InputDiag *diag)
{
    const TextBridgeRow *row;
    const char *text = NULL;
    size_t length = 0u;

    if (!command || !editor) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "a command and an editor are both needed");
    }
    if (!InputCommandIsValid(command)) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "the command is not one a target may be asked to execute");
    }

    row = TextBridgeRowFor(command->id);
    if (!row) {
        /* Not the editor's: **declined**, which is how a router keeps walking outwards to whatever does own
         * the family. A refusal here would stop the walk and turn a focus command into a failed one. */
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboNotSupported,
                             "that command belongs to another layer; the editor declines it");
    }

    /* The two document movements: a position, not an operation. `SetCaret` collapses the selection on it, which
     * is what "move to the start" means -- a *select* to the start is a different command and 12.22's selection
     * doors are its own. */
    if (!row->named) {
        RoboTextScalarIndex offset = (command->id == RoboInputCommandMoveDocumentStart)
                                         ? (RoboTextScalarIndex)0u
                                         : RoboTextEditorScalarCount(editor);

        if (RoboTextEditorSetCaret(editor, RoboTextPositionMake(offset), RoboTextAffinityDownstream) !=
            RoboOk) {
            return RoboInputDiag(diag, RoboInputDomainCommand, RoboError,
                                 "the editor refused a caret placement at the document's edge");
        }
        if (out) {
            /* There is no operation to publish: a caret move changes no text, and inventing an
             * "unchanged operation" would make a caller think an edit happened. `changed` 0 is the whole
             * truth. */
            RoboTextEditResultInit(out);
        }
        return RoboOk;
    }

    /* Only an insert and a paste carry text, and 13.13's record already refuses text on any other id, so this is
     * a read rather than a validation. */
    if (command->id == RoboInputCommandInsertText || command->id == RoboInputCommandPaste ||
        command->id == RoboInputCommandCut) {
        text = command->text;
        length = RiBoundedStrlen(command->text, RoboInputCommandTextMax);
    }

    return RoboTextEditorCommand(editor, row->edit, text, length, outText, out, NULL);
}

/* ------------------------------------------------------------------ */
/* The target                                                          */
/* ------------------------------------------------------------------ */

Result InputEditorTargetInit(InputEditorTarget *target, RoboTextEditor *editor, InputDiag *diag)
{
    if (!target || !editor) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "a target and an editor are both needed");
    }
    target->editor = editor;
    /* Cleared rather than released: a caller pointing an existing target at a new editor owns whatever the old
     * one's last operation was. `InputEditorTargetRelease` is the door for that. */
    RoboTextEditResultInit(&target->last);
    target->hasLast = 0;
    target->appliedTotal = 0u;
    target->declinedTotal = 0u;
    return RoboOk;
}

void InputEditorTargetRelease(InputEditorTarget *target)
{
    if (!target) {
        return;
    }
    RoboTextEditResultDestroy(&target->last);
    RoboTextEditResultInit(&target->last);
    target->hasLast = 0;
}

Result InputEditorTargetExecute(void *userdata, const InputCommand *command, InputDiag *diag)
{
    InputEditorTarget *target = (InputEditorTarget *)userdata;
    RoboTextEditResult result;
    Result status;

    if (!target || !target->editor) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "the editor target has no editor");
    }
    RoboTextEditResultInit(&result);
    status = InputCommandApplyToEditor(command, target->editor, NULL, &result, diag);
    if (status == RoboNotSupported) {
        target->declinedTotal++;
        RoboTextEditResultDestroy(&result);
        return RoboNotSupported;
    }
    if (status != RoboOk) {
        RoboTextEditResultDestroy(&result);
        return status;
    }

    /* The last operation is **replaced**, so a caller reading it after a command sees that command's work and
     * not a mixture with an earlier one. */
    RoboTextEditResultDestroy(&target->last);
    target->last = result;
    target->hasLast = 1;
    target->appliedTotal++;
    return RoboOk;
}

Result InputEditorTargetAsCommandTarget(InputEditorTarget *target,
                                       InputCommandTarget *out,
                                       InputDiag *diag)
{
    if (!target || !out) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "a target record and somewhere to put the command target are both needed");
    }
    if (!target->editor) {
        return RoboInputDiag(diag, RoboInputDomainCommand, RoboInvalidArgument,
                             "the editor target has no editor");
    }
    InputCommandTargetInit(out);
    out->userdata = target;
    out->execute = InputEditorTargetExecute;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

Result InputEditorTargetWrite(const InputEditorTarget *target,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             InputDiag *diag)
{
    InputWriter text;

    if (!RiWriterArgumentsAreValid(buffer, capacity, outLength)) {
        return RoboInputDiag(diag, RoboInputDomainDump, RoboInvalidArgument,
                             "a dump needs a buffer, a capacity and somewhere to put the length");
    }
    RiWriterInit(&text, buffer, capacity);
    if (!target) {
        RiWriterText(&text, "editor <none>\n");
        return RiWriterFinish(&text, outLength, diag);
    }

    RiWriterText(&text, "editor");
    RiWriterFieldU64(&text, " applied", target->appliedTotal);
    RiWriterFieldU64(&text, " declined", target->declinedTotal);
    RiWriterFieldU64(&text, " has_last", target->hasLast ? 1u : 0u);
    RiWriterFieldU64(&text, " last_changed",
                     target->hasLast && RoboTextEditResultChanged(&target->last) ? 1u : 0u);
    RiWriterFieldU64(&text, " scalars", (uint64_t)RoboTextEditorScalarCount(target->editor));
    RiWriterChar(&text, '\n');
    return RiWriterFinish(&text, outLength, diag);
}
