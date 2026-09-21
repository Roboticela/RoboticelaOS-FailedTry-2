/*
 * RoboHTML Phase 8.1 document suite:
 * creation, empty state, source attachment, encoding boundary,
 * node registration, teardown behavior.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLResource.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboTest.h"

#include <string.h>

static HtmlDiag gDiag;

static void HtmlDocFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
    HtmlDiagReset(&gDiag);
}

static void HtmlDocFixtureEnd(void)
{
    HtmlShutdown();
}

static void TestHtmlDocCreateEmpty(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlDocumentLifecycle life;
    HtmlNodeHandle root;

    HtmlDocFixtureBegin();

    RoboCheckEqInt(HtmlDocumentCreate(NULL, &gDiag),
                      RoboInvalidArgument);
    RoboCheckEqInt(gDiag.domain, RoboHtmlDomainDocument);

    RoboCheckEqInt(HtmlDocumentCreate(&id, &gDiag), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    RoboCheckEqInt(gDiag.code, RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleCreated);
    RoboCheck(HtmlDocumentIsLive(id));
    RoboCheck(HtmlDocumentIsEmpty(id));
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 1u); /* root only */
    RoboCheckEqInt(HtmlLiveDocumentCount(), 1u);

    root = HtmlDocumentRoot(id);
    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    {
        HtmlNodeKind kind = (HtmlNodeKind)0;
        RoboCheckEqInt(HtmlNodeHandleResolve(&root, &kind),
                          RoboOk);
        RoboCheckEqInt(kind, RoboHtmlNodeKindDocument);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, &gDiag), RoboOk);
    HtmlDocFixtureEnd();
}

static void TestHtmlDocSourceAttach(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlDocumentLifecycle life;
    HtmlSource src;
    static const uint8_t bytes[] = "<html></html>";

    HtmlDocFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);

    /* NULL / unknown args. */
    RoboCheckEqInt(HtmlDocumentAttachSource(
                          RoboHtmlDocumentIdNone, &src, &gDiag),
                      RoboNotFound);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, NULL, &gDiag),
                      RoboInvalidArgument);

    /*
     * Empty source: settles through INTERACTIVE to COMPLETE, because there
     * is nothing to parse and so nothing can be outstanding. The document
     * stays empty either way.
     */
    RoboCheckEqInt(HtmlSourceEmpty(&src, &gDiag), RoboOk);
    RoboCheck(HtmlSourceIsEmpty(&src));
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, &gDiag),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsComplete(id));
    RoboCheck(!HtmlDocumentIsInteractive(id));
    RoboCheck(HtmlDocumentIsSettled(id));
    RoboCheck(HtmlDocumentIsEmpty(id));

    /* COMPLETE accepts a replacement source (COMPLETE → LOADING). */
    RoboCheckEqInt(HtmlSourceMemory(&src, bytes, sizeof(bytes) - 1u,
                                              &gDiag),
                      RoboOk);
    RoboCheckEqInt(HtmlSourceSetName(&src, "app://main",
                                                &gDiag),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, &gDiag),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleLoading);
    RoboCheckStrEq(HtmlDocumentSourceName(id), "app://main");
    /* Non-empty source stays LOADING until a parse finishes. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, &gDiag),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleInteractive);
    RoboCheck(HtmlDocumentIsInteractive(id));
    RoboCheck(!HtmlDocumentIsComplete(id));
    /* INTERACTIVE → INTERACTIVE is refused (nothing changed). */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, &gDiag),
                      RoboInvalidArgument);
    /* The DOM is live at INTERACTIVE, so completion is allowed. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, &gDiag), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt(life, RoboHtmlDocumentLifecycleComplete);
    /* Completion happens once. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, &gDiag),
                      RoboInvalidArgument);

    /* Detach on a settled document is a no-op success. */
    RoboCheckEqInt(HtmlDocumentDetachSource(id, &gDiag), RoboOk);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    HtmlDocFixtureEnd();
}

static void TestHtmlDocEncodingBoundary(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlEncodingState st;

    HtmlDocFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);

    RoboCheckEqInt(HtmlDocumentGetEncodingState(id, &st),
                      RoboOk);
    RoboCheckEqInt(st.declared, RoboHtmlEncodingUnknown);
    RoboCheckEqInt(st.actual, RoboHtmlEncodingUnknown);
    RoboCheckEqInt(st.status, RoboHtmlEncodingStatusOk);

    /* Known label: declared recorded, actual defaults to UTF-8. */
    RoboCheckEqInt(
        HtmlDocumentInputEncodingDeclared(id, "UTF-8", &gDiag),
        RoboOk);
    RoboCheckEqInt(HtmlDocumentGetEncodingState(id, &st), RoboOk);
    RoboCheckEqInt(st.declared, RoboHtmlEncodingUtf8);
    RoboCheckEqInt(st.actual, RoboHtmlEncodingUtf8);

    /* Case-insensitive label + alias. */
    RoboCheckEqInt(
        HtmlDocumentInputEncodingDeclared(id, "utf8", &gDiag),
        RoboOk);
    RoboCheckEqInt(HtmlDocumentGetEncodingState(id, &st), RoboOk);
    RoboCheckEqInt(st.declared, RoboHtmlEncodingUtf8);

    /* Valid ASCII but unsupported label: recorded, not fatal. */
    RoboCheckEqInt(
        HtmlDocumentInputEncodingDeclared(id, "klingon-8", &gDiag),
        RoboNotSupported);
    RoboCheckEqInt(HtmlDocumentGetEncodingState(id, &st), RoboOk);
    RoboCheckEqInt(st.status, RoboHtmlEncodingStatusUnsupportedLabel);
    /* Previous declaration survives an unsupported attempt. */
    RoboCheckEqInt(st.declared, RoboHtmlEncodingUtf8);

    /* Non-ASCII label is a boundary failure. */
    {
        const char bad[] = { (char)0xC3, (char)0xA9, 'x', '\0' };
        RoboCheckEqInt(
            HtmlDocumentInputEncodingDeclared(id, bad, &gDiag),
            RoboParseError);
        RoboCheckEqInt(gDiag.domain, RoboHtmlDomainEncoding);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    HtmlDocFixtureEnd();
}

static void TestHtmlDocNodeRegistration(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNodeHandle b;
    HtmlNodeKind kind;

    HtmlDocFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    root = HtmlDocumentRoot(id);

    /* Kinds and origins are validated. */
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(
            id, (HtmlNodeKind)50, RoboHtmlNodeOriginTest, &a,
            &gDiag),
        RoboInvalidArgument);
    RoboCheckEqInt(gDiag.domain, RoboHtmlDomainNode);

    /* Registration hands out unique, resolvable handles. */
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(id, RoboHtmlNodeKindElement,
                                         RoboHtmlNodeOriginTest, &a,
                                         &gDiag),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(id, RoboHtmlNodeKindText,
                                         RoboHtmlNodeOriginTest, &b,
                                         &gDiag),
        RoboOk);
    RoboCheck(!HtmlNodeHandleEqual(&a, &b));
    RoboCheck(!HtmlNodeHandleEqual(&a, &root));
    RoboCheckEqInt(a.documentId, id);
    RoboCheckEqInt(b.documentId, id);
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 3u);

    RoboCheckEqInt(HtmlNodeHandleResolve(&a, &kind), RoboOk);
    RoboCheckEqInt(kind, RoboHtmlNodeKindElement);
    RoboCheckEqInt(HtmlNodeHandleResolve(&b, &kind), RoboOk);
    RoboCheckEqInt(kind, RoboHtmlNodeKindText);

    /* Registering into a destroyed document is refused. */
    {
        HtmlDocumentId id2 = RoboHtmlDocumentIdNone;
        HtmlNodeHandle h;
        RoboCheckEqInt(HtmlDocumentCreate(&id2, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentDestroy(id2, NULL), RoboOk);
        RoboCheckEqInt(
            HtmlDocumentRegisterNode(id2, RoboHtmlNodeKindText,
                                             RoboHtmlNodeOriginTest, &h,
                                             &gDiag),
            RoboNotFound);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    HtmlDocFixtureEnd();
}

static void TestHtmlDocTeardown(void)
{
    HtmlDocumentId a = RoboHtmlDocumentIdNone;
    HtmlDocumentId b = RoboHtmlDocumentIdNone;
    HtmlDocumentLifecycle life;
    HtmlNodeHandle aNode;

    HtmlDocFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&a, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreate(&b, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlDocumentRegisterNode(a, RoboHtmlNodeKindElement,
                                         RoboHtmlNodeOriginTest, &aNode,
                                         NULL),
        RoboOk);

    /* Valid destroy. */
    RoboCheckEqInt(HtmlDocumentDestroy(a, &gDiag), RoboOk);
    RoboCheck(!HtmlDocumentIsLive(a));

    /* Repeated teardown is detected, never undefined. */
    RoboCheckEqInt(HtmlDocumentDestroy(a, &gDiag), RoboNotFound);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(a, &life),
                      RoboNotFound);
    RoboCheckEqInt(HtmlDocumentDestroy(RoboHtmlDocumentIdNone,
                                                 &gDiag),
                      RoboNotFound);

    /* Sibling documents are unaffected. */
    RoboCheck(HtmlDocumentIsLive(b));
    RoboCheckEqInt(HtmlDocumentNodeCount(b), 1u);

    /* Handles of destroyed documents are stale, not resolvable. */
    RoboCheck(HtmlNodeHandleIsStale(&aNode));
    RoboCheckEqInt(HtmlNodeHandleResolve(&aNode, NULL),
                      RoboNotFound);

    /* And a *fresh* node from a still-live document stays resolvable. */
    {
        HtmlNodeHandle bNode;
        RoboCheckEqInt(
            HtmlDocumentRegisterNode(b, RoboHtmlNodeKindComment,
                                             RoboHtmlNodeOriginTest,
                                             &bNode, NULL),
            RoboOk);
        RoboCheck(!HtmlNodeHandleIsStale(&bNode));
        RoboCheckEqInt(HtmlNodeHandleResolve(&bNode, NULL),
                          RoboOk);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(b, NULL), RoboOk);
    RoboCheckEqInt(HtmlLiveDocumentCount(), 0u);
    HtmlDocFixtureEnd();
}

static void TestHtmlDocIdentityMonotonic(void)
{
    HtmlDocumentId ids[RoboHtmlMaxDocuments];
    int i;
    int ok = 1;

    HtmlDocFixtureBegin();
    for (i = 0; i < (int)RoboHtmlMaxDocuments; i++) {
        ids[i] = RoboHtmlDocumentIdNone;
        RoboCheckEqInt(HtmlDocumentCreate(&ids[i], NULL), RoboOk);
    }
    /* Table exhaustion is a reported error, not a crash. */
    {
        HtmlDocumentId extra = RoboHtmlDocumentIdNone;
        RoboCheckEqInt(HtmlDocumentCreate(&extra, &gDiag),
                          RoboOutOfMemory);
        RoboCheckEqInt(extra, RoboHtmlDocumentIdNone);
    }
    /* All live ids distinct and non-zero. */
    for (i = 0; i < (int)RoboHtmlMaxDocuments; i++) {
        int j;
        if (ids[i] == RoboHtmlDocumentIdNone) {
            ok = 0;
        }
        for (j = 0; j < i; j++) {
            if (ids[i] == ids[j]) {
                ok = 0;
            }
        }
    }
    RoboCheck(ok);
    for (i = 0; i < (int)RoboHtmlMaxDocuments; i++) {
        RoboCheckEqInt(HtmlDocumentDestroy(ids[i], NULL), RoboOk);
    }
    RoboCheckEqInt(HtmlLiveDocumentCount(), 0u);
    HtmlDocFixtureEnd();
}

void TestRegisterHtmlDocument(void)
{
    TestRegister("html.doc.create_empty", TestHtmlDocCreateEmpty);
    TestRegister("html.doc.source_attach", TestHtmlDocSourceAttach);
    TestRegister("html.doc.encoding", TestHtmlDocEncodingBoundary);
    TestRegister("html.doc.nodes", TestHtmlDocNodeRegistration);
    TestRegister("html.doc.teardown", TestHtmlDocTeardown);
    TestRegister("html.doc.identity", TestHtmlDocIdentityMonotonic);
}
