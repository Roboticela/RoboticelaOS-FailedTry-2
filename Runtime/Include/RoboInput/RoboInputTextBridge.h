/*
 * The bridge from a RoboInput command to RoboText's editor (Phase 13.12) — the one place the two vocabularies
 * meet, and the only file in this module that includes RoboText at all.
 *
 * ## Why the bridge is here and not there
 *
 * The command vocabulary is RoboInput's (`RoboInputCommand.h`), because a menu item, an accessibility action, a
 * gamepad button and a scripted test all have to be able to say *copy* without knowing what an editor is. The
 * editing semantics are RoboText's (12.22), because one mutation expressed as a replacement of a source range is
 * what makes the whole text subsystem coherent. Something has to translate, and this file is it: a **table**
 * and one call, so the translation is data rather than a second implementation of editing.
 *
 * RoboInput may include RoboText; RoboText may not include RoboInput. The direction is the phase brief's own
 * pipeline (`Keyboard → RoboInput → TextEditCommand → RoboText TextEditor`), and it is what stops the editor
 * from having an opinion about keys.
 *
 * ## What the bridge does *not* decide
 *
 * - **Where a copied selection goes.** `InputCommandApplyToEditor` hands the removed or copied text back
 *   through `outText` when the caller wants it, and the clipboard (13.40/13.41) is what keeps it. A bridge that
 *   owned a clipboard would be a bridge that owns state.
 * - **What a command means when nothing is focused.** A command that reaches no target is `RoboNotFound` from
 *   the *router*, not a silent no-op here.
 * - **Editing semantics.** Every door this file calls is 12.22's; nothing re-implements a boundary, an
 *   invalidation or a selection rule.
 *
 * ## The two commands the editor's own vocabulary cannot express
 *
 * `MOVE_DOCUMENT_START` and `MOVE_DOCUMENT_END` are in the brief's command list but not in
 * `RoboTextEditCommand`, and they do not need to be: they are a **caret placement**, and the editor already has
 * a public door for placing a caret at any position. So the bridge expresses them with
 * `RoboTextEditorSetCaret` and `RoboTextEditorScalarCount` rather than asking 12.22 to grow two enum values for
 * two positions it can already reach. The four line/vertical movements are the opposite case and are *not*
 * faked: they need a layout, RoboText refuses them by name, and the bridge passes that refusal through so a
 * caller learns the truth instead of getting an approximation.
 */
#ifndef ROBO_INPUT_TEXT_BRIDGE_H
#define ROBO_INPUT_TEXT_BRIDGE_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputCommand.h"
#include "RoboInput/RoboInputDiagnostics.h"

#include "RoboText/RoboText.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The editor command a command id is, or `RoboTextEditCommandNone` when it is not one the editor names.
 *
 * `MOVE_DOCUMENT_START`/`MOVE_DOCUMENT_END` answer `NONE` *by design*: they are real commands this bridge can
 * execute, expressed through the editor's caret doors rather than through its command vocabulary. Ask
 * `InputCommandIdIsEditorCommand` when the question is "can this be executed?" rather than "which enum value
 * is it?".
 */
RoboTextEditCommand InputCommandToTextEdit(InputCommandId id);

/** Non-zero when `InputCommandApplyToEditor` can execute @p id (which includes the two document movements). */
int InputCommandIdIsEditorCommand(InputCommandId id);

/**
 * Execute @p command against @p editor.
 *
 * @param outText receives the text a `COPY` or a `CUT` read, when the caller passes somewhere to put it; a
 *                `NULL` @p outText makes `COPY` a no-op that reports **changed 0**, which is the honest answer
 *                to "copy into nothing". The buffer is the caller's to destroy
 *                (`RoboTextBufferDestroy`/whatever 12.22 names).
 * @param out     receives the operation 12.22 published, for a caller that wants the invalidation record.
 *
 * @return `RoboOk` when the editor executed it (including a legitimate no-op such as a backspace at position 0);
 *         `RoboNotSupported` when @p command's id belongs to another family (focus, view, window), with the
 *         diagnostic naming the family — that is *not* an error, it is how a router keeps walking; the editor's
 *         own refusal otherwise, passed through unchanged so a caller sees 12.22's answer and not a second
 *         opinion about it.
 */
Result InputCommandApplyToEditor(const InputCommand *command,
                                RoboTextEditor *editor,
                                RoboTextBuffer **outText,
                                RoboTextEditResult *out,
                                InputDiag *diag);

/**
 * An editor as a **command target**, so it can be pushed onto a router's chain.
 *
 * The state is small and owned by the caller (held by value, like everything else in this module): the borrowed
 * editor, the last operation and the count of commands it executed. `InputEditorTargetAsCommandTarget` fills the
 * two-field target the router wants, with this record as its `userdata` — which is why a text field plugs into
 * the same chain as a focus navigator and a scroller without any of them knowing about the others.
 */
typedef struct InputEditorTarget {
    /** The editor to drive. **Borrowed**: the target never creates, destroys or outlives it. */
    RoboTextEditor *editor;
    /** The operation the last executed command published. */
    RoboTextEditResult last;
    /** Non-zero once @ref last holds a real operation. */
    int hasLast;
    /** Commands this target executed. Diagnostics, and the number a test asserts. */
    uint64_t appliedTotal;
    /** Commands another family owned, which the target declined. Diagnostics. */
    uint64_t declinedTotal;
} InputEditorTarget;

/** Point @p target at @p editor, clearing its counters and its last operation. Refused for a NULL editor. */
Result InputEditorTargetInit(InputEditorTarget *target, RoboTextEditor *editor, InputDiag *diag);

/** Destroy what a target's last operation owns. Safe on a target whose last operation is empty, and on NULL. */
void InputEditorTargetRelease(InputEditorTarget *target);

/** Fill @p out with a command target backed by @p target. Refused for NULL or an unpointed target. */
Result InputEditorTargetAsCommandTarget(InputEditorTarget *target,
                                       InputCommandTarget *out,
                                       InputDiag *diag);

/**
 * The target function `InputEditorTargetAsCommandTarget` installs.
 *
 * Public because a caller assembling a chain by hand may want it, and because a test asserting *why* a command
 * was declined should be able to call it directly. It declines with `RoboNotSupported` for a command that is not
 * the editor's, and reports the editor's own answer otherwise.
 */
Result InputEditorTargetExecute(void *userdata, const InputCommand *command, InputDiag *diag);

/**
 * Room `InputEditorTargetWrite` needs, NUL included: a summary line and the last operation's two ranges left
 * unprinted, because 12.22 already has a writer for those.
 */
#define RoboInputEditorTargetDumpMax 192u

/**
 * Write @p target as one line:
 *
 *     editor applied=3 declined=1 has_last=1 last_changed=1
 */
Result InputEditorTargetWrite(const InputEditorTarget *target,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_TEXT_BRIDGE_H */
