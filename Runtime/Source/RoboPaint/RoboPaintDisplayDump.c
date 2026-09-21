/*
 * RoboDisplay dump (Phase 11.3) — see RoboPaintDisplayDump.h.
 *
 * One line per command: a zero-padded ordinal, the indentation the command's own depth
 * asks for, the command's one-line description, and a newline. The frame is the paint
 * dump's, and the line's content is the record's own text (`PaintDisplayCommandWrite`)
 * rather than a second rendering of it -- a dump that rendered its own rectangles would be
 * a second implementation of the record, and the day the two disagreed the dump would be
 * the one a person believed.
 */

#include "RoboPaint/RoboPaintDisplayDump.h"

#include "RoboPaint/RoboPaintDisplayCommand.h"

#include "RoboPaintInternal.h"

#include <string.h>

/** Digits in the line ordinal: `#0000` through `#9999` sort as integers. */
#define RoboPaintDisplayDumpOrdinalDigits 4u

/** Spaces one level of depth indents by. */
#define RoboPaintDisplayDumpIndent 2u

/** Write @p ordinal as a zero-padded four-digit number, preceded by `#`. */
static void DisplayDumpOrdinal(PaintWriter *text, size_t ordinal)
{
    char digits[RoboPaintDisplayDumpOrdinalDigits];
    size_t index = RoboPaintDisplayDumpOrdinalDigits;

    PaintWriterChar(text, '#');
    while (index > 0u) {
        index--;
        digits[index] = (char)('0' + (int)(ordinal % 10u));
        ordinal /= 10u;
    }
    for (index = 0u; index < RoboPaintDisplayDumpOrdinalDigits; index++) {
        PaintWriterChar(text, digits[index]);
    }
}

Result PaintDisplayListWrite(const PaintDisplayList *list,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     PaintDiag *diag)
{
    PaintWriter text;
    size_t index;
    size_t count;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboInvalidArgument,
                               "a buffer and an out_length are required");
    }

    PaintWriterInit(&text, buffer, capacity);
    /* A NULL list and an empty list are the same answer -- an empty dump -- and it is a
     * legitimate one: a document that draws nothing has no commands to describe. */
    count = PaintDisplayListCount(list);
    for (index = 0u; index < count; index++) {
        PaintDisplayCommand command;
        char line[RoboPaintDisplayDumpLineMax + 1u];
        size_t lineLength = 0u;
        size_t indent;

        if (PaintDisplayListAt(list, index, &command) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboError,
                                   "the list changed under the dump");
        }
        if (PaintDisplayCommandWrite(&command, line, sizeof(line), &lineLength,
                                       NULL) != RoboOk) {
            /* The line did not fit the bound `RoboPaintDisplayDump.h` states -- a bound this
             * module owns and a suite asserts, so this is an internal failure rather than a
             * caller's buffer being too small. */
            *outLength = text.length;
            return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboNotSupported,
                                   "a command's line exceeded ROBO_PAINT_DISPLAY_DUMP_LINE_MAX");
        }
        /* The run's characters, appended in the same line buffer -- the one part of a
         * `draw-text` line that the command's own writer cannot produce, because a command
         * names its characters through a span and the list owns the pool. */
        if (command.kind == RoboPaintDisplayCmdDrawText) {
            PaintWriter tail;

            PaintWriterInit(&tail, line + lineLength,
                                   sizeof(line) - (lineLength < sizeof(line) ? lineLength
                                                                                             : sizeof(line)));
            PaintWriterTextRun(&tail, PaintDisplayListTextPool(list),
                                       command.text.span,
                                       RoboPaintDisplayDumpTextMax);
            if (!PaintWriterFits(&tail)) {
                *outLength = text.length;
                return RoboPaintDiag(diag, RoboPaintDomainDisplayList,
                                       RoboNotSupported,
                                       "a command's line exceeded "
                                       "ROBO_PAINT_DISPLAY_DUMP_LINE_MAX");
            }
            lineLength += tail.length;
        }

        DisplayDumpOrdinal(&text, index);
        PaintWriterChar(&text, ' ');
        for (indent = 0u; indent < command.depth * RoboPaintDisplayDumpIndent; indent++) {
            PaintWriterChar(&text, ' ');
        }
        PaintWriterString(&text, line);
        PaintWriterChar(&text, '\n');
    }

    *outLength = text.length;
    if (!PaintWriterFits(&text)) {
        return RoboPaintDiag(diag, RoboPaintDomainDisplayList, RoboNotSupported,
                               "the dump does not fit the buffer");
    }
    return RoboOk;
}
