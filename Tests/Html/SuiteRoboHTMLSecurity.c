/*
 * RoboHTML Phase 17.10 security suite (html.security.*).
 *
 * Most of this phase is an *absence*, and an absence is only a claim if a test
 * can fail on it. So the cases here have one shape: take a door away from a
 * document and prove it refuses, with the code that says "not allowed" rather
 * than "not implemented"; then prove what must keep working (parsing, for one)
 * still works.
 *
 * The counting probes matter as much as the assertions. A refused image load
 * and a refused navigation are proved to have reached *nothing* by counting how
 * many times the seam was consulted: a refusal that had already asked a
 * resolver for bytes, or a router where to go, would be a refusal of the wrong
 * thing.
 */

#include "RoboBase/RoboBase.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLCustomElements.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLImageLoad.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNavigation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSecurity.h"
#include "RoboHTML/RoboHTMLShadow.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTemplate.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static HtmlDocumentId SecLiveDocument = RoboHtmlDocumentIdNone;

static void SecFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static HtmlDocumentId SecDoc(void)
{
    SecFixtureBegin();
    if (SecLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(SecLiveDocument, NULL), RoboOk);
        SecLiveDocument = RoboHtmlDocumentIdNone;
    }
    RoboCheckEqInt(HtmlDocumentCreate(&SecLiveDocument, NULL), RoboOk);
    RoboCheck(SecLiveDocument != RoboHtmlDocumentIdNone);
    return SecLiveDocument;
}

static void SecDocEnd(void)
{
    if (SecLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(SecLiveDocument, NULL), RoboOk);
        SecLiveDocument = RoboHtmlDocumentIdNone;
    }
}

static void SecBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle SecRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct secTagQuery {
    HtmlTag tag;
} SecTagQuery;

static int SecPredicateTag(const HtmlNodeView *view, void *user)
{
    const SecTagQuery *q = (const SecTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

static int SecTryFind(const HtmlNodeHandle *start, HtmlTag tag,
                      HtmlNodeHandle *out)
{
    SecTagQuery q;
    Result r;

    q.tag = tag;
    HtmlNodeHandleClear(out);
    r = HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                          SecPredicateTag, &q, out, NULL);
    if (r == RoboNotFound) {
        return 0;
    }
    RoboCheckEqInt(r, RoboOk);
    return HtmlNodeHandleIsWellformed(out) ? 1 : 0;
}

static HtmlNodeHandle SecFind(const HtmlNodeHandle *start, HtmlTag tag)
{
    HtmlNodeHandle out;

    RoboCheck(SecTryFind(start, tag, &out));
    return out;
}

static size_t SecChildCount(const HtmlNodeHandle *node)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(node, &count, NULL), RoboOk);
    return count;
}

/** Set @p id's capability set explicitly, every field named. */
static void SecSetCaps(HtmlDocumentId id, int domMutation,
                       int resourceLoading, int navigation,
                       int customElements, int shadowDom)
{
    HtmlDocumentCapabilities caps;

    caps.domMutation = domMutation;
    caps.resourceLoading = resourceLoading;
    caps.navigation = navigation;
    caps.customElements = customElements;
    caps.shadowDom = shadowDom;
    RoboCheckEqInt(HtmlDocumentSetCapabilities(id, &caps, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Posture                                                             */
/* ------------------------------------------------------------------ */

static void TestSecurityPosture(void)
{
    HtmlSecurityPosture posture;

    memset(&posture, 0xff, sizeof(posture));
    RoboCheckEqInt(HtmlSecurityGetPosture(&posture), RoboOk);
    /* The statement the phase exists to make, field by field. */
    RoboCheckEqInt(posture.filesystem, 0);
    RoboCheckEqInt(posture.network, 0);
    RoboCheckEqInt(posture.nativePointers, 0);
    RoboCheckEqInt(posture.processes, 0);
    RoboCheckEqInt(posture.hardware, 0);
    RoboCheckEqInt(posture.hostEnvironment, 0);
    RoboCheckEqInt(posture.scriptExecution, 0);
    RoboCheckEqInt(posture.eventListeners, 0);
    /* ... and how it gets what it does need without any of them. */
    RoboCheckEqInt(posture.bytesOnlyThroughSeam, 1);
    RoboCheckEqInt(posture.hostOwnedDocument, 1);
    RoboCheckStrEq(posture.version, RoboHtmlVersionString);

    RoboCheckEqInt(HtmlSecurityGetPosture(NULL), RoboInvalidArgument);

    /* Every capability is a known enumerator, and the count is the field
     * count of the record — so a table can be indexed by the enumerator. */
    RoboCheckEqInt(RoboHtmlCapabilityCount, 5);
    RoboCheckEqInt(HtmlSecurityCapabilityIsKnown(
                       RoboHtmlCapabilityDomMutation), 1);
    RoboCheckEqInt(HtmlSecurityCapabilityIsKnown(
                       RoboHtmlCapabilityShadowDom), 1);
    RoboCheckEqInt(HtmlSecurityCapabilityIsKnown(
                       (HtmlSecurityCapability)RoboHtmlCapabilityCount), 0);
    RoboCheckStrEq(HtmlSecurityCapabilityName(
                       RoboHtmlCapabilityDomMutation), "dom-mutation");
    RoboCheckStrEq(HtmlSecurityCapabilityName(
                       RoboHtmlCapabilityResourceLoading), "resource-loading");
    RoboCheckStrEq(HtmlSecurityCapabilityName(
                       RoboHtmlCapabilityNavigation), "navigation");
    RoboCheckStrEq(HtmlSecurityCapabilityName(
                       RoboHtmlCapabilityCustomElements), "custom-elements");
    RoboCheckStrEq(HtmlSecurityCapabilityName(
                       RoboHtmlCapabilityShadowDom), "shadow-dom");
    RoboCheckStrEq(HtmlSecurityCapabilityName(
                       (HtmlSecurityCapability)99), "unknown");
}

/* ------------------------------------------------------------------ */
/* The capability record                                               */
/* ------------------------------------------------------------------ */

static void TestSecurityCapabilities(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlDocumentCapabilities caps;
    HtmlDocumentCapabilities readback;

    /* A document that never set one is fully capable: an app rendering its
     * own markup is not attacking itself. */
    memset(&caps, 0, sizeof(caps));
    HtmlDocumentCapabilitiesDefault(&caps);
    RoboCheckEqInt(caps.domMutation, 1);
    RoboCheckEqInt(caps.resourceLoading, 1);
    RoboCheckEqInt(caps.navigation, 1);
    RoboCheckEqInt(caps.customElements, 1);
    RoboCheckEqInt(caps.shadowDom, 1);
    HtmlDocumentCapabilitiesDefault(NULL); /* must not fault */

    memset(&readback, 0, sizeof(readback));
    RoboCheckEqInt(HtmlDocumentGetCapabilities(id, &readback, NULL), RoboOk);
    RoboCheckEqInt(readback.domMutation, 1);
    RoboCheckEqInt(readback.shadowDom, 1);
    RoboCheckEqInt(HtmlDocumentCapabilityAllowed(
                       id, RoboHtmlCapabilityNavigation), 1);

    /* Narrowing is read back exactly. */
    SecSetCaps(id, 0, 1, 0, 1, 0);
    RoboCheckEqInt(HtmlDocumentGetCapabilities(id, &readback, NULL), RoboOk);
    RoboCheckEqInt(readback.domMutation, 0);
    RoboCheckEqInt(readback.resourceLoading, 1);
    RoboCheckEqInt(readback.navigation, 0);
    RoboCheckEqInt(readback.customElements, 1);
    RoboCheckEqInt(readback.shadowDom, 0);
    RoboCheckEqInt(HtmlDocumentCapabilityAllowed(
                       id, RoboHtmlCapabilityNavigation), 0);

    /* A field that is neither 0 nor 1 is a caller mistake, not a truthy
     * grant — and the refusal changes nothing. */
    {
        HtmlDocumentCapabilities bad = readback;

        bad.resourceLoading = 2;
        RoboCheckEqInt(HtmlDocumentSetCapabilities(id, &bad, NULL),
                       RoboInvalidArgument);
        RoboCheckEqInt(HtmlDocumentGetCapabilities(id, &readback, NULL),
                       RoboOk);
        RoboCheckEqInt(readback.resourceLoading, 1);
        bad.resourceLoading = 1;
        bad.shadowDom = -1;
        RoboCheckEqInt(HtmlDocumentSetCapabilities(id, &bad, NULL),
                       RoboInvalidArgument);
        RoboCheckEqInt(HtmlDocumentGetCapabilities(id, &readback, NULL),
                       RoboOk);
        RoboCheckEqInt(readback.shadowDom, 0);
    }

    /* A destroyed document grants nothing, and answers NotFound. */
    {
        HtmlDocumentId dead = RoboHtmlDocumentIdNone;

        RoboCheckEqInt(HtmlDocumentCreate(&dead, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentDestroy(dead, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentGetCapabilities(dead, &readback, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentSetCapabilities(dead, &caps, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentCapabilityAllowed(
                           dead, RoboHtmlCapabilityDomMutation), 0);
    }
    /* An unknown enumerator grants nothing even for a live, fully capable
     * document — the safe direction for a predicate. */
    RoboCheckEqInt(HtmlDocumentCapabilityAllowed(
                       id, (HtmlSecurityCapability)99), 0);

    RoboCheckEqInt(HtmlDocumentGetCapabilities(id, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentSetCapabilities(id, NULL, NULL),
                   RoboInvalidArgument);

    SecDocEnd();
}

/* ------------------------------------------------------------------ */
/* DOM mutation                                                        */
/* ------------------------------------------------------------------ */

static void TestSecurityMutationGate(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle created;
    HtmlNodeHandle fragment;
    HtmlNodeHandle host;
    size_t count = 9u;

    /*
     * Parsing happens *before* the capability is narrowed — and, more to the
     * point, a parse links nodes through the construction path rather than the
     * mutation one, so it is unaffected either way. This is the property that
     * makes the capability usable on untrusted markup at all.
     */
    SecSetCaps(id, 0, 1, 1, 1, 1);
    SecBuild(id, "<body><div id=\"host\"><p id=\"one\">One</p></div>"
                    "<a href=\"/settings\">Settings</a></body>");
    root = SecRoot(id);
    body = SecFind(&root, RoboHtmlTagBody);
    RoboCheckEqInt((int)SecChildCount(&body), 2);
    host = SecFind(&root, RoboHtmlTagDiv);

    /* Every structural verb refuses, with the code that says "not allowed". */
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "em", &created, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateFragment(id, &fragment, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&host, &created, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlNodePrependChild(&host, &created, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&host, &fragment, &count,
                                                 NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt((int)count, 0); /* reported pending work, not a lie */
    RoboCheckEqInt(HtmlNodeRemoveChildren(&host, &count, NULL),
                   RoboPermissionDenied);
    /* Attribute writes are the same edit surface. */
    RoboCheckEqInt(HtmlElementSetAttribute(&host, "class", "x", NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlElementRemoveAttribute(&host, "id", NULL),
                   RoboPermissionDenied);
    RoboCheck(HtmlElementHasAttribute(&host, "id"));
    RoboCheckEqInt((int)SecChildCount(&host), 1);

    /* Detaching a node is a mutation too. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &host, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt((int)SecChildCount(&body), 2);

    /* Granted again, the same calls work — the gate is a permission, not a
     * state the document can get stuck in. */
    SecSetCaps(id, 1, 1, 1, 1, 1);
    RoboCheckEqInt(HtmlNodeAppendChild(&host, &created, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementSetAttribute(&host, "class", "x", NULL),
                   RoboOk);
    RoboCheck(HtmlElementHasAttribute(&host, "class"));
    RoboCheckEqInt(HtmlElementRemoveAttribute(&host, "class", NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &host, NULL), RoboOk);
    RoboCheckEqInt((int)SecChildCount(&body), 1);

    SecDocEnd();
}

/* ------------------------------------------------------------------ */
/* Resource loading                                                    */
/* ------------------------------------------------------------------ */

typedef struct secCounting {
    const uint8_t *bytes;
    size_t length;
    int calls;
} SecCounting;

static Result SecFetchCounting(void *userdata,
                               const HtmlImageRequest *request,
                               const uint8_t **outBytes,
                               size_t *outLength,
                               HtmlDiag *diag)
{
    SecCounting *state = (SecCounting *)userdata;

    (void)request;
    (void)diag;
    state->calls++;
    *outBytes = state->bytes;
    *outLength = state->length;
    return RoboOk;
}

static GfxSurface *SecSurface(uint32_t width, uint32_t height)
{
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    return surface;
}

static void TestSecurityResourceGate(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle img;
    GfxSurface *surface;
    GfxEncodedBuffer encoded;
    GfxEncodeOptions opts;
    SecCounting counting;
    HtmlImageResolver resolver;
    HtmlImageLoadOutcome outcome;

    SecBuild(id, "<body><img src=\"Assets/Logo.png\"></body>");
    root = SecRoot(id);
    img = SecFind(&root, RoboHtmlTagImg);

    surface = SecSurface(2u, 2u);
    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;
    memset(&encoded, 0, sizeof(encoded));
    RoboCheckEqInt(GfxSurfaceEncode(surface, &opts, &encoded), RoboOk);

    counting.bytes = encoded.data;
    counting.length = encoded.size;
    counting.calls = 0;
    HtmlImageResolverInit(&resolver);
    resolver.fetch = SecFetchCounting;
    resolver.userdata = &counting;

    /* Refused: and the resolver was never consulted, which is the point — a
     * refusal that had already asked for bytes would be a refusal of the
     * wrong thing. */
    SecSetCaps(id, 1, 0, 1, 1, 1);
    RoboCheckEqInt(HtmlElementImageLoad(&img, &resolver, &outcome, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(counting.calls, 0);
    RoboCheckEqInt(outcome.requested, 0);
    RoboCheckEqInt(outcome.loaded, 0);
    /* The element still reads PENDING: no resource, and a usable `src`. */
    RoboCheckEqInt(HtmlElementImageState(&img), RoboHtmlImageStatePending);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 0u);
    RoboCheckEqInt(HtmlElementImageReplaceSource(&img, "Other.png", 9u,
                                                &resolver, &outcome, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(counting.calls, 0);
    /* ... so the `src` was not rewritten either: the refusal changed nothing
     * at all. */
    {
        HtmlImageAttributes attributes;

        RoboCheckEqInt(HtmlElementImageAttributes(&img, &attributes, NULL),
                       RoboOk);
        RoboCheckEqInt((int)attributes.sourceLength, 15);
        RoboCheck(memcmp(attributes.source, "Assets/Logo.png", 15u) == 0);
    }

    /* Granted: the same call goes through, and the bitmap's real size is what
     * the outcome reports. */
    SecSetCaps(id, 1, 1, 1, 1, 1);
    RoboCheckEqInt(HtmlElementImageLoad(&img, &resolver, &outcome, NULL),
                   RoboOk);
    RoboCheckEqInt(counting.calls, 1);
    RoboCheckEqInt(outcome.state, RoboHtmlImageStateDecoded);
    RoboCheckEqInt(outcome.requested, 1);
    RoboCheckEqInt(outcome.loaded, 1);
    RoboCheckEqInt(outcome.hasIntrinsic, 1);
    RoboCheckEqInt((int)outcome.intrinsicWidth, 2);
    RoboCheckEqInt((int)outcome.intrinsicHeight, 2);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 1u);

    GfxEncodedBufferRelease(&encoded);
    GfxSurfaceRelease(surface);
    SecDocEnd();
}

/* ------------------------------------------------------------------ */
/* Navigation                                                          */
/* ------------------------------------------------------------------ */

typedef struct secNav {
    int calls;
} SecNav;

static Result SecNavHandler(void *userdata,
                            const HtmlNavigationRequest *request,
                            HtmlNavigationOutcomeKind *outKind,
                            HtmlDiag *diag)
{
    SecNav *state = (SecNav *)userdata;

    (void)request;
    (void)diag;
    state->calls++;
    *outKind = RoboHtmlNavigationRoute;
    return RoboOk;
}

static void TestSecurityNavigationGate(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle anchor;
    SecNav nav;
    HtmlNavigationHandler handler;
    HtmlNavigationOutcome outcome;

    SecBuild(id, "<body><a href=\"/settings\">Settings</a>"
                    "<p id=\"top\">Top</p></body>");
    root = SecRoot(id);
    anchor = SecFind(&root, RoboHtmlTagA);

    nav.calls = 0;
    HtmlNavigationHandlerInit(&handler);
    handler.navigate = SecNavHandler;
    handler.userdata = &nav;
    RoboCheckEqInt(HtmlDocumentSetNavigationHandler(id, &handler, NULL),
                   RoboOk);
    RoboCheck(HtmlDocumentHasNavigationHandler(id));

    /* Refused before the handler is consulted, so an embedded document cannot
     * even ask the application's router to go somewhere. */
    SecSetCaps(id, 1, 1, 0, 1, 1);
    RoboCheckEqInt(HtmlDocumentActivateLink(&anchor, &outcome, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(nav.calls, 0);
    RoboCheckEqInt(HtmlDocumentNavigateToFragment(id, "top", 3u, &outcome,
                                                 NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(nav.calls, 0);
    RoboCheckEqInt(HtmlDocumentHasNavigationHandler(id), 1);

    /* Granted, the route reaches the handler exactly once. */
    SecSetCaps(id, 1, 1, 1, 1, 1);
    RoboCheckEqInt(HtmlDocumentActivateLink(&anchor, &outcome, NULL), RoboOk);
    RoboCheckEqInt(nav.calls, 1);
    HtmlNavigationOutcomeClear(&outcome);
    RoboCheckEqInt(HtmlDocumentNavigateToFragment(id, "top", 3u, &outcome,
                                                 NULL),
                   RoboOk);
    HtmlNavigationOutcomeClear(&outcome);

    SecDocEnd();
}

/* ------------------------------------------------------------------ */
/* Custom elements and shadow DOM                                      */
/* ------------------------------------------------------------------ */

static void TestSecurityDefinitionAndShadowGates(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlCustomElementDefinition definition;
    HtmlCustomElementCapabilities ceCaps;
    HtmlNodeHandle root;
    HtmlNodeHandle host;
    HtmlNodeHandle shadow;

    SecBuild(id, "<body><my-row>Untrusted</my-row>"
                    "<div id=\"host\"></div></body>");
    root = SecRoot(id);

    /*
     * A name the host refuses to define still parses: the markup is data, and
     * the capability is about giving it a behaviour, not about stopping the
     * parse.
     */
    SecSetCaps(id, 1, 1, 1, 0, 1);
    HtmlCustomElementDefinitionInit(&definition);
    definition.name = "my-row";
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt((int)HtmlDocumentCustomElementCount(id), 0);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-row"), 0);
    {
        /* The element is there, unupgraded — a refusal is not a parse error. */
        HtmlNodeHandle body = SecFind(&root, RoboHtmlTagBody);
        HtmlNodeHandle custom;
        size_t length = 0u;
        const char *name;

        RoboCheckEqInt((int)SecChildCount(&body), 2);
        RoboCheckEqInt(HtmlNodeChildAt(&body, 0u, &custom, NULL), RoboOk);
        name = HtmlElementName(&custom, &length);
        RoboCheck(name != NULL);
        RoboCheckEqInt((int)length, 6);
        RoboCheck(memcmp(name, "my-row", 6u) == 0);
    }

    /* Granted, the definition lands. */
    SecSetCaps(id, 1, 1, 1, 1, 1);
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboOk);
    RoboCheckEqInt((int)HtmlDocumentCustomElementCount(id), 1);
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-row"), 1);

    memset(&ceCaps, 0, sizeof(ceCaps));
    RoboCheckEqInt(HtmlCustomElementGetCapabilities(&ceCaps), RoboOk);
    RoboCheckEqInt(ceCaps.registry, 1);
    RoboCheckEqInt(ceCaps.constructors, 0); /* a stated absence, read back */
    RoboCheckEqInt(ceCaps.scopedRegistries, 0);

    /* Shadow DOM is its own capability. */
    host = SecFind(&root, RoboHtmlTagDiv);
    SecSetCaps(id, 1, 1, 1, 1, 0);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &shadow, NULL),
                   RoboPermissionDenied);
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &shadow, NULL), RoboNotFound);

    SecSetCaps(id, 1, 1, 1, 1, 1);
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &shadow, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&shadow));
    RoboCheckEqInt(HtmlElementShadowRoot(&host, &shadow, NULL), RoboOk);

    SecDocEnd();
}

/* ------------------------------------------------------------------ */
/* Hostile input is data                                               */
/* ------------------------------------------------------------------ */

static void TestSecurityHostileInput(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle anchor;
    HtmlNodeHandle img;
    HtmlImageReferenceClass class = RoboHtmlImageReferenceNone;
    HtmlNavigationTarget *target = NULL;
    size_t length = 0u;
    const char *value;

    SecBuild(id, "<body>"
                    "<a href=\"javascript:alert(1)\">Script</a>"
                    "<div onload=\"evil()\" title=\"t\">Hostile</div>"
                    "<img src=\"file:///etc/passwd\">"
                    "<img src=\"data:image/png;base64,AAAA\">"
                    "</body>");
    root = SecRoot(id);

    /* A `javascript:` href is *data*: stored verbatim, and the scheme policy
     * refuses it before any seam is consulted (Phase 8.13's rule, reused). */
    anchor = SecFind(&root, RoboHtmlTagA);
    value = HtmlElementAttributeValue(&anchor, "href", &length);
    RoboCheck(value != NULL);
    RoboCheckEqInt((int)length, 19);
    RoboCheck(memcmp(value, "javascript:alert(1)", 19u) == 0);
    RoboCheckEqInt(HtmlNavigationTargetCreate("javascript:alert(1)", 18u,
                                             "", 0u, &target, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNavigationTargetKind(target),
                   RoboHtmlTargetExternal);
    HtmlNavigationTargetDestroy(target);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("javascript", 10u), 0);
    RoboCheckEqInt(HtmlNavigationSchemeIsPermitted("http", 4u), 1);

    /* An event-handler attribute is inert data too: the DOM has no listener
     * registry for it to reach, which is what `eventListeners: 0` says. */
    {
        HtmlNodeHandle hostile = SecFind(&root, RoboHtmlTagDiv);

        value = HtmlElementAttributeValue(&hostile, "onload", &length);
        RoboCheck(value != NULL);
        RoboCheckEqInt((int)length, 6);
        RoboCheck(memcmp(value, "evil()", 6u) == 0);
    }

    /* A `file:` image reference is classified as refused and never becomes a
     * fetch, and a `data:` one is refused by the same policy (this runtime
     * does not decode it). */
    img = SecFind(&root, RoboHtmlTagImg);
    RoboCheckEqInt(HtmlElementImageReferenceClass(&img, &class, NULL), RoboOk);
    RoboCheckEqInt(class, RoboHtmlImageReferenceUnsupported);
    {
        HtmlNodeHandle second;

        RoboCheckEqInt(HtmlNodeNextSibling(&img, &second, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementImageReferenceClass(&second, &class, NULL),
                       RoboOk);
        RoboCheckEqInt(class, RoboHtmlImageReferenceUnsupported);
    }

    /* The same policy refuses a `javascript:` image reference (and an empty
     * one is NONE, not INVALID: `alt`-only is a legal element). */
    {
        HtmlImageReferenceClass empty = RoboHtmlImageReferenceInvalid;
        HtmlImageReferenceClass script = RoboHtmlImageReferenceNone;

        RoboCheckEqInt(HtmlImageReferenceClassify("", 0u, "", 0u, &empty,
                                                 NULL), RoboOk);
        RoboCheckEqInt(empty, RoboHtmlImageReferenceNone);
        RoboCheckEqInt(HtmlImageReferenceClassify("javascript:alert(1)", 19u,
                                                 "", 0u, &script, NULL),
                       RoboOk);
        RoboCheckEqInt(script, RoboHtmlImageReferenceUnsupported);
    }

    /* A hostile attribute *name* is refused, and so is a nameless one; a
     * hostile *value* is stored verbatim, because a value is data. */
    {
        HtmlNodeHandle body = SecFind(&root, RoboHtmlTagBody);

        RoboCheckEqInt(HtmlElementSetAttribute(&body, "a b", "1", NULL),
                       RoboParseError);
        RoboCheckEqInt(HtmlElementSetAttribute(&body, "", "1", NULL),
                       RoboInvalidArgument);
        RoboCheckEqInt(HtmlElementSetAttribute(&body, NULL, "1", NULL),
                       RoboInvalidArgument);
        RoboCheckEqInt(HtmlElementSetAttribute(&body, "data-x",
                                              "javascript:alert(2)", NULL),
                       RoboOk);
        value = HtmlElementAttributeValue(&body, "data-x", &length);
        RoboCheck(value != NULL);
        RoboCheckEqInt((int)length, 19);
        RoboCheck(memcmp(value, "javascript:alert(2)", 19u) == 0);
        RoboCheckEqInt(HtmlElementRemoveAttribute(&body, "data-x", NULL),
                       RoboOk);
    }

    SecDocEnd();
}

/* ------------------------------------------------------------------ */
/* Cross-document misuse                                               */
/* ------------------------------------------------------------------ */

static void TestSecurityCrossDocument(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlDocumentId other = RoboHtmlDocumentIdNone;
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle otherRoot;
    HtmlNodeHandle theirs;
    HtmlNodeHandle mine;

    SecBuild(id, "<body></body>");
    root = SecRoot(id);
    body = SecFind(&root, RoboHtmlTagBody);

    RoboCheckEqInt(HtmlDocumentCreate(&other, NULL), RoboOk);
    SecBuild(other, "<body><p id=\"theirs\">Theirs</p></body>");
    otherRoot = SecRoot(other);
    theirs = SecFind(&otherRoot, RoboHtmlTagP);

    /* A node from another document is refused by the mutation that would graft
     * it in, whichever direction the attempt goes — and said with the code
     * that makes it actionable rather than a resolution miss. */
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &theirs, NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlNodeAppendChild(&otherRoot, &body, NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlNodeRemoveChild(&otherRoot, &body, NULL),
                   RoboNotSupported);
    /* A clone lands in the node's own document, so it cannot carry one tree's
     * node into another's. */
    RoboCheckEqInt(HtmlNodeClone(&theirs, 1, &mine, NULL), RoboOk);
    RoboCheckEqInt((int)HtmlNodeHandleDocument(&mine), (int)other);
    RoboCheckEqInt((int)HtmlNodeHandleDocument(&theirs), (int)other);
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &mine, NULL),
                   RoboNotSupported);

    /* A template's content cannot be handed another document's fragment. */
    {
        HtmlNodeHandle template;
        HtmlNodeHandle otherFragment;

        RoboCheckEqInt(HtmlDocumentCreateElement(id, "template", &template,
                                                 NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlDocumentCreateFragment(other, &otherFragment,
                                                  NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementTemplateSetContent(&template,
                                                      &otherFragment, NULL),
                       RoboNotSupported);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(other, NULL), RoboOk);
    SecDocEnd();
}

static void TestSecurityDestroyedDocument(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle created;
    HtmlDocumentCapabilities caps;
    HtmlSecurityReport report;
    HtmlNodeHandle img;
    HtmlImageResolver resolver;
    HtmlImageLoadOutcome outcome;
    HtmlNavigationOutcome navOutcome;

    HtmlDocumentCapabilitiesDefault(&caps);
    SecBuild(id, "<body><img src=\"x.png\"></body>");
    root = SecRoot(id);
    body = SecFind(&root, RoboHtmlTagBody);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "em", &created, NULL),
                   RoboOk);
    img = SecFind(&root, RoboHtmlTagImg);
    HtmlImageResolverInit(&resolver);

    SecDocEnd();

    /* Every gated door answers NotFound for a destroyed document — not
     * PermissionDenied, because there is no document left to have granted
     * anything. */
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &created, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &created, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlElementSetAttribute(&body, "class", "x", NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlElementImageLoad(&img, &resolver, &outcome, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentActivateLink(&body, &navOutcome, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentGetCapabilities(id, &caps, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentSetCapabilities(id, &caps, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentUpgradeCustomElements(id, NULL, NULL),
                   RoboNotFound);
}

/* ------------------------------------------------------------------ */
/* The lifetime report                                                 */
/* ------------------------------------------------------------------ */

static void TestSecurityReport(void)
{
    HtmlDocumentId id = SecDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle host;
    HtmlSecurityReport report;
    size_t detachedBefore;

    SecBuild(id, "<body><div id=\"host\"></div></body>");
    root = SecRoot(id);
    body = SecFind(&root, RoboHtmlTagBody);
    host = SecFind(&root, RoboHtmlTagDiv);

    memset(&report, 0xff, sizeof(report));
    RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL), RoboOk);
    RoboCheckEqInt((int)report.documentId, (int)id);
    RoboCheck(report.reachableNodes >= 3u); /* document, html, body, div */
    detachedBefore = report.detachedRoots;
    RoboCheckEqInt(report.treeIntact, 1);
    RoboCheck(report.treeVersion != 0u);
    RoboCheckEqInt((int)report.imageAttachments, 0);
    RoboCheckEqInt((int)report.customElementDefinitions, 0);
    RoboCheckEqInt(report.capabilities.domMutation, 1);

    /* A detached subtree is reported as such — and is still not corruption,
     * which is the distinction the audit draws. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &host, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL), RoboOk);
    RoboCheckEqInt((int)report.detachedRoots, (int)(detachedBefore + 1u));
    RoboCheckEqInt(report.treeIntact, 1);

    /* The definition count comes from the door that owns it, so it moves with
     * that door and not with this report. */
    {
        HtmlCustomElementDefinition definition;

        HtmlCustomElementDefinitionInit(&definition);
        definition.name = "my-row";
        RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition,
                                                       NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL),
                       RoboOk);
        RoboCheckEqInt((int)report.customElementDefinitions, 1);
    }

    /* The report carries the capability set in force. */
    SecSetCaps(id, 0, 0, 1, 1, 1);
    RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL), RoboOk);
    RoboCheckEqInt(report.capabilities.domMutation, 0);
    RoboCheckEqInt(report.capabilities.resourceLoading, 0);
    RoboCheckEqInt(report.capabilities.navigation, 1);

    RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, NULL, NULL),
                   RoboInvalidArgument);

    SecDocEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlSecurity(void);

void TestRegisterHtmlSecurity(void)
{
    TestRegister("html.security.posture", TestSecurityPosture);
    TestRegister("html.security.capabilities", TestSecurityCapabilities);
    TestRegister("html.security.mutation_gate", TestSecurityMutationGate);
    TestRegister("html.security.resource_gate", TestSecurityResourceGate);
    TestRegister("html.security.navigation_gate",
                 TestSecurityNavigationGate);
    TestRegister("html.security.definition_and_shadow_gates",
                 TestSecurityDefinitionAndShadowGates);
    TestRegister("html.security.hostile_input", TestSecurityHostileInput);
    TestRegister("html.security.cross_document",
                 TestSecurityCrossDocument);
    TestRegister("html.security.destroyed_document",
                 TestSecurityDestroyedDocument);
    TestRegister("html.security.report", TestSecurityReport);
}
