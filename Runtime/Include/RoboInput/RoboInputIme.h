/*
 * Input methods (Phase 13.37--13.39).
 *
 * ## The boundary, exactly
 *
 * An input method is code outside this runtime's control -- an IME on a host, a candidate window owned by
 * somebody else -- that wants three things from the focused editor:
 *
 *   **where the caret is**, so its candidate window goes next to it (13.39);
 *   **what text is around it**, within whatever privacy the editor permits (13.38);
 *   **somewhere to put a composition**, which is text the user has not committed (13.37).
 *
 * The third is the one that has to be got right, and the rule is the same one 12.22 states: **composition is
 * text with a kept slice.** The preedit lives *in the editor's buffer*, marked by `RoboTextComposition`, so the
 * caret, the selection and every query work during it; committing is a replacement of that slice, and cancelling
 * restores it. RoboInput's job is the *interface* -- it owns `InputMethodContext` and the vocabulary, and it
 * never owns a buffer, a caret or a glyph.
 *
 * ## The client is a vtable, and it is RoboText's
 *
 * `InputMethodClient` is four functions that map one-to-one onto RoboText's editing and selection doors (12.21's
 * caret geometry, 12.22's `RoboTextEditorComposition*`). That is deliberate: the editor is on the other side of
 * the seam, this module may not include it, and a caller that has an editor wires the two together in one
 * function each. A client that cannot answer a question returns `RoboNotSupported` and this layer *reports* it
 * -- the context's `capabilities` is how a caller knows whether the caret geometry is available, rather than
 * guessing from a zero.
 *
 * ## What this build does not do
 *
 * It does not implement an input method, it does not read a keyboard, and it has no candidate-window rendering:
 * `InputMethodCandidateRect` is a rectangle a caller *uses*, and drawing it is the shell's. There is no
 * `InputMethodActivate` that reaches a platform backend either -- `platformAdapters` is still 0 -- so the
 * context is exercised by callers, tests and (later) an adapter, which is the honest description of where this
 * stands.
 *
 * See `Documentation/Runtime/Input/InputMethods.md`.
 */

#ifndef ROBO_INPUT_IME_H
#define ROBO_INPUT_IME_H

#include "RoboInput/RoboInputComposition.h"
#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How much surrounding text the context may ask for. */
#define RoboInputImeSurroundingMax 128u

/** How many clauses a composition may declare (a candidate's segments). */
#define RoboInputImeMaxClauses 8u

/** What an input method may be told about the editor's state. */
#define RoboInputImeCapabilitySurroundingText ((uint32_t)(1u << 0))
#define RoboInputImeCapabilityCaretGeometry ((uint32_t)(1u << 1))
#define RoboInputImeCapabilitySelection ((uint32_t)(1u << 2))
#define RoboInputImeCapabilityInsert ((uint32_t)(1u << 3))
/** Every capability this build names. */
#define RoboInputImeCapabilityAll \
    (RoboInputImeCapabilitySurroundingText | RoboInputImeCapabilityCaretGeometry | \
     RoboInputImeCapabilitySelection | RoboInputImeCapabilityInsert)

/** What an editor permits. */
typedef enum InputImeTextPurpose {
    /** Nothing particular. */
    RoboInputImePurposeNone = 0,
    /** Ordinary prose. */
    RoboInputImePurposeText = 1,
    /** A search field: an input method should not keep a learning history. */
    RoboInputImePurposeSearch = 2,
    /** A password field: **no** surrounding text, ever, whatever the editor could report. */
    RoboInputImePurposePassword = 3,
    /** An email address. */
    RoboInputImePurposeEmail = 4,
    /** A URL. */
    RoboInputImePurposeUrl = 5,
    /** A number. */
    RoboInputImePurposeNumber = 6
} InputImeTextPurpose;

/** Number of defined purposes. Asserted positionally by `input.ime.*`. */
#define RoboInputImeTextPurposeCount 7u

/** Non-zero if @p purpose is a defined purpose. */
int InputImeTextPurposeIsKnown(InputImeTextPurpose purpose);

/** Human-readable purpose; never NULL; `"unknown"` when undefined. */
const char *InputImeTextPurposeName(InputImeTextPurpose purpose);

/** Non-zero when @p purpose forbids reporting surrounding text. */
int InputImeTextPurposeIsPrivate(InputImeTextPurpose purpose);

/** A caret, in the editor's own coordinates, plus which way it points. */
typedef struct InputMethodCaret {
    /** Non-zero when the rectangle is a real one. */
    int valid;
    /** The caret's rectangle: `x`/`y` is the insertion point's top-left. `width` carries the *inline* extent,
     *  so a horizontal line's caret has a small width and a column's has a small height (12.21's rule). */
    float x;
    float y;
    float width;
    float height;
    /** Non-zero when the caret is horizontal (a vertical writing mode). */
    int horizontal;
} InputMethodCaret;

/**
 * What a focused editor exposes to an input method.
 *
 * Every door may refuse with @ref RoboNotSupported, and that is the design: an editor that has no caret
 * geometry (nobody laid it out yet) says so once, in `capabilities`, rather than answering with zeroes a
 * caller would read as a caret in the corner.
 */
typedef struct InputMethodClient {
    /** Whatever the client needs (an editor, in practice). */
    void *userdata;
    /** What it can answer: `ROBO_INPUT_IME_CAPABILITY_*` bits. */
    uint32_t capabilities;
    /** Where the caret is **in document/window space**, so a candidate window can be placed beside it. */
    Result (*caret)(void *userdata, InputMethodCaret *out);
    /** Text around the caret, into @p out (UTF-8, NUL-terminated). */
    Result (*surroundingText)(void *userdata,
                              uint32_t before,
                              uint32_t after,
                              char *out,
                              size_t capacity,
                              size_t *outLength);
    /** Insert text at the caret, replacing the selection. */
    Result (*insertText)(void *userdata, const char *text, size_t length);
    /** Set or replace the composition slice. */
    Result (*setComposition)(void *userdata, const char *text, size_t length);
    /** Commit the composition slice (`text` NULL commits what is there). */
    Result (*commitComposition)(void *userdata, const char *text, size_t length);
    /** Cancel the composition slice, restoring what it replaced. */
    Result (*cancelComposition)(void *userdata);
} InputMethodClient;

/** The context RoboInput owns and an input method talks to. */
typedef struct InputMethodContext {
    /** The focused target the input method is composing into. */
    InputTarget target;
    /** Non-zero when @ref target is live. */
    int active;
    /** The editor's doors. */
    InputMethodClient client;
    /** What the editor permits. */
    InputImeTextPurpose purpose;
    /** What the **last** question the client refused was called. Never NULL; empty when none refused. */
    const char *lastRefusal;
    /** Refusals seen. Non-zero is not an error: it is a client that cannot answer. */
    uint32_t refusals;
    /** Compositions started through this context. */
    uint32_t compositions;
    /** Compositions committed. */
    uint32_t commits;
    /** Compositions cancelled. */
    uint32_t cancels;
} InputMethodContext;

/** Clear @p context to "no focused editor". Safe on NULL. */
void InputMethodContextInit(InputMethodContext *context);

/**
 * Attach @p client to the focused @p target.
 *
 * Attaching a *new* target cancels any composition the previous one had: a composition belongs to an editor,
 * and leaving one behind on a target that no longer has focus is how a preedit appears in the wrong field.
 * Returns @ref RoboInvalidArgument for an invalid target; @p client may be NULL, in which case the context is
 * active but can answer nothing, which is a legitimate state (a field with no editor behind it yet).
 */
Result InputMethodContextAttach(InputMethodContext *context,
                                const InputTarget *target,
                                const InputMethodClient *client,
                                InputImeTextPurpose purpose,
                                InputDiag *diag);

/** Detach, cancelling any composition. Returns non-zero when one was cancelled. */
int InputMethodContextDetach(InputMethodContext *context);

/** Non-zero when a focused editor is attached. */
int InputMethodContextIsActive(const InputMethodContext *context);

/** What the attached client can answer, or 0. */
uint32_t InputMethodContextCapabilities(const InputMethodContext *context);

/**
 * The caret's rectangle, in the space the client reports.
 *
 * Refused with @ref RoboNotSupported when the client has no caret geometry -- and *reported* into
 * @ref InputMethodContext::refusals, so a caller can tell "no layout yet" from "no editor".
 */
Result InputMethodContextCaret(InputMethodContext *context,
                               InputMethodCaret *out,
                               InputDiag *diag);

/**
 * Text around the caret.
 *
 * A **private** purpose (`PASSWORD`) answers @ref RoboNotSupported and never asks the client: the constraint is
 * this layer's, and a client that could report the text must still not be asked for it.
 */
Result InputMethodContextSurroundingText(InputMethodContext *context,
                                         uint32_t before,
                                         uint32_t after,
                                         char *out,
                                         size_t capacity,
                                         size_t *outLength,
                                         InputDiag *diag);

/**
 * Where an input method's candidate window should go, given the caret and a preferred size.
 *
 * The caret's own rectangle is the anchor: the candidate goes **below** it by default, flipped **above** when
 * it would leave @p viewport, and clamped horizontally so it never hangs off the side. The result is in the
 * same space the caret was reported in, which is the only space this layer knows.
 *
 * Refused with @ref RoboNotSupported when there is no caret geometry to anchor to, because a candidate window
 * placed with no anchor is a window placed at the origin.
 */
typedef struct InputMethodCandidateRect {
    /** Non-zero when @ref x / `y` / `width` / `height` are a rectangle to use. */
    int valid;
    float x;
    float y;
    float width;
    float height;
    /** Non-zero when the candidate had to be flipped above the caret. */
    int flipped;
    /** Non-zero when the candidate had to be clamped horizontally. */
    int clamped;
} InputMethodCandidateRect;

Result InputMethodContextCandidateRect(InputMethodContext *context,
                                       float width,
                                       float height,
                                       const GfxRect *viewport,
                                       InputMethodCandidateRect *out,
                                       InputDiag *diag);

/**
 * Begin or update a composition of @p text.
 *
 * Both go to the client's `setComposition`, which is RoboText's replace-composition door: the preedit is text
 * in the buffer with a kept slice, so a caret and a selection work during it. @p clauses (may be NULL) is how
 * many segments the input method wants highlighted; this layer counts them and nothing more.
 */
Result InputMethodContextCompose(InputMethodContext *context,
                                 const char *text,
                                 size_t length,
                                 uint32_t clauses,
                                 InputDiag *diag);

/** Commit the composition, optionally replacing it with @p text (NULL commits what is there). */
Result InputMethodContextCommit(InputMethodContext *context,
                                const char *text,
                                size_t length,
                                InputDiag *diag);

/** Cancel the composition, restoring what it replaced. */
Result InputMethodContextCancel(InputMethodContext *context, InputDiag *diag);

/** Insert committed text at the caret, replacing the selection. */
Result InputMethodContextInsertText(InputMethodContext *context,
                                    const char *text,
                                    size_t length,
                                    InputDiag *diag);

/** One complete line for @p context, or @ref RoboNotSupported when it does not fit. */
Result InputMethodContextWrite(const InputMethodContext *context,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag);

/** One complete line for @p caret, or @ref RoboNotSupported when it does not fit. */
Result InputMethodCaretWrite(const InputMethodCaret *caret,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             InputDiag *diag);

/** One complete line for @p rect, or @ref RoboNotSupported when it does not fit. */
Result InputMethodCandidateRectWrite(const InputMethodCandidateRect *rect,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_IME_H */
