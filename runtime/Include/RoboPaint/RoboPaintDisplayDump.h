/*
 * RoboDisplay dump (Phase 11.3).
 *
 * The developer's view of a display list: one line per command, indented by the depth the
 * command carries, in the order the list holds them. It is the paint dump's shape
 * (`RoboPaintDump.h`) applied to the artifact one stage above it, and it exists so that a
 * person can diff two documents *after* the box model has been resolved -- which is the
 * view a rasteriser will actually have.
 *
 * The three things it deliberately is not are the paint dump's: not a renderer (it prints
 * what the command says; a dump that disagreed would be a second implementation of the
 * record), not a tree (the indentation is the command's own `depth`, so it is what a
 * rasteriser walks), and not a console (the guest target has no `stdio`). The format is
 * fixed so a suite can assert it:
 *
 *   #0000 BEGIN_NODE       node=1 depth=0 rect=0,0,100,40 clip=visible/visible
 *   #0001   DRAW_BACKGROUND node=1 depth=0 rect=0,0,100,40 clip=visible/visible color=#ff0000ff
 *   #0002   DRAW_BORDER     node=1 depth=0 rect=0,0,100,5  clip=visible/visible color=#000000ff
 *   #0003   CONTENT         node=1 depth=0 rect=1,1,98,38  clip=visible/visible
 *   #0004   DRAW_TEXT       node=7 depth=1 rect=1,5,80,16   clip=visible/visible
 *   color=#000000ff code_points=5 baseline=13 font=16.00/400/upright/ltr text="Hello"
 *   #0005 END_NODE          node=1 depth=0 rect=0,0,100,40 clip=visible/visible
 *
 * The ordinal is a 4-digit zero-padded index so the lines sort as the list does, and the
 * kind is left-aligned in a fixed column so a two-list diff lines up.
 *
 * A `draw-text` line carries its run's **characters** (11.6) -- read out of the display
 * list's own pool, escaped, and bounded by `ROBO_PAINT_DISPLAY_DUMP_TEXT_MAX` -- which is
 * the one part of a line no command's own writer can produce, because a command names its
 * characters through a span and only the list knows what the span names. It also means this
 * door's lines are longer than they were before the phase, which the bound below records.
 */
#ifndef ROBO_PAINT_DISPLAY_DUMP_H
#define ROBO_PAINT_DISPLAY_DUMP_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintDiagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Characters one dumped command line can need, excluding the NUL.
 *
 * A bound rather than a guess, derived the way `ROBO_PAINT_DUMP_LINE_MAX` is. The widest
 * line's parts at their widest printed forms are:
 *
 *   the kind, padded to `ROBO_PAINT_DISPLAY_NAME_COLUMN` (16) plus its space           17
 *   `node=` plus ten digits                                                      15
 *   ` depth=` plus ten digits                                                    17
 *   ` rect=` plus four `int32` numbers and three commas                          52
 *   ` clip=` plus two axes of `mode[start,end]`                                  72
 *   ` color=` plus `#rrggbbaa`                                                   16
 *                                                                              ----
 *                                                                               189
 *
 * and, for a `draw-text` command (11.6), the widest printed characters:
 *
 *   ` text="` plus `ROBO_PAINT_DISPLAY_DUMP_TEXT_MAX` code points at ten bytes
 *   each, plus `...` and the closing quote                                      173
 *                                                                              ----
 *                                                                               362
 *
 * plus a line's worth of headroom, so a caller sizing a buffer from this gets a dump it
 * does not have to retry -- and a future field that pushes the widest line past it fails
 * `paint.display.dump_line_bound`, which measures the widest line this build can actually
 * produce rather than trusting this paragraph.
 */
#define ROBO_PAINT_DISPLAY_DUMP_LINE_MAX 384u

/** Code points of a run one dumped line prints before it stops, for the reason
 *  `ROBO_PAINT_DUMP_TEXT_MAX` gives in `RoboPaintDump.h`: a line is a fixed-size buffer and a
 *  run's length is the document's to choose. */
#define ROBO_PAINT_DISPLAY_DUMP_TEXT_MAX 16u

/**
 * Write the whole of @p list into @p buffer.
 *
 * One line per command, in list order, `\n`-terminated including the last, and a NUL
 * after it. An empty list writes an empty buffer of length 0.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument; ROBO_NOT_SUPPORTED when the
 *         dump does not fit, in which case @p out_length receives the length it would
 *         have needed -- so a caller can size a buffer from the refusal.
 */
robo_result_t robo_paint_display_list_write(const robo_paint_display_list_t *list,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DISPLAY_DUMP_H */
