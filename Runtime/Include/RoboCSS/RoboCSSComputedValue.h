/*
 * RoboCSS computed values (Phase 9.8).
 *
 * A *specified* value is what a declaration says: the component-value tree the
 * grammar built, tokens intact. A *computed* value is what the property's own
 * semantics make of it — `50%` is a percentage and not a length, `#f00` is the
 * colour red and not a hash token, `auto` is the keyword it is and not the five
 * letters. This header is that second representation, and it is deliberately a
 * small closed set of shapes rather than "a string, but typed":
 *
 *   NONE         nothing computed (a cleared record)
 *   KEYWORD      one identifier, decoded: `auto`, `normal`, `inline`, `static`
 *   NUMBER       a bare number: `line-height: 1.5`
 *   PERCENTAGE   a percentage, which stays one because only layout can resolve it
 *   LENGTH       a number and a unit: `10px`, `2rem`, `1em`
 *   COLOR        a colour: four channels, or the contextual `currentColor`
 *   FAMILY_LIST  an ordered font family list
 *   DISPLAY      one of the closed display vocabulary (9.12)
 *   POSITION     one of the closed positioning vocabulary (9.13)
 *   PRESERVED    the specified tokens, carried intact
 *   INVALID      the value did not mean anything for this property
 *
 * The last two of those are the ones a reader will question, and both are
 * load-bearing — see below.
 *
 * The last two are the ones a reader will question, and both are load-bearing.
 *
 * PRESERVED is not "unimplemented". `width: calc(100% - 20px)` is a real
 * computed value in this build: it is the expression, structurally intact, and
 * reducing it here would be a lie because `%` needs a containing block. The
 * same is true of `var(--x)` (a reference, not a value), of a shorthand whose
 * longhands are not expanded yet, and of a custom property, which is *defined*
 * as an opaque token sequence. A representation that kept the tokens is exactly
 * as computed as one that produced a pixel, and more honest.
 *
 * INVALID is not an error return. "This value means nothing for this property"
 * is an *answer* about the value, and it is reported as one: the operation
 * succeeded, the computed value says INVALID, a diagnostic carries a short
 * reason, and the computed style counts it as unsupported. A function that
 * returned a failure code instead would force a caller to choose between
 * dropping the property and inventing a value for it.
 *
 * What is deliberately not here: used values. `50%` stays `50%`, `1em` stays
 * `1em`, `normal` stays `normal`, and nothing asks how wide anything is. The
 * conversion of a length percentage to a number is the layout phases' business,
 * and a colour space this build does not represent stays carried (see below).
 *
 * Colours, and the 9.9 seam
 *
 * A colour is computed here and *parsed* elsewhere: `CssColorFromValue`
 * (RoboCSSColor.h) owns every spelling — the standard named keywords,
 * `transparent`, all four hex lengths, `rgb()`/`rgba()` and `hsl()`/`hsla()` in
 * both the comma and the space syntax — and this layer turns its answer into the
 * right computed value. That is the whole of the colour knowledge in this file,
 * and it is deliberately almost nothing: a second colour parser here is how two
 * spellings of the same colour come to disagree.
 *
 * `currentColor` arrives as a COLOR value whose colour is *contextual*: typed,
 * and flagged unresolved, because the only thing that can resolve it is the
 * element's own computed `color` — and its parent's, when the property being
 * computed *is* `color`. That resolution happens in the computed-style layer
 * (RoboCSSComputed.c), which is the first stage with a parent to read. Colours
 * this build cannot represent (`oklch()`, `color()`, …) are carried PRESERVED and
 * flagged unresolved rather than called invalid, because the author wrote legal
 * CSS and saying otherwise would be a lie.
 *
 * Ownership
 *
 * A computed value owns every byte it reports except the slices inside a
 * PRESERVED tree, which borrow the source that declared them exactly as every
 * component value does (see RoboCSSValue.h). A computed style therefore stays
 * valid as long as the sheets it was computed from.
 */

#ifndef ROBO_CSS_COMPUTED_VALUE_H
#define ROBO_CSS_COMPUTED_VALUE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSBackground.h"
#include "RoboCSS/RoboCSSBorder.h"
#include "RoboCSS/RoboCSSColor.h"
#include "RoboCSS/RoboCSSFlex.h"
#include "RoboCSS/RoboCSSGrid.h"
#include "RoboCSS/RoboCSSOverflow.h"
#include "RoboCSS/RoboCSSTable.h"
#include "RoboCSS/RoboCSSColumn.h"
#include "RoboCSS/RoboCSSWriting.h"
#include "RoboCSS/RoboCSSTransform.h"
#include "RoboCSS/RoboCSSTransition.h"
#include "RoboCSS/RoboCSSAnimation.h"
#include "RoboCSS/RoboCSSContainer.h"
#include "RoboCSS/RoboCSSFont.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboCSS/RoboCSSPosition.h"
#include "RoboCSS/RoboCSSPropertyRegistry.h"
#include "RoboCSS/RoboCSSUnit.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Length units — moved to RoboCSSUnit.h in 9.10                        */
/* ------------------------------------------------------------------ */

/*
 * 9.8 defined `CssUnit` and its accessors here, beside the value model
 * they were one part of, and this header includes RoboCSSUnit.h (above) so that
 * every caller of this one still reaches them through the include it already had.
 *
 * The move is not a reorganisation. 9.8's unit set was seven absolute units, four
 * font-relative ones and four viewport ones, and its only question was "does this
 * unit have a fixed factor to px". 9.10 adds the rest of the set — `lh` and `rlh`,
 * `vi` and `vb`, and the small, large and dynamic viewport families — and, more to
 * the point, a *context* and a resolver: the question "what is `2em` in pixels" is
 * a different kind of question from "is `in` convertible", and it needs an answer
 * that can be "I do not know yet". A second reader arrived, so the vocabulary got
 * a module, exactly as the colour model did in 9.9.
 *
 * The one habit this header keeps is the *computed* contract: a computed LENGTH
 * normalises an absolute unit to `px` (9.8's rule, unchanged, because CSS's
 * absolute factors are definitions and the conversion is exact), and carries a
 * relative unit unconverted with its number, because only whoever has the font or
 * the viewport can resolve it. @ref CssUnitResolvePx is where that
 * happens, and it is a question with four answers rather than a multiplication.
 */

/* ------------------------------------------------------------------ */
/* Colours — moved to RoboCSSColor.h in 9.9                             */
/* ------------------------------------------------------------------ */

/*
 * 9.8 kept the colour model here, beside the value model it was one part of.
 * 9.9 gave it a module of its own (RoboCSSColor.h, included above, so that this
 * header and every caller of it can still reach `CssColor` through the
 * one include they already had).
 *
 * The move is not a reorganisation: 9.8's colour section was a channel record, a
 * named table, hex and `rgb()`. 9.9 adds `hsl()`/`hsla()`, makes `currentColor` a
 * *kind* rather than a carried token tree, adds contextual resolution and
 * serialization, and is the single place any of it lives. A second parse of
 * `rgb()` in this file is how two spellings of the same colour come to disagree,
 * which is why nothing colour-shaped is left here.
 *
 * `CssComputedValue.color` is that record, and it is a kind-aware one:
 * a computed value can hold `currentColor` as a colour that has not been resolved
 * yet, which is a state 9.8 could only express by carrying the tokens instead.
 */

/* ------------------------------------------------------------------ */
/* The computed value                                                  */
/* ------------------------------------------------------------------ */

/** What a computed value is. */
typedef enum CssComputedValueKind {
    /** Nothing computed: a cleared record. */
    RoboCssComputedValueNone = 0,
    /** One decoded identifier. */
    RoboCssComputedValueKeyword = 1,
    /** A bare number. */
    RoboCssComputedValueNumber = 2,
    /** A percentage, unresolved by design. */
    RoboCssComputedValuePercentage = 3,
    /** A number with a unit. */
    RoboCssComputedValueLength = 4,
    /** Four channels. */
    RoboCssComputedValueColor = 5,
    /** An ordered font family list. */
    RoboCssComputedValueFamilyList = 6,
    /** The specified tokens, intact. */
    RoboCssComputedValuePreserved = 7,
    /** The value means nothing for this property. */
    RoboCssComputedValueInvalid = 8,
    /** One of the closed display vocabulary (9.12). */
    RoboCssComputedValueDisplay = 9,
    /** One of the closed positioning vocabulary (9.13). */
    RoboCssComputedValuePosition = 10,
    /** A pair of position components, one per axis (9.14). */
    RoboCssComputedValueBackgroundPosition = 11,
    /** A whole-value size keyword, or a pair of size components (9.14). */
    RoboCssComputedValueBackgroundSize = 12,
    /** One of the closed border-style vocabulary (9.15). */
    RoboCssComputedValueBorderStyle = 13,
    /** One of the closed overflow vocabulary (10.13). */
    RoboCssComputedValueOverflow = 14,
    /**
     * An `opacity` value (11.9): a real in `[0, 1]`, already clamped.
     *
     * The first kind here whose value is a **range** rather than a closed set -- so it is
     * the first whose spelling is arithmetic rather than one row of a table -- and the first
     * added by a phase of the *paint* stack rather than by one of the 9.x value phases or by
     * RoboLayout. It has a kind of its own rather than riding on NUMBER for the reason every
     * closed vocabulary above has one: `opacity` is a value the paint layer dispatches on,
     * and a NUMBER that happened to be a number would be a value whose *property* is the only
     * thing that says it is an opacity.
     *
     * The initial value is `1`, which is why `CssComputedValueOpacity` answers *one*
     * for any other kind rather than zero (RoboCSSOpacity.h argues the direction).
     */
    RoboCssComputedValueOpacity = 15,
    /** One of the closed flex-direction vocabulary (18.3). */
    RoboCssComputedValueFlexDirection = 16,
    /** One of the closed flex-wrap vocabulary (18.3). */
    RoboCssComputedValueFlexWrap = 17,
    /**
     * One of the aligned vocabulary `justify-content`, `align-items`, `align-self`
     * and `align-content` share (18.3).
     *
     * One kind for four properties, because the *value* is the same kind of thing: an
     * `alignment` keyword. Which property it was written on is a fact the registry and
     * the shape hold — the four shapes refuse the values their own property cannot
     * take — so by the time a value is typed here it is already legal for the property
     * that produced it, and a consumer reads the enumerator and its classifications.
     * The kind is named FLEX_ALIGN rather than for one property because it is not one
     * property's: the initial `normal` and the value `space-between` are the same kind
     * of value whichever of the four declared them.
     */
    RoboCssComputedValueFlexAlign = 18,
    /**
     * One of the closed `grid-auto-flow` vocabulary (18.4).
     *
     * A kind of its own rather than riding on FLEX_DIRECTION or on a keyword, for the
     * reason every closed vocabulary has one: `grid-auto-flow` is what a layout pass
     * dispatches on, so it is read as a value and never compared as a spelling — and
     * its canonical text is two words for three of its four values, which a keyword
     * kind would carry as the author's spacing rather than as the value.
     */
    RoboCssComputedValueGridAutoFlow = 19
} CssComputedValueKind;

/** Number of defined kinds. */
#define RoboCssComputedValueKindCount 20u

/** Non-zero when @p kind is a defined enumerator. */
int CssComputedValueKindIsKnown(CssComputedValueKind kind);

/** Human-readable kind name (\"length\", \"colour\"); never NULL. */
const char *CssComputedValueKindName(CssComputedValueKind kind);

/**
 * Capacity of the buffer CssComputedValueWrite needs.
 *
 * Sized for the longest thing this build produces: a serialised font family
 * list, or a number with six decimals, a unit and a percentage sign. A value
 * whose text does not fit is not truncated — @ref
 * CssComputedValueWrite refuses, so a caller learns rather than
 * silently receiving half a value.
 */
#define RoboCssComputedValueTextMax 256u

/**
 * One computed value.
 *
 * A flat record rather than a tagged union: each kind uses one or two of the
 * fields, and a union would save a few bytes in exchange for an ownership rule
 * per member that a reader has to reconstruct. `text` and `preserved` are the
 * only owned members, and `CssComputedValueDispose` frees exactly the
 * ones the kind says are live.
 */
typedef struct CssComputedValue {
    CssComputedValueKind kind;
    /** NUMBER / PERCENTAGE / LENGTH: the magnitude. */
    double number;
    /** LENGTH: the unit; NONE otherwise. */
    CssUnit unit;
    /** NUMBER: non-zero when the value was written as an integer. */
    int isInteger;
    /**
     * COLOR: the colour (RoboCSSColor.h); NONE otherwise.
     * Kind-aware, so a `currentColor` is distinguishable from a resolved one.
     */
    CssColor color;
    /**
     * DISPLAY: the display value (RoboCSSDisplay.h); NONE otherwise.
     *
     * A typed enumerator rather than the keyword's text, so a consumer reads the
     * display *value* instead of comparing a spelling. Meaningful only when
     * @ref kind is DISPLAY: a cleared record holds NONE, which is the value
     * `display: none` and would be a dangerous default to read by accident.
     */
    CssDisplay display;
    /**
     * POSITION: the position value (RoboCSSPosition.h); STATIC otherwise.
     *
     * A typed enumerator rather than the keyword's text, for the same reason
     * @ref display is one. Meaningful only when @ref kind is POSITION: a cleared
     * record holds STATIC, which is the property's *initial* value and therefore
     * the safest thing a cleared record could hold — and still not something to
     * read without the kind check, because "this element does not declare a
     * position" and "this element is static" are different answers.
     */
    CssPositioning position;
    /**
     * BACKGROUND_POSITION: the pair (RoboCSSBackground.h); absent otherwise.
     *
     * A struct rather than one enumerator, because the value *is* two components
     * and CSS folds its single-component forms into them: `background-position:
     * center` and `background-position: center center` are one value here, which
     * is what lets a consumer read the pair without re-deriving the folding rule.
     * Both components are always present when the kind is this one.
     */
    CssBackgroundPosition backgroundPosition;
    /**
     * BACKGROUND_SIZE: the keyword or pair (RoboCSSBackground.h); `auto`
     * otherwise — the property's initial value, and not a value to read without
     * the kind check, exactly as for @ref display and @ref position.
     */
    CssBackgroundSize backgroundSize;
    /**
     * BORDER_STYLE: the border style (RoboCSSBorder.h); NONE otherwise.
     *
     * A typed enumerator rather than the keyword's text, for the reason @ref
     * display and @ref position are: `border-top-style` is what a paint phase
     * dispatches on — `none` and `hidden` draw nothing, and a border that draws
     * nothing has a *used* width of zero whatever `border-width` says. Meaningful
     * only when @ref kind is BORDER_STYLE: a cleared record holds NONE, which is
     * the property's initial value and still not something to read without the
     * kind check, exactly as for @ref display.
     */
    CssBorderStyle borderStyle;
    /**
     * OVERFLOW: the overflow value (RoboCSSOverflow.h); VISIBLE otherwise.
     *
     * A typed enumerator rather than the keyword's text, for the reason @ref
     * display, @ref position and @ref borderStyle are: `overflow` is what a layout
     * pass dispatches on — whether an axis has a boundary — so it is read as a value
     * and never as a spelling. Meaningful only when @ref kind is OVERFLOW: a cleared
     * record holds VISIBLE, which is the property's *initial* value and therefore the
     * safest thing a cleared record could hold — and still not something to read
     * without the kind check, because "this element does not declare an overflow" and
     * "this element is visible" are different answers.
     */
    CssOverflow overflow;
    /**
     * OPACITY: the value (RoboCSSOpacity.h); `1` otherwise.
     *
     * A `double` in `[0, 1]` -- already clamped, so a consumer never applies the range rule
     * itself -- and a **separate field from @ref number** even though a cleared record leaves
     * both at zero-or-linear. That is deliberate: a length's magnitude and an opacity are
     * different facts about different things, and sharing one slot is how a consumer that
     * switched on the kind would come to read a `200px` width as an opacity of `200`. A
     * cleared record holds `1`, which is the property's *initial* value: "this element does
     * not declare an opacity" and "this element is fully opaque" are different answers, and
     * `1` is the one that renders the same way, which is exactly why it is worth saying that
     * they must not be conflated.
     */
    double opacity;
    /**
     * FLEX_DIRECTION: the direction (RoboCSSFlex.h); ROW otherwise.
     *
     * A typed enumerator rather than the keyword's text, for the reason @ref display
     * and @ref position are: `flex-direction` is what a layout pass dispatches on, so
     * it is read as a value rather than compared as a spelling. Meaningful only when
     * @ref kind is FLEX_DIRECTION: a cleared record holds ROW, which is the property's
     * *initial* value and therefore the safest thing a cleared record could hold — and
     * still not something to read without the kind check, because "this element does
     * not declare a direction" and "this element is a row" are different answers.
     */
    CssFlexDirection flexDirection;
    /**
     * FLEX_WRAP: the wrap (RoboCSSFlex.h); NOWRAP otherwise — the initial value, and
     * read together with the kind check for the reason @ref flexDirection is.
     */
    CssFlexWrap flexWrap;
    /**
     * FLEX_ALIGN: the alignment (RoboCSSFlex.h); NORMAL otherwise.
     *
     * NORMAL is legal for all four properties the vocabulary serves, which is what
     * makes it the safe default: a cleared record holds a value every one of them
     * could have declared. `align-self`'s initial is `auto`, and that is the
     * *registry's* answer rather than this default — see @ref CssComputedValueFlexAlign.
     */
    CssFlexAlign flexAlign;
    /**
     * GRID_AUTO_FLOW: the flow (RoboCSSGrid.h); ROW otherwise.
     *
     * A typed enumerator rather than the keyword's text, for the reason @ref
     * flexDirection is: `grid-auto-flow` is what a layout pass dispatches on, so it is
     * read as a value. Meaningful only when @ref kind is GRID_AUTO_FLOW: a cleared
     * record holds ROW, which is the property's *initial* value and therefore the
     * safest thing a cleared record could hold — and still not something to read
     * without the kind check, because "this element does not declare a flow" and "this
     * element auto-places by row" are different answers.
     */
    CssGridAutoFlow gridAutoFlow;
    /**
     * KEYWORD: the decoded identifier.
     * FAMILY_LIST: the family list as written, whitespace normalised.
     * INVALID: a short reason.
     * Owned, NUL-terminated; NULL when the kind does not use it.
     */
    char *text;
    /** Length of @p text in bytes, excluding its NUL. */
    size_t textLength;
    /**
     * PRESERVED: the specified tokens, owned (a deep copy).
     * KIND_NONE exactly when the kind does not use it.
     */
    CssValue preserved;
    /**
     * Non-zero when this value names something only a later stage can resolve.
     *
     * `var(--x)` is a reference, `calc(100% - 20px)` needs a containing block,
     * and `currentColor` needs the element's own computed `color` — which is why
     * it is a *contextual colour* rather than a channel set. All three are
     * legal, all three are carried intact, and none of them is a value *yet* —
     * which is a fact about the value that has to be recorded, because a
     * consumer that treated `var(--x)` as an ordinary preserved token list would
     * be reading a reference as if it were a colour.
     */
    int unresolved;
} CssComputedValue;

/** Clear @p value to NONE. Safe on NULL. */
void CssComputedValueInit(CssComputedValue *value);

/** Free @p value's owned members and reset it. Idempotent; safe on NULL. */
void CssComputedValueDispose(CssComputedValue *value);

/**
 * Move @p value into @p outValue, leaving @p value NONE.
 * @return RoboOk; RoboInvalidArgument for a NULL argument or for
 *         `value == outValue`.
 */
Result CssComputedValueMove(CssComputedValue *value,
                                          CssComputedValue *outValue,
                                          CssDiag *diag);

/**
 * Deep-copy @p value into @p outValue, which is disposed first.
 * Transactional: on failure @p outValue is left NONE rather than half-copied.
 */
Result CssComputedValueCopy(CssComputedValue *outValue,
                                          const CssComputedValue *value,
                                          CssDiag *diag);

/** Non-zero when @p value is NONE. NULL → 1. */
int CssComputedValueIsEmpty(const CssComputedValue *value);

/* --- construction --- */

/** Make @p value a KEYWORD (the text is copied). */
Result CssComputedValueMakeKeyword(CssComputedValue *value,
                                                  CssSlice keyword,
                                                  CssDiag *diag);

/** Make @p value a NUMBER. */
Result CssComputedValueMakeNumber(CssComputedValue *value,
                                                 double number,
                                                 int isInteger,
                                                 CssDiag *diag);

/** Make @p value a PERCENTAGE. */
Result CssComputedValueMakePercentage(CssComputedValue *value,
                                                     double percentage,
                                                     int isInteger,
                                                     CssDiag *diag);

/** Make @p value a LENGTH. @p unit must not be NONE. */
Result CssComputedValueMakeLength(CssComputedValue *value,
                                                 double number,
                                                 CssUnit unit,
                                                 int isInteger,
                                                 CssDiag *diag);

/** Make @p value a COLOR. */
Result CssComputedValueMakeColor(CssComputedValue *value,
                                                CssColor color,
                                                CssDiag *diag);

/** Make @p value a FAMILY_LIST (the list text is copied). */
Result CssComputedValueMakeFamilyList(CssComputedValue *value,
                                                       CssSlice families,
                                                       CssDiag *diag);

/** Make @p value a DISPLAY holding @p display. */
Result CssComputedValueMakeDisplay(CssComputedValue *value,
                                                   CssDisplay display,
                                                   CssDiag *diag);

/** Make @p value a POSITION holding @p position. */
Result CssComputedValueMakePosition(CssComputedValue *value,
                                                    CssPositioning position,
                                                    CssDiag *diag);

/** Make @p value a BACKGROUND_POSITION holding @p position (copied by value). */
Result CssComputedValueMakeBackgroundPosition(
    CssComputedValue *value,
    CssBackgroundPosition position,
    CssDiag *diag);

/** Make @p value a BACKGROUND_SIZE holding @p size (copied by value). */
Result CssComputedValueMakeBackgroundSize(
    CssComputedValue *value,
    CssBackgroundSize size,
    CssDiag *diag);

/** Make @p value a BORDER_STYLE holding @p style. */
Result CssComputedValueMakeBorderStyle(CssComputedValue *value,
                                                        CssBorderStyle style,
                                                        CssDiag *diag);

/** Make @p value an OVERFLOW holding @p overflow. */
Result CssComputedValueMakeOverflow(CssComputedValue *value,
                                                    CssOverflow overflow,
                                                    CssDiag *diag);

/**
 * Make @p value an OPACITY holding @p opacity.
 *
 * @p opacity must be a value (`CssOpacityIsValid`): finite and inside `[0, 1]`. A
 * value **outside** the range is refused rather than clamped here, and the two are different
 * mistakes: CSS's clamp is the *compute* layer's rule for a declaration an author wrote
 * (`opacity: 2` is legal CSS meaning `1`), while a caller handing this door `2` is a caller
 * that skipped the rule. The clamp is one call away when that is what was meant, and a
 * make-door that silently fixed an out-of-range argument would be one whose output a caller
 * could not predict from its input.
 */
Result CssComputedValueMakeOpacity(CssComputedValue *value,
                                                  double opacity,
                                                  CssDiag *diag);

/** Make @p value a FLEX_DIRECTION holding @p direction. */
Result CssComputedValueMakeFlexDirection(CssComputedValue *value,
                                                       CssFlexDirection direction,
                                                       CssDiag *diag);

/** Make @p value a FLEX_WRAP holding @p wrap. */
Result CssComputedValueMakeFlexWrap(CssComputedValue *value,
                                                  CssFlexWrap wrap,
                                                  CssDiag *diag);

/** Make @p value a FLEX_ALIGN holding @p align. */
Result CssComputedValueMakeFlexAlign(CssComputedValue *value,
                                                   CssFlexAlign align,
                                                   CssDiag *diag);

/** Make @p value a GRID_AUTO_FLOW holding @p flow. */
Result CssComputedValueMakeGridAutoFlow(CssComputedValue *value,
                                                     CssGridAutoFlow flow,
                                                     CssDiag *diag);

/** Make @p value a PRESERVED copy of @p specified. */
Result CssComputedValueMakePreserved(CssComputedValue *value,
                                                     const CssValue *specified,
                                                     CssDiag *diag);

/** Make @p value INVALID with @p reason as its explanation (copied). */
Result CssComputedValueMakeInvalid(CssComputedValue *value,
                                                   const char *reason,
                                                   CssDiag *diag);

/* --- reading --- */

/** Borrowed keyword/family/reason text, or an absent slice. */
CssSlice CssComputedValueText(const CssComputedValue *value);

/** Borrowed preserved tokens, or NULL when the value is not PRESERVED. */
const CssValue *CssComputedValuePreserved(
    const CssComputedValue *value);

/** The colour of a COLOR value; black-transparent for any other kind. */
CssColor CssComputedValueColor(const CssComputedValue *value);

/**
 * The display of a DISPLAY value.
 *
 * Answers NONE for any other kind, which is why it is read together with a kind
 * check rather than on its own — see the field's own note. `display: none` and
 * "this is not a display value" are different facts, and a caller that conflated
 * them would lay an out-of-scope property out as a removed box.
 */
CssDisplay CssComputedValueDisplay(const CssComputedValue *value);

/**
 * The position of a POSITION value.
 *
 * Answers STATIC for any other kind, which is why it is read together with a
 * kind check rather than on its own — see the field's own note. `position:
 * static` and "this is not a position value" are different facts, and STATIC is
 * the answer that would do the least damage if they were conflated, which is
 * exactly why it is worth saying that they must not be.
 */
CssPositioning CssComputedValuePosition(
    const CssComputedValue *value);

/**
 * The pair of a BACKGROUND_POSITION value; an absent pair for any other kind.
 *
 * Read together with a kind check, for the same reason the two above are: an
 * absent pair and `0% 0%` are different facts, and the second is a real position
 * an element can have.
 */
CssBackgroundPosition CssComputedValueBackgroundPosition(
    const CssComputedValue *value);

/**
 * The size of a BACKGROUND_SIZE value; `auto` for any other kind.
 *
 * Read together with a kind check. `background-size: auto` and "this is not a
 * size value" are different facts, and `auto` is also the property's initial
 * value — so it is the answer that would do the least damage if they were
 * conflated, which is exactly why it is worth saying that they must not be.
 */
CssBackgroundSize CssComputedValueBackgroundSize(
    const CssComputedValue *value);

/**
 * The style of a BORDER_STYLE value; `none` for any other kind.
 *
 * Read together with a kind check, for the reason the three above are: `none` is
 * both a real border style and the property's initial value, so it is the answer
 * that would do the least damage if the two were conflated — which is exactly why
 * it is worth saying that they must not be. A consumer that read it without the
 * kind check would see "this border did not draw" where the truth is "this is not
 * a border style at all".
 */
CssBorderStyle CssComputedValueBorderStyle(
    const CssComputedValue *value);

/**
 * The overflow of an OVERFLOW value; `visible` for any other kind.
 *
 * Read together with a kind check, for the reason the four above are: `visible` is
 * both a real overflow and the property's initial value, so it is the answer that
 * would do the least damage if the two were conflated — which is exactly why it is
 * worth saying that they must not be. A consumer that read it without the kind check
 * would see "this axis does not clip" where the truth is "this is not an overflow
 * value at all".
 */
CssOverflow CssComputedValueOverflow(
    const CssComputedValue *value);

/**
 * The value of an OPACITY value; `1` (fully opaque) for any other kind.
 *
 * Read together with a kind check, for the reason the five above are -- and this is the one
 * where the choice of default is load-bearing rather than tidy. Every other accessor here
 * answers its property's initial value, which is the answer that would do the least damage
 * if a caller conflated "nothing was declared" with "it was declared and is the initial
 * value". For opacity that is **one**, and a build that answered zero would make every
 * element of every document that never mentions `opacity` invisible -- a failure that is
 * silent, because the geometry would still be right and only the page would be blank.
 * `RoboCSSOpacity.h` names that trap; this door is where it is closed.
 */
double CssComputedValueOpacity(const CssComputedValue *value);

/**
 * The direction of a FLEX_DIRECTION value; `row` for any other kind.
 *
 * Read together with a kind check, for the reason the accessors above are: `row` is
 * both a real direction and the property's initial value, so it is the answer that
 * would do the least damage if the two were conflated — which is exactly why it is
 * worth saying that they must not be. A consumer that read it without the kind check
 * would see "this container's main axis is the inline axis" where the truth is "this
 * is not a flex-direction value at all".
 */
CssFlexDirection CssComputedValueFlexDirection(
    const CssComputedValue *value);

/**
 * The wrap of a FLEX_WRAP value; `nowrap` for any other kind.
 *
 * Read together with a kind check; `nowrap` is a real value and the property's
 * initial value, exactly as `row` above is.
 */
CssFlexWrap CssComputedValueFlexWrap(const CssComputedValue *value);

/**
 * The alignment of a FLEX_ALIGN value; `normal` for any other kind.
 *
 * Read together with a kind check; `normal` is a real value and the initial value of
 * three of the four properties the vocabulary serves, which is what makes it the safe
 * default. `align-self`'s initial is `auto` and is written by its registry row rather
 * than here — see @ref CssComputedValueFlexAlign.
 */
CssFlexAlign CssComputedValueFlexAlign(const CssComputedValue *value);

/**
 * The flow of a GRID_AUTO_FLOW value; `row` for any other kind.
 *
 * Read together with a kind check; `row` is both a real flow and the property's
 * initial value, exactly as `row` is for @ref CssComputedValueFlexDirection.
 */
CssGridAutoFlow CssComputedValueGridAutoFlow(
    const CssComputedValue *value);

/**
 * Non-zero when @p a and @p b are the same computed value.
 *
 * Structural, and total: two PRESERVED values are equal when their trees are
 * equal (kind, token text, children, recursively), two LENGTHs when the unit and
 * the number agree, and values of different kinds are never equal. This is what
 * makes a computed style testable without serialising it, and it is why the
 * representation uses integers for channels rather than floats.
 */
int CssComputedValueEquals(const CssComputedValue *a,
                                   const CssComputedValue *b);

/**
 * Write @p value's canonical text into @p buffer, NUL-terminated.
 *
 * The text is a *diagnostic* rendering, not CSS serialisation: it is stable,
 * locale-independent and exactly reversible for numbers, but a PRESERVED value
 * renders as its token text and an INVALID one as its reason. Rationale: this
 * exists so that a test can say what it expected and a developer tool can print
 * what it got, and neither needs the CSSOM's serialisation rules (which are a
 * later phase's subject).
 *
 * @param buffer    destination, at least @p capacity bytes.
 * @param capacity  bytes available.
 * @param outLength non-NULL; receives the length written, excluding the NUL.
 * @param diag      optional diagnostic sink.
 * @return RoboOk; RoboInvalidArgument for a NULL argument;
 *         RoboNotSupported when the text does not fit, in which case
 *         @p outLength receives the length it would have needed.
 */
Result CssComputedValueWrite(const CssComputedValue *value,
                                            char *buffer,
                                            size_t capacity,
                                            size_t *outLength,
                                            CssDiag *diag);

/* ------------------------------------------------------------------ */
/* Computing a specified value                                         */
/* ------------------------------------------------------------------ */

/**
 * Turn a specified value into a computed one for a property of @p computation.
 *
 * The one normalising function, and the only place a token sequence becomes
 * semantic. It is a function of (the value's shape, the specified tree) and
 * nothing else: no context, no parent, no DOM — which is what makes it
 * testable and what keeps the inheritance story (\"a child inherits its parent's
 * *computed* value\") a property of the caller rather than of this function.
 *
 * @p computation comes from the property's definition
 * (RoboCSSPropertyRegistry.h) or, for a property with no definition — a custom
 * property, or a name nothing classifies — is
 * RoboCssPropertyComputationPreserve.
 *
 * What each shape accepts, and what it does with anything else:
 *
 *   COLOUR       anything `CssColorFromValue` (RoboCSSColor.h) reads as
 *                a colour: an ident (a named colour or `transparent`), one hash
 *                of 3/4/6/8 digits, `rgb()`/`rgba()` or `hsl()`/`hsla()` in the
 *                comma or space syntax with numbers or percentages and an
 *                optional alpha. `currentColor` becomes a contextual COLOR
 *                value — typed, and flagged unresolved — and a colour function
 *                this build cannot represent is carried PRESERVED the same way.
 *                Anything else is INVALID, with the diagnostic saying whether it
 *                was a malformed colour call or not a colour at all.
 *   LENGTH       one dimension with a known unit (absolute units converted to
 *                px), one bare `0`, one percentage, or one ident (`auto`,
 *                `normal`, `medium`). A negative value is accepted and kept:
 *                whether `-10px` is legal is the property's rule, and this
 *                function has the *shape*, not the rule.
 *   KEYWORD      exactly one ident, decoded. Anything else is INVALID.
 *   FAMILY_LIST  a comma-separated list of strings and idents. The items are
 *                kept in order and joined with `, ` so the value is a list and
 *                not a coincidence of token order.
 *   FONT_WEIGHT  `normal`, `bold`, `lighter`, `bolder`, or a number.
 *   LINE_HEIGHT  `normal`, a number, a dimension or a percentage.
 *   DISPLAY      exactly one identifier, and it must be one of the vocabulary
 *                (RoboCSSDisplay.h): `none`, `block`, `inline` or `inline-block`,
 *                case-insensitively. Anything else is INVALID with the reason
 *                saying whether it was not an identifier or not a display value —
 *                `display: 20px` and `display: bogus` are different mistakes. A
 *                reference (`display: var(--d)`) is carried PRESERVED and flagged
 *                unresolved, and the multi-keyword syntax is refused rather than
 *                carried, because a display value nothing can read is worse than
 *                one CSS's own recovery rule drops.
 *   POSITION     the same shape over a different vocabulary
 *                (RoboCSSPosition.h): `static`, `relative`, `absolute`, `fixed`
 *                or `sticky`, case-insensitively, with the same three refusals
 *                and the same treatment of a reference. It shares the DISPLAY
 *                body rather than restating it, because a second copy of "ask the
 *                identifier, then the vocabulary" is how two closed vocabularies
 *                come to disagree about which of the two they asked.
 *   Z_INDEX      `auto`, or a whole number: `z-index: -1` and `z-index: 0` are
 *                NUMBER values (with `isInteger` set), `z-index: auto` is the
 *                KEYWORD. `1.5` and `1.0` are both INVALID — CSS's `<integer>` is
 *                a grammar, not a value that happens to be whole — and the reason
 *                names which of the two the author wrote. A very large magnitude
 *                is refused too, by the unit module's own representable-integer
 *                bound rather than by a limit invented here.
 *   PRESERVE     the tree, copied. Never invalid: a property whose shape is not
 *                claimed cannot have a value that is wrong for it.
 *
 * @p out is disposed first and is left NONE whenever the call fails to produce
 * anything at all.
 *
 * @return RoboOk whenever a computed value was produced — **including** an
 *         INVALID one, which is an answer about the value and not a failure of
 *         this call; RoboInvalidArgument for a NULL argument or an unknown
 *         @p computation; RoboOutOfMemory when nothing could be built.
 *         A diagnostic is recorded for an INVALID result, naming the property
 *         shape and the reason, so a caller can report *why*.
 */
Result CssComputedValueCompute(
    CssPropertyComputation computation,
    const CssValue *specified,
    CssComputedValue *out,
    CssDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_COMPUTED_VALUE_H */
