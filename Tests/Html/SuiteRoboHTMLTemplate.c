/*
 * RoboHTML Phase 17.7 template suite (html.template.*).
 *
 * The claim this suite has to make good is a *shape* claim — after a parse, a
 * template element has no light children and its markup lives in a parentless
 * fragment — plus the two consequences that make it worth having: content is
 * inert (unreachable from the document root, so lookup, traversal and anything
 * else that walks from the root cannot see it), and a deep clone of it can be
 * inserted into the tree as a working subtree.
 *
 * So the decisive cases are `html.template.inert` (a lookup that must *not*
 * find a node inside the template) and `html.template.insert_clone` (a clone
 * that becomes a real, audited part of the document).
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLDomQuery.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLTemplate.h"
#include "RoboHTML/RoboHTMLVersion.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture (one live document at a time, as the other Html suites do)   */
/* ------------------------------------------------------------------ */

static HtmlDocumentId TplLiveDocument = RoboHtmlDocumentIdNone;

static void TplFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static HtmlDocumentId TplDoc(void)
{
    TplFixtureBegin();
    if (TplLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(TplLiveDocument, NULL), RoboOk);
        TplLiveDocument = RoboHtmlDocumentIdNone;
    }
    RoboCheckEqInt(HtmlDocumentCreate(&TplLiveDocument, NULL), RoboOk);
    RoboCheck(TplLiveDocument != RoboHtmlDocumentIdNone);
    return TplLiveDocument;
}

static void TplDocEnd(void)
{
    if (TplLiveDocument != RoboHtmlDocumentIdNone) {
        RoboCheckEqInt(HtmlDocumentDestroy(TplLiveDocument, NULL), RoboOk);
        TplLiveDocument = RoboHtmlDocumentIdNone;
    }
}

static void TplBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle TplRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

typedef struct tplTagQuery {
    HtmlTag tag;
} TplTagQuery;

static int TplPredicateTag(const HtmlNodeView *view, void *user)
{
    const TplTagQuery *q = (const TplTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

static int TplTryFind(const HtmlNodeHandle *start, HtmlTag tag,
                      HtmlNodeHandle *out)
{
    TplTagQuery q;
    Result r;

    q.tag = tag;
    HtmlNodeHandleClear(out);
    r = HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                          TplPredicateTag, &q, out, NULL);
    if (r == RoboNotFound) {
        return 0;
    }
    RoboCheckEqInt(r, RoboOk);
    return HtmlNodeHandleIsWellformed(out) ? 1 : 0;
}

static HtmlNodeHandle TplFind(const HtmlNodeHandle *start, HtmlTag tag)
{
    HtmlNodeHandle out;

    RoboCheck(TplTryFind(start, tag, &out));
    return out;
}

typedef struct tplCountQuery {
    HtmlTag tag;
    size_t count;
} TplCountQuery;

static Result TplCountVisitor(const HtmlNodeView *view, size_t depth,
                              void *user)
{
    TplCountQuery *q = (TplCountQuery *)user;

    (void)depth;
    if (HtmlElementIsTag(&view->handle, q->tag)) {
        q->count++;
    }
    return RoboOk;
}

static size_t TplCount(const HtmlNodeHandle *start, HtmlTag tag)
{
    TplCountQuery q;

    q.tag = tag;
    q.count = 0u;
    RoboCheckEqInt(HtmlNodeWalkDescendants(start, RoboHtmlNodeFilterElements,
                                           TplCountVisitor, &q, NULL),
                   RoboOk);
    return q.count;
}

static size_t TplChildCount(const HtmlNodeHandle *node)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(node, &count, NULL), RoboOk);
    return count;
}

static void TplExpectCleanTree(HtmlDocumentId id)
{
    HtmlTreeReport report;

    memset(&report, 0, sizeof(report));
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL), RoboOk);
    RoboCheckMsg(report.ok, "tree invariant audit reported corruption");
    RoboCheckEqInt(report.orphans, 0u);
    RoboCheckEqInt(report.badLinks, 0u);
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

static void TestTemplateCapabilities(void)
{
    HtmlTemplateCapabilities caps;

    memset(&caps, 0xff, sizeof(caps));
    RoboCheckEqInt(HtmlTemplateGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.contentFragment, 1);
    RoboCheckEqInt(caps.parserAdoption, 1);
    RoboCheckEqInt(caps.inertContent, 1);
    RoboCheckEqInt(caps.cloning, 1);
    RoboCheckEqInt(caps.fragmentInsertion, 1);
    RoboCheckEqInt(caps.setContent, 1);
    RoboCheckEqInt(caps.inertnessQuery, 1);
    /* The stated non-goals. */
    RoboCheckEqInt(caps.insertionMode, 0);
    RoboCheckEqInt(caps.declarativeShadowDom, 0);
    RoboCheckEqInt(caps.scriptExecution, 0);
    RoboCheckStrEq(caps.version, RoboHtmlVersionString);

    RoboCheckEqInt(HtmlTemplateGetCapabilities(NULL), RoboInvalidArgument);

    RoboCheckEqInt(HtmlTagFromName(RoboHtmlNamespaceHtml, "template"),
                   RoboHtmlTagTemplate);
}

/* ------------------------------------------------------------------ */
/* The parse adopts                                                    */
/* ------------------------------------------------------------------ */

static void TestTemplateParseAdoption(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle template;
    HtmlNodeHandle content;
    HtmlNodeHandle para;
    size_t adopted = 0u;
    size_t length = 0u;
    const char *text;

    TplBuild(id, "<body>"
                 "<template id=\"row\"><p class=\"row\">One</p>text</template>"
                 "<p id=\"outside\">Outside</p>"
                 "</body>");
    root = TplRoot(id);

    template = TplFind(&root, RoboHtmlTagTemplate);
    /* The template element itself is ordinary and lives in the tree. */
    RoboCheck(HtmlNodeHandleIsWellformed(&template));
    text = HtmlElementAttributeValue(&template, "id", &length);
    RoboCheck(text != NULL && length == 3u);
    RoboCheck(memcmp(text, "row", 3u) == 0);

    /* Its light children are gone: the markup moved into `content`. */
    RoboCheckEqInt((int)TplChildCount(&template), 0);

    RoboCheckEqInt(HtmlElementTemplateContent(&template, &content, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&content));
    {
        HtmlNodeKind kind = RoboHtmlNodeKindDocument;

        RoboCheckEqInt(HtmlNodeHandleResolve(&content, &kind), RoboOk);
        RoboCheckEqInt(kind, RoboHtmlNodeKindFragment);
    }

    /* `content` is a root: no parent, which is what makes it inert. */
    {
        HtmlNodeHandle parent;

        RoboCheckEqInt(HtmlNodeParent(&content, &parent, NULL), RoboNotFound);
    }

    /* The adopted children are exactly the parsed ones, in order: one <p>
     * then one text node. */
    RoboCheckEqInt((int)TplChildCount(&content), 2);
    RoboCheck(TplTryFind(&content, RoboHtmlTagP, &para));
    RoboCheck(HtmlElementIsTag(&para, RoboHtmlTagP));
    {
        HtmlNodeHandle tail;
        HtmlNodeKind kind = RoboHtmlNodeKindDocument;

        RoboCheckEqInt(HtmlNodeNextSibling(&para, &tail, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeHandleResolve(&tail, &kind), RoboOk);
        RoboCheckEqInt(kind, RoboHtmlNodeKindText);
    }

    /* Idempotent: nothing left to adopt, and the fragment is unchanged. */
    RoboCheckEqInt(HtmlDocumentAdoptTemplates(id, &adopted, NULL), RoboOk);
    RoboCheckEqInt((int)adopted, 0);
    {
        HtmlNodeHandle again;

        RoboCheckEqInt(HtmlElementTemplateContent(&template, &again, NULL),
                       RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&again, &content));
        RoboCheckEqInt((int)TplChildCount(&again), 2);
    }

    TplExpectCleanTree(id);
    TplDocEnd();
}

static void TestTemplateMultipleAndNested(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle first;
    HtmlNodeHandle second;
    HtmlNodeHandle content1;
    HtmlNodeHandle content2;
    HtmlNodeHandle inner;
    HtmlNodeHandle innerContent;

    TplBuild(id, "<body>"
                 "<template id=\"a\"><p>A</p></template>"
                 "<template id=\"b\"><template id=\"c\"><p>C</p>"
                 "</template><span>B</span></template>"
                 "</body>");
    root = TplRoot(id);

    /* Two templates at top level, one nested inside the other's content -----
     * the nested one is adopted too, because the collection happens before
     * any of the moves. */
    /* The first top-level template (#a) holds one paragraph. */
    first = TplFind(&root, RoboHtmlTagTemplate);
    RoboCheckEqInt((int)TplChildCount(&first), 0);
    RoboCheckEqInt(HtmlElementTemplateContent(&first, &content1, NULL),
                   RoboOk);
    RoboCheckEqInt((int)TplChildCount(&content1), 1);
    RoboCheck(TplTryFind(&content1, RoboHtmlTagP, &inner));

    /* The second (#b) is the next light sibling of the first, found from the
     * root — which never enters a content fragment. */
    RoboCheckEqInt(HtmlNodeNextSibling(&first, &second, NULL), RoboOk);
    RoboCheck(HtmlElementIsTag(&second, RoboHtmlTagTemplate));
    RoboCheckEqInt((int)TplChildCount(&second), 0);
    RoboCheckEqInt(HtmlElementTemplateContent(&second, &content2, NULL),
                   RoboOk);
    RoboCheckEqInt((int)TplChildCount(&content2), 2);

    /* The nested template (#c) was adopted too, because the collection
     * happens before any of the moves — so it is an ordinary child of #b's
     * content with no children of its own and a content of its own. */
    RoboCheckEqInt(HtmlNodeFirstChild(&content2, &inner, NULL), RoboOk);
    RoboCheck(HtmlElementIsTag(&inner, RoboHtmlTagTemplate));
    RoboCheckEqInt((int)TplChildCount(&inner), 0);
    RoboCheckEqInt(HtmlElementTemplateContent(&inner, &innerContent, NULL),
                   RoboOk);
    RoboCheckEqInt((int)TplChildCount(&innerContent), 1);
    {
        int inert = 0;

        RoboCheckEqInt(HtmlElementTemplateIsInert(&inner, &inert, NULL),
                       RoboOk);
        RoboCheckEqInt(inert, 1);
    }

    /* The count from the root sees the two outer templates only: the nested
     * one is content, not a light child. */
    RoboCheckEqInt((int)TplCount(&root, RoboHtmlTagTemplate), 2);

    TplExpectCleanTree(id);
    TplDocEnd();
}

/* ------------------------------------------------------------------ */
/* Inert                                                              */
/* ------------------------------------------------------------------ */

static void TestTemplateInert(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle template;
    int inert = 0;

    TplBuild(id, "<body>"
                 "<template><p id=\"inside\">Hidden</p></template>"
                 "<p id=\"outside\">Shown</p>"
                 "</body>");
    root = TplRoot(id);
    template = TplFind(&root, RoboHtmlTagTemplate);

    RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                   RoboOk);
    RoboCheckEqInt(inert, 1);

    /*
     * The decisive assertion: document lookup walks from the root, and the
     * template's markup is not on any path from it.
     */
    {
        HtmlNodeHandle found;

        /* A miss is RoboNotFound — the same answer a search that found
         * nothing gives everywhere else in the DOM surface. */
        RoboCheckEqInt(HtmlDocumentGetElementById(id, "inside", &found, NULL),
                       RoboNotFound);
        RoboCheck(!HtmlNodeHandleIsWellformed(&found));
        RoboCheckEqInt(HtmlDocumentGetElementById(id, "outside", &found,
                                                 NULL),
                       RoboOk);
        RoboCheck(HtmlNodeHandleIsWellformed(&found));
    }

    /* Traversal from the body does not reach the template's paragraph. */
    body = TplFind(&root, RoboHtmlTagBody);
    RoboCheckEqInt((int)TplCount(&body, RoboHtmlTagP), 1);

    /* The template element itself is looked up normally. */
    {
        size_t count = 0u;

        RoboCheckEqInt(HtmlDocumentGetElementsByTagName(id, "template",
                                                       NULL, NULL, &count,
                                                       NULL),
                       RoboOk);
        RoboCheckEqInt((int)count, 1);
    }

    TplExpectCleanTree(id);
    TplDocEnd();
}

static void TestTemplateContentCreated(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle template;
    HtmlNodeHandle content;
    HtmlNodeHandle again;
    HtmlNodeHandle para;
    int inert = 0;

    TplBuild(id, "<body><div id=\"host\"></div></body>");
    root = TplRoot(id);

    /* A template built through the DOM rather than the parser. */
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "template", &template, NULL),
                   RoboOk);
    RoboCheckEqInt((int)TplChildCount(&template), 0);

    RoboCheckEqInt(HtmlElementTemplateContent(&template, &content, NULL),
                   RoboOk);
    RoboCheckEqInt((int)TplChildCount(&content), 0);
    /* Asking twice is the same fragment, not a second one. */
    RoboCheckEqInt(HtmlElementTemplateContent(&template, &again, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&again, &content));

    RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                   RoboOk);
    RoboCheckEqInt(inert, 1);

    /* Children appended to the content are in the fragment, not the tree. */
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "p", &para, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&content, &para, NULL), RoboOk);
    RoboCheckEqInt((int)TplChildCount(&content), 1);
    RoboCheckEqInt((int)TplChildCount(&template), 0);
    RoboCheckEqInt((int)TplCount(&root, RoboHtmlTagP), 0);

    /* Adopting now has nothing to do: the content is already the content. */
    {
        size_t adopted = 99u;

        RoboCheckEqInt(HtmlDocumentAdoptTemplates(id, &adopted, NULL),
                       RoboOk);
        RoboCheckEqInt((int)adopted, 0);
        RoboCheckEqInt((int)TplChildCount(&content), 1);
    }

    TplExpectCleanTree(id);
    TplDocEnd();
}

/* ------------------------------------------------------------------ */
/* content replacement                                                 */
/* ------------------------------------------------------------------ */

static void TestTemplateSetContent(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle template;
    HtmlNodeHandle body;
    HtmlNodeHandle replacement;
    HtmlNodeHandle readback;
    HtmlNodeHandle para;

    TplBuild(id, "<body><template><p>Original</p></template></body>");
    root = TplRoot(id);
    body = TplFind(&root, RoboHtmlTagBody);
    template = TplFind(&root, RoboHtmlTagTemplate);

    RoboCheckEqInt(HtmlDocumentCreateFragment(id, &replacement, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlDocumentCreateElement(id, "span", &para, NULL),
                   RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&replacement, &para, NULL), RoboOk);

    RoboCheckEqInt(
        HtmlElementTemplateSetContent(&template, &replacement, NULL), RoboOk);
    RoboCheckEqInt(HtmlElementTemplateContent(&template, &readback, NULL),
                   RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&readback, &replacement));
    RoboCheckEqInt((int)TplChildCount(&readback), 1);
    RoboCheck(HtmlElementIsTag(&para, RoboHtmlTagSpan));

    /* A non-fragment is refused. */
    RoboCheckEqInt(HtmlElementTemplateSetContent(&template, &para, NULL),
                   RoboInvalidArgument);

    /* A fragment that is a child of the document would make content
     * reachable, which is exactly what content must not be. */
    {
        HtmlNodeHandle nested;

        RoboCheckEqInt(HtmlDocumentCreateFragment(id, &nested, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&body, &nested, NULL), RoboOk);
        RoboCheckEqInt(HtmlElementTemplateSetContent(&template, &nested,
                                                     NULL),
                       RoboAlreadyExists);
    }

    /* Cross-document content is refused. */
    {
        HtmlDocumentId second = RoboHtmlDocumentIdNone;
        HtmlNodeHandle otherFragment;

        RoboCheckEqInt(HtmlDocumentCreate(&second, NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentCreateFragment(second, &otherFragment,
                                                  NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlElementTemplateSetContent(&template,
                                                     &otherFragment, NULL),
                       RoboNotSupported);
        RoboCheckEqInt(HtmlDocumentDestroy(second, NULL), RoboOk);
    }

    TplExpectCleanTree(id);
    TplDocEnd();
}

/* ------------------------------------------------------------------ */
/* Cloning and insertion                                               */
/* ------------------------------------------------------------------ */

static void TestTemplateCloneDeep(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle template;
    HtmlNodeHandle content;
    HtmlNodeHandle clone;
    HtmlNodeHandle clonePara;
    size_t length = 0u;

    TplBuild(id, "<body><template><p>Seven</p><span>S</span>"
                 "</template></body>");
    root = TplRoot(id);
    template = TplFind(&root, RoboHtmlTagTemplate);
    RoboCheckEqInt(HtmlElementTemplateContent(&template, &content, NULL),
                   RoboOk);

    /* Shallow clone: a fragment with no children. */
    RoboCheckEqInt(HtmlElementTemplateCloneContent(&template, 0, &clone,
                                                   NULL),
                   RoboOk);
    {
        HtmlNodeKind kind = RoboHtmlNodeKindDocument;

        RoboCheckEqInt(HtmlNodeHandleResolve(&clone, &kind), RoboOk);
        RoboCheckEqInt(kind, RoboHtmlNodeKindFragment);
    }
    RoboCheckEqInt((int)TplChildCount(&clone), 0);

    /* Deep clone: independent nodes, same shape. */
    RoboCheckEqInt(HtmlElementTemplateCloneContent(&template, 1, &clone,
                                                   NULL),
                   RoboOk);
    RoboCheckEqInt((int)TplChildCount(&clone), 2);
    RoboCheck(!HtmlNodeHandleEqual(&clone, &content));
    clonePara = TplFind(&clone, RoboHtmlTagP);
    RoboCheck(HtmlNodeHandleIsWellformed(&clonePara));
    {
        HtmlNodeHandle originalPara = TplFind(&content, RoboHtmlTagP);

        RoboCheck(!HtmlNodeHandleEqual(&clonePara, &originalPara));
    }

    /* Mutating the clone does not touch the original. */
    {
        HtmlNodeHandle cloneText;
        char buffer[16];

        RoboCheckEqInt(HtmlNodeFirstChild(&clonePara, &cloneText, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeTextContentSet(&cloneText, "Changed", NULL),
                       RoboOk);
        memset(buffer, 0, sizeof(buffer));
        RoboCheckEqInt(HtmlNodeTextContentGet(&cloneText, buffer,
                                              sizeof(buffer), &length,
                                              NULL),
                       RoboOk);
        RoboCheckEqInt((int)length, 7);
        RoboCheck(memcmp(buffer, "Changed", 7u) == 0);
    }
    {
        HtmlNodeHandle originalPara = TplFind(&content, RoboHtmlTagP);
        HtmlNodeHandle originalText;
        char buffer[16];
        size_t originalLength = 0u;

        RoboCheckEqInt(HtmlNodeFirstChild(&originalPara, &originalText,
                                          NULL),
                       RoboOk);
        memset(buffer, 0, sizeof(buffer));
        RoboCheckEqInt(HtmlNodeTextContentGet(&originalText, buffer,
                                              sizeof(buffer),
                                              &originalLength, NULL),
                       RoboOk);
        RoboCheckEqInt((int)originalLength, 5);
        RoboCheck(memcmp(buffer, "Seven", 5u) == 0);
    }

    /* The original content is still inert; the clone is a root too. */
    {
        int inert = 0;

        RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                       RoboOk);
        RoboCheckEqInt(inert, 1);
    }

    TplExpectCleanTree(id);
    TplDocEnd();
}

static void TestTemplateInsertClone(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle template;
    HtmlNodeHandle clone;
    size_t inserted = 0u;

    TplBuild(id, "<body><template><p class=\"row\">Row</p>"
                 "<p class=\"row\">Row</p></template></body>");
    root = TplRoot(id);
    body = TplFind(&root, RoboHtmlTagBody);
    template = TplFind(&root, RoboHtmlTagTemplate);
    RoboCheckEqInt((int)TplCount(&body, RoboHtmlTagP), 0);

    RoboCheckEqInt(HtmlElementTemplateCloneContent(&template, 1, &clone,
                                                   NULL),
                   RoboOk);
    RoboCheckEqInt((int)TplChildCount(&clone), 2);

    /* The insert after clone that the phase exists for. */
    RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&body, &clone, &inserted,
                                                  NULL),
                   RoboOk);
    RoboCheckEqInt((int)inserted, 2);
    /* The fragment is emptied, not destroyed. */
    RoboCheckEqInt((int)TplChildCount(&clone), 0);
    RoboCheck(HtmlNodeHandleResolve(&clone, NULL) == RoboOk);

    /* The inserted nodes are now real: reachable from the root, matched by
     * document lookup, and counted. */
    RoboCheckEqInt((int)TplCount(&body, RoboHtmlTagP), 2);
    RoboCheckEqInt((int)TplCount(&root, RoboHtmlTagP), 2);
    {
        size_t matches = 0u;

        RoboCheckEqInt(HtmlDocumentGetElementsByClassName(id, "row",
                                                         NULL, NULL,
                                                         &matches, NULL),
                       RoboOk);
        RoboCheckEqInt((int)matches, 2);
    }

    /* The template's own content is untouched and still inert. */
    {
        int inert = 0;
        HtmlNodeHandle content;

        RoboCheckEqInt(HtmlElementTemplateContent(&template, &content, NULL),
                       RoboOk);
        RoboCheckEqInt((int)TplChildCount(&content), 2);
        RoboCheckEqInt(HtmlElementTemplateIsInert(&template, &inert, NULL),
                       RoboOk);
        RoboCheckEqInt(inert, 1);
    }

    TplExpectCleanTree(id);
    TplDocEnd();
}

static void TestTemplateFragmentInsertionRefusals(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle template;
    HtmlNodeHandle content;
    size_t inserted = 99u;

    TplBuild(id, "<body><template><p>One</p></template></body>");
    root = TplRoot(id);
    body = TplFind(&root, RoboHtmlTagBody);
    template = TplFind(&root, RoboHtmlTagTemplate);
    RoboCheckEqInt(HtmlElementTemplateContent(&template, &content, NULL),
                   RoboOk);

    /* Not a fragment. */
    RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&body, &body, &inserted,
                                                  NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt((int)inserted, 0);

    /* A fragment cannot receive its own children. */
    RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&content, &content,
                                                  &inserted, NULL),
                   RoboInvalidArgument);

    /* An empty fragment inserts nothing and is not an error. */
    {
        HtmlNodeHandle empty;

        RoboCheckEqInt(HtmlDocumentCreateFragment(id, &empty, NULL), RoboOk);
        RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&body, &empty,
                                                      &inserted, NULL),
                       RoboOk);
        RoboCheckEqInt((int)inserted, 0);
    }

    /* A parent that cannot hold children. */
    {
        HtmlNodeHandle text;

        RoboCheckEqInt(HtmlDocumentCreateTextNode(id, "t", &text, NULL),
                       RoboOk);
        RoboCheckEqInt(HtmlNodeAppendFragmentChildren(&text, &content,
                                                      &inserted, NULL),
                       RoboNotSupported);
        RoboCheckEqInt((int)inserted, 0);
    }

    /* Nothing moved: the content still holds its paragraph. */
    RoboCheckEqInt((int)TplChildCount(&content), 1);
    RoboCheckEqInt((int)TplCount(&body, RoboHtmlTagP), 0);

    TplExpectCleanTree(id);
    TplDocEnd();
}

/* ------------------------------------------------------------------ */
/* Errors and lifetime                                                 */
/* ------------------------------------------------------------------ */

static void TestTemplateErrors(void)
{
    HtmlDocumentId id = TplDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle para;
    HtmlNodeHandle content;
    size_t adopted = 0u;

    TplBuild(id, "<body><p id=\"plain\">Plain</p>"
                 "<template><i>x</i></template></body>");
    root = TplRoot(id);

    RoboCheckEqInt(HtmlElementTemplateContent(NULL, &content, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(HtmlElementTemplateContent(&root, NULL, NULL),
                   RoboInvalidArgument);

    /* A non-template element is a refusal, not an empty fragment. */
    para = TplFind(&root, RoboHtmlTagP);
    RoboCheckEqInt(HtmlElementTemplateContent(&para, &content, NULL),
                   RoboNotSupported);
    {
        int inert = 0;

        RoboCheckEqInt(HtmlElementTemplateIsInert(&para, &inert, NULL),
                       RoboNotSupported);
        RoboCheckEqInt(HtmlElementTemplateIsInert(&para, NULL, NULL),
                       RoboInvalidArgument);
    }
    RoboCheckEqInt(HtmlElementTemplateCloneContent(&para, 1, &content, NULL),
                   RoboNotSupported);

    /* A stale handle after the document is gone. */
    {
        HtmlNodeHandle template = TplFind(&root, RoboHtmlTagTemplate);
        HtmlNodeHandle stale = template;
        HtmlNodeHandle stalePara = para;

        TplDocEnd();
        RoboCheckEqInt(HtmlElementTemplateContent(&stale, &content, NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlElementTemplateContent(&stalePara, &content,
                                                  NULL),
                       RoboNotFound);
        RoboCheckEqInt(HtmlDocumentAdoptTemplates(id, &adopted, NULL),
                       RoboNotFound);
    }

    TplDocEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlTemplate(void);

void TestRegisterHtmlTemplate(void)
{
    TestRegister("html.template.capabilities", TestTemplateCapabilities);
    TestRegister("html.template.parse_adoption", TestTemplateParseAdoption);
    TestRegister("html.template.multiple_and_nested",
                 TestTemplateMultipleAndNested);
    TestRegister("html.template.inert", TestTemplateInert);
    TestRegister("html.template.content_created", TestTemplateContentCreated);
    TestRegister("html.template.set_content", TestTemplateSetContent);
    TestRegister("html.template.clone_deep", TestTemplateCloneDeep);
    TestRegister("html.template.insert_clone", TestTemplateInsertClone);
    TestRegister("html.template.fragment_insertion_refusals",
                 TestTemplateFragmentInsertionRefusals);
    TestRegister("html.template.errors", TestTemplateErrors);
}
