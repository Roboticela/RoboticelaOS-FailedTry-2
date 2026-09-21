/*
 * RoboLayout object accessors (Phase 10.1) — see RoboLayoutObject.h.
 *
 * Every door here validates the id through the same three lines: resolve it
 * against the tree, and answer RoboNotFound when it names no box. That is the
 * whole reason the id-to-object conversion lives in one place
 * (RoboLayoutInternal.h) instead of being repeated per accessor with a different
 * amount of care each time.
 */

#include "RoboLayout/RoboLayoutObject.h"

#include "RoboLayoutInternal.h"

#include <string.h>

Result LayoutObjectGet(const LayoutTree *tree,
                                     LayoutNodeId id,
                                     LayoutObject *out,
                                     LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "out is NULL");
    }
    memset(out, 0, sizeof(*out));
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "tree or id is invalid");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no box in this tree");
    }
    /* A copy, so the caller's value outlives the next add to the tree. */
    *out = *object;
    return RoboOk;
}

int LayoutObjectIsLive(const LayoutTree *tree,
                               LayoutNodeId id)
{
    return LayoutObjectSlotConst(tree, id) != NULL;
}

LayoutObjectKind MakeLayoutObjectKind(const LayoutTree *tree,
                                                  LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    return object ? object->kind : RoboLayoutObjectNone;
}

Result LayoutObjectDomNode(const LayoutTree *tree,
                                          LayoutNodeId id,
                                          HtmlNodeHandle *out)
{
    const LayoutObject *object;

    if (!out) {
        return RoboInvalidArgument;
    }
    HtmlNodeHandleClear(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboInvalidArgument;
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboNotFound;
    }
    *out = object->domNode;
    return RoboOk;
}

const CssComputedStyle *LayoutObjectStyle(
    const LayoutTree *tree,
    LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    return object ? object->style : NULL;
}

CssDisplay LayoutObjectDisplay(const LayoutTree *tree,
                                              LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    /* NONE for an id that names no box, which is the same value `display: none`
     * carries -- and that is why a caller asks LayoutObjectIsLive first
     * when the difference matters. */
    return object ? object->display : RoboCssDisplayNone;
}

Result LayoutObjectTextRun(const LayoutTree *tree,
                                         LayoutNodeId id,
                                         LayoutTextRun *out,
                                         LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading a text run needs an out");
    }
    LayoutTextRunInit(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading a text run needs a tree and an id");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no object in this tree");
    }
    /*
     * A box is refused rather than answered, and the refusal is the point: a box
     * holds a zeroed run, and a zeroed run is a *shape* a caller could read a width
     * out of. "This object is not a text run" has to be a miss.
     */
    if (object->kind != RoboLayoutObjectText) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the object is not a text run");
    }
    /*
     * And a run that was not *placed* is refused too, one level down from the same
     * reason: a text object a pass reached but declined -- a text run in a
     * block-direction context, which waits for the anonymous box -- holds a zeroed run
     * because nothing measured it, and a zeroed run read as an answer is exactly the
     * "plausible number" this module refuses to hand out. The result state is the
     * per-object authority, here as everywhere: COMPUTED means the run was placed and
     * measured, and anything else is a miss.
     */
    if (object->result.state != RoboLayoutResultComputed) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "no pass has placed this text run");
    }
    *out = object->textRun;
    return RoboOk;
}

Result LayoutObjectBaseline(const LayoutTree *tree,
                                          LayoutNodeId id,
                                          LayoutBaselineMetrics *out,
                                          LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading a baseline needs an out");
    }
    LayoutBaselineMetricsInit(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading a baseline needs a tree and an id");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no object in this tree");
    }
    /*
     * One check here where the text-run door above needs two, and the difference is
     * the placement fact itself: a baseline is written at the moment of placement and
     * by nothing else, so "has a usable baseline" and "was placed" are the same
     * statement. There is no object kind to refuse -- every object this build places
     * gets a baseline, a text run's from its font and a box's from the fallback -- so
     * the door does not ask what the object *is*, only whether it went onto a line.
     *
     * That makes a transparent inline box with contents a miss, which is the
     * documented answer rather than an oversight: such a box is a participant of the
     * context and a member of no line, so nothing placed it and there is no floor to
     * report (RoboLayoutLineBox.h).
     */
    if (!LayoutBaselineMetricsIsValid(&object->baseline)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "no pass has placed this object");
    }
    *out = object->baseline;
    return RoboOk;
}

Result LayoutObjectMargins(const LayoutTree *tree,
                                         LayoutNodeId id,
                                         LayoutMarginEdges *out,
                                         LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading margins needs an out");
    }
    /* Cleared beside no containing block: the record a caller reads after a refusal
     * is a *stated* absence rather than whatever its stack held. */
    LayoutMarginEdgesInit(out, 0);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading margins needs a tree and an id");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no object in this tree");
    }

    /*
     * Three refusals, and the header argues each: a text run is not a box, an object
     * nothing placed has no used margins, and a transparent inline box is a box CSS
     * applies no margins to.
     *
     * The middle check asks the **result** rather than the record, and that asymmetry
     * with the baseline door above is stated rather than accidental: a cleared
     * baseline has an invalid kind and could be recognised on its own, while a cleared
     * margin record *is* the initial value -- zero on all four sides -- so "never
     * placed" and "placed with no margins" are indistinguishable from the record and
     * the state is what tells them apart.
     *
     * The third reuses 10.4's doctrine rather than inventing a check: a participant of
     * an inline context is atomic exactly when it established a context of its own, so
     * an inline-level box that established none is the transparent one -- the box whose
     * rectangle is the union of what it contained and whose margins do not apply.
     */
    if (object->kind != RoboLayoutObjectBox) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a text run has no margins");
    }
    if (MakeLayoutResultState(&object->result) != RoboLayoutResultComputed) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "no pass has placed this box");
    }
    if (CssDisplayIsInlineLevel(object->display) &&
        object->contextOwned == RoboLayoutFcIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "margins do not apply to a transparent inline box");
    }

    *out = object->margins;
    return RoboOk;
}

Result LayoutObjectPadding(const LayoutTree *tree,
                                         LayoutNodeId id,
                                         LayoutPaddingEdges *out,
                                         LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading padding needs an out");
    }
    /* Cleared first, so the record a caller reads after a refusal is a *stated* absence
     * rather than whatever its stack held. */
    LayoutPaddingEdgesInit(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading padding needs a tree and an id");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no object in this tree");
    }

    /*
     * The same three refusals the margin door makes, and each for its own reason -- the
     * header argues them. The two doors are deliberately separate rather than one door with
     * a record kind, because they answer two different questions about two different
     * records and their refusals are about *the record* rather than about the box: an
     * `inline-block` has both, a transparent inline box has neither, and a text run has
     * neither while a *replaced* inline box in a later goal may have both.
     */
    if (object->kind != RoboLayoutObjectBox) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a text run has no padding");
    }
    if (MakeLayoutResultState(&object->result) != RoboLayoutResultComputed) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "no pass has sized this box");
    }
    if (CssDisplayIsInlineLevel(object->display) &&
        object->contextOwned == RoboLayoutFcIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "padding does not apply to a transparent inline box");
    }

    *out = object->padding;
    return RoboOk;
}

Result LayoutObjectBorders(const LayoutTree *tree,
                                         LayoutNodeId id,
                                         LayoutBorderEdges *out,
                                         LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading borders needs an out");
    }
    /* Cleared first, so the record a caller reads after a refusal is a *stated* absence
     * rather than whatever its stack held. */
    LayoutBorderEdgesInit(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading borders needs a tree and an id");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no object in this tree");
    }

    /*
     * The same three refusals the margin and padding doors make, and each for the reason
     * the header argues. The doors are separate rather than one with a record kind
     * because their refusals are about *the record* rather than about the box: an
     * `inline-block` has all three, a transparent inline box has none of them, and a text
     * run is not a box to apply any of them to.
     */
    if (object->kind != RoboLayoutObjectBox) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a text run has no border");
    }
    if (MakeLayoutResultState(&object->result) != RoboLayoutResultComputed) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "no pass has sized this box");
    }
    if (CssDisplayIsInlineLevel(object->display) &&
        object->contextOwned == RoboLayoutFcIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a border does not apply to a transparent inline box");
    }

    *out = object->borders;
    return RoboOk;
}

Result LayoutObjectPosition(const LayoutTree *tree,
                                          LayoutNodeId id,
                                          LayoutPositionOffset *out,
                                          LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading a position offset needs an out");
    }
    /* Cleared first, so the record a caller reads after a refusal is a *stated* absence
     * rather than whatever its stack held. */
    LayoutPositionOffsetInit(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading a position offset needs a tree and an id");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no object in this tree");
    }

    /*
     * The same three refusals the inset doors make, and each for the reason the header
     * argues -- but the *contrast* with them is the point of this door. A cleared padding
     * or border record is a legal value and a legitimate state, so those doors ask the
     * result to tell "no padding" from "no pass"; here a zero displacement is the answer
     * for every `static` box in every document, so the door answers for any placed box
     * and keeps its refusals for the cases where no offset could exist at all.
     */
    if (object->kind != RoboLayoutObjectBox) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a text run has no position offset");
    }
    if (MakeLayoutResultState(&object->result) != RoboLayoutResultComputed) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "no pass has placed this box");
    }
    if (CssDisplayIsInlineLevel(object->display) &&
        object->contextOwned == RoboLayoutFcIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a position offset does not apply to a transparent inline box");
    }

    *out = object->positionOffset;
    return RoboOk;
}

Result LayoutObjectOverflow(const LayoutTree *tree,
                                         LayoutNodeId id,
                                         LayoutOverflowEdges *out,
                                         LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading an overflow needs an out");
    }
    /* Cleared first, so the record a caller reads after a refusal is a *stated* absence
     * rather than whatever its stack held. */
    LayoutOverflowEdgesInit(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboInvalidArgument,
                                "reading an overflow needs a tree and an id");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "the id names no object in this tree");
    }

    /*
     * The same three refusals `LayoutObjectBorders` makes, for the same three
     * reasons -- and the third one is particular to this phase: a transparent inline box's
     * rectangle is its contents' union, so an overflow measured against it would be a
     * statement about a rectangle that was *derived from* the things being measured. Its
     * contents still contribute to its **parent**, through the region the parent's
     * measurement reads, which is where the honest answer lives.
     */
    if (object->kind != RoboLayoutObjectBox) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a text run has no overflow of its own");
    }
    if (MakeLayoutResultState(&object->result) != RoboLayoutResultComputed) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "no pass has placed this box");
    }
    if (CssDisplayIsInlineLevel(object->display) &&
        object->contextOwned == RoboLayoutFcIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainObject, RoboNotFound,
                                "a transparent inline box has no overflow of its own");
    }

    *out = object->overflow;
    return RoboOk;
}

/** Which link a caller asked for. */
typedef enum layoutLinkPick {
    LAYOUT_LINK_PARENT = 0,
    LAYOUT_LINK_FIRST_CHILD = 1,
    LAYOUT_LINK_LAST_CHILD = 2,
    LAYOUT_LINK_PREVIOUS = 3,
    LAYOUT_LINK_NEXT = 4
} LayoutLinkPick;

/** The selected link of @p object (NONE when absent). */
static LayoutNodeId LayoutObjectLinkOf(const LayoutObject *object,
                                                   LayoutLinkPick pick)
{
    switch (pick) {
    case LAYOUT_LINK_FIRST_CHILD:
        return object->firstChild;
    case LAYOUT_LINK_LAST_CHILD:
        return object->lastChild;
    case LAYOUT_LINK_PREVIOUS:
        return object->prevSibling;
    case LAYOUT_LINK_NEXT:
        return object->NextSibling;
    case LAYOUT_LINK_PARENT:
    default:
        return object->parent;
    }
}

/**
 * The link @p pick selects, as an id.
 *
 * One function for the five link accessors: they differ only in which field they
 * read and in what "absent" means, and five copies of the same three checks is
 * five chances for one of them to check a different subset.
 *
 * "Absent" is RoboNotFound for every one of them, which is deliberate: a box
 * whose parent is absent is the root, and a box whose previous sibling is absent
 * is the first box among its siblings. Neither is an error and neither is an id,
 * so returning an id of NONE through a successful call would be the same mistake
 * the DOM's relation callbacks avoid by having no sentinel at all.
 */
static Result LayoutObjectLink(const LayoutTree *tree,
                                        LayoutNodeId id,
                                        LayoutLinkPick pick,
                                        LayoutNodeId *out)
{
    const LayoutObject *object;
    LayoutNodeId link;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = RoboLayoutNodeIdNone;
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboInvalidArgument;
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboNotFound;
    }
    link = LayoutObjectLinkOf(object, pick);
    if (link == RoboLayoutNodeIdNone) {
        return RoboNotFound;
    }
    *out = link;
    return RoboOk;
}

Result LayoutObjectParent(const LayoutTree *tree,
                                       LayoutNodeId id,
                                       LayoutNodeId *out)
{
    return LayoutObjectLink(tree, id, LAYOUT_LINK_PARENT, out);
}

Result LayoutObjectFirstChild(const LayoutTree *tree,
                                            LayoutNodeId id,
                                            LayoutNodeId *out)
{
    return LayoutObjectLink(tree, id, LAYOUT_LINK_FIRST_CHILD, out);
}

Result LayoutObjectLastChild(const LayoutTree *tree,
                                           LayoutNodeId id,
                                           LayoutNodeId *out)
{
    return LayoutObjectLink(tree, id, LAYOUT_LINK_LAST_CHILD, out);
}

Result LayoutObjectPreviousSibling(const LayoutTree *tree,
                                                 LayoutNodeId id,
                                                 LayoutNodeId *out)
{
    return LayoutObjectLink(tree, id, LAYOUT_LINK_PREVIOUS, out);
}

Result LayoutObjectNextSibling(const LayoutTree *tree,
                                             LayoutNodeId id,
                                             LayoutNodeId *out)
{
    return LayoutObjectLink(tree, id, LAYOUT_LINK_NEXT, out);
}

Result LayoutObjectChildCount(const LayoutTree *tree,
                                            LayoutNodeId id,
                                            size_t *out)
{
    const LayoutObject *object;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = 0u;
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboInvalidArgument;
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboNotFound;
    }
    *out = object->childCount;
    return RoboOk;
}

Result LayoutObjectDepth(const LayoutTree *tree,
                                      LayoutNodeId id,
                                      uint32_t *out)
{
    const LayoutObject *object;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = 0u;
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboInvalidArgument;
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboNotFound;
    }
    *out = object->depth;
    return RoboOk;
}

int LayoutObjectIsRoot(const LayoutTree *tree,
                               LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    /*
     * Asked as "is this the tree's root" rather than "has this box no parent",
     * and the two answers are the same only because a root can only be added to an
     * empty tree. Stating it through the tree means a caller that has a *detached*
     * box someday does not silently start being told it is the root.
     */
    return object != NULL && LayoutTreeRoot(tree) == id;
}

int LayoutObjectHasChildren(const LayoutTree *tree,
                                    LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    return object != NULL && object->childCount > 0u;
}

uint32_t LayoutObjectFlags(const LayoutTree *tree,
                                  LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    return object ? object->flags : 0u;
}

int LayoutObjectHasFlag(const LayoutTree *tree,
                                LayoutNodeId id,
                                uint32_t flag)
{
    return flag != 0u &&
           (LayoutObjectFlags(tree, id) & flag) == flag;
}

Result LayoutObjectResult(const LayoutTree *tree,
                                       LayoutNodeId id,
                                       LayoutResult *out)
{
    const LayoutObject *object;

    if (!out) {
        return RoboInvalidArgument;
    }
    LayoutResultInit(out);
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboInvalidArgument;
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboNotFound;
    }
    *out = object->result;
    return RoboOk;
}

const LayoutResult *LayoutObjectResultRef(
    const LayoutTree *tree,
    LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    return object ? &object->result : NULL;
}

/**
 * Whether @p id names a context that exists in @p tree *right now*.
 *
 * The ids on a box are a cache of a classification, and a classification is
 * invalidated whenever the box set changes (RoboLayoutTree.h). So the box's own
 * field is not enough to answer with: a box can hold an id from a classification
 * that has since been released, and handing that id back would be a stale answer
 * a caller could not tell from a live one.
 *
 * Validating here rather than zeroing every box's ids in the invalidation is a
 * deliberate trade. Zeroing is O(boxes) per invalidation, and invalidation happens
 * on *every add* -- so a build of a four-thousand-box document would be
 * quadratic in a walk that exists only to write two zeroes into each box. This
 * check is two comparisons, and it makes the property the module actually needs
 * true: an id that names nothing is a reported miss, wherever it came from.
 */
static int LayoutObjectContextIsLive(const LayoutTree *tree,
                                        LayoutFcId id)
{
    return id != RoboLayoutFcIdNone && LayoutFcSlotConst(tree, id) != NULL;
}

Result LayoutObjectContext(const LayoutTree *tree,
                                        LayoutNodeId id,
                                        LayoutFcId *out,
                                        LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "out is NULL");
    }
    *out = RoboLayoutFcIdNone;
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "tree is NULL or the id is NONE");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound, "the id names no box in this tree");
    }
    /* NOT_FOUND covers both "nobody has classified this tree" and "the
     * classification this box carries has been released", because they are the
     * same statement to a caller: there is no context to read. Which one it was is
     * a fact about the *tree*, answered by `LayoutTreeIsClassified`. */
    if (!LayoutObjectContextIsLive(tree, object->context)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound,
                                "the box participates in no live formatting context");
    }
    *out = object->context;
    return RoboOk;
}

Result LayoutObjectContextOwned(const LayoutTree *tree,
                                               LayoutNodeId id,
                                               LayoutFcId *out,
                                               LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "out is NULL");
    }
    *out = RoboLayoutFcIdNone;
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "tree is NULL or the id is NONE");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound, "the id names no box in this tree");
    }
    if (!LayoutObjectContextIsLive(tree, object->contextOwned)) {
        /* The common answer, and not a failure: this box is transparent to
         * formatting context, or it is an inline box, or the classification that
         * gave it one has been released. Either way its contents are not formatted
         * by a context of its own that a caller can inspect. */
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound,
                                "the box establishes no live formatting context of its own");
    }
    *out = object->contextOwned;
    return RoboOk;
}

int LayoutObjectEstablishesContext(const LayoutTree *tree,
                                           LayoutNodeId id)
{
    const LayoutObject *object = LayoutObjectSlotConst(tree, id);

    return object != NULL && LayoutObjectContextIsLive(tree, object->contextOwned);
}

/**
 * Whether @p id names a line this tree actually holds.
 *
 * The same rule the context accessor above states, applied to the third arena: a
 * box's `line` field is a cache of a pass's conclusion, and a released line must be a
 * reported miss rather than a number that happens to name a different line -- which
 * it would, because ids restart at 1 with each pass.
 */
static int LayoutObjectLineIsLive(const LayoutTree *tree,
                                      LayoutLineId id)
{
    return id != RoboLayoutLineIdNone && LayoutLineSlotConst(tree, id) != NULL;
}

Result LayoutObjectLine(const LayoutTree *tree,
                                      LayoutNodeId id,
                                      LayoutLineId *out,
                                      LayoutDiag *diag)
{
    const LayoutObject *object;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "out is NULL");
    }
    *out = RoboLayoutLineIdNone;
    if (!tree || id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout,
                                RoboInvalidArgument, "tree is NULL or the id is NONE");
    }
    object = LayoutObjectSlotConst(tree, id);
    if (!object) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the id names no object in this tree");
    }
    /*
     * Three routes to one answer, and the door's own note in RoboLayoutObject.h lists
     * them: never placed, placed by a released pass, or a transparent inline box with
     * contents -- which is a participant of its context and a member of no single
     * line, because its rectangle is the union of what it encloses.
     */
    if (!LayoutObjectLineIsLive(tree, object->line)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the object was placed on no line this tree holds");
    }
    *out = object->line;
    return RoboOk;
}
