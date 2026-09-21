/*
 * RoboCSS background vocabulary (Phase 9.14).
 *
 * A background is the first property *family* whose parts are not the same kind
 * of thing. The box model's four edges are four of one value; `background` is one
 * declaration that speaks for six properties of six different shapes — a colour,
 * an image reference, a repeat style, a pair of positions, a pair of sizes and an
 * attachment. That is what makes it a different module from RoboCSSBox.h rather
 * than another row in it: there is no positional mapping from one component to
 * one longhand, because a component's *kind* is what decides where it goes.
 *
 * Four things live here and nothing else:
 *
 *   the vocabularies   `background-repeat` (four values) and `background-attachment`
 *                      (two), each a closed set with one row per value
 *   the position pair  two components, each a keyword on its axis or a length or
 *                      a percentage — the thing `<length-percentage>` alone cannot
 *                      express, because `left top` and `10px 20px` are the same
 *                      *property* and different *values*
 *   the size pair      `auto`, `cover`, `contain`, or one or two components that
 *                      are each `auto` or a length or a percentage
 *   the shorthand      the six longhands `background` stands for, and how one
 *                      layer's components distribute over them
 *
 * The repeat vocabulary is a table with two columns, not four spellings
 *
 * `repeat-x` is not a fifth behaviour: it is "repeat horizontally, do not repeat
 * vertically", and `repeat-y` is the mirror of it. So the row carries the two
 * *axes* rather than a flag, exactly as the display vocabulary carries an outer
 * and an inner display and the positioning vocabulary carries in-flow and
 * positioned. `repeats_on_axis` is derived from the row, so a value added later
 * (`space`, `round` — CSS's other two) answers both questions for itself, and no
 * function below compares a spelling to decide anything.
 *
 * The position pair, and why a keyword's meaning depends on its axis
 *
 * `background-position: left top` names an edge on each axis; `10px 20px` names an
 * offset on each. Both are two components, and the *component* is what differs:
 *
 *   an edge        CENTER / START / END, where `left` is START horizontally and
 *                  `top` is START vertically — so the axis is what spells it, and
 *                  `left` in the vertical slot is a mistake rather than a synonym
 *   a length       a number with a unit, kept as one
 *   a percentage   `25%`, kept as one, because only layout knows 25% of what
 *
 * The pair therefore carries a *per-axis* component rather than one shape with two
 * optional halves, and the single-value form is folded during parsing rather than
 * carried: `center` computes to `center center` and `10px` to `10px center`,
 * because the computed value of `background-position` is a pair however it was
 * written. What is *not* folded is the edge-relative form (`left 10px`,
 * `right 2em`) and the three- and four-component spellings: those are a different
 * grammar whose component count decides the meaning, and this build refuses them
 * rather than guessing at a reading.
 *
 * What it deliberately is not
 *
 * Not a property registry: nothing here classifies a property or gives it an
 * initial value (RoboCSSPropertyRegistry.h). Not a value parser: what arrives is
 * a `CssValue` — a component-value tree the grammar already built — and
 * the tokens inside it are the tokenizer's. And not layout or paint: nothing
 * below resolves a percentage, loads an image, measures a box or draws anything.
 * `background-size: 50% 25%` stays two percentages because only the phase that
 * knows the box can say what they are 50% *of*.
 *
 * What it does not implement, and says so
 *
 * Multiple background *layers* (`background: url(a.png), url(b.png)`) are legal
 * CSS and are refused here, with a reason, because the shorthand grammar for them
 * is a comma-separated list of layers and each layer is a whole set of longhands —
 * which is a value model this build does not have. An image *reference* is carried
 * rather than resolved: `url("a.png")` is a reference this build can hold and
 * cannot fetch, which is a fact about the value rather than a reason to lose the
 * declaration (the same answer 9.9 gives `oklch()`).
 */

#ifndef ROBO_CSS_BACKGROUND_H
#define ROBO_CSS_BACKGROUND_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSUnit.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The repeat vocabulary                                               */
/* ------------------------------------------------------------------ */

/**
 * A computed `background-repeat`.
 *
 * A closed set, and the enumerator values are append-only because a stored
 * enumerator is a contract: CSS's other two (`space`, `round`) join as 4 and 5
 * rather than reshuffling what a previous phase wrote down. There is no "none"
 * enumerator, so unlike the display and positioning vocabularies a zero record
 * reads as `repeat` — the property's *initial* value, which is the safest thing
 * a cleared record could hold and still not something to read without checking
 * the containing record's kind.
 */
typedef enum CssBackgroundRepeat {
    /** `repeat`: both axes tile. The initial value. */
    RoboCssBackgroundRepeat = 0,
    /** `repeat-x`: horizontally only. */
    RoboCssBackgroundRepeatX = 1,
    /** `repeat-y`: vertically only. */
    RoboCssBackgroundRepeatY = 2,
    /** `no-repeat`: neither axis tiles. */
    RoboCssBackgroundNoRepeat = 3
} CssBackgroundRepeat;

/** Number of defined repeat values. */
#define RoboCssBackgroundRepeatCount 4u

/** Non-zero when @p repeat is one of the four. */
int CssBackgroundRepeatIsKnown(CssBackgroundRepeat repeat);

/** Human-readable name ("repeat-x"); never NULL, "" for unknown. */
const char *CssBackgroundRepeatName(CssBackgroundRepeat repeat);

/** Number of rows in the vocabulary. */
size_t CssBackgroundRepeatCount(void);

/**
 * The repeat value an identifier spells, or `repeat` when it spells none of them.
 *
 * ASCII case-insensitive, as CSS spells every keyword, and against the canonical
 * spellings only — so `REPEAT-X` matches and `repeatx` does not. `repeat` is both
 * a value and the answer for "not one of them", which is the ambiguity
 * @ref CssBackgroundRepeatSliceIsKnown exists to resolve: ask the
 * *identifier*, then read the value.
 */
CssBackgroundRepeat CssBackgroundRepeatFromSlice(CssSlice keyword);

/** NUL-terminated flavour of the above. */
CssBackgroundRepeat CssBackgroundRepeatFromText(const char *keyword);

/** Non-zero when @p keyword spells one of the vocabulary, case-insensitively. */
int CssBackgroundRepeatSliceIsKnown(CssSlice keyword);

/**
 * Write @p repeat's canonical keyword into @p buffer, NUL-terminated.
 *
 * One stable spelling per value: lowercase, hyphenated, `repeat-x` never the
 * enumerator's name.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param outLength non-NULL; receives the length written, excluding the NUL.
 * @param diag       optional diagnostic sink.
 * @return RoboOk; RoboInvalidArgument for a NULL argument or an unknown
 *         enumerator; RoboNotSupported when the keyword does not fit, in which
 *         case @p outLength receives the length it would have needed.
 */
Result CssBackgroundRepeatWrite(CssBackgroundRepeat repeat,
                                               char *buffer,
                                               size_t capacity,
                                               size_t *outLength,
                                               CssDiag *diag);

/** Capacity @ref CssBackgroundRepeatWrite needs: `no-repeat` (9) + NUL. */
#define RoboCssBackgroundRepeatTextMax 10u

/**
 * A background's axis.
 *
 * Two, and they are named rather than `x`/`y` because a position keyword is
 * spelled by its axis: START is `left` horizontally and `top` vertically, and a
 * function that has to pick a spelling needs to be told which.
 */
typedef enum CssBackgroundAxis {
    RoboCssBackgroundAxisHorizontal = 0,
    RoboCssBackgroundAxisVertical = 1
} CssBackgroundAxis;

/** Number of axes a background has. */
#define RoboCssBackgroundAxisCount 2u

/** Non-zero when @p axis is one of the two. */
int CssBackgroundAxisIsKnown(CssBackgroundAxis axis);

/** Human-readable axis name ("horizontal", "vertical"); never NULL. */
const char *CssBackgroundAxisName(CssBackgroundAxis axis);

/**
 * Whether @p repeat tiles @p axis — asked of the row, so a value added later
 * answers for itself.
 *
 * `repeat-x` is 1 horizontally and 0 vertically; `repeat` is 1 on both, and
 * `no-repeat` 0 on both. An unknown enumerator answers 0 rather than the first
 * row's answer.
 */
int CssBackgroundRepeatRepeatsOnAxis(CssBackgroundRepeat repeat,
                                               CssBackgroundAxis axis);

/* ------------------------------------------------------------------ */
/* The attachment vocabulary                                           */
/* ------------------------------------------------------------------ */

/**
 * A computed `background-attachment`.
 *
 * Two values, and the set is closed: CSS's third (`local`, which scrolls with
 * the element's own content rather than with the page) is a value this build has
 * not added, which is a different answer from a word nothing knows.
 */
typedef enum CssBackgroundAttachment {
    /** `scroll`: the background is fixed to the element's box. The initial value. */
    RoboCssBackgroundAttachmentScroll = 0,
    /** `fixed`: the background is fixed to the viewport. */
    RoboCssBackgroundAttachmentFixed = 1
} CssBackgroundAttachment;

/** Number of defined attachment values. */
#define RoboCssBackgroundAttachmentCount 2u

/** Non-zero when @p attachment is one of the two. */
int CssBackgroundAttachmentIsKnown(CssBackgroundAttachment attachment);

/** Human-readable name ("scroll"); never NULL, "" for unknown. */
const char *CssBackgroundAttachmentName(CssBackgroundAttachment attachment);

/** Number of rows in the vocabulary. */
size_t CssBackgroundAttachmentCount(void);

/** The attachment an identifier spells, or `scroll` when it spells none of them. */
CssBackgroundAttachment CssBackgroundAttachmentFromSlice(
    CssSlice keyword);

/** NUL-terminated flavour of the above. */
CssBackgroundAttachment CssBackgroundAttachmentFromText(
    const char *keyword);

/** Non-zero when @p keyword spells one of the vocabulary, case-insensitively. */
int CssBackgroundAttachmentSliceIsKnown(CssSlice keyword);

/** Write @p attachment's canonical keyword; same contract as the repeat writer. */
Result CssBackgroundAttachmentWrite(CssBackgroundAttachment attachment,
                                                   char *buffer,
                                                   size_t capacity,
                                                   size_t *outLength,
                                                   CssDiag *diag);

/** Capacity @ref CssBackgroundAttachmentWrite needs: `scroll` (6) + NUL. */
#define RoboCssBackgroundAttachmentTextMax 7u

/* ------------------------------------------------------------------ */
/* The position pair                                                   */
/* ------------------------------------------------------------------ */

/**
 * Where along an axis a position component sits, when it is a keyword.
 *
 * Three, and they are START/END rather than LEFT/RIGHT because the axis spells
 * them: START is `left` horizontally and `top` vertically, END is `right` and
 * `bottom`. CENTER is the one that means the same on both.
 */
typedef enum CssBackgroundEdge {
    /** `center`, on either axis. */
    RoboCssBackgroundEdgeCenter = 0,
    /** `left` / `top`. */
    RoboCssBackgroundEdgeStart = 1,
    /** `right` / `bottom`. */
    RoboCssBackgroundEdgeEnd = 2
} CssBackgroundEdge;

/** Number of defined edge positions. */
#define RoboCssBackgroundEdgeCount 3u

/** Non-zero when @p edge is one of the three. */
int CssBackgroundEdgeIsKnown(CssBackgroundEdge edge);

/**
 * Human-readable edge name, spelled for @p axis: `left`/`top` for START,
 * `right`/`bottom` for END, and `center` for CENTER on either.
 *
 * Never NULL; "" for an unknown edge or axis.
 */
const char *CssBackgroundEdgeName(CssBackgroundEdge edge,
                                          CssBackgroundAxis axis);

/**
 * The edge @p keyword names *on @p axis*, or CENTER when it names none of them.
 *
 * The axis is an argument rather than ignored because it is what decides whether
 * the keyword is a keyword at all: `top` names an edge on the vertical axis and
 * nothing on the horizontal one, which is why the question has a second door —
 * @ref CssBackgroundEdgeSliceIsKnown — so that "`top` in the horizontal
 * slot" is a refusal rather than a silent CENTER.
 */
CssBackgroundEdge CssBackgroundEdgeFromSlice(CssSlice keyword,
                                                               CssBackgroundAxis axis);

/** Non-zero when @p keyword names an edge on @p axis, case-insensitively. */
int CssBackgroundEdgeSliceIsKnown(CssSlice keyword,
                                            CssBackgroundAxis axis);

/** What a position component is. */
typedef enum CssBackgroundComponentKind {
    /** No component: a cleared record. */
    RoboCssBackgroundComponentNone = 0,
    /** An edge keyword; @ref CssBackgroundComponent.edge is live. */
    RoboCssBackgroundComponentEdge = 1,
    /** A length; @ref CssBackgroundComponent.numeric is live. */
    RoboCssBackgroundComponentLength = 2,
    /** A percentage; @ref CssBackgroundComponent.numeric is live. */
    RoboCssBackgroundComponentPercentage = 3
} CssBackgroundComponentKind;

/** Number of defined component kinds. */
#define RoboCssBackgroundComponentKindCount 4u

/** Non-zero when @p kind is a defined enumerator. */
int CssBackgroundComponentKindIsKnown(CssBackgroundComponentKind kind);

/** Human-readable kind name ("edge", "percentage"); never NULL. */
const char *CssBackgroundComponentKindName(CssBackgroundComponentKind kind);

/**
 * One component of a `background-position`.
 *
 * A flat record of a tag, an enumerator and a numeric value, owning nothing —
 * the same shape `CssNumeric` and `CssColor` have, and for the
 * same reason: it can be copied, compared and stored without a lifetime rule.
 * `edge` is live for EDGE and `numeric` for LENGTH and PERCENTAGE, and nothing
 * outside this module constructs one, so a component cannot come to carry a
 * field its kind does not mean.
 */
typedef struct CssBackgroundComponent {
    CssBackgroundComponentKind kind;
    /** EDGE: where along the axis. */
    CssBackgroundEdge edge;
    /** LENGTH / PERCENTAGE: the magnitude and, for a length, the unit. */
    CssNumeric numeric;
} CssBackgroundComponent;

/** The absence of a component. */
CssBackgroundComponent CssBackgroundComponentNone(void);

/** Non-zero when @p component holds a value. */
int CssBackgroundComponentIsPresent(CssBackgroundComponent component);

/**
 * Kind-aware equality: two components are equal when they are the same kind and
 * the same value, so `left` and `0px` are different however equal they look.
 *
 * Across kinds it is 0, including for two absent components being asked — which
 * is deliberate, since "no component" is not a value something can equal.
 */
int CssBackgroundComponentEquals(CssBackgroundComponent a,
                                         CssBackgroundComponent b);

/**
 * Write @p component as @p axis spells it into @p buffer, NUL-terminated.
 *
 * An edge writes its axis spelling; a length its number and unit; a percentage
 * its number and `%`. One stable spelling per value, so two equal components
 * cannot print differently.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument, an unknown axis,
 *         an unknown edge or an absent component; RoboNotSupported when it
 *         does not fit, with @p outLength set to the length needed.
 */
Result CssBackgroundComponentWrite(CssBackgroundComponent component,
                                                  CssBackgroundAxis axis,
                                                  char *buffer,
                                                  size_t capacity,
                                                  size_t *outLength,
                                                  CssDiag *diag);

/**
 * A computed `background-position`: one component per axis, both always present.
 *
 * Both halves being required is the representation's whole content. CSS's
 * single-value forms are *folded* here rather than carried — `center` is
 * `center center` and `10px` is `10px center` — because the computed value of
 * this property is the pair, and a consumer that had to remember which half was
 * defaulted would be re-deriving a rule this module already applied.
 */
typedef struct CssBackgroundPosition {
    CssBackgroundComponent horizontal;
    CssBackgroundComponent vertical;
} CssBackgroundPosition;

/** The absence of a position: both components absent. */
CssBackgroundPosition CssBackgroundPositionNone(void);

/** Non-zero when *both* components hold a value — the only complete state. */
int CssBackgroundPositionIsPresent(CssBackgroundPosition position);

/** Component-wise equality; two incomplete positions are never equal. */
int CssBackgroundPositionEquals(CssBackgroundPosition a,
                                        CssBackgroundPosition b);

/**
 * Write @p position as `horizontal vertical` into @p buffer, NUL-terminated.
 *
 * Both components, always, separated by one space — the computed value's own
 * spelling, whatever the declaration looked like. `center` prints `center center`.
 */
Result CssBackgroundPositionWrite(CssBackgroundPosition position,
                                                 char *buffer,
                                                 size_t capacity,
                                                 size_t *outLength,
                                                 CssDiag *diag);

/* ------------------------------------------------------------------ */
/* The size pair                                                       */
/* ------------------------------------------------------------------ */

/** What a computed `background-size` is. */
typedef enum CssBackgroundSizeKind {
    /** `auto`: the image's intrinsic size. The initial value. */
    RoboCssBackgroundSizeAuto = 0,
    /** `cover`: scaled to cover the box, aspect preserved. */
    RoboCssBackgroundSizeCover = 1,
    /** `contain`: scaled to fit inside the box, aspect preserved. */
    RoboCssBackgroundSizeContain = 2,
    /** Two components; @ref CssBackgroundSize.width / .height are live. */
    RoboCssBackgroundSizePair = 3
} CssBackgroundSizeKind;

/** Number of defined size kinds. */
#define RoboCssBackgroundSizeKindCount 4u

/** Non-zero when @p kind is a defined enumerator. */
int CssBackgroundSizeKindIsKnown(CssBackgroundSizeKind kind);

/** Human-readable kind name ("auto", "cover", "pair"); never NULL. */
const char *CssBackgroundSizeKindName(CssBackgroundSizeKind kind);

/**
 * One component of a `background-size` pair: `auto`, a length, or a percentage.
 *
 * `auto` is a flag rather than a third `CssNumeric` kind because it is
 * not a length: it says "keep the image's own size", which is a different
 * statement from any number, and one the module that has an image will read.
 */
typedef struct CssBackgroundSizeComponent {
    /** Non-zero when this component is `auto`; @ref numeric is then NONE. */
    int isAuto;
    /** LENGTH / PERCENTAGE, when it is not `auto`. */
    CssNumeric numeric;
} CssBackgroundSizeComponent;

/**
 * A computed `background-size`.
 *
 * Either one of the three whole-value keywords or a pair, and never both: `cover`
 * is not a pair of anything, and a pair whose components were both `auto` is
 * written `auto` — which is what makes `background-size: auto` and
 * `background-size: auto auto` the *same* computed value rather than two that
 * print differently. The single-component form is folded for the same reason
 * `background-position`'s is: CSS says a single width leaves the height `auto`.
 */
typedef struct CssBackgroundSize {
    CssBackgroundSizeKind kind;
    /** PAIR only. */
    CssBackgroundSizeComponent width;
    /** PAIR only. */
    CssBackgroundSizeComponent height;
} CssBackgroundSize;

/** The absence of a size: a cleared record. */
CssBackgroundSize CssBackgroundSizeNone(void);

/** Non-zero when @p size holds a value — a keyword, or a pair with both halves. */
int CssBackgroundSizeIsPresent(CssBackgroundSize size);

/** Kind-aware equality; two absent sizes are never equal. */
int CssBackgroundSizeEquals(CssBackgroundSize a,
                                    CssBackgroundSize b);

/**
 * Write @p size as the keyword or `width height` into @p buffer, NUL-terminated.
 *
 * `auto` prints `auto`, not `auto auto`: the two are one computed value and have
 * one spelling.
 */
Result CssBackgroundSizeWrite(CssBackgroundSize size,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *outLength,
                                             CssDiag *diag);

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/**
 * What happened when a background-shaped value was read.
 *
 * Four outcomes, and the third is the one that keeps this phase honest.
 * DEFERRED means "legal CSS this build has no representation for" — a `var()`
 * reference, or a colour function 9.9 already defers — and such a declaration is
 * *carried* with its tokens rather than refused. INVALID means the value means
 * nothing here at all. They are separate because the diagnostic differs and the
 * diagnostic is the only reason the outcome exists: an author who wrote
 * `background-position: var(--p)` needs a different sentence from one who wrote
 * `background-position: banana`.
 */
typedef enum CssBackgroundParse {
    /** Not a background-shaped value. */
    RoboCssBackgroundParseInvalid = 0,
    /** A position: @p out holds both components. */
    RoboCssBackgroundParsePosition = 1,
    /** A size: @p out holds a keyword or a pair. */
    RoboCssBackgroundParseSize = 2,
    /** Legal CSS with no representation here; keep the tokens. */
    RoboCssBackgroundParseDeferred = 3
} CssBackgroundParse;

/** Number of defined outcomes. */
#define RoboCssBackgroundParseCount 4u

/** Non-zero when @p outcome is a defined enumerator. */
int CssBackgroundParseIsKnown(CssBackgroundParse outcome);

/** Human-readable outcome name ("position", "deferred"); never NULL. */
const char *CssBackgroundParseName(CssBackgroundParse outcome);

/**
 * Read @p specified as a `background-position`, writing the pair to @p out.
 *
 * Accepted: one or two *significant* components, where each is an edge keyword on
 * its own axis, a length, or a percentage; or two components that are both lengths
 * or percentages. A single component names one axis and leaves the other at
 * CENTER, as CSS says it does.
 *
 * Refused, each for its own reason: the edge-relative form (`left 10px`,
 * `right 2em`), which is a four-component grammar and not a two-component one;
 * three or more components; a bare number, which is not a position because it has
 * no unit; two keywords on the same axis (`left right`); an identifier that names
 * no edge; and anything that is not a component at all.
 *
 * @p out is cleared first and left absent on any outcome but POSITION.
 */
CssBackgroundParse CssBackgroundPositionFromValue(
    const CssValue *specified,
    CssBackgroundPosition *out);

/**
 * Read @p specified as a `background-size`, writing the value to @p out.
 *
 * Accepted: `auto`, `cover`, `contain`, or one or two components that are each
 * `auto`, a length or a percentage. A single component sets the width and leaves
 * the height `auto`.
 *
 * Refused: three or more components; a bare number; a keyword that is not one of
 * the three; and anything that is not a component at all.
 */
CssBackgroundParse CssBackgroundSizeFromValue(
    const CssValue *specified,
    CssBackgroundSize *out);

/* ------------------------------------------------------------------ */
/* The shorthand                                                       */
/* ------------------------------------------------------------------ */

/**
 * Number of longhands `background` stands for.
 *
 * Six, and the order is the order a serialisation would write them in — image,
 * position, size, repeat, attachment, colour — because that is the order CSS
 * itself serialises the shorthand, and a table whose order is arbitrary is a
 * table two readers can disagree about.
 */
#define RoboCssBackgroundLonghandCount 6u

/** Number of longhands `background` stands for. */
size_t CssBackgroundLonghandCount(void);

/**
 * The longhand at @p index, or NULL when out of range. Borrowed and static.
 *
 * The names are spelled here rather than derived, because the registry's rows and
 * this table are the two places that must agree about them and this is the one
 * place that can be walked.
 */
const char *CssBackgroundLonghandName(size_t index);

/** Non-zero when @p name is one of the six longhands, case-insensitively. */
int CssBackgroundIsLonghand(CssSlice name);

/** The shorthand's own name: `background`. Never NULL. */
const char *CssBackgroundShorthandName(void);

/**
 * Distribute one `background:` layer over the six longhands.
 *
 * @p outParts is indexed by the order @ref CssBackgroundLonghandName
 * uses, and each element is either a *list* holding that longhand's component (or
 * its one or two components, for a pair) or KIND_NONE when the shorthand did not
 * mention it.
 *
 * KIND_NONE is not "leave it alone": CSS's `background` is a shorthand for every
 * one of the six, so a component the author did not write means the longhand's
 * *initial value*, and the caller applies that. This function deliberately does
 * not supply it — the initial values live in the registry, and a module that
 * invented them would be a second place they are written down.
 *
 * The classification, which is what makes this a different machine from the box
 * model's positional mapping:
 *
 *   a colour function, or a colour name      `background-color`
 *   any other function, or a URL token       `background-image` (a reference)
 *   `none`                                   `background-image`
 *   a repeat keyword                         `background-repeat`
 *   an attachment keyword                    `background-attachment`
 *   a position keyword, length or percentage `background-position`
 *   after a `/`                              `background-size`
 *
 * Two components for the same longhand are folded into one part where the grammar
 * allows it (two position components, two size components) and refused where it
 * does not (two colours, two images, two repeats, two attachments).
 *
 * Refused, with a diagnostic and every output left KIND_NONE: more than one layer
 * (a top-level comma); a `var()` or `env()` component, whose substitution can
 * change how many components there are; `cover`, `contain` or `auto` written
 * without a `/`, which is where CSS's grammar puts them; more than one `/`; a
 * component that belongs to no longhand; a `/` with nothing after it; and two
 * components the same longhand cannot take.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported
 *         for a value that cannot be distributed (the author's declaration
 *         survives on the shorthand's own entry either way); RoboOutOfMemory.
 */
Result CssBackgroundExpand(
    const CssValue *value,
    CssValue outParts[RoboCssBackgroundLonghandCount],
    CssDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_BACKGROUND_H */
