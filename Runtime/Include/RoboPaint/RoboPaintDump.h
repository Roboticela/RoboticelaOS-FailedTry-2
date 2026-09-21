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
 *   #0003   TEXT       node=7 depth=1 clip=none  border=1,5,80,16 color=rgb(0,0,0)
 *   codePoints=5 baseline=13 font=16.00/400/upright/ltr text="Hello"
 *   #0004 END_BOX      node=1 depth=0 clip=none  border=0,0,100,40
 *
 * The ordinal is a 4-digit zero-padded index so that the lines sort as the list does,
 * the kind is left-aligned in a fixed column so that a two-list diff lines up, and the
 * marker lines echo the border box because that is the rectangle a reader checks a
 * box's position against.
 *
 * Since 11.6 a text line carries one part no record's own writer can produce: the run's
 * **characters**, read out of the list's pool and appended ` text="..."` (the line above is
 * wrapped here to fit this comment; a real dump writes one line per operation). That is why
 * this door needs the list rather than only the record, and why the line's bound below is
 * larger than it was: the characters are the one payload in a paint list that is
 * variable-length.
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
 *   the kind, padded to `RoboPaintOpNameColumn` (14) plus its space     15
 *   `node=` plus ten digits                                                15
 *   ` depth=` plus ten digits                                              17
 *   ` clip=` plus two axes of `mode[start,end]`, where a mode is at most six
 *   characters and an `int32` at most eleven, plus a `/` and the brackets     72
 *   ` band=` plus four `int32` numbers and three commas                     52
 *   ` color=` plus `#rrggbbaa`                                             16
 *                                                                        ----
 *                                                                           187
 *
 * and, for a text record (11.6), the widest printed characters:
 *
 *   ` text="` plus `RoboPaintDumpTextMax` code points, each at most ten
 *   bytes (`\U0001F600`), plus `...` and the closing quote                    173
 *                                                                           ----
 *                                                                            360
 *
 * The bound is that plus a line's worth of headroom, so a caller sizing a buffer from
 * this gets a dump it does not have to retry -- and a future field that pushes the widest
 * line past it fails `paint.dump.line_bound`, which measures the widest line this build
 * can actually produce rather than trusting this paragraph.
 */
#define RoboPaintDumpLineMax 384u

/**
 * Code points of a run one dumped line prints before it stops.
 *
 * A **bound** and not a preference: a dump line is a fixed-size buffer, a run's length is the
 * document's to choose, and a bound the document could exceed is not a bound. A run longer
 * than this prints its first this-many code points and `...`, and the record's own part of the
 * line already says how many there were in total -- so a reader of a truncated run knows
 * exactly what they are looking at.
 */
#define RoboPaintDumpTextMax 16u

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
 * @param outLength non-NULL; receives the length written, excluding the NUL.
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when
 *         the dump does not fit, in which case @p outLength receives the length it
 *         would have needed -- so a caller can size a buffer from the refusal rather
 *         than from an estimate, which is the contract every `_write` door in this tree
 *         uses.
 */
Result PaintListWrite(const PaintList *list,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   PaintDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DUMP_H */
