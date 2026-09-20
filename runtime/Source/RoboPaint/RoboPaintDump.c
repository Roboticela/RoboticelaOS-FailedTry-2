/*
 * RoboPaint dump (Phase 11.1) — see RoboPaintDump.h.
 *
 * One line per operation: a zero-padded ordinal, the indentation the operation's own
 * depth asks for, the operation's one-line description, and a newline. The format is
 * fixed so that a suite can assert on it and a person can diff two documents, and the
 * ordinals are padded to four digits so that the lines sort the way the list does.
 *
 * The description of an operation is `robo_paint_operation_write`'s and is *not*
 * reimplemented here: a dump that rendered its own rectangles would be a second
 * implementation of the record, and the day the two disagreed the dump would be the one
 * a person believed. So this file's whole job is the frame -- ordinal, indent, newline --
 * and the line's content is the record's own text.
 *
 * Since 11.6 there is exactly one thing appended after that text, and it is the one thing a
 * record's writer **cannot** produce: a text operation's characters. A record carries a span
 * and two numbers, and the pool is the only thing that knows what the span names -- so the
 * characters belong to the door that has the list, appended ` text="..."` and escaped by
 * `RoboPaintInternal.h`'s helper so that a newline inside a run cannot turn one operation's
 * line into two.
 *
 * Two details that are deliberate rather than incidental:
 *
 *   the indentation is **the operation's depth**, not a nested print of the tree. It is
 *   what a rasteriser walking the flat sequence would see, which is the view worth having
 *   when an operation is in the wrong place; a nested print would re-derive the structure
 *   from the tree and hide exactly that;
 *
 *   a long dump is refused with the length it **would** have needed
 *   (`RoboPaintDump.h`), which is why the text builder in `RoboPaintInternal.h` keeps
 *   counting after the buffer runs out. A caller can size a buffer from the refusal
 *   instead of from an estimate -- the contract every `_write` door in this tree uses.
 */

#include "RoboPaint/RoboPaintDump.h"

#include "RoboPaint/RoboPaintOperation.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Digits in the line ordinal: `#0000` through `#9999` sort as integers. */
#define ROBO_PAINT_DUMP_ORDINAL_DIGITS 4u

/** Spaces one level of depth indents by. */
#define ROBO_PAINT_DUMP_INDENT 2u

/** Write @p ordinal as a zero-padded four-digit number, preceded by `#`. */
static void paint_dump_ordinal(robo_paint_writer_t *text, size_t ordinal)
{
    char digits[ROBO_PAINT_DUMP_ORDINAL_DIGITS];
    size_t index = ROBO_PAINT_DUMP_ORDINAL_DIGITS;

    robo_paint_writer_char(text, '#');
    /*
     * Filled from the least significant end, so the padding is the digits the number did
     * not use. A five-digit ordinal keeps only its last four and the *value* is what a
     * caller compares, not the printed form -- which is why the ordinal is documented as
     * padding rather than as a total order.
     */
    while (index > 0u) {
        index--;
        digits[index] = (char)('0' + (int)(ordinal % 10u));
        ordinal /= 10u;
    }
    for (index = 0u; index < ROBO_PAINT_DUMP_ORDINAL_DIGITS; index++) {
        robo_paint_writer_char(text, digits[index]);
    }
}

robo_result_t robo_paint_list_write(const robo_paint_list_t *list,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *out_length,
                                   robo_paint_diag_t *diag)
{
    robo_paint_writer_t text;
    size_t index;
    size_t count;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DUMP, ROBO_INVALID_ARGUMENT,
                               "a buffer and an out_length are required");
    }

    robo_paint_writer_init(&text, buffer, capacity);
    /*
     * A NULL list and an empty list are the same answer -- an empty dump -- and it is a
     * legitimate one rather than a placeholder: a document that draws nothing has no
     * operations to describe, and a line saying so would be a line about the dumper.
     */
    count = robo_paint_list_count(list);
    for (index = 0u; index < count; index++) {
        robo_paint_operation_t operation;
        char line[ROBO_PAINT_DUMP_LINE_MAX + 1u];
        size_t line_length = 0u;
        size_t indent;

        if (robo_paint_list_at(list, index, &operation) != ROBO_OK) {
            /* Unreachable: the index came from the list's own count. Reported rather than
             * skipped so that a future list that can lose an operation says so here. */
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DUMP, ROBO_ERROR,
                                   "the list changed under the dump");
        }
        if (robo_paint_operation_write(&operation, line, sizeof(line), &line_length,
                                       NULL) != ROBO_OK) {
            /* The line did not fit the bound `RoboPaintDump.h` states. That is a bound
             * this module owns and a suite asserts, so it is an internal failure rather
             * than a caller's buffer being too small. */
            *out_length = text.length;
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DUMP, ROBO_NOT_SUPPORTED,
                                   "an operation's line exceeded ROBO_PAINT_DUMP_LINE_MAX");
        }
        /*
         * The characters, when the operation draws a run. The tail writer continues in the
         * **same line buffer** at the offset the record's own writer stopped at, so the two
         * halves are one string by construction and the `fits` check below is the whole of the
         * bound's enforcement.
         */
        if (operation.kind == ROBO_PAINT_OP_TEXT) {
            robo_paint_writer_t tail;

            robo_paint_writer_init(&tail, line + line_length,
                                   sizeof(line) - (line_length < sizeof(line) ? line_length
                                                                                             : sizeof(line)));
            robo_paint_writer_text_run(&tail, robo_paint_list_text_pool(list),
                                       operation.text.span, ROBO_PAINT_DUMP_TEXT_MAX);
            if (!robo_paint_writer_fits(&tail)) {
                *out_length = text.length;
                return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DUMP, ROBO_NOT_SUPPORTED,
                                       "an operation's line exceeded "
                                       "ROBO_PAINT_DUMP_LINE_MAX");
            }
            line_length += tail.length;
        }

        paint_dump_ordinal(&text, index);
        robo_paint_writer_char(&text, ' ');
        for (indent = 0u; indent < operation.depth * ROBO_PAINT_DUMP_INDENT; indent++) {
            robo_paint_writer_char(&text, ' ');
        }
        robo_paint_writer_string(&text, line);
        robo_paint_writer_char(&text, '\n');
    }

    *out_length = text.length;
    if (!robo_paint_writer_fits(&text)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DUMP, ROBO_NOT_SUPPORTED,
                               "the dump does not fit the buffer");
    }
    return ROBO_OK;
}
