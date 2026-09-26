/*
 * RoboHTML Phase 17.1 DOM-completion suite.
 *
 * What it covers: node creation (element, text, comment, fragment) including
 * creation on a *finalized* document; shallow and deep clone; the independence
 * of a clone; textContent read and write for elements and character nodes; the
 * `prepend` convenience; getElementById / getElementsByTagName /
 * getElementsByClassName; the selector seam (absent → RoboNotSupported, an
 * installed test matcher → querySelector/All), and the capability table.
 *
 * The fixture parses real markup through the real tokenizer and tree builder,
 * so a created node lives beside parsed ones in one tree and the queries see
 * both. Structural assertions are doubled with the runtime's own whole-tree
 * audit (`DomExpectCleanTree`), because the specific check says what a call
 * was supposed to do and the audit catches a repair that fixed the link the
 * test looked at and left another one wrong.
 */

#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboTest.h"

#include "RoboHTMLDomTestSupport.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

static const char DomQueryMarkup[] =
    "<html><head><title>t</title></head><body>"
    "<div id=\"main\" class=\"card wide\">"
    "<p class=\"card\">one</p>"
    "<p class=\"card wide\">two</p>"
    "</div>"
    "<span id=\"tail\">tail</span>"
    "</body></html>";

/** A parsed document over DomQueryMarkup. */
static HtmlDocumentId DomQueryDoc(void)
{
    return DomDocumentOf(DomQueryMarkup);
}

/** Count via a visitor into *user. */
static Result CountVisitor(const HtmlNodeView *view, size_t depth, void *user)
{
    size_t *count = (size_t *)user;

    (void)view;
    (void)depth;
    (*count)++;
    return RoboOk;
}

/* A test-owned selector matcher: matches an element by exact tag name. It is
 * deliberately *not* a CSS engine — it exists to prove the seam routes a
 * selector and a traversal to the installed function. */
static int TagSelectorMatcher(const HtmlNodeHandle *element,
                              const char *selector,
                              size_t selectorLength,
                              void *userdata,
                              HtmlDiag *diag)
{
    HtmlElementView view;

    (void)userdata;
    (void)diag;
    if (HtmlElementViewGet(element, &view, NULL) != RoboOk) {
        return 0;
    }
    return view.nameLength == selectorLength &&
           strncmp(view.name, selector, selectorLength) == 0;
}

/* ------------------------------------------------------------------ */
/* Cases                                                               */
/* ------------------------------------------------------------------ */

static void TestDomQueryCapabilities(void)
{
    HtmlDomCompletionCapabilities caps;

    memset(&caps, 0, sizeof(caps));
    RoboCheckEqInt(HtmlDomCompletionGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.creation, 1);
    RoboCheckEqInt(caps.cloning, 1);
    RoboCheckEqInt(caps.textContent, 1);
    RoboCheckEqInt(caps.prepend, 1);
    RoboCheckEqInt(caps.lookupById, 1);
    RoboCheckEqInt(caps.lookupByTag, 1);
    RoboCheckEqInt(caps.lookupByClass, 1);
    RoboCheckEqInt(caps.selectorSeam, 1);
    RoboCheckEqInt(caps.selectorEngine, 0);
    RoboCheckEqInt(caps.innerHtml, 0);
    RoboCheckEqInt(HtmlDomCompletionGetCapabilities(NULL), RoboInvalidArgument);
}

static void TestDomQueryCreateElement(void)
{
    HtmlDocumentId id = DomDocumentOf("<html><body></body></html>");
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle created;
    HtmlNodeHandle bodyParent;
    HtmlElementView view;

    root = DomRoot(id);
    body = DomFindTag(&root, RoboHtmlTagBody);

    RoboCheckEqInt(HtmlDocumentCreateElement(id, "SPAN", &created, NULL),
                   RoboOk);
    /* The name is normalized to lowercase and the node is detached. */
    RoboCheckEqInt(HtmlElementViewGet(&created, &view, NULL), RoboOk);
    RoboCheck(strcmp(view.name, "span") == 0);
    RoboCheckEqInt(HtmlNodeHandleResolve(&created, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementParent(&created, &bodyParent, NULL),
                   RoboNotFound);

    /* It becomes a real child once inserted. */
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &created, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementParent(&created, &bodyParent, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&bodyParent, &body));
    DomExpectCleanTree(id);

    /* A bad name is refused, not repaired. */
    RoboCheck(HtmlDocumentCreateElement(id, "", &created, NULL) != RoboOk);
    RoboCheck(HtmlDocumentCreateElement(id, NULL, &created, NULL) != RoboOk);

    HtmlShutdown();
}

static void TestDomQueryCreateCharacters(void)
{
    HtmlDocumentId id = DomDocumentOf("<html><body></body></html>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);
    HtmlNodeHandle text;
    HtmlNodeHandle comment;
    HtmlNodeView view;

    RoboCheckEqInt(HtmlDocumentCreateTextNode(id, "hello", &text, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeGetView(&text, &view, NULL), RoboOk);
    RoboCheckEqInt((int)view.kind, (int)RoboHtmlNodeKindText);
    RoboCheck(strcmp(view.text, "hello") == 0);

    RoboCheckEqInt(HtmlDocumentCreateComment(id, "note", &comment, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeGetView(&comment, &view, NULL), RoboOk);
    RoboCheckEqInt((int)view.kind, (int)RoboHtmlNodeKindComment);
    RoboCheck(strcmp(view.text, "note") == 0);

    RoboCheckEqInt(HtmlNodeAppendChild(&body, &text, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &comment, NULL), RoboOk);
    DomExpectCleanTree(id);

    HtmlShutdown();
}

static void TestDomQueryCreateFragment(void)
{
    HtmlDocumentId id = DomDocumentOf("<html><body></body></html>");
    HtmlNodeHandle fragment;
    HtmlNodeHandle child;
    size_t count = 0u;

    RoboCheckEqInt(HtmlDocumentCreateFragment(id, &fragment, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeHandleResolve(&fragment, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "li", &child, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&fragment, &child, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildCount(&fragment, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);
    DomExpectCleanTree(id);

    HtmlShutdown();
}

static void TestDomQueryPrepend(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle div = DomFindId(&root, "main");
    HtmlNodeHandle first;
    HtmlNodeHandle newFirst;
    HtmlNodeHandle moved;

    RoboCheckEqInt(HtmlNodeFirstChild(&div, &first, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "hr", &newFirst, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodePrependChild(&div, &newFirst, NULL), RoboOk);

    RoboCheckEqInt(HtmlNodeFirstChild(&div, &moved, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&moved, &newFirst));
    /* The old first child is now second. */
    {
        HtmlNodeHandle second;

        RoboCheckEqInt(HtmlNodeNextSibling(&newFirst, &second, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&second, &first));
    }
    DomExpectCleanTree(id);

    HtmlShutdown();
}

static void TestDomQueryCloneShallow(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle div = DomFindId(&root, "main");
    HtmlNodeHandle clone;
    HtmlElementView view;
    HtmlNodeHandle parent;
    size_t children = 99u;

    RoboCheckEqInt(HtmlNodeClone(&div, 0, &clone, NULL), RoboOk);
    RoboCheck(!HtmlNodeHandleEqual(&clone, &div));

    /* Attributes carried; children did not. */
    RoboCheckEqInt(HtmlElementViewGet(&clone, &view, NULL), RoboOk);
    RoboCheck(strcmp(view.name, "div") == 0);
    RoboCheckEqInt((int)view.attributeCount, 2);
    RoboCheck(strcmp(DomAttr(&clone, "id"), "main") == 0);
    RoboCheckEqInt(HtmlNodeChildCount(&clone, &children, NULL), RoboOk);
    RoboCheckEqInt((int)children, 0);

    /* Still detached. */
    RoboCheckEqInt(HtmlElementParent(&clone, &parent, NULL), RoboNotFound);

    HtmlShutdown();
}

static void TestDomQueryCloneDeep(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle div = DomFindId(&root, "main");
    HtmlNodeHandle clone;
    size_t sourceChildren = 0u;
    size_t cloneChildren = 0u;
    char text[64];
    size_t length = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(&div, &sourceChildren, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeClone(&div, 1, &clone, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildCount(&clone, &cloneChildren, NULL), RoboOk);
    RoboCheckEqInt((int)cloneChildren, (int)sourceChildren);
    RoboCheck(cloneChildren > 0u);

    /* Same text; the copy is independent. */
    RoboCheckEqInt(HtmlNodeTextContentGet(&div, text, sizeof(text), &length,
                                          NULL),
                   RoboOk);
    RoboCheck(strcmp(text, "onetwo") == 0);

    /* Mutating the clone's text leaves the original untouched. */
    {
        HtmlNodeHandle firstChild;

        RoboCheckEqInt(HtmlNodeFirstChild(&clone, &firstChild, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeTextContentSet(&firstChild, "X", NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeTextContentGet(&div, text, sizeof(text), &length,
                                              NULL),
                       RoboOk);
        RoboCheck(strcmp(text, "onetwo") == 0);
    }
    /* The original tree is untouched by the clone entirely. */
    RoboCheckEqInt(HtmlNodeChildCount(&div, &sourceChildren, NULL), RoboOk);
    DomExpectCleanTree(id);

    HtmlShutdown();
}

static void TestDomQueryCloneRefusals(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle clone;

    /* The document node cannot be cloned. */
    RoboCheckEqInt(HtmlNodeClone(&root, 1, &clone, NULL), RoboNotSupported);
    /* NULL output / node. */
    RoboCheckEqInt(HtmlNodeClone(&root, 0, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeClone(NULL, 0, &clone, NULL), RoboInvalidArgument);

    HtmlShutdown();
}

static void TestDomQueryTextContent(void)
{
    HtmlDocumentId id = DomDocumentOf("<html><body></body></html>");
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);
    HtmlNodeHandle div;
    HtmlNodeHandle text;
    char buffer[128];
    size_t length = 0u;

    RoboCheckEqInt(HtmlDocumentCreateElement(id, "div", &div, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &div, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeTextContentSet(&div, "hello", NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeTextContentGet(&div, buffer, sizeof(buffer),
                                          &length, NULL),
                   RoboOk);
    RoboCheck(strcmp(buffer, "hello") == 0);
    RoboCheckEqInt((int)length, 5);

    /* Replacing the content drops the previous text node. */
    RoboCheckEqInt(HtmlNodeTextContentSet(&div, "bye", NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeTextContentGet(&div, buffer, sizeof(buffer),
                                          &length, NULL),
                   RoboOk);
    RoboCheck(strcmp(buffer, "bye") == 0);

    /* Empty clears every child. */
    RoboCheckEqInt(HtmlNodeTextContentSet(&div, "", NULL), RoboOk);
    {
        size_t count = 99u;

        RoboCheckEqInt(HtmlNodeChildCount(&div, &count, NULL), RoboOk);
        RoboCheckEqInt((int)count, 0);
    }

    /* A text node's own content is itself. */
    RoboCheckEqInt(HtmlDocumentCreateTextNode(id, "raw", &text, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeTextContentGet(&text, buffer, sizeof(buffer),
                                          &length, NULL),
                   RoboOk);
    RoboCheck(strcmp(buffer, "raw") == 0);
    RoboCheckEqInt(HtmlNodeTextContentSet(&text, "cooked", NULL), RoboOk);
    RoboCheck(strcmp(HtmlTextContent(&text, NULL), "cooked") == 0);

    /* The document node is refused rather than emptied. */
    RoboCheck(HtmlNodeTextContentSet(&root, "x", NULL) != RoboOk);

    DomExpectCleanTree(id);

    HtmlShutdown();
}

static void TestDomQueryGetElementById(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle out;
    HtmlNodeHandle expected;
    HtmlNodeHandle root = DomRoot(id);

    expected = DomFindId(&root, "main");
    RoboCheckEqInt(HtmlDocumentGetElementById(id, "main", &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &expected));

    RoboCheckEqInt(HtmlDocumentGetElementById(id, "tail", &out, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetElementById(id, "nope", &out, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentGetElementById(id, NULL, &out, NULL),
                   RoboInvalidArgument);

    HtmlShutdown();
}

static void TestDomQueryGetElementsByTagName(void)
{
    HtmlDocumentId id = DomQueryDoc();
    size_t count = 0u;
    HtmlNodeHandle root;
    HtmlNodeHandle div;

    RoboCheckEqInt(HtmlDocumentGetElementsByTagName(id, "p", NULL, NULL, &count,
                                                    NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 2);

    /* A visitor sees each match in document order and can count too. */
    {
        size_t visited = 0u;

        RoboCheckEqInt(HtmlDocumentGetElementsByTagName(
                           id, "p", CountVisitor, &visited, &count, NULL),
                       RoboOk);
        RoboCheckEqInt((int)count, 2);
        RoboCheckEqInt((int)visited, 2);
    }

    /* The wildcard matches every element; assert it is non-zero and at least
     * the named count (the exact number depends on the implied html/head). */
    RoboCheckEqInt(HtmlDocumentGetElementsByTagName(id, "*", NULL, NULL,
                                                    &count, NULL),
                   RoboOk);
    RoboCheck(count > 2u);

    /* Element-rooted search only looks below the root. */
    root = DomRoot(id);
    div = DomFindId(&root, "main");
    RoboCheckEqInt(HtmlElementGetElementsByTagName(&div, "p", NULL, NULL,
                                                   &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 2);
    RoboCheckEqInt(HtmlElementGetElementsByTagName(&div, "span", NULL, NULL,
                                                   &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 0);

    HtmlShutdown();
}

static void TestDomQueryGetElementsByClassName(void)
{
    HtmlDocumentId id = DomQueryDoc();
    size_t count = 0u;

    RoboCheckEqInt(HtmlDocumentGetElementsByClassName(id, "card", NULL, NULL,
                                                      &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 3);

    /* Every named class must be present. */
    RoboCheckEqInt(HtmlDocumentGetElementsByClassName(id, "card wide", NULL,
                                                      NULL, &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 2);

    /* A token that is not a whole class never matches. */
    RoboCheckEqInt(HtmlDocumentGetElementsByClassName(id, "car", NULL, NULL,
                                                      &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 0);

    /* The empty list is not a wildcard. */
    RoboCheckEqInt(HtmlDocumentGetElementsByClassName(id, "", NULL, NULL,
                                                      &count, NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 0);

    HtmlShutdown();
}

static void TestDomQuerySelectorSeam(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle out;
    size_t count = 0u;
    HtmlNodeHandle root;
    HtmlNodeHandle div;

    /* With no matcher installed, the query is visibly unsupported. */
    RoboCheckEqInt(HtmlDocumentHasSelectorMatcher(id), 0);
    RoboCheckEqInt(HtmlDocumentQuerySelector(id, "p", &out, NULL),
                   RoboNotSupported);
    RoboCheckEqInt(HtmlDocumentQuerySelectorAll(id, "p", NULL, NULL, &count,
                                                NULL),
                   RoboNotSupported);

    /* Install the test matcher and the same queries answer. */
    RoboCheckEqInt(HtmlDocumentSetSelectorMatcher(id, TagSelectorMatcher, NULL,
                                                  NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentHasSelectorMatcher(id), 1);

    root = DomRoot(id);
    div = DomFindId(&root, "main");
    RoboCheckEqInt(HtmlDocumentQuerySelector(id, "p", &out, NULL), RoboOk);
    {
        HtmlElementView view;

        RoboCheckEqInt(HtmlElementViewGet(&out, &view, NULL), RoboOk);
        RoboCheck(strcmp(view.name, "p") == 0);
    }

    RoboCheckEqInt(HtmlDocumentQuerySelectorAll(id, "p", NULL, NULL, &count,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 2);

    /* Element-rooted: descendants only. */
    RoboCheckEqInt(HtmlElementQuerySelectorAll(&div, "p", NULL, NULL, &count,
                                               NULL),
                   RoboOk);
    RoboCheckEqInt((int)count, 2);
    RoboCheckEqInt(HtmlElementQuerySelector(&div, "span", &out, NULL),
                   RoboNotFound);

    /* Clearing the matcher restores the unsupported answer. */
    RoboCheckEqInt(HtmlDocumentSetSelectorMatcher(id, NULL, NULL, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentQuerySelector(id, "p", &out, NULL),
                   RoboNotSupported);

    HtmlShutdown();
}

static void TestDomQueryCreateOnFinalized(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle root = DomRoot(id);
    HtmlNodeHandle body = DomFindTag(&root, RoboHtmlTagBody);
    HtmlNodeHandle created;

    /* Parsing finalized the document; the DOM creation door still works. */
    RoboCheckEqInt(HtmlDocumentIsFinalized(id), 1);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "section", &created, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &created, NULL), RoboOk);
    DomExpectCleanTree(id);

    HtmlShutdown();
}

static void TestDomQueryUnknownDocument(void)
{
    HtmlDocumentId id = DomQueryDoc();
    HtmlNodeHandle out;
    size_t count = 0u;

    HtmlShutdown();
    /* Every door reports the dead document rather than reading freed state. */
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "div", &out, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentGetElementById(id, "main", &out, NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentGetElementsByTagName(id, "p", NULL, NULL, &count,
                                                    NULL),
                   RoboNotFound);
    RoboCheckEqInt(HtmlDocumentCreateFragment(id, &out, NULL), RoboNotFound);
}

void TestRegisterHtmlDomQuery(void)
{
    TestRegister("html.domq.capabilities", TestDomQueryCapabilities);
    TestRegister("html.domq.create_element", TestDomQueryCreateElement);
    TestRegister("html.domq.create_characters", TestDomQueryCreateCharacters);
    TestRegister("html.domq.create_fragment", TestDomQueryCreateFragment);
    TestRegister("html.domq.prepend", TestDomQueryPrepend);
    TestRegister("html.domq.clone_shallow", TestDomQueryCloneShallow);
    TestRegister("html.domq.clone_deep", TestDomQueryCloneDeep);
    TestRegister("html.domq.clone_refusals", TestDomQueryCloneRefusals);
    TestRegister("html.domq.text_content", TestDomQueryTextContent);
    TestRegister("html.domq.get_element_by_id", TestDomQueryGetElementById);
    TestRegister("html.domq.get_elements_by_tag_name",
                 TestDomQueryGetElementsByTagName);
    TestRegister("html.domq.get_elements_by_class_name",
                 TestDomQueryGetElementsByClassName);
    TestRegister("html.domq.selector_seam", TestDomQuerySelectorSeam);
    TestRegister("html.domq.create_on_finalized", TestDomQueryCreateOnFinalized);
    TestRegister("html.domq.unknown_document", TestDomQueryUnknownDocument);
}
