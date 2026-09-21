/*
 * RoboPaint operations (Phase 11.1–11.2).
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
 *            coordinates** recoverable in one call (`PaintOperationLocalBoxes`)
 *            rather than in a walk: layout wrote a box's geometry relative to its
 *            containing block's content origin, so `local = boxes - origin`.
 *
 * Carrying both is not a redundancy to be tidied away, and the reason is worth stating
 * because the obvious "keep one and derive the other" is wrong in a specific way: a
 * consumer that has only the list cannot recover the *other* space without re-walking
 * the tree, and re-walking the tree to rediscover what layout concluded is exactly the
 * "recalculate in paint" this phase exists to prevent. What makes the pair safe rather
 * than a second opinion is that the relationship between them is **checkable and
 * checked**: `PaintOperationLocalBoxes` must equal what
 * `PaintBoxesOf` reads straight out of layout, for every operation of every
 * box, and the suite asserts exactly that. A redundancy with an assertion holding the
 * two halves together is a test; one without is a place for them to drift.
 *
 * The colour is the one field whose *meaning* varies by kind, and it is a **stated
 * absence** rather than an unset field: `CssColorNone()` for the two markers,
 * which is RoboCSS's own "no colour at all" and not a zero that might be black.
 * `PaintOperationIsValid` requires exactly that, so a marker carrying a colour
 * is a refused record rather than one a consumer might draw.
 *
 * The **border style** (11.5) is the second field whose meaning varies by kind, and it
 * is carried for the reason `RoboLayoutBorder.h` says the style is carried *there*: the
 * layout layer resolved twelve declarations into four used sides that hold "the width
 * it occupies, and the style and colour that produced it", and it states that paint
 * "decides whether to draw, **which of the ten styles to draw** and in what colour". A
 * paint layer that read only the width and the colour would be dropping a fact the
 * layer below deliberately preserved, and it would drop it invisibly: a `dashed` border
 * would be drawn as a `solid` one with nothing anywhere saying so.
 *
 * The **text run** (11.6) is the third field whose meaning varies by kind, and it is the
 * only one that is not a scalar: a text record carries the span naming its characters in
 * the list's pool, the baseline's offset from the run's own top, and the font inputs the
 * run was measured with (`RoboPaintText.h`). It is a sub-record rather than six more
 * fields on this struct for the reason `PaintBoxes` is a sub-record: the facts
 * travel together, so a record cannot carry half of them.
 *
 * So the rule is the colour rule's shape, stated per kind and **enforced** rather than
 * assumed: a border record carries a style that **generates** a border, every other kind
 * carries `NONE`, and a text record carries a run while every other kind carries a
 * cleared one. `generates_border` rather than "not `NONE`" because `none` and
 * `hidden` both draw nothing (10.11's rule, which is why such a side's used width is
 * zero and no record is emitted for it) -- so a hand-built record claiming to draw a
 * `hidden` border is inconsistent and refused, by the same door the layout layer uses
 * rather than by a second spelling of the same rule.
 */
#ifndef ROBO_PAINT_OPERATION_H
#define ROBO_PAINT_OPERATION_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSColor.h"

#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutTypes.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintImage.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintText.h"
#include "RoboPaint/RoboPaintTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One operation of a paint list. */
typedef struct PaintOperation {
    /** What this is (RoboPaintTypes.h). `NONE` is a cleared record and is never in a
     *  list a pass produced. */
    PaintOpKind kind;
    /** The layout object this operation draws, by id. Never `NONE` for a live
     *  operation: an operation with no box would be one a reader could not check. */
    LayoutNodeId node;
    /** How deep in the tree the box is: 0 for the root. It is what makes the tree's
     *  shape recoverable from a flat list, and what a later clip or blend stack pushes
     *  and pops by. */
    size_t depth;
    /** The box's three rectangles, in **paint coordinates**. */
    PaintBoxes boxes;
    /** The containing block's content origin, in paint coordinates; layout
     *  coordinates are `boxes - origin` (see the header comment). */
    LayoutPoint origin;
    /** The boundary in force. */
    PaintClip clip;
    /**
     * The **opacity in force** (11.9): the factor this operation's drawing is composited with,
     * already multiplied by every ancestor's declared `opacity`.
     *
     * Carried on **every** kind rather than only on the two scopes, for the reason @ref clip is: a
     * consumer that walks a flat list reads what is in force from the record it is looking at, and
     * a value that arrived only on a push would make every drawing under it unreadable without
     * replaying the list from the beginning. A cleared record carries the fully **opaque** value, so
     * "nothing was declared" and "a drawing at full strength" are one state -- which is the
     * direction this field must fail in, because the other one draws nothing (RoboPaintOpacity.h).
     *
     * It is not the *declared* value of the box this operation draws: that is a property of one
     * element, read once from its computed style, and what travels here is the product along the
     * path that reached it.
     */
    PaintOpacity opacity;
    /** The colour this operation draws with. `none` for a marker, and required to be
     *  a colour a rasteriser can use for a drawing kind. */
    CssColor color;
    /** The `border-style` of the side this operation draws (11.5). Meaningful for a
     *  **border** record and required to be `NONE` for every other kind -- which is the
     *  same shape @ref color has for a marker, and the reason the field is a stated
     *  absence rather than an unset value. See the header comment. */
    CssBorderStyle style;
    /**
     * The text facts of a **text** record (11.6): the span naming the run's characters in
     * the list's pool, the baseline's offset from the run's own top, and the font inputs
     * the run was measured with. Cleared for every other kind, which is the same rule
     * @ref style has one field along, and it is a **sub-record** rather than six more
     * fields here for the reason `PaintBoxes` is one: the facts a text drawing has
     * and a fill does not arrive together, so a record cannot carry half of them
     * (`RoboPaintText.h`).
     */
    PaintTextRun text;
    /**
     * The image facts of an **image** record (11.7): the resource, the rectangle of it to
     * draw and the resource's own dimensions, format and identity at snapshot time
     * (`RoboPaintImage.h`). Cleared for every other kind, which is the same rule @ref text
     * has one field along -- and a **sub-record** for the same reason: the facts an image
     * drawing has and a fill does not arrive together.
     */
    PaintImage image;
} PaintOperation;

/** Clear @p operation to a `NONE` marker with no geometry. Safe on NULL. */
void PaintOperationInit(PaintOperation *operation);

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
 *   the colour rule the header states: `none` for a **structural** record
 *   (`BEGIN_BOX`, `CONTENT`, `END_BOX`), and a colour a rasteriser can use -- resolved,
 *   or `currentColor` that this layer could not resolve -- for a **drawing** kind.
 *   `none` on a drawing operation is refused because a fill or a border with no colour
 *   draws nothing, and a builder that emitted one would be emitting an operation that is
 *   not a statement about the document. The split is
 *   `PaintOpKindDraws`, not `is_marker`, so a `CONTENT` record -- which draws
 *   nothing and is not a box marker -- is held to the structural side of the rule;
 *   and the **style rule** (11.5), which is the same shape one field along: a border
 *   record's style must generate a border, and every other kind's must be `NONE`. Both
 *   halves are refusals rather than tolerances -- a background carrying a style, or a
 *   border carrying `none`, is an operation whose fields disagree about what it is;
 *   and the **text rule** (11.6), which is the style rule's shape a third time: a `TEXT`
 *   record must carry **a run** -- a non-empty span, a non-negative baseline offset and
 *   font inputs that are a record (`PaintTextRunIsValid`) -- and every other
 *   kind must carry a cleared one. So a `TEXT` record with no characters and a background
 *   that came with font inputs are both refused, for the same reason and by the same
 *   door.
 *
 *   The span is checked for shape **here** and for *membership* in a pool by the list
 *   that owns one (`RoboPaintList.h`): a record's validity is a function of the record,
 *   and "is this span inside the characters this list holds?" is not a question a record
 *   can answer about itself.
 */
int PaintOperationIsValid(const PaintOperation *operation);

/** Field-by-field equality of every field, including the boxes and the clip. */
int PaintOperationEqual(const PaintOperation *a,
                              const PaintOperation *b);

/** The kind (RoboPaintOpNone for NULL). */
PaintOpKind PaintOperationKind(const PaintOperation *operation);

/** The node (RoboLayoutNodeIdNone for NULL). */
LayoutNodeId PaintOperationNode(const PaintOperation *operation);

/** The depth (0 for NULL). */
size_t PaintOperationDepth(const PaintOperation *operation);

/** The boxes, by value. Zeros for NULL. */
void PaintOperationBoxes(const PaintOperation *operation,
                               PaintBoxes *out);

/** The accumulated origin, by value. The origin for NULL. */
void PaintOperationOrigin(const PaintOperation *operation,
                                LayoutPoint *out);

/** The clip, by value. A cleared clip for NULL. */
void PaintOperationClip(const PaintOperation *operation,
                              PaintClip *out);

/** The opacity in force, by value. The fully opaque record for NULL -- so a caller that reads
 *  a record without asking anything draws it at full strength, which is the answer a document
 *  that declared no opacity would give. */
void PaintOperationOpacity(const PaintOperation *operation,
                                 PaintOpacity *out);

/** The colour, by value. `none` for NULL. */
CssColor PaintOperationColor(const PaintOperation *operation);

/** The `border-style`, by value. `NONE` for NULL, and for every kind except a border
 *  -- so a caller that wants "is this side dashed?" asks here and gets an answer it can
 *  act on rather than a value it must first check the kind against. */
CssBorderStyle PaintOperationStyle(const PaintOperation *operation);

/** The text run, by value. A cleared run for NULL -- and for every kind except `TEXT`,
 *  so a caller that reads it without asking the kind gets "no characters", which is a
 *  statement it can act on rather than a leftover. */
PaintTextRun PaintOperationText(const PaintOperation *operation);

/**
 * The image payload, by value, **without taking a reference on the resource**.
 *
 * A cleared payload for NULL and for every kind except `IMAGE`, which is the same shape
 * @ref PaintOperationText has: a caller that reads it without asking the kind gets
 * "no resource", a statement it can act on rather than a leftover.
 *
 * The resource it names belongs to the tree the record was built from, and the record's
 * container holds the reference that keeps it alive — so a caller that wants the payload to
 * outlive the list calls `PaintImageRetain` on a copy of it, which is what the
 * display builder does. Returning a *borrowed* payload rather than an owning one is
 * deliberate: a reader that only wants the source rectangle pays nothing, and a copy that
 * retained would leak for every caller that forgot to release it.
 */
PaintImage PaintOperationImage(const PaintOperation *operation);

/**
 * Non-zero when @p operation is **structural** -- draws nothing and is emitted for every
 * placed box: `BEGIN_BOX`, `CONTENT` or `END_BOX`. Safe on NULL (0).
 *
 * The predicate a consumer asks to skip the records that carry no colour, and the exact
 * complement of @ref PaintOperationIsDrawing over a valid record.
 */
int PaintOperationIsStructural(const PaintOperation *operation);

/** Non-zero when @p operation is a `BEGIN_BOX`/`END_BOX` marker. Safe on NULL (0).
 *  Narrower than @ref PaintOperationIsStructural, which also covers `CONTENT`. */
int PaintOperationIsMarker(const PaintOperation *operation);

/** Non-zero when @p operation draws something: a background or a border side. Safe on
 *  NULL (0). */
int PaintOperationIsDrawing(const PaintOperation *operation);

/**
 * The border side @p operation draws.
 * @return RoboOk and the side; RoboInvalidArgument for NULL arguments or an
 *         operation whose kind is not one of the four border kinds (@p out is
 *         untouched). The side is **derived from the kind**, so no operation can carry
 *         a side that disagrees with what it draws.
 */
Result PaintOperationSide(const PaintOperation *operation,
                                       CssBoxSide *out);

/**
 * The three rectangles of @p operation in **layout coordinates**.
 *
 * `boxes - origin`, and the one call that keeps a consumer from doing the subtraction
 * itself. It is a door rather than an arithmetic hint because the two spaces are the
 * phase's central claim: a consumer that wants to compare a paint list against what
 * RoboLayout concluded asks this, and the suite asserts that its answer is
 * **field-for-field** what `PaintBoxesOf` reads out of the tree.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a group of boxes that is
 *         not a record.
 */
Result PaintOperationLocalBoxes(const PaintOperation *operation,
                                              PaintBoxes *out,
                                              PaintDiag *diag);

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
 * @return RoboOk and the band; RoboInvalidArgument for NULL arguments or an
 *         operation that is not a border kind.
 */
Result PaintOperationSideBand(const PaintOperation *operation,
                                            LayoutRect *out,
                                            PaintDiag *diag);

/**
 * Write a one-line description of @p operation into @p buffer.
 *
 * The developer's door, and the reason it lives beside the record rather than only in
 * `RoboPaintDump.h`: a failing assertion wants *one* operation rendered, and a dump of
 * a thousand would hide it. `"background node=3 depth=1 border=0,0,100,40"`, with the
 * boxes and the clip appended when they are interesting.
 *
 * The rectangle's **label names which of the three boxes the kind acts on**, because that
 * is the one thing a reader of a record cannot infer from the kind's name alone: `band=` for
 * a border side, `content=` for the content boundary, `padding=` for a clip scope (11.8 --
 * a box's boundary is defined at its padding edge, and the display command for the same
 * record carries that rectangle as its `rect`), and `border=` for everything else. A
 * generic box would print the border box for all four and tell a reader a scope came from a
 * rectangle it did not come from.
 *
 * @param buffer     destination, at least @p capacity bytes.
 * @param capacity   bytes available.
 * @param outLength non-NULL; receives the length written, excluding the NUL.
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboNotSupported when
 *         the text does not fit, in which case @p outLength receives the length it
 *         would have needed.
 */
Result PaintOperationWrite(const PaintOperation *operation,
                                        char *buffer,
                                        size_t capacity,
                                        size_t *outLength,
                                        PaintDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_OPERATION_H */
