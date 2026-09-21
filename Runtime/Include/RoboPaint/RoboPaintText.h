/*
 * RoboPaint text vocabulary (Phase 11.6).
 *
 * The first thing this module carries that is **not a number**, and the whole of this
 * header exists because of what that costs.
 *
 * Everything above it is a rectangle and a colour. A drawing operation is a value record
 * with no pointers in it (`RoboPaintOperation.h`), which is what makes a paint list safe
 * to copy, to discard and to rebuild: there is nothing in an operation that can dangle or
 * be freed under it. Text breaks that shape in one specific way -- a run's characters are
 * **variable-length data**, and no fixed struct can hold them -- so this phase does the
 * one thing that keeps every property the earlier phases established while adding the
 * payload:
 *
 *   the characters live in a **pool**, the pool is owned by exactly one list, and a
 *   record carries a **span** into it rather than a pointer to anything.
 *
 * A span is two numbers (`first`, `count`), so a text operation is still a value record
 * with no pointers in it, a list is still safe to copy and to rebuild, and "no dangling
 * pointer" is still true by construction rather than by discipline. The pool is the
 * second array a list grows, and it is the same shape as the first: contiguous, owned,
 * cleared in place, grown on demand, and read through a door that validates the span
 * against the pool rather than trusting it.
 *
 * **Code points, not bytes and not glyphs.** Code points are the unit RoboLayout's own
 * ranges count in (`RoboLayoutText.h` argues it: a byte offset is not a character
 * position the moment a document contains an accent, an Arabic letter or an emoji), so
 * the unit a run is measured in and the unit it is painted in are the same one. And they
 * are deliberately **not glyph ids**, which is the more "render-ready" choice and the
 * wrong one: a glyph id is a fact about a *font*, so a list that stored them would be a
 * list only that font could draw, and the painter's face and the traversal's face would
 * be two faces nothing could check for agreement. Storing characters keeps the display
 * list a statement about the **document**, independent of the font it will be drawn with,
 * exactly as a background command is independent of the surface it will be filled into --
 * and it puts glyph resolution where RoboGraphics' own bridge says it belongs, which is
 * at the drawing step: *"RoboGraphics does not shape, segment, bidi, or fall back fonts.
 * RoboText supplies glyph IDs + placement later."*
 *
 * The boundary that leaves for Phase 14 is therefore explicit and already the right
 * shape:
 *
 *   this build    text run -> the font's cmap, per code point, in logical order -> paint
 *   Phase 14      text run -> RoboText shaping -> a glyph run -> paint
 *
 * and the record a shaper will fill in is this one, with `glyphsMissing` becoming a
 * shaping result instead of a cmap miss.
 *
 * What is deliberately absent: shaping (GlyphSubstitution/GlyphPositioning, ligatures,
 * kerning), the bidi algorithm, line breaking, whitespace collapsing, font fallback,
 * `text-decoration`, `text-shadow` and any text effect. The runs this module paints are
 * the runs RoboLayout measured, in logical order, at the advances the font's own `hmtx`
 * gives -- which is CSS's *advance width* of a run before layout reorders it, named in
 * `RoboLayoutTextFont.h` and restated here because a painter is where the difference
 * shows.
 */
#ifndef ROBO_PAINT_TEXT_H
#define ROBO_PAINT_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutText.h"

#include "RoboPaint/RoboPaintDiagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A span of **code points** inside a paint list's text pool.
 *
 * `first` is an index into the pool and `count` is a length, so the span names
 * `[first, first + count)` -- half-open, like every span in this tree. It is a value
 * record: it names a place in a container, and it is meaningless apart from one, which
 * is why the doors that consume one take the pool too and **refuse** a span that runs
 * past the end rather than reading whatever is there.
 */
typedef struct PaintTextSpan {
    /** Index of the first code point in the pool. */
    uint32_t first;
    /** How many code points the span covers. */
    uint32_t count;
} PaintTextSpan;

/** The empty span at @p at: a position in a pool rather than a run of it. */
PaintTextSpan PaintTextSpanAt(uint32_t first);

/** Non-zero when @p span covers nothing. */
int PaintTextSpanIsEmpty(PaintTextSpan span);

/**
 * The text facts of one text operation (and of the display command it becomes).
 *
 * Deliberately a **sub-record** rather than six more fields on the operation, for the
 * reason `PaintBoxes` is one: the facts a text drawing has and a fill does not
 * arrive together, so a record cannot carry half of them. A background's `text` member is
 * a cleared one, exactly as a text run's `style` member is `NONE`.
 *
 * `baselineOffset` is the **one number that says where the text sits**, and it is an
 * offset rather than a coordinate on purpose. The phase could have carried an absolute
 * baseline y in paint coordinates -- and would then have had two spaces to keep in
 * agreement, which is precisely the redundancy 11.1 resolved with an assertion
 * (`RoboPaintOperation.h`). Offset-from-the-rect's-top is a single number in one space:
 * the baseline is at `rect.y + baselineOffset` in paint coordinates and at
 * `baselineOffset` relative to the run's own top, and there is nothing to drift. It is
 * the distance RoboLayout's baseline placement produced -- a text run's own ascent, so a
 * run's top *is* its baseline minus its ascent -- and it is **read, never recomputed**
 * (10.7 placed it; a painter that derived it would be a second baseline model).
 *
 * `font` is the resolved text style the run was **measured with**, by value
 * (`RoboLayoutText.h`): a pixel size, a numeric weight, an italic flag and a direction.
 * Carrying the measurer's own record rather than the four numbers re-derived here is what
 * makes "the size it was measured at is the size it is drawn at" a copy rather than an
 * agreement to maintain. `direction` travels for the shaper that will read it; this build
 * draws a run in logical order and counts the RTL ones it drew that way.
 */
typedef struct PaintTextRun {
    /** Where the characters are in the pool that owns them. */
    PaintTextSpan span;
    /** Distance from the run's top to its baseline, in px; never negative. */
    int32_t baselineOffset;
    /** The font inputs the run was measured with. */
    LayoutTextStyle font;
} PaintTextRun;

/** Clear @p run to an empty span, no baseline and the CSS initial font inputs. Safe on
 *  NULL. */
void PaintTextRunInit(PaintTextRun *run);

/**
 * Non-zero when @p run is a record this build may carry.
 *
 * Three conditions, and each refuses a shape that would travel a stage too far:
 *
 *   a **non-empty span**. A text drawing with no characters is not a drawing, and an
 *   operation carrying one would be a command whose consumer does nothing -- so it is
 *   refused here rather than skipped later, which is the same rule the colour rule states
 *   for a fill with no colour;
 *   a **non-negative baseline offset**, because a baseline above the run's own top is not
 *   a placement this build's baseline model can produce -- an offset that came out
 *   negative is arithmetic that went wrong one level down, not an interesting case;
 *   and **font inputs that are a record**: a finite, positive size in pixels, and a
 *   direction that is one of the two. The weight and the italic flag are not bounded here
 *   -- CSS allows a numeric weight outside the nine-value scale and RoboFont's own
 *   selection is a later phase's -- so they are carried as they were resolved.
 */
int PaintTextRunIsValid(const PaintTextRun *run);

/** Field-by-field equality of the span, the offset and all four font inputs. Safe on
 *  NULL (two NULLs are equal). */
int PaintTextRunEqual(const PaintTextRun *a,
                              const PaintTextRun *b);

/** The span (an empty span for NULL). */
PaintTextSpan PaintTextRunSpan(const PaintTextRun *run);

/** The baseline offset, in px (0 for NULL). */
int32_t PaintTextRunBaselineOffset(const PaintTextRun *run);

/** The font inputs, by value. The CSS initial inputs for NULL -- a stated answer rather
 *  than a zeroed record that reads as "no font". */
LayoutTextStyle PaintTextRunFont(const PaintTextRun *run);

/* ------------------------------------------------------------------ */
/* The pool                                                            */
/* ------------------------------------------------------------------ */

/**
 * The code points a paint list carries, in one contiguous array it owns.
 *
 * **One implementation and two owners**, which is why it is a type of its own rather than
 * a pair of arrays inside the paint list: a display list owns one too (`the builder copies
 * each span across`), and two copies of a growable code-point array would be two places
 * for the same span arithmetic to go wrong. It is opaque because the rule it enforces --
 * a span is valid against *this* pool or it is not -- is only checkable from inside.
 *
 * It owns one allocation and borrows nothing. `clear` keeps the capacity, for the same
 * reason the operation list's does: a renderer that repaints per frame reuses the storage
 * and never the characters.
 */
typedef struct PaintTextPool PaintTextPool;

/** Create an empty pool. @return RoboOk; RoboInvalidArgument for NULL @p outPool;
 *  RoboOutOfMemory. */
Result PaintTextPoolCreate(PaintTextPool **outPool,
                                          PaintDiag *diag);

/** Destroy @p pool and its code points. Safe on NULL. */
void PaintTextPoolDestroy(PaintTextPool *pool);

/** Drop every code point, keeping the capacity. Safe on NULL. */
void PaintTextPoolClear(PaintTextPool *pool);

/**
 * Append @p count code points and hand back the span that names them.
 *
 * The one way characters enter a pool, so the span a record carries is always one the
 * pool produced. A @p count of zero is **refused**: it would return an empty span, which
 * is a position rather than a run, and no record may carry one
 * (`PaintTextRunIsValid`). @p codePoints may not be NULL when @p count is
 * above zero.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, a NULL @p codePoints with a
 *         non-zero count, or a count of zero; RoboOutOfMemory.
 */
Result PaintTextPoolAppend(PaintTextPool *pool,
                                         const uint32_t *codePoints,
                                         size_t count,
                                         PaintTextSpan *outSpan,
                                         PaintDiag *diag);

/**
 * Copy a run from @p source into @p pool, handing back the span that names it here.
 *
 * The one door between two pools, and it exists because the display list's builder needs
 * exactly this and nothing else: it converts a record list into a display list, so every
 * span in the output has to be renumbered into the output's own pool -- and doing that
 * through `read` would need a temporary buffer sized to the longest run in the document
 * for no reason, since the bytes only have to move from one owned array to another.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a @p span the **source**
 *         does not contain; RoboOutOfMemory.
 */
Result PaintTextPoolCopy(PaintTextPool *pool,
                                       const PaintTextPool *source,
                                       PaintTextSpan span,
                                       PaintTextSpan *outSpan,
                                       PaintDiag *diag);

/** Code points in @p pool (0 for NULL). */
size_t PaintTextPoolCount(const PaintTextPool *pool);

/** Code points @p pool can hold before growing (0 for NULL). */
size_t PaintTextPoolCapacity(const PaintTextPool *pool);

/** The code points, borrowed and contiguous, or NULL when the pool is empty. Valid until
 *  the next append. Nothing points into it. */
const uint32_t *PaintTextPoolCodePoints(const PaintTextPool *pool);

/**
 * Non-zero when @p span lies inside @p pool.
 *
 * The rule a consumer's span must pass, and it is asked **here** rather than trusted --
 * the same argument `RoboPaintList.h` makes for validating an operation at the container
 * rather than at every call site: a span is two numbers that can name anything, and the
 * only thing that knows whether they name characters is the pool that holds them.
 */
int PaintTextPoolContains(const PaintTextPool *pool,
                                  PaintTextSpan span);

/**
 * The code points @p span covers, by value, into @p out.
 *
 * The read door, and the reason a consumer never touches the array directly: the span is
 * validated first, so a run's characters are either all there or the call reports a miss.
 *
 * @param outCapacity code points @p out can hold.
 * @param outCount    non-NULL; receives the code points written.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a @p span the pool does
 *         not contain; RoboNotSupported when @p outCapacity is too small, in which
 *         case @p outCount receives the count it would have needed.
 */
Result PaintTextPoolRead(const PaintTextPool *pool,
                                       PaintTextSpan span,
                                       uint32_t *out,
                                       size_t outCapacity,
                                       size_t *outCount,
                                       PaintDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_TEXT_H */
