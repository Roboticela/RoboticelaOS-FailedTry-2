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
 * RoboText supplies glyph IDs + placement later."* Since Phase 12.4 that is literally
 * what happens: RoboText supplies the ids and the placement, and this module draws them.
 *
 * The boundary this header predicted arrived in **Phase 12.4**, and it arrived exactly
 * where it was said it would:
 *
 *   11.6 (then)   text run -> the font's cmap, per code point, in logical order -> paint
 *   12.4 (now)    text run -> RoboText -> a glyph run -> paint
 *
 * The *pool* did not change, and that is the point of the design: the display list still
 * stores characters, because a run's characters are a statement about the **document**
 * while glyph ids are a statement about a font. What changed is who resolves them. The
 * glyph loop lives in `RoboPaintDecoration.c` and is now four lines that own no font
 * knowledge at all -- no cmap, no id, no advance, no way to disagree with the measurer,
 * which runs the same glyph generation. `glyphsMissing` is still this record's own
 * count, taken from RoboText's rather than from a lookup the painter performed.
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

/*
 * ## 12.16: the glyph run, and why the list carries both
 *
 * The paragraph below (and the header's own introduction) explains why this list stores **characters**
 * rather than glyph ids, and that argument was right for 11.6: a display list that held only glyph ids
 * would be a list only one font could draw. What 12.16 adds is the **other** half, because the same
 * argument cuts the other way once shaping arrives:
 *
 *     a glyph id is meaningless without the face that produced it, and shaping is expensive -- so it
 *     happens once, when the document is recorded, rather than again on every frame the list is
 *     replayed into a surface.
 *
 * So a text record now carries **both**:
 *
 *   the **characters** (`PaintTextSpan`), a statement about the document -- what a selection, a hit
 *   test and a copy will later read, and what makes the record meaningful independently of the face
 *   it will be drawn with;
 *   the **glyphs** (`PaintGlyphSpan`) and the **token** of the face they came from
 *   (`PaintFontToken`), the shaped result -- ids, placements and clusters, the same numbers RoboText
 *   produced -- so a replay draws them and never shapes, never looks up a `cmap`, and cannot disagree
 *   with what was measured.
 *
 * The token rather than a face pointer is what keeps every property the paragraphs above claim: a
 * record is still a value with no pointers in it, a list is still safe to copy and to rebuild, and "no
 * dangling pointer" is still true by construction. The face lives in a `PaintFontTable` the caller
 * owns, and a token that does not resolve is a **countable miss** rather than a face someone guessed at
 * (`RoboPaintFontTable.h`).
 *
 * A record whose glyph span is **empty** is a run recorded without a face. That is legal, and it is the
 * state 11.6's counter `noFont` names: the characters are there, nothing was shaped, and a replay says
 * so rather than drawing `.notdef` boxes it would have to invent a face to find.
 */

typedef enum PaintGlyphFlag {
    /** No flags: an ordinary mapped glyph. */
    PaintGlyphFlagNone = 0,
    /**
     * A bidi **formatting control** rather than a character (Phase 12.8): no id, no advance and
     * nothing drawn. Kept in the array because dropping it would make every later glyph's index
     * differ from the one the shaped run named, and because the source mapping is one-per-scalar
     * only while it is there.
     */
    PaintGlyphFlagControl = 1u << 0,
    /** A glyph several scalars were folded into (Phase 12.12): `sourceCount >= 2`. */
    PaintGlyphFlagLigature = 1u << 1,
    /** A combining mark (Phase 12.14): it may not be separated from the glyph before it. */
    PaintGlyphFlagMark = 1u << 2,
    /** The face had no glyph for the source scalars, or its metrics would not parse. */
    PaintGlyphFlagMissing = 1u << 3,
    /**
     * The glyph is drawn **sideways** -- rotated a quarter turn -- rather than standing on its own baseline
     * (Phase 12.20).
     *
     * The flag is **RoboText's** answer, carried rather than derived: `RoboTextGlyphRunOrientation` resolves it
     * per run from the three orientation tables, so an ideograph stands up and a Latin letter in the same
     * column lies on its side. The renderer never looks at a code point to decide, and the pen composes the
     * angle RoboText states (`RoboTextGlyphOrientationRotation`) into the rasterizer's own transform.
     *
     * It is a **glyph** flag rather than a run field because one record can hold several shaped runs: a
     * vertical line of `一 A B 丁` is three runs with two orientations, and a record-level answer could not
     * describe it. The parallel with the character half is exact -- the characters live in the record's span
     * and the presentation lives on the glyph -- and the flag survives into a display list's own glyph pool
     * because it is part of the record the builder copies.
     */
    PaintGlyphFlagSideways = 1u << 4
} PaintGlyphFlag;

/**
 * One shaped glyph, as the paint layer carries it (Phase 12.16).
 *
 * The record is a **copy of the shaped facts** rather than a pointer to a `RoboTextGlyph`: a shape
 * result is a temporary the recording pass destroys, and a display list that referenced it would be a
 * list whose lifetime was someone else's. So the four numbers a renderer needs travel by value -- the
 * id, the pen-relative placement, the two advances -- and so do the three facts a later selection
 * needs: which **cluster** the glyph belongs to and the source range it came from.
 *
 * `sourceFirst`/`sourceCount` are **relative to the run's own span**, never document positions: a
 * record whose source range named document offsets would have two coordinate spaces in one struct, and
 * the run's span sits right beside them for a caller that wants an absolute range.
 */
typedef struct PaintGlyph {
    /** The face's glyph id. Meaningful only together with the record's `fontToken`. */
    uint16_t glyphId;
    /** The cluster this glyph belongs to, as the run's own cluster index. */
    uint32_t cluster;
    /** First scalar of this glyph's source, relative to the run's span. */
    uint32_t sourceFirst;
    /** Scalars the glyph covers: 1 for an ordinary glyph, more for a ligature, 0 for a control. */
    uint32_t sourceCount;
    /** The advance the pen moves after this glyph, in px. Fractional, never rounded here. */
    float advanceX;
    /** See @ref advanceX: the vertical advance, zero for horizontal text. */
    float advanceY;
    /** Where the glyph sits relative to the pen, in px. Positioning is already in this number. */
    float offsetX;
    /** See @ref offsetX. */
    float offsetY;
    /** A bit word of @ref PaintGlyphFlag. */
    uint32_t flags;
} PaintGlyph;

/** Clear @p glyph to the null glyph: id 0, no source, zero vectors. Safe on NULL. */
void PaintGlyphInit(PaintGlyph *glyph);

/** Non-zero when @p glyph is a record this build may carry: finite vectors and a well-formed source.
 *  Safe on NULL. */
int PaintGlyphIsValid(const PaintGlyph *glyph);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int PaintGlyphEqual(const PaintGlyph *a, const PaintGlyph *b);

/** Non-zero when @p glyph carries the `control` flag. Safe on NULL. */
int PaintGlyphIsControl(const PaintGlyph *glyph);

/**
 * Non-zero when @p glyph is drawn sideways -- a quarter turn from the way its face states it (Phase 12.20).
 *
 * Safe on NULL, which answers 0: a glyph a caller does not have is not a glyph it should rotate.
 */
int PaintGlyphIsSideways(const PaintGlyph *glyph);

/**
 * A span of **glyphs** inside a paint list's glyph pool: the same two-number shape as
 * @ref PaintTextSpan, for the same reason.
 */
typedef struct PaintGlyphSpan {
    /** Index of the first glyph in the pool. */
    uint32_t first;
    /** How many glyphs the span covers. */
    uint32_t count;
} PaintGlyphSpan;

/** The empty span at @p at: a position in a pool rather than a run of it. */
PaintGlyphSpan PaintGlyphSpanAt(uint32_t first);

/** Non-zero when @p span covers nothing. */
int PaintGlyphSpanIsEmpty(PaintGlyphSpan span);

/**
 * The token that names the face a glyph run was shaped with (Phase 12.16).
 *
 * A number, never a pointer, so a record carrying one is still a value: the face lives in a
 * `PaintFontTable` the caller owns, and this is the key it registered the face under. Two sentinels and
 * no other special values.
 */
typedef uint32_t PaintFontToken;

/**
 * No face: this record's glyph span is empty and nothing was shaped.
 *
 * The state a pass given no face records, and the one a run's `fontToken` holds exactly when its glyph
 * span is empty -- the pair is an invariant rather than two fields a caller has to keep in step.
 */
#define PaintFontTokenNone ((PaintFontToken)0u)

/**
 * The **primary** face: the one the drawing pass itself was handed.
 *
 * The degenerate table, and the common case -- one document, one face, the recorder and the renderer
 * given the same object by the same caller. A record carrying this token is drawn with the pass's own
 * `face` argument, which keeps a caller with exactly one face from having to build a table to say so.
 */
#define PaintFontTokenPrimary ((PaintFontToken)1u)

/** Tokens a table assigns are at or above this: neither sentinel is ever assigned. */
#define PaintFontTokenFirstAssigned ((PaintFontToken)2u)

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
    /**
     * Where this run's shaped glyphs are in the list's glyph pool (Phase 12.16).
     *
     * **Empty when the run was recorded without a face** -- the characters are here, nothing was shaped,
     * and a replay counts the run `noFont` rather than drawing boxes it would have to invent a face to
     * find. Non-empty means every glyph the run draws is in this span, in the run's own logical order, at
     * the positions the shaper produced.
     */
    PaintGlyphSpan glyphs;
    /**
     * The face the glyphs came from, by token (Phase 12.16).
     *
     * `PaintFontTokenNone` **exactly when** @ref glyphs is empty: a pair of fields carrying one fact would
     * be two facts that can disagree, so the invariant is stated and checked rather than assumed. A token
     * that does not resolve in the drawing pass's own table is a counted miss and not a fallback to some
     * other face -- a glyph id drawn with the wrong face is a different letter, not a wrong colour.
     */
    PaintFontToken fontToken;
} PaintTextRun;

/** Clear @p run to an empty span, no baseline and the CSS initial font inputs. Safe on
 *  NULL. */
void PaintTextRunInit(PaintTextRun *run);

/**
 * Non-zero when @p run is a record this build may carry.
 *
 * Four conditions, and each refuses a shape that would travel a stage too far:
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
 *   selection is a later phase's -- so they are carried as they were resolved;
 *   and the fourth (12.16): **the glyph span and the font token agree**. Either the span is empty and the
 *   token is `PaintFontTokenNone`, or the span is non-empty and the token is one this build can name.
 *   Glyphs without a face are ids a caller would have to guess a font for, and a face without glyphs
 *   is a claim about a run that has nothing to draw -- both are refused here rather than sorted out
 *   one layer down. The span's *bounds* against a pool are checked by the container that owns the pool
 *   (`PaintListAppend`, `PaintDisplayListAppend`), because only it knows what its pool holds.
 */
int PaintTextRunIsValid(const PaintTextRun *run);

/** Field-by-field equality of the span, the offset and all four font inputs. Safe on
 *  NULL (two NULLs are equal). */
int PaintTextRunEqual(const PaintTextRun *a,
                              const PaintTextRun *b);

/** The span (an empty span for NULL). */
PaintTextSpan PaintTextRunSpan(const PaintTextRun *run);

/** The glyph span (an empty span for NULL): where this run's shaped glyphs are. */
PaintGlyphSpan PaintTextRunGlyphs(const PaintTextRun *run);

/** The face token the glyphs came from (`PaintFontTokenNone` for NULL, which is the empty-glyph state). */
PaintFontToken PaintTextRunFontToken(const PaintTextRun *run);

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

/* ------------------------------------------------------------------ */
/* The glyph pool                                                      */
/* ------------------------------------------------------------------ */

/**
 * The shaped glyphs a paint list carries, in one contiguous array it owns (Phase 12.16).
 *
 * The **same shape as the text pool** and for the same reasons -- contiguous, owned, cleared in place,
 * grown on demand, read through a door that validates the span against the pool rather than trusting it --
 * with the span arithmetic written **once** -- both pools live in `RoboPaintText.c`, and the wrap-safe
 * containment test is a single static function they share, because a second copy of *that* is precisely the
 * kind of thing that drifts. It is a second *type* and not a second implementation.
 *
 * One element is a `PaintGlyph` rather than a `uint32_t`, so the pool cannot be generic without giving up
 * the element type; what must not be duplicated is the arithmetic, and it is not.
 */
typedef struct PaintGlyphPool PaintGlyphPool;

/** Create an empty glyph pool. @return RoboOk; RoboInvalidArgument for NULL @p outPool; RoboOutOfMemory. */
Result PaintGlyphPoolCreate(PaintGlyphPool **outPool,
                            PaintDiag *diag);

/** Destroy @p pool and its glyphs. Safe on NULL. */
void PaintGlyphPoolDestroy(PaintGlyphPool *pool);

/** Drop every glyph, keeping the capacity. Safe on NULL. */
void PaintGlyphPoolClear(PaintGlyphPool *pool);

/**
 * Append @p count glyphs and hand back the span that names them.
 *
 * The one way glyphs enter a pool, so the span a record carries is always one the pool produced. A
 * @p count of zero is **refused** -- it would return an empty span, which is the "no face" state rather
 * than a run of glyphs, and the sentinel `PaintGlyphSpanAt` already spells that.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments, a NULL @p glyphs with a non-zero count, a count
 *         of zero, or a glyph that is not a valid record; RoboOutOfMemory.
 */
Result PaintGlyphPoolAppend(PaintGlyphPool *pool,
                            const PaintGlyph *glyphs,
                            size_t count,
                            PaintGlyphSpan *outSpan,
                            PaintDiag *diag);

/**
 * Copy a run from @p source into @p pool, handing back the span that names it here.
 *
 * The one door between two pools, and the builder's whole use of it: a display list owns its own glyphs,
 * so every span in the output has to be renumbered into the output's pool.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a @p span the source does not contain;
 *         RoboOutOfMemory.
 */
Result PaintGlyphPoolCopy(PaintGlyphPool *pool,
                          const PaintGlyphPool *source,
                          PaintGlyphSpan span,
                          PaintGlyphSpan *outSpan,
                          PaintDiag *diag);

/** Glyphs in @p pool (0 for NULL). */
size_t PaintGlyphPoolCount(const PaintGlyphPool *pool);

/** Glyphs @p pool can hold before growing (0 for NULL). */
size_t PaintGlyphPoolCapacity(const PaintGlyphPool *pool);

/** The glyphs, borrowed and contiguous, or NULL when the pool is empty. Valid until the next append.
 *  Nothing points into it. */
const PaintGlyph *PaintGlyphPoolGlyphs(const PaintGlyphPool *pool);

/** Non-zero when @p span lies inside @p pool. */
int PaintGlyphPoolContains(const PaintGlyphPool *pool,
                           PaintGlyphSpan span);

/**
 * The glyphs @p span covers, by value, into @p out.
 *
 * @param outCapacity glyphs @p out can hold.
 * @param outCount    non-NULL; receives the glyphs written.
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a @p span the pool does not contain;
 *         RoboNotSupported when @p outCapacity is too small, in which case @p outCount receives the count
 *         it would have needed.
 */
Result PaintGlyphPoolRead(const PaintGlyphPool *pool,
                          PaintGlyphSpan span,
                          PaintGlyph *out,
                          size_t outCapacity,
                          size_t *outCount,
                          PaintDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_TEXT_H */
