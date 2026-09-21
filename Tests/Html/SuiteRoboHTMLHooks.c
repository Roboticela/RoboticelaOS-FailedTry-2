/*
 * RoboHTML Phase 8.1 boundaries suite:
 * node identity edges, mutation hooks, client/render hooks,
 * resource boundary, DOM capability stubs, element name policy.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboTest.h"

#include <string.h>

static void HtmlHooksFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void HtmlHooksFixtureEnd(void)
{
    HtmlShutdown();
}

/* --- node identity edges ------------------------------------------------ */

static void TestHtmlNodeIdentityEdges(void)
{
    HtmlNodeHandle h;

    HtmlNodeHandleClear(&h);
    RoboCheck(!HtmlNodeHandleIsWellformed(&h));
    RoboCheck(!HtmlNodeHandleIsStale(&h)); /* ill-formed: not stale */
    RoboCheckEqInt(HtmlNodeHandleResolve(&h, NULL),
                      RoboInvalidArgument);
    /* Clear is safe on NULL. */
    HtmlNodeHandleClear(NULL);

    /* A handle against a non-existent document is stale. */
    h.documentId = 0xDEADu;
    h.nodeId = 1u;
    h.generation = 1u;
    RoboCheck(HtmlNodeHandleIsWellformed(&h));
    HtmlHooksFixtureBegin();
    RoboCheck(HtmlNodeHandleIsStale(&h));
    RoboCheckEqInt(HtmlNodeHandleResolve(&h, NULL), RoboNotFound);

    /* Zero generation is ill-formed even with ids set. */
    h.documentId = 1u;
    h.nodeId = 1u;
    h.generation = 0u;
    RoboCheck(!HtmlNodeHandleIsWellformed(&h));
    HtmlHooksFixtureEnd();
}

/* --- mutation hooks ------------------------------------------------------ */

typedef struct mutRecord {
    int nodeRegistered;
    int lifecycle;
    HtmlDocumentLifecycle lastLife;
    HtmlDocumentId lastDoc;
} MutRecord;

static MutRecord gMut;

static void MutHook(const HtmlMutationEvent *event, void *userdata)
{
    MutRecord *r = (MutRecord *)userdata;
    if (!r || !event) {
        return;
    }
    if (event->kind == RoboHtmlMutationNodeRegistered) {
        r->nodeRegistered++;
    } else if (event->kind == RoboHtmlMutationDocumentLifecycle) {
        r->lifecycle++;
        r->lastLife = event->lifecycle;
    }
    r->lastDoc = event->documentId;
}

static void TestHtmlMutationHooks(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlNodeHandle h;

    HtmlHooksFixtureBegin();
    memset(&gMut, 0, sizeof(gMut));

    RoboCheckEqInt(HtmlDocumentSetMutationHook(
                          RoboHtmlDocumentIdNone, MutHook, &gMut, NULL),
                      RoboNotFound);

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlDocumentSetMutationHook(id, MutHook, &gMut, NULL),
        RoboOk);
    /* Over-install refused. */
    RoboCheckEqInt(
        HtmlDocumentSetMutationHook(id, MutHook, &gMut, NULL),
        RoboAlreadyExists);
    /* Clear then re-install. */
    RoboCheckEqInt(
        HtmlDocumentSetMutationHook(id, NULL, NULL, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlDocumentSetMutationHook(id, MutHook, &gMut, NULL),
        RoboOk);

    /* Registration after install fires the hook. */
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(id, RoboHtmlNodeKindElement,
                                         RoboHtmlNodeOriginDom, &h, NULL),
        RoboOk);
    RoboCheckEqInt(gMut.nodeRegistered, 1);
    RoboCheckEqInt(gMut.lastDoc, id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    HtmlHooksFixtureEnd();
}

/* --- client/render hooks -------------------------------------------------- */

typedef struct clientRecord {
    int lifecycleEvents;
    int invalidations;
    HtmlInvalidationReason lastReason;
    HtmlDocumentLifecycle lastFrom;
    HtmlDocumentLifecycle lastTo;
} ClientRecord;

static ClientRecord gClientRec;

static void ClientLifecycle(HtmlDocumentId documentId,
                             HtmlDocumentLifecycle from,
                             HtmlDocumentLifecycle to,
                             void *userdata)
{
    ClientRecord *r = (ClientRecord *)userdata;
    (void)documentId;
    if (r) {
        r->lifecycleEvents++;
        r->lastFrom = from;
        r->lastTo = to;
    }
}

static void ClientInvalidate(HtmlDocumentId documentId,
                              HtmlInvalidationReason reason,
                              void *userdata)
{
    ClientRecord *r = (ClientRecord *)userdata;
    (void)documentId;
    if (r) {
        r->invalidations++;
        r->lastReason = reason;
    }
}

static void TestHtmlClientHooks(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlClientHooks hooks;
    HtmlSource src;

    HtmlHooksFixtureBegin();
    memset(&gClientRec, 0, sizeof(gClientRec));

    RoboCheckEqInt(HtmlDocumentSetClient(
                          RoboHtmlDocumentIdNone, &hooks, NULL, NULL),
                      RoboNotFound);

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(!HtmlDocumentHasClient(id));

    memset(&hooks, 0, sizeof(hooks));
    hooks.OnDocumentLifecycle = ClientLifecycle;
    hooks.OnInvalidate = ClientInvalidate;
    RoboCheckEqInt(HtmlDocumentSetClient(id, &hooks, &gClientRec,
                                                    NULL),
                      RoboOk);
    RoboCheck(HtmlDocumentHasClient(id));
    /* Over-install refused. */
    RoboCheckEqInt(HtmlDocumentSetClient(id, &hooks, NULL, NULL),
                      RoboAlreadyExists);

    /* Lifecycle + invalidation fire on source attach. */
    RoboCheckEqInt(HtmlSourceMemory(&src, (const uint8_t *)"x", 1u,
                                              NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboOk);
    RoboCheck(gClientRec.lifecycleEvents >= 1);
    RoboCheckEqInt(gClientRec.lastFrom,
                      RoboHtmlDocumentLifecycleCreated);
    RoboCheckEqInt(gClientRec.lastTo,
                      RoboHtmlDocumentLifecycleLoading);
    RoboCheckEqInt(gClientRec.invalidations, 1);
    RoboCheckEqInt(gClientRec.lastReason, RoboHtmlInvalidationSource);

    /* Clearing hooks stops notifications. */
    RoboCheckEqInt(HtmlDocumentSetClient(id, NULL, NULL, NULL),
                      RoboOk);
    RoboCheck(!HtmlDocumentHasClient(id));
    {
        int before = gClientRec.lifecycleEvents;
        RoboCheckEqInt(HtmlDocumentDetachSource(id, NULL), RoboOk);
        RoboCheckEqInt(gClientRec.lifecycleEvents, before);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    HtmlHooksFixtureEnd();
}

/* --- resource boundary ---------------------------------------------------- */

static void TestHtmlResourceBoundary(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlLoadPhase phase;
    HtmlSource src;
    HtmlDocumentLifecycle life;

    HtmlHooksFixtureBegin();
    RoboCheckEqInt(HtmlResourceGetPhase(
                          RoboHtmlDocumentIdNone, &phase),
                      RoboNotFound);
    RoboCheck(HtmlLoadPhaseIsKnown(RoboHtmlLoadPhaseAcquiring));
    RoboCheck(!HtmlLoadPhaseIsKnown((HtmlLoadPhase)31));
    RoboCheckStrEq(HtmlLoadPhaseName(RoboHtmlLoadPhaseParsing),
                      "parsing");

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);

    /* Begin delegated acquisition; direct attach is refused while open. */
    RoboCheckEqInt(HtmlResourceBegin(id, NULL, NULL), RoboOk);
    RoboCheckEqInt(HtmlResourceGetPhase(id, &phase), RoboOk);
    RoboCheckEqInt(phase, RoboHtmlLoadPhaseAcquiring);
    RoboCheckEqInt(HtmlResourceBegin(id, NULL, NULL), RoboBusy);

    RoboCheckEqInt(HtmlSourceEmpty(&src, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboBusy);

    /* Cancel returns to CREATED, attach now allowed. */
    RoboCheckEqInt(HtmlResourceCancel(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlResourceGetPhase(id, &phase), RoboOk);
    RoboCheckEqInt(phase, RoboHtmlLoadPhaseNone);
    RoboCheckEqInt(HtmlResourceCancel(id, NULL), RoboOk); /* idempotent */

    /* Successful end also releases the slot. */
    RoboCheckEqInt(HtmlResourceBegin(id, NULL, NULL), RoboOk);
    RoboCheckEqInt(HtmlResourceEnd(id, 1, NULL), RoboOk);
    RoboCheckEqInt(HtmlResourceGetPhase(id, &phase), RoboOk);
    RoboCheckEqInt(phase, RoboHtmlLoadPhaseNone);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    /* An empty source settles immediately: nothing to parse, nothing out. */
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleComplete);

    /* Failed end marks the document FAILED (sticky). */
    {
        HtmlDocumentId id2 = RoboHtmlDocumentIdNone;
        RoboCheckEqInt(HtmlDocumentCreate(&id2, NULL), RoboOk);
        RoboCheckEqInt(HtmlResourceBegin(id2, NULL, NULL), RoboOk);
        RoboCheckEqInt(HtmlResourceEnd(id2, 0, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentGetLifecycle(id2, &life),
                          RoboOk);
        RoboCheckEqInt(life, RoboHtmlDocumentLifecycleFailed);
        /* Failed documents refuse source attachment but support destroy. */
        RoboCheckEqInt(HtmlDocumentAttachSource(id2, &src, NULL),
                          RoboError);
        RoboCheckEqInt(HtmlDocumentDestroy(id2, NULL), RoboOk);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    HtmlHooksFixtureEnd();
}

/* --- DOM capability stubs -------------------------------------------------- */

static void TestHtmlDomCapabilities(void)
{
    HtmlDomCapabilities caps;

    HtmlDomCapabilitiesInit(&caps);
    RoboCheckEqInt(caps.domNodes, 0);
    RoboCheckEqInt(caps.attributes, 0);
    RoboCheckEqInt(caps.mutation, 0);
    RoboCheckEqInt(caps.elementTags, 0);
    RoboCheckEqInt(caps.images, 0);
    RoboCheckEqInt(caps.events, 0);
    RoboCheckEqInt(caps.treeBuilding, 0);
    RoboCheckEqInt(HtmlDomQueryCapabilities(NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
    /*
     * Tree construction (8.3) and attribute storage (8.6) are available, and
     * since 8.9 so is DOM mutation — which is what this file's hook
     * foundation was built for — and since 8.11 so is element type identity.
     * Since 8.12 so is the image resource model: `images` means an `<img>`
     * holds real decoded pixels reachable through a resolver, not that the
     * runtime can reach a network. The node DOM and events are not. Each flag
     * reports the model its own phase landed, not a compatible DOM surface:
     * `attributes` is the attribute model rather than a NamedNodeMap,
     * `elementTags` is the tag vocabulary rather than a style or selector
     * engine, and `images` is a resource model rather than an `HTMLImageElement`.
     */
    RoboCheckEqInt(caps.domNodes, 0);
    RoboCheckEqInt(caps.attributes, 1);
    RoboCheckEqInt(caps.mutation, 1);
    RoboCheckEqInt(caps.elementTags, 1);
    RoboCheckEqInt(caps.images, 1);
    RoboCheckEqInt(caps.events, 0);
    RoboCheckEqInt(caps.treeBuilding, 1);

    RoboCheck(HtmlNodeOriginIsKnown(RoboHtmlNodeOriginParser));
    RoboCheck(!HtmlNodeOriginIsKnown((HtmlNodeOrigin)19));
    RoboCheckStrEq(HtmlNodeOriginName(RoboHtmlNodeOriginDom),
                      "dom");
    RoboCheck(HtmlMutationKindIsKnown(
        RoboHtmlMutationNodeRegistered));
    RoboCheck(!HtmlMutationKindIsKnown(
        (HtmlMutationKind)8));
}

/* --- element/attribute boundary ------------------------------------------- */

static void TestHtmlElementNamePolicy(void)
{
    char out[RoboHtmlNameMax];

    RoboCheckEqInt(HtmlElementNameValidate("div", NULL), RoboOk);
    RoboCheckEqInt(HtmlElementNameValidate("my-widget_2", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementNameValidate(NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementNameValidate("", NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementNameValidate("a b", NULL),
                      RoboParseError);
    RoboCheckEqInt(HtmlElementNameValidate("<x>", NULL),
                      RoboParseError);
    {
        char huge[RoboHtmlNameMax + 4u];
        memset(huge, 'a', sizeof(huge) - 1u);
        huge[sizeof(huge) - 1u] = '\0';
        RoboCheckEqInt(HtmlElementNameValidate(huge, NULL),
                          RoboNotSupported);
    }

    /* Normalization lowercases ASCII and copies. */
    RoboCheckEqInt(HtmlElementNameNormalize("DiV", out, NULL),
                      RoboOk);
    RoboCheckStrEq(out, "div");
    RoboCheckEqInt(HtmlElementNameNormalize("My-App", out, NULL),
                      RoboOk);
    RoboCheckStrEq(out, "my-app");
    RoboCheckEqInt(HtmlElementNameNormalize(NULL, out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementNameNormalize("div", NULL, NULL),
                      RoboInvalidArgument);

    HtmlElementBoundaryInit(NULL); /* safe no-op */
    {
        HtmlElementBoundary b;
        HtmlElementBoundaryInit(&b);
        RoboCheckEqInt(b.reserved, 0u);
    }
}

void TestRegisterHtmlHooks(void)
{
    TestRegister("html.node.identity", TestHtmlNodeIdentityEdges);
    TestRegister("html.mutation.hooks", TestHtmlMutationHooks);
    TestRegister("html.client.hooks", TestHtmlClientHooks);
    TestRegister("html.resource.boundary", TestHtmlResourceBoundary);
    TestRegister("html.dom.capabilities", TestHtmlDomCapabilities);
    TestRegister("html.element.name_policy",
                       TestHtmlElementNamePolicy);
}
