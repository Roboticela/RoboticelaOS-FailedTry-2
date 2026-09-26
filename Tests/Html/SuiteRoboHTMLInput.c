/*
 * RoboHTML Phase 17.3 input-control behaviour suite: text editing,
 * checkbox toggling, radio groups (with and without a form owner), number
 * stepping with clamping, and the refusal cases a disabled/read-only or
 * non-editable control must produce.
 */

#include "RoboHTML/RoboHTMLInput.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboTest.h"

#include "RoboHTMLDomTestSupport.h"

#include <stddef.h>
#include <string.h>

static const char InputMarkup[] =
    "<html><body>"
    "<form id=\"f\">"
    "<input id=\"txt\" name=\"txt\" type=\"text\" value=\"ab\">"
    "<input id=\"ro\" name=\"ro\" type=\"text\" readonly value=\"x\">"
    "<input id=\"dis\" name=\"dis\" type=\"text\" disabled>"
    "<input id=\"num\" name=\"num\" type=\"number\" value=\"5\" min=\"0\" "
    "max=\"10\" step=\"2\">"
    "<input id=\"chk\" name=\"chk\" type=\"checkbox\">"
    "<input id=\"dchk\" name=\"dchk\" type=\"checkbox\" disabled>"
    "<input id=\"r1\" name=\"ring\" type=\"radio\" value=\"1\">"
    "<input id=\"r2\" name=\"ring\" type=\"radio\" value=\"2\" checked>"
    "<textarea id=\"area\" name=\"area\">hi</textarea>"
    "</form>"
    "<input id=\"orphan1\" name=\"solo\" type=\"radio\">"
    "<input id=\"orphan2\" name=\"solo\" type=\"radio\">"
    "</body></html>";

static HtmlDocumentId InputDoc(void)
{
    return DomDocumentOf(InputMarkup);
}

static HtmlNodeHandle InputEl(HtmlDocumentId id, const char *value)
{
    HtmlNodeHandle out;

    RoboCheckEqInt(HtmlDocumentGetElementById(id, value, &out, NULL), RoboOk);
    return out;
}

static void InputValueIs(const HtmlNodeHandle *control, const char *expected)
{
    char buffer[128];
    size_t length = 0u;

    RoboCheckEqInt(HtmlElementControlValue(control, buffer, sizeof(buffer),
                                           &length, NULL, NULL),
                   RoboOk);
    buffer[length] = '\0';
    RoboCheck(strcmp(buffer, expected) == 0);
}

static void TestInputCapabilities(void)
{
    HtmlInputCapabilities caps;

    memset(&caps, 0, sizeof(caps));
    RoboCheckEqInt(HtmlInputGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.textEditing, 1);
    RoboCheckEqInt(caps.checkbox, 1);
    RoboCheckEqInt(caps.radioGroups, 1);
    RoboCheckEqInt(caps.stepping, 1);
    RoboCheckEqInt(caps.caretModel, 0);
    RoboCheckEqInt(caps.eventDispatch, 0);
    RoboCheckEqInt(HtmlInputGetCapabilities(NULL), RoboInvalidArgument);
}

static void TestInputTextEditing(void)
{
    HtmlDocumentId id = InputDoc();
    HtmlNodeHandle txt = InputEl(id, "txt");
    HtmlNodeHandle area = InputEl(id, "area");
    HtmlInputChange change;

    RoboCheckEqInt(HtmlInputIsTextEditable(&txt), 1);
    RoboCheckEqInt(HtmlInputIsTextEditable(&area), 1);
    InputValueIs(&txt, "ab");

    /* Insert appends; the change record says a value changed. */
    RoboCheckEqInt(HtmlInputInsertText(&txt, "cd", 2u, &change, NULL), RoboOk);
    RoboCheckEqInt(change.changed, 1);
    RoboCheckEqInt((int)change.kind, (int)RoboHtmlInputChangeValue);
    InputValueIs(&txt, "abcd");

    /* Backspace removes one scalar; a UTF-8 sequence is one scalar. */
    RoboCheckEqInt(HtmlInputDeleteBackward(&txt, &change, NULL), RoboOk);
    InputValueIs(&txt, "abc");
    RoboCheckEqInt(HtmlElementSetControlValue(&txt, "a\xc3\xa9", 3u, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlInputDeleteBackward(&txt, &change, NULL), RoboOk);
    InputValueIs(&txt, "a");

    /* Clear is a value change to empty. */
    RoboCheckEqInt(HtmlInputClear(&txt, &change, NULL), RoboOk);
    InputValueIs(&txt, "");

    /* Deleting at empty is a no-op, not a failure. */
    RoboCheckEqInt(HtmlInputDeleteBackward(&txt, &change, NULL), RoboOk);
    RoboCheckEqInt(change.changed, 0);

    /* readonly and disabled refuse edits. */
    {
        HtmlNodeHandle ro = InputEl(id, "ro");
        HtmlNodeHandle dis = InputEl(id, "dis");

        RoboCheckEqInt(HtmlInputInsertText(&ro, "q", 1u, NULL, NULL),
                       RoboNotSupported);
        RoboCheckEqInt(HtmlInputInsertText(&dis, "q", 1u, NULL, NULL),
                       RoboNotSupported);
    }

    /* A checkbox is not text-editable. */
    {
        HtmlNodeHandle chk = InputEl(id, "chk");

        RoboCheckEqInt(HtmlInputIsTextEditable(&chk), 0);
        RoboCheckEqInt(HtmlInputInsertText(&chk, "q", 1u, NULL, NULL),
                       RoboNotSupported);
    }

    HtmlShutdown();
}

static void TestInputCheckbox(void)
{
    HtmlDocumentId id = InputDoc();
    HtmlNodeHandle chk = InputEl(id, "chk");
    HtmlInputChange change;
    int checked = 9;

    RoboCheckEqInt(HtmlElementControlChecked(&chk, &checked, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(checked, 0);

    RoboCheckEqInt(HtmlInputToggleChecked(&chk, &change, NULL), RoboOk);
    RoboCheckEqInt(change.changed, 1);
    RoboCheckEqInt((int)change.kind, (int)RoboHtmlInputChangeChecked);
    RoboCheckEqInt(change.checked, 1);

    RoboCheckEqInt(HtmlInputSetChecked(&chk, 0, &change, NULL), RoboOk);
    RoboCheckEqInt(change.changed, 1);
    RoboCheckEqInt(change.checked, 0);

    /* Setting the same value is a no-op. */
    RoboCheckEqInt(HtmlInputSetChecked(&chk, 0, &change, NULL), RoboOk);
    RoboCheckEqInt(change.changed, 0);

    /* A control with no checkedness refuses, and so does a disabled one. */
    {
        HtmlNodeHandle dis = InputEl(id, "dis");
        HtmlNodeHandle dchk = InputEl(id, "dchk");

        RoboCheckEqInt(HtmlInputToggleChecked(&dis, NULL, NULL),
                       RoboNotSupported);
        RoboCheckEqInt(HtmlInputToggleChecked(&dchk, NULL, NULL),
                       RoboNotSupported);
    }

    HtmlShutdown();
}

static void TestInputRadioGroups(void)
{
    HtmlDocumentId id = InputDoc();
    HtmlNodeHandle r1 = InputEl(id, "r1");
    HtmlNodeHandle r2 = InputEl(id, "r2");
    HtmlNodeHandle orphan1 = InputEl(id, "orphan1");
    HtmlNodeHandle orphan2 = InputEl(id, "orphan2");
    HtmlInputChange change;
    int checked = 9;

    /* r2 starts checked. */
    RoboCheckEqInt(HtmlElementControlChecked(&r2, &checked, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(checked, 1);

    /* Selecting r1 clears r2 (same name, same form). */
    RoboCheckEqInt(HtmlInputRadioSelect(&r1, &change, NULL), RoboOk);
    RoboCheckEqInt(change.checked, 1);
    RoboCheckEqInt((int)change.groupCleared, 1);
    RoboCheckEqInt(HtmlElementControlChecked(&r1, &checked, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(checked, 1);
    RoboCheckEqInt(HtmlElementControlChecked(&r2, &checked, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(checked, 0);

    /* The unowned group (no form) still clears within its name. */
    RoboCheckEqInt(HtmlInputRadioSelect(&orphan1, &change, NULL), RoboOk);
    RoboCheckEqInt(HtmlInputRadioSelect(&orphan2, &change, NULL), RoboOk);
    RoboCheckEqInt(change.checked, 1);
    RoboCheckEqInt(HtmlElementControlChecked(&orphan1, &checked, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(checked, 0);
    RoboCheckEqInt(HtmlElementControlChecked(&orphan2, &checked, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(checked, 1);

    /* A checkbox is not a radio. */
    {
        HtmlNodeHandle chk = InputEl(id, "chk");

        RoboCheckEqInt(HtmlInputRadioSelect(&chk, NULL, NULL), RoboNotSupported);
    }

    HtmlShutdown();
}

static void TestInputStepping(void)
{
    HtmlDocumentId id = InputDoc();
    HtmlNodeHandle num = InputEl(id, "num");
    HtmlInputChange change;

    InputValueIs(&num, "5");

    /* step=2, so one step is 7. */
    RoboCheckEqInt(HtmlInputStep(&num, 1, &change, NULL), RoboOk);
    RoboCheckEqInt(change.changed, 1);
    InputValueIs(&num, "7");

    /* Three steps would be 13; max=10 clamps it. */
    RoboCheckEqInt(HtmlInputStep(&num, 3, &change, NULL), RoboOk);
    InputValueIs(&num, "10");

    /* Stepping below min clamps to min. */
    RoboCheckEqInt(HtmlInputStep(&num, -10, &change, NULL), RoboOk);
    InputValueIs(&num, "0");

    /* A non-number control is refused. */
    {
        HtmlNodeHandle txt = InputEl(id, "txt");

        RoboCheckEqInt(HtmlInputStep(&txt, 1, NULL, NULL), RoboNotSupported);
    }

    HtmlShutdown();
}

static void TestInputUnknownDocument(void)
{
    HtmlDocumentId id = InputDoc();
    HtmlNodeHandle txt = InputEl(id, "txt");
    HtmlInputChange change;

    HtmlShutdown();
    RoboCheckEqInt(HtmlInputInsertText(&txt, "x", 1u, &change, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlInputInsertText(NULL, "x", 1u, &change, NULL),
                   RoboInvalidArgument);
}

void TestRegisterHtmlInput(void)
{
    TestRegister("html.input.capabilities", TestInputCapabilities);
    TestRegister("html.input.text_editing", TestInputTextEditing);
    TestRegister("html.input.checkbox", TestInputCheckbox);
    TestRegister("html.input.radio_groups", TestInputRadioGroups);
    TestRegister("html.input.stepping", TestInputStepping);
    TestRegister("html.input.unknown_document", TestInputUnknownDocument);
}
