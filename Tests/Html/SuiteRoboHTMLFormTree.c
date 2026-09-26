/*
 * RoboHTML Phase 17.2 form suite: association, form.elements, validation,
 * submission (blocked, refused, requested) and reset.
 *
 * The fixture is parsed markup with two forms plus a control associated to the
 * first by `form` attribute, so "association" is proved across both rules.
 */

#include "RoboHTML/RoboHTMLFormTree.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboTest.h"

#include "RoboHTMLDomTestSupport.h"

#include <stddef.h>
#include <string.h>

static const char FormMarkup[] =
    "<html><body>"
    "<form id=\"f\" action=\"/submit\" method=\"post\" "
    "enctype=\"application/x-www-form-urlencoded\">"
    "<label for=\"name\">Name</label>"
    "<input id=\"name\" name=\"name\" type=\"text\" required minlength=\"2\" "
    "maxlength=\"10\">"
    "<input id=\"age\" name=\"age\" type=\"number\" min=\"1\" max=\"120\" "
    "value=\"30\">"
    "<input id=\"ok\" name=\"ok\" type=\"checkbox\" required>"
    "<select id=\"c\" name=\"c\" required><option value=\"a\" selected>A</option>"
    "<option value=\"b\">B</option></select>"
    "<textarea id=\"t\" name=\"t\" required></textarea>"
    "<button type=\"submit\">Go</button>"
    "</form>"
    "<form id=\"g\"><input id=\"other\" name=\"other\" type=\"text\"></form>"
    "<input id=\"orphan\" name=\"orphan\" type=\"text\" form=\"f\">"
    "</body></html>";

static HtmlDocumentId FormDoc(void)
{
    return DomDocumentOf(FormMarkup);
}

static HtmlNodeHandle FormById(HtmlDocumentId id, const char *value)
{
    HtmlNodeHandle out;

    RoboCheckEqInt(HtmlDocumentGetElementById(id, value, &out, NULL), RoboOk);
    return out;
}

static size_t FormElementCount(const HtmlNodeHandle *form)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlFormGetElements(form, NULL, NULL, &count, NULL), RoboOk);
    return count;
}

/* A handler that records the call. */
static int FormHandlerCalls = 0;

static Result FormTestHandler(const HtmlNodeHandle *form,
                              const HtmlFormAttributes *attributes,
                              size_t controlCount,
                              void *userdata,
                              HtmlDiag *diag)
{
    (void)form;
    (void)attributes;
    (void)controlCount;
    (void)userdata;
    (void)diag;
    FormHandlerCalls++;
    return RoboOk;
}

static void TestFormCapabilities(void)
{
    HtmlFormTreeCapabilities caps;

    memset(&caps, 0, sizeof(caps));
    RoboCheckEqInt(HtmlFormTreeGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.association, 1);
    RoboCheckEqInt(caps.elements, 1);
    RoboCheckEqInt(caps.validation, 1);
    RoboCheckEqInt(caps.submit, 1);
    RoboCheckEqInt(caps.reset, 1);
    RoboCheckEqInt(caps.patternValidation, 0);
    RoboCheckEqInt(caps.submissionNetworking, 0);
    RoboCheckEqInt(HtmlFormTreeGetCapabilities(NULL), RoboInvalidArgument);
}

static void TestFormElements(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle f = FormById(id, "f");
    HtmlNodeHandle g = FormById(id, "g");

    /* name, age, ok, c, t, button, plus the `form="f"` control outside. */
    RoboCheckEqInt((int)FormElementCount(&f), 7);
    RoboCheckEqInt((int)FormElementCount(&g), 1);

    /* A non-form is refused rather than reported empty. */
    {
        HtmlNodeHandle body = FormById(id, "orphan");
        size_t count = 0u;

        RoboCheckEqInt(HtmlFormGetElements(&body, NULL, NULL, &count, NULL),
                       RoboNotSupported);
    }

    HtmlShutdown();
}

static void TestFormAssociationMove(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle f = FormById(id, "f");
    HtmlNodeHandle g = FormById(id, "g");

    /* Moving a control in the tree changes its owner with no cache to bust. */
    RoboCheckEqInt((int)FormElementCount(&f), 7);
    {
        HtmlNodeHandle other = FormById(id, "other");
        HtmlNodeHandle body;

        RoboCheckEqInt(HtmlNodeParent(&other, &body, NULL), RoboOk);
        /* body may be the html; resolve the real parent by moving to f. */
        {
            HtmlNodeHandle ownerBefore;
            HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;

            RoboCheckEqInt(HtmlElementFormOwner(&other, &ownerBefore, &source,
                                                NULL),
                           RoboOk);
            RoboCheck(HtmlNodeHandleEqual(&ownerBefore, &g));
        }
        /* Remove then append into f (a node must be detached to be re-inserted). */
        {
            HtmlNodeHandle parent;
            HtmlNodeHandle newOwner;
            HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;

            RoboCheckEqInt(HtmlNodeParent(&other, &parent, NULL), RoboOk);
            RoboCheckEqInt(HtmlNodeRemoveChild(&parent, &other, NULL), RoboOk);
            RoboCheckEqInt(HtmlNodeAppendChild(&f, &other, NULL), RoboOk);
            RoboCheckEqInt(HtmlElementFormOwner(&other, &newOwner, &source,
                                                NULL),
                           RoboOk);
            RoboCheck(HtmlNodeHandleEqual(&newOwner, &f));
        }
        RoboCheckEqInt((int)FormElementCount(&f), 8);
        RoboCheckEqInt((int)FormElementCount(&g), 0);
    }
    DomExpectCleanTree(id);

    HtmlShutdown();
}

static void TestFormValidation(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle f = FormById(id, "f");
    HtmlFormValidation report;

    /* Empty required name, unchecked required box, empty required textarea. */
    memset(&report, 0, sizeof(report));
    RoboCheckEqInt(HtmlFormValidate(&f, &report, NULL), RoboOk);
    RoboCheckEqInt(report.valid, 0);
    RoboCheckEqInt((int)report.invalid, 3);

    /* Fill them in; the form becomes valid. */
    {
        HtmlNodeHandle name = FormById(id, "name");
        HtmlNodeHandle ok = FormById(id, "ok");
        HtmlNodeHandle area = FormById(id, "t");

        RoboCheckEqInt(HtmlElementSetControlValue(&name, "Ana", 3u, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementSetControlChecked(&ok, 1, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementSetControlValue(&area, "hi", 2u, NULL),
                       RoboOk);
    }
    memset(&report, 0, sizeof(report));
    RoboCheckEqInt(HtmlFormValidate(&f, &report, NULL), RoboOk);
    RoboCheckEqInt(report.valid, 1);
    RoboCheckEqInt((int)report.invalid, 0);

    HtmlShutdown();
}

static void TestFormControlValidation(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle name = FormById(id, "name");
    HtmlNodeHandle age = FormById(id, "age");
    HtmlControlValidation check;

    /* required + minlength: empty is valueMissing, "A" is tooShort. */
    RoboCheckEqInt(HtmlElementValidateControl(&name, &check, NULL), RoboOk);
    RoboCheckEqInt(check.validated, 1);
    RoboCheckEqInt(check.valueMissing, 1);
    RoboCheckEqInt(check.valid, 0);

    RoboCheckEqInt(HtmlElementSetControlValue(&name, "A", 1u, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementValidateControl(&name, &check, NULL), RoboOk);
    RoboCheckEqInt(check.tooShort, 1);
    RoboCheckEqInt(check.valid, 0);

    RoboCheckEqInt(HtmlElementSetControlValue(&name, "Ana", 3u, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementValidateControl(&name, &check, NULL), RoboOk);
    RoboCheckEqInt(check.valid, 1);

    /* number within [1,120] is valid; out of range is not. */
    RoboCheckEqInt(HtmlElementValidateControl(&age, &check, NULL), RoboOk);
    RoboCheckEqInt(check.valid, 1);
    RoboCheckEqInt(HtmlElementSetControlValue(&age, "999", 3u, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementValidateControl(&age, &check, NULL), RoboOk);
    RoboCheckEqInt(check.aboveMax, 1);
    RoboCheckEqInt(check.valid, 0);

    /* A disabled control is exempt. */
    {
        HtmlNodeHandle other = FormById(id, "other");

        RoboCheckEqInt(HtmlElementSetAttribute(&other, "disabled", "", NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementValidateControl(&other, &check, NULL),
                       RoboOk);
        RoboCheckEqInt(check.validated, 0);
        RoboCheckEqInt(check.valid, 1);
    }

    /* A pattern is recognised and reported, never silently matched. */
    {
        HtmlNodeHandle other = FormById(id, "other");

        RoboCheckEqInt(HtmlElementRemoveAttribute(&other, "disabled", NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementSetAttribute(&other, "pattern", "[0-9]+",
                                               NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementValidateControl(&other, &check, NULL),
                       RoboOk);
        RoboCheckEqInt(check.patternUnsupported, 1);
    }

    HtmlShutdown();
}

static void TestFormSubmitRefusedAndBlocked(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle f = FormById(id, "f");
    HtmlSubmitOutcome outcome;

    /* An invalid form is blocked before any handler is consulted. */
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlFormSubmit(&f, &outcome, NULL), RoboOk);
    RoboCheckEqInt(outcome.validated, 1);
    RoboCheckEqInt(outcome.valid, 0);
    RoboCheckEqInt(outcome.requested, 0);
    RoboCheck(outcome.invalidControls > 0u);

    /* Make it valid: with no handler a submission is unsupported, not faked. */
    {
        HtmlNodeHandle name = FormById(id, "name");
        HtmlNodeHandle ok = FormById(id, "ok");
        HtmlNodeHandle area = FormById(id, "t");

        RoboCheckEqInt(HtmlElementSetControlValue(&name, "Ana", 3u, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementSetControlChecked(&ok, 1, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementSetControlValue(&area, "hi", 2u, NULL),
                       RoboOk);
    }
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlFormSubmit(&f, &outcome, NULL), RoboNotSupported);
    RoboCheckEqInt(outcome.valid, 1);
    RoboCheckEqInt(outcome.requested, 0);

    /* Install a handler; the same submission is now requested. */
    FormHandlerCalls = 0;
    RoboCheckEqInt(HtmlDocumentSetFormSubmitHandler(id, FormTestHandler, NULL,
                                                    NULL),
                   RoboOk);
    memset(&outcome, 0, sizeof(outcome));
    RoboCheckEqInt(HtmlFormSubmit(&f, &outcome, NULL), RoboOk);
    RoboCheckEqInt(outcome.valid, 1);
    RoboCheckEqInt(outcome.requested, 1);
    RoboCheckEqInt(FormHandlerCalls, 1);

    HtmlShutdown();
}

static void TestFormReset(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle f = FormById(id, "f");
    HtmlNodeHandle age = FormById(id, "age");
    HtmlNodeHandle ok = FormById(id, "ok");
    char value[64];
    size_t length = 0u;
    size_t reset = 0u;
    int checked = 0;

    /* Override the two editable states. */
    RoboCheckEqInt(HtmlElementSetControlValue(&age, "77", 2u, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementSetControlChecked(&ok, 1, NULL), RoboOk);

    RoboCheckEqInt(HtmlFormReset(&f, &reset, NULL), RoboOk);
    RoboCheck(reset >= 1u);

    /* The declared value returns. */
    RoboCheckEqInt(HtmlElementControlValue(&age, value, sizeof(value), &length,
                                           NULL, NULL),
                   RoboOk);
    value[length] = '\0';
    RoboCheck(strcmp(value, "30") == 0);

    /* The checkbox returns to its declared (unchecked) state. */
    RoboCheckEqInt(HtmlElementControlChecked(&ok, &checked, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(checked, 0);

    HtmlShutdown();
}

static void TestFormUnknownDocument(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle f = FormById(id, "f");
    HtmlFormValidation report;
    HtmlSubmitOutcome outcome;

    HtmlShutdown();
    RoboCheckEqInt(HtmlFormValidate(&f, &report, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlFormReset(&f, NULL, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlFormSubmit(&f, &outcome, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlDocumentSetFormSubmitHandler(id, FormTestHandler, NULL,
                                                    NULL),
                   RoboNotFound);
}

void TestRegisterHtmlFormTree(void)
{
    TestRegister("html.formtree.capabilities", TestFormCapabilities);
    TestRegister("html.formtree.elements", TestFormElements);
    TestRegister("html.formtree.association_move", TestFormAssociationMove);
    TestRegister("html.formtree.validation", TestFormValidation);
    TestRegister("html.formtree.control_validation", TestFormControlValidation);
    TestRegister("html.formtree.submit_refused_and_blocked",
                 TestFormSubmitRefusedAndBlocked);
    TestRegister("html.formtree.reset", TestFormReset);
    TestRegister("html.formtree.unknown_document", TestFormUnknownDocument);
}
