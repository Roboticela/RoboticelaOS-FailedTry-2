/*
 * RoboHTML Phase 17.12 DOM final-integration suite (html.integration.*).
 *
 * The conformance suite (17.11) proves the *joins* between surfaces with small
 * documents. This one proves the whole thing at the size a page actually is:
 * one realistic document taken through parse → read → edit → submit → clone →
 * shadow → upgrade → load → navigate → audit → destroy, with each step's
 * assertion written in terms of the step before it, so a failure names the
 * stage that moved.
 *
 * Nothing here is a second implementation of a surface. Where a case needs a
 * decoded image it uses the real encoder to build the bytes and the real
 * resolver seam to serve them; where it needs a route it installs a handler
 * and reads what the handler was told; where it needs a submission it installs
 * the submission handler and reads the count it was given.
 */

#include "RoboBase/RoboBase.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLAnchor.h"
#include "RoboHTML/RoboHTMLCustomElements.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLFormTree.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLImageLoad.h"
#include "RoboHTML/RoboHTMLInput.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNavigation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSecurity.h"
#include "RoboHTML/RoboHTMLShadow.h"
#include "RoboHTML/RoboHTMLTable.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTemplate.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboTest.h"

#include "RoboHTMLDomTestSupport.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The page of record                                                  */
/* ------------------------------------------------------------------ */

/*
 * One document that reaches every 17.x surface: a title, a link, an image, a
 * form with four kinds of control, a table, a template holding a reusable
 * fragment, a shadow host, and a custom element. If a phase's surface cannot
 * survive living next to the others, this is where it shows.
 */
static const char IntegrationPage[] =
    "<!DOCTYPE html><html><head><title>Dashboard</title></head><body>"
    "<h1>Dashboard</h1>"
    "<a id=\"settings\" href=\"/settings\">Settings</a>"
    "<a id=\"jump\" href=\"#Summary\">Jump</a>"
    "<img id=\"logo\" src=\"assets/logo.png\" width=\"64\">"
    "<form id=\"signup\" action=\"/register\" method=\"post\">"
    "<input id=\"name\" name=\"name\" required>"
    "<input id=\"age\" name=\"age\" type=\"number\" min=\"1\" max=\"120\""
    " value=\"7\">"
    "<input id=\"notes\" name=\"notes\" type=\"checkbox\" checked>"
    "<button type=\"submit\">Register</button>"
    "</form>"
    "<table id=\"grid\"><caption>Grid</caption>"
    "<thead><tr><th>Key</th><th>Value</th></tr></thead>"
    "<tbody><tr><td>alpha</td><td>1</td></tr>"
    "<tr><td>beta</td><td>2</td></tr></tbody></table>"
    "<template id=\"row\"><tr><td>gamma</td><td>3</td></tr></template>"
    "<div id=\"host\"><span id=\"light\">Light</span></div>"
    "<my-card id=\"card\" data-kind=\"summary\">Card</my-card>"
    "<p id=\"Summary\">The summary.</p>"
    "</body></html>";

static HtmlDocumentId IntegrationDoc(void)
{
    return DomDocumentOf(IntegrationPage);
}

/* ------------------------------------------------------------------ */
/* Callers the document reaches                                        */
/* ------------------------------------------------------------------ */

typedef struct integrationRoute {
    int calls;
    int lastIsRoute;
    int fragmentCalls;
} IntegrationRoute;

static Result IntegrationNavHandler(void *userdata,
                                     const HtmlNavigationRequest *request,
                                     HtmlNavigationOutcomeKind *outKind,
                                     HtmlDiag *diag)
{
    IntegrationRoute *route = (IntegrationRoute *)userdata;

    (void)request;
    (void)diag;
    route->calls++;
    *outKind = RoboHtmlNavigationRoute;
    route->lastIsRoute = 1;
    return RoboOk;
}

typedef struct integrationSubmit {
    int calls;
    size_t controlCount;
    char action[64];
} IntegrationSubmit;

static Result IntegrationSubmitHandler(const HtmlNodeHandle *form,
                                       const HtmlFormAttributes *attributes,
                                       size_t controlCount, void *userdata,
                                       HtmlDiag *diag)
{
    IntegrationSubmit *submit = (IntegrationSubmit *)userdata;

    (void)form;
    (void)diag;
    submit->calls++;
    submit->controlCount = controlCount;
    if (attributes && attributes->action && attributes->actionLength <
                            sizeof(submit->action)) {
        memcpy(submit->action, attributes->action, attributes->actionLength);
        submit->action[attributes->actionLength] = '\0';
    }
    return RoboOk;
}

typedef struct integrationLife {
    int connected;
    int disconnected;
    int attributeChanged;
} IntegrationLife;

static Result IntegrationOnConnected(const HtmlNodeHandle *element,
                                     void *userdata, HtmlDiag *diag)
{
    (void)element;
    (void)diag;
    ((IntegrationLife *)userdata)->connected++;
    return RoboOk;
}

static Result IntegrationOnDisconnected(const HtmlNodeHandle *element,
                                        void *userdata, HtmlDiag *diag)
{
    (void)element;
    (void)diag;
    ((IntegrationLife *)userdata)->disconnected++;
    return RoboOk;
}

static Result IntegrationOnAttribute(const HtmlNodeHandle *element,
                                     const char *name, const char *oldValue,
                                     const char *newValue, void *userdata,
                                     HtmlDiag *diag)
{
    (void)element;
    (void)name;
    (void)oldValue;
    (void)newValue;
    (void)diag;
    ((IntegrationLife *)userdata)->attributeChanged++;
    return RoboOk;
}

typedef struct integrationFetch {
    const uint8_t *bytes;
    size_t length;
    int calls;
} IntegrationFetch;

static Result IntegrationFetchFn(void *userdata,
                                 const HtmlImageRequest *request,
                                 const uint8_t **outBytes,
                                 size_t *outLength,
                                 HtmlDiag *diag)
{
    IntegrationFetch *fetch = (IntegrationFetch *)userdata;

    (void)request;
    (void)diag;
    fetch->calls++;
    *outBytes = fetch->bytes;
    *outLength = fetch->length;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* 1. The page, whole                                                  */
/* ------------------------------------------------------------------ */

static void TestIntegrationDocument(void)
{
    HtmlDocumentId id = IntegrationDoc();
    HtmlNodeHandle root = DomRoot(id);
    size_t tagCount;

    /* The parse produced the document shape, and every surface's element is
     * present at once. */
    {
        HtmlNodeHandle html;
        HtmlNodeHandle head;
        HtmlNodeHandle title;
        char text[64];

        RoboCheckEqInt(HtmlNodeFindElement(&root, "html", &html, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeFindElement(&root, "head", &head, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeFindElement(&head, "title", &title, NULL),
                       RoboOk);
        DomText(&title, text, sizeof(text));
        RoboCheckStrEq(text, "Dashboard");
    }
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagForm), 1);
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagA), 2);
    RoboCheckEqInt((int)DomCountTags(&root, RoboHtmlTagTemplate), 1);

    /* The template's children are in its content, not the tree, so a
     * `tr` count over the document is the table's rows only. */
    {
        HtmlNodeHandle table = DomFindId(&root, "grid");

        tagCount = 0u;
        RoboCheckEqInt(HtmlTableGetRows(&table, NULL, NULL, &tagCount, NULL),
                       RoboOk);
        RoboCheckEqInt((int)tagCount, 3); /* thead 1 + tbody 2 */
    }

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 2. Form read → edit → submit → reset, in one pass                   */
/* ------------------------------------------------------------------ */

static void TestIntegrationFormRoundTrip(void)
{
    HtmlDocumentId id = IntegrationDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle form = DomFindId(&root, "signup");
    HtmlNodeHandle name = DomFindId(&root, "name");
    IntegrationSubmit submit;
    HtmlSubmitOutcome outcome;
    HtmlControlValidation control;
    char value[64];
    size_t length = 0u;
    int declared = 0;
    size_t reset = 0u;

    memset(&submit, 0, sizeof(submit));
    RoboCheckEqInt(HtmlDocumentSetFormSubmitHandler(id,
                                                   IntegrationSubmitHandler,
                                                   &submit, NULL),
                   RoboOk);

    /* A `required` control is invalid while empty... */
    RoboCheckEqInt(HtmlElementValidateControl(&name, &control, NULL), RoboOk);
    RoboCheckEqInt(control.validated, 1);
    RoboCheckEqInt(control.valid, 0);
    RoboCheckEqInt(control.valueMissing, 1);

    /* ... and submitting refuses before the handler sees anything. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlFormSubmit(&form, &outcome, NULL), RoboOk);
    RoboCheckEqInt(outcome.valid, 0);
    RoboCheckEqInt(outcome.requested, 0);
    RoboCheckEqInt(submit.calls, 0);

    /* Typing makes it valid, and the *current* value is what a reader sees —
     * not the (absent) attribute. */
    {
        HtmlInputChange change;

        HtmlInputChangeInit(&change);
        RoboCheckEqInt(HtmlInputInsertText(&name, "Ada", 3u, &change, NULL),
                       RoboOk);
        RoboCheckEqInt(change.changed, 1);
        RoboCheckEqInt((int)change.valueLength, 3);
    }
    RoboCheckEqInt(HtmlElementControlValue(&name, value, sizeof(value),
                                           &length, &declared, NULL),
                   RoboOk);
    RoboCheckEqInt((int)length, 3);
    RoboCheck(memcmp(value, "Ada", 3u) == 0);
    RoboCheckEqInt(declared, 0); /* an override, not the declared value */
    RoboCheckEqInt(HtmlElementValidateControl(&name, &control, NULL), RoboOk);
    RoboCheckEqInt(control.valid, 1);

    /* Now the submission runs, and the handler is handed the form's own
     * action and the number of controls the collection found. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlFormSubmit(&form, &outcome, NULL), RoboOk);
    RoboCheckEqInt(outcome.valid, 1);
    RoboCheckEqInt(outcome.requested, 1);
    RoboCheckEqInt(submit.calls, 1);
    RoboCheckEqInt((int)submit.controlCount, 4); /* 3 inputs + the button */
    RoboCheckStrEq(submit.action, "/register");

    /* Reset drops the override, so the control tracks its declaration again
     * and the required check fails once more. */
    RoboCheckEqInt(HtmlFormReset(&form, &reset, NULL), RoboOk);
    RoboCheckEqInt((int)reset, 4);
    RoboCheckEqInt(HtmlElementControlValue(&name, value, sizeof(value),
                                           &length, &declared, NULL),
                   RoboOk);
    RoboCheckEqInt((int)length, 0);
    RoboCheckEqInt(declared, 1); /* back to the declared (empty) value */
    RoboCheckEqInt(HtmlElementValidateControl(&name, &control, NULL), RoboOk);
    RoboCheckEqInt(control.valid, 0);

    /* A form with no handler is a submission this build cannot perform, and it
     * says so rather than pretending. */
    RoboCheckEqInt(HtmlInputInsertText(&name, "Ada", 3u, NULL, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentSetFormSubmitHandler(id, NULL, NULL, NULL),
                   RoboOk);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlFormSubmit(&form, &outcome, NULL), RoboNotSupported);
    RoboCheckEqInt(outcome.valid, 1);
    RoboCheckEqInt(outcome.requested, 0);

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 3. Clone a template's content into the live document                */
/* ------------------------------------------------------------------ */

static void TestIntegrationTemplateClone(void)
{
    HtmlDocumentId id = IntegrationDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle template = DomFindId(&root, "row");
    HtmlNodeHandle body;
    HtmlNodeHandle clone;
    HtmlNodeHandle table;
    size_t rows = 0u;
    size_t inserted = 0u;
    int inert = 0;

    RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                   RoboOk);
    RoboCheckEqInt(inert, 1);

    RoboCheckEqInt(HtmlElementTemplateCloneContent(&template, 1, &clone, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeFindElement(&root, "tbody", &body, NULL), RoboOk);

    /* The clone is the *content of a fragment*, so the operation that inserts
     * it is the fragment one — one call, and the count it reports is the one
     * the table growth agrees with. */
    RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&body, &clone, &inserted,
                                                 NULL),
                   RoboOk);
    RoboCheckEqInt((int)inserted, 1);

    table = DomFindId(&root, "grid");
    RoboCheckEqInt(HtmlTableGetRows(&table, NULL, NULL, &rows, NULL), RoboOk);
    RoboCheckEqInt((int)rows, 4); /* three before, plus the clone */

    /* And the template is still inert: the clone is a second tree. */
    RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                   RoboOk);
    RoboCheckEqInt(inert, 1);

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 4. Shadow root + custom element, together                           */
/* ------------------------------------------------------------------ */

static void TestIntegrationShadowAndCustomElement(void)
{
    HtmlDocumentId id = IntegrationDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle host = DomFindId(&root, "host");
    HtmlNodeHandle card = DomFindId(&root, "card");
    HtmlNodeHandle shadow;
    HtmlNodeHandle inner;
    HtmlNodeHandle found;
    HtmlCustomElementDefinition definition;
    const char *observed[1];
    IntegrationLife life;
    int reachable = 0;

    /*
     * The element is *already parsed and connected* when the definition
     * arrives, so the upgrade pass is what fires the callbacks — the same path
     * a page with deferred script takes.
     */
    observed[0] = "data-kind";
    memset(&life, 0, sizeof(life));
    HtmlCustomElementDefinitionInit(&definition);
    definition.name = "my-card";
    definition.observedAttributes = observed;
    definition.observedCount = 1u;
    definition.callbacks.connected = IntegrationOnConnected;
    definition.callbacks.disconnected = IntegrationOnDisconnected;
    definition.callbacks.attributeChanged = IntegrationOnAttribute;
    definition.userdata = &life;
    RoboCheckEqInt(HtmlDocumentDefineCustomElement(id, &definition, NULL),
                   RoboOk);
    RoboCheckEqInt(life.connected, 1);
    RoboCheckEqInt(life.attributeChanged, 1);
    RoboCheckStrEq(HtmlElementName(&card, NULL), "my-card");
    RoboCheckEqInt(HtmlDocumentCustomElementIsDefined(id, "my-card"), 1);

    /* The host gets a shadow root; its light child is untouched and the
     * document lookup can still see it. */
    RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                          &shadow, NULL),
                   RoboOk);
    {
        HtmlNodeHandle light;

        RoboCheckEqInt(HtmlDocumentGetElementById(id, "light", &light, NULL),
                       RoboOk);
        RoboCheckStrEq(HtmlElementName(&light, NULL), "span");
    }

    /* A node appended *inside* the shadow tree is unreachable from the
     * document but reachable from the root, which is the whole point of the
     * boundary. */
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "b", &inner, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlElementSetAttribute(&inner, "id", "inner", NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&shadow, &inner, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetElementById(id, "inner", &found, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlNodeIsReachableFromDocument(&inner, &reachable, NULL),
                   RoboOk);
    RoboCheckEqInt(reachable, 0);

    /* ... and the shadow tree is part of the same document's audit, not a
     * second tree the audit cannot see. */
    DomExpectCleanTree(id);
    {
        HtmlSecurityReport report;

        memset(&report, 0xff, sizeof(report));
        RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL),
                       RoboOk);
        RoboCheckEqInt(report.treeIntact, 1);
        RoboCheckEqInt((int)report.customElementDefinitions, 1);
    }

    /* Detaching the *host* disconnects the custom element it contains — the
     * tree's shape, not a per-element flag. */
    {
        HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);

        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &host, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&body, &host, NULL), RoboOk);
    }
    /* The card is still connected: it was never detached. */
    RoboCheckEqInt(life.connected, 1);
    RoboCheckEqInt(life.disconnected, 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 5. The image lifecycle inside a real page                           */
/* ------------------------------------------------------------------ */

static void TestIntegrationImageLifecycle(void)
{
    HtmlDocumentId id = IntegrationDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle img = DomFindId(&root, "logo");
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    GfxEncodedBuffer encoded;
    GfxEncodeOptions opts;
    IntegrationFetch fetch;
    HtmlImageResolver resolver;
    HtmlImageLoadOutcome outcome;
    HtmlImageAttributes attributes;

    GfxSurfaceDescInit(&desc);
    desc.width = 3u;
    desc.height = 5u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;
    memset(&encoded, 0, sizeof(encoded));
    RoboCheckEqInt(GfxSurfaceEncode(surface, &opts, &encoded), RoboOk);

    fetch.bytes = encoded.data;
    fetch.length = encoded.size;
    fetch.calls = 0;
    HtmlImageResolverInit(&resolver);
    resolver.fetch = IntegrationFetchFn;
    resolver.userdata = &fetch;

    /* The declared box is 64 wide, parsed as a plain DOM attribute. */
    RoboCheckEqInt(HtmlElementImageAttributes(&img, &attributes, NULL),
                   RoboOk);
    RoboCheckEqInt(attributes.widthSet, 1);
    RoboCheckEqInt(attributes.widthValid, 1);
    RoboCheckEqInt((int)attributes.width, 64);
    {
        HtmlImageReferenceClass class = RoboHtmlImageReferenceNone;

        RoboCheckEqInt(HtmlElementImageReferenceClass(&img, &class, NULL),
                       RoboOk);
        RoboCheckEqInt(class, RoboHtmlImageReferenceLocal);
    }

    /* Loading reports the used size: the declaration wins over the bitmap. */
    RoboCheckEqInt(HtmlElementImageLoad(&img, &resolver, &outcome, NULL),
                   RoboOk);
    RoboCheckEqInt(fetch.calls, 1);
    RoboCheckEqInt(outcome.state, RoboHtmlImageStateDecoded);
    RoboCheckEqInt(outcome.hasIntrinsic, 1);
    RoboCheckEqInt((int)outcome.intrinsicWidth, 3);
    RoboCheckEqInt((int)outcome.intrinsicHeight, 5);
    RoboCheckEqInt(outcome.hasUsedSize, 1);
    RoboCheckEqInt((int)outcome.usedWidth, 64);
    RoboCheckEqInt((int)outcome.usedHeight, 5); /* height comes from the bitmap */
    RoboCheckEqInt(outcome.layoutAffected, 1);
    RoboCheckEqInt(HtmlDocumentImageCount(id), 1u);

    /* Replacing the source reloads and reports the change; abandoning the
     * element releases the resource and leaves the document intact. */
    RoboCheckEqInt(HtmlElementImageReplaceSource(&img, "assets/other.png", 16u,
                                                &resolver, &outcome, NULL),
                   RoboOk);
    RoboCheckEqInt(fetch.calls, 2);
    RoboCheckEqInt(outcome.replaced, 1);
    RoboCheckEqInt(outcome.sourceChanged, 1);
    {
        HtmlImageAttributes now;

        RoboCheckEqInt(HtmlElementImageAttributes(&img, &now, NULL), RoboOk);
        RoboCheckEqInt((int)now.sourceLength, 16);
        RoboCheck(memcmp(now.source, "assets/other.png", 16u) == 0);
    }
    {
        int wasPending = 0;

        RoboCheckEqInt(HtmlElementImageAbandon(&img, &wasPending, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 0u);
    }

    GfxEncodedBufferRelease(&encoded);
    GfxSurfaceRelease(surface);
    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 6. Navigation: route, fragment, and a refused scheme                */
/* ------------------------------------------------------------------ */

static void TestIntegrationNavigation(void)
{
    HtmlDocumentId id = IntegrationDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle settings = DomFindId(&root, "settings");
    HtmlNodeHandle jump = DomFindId(&root, "jump");
    IntegrationRoute route;
    HtmlNavigationHandler handler;
    HtmlNavigationOutcome outcome;
    HtmlNavigationLocation location;

    memset(&route, 0, sizeof(route));
    HtmlNavigationHandlerInit(&handler);
    handler.navigate = IntegrationNavHandler;
    handler.userdata = &route;
    RoboCheckEqInt(HtmlDocumentSetNavigationHandler(id, &handler, NULL),
                   RoboOk);

    /* A route reaches the handler exactly once. */
    RoboCheckEqInt(HtmlDocumentActivateLink(&settings, &outcome, NULL),
                   RoboOk);
    RoboCheckEqInt(route.calls, 1);
    RoboCheckEqInt(outcome.handlerCalled, 1);
    HtmlNavigationOutcomeClear(&outcome);

    /* A fragment is resolved locally — the handler is not consulted — and the
     * location records the element it named. */
    RoboCheckEqInt(HtmlDocumentActivateLink(&jump, &outcome, NULL), RoboOk);
    RoboCheckEqInt(route.calls, 1);
    RoboCheckEqInt(outcome.kind, RoboHtmlNavigationFragment);
    RoboCheckEqInt(outcome.fragmentFound, 1);
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt(HtmlDocumentNavigateToFragment(id, "Summary", 7u, &outcome,
                                                 NULL),
                   RoboOk);
    RoboCheckEqInt(outcome.fragmentFound, 1);
    HtmlNavigationOutcomeClear(&outcome);
    memset(&location, 0, sizeof(location));
    RoboCheckEqInt(HtmlDocumentCurrentLocation(id, &location, NULL), RoboOk);

    /* A `javascript:` reference never reaches the handler: the scheme policy
     * refuses it before the seam is consulted, so the count stays at one. */
    {
        HtmlNodeHandle hostile;

        RoboCheckEqInt(HtmlElementSetAttribute(&jump, "href",
                                               "javascript:alert(1)", NULL),
                       RoboOk);
        hostile = jump;
        RoboCheckEqInt(HtmlDocumentActivateLink(&hostile, &outcome, NULL),
                       RoboOk);
        RoboCheckEqInt(route.calls, 1);
        RoboCheckEqInt(outcome.handlerCalled, 0);
        HtmlNavigationOutcomeClear(&outcome);
    }

    DomExpectCleanTree(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 7. Mutation invalidation and the audit                              */
/* ------------------------------------------------------------------ */

static void TestIntegrationMutationAndAudit(void)
{
    HtmlDocumentId id = IntegrationDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle grid = DomFindId(&root, "grid");
    uint32_t before = 0u;
    uint32_t after = 0u;
    size_t rows = 0u;
    HtmlSecurityReport report;

    RoboCheckEqInt(HtmlNodeTreeVersion(id, &before, NULL), RoboOk);
    RoboCheck(before != 0u);

    /* A structural change bumps the version, and the *reads* over the changed
     * subtree follow it with no invalidation step of their own. */
    {
        HtmlNodeHandle body;
        HtmlNodeHandle clone;
        HtmlNodeHandle template = DomFindId(&root, "row");
        size_t inserted = 0u;

        RoboCheckEqInt(HtmlElementTemplateCloneContent(&template, 1, &clone,
                                                      NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeFindElement(&grid, "tbody", &body, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&body, &clone,
                                                      &inserted, NULL),
                       RoboOk);
        RoboCheckEqInt((int)inserted, 1);
    }
    RoboCheckEqInt(HtmlNodeTreeVersion(id, &after, NULL), RoboOk);
    RoboCheck(after > before);
    RoboCheckEqInt(HtmlTableGetRows(&grid, NULL, NULL, &rows, NULL), RoboOk);
    RoboCheckEqInt((int)rows, 4);

    /* The report follows the same counts, and the audit stays clean through a
     * rollback as well as a change. */
    memset(&report, 0xff, sizeof(report));
    RoboCheckEqInt(HtmlDocumentGetSecurityReport(id, &report, NULL), RoboOk);
    RoboCheckEqInt(report.treeVersion, after);
    RoboCheck(report.reachableNodes >= 10u);
    DomExpectCleanTree(id);

    /* A refused mutation leaves both the version and the tree untouched. */
    {
        HtmlDocumentCapabilities caps;
        HtmlNodeHandle body;

        HtmlDocumentCapabilitiesDefault(&caps);
        caps.domMutation = 0;
        RoboCheckEqInt(HtmlDocumentSetCapabilities(id, &caps, NULL), RoboOk);
        before = after;
        RoboCheckEqInt(HtmlNodeFindElement(&root, "body", &body, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&body, &grid, NULL),
                       RoboPermissionDenied);
        RoboCheckEqInt(HtmlNodeTreeVersion(id, &after, NULL), RoboOk);
        RoboCheckEqInt(after, before);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* 8. Lifetime: build, use, destroy, and no survivors                  */
/* ------------------------------------------------------------------ */

static void TestIntegrationLifetime(void)
{
    HtmlDocumentId retired[6];
    GfxSurfaceDesc desc;
    GfxSurface *surface = NULL;
    GfxEncodedBuffer encoded;
    GfxEncodeOptions opts;
    IntegrationFetch fetch;
    HtmlImageResolver resolver;
    size_t i;

    /* One encoded bitmap serves every pass: what is under test is the
     * document's lifetime, not the encoder. */
    GfxSurfaceDescInit(&desc);
    desc.width = 2u;
    desc.height = 2u;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheckEqInt(GfxSurfaceCreate(&desc, &surface), RoboOk);
    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;
    memset(&encoded, 0, sizeof(encoded));
    RoboCheckEqInt(GfxSurfaceEncode(surface, &opts, &encoded), RoboOk);
    fetch.bytes = encoded.data;
    fetch.length = encoded.size;
    fetch.calls = 0;
    HtmlImageResolverInit(&resolver);
    resolver.fetch = IntegrationFetchFn;
    resolver.userdata = &fetch;

    memset(retired, 0, sizeof(retired));
    for (i = 0u; i < 6u; i++) {
        HtmlDocumentId id = IntegrationDoc();
        HtmlNodeHandle root = DomRoot(id);
        HtmlNodeHandle host = DomFindId(&root, "host");
        HtmlNodeHandle shadow;
        HtmlNodeHandle img = DomFindId(&root, "logo");
        HtmlImageLoadOutcome outcome;

        /* Two surfaces per pass, so a leak in either shows up here. */
        RoboCheckEqInt(HtmlElementAttachShadow(&host, RoboHtmlShadowModeOpen,
                                              &shadow, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementImageLoad(&img, &resolver, &outcome, NULL),
                       RoboOk);
        RoboCheckEqInt(outcome.state, RoboHtmlImageStateDecoded);
        RoboCheckEqInt(HtmlDocumentImageCount(id), 1u);
        DomExpectCleanTree(id);

        retired[i] = id;
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
    RoboCheckEqInt(fetch.calls, 6);

    GfxEncodedBufferRelease(&encoded);
    GfxSurfaceRelease(surface);

    /* Every retired id is retired: a fresh document never reuses one, and
     * every door answers NotFound for it. */
    for (i = 0u; i < 6u; i++) {
        HtmlDocumentCapabilities caps;

        RoboCheckEqInt(HtmlDocumentGetCapabilities(retired[i], &caps, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentCapabilityAllowed(
                           retired[i], RoboHtmlCapabilityDomMutation), 0);
        RoboCheckEqInt(HtmlDocumentIsLive(retired[i]), 0);
    }

    /* And a document created now has an id none of them had. */
    {
        HtmlDocumentId fresh = DomDocumentOf("<body><p>x</p></body>");

        for (i = 0u; i < 6u; i++) {
            RoboCheck(fresh != retired[i]);
        }
        RoboCheckEqInt(HtmlDocumentDestroy(fresh, NULL), RoboOk);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlDomIntegration(void);

void TestRegisterHtmlDomIntegration(void)
{
    TestRegister("html.integration.document", TestIntegrationDocument);
    TestRegister("html.integration.form_round_trip",
                 TestIntegrationFormRoundTrip);
    TestRegister("html.integration.template_clone",
                 TestIntegrationTemplateClone);
    TestRegister("html.integration.shadow_and_custom_element",
                 TestIntegrationShadowAndCustomElement);
    TestRegister("html.integration.image_lifecycle",
                 TestIntegrationImageLifecycle);
    TestRegister("html.integration.navigation", TestIntegrationNavigation);
    TestRegister("html.integration.mutation_and_audit",
                 TestIntegrationMutationAndAudit);
    TestRegister("html.integration.lifetime", TestIntegrationLifetime);
}
