/*
 * RoboHTML Phase 8.5 element-model suite:
 * element creation, identity/validity, tag names, namespace and
 * classification, document ownership and cross-document rejection,
 * parent/child/sibling relationships, temporary attribute storage,
 * self-closing and state bits, start/end tag integration, teardown,
 * malformed recovery, Unicode text, the tree dump, and the visual demo
 * document's expected element tree.
 *
 * Note on lookups: the tree builder keeps whitespace between tags as text
 * nodes (as the 8.3 suite documents for the demo document), so a child index
 * is never an element index. Everything here finds elements either by name
 * (HtmlNodeFindElement) or through ElElementChildren, never by a
 * fixed offset into a child list.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void ElFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void ElFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId ElDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Build @p text into @p id with the one-shot API; result optional. */
static void ElBuild(HtmlDocumentId id,
                     const char *text,
                     HtmlTreeResult *out)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, out, NULL), RoboOk);
}

static HtmlNodeHandle ElRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/** Handle of the first descendant element named @p name (invalid when none). */
static HtmlNodeHandle ElFind(HtmlDocumentId id,
                                       const char *name)
{
    HtmlNodeHandle root = ElRoot(id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (HtmlNodeFindElement(&root, name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/** Child @p index (any kind); invalid when out of range. */
static HtmlNodeHandle ElChild(const HtmlNodeHandle *parent,
                                        size_t index)
{
    HtmlNodeHandle out;

    HtmlNodeHandleClear(&out);
    if (HtmlNodeChildAt(parent, index, &out, NULL) != RoboOk) {
        HtmlNodeHandleClear(&out);
    }
    return out;
}

/** Element children of @p parent in document order; returns how many. */
static size_t ElElementChildren(const HtmlNodeHandle *parent,
                                  HtmlNodeHandle *out,
                                  size_t cap)
{
    size_t children = 0u;
    size_t found = 0u;
    size_t i;

    if (!parent || !out || cap == 0u) {
        return 0u;
    }
    if (HtmlNodeChildCount(parent, &children, NULL) != RoboOk) {
        return 0u;
    }
    for (i = 0u; i < children && found < cap; i++) {
        HtmlNodeHandle child = ElChild(parent, i);

        if (!HtmlElementIsElement(&child)) {
            continue;
        }
        out[found++] = child;
    }
    return found;
}

/** Concatenated text of a subtree; "" when the node is unknown. */
static void ElText(HtmlNodeHandle h, char *buf, size_t cap)
{
    size_t length = 0u;

    buf[0] = '\0';
    if (HtmlNodeSubtreeText(&h, buf, cap, &length, NULL) != RoboOk) {
        buf[0] = '\0';
    }
}

/** Create an element under @p parent; returns an invalid handle on failure. */
static HtmlNodeHandle ElCreateUnder(
    HtmlDocumentId id,
    const HtmlNodeHandle *parent,
    const char *name,
    Result *outCode)
{
    HtmlElementCreateInfo info;
    HtmlNodeHandle out;
    Result r;

    HtmlElementCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = RoboHtmlNodeOriginTest;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = name;

    HtmlNodeHandleClear(&out);
    r = HtmlElementCreate(id, &info, &out, NULL);
    if (outCode) {
        *outCode = r;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Namespace + classification                                          */
/* ------------------------------------------------------------------ */

static void TestElNamespaceHelpers(void)
{
    ElFixtureBegin();

    RoboCheck(HtmlNamespaceIsKnown(RoboHtmlNamespaceNone));
    RoboCheck(HtmlNamespaceIsKnown(RoboHtmlNamespaceHtml));
    RoboCheck(HtmlNamespaceIsKnown(RoboHtmlNamespaceSvg));
    RoboCheck(HtmlNamespaceIsKnown(RoboHtmlNamespaceMathml));
    RoboCheck(!HtmlNamespaceIsKnown((HtmlNamespace)42));

    RoboCheckStrEq(HtmlNamespaceName(RoboHtmlNamespaceHtml),
                      "html");
    RoboCheckStrEq(HtmlNamespaceName(RoboHtmlNamespaceSvg), "svg");
    RoboCheckStrEq(HtmlNamespaceName(RoboHtmlNamespaceNone),
                      "none");
    RoboCheckStrEq(HtmlNamespaceName((HtmlNamespace)42),
                      "unknown");

    RoboCheckStrEq(HtmlNamespaceUrl(RoboHtmlNamespaceHtml),
                      "http://www.w3.org/1999/xhtml");
    RoboCheckStrEq(HtmlNamespaceUrl(RoboHtmlNamespaceSvg),
                      "http://www.w3.org/2000/svg");
    RoboCheckStrEq(HtmlNamespaceUrl(RoboHtmlNamespaceMathml),
                      "http://www.w3.org/1998/Math/MathML");
    RoboCheckStrEq(HtmlNamespaceUrl(RoboHtmlNamespaceNone), "");
    RoboCheckStrEq(HtmlNamespaceUrl((HtmlNamespace)42), "");

    /* The element domain is part of the shared diagnostic vocabulary. */
    RoboCheck(HtmlDiagnosticDomainIsKnown(RoboHtmlDomainElement));
    RoboCheckStrEq(HtmlDiagnosticDomainName(RoboHtmlDomainElement),
                      "element");

    ElFixtureEnd();
}

static void TestElClassificationHelpers(void)
{
    ElFixtureBegin();

    RoboCheck(HtmlElementClassIsKnown(RoboHtmlElementClassGeneric));
    RoboCheck(HtmlElementClassIsKnown(RoboHtmlElementClassControl));
    RoboCheck(!HtmlElementClassIsKnown(
        (HtmlElementClass)99));

    RoboCheckStrEq(
        HtmlElementClassName(RoboHtmlElementClassMetadata),
        "metadata");
    RoboCheckStrEq(
        HtmlElementClassName(RoboHtmlElementClassReplaced),
        "replaced");
    RoboCheckStrEq(
        HtmlElementClassName((HtmlElementClass)99),
        "unknown");

    /* Every element this phase claims to know classifies to its own role. */
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "html"),
        (int)RoboHtmlElementClassRoot);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "head"),
        (int)RoboHtmlElementClassMetadata);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "title"),
        (int)RoboHtmlElementClassMetadata);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "body"),
        (int)RoboHtmlElementClassSection);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "div"),
        (int)RoboHtmlElementClassSection);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "h1"),
        (int)RoboHtmlElementClassHeading);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "h6"),
        (int)RoboHtmlElementClassHeading);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "p"),
        (int)RoboHtmlElementClassParagraph);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "span"),
        (int)RoboHtmlElementClassText);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "a"),
        (int)RoboHtmlElementClassText);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "img"),
        (int)RoboHtmlElementClassReplaced);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "button"),
        (int)RoboHtmlElementClassControl);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "input"),
        (int)RoboHtmlElementClassControl);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "form"),
        (int)RoboHtmlElementClassControl);

    /* An unknown name is GENERIC, never an error. */
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "my-widget"),
        (int)RoboHtmlElementClassGeneric);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, NULL),
        (int)RoboHtmlElementClassGeneric);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, ""),
        (int)RoboHtmlElementClassGeneric);

    /* The same local name in another namespace is not the HTML element. */
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceSvg, "a"),
        (int)RoboHtmlElementClassGeneric);
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceNone, "html"),
        (int)RoboHtmlElementClassGeneric);

    /* Case-insensitive, because callers hold names from several sources. */
    RoboCheckEqInt(
        (int)HtmlElementClassify(RoboHtmlNamespaceHtml, "DIV"),
        (int)RoboHtmlElementClassSection);

    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void TestElCreateInfoInit(void)
{
    HtmlElementCreateInfo info;
    HtmlElementCreateInfo dirty;

    ElFixtureBegin();

    memset(&dirty, 0xAB, sizeof(dirty));
    HtmlElementCreateInfoInit(&dirty);
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&dirty.parent), 0);
    RoboCheckEqInt((int)dirty.origin, (int)RoboHtmlNodeOriginNone);
    RoboCheckEqInt((int)dirty.namespaceId, (int)RoboHtmlNamespaceNone);
    RoboCheck(dirty.localName == NULL);
    RoboCheck(dirty.attributes == NULL);
    RoboCheckEqInt((int)dirty.attributeCount, 0);
    RoboCheckEqInt(dirty.selfClosing, 0);

    /* NULL-safe. */
    HtmlElementCreateInfoInit(NULL);

    memset(&info, 0, sizeof(info));
    HtmlElementCreateInfoInit(&info);
    RoboCheckEqInt((int)info.namespaceId, (int)RoboHtmlNamespaceNone);

    ElFixtureEnd();
}

static void TestElCreateBasic(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle div;
    HtmlNodeHandle span;
    HtmlNodeHandle parent;
    HtmlElementView view;
    HtmlNodeKind kind = RoboHtmlNodeKindDocument;
    HtmlTreeReport report;
    size_t count = 0u;

    ElFixtureBegin();
    id = ElDoc();
    root = ElRoot(id);

    div = ElCreateUnder(id, &root, "div", NULL);
    RoboCheck(HtmlNodeHandleIsWellformed(&div));

    /* Identity: the element IS a node of this document. */
    RoboCheckEqInt((int)HtmlNodeHandleDocument(&div), (int)id);
    RoboCheck(HtmlDocumentOwnsNode(id, &div));
    RoboCheckEqInt(HtmlNodeHandleResolve(&div, &kind), RoboOk);
    RoboCheckEqInt((int)kind, (int)RoboHtmlNodeKindElement);
    RoboCheck(HtmlElementIsElement(&div));

    /* Shape. */
    RoboCheckEqInt(HtmlElementViewGet(&div, &view, NULL), RoboOk);
    RoboCheckStrEq(view.name, "div");
    RoboCheckEqInt((int)view.nameLength, 3);
    RoboCheckEqInt((int)view.namespaceId, (int)RoboHtmlNamespaceHtml);
    RoboCheckEqInt((int)view.classification,
                      (int)RoboHtmlElementClassSection);
    RoboCheckEqInt((int)view.attributeCount, 0);
    RoboCheckEqInt(view.selfClosing, 0);
    /* Created under the document root with the TEST origin: connected, but
     * not parser-created and not self-closing. */
    RoboCheck(view.state & RoboHtmlElementStateConnected);
    RoboCheck(!(view.state & RoboHtmlElementStateParserCreated));
    RoboCheck(!(view.state & RoboHtmlElementStateSelfClosing));

    /* Relationships: linked, never detached. */
    RoboCheckEqInt(HtmlElementParent(&div, &parent, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&parent, &root));
    RoboCheckEqInt(HtmlNodeChildCount(&root, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);
    {
        HtmlNodeHandle first = ElChild(&root, 0u);

        RoboCheck(HtmlNodeHandleEqual(&first, &div));
    }

    /* A second element lands after the first: creation order is tree order. */
    span = ElCreateUnder(id, &root, "span", NULL);
    RoboCheck(HtmlElementIsElement(&span));
    RoboCheckEqInt(HtmlElementIsNamed(&span, "span"), 1);
    {
        size_t elements;
        HtmlNodeHandle list[4];

        elements = ElElementChildren(&root, list, 4u);
        RoboCheckEqInt((int)elements, 2);
        RoboCheck(HtmlNodeHandleEqual(&list[0], &div));
        RoboCheck(HtmlNodeHandleEqual(&list[1], &span));
    }

    /* The tree is internally consistent after direct creation. */
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheck(report.ok);
    RoboCheckEqInt((int)report.nodes, 3); /* root + div + span */

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElCreateValidation(void)
{
    HtmlDocumentId id;
    HtmlDocumentId other;
    HtmlNodeHandle root;
    HtmlNodeHandle foreignRoot;
    HtmlElementCreateInfo info;
    HtmlNodeHandle out;
    HtmlDiag diag;

    ElFixtureBegin();
    id = ElDoc();
    other = ElDoc();
    root = ElRoot(id);
    foreignRoot = ElRoot(other);

    HtmlElementCreateInfoInit(&info);
    info.parent = root;
    info.origin = RoboHtmlNodeOriginTest;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = "div";

    /* NULL arguments. */
    RoboCheckEqInt(HtmlElementCreate(id, NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementCreate(id, &info, NULL, NULL),
                      RoboInvalidArgument);

    /* Missing / empty name. */
    info.localName = NULL;
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainElement);
    info.localName = "";
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);
    info.localName = "div";

    /* Namespace must be live and not NONE. */
    info.namespaceId = RoboHtmlNamespaceNone;
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainElement);
    info.namespaceId = (HtmlNamespace)99;
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);
    info.namespaceId = RoboHtmlNamespaceHtml;

    /* Attributes claimed without an array, and an unknown origin. */
    info.attributeCount = 1u;
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);
    info.attributeCount = 0u;
    info.origin = (HtmlNodeOrigin)99;
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);
    info.origin = RoboHtmlNodeOriginTest;

    /* Parent must be well-formed, same-document and live. */
    {
        HtmlNodeHandle bad;

        HtmlNodeHandleClear(&bad);
        info.parent = bad;
        RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                          RoboInvalidArgument);
    }
    info.parent = foreignRoot;
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainElement);
    /* ... and the cross-document attempt must not attach anything to
     * either document. */
    RoboCheckEqInt((int)HtmlDocumentNodeCount(id), 1);
    RoboCheckEqInt((int)HtmlDocumentNodeCount(other), 1);
    {
        HtmlNodeHandle ghost;

        ghost.documentId = id;
        ghost.nodeId = (HtmlNodeId)9999;
        ghost.generation = 1u;
        info.parent = ghost;
        RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                          RoboNotFound);
        RoboCheckEqInt((int)HtmlDocumentNodeCount(id), 1);
    }
    info.parent = root;

    /* Unknown document. */
    RoboCheckEqInt(
        HtmlElementCreate(RoboHtmlDocumentIdNone, &info, &out, NULL),
        RoboNotFound);

    /* Failure never leaves a partially valid handle behind. */
    RoboCheck(!HtmlNodeHandleIsWellformed(&out));

    /* A destroyed document cannot create, and neither can a retired id. */
    RoboCheckEqInt(HtmlDocumentDestroy(other, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementCreate(other, &info, &out, NULL),
                      RoboNotFound);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElCreateRefusedAfterBuildAndFailure(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlElementCreateInfo info;
    HtmlNodeHandle out;

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id, "<p>done</p>", NULL);
    RoboCheck(HtmlDocumentIsFinalized(id));

    root = HtmlDocumentRoot(id);
    HtmlElementCreateInfoInit(&info);
    info.parent = root;
    info.origin = RoboHtmlNodeOriginDom;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = "div";

    /* A completed build closes construction. */
    RoboCheckEqInt((int)HtmlDocumentAllowsConstruction(id), 0);
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                      RoboError);
    RoboCheck(!HtmlNodeHandleIsWellformed(&out));

    /* Reopening is explicit. */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt((int)HtmlDocumentAllowsConstruction(id), 1);
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL), RoboOk);
    RoboCheck(HtmlElementIsElement(&out));
    RoboCheckEqInt((int)HtmlElementClassification(&out),
                      (int)RoboHtmlElementClassSection);

    /* A FAILED document refuses creation. */
    RoboCheckEqInt(HtmlDocumentMarkFailed(
                          id, RoboHtmlDomainParser, "test", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                      RoboError);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Identity, names and state                                           */
/* ------------------------------------------------------------------ */

static void TestElIdentityAndValidity(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle html;
    HtmlNodeHandle head;
    HtmlNodeHandle body;
    HtmlNodeHandle h1;
    HtmlNodeHandle text;
    HtmlNodeHandle comment;
    HtmlNodeHandle doctype;
    HtmlNodeHandle missing;
    HtmlNodeHandle fresh;
    HtmlElementView view;
    HtmlDiag diag;

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id,
             "<!DOCTYPE html><html><head><title>T</title></head>"
             "<body><h1>Hi</h1><!-- c --></body></html>",
             NULL);

    root = ElRoot(id);
    html = ElFind(id, "html");
    h1 = ElFind(id, "h1");
    missing = ElFind(id, "video");
    RoboCheck(!HtmlNodeHandleIsWellformed(&missing));

    /* The document node is not an element. */
    RoboCheckEqInt(HtmlElementIsElement(&root), 0);
    RoboCheckEqInt(HtmlElementViewGet(&root, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementViewGet(&root, &view, &diag),
                      RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainNode);

    /* An element resolves, and comparison is case-insensitive. */
    RoboCheckEqInt(HtmlElementIsElement(&h1), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&h1, "h1"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&h1, "H1"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&h1, "p"), 0);

    /* A text child is a node, not an element. */
    text = ElChild(&h1, 0u);
    RoboCheck(HtmlNodeHandleIsWellformed(&text));
    RoboCheckEqInt(HtmlElementIsElement(&text), 0);
    RoboCheckEqInt(HtmlElementIsNamed(&text, "h1"), 0);
    RoboCheckStrEq(HtmlElementName(&text, NULL), "");
    RoboCheckEqInt((int)HtmlElementNamespace(&text),
                      (int)RoboHtmlNamespaceNone);
    RoboCheckEqInt((int)HtmlElementClassification(&text),
                      (int)RoboHtmlElementClassGeneric);
    RoboCheckEqInt(HtmlElementViewGet(&text, &view, NULL),
                      RoboNotFound);

    /* A comment child is not an element either. */
    head = ElChild(&html, 0u);
    body = ElChild(&html, 1u);
    RoboCheckEqInt(HtmlElementIsNamed(&head, "head"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&body, "body"), 1);
    comment = ElChild(&body, 1u);
    RoboCheck(HtmlNodeHandleIsWellformed(&comment));
    RoboCheckEqInt(HtmlElementIsElement(&comment), 0);

    /* Neither is a DOCTYPE node. */
    doctype = ElChild(&root, 0u);
    RoboCheck(HtmlNodeHandleIsWellformed(&doctype));
    RoboCheckEqInt(HtmlElementIsElement(&doctype), 0);
    {
        HtmlNodeKind kind = RoboHtmlNodeKindElement;

        RoboCheckEqInt(HtmlNodeHandleResolve(&doctype, &kind),
                          RoboOk);
        RoboCheckEqInt((int)kind, (int)RoboHtmlNodeKindDocumentType);
    }

    /* Malformed and NULL handles are refused, never dereferenced. */
    {
        HtmlNodeHandle malformed;
        HtmlNodeHandle foreign;

        HtmlNodeHandleClear(&malformed);
        RoboCheckEqInt(HtmlElementIsElement(&malformed), 0);
        RoboCheckEqInt(HtmlElementIsElement(NULL), 0);
        RoboCheckEqInt(HtmlElementIsNamed(NULL, "h1"), 0);
        RoboCheckEqInt(HtmlElementIsNamed(&h1, NULL), 0);
        RoboCheckEqInt(HtmlElementIsNamed(&h1, ""), 0);
        RoboCheckEqInt(HtmlElementViewGet(NULL, &view, NULL),
                          RoboInvalidArgument);

        foreign = h1;
        foreign.documentId += 1000u;
        RoboCheckEqInt(HtmlElementIsElement(&foreign), 0);
        RoboCheckStrEq(HtmlElementName(&foreign, NULL), "");
    }

    /* A reset retires every element handle from the discarded tree. */
    {
        HtmlNodeHandle staleH1 = h1;
        HtmlNodeHandle staleHtml = html;
        size_t length = 7u;

        RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleIsStale(&staleH1));
        RoboCheck(HtmlNodeHandleIsStale(&staleHtml));
        RoboCheckEqInt(HtmlElementIsElement(&staleH1), 0);
        RoboCheckEqInt(HtmlElementIsNamed(&staleH1, "h1"), 0);
        RoboCheckStrEq(HtmlElementName(&staleH1, &length), "");
        RoboCheckEqInt((int)length, 0);
        RoboCheckEqInt((int)HtmlElementNamespace(&staleH1),
                          (int)RoboHtmlNamespaceNone);
        RoboCheckEqInt(HtmlElementViewGet(&staleH1, &view, NULL),
                          RoboNotFound);
    }

    /* Rebuilding hands out fresh handles for the same names. */
    ElBuild(id, "<html><body><h1>Again</h1></body></html>", NULL);
    fresh = ElFind(id, "h1");
    RoboCheck(HtmlElementIsElement(&fresh));
    RoboCheckEqInt(HtmlElementIsNamed(&fresh, "h1"), 1);
    RoboCheck(!HtmlNodeHandleEqual(&fresh, &h1));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElTagNames(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle upper;
    HtmlNodeHandle custom;
    HtmlNodeHandle longEl;
    char longName[128];
    char expected[128];
    const char *stored;
    size_t length = 0u;
    size_t i;

    ElFixtureBegin();
    id = ElDoc();
    root = ElRoot(id);

    /* Creation lowercases, so the stored-name invariant holds for the DOM
     * seam too, not only for parser input. */
    upper = ElCreateUnder(id, &root, "SeCtIoN", NULL);
    RoboCheck(HtmlElementIsElement(&upper));
    RoboCheckStrEq(HtmlElementName(&upper, &length), "section");
    RoboCheckEqInt((int)length, 7);
    RoboCheckEqInt(HtmlElementIsNamed(&upper, "SECTION"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&upper, "section"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&upper, "div"), 0);
    RoboCheckEqInt((int)HtmlElementClassification(&upper),
                      (int)RoboHtmlElementClassSection);

    /* Unknown / custom-looking names are ordinary elements. */
    custom = ElCreateUnder(id, &root, "my-widget-2", NULL);
    RoboCheck(HtmlElementIsElement(&custom));
    RoboCheckStrEq(HtmlElementName(&custom, NULL), "my-widget-2");
    RoboCheckEqInt((int)HtmlElementClassification(&custom),
                      (int)RoboHtmlElementClassGeneric);
    RoboCheckEqInt(HtmlElementIsNamed(&custom, "MY-WIDGET-2"), 1);

    /* A name longer than the normalization buffer is stored verbatim: it is
     * never truncated, because shrinking a tag name would change the
     * element's identity. */
    for (i = 0u; i < 100u; i++) {
        longName[i] = (i % 2u == 0u) ? 'A' : 'b';
        expected[i] = (i % 2u == 0u) ? 'A' : 'b'; /* unchanged */
    }
    longName[100] = '\0';
    expected[100] = '\0';
    longEl = ElCreateUnder(id, &root, longName, NULL);
    RoboCheck(HtmlElementIsElement(&longEl));
    stored = HtmlElementName(&longEl, &length);
    RoboCheckEqInt((int)length, 100);
    RoboCheckStrEq(stored, expected);
    RoboCheckEqInt(HtmlElementIsNamed(&longEl, longName), 1);

    /* Empty and NULL names are rejected at creation, so no element can ever
     * exist with an empty tag name. */
    {
        Result code = RoboOk;
        HtmlNodeHandle empty = ElCreateUnder(id, &root, "", &code);

        RoboCheck(!HtmlNodeHandleIsWellformed(&empty));
        RoboCheckEqInt(code, RoboInvalidArgument);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElStateBits(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle img;
    HtmlNodeHandle br;
    HtmlElementView view;

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id, "<html><body><img src=test.png/><br><p>x</p></body></html>",
             NULL);
    img = ElFind(id, "img");
    br = ElFind(id, "br");

    RoboCheckEqInt(HtmlElementViewGet(&img, &view, NULL), RoboOk);
    RoboCheckEqInt(view.selfClosing, 1);
    RoboCheck(view.state & RoboHtmlElementStateSelfClosing);
    RoboCheck(view.state & RoboHtmlElementStateParserCreated);
    RoboCheck(view.state & RoboHtmlElementStateConnected);
    RoboCheckEqInt((int)view.classification,
                      (int)RoboHtmlElementClassReplaced);

    /* A void element written without "/" is not self-closing. */
    RoboCheckEqInt(HtmlElementViewGet(&br, &view, NULL), RoboOk);
    RoboCheckEqInt(view.selfClosing, 0);
    RoboCheck(!(view.state & RoboHtmlElementStateSelfClosing));
    RoboCheck(view.state & RoboHtmlElementStateParserCreated);
    RoboCheck(view.state & RoboHtmlElementStateConnected);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Relationships                                                       */
/* ------------------------------------------------------------------ */

static void TestElRelationships(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle html;
    HtmlNodeHandle head;
    HtmlNodeHandle body;
    HtmlNodeHandle parent;
    HtmlNodeKind kind = RoboHtmlNodeKindElement;
    HtmlNodeView bodyView;
    size_t count = 0u;

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id, "<html><head><title>T</title></head><body><p>a</p></body></html>",
             NULL);
    root = ElRoot(id);
    html = ElFind(id, "html");
    head = ElFind(id, "head");
    body = ElFind(id, "body");

    /* The document element's parent is the document node itself. */
    RoboCheckEqInt(HtmlElementParent(&html, &parent, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&parent, &root));
    RoboCheckEqInt(HtmlNodeHandleResolve(&parent, &kind), RoboOk);
    RoboCheckEqInt((int)kind, (int)RoboHtmlNodeKindDocument);

    RoboCheckEqInt(HtmlElementParent(&body, &parent, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&parent, &html));

    /* The document node is not an element, so it has no element parent. */
    RoboCheckEqInt(HtmlElementParent(&root, &parent, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&parent));

    /* Element children of <html> are head then body. */
    {
        HtmlNodeHandle list[4];
        size_t elements = ElElementChildren(&html, list, 4u);

        RoboCheckEqInt((int)elements, 2);
        RoboCheck(HtmlNodeHandleEqual(&list[0], &head));
        RoboCheck(HtmlNodeHandleEqual(&list[1], &body));
    }

    RoboCheckEqInt(HtmlNodeChildCount(&html, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 2);

    /* Out of range is a miss, never a zeroed-but-successful handle. */
    HtmlNodeHandleClear(&parent);
    RoboCheckEqInt(HtmlNodeChildAt(&html, 2u, &parent, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&parent));

    /* Sibling links agree with element order. */
    RoboCheckEqInt(HtmlNodeGetView(&body, &bodyView, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&bodyView.prevSibling, &head));
    RoboCheck(!HtmlNodeHandleIsWellformed(&bodyView.NextSibling));

    /* Misuse is reported, not undefined. */
    RoboCheckEqInt(HtmlElementParent(NULL, &parent, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementParent(&html, NULL, NULL),
                      RoboInvalidArgument);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElNestingAndSiblings(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle outer;
    HtmlNodeHandle inner;
    HtmlNodeHandle p1;
    HtmlNodeHandle p2;
    HtmlNodeHandle up;
    char text[64];

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id,
             "<html><body><div class='x'><span><a href='/'>link</a></span></div>"
             "<p>one</p><p>two</p></body></html>",
             NULL);

    outer = ElFind(id, "div");
    inner = ElFind(id, "span");
    p1 = ElFind(id, "p");

    RoboCheck(HtmlElementIsNamed(&outer, "div"));
    RoboCheck(HtmlElementIsNamed(&inner, "span"));
    RoboCheckEqInt((int)HtmlElementClassification(&inner),
                      (int)RoboHtmlElementClassText);
    RoboCheckEqInt((int)HtmlElementClassification(&p1),
                      (int)RoboHtmlElementClassParagraph);

    /* The nested element sits under the right ancestors. */
    RoboCheckEqInt(HtmlElementParent(&inner, &up, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&up, &outer));
    RoboCheckEqInt(HtmlElementParent(&outer, &up, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementIsNamed(&up, "body"), 1);

    /* Siblings in document order: div, p, p. */
    {
        HtmlNodeHandle body = ElFind(id, "body");
        HtmlNodeHandle list[8];
        size_t elements = ElElementChildren(&body, list, 8u);

        RoboCheckEqInt((int)elements, 3);
        RoboCheck(HtmlNodeHandleEqual(&list[0], &outer));
        RoboCheck(HtmlNodeHandleEqual(&list[1], &p1));
        p2 = list[2];
        RoboCheck(HtmlElementIsNamed(&p2, "p"));
        RoboCheck(!HtmlNodeHandleEqual(&p1, &p2));
    }

    ElText(inner, text, sizeof(text));
    RoboCheckStrEq(text, "link");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElOwnershipRules(void)
{
    HtmlDocumentId a;
    HtmlDocumentId b;
    HtmlNodeHandle aRoot;
    HtmlNodeHandle bRoot;
    HtmlNodeHandle aChild;
    HtmlNodeHandle bChild;

    ElFixtureBegin();
    a = ElDoc();
    b = ElDoc();
    aRoot = ElRoot(a);
    bRoot = ElRoot(b);

    aChild = ElCreateUnder(a, &aRoot, "div", NULL);
    bChild = ElCreateUnder(b, &bRoot, "div", NULL);
    RoboCheck(HtmlElementIsElement(&aChild));
    RoboCheck(HtmlElementIsElement(&bChild));

    /* One document owns its element and not the other's. */
    RoboCheck(HtmlDocumentOwnsNode(a, &aChild));
    RoboCheck(!HtmlDocumentOwnsNode(a, &bChild));
    RoboCheck(HtmlDocumentOwnsNode(b, &bChild));
    RoboCheck(!HtmlDocumentOwnsNode(b, &aChild));

    RoboCheck(!HtmlNodeHandleEqual(&aChild, &bChild));
    RoboCheckEqInt((int)HtmlNodeHandleDocument(&aChild), (int)a);
    RoboCheckEqInt((int)HtmlNodeHandleDocument(&bChild), (int)b);

    /*
     * A handle is inert identity, not a capability: the document id is what
     * separates the two documents' node 2, and ownership compares the whole
     * triple. A handle whose document id names a different live document is
     * simply not owned here, and a generation that was never issued for this
     * document is not owned either.
     */
    {
        HtmlNodeHandle retired = aChild;

        retired.generation += 1u;
        RoboCheck(!HtmlDocumentOwnsNode(a, &retired));
        RoboCheckEqInt(HtmlElementIsElement(&retired), 0);
    }

    /* Destroying A retires A's elements and leaves B untouched. */
    RoboCheckEqInt(HtmlDocumentDestroy(a, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsStale(&aChild));
    RoboCheckEqInt(HtmlElementIsElement(&aChild), 0);
    RoboCheck(HtmlElementIsElement(&bChild));
    RoboCheck(HtmlDocumentOwnsNode(b, &bChild));

    RoboCheckEqInt(HtmlDocumentDestroy(b, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Attributes (the 8.6 boundary, as shepherded in 8.5)                 */
/* ------------------------------------------------------------------ */

static void TestElAttributes(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle div;
    HtmlNodeHandle input;
    HtmlNodeHandle p;
    HtmlNodeHandle text;
    HtmlElementView view;
    HtmlAttribute attr;
    size_t count = 0u;

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id,
             "<html><body><div CLASS='Test' id=\"main\" data-x='1' class='second'>"
             "<p>x</p></div><input disabled value='hello world'></body></html>",
             NULL);
    div = ElFind(id, "div");
    input = ElFind(id, "input");
    p = ElFind(id, "p");
    text = ElChild(&p, 0u);
    RoboCheckEqInt(HtmlElementIsElement(&text), 0);

    /* Count + iteration in source order, duplicates preserved. */
    RoboCheckEqInt(HtmlElementAttributeCount(&div, &count, NULL),
                      RoboOk);
    RoboCheckEqInt((int)count, 4);
    RoboCheckEqInt(HtmlElementViewGet(&div, &view, NULL), RoboOk);
    RoboCheckEqInt((int)view.attributeCount, 4);

    RoboCheckEqInt(HtmlElementAttributeAt(&div, 0u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "class"); /* lowercased by the tokenizer */
    RoboCheckStrEq(attr.value, "Test");
    RoboCheckEqInt(HtmlElementAttributeAt(&div, 1u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "id");
    RoboCheckStrEq(attr.value, "main");
    RoboCheckEqInt(HtmlElementAttributeAt(&div, 2u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "data-x");
    RoboCheckStrEq(attr.value, "1");
    RoboCheckEqInt(HtmlElementAttributeAt(&div, 3u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.value, "second"); /* the duplicate stays one */

    /* Out of range and misuse. */
    RoboCheckEqInt(HtmlElementAttributeAt(&div, 4u, &attr, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlElementAttributeAt(&div, 0u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementAttributeAt(NULL, 0u, &attr, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementAttributeCount(NULL, &count, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementAttributeCount(&div, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementAttributeCount(&text, &count, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlElementAttributeAt(&text, 0u, &attr, NULL),
                      RoboNotFound);

    /* Lookup by name is case-insensitive; the first match wins. */
    RoboCheckEqInt(HtmlElementFindAttribute(&div, "id", &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.value, "main");
    RoboCheckEqInt(
        HtmlElementFindAttribute(&div, "CLASS", &attr, NULL), RoboOk);
    RoboCheckStrEq(attr.value, "Test");
    RoboCheckEqInt(
        HtmlElementFindAttribute(&div, "missing", &attr, NULL),
        RoboNotFound);
    RoboCheckEqInt(HtmlElementFindAttribute(&div, NULL, &attr, NULL),
                      RoboInvalidArgument);

    /* Presence is a separate question from value. */
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "id"), 1);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "ID"), 1);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, "nope"), 0);
    RoboCheckEqInt(HtmlElementHasAttribute(&div, NULL), 0);
    RoboCheckEqInt(HtmlElementHasAttribute(NULL, "id"), 0);

    /* A boolean-style attribute is present with an empty value. */
    RoboCheckEqInt(HtmlElementHasAttribute(&input, "disabled"), 1);
    RoboCheckEqInt(
        HtmlElementFindAttribute(&input, "disabled", &attr, NULL),
        RoboOk);
    RoboCheckStrEq(attr.value, "");
    RoboCheckEqInt(HtmlElementAttributeCount(&input, &count, NULL),
                      RoboOk);
    RoboCheckEqInt((int)count, 2);
    RoboCheckEqInt(HtmlElementAttributeAt(&input, 1u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "value");
    RoboCheckStrEq(attr.value, "hello world");

    /* An element created with no attributes reports none. */
    RoboCheckEqInt(HtmlElementAttributeCount(&p, &count, NULL),
                      RoboOk);
    RoboCheckEqInt((int)count, 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Tree builder integration                                            */
/* ------------------------------------------------------------------ */

static void TestElStartAndEndTags(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle html;
    HtmlNodeHandle h1;
    HtmlNodeHandle title;
    HtmlNodeHandle up;
    HtmlElementView view;
    HtmlTreeResult result;
    char text[64];

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id,
             "<html><head><title>T</title></head><body><h1>Hello</h1></body></html>",
             &result);

    RoboCheckEqInt((int)result.elementCount, 5); /* html head title h1 body */
    RoboCheckEqInt((int)result.textCount, 2);    /* T, Hello */
    RoboCheckEqInt((int)result.truncated, 0);

    html = ElFind(id, "html");
    h1 = ElFind(id, "h1");
    title = ElFind(id, "title");

    RoboCheckEqInt(HtmlElementViewGet(&html, &view, NULL), RoboOk);
    RoboCheckEqInt((int)view.namespaceId, (int)RoboHtmlNamespaceHtml);
    RoboCheckEqInt((int)view.classification,
                      (int)RoboHtmlElementClassRoot);
    RoboCheck(view.state & RoboHtmlElementStateParserCreated);
    RoboCheck(view.state & RoboHtmlElementStateConnected);

    RoboCheck(HtmlElementIsNamed(&title, "title"));
    RoboCheckEqInt((int)HtmlElementClassification(&title),
                      (int)RoboHtmlElementClassMetadata);
    RoboCheck(HtmlElementIsNamed(&h1, "h1"));
    RoboCheckEqInt((int)HtmlElementClassification(&h1),
                      (int)RoboHtmlElementClassHeading);

    /* The end tag closed <h1>: its text is a child, and the heading's parent
     * is <body>, so a later sibling would land there and not inside it. */
    ElText(h1, text, sizeof(text));
    RoboCheckStrEq(text, "Hello");
    RoboCheckEqInt(HtmlElementParent(&h1, &up, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementIsNamed(&up, "body"), 1);
    RoboCheckEqInt(HtmlElementParent(&title, &up, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementIsNamed(&up, "head"), 1);

    /* Every element this phase claims to support exists, named and in the
     * HTML namespace. */
    ElBuild(id,
             "<html><head><title>T</title></head><body><h1>a</h1><p>b</p>"
             "<div>c</div><span>d</span><button>e</button>"
             "<img src=x><a href=y>f</a><form><input></form></body></html>",
             NULL);
    {
        static const char *const kNames[] = {
            "html", "head", "title", "body", "h1",   "p",     "div",
            "span", "button", "img",   "a",    "form",  "input",
        };
        size_t i;

        for (i = 0u; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
            HtmlNodeHandle h = ElFind(id, kNames[i]);

            RoboCheckMsg(HtmlElementIsElement(&h), kNames[i]);
            RoboCheckEqInt(HtmlElementIsNamed(&h, kNames[i]), 1);
            RoboCheckEqInt((int)HtmlElementNamespace(&h),
                              (int)RoboHtmlNamespaceHtml);
        }
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElMalformedRecovery(void)
{
    static const char *const kCases[] = {
        "",
        "<",
        "</",
        "<div",
        "<div a=",
        "<div a=\"",
        "<div a='x'/",
        "</>",
        "<h1>x</h2>",
        "<div><p>unclosed",
        "<p>a<p>b",
        "<!-- unterminated",
        "<!DOCTYPE",
        "<div></div></div>",
        "text before html",
    };
    size_t i;

    ElFixtureBegin();

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        HtmlDocumentId id = ElDoc();
        HtmlTreeReport report;
        HtmlNodeHandle root;
        size_t children = 0u;
        size_t index;

        ElBuild(id, kCases[i], NULL);
        root = ElRoot(id);
        RoboCheck(HtmlNodeHandleIsWellformed(&root));

        /* Whatever the builder decided, the tree stays internally valid and
         * every element in it is an identifiable HTML element. */
        RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                          RoboOk);
        RoboCheckMsg(report.ok, kCases[i]);
        RoboCheckEqInt(HtmlNodeChildCount(&root, &children, NULL),
                          RoboOk);
        for (index = 0u; index < children; index++) {
            HtmlNodeHandle child = ElChild(&root, index);

            if (!HtmlElementIsElement(&child)) {
                continue;
            }
            RoboCheckMsg(
                HtmlElementNamespace(&child) == RoboHtmlNamespaceHtml,
                kCases[i]);
        }
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    ElFixtureEnd();
}

static void TestElUnicodeText(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle p;
    char text[128];

    ElFixtureBegin();
    id = ElDoc();
    /* UTF-8 passes through unchanged: deciding what an element is must not
     * mangle what it contains. */
    ElBuild(id, "<html><body><p>Héllo — 日本語 🚀</p></body></html>", NULL);
    p = ElFind(id, "p");
    RoboCheck(HtmlElementIsNamed(&p, "p"));
    ElText(p, text, sizeof(text));
    RoboCheckStrEq(text, "Héllo — 日本語 🚀");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Teardown                                                            */
/* ------------------------------------------------------------------ */

static void TestElTeardown(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle h1;
    HtmlNodeHandle body;
    HtmlElementView view;

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id, "<html><body><h1>Hi</h1></body></html>", NULL);
    h1 = ElFind(id, "h1");
    body = ElFind(id, "body");
    RoboCheck(HtmlElementIsElement(&h1));

    /* A reset drops the whole element tree and retires every handle. */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsStale(&h1));
    RoboCheck(HtmlNodeHandleIsStale(&body));
    RoboCheckEqInt(HtmlElementViewGet(&h1, &view, NULL),
                      RoboNotFound);
    RoboCheckEqInt((int)HtmlDocumentNodeCount(id), 1);

    /* The document survives the reset and can build again. */
    ElBuild(id, "<html><body><p>second life</p></body></html>", NULL);
    {
        HtmlNodeHandle p = ElFind(id, "p");
        char text[32];

        RoboCheck(HtmlElementIsNamed(&p, "p"));
        ElText(p, text, sizeof(text));
        RoboCheckStrEq(text, "second life");
        /* The builder implies the structural elements, so a document that
         * names only html/body/p still has a head: document, html, head,
         * body, p and the text node. */
        RoboCheckEqInt((int)HtmlDocumentNodeCount(id), 6);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    /* Destroy is terminal and repeatable. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlElementIsElement(&h1), 0);

    ElFixtureEnd();
}

static void TestElLargeTreeTeardown(void)
{
    HtmlDocumentId id;
    char source[8192];
    size_t used = 0u;
    size_t i;
    HtmlTreeResult result;
    HtmlNodeHandle deepest;
    HtmlTreeReport report;

    ElFixtureBegin();
    id = ElDoc();

    /* A deep chain. Ownership is what makes teardown one operation rather
     * than a walk, so building many elements must not change how cleanly
     * they go away. */
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "<html><body>");
    for (i = 0u; i < 200u; i++) {
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 "<div>");
    }
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "<p>deep</p>");
    for (i = 0u; i < 200u; i++) {
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 "</div>");
    }
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "</body></html>");
    ElBuild(id, source, &result);

    RoboCheck(result.elementCount >= 200u);
    deepest = ElFind(id, "p");
    RoboCheck(HtmlElementIsNamed(&deepest, "p"));
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheck(report.ok);
    RoboCheck(report.maxDepth >= 200u);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Debug output + the demo document                                    */
/* ------------------------------------------------------------------ */

static void TestElTreeDump(void)
{
    HtmlDocumentId id;
    char dump[1024];
    size_t length = 0u;

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id,
             "<!DOCTYPE html><html><head><title>T</title></head>"
             "<body><h1>Hi</h1><button>Go</button></body></html>",
             NULL);
    dump[0] = '\0';
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), &length, NULL),
        RoboOk);
    RoboCheck(length > 0u);

    /* The dump identifies elements by name, so it is usable for debugging
     * element-model problems without a viewer. */
    RoboCheck(strstr(dump, "Document") != NULL);
    RoboCheck(strstr(dump, "doctype html") != NULL);
    RoboCheck(strstr(dump, "head") != NULL);
    RoboCheck(strstr(dump, "title") != NULL);
    RoboCheck(strstr(dump, "body") != NULL);
    RoboCheck(strstr(dump, "h1") != NULL);
    RoboCheck(strstr(dump, "button") != NULL);
    RoboCheck(strstr(dump, "Text: \"Hi\"") != NULL);
    /* HTML names are printed unprefixed. */
    RoboCheck(strstr(dump, "html:") == NULL);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElDemoDocumentTree(void)
{
    static const char *const kDemo =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<title>RoboticelaOS Runtime</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>RoboticelaOS Runtime</h1>\n"
        "<p>Independent HTML rendering is working.</p>\n"
        "<p>RoboGraphics + RoboWindow + RoboHTML</p>\n"
        "<button>Demo Button</button>\n"
        "</body>\n"
        "</html>\n";
    HtmlDocumentId id;
    HtmlTreeResult result;
    HtmlTreeReport report;
    HtmlNodeHandle html;
    HtmlNodeHandle head;
    HtmlNodeHandle body;
    HtmlNodeHandle list[8];
    size_t elements;
    char text[128];

    ElFixtureBegin();
    id = ElDoc();
    ElBuild(id, kDemo, &result);

    /* Document → html → head/title + body/h1/p/p/button. Element counts are
     * independent of the whitespace text nodes the builder keeps. */
    RoboCheckEqInt((int)result.elementCount, 8);
    RoboCheckEqInt((int)result.doctypeCount, 1);
    RoboCheckEqInt((int)result.commentCount, 0);
    RoboCheckEqInt((int)result.attributeCount, 0);
    RoboCheckEqInt((int)result.truncated, 0);

    html = ElFind(id, "html");
    RoboCheckEqInt((int)HtmlElementClassification(&html),
                      (int)RoboHtmlElementClassRoot);

    head = ElFind(id, "head");
    body = ElFind(id, "body");
    RoboCheckEqInt((int)HtmlElementClassification(&head),
                      (int)RoboHtmlElementClassMetadata);
    RoboCheckEqInt((int)HtmlElementClassification(&body),
                      (int)RoboHtmlElementClassSection);

    /* <head> holds exactly <title>. */
    elements = ElElementChildren(&head, list, 8u);
    RoboCheckEqInt((int)elements, 1);
    RoboCheck(HtmlElementIsNamed(&list[0], "title"));
    RoboCheckEqInt((int)HtmlElementClassification(&list[0]),
                      (int)RoboHtmlElementClassMetadata);
    ElText(list[0], text, sizeof(text));
    RoboCheckStrEq(text, "RoboticelaOS Runtime");

    /* <body> holds exactly h1, p, p, button — in that order. */
    elements = ElElementChildren(&body, list, 8u);
    RoboCheckEqInt((int)elements, 4);
    RoboCheckEqInt(HtmlElementIsNamed(&list[0], "h1"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&list[1], "p"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&list[2], "p"), 1);
    RoboCheckEqInt(HtmlElementIsNamed(&list[3], "button"), 1);

    RoboCheckEqInt((int)HtmlElementClassification(&list[0]),
                      (int)RoboHtmlElementClassHeading);
    ElText(list[0], text, sizeof(text));
    RoboCheckStrEq(text, "RoboticelaOS Runtime");

    RoboCheckEqInt((int)HtmlElementClassification(&list[1]),
                      (int)RoboHtmlElementClassParagraph);
    ElText(list[1], text, sizeof(text));
    RoboCheckStrEq(text, "Independent HTML rendering is working.");

    ElText(list[2], text, sizeof(text));
    RoboCheckStrEq(text, "RoboGraphics + RoboWindow + RoboHTML");

    RoboCheckEqInt((int)HtmlElementClassification(&list[3]),
                      (int)RoboHtmlElementClassControl);
    ElText(list[3], text, sizeof(text));
    RoboCheckStrEq(text, "Demo Button");

    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheck(report.ok);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */

void TestRegisterHtmlElement(void)
{
    TestRegister("html.element.namespace_helpers",
                       TestElNamespaceHelpers);
    TestRegister("html.element.classification_helpers",
                       TestElClassificationHelpers);
    TestRegister("html.element.create_info_init",
                       TestElCreateInfoInit);
    TestRegister("html.element.create_basic", TestElCreateBasic);
    TestRegister("html.element.create_validation",
                       TestElCreateValidation);
    TestRegister("html.element.create_refused",
                       TestElCreateRefusedAfterBuildAndFailure);
    TestRegister("html.element.identity", TestElIdentityAndValidity);
    TestRegister("html.element.tag_names", TestElTagNames);
    TestRegister("html.element.state", TestElStateBits);
    TestRegister("html.element.relationships", TestElRelationships);
    TestRegister("html.element.nesting", TestElNestingAndSiblings);
    TestRegister("html.element.ownership", TestElOwnershipRules);
    TestRegister("html.element.attributes", TestElAttributes);
    TestRegister("html.element.start_end_tags",
                       TestElStartAndEndTags);
    TestRegister("html.element.malformed", TestElMalformedRecovery);
    TestRegister("html.element.unicode_text", TestElUnicodeText);
    TestRegister("html.element.teardown", TestElTeardown);
    TestRegister("html.element.large_tree_teardown",
                       TestElLargeTreeTeardown);
    TestRegister("html.element.tree_dump", TestElTreeDump);
    TestRegister("html.element.demo_document", TestElDemoDocumentTree);
}
