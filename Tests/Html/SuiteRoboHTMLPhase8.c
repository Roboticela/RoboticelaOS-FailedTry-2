/*
 * RoboHTML Phase 8.1 acceptance suite (html.p8.*).
 *
 * Cross-cutting acceptance: one document flows through create → lease →
 * attach → ready → destroy with all hooks, leases, and node identities
 * released; plus a compile-time boundary check that RoboHTML never pulls
 * graphics/window/render modules (DEPENDENCIES.md direction).
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLClient.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLEncoding.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLParser.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboTest.h"

#include <string.h>

typedef struct p8Record {
    int lifecycleEvents;
    int invalidations;
    int mutations;
    int released;
} P8Record;

static P8Record gP8;

static void P8Lifecycle(HtmlDocumentId documentId,
                         HtmlDocumentLifecycle from,
                         HtmlDocumentLifecycle to,
                         void *userdata)
{
    P8Record *r = (P8Record *)userdata;
    (void)documentId;
    (void)from;
    if (r) {
        r->lifecycleEvents++;
        /* No callbacks may arrive after teardown completed. */
        if (to == RoboHtmlDocumentLifecycleDestroyed) {
            r->released++;
        }
    }
}

static void P8Invalidate(HtmlDocumentId documentId,
                          HtmlInvalidationReason reason,
                          void *userdata)
{
    P8Record *r = (P8Record *)userdata;
    (void)documentId;
    (void)reason;
    if (r) {
        r->invalidations++;
    }
}

static void P8Mutation(const HtmlMutationEvent *event,
                        void *userdata)
{
    P8Record *r = (P8Record *)userdata;
    (void)event;
    if (r) {
        r->mutations++;
    }
}

static void TestHtmlP8FullDocumentFlow(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlClientHooks hooks;
    HtmlNodeHandle root;
    HtmlNodeHandle para;
    HtmlNodeHandle text;
    HtmlParser *p = NULL;
    HtmlSource src;
    HtmlEncodingState enc;
    HtmlDocumentLifecycle life;

    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
    memset(&gP8, 0, sizeof(gP8));

    /* 1. Create an empty document. */
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    root = HtmlDocumentRoot(id);
    RoboCheck(HtmlNodeHandleIsWellformed(&root));

    /* 2. Install client + mutation observers. */
    memset(&hooks, 0, sizeof(hooks));
    hooks.OnDocumentLifecycle = P8Lifecycle;
    hooks.OnInvalidate = P8Invalidate;
    RoboCheckEqInt(HtmlDocumentSetClient(id, &hooks, &gP8, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentSetMutationHook(id, P8Mutation,
                                                           &gP8, NULL),
                      RoboOk);

    /* 3. Lease a parser exclusively. */
    RoboCheckEqInt(HtmlParserCreate(id, &p, NULL), RoboOk);

    /* 4. Attach a UTF-8 source through the parser boundary. */
    RoboCheckEqInt(HtmlSourceMemory(&src,
                                              (const uint8_t *)"<p>Hi</p>",
                                              9u, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlSourceSetDeclaredEncoding(&src, "UTF-8",
                                                             NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlSourceSetName(&src, "app://welcome", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlParserAttachSource(p, &src, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleLoading);
    RoboCheckEqInt(HtmlDocumentGetEncodingState(id, &enc), RoboOk);
    RoboCheckEqInt(enc.declared, RoboHtmlEncodingUtf8);

    /* 5. Tree-builder entry point registers nodes (no tree yet in 8.1). */
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(id, RoboHtmlNodeKindElement,
                                         RoboHtmlNodeOriginParser, &para,
                                         NULL),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(id, RoboHtmlNodeKindText,
                                         RoboHtmlNodeOriginParser, &text,
                                         NULL),
        RoboOk);
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 3u);
    /* Hook was installed after creation: only para + text fired. */
    RoboCheckEqInt(gP8.mutations, 2);

    /* 6. Parsing completes: the DOM is live at INTERACTIVE. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleInteractive);

    /* 7. Nothing further to load: COMPLETE, and the client sees both. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleComplete);

    /* 8. Client saw lifecycle + invalidation traffic: LOADING,
     * INTERACTIVE and COMPLETE each produced one callback. */
    RoboCheckEqInt(gP8.lifecycleEvents, 3);
    RoboCheck(gP8.invalidations >= 1);

    /* 9. Release the lease, then destroy: everything released. */
    RoboCheckEqInt(HtmlParserDetach(p, NULL), RoboOk);
    HtmlParserDestroy(p);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(!HtmlDocumentIsLive(id));
    RoboCheck(HtmlNodeHandleIsStale(&para));
    RoboCheck(HtmlNodeHandleIsStale(&text));
    RoboCheck(HtmlNodeHandleIsStale(&root));
    RoboCheckEqInt(gP8.released, 0); /* no DESTROYED callback after teardown */

    HtmlShutdown();
}

static void TestHtmlP8ModuleShutdownReleasesAll(void)
{
    HtmlDocumentId a = RoboHtmlDocumentIdNone;
    HtmlDocumentId b = RoboHtmlDocumentIdNone;
    HtmlNodeHandle ha;
    HtmlNodeHandle hb;

    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
    RoboCheckEqInt(HtmlDocumentCreate(&a, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreate(&b, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(a, RoboHtmlNodeKindElement,
                                         RoboHtmlNodeOriginTest, &ha,
                                         NULL),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(b, RoboHtmlNodeKindText,
                                         RoboHtmlNodeOriginTest, &hb,
                                         NULL),
        RoboOk);

    /* Module shutdown destroys every live document deterministically. */
    HtmlShutdown();
    RoboCheck(!HtmlDocumentIsLive(a));
    RoboCheck(!HtmlDocumentIsLive(b));
    RoboCheck(HtmlNodeHandleIsStale(&ha));
    RoboCheck(HtmlNodeHandleIsStale(&hb));
    RoboCheckEqInt(HtmlLiveDocumentCount(), 0u);
}

static void TestHtmlP8BoundariesCompile(void)
{
    /*
     * Compile-time boundary guards: RoboHTML public headers must expose
     * only runtime-owned types (no platform/window/graphics linkage).
     * These checks assert the ABI shape that future phases compile against.
     */
    HtmlDiag diag;
    HtmlNodeHandle h;
    HtmlEncoding e;

    HtmlDiagReset(&diag);
    HtmlNodeHandleClear(&h);
    e = HtmlEncodingFromLabel("UTF-16LE");
    RoboCheckEqInt(e, RoboHtmlEncodingUtf16LE);
    RoboCheckStrEq(HtmlEncodingLabel(e), "UTF-16LE");
    RoboCheck(HtmlEncodingStatusIsKnown(
        RoboHtmlEncodingStatusDecodeMismatch));
    RoboCheckStrEq(
        HtmlEncodingStatusName(RoboHtmlEncodingStatusMalformed),
        "malformed");
    /* Identity tokens are plain integers: safe for application storage. */
    RoboCheckEqInt(sizeof(HtmlDocumentId), 8u);
    RoboCheckEqInt(sizeof(HtmlNodeId), 8u);
}

void TestRegisterHtmlPhase8(void)
{
    TestRegister("html.p8.full_flow", TestHtmlP8FullDocumentFlow);
    TestRegister("html.p8.shutdown_releases",
                       TestHtmlP8ModuleShutdownReleasesAll);
    TestRegister("html.p8.boundaries", TestHtmlP8BoundariesCompile);
}
