/*
 * RoboCSS flexbox vocabulary (Phase 18.3).
 *
 * Flexbox is the first property *family* in this build whose values are not one
 * kind of thing at all, so this module is three vocabularies and one rule, and the
 * reason they are together is that a consumer reads them together: a flex container
 * has a main axis (`flex-direction`), a wrapping rule (`flex-wrap`), a way of
 * distributing free space along the main axis (`justify-content`) and a way of
 * aligning the items across it (`align-items`, and `align-self` for one item). What
 * every one of them has in common is that it is a value a *layout* pass dispatches
 * on — "which axis is main", "does this wrap", "how are the items placed" — so an
 * identifier none of them knows has to be a mistake rather than a keyword carrying
 * its letters to a phase that can only fail on it. That is the same decision
 * `display`, `position`, `overflow` and `border-style` each record, and the third
 * time it has been made it is a pattern: a closed set is a table, and the table
 * carries the classifications so no call site compares a spelling.
 *
 *   CssFlexDirection   row | row-reverse | column | column-reverse
 *   CssFlexWrap        nowrap | wrap | wrap-reverse
 *   CssFlexAlign       the union `justify-content`, `align-items`, `align-self`
 *                      and `align-content` are each a subset of
 *
 * One table per vocabulary, and each row carries the facts a consumer asks rather
 * than the spelling it was written as. A direction row says which physical axis is
 * the main one and whether the items are reversed; a wrap row says whether it wraps
 * and whether the lines reverse; an alignment row says which of the four properties
 * may be written with it — which is the one column that makes the union safe, since
 * `align-items: space-between` and `justify-content: baseline` are values CSS refuses
 * and a single shared enumerator could not tell you so on its own.
 *
 * Two properties of the family are *shorthands over these vocabularies* and are
 * deliberately not read here. `flex-flow` is `<flex-direction> || <flex-wrap>` and
 * `flex` is `none | <flex-grow> <flex-shrink>? || <flex-basis>`; each is a shorthand
 * whose two halves are different kinds of value, so neither can take one vocabulary
 * the way `overflow` took its own. They are registered as carried declarations
 * (PRESERVE) and the boundary is stated rather than crossed: nothing in this build
 * expands a shorthand, so `flex-flow: column wrap` reaches a consumer as the tokens
 * the author wrote and each half is read from its own longhand. That is the honest
 * half-answer and it is named in `Flexbox.md`.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides whether a property inherits or what its
 * initial value is (RoboCSSPropertyRegistry.h). Not a value parser: what arrives is
 * one identifier's text. Not a layout: nothing here measures a line, distributes free
 * space, or computes a main-axis position, and no function below takes or returns a
 * rectangle — a value says *what rule* a layout pass follows, and where the items land
 * belongs to the phase that has a box.
 */

#ifndef ROBO_CSS_FLEX_H
#define ROBO_CSS_FLEX_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* flex-direction                                                      */
/* ------------------------------------------------------------------ */

/**
 * A computed `flex-direction`.
 *
 * A closed set, and the enumerator values are append-only because a stored
 * enumerator is a contract: a later value (`flex-direction` has no others today)
 * joins as 4 rather than reshuffling what a previous phase wrote down. ROW is **0**
 * because it is the property's initial value, so a cleared record holds the state in
 * which an author who declared nothing finds their container.
 */
typedef enum CssFlexDirection {
    /** `row`: the main axis is the inline axis, left to right in `ltr`. */
    RoboCssFlexDirectionRow = 0,
    /** `row-reverse`: the inline axis, with the start and end edges swapped. */
    RoboCssFlexDirectionRowReverse = 1,
    /** `column`: the main axis is the block axis, top to bottom. */
    RoboCssFlexDirectionColumn = 2,
    /** `column-reverse`: the block axis, with the start and end edges swapped. */
    RoboCssFlexDirectionColumnReverse = 3
} CssFlexDirection;

/** Number of defined flex-direction values. */
#define RoboCssFlexDirectionCount 4u

/** Non-zero when @p direction is one of the four. ROW is defined. */
int CssFlexDirectionIsKnown(CssFlexDirection direction);

/** Human-readable name ("row", "column-reverse"); never NULL, "" for unknown. */
const char *CssFlexDirectionName(CssFlexDirection direction);

/** Number of rows in the vocabulary. */
size_t CssFlexDirectionCount(void);

/**
 * The direction an identifier spells, or ROW when it spells none of them.
 *
 * ASCII case-insensitive. ROW is both a value and the answer for "not one", which is
 * the ambiguity every closed vocabulary in this build has, and the door that resolves
 * it is @ref CssFlexDirectionSliceIsKnown rather than a predicate on the result.
 */
CssFlexDirection CssFlexDirectionFromSlice(CssSlice keyword);

/** NUL-terminated flavour of the above. */
CssFlexDirection CssFlexDirectionFromText(const char *keyword);

/** Whether @p keyword spells one of the four, case-insensitively. */
int CssFlexDirectionSliceIsKnown(CssSlice keyword);

/**
 * Write @p direction's canonical keyword into @p buffer, NUL-terminated.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param outLength non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an unknown
 *         enumerator; RoboNotSupported when the keyword does not fit, in which
 *         case @p outLength receives the length it would have needed.
 */
Result CssFlexDirectionWrite(CssFlexDirection direction,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag);

/** Capacity @ref CssFlexDirectionWrite needs ("column-reverse" plus a NUL). */
#define RoboCssFlexDirectionTextMax 16u

/**
 * Whether @p direction's main axis is the **inline** axis — `row` or `row-reverse`.
 *
 * The one classification a layout pass dispatches on before anything else: with a row
 * main axis the items run along the inline axis and the cross axis is the block axis,
 * and with a column main axis those two swap. The `-reverse` spellings answer the same
 * as their un-reversed forms, because reversing a direction does not move the axis.
 */
int CssFlexDirectionIsRow(CssFlexDirection direction);

/**
 * Whether @p direction's main axis is the **block** axis — `column` or
 * `column-reverse`. The exact complement of @ref CssFlexDirectionIsRow for a known
 * value, and 0 for an unknown one (which is the safe direction: an unreadable value
 * must not claim the main axis is a column).
 */
int CssFlexDirectionIsColumn(CssFlexDirection direction);

/**
 * Whether @p direction swaps the main axis's start and end edges.
 *
 * True for `row-reverse` and `column-reverse`. This is not a mirroring of the text:
 * it moves the *start* edge, and with it the whole ordering, which is why it is a
 * classification rather than something a consumer derives from "inline vs block".
 */
int CssFlexDirectionIsReversed(CssFlexDirection direction);

/* ------------------------------------------------------------------ */
/* flex-wrap                                                           */
/* ------------------------------------------------------------------ */

/**
 * A computed `flex-wrap`.
 *
 * A closed set. NOWRAP is **0** because it is the property's initial value.
 */
typedef enum CssFlexWrap {
    /** `nowrap`: the items are laid out in a single line. */
    RoboCssFlexWrapNoWrap = 0,
    /** `wrap`: the items are broken into several lines when they do not fit. */
    RoboCssFlexWrapWrap = 1,
    /** `wrap-reverse`: as `wrap`, with the cross-axis start and end swapped. */
    RoboCssFlexWrapWrapReverse = 2
} CssFlexWrap;

/** Number of defined flex-wrap values. */
#define RoboCssFlexWrapCount 3u

/** Non-zero when @p wrap is one of the three. NOWRAP is defined. */
int CssFlexWrapIsKnown(CssFlexWrap wrap);

/** Human-readable name ("nowrap", "wrap-reverse"); never NULL, "" for unknown. */
const char *CssFlexWrapName(CssFlexWrap wrap);

/** Number of rows in the vocabulary. */
size_t CssFlexWrapCount(void);

/** The wrap an identifier spells, or NOWRAP when it spells none of them. */
CssFlexWrap CssFlexWrapFromSlice(CssSlice keyword);

/** NUL-terminated flavour of the above. */
CssFlexWrap CssFlexWrapFromText(const char *keyword);

/** Whether @p keyword spells one of the three, case-insensitively. */
int CssFlexWrapSliceIsKnown(CssSlice keyword);

/** Write @p wrap's canonical keyword. Same contract as CssFlexDirectionWrite. */
Result CssFlexWrapWrite(CssFlexWrap wrap,
                        char *buffer,
                        size_t capacity,
                        size_t *outLength,
                        CssDiag *diag);

/** Capacity @ref CssFlexWrapWrite needs ("wrap-reverse" plus a NUL). */
#define RoboCssFlexWrapTextMax 16u

/**
 * Whether @p wrap breaks the items into several lines.
 *
 * The question a layout pass asks first — a `nowrap` container has one line however
 * many items it holds — and it is a door on the vocabulary rather than a comparison
 * against `NOWRAP` at each call site, so the day a value is added it answers for
 * itself.
 */
int CssFlexWrapIsWrapped(CssFlexWrap wrap);

/** Whether @p wrap reverses the cross-axis line order (`wrap-reverse`). */
int CssFlexWrapIsReversed(CssFlexWrap wrap);

/* ------------------------------------------------------------------ */
/* The alignment vocabulary (justify-content / align-items / self / content) */
/* ------------------------------------------------------------------ */

/**
 * A computed alignment value.
 *
 * One enumerator set shared by four properties, which is not a simplification but
 * the fact: CSS Box Alignment defines them as one vocabulary of *content
 * distribution*, *content position*, *self position* and *baseline position* values,
 * and `justify-content`, `align-items`, `align-self` and `align-content` are four
 * subsets of it. What differs between them is which of the values are legal, and that
 * is what @ref CssFlexAlignAllowedFor answers from the table — so
 * `align-items: space-between` is a mistake while `justify-content: space-between` is
 * a value, and neither fact is a comparison written at a call site.
 *
 * NORMAL is **0**: it is the initial value of `justify-content`, `align-items` and
 * `align-content`, and it is legal for all four properties, which makes it the one
 * safe answer a cleared record can hold. (`align-self`'s initial is `auto`, which is
 * the registry's answer rather than this default — see RoboCSSComputedValue.h.)
 */
typedef enum CssFlexAlign {
    /** `normal`: the property's own default behaviour. */
    RoboCssFlexAlignNormal = 0,
    /** `stretch`: items with no size of their own fill the container. */
    RoboCssFlexAlignStretch = 1,
    /** `flex-start`: packed against the start edge. */
    RoboCssFlexAlignFlexStart = 2,
    /** `flex-end`: packed against the end edge. */
    RoboCssFlexAlignFlexEnd = 3,
    /** `center`: packed in the middle. */
    RoboCssFlexAlignCenter = 4,
    /** `space-between`: the first item at the start, the last at the end. */
    RoboCssFlexAlignSpaceBetween = 5,
    /** `space-around`: equal space around every item. */
    RoboCssFlexAlignSpaceAround = 6,
    /** `space-evenly`: equal space between, before and after every item. */
    RoboCssFlexAlignSpaceEvenly = 7,
    /** `baseline`: aligned on their first baselines. */
    RoboCssFlexAlignBaseline = 8,
    /** `auto`: `align-self` inherits the parent's `align-items`. */
    RoboCssFlexAlignAuto = 9,
    /** `start`: the start edge of the container's own writing mode. */
    RoboCssFlexAlignStart = 10,
    /** `end`: the end edge of the container's own writing mode. */
    RoboCssFlexAlignEnd = 11,
    /** `self-start`: the start edge of the item's own writing mode. */
    RoboCssFlexAlignSelfStart = 12,
    /** `self-end`: the end edge of the item's own writing mode. */
    RoboCssFlexAlignSelfEnd = 13,
    /** `left`: the physical left edge (`justify-content` only). */
    RoboCssFlexAlignLeft = 14,
    /** `right`: the physical right edge (`justify-content` only). */
    RoboCssFlexAlignRight = 15
} CssFlexAlign;

/** Number of defined alignment values. */
#define RoboCssFlexAlignCount 16u

/** Non-zero when @p align is one of the sixteen. NORMAL is defined. */
int CssFlexAlignIsKnown(CssFlexAlign align);

/** Human-readable name ("space-between"); never NULL, "" for unknown. */
const char *CssFlexAlignName(CssFlexAlign align);

/** Number of rows in the vocabulary. */
size_t CssFlexAlignCount(void);

/** The alignment an identifier spells, or NORMAL when it spells none of them. */
CssFlexAlign CssFlexAlignFromSlice(CssSlice keyword);

/** NUL-terminated flavour of the above. */
CssFlexAlign CssFlexAlignFromText(const char *keyword);

/** Whether @p keyword spells one of the sixteen, case-insensitively. */
int CssFlexAlignSliceIsKnown(CssSlice keyword);

/** Write @p align's canonical keyword. Same contract as CssFlexDirectionWrite. */
Result CssFlexAlignWrite(CssFlexAlign align,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssFlexAlignWrite needs ("space-between" plus a NUL). */
#define RoboCssFlexAlignTextMax 16u

/** Which of the four alignment properties a value is being read for. */
typedef enum CssFlexAlignRole {
    /** `justify-content`: the main-axis distribution. */
    RoboCssFlexAlignRoleJustify = 0,
    /** `align-items`: the cross-axis alignment of every item. */
    RoboCssFlexAlignRoleItems = 1,
    /** `align-self`: the cross-axis alignment of one item. */
    RoboCssFlexAlignRoleSelf = 2,
    /** `align-content`: the cross-axis distribution of the lines. */
    RoboCssFlexAlignRoleContent = 3
} CssFlexAlignRole;

/** Number of defined roles. */
#define RoboCssFlexAlignRoleCount 4u

/** Non-zero when @p role is one of the four. */
int CssFlexAlignRoleIsKnown(CssFlexAlignRole role);

/** Human-readable role name ("justify", "items", "self", "content"); never NULL. */
const char *CssFlexAlignRoleName(CssFlexAlignRole role);

/**
 * Whether @p align may be written for @p role.
 *
 * The one question that makes one enumerator set safe for four properties, and it is
 * answered from the row rather than by a comparison here: a value legal for
 * `justify-content` and not for `align-items` (`space-between`, `left`) is a fact
 * about the row, so a later value answers it by being one more row. An unknown value
 * or an unknown role answers 0 — a value this build cannot read must not be accepted
 * for a property it might not belong to.
 */
int CssFlexAlignAllowedFor(CssFlexAlign align, CssFlexAlignRole role);

/** Whether @p align is a *distribution* value (`space-between`/`around`/`evenly`). */
int CssFlexAlignIsDistributed(CssFlexAlign align);

/** Whether @p align is a baseline position (`baseline`). */
int CssFlexAlignIsBaselinePosition(CssFlexAlign align);

/** Whether @p align is `auto` — the answer only `align-self` may hold. */
int CssFlexAlignIsAuto(CssFlexAlign align);

/** Whether @p align is the initial `normal`. */
int CssFlexAlignIsNormal(CssFlexAlign align);

/* ------------------------------------------------------------------ */
/* flex-grow / flex-shrink                                             */
/* ------------------------------------------------------------------ */

/**
 * Whether @p factor is a legal flex factor: finite, and not negative.
 *
 * `flex-grow` and `flex-shrink` are `<number>`, and CSS requires a non-negative one.
 * The rule lives here rather than in the compute layer because it is the property
 * family's, and because both properties share it: a negative factor is a mistake with
 * a diagnostic rather than a value a layout pass would have to interpret. There is no
 * upper bound — a very large factor is legal CSS and simply takes the free space.
 */
int CssFlexFactorIsValid(double factor);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_FLEX_H */
