/*
 * RoboInput composition payload (Phase 13.37) — an input method's preedit, as an event.
 *
 * ## What a composition is, in one paragraph
 *
 * An input method (an IME, a dead-key sequence, a handwriting or voice front end) does not produce text a
 * character at a time. It produces a *preedit*: text that is being composed, visible to the user, not yet
 * committed, and mutable until the user accepts it. Some of it is already final -- a clause the method has
 * resolved -- and some is not. This module's job is to say when that starts, changes, ends and is
 * cancelled; the *text* is the editor's, and that split is the whole design here.
 *
 * ## Why the preedit is on the event *and* not the event's to own
 *
 * The authoritative composing text lives in the **editor** (RoboText's `RoboTextComposition`,
 * Phase 12.22), where it is a slice of the same buffer the caret, the selection and every edit query work
 * against -- so an editor can delete inside a preedit, move within it and replace it, all through one
 * mutation. An input layer that owned a second copy would have to keep two strings agreed, and the day they
 * disagreed the user would be looking at one of them while typing into the other.
 *
 * The event therefore carries a **bounded copy** of what the input method last reported, because an event
 * is a value the queue owns and a consumer that must *display* an increment should not have to keep a
 * reference to an editor it may not have. The bound is one keystroke's (`RoboInputTextMax`) and the reason
 * is this module's oldest rule: a fragment longer than the event can carry is **refused, never truncated**,
 * because a truncated preedit is different text. An input method with a longer composition reports it in
 * steps -- which is what a composition *is*, a sequence of updates -- and the editor accumulates them.
 *
 * ## Byte offsets, not scalar offsets
 *
 * `clauseStart`/`clauseEnd` and `caret` are **byte** offsets into `preedit`, and that is the deliberate
 * choice: the string travels whole on the event, so a range into it should be a range into the thing the
 * consumer is looking at, and bytes need no decoding to be checked for "inside the string". RoboText, whose
 * positions are scalar offsets, converts when it hands the preedit to the editor -- which it does through
 * the editor's own byte-taking insert door, so the conversion is in the layer that owns the scalar model
 * rather than in a record that carries bytes.
 *
 * ## The clause, and why it is on the record
 *
 * A real input method commits in clauses: a Japanese preedit may consist of several resolved segments, and
 * the *current* one is what a candidate window is about. `clauseStart`/`clauseEnd` name that one segment,
 * with both zero meaning "no clause is being resolved" -- which is the honest value for a dead-key
 * sequence, where there is exactly one result and no candidates at all. A null range (`clauseStart ==
 * clauseEnd` with a non-empty preedit) is legitimate and means "the caret's clause is empty", which is what
 * an input method reports between clauses.
 *
 * ## What is absent
 *
 * No candidate *list*: a candidate window's contents belong to the input method, which this module does not
 * implement, and a bounded list of strings on an event would be a second place candidates live. No
 * candidate *position*: that is derived from the caret, which the editor answers (13.39), so the geometry
 * has one source rather than two. No style, colour or underline information: a preedit's appearance is a
 * document's business, and the editor is where the text is. And no "composition id" or sequence number:
 * there is one composition at a time per focus scope, and the focus system (13.5) is what enforces that.
 */
#ifndef ROBO_INPUT_COMPOSITION_H
#define ROBO_INPUT_COMPOSITION_H

#include "RoboBase/RoboError.h"

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The preedit is empty: this update reports that the composition is now nothing. */
#define RoboInputCompositionFlagEmpty ((uint32_t)(1u << 0))
/** The preedit is a candidate the input method has resolved and the user has not yet accepted. */
#define RoboInputCompositionFlagResolved ((uint32_t)(1u << 1))
/** The user is inside the preedit: the caret is between composed characters rather than at its end. */
#define RoboInputCompositionFlagCaretInside ((uint32_t)(1u << 2))

/** Every defined composition flag. */
#define RoboInputCompositionFlagAll                                                       \
    (RoboInputCompositionFlagEmpty | RoboInputCompositionFlagResolved |                   \
     RoboInputCompositionFlagCaretInside)

/**
 * Capacity of one composition event's preedit, including the NUL.
 *
 * One keystroke's worth, deliberately (see this header's comment): an input method reports a composition as
 * a *sequence* of updates and the editor accumulates them, so the bound is on one update rather than on a
 * composition.
 */
#define RoboInputCompositionTextMax RoboInputTextMax

/** One composition event's payload, by value. */
typedef struct InputCompositionData {
    /** The preedit as the input method last reported it. UTF-8, NUL-terminated, at most
     *  `RoboInputCompositionTextMax - 1` bytes; may be empty (`EMPTY`). */
    char preedit[RoboInputCompositionTextMax];
    /** Byte offset of the start of the clause being resolved; 0 when there is none. */
    uint32_t clauseStart;
    /** Byte offset one past the end of that clause; 0 when there is none. */
    uint32_t clauseEnd;
    /** Byte offset of the caret within @ref preedit, or its length when the caret is at the end. */
    uint32_t caret;
    /** `RoboInputCompositionFlag*` bits. */
    uint32_t flags;
} InputCompositionData;

/** Clear @p data: an empty preedit, no clause, the caret at 0. Safe on NULL. */
void InputCompositionDataInit(InputCompositionData *data);

/**
 * Fill @p data.
 *
 * @param preedit     may be NULL, which is an empty preedit; longer than `RoboInputCompositionTextMax - 1`
 *                    bytes is **refused** (never truncated).
 * @param clauseStart byte offset of the clause's start, or 0 for "no clause".
 * @param clauseEnd   byte offset one past the clause's end, or 0 for "no clause". Must not precede
 *                    @p clauseStart, and must not exceed the preedit's length.
 * @param caret       byte offset of the caret; must not exceed the preedit's length.
 * @param flags       `RoboInputCompositionFlag*` bits.
 *
 * The `EMPTY` flag is **derived**, not trusted: an empty preedit sets it and a non-empty one clears it, so
 * a caller cannot hand over a record that says it is empty while carrying text. `data` is cleared first and
 * filled only if every argument is accepted.
 */
Result InputCompositionDataSet(InputCompositionData *data,
                               const char *preedit,
                               uint32_t clauseStart,
                               uint32_t clauseEnd,
                               uint32_t caret,
                               uint32_t flags,
                               InputDiag *diag);

/**
 * Non-zero when @p data is a record this build may carry on a composition event.
 *
 * The rules: the preedit is terminated inside its capacity; the caret is within the preedit; the clause is
 * either absent (both offsets 0) or a range that lies inside the preedit and does not invert; the flags are
 * defined bits; and `EMPTY` agrees with the preedit being empty in both directions, which is the rule that
 * makes the flag worth having.
 */
int InputCompositionDataIsValid(const InputCompositionData *data);

/** Length of @p data's preedit in bytes, excluding the NUL; 0 for NULL. */
size_t InputCompositionDataLength(const InputCompositionData *data);

/** Non-zero when @p data carries no preedit. Safe on NULL (1: nothing carries nothing). */
int InputCompositionDataIsEmpty(const InputCompositionData *data);

/** Copy @p data's preedit into @p out, guaranteed terminated. Returns the length copied (excluding NUL). */
size_t InputCompositionDataText(const InputCompositionData *data, char *out, size_t capacity);

/** Field-by-field equality, the preedit compared as a string. Safe on NULL; two NULLs are equal. */
int InputCompositionDataEqual(const InputCompositionData *a, const InputCompositionData *b);

/*
 * The four event types a composition produces, as one sentence each -- they are the vocabulary
 * `RoboInputEvent.h` numbers, gathered here because a reader of this file is the reader who wants them:
 *
 *   `CompositionStart`    the input method has begun composing. The preedit may be empty (a method that
 *                         starts on the first keystroke and fills in as it goes) or already carry text.
 *   `CompositionUpdate`   the preedit changed: text was added, removed or replaced, or the clause or caret
 *                         moved. This is the type an input method reports most, and the only one that may
 *                         be coalesced (13.47), because only the last state is observable.
 *   `CompositionCommit`   the composition ended and was **accepted**. The preedit becomes committed text;
 *                         a consumer that already holds it in its editor buffer may ignore the copy here.
 *   `CompositionCancel`   the composition ended and was **discarded**. The editor restores the state the
 *                         composition began over -- which is 12.22's job and not an event's.
 */

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_COMPOSITION_H */
