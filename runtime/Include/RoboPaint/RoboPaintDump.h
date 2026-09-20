/*
 * RoboPaint dump (Phase 11.1).
 *
 * The developer's view of a paint list: one line per operation, indented by the depth
 * the operation carries, in the order the list holds them. It is a *text* door into a
 * buffer and nothing more, and the three things it deliberately is not are worth
 * stating, because each of them is the obvious thing to build and all three are out of
 * scope for an architecture release:
 *
 *   it is **not a renderer**. Nothing here decides a colour, resolves a rectangle or
 *   composites anything; it prints what the list already says, and a dump that
 *   disagreed with the record would be a second implementation of the record.
 *
 *   it is **not a tree**. The indentation is the operation's own `depth`, so a dump is
 *   what a *rasteriser* would see walking the sequence -- which is the view worth
 *   having when an operation is in the wrong place, and a nested print would hide
 *   exactly that by re-deriving the structure from the tree.
 *
 *   it is **not a console**. The runtime's guest target has no `stdio`, so there is no
 *   door that prints; a host caller writes the buffer with whatever it already has, and
 *   a suite asserts against the text.
 *
 * The format is fixed rather than configurable, so that a suite can assert on it and a
 * person can read a diff of two documents:
 *
 *   #0000 BEGIN_BOX    node=1 depth=0 clip=none  border=0,0,100,40
 *   #0001   BACKGROUND node=1 depth=0 clip=none  rect=0,0,100,40 color=rgb(255,0,0)
 *   #0002   BORDER_TOP node=1 depth=0 clip=none  band=0,0,100,5 color=rgb(0,0,0)
 *   #0003 END_BOX      node=1 depth=0 clip=none  border=0,0,100,40
 *
 * The ordinal is a 4-digit zero-padded index so that the lines sort as the list does,
 * the kind is left-aligned in a fixed column so that a two-list diff lines up, and the
 * marker lines echo the border box because that is the rectangle a reader checks a
 * box's position against.
 */
#ifndef ROBO_PAINT_DUMP_H
#define ROBO_PAINT_DUMP_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintList.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Characters one dumped operation line can need, excluding the NUL.
 *
 * A bound rather than a guess, and it is *derived* rather than chosen -- which is the only
 * way a bound stays true when a field is added to a line. The widest line is a border
 * side, and its parts at their widest printed forms are:
 *
 *   the kind, padded to `ROBO_PAINT_OP_NAME_COLUMN` (14) plus its space     15
 *   `node=` plus ten digits                                                15
 *   ` depth=` plus ten digits                                              17
 *   ` clip=` plus two axes of `mode[start,end]`, where a mode is at most six
 *   characters and an `int32` at most eleven, plus a `/` and the brackets     72
 *   ` band=` plus four `int32` numbers and three commas                     52
 *   ` color=` plus `#rrggbbaa`                                             16
 *                                                                        ----
 *                                                                           187
 *
 * The bound is that plus a line's worth of headroom, so a caller sizing a buffer from
 * this gets a dump it does not have to retry -- and a future field that pushes the widest
 * line past it fails `paint.dump.line_bound`, which measures the widest line this build
 * can actually produce rather than trusting this paragraph.
 */
#define ROBO_PAINT_DUMP_LINE_MAX 224u

/**
 * Write the whole of @p list into @p buffer.
 *
 * One line per operation, in list order, `\n`-terminated including the last, and a
 * NUL after it. An empty list writes an empty buffer of length 0, which is a
 * legitimate answer -- a document that draws nothing has an empty dump rather than a
 * placeholder line.
 *
 * @param list       the list to dump.
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument; ROBO_NOT_SUPPORTED when
 *         the dump does not fit, in which case @p out_length receives the length it
 *         would have needed -- so a caller can size a buffer from the refusal rather
 *         than from an estimate, which is the contract every `_write` door in this tree
 *         uses.
 */
robo_result_t robo_paint_list_write(const robo_paint_list_t *list,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *out_length,
                                   robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DUMP_H */
