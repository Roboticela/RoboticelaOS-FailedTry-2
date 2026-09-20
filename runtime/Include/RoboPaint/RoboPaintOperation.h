/*
 * RoboPaint operations (Phase 11.1).
 *
 * One operation: what will be drawn, on which box, in which space, under which
 * boundary. It is a **record by value** and owned by nobody, like every model record
 * in this tree, and a list is an array of them (`RoboPaintList.h`) rather than a tree
 * of them, for the reason `RoboPaintVersion.h` gives: a rasteriser walks a sequence
 * once and keeps a clip stack.
 *
 * The record carries **two coordinate facts and three rectangles**, and the split
 * between them is the phase's coordinate model:
 *
 *   `boxes`  the border, padding and content boxes in **paint coordinates** -- one
 *            page space, where the accumulated origins of every ancestor have already
 *            been added;
 *
 *   `origin` the containing block's content origin, in paint coordinates. It is the
 *            number the accumulation produced for this box, which makes the **layout
 *            coordinates** recoverable in one call (`robo_paint_operation_local_boxes`)
 *            rather than in a walk: layout wrote a box's geometry relative to its
 *            containing block's content origin, so `local = boxes - origin`.
 *
 * Carrying both is not a redundancy to be tidied away, and the reason is worth stating
 * because the obvious "keep one and derive the other" is wrong in a specific way: a
 * consumer that has only the list cannot recover the *other* space without re-walking
 * the tree, and re-walking the tree to rediscover what layout concluded is exactly the
 * "recalculate in paint" this phase exists to prevent. What makes the pair safe rather
 * than a second opinion is that the relationship between them is **checkable and
 * checked**: `robo_paint_operation_local_boxes` must equal what
 * `robo_paint_boxes_of` reads straight out of layout, for every operation of every
 * box, and the suite asserts exactly that. A redundancy with an assertion holding the
 * two halves together is a test; one without is a place for them to drift.
 *
 * The colour is the one field whose *meaning* varies by kind, and it is a **stated
 * absence** rather than an unset field: `robo_css_color_none()` for the two markers,
 * which is RoboCSS's own "no colour at all" and not a zero that might be black.
 * `robo_paint_operation_is_valid` requires exactly that, so a marker carrying a colour
 * is a refused record rather than one a consumer might draw.
 */
#ifndef ROBO_PAINT_OPERATION_H
#define ROBO_PAINT_OPERATION_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSColor.h"

#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutTypes.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One operation of a paint list. */
typedef struct robo_paint_operation {
    /** What this is (RoboPaintTypes.h). `NONE` is a cleared record and is never in a
     *  list a pass produced. */
    robo_paint_op_kind_t kind;
    /** The layout object this operation draws, by id. Never `NONE` for a live
     *  operation: an operation with no box would be one a reader could not check. */
    robo_layout_node_id_t node;
    /** How deep in the tree the box is: 0 for the root. It is what makes the tree's
     *  shape recoverable from a flat list, and what a later clip or blend stack pushes
     *  and pops by. */
    size_t depth;
    /** The box's three rectangles, in **paint coordinates**. */
    robo_paint_boxes_t boxes;
    /** The containing block's content origin, in paint coordinates; layout
     *  coordinates are `boxes - origin` (see the header comment). */
    robo_layout_point_t origin;
    /** The boundary in force. */
    robo_paint_clip_t clip;
    /** The colour this operation draws with. `none` for a marker, and required to be
     *  a colour a rasteriser can use for a drawing kind. */
    robo_css_color_t color;
} robo_paint_operation_t;

/** Clear @p operation to a `NONE` marker with no geometry. Safe on NULL. */
void robo_paint_operation_init(robo_paint_operation_t *operation);

/**
 * Non-zero when @p operation is a record this build may hand to a consumer.
 *
 * The rules are the vocabulary's shape rather than a taste, and each of them refuses a
 * combination that would otherwise travel a stage further than it should:
 *
 *   a **known kind**, and never `NONE` -- a cleared record is not an operation;
 *   a **live node id**, because an operation names the box a reader verifies it
 *   against;
 *   **three valid boxes** whose border box contains the padding box contains the
 *   content box;
 *   a **valid clip**;
 *   and the colour rule the header states: `none` for a marker, and a colour a
 *   rasteriser can use -- resolved, or `currentColor` that this layer could not
 *   resolve -- for a drawing kind. `none` on a drawing operation is refused because a
 *   fill or a border with no colour draws nothing, and a builder that emitted one would
 *   be emitting an operation that is not a statement about the document.
 */
int robo_paint_operation_is_valid(const robo_paint_operation_t *operation);

/** Field-by-field equality of every field, including the boxes and the clip. */
int robo_paint_operation_equal(const robo_paint_operation_t *a,
                              const robo_paint_operation_t *b);

/** The kind (ROBO_PAINT_OP_NONE for NULL). */
robo_paint_op_kind_t robo_paint_operation_kind(const robo_paint_operation_t *operation);

/** The node (ROBO_LAYOUT_NODE_ID_NONE for NULL). */
robo_layout_node_id_t robo_paint_operation_node(const robo_paint_operation_t *operation);

/** The depth (0 for NULL). */
size_t robo_paint_operation_depth(const robo_paint_operation_t *operation);

/** The boxes, by value. Zeros for NULL. */
void robo_paint_operation_boxes(const robo_paint_operation_t *operation,
                               robo_paint_boxes_t *out);

/** The accumulated origin, by value. The origin for NULL. */
void robo_paint_operation_origin(const robo_paint_operation_t *operation,
                                robo_layout_point_t *out);

/** The clip, by value. A cleared clip for NULL. */
void robo_paint_operation_clip(const robo_paint_operation_t *operation,
                              robo_paint_clip_t *out);

/** The colour, by value. `none` for NULL. */
robo_css_color_t robo_paint_operation_color(const robo_paint_operation_t *operation);

/** Non-zero when @p operation is a `BEGIN_BOX`/`END_BOX` marker. Safe on NULL (0). */
int robo_paint_operation_is_marker(const robo_paint_operation_t *operation);

/** Non-zero when @p operation draws something: a background or a border side. Safe on
 *  NULL (0). */
int robo_paint_operation_is_drawing(const robo_paint_operation_t *operation);

/**
 * The border side @p operation draws.
 * @return ROBO_OK and the side; ROBO_INVALID_ARGUMENT for NULL arguments or an
 *         operation whose kind is not one of the four border kinds (@p out is
 *         untouched). The side is **derived from the kind**, so no operation can carry
 *         a side that disagrees with what it draws.
 */
robo_result_t robo_paint_operation_side(const robo_paint_operation_t *operation,
                                       robo_css_box_side_t *out);

/**
 * The three rectangles of @p operation in **layout coordinates**.
 *
 * `boxes - origin`, and the one call that keeps a consumer from doing the subtraction
 * itself. It is a door rather than an arithmetic hint because the two spaces are the
 * phase's central claim: a consumer that wants to compare a paint list against what
 * RoboLayout concluded asks this, and the suite asserts that its answer is
 * **field-for-field** what `robo_paint_boxes_of` reads out of the tree.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a group of boxes that is
 *         not a record.
 */
robo_result_t robo_paint_operation_local_boxes(const robo_paint_operation_t *operation,
                                              robo_paint_boxes_t *out,
                                              robo_paint_diag_t *diag);

/**
 * The rectangle a border side occupies.
 *
 * The band between the border box and the padding box on that side -- and it is a
 * **derived** value rather than a field, because it is exactly the difference between
 * two rectangles the operation already carries. Storing it as a fourth rectangle would
 * be a third copy of the same numbers, and the one that could disagree.
 *
 * The width of the band on the axis the side faces is the box's own border width on that
 * side, which is `border.width - padding.width` horizontally and `border.height -
 * padding.height` vertically -- *both* sides of the axis, since a box with two borders
 * has one band at each end and this door answers for the one the kind names.
 *
 * @return ROBO_OK and the band; ROBO_INVALID_ARGUMENT for NULL arguments or an
 *         operation that is not a border kind.
 */
robo_result_t robo_paint_operation_side_band(const robo_paint_operation_t *operation,
                                            robo_layout_rect_t *out,
                                            robo_paint_diag_t *diag);

/**
 * Write a one-line description of @p operation into @p buffer.
 *
 * The developer's door, and the reason it lives beside the record rather than only in
 * `RoboPaintDump.h`: a failing assertion wants *one* operation rendered, and a dump of
 * a thousand would hide it. `"background node=3 depth=1 border=0,0,100,40"`, with the
 * boxes and the clip appended when they are interesting.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param out_length non-NULL; receives the length written, excluding the NUL.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument; ROBO_NOT_SUPPORTED when
 *         the text does not fit, in which case @p out_length receives the length it
 *         would have needed.
 */
robo_result_t robo_paint_operation_write(const robo_paint_operation_t *operation,
                                        char *buffer,
                                        size_t capacity,
                                        size_t *out_length,
                                        robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_OPERATION_H */
