/*
 * RoboLayout tree (Phase 10.1).
 *
 * The container: an owned arena of boxes, the links between them, and the
 * lookup the rest of the module goes through.
 *
 * **Why a tree at all, when there is already a DOM.** Because the two trees do
 * not have the same members. An element with `display: none` is a DOM node and
 * generates no box; a text node is a DOM node and will generate something in
 * 10.5 that is not an element box; a future `::before` is a box with no element
 * behind it. A layout pass that walked the DOM would have to re-decide, at every
 * step, which nodes it was allowed to look at -- and every later goal (line
 * breaking, anonymous blocks, fragments) would add another such decision in
 * another place. The tree is built *once*, by the builder, so that the pass walks
 * boxes and only boxes.
 *
 * **Identity is dense and tree-scoped.** Object ids are 1-based and assigned in
 * creation order, so `id == index + 1` holds until the tree is cleared, and
 * `LayoutObjectIdAt` makes walking the whole tree a loop. Ids are not
 * reused: clearing a tree restarts the cursor, which is why a tree is cleared and
 * rebuilt rather than edited.
 *
 * **The arena grows; borrowed pointers do not survive it.** `LayoutObject`
 * hands out a borrowed pointer for the same reason RoboHTML's node view does --
 * one call gets every field -- and it is invalidated by the next `add_object`,
 * because the arena may move. Keeping an id and looking it up again is the
 * supported way to hold on to a box.
 *
 * **Building is add-only.** There is no remove, no re-parent and no reorder. A
 * tree that could be edited while a pass walks it would need the pass to be told
 * about it, and nothing in 10.1 needs the capability: DOM mutation rebuilds the
 * tree, which is the honest model for a phase whose tree is derived state.
 *
 * The second arena (Phase 10.2)
 *
 * A tree owns *two* arrays: the boxes, and the formatting contexts a pass
 * classified them into. The second is derived state exactly as a box's result is,
 * and it lives here rather than in the pass for one reason: a formatting context
 * is a fact about the *tree*, so a caller that has a tree must be able to ask
 * about it without also holding the pass that produced it. A pass that owned its
 * contexts would make "what context does this box participate in?" a question
 * with a lifetime, and every later phase would have to be handed the pass.
 *
 * The arena is *invalidated*, not merged: adding an object releases it and marks
 * the tree unclassified, because a participant count and a child-composition
 * classification are both statements about a box set that has just changed. The
 * alternative -- leaving a stale classification in place and hoping the caller
 * re-runs a pass -- is the one failure mode that would be invisible until a paint
 * phase drew the wrong thing.
 *
 * `LayoutTreeIsClassified` is therefore the door that distinguishes "no
 * pass has run" from "a pass ran over an empty tree", and it is why the two ids
 * on a box (RoboLayoutObject.h) can stay NONE without being ambiguous.
 */

#ifndef ROBO_LAYOUT_TREE_H
#define ROBO_LAYOUT_TREE_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An opaque, caller-owned arena of layout objects. */
typedef struct LayoutTree LayoutTree;

/**
 * Everything needed to create one box.
 *
 * A descriptor rather than six positional arguments, for the same reason
 * RoboHTML's element-create descriptor is one: the builder is the only caller
 * today, and the goals that follow will extend it (a kind, an anonymous flag, a
 * text payload) without every call site changing shape.
 */
typedef struct LayoutObjectInfo {
    /** Parent to link under as its last child. RoboLayoutNodeIdNone means
     *  "this is the root object", which is only accepted for an empty tree. */
    LayoutNodeId parent;
    /**
     * What to create: a box, or a text run (RoboLayoutTypes.h).
     *
     * `LayoutObjectInfoInit` defaults it to `BOX`, because that is what a
     * builder makes most of and because a descriptor whose kind nobody set should
     * describe the common thing rather than a third state nothing creates. A text
     * object is therefore **opt-in**, and the two kinds carry different
     * obligations below.
     */
    LayoutObjectKind kind;
    /** The DOM node this object comes from: an element for a box, a text node for
     *  a text object. Must be well formed, and must be the right kind of node. */
    HtmlNodeHandle domNode;
    /**
     * Borrowed computed style, or NULL. Must outlive the tree.
     *
     * Read for a box's `display` and borrowed by it; borrowed by a text object
     * from its **parent element**, because inherited properties are what a run
     * needs (RoboLayoutText.h).
     */
    const CssComputedStyle *style;
    /**
     * The display to create the object under. Must generate a box.
     *
     * Meaningful for a BOX, and **ignored** for a text object: a text object's
     * display is derived as `inline` (RoboLayoutObject.h says why), so a caller
     * cannot get it wrong by leaving it at the initial value.
     */
    CssDisplay display;
    /** ROBO_LAYOUT_OBJECT_FLAG_* bits; unset bits are added by the tree when it
     *  can tell (a NULL style sets STYLE_MISSING). */
    uint32_t flags;
} LayoutObjectInfo;

/**
 * Zero @p info: no parent (a root candidate), no node, no style, and the kind a
 * builder makes most of -- an element box.
 */
void LayoutObjectInfoInit(LayoutObjectInfo *info);

/**
 * Create an empty tree.
 * @return RoboOk; RoboInvalidArgument for NULL output; RoboOutOfMemory.
 */
Result LayoutTreeCreate(LayoutTree **outTree,
                                      LayoutDiag *diag);

/** Destroy @p tree and every box in it. Borrowed styles and DOM handles are the
 *  caller's and are not touched. Safe on NULL. */
void LayoutTreeDestroy(LayoutTree *tree);

/** Release every box, keeping the tree usable. Borrowed styles are not touched.
 *  After this the tree is empty and ids restart at 1. Safe on NULL. */
void LayoutTreeClear(LayoutTree *tree);

/** Number of boxes (0 for NULL). */
size_t LayoutTreeObjectCount(const LayoutTree *tree);

/** Capacity of the arena; >= the count. Exposed because a test that asserts the
 *  arena *grew* is asserting the one thing about it a caller can observe. */
size_t LayoutTreeCapacity(const LayoutTree *tree);

/** Non-zero when the tree holds no box (also true for NULL). */
int LayoutTreeIsEmpty(const LayoutTree *tree);

/** Id of the root box, or RoboLayoutNodeIdNone when the tree is empty. */
LayoutNodeId LayoutTreeRoot(const LayoutTree *tree);

/**
 * Fill @p out with info for the object at @p index in creation order.
 * @return RoboOk; RoboInvalidArgument for NULL args; RoboNotFound for an
 *         index at or past the count. Ids are dense, so the id of the object at
 *         @p index is `index + 1`; this door exists so a walk needs no
 *         arithmetic of its own.
 */
Result LayoutObjectIdAt(const LayoutTree *tree,
                                       size_t index,
                                       LayoutNodeId *out);

/**
 * Append one box.
 *
 * Validates rather than trusts, because every rule here is one a later goal
 * would otherwise have to re-check while walking:
 *
 *   - @p info->parent must be NONE only when the tree is empty (one root); a
 *     parent id must name a live box in *this* tree.
 *   - @p info->kind must be a defined object kind other than NONE.
 *   - @p info->domNode must be well formed (an object always came from something
 *     the DOM can be asked about), and must be the *right kind* of node: an
 *     element for a BOX, a text node for a TEXT object. That is checked rather
 *     than assumed because the two kinds read different payload fields, so a
 *     mismatch would not fail here -- it would produce an object whose text is
 *     empty or whose style lookup is meaningless, several phases later.
 *   - @p info->display must generate a box (`CssDisplayGeneratesBox`)
 *     when the kind is BOX; a `display: none` element has no box by definition, so
 *     creating one would make the tree a copy of the DOM's element list instead of
 *     what layout walks. A TEXT object's display is derived as `inline` and the
 *     field is ignored (RoboLayoutObject.h).
 *
 * A NULL style is accepted and sets STYLE_MISSING rather than failing: a caller
 * building a tree without CSS is doing something legitimate (a structural-only
 * walk), and it is told what it has.
 *
 * @return RoboOk; RoboInvalidArgument (NULL arguments, a NONE or ill-formed
 *         element handle, a display that generates no box, an unknown flag, a
 *         parent id of NONE in a non-empty tree); RoboNotFound (a parent id
 *         that names no box in this tree); RoboAlreadyExists (a second root);
 *         RoboOutOfMemory.
 */
Result LayoutTreeAddObject(LayoutTree *tree,
                                          const LayoutObjectInfo *info,
                                          LayoutNodeId *outId,
                                          LayoutDiag *diag);

/**
 * Borrowed object for @p id, or NULL when the id names no box.
 *
 * The whole struct in one call, and the reason a walk of a thousand boxes is a
 * thousand lookups rather than six thousand. Valid until the next add.
 */
const LayoutObject *GetLayoutObject(const LayoutTree *tree,
                                               LayoutNodeId id);

/**
 * The box built for @p domNode, or RoboLayoutNodeIdNone when the element
 * generated none.
 *
 * The reverse of LayoutObjectDomNode, and the door that makes a
 * `display: none` element distinguishable from an element nobody built: the
 * first answers NONE, the second answers NONE too, and a caller that needs the
 * difference compares the element against the report the build returned
 * (RoboLayoutBuilder.h).
 */
LayoutNodeId LayoutObjectIdForDomNode(
    const LayoutTree *tree,
    const HtmlNodeHandle *domNode);

/* ------------------------------------------------------------------ */
/* Formatting contexts (Phase 10.2)                                    */
/* ------------------------------------------------------------------ */

/**
 * Number of formatting contexts, or 0 when no pass has classified this tree.
 *
 * Zero is therefore also the answer for a classified but empty tree, which is why
 * @ref LayoutTreeIsClassified exists: "nothing to classify" and "not
 * classified" are different statements, and a caller that conflated them would
 * silently treat an unclassifiable document as one with no contexts.
 */
size_t LayoutTreeContextCount(const LayoutTree *tree);

/** Non-zero once a pass has classified this tree. A build, a clear and an add
 *  all make it 0 again. */
int LayoutTreeIsClassified(const LayoutTree *tree);

/**
 * Release the classification, keeping every box.
 *
 * The door a caller reaches for when a box's *style* changed without the box set
 * changing -- e.g. a stylesheet mutation followed by a style refresh
 * (RoboLayoutDom.h) on a document whose element tree is unchanged. The tree keeps
 * each box's `display` from the build, so a caller that changed what that display
 * would be must rebuild the tree; what this door releases is the classification
 * derived from the boxes as they were.
 *
 * Safe on NULL. Invalidating an unclassified tree is a no-op rather than an
 * error, because "make sure nothing is classified" is a legitimate thing to want
 * idempotently.
 */
void LayoutTreeInvalidateContexts(LayoutTree *tree);

/**
 * Context at @p index in creation order.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound for
 *         an index at or past the count. Contexts are dense and 1-based, so the
 *         context at @p index has id `index + 1`; this door exists so a walk needs
 *         no arithmetic of its own, exactly as `LayoutObjectIdAt` does for
 *         boxes.
 */
Result LayoutTreeContextAt(const LayoutTree *tree,
                                         size_t index,
                                         LayoutFormattingContext *out,
                                         LayoutDiag *diag);

/**
 * Context by id, by value.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a NONE id;
 *         RoboNotFound for an id that names no context in @p tree. @p out is
 *         cleared first, so a failure never leaves a half-filled copy.
 */
Result LayoutTreeContext(const LayoutTree *tree,
                                      LayoutFcId id,
                                      LayoutFormattingContext *out,
                                      LayoutDiag *diag);

/**
 * The tree's initial formatting context.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         the tree has not been classified or has no root box -- a document that
 *         generated nothing has no initial context, which is the truth rather
 *         than a default to invent.
 */
Result LayoutTreeRootContext(const LayoutTree *tree,
                                           LayoutFcId *out,
                                           LayoutDiag *diag);

/* ------------------------------------------------------------------ */
/* Line boxes (10.6)                                                   */
/* ------------------------------------------------------------------ */

/**
 * Number of line boxes this tree holds, or 0 when no pass has laid it out.
 *
 * Zero is also the answer for a laid-out tree that produced no lines -- a document
 * with no inline content, or a viewport-free pass -- and the two are told apart the
 * way this module tells apart every other pair of "nothing" and "not yet": by asking
 * the pass's own record (`LayoutInlineStats.lines`, or the block record's
 * placement counts) rather than by inferring one from the other.
 *
 * Lines are released by the next pass and by anything that releases the
 * classification, so this count always describes *the last pass*.
 */
size_t LayoutTreeLineCount(const LayoutTree *tree);

/**
 * Identity of the line at @p index in finalization order.
 *
 * @return RoboOk and the id; RoboInvalidArgument for NULL arguments;
 *         RoboNotFound for an index at or past the count. Line ids are dense and
 *         1-based, so the line at @p index has id `index + 1`; this door exists so a
 *         walk needs no arithmetic of its own, exactly as the context and object
 *         doors do.
 *
 * The order is the order the pass *finalized* lines, which for one context is the
 * order they were laid out -- but the lines of two contexts interleave in this list,
 * so a caller that wants one context's lines asks for them by context and index
 * (`LayoutLineBox.context` and @ref `.index`) rather than by position here.
 */
Result LayoutTreeLineAt(const LayoutTree *tree,
                                       size_t index,
                                       LayoutLineId *outId,
                                       LayoutDiag *diag);

/**
 * Line box by id, by value.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a NONE id;
 *         RoboNotFound for an id that names no line in @p tree -- which includes
 *         a line of a previous pass, because a line id is valid until the next one.
 *         @p out is cleared first, so a failure never leaves a half-filled copy.
 */
Result LayoutTreeLine(const LayoutTree *tree,
                                    LayoutLineId id,
                                    LayoutLineBox *out,
                                    LayoutDiag *diag);

/**
 * The @p index'th member of line @p id, in layout order, as the object's id.
 *
 * The members are the objects the line *placed*, which is not the same set as the
 * context's participants: a transparent inline box with contents encloses members
 * without being one (RoboLayoutLineBox.h). To go the other way -- from an object to
 * its line -- a caller uses `LayoutObjectLine` (RoboLayoutObject.h).
 *
 * O(index): a line's members are a chain because the placements of two lines
 * interleave, so the walk is proportional to the position asked for. The count in the
 * record is what bounds it, which is why the index is validated before the walk rather
 * than during it.
 *
 * @return RoboOk and the object id; RoboInvalidArgument for NULL arguments or a
 *         NONE id; RoboNotFound for an id that names no line, or an index at or
 *         past the line's member count.
 */
Result GetLayoutLineMember(const LayoutTree *tree,
                                     LayoutLineId id,
                                     size_t index,
                                     LayoutNodeId *outNode,
                                     LayoutDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_TREE_H */
