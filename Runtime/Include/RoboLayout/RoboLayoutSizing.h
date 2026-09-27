/*
 * RoboLayout sizing (Phase 10.8).
 *
 * How big a box is: what its declarations asked for, what its containing block
 * offers, what its contents turned out to be, and the one arithmetic that turns
 * those three into the `size` a result carries. The phase is the one where the
 * `width` RoboCSS has parsed since 9.11 finally *means* something, and where the
 * `height` 10.3 could already read stops being read by 10.3.
 *
 * Four decisions, each of which a later sizing goal will be glad it does not have
 * to re-litigate.
 *
 * **A dimension is three kinds, not a number with a flag.** `auto` is a real CSS
 * value with a defined meaning ("the containing block and the contents decide"),
 * and this build has a *second* way for a declaration to determine no length: a
 * value it cannot resolve -- `50%`, `2em` -- because there is no containing block
 * dimension or font to resolve it against here. Those are different facts about a
 * document. `RoboLayoutDimensionAuto` is the first, `UNRESOLVED` is the second,
 * and they resolve *identically* -- which is exactly why they must not be stored
 * identically: the resolution rule will change for one of them long before it
 * changes for the other, and a build that had merged them could not tell which
 * declaration it was changing. `DEFINITE` is a px length this build can use,
 * which is the whole of what it can use (9.8's computed rule normalises an
 * *absolute* unit to px, so `1in` arrives as `96`).
 *
 * **The record carries all ten numbers the phase is about, and reads like the box
 * model.** Available, specified, resolved, and the four bounds, each with whether
 * it is known at all -- because "nobody has told me the available width" and "the
 * available width is zero" are different inputs and a `0` that meant both would
 * be the same silent wrong answer 10.1 refused for `available` in the constraints
 * record.
 *
 * **Two dimensions resolve separately; `LayoutSizingResolve` is the pair.**
 * A block box's width does not depend on its height, so it resolves both at once.
 * An atomic inline's *does*: its `auto` height is the block extent of its own
 * contents, and its contents have to be laid out *at a width*, so the width must
 * be settled before the height is asked for. Making the pair the only door would
 * have forced that caller to either lie about the content it does not have yet or
 * resolve twice and throw one answer away.
 *
 * **The bounds arithmetic is 10.1's, not a second copy.** `RoboLayoutConstraints.h`
 * states that "min wins over max" is decided once, in
 * `LayoutConstraintsClamp`, and that goal reached this one with its promise
 * intact: the two resolvers below fill a constraints record from their own bounds
 * and call it. What this header decides is only *what* bounds a box has and what
 * happens when they contradict each other, which is to report it
 * (`RoboNotFound`) rather than pick a size nobody can defend.
 *
 * Where the bounds *come from*, since RoboCSS registers no `min-width` yet: the
 * block pass reads `min-width`/`max-width`/`min-height`/`max-height` through the
 * same seam it reads `width`/`height` through, and a property the property table
 * does not know simply has no entry and leaves its bound unset. So the clamping
 * path is **live and tested** -- the suite drives it through this record
 * directly -- and the day 9.17 or 10.9 registers those properties, boxes start
 * obeying them with no change to either pass.
 *
 * What is deliberately absent, and each a named later goal: percentages, because
 * they need a containing block dimension that is *known* rather than merely
 * offered (`UNRESOLVED` is where they wait); intrinsic sizing -- `min-content`,
 * `max-content`, `fit-content`, shrink-to-fit; `box-sizing`; margins and padding,
 * which is why every size here is stated as the border box the result already
 * documents; and flex/grid/table sizing. A build that grew those here would give
 * every later goal two places to look.
 *
 * The one thing this header does **not** change: `RoboLayoutResult.h`'s statement
 * that `size` is the border-box size. Sizing decides what that number is; it does
 * not redefine it.
 */

#ifndef ROBO_LAYOUT_SIZING_H
#define ROBO_LAYOUT_SIZING_H

#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboCSS/RoboCSSComputed.h"

#include "RoboLayout/RoboLayoutConstraints.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutIntrinsic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* One dimension                                                       */
/* ------------------------------------------------------------------ */

/**
 * What a declaration determined about one dimension.
 *
 * The order is the order of the question: nothing usable, a usable length, and
 * "declared, and not resolvable here" -- which is deliberately last rather than
 * next to `AUTO`, because the two are not neighbours in meaning even though they
 * resolve the same way today.
 */
typedef enum LayoutDimensionKind {
    /**
     * The declaration determines no length: no entry at all, or the keyword
     * `auto`. CSS's initial value for `width` and `height` is `auto`, so an
     * element nothing declared one for *is* `auto` rather than a separate case,
     * and the two are one kind here for the same reason they are one value there.
     */
    RoboLayoutDimensionAuto = 0,
    /** A px length this build can use, and nothing else is usable (9.8 normalises
     *  an absolute unit to px, so `1in` arrives as `96`). */
    RoboLayoutDimensionDefinite = 1,
    /**
     * Declared, and not resolvable in this build: a percentage, or a length in a
     * relative unit. Distinct from `AUTO` because it is a *statement about the
     * document* -- somebody asked for something this build cannot honour -- and
     * `AUTO` is a statement about CSS's default. They resolve the same way today;
     * they will not when percentages arrive.
     */
    RoboLayoutDimensionUnresolved = 2,
    /**
     * An intrinsic request -- `min-content`, `max-content` or `fit-content` (19.1).
     *
     * A fourth kind rather than another `UNRESOLVED`, because the two say different
     * things: `UNRESOLVED` is "declared, and not resolvable in this build" and an
     * intrinsic request *is* resolvable -- from the box's contents, measured by
     * `RoboLayoutIntrinsic.h`. It resolves from the contents' intrinsic size when a
     * caller has recorded one, and degrades to the available width (exactly as `AUTO`
     * does) when it has not, which is the same honest degradation 10.5 documents for a
     * pass with no metrics source.
     */
    RoboLayoutDimensionIntrinsic = 3
} LayoutDimensionKind;

/** Number of defined dimension kinds. */
#define RoboLayoutDimensionKindCount 4u

/** Non-zero when @p kind is one of the three above. */
int LayoutDimensionKindIsKnown(LayoutDimensionKind kind);

/** Name of @p kind; never NULL; "unknown" when undefined. */
const char *LayoutDimensionKindName(LayoutDimensionKind kind);

/**
 * What one dimension was *declared* to be.
 *
 * A record by value and owned by nobody, like every other model record here: no
 * allocation, safe to copy and to put on the stack. `length` is meaningful only
 * when `kind` is `DEFINITE` -- reading it in the other two states is reading a
 * field that means nothing, which is why `LayoutDimensionLength` reports
 * rather than plain returns.
 *
 * `length` is deliberately a *pixel* quantity rather than a `double`: by the time
 * a declaration is this record it has already been through 9.10's unit system and
 * been rounded, and a sizing layer that kept the sub-pixel value would be a
 * second place for a rounding decision to live.
 */
typedef struct LayoutDimension {
    LayoutDimensionKind kind;
    /** The px length, when @c kind is DEFINITE. Undefined otherwise. */
    GfxPixelX length;
    /** Which intrinsic width was asked for, when @c kind is INTRINSIC. Undefined
     *  otherwise; `NONE` in a cleared record. */
    LayoutIntrinsicKind intrinsicKind;
} LayoutDimension;

/** Clear @p dimension to `AUTO` -- "nothing determines this". Safe on NULL. */
void LayoutDimensionInit(LayoutDimension *dimension);

/** The `auto` dimension. A maker rather than a setter, because there is nothing
 *  to set: this is the cleared state. */
LayoutDimension LayoutDimensionAuto(void);

/** A `DEFINITE` dimension of @p length px. Does **not** validate: a caller that
 *  built one from a computed style has already checked, and the door that stores
 *  one refuses an invalid record (see `LayoutSizingSetWidth`). Negative
 *  lengths are refused *there* rather than silently clamped here. */
LayoutDimension LayoutDimensionDefinite(GfxPixelX length);

/** The `UNRESOLVED` dimension: declared, and not resolvable in this build. */
LayoutDimension LayoutDimensionUnresolved(void);

/** An `INTRINSIC` dimension: `min-content`, `max-content` or `fit-content` (19.1).
 *  A `NONE` @p intrinsicKind is refused by nothing here -- it is a maker, not a door
 *  -- but a dimension built with it is not valid (see `LayoutDimensionIsValid`). */
LayoutDimension LayoutDimensionIntrinsic(LayoutIntrinsicKind intrinsicKind);

/** Non-zero when @p dimension is `auto`. 0 for NULL. */
int LayoutDimensionIsAuto(const LayoutDimension *dimension);
/** Non-zero when @p dimension is `DEFINITE`. 0 for NULL. */
int LayoutDimensionIsDefinite(const LayoutDimension *dimension);
/** Non-zero when @p dimension is `UNRESOLVED`. 0 for NULL. */
int LayoutDimensionIsUnresolved(const LayoutDimension *dimension);
/** Non-zero when @p dimension is `INTRINSIC`. 0 for NULL. */
int LayoutDimensionIsIntrinsic(const LayoutDimension *dimension);
/** The intrinsic kind of an `INTRINSIC` @p dimension, or `RoboLayoutIntrinsicNone`
 *  for NULL or a dimension of another kind. */
LayoutIntrinsicKind LayoutDimensionIntrinsicKindOf(const LayoutDimension *dimension);

/**
 * Non-zero when @p dimension is internally consistent: a `DEFINITE` one has a
 * non-negative length.
 *
 * A predicate rather than a repair, for the reason 10.1's constraints record gives
 * for refusing a negative size: nothing in layout can mean anything by a negative
 * width, and a build that clamped it here would be hiding a caller bug at the one
 * moment it is cheap to see.
 */
int LayoutDimensionIsValid(const LayoutDimension *dimension);

/**
 * The length of a `DEFINITE` @p dimension.
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         @p dimension is not `DEFINITE` (@p out is untouched) -- a different
 *         answer from a length of zero, which is a box an author asked to be
 *         empty and is why this does not simply return a number.
 */
Result LayoutDimensionLength(const LayoutDimension *dimension,
                                           GfxPixelX *out);

/** Field-by-field equality. Safe on NULL. */
int LayoutDimensionEqual(const LayoutDimension *a,
                                const LayoutDimension *b);

/* ------------------------------------------------------------------ */
/* Where a resolved number came from                                   */
/* ------------------------------------------------------------------ */

/**
 * How a resolved dimension was arrived at.
 *
 * Recorded beside the number rather than inferred from it, because the whole point
 * of a sizing layer is that the same number can be the answer to three different
 * questions: a `width: 300px` and an `auto` width inside a 300px containing block
 * both resolve to 300, and a caller reading only the size cannot tell whether the
 * box obeyed its author or happened to fill what it was given. The stats records
 * count the same distinction in aggregate; this is the per-box form.
 */
typedef enum LayoutDimensionSource {
    /** Nothing has been resolved. */
    RoboLayoutDimensionSourceNone = 0,
    /** A declared px length this build could use. */
    RoboLayoutDimensionSourceDeclared = 1,
    /** An `auto` (or unresolvable) width, filled from the containing block. */
    RoboLayoutDimensionSourceAvailable = 2,
    /** An `auto` (or unresolvable) height, taken from the contents' extent. */
    RoboLayoutDimensionSourceContent = 3,
    /** An intrinsic `width` (`min-content`/`max-content`/`fit-content`), taken
     *  from the subtree's measured intrinsic size (19.1). */
    RoboLayoutDimensionSourceIntrinsic = 4
} LayoutDimensionSource;

/** Number of defined sources. */
#define RoboLayoutDimensionSourceCount 5u

/** Non-zero when @p source is one of the four above. */
int LayoutDimensionSourceIsKnown(LayoutDimensionSource source);

/** Name of @p source; never NULL; "unknown" when undefined. */
const char *LayoutDimensionSourceName(LayoutDimensionSource source);

/** Field-by-field equality for two sources. */
int LayoutDimensionSourceEqual(LayoutDimensionSource a,
                                       LayoutDimensionSource b);

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * One box's sizing: the question and the answer.
 *
 * A record by value, owned by nobody, and — like the constraints record it borrows
 * its bounds arithmetic from — *not* a cache: it is filled by whoever has the
 * inputs, resolved on demand, and discarded with the stack frame that made it.
 * Nothing in this module holds one between passes, which is what keeps the pass a
 * pure function of the tree (RoboLayoutBlock.c says so) and what makes a second
 * resolve over a changed input produce a changed answer rather than a stale one.
 *
 * `available` is the containing block's **content** box, which today is the same
 * number as its border box because padding and borders are not applied (10.10,
 * 10.11). Its two flags are separate for the reason the whole header argues: a
 * block box is given a width and no height at all — a block's height is its
 * contents', never its containing block's — so "the available height is zero"
 * would be a lie a single flag could not avoid telling.
 */
typedef struct LayoutSizing {
    /** What the containing block offers, valid per the two flags below. */
    LayoutSize available;
    int hasAvailableWidth;
    int hasAvailableHeight;

    /** What the declarations asked for. */
    LayoutDimension width;
    LayoutDimension height;

    /** What the bounds allow, when anybody has said. RoboCSS registers no
     *  `min-width` yet, so these stay unset for every document this build can
     *  parse — and the path is live all the same (see the header comment). */
    LayoutSize minimum;
    int hasMinimum;
    LayoutSize maximum;
    int hasMaximum;

    /** What the contents turned out to be: the `auto` height's answer. */
    LayoutSize content;
    int hasContent;

    /**
     * The subtree's intrinsic size, when a caller has measured one (19.1).
     *
     * The input an `INTRINSIC` dimension resolves from. It is *set by the caller* and
     * not computed here, because measuring the subtree needs the metrics seam and this
     * record has no tree in it -- exactly as `content` is set by whoever laid the
     * children out. Unset means "nobody measured this", and an `INTRINSIC` width then
     * degrades to the available width, which is the same answer `AUTO` gives.
     */
    LayoutIntrinsicSize intrinsic;
    int hasIntrinsic;

    /* ---- the answer ---- */

    /** The resolved border-box size. Meaningful once both dimensions have been
     *  resolved; `LayoutSizingResolved` reports rather than plain
     *  returns. */
    LayoutSize resolved;
    /** How each dimension was arrived at. */
    LayoutDimensionSource widthSource;
    LayoutDimensionSource heightSource;
    /** Non-zero when a bound moved the number the resolver would have used.
     *  Recorded rather than left to a comparison against the original, because
     *  the original is not in the record any more than it is in the result. */
    int widthClamped;
    int heightClamped;
} LayoutSizing;

/** Clear @p sizing: both dimensions `auto`, every value-flag clear. Safe on
 *  NULL. A cleared record is a *legal* one -- it describes a box nothing has been
 *  said about -- which is the same choice 10.1 made for the constraints. */
void LayoutSizingInit(LayoutSizing *sizing);

/** Same as init. Safe on NULL. */
void LayoutSizingClear(LayoutSizing *sizing);

/* ---- the containing block ---- */

/**
 * Record the space the containing block offers in one direction.
 * @return RoboOk; RoboInvalidArgument for NULL @p sizing or a negative
 *         @p extent. A zero extent is a *known* zero, which is why it is accepted
 *         while a negative one is not.
 */
Result LayoutSizingSetAvailableWidth(LayoutSizing *sizing,
                                                     GfxPixelX extent,
                                                     LayoutDiag *diag);
Result LayoutSizingSetAvailableHeight(LayoutSizing *sizing,
                                                      GfxPixelY extent,
                                                      LayoutDiag *diag);

/** Non-zero when that direction's availability has been recorded. 0 for NULL. */
int LayoutSizingHasAvailableWidth(const LayoutSizing *sizing);
int LayoutSizingHasAvailableHeight(const LayoutSizing *sizing);

/**
 * Read a recorded available extent.
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         that direction was never set.
 */
Result LayoutSizingAvailableWidth(const LayoutSizing *sizing,
                                                 GfxPixelX *out);
Result LayoutSizingAvailableHeight(const LayoutSizing *sizing,
                                                  GfxPixelY *out);

/* ---- what was declared ---- */

/**
 * Record a declared dimension.
 * @return RoboOk; RoboInvalidArgument for NULL @p sizing, or a @p dimension
 *         that is not valid (a negative length) -- refused here rather than
 *         clamped, because a negative width is a caller bug and the one moment it
 *         is cheap to see is this one.
 */
Result LayoutSizingSetWidth(LayoutSizing *sizing,
                                           LayoutDimension dimension,
                                           LayoutDiag *diag);
Result LayoutSizingSetHeight(LayoutSizing *sizing,
                                            LayoutDimension dimension,
                                            LayoutDiag *diag);

/** The declared dimension. `auto` for NULL, which is the same fact the cleared
 *  record carries rather than a second answer. */
LayoutDimension LayoutSizingWidth(const LayoutSizing *sizing);
LayoutDimension LayoutSizingHeight(const LayoutSizing *sizing);

/* ---- the bounds ---- */

/** Record a bound. Same contract as `LayoutConstraintsSetMinimum`:
 *  RoboOk, or RoboInvalidArgument for NULL @p sizing or a negative @p size. */
Result LayoutSizingSetMinimum(LayoutSizing *sizing,
                                             LayoutSize size,
                                             LayoutDiag *diag);
Result LayoutSizingSetMaximum(LayoutSizing *sizing,
                                             LayoutSize size,
                                             LayoutDiag *diag);

/** Non-zero when that bound has been recorded. 0 for NULL. */
int LayoutSizingHasMinimum(const LayoutSizing *sizing);
int LayoutSizingHasMaximum(const LayoutSizing *sizing);

/** Read a recorded bound. @return RoboOk; RoboInvalidArgument for NULL
 *  arguments; RoboNotFound when it was never set. */
Result LayoutSizingMinimum(const LayoutSizing *sizing,
                                         LayoutSize *out);
Result LayoutSizingMaximum(const LayoutSizing *sizing,
                                         LayoutSize *out);

/**
 * Non-zero when the record is internally consistent: a minimum and a maximum that
 * are both known must not contradict each other.
 *
 * The same predicate the constraints record carries, restated over this record's
 * fields rather than reached through a conversion, because the two records hold the
 * bounds the same way on purpose -- a sizing record that stored its bounds
 * differently from 10.1's would be two shapes for one fact.
 */
int LayoutSizingIsValid(const LayoutSizing *sizing);

/* ---- what the contents turned out to be ---- */

/**
 * Record the extent the box's contents reached.
 *
 * The `auto` height's input, and the reason an atomic inline's sizing happens in
 * two phases: this number does not exist until the box's children have been laid
 * out, and they are laid out at a width that has to be resolved first.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p sizing or a negative @p size
 *         -- a contents extent is an extent, and an empty box reached zero.
 */
Result LayoutSizingSetContent(LayoutSizing *sizing,
                                             LayoutSize size,
                                             LayoutDiag *diag);

/** Non-zero when the contents' extent has been recorded. 0 for NULL. */
int LayoutSizingHasContent(const LayoutSizing *sizing);

/**
 * Record the subtree's intrinsic size (19.1).
 *
 * A separate door from `LayoutSizingSetContent` because they are different facts: the
 * content *extent* is what an `auto` height resolves from and exists only after the
 * children were laid out, while the intrinsic *size* is what an intrinsic width resolves
 * from and is a measurement taken before anything is placed.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p sizing or an @p intrinsicSize
 *         that is not valid (a negative width, or `min > max`).
 */
Result LayoutSizingSetIntrinsic(LayoutSizing *sizing,
                                                LayoutIntrinsicSize intrinsicSize,
                                                LayoutDiag *diag);

/** Non-zero when an intrinsic size has been recorded. 0 for NULL. */
int LayoutSizingHasIntrinsic(const LayoutSizing *sizing);

/** Read the recorded intrinsic size. @return RoboOk; RoboInvalidArgument for NULL
 *  arguments; RoboNotFound when it was never set. */
Result LayoutSizingIntrinsic(const LayoutSizing *sizing,
                                         LayoutIntrinsicSize *out);

/** Read the recorded contents extent. @return RoboOk; RoboInvalidArgument for
 *  NULL arguments; RoboNotFound when it was never set. */
Result LayoutSizingContent(const LayoutSizing *sizing,
                                         LayoutSize *out);

/* ---- resolving ---- */

/**
 * Resolve the width and record it in `resolved.width` and `widthSource`.
 *
 *   DEFINITE    the declared length, source `DECLARED`
 *   AUTO        the available width, source `AVAILABLE`
 *   UNRESOLVED  the available width too, source `AVAILABLE` -- CSS's `width: 50%`
 *               is "half of a containing block this build has not been given the
 *               dimension for", and filling the containing block is the only
 *               answer it can defend until the containing dimension is a fact.
 *               Recorded as `UNRESOLVED` in the dimension, so a *rule* that later
 *               resolves percentages changes this branch and nothing else
 *
 * then applies the bounds. A minimum above a maximum is reported
 * (`RoboNotFound`) rather than resolved in either's favour: the conflict is a
 * caller's to fix, and 10.1's clamp already states that picking one would hide it.
 *
 * Re-running is legal and *is* the re-layout story (§17 of the phase): the answer
 * is a function of the record, so a caller that changed the available width or the
 * contents and resolved again gets the new geometry and no stale half of the old.
 * Nothing here is cached, which is what makes that true.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p sizing; RoboNotFound when
 *         an `auto` width has no available width to fill.
 */
Result LayoutSizingResolveWidth(LayoutSizing *sizing,
                                               LayoutDiag *diag);

/**
 * Resolve the height and record it in `resolved.height` and `heightSource`.
 *
 *   DEFINITE    the declared length, source `DECLARED`
 *   AUTO        the contents' extent, source `CONTENT`
 *   UNRESOLVED  the contents' extent too, source `CONTENT` -- a declared
 *               percentage height is unresolvable for the same reason a percentage
 *               width is
 *
 * then applies the bounds, exactly as the width does.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p sizing; RoboNotFound when
 *         an `auto` height has no contents extent to take -- which is a *different*
 *         failure from an `auto` width with no available width, and the two
 *         diagnostics say which.
 */
Result LayoutSizingResolveHeight(LayoutSizing *sizing,
                                                LayoutDiag *diag);

/**
 * Resolve both dimensions — the width first, then the height, so that a caller
 * which happens to have set `content` gets one consistent pair.
 *
 * A failure of either half fails the whole call, and the half that succeeded is
 * **not** rolled back: the record is a scratchpad, the caller is about to discard
 * it, and undoing a correct answer to make a failure look tidy would be work that
 * hides which half went wrong.
 *
 * @return RoboOk, or the first half's failure.
 */
Result LayoutSizingResolve(LayoutSizing *sizing,
                                         LayoutDiag *diag);

/** The resolved size. @return RoboOk; RoboInvalidArgument for NULL arguments;
 *  RoboNotFound while a dimension is still unresolved. */
Result LayoutSizingResolved(const LayoutSizing *sizing,
                                          LayoutSize *out);

/** How the width was arrived at. `NONE` until it has been resolved, and for
 *  NULL -- there is no failure mode a source enum has to report, because it *is*
 *  the report. */
LayoutDimensionSource LayoutSizingWidthSource(
    const LayoutSizing *sizing);
LayoutDimensionSource LayoutSizingHeightSource(
    const LayoutSizing *sizing);

/** Non-zero when a bound moved the resolved width / height. 0 for NULL. */
int LayoutSizingWidthClamped(const LayoutSizing *sizing);
int LayoutSizingHeightClamped(const LayoutSizing *sizing);

/** Field-by-field equality, including which fields are set and both resolved
 *  dimensions. Safe on NULL. */
int LayoutSizingEqual(const LayoutSizing *a,
                             const LayoutSizing *b);

/* ------------------------------------------------------------------ */
/* The CSS seam                                                        */
/* ------------------------------------------------------------------ */

/**
 * Read what @p style declares for @p property, as a dimension.
 *
 * The **one** place a computed style becomes a size in this module, and it is
 * deliberately four checks rather than a resolver — the argument is 10.3's, which
 * wrote them:
 *
 *   the entry must exist. A style with no entry is an element nothing declared the
 *   property for -- `auto`, by CSS's initial value.
 *
 *   a `KEYWORD` value spelling `auto` is `AUTO`; any other keyword is
 *   `UNRESOLVED` (there is no keyword that names a size here).
 *
 *   a `LENGTH` in `px` that is finite, non-negative and inside the geometry's range
 *   is `DEFINITE`. 9.8's computed rule normalises an *absolute* unit to px exactly,
 *   so `1in` is usable and a relative unit is not.
 *
 *   a `PERCENTAGE`, a relative `LENGTH` or an out-of-range number is `UNRESOLVED`:
 *   declared, and not something this build can turn into pixels.
 *
 * `UNRESOLVED` resolving the same way as `AUTO` is the *caller's* rule (see the two
 * resolvers), not this door's: what a percentage should do is a decision that
 * belongs where the containing dimension is known, and a reader that folded it into
 * `AUTO` here would have taken that decision in the wrong place.
 *
 * @p out is filled only when this door can answer: a NULL @p style or @p property
 * leaves it as it found it, which is what lets a caller initialise a record and
 * read four properties in a row without a branch between them. That is not sloppy
 * NULL handling but the *contract*: a box with no computed style has made no
 * declarations, and the caller's initialised record already says `AUTO`.
 *
 * @return 1 when @p out was filled; 0 for a NULL argument.
 */
int LayoutSizingReadDimension(const CssComputedStyle *style,
                                      const char *property,
                                      LayoutDimension *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_SIZING_H */
