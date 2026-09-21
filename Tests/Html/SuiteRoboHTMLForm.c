/*
 * RoboHTML Phase 8.14 forms suite (html.form.*).
 *
 * What it pins, in the order the phase argues for it: that the seven form
 * elements are real DOM elements with real attribute views; that an
 * unrecognised `type` degrades instead of damaging the element; that the
 * declared-versus-current split behaves the way the dirty-value flag defines
 * it, *including the half that is easy to get wrong* — that an un-overridden
 * control follows its attribute and an overridden one does not; that a label
 * and a form owner are resolved from the live tree so nothing can go stale;
 * and that a malformed document cannot corrupt a lifecycle or a tree.
 *
 * Three tests are worth reading first, because they are the ones that would
 * fail if the model were quietly wrong:
 *
 *   html.form.input_value_state
 *       the declared/current split, both halves.
 *   html.form.owner_attribute
 *       the `form` attribute winning when it resolves, and falling back when
 *       it does not — plus the source field that says which rule answered.
 *   html.form.parser_malformed
 *       a document full of unclosed tags, a stray `</form>`, a void input
 *       swallowing nothing, still passes the tree invariant audit.
 *
 * The fixture parses real HTML through the real tokenizer and tree builder:
 * a test that built its elements by hand would not be testing the parser
 * integration the phase is largely about.
 */

#include "RoboBase/RoboBase.h"
#include "RoboMem/RoboMem.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLForm.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static void FormFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static HtmlDocumentId FormDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    FormFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Parse @p text into @p id through the real tokenizer and tree builder. */
static void FormBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle FormRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct formTagQuery {
    HtmlTag tag;
} FormTagQuery;

static int FormMatchTag(const HtmlNodeView *view, void *user)
{
    const FormTagQuery *q = (const FormTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static HtmlNodeHandle FormFindTag(
    const HtmlNodeHandle *start, HtmlTag tag)
{
    FormTagQuery q;
    HtmlNodeHandle out;

    q.tag = tag;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(
        HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  FormMatchTag, &q, &out, NULL),
        RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

typedef struct formIdQuery {
    const char *id;
} FormIdQuery;

static int FormMatchId(const HtmlNodeView *view, void *user)
{
    const FormIdQuery *q = (const FormIdQuery *)user;
    size_t length = 0u;
    const char *value;

    if (!view || view->kind != RoboHtmlNodeKindElement) {
        return 0;
    }
    value = HtmlElementAttributeValue(&view->handle, "id", &length);
    return value && strcmp(value, q->id) == 0;
}

/**
 * First element at or below @p start whose `id` is @p value, or an invalid
 * handle when there is none.
 *
 * Built on the library's own search so this helper and the module under test
 * agree about document order rather than each having an opinion. It does not
 * assert, because "there is no such element" is one of the states under test.
 */
static HtmlNodeHandle FormFindId(
    const HtmlNodeHandle *start, const char *value)
{
    FormIdQuery q;
    HtmlNodeHandle out;

    q.id = value;
    HtmlNodeHandleClear(&out);
    if (HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  FormMatchId, &q, &out, NULL) != RoboOk) {
        HtmlNodeHandleClear(&out);
    }
    return out;
}

/** As FormFindTag, but starting at the document root of @p id. */
static HtmlNodeHandle FormFindTagDoc(HtmlDocumentId id,
                                                 HtmlTag tag)
{
    HtmlNodeHandle root = FormRoot(id);

    return FormFindTag(&root, tag);
}

/** As FormFindId, but starting at the document root of @p id, asserting
 *  that the element is there. */
static HtmlNodeHandle FormFindIdDoc(HtmlDocumentId id,
                                                const char *value)
{
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle out = FormFindId(&root, value);

    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

typedef struct formLastOptionQuery {
    HtmlNodeHandle last;
    int found;
} FormLastOptionQuery;

static Result FormLastOptionVisit(const HtmlNodeView *view,
                                            size_t depth, void *user)
{
    FormLastOptionQuery *q = (FormLastOptionQuery *)user;

    (void)depth;
    if (view && view->kind == RoboHtmlNodeKindElement &&
        HtmlElementIsTag(&view->handle, RoboHtmlTagOption)) {
        q->last = view->handle;
        q->found = 1;
    }
    return RoboOk;
}

/**
 * The last `<option>` in @p select's subtree, in document order.
 *
 * Deliberately not a sibling walk: options can be nested in an `<optgroup>`,
 * and a sibling chain ends at its parent's last child rather than continuing
 * into the next parent. A subtree walk is the only thing that gives document
 * order here, and it is the same order the option indices use.
 */
static HtmlNodeHandle FormLastOption(
    const HtmlNodeHandle *select)
{
    FormLastOptionQuery q;

    q.found = 0;
    HtmlNodeHandleClear(&q.last);
    RoboCheckEqInt(
        HtmlNodeWalkDescendants(select, RoboHtmlNodeFilterElements,
                                        FormLastOptionVisit, &q, NULL),
        RoboOk);
    RoboCheck(q.found);
    return q.last;
}

/** The subtree text of @p node, through the DOM's own reader. */
static const char *FormText(const HtmlNodeHandle *node)
{
    static char buf[1024];
    size_t length = 0u;

    memset(buf, 0, sizeof(buf));
    RoboCheckEqInt(
        HtmlNodeSubtreeText(node, buf, sizeof(buf), &length, NULL),
        RoboOk);
    RoboCheck(length < sizeof(buf));
    return buf;
}

/** Create an element under @p parent (mutation tests). */
static HtmlNodeHandle FormCreateUnder(
    HtmlDocumentId id, const HtmlNodeHandle *parent,
    const char *name)
{
    HtmlElementCreateInfo info;
    HtmlNodeHandle out;

    HtmlElementCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = RoboHtmlNodeOriginTest;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = name;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/** Read a control's current value into a static buffer. */
static const char *FormControlValue(const HtmlNodeHandle *element,
                                      int *outIsDeclared)
{
    static char buf[1024];
    size_t length = 0u;
    int declared = 0;

    memset(buf, 0, sizeof(buf));
    RoboCheckEqInt(HtmlElementControlValue(element, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      RoboOk);
    RoboCheck(length < sizeof(buf));
    if (outIsDeclared) {
        *outIsDeclared = declared;
    }
    return buf;
}

static int nearly(double a, double b)
{
    double d = a - b;

    if (d < 0.0) {
        d = -d;
    }
    return d < 0.000001;
}

/* ------------------------------------------------------------------ */
/* Element identity and attributes                                     */
/* ------------------------------------------------------------------ */

static void TestFormParserElement(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle f;

    FormBuild(id,
               "<form action=\"/Save\" method=\"post\" "
               "enctype=\"multipart/form-data\" name=\"Editor\" "
               "target=\"_self\"><input name=\"a\"></form>");

    f = FormFindTag(&root, RoboHtmlTagForm);
    RoboCheck(HtmlElementIsTag(&f, RoboHtmlTagForm));
    RoboCheck(HtmlElementIsNamed(&f, "form"));
    RoboCheckEqInt(HtmlElementClassification(&f),
                      RoboHtmlElementClassControl);
    RoboCheckStrEq(HtmlElementAttributeValue(&f, "action", NULL),
                      "/Save");

    /* The nine elements this phase models are all real tags with the control
       role, which is the fact the parser already provided in 8.11 and this
       phase must not have broken. */
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "label") ==
               RoboHtmlTagLabel);
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "input") ==
               RoboHtmlTagInput);
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "button") ==
               RoboHtmlTagButton);
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "textarea") ==
               RoboHtmlTagTextarea);
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "select") ==
               RoboHtmlTagSelect);
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "option") ==
               RoboHtmlTagOption);
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "fieldset") ==
               RoboHtmlTagFieldset);
    RoboCheck(HtmlTagFromName(RoboHtmlNamespaceHtml, "legend") ==
               RoboHtmlTagLegend);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormFormAttributes(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlFormAttributes attrs;
    HtmlNodeHandle f;

    FormBuild(id,
               "<form action=\"/Save\" method=\"POST\" "
               "enctype=\"multipart/form-data\" name=\"Editor\" "
               "target=\"Frame\"></form>");
    f = FormFindTag(&root, RoboHtmlTagForm);
    RoboCheckEqInt(HtmlElementFormAttributes(&f, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.actionPresent && attrs.actionUsable);
    RoboCheckStrEq(attrs.action, "/Save");
    RoboCheckEqInt(attrs.actionLength, 5);
    RoboCheckEqInt(attrs.methodKind, RoboHtmlFormMethodPost);
    RoboCheckEqInt(attrs.enctypeKind,
                      RoboHtmlFormEnctypeMultipart);
    RoboCheckStrEq(attrs.name, "Editor");
    RoboCheckStrEq(attrs.target, "Frame");

    /* `<form action>` and `<form action="  ">` name nothing, and that is a
       different fact from "no attribute at all". */
    FormBuild(id, "<form action=\"  \"></form>");
    f = FormFindTagDoc(id, RoboHtmlTagForm);
    RoboCheckEqInt(HtmlElementFormAttributes(&f, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.actionPresent);
    RoboCheck(!attrs.actionUsable);
    RoboCheckEqInt(attrs.methodKind, RoboHtmlFormMethodNone);
    RoboCheckEqInt(attrs.enctypeKind, RoboHtmlFormEnctypeNone);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormMethodEnctypeVocabulary(void)
{
    /* Absent or blank is NONE; a name we do not know is UNKNOWN. The two are
       different answers, and collapsing them would make "the author said
       something odd" indistinguishable from "the author said nothing". */
    RoboCheckEqInt(HtmlFormMethodFromName(NULL, 0u),
                      RoboHtmlFormMethodNone);
    RoboCheckEqInt(HtmlFormMethodFromName("", 0u),
                      RoboHtmlFormMethodNone);
    RoboCheckEqInt(HtmlFormMethodFromName("   ", 3u),
                      RoboHtmlFormMethodNone);
    RoboCheckEqInt(HtmlFormMethodFromName("get", 3u),
                      RoboHtmlFormMethodGet);
    RoboCheckEqInt(HtmlFormMethodFromName("POST", 4u),
                      RoboHtmlFormMethodPost);
    RoboCheckEqInt(HtmlFormMethodFromName(" dialog ", 8u),
                      RoboHtmlFormMethodDialog);
    RoboCheckEqInt(HtmlFormMethodFromName("patch", 5u),
                      RoboHtmlFormMethodUnknown);
    RoboCheckStrEq(HtmlFormMethodName(RoboHtmlFormMethodGet),
                      "get");
    RoboCheckStrEq(HtmlFormMethodName(
                          RoboHtmlFormMethodUnknown),
                      "unknown");
    RoboCheck(HtmlFormMethodIsKnown(RoboHtmlFormMethodDialog));
    RoboCheck(!HtmlFormMethodIsKnown(
        (HtmlFormMethod)99));

    RoboCheckEqInt(
        HtmlFormEnctypeFromName("application/x-www-form-urlencoded",
                                         33u),
        RoboHtmlFormEnctypeUrlencoded);
    RoboCheckEqInt(HtmlFormEnctypeFromName("MULTIPART/FORM-DATA",
                                                      19u),
                      RoboHtmlFormEnctypeMultipart);
    RoboCheckEqInt(HtmlFormEnctypeFromName("text/plain", 10u),
                      RoboHtmlFormEnctypeTextPlain);
    RoboCheckEqInt(HtmlFormEnctypeFromName("application/json",
                                                      16u),
                      RoboHtmlFormEnctypeUnknown);
    RoboCheckEqInt(HtmlFormEnctypeFromName("", 0u),
                      RoboHtmlFormEnctypeNone);
    RoboCheckStrEq(
        HtmlFormEnctypeName(RoboHtmlFormEnctypeMultipart),
        "multipart/form-data");
}

static void TestFormNonFormRefused(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlFormAttributes attrs;
    HtmlInputAttributes inputs;
    HtmlNodeHandle div;
    HtmlNodeHandle stale;

    FormBuild(id, "<div id=\"d\"><input id=\"i\"></div>");
    div = FormFindTag(&root, RoboHtmlTagDiv);
    RoboCheckEqInt(HtmlElementFormAttributes(&div, &attrs, NULL),
                      RoboNotSupported);
    RoboCheckEqInt(HtmlElementInputAttributes(&div, &inputs, NULL),
                      RoboNotSupported);
    /* NULL and unknown handles are argument/liveness problems, not "wrong
       element type" ones. */
    RoboCheckEqInt(HtmlElementFormAttributes(NULL, &attrs, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementFormAttributes(&div, NULL, NULL),
                      RoboInvalidArgument);

    stale.documentId = id;
    stale.nodeId = 999999u;
    stale.generation = 1u;
    RoboCheckEqInt(HtmlElementFormAttributes(&stale, &attrs, NULL),
                      RoboNotFound);

    /* The whole tree was zeroed by the refusals: a failed read must not leave
       a partially filled view behind. */
    RoboCheck(!attrs.actionPresent);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Input types                                                         */
/* ------------------------------------------------------------------ */

static void TestFormInputTypes(void)
{
    /* The vocabulary, and the degradation that makes "unknown" safe. */
    RoboCheckEqInt(HtmlInputTypeFromName(NULL, 0u),
                      RoboHtmlInputTypeText);
    RoboCheckEqInt(HtmlInputTypeFromName("", 0u),
                      RoboHtmlInputTypeText);
    RoboCheckEqInt(HtmlInputTypeFromName("TEXT", 4u),
                      RoboHtmlInputTypeText);
    RoboCheckEqInt(HtmlInputTypeFromName("password", 8u),
                      RoboHtmlInputTypePassword);
    RoboCheckEqInt(HtmlInputTypeFromName("checkbox", 8u),
                      RoboHtmlInputTypeCheckbox);
    RoboCheckEqInt(HtmlInputTypeFromName("radio", 5u),
                      RoboHtmlInputTypeRadio);
    RoboCheckEqInt(HtmlInputTypeFromName("number", 6u),
                      RoboHtmlInputTypeNumber);
    RoboCheckEqInt(HtmlInputTypeFromName("email", 5u),
                      RoboHtmlInputTypeEmail);
    RoboCheckEqInt(HtmlInputTypeFromName("submit", 6u),
                      RoboHtmlInputTypeSubmit);
    RoboCheckEqInt(HtmlInputTypeFromName("button", 6u),
                      RoboHtmlInputTypeButton);
    RoboCheckEqInt(HtmlInputTypeFromName("range", 5u),
                      RoboHtmlInputTypeUnknown);
    RoboCheckEqInt(HtmlInputTypeFromName("color", 5u),
                      RoboHtmlInputTypeUnknown);

    /* The degradation rule itself, and that it never returns UNKNOWN. */
    RoboCheckEqInt(
        HtmlInputTypeEffective(RoboHtmlInputTypeUnknown),
        RoboHtmlInputTypeText);
    RoboCheckEqInt(
        HtmlInputTypeEffective(RoboHtmlInputTypeCheckbox),
        RoboHtmlInputTypeCheckbox);
    RoboCheckEqInt(
        HtmlInputTypeEffective((HtmlInputType)500),
        RoboHtmlInputTypeText);
    RoboCheck(HtmlInputTypeIsCheckable(
        RoboHtmlInputTypeCheckbox));
    RoboCheck(HtmlInputTypeIsCheckable(RoboHtmlInputTypeRadio));
    RoboCheck(!HtmlInputTypeIsCheckable(RoboHtmlInputTypeText));
    RoboCheckStrEq(HtmlInputTypeName(RoboHtmlInputTypeNumber),
                      "number");
    RoboCheckStrEq(HtmlInputTypeName(RoboHtmlInputTypeUnknown),
                      "unknown");

    /* An unrecognised type in a real document: the element keeps everything it
       has, and reports the type it actually behaves as. */
    {
        HtmlDocumentId id = FormDoc();
        HtmlNodeHandle root = FormRoot(id);
        HtmlInputAttributes attrs;
        HtmlNodeHandle input;

        FormBuild(id, "<input id=\"i\" type=\"range\" name=\"r\" value=\"7\">");
        input = FormFindTag(&root, RoboHtmlTagInput);
        RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs,
                                                             NULL),
                          RoboOk);
        RoboCheckEqInt(attrs.typeDeclared, RoboHtmlInputTypeUnknown);
        RoboCheckEqInt(attrs.typeEffective, RoboHtmlInputTypeText);
        RoboCheck(!attrs.typeRecognised);
        /* Degraded, not damaged: name and value are intact, and so is the
           element's place in the tree. */
        RoboCheckStrEq(attrs.name, "r");
        RoboCheckStrEq(attrs.value, "7");
        RoboCheck(HtmlElementIsTag(&input, RoboHtmlTagInput));
        RoboCheckStrEq(HtmlElementAttributeValue(&input, "type",
                                                            NULL),
                          "range");

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
}

static void TestFormInputAttributes(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlInputAttributes attrs;
    HtmlNodeHandle input;

    FormBuild(id,
               "<input id=\"e\" type=\"email\" name=\"email\" "
               "value=\"user@example.com\" placeholder=\"you@example.com\" "
               "disabled required readonly>");
    input = FormFindTag(&root, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.typeEffective, RoboHtmlInputTypeEmail);
    RoboCheck(attrs.typeRecognised);
    RoboCheckStrEq(attrs.name, "email");
    RoboCheckStrEq(attrs.value, "user@example.com");
    RoboCheckStrEq(attrs.placeholder, "you@example.com");
    /* Boolean attributes are presence tests, so a bare attribute and an
       attribute with any value agree. */
    RoboCheck(attrs.disabled);
    RoboCheck(attrs.required);
    RoboCheck(attrs.readonly);
    RoboCheck(!attrs.checked);

    /* `<input checked>` and `<input checked="checked">` agree; `<input>` and
       `<input value="">` do not, which is why `valuePresent` exists. */
    FormBuild(id, "<input checked value=\"\"><input>");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.checked);
    RoboCheck(attrs.valuePresent);
    RoboCheckEqInt(attrs.valueLength, 0u);
    RoboCheck(HtmlElementHasAttribute(&input, "value"));

    RoboCheckEqInt(HtmlElementSetAttribute(&input, "checked",
                                                      "anything", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.checked);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Declared vs current value — the heart of the phase                  */
/* ------------------------------------------------------------------ */

static void TestFormInputValueState(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle input;
    int declared = 0;

    FormBuild(id, "<input id=\"a\" name=\"a\" value=\"declared\">");
    input = FormFindTag(&root, RoboHtmlTagInput);

    /* No override: the current value *is* the declared one, and the flag says
       so rather than the caller having to guess from the bytes. */
    RoboCheckStrEq(FormControlValue(&input, &declared), "declared");
    RoboCheck(declared);

    /* Pin an override. The attribute is untouched — the declared value is
       still there to be read, which is what a form reset restores. */
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, "current", 7u, NULL),
        RoboOk);
    RoboCheckStrEq(FormControlValue(&input, &declared), "current");
    RoboCheck(!declared);
    RoboCheckStrEq(HtmlElementAttributeValue(&input, "value", NULL),
                      "declared");

    /* This is the half that is easy to get wrong, and it is the entire
       content of the dirty-value flag: an *overridden* control does not
       follow its attribute... */
    RoboCheckEqInt(HtmlElementSetAttribute(&input, "value",
                                                      "changed", NULL),
                      RoboOk);
    RoboCheckStrEq(FormControlValue(&input, &declared), "current");
    RoboCheck(!declared);

    /* ...and an un-overridden one does. */
    {
        HtmlInputAttributes attrs;

        FormBuild(id, "<input id=\"b\" value=\"first\">");
        input = FormFindTagDoc(id, RoboHtmlTagInput);
        RoboCheckStrEq(FormControlValue(&input, NULL), "first");
        RoboCheckEqInt(HtmlElementSetAttribute(&input, "value",
                                                          "second", NULL),
                          RoboOk);
        RoboCheckStrEq(FormControlValue(&input, &declared), "second");
        RoboCheck(declared);
        RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs,
                                                             NULL),
                          RoboOk);
        RoboCheckStrEq(attrs.value, "second");
    }

    /* Clearing the override returns the control to tracking the attribute —
       and clearing when there is nothing to clear is a success, because a
       form reset must not fail on a field nobody edited. */
    FormBuild(id, "<input id=\"c\" value=\"declared\">");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, "set", 3u, NULL), RoboOk);
    RoboCheckStrEq(FormControlValue(&input, NULL), "set");
    RoboCheckEqInt(HtmlElementClearControlValue(&input, NULL),
                      RoboOk);
    RoboCheckStrEq(FormControlValue(&input, &declared), "declared");
    RoboCheck(declared);
    RoboCheckEqInt(HtmlElementClearControlValue(&input, NULL),
                      RoboOk);

    /* An empty override is a real state, distinct from "no override": the
       field was cleared, and a reset brings the declared value back. */
    RoboCheckEqInt(HtmlElementSetControlValue(&input, "", 0u, NULL),
                      RoboOk);
    RoboCheckStrEq(FormControlValue(&input, &declared), "");
    RoboCheck(!declared);
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, NULL, 0u, NULL), RoboOk);
    RoboCheckStrEq(FormControlValue(&input, NULL), "");
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, NULL, 4u, NULL),
        RoboInvalidArgument);

    /* A value without a `value` attribute at all: the declared value is the
       empty string, not an error. */
    FormBuild(id, "<input id=\"d\">");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckStrEq(FormControlValue(&input, &declared), "");
    RoboCheck(declared);

    /* Wrong element: a div has no value to hold. */
    FormBuild(id, "<div id=\"e\">x</div>");
    {
        HtmlNodeHandle div =
            FormFindTagDoc(id, RoboHtmlTagDiv);
        char buf[8];
        size_t length = 0u;

        RoboCheckEqInt(HtmlElementControlValue(&div, buf,
                                                          sizeof(buf), &length,
                                                          NULL, NULL),
                          RoboNotSupported);
        RoboCheckEqInt(
            HtmlElementSetControlValue(&div, "x", 1u, NULL),
            RoboNotSupported);
        RoboCheckEqInt(HtmlElementControlValue(&input, NULL, 0u,
                                                          &length, NULL, NULL),
                          RoboInvalidArgument);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormInputCheckedState(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle box;
    int checked = 0;
    int declared = 0;

    FormBuild(id,
               "<input id=\"a\" type=\"checkbox\" name=\"news\" checked>"
               "<input id=\"b\" type=\"radio\" name=\"r\">");
    box = FormFindTag(&root, RoboHtmlTagInput);

    RoboCheckEqInt(HtmlElementControlChecked(&box, &checked,
                                                        &declared, NULL),
                      RoboOk);
    RoboCheck(checked);
    RoboCheck(declared);

    /* The override is independent of the attribute, exactly as in HTML: the
       attribute still says checked, the control does not. */
    RoboCheckEqInt(HtmlElementSetControlChecked(&box, 0, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementControlChecked(&box, &checked,
                                                        &declared, NULL),
                      RoboOk);
    RoboCheck(!checked);
    RoboCheck(!declared);
    RoboCheck(HtmlElementHasAttribute(&box, "checked"));

    /* Removing the attribute does *not* resurrect the override, and clearing
       the override returns the control to following the attribute. */
    RoboCheckEqInt(HtmlElementRemoveAttribute(&box, "checked",
                                                         NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementControlChecked(&box, &checked,
                                                        &declared, NULL),
                      RoboOk);
    RoboCheck(!checked);
    RoboCheck(!declared);
    RoboCheckEqInt(HtmlElementClearControlChecked(&box, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementControlChecked(&box, &checked,
                                                        &declared, NULL),
                      RoboOk);
    RoboCheck(!checked);
    RoboCheck(declared);

    /* Setting a value never disturbs checkedness, and vice versa. */
    FormBuild(id, "<input id=\"c\" type=\"checkbox\" checked value=\"v\">");
    box = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(
        HtmlElementSetControlValue(&box, "w", 1u, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementControlChecked(&box, &checked,
                                                        &declared, NULL),
                      RoboOk);
    RoboCheck(checked && declared);
    RoboCheckEqInt(HtmlElementSetControlChecked(&box, 0, NULL),
                      RoboOk);
    RoboCheckStrEq(FormControlValue(&box, &declared), "w");
    RoboCheck(!declared);

    /* Checkedness is modelled for checkable inputs only, so a text input has
       an attribute that is preserved and no checkedness to report — two
       honest facts instead of one misleading one. */
    FormBuild(id, "<input id=\"d\" type=\"text\" checked>");
    box = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementControlChecked(&box, &checked, NULL,
                                                        NULL),
                      RoboNotSupported);
    RoboCheckEqInt(HtmlElementSetControlChecked(&box, 1, NULL),
                      RoboNotSupported);
    RoboCheckEqInt(HtmlElementClearControlChecked(&box, NULL),
                      RoboNotSupported);

    /* And a non-input is refused for the same reason, with the same code, so
       "not an input" and "an input that cannot be checked" are both
       NOT_SUPPORTED rather than one of them being an error. */
    FormBuild(id, "<select id=\"s\"></select>");
    {
        HtmlNodeHandle select =
            FormFindTagDoc(id, RoboHtmlTagSelect);

        RoboCheckEqInt(HtmlElementControlChecked(&select, &checked,
                                                            NULL, NULL),
                          RoboNotSupported);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormInputNumericConstraints(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlInputAttributes attrs;
    HtmlNodeHandle input;

    FormBuild(id,
               "<input id=\"a\" type=\"number\" min=\"0\" max=\"10.5\" "
               "step=\"0.5\">");
    input = FormFindTag(&root, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.minPresent && attrs.minValid);
    RoboCheck(nearly(attrs.minValue, 0.0));
    RoboCheck(attrs.maxPresent && attrs.maxValid);
    RoboCheck(nearly(attrs.maxValue, 10.5));
    RoboCheck(attrs.stepPresent && attrs.stepValid);
    RoboCheck(nearly(attrs.stepValue, 0.5));

    /* Negative and signed values, and whitespace around the value. */
    FormBuild(id, "<input min=\"-2.25\" max=\" +3 \" step=\"1\">");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.minValid);
    RoboCheck(nearly(attrs.minValue, -2.25));
    RoboCheck(attrs.maxValid);
    RoboCheck(nearly(attrs.maxValue, 3.0));

    /* Everything a strict decimal refuses. Each is reported as present *and*
       invalid rather than silently dropped, because the document said
       something and a later validation phase needs to know it did. */
    {
        static const char *const bad[] = {
            "abc", "1.", ".", "+", "", "   ", "1e3", "0x10", "1,5", "nan",
            "inf", "--1", "1-", "1 2", "٣",
        };
        size_t i;

        for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
            char markup[128];

            RoboCheckEqInt(
                snprintf(markup, sizeof(markup),
                         "<input type=\"number\" min=\"%s\">", bad[i]) > 0, 1);
            FormBuild(id, markup);
            input = FormFindTagDoc(id, RoboHtmlTagInput);
            RoboCheckEqInt(HtmlElementInputAttributes(&input,
                                                                 &attrs, NULL),
                              RoboOk);
            RoboCheck(attrs.minPresent);
            RoboCheckMsg(!attrs.minValid, bad[i]);
            /* Present but invalid must not leave a stale parsed value
               behind. */
            RoboCheck(nearly(attrs.minValue, 0.0));
        }
    }

    /* The fifteen-significant-digit ceiling: at it the value is exact, past it
       the number is refused rather than silently rounded to something else. */
    FormBuild(id, "<input min=\"999999999999999\">");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.minValid);
    RoboCheck(nearly(attrs.minValue, 999999999999999.0));

    FormBuild(id, "<input min=\"9999999999999999\">");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.minPresent);
    RoboCheck(!attrs.minValid);

    /* Leading zeros do not count toward the ceiling, so this is 1.5, not a
       seventeen-digit number. */
    FormBuild(id, "<input min=\"00000000000000001.5\">");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &attrs, NULL),
                      RoboOk);
    RoboCheck(attrs.minValid);
    RoboCheck(nearly(attrs.minValue, 1.5));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Button / textarea / select                                          */
/* ------------------------------------------------------------------ */

static void TestFormButton(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlButtonAttributes attrs;
    HtmlNodeHandle button;
    int declared = 0;

    FormBuild(id,
               "<button type=\"submit\" name=\"save\" value=\"1\" "
               "disabled>Save</button>");
    button = FormFindTag(&root, RoboHtmlTagButton);
    RoboCheckEqInt(HtmlElementButtonAttributes(&button, &attrs,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.typeDeclared, RoboHtmlButtonTypeSubmit);
    RoboCheck(attrs.typeRecognised);
    RoboCheckStrEq(attrs.name, "save");
    RoboCheckStrEq(attrs.value, "1");
    RoboCheck(attrs.disabled);

    /* An absent type is the declared default (submit) and not a degradation;
       an unrecognised one degrades to submit and says that it did. */
    FormBuild(id, "<button>B</button>");
    button = FormFindTagDoc(id, RoboHtmlTagButton);
    RoboCheckEqInt(HtmlElementButtonAttributes(&button, &attrs,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.typeDeclared, RoboHtmlButtonTypeSubmit);
    RoboCheck(attrs.typeRecognised);
    RoboCheck(!attrs.typePresent);

    FormBuild(id, "<button type=\"menu\">B</button>");
    button = FormFindTagDoc(id, RoboHtmlTagButton);
    RoboCheckEqInt(HtmlElementButtonAttributes(&button, &attrs,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(attrs.typeDeclared, RoboHtmlButtonTypeUnknown);
    RoboCheckEqInt(attrs.typeEffective, RoboHtmlButtonTypeSubmit);
    RoboCheck(!attrs.typeRecognised);
    RoboCheckStrEq(HtmlElementAttributeValue(&button, "type", NULL),
                      "menu");

    /* Reset and plain_button are recognised, and the vocabulary's own
       degradation is asserted directly too. */
    RoboCheckEqInt(HtmlButtonTypeFromName("RESET", 5u),
                      RoboHtmlButtonTypeReset);
    RoboCheckEqInt(HtmlButtonTypeFromName("button", 6u),
                      RoboHtmlButtonTypeButton);
    RoboCheckEqInt(HtmlButtonTypeFromName("menu", 4u),
                      RoboHtmlButtonTypeUnknown);
    RoboCheckEqInt(HtmlButtonTypeFromName(NULL, 0u),
                      RoboHtmlButtonTypeSubmit);
    RoboCheckEqInt(HtmlButtonTypeEffective(
                          RoboHtmlButtonTypeUnknown),
                      RoboHtmlButtonTypeSubmit);
    RoboCheckEqInt(HtmlButtonTypeEffective(
                          RoboHtmlButtonTypeReset),
                      RoboHtmlButtonTypeReset);
    RoboCheckStrEq(HtmlButtonTypeName(RoboHtmlButtonTypeReset),
                      "reset");
    RoboCheck(!HtmlButtonTypeIsKnown(
        (HtmlButtonType)77));

    /* A button holds a value like any other control, with the same declared /
       current split, and its text is its children — not a value. */
    FormBuild(id, "<button type=\"button\" value=\"declared\">Label</button>");
    button = FormFindTagDoc(id, RoboHtmlTagButton);
    RoboCheckStrEq(FormControlValue(&button, &declared), "declared");
    RoboCheck(declared);
    RoboCheckEqInt(
        HtmlElementSetControlValue(&button, "current", 7u, NULL),
        RoboOk);
    RoboCheckStrEq(FormControlValue(&button, &declared), "current");
    RoboCheck(!declared);
    RoboCheckStrEq(FormText(&button), "Label");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormTextarea(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlTextareaAttributes attrs;
    HtmlNodeHandle area;
    int declared = 0;

    FormBuild(id,
               "<textarea name=\"message\" rows=\"4\" cols=\"40\" "
               "placeholder=\"say something\" readonly>Hello</textarea>");
    area = FormFindTag(&root, RoboHtmlTagTextarea);
    RoboCheckEqInt(HtmlElementTextareaAttributes(&area, &attrs,
                                                            NULL),
                      RoboOk);
    RoboCheckStrEq(attrs.name, "message");
    RoboCheck(attrs.rowsValid);
    RoboCheckEqInt(attrs.rowsValue, 4u);
    RoboCheck(attrs.colsValid);
    RoboCheckEqInt(attrs.colsValue, 40u);
    RoboCheckStrEq(attrs.placeholder, "say something");
    RoboCheck(attrs.readonly);
    RoboCheck(!attrs.disabled);
    RoboCheck(!attrs.required);

    /* The declared value is the element's own text — a textarea has no
       `value` attribute, and inventing one here would model something the
       element does not have. */
    RoboCheckStrEq(FormControlValue(&area, &declared), "Hello");
    RoboCheck(declared);
    RoboCheckStrEq(FormText(&area), "Hello");

    /* The current value can differ, and the override does not touch the
       children. */
    RoboCheckEqInt(
        HtmlElementSetControlValue(&area, "Edited", 6u, NULL),
        RoboOk);
    RoboCheckStrEq(FormControlValue(&area, &declared), "Edited");
    RoboCheck(!declared);
    RoboCheckStrEq(FormText(&area), "Hello");
    RoboCheckEqInt(HtmlElementClearControlValue(&area, NULL),
                      RoboOk);
    RoboCheckStrEq(FormControlValue(&area, &declared), "Hello");
    RoboCheck(declared);

    /* rows/cols are non-negative integers: a sign, a fraction or a word is
       invalid rather than defaulted, and a huge value is refused rather than
       clamped. */
    {
        static const char *const bad[] = { "abc", "-1", "1.5", "", " ",
                                           "1e3", "99999999" };
        size_t i;

        for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
            char markup[128];

            RoboCheckEqInt(
                snprintf(markup, sizeof(markup),
                         "<textarea rows=\"%s\"></textarea>", bad[i]) > 0, 1);
            FormBuild(id, markup);
            area = FormFindTagDoc(id, RoboHtmlTagTextarea);
            RoboCheckEqInt(HtmlElementTextareaAttributes(&area,
                                                                   &attrs,
                                                                   NULL),
                              RoboOk);
            RoboCheck(attrs.rowsPresent);
            RoboCheckMsg(!attrs.rowsValid, bad[i]);
            RoboCheckEqInt(attrs.rowsValue, 0u);
        }
    }

    /* The optional closing tag: `<textarea>` is RCDATA, so the parser knows
       where its content ends and a following element is a sibling. */
    FormBuild(id, "<div><textarea>t</textarea><span>s</span></div>");
    {
        HtmlNodeHandle div =
            FormFindTagDoc(id, RoboHtmlTagDiv);
        HtmlNodeHandle span =
            FormFindTag(&div, RoboHtmlTagSpan);
        HtmlNodeHandle area2 =
            FormFindTag(&div, RoboHtmlTagTextarea);

        RoboCheck(HtmlNodeHandleIsWellformed(&span));
        RoboCheck(!HtmlNodeHandleEqual(&span, &area2));
        RoboCheckStrEq(FormText(&area2), "t");
        RoboCheckStrEq(FormText(&span), "s");
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormSelectOptions(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlSelectAttributes sattrs;
    HtmlOptionAttributes oattrs;
    HtmlNodeHandle select;
    HtmlNodeHandle first;
    HtmlNodeHandle second;
    size_t count = 0u;
    size_t index = 0u;

    FormBuild(id,
               "<select name=\"country\" multiple required>"
               "<option value=\"pt\">Portugal</option>"
               "<option value=\"us\" selected>United States</option>"
               "<option disabled>Disabled</option>"
               "</select>");
    select = FormFindTag(&root, RoboHtmlTagSelect);
    RoboCheckEqInt(HtmlElementSelectAttributes(&select, &sattrs,
                                                          NULL),
                      RoboOk);
    RoboCheckStrEq(sattrs.name, "country");
    RoboCheck(sattrs.multiple);
    RoboCheck(sattrs.required);
    RoboCheck(!sattrs.disabled);

    RoboCheckEqInt(HtmlElementSelectOptionCount(&select, &count,
                                                            NULL),
                      RoboOk);
    RoboCheckEqInt(count, 3u);

    first = FormFindTag(&select, RoboHtmlTagOption);
    RoboCheckEqInt(HtmlElementOptionAttributes(&first, &oattrs,
                                                          NULL),
                      RoboOk);
    RoboCheckStrEq(oattrs.value, "pt");
    RoboCheck(!oattrs.selected);
    RoboCheckEqInt(HtmlElementOptionIndex(&first, &index, NULL),
                      RoboOk);
    RoboCheckEqInt(index, 0u);

    /* Ordering is document order, so index 1 is the second option — the one
       the markup marked as selected. */
    {
        HtmlOptionAttributes a2;
        HtmlNodeHandle node = first;
        HtmlNodeHandle sibling;

        RoboCheckEqInt(HtmlNodeNextSibling(&node, &sibling, NULL),
                          RoboOk);
        second = sibling;
        RoboCheckEqInt(HtmlElementOptionAttributes(&second, &a2,
                                                              NULL),
                          RoboOk);
        RoboCheckStrEq(a2.value, "us");
        RoboCheck(a2.selected);
        RoboCheckStrEq(FormText(&second), "United States");
        RoboCheckEqInt(HtmlElementOptionIndex(&second, &index,
                                                         NULL),
                          RoboOk);
        RoboCheckEqInt(index, 1u);
    }

    /* The selected option is the first one marked selected. */
    {
        HtmlNodeHandle picked;

        RoboCheckEqInt(HtmlElementSelectSelectedOption(&select,
                                                                   &picked,
                                                                   NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&picked, &second));
    }

    /* With nothing marked selected, the first option that is not disabled
       answers — a disabled first option is a state a user could never
       reach. */
    FormBuild(id,
               "<select><option disabled>a</option><option>b</option>"
               "<option>c</option></select>");
    select = FormFindTagDoc(id, RoboHtmlTagSelect);
    {
        HtmlNodeHandle picked;
        HtmlNodeHandle expect;

        RoboCheckEqInt(HtmlElementSelectSelectedOption(&select,
                                                                   &picked,
                                                                   NULL),
                          RoboOk);
        expect = FormFindTag(&select, RoboHtmlTagOption);
        RoboCheck(!HtmlNodeHandleEqual(&picked, &expect));
        RoboCheckStrEq(FormText(&picked), "b");
    }

    /* An empty select selects nothing, which is a valid empty answer rather
       than an error. */
    FormBuild(id, "<select></select>");
    select = FormFindTagDoc(id, RoboHtmlTagSelect);
    {
        HtmlNodeHandle picked;

        RoboCheckEqInt(HtmlElementSelectSelectedOption(&select,
                                                                   &picked,
                                                                   NULL),
                          RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&picked));
        RoboCheckEqInt(HtmlElementSelectOptionCount(&select,
                                                                &count, NULL),
                          RoboOk);
        RoboCheckEqInt(count, 0u);
    }

    /* An option outside a select has no index: 0 would be a lie that looks
       like a position. */
    FormBuild(id, "<option>loose</option>");
    {
        HtmlNodeHandle loose =
            FormFindTagDoc(id, RoboHtmlTagOption);

        RoboCheckEqInt(HtmlElementOptionIndex(&loose, &index, NULL),
                          RoboNotFound);
        RoboCheckEqInt(HtmlElementSelectSelectedOption(
                              &loose, &loose, NULL),
                          RoboNotSupported);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormOptionValueFallback(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle option;
    char buf[128];
    size_t length = 0u;
    int explicitValue = 0;

    FormBuild(id,
               "<select>"
               "<option value=\"pt\">Portugal</option>"
               "<option>United Kingdom</option>"
               "<option value=\"\">Empty</option>"
               "</select>");
    option = FormFindTag(&root, RoboHtmlTagOption);

    RoboCheckEqInt(HtmlElementOptionValue(&option, buf,
                                                     sizeof(buf), &length,
                                                     &explicitValue, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "pt");
    RoboCheck(explicitValue);

    /* The fallback is the real rule: an option with no `value` submits its
       text, and the flag says the text *is* the value rather than merely
       looking like it. */
    {
        HtmlNodeHandle sibling;

        RoboCheckEqInt(HtmlNodeNextSibling(&option, &sibling, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementOptionValue(&sibling, buf,
                                                         sizeof(buf), &length,
                                                         &explicitValue,
                                                         NULL),
                          RoboOk);
        RoboCheckStrEq(buf, "United Kingdom");
        RoboCheck(!explicitValue);

        /* An explicit empty value is not the same as no value at all. */
        RoboCheckEqInt(HtmlNodeNextSibling(&sibling, &option, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementOptionValue(&option, buf,
                                                         sizeof(buf), &length,
                                                         &explicitValue,
                                                         NULL),
                          RoboOk);
        RoboCheckStrEq(buf, "");
        RoboCheck(explicitValue);
    }

    /* Buffer truncation reports the untruncated length, so a caller can tell
       a short value from a lost one. */
    FormBuild(id, "<select><option>0123456789</option></select>");
    option = FormFindTagDoc(id, RoboHtmlTagOption);
    RoboCheckEqInt(HtmlElementOptionValue(&option, buf, 4u, &length,
                                                     NULL, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "012");
    RoboCheckEqInt(length, 10u);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Relations                                                           */
/* ------------------------------------------------------------------ */

static void TestFormLabelTarget(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle label;
    HtmlNodeHandle target;
    int fromAttribute = 0;

    /* Rule 1: `for` names an id and that element is labelable. */
    FormBuild(id,
               "<label for=\"Email\">Email</label>"
               "<input id=\"Email\" type=\"email\">"
               "<div id=\"NotAControl\"></div>");
    label = FormFindTag(&root, RoboHtmlTagLabel);
    RoboCheckEqInt(HtmlElementLabelTarget(&label, &target,
                                                     &fromAttribute, NULL),
                      RoboOk);
    RoboCheck(HtmlElementIsTag(&target, RoboHtmlTagInput));
    RoboCheck(fromAttribute);
    RoboCheckStrEq(HtmlElementAttributeValue(&target, "id", NULL),
                      "Email");

    /* Rule 2: no `for`, so the first labelable descendant answers. */
    FormBuild(id, "<label>Name <input id=\"inner\"></label>");
    label = FormFindTagDoc(id, RoboHtmlTagLabel);
    RoboCheckEqInt(HtmlElementLabelTarget(&label, &target,
                                                     &fromAttribute, NULL),
                      RoboOk);
    RoboCheck(fromAttribute == 0);
    RoboCheckStrEq(HtmlElementAttributeValue(&target, "id", NULL),
                      "inner");

    /* Rule 3: a label wrapping only text labels nothing, which is a valid
       state and not an error. */
    FormBuild(id, "<label>Just text</label>");
    label = FormFindTagDoc(id, RoboHtmlTagLabel);
    RoboCheckEqInt(HtmlElementLabelTarget(&label, &target,
                                                     &fromAttribute, NULL),
                      RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&target));
    RoboCheckEqInt(fromAttribute, 0);

    /* A usable `for` that names a non-labelable element is *not* a fallback
       chain: the author named something, and the honest answer is that the
       name does not resolve to a control. The wrapped input is deliberately
       not reported. */
    FormBuild(id,
               "<label for=\"NotAControl\"><input id=\"wrapped\"></label>"
               "<div id=\"NotAControl\"></div>");
    label = FormFindTagDoc(id, RoboHtmlTagLabel);
    RoboCheckEqInt(HtmlElementLabelTarget(&label, &target,
                                                     &fromAttribute, NULL),
                      RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&target));
    RoboCheckEqInt(fromAttribute, 0);

    /* A usable `for` that names nothing at all behaves the same way. */
    FormBuild(id,
               "<label for=\"Ghost\"><input id=\"wrapped\"></label>");
    label = FormFindTagDoc(id, RoboHtmlTagLabel);
    RoboCheckEqInt(HtmlElementLabelTarget(&label, &target,
                                                     &fromAttribute, NULL),
                      RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&target));

    /* An empty `for` is not a usable reference, so the descendant rule
       answers. */
    FormBuild(id,
               "<label for=\"\" id=\"l2\"><input id=\"w\"></label>"
               "<div id=\"d\"></div>");
    label = FormFindIdDoc(id, "l2");
    RoboCheckEqInt(HtmlElementLabelTarget(&label, &target,
                                                     &fromAttribute, NULL),
                      RoboOk);
    RoboCheckStrEq(HtmlElementAttributeValue(&target, "id", NULL),
                      "w");

    /* A live element that is not a label is refused with NOT_SUPPORTED, which
       is a different answer from the NOT_FOUND a dead handle gets. */
    {
        HtmlNodeHandle div = FormFindIdDoc(id, "d");

        RoboCheckEqInt(HtmlElementLabelTarget(&div, &target, NULL,
                                                         NULL),
                          RoboNotSupported);
        RoboCheckEqInt(HtmlElementLabelTarget(NULL, &target, NULL,
                                                         NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(HtmlElementLabelTarget(&label, NULL, NULL,
                                                         NULL),
                          RoboInvalidArgument);
        /* The output may be the same object as the input: the resolver works
           on a copy, so this is well defined rather than a self-wipe. */
        RoboCheckEqInt(HtmlElementLabelTarget(&div, &div, NULL,
                                                         NULL),
                          RoboNotSupported);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormOwnerNested(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle outer;
    HtmlNodeHandle innerInput;
    HtmlNodeHandle owner;
    HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;

    /* The nearest ancestor form wins, not the outermost. */
    FormBuild(id,
               "<form id=\"outer\"><div><form id=\"inner\">"
               "<input id=\"x\"></form></div></form>");
    outer = FormFindTag(&root, RoboHtmlTagForm);
    {
        HtmlNodeHandle inner = FormFindId(&outer, "inner");

        RoboCheck(HtmlNodeHandleIsWellformed(&inner));
        innerInput = FormFindId(&inner, "x");
        RoboCheck(HtmlNodeHandleIsWellformed(&innerInput));
        RoboCheckEqInt(
            HtmlElementFormOwner(&innerInput, &owner, &source, NULL),
            RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &inner));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);
    }

    /* A control outside every form has no owner — a valid state, reported as
       a success with an empty answer rather than as an error. */
    {
        HtmlNodeHandle loose;

        FormBuild(id, "<div><input id=\"loose\"></div>");
        loose = FormFindIdDoc(id, "loose");
        RoboCheck(HtmlNodeHandleIsWellformed(&loose));
        RoboCheckEqInt(HtmlElementFormOwner(&loose, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceNone);
    }

    /* An orphaned control (created, then detached) has no ancestor form, and
       the walk terminates rather than chasing a broken chain. */
    FormBuild(id, "<form id=\"f\"><input id=\"i\"></form>");
    {
        HtmlNodeHandle form = FormFindIdDoc(id, "f");
        HtmlNodeHandle input = FormFindId(&form, "i");
        HtmlNodeHandle parent;

        /* Detaching the form from *its* parent, not from the document root —
           the parser placed it inside <body>, and the mutation API only
           removes a child from its actual parent. */
        RoboCheckEqInt(HtmlElementParent(&form, &parent, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeRemoveChild(&parent, &form, NULL),
                          RoboOk);
        /* Detaching a form does *not* detach its children, so the input is
           still nested inside the form element and still owned by it — the
           ancestor rule describes the tree's shape, not whether that shape is
           reachable from the root. Removing the input from the form is what
           clears the association. */
        RoboCheck(HtmlNodeHandleIsWellformed(&input));
        RoboCheckEqInt(HtmlElementFormOwner(&input, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &form));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);

        RoboCheckEqInt(HtmlNodeRemoveChild(&form, &input, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementFormOwner(&input, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceNone);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormOwnerAttribute(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle target;
    HtmlNodeHandle owner;
    HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;

    /* The `form` attribute wins over ancestry when it resolves to a form. */
    FormBuild(id,
               "<form id=\"remote\"></form>"
               "<div><form id=\"local\">"
               "<input id=\"a\" form=\"remote\">"
               "<input id=\"b\">"
               "</form></div>");
    target = FormFindId(&root, "remote");
    {
        HtmlNodeHandle local = FormFindId(&root, "local");
        HtmlNodeHandle a = FormFindId(&local, "a");
        HtmlNodeHandle b = FormFindId(&local, "b");

        RoboCheckEqInt(HtmlElementFormOwner(&a, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &target));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAttribute);

        /* The sibling with no attribute is owned by its ancestor, so the two
           rules are distinguishable in one document. */
        RoboCheckEqInt(HtmlElementFormOwner(&b, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &local));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);
    }

    /* A `form` attribute naming an element that is not a form falls back to
       ancestry, and says so through the source field. */
    FormBuild(id,
               "<div id=\"notform\"></div>"
               "<form id=\"f\"><input id=\"c\" form=\"notform\"></form>");
    {
        HtmlNodeHandle form = FormFindIdDoc(id, "f");
        HtmlNodeHandle c = FormFindId(&form, "c");

        RoboCheckEqInt(HtmlElementFormOwner(&c, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &form));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);
    }

    /* A `form` attribute naming nothing at all: same fallback. */
    FormBuild(id, "<form id=\"f\"><input id=\"d\" form=\"ghost\"></form>");
    {
        HtmlNodeHandle form = FormFindIdDoc(id, "f");
        HtmlNodeHandle d = FormFindId(&form, "d");

        RoboCheckEqInt(HtmlElementFormOwner(&d, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &form));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);
    }

    /* A blank `form` is not a usable reference either. */
    FormBuild(id, "<form id=\"f\"><input id=\"e\" form=\"  \"></form>");
    {
        HtmlNodeHandle form = FormFindIdDoc(id, "f");
        HtmlNodeHandle e = FormFindId(&form, "e");

        RoboCheckEqInt(HtmlElementFormOwner(&e, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &form));
    }

    /* And when the referenced form is removed from the tree, the attribute
       rule stops answering: the next lookup finds the ancestor form instead
       of a dangling reference. Nothing needed invalidating for that to be
       true. */
    FormBuild(id,
               "<form id=\"remote\"></form>"
               "<form id=\"local\">"
               "<input id=\"g\" form=\"remote\">"
               "</form>");
    {
        HtmlNodeHandle remote = FormFindIdDoc(id, "remote");
        HtmlNodeHandle local = FormFindIdDoc(id, "local");
        HtmlNodeHandle g = FormFindId(&local, "g");

        RoboCheckEqInt(HtmlElementFormOwner(&g, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &remote));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAttribute);

        {
            HtmlNodeHandle parent;

            RoboCheckEqInt(
                HtmlElementParent(&remote, &parent, NULL), RoboOk);
            RoboCheckEqInt(
                HtmlNodeRemoveChild(&parent, &remote, NULL), RoboOk);
        }
        RoboCheckEqInt(HtmlElementFormOwner(&g, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &local));
        RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormOwnerNotSupported(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle owner;
    HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;

    /* A div inside a form is not owned by it: the question is a category
       error for an element that cannot have an owner, and saying so is more
       useful than a lookup that happens to fail. An option is likewise not
       form-associated, even inside a form. */
    FormBuild(id,
               "<form id=\"f\"><div id=\"d\"></div>"
               "<select id=\"s\"><option id=\"o\">x</option></select>"
               "</form>");
    {
        HtmlNodeHandle d = FormFindId(&root, "d");
        HtmlNodeHandle s = FormFindId(&root, "s");
        HtmlNodeHandle f = FormFindId(&root, "f");
        HtmlNodeHandle label;

        RoboCheckEqInt(HtmlElementFormOwner(&d, &owner, NULL, NULL),
                          RoboNotSupported);

        /* A select *is* form-associated, so it has an owner even though it
           is not the element whose value the select models. */
        RoboCheckEqInt(HtmlElementFormOwner(&s, &owner, &source,
                                                       NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&owner, &f));

        FormBuild(id, "<form id=\"f2\"><label id=\"l\">x</label>"
                       "<div id=\"d2\"></div></form>");
        label = FormFindIdDoc(id, "l");
        RoboCheckEqInt(HtmlElementFormOwner(&label, &owner, NULL,
                                                       NULL),
                          RoboNotSupported);

        /* A real div in the current tree, so the NULL-output path is
           exercised on an element that *is* live: the argument check comes
           first, which is what makes it an argument error rather than a
           not-supported one. */
        {
            HtmlNodeHandle div2 = FormFindIdDoc(id, "d2");

            RoboCheck(HtmlNodeHandleIsWellformed(&div2));
            RoboCheckEqInt(
                HtmlElementFormOwner(&div2, NULL, NULL, NULL),
                RoboInvalidArgument);
        }
    }

    RoboCheckEqInt(HtmlElementFormOwner(NULL, &owner, NULL, NULL),
                      RoboInvalidArgument);

    /* The vocabulary these answers rest on, asserted directly: the two sets
       are different, and neither is everything with the control role. */
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagInput));
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagSelect));
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagTextarea));
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagButton));
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagFieldset));
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagObject));
    RoboCheck(!HtmlTagIsFormAssociated(RoboHtmlTagDiv));
    RoboCheck(!HtmlTagIsFormAssociated(RoboHtmlTagOption));
    RoboCheck(!HtmlTagIsFormAssociated(RoboHtmlTagForm));
    RoboCheck(!HtmlTagIsFormAssociated(RoboHtmlTagLabel));

    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagInput));
    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagTextarea));
    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagSelect));
    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagButton));
    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagMeter));
    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagProgress));
    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagOutput));
    RoboCheck(!HtmlTagIsLabelable(RoboHtmlTagFieldset));
    RoboCheck(!HtmlTagIsLabelable(RoboHtmlTagOption));
    RoboCheck(!HtmlTagIsLabelable(RoboHtmlTagLabel));

    /* Form-associated but not labelable is a real combination, and it is the
       reason these are two flags rather than one. */
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagOutput));
    RoboCheck(HtmlTagIsLabelable(RoboHtmlTagOutput));
    RoboCheck(HtmlTagIsFormAssociated(RoboHtmlTagFieldset));
    RoboCheck(!HtmlTagIsLabelable(RoboHtmlTagFieldset));

    /* The new flags did not disturb the ones already there. */
    RoboCheck(HtmlTagIsVoid(RoboHtmlTagInput));
    RoboCheck(!HtmlTagIsVoid(RoboHtmlTagTextarea));
    RoboCheckEqInt(HtmlTagClass(RoboHtmlTagInput),
                      RoboHtmlElementClassControl);

    /* The source vocabulary is itself observable, so a caller can report
       which rule answered without re-deriving it from an integer. */
    RoboCheckStrEq(HtmlFormOwnerSourceName(
                          RoboHtmlFormOwnerSourceAttribute),
                      "attribute");
    RoboCheckStrEq(HtmlFormOwnerSourceName(
                          RoboHtmlFormOwnerSourceAncestor),
                      "ancestor");
    RoboCheckStrEq(HtmlFormOwnerSourceName(
                          RoboHtmlFormOwnerSourceNone),
                      "none");
    RoboCheckStrEq(HtmlFormOwnerSourceName(
                          (HtmlFormOwnerSource)99),
                      "unknown");
    RoboCheck(HtmlFormOwnerSourceIsKnown(
        RoboHtmlFormOwnerSourceAncestor));
    RoboCheck(!HtmlFormOwnerSourceIsKnown(
        (HtmlFormOwnerSource)99));
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void TestFormParserMalformed(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlTreeReport report;
    HtmlNodeHandle root;

    /*
     * A document that is wrong in several ordinary ways at once: an unclosed
     * form containing an unclosed div, a stray end tag for a form that was
     * never opened, a void input followed immediately by a sibling, a
     * textarea whose content ends at its own end tag, and an option with no
     * select. None of it may corrupt the tree.
     */
    FormBuild(id,
               "<form action=\"/a\">"
               "<div>"
               "<input name=\"a\">"
               "<span>after</span>"
               "</form>"
               "</form>"
               "<textarea>text</textarea>"
               "<option>loose</option>");

    root = FormRoot(id);
    memset(&report, 0, sizeof(report));
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheckMsg(report.ok, "malformed form markup corrupted the tree");

    /* The void input did not swallow its following sibling: the span is a
       real element and is not inside the input. */
    {
        HtmlNodeHandle input =
            FormFindTag(&root, RoboHtmlTagInput);
        HtmlNodeHandle span = FormFindTag(&root, RoboHtmlTagSpan);
        int contains = 99;
        size_t children = 99u;

        RoboCheck(HtmlNodeHandleIsWellformed(&span));
        RoboCheck(!HtmlNodeHandleEqual(&span, &input));
        RoboCheckEqInt(HtmlNodeContains(&input, &span, &contains,
                                                  NULL),
                          RoboOk);
        RoboCheckEqInt(contains, 0);
        RoboCheckEqInt(HtmlNodeChildCount(&input, &children, NULL),
                          RoboOk);
        RoboCheckEqInt(children, 0u);
    }

    /* The textarea kept its content and gave nothing to the element after
       it. */
    {
        HtmlNodeHandle area =
            FormFindTag(&root, RoboHtmlTagTextarea);
        HtmlNodeHandle option =
            FormFindTag(&root, RoboHtmlTagOption);

        RoboCheckStrEq(FormText(&area), "text");
        RoboCheckStrEq(FormText(&option), "loose");
    }

    /* The form is still a form, and still answerable. */
    {
        HtmlFormAttributes attrs;
        HtmlNodeHandle form = FormFindTag(&root, RoboHtmlTagForm);

        RoboCheckEqInt(HtmlElementFormAttributes(&form, &attrs,
                                                            NULL),
                          RoboOk);
        RoboCheckStrEq(attrs.action, "/a");
        RoboCheck(attrs.actionUsable);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormVoidInput(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);

    /* `input` is void in the tag vocabulary, so the parser inserts and pops
       it: `<input>x</input>` leaves `x` as a sibling, not a child. */
    FormBuild(id, "<div><input>orphan</input></div>");
    {
        HtmlNodeHandle input =
            FormFindTag(&root, RoboHtmlTagInput);
        size_t children = 99u;

        RoboCheckEqInt(HtmlNodeChildCount(&input, &children, NULL),
                          RoboOk);
        RoboCheckEqInt(children, 0u);
        {
            HtmlNodeHandle div =
                FormFindTag(&root, RoboHtmlTagDiv);

            RoboCheckStrEq(FormText(&div), "orphan");
        }
    }

    /* The self-closing spelling behaves identically, and a following element
       is never consumed. */
    FormBuild(id,
               "<div><input/><input><span>s</span></div>");
    {
        HtmlNodeHandle div =
            FormFindTag(&root, RoboHtmlTagDiv);
        HtmlNodeHandle span = FormFindTag(&div, RoboHtmlTagSpan);
        size_t inputs = 0u;

        RoboCheck(HtmlNodeHandleIsWellformed(&span));
        RoboCheckStrEq(FormText(&div), "s");
        /* Two inputs exist, so the second `<input>` was not nested in the
           first (which is void and could not hold it anyway). */
        {
            HtmlNodeHandle cursor;
            size_t guard = 0u;

            if (HtmlNodeFirstChild(&div, &cursor, NULL) == RoboOk) {
                for (;;) {
                    HtmlNodeHandle next;

                    if (HtmlElementIsTag(&cursor, RoboHtmlTagInput)) {
                        inputs++;
                    }
                    /* The out parameter is separate from the in parameter:
                       the reader clears its out slot, so passing one handle
                       for both would wipe it on the first step. */
                    if (HtmlNodeNextSibling(&cursor, &next, NULL) !=
                        RoboOk) {
                        break;
                    }
                    cursor = next;
                    if (guard++ > 16u) {
                        break;
                    }
                }
            }
        }
        RoboCheckEqInt(inputs, 2u);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

static void TestFormMutation(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle firstForm;
    HtmlNodeHandle secondForm;
    HtmlNodeHandle control;
    HtmlNodeHandle owner;
    HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;
    char buf[64];
    size_t length = 0u;

    /*
     * Adding a control to a form, removing it, and moving it between forms —
     * all through the real DOM mutation API on a parsed tree, because that is
     * what an application can actually do: a finalized document refuses
     * *construction* but the mutation seam stays open, deliberately.
     */
    FormBuild(id,
               "<form id=\"a\"></form>"
               "<form id=\"b\"><input id=\"c\" name=\"n\"></form>");
    firstForm = FormFindId(&root, "a");
    secondForm = FormFindId(&root, "b");
    control = FormFindId(&root, "c");
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, &source,
                                                   NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &secondForm));
    RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);

    /* Pin an override, then move the control into the other form: the owner
       changes and the state comes along, because state is keyed by node
       identity rather than by position. */
    RoboCheckEqInt(
        HtmlElementSetControlValue(&control, "kept", 4u, NULL),
        RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&secondForm, &control, NULL),
                      RoboOk);
    /* Detached: no ancestor form, so no owner — still a success, with an
       empty answer. */
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, &source,
                                                   NULL),
                      RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
    RoboCheckEqInt(source, RoboHtmlFormOwnerSourceNone);

    RoboCheckEqInt(HtmlNodeAppendChild(&firstForm, &control, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, &source,
                                                   NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &firstForm));
    RoboCheckEqInt(HtmlElementControlValue(&control, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "kept");

    /* Re-attaching a control that already has a parent is refused rather
       than silently moving it — the caller has to say "remove, then append",
       which is the one rule that keeps every node's parent unambiguous. */
    RoboCheckEqInt(HtmlNodeAppendChild(&firstForm, &control, NULL),
                      RoboAlreadyExists);

    /* Detaching the form from the document leaves the control owned by it:
       the control is still nested inside the form element. Taking the
       control out is what makes it unowned again. */
    {
        HtmlNodeHandle parent;

        RoboCheckEqInt(HtmlElementParent(&firstForm, &parent, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeRemoveChild(&parent, &firstForm,
                                                      NULL),
                          RoboOk);
    }
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, &source,
                                                   NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &firstForm));
    RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);

    RoboCheckEqInt(HtmlNodeRemoveChild(&firstForm, &control, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, &source,
                                                   NULL),
                      RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&owner));
    RoboCheckEqInt(source, RoboHtmlFormOwnerSourceNone);

    /* An `id` change is visible immediately, in both the label and the
       ownership direction. */
    FormBuild(id,
               "<form id=\"one\"></form><form id=\"two\"></form>"
               "<input id=\"c\" form=\"one\">"
               "<label id=\"l\" for=\"c\">L</label>");
    firstForm = FormFindId(&root, "one");
    secondForm = FormFindId(&root, "two");
    control = FormFindId(&root, "c");
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, NULL,
                                                   NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &firstForm));

    RoboCheckEqInt(
        HtmlElementSetAttribute(&control, "form", "two", NULL),
        RoboOk);
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, NULL,
                                                   NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&owner, &secondForm));

    /* Renaming the form the control references makes the reference stop
       resolving, and the fallback rule answers instead (there is no ancestor
       form here, so the answer is "no owner"). */
    RoboCheckEqInt(
        HtmlElementSetAttribute(&secondForm, "id", "renamed", NULL),
        RoboOk);
    RoboCheckEqInt(HtmlElementFormOwner(&control, &owner, NULL,
                                                   NULL),
                      RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&owner));

    /* A label's `for` follows the same live lookup, so renaming the control
       breaks the association and restoring it repairs it. */
    {
        HtmlNodeHandle label = FormFindId(&root, "l");
        HtmlNodeHandle target;

        RoboCheckEqInt(HtmlElementLabelTarget(&label, &target, NULL,
                                                         NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&target, &control));
        RoboCheckEqInt(
            HtmlElementSetAttribute(&control, "id", "c2", NULL),
            RoboOk);
        RoboCheckEqInt(HtmlElementLabelTarget(&label, &target, NULL,
                                                         NULL),
                          RoboOk);
        RoboCheck(!HtmlNodeHandleIsWellformed(&target));
        RoboCheckEqInt(
            HtmlElementSetAttribute(&label, "for", "c2", NULL),
            RoboOk);
        RoboCheckEqInt(HtmlElementLabelTarget(&label, &target, NULL,
                                                         NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&target, &control));
    }

    /*
     * Replacing a control: the replacement is a *different node*, so it does
     * not inherit the state the old one carried — which is what "keyed by node
     * identity" is supposed to mean, asserted rather than assumed. The two
     * inputs deliberately share an id and a declared value, so the only way
     * the replacement could report "override" is by inheriting state it should
     * not have.
     */
    FormBuild(id,
               "<form id=\"f\">"
               "<input id=\"i\" value=\"declared\">"
               "<input id=\"i\" value=\"declared\">"
               "</form>");
    {
        HtmlNodeHandle form = FormFindId(&root, "f");
        HtmlNodeHandle old = FormFindId(&form, "i");
        HtmlNodeHandle replacement;
        int declared = 0;

        RoboCheckEqInt(
            HtmlElementSetControlValue(&old, "override", 8u, NULL),
            RoboOk);
        RoboCheckStrEq(FormControlValue(&old, &declared), "override");
        RoboCheck(!declared);

        /* The second input is the replacement; detach it so replace_child
           accepts it, which is the move rule the DOM enforces. */
        RoboCheckEqInt(HtmlNodeNextSibling(&old, &replacement,
                                                      NULL),
                          RoboOk);
        RoboCheckEqInt(
            HtmlNodeRemoveChild(&form, &replacement, NULL), RoboOk);
        RoboCheckEqInt(
            HtmlNodeReplaceChild(&form, &replacement, &old, NULL),
            RoboOk);

        RoboCheckStrEq(FormControlValue(&replacement, &declared),
                          "declared");
        RoboCheck(declared);
        /* The displaced input is detached and still holds its own override;
           nothing was copied between them. */
        RoboCheckStrEq(FormControlValue(&old, &declared), "override");
        RoboCheck(!declared);
        RoboCheck(!HtmlNodeHandleEqual(&old, &replacement));
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

static void TestFormMutationOptions(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle select;
    size_t count = 0u;
    size_t index = 0u;

    FormBuild(id,
               "<select id=\"s\"><option>a</option>"
               "<option id=\"b\" value=\"b\" selected>b</option></select>");
    select = FormFindId(&root, "s");

    /* Two options, in order: adding one by moving another in would need a
       detached node, and the ordering that matters is the one the tree has.
       The document is finalized after parsing, so a *new* option is not
       something an application can create here — which is itself the rule
       this phase inherits rather than invents. */
    {
        HtmlNodeHandle added = FormFindId(&select, "b");

        RoboCheckEqInt(HtmlElementSelectOptionCount(&select,
                                                                &count, NULL),
                          RoboOk);
        RoboCheckEqInt(count, 2u);
        RoboCheckEqInt(HtmlElementOptionIndex(&added, &index, NULL),
                          RoboOk);
        RoboCheckEqInt(index, 1u);

        /* The marked option is the selected one. */
        {
            HtmlNodeHandle picked;

            RoboCheckEqInt(
                HtmlElementSelectSelectedOption(&select, &picked,
                                                         NULL),
                RoboOk);
            RoboCheck(HtmlNodeHandleEqual(&picked, &added));
        }

        /* Changing `selected`, and removing it, both take effect on the next
           read — and setting it on the other option moves the selection. */
        RoboCheckEqInt(
            HtmlElementSetAttribute(&added, "selected", "selected",
                                            NULL),
            RoboOk);
        {
            HtmlNodeHandle first;
            HtmlNodeHandle picked;

            RoboCheckEqInt(HtmlNodePrevSibling(&added, &first,
                                                          NULL),
                              RoboOk);
            RoboCheckEqInt(
                HtmlElementSetAttribute(&first, "selected", NULL,
                                                NULL),
                RoboOk);
            RoboCheckEqInt(
                HtmlElementSelectSelectedOption(&select, &picked,
                                                         NULL),
                RoboOk);
            RoboCheck(HtmlNodeHandleEqual(&picked, &first));
            RoboCheckStrEq(FormText(&picked), "a");
        }

        /* Removing the selected option puts the selection back on the first
           selectable one, because nothing carries the attribute now. */
        RoboCheckEqInt(
            HtmlElementRemoveAttribute(&added, "selected", NULL),
            RoboOk);
        {
            HtmlNodeHandle picked;

            RoboCheckEqInt(
                HtmlElementSelectSelectedOption(&select, &picked,
                                                         NULL),
                RoboOk);
            RoboCheck(!HtmlNodeHandleEqual(&picked, &added));
            RoboCheckStrEq(FormText(&picked), "a");
        }

        RoboCheckEqInt(HtmlNodeRemoveChild(&select, &added, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementSelectOptionCount(&select,
                                                                &count, NULL),
                          RoboOk);
        RoboCheckEqInt(count, 1u);
        /* The removed option is detached, so it is no longer inside its
           select and has no index. */
        RoboCheckEqInt(HtmlElementOptionIndex(&added, &index, NULL),
                          RoboNotFound);
    }

    /* Options nested in an optgroup still count and still have positions —
       the walk is the subtree, not the direct children, so the count and the
       index cannot disagree. */
    FormBuild(id,
               "<select id=\"s\">"
               "<optgroup label=\"g\"><option>a</option><option>b</option>"
               "</optgroup>"
               "<option>c</option></select>");
    select = FormFindId(&root, "s");
    RoboCheckEqInt(HtmlElementSelectOptionCount(&select, &count,
                                                            NULL),
                      RoboOk);
    RoboCheckEqInt(count, 3u);
    {
        HtmlNodeHandle last;
        FormTagQuery q;

        q.tag = RoboHtmlTagOption;
        /* The first option in the subtree is inside the optgroup, so its
           index is 0 — the walk does not stop at a direct child. */
        RoboCheckEqInt(
            HtmlNodeFindDescendant(&select,
                                           RoboHtmlNodeFilterElements,
                                           FormMatchTag, &q, &last, NULL),
            RoboOk);
        RoboCheckEqInt(HtmlElementOptionIndex(&last, &index, NULL),
                          RoboOk);
        RoboCheckEqInt(index, 0u);
        RoboCheckStrEq(FormText(&last), "a");

        /* And the last option — outside the optgroup — is index 2, so the
           count of 3 and the indices agree across the nesting boundary. */
        {
            HtmlNodeHandle cursor = FormLastOption(&select);

            RoboCheckEqInt(
                HtmlElementOptionIndex(&cursor, &index, NULL),
                RoboOk);
            RoboCheckEqInt(index, 2u);
            RoboCheckStrEq(FormText(&cursor), "c");
        }
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void TestFormLifecycle(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle input;
    char buf[64];
    size_t length = 0u;
    int declared = 0;

    /* A control created before anything is parsed works while the document is
       still CREATED: the state model does not depend on the parser having
       run. */
    root = FormRoot(id);
    input = FormCreateUnder(id, &root, "input");
    RoboCheckEqInt(HtmlElementSetAttribute(&input, "value",
                                                      "v", NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, "w", 1u, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementControlValue(&input, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "w");
    RoboCheck(!declared);

    /* Parsing into the same document is a fresh tree: the control above is
       gone with it, and the new tree's controls work. */
    FormBuild(id, "<form><input value=\"parsed\"></form>");
    RoboCheck(HtmlDocumentIsSettled(id));
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementControlValue(&input, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "parsed");
    RoboCheck(declared);

    /* A reset leaves a document that accepts a new source, and no control
       state survives it — an override must not outlive the tree it was
       written against. */
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, "override", 8u, NULL),
        RoboOk);
    /* Parsing already settled the document to COMPLETE, and a second
       transition to the state it is already in is not a valid one. */
    RoboCheck(HtmlDocumentIsComplete(id));
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);

    FormBuild(id, "<form><input value=\"fresh\"></form>");
    input = FormFindTagDoc(id, RoboHtmlTagInput);
    RoboCheckEqInt(HtmlElementControlValue(&input, buf,
                                                      sizeof(buf), &length,
                                                      &declared, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "fresh");
    RoboCheck(declared);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);

    /* And the capability flag says this build has the model. */
    {
        HtmlDomCapabilities caps;

        RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
        RoboCheck(caps.forms);
        RoboCheck(caps.treeBuilding);
        RoboCheck(caps.elementTags);
    }
}

static void TestFormDestroyedState(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlNodeHandle input;
    HtmlFormAttributes attrs;
    HtmlInputAttributes iattrs;
    char buf[64];
    size_t length = 0u;
    size_t count = 0u;

    FormBuild(id,
               "<form action=\"/a\"><input value=\"v\"><select id=\"s\">"
               "<option>a</option></select></form>");
    input = FormFindTag(&root, RoboHtmlTagInput);
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, "over", 4u, NULL),
        RoboOk);

    /* Destroying the document removes the tree and the state with it: every
       read against the retired handles is NOT_FOUND, never a stale answer,
       and every write is refused rather than resurrecting state. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(!HtmlDocumentIsLive(id));

    RoboCheckEqInt(HtmlElementInputAttributes(&input, &iattrs,
                                                         NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlElementControlValue(&input, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      RoboNotFound);
    RoboCheckEqInt(
        HtmlElementSetControlValue(&input, "x", 1u, NULL),
        RoboNotFound);
    RoboCheckEqInt(HtmlElementClearControlValue(&input, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlElementControlChecked(&input, NULL, NULL,
                                                        NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementFormOwner(&input, &input, NULL, NULL),
                      RoboNotFound);

    {
        HtmlNodeHandle form = input;

        RoboCheckEqInt(HtmlElementFormAttributes(&form, &attrs,
                                                            NULL),
                          RoboNotFound);
        RoboCheckEqInt(HtmlElementSelectOptionCount(&form, &count,
                                                                NULL),
                          RoboNotFound);
        RoboCheckEqInt(HtmlElementSelectAttributes(&form, NULL,
                                                              NULL),
                          RoboInvalidArgument);
    }

    /* Destruction is safe to repeat — it is not an error, but it is also not
       a success, because the document is already gone. What matters is that
       it does not crash, does not resurrect the id, and cannot bring the
       retired handles back to life. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    RoboCheck(!HtmlDocumentIsLive(id));
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &iattrs,
                                                         NULL),
                      RoboNotFound);
}

static void TestFormStructure(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlTreeReport report;
    HtmlNodeHandle form;

    /* One realistic document exercising every element this phase models, then
       a full invariant audit: parent links, sibling chains, counts and cycle
       freedom must all still hold with form controls in the tree. */
    FormBuild(id,
               "<form action=\"/Save\" method=\"post\">"
               "  <fieldset>"
               "    <legend>Account</legend>"
               "    <label for=\"Email\">Email</label>"
               "    <input id=\"Email\" type=\"email\" name=\"email\" "
               "required>"
               "    <label><input type=\"checkbox\" name=\"news\" checked>"
               "Send news</label>"
               "    <textarea name=\"note\" rows=\"3\">Hello</textarea>"
               "    <select name=\"country\">"
               "      <optgroup label=\"Europe\">"
               "        <option value=\"pt\">Portugal</option>"
               "      </optgroup>"
               "      <option value=\"us\" selected>United States</option>"
               "    </select>"
               "    <button type=\"submit\">Save</button>"
               "    <button type=\"reset\">Reset</button>"
               "  </fieldset>"
               "</form>");

    memset(&report, 0, sizeof(report));
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheckMsg(report.ok, "form structure broke a tree invariant");

    form = FormFindTag(&root, RoboHtmlTagForm);
    RoboCheck(HtmlElementIsTag(&form, RoboHtmlTagForm));
    RoboCheck(HtmlElementIsNamed(&form, "form"));
    /* The fieldset really is inside the form, so the ancestor rule below is
       answering a question about this tree rather than about a detached
       node. */
    {
        HtmlNodeHandle fieldset =
            FormFindTag(&root, RoboHtmlTagFieldset);
        int contains = 0;

        RoboCheckEqInt(HtmlNodeContains(&form, &fieldset, &contains,
                                                  NULL),
                          RoboOk);
        RoboCheck(contains);
    }

    /* Count by walking, so the structure is asserted through the real
       traversal rather than by trust. */
    {
        static const HtmlTag expected[] = {
            RoboHtmlTagForm,    RoboHtmlTagFieldset,
            RoboHtmlTagLegend,  RoboHtmlTagLabel,
            RoboHtmlTagInput,   RoboHtmlTagTextarea,
            RoboHtmlTagSelect,  RoboHtmlTagOptgroup,
            RoboHtmlTagOption,  RoboHtmlTagButton,
        };
        size_t i;

        for (i = 0u; i < sizeof(expected) / sizeof(expected[0]); i++) {
            FormTagQuery q;
            HtmlNodeHandle found;

            q.tag = expected[i];
            HtmlNodeHandleClear(&found);
            RoboCheckEqInt(HtmlNodeFindFirst(
                                  &root, RoboHtmlNodeFilterElements,
                                  FormMatchTag, &q, &found, NULL),
                              RoboOk);
            RoboCheck(HtmlElementIsTag(&found, expected[i]));
        }
    }

    /* Every control in the document resolves to the same owner, through the
       two different rules — which is what makes the document a form. */
    {
        HtmlNodeHandle owner;
        HtmlFormOwnerSource source = RoboHtmlFormOwnerSourceNone;
        /* Every control in the form answers with the same owner, through the
           real ancestor rule — so "this is a form" is asserted by every
           control agreeing, not by one lookup succeeding. */
        static const HtmlTag controls[] = {
            RoboHtmlTagInput, RoboHtmlTagTextarea, RoboHtmlTagSelect,
            RoboHtmlTagFieldset,
        };
        size_t i;

        for (i = 0u; i < sizeof(controls) / sizeof(controls[0]); i++) {
            HtmlNodeHandle control = FormFindTag(&form, controls[i]);

            RoboCheckEqInt(
                HtmlElementFormOwner(&control, &owner, &source, NULL),
                RoboOk);
            RoboCheck(HtmlNodeHandleEqual(&owner, &form));
            RoboCheckEqInt(source, RoboHtmlFormOwnerSourceAncestor);
        }
    }

    /* Both buttons kept their own types. */
    {
        HtmlNodeHandle button =
            FormFindTag(&form, RoboHtmlTagButton);
        HtmlButtonAttributes attrs;

        RoboCheckEqInt(HtmlElementButtonAttributes(&button, &attrs,
                                                              NULL),
                          RoboOk);
        RoboCheckEqInt(attrs.typeEffective, RoboHtmlButtonTypeSubmit);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Unicode                                                             */
/* ------------------------------------------------------------------ */

static void TestFormUnicode(void)
{
    HtmlDocumentId id = FormDoc();
    HtmlNodeHandle root = FormRoot(id);
    HtmlInputAttributes iattrs;
    HtmlTextareaAttributes tattrs;
    HtmlNodeHandle input;
    HtmlNodeHandle area;
    HtmlNodeHandle option;
    char buf[512];
    size_t length = 0u;

    /* Labels, values and names in the scripts this runtime already carries:
       ASCII, accented Latin, Arabic, Persian, Urdu and emoji. Nothing here
       shapes or normalizes — the requirement is preservation, and the bytes
       that arrive are the bytes that come back. */
    FormBuild(id,
               "<form name=\"نموذج\">"
               "<label for=\"e\">البريد الإلكتروني</label>"
               "<input id=\"e\" type=\"email\" name=\"ای میل\" "
               "value=\"مستخدم@مثال.پاکستان\" "
               "placeholder=\"ایمیل درج کریں\">"
               "<input id=\"r\" type=\"radio\" name=\"درجہ\" value=\"جی ہاں\">"
               "<textarea id=\"t\" name=\"پیغام\">نام خدا</textarea>"
               "<select id=\"s\" name=\"ملک\">"
               "<option value=\"پاکستان\">پاکستان</option>"
               "<option selected>مملکت متحدہ 🇵🇰</option>"
               "</select>"
               "<button type=\"submit\" value=\"محفوظ کریں\">محفوظ کریں</button>"
               "</form>");

    input = FormFindId(&root, "e");
    RoboCheckEqInt(HtmlElementInputAttributes(&input, &iattrs,
                                                         NULL),
                      RoboOk);
    RoboCheckStrEq(iattrs.name, "ای میل");
    RoboCheckStrEq(iattrs.value, "مستخدم@مثال.پاکستان");
    RoboCheckStrEq(iattrs.placeholder, "ایمیل درج کریں");

    /* A label's `for` is a byte-exact id, and the Unicode id resolves. */
    {
        HtmlNodeHandle label =
            FormFindTag(&root, RoboHtmlTagLabel);
        HtmlNodeHandle target;

        RoboCheckEqInt(HtmlElementLabelTarget(&label, &target, NULL,
                                                         NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&target, &input));
    }

    /* The declared value of a control read through the buffer API keeps every
       byte, including the multi-byte ones. */
    RoboCheckEqInt(HtmlElementControlValue(&input, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "مستخدم@مثال.پاکستان");
    RoboCheckEqInt(length, strlen("مستخدم@مثال.پاکستان"));

    /* A textarea's default text is its children, in the same bytes. */
    area = FormFindId(&root, "t");
    RoboCheckEqInt(HtmlElementTextareaAttributes(&area, &tattrs,
                                                            NULL),
                      RoboOk);
    RoboCheckStrEq(tattrs.name, "پیغام");
    RoboCheckStrEq(FormText(&area), "نام خدا");
    RoboCheckEqInt(HtmlElementControlValue(&area, buf,
                                                      sizeof(buf), &length,
                                                      NULL, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "نام خدا");

    /* An option's fallback value is its Unicode text, verbatim — emoji and
       all, because the text is bytes and this layer does not care which
       bytes. */
    option = FormFindTag(&root, RoboHtmlTagOption);
    {
        HtmlNodeHandle sibling;
        int explicitValue = 9;

        RoboCheckEqInt(HtmlNodeNextSibling(&option, &sibling, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlElementOptionValue(&sibling, buf,
                                                         sizeof(buf), &length,
                                                         &explicitValue,
                                                         NULL),
                          RoboOk);
        RoboCheckStrEq(buf, "مملکت متحدہ 🇵🇰");
        RoboCheck(!explicitValue);
        RoboCheckEqInt(length, strlen("مملکت متحدہ 🇵🇰"));
    }

    /* An override carrying Unicode round-trips, including into the value of
       an element whose declared value is empty. */
    FormBuild(id, "<input id=\"u\">");
    input = FormFindIdDoc(id, "u");
    RoboCheckEqInt(HtmlElementSetControlValue(
                          &input, "café ☕ Тест", strlen("café ☕ Тест"), NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementControlValue(&input, buf,
                                                      sizeof(buf), NULL, NULL,
                                                      NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "café ☕ Тест");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlForm(void)
{
    TestRegister("html.form.parser_element", TestFormParserElement);
    TestRegister("html.form.form_attributes",
                       TestFormFormAttributes);
    TestRegister("html.form.method_enctype_vocabulary",
                       TestFormMethodEnctypeVocabulary);
    TestRegister("html.form.non_form_refused",
                       TestFormNonFormRefused);
    TestRegister("html.form.input_types", TestFormInputTypes);
    TestRegister("html.form.input_attributes",
                       TestFormInputAttributes);
    TestRegister("html.form.input_value_state",
                       TestFormInputValueState);
    TestRegister("html.form.input_checked_state",
                       TestFormInputCheckedState);
    TestRegister("html.form.input_numeric_constraints",
                       TestFormInputNumericConstraints);
    TestRegister("html.form.button", TestFormButton);
    TestRegister("html.form.textarea", TestFormTextarea);
    TestRegister("html.form.select_options", TestFormSelectOptions);
    TestRegister("html.form.option_value_fallback",
                       TestFormOptionValueFallback);
    TestRegister("html.form.label_target", TestFormLabelTarget);
    TestRegister("html.form.owner_nested", TestFormOwnerNested);
    TestRegister("html.form.owner_attribute",
                       TestFormOwnerAttribute);
    TestRegister("html.form.owner_not_supported",
                       TestFormOwnerNotSupported);
    TestRegister("html.form.parser_malformed",
                       TestFormParserMalformed);
    TestRegister("html.form.void_input", TestFormVoidInput);
    TestRegister("html.form.mutation", TestFormMutation);
    TestRegister("html.form.mutation_options",
                       TestFormMutationOptions);
    TestRegister("html.form.lifecycle", TestFormLifecycle);
    TestRegister("html.form.destroyed_state",
                       TestFormDestroyedState);
    TestRegister("html.form.structure", TestFormStructure);
    TestRegister("html.form.unicode", TestFormUnicode);
}
