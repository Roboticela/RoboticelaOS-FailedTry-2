/*
 * RoboCSS text vocabulary (Phase 18.15).
 *
 * The text family is the fifth property *family* this build types, and it is the
 * first whose central fact is a **pair of booleans hiding in six words**. CSS
 * spells `white-space` as `normal | pre | nowrap | pre-wrap | pre-line |
 * break-spaces`, and what those six actually say is three independent things:
 *
 *   does runs of spaces collapse?        normal, nowrap, pre-line
 *   are newlines preserved?              pre, pre-wrap, pre-line, break-spaces
 *   does a line wrap at all?             everything but pre and nowrap
 *
 * `normal` and `pre-line` agree on the first and third and differ only on the
 * second; `pre-wrap` and `break-spaces` agree on all three and differ only on
 * whether a *preserved* space may break — which is why the table carries four
 * columns for six words and why a consumer reads a classification rather than
 * matching a spelling. A build that stored the keyword and tested it would have to
 * know that `pre-line` is not "pre with wraps".
 *
 * The second fact is that **`text-align`'s values are two kinds of statement**.
 * `start` and `end` are *logical*: which physical edge they mean depends on the
 * writing mode's direction (18.13), so they are the values that stay true when a
 * document's direction changes. `left` and `right` are *physical*: they name an
 * edge and mean it. `center` is neither, and `justify` is a third thing again. The
 * two axis predicates are therefore asked separately, exactly as `caption-side`'s
 * block-axis and inline-axis pair is, because "not logical" must not be read as
 * "physical".
 *
 * Fourteen closed sets live here, each a table of rows every accessor reads:
 *
 *   typed — the property's value *is* the closed set:
 *     text-align              start | end | left | right | center | justify | match-parent
 *     text-align-last         auto | start | end | left | right | center | justify
 *     text-justify            auto | inter-word | inter-character | none
 *     text-transform          none | capitalize | uppercase | lowercase |
 *                             full-width | full-size-kana
 *     white-space             normal | pre | nowrap | pre-wrap | pre-line | break-spaces
 *     word-break              normal | break-all | keep-all | break-word
 *     overflow-wrap           normal | break-word | anywhere
 *     line-break              auto | loose | normal | strict | anywhere
 *     hyphens                 none | manual | auto
 *     text-decoration-style   solid | double | dotted | dashed | wavy
 *
 *   classified — the value's grammar is *wider* than the set, so the declaration
 *   is carried and the set types what is inside it:
 *     text-decoration-line    none | underline | overline | line-through | blink
 *     text-overflow           clip | ellipsis   (plus a string this build carries)
 *     text-decoration-thickness  auto | from-font   (plus a length)
 *     text-underline-position auto | under | left | right
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a line breaker, a justifier, a hyphenator or a
 * text transformer: nothing wraps a line, distributes slack, finds a hyphenation
 * point or changes a character's case, and no function below takes or returns a
 * string, a line or a rectangle. Not a shaper: no glyph id, no run and no font
 * appears here, and `text-orientation` and `direction` are 18.13's. A value here
 * says *what rule* a text pass follows; where the line breaks and the glyphs land
 * belongs to the phase that has a text engine.
 *
 * Two families are deliberately **not typed**, and they are named rather than
 * implied: the `text-emphasis-*` marks (a shape list of eight kinds plus a
 * filled/open pair, with no renderer in this build that draws an emphasis mark) and
 * the decoration's *thickness* and *offset* grammars as whole values. Their rows are
 * registered as carried declarations so the cascade and the computed style stay
 * correct; the vocabularies are the phase that draws or measures them.
 */

#ifndef ROBO_CSS_TEXT_H
#define ROBO_CSS_TEXT_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* text-align                                                          */
/* ------------------------------------------------------------------ */

/**
 * A computed `text-align`.
 *
 * START is 0 because it is the property's initial value, so a cleared record holds
 * the state an author who declared nothing finds their text in — and it is also the
 * value that keeps meaning the same thing when a document's direction changes.
 */
typedef enum CssTextAlign {
    /** `start`: the inline-start edge of whatever direction is in force. */
    RoboCssTextAlignStart = 0,
    /** `end`: the inline-end edge. */
    RoboCssTextAlignEnd = 1,
    /** `left`: the physical left edge, whatever the direction says. */
    RoboCssTextAlignLeft = 2,
    /** `right`: the physical right edge. */
    RoboCssTextAlignRight = 3,
    /** `center`: the middle, which is neither an edge nor an axis. */
    RoboCssTextAlignCenter = 4,
    /** `justify`: both edges, with the slack distributed between the lines' content. */
    RoboCssTextAlignJustify = 5,
    /**
     * `match-parent`: the parent's *physical* alignment, re-interpreted in the
     * child's direction. A logical value, because which physical edge it resolves
     * to depends on the child's direction rather than on the word.
     */
    RoboCssTextAlignMatchParent = 6
} CssTextAlign;

/** Number of defined `text-align` values. */
#define RoboCssTextAlignCount 7u

int CssTextAlignIsKnown(CssTextAlign align);
const char *CssTextAlignName(CssTextAlign align);
size_t CssTextAlignCount(void);
CssTextAlign CssTextAlignFromSlice(CssSlice keyword);
CssTextAlign CssTextAlignFromText(const char *keyword);
int CssTextAlignSliceIsKnown(CssSlice keyword);

/**
 * Whether @p align names an edge **relative to the inline direction** (`start`,
 * `end`, `match-parent`).
 *
 * The value that stays true when a document's `direction` flips, which is why it is
 * read as a fact about the value rather than as a comparison against the word
 * `start`. An unknown value answers 0.
 */
int CssTextAlignIsLogical(CssTextAlign align);

/**
 * Whether @p align names a **physical** edge (`left`, `right`).
 *
 * Asked separately from @ref CssTextAlignIsLogical rather than as its negation,
 * because `center` and `justify` are neither: a build that read "not logical" as
 * "physical" would treat `center` as an edge it has to map through a direction.
 * An unknown value answers 0.
 */
int CssTextAlignIsPhysical(CssTextAlign align);

/** Whether @p align justifies (`justify`). An unknown value answers 0. */
int CssTextAlignIsJustified(CssTextAlign align);

Result CssTextAlignWrite(CssTextAlign align,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssTextAlignWrite needs ("match-parent" plus a NUL). */
#define RoboCssTextAlignTextMax 16u

/* ------------------------------------------------------------------ */
/* text-align-last                                                     */
/* ------------------------------------------------------------------ */

/**
 * A computed `text-align-last`.
 *
 * The same six values as `text-align` plus `auto`, which is the property's initial
 * and means *use whatever `text-align` says* — so a consumer that read `auto` as
 * "align the last line at the start" would be inventing an answer where the
 * cascade had already given one.
 */
typedef enum CssTextAlignLast {
    /** `auto`: whatever `text-align` resolves to. */
    RoboCssTextAlignLastAuto = 0,
    /** `start`: the inline-start edge. */
    RoboCssTextAlignLastStart = 1,
    /** `end`: the inline-end edge. */
    RoboCssTextAlignLastEnd = 2,
    /** `left`: the physical left edge. */
    RoboCssTextAlignLastLeft = 3,
    /** `right`: the physical right edge. */
    RoboCssTextAlignLastRight = 4,
    /** `center`. */
    RoboCssTextAlignLastCenter = 5,
    /** `justify`. */
    RoboCssTextAlignLastJustify = 6
} CssTextAlignLast;

/** Number of defined `text-align-last` values. */
#define RoboCssTextAlignLastCount 7u

int CssTextAlignLastIsKnown(CssTextAlignLast align);
const char *CssTextAlignLastName(CssTextAlignLast align);
size_t CssTextAlignLastCount(void);
CssTextAlignLast CssTextAlignLastFromSlice(CssSlice keyword);
CssTextAlignLast CssTextAlignLastFromText(const char *keyword);
int CssTextAlignLastSliceIsKnown(CssSlice keyword);

/**
 * Whether @p align defers to `text-align` (`auto`).
 *
 * The one row of this vocabulary that is not an alignment at all, and the reason a
 * consumer must ask before it reads the other three classifications: `auto` is not
 * "start". An unknown value answers 0.
 */
int CssTextAlignLastIsAutomatic(CssTextAlignLast align);

/** Whether @p align is direction-relative (`start`, `end`). Unknown answers 0. */
int CssTextAlignLastIsLogical(CssTextAlignLast align);

/** Whether @p align is a physical edge (`left`, `right`). Unknown answers 0. */
int CssTextAlignLastIsPhysical(CssTextAlignLast align);

/** Whether @p align justifies (`justify`). An unknown value answers 0. */
int CssTextAlignLastIsJustified(CssTextAlignLast align);

Result CssTextAlignLastWrite(CssTextAlignLast align,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag);

/** Capacity @ref CssTextAlignLastWrite needs ("justify" plus a NUL). */
#define RoboCssTextAlignLastTextMax 8u

/* ------------------------------------------------------------------ */
/* text-justify                                                        */
/* ------------------------------------------------------------------ */

/**
 * A computed `text-justify`, which says **how** `text-align: justify` justifies.
 *
 * The row worth reading is `none`: it does not mean "do not justify" as a property
 * of this value alone — it *turns off* the justification the alignment asked for, so
 * the same declaration that was aligned two edges apart spells its slack out. That
 * is why the classification is "justifies" rather than "is a method".
 */
typedef enum CssTextJustify {
    /** `auto`: the user agent's usual rule for the script. */
    RoboCssTextJustifyAuto = 0,
    /** `inter-word`: slack goes into the word gaps. */
    RoboCssTextJustifyInterWord = 1,
    /** `inter-character`: slack goes between characters. */
    RoboCssTextJustifyInterCharacter = 2,
    /** `none`: justification is switched off. */
    RoboCssTextJustifyNone = 3
} CssTextJustify;

/** Number of defined `text-justify` values. */
#define RoboCssTextJustifyCount 4u

int CssTextJustifyIsKnown(CssTextJustify justify);
const char *CssTextJustifyName(CssTextJustify justify);
size_t CssTextJustifyCount(void);
CssTextJustify CssTextJustifyFromSlice(CssSlice keyword);
CssTextJustify CssTextJustifyFromText(const char *keyword);
int CssTextJustifySliceIsKnown(CssSlice keyword);

/**
 * Whether @p justify lets justification happen at all.
 *
 * `auto`, `inter-word` and `inter-character` do; `none` does not, and it is the
 * only value that refuses. An unknown value answers 0.
 */
int CssTextJustifyJustifies(CssTextJustify justify);

/**
 * Whether @p justify distributes the slack **between characters** rather than
 * between words (`inter-character`).
 *
 * The difference matters to anything that counts break opportunities: an
 * inter-character justification can put a line break inside what a word-based
 * reader would call one word. An unknown value answers 0.
 */
int CssTextJustifyExpandsCharacters(CssTextJustify justify);

Result CssTextJustifyWrite(CssTextJustify justify,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssTextJustifyWrite needs ("inter-character" plus a NUL). */
#define RoboCssTextJustifyTextMax 16u

/* ------------------------------------------------------------------ */
/* text-transform                                                      */
/* ------------------------------------------------------------------ */

/**
 * A computed `text-transform`.
 *
 * Six rows, and the classifications are the point: `capitalize` is **not** a case
 * change of the whole string — it is a per-word rule about the first letter — while
 * `uppercase` and `lowercase` are. `full-width` changes *widths* rather than case,
 * and `full-size-kana` changes neither: it turns small kana into their full-size
 * forms, which is a *reading* change for ruby text.
 *
 * A build that treated all six as "the text is altered" would be unable to say which
 * layer has to do the work, and the answer is different for each group.
 */
typedef enum CssTextTransform {
    /** `none`: the text is left alone. */
    RoboCssTextTransformNone = 0,
    /** `capitalize`: the first letter of each word. */
    RoboCssTextTransformCapitalize = 1,
    /** `uppercase`. */
    RoboCssTextTransformUppercase = 2,
    /** `lowercase`. */
    RoboCssTextTransformLowercase = 3,
    /** `full-width`: a width change, not a case change. */
    RoboCssTextTransformFullWidth = 4,
    /** `full-size-kana`: the small kana become full size. */
    RoboCssTextTransformFullSizeKana = 5
} CssTextTransform;

/** Number of defined `text-transform` values. */
#define RoboCssTextTransformCount 6u

int CssTextTransformIsKnown(CssTextTransform transform);
const char *CssTextTransformName(CssTextTransform transform);
size_t CssTextTransformCount(void);
CssTextTransform CssTextTransformFromSlice(CssSlice keyword);
CssTextTransform CssTextTransformFromText(const char *keyword);
int CssTextTransformSliceIsKnown(CssSlice keyword);

/**
 * Whether @p transform changes a character's **case** (`capitalize`, `uppercase`,
 * `lowercase`).
 *
 * `capitalize` counts, and it is the row that makes this question worth asking: it
 * *cases* a letter — the first of each word — without being a rule about the string
 * as a whole. An unknown value answers 0.
 */
int CssTextTransformChangesCase(CssTextTransform transform);

/**
 * Whether @p transform changes a character's **width** (`full-width`).
 *
 * A different layer from a case change: a full-width `A` is a different scalar with
 * a different advance, so a consumer that ran the width map and the case map in one
 * step would lose the distinction the two values exist for. An unknown value
 * answers 0.
 */
int CssTextTransformChangesWidth(CssTextTransform transform);

/**
 * Whether @p transform changes the **sound** of the text (`full-size-kana`).
 *
 * The third and last group, included so that the three predicates tile the six rows
 * rather than leaving one unclassified. An unknown value answers 0.
 */
int CssTextTransformChangesSound(CssTextTransform transform);

Result CssTextTransformWrite(CssTextTransform transform,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag);

/** Capacity @ref CssTextTransformWrite needs ("full-size-kana" plus a NUL). */
#define RoboCssTextTransformTextMax 16u

/* ------------------------------------------------------------------ */
/* white-space                                                         */
/* ------------------------------------------------------------------ */

/**
 * A computed `white-space`.
 *
 * The family's central table, because six words are four questions:
 *
 * | value          | collapses spaces | preserves newlines | wraps | breaks after a preserved space |
 * |----------------|------------------|--------------------|-------|-------------------------------|
 * | `normal`       | yes              | no                 | yes   | —                             |
 * | `pre`          | no               | yes                | no    | no                            |
 * | `nowrap`       | yes              | no                 | no    | —                             |
 * | `pre-wrap`     | no               | yes                | yes   | no                            |
 * | `pre-line`     | yes              | yes                | yes   | —                             |
 * | `break-spaces` | no               | yes                | yes   | yes                           |
 *
 * Two pairs in that table are the reason the classifications exist. `normal` and
 * `pre-line` differ *only* in whether a newline survives, so "which one is this"
 * is not a question about collapsing or wrapping. And `pre-wrap` and
 * `break-spaces` agree on all three of the first columns and differ only in the
 * fourth — CSS Text 4 added `break-spaces` for exactly that one behaviour, so a
 * build with three columns could not tell them apart.
 */
typedef enum CssWhiteSpace {
    /** `normal`: collapse, do not preserve newlines, wrap. */
    RoboCssWhiteSpaceNormal = 0,
    /** `pre`: preserve everything, do not wrap. */
    RoboCssWhiteSpacePre = 1,
    /** `nowrap`: collapse, do not wrap. */
    RoboCssWhiteSpaceNowrap = 2,
    /** `pre-wrap`: preserve, wrap, no break after a preserved space. */
    RoboCssWhiteSpacePreWrap = 3,
    /** `pre-line`: collapse spaces, preserve newlines, wrap. */
    RoboCssWhiteSpacePreLine = 4,
    /** `break-spaces`: like `pre-wrap`, and a preserved space may break. */
    RoboCssWhiteSpaceBreakSpaces = 5
} CssWhiteSpace;

/** Number of defined `white-space` values. */
#define RoboCssWhiteSpaceCount 6u

int CssWhiteSpaceIsKnown(CssWhiteSpace whiteSpace);
const char *CssWhiteSpaceName(CssWhiteSpace whiteSpace);
size_t CssWhiteSpaceCount(void);
CssWhiteSpace CssWhiteSpaceFromSlice(CssSlice keyword);
CssWhiteSpace CssWhiteSpaceFromText(const char *keyword);
int CssWhiteSpaceSliceIsKnown(CssSlice keyword);

/**
 * Whether @p whiteSpace collapses a run of spaces into one.
 *
 * `normal`, `nowrap` and `pre-line` do. An unknown value answers **1**, because the
 * initial value collapses and a value this build cannot read must not be read as
 * "preserve every space", which is the direction that would reflow a document
 * nobody asked to reflow.
 */
int CssWhiteSpaceCollapsesSpaces(CssWhiteSpace whiteSpace);

/**
 * Whether @p whiteSpace preserves a **newline** as a forced line break.
 *
 * The column that tells `normal` from `pre-line`, which agree on everything else.
 * An unknown value answers 0.
 */
int CssWhiteSpacePreservesNewlines(CssWhiteSpace whiteSpace);

/** Whether @p whiteSpace wraps at all (`pre` and `nowrap` do not). Unknown answers 1. */
int CssWhiteSpaceWraps(CssWhiteSpace whiteSpace);

/**
 * Whether a **preserved space may be broken after** (`break-spaces`).
 *
 * The one behaviour that separates `break-spaces` from `pre-wrap`: a preserved space
 * in `pre-wrap` hangs at the end of the line and does not create a break
 * opportunity, while in `break-spaces` it takes up room and may. An unknown value
 * answers 0.
 */
int CssWhiteSpaceBreaksAfterPreservedSpace(CssWhiteSpace whiteSpace);

Result CssWhiteSpaceWrite(CssWhiteSpace whiteSpace,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag);

/** Capacity @ref CssWhiteSpaceWrite needs ("break-spaces" plus a NUL). */
#define RoboCssWhiteSpaceTextMax 16u

/* ------------------------------------------------------------------ */
/* word-break                                                          */
/* ------------------------------------------------------------------ */

/**
 * A computed `word-break`.
 *
 * `break-word` is the row worth reading: CSS Text 3 kept it for compatibility and
 * defines it as behaving like `overflow-wrap: anywhere` *together with*
 * `word-break: normal`, so it is a value that sets a property it does not belong to.
 * This build carries the value and classifies it as breaking within words, and the
 * phase that wraps lines is where the definition becomes behaviour.
 */
typedef enum CssWordBreak {
    /** `normal`: the script's own rules. */
    RoboCssWordBreakNormal = 0,
    /** `break-all`: a break is allowed between any two characters. */
    RoboCssWordBreakBreakAll = 1,
    /** `keep-all`: breaks inside a word are forbidden, CJK included. */
    RoboCssWordBreakKeepAll = 2,
    /** `break-word`: as `normal`, plus breaking within a word when nothing else fits. */
    RoboCssWordBreakBreakWord = 3
} CssWordBreak;

/** Number of defined `word-break` values. */
#define RoboCssWordBreakCount 4u

int CssWordBreakIsKnown(CssWordBreak wordBreak);
const char *CssWordBreakName(CssWordBreak wordBreak);
size_t CssWordBreakCount(void);
CssWordBreak CssWordBreakFromSlice(CssSlice keyword);
CssWordBreak CssWordBreakFromText(const char *keyword);
int CssWordBreakSliceIsKnown(CssSlice keyword);

/** Whether @p wordBreak may break inside a word (`break-all`, `break-word`). */
int CssWordBreakBreaksWithinWords(CssWordBreak wordBreak);

/**
 * Whether @p wordBreak forbids the breaks a script would otherwise allow
 * **inside a run of CJK** (`keep-all`).
 *
 * A separate question from breaking within a word, because `keep-all` is about
 * *disabling* opportunities a script normally has rather than about creating new
 * ones — a build with only the first predicate would read `keep-all` as "never
 * break inside a word", which is already true of `normal` for Latin. An unknown
 * value answers 0.
 */
int CssWordBreakForbidsScriptBreaks(CssWordBreak wordBreak);

Result CssWordBreakWrite(CssWordBreak wordBreak,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssWordBreakWrite needs ("break-word" plus a NUL). */
#define RoboCssWordBreakTextMax 12u

/* ------------------------------------------------------------------ */
/* overflow-wrap                                                       */
/* ------------------------------------------------------------------ */

/**
 * A computed `overflow-wrap` (the property formerly called `word-wrap`).
 *
 * Three rows, and the pair that matters is `break-word` against `anywhere`: they
 * break **identically** and differ only in whether the break opportunities count
 * towards the element's *intrinsic* minimum size. `anywhere` does; `break-word`
 * does not. So a build that classified them by "does it break" would answer 1 for
 * both and be unable to explain why a box with `anywhere` is narrower.
 */
typedef enum CssOverflowWrap {
    /** `normal`: an unbreakable string may overflow. */
    RoboCssOverflowWrapNormal = 0,
    /** `break-word`: break it if nothing else fits, ignoring it for intrinsic size. */
    RoboCssOverflowWrapBreakWord = 1,
    /** `anywhere`: break it, and count the opportunity when sizing the box. */
    RoboCssOverflowWrapAnywhere = 2
} CssOverflowWrap;

/** Number of defined `overflow-wrap` values. */
#define RoboCssOverflowWrapCount 3u

int CssOverflowWrapIsKnown(CssOverflowWrap overflowWrap);
const char *CssOverflowWrapName(CssOverflowWrap overflowWrap);
size_t CssOverflowWrapCount(void);
CssOverflowWrap CssOverflowWrapFromSlice(CssSlice keyword);
CssOverflowWrap CssOverflowWrapFromText(const char *keyword);
int CssOverflowWrapSliceIsKnown(CssSlice keyword);

/** Whether @p overflowWrap may break an unbreakable string (`break-word`, `anywhere`). */
int CssOverflowWrapBreaksWithinWords(CssOverflowWrap overflowWrap);

/**
 * Whether @p overflowWrap's break opportunities affect the element's **intrinsic
 * size** (`anywhere`).
 *
 * The second question, and the only thing that tells the two breaking values apart.
 * An unknown value answers 0.
 */
int CssOverflowWrapAffectsIntrinsicSize(CssOverflowWrap overflowWrap);

Result CssOverflowWrapWrite(CssOverflowWrap overflowWrap,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag);

/** Capacity @ref CssOverflowWrapWrite needs ("break-word" plus a NUL). */
#define RoboCssOverflowWrapTextMax 12u

/* ------------------------------------------------------------------ */
/* line-break                                                          */
/* ------------------------------------------------------------------ */

/**
 * A computed `line-break`.
 *
 * The strictness of the *script's own* line-breaking rules, which is why the
 * property is a three-step ladder plus two specials rather than a set of
 * independent switches: `loose`, `normal` and `strict` are the same rule at three
 * settings, `auto` is "whatever the user agent picks", and `anywhere` is "do not
 * apply the rules at all".
 */
typedef enum CssLineBreak {
    /** `auto`: the user agent's choice. */
    RoboCssLineBreakAuto = 0,
    /** `loose`: the most break opportunities. */
    RoboCssLineBreakLoose = 1,
    /** `normal`: the script's default strictness. */
    RoboCssLineBreakNormal = 2,
    /** `strict`: the fewest, for careful typography. */
    RoboCssLineBreakStrict = 3,
    /** `anywhere`: the rules are not applied. */
    RoboCssLineBreakAnywhere = 4
} CssLineBreak;

/** Number of defined `line-break` values. */
#define RoboCssLineBreakCount 5u

int CssLineBreakIsKnown(CssLineBreak lineBreak);
const char *CssLineBreakName(CssLineBreak lineBreak);
size_t CssLineBreakCount(void);
CssLineBreak CssLineBreakFromSlice(CssSlice keyword);
CssLineBreak CssLineBreakFromText(const char *keyword);
int CssLineBreakSliceIsKnown(CssSlice keyword);

/** Whether @p lineBreak leaves the choice to the user agent (`auto`). */
int CssLineBreakIsAutomatic(CssLineBreak lineBreak);

/**
 * Whether @p lineBreak names one of the three **strictness** settings (`loose`,
 * `normal`, `strict`).
 *
 * The ladder is one rule at three settings, so a consumer reads the row rather than
 * comparing the word — and this predicate is what separates the ladder from the two
 * specials that sit on either side of it. An unknown value answers 0.
 */
int CssLineBreakIsStrictness(CssLineBreak lineBreak);

/**
 * Whether @p lineBreak **disables the script's rules** (`anywhere`).
 *
 * Not a fourth strictness: it is the absence of a rule, so a build that treated it
 * as the loosest setting would apply the rules it turns off. An unknown value
 * answers 0.
 */
int CssLineBreakBreaksAnywhere(CssLineBreak lineBreak);

Result CssLineBreakWrite(CssLineBreak lineBreak,
                         char *buffer,
                         size_t capacity,
                         size_t *outLength,
                         CssDiag *diag);

/** Capacity @ref CssLineBreakWrite needs ("anywhere" plus a NUL). */
#define RoboCssLineBreakTextMax 12u

/* ------------------------------------------------------------------ */
/* hyphens                                                             */
/* ------------------------------------------------------------------ */

/**
 * A computed `hyphens`.
 *
 * Three rows, and the distinction is *who supplies the hyphen*: `none` forbids
 * hyphenation entirely, `manual` allows only the points an author marked (a soft
 * hyphen or a `&shy;`), and `auto` lets the user agent find them. That is why the
 * table carries two questions rather than one: `manual` *enables* hyphenation while
 * *inserting* nothing.
 */
typedef enum CssHyphens {
    /** `none`: no hyphenation, marked or found. */
    RoboCssHyphensNone = 0,
    /** `manual`: only the points the author marked. */
    RoboCssHyphensManual = 1,
    /** `auto`: the user agent may find them too. */
    RoboCssHyphensAuto = 2
} CssHyphens;

/** Number of defined `hyphens` values. */
#define RoboCssHyphensCount 3u

int CssHyphensIsKnown(CssHyphens hyphens);
const char *CssHyphensName(CssHyphens hyphens);
size_t CssHyphensCount(void);
CssHyphens CssHyphensFromSlice(CssSlice keyword);
CssHyphens CssHyphensFromText(const char *keyword);
int CssHyphensSliceIsKnown(CssSlice keyword);

/**
 * Whether @p hyphens allows a break at a hyphenation point at all (`manual`, `auto`).
 *
 * `manual` answers yes, which is the fact worth reading twice: an author who wrote
 * it has allowed the mechanism and supplied no points. An unknown value answers 0.
 */
int CssHyphensEnablesHyphenation(CssHyphens hyphens);

/**
 * Whether @p hyphens lets the user agent **insert** a hyphen the author did not mark
 * (`auto`).
 *
 * The second question, and the one that makes `manual` and `auto` different
 * statements rather than two strengths of one. An unknown value answers 0.
 */
int CssHyphensInsertsHyphens(CssHyphens hyphens);

Result CssHyphensWrite(CssHyphens hyphens,
                       char *buffer,
                       size_t capacity,
                       size_t *outLength,
                       CssDiag *diag);

/** Capacity @ref CssHyphensWrite needs ("manual" plus a NUL). */
#define RoboCssHyphensTextMax 8u

/* ------------------------------------------------------------------ */
/* text-decoration-style                                               */
/* ------------------------------------------------------------------ */

/**
 * A computed `text-decoration-style`.
 *
 * The five ways a decoration line may be drawn, and the classifications split them
 * by *how the ink is laid down*: `solid` is one unbroken stroke, `dotted` and
 * `dashed` are a stroke with gaps, `double` is two strokes, and `wavy` is a stroke
 * that is not straight. A renderer needs to know which of those it is before it can
 * pick a path, and the row is the only place that knows.
 */
typedef enum CssTextDecorationStyle {
    /** `solid`: one unbroken line. */
    RoboCssTextDecorationStyleSolid = 0,
    /** `double`: two lines. */
    RoboCssTextDecorationStyleDouble = 1,
    /** `dotted`: a line with gaps. */
    RoboCssTextDecorationStyleDotted = 2,
    /** `dashed`: longer gaps. */
    RoboCssTextDecorationStyleDashed = 3,
    /** `wavy`: a line that is not straight. */
    RoboCssTextDecorationStyleWavy = 4
} CssTextDecorationStyle;

/** Number of defined `text-decoration-style` values. */
#define RoboCssTextDecorationStyleCount 5u

int CssTextDecorationStyleIsKnown(CssTextDecorationStyle style);
const char *CssTextDecorationStyleName(CssTextDecorationStyle style);
size_t CssTextDecorationStyleCount(void);
CssTextDecorationStyle CssTextDecorationStyleFromSlice(CssSlice keyword);
CssTextDecorationStyle CssTextDecorationStyleFromText(const char *keyword);
int CssTextDecorationStyleSliceIsKnown(CssSlice keyword);

/** Whether @p style is the unbroken single stroke (`solid`). Unknown answers 0. */
int CssTextDecorationStyleIsSolid(CssTextDecorationStyle style);

/** Whether @p style is drawn **not straight** (`wavy`). An unknown value answers 0. */
int CssTextDecorationStyleIsWavy(CssTextDecorationStyle style);

/**
 * Whether @p style is a stroke **with gaps** (`dotted`, `dashed`).
 *
 * Separate from "not solid", because `double` is neither: it is two unbroken
 * strokes, so a consumer that read "not solid" as "has gaps" would dash a double
 * rule. An unknown value answers 0.
 */
int CssTextDecorationStyleIsSegmented(CssTextDecorationStyle style);

Result CssTextDecorationStyleWrite(CssTextDecorationStyle style,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength,
                                   CssDiag *diag);

/** Capacity @ref CssTextDecorationStyleWrite needs ("double" plus a NUL). */
#define RoboCssTextDecorationStyleTextMax 8u

/* ------------------------------------------------------------------ */
/* text-decoration-line                                                */
/* ------------------------------------------------------------------ */

/**
 * One member of a `text-decoration-line`.
 *
 * The property's grammar is a **set** — `text-decoration-line: underline overline`
 * is legal — so the declaration is registered as a **carried** one and this table
 * types what is inside it, exactly as 18.7 typed the names inside a `transform`
 * list. What a consumer reads is membership plus the classifications, and the
 * classifications are what a renderer needs: which of these values draws ink, and
 * which of them animates.
 */
typedef enum CssTextDecorationLine {
    /** `none`: no line, and the only value that is not a member of the set. */
    RoboCssTextDecorationLineNone = 0,
    /** `underline`: under the baseline. */
    RoboCssTextDecorationLineUnderline = 1,
    /** `overline`: above the text. */
    RoboCssTextDecorationLineOverline = 2,
    /** `line-through`: through the middle of the text. */
    RoboCssTextDecorationLineLineThrough = 3,
    /** `blink`: the line is not drawn steadily. */
    RoboCssTextDecorationLineBlink = 4
} CssTextDecorationLine;

/** Number of defined `text-decoration-line` values. */
#define RoboCssTextDecorationLineCount 5u

int CssTextDecorationLineIsKnown(CssTextDecorationLine line);
const char *CssTextDecorationLineName(CssTextDecorationLine line);
size_t CssTextDecorationLineCount(void);
CssTextDecorationLine CssTextDecorationLineFromSlice(CssSlice keyword);
CssTextDecorationLine CssTextDecorationLineFromText(const char *keyword);
int CssTextDecorationLineSliceIsKnown(CssSlice keyword);

/**
 * Whether @p line draws ink.
 *
 * The three line positions do; `none` does not, and `blink` **also** does not on its
 * own — a decorative blink with no line has nothing to draw — which is why the
 * predicate is not "is not none". An unknown value answers 0.
 */
int CssTextDecorationLineDrawsInk(CssTextDecorationLine line);

/** Whether @p line is the animating decoration (`blink`). Unknown answers 0. */
int CssTextDecorationLineIsBlink(CssTextDecorationLine line);

Result CssTextDecorationLineWrite(CssTextDecorationLine line,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength,
                                  CssDiag *diag);

/** Capacity @ref CssTextDecorationLineWrite needs ("line-through" plus a NUL). */
#define RoboCssTextDecorationLineTextMax 16u

/* ------------------------------------------------------------------ */
/* text-overflow                                                       */
/* ------------------------------------------------------------------ */

/**
 * A `text-overflow` keyword.
 *
 * The property's grammar is `[ clip | ellipsis | <string> ]{1,2}` — the string form
 * lets an author supply their own marker — so the declaration is **carried** and
 * this table types the two keywords, with the string form named as carried rather
 * than refused.
 */
typedef enum CssTextOverflow {
    /** `clip`: the overflow is cut off. */
    RoboCssTextOverflowClip = 0,
    /** `ellipsis`: a marker is substituted for the cut-off part. */
    RoboCssTextOverflowEllipsis = 1
} CssTextOverflow;

/** Number of defined `text-overflow` keywords. */
#define RoboCssTextOverflowCount 2u

int CssTextOverflowIsKnown(CssTextOverflow overflow);
const char *CssTextOverflowName(CssTextOverflow overflow);
size_t CssTextOverflowCount(void);
CssTextOverflow CssTextOverflowFromSlice(CssSlice keyword);
CssTextOverflow CssTextOverflowFromText(const char *keyword);
int CssTextOverflowSliceIsKnown(CssSlice keyword);

/**
 * Whether @p overflow **substitutes a marker** for the part that did not fit.
 *
 * Only `ellipsis` does; `clip` cuts the text in place. A consumer decides by this
 * whether it shares a truncation point with the renderer, so the classification is
 * the difference between a visible marker and a cut. An unknown value answers 0.
 */
int CssTextOverflowAddsMarker(CssTextOverflow overflow);

Result CssTextOverflowWrite(CssTextOverflow overflow,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength,
                            CssDiag *diag);

/** Capacity @ref CssTextOverflowWrite needs ("ellipsis" plus a NUL). */
#define RoboCssTextOverflowTextMax 12u

/* ------------------------------------------------------------------ */
/* text-decoration-thickness                                           */
/* ------------------------------------------------------------------ */

/**
 * A `text-decoration-thickness` keyword.
 *
 * The property's grammar is `auto | from-font | <length-percentage>`, so it is
 * **carried** and this table types the two keywords. The pair is a genuine
 * distinction rather than two spellings of "you decide": `auto` leaves the thickness
 * to the user agent, while `from-font` reads the *face's own* decoration metric — a
 * fact about the file rather than a choice.
 */
typedef enum CssTextDecorationThickness {
    /** `auto`: the user agent decides. */
    RoboCssTextDecorationThicknessAuto = 0,
    /** `from-font`: the face's own metric decides. */
    RoboCssTextDecorationThicknessFromFont = 1
} CssTextDecorationThickness;

/** Number of defined `text-decoration-thickness` keywords. */
#define RoboCssTextDecorationThicknessCount 2u

int CssTextDecorationThicknessIsKnown(CssTextDecorationThickness thickness);
const char *CssTextDecorationThicknessName(CssTextDecorationThickness thickness);
size_t CssTextDecorationThicknessCount(void);
CssTextDecorationThickness CssTextDecorationThicknessFromSlice(CssSlice keyword);
CssTextDecorationThickness CssTextDecorationThicknessFromText(const char *keyword);
int CssTextDecorationThicknessSliceIsKnown(CssSlice keyword);

/** Whether @p thickness leaves the choice to the user agent (`auto`). */
int CssTextDecorationThicknessIsAutomatic(CssTextDecorationThickness thickness);

/**
 * Whether @p thickness comes from the **face's own metric** (`from-font`).
 *
 * A different answer from "the user agent decides", and the reason the keyword
 * exists: a document that hit `from-font` and a face that states nothing still has
 * an `auto` answer to fall back to, which is a fact about the font and not a policy.
 * An unknown value answers 0.
 */
int CssTextDecorationThicknessUsesFontMetric(
    CssTextDecorationThickness thickness);

Result CssTextDecorationThicknessWrite(CssTextDecorationThickness thickness,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *outLength,
                                       CssDiag *diag);

/** Capacity @ref CssTextDecorationThicknessWrite needs ("from-font" + a NUL). */
#define RoboCssTextDecorationThicknessTextMax 12u

/* ------------------------------------------------------------------ */
/* text-underline-position                                             */
/* ------------------------------------------------------------------ */

/**
 * A computed `text-underline-position`.
 *
 * The property's grammar is a **set** — `text-underline-position: under left` is
 * legal — so the declaration is **carried** and this table types the members. The
 * four values are two different questions: `under` and `auto` are about *where the
 * line sits vertically* (below the baseline, or below the descenders), while `left`
 * and `right` only have an effect in vertical writing, where they shift the line
 * across the column.
 */
typedef enum CssTextUnderlinePosition {
    /** `auto`: the user agent's position. */
    RoboCssTextUnderlinePositionAuto = 0,
    /** `under`: below the baseline, clearing the descenders. */
    RoboCssTextUnderlinePositionUnder = 1,
    /** `left`: on the left of a vertical line. */
    RoboCssTextUnderlinePositionLeft = 2,
    /** `right`: on the right of a vertical line. */
    RoboCssTextUnderlinePositionRight = 3
} CssTextUnderlinePosition;

/** Number of defined `text-underline-position` values. */
#define RoboCssTextUnderlinePositionCount 4u

int CssTextUnderlinePositionIsKnown(CssTextUnderlinePosition position);
const char *CssTextUnderlinePositionName(CssTextUnderlinePosition position);
size_t CssTextUnderlinePositionCount(void);
CssTextUnderlinePosition CssTextUnderlinePositionFromSlice(CssSlice keyword);
CssTextUnderlinePosition CssTextUnderlinePositionFromText(const char *keyword);
int CssTextUnderlinePositionSliceIsKnown(CssSlice keyword);

/** Whether @p position leaves the choice to the user agent (`auto`). */
int CssTextUnderlinePositionIsAutomatic(CssTextUnderlinePosition position);

/**
 * Whether @p position clears the **descenders** (`under`).
 *
 * The vertical half of the property, and the reason it is a separate question from
 * the horizontal one: an underline that clears descenders is drawn lower than one
 * that sits on the baseline, and neither `left` nor `right` says anything about that.
 * An unknown value answers 0.
 */
int CssTextUnderlinePositionClearsDescenders(CssTextUnderlinePosition position);

/**
 * Whether @p position shifts the line **across** the text (`left`, `right`).
 *
 * Meaningful only in vertical writing — a horizontal line has no left or right to
 * sit on — so a consumer reads the writing mode (18.13) before it acts on this.
 * An unknown value answers 0.
 */
int CssTextUnderlinePositionIsPhysicalSide(CssTextUnderlinePosition position);

Result CssTextUnderlinePositionWrite(CssTextUnderlinePosition position,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *outLength,
                                     CssDiag *diag);

/** Capacity @ref CssTextUnderlinePositionWrite needs ("right" plus a NUL). */
#define RoboCssTextUnderlinePositionTextMax 8u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TEXT_H */
