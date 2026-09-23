/*
 * RoboText module-internal helpers (Phase 12.4).
 *
 * One thing lives here: a tiny writer for the `_write` dump doors, and it is here rather than repeated in
 * three files because the three dumps (`RoboTextFontStyleWrite`, `RoboTextGlyphWrite`,
 * `RoboTextMeasurementWrite`) must agree about a number's spelling or a test that compares two dumps of the
 * same setting will fail on formatting rather than on a value.
 *
 * **No `snprintf`.** This module is compiled into the guest program, which is freestanding and has no
 * formatted-output machinery; `RoboTextTypes.c` and `RoboDesktopText.c` each write their own for the same
 * reason, and sharing one between the three dumps here keeps the exception from spreading further.
 *
 * The convention is the one `_write` uses everywhere in this tree, with one addition: an append that would
 * not fit **abandons the line** and returns `RoboTextWriterOverflow` rather than writing a prefix. A
 * truncated dump reads as a different record, which is the one thing a dump must not do; the caller
 * therefore builds into a scratch buffer and copies the finished line out only if the caller's buffer can
 * hold it.
 */
#ifndef ROBO_TEXT_INTERNAL_H
#define ROBO_TEXT_INTERNAL_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

/** Returned by every writer below when the append did not fit. */
#define RoboTextWriterOverflow ((size_t)-1)

/** Append a NUL-terminated @p text. Returns the new length, or @ref RoboTextWriterOverflow. */
size_t RoboTextWriterText(char *buffer, size_t capacity, size_t length, const char *text);

/** Append an unsigned decimal. */
size_t RoboTextWriterUnsigned(char *buffer, size_t capacity, size_t length, uint32_t value);

/** Append a signed decimal (the `int32_t` the range and index doors report). */
size_t RoboTextWriterSigned(char *buffer, size_t capacity, size_t length, int32_t value);

/**
 * Append a float with two decimals, and the four cases that are not a finite number spelled out:
 * `nan`, `inf`, `-inf`, and a magnitude beyond what a dump needs to be exact about, which collapses to
 * `inf`/`-inf` rather than printing ten digits of a number nobody reads.
 *
 * Rounded to the nearest hundredth **with a carry**, so `0.999` prints `1.00` rather than `0.100`.
 */
size_t RoboTextWriterFloat(char *buffer, size_t capacity, size_t length, float value);

/**
 * One run row, formatted once for every stage that prints runs.
 *
 * `[a..b) scalars  bytes [c..d)  <label>` when @p hasBytes and `[a..b) scalars  <label>` when not. The
 * script table, the direction table and the bidi dump all print this shape, and this is the only place it
 * is spelled: a stage that formatted its own row would drift, and a reader comparing two dumps would be
 * comparing two formats.
 *
 * @return the row's length, or @ref RoboTextWriterOverflow when it does not fit @p capacity.
 */
size_t RoboTextWriterRunRow(char *line,
                            size_t capacity,
                            RoboTextRange range,
                            const char *label,
                            int hasBytes,
                            RoboTextByteIndex byteStart,
                            RoboTextByteIndex byteEnd);

/**
 * How a run table asks for one of its rows.
 *
 * A callback rather than a copy of the table loop in each stage: the *shape* of the table — the header, both
 * index spaces, the completeness check, the measuring pass that leaves a too-small buffer untouched — is the
 * same for script runs and direction runs, and only two fields of a row differ. `outRange` and `outLabel`
 * are both required to be filled; `outLabel` is borrowed and must outlive the call.
 */
typedef void (*RoboTextRunTableRow)(const void *runs,
                                    size_t index,
                                    RoboTextRange *outRange,
                                    const char **outLabel);

/**
 * A run set as a table: @p header (which includes its own newline), then one row per run.
 *
 * A row is `[a..b) scalars  bytes [c..d)  <label>` when a view is supplied and `[a..b) scalars  <label>`
 * when it is not, so a caller outside the text layer can read the byte spans it actually works in. With a
 * view the runs are validated against it first — an inverted or out-of-range run is refused rather than
 * printed — and the whole table is measured before a byte of it is written, so a buffer that cannot hold it
 * is left as the empty string.
 *
 * @param view      the text the runs index; its bytes may be NULL, in which case the byte column is absent.
 * @param runs      the run array @p row reads from.
 * @param count     runs in @p runs.
 * @param row       the row accessor.
 * @param header    the table's first line, newline included.
 * @param outLength receives the length a complete table needs; the caller's buffer is only written when it
 *                  fits, and this is set either way.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a run outside @p view; RoboNotSupported when
 *         @p buffer cannot hold the table or a row does not fit the internal line buffer.
 */
Result RoboTextWriterRunTable(RoboTextView view,
                              const void *runs,
                              size_t count,
                              RoboTextRunTableRow row,
                              const char *header,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength);

/**
 * The byte offset at which each scalar of @p view begins, plus the end of text at the end.
 *
 * The shared half of the two run dumps: a table that names both index spaces has to know where one space's
 * boundaries land in the other, and doing it once here is what keeps the script table and the direction
 * table agreeing about the same text. The result has @p scalarCount + 1 entries — the last is the view's
 * byte length — and the caller releases it with `RoboFree`.
 *
 * @param outOffsets receives NULL for an empty text, or a new array.
 *
 * @return RoboOk; RoboInvalidArgument when the text holds **fewer** scalars than the runs describe (the table is
 *         refused rather than filled with offsets that belong to a different text); RoboOutOfMemory.
 */
Result RoboTextWriterByteOffsets(RoboTextView view,
                                 size_t scalarCount,
                                 RoboTextByteIndex **outOffsets,
                                 RoboTextDiag *diag);

#endif /* ROBO_TEXT_INTERNAL_H */
