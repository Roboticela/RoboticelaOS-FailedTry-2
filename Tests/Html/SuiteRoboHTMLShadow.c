/*
 * RoboHTML Phase 17.8 Shadow DOM suite (html.shadow.*).
 *
 * The claim under test is a *boundary*, and a boundary is only real if some
 * query fails to cross it. So the decisive cases are the lookups that must
 * **not** find anything (`html.shadow.boundary_lookup` — an id inside a shadow
 * tree is invisible to `getElementById`, and a tag inside it is invisible to a
 * host-scoped `getElementsByTagName`) and `html.shadow.retarget`, where a node
 * inside the tree is reported from outside as the host.
 *
 * Light DOM preservation is the third leg: attaching a shadow root must not
 * move, hide or reparent the host's own children.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLShadow.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static HtmlDocumentId ShadowLiveDocument = RoboHtmlDocumentIdNone;

static void ShadowFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static HtmlDocumentId ShadowDoc(void)
{
    ShadowFixtureBegin();
    if (ShadowLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(ShadowLiveDocument, NULL), RoboOk);
        ShadowLiveDocument = RoboHtmlDocumentIdNone;
    }
    RoboCheckEqInt(HtmlDocumentCreate(&ShadowLiveDocument, NULL), RoboOk);
    RoboCheck(ShadowLiveDocument != RoboHtmlDocumentIdNone);
    return ShadowLiveDocument;
}

static void ShadowDocEnd(void)
{
    if (ShadowLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(ShadowLiveDocument, NULL), RoboOk);
        ShadowLiveDocument = RoboHtmlDocumentIdNone;
    }
}

static void ShadowBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle ShadowRootOfDoc(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct shadowTagQuery {
    HtmlTag tag;
} ShadowTagQuery;

static int ShadowPredicateTag(const HtmlNodeView *view, void *user)
{
    const ShadowTagQuery *q = (const ShadowTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

static int ShadowTryFind(const HtmlNodeHandle *start, HtmlTag tag,
                         HtmlNodeHandle *out)
{
    ShadowTagQuery q;
    Result r;

    q.tag = tag;
    HtmlNodeHandleClear(out);
    r = HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                          ShadowPredicateTag, &q, out, NULL);
    if (r == RoboNotFound) {
        return 0;
    }
    RoboCheckEqInt(r, RoboOk);
    return HtmlNodeHandleIsWellformed(out) ? 1 : 0;
}

static HtmlNodeHandle ShadowFind(const HtmlNodeHandle *start, HtmlTag tag)
{
    HtmlNodeHandle out;

    RoboCheck(ShadowTryFind(start, tag, &out));
    return out;
}

typedef struct shadowCountQuery {
    HtmlTag tag;
    size_t count;
} ShadowCountQuery;

static Result ShadowCountVisitor(const HtmlNodeView *view, size_t depth,
                                 void *user)
{
    ShadowCountQuery *q = (ShadowCountQuery *)user;

    (void)depth;
    if (HtmlElementIsTag(&view->handle, q->tag)) {
        q->count++;
    }
    return RoboOk;
}

static size_t ShadowCount(const HtmlNodeHandle *start, HtmlTag tag)
{
    ShadowCountQuery q;

    q.tag = tag;
    q.count = 0u;
    RoboCheckEqInt(HtmlNodeWalkDescendants(start, RoboHtmlNodeFilterElements,
                                           ShadowCountVisitor, &q, NULL),
                   RoboOk);
    return q.count;
}

static size_t ShadowChildCount(const HtmlNodeHandle *node)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(node, &count, NULL), RoboOk);
    return count;
}

static void ShadowExpectCleanTree(HtmlDocumentId id)
{
    HtmlTreeReport report;

    memset(&report, 0, sizeof(report));
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL), RoboOk);
    RoboCheckMsg(report.ok, "tree invariant audit reported corruption");
    RoboCheckEqInt(report.orphans, 0u);
    RoboCheckEqInt(report.badLinks, 0u);
}

/* ------------------------------------------------------------------ */
/* Vocabulary and capabilities                                         */
/* ------------------------------------------------------------------ */

static void TestShadowCapabilities(void)
{
    HtmlShadowCapabilities caps;

    memset(&caps, 0xff, sizeof(caps));
    RoboCheckEqInt(HtmlShadowGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.attachShadow, 1);
    RoboCheckEqInt(caps.shadowRootQuery, 1);
    RoboCheckEqInt(caps.hostQuery, 1);
    RoboCheckEqInt(caps.lightDomPreserved, 1);
    RoboCheckEqInt(caps.boundaryLookup, 1);
    RoboCheckEqInt(caps.scopedLookup, 1);
    RoboCheckEqInt(caps.eventRetargeting, 1);
    RoboCheckEqInt(caps.focusBoundary, 1);
    RoboCheckEqInt(caps.reachabilityQuery, 1);
    /* The stated non-goals. */
    RoboCheckEqInt(caps.closedMode, 0);
    RoboCheckEqInt(caps.slotting, 0);
    RoboCheckEqInt(caps.hostSelectors, 0);
    RoboCheckEqInt(caps.declarativeShadowDom, 0);
    RoboCheckEqInt(caps.secondRenderingTree, 0);
    RoboCheckEqInt(caps.hostCandidates, 0);
    RoboCheckStrEq(caps.version, RoboHtmlVersionString);

    RoboCheckEqInt(HtmlShadowGetCapabilities(NULL), RoboInvalidArgument);

    RoboCheckEqInt(HtmlShadowModeIsKnown(RoboHtmlShadowModeOpen), 1);
    /* 0 is deliberately not a mode: "no mode" is not a thing a root has. */
    RoboCheckEqInt(HtmlShadowModeIsKnown((HtmlShadowMode)0), 0);
    RoboCheckEqInt(HtmlShadowModeIsKnown((HtmlShadowMode)2), 0);
    RoboCheckStrEq(HtmlShadowModeName(RoboHtmlShadowModeOpen), "open");
    RoboCheckStrEq(HtmlShadowModeName((HtmlShadowMode)2), "unknown");
}

/* ------------------------------------------------------------------ */
/* attachShadow                                                        */
/* ------------------------------------------------------------------ */

static void TestShadowAttach(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlNodeHandle again;
    HtmlShadowMode mode = (HtmlShadowMode)0;
    int isHost = 0;
    int isRoot = 0;

    ShadowBuild(id, "<body><div id=\"host\"><p id=\"light\">Light</p></div>"
                    "<p>Other</p></body>");
    docRoot = ShadowRootOfDoc(id);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);

    RoboCheckEqInt(HtmlElementIsShadowHost(&host, &isHost, NULL), RoboOk);
    RoboCheckEqInt(isHost, 0);

    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&root));

    /* The root is a fragment and a root: no parent, so nothing in the
     * document tree can reach it. */
    {
        HtmlNodeKind kind = RoboHtmlNodeKindDocument;
        HtmlNodeHandle parent;

        RoboCheckEqInt(HtmlNodeHandleResolve(&root, &kind), RoboOk);
        RoboCheckEqInt(kind, RoboHtmlNodeKindFragment);
        RoboCheckEqInt(HtmlNodeParent(&root, &parent, NULL), RoboNotFound);
    }

    RoboCheckEqInt(HtmlShadowRootMode(&root, &mode, NULL), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlShadowModeOpen);

    RoboCheckEqInt(HtmlNodeIsShadowRoot(&root, &isRoot, NULL), RoboOk);
    RoboCheckEqInt(isRoot, 1);

    /* Both directions of the relation. */
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &again, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&again, &root));
    {
        HtmlNodeHandle back;

        RoboCheckEqInt(HtmlShadowRootHost(&root, &back, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&back, &host));
    }

    RoboCheckEqInt(HtmlElementIsShadowHost(&host, &isHost, NULL), RoboOk);
    RoboCheckEqInt(isHost, 1);

    /* One shadow root per host: a second attach is refused, not a silent
     * replacement that would orphan what the caller already holds. */
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &again, NULL),
                   RoboAlreadyExists);
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &again, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&again, &root));

    ShadowExpectCleanTree(id);
    ShadowDocEnd();
}

static void TestShadowClosedModeRefused(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle host;
    HtmlNodeHandle root;

    ShadowBuild(id, "<body><div id=\"host\"></div></body>");
    docRoot = ShadowRootOfDoc(id);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);

    /* A closed root that every API here could still read would be a lie, so
     * the mode is refused rather than accepted and ignored. */
    RoboCheckEqInt(HtmlElementAttachShadow(&host, (HtmlShadowMode)0, &root,
                                          NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, (HtmlShadowMode)2, &root,
                                          NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &root, NULL), RoboNotFound);

    /* Nothing was attached, so the second open attempt still succeeds. */
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);

    ShadowDocEnd();
}

/* ------------------------------------------------------------------ */
/* The boundary                                                        */
/* ------------------------------------------------------------------ */

static void TestShadowBoundaryLookup(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle body;
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlNodeHandle inner;
    HtmlNodeHandle light;
    size_t count = 0u;

    ShadowBuild(id, "<body>"
                    "<div id=\"host\"><p id=\"light\">Light</p></div>"
                    "<p id=\"outside\">Outside</p>"
                    "</body>");
    docRoot = ShadowRootOfDoc(id);
    body = ShadowFind(&docRoot, RoboHtmlTagBody);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);

    /* Build shadow content. */
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "span", &inner, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementSetAttribute(&inner, "id", "inside", NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &inner, NULL), RoboOk);

    /* The light DOM is untouched: the host still has exactly its own child,
     * and the shadow root is not one of them. */
    RoboCheckEqInt((int)ShadowChildCount(&host), 1);
    light = ShadowFind(&host, RoboHtmlTagP);
    {
        size_t length = 0u;
        const char *value = HtmlElementAttributeValue(&light, "id", &length);

        RoboCheck(value != NULL && length == 5u);
        RoboCheck(memcmp(value, "light", 5u) == 0);
    }

    /*
     * The boundary, in the three directions it can be crossed:
     *   1. document lookup does not see into the shadow tree
     *   2. a host-scoped query does not see into it either
     *   3. a shadow-scoped query sees only its own subtree
     */
    {
        HtmlNodeHandle found;

        RoboCheckEqInt(HtmlDocumentGetElementById(id, "inside", &found, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentGetElementById(id, "light", &found, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlDocumentGetElementById(id, "outside", &found,
                                                 NULL),
                       RoboOk);
    }
    RoboCheckEqInt((int)ShadowCount(&host, RoboHtmlTagSpan), 0);
    RoboCheckEqInt((int)ShadowCount(&body, RoboHtmlTagSpan), 0);
    RoboCheckEqInt((int)ShadowCount(&root, RoboHtmlTagSpan), 1);

    /* The host is not a descendant of its own shadow root, and the root is
     * not a descendant of the host. */
    {
        int contains = 0;

        RoboCheckEqInt(HtmlNodeContains(&host, &root, &contains, NULL),
                       RoboOk);
        RoboCheckEqInt(contains, 0);
        RoboCheckEqInt(HtmlNodeContains(&root, &host, &contains, NULL),
                       RoboOk);
        RoboCheckEqInt(contains, 0);
    }

    /* A host-scoped tag query counts the light DOM only. */
    RoboCheckEqInt(HtmlElementGetElementsByTagName(&host, "span", NULL, NULL,
                                                   &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 0);
    RoboCheckEqInt(HtmlElementGetElementsByTagName(&body, "span", NULL, NULL,
                                                   &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 0);
    RoboCheckEqInt(HtmlElementGetElementsByTagName(&root, "span", NULL, NULL,
                                                   &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 1);

    ShadowExpectCleanTree(id);
    ShadowDocEnd();
}

static void TestShadowReachability(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlNodeHandle inner;
    int reachable = 0;

    ShadowBuild(id, "<body><div id=\"host\"></div></body>");
    docRoot = ShadowRootOfDoc(id);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "span", &inner, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &inner, NULL), RoboOk);

    /* The host is in the light tree; its shadow root and the root's children
     * are not. */
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&host, &reachable, NULL),
                   RoboOk);
    RoboCheckEqInt(reachable, 1);
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&root, &reachable, NULL),
                   RoboOk);
    RoboCheckEqInt(reachable, 0);
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&inner, &reachable, NULL),
                   RoboOk);
    RoboCheckEqInt(reachable, 0);
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&docRoot, &reachable,
                                                  NULL),
                   RoboOk);
    RoboCheckEqInt(reachable, 1);

    /*
     * A node inserted into the light tree *while* the shadow tree exists
     * becomes reachable through the ordinary path — the boundary is the
     * shadow root's own lack of a parent, not a flag on the host.
     */
    {
        HtmlNodeHandle lightChild;

        RoboCheckEqInt(HtmlDocumentCreateElement(id, "em", &lightChild, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&host, &lightChild, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&lightChild,
                                                      &reachable, NULL),
                       RoboOk);
        RoboCheckEqInt(reachable, 1);
    }

    ShadowExpectCleanTree(id);
    ShadowDocEnd();
}

/* ------------------------------------------------------------------ */
/* Retargeting                                                         */
/* ------------------------------------------------------------------ */

static void TestShadowRetarget(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlNodeHandle inner;
    HtmlNodeHandle outside;
    HtmlNodeHandle retargeted;

    ShadowBuild(id, "<body><div id=\"host\"></div><p id=\"outside\"></p>"
                    "</body>");
    docRoot = ShadowRootOfDoc(id);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);
    outside = ShadowFind(&docRoot, RoboHtmlTagP);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "span", &inner, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &inner, NULL), RoboOk);

    /* Inside the tree: reported from outside as the host. */
    RoboCheckEqInt(HtmlShadowRootRetargetEvent(&root, &inner, &retargeted,
                                              NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&retargeted, &host));

    /* A target outside crosses nothing and is unchanged. */
    RoboCheckEqInt(HtmlShadowRootRetargetEvent(&root, &outside, &retargeted,
                                              NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&retargeted, &outside));

    /* The root itself is inside its own tree, so it too is seen as the host. */
    RoboCheckEqInt(HtmlShadowRootRetargetEvent(&root, &root, &retargeted,
                                              NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&retargeted, &host));

    /* Focus uses the same crossing. */
    RoboCheckEqInt(HtmlShadowRootRetargetFocus(&root, &inner, &retargeted,
                                              NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&retargeted, &host));

    ShadowExpectCleanTree(id);
    ShadowDocEnd();
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

static void TestShadowHostDetached(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle body;
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlNodeHandle back;
    int reachable = 0;

    ShadowBuild(id, "<body><div id=\"host\"></div></body>");
    docRoot = ShadowRootOfDoc(id);
    body = ShadowFind(&docRoot, RoboHtmlTagBody);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);

    /* Detaching the host does not lose the relation: the table is keyed by
     * node identity, so a detached host still answers. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &host, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &back, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&back, &root));
    RoboCheckEqInt(HtmlShadowRootHost(&root, &back, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&back, &host));
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&host, &reachable, NULL),
                   RoboOk);
    RoboCheckEqInt(reachable, 0);

    /* Re-attaching it to the tree restores reachability without touching the
     * shadow root. */
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &host, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &back, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&back, &root));
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&host, &reachable, NULL),
                   RoboOk);
    RoboCheckEqInt(reachable, 1);

    ShadowExpectCleanTree(id);
    ShadowDocEnd();
}

static void TestShadowCloneAndErrors(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlNodeHandle clone;
    HtmlNodeHandle inner;
    HtmlNodeHandle para;
    int isHost = 0;
    int isRoot = 0;

    ShadowBuild(id, "<body><div id=\"host\"><p>Light</p></div>"
                    "<p id=\"plain\">Plain</p></body>");
    docRoot = ShadowRootOfDoc(id);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);
    para = ShadowFind(&docRoot, RoboHtmlTagP);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "span", &inner, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &inner, NULL), RoboOk);

    /*
     * Cloning a host clones the light DOM only. A clone is a new element and
     * nothing in the model attaches a shadow root to it — which is the
     * observable half of "a shadow root is not a child".
     */
    RoboCheckEqInt(HtmlNodeClone(&host, 1, &clone, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&clone));
    RoboCheckEqInt((int)ShadowChildCount(&clone), 1);
    RoboCheckEqInt(HtmlElementIsShadowHost(&clone, &isHost, NULL), RoboOk);
    RoboCheckEqInt(isHost, 0);
    RoboCheckEqInt(HtmlElementShadowRoot(&clone, &root, NULL), RoboNotFound);

    /* A non-element cannot host. */
    {
        HtmlNodeHandle text;

        RoboCheckEqInt(HtmlDocumentCreateTextNode(id, "t", &text, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementAttachShadow(&text,
                                               RoboHtmlShadowModeOpen,
                                               &root, NULL),
                       RoboNotSupported);
        RoboCheckEqInt(HtmlNodeIsShadowRoot(&text, &isRoot, NULL), RoboOk);
        RoboCheckEqInt(isRoot, 0);
    }

    /* An element with no shadow root is a miss, not an empty root. */
    RoboCheckEqInt(HtmlElementShadowRoot(&para, &root, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlShadowRootHost(&para, &root, NULL), RoboNotFound);
    {
        HtmlShadowMode mode = (HtmlShadowMode)0;

        RoboCheckEqInt(HtmlShadowRootMode(&para, &mode, NULL), RoboNotFound);
    }
    RoboCheckEqInt(HtmlShadowRootRetargetEvent(&para, &host, &root, NULL),
                   RoboNotFound);

    /* Argument errors. */
    RoboCheckEqInt(HtmlElementAttachShadow(NULL, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementShadowRoot(&host, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlShadowRootHost(NULL, &root, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementIsShadowHost(&host, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeIsShadowRoot(&root, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&host, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlShadowRootRetargetEvent(&root, &host, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlShadowRootRetargetEvent(NULL, &host, &root, NULL),
                   RoboInvalidArgument);

    ShadowExpectCleanTree(id);
    ShadowDocEnd();
}

static void TestShadowStaleHandles(void)
{
    HtmlDocumentId id = ShadowDoc();
    HtmlNodeHandle docRoot;
    HtmlNodeHandle host;
    HtmlNodeHandle root;
    HtmlNodeHandle staleHost;
    HtmlNodeHandle staleRoot;
    int isHost = 1;
    int reachable = 1;

    ShadowBuild(id, "<body><div id=\"host\"></div></body>");
    docRoot = ShadowRootOfDoc(id);
    host = ShadowFind(&docRoot, RoboHtmlTagDiv);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &root, NULL),
                   RoboOk);
    staleHost = host;
    staleRoot = root;

    ShadowDocEnd();

    /* Every door refuses a handle whose document is gone, and none of them
     * follows the retired id. */
    RoboCheckEqInt(HtmlElementShadowRoot(&staleHost, &root, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlElementAttachShadow(&staleHost,
                                          RoboHtmlShadowModeOpen, &root,
                                          NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlShadowRootHost(&staleRoot, &host, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlElementIsShadowHost(&staleHost, &isHost, NULL),
                   RoboNotFound);
    RoboCheckEqInt(isHost, 0);
    RoboCheckEqInt(HtmlNodeIsShadowRoot(&staleRoot, &isHost, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&staleHost, &reachable,
                                                  NULL),
                   RoboNotFound);
    RoboCheckEqInt(reachable, 0);
    {
        HtmlNodeHandle retargeted;

        RoboCheckEqInt(HtmlShadowRootRetargetEvent(&staleRoot, &staleHost,
                                                  &retargeted, NULL),
                       RoboNotFound);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlShadow(void);

void TestRegisterHtmlShadow(void)
{
    TestRegister("html.shadow.capabilities", TestShadowCapabilities);
    TestRegister("html.shadow.attach", TestShadowAttach);
    TestRegister("html.shadow.closed_mode_refused",
                 TestShadowClosedModeRefused);
    TestRegister("html.shadow.boundary_lookup", TestShadowBoundaryLookup);
    TestRegister("html.shadow.reachability", TestShadowReachability);
    TestRegister("html.shadow.retarget", TestShadowRetarget);
    TestRegister("html.shadow.host_detached", TestShadowHostDetached);
    TestRegister("html.shadow.clone_and_errors", TestShadowCloneAndErrors);
    TestRegister("html.shadow.stale_handles", TestShadowStaleHandles);
}
