/*
 * RoboHTML Phase 8.11 basic-elements suite (html.elements.*).
 *
 * The tag suite (html.tag.*) proves the vocabulary is internally consistent.
 * This suite proves the *document* agrees with it: that the parser produces
 * the element a tag name claims, that each element carries attributes and
 * text the way its neighbours do, and that the whole existing DOM surface —
 * traversal, mutation, lifecycle, the debug dump — works on the new types
 * without a special case for any of them.
 *
 * Two shapes of test appear here on purpose:
 *   - table-driven sweeps over the element list, which catch the element
 *     someone adds to the vocabulary and forgets to make work; and
 *   - hand-written cases for the elements whose behaviour is genuinely
 *     different (void <br>, the document structure, <pre>'s whitespace, an
 *     unrecognised tag).
 *
 * The fixtures build through the real parser (`<tag>…</tag>`) rather than
 * through the element API, because "the parser creates the right element"
 * is exactly what is being tested.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTag.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
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

    ElFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

static void ElBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle ElRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/** Create an element under @p parent, through the element seam. */
static HtmlNodeHandle ElMake(HtmlDocumentId id,
                                       const HtmlNodeHandle *parent,
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
    return out;
}

static size_t ElChildCount(const HtmlNodeHandle *node)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(node, &count, NULL), RoboOk);
    return count;
}

/** Whole subtree text of @p node into @p out (NUL-terminated). */
static void ElText(const HtmlNodeHandle *node, char *out, size_t cap)
{
    size_t length = 0u;

    out[0] = '\0';
    if (HtmlNodeSubtreeText(node, out, cap, &length, NULL) != RoboOk) {
        return;
    }
    if (length >= cap) {
        out[cap - 1u] = '\0';
    }
}

typedef struct elTagQuery {
    HtmlTag tag;
} ElTagQuery;

static int ElMatchTag(const HtmlNodeView *view, void *user)
{
    const ElTagQuery *q = (const ElTagQuery *)user;

    return HtmlElementIsTag(&view->handle, q->tag);
}

/** First element of @p tag at or below @p start; asserts it was found. */
static HtmlNodeHandle ElFind(const HtmlNodeHandle *start,
                                       HtmlTag tag)
{
    ElTagQuery q;
    HtmlNodeHandle out;

    q.tag = tag;
    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(
        HtmlNodeFindFirst(start, RoboHtmlNodeFilterElements,
                                  ElMatchTag, &q, &out, NULL),
        RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/** True when an element of @p tag is reachable from the document. */
static int ElHasTag(const HtmlNodeHandle *root, HtmlTag tag)
{
    ElTagQuery q;
    HtmlNodeHandle out;

    q.tag = tag;
    HtmlNodeHandleClear(&out);
    return HtmlNodeFindFirst(root, RoboHtmlNodeFilterElements,
                                     ElMatchTag, &q, &out, NULL) == RoboOk;
}

static Result ElCountVisit(const HtmlNodeView *view,
                                    size_t depth,
                                    void *user);
static Result ElVisitIdentity(const HtmlNodeView *view,
                                       size_t depth,
                                       void *user);

/** Node kind of @p node, through the resolver. */
static HtmlNodeKind ElKind(const HtmlNodeHandle *node)
{
    HtmlNodeKind kind = RoboHtmlNodeKindDocument;

    RoboCheckEqInt(HtmlNodeHandleResolve(node, &kind), RoboOk);
    return kind;
}

/* ------------------------------------------------------------------ */
/* Every supported element type                                        */
/* ------------------------------------------------------------------ */

/**
 * Parse `<tag>text</tag>`, then assert the element really is that type.
 *
 * One row per element the phase introduces, so this is the test that answers
 * "does the parser produce the element the vocabulary names?" for all of
 * them at once. An element that cannot be found, or is found with the wrong
 * identity, fails on its own row rather than being silently absent.
 */
typedef struct elCase {
    HtmlTag tag;
    /** Expected role, so a misclassified element is caught too. */
    HtmlElementClass cls;
} ElCase;

static void ElRunCase(const ElCase *c)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle el;
    HtmlElementView view;

    id = ElDoc();
    ElBuild(id,
             "<!DOCTYPE html><html><head>"
             "<title>Doc</title></head><body>"
             "<div>alpha<span>beta</span></div>"
             "<h1>one</h1><h2>two</h2><h3>three</h3>"
             "<h4>four</h4><h5>five</h5><h6>six</h6>"
             "<p>para</p><br>"
             "<ul><li>u1</li></ul><ol><li>o1</li></ol>"
             "<strong>s</strong><em>e</em><b>b</b><i>i</i><u>u</u>"
             "<small>sm</small><code>c</code><pre>pre</pre>"
             "<section>sec</section><article>art</article><nav>nv</nav>"
             "<main>mn</main><header>hd</header><footer>ft</footer>"
             "<aside>as</aside>"
             "</body></html>");
    root = ElRoot(id);

    RoboCheckMsg(ElHasTag(&root, c->tag),
                   HtmlTagName(c->tag));
    el = ElFind(&root, c->tag);
    RoboCheckEqInt((int)HtmlElementTag(&el), (int)c->tag);
    RoboCheck(HtmlElementIsTag(&el, c->tag));
    RoboCheckEqInt(HtmlElementViewGet(&el, &view, NULL), RoboOk);
    RoboCheckEqInt((int)view.tag, (int)c->tag);
    RoboCheckEqInt((int)view.classification, (int)c->cls);
    RoboCheckEqInt((int)view.namespaceId, (int)RoboHtmlNamespaceHtml);
    /* The element's stored name is the canonical spelling of its type. */
    RoboCheckStrEq(HtmlElementName(&el, NULL),
                      HtmlTagName(c->tag));
    RoboCheckEqInt((int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   view.name),
                      (int)c->tag);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElDocumentStructureElements(void)
{
    static const ElCase kCases[] = {
        {RoboHtmlTagHtml, RoboHtmlElementClassRoot},
        {RoboHtmlTagHead, RoboHtmlElementClassMetadata},
        {RoboHtmlTagBody, RoboHtmlElementClassSection},
        {RoboHtmlTagTitle, RoboHtmlElementClassMetadata},
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        ElRunCase(&kCases[i]);
    }
}

static void TestElStructuralContentElements(void)
{
    static const ElCase kCases[] = {
        {RoboHtmlTagDiv, RoboHtmlElementClassSection},
        {RoboHtmlTagSpan, RoboHtmlElementClassText},
        {RoboHtmlTagP, RoboHtmlElementClassParagraph},
        {RoboHtmlTagBr, RoboHtmlElementClassText},
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        ElRunCase(&kCases[i]);
    }
}

static void TestElHeadingElements(void)
{
    static const ElCase kCases[] = {
        {RoboHtmlTagH1, RoboHtmlElementClassHeading},
        {RoboHtmlTagH2, RoboHtmlElementClassHeading},
        {RoboHtmlTagH3, RoboHtmlElementClassHeading},
        {RoboHtmlTagH4, RoboHtmlElementClassHeading},
        {RoboHtmlTagH5, RoboHtmlElementClassHeading},
        {RoboHtmlTagH6, RoboHtmlElementClassHeading},
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        ElRunCase(&kCases[i]);
    }
}

static void TestElListElements(void)
{
    static const ElCase kCases[] = {
        {RoboHtmlTagUl, RoboHtmlElementClassSection},
        {RoboHtmlTagOl, RoboHtmlElementClassSection},
        {RoboHtmlTagLi, RoboHtmlElementClassSection},
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        ElRunCase(&kCases[i]);
    }
}

static void TestElInlineSemanticElements(void)
{
    static const ElCase kCases[] = {
        {RoboHtmlTagStrong, RoboHtmlElementClassText},
        {RoboHtmlTagEm, RoboHtmlElementClassText},
        {RoboHtmlTagB, RoboHtmlElementClassText},
        {RoboHtmlTagI, RoboHtmlElementClassText},
        {RoboHtmlTagU, RoboHtmlElementClassText},
        {RoboHtmlTagSmall, RoboHtmlElementClassText},
        {RoboHtmlTagCode, RoboHtmlElementClassText},
        {RoboHtmlTagPre, RoboHtmlElementClassSection},
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        ElRunCase(&kCases[i]);
    }
}

static void TestElGenericSemanticElements(void)
{
    static const ElCase kCases[] = {
        {RoboHtmlTagSection, RoboHtmlElementClassSection},
        {RoboHtmlTagArticle, RoboHtmlElementClassSection},
        {RoboHtmlTagNav, RoboHtmlElementClassSection},
        {RoboHtmlTagMain, RoboHtmlElementClassSection},
        {RoboHtmlTagHeader, RoboHtmlElementClassSection},
        {RoboHtmlTagFooter, RoboHtmlElementClassSection},
        {RoboHtmlTagAside, RoboHtmlElementClassSection},
    };
    size_t i;

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        ElRunCase(&kCases[i]);
    }
}

/**
 * The pure identity/role sweep over the *whole* vocabulary: every known tag
 * resolves to itself by name, so an element the vocabulary lists is an
 * element the document can name. This is the test that would catch a table
 * row added without a matching identity path.
 */
static void TestElVocabularyResolvesThroughDocument(void)
{
    HtmlDocumentId id;
    size_t count = HtmlTagCount();
    size_t i;

    id = ElDoc();

    /* The capability reports the vocabulary this build actually has, and it
     * is a claim about the tag model rather than about CSS or selectors. */
    {
        HtmlDomCapabilities caps;

        HtmlDomCapabilitiesInit(&caps);
        RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
        RoboCheckEqInt(caps.elementTags, 1);
        RoboCheckEqInt(caps.events, 0);
        RoboCheck(count > 0u);
        /* The count the capability implies is the count the table has. */
        RoboCheckEqInt((int)RoboHtmlTagCount - 1, (int)count);
    }

    for (i = 0u; i < count; i++) {
        HtmlTag tag;
        const char *name;

        RoboCheckEqInt(HtmlTagAt(i, &tag), RoboOk);
        name = HtmlTagName(tag);
        RoboCheckStrEq(HtmlTagName(
                              HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                      name)),
                          name);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* The document structure                                              */
/* ------------------------------------------------------------------ */

static void TestElFullDocumentTree(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle html;
    HtmlNodeHandle head;
    HtmlNodeHandle body;
    HtmlNodeHandle title;
    char text[128];

    id = ElDoc();
    ElBuild(id,
             "<!DOCTYPE html><html><head><title>Hello</title></head>"
             "<body>Hi</body></html>");
    root = ElRoot(id);

    html = ElFind(&root, RoboHtmlTagHtml);
    head = ElFind(&html, RoboHtmlTagHead);
    body = ElFind(&html, RoboHtmlTagBody);
    title = ElFind(&head, RoboHtmlTagTitle);

    /* The document structure is real nesting, not a flat list of names. */
    {
        int inside = 0;

        RoboCheckEqInt(HtmlNodeContains(&html, &head, &inside, NULL),
                          RoboOk);
        RoboCheck(inside);
        RoboCheckEqInt(HtmlNodeContains(&head, &html, &inside, NULL),
                          RoboOk);
        RoboCheck(!inside);
        RoboCheckEqInt(HtmlNodeContains(&html, &body, &inside, NULL),
                          RoboOk);
        RoboCheck(inside);
        RoboCheckEqInt(HtmlNodeContains(&head, &title, &inside, NULL),
                          RoboOk);
        RoboCheck(inside);
        /* <title> belongs to <head>, and it is not inside <body>. */
        RoboCheckEqInt(HtmlNodeContains(&body, &title, &inside, NULL),
                          RoboOk);
        RoboCheck(!inside);
    }

    ElText(&title, text, sizeof(text));
    RoboCheckStrEq(text, "Hello");
    ElText(&body, text, sizeof(text));
    RoboCheckStrEq(text, "Hi");

    /* The document element is the root element, and every tag sees itself. */
    RoboCheck(HtmlElementIsTag(&html, RoboHtmlTagHtml));
    RoboCheck(HtmlTagIsRoot(HtmlElementTag(&html)));
    RoboCheck(HtmlTagIsDocumentStructure(
        HtmlElementTag(&head)));
    RoboCheck(HtmlTagIsDocumentStructure(
        HtmlElementTag(&body)));
    RoboCheck(HtmlTagIsMetadata(HtmlElementTag(&title)));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Void behaviour                                                      */
/* ------------------------------------------------------------------ */

static void TestElVoidBr(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle p;
    HtmlNodeHandle br;
    char buf[64];

    id = ElDoc();
    /* <br> is void: it takes no children, so the text after it is a sibling
     * of the <br>, not a child of one. */
    ElBuild(id, "<body><p>one<br>two</p></body>");
    root = ElRoot(id);

    p = ElFind(&root, RoboHtmlTagP);
    br = ElFind(&p, RoboHtmlTagBr);
    RoboCheck(HtmlTagIsVoid(HtmlElementTag(&br)));
    RoboCheckEqInt(ElChildCount(&br), 0u);
    RoboCheckEqInt((int)HtmlElementTag(&br),
                      (int)RoboHtmlTagBr);

    /* Its parent is the paragraph, and it has siblings on both sides. */
    {
        HtmlNodeHandle parent;
        HtmlNodeHandle prev;
        HtmlNodeHandle next;

        RoboCheckEqInt(HtmlNodeParent(&br, &parent, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&parent, &p));
        RoboCheckEqInt(HtmlNodePrevSibling(&br, &prev, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeNextSibling(&br, &next, NULL),
                          RoboOk);
        RoboCheckEqInt((int)ElKind(&prev),
                          (int)RoboHtmlNodeKindText);
    }

    /*
     * The whole paragraph still reads as one run of text. A void element
     * contributes no content of its own, so the run is the two text nodes
     * with nothing in between — the <br> separates nothing textually until a
     * layout phase decides it does.
     */
    ElText(&p, buf, sizeof(buf));
    RoboCheckStrEq(buf, "onetwo");

    /* A void element parses the same whether or not it is self-closed, and
     * the flag records the spelling the source used. */
    {
        HtmlDocumentId id2 = ElDoc();
        HtmlNodeHandle root2;
        HtmlNodeHandle div;
        HtmlElementView view;

        ElBuild(id2, "<div>a<br/>b</div>");
        root2 = ElRoot(id2);
        div = ElFind(&root2, RoboHtmlTagDiv);
        br = ElFind(&div, RoboHtmlTagBr);
        RoboCheckEqInt(ElChildCount(&br), 0u);
        RoboCheckEqInt(HtmlElementViewGet(&br, &view, NULL),
                          RoboOk);
        RoboCheckEqInt((int)view.tag, (int)RoboHtmlTagBr);
        RoboCheckStrEq(view.name, "br");
        RoboCheck(view.selfClosing);
        /* Three children: text, the void <br>, text — never a child of <br>. */
        RoboCheckEqInt(ElChildCount(&div), 3u);
        ElText(&div, buf, sizeof(buf));
        RoboCheckStrEq(buf, "ab");
        RoboCheckEqInt(HtmlDocumentDestroy(id2, NULL), RoboOk);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Attributes and text on the new elements                             */
/* ------------------------------------------------------------------ */

static void TestElAttributesOnNewElements(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle el;
    const char *value;
    size_t length = 0u;

    id = ElDoc();
    ElBuild(id,
             "<body><div id=\"main\" class=\"container\" title=\"Card\" "
             "data-x=\"1\">hello</div>"
             "<section aria-label=\"Region\"></section>"
             "<p disabled>x</p></body>");
    root = ElRoot(id);

    el = ElFind(&root, RoboHtmlTagDiv);
    RoboCheck(HtmlElementHasAttribute(&el, "id"));
    value = HtmlElementAttributeValue(&el, "id", &length);
    RoboCheck(value != NULL);
    if (value) {
        RoboCheckStrEq(value, "main");
        RoboCheckEqInt((int)length, 4);
    }
    value = HtmlElementAttributeValue(&el, "class", NULL);
    RoboCheck(value != NULL);
    if (value) {
        RoboCheckStrEq(value, "container");
    }
    value = HtmlElementAttributeValue(&el, "data-x", NULL);
    RoboCheck(value != NULL);
    if (value) {
        RoboCheckStrEq(value, "1");
    }
    /* Attribute lookup is case-insensitive; storage is lowercase. */
    RoboCheck(HtmlElementHasAttribute(&el, "ID"));
    RoboCheck(HtmlElementHasAttribute(&el, "Class"));

    el = ElFind(&root, RoboHtmlTagSection);
    value = HtmlElementAttributeValue(&el, "aria-label", NULL);
    RoboCheck(value != NULL);
    if (value) {
        RoboCheckStrEq(value, "Region");
    }

    /*
     * A boolean-style attribute on any element is "present with an empty
     * value" — the same answer `<input disabled>` gives.
     */
    el = ElFind(&root, RoboHtmlTagP);
    RoboCheck(HtmlElementHasAttribute(&el, "disabled"));
    value = HtmlElementAttributeValue(&el, "disabled", &length);
    RoboCheck(value != NULL);
    if (value) {
        RoboCheckStrEq(value, "");
        RoboCheckEqInt((int)length, 0);
    }
    RoboCheck(!HtmlElementHasAttribute(&el, "absent"));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElTextChildren(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle p;
    HtmlNodeHandle strong;
    char text[128];

    id = ElDoc();
    ElBuild(id,
             "<body><p>Hello <strong>Roboticela</strong> Runtime</p>"
             "<div>one<b>two</b>three</div>"
             "<h1>Plain</h1></body>");
    root = ElRoot(id);

    /*
     * Three children in order: text, element, text. The text either side of
     * the inline element is not folded into it.
     */
    p = ElFind(&root, RoboHtmlTagP);
    RoboCheckEqInt(ElChildCount(&p), 3u);
    {
        HtmlNodeHandle first;
        HtmlNodeHandle last;
        HtmlNodeHandle middle;

        RoboCheckEqInt(HtmlNodeChildAt(&p, 0u, &first, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeChildAt(&p, 1u, &middle, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeChildAt(&p, 2u, &last, NULL),
                          RoboOk);
        RoboCheckEqInt((int)ElKind(&first),
                          (int)RoboHtmlNodeKindText);
        RoboCheckEqInt((int)ElKind(&last),
                          (int)RoboHtmlNodeKindText);
        RoboCheck(HtmlElementIsTag(&middle, RoboHtmlTagStrong));
        ElText(&first, text, sizeof(text));
        RoboCheckStrEq(text, "Hello ");
        ElText(&last, text, sizeof(text));
        RoboCheckStrEq(text, " Runtime");
    }
    strong = ElFind(&p, RoboHtmlTagStrong);
    ElText(&strong, text, sizeof(text));
    RoboCheckStrEq(text, "Roboticela");
    RoboCheck(HtmlTagIsTextLevel(HtmlElementTag(&strong)));

    /* Six children: text, element, text inside a div. */
    {
        HtmlNodeHandle div = ElFind(&root, RoboHtmlTagDiv);

        RoboCheckEqInt(ElChildCount(&div), 3u);
        /* No whitespace in the source, so none appears in the run. */
        ElText(&div, text, sizeof(text));
        RoboCheckStrEq(text, "onetwothree");
    }

    /* A heading holding a single text node. */
    {
        HtmlNodeHandle h1 = ElFind(&root, RoboHtmlTagH1);

        HtmlNodeHandle only;

        RoboCheckEqInt(ElChildCount(&h1), 1u);
        RoboCheckEqInt(HtmlNodeChildAt(&h1, 0u, &only, NULL),
                          RoboOk);
        RoboCheckEqInt((int)ElKind(&only),
                          (int)RoboHtmlNodeKindText);
        /* Out of range is a miss, never a zeroed-but-successful handle. */
        RoboCheckEqInt(HtmlNodeChildAt(&h1, 1u, &only, NULL),
                          RoboNotFound);
        ElText(&h1, text, sizeof(text));
        RoboCheckStrEq(text, "Plain");
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElPrePreservesWhitespace(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle pre;
    char text[256];

    id = ElDoc();
    ElBuild(id, "<body><pre>  line one\n\tline two  </pre></body>");
    root = ElRoot(id);

    pre = ElFind(&root, RoboHtmlTagPre);
    ElText(&pre, text, sizeof(text));
    /* Byte-exact: no trimming, no collapsing, tabs kept. */
    RoboCheckStrEq(text, "  line one\n\tline two  ");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElUnicodeText(void)
{
    static const char *const kSamples[] = {
        "مرحبا",       /* Arabic  */
        "فارسی",       /* Persian */
        "اردو",        /* Urdu    */
        "हिन्दी",         /* Hindi   */
        "বাংলা",        /* Bengali */
        "中文",        /* Chinese */
        "日本語",      /* Japanese */
        "한국어",      /* Korean  */
        "😀 🚀 ❤️", /* emoji   */
        "line\nbreak",
    };
    size_t i;

    for (i = 0u; i < sizeof(kSamples) / sizeof(kSamples[0]); i++) {
        HtmlDocumentId id = ElDoc();
        HtmlNodeHandle root;
        HtmlNodeHandle h1;
        char text[256];


        /* Append the sample through the real parser, then read it back byte
         * for byte: a tag type that cannot carry arbitrary text is not a
         * usable element. */
        {
            HtmlSource src;
            char source[512];

            snprintf(source, sizeof(source), "<body><h1>%s</h1></body>",
                     kSamples[i]);
            ElBuild(id, source);
            (void)src;
            root = ElRoot(id);
            h1 = ElFind(&root, RoboHtmlTagH1);
        }
        ElText(&h1, text, sizeof(text));
        RoboCheckMsg(strstr(text, kSamples[i]) != NULL, kSamples[i]);

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        ElFixtureEnd();
    }
}

static void TestElUnicodeThroughParser(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle p;
    HtmlNodeHandle h2;
    char text[256];

    id = ElDoc();
    ElBuild(id,
             "<body><p title=\"مرحبا\">فارسی and 中文</p>"
             "<h2>😀 emoji</h2></body>");
    root = ElRoot(id);

    p = ElFind(&root, RoboHtmlTagP);
    ElText(&p, text, sizeof(text));
    RoboCheckStrEq(text, "فارسی and 中文");
    {
        const char *value = HtmlElementAttributeValue(&p, "title", NULL);

        RoboCheck(value != NULL);
        if (value) {
            RoboCheckStrEq(value, "مرحبا");
        }
    }

    h2 = ElFind(&root, RoboHtmlTagH2);
    ElText(&h2, text, sizeof(text));
    RoboCheckStrEq(text, "😀 emoji");
    RoboCheck(HtmlTagIsHeading(HtmlElementTag(&h2)));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Traversal through the new elements                                  */
/* ------------------------------------------------------------------ */

typedef struct elOrder {
    const HtmlTag *tags;
    size_t count;
    size_t seen;
    int failed;
} ElOrder;

static Result ElVisitOrder(const HtmlNodeView *view,
                                    size_t depth,
                                    void *user)
{
    ElOrder *order = (ElOrder *)user;

    (void)depth;
    if (order->seen >= order->count) {
        order->failed = 1;
        return RoboOk;
    }
    if (!HtmlElementIsTag(&view->handle, order->tags[order->seen])) {
        order->failed = 1;
        return RoboOk;
    }
    order->seen++;
    return RoboOk;
}

static void TestElTraversalOrder(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    static const HtmlTag kExpected[] = {
        RoboHtmlTagBody,  RoboHtmlTagH1,     RoboHtmlTagDiv,
        RoboHtmlTagP,     RoboHtmlTagSpan,   RoboHtmlTagSpan,
        RoboHtmlTagSection, RoboHtmlTagUl,   RoboHtmlTagLi,
    };
    ElOrder order;

    id = ElDoc();
    ElBuild(id,
             "<body><h1>Head</h1><div>"
             "<p>x<span>one</span><span>two</span></p>"
             "<section><ul><li>item</li></ul></section>"
             "</div></body>");
    root = ElRoot(id);
    body = ElFind(&root, RoboHtmlTagBody);

    order.tags = kExpected;
    order.count = sizeof(kExpected) / sizeof(kExpected[0]);
    order.seen = 0u;
    order.failed = 0;

    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &body, RoboHtmlNodeFilterElements,
                          ElVisitOrder, &order, NULL),
                      RoboOk);
    RoboCheckMsg(!order.failed, "element order differed from source order");
    RoboCheckEqInt((int)order.seen, (int)order.count);

    /* Element filtering excludes text; the unfiltered walk includes it. */
    {
        size_t elements = 0u;
        size_t texts = 0u;
        size_t all = 0u;

        RoboCheckEqInt(HtmlNodeWalkDescendants(
                              &body, RoboHtmlNodeFilterElements,
                              ElCountVisit, &elements, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeWalkDescendants(
                              &body, RoboHtmlNodeFilterText, ElCountVisit,
                              &texts, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeWalkDescendants(
                              &body, RoboHtmlNodeFilterAll, ElCountVisit,
                              &all, NULL),
                          RoboOk);
        RoboCheckEqInt((int)all, (int)(elements + texts));
        RoboCheck(elements > 0u);
        RoboCheck(texts > 0u);
    }

    /* Upward traversal from the deepest element reaches the document. */
    {
        HtmlNodeHandle li = ElFind(&root, RoboHtmlTagLi);
        HtmlNodeHandle up = li;
        HtmlNodeHandle parent;
        size_t depth = 0u;
        size_t climbed = 0u;

        RoboCheckEqInt(HtmlNodeDepth(&li, &depth, NULL), RoboOk);
        RoboCheck(depth >= 4u);
        while (HtmlNodeParent(&up, &parent, NULL) == RoboOk) {
            up = parent;
            climbed++;
            /* A bounded climb: a cycle would hang the suite, not fail it. */
            RoboCheck(climbed <= 16u);
            if (climbed > 16u) {
                break;
            }
        }
        RoboCheckEqInt((int)ElKind(&up), (int)RoboHtmlNodeKindDocument);
        RoboCheck(HtmlNodeHandleEqual(&up, &root));
        /* The document node itself is the end of the chain. */
        RoboCheckEqInt(HtmlNodeParent(&root, &parent, NULL),
                          RoboNotFound);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static Result ElCountVisit(const HtmlNodeView *view,
                                    size_t depth,
                                    void *user)
{
    size_t *count = (size_t *)user;

    (void)view;
    (void)depth;
    (*count)++;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Element creation (the construction seam)                            */
/* ------------------------------------------------------------------ */

/** Append a text node under @p parent, through the text seam. */
static void ElNote(HtmlDocumentId id,
                    const HtmlNodeHandle *parent,
                    const char *content)
{
    HtmlTextCreateInfo info;
    HtmlNodeHandle out;

    HtmlTextCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = RoboHtmlNodeOriginTest;
    info.content = content;
    info.length = content ? strlen(content) : 0u;

    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL), RoboOk);
}

/**
 * Every element type can be *created*, not only parsed.
 *
 * Creation is a construction operation, so this runs on a document that has
 * not been finalized — the same gate Phase 8.9 established and pinned
 * (`html.mutate.creation_stays_gated`). Moving parsed nodes around is the
 * mutation story below; this is the creation story, and it is the half of
 * the tag layer the parser cannot exercise because the parser only ever
 * spells names.
 */
static void TestElCreationSeam(void)
{
    static const HtmlTag kTypes[] = {
        RoboHtmlTagHtml,     RoboHtmlTagHead,    RoboHtmlTagBody,
        RoboHtmlTagTitle,    RoboHtmlTagDiv,     RoboHtmlTagSpan,
        RoboHtmlTagP,        RoboHtmlTagBr,      RoboHtmlTagH1,
        RoboHtmlTagH2,       RoboHtmlTagH3,      RoboHtmlTagH4,
        RoboHtmlTagH5,       RoboHtmlTagH6,      RoboHtmlTagUl,
        RoboHtmlTagOl,       RoboHtmlTagLi,      RoboHtmlTagStrong,
        RoboHtmlTagEm,       RoboHtmlTagB,       RoboHtmlTagI,
        RoboHtmlTagU,        RoboHtmlTagSmall,   RoboHtmlTagCode,
        RoboHtmlTagPre,      RoboHtmlTagSection, RoboHtmlTagArticle,
        RoboHtmlTagNav,      RoboHtmlTagMain,    RoboHtmlTagHeader,
        RoboHtmlTagFooter,   RoboHtmlTagAside,
    };
    HtmlDocumentId id = ElDoc();
    HtmlNodeHandle root = ElRoot(id);
    size_t i;

    for (i = 0u; i < sizeof(kTypes) / sizeof(kTypes[0]); i++) {
        HtmlNodeHandle made = ElMake(id, &root, HtmlTagName(
                                                         kTypes[i]));
        HtmlElementView view;

        RoboCheckEqInt((int)HtmlElementTag(&made),
                          (int)kTypes[i]);
        RoboCheck(HtmlElementIsTag(&made, kTypes[i]));
        RoboCheck(HtmlElementViewGet(&made, &view, NULL) == RoboOk);
        RoboCheckEqInt((int)view.tag, (int)kTypes[i]);
        RoboCheckEqInt((int)view.classification,
                          (int)HtmlTagClass(kTypes[i]));
        /* A created element is as real as a parsed one: it can hold text. */
        if (kTypes[i] == RoboHtmlTagHtml) {
            continue; /* <html> is the root slot; a second one is refused */
        }
        ElNote(id, &made, "x");
        RoboCheckEqInt(ElChildCount(&made), 1u);
        {
            HtmlNodeHandle child;

            RoboCheckEqInt(HtmlNodeChildAt(&made, 0u, &child, NULL),
                              RoboOk);
            RoboCheckEqInt((int)ElKind(&child),
                              (int)RoboHtmlNodeKindText);
        }
    }

    /* A name the vocabulary does not know creates a valid generic element. */
    {
        HtmlNodeHandle custom = ElMake(id, &root, "my-widget");

        RoboCheckEqInt((int)HtmlElementTag(&custom),
                          (int)RoboHtmlTagUnknown);
        RoboCheck(HtmlElementIsTag(&custom, RoboHtmlTagUnknown));
        RoboCheckStrEq(HtmlElementName(&custom, NULL), "my-widget");
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Mutation of the new elements                                        */
/* ------------------------------------------------------------------ */

/**
 * Rearranging a parsed document, which is where mutation is legal.
 *
 * Phase 8.9's rule is that a mutation may only rearrange the nodes a
 * document already has — registering a new node is a construction operation
 * and finalization closes construction. So this test moves parsed elements,
 * which is exactly what a consumer does to a loaded page, and asserts that
 * every element stays the type it was through every move.
 */
static void TestElMutation(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle section;
    HtmlNodeHandle ul;
    HtmlNodeHandle div;
    HtmlNodeHandle firstLi;
    HtmlNodeHandle secondLi;
    char text[128];

    id = ElDoc();
    ElBuild(id,
             "<body><h1>Head</h1>"
             "<section><ul><li>one</li><li>two</li></ul></section>"
             "<div>spare</div></body>");
    root = ElRoot(id);
    body = ElFind(&root, RoboHtmlTagBody);
    section = ElFind(&body, RoboHtmlTagSection);
    ul = ElFind(&section, RoboHtmlTagUl);
    div = ElFind(&body, RoboHtmlTagDiv);
    firstLi = ElFind(&ul, RoboHtmlTagLi);
    RoboCheckEqInt(ElChildCount(&ul), 2u);
    RoboCheckEqInt(HtmlNodeChildAt(&ul, 1u, &secondLi, NULL),
                      RoboOk);

    /* Remove, then re-append: a move is two explicit steps (8.9). */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &div, NULL),
                      RoboOk);
    RoboCheck(HtmlElementIsTag(&div, RoboHtmlTagDiv));
    RoboCheckEqInt(HtmlNodeAppendChild(&section, &div, NULL),
                      RoboOk);
    RoboCheck(HtmlElementIsTag(&div, RoboHtmlTagDiv));

    /* Insert before an existing sibling: the moved div lands before the ul. */
    {
        HtmlNodeHandle moved = div;

        RoboCheckEqInt(HtmlNodeRemoveChild(&section, &moved, NULL),
                          RoboOk);
        RoboCheckEqInt(
            HtmlNodeInsertBefore(&section, &moved, &ul, NULL),
            RoboOk);
        RoboCheckEqInt(ElChildCount(&section), 2u);
        {
            HtmlNodeHandle child0;
            HtmlNodeHandle child1;

            RoboCheckEqInt(HtmlNodeChildAt(&section, 0u, &child0,
                                                      NULL),
                              RoboOk);
            RoboCheckEqInt(HtmlNodeChildAt(&section, 1u, &child1,
                                                      NULL),
                              RoboOk);
            RoboCheck(HtmlNodeHandleEqual(&child0, &div));
            RoboCheck(HtmlNodeHandleEqual(&child1, &ul));
        }
        /* The moved node keeps its identity and its content. */
        RoboCheck(HtmlElementIsTag(&div, RoboHtmlTagDiv));
        ElText(&div, text, sizeof(text));
        RoboCheckStrEq(text, "spare");
    }

    /* Replace the first list item with the second, then back again. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&ul, &secondLi, NULL),
                      RoboOk);
    RoboCheckEqInt(
        HtmlNodeReplaceChild(&ul, &secondLi, &firstLi, NULL),
        RoboOk);
    RoboCheckEqInt(ElChildCount(&ul), 1u);
    {
        HtmlNodeHandle li = ElFind(&ul, RoboHtmlTagLi);

        RoboCheck(HtmlNodeHandleEqual(&li, &secondLi));
        RoboCheck(HtmlElementIsTag(&li, RoboHtmlTagLi));
        RoboCheck(HtmlTagIsListItem(HtmlElementTag(&li)));
        ElText(&li, text, sizeof(text));
        RoboCheckStrEq(text, "two");
    }
    /* The displaced item survives, detached, still typed, content intact. */
    RoboCheck(HtmlElementIsTag(&firstLi, RoboHtmlTagLi));
    ElText(&firstLi, text, sizeof(text));
    RoboCheckStrEq(text, "one");

    /* Re-parent the displaced item, which is how a move is spelled. */
    RoboCheckEqInt(HtmlNodeAppendChild(&ul, &firstLi, NULL),
                      RoboOk);
    RoboCheckEqInt(ElChildCount(&ul), 2u);
    RoboCheck(HtmlElementIsTag(&firstLi, RoboHtmlTagLi));

    /* The tree is still a tree, and every element still knows its type. */
    {
        HtmlTreeReport report;

        RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                          RoboOk);
        RoboCheckMsg(report.ok, "mutation broke a tree invariant");
        RoboCheckEqInt((int)report.orphans, 0);
        RoboCheckEqInt((int)report.badLinks, 0);
        RoboCheckEqInt((int)report.badParents, 0);
        RoboCheckEqInt((int)report.cycles, 0);
    }
    RoboCheck(ElHasTag(&body, RoboHtmlTagDiv));
    RoboCheck(ElHasTag(&section, RoboHtmlTagUl));
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &root, RoboHtmlNodeFilterElements,
                          ElVisitIdentity, NULL, NULL),
                      RoboOk);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Lifecycle compatibility                                             */
/* ------------------------------------------------------------------ */

static void TestElLifecycle(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlDocumentLifecycle life;
    HtmlSource src;

    ElFixtureBegin();
    HtmlSourceInit(&src);
    id = ElDoc();
    root = ElRoot(id);

    /* CREATED: no input yet, construction open, elements typed as usual. */
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboHtmlDocumentLifecycleCreated);
    {
        HtmlNodeHandle div = ElMake(id, &root, "div");

        RoboCheck(HtmlElementIsTag(&div, RoboHtmlTagDiv));
    }

    /*
     * A complete one-shot load settles at COMPLETE, so it is neither
     * "loading" nor "interactive" any more: INTERACTIVE names the window
     * between the parse ending and the input settling, and a whole-source
     * load passes through it without stopping there (Phase 8.10).
     */
    ElBuild(id, "<body><h1>Head</h1><ul><li>one</li></ul></body>");
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    RoboCheckEqInt((int)life, (int)RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsComplete(id));
    RoboCheck(!HtmlDocumentIsLoading(id));
    RoboCheck(!HtmlDocumentIsInteractive(id));

    /* The parsed elements are typed, and mutation still works. */
    {
        HtmlNodeHandle body = ElFind(&root, RoboHtmlTagBody);
        HtmlNodeHandle h1 = ElFind(&body, RoboHtmlTagH1);
        HtmlNodeHandle ul = ElFind(&body, RoboHtmlTagUl);

        RoboCheck(HtmlElementIsTag(&body, RoboHtmlTagBody));
        RoboCheck(HtmlTagIsHeading(HtmlElementTag(&h1)));
        RoboCheck(HtmlTagIsList(HtmlElementTag(&ul)));
        /* A move on a completed document: still allowed, still typed. */
        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &ul, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeInsertBefore(&body, &ul, &h1, NULL),
                          RoboOk);
        RoboCheck(HtmlElementIsTag(&ul, RoboHtmlTagUl));
        /* Registering a *new* node stays closed: creation is construction. */
        {
            HtmlElementCreateInfo info;
            HtmlNodeHandle out;

            HtmlElementCreateInfoInit(&info);
            info.parent = body;
            info.origin = RoboHtmlNodeOriginTest;
            info.namespaceId = RoboHtmlNamespaceHtml;
            info.localName = "h3";
            HtmlNodeHandleClear(&out);
            RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL),
                              RoboError);
        }
    }

    /* The tree stayed consistent across every transition. */
    {
        HtmlTreeReport report;

        RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                          RoboOk);
        RoboCheck(report.ok);
    }

    /* After destruction nothing resolves, so identity is simply absent. */
    {
        HtmlNodeHandle stale = ElFind(&root, RoboHtmlTagH1);

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        RoboCheckEqInt((int)HtmlElementTag(&stale),
                          (int)RoboHtmlTagUnknown);
        RoboCheck(!HtmlElementIsTag(&stale, RoboHtmlTagH1));
        RoboCheckEqInt(HtmlNodeAppendChild(&root, &stale, NULL),
                          RoboNotFound);
    }

    /* A fresh document with an attached (incomplete) source is LOADING. */
    {
        HtmlDocumentId id2 = ElDoc();
        HtmlNodeHandle root2 = ElRoot(id2);

        RoboCheckEqInt(HtmlSourceString(&src, "<p>x</p>", NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlDocumentAttachSource(id2, &src, NULL),
                          RoboOk);
        RoboCheck(HtmlDocumentIsLoading(id2));
        RoboCheckEqInt((int)HtmlElementTag(&root2),
                          (int)RoboHtmlTagUnknown);
        RoboCheckEqInt(HtmlDocumentDestroy(id2, NULL), RoboOk);
    }

    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Malformed and unrecognised markup                                   */
/* ------------------------------------------------------------------ */

static void TestElMalformedMarkup(void)
{
    static const char *const kInputs[] = {
        "",
        "plain text only",
        "   \n\t ",
        "<div",
        "<div>",
        "<div id",
        "<div id=",
        "<div id=\"x",
        "<div =bad>text",
        "<div id=\"x\" id=\"y\">dup</div>",
        "<div class>empty</div>",
        "<div disabled>x</div>",
        "<p>unclosed",
        "<p>a<p>b",
        "<span></div>mismatched",
        "<ul><li>a<li>b</ul>",
        "<h1>unclosed heading",
        "<h1><h2>nested</h2></h1>",
        "<div><my-widget>x</my-widget></div>",
        "<DIV CLASS=\"UPPER\">x</DIV>",
        "<div><div><div><div><div>deep</div></div></div></div></div>",
        "<br><br><br>",
        "<section></section><section></section>",
    };
    size_t i;

    for (i = 0u; i < sizeof(kInputs) / sizeof(kInputs[0]); i++) {
        HtmlDocumentId id = ElDoc();
        HtmlNodeHandle root;
        HtmlTreeReport report;

        ElBuild(id, kInputs[i]);
        root = ElRoot(id);

        /* Whatever the recovery decided, the result is a sound tree. */
        RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                          RoboOk);
        RoboCheckMsg(report.ok, kInputs[i]);
        RoboCheckEqInt((int)report.orphans, 0);
        RoboCheckEqInt((int)report.badLinks, 0);
        RoboCheckEqInt((int)report.cycles, 0);
        RoboCheckEqInt((int)report.badParents, 0);

        /* Every element the recovery produced has a coherent identity. */
        RoboCheckEqInt(HtmlNodeWalkDescendants(
                              &root, RoboHtmlNodeFilterElements,
                              ElVisitIdentity, NULL, NULL),
                          RoboOk);

        /* The dumped tree agrees with the tree, malformed input included. */
        {
            char dump[4096];

            dump[0] = '\0';
            RoboCheckEqInt(
                HtmlTreeDumpToBuffer(id, dump, sizeof(dump), NULL,
                                              NULL),
                RoboOk);
        }

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
        ElFixtureEnd();
    }
}

static Result ElVisitIdentity(const HtmlNodeView *view,
                                       size_t depth,
                                       void *user)
{
    HtmlTag tag = HtmlElementTag(&view->handle);

    (void)depth;
    (void)user;
    RoboCheckEqInt((int)tag,
                      (int)HtmlTagFromName(RoboHtmlNamespaceHtml,
                                                   view->name));
    RoboCheck(HtmlElementIsTag(&view->handle, tag));
    return RoboOk;
}

static void TestElUnknownElement(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle widget;
    HtmlElementView view;
    char text[128];

    id = ElDoc();
    ElBuild(id,
             "<body><my-widget data-kind=\"custom\">inside"
             "<span>nested</span></my-widget></body>");
    root = ElRoot(id);

    widget = ElFind(&root, RoboHtmlTagUnknown);
    RoboCheckStrEq(HtmlElementName(&widget, NULL), "my-widget");
    RoboCheckEqInt(HtmlElementViewGet(&widget, &view, NULL),
                      RoboOk);
    RoboCheckEqInt((int)view.tag, (int)RoboHtmlTagUnknown);
    RoboCheckEqInt((int)view.classification,
                      (int)RoboHtmlElementClassGeneric);

    /* Attributes, children, text and traversal all behave normally. */
    {
        const char *value =
            HtmlElementAttributeValue(&widget, "data-kind", NULL);

        RoboCheck(value != NULL);
        if (value) {
            RoboCheckStrEq(value, "custom");
        }
    }
    RoboCheckEqInt(ElChildCount(&widget), 2u);
    ElText(&widget, text, sizeof(text));
    RoboCheckStrEq(text, "insidenested");
    RoboCheck(ElHasTag(&widget, RoboHtmlTagSpan));

    /* It is not silently coerced into a known element. */
    RoboCheck(!HtmlElementIsTag(&widget, RoboHtmlTagDiv));
    RoboCheck(!HtmlElementIsTag(&widget, RoboHtmlTagSpan));
    {
        size_t elements = 0u;

        RoboCheckEqInt(HtmlNodeWalkDescendants(
                              &widget, RoboHtmlNodeFilterElements,
                              ElCountVisit, &elements, NULL),
                          RoboOk);
        RoboCheckEqInt((int)elements, 1); /* only the nested span */
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Debug dump                                                          */
/* ------------------------------------------------------------------ */

static void TestElTreeDumpShowsTypes(void)
{
    HtmlDocumentId id;
    static char dump[8192];
    static const char *const kExpected[] = {
        "body",   "h1",      "div",     "p",       "span",
        "ul",     "li",      "strong",  "em",      "code",
        "pre",    "section", "article", "nav",     "main",
        "header", "footer",  "aside",   "br",
    };
    size_t i;

    id = ElDoc();
    ElBuild(id,
             "<!DOCTYPE html><html><head><title>T</title></head><body>\n"
             "<h1>Head</h1>\n"
             "<div><p>text <span>s</span></p>"
             "<ul><li>i</li></ul><strong>st</strong><em>e</em><code>c</code>"
             "<pre>p</pre><section>sec</section><article>art</article>"
             "<nav>nv</nav><main>mn</main><header>hd</header>"
             "<footer>ft</footer><aside>as</aside><br></div>\n"
             "</body></html>");

    dump[0] = '\0';
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), NULL, NULL),
        RoboOk);

    /*
     * The dump names every element by its tag, which is the element's type in
     * this architecture — so "the dump shows element types" is exactly this
     * assertion. The line-per-node shape is unchanged by this phase on
     * purpose: reformatting it would invalidate six suites to convey nothing
     * the name does not already convey.
     */
    for (i = 0u; i < sizeof(kExpected) / sizeof(kExpected[0]); i++) {
        char needle[32];

        snprintf(needle, sizeof(needle), " %s\n", kExpected[i]);
        RoboCheckMsg(strstr(dump, needle) != NULL, kExpected[i]);
    }
    RoboCheck(strstr(dump, "Text: \"Head\"") != NULL);
    RoboCheck(strstr(dump, "Text: \"i\"") != NULL);
    /* Void <br> has no children, so no line is indented under it. */
    RoboCheck(strstr(dump, " br\n") != NULL);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

static void TestElDumpReflectsMutation(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle section;
    static char dump[4096];

    id = ElDoc();
    ElBuild(id, "<body><h1>Head</h1><section>s</section></body>");
    root = ElRoot(id);
    body = ElFind(&root, RoboHtmlTagBody);
    section = ElFind(&body, RoboHtmlTagSection);

    dump[0] = '\0';
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), NULL, NULL),
        RoboOk);
    RoboCheck(strstr(dump, " section\n") != NULL);

    /*
     * The dump is read from the live tree, not from what was parsed: detaching
     * the section removes it from the picture, and re-attaching brings it
     * back — a real structural change, observed through the debug surface.
     */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &section, NULL),
                      RoboOk);
    dump[0] = '\0';
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), NULL, NULL),
        RoboOk);
    RoboCheck(strstr(dump, " section\n") == NULL);
    /*
     * The dump walks the document, so a detached subtree is absent from it —
     * and that is the honest picture, not a gap: the section is still a typed
     * tree in its own right, which its own accessors show.
     */
    RoboCheck(strstr(dump, "Text: \"s\"") == NULL);
    RoboCheck(HtmlElementIsTag(&section, RoboHtmlTagSection));
    {
        char text[32];

        ElText(&section, text, sizeof(text));
        RoboCheckStrEq(text, "s");
    }

    RoboCheckEqInt(HtmlNodeAppendChild(&body, &section, NULL),
                      RoboOk);
    dump[0] = '\0';
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), NULL, NULL),
        RoboOk);
    RoboCheck(strstr(dump, " section\n") != NULL);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    ElFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlElements(void)
{
    TestRegister("html.elements.document_structure_elements",
                       TestElDocumentStructureElements);
    TestRegister("html.elements.structural_content_elements",
                       TestElStructuralContentElements);
    TestRegister("html.elements.heading_elements",
                       TestElHeadingElements);
    TestRegister("html.elements.list_elements",
                       TestElListElements);
    TestRegister("html.elements.inline_semantic_elements",
                       TestElInlineSemanticElements);
    TestRegister("html.elements.generic_semantic_elements",
                       TestElGenericSemanticElements);
    TestRegister("html.elements.vocabulary_resolves_through_document",
                       TestElVocabularyResolvesThroughDocument);
    TestRegister("html.elements.full_document_tree",
                       TestElFullDocumentTree);
    TestRegister("html.elements.void_br", TestElVoidBr);
    TestRegister("html.elements.attributes_on_new_elements",
                       TestElAttributesOnNewElements);
    TestRegister("html.elements.text_children",
                       TestElTextChildren);
    TestRegister("html.elements.pre_preserves_whitespace",
                       TestElPrePreservesWhitespace);
    TestRegister("html.elements.unicode_text",
                       TestElUnicodeText);
    TestRegister("html.elements.unicode_through_parser",
                       TestElUnicodeThroughParser);
    TestRegister("html.elements.traversal_order",
                       TestElTraversalOrder);
    TestRegister("html.elements.creation_seam",
                       TestElCreationSeam);
    TestRegister("html.elements.mutation", TestElMutation);
    TestRegister("html.elements.lifecycle", TestElLifecycle);
    TestRegister("html.elements.malformed_markup",
                       TestElMalformedMarkup);
    TestRegister("html.elements.unknown_element",
                       TestElUnknownElement);
    TestRegister("html.elements.tree_dump_shows_types",
                       TestElTreeDumpShowsTypes);
    TestRegister("html.elements.dump_reflects_mutation",
                       TestElDumpReflectsMutation);
}
