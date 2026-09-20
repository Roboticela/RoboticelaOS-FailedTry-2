/*
 * RoboDisplay dump (Phase 11.3) — see RoboPaintDisplayDump.h.
 *
 * One line per command: a zero-padded ordinal, the indentation the command's own depth
 * asks for, the command's one-line description, and a newline. The frame is the paint
 * dump's, and the line's content is the record's own text (`robo_paint_display_command_write`)
 * rather than a second rendering of it -- a dump that rendered its own rectangles would be
 * a second implementation of the record, and the day the two disagreed the dump would be
 * the one a person believed.
 */

#include "RoboPaint/RoboPaintDisplayDump.h"

#include "RoboPaint/RoboPaintDisplayCommand.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Digits in the line ordinal: `#0000` through `#9999` sort as integers. */
#define ROBO_PAINT_DISPLAY_DUMP_ORDINAL_DIGITS 4u

/** Spaces one level of depth indents by. */
#define ROBO_PAINT_DISPLAY_DUMP_INDENT 2u

/** Write @p ordinal as a zero-padded four-digit number, preceded by `#`. */
static void display_dump_ordinal(robo_paint_text_t *text, size_t ordinal)
{
    char digits[ROBO_PAINT_DISPLAY_DUMP_ORDINAL_DIGITS];
    size_t index = ROBO_PAINT_DISPLAY_DUMP_ORDINAL_DIGITS;

    robo_paint_text_char(text, '#');
    while (index > 0u) {
        index--;
        digits[index] = (char)('0' + (int)(ordinal % 10u));
        ordinal /= 10u;
    }
    for (index = 0u; index < ROBO_PAINT_DISPLAY_DUMP_ORDINAL_DIGITS; index++) {
        robo_paint_text_char(text, digits[index]);
    }
}

robo_result_t robo_paint_display_list_write(const robo_paint_display_list_t *list,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_paint_diag_t *diag)
{
    robo_paint_text_t text;
    size_t index;
    size_t count;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_INVALID_ARGUMENT,
                               "a buffer and an out_length are required");
    }

    robo_paint_text_init(&text, buffer, capacity);
    /* A NULL list and an empty list are the same answer -- an empty dump -- and it is a
     * legitimate one: a document that draws nothing has no commands to describe. */
    count = robo_paint_display_list_count(list);
    for (index = 0u; index < count; index++) {
        robo_paint_display_command_t command;
        char line[ROBO_PAINT_DISPLAY_DUMP_LINE_MAX + 1u];
        size_t line_length = 0u;
        size_t indent;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_ERROR,
                                   "the list changed under the dump");
        }
        if (robo_paint_display_command_write(&command, line, sizeof(line), &line_length,
                                       NULL) != ROBO_OK) {
            /* The line did not fit the bound `RoboPaintDisplayDump.h` states -- a bound this
             * module owns and a suite asserts, so this is an internal failure rather than a
             * caller's buffer being too small. */
            *out_length = text.length;
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_NOT_SUPPORTED,
                                   "a command's line exceeded ROBO_PAINT_DISPLAY_DUMP_LINE_MAX");
        }

        display_dump_ordinal(&text, index);
        robo_paint_text_char(&text, ' ');
        for (indent = 0u; indent < command.depth * ROBO_PAINT_DISPLAY_DUMP_INDENT; indent++) {
            robo_paint_text_char(&text, ' ');
        }
        robo_paint_text_string(&text, line);
        robo_paint_text_char(&text, '\n');
    }

    *out_length = text.length;
    if (!robo_paint_text_fits(&text)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_DISPLAY_LIST, ROBO_NOT_SUPPORTED,
                               "the dump does not fit the buffer");
    }
    return ROBO_OK;
}
