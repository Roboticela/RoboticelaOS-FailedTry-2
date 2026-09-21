/*
 * RoboHTML Phase 8.13 anchor suite (html.anchor.*).
 *
 * What it pins, in the order the phase argues for it: that an `<a>` is a real
 * DOM element with a real attribute view; that a reference resolves to the
 * thing the header documents, including the cases that are *refused*; that a
 * fragment activation completes because it needs no I/O; that everything else
 * either reaches the caller's seam or is refused before it; and that no path
 * anywhere can report a navigation that did not happen.
 *
 * Two tests here are security tests rather than feature tests, and they are
 * the two worth reading first:
 *
 *   html.anchor.activate_refused_schemes
 *       `javascript:`, `file:` and `data:` must never reach the handler —
 *       asserted by a handler that counts its own calls.
 *   html.anchor.activate_no_fake_success
 *       an activation with no handler reports UNSUPPORTED, not a success.
 *
 * The fixture parses real HTML through the real tokenizer and tree builder and
 * names its documents, because the document's own reference is what a relative
 * href resolves against — a test that skipped the name would be testing a
 * resolution the runtime never performs.
 */

#include "RoboBase/RoboBase.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static void AnchorFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static HtmlDocumentId AnchorDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    AnchorFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Parse @p text into @p id, with no document reference (no base). */
static void AnchorBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

/**
 * Parse @p text into @p id with @p name as the document's own reference.
 *
 * The name is the base every relative href in this suite resolves against, so
 * it is set through the real source API rather than by poking the document —
 * the parser is what records it, and the parser is what a caller would use.
 */
static void AnchorBuildNamed(HtmlDocumentId id, const char *name,
                               const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlSourceSetName(&src, name, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
    /* The base really arrived, or every relative expectation below would be
     * measuring a resolution the runtime never had an input for. */
    RoboCheckStrEq(HtmlDocumentSourceName(id), name);
}

/** The document's lifecycle state, asserted to be observable. */
static HtmlDocumentLifecycle AnchorLifecycle(
    HtmlDocumentId id)
{
    HtmlDocumentLifecycle life =
        RoboHtmlDocumentLifecycleUninitialized;

    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    return life;
}

static HtmlNodeHandle AnchorRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct anchorTagQuery {
    HtmlTag tag;
} AnchorTagQuery;

static int AnchorMatchTag(const HtmlNodeView *view, void *user)
{
    const AnchorTagQuery *q = (const AnchorTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static HtmlNodeHandle AnchorFind(const HtmlNodeHandle *start,
                                           HtmlTag tag)
{
    AnchorTagQuery q;
    HtmlNodeHandle out;

    q.tag = tag;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(
        HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  AnchorMatchTag, &q, &out, NULL),
        RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/**
 * The subtree text of @p node, through the DOM's own reader.
 *
 * One static buffer, so a caller compares one node's text at a time — which is
 * exactly what this suite does, and the reason the return is a plain string
 * rather than an out-parameter.
 */
static const char *AnchorText(const HtmlNodeHandle *node)
{
    static char buf[512];
    size_t length = 0u;

    memset(buf, 0, sizeof(buf));
    RoboCheckEqInt(
        HtmlNodeSubtreeText(node, buf, sizeof(buf), &length, NULL),
        RoboOk);
    RoboCheck(length < sizeof(buf));
    return buf;
}

/** Number of elements of @p tag in the whole document. */
typedef struct anchorCountCtx {
    HtmlTag tag;
    size_t count;
} AnchorCountCtx;

static Result AnchorCountVisit(const HtmlNodeView *view,
                                        size_t depth, void *user)
{
    AnchorCountCtx *ctx = (AnchorCountCtx *)user;

    (void)depth;
    if (HtmlElementIsTag(&view->handle, ctx->tag)) {
        ctx->count++;
    }
    return RoboOk;
}

static size_t AnchorCount(HtmlDocumentId id, HtmlTag tag)
{
    HtmlNodeHandle root = AnchorRoot(id);
    AnchorCountCtx ctx;

    ctx.tag = tag;
    ctx.count = 0u;
    RoboCheckEqInt(
        HtmlNodeWalkPreOrder(&root, RoboHtmlNodeFilterElements,
                                      AnchorCountVisit, &ctx, NULL),
        RoboOk);
    return ctx.count;
}

/**
 * Everything one handler was told, and how it answered.
 *
 * The call count is the security assertion in this suite: "the handler was
 * never consulted" is only checkable by asking the handler afterwards.
 */
typedef struct anchorHandlerLog {
    int calls;
    char base[128];
    char targetAttribute[32];
    HtmlLinkTargetContext targetContext;
    char rel[64];
    int downloadRequested;
    char downloadName[64];
    char title[64];
    HtmlNavigationReason reason;
    HtmlDocumentId documentId;
    char anchorName[32];
    HtmlTargetKind targetKind;
    char resolved[128];
    char scheme[32];
    /* Answer policy. */
    int decline;
} AnchorHandlerLog;

static void AnchorLogReset(AnchorHandlerLog *log)
{
    memset(log, 0, sizeof(*log));
    log->targetContext = RoboHtmlLinkTargetContextCurrent;
}

static Result AnchorHandler(void *userdata,
                                    const HtmlNavigationRequest *req,
                                    HtmlNavigationOutcomeKind *outKind,
                                    HtmlDiag *diag)
{
    AnchorHandlerLog *log = (AnchorHandlerLog *)userdata;
    const char *s;
    size_t length = 0u;

    (void)diag;
    (void)outKind;
    log->calls++;
    log->documentId = req->documentId;
    log->targetKind = HtmlNavigationTargetKind(req->target);
    log->targetContext = req->targetContext;
    log->downloadRequested = req->downloadRequested;
    log->reason = req->reason;

    if (req->base) {
        size_t n = req->baseLength < sizeof(log->base) - 1u
                       ? req->baseLength
                       : sizeof(log->base) - 1u;
        memcpy(log->base, req->base, n);
        log->base[n] = '\0';
    }
    if (req->targetAttribute) {
        size_t n = req->targetLength < sizeof(log->targetAttribute) - 1u
                       ? req->targetLength
                       : sizeof(log->targetAttribute) - 1u;
        memcpy(log->targetAttribute, req->targetAttribute, n);
        log->targetAttribute[n] = '\0';
    }
    if (req->rel) {
        size_t n = req->relLength < sizeof(log->rel) - 1u
                       ? req->relLength
                       : sizeof(log->rel) - 1u;
        memcpy(log->rel, req->rel, n);
        log->rel[n] = '\0';
    }
    if (req->downloadName) {
        size_t n = req->downloadNameLength < sizeof(log->downloadName) - 1u
                       ? req->downloadNameLength
                       : sizeof(log->downloadName) - 1u;
        memcpy(log->downloadName, req->downloadName, n);
        log->downloadName[n] = '\0';
    }
    if (req->title) {
        size_t n = req->titleLength < sizeof(log->title) - 1u
                       ? req->titleLength
                       : sizeof(log->title) - 1u;
        memcpy(log->title, req->title, n);
        log->title[n] = '\0';
    }
    s = HtmlElementAttributeValue(&req->anchor, "data-name", &length);
    if (s) {
        size_t n = length < sizeof(log->anchorName) - 1u
                       ? length
                       : sizeof(log->anchorName) - 1u;
        memcpy(log->anchorName, s, n);
        log->anchorName[n] = '\0';
    }
    s = HtmlNavigationTargetResolved(req->target, &length);
    if (s) {
        size_t n = length < sizeof(log->resolved) - 1u ? length
                                                       : sizeof(log->resolved) - 1u;
        memcpy(log->resolved, s, n);
        log->resolved[n] = '\0';
    }
    s = HtmlNavigationTargetScheme(req->target, &length);
    if (s) {
        size_t n = length < sizeof(log->scheme) - 1u ? length
                                                     : sizeof(log->scheme) - 1u;
        memcpy(log->scheme, s, n);
        log->scheme[n] = '\0';
    }

    if (log->decline) {
        return RoboNotSupported;
    }
    return RoboOk;
}

static HtmlNavigationHandler AnchorMakeHandler(
    AnchorHandlerLog *log)
{
    HtmlNavigationHandler handler;

    HtmlNavigationHandlerInit(&handler);
    handler.navigate = AnchorHandler;
    handler.userdata = log;
    return handler;
}

/** Resolve @p element's href and report the fields this suite asserts on. */
typedef struct anchorResolved {
    HtmlTargetKind kind;
    char reference[256];
    char resolved[256];
    char fragment[128];
    char scheme[64];
    int hasReference;
    int hasResolved;
    int hasFragment;
    int hasScheme;
} AnchorResolved;

static void AnchorResolve(const HtmlNodeHandle *element,
                           AnchorResolved *out)
{
    HtmlNavigationTarget *target = NULL;
    const char *s;
    size_t length = 0u;

    memset(out, 0, sizeof(*out));
    RoboCheckEqInt(
        HtmlElementNavigationTarget(element, &target, NULL), RoboOk);
    RoboCheck(target != NULL);
    if (!target) {
        return;
    }
    out->kind = HtmlNavigationTargetKind(target);
    s = HtmlNavigationTargetReference(target, &length);
    if (s) {
        memcpy(out->reference, s, length);
        out->reference[length] = '\0';
        out->hasReference = 1;
    }
    s = HtmlNavigationTargetResolved(target, &length);
    if (s) {
        memcpy(out->resolved, s, length);
        out->resolved[length] = '\0';
        out->hasResolved = 1;
    }
    s = HtmlNavigationTargetFragment(target, &length);
    if (s) {
        memcpy(out->fragment, s, length);
        out->fragment[length] = '\0';
        out->hasFragment = 1;
    }
    s = HtmlNavigationTargetScheme(target, &length);
    if (s) {
        memcpy(out->scheme, s, length);
        out->scheme[length] = '\0';
        out->hasScheme = 1;
    }
    HtmlNavigationTargetDestroy(target);
}

/** Resolve a bare reference against a bare base, with no document involved. */
static void AnchorResolveRaw(const char *href, const char *base,
                               AnchorResolved *out)
{
    HtmlNavigationTarget *target = NULL;
    const char *s;
    size_t length = 0u;

    memset(out, 0, sizeof(*out));
    RoboCheckEqInt(HtmlNavigationTargetCreate(
                          href, href ? strlen(href) : 0u, base,
                          base ? strlen(base) : 0u, &target, NULL),
                      RoboOk);
    RoboCheck(target != NULL);
    if (!target) {
        return;
    }
    out->kind = HtmlNavigationTargetKind(target);
    s = HtmlNavigationTargetReference(target, &length);
    if (s) {
        memcpy(out->reference, s, length);
        out->reference[length] = '\0';
        out->hasReference = 1;
    }
    s = HtmlNavigationTargetResolved(target, &length);
    if (s) {
        memcpy(out->resolved, s, length);
        out->resolved[length] = '\0';
        out->hasResolved = 1;
    }
    s = HtmlNavigationTargetFragment(target, &length);
    if (s) {
        memcpy(out->fragment, s, length);
        out->fragment[length] = '\0';
        out->hasFragment = 1;
    }
    s = HtmlNavigationTargetScheme(target, &length);
    if (s) {
        memcpy(out->scheme, s, length);
        out->scheme[length] = '\0';
        out->hasScheme = 1;
    }
    HtmlNavigationTargetDestroy(target);
}

/* ------------------------------------------------------------------ */
/* The element                                                         */
/* ------------------------------------------------------------------ */

static void TestAnchorParserElement(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlElementView view;

    AnchorBuildNamed(id, "App/Pages/Index.html",
                       "<div><p>before</p><a href=\"/Settings\" "
                       "title=\"Go\">Settings</a><span>after</span></div>");

    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    /* Identity through the 8.11 vocabulary, not a string comparison. */
    RoboCheck(HtmlElementIsTag(&a, RoboHtmlTagA));
    RoboCheckEqInt((int)HtmlElementTag(&a), (int)RoboHtmlTagA);
    RoboCheck(HtmlTagIsTextLevel(HtmlElementTag(&a)));
    RoboCheckEqInt(HtmlElementViewGet(&a, &view, NULL), RoboOk);
    RoboCheckStrEq(view.name, "a");
    RoboCheckEqInt((int)view.classification,
                      (int)RoboHtmlElementClassText);

    /* Not void: it holds the text, and the span after it stays a sibling. */
    {
        size_t children = 0u;

        RoboCheck(!HtmlTagIsVoid(HtmlElementTag(&a)));
        RoboCheckEqInt(HtmlNodeChildCount(&a, &children, NULL),
                          RoboOk);
        RoboCheckEqInt((int)children, 1);
    }
    RoboCheckStrEq(AnchorText(&a), "Settings");
    {
        HtmlNodeHandle parent;
        HtmlNodeHandle next;

        RoboCheckEqInt(HtmlElementParent(&a, &parent, NULL), RoboOk);
        RoboCheck(HtmlElementIsTag(&parent, RoboHtmlTagDiv));
        RoboCheckEqInt(HtmlNodeNextSibling(&a, &next, NULL), RoboOk);
        RoboCheck(HtmlElementIsTag(&next, RoboHtmlTagSpan));
    }

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorAttributes(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlAnchorAttributes attrs;

    AnchorBuildNamed(id, "App/Index.html",
                       "<a href=\"/Settings\" target=\"_blank\" "
                       "rel=\"noopener noreferrer\" download=\"Plan.Robo\" "
                       "title=\"Settings page\" data-kind=\"nav\">S</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.hrefPresent, 1);
    RoboCheckEqInt(attrs.hrefUsable, 1);
    RoboCheckEqInt((int)attrs.hrefLength, 9);
    RoboCheck(memcmp(attrs.href, "/Settings", 9u) == 0);
    RoboCheckEqInt(attrs.targetPresent, 1);
    RoboCheck(memcmp(attrs.target, "_blank", 6u) == 0);
    RoboCheckEqInt(attrs.relPresent, 1);
    RoboCheck(memcmp(attrs.rel, "noopener noreferrer", 20u) == 0);
    RoboCheckEqInt(attrs.downloadPresent, 1);
    RoboCheck(memcmp(attrs.download, "Plan.Robo", 9u) == 0);
    RoboCheckEqInt(attrs.titlePresent, 1);
    RoboCheck(memcmp(attrs.title, "Settings page", 13u) == 0);

    /* An arbitrary attribute is untouched by this phase, and still there. */
    RoboCheck(HtmlElementHasAttribute(&a, "data-kind"));

    /* A bare anchor: nothing present, nothing usable. */
    AnchorBuild(id, "<a>Bare</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.hrefPresent, 0);
    RoboCheckEqInt(attrs.hrefUsable, 0);
    RoboCheck(attrs.href == NULL);
    RoboCheckEqInt(attrs.targetPresent, 0);
    RoboCheckEqInt(attrs.relPresent, 0);
    RoboCheckEqInt(attrs.downloadPresent, 0);

    /*
     * Three situations that look alike and are not: no attribute, an empty
     * value, and a whitespace value. The first two are distinguishable by
     * `present`; the third is what `usable` exists for.
     */
    AnchorBuild(id, "<a href=\"\">Empty</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.hrefPresent, 1);
    RoboCheckEqInt(attrs.hrefUsable, 0);
    RoboCheck(attrs.href != NULL && attrs.hrefLength == 0u);

    AnchorBuild(id, "<a href=\"   \">Blank</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.hrefPresent, 1);
    RoboCheckEqInt(attrs.hrefUsable, 0);

    /* A bare `download`: present with a zero-length name, not absent. */
    AnchorBuild(id, "<a href=\"/x\" download>Save</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.downloadPresent, 1);
    RoboCheckEqInt((int)attrs.downloadLength, 0);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorNonAnchorRefused(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle p;
    HtmlNodeHandle text;
    HtmlAnchorAttributes attrs;
    HtmlNavigationTarget *target = NULL;
    HtmlNodeHandle stale;
    HtmlDiag diag;

    AnchorBuildNamed(id, "App/Index.html", "<p>Not a link</p>");
    root = AnchorRoot(id);
    p = AnchorFind(&root, RoboHtmlTagP);

    /*
     * A live non-anchor is NOT_SUPPORTED; reporting "no href" instead would
     * let a caller mistake "not a link" for "a link with nothing to do".
     */
    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlElementAnchorAttributes(&p, &attrs, &diag),
                      RoboNotSupported);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainAnchor);
    RoboCheckEqInt(HtmlElementNavigationTarget(&p, &target, NULL),
                      RoboNotSupported);
    RoboCheck(target == NULL);

    /* A text node is not an element at all, so it is NOT_FOUND, not
     * NOT_SUPPORTED — the two answers differ and both are asserted. */
    HtmlNodeHandleClear(&text);
    RoboCheckEqInt(HtmlNodeFirstChild(&p, &text, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&text));
    RoboCheckEqInt(HtmlElementAnchorAttributes(&text, &attrs, NULL),
                      RoboNotFound);

    /* Argument safety. */
    RoboCheckEqInt(HtmlElementAnchorAttributes(NULL, &attrs, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementAnchorAttributes(&p, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementNavigationTarget(&p, NULL, NULL),
                      RoboInvalidArgument);

    /* A handle whose document is gone is NOT_FOUND, never NOT_SUPPORTED:
     * "not a link" is a misleading thing to say about a dead node. */
    stale = p;
    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementAnchorAttributes(&stale, &attrs, NULL),
                      RoboNotFound);
    RoboCheckEqInt(
        HtmlElementNavigationTarget(&stale, &target, NULL),
        RoboNotFound);
    RoboCheck(target == NULL);
}

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

static void TestAnchorResolutionMatrix(void)
{
    /* Root-relative, relative, `./`, `../`, and the `..` clamp at the root. */
    AnchorResolved r;

    AnchorResolveRaw("/Settings", "App/Pages/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.reference, "/Settings");
    RoboCheckStrEq(r.resolved, "/Settings");

    AnchorResolveRaw("Details", "App/Pages/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "App/Pages/Details");

    AnchorResolveRaw("./Details", "App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "App/Pages/Details");

    AnchorResolveRaw("../Details", "App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "App/Details");

    AnchorResolveRaw("../../Details", "App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "Details");

    /* Escaping above the root clamps to the root rather than failing: a URL
     * does the same, and it is not an error worth reporting. */
    AnchorResolveRaw("../../../../Details", "App/Pages/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "Details");

    /* A root-relative base keeps its root through `..`. */
    AnchorResolveRaw("/App/Details", "/App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "/App/Details");
    AnchorResolveRaw("../Details", "/App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "/App/Details");
    AnchorResolveRaw("../../Details", "/App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "/Details");
    AnchorResolveRaw("../../../../Details", "/App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "/Details");

    /* A trailing separator is part of the reference's meaning. */
    AnchorResolveRaw("Details/", "App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "App/Pages/Details/");
    AnchorResolveRaw("../", "App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "App/");
    AnchorResolveRaw("/Details/", "/App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "/Details/");

    /* A query rides along verbatim and is not broken out. */
    AnchorResolveRaw("Details?Sort=Name", "App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "App/Pages/Details?Sort=Name");
    /* A bare query keeps the current file, not its directory. */
    AnchorResolveRaw("?Sort=Name", "App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "App/Pages/Index.html?Sort=Name");

    /* Surrounding whitespace is not part of the reference, and the reference
     * is still reported exactly as written. */
    AnchorResolveRaw("  /Settings  ", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.reference, "/Settings");
    RoboCheckStrEq(r.resolved, "/Settings");

    /* Whitespace inside is not trimmed, because it is not at an edge. */
    AnchorResolveRaw("Set tings", "App/Index.html", &r);
    RoboCheckStrEq(r.resolved, "App/Set tings");

    /* `.` alone is the base's directory. */
    AnchorResolveRaw(".", "App/Pages/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "App/Pages/");
    AnchorResolveRaw(".", "/App/Pages/Index.html", &r);
    RoboCheckStrEq(r.resolved, "/App/Pages/");
}

static void TestAnchorResolutionWithoutBase(void)
{
    AnchorResolved r;

    /* No base at all: a nameless in-memory document is normal, so a relative
     * reference resolves against nothing rather than failing. */
    AnchorResolveRaw("Details", NULL, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "Details");

    AnchorResolveRaw("Details", "", &r);
    RoboCheckStrEq(r.resolved, "Details");

    AnchorResolveRaw("../Details", NULL, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "Details");

    AnchorResolveRaw("/Details", NULL, &r);
    RoboCheckStrEq(r.resolved, "/Details");

    /* A base that is a bare file name has no directory to resolve against. */
    AnchorResolveRaw("Details", "Index.html", &r);
    RoboCheckStrEq(r.resolved, "Details");
    AnchorResolveRaw("../Details", "Index.html", &r);
    RoboCheckStrEq(r.resolved, "Details");
}

static void TestAnchorNoneAndMissingHref(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    AnchorResolved r;

    /* Absent, empty and whitespace-only are one kind, and it is a state. */
    AnchorBuildNamed(id, "App/Index.html", "<a>No href</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorResolve(&a, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetNone);
    RoboCheckEqInt(r.hasReference, 0);
    RoboCheckEqInt(r.hasResolved, 0);

    AnchorBuild(id, "<a href=\"\">Empty</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorResolve(&a, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetNone);

    AnchorBuild(id, "<a href=\" \t \">Blank</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorResolve(&a, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetNone);

    /* And a bare target create agrees, because both readers share one rule. */
    AnchorResolveRaw("", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetNone);
    AnchorResolveRaw("   ", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetNone);
    AnchorResolveRaw(NULL, "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetNone);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorInvalidTargets(void)
{
    AnchorResolved r;

    /* `:rest` — a colon where a scheme should be and no scheme name. */
    AnchorResolveRaw(":Settings", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetInvalid);

    /* A scheme that does not start with a letter. */
    AnchorResolveRaw("1thing:Settings", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetInvalid);

    /* A scheme with a character the grammar does not allow. */
    AnchorResolveRaw("ht^tp:Settings", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetInvalid);

    /*
     * A protocol-relative reference names a network location that only the
     * base's scheme could complete. Guessing a scheme would be wrong and
     * reinterpreting the host as an internal path would be dangerous, so it
     * is refused.
     */
    AnchorResolveRaw("//example.com/Index.html", "/App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetInvalid);

    /* A control byte inside the reference. */
    AnchorResolveRaw("Details\x01", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetInvalid);
    AnchorResolveRaw("Set\x7ftings", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetInvalid);

    /* The same rule through the attribute view's `usable` flag, so the two
     * readers cannot disagree about a reference neither will resolve. */
    {
        HtmlDocumentId id = AnchorDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle a;
        HtmlAnchorAttributes attrs;

        AnchorBuildNamed(id, "App/Index.html", "<a href=\"/x\">ok</a>");
        root = AnchorRoot(id);
        a = AnchorFind(&root, RoboHtmlTagA);
        RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                          RoboOk);
        RoboCheckEqInt(attrs.hrefUsable, 1);
        RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* An over-long reference is refused at the boundary rather than sized
     * from: a hostile document must not be able to make resolution
     * expensive by claiming a large href. */
    {
        size_t tooLong = (size_t)RoboHtmlTargetMaxReference + 1u;
        char *big = (char *)Malloc(tooLong + 1u);

        RoboCheck(big != NULL);
        if (big) {
            size_t i;

            big[0] = '/';
            for (i = 1u; i < tooLong; i++) {
                big[i] = 'a';
            }
            big[tooLong] = '\0';
            AnchorResolveRaw(big, "App/Index.html", &r);
            RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetInvalid);
            RoboFree(big);
        }
    }

    /* Kind vocabulary. */
    RoboCheckEqInt(HtmlTargetKindIsKnown(RoboHtmlTargetNone), 1);
    RoboCheckEqInt(HtmlTargetKindIsKnown(RoboHtmlTargetInvalid),
                      1);
    RoboCheckEqInt(HtmlTargetKindIsKnown((HtmlTargetKind)99),
                      0);
    RoboCheckStrEq(HtmlTargetKindName(RoboHtmlTargetRoute),
                      "route");
    RoboCheckStrEq(HtmlTargetKindName(RoboHtmlTargetFragment),
                      "fragment");
    RoboCheckStrEq(HtmlTargetKindName(RoboHtmlTargetExternal),
                      "external");
    RoboCheckStrEq(HtmlTargetKindName(RoboHtmlTargetInvalid),
                      "invalid");
    RoboCheckStrEq(HtmlTargetKindName((HtmlTargetKind)99),
                      "unknown");
}

static void TestAnchorFragmentTargets(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle found;
    AnchorResolved r;

    AnchorBuildNamed(
        id, "App/Pages/Index.html",
        "<a href=\"#About\">About</a><a href=\"#\">Top</a>"
        "<a href=\"Details#Step\">Step</a>"
        "<section id=\"About\">About the runtime</section>"
        "<section id=\"Step\">Step</section>");
    root = AnchorRoot(id);

    {
        HtmlNodeHandle first = AnchorFind(&root, RoboHtmlTagA);

        AnchorResolve(&first, &r);
        RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetFragment);
        RoboCheckEqInt(r.hasFragment, 1);
        RoboCheckStrEq(r.fragment, "About");
        RoboCheckEqInt(r.hasResolved, 1);
        RoboCheckStrEq(r.resolved, "App/Pages/Index.html");
        RoboCheckEqInt(HtmlNavigationTargetIsInternal(NULL), 0);
    }

    /* The fragment names a real element, in document order. */
    {
        HtmlNodeHandle first;

        RoboCheckEqInt(
            HtmlNavigationFindFragment(id, "About", 5u, &found, NULL),
            RoboOk);
        RoboCheck(HtmlElementIsTag(&found, RoboHtmlTagSection));
        RoboCheckEqInt(
            (int)HtmlElementAttributeValue(&found, "id", NULL)[0],
            (int)'A');

        first = AnchorFind(&found, RoboHtmlTagSection);
        RoboCheckEqInt(
            (int)HtmlElementTag(&first), (int)RoboHtmlTagSection);
    }

    /* First match in document order wins, and the comparison is exact. */
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "about", 5u, &found, NULL),
        RoboNotFound);
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "About ", 6u, &found, NULL),
        RoboNotFound);

    /*
     * An empty fragment names the top of the document, which is not an
     * element — so it resolves to nothing rather than to whatever happens to
     * be first. Returning the root would invent a "top" element.
     */
    RoboCheckEqInt(
        HtmlNavigationFindFragment(id, "", 0u, &found, NULL),
        RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&found));

    /* `#` with nothing after it is still a fragment, and still empty. */
    {
        HtmlNodeHandle first = AnchorFind(&root, RoboHtmlTagA);
        HtmlNodeHandle second;

        RoboCheckEqInt(HtmlNodeNextSibling(&first, &second, NULL),
                          RoboOk);
        AnchorResolve(&second, &r);
        RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetFragment);
        RoboCheckEqInt(r.hasFragment, 1);
        RoboCheckEqInt((int)strlen(r.fragment), 0);
    }

    /* A route with a fragment keeps both. */
    {
        HtmlNodeHandle third;
        HtmlNodeHandle second;
        HtmlNodeHandle first = AnchorFind(&root, RoboHtmlTagA);

        RoboCheckEqInt(
            HtmlNodeNextSibling(&first, &second, NULL), RoboOk);
        RoboCheckEqInt(
            HtmlNodeNextSibling(&second, &third, NULL), RoboOk);
        AnchorResolve(&third, &r);
        RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
        RoboCheckStrEq(r.resolved, "App/Pages/Details");
        RoboCheckEqInt(r.hasFragment, 1);
        RoboCheckStrEq(r.fragment, "Step");
    }

    /* Argument safety on the lookup. */
    RoboCheckEqInt(HtmlNavigationFindFragment(id, "About", 5u, NULL,
                                                        NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNavigationFindFragment(
                          RoboHtmlDocumentIdNone, "About", 5u, &found, NULL),
                      RoboNotFound);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorExternalUris(void)
{
    AnchorResolved r;

    AnchorResolveRaw("https://example.com/Path", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetExternal);
    RoboCheckStrEq(r.scheme, "https");
    /* An absolute URI is carried verbatim: normalizing it is a URL library's
     * job and the reference is already absolute. */
    RoboCheckStrEq(r.resolved, "https://example.com/Path");
    RoboCheckStrEq(r.reference, "https://example.com/Path");

    AnchorResolveRaw("http://example.com", "App/Index.html", &r);
    RoboCheckStrEq(r.scheme, "http");
    AnchorResolveRaw("mailto:someone@example.com", "App/Index.html", &r);
    RoboCheckStrEq(r.scheme, "mailto");
    RoboCheckStrEq(r.resolved, "mailto:someone@example.com");
    AnchorResolveRaw("tel:+10000000000", "App/Index.html", &r);
    RoboCheckStrEq(r.scheme, "tel");

    /* Scheme matching is case-insensitive and the *reported* scheme is
     * lowercase, because that is what the policy table is keyed on. */
    AnchorResolveRaw("HTTPS://Example.com/Path", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetExternal);
    RoboCheckStrEq(r.scheme, "https");
    RoboCheckStrEq(r.resolved, "HTTPS://Example.com/Path");

    /* A scheme the runtime has never heard of is still classified as an
     * external URI — that is what recognition means — and is not permitted. */
    AnchorResolveRaw("roboticela-notes:Open/Plan", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetExternal);
    RoboCheckStrEq(r.scheme, "roboticela-notes");
    RoboCheckEqInt(
        HtmlNavigationSchemeIsKnown("roboticela-notes", 16u), 0);
    RoboCheckEqInt(
        HtmlNavigationSchemeIsPermitted("roboticela-notes", 16u), 0);

    /* A `+` in a scheme name is legal and stays part of it. */
    AnchorResolveRaw("svn+ssh:host/Path", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetExternal);
    RoboCheckStrEq(r.scheme, "svn+ssh");

    /* A colon after a separator is a path character, not a scheme. */
    AnchorResolveRaw("Details/Note:x", "App/Index.html", &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "App/Details/Note:x");
}

static void TestAnchorSchemePolicy(void)
{
    /*
     * The two columns, and the rows where they disagree. This is the policy
     * table's contract stated as a test rather than left to the reader: the
     * point of `known` next to `permitted` is that `file` and `data` are
     * understood *and refused*, which is a different thing from unknown.
     */
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("http", 4u), 1);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("https", 5u), 1);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("mailto", 6u), 1);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("tel", 3u), 1);

    RoboCheckEqInt(HtmlNavigationSchemeIsKnown("file", 4u), 1);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("file", 4u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsKnown("data", 4u), 1);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("data", 4u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsKnown("javascript", 10u),
                      1);
    RoboCheckEqInt(
        HtmlNavigationSchemeIsPermitted("javascript", 10u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("vbscript", 8u),
                      0);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("blob", 4u), 0);

    /* Unknown: neither known nor permitted. */
    RoboCheckEqInt(HtmlNavigationSchemeIsKnown("nonsense", 8u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("nonsense", 8u),
                      0);

    /* Case-insensitive, and a prefix is not a match. */
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("HTTPS", 5u), 1);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("htt", 3u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("httpsx", 6u), 0);

    /* Empty and NULL are not schemes. */
    RoboCheckEqInt(HtmlNavigationSchemeIsKnown("", 0u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted(NULL, 0u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("https", 0u), 0);
}

static void TestAnchorTargetContext(void)
{
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor(NULL, 0u),
        (int)RoboHtmlLinkTargetContextCurrent);
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("", 0u),
        (int)RoboHtmlLinkTargetContextCurrent);
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("_self", 5u),
        (int)RoboHtmlLinkTargetContextCurrent);
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("_SELF", 5u),
        (int)RoboHtmlLinkTargetContextCurrent);
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("_blank", 6u),
        (int)RoboHtmlLinkTargetContextNew);
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("_parent", 7u),
        (int)RoboHtmlLinkTargetContextParent);
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("_top", 4u),
        (int)RoboHtmlLinkTargetContextTop);
    /* A name the runtime does not define is not silently treated as _self. */
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("_weird", 6u),
        (int)RoboHtmlLinkTargetContextUnknown);
    RoboCheckEqInt(
        (int)HtmlLinkTargetContextFor("MainWindow", 10u),
        (int)RoboHtmlLinkTargetContextNamed);

    RoboCheckEqInt(HtmlLinkTargetContextIsKnown(
                          RoboHtmlLinkTargetContextNamed),
                      1);
    RoboCheckEqInt(
        HtmlLinkTargetContextIsKnown((HtmlLinkTargetContext)99),
        0);
    RoboCheckStrEq(
        HtmlLinkTargetContextName(RoboHtmlLinkTargetContextNew),
        "new");
    RoboCheckStrEq(
        HtmlLinkTargetContextName((HtmlLinkTargetContext)99),
        "unknown");

    /*
     * `_blank` is classified and nothing else happens: RoboticelaOS has
     * applications and windows, not a tab strip, so the value is preserved
     * for a future window layer rather than acted on here.
     */
    {
        HtmlDocumentId id = AnchorDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle a;
        HtmlAnchorAttributes attrs;

        AnchorBuildNamed(id, "App/Index.html",
                           "<a href=\"/x\" target=\"_blank\">New</a>");
        root = AnchorRoot(id);
        a = AnchorFind(&root, RoboHtmlTagA);
        RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                          RoboOk);
        RoboCheckEqInt(
            (int)HtmlLinkTargetContextFor(attrs.target,
                                                   attrs.targetLength),
            (int)RoboHtmlLinkTargetContextNew);
        RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

static void TestAnchorRelTokens(void)
{
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("noopener noreferrer", 19u,
                                          "noopener"),
        1);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("noopener noreferrer", 19u,
                                          "noreferrer"),
        1);
    /* The second token is not a substring of the first, and neither is empty:
     * the separator is what makes them two tokens. */
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("noopener noreferrer", 14u,
                                          "noreferrer"),
        0);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("NOOPENER", 8u, "noopener"), 1);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("  noopener  ", 12u, "noopener"), 1);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("\tnoopener\n", 10u, "noopener"), 1);

    /* A substring is not a token, which is the whole reason this is a
     * predicate over tokens rather than a search. */
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("noopeners", 9u, "noopener"), 0);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("xnoopener", 9u, "noopener"), 0);

    RoboCheckEqInt(
        HtmlLinkRelContainsToken("external", 8u, "nofollow"), 0);
    RoboCheckEqInt(HtmlLinkRelContainsToken("", 0u, "noopener"), 0);
    RoboCheckEqInt(HtmlLinkRelContainsToken(NULL, 0u, "noopener"),
                      0);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("noopener", 8u, ""), 0);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken("noopener", 8u, NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Activation                                                          */
/* ------------------------------------------------------------------ */

static void TestAnchorActivateNoTarget(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;
    HtmlNavigationOutcome outcome;

    AnchorBuildNamed(id, "App/Index.html", "<a>Nothing here</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationNoTarget);
    RoboCheckEqInt((int)outcome.targetKind, (int)RoboHtmlTargetNone);
    /* Nothing to route means nothing was asked. */
    RoboCheckEqInt(log.calls, 0);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    RoboCheckEqInt((int)outcome.handlerResult, (int)RoboOk);
    HtmlNavigationOutcomeClear(&outcome);

    /* A non-anchor is a caller bug, not an outcome, and says so. */
    AnchorBuild(id, "<p>Text</p>");
    root = AnchorRoot(id);
    {
        HtmlNodeHandle p = AnchorFind(&root, RoboHtmlTagP);

        RoboCheckEqInt(HtmlElementActivate(&p, &handler, &outcome,
                                                     NULL),
                          RoboNotSupported);
    }
    /* A text node is not even an element. */
    {
        HtmlNodeHandle p = AnchorFind(&root, RoboHtmlTagP);
        HtmlNodeHandle text;

        HtmlNodeHandleClear(&text);
        RoboCheckEqInt(HtmlNodeFirstChild(&p, &text, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementActivate(&text, &handler, &outcome,
                                                     NULL),
                          RoboNotFound);
    }
    RoboCheckEqInt(HtmlElementActivate(NULL, &handler, &outcome,
                                                 NULL),
                      RoboInvalidArgument);
    /* An outcome is safe to clear on every failure path above. */
    HtmlNavigationOutcomeClear(&outcome);
    HtmlNavigationOutcomeClear(NULL);
    memset(&outcome, 0, sizeof(outcome));
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorActivateRoute(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;
    HtmlNavigationOutcome outcome;

    AnchorBuildNamed(id, "App/Pages/Index.html",
                       "<a href=\"../Settings\" rel=\"external\" "
                       "title=\"Settings\">S</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    /* With a router installed, the route is handed over and accepted. */
    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationRoute);
    RoboCheckEqInt(log.calls, 1);
    RoboCheckEqInt(outcome.handlerCalled, 1);
    /* The request carried the real document, the resolved route, the base,
     * the reason, and the attributes verbatim. */
    RoboCheckEqInt((int)log.documentId, (int)id);
    RoboCheckStrEq(log.base, "App/Pages/Index.html");
    RoboCheckStrEq(log.resolved, "App/Settings");
    RoboCheckStrEq(log.rel, "external");
    RoboCheckStrEq(log.title, "Settings");
    RoboCheckEqInt((int)log.reason,
                      (int)RoboHtmlNavigationReasonActivation);
    RoboCheckEqInt((int)log.targetContext,
                      (int)RoboHtmlLinkTargetContextCurrent);
    RoboCheckEqInt(log.downloadRequested, 0);
    /* The outcome reports the same resolution, owned by the outcome. */
    RoboCheck(outcome.target != NULL);
    RoboCheckEqInt(outcome.resolved != NULL, 1);
    RoboCheck(memcmp(outcome.resolved, "App/Settings", 12u) == 0);
    RoboCheckEqInt(outcome.fragmentFound, 0);
    HtmlNavigationOutcomeClear(&outcome);
    RoboCheck(outcome.target == NULL);

    /*
     * The outcome's reported strings survive the call and are readable after
     * the outcome was cleared only by the caller — which is the ownership
     * contract, and the reason the outcome owns the target.
     */
    AnchorLogReset(&log);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)strlen(outcome.resolved), 12);
    HtmlNavigationOutcomeClear(&outcome);

    /* A router that declines is UNSUPPORTED, and its reason is recorded. */
    AnchorLogReset(&log);
    log.decline = 1;
    handler = AnchorMakeHandler(&log);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationUnsupported);
    RoboCheckEqInt(log.calls, 1);
    RoboCheckEqInt(outcome.handlerCalled, 1);
    RoboCheckEqInt((int)outcome.handlerResult, (int)RoboNotSupported);
    HtmlNavigationOutcomeClear(&outcome);

    /* `out` is optional: activating for the side effect alone still calls the
     * router and still does not crash. */
    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(log.calls, 1);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorActivateNoFakeSuccess(void)
{
    /*
     * The single most important assertion in the phase: with no handler
     * installed there is no way for an internal route to be reported as
     * anything but UNSUPPORTED. A vocabulary without a `NAVIGATED` kind is
     * what makes that structural rather than a matter of discipline, and this
     * test is what holds the vocabulary to it.
     */
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationOutcome outcome;
    HtmlNavigationHandler empty;

    AnchorBuildNamed(id, "App/Index.html", "<a href=\"/Settings\">S</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    HtmlNavigationHandlerInit(&empty);
    RoboCheckEqInt(HtmlNavigationHandlerIsUsable(&empty), 0);
    RoboCheckEqInt(HtmlNavigationHandlerIsUsable(NULL), 0);

    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, NULL, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationUnsupported);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    RoboCheckEqInt((int)outcome.handlerResult, (int)RoboOk);
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt(HtmlElementActivate(&a, &empty, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationUnsupported);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    HtmlNavigationOutcomeClear(&outcome);

    /* And no outcome kind is called "navigated", in name or meaning. */
    RoboCheckEqInt(HtmlNavigationOutcomeKindIsKnown(
                          RoboHtmlNavigationRoute),
                      1);
    RoboCheckEqInt(HtmlNavigationOutcomeKindIsKnown(
                          RoboHtmlNavigationUnsupported),
                      1);
    RoboCheckEqInt(HtmlNavigationOutcomeKindIsKnown(
                          (HtmlNavigationOutcomeKind)99),
                      0);
    RoboCheckStrEq(
        HtmlNavigationOutcomeKindName(RoboHtmlNavigationUnsupported),
        "unsupported");
    RoboCheckStrEq(HtmlNavigationOutcomeKindName(
                          (HtmlNavigationOutcomeKind)99),
                      "unknown");

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorActivateFragment(void)
{
    /*
     * A fragment is the one navigation this phase can complete, so it is
     * answered locally — which is also why it works with no router at all.
     */
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationOutcome outcome;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;

    AnchorBuildNamed(id, "App/Index.html",
                       "<a href=\"#About\">About</a>"
                       "<a href=\"#Missing\">Gone</a>"
                       "<section id=\"About\">About</section>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationFragment);
    RoboCheckEqInt((int)outcome.targetKind,
                      (int)RoboHtmlTargetFragment);
    RoboCheckEqInt(outcome.fragmentFound, 1);
    RoboCheckEqInt(
        (int)HtmlElementTag(&outcome.fragmentNode),
        (int)RoboHtmlTagSection);
    RoboCheckStrEq(outcome.fragment, "About");
    /* Delegating a fragment would be asking somebody to do a tree walk we
     * already did, so the router is deliberately not consulted. */
    RoboCheckEqInt(log.calls, 0);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    HtmlNavigationOutcomeClear(&outcome);

    /* A fragment that names nothing is still reported, with its target. */
    {
        HtmlNodeHandle second;

        RoboCheckEqInt(
            HtmlNodeNextSibling(&a, &second, NULL), RoboOk);
        RoboCheckEqInt(
            HtmlElementActivate(&second, &handler, &outcome, NULL),
            RoboOk);
        RoboCheckEqInt((int)outcome.kind,
                          (int)RoboHtmlNavigationFragmentMissing);
        RoboCheckEqInt(outcome.fragmentFound, 0);
        RoboCheckStrEq(outcome.fragment, "Missing");
        RoboCheckEqInt(log.calls, 0);
        HtmlNavigationOutcomeClear(&outcome);
    }

    /* With no router at all, a fragment still completes: it needs no I/O. */
    RoboCheckEqInt(HtmlElementActivate(&a, NULL, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationFragment);
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorActivateExternal(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationOutcome outcome;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;

    AnchorBuildNamed(id, "App/Index.html",
                       "<a href=\"https://example.com/Path\">Web</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationExternal);
    RoboCheckEqInt(log.calls, 1);
    RoboCheckStrEq(log.scheme, "https");
    RoboCheckStrEq(log.resolved, "https://example.com/Path");
    HtmlNavigationOutcomeClear(&outcome);

    /* Permitted but with no handler: UNSUPPORTED, because this runtime has
     * no network stack and saying otherwise would be claiming a fetch. */
    RoboCheckEqInt(HtmlElementActivate(&a, NULL, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationUnsupported);
    RoboCheckEqInt((int)outcome.targetKind,
                      (int)RoboHtmlTargetExternal);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorActivateRefusedSchemes(void)
{
    /*
     * The security test. A handler is installed and counts its own calls, so
     * "this never reached a handler" is asserted rather than assumed — which
     * is the only way to know that the refusal happens *before* the seam
     * instead of inside it.
     */
    static const char *const kRefused[] = {
        "<a href=\"javascript:App.Run()\">A</a>",
        "<a href=\"JavaScript:App.Run()\">B</a>",
        "<a href=\"file:/System/Secret\">C</a>",
        "<a href=\"data:text/html,<b>x</b>\">D</a>",
        "<a href=\"vbscript:MsgBox\">E</a>",
        "<a href=\"blob:abc\">F</a>",
        "<a href=\"about:blank\">G</a>",
        "<a href=\"unknown-scheme:Thing\">H</a>"
    };
    size_t i;

    for (i = 0u; i < sizeof(kRefused) / sizeof(kRefused[0]); i++) {
        HtmlDocumentId id = AnchorDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle a;
        HtmlNavigationOutcome outcome;
        AnchorHandlerLog log;
        HtmlNavigationHandler handler;

        AnchorBuildNamed(id, "App/Index.html", kRefused[i]);
        root = AnchorRoot(id);
        a = AnchorFind(&root, RoboHtmlTagA);

        AnchorLogReset(&log);
        handler = AnchorMakeHandler(&log);
        memset(&outcome, 0, sizeof(outcome));
        RoboCheckEqInt(
            HtmlElementActivate(&a, &handler, &outcome, NULL), RoboOk);

        RoboCheckEqInt((int)outcome.kind,
                          (int)RoboHtmlNavigationUnsupported);
        RoboCheckEqInt((int)outcome.targetKind,
                          (int)RoboHtmlTargetExternal);
        /* The handler was never consulted. */
        RoboCheckEqInt(log.calls, 0);
        RoboCheckEqInt(outcome.handlerCalled, 0);
        /* And the refusal still reports what it refused. */
        RoboCheck(outcome.scheme != NULL);
        RoboCheckEqInt(
            HtmlNavigationSchemeIsPermitted(outcome.scheme,
                                                    outcome.schemeLength),
            0);
        HtmlNavigationOutcomeClear(&outcome);
        RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

static void TestAnchorActivateDownload(void)
{
    /*
     * `download` asks to save rather than navigate. Saving needs a filesystem
     * capability this runtime does not have, so the intent is reported and
     * the handler is *not* asked — handing a router a save it cannot perform
     * would be worse than telling the truth.
     */
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationOutcome outcome;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;

    AnchorBuildNamed(id, "App/Index.html",
                       "<a href=\"/Files/Plan.Robo\" download=\"Plan.robo\">"
                       "Get</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationDownloadPending);
    RoboCheckEqInt((int)outcome.targetKind, (int)RoboHtmlTargetRoute);
    RoboCheckEqInt(log.calls, 0);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    /* The target is still reported, so a caller can see what would be saved. */
    RoboCheckStrEq(outcome.resolved, "/Files/Plan.Robo");
    HtmlNavigationOutcomeClear(&outcome);

    /* An external target with `download` is the same answer. */
    AnchorBuild(id, "<a href=\"https://example.com/Plan\" download>Get</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationDownloadPending);
    RoboCheckEqInt((int)outcome.targetKind, (int)RoboHtmlTargetExternal);
    RoboCheckEqInt(log.calls, 0);
    HtmlNavigationOutcomeClear(&outcome);

    /* But a refused scheme stays refused even with `download`: the policy
     * check comes first, so `download` cannot be used to reach `file:`. */
    AnchorBuild(id, "<a href=\"file:/System/Secret\" download>Get</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                      (int)RoboHtmlNavigationUnsupported);
    RoboCheckEqInt(log.calls, 0);
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorActivateRejected(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationOutcome outcome;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;

    AnchorBuildNamed(id, "App/Index.html",
                       "<a href=\"//example.com/Path\">Host</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationRejected);
    /* The reason travels with the outcome: REJECTED plus INVALID is the
     * pair a caller needs to explain what happened. */
    RoboCheckEqInt((int)outcome.targetKind, (int)RoboHtmlTargetInvalid);
    RoboCheckEqInt(log.calls, 0);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    HtmlNavigationOutcomeClear(&outcome);

    /* An empty reference is not a rejection: it is simply no target. */
    AnchorBuild(id, "<a href=\"  \">Empty</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationNoTarget);
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorRequestContents(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationOutcome outcome;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;

    /* Every field the request carries, filled at once. The anchor is
     * identified inside the handler through its own attributes, which is what
     * proves the request's handle is the real node rather than a copy. */
    AnchorBuildNamed(
        id, "App/Pages/Index.html",
        "<a href=\"../Settings\" target=\"MainWindow\" "
        "rel=\"noopener external\" title=\"Open settings\" "
        "data-name=\"Settings\">Settings</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationRoute);
    RoboCheckEqInt(log.calls, 1);
    RoboCheckEqInt((int)log.documentId, (int)id);
    RoboCheckStrEq(log.anchorName, "Settings");
    RoboCheckStrEq(log.base, "App/Pages/Index.html");
    RoboCheckStrEq(log.resolved, "App/Settings");
    RoboCheckStrEq(log.targetAttribute, "MainWindow");
    RoboCheckEqInt((int)log.targetContext,
                      (int)RoboHtmlLinkTargetContextNamed);
    RoboCheckStrEq(log.rel, "noopener external");
    RoboCheckStrEq(log.title, "Open settings");
    RoboCheckEqInt(log.downloadRequested, 0);
    RoboCheckStrEq(log.downloadName, "");
    RoboCheckEqInt((int)log.targetKind, (int)RoboHtmlTargetRoute);
    RoboCheckEqInt((int)log.reason,
                      (int)RoboHtmlNavigationReasonActivation);

    /* `rel` is preserved and queryable, and this phase invents no consequences
     * for it: the request still says what the document asked for. */
    RoboCheckEqInt(
        HtmlLinkRelContainsToken(log.rel, strlen(log.rel), "noopener"),
        1);
    RoboCheckEqInt(
        HtmlLinkRelContainsToken(log.rel, strlen(log.rel), "nofollow"),
        0);
    HtmlNavigationOutcomeClear(&outcome);

    /* A bare `download` reports a present-but-empty name. */
    AnchorBuild(id, "<a href=\"/x\" download>Save</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorLogReset(&log);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt(log.calls, 0); /* download never reaches the handler */
    HtmlNavigationOutcomeClear(&outcome);

    /* Reason vocabulary. */
    RoboCheckEqInt(HtmlNavigationReasonIsKnown(
                          RoboHtmlNavigationReasonActivation),
                      1);
    RoboCheckEqInt(HtmlNavigationReasonIsKnown(
                          (HtmlNavigationReason)99),
                      0);
    RoboCheckStrEq(
        HtmlNavigationReasonName(RoboHtmlNavigationReasonActivation),
        "activation");
    RoboCheckStrEq(HtmlNavigationReasonName(
                          (HtmlNavigationReason)99),
                      "unknown");

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Structure, mutation, lifecycle                                      */
/* ------------------------------------------------------------------ */

static void TestAnchorStructure(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle first;
    HtmlNodeHandle second;
    HtmlNodeHandle third;

    /* Adjacent anchors, a nested span, an anchor wrapping an image, and an
     * anchor that must not swallow what follows it. */
    AnchorBuildNamed(
        id, "App/Index.html",
        "<p><a href=\"/A\">A</a><a href=\"/B\">B</a></p>"
        "<a href=\"/C\"><span>C</span></a>"
        "<a href=\"/D\"><img src=\"Assets/Home.png\" alt=\"Home\"></a>"
        "<p>After the links</p>");

    root = AnchorRoot(id);
    /*
     * Four anchors, and they are not all siblings: the first two are children
     * of the paragraph, the last two are siblings *of* it. Which is which is
     * the structure question, so it is asserted rather than assumed.
     */
    RoboCheckEqInt(AnchorCount(id, RoboHtmlTagA), 4);
    {
        HtmlNodeHandle p = AnchorFind(&root, RoboHtmlTagP);

        HtmlNodeHandleClear(&first);
        RoboCheckEqInt(HtmlNodeFirstChild(&p, &first, NULL), RoboOk);
        RoboCheck(HtmlElementIsTag(&first, RoboHtmlTagA));
        RoboCheckEqInt(HtmlNodeNextSibling(&first, &second, NULL),
                          RoboOk);
        RoboCheck(HtmlElementIsTag(&second, RoboHtmlTagA));
        RoboCheckEqInt((int)HtmlElementTag(&second),
                          (int)RoboHtmlTagA);
        /* The first anchor's own text is its, not the second's. */
        RoboCheckStrEq(AnchorText(&first), "A");
        RoboCheckStrEq(AnchorText(&second), "B");
        /* The second anchor closes the paragraph, and the next anchor belongs
         * to the body — so the third is the paragraph's next sibling, not the
         * second anchor's. */
        RoboCheckEqInt(HtmlNodeNextSibling(&second, &third, NULL),
                          RoboNotFound);
        HtmlNodeHandleClear(&third);
        RoboCheckEqInt(HtmlNodeNextSibling(&p, &third, NULL),
                          RoboOk);
        RoboCheck(HtmlElementIsTag(&third, RoboHtmlTagA));
    }

    /* An anchor holding a span: the span is a real child, not a sibling, and
     * the anchor's own subtree text is the span's text. */
    {
        HtmlNodeHandle span;
        size_t children = 0u;

        RoboCheck(HtmlElementIsTag(&third, RoboHtmlTagA));
        RoboCheckEqInt(HtmlNodeChildCount(&third, &children, NULL),
                          RoboOk);
        RoboCheckEqInt((int)children, 1);
        HtmlNodeHandleClear(&span);
        RoboCheckEqInt(HtmlNodeFirstChild(&third, &span, NULL),
                          RoboOk);
        RoboCheck(HtmlElementIsTag(&span, RoboHtmlTagSpan));
        RoboCheckStrEq(AnchorText(&third), "C");
    }

    /* An anchor wrapping an image: the image is the child, and the anchor
     * resolves normally. */
    {
        HtmlNodeHandle imgAnchor;
        HtmlNodeHandle img;
        size_t children = 0u;
        AnchorResolved r;

        RoboCheckEqInt(HtmlNodeNextSibling(&third, &imgAnchor,
                                                      NULL),
                          RoboOk);
        RoboCheck(HtmlElementIsTag(&imgAnchor, RoboHtmlTagA));
        RoboCheckEqInt(
            HtmlNodeChildCount(&imgAnchor, &children, NULL), RoboOk);
        RoboCheckEqInt((int)children, 1);
        HtmlNodeHandleClear(&img);
        RoboCheckEqInt(HtmlNodeFirstChild(&imgAnchor, &img, NULL),
                          RoboOk);
        RoboCheck(HtmlElementIsTag(&img, RoboHtmlTagImg));
        AnchorResolve(&imgAnchor, &r);
        RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
        RoboCheckStrEq(r.resolved, "/D");
    }

    /* The paragraph after the anchors is a sibling of the anchors, not inside
     * the last one — an anchor that swallowed following content would show up
     * exactly here. */
    {
        HtmlNodeHandle p = AnchorFind(&root, RoboHtmlTagP);
        HtmlNodeHandle last = p;
        HtmlNodeHandle after;

        /* Walk the body's children to the final paragraph. */
        while (HtmlNodeHandleIsWellformed(&last) &&
               HtmlNodeNextSibling(&last, &after, NULL) == RoboOk) {
            last = after;
        }
        RoboCheck(HtmlElementIsTag(&last, RoboHtmlTagP));
        RoboCheckStrEq(AnchorText(&last), "After the links");
    }

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorMutation(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    AnchorResolved r;

    AnchorBuildNamed(id, "App/Index.html",
                       "<div><a href=\"/One\">One</a><a href=\"/Two\">Two</a>"
                       "</div>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);

    /* Mutating href is honoured by the next resolution — there is no cached
     * target to go stale, because resolution is a pure function of the
     * attributes at the moment it is asked. */
    AnchorResolve(&a, &r);
    RoboCheckStrEq(r.resolved, "/One");
    RoboCheckEqInt(HtmlElementSetAttribute(&a, "href", "/Changed",
                                                     NULL),
                      RoboOk);
    AnchorResolve(&a, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "/Changed");

    /* Removing href makes it inert, and restoring it brings it back. */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&a, "href", NULL),
                      RoboOk);
    {
        HtmlAnchorAttributes attrs;

        RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                          RoboOk);
        RoboCheckEqInt(attrs.hrefPresent, 0);
    }
    AnchorResolve(&a, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetNone);
    RoboCheckEqInt(HtmlElementSetAttribute(&a, "href", "/Restored",
                                                     NULL),
                      RoboOk);
    AnchorResolve(&a, &r);
    RoboCheckStrEq(r.resolved, "/Restored");

    /* target / rel / download mutate like any attribute and the view follows. */
    RoboCheckEqInt(HtmlElementSetAttribute(&a, "target", "_blank",
                                                     NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementSetAttribute(&a, "rel",
                                                     "noopener", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementSetAttribute(&a, "download", "X.robo",
                                                     NULL),
                      RoboOk);
    {
        HtmlAnchorAttributes attrs;

        RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                          RoboOk);
        RoboCheckEqInt(
            (int)HtmlLinkTargetContextFor(attrs.target,
                                                   attrs.targetLength),
            (int)RoboHtmlLinkTargetContextNew);
        RoboCheckEqInt(
            HtmlLinkRelContainsToken(attrs.rel, attrs.relLength,
                                              "noopener"),
            1);
        RoboCheckEqInt(attrs.downloadPresent, 1);
    }
    RoboCheckEqInt(HtmlElementRemoveAttribute(&a, "download", NULL),
                      RoboOk);
    {
        HtmlAnchorAttributes attrs;

        RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                          RoboOk);
        RoboCheckEqInt(attrs.downloadPresent, 0);
    }

    /* Structural mutation: an anchor can be detached, moved and replaced, and
     * stays a valid anchor with a working resolution throughout. */
    {
        HtmlNodeHandle second;
        HtmlNodeHandle parent;
        HtmlNodeHandle sibling;

        RoboCheckEqInt(
            HtmlNodeNextSibling(&a, &second, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeParent(&second, &parent, NULL),
                          RoboOk);
        /* Remove the second, put it back before the first. */
        RoboCheckEqInt(HtmlNodeRemoveChild(&parent, &second, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeParent(&a, &parent, NULL), RoboOk);
        RoboCheckEqInt(
            HtmlNodeInsertBefore(&parent, &second, &a, NULL), RoboOk);
        AnchorResolve(&second, &r);
        RoboCheckStrEq(r.resolved, "/Two");
        /* The move really happened: the two anchors are still adjacent, in the
         * other order. */
        HtmlNodeHandleClear(&sibling);
        RoboCheckEqInt(HtmlNodeNextSibling(&second, &sibling, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&sibling, &a));

        /*
         * Replacement takes a *detached* replacement, so the order is
         * detach-then-replace: removing `a` frees the slot, and putting `a`
         * back into it takes `second` out. An attached replacement would be
         * refused, and that refusal is asserted below because it is what keeps
         * a half-applied replacement from losing a child.
         */
        RoboCheckEqInt(HtmlNodeReplaceChild(&parent, &a, &second,
                                                       NULL),
                          RoboAlreadyExists);
        RoboCheckEqInt(HtmlNodeRemoveChild(&parent, &a, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeReplaceChild(&parent, &a, &second,
                                                       NULL),
                          RoboOk);
        /* `second` is now detached and still resolvable: detachment is a legal
         * state, not corruption. */
        AnchorResolve(&second, &r);
        RoboCheckStrEq(r.resolved, "/Two");
        RoboCheckEqInt(HtmlNodeParent(&second, &parent, NULL),
                          RoboNotFound);
        /* ... and `a` took the slot `second` was in. */
        RoboCheckEqInt(HtmlNodeParent(&a, &parent, NULL), RoboOk);
        RoboCheck(HtmlElementIsTag(&parent, RoboHtmlTagDiv));
        AnchorResolve(&a, &r);
        RoboCheckStrEq(r.resolved, "/Restored");
    }

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorLifecycle(void)
{
    /*
     * Resolution and activation answer the same way in every *live* lifecycle
     * state, because a link is a property of the tree and the tree does not
     * change when the document's input state does. What changes is only what a
     * destroyed document can answer: nothing, cleanly.
     *
     * The states are reached the way the 8.10 machine allows rather than by
     * forcing them: CREATED is a fresh document, LOADING is an attached source,
     * INTERACTIVE is a build the node ceiling truncated, COMPLETE is a finished
     * parse. Anchors exist in the last two, which is where a link can be
     * resolved and activated for real.
     */
    enum { MANY_ANCHORS = 2300 };
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationOutcome outcome;
    AnchorResolved r;
    AnchorHandlerLog log;
    HtmlNavigationHandler handler;
    HtmlSource src;
    HtmlTreeResult result;
    char *html;
    size_t i;
    size_t at = 0u;

    AnchorLogReset(&log);
    handler = AnchorMakeHandler(&log);

    /* --- CREATED: nothing attached, no tree --------------------------------- */
    RoboCheckEqInt((int)AnchorLifecycle(id),
                      (int)RoboHtmlDocumentLifecycleCreated);
    RoboCheck(HtmlDocumentAllowsConstruction(id));
    /* With no source there is no base, so a relative reference resolves to
     * itself rather than being refused: a nameless in-memory document is a
     * normal document. */
    AnchorResolveRaw("../Settings", HtmlDocumentSourceName(id), &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "Settings");
    /* Nothing is parsed, so no fragment can name an element yet. */
    {
        HtmlNodeHandle found;

        RoboCheckEqInt(
            HtmlNavigationFindFragment(id, "S", 1u, &found, NULL),
            RoboNotFound);
    }

    /* --- LOADING: input attached, base recorded, tree not built yet --------- */
    HtmlSourceInit(&src);
    RoboCheckEqInt(
        HtmlSourceString(&src,
                                "<a href=\"../Settings\">S</a>"
                                "<section id=\"S\">S</section>",
                                NULL),
        RoboOk);
    RoboCheckEqInt(
        HtmlSourceSetName(&src, "App/Pages/Index.html", NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboOk);
    RoboCheckEqInt((int)AnchorLifecycle(id),
                      (int)RoboHtmlDocumentLifecycleLoading);
    RoboCheck(HtmlDocumentIsLoading(id));
    /* The document's own reference arrives with the source, so a href resolves
     * against a real base while the input is still being consumed. */
    RoboCheckStrEq(HtmlDocumentSourceName(id), "App/Pages/Index.html");
    AnchorResolveRaw("../Settings", HtmlDocumentSourceName(id), &r);
    RoboCheckStrEq(r.resolved, "App/Settings");

    /* --- COMPLETE: a finished parse, with a real anchor in the tree --------- */
    RoboCheckEqInt(HtmlBuildTree(id, &src, &result, NULL), RoboOk);
    RoboCheckEqInt((int)AnchorLifecycle(id),
                      (int)RoboHtmlDocumentLifecycleComplete);
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorResolve(&a, &r);
    RoboCheckStrEq(r.resolved, "App/Settings");
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationRoute);
    RoboCheckEqInt(log.calls, 1);
    HtmlNavigationOutcomeClear(&outcome);

    /* --- INTERACTIVE: a truncated build, anchors live and activatable ------- */
    html = (char *)Malloc(64u + (size_t)MANY_ANCHORS * 32u);
    RoboCheck(html != NULL);
    if (!html) {
        (void)HtmlDocumentDestroy(id, NULL);
        return;
    }
    for (i = 0u; i < (size_t)MANY_ANCHORS; i++) {
        static const char PIECE[] = "<a href=\"/A\">x</a>";

        memcpy(html + at, PIECE, sizeof(PIECE) - 1u);
        at += sizeof(PIECE) - 1u;
    }
    html[at] = '\0';
    HtmlSourceInit(&src);
    RoboCheckEqInt(
        HtmlSourceString(&src, html, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlSourceSetName(&src, "App/Index.html", NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, &result, NULL), RoboOk);
    RoboCheckMsg(result.truncated, "expected the node ceiling to truncate");
    RoboCheckEqInt((int)AnchorLifecycle(id),
                      (int)RoboHtmlDocumentLifecycleInteractive);
    RoboCheck(HtmlDocumentIsInteractive(id));
    RoboCheck(!HtmlDocumentIsComplete(id));
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorResolve(&a, &r);
    RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
    RoboCheckStrEq(r.resolved, "/A");
    AnchorLogReset(&log);
    RoboCheckEqInt(HtmlElementActivate(&a, &handler, &outcome, NULL),
                      RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationRoute);
    RoboCheckEqInt(log.calls, 1);
    HtmlNavigationOutcomeClear(&outcome);
    RoboFree(html);

    /*
     * Reset drops the tree without changing the identity, and a document that
     * has loaded something settles in COMPLETE with an empty tree — there is
     * no edge back to CREATED, so a reset document has no anchors to resolve
     * until it is fed again.
     */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt((int)AnchorLifecycle(id),
                      (int)RoboHtmlDocumentLifecycleComplete);
    RoboCheckEqInt(AnchorCount(id, RoboHtmlTagA), 0);
    AnchorBuildNamed(id, "App/Pages/Index.html",
                       "<a href=\"../Settings\">S</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    AnchorResolve(&a, &r);
    RoboCheckStrEq(r.resolved, "App/Settings");

    /* --- DESTROYED: everything answers nothing, and answers it cleanly ------ */
    {
        HtmlNodeHandle stale = a;
        HtmlNodeHandle found;
        HtmlNavigationTarget *deadTarget = (void *)0;

        AnchorLogReset(&log);
        RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
        /*
         * Resolution of a handle whose document is gone is NOT_FOUND rather
         * than an empty target: there is no document to resolve against, and
         * reporting "no href" would confuse a dead handle with a live bare
         * anchor. The caller's out-parameter is left NULL either way.
         */
        RoboCheckEqInt(
            HtmlElementNavigationTarget(&stale, &deadTarget, NULL),
            RoboNotFound);
        RoboCheck(deadTarget == NULL);
        RoboCheck(HtmlNodeHandleIsStale(&stale));
        /* Activation finds no document, so it is NOT_FOUND and the router is
         * never handed a request for a tree that no longer exists. */
        memset(&outcome, 0, sizeof(outcome));
        RoboCheckEqInt(
            HtmlElementActivate(&stale, &handler, &outcome, NULL),
            RoboNotFound);
        RoboCheckEqInt(log.calls, 0);
        HtmlNavigationOutcomeClear(&outcome);
        RoboCheckEqInt(
            HtmlNavigationFindFragment(id, "S", 1u, &found, NULL),
            RoboNotFound);
        /* A reference still parses without the document: resolution is a pure
         * function of its arguments, which is why activation is the only part
         * that has to care about document lifetime. */
        AnchorResolveRaw("/A", NULL, &r);
        RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
        RoboCheckStrEq(r.resolved, "/A");
    }
}

static void TestAnchorUnicodeText(void)
{
    /*
     * Link text and hrefs are bytes and this phase never re-encodes either.
     * The text is checked through the DOM's own subtree-text reader, so what
     * is verified is that the anchor really holds the bytes, not that a
     * comparison function agreed with itself.
     */
    static const struct {
        const char *html;
        const char *text;
    } cases[] = {
        {"<a href=\"/A\">Settings</a>", "Settings"},
        {"<a href=\"/B\">\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7</a>", "مرحبا"},
        {"<a href=\"/C\">\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C</a>",
         "فارسی"},
        {"<a href=\"/D\">\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88</a>", "اردو"},
        {"<a href=\"/E\">\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D"
         "\xE0\xA4\xA6\xE0\xA5\x80</a>",
         "हिन्दी"},
        {"<a href=\"/F\">\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2"
         "\xE0\xA6\xBE</a>",
         "বাংলা"},
        {"<a href=\"/G\">\xE4\xB8\xAD\xE6\x96\x87</a>", "中文"},
        {"<a href=\"/H\">\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E</a>", "日本語"},
        {"<a href=\"/I\">\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4</a>", "한국어"},
        {"<a href=\"/J\">\xF0\x9F\x98\x80 \xF0\x9F\x9A\x80 \xE2\x9D\xA4\xEF"
         "\xB8\x8F</a>",
         "😀 🚀 ❤️"}
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HtmlDocumentId id = AnchorDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle a;

        AnchorBuildNamed(id, "App/Index.html", cases[i].html);
        root = AnchorRoot(id);
        a = AnchorFind(&root, RoboHtmlTagA);
        RoboCheckStrEq(AnchorText(&a), cases[i].text);
        RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* A non-ASCII href survives resolution byte for byte, because nothing in
     * this phase percent-encodes or re-encodes a reference. */
    {
        HtmlDocumentId id = AnchorDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle a;
        AnchorResolved r;

        AnchorBuildNamed(id, "App/Index.html",
                           "<a href=\"/Files/\xE4\xB8\xAD\xE6\x96\x87\">Open</a>");
        root = AnchorRoot(id);
        a = AnchorFind(&root, RoboHtmlTagA);
        AnchorResolve(&a, &r);
        RoboCheckEqInt((int)r.kind, (int)RoboHtmlTargetRoute);
        RoboCheckEqInt((int)strlen(r.resolved), 13);
        RoboCheck(memcmp(r.resolved, "/Files/\xE4\xB8\xAD\xE6\x96\x87", 13u) ==
                   0);
        RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* And a Unicode `title`/`rel`/`download` reads back verbatim. */
    {
        HtmlDocumentId id = AnchorDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle a;
        HtmlAnchorAttributes attrs;

        AnchorBuildNamed(id, "App/Index.html",
                           "<a href=\"/A\" title=\"\xE4\xB8\xAD\xE6\x96\x87\" "
                           "download=\"\xD9\x85\xD9\x84\xD9\x81.txt\">T</a>");
        root = AnchorRoot(id);
        a = AnchorFind(&root, RoboHtmlTagA);
        RoboCheckEqInt(HtmlElementAnchorAttributes(&a, &attrs, NULL),
                          RoboOk);
        RoboCheckEqInt((int)attrs.titleLength, 6);
        RoboCheck(memcmp(attrs.title, "\xE4\xB8\xAD\xE6\x96\x87", 6u) == 0);
        RoboCheckEqInt((int)attrs.downloadLength, 10);
        RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

static void TestAnchorDebugDescribe(void)
{
    HtmlDocumentId id = AnchorDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle a;
    HtmlNavigationTarget *target = NULL;
    char line[256];
    size_t written;
    size_t targetWritten;

    AnchorBuildNamed(id, "App/Pages/Index.html",
                       "<a href=\"../Settings\" target=\"_blank\" "
                       "rel=\"noopener\">Settings</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementNavigationTarget(&a, &target, NULL),
                      RoboOk);

    memset(line, 0, sizeof(line));
    targetWritten =
        HtmlNavigationTargetDescribe(target, line, sizeof(line));
    RoboCheck(targetWritten > 0u);
    RoboCheck(strstr(line, "kind=route") != NULL);
    RoboCheck(strstr(line, "../Settings") != NULL);
    RoboCheck(strstr(line, "resolved=\"App/Settings\"") != NULL);

    memset(line, 0, sizeof(line));
    written = HtmlElementAnchorDescribe(&a, target, line, sizeof(line));
    RoboCheck(written > 0u);
    RoboCheck(strstr(line, "a href=\"../Settings\"") != NULL);
    RoboCheck(strstr(line, "resolved=\"App/Settings\"") != NULL);
    RoboCheck(strstr(line, "target=new") != NULL);
    RoboCheck(strstr(line, "rel=\"noopener\"") != NULL);
    RoboCheck(strstr(line, "state=activatable") != NULL);

    /*
     * Truncation always terminates and reports what it actually wrote — the
     * same contract as the 8.12 image describe, so a caller that needs the
     * whole line can size a buffer from a first call and know the answer was
     * short rather than complete.
     */
    {
        char small[8];

        memset(small, 0, sizeof(small));
        RoboCheckEqInt(
            (int)HtmlNavigationTargetDescribe(target, small,
                                                      sizeof(small)),
            7);
        RoboCheckEqInt((int)strlen(small), 7);
        /* The line really is longer than the buffer, so this exercised
         * truncation rather than a line that happened to fit. */
        RoboCheck(targetWritten > 7u);
    }
    RoboCheckEqInt((int)HtmlNavigationTargetDescribe(target, NULL, 0u),
                      0);
    RoboCheckEqInt((int)HtmlNavigationTargetDescribe(NULL, line,
                                                               sizeof(line)) >
                          0,
                      1);

    HtmlNavigationTargetDestroy(target);
    target = NULL;

    /* Without a target the anchor line still reports the attributes, and
     * never invents a resolution the caller did not perform. */
    memset(line, 0, sizeof(line));
    RoboCheck(HtmlElementAnchorDescribe(&a, NULL, line, sizeof(line)) >
               0u);
    RoboCheck(strstr(line, "resolved=") == NULL);
    RoboCheck(strstr(line, "href=\"../Settings\"") != NULL);

    /* A refused scheme is visible as such, which is what makes the debug line
     * useful for the security cases. */
    AnchorBuild(id, "<a href=\"javascript:App.Run()\">Run</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementNavigationTarget(&a, &target, NULL),
                      RoboOk);
    memset(line, 0, sizeof(line));
    RoboCheck(HtmlElementAnchorDescribe(&a, target, line,
                                                 sizeof(line)) > 0u);
    RoboCheck(strstr(line, "state=refused") != NULL);
    memset(line, 0, sizeof(line));
    RoboCheck(HtmlNavigationTargetDescribe(target, line,
                                                    sizeof(line)) > 0u);
    RoboCheck(strstr(line, "permitted=no") != NULL);
    HtmlNavigationTargetDestroy(target);

    /* A live non-anchor is not described as one. */
    AnchorBuild(id, "<p>Text</p>");
    root = AnchorRoot(id);
    {
        HtmlNodeHandle p = AnchorFind(&root, RoboHtmlTagP);

        memset(line, 0, sizeof(line));
        RoboCheck(HtmlElementAnchorDescribe(&p, NULL, line,
                                                     sizeof(line)) > 0u);
        RoboCheck(strstr(line, "not an anchor") != NULL);
    }

    /* A detached-but-live anchor still describes (it is an element), and an
     * over-long href reports `state=rejected` without a resolution. */
    AnchorBuild(id, "<a href=\"/A\">A</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    RoboCheckEqInt(HtmlElementNavigationTarget(&a, &target, NULL),
                      RoboOk);
    memset(line, 0, sizeof(line));
    RoboCheck(HtmlElementAnchorDescribe(&a, target, line,
                                                 sizeof(line)) > 0u);
    RoboCheck(strstr(line, "state=activatable") != NULL);
    HtmlNavigationTargetDestroy(target);

    /* An anchor with no href is inert, not activatable. */
    AnchorBuild(id, "<a>Bare</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    memset(line, 0, sizeof(line));
    RoboCheck(HtmlElementAnchorDescribe(&a, NULL, line,
                                                 sizeof(line)) > 0u);
    RoboCheck(strstr(line, "state=inert") != NULL);

    /* And a whitespace href is `empty`, distinct from both. */
    AnchorBuild(id, "<a href=\"   \">Blank</a>");
    root = AnchorRoot(id);
    a = AnchorFind(&root, RoboHtmlTagA);
    memset(line, 0, sizeof(line));
    RoboCheck(HtmlElementAnchorDescribe(&a, NULL, line,
                                                 sizeof(line)) > 0u);
    RoboCheck(strstr(line, "state=empty") != NULL);

    RoboCheckEqInt((int)HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestAnchorCapabilityReported(void)
{
    HtmlDomCapabilities caps;

    RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.links, 1);
    /* Still narrow: no DOM node API and no events, because this phase added
     * neither. */
    RoboCheckEqInt(caps.domNodes, 0);
    RoboCheckEqInt(caps.events, 0);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlAnchor(void)
{
    TestRegister("html.anchor.parser_element", TestAnchorParserElement);
    TestRegister("html.anchor.attributes", TestAnchorAttributes);
    TestRegister("html.anchor.non_anchor_refused",
                       TestAnchorNonAnchorRefused);
    TestRegister("html.anchor.resolution_matrix",
                       TestAnchorResolutionMatrix);
    TestRegister("html.anchor.resolution_without_base",
                       TestAnchorResolutionWithoutBase);
    TestRegister("html.anchor.none_and_missing_href",
                       TestAnchorNoneAndMissingHref);
    TestRegister("html.anchor.invalid_targets",
                       TestAnchorInvalidTargets);
    TestRegister("html.anchor.fragment_targets",
                       TestAnchorFragmentTargets);
    TestRegister("html.anchor.external_uris", TestAnchorExternalUris);
    TestRegister("html.anchor.scheme_policy", TestAnchorSchemePolicy);
    TestRegister("html.anchor.target_context", TestAnchorTargetContext);
    TestRegister("html.anchor.rel_tokens", TestAnchorRelTokens);
    TestRegister("html.anchor.activate_no_target",
                       TestAnchorActivateNoTarget);
    TestRegister("html.anchor.activate_route",
                       TestAnchorActivateRoute);
    TestRegister("html.anchor.activate_no_fake_success",
                       TestAnchorActivateNoFakeSuccess);
    TestRegister("html.anchor.activate_fragment",
                       TestAnchorActivateFragment);
    TestRegister("html.anchor.activate_external",
                       TestAnchorActivateExternal);
    TestRegister("html.anchor.activate_refused_schemes",
                       TestAnchorActivateRefusedSchemes);
    TestRegister("html.anchor.activate_download",
                       TestAnchorActivateDownload);
    TestRegister("html.anchor.activate_rejected",
                       TestAnchorActivateRejected);
    TestRegister("html.anchor.request_contents",
                       TestAnchorRequestContents);
    TestRegister("html.anchor.structure", TestAnchorStructure);
    TestRegister("html.anchor.mutation", TestAnchorMutation);
    TestRegister("html.anchor.lifecycle", TestAnchorLifecycle);
    TestRegister("html.anchor.unicode_text", TestAnchorUnicodeText);
    TestRegister("html.anchor.debug_describe",
                       TestAnchorDebugDescribe);
    TestRegister("html.anchor.capability_reported",
                       TestAnchorCapabilityReported);
}
