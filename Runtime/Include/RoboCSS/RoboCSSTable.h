/*
 * RoboCSS table vocabulary (Phase 18.5).
 *
 * Tables are the fifth property *family* in this build (after the box model, the
 * background family, the border family and the flex/grid alignment sets), and the
 * family is an odd one: CSS's table properties are almost all *inherited*, which
 * is the opposite of every other layout family here, because a table's cells
 * inherit the collapse, the spacing, the caption side and the empty-cell rule from
 * the table box rather than deciding them for themselves. Exactly one of them —
 * `table-layout` — is the box's own.
 *
 * Five closed vocabularies live here, each a row per value and every question
 * asked of the table rather than answered by a comparison against a spelling:
 *
 *   table-layout     auto | fixed
 *   border-collapse  separate | collapse
 *   caption-side     top | bottom | block-start | block-end | inline-start |
 *                    inline-end
 *   empty-cells      show | hide
 *   visibility       visible | hidden | collapse
 *
 * The interesting row in that list is `caption-side`: it is the first vocabulary
 * here with *logical* values, so half its members are block-axis and half are
 * inline-axis and which physical edge a logical one lands on is a writing-mode
 * question the phase that has one answers. This module only reports the axis.
 *
 * `border-spacing` is deliberately **not** a vocabulary here. Its grammar is
 * `<length>{1,2}` — one length for both axes or two for each — which is the shared
 * length grammar with a pair above it rather than a closed set, and this build
 * carries the declaration PRESERVED until the phase that reads it (19.6, table
 * layout). That is the same boundary `grid-template-columns` is drawn at in 18.4,
 * and it is named rather than implied.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a layout: nothing measures a cell, distributes
 * columns or collapses a border, and no function below takes or returns a
 * rectangle. The `display: table` values and the internal table boxes are a
 * layout question of their own (the internal-table display types are not rows in
 * this build's display vocabulary, and 19.6 owns them); this phase types the
 * properties a table box is described by. A value here says *what rule* a table
 * layout pass follows; where the cells land belongs to the phase that has a box.
 */

#ifndef ROBO_CSS_TABLE_H
#define ROBO_CSS_TABLE_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* table-layout                                                        */
/* ------------------------------------------------------------------ */

/**
 * A computed `table-layout`.
 *
 * AUTO is 0 because it is the property's initial value, so a cleared record holds
 * the state an author who declared nothing finds their table in: column widths
 * come from the contents.
 */
typedef enum CssTableLayout {
    /** `auto`: column widths are derived from the cells' contents. */
    RoboCssTableLayoutAuto = 0,
    /** `fixed`: column widths come from the first row and the table's own width. */
    RoboCssTableLayoutFixed = 1
} CssTableLayout;

/** Number of defined `table-layout` values. */
#define RoboCssTableLayoutCount 2u

int CssTableLayoutIsKnown(CssTableLayout layout);
const char *CssTableLayoutName(CssTableLayout layout);
size_t CssTableLayoutCount(void);
CssTableLayout CssTableLayoutFromSlice(CssSlice keyword);
CssTableLayout CssTableLayoutFromText(const char *keyword);
int CssTableLayoutSliceIsKnown(CssSlice keyword);

/**
 * Whether @p layout lets the table's contents decide a column's width.
 *
 * The one classification a table layout pass dispatches on before anything else:
 * `auto` is content-driven and `fixed` is not. Asked of the row rather than
 * compared against a spelling, and an unknown value answers 0 — a value this build
 * cannot read must not claim the table may be laid out from its contents.
 */
int CssTableLayoutIsContentDriven(CssTableLayout layout);

Result CssTableLayoutWrite(CssTableLayout layout,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssTableLayoutWrite needs (\"fixed\" plus a NUL). */
#define RoboCssTableLayoutTextMax 8u

/* ------------------------------------------------------------------ */
/* border-collapse                                                     */
/* ------------------------------------------------------------------ */

/** A computed `border-collapse`. SEPARATE is 0 — the initial value. */
typedef enum CssBorderCollapse {
    /** `separate`: each cell keeps its own border, with spacing between cells. */
    RoboCssBorderCollapseSeparate = 0,
    /** `collapse`: adjoining borders are collapsed into one. */
    RoboCssBorderCollapseCollapse = 1
} CssBorderCollapse;

/** Number of defined `border-collapse` values. */
#define RoboCssBorderCollapseCount 2u

int CssBorderCollapseIsKnown(CssBorderCollapse collapse);
const char *CssBorderCollapseName(CssBorderCollapse collapse);
size_t CssBorderCollapseCount(void);
CssBorderCollapse CssBorderCollapseFromSlice(CssSlice keyword);
CssBorderCollapse CssBorderCollapseFromText(const char *keyword);
int CssBorderCollapseSliceIsKnown(CssSlice keyword);

/**
 * Whether @p collapse makes adjoining cells share a border.
 *
 * `collapse` is the answer that changes a `border-spacing` from a real gutter to a
 * declaration CSS ignores, so it is read as a fact about the value rather than as
 * a spelling comparison. An unknown value answers 0.
 */
int CssBorderCollapseBordersAdjoin(CssBorderCollapse collapse);

Result CssBorderCollapseWrite(CssBorderCollapse collapse,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength,
                              CssDiag *diag);

/** Capacity @ref CssBorderCollapseWrite needs (\"separate\" plus a NUL). */
#define RoboCssBorderCollapseTextMax 12u

/* ------------------------------------------------------------------ */
/* caption-side                                                        */
/* ------------------------------------------------------------------ */

/**
 * A computed `caption-side`.
 *
 * The first vocabulary in this build whose values are *logical*: the four
 * `block-*`/`inline-*` members name an axis-relative edge, and which physical edge
 * that is depends on the table's writing mode. TOP is 0 — the initial value — and
 * the four logical rows are appended so a stored enumerator stays a contract.
 */
typedef enum CssCaptionSide {
    /** `top`: the physical top edge. */
    RoboCssCaptionSideTop = 0,
    /** `bottom`: the physical bottom edge. */
    RoboCssCaptionSideBottom = 1,
    /** `block-start`: the block-start edge of whatever writing mode is in force. */
    RoboCssCaptionSideBlockStart = 2,
    /** `block-end`: the block-end edge. */
    RoboCssCaptionSideBlockEnd = 3,
    /** `inline-start`: the inline-start edge. */
    RoboCssCaptionSideInlineStart = 4,
    /** `inline-end`: the inline-end edge. */
    RoboCssCaptionSideInlineEnd = 5
} CssCaptionSide;

/** Number of defined `caption-side` values. */
#define RoboCssCaptionSideCount 6u

int CssCaptionSideIsKnown(CssCaptionSide side);
const char *CssCaptionSideName(CssCaptionSide side);
size_t CssCaptionSideCount(void);
CssCaptionSide CssCaptionSideFromSlice(CssSlice keyword);
CssCaptionSide CssCaptionSideFromText(const char *keyword);
int CssCaptionSideSliceIsKnown(CssSlice keyword);

/** Whether @p side is expressed on the **block** axis (the two `block-*` rows). */
int CssCaptionSideIsBlockAxis(CssCaptionSide side);

/**
 * Whether @p side is expressed on the **inline** axis (the two `inline-*` rows).
 *
 * The physical rows answer 0 to both axis predicates: `top` and `bottom` are not
 * axis-relative, so they are neither — which is a different fact from \"block\", and
 * the reason the two questions are asked separately rather than as one negation.
 */
int CssCaptionSideIsInlineAxis(CssCaptionSide side);

Result CssCaptionSideWrite(CssCaptionSide side,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssCaptionSideWrite needs (\"inline-start\" plus a NUL). */
#define RoboCssCaptionSideTextMax 16u

/* ------------------------------------------------------------------ */
/* empty-cells                                                         */
/* ------------------------------------------------------------------ */

/** A computed `empty-cells`. SHOW is 0 — the initial value. */
typedef enum CssEmptyCells {
    /** `show`: a cell with no visible content still draws its border and background. */
    RoboCssEmptyCellsShow = 0,
    /** `hide`: a cell with no visible content draws nothing at all. */
    RoboCssEmptyCellsHide = 1
} CssEmptyCells;

/** Number of defined `empty-cells` values. */
#define RoboCssEmptyCellsCount 2u

int CssEmptyCellsIsKnown(CssEmptyCells cells);
const char *CssEmptyCellsName(CssEmptyCells cells);
size_t CssEmptyCellsCount(void);
CssEmptyCells CssEmptyCellsFromSlice(CssSlice keyword);
CssEmptyCells CssEmptyCellsFromText(const char *keyword);
int CssEmptyCellsSliceIsKnown(CssSlice keyword);

/** Whether @p cells draws an empty cell (`show`). An unknown value answers 0. */
int CssEmptyCellsDrawsEmpty(CssEmptyCells cells);

Result CssEmptyCellsWrite(CssEmptyCells cells,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag);

/** Capacity @ref CssEmptyCellsWrite needs (\"show\" plus a NUL). */
#define RoboCssEmptyCellsTextMax 8u

/* ------------------------------------------------------------------ */
/* visibility                                                          */
/* ------------------------------------------------------------------ */

/**
 * A computed `visibility`.
 *
 * VISIBLE is 0 — the initial value. `collapse` is the third value and the one
 * that is not merely a painting rule: for a table row it removes the box from the
 * layout, while for anything else it behaves as `hidden`. This build carries the
 * value and its classification; what a collapsed row does belongs to table layout.
 */
typedef enum CssVisibility {
    /** `visible`: the box is drawn. */
    RoboCssVisibilityVisible = 0,
    /** `hidden`: the box takes space but is not drawn. */
    RoboCssVisibilityHidden = 1,
    /** `collapse`: removed for a table row, `hidden` otherwise. */
    RoboCssVisibilityCollapse = 2
} CssVisibility;

/** Number of defined `visibility` values. */
#define RoboCssVisibilityCount 3u

int CssVisibilityIsKnown(CssVisibility visibility);
const char *CssVisibilityName(CssVisibility visibility);
size_t CssVisibilityCount(void);
CssVisibility CssVisibilityFromSlice(CssSlice keyword);
CssVisibility CssVisibilityFromText(const char *keyword);
int CssVisibilitySliceIsKnown(CssSlice keyword);

/**
 * Whether @p visibility leaves the box taking up room in the layout.
 *
 * `hidden` takes space — the box is laid out and simply not painted — while
 * `collapse` removes it for a table row. So this is **not** \"is it visible\": a
 * hidden element still pushes its siblings where they were. An unknown value
 * answers 1, because the initial value takes space and a value nothing defined must
 * not silently shrink a layout.
 */
int CssVisibilityTakesSpace(CssVisibility visibility);

/**
 * Whether @p visibility paints the box.
 *
 * Only `visible` does. An unknown value answers 0 — a value this build cannot read
 * must not claim the box is drawn.
 */
int CssVisibilityPaints(CssVisibility visibility);

Result CssVisibilityWrite(CssVisibility visibility,
                          char *buffer,
                          size_t capacity,
                          size_t *outLength,
                          CssDiag *diag);

/** Capacity @ref CssVisibilityWrite needs (\"visible\" plus a NUL). */
#define RoboCssVisibilityTextMax 12u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_TABLE_H */
