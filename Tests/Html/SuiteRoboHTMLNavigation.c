/*
 * RoboHTML Phase 17.4 navigation suite: href/target readers, the document
 * navigation controller, same-document fragment navigation, the
 * decline-means-preventDefault path, the scheme policy (javascript: never
 * reaches a handler), the location state, and the document-load step.
 */

#include "RoboHTML/RoboHTMLNavigation.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include "RoboHTMLDomTestSupport.h"

#include <stddef.h>
#include <string.h>

static const char NavMarkup[] =
    "<html><body>"
    "<a id=\"route\" href=\"/Settings\" target=\"_self\" rel=\"noopener\">S</a>"
    "<a id=\"frag\" href=\"#Overview\">O</a>"
    "<a id=\"ext\" href=\"https://example.com/x\">E</a>"
    "<a id=\"js\" href=\"javascript:alert(1)\">J</a>"
    "<a id=\"none\">N</a>"
    "<p id=\"Overview\">Here</p>"
    "</body></html>";

static int NavHandlerCalls = 0;
static int NavDecline = 0;

static Result NavTestHandler(void *userdata,
                             const HtmlNavigationRequest *request,
                             HtmlNavigationOutcomeKind *outKind,
                             HtmlDiag *diag)
{
    HtmlTargetKind kind;

    (void)userdata;
    (void)diag;
    NavHandlerCalls++;
    if (!request || !request->target) {
        return RoboError;
    }
    kind = HtmlNavigationTargetKind(request->target);
    if (NavDecline && kind == RoboHtmlTargetRoute) {
        return RoboError;
    }
    switch (kind) {
    case RoboHtmlTargetRoute:
        *outKind = RoboHtmlNavigationRoute;
        return RoboOk;
    case RoboHtmlTargetExternal:
        *outKind = RoboHtmlNavigationExternal;
        return RoboOk;
    case RoboHtmlTargetFragment:
        *outKind = RoboHtmlNavigationFragment;
        return RoboOk;
    default:
        *outKind = RoboHtmlNavigationNoTarget;
        return RoboOk;
    }
}

static HtmlNodeHandle NavEl(HtmlDocumentId id, const char *value)
{
    HtmlNodeHandle out;

    RoboCheckEqInt(HtmlDocumentGetElementById(id, value, &out, NULL), RoboOk);
    return out;
}

static void NavInstallHandler(HtmlDocumentId id)
{
    HtmlNavigationHandler handler;

    HtmlNavigationHandlerInit(&handler);
    handler.navigate = NavTestHandler;
    RoboCheckEqInt(HtmlDocumentSetNavigationHandler(id, &handler, NULL),
                   RoboOk);
}

static void TestNavCapabilities(void)
{
    HtmlNavigationCapabilities caps;

    memset(&caps, 0, sizeof(caps));
    RoboCheckEqInt(HtmlNavigationGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.elementHref, 1);
    RoboCheckEqInt(caps.controller, 1);
    RoboCheckEqInt(caps.fragmentNavigation, 1);
    RoboCheckEqInt(caps.locationState, 1);
    RoboCheckEqInt(caps.documentLoad, 1);
    RoboCheckEqInt(caps.preventDefault, 0);
    RoboCheckEqInt(caps.networking, 0);
    RoboCheckEqInt(HtmlNavigationGetCapabilities(NULL), RoboInvalidArgument);
}

static void TestNavElementReaders(void)
{
    HtmlDocumentId id = DomDocumentOf(NavMarkup);
    HtmlNodeHandle route = NavEl(id, "route");
    HtmlNodeHandle none = NavEl(id, "none");
    char buffer[128];
    size_t length = 0u;

    RoboCheckEqInt(HtmlElementHref(&route, buffer, sizeof(buffer), &length,
                                   NULL),
                   RoboOk);
    RoboCheck(strcmp(buffer, "/Settings") == 0);
    RoboCheckEqInt(HtmlElementTarget(&route, buffer, sizeof(buffer), &length,
                                     NULL),
                   RoboOk);
    RoboCheck(strcmp(buffer, "_self") == 0);

    /* Absent attributes read as empty, not as an error. */
    RoboCheckEqInt(HtmlElementHref(&none, buffer, sizeof(buffer), &length,
                                   NULL),
                   RoboOk);
    RoboCheckEqInt((int)length, 0);
    RoboCheckEqInt(HtmlElementTarget(&none, buffer, sizeof(buffer), &length,
                                     NULL),
                   RoboOk);
    RoboCheckEqInt((int)length, 0);

    /* A non-anchor is refused. */
    {
        HtmlNodeHandle body = NavEl(id, "Overview");

        RoboCheckEqInt(HtmlElementHref(&body, buffer, sizeof(buffer), &length,
                                       NULL),
                       RoboNotSupported);
    }

    HtmlShutdown();
}

static void TestNavController(void)
{
    HtmlDocumentId id = DomDocumentOf(NavMarkup);
    HtmlNodeHandle route = NavEl(id, "route");
    HtmlNavigationOutcome outcome;
    HtmlNavigationLocation location;

    /* With no handler installed a route is unsupported, not faked. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentActivateLink(&route, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationUnsupported);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    HtmlNavigationOutcomeClear(&outcome);

    /* Install a handler; the route is accepted and recorded. */
    NavHandlerCalls = 0;
    NavInstallHandler(id);
    RoboCheckEqInt(HtmlDocumentHasNavigationHandler(id), 1);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentActivateLink(&route, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationRoute);
    RoboCheckEqInt(outcome.handlerCalled, 1);
    RoboCheckEqInt(NavHandlerCalls, 1);
    HtmlNavigationOutcomeClear(&outcome);

    RoboCheckEqInt(HtmlDocumentCurrentLocation(id, &location, NULL), RoboOk);
    RoboCheck(strcmp(location.route, "/Settings") == 0);
    RoboCheckEqInt((int)location.navigations, 1);

    HtmlShutdown();
}

static void TestNavFragment(void)
{
    HtmlDocumentId id = DomDocumentOf(NavMarkup);
    HtmlNodeHandle frag = NavEl(id, "frag");
    HtmlNavigationOutcome outcome;
    HtmlNavigationLocation location;

    NavInstallHandler(id);

    /* A href="#id" activation records the fragment. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentActivateLink(&frag, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationFragment);
    HtmlNavigationOutcomeClear(&outcome);
    RoboCheckEqInt(HtmlDocumentCurrentLocation(id, &location, NULL), RoboOk);
    RoboCheck(strcmp(location.fragment, "Overview") == 0);

    /* Direct fragment navigation: present and missing. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentNavigateToFragment(id, "Overview", 8u, &outcome,
                                                  NULL),
                   RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationFragment);
    RoboCheckEqInt(outcome.fragmentFound, 1);
    HtmlNavigationOutcomeClear(&outcome);

    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentNavigateToFragment(id, "nope", 4u, &outcome,
                                                  NULL),
                   RoboOk);
    RoboCheckEqInt((int)outcome.kind,
                   (int)RoboHtmlNavigationFragmentMissing);
    RoboCheckEqInt(outcome.fragmentFound, 0);
    HtmlNavigationOutcomeClear(&outcome);

    HtmlShutdown();
}

static void TestNavDeclineIsPreventDefault(void)
{
    HtmlDocumentId id = DomDocumentOf(NavMarkup);
    HtmlNodeHandle route = NavEl(id, "route");
    HtmlNavigationOutcome outcome;
    HtmlNavigationLocation location;

    NavDecline = 1;
    NavInstallHandler(id);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentActivateLink(&route, &outcome, NULL), RoboOk);
    /* The handler was consulted and declined: no navigation. */
    RoboCheckEqInt(outcome.handlerCalled, 1);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationUnsupported);
    HtmlNavigationOutcomeClear(&outcome);
    RoboCheckEqInt(HtmlDocumentCurrentLocation(id, &location, NULL), RoboOk);
    RoboCheckEqInt((int)location.navigations, 0);
    NavDecline = 0;

    HtmlShutdown();
}

static void TestNavSchemePolicy(void)
{
    HtmlDocumentId id = DomDocumentOf(NavMarkup);
    HtmlNodeHandle js = NavEl(id, "js");
    HtmlNodeHandle ext = NavEl(id, "ext");
    HtmlNavigationOutcome outcome;

    NavHandlerCalls = 0;
    NavInstallHandler(id);

    /* javascript: is refused before the handler is consulted. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentActivateLink(&js, &outcome, NULL), RoboOk);
    RoboCheckEqInt((int)outcome.kind, (int)RoboHtmlNavigationUnsupported);
    RoboCheckEqInt(outcome.handlerCalled, 0);
    RoboCheckEqInt(NavHandlerCalls, 0);
    HtmlNavigationOutcomeClear(&outcome);

    /* https: is permitted and reaches the handler. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlDocumentActivateLink(&ext, &outcome, NULL), RoboOk);
    RoboCheckEqInt(outcome.handlerCalled, 1);
    HtmlNavigationOutcomeClear(&outcome);

    HtmlShutdown();
}

static void TestNavDocumentLoad(void)
{
    HtmlDocumentId id = DomDocumentOf(NavMarkup);
    HtmlSource source;
    HtmlNodeHandle out;
    char buffer[64];
    size_t length = 0u;

    HtmlSourceInit(&source);
    RoboCheckEqInt(HtmlSourceString(&source,
                                    "<html><body><p id=\"loaded\">Two</p>"
                                    "</body></html>",
                                    NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentLoadSource(id, &source, NULL), RoboOk);

    /* The new tree replaced the old one. */
    RoboCheckEqInt(HtmlDocumentGetElementById(id, "loaded", &out, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeTextContentGet(&out, buffer, sizeof(buffer), &length,
                                          NULL),
                   RoboOk);
    RoboCheck(strcmp(buffer, "Two") == 0);
    RoboCheckEqInt(HtmlDocumentGetElementById(id, "route", &out, NULL),
                   RoboNotFound);

    HtmlShutdown();
}

static void TestNavUnknownDocument(void)
{
    HtmlDocumentId id = DomDocumentOf(NavMarkup);
    HtmlNavigationOutcome outcome;
    HtmlNavigationLocation location;

    HtmlShutdown();
    RoboCheckEqInt(HtmlDocumentSetNavigationHandler(id, NULL, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentNavigateToFragment(id, "x", 1u, &outcome, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentCurrentLocation(id, &location, NULL),
                   RoboNotFound);
}

void TestRegisterHtmlNavigation(void)
{
    TestRegister("html.nav.capabilities", TestNavCapabilities);
    TestRegister("html.nav.element_readers", TestNavElementReaders);
    TestRegister("html.nav.controller", TestNavController);
    TestRegister("html.nav.fragment", TestNavFragment);
    TestRegister("html.nav.decline_is_prevent_default",
                 TestNavDeclineIsPreventDefault);
    TestRegister("html.nav.scheme_policy", TestNavSchemePolicy);
    TestRegister("html.nav.document_load", TestNavDocumentLoad);
    TestRegister("html.nav.unknown_document", TestNavUnknownDocument);
}
