/*
 * The clipboard (Phase 13.40/13.41).
 *
 * ## The rule the whole file exists for
 *
 * **A clipboard holds logical data, never glyphs.** 13.41 is explicit: copy takes a selection, produces the
 * *source text* that selection covers, and puts that on the clipboard; paste takes clipboard text and hands it
 * to an editor as an insertion. A shaped glyph buffer on the clipboard would be a picture, and pasting a
 * picture into a text field is not a thing any user means.
 *
 * ## Where the text comes from, and why this layer cannot get it itself
 *
 * Converting a `RoboTextSelection` into the characters it covers is **RoboText's** job (12.21's positions and
 * 12.22's buffer), and this module may not include RoboText for it -- the one file that does is
 * `RoboInputTextBridge.c`. So the conversion arrives as a **function pointer**:
 *
 *     InputClipboardTextSource::readRange(userdata, start, end, out, capacity, outLength)
 *
 * which is exactly the shape `RoboTextSelectionMapWrite`-style doors already have. A caller with an editor
 * wires it to the editor; a caller with no editor passes NULL and copying refuses, which is the honest
 * answer.
 *
 * ## Formats
 *
 * `PlainText` is the only format this build **produces**. The vocabulary names HTML, rich text, images and
 * files so that a caller can *ask* for them and be told no, rather than inventing its own list -- and so that
 * the day one arrives, the number it gets is already fixed.
 *
 * See `Documentation/Runtime/Input/Clipboard.md`.
 */

#ifndef ROBO_INPUT_CLIPBOARD_H
#define ROBO_INPUT_CLIPBOARD_H

#include "RoboInput/RoboInputDiagnostics.h"
#include "RoboInput/RoboInputTarget.h"
#include "RoboInput/RoboInputTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The most text one clipboard holds. Generous for a paste and small enough to be a field. */
#define RoboInputClipboardTextMax 1024u

/** How many formats one clipboard entry may carry. */
#define RoboInputClipboardFormatsMax 4u

/** What a clipboard entry can be. */
typedef enum InputClipboardFormat {
    /** Nothing: a cleared entry's value, and what an empty clipboard reports. */
    RoboInputClipboardFormatNone = 0,
    /** UTF-8 text. The only format this build produces and consumes. */
    RoboInputClipboardFormatPlainText = 1,
    /** Markup. Named, never produced here (18.x owns documents). */
    RoboInputClipboardFormatHtml = 2,
    /** Styled text. Named, never produced here. */
    RoboInputClipboardFormatRichText = 3,
    /** An image. Named, never produced here. */
    RoboInputClipboardFormatImage = 4,
    /** A list of files. Named, never produced here (11.x owns the filesystem). */
    RoboInputClipboardFormatFiles = 5
} InputClipboardFormat;

/** Number of defined formats. Asserted positionally by `input.clipboard.*`. */
#define RoboInputClipboardFormatCount 6u

/** Non-zero if @p format is a defined format. */
int InputClipboardFormatIsKnown(InputClipboardFormat format);

/** Human-readable format name; never NULL; `"unknown"` when undefined. */
const char *InputClipboardFormatName(InputClipboardFormat format);

/** Non-zero when this build can **produce** @p format. Only plain text is true. */
int InputClipboardFormatIsSupported(InputClipboardFormat format);

/**
 * Where the characters of a range come from.
 *
 * `readRange` fills @p out with the UTF-8 text between two *source* positions (12.21's coordinates, which is
 * what a selection is expressed in), NUL-terminated and within @p capacity; @p outLength receives the length
 * written. It returns @ref RoboNotFound when the range is not in the current text -- an editor whose text
 * changed under a stale selection -- which the caller of `InputClipboardCopy` sees as a refusal rather than
 * as empty text.
 */
typedef struct InputClipboardTextSource {
    /** Whatever the source needs (an editor, in practice). */
    void *userdata;
    /** The characters between two source positions. */
    Result (*readRange)(void *userdata,
                        uint32_t start,
                        uint32_t end,
                        char *out,
                        size_t capacity,
                        size_t *outLength);
} InputClipboardTextSource;

/**
 * One clipboard entry.
 *
 * The text is stored **by value** in a fixed buffer, and that is a deliberate limit rather than an oversight:
 * a clipboard that allocated would need an owner, a lock and a lifetime, and the three of those are a
 * subsystem. A paste of more than `RoboInputClipboardTextMax` bytes is refused with the length it needed, the
 * same complete-or-absent rule every writer and reader in this module follows.
 */
typedef struct InputClipboardData {
    /** Which formats this entry carries. Bit `1 << format`. */
    uint32_t formats;
    /** The text, NUL-terminated. Empty when there is none. */
    char text[RoboInputClipboardTextMax];
    /** Bytes of @ref text, without the terminator. */
    size_t textLength;
    /** A sequential number, bumped on every write: what a caller caches against. */
    uint32_t serial;
    /** The application that wrote it, or `RoboInputApplicationIdNone` when nobody said. */
    InputApplicationId owner;
} InputClipboardData;

/** Clear @p data to "empty". Safe on NULL. */
void InputClipboardDataInit(InputClipboardData *data);

/** Non-zero when @p data carries @p format. Safe on NULL (0). */
int InputClipboardDataHasFormat(const InputClipboardData *data, InputClipboardFormat format);

/** Non-zero when @p data is an entry this build may paste from: text present, length consistent. */
int InputClipboardDataIsValid(const InputClipboardData *data);

/**
 * Replace @p data's text with @p text.
 *
 * Refused (@ref RoboNotFound) when the text does not fit, with @p outLength receiving the length it needed --
 * so a caller can size and retry rather than learn nothing. Refused as @ref RoboInvalidArgument for a NULL
 * @p text, and a `NULL` text of zero length clears the entry, which is how "copy nothing" empties a clipboard.
 */
Result InputClipboardDataSetText(InputClipboardData *data,
                                 const char *text,
                                 size_t length,
                                 InputApplicationId owner,
                                 size_t *outLength,
                                 InputDiag *diag);

/** The text. Never NULL; empty for an empty clipboard. */
const char *InputClipboardDataText(const InputClipboardData *data);

/** Field-by-field equality, the text compared by content. */
int InputClipboardDataEqual(const InputClipboardData *a, const InputClipboardData *b);

/* ============================================================================================== *
 * The operations (13.41)
 * ============================================================================================== */

/** How a copy ended. */
typedef enum InputClipboardResult {
    /** The range was read and the clipboard now holds it. */
    RoboInputClipboardCopied = 0,
    /** The range is empty: nothing was written, and a paste still holds what it held. */
    RoboInputClipboardNothing = 1,
    /** There is no text source, or the range could not be read. */
    RoboInputClipboardUnavailable = 2,
    /** The text did not fit `RoboInputClipboardTextMax`. */
    RoboInputClipboardTooLarge = 3
} InputClipboardResult;

/** Number of defined results. Asserted positionally by `input.clipboard.*`. */
#define RoboInputClipboardResultCount 4u

/** Non-zero if @p result is a defined result. */
int InputClipboardResultIsKnown(InputClipboardResult result);

/** Human-readable result name; never NULL; `"unknown"` when undefined. */
const char *InputClipboardResultName(InputClipboardResult result);

/**
 * Copy `[start, end)` of the source's text onto @p data.
 *
 * A **half-open** range in source coordinates, which is what a selection already is. An empty range is
 * `Nothing` and leaves the clipboard alone: replacing it with an empty string is how a stray click destroys
 * what the user copied.
 *
 * @p outLength receives the length the range needed, which is what a `TooLarge` refusal reports.
 */
InputClipboardResult InputClipboardCopy(const InputClipboardTextSource *source,
                                        uint32_t start,
                                        uint32_t end,
                                        InputApplicationId owner,
                                        InputClipboardData *data,
                                        size_t *outLength,
                                        InputDiag *diag);

/**
 * Paste @p data into a text destination.
 *
 * The paste is **handed over, not performed**: @p insert is the destination's own insertion door (the
 * command layer's, in practice), and this returns the result it returned. A clipboard with no plain text is
 * `Nothing` and the destination is not called at all, because calling it with an empty string would delete a
 * selection.
 */
typedef Result (*InputClipboardInsertFn)(void *userdata, const char *text, size_t length);

InputClipboardResult InputClipboardPaste(const InputClipboardData *data,
                                         InputClipboardInsertFn insert,
                                         void *userdata);

/** One complete line for @p data, or @ref RoboNotSupported when it does not fit. */
Result InputClipboardDataWrite(const InputClipboardData *data,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength,
                               InputDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_INPUT_CLIPBOARD_H */
