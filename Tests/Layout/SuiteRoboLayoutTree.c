/*
 * RoboLayout tree suite (Phase 10.1) — layout.tree.*
 *
 * The container and the one box it holds. Everything asserted here is a rule a
 * later goal would otherwise re-check while walking, so each test is written as
 * the question that rule answers rather than as a field comparison:
 *
 *   can a second root exist?              no, and the call says ALREADY_EXISTS
 *   can a `display: none` box exist?      no, and the call says so rather than
 *                                         silently filtering it later
 *   is a box's id dense?                  id == index + 1 until the tree clears
 *   does a miss look like a hit?          no: NOT_FOUND, and the output record is
 *                                         cleared first so a caller cannot read a
 *                                         half-filled copy
 *
 * The structural fixtures are built *by hand*, one add per box, rather than from
 * a document: this suite is about the container, and a builder that happened to
 * produce the right shape would make a container that could not hold one look
 * correct.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A hand-built fixture                                                */
/* ------------------------------------------------------------------ */

/*
 * The shape every structural test below asserts:
 *
 *   html                 id 1, depth 0, the root
 *   `- body              id 2, depth 1
 *      |- section        id 3, depth 2
 *      |  `- p           id 4, depth 3
 *      `- div            id 5, depth 2
 *
 * Four levels, so depth is asserted somewhere other than 0 and 1, and two
 * siblings under `body` with a *grandchild* under the first, so first/last/prev
 * and next are each distinguishable from the only-child case.
 */
typedef struct layoutTreeFixture {
    LayoutDocument doc;
    LayoutTree *tree;
    HtmlNodeHandle html;
    HtmlNodeHandle body;
    HtmlNodeHandle section;
    HtmlNodeHandle p;
    HtmlNodeHandle div;
} LayoutTreeFixture;

#define LAYOUT_TREE_FIXTURE_HTML \
    "<html>\n"                    \
    "  <body>\n"                  \
    "    <section>\n"             \
    "      <p>deep</p>\n"         \
    "    </section>\n"            \
    "    <div></div>\n"           \
    "  </body>\n"                 \
    "</html>\n"

static Result LayoutTreeAdd(LayoutTreeFixture *fixture,
                                     LayoutNodeId parent,
                                     const HtmlNodeHandle *element,
                                     const CssComputedStyle *style,
                                     LayoutNodeId *out)
{
    LayoutObjectInfo info;

    LayoutObjectInfoInit(&info);
    info.parent = parent;
    info.domNode = *element;
    info.style = style;
    info.display = RoboCssDisplayBlock;
    return LayoutTreeAddObject(fixture->tree, &info, out, NULL);
}

static void LayoutTreeFixtureBegin(LayoutTreeFixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, LAYOUT_TREE_FIXTURE_HTML);
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    fixture->html = LayoutDocumentFind(&fixture->doc, "html");
    fixture->body = LayoutDocumentFind(&fixture->doc, "body");
    fixture->section = LayoutDocumentFind(&fixture->doc, "section");
    fixture->p = LayoutDocumentFind(&fixture->doc, "p");
    fixture->div = LayoutDocumentFind(&fixture->doc, "div");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&fixture->html), 1);
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&fixture->div), 1);
}

static void LayoutTreeFixtureEnd(LayoutTreeFixture *fixture)
{
    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    LayoutDocumentRelease(&fixture->doc);
}

/** Add all five boxes and check every add succeeded. */
static void LayoutTreeFixtureBuild(LayoutTreeFixture *fixture,
                                      LayoutNodeId *outIds)
{
    LayoutTreeFixtureBegin(fixture);
    RoboCheckEqInt(LayoutTreeAdd(fixture, RoboLayoutNodeIdNone, &fixture->html,
                                      NULL, &outIds[0]),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeAdd(fixture, outIds[0], &fixture->body, NULL,
                                      &outIds[1]),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeAdd(fixture, outIds[1], &fixture->section, NULL,
                                      &outIds[2]),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeAdd(fixture, outIds[2], &fixture->p, NULL,
                                      &outIds[3]),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeAdd(fixture, outIds[1], &fixture->div, NULL,
                                      &outIds[4]),
                      RoboOk);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void TestTreeLifecycle(void)
{
    LayoutTree *tree = NULL;
    LayoutDocument doc;
    LayoutObjectInfo info;
    LayoutNodeId id = RoboLayoutNodeIdNone;

    LayoutFixture();

    RoboCheckEqInt(LayoutTreeCreate(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    RoboCheck(tree != NULL);

    RoboCheckEqInt(LayoutTreeObjectCount(tree), 0);
    RoboCheckEqInt(LayoutTreeIsEmpty(tree), 1);
    /* Capacity is the arena's, not the tree's contents: a fresh tree need not be
     * a zero-capacity one. What must hold is the invariant the header states. */
    RoboCheck(LayoutTreeCapacity(tree) >= LayoutTreeObjectCount(tree));
    RoboCheckEqInt((int)LayoutTreeRoot(tree), (int)RoboLayoutNodeIdNone);

    /* Every read door answers sanely before anything exists. */
    RoboCheck(GetLayoutObject(tree, (LayoutNodeId)1u) == NULL);
    RoboCheckEqInt((int)MakeLayoutObjectKind(tree, (LayoutNodeId)1u),
                      (int)RoboLayoutObjectNone);
    RoboCheck(LayoutObjectStyle(tree, (LayoutNodeId)1u) == NULL);
    RoboCheckEqInt(LayoutObjectIsLive(tree, (LayoutNodeId)1u), 0);
    RoboCheckEqInt(LayoutObjectIsRoot(tree, (LayoutNodeId)1u), 0);
    RoboCheckEqInt(LayoutObjectHasChildren(tree, (LayoutNodeId)1u), 0);
    RoboCheckEqInt(LayoutObjectFlags(tree, (LayoutNodeId)1u), 0);
    RoboCheck(LayoutObjectResultRef(tree, (LayoutNodeId)1u) == NULL);

    /* An empty tree behaves like one; a NULL tree behaves like an empty one too,
     * because "no tree" and "no boxes" are the same answer to every read. */
    RoboCheckEqInt(LayoutTreeObjectCount(NULL), 0);
    RoboCheckEqInt(LayoutTreeIsEmpty(NULL), 1);
    RoboCheckEqInt(LayoutObjectIsLive(NULL, (LayoutNodeId)1u), 0);
    LayoutTreeClear(NULL);
    LayoutTreeDestroy(NULL);

    /* An add on a NULL tree is refused without touching the output. */
    LayoutDocumentBuild(&doc, "<html><body></body></html>");
    LayoutObjectInfoInit(&info);
    info.domNode = LayoutDocumentElement(&doc);
    info.display = RoboCssDisplayBlock;
    id = (LayoutNodeId)9u;
    RoboCheckEqInt(LayoutTreeAddObject(NULL, &info, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)id, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutTreeAddObject(tree, NULL, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeAddObject(tree, &info, NULL, NULL),
                      RoboInvalidArgument);

    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* Adding the root                                                     */
/* ------------------------------------------------------------------ */

static void TestTreeAddRoot(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId root = RoboLayoutNodeIdNone;
    const LayoutObject *object;
    LayoutNodeId parent = (LayoutNodeId)4u;
    uint32_t depth = 99u;
    size_t children = 99u;

    LayoutFixture();
    LayoutTreeFixtureBegin(&fixture);
    RoboCheckEqInt(LayoutTreeAdd(&fixture, RoboLayoutNodeIdNone, &fixture.html,
                                      NULL, &root),
                      RoboOk);

    /* Ids are 1-based and dense: the root is 1, and it is the tree's root. */
    RoboCheckEqInt((int)root, 1);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeIsEmpty(fixture.tree), 0);
    RoboCheckEqInt((int)LayoutTreeRoot(fixture.tree), 1);
    RoboCheck(LayoutTreeCapacity(fixture.tree) >= 1u);

    object = GetLayoutObject(fixture.tree, root);
    RoboCheck(object != NULL);
    if (object) {
        RoboCheckEqInt((int)object->kind, (int)RoboLayoutObjectBox);
        RoboCheckEqInt((int)object->parent, (int)RoboLayoutNodeIdNone);
        RoboCheckEqInt(object->childCount, 0u);
        RoboCheckEqInt(object->depth, 0u);
        /* A NULL style is a fact, not an accident, and it is *stated*. */
        RoboCheckEqInt(object->flags, RoboLayoutObjectFlagStyleMissing);
        RoboCheck(object->style == NULL);
        /* No pass has spoken about this box yet, so its result is NONE rather
         * than UNRESOLVED: "the tree was built" and "a pass saw it" stay two
         * different facts. */
        RoboCheckEqInt((int)object->result.state, (int)RoboLayoutResultNone);
        /* The element is the one the DOM was asked about, by handle equality. */
        RoboCheckEqInt(HtmlNodeHandleEqual(&object->domNode, &fixture.html), 1);
    }

    RoboCheckEqInt(LayoutObjectIsRoot(fixture.tree, root), 1);
    RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, root), 1);
    RoboCheckEqInt(LayoutObjectHasChildren(fixture.tree, root), 0);
    RoboCheckEqInt((int)MakeLayoutObjectKind(fixture.tree, root),
                      (int)RoboLayoutObjectBox);
    RoboCheckEqInt(LayoutObjectDepth(fixture.tree, root, &depth), RoboOk);
    RoboCheckEqInt(depth, 0u);
    RoboCheckEqInt(LayoutObjectChildCount(fixture.tree, root, &children), RoboOk);
    RoboCheckEqInt(children, 0u);

    /* The root has no parent, and that is a *miss* rather than a NONE answer --
     * the caller distinguishes it with LayoutObjectIsRoot. */
    RoboCheckEqInt(LayoutObjectParent(fixture.tree, root, &parent), RoboNotFound);
    RoboCheckEqInt(LayoutObjectFirstChild(fixture.tree, root, &parent),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectLastChild(fixture.tree, root, &parent),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectNextSibling(fixture.tree, root, &parent),
                      RoboNotFound);

    LayoutTreeFixtureEnd(&fixture);
}

static void TestTreeAddSecondRootRefused(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId ids[2];
    LayoutNodeId id = (LayoutNodeId)7u;

    LayoutFixture();
    LayoutTreeFixtureBegin(&fixture);
    RoboCheckEqInt(LayoutTreeAdd(&fixture, RoboLayoutNodeIdNone, &fixture.html,
                                      NULL, &ids[0]),
                      RoboOk);
    /* A tree has one root. A second is refused rather than re-parented, because
     * "which one is the root" would otherwise depend on insertion order. */
    RoboCheckEqInt(LayoutTreeAdd(&fixture, RoboLayoutNodeIdNone, &fixture.body,
                                      NULL, &id),
                      RoboAlreadyExists);
    RoboCheckEqInt((int)id, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 1);

    /* And an id that names nothing is NOT_FOUND, distinct from the refused root. */
    id = (LayoutNodeId)7u;
    RoboCheckEqInt(LayoutTreeAdd(&fixture, (LayoutNodeId)42u, &fixture.body,
                                      NULL, &id),
                      RoboNotFound);
    RoboCheckEqInt((int)id, (int)RoboLayoutNodeIdNone);

    LayoutTreeFixtureEnd(&fixture);
}

static void TestTreeAddValidation(void)
{
    LayoutTreeFixture fixture;
    LayoutObjectInfo info;
    LayoutNodeId id = (LayoutNodeId)1u;
    HtmlNodeHandle unformed;

    LayoutFixture();
    LayoutTreeFixtureBegin(&fixture);

    /* An element handle that is not well formed: a box always came from
     * something the DOM can be asked about. */
    HtmlNodeHandleClear(&unformed);
    LayoutObjectInfoInit(&info);
    info.domNode = unformed;
    info.display = RoboCssDisplayBlock;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &id, NULL),
                      RoboInvalidArgument);

    /* `display: none` generates no box. Accepting one would make this tree a
     * copy of the DOM's element list, and every pass would re-filter what the
     * container had already failed to. */
    LayoutObjectInfoInit(&info);
    info.domNode = fixture.html;
    info.display = RoboCssDisplayNone;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 0);

    /* An undefined flag bit is refused rather than stored, so a typo cannot
     * travel through the pipeline looking like a laid-out fact. */
    info.display = RoboCssDisplayBlock;
    info.flags = 0x80000000u;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &id, NULL),
                      RoboInvalidArgument);

    /* The defined bit is accepted and travels. */
    info.flags = RoboLayoutObjectFlagStyleMissing;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &id, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutObjectFlags(fixture.tree, id),
                      RoboLayoutObjectFlagStyleMissing);
    RoboCheckEqInt(LayoutObjectHasFlag(fixture.tree, id,
                                                  RoboLayoutObjectFlagStyleMissing),
                      1);

    /* Every display that generates a box is accepted, and none of them arrive as
     * a false STYLE_MISSING. */
    {
        CssDisplay displays[] = { RoboCssDisplayBlock, RoboCssDisplayInline,
                                          RoboCssDisplayInlineBlock };
        size_t i;

        LayoutTreeClear(fixture.tree);
        for (i = 0u; i < sizeof(displays) / sizeof(displays[0]); i++) {
            LayoutObjectInfoInit(&info);
            info.parent = (i == 0u) ? RoboLayoutNodeIdNone : (LayoutNodeId)i;
            info.domNode = (i == 0u) ? fixture.html
                                      : ((i == 1u) ? fixture.body : fixture.section);
            info.display = displays[i];
            RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &id, NULL),
                              RoboOk);
            RoboCheckEqInt(LayoutObjectDisplay(fixture.tree, id),
                              (int)displays[i]);
        }
    }

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Relationships                                                       */
/* ------------------------------------------------------------------ */

static void TestTreeRelationships(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId ids[5];
    LayoutNodeId out = RoboLayoutNodeIdNone;
    size_t count = 0u;
    uint32_t depth = 0u;
    size_t i;

    LayoutFixture();
    LayoutTreeFixtureBuild(&fixture, ids);

    /* Dense and in creation order -- the property every walk in the module and
     * in the suites relies on. */
    for (i = 0u; i < 5u; i++) {
        LayoutNodeId at = RoboLayoutNodeIdNone;

        RoboCheckEqInt(ids[i], (LayoutNodeId)(i + 1u));
        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, i, &at), RoboOk);
        RoboCheckEqInt((int)at, (int)ids[i]);
    }
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 5u);

    /* html: one child. */
    RoboCheckEqInt(LayoutObjectFirstChild(fixture.tree, ids[0], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[1]);
    RoboCheckEqInt(LayoutObjectLastChild(fixture.tree, ids[0], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[1]);
    RoboCheckEqInt(LayoutObjectChildCount(fixture.tree, ids[0], &count), RoboOk);
    RoboCheckEqInt(count, 1u);
    RoboCheckEqInt(LayoutObjectHasChildren(fixture.tree, ids[0]), 1);

    /* body: two children, so first and last differ and prev/next are real. */
    RoboCheckEqInt(LayoutObjectFirstChild(fixture.tree, ids[1], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[2]);
    RoboCheckEqInt(LayoutObjectLastChild(fixture.tree, ids[1], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[4]);
    RoboCheckEqInt(LayoutObjectChildCount(fixture.tree, ids[1], &count), RoboOk);
    RoboCheckEqInt(count, 2u);

    RoboCheckEqInt(LayoutObjectNextSibling(fixture.tree, ids[2], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[4]);
    RoboCheckEqInt(LayoutObjectPreviousSibling(fixture.tree, ids[4], &out),
                      RoboOk);
    RoboCheckEqInt((int)out, (int)ids[2]);

    /* The first child has no previous sibling and the last has no next: a miss,
     * not a NONE answer. */
    RoboCheckEqInt(LayoutObjectPreviousSibling(fixture.tree, ids[2], &out),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectNextSibling(fixture.tree, ids[4], &out),
                      RoboNotFound);

    /* `p` is a box under `section`, *not* under `body`: the grandchild depth is
     * what separates "linked to the right parent" from "linked to the nearest
     * one". */
    RoboCheckEqInt(LayoutObjectParent(fixture.tree, ids[3], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[2]);
    RoboCheckEqInt(LayoutObjectDepth(fixture.tree, ids[3], &depth), RoboOk);
    RoboCheckEqInt(depth, 3u);

    /* Depth is 0 at the root and increments by exactly one per level. */
    for (i = 0u; i < 4u; i++) {
        RoboCheckEqInt(LayoutObjectDepth(fixture.tree, ids[i], &depth), RoboOk);
        RoboCheckEqInt(depth, (uint32_t)i);
    }
    RoboCheckEqInt(LayoutObjectIsRoot(fixture.tree, ids[0]), 1);
    RoboCheckEqInt(LayoutObjectIsRoot(fixture.tree, ids[1]), 0);

    /* A leaf has no children and is nobody's parent. */
    RoboCheckEqInt(LayoutObjectChildCount(fixture.tree, ids[3], &count), RoboOk);
    RoboCheckEqInt(count, 0u);
    RoboCheckEqInt(LayoutObjectHasChildren(fixture.tree, ids[3]), 0);
    RoboCheckEqInt(LayoutObjectFirstChild(fixture.tree, ids[3], &out),
                      RoboNotFound);

    /* And both children of `body` agree about the link, from either end. */
    RoboCheckEqInt(LayoutObjectParent(fixture.tree, ids[4], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[1]);
    RoboCheckEqInt(LayoutObjectParent(fixture.tree, ids[2], &out), RoboOk);
    RoboCheckEqInt((int)out, (int)ids[1]);

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Misses                                                              */
/* ------------------------------------------------------------------ */

static void TestTreeMisses(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId ids[5];
    LayoutNodeId id = (LayoutNodeId)99u;
    LayoutNodeId out = (LayoutNodeId)88u;
    LayoutObject object;
    LayoutResult result = RoboLayoutCleared;
    HtmlNodeHandle handle;
    LayoutDiag diag = RoboLayoutCleared;
    size_t count = 7u;
    uint32_t depth = 7u;

    LayoutFixture();
    LayoutTreeFixtureBuild(&fixture, ids);

    /* An id past the end names nothing, and the answer is a reported miss rather
     * than a read of whatever now occupies that slot. */
    memset(&object, 0xAB, sizeof(object));
    RoboCheckEqInt(LayoutObjectGet(fixture.tree, id, &object, &diag), RoboNotFound);
    RoboCheckEqInt(diag.code, RoboNotFound);
    /* Out is cleared first, so a failure never leaves a half-filled copy. */
    RoboCheckEqInt(object.id, 0u);
    RoboCheckEqInt(object.kind, RoboLayoutObjectNone);

    /* A NONE id is a caller bug rather than a miss. */
    RoboCheckEqInt(LayoutObjectGet(fixture.tree, RoboLayoutNodeIdNone,
                                             &object, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectGet(NULL, ids[0], &object, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectGet(fixture.tree, ids[0], NULL, NULL),
                      RoboInvalidArgument);

    /* Every navigation door joins the two regimes. */
    RoboCheckEqInt(LayoutObjectDomNode(fixture.tree, id, &handle), RoboNotFound);
    RoboCheckEqInt(LayoutObjectDomNode(fixture.tree, RoboLayoutNodeIdNone,
                                                  &handle),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectDomNode(NULL, ids[0], &handle),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectDomNode(fixture.tree, ids[0], NULL),
                      RoboInvalidArgument);

    /* Each navigation door takes its own output, so a miss on one cannot be
     * confused with a hit on the next: a door that cleared its caller's *id* into
     * no box would make every assert after it a test of the wrong thing. */
    RoboCheckEqInt(LayoutObjectParent(fixture.tree, id, &out), RoboNotFound);
    RoboCheckEqInt((int)out, (int)RoboLayoutNodeIdNone);
    /* A miss *clears* the output rather than leaving it, so a caller that ignored
     * the result code cannot read whatever its variable happened to hold as if it
     * were a fact. */
    RoboCheckEqInt(LayoutObjectChildCount(fixture.tree, id, &count), RoboNotFound);
    RoboCheckEqInt(count, 0u);
    RoboCheckEqInt(LayoutObjectDepth(fixture.tree, id, &depth), RoboNotFound);
    RoboCheckEqInt(depth, 0u);

    RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &result), RoboNotFound);
    RoboCheckEqInt(LayoutObjectResult(fixture.tree, ids[0], &result), RoboOk);

    /* The id_at door is the walk's own, so its two failures are the two a walk
     * can make. */
    RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, 5u, &id), RoboNotFound);
    RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, 0u, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectIdAt(NULL, 0u, &id), RoboInvalidArgument);

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Reverse lookup                                                      */
/* ------------------------------------------------------------------ */

static void TestTreeReverseLookup(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId ids[5];
    HtmlNodeHandle unformed;

    LayoutFixture();
    LayoutTreeFixtureBuild(&fixture, ids);

    /* Every box is reachable from its element, and the box that comes back is
     * the one built for it rather than the first box in the tree. */
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &fixture.html),
                      (int)ids[0]);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &fixture.body),
                      (int)ids[1]);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &fixture.section),
                      (int)ids[2]);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &fixture.p),
                      (int)ids[3]);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &fixture.div),
                      (int)ids[4]);

    /* An element no box was built for answers NONE -- which is also what a
     * `display: none` element answers, which is why the builder's report is the
     * door that tells the two apart (RoboLayoutBuilder.h). */
    HtmlNodeHandleClear(&unformed);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &unformed),
                      (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(NULL, &fixture.html),
                      (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, NULL),
                      (int)RoboLayoutNodeIdNone);

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Clearing and rebuilding                                             */
/* ------------------------------------------------------------------ */

static void TestTreeClear(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId ids[5];
    LayoutNodeId id = RoboLayoutNodeIdNone;

    LayoutFixture();
    LayoutTreeFixtureBuild(&fixture, ids);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 5u);

    LayoutTreeClear(fixture.tree);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 0u);
    RoboCheckEqInt(LayoutTreeIsEmpty(fixture.tree), 1);
    RoboCheckEqInt((int)LayoutTreeRoot(fixture.tree), (int)RoboLayoutNodeIdNone);
    /* The old ids name nothing now: a stale id must not alias whatever occupies
     * the slot next. */
    RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, ids[0]), 0);
    RoboCheck(GetLayoutObject(fixture.tree, ids[0]) == NULL);
    RoboCheckEqInt(MakeLayoutObjectKind(fixture.tree, ids[4]),
                      RoboLayoutObjectNone);

    /* Ids restart at 1, which is what makes a rebuild produce the same tree
     * rather than a tree that grows every time. */
    RoboCheckEqInt(LayoutTreeAdd(&fixture, RoboLayoutNodeIdNone, &fixture.html,
                                      NULL, &id),
                      RoboOk);
    RoboCheckEqInt((int)id, 1);
    RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, id), 1);

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Borrowed styles                                                     */
/* ------------------------------------------------------------------ */

static void TestTreeObjectStyle(void)
{
    LayoutTreeFixture fixture;
    LayoutFakeSource fake;
    LayoutNodeId withStyle = RoboLayoutNodeIdNone;
    LayoutNodeId withoutStyle = RoboLayoutNodeIdNone;
    LayoutObjectInfo info;
    LayoutObject object;

    LayoutFixture();
    LayoutTreeFixtureBegin(&fixture);
    LayoutFakeInit(&fake);
    LayoutFakeAdd(&fake, "html", RoboCssDisplayBlock);

    RoboCheckEqInt(fake.count, 1u);

    /* A box built *with* a style returns that exact pointer -- not a copy, which
     * is what makes "the tree borrows the styles" checkable rather than a claim
     * in a comment. */
    LayoutObjectInfoInit(&info);
    info.domNode = fixture.html;
    info.style = fake.rules[0].style;
    info.display = RoboCssDisplayBlock;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &withStyle, NULL),
                      RoboOk);
    RoboCheck(fake.rules[0].style == LayoutObjectStyle(fixture.tree, withStyle));
    RoboCheckEqInt(LayoutObjectHasFlag(fixture.tree, withStyle,
                                                  RoboLayoutObjectFlagStyleMissing),
                      0);
    RoboCheckEqInt(LayoutObjectFlags(fixture.tree, withStyle), 0u);
    /* And the element it is for comes back by handle equality. */
    {
        HtmlNodeHandle handle;

        RoboCheckEqInt(LayoutObjectDomNode(fixture.tree, withStyle, &handle),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeHandleEqual(&handle, &fixture.html), 1);
    }

    /* A box built with no style keeps a NULL style and the flag, because the
     * absence is a property of the build rather than an accident. */
    LayoutObjectInfoInit(&info);
    info.parent = withStyle;
    info.domNode = fixture.body;
    info.display = RoboCssDisplayBlock;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &withoutStyle, NULL),
                      RoboOk);
    RoboCheck(LayoutObjectStyle(fixture.tree, withoutStyle) == NULL);
    RoboCheckEqInt(LayoutObjectHasFlag(fixture.tree, withoutStyle,
                                                  RoboLayoutObjectFlagStyleMissing),
                      1);
    /* The read side agrees with the doors: a box nothing styled carries a NULL
     * style and *states* it with the flag rather than leaving a formatting context
     * to infer it from the pointer. */
    RoboCheckEqInt(LayoutObjectGet(fixture.tree, withoutStyle, &object, NULL),
                      RoboOk);
    RoboCheck(object.style == NULL);
    RoboCheckEqInt(object.flags, RoboLayoutObjectFlagStyleMissing);
    RoboCheckEqInt(object.childCount, 0u);
    /* And the parent's side of the link, so the box is where the add put it. */
    RoboCheckEqInt((int)object.parent, (int)withStyle);
    RoboCheckEqInt(LayoutObjectGet(fixture.tree, withStyle, &object, NULL),
                      RoboOk);
    RoboCheck(object.style != NULL);
    RoboCheckEqInt(object.flags, 0u);
    RoboCheckEqInt(object.childCount, 1u);
    RoboCheckEqInt((int)object.firstChild, (int)withoutStyle);

    LayoutFakeDispose(&fake);
    LayoutTreeFixtureEnd(&fixture);
}

static void TestTreeObjectDisplay(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId id = RoboLayoutNodeIdNone;
    LayoutObjectInfo info;

    LayoutFixture();
    LayoutTreeFixtureBegin(&fixture);

    /* The display is *copied*, and it is the one CSS value that is: the box
     * would not exist without it, and the first question a formatting context
     * asks is what kind of box it has. */
    LayoutObjectInfoInit(&info);
    info.domNode = fixture.html;
    info.display = RoboCssDisplayInlineBlock;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &id, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutObjectDisplay(fixture.tree, id),
                      (int)RoboCssDisplayInlineBlock);
    {
        const LayoutObject *object = GetLayoutObject(fixture.tree, id);

        RoboCheck(object != NULL);
        if (object) {
            RoboCheckEqInt((int)object->display, (int)RoboCssDisplayInlineBlock);
        }
    }
    /* A miss answers the display of no box. */
    RoboCheckEqInt((int)LayoutObjectDisplay(fixture.tree,
                                                      (LayoutNodeId)44u),
                      (int)RoboCssDisplayNone);

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Growth                                                              */
/* ------------------------------------------------------------------ */

static void TestTreeGrowth(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId root = RoboLayoutNodeIdNone;
    LayoutObjectInfo info;
    size_t initialCapacity;
    size_t i;
    const size_t wanted = 64u;

    LayoutFixture();
    LayoutTreeFixtureBegin(&fixture);

    LayoutObjectInfoInit(&info);
    info.domNode = fixture.html;
    info.display = RoboCssDisplayBlock;
    RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &root, NULL), RoboOk);
    initialCapacity = LayoutTreeCapacity(fixture.tree);

    /*
     * A *chain* rather than a fan, so every add after the first must resolve a
     * parent that the previous add created, and the depth of the last box is the
     * number of adds. The ids are dense from 1, so the box at index `i + 1` has id
     * `i + 2` -- which makes the parent of the i-th add the id the *previous* add
     * returned. A tree that lost a link when the arena moved fails at the growth
     * boundary, not at the first add.
     */
    for (i = 0u; i < wanted; i++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        LayoutNodeId parentOfThis = (i == 0u) ? root
                                                          : (LayoutNodeId)(i + 1u);

        LayoutObjectInfoInit(&info);
        info.parent = parentOfThis;
        info.domNode = fixture.body;
        info.display = RoboCssDisplayBlock;
        RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &id, NULL),
                          RoboOk);
        RoboCheckEqInt((int)id, (int)(i + 2u));
    }

    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), wanted + 1u);
    RoboCheck(LayoutTreeCapacity(fixture.tree) > initialCapacity);
    RoboCheck(LayoutTreeCapacity(fixture.tree) >=
               LayoutTreeObjectCount(fixture.tree));

    /* Every link survived the growth: the arena moved, the ids did not. */
    {
        uint32_t depth = 0u;

        /* The last box's depth is the number of links above it, which is the
         * number of adds: a chain that was broken anywhere would be shorter. */
        RoboCheckEqInt(LayoutObjectDepth(fixture.tree,
                                                   (LayoutNodeId)(wanted + 1u),
                                                   &depth),
                          RoboOk);
        RoboCheckEqInt(depth, (uint32_t)wanted);
        RoboCheckEqInt(LayoutObjectIsRoot(fixture.tree, root), 1);
    }

    /* And the whole chain is walkable from the root, one child per level. */
    {
        LayoutNodeId walker = root;
        size_t steps = 0u;

        for (;;) {
            LayoutNodeId next = RoboLayoutNodeIdNone;
            uint32_t depth = 0u;

            RoboCheckEqInt(LayoutObjectDepth(fixture.tree, walker, &depth), RoboOk);
            RoboCheckEqInt(depth, (uint32_t)steps);
            if (LayoutObjectFirstChild(fixture.tree, walker, &next) != RoboOk) {
                break;
            }
            walker = next;
            steps++;
        }
        RoboCheckEqInt(steps, wanted);
    }

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The borrowed-pointer contract                                       */
/* ------------------------------------------------------------------ */

static void TestTreeBorrowedPointer(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId ids[5];
    const LayoutObject *borrowed;

    LayoutFixture();
    LayoutTreeFixtureBuild(&fixture, ids);

    /* The whole struct in one call, valid until the next add -- which is why the
     * supported way to hold on to a box is its id. This test asserts the contract
     * that a caller can rely on: *before* an add, the pointer is the object. */
    borrowed = GetLayoutObject(fixture.tree, ids[2]);
    RoboCheck(borrowed != NULL);
    if (borrowed) {
        RoboCheckEqInt((int)borrowed->id, (int)ids[2]);
        RoboCheckEqInt((int)borrowed->parent, (int)ids[1]);
        RoboCheckEqInt((int)borrowed->firstChild, (int)ids[3]);
        /* `<section>`'s only child is `<p>`, and it is also the last child. */
        RoboCheckEqInt((int)borrowed->lastChild, (int)ids[3]);
        RoboCheckEqInt((int)borrowed->NextSibling, (int)ids[4]);
        RoboCheckEqInt((int)borrowed->prevSibling, (int)RoboLayoutNodeIdNone);
        RoboCheckEqInt(borrowed->childCount, 1u);
        RoboCheckEqInt(borrowed->depth, 2u);
    }

    /* The read side is the same data by value, which is the door that survives a
     * growth. */
    {
        LayoutObject copy;

        RoboCheckEqInt(LayoutObjectGet(fixture.tree, ids[2], &copy, NULL), RoboOk);
        RoboCheckEqInt((int)copy.id, (int)ids[2]);
        RoboCheckEqInt(LayoutObjectGet(fixture.tree, ids[3], &copy, NULL), RoboOk);
        RoboCheckEqInt((int)copy.parent, (int)ids[2]);
        RoboCheckEqInt(copy.childCount, 0u);
        RoboCheckEqInt(copy.depth, 3u);
    }

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Results before a pass                                               */
/* ------------------------------------------------------------------ */

static void TestTreeResultBeforePass(void)
{
    LayoutTreeFixture fixture;
    LayoutNodeId ids[5];
    LayoutResult result;
    const LayoutResult *ref;
    size_t i;

    LayoutFixture();
    LayoutTreeFixtureBuild(&fixture, ids);

    for (i = 0u; i < 5u; i++) {
        /* A box that exists always has a result *record*; what varies is its
         * state. Building a tree is not a pass, so the state is NONE and the
         * geometry is zeroed rather than meaningful. */
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, ids[i], &result), RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultNone);
        RoboCheckEqInt(result.flags, 0u);
        RoboCheckEqInt(result.position.x, 0);
        RoboCheckEqInt(result.position.y, 0);
        RoboCheckEqInt(result.size.width, 0);
        RoboCheckEqInt(result.size.height, 0);
        RoboCheckEqInt(LayoutResultIsComputed(&result), 0);

        ref = LayoutObjectResultRef(fixture.tree, ids[i]);
        RoboCheck(ref != NULL);
        if (ref) {
            RoboCheckEqInt((int)ref->state, (int)RoboLayoutResultNone);
        }
        /* And reading geometry that nobody computed is a miss, not a zero. */
        {
            LayoutPoint position;
            LayoutSize size;
            LayoutRect rect;

            RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboNotFound);
            RoboCheckEqInt(LayoutResultSize(&result, &size), RoboNotFound);
            RoboCheckEqInt(LayoutResultRect(&result, &rect, NULL), RoboNotFound);
        }
    }

    LayoutTreeFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void TestTreeAllocatorBalance(void)
{
    size_t before;
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    HtmlNodeHandle html;
    LayoutObjectInfo info;
    int pass;

    LayoutFixture();
    before = LayoutLiveBlocks();
    LayoutDocumentBuild(&doc, LAYOUT_TREE_FIXTURE_HTML);
    html = LayoutDocumentElement(&doc);

    for (pass = 0; pass < 8; pass++) {
        size_t i;

        RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
        for (i = 0u; i < 32u; i++) {
            LayoutNodeId id = RoboLayoutNodeIdNone;

            LayoutObjectInfoInit(&info);
            info.parent = (i == 0u) ? RoboLayoutNodeIdNone : (LayoutNodeId)i;
            info.domNode = html;
            info.display = RoboCssDisplayBlock;
            RoboCheckEqInt(LayoutTreeAddObject(tree, &info, &id, NULL), RoboOk);
        }
        /* A failed add must not leak either: it is the path a suite hits least. */
        LayoutObjectInfoInit(&info);
        info.parent = (LayoutNodeId)999u;
        info.domNode = html;
        info.display = RoboCssDisplayBlock;
        {
            LayoutNodeId id = RoboLayoutNodeIdNone;

            RoboCheckEqInt(LayoutTreeAddObject(tree, &info, &id, NULL),
                              RoboNotFound);
        }
        LayoutTreeDestroy(tree);
        tree = NULL;
    }

    LayoutDocumentRelease(&doc);
    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutTree(void);

void TestRegisterLayoutTree(void)
{
    TestRegister("layout.tree.lifecycle", TestTreeLifecycle);
    TestRegister("layout.tree.add.root", TestTreeAddRoot);
    TestRegister("layout.tree.add.second_root", TestTreeAddSecondRootRefused);
    TestRegister("layout.tree.add.validation", TestTreeAddValidation);
    TestRegister("layout.tree.relationships", TestTreeRelationships);
    TestRegister("layout.tree.misses", TestTreeMisses);
    TestRegister("layout.tree.reverse_lookup", TestTreeReverseLookup);
    TestRegister("layout.tree.clear", TestTreeClear);
    TestRegister("layout.tree.object.style", TestTreeObjectStyle);
    TestRegister("layout.tree.object.display", TestTreeObjectDisplay);
    TestRegister("layout.tree.growth", TestTreeGrowth);
    TestRegister("layout.tree.borrowed_pointer", TestTreeBorrowedPointer);
    TestRegister("layout.tree.result.before_pass", TestTreeResultBeforePass);
    TestRegister("layout.tree.allocator_balance", TestTreeAllocatorBalance);
}
