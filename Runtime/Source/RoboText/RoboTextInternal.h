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

#endif /* ROBO_TEXT_INTERNAL_H */
