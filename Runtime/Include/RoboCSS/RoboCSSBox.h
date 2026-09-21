/*
 * RoboCSS box model vocabulary (Phase 9.11).
 *
 * CSS writes a box's four edges twice: once as four longhands and once as one
 * shorthand.
 *
 *   margin-top: 10px;  margin-right: 10px;  margin-bottom: 10px;  margin-left: 10px;
 *   margin: 10px;
 *
 * The two spellings are the *same four values*, and everything downstream —
 * inheritance, computed values, and eventually layout — is written against the
 * longhands. So something has to turn the second into the first, and this header
 * is that something, held apart from the property registry, the value layer and
 * the inheritance model because it is the one piece of the box model that is
 * neither a property fact, a value shape, nor a resolution rule.
 *
 * Three facts live here and nothing else
 *
 *   the sides          top, right, bottom, left — as an ordered, closed set,
 *                      because the shorthand's component order *is* that order
 *                      and four places would otherwise each restate it
 *   the shorthands     which longhands each positional shorthand stands for, in
 *                      side order — `margin`, `padding`, and the three border
 *                      kinds `border-width`, `border-style` and `border-color`
 *   the mapping        how one, two, three or four components distribute over
 *                      the four sides
 *
 * What it deliberately is not
 *
 * Not a property registry: nothing here classifies a property, gives it an
 * initial value or decides whether it inherits (RoboCSSPropertyRegistry.h). Not
 * a value parser: a component is a `CssValue` and a component's
 * *meaning* — is `10px` a length, is `auto` acceptable here — belongs to the
 * value layer, which reads the property's shape. And not a resolution rule:
 * *which* of a shorthand and a longhand wins when both are declared is the
 * cascade's answer, not this module's, and expansion happens after it.
 *
 * The mapping, and why it is a table
 *
 * CSS distributes one to four components over four sides by a rule whose whole
 * content is the shortest spelling that writes a distinct value per side:
 *
 *   1  value      all four sides
 *   2  a b        top/bottom = a, right/left = b
 *   3  a b c      top = a, right/left = b, bottom = c
 *   4  a b c d    top, right, bottom, left
 *
 * Four rows, read by (component count, side). Written as a switch it would be
 * four branches restated per shorthand; written as a table it is one row per
 * count, and a fifth count is a value this module refuses. The border family's
 * three rows carry a fourth thing over the same mapping: a *kind*, so its
 * longhand for a side is the kind's row at that side rather than the shorthand's
 * own (§ the row table).
 *
 * The three border rows are here for the same reason `margin` is: the four
 * widths, the four styles and the four colours are each four values over four
 * sides, which is this table's shape exactly, and a `border-width: 1px 2px` that
 * no longhand ever saw would be a declaration no later phase could consume.
 * Nothing in this header knows what a border looks like.
 *
 * Vocabulary here, distribution elsewhere — and the split is not arbitrary
 *
 * 9.15 moved the *distribution* of the three border rows out of the box model's
 * pass and into the border family's, because a border longhand can be set by more
 * than one shorthand (`border-width`, a side shorthand, `border`) and a longhand
 * two declarations can speak for cannot be materialised by two independent passes
 * (RoboCSSBorder.h). What stayed is the table, and it is the half that has to
 * stay: the twelve longhand names are the vocabulary, and a row is where the
 * three-way relation between a shorthand, its four sides and those names is
 * written down once, so `CssBorderLonghandName` can read it rather than
 * spell it again. The box pass asks `CssBorderDistributesShorthand`
 * before it distributes a row — the question is not "is this a positional
 * shorthand" but "is this row mine or the border family's", and only the border
 * module can answer it.
 *
 * Expansion and `var()`
 *
 * `margin: var(--edges)` is one component that will not be one component until
 * `--edges` is substituted, and substitution can change how many values there
 * are: `--edges: 10px 20px` makes it two. Splitting *that* into sides now would
 * assign the whole reference to every side and be wrong on the first real
 * declaration. So a value containing a `var()` or `env()` reference is reported
 * as *not expandable* rather than expanded into something plausible — the
 * author's declaration survives intact on the shorthand, and the longhands
 * arrive when a later phase can substitute. `calc()` is different and needs no
 * such care: `calc(10px + 5px)` is one component whatever is inside it.
 */

#ifndef ROBO_CSS_BOX_H
#define ROBO_CSS_BOX_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The sides                                                           */
/* ------------------------------------------------------------------ */

/**
 * A box edge. The order is CSS's own — top, right, bottom, left — and it is
 * load-bearing rather than conventional: it is the order the shorthand's
 * components are read in, so a set that listed them differently would silently
 * transpose every two- and three-component shorthand.
 */
typedef enum CssBoxSide {
    RoboCssBoxSideTop = 0,
    RoboCssBoxSideRight = 1,
    RoboCssBoxSideBottom = 2,
    RoboCssBoxSideLeft = 3
} CssBoxSide;

/** Number of sides a box has. */
#define RoboCssBoxSideCount 4u

/**
 * The most components a shorthand may be written with. Five is not a longer
 * shorthand, it is an invalid declaration — which is why this is a bound and not
 * a starting point.
 */
#define RoboCssBoxComponentMax 4u

/** Non-zero when @p side is one of the four. */
int CssBoxSideIsKnown(CssBoxSide side);

/** Human-readable side name ("top", "right", "bottom", "left"); never NULL. */
const char *CssBoxSideName(CssBoxSide side);

/* ------------------------------------------------------------------ */
/* The shorthands                                                      */
/* ------------------------------------------------------------------ */

/**
 * One shorthand and the four longhands it stands for, in side order.
 *
 * Both arrays are borrowed and static: they are this build's vocabulary, they
 * outlive every caller, and handing them out as pointers rather than copies is
 * what lets a test walk the whole table — which is how "every shorthand this
 * build claims expands in both directions" becomes one loop rather than three
 * hand-written cases.
 */
typedef struct CssBoxShorthand {
    /** Borrowed, NUL-terminated shorthand name (`margin`). */
    const char *name;
    /** Length of @p name in bytes, excluding its NUL. */
    size_t nameLength;
    /** Borrowed, NUL-terminated longhand names, indexed by CssBoxSide. */
    const char *longhands[RoboCssBoxSideCount];
} CssBoxShorthand;

/** Number of shorthands this build expands. */
size_t CssBoxShorthandCount(void);

/** The shorthand at @p index in table order, or NULL when out of range. */
const CssBoxShorthand *CssBoxShorthandAt(size_t index);

/**
 * Find the shorthand named @p name (ASCII case-insensitive, because CSS
 * compares property names that way).
 * @return a borrowed, static row, or NULL when @p name is not a shorthand this
 *         build expands.
 */
const CssBoxShorthand *CssBoxShorthandFind(CssSlice name);

/**
 * The longhand @p shorthand uses for @p side, or NULL for a NULL shorthand or an
 * unknown side. Borrowed and static.
 */
const char *CssBoxShorthandLonghand(const CssBoxShorthand *shorthand,
                                            CssBoxSide side);

/**
 * The shorthand that owns @p name as one of its longhands, and which side.
 *
 * The question a resolver asks about a *declaration* rather than about a
 * shorthand: `margin-left` is not "the left edge of", it is a longhand whose
 * value has to be decided together with the other three, and a resolution that
 * let a `margin-left` declaration be answered on its own would answer it twice —
 * once as itself and once as part of the shorthand.
 *
 * @p outSide may be NULL. @return the shorthand row, or NULL when @p name is
 *         not a longhand this build distributes.
 */
const CssBoxShorthand *CssBoxShorthandOfLonghand(
    CssSlice name,
    CssBoxSide *outSide);

/* ------------------------------------------------------------------ */
/* Expansion                                                           */
/* ------------------------------------------------------------------ */

/**
 * How many components @p shorthandValue is written with, or 0 when it is not a
 * sequence of components at all.
 *
 * Trivia is skipped, because a declaration's value keeps its interior
 * whitespace: `10px 20px` arrives as three children, two of them the values. A
 * bare token counts as one component, so a hand-built value and a parsed one
 * answer the same way.
 *
 * This is a *structural* count and says nothing about whether the components can
 * be distributed — see CssBoxIsExpandable.
 */
size_t CssBoxComponentCount(const CssValue *shorthandValue);

/**
 * Non-zero when @p shorthandValue can be distributed over the four sides, with
 * the component count written to @p outComponentCount (which may be NULL).
 *
 * Two conditions, and each refuses a different mistake. The count must be
 * between one and four: zero (nothing was written) and five (an invalid
 * declaration) have no mapping, and inventing one would turn a malformed
 * shorthand into four plausible values. And no component may be a `var()` or
 * `env()` reference, because substitution happens *before* expansion in CSS and
 * can change how many components there are — see the header comment.
 */
int CssBoxIsExpandable(const CssValue *shorthandValue,
                               size_t *outComponentCount);

/**
 * Copy the component @p side is written with out of @p shorthandValue.
 *
 * @p componentCount must be the count CssBoxIsExpandable reported; it
 * is passed in rather than recomputed so that the mapping a caller checked is
 * the mapping that is applied.
 *
 * The result is a value tree holding exactly that one component — a list of one,
 * which is the shape a declared longhand's value has — so it can be handed to
 * the same code a `margin-left: 10px` declaration goes through. Its slices
 * borrow @p shorthandValue's source, exactly as every component value does.
 *
 * @p out is disposed first and is left KIND_NONE on failure.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an unknown side;
 *         RoboNotSupported when @p componentCount is outside 1..4 or the
 *         component is past the end of the value (neither of which can happen
 *         for a count this module produced); RoboOutOfMemory.
 */
Result CssBoxExpandSide(const CssValue *shorthandValue,
                                       size_t componentCount,
                                       CssBoxSide side,
                                       CssValue *out,
                                       CssDiag *diag);

/**
 * Copy every side's component out of @p shorthandValue in one call.
 *
 * @p outSides is filled in side order and is *all or nothing*: on any failure
 * every element is left KIND_NONE, so a caller that ignores the result cannot be
 * handed two sides of a four-sided box.
 *
 * @return RoboOk; the codes CssBoxExpandSide returns.
 */
Result CssBoxExpand(const CssValue *shorthandValue,
                                  size_t componentCount,
                                  CssValue outSides[RoboCssBoxSideCount],
                                  CssDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_BOX_H */
