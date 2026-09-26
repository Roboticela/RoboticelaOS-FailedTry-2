/*
 * RoboHTML input controls — behaviour (Phase 17.3).
 *
 * Phase 8.14 modelled an input's *state*: its typed attributes, its declared
 * value, its current value and its checkedness. This header is the behaviour a
 * user (through RoboInput) or a script (through RoboJS) drives: editing the
 * text of a text-like control, toggling a checkbox, selecting a radio within
 * its group, stepping a number, and clearing a control.
 *
 * ## What each operation is, and what it is not
 *
 *   - Editing is **value mutation, not an editor.** `HtmlInputInsertText` /
 *     `HtmlInputDeleteBackward` / `HtmlInputClear` change the control's current
 *     value. A caret, a selection and an undo stack are RoboText's editor
 *     (`RoboTextEditing.h`); the shell's input layer owns the bridge between
 *     the two. This module deliberately holds no caret and no composition
 *     state — duplicating an editor here is exactly what the architecture
 *     forbids.
 *   - Checkbox/radio behaviour is the *group* rule: selecting one radio clears
 *     the others of its name within its form owner, which is the one piece of
 *     control behaviour that is not expressible as a single state write.
 *   - Stepping applies `step` (default 1) and clamps to `min`/`max` when they
 *     are present and parse, because a value outside them is what the next
 *     validation pass would reject anyway.
 *
 * ## Events are a record, not a dispatch
 *
 * This build has no DOM event system yet (the `events` capability is 0), so
 * every operation that changed something fills an `HtmlInputChange` record.
 * The caller — a shell or a future event layer — turns that record into an
 * `input`/`change` event. Inventing a second, control-local event queue here
 * would be the duplicate the architecture warns against.
 */

#ifndef ROBO_HTML_INPUT_H
#define ROBO_HTML_INPUT_H

#include "RoboBase/RoboError.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What an operation changed. */
typedef enum HtmlInputChangeKind {
    /** Nothing changed (a no-op is not a failure). */
    RoboHtmlInputChangeNone = 0,
    /** The control's current value changed. */
    RoboHtmlInputChangeValue = 1,
    /** The control's checkedness changed. */
    RoboHtmlInputChangeChecked = 2
} HtmlInputChangeKind;

/** Report of one operation. */
typedef struct HtmlInputChange {
    int changed;               /* non-zero when @c kind is not NONE */
    HtmlInputChangeKind kind;
    int checked;               /* resulting checkedness, for a CHECKED change */
    size_t valueLength;        /* resulting value length, for a VALUE change */
    size_t groupCleared;       /* radios unselected by a group select */
} HtmlInputChange;

/** Clear @p change to a no-op report. Safe on NULL. */
void HtmlInputChangeInit(HtmlInputChange *change);

/** Non-zero when @p control accepts typed text (text-like input, textarea). */
int HtmlInputIsTextEditable(const HtmlNodeHandle *control);

/**
 * Insert @p text at the *end* of @p control's current value (the plain
 * behaviour for a control this layer does not track a caret for).
 *
 * @return RoboOk; RoboInvalidArgument (NULL args); RoboNotFound (@p control
 *         does not resolve); RoboNotSupported (the control does not accept
 *         text, or is disabled/readonly); RoboOutOfMemory.
 */
Result HtmlInputInsertText(
    const HtmlNodeHandle *control,
    const char *text,
    size_t length,
    HtmlInputChange *out,
    HtmlDiag *diag);

/**
 * Remove the last UTF-8 scalar from @p control's current value (backspace).
 * A value that ends mid-sequence is refused rather than corrupted.
 */
Result HtmlInputDeleteBackward(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag);

/** Clear @p control's current value to the empty string. */
Result HtmlInputClear(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag);

/**
 * Set @p control's checkedness directly (checkbox only; a radio uses
 * HtmlInputRadioSelect so the group rule is applied).
 */
Result HtmlInputSetChecked(
    const HtmlNodeHandle *control,
    int checked,
    HtmlInputChange *out,
    HtmlDiag *diag);

/** Flip @p control's checkedness (checkbox). */
Result HtmlInputToggleChecked(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag);

/**
 * Select @p control (a radio): set it checked and clear every other radio of
 * the same `name` in the same form owner. With no form owner the group is the
 * document's unowned radios of that name.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound; RoboNotSupported (not a
 *         radio).
 */
Result HtmlInputRadioSelect(
    const HtmlNodeHandle *control,
    HtmlInputChange *out,
    HtmlDiag *diag);

/**
 * Step a number control's value by @p steps (its `step`, default 1), clamped
 * to `min`/`max` when present and valid. An empty value starts from 0.
 *
 * @return RoboOk; RoboInvalidArgument; RoboNotFound; RoboNotSupported (not a
 *         number input); RoboParseError (the current value is not a number).
 */
Result HtmlInputStep(
    const HtmlNodeHandle *control,
    int steps,
    HtmlInputChange *out,
    HtmlDiag *diag);

/** Capability table for this surface. */
typedef struct HtmlInputCapabilities {
    int textEditing;      /* insert/delete/clear */
    int checkbox;         /* toggle / set */
    int radioGroups;      /* group select */
    int stepping;         /* number step */
    int caretModel;       /* 0: a caret/selection belongs to RoboText */
    int eventDispatch;    /* 0: events are a later phase; changes are records */
} HtmlInputCapabilities;

/** Fill @p out. RoboInvalidArgument on NULL. */
Result HtmlInputGetCapabilities(HtmlInputCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_INPUT_H */
