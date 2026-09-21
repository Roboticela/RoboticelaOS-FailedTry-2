/*
 * RoboLayout tree (Phase 10.1) — see RoboLayoutTree.h.
 *
 * The arena, the links, and the lookup. Every validation rule the header states
 * lives here rather than in the builder, because the builder is one caller and a
 * rule enforced in the container is a rule no future caller can forget.
 */

#include "RoboLayout/RoboLayoutTree.h"

#include "RoboLayoutInternal.h"

#include "RoboCSS/RoboCSSDisplay.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

void LayoutObjectInfoInit(LayoutObjectInfo *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->parent = RoboLayoutNodeIdNone;
    /*
     * The common case, stated as the default so a caller cannot get a third
     * "kind nobody set" state: a descriptor describes an element box unless it
     * says otherwise. The display below is deliberately the opposite default --
     * NONE rather than `inline` -- because a descriptor that forgot its display
     * must NOT accidentally claim a box, while a descriptor that forgot its kind
     * is simply the ordinary one.
     */
    info->kind = RoboLayoutObjectBox;
    info->display = RoboCssDisplayNone;
}

int LayoutObjectIndex(const LayoutTree *tree,
                             LayoutNodeId id,
                             size_t *outIndex)
{
    size_t index;

    if (!tree || id == RoboLayoutNodeIdNone) {
        return 0;
    }
    /*
     * Ids are dense, so the id *is* the index plus one. The comparison against
     * `count` is what makes arithmetic safe: an id handed out before a clear, or
     * never handed out at all, is past the end and therefore a miss rather than an
     * alias -- which is the whole reason ids restart on clear instead of being
     * recycled.
     */
    index = (size_t)(id - 1u);
    if (index >= tree->count) {
        return 0;
    }
    if (tree->objects[index].id != id) {
        return 0;
    }
    if (outIndex) {
        *outIndex = index;
    }
    return 1;
}

const LayoutObject *LayoutObjectSlotConst(const LayoutTree *tree,
                                                          LayoutNodeId id)
{
    size_t index;

    if (!LayoutObjectIndex(tree, id, &index)) {
        return NULL;
    }
    return &tree->objects[index];
}

LayoutObject *LayoutObjectSlot(LayoutTree *tree,
                                              LayoutNodeId id)
{
    size_t index;

    if (!LayoutObjectIndex(tree, id, &index)) {
        return NULL;
    }
    return &tree->objects[index];
}

const LayoutObject *LayoutObjectArray(const LayoutTree *tree)
{
    return tree ? tree->objects : NULL;
}

Result LayoutTreeCreate(LayoutTree **outTree,
                                      LayoutDiag *diag)
{
    LayoutTree *tree;

    if (!outTree) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                "out_tree is NULL");
    }
    *outTree = NULL;
    tree = (LayoutTree *)RoboCalloc(1u, sizeof(*tree));
    if (!tree) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboOutOfMemory,
                                "could not allocate the layout tree");
    }
    /* One allocation up front, so a tree that is never used costs nothing more and
     * a tree that is used does not start with a realloc. */
    tree->objects = (LayoutObject *)RoboCalloc(RoboLayoutInitialCapacity,
                                                       sizeof(LayoutObject));
    if (!tree->objects) {
        RoboFree(tree);
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboOutOfMemory,
                                "could not allocate the layout object arena");
    }
    tree->capacity = RoboLayoutInitialCapacity;
    tree->count = 0u;
    tree->nextId = 1u;
    /* No context arena, and that is deliberate rather than an oversight: a tree
     * that is built and never classified owns no contexts, so the second
     * allocation waits for the pass that needs it (see the internal header). */
    tree->contexts = NULL;
    tree->contextCount = 0u;
    tree->contextCapacity = 0u;
    tree->classified = 0;
    /*
     * And no line boxes either, for the same reason: a tree that is built and never
     * laid out owns no lines, so the third and fourth allocations wait for the pass
     * that makes them (the internal header).
     */
    tree->lines = NULL;
    tree->lineCount = 0u;
    tree->lineCapacity = 0u;
    tree->members = NULL;
    tree->memberCount = 0u;
    tree->memberCapacity = 0u;
    *outTree = tree;
    return RoboOk;
}

void LayoutTreeDestroy(LayoutTree *tree)
{
    if (!tree) {
        return;
    }
    /* One free per arena: no recursion, no per-node teardown to get wrong, and
     * nothing borrowed is touched. Four frees since 10.6, and the line and member
     * arenas are released by their own reset so that this one place and the
     * invalidation path cannot disagree about what releasing a line means. */
    LayoutTreeLinesReset(tree);
    RoboFree(tree->contexts);
    tree->contexts = NULL;
    tree->contextCount = 0u;
    tree->contextCapacity = 0u;
    tree->classified = 0;
    RoboFree(tree->objects);
    tree->objects = NULL;
    tree->count = 0u;
    tree->capacity = 0u;
    tree->nextId = 1u;
    RoboFree(tree);
}

void LayoutTreeContextsReset(LayoutTree *tree)
{
    size_t index;

    if (!tree) {
        return;
    }
    /* Released rather than zeroed: a classification is rebuilt wholesale by the
     * next pass, so keeping a grown arena to overwrite would be holding memory
     * for a shape nobody has asked for yet. */
    RoboFree(tree->contexts);
    tree->contexts = NULL;
    tree->contextCount = 0u;
    tree->contextCapacity = 0u;
    tree->classified = 0;

    /*
     * And the geometry goes with it (Phase 10.3).
     *
     * Block layout is a function of the classification: the placement gate *is*
     * `object->context`, and a box's height is the extent of the children that gate
     * let through. So the moment a classification stops describing the tree, every
     * coordinate derived from it is a statement about a tree that no longer exists
     * -- and it would still be marked COMPUTED, which is the one thing that state
     * is not allowed to mean.
     *
     * This is the shared path on purpose. All three callers -- an explicit
     * invalidation, a clear, and an add -- are saying the same thing ("the box set
     * changed or the classification was withdrawn"), and a rule written three times
     * is a rule two of them will eventually disagree about. It also means a caller
     * that adds a box after a pass gets results that are honestly UNRESOLVED rather
     * than numbers computed with the box set of a moment ago.
     *
     * The boxes themselves are untouched: a result is not part of what a box *is*, it
     * is what a pass concluded about it, and "unresolved" is a conclusion this
     * module can state truthfully.
     *
     * What is withdrawn is a *conclusion*, so a box that never had one keeps the
     * one it has -- which is NONE, and is not the same fact as UNRESOLVED. Only a
     * result that claimed something is demoted; a fresh box in a fresh tree stays
     * NONE, because nobody has computed its geometry *and* nobody has said that
     * nobody has. A blunt `set_unresolved` over every slot would erase that
     * distinction: it would make NONE a state no path in this module produces
     * (every add would manufacture UNRESOLVED for every box, including the ones
     * the add did not touch), and it would make a tree built by eight adds
     * indistinguishable from one a pass had swept without placing anything. The
     * conditional is the whole of the difference, and it is what keeps
     * `LayoutResultInit`'s state reachable.
     */
    for (index = 0u; index < tree->count; index++) {
        LayoutResult *result = &tree->objects[index].result;

        if (MakeLayoutResultState(result) != RoboLayoutResultNone) {
            LayoutResultSetUnresolved(result);
        }
    }

    /*
     * And the line boxes go with the geometry (Phase 10.6), which is the same
     * argument one step further on: a line box is a conclusion *derived from* the
     * results -- it groups the participants a pass placed and states the extent they
     * reached -- so a line box that outlived the classification would be a rectangle
     * about a layout that no longer holds, plus member links to objects whose results
     * had just been withdrawn. Both are refused by construction rather than by a
     * check somebody has to remember.
     */
    LayoutTreeLinesReset(tree);
}

void LayoutTreeInvalidateContexts(LayoutTree *tree)
{
    LayoutTreeContextsReset(tree);
    /*
     * Releasing the classification makes every conclusion stale, not just the
     * contexts (10.14): the placement gate *is* the formatting context
     * (RoboLayoutBlock.h), so a tree nobody has classified is a tree whose geometry
     * cannot be trusted. Marking here rather than asking a caller to remember is what
     * makes the two halves of "this tree needs work" impossible to get out of step.
     */
    if (tree) {
        (void)LayoutTreeMarkAll(tree, RoboLayoutInvalidationReasonContent, NULL);
    }
}

void LayoutTreeMarkClassified(LayoutTree *tree)
{
    if (tree) {
        tree->classified = 1;
    }
}

void LayoutTreeClear(LayoutTree *tree)
{
    if (!tree) {
        return;
    }
    if (tree->objects && tree->capacity > 0u) {
        memset(tree->objects, 0, tree->capacity * sizeof(LayoutObject));
    }
    tree->count = 0u;
    tree->nextId = 1u;
    /* The invalidation count describes a box set too, and it is gone with the same
     * memset that took the records (10.14). Nothing else has to be done: a zeroed
     * object carries a CLEAN record by construction. */
    tree->invalidated = 0u;
    /* A classification describes a box set, and the box set is gone. The ids on
     * the boxes are zeroed with them, so no box can be left holding an id that
     * names a context in an arena that no longer exists. */
    LayoutTreeContextsReset(tree);
}

size_t LayoutTreeObjectCount(const LayoutTree *tree)
{
    return tree ? tree->count : 0u;
}

size_t LayoutTreeCapacity(const LayoutTree *tree)
{
    return tree ? tree->capacity : 0u;
}

int LayoutTreeIsEmpty(const LayoutTree *tree)
{
    return !tree || tree->count == 0u;
}

LayoutNodeId LayoutTreeRoot(const LayoutTree *tree)
{
    /*
     * The root is always slot 0: a root can only be added to an empty tree, and
     * nothing removes an object, so the first box ever added stays the first.
     */
    if (!tree || tree->count == 0u) {
        return RoboLayoutNodeIdNone;
    }
    return tree->objects[0].id;
}

Result LayoutObjectIdAt(const LayoutTree *tree,
                                       size_t index,
                                       LayoutNodeId *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = RoboLayoutNodeIdNone;
    if (!tree) {
        return RoboInvalidArgument;
    }
    if (index >= tree->count) {
        return RoboNotFound;
    }
    *out = tree->objects[index].id;
    return RoboOk;
}

const LayoutObject *GetLayoutObject(const LayoutTree *tree,
                                               LayoutNodeId id)
{
    return LayoutObjectSlotConst(tree, id);
}

/**
 * Make room for one more object, doubling the arena.
 * @return RoboOk, or RoboOutOfMemory leaving the tree untouched.
 */
static Result LayoutTreeReserve(LayoutTree *tree,
                                         LayoutDiag *diag)
{
    size_t wanted;
    LayoutObject *grown;

    if (tree->count < tree->capacity) {
        return RoboOk;
    }
    wanted = tree->capacity * 2u;
    if (wanted <= tree->capacity) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboOutOfMemory,
                                "layout tree capacity overflowed");
    }
    grown = (LayoutObject *)Realloc(tree->objects,
                                                wanted * sizeof(LayoutObject));
    if (!grown) {
        /* The old arena is still valid and still owned, so a failed add leaves a
         * usable tree rather than a dangling one. */
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboOutOfMemory,
                                "could not grow the layout object arena");
    }
    /* Zero the new half so that no field of a fresh slot is inherited from
     * whatever the allocator had. */
    memset(grown + tree->capacity, 0,
           (wanted - tree->capacity) * sizeof(LayoutObject));
    tree->objects = grown;
    tree->capacity = wanted;
    return RoboOk;
}

Result LayoutTreeAddObject(LayoutTree *tree,
                                          const LayoutObjectInfo *info,
                                          LayoutNodeId *outId,
                                          LayoutDiag *diag)
{
    LayoutObject *object;
    LayoutObject *parent;
    LayoutNodeId id;
    size_t index;
    int isRoot;

    if (outId) {
        *outId = RoboLayoutNodeIdNone;
    }
    if (!tree || !info || !outId) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                "tree, info or out_id is NULL");
    }
    if (!LayoutObjectKindIsKnown(info->kind) ||
        info->kind == RoboLayoutObjectNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                "the object kind is not a defined kind");
    }
    if (!HtmlNodeHandleIsWellformed(&info->domNode)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                "the node handle is not well formed");
    }
    /*
     * The node must be the *right kind* of node for the object being built, and
     * the check is here rather than in the builder because it is the invariant the
     * arena is allowed to rely on afterwards: a text object whose handle named an
     * element would read an empty payload and measure nothing, and an element box
     * whose handle named a text node would have a `display` nobody computed. Both
     * would build, and both would be wrong several phases later, so the mismatch
     * fails where it is made.
     */
    {
        HtmlNodeView nodeView;

        if (HtmlNodeGetView(&info->domNode, &nodeView, NULL) != RoboOk) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboNotFound,
                                    "the node handle did not resolve");
        }
        if (info->kind == RoboLayoutObjectText) {
            if (nodeView.kind != RoboHtmlNodeKindText) {
                return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                        "a text object's node must be a text node");
            }
        } else if (nodeView.kind != RoboHtmlNodeKindElement) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                    "a box's node must be an element");
        }
    }
    if (info->kind == RoboLayoutObjectBox && !CssDisplayGeneratesBox(info->display)) {
        /* `display: none` means no box. Accepting one would make this tree a copy
         * of the DOM's element list, and every later pass would have to re-filter
         * what the container had already failed to. */
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                "the display does not generate a box");
    }
    if ((info->flags & ~(uint32_t)RoboLayoutObjectFlagAll) != 0u) {
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboInvalidArgument,
                                "object flags carry an undefined bit");
    }

    isRoot = info->parent == RoboLayoutNodeIdNone;
    if (isRoot) {
        if (tree->count != 0u) {
            return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboAlreadyExists,
                                    "the tree already has a root box");
        }
    } else if (!LayoutObjectSlot(tree, info->parent)) {
        /* Existence only: the *pointer* is taken below, after the reserve. */
        return RoboLayoutDiag(diag, RoboLayoutDomainTree, RoboNotFound,
                                "the parent id names no box in this tree");
    }

    if (LayoutTreeReserve(tree, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }

    /*
     * Resolved *after* the reserve, and that ordering is load-bearing rather than
     * tidy. The reserve may realloc the arena, so a parent slot taken before it is
     * a dangling pointer the moment the arena moves -- and the arena only moves
     * once a tree outgrows RoboLayoutInitialCapacity, so the symptom is a tree
     * that builds correctly for eight boxes and corrupts the eighth parent after
     * that. Reading the parent through a stale pointer is the one way an add-only
     * tree with realloc-backed storage can be wrong.
     */
    parent = isRoot ? NULL : LayoutObjectSlot(tree, info->parent);

    /*
     * The classification is released by the reserve above, which may have moved
     * the context arena as well as the object one. Done here rather than at the
     * top so a *refused* add -- a bad display, a second root, an unknown flag --
     * leaves the tree exactly as it was: a classification is invalid only because
     * the box set changed, and a refused add does not change it.
     */
    LayoutTreeContextsReset(tree);

    id = tree->nextId++;
    index = tree->count;
    object = &tree->objects[index];
    memset(object, 0, sizeof(*object));
    object->id = id;
    object->kind = info->kind;
    object->domNode = info->domNode;
    object->style = info->style;
    /*
     * A text object's display is *derived*, not copied: its content is
     * inline-level by construction and no declaration could make it otherwise
     * (RoboLayoutObject.h). Deriving it here rather than asking the caller to set
     * it is what keeps the two passes free of a special case -- they dispatch on
     * this field, and a text object reads as an inline participant because that is
     * what it is.
     */
    object->display = info->kind == RoboLayoutObjectText ? RoboCssDisplayInline
                                                           : info->display;
    object->parent = isRoot ? RoboLayoutNodeIdNone : info->parent;
    object->firstChild = RoboLayoutNodeIdNone;
    object->lastChild = RoboLayoutNodeIdNone;
    object->prevSibling = RoboLayoutNodeIdNone;
    object->NextSibling = RoboLayoutNodeIdNone;
    object->childCount = 0u;
    object->depth = parent ? parent->depth + 1u : 0u;
    object->flags = info->flags;
    if (!info->style) {
        /* Stated, not inferred: a box with no style is a box a formatting context
         * must not read a property off. */
        object->flags |= RoboLayoutObjectFlagStyleMissing;
    }
    /* No pass has spoken about this box yet. The *pass* is what moves it to
     * UNRESOLVED, so "the tree was built" and "a pass saw it" stay different
     * facts -- see RoboLayoutResult.h. */
    LayoutResultInit(&object->result);
    /*
     * And nothing has concluded anything about it either (10.14). A box that has just
     * arrived has no geometry anybody computed, so it owes the next pass all three
     * layout bits -- and it is marked *here* rather than left to the caller, because a
     * caller that forgot would produce a box a pass skipped. The reason is CONTENT:
     * what changed is what the document contains.
     */
    LayoutInvalidationMarkNew(tree, object);

    if (parent) {
        if (parent->firstChild == RoboLayoutNodeIdNone) {
            parent->firstChild = id;
        } else {
            LayoutObject *previous =
                LayoutObjectSlot(tree, parent->lastChild);

            previous->NextSibling = id;
            object->prevSibling = previous->id;
        }
        parent->lastChild = id;
        parent->childCount++;
    }

    tree->count = index + 1u;
    *outId = id;
    return RoboOk;
}

LayoutNodeId LayoutObjectIdForDomNode(
    const LayoutTree *tree,
    const HtmlNodeHandle *domNode)
{
    size_t index;

    if (!tree || !domNode) {
        return RoboLayoutNodeIdNone;
    }
    for (index = 0u; index < tree->count; index++) {
        if (HtmlNodeHandleEqual(&tree->objects[index].domNode, domNode)) {
            return tree->objects[index].id;
        }
    }
    return RoboLayoutNodeIdNone;
}

/* ------------------------------------------------------------------ */
/* The formatting-context arena (Phase 10.2)                           */
/* ------------------------------------------------------------------ */

const LayoutFormattingContext *LayoutFcSlotConst(
    const LayoutTree *tree,
    LayoutFcId id)
{
    size_t index;

    if (!tree || id == RoboLayoutFcIdNone) {
        return NULL;
    }
    /* Dense and 1-based, and the id is checked against the slot rather than only
     * against the count -- the same discipline the object arena uses, so an id
     * from a previous classification is a miss rather than an alias. */
    index = (size_t)(id - 1u);
    if (index >= tree->contextCount) {
        return NULL;
    }
    if (tree->contexts[index].id != id) {
        return NULL;
    }
    return &tree->contexts[index];
}

LayoutFormattingContext *LayoutFcSlot(LayoutTree *tree,
                                                     LayoutFcId id)
{
    size_t index;

    if (!tree || id == RoboLayoutFcIdNone) {
        return NULL;
    }
    index = (size_t)(id - 1u);
    if (index >= tree->contextCount) {
        return NULL;
    }
    if (tree->contexts[index].id != id) {
        return NULL;
    }
    return &tree->contexts[index];
}

/**
 * Make room for one more context, doubling the arena.
 * @return RoboOk, or RoboOutOfMemory leaving the tree untouched.
 */
static Result LayoutTreeContextReserve(LayoutTree *tree,
                                                LayoutDiag *diag)
{
    size_t wanted;
    LayoutFormattingContext *grown;

    if (tree->contexts == NULL) {
        tree->contexts = (LayoutFormattingContext *)RoboCalloc(
            RoboLayoutInitialContextCapacity,
            sizeof(LayoutFormattingContext));
        if (!tree->contexts) {
            return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                    RoboOutOfMemory,
                                    "could not allocate the formatting-context arena");
        }
        tree->contextCapacity = RoboLayoutInitialContextCapacity;
        return RoboOk;
    }
    if (tree->contextCount < tree->contextCapacity) {
        return RoboOk;
    }
    wanted = tree->contextCapacity * 2u;
    if (wanted <= tree->contextCapacity) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboOutOfMemory,
                                "formatting-context capacity overflowed");
    }
    grown = (LayoutFormattingContext *)Realloc(
        tree->contexts, wanted * sizeof(LayoutFormattingContext));
    if (!grown) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboOutOfMemory,
                                "could not grow the formatting-context arena");
    }
    memset(grown + tree->contextCapacity, 0,
           (wanted - tree->contextCapacity) * sizeof(LayoutFormattingContext));
    tree->contexts = grown;
    tree->contextCapacity = wanted;
    return RoboOk;
}

Result LayoutTreeContextAdd(LayoutTree *tree,
                                           LayoutFcKind kind,
                                           LayoutNodeId owner,
                                           LayoutFcId parent,
                                           uint32_t depth,
                                           LayoutFcId *outId,
                                           LayoutDiag *diag)
{
    LayoutFormattingContext *context;
    LayoutFcId id;

    if (outId) {
        *outId = RoboLayoutFcIdNone;
    }
    if (!tree || !outId) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "tree or out_id is NULL");
    }
    if (!LayoutFcKindIsKnown(kind) || kind == RoboLayoutFcNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "the kind is not a context kind");
    }
    if (owner == RoboLayoutNodeIdNone || !LayoutObjectSlot(tree, owner)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound, "the owner id names no box in this tree");
    }
    /* ROOT is the only context with no enclosing one. Every other kind is
     * established *inside* something, so a NONE parent would be a context floating
     * outside the tree it describes. */
    if (kind == RoboLayoutFcRoot) {
        if (parent != RoboLayoutFcIdNone) {
            return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                    RoboInvalidArgument,
                                    "the initial formatting context has no parent");
        }
    } else if (LayoutFcSlot(tree, parent) == NULL) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound,
                                "the parent context id names no context in this tree");
    }

    if (LayoutTreeContextReserve(tree, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }

    id = (LayoutFcId)(tree->contextCount + 1u);
    context = &tree->contexts[tree->contextCount];
    LayoutFormattingContextInit(context);
    context->id = id;
    context->kind = kind;
    context->owner = owner;
    context->parent = (kind == RoboLayoutFcRoot) ? RoboLayoutFcIdNone : parent;
    context->participants = 0u;
    context->depth = depth;
    tree->contextCount++;
    *outId = id;
    return RoboOk;
}

void LayoutTreeContextAddParticipant(LayoutTree *tree,
                                              LayoutFcId id)
{
    LayoutFormattingContext *context = LayoutFcSlot(tree, id);

    if (context) {
        context->participants++;
    }
}

size_t LayoutTreeContextCount(const LayoutTree *tree)
{
    return tree ? tree->contextCount : 0u;
}

int LayoutTreeIsClassified(const LayoutTree *tree)
{
    return tree && tree->classified;
}

Result LayoutTreeContextAt(const LayoutTree *tree,
                                         size_t index,
                                         LayoutFormattingContext *out,
                                         LayoutDiag *diag)
{
    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "out is NULL");
    }
    LayoutFormattingContextInit(out);
    if (!tree) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "tree is NULL");
    }
    if (index >= tree->contextCount) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound, "the index is past the context count");
    }
    *out = tree->contexts[index];
    return RoboOk;
}

Result LayoutTreeContext(const LayoutTree *tree,
                                      LayoutFcId id,
                                      LayoutFormattingContext *out,
                                      LayoutDiag *diag)
{
    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "out is NULL");
    }
    LayoutFormattingContextInit(out);
    if (!tree || id == RoboLayoutFcIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "tree is NULL or the id is NONE");
    }
    {
        const LayoutFormattingContext *context = LayoutFcSlotConst(tree, id);

        if (!context) {
            return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                    RoboNotFound,
                                    "the id names no context in this tree");
        }
        *out = *context;
    }
    return RoboOk;
}

Result LayoutTreeRootContext(const LayoutTree *tree,
                                           LayoutFcId *out,
                                           LayoutDiag *diag)
{
    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "out is NULL");
    }
    *out = RoboLayoutFcIdNone;
    if (!tree) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboInvalidArgument, "tree is NULL");
    }
    if (!LayoutTreeIsClassified(tree)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                RoboNotFound, "the tree has not been classified");
    }
    /* The initial context is the first one the pass establishes -- the root box is
     * created first, so it is established first. Stated as a search rather than as
     * "slot 0" because the ordering is a property of the pass and this door should
     * keep answering correctly if a later goal establishes something before it. */
    {
        size_t index;

        for (index = 0u; index < tree->contextCount; index++) {
            if (tree->contexts[index].kind == RoboLayoutFcRoot) {
                *out = tree->contexts[index].id;
                return RoboOk;
            }
        }
    }
    return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext, RoboNotFound,
                            "the tree has no root box, so it has no initial context");
}

/* ------------------------------------------------------------------ */
/* The line-box arena (Phase 10.6)                                     */
/* ------------------------------------------------------------------ */

/*
 * Two arenas, one writer, four doors -- and the same three rules the context
 * arena above follows: released rather than zeroed, ids are `count + 1`, and
 * nothing public can create one.
 *
 * The one thing that is different is the *member* chain, and the internal header
 * argues why it has to be a chain: the placements of one line are interleaved with
 * another's, because laying out an atomic inline recurses into the block pass for
 * its contents before the inline itself is placed. So a member record names the
 * next member of its own line and the line names the head, which makes a line's
 * membership exact rather than merely usually-contiguous -- at the price of O(n) to
 * read the n-th member, which the public door says out loud.
 */

void LayoutTreeLinesReset(LayoutTree *tree)
{
    size_t index;

    if (!tree) {
        return;
    }
    /* Released rather than zeroed, like the contexts: the next pass rebuilds lines
     * wholesale, so keeping a grown arena would hold memory for a shape nobody has
     * asked for. */
    RoboFree(tree->lines);
    tree->lines = NULL;
    tree->lineCount = 0u;
    tree->lineCapacity = 0u;
    RoboFree(tree->members);
    tree->members = NULL;
    tree->memberCount = 0u;
    tree->memberCapacity = 0u;

    /*
     * And every object's `line` id goes with them. This is the one place this module
     * *does* sweep the boxes rather than validating on read, and the asymmetry with
     * the context field is deliberate: ids restart at 1 with every pass, so a stale
     * line id does not merely name nothing -- it names *a different line*, and a
     * reader would get a plausible rectangle for the wrong text. The context field
     * has no such failure mode (a released classification leaves nothing to read),
     * and it is also swept here, in the same loop, because the two facts are released
     * together and one loop is cheaper than two.
     */
    for (index = 0u; index < tree->count; index++) {
        tree->objects[index].line = RoboLayoutLineIdNone;
        /*
         * And the baseline with it (10.7), for the same reason and in the same sweep:
         * a baseline is what a participant was *placed* with, so it is a conclusion of
         * the pass that produced the lines and not a property of the object. Leaving it
         * behind would be worse than leaving a line id behind -- a stale line id names a
         * *different* line, which a caller can at least notice, while a stale baseline
         * is a plausible-looking ascent for a document that is no longer laid out.
         */
        LayoutBaselineMetricsInit(&tree->objects[index].baseline);
        /*
         * And the used margins too (10.9), in the same sweep and for the same reason:
         * a used margin is the number a pass *concluded* from a containing block, so a
         * record that outlived the classification would be a plausible-looking gap
         * about a layout that no longer holds -- the exact failure mode a stale
         * baseline has, one axis further out.
         *
         * Cleared to no margins beside no containing block, which is the record's own
         * cleared state rather than a claim: `LayoutObjectMargins` refuses an
         * object that was never placed, so "no margins were used" and "no pass has
         * said anything" stay distinguishable through the result's state.
         */
        LayoutMarginEdgesInit(&tree->objects[index].margins, 0);
        /*
         * And the used padding (10.10), in the same sweep and for the same reason, with
         * one difference worth stating: a margin is a conclusion *about a containing
         * block*, while a padding is a conclusion about the box's own style -- so it is
         * the steadiest of the three records to leave behind and still the wrong one to
         * leave. Its four numbers come from declarations that do not change between
         * passes, but "this box has padding" *is* a statement of a pass, and
         * `LayoutObjectPadding` refuses an object that was never placed precisely
         * so that the record's silence cannot be read as an answer.
         */
        LayoutPaddingEdgesInit(&tree->objects[index].padding);
        /*
         * And the used border (10.11), the last of the four insets and the outermost of
         * the two that belong to the box. It is steadier than the padding even -- the
         * used widths come from declarations and a style that do not change between
         * passes -- and it is cleared for the reason every record above is: it carries
         * the *conclusion* that a side draws, and a conclusion that outlived the box set
         * it was drawn from would be a border a paint pass could believe. The door
         * refuses an object that was never placed so that the record's silence cannot be
         * read as an answer.
         */
        LayoutBorderEdgesInit(&tree->objects[index].borders);
        /*
         * And the position offset (10.12), the one record here that is a *displacement*
         * rather than an extent or an inset. It is cleared in the same sweep and for the
         * same reason -- a displacement that outlived the pass that concluded it would be
         * a box drawn somewhere the current layout never put it -- and it needs the
         * clearing more than the three above it, because its zero is the *common* value:
         * every `static` box resolves to one, so "no offset" and "no pass" are the same
         * record and only the result's state tells them apart.
         */
        LayoutPositionOffsetInit(&tree->objects[index].positionOffset);
        /*
         * And the overflow (10.13), the last of the five and the one that is *not* about
         * the box at all: four extents saying how far the contents reached past it, plus
         * the two modes that decided whether anything was kept. Cleared here for the same
         * reason as the four above -- an overflow that outlived the pass that measured it
         * would be a claim about a layout that no longer holds -- and its cleared state is
         * a *real* answer rather than a placeholder: two `visible` axes and four zeroes is
         * a box with nothing sticking out, which is most boxes in most documents. The
         * result's state is what tells "nothing sticks out" from "nothing measured",
         * exactly as it does for the three insets.
         */
        LayoutOverflowEdgesInit(&tree->objects[index].overflow);
    }
}

/** Grow the line arena if it is full. 0 on success, RoboOutOfMemory otherwise. */
static Result LayoutTreeLineReserve(LayoutTree *tree,
                                              LayoutDiag *diag)
{
    size_t wanted;
    LayoutLineSlot *grown;

    if (tree->lines == NULL) {
        tree->lines = (LayoutLineSlot *)RoboCalloc(RoboLayoutInitialLineCapacity,
                                                       sizeof(LayoutLineSlot));
        if (!tree->lines) {
            return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboOutOfMemory,
                                    "could not allocate the line-box arena");
        }
        tree->lineCapacity = RoboLayoutInitialLineCapacity;
        return RoboOk;
    }
    if (tree->lineCount < tree->lineCapacity) {
        return RoboOk;
    }
    wanted = tree->lineCapacity * 2u;
    if (wanted <= tree->lineCapacity) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboOutOfMemory,
                                "line-box capacity overflowed");
    }
    grown = (LayoutLineSlot *)Realloc(tree->lines,
                                               wanted * sizeof(LayoutLineSlot));
    if (!grown) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboOutOfMemory,
                                "could not grow the line-box arena");
    }
    memset(grown + tree->lineCapacity, 0,
           (wanted - tree->lineCapacity) * sizeof(LayoutLineSlot));
    tree->lines = grown;
    tree->lineCapacity = wanted;
    return RoboOk;
}

/** Grow the member arena if it is full. 0 on success, RoboOutOfMemory otherwise. */
static Result LayoutTreeMemberReserve(LayoutTree *tree,
                                                LayoutDiag *diag)
{
    size_t wanted;
    LayoutLineMember *grown;

    if (tree->members == NULL) {
        tree->members = (LayoutLineMember *)RoboCalloc(
            RoboLayoutInitialLineMemberCapacity, sizeof(LayoutLineMember));
        if (!tree->members) {
            return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboOutOfMemory,
                                    "could not allocate the line-member arena");
        }
        tree->memberCapacity = RoboLayoutInitialLineMemberCapacity;
        return RoboOk;
    }
    if (tree->memberCount < tree->memberCapacity) {
        return RoboOk;
    }
    wanted = tree->memberCapacity * 2u;
    if (wanted <= tree->memberCapacity) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboOutOfMemory,
                                "line-member capacity overflowed");
    }
    grown = (LayoutLineMember *)Realloc(
        tree->members, wanted * sizeof(LayoutLineMember));
    if (!grown) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboOutOfMemory,
                                "could not grow the line-member arena");
    }
    memset(grown + tree->memberCapacity, 0,
           (wanted - tree->memberCapacity) * sizeof(LayoutLineMember));
    tree->members = grown;
    tree->memberCapacity = wanted;
    return RoboOk;
}

Result LayoutLineMembersAppend(LayoutTree *tree,
                                             LayoutNodeId node,
                                             LayoutLineMemberId *head,
                                             LayoutLineMemberId *tail,
                                             size_t *count,
                                             LayoutDiag *diag)
{
    LayoutLineMember *member;
    LayoutLineMemberId id;

    if (!tree || !head || !tail || !count) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "a member append needs a tree and the chain's three fields");
    }
    if (node == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "the object id is NONE");
    }
    if (LayoutTreeMemberReserve(tree, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }

    /*
     * Allocated, linked and counted in one call, in that order, so an append either
     * happened or did not: a record that was allocated and not linked would be a member
     * no line names, and a link to an id that was never allocated would be a chain that
     * ends in nothing. The reserve above is what makes each of the three infallible
     * once reached, which is the only reason this can promise it.
     */
    id = (LayoutLineMemberId)(tree->memberCount + 1u);
    member = &tree->members[tree->memberCount];
    member->node = node;
    member->next = LAYOUT_LINE_MEMBER_ID_NONE;
    tree->memberCount++;

    if (*tail != LAYOUT_LINE_MEMBER_ID_NONE) {
        LayoutLineMember *previous = &tree->members[*tail - 1u];

        previous->next = id;
    } else {
        *head = id;
    }
    *tail = id;
    *count += 1u;
    return RoboOk;
}

Result LayoutLineAdd(LayoutTree *tree,
                                   LayoutFcId context,
                                   LayoutNodeId owner,
                                   size_t index,
                                   LayoutPoint origin,
                                   LayoutSize size,
                                   LayoutBaselineMetrics baseline,
                                   int32_t availableWidth,
                                   LayoutLineMemberId firstMember,
                                   size_t members,
                                   LayoutLineId *outId,
                                   LayoutDiag *diag)
{
    LayoutLineSlot *slot;
    LayoutLineId id;

    if (outId) {
        *outId = RoboLayoutLineIdNone;
    }
    if (!tree || !outId) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "tree or out_id is NULL");
    }
    /*
     * An owner that names no object is refused rather than stored, for the reason the
     * context arena refuses one: a line nothing can attribute is a line a reader can
     * only misread. A NONE *context* is refused for the same reason; a context id that
     * names none is refused too, because the pass reads it off the box that
     * established the context rather than inventing one, so a miss here is a bug in
     * the pass and not a document shape.
     */
    if (owner == RoboLayoutNodeIdNone || !LayoutObjectSlot(tree, owner)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the line's owner id names no object in this tree");
    }
    if (context == RoboLayoutFcIdNone || LayoutFcSlot(tree, context) == NULL) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the line names no live formatting context");
    }
    /* A line is finalized, so an open one cannot be stored: the construction record
     * is the pass's and never reaches here (RoboLayoutLineBox.h). What the pass passes
     * is the *state it is storing*, and the only state the arena can hold is FINAL. */
    if (members == 0u || firstMember == LAYOUT_LINE_MEMBER_ID_NONE) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "a finalized line has at least one member");
    }
    /*
     * Every stored line has a usable baseline (RoboLayoutLineBox.h), and this is the
     * one place that can be enforced rather than asserted. It is worth enforcing here
     * because the alternative failure is silent in a way the module's other refusals
     * are not: a line stored with a NONE baseline is a perfectly-shaped record whose
     * floor is *absent*, and a renderer that read it would place text at the line's
     * top edge instead of on the baseline -- a wrong picture, not a crash, and one
     * nobody would attribute to a missing enumerator.
     */
    if (!LayoutBaselineMetricsIsValid(&baseline)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "a finalized line needs a usable baseline");
    }

    if (LayoutTreeLineReserve(tree, diag) != RoboOk) {
        return diag ? diag->code : RoboOutOfMemory;
    }

    id = (LayoutLineId)(tree->lineCount + 1u);
    slot = &tree->lines[tree->lineCount];
    LayoutLineBoxInit(&slot->box);
    slot->box.id = id;
    slot->box.state = RoboLayoutLineFinal;
    slot->box.context = context;
    slot->box.owner = owner;
    slot->box.index = index;
    slot->box.origin = origin;
    slot->box.size = size;
    slot->box.baseline = baseline;
    slot->box.availableWidth = availableWidth;
    slot->box.members = members;
    slot->firstMember = firstMember;
    tree->lineCount++;
    *outId = id;
    return RoboOk;
}

const LayoutLineSlot *LayoutLineSlotConst(const LayoutTree *tree,
                                                      LayoutLineId id)
{
    if (!tree || id == RoboLayoutLineIdNone || id > tree->lineCount) {
        return NULL;
    }
    return &tree->lines[id - 1u];
}

const LayoutLineMember *LayoutLineMemberSlotConst(
    const LayoutTree *tree,
    LayoutLineMemberId id)
{
    if (!tree || id == LAYOUT_LINE_MEMBER_ID_NONE || id > tree->memberCount) {
        return NULL;
    }
    return &tree->members[id - 1u];
}

/* ------------------------------------------------------------------ */
/* The read side                                                       */
/* ------------------------------------------------------------------ */

size_t LayoutTreeLineCount(const LayoutTree *tree)
{
    return tree ? tree->lineCount : 0u;
}

Result LayoutTreeLineAt(const LayoutTree *tree,
                                       size_t index,
                                       LayoutLineId *outId,
                                       LayoutDiag *diag)
{
    if (!outId) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "out_id is NULL");
    }
    *outId = RoboLayoutLineIdNone;
    if (!tree) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "tree is NULL");
    }
    if (index >= tree->lineCount) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the index is past the line count");
    }
    /* Ids are dense and ordered, so the n-th line's id *is* n + 1 -- which is what
     * makes walking the lines a loop rather than a lookup (RoboLayoutLineBox.h). */
    *outId = (LayoutLineId)(index + 1u);
    return RoboOk;
}

Result LayoutTreeLine(const LayoutTree *tree,
                                    LayoutLineId id,
                                    LayoutLineBox *out,
                                    LayoutDiag *diag)
{
    const LayoutLineSlot *slot;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "out is NULL");
    }
    LayoutLineBoxInit(out);
    if (!tree || id == RoboLayoutLineIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout,
                                RoboInvalidArgument, "tree is NULL or the id is NONE");
    }
    slot = LayoutLineSlotConst(tree, id);
    if (!slot) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the id names no line in this tree");
    }
    *out = slot->box;
    return RoboOk;
}

Result GetLayoutLineMember(const LayoutTree *tree,
                                      LayoutLineId id,
                                      size_t index,
                                      LayoutNodeId *outNode,
                                      LayoutDiag *diag)
{
    const LayoutLineSlot *slot;
    const LayoutLineMember *member;
    size_t walked = 0u;

    if (!outNode) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboInvalidArgument,
                                "out_node is NULL");
    }
    *outNode = RoboLayoutNodeIdNone;
    if (!tree || id == RoboLayoutLineIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout,
                                RoboInvalidArgument, "tree is NULL or the id is NONE");
    }
    slot = LayoutLineSlotConst(tree, id);
    if (!slot) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the id names no line in this tree");
    }
    if (index >= slot->box.members) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboNotFound,
                                "the index is past the line's member count");
    }
    /*
     * The walk, and the reason the door is O(index) rather than O(1): a line's
     * members are a chain, because the placements of two lines interleave (the
     * internal header). The count in the record is what bounds it, so a corrupt link
     * cannot walk off the end -- the loop ends on the index, and the *count* was
     * validated above.
     */
    member = LayoutLineMemberSlotConst(tree, slot->firstMember);
    while (member && walked < index) {
        member = LayoutLineMemberSlotConst(tree, member->next);
        walked++;
    }
    if (!member) {
        return RoboLayoutDiag(diag, RoboLayoutDomainInlineLayout, RoboError,
                                "a line's member chain ended before its own count");
    }
    *outNode = member->node;
    return RoboOk;
}
