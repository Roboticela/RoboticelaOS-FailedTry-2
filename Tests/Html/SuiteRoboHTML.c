/*
 * RoboHTML Phase 8.1 foundation suite:
 * module lifecycle, diagnostics, lifecycle state machine.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDiagnostics.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLTypes.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboTest.h"

static void HtmlFixture(void)
{
    /* Foundation must be up for every RoboHTML call. */
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
}

static void TestHtmlVersion(void)
{
    I32 major = 0;
    I32 minor = 0;
    I32 patch = 0;

    RoboCheckStrEq(HtmlVersionString(), RoboHtmlVersionString);
    RoboCheckEqInt(RoboHtmlVersionMajor, 0);
    /* Phase 17's DOM batch: the version moves with the surface, and this is
     * the one place that pins it, so a bump is a deliberate edit here too. */
    RoboCheckEqInt(RoboHtmlVersionMinor, 16);
    RoboCheckEqInt(RoboHtmlVersionPatch, 0);
    /* The API is still growing through Phase 17, so it is not frozen. */
    RoboCheckEqInt(RoboHtmlApiFrozen, 0);

    RoboCheckEqInt(HtmlGetVersion(NULL, &minor, &patch),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlGetVersion(&major, NULL, &patch),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlGetVersion(&major, &minor, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlGetVersion(&major, &minor, &patch), RoboOk);
    RoboCheckEqInt(major, 0);
    RoboCheckEqInt(minor, 16);
    RoboCheckEqInt(patch, 0);
}

static void TestHtmlModuleInitShutdown(void)
{
    HtmlFixture();
    RoboCheckEqInt(HtmlIsInitialized(), 0);

    /* Document APIs refuse to run while the module is down. */
    {
        HtmlDocumentId id = RoboHtmlDocumentIdNone;
        RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboInitError);
        RoboCheckEqInt(id, RoboHtmlDocumentIdNone);
    }

    RoboCheckEqInt(HtmlInit(), RoboOk);
    RoboCheck(HtmlIsInitialized());
    RoboCheckEqInt(HtmlInit(), RoboAlreadyExists);
    RoboCheckEqInt(HtmlLiveDocumentCount(), 0u);

    /* Idempotent shutdown. */
    HtmlShutdown();
    RoboCheckEqInt(HtmlIsInitialized(), 0);
    HtmlShutdown();
    RoboCheckEqInt(HtmlIsInitialized(), 0);

    /* Re-init works after clean shutdown. */
    RoboCheckEqInt(HtmlInit(), RoboOk);
    RoboCheckEqInt(HtmlLiveDocumentCount(), 0u);
    HtmlShutdown();
}

static void TestHtmlDiagnostics(void)
{
    HtmlDiag diag;
    HtmlDiag missing;

    HtmlDiagReset(&diag);
    RoboCheckEqInt(diag.code, RoboOk);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainNone);

    /* NULL diag record: returns the strict code. */
    RoboCheckEqInt(HtmlDiagSet(NULL, RoboError,
                                         RoboHtmlDomainDocument, "m",
                                         "f", 1),
                      RoboInvalidArgument);

    HtmlDiagReset(&missing);
    RoboCheckEqInt(RoboHtmlDiag(&missing, RoboHtmlDomainParser,
                                     RoboBusy, "busy"),
                      RoboBusy);
    RoboCheckEqInt(missing.code, RoboBusy);
    RoboCheckEqInt(missing.domain, RoboHtmlDomainParser);
    RoboCheck(missing.message != NULL);
    RoboCheck(missing.file != NULL);
    RoboCheck(missing.line > 0);

    /* NULL diag through the non-strict macro: code is authoritative. */
    RoboCheckEqInt(RoboHtmlDiag(NULL, RoboHtmlDomainSource,
                                     RoboParseError, "ignored"),
                      RoboParseError);

    /* OK code records cleanly. */
    RoboCheckEqInt(RoboHtmlDiag(&missing, RoboHtmlDomainNone,
                                     RoboOk, "ok"),
                      RoboOk);
    RoboCheckEqInt(missing.code, RoboOk);
}

static void TestHtmlLifecycleMachine(void)
{
    /* Enum coverage. */
    RoboCheck(HtmlDocumentLifecycleIsKnown(
        RoboHtmlDocumentLifecycleCreated));
    RoboCheck(HtmlDocumentLifecycleIsKnown(
        RoboHtmlDocumentLifecycleDestroyed));
    RoboCheck(HtmlDocumentLifecycleIsKnown(
        RoboHtmlDocumentLifecycleInteractive));
    RoboCheck(HtmlDocumentLifecycleIsKnown(
        RoboHtmlDocumentLifecycleComplete));
    RoboCheck(!HtmlDocumentLifecycleIsKnown(
        (HtmlDocumentLifecycle)99));

    /* Allowed edges. */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleUninitialized,
            RoboHtmlDocumentLifecycleCreated),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleCreated,
            RoboHtmlDocumentLifecycleLoading),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleLoading,
            RoboHtmlDocumentLifecycleInteractive),
        RoboOk);
    /* The one-way step that makes COMPLETE mean "nothing more can arrive". */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleInteractive,
            RoboHtmlDocumentLifecycleComplete),
        RoboOk);
    /* A settled document can take a replacement source. */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleInteractive,
            RoboHtmlDocumentLifecycleLoading),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleComplete,
            RoboHtmlDocumentLifecycleLoading),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleCreated,
            RoboHtmlDocumentLifecycleFailed),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleFailed,
            RoboHtmlDocumentLifecycleDestroying),
        RoboOk);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleDestroying,
            RoboHtmlDocumentLifecycleDestroyed),
        RoboOk);
    /* Idempotent destroyed stay. */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleDestroyed,
            RoboHtmlDocumentLifecycleDestroyed),
        RoboOk);

    /* Forbidden edges. */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleUninitialized,
            RoboHtmlDocumentLifecycleInteractive),
        RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleInteractive,
            RoboHtmlDocumentLifecycleCreated),
        RoboInvalidArgument);
    /*
     * The two rules that give the phase its meaning: a document cannot be
     * finished loading before it has finished parsing, and a settled
     * document cannot go back to being merely interactive.
     */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleCreated,
            RoboHtmlDocumentLifecycleComplete),
        RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleLoading,
            RoboHtmlDocumentLifecycleComplete),
        RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleComplete,
            RoboHtmlDocumentLifecycleInteractive),
        RoboInvalidArgument);
    /* Stay-edges are refused everywhere but DESTROYED. */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleInteractive,
            RoboHtmlDocumentLifecycleInteractive),
        RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleComplete,
            RoboHtmlDocumentLifecycleComplete),
        RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleFailed,
            RoboHtmlDocumentLifecycleInteractive),
        RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            RoboHtmlDocumentLifecycleCreated,
            RoboHtmlDocumentLifecycleDestroyed),
        RoboInvalidArgument); /* must pass through DESTROYING */
    RoboCheckEqInt(
        HtmlDocumentLifecycleCheckTransition(
            (HtmlDocumentLifecycle)77,
            RoboHtmlDocumentLifecycleCreated),
        RoboInvalidArgument);

    /* Names and predicates. */
    RoboCheckStrEq(HtmlDocumentLifecycleName(
                          RoboHtmlDocumentLifecycleLoading),
                      "loading");
    RoboCheckStrEq(HtmlDocumentLifecycleName(
                          RoboHtmlDocumentLifecycleInteractive),
                      "interactive");
    RoboCheckStrEq(HtmlDocumentLifecycleName(
                          RoboHtmlDocumentLifecycleComplete),
                      "complete");
    RoboCheckStrEq(HtmlDocumentLifecycleName(
                          (HtmlDocumentLifecycle)55),
                      "unknown");
    RoboCheck(HtmlDocumentLifecycleAllowsContent(
        RoboHtmlDocumentLifecycleCreated));
    RoboCheck(HtmlDocumentLifecycleAllowsContent(
        RoboHtmlDocumentLifecycleLoading));
    RoboCheck(HtmlDocumentLifecycleAllowsContent(
        RoboHtmlDocumentLifecycleInteractive));
    /* Completion describes input, not permission: the DOM stays open. */
    RoboCheck(HtmlDocumentLifecycleAllowsContent(
        RoboHtmlDocumentLifecycleComplete));
    RoboCheck(!HtmlDocumentLifecycleAllowsContent(
        RoboHtmlDocumentLifecycleFailed));
    RoboCheck(!HtmlDocumentLifecycleAllowsContent(
        RoboHtmlDocumentLifecycleDestroying));
    RoboCheck(HtmlDocumentLifecycleAllowsSourceAttach(
        RoboHtmlDocumentLifecycleCreated));
    RoboCheck(HtmlDocumentLifecycleAllowsSourceAttach(
        RoboHtmlDocumentLifecycleInteractive));
    RoboCheck(HtmlDocumentLifecycleAllowsSourceAttach(
        RoboHtmlDocumentLifecycleComplete));
    RoboCheck(!HtmlDocumentLifecycleAllowsSourceAttach(
        RoboHtmlDocumentLifecycleLoading));
    RoboCheck(!HtmlDocumentLifecycleAllowsSourceAttach(
        RoboHtmlDocumentLifecycleFailed));

    /* Settled / terminal / destroyed classifiers. */
    RoboCheck(HtmlDocumentLifecycleIsSettled(
        RoboHtmlDocumentLifecycleInteractive));
    RoboCheck(HtmlDocumentLifecycleIsSettled(
        RoboHtmlDocumentLifecycleComplete));
    RoboCheck(!HtmlDocumentLifecycleIsSettled(
        RoboHtmlDocumentLifecycleLoading));
    RoboCheck(HtmlDocumentLifecycleIsTerminal(
        RoboHtmlDocumentLifecycleFailed));
    RoboCheck(HtmlDocumentLifecycleIsTerminal(
        RoboHtmlDocumentLifecycleDestroyed));
    RoboCheck(!HtmlDocumentLifecycleIsTerminal(
        RoboHtmlDocumentLifecycleComplete));
    RoboCheck(HtmlDocumentLifecycleIsDestroyed(
        RoboHtmlDocumentLifecycleDestroyed));
    RoboCheck(!HtmlDocumentLifecycleIsDestroyed(
        RoboHtmlDocumentLifecycleDestroying));
}

static void TestHtmlTypeHelpers(void)
{
    RoboCheck(HtmlNodeKindIsKnown(RoboHtmlNodeKindDocument));
    RoboCheck(HtmlNodeKindIsKnown(RoboHtmlNodeKindFragment));
    RoboCheck(!HtmlNodeKindIsKnown((HtmlNodeKind)42));
    RoboCheckStrEq(HtmlNodeKindName(RoboHtmlNodeKindText),
                      "text");
    RoboCheckStrEq(HtmlNodeKindName((HtmlNodeKind)42),
                      "unknown");

    RoboCheck(HtmlDiagnosticDomainIsKnown(RoboHtmlDomainParser));
    RoboCheck(!HtmlDiagnosticDomainIsKnown(
        (HtmlDiagnosticDomain)77));
    RoboCheckStrEq(
        HtmlDiagnosticDomainName(RoboHtmlDomainEncoding),
        "encoding");
    RoboCheckStrEq(
        HtmlDiagnosticDomainName((HtmlDiagnosticDomain)77),
        "unknown");
}

void TestRegisterHtml(void)
{
    TestRegister("html.version", TestHtmlVersion);
    TestRegister("html.module.init_shutdown",
                       TestHtmlModuleInitShutdown);
    TestRegister("html.module.diagnostics", TestHtmlDiagnostics);
    TestRegister("html.lifecycle.machine", TestHtmlLifecycleMachine);
    TestRegister("html.types.helpers", TestHtmlTypeHelpers);
}
