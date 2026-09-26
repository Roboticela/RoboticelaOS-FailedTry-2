/*
 * RoboCSS multi-column vocabulary (Phase 18.6).
 *
 * The multi-column family describes a *fragmentation* container: a block that
 * splits its content into a sequence of columns. Two of its properties are closed
 * sets of their own and live here, because they are what a layout pass reads to
 * know how a column box behaves rather than how it is sized:
 *
 *   column-span   none | all
 *   column-fill   auto | balance
 *
 * `column-span` says whether the box spans every column, which is the fact that
 * takes it *out* of the column flow; `column-fill` says whether the columns are
 * filled to a balanced height or one after another. Neither has anything to do
 * with the two properties that decide how many columns there are and how wide they
 * are — `column-count` (a positive integer, or `auto`) and `column-width` (a
 * length, or `auto`) — which are a numeric and a length family and are read
 * through the shapes every other number and length uses, and neither has anything
 * to do with `column-gap`, which is the gutter `row-gap` already is.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a layout: nothing here fragments content, fills
 * a column, balances a height or decides how many columns fit, and no function
 * below takes or returns a rectangle. A value here says *what rule* a fragmentation
 * pass follows; where the columns land belongs to the phase that has a box.
 * `column-rule-*` reuse the border family's shapes rather than growing a second
 * copy of a style, a width or a colour.
 */

#ifndef ROBO_CSS_COLUMN_H
#define ROBO_CSS_COLUMN_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* column-span                                                         */
/* ------------------------------------------------------------------ */

/** A computed `column-span`. NONE is 0 — the initial value. */
typedef enum CssColumnSpan {
    /** `none`: the box sits inside one column. */
    RoboCssColumnSpanNone = 0,
    /** `all`: the box spans every column and breaks the column flow. */
    RoboCssColumnSpanAll = 1
} CssColumnSpan;

/** Number of defined `column-span` values. */
#define RoboCssColumnSpanCount 2u

int CssColumnSpanIsKnown(CssColumnSpan span);
const char *CssColumnSpanName(CssColumnSpan span);
size_t CssColumnSpanCount(void);
CssColumnSpan CssColumnSpanFromSlice(CssSlice keyword);
CssColumnSpan CssColumnSpanFromText(const char *keyword);
int CssColumnSpanSliceIsKnown(CssSlice keyword);

/**
 * Whether @p span takes the box out of the column flow (`all`).
 *
 * The one classification a fragmentation pass dispatches on: a spanning box is
 * laid out across every column rather than inside one, so it is a different kind
 * of participant. An unknown value answers 0.
 */
int CssColumnSpanSpansAll(CssColumnSpan span);

Result CssColumnSpanWrite(CssColumnSpan span,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag);

/** Capacity @ref CssColumnSpanWrite needs ("none" plus a NUL). */
#define RoboCssColumnSpanTextMax 8u

/* ------------------------------------------------------------------ */
/* column-fill                                                         */
/* ------------------------------------------------------------------ */

/**
 * A computed `column-fill`.
 *
 * AUTO is 0, but the property's *initial* value is `balance` — the registry writes
 * that down rather than relying on the zero of the enumerator, which is the same
 * trap `opacity` closed in 11.9 in a smaller way. Since the shape carries the
 * keyword, the initial arrives as the text `balance` and there is no cleared-record
 * hazard at all.
 */
typedef enum CssColumnFill {
    /** `auto`: each column is filled to its height before the next begins. */
    RoboCssColumnFillAuto = 0,
    /** `balance`: the content is distributed so the columns are as equal as possible. */
    RoboCssColumnFillBalance = 1
} CssColumnFill;

/** Number of defined `column-fill` values. */
#define RoboCssColumnFillCount 2u

int CssColumnFillIsKnown(CssColumnFill fill);
const char *CssColumnFillName(CssColumnFill fill);
size_t CssColumnFillCount(void);
CssColumnFill CssColumnFillFromSlice(CssSlice keyword);
CssColumnFill CssColumnFillFromText(const char *keyword);
int CssColumnFillSliceIsKnown(CssSlice keyword);

/**
 * Whether @p fill balances the columns (`balance`).
 *
 * `auto` fills one column to its full height before the next, which is a different
 * picture from balanced columns; the classification is asked of the row so a
 * consumer never compares a spelling. An unknown value answers 0.
 */
int CssColumnFillBalances(CssColumnFill fill);

Result CssColumnFillWrite(CssColumnFill fill,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag);

/** Capacity @ref CssColumnFillWrite needs ("balance" plus a NUL). */
#define RoboCssColumnFillTextMax 12u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_COLUMN_H */
